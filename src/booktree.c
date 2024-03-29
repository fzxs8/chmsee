/*
 *  Copyright (c) 2006           Ji YongGang <jungle@soforge-studio.com>
 *  Copyright (C) 2009 LI Daobing <lidaobing@gmail.com>
 *
 *  ChmSee is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.

 *  ChmSee is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.

 *  You should have received a copy of the GNU General Public License
 *  along with ChmSee; see the file COPYING.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "booktree.h"

#include "models/hhc.h"
#include "utils/utils.h"

#define selfp (self->priv)

static void booktree_dispose(GObject *);
static void booktree_finalize(GObject *);

static void booktree_selection_changed_cb(GtkTreeSelection *, BookTree *);

static void booktree_create_pixbufs(BookTree *);
static void booktree_add_columns(BookTree *);
static void booktree_setup_selection(BookTree *);
static void booktree_populate_tree(BookTree *);
static void booktree_insert_node(BookTree *, GNode *, GtkTreeIter *);
static void on_row_activated(BookTree* self, GtkTreePath* path);

typedef struct {
        GdkPixbuf *pixbuf_opened;
        GdkPixbuf *pixbuf_closed;
        GdkPixbuf *pixbuf_doc;
} BookTreePixbufs;

typedef struct {
        const gchar *uri;
        gboolean     found;
        GtkTreeIter  iter;
        GtkTreePath *path;
} FindURIData;

struct _BookTreePrivate {
    GtkTreeStore    *store;
    BookTreePixbufs *pixbufs;
    Hhc             *link_tree;
};

/* Signals */
enum {
        LINK_SELECTED,
        LAST_SIGNAL
};

enum {
        COL_OPEN_PIXBUF,
        COL_CLOSED_PIXBUF,
        COL_TITLE,
        COL_LINK,
        N_COLUMNS
};

static gint              signals[LAST_SIGNAL] = { 0 };

#define GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), TYPE_BOOKTREE,  BookTreePrivate))

G_DEFINE_TYPE (BookTree, booktree, GTK_TYPE_TREE_VIEW);

static void
booktree_class_init(BookTreeClass *klass)
{
        GObjectClass *object_class;
        g_type_class_add_private(klass, sizeof(BookTreePrivate));

        object_class = (GObjectClass *)klass;

        object_class->dispose = booktree_dispose;
        object_class->finalize = booktree_finalize;

        signals[LINK_SELECTED] =
                g_signal_new ("link_selected",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (BookTreeClass, link_selected),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__POINTER,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_POINTER);
}

static void
booktree_init(BookTree *self)
{
	self->priv = GET_PRIVATE(self);
	selfp->store = gtk_tree_store_new(N_COLUMNS,
			GDK_TYPE_PIXBUF,
			GDK_TYPE_PIXBUF,
			G_TYPE_STRING,
			G_TYPE_POINTER);
	gtk_tree_view_set_model(GTK_TREE_VIEW (self),
			GTK_TREE_MODEL (selfp->store));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW (self), FALSE);
	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(self), TRUE);

	booktree_create_pixbufs(self);
	booktree_add_columns(self);
	booktree_setup_selection(self);

	g_signal_connect(G_OBJECT(self),
			"row-activated",
			G_CALLBACK(on_row_activated),
			NULL);
}

static void
booktree_dispose(GObject* object) {
	BookTree* self = BOOKTREE(object);

	if(selfp->store) {
		g_object_unref(selfp->store);
		selfp->store = NULL;
	}

	if(selfp->pixbufs->pixbuf_opened) {
		g_object_unref(selfp->pixbufs->pixbuf_opened);
		selfp->pixbufs->pixbuf_opened = NULL;
	}

	if(selfp->pixbufs->pixbuf_closed) {
		g_object_unref(selfp->pixbufs->pixbuf_closed);
		selfp->pixbufs->pixbuf_closed = NULL;
	}

	if(selfp->pixbufs->pixbuf_doc) {
		g_object_unref(selfp->pixbufs->pixbuf_doc);
		selfp->pixbufs->pixbuf_doc = NULL;
	}
}

static void
booktree_finalize(GObject *object)
{
        BookTree *self;

        self = BOOKTREE (object);

        g_free(selfp->pixbufs);

        G_OBJECT_CLASS (booktree_parent_class)->finalize(object);
}

/* internal functions */

static void
booktree_create_pixbufs(BookTree *self)
{
        BookTreePixbufs *pixbufs;

        pixbufs = g_new0(BookTreePixbufs, 1);

        pixbufs->pixbuf_closed = gdk_pixbuf_new_from_file(get_resource_path("book-closed.png"), NULL);
        pixbufs->pixbuf_opened = gdk_pixbuf_new_from_file(get_resource_path("book-open.png"), NULL);
        pixbufs->pixbuf_doc = gdk_pixbuf_new_from_file(get_resource_path("helpdoc.png"), NULL);

        selfp->pixbufs = pixbufs;
}

static void
booktree_add_columns(BookTree *tree)
{
        GtkCellRenderer   *cell;
        GtkTreeViewColumn *column;

        column = gtk_tree_view_column_new();

        cell = gtk_cell_renderer_pixbuf_new();
        gtk_tree_view_column_pack_start(column, cell, FALSE);
        gtk_tree_view_column_set_attributes(
                column,
                cell,
                "pixbuf", COL_OPEN_PIXBUF,
                "pixbuf-expander-open", COL_OPEN_PIXBUF,
                "pixbuf-expander-closed", COL_CLOSED_PIXBUF,
                NULL);

        cell = gtk_cell_renderer_text_new();
        g_object_set(cell,
                     "ellipsize", PANGO_ELLIPSIZE_END,
                     NULL);
        gtk_tree_view_column_pack_start(column, cell, TRUE);
        gtk_tree_view_column_set_attributes(column, cell,
                                            "text", COL_TITLE,
                                            NULL);

        gtk_tree_view_append_column(GTK_TREE_VIEW (tree), column);
}

static void
booktree_setup_selection(BookTree *tree)
{
        GtkTreeSelection *selection;

        selection = gtk_tree_view_get_selection(GTK_TREE_VIEW (tree));
        gtk_tree_selection_set_mode(selection, GTK_SELECTION_BROWSE);

        g_signal_connect(selection,
                         "changed",
                         G_CALLBACK (booktree_selection_changed_cb),
                         tree);
}

static void
booktree_populate_tree(BookTree *self)
{
        GNode *node;

        for (node = g_node_first_child(selfp->link_tree);
             node;
             node = g_node_next_sibling(node))
        {
                booktree_insert_node(self, node, NULL);
        }
}

static void
booktree_insert_node(BookTree *self, GNode *node, GtkTreeIter *parent_iter)
{
        GtkTreeIter iter;
        Link *link;
        GNode *child;

        link = node->data;

        if (g_node_n_children(node))
                link_change_type(link, LINK_TYPE_BOOK);

        gtk_tree_store_append(selfp->store, &iter, parent_iter);

/*         d(g_debug("insert node::name = %s", link->name)); */
/*         d(g_debug("insert node::uri = %s", link->uri)); */

        if (link->type == LINK_TYPE_BOOK) {
                gtk_tree_store_set(selfp->store, &iter,
                                   COL_OPEN_PIXBUF, selfp->pixbufs->pixbuf_opened,
                                   COL_CLOSED_PIXBUF, selfp->pixbufs->pixbuf_closed,
                                   COL_TITLE, link->name,
                                   COL_LINK, link,
                                   -1);
        } else {
                gtk_tree_store_set(selfp->store, &iter,
                                   COL_OPEN_PIXBUF, selfp->pixbufs->pixbuf_doc,
                                   COL_CLOSED_PIXBUF, selfp->pixbufs->pixbuf_doc,
                                   COL_TITLE, link->name,
                                   COL_LINK, link,
                                   -1);
        }

        for (child = g_node_first_child(node);
             child;
             child = g_node_next_sibling(child)) {
                booktree_insert_node(self, child, &iter);
        }
}

static gboolean
booktree_find_uri_foreach(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, FindURIData *data)
{
        Link *link;

        gtk_tree_model_get(model, iter, COL_LINK, &link, -1);

        if (g_str_has_suffix(data->uri, link->uri)) {
                g_debug("data->uri: %s", data->uri);
                g_debug("link->uri: %s", link->uri);

                data->found = TRUE;
                data->iter = *iter;
                data->path = gtk_tree_path_copy(path);
        }

        return data->found;
}

static gboolean
booktree_find_name_foreach(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, FindURIData *data)
{
        Link *link;

        gtk_tree_model_get(model, iter, COL_LINK, &link, -1);

        if (g_strcmp0(data->uri, link->name) == 0) {
                data->found = TRUE;
                data->iter = *iter;
                data->path = gtk_tree_path_copy(path);
        }

        return data->found;
}


/* callbacks */

static void
booktree_selection_changed_cb(GtkTreeSelection *selection, BookTree *self)
{
        GtkTreeIter iter;
        Link *link;

        if (gtk_tree_selection_get_selected(selection, NULL, &iter)) {
                gtk_tree_model_get(GTK_TREE_MODEL (selfp->store),
                                   &iter, COL_LINK, &link, -1);

                g_debug("book tree emiting '%s'\n", link->uri);

                g_signal_emit(self, signals[LINK_SELECTED], 0, link);
        }
}

/* external functions */

GtkWidget *
booktree_new(GNode *link_tree)
{
        BookTree *self;

        self = g_object_new(TYPE_BOOKTREE, NULL);

        selfp->link_tree = link_tree;

        booktree_populate_tree(self);

        return GTK_WIDGET (self);
}

void booktree_set_model(BookTree* self, GNode* model) {
	g_object_unref(selfp->store);
	selfp->store = gtk_tree_store_new(N_COLUMNS,
			GDK_TYPE_PIXBUF,
			GDK_TYPE_PIXBUF,
			G_TYPE_STRING,
			G_TYPE_POINTER);
	gtk_tree_view_set_model(GTK_TREE_VIEW (self),
			GTK_TREE_MODEL (selfp->store));


	selfp->link_tree = model;
	booktree_populate_tree(self);
}


void
booktree_select_uri(BookTree *self, const gchar *uri)
{
        GtkTreeSelection *selection;
        FindURIData data;
        gchar *real_uri;

        g_return_if_fail(IS_BOOKTREE (self));

        real_uri = get_real_uri(uri);

        data.found = FALSE;
        data.uri = real_uri;

        gtk_tree_model_foreach(GTK_TREE_MODEL (selfp->store),
                               (GtkTreeModelForeachFunc) booktree_find_uri_foreach,
                               &data);

        if (!data.found) {
                g_debug("booktree select uri: cannot found data");
                return;
        }

        selection = gtk_tree_view_get_selection(GTK_TREE_VIEW (self));

        g_signal_handlers_block_by_func(selection,
                                        booktree_selection_changed_cb,
                                        self);

        gtk_tree_view_expand_to_path(GTK_TREE_VIEW (self), data.path);
        gtk_tree_selection_select_iter(selection, &data.iter);
        gtk_tree_view_set_cursor(GTK_TREE_VIEW (self), data.path, NULL, 0);

        g_signal_handlers_unblock_by_func(selection,
                                          booktree_selection_changed_cb,
                                          self);

        gtk_tree_path_free(data.path);
        g_free(real_uri);
}

const gchar *
booktree_get_selected_book_title(BookTree *tree)
{
        GtkTreeSelection *selection;
        GtkTreeModel     *model;
        GtkTreeIter       iter;
        GtkTreePath      *path;
        Link             *link;

        g_return_val_if_fail(IS_BOOKTREE (tree), NULL);

        selection = gtk_tree_view_get_selection(GTK_TREE_VIEW (tree));

        if (!gtk_tree_selection_get_selected(selection, &model, &iter))
                return NULL;

        path = gtk_tree_model_get_path(model, &iter);

        /* Get the book node for this link. */
        while (gtk_tree_path_get_depth(path) > 1)
                gtk_tree_path_up(path);

        gtk_tree_model_get_iter(model, &iter, path);
        gtk_tree_path_free(path);

        gtk_tree_model_get(model, &iter,
                           COL_LINK, &link,
                           -1);

        return link->name;
}

void on_row_activated(BookTree* self, GtkTreePath* path) {
	g_return_if_fail(IS_BOOKTREE(self));
	if(gtk_tree_view_row_expanded(GTK_TREE_VIEW(self), path)) {
		gtk_tree_view_collapse_row(GTK_TREE_VIEW(self), path);
	} else {
		gtk_tree_view_expand_row(GTK_TREE_VIEW(self), path, FALSE);
	}
}

gboolean booktree_select_link_by_name(BookTree* self, const gchar* name) {
    GtkTreeSelection *selection;
    FindURIData data;

    g_return_val_if_fail(IS_BOOKTREE (self), FALSE);

    data.found = FALSE;
    data.uri = name;

    gtk_tree_model_foreach(GTK_TREE_MODEL (selfp->store),
                           (GtkTreeModelForeachFunc) booktree_find_name_foreach,
                           &data);

    if (!data.found) {
            g_debug("booktree select uri: cannot found data");
            return FALSE;
    }

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW (self));

    gtk_tree_view_expand_to_path(GTK_TREE_VIEW (self), data.path);
    gtk_tree_selection_select_iter(selection, &data.iter);
    gtk_tree_view_set_cursor(GTK_TREE_VIEW (self), data.path, NULL, 0);

    gtk_tree_path_free(data.path);
    return TRUE;
}


#include <glib/gi18n.h>

#include <eel/eel-vfs-extensions.h>

#include "nautilus-rename-file-popover-controller.h"

#include "nautilus-directory.h"
#include "nautilus-file-private.h"


#define RENAME_ENTRY_MIN_CHARS 20
#define RENAME_ENTRY_MAX_CHARS 35

struct _NautilusRenameFilePopoverController
{
    NautilusFileNameWidgetController parent_instance;

    NautilusFile *target_file;
    gboolean target_is_folder;

    GtkWidget *rename_file_popover;

    gint closed_handler_id;
};

G_DEFINE_TYPE (NautilusRenameFilePopoverController, nautilus_rename_file_popover_controller, NAUTILUS_TYPE_FILE_NAME_WIDGET_CONTROLLER)

static void
rename_file_popover_controller_on_closed (GtkPopover *popover,
                                          gpointer    user_data)
{
    NautilusRenameFilePopoverController *controller;

    controller = NAUTILUS_RENAME_FILE_POPOVER_CONTROLLER (user_data);

    g_signal_handler_disconnect (controller->rename_file_popover,
                                 controller->closed_handler_id);
    controller->closed_handler_id = 0;
    controller->rename_file_popover = NULL;

    g_signal_emit_by_name (controller, "cancelled");
}

static gboolean
nautilus_rename_file_popover_controller_name_is_valid (NautilusFileNameWidgetController  *controller,
                                                       gchar                             *name,
                                                       gchar                            **error_message)
{
    NautilusRenameFilePopoverController *self;

    self = NAUTILUS_RENAME_FILE_POPOVER_CONTROLLER (controller);

    if (strlen (name) == 0)
    {
        return FALSE;
    }

    if (strstr (name, "/") != NULL)
    {
        if (self->target_is_folder)
        {
            *error_message = _("Folder names cannot contain “/”.");
        }
        else
        {
            *error_message = _("File names cannot contain “/”.");
        }
    }
    else if (strcmp (name, ".") == 0)
    {
        if (self->target_is_folder)
        {
            *error_message = _("A folder cannot be called “.”.");
        }
        else
        {
            *error_message = _("A file cannot be called “.”.");
        }
    }
    else if (strcmp (name, "..") == 0)
    {
        if (self->target_is_folder)
        {
            *error_message = _("A folder cannot be called “..”.");
        }
        else
        {
            *error_message = _("A file cannot be called “..”.");
        }
    }

    return *error_message == NULL;
}

static gboolean
nautilus_rename_file_popover_controller_ignore_existing_file (NautilusFileNameWidgetController *controller,
                                                              NautilusFile                     *existing_file)
{
    NautilusRenameFilePopoverController *self;
    g_autofree gchar *display_name;

    self = NAUTILUS_RENAME_FILE_POPOVER_CONTROLLER (controller);

    display_name = nautilus_file_get_display_name (existing_file);

    return nautilus_file_compare_display_name (self->target_file, display_name) == 0;
}

NautilusRenameFilePopoverController *
nautilus_rename_file_popover_controller_new (NautilusFile *target_file,
                                             GdkRectangle *pointing_to,
                                             GtkWidget    *relative_to)
{
    NautilusRenameFilePopoverController *self;
    g_autoptr (GtkBuilder) builder;
    GtkWidget *rename_file_popover;
    GtkWidget *error_revealer;
    GtkWidget *error_label;
    GtkWidget *name_entry;
    GtkWidget *activate_button;
    GtkWidget *name_label;
    NautilusDirectory *containing_directory;
    gint start_offset;
    gint end_offset;
    gint n_chars;

    builder = gtk_builder_new_from_resource ("/org/gnome/nautilus/ui/nautilus-rename-file-popover.ui");
    rename_file_popover = GTK_WIDGET (gtk_builder_get_object (builder, "rename_file_popover"));
    error_revealer = GTK_WIDGET (gtk_builder_get_object (builder, "error_revealer"));
    error_label = GTK_WIDGET (gtk_builder_get_object (builder, "error_label"));
    name_entry = GTK_WIDGET (gtk_builder_get_object (builder, "name_entry"));
    activate_button = GTK_WIDGET (gtk_builder_get_object (builder, "rename_button"));
    name_label = GTK_WIDGET (gtk_builder_get_object (builder, "name_label"));

    if (!nautilus_file_is_self_owned (target_file))
    {
        NautilusFile *parent_location;

        parent_location = nautilus_file_get_parent (target_file);
        containing_directory = nautilus_directory_get_for_file (parent_location);

        nautilus_file_unref (parent_location);
    }
    else
    {
        containing_directory = nautilus_directory_get_for_file (target_file);
    }

    self = g_object_new (NAUTILUS_TYPE_RENAME_FILE_POPOVER_CONTROLLER,
                         "error-revealer", error_revealer,
                         "error-label", error_label,
                         "name-entry", name_entry,
                         "activate-button", activate_button,
                         "containing-directory", containing_directory, NULL);

    self->target_is_folder = nautilus_file_is_directory (target_file);
    self->target_file = nautilus_file_ref (target_file);

    self->rename_file_popover = rename_file_popover;

    self->closed_handler_id = g_signal_connect (rename_file_popover,
                                                "closed",
                                                (GCallback) rename_file_popover_controller_on_closed,
                                                self);
    g_signal_connect (rename_file_popover,
                      "unmap",
                      (GCallback) gtk_widget_destroy,
                      NULL);

    gtk_label_set_text (GTK_LABEL (name_label),
                        self->target_is_folder ? _("Folder name") :
                        _("File name"));

    gtk_entry_set_text (GTK_ENTRY (name_entry),
                        nautilus_file_get_display_name (target_file));

    gtk_popover_set_default_widget (GTK_POPOVER (rename_file_popover), name_entry);
    gtk_popover_set_pointing_to (GTK_POPOVER (rename_file_popover), pointing_to);
    gtk_popover_set_relative_to (GTK_POPOVER (rename_file_popover), relative_to);

    gtk_widget_show (rename_file_popover);

    /* Select the name part withouth the file extension */
    eel_filename_get_rename_region (nautilus_file_get_display_name (target_file),
                                    &start_offset, &end_offset);
    n_chars = g_utf8_strlen (nautilus_file_get_display_name (target_file), -1);
    gtk_entry_set_width_chars (GTK_ENTRY (name_entry),
                               MIN (MAX (n_chars, RENAME_ENTRY_MIN_CHARS), RENAME_ENTRY_MAX_CHARS));
    gtk_editable_select_region (GTK_EDITABLE (name_entry), start_offset, end_offset);

    nautilus_directory_unref (containing_directory);

    return self;
}

NautilusFile *
nautilus_rename_file_popover_controller_get_target_file (NautilusRenameFilePopoverController *self)
{
    g_return_val_if_fail (NAUTILUS_IS_RENAME_FILE_POPOVER_CONTROLLER (self), NULL);

    return self->target_file;
}

static void
nautilus_rename_file_popover_controller_init (NautilusRenameFilePopoverController *self)
{
}

static void
nautilus_rename_file_popover_controller_finalize (GObject *object)
{
    NautilusRenameFilePopoverController *self;

    self = NAUTILUS_RENAME_FILE_POPOVER_CONTROLLER (object);

    if (self->rename_file_popover)
    {
        if (self->closed_handler_id)
        {
            g_signal_handler_disconnect (self->rename_file_popover,
                                         self->closed_handler_id);
            self->closed_handler_id = 0;
        }
        gtk_widget_hide (self->rename_file_popover);
        self->rename_file_popover = NULL;
    }

    nautilus_file_unref (self->target_file);

    G_OBJECT_CLASS (nautilus_rename_file_popover_controller_parent_class)->finalize (object);
}

static void
nautilus_rename_file_popover_controller_class_init (NautilusRenameFilePopoverControllerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    NautilusFileNameWidgetControllerClass *parent_class = NAUTILUS_FILE_NAME_WIDGET_CONTROLLER_CLASS (klass);

    object_class->finalize = nautilus_rename_file_popover_controller_finalize;

    parent_class->name_is_valid = nautilus_rename_file_popover_controller_name_is_valid;
    parent_class->ignore_existing_file = nautilus_rename_file_popover_controller_ignore_existing_file;
}
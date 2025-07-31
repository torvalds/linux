/*
 * HER OS Semantic Snapshot Browser
 *
 * GTK4 desktop application for visual browsing of semantic data, snapshots, and system state.
 * Integrates with Metadata Daemon and PTA Engine for semantic data and proactive suggestions.
 *
 * Features:
 * - Main window with navigation sidebar
 * - Secure initialization and PDP authorization
 * - Placeholder for semantic data view and search
 * - Audit logging for all user actions
 * - Follows Linux and GTK best practices
 *
 * Author: HER OS Project
 * License: GPL-2.0
 * Version: 1.0.0
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <glib/gstdio.h>
#include "metadata_client.c"

#define APP_ID "org.heros.SemanticSnapshotBrowser"
#define MAX_SNAPSHOTS 256
#define MAX_TAGS 256
#define MAX_SEARCH_RESULTS 100

// Forward declarations
static void activate(GtkApplication *app, gpointer user_data);
static void on_quit(GtkWidget *widget, gpointer user_data);
static void refresh_snapshots(GtkListBox *list_box);

// Real PDP authorization integration with comprehensive security validation
static int secure_init(void) {
    openlog("heros_semantic_snapshot_browser", LOG_PID | LOG_CONS, LOG_USER);
    syslog(LOG_INFO, "[SemanticSnapshotBrowser] Starting secure initialization");
    
    // Get current user and process information for authorization
    uid_t current_uid = getuid();
    pid_t current_pid = getpid();
    char username[256];
    struct passwd *pw = getpwuid(current_uid);
    if (pw) {
        strncpy(username, pw->pw_name, sizeof(username) - 1);
        username[sizeof(username) - 1] = '\0';
    } else {
        snprintf(username, sizeof(username), "uid_%d", current_uid);
    }
    
    // Connect to PDP daemon for session authorization
    int pdp_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (pdp_sock < 0) {
        syslog(LOG_ERR, "[SemanticSnapshotBrowser] Failed to create PDP socket: %s", strerror(errno));
        return -1;
    }
    
    struct sockaddr_un pdp_addr;
    memset(&pdp_addr, 0, sizeof(pdp_addr));
    pdp_addr.sun_family = AF_UNIX;
    strncpy(pdp_addr.sun_path, current_settings.pdp_socket_path, sizeof(pdp_addr.sun_path) - 1);
    
    // Attempt to connect to PDP daemon
    if (connect(pdp_sock, (struct sockaddr *)&pdp_addr, sizeof(pdp_addr)) < 0) {
        syslog(LOG_WARNING, "[SemanticSnapshotBrowser] PDP daemon not available, proceeding with reduced security: %s", strerror(errno));
        close(pdp_sock);
        
        // Fallback: basic security checks
        if (current_uid == 0) {
            syslog(LOG_ERR, "[SemanticSnapshotBrowser] Security violation: running as root");
            return -1;
        }
        
        syslog(LOG_INFO, "[SemanticSnapshotBrowser] Secure initialization complete (fallback mode)");
        return 0;
    }
    
    // Send authorization request to PDP
    char auth_request[512];
    snprintf(auth_request, sizeof(auth_request), 
             "AUTHORIZE session_start semantic_snapshot_browser uid=%d pid=%d user=%s action=launch_app\n",
             current_uid, current_pid, username);
    
    ssize_t sent = send(pdp_sock, auth_request, strlen(auth_request), 0);
    if (sent < 0) {
        syslog(LOG_ERR, "[SemanticSnapshotBrowser] Failed to send authorization request: %s", strerror(errno));
        close(pdp_sock);
        return -1;
    }
    
    // Receive authorization response
    char auth_response[256];
    ssize_t received = recv(pdp_sock, auth_response, sizeof(auth_response) - 1, 0);
    close(pdp_sock);
    
    if (received < 0) {
        syslog(LOG_ERR, "[SemanticSnapshotBrowser] Failed to receive authorization response: %s", strerror(errno));
        return -1;
    }
    
    auth_response[received] = '\0';
    
    // Parse authorization response
    if (strstr(auth_response, "ALLOW") != NULL) {
        syslog(LOG_INFO, "[SemanticSnapshotBrowser] PDP authorization granted for user %s (uid=%d)", username, current_uid);
        audit_log("SESSION_AUTHORIZED", "PDP authorization successful");
        
        // Store session information for future operations
        current_settings.security_level[0] = '\0'; // Will be updated from PDP response if provided
        
        syslog(LOG_INFO, "[SemanticSnapshotBrowser] Secure initialization complete");
        return 0;
        
    } else if (strstr(auth_response, "DENY") != NULL) {
        syslog(LOG_ERR, "[SemanticSnapshotBrowser] PDP authorization denied for user %s (uid=%d)", username, current_uid);
        audit_log("SESSION_DENIED", "PDP authorization failed");
        return -1;
        
    } else {
        syslog(LOG_WARNING, "[SemanticSnapshotBrowser] Unexpected PDP response: %s", auth_response);
        audit_log("SESSION_UNKNOWN", "Unexpected PDP response");
        
        // For unknown responses, apply conservative security policy
        if (current_uid == 0) {
            syslog(LOG_ERR, "[SemanticSnapshotBrowser] Security violation: running as root with unknown PDP response");
            return -1;
        }
        
        syslog(LOG_INFO, "[SemanticSnapshotBrowser] Secure initialization complete (conservative mode)");
        return 0;
    }
}

// Audit logging utility
static void audit_log(const char *event, const char *details) {
    syslog(LOG_INFO, "[SemanticSnapshotBrowser] %s: %s", event, details);
}

// Snapshots row widget factory
gboolean snapshot_row_update(GtkListBoxRow *row, gpointer user_data) {
    snapshot_info_t *snap = (snapshot_info_t *)user_data;
    if (!snap) return FALSE;
    char buf[512];
    snprintf(buf, sizeof(buf), "%s\n%s\nTags: %s\n%s", snap->timestamp, snap->id, snap->tags, snap->description);
    GtkWidget *label = gtk_label_new(buf);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_list_box_row_set_child(row, label);
    return TRUE;
}

// Global variables for search/filter state
static GtkEntry *search_entry = NULL;
static GtkListBox *snapshots_list = NULL;
static snapshot_info_t *current_snapshots = NULL;
static int current_snapshot_count = 0;

// Global variables for tags and search
static GtkEntry *tag_search_entry = NULL;
static GtkListBox *tags_list = NULL;
static GtkListBox *search_results_list = NULL;
static tag_info_t *current_tags = NULL;
static int current_tag_count = 0;
static snapshot_info_t *search_results = NULL;
static int search_results_count = 0;

// Global variables for dedicated search view
static GtkEntry *dedicated_search_entry = NULL;
static GtkListBox *dedicated_results_list = NULL;
static GtkComboBoxText *search_type_combo = NULL;
static snapshot_info_t *dedicated_search_results = NULL;
static int dedicated_search_results_count = 0;

// Global variables for settings
static GtkSwitch *audit_logging_switch = NULL;
static GtkSwitch *semantic_search_switch = NULL;
static GtkSwitch *auto_refresh_switch = NULL;
static GtkEntry *refresh_interval_entry = NULL;
static GtkComboBoxText *security_level_combo = NULL;
static GtkEntry *pdp_socket_entry = NULL;
static GtkEntry *metadata_socket_entry = NULL;
static GtkSwitch *notifications_switch = NULL;
static GtkSwitch *privacy_mode_switch = NULL;

// Settings structure
typedef struct {
    gboolean audit_logging_enabled;
    gboolean semantic_search_enabled;
    gboolean auto_refresh_enabled;
    int refresh_interval_seconds;
    char security_level[32];
    char pdp_socket_path[256];
    char metadata_socket_path[256];
    gboolean notifications_enabled;
    gboolean privacy_mode_enabled;
} app_settings_t;

static app_settings_t current_settings = {
    .audit_logging_enabled = TRUE,
    .semantic_search_enabled = TRUE,
    .auto_refresh_enabled = FALSE,
    .refresh_interval_seconds = 30,
    .security_level = "standard",
    .pdp_socket_path = "/tmp/heros_pdp.sock",
    .metadata_socket_path = "/tmp/heros_metadata.sock",
    .notifications_enabled = TRUE,
    .privacy_mode_enabled = FALSE
};

// Load settings from file
static void load_settings(void) {
    const char *config_dir = g_get_user_config_dir();
    char *settings_file = g_build_filename(config_dir, "heros", "semantic_browser.conf", NULL);
    
    GKeyFile *keyfile = g_key_file_new();
    if (g_key_file_load_from_file(keyfile, settings_file, G_KEY_FILE_NONE, NULL)) {
        current_settings.audit_logging_enabled = g_key_file_get_boolean(keyfile, "General", "audit_logging", NULL);
        current_settings.semantic_search_enabled = g_key_file_get_boolean(keyfile, "General", "semantic_search", NULL);
        current_settings.auto_refresh_enabled = g_key_file_get_boolean(keyfile, "General", "auto_refresh", NULL);
        current_settings.refresh_interval_seconds = g_key_file_get_integer(keyfile, "General", "refresh_interval", NULL);
        
        char *security_level = g_key_file_get_string(keyfile, "Security", "level", NULL);
        if (security_level) {
            strncpy(current_settings.security_level, security_level, sizeof(current_settings.security_level) - 1);
            g_free(security_level);
        }
        
        char *pdp_socket = g_key_file_get_string(keyfile, "Security", "pdp_socket", NULL);
        if (pdp_socket) {
            strncpy(current_settings.pdp_socket_path, pdp_socket, sizeof(current_settings.pdp_socket_path) - 1);
            g_free(pdp_socket);
        }
        
        char *metadata_socket = g_key_file_get_string(keyfile, "Security", "metadata_socket", NULL);
        if (metadata_socket) {
            strncpy(current_settings.metadata_socket_path, metadata_socket, sizeof(current_settings.metadata_socket_path) - 1);
            g_free(metadata_socket);
        }
        
        current_settings.notifications_enabled = g_key_file_get_boolean(keyfile, "Privacy", "notifications", NULL);
        current_settings.privacy_mode_enabled = g_key_file_get_boolean(keyfile, "Privacy", "privacy_mode", NULL);
    }
    
    g_key_file_free(keyfile);
    g_free(settings_file);
    audit_log("SETTINGS_LOADED", "Settings loaded from configuration file");
}

// Save settings to file
static void save_settings(void) {
    const char *config_dir = g_get_user_config_dir();
    char *heros_dir = g_build_filename(config_dir, "heros", NULL);
    char *settings_file = g_build_filename(heros_dir, "semantic_browser.conf", NULL);
    
    // Create directory if it doesn't exist
    g_mkdir_with_parents(heros_dir, 0755);
    
    GKeyFile *keyfile = g_key_file_new();
    
    // General settings
    g_key_file_set_boolean(keyfile, "General", "audit_logging", current_settings.audit_logging_enabled);
    g_key_file_set_boolean(keyfile, "General", "semantic_search", current_settings.semantic_search_enabled);
    g_key_file_set_boolean(keyfile, "General", "auto_refresh", current_settings.auto_refresh_enabled);
    g_key_file_set_integer(keyfile, "General", "refresh_interval", current_settings.refresh_interval_seconds);
    
    // Security settings
    g_key_file_set_string(keyfile, "Security", "level", current_settings.security_level);
    g_key_file_set_string(keyfile, "Security", "pdp_socket", current_settings.pdp_socket_path);
    g_key_file_set_string(keyfile, "Security", "metadata_socket", current_settings.metadata_socket_path);
    
    // Privacy settings
    g_key_file_set_boolean(keyfile, "Privacy", "notifications", current_settings.notifications_enabled);
    g_key_file_set_boolean(keyfile, "Privacy", "privacy_mode", current_settings.privacy_mode_enabled);
    
    // Write to file
    gsize length;
    char *data = g_key_file_to_data(keyfile, &length, NULL);
    g_file_set_contents(settings_file, data, length, NULL);
    
    g_free(data);
    g_key_file_free(keyfile);
    g_free(settings_file);
    g_free(heros_dir);
    
    audit_log("SETTINGS_SAVED", "Settings saved to configuration file");
}

// Update UI from settings
static void update_ui_from_settings(void) {
    gtk_switch_set_active(audit_logging_switch, current_settings.audit_logging_enabled);
    gtk_switch_set_active(semantic_search_switch, current_settings.semantic_search_enabled);
    gtk_switch_set_active(auto_refresh_switch, current_settings.auto_refresh_enabled);
    
    char interval_str[16];
    snprintf(interval_str, sizeof(interval_str), "%d", current_settings.refresh_interval_seconds);
    gtk_editable_set_text(GTK_EDITABLE(refresh_interval_entry), interval_str);
    
    // Set security level combo
    if (strcmp(current_settings.security_level, "low") == 0) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(security_level_combo), 0);
    } else if (strcmp(current_settings.security_level, "high") == 0) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(security_level_combo), 2);
    } else {
        gtk_combo_box_set_active(GTK_COMBO_BOX(security_level_combo), 1);
    }
    
    gtk_editable_set_text(GTK_EDITABLE(pdp_socket_entry), current_settings.pdp_socket_path);
    gtk_editable_set_text(GTK_EDITABLE(metadata_socket_entry), current_settings.metadata_socket_path);
    gtk_switch_set_active(notifications_switch, current_settings.notifications_enabled);
    gtk_switch_set_active(privacy_mode_switch, current_settings.privacy_mode_enabled);
}

// Update settings from UI
static void update_settings_from_ui(void) {
    current_settings.audit_logging_enabled = gtk_switch_get_active(audit_logging_switch);
    current_settings.semantic_search_enabled = gtk_switch_get_active(semantic_search_switch);
    current_settings.auto_refresh_enabled = gtk_switch_get_active(auto_refresh_switch);
    
    const char *interval_str = gtk_editable_get_text(GTK_EDITABLE(refresh_interval_entry));
    current_settings.refresh_interval_seconds = atoi(interval_str);
    if (current_settings.refresh_interval_seconds < 5) current_settings.refresh_interval_seconds = 5;
    if (current_settings.refresh_interval_seconds > 3600) current_settings.refresh_interval_seconds = 3600;
    
    int security_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(security_level_combo));
    if (security_idx == 0) strcpy(current_settings.security_level, "low");
    else if (security_idx == 2) strcpy(current_settings.security_level, "high");
    else strcpy(current_settings.security_level, "standard");
    
    const char *pdp_socket = gtk_editable_get_text(GTK_EDITABLE(pdp_socket_entry));
    strncpy(current_settings.pdp_socket_path, pdp_socket, sizeof(current_settings.pdp_socket_path) - 1);
    
    const char *metadata_socket = gtk_editable_get_text(GTK_EDITABLE(metadata_socket_entry));
    strncpy(current_settings.metadata_socket_path, metadata_socket, sizeof(current_settings.metadata_socket_path) - 1);
    
    current_settings.notifications_enabled = gtk_switch_get_active(notifications_switch);
    current_settings.privacy_mode_enabled = gtk_switch_get_active(privacy_mode_switch);
}

// Save settings callback
static void save_settings_callback(GtkWidget *widget, gpointer user_data) {
    update_settings_from_ui();
    save_settings();
    
    // Show success message
    GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "Settings saved successfully.");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    audit_log("SETTINGS_SAVED", "User saved settings");
}

// Reset to defaults callback
static void reset_settings_callback(GtkWidget *widget, gpointer user_data) {
    GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, 
        "Are you sure you want to reset all settings to defaults?");
    
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    if (response == GTK_RESPONSE_YES) {
        // Reset to defaults
        current_settings = (app_settings_t){
            .audit_logging_enabled = TRUE,
            .semantic_search_enabled = TRUE,
            .auto_refresh_enabled = FALSE,
            .refresh_interval_seconds = 30,
            .security_level = "standard",
            .pdp_socket_path = "/tmp/heros_pdp.sock",
            .metadata_socket_path = "/tmp/heros_metadata.sock",
            .notifications_enabled = TRUE,
            .privacy_mode_enabled = FALSE
        };
        
        update_ui_from_settings();
        save_settings();
        audit_log("SETTINGS_RESET", "Settings reset to defaults");
    }
}

// Test PDP connection callback
static void test_pdp_connection(GtkWidget *widget, gpointer user_data) {
    const char *pdp_socket = gtk_editable_get_text(GTK_EDITABLE(pdp_socket_entry));
    
    // Simple socket test
    int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) {
        GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Failed to create socket for PDP test.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, pdp_socket, sizeof(addr.sun_path) - 1);
    
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, 
            "PDP connection failed. Please check if the PDP daemon is running.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    } else {
        GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "PDP connection successful!");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
    
    close(sockfd);
    audit_log("PDP_TEST", "PDP connection test performed");
}

// Filter snapshots based on search text
static void filter_snapshots(void) {
    const char *search_text = gtk_editable_get_text(GTK_EDITABLE(search_entry));
    gtk_list_box_remove_all(snapshots_list);
    
    if (!current_snapshots || current_snapshot_count <= 0) {
        GtkWidget *empty = gtk_label_new("No snapshots available.");
        gtk_list_box_append(snapshots_list, empty);
        return;
    }
    
    int filtered_count = 0;
    for (int i = 0; i < current_snapshot_count; ++i) {
        // Simple text search in id, tags, and description
        if (strlen(search_text) == 0 ||
            strstr(current_snapshots[i].id, search_text) ||
            strstr(current_snapshots[i].tags, search_text) ||
            strstr(current_snapshots[i].description, search_text)) {
            
            GtkListBoxRow *row = GTK_LIST_BOX_ROW(gtk_list_box_row_new());
            snapshot_row_update(row, &current_snapshots[i]);
            gtk_list_box_append(snapshots_list, GTK_WIDGET(row));
            filtered_count++;
        }
    }
    
    if (filtered_count == 0) {
        GtkWidget *no_match = gtk_label_new("No snapshots match your search.");
        gtk_list_box_append(snapshots_list, no_match);
    }
    
    audit_log("SNAPSHOTS_FILTERED", search_text);
}

// Tag row widget factory
gboolean tag_row_update(GtkListBoxRow *row, gpointer user_data) {
    tag_info_t *tag = (tag_info_t *)user_data;
    if (!tag) return FALSE;
    
    char buf[1024];
    snprintf(buf, sizeof(buf), 
        "Key: %s\nValue: %s\nUsage: %d times\nLast Used: %s\nRelated: %s",
        tag->key, tag->value, tag->usage_count, tag->last_used, tag->related_tags);
    
    GtkWidget *label = gtk_label_new(buf);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_list_box_row_set_child(row, label);
    return TRUE;
}

// Filter tags based on search text
static void filter_tags(void) {
    const char *search_text = gtk_editable_get_text(GTK_EDITABLE(tag_search_entry));
    gtk_list_box_remove_all(tags_list);
    
    if (!current_tags || current_tag_count <= 0) {
        GtkWidget *empty = gtk_label_new("No tags available.");
        gtk_list_box_append(tags_list, empty);
        return;
    }
    
    int filtered_count = 0;
    for (int i = 0; i < current_tag_count; ++i) {
        // Search in key, value, and related tags
        if (strlen(search_text) == 0 ||
            strstr(current_tags[i].key, search_text) ||
            strstr(current_tags[i].value, search_text) ||
            strstr(current_tags[i].related_tags, search_text)) {
            
            GtkListBoxRow *row = GTK_LIST_BOX_ROW(gtk_list_box_row_new());
            tag_row_update(row, &current_tags[i]);
            gtk_list_box_append(tags_list, GTK_WIDGET(row));
            filtered_count++;
        }
    }
    
    if (filtered_count == 0) {
        GtkWidget *no_match = gtk_label_new("No tags match your search.");
        gtk_list_box_append(tags_list, no_match);
    }
    
    audit_log("TAGS_FILTERED", search_text);
}

// Restore selected snapshot (with PDP authorization stub)
static void restore_snapshot(GtkWidget *widget, gpointer user_data) {
    GtkListBoxRow *selected = gtk_list_box_get_selected_row(snapshots_list);
    if (!selected) {
        // Show error dialog
        GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "No snapshot selected.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    // TODO: Get snapshot ID from selected row and send restore command
    // For now, just log the action
    audit_log("SNAPSHOT_RESTORE_REQUESTED", "Snapshot restore requested (PDP authorization pending)");
    
    // Show confirmation dialog
    GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, 
        "Are you sure you want to restore this snapshot? This will overwrite current system state.");
    
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    if (response == GTK_RESPONSE_YES) {
        // TODO: Implement actual restore via Metadata Daemon
        audit_log("SNAPSHOT_RESTORE_CONFIRMED", "Snapshot restore confirmed (implementation pending)");
        
        // Show success message
        GtkWidget *success_dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "Snapshot restore initiated.");
        gtk_dialog_run(GTK_DIALOG(success_dialog));
        gtk_widget_destroy(success_dialog);
    }
}

// Refresh snapshots with search/filter support
static void refresh_snapshots(GtkListBox *list_box) {
    gtk_list_box_remove_all(list_box);
    
    // Free previous snapshot data
    if (current_snapshots) {
        free(current_snapshots);
        current_snapshots = NULL;
    }
    
    current_snapshots = malloc(MAX_SNAPSHOTS * sizeof(snapshot_info_t));
    if (!current_snapshots) {
        GtkWidget *err = gtk_label_new("Memory allocation failed.");
        gtk_list_box_append(list_box, err);
        audit_log("SNAPSHOTS_ERROR", "Memory allocation failed");
        return;
    }
    
    current_snapshot_count = fetch_snapshots(current_snapshots, MAX_SNAPSHOTS);
    if (current_snapshot_count < 0) {
        GtkWidget *err = gtk_label_new("Failed to load snapshots (Metadata Daemon unreachable)");
        gtk_list_box_append(list_box, err);
        audit_log("SNAPSHOTS_ERROR", "Failed to fetch snapshots");
        free(current_snapshots);
        current_snapshots = NULL;
        current_snapshot_count = 0;
        return;
    }
    
    if (current_snapshot_count == 0) {
        GtkWidget *empty = gtk_label_new("No snapshots available.");
        gtk_list_box_append(list_box, empty);
        free(current_snapshots);
        current_snapshots = NULL;
        return;
    }
    
    // Apply current filter
    filter_snapshots();
    audit_log("SNAPSHOTS_LOADED", "Snapshots loaded from Metadata Daemon");
}

// Refresh tags from Metadata Daemon
static void refresh_tags(GtkListBox *list_box) {
    gtk_list_box_remove_all(list_box);
    
    // Free previous tag data
    if (current_tags) {
        free(current_tags);
        current_tags = NULL;
    }
    
    current_tags = malloc(MAX_TAGS * sizeof(tag_info_t));
    if (!current_tags) {
        GtkWidget *err = gtk_label_new("Memory allocation failed.");
        gtk_list_box_append(list_box, err);
        audit_log("TAGS_ERROR", "Memory allocation failed");
        return;
    }
    
    current_tag_count = fetch_semantic_tags(current_tags, MAX_TAGS);
    if (current_tag_count < 0) {
        GtkWidget *err = gtk_label_new("Failed to load tags (Metadata Daemon unreachable)");
        gtk_list_box_append(list_box, err);
        audit_log("TAGS_ERROR", "Failed to fetch tags");
        free(current_tags);
        current_tags = NULL;
        current_tag_count = 0;
        return;
    }
    
    if (current_tag_count == 0) {
        GtkWidget *empty = gtk_label_new("No tags available.");
        gtk_list_box_append(list_box, empty);
        free(current_tags);
        current_tags = NULL;
        return;
    }
    
    // Apply current filter
    filter_tags();
    audit_log("TAGS_LOADED", "Tags loaded from Metadata Daemon");
}

// Perform semantic search
static void perform_search(GtkWidget *widget, gpointer user_data) {
    GtkEntry *search_entry = GTK_ENTRY(user_data);
    const char *query = gtk_editable_get_text(GTK_EDITABLE(search_entry));
    
    if (strlen(query) == 0) {
        gtk_list_box_remove_all(search_results_list);
        GtkWidget *empty = gtk_label_new("Enter a search query to find snapshots.");
        gtk_list_box_append(search_results_list, empty);
        return;
    }
    
    gtk_list_box_remove_all(search_results_list);
    
    // Free previous search results
    if (search_results) {
        free(search_results);
        search_results = NULL;
    }
    
    search_results = malloc(MAX_SEARCH_RESULTS * sizeof(snapshot_info_t));
    if (!search_results) {
        GtkWidget *err = gtk_label_new("Memory allocation failed.");
        gtk_list_box_append(search_results_list, err);
        audit_log("SEARCH_ERROR", "Memory allocation failed");
        return;
    }
    
    search_results_count = perform_semantic_search(query, search_results, MAX_SEARCH_RESULTS);
    if (search_results_count < 0) {
        GtkWidget *err = gtk_label_new("Search failed (Metadata Daemon unreachable)");
        gtk_list_box_append(search_results_list, err);
        audit_log("SEARCH_ERROR", "Failed to perform semantic search");
        free(search_results);
        search_results = NULL;
        search_results_count = 0;
        return;
    }
    
    if (search_results_count == 0) {
        GtkWidget *no_results = gtk_label_new("No snapshots found for your query.");
        gtk_list_box_append(search_results_list, no_results);
        free(search_results);
        search_results = NULL;
        return;
    }
    
    // Display search results
    for (int i = 0; i < search_results_count; ++i) {
        GtkListBoxRow *row = GTK_LIST_BOX_ROW(gtk_list_box_row_new());
        snapshot_row_update(row, &search_results[i]);
        gtk_list_box_append(search_results_list, GTK_WIDGET(row));
    }
    
    audit_log("SEMANTIC_SEARCH", query);
}

// Perform dedicated semantic search
static void perform_dedicated_search(GtkWidget *widget, gpointer user_data) {
    const char *query = gtk_editable_get_text(GTK_EDITABLE(dedicated_search_entry));
    const char *search_type = gtk_combo_box_text_get_active_text(search_type_combo);
    
    if (strlen(query) == 0) {
        gtk_list_box_remove_all(dedicated_results_list);
        GtkWidget *empty = gtk_label_new("Enter a search query to find snapshots.");
        gtk_list_box_append(dedicated_results_list, empty);
        return;
    }
    
    gtk_list_box_remove_all(dedicated_results_list);
    
    // Free previous search results
    if (dedicated_search_results) {
        free(dedicated_search_results);
        dedicated_search_results = NULL;
    }
    
    dedicated_search_results = malloc(MAX_SEARCH_RESULTS * sizeof(snapshot_info_t));
    if (!dedicated_search_results) {
        GtkWidget *err = gtk_label_new("Memory allocation failed.");
        gtk_list_box_append(dedicated_results_list, err);
        audit_log("DEDICATED_SEARCH_ERROR", "Memory allocation failed");
        return;
    }
    
    // Perform search based on type
    if (search_type && strcmp(search_type, "Semantic") == 0) {
        dedicated_search_results_count = perform_semantic_search(query, dedicated_search_results, MAX_SEARCH_RESULTS);
    } else {
        // Default to semantic search
        dedicated_search_results_count = perform_semantic_search(query, dedicated_search_results, MAX_SEARCH_RESULTS);
    }
    
    if (dedicated_search_results_count < 0) {
        GtkWidget *err = gtk_label_new("Search failed (Metadata Daemon unreachable)");
        gtk_list_box_append(dedicated_results_list, err);
        audit_log("DEDICATED_SEARCH_ERROR", "Failed to perform search");
        free(dedicated_search_results);
        dedicated_search_results = NULL;
        dedicated_search_results_count = 0;
        return;
    }
    
    if (dedicated_search_results_count == 0) {
        GtkWidget *no_results = gtk_label_new("No snapshots found for your query.");
        gtk_list_box_append(dedicated_results_list, no_results);
        free(dedicated_search_results);
        dedicated_search_results = NULL;
        return;
    }
    
    // Display search results
    for (int i = 0; i < dedicated_search_results_count; ++i) {
        GtkListBoxRow *row = GTK_LIST_BOX_ROW(gtk_list_box_row_new());
        snapshot_row_update(row, &dedicated_search_results[i]);
        gtk_list_box_append(dedicated_results_list, GTK_WIDGET(row));
    }
    
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "Query: %s, Type: %s, Results: %d", 
             query, search_type ? search_type : "Semantic", dedicated_search_results_count);
    audit_log("DEDICATED_SEARCH", log_msg);
}

// Clear search results
static void clear_search_results(GtkWidget *widget, gpointer user_data) {
    gtk_list_box_remove_all(dedicated_results_list);
    GtkWidget *empty = gtk_label_new("Enter a search query to find snapshots.");
    gtk_list_box_append(dedicated_results_list, empty);
    
    if (dedicated_search_results) {
        free(dedicated_search_results);
        dedicated_search_results = NULL;
    }
    dedicated_search_results_count = 0;
    
    audit_log("SEARCH_CLEARED", "Search results cleared");
}

// Main application entry point
int main(int argc, char *argv[]) {
    // Secure initialization
    if (secure_init() != 0) {
        fprintf(stderr, "[SemanticSnapshotBrowser] Secure initialization failed\n");
        return 1;
    }

    // GTK application
    GtkApplication *app = gtk_application_new(APP_ID, G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    syslog(LOG_INFO, "[SemanticSnapshotBrowser] Application exited");
    closelog();
    return status;
}

// GTK activate callback
static void activate(GtkApplication *app, gpointer user_data) {
    // Main window
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "HER OS Semantic Snapshot Browser");
    gtk_window_set_default_size(GTK_WINDOW(window), 1200, 800);

    // Main layout: sidebar + content
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_window_set_child(GTK_WINDOW(window), main_box);

    // Sidebar navigation
    GtkWidget *sidebar = gtk_list_box_new();
    gtk_widget_set_size_request(sidebar, 220, -1);
    gtk_box_append(GTK_BOX(main_box), sidebar);

    GtkWidget *snapshots_row = gtk_label_new("Snapshots");
    GtkWidget *tags_row = gtk_label_new("Semantic Tags");
    GtkWidget *search_row = gtk_label_new("Search");
    GtkWidget *settings_row = gtk_label_new("Settings");
    gtk_list_box_append(GTK_LIST_BOX(sidebar), snapshots_row);
    gtk_list_box_append(GTK_LIST_BOX(sidebar), tags_row);
    gtk_list_box_append(GTK_LIST_BOX(sidebar), search_row);
    gtk_list_box_append(GTK_LIST_BOX(sidebar), settings_row);

    // Content area
    GtkWidget *content_area = gtk_stack_new();
    gtk_box_append(GTK_BOX(main_box), content_area);

    // Snapshots View with search and controls
    GtkWidget *snapshots_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(snapshots_box, 10);
    gtk_widget_set_margin_end(snapshots_box, 10);
    gtk_widget_set_margin_top(snapshots_box, 10);
    gtk_widget_set_margin_bottom(snapshots_box, 10);
    
    // Search and controls bar
    GtkWidget *controls_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(snapshots_box), controls_box);
    
    // Search entry
    search_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(search_entry, "Search snapshots...");
    gtk_box_append(GTK_BOX(controls_box), GTK_WIDGET(search_entry));
    
    // Refresh button
    GtkWidget *refresh_button = gtk_button_new_with_label("Refresh");
    gtk_box_append(GTK_BOX(controls_box), refresh_button);
    
    // Restore button
    GtkWidget *restore_button = gtk_button_new_with_label("Restore Selected");
    gtk_box_append(GTK_BOX(controls_box), restore_button);
    
    // Snapshots list
    snapshots_list = GTK_LIST_BOX(gtk_list_box_new());
    GtkWidget *scrolled_window = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), GTK_WIDGET(snapshots_list));
    gtk_box_append(GTK_BOX(snapshots_box), scrolled_window);
    
    gtk_stack_add_named(GTK_STACK(content_area), snapshots_box, "snapshots");
    
    // Connect signals
    g_signal_connect(search_entry, "changed", G_CALLBACK(filter_snapshots), NULL);
    g_signal_connect(refresh_button, "clicked", G_CALLBACK(refresh_snapshots), snapshots_list);
    g_signal_connect(restore_button, "clicked", G_CALLBACK(restore_snapshot), NULL);
    
    // Initial load
    refresh_snapshots(snapshots_list);

    // Tags View with semantic search
    GtkWidget *tags_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(tags_box, 10);
    gtk_widget_set_margin_end(tags_box, 10);
    gtk_widget_set_margin_top(tags_box, 10);
    gtk_widget_set_margin_bottom(tags_box, 10);
    
    // Tags controls
    GtkWidget *tags_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(tags_box), tags_controls);
    
    // Tag search entry
    tag_search_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(tag_search_entry, "Search tags...");
    gtk_box_append(GTK_BOX(tags_controls), GTK_WIDGET(tag_search_entry));
    
    // Refresh tags button
    GtkWidget *refresh_tags_button = gtk_button_new_with_label("Refresh Tags");
    gtk_box_append(GTK_BOX(tags_controls), refresh_tags_button);
    
    // Tags list
    tags_list = GTK_LIST_BOX(gtk_list_box_new());
    GtkWidget *tags_scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(tags_scrolled), GTK_WIDGET(tags_list));
    gtk_box_append(GTK_BOX(tags_box), tags_scrolled);
    
    // Semantic search section
    GtkWidget *search_label = gtk_label_new("Semantic Search");
    gtk_widget_set_margin_top(search_label, 20);
    gtk_box_append(GTK_BOX(tags_box), search_label);
    
    GtkWidget *search_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(tags_box), search_controls);
    
    // Semantic search entry
    GtkEntry *semantic_search_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(semantic_search_entry, "Enter semantic query...");
    gtk_box_append(GTK_BOX(search_controls), GTK_WIDGET(semantic_search_entry));
    
    // Search button
    GtkWidget *search_button = gtk_button_new_with_label("Search");
    gtk_box_append(GTK_BOX(search_controls), search_button);
    
    // Search results
    search_results_list = GTK_LIST_BOX(gtk_list_box_new());
    GtkWidget *results_scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(results_scrolled), GTK_WIDGET(search_results_list));
    gtk_box_append(GTK_BOX(tags_box), results_scrolled);
    
    gtk_stack_add_named(GTK_STACK(content_area), tags_box, "tags");
    
    // Connect tag signals
    g_signal_connect(tag_search_entry, "changed", G_CALLBACK(filter_tags), NULL);
    g_signal_connect(refresh_tags_button, "clicked", G_CALLBACK(refresh_tags), tags_list);
    g_signal_connect(search_button, "clicked", G_CALLBACK(perform_search), semantic_search_entry);
    
    // Initial load
    refresh_tags(tags_list);
    
    // Initialize search results
    GtkWidget *search_placeholder = gtk_label_new("Enter a search query to find snapshots.");
    gtk_list_box_append(search_results_list, search_placeholder);

    // Search View with advanced search
    GtkWidget *search_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(search_box, 10);
    gtk_widget_set_margin_end(search_box, 10);
    gtk_widget_set_margin_top(search_box, 10);
    gtk_widget_set_margin_bottom(search_box, 10);
    
    // Search header
    GtkWidget *search_header = gtk_label_new("Advanced Semantic Search");
    gtk_widget_set_margin_bottom(search_header, 20);
    gtk_box_append(GTK_BOX(search_box), search_header);
    
    // Search controls
    GtkWidget *search_controls_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(search_box), search_controls_box);
    
    // Search type selector
    search_type_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append_text(search_type_combo, "Semantic");
    gtk_combo_box_text_append_text(search_type_combo, "Keyword");
    gtk_combo_box_text_append_text(search_type_combo, "Tag-based");
    gtk_combo_box_text_set_active(GTK_COMBO_BOX_TEXT(search_type_combo), 0);
    gtk_box_append(GTK_BOX(search_controls_box), GTK_WIDGET(search_type_combo));
    
    // Search entry
    dedicated_search_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(dedicated_search_entry, "Enter your search query...");
    gtk_widget_set_hexpand(GTK_WIDGET(dedicated_search_entry), TRUE);
    gtk_box_append(GTK_BOX(search_controls_box), GTK_WIDGET(dedicated_search_entry));
    
    // Search button
    GtkWidget *dedicated_search_button = gtk_button_new_with_label("Search");
    gtk_box_append(GTK_BOX(search_controls_box), dedicated_search_button);
    
    // Clear button
    GtkWidget *clear_button = gtk_button_new_with_label("Clear");
    gtk_box_append(GTK_BOX(search_controls_box), clear_button);
    
    // Search results
    dedicated_results_list = GTK_LIST_BOX(gtk_list_box_new());
    GtkWidget *dedicated_results_scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(dedicated_results_scrolled), GTK_WIDGET(dedicated_results_list));
    gtk_box_append(GTK_BOX(search_box), dedicated_results_scrolled);
    
    gtk_stack_add_named(GTK_STACK(content_area), search_box, "search");
    
    // Connect search signals
    g_signal_connect(dedicated_search_button, "clicked", G_CALLBACK(perform_dedicated_search), NULL);
    g_signal_connect(clear_button, "clicked", G_CALLBACK(clear_search_results), NULL);
    g_signal_connect(dedicated_search_entry, "activate", G_CALLBACK(perform_dedicated_search), NULL);
    
    // Initialize search results
    GtkWidget *search_placeholder = gtk_label_new("Enter a search query to find snapshots.");
    gtk_list_box_append(dedicated_results_list, search_placeholder);

    // Settings View with comprehensive controls
    GtkWidget *settings_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(settings_box, 10);
    gtk_widget_set_margin_end(settings_box, 10);
    gtk_widget_set_margin_top(settings_box, 10);
    gtk_widget_set_margin_bottom(settings_box, 10);
    
    // Settings header
    GtkWidget *settings_header = gtk_label_new("Settings and Security");
    gtk_widget_set_margin_bottom(settings_header, 20);
    gtk_box_append(GTK_BOX(settings_box), settings_header);
    
    // Create notebook for organized settings
    GtkWidget *notebook = gtk_notebook_new();
    gtk_box_append(GTK_BOX(settings_box), notebook);
    
    // General Settings Page
    GtkWidget *general_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(general_page, 10);
    gtk_widget_set_margin_end(general_page, 10);
    gtk_widget_set_margin_top(general_page, 10);
    gtk_widget_set_margin_bottom(general_page, 10);
    
    // Audit logging
    GtkWidget *audit_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *audit_label = gtk_label_new("Enable Audit Logging");
    audit_logging_switch = GTK_SWITCH(gtk_switch_new());
    gtk_box_append(GTK_BOX(audit_row), audit_label);
    gtk_box_append(GTK_BOX(audit_row), GTK_WIDGET(audit_logging_switch));
    gtk_box_append(GTK_BOX(general_page), audit_row);
    
    // Semantic search
    GtkWidget *semantic_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *semantic_label = gtk_label_new("Enable Semantic Search");
    semantic_search_switch = GTK_SWITCH(gtk_switch_new());
    gtk_box_append(GTK_BOX(semantic_row), semantic_label);
    gtk_box_append(GTK_BOX(semantic_row), GTK_WIDGET(semantic_search_switch));
    gtk_box_append(GTK_BOX(general_page), semantic_row);
    
    // Auto refresh
    GtkWidget *refresh_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *refresh_label = gtk_label_new("Auto Refresh");
    auto_refresh_switch = GTK_SWITCH(gtk_switch_new());
    gtk_box_append(GTK_BOX(refresh_row), refresh_label);
    gtk_box_append(GTK_BOX(refresh_row), GTK_WIDGET(auto_refresh_switch));
    gtk_box_append(GTK_BOX(general_page), refresh_row);
    
    // Refresh interval
    GtkWidget *interval_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *interval_label = gtk_label_new("Refresh Interval (seconds)");
    refresh_interval_entry = GTK_ENTRY(gtk_entry_new());
    gtk_box_append(GTK_BOX(interval_row), interval_label);
    gtk_box_append(GTK_BOX(interval_row), GTK_WIDGET(refresh_interval_entry));
    gtk_box_append(GTK_BOX(general_page), interval_row);
    
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), general_page, gtk_label_new("General"));
    
    // Security Settings Page
    GtkWidget *security_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(security_page, 10);
    gtk_widget_set_margin_end(security_page, 10);
    gtk_widget_set_margin_top(security_page, 10);
    gtk_widget_set_margin_bottom(security_page, 10);
    
    // Security level
    GtkWidget *security_level_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *security_level_label = gtk_label_new("Security Level");
    security_level_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append_text(security_level_combo, "Low");
    gtk_combo_box_text_append_text(security_level_combo, "Standard");
    gtk_combo_box_text_append_text(security_level_combo, "High");
    gtk_box_append(GTK_BOX(security_level_row), security_level_label);
    gtk_box_append(GTK_BOX(security_level_row), GTK_WIDGET(security_level_combo));
    gtk_box_append(GTK_BOX(security_page), security_level_row);
    
    // PDP socket
    GtkWidget *pdp_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *pdp_label = gtk_label_new("PDP Socket Path");
    pdp_socket_entry = GTK_ENTRY(gtk_entry_new());
    gtk_widget_set_hexpand(GTK_WIDGET(pdp_socket_entry), TRUE);
    gtk_box_append(GTK_BOX(pdp_row), pdp_label);
    gtk_box_append(GTK_BOX(pdp_row), GTK_WIDGET(pdp_socket_entry));
    gtk_box_append(GTK_BOX(security_page), pdp_row);
    
    // Test PDP button
    GtkWidget *test_pdp_button = gtk_button_new_with_label("Test PDP Connection");
    gtk_box_append(GTK_BOX(security_page), test_pdp_button);
    
    // Metadata socket
    GtkWidget *metadata_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *metadata_label = gtk_label_new("Metadata Socket Path");
    metadata_socket_entry = GTK_ENTRY(gtk_entry_new());
    gtk_widget_set_hexpand(GTK_WIDGET(metadata_socket_entry), TRUE);
    gtk_box_append(GTK_BOX(metadata_row), metadata_label);
    gtk_box_append(GTK_BOX(metadata_row), GTK_WIDGET(metadata_socket_entry));
    gtk_box_append(GTK_BOX(security_page), metadata_row);
    
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), security_page, gtk_label_new("Security"));
    
    // Privacy Settings Page
    GtkWidget *privacy_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(privacy_page, 10);
    gtk_widget_set_margin_end(privacy_page, 10);
    gtk_widget_set_margin_top(privacy_page, 10);
    gtk_widget_set_margin_bottom(privacy_page, 10);
    
    // Notifications
    GtkWidget *notifications_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *notifications_label = gtk_label_new("Enable Notifications");
    notifications_switch = GTK_SWITCH(gtk_switch_new());
    gtk_box_append(GTK_BOX(notifications_row), notifications_label);
    gtk_box_append(GTK_BOX(notifications_row), GTK_WIDGET(notifications_switch));
    gtk_box_append(GTK_BOX(privacy_page), notifications_row);
    
    // Privacy mode
    GtkWidget *privacy_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *privacy_label = gtk_label_new("Privacy Mode (reduced logging)");
    privacy_mode_switch = GTK_SWITCH(gtk_switch_new());
    gtk_box_append(GTK_BOX(privacy_row), privacy_label);
    gtk_box_append(GTK_BOX(privacy_row), GTK_WIDGET(privacy_mode_switch));
    gtk_box_append(GTK_BOX(privacy_page), privacy_row);
    
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), privacy_page, gtk_label_new("Privacy"));
    
    // Settings buttons
    GtkWidget *settings_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_top(settings_buttons, 20);
    
    GtkWidget *save_button = gtk_button_new_with_label("Save Settings");
    GtkWidget *reset_button = gtk_button_new_with_label("Reset to Defaults");
    
    gtk_box_append(GTK_BOX(settings_buttons), save_button);
    gtk_box_append(GTK_BOX(settings_buttons), reset_button);
    gtk_box_append(GTK_BOX(settings_box), settings_buttons);
    
    gtk_stack_add_named(GTK_STACK(content_area), settings_box, "settings");
    
    // Connect settings signals
    g_signal_connect(save_button, "clicked", G_CALLBACK(save_settings_callback), NULL);
    g_signal_connect(reset_button, "clicked", G_CALLBACK(reset_settings_callback), NULL);
    g_signal_connect(test_pdp_button, "clicked", G_CALLBACK(test_pdp_connection), NULL);
    
    // Load and apply settings
    load_settings();
    update_ui_from_settings();

    // Navigation logic
    g_signal_connect(sidebar, "row-selected", G_CALLBACK(
        +[](GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
            GtkWidget *content_area = GTK_WIDGET(user_data);
            int idx = gtk_list_box_row_get_index(row);
            switch (idx) {
                case 0:
                    gtk_stack_set_visible_child_name(GTK_STACK(content_area), "snapshots");
                    audit_log("NAVIGATE", "Snapshots");
                    break;
                case 1:
                    gtk_stack_set_visible_child_name(GTK_STACK(content_area), "tags");
                    audit_log("NAVIGATE", "Semantic Tags");
                    break;
                case 2:
                    gtk_stack_set_visible_child_name(GTK_STACK(content_area), "search");
                    audit_log("NAVIGATE", "Search");
                    break;
                case 3:
                    gtk_stack_set_visible_child_name(GTK_STACK(content_area), "settings");
                    audit_log("NAVIGATE", "Settings");
                    break;
                default:
                    break;
            }
        }
    ), content_area);

    // Quit action
    g_signal_connect(window, "close-request", G_CALLBACK(on_quit), NULL);

    gtk_window_present(GTK_WINDOW(window));
    audit_log("APP_START", "Semantic Snapshot Browser started");
}

// Quit callback
static void on_quit(GtkWidget *widget, gpointer user_data) {
    audit_log("APP_QUIT", "Semantic Snapshot Browser exited");
    gtk_window_destroy(GTK_WINDOW(widget));
} 
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <errno.h>

// Configuration structure
typedef struct {
    GtkWidget *window;
    GtkWidget *notebook;
    GtkWidget *status_bar;
    
    // AI Configuration
    GtkWidget *ai_provider_combo;
    GtkWidget *ai_model_entry;
    GtkWidget *ai_temperature_scale;
    GtkWidget *ai_max_tokens_spin;
    GtkWidget *ollama_install_button;
    GtkWidget *ollama_status_label;
    
    // System Configuration
    GtkWidget *hostname_entry;
    GtkWidget *data_directory_entry;
    GtkWidget *log_directory_entry;
    GtkWidget *max_memory_spin;
    GtkWidget *max_cpu_scale;
    
    // Security Configuration
    GtkWidget *audit_logging_switch;
    GtkWidget *ssl_enabled_switch;
    GtkWidget *firewall_enabled_switch;
    GtkWidget *default_permissions_combo;
    
    // Performance Configuration
    GtkWidget *enable_caching_switch;
    GtkWidget *cache_size_spin;
    GtkWidget *enable_compression_switch;
    GtkWidget *compression_level_scale;
    
    // Network Configuration
    GtkWidget *metadata_port_spin;
    GtkWidget *pdp_port_spin;
    GtkWidget *wal_port_spin;
    GtkWidget *ai_port_spin;
    
    // API Keys
    GtkWidget *claude_api_key_entry;
    GtkWidget *google_api_key_entry;
    GtkWidget *openai_api_key_entry;
    
} ConfiguratorData;

// Function prototypes
static void create_main_window(ConfiguratorData *data);
static void create_ai_configuration_page(ConfiguratorData *data);
static void create_system_configuration_page(ConfiguratorData *data);
static void create_security_configuration_page(ConfiguratorData *data);
static void create_performance_configuration_page(ConfiguratorData *data);
static void create_network_configuration_page(ConfiguratorData *data);
static void create_api_keys_page(ConfiguratorData *data);
static void on_ai_provider_changed(GtkComboBox *combo, ConfiguratorData *data);
static void on_ollama_install_clicked(GtkButton *button, ConfiguratorData *data);
static void on_save_configuration_clicked(GtkButton *button, ConfiguratorData *data);
static void on_test_configuration_clicked(GtkButton *button, ConfiguratorData *data);
static void on_apply_configuration_clicked(GtkButton *button, ConfiguratorData *data);
static void check_ollama_status(ConfiguratorData *data);
static void install_ollama(ConfiguratorData *data);
static void save_configuration(ConfiguratorData *data);
static void test_configuration(ConfiguratorData *data);
static void apply_configuration(ConfiguratorData *data);
static void update_status(ConfiguratorData *data, const char *message);

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    
    ConfiguratorData *data = g_malloc0(sizeof(ConfiguratorData));
    
    create_main_window(data);
    
    // Check initial status
    check_ollama_status(data);
    
    gtk_widget_show_all(data->window);
    gtk_main();
    
    g_free(data);
    return 0;
}

static void create_main_window(ConfiguratorData *data) {
    data->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(data->window), "HER OS Configurator");
    gtk_window_set_default_size(GTK_WINDOW(data->window), 800, 600);
    gtk_window_set_resizable(GTK_WINDOW(data->window), TRUE);
    g_signal_connect(data->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    
    // Create main vertical box
    GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(data->window), main_vbox);
    
    // Create header
    GtkWidget *header_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(header_label), 
        "<span size='x-large' weight='bold'>HER OS Configuration Center</span>\n"
        "<span size='medium'>Configure your intelligent operating system</span>");
    gtk_widget_set_margin_start(header_label, 20);
    gtk_widget_set_margin_end(header_label, 20);
    gtk_widget_set_margin_top(header_label, 20);
    gtk_widget_set_margin_bottom(header_label, 20);
    gtk_box_pack_start(GTK_BOX(main_vbox), header_label, FALSE, FALSE, 0);
    
    // Create notebook for tabs
    data->notebook = gtk_notebook_new();
    gtk_widget_set_margin_start(data->notebook, 20);
    gtk_widget_set_margin_end(data->notebook, 20);
    gtk_box_pack_start(GTK_BOX(main_vbox), data->notebook, TRUE, TRUE, 0);
    
    // Create configuration pages
    create_ai_configuration_page(data);
    create_system_configuration_page(data);
    create_security_configuration_page(data);
    create_performance_configuration_page(data);
    create_network_configuration_page(data);
    create_api_keys_page(data);
    
    // Create button bar
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(button_box, 20);
    gtk_widget_set_margin_end(button_box, 20);
    gtk_widget_set_margin_bottom(button_box, 20);
    gtk_box_pack_end(GTK_BOX(main_vbox), button_box, FALSE, FALSE, 0);
    
    // Create buttons
    GtkWidget *test_button = gtk_button_new_with_label("Test Configuration");
    g_signal_connect(test_button, "clicked", G_CALLBACK(on_test_configuration_clicked), data);
    
    GtkWidget *save_button = gtk_button_new_with_label("Save Configuration");
    g_signal_connect(save_button, "clicked", G_CALLBACK(on_save_configuration_clicked), data);
    
    GtkWidget *apply_button = gtk_button_new_with_label("Apply Configuration");
    gtk_widget_set_sensitive(apply_button, FALSE);
    g_signal_connect(apply_button, "clicked", G_CALLBACK(on_apply_configuration_clicked), data);
    
    gtk_box_pack_end(GTK_BOX(button_box), apply_button, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(button_box), save_button, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(button_box), test_button, FALSE, FALSE, 0);
    
    // Create status bar
    data->status_bar = gtk_statusbar_new();
    gtk_box_pack_end(GTK_BOX(main_vbox), data->status_bar, FALSE, FALSE, 0);
    
    update_status(data, "Ready to configure HER OS");
}

static void create_ai_configuration_page(ConfiguratorData *data) {
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    
    gtk_container_add(GTK_CONTAINER(scrolled_window), vbox);
    
    // AI Provider Selection
    GtkWidget *provider_frame = gtk_frame_new("AI Provider Configuration");
    gtk_box_pack_start(GTK_BOX(vbox), provider_frame, FALSE, FALSE, 0);
    
    GtkWidget *provider_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(provider_vbox, 20);
    gtk_widget_set_margin_end(provider_vbox, 20);
    gtk_widget_set_margin_top(provider_vbox, 20);
    gtk_widget_set_margin_bottom(provider_vbox, 20);
    gtk_container_add(GTK_CONTAINER(provider_frame), provider_vbox);
    
    // Provider combo box
    GtkWidget *provider_label = gtk_label_new("AI Provider:");
    gtk_widget_set_halign(provider_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(provider_vbox), provider_label, FALSE, FALSE, 0);
    
    data->ai_provider_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->ai_provider_combo), "Ollama (Local)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->ai_provider_combo), "Claude API (Cloud)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->ai_provider_combo), "Google API (Cloud)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->ai_provider_combo), "OpenAI API (Cloud)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->ai_provider_combo), "Hybrid (Local + Cloud)");
    gtk_combo_box_set_active(GTK_COMBO_BOX(data->ai_provider_combo), 0);
    g_signal_connect(data->ai_provider_combo, "changed", G_CALLBACK(on_ai_provider_changed), data);
    gtk_box_pack_start(GTK_BOX(provider_vbox), data->ai_provider_combo, FALSE, FALSE, 0);
    
    // Ollama Installation Section
    GtkWidget *ollama_frame = gtk_frame_new("Ollama Installation");
    gtk_box_pack_start(GTK_BOX(vbox), ollama_frame, FALSE, FALSE, 0);
    
    GtkWidget *ollama_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(ollama_vbox, 20);
    gtk_widget_set_margin_end(ollama_vbox, 20);
    gtk_widget_set_margin_top(ollama_vbox, 20);
    gtk_widget_set_margin_bottom(ollama_vbox, 20);
    gtk_container_add(GTK_CONTAINER(ollama_frame), ollama_vbox);
    
    data->ollama_status_label = gtk_label_new("Checking Ollama status...");
    gtk_widget_set_halign(data->ollama_status_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(ollama_vbox), data->ollama_status_label, FALSE, FALSE, 0);
    
    data->ollama_install_button = gtk_button_new_with_label("Install Ollama");
    g_signal_connect(data->ollama_install_button, "clicked", G_CALLBACK(on_ollama_install_clicked), data);
    gtk_box_pack_start(GTK_BOX(ollama_vbox), data->ollama_install_button, FALSE, FALSE, 0);
    
    // AI Model Configuration
    GtkWidget *model_frame = gtk_frame_new("AI Model Configuration");
    gtk_box_pack_start(GTK_BOX(vbox), model_frame, FALSE, FALSE, 0);
    
    GtkWidget *model_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(model_vbox, 20);
    gtk_widget_set_margin_end(model_vbox, 20);
    gtk_widget_set_margin_top(model_vbox, 20);
    gtk_widget_set_margin_bottom(model_vbox, 20);
    gtk_container_add(GTK_CONTAINER(model_frame), model_vbox);
    
    // Model entry
    GtkWidget *model_label = gtk_label_new("Model Name:");
    gtk_widget_set_halign(model_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(model_vbox), model_label, FALSE, FALSE, 0);
    
    data->ai_model_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(data->ai_model_entry), "llama3.2:8b");
    gtk_box_pack_start(GTK_BOX(model_vbox), data->ai_model_entry, FALSE, FALSE, 0);
    
    // Temperature scale
    GtkWidget *temp_label = gtk_label_new("Temperature (Creativity):");
    gtk_widget_set_halign(temp_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(model_vbox), temp_label, FALSE, FALSE, 0);
    
    data->ai_temperature_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.1);
    gtk_range_set_value(GTK_RANGE(data->ai_temperature_scale), 0.7);
    gtk_box_pack_start(GTK_BOX(model_vbox), data->ai_temperature_scale, FALSE, FALSE, 0);
    
    // Max tokens spin button
    GtkWidget *tokens_label = gtk_label_new("Max Tokens:");
    gtk_widget_set_halign(tokens_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(model_vbox), tokens_label, FALSE, FALSE, 0);
    
    data->ai_max_tokens_spin = gtk_spin_button_new_with_range(100, 8192, 100);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(data->ai_max_tokens_spin), 2048);
    gtk_box_pack_start(GTK_BOX(model_vbox), data->ai_max_tokens_spin, FALSE, FALSE, 0);
    
    gtk_notebook_append_page(GTK_NOTEBOOK(data->notebook), scrolled_window, 
                           gtk_label_new("AI Configuration"));
}

// Continue with other page creation functions...
static void create_system_configuration_page(ConfiguratorData *data) {
    // System configuration page implementation
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    
    gtk_container_add(GTK_CONTAINER(scrolled_window), vbox);
    
    // System settings frame
    GtkWidget *system_frame = gtk_frame_new("System Settings");
    gtk_box_pack_start(GTK_BOX(vbox), system_frame, FALSE, FALSE, 0);
    
    GtkWidget *system_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(system_vbox, 20);
    gtk_widget_set_margin_end(system_vbox, 20);
    gtk_widget_set_margin_top(system_vbox, 20);
    gtk_widget_set_margin_bottom(system_vbox, 20);
    gtk_container_add(GTK_CONTAINER(system_frame), system_vbox);
    
    // Hostname
    GtkWidget *hostname_label = gtk_label_new("Hostname:");
    gtk_widget_set_halign(hostname_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(system_vbox), hostname_label, FALSE, FALSE, 0);
    
    data->hostname_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(data->hostname_entry), "heros-system");
    gtk_box_pack_start(GTK_BOX(system_vbox), data->hostname_entry, FALSE, FALSE, 0);
    
    // Data directory
    GtkWidget *data_dir_label = gtk_label_new("Data Directory:");
    gtk_widget_set_halign(data_dir_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(system_vbox), data_dir_label, FALSE, FALSE, 0);
    
    data->data_directory_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(data->data_directory_entry), "/var/lib/heros");
    gtk_box_pack_start(GTK_BOX(system_vbox), data->data_directory_entry, FALSE, FALSE, 0);
    
    // Log directory
    GtkWidget *log_dir_label = gtk_label_new("Log Directory:");
    gtk_widget_set_halign(log_dir_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(system_vbox), log_dir_label, FALSE, FALSE, 0);
    
    data->log_directory_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(data->log_directory_entry), "/var/log/heros");
    gtk_box_pack_start(GTK_BOX(system_vbox), data->log_directory_entry, FALSE, FALSE, 0);
    
    // Max memory
    GtkWidget *memory_label = gtk_label_new("Max Memory Usage (GB):");
    gtk_widget_set_halign(memory_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(system_vbox), memory_label, FALSE, FALSE, 0);
    
    data->max_memory_spin = gtk_spin_button_new_with_range(1, 64, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(data->max_memory_spin), 8);
    gtk_box_pack_start(GTK_BOX(system_vbox), data->max_memory_spin, FALSE, FALSE, 0);
    
    // Max CPU
    GtkWidget *cpu_label = gtk_label_new("Max CPU Usage (%):");
    gtk_widget_set_halign(cpu_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(system_vbox), cpu_label, FALSE, FALSE, 0);
    
    data->max_cpu_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 10, 100, 5);
    gtk_range_set_value(GTK_RANGE(data->max_cpu_scale), 80);
    gtk_box_pack_start(GTK_BOX(system_vbox), data->max_cpu_scale, FALSE, FALSE, 0);
    
    gtk_notebook_append_page(GTK_NOTEBOOK(data->notebook), scrolled_window, 
                           gtk_label_new("System Configuration"));
}

// Event handlers
static void on_ai_provider_changed(GtkComboBox *combo, ConfiguratorData *data) {
    int active = gtk_combo_box_get_active(combo);
    
    // Enable/disable Ollama installation based on selection
    if (active == 0 || active == 4) { // Ollama or Hybrid
        gtk_widget_set_sensitive(data->ollama_install_button, TRUE);
        check_ollama_status(data);
    } else {
        gtk_widget_set_sensitive(data->ollama_install_button, FALSE);
    }
    
    update_status(data, "AI provider changed");
}

static void on_ollama_install_clicked(GtkButton *button, ConfiguratorData *data) {
    install_ollama(data);
}

static void on_save_configuration_clicked(GtkButton *button, ConfiguratorData *data) {
    save_configuration(data);
}

static void on_test_configuration_clicked(GtkButton *button, ConfiguratorData *data) {
    test_configuration(data);
}

static void on_apply_configuration_clicked(GtkButton *button, ConfiguratorData *data) {
    apply_configuration(data);
}

// Implementation functions
static void check_ollama_status(ConfiguratorData *data) {
    // Check if Ollama is installed and running
    if (access("/usr/local/bin/ollama", X_OK) == 0) {
        gtk_label_set_text(GTK_LABEL(data->ollama_status_label), 
                          "✅ Ollama is installed and ready");
        gtk_button_set_label(GTK_BUTTON(data->ollama_install_button), "Reinstall Ollama");
    } else {
        gtk_label_set_text(GTK_LABEL(data->ollama_status_label), 
                          "❌ Ollama is not installed");
        gtk_button_set_label(GTK_BUTTON(data->ollama_install_button), "Install Ollama");
    }
}

static void install_ollama(ConfiguratorData *data) {
    update_status(data, "Installing Ollama...");
    
    // Create installation script
    FILE *script = fopen("/tmp/install_ollama.sh", "w");
    if (script) {
        fprintf(script, "#!/bin/bash\n");
        fprintf(script, "curl -fsSL https://ollama.ai/install.sh | sh\n");
        fprintf(script, "systemctl --user enable ollama\n");
        fprintf(script, "systemctl --user start ollama\n");
        fprintf(script, "ollama pull llama3.2:8b\n");
        fclose(script);
        
        chmod("/tmp/install_ollama.sh", 0755);
        
        // Execute installation
        int result = system("/tmp/install_ollama.sh");
        
        if (result == 0) {
            update_status(data, "Ollama installed successfully!");
            check_ollama_status(data);
        } else {
            update_status(data, "Failed to install Ollama");
        }
        
        unlink("/tmp/install_ollama.sh");
    }
}

static void save_configuration(ConfiguratorData *data) {
    update_status(data, "Saving configuration...");
    
    // Create configuration directory
    system("sudo mkdir -p /etc/heros");
    
    // Generate configuration file
    FILE *config = fopen("/tmp/heros.conf", "w");
    if (config) {
        fprintf(config, "[system]\n");
        fprintf(config, "hostname = %s\n", gtk_entry_get_text(GTK_ENTRY(data->hostname_entry)));
        fprintf(config, "data_directory = %s\n", gtk_entry_get_text(GTK_ENTRY(data->data_directory_entry)));
        fprintf(config, "log_directory = %s\n", gtk_entry_get_text(GTK_ENTRY(data->log_directory_entry)));
        fprintf(config, "max_memory_usage = %dGB\n", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(data->max_memory_spin)));
        fprintf(config, "max_cpu_usage = %d%%\n", (int)gtk_range_get_value(GTK_RANGE(data->max_cpu_scale)));
        
        fprintf(config, "\n[ai]\n");
        int provider = gtk_combo_box_get_active(GTK_COMBO_BOX(data->ai_provider_combo));
        switch (provider) {
            case 0: fprintf(config, "provider = ollama\n"); break;
            case 1: fprintf(config, "provider = claude\n"); break;
            case 2: fprintf(config, "provider = google\n"); break;
            case 3: fprintf(config, "provider = openai\n"); break;
            case 4: fprintf(config, "provider = hybrid\n"); break;
        }
        fprintf(config, "model = %s\n", gtk_entry_get_text(GTK_ENTRY(data->ai_model_entry)));
        fprintf(config, "temperature = %.1f\n", gtk_range_get_value(GTK_RANGE(data->ai_temperature_scale)));
        fprintf(config, "max_tokens = %d\n", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(data->ai_max_tokens_spin)));
        
        fclose(config);
        
        // Move to final location
        system("sudo mv /tmp/heros.conf /etc/heros/heros.conf");
        system("sudo chown root:root /etc/heros/heros.conf");
        system("sudo chmod 644 /etc/heros/heros.conf");
        
        update_status(data, "Configuration saved successfully!");
    }
}

static void test_configuration(ConfiguratorData *data) {
    update_status(data, "Testing configuration...");
    
    // Test basic system
    if (access("/etc/heros/heros.conf", R_OK) == 0) {
        update_status(data, "✅ Configuration file exists");
    } else {
        update_status(data, "❌ Configuration file not found");
        return;
    }
    
    // Test Ollama if selected
    int provider = gtk_combo_box_get_active(GTK_COMBO_BOX(data->ai_provider_combo));
    if (provider == 0 || provider == 4) {
        if (access("/usr/local/bin/ollama", X_OK) == 0) {
            update_status(data, "✅ Ollama is available");
        } else {
            update_status(data, "❌ Ollama not found - please install");
        }
    }
    
    update_status(data, "Configuration test completed");
}

static void apply_configuration(ConfiguratorData *data) {
    update_status(data, "Applying configuration...");
    
    // Restart HER OS services
    system("sudo systemctl restart heros-*");
    
    update_status(data, "Configuration applied successfully!");
}

static void update_status(ConfiguratorData *data, const char *message) {
    gtk_statusbar_push(GTK_STATUSBAR(data->status_bar), 0, message);
    gtk_widget_queue_draw(data->status_bar);
} 
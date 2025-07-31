#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <errno.h>
#include <json-glib/json-glib.h>

// Enhanced configuration structure
typedef struct {
    GtkWidget *window;
    GtkWidget *notebook;
    GtkWidget *status_bar;
    
    // AI Configuration - Enhanced
    GtkWidget *ai_mode_combo;           // Local, Online, Hybrid
    GtkWidget *ai_provider_combo;       // Ollama, Claude, Google, OpenAI
    GtkWidget *ai_model_entry;
    GtkWidget *ai_temperature_scale;
    GtkWidget *ai_max_tokens_spin;
    GtkWidget *ai_top_p_scale;
    GtkWidget *ai_frequency_penalty_scale;
    GtkWidget *ai_presence_penalty_scale;
    
    // Local AI Settings
    GtkWidget *ollama_install_button;
    GtkWidget *ollama_status_label;
    GtkWidget *ollama_model_combo;
    GtkWidget *ollama_gpu_check;
    GtkWidget *ollama_memory_spin;
    
    // Online AI Settings
    GtkWidget *online_api_key_entry;
    GtkWidget *online_cost_limit_spin;
    GtkWidget *online_rate_limit_spin;
    GtkWidget *online_timeout_spin;
    
    // Hybrid AI Settings
    GtkWidget *hybrid_strategy_combo;
    GtkWidget *hybrid_local_threshold_scale;
    GtkWidget *hybrid_cost_optimization_check;
    GtkWidget *hybrid_privacy_mode_check;
    
    // Model-specific settings
    GtkWidget *model_temperature_label;
    GtkWidget *model_capabilities_label;
    GtkWidget *model_memory_label;
    GtkWidget *model_speed_label;
    
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
    
    // Advanced Settings
    GtkWidget *advanced_debug_switch;
    GtkWidget *advanced_profiling_switch;
    GtkWidget *advanced_log_level_combo;
    GtkWidget *advanced_backup_switch;
    
} EnhancedConfiguratorData;

// Function prototypes
static void create_enhanced_main_window(EnhancedConfiguratorData *data);
static void create_enhanced_ai_configuration_page(EnhancedConfiguratorData *data);
static void create_system_configuration_page(EnhancedConfiguratorData *data);
static void create_security_configuration_page(EnhancedConfiguratorData *data);
static void create_performance_configuration_page(EnhancedConfiguratorData *data);
static void create_network_configuration_page(EnhancedConfiguratorData *data);
static void create_api_keys_page(EnhancedConfiguratorData *data);
static void create_advanced_settings_page(EnhancedConfiguratorData *data);

// Event handlers
static void on_ai_mode_changed(GtkComboBox *combo, EnhancedConfiguratorData *data);
static void on_ai_provider_changed(GtkComboBox *combo, EnhancedConfiguratorData *data);
static void on_ollama_model_changed(GtkComboBox *combo, EnhancedConfiguratorData *data);
static void on_temperature_changed(GtkRange *range, EnhancedConfiguratorData *data);
static void on_ollama_install_clicked(GtkButton *button, EnhancedConfiguratorData *data);
static void on_save_configuration_clicked(GtkButton *button, EnhancedConfiguratorData *data);
static void on_test_configuration_clicked(GtkButton *button, EnhancedConfiguratorData *data);
static void on_apply_configuration_clicked(GtkButton *button, EnhancedConfiguratorData *data);
static void on_advanced_backup_clicked(GtkButton *button, EnhancedConfiguratorData *data);
static void on_advanced_restore_clicked(GtkButton *button, EnhancedConfiguratorData *data);

// Implementation functions
static void check_ollama_status(EnhancedConfiguratorData *data);
static void install_ollama(EnhancedConfiguratorData *data);
static void update_model_info(EnhancedConfiguratorData *data);
static void save_enhanced_configuration(EnhancedConfiguratorData *data);
static void test_enhanced_configuration(EnhancedConfiguratorData *data);
static void apply_enhanced_configuration(EnhancedConfiguratorData *data);
static void update_status(EnhancedConfiguratorData *data, const char *message);
static void create_backup(EnhancedConfiguratorData *data);
static void restore_backup(EnhancedConfiguratorData *data);

// Model information structure
typedef struct {
    const char *name;
    const char *description;
    double min_temp;
    double max_temp;
    double default_temp;
    int min_tokens;
    int max_tokens;
    int default_tokens;
    const char *capabilities;
    const char *memory_usage;
    const char *speed;
    const char *cost;
} ModelInfo;

// Model database
static ModelInfo model_database[] = {
    // Local Models (Ollama)
    {
        "llama3.2:3b", "Fast, lightweight model for basic tasks",
        0.0, 1.0, 0.7, 100, 2048, 1024,
        "Text generation, basic reasoning",
        "2GB RAM", "Fast", "Free"
    },
    {
        "llama3.2:8b", "Balanced model for most tasks",
        0.0, 1.0, 0.7, 100, 4096, 2048,
        "Text generation, reasoning, coding",
        "4GB RAM", "Medium", "Free"
    },
    {
        "llama3.2:70b", "High-quality model for complex tasks",
        0.0, 1.0, 0.7, 100, 8192, 4096,
        "Advanced reasoning, complex analysis",
        "16GB RAM", "Slow", "Free"
    },
    {
        "codellama:7b", "Specialized for code generation",
        0.0, 1.0, 0.3, 100, 4096, 2048,
        "Code generation, debugging, analysis",
        "4GB RAM", "Medium", "Free"
    },
    {
        "mistral:7b", "Excellent general-purpose model",
        0.0, 1.0, 0.7, 100, 4096, 2048,
        "Text generation, reasoning, analysis",
        "4GB RAM", "Medium", "Free"
    },
    {
        "phi3:3.8b", "Microsoft's efficient model",
        0.0, 1.0, 0.7, 100, 2048, 1024,
        "Text generation, basic reasoning",
        "2GB RAM", "Fast", "Free"
    },
    
    // Cloud Models
    {
        "claude-3-sonnet-20240229", "Anthropic's balanced model",
        0.0, 1.0, 0.7, 100, 4096, 2048,
        "Advanced reasoning, analysis, coding",
        "Cloud", "Fast", "$0.003/1K input"
    },
    {
        "claude-3-opus-20240229", "Anthropic's most capable model",
        0.0, 1.0, 0.7, 100, 4096, 2048,
        "Complex reasoning, analysis, creation",
        "Cloud", "Medium", "$0.015/1K input"
    },
    {
        "gpt-4o", "OpenAI's latest model",
        0.0, 1.0, 0.7, 100, 4096, 2048,
        "Advanced reasoning, analysis, coding",
        "Cloud", "Fast", "$0.005/1K input"
    },
    {
        "gemini-1.5-pro", "Google's advanced model",
        0.0, 1.0, 0.7, 100, 8192, 4096,
        "Advanced reasoning, analysis, coding",
        "Cloud", "Fast", "$0.0005/1K input"
    }
};

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    
    EnhancedConfiguratorData *data = g_malloc0(sizeof(EnhancedConfiguratorData));
    
    create_enhanced_main_window(data);
    
    // Check initial status
    check_ollama_status(data);
    
    gtk_widget_show_all(data->window);
    gtk_main();
    
    g_free(data);
    return 0;
}

static void create_enhanced_main_window(EnhancedConfiguratorData *data) {
    data->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(data->window), "HER OS Enhanced Configurator");
    gtk_window_set_default_size(GTK_WINDOW(data->window), 1000, 700);
    gtk_window_set_resizable(GTK_WINDOW(data->window), TRUE);
    g_signal_connect(data->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    
    // Create main vertical box
    GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(data->window), main_vbox);
    
    // Create enhanced header
    GtkWidget *header_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(header_label), 
        "<span size='x-large' weight='bold'>HER OS Enhanced Configuration Center</span>\n"
        "<span size='medium'>Advanced configuration for your intelligent operating system</span>");
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
    
    // Create enhanced configuration pages
    create_enhanced_ai_configuration_page(data);
    create_system_configuration_page(data);
    create_security_configuration_page(data);
    create_performance_configuration_page(data);
    create_network_configuration_page(data);
    create_api_keys_page(data);
    create_advanced_settings_page(data);
    
    // Create enhanced button bar
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(button_box, 20);
    gtk_widget_set_margin_end(button_box, 20);
    gtk_widget_set_margin_bottom(button_box, 20);
    gtk_box_pack_end(GTK_BOX(main_vbox), button_box, FALSE, FALSE, 0);
    
    // Create enhanced buttons
    GtkWidget *backup_button = gtk_button_new_with_label("Backup Config");
    g_signal_connect(backup_button, "clicked", G_CALLBACK(on_advanced_backup_clicked), data);
    
    GtkWidget *restore_button = gtk_button_new_with_label("Restore Config");
    g_signal_connect(restore_button, "clicked", G_CALLBACK(on_advanced_restore_clicked), data);
    
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
    gtk_box_pack_end(GTK_BOX(button_box), restore_button, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(button_box), backup_button, FALSE, FALSE, 0);
    
    // Create status bar
    data->status_bar = gtk_statusbar_new();
    gtk_box_pack_end(GTK_BOX(main_vbox), data->status_bar, FALSE, FALSE, 0);
    
    update_status(data, "Enhanced HER OS Configurator ready");
}

static void create_enhanced_ai_configuration_page(EnhancedConfiguratorData *data) {
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    
    gtk_container_add(GTK_CONTAINER(scrolled_window), vbox);
    
    // AI Mode Selection (Local, Online, Hybrid)
    GtkWidget *mode_frame = gtk_frame_new("AI Operation Mode");
    gtk_box_pack_start(GTK_BOX(vbox), mode_frame, FALSE, FALSE, 0);
    
    GtkWidget *mode_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(mode_vbox, 20);
    gtk_widget_set_margin_end(mode_vbox, 20);
    gtk_widget_set_margin_top(mode_vbox, 20);
    gtk_widget_set_margin_bottom(mode_vbox, 20);
    gtk_container_add(GTK_CONTAINER(mode_frame), mode_vbox);
    
    GtkWidget *mode_label = gtk_label_new("AI Operation Mode:");
    gtk_widget_set_halign(mode_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(mode_vbox), mode_label, FALSE, FALSE, 0);
    
    data->ai_mode_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->ai_mode_combo), "🖥️ Local AI (Ollama) - Free, Private, Offline");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->ai_mode_combo), "☁️ Online AI (Cloud APIs) - Powerful, Paid");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->ai_mode_combo), "🔄 Hybrid AI (Local + Cloud) - Best of Both Worlds");
    gtk_combo_box_set_active(GTK_COMBO_BOX(data->ai_mode_combo), 0);
    g_signal_connect(data->ai_mode_combo, "changed", G_CALLBACK(on_ai_mode_changed), data);
    gtk_box_pack_start(GTK_BOX(mode_vbox), data->ai_mode_combo, FALSE, FALSE, 0);
    
    // AI Provider Selection
    GtkWidget *provider_frame = gtk_frame_new("AI Provider Configuration");
    gtk_box_pack_start(GTK_BOX(vbox), provider_frame, FALSE, FALSE, 0);
    
    GtkWidget *provider_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(provider_vbox, 20);
    gtk_widget_set_margin_end(provider_vbox, 20);
    gtk_widget_set_margin_top(provider_vbox, 20);
    gtk_widget_set_margin_bottom(provider_vbox, 20);
    gtk_container_add(GTK_CONTAINER(provider_frame), provider_vbox);
    
    GtkWidget *provider_label = gtk_label_new("AI Provider:");
    gtk_widget_set_halign(provider_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(provider_vbox), provider_label, FALSE, FALSE, 0);
    
    data->ai_provider_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->ai_provider_combo), "Ollama (Local)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->ai_provider_combo), "Claude API (Anthropic)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->ai_provider_combo), "Google API (Gemini)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->ai_provider_combo), "OpenAI API (GPT)");
    gtk_combo_box_set_active(GTK_COMBO_BOX(data->ai_provider_combo), 0);
    g_signal_connect(data->ai_provider_combo, "changed", G_CALLBACK(on_ai_provider_changed), data);
    gtk_box_pack_start(GTK_BOX(provider_vbox), data->ai_provider_combo, FALSE, FALSE, 0);
    
    // Ollama Installation Section
    GtkWidget *ollama_frame = gtk_frame_new("Ollama Installation & Management");
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
    
    // Ollama Model Selection
    GtkWidget *ollama_model_label = gtk_label_new("Ollama Model:");
    gtk_widget_set_halign(ollama_model_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(ollama_vbox), ollama_model_label, FALSE, FALSE, 0);
    
    data->ollama_model_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->ollama_model_combo), "llama3.2:3b - Fast, lightweight (2GB RAM)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->ollama_model_combo), "llama3.2:8b - Balanced (4GB RAM)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->ollama_model_combo), "llama3.2:70b - High quality (16GB RAM)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->ollama_model_combo), "codellama:7b - Code specialized (4GB RAM)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->ollama_model_combo), "mistral:7b - Excellent general purpose (4GB RAM)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->ollama_model_combo), "phi3:3.8b - Microsoft efficient (2GB RAM)");
    gtk_combo_box_set_active(GTK_COMBO_BOX(data->ollama_model_combo), 1);
    g_signal_connect(data->ollama_model_combo, "changed", G_CALLBACK(on_ollama_model_changed), data);
    gtk_box_pack_start(GTK_BOX(ollama_vbox), data->ollama_model_combo, FALSE, FALSE, 0);
    
    // Ollama Advanced Settings
    GtkWidget *ollama_advanced_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_box_pack_start(GTK_BOX(ollama_vbox), ollama_advanced_hbox, FALSE, FALSE, 0);
    
    data->ollama_gpu_check = gtk_check_button_new_with_label("Enable GPU acceleration");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->ollama_gpu_check), TRUE);
    gtk_box_pack_start(GTK_BOX(ollama_advanced_hbox), data->ollama_gpu_check, FALSE, FALSE, 0);
    
    GtkWidget *memory_label = gtk_label_new("GPU Memory (GB):");
    gtk_box_pack_start(GTK_BOX(ollama_advanced_hbox), memory_label, FALSE, FALSE, 0);
    
    data->ollama_memory_spin = gtk_spin_button_new_with_range(1, 32, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(data->ollama_memory_spin), 4);
    gtk_box_pack_start(GTK_BOX(ollama_advanced_hbox), data->ollama_memory_spin, FALSE, FALSE, 0);
    
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
    
    // Model information display
    GtkWidget *model_info_frame = gtk_frame_new("Model Information");
    gtk_box_pack_start(GTK_BOX(model_vbox), model_info_frame, FALSE, FALSE, 0);
    
    GtkWidget *model_info_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_start(model_info_vbox, 10);
    gtk_widget_set_margin_end(model_info_vbox, 10);
    gtk_widget_set_margin_top(model_info_vbox, 10);
    gtk_widget_set_margin_bottom(model_info_vbox, 10);
    gtk_container_add(GTK_CONTAINER(model_info_frame), model_info_vbox);
    
    data->model_temperature_label = gtk_label_new("Temperature Range: 0.0 - 1.0 (Default: 0.7)");
    gtk_widget_set_halign(data->model_temperature_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(model_info_vbox), data->model_temperature_label, FALSE, FALSE, 0);
    
    data->model_capabilities_label = gtk_label_new("Capabilities: Text generation, reasoning, coding");
    gtk_widget_set_halign(data->model_capabilities_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(model_info_vbox), data->model_capabilities_label, FALSE, FALSE, 0);
    
    data->model_memory_label = gtk_label_new("Memory Usage: 4GB RAM");
    gtk_widget_set_halign(data->model_memory_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(model_info_vbox), data->model_memory_label, FALSE, FALSE, 0);
    
    data->model_speed_label = gtk_label_new("Speed: Medium | Cost: Free");
    gtk_widget_set_halign(data->model_speed_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(model_info_vbox), data->model_speed_label, FALSE, FALSE, 0);
    
    // Advanced AI Parameters
    GtkWidget *params_frame = gtk_frame_new("Advanced AI Parameters");
    gtk_box_pack_start(GTK_BOX(vbox), params_frame, FALSE, FALSE, 0);
    
    GtkWidget *params_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(params_vbox, 20);
    gtk_widget_set_margin_end(params_vbox, 20);
    gtk_widget_set_margin_top(params_vbox, 20);
    gtk_widget_set_margin_bottom(params_vbox, 20);
    gtk_container_add(GTK_CONTAINER(params_frame), params_vbox);
    
    // Temperature scale with detailed labels
    GtkWidget *temp_label = gtk_label_new("Temperature (Creativity): 0.0 = Focused, 1.0 = Creative");
    gtk_widget_set_halign(temp_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(params_vbox), temp_label, FALSE, FALSE, 0);
    
    data->ai_temperature_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.1);
    gtk_range_set_value(GTK_RANGE(data->ai_temperature_scale), 0.7);
    g_signal_connect(data->ai_temperature_scale, "value-changed", G_CALLBACK(on_temperature_changed), data);
    gtk_box_pack_start(GTK_BOX(params_vbox), data->ai_temperature_scale, FALSE, FALSE, 0);
    
    // Temperature value display
    GtkWidget *temp_value_label = gtk_label_new("Current: 0.7");
    gtk_widget_set_halign(temp_value_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(params_vbox), temp_value_label, FALSE, FALSE, 0);
    
    // Top-p scale
    GtkWidget *top_p_label = gtk_label_new("Top-p (Nucleus Sampling): 0.0 = Deterministic, 1.0 = Diverse");
    gtk_widget_set_halign(top_p_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(params_vbox), top_p_label, FALSE, FALSE, 0);
    
    data->ai_top_p_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.05);
    gtk_range_set_value(GTK_RANGE(data->ai_top_p_scale), 0.9);
    gtk_box_pack_start(GTK_BOX(params_vbox), data->ai_top_p_scale, FALSE, FALSE, 0);
    
    // Frequency penalty
    GtkWidget *freq_label = gtk_label_new("Frequency Penalty: Reduce repetition");
    gtk_widget_set_halign(freq_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(params_vbox), freq_label, FALSE, FALSE, 0);
    
    data->ai_frequency_penalty_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 2.0, 0.1);
    gtk_range_set_value(GTK_RANGE(data->ai_frequency_penalty_scale), 0.0);
    gtk_box_pack_start(GTK_BOX(params_vbox), data->ai_frequency_penalty_scale, FALSE, FALSE, 0);
    
    // Presence penalty
    GtkWidget *presence_label = gtk_label_new("Presence Penalty: Encourage new topics");
    gtk_widget_set_halign(presence_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(params_vbox), presence_label, FALSE, FALSE, 0);
    
    data->ai_presence_penalty_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 2.0, 0.1);
    gtk_range_set_value(GTK_RANGE(data->ai_presence_penalty_scale), 0.0);
    gtk_box_pack_start(GTK_BOX(params_vbox), data->ai_presence_penalty_scale, FALSE, FALSE, 0);
    
    // Max tokens spin button
    GtkWidget *tokens_label = gtk_label_new("Max Tokens (Response Length):");
    gtk_widget_set_halign(tokens_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(params_vbox), tokens_label, FALSE, FALSE, 0);
    
    data->ai_max_tokens_spin = gtk_spin_button_new_with_range(100, 8192, 100);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(data->ai_max_tokens_spin), 2048);
    gtk_box_pack_start(GTK_BOX(params_vbox), data->ai_max_tokens_spin, FALSE, FALSE, 0);
    
    // Hybrid Configuration
    GtkWidget *hybrid_frame = gtk_frame_new("Hybrid AI Configuration");
    gtk_box_pack_start(GTK_BOX(vbox), hybrid_frame, FALSE, FALSE, 0);
    
    GtkWidget *hybrid_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(hybrid_vbox, 20);
    gtk_widget_set_margin_end(hybrid_vbox, 20);
    gtk_widget_set_margin_top(hybrid_vbox, 20);
    gtk_widget_set_margin_bottom(hybrid_vbox, 20);
    gtk_container_add(GTK_CONTAINER(hybrid_frame), hybrid_vbox);
    
    GtkWidget *strategy_label = gtk_label_new("Hybrid Strategy:");
    gtk_widget_set_halign(strategy_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(hybrid_vbox), strategy_label, FALSE, FALSE, 0);
    
    data->hybrid_strategy_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->hybrid_strategy_combo), "Smart Routing - AI decides based on task complexity");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->hybrid_strategy_combo), "Cost Optimization - Minimize cloud API usage");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->hybrid_strategy_combo), "Privacy First - Use local for sensitive data");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->hybrid_strategy_combo), "Performance First - Use cloud for speed");
    gtk_combo_box_set_active(GTK_COMBO_BOX(data->hybrid_strategy_combo), 0);
    gtk_box_pack_start(GTK_BOX(hybrid_vbox), data->hybrid_strategy_combo, FALSE, FALSE, 0);
    
    GtkWidget *threshold_label = gtk_label_new("Local Processing Threshold (Complexity):");
    gtk_widget_set_halign(threshold_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(hybrid_vbox), threshold_label, FALSE, FALSE, 0);
    
    data->hybrid_local_threshold_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.1);
    gtk_range_set_value(GTK_RANGE(data->hybrid_local_threshold_scale), 0.5);
    gtk_box_pack_start(GTK_BOX(hybrid_vbox), data->hybrid_local_threshold_scale, FALSE, FALSE, 0);
    
    data->hybrid_cost_optimization_check = gtk_check_button_new_with_label("Enable cost optimization");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->hybrid_cost_optimization_check), TRUE);
    gtk_box_pack_start(GTK_BOX(hybrid_vbox), data->hybrid_cost_optimization_check, FALSE, FALSE, 0);
    
    data->hybrid_privacy_mode_check = gtk_check_button_new_with_label("Enable privacy mode (local processing for sensitive data)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->hybrid_privacy_mode_check), TRUE);
    gtk_box_pack_start(GTK_BOX(hybrid_vbox), data->hybrid_privacy_mode_check, FALSE, FALSE, 0);
    
    gtk_notebook_append_page(GTK_NOTEBOOK(data->notebook), scrolled_window, 
                           gtk_label_new("🤖 AI Configuration"));
}

// Event handlers
static void on_ai_mode_changed(GtkComboBox *combo, EnhancedConfiguratorData *data) {
    int mode = gtk_combo_box_get_active(combo);
    
    // Update provider combo based on mode
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(data->ai_provider_combo));
    
    switch (mode) {
        case 0: // Local
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->ai_provider_combo), "Ollama (Local)");
            gtk_combo_box_set_active(GTK_COMBO_BOX(data->ai_provider_combo), 0);
            gtk_widget_set_sensitive(data->ollama_install_button, TRUE);
            check_ollama_status(data);
            break;
        case 1: // Online
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->ai_provider_combo), "Claude API (Anthropic)");
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->ai_provider_combo), "Google API (Gemini)");
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->ai_provider_combo), "OpenAI API (GPT)");
            gtk_combo_box_set_active(GTK_COMBO_BOX(data->ai_provider_combo), 0);
            gtk_widget_set_sensitive(data->ollama_install_button, FALSE);
            break;
        case 2: // Hybrid
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->ai_provider_combo), "Ollama + Claude API");
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->ai_provider_combo), "Ollama + Google API");
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(data->ai_provider_combo), "Ollama + OpenAI API");
            gtk_combo_box_set_active(GTK_COMBO_BOX(data->ai_provider_combo), 0);
            gtk_widget_set_sensitive(data->ollama_install_button, TRUE);
            check_ollama_status(data);
            break;
    }
    
    update_status(data, "AI mode changed");
}

static void on_ai_provider_changed(GtkComboBox *combo, EnhancedConfiguratorData *data) {
    int provider = gtk_combo_box_get_active(combo);
    update_model_info(data);
    update_status(data, "AI provider changed");
}

static void on_ollama_model_changed(GtkComboBox *combo, EnhancedConfiguratorData *data) {
    int model_index = gtk_combo_box_get_active(combo);
    if (model_index >= 0 && model_index < 6) { // Local models
        const char *model_name = model_database[model_index].name;
        gtk_entry_set_text(GTK_ENTRY(data->ai_model_entry), model_name);
        update_model_info(data);
    }
    update_status(data, "Ollama model changed");
}

static void on_temperature_changed(GtkRange *range, EnhancedConfiguratorData *data) {
    double temp = gtk_range_get_value(range);
    char temp_text[100];
    snprintf(temp_text, sizeof(temp_text), "Current: %.1f", temp);
    
    // Update temperature label
    GtkWidget *temp_label = gtk_widget_get_parent(data->ai_temperature_scale);
    GList *children = gtk_container_get_children(GTK_CONTAINER(temp_label));
    if (g_list_length(children) >= 3) {
        GtkWidget *value_label = g_list_nth_data(children, 2);
        gtk_label_set_text(GTK_LABEL(value_label), temp_text);
    }
    
    update_status(data, "Temperature adjusted");
}

// Continue with other functions...
static void check_ollama_status(EnhancedConfiguratorData *data) {
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

static void update_model_info(EnhancedConfiguratorData *data) {
    // Find current model in database
    const char *current_model = gtk_entry_get_text(GTK_ENTRY(data->ai_model_entry));
    ModelInfo *model = NULL;
    
    for (int i = 0; i < sizeof(model_database) / sizeof(ModelInfo); i++) {
        if (strcmp(model_database[i].name, current_model) == 0) {
            model = &model_database[i];
            break;
        }
    }
    
    if (model) {
        char temp_text[200];
        snprintf(temp_text, sizeof(temp_text), "Temperature Range: %.1f - %.1f (Default: %.1f)", 
                model->min_temp, model->max_temp, model->default_temp);
        gtk_label_set_text(GTK_LABEL(data->model_temperature_label), temp_text);
        
        gtk_label_set_text(GTK_LABEL(data->model_capabilities_label), model->capabilities);
        gtk_label_set_text(GTK_LABEL(data->model_memory_label), model->memory_usage);
        gtk_label_set_text(GTK_LABEL(data->model_speed_label), model->speed);
        
        // Update temperature scale range
        gtk_range_set_range(GTK_RANGE(data->ai_temperature_scale), model->min_temp, model->max_temp);
        gtk_range_set_value(GTK_RANGE(data->ai_temperature_scale), model->default_temp);
        
        // Update token range
        gtk_spin_button_set_range(GTK_SPIN_BUTTON(data->ai_max_tokens_spin), model->min_tokens, model->max_tokens);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(data->ai_max_tokens_spin), model->default_tokens);
    }
}

static void update_status(EnhancedConfiguratorData *data, const char *message) {
    gtk_statusbar_push(GTK_STATUSBAR(data->status_bar), 0, message);
    gtk_widget_queue_draw(data->status_bar);
} 
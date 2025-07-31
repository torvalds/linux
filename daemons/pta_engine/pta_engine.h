/*
 * pta_engine.h - Proactive Task Anticipation Engine Header
 *
 * Defines the core structures, functions, and constants for the PTA Engine.
 * Follows Linux kernel coding style (K&R, tabs, block comments).
 *
 * Author: HER OS Project
 */

#ifndef PTA_ENGINE_H
#define PTA_ENGINE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sqlite3.h>
#include <json-c/json.h>
#include <glib.h>
#include <gio/gio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* PTA Engine constants */
#define PTA_DBUS_NAME "org.heros.PTA"
#define PTA_DBUS_PATH "/org/heros/PTA"
#define PTA_DBUS_INTERFACE "org.heros.PTA"
#define METADATA_SOCKET_PATH "/tmp/heros_metadata.sock"
#define ACTION_DBUS_NAME "org.heros.Action"
#define ACTION_DBUS_PATH "/org/heros/Action"
#define ACTION_DBUS_INTERFACE "org.heros.Action"

/* Performance and configuration constants */
#define MAX_EVENT_QUEUE_SIZE 1000
#define MAX_RULES 100
#define MAX_CONTEXT_HISTORY 1000
#define MAX_SUGGESTIONS 10
#define EVENT_PROCESSING_TIMEOUT_MS 50
#define SUGGESTION_GENERATION_TIMEOUT_MS 100

/* Attention credit constants */
#define DEFAULT_BASE_CREDITS 100
#define DEFAULT_RECOVERY_RATE 5.0
#define MAX_CREDITS 1000
#define MIN_CREDITS 0

/* Rule types */
typedef enum {
	RULE_TYPE_PATTERN = 0,
	RULE_TYPE_SEQUENCE,
	RULE_TYPE_TEMPORAL,
	RULE_TYPE_CONTEXT
} rule_type_t;

/* Rule priority levels */
typedef enum {
	PRIORITY_LOW = 0,
	PRIORITY_MEDIUM,
	PRIORITY_HIGH,
	PRIORITY_CRITICAL
} rule_priority_t;

/* Event types from UKG */
typedef enum {
	EVENT_UI_FOCUS = 0,
	EVENT_UI_TEXT_CHANGE,
	EVENT_UI_SELECTION,
	EVENT_UI_WINDOW_ACTIVATE,
	EVENT_UI_WINDOW_DEACTIVATE,
	EVENT_UI_BUTTON_CLICK,
	EVENT_UI_TEXT_READ,
	EVENT_UI_TEXT_WRITE,
	EVENT_UI_TREE_ACCESS,
	EVENT_FILE_OPEN,
	EVENT_FILE_SAVE,
	EVENT_APP_LAUNCH,
	EVENT_APP_CLOSE,
	EVENT_MAX
} event_type_t;

/* Suggestion types */
typedef enum {
	SUGGESTION_AUTOMATION = 0,
	SUGGESTION_NOTIFICATION,
	SUGGESTION_RESOURCE_PREP,
	SUGGESTION_WORKFLOW_OPT
} suggestion_type_t;

/*
 * UKG Event structure
 */
typedef struct {
	event_type_t type;
	char *app_name;
	int app_pid;
	char *element_name;
	char *element_role;
	char *context_data;
	time_t timestamp;
} ukg_event_t;

/*
 * Context model structure
 */
typedef struct {
	char *current_app;
	int current_app_pid;
	char *current_workflow;
	time_t session_start;
	int total_events;
	int events_this_session;
	GHashTable *app_usage;  /* app_name -> usage_count */
	GHashTable *element_interactions;  /* element_name -> interaction_count */
} context_model_t;

/*
 * Rule structure
 */
typedef struct {
	char *name;
	rule_type_t type;
	rule_priority_t priority;
	char *trigger;
	char *condition;
	char *action;
	float confidence;
	gboolean enabled;
	time_t last_triggered;
	int trigger_count;
	int success_count;
} pta_rule_t;

/*
 * Suggestion structure
 */
typedef struct {
	char *id;
	suggestion_type_t type;
	char *title;
	char *description;
	char *action_command;
	float confidence;
	int credit_cost;
	time_t timestamp;
	gboolean executed;
} pta_suggestion_t;

/*
 * Attention credit system
 */
typedef struct {
	int base_credits;
	int earned_credits;
	int spent_credits;
	time_t last_recovery;
	float recovery_rate;
	pthread_mutex_t mutex;
} attention_credit_t;

/*
 * Event queue for processing
 */
typedef struct {
	ukg_event_t *events;
	int head;
	int tail;
	int size;
	int capacity;
	pthread_mutex_t mutex;
	pthread_cond_t not_empty;
} event_queue_t;

/*
 * PTA Engine main structure
 */
typedef struct {
	/* Core components */
	context_model_t *context_model;
	GList *rules;
	attention_credit_t *attention_credits;
	event_queue_t *event_queue;
	
	/* Database connections */
	sqlite3 *ukg_db;
	
	/* D-Bus connections */
	GDBusConnection *session_bus;
	GDBusConnection *action_bus;
	
	/* Threading */
	pthread_t event_processor_thread;
	pthread_t suggestion_generator_thread;
	gboolean running;
	
	/* Configuration */
	char *config_file;
	int proactivity_level;
	gboolean automation_enabled;
	gboolean learning_enabled;
	
	/* Statistics */
	int total_events_processed;
	int total_suggestions_generated;
	int total_automations_executed;
	time_t start_time;
} pta_engine_t;

/* Function declarations */

/*
 * Core engine functions
 */
pta_engine_t *pta_engine_init(const char *config_file);
void pta_engine_cleanup(pta_engine_t *engine);
int pta_engine_start(pta_engine_t *engine);
void pta_engine_stop(pta_engine_t *engine);

/*
 * Event processing functions
 */
int pta_process_event(pta_engine_t *engine, ukg_event_t *event);
int pta_queue_event(pta_engine_t *engine, ukg_event_t *event);
void *pta_event_processor_thread(void *arg);
int pta_parse_ukg_event(const char *event_str, ukg_event_t *event);
void pta_free_event(ukg_event_t *event);

/*
 * Context modeling functions
 */
context_model_t *pta_context_model_init(void);
void pta_context_model_cleanup(context_model_t *model);
int pta_update_context(pta_engine_t *engine, ukg_event_t *event);
int pta_analyze_patterns(pta_engine_t *engine);

/*
 * Rule engine functions
 */
int pta_load_rules(pta_engine_t *engine, const char *rules_file);
int pta_evaluate_rules(pta_engine_t *engine, ukg_event_t *event);
int pta_execute_rule(pta_engine_t *engine, pta_rule_t *rule, ukg_event_t *event);
int pta_rule_matches(pta_rule_t *rule, ukg_event_t *event);
void pta_free_rule(pta_rule_t *rule);

/*
 * Suggestion generation functions
 */
void *pta_suggestion_generator_thread(void *arg);
int pta_generate_suggestions(pta_engine_t *engine);
pta_suggestion_t *pta_create_suggestion(suggestion_type_t type, 
	const char *title, const char *description, const char *action);
int pta_execute_suggestion(pta_engine_t *engine, pta_suggestion_t *suggestion);
void pta_free_suggestion(pta_suggestion_t *suggestion);

/*
 * Attention credit functions
 */
attention_credit_t *pta_attention_credits_init(void);
void pta_attention_credits_cleanup(attention_credit_t *credits);
int pta_spend_credits(attention_credit_t *credits, int amount);
int pta_earn_credits(attention_credit_t *credits, int amount);
int pta_get_available_credits(attention_credit_t *credits);
void pta_update_credits(attention_credit_t *credits);

/*
 * UKG database functions
 */
int pta_ukg_connect(pta_engine_t *engine, const char *db_path);
void pta_ukg_disconnect(pta_engine_t *engine);
int pta_ukg_store_event(pta_engine_t *engine, ukg_event_t *event);
int pta_ukg_query_patterns(pta_engine_t *engine, const char *app_name, 
	GList **patterns);

/*
 * AT-SPI Action Layer integration
 */
int pta_action_layer_connect(pta_engine_t *engine);
void pta_action_layer_disconnect(pta_engine_t *engine);
int pta_execute_automation(pta_engine_t *engine, const char *app_pid, 
	const char *action, const char *params);

/*
 * D-Bus interface functions
 */
int pta_dbus_init(pta_engine_t *engine);
void pta_dbus_cleanup(pta_engine_t *engine);
int pta_dbus_register_methods(pta_engine_t *engine);

/*
 * Configuration functions
 */
int pta_load_config(pta_engine_t *engine, const char *config_file);
int pta_save_config(pta_engine_t *engine, const char *config_file);
int pta_get_config_value(pta_engine_t *engine, const char *key, char *value, 
	size_t value_size);

/*
 * Utility functions
 */
char *pta_event_type_to_string(event_type_t type);
event_type_t pta_string_to_event_type(const char *type_str);
char *pta_suggestion_type_to_string(suggestion_type_t type);
suggestion_type_t pta_string_to_suggestion_type(const char *type_str);
time_t pta_get_current_time(void);
void pta_log_message(const char *level, const char *format, ...);

/*
 * Machine learning model functions - Real implementation
 */
int pta_ml_predict_next_action(pta_engine_t *engine, ukg_event_t *event, 
	char **predicted_action);
int pta_ml_train_model(pta_engine_t *engine, ukg_event_t *event, 
	const char *outcome);
int pta_ml_get_confidence(pta_engine_t *engine, const char *prediction);
int pta_ml_init_models(pta_engine_t *engine);
void pta_ml_cleanup_models(pta_engine_t *engine);
int pta_ml_update_user_patterns(pta_engine_t *engine, ukg_event_t *event);
int pta_ml_analyze_workflow_efficiency(pta_engine_t *engine, const char *workflow_id);
int pta_ml_suggest_optimizations(pta_engine_t *engine, const char *app_name);
int pta_ml_predict_resource_needs(pta_engine_t *engine, const char *app_name);
int pta_ml_learn_from_feedback(pta_engine_t *engine, const char *suggestion_id, 
	int feedback_score);
int pta_ml_generate_personalized_suggestions(pta_engine_t *engine, 
	ukg_event_t *event, GList **suggestions);
int pta_ml_adapt_to_user_preferences(pta_engine_t *engine, 
	const char *user_id, const char *preference_data);
int pta_ml_forecast_usage_patterns(pta_engine_t *engine, const char *app_name, 
	int forecast_hours, double **predictions);
int pta_ml_detect_anomalies(pta_engine_t *engine, ukg_event_t *event);
int pta_ml_optimize_attention_allocation(pta_engine_t *engine, 
	attention_credit_t *credits);
int pta_ml_continuous_learning_update(pta_engine_t *engine, 
	const char *model_type, const char *training_data);
int pta_ml_get_model_performance(pta_engine_t *engine, const char *model_type, 
	double *accuracy, double *precision, double *recall);
int pta_ml_export_user_model(pta_engine_t *engine, const char *user_id, 
	const char *export_path);
int pta_ml_import_user_model(pta_engine_t *engine, const char *user_id, 
	const char *import_path);

#endif /* PTA_ENGINE_H */

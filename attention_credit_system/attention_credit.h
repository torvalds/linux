/*
 * attention_credit.h - HER OS Attention Credit Economy Header
 *
 * Defines the core structures, functions, and constants for the attention
 * credit economy that balances proactive automation with user autonomy.
 * Follows Linux kernel coding style (K&R, tabs, block comments).
 *
 * Author: HER OS Project
 */

#ifndef ATTENTION_CREDIT_H
#define ATTENTION_CREDIT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sqlite3.h>
#include <json-c/json.h>
#include <glib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <uuid/uuid.h>
#include <math.h>

/* Configuration constants */
#define ATTENTION_CREDIT_CONFIG_FILE "/etc/heros/attention_credit_config.json"
#define ATTENTION_CREDIT_LOG_FILE "/var/log/attention_credit_system.log"
#define ATTENTION_CREDIT_SOCKET_PATH "/tmp/heros_attention_credit.sock"

/* Credit types for different actions */
typedef enum {
    CREDIT_TYPE_NOTIFICATION = 0,    /* System notifications */
    CREDIT_TYPE_AUTOMATION,          /* Automated actions */
    CREDIT_TYPE_SUGGESTION,          /* Proactive suggestions */
    CREDIT_TYPE_OPTIMIZATION,        /* Background optimizations */
    CREDIT_TYPE_LEARNING,            /* Model training and updates */
    CREDIT_TYPE_INTEGRATION,         /* Cross-component coordination */
    CREDIT_TYPE_MAX
} credit_type_t;

/* Credit allocation strategy */
typedef enum {
    ALLOCATION_STRATEGY_EQUAL = 0,   /* Equal distribution */
    ALLOCATION_STRATEGY_PRIORITY,    /* Priority-based allocation */
    ALLOCATION_STRATEGY_ADAPTIVE,    /* Adaptive based on behavior */
    ALLOCATION_STRATEGY_PREDICTIVE,  /* Predictive allocation */
    ALLOCATION_STRATEGY_MAX
} allocation_strategy_t;

/* Credit spending policy */
typedef enum {
    SPENDING_POLICY_CONSERVATIVE = 0, /* Conservative spending */
    SPENDING_POLICY_BALANCED,         /* Balanced approach */
    SPENDING_POLICY_AGGRESSIVE,       /* Aggressive automation */
    SPENDING_POLICY_ADAPTIVE,         /* Adaptive based on feedback */
    SPENDING_POLICY_MAX
} spending_policy_t;

/* System state */
typedef enum {
    ATTENTION_CREDIT_STATE_UNINITIALIZED = 0,
    ATTENTION_CREDIT_STATE_INITIALIZING,
    ATTENTION_CREDIT_STATE_READY,
    ATTENTION_CREDIT_STATE_ALLOCATING,
    ATTENTION_CREDIT_STATE_SPENDING,
    ATTENTION_CREDIT_STATE_RECOVERING,
    ATTENTION_CREDIT_STATE_ERROR,
    ATTENTION_CREDIT_STATE_SHUTDOWN
} attention_credit_state_t;

/*
 * Credit allocation structure
 */
typedef struct {
    char *allocation_id;             /* Unique allocation identifier */
    credit_type_t credit_type;       /* Type of credit being allocated */
    double amount;                   /* Amount of credits allocated */
    double priority;                 /* Priority level (0.0-1.0) */
    time_t timestamp;                /* Allocation timestamp */
    time_t expiry;                   /* Credit expiry time */
    char *context;                   /* Context for allocation */
    allocation_strategy_t strategy;  /* Allocation strategy used */
} credit_allocation_t;

/*
 * Credit spending structure
 */
typedef struct {
    char *spending_id;               /* Unique spending identifier */
    credit_type_t credit_type;       /* Type of credit being spent */
    double amount;                   /* Amount of credits spent */
    char *action;                    /* Action being performed */
    char *reason;                    /* Reason for spending */
    time_t timestamp;                /* Spending timestamp */
    double user_satisfaction;        /* User satisfaction score */
    spending_policy_t policy;        /* Spending policy used */
} credit_spending_t;

/*
 * Credit recovery structure
 */
typedef struct {
    char *recovery_id;               /* Unique recovery identifier */
    credit_type_t credit_type;       /* Type of credit being recovered */
    double amount;                   /* Amount of credits recovered */
    double rate;                     /* Recovery rate */
    time_t timestamp;                /* Recovery timestamp */
    char *trigger;                   /* Recovery trigger */
    double user_satisfaction;        /* User satisfaction influence */
} credit_recovery_t;

/*
 * User behavior pattern structure
 */
typedef struct {
    char *pattern_id;                /* Unique pattern identifier */
    char *user_id;                   /* User identifier */
    char *behavior_type;             /* Type of behavior */
    json_object *pattern_data;       /* Pattern data */
    double frequency;                /* Behavior frequency */
    double importance;               /* Behavior importance */
    time_t first_observed;           /* First observation time */
    time_t last_observed;            /* Last observation time */
    int observation_count;           /* Number of observations */
} user_behavior_pattern_t;

/*
 * User feedback structure
 */
typedef struct {
    char *feedback_id;               /* Unique feedback identifier */
    char *user_id;                   /* User identifier */
    char *action_id;                 /* Action that received feedback */
    double satisfaction_score;       /* Satisfaction score (0.0-1.0) */
    char *feedback_type;             /* Type of feedback */
    char *feedback_text;             /* Feedback text */
    time_t timestamp;                /* Feedback timestamp */
    json_object *context;            /* Context when feedback was given */
} user_feedback_t;

/*
 * Credit balance structure
 */
typedef struct {
    char *user_id;                   /* User identifier */
    double total_balance;            /* Total credit balance */
    double available_balance;        /* Available credits for spending */
    double allocated_balance;        /* Credits currently allocated */
    double spent_balance;            /* Credits spent in current period */
    double recovery_rate;            /* Current recovery rate */
    time_t last_update;              /* Last balance update */
    json_object *credit_breakdown;   /* Breakdown by credit type */
} credit_balance_t;

/*
 * Economic metrics structure
 */
typedef struct {
    int total_allocations;           /* Total number of allocations */
    int total_spendings;             /* Total number of spendings */
    int total_recoveries;            /* Total number of recoveries */
    double total_credits_allocated;  /* Total credits allocated */
    double total_credits_spent;      /* Total credits spent */
    double total_credits_recovered;  /* Total credits recovered */
    double avg_user_satisfaction;    /* Average user satisfaction */
    double avg_allocation_time;      /* Average allocation time */
    double avg_spending_time;        /* Average spending time */
    double avg_recovery_time;        /* Average recovery time */
    time_t start_time;               /* System start time */
} economic_metrics_t;

/*
 * Attention credit configuration
 */
typedef struct {
    gboolean enable_credit_system;
    gboolean enable_behavioral_analysis;
    gboolean enable_feedback_integration;
    gboolean enable_economic_balancing;
    double base_credit_pool;
    double credit_regeneration_rate;
    double max_credit_balance;
    double min_credit_threshold;
    char *log_file;
    char *config_file;
} attention_credit_config_t;

/*
 * Global attention credit system state
 */
typedef struct {
    attention_credit_state_t state;
    attention_credit_config_t config;
    
    /* Credit management */
    GHashTable *credit_balances;     /* user_id -> credit_balance */
    GHashTable *credit_allocations;  /* allocation_id -> credit_allocation */
    GHashTable *credit_spendings;    /* spending_id -> credit_spending */
    GHashTable *credit_recoveries;   /* recovery_id -> credit_recovery */
    
    /* User analysis */
    GHashTable *behavior_patterns;   /* pattern_id -> user_behavior_pattern */
    GHashTable *user_feedback;       /* feedback_id -> user_feedback */
    
    /* Performance tracking */
    economic_metrics_t metrics;
    
    /* Threading */
    pthread_mutex_t system_mutex;
    pthread_mutex_t credit_mutex;
    pthread_mutex_t behavior_mutex;
    pthread_mutex_t metrics_mutex;
    pthread_t allocation_thread;
    pthread_t recovery_thread;
    pthread_t analysis_thread;
    gboolean threads_running;
    
    /* Database connection */
    sqlite3 *attention_credit_db;
} attention_credit_system_t;

/* Function declarations */

/*
 * Core system functions
 */
int attention_credit_system_init(const char *config_file);
void attention_credit_system_cleanup(void);
attention_credit_system_t *get_attention_credit_system(void);
int attention_credit_system_start(void);
int attention_credit_system_stop(void);

/*
 * Credit allocation functions
 */
int allocate_credits(const char *user_id, credit_type_t type, double amount, 
    double priority, const char *context, credit_allocation_t **allocation);
int get_available_credits(const char *user_id, credit_type_t type, double *available);
int check_credit_sufficiency(const char *user_id, credit_type_t type, double amount);
int expire_allocations(const char *user_id);
int get_allocation_strategy(allocation_strategy_t strategy, double *multiplier);

/*
 * Credit spending functions
 */
int spend_credits(const char *user_id, credit_type_t type, double amount, 
    const char *action, const char *reason, spending_policy_t policy, 
    credit_spending_t **spending);
int can_spend_credits(const char *user_id, credit_type_t type, double amount);
int calculate_spending_cost(credit_type_t type, double base_amount, 
    spending_policy_t policy, double *cost);
int apply_spending_policy(spending_policy_t policy, double *threshold, 
    double *satisfaction_threshold, double *recovery_bonus);

/*
 * Credit recovery functions
 */
int recover_credits(const char *user_id, credit_type_t type, double amount, 
    const char *trigger, double user_satisfaction, credit_recovery_t **recovery);
int calculate_recovery_rate(const char *user_id, double base_rate, 
    double user_satisfaction, double *rate);
int apply_satisfaction_bonus(double user_satisfaction, double base_amount, 
    double *bonus_amount);
int process_time_based_recovery(void);

/*
 * User behavior analysis functions
 */
int analyze_user_behavior(const char *user_id, const char *behavior_type, 
    json_object *behavior_data);
int detect_behavior_pattern(const char *user_id, const char *behavior_type, 
    user_behavior_pattern_t **pattern);
int update_behavior_pattern(user_behavior_pattern_t *pattern, 
    json_object *new_data);
int calculate_behavior_importance(const char *behavior_type, double frequency, 
    double *importance);

/*
 * Feedback integration functions
 */
int process_user_feedback(const char *user_id, const char *action_id, 
    double satisfaction_score, const char *feedback_type, const char *feedback_text, 
    json_object *context, user_feedback_t **feedback);
int calculate_feedback_weight(const char *feedback_type, double *weight);
int apply_feedback_decay(user_feedback_t *feedback, double decay_rate);
int aggregate_user_satisfaction(const char *user_id, double *avg_satisfaction);

/*
 * Economic balancing functions
 */
int balance_economy(const char *user_id);
int calculate_equilibrium(const char *user_id, double *allocation_rate, 
    double *spending_rate, double *recovery_rate);
int optimize_credit_distribution(const char *user_id);
int maintain_economic_balance(void);

/*
 * Credit balance functions
 */
int get_credit_balance(const char *user_id, credit_balance_t **balance);
int update_credit_balance(const char *user_id, double allocation_delta, 
    double spending_delta, double recovery_delta);
int calculate_credit_breakdown(const char *user_id, json_object **breakdown);
int validate_credit_balance(credit_balance_t *balance);

/*
 * Performance monitoring functions
 */
int update_economic_metrics(const char *metric_type, double value);
int get_economic_metrics(economic_metrics_t *metrics);
int reset_economic_metrics(void);
int export_economic_report(const char *format, char **report);

/*
 * Configuration functions
 */
int load_attention_credit_config(attention_credit_config_t *config, const char *config_file);
int save_attention_credit_config(attention_credit_config_t *config, const char *config_file);
void free_attention_credit_config(attention_credit_config_t *config);

/*
 * Utility functions
 */
char *generate_allocation_id(void);
char *generate_spending_id(void);
char *generate_recovery_id(void);
char *generate_pattern_id(void);
char *generate_feedback_id(void);
char *credit_type_to_string(credit_type_t type);
credit_type_t string_to_credit_type(const char *type_str);
char *allocation_strategy_to_string(allocation_strategy_t strategy);
allocation_strategy_t string_to_allocation_strategy(const char *strategy_str);
char *spending_policy_to_string(spending_policy_t policy);
spending_policy_t string_to_spending_policy(const char *policy_str);
void attention_credit_log_message(const char *level, const char *format, ...);
int validate_allocation(credit_allocation_t *allocation);
int validate_spending(credit_spending_t *spending);
int validate_recovery(credit_recovery_t *recovery);
int validate_behavior_pattern(user_behavior_pattern_t *pattern);
int validate_user_feedback(user_feedback_t *feedback);

/*
 * Threading functions
 */
void *allocation_thread(void *arg);
void *recovery_thread(void *arg);
void *analysis_thread(void *arg);
int start_attention_credit_threads(void);
int stop_attention_credit_threads(void);

/*
 * Database functions
 */
int init_attention_credit_database(const char *db_path);
void cleanup_attention_credit_database(void);
int store_allocation_in_database(const char *allocation_id, credit_allocation_t *allocation);
int load_allocation_from_database(const char *allocation_id, credit_allocation_t **allocation);
int store_spending_in_database(const char *spending_id, credit_spending_t *spending);
int load_spending_from_database(const char *spending_id, credit_spending_t **spending);
int store_recovery_in_database(const char *recovery_id, credit_recovery_t *recovery);
int load_recovery_from_database(const char *recovery_id, credit_recovery_t **recovery);
int store_behavior_pattern_in_database(const char *pattern_id, user_behavior_pattern_t *pattern);
int load_behavior_pattern_from_database(const char *pattern_id, user_behavior_pattern_t **pattern);
int store_user_feedback_in_database(const char *feedback_id, user_feedback_t *feedback);
int load_user_feedback_from_database(const char *feedback_id, user_feedback_t **feedback);
int query_economic_metrics(const char *query, GList **results);

/*
 * Mathematical functions
 */
double calculate_priority_multiplier(double priority);
double calculate_time_decay(time_t timestamp, double decay_rate);
double calculate_satisfaction_bonus(double satisfaction_score);
double calculate_behavior_frequency(time_t first_observed, time_t last_observed, int count);
double calculate_credit_efficiency(double spent, double satisfaction);
double calculate_equilibrium_point(double allocation_rate, double spending_rate, double recovery_rate);
double apply_adaptive_multiplier(double base_value, double user_satisfaction, double learning_rate);

/*
 * Error handling functions
 */
void set_attention_credit_error(int error_code, const char *error_message);
int get_attention_credit_error(void);
const char *get_attention_credit_error_message(void);

/* Global system instance */
extern attention_credit_system_t *g_attention_credit_system;

/* Error handling */
extern int g_attention_credit_error_code;
extern char g_attention_credit_error_message[256];

#endif /* ATTENTION_CREDIT_H */

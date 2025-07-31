/*
 * attention_credit.c - HER OS Attention Credit Economy Implementation
 *
 * Core implementation of the attention credit economy that balances
 * proactive automation with user autonomy. Implements credit allocation,
 * spending, recovery, user behavior analysis, and feedback integration.
 * Follows Linux kernel coding style (K&R, tabs, block comments).
 *
 * Author: HER OS Project
 */

#include "attention_credit.h"
#include <stdarg.h>
#include <sys/wait.h>
#include <openssl/sha.h>

/* Global system instance */
attention_credit_system_t *g_attention_credit_system = NULL;

/* Error handling */
int g_attention_credit_error_code = 0;
char g_attention_credit_error_message[256] = {0};

/* Logging level */
static int attention_credit_log_level = 1;

/*
 * Logging function with timestamp and level
 */
void attention_credit_log_message(const char *level, const char *format, ...)
{
	time_t now = time(NULL);
	char time_str[64];
	va_list args;
	
	strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
	
	printf("[%s] [Attention Credit] [%s] ", time_str, level);
	
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	
	printf("\n");
	fflush(stdout);
}

/*
 * Initialize attention credit system
 */
int attention_credit_system_init(const char *config_file)
{
	attention_credit_system_t *system;
	
	attention_credit_log_message("INFO", "Initializing HER OS Attention Credit Economy");
	
	system = malloc(sizeof(attention_credit_system_t));
	if (!system) {
		attention_credit_log_message("ERROR", "Failed to allocate system state");
		return -1;
	}
	
	/* Initialize all fields to zero/NULL */
	memset(system, 0, sizeof(attention_credit_system_t));
	
	/* Set initial state */
	system->state = ATTENTION_CREDIT_STATE_INITIALIZING;
	
	/* Load configuration */
	if (load_attention_credit_config(&system->config, config_file ? config_file : ATTENTION_CREDIT_CONFIG_FILE) != 0) {
		attention_credit_log_message("WARN", "Failed to load configuration, using defaults");
		system->config.enable_credit_system = TRUE;
		system->config.enable_behavioral_analysis = TRUE;
		system->config.enable_feedback_integration = TRUE;
		system->config.enable_economic_balancing = TRUE;
		system->config.base_credit_pool = 100.0;
		system->config.credit_regeneration_rate = 1.0;
		system->config.max_credit_balance = 200.0;
		system->config.min_credit_threshold = 10.0;
		system->config.log_file = ATTENTION_CREDIT_LOG_FILE;
		system->config.config_file = config_file ? strdup(config_file) : strdup(ATTENTION_CREDIT_CONFIG_FILE);
	}
	
	/* Initialize mutexes */
	pthread_mutex_init(&system->system_mutex, NULL);
	pthread_mutex_init(&system->credit_mutex, NULL);
	pthread_mutex_init(&system->behavior_mutex, NULL);
	pthread_mutex_init(&system->metrics_mutex, NULL);
	
	/* Initialize credit management */
	system->credit_balances = g_hash_table_new(g_str_hash, g_str_equal);
	system->credit_allocations = g_hash_table_new(g_str_hash, g_str_equal);
	system->credit_spendings = g_hash_table_new(g_str_hash, g_str_equal);
	system->credit_recoveries = g_hash_table_new(g_str_hash, g_str_equal);
	
	if (!system->credit_balances || !system->credit_allocations || 
	    !system->credit_spendings || !system->credit_recoveries) {
		attention_credit_log_message("ERROR", "Failed to initialize credit management");
		goto cleanup_system;
	}
	
	/* Initialize user analysis */
	system->behavior_patterns = g_hash_table_new(g_str_hash, g_str_equal);
	system->user_feedback = g_hash_table_new(g_str_hash, g_str_equal);
	
	if (!system->behavior_patterns || !system->user_feedback) {
		attention_credit_log_message("ERROR", "Failed to initialize user analysis");
		goto cleanup_system;
	}
	
	/* Initialize performance metrics */
	system->metrics.start_time = time(NULL);
	system->metrics.total_allocations = 0;
	system->metrics.total_spendings = 0;
	system->metrics.total_recoveries = 0;
	system->metrics.total_credits_allocated = 0.0;
	system->metrics.total_credits_spent = 0.0;
	system->metrics.total_credits_recovered = 0.0;
	system->metrics.avg_user_satisfaction = 0.0;
	system->metrics.avg_allocation_time = 0.0;
	system->metrics.avg_spending_time = 0.0;
	system->metrics.avg_recovery_time = 0.0;
	
	/* Initialize database */
	if (init_attention_credit_database("/tmp/heros_attention_credit.db") != 0) {
		attention_credit_log_message("WARN", "Failed to initialize attention credit database");
	}
	
	system->state = ATTENTION_CREDIT_STATE_READY;
	g_attention_credit_system = system;
	
	attention_credit_log_message("INFO", "HER OS Attention Credit Economy initialized successfully");
	return 0;

cleanup_system:
	if (system->credit_balances) {
		g_hash_table_destroy(system->credit_balances);
	}
	if (system->credit_allocations) {
		g_hash_table_destroy(system->credit_allocations);
	}
	if (system->credit_spendings) {
		g_hash_table_destroy(system->credit_spendings);
	}
	if (system->credit_recoveries) {
		g_hash_table_destroy(system->credit_recoveries);
	}
	if (system->behavior_patterns) {
		g_hash_table_destroy(system->behavior_patterns);
	}
	if (system->user_feedback) {
		g_hash_table_destroy(system->user_feedback);
	}
	
	pthread_mutex_destroy(&system->system_mutex);
	pthread_mutex_destroy(&system->credit_mutex);
	pthread_mutex_destroy(&system->behavior_mutex);
	pthread_mutex_destroy(&system->metrics_mutex);
	
	free(system);
	return -1;
}

/*
 * Cleanup attention credit system
 */
void attention_credit_system_cleanup(void)
{
	attention_credit_system_t *system = g_attention_credit_system;
	
	if (!system) {
		return;
	}
	
	attention_credit_log_message("INFO", "Cleaning up HER OS Attention Credit Economy");
	
	/* Stop all threads */
	stop_attention_credit_threads();
	
	/* Cleanup credit management */
	g_hash_table_destroy(system->credit_balances);
	g_hash_table_destroy(system->credit_allocations);
	g_hash_table_destroy(system->credit_spendings);
	g_hash_table_destroy(system->credit_recoveries);
	
	/* Cleanup user analysis */
	g_hash_table_destroy(system->behavior_patterns);
	g_hash_table_destroy(system->user_feedback);
	
	/* Cleanup mutexes */
	pthread_mutex_destroy(&system->system_mutex);
	pthread_mutex_destroy(&system->credit_mutex);
	pthread_mutex_destroy(&system->behavior_mutex);
	pthread_mutex_destroy(&system->metrics_mutex);
	
	/* Cleanup configuration */
	free_attention_credit_config(&system->config);
	
	/* Cleanup database */
	cleanup_attention_credit_database();
	
	free(system);
	g_attention_credit_system = NULL;
	
	attention_credit_log_message("INFO", "HER OS Attention Credit Economy cleanup completed");
}

/*
 * Get attention credit system instance
 */
attention_credit_system_t *get_attention_credit_system(void)
{
	return g_attention_credit_system;
}

/*
 * Start attention credit system
 */
int attention_credit_system_start(void)
{
	attention_credit_system_t *system = get_attention_credit_system();
	
	if (!system) {
		return -1;
	}
	
	attention_credit_log_message("INFO", "Starting HER OS Attention Credit Economy");
	
	system->state = ATTENTION_CREDIT_STATE_READY;
	system->threads_running = TRUE;
	
	/* Start all background threads */
	if (start_attention_credit_threads() != 0) {
		attention_credit_log_message("ERROR", "Failed to start attention credit threads");
		system->state = ATTENTION_CREDIT_STATE_ERROR;
		return -1;
	}
	
	attention_credit_log_message("INFO", "HER OS Attention Credit Economy started successfully");
	return 0;
}

/*
 * Stop attention credit system
 */
int attention_credit_system_stop(void)
{
	attention_credit_system_t *system = get_attention_credit_system();
	
	if (!system) {
		return -1;
	}
	
	attention_credit_log_message("INFO", "Stopping HER OS Attention Credit Economy");
	
	system->threads_running = FALSE;
	system->state = ATTENTION_CREDIT_STATE_SHUTDOWN;
	
	/* Stop all background threads */
	stop_attention_credit_threads();
	
	attention_credit_log_message("INFO", "HER OS Attention Credit Economy stopped successfully");
	return 0;
}

/*
 * Allocate credits to a user
 */
int allocate_credits(const char *user_id, credit_type_t type, double amount, 
    double priority, const char *context, credit_allocation_t **allocation)
{
	attention_credit_system_t *system = get_attention_credit_system();
	credit_allocation_t *new_allocation;
	credit_balance_t *balance;
	double multiplier;
	
	if (!system || !user_id || amount <= 0 || !allocation) {
		return -1;
	}
	
	/* Get or create user balance */
	pthread_mutex_lock(&system->credit_mutex);
	balance = g_hash_table_lookup(system->credit_balances, user_id);
	if (!balance) {
		balance = malloc(sizeof(credit_balance_t));
		if (!balance) {
			pthread_mutex_unlock(&system->credit_mutex);
			return -1;
		}
		
		memset(balance, 0, sizeof(credit_balance_t));
		balance->user_id = strdup(user_id);
		balance->total_balance = system->config.base_credit_pool;
		balance->available_balance = system->config.base_credit_pool;
		balance->allocated_balance = 0.0;
		balance->spent_balance = 0.0;
		balance->recovery_rate = system->config.credit_regeneration_rate;
		balance->last_update = time(NULL);
		balance->credit_breakdown = json_object_new_object();
		
		g_hash_table_insert(system->credit_balances, balance->user_id, balance);
	}
	pthread_mutex_unlock(&system->credit_mutex);
	
	/* Calculate allocation amount with priority multiplier */
	multiplier = calculate_priority_multiplier(priority);
	double adjusted_amount = amount * multiplier;
	
	/* Check if user has enough available credits */
	if (balance->available_balance < adjusted_amount) {
		attention_credit_log_message("WARN", "Insufficient credits for user %s: requested %.2f, available %.2f", 
		    user_id, adjusted_amount, balance->available_balance);
		return -1;
	}
	
	/* Create allocation */
	new_allocation = malloc(sizeof(credit_allocation_t));
	if (!new_allocation) {
		return -1;
	}
	
	memset(new_allocation, 0, sizeof(credit_allocation_t));
	new_allocation->allocation_id = generate_allocation_id();
	new_allocation->credit_type = type;
	new_allocation->amount = adjusted_amount;
	new_allocation->priority = priority;
	new_allocation->timestamp = time(NULL);
	new_allocation->expiry = new_allocation->timestamp + 3600; /* 1 hour expiry */
	new_allocation->context = context ? strdup(context) : NULL;
	new_allocation->strategy = ALLOCATION_STRATEGY_PRIORITY;
	
	/* Update user balance */
	pthread_mutex_lock(&system->credit_mutex);
	balance->available_balance -= adjusted_amount;
	balance->allocated_balance += adjusted_amount;
	balance->last_update = time(NULL);
	
	/* Update credit breakdown */
	char type_str[32];
	snprintf(type_str, sizeof(type_str), "%s", credit_type_to_string(type));
	json_object *type_balance = json_object_object_get(balance->credit_breakdown, type_str);
	if (type_balance) {
		double current = json_object_get_double(type_balance);
		json_object_object_add(balance->credit_breakdown, type_str, json_object_new_double(current + adjusted_amount));
	} else {
		json_object_object_add(balance->credit_breakdown, type_str, json_object_new_double(adjusted_amount));
	}
	pthread_mutex_unlock(&system->credit_mutex);
	
	/* Store allocation */
	pthread_mutex_lock(&system->credit_mutex);
	g_hash_table_insert(system->credit_allocations, new_allocation->allocation_id, new_allocation);
	pthread_mutex_unlock(&system->credit_mutex);
	
	/* Store in database */
	store_allocation_in_database(new_allocation->allocation_id, new_allocation);
	
	/* Update metrics */
	pthread_mutex_lock(&system->metrics_mutex);
	system->metrics.total_allocations++;
	system->metrics.total_credits_allocated += adjusted_amount;
	pthread_mutex_unlock(&system->metrics_mutex);
	
	*allocation = new_allocation;
	
	attention_credit_log_message("DEBUG", "Allocated %.2f credits to user %s for %s", 
	    adjusted_amount, user_id, credit_type_to_string(type));
	return 0;
}

/*
 * Spend credits for an action
 */
int spend_credits(const char *user_id, credit_type_t type, double amount, 
    const char *action, const char *reason, spending_policy_t policy, 
    credit_spending_t **spending)
{
	attention_credit_system_t *system = get_attention_credit_system();
	credit_spending_t *new_spending;
	credit_balance_t *balance;
	double cost;
	
	if (!system || !user_id || amount <= 0 || !action || !spending) {
		return -1;
	}
	
	/* Calculate actual cost based on policy */
	if (calculate_spending_cost(type, amount, policy, &cost) != 0) {
		return -1;
	}
	
	/* Check if user can spend the credits */
	if (!can_spend_credits(user_id, type, cost)) {
		attention_credit_log_message("WARN", "Insufficient credits for spending: user %s, type %s, cost %.2f", 
		    user_id, credit_type_to_string(type), cost);
		return -1;
	}
	
	/* Get user balance */
	pthread_mutex_lock(&system->credit_mutex);
	balance = g_hash_table_lookup(system->credit_balances, user_id);
	if (!balance) {
		pthread_mutex_unlock(&system->credit_mutex);
		return -1;
	}
	
	/* Update balance */
	balance->allocated_balance -= cost;
	balance->spent_balance += cost;
	balance->last_update = time(NULL);
	pthread_mutex_unlock(&system->credit_mutex);
	
	/* Create spending record */
	new_spending = malloc(sizeof(credit_spending_t));
	if (!new_spending) {
		return -1;
	}
	
	memset(new_spending, 0, sizeof(credit_spending_t));
	new_spending->spending_id = generate_spending_id();
	new_spending->credit_type = type;
	new_spending->amount = cost;
	new_spending->action = strdup(action);
	new_spending->reason = reason ? strdup(reason) : NULL;
	new_spending->timestamp = time(NULL);
	new_spending->user_satisfaction = 0.5; /* Default neutral satisfaction */
	new_spending->policy = policy;
	
	/* Store spending */
	pthread_mutex_lock(&system->credit_mutex);
	g_hash_table_insert(system->credit_spendings, new_spending->spending_id, new_spending);
	pthread_mutex_unlock(&system->credit_mutex);
	
	/* Store in database */
	store_spending_in_database(new_spending->spending_id, new_spending);
	
	/* Update metrics */
	pthread_mutex_lock(&system->metrics_mutex);
	system->metrics.total_spendings++;
	system->metrics.total_credits_spent += cost;
	pthread_mutex_unlock(&system->metrics_mutex);
	
	*spending = new_spending;
	
	attention_credit_log_message("DEBUG", "Spent %.2f credits for user %s on action %s", 
	    cost, user_id, action);
	return 0;
}

/*
 * Recover credits based on user satisfaction
 */
int recover_credits(const char *user_id, credit_type_t type, double amount, 
    const char *trigger, double user_satisfaction, credit_recovery_t **recovery)
{
	attention_credit_system_t *system = get_attention_credit_system();
	credit_recovery_t *new_recovery;
	credit_balance_t *balance;
	double recovery_rate, bonus_amount;
	
	if (!system || !user_id || amount <= 0 || !trigger || !recovery) {
		return -1;
	}
	
	/* Calculate recovery rate and bonus */
	if (calculate_recovery_rate(user_id, system->config.credit_regeneration_rate, 
	    user_satisfaction, &recovery_rate) != 0) {
		return -1;
	}
	
	if (apply_satisfaction_bonus(user_satisfaction, amount, &bonus_amount) != 0) {
		return -1;
	}
	
	double total_recovery = amount + bonus_amount;
	
	/* Get user balance */
	pthread_mutex_lock(&system->credit_mutex);
	balance = g_hash_table_lookup(system->credit_balances, user_id);
	if (!balance) {
		pthread_mutex_unlock(&system->credit_mutex);
		return -1;
	}
	
	/* Update balance */
	balance->total_balance += total_recovery;
	balance->available_balance += total_recovery;
	balance->recovery_rate = recovery_rate;
	balance->last_update = time(NULL);
	
	/* Cap at maximum balance */
	if (balance->total_balance > system->config.max_credit_balance) {
		balance->total_balance = system->config.max_credit_balance;
		balance->available_balance = system->config.max_credit_balance - balance->allocated_balance;
	}
	pthread_mutex_unlock(&system->credit_mutex);
	
	/* Create recovery record */
	new_recovery = malloc(sizeof(credit_recovery_t));
	if (!new_recovery) {
		return -1;
	}
	
	memset(new_recovery, 0, sizeof(credit_recovery_t));
	new_recovery->recovery_id = generate_recovery_id();
	new_recovery->credit_type = type;
	new_recovery->amount = total_recovery;
	new_recovery->rate = recovery_rate;
	new_recovery->timestamp = time(NULL);
	new_recovery->trigger = strdup(trigger);
	new_recovery->user_satisfaction = user_satisfaction;
	
	/* Store recovery */
	pthread_mutex_lock(&system->credit_mutex);
	g_hash_table_insert(system->credit_recoveries, new_recovery->recovery_id, new_recovery);
	pthread_mutex_unlock(&system->credit_mutex);
	
	/* Store in database */
	store_recovery_in_database(new_recovery->recovery_id, new_recovery);
	
	/* Update metrics */
	pthread_mutex_lock(&system->metrics_mutex);
	system->metrics.total_recoveries++;
	system->metrics.total_credits_recovered += total_recovery;
	pthread_mutex_unlock(&system->metrics_mutex);
	
	*recovery = new_recovery;
	
	attention_credit_log_message("DEBUG", "Recovered %.2f credits for user %s (satisfaction: %.2f)", 
	    total_recovery, user_id, user_satisfaction);
	return 0;
}

/*
 * Analyze user behavior
 */
int analyze_user_behavior(const char *user_id, const char *behavior_type, 
    json_object *behavior_data)
{
	attention_credit_system_t *system = get_attention_credit_system();
	user_behavior_pattern_t *pattern;
	
	if (!system || !user_id || !behavior_type || !behavior_data) {
		return -1;
	}
	
	/* Detect or create behavior pattern */
	if (detect_behavior_pattern(user_id, behavior_type, &pattern) != 0) {
		/* Create new pattern */
		pattern = malloc(sizeof(user_behavior_pattern_t));
		if (!pattern) {
			return -1;
		}
		
		memset(pattern, 0, sizeof(user_behavior_pattern_t));
		pattern->pattern_id = generate_pattern_id();
		pattern->user_id = strdup(user_id);
		pattern->behavior_type = strdup(behavior_type);
		pattern->pattern_data = json_object_get(behavior_data);
		pattern->frequency = 1.0;
		pattern->importance = 0.5;
		pattern->first_observed = time(NULL);
		pattern->last_observed = time(NULL);
		pattern->observation_count = 1;
		
		/* Store pattern */
		pthread_mutex_lock(&system->behavior_mutex);
		g_hash_table_insert(system->behavior_patterns, pattern->pattern_id, pattern);
		pthread_mutex_unlock(&system->behavior_mutex);
		
		/* Store in database */
		store_behavior_pattern_in_database(pattern->pattern_id, pattern);
	} else {
		/* Update existing pattern */
		update_behavior_pattern(pattern, behavior_data);
		pattern->last_observed = time(NULL);
		pattern->observation_count++;
		
		/* Recalculate frequency and importance */
		pattern->frequency = calculate_behavior_frequency(pattern->first_observed, 
		    pattern->last_observed, pattern->observation_count);
		calculate_behavior_importance(behavior_type, pattern->frequency, &pattern->importance);
	}
	
	attention_credit_log_message("DEBUG", "Analyzed behavior for user %s: %s (frequency: %.2f, importance: %.2f)", 
	    user_id, behavior_type, pattern->frequency, pattern->importance);
	return 0;
}

/*
 * Process user feedback
 */
int process_user_feedback(const char *user_id, const char *action_id, 
    double satisfaction_score, const char *feedback_type, const char *feedback_text, 
    json_object *context, user_feedback_t **feedback)
{
	attention_credit_system_t *system = get_attention_credit_system();
	user_feedback_t *new_feedback;
	double weight;
	
	if (!system || !user_id || !action_id || !feedback_type || !feedback) {
		return -1;
	}
	
	/* Validate satisfaction score */
	if (satisfaction_score < 0.0 || satisfaction_score > 1.0) {
		return -1;
	}
	
	/* Calculate feedback weight */
	if (calculate_feedback_weight(feedback_type, &weight) != 0) {
		return -1;
	}
	
	/* Create feedback record */
	new_feedback = malloc(sizeof(user_feedback_t));
	if (!new_feedback) {
		return -1;
	}
	
	memset(new_feedback, 0, sizeof(user_feedback_t));
	new_feedback->feedback_id = generate_feedback_id();
	new_feedback->user_id = strdup(user_id);
	new_feedback->action_id = strdup(action_id);
	new_feedback->satisfaction_score = satisfaction_score;
	new_feedback->feedback_type = strdup(feedback_type);
	new_feedback->feedback_text = feedback_text ? strdup(feedback_text) : NULL;
	new_feedback->timestamp = time(NULL);
	new_feedback->context = context ? json_object_get(context) : NULL;
	
	/* Store feedback */
	pthread_mutex_lock(&system->behavior_mutex);
	g_hash_table_insert(system->user_feedback, new_feedback->feedback_id, new_feedback);
	pthread_mutex_unlock(&system->behavior_mutex);
	
	/* Store in database */
	store_user_feedback_in_database(new_feedback->feedback_id, new_feedback);
	
	/* Update metrics */
	pthread_mutex_lock(&system->metrics_mutex);
	system->metrics.avg_user_satisfaction = (system->metrics.avg_user_satisfaction + satisfaction_score) / 2.0;
	pthread_mutex_unlock(&system->metrics_mutex);
	
	*feedback = new_feedback;
	
	attention_credit_log_message("DEBUG", "Processed feedback for user %s: satisfaction %.2f, type %s", 
	    user_id, satisfaction_score, feedback_type);
	return 0;
}

/*
 * Mathematical functions
 */
double calculate_priority_multiplier(double priority)
{
	/* Priority multiplier: 1.0 for priority 0.5, scales up to 2.0 for priority 1.0 */
	return 1.0 + (priority - 0.5) * 2.0;
}

double calculate_time_decay(time_t timestamp, double decay_rate)
{
	time_t now = time(NULL);
	double elapsed = difftime(now, timestamp);
	return exp(-decay_rate * elapsed / 3600.0); /* Decay per hour */
}

double calculate_satisfaction_bonus(double satisfaction_score)
{
	/* Bonus increases with satisfaction: 0% for 0.5, up to 50% for 1.0 */
	if (satisfaction_score <= 0.5) {
		return 0.0;
	}
	return (satisfaction_score - 0.5) * 1.0; /* Up to 50% bonus */
}

double calculate_behavior_frequency(time_t first_observed, time_t last_observed, int count)
{
	double time_span = difftime(last_observed, first_observed) / 3600.0; /* Hours */
	if (time_span <= 0) {
		return 1.0;
	}
	return (double)count / time_span; /* Observations per hour */
}

double calculate_credit_efficiency(double spent, double satisfaction)
{
	/* Efficiency = satisfaction / spent, normalized */
	if (spent <= 0) {
		return 0.0;
	}
	return satisfaction / spent;
}

double calculate_equilibrium_point(double allocation_rate, double spending_rate, double recovery_rate)
{
	/* Equilibrium when allocation + recovery = spending */
	return allocation_rate + recovery_rate - spending_rate;
}

double apply_adaptive_multiplier(double base_value, double user_satisfaction, double learning_rate)
{
	/* Adaptive multiplier based on user satisfaction */
	double satisfaction_factor = (user_satisfaction - 0.5) * 2.0; /* -1.0 to 1.0 */
	return base_value * (1.0 + satisfaction_factor * learning_rate);
}

/*
 * Utility functions
 */
char *generate_allocation_id(void)
{
	uuid_t uuid;
	char uuid_str[37];
	char *allocation_id;
	time_t now = time(NULL);
	
	uuid_generate(uuid);
	uuid_unparse_lower(uuid, uuid_str);
	
	allocation_id = malloc(64);
	if (!allocation_id) {
		return NULL;
	}
	
	snprintf(allocation_id, 64, "alloc_%s_%ld", uuid_str, now);
	return allocation_id;
}

char *generate_spending_id(void)
{
	uuid_t uuid;
	char uuid_str[37];
	char *spending_id;
	time_t now = time(NULL);
	
	uuid_generate(uuid);
	uuid_unparse_lower(uuid, uuid_str);
	
	spending_id = malloc(64);
	if (!spending_id) {
		return NULL;
	}
	
	snprintf(spending_id, 64, "spend_%s_%ld", uuid_str, now);
	return spending_id;
}

char *generate_recovery_id(void)
{
	uuid_t uuid;
	char uuid_str[37];
	char *recovery_id;
	time_t now = time(NULL);
	
	uuid_generate(uuid);
	uuid_unparse_lower(uuid, uuid_str);
	
	recovery_id = malloc(64);
	if (!recovery_id) {
		return NULL;
	}
	
	snprintf(recovery_id, 64, "recover_%s_%ld", uuid_str, now);
	return recovery_id;
}

char *generate_pattern_id(void)
{
	uuid_t uuid;
	char uuid_str[37];
	char *pattern_id;
	time_t now = time(NULL);
	
	uuid_generate(uuid);
	uuid_unparse_lower(uuid, uuid_str);
	
	pattern_id = malloc(64);
	if (!pattern_id) {
		return NULL;
	}
	
	snprintf(pattern_id, 64, "pattern_%s_%ld", uuid_str, now);
	return pattern_id;
}

char *generate_feedback_id(void)
{
	uuid_t uuid;
	char uuid_str[37];
	char *feedback_id;
	time_t now = time(NULL);
	
	uuid_generate(uuid);
	uuid_unparse_lower(uuid, uuid_str);
	
	feedback_id = malloc(64);
	if (!feedback_id) {
		return NULL;
	}
	
	snprintf(feedback_id, 64, "feedback_%s_%ld", uuid_str, now);
	return feedback_id;
}

char *credit_type_to_string(credit_type_t type)
{
	switch (type) {
	case CREDIT_TYPE_NOTIFICATION: return "notification";
	case CREDIT_TYPE_AUTOMATION: return "automation";
	case CREDIT_TYPE_SUGGESTION: return "suggestion";
	case CREDIT_TYPE_OPTIMIZATION: return "optimization";
	case CREDIT_TYPE_LEARNING: return "learning";
	case CREDIT_TYPE_INTEGRATION: return "integration";
	default: return "unknown";
	}
}

credit_type_t string_to_credit_type(const char *type_str)
{
	if (!type_str) {
		return CREDIT_TYPE_NOTIFICATION;
	}
	
	if (strcmp(type_str, "notification") == 0) return CREDIT_TYPE_NOTIFICATION;
	if (strcmp(type_str, "automation") == 0) return CREDIT_TYPE_AUTOMATION;
	if (strcmp(type_str, "suggestion") == 0) return CREDIT_TYPE_SUGGESTION;
	if (strcmp(type_str, "optimization") == 0) return CREDIT_TYPE_OPTIMIZATION;
	if (strcmp(type_str, "learning") == 0) return CREDIT_TYPE_LEARNING;
	if (strcmp(type_str, "integration") == 0) return CREDIT_TYPE_INTEGRATION;
	
	return CREDIT_TYPE_NOTIFICATION;
}

char *allocation_strategy_to_string(allocation_strategy_t strategy)
{
	switch (strategy) {
	case ALLOCATION_STRATEGY_EQUAL: return "equal";
	case ALLOCATION_STRATEGY_PRIORITY: return "priority";
	case ALLOCATION_STRATEGY_ADAPTIVE: return "adaptive";
	case ALLOCATION_STRATEGY_PREDICTIVE: return "predictive";
	default: return "unknown";
	}
}

allocation_strategy_t string_to_allocation_strategy(const char *strategy_str)
{
	if (!strategy_str) {
		return ALLOCATION_STRATEGY_EQUAL;
	}
	
	if (strcmp(strategy_str, "equal") == 0) return ALLOCATION_STRATEGY_EQUAL;
	if (strcmp(strategy_str, "priority") == 0) return ALLOCATION_STRATEGY_PRIORITY;
	if (strcmp(strategy_str, "adaptive") == 0) return ALLOCATION_STRATEGY_ADAPTIVE;
	if (strcmp(strategy_str, "predictive") == 0) return ALLOCATION_STRATEGY_PREDICTIVE;
	
	return ALLOCATION_STRATEGY_EQUAL;
}

char *spending_policy_to_string(spending_policy_t policy)
{
	switch (policy) {
	case SPENDING_POLICY_CONSERVATIVE: return "conservative";
	case SPENDING_POLICY_BALANCED: return "balanced";
	case SPENDING_POLICY_AGGRESSIVE: return "aggressive";
	case SPENDING_POLICY_ADAPTIVE: return "adaptive";
	default: return "unknown";
	}
}

spending_policy_t string_to_spending_policy(const char *policy_str)
{
	if (!policy_str) {
		return SPENDING_POLICY_BALANCED;
	}
	
	if (strcmp(policy_str, "conservative") == 0) return SPENDING_POLICY_CONSERVATIVE;
	if (strcmp(policy_str, "balanced") == 0) return SPENDING_POLICY_BALANCED;
	if (strcmp(policy_str, "aggressive") == 0) return SPENDING_POLICY_AGGRESSIVE;
	if (strcmp(policy_str, "adaptive") == 0) return SPENDING_POLICY_ADAPTIVE;
	
	return SPENDING_POLICY_BALANCED;
}

int validate_allocation(credit_allocation_t *allocation)
{
	if (!allocation) {
		return 0;
	}
	
	if (!allocation->allocation_id || allocation->amount <= 0) {
		return 0;
	}
	
	return 1;
}

int validate_spending(credit_spending_t *spending)
{
	if (!spending) {
		return 0;
	}
	
	if (!spending->spending_id || !spending->action || spending->amount <= 0) {
		return 0;
	}
	
	return 1;
}

int validate_recovery(credit_recovery_t *recovery)
{
	if (!recovery) {
		return 0;
	}
	
	if (!recovery->recovery_id || !recovery->trigger || recovery->amount <= 0) {
		return 0;
	}
	
	return 1;
}

int validate_behavior_pattern(user_behavior_pattern_t *pattern)
{
	if (!pattern) {
		return 0;
	}
	
	if (!pattern->pattern_id || !pattern->user_id || !pattern->behavior_type) {
		return 0;
	}
	
	return 1;
}

int validate_user_feedback(user_feedback_t *feedback)
{
	if (!feedback) {
		return 0;
	}
	
	if (!feedback->feedback_id || !feedback->user_id || !feedback->action_id) {
		return 0;
	}
	
	return 1;
}

/*
 * Threading functions
 */
void *allocation_thread(void *arg)
{
	attention_credit_system_t *system = get_attention_credit_system();
	
	if (!system) {
		return NULL;
	}
	
	attention_credit_log_message("INFO", "Credit allocation thread started");
	
	while (system->threads_running) {
		/* Process time-based credit allocation */
		/* This would implement periodic credit allocation logic */
		
		/* Sleep for allocation interval */
		sleep(60); /* 1 minute */
	}
	
	attention_credit_log_message("INFO", "Credit allocation thread stopped");
	return NULL;
}

void *recovery_thread(void *arg)
{
	attention_credit_system_t *system = get_attention_credit_system();
	
	if (!system) {
		return NULL;
	}
	
	attention_credit_log_message("INFO", "Credit recovery thread started");
	
	while (system->threads_running) {
		/* Process time-based credit recovery */
		process_time_based_recovery();
		
		/* Sleep for recovery interval */
		sleep(300); /* 5 minutes */
	}
	
	attention_credit_log_message("INFO", "Credit recovery thread stopped");
	return NULL;
}

void *analysis_thread(void *arg)
{
	attention_credit_system_t *system = get_attention_credit_system();
	
	if (!system) {
		return NULL;
	}
	
	attention_credit_log_message("INFO", "Behavior analysis thread started");
	
	while (system->threads_running) {
		/* Process behavioral analysis */
		/* This would implement continuous behavior analysis logic */
		
		/* Sleep for analysis interval */
		sleep(60); /* 1 minute */
	}
	
	attention_credit_log_message("INFO", "Behavior analysis thread stopped");
	return NULL;
}

int start_attention_credit_threads(void)
{
	attention_credit_system_t *system = get_attention_credit_system();
	
	if (!system) {
		return -1;
	}
	
	/* Start allocation thread */
	if (pthread_create(&system->allocation_thread, NULL, allocation_thread, NULL) != 0) {
		attention_credit_log_message("ERROR", "Failed to start allocation thread");
		return -1;
	}
	
	/* Start recovery thread */
	if (pthread_create(&system->recovery_thread, NULL, recovery_thread, NULL) != 0) {
		attention_credit_log_message("ERROR", "Failed to start recovery thread");
		return -1;
	}
	
	/* Start analysis thread */
	if (pthread_create(&system->analysis_thread, NULL, analysis_thread, NULL) != 0) {
		attention_credit_log_message("ERROR", "Failed to start analysis thread");
		return -1;
	}
	
	return 0;
}

int stop_attention_credit_threads(void)
{
	attention_credit_system_t *system = get_attention_credit_system();
	
	if (!system) {
		return -1;
	}
	
	system->threads_running = FALSE;
	
	/* Wait for threads to complete */
	pthread_join(system->allocation_thread, NULL);
	pthread_join(system->recovery_thread, NULL);
	pthread_join(system->analysis_thread, NULL);
	
	return 0;
}

/*
 * Placeholder functions for remaining functionality
 */
int get_available_credits(const char *user_id, credit_type_t type, double *available) { return -1; }
int check_credit_sufficiency(const char *user_id, credit_type_t type, double amount) { return 0; }
int expire_allocations(const char *user_id) { return 0; }
int get_allocation_strategy(allocation_strategy_t strategy, double *multiplier) { return 0; }
int can_spend_credits(const char *user_id, credit_type_t type, double amount) { return 1; }
int calculate_spending_cost(credit_type_t type, double base_amount, spending_policy_t policy, double *cost) { *cost = base_amount; return 0; }
int apply_spending_policy(spending_policy_t policy, double *threshold, double *satisfaction_threshold, double *recovery_bonus) { return 0; }
int calculate_recovery_rate(const char *user_id, double base_rate, double user_satisfaction, double *rate) { *rate = base_rate; return 0; }
int apply_satisfaction_bonus(double user_satisfaction, double base_amount, double *bonus_amount) { *bonus_amount = 0.0; return 0; }
int process_time_based_recovery(void) { return 0; }
int detect_behavior_pattern(const char *user_id, const char *behavior_type, user_behavior_pattern_t **pattern) { return -1; }
int update_behavior_pattern(user_behavior_pattern_t *pattern, json_object *new_data) { return 0; }
int calculate_behavior_importance(const char *behavior_type, double frequency, double *importance) { *importance = 0.5; return 0; }
int calculate_feedback_weight(const char *feedback_type, double *weight) { *weight = 0.5; return 0; }
int apply_feedback_decay(user_feedback_t *feedback, double decay_rate) { return 0; }
int aggregate_user_satisfaction(const char *user_id, double *avg_satisfaction) { *avg_satisfaction = 0.5; return 0; }
int balance_economy(const char *user_id) { return 0; }
int calculate_equilibrium(const char *user_id, double *allocation_rate, double *spending_rate, double *recovery_rate) { return 0; }
int optimize_credit_distribution(const char *user_id) { return 0; }
int maintain_economic_balance(void) { return 0; }
int get_credit_balance(const char *user_id, credit_balance_t **balance) { return -1; }
int update_credit_balance(const char *user_id, double allocation_delta, double spending_delta, double recovery_delta) { return 0; }
int calculate_credit_breakdown(const char *user_id, json_object **breakdown) { return -1; }
int validate_credit_balance(credit_balance_t *balance) { return 1; }
int update_economic_metrics(const char *metric_type, double value) { return 0; }
int get_economic_metrics(economic_metrics_t *metrics) { return 0; }
int reset_economic_metrics(void) { return 0; }
int export_economic_report(const char *format, char **report) { return -1; }
int load_attention_credit_config(attention_credit_config_t *config, const char *config_file) { return -1; }
int save_attention_credit_config(attention_credit_config_t *config, const char *config_file) { return 0; }
void free_attention_credit_config(attention_credit_config_t *config) {}
int init_attention_credit_database(const char *db_path) { return 0; }
void cleanup_attention_credit_database(void) {}
int store_allocation_in_database(const char *allocation_id, credit_allocation_t *allocation) { return 0; }
int load_allocation_from_database(const char *allocation_id, credit_allocation_t **allocation) { return -1; }
int store_spending_in_database(const char *spending_id, credit_spending_t *spending) { return 0; }
int load_spending_from_database(const char *spending_id, credit_spending_t **spending) { return -1; }
int store_recovery_in_database(const char *recovery_id, credit_recovery_t *recovery) { return 0; }
int load_recovery_from_database(const char *recovery_id, credit_recovery_t **recovery) { return -1; }
int store_behavior_pattern_in_database(const char *pattern_id, user_behavior_pattern_t *pattern) { return 0; }
int load_behavior_pattern_from_database(const char *pattern_id, user_behavior_pattern_t **pattern) { return -1; }
int store_user_feedback_in_database(const char *feedback_id, user_feedback_t *feedback) { return 0; }
int load_user_feedback_from_database(const char *feedback_id, user_feedback_t **feedback) { return -1; }
int query_economic_metrics(const char *query, GList **results) { return -1; }
void set_attention_credit_error(int error_code, const char *error_message) {}
int get_attention_credit_error(void) { return 0; }
const char *get_attention_credit_error_message(void) { return ""; }

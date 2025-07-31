/*
 * HER OS Integration Layer
 *
 * Central orchestration engine that coordinates all HER OS intelligence components.
 * Provides unified interface for system-wide intelligence, automation, and user experience.
 *
 * Features:
 * - Unified daemon coordination and communication
 * - System-wide intelligence orchestration
 * - Workflow automation and task management
 * - User experience integration
 * - Performance monitoring and optimization
 * - Security policy enforcement
 * - Resource management and allocation
 *
 * Author: HER OS Project
 * License: GPL-2.0
 * Version: 1.0.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <syslog.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <json-c/json.h>
#include <curl/curl.h>
#include <sqlite3.h>
#include <math.h>
#include <stdatomic.h>

// Include our optimization libraries
#include "../shared/zero_copy_ipc.h"
#include "../shared/lock_free_structures.h"
#include "../shared/simd_optimizations.h"

#define INTEGRATION_SOCKET_PATH "/tmp/heros_integration.sock"
#define INTEGRATION_DB_PATH "/var/lib/heros/integration.db"
#define INTEGRATION_CONFIG_PATH "/etc/heros/integration_config.json"
#define MAX_DAEMONS 20
#define MAX_WORKFLOWS 100
#define MAX_TASKS 1000
#define MAX_USERS 1000

// Integration Layer states
typedef enum {
    INTEGRATION_STATE_INITIALIZING = 0,
    INTEGRATION_STATE_RUNNING = 1,
    INTEGRATION_STATE_COORDINATING = 2,
    INTEGRATION_STATE_OPTIMIZING = 3,
    INTEGRATION_STATE_SHUTDOWN = 4
} integration_state_t;

// Daemon information
typedef struct {
    char name[64];
    char socket_path[256];
    char url[256];
    int pid;
    integration_state_t state;
    uint64_t last_heartbeat;
    double health_score;
    uint64_t response_time_ms;
    uint64_t error_count;
    uint64_t success_count;
} daemon_info_t;

// Workflow definition
typedef struct {
    uint64_t workflow_id;
    char workflow_name[128];
    char workflow_type[64];
    char workflow_json[4096];
    double priority;
    uint64_t created_time;
    uint64_t scheduled_time;
    integration_state_t status;
    uint64_t current_step;
    uint64_t total_steps;
    double progress_percent;
} workflow_t;

// System-wide task
typedef struct {
    uint64_t task_id;
    char task_name[128];
    char task_type[64];
    char description[512];
    uint64_t assigned_daemon;
    double priority;
    uint64_t created_time;
    uint64_t deadline;
    integration_state_t status;
    char result_data[2048];
} system_task_t;

// User session
typedef struct {
    uint64_t user_id;
    char username[64];
    uint64_t session_start;
    uint64_t last_activity;
    double attention_credits;
    char current_context[512];
    uint64_t active_workflows;
    uint64_t completed_tasks;
} user_session_t;

// Performance metrics
typedef struct {
    atomic_uint64_t total_requests;
    atomic_uint64_t successful_requests;
    atomic_uint64_t failed_requests;
    atomic_uint64_t workflows_executed;
    atomic_uint64_t tasks_completed;
    atomic_uint64_t daemon_communications;
    atomic_uint64_t optimization_cycles;
    time_t start_time;
} integration_metrics_t;

// Integration configuration
typedef struct {
    int max_concurrent_workflows;
    int max_concurrent_tasks;
    int daemon_health_check_interval;
    int optimization_interval;
    int workflow_timeout_seconds;
    int task_timeout_seconds;
    double min_health_score;
    char backup_path[256];
} integration_config_t;

// Global state
static integration_config_t g_config;
static integration_metrics_t g_metrics;
static integration_state_t g_integration_state = INTEGRATION_STATE_INITIALIZING;
static sqlite3 *g_db = NULL;
static pthread_mutex_t g_db_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_state_mutex = PTHREAD_MUTEX_INITIALIZER;

// Daemon registry
static daemon_info_t g_daemons[MAX_DAEMONS];
static int g_daemon_count = 0;
static pthread_mutex_t g_daemon_mutex = PTHREAD_MUTEX_INITIALIZER;

// Lock-free data structures
static lf_ring_buffer_t g_request_queue;
static lf_hash_table_t g_workflow_cache;
static lf_cache_t g_user_session_cache;
static lf_memory_pool_t g_task_pool;

// Zero-copy IPC contexts for all daemons
static ipc_context_t g_metadata_ipc;
static ipc_context_t g_action_ipc;
static ipc_context_t g_ai_ipc;
static ipc_context_t g_pta_ipc;
static ipc_context_t g_wal_ipc;
static ipc_context_t g_dedup_ipc;
static ipc_context_t g_tiering_ipc;
static ipc_context_t g_pdp_ipc;

// Function prototypes
int integration_init(void);
void integration_cleanup(void);
int integration_load_config(void);
int integration_init_database(void);
int integration_init_optimizations(void);
int integration_init_daemon_connections(void);
int integration_register_daemon(const char *name, const char *socket_path, const char *url);
int integration_unregister_daemon(const char *name);
int integration_check_daemon_health(const char *name);
int integration_get_daemon_status(const char *name, daemon_info_t *info);

// Workflow management
int integration_create_workflow(const char *name, const char *type, const char *workflow_json, double priority);
int integration_execute_workflow(uint64_t workflow_id, uint64_t user_id);
int integration_monitor_workflow(uint64_t workflow_id, double *progress);
int integration_cancel_workflow(uint64_t workflow_id);
int integration_get_workflow_status(uint64_t workflow_id, integration_state_t *status);

// Task management
int integration_create_task(const char *name, const char *type, const char *description, 
                           uint64_t assigned_daemon, double priority, uint64_t deadline);
int integration_assign_task(uint64_t task_id, uint64_t daemon_id);
int integration_monitor_task(uint64_t task_id, integration_state_t *status);
int integration_cancel_task(uint64_t task_id);
int integration_get_task_result(uint64_t task_id, char *result, size_t result_size);

// User session management
int integration_create_user_session(uint64_t user_id, const char *username);
int integration_update_user_context(uint64_t user_id, const char *context);
int integration_get_user_session(uint64_t user_id, user_session_t *session);
int integration_end_user_session(uint64_t user_id);

// System coordination
int integration_coordinate_daemons(void);
int integration_optimize_system(void);
int integration_balance_load(void);
int integration_handle_daemon_failure(const char *daemon_name);
int integration_restart_daemon(const char *daemon_name);

// Communication with daemons
int integration_send_to_daemon(const char *daemon_name, const char *message);
int integration_receive_from_daemon(const char *daemon_name, char *response, size_t response_size);
int integration_broadcast_to_daemons(const char *message);
int integration_collect_daemon_metrics(void);

// Main coordination loops
void *integration_main_loop(void *arg);
void *integration_health_monitor_loop(void *arg);
void *integration_optimization_loop(void *arg);
void *integration_workflow_execution_loop(void *arg);
void *integration_task_coordination_loop(void *arg);

// Utility functions
uint64_t integration_get_timestamp(void);
int integration_validate_workflow(const workflow_t *workflow);
int integration_validate_task(const system_task_t *task);
void integration_log_event(const char *event_type, const char *details);
void integration_update_metrics(const char *metric_type);
double integration_calculate_health_score(const daemon_info_t *daemon);

// Signal handlers
void integration_handle_sigterm(int sig);
void integration_handle_sigint(int sig);
void integration_handle_sighup(int sig);

/**
 * Initialize Integration Layer
 */
int integration_init(void) {
    printf("[Integration Layer] Initializing HER OS Integration Layer...\n");
    
    // Initialize metrics
    memset(&g_metrics, 0, sizeof(integration_metrics_t));
    g_metrics.start_time = time(NULL);
    
    // Load configuration
    if (integration_load_config() != 0) {
        fprintf(stderr, "[Integration Layer] Failed to load configuration\n");
        return -1;
    }
    
    // Initialize database
    if (integration_init_database() != 0) {
        fprintf(stderr, "[Integration Layer] Failed to initialize database\n");
        return -1;
    }
    
    // Initialize optimization libraries
    if (integration_init_optimizations() != 0) {
        fprintf(stderr, "[Integration Layer] Failed to initialize optimizations\n");
        return -1;
    }
    
    // Initialize daemon connections
    if (integration_init_daemon_connections() != 0) {
        fprintf(stderr, "[Integration Layer] Failed to initialize daemon connections\n");
        return -1;
    }
    
    // Register all HER OS daemons
    integration_register_daemon("metadata", "/tmp/heros_metadata.sock", "");
    integration_register_daemon("action", "/tmp/heros_action.sock", "");
    integration_register_daemon("ai_integration", "/tmp/heros_ai_integration.sock", "");
    integration_register_daemon("pta_engine", "/tmp/heros_pta_engine.sock", "");
    integration_register_daemon("wal_daemon", "", "http://127.0.0.1:9292");
    integration_register_daemon("dedup_daemon", "/tmp/heros_dedup.sock", "");
    integration_register_daemon("tiering_daemon", "/tmp/heros_tiering.sock", "");
    integration_register_daemon("pdp_daemon", "/tmp/heros_pdp.sock", "");
    
    // Set up signal handlers
    signal(SIGTERM, integration_handle_sigterm);
    signal(SIGINT, integration_handle_sigint);
    signal(SIGHUP, integration_handle_sighup);
    
    // Initialize syslog
    openlog("heros_integration_layer", LOG_PID | LOG_CONS, LOG_DAEMON);
    
    // Set state to running
    g_integration_state = INTEGRATION_STATE_RUNNING;
    
    printf("[Integration Layer] Initialization completed successfully\n");
    integration_log_event("INIT", "Integration Layer initialized successfully");
    
    return 0;
}

/**
 * Load Integration Layer configuration
 */
int integration_load_config(void) {
    json_object *config_json = json_object_from_file(INTEGRATION_CONFIG_PATH);
    if (!config_json) {
        // Use default configuration
        g_config.max_concurrent_workflows = 10;
        g_config.max_concurrent_tasks = 50;
        g_config.daemon_health_check_interval = 30;
        g_config.optimization_interval = 300;
        g_config.workflow_timeout_seconds = 600;
        g_config.task_timeout_seconds = 300;
        g_config.min_health_score = 0.7;
        strcpy(g_config.backup_path, "/var/backup/heros/integration_backup.db");
        
        printf("[Integration Layer] Using default configuration\n");
        return 0;
    }
    
    // Parse configuration
    json_object *max_workflows, *max_tasks, *health_interval, *opt_interval;
    json_object *workflow_timeout, *task_timeout, *min_health, *backup_path;
    
    if (json_object_object_get_ex(config_json, "max_concurrent_workflows", &max_workflows))
        g_config.max_concurrent_workflows = json_object_get_int(max_workflows);
    
    if (json_object_object_get_ex(config_json, "max_concurrent_tasks", &max_tasks))
        g_config.max_concurrent_tasks = json_object_get_int(max_tasks);
    
    if (json_object_object_get_ex(config_json, "daemon_health_check_interval", &health_interval))
        g_config.daemon_health_check_interval = json_object_get_int(health_interval);
    
    if (json_object_object_get_ex(config_json, "optimization_interval", &opt_interval))
        g_config.optimization_interval = json_object_get_int(opt_interval);
    
    if (json_object_object_get_ex(config_json, "workflow_timeout_seconds", &workflow_timeout))
        g_config.workflow_timeout_seconds = json_object_get_int(workflow_timeout);
    
    if (json_object_object_get_ex(config_json, "task_timeout_seconds", &task_timeout))
        g_config.task_timeout_seconds = json_object_get_int(task_timeout);
    
    if (json_object_object_get_ex(config_json, "min_health_score", &min_health))
        g_config.min_health_score = json_object_get_double(min_health);
    
    if (json_object_object_get_ex(config_json, "backup_path", &backup_path))
        strcpy(g_config.backup_path, json_object_get_string(backup_path));
    
    json_object_put(config_json);
    
    printf("[Integration Layer] Configuration loaded successfully\n");
    return 0;
}

/**
 * Initialize database
 */
int integration_init_database(void) {
    int rc = sqlite3_open(INTEGRATION_DB_PATH, &g_db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[Integration Layer] Failed to open database: %s\n", sqlite3_errmsg(g_db));
        return -1;
    }
    
    // Create tables
    const char *create_tables_sql = 
        "CREATE TABLE IF NOT EXISTS daemons ("
        "daemon_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT UNIQUE NOT NULL,"
        "socket_path TEXT,"
        "url TEXT,"
        "pid INTEGER,"
        "state INTEGER NOT NULL,"
        "last_heartbeat INTEGER NOT NULL,"
        "health_score REAL NOT NULL,"
        "response_time_ms INTEGER NOT NULL,"
        "error_count INTEGER NOT NULL,"
        "success_count INTEGER NOT NULL,"
        "created_time INTEGER NOT NULL"
        ");"
        
        "CREATE TABLE IF NOT EXISTS workflows ("
        "workflow_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "workflow_name TEXT NOT NULL,"
        "workflow_type TEXT NOT NULL,"
        "workflow_json TEXT NOT NULL,"
        "priority REAL NOT NULL,"
        "created_time INTEGER NOT NULL,"
        "scheduled_time INTEGER NOT NULL,"
        "status INTEGER NOT NULL,"
        "current_step INTEGER NOT NULL,"
        "total_steps INTEGER NOT NULL,"
        "progress_percent REAL NOT NULL"
        ");"
        
        "CREATE TABLE IF NOT EXISTS system_tasks ("
        "task_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "task_name TEXT NOT NULL,"
        "task_type TEXT NOT NULL,"
        "description TEXT NOT NULL,"
        "assigned_daemon INTEGER NOT NULL,"
        "priority REAL NOT NULL,"
        "created_time INTEGER NOT NULL,"
        "deadline INTEGER NOT NULL,"
        "status INTEGER NOT NULL,"
        "result_data TEXT"
        ");"
        
        "CREATE TABLE IF NOT EXISTS user_sessions ("
        "user_id INTEGER PRIMARY KEY,"
        "username TEXT NOT NULL,"
        "session_start INTEGER NOT NULL,"
        "last_activity INTEGER NOT NULL,"
        "attention_credits REAL NOT NULL,"
        "current_context TEXT,"
        "active_workflows INTEGER NOT NULL,"
        "completed_tasks INTEGER NOT NULL"
        ");"
        
        "CREATE INDEX IF NOT EXISTS idx_daemons_name ON daemons(name);"
        "CREATE INDEX IF NOT EXISTS idx_workflows_scheduled ON workflows(scheduled_time);"
        "CREATE INDEX IF NOT EXISTS idx_tasks_assigned ON system_tasks(assigned_daemon);"
        "CREATE INDEX IF NOT EXISTS idx_sessions_user_id ON user_sessions(user_id);";
    
    char *err_msg = 0;
    rc = sqlite3_exec(g_db, create_tables_sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[Integration Layer] Failed to create tables: %s\n", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }
    
    printf("[Integration Layer] Database initialized successfully\n");
    return 0;
}

/**
 * Initialize optimization libraries
 */
int integration_init_optimizations(void) {
    // Initialize lock-free ring buffer for requests
    if (lf_ring_init(&g_request_queue, sizeof(char) * 2048, 1000) != 0) {
        fprintf(stderr, "[Integration Layer] Failed to initialize request queue\n");
        return -1;
    }
    
    // Initialize lock-free hash table for workflow cache
    if (lf_hash_init(&g_workflow_cache, 1024) != 0) {
        fprintf(stderr, "[Integration Layer] Failed to initialize workflow cache\n");
        return -1;
    }
    
    // Initialize lock-free cache for user sessions
    if (lf_cache_init(&g_user_session_cache, 512) != 0) {
        fprintf(stderr, "[Integration Layer] Failed to initialize user session cache\n");
        return -1;
    }
    
    // Initialize memory pool for tasks
    if (lf_pool_init(&g_task_pool, 200) != 0) {
        fprintf(stderr, "[Integration Layer] Failed to initialize task memory pool\n");
        return -1;
    }
    
    printf("[Integration Layer] Optimization libraries initialized successfully\n");
    return 0;
}

/**
 * Initialize daemon connections
 */
int integration_init_daemon_connections(void) {
    // Initialize connections to all daemons
    const char *daemon_connections[][2] = {
        {"metadata", "/tmp/heros_metadata_integration.sock"},
        {"action", "/tmp/heros_action_integration.sock"},
        {"ai_integration", "/tmp/heros_ai_integration.sock"},
        {"pta_engine", "/tmp/heros_pta_integration.sock"},
        {"dedup_daemon", "/tmp/heros_dedup_integration.sock"},
        {"tiering_daemon", "/tmp/heros_tiering_integration.sock"},
        {"pdp_daemon", "/tmp/heros_pdp_integration.sock"}
    };
    
    for (int i = 0; i < 7; i++) {
        if (ipc_init(&g_metadata_ipc, daemon_connections[i][1], 1, -1) != IPC_SUCCESS) {
            fprintf(stderr, "[Integration Layer] Failed to initialize %s IPC\n", daemon_connections[i][0]);
            return -1;
        }
    }
    
    printf("[Integration Layer] Daemon connections initialized successfully\n");
    return 0;
}

/**
 * Register a daemon with the integration layer
 */
int integration_register_daemon(const char *name, const char *socket_path, const char *url) {
    if (!name) {
        return -1;
    }
    
    pthread_mutex_lock(&g_daemon_mutex);
    
    if (g_daemon_count >= MAX_DAEMONS) {
        pthread_mutex_unlock(&g_daemon_mutex);
        return -1;
    }
    
    // Check if daemon already exists
    for (int i = 0; i < g_daemon_count; i++) {
        if (strcmp(g_daemons[i].name, name) == 0) {
            pthread_mutex_unlock(&g_daemon_mutex);
            return 0; // Already registered
        }
    }
    
    // Add new daemon
    daemon_info_t *daemon = &g_daemons[g_daemon_count];
    strncpy(daemon->name, name, sizeof(daemon->name) - 1);
    strncpy(daemon->socket_path, socket_path ? socket_path : "", sizeof(daemon->socket_path) - 1);
    strncpy(daemon->url, url ? url : "", sizeof(daemon->url) - 1);
    daemon->pid = 0;
    daemon->state = INTEGRATION_STATE_INITIALIZING;
    daemon->last_heartbeat = integration_get_timestamp();
    daemon->health_score = 1.0;
    daemon->response_time_ms = 0;
    daemon->error_count = 0;
    daemon->success_count = 0;
    
    g_daemon_count++;
    
    pthread_mutex_unlock(&g_daemon_mutex);
    
    // Store in database
    pthread_mutex_lock(&g_db_mutex);
    
    const char *sql = "INSERT OR REPLACE INTO daemons (name, socket_path, url, pid, state, "
                     "last_heartbeat, health_score, response_time_ms, error_count, success_count, created_time) "
                     "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, daemon->name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, daemon->socket_path, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, daemon->url, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 4, daemon->pid);
        sqlite3_bind_int(stmt, 5, daemon->state);
        sqlite3_bind_int64(stmt, 6, daemon->last_heartbeat);
        sqlite3_bind_double(stmt, 7, daemon->health_score);
        sqlite3_bind_int64(stmt, 8, daemon->response_time_ms);
        sqlite3_bind_int64(stmt, 9, daemon->error_count);
        sqlite3_bind_int64(stmt, 10, daemon->success_count);
        sqlite3_bind_int64(stmt, 11, integration_get_timestamp());
        
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    
    pthread_mutex_unlock(&g_db_mutex);
    
    if (rc == SQLITE_DONE) {
        integration_log_event("DAEMON_REGISTERED", name);
        printf("[Integration Layer] Registered daemon: %s\n", name);
        return 0;
    } else {
        return -1;
    }
}

/**
 * Check daemon health
 */
int integration_check_daemon_health(const char *name) {
    if (!name) {
        return -1;
    }
    
    pthread_mutex_lock(&g_daemon_mutex);
    
    daemon_info_t *daemon = NULL;
    for (int i = 0; i < g_daemon_count; i++) {
        if (strcmp(g_daemons[i].name, name) == 0) {
            daemon = &g_daemons[i];
            break;
        }
    }
    
    if (!daemon) {
        pthread_mutex_unlock(&g_daemon_mutex);
        return -1;
    }
    
    // Send health check message
    uint64_t start_time = integration_get_timestamp();
    char health_message[256];
    snprintf(health_message, sizeof(health_message), 
             "{\"type\":\"health_check\",\"timestamp\":%lu}", start_time);
    
    int result = integration_send_to_daemon(name, health_message);
    
    uint64_t end_time = integration_get_timestamp();
    daemon->response_time_ms = (end_time - start_time) / 1000000; // Convert to milliseconds
    daemon->last_heartbeat = end_time;
    
    if (result == 0) {
        daemon->success_count++;
        daemon->health_score = integration_calculate_health_score(daemon);
        daemon->state = INTEGRATION_STATE_RUNNING;
    } else {
        daemon->error_count++;
        daemon->health_score = integration_calculate_health_score(daemon);
        if (daemon->health_score < g_config.min_health_score) {
            daemon->state = INTEGRATION_STATE_SHUTDOWN;
            integration_handle_daemon_failure(name);
        }
    }
    
    pthread_mutex_unlock(&g_daemon_mutex);
    
    return result;
}

/**
 * Main integration loop
 */
void *integration_main_loop(void *arg) {
    printf("[Integration Layer] Starting main coordination loop...\n");
    
    pthread_t health_thread, optimization_thread, workflow_thread, task_thread;
    
    // Start worker threads
    pthread_create(&health_thread, NULL, integration_health_monitor_loop, NULL);
    pthread_create(&optimization_thread, NULL, integration_optimization_loop, NULL);
    pthread_create(&workflow_thread, NULL, integration_workflow_execution_loop, NULL);
    pthread_create(&task_thread, NULL, integration_task_coordination_loop, NULL);
    
    // Main coordination loop
    while (g_integration_state != INTEGRATION_STATE_SHUTDOWN) {
        // Coordinate all daemons
        integration_coordinate_daemons();
        
        // Process requests from ring buffer
        char request_data[2048];
        uint32_t request_size = sizeof(request_data);
        
        if (lf_ring_pop(&g_request_queue, request_data) == 0) {
            // Process request
            printf("[Integration Layer] Processing request: %s\n", request_data);
            
            // Parse request and coordinate response
            json_object *request_json = json_tokener_parse(request_data);
            if (request_json) {
                json_object *request_type, *user_id, *data;
                
                if (json_object_object_get_ex(request_json, "type", &request_type)) {
                    const char *type = json_object_get_string(request_type);
                    
                    if (strcmp(type, "workflow_request") == 0) {
                        // Handle workflow request
                        if (json_object_object_get_ex(request_json, "data", &data)) {
                            json_object *workflow_name, *workflow_type, *workflow_json;
                            if (json_object_object_get_ex(data, "name", &workflow_name) &&
                                json_object_object_get_ex(data, "type", &workflow_type) &&
                                json_object_object_get_ex(data, "workflow", &workflow_json)) {
                                integration_create_workflow(json_object_get_string(workflow_name),
                                                          json_object_get_string(workflow_type),
                                                          json_object_get_string(workflow_json),
                                                          0.5);
                            }
                        }
                    } else if (strcmp(type, "task_request") == 0) {
                        // Handle task request
                        if (json_object_object_get_ex(request_json, "data", &data)) {
                            json_object *task_name, *task_type, *description;
                            if (json_object_object_get_ex(data, "name", &task_name) &&
                                json_object_object_get_ex(data, "type", &task_type) &&
                                json_object_object_get_ex(data, "description", &description)) {
                                integration_create_task(json_object_get_string(task_name),
                                                      json_object_get_string(task_type),
                                                      json_object_get_string(description),
                                                      0, 0.5, 0);
                            }
                        }
                    } else if (strcmp(type, "user_session") == 0) {
                        // Handle user session
                        if (json_object_object_get_ex(request_json, "user_id", &user_id) &&
                            json_object_object_get_ex(request_json, "data", &data)) {
                            json_object *username;
                            if (json_object_object_get_ex(data, "username", &username)) {
                                integration_create_user_session(json_object_get_int64(user_id),
                                                               json_object_get_string(username));
                            }
                        }
                    }
                }
                json_object_put(request_json);
            }
        }
        
        // Sleep briefly to prevent busy waiting
        usleep(10000); // 10ms
    }
    
    // Wait for worker threads to finish
    pthread_join(health_thread, NULL);
    pthread_join(optimization_thread, NULL);
    pthread_join(workflow_thread, NULL);
    pthread_join(task_thread, NULL);
    
    printf("[Integration Layer] Main coordination loop finished\n");
    return NULL;
}

/**
 * Health monitoring loop
 */
void *integration_health_monitor_loop(void *arg) {
    printf("[Integration Layer] Starting health monitoring loop...\n");
    
    while (g_integration_state != INTEGRATION_STATE_SHUTDOWN) {
        // Check health of all registered daemons
        pthread_mutex_lock(&g_daemon_mutex);
        
        for (int i = 0; i < g_daemon_count; i++) {
            integration_check_daemon_health(g_daemons[i].name);
        }
        
        pthread_mutex_unlock(&g_daemon_mutex);
        
        // Sleep for health check interval
        sleep(g_config.daemon_health_check_interval);
    }
    
    printf("[Integration Layer] Health monitoring loop finished\n");
    return NULL;
}

/**
 * Optimization loop
 */
void *integration_optimization_loop(void *arg) {
    printf("[Integration Layer] Starting optimization loop...\n");
    
    while (g_integration_state != INTEGRATION_STATE_SHUTDOWN) {
        // Perform system optimization
        integration_optimize_system();
        
        // Balance load across daemons
        integration_balance_load();
        
        // Sleep for optimization interval
        sleep(g_config.optimization_interval);
    }
    
    printf("[Integration Layer] Optimization loop finished\n");
    return NULL;
}

/**
 * Workflow execution loop
 */
void *integration_workflow_execution_loop(void *arg) {
    printf("[Integration Layer] Starting workflow execution loop...\n");
    
    while (g_integration_state != INTEGRATION_STATE_SHUTDOWN) {
        // Execute scheduled workflows
        // This would typically involve:
        // - Checking for workflows ready to execute
        // - Coordinating workflow steps across daemons
        // - Monitoring workflow progress
        
        sleep(10); // Check every 10 seconds
    }
    
    printf("[Integration Layer] Workflow execution loop finished\n");
    return NULL;
}

/**
 * Task coordination loop
 */
void *integration_task_coordination_loop(void *arg) {
    printf("[Integration Layer] Starting task coordination loop...\n");
    
    while (g_integration_state != INTEGRATION_STATE_SHUTDOWN) {
        // Coordinate tasks across daemons
        // This would typically involve:
        // - Assigning tasks to appropriate daemons
        // - Monitoring task progress
        // - Handling task failures and retries
        
        sleep(5); // Check every 5 seconds
    }
    
    printf("[Integration Layer] Task coordination loop finished\n");
    return NULL;
}

/**
 * Coordinate all daemons
 */
int integration_coordinate_daemons(void) {
    // Send coordination message to all daemons
    char coordination_message[512];
    snprintf(coordination_message, sizeof(coordination_message),
             "{\"type\":\"coordination\",\"timestamp\":%lu,\"state\":%d}",
             integration_get_timestamp(), g_integration_state);
    
    return integration_broadcast_to_daemons(coordination_message);
}

/**
 * Optimize system performance
 */
int integration_optimize_system(void) {
    printf("[Integration Layer] Performing system optimization...\n");
    
    // Collect performance metrics from all daemons
    integration_collect_daemon_metrics();
    
    // Analyze system performance
    // This would typically involve:
    // - Analyzing daemon performance metrics
    // - Identifying bottlenecks
    // - Adjusting resource allocation
    // - Optimizing communication patterns
    
    integration_update_metrics("optimization_cycles");
    return 0;
}

/**
 * Balance load across daemons
 */
int integration_balance_load(void) {
    printf("[Integration Layer] Balancing load across daemons...\n");
    
    // Analyze current load distribution
    // This would typically involve:
    // - Checking daemon utilization
    // - Identifying overloaded daemons
    // - Redistributing tasks
    // - Scaling daemon resources
    
    return 0;
}

/**
 * Handle daemon failure
 */
int integration_handle_daemon_failure(const char *daemon_name) {
    printf("[Integration Layer] Handling failure of daemon: %s\n", daemon_name);
    
    // Attempt to restart the daemon
    return integration_restart_daemon(daemon_name);
}

/**
 * Restart a daemon
 */
int integration_restart_daemon(const char *daemon_name) {
    printf("[Integration Layer] Restarting daemon: %s\n", daemon_name);
    
    // This would typically involve:
    // - Stopping the daemon process
    // - Cleaning up resources
    // - Starting a new daemon process
    // - Re-establishing connections
    
    return 0;
}

/**
 * Send message to specific daemon
 */
int integration_send_to_daemon(const char *daemon_name, const char *message) {
    if (!daemon_name || !message) {
        return -1;
    }
    
    // Find daemon
    pthread_mutex_lock(&g_daemon_mutex);
    
    daemon_info_t *daemon = NULL;
    for (int i = 0; i < g_daemon_count; i++) {
        if (strcmp(g_daemons[i].name, daemon_name) == 0) {
            daemon = &g_daemons[i];
            break;
        }
    }
    
    if (!daemon) {
        pthread_mutex_unlock(&g_daemon_mutex);
        return -1;
    }
    
    pthread_mutex_unlock(&g_daemon_mutex);
    
    // Send message via appropriate method
    if (strlen(daemon->socket_path) > 0) {
        // Use Unix socket
        return ipc_send(&g_metadata_ipc, MSG_TYPE_METRICS_UPDATE, message, strlen(message), 1000);
    } else if (strlen(daemon->url) > 0) {
        // Use HTTP/REST
        // This would use curl to send HTTP request
        return 0;
    }
    
    return -1;
}

/**
 * Broadcast message to all daemons
 */
int integration_broadcast_to_daemons(const char *message) {
    if (!message) {
        return -1;
    }
    
    pthread_mutex_lock(&g_daemon_mutex);
    
    int success_count = 0;
    for (int i = 0; i < g_daemon_count; i++) {
        if (integration_send_to_daemon(g_daemons[i].name, message) == 0) {
            success_count++;
        }
    }
    
    pthread_mutex_unlock(&g_daemon_mutex);
    
    return (success_count == g_daemon_count) ? 0 : -1;
}

/**
 * Utility functions
 */
uint64_t integration_get_timestamp(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

double integration_calculate_health_score(const daemon_info_t *daemon) {
    if (!daemon) {
        return 0.0;
    }
    
    // Calculate health score based on various factors
    double response_score = (daemon->response_time_ms < 100) ? 1.0 : 
                           (daemon->response_time_ms < 1000) ? 0.8 : 0.5;
    
    double error_score = (daemon->error_count == 0) ? 1.0 :
                        (daemon->success_count > 0) ? 
                        (double)daemon->success_count / (daemon->success_count + daemon->error_count) : 0.0;
    
    double recency_score = (integration_get_timestamp() - daemon->last_heartbeat < 60000000000ULL) ? 1.0 : 0.5;
    
    return (response_score + error_score + recency_score) / 3.0;
}

void integration_log_event(const char *event_type, const char *details) {
    syslog(LOG_INFO, "[Integration Layer] %s: %s", event_type, details);
}

void integration_update_metrics(const char *metric_type) {
    if (strcmp(metric_type, "total_requests") == 0) {
        atomic_fetch_add(&g_metrics.total_requests, 1);
    } else if (strcmp(metric_type, "successful_requests") == 0) {
        atomic_fetch_add(&g_metrics.successful_requests, 1);
    } else if (strcmp(metric_type, "failed_requests") == 0) {
        atomic_fetch_add(&g_metrics.failed_requests, 1);
    } else if (strcmp(metric_type, "workflows_executed") == 0) {
        atomic_fetch_add(&g_metrics.workflows_executed, 1);
    } else if (strcmp(metric_type, "tasks_completed") == 0) {
        atomic_fetch_add(&g_metrics.tasks_completed, 1);
    } else if (strcmp(metric_type, "daemon_communications") == 0) {
        atomic_fetch_add(&g_metrics.daemon_communications, 1);
    } else if (strcmp(metric_type, "optimization_cycles") == 0) {
        atomic_fetch_add(&g_metrics.optimization_cycles, 1);
    }
}

/**
 * Signal handlers
 */
void integration_handle_sigterm(int sig) {
    printf("[Integration Layer] Received SIGTERM, shutting down...\n");
    g_integration_state = INTEGRATION_STATE_SHUTDOWN;
}

void integration_handle_sigint(int sig) {
    printf("[Integration Layer] Received SIGINT, shutting down...\n");
    g_integration_state = INTEGRATION_STATE_SHUTDOWN;
}

void integration_handle_sighup(int sig) {
    printf("[Integration Layer] Received SIGHUP, reloading configuration...\n");
    integration_load_config();
}

/**
 * Cleanup function
 */
void integration_cleanup(void) {
    printf("[Integration Layer] Cleaning up...\n");
    
    // Clean up optimization libraries
    lf_ring_cleanup(&g_request_queue);
    lf_hash_cleanup(&g_workflow_cache);
    lf_cache_cleanup(&g_user_session_cache);
    lf_pool_cleanup(&g_task_pool);
    
    // Clean up IPC connections
    ipc_cleanup(&g_metadata_ipc);
    ipc_cleanup(&g_action_ipc);
    ipc_cleanup(&g_ai_ipc);
    ipc_cleanup(&g_pta_ipc);
    ipc_cleanup(&g_wal_ipc);
    ipc_cleanup(&g_dedup_ipc);
    ipc_cleanup(&g_tiering_ipc);
    ipc_cleanup(&g_pdp_ipc);
    
    // Close database
    if (g_db) {
        sqlite3_close(g_db);
        g_db = NULL;
    }
    
    // Close syslog
    closelog();
    
    printf("[Integration Layer] Cleanup completed\n");
}

/**
 * Main function
 */
int main(int argc, char *argv[]) {
    printf("[Integration Layer] HER OS Integration Layer v1.0.0\n");
    
    // Initialize Integration Layer
    if (integration_init() != 0) {
        fprintf(stderr, "[Integration Layer] Failed to initialize\n");
        return 1;
    }
    
    // Start main coordination loop
    integration_main_loop(NULL);
    
    // Cleanup
    integration_cleanup();
    
    printf("[Integration Layer] Shutdown completed\n");
    return 0;
} 
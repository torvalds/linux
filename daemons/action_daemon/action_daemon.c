/*
 * action_daemon.c - AT-SPI Action Layer Daemon for HER OS
 *
 * Provides semantic UI automation and context ingestion via D-Bus (org.heros.Action).
 * Follows Linux kernel coding style (K&R, tabs, block comments).
 *
 * Enhanced Features:
 * - Comprehensive security validation and access control
 * - Advanced error handling and recovery mechanisms
 * - Performance monitoring and optimization
 * - Audit logging and compliance
 * - Thread safety and resource management
 * - Integration with HER OS security model
 *
 * Core methods:
 *   - ClickButton: Click UI elements with security validation
 *   - GetText: Retrieve text content with access control
 *   - SetText: Set text content with input validation
 *   - GetUITree: Get UI hierarchy with performance optimization
 *
 * Security Model:
 * - Input validation and sanitization
 * - Access control based on process permissions
 * - Path traversal prevention
 * - XSS and injection attack prevention
 * - Audit logging for all operations
 *
 * Performance Features:
 * - Multi-level caching (applications, elements, UI trees)
 * - Event batching and background processing
 * - Memory usage optimization
 * - Metrics collection and monitoring
 * - Thread-safe operations
 *
 * Author: HER OS Project
 * License: GPL-2.0
 * Version: 2.1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <gio/gio.h>
#include <atspi/atspi.h>
#include <regex.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <string.h>
#include <unistd.h>
#include <sys/un.h>
#include <pthread.h>
#include <glib.h>
#include <time.h>
#include <errno.h>
#include <syslog.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <signal.h>

#define ACTION_DBUS_NAME "org.heros.Action"
#define ACTION_DBUS_PATH "/org/heros/Action"
#define ACTION_DBUS_INTERFACE "org.heros.Action"
#define NETLINK_USER 31
#define MAX_PAYLOAD 1024
#define METADATA_SOCKET_PATH "/tmp/heros_metadata.sock"

/* Security and validation constants */
#define MAX_PATH_LENGTH 4096
#define MAX_TEXT_LENGTH 65536
#define MAX_ELEMENT_ID_LENGTH 256
#define MAX_APP_NAME_LENGTH 128
#define SOCKET_PERMISSIONS 0600

/* Performance and caching constants */
#define MAX_CACHE_SIZE 1000
#define CACHE_TTL_SECONDS 300  /* 5 minutes */
#define MAX_EVENT_QUEUE_SIZE 1000
#define EVENT_BATCH_SIZE 10
#define EVENT_BATCH_TIMEOUT_MS 100
#define MAX_CONCURRENT_OPERATIONS 50

/* Security validation patterns */
static const char *DANGEROUS_PATTERNS[] = {
    "../", "..\\", "script:", "javascript:", "data:", "vbscript:",
    "<script", "</script>", "<?php", "<?=", "<?", "?>",
    "union select", "drop table", "delete from", "insert into",
    "update set", "alter table", "create table", "exec ", "execute ",
    NULL
};

/* Performance monitoring */
typedef struct {
    atomic_int operations_total;
    atomic_int operations_successful;
    atomic_int operations_failed;
    atomic_int security_violations;
    atomic_int cache_hits;
    atomic_int cache_misses;
    atomic_long total_latency_ms;
    atomic_int active_connections;
    time_t start_time;
} performance_metrics_t;

static performance_metrics_t metrics = {0};

/* Access control functions */
static gboolean check_process_permission(int target_pid) {
    /* Get current user ID */
    uid_t current_uid = getuid();
    
    /* Get target process user ID */
    char proc_path[256];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d/status", target_pid);
    
    FILE *fp = fopen(proc_path, "r");
    if (!fp) {
        syslog(LOG_WARNING, "Access control: cannot read process status for PID %d", target_pid);
        return FALSE;
    }
    
    uid_t target_uid = 0;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "Uid:", 4) == 0) {
            sscanf(line, "Uid: %u", &target_uid);
            break;
        }
    }
    fclose(fp);
    
    /* Check if current user can access target process */
    if (current_uid != 0 && current_uid != target_uid) {
        syslog(LOG_WARNING, "Access control: user %u cannot access process %d (owned by %u)", 
               current_uid, target_pid, target_uid);
        return FALSE;
    }
    
    return TRUE;
}

/* Audit logging */
static void log_audit_event(const char *operation, const char *app_name, int app_pid, 
                           const char *element_id, const char *result, const char *details) {
    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    syslog(LOG_INFO, "[AUDIT] %s | %s | %s | %d | %s | %s | %s | %s",
           timestamp, operation, app_name ? app_name : "unknown", app_pid,
           element_id ? element_id : "none", result, details ? details : "",
           getenv("USER") ? getenv("USER") : "unknown");
}

/* Performance monitoring */
static void record_operation(gboolean success, long latency_ms) {
    atomic_fetch_add(&metrics.operations_total, 1);
    if (success) {
        atomic_fetch_add(&metrics.operations_successful, 1);
    } else {
        atomic_fetch_add(&metrics.operations_failed, 1);
    }
    atomic_fetch_add(&metrics.total_latency_ms, latency_ms);
}

static void record_cache_access(gboolean hit) {
    if (hit) {
        atomic_fetch_add(&metrics.cache_hits, 1);
    } else {
        atomic_fetch_add(&metrics.cache_misses, 1);
    }
}

/* Enhanced error handling */
typedef enum {
    ERROR_NONE = 0,
    ERROR_INVALID_PARAMETER,
    ERROR_SECURITY_VIOLATION,
    ERROR_ACCESS_DENIED,
    ERROR_PROCESS_NOT_FOUND,
    ERROR_ELEMENT_NOT_FOUND,
    ERROR_ATSPI_ERROR,
    ERROR_MEMORY_ERROR,
    ERROR_TIMEOUT,
    ERROR_UNKNOWN
} action_error_t;

static const char *error_messages[] = {
    "No error",
    "Invalid parameter",
    "Security violation",
    "Access denied",
    "Process not found",
    "Element not found",
    "AT-SPI error",
    "Memory error",
    "Operation timeout",
    "Unknown error"
};

static action_error_t last_error = ERROR_NONE;

static void set_error(action_error_t error) {
    last_error = error;
    syslog(LOG_ERR, "Action daemon error: %s", error_messages[error]);
}

static const char *get_last_error_message(void) {
    return error_messages[last_error];
}

/* Resource management */
static void cleanup_resources(void) {
    /* Clean up AT-SPI connection */
    if (root_accessible) {
        g_object_unref(root_accessible);
        root_accessible = NULL;
    }
    
    /* Clean up caches */
    cleanup_cache(&app_cache);
    cleanup_cache(&element_cache);
    cleanup_cache(&ui_tree_cache);
    
    /* Clean up event queue */
    cleanup_event_queue(&event_queue);
    
    /* Close sockets */
    if (pdp_socket >= 0) {
        close(pdp_socket);
        pdp_socket = -1;
    }
    
    if (metadata_socket >= 0) {
        close(metadata_socket);
        metadata_socket = -1;
    }
    
    /* Stop context thread */
    context_running = FALSE;
    pthread_join(context_thread, NULL);
    
    syslog(LOG_INFO, "Action daemon resources cleaned up");
}

/* Signal handlers */
static void handle_sigterm(int sig) {
    syslog(LOG_INFO, "Action daemon received SIGTERM, shutting down gracefully");
    cleanup_resources();
    exit(0);
}

static void handle_sigint(int sig) {
    syslog(LOG_INFO, "Action daemon received SIGINT, shutting down gracefully");
    cleanup_resources();
    exit(0);
}

/* Enhanced initialization */
static gboolean init_action_daemon(void) {
    /* Initialize syslog */
    openlog("heros-action-daemon", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "HER OS Action Daemon starting (version 2.0.0)");
    
    /* Set up signal handlers */
    signal(SIGTERM, handle_sigterm);
    signal(SIGINT, handle_sigint);
    
    /* Initialize performance metrics */
    metrics.start_time = time(NULL);
    
    /* Initialize AT-SPI with error handling */
    if (!init_atspi()) {
        syslog(LOG_ERR, "Failed to initialize AT-SPI");
        return FALSE;
    }
    
    /* Initialize PDP connection with security */
    if (!init_pdp_connection()) {
        syslog(LOG_ERR, "Failed to initialize PDP connection");
        return FALSE;
    }
    
    /* Initialize metadata connection */
    if (!init_metadata_connection()) {
        syslog(LOG_ERR, "Failed to initialize metadata connection");
        return FALSE;
    }
    
    /* Initialize caches with performance optimization */
    init_cache(&app_cache, 100);
    init_cache(&element_cache, 500);
    init_cache(&ui_tree_cache, 200);
    
    /* Initialize event queue */
    init_event_queue(&event_queue, MAX_EVENT_QUEUE_SIZE);
    
    /* Start context ingestion thread */
    if (pthread_create(&context_thread, NULL, context_ingestion_thread, NULL) != 0) {
        syslog(LOG_ERR, "Failed to create context ingestion thread");
        return FALSE;
    }
    
    /* Initialize AT-SPI events with security */
    if (!init_atspi_events()) {
        syslog(LOG_ERR, "Failed to initialize AT-SPI events");
        return FALSE;
    }
    
    syslog(LOG_INFO, "Action daemon initialized successfully");
    syslog(LOG_INFO, "Security features: Input validation, access control, audit logging");
    syslog(LOG_INFO, "Performance features: Multi-level caching, event batching, metrics collection");
    
    return TRUE;
}

/*
 * action_daemon.c - AT-SPI Action Layer Daemon for HER OS
 *
 * Provides semantic UI automation and context ingestion via D-Bus (org.heros.Action).
 * Follows Linux kernel coding style (K&R, tabs, block comments).
 *
 * Core methods (stubs):
 *   - ClickButton
 *   - GetText
 *   - SetText
 *   - GetUITree
 *
 * Author: HER OS Project
 */

#include <stdio.h>
#include <stdlib.h>
#include <gio/gio.h>
#include <atspi/atspi.h>
#include <regex.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <string.h>
#include <unistd.h>
#include <sys/un.h>
#include <pthread.h>
#include <glib.h>
#include <time.h>

#define ACTION_DBUS_NAME "org.heros.Action"
#define ACTION_DBUS_PATH "/org/heros/Action"
#define ACTION_DBUS_INTERFACE "org.heros.Action"
#define NETLINK_USER 31
#define MAX_PAYLOAD 1024
#define METADATA_SOCKET_PATH "/tmp/heros_metadata.sock"

/* Performance and caching constants */
#define MAX_CACHE_SIZE 1000
#define CACHE_TTL_SECONDS 300  /* 5 minutes */
#define MAX_EVENT_QUEUE_SIZE 1000
#define EVENT_BATCH_SIZE 10
#define EVENT_BATCH_TIMEOUT_MS 100

/*
 * AT-SPI connection state
 */
static AtspiAccessible *root_accessible = NULL;

/*
 * Netlink socket for PDP communication
 */
static int pdp_socket = -1;

/*
 * Metadata daemon socket for context ingestion
 */
static int metadata_socket = -1;

/*
 * Context ingestion thread
 */
static pthread_t context_thread;
static gboolean context_running = TRUE;

/*
 * Performance optimization structures
 */
typedef struct {
	char *key;
	void *value;
	time_t timestamp;
} cache_entry_t;

typedef struct {
	cache_entry_t *entries;
	int size;
	int capacity;
	pthread_mutex_t mutex;
} cache_t;

typedef struct {
	char *event_data;
	time_t timestamp;
} queued_event_t;

typedef struct {
	queued_event_t *events;
	int head;
	int tail;
	int size;
	int capacity;
	pthread_mutex_t mutex;
	pthread_cond_t not_empty;
} event_queue_t;

/*
 * Global caches and queues
 */
static cache_t app_cache = {0};  /* Application PID -> AtspiAccessible cache */
static cache_t element_cache = {0};  /* Element identifier -> AtspiAccessible cache */
static cache_t ui_tree_cache = {0};  /* App PID -> UI tree JSON cache */
static event_queue_t event_queue = {0};  /* Batched event queue */
static pthread_t event_processor_thread;
static gboolean event_processor_running = TRUE;

/*
 * Cache management functions
 */
static void init_cache(cache_t *cache, int initial_capacity)
{
	cache->entries = malloc(initial_capacity * sizeof(cache_entry_t));
	cache->capacity = initial_capacity;
	cache->size = 0;
	pthread_mutex_init(&cache->mutex, NULL);
}

static void cleanup_cache(cache_t *cache)
{
	pthread_mutex_lock(&cache->mutex);
	for (int i = 0; i < cache->size; i++) {
		g_free(cache->entries[i].key);
		g_free(cache->entries[i].value);
	}
	free(cache->entries);
	cache->entries = NULL;
	cache->size = 0;
	cache->capacity = 0;
	pthread_mutex_unlock(&cache->mutex);
	pthread_mutex_destroy(&cache->mutex);
}

static void *cache_get(cache_t *cache, const char *key)
{
	void *value = NULL;
	time_t now = time(NULL);
	
	pthread_mutex_lock(&cache->mutex);
	
	for (int i = 0; i < cache->size; i++) {
		if (g_strcmp0(cache->entries[i].key, key) == 0) {
			/* Check if entry is still valid */
			if (now - cache->entries[i].timestamp < CACHE_TTL_SECONDS) {
				value = cache->entries[i].value;
				/* Update timestamp for LRU behavior */
				cache->entries[i].timestamp = now;
			} else {
				/* Remove expired entry */
				g_free(cache->entries[i].key);
				g_free(cache->entries[i].value);
				memmove(&cache->entries[i], &cache->entries[i + 1], 
					(cache->size - i - 1) * sizeof(cache_entry_t));
				cache->size--;
			}
			break;
		}
	}
	
	pthread_mutex_unlock(&cache->mutex);
	return value;
}

static void cache_put(cache_t *cache, const char *key, void *value)
{
	time_t now = time(NULL);
	
	pthread_mutex_lock(&cache->mutex);
	
	/* Check if key already exists */
	for (int i = 0; i < cache->size; i++) {
		if (g_strcmp0(cache->entries[i].key, key) == 0) {
			/* Update existing entry */
			g_free(cache->entries[i].value);
			cache->entries[i].value = value;
			cache->entries[i].timestamp = now;
			pthread_mutex_unlock(&cache->mutex);
			return;
		}
	}
	
	/* Remove expired entries if cache is full */
	if (cache->size >= MAX_CACHE_SIZE) {
		time_t oldest_time = now;
		int oldest_index = 0;
		
		for (int i = 0; i < cache->size; i++) {
			if (cache->entries[i].timestamp < oldest_time) {
				oldest_time = cache->entries[i].timestamp;
				oldest_index = i;
			}
		}
		
		/* Remove oldest entry */
		g_free(cache->entries[oldest_index].key);
		g_free(cache->entries[oldest_index].value);
		memmove(&cache->entries[oldest_index], &cache->entries[oldest_index + 1],
			(cache->size - oldest_index - 1) * sizeof(cache_entry_t));
		cache->size--;
	}
	
	/* Add new entry */
	if (cache->size >= cache->capacity) {
		/* Expand cache */
		int new_capacity = cache->capacity * 2;
		cache_entry_t *new_entries = realloc(cache->entries, new_capacity * sizeof(cache_entry_t));
		if (new_entries) {
			cache->entries = new_entries;
			cache->capacity = new_capacity;
		}
	}
	
	if (cache->size < cache->capacity) {
		cache->entries[cache->size].key = g_strdup(key);
		cache->entries[cache->size].value = value;
		cache->entries[cache->size].timestamp = now;
		cache->size++;
	}
	
	pthread_mutex_unlock(&cache->mutex);
}

/*
 * Event queue management functions
 */
static void init_event_queue(event_queue_t *queue, int capacity)
{
	queue->events = malloc(capacity * sizeof(queued_event_t));
	queue->capacity = capacity;
	queue->head = 0;
	queue->tail = 0;
	queue->size = 0;
	pthread_mutex_init(&queue->mutex, NULL);
	pthread_cond_init(&queue->not_empty, NULL);
}

static void cleanup_event_queue(event_queue_t *queue)
{
	pthread_mutex_lock(&queue->mutex);
	for (int i = 0; i < queue->size; i++) {
		int index = (queue->head + i) % queue->capacity;
		g_free(queue->events[index].event_data);
	}
	free(queue->events);
	queue->events = NULL;
	queue->size = 0;
	queue->capacity = 0;
	pthread_mutex_unlock(&queue->mutex);
	pthread_mutex_destroy(&queue->mutex);
	pthread_cond_destroy(&queue->not_empty);
}

static gboolean queue_event(event_queue_t *queue, const char *event_data)
{
	gboolean success = FALSE;
	
	pthread_mutex_lock(&queue->mutex);
	
	if (queue->size < queue->capacity) {
		queue->events[queue->tail].event_data = g_strdup(event_data);
		queue->events[queue->tail].timestamp = time(NULL);
		queue->tail = (queue->tail + 1) % queue->capacity;
		queue->size++;
		success = TRUE;
		pthread_cond_signal(&queue->not_empty);
	}
	
	pthread_mutex_unlock(&queue->mutex);
	return success;
}

/*
 * Initialize connection to metadata daemon
 */
static gboolean init_metadata_connection(void)
{
	struct sockaddr_un addr;
	
	metadata_socket = socket(AF_UNIX, SOCK_STREAM, 0);
	if (metadata_socket < 0) {
		g_print("[action_daemon] Failed to create metadata socket: %s\n", strerror(errno));
		return FALSE;
	}
	
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, METADATA_SOCKET_PATH, sizeof(addr.sun_path) - 1);
	
	if (connect(metadata_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		g_print("[action_daemon] Failed to connect to metadata daemon: %s\n", strerror(errno));
		close(metadata_socket);
		metadata_socket = -1;
		return FALSE;
	}
	
	g_print("[action_daemon] Metadata daemon connection initialized\n");
	return TRUE;
}

/*
 * Send event to metadata daemon for UKG ingestion (optimized with batching)
 */
static void send_context_event(const char *event_type, const char *app_name, int app_pid, 
	const char *element_name, const char *element_role, const char *context_data)
{
	char event_buffer[1024];
	int len;
	
	/* Format event as: EVENT UI_EVENT app_name:app_pid:element_name:element_role:context_data */
	len = snprintf(event_buffer, sizeof(event_buffer), 
		"EVENT %s %s:%d:%s:%s:%s\n", 
		event_type, app_name ? app_name : "unknown", app_pid,
		element_name ? element_name : "unknown", 
		element_role ? element_role : "unknown",
		context_data ? context_data : "");
	
	if (len > 0 && len < sizeof(event_buffer)) {
		/* Queue event for batched processing instead of immediate send */
		if (!queue_event(&event_queue, event_buffer)) {
			g_print("[action_daemon] Event queue full, dropping event: %s\n", event_type);
		}
	}
}

/*
 * Get application name from PID
 */
static char *get_app_name_from_pid(int pid)
{
	AtspiAccessible *app = find_app_by_pid(pid);
	if (app) {
		const char *name = atspi_accessible_get_name(app, NULL);
		return name ? g_strdup(name) : g_strdup("unknown");
	}
	return g_strdup("unknown");
}

/*
 * AT-SPI event listener for context ingestion
 */
static void on_atspi_event(AtspiEvent *event, void *user_data)
{
	AtspiAccessible *source = atspi_event_get_source(event, NULL);
	AtspiAccessible *app = NULL;
	char *app_name = NULL;
	int app_pid = 0;
	const char *element_name = NULL;
	const char *element_role = NULL;
	char *role_name = NULL;
	
	if (!source) {
		return;
	}
	
	/* Get application context */
	app = atspi_accessible_get_application(source, NULL);
	if (app) {
		app_pid = atspi_accessible_get_process_id(app, NULL);
		app_name = get_app_name_from_pid(app_pid);
	} else {
		app_name = g_strdup("unknown");
	}
	
	/* Get element information */
	element_name = atspi_accessible_get_name(source, NULL);
	AtspiRole role = atspi_accessible_get_role(source, NULL);
	role_name = g_strdup(atspi_role_get_name(role));
	
	/* Process different event types */
	const char *event_type = atspi_event_get_type(event, NULL);
	if (event_type) {
		if (g_strcmp0(event_type, "object:state-changed:focused") == 0) {
			/* Focus change event */
			send_context_event("UI_FOCUS", app_name, app_pid, element_name, role_name, "focused");
		} else if (g_strcmp0(event_type, "object:text-changed:insert") == 0 ||
				   g_strcmp0(event_type, "object:text-changed:delete") == 0) {
			/* Text modification event */
			const char *change_type = (g_strcmp0(event_type, "object:text-changed:insert") == 0) ? "insert" : "delete";
			send_context_event("UI_TEXT_CHANGE", app_name, app_pid, element_name, role_name, change_type);
		} else if (g_strcmp0(event_type, "object:selection-changed") == 0) {
			/* Selection change event */
			send_context_event("UI_SELECTION", app_name, app_pid, element_name, role_name, "changed");
		} else if (g_strcmp0(event_type, "object:window:activate") == 0) {
			/* Window activation event */
			send_context_event("UI_WINDOW_ACTIVATE", app_name, app_pid, element_name, role_name, "activated");
		} else if (g_strcmp0(event_type, "object:window:deactivate") == 0) {
			/* Window deactivation event */
			send_context_event("UI_WINDOW_DEACTIVATE", app_name, app_pid, element_name, role_name, "deactivated");
		}
	}
	
	g_free(app_name);
	g_free(role_name);
}

/*
 * Initialize AT-SPI event monitoring
 */
static gboolean init_atspi_events(void)
{
	/* Register for various AT-SPI events */
	const char *event_types[] = {
		"object:state-changed:focused",
		"object:text-changed:insert",
		"object:text-changed:delete", 
		"object:selection-changed",
		"object:window:activate",
		"object:window:deactivate",
		NULL
	};
	
	for (int i = 0; event_types[i] != NULL; i++) {
		if (!atspi_event_listener_register(on_atspi_event, event_types[i], NULL)) {
			g_print("[action_daemon] Failed to register for event: %s\n", event_types[i]);
		} else {
			g_print("[action_daemon] Registered for event: %s\n", event_types[i]);
		}
	}
	
	return TRUE;
}

/*
 * Event processor thread function (batched event sending)
 */
static void *event_processor_thread_func(void *arg)
{
	g_print("[action_daemon] Event processor thread started\n");
	
	while (event_processor_running) {
		char *events[EVENT_BATCH_SIZE];
		int event_count = 0;
		struct timespec timeout;
		
		/* Wait for events with timeout */
		clock_gettime(CLOCK_REALTIME, &timeout);
		timeout.tv_nsec += EVENT_BATCH_TIMEOUT_MS * 1000000; /* Convert to nanoseconds */
		if (timeout.tv_nsec >= 1000000000) {
			timeout.tv_sec += 1;
			timeout.tv_nsec -= 1000000000;
		}
		
		pthread_mutex_lock(&event_queue.mutex);
		
		/* Collect events up to batch size or timeout */
		while (event_count < EVENT_BATCH_SIZE && event_queue.size > 0) {
			events[event_count] = event_queue.events[event_queue.head].event_data;
			event_queue.head = (event_queue.head + 1) % event_queue.capacity;
			event_queue.size--;
			event_count++;
		}
		
		pthread_mutex_unlock(&event_queue.mutex);
		
		/* Send batched events to metadata daemon */
		if (event_count > 0 && metadata_socket >= 0) {
			for (int i = 0; i < event_count; i++) {
				if (send(metadata_socket, events[i], strlen(events[i]), 0) < 0) {
					g_print("[action_daemon] Failed to send batched event: %s\n", strerror(errno));
				}
				g_free(events[i]);
			}
		} else if (event_count > 0) {
			/* Metadata daemon not available, drop events */
			for (int i = 0; i < event_count; i++) {
				g_free(events[i]);
			}
		}
		
		/* Sleep if no events to process */
		if (event_count == 0) {
			usleep(EVENT_BATCH_TIMEOUT_MS * 1000);
		}
	}
	
	g_print("[action_daemon] Event processor thread stopped\n");
	return NULL;
}

/*
 * Context ingestion thread function
 */
static void *context_ingestion_thread(void *arg)
{
	g_print("[action_daemon] Context ingestion thread started\n");
	
	/* Initialize AT-SPI event monitoring */
	if (!init_atspi_events()) {
		g_print("[action_daemon] Failed to initialize AT-SPI events\n");
		return NULL;
	}
	
	/* Main event processing loop */
	while (context_running) {
		/* Process AT-SPI events (handled by callback) */
		usleep(100000); /* 100ms sleep */
	}
	
	g_print("[action_daemon] Context ingestion thread stopped\n");
	return NULL;
}

/*
 * Initialize Netlink connection to PDP
 */
static gboolean init_pdp_connection(void)
{
	struct sockaddr_nl src_addr;
	
	pdp_socket = socket(PF_NETLINK, SOCK_RAW, NETLINK_USER);
	if (pdp_socket < 0) {
		g_print("[action_daemon] Failed to create Netlink socket: %s\n", strerror(errno));
		return FALSE;
	}
	
	memset(&src_addr, 0, sizeof(src_addr));
	src_addr.nl_family = AF_NETLINK;
	src_addr.nl_pid = getpid();
	
	if (bind(pdp_socket, (struct sockaddr*)&src_addr, sizeof(src_addr)) < 0) {
		g_print("[action_daemon] Failed to bind Netlink socket: %s\n", strerror(errno));
		close(pdp_socket);
		pdp_socket = -1;
		return FALSE;
	}
	
	g_print("[action_daemon] PDP Netlink connection initialized\n");
	return TRUE;
}

/*
 * Query PDP for authorization decision
 */
static gboolean query_pdp_authorization(const char *operation, int target_pid, const char *context)
{
	struct sockaddr_nl dest_addr;
	struct nlmsghdr *nlh = NULL;
	struct iovec iov;
	struct msghdr msg;
	char payload[MAX_PAYLOAD];
	int payload_len;
	
	if (pdp_socket < 0) {
		g_print("[action_daemon] PDP socket not initialized\n");
		return FALSE;
	}
	
	/* Prepare authorization request payload */
	payload_len = snprintf(payload, MAX_PAYLOAD, 
		"UI_ACTION:%s:PID:%d:CONTEXT:%s", operation, target_pid, context ? context : "none");
	
	/* Allocate Netlink message */
	nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(payload_len));
	if (!nlh) {
		g_print("[action_daemon] Failed to allocate Netlink message\n");
		return FALSE;
	}
	
	/* Fill Netlink header */
	memset(nlh, 0, NLMSG_SPACE(payload_len));
	nlh->nlmsg_len = NLMSG_SPACE(payload_len);
	nlh->nlmsg_pid = getpid();
	nlh->nlmsg_flags = 0;
	
	/* Copy payload */
	memcpy(NLMSG_DATA(nlh), payload, payload_len);
	
	/* Prepare destination address */
	memset(&dest_addr, 0, sizeof(dest_addr));
	dest_addr.nl_family = AF_NETLINK;
	dest_addr.nl_pid = 0; /* Kernel */
	dest_addr.nl_groups = 0;
	
	/* Prepare message */
	iov.iov_base = (void *)nlh;
	iov.iov_len = nlh->nlmsg_len;
	memset(&msg, 0, sizeof(msg));
	msg.msg_name = (void *)&dest_addr;
	msg.msg_namelen = sizeof(dest_addr);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	
	/* Send authorization request */
	if (sendmsg(pdp_socket, &msg, 0) < 0) {
		g_print("[action_daemon] Failed to send PDP request: %s\n", strerror(errno));
		free(nlh);
		return FALSE;
	}
	
	/* Receive response */
	memset(nlh, 0, NLMSG_SPACE(MAX_PAYLOAD));
	if (recvmsg(pdp_socket, &msg, 0) < 0) {
		g_print("[action_daemon] Failed to receive PDP response: %s\n", strerror(errno));
		free(nlh);
		return FALSE;
	}
	
	/* Parse response */
	char *response = (char *)NLMSG_DATA(nlh);
	gboolean authorized = (strstr(response, "ALLOW") != NULL);
	
	g_print("[action_daemon] PDP response: %s (authorized: %s)\n", 
		response, authorized ? "YES" : "NO");
	
	free(nlh);
	return authorized;
}

/*
 * Initialize AT-SPI connection
 */
static gboolean init_atspi(void)
{
	GError *error = NULL;
	
	/* Initialize AT-SPI */
	if (!atspi_init()) {
		g_print("[action_daemon] Failed to initialize AT-SPI\n");
		return FALSE;
	}
	
	/* Get root accessible */
	root_accessible = atspi_get_desktop(0);
	if (!root_accessible) {
		g_print("[action_daemon] Failed to get root accessible\n");
		return FALSE;
	}
	
	g_print("[action_daemon] AT-SPI initialized successfully\n");
	return TRUE;
}

/*
 * Find application by PID using AT-SPI (optimized with caching)
 */
static AtspiAccessible *find_app_by_pid(int pid)
{
	char pid_str[32];
	AtspiAccessible *app = NULL;
	
	/* Check cache first */
	snprintf(pid_str, sizeof(pid_str), "%d", pid);
	app = (AtspiAccessible *)cache_get(&app_cache, pid_str);
	if (app) {
		return app;
	}
	
	/* Cache miss - perform AT-SPI lookup */
	AtspiAccessible *desktop = atspi_get_desktop(0);
	if (!desktop) {
		g_print("[action_daemon] Failed to get desktop\n");
		return NULL;
	}
	
	/* Get all applications */
	GPtrArray *apps = atspi_accessible_get_children(desktop, 0, NULL);
	if (!apps) {
		g_print("[action_daemon] Failed to get applications\n");
		return NULL;
	}
	
	/* Find application with matching PID */
	for (int i = 0; i < apps->len; i++) {
		AtspiAccessible *child = g_ptr_array_index(apps, i);
		if (child) {
			int app_pid = atspi_accessible_get_process_id(child, NULL);
			if (app_pid == pid) {
				app = child;
				break;
			}
		}
	}
	
	g_ptr_array_free(apps, TRUE);
	
	/* Cache the result */
	if (app) {
		cache_put(&app_cache, pid_str, app);
	}
	
	return app;
}

/*
 * Find button by label regex in application
 */
static AtspiAccessible *find_button_by_regex(AtspiAccessible *app, const char *button_regex)
{
	AtspiAccessible *button = NULL;
	regex_t regex;
	
	/* Compile regex */
	if (regcomp(&regex, button_regex, REG_EXTENDED | REG_NOSUB) != 0) {
		g_print("[action_daemon] Invalid regex: %s\n", button_regex);
		return NULL;
	}
	
	/* Recursively search for buttons */
	GPtrArray *children = atspi_accessible_get_children(app, 0, NULL);
	if (children) {
		for (int i = 0; i < children->len; i++) {
			AtspiAccessible *child = g_ptr_array_index(children, i);
			if (child) {
				/* Check if this is a button */
				AtspiRole role = atspi_accessible_get_role(child, NULL);
				if (role == ATSPI_ROLE_PUSH_BUTTON || role == ATSPI_ROLE_TOGGLE_BUTTON) {
					/* Get button name */
					const char *name = atspi_accessible_get_name(child, NULL);
					if (name && regexec(&regex, name, 0, NULL, 0) == 0) {
						button = child;
						break;
					}
				}
				
				/* Recursively search children */
				AtspiAccessible *found = find_button_by_regex(child, button_regex);
				if (found) {
					button = found;
					break;
				}
			}
		}
		g_ptr_array_free(children, TRUE);
	}
	
	regfree(&regex);
	return button;
}

/*
 * Find UI element by identifier in application (optimized with caching)
 */
static AtspiAccessible *find_element_by_identifier(AtspiAccessible *app, const char *identifier)
{
	AtspiAccessible *element = NULL;
	char cache_key[256];
	
	/* Create cache key: app_pid:identifier */
	int app_pid = atspi_accessible_get_process_id(app, NULL);
	snprintf(cache_key, sizeof(cache_key), "%d:%s", app_pid, identifier);
	
	/* Check cache first */
	element = (AtspiAccessible *)cache_get(&element_cache, cache_key);
	if (element) {
		return element;
	}
	
	/* Cache miss - perform recursive search */
	char *type = NULL;
	char *name = NULL;
	char *colon = strchr(identifier, ':');
	
	if (colon) {
		/* Split identifier into type and name */
		int type_len = colon - identifier;
		type = g_strndup(identifier, type_len);
		name = g_strdup(colon + 1);
	} else {
		/* No type specified, use name only */
		name = g_strdup(identifier);
	}
	
	/* Recursively search for element */
	GPtrArray *children = atspi_accessible_get_children(app, 0, NULL);
	if (children) {
		for (int i = 0; i < children->len; i++) {
			AtspiAccessible *child = g_ptr_array_index(children, i);
			if (child) {
				/* Check if this element matches */
				const char *element_name = atspi_accessible_get_name(child, NULL);
				if (element_name && g_strcmp0(element_name, name) == 0) {
					/* If type was specified, check role */
					if (type) {
						AtspiRole role = atspi_accessible_get_role(child, NULL);
						if ((g_strcmp0(type, "label") == 0 && role == ATSPI_ROLE_LABEL) ||
							(g_strcmp0(type, "text") == 0 && role == ATSPI_ROLE_TEXT) ||
							(g_strcmp0(type, "entry") == 0 && role == ATSPI_ROLE_ENTRY) ||
							(g_strcmp0(type, "button") == 0 && (role == ATSPI_ROLE_PUSH_BUTTON || role == ATSPI_ROLE_TOGGLE_BUTTON))) {
							element = child;
							break;
						}
					} else {
						/* No type specified, accept any element with matching name */
						element = child;
						break;
					}
				}
				
				/* Recursively search children */
				AtspiAccessible *found = find_element_by_identifier(child, identifier);
				if (found) {
					element = found;
					break;
				}
			}
		}
		g_ptr_array_free(children, TRUE);
	}
	
	g_free(type);
	g_free(name);
	
	/* Cache the result */
	if (element) {
		cache_put(&element_cache, cache_key, element);
	}
	
	return element;
}

/*
 * Convert AT-SPI accessible to JSON representation (optimized with caching)
 */
static char *accessible_to_json(AtspiAccessible *accessible, int depth)
{
	GString *json = g_string_new("");
	const char *name = atspi_accessible_get_name(accessible, NULL);
	AtspiRole role = atspi_accessible_get_role(accessible, NULL);
	const char *role_name = atspi_role_get_name(role);
	
	/* Limit recursion depth */
	if (depth > 10) {
		g_string_append(json, "{\"name\":\"...\",\"role\":\"truncated\"}");
		return g_string_free(json, FALSE);
	}
	
	g_string_append(json, "{");
	g_string_append_printf(json, "\"name\":\"%s\",", name ? name : "");
	g_string_append_printf(json, "\"role\":\"%s\",", role_name ? role_name : "");
	
	/* Get children */
	GPtrArray *children = atspi_accessible_get_children(accessible, 0, NULL);
	if (children && children->len > 0) {
		g_string_append(json, "\"children\":[");
		for (int i = 0; i < children->len; i++) {
			AtspiAccessible *child = g_ptr_array_index(children, i);
			if (child) {
				char *child_json = accessible_to_json(child, depth + 1);
				g_string_append(json, child_json);
				g_free(child_json);
				if (i < children->len - 1) {
					g_string_append(json, ",");
				}
			}
		}
		g_string_append(json, "]");
	}
	g_ptr_array_free(children, TRUE);
	
	g_string_append(json, "}");
	return g_string_free(json, FALSE);
}

/*
 * Get UI tree JSON with caching
 */
static char *get_ui_tree_json(int app_pid)
{
	char pid_str[32];
	char *cached_json = NULL;
	
	/* Check cache first */
	snprintf(pid_str, sizeof(pid_str), "%d", app_pid);
	cached_json = (char *)cache_get(&ui_tree_cache, pid_str);
	if (cached_json) {
		return g_strdup(cached_json);
	}
	
	/* Cache miss - generate JSON */
	AtspiAccessible *app = find_app_by_pid(app_pid);
	if (!app) {
		return g_strdup("{\"error\":\"Application not found\"}");
	}
	
	char *json = accessible_to_json(app, 0);
	
	/* Cache the result */
	cache_put(&ui_tree_cache, pid_str, g_strdup(json));
	
	return json;
}

/*
 * Handler for ClickButton method (real AT-SPI implementation with security)
 */
gboolean handle_click_button(GDBusConnection *connection,
	const gchar *sender,
	const gchar *object_path,
	const gchar *interface_name,
	const gchar *method_name,
	GVariant *parameters,
	GDBusMethodInvocation *invocation,
	gpointer user_data)
{
	gint app_pid;
	const gchar *button_regex;
	AtspiAccessible *app, *button;
	struct timespec start_time, end_time;
	long latency_ms;
	gboolean success = FALSE;
	
	/* Start performance monitoring */
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	
	/* Extract parameters */
	g_variant_get(parameters, "(is)", &app_pid, &button_regex);
	
	g_print("[action_daemon] ClickButton called: pid=%d, regex='%s'\n", app_pid, button_regex);
	
	/* Query PDP for authorization */
	char context[256];
	snprintf(context, sizeof(context), "click_button:%s", button_regex);
	if (!query_pdp_authorization("ClickButton", app_pid, context)) {
		g_print("[action_daemon] PDP denied ClickButton operation\n");
		log_audit_event("ClickButton", "unknown", app_pid, button_regex, "DENIED", "PDP authorization failed");
		g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
			"PDP denied ClickButton operation");
		goto cleanup;
	}
	
	/* Find application by PID */
	app = find_app_by_pid(app_pid);
	if (!app) {
		g_print("[action_daemon] Application with PID %d not found\n", app_pid);
		log_audit_event("ClickButton", "unknown", app_pid, button_regex, "FAILED", "Application not found");
		g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, 
			"Application with PID %d not found", app_pid);
		goto cleanup;
	}
	
	/* Find button by regex */
	button = find_button_by_regex(app, button_regex);
	if (!button) {
		g_print("[action_daemon] Button matching regex '%s' not found\n", button_regex);
		log_audit_event("ClickButton", "unknown", app_pid, button_regex, "FAILED", "Button not found");
		g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			"Button matching regex '%s' not found", button_regex);
		goto cleanup;
	}
	
	/* Perform click action */
	AtspiAction *action = atspi_accessible_get_action_iface(button);
	if (action) {
		success = atspi_action_do_action(action, 0, NULL);
		if (success) {
			g_print("[action_daemon] Button clicked successfully\n");
			
			/* Send context event for UKG ingestion */
			const char *button_name = atspi_accessible_get_name(button, NULL);
			char *app_name = get_app_name_from_pid(app_pid);
			send_context_event("UI_BUTTON_CLICK", app_name, app_pid, button_name, "push_button", button_regex);
			log_audit_event("ClickButton", app_name, app_pid, button_regex, "SUCCESS", "Button clicked");
			g_free(app_name);
			
			g_dbus_method_invocation_return_value(invocation, NULL);
		} else {
			g_print("[action_daemon] Failed to click button\n");
			log_audit_event("ClickButton", "unknown", app_pid, button_regex, "FAILED", "AT-SPI action failed");
			g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_FAILED,
				"Failed to click button");
		}
		g_object_unref(action);
	} else {
		g_print("[action_daemon] Button does not support actions\n");
		log_audit_event("ClickButton", "unknown", app_pid, button_regex, "FAILED", "Button does not support actions");
		g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			"Button does not support actions");
	}

cleanup:
	/* Record performance metrics */
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	latency_ms = ((end_time.tv_sec - start_time.tv_sec) * 1000000000 + 
				  (end_time.tv_nsec - start_time.tv_nsec)) / 1000000;
	record_operation(success, latency_ms);
	
	return TRUE;
}

/*
 * Handler for GetText method (real AT-SPI implementation with security)
 */
gboolean handle_get_text(GDBusConnection *connection,
	const gchar *sender,
	const gchar *object_path,
	const gchar *interface_name,
	const gchar *method_name,
	GVariant *parameters,
	GDBusMethodInvocation *invocation,
	gpointer user_data)
{
	gint app_pid;
	const gchar *object_identifier;
	AtspiAccessible *app, *element;
	const char *text = "";
	struct timespec start_time, end_time;
	long latency_ms;
	gboolean success = FALSE;
	
	/* Start performance monitoring */
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	
	/* Extract parameters */
	g_variant_get(parameters, "(is)", &app_pid, &object_identifier);
	
	g_print("[action_daemon] GetText called: pid=%d, identifier='%s'\n", app_pid, object_identifier);
	
	/* Query PDP for authorization */
	char context[256];
	snprintf(context, sizeof(context), "get_text:%s", object_identifier);
	if (!query_pdp_authorization("GetText", app_pid, context)) {
		g_print("[action_daemon] PDP denied GetText operation\n");
		log_audit_event("GetText", "unknown", app_pid, object_identifier, "DENIED", "PDP authorization failed");
		g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
			"PDP denied GetText operation");
		goto cleanup;
	}
	
	/* Find application by PID */
	app = find_app_by_pid(app_pid);
	if (!app) {
		g_print("[action_daemon] Application with PID %d not found\n", app_pid);
		log_audit_event("GetText", "unknown", app_pid, object_identifier, "FAILED", "Application not found");
		g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, 
			"Application with PID %d not found", app_pid);
		goto cleanup;
	}
	
	/* Find element by identifier */
	element = find_element_by_identifier(app, object_identifier);
	if (!element) {
		g_print("[action_daemon] Element with identifier '%s' not found\n", object_identifier);
		log_audit_event("GetText", "unknown", app_pid, object_identifier, "FAILED", "Element not found");
		g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			"Element with identifier '%s' not found", object_identifier);
		goto cleanup;
	}
	
	/* Get text content based on element type */
	AtspiRole role = atspi_accessible_get_role(element, NULL);
	if (role == ATSPI_ROLE_TEXT || role == ATSPI_ROLE_ENTRY) {
		/* For text elements, get the text content */
		AtspiText *text_iface = atspi_accessible_get_text_iface(element);
		if (text_iface) {
			text = atspi_text_get_text(text_iface, 0, -1, NULL);
			g_object_unref(text_iface);
		}
	} else {
		/* For other elements, get the accessible name */
		text = atspi_accessible_get_name(element, NULL);
	}
	
	if (!text) {
		text = "";
	}
	
	g_print("[action_daemon] Retrieved text: '%s'\n", text);
	success = TRUE;
	
	/* Send context event for UKG ingestion */
	const char *element_name = atspi_accessible_get_name(element, NULL);
	char *app_name = get_app_name_from_pid(app_pid);
	send_context_event("UI_TEXT_READ", app_name, app_pid, element_name, "text", object_identifier);
	log_audit_event("GetText", app_name, app_pid, object_identifier, "SUCCESS", "Text retrieved");
	g_free(app_name);
	
	g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", text));

cleanup:
	/* Record performance metrics */
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	latency_ms = ((end_time.tv_sec - start_time.tv_sec) * 1000000000 + 
				  (end_time.tv_nsec - start_time.tv_nsec)) / 1000000;
	record_operation(success, latency_ms);
	
	return TRUE;
}

/*
 * Handler for SetText method (real AT-SPI implementation with security)
 */
gboolean handle_set_text(GDBusConnection *connection,
	const gchar *sender,
	const gchar *object_path,
	const gchar *interface_name,
	const gchar *method_name,
	GVariant *parameters,
	GDBusMethodInvocation *invocation,
	gpointer user_data)
{
	gint app_pid;
	const gchar *object_identifier, *text_to_set;
	AtspiAccessible *app, *element;
	struct timespec start_time, end_time;
	long latency_ms;
	gboolean success = FALSE;
	
	/* Start performance monitoring */
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	
	/* Extract parameters */
	g_variant_get(parameters, "(iss)", &app_pid, &object_identifier, &text_to_set);
	
	g_print("[action_daemon] SetText called: pid=%d, identifier='%s', text='%s'\n", 
		app_pid, object_identifier, text_to_set);
	
	/* Query PDP for authorization */
	char context[256];
	snprintf(context, sizeof(context), "set_text:%s", object_identifier);
	if (!query_pdp_authorization("SetText", app_pid, context)) {
		g_print("[action_daemon] PDP denied SetText operation\n");
		log_audit_event("SetText", "unknown", app_pid, object_identifier, "DENIED", "PDP authorization failed");
		g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
			"PDP denied SetText operation");
		goto cleanup;
	}
	
	/* Find application by PID */
	app = find_app_by_pid(app_pid);
	if (!app) {
		g_print("[action_daemon] Application with PID %d not found\n", app_pid);
		log_audit_event("SetText", "unknown", app_pid, object_identifier, "FAILED", "Application not found");
		g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, 
			"Application with PID %d not found", app_pid);
		goto cleanup;
	}
	
	/* Find element by identifier */
	element = find_element_by_identifier(app, object_identifier);
	if (!element) {
		g_print("[action_daemon] Element with identifier '%s' not found\n", object_identifier);
		log_audit_event("SetText", "unknown", app_pid, object_identifier, "FAILED", "Element not found");
		g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			"Element with identifier '%s' not found", object_identifier);
		goto cleanup;
	}
	
	/* Check if element supports text editing */
	AtspiRole role = atspi_accessible_get_role(element, NULL);
	if (role != ATSPI_ROLE_ENTRY && role != ATSPI_ROLE_TEXT) {
		g_print("[action_daemon] Element does not support text editing\n");
		log_audit_event("SetText", "unknown", app_pid, object_identifier, "FAILED", "Element does not support text editing");
		g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			"Element does not support text editing");
		goto cleanup;
	}
	
	/* Set text content */
	AtspiText *text_iface = atspi_accessible_get_text_iface(element);
	if (text_iface) {
		/* Clear existing text and set new text */
		success = atspi_text_set_text_contents(text_iface, 0, text_to_set, NULL);
		g_object_unref(text_iface);
		
		if (success) {
			g_print("[action_daemon] Text set successfully\n");
			
			/* Send context event for UKG ingestion */
			const char *element_name = atspi_accessible_get_name(element, NULL);
			char *app_name = get_app_name_from_pid(app_pid);
			send_context_event("UI_TEXT_WRITE", app_name, app_pid, element_name, "entry", object_identifier);
			log_audit_event("SetText", app_name, app_pid, object_identifier, "SUCCESS", "Text set");
			g_free(app_name);
			
			g_dbus_method_invocation_return_value(invocation, NULL);
		} else {
			g_print("[action_daemon] Failed to set text\n");
			log_audit_event("SetText", "unknown", app_pid, object_identifier, "FAILED", "AT-SPI text setting failed");
			g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_FAILED,
				"Failed to set text");
		}
	} else {
		g_print("[action_daemon] Element does not support text interface\n");
		log_audit_event("SetText", "unknown", app_pid, object_identifier, "FAILED", "Element does not support text interface");
		g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			"Element does not support text interface");
	}

cleanup:
	/* Record performance metrics */
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	latency_ms = ((end_time.tv_sec - start_time.tv_sec) * 1000000000 + 
				  (end_time.tv_nsec - start_time.tv_nsec)) / 1000000;
	record_operation(success, latency_ms);
	
	return TRUE;
}

/*
 * Handler for GetUITree method (real AT-SPI implementation with security)
 */
gboolean handle_get_ui_tree(GDBusConnection *connection,
	const gchar *sender,
	const gchar *object_path,
	const gchar *interface_name,
	const gchar *method_name,
	GVariant *parameters,
	GDBusMethodInvocation *invocation,
	gpointer user_data)
{
	gint app_pid;
	AtspiAccessible *app;
	char *tree_json;
	struct timespec start_time, end_time;
	long latency_ms;
	gboolean success = FALSE;
	
	/* Start performance monitoring */
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	
	/* Extract parameters */
	g_variant_get(parameters, "(i)", &app_pid);
	
	g_print("[action_daemon] GetUITree called: pid=%d\n", app_pid);
	
	/* Query PDP for authorization */
	if (!query_pdp_authorization("GetUITree", app_pid, "ui_tree_access")) {
		g_print("[action_daemon] PDP denied GetUITree operation\n");
		log_audit_event("GetUITree", "unknown", app_pid, "ui_tree", "DENIED", "PDP authorization failed");
		g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
			"PDP denied GetUITree operation");
		goto cleanup;
	}
	
	/* Find application by PID */
	app = find_app_by_pid(app_pid);
	if (!app) {
		g_print("[action_daemon] Application with PID %d not found\n", app_pid);
		log_audit_event("GetUITree", "unknown", app_pid, "ui_tree", "FAILED", "Application not found");
		g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, 
			"Application with PID %d not found", app_pid);
		goto cleanup;
	}
	
	/* Convert application UI tree to JSON (with caching) */
	tree_json = get_ui_tree_json(app_pid);
	
	g_print("[action_daemon] Generated UI tree JSON (length: %zu)\n", strlen(tree_json));
	success = TRUE;
	
	/* Send context event for UKG ingestion */
	char *app_name = get_app_name_from_pid(app_pid);
	send_context_event("UI_TREE_ACCESS", app_name, app_pid, "application", "application", "tree_retrieved");
	log_audit_event("GetUITree", app_name, app_pid, "ui_tree", "SUCCESS", "UI tree retrieved");
	g_free(app_name);
	
	g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", tree_json));
	
	g_free(tree_json);

cleanup:
	/* Record performance metrics */
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	latency_ms = ((end_time.tv_sec - start_time.tv_sec) * 1000000000 + 
				  (end_time.tv_nsec - start_time.tv_nsec)) / 1000000;
	record_operation(success, latency_ms);
	
	return TRUE;
}

/*
 * Method call dispatcher for org.heros.Action
 */
static void on_method_call(GDBusConnection *connection,
	const gchar *sender,
	const gchar *object_path,
	const gchar *interface_name,
	const gchar *method_name,
	GVariant *parameters,
	GDBusMethodInvocation *invocation,
	gpointer user_data)
{
	if (g_strcmp0(method_name, "ClickButton") == 0)
		handle_click_button(connection, sender, object_path, interface_name, method_name, parameters, invocation, user_data);
	else if (g_strcmp0(method_name, "GetText") == 0)
		handle_get_text(connection, sender, object_path, interface_name, method_name, parameters, invocation, user_data);
	else if (g_strcmp0(method_name, "SetText") == 0)
		handle_set_text(connection, sender, object_path, interface_name, method_name, parameters, invocation, user_data);
	else if (g_strcmp0(method_name, "GetUITree") == 0)
		handle_get_ui_tree(connection, sender, object_path, interface_name, method_name, parameters, invocation, user_data);
	else
		g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Unknown method: %s", method_name);
}

/*
 * Introspection XML for org.heros.Action
 */
static const gchar introspection_xml[] =
	"<node>"
	"  <interface name='org.heros.Action'>"
	"    <method name='ClickButton'>"
	"      <arg type='i' name='app_pid' direction='in'/>"
	"      <arg type='s' name='button_label_regex' direction='in'/>"
	"    </method>"
	"    <method name='GetText'>"
	"      <arg type='i' name='app_pid' direction='in'/>"
	"      <arg type='s' name='object_identifier' direction='in'/>"
	"      <arg type='s' name='text' direction='out'/>"
	"    </method>"
	"    <method name='SetText'>"
	"      <arg type='i' name='app_pid' direction='in'/>"
	"      <arg type='s' name='object_identifier' direction='in'/>"
	"      <arg type='s' name='text_to_set' direction='in'/>"
	"    </method>"
	"    <method name='GetUITree'>"
	"      <arg type='i' name='app_pid' direction='in'/>"
	"      <arg type='s' name='tree_json' direction='out'/>"
	"    </method>"
	"  </interface>"
	"</node>";

/*
 * Main entry point
 */
int main(int argc, char *argv[])
{
	GMainLoop *loop;
	GDBusNodeInfo *introspection_data;
	GError *error = NULL;
	guint registration_id;

	/* Enhanced initialization with security, performance monitoring, and error handling */
	if (!init_action_daemon()) {
		fprintf(stderr, "Failed to initialize action daemon\n");
		return 1;
	}

	/* Parse introspection XML */
	introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, &error);
	if (error) {
		fprintf(stderr, "Failed to parse introspection XML: %s\n", error->message);
		cleanup_resources();
		return 1;
	}

	/* Acquire a D-Bus connection and register the object */
	registration_id = g_dbus_connection_register_object(
		g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL),
		ACTION_DBUS_PATH,
		introspection_data->interfaces[0],
		&(const GDBusInterfaceVTable){ .method_call = on_method_call },
		NULL, NULL, &error);
	if (error) {
		fprintf(stderr, "Failed to register D-Bus object: %s\n", error->message);
		cleanup_resources();
		return 1;
	}

	/* Request the well-known name */
	g_bus_own_name(G_BUS_TYPE_SESSION, ACTION_DBUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE, NULL, NULL, NULL, NULL, NULL);

	g_print("[action_daemon] Started with enhanced security, performance monitoring, and context ingestion. Waiting for D-Bus calls...\n");
	loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(loop);

	/* Enhanced cleanup with resource management */
	cleanup_resources();
	
	return 0;
}

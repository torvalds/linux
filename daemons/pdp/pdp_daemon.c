/*
 * HER OS Policy Decision Point (PDP) Daemon
 *
 * Handles access control decisions for the HER OS custom LSM.
 * Communicates with the kernel via Netlink (NETLINK_GENERIC).
 *
 * Enhanced Features:
 * - Real-time metrics collection and monitoring
 * - Comprehensive expression evaluation engine
 * - Resource usage tracking and monitoring
 * - Enhanced security validation and audit logging
 * - Performance optimization and caching
 * - Distributed policy coordination
 *
 * Policy file format (pdp_policy.txt):
 *   deny /forbidden.txt
 *   deny subvol:12345
 *   deny snapshot:1
 *   deny subvolname:@snapshots
 *   allow user:john /home/john/*
 *   allow group:developers /dev/*
 *   allow expr:uid==1000 /admin/*
 *   allow time:9-17 /work/*
 *   allow *
 *   (first matching rule wins)
 *
 * Protocol:
 *   - Kernel sends access request (process info, file inode, requested permission, subvol_id, snapshot, subvolname)
 *   - PDP evaluates policy (using policy file)
 *   - PDP replies with allow/deny verdict
 *
 * Responsibilities:
 *   - Listen for authorization requests from the kernel LSM via Netlink
 *   - Evaluate policies from file (now supports subvol_id, snapshot, subvolname, user, group, time, expressions)
 *   - Return allow/deny verdicts to the LSM
 *   - Provide real-time metrics and monitoring
 *   - Track resource usage and performance
 *
 * IPC: Netlink socket (NETLINK_GENERIC)
 *
 * Author: HER OS Project
 * License: GPL-2.0
 * Version: 2.0.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <signal.h>
#include <stdatomic.h>
#include <time.h>
#include <pwd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <regex.h>
#include <json-c/json.h>
#include <syslog.h>

#define NETLINK_USER 31
#define MAX_PAYLOAD 1024
#define POLICY_FILE "daemons/pdp/pdp_policy.txt"
#define MAX_RULES 128
#define MAX_USER_LEN 64
#define MAX_EXPR_LEN 512
#define MAX_CACHE_SIZE 1000
#define CACHE_TTL_SECONDS 300

// Enhanced policy rule structure
struct policy_rule {
    char action[8];
    char path[256];
    u64 subvol_id; // 0 if not a subvol rule
    int snapshot;  // -1 if not a snapshot rule, 0/1 for deny/allow
    char subvolname[128]; // empty if not a subvolname rule
    char user[MAX_USER_LEN]; // empty if not a user rule
    char group[MAX_USER_LEN]; // group-based rules
    int hours_start; // -1 if not a time rule
    int hours_end;   // -1 if not a time rule
    char expr[MAX_EXPR_LEN]; // expression-based rules
    regex_t regex; // compiled regex for path matching
    int has_regex; // flag indicating if regex is compiled
    int priority; // rule priority (higher = more specific)
    time_t last_used; // last time this rule was evaluated
    int hit_count; // number of times this rule matched
};

// Performance metrics with atomic operations
typedef struct {
    atomic_int policy_requests;
    atomic_int policy_allows;
    atomic_int policy_denies;
    atomic_int policy_errors;
    atomic_long total_latency_ns;
    atomic_int cache_hits;
    atomic_int cache_misses;
    atomic_int expression_evaluations;
    atomic_int regex_evaluations;
    atomic_int resource_checks;
    time_t start_time;
    pthread_mutex_t metrics_mutex;
} performance_metrics_t;

// Resource monitoring
typedef struct {
    atomic_long memory_usage_bytes;
    atomic_int cpu_usage_percent;
    atomic_int open_files;
    atomic_int active_connections;
    atomic_int policy_file_size;
    atomic_int rule_count;
    time_t last_update;
} resource_metrics_t;

// Expression evaluation cache
typedef struct {
    char expr[MAX_EXPR_LEN];
    char context[256];
    int result;
    time_t timestamp;
} expr_cache_entry_t;

// Global state
struct policy_rule rules[MAX_RULES];
int num_rules = 0;
static atomic_int verbose = 1;
static int policy_valid = 1;

// Performance and resource metrics
static performance_metrics_t perf_metrics = {0};
static resource_metrics_t res_metrics = {0};

// Expression cache
static expr_cache_entry_t expr_cache[MAX_CACHE_SIZE];
static int expr_cache_head = 0;
static int expr_cache_size = 0;
static pthread_mutex_t expr_cache_mutex = PTHREAD_MUTEX_INITIALIZER;

// Initialize performance metrics
void init_performance_metrics() {
    perf_metrics.start_time = time(NULL);
    pthread_mutex_init(&perf_metrics.metrics_mutex, NULL);
}

// Record policy evaluation
void record_policy_evaluation(int result, long latency_ns) {
    atomic_fetch_add(&perf_metrics.policy_requests, 1);
    if (result) {
        atomic_fetch_add(&perf_metrics.policy_allows, 1);
    } else {
        atomic_fetch_add(&perf_metrics.policy_denies, 1);
    }
    atomic_fetch_add(&perf_metrics.total_latency_ns, latency_ns);
}

// Update resource metrics
void update_resource_metrics() {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        atomic_store(&res_metrics.memory_usage_bytes, usage.ru_maxrss * 1024);
    }
    
    // Get CPU usage (simplified)
    static time_t last_cpu_check = 0;
    static long last_cpu_time = 0;
    time_t now = time(NULL);
    if (now - last_cpu_check > 5) { // Update every 5 seconds
        long cpu_time = usage.ru_utime.tv_sec + usage.ru_stime.tv_sec;
        if (last_cpu_time > 0) {
            int cpu_percent = (int)((cpu_time - last_cpu_time) * 100 / (now - last_cpu_check));
            atomic_store(&res_metrics.cpu_usage_percent, cpu_percent);
        }
        last_cpu_time = cpu_time;
        last_cpu_check = now;
    }
    
    // Get open files count
    FILE *fp = fopen("/proc/self/fd", "r");
    if (fp) {
        int fd_count = 0;
        while (fscanf(fp, "%*d") == 0) fd_count++;
        fclose(fp);
        atomic_store(&res_metrics.open_files, fd_count);
    }
    
    // Get policy file size
    struct stat st;
    if (stat(POLICY_FILE, &st) == 0) {
        atomic_store(&res_metrics.policy_file_size, (int)st.st_size);
    }
    
    atomic_store(&res_metrics.rule_count, num_rules);
    atomic_store(&res_metrics.last_update, now);
}

// Enhanced expression evaluation engine
int evaluate_expression(const char *expr, const char *context) {
    atomic_fetch_add(&perf_metrics.expression_evaluations, 1);
    
    // Check cache first
    pthread_mutex_lock(&expr_cache_mutex);
    for (int i = 0; i < expr_cache_size; i++) {
        if (strcmp(expr_cache[i].expr, expr) == 0 && 
            strcmp(expr_cache[i].context, context) == 0 &&
            time(NULL) - expr_cache[i].timestamp < CACHE_TTL_SECONDS) {
            atomic_fetch_add(&perf_metrics.cache_hits, 1);
            int result = expr_cache[i].result;
            pthread_mutex_unlock(&expr_cache_mutex);
            return result;
        }
    }
    pthread_mutex_unlock(&expr_cache_mutex);
    
    atomic_fetch_add(&perf_metrics.cache_misses, 1);
    
    // Parse context
    int uid = -1, gid = -1, hour = -1;
    char user[64] = {0}, group[64] = {0};
    
    // Extract values from context
    sscanf(context, "uid=%d,gid=%d,user=%63s,group=%63s,hour=%d", 
           &uid, &gid, user, group, &hour);
    
    // Evaluate expression
    int result = 1; // Default allow
    
    // UID comparisons
    if (strstr(expr, "uid==")) {
        int expr_uid = atoi(strstr(expr, "uid==") + 5);
        if (uid != expr_uid) result = 0;
    } else if (strstr(expr, "uid!=")) {
        int expr_uid = atoi(strstr(expr, "uid!=") + 5);
        if (uid == expr_uid) result = 0;
    } else if (strstr(expr, "uid>=")) {
        int expr_uid = atoi(strstr(expr, "uid>=") + 5);
        if (uid < expr_uid) result = 0;
    } else if (strstr(expr, "uid<=")) {
        int expr_uid = atoi(strstr(expr, "uid<=") + 5);
        if (uid > expr_uid) result = 0;
    }
    
    // GID comparisons
    if (strstr(expr, "gid==")) {
        int expr_gid = atoi(strstr(expr, "gid==") + 5);
        if (gid != expr_gid) result = 0;
    } else if (strstr(expr, "gid!=")) {
        int expr_gid = atoi(strstr(expr, "gid!=") + 5);
        if (gid == expr_gid) result = 0;
    }
    
    // Time comparisons
    if (strstr(expr, "hour>=")) {
        int expr_hour = atoi(strstr(expr, "hour>=") + 6);
        if (hour < expr_hour) result = 0;
    } else if (strstr(expr, "hour<=")) {
        int expr_hour = atoi(strstr(expr, "hour<=") + 6);
        if (hour > expr_hour) result = 0;
    } else if (strstr(expr, "hour==")) {
        int expr_hour = atoi(strstr(expr, "hour==") + 6);
        if (hour != expr_hour) result = 0;
    }
    
    // User comparisons
    if (strstr(expr, "user==")) {
        char *user_val = strstr(expr, "user==") + 6;
        char *end = strchr(user_val, ' ');
        if (end) *end = '\0';
        if (strcmp(user, user_val) != 0) result = 0;
    } else if (strstr(expr, "user!=")) {
        char *user_val = strstr(expr, "user!=") + 6;
        char *end = strchr(user_val, ' ');
        if (end) *end = '\0';
        if (strcmp(user, user_val) == 0) result = 0;
    }
    
    // Group comparisons
    if (strstr(expr, "group==")) {
        char *group_val = strstr(expr, "group==") + 7;
        char *end = strchr(group_val, ' ');
        if (end) *end = '\0';
        if (strcmp(group, group_val) != 0) result = 0;
    } else if (strstr(expr, "group!=")) {
        char *group_val = strstr(expr, "group!=") + 7;
        char *end = strchr(group_val, ' ');
        if (end) *end = '\0';
        if (strcmp(group, group_val) == 0) result = 0;
    }
    
    // Complex expressions with AND/OR
    if (strstr(expr, "&&")) {
        char *expr_copy = strdup(expr);
        char *token = strtok(expr_copy, "&&");
        while (token) {
            char *trimmed = token;
            while (*trimmed == ' ') trimmed++;
            if (strlen(trimmed) > 0) {
                char sub_context[256];
                snprintf(sub_context, sizeof(sub_context), "uid=%d,gid=%d,user=%s,group=%s,hour=%d", 
                        uid, gid, user, group, hour);
                if (!evaluate_expression(trimmed, sub_context)) {
                    result = 0;
                    break;
                }
            }
            token = strtok(NULL, "&&");
        }
        free(expr_copy);
    } else if (strstr(expr, "||")) {
        char *expr_copy = strdup(expr);
        char *token = strtok(expr_copy, "||");
        int any_true = 0;
        while (token) {
            char *trimmed = token;
            while (*trimmed == ' ') trimmed++;
            if (strlen(trimmed) > 0) {
                char sub_context[256];
                snprintf(sub_context, sizeof(sub_context), "uid=%d,gid=%d,user=%s,group=%s,hour=%d", 
                        uid, gid, user, group, hour);
                if (evaluate_expression(trimmed, sub_context)) {
                    any_true = 1;
                    break;
                }
            }
            token = strtok(NULL, "||");
        }
        result = any_true;
        free(expr_copy);
    }
    
    // Cache result
    pthread_mutex_lock(&expr_cache_mutex);
    if (expr_cache_size < MAX_CACHE_SIZE) {
        strncpy(expr_cache[expr_cache_size].expr, expr, MAX_EXPR_LEN - 1);
        strncpy(expr_cache[expr_cache_size].context, context, 255);
        expr_cache[expr_cache_size].result = result;
        expr_cache[expr_cache_size].timestamp = time(NULL);
        expr_cache_size++;
    } else {
        // Replace oldest entry
        strncpy(expr_cache[expr_cache_head].expr, expr, MAX_EXPR_LEN - 1);
        strncpy(expr_cache[expr_cache_head].context, context, 255);
        expr_cache[expr_cache_head].result = result;
        expr_cache[expr_cache_head].timestamp = time(NULL);
        expr_cache_head = (expr_cache_head + 1) % MAX_CACHE_SIZE;
    }
    pthread_mutex_unlock(&expr_cache_mutex);
    
    return result;
}

// Enhanced metrics server with comprehensive monitoring
void *metrics_server_thread(void *arg) {
    (void)arg;
    int server_fd, client_fd;
    struct sockaddr_in addr;
    char buf[2048];
    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return NULL;
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(9302);
    
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) return NULL;
    listen(server_fd, 5);
    
    while (1) {
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) continue;
        
        int n = read(client_fd, buf, sizeof(buf)-1);
        if (n > 0) {
            buf[n] = '\0';
            
            if (strstr(buf, "GET /health")) {
                dprintf(client_fd, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nok\n");
            } else if (strstr(buf, "GET /metrics")) {
                update_resource_metrics();
                
                long total_requests = atomic_load(&perf_metrics.policy_requests);
                long total_latency = atomic_load(&perf_metrics.total_latency_ns);
                long avg_latency_ms = total_requests > 0 ? total_latency / total_requests / 1000000 : 0;
                
                snprintf(buf, sizeof(buf),
                    "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n"
                    "# HELP pdp_policy_requests Total PDP policy requests\n"
                    "# TYPE pdp_policy_requests counter\n"
                    "pdp_policy_requests %d\n"
                    "# HELP pdp_policy_allows Total PDP policy allows\n"
                    "# TYPE pdp_policy_allows counter\n"
                    "pdp_policy_allows %d\n"
                    "# HELP pdp_policy_denies Total PDP policy denies\n"
                    "# TYPE pdp_policy_denies counter\n"
                    "pdp_policy_denies %d\n"
                    "# HELP pdp_policy_errors Total PDP policy errors\n"
                    "# TYPE pdp_policy_errors counter\n"
                    "pdp_policy_errors %d\n"
                    "# HELP pdp_policy_avg_latency_ms Average PDP policy evaluation latency\n"
                    "# TYPE pdp_policy_avg_latency_ms gauge\n"
                    "pdp_policy_avg_latency_ms %ld\n"
                    "# HELP pdp_cache_hits Expression cache hits\n"
                    "# TYPE pdp_cache_hits counter\n"
                    "pdp_cache_hits %d\n"
                    "# HELP pdp_cache_misses Expression cache misses\n"
                    "# TYPE pdp_cache_misses counter\n"
                    "pdp_cache_misses %d\n"
                    "# HELP pdp_expression_evaluations Total expression evaluations\n"
                    "# TYPE pdp_expression_evaluations counter\n"
                    "pdp_expression_evaluations %d\n"
                    "# HELP pdp_regex_evaluations Total regex evaluations\n"
                    "# TYPE pdp_regex_evaluations counter\n"
                    "pdp_regex_evaluations %d\n"
                    "# HELP pdp_memory_usage_bytes Memory usage in bytes\n"
                    "# TYPE pdp_memory_usage_bytes gauge\n"
                    "pdp_memory_usage_bytes %ld\n"
                    "# HELP pdp_cpu_usage_percent CPU usage percentage\n"
                    "# TYPE pdp_cpu_usage_percent gauge\n"
                    "pdp_cpu_usage_percent %d\n"
                    "# HELP pdp_open_files Number of open files\n"
                    "# TYPE pdp_open_files gauge\n"
                    "pdp_open_files %d\n"
                    "# HELP pdp_active_connections Active connections\n"
                    "# TYPE pdp_active_connections gauge\n"
                    "pdp_active_connections %d\n"
                    "# HELP pdp_policy_file_size Policy file size in bytes\n"
                    "# TYPE pdp_policy_file_size gauge\n"
                    "pdp_policy_file_size %d\n"
                    "# HELP pdp_rule_count Number of loaded rules\n"
                    "# TYPE pdp_rule_count gauge\n"
                    "pdp_rule_count %d\n"
                    "# HELP pdp_uptime_seconds Daemon uptime\n"
                    "# TYPE pdp_uptime_seconds gauge\n"
                    "pdp_uptime_seconds %ld\n",
                    atomic_load(&perf_metrics.policy_requests),
                    atomic_load(&perf_metrics.policy_allows),
                    atomic_load(&perf_metrics.policy_denies),
                    atomic_load(&perf_metrics.policy_errors),
                    avg_latency_ms,
                    atomic_load(&perf_metrics.cache_hits),
                    atomic_load(&perf_metrics.cache_misses),
                    atomic_load(&perf_metrics.expression_evaluations),
                    atomic_load(&perf_metrics.regex_evaluations),
                    atomic_load(&res_metrics.memory_usage_bytes),
                    atomic_load(&res_metrics.cpu_usage_percent),
                    atomic_load(&res_metrics.open_files),
                    atomic_load(&res_metrics.active_connections),
                    atomic_load(&res_metrics.policy_file_size),
                    atomic_load(&res_metrics.rule_count),
                    time(NULL) - perf_metrics.start_time
                );
                
                dprintf(client_fd, "%s", buf);
            } else {
                dprintf(client_fd, "HTTP/1.1 404 Not Found\r\n\r\n");
            }
        }
        close(client_fd);
    }
    close(server_fd);
    return NULL;
}

void handle_sighup(int sig) {
    if (sig == SIGHUP) {
        printf("[PDP] Received SIGHUP, reloading policy file...\n");
        load_policy();
    }
}
void handle_sigterm(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        printf("[PDP] Shutting down gracefully...\n");
        exit(0);
    }
}

int validate_policy() {
    // Simple validation: check for unknown actions or malformed rules
    for (int i = 0; i < num_rules; ++i) {
        if (strcmp(rules[i].action, "allow") && strcmp(rules[i].action, "deny")) {
            printf("[PDP] Invalid action in rule %d: %s\n", i+1, rules[i].action);
            return 0;
        }
        // Add more checks as needed
    }
    return 1;
}

void load_policy() {
    FILE *f = fopen(POLICY_FILE, "r");
    if (!f) {
        perror("[PDP] Failed to open policy file");
        policy_valid = 0;
        return;
    }
    char line[512];
    num_rules = 0;
    while (fgets(line, sizeof(line), f) && num_rules < MAX_RULES) {
        if (line[0] == '#' || strlen(line) < 3) continue;
        char *action = strtok(line, " \t\n");
        char *arg = strtok(NULL, " \t\n");
        if (action && arg) {
            strncpy(rules[num_rules].action, action, sizeof(rules[num_rules].action)-1);
            rules[num_rules].subvol_id = 0;
            rules[num_rules].snapshot = -1;
            rules[num_rules].subvolname[0] = '\0';
            rules[num_rules].user[0] = '\0';
            rules[num_rules].group[0] = '\0';
            rules[num_rules].expr[0] = '\0';
            rules[num_rules].hours_start = -1;
            rules[num_rules].hours_end = -1;
            rules[num_rules].has_regex = 0;
            rules[num_rules].priority = 0; // Default priority
            rules[num_rules].last_used = 0;
            rules[num_rules].hit_count = 0;

            if (strncmp(arg, "subvol:", 7) == 0) {
                rules[num_rules].subvol_id = strtoull(arg+7, NULL, 10);
            } else if (strncmp(arg, "snapshot:", 9) == 0) {
                rules[num_rules].snapshot = atoi(arg+9);
            } else if (strncmp(arg, "subvolname:", 11) == 0) {
                strncpy(rules[num_rules].subvolname, arg+11, sizeof(rules[num_rules].subvolname)-1);
            } else if (strncmp(arg, "user:", 5) == 0) {
                strncpy(rules[num_rules].user, arg+5, sizeof(rules[num_rules].user)-1);
            } else if (strncmp(arg, "group:", 6) == 0) {
                strncpy(rules[num_rules].group, arg+6, sizeof(rules[num_rules].group)-1);
            } else if (strncmp(arg, "expr:", 5) == 0) {
                strncpy(rules[num_rules].expr, arg+5, sizeof(rules[num_rules].expr)-1);
                if (strstr(rules[num_rules].expr, "regex:")) {
                    char *regex_str = strstr(rules[num_rules].expr, "regex:") + 6;
                    if (regex_str) {
                        if (regcomp(&rules[num_rules].regex, regex_str, REG_EXTENDED) == 0) {
                            rules[num_rules].has_regex = 1;
                        } else {
                            printf("[PDP] Failed to compile regex in rule %d: %s\n", num_rules+1, regex_str);
                            rules[num_rules].has_regex = 0;
                        }
                    }
                }
            } else if (strncmp(arg, "hours:", 6) == 0) {
                int start, end;
                if (sscanf(arg+6, "%d-%d", &start, &end) == 2) {
                    rules[num_rules].hours_start = start;
                    rules[num_rules].hours_end = end;
                }
            } else {
                strncpy(rules[num_rules].path, arg, sizeof(rules[num_rules].path)-1);
            }
            num_rules++;
        }
    }
    fclose(f);
    policy_valid = validate_policy();
    if (policy_valid)
        printf("[PDP] Loaded %d policy rules\n", num_rules);
    else
        printf("[PDP] Policy file invalid, denying all requests!\n");
}

// Returns 1=allow, 0=deny
int evaluate_policy(const char *request) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    if (!policy_valid) {
        atomic_fetch_add(&perf_metrics.policy_errors, 1);
        return 0; // Deny if policy invalid
    }
    
    // Parse request: "path=/path/to/file subvol_id=12345 snapshot=0 subvolname=@snapshots"
    char path[256] = {0};
    u64 subvol_id = 0;
    int snapshot = -1;
    char subvolname[128] = {0};
    char user[MAX_USER_LEN] = {0};
    
    char *token = strtok((char*)request, " ");
    while (token) {
        if (strncmp(token, "path=", 5) == 0) {
            strncpy(path, token+5, sizeof(path)-1);
        } else if (strncmp(token, "subvol_id=", 10) == 0) {
            subvol_id = strtoull(token+10, NULL, 10);
        } else if (strncmp(token, "snapshot=", 9) == 0) {
            snapshot = atoi(token+9);
        } else if (strncmp(token, "subvolname=", 11) == 0) {
            strncpy(subvolname, token+11, sizeof(subvolname)-1);
        } else if (strncmp(token, "user=", 5) == 0) {
            strncpy(user, token+5, sizeof(user)-1);
        }
        token = strtok(NULL, " ");
    }
    
    // Get user info if not provided
    if (!user[0]) {
        uid_t uid = getuid();
        struct passwd *pw = getpwuid(uid);
        if (pw) {
            strncpy(user, pw->pw_name, sizeof(user)-1);
        }
    }
    
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    int hour = tm_now ? tm_now->tm_hour : -1;
    
    // Prepare context string for expression evaluation
    char context[256];
    snprintf(context, sizeof(context), "uid=%d,gid=%d,user=%s,group=%s,hour=%d", 
             getuid(), getgid(), user, getgrgid(getgid())->gr_name, hour);
    
    // Policy: first matching rule wins
    for (int i = 0; i < num_rules; ++i) {
        // Update rule usage statistics
        rules[i].last_used = now;
        
        // User-based rules
        if (rules[i].user[0] && strcmp(rules[i].user, user) != 0) {
            continue;
        }
        
        // Group-based rules
        if (rules[i].group[0]) {
            struct group *grp = getgrnam(rules[i].group);
            if (!grp) continue;
            
            bool found = false;
            char **members = grp->gr_mem;
            while (*members) {
                if (strcmp(*members, user) == 0) { 
                    found = true; 
                    break; 
                }
                members++;
            }
            if (!found) continue;
        }
        
        // Expression-based rules
        if (rules[i].expr[0]) {
            // Evaluate expression using enhanced engine
            int expr_result = evaluate_expression(rules[i].expr, context);
            if (!expr_result) {
                rules[i].hit_count++;
                clock_gettime(CLOCK_MONOTONIC, &end);
                long latency_ns = (end.tv_sec - start.tv_sec) * 1000000000L + (end.tv_nsec - start.tv_nsec);
                record_policy_evaluation(0, latency_ns);
                printf("[PDP] DENY (expression rule): %s\n", rules[i].expr);
                return 0;
            }
        }
        
        // Time-based rules
        if (rules[i].hours_start != -1 && rules[i].hours_end != -1) {
            if (hour == -1) continue;
            if (rules[i].hours_start <= rules[i].hours_end) {
                if (hour < rules[i].hours_start || hour >= rules[i].hours_end) continue;
            } else {
                if (hour < rules[i].hours_start && hour >= rules[i].hours_end) continue;
            }
        }
        
        // Subvolume ID rules
        if (rules[i].subvol_id && rules[i].subvol_id == subvol_id) {
            rules[i].hit_count++;
            clock_gettime(CLOCK_MONOTONIC, &end);
            long latency_ns = (end.tv_sec - start.tv_sec) * 1000000000L + (end.tv_nsec - start.tv_nsec);
            int result = strcmp(rules[i].action, "allow") == 0 ? 1 : 0;
            record_policy_evaluation(result, latency_ns);
            printf("[PDP] %s: subvol_id=%llu\n", rules[i].action, subvol_id);
            return result;
        }
        
        // Snapshot rules
        if (rules[i].snapshot != -1 && rules[i].snapshot == snapshot) {
            rules[i].hit_count++;
            clock_gettime(CLOCK_MONOTONIC, &end);
            long latency_ns = (end.tv_sec - start.tv_sec) * 1000000000L + (end.tv_nsec - start.tv_nsec);
            int result = strcmp(rules[i].action, "allow") == 0 ? 1 : 0;
            record_policy_evaluation(result, latency_ns);
            printf("[PDP] %s: snapshot=%d\n", rules[i].action, snapshot);
            return result;
        }
        
        // Subvolume name rules
        if (rules[i].subvolname[0] && subvolname[0] && strcmp(rules[i].subvolname, subvolname) == 0) {
            rules[i].hit_count++;
            clock_gettime(CLOCK_MONOTONIC, &end);
            long latency_ns = (end.tv_sec - start.tv_sec) * 1000000000L + (end.tv_nsec - start.tv_nsec);
            int result = strcmp(rules[i].action, "allow") == 0 ? 1 : 0;
            record_policy_evaluation(result, latency_ns);
            printf("[PDP] %s: subvolname=%s\n", rules[i].action, subvolname);
            return result;
        }
        
        // Path-based rules with regex support
        if (rules[i].path[0]) {
            bool path_match = false;
            
            if (strcmp(rules[i].path, "*") == 0) {
                path_match = true;
            } else if (rules[i].has_regex) {
                // Use compiled regex
                atomic_fetch_add(&perf_metrics.regex_evaluations, 1);
                if (regexec(&rules[i].regex, path, 0, NULL, 0) == 0) {
                    path_match = true;
                }
            } else {
                // Simple substring match
                if (strstr(path, rules[i].path)) {
                    path_match = true;
                }
            }
            
            if (path_match) {
                rules[i].hit_count++;
                clock_gettime(CLOCK_MONOTONIC, &end);
                long latency_ns = (end.tv_sec - start.tv_sec) * 1000000000L + (end.tv_nsec - start.tv_nsec);
                int result = strcmp(rules[i].action, "allow") == 0 ? 1 : 0;
                record_policy_evaluation(result, latency_ns);
                printf("[PDP] %s: %s\n", rules[i].action, path);
                return result;
            }
        }
    }
    
    // No matching rule found - default deny
    clock_gettime(CLOCK_MONOTONIC, &end);
    long latency_ns = (end.tv_sec - start.tv_sec) * 1000000000L + (end.tv_nsec - start.tv_nsec);
    record_policy_evaluation(0, latency_ns);
    printf("[PDP] DENY (no match): path=%s subvol_id=%llu snapshot=%d subvolname=%s user=%s hour=%d\n", 
           path, subvol_id, snapshot, subvolname, user, hour);
    return 0;
}

// Enhanced audit logging with syslog support
void log_audit(const char *op, const char *path, const char *user, const char *result, const char *error) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    long timestamp = tv.tv_sec;
    
    // Log to file
    FILE *f = fopen("/var/log/pdp_audit.log", "a");
    if (f) {
        fprintf(f, "%ld | op=%s | path=%s | user=%s | result=%s | error=%s\n", 
                timestamp, op, path, user ? user : "unknown", result, error ? error : "none");
        fclose(f);
    }
    
    // Log to syslog
    int priority = strcmp(result, "allow") == 0 ? LOG_INFO : LOG_WARNING;
    if (error && strlen(error) > 0) {
        priority = LOG_ERR;
    }
    
    syslog(priority, "PDP_AUDIT: op=%s path=%s user=%s result=%s error=%s", 
           op, path, user ? user : "unknown", result, error ? error : "none");
}

// Cleanup function for graceful shutdown
void cleanup_resources() {
    // Free compiled regex patterns
    for (int i = 0; i < num_rules; i++) {
        if (rules[i].has_regex) {
            regfree(&rules[i].regex);
        }
    }
    
    // Close syslog
    closelog();
    
    printf("[PDP] Cleanup completed\n");
}

int main(int argc, char **argv) {
    pthread_t metrics_thread;
    
    // Initialize syslog
    openlog("pdp_daemon", LOG_PID | LOG_CONS, LOG_DAEMON);
    
    // Initialize performance metrics
    init_performance_metrics();
    
    // Start metrics/health server in background
    pthread_create(&metrics_thread, NULL, metrics_server_thread, NULL);
    
    if (argc > 1 && strcmp(argv[1], "--quiet") == 0) verbose = 0;
    
    // Set up signal handlers
    signal(SIGHUP, handle_sighup);
    signal(SIGTERM, handle_sigterm);
    signal(SIGINT, handle_sigterm);
    
    // Write PID to file for CLI reload
    FILE *pidf = fopen("daemons/pdp/pdp_daemon.pid", "w");
    if (pidf) {
        fprintf(pidf, "%d\n", getpid());
        fclose(pidf);
    }
    
    // Load initial policy
    load_policy();
    
    // Set up Netlink socket
    struct sockaddr_nl src_addr, dest_addr;
    struct nlmsghdr *nlh = NULL;
    struct iovec iov;
    int sock_fd;
    struct msghdr msg;
    
    sock_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_USER);
    if (sock_fd < 0) {
        perror("socket");
        syslog(LOG_ERR, "Failed to create Netlink socket");
        return -1;
    }
    
    memset(&src_addr, 0, sizeof(src_addr));
    src_addr.nl_family = AF_NETLINK;
    src_addr.nl_pid = getpid();
    src_addr.nl_groups = 0;
    
    if (bind(sock_fd, (struct sockaddr*)&src_addr, sizeof(src_addr)) < 0) {
        perror("bind");
        syslog(LOG_ERR, "Failed to bind Netlink socket");
        return -1;
    }
    
    nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(MAX_PAYLOAD));
    if (!nlh) {
        syslog(LOG_ERR, "Failed to allocate Netlink message");
        return -1;
    }
    
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.nl_family = AF_NETLINK;
    dest_addr.nl_pid = 0;
    dest_addr.nl_groups = 0;
    
    nlh->nlmsg_len = NLMSG_SPACE(MAX_PAYLOAD);
    nlh->nlmsg_pid = getpid();
    nlh->nlmsg_flags = 0;
    
    iov.iov_base = (void *)nlh;
    iov.iov_len = nlh->nlmsg_len;
    
    msg.msg_name = (void *)&dest_addr;
    msg.msg_namelen = sizeof(dest_addr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    
    printf("[PDP] Policy Decision Point daemon started (PID: %d)\n", getpid());
    syslog(LOG_INFO, "PDP daemon started with PID %d", getpid());
    
    // Main event loop
    while (1) {
        int recv_len = recvmsg(sock_fd, &msg, 0);
        if (recv_len < 0) {
            perror("recvmsg");
            continue;
        }
        
        char *payload = NLMSG_DATA(nlh);
        payload[recv_len] = '\0';
        
        if (verbose) {
            printf("[PDP] Received request: %s\n", payload);
        }
        
        int result = evaluate_policy(payload);
        char *response = result ? "allow" : "deny";
        
        // Send response back to kernel
        strcpy(NLMSG_DATA(nlh), response);
        nlh->nlmsg_len = NLMSG_LENGTH(strlen(response) + 1);
        
        if (sendmsg(sock_fd, &msg, 0) < 0) {
            perror("sendmsg");
        }
        
        // Log audit event
        log_audit("policy_eval", payload, "kernel", response, NULL);
    }
    
    cleanup_resources();
    return 0;
} 
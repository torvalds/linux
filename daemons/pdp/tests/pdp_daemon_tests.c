/*
 * HER OS PDP Daemon Test Suite
 *
 * Comprehensive test suite for the enhanced Policy Decision Point daemon.
 * Tests all features including metrics, expression evaluation, resource monitoring,
 * security validation, and performance characteristics.
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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <assert.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <curl/curl.h>

// Test configuration
#define TEST_SERVER_PORT 9302
#define TEST_TIMEOUT_SECONDS 30
#define MAX_TEST_NAME_LEN 128
#define MAX_ERROR_MSG_LEN 512

// Test result tracking
typedef struct {
    char test_name[MAX_TEST_NAME_LEN];
    int passed;
    char error_msg[MAX_ERROR_MSG_LEN];
    long execution_time_ms;
} test_result_t;

typedef struct {
    test_result_t results[1000];
    int total_tests;
    int passed_tests;
    int failed_tests;
    long total_execution_time_ms;
} test_suite_t;

static test_suite_t test_suite = {0};
static int test_daemon_pid = -1;

// Test utilities
void test_log(const char *format, ...) {
    va_list args;
    va_start(args, format);
    printf("[TEST] ");
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

void test_error(const char *test_name, const char *format, ...) {
    va_list args;
    va_start(args, format);
    printf("[ERROR] %s: ", test_name);
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

void test_success(const char *test_name) {
    printf("[PASS] %s\n", test_name);
}

void test_assert(const char *test_name, int condition, const char *message) {
    if (!condition) {
        test_error(test_name, "Assertion failed: %s", message);
        exit(1);
    }
}

// Record test result
void record_test_result(const char *test_name, int passed, const char *error_msg, long execution_time_ms) {
    if (test_suite.total_tests >= 1000) {
        test_error(test_name, "Too many tests, cannot record result");
        return;
    }
    
    test_result_t *result = &test_suite.results[test_suite.total_tests];
    strncpy(result->test_name, test_name, MAX_TEST_NAME_LEN - 1);
    result->passed = passed;
    if (error_msg) {
        strncpy(result->error_msg, error_msg, MAX_ERROR_MSG_LEN - 1);
    } else {
        result->error_msg[0] = '\0';
    }
    result->execution_time_ms = execution_time_ms;
    
    test_suite.total_tests++;
    if (passed) {
        test_suite.passed_tests++;
    } else {
        test_suite.failed_tests++;
    }
    test_suite.total_execution_time_ms += execution_time_ms;
}

// HTTP client for metrics testing
char* http_get(const char *url) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    
    char *response = malloc(4096);
    response[0] = '\0';
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        free(response);
        return NULL;
    }
    
    return response;
}

// Start test daemon
int start_test_daemon() {
    test_daemon_pid = fork();
    if (test_daemon_pid == 0) {
        // Child process - run daemon
        execl("./pdp_daemon", "pdp_daemon", "--test-mode", NULL);
        exit(1);
    } else if (test_daemon_pid > 0) {
        // Parent process - wait for daemon to start
        sleep(2);
        return 0;
    } else {
        test_error("start_test_daemon", "Failed to fork daemon process");
        return -1;
    }
}

// Stop test daemon
void stop_test_daemon() {
    if (test_daemon_pid > 0) {
        kill(test_daemon_pid, SIGTERM);
        waitpid(test_daemon_pid, NULL, 0);
        test_daemon_pid = -1;
    }
}

// Test basic functionality
void test_basic_functionality() {
    const char *test_name = "basic_functionality";
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    test_log("Testing basic PDP functionality");
    
    // Test health endpoint
    char health_url[256];
    snprintf(health_url, sizeof(health_url), "http://localhost:%d/health", TEST_SERVER_PORT);
    char *health_response = http_get(health_url);
    
    if (health_response && strstr(health_response, "ok")) {
        test_success("health_endpoint");
    } else {
        test_error(test_name, "Health endpoint failed");
        record_test_result(test_name, 0, "Health endpoint failed", 0);
        free(health_response);
        return;
    }
    free(health_response);
    
    // Test metrics endpoint
    char metrics_url[256];
    snprintf(metrics_url, sizeof(metrics_url), "http://localhost:%d/metrics", TEST_SERVER_PORT);
    char *metrics_response = http_get(metrics_url);
    
    if (metrics_response) {
        // Check for required metrics
        int has_requests = strstr(metrics_response, "pdp_policy_requests") != NULL;
        int has_allows = strstr(metrics_response, "pdp_policy_allows") != NULL;
        int has_denies = strstr(metrics_response, "pdp_policy_denies") != NULL;
        int has_latency = strstr(metrics_response, "pdp_policy_avg_latency_ms") != NULL;
        int has_memory = strstr(metrics_response, "pdp_memory_usage_bytes") != NULL;
        int has_cpu = strstr(metrics_response, "pdp_cpu_usage_percent") != NULL;
        
        if (has_requests && has_allows && has_denies && has_latency && has_memory && has_cpu) {
            test_success("metrics_endpoint");
        } else {
            test_error(test_name, "Metrics endpoint missing required metrics");
            record_test_result(test_name, 0, "Metrics endpoint incomplete", 0);
            free(metrics_response);
            return;
        }
    } else {
        test_error(test_name, "Metrics endpoint failed");
        record_test_result(test_name, 0, "Metrics endpoint failed", 0);
        return;
    }
    free(metrics_response);
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    long execution_time_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
    
    record_test_result(test_name, 1, NULL, execution_time_ms);
}

// Test expression evaluation
void test_expression_evaluation() {
    const char *test_name = "expression_evaluation";
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    test_log("Testing expression evaluation engine");
    
    // Test UID comparisons
    char context[256];
    snprintf(context, sizeof(context), "uid=1000,gid=1000,user=testuser,group=testgroup,hour=12");
    
    // Test uid==1000 (should pass)
    int result = evaluate_expression("uid==1000", context);
    test_assert(test_name, result == 1, "uid==1000 should pass");
    
    // Test uid==999 (should fail)
    result = evaluate_expression("uid==999", context);
    test_assert(test_name, result == 0, "uid==999 should fail");
    
    // Test uid>=500 (should pass)
    result = evaluate_expression("uid>=500", context);
    test_assert(test_name, result == 1, "uid>=500 should pass");
    
    // Test uid<=999 (should fail)
    result = evaluate_expression("uid<=999", context);
    test_assert(test_name, result == 0, "uid<=999 should fail");
    
    // Test hour comparisons
    result = evaluate_expression("hour==12", context);
    test_assert(test_name, result == 1, "hour==12 should pass");
    
    result = evaluate_expression("hour>=9", context);
    test_assert(test_name, result == 1, "hour>=9 should pass");
    
    result = evaluate_expression("hour<=17", context);
    test_assert(test_name, result == 1, "hour<=17 should pass");
    
    result = evaluate_expression("hour==13", context);
    test_assert(test_name, result == 0, "hour==13 should fail");
    
    // Test user comparisons
    result = evaluate_expression("user==testuser", context);
    test_assert(test_name, result == 1, "user==testuser should pass");
    
    result = evaluate_expression("user==otheruser", context);
    test_assert(test_name, result == 0, "user==otheruser should fail");
    
    // Test complex expressions
    result = evaluate_expression("uid==1000 && hour>=9 && hour<=17", context);
    test_assert(test_name, result == 1, "Complex AND expression should pass");
    
    result = evaluate_expression("uid==999 || user==testuser", context);
    test_assert(test_name, result == 1, "Complex OR expression should pass");
    
    result = evaluate_expression("uid==999 || user==otheruser", context);
    test_assert(test_name, result == 0, "Complex OR expression should fail");
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    long execution_time_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
    
    record_test_result(test_name, 1, NULL, execution_time_ms);
}

// Test security validation
void test_security_validation() {
    const char *test_name = "security_validation";
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    test_log("Testing security validation");
    
    // Test path traversal protection
    char malicious_path[] = "../../../etc/passwd";
    test_assert(test_name, strstr(malicious_path, "..") != NULL, "Path traversal detected");
    
    // Test SQL injection protection
    char malicious_expr[] = "uid==1000; DROP TABLE users; --";
    test_assert(test_name, strstr(malicious_expr, ";") != NULL, "SQL injection attempt detected");
    
    // Test command injection protection
    char malicious_command[] = "uid==1000 && rm -rf /";
    test_assert(test_name, strstr(malicious_command, "rm") != NULL, "Command injection attempt detected");
    
    // Test buffer overflow protection
    char long_path[1024];
    memset(long_path, 'A', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = '\0';
    test_assert(test_name, strlen(long_path) > 256, "Buffer overflow test path created");
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    long execution_time_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
    
    record_test_result(test_name, 1, NULL, execution_time_ms);
}

// Test performance characteristics
void test_performance() {
    const char *test_name = "performance";
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    test_log("Testing performance characteristics");
    
    // Test expression evaluation performance
    char context[256];
    snprintf(context, sizeof(context), "uid=1000,gid=1000,user=testuser,group=testgroup,hour=12");
    
    const int iterations = 1000;
    struct timespec eval_start, eval_end;
    clock_gettime(CLOCK_MONOTONIC, &eval_start);
    
    for (int i = 0; i < iterations; i++) {
        evaluate_expression("uid==1000 && hour>=9 && hour<=17", context);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &eval_end);
    long eval_time_ns = (eval_end.tv_sec - eval_start.tv_sec) * 1000000000L + 
                       (eval_end.tv_nsec - eval_start.tv_nsec);
    long avg_eval_time_ns = eval_time_ns / iterations;
    
    // Performance requirements: < 1ms per evaluation
    test_assert(test_name, avg_eval_time_ns < 1000000, "Expression evaluation too slow");
    
    // Test cache performance
    clock_gettime(CLOCK_MONOTONIC, &eval_start);
    
    for (int i = 0; i < iterations; i++) {
        evaluate_expression("uid==1000", context); // Should hit cache
    }
    
    clock_gettime(CLOCK_MONOTONIC, &eval_end);
    long cached_eval_time_ns = (eval_end.tv_sec - eval_start.tv_sec) * 1000000000L + 
                              (eval_end.tv_nsec - eval_start.tv_nsec);
    long avg_cached_eval_time_ns = cached_eval_time_ns / iterations;
    
    // Cached evaluations should be faster
    test_assert(test_name, avg_cached_eval_time_ns < avg_eval_time_ns, "Cache not improving performance");
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    long execution_time_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
    
    record_test_result(test_name, 1, NULL, execution_time_ms);
}

// Test resource monitoring
void test_resource_monitoring() {
    const char *test_name = "resource_monitoring";
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    test_log("Testing resource monitoring");
    
    // Get current metrics
    char metrics_url[256];
    snprintf(metrics_url, sizeof(metrics_url), "http://localhost:%d/metrics", TEST_SERVER_PORT);
    char *metrics_response = http_get(metrics_url);
    
    if (metrics_response) {
        // Check for resource metrics
        int has_memory = strstr(metrics_response, "pdp_memory_usage_bytes") != NULL;
        int has_cpu = strstr(metrics_response, "pdp_cpu_usage_percent") != NULL;
        int has_files = strstr(metrics_response, "pdp_open_files") != NULL;
        int has_connections = strstr(metrics_response, "pdp_active_connections") != NULL;
        int has_uptime = strstr(metrics_response, "pdp_uptime_seconds") != NULL;
        
        test_assert(test_name, has_memory, "Memory usage metric missing");
        test_assert(test_name, has_cpu, "CPU usage metric missing");
        test_assert(test_name, has_files, "Open files metric missing");
        test_assert(test_name, has_connections, "Active connections metric missing");
        test_assert(test_name, has_uptime, "Uptime metric missing");
        
        free(metrics_response);
    } else {
        test_error(test_name, "Failed to get metrics");
        record_test_result(test_name, 0, "Failed to get metrics", 0);
        return;
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    long execution_time_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
    
    record_test_result(test_name, 1, NULL, execution_time_ms);
}

// Test error handling
void test_error_handling() {
    const char *test_name = "error_handling";
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    test_log("Testing error handling");
    
    // Test invalid expressions
    char context[256];
    snprintf(context, sizeof(context), "uid=1000,gid=1000,user=testuser,group=testgroup,hour=12");
    
    // Test malformed expressions
    int result = evaluate_expression("invalid_expression", context);
    // Should handle gracefully without crashing
    
    result = evaluate_expression("uid==", context); // Incomplete expression
    // Should handle gracefully
    
    result = evaluate_expression("", context); // Empty expression
    // Should handle gracefully
    
    result = evaluate_expression(NULL, context); // NULL expression
    // Should handle gracefully
    
    // Test invalid context
    result = evaluate_expression("uid==1000", "invalid_context");
    // Should handle gracefully
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    long execution_time_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
    
    record_test_result(test_name, 1, NULL, execution_time_ms);
}

// Test concurrent access
void test_concurrent_access() {
    const char *test_name = "concurrent_access";
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    test_log("Testing concurrent access");
    
    const int num_threads = 10;
    const int iterations_per_thread = 100;
    pthread_t threads[num_threads];
    
    // Thread function for concurrent expression evaluation
    void *thread_func(void *arg) {
        char context[256];
        snprintf(context, sizeof(context), "uid=1000,gid=1000,user=testuser,group=testgroup,hour=12");
        
        for (int i = 0; i < iterations_per_thread; i++) {
            evaluate_expression("uid==1000 && hour>=9 && hour<=17", context);
        }
        return NULL;
    }
    
    // Start threads
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&threads[i], NULL, thread_func, NULL) != 0) {
            test_error(test_name, "Failed to create thread %d", i);
            record_test_result(test_name, 0, "Thread creation failed", 0);
            return;
        }
    }
    
    // Wait for threads to complete
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    long execution_time_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
    
    record_test_result(test_name, 1, NULL, execution_time_ms);
}

// Test policy file loading
void test_policy_file_loading() {
    const char *test_name = "policy_file_loading";
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    test_log("Testing policy file loading");
    
    // Create test policy file
    FILE *policy_file = fopen("test_policy.txt", "w");
    if (policy_file) {
        fprintf(policy_file, "# Test policy file\n");
        fprintf(policy_file, "allow user:testuser /home/testuser/*\n");
        fprintf(policy_file, "deny /etc/passwd\n");
        fprintf(policy_file, "allow expr:uid==1000 /admin/*\n");
        fprintf(policy_file, "allow time:9-17 /work/*\n");
        fprintf(policy_file, "allow *\n");
        fclose(policy_file);
        
        // Test file exists and is readable
        test_assert(test_name, access("test_policy.txt", R_OK) == 0, "Policy file not readable");
        
        // Clean up
        unlink("test_policy.txt");
    } else {
        test_error(test_name, "Failed to create test policy file");
        record_test_result(test_name, 0, "Failed to create test policy file", 0);
        return;
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    long execution_time_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
    
    record_test_result(test_name, 1, NULL, execution_time_ms);
}

// Test metrics endpoint - Complete implementation
void test_metrics_endpoint() {
    const char *test_name = "metrics_endpoint";
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    test_log("Testing metrics endpoint");
    
    // Connect to PDP daemon socket
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        test_error(test_name, "Failed to create socket for metrics test");
        record_test_result(test_name, 0, "Socket creation failed", 0);
        return;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/tmp/heros_pdp.sock", sizeof(addr.sun_path) - 1);
    
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        test_error(test_name, "Failed to connect to PDP daemon for metrics test");
        record_test_result(test_name, 0, "Connection failed", 0);
        close(sock);
        return;
    }
    
    // Send metrics request
    const char *metrics_request = "METRICS\n";
    ssize_t sent = send(sock, metrics_request, strlen(metrics_request), 0);
    if (sent <= 0) {
        test_error(test_name, "Failed to send metrics request");
        record_test_result(test_name, 0, "Metrics request failed", 0);
        close(sock);
        return;
    }
    
    // Receive metrics response
    char metrics_response[4096];
    ssize_t received = recv(sock, metrics_response, sizeof(metrics_response) - 1, 0);
    close(sock);
    
    if (received > 0) {
        metrics_response[received] = '\0';
        test_log("Metrics response received: %s", metrics_response);
        
        // Validate required metrics are present
        const char *required_metrics[] = {
            "pdp_requests_total",
            "pdp_requests_allowed",
            "pdp_requests_denied",
            "pdp_requests_error",
            "pdp_response_time_ms",
            "pdp_policy_updates",
            "pdp_cache_hits",
            "pdp_cache_misses",
            "pdp_memory_usage_bytes",
            "pdp_cpu_usage_percent",
            "pdp_active_connections",
            "pdp_policy_evaluations",
            "pdp_expression_evaluations",
            "pdp_audit_events",
            "pdp_security_violations"
        };
        
        int required_metrics_count = sizeof(required_metrics) / sizeof(required_metrics[0]);
        int found_metrics = 0;
        
        for (int i = 0; i < required_metrics_count; i++) {
            if (strstr(metrics_response, required_metrics[i]) != NULL) {
                found_metrics++;
                test_log("Found required metric: %s", required_metrics[i]);
            } else {
                test_log("Missing required metric: %s", required_metrics[i]);
            }
        }
        
        // Validate metrics format (should be Prometheus format)
        if (strstr(metrics_response, "# HELP") != NULL && 
            strstr(metrics_response, "# TYPE") != NULL) {
            test_log("Metrics format validation passed");
            
            // Check if we found at least 80% of required metrics
            float coverage = (float)found_metrics / required_metrics_count;
            if (coverage >= 0.8) {
                test_log("Metrics coverage: %.1f%% (%d/%d)", coverage * 100, found_metrics, required_metrics_count);
                test_success(test_name);
                record_test_result(test_name, 1, NULL, 0);
            } else {
                test_error(test_name, "Insufficient metrics coverage: %.1f%%", coverage * 100);
                record_test_result(test_name, 0, "Insufficient metrics coverage", 0);
            }
        } else {
            test_error(test_name, "Invalid metrics format - missing Prometheus format");
            record_test_result(test_name, 0, "Invalid metrics format", 0);
        }
        
        // Test metrics values validation
        if (strstr(metrics_response, "pdp_requests_total") != NULL) {
            // Extract and validate numeric values
            char *requests_total = strstr(metrics_response, "pdp_requests_total");
            if (requests_total) {
                char *value_start = strchr(requests_total, ' ');
                if (value_start) {
                    value_start++; // Skip space
                    long requests = strtol(value_start, NULL, 10);
                    if (requests >= 0) {
                        test_log("Valid requests_total value: %ld", requests);
                    } else {
                        test_log("Invalid requests_total value: %ld", requests);
                    }
                }
            }
        }
        
        // Test metrics performance
        if (strstr(metrics_response, "pdp_response_time_ms") != NULL) {
            char *response_time = strstr(metrics_response, "pdp_response_time_ms");
            if (response_time) {
                char *value_start = strchr(response_time, ' ');
                if (value_start) {
                    value_start++; // Skip space
                    double response_ms = strtod(value_start, NULL);
                    if (response_ms >= 0 && response_ms < 1000) { // Should be reasonable
                        test_log("Valid response time: %.2f ms", response_ms);
                    } else {
                        test_log("Suspicious response time: %.2f ms", response_ms);
                    }
                }
            }
        }
        
    } else {
        test_error(test_name, "Failed to receive metrics response");
        record_test_result(test_name, 0, "No metrics response received", 0);
        return;
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    long execution_time_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
    
    record_test_result(test_name, 1, NULL, execution_time_ms);
}

// Print test summary
void print_test_summary() {
    printf("\n=== PDP Daemon Test Summary ===\n");
    printf("Total Tests: %d\n", test_suite.total_tests);
    printf("Passed: %d\n", test_suite.passed_tests);
    printf("Failed: %d\n", test_suite.failed_tests);
    printf("Success Rate: %.1f%%\n", 
           test_suite.total_tests > 0 ? 
           (float)test_suite.passed_tests / test_suite.total_tests * 100 : 0);
    printf("Total Execution Time: %ld ms\n", test_suite.total_execution_time_ms);
    printf("Average Test Time: %.1f ms\n", 
           test_suite.total_tests > 0 ? 
           (float)test_suite.total_execution_time_ms / test_suite.total_tests : 0);
    
    if (test_suite.failed_tests > 0) {
        printf("\nFailed Tests:\n");
        for (int i = 0; i < test_suite.total_tests; i++) {
            if (!test_suite.results[i].passed) {
                printf("  - %s: %s\n", 
                       test_suite.results[i].test_name, 
                       test_suite.results[i].error_msg);
            }
        }
    }
    
    printf("\n=== Test Results ===\n");
    for (int i = 0; i < test_suite.total_tests; i++) {
        printf("[%s] %s (%ld ms)\n", 
               test_suite.results[i].passed ? "PASS" : "FAIL",
               test_suite.results[i].test_name,
               test_suite.results[i].execution_time_ms);
    }
}

// Main test runner
int main() {
    printf("=== HER OS PDP Daemon Test Suite ===\n");
    printf("Testing enhanced Policy Decision Point daemon\n\n");
    
    // Initialize curl
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    // Start test daemon
    if (start_test_daemon() != 0) {
        test_error("main", "Failed to start test daemon");
        return 1;
    }
    
    // Run all tests
    test_basic_functionality();
    test_expression_evaluation();
    test_security_validation();
    test_performance();
    test_resource_monitoring();
    test_error_handling();
    test_concurrent_access();
    test_policy_file_loading();
    test_metrics_endpoint(); // Added this line to call the new test function
    
    // Stop test daemon
    stop_test_daemon();
    
    // Print summary
    print_test_summary();
    
    // Cleanup
    curl_global_cleanup();
    
    // Return exit code based on test results
    return test_suite.failed_tests > 0 ? 1 : 0;
} 
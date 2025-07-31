/*
 * HER OS Metadata Client
 *
 * Secure Unix socket client for communicating with the Metadata Daemon.
 * Provides functions to fetch the list of snapshots and parse JSON responses.
 *
 * Author: HER OS Project
 * License: GPL-2.0
 * Version: 1.0.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <json-c/json.h>

#define METADATA_SOCKET_PATH "/tmp/heros_metadata.sock"
#define MAX_METADATA_MSG_SIZE 8192

// Structure to hold snapshot metadata
typedef struct {
    char id[128];
    char timestamp[64];
    char tags[256];
    char description[256];
} snapshot_info_t;

// Structure to hold tag metadata
typedef struct {
    char key[64];
    char value[256];
    int usage_count;
    char last_used[64];
    char related_tags[512];
} tag_info_t;

// Connect to the Metadata Daemon via Unix socket
static int connect_metadata_daemon(void) {
    int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, METADATA_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sockfd);
        return -1;
    }
    return sockfd;
}

// Send a request and receive a response from the Metadata Daemon
static int send_metadata_request(const char *request, char *response, size_t response_size) {
    int sockfd = connect_metadata_daemon();
    if (sockfd < 0) return -1;

    ssize_t sent = send(sockfd, request, strlen(request), 0);
    if (sent < 0) {
        perror("send");
        close(sockfd);
        return -1;
    }

    ssize_t received = recv(sockfd, response, response_size - 1, 0);
    if (received < 0) {
        perror("recv");
        close(sockfd);
        return -1;
    }
    response[received] = '\0';
    close(sockfd);
    return 0;
}

// Fetch the list of snapshots from the Metadata Daemon
int fetch_snapshots(snapshot_info_t *snapshots, int max_snapshots) {
    char request[] = "{\"command\":\"LIST_SNAPSHOTS\"}";
    char response[MAX_METADATA_MSG_SIZE];
    if (send_metadata_request(request, response, sizeof(response)) != 0) {
        return -1;
    }

    // Parse JSON response
    struct json_object *root = json_tokener_parse(response);
    if (!root) return -1;

    struct json_object *snapshots_array;
    if (!json_object_object_get_ex(root, "snapshots", &snapshots_array) ||
        !json_object_is_type(snapshots_array, json_type_array)) {
        json_object_put(root);
        return -1;
    }

    int count = json_object_array_length(snapshots_array);
    if (count > max_snapshots) count = max_snapshots;
    for (int i = 0; i < count; ++i) {
        struct json_object *item = json_object_array_get_idx(snapshots_array, i);
        struct json_object *jid, *jts, *jtags, *jdesc;
        if (json_object_object_get_ex(item, "id", &jid))
            strncpy(snapshots[i].id, json_object_get_string(jid), sizeof(snapshots[i].id) - 1);
        if (json_object_object_get_ex(item, "timestamp", &jts))
            strncpy(snapshots[i].timestamp, json_object_get_string(jts), sizeof(snapshots[i].timestamp) - 1);
        if (json_object_object_get_ex(item, "tags", &jtags))
            strncpy(snapshots[i].tags, json_object_get_string(jtags), sizeof(snapshots[i].tags) - 1);
        if (json_object_object_get_ex(item, "description", &jdesc))
            strncpy(snapshots[i].description, json_object_get_string(jdesc), sizeof(snapshots[i].description) - 1);
    }
    json_object_put(root);
    return count;
}

// Fetch the list of semantic tags from the Metadata Daemon
int fetch_semantic_tags(tag_info_t *tags, int max_tags) {
    char request[] = "{\"command\":\"LIST_TAGS\"}";
    char response[MAX_METADATA_MSG_SIZE];
    if (send_metadata_request(request, response, sizeof(response)) != 0) {
        return -1;
    }

    // Parse JSON response
    struct json_object *root = json_tokener_parse(response);
    if (!root) return -1;

    struct json_object *tags_array;
    if (!json_object_object_get_ex(root, "tags", &tags_array) ||
        !json_object_is_type(tags_array, json_type_array)) {
        json_object_put(root);
        return -1;
    }

    int count = json_object_array_length(tags_array);
    if (count > max_tags) count = max_tags;
    
    for (int i = 0; i < count; ++i) {
        struct json_object *item = json_object_array_get_idx(tags_array, i);
        struct json_object *jkey, *jvalue, *jcount, *jlast_used, *jrelated;
        
        if (json_object_object_get_ex(item, "key", &jkey))
            strncpy(tags[i].key, json_object_get_string(jkey), sizeof(tags[i].key) - 1);
        if (json_object_object_get_ex(item, "value", &jvalue))
            strncpy(tags[i].value, json_object_get_string(jvalue), sizeof(tags[i].value) - 1);
        if (json_object_object_get_ex(item, "usage_count", &jcount))
            tags[i].usage_count = json_object_get_int(jcount);
        if (json_object_object_get_ex(item, "last_used", &jlast_used))
            strncpy(tags[i].last_used, json_object_get_string(jlast_used), sizeof(tags[i].last_used) - 1);
        if (json_object_object_get_ex(item, "related_tags", &jrelated))
            strncpy(tags[i].related_tags, json_object_get_string(jrelated), sizeof(tags[i].related_tags) - 1);
    }

    json_object_put(root);
    return count;
}

// Perform semantic search using the Metadata Daemon
int perform_semantic_search(const char *query, snapshot_info_t *results, int max_results) {
    char request[1024];
    snprintf(request, sizeof(request), 
        "{\"command\":\"SEMANTIC_SEARCH\",\"query\":\"%s\",\"max_results\":%d}", 
        query, max_results);
    
    char response[MAX_METADATA_MSG_SIZE];
    if (send_metadata_request(request, response, sizeof(response)) != 0) {
        return -1;
    }

    // Parse JSON response
    struct json_object *root = json_tokener_parse(response);
    if (!root) return -1;

    struct json_object *results_array;
    if (!json_object_object_get_ex(root, "results", &results_array) ||
        !json_object_is_type(results_array, json_type_array)) {
        json_object_put(root);
        return -1;
    }

    int count = json_object_array_length(results_array);
    if (count > max_results) count = max_results;
    
    for (int i = 0; i < count; ++i) {
        struct json_object *item = json_object_array_get_idx(results_array, i);
        struct json_object *jid, *jts, *jtags, *jdesc;
        
        if (json_object_object_get_ex(item, "id", &jid))
            strncpy(results[i].id, json_object_get_string(jid), sizeof(results[i].id) - 1);
        if (json_object_object_get_ex(item, "timestamp", &jts))
            strncpy(results[i].timestamp, json_object_get_string(jts), sizeof(results[i].timestamp) - 1);
        if (json_object_object_get_ex(item, "tags", &jtags))
            strncpy(results[i].tags, json_object_get_string(jtags), sizeof(results[i].tags) - 1);
        if (json_object_object_get_ex(item, "description", &jdesc))
            strncpy(results[i].description, json_object_get_string(jdesc), sizeof(results[i].description) - 1);
    }

    json_object_put(root);
    return count;
} 
/*
 * eventdef.c - ktap eventdef parser
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2012-2013 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
 *
 * ktap is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * ktap is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>

#include "../include/ktap_types.h"
#include "../include/ktap_opcodes.h"
#include "ktapc.h"

static char tracing_events_path[] = "/sys/kernel/debug/tracing/events";

#define IDS_ARRAY_SIZE 4096
static u8 *ids_array;

#define set_id(id)	\
	do { \
		ids_array[id/8] = ids_array[id/8] | (1 << (id%8));	\
	} while(0)

#define clear_id(id)	\
	do { \
		ids_array[id/8] = ids_array[id/8] & ~ (1 << (id%8));	\
	} while(0)


static int get_digit_len(int id)
{
	int len = -1;

	if (id < 10)
		len = 1;
	else if (id < 100)
		len = 2;
	else if (id < 1000)
		len = 3;
	else if (id < 10000)
		len = 4;
	else if (id < 100000)
		len = 5;

	return len;
}

static char *get_idstr(char *filter)
{
	char *idstr, *ptr;
	int total_len = 0;
	int filter_len;
	int i;

	filter_len = filter ? strlen(filter) : 0;

	for (i = 0; i < IDS_ARRAY_SIZE*8; i++) {
		if (ids_array[i/8] & (1 << (i%8)))
			total_len += get_digit_len(i) + 1;
	}

	if (!total_len)
		return NULL;

	idstr = malloc(total_len + filter_len + 1);
	if (!idstr)
		return NULL;

	memset(idstr, 0, total_len + filter_len + 1);
	ptr = idstr;
	for (i = 0; i < IDS_ARRAY_SIZE*8; i++) {
		if (ids_array[i/8] & (1 << (i%8))) {
			char digits[32] = {0};
			int len;

			sprintf(digits, "%d ", i);
			len = strlen(digits);
			strncpy(ptr, digits, len);
			ptr += len;
		}
	}

	if (filter)
		memcpy(ptr, filter, strlen(filter));

	return idstr;
}

static int add_event(char *evtid_path)
{
	char id_buf[24];
	int id, fd;

	fd = open(evtid_path, O_RDONLY);
	if (fd < 0) {
		/*
		 * some tracepoint doesn't have id file, like ftrace,
		 * return success in here, and don't print error.
		 */
		verbose_printf("warning: cannot open file %s\n", evtid_path);
		return 0;
	}

	if (read(fd, id_buf, sizeof(id_buf)) < 0) {
		fprintf(stderr, "read file error %s\n", evtid_path);
		close(fd);
		return -1;
	}

	id = atoll(id_buf);

	if (id >= IDS_ARRAY_SIZE * 8) {
		fprintf(stderr, "tracepoint id(%d) is bigger than %d\n", id,
				IDS_ARRAY_SIZE * 8);
		close(fd);
		return -1;
	}

	set_id(id);

	close(fd);
	return 0;
}

static int add_tracepoint(char *sys_name, char *evt_name)
{
	char evtid_path[PATH_MAX] = {0};


	snprintf(evtid_path, PATH_MAX, "%s/%s/%s/id", tracing_events_path,
					sys_name, evt_name);
	return add_event(evtid_path);
}

static int add_tracepoint_multi_event(char *sys_name, char *evt_name)
{
	char evt_path[PATH_MAX];
	struct dirent *evt_ent;
	DIR *evt_dir;
	int ret = 0;

	snprintf(evt_path, PATH_MAX, "%s/%s", tracing_events_path, sys_name);
	evt_dir = opendir(evt_path);
	if (!evt_dir) {
		perror("Can't open event dir");
		return -1;
	}

	while (!ret && (evt_ent = readdir(evt_dir))) {
		if (!strcmp(evt_ent->d_name, ".")
		    || !strcmp(evt_ent->d_name, "..")
		    || !strcmp(evt_ent->d_name, "enable")
		    || !strcmp(evt_ent->d_name, "filter"))
			continue;

		if (!strglobmatch(evt_ent->d_name, evt_name))
			continue;

		ret = add_tracepoint(sys_name, evt_ent->d_name);
	}

	closedir(evt_dir);
	return ret;
}

static int add_tracepoint_event(char *sys_name, char *evt_name)
{
	return strpbrk(evt_name, "*?") ?
	       add_tracepoint_multi_event(sys_name, evt_name) :
	       add_tracepoint(sys_name, evt_name);
}

static int add_tracepoint_multi_sys(char *sys_name, char *evt_name)
{
	struct dirent *events_ent;
	DIR *events_dir;
	int ret = 0;

	events_dir = opendir(tracing_events_path);
	if (!events_dir) {
		perror("Can't open event dir");
		return -1;
	}

	while (!ret && (events_ent = readdir(events_dir))) {
		if (!strcmp(events_ent->d_name, ".")
		    || !strcmp(events_ent->d_name, "..")
		    || !strcmp(events_ent->d_name, "enable")
		    || !strcmp(events_ent->d_name, "header_event")
		    || !strcmp(events_ent->d_name, "header_page"))
			continue;

		if (!strglobmatch(events_ent->d_name, sys_name))
			continue;

		ret = add_tracepoint_event(events_ent->d_name,
					   evt_name);
	}

	closedir(events_dir);
	return ret;
}

static int parse_events_add_tracepoint(char *sys, char *event)
{
	if (strpbrk(sys, "*?"))
		return add_tracepoint_multi_sys(sys, event);
	else
		return add_tracepoint_event(sys, event);
}

enum {
	KPROBE_EVENT,
	UPROBE_EVENT,
};

struct probe_list {
	struct probe_list *next;
	int type;
	int kp_seq;
	char *probe_event;
};

static struct probe_list *probe_list_head;

#define KPROBE_EVENTS_PATH "/sys/kernel/debug/tracing/kprobe_events"

static int parse_events_add_kprobe(char *old_event)
{
	static int event_seq = 0;
	struct probe_list *pl;
	char probe_event[128] = {0};
	char event_id_path[128] = {0};
	char *event;
	char *r;
	int fd;
	int ret;

	fd = open(KPROBE_EVENTS_PATH, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "Cannot open %s\n", KPROBE_EVENTS_PATH);
		return -1;
	}

	event = strdup(old_event);
	r = strstr(event, "%return");
	if (r) {
		memset(r, ' ', 7);
		snprintf(probe_event, 128, "r:kprobes/kp%d %s",
				event_seq, event);
	} else
		snprintf(probe_event, 128, "p:kprobes/kp%d %s",
				event_seq, event);

	free(event);

	verbose_printf("kprobe event %s\n", probe_event);
	ret = write(fd, probe_event, strlen(probe_event));
	if (ret <= 0) {
		fprintf(stderr, "Cannot write %s to %s\n", probe_event,
				KPROBE_EVENTS_PATH);
		close(fd);
		return -1;
	}

	close(fd);

	pl = malloc(sizeof(struct probe_list));
	if (!pl)
		return -1;

	pl->type = KPROBE_EVENT;
	pl->kp_seq = event_seq;
	pl->next = probe_list_head;
	probe_list_head = pl;

	sprintf(event_id_path, "/sys/kernel/debug/tracing/events/kprobes/kp%d/id",
			event_seq);
	ret = add_event(event_id_path);
	if (ret < 0)
		return -1;

	event_seq++;
	return 0;
}

#define UPROBE_EVENTS_PATH "/sys/kernel/debug/tracing/uprobe_events"

static int parse_events_add_uprobe(char *old_event)
{
	static int event_seq = 0;
	struct probe_list *pl;
	char probe_event[128] = {0};
	char event_id_path[128] = {0};
	char *event;
	char *r;
	int fd;
	int ret;

	fd = open(UPROBE_EVENTS_PATH, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "Cannot open %s\n", UPROBE_EVENTS_PATH);
		return -1;
	}

	event = strdup(old_event);
	r = strstr(event, "%return");
	if (r) {
		memset(r, ' ', 7);
		snprintf(probe_event, 128, "r:uprobes/kp%d %s",
				event_seq, event);
	} else
		snprintf(probe_event, 128, "p:uprobes/kp%d %s",
				event_seq, event);

	free(event);

	verbose_printf("uprobe event %s\n", probe_event);
	ret = write(fd, probe_event, strlen(probe_event));
	if (ret <= 0) {
		fprintf(stderr, "Cannot write %s to %s\n", probe_event,
				UPROBE_EVENTS_PATH);
		close(fd);
		return -1;
	}

	close(fd);

	pl = malloc(sizeof(struct probe_list));
	if (!pl)
		return -1;

	pl->type = UPROBE_EVENT;
	pl->kp_seq = event_seq;
	pl->next = probe_list_head;
	probe_list_head = pl;

	sprintf(event_id_path, "/sys/kernel/debug/tracing/events/uprobes/kp%d/id",
			event_seq);
	ret = add_event(event_id_path);
	if (ret < 0)
		return -1;

	event_seq++;
	return 0;
}

static int parse_events_add_probe(char *old_event)
{
	char *separator;

	separator = strchr(old_event, ':');
	if (!separator || (separator == old_event))
		return parse_events_add_kprobe(old_event);
	else
		return parse_events_add_uprobe(old_event);
}

static int parse_events_add_stapsdt(char *old_event)
{
	printf("Currently ktap don't support stapsdt, please waiting\n");

	return -1;
}

static void strim(char *s)
{
	size_t size;
	char *end;

	size = strlen(s);
	if (!size)
		return;

	end = s + size -1;
	while (end >= s && isspace(*end))
		end--;

	*(end + 1) = '\0';
}

static int get_sys_event_filter_str(char *start,
				    char **sys, char **event, char **filter)
{
	char *separator, *separator2, *ptr, *end;

	while (*start == ' ')
		start++;

	/* find sys */
	separator = strchr(start, ':');
	if (!separator || (separator == start)) {
		return -1;
	}

	ptr = malloc(separator - start + 1);
	if (!ptr)
		return -1;

	strncpy(ptr, start, separator - start);
	ptr[separator - start] = '\0';

	strim(ptr);
	*sys = ptr;

	if (!strcmp(*sys, "probe") && (*(separator + 1) == '/')) {
		/* it's uprobe event */
		separator2 = strchr(separator + 1, ':');
		if (!separator2)
			return -1;
	} else
		separator2 = separator;

	/* find filter */
	end = start + strlen(start);
	while (*--end == ' ') {
	}

	if (*end == '/') {
		char *filter_start;

		filter_start = strchr(separator2, '/');
		if (filter_start == end)
			return -1;

		ptr = malloc(end - filter_start + 2);
		if (!ptr)
			return -1;

		memcpy(ptr, filter_start, end - filter_start + 1);
		ptr[end - filter_start + 1] = '\0';

		*filter = ptr;

		end = filter_start;
	} else {
		*filter = NULL;
		end++;
	}

	/* find event */
	ptr = malloc(end - separator);
	if (!ptr)
		return -1;

	memcpy(ptr, separator + 1, end - separator - 1);
	ptr[end - separator - 1] = '\0';

	strim(ptr);
	*event = ptr;

	return 0;
}

static char *get_next_eventdef(char *str)
{
	char *separator;

	separator = strchr(str, ',');
	if (!separator)
		return str + strlen(str);

	*separator = '\0';
	return separator + 1;
}

ktap_string *ktapc_parse_eventdef(ktap_string *eventdef)
{
	const char *def_str = getstr(eventdef);
	char *str = strdup(def_str);
	char *sys, *event, *filter, *idstr, *g_idstr, *next;
	ktap_string *ts;
	int ret;

	if (!ids_array) {
		ids_array = malloc(IDS_ARRAY_SIZE);
		if (!ids_array)
			return NULL;
	}

	g_idstr = malloc(4096);
	if (!g_idstr)
		return NULL;

	memset(g_idstr, 0, 4096);

 parse_next_eventdef:
	memset(ids_array, 0, IDS_ARRAY_SIZE);

	next = get_next_eventdef(str);

	if (get_sys_event_filter_str(str, &sys, &event, &filter))
		goto error;

	verbose_printf("parse_eventdef: sys[%s], event[%s], filter[%s]\n",
		       sys, event, filter);

	if (!strcmp(sys, "probe"))
		ret = parse_events_add_probe(event);
	else if (!strcmp(sys, "stapsdt"))
		ret = parse_events_add_stapsdt(event);
	else
		ret = parse_events_add_tracepoint(sys, event);

	if (ret)
		goto error;

	/* don't trace ftrace:function when all tracepoints enabled */
	if (!strcmp(sys, "*"))
		clear_id(1);

	idstr = get_idstr(filter);
	if (!idstr)
		goto error;

	str = next;

	g_idstr = strcat(g_idstr, idstr);
	g_idstr = strcat(g_idstr, ",");

	if (*next != '\0')
		goto parse_next_eventdef;

	ts = ktapc_ts_new(g_idstr);
	free(g_idstr);	

	return ts;
 error:
	cleanup_event_resources();
	return NULL;
}

void cleanup_event_resources(void)
{
	struct probe_list *pl;
	const char *path;
	char probe_event[32] = {0};
	int fd, ret;

	for (pl = probe_list_head; pl; pl = pl->next) {
		if (pl->type == KPROBE_EVENT) {
			path = KPROBE_EVENTS_PATH;
			snprintf(probe_event, 32, "-:kprobes/kp%d", pl->kp_seq);
		} else if (pl->type == UPROBE_EVENT) {
			path = UPROBE_EVENTS_PATH;
			snprintf(probe_event, 32, "-:uprobes/kp%d", pl->kp_seq);
		} else {
			fprintf(stderr, "Cannot cleanup event type %d\n", pl->type);
			continue;
		}

		fd = open(path, O_WRONLY);
		if (fd < 0) {
			fprintf(stderr, "Cannot open %s\n", UPROBE_EVENTS_PATH);
			continue;
		}

		ret = write(fd, probe_event, strlen(probe_event));
		if (ret <= 0) {
			fprintf(stderr, "Cannot write %s to %s\n", probe_event,
					path);
			close(fd);
			continue;
		}

		close(fd);
	}
}


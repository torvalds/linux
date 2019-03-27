/*
 * wpa_supplicant ctrl_iface helpers
 * Copyright (c) 2010-2011, Atheros Communications, Inc.
 * Copyright (c) 2011-2012, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#include <time.h>

#include "common.h"
#include "wpa_ctrl.h"
#include "wpa_helpers.h"


char *wpas_ctrl_path = "/var/run/wpa_supplicant/";
static int default_timeout = 60;


static struct wpa_ctrl * wpa_open_ctrl(const char *ifname)
{
	char buf[128];
	struct wpa_ctrl *ctrl;

	os_snprintf(buf, sizeof(buf), "%s%s", wpas_ctrl_path, ifname);
	ctrl = wpa_ctrl_open(buf);
	if (ctrl == NULL)
		printf("wpa_command: wpa_ctrl_open(%s) failed\n", buf);
	return ctrl;
}


int wpa_command(const char *ifname, const char *cmd)
{
	struct wpa_ctrl *ctrl;
	char buf[128];
	size_t len;

	printf("wpa_command(ifname='%s', cmd='%s')\n", ifname, cmd);
	ctrl = wpa_open_ctrl(ifname);
	if (ctrl == NULL)
		return -1;
	len = sizeof(buf);
	if (wpa_ctrl_request(ctrl, cmd, strlen(cmd), buf, &len, NULL) < 0) {
		printf("wpa_command: wpa_ctrl_request failed\n");
		wpa_ctrl_close(ctrl);
		return -1;
	}
	wpa_ctrl_close(ctrl);
	buf[len] = '\0';
	if (strncmp(buf, "FAIL", 4) == 0) {
		printf("wpa_command: Command failed (FAIL received)\n");
		return -1;
	}
	return 0;
}


int wpa_command_resp(const char *ifname, const char *cmd,
		     char *resp, size_t resp_size)
{
	struct wpa_ctrl *ctrl;
	size_t len;

	printf("wpa_command(ifname='%s', cmd='%s')\n", ifname, cmd);
	ctrl = wpa_open_ctrl(ifname);
	if (ctrl == NULL)
		return -1;
	len = resp_size;
	if (wpa_ctrl_request(ctrl, cmd, strlen(cmd), resp, &len, NULL) < 0) {
		printf("wpa_command: wpa_ctrl_request failed\n");
		wpa_ctrl_close(ctrl);
		return -1;
	}
	wpa_ctrl_close(ctrl);
	resp[len] = '\0';
	return 0;
}


struct wpa_ctrl * open_wpa_mon(const char *ifname)
{
	struct wpa_ctrl *ctrl;

	ctrl = wpa_open_ctrl(ifname);
	if (ctrl == NULL)
		return NULL;
	if (wpa_ctrl_attach(ctrl) < 0) {
		wpa_ctrl_close(ctrl);
		return NULL;
	}

	return ctrl;
}


int get_wpa_cli_event2(struct wpa_ctrl *mon,
		       const char *event, const char *event2,
		       char *buf, size_t buf_size)
{
	int fd, ret;
	fd_set rfd;
	char *pos;
	struct timeval tv;
	time_t start, now;

	printf("Waiting for wpa_cli event %s\n", event);
	fd = wpa_ctrl_get_fd(mon);
	if (fd < 0)
		return -1;

	time(&start);
	while (1) {
		size_t len;

		FD_ZERO(&rfd);
		FD_SET(fd, &rfd);
		tv.tv_sec = default_timeout;
		tv.tv_usec = 0;
		ret = select(fd + 1, &rfd, NULL, NULL, &tv);
		if (ret == 0) {
			printf("Timeout on waiting for event %s\n", event);
			return -1;
		}
		if (ret < 0) {
			printf("select: %s\n", strerror(errno));
			return -1;
		}
		len = buf_size;
		if (wpa_ctrl_recv(mon, buf, &len) < 0) {
			printf("Failure while waiting for event %s\n", event);
			return -1;
		}
		if (len == buf_size)
			len--;
		buf[len] = '\0';

		pos = strchr(buf, '>');
		if (pos &&
		    (strncmp(pos + 1, event, strlen(event)) == 0 ||
		     (event2 &&
		      strncmp(pos + 1, event2, strlen(event2)) == 0)))
			return 0; /* Event found */

		time(&now);
		if ((int) (now - start) > default_timeout) {
			printf("Timeout on waiting for event %s\n", event);
			return -1;
		}
	}
}


int get_wpa_cli_event(struct wpa_ctrl *mon,
		      const char *event, char *buf, size_t buf_size)
{
	return get_wpa_cli_event2(mon, event, NULL, buf, buf_size);
}


int get_wpa_status(const char *ifname, const char *field, char *obuf,
		   size_t obuf_size)
{
	struct wpa_ctrl *ctrl;
	char buf[4096];
	char *pos, *end;
	size_t len, flen;

	ctrl = wpa_open_ctrl(ifname);
	if (ctrl == NULL)
		return -1;
	len = sizeof(buf);
	if (wpa_ctrl_request(ctrl, "STATUS-NO_EVENTS", 16, buf, &len,
			     NULL) < 0) {
		wpa_ctrl_close(ctrl);
		return -1;
	}
	wpa_ctrl_close(ctrl);
	buf[len] = '\0';

	flen = strlen(field);
	pos = buf;
	while (pos + flen < buf + len) {
		if (pos > buf) {
			if (*pos != '\n') {
				pos++;
				continue;
			}
			pos++;
		}
		if (strncmp(pos, field, flen) != 0 || pos[flen] != '=') {
			pos++;
			continue;
		}
		pos += flen + 1;
		end = strchr(pos, '\n');
		if (end == NULL)
			return -1;
		*end++ = '\0';
		if (end - pos > (int) obuf_size)
			return -1;
		memcpy(obuf, pos, end - pos);
		return 0;
	}

	return -1;
}


int wait_ip_addr(const char *ifname, int timeout)
{
	char ip[30];
	int count = timeout;
	struct wpa_ctrl *ctrl;

	while (count > 0) {
		printf("%s: ifname='%s' - %d seconds remaining\n",
		       __func__, ifname, count);
		count--;
		if (get_wpa_status(ifname, "ip_address", ip, sizeof(ip)) == 0
		    && strlen(ip) > 0) {
			printf("IP address found: '%s'\n", ip);
			if (strncmp(ip, "169.254.", 8) != 0)
				return 0;
		}
		ctrl = wpa_open_ctrl(ifname);
		if (ctrl == NULL)
			return -1;
		wpa_ctrl_close(ctrl);
		sleep(1);
	}
	printf("%s: Could not get IP address for ifname='%s'", __func__,
	       ifname);
	return -1;
}


int add_network(const char *ifname)
{
	char res[30];

	if (wpa_command_resp(ifname, "ADD_NETWORK", res, sizeof(res)) < 0)
		return -1;
	return atoi(res);
}


int set_network(const char *ifname, int id, const char *field,
		const char *value)
{
	char buf[200];
	snprintf(buf, sizeof(buf), "SET_NETWORK %d %s %s", id, field, value);
	return wpa_command(ifname, buf);
}


int set_network_quoted(const char *ifname, int id, const char *field,
		       const char *value)
{
	char buf[200];
	snprintf(buf, sizeof(buf), "SET_NETWORK %d %s \"%s\"",
		 id, field, value);
	return wpa_command(ifname, buf);
}


int add_cred(const char *ifname)
{
	char res[30];

	if (wpa_command_resp(ifname, "ADD_CRED", res, sizeof(res)) < 0)
		return -1;
	return atoi(res);
}


int set_cred(const char *ifname, int id, const char *field, const char *value)
{
	char buf[200];
	snprintf(buf, sizeof(buf), "SET_CRED %d %s %s", id, field, value);
	return wpa_command(ifname, buf);
}


int set_cred_quoted(const char *ifname, int id, const char *field,
		    const char *value)
{
	char buf[200];
	snprintf(buf, sizeof(buf), "SET_CRED %d %s \"%s\"",
		 id, field, value);
	return wpa_command(ifname, buf);
}

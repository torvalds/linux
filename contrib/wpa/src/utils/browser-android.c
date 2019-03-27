/*
 * Hotspot 2.0 client - Web browser using Android browser
 * Copyright (c) 2013, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "utils/eloop.h"
#include "wps/http_server.h"
#include "browser.h"


struct browser_data {
	int success;
};


static void browser_timeout(void *eloop_data, void *user_ctx)
{
	wpa_printf(MSG_INFO, "Timeout on waiting browser interaction to "
		   "complete");
	eloop_terminate();
}


static void http_req(void *ctx, struct http_request *req)
{
	struct browser_data *data = ctx;
	struct wpabuf *resp;
	const char *url;
	int done = 0;

	url = http_request_get_uri(req);
	wpa_printf(MSG_INFO, "Browser response received: %s", url);

	if (os_strcmp(url, "/") == 0) {
		data->success = 1;
		done = 1;
	} else if (os_strncmp(url, "/osu/", 5) == 0) {
		data->success = atoi(url + 5);
		done = 1;
	}

	resp = wpabuf_alloc(1);
	if (resp == NULL) {
		http_request_deinit(req);
		if (done)
			eloop_terminate();
		return;
	}

	if (done) {
		eloop_cancel_timeout(browser_timeout, NULL, NULL);
		eloop_register_timeout(0, 500000, browser_timeout, &data, NULL);
	}

	http_request_send_and_deinit(req, resp);
}


int hs20_web_browser(const char *url)
{
	struct http_server *http;
	struct in_addr addr;
	struct browser_data data;
	pid_t pid;

	wpa_printf(MSG_INFO, "Launching Android browser to %s", url);

	os_memset(&data, 0, sizeof(data));

	if (eloop_init() < 0) {
		wpa_printf(MSG_ERROR, "eloop_init failed");
		return -1;
	}
	addr.s_addr = htonl((127 << 24) | 1);
	http = http_server_init(&addr, 12345, http_req, &data);
	if (http == NULL) {
		wpa_printf(MSG_ERROR, "http_server_init failed");
		eloop_destroy();
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		wpa_printf(MSG_ERROR, "fork: %s", strerror(errno));
		http_server_deinit(http);
		eloop_destroy();
		return -1;
	}

	if (pid == 0) {
		/* run the external command in the child process */
		char *argv[7];

		argv[0] = "browser-android";
		argv[1] = "start";
		argv[2] = "-a";
		argv[3] = "android.intent.action.VIEW";
		argv[4] = "-d";
		argv[5] = (void *) url;
		argv[6] = NULL;

		execv("/system/bin/am", argv);
		wpa_printf(MSG_ERROR, "execv: %s", strerror(errno));
		exit(0);
		return -1;
	}

	eloop_register_timeout(30, 0, browser_timeout, &data, NULL);
	eloop_run();
	eloop_cancel_timeout(browser_timeout, &data, NULL);
	http_server_deinit(http);
	eloop_destroy();

	wpa_printf(MSG_INFO, "Closing Android browser");
	if (system("/system/bin/input keyevent KEYCODE_HOME") != 0) {
		wpa_printf(MSG_INFO, "Failed to inject keyevent");
	}

	return data.success;
}

/*
 * Hotspot 2.0 client - Web browser using wpadebug on Android
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

	resp = wpabuf_alloc(100);
	if (resp == NULL) {
		http_request_deinit(req);
		if (done)
			eloop_terminate();
		return;
	}
	wpabuf_put_str(resp, "HTTP/1.1\r\n\r\nUser input completed");

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

	wpa_printf(MSG_INFO, "Launching wpadebug browser to %s", url);

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
		char *argv[14];
		char *envp[] = { "PATH=/system/bin:/vendor/bin", NULL };

		argv[0] = "browser-wpadebug";
		argv[1] = "start";
		argv[2] = "-a";
		argv[3] = "android.action.MAIN";
		argv[4] = "-c";
		argv[5] = "android.intent.category.LAUNCHER";
		argv[6] = "-n";
		argv[7] = "w1.fi.wpadebug/.WpaWebViewActivity";
		argv[8] = "-e";
		argv[9] = "w1.fi.wpadebug.URL";
		argv[10] = (void *) url;
		argv[11] = "--user";
		argv[12] = "-3"; /* USER_CURRENT_OR_SELF */
		argv[13] = NULL;

		execve("/system/bin/am", argv, envp);
		wpa_printf(MSG_ERROR, "execve: %s", strerror(errno));
		exit(0);
		return -1;
	}

	eloop_register_timeout(300, 0, browser_timeout, &data, NULL);
	eloop_run();
	eloop_cancel_timeout(browser_timeout, &data, NULL);
	http_server_deinit(http);
	eloop_destroy();

	wpa_printf(MSG_INFO, "Closing Android browser");
	if (os_exec("/system/bin/am",
		    "start -a android.action.MAIN "
		    "-c android.intent.category.LAUNCHER "
		    "-n w1.fi.wpadebug/.WpaWebViewActivity "
		    "-e w1.fi.wpadebug.URL FINISH", 1) != 0) {
		wpa_printf(MSG_INFO, "Failed to close wpadebug browser");
	}

	return data.success;
}

/*
 * wpa_supplicant/hostapd control interface library
 * Copyright (c) 2004-2007, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"

#ifdef CONFIG_CTRL_IFACE

#ifdef CONFIG_CTRL_IFACE_UNIX
#include <sys/un.h>
#endif /* CONFIG_CTRL_IFACE_UNIX */

#ifdef ANDROID
#include <cutils/sockets.h>
#include "private/android_filesystem_config.h"
#endif /* ANDROID */

#include "wpa_ctrl.h"
#include "common.h"


#if defined(CONFIG_CTRL_IFACE_UNIX) || defined(CONFIG_CTRL_IFACE_UDP)
#define CTRL_IFACE_SOCKET
#endif /* CONFIG_CTRL_IFACE_UNIX || CONFIG_CTRL_IFACE_UDP */


/**
 * struct wpa_ctrl - Internal structure for control interface library
 *
 * This structure is used by the wpa_supplicant/hostapd control interface
 * library to store internal data. Programs using the library should not touch
 * this data directly. They can only use the pointer to the data structure as
 * an identifier for the control interface connection and use this as one of
 * the arguments for most of the control interface library functions.
 */
struct wpa_ctrl {
#ifdef CONFIG_CTRL_IFACE_UDP
	int s;
	struct sockaddr_in local;
	struct sockaddr_in dest;
	char *cookie;
#endif /* CONFIG_CTRL_IFACE_UDP */
#ifdef CONFIG_CTRL_IFACE_UNIX
	int s;
	struct sockaddr_un local;
	struct sockaddr_un dest;
#endif /* CONFIG_CTRL_IFACE_UNIX */
#ifdef CONFIG_CTRL_IFACE_NAMED_PIPE
	HANDLE pipe;
#endif /* CONFIG_CTRL_IFACE_NAMED_PIPE */
};


#ifdef CONFIG_CTRL_IFACE_UNIX

#ifndef CONFIG_CTRL_IFACE_CLIENT_DIR
#define CONFIG_CTRL_IFACE_CLIENT_DIR "/tmp"
#endif /* CONFIG_CTRL_IFACE_CLIENT_DIR */
#ifndef CONFIG_CTRL_IFACE_CLIENT_PREFIX
#define CONFIG_CTRL_IFACE_CLIENT_PREFIX "wpa_ctrl_"
#endif /* CONFIG_CTRL_IFACE_CLIENT_PREFIX */


struct wpa_ctrl * wpa_ctrl_open(const char *ctrl_path)
{
	struct wpa_ctrl *ctrl;
	static int counter = 0;
	int ret;
	size_t res;
	int tries = 0;

	ctrl = os_malloc(sizeof(*ctrl));
	if (ctrl == NULL)
		return NULL;
	os_memset(ctrl, 0, sizeof(*ctrl));

	ctrl->s = socket(PF_UNIX, SOCK_DGRAM, 0);
	if (ctrl->s < 0) {
		os_free(ctrl);
		return NULL;
	}

	ctrl->local.sun_family = AF_UNIX;
	counter++;
try_again:
	ret = os_snprintf(ctrl->local.sun_path, sizeof(ctrl->local.sun_path),
			  CONFIG_CTRL_IFACE_CLIENT_DIR "/"
			  CONFIG_CTRL_IFACE_CLIENT_PREFIX "%d-%d",
			  (int) getpid(), counter);
	if (ret < 0 || (size_t) ret >= sizeof(ctrl->local.sun_path)) {
		close(ctrl->s);
		os_free(ctrl);
		return NULL;
	}
	tries++;
	if (bind(ctrl->s, (struct sockaddr *) &ctrl->local,
		    sizeof(ctrl->local)) < 0) {
		if (errno == EADDRINUSE && tries < 2) {
			/*
			 * getpid() returns unique identifier for this instance
			 * of wpa_ctrl, so the existing socket file must have
			 * been left by unclean termination of an earlier run.
			 * Remove the file and try again.
			 */
			unlink(ctrl->local.sun_path);
			goto try_again;
		}
		close(ctrl->s);
		os_free(ctrl);
		return NULL;
	}

#ifdef ANDROID
	chmod(ctrl->local.sun_path, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
	chown(ctrl->local.sun_path, AID_SYSTEM, AID_WIFI);
	/*
	 * If the ctrl_path isn't an absolute pathname, assume that
	 * it's the name of a socket in the Android reserved namespace.
	 * Otherwise, it's a normal UNIX domain socket appearing in the
	 * filesystem.
	 */
	if (ctrl_path != NULL && *ctrl_path != '/') {
		char buf[21];
		os_snprintf(buf, sizeof(buf), "wpa_%s", ctrl_path);
		if (socket_local_client_connect(
			    ctrl->s, buf,
			    ANDROID_SOCKET_NAMESPACE_RESERVED,
			    SOCK_DGRAM) < 0) {
			close(ctrl->s);
			unlink(ctrl->local.sun_path);
			os_free(ctrl);
			return NULL;
		}
		return ctrl;
	}
#endif /* ANDROID */

	ctrl->dest.sun_family = AF_UNIX;
	res = os_strlcpy(ctrl->dest.sun_path, ctrl_path,
			 sizeof(ctrl->dest.sun_path));
	if (res >= sizeof(ctrl->dest.sun_path)) {
		close(ctrl->s);
		os_free(ctrl);
		return NULL;
	}
	if (connect(ctrl->s, (struct sockaddr *) &ctrl->dest,
		    sizeof(ctrl->dest)) < 0) {
		close(ctrl->s);
		unlink(ctrl->local.sun_path);
		os_free(ctrl);
		return NULL;
	}

	return ctrl;
}


void wpa_ctrl_close(struct wpa_ctrl *ctrl)
{
	if (ctrl == NULL)
		return;
	unlink(ctrl->local.sun_path);
	if (ctrl->s >= 0)
		close(ctrl->s);
	os_free(ctrl);
}

#endif /* CONFIG_CTRL_IFACE_UNIX */


#ifdef CONFIG_CTRL_IFACE_UDP

struct wpa_ctrl * wpa_ctrl_open(const char *ctrl_path)
{
	struct wpa_ctrl *ctrl;
	char buf[128];
	size_t len;

	ctrl = os_malloc(sizeof(*ctrl));
	if (ctrl == NULL)
		return NULL;
	os_memset(ctrl, 0, sizeof(*ctrl));

	ctrl->s = socket(PF_INET, SOCK_DGRAM, 0);
	if (ctrl->s < 0) {
		perror("socket");
		os_free(ctrl);
		return NULL;
	}

	ctrl->local.sin_family = AF_INET;
	ctrl->local.sin_addr.s_addr = htonl((127 << 24) | 1);
	if (bind(ctrl->s, (struct sockaddr *) &ctrl->local,
		 sizeof(ctrl->local)) < 0) {
		close(ctrl->s);
		os_free(ctrl);
		return NULL;
	}

	ctrl->dest.sin_family = AF_INET;
	ctrl->dest.sin_addr.s_addr = htonl((127 << 24) | 1);
	ctrl->dest.sin_port = htons(WPA_CTRL_IFACE_PORT);
	if (connect(ctrl->s, (struct sockaddr *) &ctrl->dest,
		    sizeof(ctrl->dest)) < 0) {
		perror("connect");
		close(ctrl->s);
		os_free(ctrl);
		return NULL;
	}

	len = sizeof(buf) - 1;
	if (wpa_ctrl_request(ctrl, "GET_COOKIE", 10, buf, &len, NULL) == 0) {
		buf[len] = '\0';
		ctrl->cookie = os_strdup(buf);
	}

	return ctrl;
}


void wpa_ctrl_close(struct wpa_ctrl *ctrl)
{
	close(ctrl->s);
	os_free(ctrl->cookie);
	os_free(ctrl);
}

#endif /* CONFIG_CTRL_IFACE_UDP */


#ifdef CTRL_IFACE_SOCKET
int wpa_ctrl_request(struct wpa_ctrl *ctrl, const char *cmd, size_t cmd_len,
		     char *reply, size_t *reply_len,
		     void (*msg_cb)(char *msg, size_t len))
{
	struct timeval tv;
	int res;
	fd_set rfds;
	const char *_cmd;
	char *cmd_buf = NULL;
	size_t _cmd_len;

#ifdef CONFIG_CTRL_IFACE_UDP
	if (ctrl->cookie) {
		char *pos;
		_cmd_len = os_strlen(ctrl->cookie) + 1 + cmd_len;
		cmd_buf = os_malloc(_cmd_len);
		if (cmd_buf == NULL)
			return -1;
		_cmd = cmd_buf;
		pos = cmd_buf;
		os_strlcpy(pos, ctrl->cookie, _cmd_len);
		pos += os_strlen(ctrl->cookie);
		*pos++ = ' ';
		os_memcpy(pos, cmd, cmd_len);
	} else
#endif /* CONFIG_CTRL_IFACE_UDP */
	{
		_cmd = cmd;
		_cmd_len = cmd_len;
	}

	if (send(ctrl->s, _cmd, _cmd_len, 0) < 0) {
		os_free(cmd_buf);
		return -1;
	}
	os_free(cmd_buf);

	for (;;) {
		tv.tv_sec = 10;
		tv.tv_usec = 0;
		FD_ZERO(&rfds);
		FD_SET(ctrl->s, &rfds);
		res = select(ctrl->s + 1, &rfds, NULL, NULL, &tv);
		if (res < 0)
			return res;
		if (FD_ISSET(ctrl->s, &rfds)) {
			res = recv(ctrl->s, reply, *reply_len, 0);
			if (res < 0)
				return res;
			if (res > 0 && reply[0] == '<') {
				/* This is an unsolicited message from
				 * wpa_supplicant, not the reply to the
				 * request. Use msg_cb to report this to the
				 * caller. */
				if (msg_cb) {
					/* Make sure the message is nul
					 * terminated. */
					if ((size_t) res == *reply_len)
						res = (*reply_len) - 1;
					reply[res] = '\0';
					msg_cb(reply, res);
				}
				continue;
			}
			*reply_len = res;
			break;
		} else {
			return -2;
		}
	}
	return 0;
}
#endif /* CTRL_IFACE_SOCKET */


static int wpa_ctrl_attach_helper(struct wpa_ctrl *ctrl, int attach)
{
	char buf[10];
	int ret;
	size_t len = 10;

	ret = wpa_ctrl_request(ctrl, attach ? "ATTACH" : "DETACH", 6,
			       buf, &len, NULL);
	if (ret < 0)
		return ret;
	if (len == 3 && os_memcmp(buf, "OK\n", 3) == 0)
		return 0;
	return -1;
}


int wpa_ctrl_attach(struct wpa_ctrl *ctrl)
{
	return wpa_ctrl_attach_helper(ctrl, 1);
}


int wpa_ctrl_detach(struct wpa_ctrl *ctrl)
{
	return wpa_ctrl_attach_helper(ctrl, 0);
}


#ifdef CTRL_IFACE_SOCKET

int wpa_ctrl_recv(struct wpa_ctrl *ctrl, char *reply, size_t *reply_len)
{
	int res;

	res = recv(ctrl->s, reply, *reply_len, 0);
	if (res < 0)
		return res;
	*reply_len = res;
	return 0;
}


int wpa_ctrl_pending(struct wpa_ctrl *ctrl)
{
	struct timeval tv;
	fd_set rfds;
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	FD_ZERO(&rfds);
	FD_SET(ctrl->s, &rfds);
	select(ctrl->s + 1, &rfds, NULL, NULL, &tv);
	return FD_ISSET(ctrl->s, &rfds);
}


int wpa_ctrl_get_fd(struct wpa_ctrl *ctrl)
{
	return ctrl->s;
}

#endif /* CTRL_IFACE_SOCKET */


#ifdef CONFIG_CTRL_IFACE_NAMED_PIPE

#ifndef WPA_SUPPLICANT_NAMED_PIPE
#define WPA_SUPPLICANT_NAMED_PIPE "WpaSupplicant"
#endif
#define NAMED_PIPE_PREFIX TEXT("\\\\.\\pipe\\") TEXT(WPA_SUPPLICANT_NAMED_PIPE)

struct wpa_ctrl * wpa_ctrl_open(const char *ctrl_path)
{
	struct wpa_ctrl *ctrl;
	DWORD mode;
	TCHAR name[256];
	int i, ret;

	ctrl = os_malloc(sizeof(*ctrl));
	if (ctrl == NULL)
		return NULL;
	os_memset(ctrl, 0, sizeof(*ctrl));

#ifdef UNICODE
	if (ctrl_path == NULL)
		ret = _snwprintf(name, 256, NAMED_PIPE_PREFIX);
	else
		ret = _snwprintf(name, 256, NAMED_PIPE_PREFIX TEXT("-%S"),
				 ctrl_path);
#else /* UNICODE */
	if (ctrl_path == NULL)
		ret = os_snprintf(name, 256, NAMED_PIPE_PREFIX);
	else
		ret = os_snprintf(name, 256, NAMED_PIPE_PREFIX "-%s",
				  ctrl_path);
#endif /* UNICODE */
	if (ret < 0 || ret >= 256) {
		os_free(ctrl);
		return NULL;
	}

	for (i = 0; i < 10; i++) {
		ctrl->pipe = CreateFile(name, GENERIC_READ | GENERIC_WRITE, 0,
					NULL, OPEN_EXISTING, 0, NULL);
		/*
		 * Current named pipe server side in wpa_supplicant is
		 * re-opening the pipe for new clients only after the previous
		 * one is taken into use. This leaves a small window for race
		 * conditions when two connections are being opened at almost
		 * the same time. Retry if that was the case.
		 */
		if (ctrl->pipe != INVALID_HANDLE_VALUE ||
		    GetLastError() != ERROR_PIPE_BUSY)
			break;
		WaitNamedPipe(name, 1000);
	}
	if (ctrl->pipe == INVALID_HANDLE_VALUE) {
		os_free(ctrl);
		return NULL;
	}

	mode = PIPE_READMODE_MESSAGE;
	if (!SetNamedPipeHandleState(ctrl->pipe, &mode, NULL, NULL)) {
		CloseHandle(ctrl->pipe);
		os_free(ctrl);
		return NULL;
	}

	return ctrl;
}


void wpa_ctrl_close(struct wpa_ctrl *ctrl)
{
	CloseHandle(ctrl->pipe);
	os_free(ctrl);
}


int wpa_ctrl_request(struct wpa_ctrl *ctrl, const char *cmd, size_t cmd_len,
		     char *reply, size_t *reply_len,
		     void (*msg_cb)(char *msg, size_t len))
{
	DWORD written;
	DWORD readlen = *reply_len;

	if (!WriteFile(ctrl->pipe, cmd, cmd_len, &written, NULL))
		return -1;

	if (!ReadFile(ctrl->pipe, reply, *reply_len, &readlen, NULL))
		return -1;
	*reply_len = readlen;

	return 0;
}


int wpa_ctrl_recv(struct wpa_ctrl *ctrl, char *reply, size_t *reply_len)
{
	DWORD len = *reply_len;
	if (!ReadFile(ctrl->pipe, reply, *reply_len, &len, NULL))
		return -1;
	*reply_len = len;
	return 0;
}


int wpa_ctrl_pending(struct wpa_ctrl *ctrl)
{
	DWORD left;

	if (!PeekNamedPipe(ctrl->pipe, NULL, 0, NULL, &left, NULL))
		return -1;
	return left ? 1 : 0;
}


int wpa_ctrl_get_fd(struct wpa_ctrl *ctrl)
{
	return -1;
}

#endif /* CONFIG_CTRL_IFACE_NAMED_PIPE */

#endif /* CONFIG_CTRL_IFACE */

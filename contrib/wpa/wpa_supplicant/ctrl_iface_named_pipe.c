/*
 * WPA Supplicant / Windows Named Pipe -based control interface
 * Copyright (c) 2004-2006, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "eloop.h"
#include "config.h"
#include "eapol_supp/eapol_supp_sm.h"
#include "wpa_supplicant_i.h"
#include "ctrl_iface.h"
#include "common/wpa_ctrl.h"

#ifdef __MINGW32_VERSION
/* mingw-w32api v3.1 does not yet include sddl.h, so define needed parts here
 */
#define SDDL_REVISION_1 1
BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorA(
	LPCSTR, DWORD, PSECURITY_DESCRIPTOR *, PULONG);
BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorW(
	LPCWSTR, DWORD, PSECURITY_DESCRIPTOR *, PULONG);
#ifdef UNICODE
#define ConvertStringSecurityDescriptorToSecurityDescriptor \
ConvertStringSecurityDescriptorToSecurityDescriptorW
#else
#define ConvertStringSecurityDescriptorToSecurityDescriptor \
ConvertStringSecurityDescriptorToSecurityDescriptorA
#endif
#else /* __MINGW32_VERSION */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#endif
#include <sddl.h>
#endif /* __MINGW32_VERSION */

#ifndef WPA_SUPPLICANT_NAMED_PIPE
#define WPA_SUPPLICANT_NAMED_PIPE "WpaSupplicant"
#endif
#define NAMED_PIPE_PREFIX TEXT("\\\\.\\pipe\\") TEXT(WPA_SUPPLICANT_NAMED_PIPE)

/* Per-interface ctrl_iface */

#define REQUEST_BUFSIZE 256
#define REPLY_BUFSIZE 4096

struct ctrl_iface_priv;

/**
 * struct wpa_ctrl_dst - Internal data structure of control interface clients
 *
 * This structure is used to store information about registered control
 * interface monitors into struct wpa_supplicant. This data is private to
 * ctrl_iface_named_pipe.c and should not be touched directly from other files.
 */
struct wpa_ctrl_dst {
	/* Note: OVERLAPPED must be the first member of struct wpa_ctrl_dst */
	OVERLAPPED overlap;
	struct wpa_ctrl_dst *next, *prev;
	struct ctrl_iface_priv *priv;
	HANDLE pipe;
	int attached;
	int debug_level;
	int errors;
	char req_buf[REQUEST_BUFSIZE];
	char *rsp_buf;
	int used;
};


struct ctrl_iface_priv {
	struct wpa_supplicant *wpa_s;
	struct wpa_ctrl_dst *ctrl_dst;
	SECURITY_ATTRIBUTES attr;
	int sec_attr_set;
};


static void wpa_supplicant_ctrl_iface_send(struct ctrl_iface_priv *priv,
					   int level, const char *buf,
					   size_t len);

static void ctrl_close_pipe(struct wpa_ctrl_dst *dst);
static void wpa_supplicant_ctrl_iface_receive(void *, void *);
static VOID WINAPI ctrl_iface_read_completed(DWORD err, DWORD bytes,
					     LPOVERLAPPED overlap);

struct wpa_global_dst;
static void global_close_pipe(struct wpa_global_dst *dst);
static void wpa_supplicant_global_iface_receive(void *eloop_data,
						void *user_ctx);
static VOID WINAPI global_iface_read_completed(DWORD err, DWORD bytes,
					       LPOVERLAPPED overlap);


static int ctrl_broken_pipe(HANDLE pipe, int used)
{
	DWORD err;

	if (PeekNamedPipe(pipe, NULL, 0, NULL, NULL, NULL))
		return 0;

	err = GetLastError();
	if (err == ERROR_BROKEN_PIPE || (err == ERROR_BAD_PIPE && used))
		return 1;
	return 0;
}


static void ctrl_flush_broken_pipes(struct ctrl_iface_priv *priv)
{
	struct wpa_ctrl_dst *dst, *next;

	dst = priv->ctrl_dst;

	while (dst) {
		next = dst->next;
		if (ctrl_broken_pipe(dst->pipe, dst->used)) {
			wpa_printf(MSG_DEBUG, "CTRL: closing broken pipe %p",
				   dst);
			ctrl_close_pipe(dst);
		}
		dst = next;
	}
}


static int ctrl_open_pipe(struct ctrl_iface_priv *priv)
{
	struct wpa_ctrl_dst *dst;
	DWORD err;
	TCHAR name[256];

	dst = os_zalloc(sizeof(*dst));
	if (dst == NULL)
		return -1;
	wpa_printf(MSG_DEBUG, "CTRL: Open pipe %p", dst);

	dst->priv = priv;
	dst->debug_level = MSG_INFO;
	dst->pipe = INVALID_HANDLE_VALUE;

	dst->overlap.hEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
	if (dst->overlap.hEvent == NULL) {
		wpa_printf(MSG_ERROR, "CTRL: CreateEvent failed: %d",
			   (int) GetLastError());
		goto fail;
	}

	eloop_register_event(dst->overlap.hEvent,
			     sizeof(dst->overlap.hEvent),
			     wpa_supplicant_ctrl_iface_receive, dst, NULL);

#ifdef UNICODE
	_snwprintf(name, 256, NAMED_PIPE_PREFIX TEXT("-%S"),
		   priv->wpa_s->ifname);
#else /* UNICODE */
	os_snprintf(name, 256, NAMED_PIPE_PREFIX "-%s",
		    priv->wpa_s->ifname);
#endif /* UNICODE */

	/* TODO: add support for configuring access list for the pipe */
	dst->pipe = CreateNamedPipe(name,
				    PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
				    PIPE_TYPE_MESSAGE |
				    PIPE_READMODE_MESSAGE |
				    PIPE_WAIT,
				    15, REPLY_BUFSIZE, REQUEST_BUFSIZE,
				    1000,
				    priv->sec_attr_set ? &priv->attr : NULL);
	if (dst->pipe == INVALID_HANDLE_VALUE) {
		wpa_printf(MSG_ERROR, "CTRL: CreateNamedPipe failed: %d",
			   (int) GetLastError());
		goto fail;
	}

	if (ConnectNamedPipe(dst->pipe, &dst->overlap)) {
		wpa_printf(MSG_ERROR, "CTRL: ConnectNamedPipe failed: %d",
			   (int) GetLastError());
		CloseHandle(dst->pipe);
		os_free(dst);
		return -1;
	}

	err = GetLastError();
	switch (err) {
	case ERROR_IO_PENDING:
		wpa_printf(MSG_DEBUG, "CTRL: ConnectNamedPipe: connection in "
			   "progress");
		break;
	case ERROR_PIPE_CONNECTED:
		wpa_printf(MSG_DEBUG, "CTRL: ConnectNamedPipe: already "
			   "connected");
		if (SetEvent(dst->overlap.hEvent))
			break;
		/* fall through */
	default:
		wpa_printf(MSG_DEBUG, "CTRL: ConnectNamedPipe error: %d",
			   (int) err);
		CloseHandle(dst->pipe);
		os_free(dst);
		return -1;
	}

	dst->next = priv->ctrl_dst;
	if (dst->next)
		dst->next->prev = dst;
	priv->ctrl_dst = dst;

	return 0;

fail:
	ctrl_close_pipe(dst);
	return -1;
}


static void ctrl_close_pipe(struct wpa_ctrl_dst *dst)
{
	wpa_printf(MSG_DEBUG, "CTRL: close pipe %p", dst);

	if (dst->overlap.hEvent) {
		eloop_unregister_event(dst->overlap.hEvent,
				       sizeof(dst->overlap.hEvent));
		CloseHandle(dst->overlap.hEvent);
	}

	if (dst->pipe != INVALID_HANDLE_VALUE) {
		/*
		 * Could use FlushFileBuffers() here to guarantee that all data
		 * gets delivered to the client, but that can block, so let's
		 * not do this for now.
		 * FlushFileBuffers(dst->pipe);
		 */
		CloseHandle(dst->pipe);
	}

	if (dst->prev)
		dst->prev->next = dst->next;
	else
		dst->priv->ctrl_dst = dst->next;
	if (dst->next)
		dst->next->prev = dst->prev;

	os_free(dst->rsp_buf);
	os_free(dst);
}


static VOID WINAPI ctrl_iface_write_completed(DWORD err, DWORD bytes,
					      LPOVERLAPPED overlap)
{
	struct wpa_ctrl_dst *dst = (struct wpa_ctrl_dst *) overlap;
	wpa_printf(MSG_DEBUG, "CTRL: Overlapped write completed: dst=%p "
		   "err=%d bytes=%d", dst, (int) err, (int) bytes);
	if (err) {
		ctrl_close_pipe(dst);
		return;
	}

	os_free(dst->rsp_buf);
	dst->rsp_buf = NULL;

	if (!ReadFileEx(dst->pipe, dst->req_buf, sizeof(dst->req_buf),
			&dst->overlap, ctrl_iface_read_completed)) {
		wpa_printf(MSG_DEBUG, "CTRL: ReadFileEx failed: %d",
			   (int) GetLastError());
		ctrl_close_pipe(dst);
		return;
	}
	wpa_printf(MSG_DEBUG, "CTRL: Overlapped read started for %p", dst);
}


static void wpa_supplicant_ctrl_iface_rx(struct wpa_ctrl_dst *dst, size_t len)
{
	struct wpa_supplicant *wpa_s = dst->priv->wpa_s;
	char *reply = NULL, *send_buf;
	size_t reply_len = 0, send_len;
	int new_attached = 0;
	char *buf = dst->req_buf;

	dst->used = 1;
	if (len >= REQUEST_BUFSIZE)
		len = REQUEST_BUFSIZE - 1;
	buf[len] = '\0';

	if (os_strcmp(buf, "ATTACH") == 0) {
		dst->attached = 1;
		wpa_printf(MSG_DEBUG, "CTRL_IFACE monitor attached");
		new_attached = 1;
		reply_len = 2;
	} else if (os_strcmp(buf, "DETACH") == 0) {
		dst->attached = 0;
		wpa_printf(MSG_DEBUG, "CTRL_IFACE monitor detached");
		reply_len = 2;
	} else if (os_strncmp(buf, "LEVEL ", 6) == 0) {
		wpa_printf(MSG_DEBUG, "CTRL_IFACE LEVEL %s", buf + 6);
		dst->debug_level = atoi(buf + 6);
		reply_len = 2;
	} else {
		reply = wpa_supplicant_ctrl_iface_process(wpa_s, buf,
							  &reply_len);
	}

	if (reply) {
		send_buf = reply;
		send_len = reply_len;
	} else if (reply_len == 2) {
		send_buf = "OK\n";
		send_len = 3;
	} else {
		send_buf = "FAIL\n";
		send_len = 5;
	}

	os_free(dst->rsp_buf);
	dst->rsp_buf = os_memdup(send_buf, send_len);
	if (dst->rsp_buf == NULL) {
		ctrl_close_pipe(dst);
		os_free(reply);
		return;
	}
	os_free(reply);

	if (!WriteFileEx(dst->pipe, dst->rsp_buf, send_len, &dst->overlap,
			 ctrl_iface_write_completed)) {
		wpa_printf(MSG_DEBUG, "CTRL: WriteFileEx failed: %d",
			   (int) GetLastError());
		ctrl_close_pipe(dst);
	} else {
		wpa_printf(MSG_DEBUG, "CTRL: Overlapped write started for %p",
			   dst);
	}

	if (new_attached)
		eapol_sm_notify_ctrl_attached(wpa_s->eapol);
}


static VOID WINAPI ctrl_iface_read_completed(DWORD err, DWORD bytes,
					     LPOVERLAPPED overlap)
{
	struct wpa_ctrl_dst *dst = (struct wpa_ctrl_dst *) overlap;
	wpa_printf(MSG_DEBUG, "CTRL: Overlapped read completed: dst=%p err=%d "
		   "bytes=%d", dst, (int) err, (int) bytes);
	if (err == 0 && bytes > 0)
		wpa_supplicant_ctrl_iface_rx(dst, bytes);
}


static void wpa_supplicant_ctrl_iface_receive(void *eloop_data, void *user_ctx)
{
	struct wpa_ctrl_dst *dst = eloop_data;
	struct ctrl_iface_priv *priv = dst->priv;
	DWORD bytes;

	wpa_printf(MSG_DEBUG, "CTRL: wpa_supplicant_ctrl_iface_receive");
	ResetEvent(dst->overlap.hEvent);

	if (!GetOverlappedResult(dst->pipe, &dst->overlap, &bytes, FALSE)) {
		wpa_printf(MSG_DEBUG, "CTRL: GetOverlappedResult failed: %d",
			   (int) GetLastError());
		return;
	}
	wpa_printf(MSG_DEBUG, "CTRL: GetOverlappedResult: New client "
		   "connected");

	/* Open a new named pipe for the next client. */
	ctrl_open_pipe(priv);

	/* Use write completion function to start reading a command */
	ctrl_iface_write_completed(0, 0, &dst->overlap);

	ctrl_flush_broken_pipes(priv);
}


static int ctrl_iface_parse(struct ctrl_iface_priv *priv, const char *params)
{
	const char *sddl = NULL;
	TCHAR *t_sddl;

	if (os_strncmp(params, "SDDL=", 5) == 0)
		sddl = params + 5;
	if (!sddl) {
		sddl = os_strstr(params, " SDDL=");
		if (sddl)
			sddl += 6;
	}

	if (!sddl)
		return 0;

	wpa_printf(MSG_DEBUG, "CTRL: SDDL='%s'", sddl);
	os_memset(&priv->attr, 0, sizeof(priv->attr));
	priv->attr.nLength = sizeof(priv->attr);
	priv->attr.bInheritHandle = FALSE;
	t_sddl = wpa_strdup_tchar(sddl);
	if (t_sddl == NULL)
		return -1;
	if (!ConvertStringSecurityDescriptorToSecurityDescriptor(
		    t_sddl, SDDL_REVISION_1,
		    (PSECURITY_DESCRIPTOR *) (void *)
		    &priv->attr.lpSecurityDescriptor,
		    NULL)) {
		os_free(t_sddl);
		wpa_printf(MSG_ERROR, "CTRL: SDDL='%s' - could not convert to "
			   "security descriptor: %d",
			   sddl, (int) GetLastError());
		return -1;
	}
	os_free(t_sddl);

	priv->sec_attr_set = 1;

	return 0;
}


static void wpa_supplicant_ctrl_iface_msg_cb(void *ctx, int level,
					     enum wpa_msg_type type,
					     const char *txt, size_t len)
{
	struct wpa_supplicant *wpa_s = ctx;
	if (wpa_s == NULL || wpa_s->ctrl_iface == NULL)
		return;
	wpa_supplicant_ctrl_iface_send(wpa_s->ctrl_iface, level, txt, len);
}


struct ctrl_iface_priv *
wpa_supplicant_ctrl_iface_init(struct wpa_supplicant *wpa_s)
{
	struct ctrl_iface_priv *priv;

	priv = os_zalloc(sizeof(*priv));
	if (priv == NULL)
		return NULL;
	priv->wpa_s = wpa_s;

	if (wpa_s->conf->ctrl_interface == NULL)
		return priv;

	if (ctrl_iface_parse(priv, wpa_s->conf->ctrl_interface) < 0) {
		os_free(priv);
		return NULL;
	}

	if (ctrl_open_pipe(priv) < 0) {
		os_free(priv);
		return NULL;
	}

	wpa_msg_register_cb(wpa_supplicant_ctrl_iface_msg_cb);

	return priv;
}


void wpa_supplicant_ctrl_iface_deinit(struct ctrl_iface_priv *priv)
{
	while (priv->ctrl_dst)
		ctrl_close_pipe(priv->ctrl_dst);
	if (priv->sec_attr_set)
		LocalFree(priv->attr.lpSecurityDescriptor);
	os_free(priv);
}


static void wpa_supplicant_ctrl_iface_send(struct ctrl_iface_priv *priv,
					   int level, const char *buf,
					   size_t len)
{
	struct wpa_ctrl_dst *dst, *next;
	char levelstr[10];
	int idx;
	char *sbuf;
	int llen;
	DWORD written;

	dst = priv->ctrl_dst;
	if (dst == NULL)
		return;

	os_snprintf(levelstr, sizeof(levelstr), "<%d>", level);

	llen = os_strlen(levelstr);
	sbuf = os_malloc(llen + len);
	if (sbuf == NULL)
		return;

	os_memcpy(sbuf, levelstr, llen);
	os_memcpy(sbuf + llen, buf, len);

	idx = 0;
	while (dst) {
		next = dst->next;
		if (dst->attached && level >= dst->debug_level) {
			wpa_printf(MSG_DEBUG, "CTRL_IFACE monitor send %p",
				   dst);
			if (!WriteFile(dst->pipe, sbuf, llen + len, &written,
				       NULL)) {
				wpa_printf(MSG_DEBUG, "CTRL: WriteFile to dst "
					   "%p failed: %d",
					   dst, (int) GetLastError());
				dst->errors++;
				if (dst->errors > 10)
					ctrl_close_pipe(dst);
			} else
				dst->errors = 0;
		}
		idx++;
		dst = next;
	}
	os_free(sbuf);
}


void wpa_supplicant_ctrl_iface_wait(struct ctrl_iface_priv *priv)
{
	wpa_printf(MSG_DEBUG, "CTRL_IFACE - %s - wait for monitor",
		   priv->wpa_s->ifname);
	if (priv->ctrl_dst == NULL)
		return;
	WaitForSingleObject(priv->ctrl_dst->pipe, INFINITE);
}


/* Global ctrl_iface */

struct ctrl_iface_global_priv;

struct wpa_global_dst {
	/* Note: OVERLAPPED must be the first member of struct wpa_global_dst
	 */
	OVERLAPPED overlap;
	struct wpa_global_dst *next, *prev;
	struct ctrl_iface_global_priv *priv;
	HANDLE pipe;
	char req_buf[REQUEST_BUFSIZE];
	char *rsp_buf;
	int used;
};

struct ctrl_iface_global_priv {
	struct wpa_global *global;
	struct wpa_global_dst *ctrl_dst;
};


static void global_flush_broken_pipes(struct ctrl_iface_global_priv *priv)
{
	struct wpa_global_dst *dst, *next;

	dst = priv->ctrl_dst;

	while (dst) {
		next = dst->next;
		if (ctrl_broken_pipe(dst->pipe, dst->used)) {
			wpa_printf(MSG_DEBUG, "CTRL: closing broken pipe %p",
				   dst);
			global_close_pipe(dst);
		}
		dst = next;
	}
}


static int global_open_pipe(struct ctrl_iface_global_priv *priv)
{
	struct wpa_global_dst *dst;
	DWORD err;

	dst = os_zalloc(sizeof(*dst));
	if (dst == NULL)
		return -1;
	wpa_printf(MSG_DEBUG, "CTRL: Open pipe %p", dst);

	dst->priv = priv;
	dst->pipe = INVALID_HANDLE_VALUE;

	dst->overlap.hEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
	if (dst->overlap.hEvent == NULL) {
		wpa_printf(MSG_ERROR, "CTRL: CreateEvent failed: %d",
			   (int) GetLastError());
		goto fail;
	}

	eloop_register_event(dst->overlap.hEvent,
			     sizeof(dst->overlap.hEvent),
			     wpa_supplicant_global_iface_receive, dst, NULL);

	/* TODO: add support for configuring access list for the pipe */
	dst->pipe = CreateNamedPipe(NAMED_PIPE_PREFIX,
				    PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
				    PIPE_TYPE_MESSAGE |
				    PIPE_READMODE_MESSAGE |
				    PIPE_WAIT,
				    10, REPLY_BUFSIZE, REQUEST_BUFSIZE,
				    1000, NULL);
	if (dst->pipe == INVALID_HANDLE_VALUE) {
		wpa_printf(MSG_ERROR, "CTRL: CreateNamedPipe failed: %d",
			   (int) GetLastError());
		goto fail;
	}

	if (ConnectNamedPipe(dst->pipe, &dst->overlap)) {
		wpa_printf(MSG_ERROR, "CTRL: ConnectNamedPipe failed: %d",
			   (int) GetLastError());
		CloseHandle(dst->pipe);
		os_free(dst);
		return -1;
	}

	err = GetLastError();
	switch (err) {
	case ERROR_IO_PENDING:
		wpa_printf(MSG_DEBUG, "CTRL: ConnectNamedPipe: connection in "
			   "progress");
		break;
	case ERROR_PIPE_CONNECTED:
		wpa_printf(MSG_DEBUG, "CTRL: ConnectNamedPipe: already "
			   "connected");
		if (SetEvent(dst->overlap.hEvent))
			break;
		/* fall through */
	default:
		wpa_printf(MSG_DEBUG, "CTRL: ConnectNamedPipe error: %d",
			   (int) err);
		CloseHandle(dst->pipe);
		os_free(dst);
		return -1;
	}

	dst->next = priv->ctrl_dst;
	if (dst->next)
		dst->next->prev = dst;
	priv->ctrl_dst = dst;

	return 0;

fail:
	global_close_pipe(dst);
	return -1;
}


static void global_close_pipe(struct wpa_global_dst *dst)
{
	wpa_printf(MSG_DEBUG, "CTRL: close pipe %p", dst);

	if (dst->overlap.hEvent) {
		eloop_unregister_event(dst->overlap.hEvent,
				       sizeof(dst->overlap.hEvent));
		CloseHandle(dst->overlap.hEvent);
	}

	if (dst->pipe != INVALID_HANDLE_VALUE) {
		/*
		 * Could use FlushFileBuffers() here to guarantee that all data
		 * gets delivered to the client, but that can block, so let's
		 * not do this for now.
		 * FlushFileBuffers(dst->pipe);
		 */
		CloseHandle(dst->pipe);
	}

	if (dst->prev)
		dst->prev->next = dst->next;
	else
		dst->priv->ctrl_dst = dst->next;
	if (dst->next)
		dst->next->prev = dst->prev;

	os_free(dst->rsp_buf);
	os_free(dst);
}


static VOID WINAPI global_iface_write_completed(DWORD err, DWORD bytes,
						LPOVERLAPPED overlap)
{
	struct wpa_global_dst *dst = (struct wpa_global_dst *) overlap;
	wpa_printf(MSG_DEBUG, "CTRL: Overlapped write completed: dst=%p "
		   "err=%d bytes=%d", dst, (int) err, (int) bytes);
	if (err) {
		global_close_pipe(dst);
		return;
	}

	os_free(dst->rsp_buf);
	dst->rsp_buf = NULL;

	if (!ReadFileEx(dst->pipe, dst->req_buf, sizeof(dst->req_buf),
			&dst->overlap, global_iface_read_completed)) {
		wpa_printf(MSG_DEBUG, "CTRL: ReadFileEx failed: %d",
			   (int) GetLastError());
		global_close_pipe(dst);
		/* FIX: if this was the pipe waiting for new global
		 * connections, at this point there are no open global pipes..
		 * Should try to open a new pipe.. */
		return;
	}
	wpa_printf(MSG_DEBUG, "CTRL: Overlapped read started for %p", dst);
}


static void wpa_supplicant_global_iface_rx(struct wpa_global_dst *dst,
					   size_t len)
{
	struct wpa_global *global = dst->priv->global;
	char *reply = NULL, *send_buf;
	size_t reply_len = 0, send_len;
	char *buf = dst->req_buf;

	dst->used = 1;
	if (len >= REQUEST_BUFSIZE)
		len = REQUEST_BUFSIZE - 1;
	buf[len] = '\0';

	reply = wpa_supplicant_global_ctrl_iface_process(global, buf,
							 &reply_len);
	if (reply) {
		send_buf = reply;
		send_len = reply_len;
	} else if (reply_len) {
		send_buf = "FAIL\n";
		send_len = 5;
	} else {
		os_free(dst->rsp_buf);
		dst->rsp_buf = NULL;
		return;
	}

	os_free(dst->rsp_buf);
	dst->rsp_buf = os_memdup(send_buf, send_len);
	if (dst->rsp_buf == NULL) {
		global_close_pipe(dst);
		os_free(reply);
		return;
	}
	os_free(reply);

	if (!WriteFileEx(dst->pipe, dst->rsp_buf, send_len, &dst->overlap,
			 global_iface_write_completed)) {
		wpa_printf(MSG_DEBUG, "CTRL: WriteFileEx failed: %d",
			   (int) GetLastError());
		global_close_pipe(dst);
	} else {
		wpa_printf(MSG_DEBUG, "CTRL: Overlapped write started for %p",
			   dst);
	}
}


static VOID WINAPI global_iface_read_completed(DWORD err, DWORD bytes,
					       LPOVERLAPPED overlap)
{
	struct wpa_global_dst *dst = (struct wpa_global_dst *) overlap;
	wpa_printf(MSG_DEBUG, "CTRL: Overlapped read completed: dst=%p err=%d "
		   "bytes=%d", dst, (int) err, (int) bytes);
	if (err == 0 && bytes > 0)
		wpa_supplicant_global_iface_rx(dst, bytes);
}


static void wpa_supplicant_global_iface_receive(void *eloop_data,
						void *user_ctx)
{
	struct wpa_global_dst *dst = eloop_data;
	struct ctrl_iface_global_priv *priv = dst->priv;
	DWORD bytes;

	wpa_printf(MSG_DEBUG, "CTRL: wpa_supplicant_global_iface_receive");
	ResetEvent(dst->overlap.hEvent);

	if (!GetOverlappedResult(dst->pipe, &dst->overlap, &bytes, FALSE)) {
		wpa_printf(MSG_DEBUG, "CTRL: GetOverlappedResult failed: %d",
			   (int) GetLastError());
		return;
	}
	wpa_printf(MSG_DEBUG, "CTRL: GetOverlappedResult: New client "
		   "connected");

	/* Open a new named pipe for the next client. */
	if (global_open_pipe(priv) < 0) {
		wpa_printf(MSG_DEBUG, "CTRL: global_open_pipe failed");
		return;
	}

	/* Use write completion function to start reading a command */
	global_iface_write_completed(0, 0, &dst->overlap);

	global_flush_broken_pipes(priv);
}


struct ctrl_iface_global_priv *
wpa_supplicant_global_ctrl_iface_init(struct wpa_global *global)
{
	struct ctrl_iface_global_priv *priv;

	priv = os_zalloc(sizeof(*priv));
	if (priv == NULL)
		return NULL;
	priv->global = global;

	if (global_open_pipe(priv) < 0) {
		os_free(priv);
		return NULL;
	}

	return priv;
}


void
wpa_supplicant_global_ctrl_iface_deinit(struct ctrl_iface_global_priv *priv)
{
	while (priv->ctrl_dst)
		global_close_pipe(priv->ctrl_dst);
	os_free(priv);
}

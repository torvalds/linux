/*
 * Event loop based on Windows events and WaitForMultipleObjects
 * Copyright (c) 2002-2006, Jouni Malinen <j@w1.fi>
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
#include <winsock2.h>

#include "common.h"
#include "eloop.h"


struct eloop_sock {
	int sock;
	void *eloop_data;
	void *user_data;
	eloop_sock_handler handler;
	WSAEVENT event;
};

struct eloop_event {
	void *eloop_data;
	void *user_data;
	eloop_event_handler handler;
	HANDLE event;
};

struct eloop_timeout {
	struct os_time time;
	void *eloop_data;
	void *user_data;
	eloop_timeout_handler handler;
	struct eloop_timeout *next;
};

struct eloop_signal {
	int sig;
	void *user_data;
	eloop_signal_handler handler;
	int signaled;
};

struct eloop_data {
	int max_sock;
	size_t reader_count;
	struct eloop_sock *readers;

	size_t event_count;
	struct eloop_event *events;

	struct eloop_timeout *timeout;

	int signal_count;
	struct eloop_signal *signals;
	int signaled;
	int pending_terminate;

	int terminate;
	int reader_table_changed;

	struct eloop_signal term_signal;
	HANDLE term_event;

	HANDLE *handles;
	size_t num_handles;
};

static struct eloop_data eloop;


int eloop_init(void)
{
	os_memset(&eloop, 0, sizeof(eloop));
	eloop.num_handles = 1;
	eloop.handles = os_malloc(eloop.num_handles *
				  sizeof(eloop.handles[0]));
	if (eloop.handles == NULL)
		return -1;

	eloop.term_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (eloop.term_event == NULL) {
		printf("CreateEvent() failed: %d\n",
		       (int) GetLastError());
		os_free(eloop.handles);
		return -1;
	}

	return 0;
}


static int eloop_prepare_handles(void)
{
	HANDLE *n;

	if (eloop.num_handles > eloop.reader_count + eloop.event_count + 8)
		return 0;
	n = os_realloc(eloop.handles,
		       eloop.num_handles * 2 * sizeof(eloop.handles[0]));
	if (n == NULL)
		return -1;
	eloop.handles = n;
	eloop.num_handles *= 2;
	return 0;
}


int eloop_register_read_sock(int sock, eloop_sock_handler handler,
			     void *eloop_data, void *user_data)
{
	WSAEVENT event;
	struct eloop_sock *tmp;

	if (eloop_prepare_handles())
		return -1;

	event = WSACreateEvent();
	if (event == WSA_INVALID_EVENT) {
		printf("WSACreateEvent() failed: %d\n", WSAGetLastError());
		return -1;
	}

	if (WSAEventSelect(sock, event, FD_READ)) {
		printf("WSAEventSelect() failed: %d\n", WSAGetLastError());
		WSACloseEvent(event);
		return -1;
	}
	tmp = os_realloc(eloop.readers,
			 (eloop.reader_count + 1) * sizeof(struct eloop_sock));
	if (tmp == NULL) {
		WSAEventSelect(sock, event, 0);
		WSACloseEvent(event);
		return -1;
	}

	tmp[eloop.reader_count].sock = sock;
	tmp[eloop.reader_count].eloop_data = eloop_data;
	tmp[eloop.reader_count].user_data = user_data;
	tmp[eloop.reader_count].handler = handler;
	tmp[eloop.reader_count].event = event;
	eloop.reader_count++;
	eloop.readers = tmp;
	if (sock > eloop.max_sock)
		eloop.max_sock = sock;
	eloop.reader_table_changed = 1;

	return 0;
}


void eloop_unregister_read_sock(int sock)
{
	size_t i;

	if (eloop.readers == NULL || eloop.reader_count == 0)
		return;

	for (i = 0; i < eloop.reader_count; i++) {
		if (eloop.readers[i].sock == sock)
			break;
	}
	if (i == eloop.reader_count)
		return;

	WSAEventSelect(eloop.readers[i].sock, eloop.readers[i].event, 0);
	WSACloseEvent(eloop.readers[i].event);

	if (i != eloop.reader_count - 1) {
		os_memmove(&eloop.readers[i], &eloop.readers[i + 1],
			   (eloop.reader_count - i - 1) *
			   sizeof(struct eloop_sock));
	}
	eloop.reader_count--;
	eloop.reader_table_changed = 1;
}


int eloop_register_event(void *event, size_t event_size,
			 eloop_event_handler handler,
			 void *eloop_data, void *user_data)
{
	struct eloop_event *tmp;
	HANDLE h = event;

	if (event_size != sizeof(HANDLE) || h == INVALID_HANDLE_VALUE)
		return -1;

	if (eloop_prepare_handles())
		return -1;

	tmp = os_realloc(eloop.events,
			 (eloop.event_count + 1) * sizeof(struct eloop_event));
	if (tmp == NULL)
		return -1;

	tmp[eloop.event_count].eloop_data = eloop_data;
	tmp[eloop.event_count].user_data = user_data;
	tmp[eloop.event_count].handler = handler;
	tmp[eloop.event_count].event = h;
	eloop.event_count++;
	eloop.events = tmp;

	return 0;
}


void eloop_unregister_event(void *event, size_t event_size)
{
	size_t i;
	HANDLE h = event;

	if (eloop.events == NULL || eloop.event_count == 0 ||
	    event_size != sizeof(HANDLE))
		return;

	for (i = 0; i < eloop.event_count; i++) {
		if (eloop.events[i].event == h)
			break;
	}
	if (i == eloop.event_count)
		return;

	if (i != eloop.event_count - 1) {
		os_memmove(&eloop.events[i], &eloop.events[i + 1],
			   (eloop.event_count - i - 1) *
			   sizeof(struct eloop_event));
	}
	eloop.event_count--;
}


int eloop_register_timeout(unsigned int secs, unsigned int usecs,
			   eloop_timeout_handler handler,
			   void *eloop_data, void *user_data)
{
	struct eloop_timeout *timeout, *tmp, *prev;
	os_time_t now_sec;

	timeout = os_malloc(sizeof(*timeout));
	if (timeout == NULL)
		return -1;
	os_get_time(&timeout->time);
	now_sec = timeout->time.sec;
	timeout->time.sec += secs;
	if (timeout->time.sec < now_sec) {
		/*
		 * Integer overflow - assume long enough timeout to be assumed
		 * to be infinite, i.e., the timeout would never happen.
		 */
		wpa_printf(MSG_DEBUG, "ELOOP: Too long timeout (secs=%u) to "
			   "ever happen - ignore it", secs);
		os_free(timeout);
		return 0;
	}
	timeout->time.usec += usecs;
	while (timeout->time.usec >= 1000000) {
		timeout->time.sec++;
		timeout->time.usec -= 1000000;
	}
	timeout->eloop_data = eloop_data;
	timeout->user_data = user_data;
	timeout->handler = handler;
	timeout->next = NULL;

	if (eloop.timeout == NULL) {
		eloop.timeout = timeout;
		return 0;
	}

	prev = NULL;
	tmp = eloop.timeout;
	while (tmp != NULL) {
		if (os_time_before(&timeout->time, &tmp->time))
			break;
		prev = tmp;
		tmp = tmp->next;
	}

	if (prev == NULL) {
		timeout->next = eloop.timeout;
		eloop.timeout = timeout;
	} else {
		timeout->next = prev->next;
		prev->next = timeout;
	}

	return 0;
}


int eloop_cancel_timeout(eloop_timeout_handler handler,
			 void *eloop_data, void *user_data)
{
	struct eloop_timeout *timeout, *prev, *next;
	int removed = 0;

	prev = NULL;
	timeout = eloop.timeout;
	while (timeout != NULL) {
		next = timeout->next;

		if (timeout->handler == handler &&
		    (timeout->eloop_data == eloop_data ||
		     eloop_data == ELOOP_ALL_CTX) &&
		    (timeout->user_data == user_data ||
		     user_data == ELOOP_ALL_CTX)) {
			if (prev == NULL)
				eloop.timeout = next;
			else
				prev->next = next;
			os_free(timeout);
			removed++;
		} else
			prev = timeout;

		timeout = next;
	}

	return removed;
}


int eloop_is_timeout_registered(eloop_timeout_handler handler,
				void *eloop_data, void *user_data)
{
	struct eloop_timeout *tmp;

	tmp = eloop.timeout;
	while (tmp != NULL) {
		if (tmp->handler == handler &&
		    tmp->eloop_data == eloop_data &&
		    tmp->user_data == user_data)
			return 1;

		tmp = tmp->next;
	}

	return 0;
}


/* TODO: replace with suitable signal handler */
#if 0
static void eloop_handle_signal(int sig)
{
	int i;

	eloop.signaled++;
	for (i = 0; i < eloop.signal_count; i++) {
		if (eloop.signals[i].sig == sig) {
			eloop.signals[i].signaled++;
			break;
		}
	}
}
#endif


static void eloop_process_pending_signals(void)
{
	int i;

	if (eloop.signaled == 0)
		return;
	eloop.signaled = 0;

	if (eloop.pending_terminate) {
		eloop.pending_terminate = 0;
	}

	for (i = 0; i < eloop.signal_count; i++) {
		if (eloop.signals[i].signaled) {
			eloop.signals[i].signaled = 0;
			eloop.signals[i].handler(eloop.signals[i].sig,
						 eloop.signals[i].user_data);
		}
	}

	if (eloop.term_signal.signaled) {
		eloop.term_signal.signaled = 0;
		eloop.term_signal.handler(eloop.term_signal.sig,
					  eloop.term_signal.user_data);
	}
}


int eloop_register_signal(int sig, eloop_signal_handler handler,
			  void *user_data)
{
	struct eloop_signal *tmp;

	tmp = os_realloc(eloop.signals,
			 (eloop.signal_count + 1) *
			 sizeof(struct eloop_signal));
	if (tmp == NULL)
		return -1;

	tmp[eloop.signal_count].sig = sig;
	tmp[eloop.signal_count].user_data = user_data;
	tmp[eloop.signal_count].handler = handler;
	tmp[eloop.signal_count].signaled = 0;
	eloop.signal_count++;
	eloop.signals = tmp;

	/* TODO: register signal handler */

	return 0;
}


#ifndef _WIN32_WCE
static BOOL eloop_handle_console_ctrl(DWORD type)
{
	switch (type) {
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
		eloop.signaled++;
		eloop.term_signal.signaled++;
		SetEvent(eloop.term_event);
		return TRUE;
	default:
		return FALSE;
	}
}
#endif /* _WIN32_WCE */


int eloop_register_signal_terminate(eloop_signal_handler handler,
				    void *user_data)
{
#ifndef _WIN32_WCE
	if (SetConsoleCtrlHandler((PHANDLER_ROUTINE) eloop_handle_console_ctrl,
				  TRUE) == 0) {
		printf("SetConsoleCtrlHandler() failed: %d\n",
		       (int) GetLastError());
		return -1;
	}
#endif /* _WIN32_WCE */

	eloop.term_signal.handler = handler;
	eloop.term_signal.user_data = user_data;
		
	return 0;
}


int eloop_register_signal_reconfig(eloop_signal_handler handler,
				   void *user_data)
{
	/* TODO */
	return 0;
}


void eloop_run(void)
{
	struct os_time tv, now;
	DWORD count, ret, timeout, err;
	size_t i;

	while (!eloop.terminate &&
	       (eloop.timeout || eloop.reader_count > 0 ||
		eloop.event_count > 0)) {
		tv.sec = tv.usec = 0;
		if (eloop.timeout) {
			os_get_time(&now);
			if (os_time_before(&now, &eloop.timeout->time))
				os_time_sub(&eloop.timeout->time, &now, &tv);
		}

		count = 0;
		for (i = 0; i < eloop.event_count; i++)
			eloop.handles[count++] = eloop.events[i].event;

		for (i = 0; i < eloop.reader_count; i++)
			eloop.handles[count++] = eloop.readers[i].event;

		if (eloop.term_event)
			eloop.handles[count++] = eloop.term_event;

		if (eloop.timeout)
			timeout = tv.sec * 1000 + tv.usec / 1000;
		else
			timeout = INFINITE;

		if (count > MAXIMUM_WAIT_OBJECTS) {
			printf("WaitForMultipleObjects: Too many events: "
			       "%d > %d (ignoring extra events)\n",
			       (int) count, MAXIMUM_WAIT_OBJECTS);
			count = MAXIMUM_WAIT_OBJECTS;
		}
#ifdef _WIN32_WCE
		ret = WaitForMultipleObjects(count, eloop.handles, FALSE,
					     timeout);
#else /* _WIN32_WCE */
		ret = WaitForMultipleObjectsEx(count, eloop.handles, FALSE,
					       timeout, TRUE);
#endif /* _WIN32_WCE */
		err = GetLastError();

		eloop_process_pending_signals();

		/* check if some registered timeouts have occurred */
		if (eloop.timeout) {
			struct eloop_timeout *tmp;

			os_get_time(&now);
			if (!os_time_before(&now, &eloop.timeout->time)) {
				tmp = eloop.timeout;
				eloop.timeout = eloop.timeout->next;
				tmp->handler(tmp->eloop_data,
					     tmp->user_data);
				os_free(tmp);
			}

		}

		if (ret == WAIT_FAILED) {
			printf("WaitForMultipleObjects(count=%d) failed: %d\n",
			       (int) count, (int) err);
			os_sleep(1, 0);
			continue;
		}

#ifndef _WIN32_WCE
		if (ret == WAIT_IO_COMPLETION)
			continue;
#endif /* _WIN32_WCE */

		if (ret == WAIT_TIMEOUT)
			continue;

		while (ret >= WAIT_OBJECT_0 &&
		       ret < WAIT_OBJECT_0 + eloop.event_count) {
			eloop.events[ret].handler(
				eloop.events[ret].eloop_data,
				eloop.events[ret].user_data);
			ret = WaitForMultipleObjects(eloop.event_count,
						     eloop.handles, FALSE, 0);
		}

		eloop.reader_table_changed = 0;
		for (i = 0; i < eloop.reader_count; i++) {
			WSANETWORKEVENTS events;
			if (WSAEnumNetworkEvents(eloop.readers[i].sock,
						 eloop.readers[i].event,
						 &events) == 0 &&
			    (events.lNetworkEvents & FD_READ)) {
				eloop.readers[i].handler(
					eloop.readers[i].sock,
					eloop.readers[i].eloop_data,
					eloop.readers[i].user_data);
				if (eloop.reader_table_changed)
					break;
			}
		}
	}
}


void eloop_terminate(void)
{
	eloop.terminate = 1;
	SetEvent(eloop.term_event);
}


void eloop_destroy(void)
{
	struct eloop_timeout *timeout, *prev;

	timeout = eloop.timeout;
	while (timeout != NULL) {
		prev = timeout;
		timeout = timeout->next;
		os_free(prev);
	}
	os_free(eloop.readers);
	os_free(eloop.signals);
	if (eloop.term_event)
		CloseHandle(eloop.term_event);
	os_free(eloop.handles);
	eloop.handles = NULL;
	os_free(eloop.events);
	eloop.events = NULL;
}


int eloop_terminated(void)
{
	return eloop.terminate;
}


void eloop_wait_for_read_sock(int sock)
{
	WSAEVENT event;

	event = WSACreateEvent();
	if (event == WSA_INVALID_EVENT) {
		printf("WSACreateEvent() failed: %d\n", WSAGetLastError());
		return;
	}

	if (WSAEventSelect(sock, event, FD_READ)) {
		printf("WSAEventSelect() failed: %d\n", WSAGetLastError());
		WSACloseEvent(event);
		return ;
	}

	WaitForSingleObject(event, INFINITE);
	WSAEventSelect(sock, event, 0);
	WSACloseEvent(event);
}

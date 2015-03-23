/*
 * WPA Supplicant - Layer2 packet handling with Microsoft NDISUIO
 * Copyright (c) 2003-2006, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 *
 * This implementation requires Windows specific event loop implementation,
 * i.e., eloop_win.c. In addition, the NDISUIO connection is shared with
 * driver_ndis.c, so only that driver interface can be used and
 * CONFIG_USE_NDISUIO must be defined.
 *
 * WinXP version of the code uses overlapped I/O and a single threaded design
 * with callback functions from I/O code. WinCE version uses a separate RX
 * thread that blocks on ReadFile() whenever the media status is connected.
 */

#include "includes.h"
#include <winsock2.h>
#include <ntddndis.h>

#ifdef _WIN32_WCE
#include <winioctl.h>
#include <nuiouser.h>
#endif /* _WIN32_WCE */

#include "common.h"
#include "eloop.h"
#include "l2_packet.h"

#ifndef _WIN32_WCE
/* from nuiouser.h */
#define FSCTL_NDISUIO_BASE      FILE_DEVICE_NETWORK
#define _NDISUIO_CTL_CODE(_Function, _Method, _Access) \
	CTL_CODE(FSCTL_NDISUIO_BASE, _Function, _Method, _Access)
#define IOCTL_NDISUIO_SET_ETHER_TYPE \
	_NDISUIO_CTL_CODE(0x202, METHOD_BUFFERED, \
			  FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#endif /* _WIN32_WCE */

/* From driver_ndis.c to shared the handle to NDISUIO */
HANDLE driver_ndis_get_ndisuio_handle(void);

/*
 * NDISUIO supports filtering of only one ethertype at the time, so we must
 * fake support for two (EAPOL and RSN pre-auth) by switching to pre-auth
 * whenever wpa_supplicant is trying to pre-authenticate and then switching
 * back to EAPOL when pre-authentication has been completed.
 */

struct l2_packet_data;

struct l2_packet_ndisuio_global {
	int refcount;
	unsigned short first_proto;
	struct l2_packet_data *l2[2];
#ifdef _WIN32_WCE
	HANDLE rx_thread;
	HANDLE stop_request;
	HANDLE ready_for_read;
	HANDLE rx_processed;
#endif /* _WIN32_WCE */
};

static struct l2_packet_ndisuio_global *l2_ndisuio_global = NULL;

struct l2_packet_data {
	char ifname[100];
	u8 own_addr[ETH_ALEN];
	void (*rx_callback)(void *ctx, const u8 *src_addr,
			    const u8 *buf, size_t len);
	void *rx_callback_ctx;
	int l2_hdr; /* whether to include layer 2 (Ethernet) header in calls to
		     * rx_callback and l2_packet_send() */
	HANDLE rx_avail;
#ifndef _WIN32_WCE
	OVERLAPPED rx_overlapped;
#endif /* _WIN32_WCE */
	u8 rx_buf[1514];
	DWORD rx_written;
};


int l2_packet_get_own_addr(struct l2_packet_data *l2, u8 *addr)
{
	os_memcpy(addr, l2->own_addr, ETH_ALEN);
	return 0;
}


int l2_packet_send(struct l2_packet_data *l2, const u8 *dst_addr, u16 proto,
		   const u8 *buf, size_t len)
{
	BOOL res;
	DWORD written;
	struct l2_ethhdr *eth;
#ifndef _WIN32_WCE
	OVERLAPPED overlapped;
#endif /* _WIN32_WCE */
	OVERLAPPED *o;

	if (l2 == NULL)
		return -1;

#ifdef _WIN32_WCE
	o = NULL;
#else /* _WIN32_WCE */
	os_memset(&overlapped, 0, sizeof(overlapped));
	o = &overlapped;
#endif /* _WIN32_WCE */

	if (l2->l2_hdr) {
		res = WriteFile(driver_ndis_get_ndisuio_handle(), buf, len,
				&written, o);
	} else {
		size_t mlen = sizeof(*eth) + len;
		eth = os_malloc(mlen);
		if (eth == NULL)
			return -1;

		os_memcpy(eth->h_dest, dst_addr, ETH_ALEN);
		os_memcpy(eth->h_source, l2->own_addr, ETH_ALEN);
		eth->h_proto = htons(proto);
		os_memcpy(eth + 1, buf, len);
		res = WriteFile(driver_ndis_get_ndisuio_handle(), eth, mlen,
				&written, o);
		os_free(eth);
	}

	if (!res) {
		DWORD err = GetLastError();
#ifndef _WIN32_WCE
		if (err == ERROR_IO_PENDING) {
			wpa_printf(MSG_DEBUG, "L2(NDISUIO): Wait for pending "
				   "write to complete");
			res = GetOverlappedResult(
				driver_ndis_get_ndisuio_handle(), &overlapped,
				&written, TRUE);
			if (!res) {
				wpa_printf(MSG_DEBUG, "L2(NDISUIO): "
					   "GetOverlappedResult failed: %d",
					   (int) GetLastError());
				return -1;
			}
			return 0;
		}
#endif /* _WIN32_WCE */
		wpa_printf(MSG_DEBUG, "L2(NDISUIO): WriteFile failed: %d",
			   (int) GetLastError());
		return -1;
	}

	return 0;
}


static void l2_packet_callback(struct l2_packet_data *l2);

#ifdef _WIN32_WCE
static void l2_packet_rx_thread_try_read(struct l2_packet_data *l2)
{
	HANDLE handles[2];

	wpa_printf(MSG_MSGDUMP, "l2_packet_rx_thread: -> ReadFile");
	if (!ReadFile(driver_ndis_get_ndisuio_handle(), l2->rx_buf,
		      sizeof(l2->rx_buf), &l2->rx_written, NULL)) {
		DWORD err = GetLastError();
		wpa_printf(MSG_DEBUG, "l2_packet_rx_thread: ReadFile failed: "
			   "%d", (int) err);
		/*
		 * ReadFile on NDISUIO/WinCE returns ERROR_DEVICE_NOT_CONNECTED
		 * error whenever the connection is not up. Yield the thread to
		 * avoid triggering a busy loop. Connection event should stop
		 * us from looping for long, but we need to allow enough CPU
		 * for the main thread to process the media disconnection.
		 */
		Sleep(100);
		return;
	}

	wpa_printf(MSG_DEBUG, "l2_packet_rx_thread: Read %d byte packet",
		   (int) l2->rx_written);

	/*
	 * Notify the main thread about the availability of a frame and wait
	 * for the frame to be processed.
	 */
	SetEvent(l2->rx_avail);
	handles[0] = l2_ndisuio_global->stop_request;
	handles[1] = l2_ndisuio_global->rx_processed;
	WaitForMultipleObjects(2, handles, FALSE, INFINITE);
	ResetEvent(l2_ndisuio_global->rx_processed);
}


static DWORD WINAPI l2_packet_rx_thread(LPVOID arg)
{
	struct l2_packet_data *l2 = arg;
	DWORD res;
	HANDLE handles[2];
	int run = 1;

	wpa_printf(MSG_DEBUG, "L2(NDISUIO): RX thread started");
	handles[0] = l2_ndisuio_global->stop_request;
	handles[1] = l2_ndisuio_global->ready_for_read;

	/*
	 * Unfortunately, NDISUIO on WinCE does not seem to support waiting
	 * on the handle. There do not seem to be anything else that we could
	 * wait for either. If one were to modify NDISUIO to set a named event
	 * whenever packets are available, this event could be used here to
	 * avoid having to poll for new packets or we could even move to use a
	 * single threaded design.
	 *
	 * In addition, NDISUIO on WinCE is returning
	 * ERROR_DEVICE_NOT_CONNECTED whenever ReadFile() is attempted while
	 * the adapter is not in connected state. For now, we are just using a
	 * local event to allow ReadFile calls only after having received NDIS
	 * media connect event. This event could be easily converted to handle
	 * another event if the protocol driver is replaced with somewhat more
	 * useful design.
	 */

	while (l2_ndisuio_global && run) {
		res = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
		switch (res) {
		case WAIT_OBJECT_0:
			wpa_printf(MSG_DEBUG, "l2_packet_rx_thread: Received "
				   "request to stop RX thread");
			run = 0;
			break;
		case WAIT_OBJECT_0 + 1:
			l2_packet_rx_thread_try_read(l2);
			break;
		case WAIT_FAILED:
		default:
			wpa_printf(MSG_DEBUG, "l2_packet_rx_thread: "
				   "WaitForMultipleObjects failed: %d",
				   (int) GetLastError());
			run = 0;
			break;
		}
	}

	wpa_printf(MSG_DEBUG, "L2(NDISUIO): RX thread stopped");

	return 0;
}
#else /* _WIN32_WCE */
static int l2_ndisuio_start_read(struct l2_packet_data *l2, int recursive)
{
	os_memset(&l2->rx_overlapped, 0, sizeof(l2->rx_overlapped));
	l2->rx_overlapped.hEvent = l2->rx_avail;
	if (!ReadFile(driver_ndis_get_ndisuio_handle(), l2->rx_buf,
		      sizeof(l2->rx_buf), &l2->rx_written, &l2->rx_overlapped))
	{
		DWORD err = GetLastError();
		if (err != ERROR_IO_PENDING) {
			wpa_printf(MSG_DEBUG, "L2(NDISUIO): ReadFile failed: "
				   "%d", (int) err);
			return -1;
		}
		/*
		 * Once read is completed, l2_packet_rx_event() will be
		 * called.
		 */
	} else {
		wpa_printf(MSG_DEBUG, "L2(NDISUIO): ReadFile returned data "
			   "without wait for completion");
		if (!recursive)
			l2_packet_callback(l2);
	}

	return 0;
}
#endif /* _WIN32_WCE */


static void l2_packet_callback(struct l2_packet_data *l2)
{
	const u8 *rx_buf, *rx_src;
	size_t rx_len;
	struct l2_ethhdr *ethhdr = (struct l2_ethhdr *) l2->rx_buf;

	wpa_printf(MSG_DEBUG, "L2(NDISUIO): Read %d bytes",
		   (int) l2->rx_written);

	if (l2->l2_hdr || l2->rx_written < sizeof(*ethhdr)) {
		rx_buf = (u8 *) ethhdr;
		rx_len = l2->rx_written;
	} else {
		rx_buf = (u8 *) (ethhdr + 1);
		rx_len = l2->rx_written - sizeof(*ethhdr);
	}
	rx_src = ethhdr->h_source;

	l2->rx_callback(l2->rx_callback_ctx, rx_src, rx_buf, rx_len);
#ifndef _WIN32_WCE
	l2_ndisuio_start_read(l2, 1);
#endif /* _WIN32_WCE */
}


static void l2_packet_rx_event(void *eloop_data, void *user_data)
{
	struct l2_packet_data *l2 = eloop_data;

	if (l2_ndisuio_global)
		l2 = l2_ndisuio_global->l2[l2_ndisuio_global->refcount - 1];

	ResetEvent(l2->rx_avail);

#ifndef _WIN32_WCE
	if (!GetOverlappedResult(driver_ndis_get_ndisuio_handle(),
				 &l2->rx_overlapped, &l2->rx_written, FALSE)) {
		wpa_printf(MSG_DEBUG, "L2(NDISUIO): GetOverlappedResult "
			   "failed: %d", (int) GetLastError());
		return;
	}
#endif /* _WIN32_WCE */

	l2_packet_callback(l2);

#ifdef _WIN32_WCE
	SetEvent(l2_ndisuio_global->rx_processed);
#endif /* _WIN32_WCE */
}


static int l2_ndisuio_set_ether_type(unsigned short protocol)
{
	USHORT proto = htons(protocol);
	DWORD written;

	if (!DeviceIoControl(driver_ndis_get_ndisuio_handle(),
			     IOCTL_NDISUIO_SET_ETHER_TYPE, &proto,
			     sizeof(proto), NULL, 0, &written, NULL)) {
		wpa_printf(MSG_ERROR, "L2(NDISUIO): "
			   "IOCTL_NDISUIO_SET_ETHER_TYPE failed: %d",
			   (int) GetLastError());
		return -1;
	}

	return 0;
}


struct l2_packet_data * l2_packet_init(
	const char *ifname, const u8 *own_addr, unsigned short protocol,
	void (*rx_callback)(void *ctx, const u8 *src_addr,
			    const u8 *buf, size_t len),
	void *rx_callback_ctx, int l2_hdr)
{
	struct l2_packet_data *l2;

	if (l2_ndisuio_global == NULL) {
		l2_ndisuio_global = os_zalloc(sizeof(*l2_ndisuio_global));
		if (l2_ndisuio_global == NULL)
			return NULL;
		l2_ndisuio_global->first_proto = protocol;
	}
	if (l2_ndisuio_global->refcount >= 2) {
		wpa_printf(MSG_ERROR, "L2(NDISUIO): Not more than two "
			   "simultaneous connections allowed");
		return NULL;
	}
	l2_ndisuio_global->refcount++;

	l2 = os_zalloc(sizeof(struct l2_packet_data));
	if (l2 == NULL)
		return NULL;
	l2_ndisuio_global->l2[l2_ndisuio_global->refcount - 1] = l2;

	os_strlcpy(l2->ifname, ifname, sizeof(l2->ifname));
	l2->rx_callback = rx_callback;
	l2->rx_callback_ctx = rx_callback_ctx;
	l2->l2_hdr = l2_hdr;

	if (own_addr)
		os_memcpy(l2->own_addr, own_addr, ETH_ALEN);

	if (l2_ndisuio_set_ether_type(protocol) < 0) {
		os_free(l2);
		return NULL;
	}

	if (l2_ndisuio_global->refcount > 1) {
		wpa_printf(MSG_DEBUG, "L2(NDISUIO): Temporarily setting "
			   "filtering ethertype to %04x", protocol);
		if (l2_ndisuio_global->l2[0])
			l2->rx_avail = l2_ndisuio_global->l2[0]->rx_avail;
		return l2;
	}

	l2->rx_avail = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (l2->rx_avail == NULL) {
		os_free(l2);
		return NULL;
	}

	eloop_register_event(l2->rx_avail, sizeof(l2->rx_avail),
			     l2_packet_rx_event, l2, NULL);

#ifdef _WIN32_WCE
	l2_ndisuio_global->stop_request = CreateEvent(NULL, TRUE, FALSE, NULL);
	/*
	 * This event is being set based on media connect/disconnect
	 * notifications in driver_ndis.c.
	 */
	l2_ndisuio_global->ready_for_read =
		CreateEvent(NULL, TRUE, FALSE, TEXT("WpaSupplicantConnected"));
	l2_ndisuio_global->rx_processed = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (l2_ndisuio_global->stop_request == NULL ||
	    l2_ndisuio_global->ready_for_read == NULL ||
	    l2_ndisuio_global->rx_processed == NULL) {
		if (l2_ndisuio_global->stop_request) {
			CloseHandle(l2_ndisuio_global->stop_request);
			l2_ndisuio_global->stop_request = NULL;
		}
		if (l2_ndisuio_global->ready_for_read) {
			CloseHandle(l2_ndisuio_global->ready_for_read);
			l2_ndisuio_global->ready_for_read = NULL;
		}
		if (l2_ndisuio_global->rx_processed) {
			CloseHandle(l2_ndisuio_global->rx_processed);
			l2_ndisuio_global->rx_processed = NULL;
		}
		eloop_unregister_event(l2->rx_avail, sizeof(l2->rx_avail));
		os_free(l2);
		return NULL;
	}

	l2_ndisuio_global->rx_thread = CreateThread(NULL, 0,
						    l2_packet_rx_thread, l2, 0,
						    NULL);
	if (l2_ndisuio_global->rx_thread == NULL) {
		wpa_printf(MSG_INFO, "L2(NDISUIO): Failed to create RX "
			   "thread: %d", (int) GetLastError());
		eloop_unregister_event(l2->rx_avail, sizeof(l2->rx_avail));
		CloseHandle(l2_ndisuio_global->stop_request);
		l2_ndisuio_global->stop_request = NULL;
		os_free(l2);
		return NULL;
	}
#else /* _WIN32_WCE */
	l2_ndisuio_start_read(l2, 0);
#endif /* _WIN32_WCE */

	return l2;
}


void l2_packet_deinit(struct l2_packet_data *l2)
{
	if (l2 == NULL)
		return;

	if (l2_ndisuio_global) {
		l2_ndisuio_global->refcount--;
		l2_ndisuio_global->l2[l2_ndisuio_global->refcount] = NULL;
		if (l2_ndisuio_global->refcount) {
			wpa_printf(MSG_DEBUG, "L2(NDISUIO): restore filtering "
				   "ethertype to %04x",
				   l2_ndisuio_global->first_proto);
			l2_ndisuio_set_ether_type(
				l2_ndisuio_global->first_proto);
			return;
		}

#ifdef _WIN32_WCE
		wpa_printf(MSG_DEBUG, "L2(NDISUIO): Waiting for RX thread to "
			   "stop");
		SetEvent(l2_ndisuio_global->stop_request);
		/*
		 * Cancel pending ReadFile() in the RX thread (if we were still
		 * connected at this point).
		 */
		if (!DeviceIoControl(driver_ndis_get_ndisuio_handle(),
				     IOCTL_CANCEL_READ, NULL, 0, NULL, 0, NULL,
				     NULL)) {
			wpa_printf(MSG_DEBUG, "L2(NDISUIO): IOCTL_CANCEL_READ "
				   "failed: %d", (int) GetLastError());
			/* RX thread will exit blocking ReadFile once NDISUIO
			 * notices that the adapter is disconnected. */
		}
		WaitForSingleObject(l2_ndisuio_global->rx_thread, INFINITE);
		wpa_printf(MSG_DEBUG, "L2(NDISUIO): RX thread exited");
		CloseHandle(l2_ndisuio_global->rx_thread);
		CloseHandle(l2_ndisuio_global->stop_request);
		CloseHandle(l2_ndisuio_global->ready_for_read);
		CloseHandle(l2_ndisuio_global->rx_processed);
#endif /* _WIN32_WCE */

		os_free(l2_ndisuio_global);
		l2_ndisuio_global = NULL;
	}

#ifndef _WIN32_WCE
	CancelIo(driver_ndis_get_ndisuio_handle());
#endif /* _WIN32_WCE */

	eloop_unregister_event(l2->rx_avail, sizeof(l2->rx_avail));
	CloseHandle(l2->rx_avail);
	os_free(l2);
}


int l2_packet_get_ip_addr(struct l2_packet_data *l2, char *buf, size_t len)
{
	return -1;
}


void l2_packet_notify_auth_start(struct l2_packet_data *l2)
{
}

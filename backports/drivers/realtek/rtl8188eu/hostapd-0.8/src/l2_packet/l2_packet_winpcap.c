/*
 * WPA Supplicant - Layer2 packet handling with WinPcap RX thread
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
 * This l2_packet implementation is explicitly for WinPcap and Windows events.
 * l2_packet_pcap.c has support for WinPcap, but it requires polling to receive
 * frames which means relatively long latency for EAPOL RX processing. The
 * implementation here uses a separate thread to allow WinPcap to be receiving
 * all the time to reduce latency for EAPOL receiving from about 100 ms to 3 ms
 * when comparing l2_packet_pcap.c to l2_packet_winpcap.c. Extra sleep of 50 ms
 * is added in to receive thread whenever no EAPOL frames has been received for
 * a while. Whenever an EAPOL handshake is expected, this sleep is removed.
 *
 * The RX thread receives a frame and signals main thread through Windows event
 * about the availability of a new frame. Processing the received frame is
 * synchronized with pair of Windows events so that no extra buffer or queuing
 * mechanism is needed. This implementation requires Windows specific event
 * loop implementation, i.e., eloop_win.c.
 *
 * WinPcap has pcap_getevent() that could, in theory at least, be used to
 * implement this kind of waiting with a simpler single-thread design. However,
 * that event handle is not really signaled immediately when receiving each
 * frame, so it does not really work for this kind of use.
 */

#include "includes.h"
#include <pcap.h>

#include "common.h"
#include "eloop.h"
#include "l2_packet.h"


static const u8 pae_group_addr[ETH_ALEN] =
{ 0x01, 0x80, 0xc2, 0x00, 0x00, 0x03 };

/*
 * Number of pcap_dispatch() iterations to do without extra wait after each
 * received EAPOL packet or authentication notification. This is used to reduce
 * latency for EAPOL receive.
 */
static const size_t no_wait_count = 750;

struct l2_packet_data {
	pcap_t *pcap;
	unsigned int num_fast_poll;
	char ifname[100];
	u8 own_addr[ETH_ALEN];
	void (*rx_callback)(void *ctx, const u8 *src_addr,
			    const u8 *buf, size_t len);
	void *rx_callback_ctx;
	int l2_hdr; /* whether to include layer 2 (Ethernet) header in calls to
		     * rx_callback and l2_packet_send() */
	int running;
	HANDLE rx_avail, rx_done, rx_thread, rx_thread_done, rx_notify;
	u8 *rx_buf, *rx_src;
	size_t rx_len;
	size_t rx_no_wait;
};


int l2_packet_get_own_addr(struct l2_packet_data *l2, u8 *addr)
{
	os_memcpy(addr, l2->own_addr, ETH_ALEN);
	return 0;
}


int l2_packet_send(struct l2_packet_data *l2, const u8 *dst_addr, u16 proto,
		   const u8 *buf, size_t len)
{
	int ret;
	struct l2_ethhdr *eth;

	if (l2 == NULL)
		return -1;

	if (l2->l2_hdr) {
		ret = pcap_sendpacket(l2->pcap, buf, len);
	} else {
		size_t mlen = sizeof(*eth) + len;
		eth = os_malloc(mlen);
		if (eth == NULL)
			return -1;

		os_memcpy(eth->h_dest, dst_addr, ETH_ALEN);
		os_memcpy(eth->h_source, l2->own_addr, ETH_ALEN);
		eth->h_proto = htons(proto);
		os_memcpy(eth + 1, buf, len);
		ret = pcap_sendpacket(l2->pcap, (u8 *) eth, mlen);
		os_free(eth);
	}

	return ret;
}


/* pcap_dispatch() callback for the RX thread */
static void l2_packet_receive_cb(u_char *user, const struct pcap_pkthdr *hdr,
				 const u_char *pkt_data)
{
	struct l2_packet_data *l2 = (struct l2_packet_data *) user;
	struct l2_ethhdr *ethhdr;

	if (pkt_data == NULL || hdr->caplen < sizeof(*ethhdr))
		return;

	ethhdr = (struct l2_ethhdr *) pkt_data;
	if (l2->l2_hdr) {
		l2->rx_buf = (u8 *) ethhdr;
		l2->rx_len = hdr->caplen;
	} else {
		l2->rx_buf = (u8 *) (ethhdr + 1);
		l2->rx_len = hdr->caplen - sizeof(*ethhdr);
	}
	l2->rx_src = ethhdr->h_source;
	SetEvent(l2->rx_avail);
	WaitForSingleObject(l2->rx_done, INFINITE);
	ResetEvent(l2->rx_done);
	l2->rx_no_wait = no_wait_count;
}


/* main RX loop that is running in a separate thread */
static DWORD WINAPI l2_packet_receive_thread(LPVOID arg)
{
	struct l2_packet_data *l2 = arg;

	while (l2->running) {
		pcap_dispatch(l2->pcap, 1, l2_packet_receive_cb,
			      (u_char *) l2);
		if (l2->rx_no_wait > 0)
			l2->rx_no_wait--;
		if (WaitForSingleObject(l2->rx_notify,
					l2->rx_no_wait ? 0 : 50) ==
		    WAIT_OBJECT_0) {
			l2->rx_no_wait = no_wait_count;
			ResetEvent(l2->rx_notify);
		}
	}
	SetEvent(l2->rx_thread_done);
	ExitThread(0);
	return 0;
}


/* main thread RX event handler */
static void l2_packet_rx_event(void *eloop_data, void *user_data)
{
	struct l2_packet_data *l2 = eloop_data;
	l2->rx_callback(l2->rx_callback_ctx, l2->rx_src, l2->rx_buf,
			l2->rx_len);
	ResetEvent(l2->rx_avail);
	SetEvent(l2->rx_done);
}


static int l2_packet_init_libpcap(struct l2_packet_data *l2,
				  unsigned short protocol)
{
	bpf_u_int32 pcap_maskp, pcap_netp;
	char pcap_filter[200], pcap_err[PCAP_ERRBUF_SIZE];
	struct bpf_program pcap_fp;

	pcap_lookupnet(l2->ifname, &pcap_netp, &pcap_maskp, pcap_err);
	l2->pcap = pcap_open_live(l2->ifname, 2500, 0, 1, pcap_err);
	if (l2->pcap == NULL) {
		fprintf(stderr, "pcap_open_live: %s\n", pcap_err);
		fprintf(stderr, "ifname='%s'\n", l2->ifname);
		return -1;
	}
	os_snprintf(pcap_filter, sizeof(pcap_filter),
		    "not ether src " MACSTR " and "
		    "( ether dst " MACSTR " or ether dst " MACSTR " ) and "
		    "ether proto 0x%x",
		    MAC2STR(l2->own_addr), /* do not receive own packets */
		    MAC2STR(l2->own_addr), MAC2STR(pae_group_addr),
		    protocol);
	if (pcap_compile(l2->pcap, &pcap_fp, pcap_filter, 1, pcap_netp) < 0) {
		fprintf(stderr, "pcap_compile: %s\n", pcap_geterr(l2->pcap));
		return -1;
	}

	if (pcap_setfilter(l2->pcap, &pcap_fp) < 0) {
		fprintf(stderr, "pcap_setfilter: %s\n", pcap_geterr(l2->pcap));
		return -1;
	}

	pcap_freecode(&pcap_fp);

	return 0;
}


struct l2_packet_data * l2_packet_init(
	const char *ifname, const u8 *own_addr, unsigned short protocol,
	void (*rx_callback)(void *ctx, const u8 *src_addr,
			    const u8 *buf, size_t len),
	void *rx_callback_ctx, int l2_hdr)
{
	struct l2_packet_data *l2;
	DWORD thread_id;

	l2 = os_zalloc(sizeof(struct l2_packet_data));
	if (l2 == NULL)
		return NULL;
	if (os_strncmp(ifname, "\\Device\\NPF_", 12) == 0)
		os_strlcpy(l2->ifname, ifname, sizeof(l2->ifname));
	else
		os_snprintf(l2->ifname, sizeof(l2->ifname), "\\Device\\NPF_%s",
			    ifname);
	l2->rx_callback = rx_callback;
	l2->rx_callback_ctx = rx_callback_ctx;
	l2->l2_hdr = l2_hdr;

	if (own_addr)
		os_memcpy(l2->own_addr, own_addr, ETH_ALEN);

	if (l2_packet_init_libpcap(l2, protocol)) {
		os_free(l2);
		return NULL;
	}

	l2->rx_avail = CreateEvent(NULL, TRUE, FALSE, NULL);
	l2->rx_done = CreateEvent(NULL, TRUE, FALSE, NULL);
	l2->rx_notify = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (l2->rx_avail == NULL || l2->rx_done == NULL ||
	    l2->rx_notify == NULL) {
		CloseHandle(l2->rx_avail);
		CloseHandle(l2->rx_done);
		CloseHandle(l2->rx_notify);
		pcap_close(l2->pcap);
		os_free(l2);
		return NULL;
	}

	eloop_register_event(l2->rx_avail, sizeof(l2->rx_avail),
			     l2_packet_rx_event, l2, NULL);

	l2->running = 1;
	l2->rx_thread = CreateThread(NULL, 0, l2_packet_receive_thread, l2, 0,
				     &thread_id);

	return l2;
}


static void l2_packet_deinit_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct l2_packet_data *l2 = eloop_ctx;

	if (l2->rx_thread_done &&
	    WaitForSingleObject(l2->rx_thread_done, 2000) != WAIT_OBJECT_0) {
		wpa_printf(MSG_DEBUG, "l2_packet_winpcap: RX thread did not "
			   "exit - kill it\n");
		TerminateThread(l2->rx_thread, 0);
	}
	CloseHandle(l2->rx_thread_done);
	CloseHandle(l2->rx_thread);
	if (l2->pcap)
		pcap_close(l2->pcap);
	eloop_unregister_event(l2->rx_avail, sizeof(l2->rx_avail));
	CloseHandle(l2->rx_avail);
	CloseHandle(l2->rx_done);
	CloseHandle(l2->rx_notify);
	os_free(l2);
}


void l2_packet_deinit(struct l2_packet_data *l2)
{
	if (l2 == NULL)
		return;

	l2->rx_thread_done = CreateEvent(NULL, TRUE, FALSE, NULL);

	l2->running = 0;
	pcap_breakloop(l2->pcap);

	/*
	 * RX thread may be waiting in l2_packet_receive_cb() for l2->rx_done
	 * event and this event is set in l2_packet_rx_event(). However,
	 * l2_packet_deinit() may end up being called from l2->rx_callback(),
	 * so we need to return from here and complete deinitialization in
	 * a registered timeout to avoid having to forcefully kill the RX
	 * thread.
	 */
	eloop_register_timeout(0, 0, l2_packet_deinit_timeout, l2, NULL);
}


int l2_packet_get_ip_addr(struct l2_packet_data *l2, char *buf, size_t len)
{
	pcap_if_t *devs, *dev;
	struct pcap_addr *addr;
	struct sockaddr_in *saddr;
	int found = 0;
	char err[PCAP_ERRBUF_SIZE + 1];

	if (pcap_findalldevs(&devs, err) < 0) {
		wpa_printf(MSG_DEBUG, "pcap_findalldevs: %s\n", err);
		return -1;
	}

	for (dev = devs; dev && !found; dev = dev->next) {
		if (os_strcmp(dev->name, l2->ifname) != 0)
			continue;

		addr = dev->addresses;
		while (addr) {
			saddr = (struct sockaddr_in *) addr->addr;
			if (saddr && saddr->sin_family == AF_INET) {
				os_strlcpy(buf, inet_ntoa(saddr->sin_addr),
					   len);
				found = 1;
				break;
			}
			addr = addr->next;
		}
	}

	pcap_freealldevs(devs);

	return found ? 0 : -1;
}


void l2_packet_notify_auth_start(struct l2_packet_data *l2)
{
	if (l2)
		SetEvent(l2->rx_notify);
}

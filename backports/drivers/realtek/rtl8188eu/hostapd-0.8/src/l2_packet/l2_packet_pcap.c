/*
 * WPA Supplicant - Layer2 packet handling with libpcap/libdnet and WinPcap
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
 */

#include "includes.h"
#ifndef CONFIG_NATIVE_WINDOWS
#include <sys/ioctl.h>
#endif /* CONFIG_NATIVE_WINDOWS */
#include <pcap.h>
#ifndef CONFIG_WINPCAP
#include <dnet.h>
#endif /* CONFIG_WINPCAP */

#include "common.h"
#include "eloop.h"
#include "l2_packet.h"


static const u8 pae_group_addr[ETH_ALEN] =
{ 0x01, 0x80, 0xc2, 0x00, 0x00, 0x03 };

struct l2_packet_data {
	pcap_t *pcap;
#ifdef CONFIG_WINPCAP
	unsigned int num_fast_poll;
#else /* CONFIG_WINPCAP */
	eth_t *eth;
#endif /* CONFIG_WINPCAP */
	char ifname[100];
	u8 own_addr[ETH_ALEN];
	void (*rx_callback)(void *ctx, const u8 *src_addr,
			    const u8 *buf, size_t len);
	void *rx_callback_ctx;
	int l2_hdr; /* whether to include layer 2 (Ethernet) header in calls
			* to rx_callback */
};


int l2_packet_get_own_addr(struct l2_packet_data *l2, u8 *addr)
{
	os_memcpy(addr, l2->own_addr, ETH_ALEN);
	return 0;
}


#ifndef CONFIG_WINPCAP
static int l2_packet_init_libdnet(struct l2_packet_data *l2)
{
	eth_addr_t own_addr;

	l2->eth = eth_open(l2->ifname);
	if (!l2->eth) {
		printf("Failed to open interface '%s'.\n", l2->ifname);
		perror("eth_open");
		return -1;
	}

	if (eth_get(l2->eth, &own_addr) < 0) {
		printf("Failed to get own hw address from interface '%s'.\n",
		       l2->ifname);
		perror("eth_get");
		eth_close(l2->eth);
		l2->eth = NULL;
		return -1;
	}
	os_memcpy(l2->own_addr, own_addr.data, ETH_ALEN);

	return 0;
}
#endif /* CONFIG_WINPCAP */


int l2_packet_send(struct l2_packet_data *l2, const u8 *dst_addr, u16 proto,
		   const u8 *buf, size_t len)
{
	int ret;
	struct l2_ethhdr *eth;

	if (l2 == NULL)
		return -1;

	if (l2->l2_hdr) {
#ifdef CONFIG_WINPCAP
		ret = pcap_sendpacket(l2->pcap, buf, len);
#else /* CONFIG_WINPCAP */
		ret = eth_send(l2->eth, buf, len);
#endif /* CONFIG_WINPCAP */
	} else {
		size_t mlen = sizeof(*eth) + len;
		eth = os_malloc(mlen);
		if (eth == NULL)
			return -1;

		os_memcpy(eth->h_dest, dst_addr, ETH_ALEN);
		os_memcpy(eth->h_source, l2->own_addr, ETH_ALEN);
		eth->h_proto = htons(proto);
		os_memcpy(eth + 1, buf, len);

#ifdef CONFIG_WINPCAP
		ret = pcap_sendpacket(l2->pcap, (u8 *) eth, mlen);
#else /* CONFIG_WINPCAP */
		ret = eth_send(l2->eth, (u8 *) eth, mlen);
#endif /* CONFIG_WINPCAP */

		os_free(eth);
	}

	return ret;
}


#ifndef CONFIG_WINPCAP
static void l2_packet_receive(int sock, void *eloop_ctx, void *sock_ctx)
{
	struct l2_packet_data *l2 = eloop_ctx;
	pcap_t *pcap = sock_ctx;
	struct pcap_pkthdr hdr;
	const u_char *packet;
	struct l2_ethhdr *ethhdr;
	unsigned char *buf;
	size_t len;

	packet = pcap_next(pcap, &hdr);

	if (packet == NULL || hdr.caplen < sizeof(*ethhdr))
		return;

	ethhdr = (struct l2_ethhdr *) packet;
	if (l2->l2_hdr) {
		buf = (unsigned char *) ethhdr;
		len = hdr.caplen;
	} else {
		buf = (unsigned char *) (ethhdr + 1);
		len = hdr.caplen - sizeof(*ethhdr);
	}
	l2->rx_callback(l2->rx_callback_ctx, ethhdr->h_source, buf, len);
}
#endif /* CONFIG_WINPCAP */


#ifdef CONFIG_WINPCAP
static void l2_packet_receive_cb(u_char *user, const struct pcap_pkthdr *hdr,
				 const u_char *pkt_data)
{
	struct l2_packet_data *l2 = (struct l2_packet_data *) user;
	struct l2_ethhdr *ethhdr;
	unsigned char *buf;
	size_t len;

	if (pkt_data == NULL || hdr->caplen < sizeof(*ethhdr))
		return;

	ethhdr = (struct l2_ethhdr *) pkt_data;
	if (l2->l2_hdr) {
		buf = (unsigned char *) ethhdr;
		len = hdr->caplen;
	} else {
		buf = (unsigned char *) (ethhdr + 1);
		len = hdr->caplen - sizeof(*ethhdr);
	}
	l2->rx_callback(l2->rx_callback_ctx, ethhdr->h_source, buf, len);
	/*
	 * Use shorter poll interval for 3 seconds to reduce latency during key
	 * handshake.
	 */
	l2->num_fast_poll = 3 * 50;
}


static void l2_packet_receive_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct l2_packet_data *l2 = eloop_ctx;
	pcap_t *pcap = timeout_ctx;
	int timeout;

	if (l2->num_fast_poll > 0) {
		timeout = 20000;
		l2->num_fast_poll--;
	} else
		timeout = 100000;

	/* Register new timeout before calling l2_packet_receive() since
	 * receive handler may free this l2_packet instance (which will
	 * cancel this timeout). */
	eloop_register_timeout(0, timeout, l2_packet_receive_timeout,
			       l2, pcap);
	pcap_dispatch(pcap, 10, l2_packet_receive_cb, (u_char *) l2);
}
#endif /* CONFIG_WINPCAP */


static int l2_packet_init_libpcap(struct l2_packet_data *l2,
				  unsigned short protocol)
{
	bpf_u_int32 pcap_maskp, pcap_netp;
	char pcap_filter[200], pcap_err[PCAP_ERRBUF_SIZE];
	struct bpf_program pcap_fp;

#ifdef CONFIG_WINPCAP
	char ifname[128];
	os_snprintf(ifname, sizeof(ifname), "\\Device\\NPF_%s", l2->ifname);
	pcap_lookupnet(ifname, &pcap_netp, &pcap_maskp, pcap_err);
	l2->pcap = pcap_open_live(ifname, 2500, 0, 10, pcap_err);
	if (l2->pcap == NULL) {
		fprintf(stderr, "pcap_open_live: %s\n", pcap_err);
		fprintf(stderr, "ifname='%s'\n", ifname);
		return -1;
	}
	if (pcap_setnonblock(l2->pcap, 1, pcap_err) < 0)
		fprintf(stderr, "pcap_setnonblock: %s\n",
			pcap_geterr(l2->pcap));
#else /* CONFIG_WINPCAP */
	pcap_lookupnet(l2->ifname, &pcap_netp, &pcap_maskp, pcap_err);
	l2->pcap = pcap_open_live(l2->ifname, 2500, 0, 10, pcap_err);
	if (l2->pcap == NULL) {
		fprintf(stderr, "pcap_open_live: %s\n", pcap_err);
		fprintf(stderr, "ifname='%s'\n", l2->ifname);
		return -1;
	}
	if (pcap_datalink(l2->pcap) != DLT_EN10MB &&
	    pcap_set_datalink(l2->pcap, DLT_EN10MB) < 0) {
		fprintf(stderr, "pcap_set_datalink(DLT_EN10MB): %s\n",
			pcap_geterr(l2->pcap));
		return -1;
	}
#endif /* CONFIG_WINPCAP */
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
#ifdef BIOCIMMEDIATE
	/*
	 * When libpcap uses BPF we must enable "immediate mode" to
	 * receive frames right away; otherwise the system may
	 * buffer them for us.
	 */
	{
		unsigned int on = 1;
		if (ioctl(pcap_fileno(l2->pcap), BIOCIMMEDIATE, &on) < 0) {
			fprintf(stderr, "%s: cannot enable immediate mode on "
				"interface %s: %s\n",
				__func__, l2->ifname, strerror(errno));
			/* XXX should we fail? */
		}
	}
#endif /* BIOCIMMEDIATE */

#ifdef CONFIG_WINPCAP
	eloop_register_timeout(0, 100000, l2_packet_receive_timeout,
			       l2, l2->pcap);
#else /* CONFIG_WINPCAP */
	eloop_register_read_sock(pcap_get_selectable_fd(l2->pcap),
				 l2_packet_receive, l2, l2->pcap);
#endif /* CONFIG_WINPCAP */

	return 0;
}


struct l2_packet_data * l2_packet_init(
	const char *ifname, const u8 *own_addr, unsigned short protocol,
	void (*rx_callback)(void *ctx, const u8 *src_addr,
			    const u8 *buf, size_t len),
	void *rx_callback_ctx, int l2_hdr)
{
	struct l2_packet_data *l2;

	l2 = os_zalloc(sizeof(struct l2_packet_data));
	if (l2 == NULL)
		return NULL;
	os_strlcpy(l2->ifname, ifname, sizeof(l2->ifname));
	l2->rx_callback = rx_callback;
	l2->rx_callback_ctx = rx_callback_ctx;
	l2->l2_hdr = l2_hdr;

#ifdef CONFIG_WINPCAP
	if (own_addr)
		os_memcpy(l2->own_addr, own_addr, ETH_ALEN);
#else /* CONFIG_WINPCAP */
	if (l2_packet_init_libdnet(l2))
		return NULL;
#endif /* CONFIG_WINPCAP */

	if (l2_packet_init_libpcap(l2, protocol)) {
#ifndef CONFIG_WINPCAP
		eth_close(l2->eth);
#endif /* CONFIG_WINPCAP */
		os_free(l2);
		return NULL;
	}

	return l2;
}


void l2_packet_deinit(struct l2_packet_data *l2)
{
	if (l2 == NULL)
		return;

#ifdef CONFIG_WINPCAP
	eloop_cancel_timeout(l2_packet_receive_timeout, l2, l2->pcap);
#else /* CONFIG_WINPCAP */
	if (l2->eth)
		eth_close(l2->eth);
	eloop_unregister_read_sock(pcap_get_selectable_fd(l2->pcap));
#endif /* CONFIG_WINPCAP */
	if (l2->pcap)
		pcap_close(l2->pcap);
	os_free(l2);
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
#ifdef CONFIG_WINPCAP
	/*
	 * Use shorter poll interval for 3 seconds to reduce latency during key
	 * handshake.
	 */
	l2->num_fast_poll = 3 * 50;
	eloop_cancel_timeout(l2_packet_receive_timeout, l2, l2->pcap);
	eloop_register_timeout(0, 10000, l2_packet_receive_timeout,
			       l2, l2->pcap);
#endif /* CONFIG_WINPCAP */
}

/*
 * WPA Supplicant - Layer2 packet handling with FreeBSD
 * Copyright (c) 2003-2005, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2005, Sam Leffler <sam@errno.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#if defined(__APPLE__) || defined(__GLIBC__)
#include <net/bpf.h>
#endif /* __APPLE__ */
#include <pcap.h>

#include <sys/ioctl.h>
#ifdef __sun__
#include <libdlpi.h>
#else /* __sun__ */
#include <sys/sysctl.h>
#endif /* __sun__ */

#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <netinet/in.h>

#include "common.h"
#include "eloop.h"
#include "l2_packet.h"


static const u8 pae_group_addr[ETH_ALEN] =
{ 0x01, 0x80, 0xc2, 0x00, 0x00, 0x03 };

struct l2_packet_data {
	pcap_t *pcap;
	char ifname[100];
	u8 own_addr[ETH_ALEN];
	void (*rx_callback)(void *ctx, const u8 *src_addr,
			    const u8 *buf, size_t len);
	void *rx_callback_ctx;
	int l2_hdr; /* whether to include layer 2 (Ethernet) header data
		     * buffers */
};


int l2_packet_get_own_addr(struct l2_packet_data *l2, u8 *addr)
{
	os_memcpy(addr, l2->own_addr, ETH_ALEN);
	return 0;
}


int l2_packet_send(struct l2_packet_data *l2, const u8 *dst_addr, u16 proto,
		   const u8 *buf, size_t len)
{
	if (!l2->l2_hdr) {
		int ret;
		struct l2_ethhdr *eth = os_malloc(sizeof(*eth) + len);
		if (eth == NULL)
			return -1;
		os_memcpy(eth->h_dest, dst_addr, ETH_ALEN);
		os_memcpy(eth->h_source, l2->own_addr, ETH_ALEN);
		eth->h_proto = htons(proto);
		os_memcpy(eth + 1, buf, len);
		ret = pcap_inject(l2->pcap, (u8 *) eth, len + sizeof(*eth));
		os_free(eth);
		return ret;
	} else
		return pcap_inject(l2->pcap, buf, len);
}


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


static int l2_packet_init_libpcap(struct l2_packet_data *l2,
				  unsigned short protocol)
{
	bpf_u_int32 pcap_maskp, pcap_netp;
	char pcap_filter[200], pcap_err[PCAP_ERRBUF_SIZE];
	struct bpf_program pcap_fp;

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
#ifndef __sun__
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
#endif /* __sun__ */

	eloop_register_read_sock(pcap_get_selectable_fd(l2->pcap),
				 l2_packet_receive, l2, l2->pcap);

	return 0;
}


static int eth_get(const char *device, u8 ea[ETH_ALEN])
{
#ifdef __sun__
	dlpi_handle_t dh;
	u32 physaddrlen = DLPI_PHYSADDR_MAX;
	u8 physaddr[DLPI_PHYSADDR_MAX];
	int retval;

	retval = dlpi_open(device, &dh, 0);
	if (retval != DLPI_SUCCESS) {
		wpa_printf(MSG_ERROR, "dlpi_open error: %s",
			   dlpi_strerror(retval));
		return -1;
	}

	retval = dlpi_get_physaddr(dh, DL_CURR_PHYS_ADDR, physaddr,
				   &physaddrlen);
	if (retval != DLPI_SUCCESS) {
		wpa_printf(MSG_ERROR, "dlpi_get_physaddr error: %s",
			   dlpi_strerror(retval));
		dlpi_close(dh);
		return -1;
	}
	os_memcpy(ea, physaddr, ETH_ALEN);
	dlpi_close(dh);
#else /* __sun__ */
	struct if_msghdr *ifm;
	struct sockaddr_dl *sdl;
	u_char *p, *buf;
	size_t len;
	int mib[] = { CTL_NET, AF_ROUTE, 0, AF_LINK, NET_RT_IFLIST, 0 };

	if (sysctl(mib, 6, NULL, &len, NULL, 0) < 0)
		return -1;
	if ((buf = os_malloc(len)) == NULL)
		return -1;
	if (sysctl(mib, 6, buf, &len, NULL, 0) < 0) {
		os_free(buf);
		return -1;
	}
	for (p = buf; p < buf + len; p += ifm->ifm_msglen) {
		ifm = (struct if_msghdr *)p;
		sdl = (struct sockaddr_dl *)(ifm + 1);
		if (ifm->ifm_type != RTM_IFINFO ||
		    (ifm->ifm_addrs & RTA_IFP) == 0)
			continue;
		if (sdl->sdl_family != AF_LINK || sdl->sdl_nlen == 0 ||
		    os_memcmp(sdl->sdl_data, device, sdl->sdl_nlen) != 0)
			continue;
		os_memcpy(ea, LLADDR(sdl), sdl->sdl_alen);
		break;
	}
	os_free(buf);

	if (p >= buf + len) {
		errno = ESRCH;
		return -1;
	}
#endif /* __sun__ */
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

	if (eth_get(l2->ifname, l2->own_addr) < 0) {
		fprintf(stderr, "Failed to get link-level address for "
			"interface '%s'.\n", l2->ifname);
		os_free(l2);
		return NULL;
	}

	if (l2_packet_init_libpcap(l2, protocol)) {
		os_free(l2);
		return NULL;
	}

	return l2;
}


struct l2_packet_data * l2_packet_init_bridge(
	const char *br_ifname, const char *ifname, const u8 *own_addr,
	unsigned short protocol,
	void (*rx_callback)(void *ctx, const u8 *src_addr,
			    const u8 *buf, size_t len),
	void *rx_callback_ctx, int l2_hdr)
{
	return l2_packet_init(br_ifname, own_addr, protocol, rx_callback,
			      rx_callback_ctx, l2_hdr);
}


void l2_packet_deinit(struct l2_packet_data *l2)
{
	if (l2 != NULL) {
		if (l2->pcap) {
			eloop_unregister_read_sock(
				pcap_get_selectable_fd(l2->pcap));
			pcap_close(l2->pcap);
		}
		os_free(l2);
	}
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
}


int l2_packet_set_packet_filter(struct l2_packet_data *l2,
				enum l2_packet_filter_type type)
{
	return -1;
}

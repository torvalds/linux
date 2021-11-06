/*
 * L2 Filter handling functions
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 *
 */
#include <bcmutils.h>
#include <bcmendian.h>
#include <bcmdefs.h>
#include <bcmdevs.h>
#include <ethernet.h>
#include <bcmip.h>
#include <bcmipv6.h>
#include <bcmudp.h>
#include <bcmarp.h>
#include <bcmicmp.h>
#include <bcmproto.h>
#include <bcmdhcp.h>
#include <802.11.h>
#include <bcm_l2_filter.h>

#ifdef BCMDBG_ERR
#define	L2_FILTER_ERROR(args)	printf args
#else
#define	L2_FILTER_ERROR(args)
#endif	/* BCMDBG_ERR */

#ifdef BCMDBG_MSG
#define	L2_FILTER_MSG(args)	printf args
#else
#define	L2_FILTER_MSG(args)
#endif	/* BCMDBG_msg */

struct arp_table {
	parp_entry_t	*parp_table[BCM_PARP_TABLE_SIZE];   /* proxyarp entries in cache table */
	parp_entry_t	*parp_candidate_list;		    /* proxyarp entries in candidate list */
	uint8 parp_smac[ETHER_ADDR_LEN];		    /* L2 SMAC from DHCP Req */
	uint8 parp_cmac[ETHER_ADDR_LEN];		    /* Bootp Client MAC from DHCP Req */
};
#ifdef DHD_DUMP_ARPTABLE
void bcm_l2_parp_dump_table(arp_table_t* arp_tbl);

void
bcm_l2_parp_dump_table(arp_table_t* arp_tbl)
{
	parp_entry_t *entry;
	uint16 idx, ip_len;
	arp_table_t *ptable;
	ip_len = IPV4_ADDR_LEN;
	ptable = arp_tbl;
	for (idx = 0; idx < BCM_PARP_TABLE_SIZE; idx++) {
		entry = ptable->parp_table[idx];
		while (entry) {
			printf("Cached entries..\n");
			printf("%d: %d.%d.%d.%d", idx, entry->ip.data[0], entry->ip.data[1],
				entry->ip.data[2], entry->ip.data[3]);
			printf("%02x:%02x:%02x:%02x:%02x:%02x", entry->ea.octet[0],
				entry->ea.octet[1], entry->ea.octet[2], entry->ea.octet[3],
				entry->ea.octet[4], entry->ea.octet[5]);
			printf("\n");
			entry = entry->next;
		}
	}
	entry = ptable->parp_candidate_list;
	while (entry) {
		printf("Candidate entries..\n");
		printf("%d.%d.%d.%d", entry->ip.data[0], entry->ip.data[1],
			entry->ip.data[2], entry->ip.data[3]);
		printf("%02x:%02x:%02x:%02x:%02x:%02x", entry->ea.octet[0],
			entry->ea.octet[1], entry->ea.octet[2], entry->ea.octet[3],
			entry->ea.octet[4], entry->ea.octet[5]);

		printf("\n");
		entry = entry->next;
	}
}
#endif /* DHD_DUMP_ARPTABLE */

arp_table_t* init_l2_filter_arp_table(osl_t* osh)
{
	return ((arp_table_t*)MALLOCZ(osh, sizeof(arp_table_t)));
}

void deinit_l2_filter_arp_table(osl_t* osh, arp_table_t* ptable)
{
	MFREE(osh, ptable, sizeof(arp_table_t));
}
/* returns 0 if gratuitous ARP or unsolicited neighbour advertisement */
int
bcm_l2_filter_gratuitous_arp(osl_t *osh, void *pktbuf)
{
	uint8 *frame = PKTDATA(osh, pktbuf);
	uint16 ethertype;
	int send_ip_offset, target_ip_offset;
	int iplen;
	int minlen;
	uint8 *data;
	int datalen;
	bool snap;

	if (get_pkt_ether_type(osh, pktbuf, &data, &datalen, &ethertype, &snap) != BCME_OK)
	    return BCME_ERROR;

	if (!ETHER_ISBCAST(frame + ETHER_DEST_OFFSET) &&
	    bcmp(&ether_ipv6_mcast, frame + ETHER_DEST_OFFSET, sizeof(ether_ipv6_mcast))) {
	    return BCME_ERROR;
	}

	if (ethertype == ETHER_TYPE_ARP) {
		L2_FILTER_MSG(("bcm_l2_filter_gratuitous_arp: ARP RX data : %p: datalen : %d\n",
			data, datalen));
		send_ip_offset = ARP_SRC_IP_OFFSET;
		target_ip_offset = ARP_TGT_IP_OFFSET;
		iplen = IPV4_ADDR_LEN;
		minlen = ARP_DATA_LEN;
	} else if (ethertype == ETHER_TYPE_IPV6) {
		send_ip_offset = NEIGHBOR_ADVERTISE_SRC_IPV6_OFFSET;
		target_ip_offset = NEIGHBOR_ADVERTISE_TGT_IPV6_OFFSET;
		iplen = IPV6_ADDR_LEN;
		minlen = target_ip_offset + iplen;

		/* check for neighbour advertisement */
		if (datalen >= minlen && (data[IPV6_NEXT_HDR_OFFSET] != IP_PROT_ICMP6 ||
		    data[NEIGHBOR_ADVERTISE_TYPE_OFFSET] != NEIGHBOR_ADVERTISE_TYPE))
			return BCME_ERROR;

		/* Dont drop Unsolicitated NA fm AP with allnode mcast dest addr (HS2-4.5.E) */
		if (datalen >= minlen &&
			(data[IPV6_NEXT_HDR_OFFSET] == IP_PROT_ICMP6) &&
			(data[NEIGHBOR_ADVERTISE_TYPE_OFFSET] == NEIGHBOR_ADVERTISE_TYPE) &&
			(data[NEIGHBOR_ADVERTISE_OPTION_OFFSET] == OPT_TYPE_TGT_LINK_ADDR)) {
				L2_FILTER_MSG(("Unsolicitated Neighbour Advertisement from AP "
					"with allnode mcast dest addr tx'ed (%d)\n", datalen));
				return -1;
			}

	} else {
		return BCME_ERROR;
	}

	if (datalen < minlen) {
		L2_FILTER_MSG(("BCM: dhd_gratuitous_arp: truncated packet (%d)\n", datalen));
		return BCME_ERROR;
	}

	if (bcmp(data + send_ip_offset, data + target_ip_offset, iplen) == 0) {
		L2_FILTER_MSG((" returning BCME_OK in bcm_l2_filter_gratuitous_arp\n"));
		return BCME_OK;
	}

	return BCME_ERROR;
}
int
get_pkt_ether_type(osl_t *osh, void *pktbuf,
	uint8 **data_ptr, int *len_ptr, uint16 *et_ptr, bool *snap_ptr)
{
	uint8 *frame = PKTDATA(osh, pktbuf);
	int length = PKTLEN(osh, pktbuf);
	uint8 *pt;			/* Pointer to type field */
	uint16 ethertype;
	bool snap = FALSE;
	/* Process Ethernet II or SNAP-encapsulated 802.3 frames */
	if (length < ETHER_HDR_LEN) {
		L2_FILTER_MSG(("BCM: get_pkt_ether_type: short eth frame (%d)\n",
		           length));
		return BCME_ERROR;
	} else if (ntoh16_ua(frame + ETHER_TYPE_OFFSET) >= ETHER_TYPE_MIN) {
		/* Frame is Ethernet II */
		pt = frame + ETHER_TYPE_OFFSET;
	} else if (length >= ETHER_HDR_LEN + SNAP_HDR_LEN + ETHER_TYPE_LEN &&
	           !bcmp(llc_snap_hdr, frame + ETHER_HDR_LEN, SNAP_HDR_LEN)) {
		pt = frame + ETHER_HDR_LEN + SNAP_HDR_LEN;
		snap = TRUE;
	} else {
		L2_FILTER_MSG((" get_pkt_ether_type: non-SNAP 802.3 frame\n"));
		return BCME_ERROR;
	}

	ethertype = ntoh16_ua(pt);

	/* Skip VLAN tag, if any */
	if (ethertype == ETHER_TYPE_8021Q) {
		pt += VLAN_TAG_LEN;

		if ((pt + ETHER_TYPE_LEN) > (frame + length)) {
			L2_FILTER_MSG(("BCM: get_pkt_ether_type: short VLAN frame (%d)\n",
			          length));
			return BCME_ERROR;
		}
		ethertype = ntoh16_ua(pt);
	}
	*data_ptr = pt + ETHER_TYPE_LEN;
	*len_ptr = length - (int32)(pt + ETHER_TYPE_LEN - frame);
	*et_ptr = ethertype;
	*snap_ptr = snap;
	return BCME_OK;
}

int
get_pkt_ip_type(osl_t *osh, void *pktbuf,
	uint8 **data_ptr, int *len_ptr, uint8 *prot_ptr)
{
	struct ipv4_hdr *iph;		/* IP frame pointer */
	int iplen;			/* IP frame length */
	uint16 ethertype, iphdrlen, ippktlen;
	uint16 iph_frag;
	uint8 prot;
	bool snap;

	if (get_pkt_ether_type(osh, pktbuf, (uint8 **)&iph,
	    &iplen, &ethertype, &snap) != 0)
		return BCME_ERROR;

	if (ethertype != ETHER_TYPE_IP) {
		return BCME_ERROR;
	}

	/* We support IPv4 only */
	if (iplen < IPV4_OPTIONS_OFFSET || (IP_VER(iph) != IP_VER_4)) {
		return BCME_ERROR;
	}

	/* Header length sanity */
	iphdrlen = IPV4_HLEN(iph);

	/*
	 * Packet length sanity; sometimes we receive eth-frame size bigger
	 * than the IP content, which results in a bad tcp chksum
	 */
	ippktlen = ntoh16(iph->tot_len);
	if (ippktlen < iplen) {
		L2_FILTER_MSG(("get_pkt_ip_type: extra frame length ignored\n"));
		iplen = ippktlen;
	} else if (ippktlen > iplen) {
		L2_FILTER_MSG(("get_pkt_ip_type: truncated IP packet (%d)\n",
		           ippktlen - iplen));
		return BCME_ERROR;
	}

	if (iphdrlen < IPV4_OPTIONS_OFFSET || iphdrlen > iplen) {
		L2_FILTER_ERROR((" get_pkt_ip_type: IP-header-len (%d) out of range (%d-%d)\n",
		           iphdrlen, IPV4_OPTIONS_OFFSET, iplen));
		return BCME_ERROR;
	}

	/*
	 * We don't handle fragmented IP packets.  A first frag is indicated by the MF
	 * (more frag) bit and a subsequent frag is indicated by a non-zero frag offset.
	 */
	iph_frag = ntoh16(iph->frag);

	if ((iph_frag & IPV4_FRAG_MORE) || (iph_frag & IPV4_FRAG_OFFSET_MASK) != 0) {
		L2_FILTER_ERROR(("get_pkt_ip_type: IP fragment not handled\n"));
		return BCME_ERROR;
	}
	prot = IPV4_PROT(iph);
	*data_ptr = (((uint8 *)iph) + iphdrlen);
	*len_ptr = iplen - iphdrlen;
	*prot_ptr = prot;
	return BCME_OK;
}

/* Check if packet type is ICMP ECHO */
int bcm_l2_filter_block_ping(osl_t *osh, void *pktbuf)
{
	struct bcmicmp_hdr *icmph;
	int udpl;
	uint8 prot;

	if (get_pkt_ip_type(osh, pktbuf, (uint8 **)&icmph, &udpl, &prot) != 0)
		return BCME_ERROR;
	if (prot == IP_PROT_ICMP) {
		if (icmph->type == ICMP_TYPE_ECHO_REQUEST)
			return BCME_OK;
	}
	return BCME_ERROR;
}

int bcm_l2_filter_get_mac_addr_dhcp_pkt(osl_t *osh, void *pktbuf,
	int ifidx, uint8** mac_addr)
{
	uint8 *eh = PKTDATA(osh, pktbuf);
	uint8 *udph;
	uint8 *dhcp;
	int udpl;
	int dhcpl;
	uint16 port;
	uint8 prot;

	if (!ETHER_ISMULTI(eh + ETHER_DEST_OFFSET))
	    return BCME_ERROR;
	if (get_pkt_ip_type(osh, pktbuf, &udph, &udpl, &prot) != 0)
		return BCME_ERROR;
	if (prot != IP_PROT_UDP)
		return BCME_ERROR;
	/* check frame length, at least UDP_HDR_LEN */
	if (udpl < UDP_HDR_LEN) {
		L2_FILTER_MSG(("BCM: bcm_l2_filter_get_mac_addr_dhcp_pkt: short UDP frame,"
			" ignored\n"));
		return BCME_ERROR;
	}
	port = ntoh16_ua(udph + UDP_DEST_PORT_OFFSET);
	/* only process DHCP packets from server to client */
	if (port != DHCP_PORT_CLIENT)
		return BCME_ERROR;

	dhcp = udph + UDP_HDR_LEN;
	dhcpl = udpl - UDP_HDR_LEN;

	if (dhcpl < DHCP_CHADDR_OFFSET + ETHER_ADDR_LEN) {
		L2_FILTER_MSG(("BCM: bcm_l2_filter_get_mac_addr_dhcp_pkt: short DHCP frame,"
			" ignored\n"));
		return BCME_ERROR;
	}
	/* only process DHCP reply(offer/ack) packets */
	if (*(dhcp + DHCP_TYPE_OFFSET) != DHCP_TYPE_REPLY)
		return BCME_ERROR;
	/* chaddr = dhcp + DHCP_CHADDR_OFFSET; */
	*mac_addr = dhcp + DHCP_CHADDR_OFFSET;
	return BCME_OK;
}
/* modify the mac address for IP, in arp table */
int
bcm_l2_filter_parp_modifyentry(arp_table_t* arp_tbl, struct ether_addr *ea,
	uint8 *ip, uint8 ip_ver, bool cached, unsigned int entry_tickcnt)
{
	parp_entry_t *entry;
	uint8 idx, ip_len;
	arp_table_t *ptable;

	if (ip_ver == IP_VER_4 && !IPV4_ADDR_NULL(ip) && !IPV4_ADDR_BCAST(ip)) {
		idx = BCM_PARP_TABLE_INDEX(ip[IPV4_ADDR_LEN - 1]);
		ip_len = IPV4_ADDR_LEN;
	}
	else if (ip_ver == IP_VER_6 && !IPV6_ADDR_NULL(ip)) {
		idx = BCM_PARP_TABLE_INDEX(ip[IPV6_ADDR_LEN - 1]);
		ip_len = IPV6_ADDR_LEN;
	}
	else {
	    return BCME_ERROR;
	}

	ptable = arp_tbl;
	if (cached) {
	    entry = ptable->parp_table[idx];
	} else {
	    entry = ptable->parp_candidate_list;
	}
	while (entry) {
		if (bcmp(entry->ip.data, ip, ip_len) == 0) {
			/* entry matches, overwrite mac content and return */
			bcopy((void *)ea, (void *)&entry->ea, ETHER_ADDR_LEN);
			entry->used = entry_tickcnt;
#ifdef DHD_DUMP_ARPTABLE
			bcm_l2_parp_dump_table(arp_tbl);
#endif
			return BCME_OK;
		}
		entry = entry->next;
	}
#ifdef DHD_DUMP_ARPTABLE
	bcm_l2_parp_dump_table(arp_tbl);
#endif
	return BCME_ERROR;
}

/* Add the IP entry in ARP table based on Cached argument, if cached argument is
 * non zero positive value: it adds to parp_table, else adds to
 * parp_candidate_list
 */
int
bcm_l2_filter_parp_addentry(osl_t *osh, arp_table_t* arp_tbl, struct ether_addr *ea,
	uint8 *ip, uint8 ip_ver, bool cached, unsigned int entry_tickcnt)
{
	parp_entry_t *entry;
	uint8 idx, ip_len;
	arp_table_t *ptable;

	if (ip_ver == IP_VER_4 && !IPV4_ADDR_NULL(ip) && !IPV4_ADDR_BCAST(ip)) {
		idx = BCM_PARP_TABLE_INDEX(ip[IPV4_ADDR_LEN - 1]);
		ip_len = IPV4_ADDR_LEN;
	}
	else if (ip_ver == IP_VER_6 && !IPV6_ADDR_NULL(ip)) {
		idx = BCM_PARP_TABLE_INDEX(ip[IPV6_ADDR_LEN - 1]);
		ip_len = IPV6_ADDR_LEN;
	}
	else {
	    return BCME_ERROR;
	}

	if ((entry = MALLOCZ(osh, sizeof(parp_entry_t) + ip_len)) == NULL) {
	    L2_FILTER_MSG(("Allocating new parp_entry for IPv%d failed!!\n", ip_ver));
	    return BCME_NOMEM;
	}

	bcopy((void *)ea, (void *)&entry->ea, ETHER_ADDR_LEN);
	entry->used = entry_tickcnt;
	entry->ip.id = ip_ver;
	entry->ip.len = ip_len;
	bcopy(ip, entry->ip.data, ip_len);
	ptable = arp_tbl;
	if (cached) {
	    entry->next = ptable->parp_table[idx];
	    ptable->parp_table[idx] = entry;
	} else {
	    entry->next = ptable->parp_candidate_list;
	    ptable->parp_candidate_list = entry;
	}
#ifdef DHD_DUMP_ARPTABLE
	bcm_l2_parp_dump_table(arp_tbl);
#endif
	return BCME_OK;
}

/* Delete the IP entry in ARP table based on Cached argument, if cached argument is
 * non zero positive value: it delete from parp_table, else delete from
 * parp_candidate_list
 */
int
bcm_l2_filter_parp_delentry(osl_t* osh, arp_table_t *arp_tbl, struct ether_addr *ea,
	uint8 *ip, uint8 ip_ver, bool cached)
{
	parp_entry_t *entry, *prev = NULL;
	uint8 idx, ip_len;
	arp_table_t *ptable;

	if (ip_ver == IP_VER_4) {
		idx = BCM_PARP_TABLE_INDEX(ip[IPV4_ADDR_LEN - 1]);
		ip_len = IPV4_ADDR_LEN;
	}
	else if (ip_ver == IP_VER_6) {
		idx = BCM_PARP_TABLE_INDEX(ip[IPV6_ADDR_LEN - 1]);
		ip_len = IPV6_ADDR_LEN;
	}
	else {
	    return BCME_ERROR;
	}
	ptable = arp_tbl;
	if (cached) {
	    entry = ptable->parp_table[idx];
	} else {
		entry = ptable->parp_candidate_list;
	}
	while (entry) {
		if (entry->ip.id == ip_ver &&
		    bcmp(entry->ip.data, ip, ip_len) == 0 &&
		    bcmp(&entry->ea, ea, ETHER_ADDR_LEN) == 0) {
			if (prev == NULL) {
			    if (cached) {
				ptable->parp_table[idx] = entry->next;
			    } else {
				ptable->parp_candidate_list = entry->next;
			    }
			} else {
			    prev->next = entry->next;
			}
			break;
		}
		prev = entry;
		entry = entry->next;
	}
	if (entry != NULL)
		MFREE(osh, entry, sizeof(parp_entry_t) + ip_len);
#ifdef DHD_DUMP_ARPTABLE
	bcm_l2_parp_dump_table(arp_tbl);
#endif
	return BCME_OK;
}

/* search the IP entry in ARP table based on Cached argument, if cached argument is
 * non zero positive value: it searches from parp_table, else search from
 * parp_candidate_list
 */
parp_entry_t *
bcm_l2_filter_parp_findentry(arp_table_t* arp_tbl, uint8 *ip, uint8 ip_ver, bool cached,
	unsigned int entry_tickcnt)
{
	parp_entry_t *entry;
	uint8 idx, ip_len;
	arp_table_t *ptable;

	if (ip_ver == IP_VER_4) {
		idx = BCM_PARP_TABLE_INDEX(ip[IPV4_ADDR_LEN - 1]);
		ip_len = IPV4_ADDR_LEN;
	} else if (ip_ver == IP_VER_6) {
		idx = BCM_PARP_TABLE_INDEX(ip[IPV6_ADDR_LEN - 1]);
		ip_len = IPV6_ADDR_LEN;
	} else {
		return NULL;
	}
	ptable = arp_tbl;
	if (cached) {
	    entry = ptable->parp_table[idx];
	} else {
	    entry = ptable->parp_candidate_list;
	}
	while (entry) {
	    if (entry->ip.id == ip_ver && bcmp(entry->ip.data, ip, ip_len) == 0) {
			/* time stamp of adding the station entry to arp table for ifp */
			entry->used = entry_tickcnt;
			break;
	    }
	    entry = entry->next;
	}
	return entry;
}

/* update arp table entries for every proxy arp enable interface */
void
bcm_l2_filter_arp_table_update(osl_t *osh, arp_table_t* arp_tbl, bool all, uint8 *del_ea,
	bool periodic, unsigned int tickcnt)
{
	parp_entry_t *prev, *entry, *delentry;
	uint8 idx, ip_ver;
	struct ether_addr ea;
	uint8 ip[IPV6_ADDR_LEN];
	arp_table_t *ptable;

	ptable = arp_tbl;
	for (idx = 0; idx < BCM_PARP_TABLE_SIZE; idx++) {
		entry = ptable->parp_table[idx];
		while (entry) {
			/* check if the entry need to be removed */
			if (all || (periodic && BCM_PARP_IS_TIMEOUT(tickcnt, entry)) ||
			    (del_ea != NULL && !bcmp(del_ea, &entry->ea, ETHER_ADDR_LEN))) {
				/* copy frame here */
				ip_ver = entry->ip.id;
				bcopy(entry->ip.data, ip, entry->ip.len);
				bcopy(&entry->ea, &ea, ETHER_ADDR_LEN);
				entry = entry->next;
				bcm_l2_filter_parp_delentry(osh, ptable, &ea, ip, ip_ver, TRUE);
			}
			else {
				entry = entry->next;
			}
		}
	}

	/* remove candidate or promote to real entry */
	prev = delentry = NULL;
	entry = ptable->parp_candidate_list;
	while (entry) {
		/* remove candidate */
		if (all || (periodic && BCM_PARP_ANNOUNCE_WAIT_REACH(tickcnt, entry)) ||
		    (del_ea != NULL && !bcmp(del_ea, (uint8 *)&entry->ea, ETHER_ADDR_LEN))) {
			bool promote = (periodic && BCM_PARP_ANNOUNCE_WAIT_REACH(tickcnt, entry)) ?
				TRUE: FALSE;
			parp_entry_t *node = NULL;

			ip_ver = entry->ip.id;

			if (prev == NULL)
				ptable->parp_candidate_list = entry->next;
			else
				prev->next = entry->next;

			node = bcm_l2_filter_parp_findentry(ptable,
				entry->ip.data, IP_VER_6, TRUE, tickcnt);
			if (promote && node == NULL) {
				bcm_l2_filter_parp_addentry(osh, ptable, &entry->ea,
					entry->ip.data, entry->ip.id, TRUE, tickcnt);
			}
			MFREE(osh, entry, sizeof(parp_entry_t) + entry->ip.len);
			if (prev == NULL) {
				entry = ptable->parp_candidate_list;
			} else {
				entry = prev->next;
			}
		}
		else {
			prev = entry;
			entry = entry->next;
		}
	}
}
/* create 42 byte ARP packet for ARP response, aligned the Buffer */
void *
bcm_l2_filter_proxyarp_alloc_reply(osl_t* osh, uint16 pktlen, struct ether_addr *src_ea,
	struct ether_addr *dst_ea, uint16 ea_type, bool snap, void **p)
{
	void *pkt;
	uint8 *frame;

	/* adjust pktlen since skb->data is aligned to 2 */
	pktlen += ALIGN_ADJ_BUFLEN;

	if ((pkt = PKTGET(osh, pktlen, FALSE)) == NULL) {
		L2_FILTER_ERROR(("bcm_l2_filter_proxyarp_alloc_reply: PKTGET failed\n"));
		return NULL;
	}
	/* adjust for pkt->data aligned */
	PKTPULL(osh, pkt, ALIGN_ADJ_BUFLEN);
	frame = PKTDATA(osh, pkt);

	/* Create 14-byte eth header, plus snap header if applicable */
	bcopy(src_ea, frame + ETHER_SRC_OFFSET, ETHER_ADDR_LEN);
	bcopy(dst_ea, frame + ETHER_DEST_OFFSET, ETHER_ADDR_LEN);
	if (snap) {
		hton16_ua_store(pktlen, frame + ETHER_TYPE_OFFSET);
		bcopy(llc_snap_hdr, frame + ETHER_HDR_LEN, SNAP_HDR_LEN);
		hton16_ua_store(ea_type, frame + ETHER_HDR_LEN + SNAP_HDR_LEN);
	} else
		hton16_ua_store(ea_type, frame + ETHER_TYPE_OFFSET);

	*p = (void *)(frame + ETHER_HDR_LEN + (snap ? SNAP_HDR_LEN + ETHER_TYPE_LEN : 0));
	return pkt;
}
/* copy the smac entry from parp_table */
void bcm_l2_filter_parp_get_smac(arp_table_t* ptable, void* smac)
{
	bcopy(ptable->parp_smac, smac, ETHER_ADDR_LEN);
}
/* copy the cmac entry from parp_table */
void bcm_l2_filter_parp_get_cmac(arp_table_t* ptable, void* cmac)
{
	bcopy(ptable->parp_cmac, cmac, ETHER_ADDR_LEN);
}
/* copy the smac entry to smac entry in parp_table */
void bcm_l2_filter_parp_set_smac(arp_table_t* ptable, void* smac)
{
	bcopy(smac, ptable->parp_smac, ETHER_ADDR_LEN);
}
/* copy the cmac entry to cmac entry in parp_table */
void bcm_l2_filter_parp_set_cmac(arp_table_t* ptable, void* cmac)
{
	bcopy(cmac, ptable->parp_cmac, ETHER_ADDR_LEN);
}

uint16
calc_checksum(uint8 *src_ipa, uint8 *dst_ipa, uint32 ul_len, uint8 prot, uint8 *ul_data)
{
	uint16 *startpos;
	uint32 sum = 0;
	int i;
	uint16 answer = 0;

	if (src_ipa) {
		uint8 ph[8] = {0, };
		for (i = 0; i < (IPV6_ADDR_LEN / 2); i++) {
			sum += *((uint16 *)src_ipa);
			src_ipa += 2;
		}

		for (i = 0; i < (IPV6_ADDR_LEN / 2); i++) {
			sum += *((uint16 *)dst_ipa);
			dst_ipa += 2;
		}

		*((uint32 *)ph) = hton32(ul_len);
		*((uint32 *)(ph+4)) = 0;
		ph[7] = prot;
		startpos = (uint16 *)ph;
		for (i = 0; i < 4; i++) {
			sum += *startpos++;
		}
	}

	startpos = (uint16 *)ul_data;
	while (ul_len > 1) {
		sum += *startpos++;
		ul_len -= 2;
	}

	if (ul_len == 1) {
		*((uint8 *)(&answer)) = *((uint8 *)startpos);
		sum += answer;
	}

	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);
	answer = ~sum;

	return answer;
}
/*
 * The length of the option including
 * the type and length fields in units of 8 octets
 */
bcm_tlv_t *
parse_nd_options(void *buf, int buflen, uint key)
{
	bcm_tlv_t *elt;
	int totlen;

	elt = (bcm_tlv_t*)buf;
	totlen = buflen;

	/* find tagged parameter */
	while (totlen >= TLV_HDR_LEN) {
		int len = elt->len * 8;

		/* validate remaining totlen */
		if ((elt->id == key) &&
		    (totlen >= len))
			return (elt);

		elt = (bcm_tlv_t*)((uint8*)elt + len);
		totlen -= len;
	}

	return NULL;
}

/* returns 0 if tdls set up request or tdls discovery request */
int
bcm_l2_filter_block_tdls(osl_t *osh, void *pktbuf)
{
	uint16 ethertype;
	uint8 *data;
	int datalen;
	bool snap;
	uint8 action_field;

	if (get_pkt_ether_type(osh, pktbuf, &data, &datalen, &ethertype, &snap) != BCME_OK)
		return BCME_ERROR;

	if (ethertype != ETHER_TYPE_89_0D)
		return BCME_ERROR;

	/* validate payload type */
	if (datalen < TDLS_PAYLOAD_TYPE_LEN + 2) {
		L2_FILTER_ERROR(("bcm_l2_filter_block_tdls: wrong length for 89-0d eth frame %d\n",
			datalen));
		return BCME_ERROR;
	}

	/* validate payload type */
	if (*data != TDLS_PAYLOAD_TYPE) {
		L2_FILTER_ERROR(("bcm_l2_filter_block_tdls: wrong payload type for 89-0d"
			" eth frame %d\n",
			*data));
		return BCME_ERROR;
	}
	data += TDLS_PAYLOAD_TYPE_LEN;

	/* validate TDLS action category */
	if (*data != TDLS_ACTION_CATEGORY_CODE) {
		L2_FILTER_ERROR(("bcm_l2_filter_block_tdls: wrong TDLS Category %d\n", *data));
		return BCME_ERROR;
	}
	data++;

	action_field = *data;

	if ((action_field == TDLS_SETUP_REQ) || (action_field == TDLS_DISCOVERY_REQ))
		return BCME_OK;

	return BCME_ERROR;
}

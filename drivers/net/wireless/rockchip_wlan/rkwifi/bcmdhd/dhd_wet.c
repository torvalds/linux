/**
 * @file
 * @brief
 * Wireless EThernet (WET) Bridge.
 *
 * WET STA and WET client are inter-exchangable in this file and refer to
 * addressable entities whose traffic are sent and received through this
 * bridge, including the hosting device.
 *
 * Supported protocol families: IP v4.
 *
 * Tx: replace frames' source MAC address with wireless interface's;
 * update the IP-MAC address mapping table entry.
 *
 * Rx: replace frames' the destination MAC address with what found in
 * the IP-MAC address mapping table.
 *
 * All data structures defined in this file are optimized for IP v4. To
 * support other protocol families, write protocol specific handlers.
 * Doing so may require data structures changes to expand various address
 * storages to fit the protocol specific needs, for example, IPX needs 10
 * octets for its network address. Also one may need to define the data
 * structures in a more generic way so that they work with all supported
 * protocol families, for example, the wet_sta strcture may be defined
 * as follow:
 *
 *	struct wet_sta {
 *		uint8 nal;		network address length
 *		uint8 na[NETA_MAX_LEN];	network address
 *		uint8 mac[ETHER_ADDR_LEN];
 *		...
 *	};
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
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

/**
 * @file
 * @brief
 * XXX Twiki: [WirelessEthernet]
 */
#include <typedefs.h>
#include <bcmdefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <siutils.h>
#include <wlioctl.h>
#include <802.11.h>
#include <ethernet.h>
#include <vlan.h>
#include <802.3.h>
#include <bcmip.h>
#include <bcmarp.h>
#include <bcmudp.h>
#include <bcmdhcp.h>
#include <bcmendian.h>
#include <dhd_dbg.h>
#include <d11.h>

#include <dhd_wet.h>

/* IP/MAC address mapping entry */
typedef struct wet_sta wet_sta_t;
struct wet_sta {
	/* client */
	uint8 ip[IPV4_ADDR_LEN];	/* client IP addr */
	struct ether_addr mac;	/* client MAC addr */
	uint8 flags[DHCP_FLAGS_LEN];	/* orig. dhcp flags */
	/* internal */
	wet_sta_t *next;	/* free STA link */
	wet_sta_t *next_ip;	/* hash link by IP */
	wet_sta_t *next_mac;	/* hash link by MAC */
};
#define WET_NUMSTAS		(1 << 8)	/* max. # clients, must be multiple of 2 */
#define WET_STA_HASH_SIZE	(WET_NUMSTAS/2)	/* must be <= WET_NUMSTAS */
#define WET_STA_HASH_IP(ip)	((ip)[3]&(WET_STA_HASH_SIZE-1))	/* hash by IP */
#define WET_STA_HASH_MAC(ea)	(((ea)[3]^(ea)[4]^(ea)[5])&(WET_STA_HASH_SIZE-1)) /* hash by MAC */
#define WET_STA_HASH_UNK	-1 /* Unknown hash */
#define IP_ISMULTI(ip)           (((ip) & 0xf0000000) == 0xe0000000) /* Check for multicast by IP */

/* WET private info structure */
struct dhd_wet_info {
	/* pointer to dhdpublic info struct */
	dhd_pub_t *pub;
	/* Host addresses */
	uint8 ip[IPV4_ADDR_LEN];
	struct ether_addr mac;
	/* STA storage, one entry per eth. client */
	wet_sta_t sta[WET_NUMSTAS];
	/* Free sta list */
	wet_sta_t *stafree;
	/* Used sta hash by IP */
	wet_sta_t *stahash_ip[WET_STA_HASH_SIZE];
	/* Used sta hash by MAC */
	wet_sta_t *stahash_mac[WET_STA_HASH_SIZE];
};

/* forward declarations */
static int wet_eth_proc(dhd_wet_info_t *weth, void *sdu,
	uint8 *frame, int length, int send);
static int wet_vtag_proc(dhd_wet_info_t *weth, void *sdu,
	uint8 * eh, uint8 *vtag, int length, int send);
static int wet_ip_proc(dhd_wet_info_t *weth, void *sdu,
	uint8 * eh, uint8 *iph, int length, int send);
static int wet_arp_proc(dhd_wet_info_t *weth, void *sdu,
	uint8 *eh, uint8 *arph, int length, int send);
static int wet_udp_proc(dhd_wet_info_t *weth,
	uint8 *eh, uint8 *iph, uint8 *udph, int length, int send);
static int wet_dhcpc_proc(dhd_wet_info_t *weth,
	uint8 *eh, uint8 *iph, uint8 *udph, uint8 *dhcp, int length, int send);
static int wet_dhcps_proc(dhd_wet_info_t *weth,
	uint8 *eh, uint8 *iph, uint8 *udph, uint8 *dhcp, int length, int send);
static int wet_sta_alloc(dhd_wet_info_t *weth, wet_sta_t **saddr);
static int wet_sta_update_all(dhd_wet_info_t *weth,
	uint8 *iaddr, struct ether_addr *eaddr, wet_sta_t **saddr);
static int wet_sta_update_mac(dhd_wet_info_t *weth,
	struct ether_addr *eaddr, wet_sta_t **saddr);
static int wet_sta_remove_mac_entry(dhd_wet_info_t *weth, struct ether_addr *eaddr);
static int wet_sta_find_ip(dhd_wet_info_t *weth,
	uint8 *iaddr, wet_sta_t **saddr);
static int wet_sta_find_mac(dhd_wet_info_t *weth,
	struct ether_addr *eaddr, wet_sta_t **saddr);
static void csum_fixup_16(uint8 *chksum,
	uint8 *optr, int olen, uint8 *nptr, int nlen);

/*
 * Protocol handler. 'ph' points to protocol specific header,
 * for example, it points to IP header if it is IP packet.
 */
typedef int (*prot_proc_t)(dhd_wet_info_t *weth, void *sdu, uint8 *eh,
	uint8 *ph, int length, int send);
/* Protocol handlers hash table - hash by ether type */
typedef struct prot_hdlr prot_hdlr_t;
struct prot_hdlr {
	uint16 type;		/* ether type */
	prot_proc_t prot_proc;
	prot_hdlr_t *next;	/* next proto handler that has the same hash */
};
#define WET_PROT_HASH_SIZE	(1 << 3)
#define WET_PROT_HASH(t)	((t)[1]&(WET_PROT_HASH_SIZE-1))
static prot_hdlr_t ept_tbl[] = {
	/* 0 */ {HTON16(ETHER_TYPE_8021Q), wet_vtag_proc, NULL}, /* 0x8100 */
};
static prot_hdlr_t prot_hash[WET_PROT_HASH_SIZE] = {
	/* 0 */ {HTON16(ETHER_TYPE_IP), wet_ip_proc, &ept_tbl[0]}, /* 0x0800 */
	/* 1 */ {0, NULL, NULL},	/* unused   */
	/* 2 */ {0, NULL, NULL},	/* unused   */
	/* 3 */ {0, NULL, NULL},	/* unused   */
	/* 4 */ {0, NULL, NULL},	/* unused   */
	/* 5 */ {0, NULL, NULL},	/* unused   */
	/* 6 */ {HTON16(ETHER_TYPE_ARP), wet_arp_proc, NULL},	/* 0x0806 */
	/* 7 */ {0, NULL, NULL},	/* unused   */
};

/*
 * IPv4 handler. 'ph' points to protocol specific header,
 * for example, it points to UDP header if it is UDP packet.
 */
typedef int (*ipv4_proc_t)(dhd_wet_info_t *weth, uint8 *eh,
	uint8 *iph, uint8 *ph, int length, int send);
/* IPv4 handlers hash table - hash by protocol type */
typedef struct ipv4_hdlr ipv4_hdlr_t;
struct ipv4_hdlr {
	uint8 type;	/* protocol type */
	ipv4_proc_t ipv4_proc;
	ipv4_hdlr_t *next;	/* next proto handler that has the same hash */
};
#define WET_IPV4_HASH_SIZE	(1 << 1)
#define WET_IPV4_HASH(p)	((p)&(WET_IPV4_HASH_SIZE-1))
static ipv4_hdlr_t ipv4_hash[WET_IPV4_HASH_SIZE] = {
	/* 0 */ {0, NULL, NULL},	/* unused   */
	/* 1 */ {IP_PROT_UDP, wet_udp_proc, NULL},	/* 0x11 */
};

/*
 * UDP handler. 'ph' points to protocol specific header,
 * for example, it points to DHCP header if it is DHCP packet.
 */
typedef int (*udp_proc_t)(dhd_wet_info_t *weth, uint8 *eh,
	uint8 *iph, uint8 *udph, uint8 *ph, int length, int send);
/* UDP handlers hash table - hash by port number */
typedef struct udp_hdlr udp_hdlr_t;
struct udp_hdlr {
	uint16 port;	/* udp dest. port */
	udp_proc_t udp_proc;
	udp_hdlr_t *next;	/* next proto handler that has the same hash */
};
#define WET_UDP_HASH_SIZE	(1 << 3)
#define WET_UDP_HASH(p)	((p)[1]&(WET_UDP_HASH_SIZE-1))
static udp_hdlr_t udp_hash[WET_UDP_HASH_SIZE] = {
	/* 0 */ {0, NULL, NULL},	/* unused   */
	/* 1 */ {0, NULL, NULL},	/* unused   */
	/* 2 */ {0, NULL, NULL},	/* unused   */
	/* 3 */ {HTON16(DHCP_PORT_SERVER), wet_dhcpc_proc, NULL}, /* 0x43 */
	/* 4 */ {HTON16(DHCP_PORT_CLIENT), wet_dhcps_proc, NULL}, /* 0x44 */
	/* 5 */ {0, NULL, NULL},	/* unused   */
	/* 6 */ {0, NULL, NULL},	/* unused   */
	/* 7 */ {0, NULL, NULL},	/* unused   */
};

#define WETHWADDR(weth)	((weth)->pub->mac.octet)
#define WETOSH(weth)	((weth)->pub->osh)

/* special values */
/* 802.3 llc/snap header */
static uint8 llc_snap_hdr[SNAP_HDR_LEN] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00};
static uint8 ipv4_bcast[IPV4_ADDR_LEN] = {0xff, 0xff, 0xff, 0xff}; /* IP v4 broadcast address */
static uint8 ipv4_null[IPV4_ADDR_LEN] = {0x00, 0x00, 0x00, 0x00}; /* IP v4 NULL address */

dhd_wet_info_t *
dhd_get_wet_info(dhd_pub_t *pub)
{
	dhd_wet_info_t *p;
	int i;
	p = (dhd_wet_info_t *)MALLOCZ(pub->osh, sizeof(dhd_wet_info_t));
	if (p == NULL) {
		return 0;
	}
	for (i = 0; i < WET_NUMSTAS - 1; i ++)
		p->sta[i].next = &p->sta[i + 1];
	p->stafree = &p->sta[0];
	p->pub = pub;
	return p;
}

void
dhd_free_wet_info(dhd_pub_t *pub, void *wet)
{
	if (wet) {
		MFREE(pub->osh, wet, sizeof(dhd_wet_info_t));
	}
}

void dhd_set_wet_host_ipv4(dhd_pub_t *pub, void *parms, uint32 len)
{
	dhd_wet_info_t *p;
	p = (dhd_wet_info_t *)pub->wet_info;
	bcopy(parms, p->ip, len);
}

void dhd_set_wet_host_mac(dhd_pub_t *pub, void *parms, uint32 len)
{
	dhd_wet_info_t *p;
	p = (dhd_wet_info_t *)pub->wet_info;
	bcopy(parms, &p->mac, len);
}
/* process Ethernet frame */
/*
* Return:
*	= 0 if frame is done ok
*	< 0 if unable to handle the frame
*	> 0 if no further process
*/
static int
BCMFASTPATH(wet_eth_proc)(dhd_wet_info_t *weth, void *sdu, uint8 *frame, int length, int send)
{
	uint8 *pt = frame + ETHER_TYPE_OFFSET;
	uint16 type;
	uint8 *ph;
	prot_hdlr_t *phdlr;
	/* intercept Ethernet II frame (type > 1500) */
	if (length >= ETHER_HDR_LEN && (pt[0] > (ETHER_MAX_DATA >> 8) ||
	    (pt[0] == (ETHER_MAX_DATA >> 8) && pt[1] > (ETHER_MAX_DATA & 0xff))))
		;
	/* intercept 802.3 LLC/SNAP frame (type <= 1500) */
	else if (length >= ETHER_HDR_LEN + SNAP_HDR_LEN + ETHER_TYPE_LEN) {
		uint8 *llc = frame + ETHER_HDR_LEN;
		if (bcmp(llc_snap_hdr, llc, SNAP_HDR_LEN))
			return 0;
		pt = llc + SNAP_HDR_LEN;
	}
	/* frame too short bail out */
	else {
		DHD_ERROR(("wet_eth_proc: %s short eth frame, ignored\n",
			send ? "send" : "recv"));
		return -1;
	}
	ph = pt + ETHER_TYPE_LEN;
	length -= ph - frame;

	/* Call protocol specific handler to process frame. */
	type = *(uint16 *)pt;

	for (phdlr = &prot_hash[WET_PROT_HASH(pt)];
	     phdlr != NULL; phdlr = phdlr->next) {
		if (phdlr->type != type || !phdlr->prot_proc)
			continue;
		return (phdlr->prot_proc)(weth, sdu, frame, ph, length, send);
	}

	if (!bcmp(WETHWADDR(weth), frame + ETHER_SRC_OFFSET, ETHER_ADDR_LEN)) {
		return 0;
	}
	else {
		DHD_INFO(("%s: %s unknown type (0x%X), ignored %s\n",
			__FUNCTION__, send ? "send" : "recv", type,
			(type == 0xDD86) ? "IPv6":""));
		/* ignore unsupported protocol from different mac addr than us */
		return BCME_UNSUPPORTED;
	}
}

/* process 8021p/Q tagged frame */
/*
* Return:
*	= 0 if frame is done ok
*	< 0 if unable to handle the frame
*	> 0 if no further process
*/
static int
BCMFASTPATH(wet_vtag_proc)(dhd_wet_info_t *weth, void *sdu,
	uint8 * eh, uint8 *vtag, int length, int send)
{
	uint16 type;
	uint8 *pt;
	prot_hdlr_t *phdlr;

	/* check minimum length */
	if (length < ETHERVLAN_HDR_LEN) {
		DHD_ERROR(("wet_vtag_proc: %s short VLAN frame, ignored\n",
			send ? "send" : "recv"));
		return -1;
	}

	/*
	 * FIXME: check recursiveness to prevent stack from overflow
	 * in case someone sent frames 8100xxxxxxxx8100xxxxxxxx...
	 */

	/* Call protocol specific handler to process frame. */
	type = *(uint16 *)(pt = vtag + VLAN_TAG_LEN);

	for (phdlr = &prot_hash[WET_PROT_HASH(pt)];
	     phdlr != NULL; phdlr = phdlr->next) {
		if (phdlr->type != type || !phdlr->prot_proc)
			continue;
		return (phdlr->prot_proc)(weth, sdu, eh,
			pt + ETHER_TYPE_LEN, length, send);
	}

	return 0;
}

/* process IP frame */
/*
* Return:
*	= 0 if frame is done ok
*	< 0 if unable to handle the frame
*       > 0 if no further process
*/
static int
BCMFASTPATH(wet_ip_proc)(dhd_wet_info_t *weth, void *sdu,
		uint8 *eh, uint8 *iph, int length, int send)
{
	uint8 type;
	int ihl;
	wet_sta_t *sta;
	ipv4_hdlr_t *iphdlr;
	uint8 *iaddr;
	struct ether_addr *ea = NULL;
	int ret, ea_off = 0;
	char eabuf[ETHER_ADDR_STR_LEN];
	BCM_REFERENCE(eabuf);

	/* IPv4 only */
	if (length < 1 || (IP_VER(iph) != IP_VER_4)) {
		DHD_INFO(("wet_ip_proc: %s non IPv4 frame, ignored\n",
			send ? "send" : "recv"));
		return -1;
	}

	ihl = IPV4_HLEN(iph);

	/* minimum length */
	if (length < ihl) {
		DHD_ERROR(("wet_ip_proc: %s short IPv4 frame, ignored\n",
		send ? "send" : "recv"));
		return -1;
	}

	/* protocol specific handling */
	type = IPV4_PROT(iph);
	for (iphdlr = &ipv4_hash[WET_IPV4_HASH(type)];
			iphdlr; iphdlr = iphdlr->next) {
		if (iphdlr->type != type || !iphdlr->ipv4_proc)
			continue;
		if ((ret = (iphdlr->ipv4_proc)(weth, eh,
			iph, iph + ihl, length - ihl, send)))
			return ret;
	}

	/* generic IP packet handling
	 * Replace source MAC in Ethernet header with wireless's and
	 * keep track of IP MAC mapping when sending frame.
	 */
	if (send) {
		uint32 iaddr_dest, iaddr_src;
		bool wet_table_upd = TRUE;
		iaddr = iph + IPV4_SRC_IP_OFFSET;
		iaddr_dest = ntoh32(*((uint32 *)(iph + IPV4_DEST_IP_OFFSET)));
		iaddr_src = ntoh32(*(uint32 *)(iaddr));

		/* Do not process and update knowledge base on receipt of a local IP
		 * multicast frame
		 */
		if (IP_ISMULTI(iaddr_dest) && !iaddr_src) {
			DHD_INFO(("recv multicast frame from %s.Don't update hash table\n",
				bcm_ether_ntoa((struct ether_addr*)
				(eh + ETHER_SRC_OFFSET), eabuf)));
			wet_table_upd = FALSE;
		}
		if (wet_table_upd && wet_sta_update_all(weth, iaddr,
				(struct ether_addr*)(eh + ETHER_SRC_OFFSET), &sta) < 0) {
			DHD_INFO(("wet_ip_proc: unable to update STA %u.%u.%u.%u %s\n",
				iaddr[0], iaddr[1], iaddr[2], iaddr[3],
				bcm_ether_ntoa(
				(struct ether_addr*)(eh + ETHER_SRC_OFFSET), eabuf)));
			return -1;
		}
		ea = (struct ether_addr *)WETHWADDR(weth);
		ea_off = ETHER_SRC_OFFSET;
		eacopy(ea, eh + ea_off);
	}
	/*
	 * Replace dest MAC in Ethernet header using the found one
	 * when receiving frame.
	 */
	/* no action for received bcast/mcast ethernet frame */
	else if (!ETHER_ISMULTI(eh)) {
		iaddr = iph + IPV4_DEST_IP_OFFSET;
		if (wet_sta_find_ip(weth, iaddr, &sta) < 0) {
			DHD_ERROR(("wet_ip_proc: unable to find STA %u.%u.%u.%u\n",
				iaddr[0], iaddr[1], iaddr[2], iaddr[3]));
			return -1;
		}
		ea = &sta->mac;
		ea_off = ETHER_DEST_OFFSET;
		eacopy(ea, eh + ea_off);
	}

	return 0;
}

/* process ARP frame - ARP proxy */
/*
 * Return:
 *	= 0 if frame is done ok
 *	< 0 if unable to handle the frame
 *       > 0 if no further process
 */
static int
BCMFASTPATH(wet_arp_proc)(dhd_wet_info_t *weth, void *sdu,
		uint8 *eh, uint8 *arph, int length, int send)
{
	wet_sta_t *sta;
	uint8 *iaddr;
	char eabuf[ETHER_ADDR_STR_LEN];
	BCM_REFERENCE(eabuf);

	/*
	 * FIXME: validate ARP header:
	 *  h/w Ethernet 2, proto IP x800, h/w addr size 6, proto addr size 4.
	 */

	/*
	 * Replace source MAC in Ethernet header as well as source MAC in
	 * ARP protocol header when processing frame sent.
	 */
	if (send) {
		iaddr = arph + ARP_SRC_IP_OFFSET;
		if (wet_sta_update_all(weth, iaddr,
				(struct ether_addr*)(eh + ETHER_SRC_OFFSET), &sta) < 0) {
			DHD_INFO(("wet_arp_proc: unable to update STA %u.%u.%u.%u %s\n",
					iaddr[0], iaddr[1], iaddr[2], iaddr[3],
					bcm_ether_ntoa(
					(struct ether_addr*)(eh + ETHER_SRC_OFFSET), eabuf)));
			return -1;
		}
		bcopy(WETHWADDR(weth), eh + ETHER_SRC_OFFSET, ETHER_ADDR_LEN);
		bcopy(WETHWADDR(weth), arph+ARP_SRC_ETH_OFFSET, ETHER_ADDR_LEN);
	}
	/*
	 * Replace dest MAC in Ethernet header as well as dest MAC in
	 * ARP protocol header when processing frame recv'd. Process ARP
	 * replies and Unicast ARP requests.
	 */
	else if ((*(uint16 *)(arph + ARP_OPC_OFFSET) == HTON16(ARP_OPC_REPLY)) ||
		((*(uint16 *)(arph + ARP_OPC_OFFSET) == HTON16(ARP_OPC_REQUEST)) &&
		(!ETHER_ISMULTI(eh)))) {
		iaddr = arph + ARP_TGT_IP_OFFSET;
		if (wet_sta_find_ip(weth, iaddr, &sta) < 0) {
			DHD_INFO(("wet_arp_proc: unable to find STA %u.%u.%u.%u\n",
				iaddr[0], iaddr[1], iaddr[2], iaddr[3]));
			return -1;
		}
		bcopy(&sta->mac, arph + ARP_TGT_ETH_OFFSET, ETHER_ADDR_LEN);
		bcopy(&sta->mac, eh + ETHER_DEST_OFFSET, ETHER_ADDR_LEN);
	}

	return 0;
}

/* process UDP frame */
/*
 * Return:
 *	= 0 if frame is done ok
 *	< 0 if unable to handle the frame
 *       > 0 if no further process
 */
static int
BCMFASTPATH(wet_udp_proc)(dhd_wet_info_t *weth,
		uint8 *eh, uint8 *iph, uint8 *udph, int length, int send)
{
	udp_hdlr_t *udphdlr;
	uint16 port;

	/* check frame length, at least UDP_HDR_LEN */
	if ((length -= UDP_HDR_LEN) < 0) {
		DHD_ERROR(("wet_udp_proc: %s short UDP frame, ignored\n",
			send ? "send" : "recv"));
		return -1;
	}

	/*
	 * Unfortunately we must spend some time here to deal with
	 * some higher layer protocol special processings.
	 * See individual handlers for protocol specific details.
	 */
	port = *(uint16 *)(udph + UDP_DEST_PORT_OFFSET);
	for (udphdlr = &udp_hash[WET_UDP_HASH((uint8 *)&port)];
			udphdlr; udphdlr = udphdlr->next) {
		if (udphdlr->port != port || !udphdlr->udp_proc)
			continue;
		return (udphdlr->udp_proc)(weth, eh, iph, udph,
				udph + UDP_HDR_LEN, length, send);
	}

	return 0;
}

/*
 * DHCP is a 'complex' protocol for WET, mainly because it
 * uses its protocol body to convey IP/MAC info. It is impossible
 * to forward frames correctly back and forth without looking
 * into the DHCP's body and interpreting it. See RFC2131 sect.
 * 4.1 'Constructing and sending DHCP messages' for details
 * of using/parsing various fields in the body.
 *
 * DHCP pass through:
 *
 *       Must alter DHCP flag to broadcast so that the server
 *       can reply with the broadcast address before we can
 *	 provide DHCP relay functionality. Otherwise the DHCP
 *       server will send DHCP replies using the DHCP client's
 *       MAC address. Such replies will not be delivered simply
 *       because:
 *
 *         1. The AP's bridge will not forward the replies back to
 *         this device through the wireless link because it does not
 *         know such node exists on this link. The bridge's forwarding
 *         table on the AP will have this device's MAC address only.
 *         It does not know anything else behind this device.
 *
 *         2. The AP's wireless driver won't allow such frames out
 *         either even if they made their way out the AP's bridge
 *         through the bridge's DLF broadcasting because there is
 *         no such STA associated with the AP.
 *
 *         3. This device's MAC won't allow such frames pass
 *         through in non-promiscuous mode even when they made
 *         their way out of the AP's wireless interface somehow.
 *
 * DHCP relay:
 *
 *       Once the WET is configured with the host MAC address it can
 *       relay the host request as if it were sent from WET itself.
 *
 *       Once the WET is configured with the host IP address it can
 *       pretend to be the host and act as a relay agent.
 *
 * process DHCP client frame (client to server, or server to relay agent)
 * Return:
 *	= 0 if frame is done ok
 *	< 0 if unable to handle the frame
 *      > 0 if no further process
 */
static int
BCMFASTPATH(wet_dhcpc_proc)(dhd_wet_info_t *weth,
		uint8 *eh, uint8 *iph, uint8 *udph, uint8 *dhcp, int length, int send)
{
	wet_sta_t *sta;
	uint16 flags;
	char eabuf[ETHER_ADDR_STR_LEN];
	uint16 port;
	uint8 *ipv4;
	const struct ether_addr *ether;
	BCM_REFERENCE(eabuf);

	/*
	 * FIXME: validate DHCP body:
	 * htype Ethernet 1, hlen Ethernet 6, frame length at least 242.
	 */

	/* only interested in requests when sending to server */
	if (send && *(dhcp + DHCP_TYPE_OFFSET) != DHCP_TYPE_REQUEST)
		return 0;
	/* only interested in replies when receiving from server as a relay agent */
	if (!send && *(dhcp + DHCP_TYPE_OFFSET) != DHCP_TYPE_REPLY)
		return 0;

	/* send request */
	if (send) {
		/* find existing or alloc new IP/MAC mapping entry */
		if (wet_sta_update_mac(weth,
				(struct ether_addr*)(dhcp + DHCP_CHADDR_OFFSET), &sta) < 0) {
			DHD_INFO(("wet_dhcpc_proc: unable to update STA %s\n",
				bcm_ether_ntoa(
				(struct ether_addr*)(dhcp + DHCP_CHADDR_OFFSET), eabuf)));
			return -1;
		}
		bcopy(dhcp + DHCP_FLAGS_OFFSET, &flags, DHCP_FLAGS_LEN);
		/* We can always relay the host's request when we know its MAC addr. */
		if (!ETHER_ISNULLADDR(weth->mac.octet) &&
				!bcmp(dhcp + DHCP_CHADDR_OFFSET, &weth->mac, ETHER_ADDR_LEN)) {
			/* replace chaddr with host's MAC */
			csum_fixup_16(udph + UDP_CHKSUM_OFFSET,
					dhcp + DHCP_CHADDR_OFFSET, ETHER_ADDR_LEN,
					WETHWADDR(weth), ETHER_ADDR_LEN);
			bcopy(WETHWADDR(weth), dhcp + DHCP_CHADDR_OFFSET, ETHER_ADDR_LEN);
			/* force reply to be unicast */
			flags &= ~HTON16(DHCP_FLAG_BCAST);
		}
		/* We can relay other clients' requests when we know the host's IP addr. */
		else if (!IPV4_ADDR_NULL(weth->ip)) {
			/* we can only handle the first hop otherwise drop it */
			if (!IPV4_ADDR_NULL(dhcp + DHCP_GIADDR_OFFSET)) {
				DHD_INFO(("wet_dhcpc_proc: not first hop, ignored\n"));
				return -1;
			}
			/* replace giaddr with host's IP */
			csum_fixup_16(udph + UDP_CHKSUM_OFFSET,
					dhcp + DHCP_GIADDR_OFFSET, IPV4_ADDR_LEN,
					weth->ip, IPV4_ADDR_LEN);
			bcopy(weth->ip, dhcp + DHCP_GIADDR_OFFSET, IPV4_ADDR_LEN);
			/* force reply to be unicast */
			flags &= ~HTON16(DHCP_FLAG_BCAST);
		}
		/*
		 * Request comes in when we don't know the host's MAC and/or IP
		 * addresses hence we can't relay the request. We must notify the
		 * server of our addressing limitation by turning on the broadcast
		 * bit at this point as what the function comments point out.
		 */
		else
			flags |= HTON16(DHCP_FLAG_BCAST);
		/* update flags */
		bcopy(dhcp + DHCP_FLAGS_OFFSET, sta->flags, DHCP_FLAGS_LEN);
		if (flags != *(uint16 *)sta->flags) {
			csum_fixup_16(udph + UDP_CHKSUM_OFFSET,
					dhcp + DHCP_FLAGS_OFFSET, DHCP_FLAGS_LEN,
					(uint8 *)&flags, DHCP_FLAGS_LEN);
			bcopy((uint8 *)&flags, dhcp + DHCP_FLAGS_OFFSET,
					DHCP_FLAGS_LEN);
		}
		/* replace the Ethernet source MAC with ours */
		bcopy(WETHWADDR(weth), eh + ETHER_SRC_OFFSET, ETHER_ADDR_LEN);
	}
	/* relay recv'd reply to its destiny */
	else if (!IPV4_ADDR_NULL(weth->ip) &&
			!bcmp(dhcp + DHCP_GIADDR_OFFSET, weth->ip, IPV4_ADDR_LEN)) {
		/* find IP/MAC mapping entry */
		if (wet_sta_find_mac(weth,
		(struct ether_addr*)(dhcp + DHCP_CHADDR_OFFSET), &sta) < 0) {
			DHD_INFO(("wet_dhcpc_proc: unable to find STA %s\n",
				bcm_ether_ntoa(
				(struct ether_addr*)(dhcp + DHCP_CHADDR_OFFSET), eabuf)));
			return -1;
		}
		/*
		 * XXX the following code works for the first hop only
		 */
		/* restore the DHCP giaddr with its original */
		csum_fixup_16(udph + UDP_CHKSUM_OFFSET,
				dhcp + DHCP_GIADDR_OFFSET, IPV4_ADDR_LEN,
				ipv4_null, IPV4_ADDR_LEN);
		bcopy(ipv4_null, dhcp + DHCP_GIADDR_OFFSET, IPV4_ADDR_LEN);
		/* restore the original client's dhcp flags */
		if (bcmp(dhcp + DHCP_FLAGS_OFFSET, sta->flags, DHCP_FLAGS_LEN)) {
			csum_fixup_16(udph + UDP_CHKSUM_OFFSET,
					dhcp + DHCP_FLAGS_OFFSET, DHCP_FLAGS_LEN,
					sta->flags, DHCP_FLAGS_LEN);
			bcopy(sta->flags, dhcp + DHCP_FLAGS_OFFSET, DHCP_FLAGS_LEN);
		}
		/* replace the dest UDP port with DHCP client port */
		port = HTON16(DHCP_PORT_CLIENT);
		csum_fixup_16(udph + UDP_CHKSUM_OFFSET,
				udph + UDP_DEST_PORT_OFFSET, UDP_PORT_LEN,
				(uint8 *)&port, UDP_PORT_LEN);
		bcopy((uint8 *)&port, udph + UDP_DEST_PORT_OFFSET, UDP_PORT_LEN);
		/* replace the dest MAC & IP addr with the client's */
		if (*(uint16 *)sta->flags & HTON16(DHCP_FLAG_BCAST)) {
			ipv4 = ipv4_bcast;
			ether = &ether_bcast;
		}
		else {
			ipv4 = dhcp + DHCP_YIADDR_OFFSET;
			ether = &sta->mac;
		}
		csum_fixup_16(udph + UDP_CHKSUM_OFFSET,
				iph + IPV4_DEST_IP_OFFSET, IPV4_ADDR_LEN,
				ipv4, IPV4_ADDR_LEN);
		csum_fixup_16(iph + IPV4_CHKSUM_OFFSET,
				iph + IPV4_DEST_IP_OFFSET, IPV4_ADDR_LEN,
				ipv4, IPV4_ADDR_LEN);
		bcopy(ipv4, iph + IPV4_DEST_IP_OFFSET, IPV4_ADDR_LEN);
		bcopy(ether, eh + ETHER_DEST_OFFSET, ETHER_ADDR_LEN);
	}
	/* it should not recv non-relay reply at all, but just in case */
	else {
		DHD_INFO(("wet_dhcpc_proc: ignore recv'd frame from %s\n",
		bcm_ether_ntoa((struct ether_addr*)(dhcp + DHCP_CHADDR_OFFSET), eabuf)));
		return -1;
	}

	/* no further processing! */
	return 1;
}

/* process DHCP server frame (server to client) */
/*
 * Return:
 *	= 0 if frame is done ok
 *	< 0 if unable to handle the frame
 *      > 0 if no further process
 */
static int
BCMFASTPATH(wet_dhcps_proc)(dhd_wet_info_t *weth,
	uint8 *eh, uint8 *iph, uint8 *udph, uint8 *dhcp, int length, int send)
{
	wet_sta_t *sta;
	char eabuf[ETHER_ADDR_STR_LEN];
	BCM_REFERENCE(eabuf);

	/*
	 * FIXME: validate DHCP body:
	 *  htype Ethernet 1, hlen Ethernet 6, frame length at least 242.
	 */

	/* only interested in replies when receiving from server */
	if (send || *(dhcp + DHCP_TYPE_OFFSET) != DHCP_TYPE_REPLY)
		return 0;

	/* find IP/MAC mapping entry */
	if (wet_sta_find_mac(weth, (struct ether_addr*)(dhcp + DHCP_CHADDR_OFFSET), &sta) < 0) {
		DHD_INFO(("wet_dhcps_proc: unable to find STA %s\n",
		bcm_ether_ntoa((struct ether_addr*)(dhcp + DHCP_CHADDR_OFFSET), eabuf)));
		return -1;
	}
	/* relay the reply to the host when we know the host's MAC addr */
	if (!ETHER_ISNULLADDR(weth->mac.octet) &&
			!bcmp(dhcp + DHCP_CHADDR_OFFSET, WETHWADDR(weth), ETHER_ADDR_LEN)) {
		csum_fixup_16(udph + UDP_CHKSUM_OFFSET,
				dhcp + DHCP_CHADDR_OFFSET, ETHER_ADDR_LEN,
				weth->mac.octet, ETHER_ADDR_LEN);
		bcopy(&weth->mac, dhcp + DHCP_CHADDR_OFFSET, ETHER_ADDR_LEN);
	}
	/* restore the original client's dhcp flags if necessary */
	if (bcmp(dhcp + DHCP_FLAGS_OFFSET, sta->flags, DHCP_FLAGS_LEN)) {
		csum_fixup_16(udph + UDP_CHKSUM_OFFSET,
				dhcp + DHCP_FLAGS_OFFSET, DHCP_FLAGS_LEN,
				sta->flags, DHCP_FLAGS_LEN);
		bcopy(sta->flags, dhcp + DHCP_FLAGS_OFFSET, DHCP_FLAGS_LEN);
	}
	/* replace the dest MAC with that of client's */
	if (*(uint16 *)sta->flags & HTON16(DHCP_FLAG_BCAST))
		bcopy((const uint8 *)&ether_bcast, eh + ETHER_DEST_OFFSET, ETHER_ADDR_LEN);
	else
		bcopy(&sta->mac, eh + ETHER_DEST_OFFSET, ETHER_ADDR_LEN);

	/* no further processing! */
	return 1;
}

/* alloc IP/MAC mapping entry
 * Returns 0 if succeeded; < 0 otherwise.
 */
static int
wet_sta_alloc(dhd_wet_info_t *weth, wet_sta_t **saddr)
{
	wet_sta_t *sta;

	/* allocate a new one */
	if (!weth->stafree) {
		DHD_INFO(("wet_sta_alloc: no room for another STA\n"));
		return -1;
	}
	sta = weth->stafree;
	weth->stafree = sta->next;

	/* init them just in case */
	sta->next = NULL;
	sta->next_ip = NULL;
	sta->next_mac = NULL;

	*saddr = sta;
	return 0;
}

/* update IP/MAC mapping entry and hash
 * Returns 0 if succeeded; < 0 otherwise.
 */
static int
BCMFASTPATH(wet_sta_update_all)(dhd_wet_info_t *weth, uint8 *iaddr, struct ether_addr *eaddr,
		wet_sta_t **saddr)
{
	wet_sta_t *sta;
	int i;
	char eabuf[ETHER_ADDR_STR_LEN];
	BCM_REFERENCE(eabuf);

	/* find the existing one and remove it from the old IP hash link */
	if (!wet_sta_find_mac(weth, eaddr, &sta)) {
		i = WET_STA_HASH_IP(sta->ip);
		if (bcmp(sta->ip, iaddr, IPV4_ADDR_LEN)) {
			wet_sta_t *sta2, **next;
			for (next = &weth->stahash_ip[i], sta2 = *next;
			sta2; sta2 = sta2->next_ip) {
				if (sta2 == sta)
					break;
				next = &sta2->next_ip;
			}
			if (sta2) {
				*next = sta2->next_ip;
				sta2->next_ip = NULL;
			}
			i = WET_STA_HASH_UNK;
		}
	}
	/* allocate a new one and hash it by MAC */
	else if (!wet_sta_alloc(weth, &sta)) {
		i = WET_STA_HASH_MAC(eaddr->octet);
		bcopy(eaddr, &sta->mac, ETHER_ADDR_LEN);
		sta->next_mac = weth->stahash_mac[i];
		weth->stahash_mac[i] = sta;
		i = WET_STA_HASH_UNK;
	}
	/* bail out if we can't find nor create any */
	else {
		DHD_INFO(("wet_sta_update_all: unable to alloc STA %u.%u.%u.%u %s\n",
		iaddr[0], iaddr[1], iaddr[2], iaddr[3],
		bcm_ether_ntoa(eaddr, eabuf)));
		return -1;
	}

	/* update IP and hash by new IP */
	if (i == WET_STA_HASH_UNK) {
		i = WET_STA_HASH_IP(iaddr);
		bcopy(iaddr, sta->ip, IPV4_ADDR_LEN);
		sta->next_ip = weth->stahash_ip[i];
		weth->stahash_ip[i] = sta;

		/* start here and look for other entries with same IP address */
		{
			wet_sta_t *sta2, *prev;
			prev = sta;
			for (sta2 = sta->next_ip;	sta2; sta2 = sta2->next_ip) {
				/* does this entry have the same IP address? */
				if (!bcmp(sta->ip, sta2->ip, IPV4_ADDR_LEN)) {
					/* sta2 currently points to the entry we need to remove */
					/* fix next pointers */
					prev->next_ip = sta2->next_ip;
					sta2->next_ip = NULL;
					/* now we need to find this guy in the MAC list and
					   remove it from that list too.
					   */
					wet_sta_remove_mac_entry(weth, &sta2->mac);
					/* entry should be completely out of the table now,
					   add it to the free list
					   */
					memset(sta2, 0, sizeof(wet_sta_t));
					sta2->next = weth->stafree;
					weth->stafree = sta2;

					sta2 = prev;
				}
				prev = sta2;
			}
		}
	}

	*saddr = sta;
	return 0;
}

/* update IP/MAC mapping entry and hash */
static int
BCMFASTPATH(wet_sta_update_mac)(dhd_wet_info_t *weth, struct ether_addr *eaddr, wet_sta_t **saddr)
{
	wet_sta_t *sta;
	int i;
	char eabuf[ETHER_ADDR_STR_LEN];
	BCM_REFERENCE(eabuf);

	/* find the existing one */
	if (!wet_sta_find_mac(weth, eaddr, &sta))
		;
	/* allocate a new one and hash it */
	else if (!wet_sta_alloc(weth, &sta)) {
		i = WET_STA_HASH_MAC(eaddr->octet);
		bcopy(eaddr, &sta->mac, ETHER_ADDR_LEN);
		sta->next_mac = weth->stahash_mac[i];
		weth->stahash_mac[i] = sta;
	}
	/* bail out if we can't find nor create any */
	else {
		DHD_INFO(("wet_sta_update_mac: unable to alloc STA %s\n",
		bcm_ether_ntoa(eaddr, eabuf)));
		return -1;
	}

	*saddr = sta;
	return 0;
}

/*  Remove MAC entry from hash list
 *  NOTE:  This only removes the entry matching "eaddr" from the MAC
 *  list.  The caller needs to remove from the IP list and
 *  put back onto the free list to completely remove the entry
 *  from the WET table.
 */
static int
BCMFASTPATH(wet_sta_remove_mac_entry)(dhd_wet_info_t *weth, struct ether_addr *eaddr)
{
	wet_sta_t *sta, *prev;
	int i = WET_STA_HASH_MAC(eaddr->octet);
	char eabuf[ETHER_ADDR_STR_LEN];
	int found = 0;
	BCM_REFERENCE(eabuf);

	/* find the existing one */
	for (sta = prev = weth->stahash_mac[i]; sta; sta = sta->next_mac) {
		if (!bcmp(&sta->mac, eaddr, ETHER_ADDR_LEN)) {
			found = 1;
			break;
		}
		prev = sta;
	}

	/* bail out if we can't find */
	if (!found) {
		DHD_INFO(("wet_sta_remove_mac_entry: unable to find STA %s entry\n",
		bcm_ether_ntoa(eaddr, eabuf)));
		return -1;
	}

	/* fix the list */
	if (prev == sta)
		weth->stahash_mac[i] = sta->next_mac; /* removing first entry in this bucket */
	else
		prev->next_mac = sta->next_mac;

	return 0;
}

/* find IP/MAC mapping entry by IP address
 * Returns 0 if succeeded; < 0 otherwise.
 */
static int
BCMFASTPATH(wet_sta_find_ip)(dhd_wet_info_t *weth, uint8 *iaddr, wet_sta_t **saddr)
{
	int i = WET_STA_HASH_IP(iaddr);
	wet_sta_t *sta;

	/* find the existing one by IP */
	for (sta = weth->stahash_ip[i]; sta; sta = sta->next_ip) {
		if (bcmp(sta->ip, iaddr, IPV4_ADDR_LEN))
			continue;
		*saddr = sta;
		return 0;
	}

	/* sta has not been learned */
	DHD_INFO(("wet_sta_find_ip: unable to find STA %u.%u.%u.%u\n",
		iaddr[0], iaddr[1], iaddr[2], iaddr[3]));
	return -1;
}

/* find IP/MAC mapping entry by MAC address
 * Returns 0 if succeeded; < 0 otherwise.
 */
static int
BCMFASTPATH(wet_sta_find_mac)(dhd_wet_info_t *weth, struct ether_addr *eaddr, wet_sta_t **saddr)
{
	int i = WET_STA_HASH_MAC(eaddr->octet);
	wet_sta_t *sta;
	char eabuf[ETHER_ADDR_STR_LEN];
	BCM_REFERENCE(eabuf);

	/* find the existing one by MAC */
	for (sta = weth->stahash_mac[i]; sta; sta = sta->next_mac) {
		if (bcmp(&sta->mac, eaddr, ETHER_ADDR_LEN))
			continue;
		*saddr = sta;
		return 0;
	}

	/* sta has not been learnt */
	DHD_INFO(("wet_sta_find_mac: unable to find STA %s\n",
		bcm_ether_ntoa(eaddr, eabuf)));
	return -1;
}

/* Adjust 16 bit checksum - taken from RFC 3022.
 *
 *   The algorithm below is applicable only for even offsets (i.e., optr
 *   below must be at an even offset from start of header) and even lengths
 *   (i.e., olen and nlen below must be even).
 */
static void
BCMFASTPATH(csum_fixup_16)(uint8 *chksum, uint8 *optr, int olen, uint8 *nptr, int nlen)
{
	long x, old, new;

	ASSERT(!((uintptr_t)optr&1) && !(olen&1));
	ASSERT(!((uintptr_t)nptr&1) && !(nlen&1));

	x = (chksum[0]<< 8)+chksum[1];
	if (!x)
		return;
	x = ~x & 0xFFFF;
	while (olen)
	{
		old = (optr[0]<< 8)+optr[1]; optr += 2;
		x -= old & 0xffff;
		if (x <= 0) { x--; x &= 0xffff; }
		olen -= 2;
	}
	while (nlen)
	{
		new = (nptr[0]<< 8)+nptr[1]; nptr += 2;
		x += new & 0xffff;
		if (x & 0x10000) { x++; x &= 0xffff; }
		nlen -= 2;
	}
	x = ~x & 0xFFFF;
	chksum[0] = (uint8)(x >> 8); chksum[1] = (uint8)x;
}

/* Process frames in transmit direction by replacing source MAC with
 * wireless's and keep track of IP MAC address mapping table.
 * Return:
 *	= 0 if frame is done ok;
 *	< 0 if unable to handle the frame;
 *
 * To avoid other interfaces to see our changes specially
 * changes to broadcast frame which definitely will be seen by
 * other bridged interfaces we must copy the frame to our own
 * buffer, modify it, and then sent it.
 * Return the new sdu in 'new'.
 */
int
BCMFASTPATH(dhd_wet_send_proc)(void *wet, void *sdu, void **new)
{
	dhd_wet_info_t *weth = (dhd_wet_info_t *)wet;
	uint8 *frame = PKTDATA(WETOSH(weth), sdu);
	int length = PKTLEN(WETOSH(weth), sdu);
	void *pkt = sdu;

	/*
	 * FIXME: need to tell if buffer is shared and only
	 * do copy on shared buffer.
	 */
	/*
	 * copy broadcast/multicast frame to our own packet
	 * otherwise we will screw up others because we alter
	 * the frame content.
	 */
	if (length < ETHER_HDR_LEN) {
		DHD_ERROR(("dhd_wet_send_proc: unable to process short frame\n"));
		return -1;
	}
	if (ETHER_ISMULTI(frame)) {
		length = pkttotlen(WETOSH(weth), sdu);
		if (!(pkt = PKTGET(WETOSH(weth), length, TRUE))) {
			DHD_ERROR(("dhd_wet_send_proc: unable to alloc, dropped\n"));
			return -1;
		}
		frame = PKTDATA(WETOSH(weth), pkt);
		pktcopy(WETOSH(weth), sdu, 0, length, frame);
		/* Transfer priority */
		PKTSETPRIO(pkt, PKTPRIO(sdu));
		PKTFREE(WETOSH(weth), sdu, TRUE);
		PKTSETLEN(WETOSH(weth), pkt, length);
	}
	*new = pkt;

	/* process frame */
	return wet_eth_proc(weth, sdu, frame, length, 1) < 0 ? -1 : 0;
}

/*
 * Process frames in receive direction by replacing destination MAC with
 * the one found in IP MAC address mapping table.
 * Return:
 *	= 0 if frame is done ok;
 *	< 0 if unable to handle the frame;
 */
int
BCMFASTPATH(dhd_wet_recv_proc)(void *wet, void *sdu)
{
	dhd_wet_info_t *weth = (dhd_wet_info_t *)wet;
	/* process frame */
	return wet_eth_proc(weth, sdu, PKTDATA(WETOSH(weth), sdu),
			PKTLEN(WETOSH(weth), sdu), 0) < 0 ? -1 : 0;
}

/* Delete WET Database */
void
dhd_wet_sta_delete_list(dhd_pub_t *dhd_pub)
{
	wet_sta_t *sta;
	int i, j;
	dhd_wet_info_t *weth = dhd_pub->wet_info;

	for (i = 0; i < WET_STA_HASH_SIZE; i ++) {
		for (sta = weth->stahash_mac[i]; sta; sta = sta->next_mac) {
			wet_sta_t *sta2, **next;
			j = WET_STA_HASH_IP(sta->ip);
			for (next = &weth->stahash_ip[j], sta2 = *next;
					sta2; sta2 = sta2->next_ip) {
				if (sta2 == sta)
					break;
				next = &sta2->next_ip;
			}
			if (sta2) {
				*next = sta2->next_ip;
				sta2->next_ip = NULL;
			}
			j = WET_STA_HASH_UNK;

			wet_sta_remove_mac_entry(weth, &sta->mac);
			memset(sta, 0, sizeof(wet_sta_t));
		}
	}
}
void
dhd_wet_dump(dhd_pub_t *dhdp, struct bcmstrbuf *b)
{
	char eabuf[ETHER_ADDR_STR_LEN];
	wet_sta_t *sta;
	int i;
	dhd_wet_info_t *weth = dhdp->wet_info;

	bcm_bprintf(b, "Host MAC: %s\n", bcm_ether_ntoa(&weth->mac, eabuf));
	bcm_bprintf(b, "Host IP: %u.%u.%u.%u\n",
			weth->ip[0], weth->ip[1], weth->ip[2], weth->ip[3]);
	bcm_bprintf(b, "Entry\tEnetAddr\t\tInetAddr\n");
	for (i = 0; i < WET_NUMSTAS; i ++) {
		/* FIXME: it leaves the last sta entry unfiltered, who cares! */
		if (weth->sta[i].next)
			continue;
		/* format the entry dump */
		sta = &weth->sta[i];
		bcm_bprintf(b, "%u\t%s\t%u.%u.%u.%u\n",
				i, bcm_ether_ntoa(&sta->mac, eabuf),
				sta->ip[0], sta->ip[1], sta->ip[2], sta->ip[3]);
	}
}

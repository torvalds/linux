/*
 * IP Packet Parser Module.
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id$
 */
#include <typedefs.h>
#include <osl.h>

#include <proto/ethernet.h>
#include <proto/vlan.h>
#include <proto/802.3.h>
#include <proto/bcmip.h>
#include <bcmendian.h>

#include <dhd_dbg.h>

#include <dhd_ip.h>

/* special values */
/* 802.3 llc/snap header */
static const uint8 llc_snap_hdr[SNAP_HDR_LEN] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00};

pkt_frag_t pkt_frag_info(osl_t *osh, void *p)
{
	uint8 *frame;
	int length;
	uint8 *pt;			/* Pointer to type field */
	uint16 ethertype;
	struct ipv4_hdr *iph;		/* IP frame pointer */
	int ipl;			/* IP frame length */
	uint16 iph_frag;

	ASSERT(osh && p);

	frame = PKTDATA(osh, p);
	length = PKTLEN(osh, p);

	/* Process Ethernet II or SNAP-encapsulated 802.3 frames */
	if (length < ETHER_HDR_LEN) {
		DHD_INFO(("%s: short eth frame (%d)\n", __FUNCTION__, length));
		return DHD_PKT_FRAG_NONE;
	} else if (ntoh16(*(uint16 *)(frame + ETHER_TYPE_OFFSET)) >= ETHER_TYPE_MIN) {
		/* Frame is Ethernet II */
		pt = frame + ETHER_TYPE_OFFSET;
	} else if (length >= ETHER_HDR_LEN + SNAP_HDR_LEN + ETHER_TYPE_LEN &&
	           !bcmp(llc_snap_hdr, frame + ETHER_HDR_LEN, SNAP_HDR_LEN)) {
		pt = frame + ETHER_HDR_LEN + SNAP_HDR_LEN;
	} else {
		DHD_INFO(("%s: non-SNAP 802.3 frame\n", __FUNCTION__));
		return DHD_PKT_FRAG_NONE;
	}

	ethertype = ntoh16(*(uint16 *)pt);

	/* Skip VLAN tag, if any */
	if (ethertype == ETHER_TYPE_8021Q) {
		pt += VLAN_TAG_LEN;

		if (pt + ETHER_TYPE_LEN > frame + length) {
			DHD_INFO(("%s: short VLAN frame (%d)\n", __FUNCTION__, length));
			return DHD_PKT_FRAG_NONE;
		}

		ethertype = ntoh16(*(uint16 *)pt);
	}

	if (ethertype != ETHER_TYPE_IP) {
		DHD_INFO(("%s: non-IP frame (ethertype 0x%x, length %d)\n",
			__FUNCTION__, ethertype, length));
		return DHD_PKT_FRAG_NONE;
	}

	iph = (struct ipv4_hdr *)(pt + ETHER_TYPE_LEN);
	ipl = length - (pt + ETHER_TYPE_LEN - frame);

	/* We support IPv4 only */
	if ((ipl < IPV4_OPTIONS_OFFSET) || (IP_VER(iph) != IP_VER_4)) {
		DHD_INFO(("%s: short frame (%d) or non-IPv4\n", __FUNCTION__, ipl));
		return DHD_PKT_FRAG_NONE;
	}

	iph_frag = ntoh16(iph->frag);

	if (iph_frag & IPV4_FRAG_DONT) {
		return DHD_PKT_FRAG_NONE;
	} else if ((iph_frag & IPV4_FRAG_MORE) == 0) {
		return DHD_PKT_FRAG_LAST;
	} else {
		return (iph_frag & IPV4_FRAG_OFFSET_MASK)? DHD_PKT_FRAG_CONT : DHD_PKT_FRAG_FIRST;
	}
}

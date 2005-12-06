/*
 * linux/drivers/s390/net/qeth_tso.h ($Revision: 1.7 $)
 *
 * Header file for qeth TCP Segmentation Offload support.
 *
 * Copyright 2004 IBM Corporation
 *
 *    Author(s): Frank Pavlic <fpavlic@de.ibm.com>
 *
 *    $Revision: 1.7 $	 $Date: 2005/05/04 20:19:18 $
 *
 */
#ifndef __QETH_TSO_H__
#define __QETH_TSO_H__

#include <linux/skbuff.h>
#include <linux/tcp.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/ip6_checksum.h>
#include "qeth.h"
#include "qeth_mpc.h"


static inline struct qeth_hdr_tso *
qeth_tso_prepare_skb(struct qeth_card *card, struct sk_buff **skb)
{
	QETH_DBF_TEXT(trace, 5, "tsoprsk");
	return qeth_push_skb(card, skb, sizeof(struct qeth_hdr_tso));
}

/**
 * fill header for a TSO packet
 */
static inline void
qeth_tso_fill_header(struct qeth_card *card, struct sk_buff *skb)
{
	struct qeth_hdr_tso *hdr;
	struct tcphdr *tcph;
	struct iphdr *iph;

	QETH_DBF_TEXT(trace, 5, "tsofhdr");

	hdr  = (struct qeth_hdr_tso *) skb->data;
	iph  = skb->nh.iph;
	tcph = skb->h.th;
	/*fix header to TSO values ...*/
	hdr->hdr.hdr.l3.id = QETH_HEADER_TYPE_TSO;
	/*set values which are fix for the first approach ...*/
	hdr->ext.hdr_tot_len = (__u16) sizeof(struct qeth_hdr_ext_tso);
	hdr->ext.imb_hdr_no  = 1;
	hdr->ext.hdr_type    = 1;
	hdr->ext.hdr_version = 1;
	hdr->ext.hdr_len     = 28;
	/*insert non-fix values */
	hdr->ext.mss = skb_shinfo(skb)->tso_size;
	hdr->ext.dg_hdr_len = (__u16)(iph->ihl*4 + tcph->doff*4);
	hdr->ext.payload_len = (__u16)(skb->len - hdr->ext.dg_hdr_len -
				       sizeof(struct qeth_hdr_tso));
}

/**
 * change some header values as requested by hardware
 */
static inline void
qeth_tso_set_tcpip_header(struct qeth_card *card, struct sk_buff *skb)
{
	struct iphdr *iph;
	struct ipv6hdr *ip6h;
	struct tcphdr *tcph;

	iph  = skb->nh.iph;
	ip6h = skb->nh.ipv6h;
	tcph = skb->h.th;

	tcph->check = 0;
	if (skb->protocol == ETH_P_IPV6) {
		ip6h->payload_len = 0;
		tcph->check = ~csum_ipv6_magic(&ip6h->saddr, &ip6h->daddr,
					       0, IPPROTO_TCP, 0);
		return;
	}
	/*OSA want us to set these values ...*/
	tcph->check = ~csum_tcpudp_magic(iph->saddr, iph->daddr,
					 0, IPPROTO_TCP, 0);
	iph->tot_len = 0;
	iph->check = 0;
}

static inline int
qeth_tso_prepare_packet(struct qeth_card *card, struct sk_buff *skb,
			int ipv, int cast_type)
{
	struct qeth_hdr_tso *hdr;

	QETH_DBF_TEXT(trace, 5, "tsoprep");

	hdr = (struct qeth_hdr_tso *) qeth_tso_prepare_skb(card, &skb);
	if (hdr == NULL) {
		QETH_DBF_TEXT(trace, 4, "tsoperr");
		return -ENOMEM;
	}
	memset(hdr, 0, sizeof(struct qeth_hdr_tso));
	/*fill first 32 bytes of  qdio header as used
	 *FIXME: TSO has two struct members
	 * with different names but same size
	 * */
	qeth_fill_header(card, &hdr->hdr, skb, ipv, cast_type);
	qeth_tso_fill_header(card, skb);
	qeth_tso_set_tcpip_header(card, skb);
	return 0;
}

static inline void
__qeth_fill_buffer_frag(struct sk_buff *skb, struct qdio_buffer *buffer,
			int is_tso, int *next_element_to_fill)
{
	struct skb_frag_struct *frag;
	int fragno;
	unsigned long addr;
	int element, cnt, dlen;
	
	fragno = skb_shinfo(skb)->nr_frags;
	element = *next_element_to_fill;
	dlen = 0;
	
	if (is_tso)
		buffer->element[element].flags =
			SBAL_FLAGS_MIDDLE_FRAG;
	else
		buffer->element[element].flags =
			SBAL_FLAGS_FIRST_FRAG;
	if ( (dlen = (skb->len - skb->data_len)) ) {
		buffer->element[element].addr = skb->data;
		buffer->element[element].length = dlen;
		element++;
	}
	for (cnt = 0; cnt < fragno; cnt++) {
		frag = &skb_shinfo(skb)->frags[cnt];
		addr = (page_to_pfn(frag->page) << PAGE_SHIFT) +
			frag->page_offset;
		buffer->element[element].addr = (char *)addr;
		buffer->element[element].length = frag->size;
		if (cnt < (fragno - 1))
			buffer->element[element].flags =
				SBAL_FLAGS_MIDDLE_FRAG;
		else
			buffer->element[element].flags =
				SBAL_FLAGS_LAST_FRAG;
		element++;
	}
	*next_element_to_fill = element;
}
#endif /* __QETH_TSO_H__ */

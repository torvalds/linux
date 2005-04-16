/*
 * linux/drivers/s390/net/qeth_tso.c ($Revision: 1.6 $)
 *
 * Header file for qeth TCP Segmentation Offload support.
 *
 * Copyright 2004 IBM Corporation
 *
 *    Author(s): Frank Pavlic <pavlic@de.ibm.com>
 *
 *    $Revision: 1.6 $	 $Date: 2005/03/24 09:04:18 $
 *
 */

#include <linux/skbuff.h>
#include <linux/tcp.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/ip6_checksum.h>
#include "qeth.h"
#include "qeth_mpc.h"
#include "qeth_tso.h"

/**
 * skb already partially prepared
 * classic qdio header in skb->data
 * */
static inline struct qeth_hdr_tso *
qeth_tso_prepare_skb(struct qeth_card *card, struct sk_buff **skb)
{
	int rc = 0;

	QETH_DBF_TEXT(trace, 5, "tsoprsk");
	rc = qeth_realloc_headroom(card, skb,sizeof(struct qeth_hdr_ext_tso));
	if (rc)
		return NULL;

	return qeth_push_skb(card, skb, sizeof(struct qeth_hdr_ext_tso));
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

static inline struct qeth_hdr_tso *
qeth_tso_prepare_packet(struct qeth_card *card, struct sk_buff *skb,
			int ipv, int cast_type)
{
	struct qeth_hdr_tso *hdr;
	int rc = 0;

	QETH_DBF_TEXT(trace, 5, "tsoprep");

	/*get headroom for tso qdio header */
	hdr = (struct qeth_hdr_tso *) qeth_tso_prepare_skb(card, &skb);
	if (hdr == NULL) {
		QETH_DBF_TEXT_(trace, 4, "2err%d", rc);
		return NULL;
	}
	memset(hdr, 0, sizeof(struct qeth_hdr_tso));
	/*fill first 32 bytes of  qdio header as used
	 *FIXME: TSO has two struct members
	 * with different names but same size
	 * */
	qeth_fill_header(card, &hdr->hdr, skb, ipv, cast_type);
	qeth_tso_fill_header(card, skb);
	qeth_tso_set_tcpip_header(card, skb);
	return hdr;
}

static inline int
qeth_tso_get_queue_buffer(struct qeth_qdio_out_q *queue)
{
	struct qeth_qdio_out_buffer *buffer;
	int flush_cnt = 0;

	QETH_DBF_TEXT(trace, 5, "tsobuf");

	/* force to non-packing*/
	if (queue->do_pack)
		queue->do_pack = 0;
	buffer = &queue->bufs[queue->next_buf_to_fill];
	/* get a new buffer if current is already in use*/
	if ((atomic_read(&buffer->state) == QETH_QDIO_BUF_EMPTY) &&
	    (buffer->next_element_to_fill > 0)) {
		atomic_set(&buffer->state, QETH_QDIO_BUF_PRIMED);
		queue->next_buf_to_fill = (queue->next_buf_to_fill + 1) %
					  QDIO_MAX_BUFFERS_PER_Q;
		flush_cnt++;
	}
	return flush_cnt;
}

static inline void
__qeth_tso_fill_buffer_frag(struct qeth_qdio_out_buffer *buf,
			  struct sk_buff *skb)
{
	struct skb_frag_struct *frag;
	struct qdio_buffer *buffer;
	int fragno, cnt, element;
	unsigned long addr;

        QETH_DBF_TEXT(trace, 6, "tsfilfrg");

	/*initialize variables ...*/
	fragno = skb_shinfo(skb)->nr_frags;
	buffer = buf->buffer;
	element = buf->next_element_to_fill;
	/*fill buffer elements .....*/
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
	buf->next_element_to_fill = element;
}

static inline int
qeth_tso_fill_buffer(struct qeth_qdio_out_buffer *buf,
		     struct sk_buff *skb)
{
        int length, length_here, element;
        int hdr_len;
	struct qdio_buffer *buffer;
	struct qeth_hdr_tso *hdr;
	char *data;

        QETH_DBF_TEXT(trace, 3, "tsfilbuf");

	/*increment user count and queue skb ...*/
        atomic_inc(&skb->users);
        skb_queue_tail(&buf->skb_list, skb);

	/*initialize all variables...*/
        buffer = buf->buffer;
	hdr = (struct qeth_hdr_tso *)skb->data;
	hdr_len = sizeof(struct qeth_hdr_tso) + hdr->ext.dg_hdr_len;
	data = skb->data + hdr_len;
	length = skb->len - hdr_len;
        element = buf->next_element_to_fill;
	/*fill first buffer entry only with header information */
	buffer->element[element].addr = skb->data;
	buffer->element[element].length = hdr_len;
	buffer->element[element].flags = SBAL_FLAGS_FIRST_FRAG;
	buf->next_element_to_fill++;

	if (skb_shinfo(skb)->nr_frags > 0) {
                 __qeth_tso_fill_buffer_frag(buf, skb);
                 goto out;
        }

       /*start filling buffer entries ...*/
        element++;
        while (length > 0) {
                /* length_here is the remaining amount of data in this page */
		length_here = PAGE_SIZE - ((unsigned long) data % PAGE_SIZE);
		if (length < length_here)
                        length_here = length;
                buffer->element[element].addr = data;
                buffer->element[element].length = length_here;
                length -= length_here;
                if (!length)
                        buffer->element[element].flags =
                                SBAL_FLAGS_LAST_FRAG;
                 else
                         buffer->element[element].flags =
                                 SBAL_FLAGS_MIDDLE_FRAG;
                data += length_here;
                element++;
        }
        /*set the buffer to primed  ...*/
        buf->next_element_to_fill = element;
out:
	atomic_set(&buf->state, QETH_QDIO_BUF_PRIMED);
        return 1;
}

int
qeth_tso_send_packet(struct qeth_card *card, struct sk_buff *skb,
		     struct qeth_qdio_out_q *queue, int ipv, int cast_type)
{
	int flush_cnt = 0;
	struct qeth_hdr_tso *hdr;
	struct qeth_qdio_out_buffer *buffer;
        int start_index;

	QETH_DBF_TEXT(trace, 3, "tsosend");

	if (!(hdr = qeth_tso_prepare_packet(card, skb, ipv, cast_type)))
	     	return -ENOMEM;
	/*check if skb fits in one SBAL ...*/
	if (!(qeth_get_elements_no(card, (void*)hdr, skb)))
		return -EINVAL;
	/*lock queue, force switching to non-packing and send it ...*/
	while (atomic_compare_and_swap(QETH_OUT_Q_UNLOCKED,
                                       QETH_OUT_Q_LOCKED,
                                       &queue->state));
        start_index = queue->next_buf_to_fill;
        buffer = &queue->bufs[queue->next_buf_to_fill];
	/*check if card is too busy ...*/
	if (atomic_read(&buffer->state) != QETH_QDIO_BUF_EMPTY){
		card->stats.tx_dropped++;
		goto out;
	}
	/*let's force to non-packing and get a new SBAL*/
	flush_cnt += qeth_tso_get_queue_buffer(queue);
	buffer = &queue->bufs[queue->next_buf_to_fill];
	if (atomic_read(&buffer->state) != QETH_QDIO_BUF_EMPTY) {
		card->stats.tx_dropped++;
		goto out;
	}
	flush_cnt += qeth_tso_fill_buffer(buffer, skb);
	queue->next_buf_to_fill = (queue->next_buf_to_fill + 1) %
				   QDIO_MAX_BUFFERS_PER_Q;
out:
	atomic_set(&queue->state, QETH_OUT_Q_UNLOCKED);
	if (flush_cnt)
		qeth_flush_buffers(queue, 0, start_index, flush_cnt);
	/*do some statistics */
	card->stats.tx_packets++;
	card->stats.tx_bytes += skb->len;
	return 0;
}

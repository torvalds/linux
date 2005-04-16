/*
 * linux/drivers/s390/net/qeth_tso.h ($Revision: 1.4 $)
 *
 * Header file for qeth TCP Segmentation Offload support.
 *
 * Copyright 2004 IBM Corporation
 *
 *    Author(s): Frank Pavlic <pavlic@de.ibm.com>
 *
 *    $Revision: 1.4 $	 $Date: 2005/03/24 09:04:18 $
 *
 */
#ifndef __QETH_TSO_H__
#define __QETH_TSO_H__


extern int
qeth_tso_send_packet(struct qeth_card *, struct sk_buff *,
		     struct qeth_qdio_out_q *, int , int);

struct qeth_hdr_ext_tso {
        __u16 hdr_tot_len;
        __u8  imb_hdr_no;
        __u8  reserved;
        __u8  hdr_type;
        __u8  hdr_version;
        __u16 hdr_len;
        __u32 payload_len;
        __u16 mss;
        __u16 dg_hdr_len;
        __u8  padding[16];
} __attribute__ ((packed));

struct qeth_hdr_tso {
        struct qeth_hdr hdr; 	/*hdr->hdr.l3.xxx*/
	struct qeth_hdr_ext_tso ext;
} __attribute__ ((packed));

/*some helper functions*/

static inline int
qeth_get_elements_no(struct qeth_card *card, void *hdr, struct sk_buff *skb)
{
	int elements_needed = 0;

	if (skb_shinfo(skb)->nr_frags > 0)
		elements_needed = (skb_shinfo(skb)->nr_frags + 1);
	if (elements_needed == 0 )
		elements_needed = 1 + (((((unsigned long) hdr) % PAGE_SIZE)
					+ skb->len) >> PAGE_SHIFT);
	if (elements_needed > QETH_MAX_BUFFER_ELEMENTS(card)){
		PRINT_ERR("qeth_do_send_packet: invalid size of "
			  "IP packet. Discarded.");
		return 0;
	}
	return elements_needed;
}
#endif /* __QETH_TSO_H__ */

/*
 * linux/drivers/s390/net/qeth_tso.h ($Revision: 1.5 $)
 *
 * Header file for qeth TCP Segmentation Offload support.
 *
 * Copyright 2004 IBM Corporation
 *
 *    Author(s): Frank Pavlic <pavlic@de.ibm.com>
 *
 *    $Revision: 1.5 $	 $Date: 2005/04/01 21:40:41 $
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

static inline void
__qeth_fill_buffer_frag(struct sk_buff *skb, struct qdio_buffer *buffer,
			int is_tso, int *next_element_to_fill)
{
	int length = skb->len;
	struct skb_frag_struct *frag;
	int fragno;
	unsigned long addr;
	int element;
	int first_lap = 1;

	fragno = skb_shinfo(skb)->nr_frags; /* start with last frag */
	element = *next_element_to_fill + fragno;
	while (length > 0) {
		if (fragno > 0) {
			frag = &skb_shinfo(skb)->frags[fragno - 1];
			addr = (page_to_pfn(frag->page) << PAGE_SHIFT) +
				frag->page_offset;
			buffer->element[element].addr = (char *)addr;
			buffer->element[element].length = frag->size;
			length -= frag->size;
			if (first_lap)
				buffer->element[element].flags =
				    SBAL_FLAGS_LAST_FRAG;
			else
				buffer->element[element].flags =
				    SBAL_FLAGS_MIDDLE_FRAG;
		} else {
			buffer->element[element].addr = skb->data;
			buffer->element[element].length = length;
			length = 0;
			if (is_tso)
				buffer->element[element].flags =
					SBAL_FLAGS_MIDDLE_FRAG;
			else
				buffer->element[element].flags =
					SBAL_FLAGS_FIRST_FRAG;
		}
		element--;
		fragno--;
		first_lap = 0;
	}
	*next_element_to_fill += skb_shinfo(skb)->nr_frags + 1;
}

#endif /* __QETH_TSO_H__ */

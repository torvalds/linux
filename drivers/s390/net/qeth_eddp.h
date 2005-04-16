/*
 * linux/drivers/s390/net/qeth_eddp.c ($Revision: 1.5 $)
 *
 * Header file for qeth enhanced device driver pakcing.
 *
 * Copyright 2004 IBM Corporation
 *
 *    Author(s): Thomas Spatzier <tspat@de.ibm.com>
 *
 *    $Revision: 1.5 $	 $Date: 2005/03/24 09:04:18 $
 *
 */
#ifndef __QETH_EDDP_H__
#define __QETH_EDDP_H__

struct qeth_eddp_element {
	u32 flags;
	u32 length;
	void *addr;
};

struct qeth_eddp_context {
	atomic_t refcnt;
	enum qeth_large_send_types type;
	int num_pages;			    /* # of allocated pages */
	u8 **pages;			    /* pointers to pages */
	int offset;			    /* offset in ctx during creation */
	int num_elements;		    /* # of required 'SBALEs' */
	struct qeth_eddp_element *elements; /* array of 'SBALEs' */
	int elements_per_skb;		    /* # of 'SBALEs' per skb **/
};

struct qeth_eddp_context_reference {
	struct list_head list;
	struct qeth_eddp_context *ctx;
};

extern struct qeth_eddp_context *
qeth_eddp_create_context(struct qeth_card *,struct sk_buff *,struct qeth_hdr *);

extern void
qeth_eddp_put_context(struct qeth_eddp_context *);

extern int
qeth_eddp_fill_buffer(struct qeth_qdio_out_q *,struct qeth_eddp_context *,int);

extern void
qeth_eddp_buf_release_contexts(struct qeth_qdio_out_buffer *);

extern int
qeth_eddp_check_buffers_for_context(struct qeth_qdio_out_q *,
				    struct qeth_eddp_context *);
/*
 * Data used for fragmenting a IP packet.
 */
struct qeth_eddp_data {
	struct qeth_hdr qh;
	struct ethhdr mac;
	u16 vlan[2];
	union {
		struct {
			struct iphdr h;
			u8 options[40];
		} ip4;
		struct {
			struct ipv6hdr h;
		} ip6;
	} nh;
	u8 nhl;
	void *nh_in_ctx;	/* address of nh within the ctx */
	union {
		struct {
			struct tcphdr h;
			u8 options[40];
		} tcp;
	} th;
	u8 thl;
	void *th_in_ctx;	/* address of th within the ctx */
	struct sk_buff *skb;
	int skb_offset;
	int frag;
	int frag_offset;
} __attribute__ ((packed));

#endif /* __QETH_EDDP_H__ */

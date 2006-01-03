/*
 *
 * linux/drivers/s390/net/qeth_eddp.c ($Revision: 1.13 $)
 *
 * Enhanced Device Driver Packing (EDDP) support for the qeth driver.
 *
 * Copyright 2004 IBM Corporation
 *
 *    Author(s): Thomas Spatzier <tspat@de.ibm.com>
 *
 *    $Revision: 1.13 $	 $Date: 2005/05/04 20:19:18 $
 *
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/ip.h>
#include <linux/inetdevice.h>
#include <linux/netdevice.h>
#include <linux/kernel.h>
#include <linux/tcp.h>
#include <net/tcp.h>
#include <linux/skbuff.h>

#include <net/ip.h>

#include "qeth.h"
#include "qeth_mpc.h"
#include "qeth_eddp.h"

int
qeth_eddp_check_buffers_for_context(struct qeth_qdio_out_q *queue,
				    struct qeth_eddp_context *ctx)
{
	int index = queue->next_buf_to_fill;
	int elements_needed = ctx->num_elements;
	int elements_in_buffer;
	int skbs_in_buffer;
	int buffers_needed = 0;

	QETH_DBF_TEXT(trace, 5, "eddpcbfc");
	while(elements_needed > 0) {
		buffers_needed++;
		if (atomic_read(&queue->bufs[index].state) !=
				QETH_QDIO_BUF_EMPTY)
			return -EBUSY;

		elements_in_buffer = QETH_MAX_BUFFER_ELEMENTS(queue->card) -
				     queue->bufs[index].next_element_to_fill;
		skbs_in_buffer = elements_in_buffer / ctx->elements_per_skb;
		elements_needed -= skbs_in_buffer * ctx->elements_per_skb;
		index = (index + 1) % QDIO_MAX_BUFFERS_PER_Q;
	}
	return buffers_needed;
}

static inline void
qeth_eddp_free_context(struct qeth_eddp_context *ctx)
{
	int i;

	QETH_DBF_TEXT(trace, 5, "eddpfctx");
	for (i = 0; i < ctx->num_pages; ++i)
		free_page((unsigned long)ctx->pages[i]);
	kfree(ctx->pages);
	if (ctx->elements != NULL)
		kfree(ctx->elements);
	kfree(ctx);
}


static inline void
qeth_eddp_get_context(struct qeth_eddp_context *ctx)
{
	atomic_inc(&ctx->refcnt);
}

void
qeth_eddp_put_context(struct qeth_eddp_context *ctx)
{
	if (atomic_dec_return(&ctx->refcnt) == 0)
		qeth_eddp_free_context(ctx);
}

void
qeth_eddp_buf_release_contexts(struct qeth_qdio_out_buffer *buf)
{
	struct qeth_eddp_context_reference *ref;
	
	QETH_DBF_TEXT(trace, 6, "eddprctx");
	while (!list_empty(&buf->ctx_list)){
		ref = list_entry(buf->ctx_list.next,
				 struct qeth_eddp_context_reference, list);
		qeth_eddp_put_context(ref->ctx);
		list_del(&ref->list);
		kfree(ref);
	}
}

static inline int
qeth_eddp_buf_ref_context(struct qeth_qdio_out_buffer *buf,
			  struct qeth_eddp_context *ctx)
{
	struct qeth_eddp_context_reference *ref;

	QETH_DBF_TEXT(trace, 6, "eddprfcx");
	ref = kmalloc(sizeof(struct qeth_eddp_context_reference), GFP_ATOMIC);
	if (ref == NULL)
		return -ENOMEM;
	qeth_eddp_get_context(ctx);
	ref->ctx = ctx;
	list_add_tail(&ref->list, &buf->ctx_list);
	return 0;
}

int
qeth_eddp_fill_buffer(struct qeth_qdio_out_q *queue,
		      struct qeth_eddp_context *ctx,
		      int index)
{
	struct qeth_qdio_out_buffer *buf = NULL;
	struct qdio_buffer *buffer;
	int elements = ctx->num_elements;
	int element = 0;
	int flush_cnt = 0;
	int must_refcnt = 1;
	int i;

	QETH_DBF_TEXT(trace, 5, "eddpfibu");
	while (elements > 0) {
		buf = &queue->bufs[index];
		if (atomic_read(&buf->state) != QETH_QDIO_BUF_EMPTY){
			/* normally this should not happen since we checked for
			 * available elements in qeth_check_elements_for_context
			 */
			if (element == 0)
				return -EBUSY;
			else {
				PRINT_WARN("could only partially fill eddp "
					   "buffer!\n");
				goto out;
			}
		}		
		/* check if the whole next skb fits into current buffer */
		if ((QETH_MAX_BUFFER_ELEMENTS(queue->card) -
					buf->next_element_to_fill)
				< ctx->elements_per_skb){
			/* no -> go to next buffer */
			atomic_set(&buf->state, QETH_QDIO_BUF_PRIMED);
			index = (index + 1) % QDIO_MAX_BUFFERS_PER_Q;
			flush_cnt++;
			/* new buffer, so we have to add ctx to buffer'ctx_list
			 * and increment ctx's refcnt */
			must_refcnt = 1;
			continue;
		}	
		if (must_refcnt){
			must_refcnt = 0;
			if (qeth_eddp_buf_ref_context(buf, ctx)){
				PRINT_WARN("no memory to create eddp context "
					   "reference\n");
				goto out_check;
			}
		}
		buffer = buf->buffer;
		/* fill one skb into buffer */
		for (i = 0; i < ctx->elements_per_skb; ++i){
			buffer->element[buf->next_element_to_fill].addr =
				ctx->elements[element].addr;
			buffer->element[buf->next_element_to_fill].length =
				ctx->elements[element].length;
			buffer->element[buf->next_element_to_fill].flags =
				ctx->elements[element].flags;
			buf->next_element_to_fill++;
			element++;
			elements--;
		}
	}
out_check:
	if (!queue->do_pack) {
		QETH_DBF_TEXT(trace, 6, "fillbfnp");
		/* set state to PRIMED -> will be flushed */
		if (buf->next_element_to_fill > 0){
			atomic_set(&buf->state, QETH_QDIO_BUF_PRIMED);
			flush_cnt++;
		}
	} else {
#ifdef CONFIG_QETH_PERF_STATS
		queue->card->perf_stats.skbs_sent_pack++;
#endif
		QETH_DBF_TEXT(trace, 6, "fillbfpa");
		if (buf->next_element_to_fill >=
				QETH_MAX_BUFFER_ELEMENTS(queue->card)) {
			/*
			 * packed buffer if full -> set state PRIMED
			 * -> will be flushed
			 */
			atomic_set(&buf->state, QETH_QDIO_BUF_PRIMED);
			flush_cnt++;
		}
	}
out:
	return flush_cnt;
}

static inline void
qeth_eddp_create_segment_hdrs(struct qeth_eddp_context *ctx,
			      struct qeth_eddp_data *eddp, int data_len)
{
	u8 *page;
	int page_remainder;
	int page_offset;
	int pkt_len;
	struct qeth_eddp_element *element;

	QETH_DBF_TEXT(trace, 5, "eddpcrsh");
	page = ctx->pages[ctx->offset >> PAGE_SHIFT];
	page_offset = ctx->offset % PAGE_SIZE;
	element = &ctx->elements[ctx->num_elements];
	pkt_len = eddp->nhl + eddp->thl + data_len;
	/* FIXME: layer2 and VLAN !!! */
	if (eddp->qh.hdr.l2.id == QETH_HEADER_TYPE_LAYER2)
		pkt_len += ETH_HLEN;
	if (eddp->mac.h_proto == __constant_htons(ETH_P_8021Q))
		pkt_len += VLAN_HLEN;
	/* does complete packet fit in current page ? */
	page_remainder = PAGE_SIZE - page_offset;
	if (page_remainder < (sizeof(struct qeth_hdr) + pkt_len)){
		/* no -> go to start of next page */
		ctx->offset += page_remainder;
		page = ctx->pages[ctx->offset >> PAGE_SHIFT];
		page_offset = 0;
	}
	memcpy(page + page_offset, &eddp->qh, sizeof(struct qeth_hdr));
	element->addr = page + page_offset;
	element->length = sizeof(struct qeth_hdr);
	ctx->offset += sizeof(struct qeth_hdr);
	page_offset += sizeof(struct qeth_hdr);
	/* add mac header (?) */
	if (eddp->qh.hdr.l2.id == QETH_HEADER_TYPE_LAYER2){
		memcpy(page + page_offset, &eddp->mac, ETH_HLEN);
		element->length += ETH_HLEN;
		ctx->offset += ETH_HLEN;
		page_offset += ETH_HLEN;
	}
	/* add VLAN tag */
	if (eddp->mac.h_proto == __constant_htons(ETH_P_8021Q)){
		memcpy(page + page_offset, &eddp->vlan, VLAN_HLEN);
		element->length += VLAN_HLEN;
		ctx->offset += VLAN_HLEN;
		page_offset += VLAN_HLEN;
	}
	/* add network header */
	memcpy(page + page_offset, (u8 *)&eddp->nh, eddp->nhl);
	element->length += eddp->nhl;
	eddp->nh_in_ctx = page + page_offset;
	ctx->offset += eddp->nhl;
	page_offset += eddp->nhl;
	/* add transport header */
	memcpy(page + page_offset, (u8 *)&eddp->th, eddp->thl);
	element->length += eddp->thl;
	eddp->th_in_ctx = page + page_offset;
	ctx->offset += eddp->thl;
}

static inline void
qeth_eddp_copy_data_tcp(char *dst, struct qeth_eddp_data *eddp, int len,
			u32 *hcsum)
{
	struct skb_frag_struct *frag;
	int left_in_frag;
	int copy_len;
	u8 *src;
	
	QETH_DBF_TEXT(trace, 5, "eddpcdtc");
	if (skb_shinfo(eddp->skb)->nr_frags == 0) {
		memcpy(dst, eddp->skb->data + eddp->skb_offset, len);
		*hcsum = csum_partial(eddp->skb->data + eddp->skb_offset, len,
				      *hcsum);
		eddp->skb_offset += len;
	} else {
		while (len > 0) {
			if (eddp->frag < 0) {
				/* we're in skb->data */
				left_in_frag = (eddp->skb->len - eddp->skb->data_len)
						- eddp->skb_offset;
				src = eddp->skb->data + eddp->skb_offset;
			} else {
				frag = &skb_shinfo(eddp->skb)->
					frags[eddp->frag];
				left_in_frag = frag->size - eddp->frag_offset;
				src = (u8 *)(
					(page_to_pfn(frag->page) << PAGE_SHIFT)+
					frag->page_offset + eddp->frag_offset);
			}
			if (left_in_frag <= 0) {
				eddp->frag++;
				eddp->frag_offset = 0;
				continue;
			}
			copy_len = min(left_in_frag, len);
			memcpy(dst, src, copy_len);
			*hcsum = csum_partial(src, copy_len, *hcsum);
			dst += copy_len;
			eddp->frag_offset += copy_len;
			eddp->skb_offset += copy_len;
			len -= copy_len;
		}
	}
}

static inline void
qeth_eddp_create_segment_data_tcp(struct qeth_eddp_context *ctx,
				  struct qeth_eddp_data *eddp, int data_len,
				  u32 hcsum)
{
	u8 *page;
	int page_remainder;
	int page_offset;
	struct qeth_eddp_element *element;
	int first_lap = 1;

	QETH_DBF_TEXT(trace, 5, "eddpcsdt");
	page = ctx->pages[ctx->offset >> PAGE_SHIFT];
	page_offset = ctx->offset % PAGE_SIZE;
	element = &ctx->elements[ctx->num_elements];
	while (data_len){
		page_remainder = PAGE_SIZE - page_offset;
		if (page_remainder < data_len){
			qeth_eddp_copy_data_tcp(page + page_offset, eddp,
						page_remainder, &hcsum);
			element->length += page_remainder;
			if (first_lap)
				element->flags = SBAL_FLAGS_FIRST_FRAG;
			else
				element->flags = SBAL_FLAGS_MIDDLE_FRAG;
			ctx->num_elements++;
			element++;
			data_len -= page_remainder;
			ctx->offset += page_remainder;
			page = ctx->pages[ctx->offset >> PAGE_SHIFT];
			page_offset = 0;
			element->addr = page + page_offset;
		} else {
			qeth_eddp_copy_data_tcp(page + page_offset, eddp,
						data_len, &hcsum);
			element->length += data_len;
			if (!first_lap)
				element->flags = SBAL_FLAGS_LAST_FRAG;
			ctx->num_elements++;
			ctx->offset += data_len;
			data_len = 0;
		}
		first_lap = 0;
	}
	((struct tcphdr *)eddp->th_in_ctx)->check = csum_fold(hcsum);
}

static inline u32
qeth_eddp_check_tcp4_hdr(struct qeth_eddp_data *eddp, int data_len)
{
	u32 phcsum; /* pseudo header checksum */

	QETH_DBF_TEXT(trace, 5, "eddpckt4");
	eddp->th.tcp.h.check = 0;
	/* compute pseudo header checksum */
	phcsum = csum_tcpudp_nofold(eddp->nh.ip4.h.saddr, eddp->nh.ip4.h.daddr,
				    eddp->thl + data_len, IPPROTO_TCP, 0);
	/* compute checksum of tcp header */
	return csum_partial((u8 *)&eddp->th, eddp->thl, phcsum);
}

static inline u32
qeth_eddp_check_tcp6_hdr(struct qeth_eddp_data *eddp, int data_len)
{
	u32 proto;
	u32 phcsum; /* pseudo header checksum */

	QETH_DBF_TEXT(trace, 5, "eddpckt6");
	eddp->th.tcp.h.check = 0;
	/* compute pseudo header checksum */
	phcsum = csum_partial((u8 *)&eddp->nh.ip6.h.saddr,
			      sizeof(struct in6_addr), 0);
	phcsum = csum_partial((u8 *)&eddp->nh.ip6.h.daddr,
			      sizeof(struct in6_addr), phcsum);
	proto = htonl(IPPROTO_TCP);
	phcsum = csum_partial((u8 *)&proto, sizeof(u32), phcsum);
	return phcsum;
}

static inline struct qeth_eddp_data *
qeth_eddp_create_eddp_data(struct qeth_hdr *qh, u8 *nh, u8 nhl, u8 *th, u8 thl)
{
	struct qeth_eddp_data *eddp;

	QETH_DBF_TEXT(trace, 5, "eddpcrda");
	eddp = kmalloc(sizeof(struct qeth_eddp_data), GFP_ATOMIC);
	if (eddp){
		memset(eddp, 0, sizeof(struct qeth_eddp_data));
		eddp->nhl = nhl;
		eddp->thl = thl;
		memcpy(&eddp->qh, qh, sizeof(struct qeth_hdr));
		memcpy(&eddp->nh, nh, nhl);
		memcpy(&eddp->th, th, thl);
		eddp->frag = -1; /* initially we're in skb->data */
	}
	return eddp;
}

static inline void
__qeth_eddp_fill_context_tcp(struct qeth_eddp_context *ctx,
			     struct qeth_eddp_data *eddp)
{
	struct tcphdr *tcph;
	int data_len;
	u32 hcsum;
	
	QETH_DBF_TEXT(trace, 5, "eddpftcp");
	eddp->skb_offset = sizeof(struct qeth_hdr) + eddp->nhl + eddp->thl;
	tcph = eddp->skb->h.th;
	while (eddp->skb_offset < eddp->skb->len) {
		data_len = min((int)skb_shinfo(eddp->skb)->tso_size,
			       (int)(eddp->skb->len - eddp->skb_offset));
		/* prepare qdio hdr */
		if (eddp->qh.hdr.l2.id == QETH_HEADER_TYPE_LAYER2){
			eddp->qh.hdr.l2.pkt_length = data_len + ETH_HLEN +
						     eddp->nhl + eddp->thl -
						     sizeof(struct qeth_hdr);
#ifdef CONFIG_QETH_VLAN
			if (eddp->mac.h_proto == __constant_htons(ETH_P_8021Q))
				eddp->qh.hdr.l2.pkt_length += VLAN_HLEN;
#endif /* CONFIG_QETH_VLAN */
		} else
			eddp->qh.hdr.l3.length = data_len + eddp->nhl +
						 eddp->thl;
		/* prepare ip hdr */
		if (eddp->skb->protocol == ETH_P_IP){
			eddp->nh.ip4.h.tot_len = data_len + eddp->nhl +
						 eddp->thl;
			eddp->nh.ip4.h.check = 0;
			eddp->nh.ip4.h.check =
				ip_fast_csum((u8 *)&eddp->nh.ip4.h,
						eddp->nh.ip4.h.ihl);
		} else
			eddp->nh.ip6.h.payload_len = data_len + eddp->thl;
		/* prepare tcp hdr */
		if (data_len == (eddp->skb->len - eddp->skb_offset)){
			/* last segment -> set FIN and PSH flags */
			eddp->th.tcp.h.fin = tcph->fin;
			eddp->th.tcp.h.psh = tcph->psh;
		}
		if (eddp->skb->protocol == ETH_P_IP)
			hcsum = qeth_eddp_check_tcp4_hdr(eddp, data_len);
		else
			hcsum = qeth_eddp_check_tcp6_hdr(eddp, data_len);
		/* fill the next segment into the context */
		qeth_eddp_create_segment_hdrs(ctx, eddp, data_len);
		qeth_eddp_create_segment_data_tcp(ctx, eddp, data_len, hcsum);
		if (eddp->skb_offset >= eddp->skb->len)
			break;
		/* prepare headers for next round */
		if (eddp->skb->protocol == ETH_P_IP)
			eddp->nh.ip4.h.id++;
		eddp->th.tcp.h.seq += data_len;
	}
}
			   
static inline int
qeth_eddp_fill_context_tcp(struct qeth_eddp_context *ctx,
			   struct sk_buff *skb, struct qeth_hdr *qhdr)
{
	struct qeth_eddp_data *eddp = NULL;
	
	QETH_DBF_TEXT(trace, 5, "eddpficx");
	/* create our segmentation headers and copy original headers */
	if (skb->protocol == ETH_P_IP)
		eddp = qeth_eddp_create_eddp_data(qhdr, (u8 *)skb->nh.iph,
				skb->nh.iph->ihl*4,
				(u8 *)skb->h.th, skb->h.th->doff*4);
	else
		eddp = qeth_eddp_create_eddp_data(qhdr, (u8 *)skb->nh.ipv6h,
				sizeof(struct ipv6hdr),
				(u8 *)skb->h.th, skb->h.th->doff*4);

	if (eddp == NULL) {
		QETH_DBF_TEXT(trace, 2, "eddpfcnm");
		return -ENOMEM;
	}
	if (qhdr->hdr.l2.id == QETH_HEADER_TYPE_LAYER2) {
		memcpy(&eddp->mac, eth_hdr(skb), ETH_HLEN);
#ifdef CONFIG_QETH_VLAN
		if (eddp->mac.h_proto == __constant_htons(ETH_P_8021Q)) {
			eddp->vlan[0] = __constant_htons(skb->protocol);
			eddp->vlan[1] = htons(vlan_tx_tag_get(skb));
		}
#endif /* CONFIG_QETH_VLAN */
	}
	/* the next flags will only be set on the last segment */
	eddp->th.tcp.h.fin = 0;
	eddp->th.tcp.h.psh = 0;
	eddp->skb = skb;
	/* begin segmentation and fill context */
	__qeth_eddp_fill_context_tcp(ctx, eddp);
	kfree(eddp);
	return 0;
}

static inline void
qeth_eddp_calc_num_pages(struct qeth_eddp_context *ctx, struct sk_buff *skb,
			 int hdr_len)
{
	int skbs_per_page;
	
	QETH_DBF_TEXT(trace, 5, "eddpcanp");
	/* can we put multiple skbs in one page? */
	skbs_per_page = PAGE_SIZE / (skb_shinfo(skb)->tso_size + hdr_len);
	if (skbs_per_page > 1){
		ctx->num_pages = (skb_shinfo(skb)->tso_segs + 1) /
				 skbs_per_page + 1;
		ctx->elements_per_skb = 1;
	} else {
		/* no -> how many elements per skb? */
		ctx->elements_per_skb = (skb_shinfo(skb)->tso_size + hdr_len +
				     PAGE_SIZE) >> PAGE_SHIFT;
		ctx->num_pages = ctx->elements_per_skb *
				 (skb_shinfo(skb)->tso_segs + 1);
	}
	ctx->num_elements = ctx->elements_per_skb *
			    (skb_shinfo(skb)->tso_segs + 1);
}

static inline struct qeth_eddp_context *
qeth_eddp_create_context_generic(struct qeth_card *card, struct sk_buff *skb,
				 int hdr_len)
{
	struct qeth_eddp_context *ctx = NULL;
	u8 *addr;
	int i;

	QETH_DBF_TEXT(trace, 5, "creddpcg");
	/* create the context and allocate pages */
	ctx = kmalloc(sizeof(struct qeth_eddp_context), GFP_ATOMIC);
	if (ctx == NULL){
		QETH_DBF_TEXT(trace, 2, "ceddpcn1");
		return NULL;
	}
	memset(ctx, 0, sizeof(struct qeth_eddp_context));
	ctx->type = QETH_LARGE_SEND_EDDP;
	qeth_eddp_calc_num_pages(ctx, skb, hdr_len);
	if (ctx->elements_per_skb > QETH_MAX_BUFFER_ELEMENTS(card)){
		QETH_DBF_TEXT(trace, 2, "ceddpcis");
		kfree(ctx);
		return NULL;
	}
	ctx->pages = kmalloc(ctx->num_pages * sizeof(u8 *), GFP_ATOMIC);
	if (ctx->pages == NULL){
		QETH_DBF_TEXT(trace, 2, "ceddpcn2");
		kfree(ctx);
		return NULL;
	}
	memset(ctx->pages, 0, ctx->num_pages * sizeof(u8 *));
	for (i = 0; i < ctx->num_pages; ++i){
		addr = (u8 *)__get_free_page(GFP_ATOMIC);
		if (addr == NULL){
			QETH_DBF_TEXT(trace, 2, "ceddpcn3");
			ctx->num_pages = i;
			qeth_eddp_free_context(ctx);
			return NULL;
		}
		memset(addr, 0, PAGE_SIZE);
		ctx->pages[i] = addr;
	}
	ctx->elements = kmalloc(ctx->num_elements *
				sizeof(struct qeth_eddp_element), GFP_ATOMIC);
	if (ctx->elements == NULL){
		QETH_DBF_TEXT(trace, 2, "ceddpcn4");
		qeth_eddp_free_context(ctx);
		return NULL;
	}
	memset(ctx->elements, 0,
	       ctx->num_elements * sizeof(struct qeth_eddp_element));
	/* reset num_elements; will be incremented again in fill_buffer to
	 * reflect number of actually used elements */
	ctx->num_elements = 0;
	return ctx;
}

static inline struct qeth_eddp_context *
qeth_eddp_create_context_tcp(struct qeth_card *card, struct sk_buff *skb,
			     struct qeth_hdr *qhdr)
{
	struct qeth_eddp_context *ctx = NULL;
	
	QETH_DBF_TEXT(trace, 5, "creddpct");
	if (skb->protocol == ETH_P_IP)
		ctx = qeth_eddp_create_context_generic(card, skb,
			sizeof(struct qeth_hdr) + skb->nh.iph->ihl*4 +
			skb->h.th->doff*4);
	else if (skb->protocol == ETH_P_IPV6)
		ctx = qeth_eddp_create_context_generic(card, skb,
			sizeof(struct qeth_hdr) + sizeof(struct ipv6hdr) +
			skb->h.th->doff*4);
	else
		QETH_DBF_TEXT(trace, 2, "cetcpinv");

	if (ctx == NULL) {
		QETH_DBF_TEXT(trace, 2, "creddpnl");
		return NULL;
	}
	if (qeth_eddp_fill_context_tcp(ctx, skb, qhdr)){
		QETH_DBF_TEXT(trace, 2, "ceddptfe");
		qeth_eddp_free_context(ctx);
		return NULL;
	}
	atomic_set(&ctx->refcnt, 1);
	return ctx;
}

struct qeth_eddp_context *
qeth_eddp_create_context(struct qeth_card *card, struct sk_buff *skb,
			 struct qeth_hdr *qhdr)
{
	QETH_DBF_TEXT(trace, 5, "creddpc");
	switch (skb->sk->sk_protocol){
	case IPPROTO_TCP:
		return qeth_eddp_create_context_tcp(card, skb, qhdr);
	default:
		QETH_DBF_TEXT(trace, 2, "eddpinvp");
	}
	return NULL;
}



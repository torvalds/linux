/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/netdevice.h>
#include <linux/module.h>
#include <brcmu_utils.h>

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("Broadcom 802.11n wireless LAN driver utilities.");
MODULE_SUPPORTED_DEVICE("Broadcom 802.11n WLAN cards");
MODULE_LICENSE("Dual BSD/GPL");

struct sk_buff *brcmu_pkt_buf_get_skb(uint len)
{
	struct sk_buff *skb;

	skb = dev_alloc_skb(len);
	if (skb) {
		skb_put(skb, len);
		skb->priority = 0;
	}

	return skb;
}
EXPORT_SYMBOL(brcmu_pkt_buf_get_skb);

/* Free the driver packet. Free the tag if present */
void brcmu_pkt_buf_free_skb(struct sk_buff *skb)
{
	struct sk_buff *nskb;
	int nest = 0;

	/* perversion: we use skb->next to chain multi-skb packets */
	while (skb) {
		nskb = skb->next;
		skb->next = NULL;

		if (skb->destructor)
			/* cannot kfree_skb() on hard IRQ (net/core/skbuff.c) if
			 * destructor exists
			 */
			dev_kfree_skb_any(skb);
		else
			/* can free immediately (even in_irq()) if destructor
			 * does not exist
			 */
			dev_kfree_skb(skb);

		nest++;
		skb = nskb;
	}
}
EXPORT_SYMBOL(brcmu_pkt_buf_free_skb);


/* copy a buffer into a pkt buffer chain */
uint brcmu_pktfrombuf(struct sk_buff *p, uint offset, int len,
		unsigned char *buf)
{
	uint n, ret = 0;

	/* skip 'offset' bytes */
	for (; p && offset; p = p->next) {
		if (offset < (uint) (p->len))
			break;
		offset -= p->len;
	}

	if (!p)
		return 0;

	/* copy the data */
	for (; p && len; p = p->next) {
		n = min((uint) (p->len) - offset, (uint) len);
		memcpy(p->data + offset, buf, n);
		buf += n;
		len -= n;
		ret += n;
		offset = 0;
	}

	return ret;
}
EXPORT_SYMBOL(brcmu_pktfrombuf);

/* return total length of buffer chain */
uint brcmu_pkttotlen(struct sk_buff *p)
{
	uint total;

	total = 0;
	for (; p; p = p->next)
		total += p->len;
	return total;
}
EXPORT_SYMBOL(brcmu_pkttotlen);

/*
 * osl multiple-precedence packet queue
 * hi_prec is always >= the number of the highest non-empty precedence
 */
struct sk_buff *brcmu_pktq_penq(struct pktq *pq, int prec,
				      struct sk_buff *p)
{
	struct pktq_prec *q;

	if (pktq_full(pq) || pktq_pfull(pq, prec))
		return NULL;

	q = &pq->q[prec];

	if (q->head)
		q->tail->prev = p;
	else
		q->head = p;

	q->tail = p;
	q->len++;

	pq->len++;

	if (pq->hi_prec < prec)
		pq->hi_prec = (u8) prec;

	return p;
}
EXPORT_SYMBOL(brcmu_pktq_penq);

struct sk_buff *brcmu_pktq_penq_head(struct pktq *pq, int prec,
					   struct sk_buff *p)
{
	struct pktq_prec *q;

	if (pktq_full(pq) || pktq_pfull(pq, prec))
		return NULL;

	q = &pq->q[prec];

	if (q->head == NULL)
		q->tail = p;

	p->prev = q->head;
	q->head = p;
	q->len++;

	pq->len++;

	if (pq->hi_prec < prec)
		pq->hi_prec = (u8) prec;

	return p;
}
EXPORT_SYMBOL(brcmu_pktq_penq_head);

struct sk_buff *brcmu_pktq_pdeq(struct pktq *pq, int prec)
{
	struct pktq_prec *q;
	struct sk_buff *p;

	q = &pq->q[prec];

	p = q->head;
	if (p == NULL)
		return NULL;

	q->head = p->prev;
	if (q->head == NULL)
		q->tail = NULL;

	q->len--;

	pq->len--;

	p->prev = NULL;

	return p;
}
EXPORT_SYMBOL(brcmu_pktq_pdeq);

struct sk_buff *brcmu_pktq_pdeq_tail(struct pktq *pq, int prec)
{
	struct pktq_prec *q;
	struct sk_buff *p, *prev;

	q = &pq->q[prec];

	p = q->head;
	if (p == NULL)
		return NULL;

	for (prev = NULL; p != q->tail; p = p->prev)
		prev = p;

	if (prev)
		prev->prev = NULL;
	else
		q->head = NULL;

	q->tail = prev;
	q->len--;

	pq->len--;

	return p;
}
EXPORT_SYMBOL(brcmu_pktq_pdeq_tail);

void
brcmu_pktq_pflush(struct pktq *pq, int prec, bool dir,
		  bool (*fn)(struct sk_buff *, void *), void *arg)
{
	struct pktq_prec *q;
	struct sk_buff *p, *prev = NULL;

	q = &pq->q[prec];
	p = q->head;
	while (p) {
		if (fn == NULL || (*fn) (p, arg)) {
			bool head = (p == q->head);
			if (head)
				q->head = p->prev;
			else
				prev->prev = p->prev;
			p->prev = NULL;
			brcmu_pkt_buf_free_skb(p);
			q->len--;
			pq->len--;
			p = (head ? q->head : prev->prev);
		} else {
			prev = p;
			p = p->prev;
		}
	}

	if (q->head == NULL)
		q->tail = NULL;
}
EXPORT_SYMBOL(brcmu_pktq_pflush);

void brcmu_pktq_flush(struct pktq *pq, bool dir,
		      bool (*fn)(struct sk_buff *, void *), void *arg)
{
	int prec;
	for (prec = 0; prec < pq->num_prec; prec++)
		brcmu_pktq_pflush(pq, prec, dir, fn, arg);
}
EXPORT_SYMBOL(brcmu_pktq_flush);

void brcmu_pktq_init(struct pktq *pq, int num_prec, int max_len)
{
	int prec;

	/* pq is variable size; only zero out what's requested */
	memset(pq, 0,
	      offsetof(struct pktq, q) + (sizeof(struct pktq_prec) * num_prec));

	pq->num_prec = (u16) num_prec;

	pq->max = (u16) max_len;

	for (prec = 0; prec < num_prec; prec++)
		pq->q[prec].max = pq->max;
}
EXPORT_SYMBOL(brcmu_pktq_init);

struct sk_buff *brcmu_pktq_peek_tail(struct pktq *pq, int *prec_out)
{
	int prec;

	if (pq->len == 0)
		return NULL;

	for (prec = 0; prec < pq->hi_prec; prec++)
		if (pq->q[prec].head)
			break;

	if (prec_out)
		*prec_out = prec;

	return pq->q[prec].tail;
}
EXPORT_SYMBOL(brcmu_pktq_peek_tail);

/* Return sum of lengths of a specific set of precedences */
int brcmu_pktq_mlen(struct pktq *pq, uint prec_bmp)
{
	int prec, len;

	len = 0;

	for (prec = 0; prec <= pq->hi_prec; prec++)
		if (prec_bmp & (1 << prec))
			len += pq->q[prec].len;

	return len;
}
EXPORT_SYMBOL(brcmu_pktq_mlen);

/* Priority dequeue from a specific set of precedences */
struct sk_buff *brcmu_pktq_mdeq(struct pktq *pq, uint prec_bmp,
				      int *prec_out)
{
	struct pktq_prec *q;
	struct sk_buff *p;
	int prec;

	if (pq->len == 0)
		return NULL;

	while ((prec = pq->hi_prec) > 0 && pq->q[prec].head == NULL)
		pq->hi_prec--;

	while ((prec_bmp & (1 << prec)) == 0 || pq->q[prec].head == NULL)
		if (prec-- == 0)
			return NULL;

	q = &pq->q[prec];

	p = q->head;
	if (p == NULL)
		return NULL;

	q->head = p->prev;
	if (q->head == NULL)
		q->tail = NULL;

	q->len--;

	if (prec_out)
		*prec_out = prec;

	pq->len--;

	p->prev = NULL;

	return p;
}
EXPORT_SYMBOL(brcmu_pktq_mdeq);

#if defined(BCMDBG)
/* pretty hex print a pkt buffer chain */
void brcmu_prpkt(const char *msg, struct sk_buff *p0)
{
	struct sk_buff *p;

	if (msg && (msg[0] != '\0'))
		printk(KERN_DEBUG "%s:\n", msg);

	for (p = p0; p; p = p->next)
		print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, p->data, p->len);
}
EXPORT_SYMBOL(brcmu_prpkt);
#endif				/* defined(BCMDBG) */

#if defined(BCMDBG)
/*
 * print bytes formatted as hex to a string. return the resulting
 * string length
 */
int brcmu_format_hex(char *str, const void *bytes, int len)
{
	int i;
	char *p = str;
	const u8 *src = (const u8 *)bytes;

	for (i = 0; i < len; i++) {
		p += snprintf(p, 3, "%02X", *src);
		src++;
	}
	return (int)(p - str);
}
EXPORT_SYMBOL(brcmu_format_hex);
#endif				/* defined(BCMDBG) */

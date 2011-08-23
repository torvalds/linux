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

/* iovar table lookup */
const struct brcmu_iovar *brcmu_iovar_lookup(const struct brcmu_iovar *table,
					const char *name)
{
	const struct brcmu_iovar *vi;
	const char *lookup_name;

	/* skip any ':' delimited option prefixes */
	lookup_name = strrchr(name, ':');
	if (lookup_name != NULL)
		lookup_name++;
	else
		lookup_name = name;

	for (vi = table; vi->name; vi++) {
		if (!strcmp(vi->name, lookup_name))
			return vi;
	}
	/* ran to end of table */

	return NULL;		/* var name not found */
}
EXPORT_SYMBOL(brcmu_iovar_lookup);

int brcmu_iovar_lencheck(const struct brcmu_iovar *vi, void *arg, int len,
			 bool set)
{
	int bcmerror = 0;

	/* length check on io buf */
	switch (vi->type) {
	case IOVT_BOOL:
	case IOVT_INT8:
	case IOVT_INT16:
	case IOVT_INT32:
	case IOVT_UINT8:
	case IOVT_UINT16:
	case IOVT_UINT32:
		/* all integers are s32 sized args at the ioctl interface */
		if (len < (int)sizeof(int))
			bcmerror = -EOVERFLOW;
		break;

	case IOVT_BUFFER:
		/* buffer must meet minimum length requirement */
		if (len < vi->minlen)
			bcmerror = -EOVERFLOW;
		break;

	case IOVT_VOID:
		if (!set)
			/* Cannot return nil... */
			bcmerror = -ENOTSUPP;
		else if (len)
			/* Set is an action w/o parameters */
			bcmerror = -ENOBUFS;
		break;

	default:
		/* unknown type for length check in iovar info */
		bcmerror = -ENOTSUPP;
	}

	return bcmerror;
}
EXPORT_SYMBOL(brcmu_iovar_lencheck);

/*
 * Traverse a string of 1-byte tag/1-byte length/variable-length value
 * triples, returning a pointer to the substring whose first element
 * matches tag
 */
struct brcmu_tlv *brcmu_parse_tlvs(void *buf, int buflen, uint key)
{
	struct brcmu_tlv *elt;
	int totlen;

	elt = (struct brcmu_tlv *) buf;
	totlen = buflen;

	/* find tagged parameter */
	while (totlen >= 2) {
		int len = elt->len;

		/* validate remaining totlen */
		if ((elt->id == key) && (totlen >= (len + 2)))
			return elt;

		elt = (struct brcmu_tlv *) ((u8 *) elt + (len + 2));
		totlen -= (len + 2);
	}

	return NULL;
}
EXPORT_SYMBOL(brcmu_parse_tlvs);


#if defined(BCMDBG)
int
brcmu_format_flags(const struct brcmu_bit_desc *bd, u32 flags, char *buf,
		   int len)
{
	int i;
	char *p = buf;
	char hexstr[16];
	int slen = 0, nlen = 0;
	u32 bit;
	const char *name;

	if (len < 2 || !buf)
		return 0;

	buf[0] = '\0';

	for (i = 0; flags != 0; i++) {
		bit = bd[i].bit;
		name = bd[i].name;
		if (bit == 0 && flags != 0) {
			/* print any unnamed bits */
			snprintf(hexstr, 16, "0x%X", flags);
			name = hexstr;
			flags = 0;	/* exit loop */
		} else if ((flags & bit) == 0)
			continue;
		flags &= ~bit;
		nlen = strlen(name);
		slen += nlen;
		/* count btwn flag space */
		if (flags != 0)
			slen += 1;
		/* need NULL char as well */
		if (len <= slen)
			break;
		/* copy NULL char but don't count it */
		strncpy(p, name, nlen + 1);
		p += nlen;
		/* copy btwn flag space and NULL char */
		if (flags != 0)
			p += snprintf(p, 2, " ");
		len -= slen;
	}

	/* indicate the str was too short */
	if (flags != 0) {
		if (len < 2)
			p -= 2 - len;	/* overwrite last char */
		p += snprintf(p, 2, ">");
	}

	return (int)(p - buf);
}
EXPORT_SYMBOL(brcmu_format_flags);

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

char *brcmu_chipname(uint chipid, char *buf, uint len)
{
	const char *fmt;

	fmt = ((chipid > 0xa000) || (chipid < 0x4000)) ? "%d" : "%x";
	snprintf(buf, len, fmt, chipid);
	return buf;
}
EXPORT_SYMBOL(brcmu_chipname);

uint brcmu_mkiovar(char *name, char *data, uint datalen, char *buf, uint buflen)
{
	uint len;

	len = strlen(name) + 1;

	if ((len + datalen) > buflen)
		return 0;

	strncpy(buf, name, buflen);

	/* append data onto the end of the name string */
	memcpy(&buf[len], data, datalen);
	len += datalen;

	return len;
}
EXPORT_SYMBOL(brcmu_mkiovar);

/* Quarter dBm units to mW
 * Table starts at QDBM_OFFSET, so the first entry is mW for qdBm=153
 * Table is offset so the last entry is largest mW value that fits in
 * a u16.
 */

#define QDBM_OFFSET 153		/* Offset for first entry */
#define QDBM_TABLE_LEN 40	/* Table size */

/* Smallest mW value that will round up to the first table entry, QDBM_OFFSET.
 * Value is ( mW(QDBM_OFFSET - 1) + mW(QDBM_OFFSET) ) / 2
 */
#define QDBM_TABLE_LOW_BOUND 6493	/* Low bound */

/* Largest mW value that will round down to the last table entry,
 * QDBM_OFFSET + QDBM_TABLE_LEN-1.
 * Value is ( mW(QDBM_OFFSET + QDBM_TABLE_LEN - 1) +
 * mW(QDBM_OFFSET + QDBM_TABLE_LEN) ) / 2.
 */
#define QDBM_TABLE_HIGH_BOUND 64938	/* High bound */

static const u16 nqdBm_to_mW_map[QDBM_TABLE_LEN] = {
/* qdBm:	+0	+1	+2	+3	+4	+5	+6	+7 */
/* 153: */ 6683, 7079, 7499, 7943, 8414, 8913, 9441, 10000,
/* 161: */ 10593, 11220, 11885, 12589, 13335, 14125, 14962, 15849,
/* 169: */ 16788, 17783, 18836, 19953, 21135, 22387, 23714, 25119,
/* 177: */ 26607, 28184, 29854, 31623, 33497, 35481, 37584, 39811,
/* 185: */ 42170, 44668, 47315, 50119, 53088, 56234, 59566, 63096
};

u16 brcmu_qdbm_to_mw(u8 qdbm)
{
	uint factor = 1;
	int idx = qdbm - QDBM_OFFSET;

	if (idx >= QDBM_TABLE_LEN)
		/* clamp to max u16 mW value */
		return 0xFFFF;

	/* scale the qdBm index up to the range of the table 0-40
	 * where an offset of 40 qdBm equals a factor of 10 mW.
	 */
	while (idx < 0) {
		idx += 40;
		factor *= 10;
	}

	/* return the mW value scaled down to the correct factor of 10,
	 * adding in factor/2 to get proper rounding.
	 */
	return (nqdBm_to_mW_map[idx] + factor / 2) / factor;
}
EXPORT_SYMBOL(brcmu_qdbm_to_mw);

u8 brcmu_mw_to_qdbm(u16 mw)
{
	u8 qdbm;
	int offset;
	uint mw_uint = mw;
	uint boundary;

	/* handle boundary case */
	if (mw_uint <= 1)
		return 0;

	offset = QDBM_OFFSET;

	/* move mw into the range of the table */
	while (mw_uint < QDBM_TABLE_LOW_BOUND) {
		mw_uint *= 10;
		offset -= 40;
	}

	for (qdbm = 0; qdbm < QDBM_TABLE_LEN - 1; qdbm++) {
		boundary = nqdBm_to_mW_map[qdbm] + (nqdBm_to_mW_map[qdbm + 1] -
						    nqdBm_to_mW_map[qdbm]) / 2;
		if (mw_uint < boundary)
			break;
	}

	qdbm += (u8) offset;

	return qdbm;
}
EXPORT_SYMBOL(brcmu_mw_to_qdbm);

uint brcmu_bitcount(u8 *bitmap, uint length)
{
	uint bitcount = 0, i;
	u8 tmp;
	for (i = 0; i < length; i++) {
		tmp = bitmap[i];
		while (tmp) {
			bitcount++;
			tmp &= (tmp - 1);
		}
	}
	return bitcount;
}
EXPORT_SYMBOL(brcmu_bitcount);

/* Initialization of brcmu_strbuf structure */
void brcmu_binit(struct brcmu_strbuf *b, char *buf, uint size)
{
	b->origsize = b->size = size;
	b->origbuf = b->buf = buf;
}
EXPORT_SYMBOL(brcmu_binit);

/* Buffer sprintf wrapper to guard against buffer overflow */
int brcmu_bprintf(struct brcmu_strbuf *b, const char *fmt, ...)
{
	va_list ap;
	int r;

	va_start(ap, fmt);
	r = vsnprintf(b->buf, b->size, fmt, ap);

	/* Non Ansi C99 compliant returns -1,
	 * Ansi compliant return r >= b->size,
	 * stdlib returns 0, handle all
	 */
	if ((r == -1) || (r >= (int)b->size) || (r == 0)) {
		b->size = 0;
	} else {
		b->size -= r;
		b->buf += r;
	}

	va_end(ap);

	return r;
}
EXPORT_SYMBOL(brcmu_bprintf);

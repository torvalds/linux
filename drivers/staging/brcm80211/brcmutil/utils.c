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
	    ifpkt_cb_t fn, void *arg)
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

	if (q->head == NULL) {
		q->tail = NULL;
	}
}
EXPORT_SYMBOL(brcmu_pktq_pflush);

void brcmu_pktq_flush(struct pktq *pq, bool dir,
		ifpkt_cb_t fn, void *arg)
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

/* parse a xx:xx:xx:xx:xx:xx format ethernet address */
int brcmu_ether_atoe(char *p, u8 *ea)
{
	int i = 0;

	for (;;) {
		ea[i++] = (char)simple_strtoul(p, &p, 16);
		if (!*p++ || i == 6)
			break;
	}

	return i == 6;
}
EXPORT_SYMBOL(brcmu_ether_atoe);

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
		if (len < (int)sizeof(int)) {
			bcmerror = -EOVERFLOW;
		}
		break;

	case IOVT_BUFFER:
		/* buffer must meet minimum length requirement */
		if (len < vi->minlen) {
			bcmerror = -EOVERFLOW;
		}
		break;

	case IOVT_VOID:
		if (!set) {
			/* Cannot return nil... */
			bcmerror = -ENOTSUPP;
		} else if (len) {
			/* Set is an action w/o parameters */
			bcmerror = -ENOBUFS;
		}
		break;

	default:
		/* unknown type for length check in iovar info */
		bcmerror = -ENOTSUPP;
	}

	return bcmerror;
}
EXPORT_SYMBOL(brcmu_iovar_lencheck);

/*******************************************************************************
 * crc8
 *
 * Computes a crc8 over the input data using the polynomial:
 *
 *       x^8 + x^7 +x^6 + x^4 + x^2 + 1
 *
 * The caller provides the initial value (either CRC8_INIT_VALUE
 * or the previous returned value) to allow for processing of
 * discontiguous blocks of data.  When generating the CRC the
 * caller is responsible for complementing the final return value
 * and inserting it into the byte stream.  When checking, a final
 * return value of CRC8_GOOD_VALUE indicates a valid CRC.
 *
 * Reference: Dallas Semiconductor Application Note 27
 *   Williams, Ross N., "A Painless Guide to CRC Error Detection Algorithms",
 *     ver 3, Aug 1993, ross@guest.adelaide.edu.au, Rocksoft Pty Ltd.,
 *     ftp://ftp.rocksoft.com/clients/rocksoft/papers/crc_v3.txt
 *
 * ****************************************************************************
 */

static const u8 crc8_table[256] = {
	0x00, 0xF7, 0xB9, 0x4E, 0x25, 0xD2, 0x9C, 0x6B,
	0x4A, 0xBD, 0xF3, 0x04, 0x6F, 0x98, 0xD6, 0x21,
	0x94, 0x63, 0x2D, 0xDA, 0xB1, 0x46, 0x08, 0xFF,
	0xDE, 0x29, 0x67, 0x90, 0xFB, 0x0C, 0x42, 0xB5,
	0x7F, 0x88, 0xC6, 0x31, 0x5A, 0xAD, 0xE3, 0x14,
	0x35, 0xC2, 0x8C, 0x7B, 0x10, 0xE7, 0xA9, 0x5E,
	0xEB, 0x1C, 0x52, 0xA5, 0xCE, 0x39, 0x77, 0x80,
	0xA1, 0x56, 0x18, 0xEF, 0x84, 0x73, 0x3D, 0xCA,
	0xFE, 0x09, 0x47, 0xB0, 0xDB, 0x2C, 0x62, 0x95,
	0xB4, 0x43, 0x0D, 0xFA, 0x91, 0x66, 0x28, 0xDF,
	0x6A, 0x9D, 0xD3, 0x24, 0x4F, 0xB8, 0xF6, 0x01,
	0x20, 0xD7, 0x99, 0x6E, 0x05, 0xF2, 0xBC, 0x4B,
	0x81, 0x76, 0x38, 0xCF, 0xA4, 0x53, 0x1D, 0xEA,
	0xCB, 0x3C, 0x72, 0x85, 0xEE, 0x19, 0x57, 0xA0,
	0x15, 0xE2, 0xAC, 0x5B, 0x30, 0xC7, 0x89, 0x7E,
	0x5F, 0xA8, 0xE6, 0x11, 0x7A, 0x8D, 0xC3, 0x34,
	0xAB, 0x5C, 0x12, 0xE5, 0x8E, 0x79, 0x37, 0xC0,
	0xE1, 0x16, 0x58, 0xAF, 0xC4, 0x33, 0x7D, 0x8A,
	0x3F, 0xC8, 0x86, 0x71, 0x1A, 0xED, 0xA3, 0x54,
	0x75, 0x82, 0xCC, 0x3B, 0x50, 0xA7, 0xE9, 0x1E,
	0xD4, 0x23, 0x6D, 0x9A, 0xF1, 0x06, 0x48, 0xBF,
	0x9E, 0x69, 0x27, 0xD0, 0xBB, 0x4C, 0x02, 0xF5,
	0x40, 0xB7, 0xF9, 0x0E, 0x65, 0x92, 0xDC, 0x2B,
	0x0A, 0xFD, 0xB3, 0x44, 0x2F, 0xD8, 0x96, 0x61,
	0x55, 0xA2, 0xEC, 0x1B, 0x70, 0x87, 0xC9, 0x3E,
	0x1F, 0xE8, 0xA6, 0x51, 0x3A, 0xCD, 0x83, 0x74,
	0xC1, 0x36, 0x78, 0x8F, 0xE4, 0x13, 0x5D, 0xAA,
	0x8B, 0x7C, 0x32, 0xC5, 0xAE, 0x59, 0x17, 0xE0,
	0x2A, 0xDD, 0x93, 0x64, 0x0F, 0xF8, 0xB6, 0x41,
	0x60, 0x97, 0xD9, 0x2E, 0x45, 0xB2, 0xFC, 0x0B,
	0xBE, 0x49, 0x07, 0xF0, 0x9B, 0x6C, 0x22, 0xD5,
	0xF4, 0x03, 0x4D, 0xBA, 0xD1, 0x26, 0x68, 0x9F
};

u8 brcmu_crc8(u8 *pdata,	/* pointer to array of data to process */
			 uint nbytes,	/* number of input data bytes to process */
			 u8 crc	/* either CRC8_INIT_VALUE or previous return value */
	) {
	/* loop over the buffer data */
	while (nbytes-- > 0)
		crc = crc8_table[(crc ^ *pdata++) & 0xff];

	return crc;
}
EXPORT_SYMBOL(brcmu_crc8);

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

/* print bytes formatted as hex to a string. return the resulting string length */
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

	if (idx >= QDBM_TABLE_LEN) {
		/* clamp to max u16 mW value */
		return 0xFFFF;
	}

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

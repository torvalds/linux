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

#ifndef	_BRCMU_UTILS_H_
#define	_BRCMU_UTILS_H_

#include <linux/skbuff.h>

/* Buffer structure for collecting string-formatted data
* using brcmu_bprintf() API.
* Use brcmu_binit() to initialize before use
*/

struct brcmu_strbuf {
	char *buf;	/* pointer to current position in origbuf */
	unsigned int size;	/* current (residual) size in bytes */
	char *origbuf;	/* unmodified pointer to orignal buffer */
	unsigned int origsize;	/* unmodified orignal buffer size in bytes */
};

/*
 * Spin at most 'us' microseconds while 'exp' is true.
 * Caller should explicitly test 'exp' when this completes
 * and take appropriate error action if 'exp' is still true.
 */
#define SPINWAIT(exp, us) { \
	uint countdown = (us) + 9; \
	while ((exp) && (countdown >= 10)) {\
		udelay(10); \
		countdown -= 10; \
	} \
}

/* osl multi-precedence packet queue */
#define PKTQ_LEN_DEFAULT        128	/* Max 128 packets */
#define PKTQ_MAX_PREC           16	/* Maximum precedence levels */

#define BCME_STRLEN		64	/* Max string length for BCM errors */

/* the largest reasonable packet buffer driver uses for ethernet MTU in bytes */
#define	PKTBUFSZ	2048

#ifndef setbit
#ifndef NBBY			/* the BSD family defines NBBY */
#define	NBBY	8		/* 8 bits per byte */
#endif				/* #ifndef NBBY */
#define	setbit(a, i)	(((u8 *)a)[(i)/NBBY] |= 1<<((i)%NBBY))
#define	clrbit(a, i)	(((u8 *)a)[(i)/NBBY] &= ~(1<<((i)%NBBY)))
#define	isset(a, i)	(((const u8 *)a)[(i)/NBBY] & (1<<((i)%NBBY)))
#define	isclr(a, i)	((((const u8 *)a)[(i)/NBBY] & (1<<((i)%NBBY))) == 0)
#endif				/* setbit */

#define	NBITS(type)	(sizeof(type) * 8)
#define NBITVAL(nbits)	(1 << (nbits))
#define MAXBITVAL(nbits)	((1 << (nbits)) - 1)
#define	NBITMASK(nbits)	MAXBITVAL(nbits)
#define MAXNBVAL(nbyte)	MAXBITVAL((nbyte) * 8)

/* crc defines */
#define CRC16_INIT_VALUE 0xffff	/* Initial CRC16 checksum value */
#define CRC16_GOOD_VALUE 0xf0b8	/* Good final CRC16 checksum value */

/* 18-bytes of Ethernet address buffer length */
#define ETHER_ADDR_STR_LEN	18

struct pktq_prec {
	struct sk_buff *head;	/* first packet to dequeue */
	struct sk_buff *tail;	/* last packet to dequeue */
	u16 len;		/* number of queued packets */
	u16 max;		/* maximum number of queued packets */
};

/* multi-priority pkt queue */
struct pktq {
	u16 num_prec;	/* number of precedences in use */
	u16 hi_prec;	/* rapid dequeue hint (>= highest non-empty prec) */
	u16 max;	/* total max packets */
	u16 len;	/* total number of packets */
	/*
	 * q array must be last since # of elements can be either
	 * PKTQ_MAX_PREC or 1
	 */
	struct pktq_prec q[PKTQ_MAX_PREC];
};

/* operations on a specific precedence in packet queue */

static inline int pktq_plen(struct pktq *pq, int prec)
{
	return pq->q[prec].len;
}

static inline int pktq_pavail(struct pktq *pq, int prec)
{
	return pq->q[prec].max - pq->q[prec].len;
}

static inline bool pktq_pfull(struct pktq *pq, int prec)
{
	return pq->q[prec].len >= pq->q[prec].max;
}

static inline bool pktq_pempty(struct pktq *pq, int prec)
{
	return pq->q[prec].len == 0;
}

static inline struct sk_buff *pktq_ppeek(struct pktq *pq, int prec)
{
	return pq->q[prec].head;
}

static inline struct sk_buff *pktq_ppeek_tail(struct pktq *pq, int prec)
{
	return pq->q[prec].tail;
}

extern struct sk_buff *brcmu_pktq_penq(struct pktq *pq, int prec,
				 struct sk_buff *p);
extern struct sk_buff *brcmu_pktq_penq_head(struct pktq *pq, int prec,
				      struct sk_buff *p);
extern struct sk_buff *brcmu_pktq_pdeq(struct pktq *pq, int prec);
extern struct sk_buff *brcmu_pktq_pdeq_tail(struct pktq *pq, int prec);

/* packet primitives */
extern struct sk_buff *brcmu_pkt_buf_get_skb(uint len);
extern void brcmu_pkt_buf_free_skb(struct sk_buff *skb);

/* Empty the queue at particular precedence level */
/* callback function fn(pkt, arg) returns true if pkt belongs to if */
extern void brcmu_pktq_pflush(struct pktq *pq, int prec,
	bool dir, bool (*fn)(struct sk_buff *, void *), void *arg);

/* operations on a set of precedences in packet queue */

extern int brcmu_pktq_mlen(struct pktq *pq, uint prec_bmp);
extern struct sk_buff *brcmu_pktq_mdeq(struct pktq *pq, uint prec_bmp,
	int *prec_out);

/* operations on packet queue as a whole */

static inline int pktq_len(struct pktq *pq)
{
	return (int)pq->len;
}

static inline int pktq_max(struct pktq *pq)
{
	return (int)pq->max;
}

static inline int pktq_avail(struct pktq *pq)
{
	return (int)(pq->max - pq->len);
}

static inline bool pktq_full(struct pktq *pq)
{
	return pq->len >= pq->max;
}

static inline bool pktq_empty(struct pktq *pq)
{
	return pq->len == 0;
}

extern void brcmu_pktq_init(struct pktq *pq, int num_prec, int max_len);
/* prec_out may be NULL if caller is not interested in return value */
extern struct sk_buff *brcmu_pktq_peek_tail(struct pktq *pq, int *prec_out);
extern void brcmu_pktq_flush(struct pktq *pq, bool dir,
		bool (*fn)(struct sk_buff *, void *), void *arg);

/* externs */
/* packet */
extern uint brcmu_pktfrombuf(struct sk_buff *p,
	uint offset, int len, unsigned char *buf);
extern uint brcmu_pkttotlen(struct sk_buff *p);

/* ip address */
struct ipv4_addr;

#ifdef BCMDBG
extern void brcmu_prpkt(const char *msg, struct sk_buff *p0);
#else
#define brcmu_prpkt(a, b)
#endif				/* BCMDBG */

/* Support for sharing code across in-driver iovar implementations.
 * The intent is that a driver use this structure to map iovar names
 * to its (private) iovar identifiers, and the lookup function to
 * find the entry.  Macros are provided to map ids and get/set actions
 * into a single number space for a switch statement.
 */

/* iovar structure */
struct brcmu_iovar {
	const char *name;	/* name for lookup and display */
	u16 varid;	/* id for switch */
	u16 flags;	/* driver-specific flag bits */
	u16 type;	/* base type of argument */
	u16 minlen;	/* min length for buffer vars */
};

/* varid definitions are per-driver, may use these get/set bits */

/* IOVar action bits for id mapping */
#define IOV_GET 0		/* Get an iovar */
#define IOV_SET 1		/* Set an iovar */

/* Varid to actionid mapping */
#define IOV_GVAL(id)		((id)*2)
#define IOV_SVAL(id)		(((id)*2)+IOV_SET)
#define IOV_ISSET(actionid)	((actionid & IOV_SET) == IOV_SET)
#define IOV_ID(actionid)	(actionid >> 1)

extern const struct
brcmu_iovar *brcmu_iovar_lookup(const struct brcmu_iovar *table,
				const char *name);
extern int brcmu_iovar_lencheck(const struct brcmu_iovar *table, void *arg,
				int len, bool set);

/* Base type definitions */
#define IOVT_VOID	0	/* no value (implictly set only) */
#define IOVT_BOOL	1	/* any value ok (zero/nonzero) */
#define IOVT_INT8	2	/* integer values are range-checked */
#define IOVT_UINT8	3	/* unsigned int 8 bits */
#define IOVT_INT16	4	/* int 16 bits */
#define IOVT_UINT16	5	/* unsigned int 16 bits */
#define IOVT_INT32	6	/* int 32 bits */
#define IOVT_UINT32	7	/* unsigned int 32 bits */
#define IOVT_BUFFER	8	/* buffer is size-checked as per minlen */

/* brcmu_format_flags() bit description structure */
struct brcmu_bit_desc {
	u32 bit;
	const char *name;
};

/* tag_ID/length/value_buffer tuple */
struct brcmu_tlv {
	u8 id;
	u8 len;
	u8 data[1];
};

/* externs */
/* format/print */
#if defined(BCMDBG)
extern int brcmu_format_flags(const struct brcmu_bit_desc *bd, u32 flags,
			      char *buf, int len);
extern int brcmu_format_hex(char *str, const void *bytes, int len);
#endif

extern char *brcmu_chipname(uint chipid, char *buf, uint len);

extern struct brcmu_tlv *brcmu_parse_tlvs(void *buf, int buflen,
					  uint key);

/* power conversion */
extern u16 brcmu_qdbm_to_mw(u8 qdbm);
extern u8 brcmu_mw_to_qdbm(u16 mw);

extern void brcmu_binit(struct brcmu_strbuf *b, char *buf, uint size);
extern int brcmu_bprintf(struct brcmu_strbuf *b, const char *fmt, ...);

extern uint brcmu_mkiovar(char *name, char *data, uint datalen,
			  char *buf, uint len);
extern uint brcmu_bitcount(u8 *bitmap, uint bytelength);

#endif				/* _BRCMU_UTILS_H_ */

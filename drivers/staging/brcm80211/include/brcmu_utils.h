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

#ifndef	_brcmutils_h_
#define	_brcmutils_h_

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

/* ** driver-only section ** */

#define GPIO_PIN_NOTDEFINED 	0x20	/* Pin not defined */

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
#ifndef PKTQ_LEN_DEFAULT
#define PKTQ_LEN_DEFAULT        128	/* Max 128 packets */
#endif
#ifndef PKTQ_MAX_PREC
#define PKTQ_MAX_PREC           16	/* Maximum precedence levels */
#endif

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
		/* q array must be last since # of elements can be either PKTQ_MAX_PREC or 1 */
		struct pktq_prec q[PKTQ_MAX_PREC];
	};

#define PKTQ_PREC_ITER(pq, prec)        for (prec = (pq)->num_prec - 1; prec >= 0; prec--)

/* fn(pkt, arg).  return true if pkt belongs to if */
typedef bool(*ifpkt_cb_t) (struct sk_buff *, void *);

/* operations on a specific precedence in packet queue */

#define pktq_psetmax(pq, prec, _max)    ((pq)->q[prec].max = (_max))
#define pktq_plen(pq, prec)             ((pq)->q[prec].len)
#define pktq_pavail(pq, prec)           ((pq)->q[prec].max - (pq)->q[prec].len)
#define pktq_pfull(pq, prec)            ((pq)->q[prec].len >= (pq)->q[prec].max)
#define pktq_pempty(pq, prec)           ((pq)->q[prec].len == 0)

#define pktq_ppeek(pq, prec)            ((pq)->q[prec].head)
#define pktq_ppeek_tail(pq, prec)       ((pq)->q[prec].tail)

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
extern void brcmu_pktq_pflush(struct pktq *pq, int prec,
	bool dir, ifpkt_cb_t fn, void *arg);

/* operations on a set of precedences in packet queue */

extern int brcmu_pktq_mlen(struct pktq *pq, uint prec_bmp);
extern struct sk_buff *brcmu_pktq_mdeq(struct pktq *pq, uint prec_bmp,
	int *prec_out);

/* operations on packet queue as a whole */

#define pktq_len(pq)                    ((int)(pq)->len)
#define pktq_max(pq)                    ((int)(pq)->max)
#define pktq_avail(pq)                  ((int)((pq)->max - (pq)->len))
#define pktq_full(pq)                   ((pq)->len >= (pq)->max)
#define pktq_empty(pq)                  ((pq)->len == 0)

/* operations for single precedence queues */
#define pktenq(pq, p)		brcmu_pktq_penq(((struct pktq *)pq), 0, (p))
#define pktenq_head(pq, p)\
	brcmu_pktq_penq_head(((struct pktq *)pq), 0, (p))
#define pktdeq(pq)		brcmu_pktq_pdeq(((struct pktq *)pq), 0)
#define pktdeq_tail(pq)		brcmu_pktq_pdeq_tail(((struct pktq *)pq), 0)
#define pktqinit(pq, len)	brcmu_pktq_init(((struct pktq *)pq), 1, len)

extern void brcmu_pktq_init(struct pktq *pq, int num_prec, int max_len);
/* prec_out may be NULL if caller is not interested in return value */
extern struct sk_buff *brcmu_pktq_peek_tail(struct pktq *pq, int *prec_out);
extern void brcmu_pktq_flush(struct pktq *pq, bool dir,
	ifpkt_cb_t fn, void *arg);

/* externs */
/* packet */
extern uint brcmu_pktfrombuf(struct sk_buff *p,
	uint offset, int len, unsigned char *buf);
extern uint brcmu_pkttotlen(struct sk_buff *p);

/* ethernet address */
extern int brcmu_ether_atoe(char *p, u8 *ea);

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
#define BCM_IOVT_VALID(type) (((unsigned int)(type)) <= IOVT_BUFFER)

/* Initializer for IOV type strings */
#define BCM_IOV_TYPE_INIT { \
	"void", \
	"bool", \
	"s8", \
	"u8", \
	"s16", \
	"u16", \
	"s32", \
	"u32", \
	"buffer", \
	"" }

#define BCM_IOVT_IS_INT(type) (\
	(type == IOVT_BOOL) || \
	(type == IOVT_INT8) || \
	(type == IOVT_UINT8) || \
	(type == IOVT_INT16) || \
	(type == IOVT_UINT16) || \
	(type == IOVT_INT32) || \
	(type == IOVT_UINT32))

/* ** driver/apps-shared section ** */

#define BCME_STRLEN 		64	/* Max string length for BCM errors */

#ifndef ABS
#define	ABS(a)			(((a) < 0) ? -(a) : (a))
#endif				/* ABS */

#define CEIL(x, y)		(((x) + ((y)-1)) / (y))
#define	ISPOWEROF2(x)		((((x)-1)&(x)) == 0)

/* map physical to virtual I/O */
#if !defined(CONFIG_MMC_MSM7X00A)
#define REG_MAP(pa, size)       ioremap_nocache((unsigned long)(pa), \
					(unsigned long)(size))
#else
#define REG_MAP(pa, size)       (void *)(0)
#endif

/* register access macros */
#if defined(BCMSDIO)
#ifdef BRCM_FULLMAC
#include <bcmsdh.h>
#endif
#endif

#if defined(BCMSDIO)
#define SELECT_BUS_WRITE(mmap_op, bus_op) bus_op
#define SELECT_BUS_READ(mmap_op, bus_op) bus_op
#else
#define SELECT_BUS_WRITE(mmap_op, bus_op) mmap_op
#define SELECT_BUS_READ(mmap_op, bus_op) mmap_op
#endif

/* the largest reasonable packet buffer driver uses for ethernet MTU in bytes */
#define	PKTBUFSZ	2048

#define OSL_SYSUPTIME()		((u32)jiffies * (1000 / HZ))
#ifdef BRCM_FULLMAC
#include <linux/kernel.h>	/* for vsn/printf's */
#include <linux/string.h>	/* for mem*, str* */
#endif

/* register access macros */
#ifndef __BIG_ENDIAN
#ifndef __mips__
#define R_REG(r) (\
	SELECT_BUS_READ(sizeof(*(r)) == sizeof(u8) ? \
	readb((volatile u8*)(r)) : \
	sizeof(*(r)) == sizeof(u16) ? readw((volatile u16*)(r)) : \
	readl((volatile u32*)(r)), bcmsdh_reg_read(NULL, (unsigned long)r, sizeof(*r))) \
)
#else				/* __mips__ */
#define R_REG(r) (\
	SELECT_BUS_READ( \
		({ \
			__typeof(*(r)) __osl_v; \
			__asm__ __volatile__("sync"); \
			switch (sizeof(*(r))) { \
			case sizeof(u8): \
				__osl_v = readb((volatile u8*)(r)); \
				break; \
			case sizeof(u16): \
				__osl_v = readw((volatile u16*)(r)); \
				break; \
			case sizeof(u32): \
				__osl_v = \
				readl((volatile u32*)(r)); \
				break; \
			} \
			__asm__ __volatile__("sync"); \
			__osl_v; \
		}), \
		({ \
			__typeof(*(r)) __osl_v; \
			__asm__ __volatile__("sync"); \
			__osl_v = bcmsdh_reg_read(NULL, (unsigned long)r, sizeof(*r)); \
			__asm__ __volatile__("sync"); \
			__osl_v; \
		})) \
)
#endif				/* __mips__ */

#define W_REG(r, v) do { \
	SELECT_BUS_WRITE( \
		switch (sizeof(*(r))) { \
		case sizeof(u8): \
			writeb((u8)(v), (volatile u8*)(r)); break; \
		case sizeof(u16): \
			writew((u16)(v), (volatile u16*)(r)); break; \
		case sizeof(u32): \
			writel((u32)(v), (volatile u32*)(r)); break; \
		}, \
		bcmsdh_reg_write(NULL, (unsigned long)r, sizeof(*r), (v))); \
	} while (0)
#else				/* __BIG_ENDIAN */
#define R_REG(r) (\
	SELECT_BUS_READ( \
		({ \
			__typeof(*(r)) __osl_v; \
			switch (sizeof(*(r))) { \
			case sizeof(u8): \
				__osl_v = \
				readb((volatile u8*)((r)^3)); \
				break; \
			case sizeof(u16): \
				__osl_v = \
				readw((volatile u16*)((r)^2)); \
				break; \
			case sizeof(u32): \
				__osl_v = readl((volatile u32*)(r)); \
				break; \
			} \
			__osl_v; \
		}), \
		bcmsdh_reg_read(NULL, (unsigned long)r, sizeof(*r))) \
)
#define W_REG(r, v) do { \
	SELECT_BUS_WRITE( \
		switch (sizeof(*(r))) { \
		case sizeof(u8):	\
			writeb((u8)(v), \
			(volatile u8*)((r)^3)); break; \
		case sizeof(u16):	\
			writew((u16)(v), \
			(volatile u16*)((r)^2)); break; \
		case sizeof(u32):	\
			writel((u32)(v), \
			(volatile u32*)(r)); break; \
		}, \
		bcmsdh_reg_write(NULL, (unsigned long)r, sizeof(*r), v)); \
	} while (0)
#endif				/* __BIG_ENDIAN */

#ifdef __mips__
/*
 * bcm4716 (which includes 4717 & 4718), plus 4706 on PCIe can reorder
 * transactions. As a fix, a read after write is performed on certain places
 * in the code. Older chips and the newer 5357 family don't require this fix.
 */
#define W_REG_FLUSH(r, v)	({ W_REG((r), (v)); (void)R_REG(r); })
#else
#define W_REG_FLUSH(r, v)	W_REG((r), (v))
#endif				/* __mips__ */

#define AND_REG(r, v)	W_REG((r), R_REG(r) & (v))
#define OR_REG(r, v)	W_REG((r), R_REG(r) | (v))

#define SET_REG(r, mask, val) \
		W_REG((r), ((R_REG(r) & ~(mask)) | (val)))

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

/* basic mux operation - can be optimized on several architectures */
#define MUX(pred, true, false) ((pred) ? (true) : (false))

/* modulo inc/dec - assumes x E [0, bound - 1] */
#define MODDEC(x, bound) MUX((x) == 0, (bound) - 1, (x) - 1)
#define MODINC(x, bound) MUX((x) == (bound) - 1, 0, (x) + 1)

/* modulo inc/dec, bound = 2^k */
#define MODDEC_POW2(x, bound) (((x) - 1) & ((bound) - 1))
#define MODINC_POW2(x, bound) (((x) + 1) & ((bound) - 1))

/* modulo add/sub - assumes x, y E [0, bound - 1] */
#define MODADD(x, y, bound) \
    MUX((x) + (y) >= (bound), (x) + (y) - (bound), (x) + (y))
#define MODSUB(x, y, bound) \
    MUX(((int)(x)) - ((int)(y)) < 0, (x) - (y) + (bound), (x) - (y))

/* module add/sub, bound = 2^k */
#define MODADD_POW2(x, y, bound) (((x) + (y)) & ((bound) - 1))
#define MODSUB_POW2(x, y, bound) (((x) - (y)) & ((bound) - 1))

/* crc defines */
#define CRC8_INIT_VALUE  0xff	/* Initial CRC8 checksum value */
#define CRC8_GOOD_VALUE  0x9f	/* Good final CRC8 checksum value */
#define CRC16_INIT_VALUE 0xffff	/* Initial CRC16 checksum value */
#define CRC16_GOOD_VALUE 0xf0b8	/* Good final CRC16 checksum value */

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

#define ETHER_ADDR_STR_LEN	18	/* 18-bytes of Ethernet address buffer length */

/* crypto utility function */
/* 128-bit xor: *dst = *src1 xor *src2. dst1, src1 and src2 may have any alignment */
	static inline void
	 xor_128bit_block(const u8 *src1, const u8 *src2, u8 *dst) {
		if (
#ifdef __i386__
			   1 ||
#endif
			   (((unsigned long) src1 | (unsigned long) src2 | (unsigned long) dst) &
			    3) == 0) {
			/* ARM CM3 rel time: 1229 (727 if alignment check could be omitted) */
			/* x86 supports unaligned.  This version runs 6x-9x faster on x86. */
			((u32 *) dst)[0] =
			    ((const u32 *)src1)[0] ^ ((const u32 *)
							 src2)[0];
			((u32 *) dst)[1] =
			    ((const u32 *)src1)[1] ^ ((const u32 *)
							 src2)[1];
			((u32 *) dst)[2] =
			    ((const u32 *)src1)[2] ^ ((const u32 *)
							 src2)[2];
			((u32 *) dst)[3] =
			    ((const u32 *)src1)[3] ^ ((const u32 *)
							 src2)[3];
		} else {
			/* ARM CM3 rel time: 4668 (4191 if alignment check could be omitted) */
			int k;
			for (k = 0; k < 16; k++)
				dst[k] = src1[k] ^ src2[k];
		}
	}

/* externs */
/* crc */
extern u8 brcmu_crc8(u8 *p, uint nbytes, u8 crc);

/* format/print */
#if defined(BCMDBG)
extern int brcmu_format_flags(const struct brcmu_bit_desc *bd, u32 flags,
			      char *buf, int len);
extern int brcmu_format_hex(char *str, const void *bytes, int len);
#endif

extern char *brcmu_chipname(uint chipid, char *buf, uint len);

extern struct brcmu_tlv *brcmu_parse_tlvs(void *buf, int buflen,
					  uint key);

/* multi-bool data type: set of bools, mbool is true if any is set */
	typedef u32 mbool;
#define mboolset(mb, bit)		((mb) |= (bit))	/* set one bool */
#define mboolclr(mb, bit)		((mb) &= ~(bit))	/* clear one bool */
#define mboolisset(mb, bit)		(((mb) & (bit)) != 0)	/* true if one bool is set */
#define	mboolmaskset(mb, mask, val)	((mb) = (((mb) & ~(mask)) | (val)))

/* power conversion */
extern u16 brcmu_qdbm_to_mw(u8 qdbm);
extern u8 brcmu_mw_to_qdbm(u16 mw);

extern void brcmu_binit(struct brcmu_strbuf *b, char *buf, uint size);
extern int brcmu_bprintf(struct brcmu_strbuf *b, const char *fmt, ...);

extern uint brcmu_mkiovar(char *name, char *data, uint datalen,
			  char *buf, uint len);
extern uint brcmu_bitcount(u8 *bitmap, uint bytelength);

#endif				/* _brcmutils_h_ */

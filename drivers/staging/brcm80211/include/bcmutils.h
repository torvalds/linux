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

#ifndef	_bcmutils_h_
#define	_bcmutils_h_

/* Buffer structure for collecting string-formatted data
* using bcm_bprintf() API.
* Use bcm_binit() to initialize before use
*/

	struct bcmstrbuf {
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
	typedef bool(*ifpkt_cb_t) (void *, int);

/* operations on a specific precedence in packet queue */

#define pktq_psetmax(pq, prec, _max)    ((pq)->q[prec].max = (_max))
#define pktq_plen(pq, prec)             ((pq)->q[prec].len)
#define pktq_pavail(pq, prec)           ((pq)->q[prec].max - (pq)->q[prec].len)
#define pktq_pfull(pq, prec)            ((pq)->q[prec].len >= (pq)->q[prec].max)
#define pktq_pempty(pq, prec)           ((pq)->q[prec].len == 0)

#define pktq_ppeek(pq, prec)            ((pq)->q[prec].head)
#define pktq_ppeek_tail(pq, prec)       ((pq)->q[prec].tail)

extern struct sk_buff *pktq_penq(struct pktq *pq, int prec,
				 struct sk_buff *p);
extern struct sk_buff *pktq_penq_head(struct pktq *pq, int prec,
				      struct sk_buff *p);
extern struct sk_buff *pktq_pdeq(struct pktq *pq, int prec);
extern struct sk_buff *pktq_pdeq_tail(struct pktq *pq, int prec);

/* Empty the queue at particular precedence level */
#ifdef BRCM_FULLMAC
	extern void pktq_pflush(struct osl_info *osh, struct pktq *pq, int prec,
		bool dir);
#else
	extern void pktq_pflush(struct osl_info *osh, struct pktq *pq, int prec,
		bool dir, ifpkt_cb_t fn, int arg);
#endif /* BRCM_FULLMAC */

/* operations on a set of precedences in packet queue */

extern int pktq_mlen(struct pktq *pq, uint prec_bmp);
extern struct sk_buff *pktq_mdeq(struct pktq *pq, uint prec_bmp, int *prec_out);

/* operations on packet queue as a whole */

#define pktq_len(pq)                    ((int)(pq)->len)
#define pktq_max(pq)                    ((int)(pq)->max)
#define pktq_avail(pq)                  ((int)((pq)->max - (pq)->len))
#define pktq_full(pq)                   ((pq)->len >= (pq)->max)
#define pktq_empty(pq)                  ((pq)->len == 0)

/* operations for single precedence queues */
#define pktenq(pq, p)		pktq_penq(((struct pktq *)pq), 0, (p))
#define pktenq_head(pq, p)	pktq_penq_head(((struct pktq *)pq), 0, (p))
#define pktdeq(pq)		pktq_pdeq(((struct pktq *)pq), 0)
#define pktdeq_tail(pq)		pktq_pdeq_tail(((struct pktq *)pq), 0)
#define pktqinit(pq, len) pktq_init(((struct pktq *)pq), 1, len)

	extern void pktq_init(struct pktq *pq, int num_prec, int max_len);
/* prec_out may be NULL if caller is not interested in return value */
	extern struct sk_buff *pktq_peek_tail(struct pktq *pq, int *prec_out);
#ifdef BRCM_FULLMAC
	extern void pktq_flush(struct osl_info *osh, struct pktq *pq, bool dir);
#else
	extern void pktq_flush(struct osl_info *osh, struct pktq *pq, bool dir,
		ifpkt_cb_t fn, int arg);
#endif

/* externs */
/* packet */
	extern uint pktfrombuf(struct osl_info *osh, struct sk_buff *p,
			       uint offset, int len, unsigned char *buf);
	extern uint pkttotlen(struct osl_info *osh, struct sk_buff *p);

/* ethernet address */
	extern int bcm_ether_atoe(char *p, u8 *ea);

/* ip address */
	struct ipv4_addr;
	extern char *bcm_ip_ntoa(struct ipv4_addr *ia, char *buf);

/* variable access */
	extern char *getvar(char *vars, const char *name);
	extern int getintvar(char *vars, const char *name);
#ifdef BCMDBG
	extern void prpkt(const char *msg, struct osl_info *osh,
			  struct sk_buff *p0);
#else
#define prpkt(a, b, c)
#endif				/* BCMDBG */

#define bcm_perf_enable()
#define bcmstats(fmt)
#define	bcmlog(fmt, a1, a2)
#define	bcmdumplog(buf, size)	(*buf = '\0')
#define	bcmdumplogent(buf, idx)	-1

#define bcmtslog(tstamp, fmt, a1, a2)
#define bcmprinttslogs()
#define bcmprinttstamp(us)

/* Support for sharing code across in-driver iovar implementations.
 * The intent is that a driver use this structure to map iovar names
 * to its (private) iovar identifiers, and the lookup function to
 * find the entry.  Macros are provided to map ids and get/set actions
 * into a single number space for a switch statement.
 */

/* iovar structure */
	typedef struct bcm_iovar {
		const char *name;	/* name for lookup and display */
		u16 varid;	/* id for switch */
		u16 flags;	/* driver-specific flag bits */
		u16 type;	/* base type of argument */
		u16 minlen;	/* min length for buffer vars */
	} bcm_iovar_t;

/* varid definitions are per-driver, may use these get/set bits */

/* IOVar action bits for id mapping */
#define IOV_GET 0		/* Get an iovar */
#define IOV_SET 1		/* Set an iovar */

/* Varid to actionid mapping */
#define IOV_GVAL(id)		((id)*2)
#define IOV_SVAL(id)		(((id)*2)+IOV_SET)
#define IOV_ISSET(actionid)	((actionid & IOV_SET) == IOV_SET)
#define IOV_ID(actionid)	(actionid >> 1)

/* flags are per-driver based on driver attributes */

	extern const bcm_iovar_t *bcm_iovar_lookup(const bcm_iovar_t *table,
						   const char *name);
	extern int bcm_iovar_lencheck(const bcm_iovar_t *table, void *arg,
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
#define VALID_BCMERROR(e)  ((e <= 0) && (e >= BCME_LAST))

/*
 * error codes could be added but the defined ones shouldn't be changed/deleted
 * these error codes are exposed to the user code
 * when ever a new error code is added to this list
 * please update errorstring table with the related error string and
 * update osl files with os specific errorcode map
*/

#define BCME_OK				0	/* Success */
#define BCME_ERROR			-1	/* Error generic */
#define BCME_BADARG			-2	/* Bad Argument */
#define BCME_BADOPTION			-3	/* Bad option */
#define BCME_NOTUP			-4	/* Not up */
#define BCME_NOTDOWN			-5	/* Not down */
#define BCME_NOTAP			-6	/* Not AP */
#define BCME_NOTSTA			-7	/* Not STA  */
#define BCME_BADKEYIDX			-8	/* BAD Key Index */
#define BCME_RADIOOFF 			-9	/* Radio Off */
#define BCME_NOTBANDLOCKED		-10	/* Not  band locked */
#define BCME_NOCLK			-11	/* No Clock */
#define BCME_BADRATESET			-12	/* BAD Rate valueset */
#define BCME_BADBAND			-13	/* BAD Band */
#define BCME_BUFTOOSHORT		-14	/* Buffer too short */
#define BCME_BUFTOOLONG			-15	/* Buffer too long */
#define BCME_BUSY			-16	/* Busy */
#define BCME_NOTASSOCIATED		-17	/* Not Associated */
#define BCME_BADSSIDLEN			-18	/* Bad SSID len */
#define BCME_OUTOFRANGECHAN		-19	/* Out of Range Channel */
#define BCME_BADCHAN			-20	/* Bad Channel */
#define BCME_BADADDR			-21	/* Bad Address */
#define BCME_NORESOURCE			-22	/* Not Enough Resources */
#define BCME_UNSUPPORTED		-23	/* Unsupported */
#define BCME_BADLEN			-24	/* Bad length */
#define BCME_NOTREADY			-25	/* Not Ready */
#define BCME_EPERM			-26	/* Not Permitted */
#define BCME_NOMEM			-27	/* No Memory */
#define BCME_ASSOCIATED			-28	/* Associated */
#define BCME_RANGE			-29	/* Not In Range */
#define BCME_NOTFOUND			-30	/* Not Found */
#define BCME_WME_NOT_ENABLED		-31	/* WME Not Enabled */
#define BCME_TSPEC_NOTFOUND		-32	/* TSPEC Not Found */
#define BCME_ACM_NOTSUPPORTED		-33	/* ACM Not Supported */
#define BCME_NOT_WME_ASSOCIATION	-34	/* Not WME Association */
#define BCME_SDIO_ERROR			-35	/* SDIO Bus Error */
#define BCME_DONGLE_DOWN		-36	/* Dongle Not Accessible */
#define BCME_VERSION			-37	/* Incorrect version */
#define BCME_TXFAIL			-38	/* TX failure */
#define BCME_RXFAIL			-39	/* RX failure */
#define BCME_NODEVICE			-40	/* Device not present */
#define BCME_NMODE_DISABLED		-41	/* NMODE disabled */
#define BCME_NONRESIDENT		-42	/* access to nonresident overlay */
#define BCME_LAST			BCME_NONRESIDENT

/* These are collection of BCME Error strings */
#define BCMERRSTRINGTABLE {		\
	"OK",				\
	"Undefined error",		\
	"Bad Argument",			\
	"Bad Option",			\
	"Not up",			\
	"Not down",			\
	"Not AP",			\
	"Not STA",			\
	"Bad Key Index",		\
	"Radio Off",			\
	"Not band locked",		\
	"No clock",			\
	"Bad Rate valueset",		\
	"Bad Band",			\
	"Buffer too short",		\
	"Buffer too long",		\
	"Busy",				\
	"Not Associated",		\
	"Bad SSID len",			\
	"Out of Range Channel",		\
	"Bad Channel",			\
	"Bad Address",			\
	"Not Enough Resources",		\
	"Unsupported",			\
	"Bad length",			\
	"Not Ready",			\
	"Not Permitted",		\
	"No Memory",			\
	"Associated",			\
	"Not In Range",			\
	"Not Found",			\
	"WME Not Enabled",		\
	"TSPEC Not Found",		\
	"ACM Not Supported",		\
	"Not WME Association",		\
	"SDIO Bus Error",		\
	"Dongle Not Accessible",	\
	"Incorrect version",		\
	"TX Failure",			\
	"RX Failure",			\
	"Device Not Present",		\
	"NMODE Disabled",		\
	"Nonresident overlay access", \
}

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

/* Register operations */
#define AND_REG(osh, r, v)	W_REG(osh, (r), R_REG(osh, r) & (v))
#define OR_REG(osh, r, v)	W_REG(osh, (r), R_REG(osh, r) | (v))

#define SET_REG(osh, r, mask, val) \
		W_REG((osh), (r), ((R_REG((osh), r) & ~(mask)) | (val)))

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

/* bcm_format_flags() bit description structure */
	typedef struct bcm_bit_desc {
		u32 bit;
		const char *name;
	} bcm_bit_desc_t;

/* tag_ID/length/value_buffer tuple */
	typedef struct bcm_tlv {
		u8 id;
		u8 len;
		u8 data[1];
	} bcm_tlv_t;

/* Check that bcm_tlv_t fits into the given buflen */
#define bcm_valid_tlv(elt, buflen) ((buflen) >= 2 && (int)(buflen) >= (int)(2 + (elt)->len))

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
	extern u8 hndcrc8(u8 *p, uint nbytes, u8 crc);
	extern u16 hndcrc16(u8 *p, uint nbytes, u16 crc);
/* format/print */
#if defined(BCMDBG)
	extern int bcm_format_flags(const bcm_bit_desc_t *bd, u32 flags,
				    char *buf, int len);
	extern int bcm_format_hex(char *str, const void *bytes, int len);
#endif
	extern char *bcm_chipname(uint chipid, char *buf, uint len);
	extern void prhex(const char *msg, unsigned char *buf, uint len);

	extern bcm_tlv_t *bcm_parse_tlvs(void *buf, int buflen,
						    uint key);
/* bcmerror */
	extern const char *bcmerrorstr(int bcmerror);

/* multi-bool data type: set of bools, mbool is true if any is set */
	typedef u32 mbool;
#define mboolset(mb, bit)		((mb) |= (bit))	/* set one bool */
#define mboolclr(mb, bit)		((mb) &= ~(bit))	/* clear one bool */
#define mboolisset(mb, bit)		(((mb) & (bit)) != 0)	/* true if one bool is set */
#define	mboolmaskset(mb, mask, val)	((mb) = (((mb) & ~(mask)) | (val)))

/* power conversion */
	extern u16 bcm_qdbm_to_mw(u8 qdbm);
	extern u8 bcm_mw_to_qdbm(u16 mw);

	extern void bcm_binit(struct bcmstrbuf *b, char *buf, uint size);
	extern int bcm_bprintf(struct bcmstrbuf *b, const char *fmt, ...);

	extern uint bcm_mkiovar(char *name, char *data, uint datalen, char *buf,
				uint len);
	extern uint bcm_bitcount(u8 *bitmap, uint bytelength);

#endif				/* _bcmutils_h_ */

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

#ifndef _linux_osl_h_
#define _linux_osl_h_


/* Linux Kernel: File Operations: start */
extern void *osl_os_open_image(char *filename);
extern int osl_os_get_image_block(char *buf, int len, void *image);
extern void osl_os_close_image(void *image);
/* Linux Kernel: File Operations: end */

extern osl_t *osl_attach(void *pdev, uint bustype, bool pkttag);
extern void osl_detach(osl_t *osh);

extern u32 g_assert_type;

#if defined(BCMDBG_ASSERT)
#define ASSERT(exp) \
	  do { if (!(exp)) osl_assert(#exp, __FILE__, __LINE__); } while (0)
extern void osl_assert(char *exp, char *file, int line);
#else
#ifdef __GNUC__
#define GCC_VERSION \
			(__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#if GCC_VERSION > 30100
#define ASSERT(exp)	do {} while (0)
#else
			/* ASSERT could cause segmentation fault on GCC3.1, use empty instead */
#define ASSERT(exp)
#endif				/* GCC_VERSION > 30100 */
#endif				/* __GNUC__ */
#endif				/* defined(BCMDBG_ASSERT) */

/* PCI configuration space access macros */
#define	OSL_PCI_READ_CONFIG(osh, offset, size) \
	osl_pci_read_config((osh), (offset), (size))
#define	OSL_PCI_WRITE_CONFIG(osh, offset, size, val) \
	osl_pci_write_config((osh), (offset), (size), (val))
extern u32 osl_pci_read_config(osl_t *osh, uint offset, uint size);
extern void osl_pci_write_config(osl_t *osh, uint offset, uint size, uint val);

/* PCI device bus # and slot # */
#define OSL_PCI_BUS(osh)	osl_pci_bus(osh)
#define OSL_PCI_SLOT(osh)	osl_pci_slot(osh)
extern uint osl_pci_bus(osl_t *osh);
extern uint osl_pci_slot(osl_t *osh);

/* Pkttag flag should be part of public information */
typedef struct {
	bool pkttag;
	uint pktalloced;	/* Number of allocated packet buffers */
	bool mmbus;		/* Bus supports memory-mapped register accesses */
	pktfree_cb_fn_t tx_fn;	/* Callback function for PKTFREE */
	void *tx_ctx;		/* Context to the callback function */
#if defined(BCMSDIO) && !defined(BRCM_FULLMAC)
	osl_rreg_fn_t rreg_fn;	/* Read Register function */
	osl_wreg_fn_t wreg_fn;	/* Write Register function */
	void *reg_ctx;		/* Context to the reg callback functions */
#endif
} osl_pubinfo_t;

#define PKTFREESETCB(osh, _tx_fn, _tx_ctx)			\
	do {							\
		((osl_pubinfo_t *)osh)->tx_fn = _tx_fn;		\
		((osl_pubinfo_t *)osh)->tx_ctx = _tx_ctx;	\
	} while (0)

#if defined(BCMSDIO) && !defined(BRCM_FULLMAC)
#define REGOPSSET(osh, rreg, wreg, ctx)			\
	do {						\
		((osl_pubinfo_t *)osh)->rreg_fn = rreg;	\
		((osl_pubinfo_t *)osh)->wreg_fn = wreg;	\
		((osl_pubinfo_t *)osh)->reg_ctx = ctx;	\
	} while (0)
#endif

#define BUS_SWAP32(v)		(v)

#define	DMA_CONSISTENT_ALIGN	osl_dma_consistent_align()
extern uint osl_dma_consistent_align(void);
extern void *osl_dma_alloc_consistent(osl_t *osh, uint size, u16 align,
				      uint *tot, unsigned long *pap);

#ifdef BRCM_FULLMAC
#define	DMA_ALLOC_CONSISTENT(osh, size, pap, dmah, alignbits) \
	osl_dma_alloc_consistent((osh), (size), (0), (tot), (pap))
#else
#define	DMA_ALLOC_CONSISTENT(osh, size, align, tot, pap, dmah) \
	osl_dma_alloc_consistent((osh), (size), (align), (tot), (pap))
#endif /* BRCM_FULLMAC */

#define	DMA_FREE_CONSISTENT(osh, va, size, pa, dmah) \
	osl_dma_free_consistent((osh), (void *)(va), (size), (pa))
extern void osl_dma_free_consistent(osl_t *osh, void *va, uint size, unsigned long pa);

/* map/unmap direction */
#define	DMA_TX	1		/* TX direction for DMA */
#define	DMA_RX	2		/* RX direction for DMA */

/* map/unmap shared (dma-able) memory */
#define	DMA_MAP(osh, va, size, direction, p, dmah) \
	osl_dma_map((osh), (va), (size), (direction))
#define	DMA_UNMAP(osh, pa, size, direction, p, dmah) \
	osl_dma_unmap((osh), (pa), (size), (direction))
extern uint osl_dma_map(osl_t *osh, void *va, uint size, int direction);
extern void osl_dma_unmap(osl_t *osh, uint pa, uint size, int direction);

/* API for DMA addressing capability */
#define OSL_DMADDRWIDTH(osh, addrwidth) do {} while (0)

/* register access macros */
#if defined(BCMSDIO)
#ifdef BRCM_FULLMAC
#include <bcmsdh.h>
#endif
#define OSL_WRITE_REG(osh, r, v) (bcmsdh_reg_write(NULL, (unsigned long)(r), sizeof(*(r)), (v)))
#define OSL_READ_REG(osh, r) (bcmsdh_reg_read(NULL, (unsigned long)(r), sizeof(*(r))))
#endif

#if defined(BCMSDIO)
#define SELECT_BUS_WRITE(osh, mmap_op, bus_op) if (((osl_pubinfo_t *)(osh))->mmbus) \
		mmap_op else bus_op
#define SELECT_BUS_READ(osh, mmap_op, bus_op) (((osl_pubinfo_t *)(osh))->mmbus) ? \
		mmap_op : bus_op
#else
#define SELECT_BUS_WRITE(osh, mmap_op, bus_op) mmap_op
#define SELECT_BUS_READ(osh, mmap_op, bus_op) mmap_op
#endif

#define OSL_ERROR(bcmerror)	osl_error(bcmerror)
extern int osl_error(int bcmerror);

/* the largest reasonable packet buffer driver uses for ethernet MTU in bytes */
#define	PKTBUFSZ	2048	/* largest reasonable packet buffer, driver uses for ethernet MTU */

#define OSL_SYSUPTIME()		((u32)jiffies * (1000 / HZ))
#define	printf(fmt, args...)	printk(fmt , ## args)
#ifdef BRCM_FULLMAC
#include <linux/kernel.h>	/* for vsn/printf's */
#include <linux/string.h>	/* for mem*, str* */
#endif
/* bcopy's: Linux kernel doesn't provide these (anymore) */
#define	bcopy(src, dst, len)	memcpy((dst), (src), (len))
#define	bcmp(b1, b2, len)	memcmp((b1), (b2), (len))
#define	bzero(b, len)		memset((b), '\0', (len))

/* register access macros */
#if defined(OSLREGOPS)
#else
#ifndef IL_BIGENDIAN
#ifndef __mips__
#define R_REG(osh, r) (\
	SELECT_BUS_READ(osh, sizeof(*(r)) == sizeof(u8) ? readb((volatile u8*)(r)) : \
	sizeof(*(r)) == sizeof(u16) ? readw((volatile u16*)(r)) : \
	readl((volatile u32*)(r)), OSL_READ_REG(osh, r)) \
)
#else				/* __mips__ */
#define R_REG(osh, r) (\
	SELECT_BUS_READ(osh, \
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
			__osl_v = OSL_READ_REG(osh, r); \
			__asm__ __volatile__("sync"); \
			__osl_v; \
		})) \
)
#endif				/* __mips__ */

#define W_REG(osh, r, v) do { \
	SELECT_BUS_WRITE(osh,  \
		switch (sizeof(*(r))) { \
		case sizeof(u8): \
			writeb((u8)(v), (volatile u8*)(r)); break; \
		case sizeof(u16): \
			writew((u16)(v), (volatile u16*)(r)); break; \
		case sizeof(u32): \
			writel((u32)(v), (volatile u32*)(r)); break; \
		}, \
		(OSL_WRITE_REG(osh, r, v))); \
	} while (0)
#else				/* IL_BIGENDIAN */
#define R_REG(osh, r) (\
	SELECT_BUS_READ(osh, \
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
		OSL_READ_REG(osh, r)) \
)
#define W_REG(osh, r, v) do { \
	SELECT_BUS_WRITE(osh,  \
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
		(OSL_WRITE_REG(osh, r, v))); \
	} while (0)
#endif				/* IL_BIGENDIAN */

#endif				/* OSLREGOPS */

#define	AND_REG(osh, r, v)		W_REG(osh, (r), R_REG(osh, r) & (v))
#define	OR_REG(osh, r, v)		W_REG(osh, (r), R_REG(osh, r) | (v))

/* bcopy, bcmp, and bzero functions */
#define	bcopy(src, dst, len)	memcpy((dst), (src), (len))
#define	bcmp(b1, b2, len)	memcmp((b1), (b2), (len))
#define	bzero(b, len)		memset((b), '\0', (len))

/* uncached/cached virtual address */
#ifdef __mips__
#include <asm/addrspace.h>
#define OSL_UNCACHED(va)	((void *)KSEG1ADDR((va)))
#define OSL_CACHED(va)		((void *)KSEG0ADDR((va)))
#else
#define OSL_UNCACHED(va)	((void *)va)
#define OSL_CACHED(va)		((void *)va)
#endif				/* mips */

#if defined(mips)
#define	OSL_GETCYCLES(x)	((x) = read_c0_count() * 2)
#elif defined(__i386__)
#define	OSL_GETCYCLES(x)	rdtscl((x))
#else
#define OSL_GETCYCLES(x)	((x) = 0)
#endif				/* defined(mips) */

/* dereference an address that may cause a bus exception */
#ifdef mips
#define	BUSPROBE(val, addr)	get_dbe((val), (addr))
#include <asm/paccess.h>
#else
#define	BUSPROBE(val, addr)	({ (val) = R_REG(NULL, (addr)); 0; })
#endif				/* mips */

/* map/unmap physical to virtual I/O */
#if !defined(CONFIG_MMC_MSM7X00A)
#define	REG_MAP(pa, size)	ioremap_nocache((unsigned long)(pa), (unsigned long)(size))
#else
#define REG_MAP(pa, size)       (void *)(0)
#endif				/* !defined(CONFIG_MMC_MSM7X00A */
#define	REG_UNMAP(va)		iounmap((va))

#define	R_SM(r)			(*(r))
#define	W_SM(r, v)		(*(r) = (v))
#define	BZERO_SM(r, len)	memset((r), '\0', (len))

#ifdef BRCM_FULLMAC
#include <linuxver.h>		/* use current 2.4.x calling conventions */
#endif

/* packet primitives */
#define	PKTGET(osh, len, send)		osl_pktget((osh), (len))
#define	PKTFREE(osh, skb, send)		osl_pktfree((osh), (skb), (send))
#define	PKTDATA(skb)		(((struct sk_buff *)(skb))->data)
#define	PKTLEN(skb)		(((struct sk_buff *)(skb))->len)
#define PKTHEADROOM(skb)		(PKTDATA(skb)-(((struct sk_buff *)(skb))->head))
#define PKTTAILROOM(skb) ((((struct sk_buff *)(skb))->end)-(((struct sk_buff *)(skb))->tail))
#define	PKTNEXT(skb)		(((struct sk_buff *)(skb))->next)
#define	PKTSETNEXT(skb, x)	\
	(((struct sk_buff *)(skb))->next = (struct sk_buff *)(x))
#define	PKTSETLEN(skb, len)	__skb_trim((struct sk_buff *)(skb), (len))
#define	PKTPUSH(skb, bytes)	skb_push((struct sk_buff *)(skb), (bytes))
#define	PKTPULL(skb, bytes)	skb_pull((struct sk_buff *)(skb), (bytes))
#define	PKTTAG(skb)		((void *)(((struct sk_buff *)(skb))->cb))
#define PKTALLOCED(osh)		(((osl_pubinfo_t *)(osh))->pktalloced)
#define PKTSETPOOL(osh, skb, x, y)	do {} while (0)
#define PKTPOOL(osh, skb)		false
extern void *osl_pktget(osl_t *osh, uint len);
extern void osl_pktfree(osl_t *osh, void *skb, bool send);

#ifdef BRCM_FULLMAC
extern void *osl_pktget_static(osl_t *osh, uint len);
extern void osl_pktfree_static(osl_t *osh, void *skb, bool send);

static inline void *
osl_pkt_frmnative(osl_pubinfo_t *osh, struct sk_buff *skb)
{
	struct sk_buff *nskb;

	if (osh->pkttag)
		bzero((void *)skb->cb, OSL_PKTTAG_SZ);

	for (nskb = skb; nskb; nskb = nskb->next)
		osh->pktalloced++;

	return (void *)skb;
}
#define PKTFRMNATIVE(osh, skb)	\
	osl_pkt_frmnative(((osl_pubinfo_t *)osh), (struct sk_buff*)(skb))

static inline struct sk_buff *
osl_pkt_tonative(osl_pubinfo_t *osh, void *pkt)
{
	struct sk_buff *nskb;

	if (osh->pkttag)
		bzero(((struct sk_buff *)pkt)->cb, OSL_PKTTAG_SZ);

	for (nskb = (struct sk_buff *)pkt; nskb; nskb = nskb->next)
		osh->pktalloced--;

	return (struct sk_buff *)pkt;
}
#define PKTTONATIVE(osh, pkt)	\
	osl_pkt_tonative((osl_pubinfo_t *)(osh), (pkt))
#else /* !BRCM_FULLMAC */
#define PKTUNALLOC(osh)			(((osl_pubinfo_t *)(osh))->pktalloced--)

#define	PKTSETSKIPCT(osh, skb)
#define	PKTCLRSKIPCT(osh, skb)
#define	PKTSKIPCT(osh, skb)
#endif	/* BRCM_FULLMAC */

#define	PKTLINK(skb)			(((struct sk_buff *)(skb))->prev)
#define	PKTSETLINK(skb, x)		(((struct sk_buff *)(skb))->prev = (struct sk_buff*)(x))
#define	PKTPRIO(skb)			(((struct sk_buff *)(skb))->priority)
#define	PKTSETPRIO(skb, x)		(((struct sk_buff *)(skb))->priority = (x))
#define PKTSUMNEEDED(skb)		(((struct sk_buff *)(skb))->ip_summed == CHECKSUM_PARTIAL)
#define PKTSETSUMGOOD(skb, x)		(((struct sk_buff *)(skb))->ip_summed = \
						((x) ? CHECKSUM_UNNECESSARY : CHECKSUM_NONE))
/* PKTSETSUMNEEDED and PKTSUMGOOD are not possible because skb->ip_summed is overloaded */
#define PKTSHARED(skb)                  (((struct sk_buff *)(skb))->cloned)

#if defined(BCMSDIO) && !defined(BRCM_FULLMAC)
#define RPC_READ_REG(osh, r) (\
	sizeof(*(r)) == sizeof(u8) ? osl_readb((osh), (volatile u8*)(r)) : \
	sizeof(*(r)) == sizeof(u16) ? osl_readw((osh), (volatile u16*)(r)) : \
	osl_readl((osh), (volatile u32*)(r)) \
)
#define RPC_WRITE_REG(osh, r, v) do { \
	switch (sizeof(*(r))) { \
	case sizeof(u8): \
		osl_writeb((osh), (volatile u8*)(r), (u8)(v)); \
		break; \
	case sizeof(u16): \
		osl_writew((osh), (volatile u16*)(r), (u16)(v)); \
		break; \
	case sizeof(u32): \
		osl_writel((osh), (volatile u32*)(r), (u32)(v)); \
		break; \
	} \
} while (0)

extern u8 osl_readb(osl_t *osh, volatile u8 *r);
extern u16 osl_readw(osl_t *osh, volatile u16 *r);
extern u32 osl_readl(osl_t *osh, volatile u32 *r);
extern void osl_writeb(osl_t *osh, volatile u8 *r, u8 v);
extern void osl_writew(osl_t *osh, volatile u16 *r, u16 v);
extern void osl_writel(osl_t *osh, volatile u32 *r, u32 v);
#endif				/* BCMSDIO */

#endif				/* _linux_osl_h_ */

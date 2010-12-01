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

#include <linux/skbuff.h>

extern struct osl_info *osl_attach(void *pdev, uint bustype);
extern void osl_detach(struct osl_info *osh);

extern u32 g_assert_type;

#if defined(BCMDBG_ASSERT)
#define ASSERT(exp) \
	  do { if (!(exp)) osl_assert(#exp, __FILE__, __LINE__); } while (0)
extern void osl_assert(char *exp, char *file, int line);
#else
#define ASSERT(exp)	do {} while (0)
#endif  /* defined(BCMDBG_ASSERT) */

/* PCI device bus # and slot # */
#define OSL_PCI_BUS(osh)	osl_pci_bus(osh)
#define OSL_PCI_SLOT(osh)	osl_pci_slot(osh)
extern uint osl_pci_bus(struct osl_info *osh);
extern uint osl_pci_slot(struct osl_info *osh);

#define BUS_SWAP32(v)		(v)

extern void *osl_dma_alloc_consistent(struct osl_info *osh, uint size,
				      u16 align, uint *tot, unsigned long *pap);

#ifdef BRCM_FULLMAC
#define	DMA_ALLOC_CONSISTENT(osh, size, pap, dmah, alignbits) \
	osl_dma_alloc_consistent((osh), (size), (0), (tot), (pap))
#else
#define	DMA_ALLOC_CONSISTENT(osh, size, align, tot, pap, dmah) \
	osl_dma_alloc_consistent((osh), (size), (align), (tot), (pap))
#endif /* BRCM_FULLMAC */

#define	DMA_FREE_CONSISTENT(osh, va, size, pa, dmah) \
	osl_dma_free_consistent((osh), (void *)(va), (size), (pa))
extern void osl_dma_free_consistent(struct osl_info *osh, void *va,
				    uint size, unsigned long pa);

/* map/unmap direction */
#define	DMA_TX	1		/* TX direction for DMA */
#define	DMA_RX	2		/* RX direction for DMA */

/* map/unmap shared (dma-able) memory */
#define	DMA_MAP(osh, va, size, direction, p, dmah) \
	osl_dma_map((osh), (va), (size), (direction))
#define	DMA_UNMAP(osh, pa, size, direction, p, dmah) \
	osl_dma_unmap((osh), (pa), (size), (direction))
extern uint osl_dma_map(struct osl_info *osh, void *va, uint size,
			int direction);
extern void osl_dma_unmap(struct osl_info *osh, uint pa, uint size,
			  int direction);

/* register access macros */
#if defined(BCMSDIO)
#ifdef BRCM_FULLMAC
#include <bcmsdh.h>
#endif
#define OSL_WRITE_REG(osh, r, v) (bcmsdh_reg_write(NULL, (unsigned long)(r), sizeof(*(r)), (v)))
#define OSL_READ_REG(osh, r) (bcmsdh_reg_read(NULL, (unsigned long)(r), sizeof(*(r))))
#endif

#if defined(BCMSDIO)
#define SELECT_BUS_WRITE(osh, mmap_op, bus_op) \
	if (((struct osl_pubinfo *)(osh))->mmbus) \
		mmap_op else bus_op
#define SELECT_BUS_READ(osh, mmap_op, bus_op) \
	(((struct osl_pubinfo *)(osh))->mmbus) ?  mmap_op : bus_op
#else
#define SELECT_BUS_WRITE(osh, mmap_op, bus_op) mmap_op
#define SELECT_BUS_READ(osh, mmap_op, bus_op) mmap_op
#endif

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

#define	bcopy(src, dst, len)	memcpy((dst), (src), (len))

/* uncached/cached virtual address */
#ifdef __mips__
#include <asm/addrspace.h>
#define OSL_UNCACHED(va)	((void *)KSEG1ADDR((va)))
#define OSL_CACHED(va)		((void *)KSEG0ADDR((va)))
#else
#define OSL_UNCACHED(va)	((void *)va)
#define OSL_CACHED(va)		((void *)va)
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

/* packet primitives */
#define	PKTGET(osh, len, send)	osl_pktget((osh), (len))
#define	PKTFREE(osh, skb, send)	osl_pktfree((osh), (skb), (send))

extern void *osl_pktget(struct osl_info *osh, uint len);
extern void osl_pktfree(struct osl_info *osh, void *skb, bool send);

#ifdef BRCM_FULLMAC
static inline void *
osl_pkt_frmnative(struct osl_pubinfo *osh, struct sk_buff *skb)
{
	struct sk_buff *nskb;

	for (nskb = skb; nskb; nskb = nskb->next)
		osh->pktalloced++;

	return (void *)skb;
}
#define PKTFRMNATIVE(osh, skb)	\
	osl_pkt_frmnative(((struct osl_pubinfo *)osh), (struct sk_buff*)(skb))

static inline struct sk_buff *
osl_pkt_tonative(struct osl_pubinfo *osh, void *pkt)
{
	struct sk_buff *nskb;

	for (nskb = (struct sk_buff *)pkt; nskb; nskb = nskb->next)
		osh->pktalloced--;

	return (struct sk_buff *)pkt;
}
#define PKTTONATIVE(osh, pkt)	\
	osl_pkt_tonative((struct osl_pubinfo *)(osh), (pkt))
#else /* !BRCM_FULLMAC */
#define	PKTSETSKIPCT(osh, skb)
#define	PKTCLRSKIPCT(osh, skb)
#define	PKTSKIPCT(osh, skb)
#endif	/* BRCM_FULLMAC */

#define PKTSUMNEEDED(skb)		(((struct sk_buff *)(skb))->ip_summed == CHECKSUM_PARTIAL)
#define PKTSETSUMGOOD(skb, x)		(((struct sk_buff *)(skb))->ip_summed = \
						((x) ? CHECKSUM_UNNECESSARY : CHECKSUM_NONE))
/* PKTSETSUMNEEDED and PKTSUMGOOD are not possible because skb->ip_summed is overloaded */

#endif				/* _linux_osl_h_ */

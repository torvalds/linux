/*
 * Linux OS Independent Layer
 *
 * Copyright (C) 1999-2010, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: linux_osl.h,v 13.131.30.8 2010/04/26 05:42:18 Exp $
 */


#ifndef _linux_osl_h_
#define _linux_osl_h_

#include <typedefs.h>


#include <linuxver.h>


#ifdef __GNUC__
#define GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#if GCC_VERSION > 30100
#define	ASSERT(exp)		do {} while (0)
#else

#define	ASSERT(exp)
#endif 
#endif 


#define	OSL_DELAY(usec)		osl_delay(usec)
extern void osl_delay(uint usec);



#define	OSL_PCMCIA_READ_ATTR(osh, offset, buf, size) \
	osl_pcmcia_read_attr((osh), (offset), (buf), (size))
#define	OSL_PCMCIA_WRITE_ATTR(osh, offset, buf, size) \
	osl_pcmcia_write_attr((osh), (offset), (buf), (size))
extern void osl_pcmcia_read_attr(osl_t *osh, uint offset, void *buf, int size);
extern void osl_pcmcia_write_attr(osl_t *osh, uint offset, void *buf, int size);


#define	OSL_PCI_READ_CONFIG(osh, offset, size) \
	osl_pci_read_config((osh), (offset), (size))
#define	OSL_PCI_WRITE_CONFIG(osh, offset, size, val) \
	osl_pci_write_config((osh), (offset), (size), (val))
extern uint32 osl_pci_read_config(osl_t *osh, uint offset, uint size);
extern void osl_pci_write_config(osl_t *osh, uint offset, uint size, uint val);


#define OSL_PCI_BUS(osh)	osl_pci_bus(osh)
#define OSL_PCI_SLOT(osh)	osl_pci_slot(osh)
extern uint osl_pci_bus(osl_t *osh);
extern uint osl_pci_slot(osl_t *osh);


typedef struct {
	bool pkttag;
	uint pktalloced; 	
	bool mmbus;		
	pktfree_cb_fn_t tx_fn;  
	void *tx_ctx;		
} osl_pubinfo_t;


extern osl_t *osl_attach(void *pdev, uint bustype, bool pkttag);
extern void osl_detach(osl_t *osh);

#define PKTFREESETCB(osh, _tx_fn, _tx_ctx) \
	do { \
	   ((osl_pubinfo_t*)osh)->tx_fn = _tx_fn; \
	   ((osl_pubinfo_t*)osh)->tx_ctx = _tx_ctx; \
	} while (0)


#define BUS_SWAP32(v)		(v)


#define	MALLOC(osh, size)	osl_malloc((osh), (size))
#define	MFREE(osh, addr, size)	osl_mfree((osh), (addr), (size))
#define MALLOCED(osh)		osl_malloced((osh))


#define	MALLOC_FAILED(osh)	osl_malloc_failed((osh))

extern void *osl_malloc(osl_t *osh, uint size);
extern void osl_mfree(osl_t *osh, void *addr, uint size);
extern uint osl_malloced(osl_t *osh);
extern uint osl_malloc_failed(osl_t *osh);


#define	DMA_CONSISTENT_ALIGN	PAGE_SIZE
#define	DMA_ALLOC_CONSISTENT(osh, size, pap, dmah, alignbits) \
	osl_dma_alloc_consistent((osh), (size), (pap))
#define	DMA_FREE_CONSISTENT(osh, va, size, pa, dmah) \
	osl_dma_free_consistent((osh), (void*)(va), (size), (pa))
extern void *osl_dma_alloc_consistent(osl_t *osh, uint size, ulong *pap);
extern void osl_dma_free_consistent(osl_t *osh, void *va, uint size, ulong pa);


#define	DMA_TX	1	
#define	DMA_RX	2	


#define	DMA_MAP(osh, va, size, direction, p, dmah) \
	osl_dma_map((osh), (va), (size), (direction))
#define	DMA_UNMAP(osh, pa, size, direction, p, dmah) \
	osl_dma_unmap((osh), (pa), (size), (direction))
extern uint osl_dma_map(osl_t *osh, void *va, uint size, int direction);
extern void osl_dma_unmap(osl_t *osh, uint pa, uint size, int direction);


#define OSL_DMADDRWIDTH(osh, addrwidth) do {} while (0)


#include <bcmsdh.h>
#define OSL_WRITE_REG(osh, r, v) (bcmsdh_reg_write(NULL, (uintptr)(r), sizeof(*(r)), (v)))
#define OSL_READ_REG(osh, r) (bcmsdh_reg_read(NULL, (uintptr)(r), sizeof(*(r))))

#define SELECT_BUS_WRITE(osh, mmap_op, bus_op) if (((osl_pubinfo_t*)(osh))->mmbus) \
	mmap_op else bus_op
#define SELECT_BUS_READ(osh, mmap_op, bus_op) (((osl_pubinfo_t*)(osh))->mmbus) ? \
	mmap_op : bus_op




#ifndef printf
#define	printf(fmt, args...)	printk(fmt, ## args)
#endif 
#include <linux/kernel.h>
#include <linux/string.h>


#ifndef IL_BIGENDIAN
#define R_REG(osh, r) (\
	SELECT_BUS_READ(osh, sizeof(*(r)) == sizeof(uint8) ? readb((volatile uint8*)(r)) : \
	sizeof(*(r)) == sizeof(uint16) ? readw((volatile uint16*)(r)) : \
	readl((volatile uint32*)(r)), OSL_READ_REG(osh, r)) \
)
#define W_REG(osh, r, v) do { \
	SELECT_BUS_WRITE(osh,  \
		switch (sizeof(*(r))) { \
			case sizeof(uint8):	writeb((uint8)(v), (volatile uint8*)(r)); break; \
			case sizeof(uint16):	writew((uint16)(v), (volatile uint16*)(r)); break; \
			case sizeof(uint32):	writel((uint32)(v), (volatile uint32*)(r)); break; \
		}, \
		(OSL_WRITE_REG(osh, r, v))); \
	} while (0)
#else	
#define R_REG(osh, r) (\
	SELECT_BUS_READ(osh, \
		({ \
			__typeof(*(r)) __osl_v; \
			switch (sizeof(*(r))) { \
				case sizeof(uint8):	__osl_v = \
					readb((volatile uint8*)((uintptr)(r)^3)); break; \
				case sizeof(uint16):	__osl_v = \
					readw((volatile uint16*)((uintptr)(r)^2)); break; \
				case sizeof(uint32):	__osl_v = \
					readl((volatile uint32*)(r)); break; \
			} \
			__osl_v; \
		}), \
		OSL_READ_REG(osh, r)) \
)
#define W_REG(osh, r, v) do { \
	SELECT_BUS_WRITE(osh,  \
		switch (sizeof(*(r))) { \
			case sizeof(uint8):	writeb((uint8)(v), \
					(volatile uint8*)((uintptr)(r)^3)); break; \
			case sizeof(uint16):	writew((uint16)(v), \
					(volatile uint16*)((uintptr)(r)^2)); break; \
			case sizeof(uint32):	writel((uint32)(v), \
					(volatile uint32*)(r)); break; \
		}, \
		(OSL_WRITE_REG(osh, r, v))); \
	} while (0)
#endif 

#define	AND_REG(osh, r, v)		W_REG(osh, (r), R_REG(osh, r) & (v))
#define	OR_REG(osh, r, v)		W_REG(osh, (r), R_REG(osh, r) | (v))


#define	bcopy(src, dst, len)	memcpy((dst), (src), (len))
#define	bcmp(b1, b2, len)	memcmp((b1), (b2), (len))
#define	bzero(b, len)		memset((b), '\0', (len))


#define OSL_UNCACHED(va)	((void*)va)


#if defined(__i386__)
#define	OSL_GETCYCLES(x)	rdtscl((x))
#else
#define OSL_GETCYCLES(x)	((x) = 0)
#endif 


#define	BUSPROBE(val, addr)	({ (val) = R_REG(NULL, (addr)); 0; })


#if !defined(CONFIG_MMC_MSM7X00A)
#define	REG_MAP(pa, size)	ioremap_nocache((unsigned long)(pa), (unsigned long)(size))
#else
#define REG_MAP(pa, size)       (void *)(0)
#endif 
#define	REG_UNMAP(va)		iounmap((va))


#define	R_SM(r)			*(r)
#define	W_SM(r, v)		(*(r) = (v))
#define	BZERO_SM(r, len)	memset((r), '\0', (len))


#define	PKTGET(osh, len, send)		osl_pktget((osh), (len))
#define	PKTFREE(osh, skb, send)		osl_pktfree((osh), (skb), (send))
#ifdef DHD_USE_STATIC_BUF
#define	PKTGET_STATIC(osh, len, send)		osl_pktget_static((osh), (len))
#define	PKTFREE_STATIC(osh, skb, send)		osl_pktfree_static((osh), (skb), (send))
#endif 
#define	PKTDATA(osh, skb)		(((struct sk_buff*)(skb))->data)
#define	PKTLEN(osh, skb)		(((struct sk_buff*)(skb))->len)
#define PKTHEADROOM(osh, skb)		(PKTDATA(osh, skb)-(((struct sk_buff*)(skb))->head))
#define PKTTAILROOM(osh, skb) ((((struct sk_buff*)(skb))->end)-(((struct sk_buff*)(skb))->tail))
#define	PKTNEXT(osh, skb)		(((struct sk_buff*)(skb))->next)
#define	PKTSETNEXT(osh, skb, x)		(((struct sk_buff*)(skb))->next = (struct sk_buff*)(x))
#define	PKTSETLEN(osh, skb, len)	__skb_trim((struct sk_buff*)(skb), (len))
#define	PKTPUSH(osh, skb, bytes)	skb_push((struct sk_buff*)(skb), (bytes))
#define	PKTPULL(osh, skb, bytes)	skb_pull((struct sk_buff*)(skb), (bytes))
#define	PKTDUP(osh, skb)		osl_pktdup((osh), (skb))
#define	PKTTAG(skb)			((void*)(((struct sk_buff*)(skb))->cb))
#define PKTALLOCED(osh)			((osl_pubinfo_t *)(osh))->pktalloced
#define PKTSETPOOL(osh, skb, x, y)	do {} while (0)
#define PKTPOOL(osh, skb)		FALSE
#define PKTPOOLLEN(osh, pktp)		(0)
#define PKTPOOLAVAIL(osh, pktp)		(0)
#define PKTPOOLADD(osh, pktp, p)	BCME_ERROR
#define PKTPOOLGET(osh, pktp)		NULL
#define PKTLIST_DUMP(osh, buf)

extern void *osl_pktget(osl_t *osh, uint len);
extern void osl_pktfree(osl_t *osh, void *skb, bool send);
extern void *osl_pktget_static(osl_t *osh, uint len);
extern void osl_pktfree_static(osl_t *osh, void *skb, bool send);
extern void *osl_pktdup(osl_t *osh, void *skb);



static INLINE void *
osl_pkt_frmnative(osl_pubinfo_t *osh, struct sk_buff *skb)
{
	struct sk_buff *nskb;

	if (osh->pkttag)
		bzero((void*)skb->cb, OSL_PKTTAG_SZ);

	
	for (nskb = skb; nskb; nskb = nskb->next) {
		osh->pktalloced++;
	}

	return (void *)skb;
}
#define PKTFRMNATIVE(osh, skb)	osl_pkt_frmnative(((osl_pubinfo_t *)osh), (struct sk_buff*)(skb))


static INLINE struct sk_buff *
osl_pkt_tonative(osl_pubinfo_t *osh, void *pkt)
{
	struct sk_buff *nskb;

	if (osh->pkttag)
		bzero(((struct sk_buff*)pkt)->cb, OSL_PKTTAG_SZ);

	
	for (nskb = (struct sk_buff *)pkt; nskb; nskb = nskb->next) {
		osh->pktalloced--;
	}

	return (struct sk_buff *)pkt;
}
#define PKTTONATIVE(osh, pkt)		osl_pkt_tonative((osl_pubinfo_t *)(osh), (pkt))

#define	PKTLINK(skb)			(((struct sk_buff*)(skb))->prev)
#define	PKTSETLINK(skb, x)		(((struct sk_buff*)(skb))->prev = (struct sk_buff*)(x))
#define	PKTPRIO(skb)			(((struct sk_buff*)(skb))->priority)
#define	PKTSETPRIO(skb, x)		(((struct sk_buff*)(skb))->priority = (x))
#define PKTSUMNEEDED(skb)		(((struct sk_buff*)(skb))->ip_summed == CHECKSUM_HW)
#define PKTSETSUMGOOD(skb, x)		(((struct sk_buff*)(skb))->ip_summed = \
						((x) ? CHECKSUM_UNNECESSARY : CHECKSUM_NONE))

#define PKTSHARED(skb)                  (((struct sk_buff*)(skb))->cloned)


#define OSL_ERROR(bcmerror)	osl_error(bcmerror)
extern int osl_error(int bcmerror);


#define	PKTBUFSZ	2048   


#define OSL_SYSUPTIME()		((uint32)jiffies * (1000 / HZ))
#endif	

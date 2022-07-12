/*
 * Linux OS Independent Layer
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
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
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: linux_osl.h 692391 2017-03-28 00:29:04Z $
 */

#ifndef _linux_osl_h_
#define _linux_osl_h_

#include <typedefs.h>
#define DECLSPEC_ALIGN(x)	__attribute__ ((aligned(x)))
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 1))
#include <linux/time64.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0))
void do_gettimeofday(struct timeval *tv);
#else
struct timeval {
        time64_t        tv_sec;
        long            tv_usec;
};
struct timeval do_gettimeofday(void);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0)  */
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 1) */
/* Linux Kernel: File Operations: start */
extern void * osl_os_open_image(char * filename);
extern int osl_os_get_image_block(char * buf, int len, void * image);
extern void osl_os_close_image(void * image);
extern int osl_os_image_size(void *image);
/* Linux Kernel: File Operations: end */

#ifdef BCMDRIVER

/* OSL initialization */
extern osl_t *osl_attach(void *pdev, uint bustype, bool pkttag);

extern void osl_detach(osl_t *osh);
extern int osl_static_mem_init(osl_t *osh, void *adapter);
extern int osl_static_mem_deinit(osl_t *osh, void *adapter);
extern void osl_set_bus_handle(osl_t *osh, void *bus_handle);
extern void* osl_get_bus_handle(osl_t *osh);
#ifdef DHD_MAP_LOGGING
extern void osl_dma_map_dump(osl_t *osh);
#define OSL_DMA_MAP_DUMP(osh)	osl_dma_map_dump(osh)
#else
#define OSL_DMA_MAP_DUMP(osh)	do {} while (0)
#endif /* DHD_MAP_LOGGING */

/* Global ASSERT type */
extern uint32 g_assert_type;

#ifdef CONFIG_PHYS_ADDR_T_64BIT
#define PRI_FMT_x       "llx"
#define PRI_FMT_X       "llX"
#define PRI_FMT_o       "llo"
#define PRI_FMT_d       "lld"
#else
#define PRI_FMT_x       "x"
#define PRI_FMT_X       "X"
#define PRI_FMT_o       "o"
#define PRI_FMT_d       "d"
#endif /* CONFIG_PHYS_ADDR_T_64BIT */
/* ASSERT */
#ifndef ASSERT
#if defined(BCMASSERT_LOG)
	#define ASSERT(exp) \
	  do { if (!(exp)) osl_assert(#exp, __FILE__, __LINE__); } while (0)
extern void osl_assert(const char *exp, const char *file, int line);
#else
	#ifdef __GNUC__
		#define GCC_VERSION \
			(__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
		#if GCC_VERSION > 30100
			#define ASSERT(exp)	do {} while (0)
		#else
			/* ASSERT could cause segmentation fault on GCC3.1, use empty instead */
			#define ASSERT(exp)
		#endif /* GCC_VERSION > 30100 */
	#endif /* __GNUC__ */
#endif // endif
#endif /* ASSERT */

/* bcm_prefetch_32B */
static inline void bcm_prefetch_32B(const uint8 *addr, const int cachelines_32B)
{
#if (defined(STB) && defined(__arm__)) && (__LINUX_ARM_ARCH__ >= 5)
	switch (cachelines_32B) {
		case 4: __asm__ __volatile__("pld\t%a0" :: "p"(addr + 96) : "cc");
		case 3: __asm__ __volatile__("pld\t%a0" :: "p"(addr + 64) : "cc");
		case 2: __asm__ __volatile__("pld\t%a0" :: "p"(addr + 32) : "cc");
		case 1: __asm__ __volatile__("pld\t%a0" :: "p"(addr +  0) : "cc");
	}
#endif // endif
}

/* microsecond delay */
#define	OSL_DELAY(usec)		osl_delay(usec)
extern void osl_delay(uint usec);

#define OSL_SLEEP(ms)			osl_sleep(ms)
extern void osl_sleep(uint ms);

#define	OSL_PCMCIA_READ_ATTR(osh, offset, buf, size) \
	osl_pcmcia_read_attr((osh), (offset), (buf), (size))
#define	OSL_PCMCIA_WRITE_ATTR(osh, offset, buf, size) \
	osl_pcmcia_write_attr((osh), (offset), (buf), (size))
extern void osl_pcmcia_read_attr(osl_t *osh, uint offset, void *buf, int size);
extern void osl_pcmcia_write_attr(osl_t *osh, uint offset, void *buf, int size);

/* PCI configuration space access macros */
#define	OSL_PCI_READ_CONFIG(osh, offset, size) \
	osl_pci_read_config((osh), (offset), (size))
#define	OSL_PCI_WRITE_CONFIG(osh, offset, size, val) \
	osl_pci_write_config((osh), (offset), (size), (val))
extern uint32 osl_pci_read_config(osl_t *osh, uint offset, uint size);
extern void osl_pci_write_config(osl_t *osh, uint offset, uint size, uint val);

/* PCI device bus # and slot # */
#define OSL_PCI_BUS(osh)	osl_pci_bus(osh)
#define OSL_PCI_SLOT(osh)	osl_pci_slot(osh)
#define OSL_PCIE_DOMAIN(osh)	osl_pcie_domain(osh)
#define OSL_PCIE_BUS(osh)	osl_pcie_bus(osh)
extern uint osl_pci_bus(osl_t *osh);
extern uint osl_pci_slot(osl_t *osh);
extern uint osl_pcie_domain(osl_t *osh);
extern uint osl_pcie_bus(osl_t *osh);
extern struct pci_dev *osl_pci_device(osl_t *osh);

#define OSL_ACP_COHERENCE		(1<<1L)
#define OSL_FWDERBUF			(1<<2L)

/* Pkttag flag should be part of public information */
typedef struct {
	bool pkttag;
	bool mmbus;		/**< Bus supports memory-mapped register accesses */
	pktfree_cb_fn_t tx_fn;  /**< Callback function for PKTFREE */
	void *tx_ctx;		/**< Context to the callback function */
	void	*unused[3];
	void (*rx_fn)(void *rx_ctx, void *p);
	void *rx_ctx;
} osl_pubinfo_t;

extern void osl_flag_set(osl_t *osh, uint32 mask);
extern void osl_flag_clr(osl_t *osh, uint32 mask);
extern bool osl_is_flag_set(osl_t *osh, uint32 mask);

#define PKTFREESETCB(osh, _tx_fn, _tx_ctx)		\
	do {						\
	   ((osl_pubinfo_t*)osh)->tx_fn = _tx_fn;	\
	   ((osl_pubinfo_t*)osh)->tx_ctx = _tx_ctx;	\
	} while (0)

#define PKTFREESETRXCB(osh, _rx_fn, _rx_ctx)		\
	do {						\
	   ((osl_pubinfo_t*)osh)->rx_fn = _rx_fn;	\
	   ((osl_pubinfo_t*)osh)->rx_ctx = _rx_ctx;	\
	} while (0)

/* host/bus architecture-specific byte swap */
#define BUS_SWAP32(v)		(v)
	#define MALLOC(osh, size)	osl_malloc((osh), (size))
	#define MALLOCZ(osh, size)	osl_mallocz((osh), (size))
	#define MFREE(osh, addr, size)	osl_mfree((osh), (addr), (size))
	#define VMALLOC(osh, size)	osl_vmalloc((osh), (size))
	#define VMALLOCZ(osh, size)	osl_vmallocz((osh), (size))
	#define VMFREE(osh, addr, size)	osl_vmfree((osh), (addr), (size))
	#define MALLOCED(osh)		osl_malloced((osh))
	#define MEMORY_LEFTOVER(osh) osl_check_memleak(osh)
	extern void *osl_malloc(osl_t *osh, uint size);
	extern void *osl_mallocz(osl_t *osh, uint size);
	extern void osl_mfree(osl_t *osh, void *addr, uint size);
	extern void *osl_vmalloc(osl_t *osh, uint size);
	extern void *osl_vmallocz(osl_t *osh, uint size);
	extern void osl_vmfree(osl_t *osh, void *addr, uint size);
	extern uint osl_malloced(osl_t *osh);
	extern uint osl_check_memleak(osl_t *osh);

#define	MALLOC_FAILED(osh)	osl_malloc_failed((osh))
extern uint osl_malloc_failed(osl_t *osh);

/* allocate/free shared (dma-able) consistent memory */
#define	DMA_CONSISTENT_ALIGN	osl_dma_consistent_align()
#define	DMA_ALLOC_CONSISTENT(osh, size, align, tot, pap, dmah) \
	osl_dma_alloc_consistent((osh), (size), (align), (tot), (pap))
#define	DMA_FREE_CONSISTENT(osh, va, size, pa, dmah) \
	osl_dma_free_consistent((osh), (void*)(va), (size), (pa))

#define	DMA_ALLOC_CONSISTENT_FORCE32(osh, size, align, tot, pap, dmah) \
	osl_dma_alloc_consistent((osh), (size), (align), (tot), (pap))
#define	DMA_FREE_CONSISTENT_FORCE32(osh, va, size, pa, dmah) \
	osl_dma_free_consistent((osh), (void*)(va), (size), (pa))

extern uint osl_dma_consistent_align(void);
extern void *osl_dma_alloc_consistent(osl_t *osh, uint size, uint16 align,
	uint *tot, dmaaddr_t *pap);
extern void osl_dma_free_consistent(osl_t *osh, void *va, uint size, dmaaddr_t pa);

/* map/unmap direction */
#define DMA_NO	0	/* Used to skip cache op */
#define	DMA_TX	1	/* TX direction for DMA */
#define	DMA_RX	2	/* RX direction for DMA */

/* map/unmap shared (dma-able) memory */
#define	DMA_UNMAP(osh, pa, size, direction, p, dmah) \
	osl_dma_unmap((osh), (pa), (size), (direction))
extern void osl_dma_flush(osl_t *osh, void *va, uint size, int direction, void *p,
	hnddma_seg_map_t *txp_dmah);
extern dmaaddr_t osl_dma_map(osl_t *osh, void *va, uint size, int direction, void *p,
	hnddma_seg_map_t *txp_dmah);
extern void osl_dma_unmap(osl_t *osh, dmaaddr_t pa, uint size, int direction);

#ifndef PHYS_TO_VIRT
#define	PHYS_TO_VIRT(pa)	osl_phys_to_virt(pa)
#endif // endif
#ifndef VIRT_TO_PHYS
#define	VIRT_TO_PHYS(va)	osl_virt_to_phys(va)
#endif // endif
extern void * osl_phys_to_virt(void * pa);
extern void * osl_virt_to_phys(void * va);

/* API for DMA addressing capability */
#define OSL_DMADDRWIDTH(osh, addrwidth) ({BCM_REFERENCE(osh); BCM_REFERENCE(addrwidth);})

#define OSL_SMP_WMB()	smp_wmb()

/* API for CPU relax */
extern void osl_cpu_relax(void);
#define OSL_CPU_RELAX() osl_cpu_relax()

extern void osl_preempt_disable(osl_t *osh);
extern void osl_preempt_enable(osl_t *osh);
#define OSL_DISABLE_PREEMPTION(osh)	osl_preempt_disable(osh)
#define OSL_ENABLE_PREEMPTION(osh)	osl_preempt_enable(osh)

#if (!defined(DHD_USE_COHERENT_MEM_FOR_RING) && defined(__ARM_ARCH_7A__)) || \
	defined(STB_SOC_WIFI)
	extern void osl_cache_flush(void *va, uint size);
	extern void osl_cache_inv(void *va, uint size);
	extern void osl_prefetch(const void *ptr);
	#define OSL_CACHE_FLUSH(va, len)	osl_cache_flush((void *)(va), len)
	#define OSL_CACHE_INV(va, len)		osl_cache_inv((void *)(va), len)
	#define OSL_PREFETCH(ptr)			osl_prefetch(ptr)
#if defined(__ARM_ARCH_7A__) || defined(STB_SOC_WIFI)
	extern int osl_arch_is_coherent(void);
	#define OSL_ARCH_IS_COHERENT()		osl_arch_is_coherent()
	extern int osl_acp_war_enab(void);
	#define OSL_ACP_WAR_ENAB()			osl_acp_war_enab()
#else  /* !__ARM_ARCH_7A__ */
	#define OSL_ARCH_IS_COHERENT()		NULL
	#define OSL_ACP_WAR_ENAB()			NULL
#endif /* !__ARM_ARCH_7A__ */
#else  /* !__mips__ && !__ARM_ARCH_7A__ */
	#define OSL_CACHE_FLUSH(va, len)	BCM_REFERENCE(va)
	#define OSL_CACHE_INV(va, len)		BCM_REFERENCE(va)
	#define OSL_PREFETCH(ptr)		BCM_REFERENCE(ptr)

	#define OSL_ARCH_IS_COHERENT()		NULL
	#define OSL_ACP_WAR_ENAB()			NULL
#endif // endif

#ifdef BCM_BACKPLANE_TIMEOUT
extern void osl_set_bpt_cb(osl_t *osh, void *bpt_cb, void *bpt_ctx);
extern void osl_bpt_rreg(osl_t *osh, ulong addr, volatile void *v, uint size);
#endif /* BCM_BACKPLANE_TIMEOUT */

#if (defined(STB) && defined(__arm__))
extern void osl_pcie_rreg(osl_t *osh, ulong addr, volatile void *v, uint size);
#endif // endif

/* register access macros */
#if defined(BCMSDIO)
	#include <bcmsdh.h>
	#define OSL_WRITE_REG(osh, r, v) (bcmsdh_reg_write(osl_get_bus_handle(osh), \
		(uintptr)(r), sizeof(*(r)), (v)))
	#define OSL_READ_REG(osh, r) (bcmsdh_reg_read(osl_get_bus_handle(osh), \
		(uintptr)(r), sizeof(*(r))))
#elif defined(BCM_BACKPLANE_TIMEOUT)
#define OSL_READ_REG(osh, r) \
	({\
		__typeof(*(r)) __osl_v; \
		osl_bpt_rreg(osh, (uintptr)(r), &__osl_v, sizeof(*(r))); \
		__osl_v; \
	})
#elif (defined(STB) && defined(__arm__))
#define OSL_READ_REG(osh, r) \
	({\
		__typeof(*(r)) __osl_v; \
		osl_pcie_rreg(osh, (uintptr)(r), &__osl_v, sizeof(*(r))); \
		__osl_v; \
	})
#endif // endif

#if defined(BCM_BACKPLANE_TIMEOUT) || (defined(STB) && defined(__arm__))
	#define SELECT_BUS_WRITE(osh, mmap_op, bus_op) ({BCM_REFERENCE(osh); mmap_op;})
	#define SELECT_BUS_READ(osh, mmap_op, bus_op) ({BCM_REFERENCE(osh); bus_op;})
#else /* !BCM47XX_CA9 && !BCM_BACKPLANE_TIMEOUT && !(STB && __arm__) */
#if defined(BCMSDIO)
	#define SELECT_BUS_WRITE(osh, mmap_op, bus_op) if (((osl_pubinfo_t*)(osh))->mmbus) \
		mmap_op else bus_op
	#define SELECT_BUS_READ(osh, mmap_op, bus_op) (((osl_pubinfo_t*)(osh))->mmbus) ? \
		mmap_op : bus_op
#else
	#define SELECT_BUS_WRITE(osh, mmap_op, bus_op) ({BCM_REFERENCE(osh); mmap_op;})
	#define SELECT_BUS_READ(osh, mmap_op, bus_op) ({BCM_REFERENCE(osh); mmap_op;})
#endif // endif
#endif // endif

#define OSL_ERROR(bcmerror)	osl_error(bcmerror)
extern int osl_error(int bcmerror);

/* the largest reasonable packet buffer driver uses for ethernet MTU in bytes */
#define	PKTBUFSZ	2048   /* largest reasonable packet buffer, driver uses for ethernet MTU */

#define OSH_NULL   NULL

/*
 * BINOSL selects the slightly slower function-call-based binary compatible osl.
 * Macros expand to calls to functions defined in linux_osl.c .
 */
#include <linuxver.h>           /* use current 2.4.x calling conventions */
#include <linux/kernel.h>       /* for vsn/printf's */
#include <linux/string.h>       /* for mem*, str* */
extern uint64 osl_sysuptime_us(void);
#define OSL_SYSUPTIME()		((uint32)jiffies_to_msecs(jiffies))
#define OSL_SYSUPTIME_US()	osl_sysuptime_us()
extern uint64 osl_localtime_ns(void);
extern void osl_get_localtime(uint64 *sec, uint64 *usec);
extern uint64 osl_systztime_us(void);
#define OSL_LOCALTIME_NS()	osl_localtime_ns()
#define OSL_GET_LOCALTIME(sec, usec)	osl_get_localtime((sec), (usec))
#define OSL_SYSTZTIME_US()	osl_systztime_us()
#define	printf(fmt, args...)	printk(fmt , ## args)
#include <linux/kernel.h>	/* for vsn/printf's */
#include <linux/string.h>	/* for mem*, str* */
/* bcopy's: Linux kernel doesn't provide these (anymore) */
#define	bcopy_hw(src, dst, len)		memcpy((dst), (src), (len))
#define	bcopy_hw_async(src, dst, len)	memcpy((dst), (src), (len))
#define	bcopy_hw_poll_for_completion()
#define	bcopy(src, dst, len)	memcpy((dst), (src), (len))
#define	bcmp(b1, b2, len)	memcmp((b1), (b2), (len))
#define	bzero(b, len)		memset((b), '\0', (len))

/* register access macros */

#ifdef CONFIG_64BIT
/* readq is defined only for 64 bit platform */
#define R_REG(osh, r) (\
	SELECT_BUS_READ(osh, \
		({ \
			__typeof(*(r)) __osl_v = 0; \
			BCM_REFERENCE(osh);	\
			switch (sizeof(*(r))) { \
				case sizeof(uint8):	__osl_v = \
					readb((volatile uint8*)(r)); break; \
				case sizeof(uint16):	__osl_v = \
					readw((volatile uint16*)(r)); break; \
				case sizeof(uint32):	__osl_v = \
					readl((volatile uint32*)(r)); break; \
				case sizeof(uint64):	__osl_v = \
					readq((volatile uint64*)(r)); break; \
			} \
			__osl_v; \
		}), \
		OSL_READ_REG(osh, r)) \
)
#else /* !CONFIG_64BIT */
#define R_REG(osh, r) (\
	SELECT_BUS_READ(osh, \
		({ \
			__typeof(*(r)) __osl_v = 0; \
			switch (sizeof(*(r))) { \
				case sizeof(uint8):	__osl_v = \
					readb((volatile uint8*)(r)); break; \
				case sizeof(uint16):	__osl_v = \
					readw((volatile uint16*)(r)); break; \
				case sizeof(uint32):	__osl_v = \
					readl((volatile uint32*)(r)); break; \
			} \
			__osl_v; \
		}), \
		OSL_READ_REG(osh, r)) \
)
#endif /* CONFIG_64BIT */

#ifdef CONFIG_64BIT
/* writeq is defined only for 64 bit platform */
#define W_REG(osh, r, v) do { \
	SELECT_BUS_WRITE(osh, \
		switch (sizeof(*(r))) { \
			case sizeof(uint8):	writeb((uint8)(v), (volatile uint8*)(r)); break; \
			case sizeof(uint16):	writew((uint16)(v), (volatile uint16*)(r)); break; \
			case sizeof(uint32):	writel((uint32)(v), (volatile uint32*)(r)); break; \
			case sizeof(uint64):	writeq((uint64)(v), (volatile uint64*)(r)); break; \
		}, \
		(OSL_WRITE_REG(osh, r, v))); \
	} while (0)

#else /* !CONFIG_64BIT */
#define W_REG(osh, r, v) do { \
	SELECT_BUS_WRITE(osh, \
		switch (sizeof(*(r))) { \
			case sizeof(uint8):	writeb((uint8)(v), (volatile uint8*)(r)); break; \
			case sizeof(uint16):	writew((uint16)(v), (volatile uint16*)(r)); break; \
			case sizeof(uint32):	writel((uint32)(v), (volatile uint32*)(r)); break; \
		}, \
		(OSL_WRITE_REG(osh, r, v))); \
	} while (0)
#endif /* CONFIG_64BIT */

#define	AND_REG(osh, r, v)		W_REG(osh, (r), R_REG(osh, r) & (v))
#define	OR_REG(osh, r, v)		W_REG(osh, (r), R_REG(osh, r) | (v))

/* bcopy, bcmp, and bzero functions */
#define	bcopy(src, dst, len)	memcpy((dst), (src), (len))
#define	bcmp(b1, b2, len)	memcmp((b1), (b2), (len))
#define	bzero(b, len)		memset((b), '\0', (len))

/* uncached/cached virtual address */
#define OSL_UNCACHED(va)	((void *)va)
#define OSL_CACHED(va)		((void *)va)

#define OSL_PREF_RANGE_LD(va, sz) BCM_REFERENCE(va)
#define OSL_PREF_RANGE_ST(va, sz) BCM_REFERENCE(va)

/* get processor cycle count */
#if defined(__i386__)
#define	OSL_GETCYCLES(x)	rdtscl((x))
#else
#define OSL_GETCYCLES(x)	((x) = 0)
#endif // endif

/* dereference an address that may cause a bus exception */
#define	BUSPROBE(val, addr)	({ (val) = R_REG(NULL, (addr)); 0; })

/* map/unmap physical to virtual I/O */
#if !defined(CONFIG_MMC_MSM7X00A)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0))
#define	REG_MAP(pa, size)	ioremap_nocache((unsigned long)(pa), (unsigned long)(size))
#else
#define REG_MAP(pa, size)	ioremap((unsigned long)(pa), (unsigned long)(size))
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0) */
#else
#define REG_MAP(pa, size)       (void *)(0)
#endif /* !defined(CONFIG_MMC_MSM7X00A */
#define	REG_UNMAP(va)		iounmap((va))

/* shared (dma-able) memory access macros */
#define	R_SM(r)			*(r)
#define	W_SM(r, v)		(*(r) = (v))
#define	BZERO_SM(r, len)	memset((r), '\0', (len))

/* Because the non BINOSL implemenation of the PKT OSL routines are macros (for
 * performance reasons),  we need the Linux headers.
 */
#include <linuxver.h>		/* use current 2.4.x calling conventions */

#define OSL_RAND()		osl_rand()
extern uint32 osl_rand(void);

#define	DMA_FLUSH(osh, va, size, direction, p, dmah) \
	osl_dma_flush((osh), (va), (size), (direction), (p), (dmah))
#if !defined(BCM_SECURE_DMA)
#define	DMA_MAP(osh, va, size, direction, p, dmah) \
	osl_dma_map((osh), (va), (size), (direction), (p), (dmah))
#endif /* !(defined(BCM_SECURE_DMA)) */

#else /* ! BCMDRIVER */

/* ASSERT */
	#define ASSERT(exp)	do {} while (0)

/* MALLOC and MFREE */
#define MALLOC(o, l) malloc(l)
#define MFREE(o, p, l) free(p)
#include <stdlib.h>

/* str* and mem* functions */
#include <string.h>

/* *printf functions */
#include <stdio.h>

/* bcopy, bcmp, and bzero */
extern void bcopy(const void *src, void *dst, size_t len);
extern int bcmp(const void *b1, const void *b2, size_t len);
extern void bzero(void *b, size_t len);
#endif /* ! BCMDRIVER */

/* Current STB 7445D1 doesn't use ACP and it is non-coherrent.
 * Adding these dummy values for build apss only
 * When we revisit need to change these.
 */

#ifdef BCM_SECURE_DMA

#define	SECURE_DMA_MAP(osh, va, size, direction, p, dmah, pcma, offset) \
	osl_sec_dma_map((osh), (va), (size), (direction), (p), (dmah), (pcma), (offset))
#define	SECURE_DMA_DD_MAP(osh, va, size, direction, p, dmah) \
	osl_sec_dma_dd_map((osh), (va), (size), (direction), (p), (dmah))
#define	SECURE_DMA_MAP_TXMETA(osh, va, size, direction, p, dmah, pcma) \
	osl_sec_dma_map_txmeta((osh), (va), (size), (direction), (p), (dmah), (pcma))
#define	SECURE_DMA_UNMAP(osh, pa, size, direction, p, dmah, pcma, offset) \
	osl_sec_dma_unmap((osh), (pa), (size), (direction), (p), (dmah), (pcma), (offset))
#define	SECURE_DMA_UNMAP_ALL(osh, pcma) \
	osl_sec_dma_unmap_all((osh), (pcma))

#define DMA_MAP(osh, va, size, direction, p, dmah)

typedef struct sec_cma_info {
	struct sec_mem_elem *sec_alloc_list;
	struct sec_mem_elem *sec_alloc_list_tail;
} sec_cma_info_t;

#if defined(__ARM_ARCH_7A__)
#define CMA_BUFSIZE_4K	4096
#define CMA_BUFSIZE_2K	2048
#define CMA_BUFSIZE_512	512

#define	CMA_BUFNUM		2048
#define SEC_CMA_COHERENT_BLK 0x8000 /* 32768 */
#define SEC_CMA_COHERENT_MAX 278
#define CMA_DMA_DESC_MEMBLOCK	(SEC_CMA_COHERENT_BLK * SEC_CMA_COHERENT_MAX)
#define CMA_DMA_DATA_MEMBLOCK	(CMA_BUFSIZE_4K*CMA_BUFNUM)
#define	CMA_MEMBLOCK		(CMA_DMA_DESC_MEMBLOCK + CMA_DMA_DATA_MEMBLOCK)
#define CONT_REGION	0x02		/* Region CMA */
#else
#define CONT_REGION	0x00		/* To access the MIPs mem, Not yet... */
#endif /* !defined __ARM_ARCH_7A__ */

#define SEC_DMA_ALIGN	(1<<16)
typedef struct sec_mem_elem {
	size_t			size;
	int				direction;
	phys_addr_t		pa_cma;     /**< physical  address */
	void			*va;        /**< virtual address of driver pkt */
	dma_addr_t		dma_handle; /**< bus address assign by linux */
	void			*vac;       /**< virtual address of cma buffer */
	struct page *pa_cma_page;	/* phys to page address */
	struct	sec_mem_elem	*next;
} sec_mem_elem_t;

extern dma_addr_t osl_sec_dma_map(osl_t *osh, void *va, uint size, int direction, void *p,
	hnddma_seg_map_t *dmah, void *ptr_cma_info, uint offset);
extern dma_addr_t osl_sec_dma_dd_map(osl_t *osh, void *va, uint size, int direction, void *p,
	hnddma_seg_map_t *dmah);
extern dma_addr_t osl_sec_dma_map_txmeta(osl_t *osh, void *va, uint size,
  int direction, void *p, hnddma_seg_map_t *dmah, void *ptr_cma_info);
extern void osl_sec_dma_unmap(osl_t *osh, dma_addr_t dma_handle, uint size, int direction,
	void *p, hnddma_seg_map_t *map, void *ptr_cma_info, uint offset);
extern void osl_sec_dma_unmap_all(osl_t *osh, void *ptr_cma_info);

#endif /* BCM_SECURE_DMA */

typedef struct sk_buff_head PKT_LIST;
#define PKTLIST_INIT(x)		skb_queue_head_init((x))
#define PKTLIST_ENQ(x, y)	skb_queue_head((struct sk_buff_head *)(x), (struct sk_buff *)(y))
#define PKTLIST_DEQ(x)		skb_dequeue((struct sk_buff_head *)(x))
#define PKTLIST_UNLINK(x, y)	skb_unlink((struct sk_buff *)(y), (struct sk_buff_head *)(x))
#define PKTLIST_FINI(x)		skb_queue_purge((struct sk_buff_head *)(x))

#ifndef _linuxver_h_
typedef struct timer_list_compat timer_list_compat_t;
#endif /* _linuxver_h_ */
typedef struct osl_timer {
	timer_list_compat_t *timer;
	bool   set;
} osl_timer_t;

typedef void (*linux_timer_fn)(ulong arg);

extern osl_timer_t * osl_timer_init(osl_t *osh, const char *name, void (*fn)(void *arg), void *arg);
extern void osl_timer_add(osl_t *osh, osl_timer_t *t, uint32 ms, bool periodic);
extern void osl_timer_update(osl_t *osh, osl_timer_t *t, uint32 ms, bool periodic);
extern bool osl_timer_del(osl_t *osh, osl_timer_t *t);

typedef atomic_t osl_atomic_t;
#define OSL_ATOMIC_SET(osh, v, x)	atomic_set(v, x)
#define OSL_ATOMIC_INIT(osh, v)		atomic_set(v, 0)
#define OSL_ATOMIC_INC(osh, v)		atomic_inc(v)
#define OSL_ATOMIC_INC_RETURN(osh, v)	atomic_inc_return(v)
#define OSL_ATOMIC_DEC(osh, v)		atomic_dec(v)
#define OSL_ATOMIC_DEC_RETURN(osh, v)	atomic_dec_return(v)
#define OSL_ATOMIC_READ(osh, v)		atomic_read(v)
#define OSL_ATOMIC_ADD(osh, v, x)	atomic_add(v, x)

#ifndef atomic_set_mask
#define OSL_ATOMIC_OR(osh, v, x)	atomic_or(x, v)
#define OSL_ATOMIC_AND(osh, v, x)	atomic_and(x, v)
#else
#define OSL_ATOMIC_OR(osh, v, x)	atomic_set_mask(x, v)
#define OSL_ATOMIC_AND(osh, v, x)	atomic_clear_mask(~x, v)
#endif // endif

#include <linux/rbtree.h>

typedef struct rb_node osl_rb_node_t;
typedef struct rb_root osl_rb_root_t;

#define OSL_RB_ENTRY(ptr, type, member)		rb_entry(ptr, type, member)
#define OSL_RB_INSERT_COLOR(root, node)		rb_insert_color(root, node)
#define OSL_RB_ERASE(node, root)		rb_erase(node, root)
#define OSL_RB_FIRST(root)			rb_first(root)
#define OSL_RB_LAST(root)			rb_last(root)
#define OSL_RB_LINK_NODE(node, parent, rb_link) \
	rb_link_node(node, parent, rb_link)

extern void *osl_spin_lock_init(osl_t *osh);
extern void osl_spin_lock_deinit(osl_t *osh, void *lock);
extern unsigned long osl_spin_lock(void *lock);
extern void osl_spin_unlock(void *lock, unsigned long flags);
#endif	/* _linux_osl_h_ */

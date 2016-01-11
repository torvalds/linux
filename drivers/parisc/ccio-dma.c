/*
** ccio-dma.c:
**	DMA management routines for first generation cache-coherent machines.
**	Program U2/Uturn in "Virtual Mode" and use the I/O MMU.
**
**	(c) Copyright 2000 Grant Grundler
**	(c) Copyright 2000 Ryan Bradetich
**	(c) Copyright 2000 Hewlett-Packard Company
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
**
**  "Real Mode" operation refers to U2/Uturn chip operation.
**  U2/Uturn were designed to perform coherency checks w/o using
**  the I/O MMU - basically what x86 does.
**
**  Philipp Rumpf has a "Real Mode" driver for PCX-W machines at:
**      CVSROOT=:pserver:anonymous@198.186.203.37:/cvsroot/linux-parisc
**      cvs -z3 co linux/arch/parisc/kernel/dma-rm.c
**
**  I've rewritten his code to work under TPG's tree. See ccio-rm-dma.c.
**
**  Drawbacks of using Real Mode are:
**	o outbound DMA is slower - U2 won't prefetch data (GSC+ XQL signal).
**      o Inbound DMA less efficient - U2 can't use DMA_FAST attribute.
**	o Ability to do scatter/gather in HW is lost.
**	o Doesn't work under PCX-U/U+ machines since they didn't follow
**        the coherency design originally worked out. Only PCX-W does.
*/

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/reboot.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/iommu-helper.h>
#include <linux/export.h>

#include <asm/byteorder.h>
#include <asm/cache.h>		/* for L1_CACHE_BYTES */
#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/hardware.h>       /* for register_module() */
#include <asm/parisc-device.h>

/* 
** Choose "ccio" since that's what HP-UX calls it.
** Make it easier for folks to migrate from one to the other :^)
*/
#define MODULE_NAME "ccio"

#undef DEBUG_CCIO_RES
#undef DEBUG_CCIO_RUN
#undef DEBUG_CCIO_INIT
#undef DEBUG_CCIO_RUN_SG

#ifdef CONFIG_PROC_FS
/* depends on proc fs support. But costs CPU performance. */
#undef CCIO_COLLECT_STATS
#endif

#include <asm/runway.h>		/* for proc_runway_root */

#ifdef DEBUG_CCIO_INIT
#define DBG_INIT(x...)  printk(x)
#else
#define DBG_INIT(x...)
#endif

#ifdef DEBUG_CCIO_RUN
#define DBG_RUN(x...)   printk(x)
#else
#define DBG_RUN(x...)
#endif

#ifdef DEBUG_CCIO_RES
#define DBG_RES(x...)   printk(x)
#else
#define DBG_RES(x...)
#endif

#ifdef DEBUG_CCIO_RUN_SG
#define DBG_RUN_SG(x...) printk(x)
#else
#define DBG_RUN_SG(x...)
#endif

#define CCIO_INLINE	inline
#define WRITE_U32(value, addr) __raw_writel(value, addr)
#define READ_U32(addr) __raw_readl(addr)

#define U2_IOA_RUNWAY 0x580
#define U2_BC_GSC     0x501
#define UTURN_IOA_RUNWAY 0x581
#define UTURN_BC_GSC     0x502

#define IOA_NORMAL_MODE      0x00020080 /* IO_CONTROL to turn on CCIO        */
#define CMD_TLB_DIRECT_WRITE 35         /* IO_COMMAND for I/O TLB Writes     */
#define CMD_TLB_PURGE        33         /* IO_COMMAND to Purge I/O TLB entry */

struct ioa_registers {
        /* Runway Supervisory Set */
        int32_t    unused1[12];
        uint32_t   io_command;             /* Offset 12 */
        uint32_t   io_status;              /* Offset 13 */
        uint32_t   io_control;             /* Offset 14 */
        int32_t    unused2[1];

        /* Runway Auxiliary Register Set */
        uint32_t   io_err_resp;            /* Offset  0 */
        uint32_t   io_err_info;            /* Offset  1 */
        uint32_t   io_err_req;             /* Offset  2 */
        uint32_t   io_err_resp_hi;         /* Offset  3 */
        uint32_t   io_tlb_entry_m;         /* Offset  4 */
        uint32_t   io_tlb_entry_l;         /* Offset  5 */
        uint32_t   unused3[1];
        uint32_t   io_pdir_base;           /* Offset  7 */
        uint32_t   io_io_low_hv;           /* Offset  8 */
        uint32_t   io_io_high_hv;          /* Offset  9 */
        uint32_t   unused4[1];
        uint32_t   io_chain_id_mask;       /* Offset 11 */
        uint32_t   unused5[2];
        uint32_t   io_io_low;              /* Offset 14 */
        uint32_t   io_io_high;             /* Offset 15 */
};

/*
** IOA Registers
** -------------
**
** Runway IO_CONTROL Register (+0x38)
** 
** The Runway IO_CONTROL register controls the forwarding of transactions.
**
** | 0  ...  13  |  14 15 | 16 ... 21 | 22 | 23 24 |  25 ... 31 |
** |    HV       |   TLB  |  reserved | HV | mode  |  reserved  |
**
** o mode field indicates the address translation of transactions
**   forwarded from Runway to GSC+:
**       Mode Name     Value        Definition
**       Off (default)   0          Opaque to matching addresses.
**       Include         1          Transparent for matching addresses.
**       Peek            3          Map matching addresses.
**
**       + "Off" mode: Runway transactions which match the I/O range
**         specified by the IO_IO_LOW/IO_IO_HIGH registers will be ignored.
**       + "Include" mode: all addresses within the I/O range specified
**         by the IO_IO_LOW and IO_IO_HIGH registers are transparently
**         forwarded. This is the I/O Adapter's normal operating mode.
**       + "Peek" mode: used during system configuration to initialize the
**         GSC+ bus. Runway Write_Shorts in the address range specified by
**         IO_IO_LOW and IO_IO_HIGH are forwarded through the I/O Adapter
**         *AND* the GSC+ address is remapped to the Broadcast Physical
**         Address space by setting the 14 high order address bits of the
**         32 bit GSC+ address to ones.
**
** o TLB field affects transactions which are forwarded from GSC+ to Runway.
**   "Real" mode is the poweron default.
** 
**   TLB Mode  Value  Description
**   Real        0    No TLB translation. Address is directly mapped and the
**                    virtual address is composed of selected physical bits.
**   Error       1    Software fills the TLB manually.
**   Normal      2    IOA fetches IO TLB misses from IO PDIR (in host memory).
**
**
** IO_IO_LOW_HV	  +0x60 (HV dependent)
** IO_IO_HIGH_HV  +0x64 (HV dependent)
** IO_IO_LOW      +0x78	(Architected register)
** IO_IO_HIGH     +0x7c	(Architected register)
**
** IO_IO_LOW and IO_IO_HIGH set the lower and upper bounds of the
** I/O Adapter address space, respectively.
**
** 0  ... 7 | 8 ... 15 |  16   ...   31 |
** 11111111 | 11111111 |      address   |
**
** Each LOW/HIGH pair describes a disjoint address space region.
** (2 per GSC+ port). Each incoming Runway transaction address is compared
** with both sets of LOW/HIGH registers. If the address is in the range
** greater than or equal to IO_IO_LOW and less than IO_IO_HIGH the transaction
** for forwarded to the respective GSC+ bus.
** Specify IO_IO_LOW equal to or greater than IO_IO_HIGH to avoid specifying
** an address space region.
**
** In order for a Runway address to reside within GSC+ extended address space:
**	Runway Address [0:7]    must identically compare to 8'b11111111
**	Runway Address [8:11]   must be equal to IO_IO_LOW(_HV)[16:19]
** 	Runway Address [12:23]  must be greater than or equal to
**	           IO_IO_LOW(_HV)[20:31] and less than IO_IO_HIGH(_HV)[20:31].
**	Runway Address [24:39]  is not used in the comparison.
**
** When the Runway transaction is forwarded to GSC+, the GSC+ address is
** as follows:
**	GSC+ Address[0:3]	4'b1111
**	GSC+ Address[4:29]	Runway Address[12:37]
**	GSC+ Address[30:31]	2'b00
**
** All 4 Low/High registers must be initialized (by PDC) once the lower bus
** is interrogated and address space is defined. The operating system will
** modify the architectural IO_IO_LOW and IO_IO_HIGH registers following
** the PDC initialization.  However, the hardware version dependent IO_IO_LOW
** and IO_IO_HIGH registers should not be subsequently altered by the OS.
** 
** Writes to both sets of registers will take effect immediately, bypassing
** the queues, which ensures that subsequent Runway transactions are checked
** against the updated bounds values. However reads are queued, introducing
** the possibility of a read being bypassed by a subsequent write to the same
** register. This sequence can be avoided by having software wait for read
** returns before issuing subsequent writes.
*/

struct ioc {
	struct ioa_registers __iomem *ioc_regs;  /* I/O MMU base address */
	u8  *res_map;	                /* resource map, bit == pdir entry */
	u64 *pdir_base;	                /* physical base address */
	u32 pdir_size; 			/* bytes, function of IOV Space size */
	u32 res_hint;	                /* next available IOVP - 
					   circular search */
	u32 res_size;		    	/* size of resource map in bytes */
	spinlock_t res_lock;

#ifdef CCIO_COLLECT_STATS
#define CCIO_SEARCH_SAMPLE 0x100
	unsigned long avg_search[CCIO_SEARCH_SAMPLE];
	unsigned long avg_idx;		  /* current index into avg_search */
	unsigned long used_pages;
	unsigned long msingle_calls;
	unsigned long msingle_pages;
	unsigned long msg_calls;
	unsigned long msg_pages;
	unsigned long usingle_calls;
	unsigned long usingle_pages;
	unsigned long usg_calls;
	unsigned long usg_pages;
#endif
	unsigned short cujo20_bug;

	/* STUFF We don't need in performance path */
	u32 chainid_shift; 		/* specify bit location of chain_id */
	struct ioc *next;		/* Linked list of discovered iocs */
	const char *name;		/* device name from firmware */
	unsigned int hw_path;           /* the hardware path this ioc is associatd with */
	struct pci_dev *fake_pci_dev;   /* the fake pci_dev for non-pci devs */
	struct resource mmio_region[2]; /* The "routed" MMIO regions */
};

static struct ioc *ioc_list;
static int ioc_count;

/**************************************************************
*
*   I/O Pdir Resource Management
*
*   Bits set in the resource map are in use.
*   Each bit can represent a number of pages.
*   LSbs represent lower addresses (IOVA's).
*
*   This was was copied from sba_iommu.c. Don't try to unify
*   the two resource managers unless a way to have different
*   allocation policies is also adjusted. We'd like to avoid
*   I/O TLB thrashing by having resource allocation policy
*   match the I/O TLB replacement policy.
*
***************************************************************/
#define IOVP_SIZE PAGE_SIZE
#define IOVP_SHIFT PAGE_SHIFT
#define IOVP_MASK PAGE_MASK

/* Convert from IOVP to IOVA and vice versa. */
#define CCIO_IOVA(iovp,offset) ((iovp) | (offset))
#define CCIO_IOVP(iova) ((iova) & IOVP_MASK)

#define PDIR_INDEX(iovp)    ((iovp)>>IOVP_SHIFT)
#define MKIOVP(pdir_idx)    ((long)(pdir_idx) << IOVP_SHIFT)
#define MKIOVA(iovp,offset) (dma_addr_t)((long)iovp | (long)offset)

/*
** Don't worry about the 150% average search length on a miss.
** If the search wraps around, and passes the res_hint, it will
** cause the kernel to panic anyhow.
*/
#define CCIO_SEARCH_LOOP(ioc, res_idx, mask, size)  \
       for(; res_ptr < res_end; ++res_ptr) { \
		int ret;\
		unsigned int idx;\
		idx = (unsigned int)((unsigned long)res_ptr - (unsigned long)ioc->res_map); \
		ret = iommu_is_span_boundary(idx << 3, pages_needed, 0, boundary_size);\
		if ((0 == (*res_ptr & mask)) && !ret) { \
			*res_ptr |= mask; \
			res_idx = idx;\
			ioc->res_hint = res_idx + (size >> 3); \
			goto resource_found; \
		} \
	}

#define CCIO_FIND_FREE_MAPPING(ioa, res_idx, mask, size) \
       u##size *res_ptr = (u##size *)&((ioc)->res_map[ioa->res_hint & ~((size >> 3) - 1)]); \
       u##size *res_end = (u##size *)&(ioc)->res_map[ioa->res_size]; \
       CCIO_SEARCH_LOOP(ioc, res_idx, mask, size); \
       res_ptr = (u##size *)&(ioc)->res_map[0]; \
       CCIO_SEARCH_LOOP(ioa, res_idx, mask, size);

/*
** Find available bit in this ioa's resource map.
** Use a "circular" search:
**   o Most IOVA's are "temporary" - avg search time should be small.
** o keep a history of what happened for debugging
** o KISS.
**
** Perf optimizations:
** o search for log2(size) bits at a time.
** o search for available resource bits using byte/word/whatever.
** o use different search for "large" (eg > 4 pages) or "very large"
**   (eg > 16 pages) mappings.
*/

/**
 * ccio_alloc_range - Allocate pages in the ioc's resource map.
 * @ioc: The I/O Controller.
 * @pages_needed: The requested number of pages to be mapped into the
 * I/O Pdir...
 *
 * This function searches the resource map of the ioc to locate a range
 * of available pages for the requested size.
 */
static int
ccio_alloc_range(struct ioc *ioc, struct device *dev, size_t size)
{
	unsigned int pages_needed = size >> IOVP_SHIFT;
	unsigned int res_idx;
	unsigned long boundary_size;
#ifdef CCIO_COLLECT_STATS
	unsigned long cr_start = mfctl(16);
#endif
	
	BUG_ON(pages_needed == 0);
	BUG_ON((pages_needed * IOVP_SIZE) > DMA_CHUNK_SIZE);
     
	DBG_RES("%s() size: %d pages_needed %d\n", 
		__func__, size, pages_needed);

	/*
	** "seek and ye shall find"...praying never hurts either...
	** ggg sacrifices another 710 to the computer gods.
	*/

	boundary_size = ALIGN((unsigned long long)dma_get_seg_boundary(dev) + 1,
			      1ULL << IOVP_SHIFT) >> IOVP_SHIFT;

	if (pages_needed <= 8) {
		/*
		 * LAN traffic will not thrash the TLB IFF the same NIC
		 * uses 8 adjacent pages to map separate payload data.
		 * ie the same byte in the resource bit map.
		 */
#if 0
		/* FIXME: bit search should shift it's way through
		 * an unsigned long - not byte at a time. As it is now,
		 * we effectively allocate this byte to this mapping.
		 */
		unsigned long mask = ~(~0UL >> pages_needed);
		CCIO_FIND_FREE_MAPPING(ioc, res_idx, mask, 8);
#else
		CCIO_FIND_FREE_MAPPING(ioc, res_idx, 0xff, 8);
#endif
	} else if (pages_needed <= 16) {
		CCIO_FIND_FREE_MAPPING(ioc, res_idx, 0xffff, 16);
	} else if (pages_needed <= 32) {
		CCIO_FIND_FREE_MAPPING(ioc, res_idx, ~(unsigned int)0, 32);
#ifdef __LP64__
	} else if (pages_needed <= 64) {
		CCIO_FIND_FREE_MAPPING(ioc, res_idx, ~0UL, 64);
#endif
	} else {
		panic("%s: %s() Too many pages to map. pages_needed: %u\n",
		       __FILE__,  __func__, pages_needed);
	}

	panic("%s: %s() I/O MMU is out of mapping resources.\n", __FILE__,
	      __func__);
	
resource_found:
	
	DBG_RES("%s() res_idx %d res_hint: %d\n",
		__func__, res_idx, ioc->res_hint);

#ifdef CCIO_COLLECT_STATS
	{
		unsigned long cr_end = mfctl(16);
		unsigned long tmp = cr_end - cr_start;
		/* check for roll over */
		cr_start = (cr_end < cr_start) ?  -(tmp) : (tmp);
	}
	ioc->avg_search[ioc->avg_idx++] = cr_start;
	ioc->avg_idx &= CCIO_SEARCH_SAMPLE - 1;
	ioc->used_pages += pages_needed;
#endif
	/* 
	** return the bit address.
	*/
	return res_idx << 3;
}

#define CCIO_FREE_MAPPINGS(ioc, res_idx, mask, size) \
        u##size *res_ptr = (u##size *)&((ioc)->res_map[res_idx]); \
        BUG_ON((*res_ptr & mask) != mask); \
        *res_ptr &= ~(mask);

/**
 * ccio_free_range - Free pages from the ioc's resource map.
 * @ioc: The I/O Controller.
 * @iova: The I/O Virtual Address.
 * @pages_mapped: The requested number of pages to be freed from the
 * I/O Pdir.
 *
 * This function frees the resouces allocated for the iova.
 */
static void
ccio_free_range(struct ioc *ioc, dma_addr_t iova, unsigned long pages_mapped)
{
	unsigned long iovp = CCIO_IOVP(iova);
	unsigned int res_idx = PDIR_INDEX(iovp) >> 3;

	BUG_ON(pages_mapped == 0);
	BUG_ON((pages_mapped * IOVP_SIZE) > DMA_CHUNK_SIZE);
	BUG_ON(pages_mapped > BITS_PER_LONG);

	DBG_RES("%s():  res_idx: %d pages_mapped %d\n", 
		__func__, res_idx, pages_mapped);

#ifdef CCIO_COLLECT_STATS
	ioc->used_pages -= pages_mapped;
#endif

	if(pages_mapped <= 8) {
#if 0
		/* see matching comments in alloc_range */
		unsigned long mask = ~(~0UL >> pages_mapped);
		CCIO_FREE_MAPPINGS(ioc, res_idx, mask, 8);
#else
		CCIO_FREE_MAPPINGS(ioc, res_idx, 0xffUL, 8);
#endif
	} else if(pages_mapped <= 16) {
		CCIO_FREE_MAPPINGS(ioc, res_idx, 0xffffUL, 16);
	} else if(pages_mapped <= 32) {
		CCIO_FREE_MAPPINGS(ioc, res_idx, ~(unsigned int)0, 32);
#ifdef __LP64__
	} else if(pages_mapped <= 64) {
		CCIO_FREE_MAPPINGS(ioc, res_idx, ~0UL, 64);
#endif
	} else {
		panic("%s:%s() Too many pages to unmap.\n", __FILE__,
		      __func__);
	}
}

/****************************************************************
**
**          CCIO dma_ops support routines
**
*****************************************************************/

typedef unsigned long space_t;
#define KERNEL_SPACE 0

/*
** DMA "Page Type" and Hints 
** o if SAFE_DMA isn't set, mapping is for FAST_DMA. SAFE_DMA should be
**   set for subcacheline DMA transfers since we don't want to damage the
**   other part of a cacheline.
** o SAFE_DMA must be set for "memory" allocated via pci_alloc_consistent().
**   This bit tells U2 to do R/M/W for partial cachelines. "Streaming"
**   data can avoid this if the mapping covers full cache lines.
** o STOP_MOST is needed for atomicity across cachelines.
**   Apparently only "some EISA devices" need this.
**   Using CONFIG_ISA is hack. Only the IOA with EISA under it needs
**   to use this hint iff the EISA devices needs this feature.
**   According to the U2 ERS, STOP_MOST enabled pages hurt performance.
** o PREFETCH should *not* be set for cases like Multiple PCI devices
**   behind GSCtoPCI (dino) bus converter. Only one cacheline per GSC
**   device can be fetched and multiply DMA streams will thrash the
**   prefetch buffer and burn memory bandwidth. See 6.7.3 "Prefetch Rules
**   and Invalidation of Prefetch Entries".
**
** FIXME: the default hints need to be per GSC device - not global.
** 
** HP-UX dorks: linux device driver programming model is totally different
**    than HP-UX's. HP-UX always sets HINT_PREFETCH since it's drivers
**    do special things to work on non-coherent platforms...linux has to
**    be much more careful with this.
*/
#define IOPDIR_VALID    0x01UL
#define HINT_SAFE_DMA   0x02UL	/* used for pci_alloc_consistent() pages */
#ifdef CONFIG_EISA
#define HINT_STOP_MOST  0x04UL	/* LSL support */
#else
#define HINT_STOP_MOST  0x00UL	/* only needed for "some EISA devices" */
#endif
#define HINT_UDPATE_ENB 0x08UL  /* not used/supported by U2 */
#define HINT_PREFETCH   0x10UL	/* for outbound pages which are not SAFE */


/*
** Use direction (ie PCI_DMA_TODEVICE) to pick hint.
** ccio_alloc_consistent() depends on this to get SAFE_DMA
** when it passes in BIDIRECTIONAL flag.
*/
static u32 hint_lookup[] = {
	[PCI_DMA_BIDIRECTIONAL]	= HINT_STOP_MOST | HINT_SAFE_DMA | IOPDIR_VALID,
	[PCI_DMA_TODEVICE]	= HINT_STOP_MOST | HINT_PREFETCH | IOPDIR_VALID,
	[PCI_DMA_FROMDEVICE]	= HINT_STOP_MOST | IOPDIR_VALID,
};

/**
 * ccio_io_pdir_entry - Initialize an I/O Pdir.
 * @pdir_ptr: A pointer into I/O Pdir.
 * @sid: The Space Identifier.
 * @vba: The virtual address.
 * @hints: The DMA Hint.
 *
 * Given a virtual address (vba, arg2) and space id, (sid, arg1),
 * load the I/O PDIR entry pointed to by pdir_ptr (arg0). Each IO Pdir
 * entry consists of 8 bytes as shown below (MSB == bit 0):
 *
 *
 * WORD 0:
 * +------+----------------+-----------------------------------------------+
 * | Phys | Virtual Index  |               Phys                            |
 * | 0:3  |     0:11       |               4:19                            |
 * |4 bits|   12 bits      |              16 bits                          |
 * +------+----------------+-----------------------------------------------+
 * WORD 1:
 * +-----------------------+-----------------------------------------------+
 * |      Phys    |  Rsvd  | Prefetch |Update |Rsvd  |Lock  |Safe  |Valid  |
 * |     20:39    |        | Enable   |Enable |      |Enable|DMA   |       |
 * |    20 bits   | 5 bits | 1 bit    |1 bit  |2 bits|1 bit |1 bit |1 bit  |
 * +-----------------------+-----------------------------------------------+
 *
 * The virtual index field is filled with the results of the LCI
 * (Load Coherence Index) instruction.  The 8 bits used for the virtual
 * index are bits 12:19 of the value returned by LCI.
 */ 
static void CCIO_INLINE
ccio_io_pdir_entry(u64 *pdir_ptr, space_t sid, unsigned long vba,
		   unsigned long hints)
{
	register unsigned long pa;
	register unsigned long ci; /* coherent index */

	/* We currently only support kernel addresses */
	BUG_ON(sid != KERNEL_SPACE);

	mtsp(sid,1);

	/*
	** WORD 1 - low order word
	** "hints" parm includes the VALID bit!
	** "dep" clobbers the physical address offset bits as well.
	*/
	pa = virt_to_phys(vba);
	asm volatile("depw  %1,31,12,%0" : "+r" (pa) : "r" (hints));
	((u32 *)pdir_ptr)[1] = (u32) pa;

	/*
	** WORD 0 - high order word
	*/

#ifdef __LP64__
	/*
	** get bits 12:15 of physical address
	** shift bits 16:31 of physical address
	** and deposit them
	*/
	asm volatile ("extrd,u %1,15,4,%0" : "=r" (ci) : "r" (pa));
	asm volatile ("extrd,u %1,31,16,%0" : "+r" (pa) : "r" (pa));
	asm volatile ("depd  %1,35,4,%0" : "+r" (pa) : "r" (ci));
#else
	pa = 0;
#endif
	/*
	** get CPU coherency index bits
	** Grab virtual index [0:11]
	** Deposit virt_idx bits into I/O PDIR word
	*/
	asm volatile ("lci %%r0(%%sr1, %1), %0" : "=r" (ci) : "r" (vba));
	asm volatile ("extru %1,19,12,%0" : "+r" (ci) : "r" (ci));
	asm volatile ("depw  %1,15,12,%0" : "+r" (pa) : "r" (ci));

	((u32 *)pdir_ptr)[0] = (u32) pa;


	/* FIXME: PCX_W platforms don't need FDC/SYNC. (eg C360)
	**        PCX-U/U+ do. (eg C200/C240)
	**        PCX-T'? Don't know. (eg C110 or similar K-class)
	**
	** See PDC_MODEL/option 0/SW_CAP word for "Non-coherent IO-PDIR bit".
	** Hopefully we can patch (NOP) these out at boot time somehow.
	**
	** "Since PCX-U employs an offset hash that is incompatible with
	** the real mode coherence index generation of U2, the PDIR entry
	** must be flushed to memory to retain coherence."
	*/
	asm volatile("fdc %%r0(%0)" : : "r" (pdir_ptr));
	asm volatile("sync");
}

/**
 * ccio_clear_io_tlb - Remove stale entries from the I/O TLB.
 * @ioc: The I/O Controller.
 * @iovp: The I/O Virtual Page.
 * @byte_cnt: The requested number of bytes to be freed from the I/O Pdir.
 *
 * Purge invalid I/O PDIR entries from the I/O TLB.
 *
 * FIXME: Can we change the byte_cnt to pages_mapped?
 */
static CCIO_INLINE void
ccio_clear_io_tlb(struct ioc *ioc, dma_addr_t iovp, size_t byte_cnt)
{
	u32 chain_size = 1 << ioc->chainid_shift;

	iovp &= IOVP_MASK;	/* clear offset bits, just want pagenum */
	byte_cnt += chain_size;

	while(byte_cnt > chain_size) {
		WRITE_U32(CMD_TLB_PURGE | iovp, &ioc->ioc_regs->io_command);
		iovp += chain_size;
		byte_cnt -= chain_size;
	}
}

/**
 * ccio_mark_invalid - Mark the I/O Pdir entries invalid.
 * @ioc: The I/O Controller.
 * @iova: The I/O Virtual Address.
 * @byte_cnt: The requested number of bytes to be freed from the I/O Pdir.
 *
 * Mark the I/O Pdir entries invalid and blow away the corresponding I/O
 * TLB entries.
 *
 * FIXME: at some threshold it might be "cheaper" to just blow
 *        away the entire I/O TLB instead of individual entries.
 *
 * FIXME: Uturn has 256 TLB entries. We don't need to purge every
 *        PDIR entry - just once for each possible TLB entry.
 *        (We do need to maker I/O PDIR entries invalid regardless).
 *
 * FIXME: Can we change byte_cnt to pages_mapped?
 */ 
static CCIO_INLINE void
ccio_mark_invalid(struct ioc *ioc, dma_addr_t iova, size_t byte_cnt)
{
	u32 iovp = (u32)CCIO_IOVP(iova);
	size_t saved_byte_cnt;

	/* round up to nearest page size */
	saved_byte_cnt = byte_cnt = ALIGN(byte_cnt, IOVP_SIZE);

	while(byte_cnt > 0) {
		/* invalidate one page at a time */
		unsigned int idx = PDIR_INDEX(iovp);
		char *pdir_ptr = (char *) &(ioc->pdir_base[idx]);

		BUG_ON(idx >= (ioc->pdir_size / sizeof(u64)));
		pdir_ptr[7] = 0;	/* clear only VALID bit */ 
		/*
		** FIXME: PCX_W platforms don't need FDC/SYNC. (eg C360)
		**   PCX-U/U+ do. (eg C200/C240)
		** See PDC_MODEL/option 0/SW_CAP for "Non-coherent IO-PDIR bit".
		**
		** Hopefully someone figures out how to patch (NOP) the
		** FDC/SYNC out at boot time.
		*/
		asm volatile("fdc %%r0(%0)" : : "r" (pdir_ptr[7]));

		iovp     += IOVP_SIZE;
		byte_cnt -= IOVP_SIZE;
	}

	asm volatile("sync");
	ccio_clear_io_tlb(ioc, CCIO_IOVP(iova), saved_byte_cnt);
}

/****************************************************************
**
**          CCIO dma_ops
**
*****************************************************************/

/**
 * ccio_dma_supported - Verify the IOMMU supports the DMA address range.
 * @dev: The PCI device.
 * @mask: A bit mask describing the DMA address range of the device.
 */
static int 
ccio_dma_supported(struct device *dev, u64 mask)
{
	if(dev == NULL) {
		printk(KERN_ERR MODULE_NAME ": EISA/ISA/et al not supported\n");
		BUG();
		return 0;
	}

	/* only support 32-bit devices (ie PCI/GSC) */
	return (int)(mask == 0xffffffffUL);
}

/**
 * ccio_map_single - Map an address range into the IOMMU.
 * @dev: The PCI device.
 * @addr: The start address of the DMA region.
 * @size: The length of the DMA region.
 * @direction: The direction of the DMA transaction (to/from device).
 *
 * This function implements the pci_map_single function.
 */
static dma_addr_t 
ccio_map_single(struct device *dev, void *addr, size_t size,
		enum dma_data_direction direction)
{
	int idx;
	struct ioc *ioc;
	unsigned long flags;
	dma_addr_t iovp;
	dma_addr_t offset;
	u64 *pdir_start;
	unsigned long hint = hint_lookup[(int)direction];

	BUG_ON(!dev);
	ioc = GET_IOC(dev);

	BUG_ON(size <= 0);

	/* save offset bits */
	offset = ((unsigned long) addr) & ~IOVP_MASK;

	/* round up to nearest IOVP_SIZE */
	size = ALIGN(size + offset, IOVP_SIZE);
	spin_lock_irqsave(&ioc->res_lock, flags);

#ifdef CCIO_COLLECT_STATS
	ioc->msingle_calls++;
	ioc->msingle_pages += size >> IOVP_SHIFT;
#endif

	idx = ccio_alloc_range(ioc, dev, size);
	iovp = (dma_addr_t)MKIOVP(idx);

	pdir_start = &(ioc->pdir_base[idx]);

	DBG_RUN("%s() 0x%p -> 0x%lx size: %0x%x\n",
		__func__, addr, (long)iovp | offset, size);

	/* If not cacheline aligned, force SAFE_DMA on the whole mess */
	if((size % L1_CACHE_BYTES) || ((unsigned long)addr % L1_CACHE_BYTES))
		hint |= HINT_SAFE_DMA;

	while(size > 0) {
		ccio_io_pdir_entry(pdir_start, KERNEL_SPACE, (unsigned long)addr, hint);

		DBG_RUN(" pdir %p %08x%08x\n",
			pdir_start,
			(u32) (((u32 *) pdir_start)[0]),
			(u32) (((u32 *) pdir_start)[1]));
		++pdir_start;
		addr += IOVP_SIZE;
		size -= IOVP_SIZE;
	}

	spin_unlock_irqrestore(&ioc->res_lock, flags);

	/* form complete address */
	return CCIO_IOVA(iovp, offset);
}

/**
 * ccio_unmap_single - Unmap an address range from the IOMMU.
 * @dev: The PCI device.
 * @addr: The start address of the DMA region.
 * @size: The length of the DMA region.
 * @direction: The direction of the DMA transaction (to/from device).
 *
 * This function implements the pci_unmap_single function.
 */
static void 
ccio_unmap_single(struct device *dev, dma_addr_t iova, size_t size, 
		  enum dma_data_direction direction)
{
	struct ioc *ioc;
	unsigned long flags; 
	dma_addr_t offset = iova & ~IOVP_MASK;
	
	BUG_ON(!dev);
	ioc = GET_IOC(dev);

	DBG_RUN("%s() iovp 0x%lx/%x\n",
		__func__, (long)iova, size);

	iova ^= offset;        /* clear offset bits */
	size += offset;
	size = ALIGN(size, IOVP_SIZE);

	spin_lock_irqsave(&ioc->res_lock, flags);

#ifdef CCIO_COLLECT_STATS
	ioc->usingle_calls++;
	ioc->usingle_pages += size >> IOVP_SHIFT;
#endif

	ccio_mark_invalid(ioc, iova, size);
	ccio_free_range(ioc, iova, (size >> IOVP_SHIFT));
	spin_unlock_irqrestore(&ioc->res_lock, flags);
}

/**
 * ccio_alloc_consistent - Allocate a consistent DMA mapping.
 * @dev: The PCI device.
 * @size: The length of the DMA region.
 * @dma_handle: The DMA address handed back to the device (not the cpu).
 *
 * This function implements the pci_alloc_consistent function.
 */
static void * 
ccio_alloc_consistent(struct device *dev, size_t size, dma_addr_t *dma_handle, gfp_t flag)
{
      void *ret;
#if 0
/* GRANT Need to establish hierarchy for non-PCI devs as well
** and then provide matching gsc_map_xxx() functions for them as well.
*/
	if(!hwdev) {
		/* only support PCI */
		*dma_handle = 0;
		return 0;
	}
#endif
        ret = (void *) __get_free_pages(flag, get_order(size));

	if (ret) {
		memset(ret, 0, size);
		*dma_handle = ccio_map_single(dev, ret, size, PCI_DMA_BIDIRECTIONAL);
	}

	return ret;
}

/**
 * ccio_free_consistent - Free a consistent DMA mapping.
 * @dev: The PCI device.
 * @size: The length of the DMA region.
 * @cpu_addr: The cpu address returned from the ccio_alloc_consistent.
 * @dma_handle: The device address returned from the ccio_alloc_consistent.
 *
 * This function implements the pci_free_consistent function.
 */
static void 
ccio_free_consistent(struct device *dev, size_t size, void *cpu_addr, 
		     dma_addr_t dma_handle)
{
	ccio_unmap_single(dev, dma_handle, size, 0);
	free_pages((unsigned long)cpu_addr, get_order(size));
}

/*
** Since 0 is a valid pdir_base index value, can't use that
** to determine if a value is valid or not. Use a flag to indicate
** the SG list entry contains a valid pdir index.
*/
#define PIDE_FLAG 0x80000000UL

#ifdef CCIO_COLLECT_STATS
#define IOMMU_MAP_STATS
#endif
#include "iommu-helpers.h"

/**
 * ccio_map_sg - Map the scatter/gather list into the IOMMU.
 * @dev: The PCI device.
 * @sglist: The scatter/gather list to be mapped in the IOMMU.
 * @nents: The number of entries in the scatter/gather list.
 * @direction: The direction of the DMA transaction (to/from device).
 *
 * This function implements the pci_map_sg function.
 */
static int
ccio_map_sg(struct device *dev, struct scatterlist *sglist, int nents, 
	    enum dma_data_direction direction)
{
	struct ioc *ioc;
	int coalesced, filled = 0;
	unsigned long flags;
	unsigned long hint = hint_lookup[(int)direction];
	unsigned long prev_len = 0, current_len = 0;
	int i;
	
	BUG_ON(!dev);
	ioc = GET_IOC(dev);
	
	DBG_RUN_SG("%s() START %d entries\n", __func__, nents);

	/* Fast path single entry scatterlists. */
	if (nents == 1) {
		sg_dma_address(sglist) = ccio_map_single(dev,
				sg_virt(sglist), sglist->length,
				direction);
		sg_dma_len(sglist) = sglist->length;
		return 1;
	}

	for(i = 0; i < nents; i++)
		prev_len += sglist[i].length;
	
	spin_lock_irqsave(&ioc->res_lock, flags);

#ifdef CCIO_COLLECT_STATS
	ioc->msg_calls++;
#endif

	/*
	** First coalesce the chunks and allocate I/O pdir space
	**
	** If this is one DMA stream, we can properly map using the
	** correct virtual address associated with each DMA page.
	** w/o this association, we wouldn't have coherent DMA!
	** Access to the virtual address is what forces a two pass algorithm.
	*/
	coalesced = iommu_coalesce_chunks(ioc, dev, sglist, nents, ccio_alloc_range);

	/*
	** Program the I/O Pdir
	**
	** map the virtual addresses to the I/O Pdir
	** o dma_address will contain the pdir index
	** o dma_len will contain the number of bytes to map 
	** o page/offset contain the virtual address.
	*/
	filled = iommu_fill_pdir(ioc, sglist, nents, hint, ccio_io_pdir_entry);

	spin_unlock_irqrestore(&ioc->res_lock, flags);

	BUG_ON(coalesced != filled);

	DBG_RUN_SG("%s() DONE %d mappings\n", __func__, filled);

	for (i = 0; i < filled; i++)
		current_len += sg_dma_len(sglist + i);

	BUG_ON(current_len != prev_len);

	return filled;
}

/**
 * ccio_unmap_sg - Unmap the scatter/gather list from the IOMMU.
 * @dev: The PCI device.
 * @sglist: The scatter/gather list to be unmapped from the IOMMU.
 * @nents: The number of entries in the scatter/gather list.
 * @direction: The direction of the DMA transaction (to/from device).
 *
 * This function implements the pci_unmap_sg function.
 */
static void 
ccio_unmap_sg(struct device *dev, struct scatterlist *sglist, int nents, 
	      enum dma_data_direction direction)
{
	struct ioc *ioc;

	BUG_ON(!dev);
	ioc = GET_IOC(dev);

	DBG_RUN_SG("%s() START %d entries, %p,%x\n",
		__func__, nents, sg_virt(sglist), sglist->length);

#ifdef CCIO_COLLECT_STATS
	ioc->usg_calls++;
#endif

	while(sg_dma_len(sglist) && nents--) {

#ifdef CCIO_COLLECT_STATS
		ioc->usg_pages += sg_dma_len(sglist) >> PAGE_SHIFT;
#endif
		ccio_unmap_single(dev, sg_dma_address(sglist),
				  sg_dma_len(sglist), direction);
		++sglist;
	}

	DBG_RUN_SG("%s() DONE (nents %d)\n", __func__, nents);
}

static struct hppa_dma_ops ccio_ops = {
	.dma_supported =	ccio_dma_supported,
	.alloc_consistent =	ccio_alloc_consistent,
	.alloc_noncoherent =	ccio_alloc_consistent,
	.free_consistent =	ccio_free_consistent,
	.map_single =		ccio_map_single,
	.unmap_single =		ccio_unmap_single,
	.map_sg = 		ccio_map_sg,
	.unmap_sg = 		ccio_unmap_sg,
	.dma_sync_single_for_cpu =	NULL,	/* NOP for U2/Uturn */
	.dma_sync_single_for_device =	NULL,	/* NOP for U2/Uturn */
	.dma_sync_sg_for_cpu =		NULL,	/* ditto */
	.dma_sync_sg_for_device =		NULL,	/* ditto */
};

#ifdef CONFIG_PROC_FS
static int ccio_proc_info(struct seq_file *m, void *p)
{
	struct ioc *ioc = ioc_list;

	while (ioc != NULL) {
		unsigned int total_pages = ioc->res_size << 3;
#ifdef CCIO_COLLECT_STATS
		unsigned long avg = 0, min, max;
		int j;
#endif

		seq_printf(m, "%s\n", ioc->name);
		
		seq_printf(m, "Cujo 2.0 bug    : %s\n",
			   (ioc->cujo20_bug ? "yes" : "no"));
		
		seq_printf(m, "IO PDIR size    : %d bytes (%d entries)\n",
			   total_pages * 8, total_pages);

#ifdef CCIO_COLLECT_STATS
		seq_printf(m, "IO PDIR entries : %ld free  %ld used (%d%%)\n",
			   total_pages - ioc->used_pages, ioc->used_pages,
			   (int)(ioc->used_pages * 100 / total_pages));
#endif

		seq_printf(m, "Resource bitmap : %d bytes (%d pages)\n",
			   ioc->res_size, total_pages);

#ifdef CCIO_COLLECT_STATS
		min = max = ioc->avg_search[0];
		for(j = 0; j < CCIO_SEARCH_SAMPLE; ++j) {
			avg += ioc->avg_search[j];
			if(ioc->avg_search[j] > max) 
				max = ioc->avg_search[j];
			if(ioc->avg_search[j] < min) 
				min = ioc->avg_search[j];
		}
		avg /= CCIO_SEARCH_SAMPLE;
		seq_printf(m, "  Bitmap search : %ld/%ld/%ld (min/avg/max CPU Cycles)\n",
			   min, avg, max);

		seq_printf(m, "pci_map_single(): %8ld calls  %8ld pages (avg %d/1000)\n",
			   ioc->msingle_calls, ioc->msingle_pages,
			   (int)((ioc->msingle_pages * 1000)/ioc->msingle_calls));

		/* KLUGE - unmap_sg calls unmap_single for each mapped page */
		min = ioc->usingle_calls - ioc->usg_calls;
		max = ioc->usingle_pages - ioc->usg_pages;
		seq_printf(m, "pci_unmap_single: %8ld calls  %8ld pages (avg %d/1000)\n",
			   min, max, (int)((max * 1000)/min));
 
		seq_printf(m, "pci_map_sg()    : %8ld calls  %8ld pages (avg %d/1000)\n",
			   ioc->msg_calls, ioc->msg_pages,
			   (int)((ioc->msg_pages * 1000)/ioc->msg_calls));

		seq_printf(m, "pci_unmap_sg()  : %8ld calls  %8ld pages (avg %d/1000)\n\n\n",
			   ioc->usg_calls, ioc->usg_pages,
			   (int)((ioc->usg_pages * 1000)/ioc->usg_calls));
#endif	/* CCIO_COLLECT_STATS */

		ioc = ioc->next;
	}

	return 0;
}

static int ccio_proc_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, &ccio_proc_info, NULL);
}

static const struct file_operations ccio_proc_info_fops = {
	.owner = THIS_MODULE,
	.open = ccio_proc_info_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int ccio_proc_bitmap_info(struct seq_file *m, void *p)
{
	struct ioc *ioc = ioc_list;

	while (ioc != NULL) {
		seq_hex_dump(m, "   ", DUMP_PREFIX_NONE, 32, 4, ioc->res_map,
			     ioc->res_size, false);
		seq_putc(m, '\n');
		ioc = ioc->next;
		break; /* XXX - remove me */
	}

	return 0;
}

static int ccio_proc_bitmap_open(struct inode *inode, struct file *file)
{
	return single_open(file, &ccio_proc_bitmap_info, NULL);
}

static const struct file_operations ccio_proc_bitmap_fops = {
	.owner = THIS_MODULE,
	.open = ccio_proc_bitmap_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif /* CONFIG_PROC_FS */

/**
 * ccio_find_ioc - Find the ioc in the ioc_list
 * @hw_path: The hardware path of the ioc.
 *
 * This function searches the ioc_list for an ioc that matches
 * the provide hardware path.
 */
static struct ioc * ccio_find_ioc(int hw_path)
{
	int i;
	struct ioc *ioc;

	ioc = ioc_list;
	for (i = 0; i < ioc_count; i++) {
		if (ioc->hw_path == hw_path)
			return ioc;

		ioc = ioc->next;
	}

	return NULL;
}

/**
 * ccio_get_iommu - Find the iommu which controls this device
 * @dev: The parisc device.
 *
 * This function searches through the registered IOMMU's and returns
 * the appropriate IOMMU for the device based on its hardware path.
 */
void * ccio_get_iommu(const struct parisc_device *dev)
{
	dev = find_pa_parent_type(dev, HPHW_IOA);
	if (!dev)
		return NULL;

	return ccio_find_ioc(dev->hw_path);
}

#define CUJO_20_STEP       0x10000000	/* inc upper nibble */

/* Cujo 2.0 has a bug which will silently corrupt data being transferred
 * to/from certain pages.  To avoid this happening, we mark these pages
 * as `used', and ensure that nothing will try to allocate from them.
 */
void ccio_cujo20_fixup(struct parisc_device *cujo, u32 iovp)
{
	unsigned int idx;
	struct parisc_device *dev = parisc_parent(cujo);
	struct ioc *ioc = ccio_get_iommu(dev);
	u8 *res_ptr;

	ioc->cujo20_bug = 1;
	res_ptr = ioc->res_map;
	idx = PDIR_INDEX(iovp) >> 3;

	while (idx < ioc->res_size) {
 		res_ptr[idx] |= 0xff;
		idx += PDIR_INDEX(CUJO_20_STEP) >> 3;
	}
}

#if 0
/* GRANT -  is this needed for U2 or not? */

/*
** Get the size of the I/O TLB for this I/O MMU.
**
** If spa_shift is non-zero (ie probably U2),
** then calculate the I/O TLB size using spa_shift.
**
** Otherwise we are supposed to get the IODC entry point ENTRY TLB
** and execute it. However, both U2 and Uturn firmware supplies spa_shift.
** I think only Java (K/D/R-class too?) systems don't do this.
*/
static int
ccio_get_iotlb_size(struct parisc_device *dev)
{
	if (dev->spa_shift == 0) {
		panic("%s() : Can't determine I/O TLB size.\n", __func__);
	}
	return (1 << dev->spa_shift);
}
#else

/* Uturn supports 256 TLB entries */
#define CCIO_CHAINID_SHIFT	8
#define CCIO_CHAINID_MASK	0xff
#endif /* 0 */

/* We *can't* support JAVA (T600). Venture there at your own risk. */
static const struct parisc_device_id ccio_tbl[] = {
	{ HPHW_IOA, HVERSION_REV_ANY_ID, U2_IOA_RUNWAY, 0xb }, /* U2 */
	{ HPHW_IOA, HVERSION_REV_ANY_ID, UTURN_IOA_RUNWAY, 0xb }, /* UTurn */
	{ 0, }
};

static int ccio_probe(struct parisc_device *dev);

static struct parisc_driver ccio_driver = {
	.name =		"ccio",
	.id_table =	ccio_tbl,
	.probe =	ccio_probe,
};

/**
 * ccio_ioc_init - Initialize the I/O Controller
 * @ioc: The I/O Controller.
 *
 * Initialize the I/O Controller which includes setting up the
 * I/O Page Directory, the resource map, and initalizing the
 * U2/Uturn chip into virtual mode.
 */
static void
ccio_ioc_init(struct ioc *ioc)
{
	int i;
	unsigned int iov_order;
	u32 iova_space_size;

	/*
	** Determine IOVA Space size from memory size.
	**
	** Ideally, PCI drivers would register the maximum number
	** of DMA they can have outstanding for each device they
	** own.  Next best thing would be to guess how much DMA
	** can be outstanding based on PCI Class/sub-class. Both
	** methods still require some "extra" to support PCI
	** Hot-Plug/Removal of PCI cards. (aka PCI OLARD).
	*/

	iova_space_size = (u32) (totalram_pages / count_parisc_driver(&ccio_driver));

	/* limit IOVA space size to 1MB-1GB */

	if (iova_space_size < (1 << (20 - PAGE_SHIFT))) {
		iova_space_size =  1 << (20 - PAGE_SHIFT);
#ifdef __LP64__
	} else if (iova_space_size > (1 << (30 - PAGE_SHIFT))) {
		iova_space_size =  1 << (30 - PAGE_SHIFT);
#endif
	}

	/*
	** iova space must be log2() in size.
	** thus, pdir/res_map will also be log2().
	*/

	/* We could use larger page sizes in order to *decrease* the number
	** of mappings needed.  (ie 8k pages means 1/2 the mappings).
	**
	** Note: Grant Grunder says "Using 8k I/O pages isn't trivial either
	**   since the pages must also be physically contiguous - typically
	**   this is the case under linux."
	*/

	iov_order = get_order(iova_space_size << PAGE_SHIFT);

	/* iova_space_size is now bytes, not pages */
	iova_space_size = 1 << (iov_order + PAGE_SHIFT);

	ioc->pdir_size = (iova_space_size / IOVP_SIZE) * sizeof(u64);

	BUG_ON(ioc->pdir_size > 8 * 1024 * 1024);   /* max pdir size <= 8MB */

	/* Verify it's a power of two */
	BUG_ON((1 << get_order(ioc->pdir_size)) != (ioc->pdir_size >> PAGE_SHIFT));

	DBG_INIT("%s() hpa 0x%p mem %luMB IOV %dMB (%d bits)\n",
			__func__, ioc->ioc_regs,
			(unsigned long) totalram_pages >> (20 - PAGE_SHIFT),
			iova_space_size>>20,
			iov_order + PAGE_SHIFT);

	ioc->pdir_base = (u64 *)__get_free_pages(GFP_KERNEL, 
						 get_order(ioc->pdir_size));
	if(NULL == ioc->pdir_base) {
		panic("%s() could not allocate I/O Page Table\n", __func__);
	}
	memset(ioc->pdir_base, 0, ioc->pdir_size);

	BUG_ON((((unsigned long)ioc->pdir_base) & PAGE_MASK) != (unsigned long)ioc->pdir_base);
	DBG_INIT(" base %p\n", ioc->pdir_base);

	/* resource map size dictated by pdir_size */
 	ioc->res_size = (ioc->pdir_size / sizeof(u64)) >> 3;
	DBG_INIT("%s() res_size 0x%x\n", __func__, ioc->res_size);
	
	ioc->res_map = (u8 *)__get_free_pages(GFP_KERNEL, 
					      get_order(ioc->res_size));
	if(NULL == ioc->res_map) {
		panic("%s() could not allocate resource map\n", __func__);
	}
	memset(ioc->res_map, 0, ioc->res_size);

	/* Initialize the res_hint to 16 */
	ioc->res_hint = 16;

	/* Initialize the spinlock */
	spin_lock_init(&ioc->res_lock);

	/*
	** Chainid is the upper most bits of an IOVP used to determine
	** which TLB entry an IOVP will use.
	*/
	ioc->chainid_shift = get_order(iova_space_size) + PAGE_SHIFT - CCIO_CHAINID_SHIFT;
	DBG_INIT(" chainid_shift 0x%x\n", ioc->chainid_shift);

	/*
	** Initialize IOA hardware
	*/
	WRITE_U32(CCIO_CHAINID_MASK << ioc->chainid_shift, 
		  &ioc->ioc_regs->io_chain_id_mask);

	WRITE_U32(virt_to_phys(ioc->pdir_base), 
		  &ioc->ioc_regs->io_pdir_base);

	/*
	** Go to "Virtual Mode"
	*/
	WRITE_U32(IOA_NORMAL_MODE, &ioc->ioc_regs->io_control);

	/*
	** Initialize all I/O TLB entries to 0 (Valid bit off).
	*/
	WRITE_U32(0, &ioc->ioc_regs->io_tlb_entry_m);
	WRITE_U32(0, &ioc->ioc_regs->io_tlb_entry_l);

	for(i = 1 << CCIO_CHAINID_SHIFT; i ; i--) {
		WRITE_U32((CMD_TLB_DIRECT_WRITE | (i << ioc->chainid_shift)),
			  &ioc->ioc_regs->io_command);
	}
}

static void __init
ccio_init_resource(struct resource *res, char *name, void __iomem *ioaddr)
{
	int result;

	res->parent = NULL;
	res->flags = IORESOURCE_MEM;
	/*
	 * bracing ((signed) ...) are required for 64bit kernel because
	 * we only want to sign extend the lower 16 bits of the register.
	 * The upper 16-bits of range registers are hardcoded to 0xffff.
	 */
	res->start = (unsigned long)((signed) READ_U32(ioaddr) << 16);
	res->end = (unsigned long)((signed) (READ_U32(ioaddr + 4) << 16) - 1);
	res->name = name;
	/*
	 * Check if this MMIO range is disable
	 */
	if (res->end + 1 == res->start)
		return;

	/* On some platforms (e.g. K-Class), we have already registered
	 * resources for devices reported by firmware. Some are children
	 * of ccio.
	 * "insert" ccio ranges in the mmio hierarchy (/proc/iomem).
	 */
	result = insert_resource(&iomem_resource, res);
	if (result < 0) {
		printk(KERN_ERR "%s() failed to claim CCIO bus address space (%08lx,%08lx)\n", 
			__func__, (unsigned long)res->start, (unsigned long)res->end);
	}
}

static void __init ccio_init_resources(struct ioc *ioc)
{
	struct resource *res = ioc->mmio_region;
	char *name = kmalloc(14, GFP_KERNEL);

	snprintf(name, 14, "GSC Bus [%d/]", ioc->hw_path);

	ccio_init_resource(res, name, &ioc->ioc_regs->io_io_low);
	ccio_init_resource(res + 1, name, &ioc->ioc_regs->io_io_low_hv);
}

static int new_ioc_area(struct resource *res, unsigned long size,
		unsigned long min, unsigned long max, unsigned long align)
{
	if (max <= min)
		return -EBUSY;

	res->start = (max - size + 1) &~ (align - 1);
	res->end = res->start + size;
	
	/* We might be trying to expand the MMIO range to include
	 * a child device that has already registered it's MMIO space.
	 * Use "insert" instead of request_resource().
	 */
	if (!insert_resource(&iomem_resource, res))
		return 0;

	return new_ioc_area(res, size, min, max - size, align);
}

static int expand_ioc_area(struct resource *res, unsigned long size,
		unsigned long min, unsigned long max, unsigned long align)
{
	unsigned long start, len;

	if (!res->parent)
		return new_ioc_area(res, size, min, max, align);

	start = (res->start - size) &~ (align - 1);
	len = res->end - start + 1;
	if (start >= min) {
		if (!adjust_resource(res, start, len))
			return 0;
	}

	start = res->start;
	len = ((size + res->end + align) &~ (align - 1)) - start;
	if (start + len <= max) {
		if (!adjust_resource(res, start, len))
			return 0;
	}

	return -EBUSY;
}

/*
 * Dino calls this function.  Beware that we may get called on systems
 * which have no IOC (725, B180, C160L, etc) but do have a Dino.
 * So it's legal to find no parent IOC.
 *
 * Some other issues: one of the resources in the ioc may be unassigned.
 */
int ccio_allocate_resource(const struct parisc_device *dev,
		struct resource *res, unsigned long size,
		unsigned long min, unsigned long max, unsigned long align)
{
	struct resource *parent = &iomem_resource;
	struct ioc *ioc = ccio_get_iommu(dev);
	if (!ioc)
		goto out;

	parent = ioc->mmio_region;
	if (parent->parent &&
	    !allocate_resource(parent, res, size, min, max, align, NULL, NULL))
		return 0;

	if ((parent + 1)->parent &&
	    !allocate_resource(parent + 1, res, size, min, max, align,
				NULL, NULL))
		return 0;

	if (!expand_ioc_area(parent, size, min, max, align)) {
		__raw_writel(((parent->start)>>16) | 0xffff0000,
			     &ioc->ioc_regs->io_io_low);
		__raw_writel(((parent->end)>>16) | 0xffff0000,
			     &ioc->ioc_regs->io_io_high);
	} else if (!expand_ioc_area(parent + 1, size, min, max, align)) {
		parent++;
		__raw_writel(((parent->start)>>16) | 0xffff0000,
			     &ioc->ioc_regs->io_io_low_hv);
		__raw_writel(((parent->end)>>16) | 0xffff0000,
			     &ioc->ioc_regs->io_io_high_hv);
	} else {
		return -EBUSY;
	}

 out:
	return allocate_resource(parent, res, size, min, max, align, NULL,NULL);
}

int ccio_request_resource(const struct parisc_device *dev,
		struct resource *res)
{
	struct resource *parent;
	struct ioc *ioc = ccio_get_iommu(dev);

	if (!ioc) {
		parent = &iomem_resource;
	} else if ((ioc->mmio_region->start <= res->start) &&
			(res->end <= ioc->mmio_region->end)) {
		parent = ioc->mmio_region;
	} else if (((ioc->mmio_region + 1)->start <= res->start) &&
			(res->end <= (ioc->mmio_region + 1)->end)) {
		parent = ioc->mmio_region + 1;
	} else {
		return -EBUSY;
	}

	/* "transparent" bus bridges need to register MMIO resources
	 * firmware assigned them. e.g. children of hppb.c (e.g. K-class)
	 * registered their resources in the PDC "bus walk" (See
	 * arch/parisc/kernel/inventory.c).
	 */
	return insert_resource(parent, res);
}

/**
 * ccio_probe - Determine if ccio should claim this device.
 * @dev: The device which has been found
 *
 * Determine if ccio should claim this chip (return 0) or not (return 1).
 * If so, initialize the chip and tell other partners in crime they
 * have work to do.
 */
static int __init ccio_probe(struct parisc_device *dev)
{
	int i;
	struct ioc *ioc, **ioc_p = &ioc_list;

	ioc = kzalloc(sizeof(struct ioc), GFP_KERNEL);
	if (ioc == NULL) {
		printk(KERN_ERR MODULE_NAME ": memory allocation failure\n");
		return 1;
	}

	ioc->name = dev->id.hversion == U2_IOA_RUNWAY ? "U2" : "UTurn";

	printk(KERN_INFO "Found %s at 0x%lx\n", ioc->name,
		(unsigned long)dev->hpa.start);

	for (i = 0; i < ioc_count; i++) {
		ioc_p = &(*ioc_p)->next;
	}
	*ioc_p = ioc;

	ioc->hw_path = dev->hw_path;
	ioc->ioc_regs = ioremap_nocache(dev->hpa.start, 4096);
	ccio_ioc_init(ioc);
	ccio_init_resources(ioc);
	hppa_dma_ops = &ccio_ops;
	dev->dev.platform_data = kzalloc(sizeof(struct pci_hba_data), GFP_KERNEL);

	/* if this fails, no I/O cards will work, so may as well bug */
	BUG_ON(dev->dev.platform_data == NULL);
	HBA_DATA(dev->dev.platform_data)->iommu = ioc;

#ifdef CONFIG_PROC_FS
	if (ioc_count == 0) {
		proc_create(MODULE_NAME, 0, proc_runway_root,
			    &ccio_proc_info_fops);
		proc_create(MODULE_NAME"-bitmap", 0, proc_runway_root,
			    &ccio_proc_bitmap_fops);
	}
#endif
	ioc_count++;

	parisc_has_iommu();
	return 0;
}

/**
 * ccio_init - ccio initialization procedure.
 *
 * Register this driver.
 */
void __init ccio_init(void)
{
	register_parisc_driver(&ccio_driver);
}


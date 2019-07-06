// SPDX-License-Identifier: GPL-2.0-or-later
/*
**  System Bus Adapter (SBA) I/O MMU manager
**
**	(c) Copyright 2000-2004 Grant Grundler <grundler @ parisc-linux x org>
**	(c) Copyright 2004 Naresh Kumar Inna <knaresh at india x hp x com>
**	(c) Copyright 2000-2004 Hewlett-Packard Company
**
**	Portions (c) 1999 Dave S. Miller (from sparc64 I/O MMU code)
**
**
**
** This module initializes the IOC (I/O Controller) found on B1000/C3000/
** J5000/J7000/N-class/L-class machines and their successors.
**
** FIXME: add DMA hint support programming in both sba and lba modules.
*/

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/init.h>

#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/scatterlist.h>
#include <linux/iommu-helper.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/dma.h>		/* for DMA_CHUNK_SIZE */

#include <asm/hardware.h>	/* for register_parisc_driver() stuff */

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/module.h>

#include <asm/ropes.h>
#include <asm/mckinley.h>	/* for proc_mckinley_root */
#include <asm/runway.h>		/* for proc_runway_root */
#include <asm/page.h>		/* for PAGE0 */
#include <asm/pdc.h>		/* for PDC_MODEL_* */
#include <asm/pdcpat.h>		/* for is_pdc_pat() */
#include <asm/parisc-device.h>

#include "iommu.h"

#define MODULE_NAME "SBA"

/*
** The number of debug flags is a clue - this code is fragile.
** Don't even think about messing with it unless you have
** plenty of 710's to sacrifice to the computer gods. :^)
*/
#undef DEBUG_SBA_INIT
#undef DEBUG_SBA_RUN
#undef DEBUG_SBA_RUN_SG
#undef DEBUG_SBA_RESOURCE
#undef ASSERT_PDIR_SANITY
#undef DEBUG_LARGE_SG_ENTRIES
#undef DEBUG_DMB_TRAP

#ifdef DEBUG_SBA_INIT
#define DBG_INIT(x...)	printk(x)
#else
#define DBG_INIT(x...)
#endif

#ifdef DEBUG_SBA_RUN
#define DBG_RUN(x...)	printk(x)
#else
#define DBG_RUN(x...)
#endif

#ifdef DEBUG_SBA_RUN_SG
#define DBG_RUN_SG(x...)	printk(x)
#else
#define DBG_RUN_SG(x...)
#endif


#ifdef DEBUG_SBA_RESOURCE
#define DBG_RES(x...)	printk(x)
#else
#define DBG_RES(x...)
#endif

#define SBA_INLINE	__inline__

#define DEFAULT_DMA_HINT_REG	0

struct sba_device *sba_list;
EXPORT_SYMBOL_GPL(sba_list);

static unsigned long ioc_needs_fdc = 0;

/* global count of IOMMUs in the system */
static unsigned int global_ioc_cnt = 0;

/* PA8700 (Piranha 2.2) bug workaround */
static unsigned long piranha_bad_128k = 0;

/* Looks nice and keeps the compiler happy */
#define SBA_DEV(d) ((struct sba_device *) (d))

#ifdef CONFIG_AGP_PARISC
#define SBA_AGP_SUPPORT
#endif /*CONFIG_AGP_PARISC*/

#ifdef SBA_AGP_SUPPORT
static int sba_reserve_agpgart = 1;
module_param(sba_reserve_agpgart, int, 0444);
MODULE_PARM_DESC(sba_reserve_agpgart, "Reserve half of IO pdir as AGPGART");
#endif


/************************************
** SBA register read and write support
**
** BE WARNED: register writes are posted.
**  (ie follow writes which must reach HW with a read)
**
** Superdome (in particular, REO) allows only 64-bit CSR accesses.
*/
#define READ_REG32(addr)	readl(addr)
#define READ_REG64(addr)	readq(addr)
#define WRITE_REG32(val, addr)	writel((val), (addr))
#define WRITE_REG64(val, addr)	writeq((val), (addr))

#ifdef CONFIG_64BIT
#define READ_REG(addr)		READ_REG64(addr)
#define WRITE_REG(value, addr)	WRITE_REG64(value, addr)
#else
#define READ_REG(addr)		READ_REG32(addr)
#define WRITE_REG(value, addr)	WRITE_REG32(value, addr)
#endif

#ifdef DEBUG_SBA_INIT

/* NOTE: When CONFIG_64BIT isn't defined, READ_REG64() is two 32-bit reads */

/**
 * sba_dump_ranges - debugging only - print ranges assigned to this IOA
 * @hpa: base address of the sba
 *
 * Print the MMIO and IO Port address ranges forwarded by an Astro/Ike/RIO
 * IO Adapter (aka Bus Converter).
 */
static void
sba_dump_ranges(void __iomem *hpa)
{
	DBG_INIT("SBA at 0x%p\n", hpa);
	DBG_INIT("IOS_DIST_BASE   : %Lx\n", READ_REG64(hpa+IOS_DIST_BASE));
	DBG_INIT("IOS_DIST_MASK   : %Lx\n", READ_REG64(hpa+IOS_DIST_MASK));
	DBG_INIT("IOS_DIST_ROUTE  : %Lx\n", READ_REG64(hpa+IOS_DIST_ROUTE));
	DBG_INIT("\n");
	DBG_INIT("IOS_DIRECT_BASE : %Lx\n", READ_REG64(hpa+IOS_DIRECT_BASE));
	DBG_INIT("IOS_DIRECT_MASK : %Lx\n", READ_REG64(hpa+IOS_DIRECT_MASK));
	DBG_INIT("IOS_DIRECT_ROUTE: %Lx\n", READ_REG64(hpa+IOS_DIRECT_ROUTE));
}

/**
 * sba_dump_tlb - debugging only - print IOMMU operating parameters
 * @hpa: base address of the IOMMU
 *
 * Print the size/location of the IO MMU PDIR.
 */
static void sba_dump_tlb(void __iomem *hpa)
{
	DBG_INIT("IO TLB at 0x%p\n", hpa);
	DBG_INIT("IOC_IBASE    : 0x%Lx\n", READ_REG64(hpa+IOC_IBASE));
	DBG_INIT("IOC_IMASK    : 0x%Lx\n", READ_REG64(hpa+IOC_IMASK));
	DBG_INIT("IOC_TCNFG    : 0x%Lx\n", READ_REG64(hpa+IOC_TCNFG));
	DBG_INIT("IOC_PDIR_BASE: 0x%Lx\n", READ_REG64(hpa+IOC_PDIR_BASE));
	DBG_INIT("\n");
}
#else
#define sba_dump_ranges(x)
#define sba_dump_tlb(x)
#endif	/* DEBUG_SBA_INIT */


#ifdef ASSERT_PDIR_SANITY

/**
 * sba_dump_pdir_entry - debugging only - print one IOMMU PDIR entry
 * @ioc: IO MMU structure which owns the pdir we are interested in.
 * @msg: text to print ont the output line.
 * @pide: pdir index.
 *
 * Print one entry of the IO MMU PDIR in human readable form.
 */
static void
sba_dump_pdir_entry(struct ioc *ioc, char *msg, uint pide)
{
	/* start printing from lowest pde in rval */
	u64 *ptr = &(ioc->pdir_base[pide & (~0U * BITS_PER_LONG)]);
	unsigned long *rptr = (unsigned long *) &(ioc->res_map[(pide >>3) & ~(sizeof(unsigned long) - 1)]);
	uint rcnt;

	printk(KERN_DEBUG "SBA: %s rp %p bit %d rval 0x%lx\n",
		 msg,
		 rptr, pide & (BITS_PER_LONG - 1), *rptr);

	rcnt = 0;
	while (rcnt < BITS_PER_LONG) {
		printk(KERN_DEBUG "%s %2d %p %016Lx\n",
			(rcnt == (pide & (BITS_PER_LONG - 1)))
				? "    -->" : "       ",
			rcnt, ptr, *ptr );
		rcnt++;
		ptr++;
	}
	printk(KERN_DEBUG "%s", msg);
}


/**
 * sba_check_pdir - debugging only - consistency checker
 * @ioc: IO MMU structure which owns the pdir we are interested in.
 * @msg: text to print ont the output line.
 *
 * Verify the resource map and pdir state is consistent
 */
static int
sba_check_pdir(struct ioc *ioc, char *msg)
{
	u32 *rptr_end = (u32 *) &(ioc->res_map[ioc->res_size]);
	u32 *rptr = (u32 *) ioc->res_map;	/* resource map ptr */
	u64 *pptr = ioc->pdir_base;	/* pdir ptr */
	uint pide = 0;

	while (rptr < rptr_end) {
		u32 rval = *rptr;
		int rcnt = 32;	/* number of bits we might check */

		while (rcnt) {
			/* Get last byte and highest bit from that */
			u32 pde = ((u32) (((char *)pptr)[7])) << 24;
			if ((rval ^ pde) & 0x80000000)
			{
				/*
				** BUMMER!  -- res_map != pdir --
				** Dump rval and matching pdir entries
				*/
				sba_dump_pdir_entry(ioc, msg, pide);
				return(1);
			}
			rcnt--;
			rval <<= 1;	/* try the next bit */
			pptr++;
			pide++;
		}
		rptr++;	/* look at next word of res_map */
	}
	/* It'd be nice if we always got here :^) */
	return 0;
}


/**
 * sba_dump_sg - debugging only - print Scatter-Gather list
 * @ioc: IO MMU structure which owns the pdir we are interested in.
 * @startsg: head of the SG list
 * @nents: number of entries in SG list
 *
 * print the SG list so we can verify it's correct by hand.
 */
static void
sba_dump_sg( struct ioc *ioc, struct scatterlist *startsg, int nents)
{
	while (nents-- > 0) {
		printk(KERN_DEBUG " %d : %08lx/%05x %p/%05x\n",
				nents,
				(unsigned long) sg_dma_address(startsg),
				sg_dma_len(startsg),
				sg_virt(startsg), startsg->length);
		startsg++;
	}
}

#endif /* ASSERT_PDIR_SANITY */




/**************************************************************
*
*   I/O Pdir Resource Management
*
*   Bits set in the resource map are in use.
*   Each bit can represent a number of pages.
*   LSbs represent lower addresses (IOVA's).
*
***************************************************************/
#define PAGES_PER_RANGE 1	/* could increase this to 4 or 8 if needed */

/* Convert from IOVP to IOVA and vice versa. */

#ifdef ZX1_SUPPORT
/* Pluto (aka ZX1) boxes need to set or clear the ibase bits appropriately */
#define SBA_IOVA(ioc,iovp,offset,hint_reg) ((ioc->ibase) | (iovp) | (offset))
#define SBA_IOVP(ioc,iova) ((iova) & (ioc)->iovp_mask)
#else
/* only support Astro and ancestors. Saves a few cycles in key places */
#define SBA_IOVA(ioc,iovp,offset,hint_reg) ((iovp) | (offset))
#define SBA_IOVP(ioc,iova) (iova)
#endif

#define PDIR_INDEX(iovp)   ((iovp)>>IOVP_SHIFT)

#define RESMAP_MASK(n)    (~0UL << (BITS_PER_LONG - (n)))
#define RESMAP_IDX_MASK   (sizeof(unsigned long) - 1)

static unsigned long ptr_to_pide(struct ioc *ioc, unsigned long *res_ptr,
				 unsigned int bitshiftcnt)
{
	return (((unsigned long)res_ptr - (unsigned long)ioc->res_map) << 3)
		+ bitshiftcnt;
}

/**
 * sba_search_bitmap - find free space in IO PDIR resource bitmap
 * @ioc: IO MMU structure which owns the pdir we are interested in.
 * @bits_wanted: number of entries we need.
 *
 * Find consecutive free bits in resource bitmap.
 * Each bit represents one entry in the IO Pdir.
 * Cool perf optimization: search for log2(size) bits at a time.
 */
static SBA_INLINE unsigned long
sba_search_bitmap(struct ioc *ioc, struct device *dev,
		  unsigned long bits_wanted)
{
	unsigned long *res_ptr = ioc->res_hint;
	unsigned long *res_end = (unsigned long *) &(ioc->res_map[ioc->res_size]);
	unsigned long pide = ~0UL, tpide;
	unsigned long boundary_size;
	unsigned long shift;
	int ret;

	boundary_size = ALIGN((unsigned long long)dma_get_seg_boundary(dev) + 1,
			      1ULL << IOVP_SHIFT) >> IOVP_SHIFT;

#if defined(ZX1_SUPPORT)
	BUG_ON(ioc->ibase & ~IOVP_MASK);
	shift = ioc->ibase >> IOVP_SHIFT;
#else
	shift = 0;
#endif

	if (bits_wanted > (BITS_PER_LONG/2)) {
		/* Search word at a time - no mask needed */
		for(; res_ptr < res_end; ++res_ptr) {
			tpide = ptr_to_pide(ioc, res_ptr, 0);
			ret = iommu_is_span_boundary(tpide, bits_wanted,
						     shift,
						     boundary_size);
			if ((*res_ptr == 0) && !ret) {
				*res_ptr = RESMAP_MASK(bits_wanted);
				pide = tpide;
				break;
			}
		}
		/* point to the next word on next pass */
		res_ptr++;
		ioc->res_bitshift = 0;
	} else {
		/*
		** Search the resource bit map on well-aligned values.
		** "o" is the alignment.
		** We need the alignment to invalidate I/O TLB using
		** SBA HW features in the unmap path.
		*/
		unsigned long o = 1 << get_order(bits_wanted << PAGE_SHIFT);
		uint bitshiftcnt = ALIGN(ioc->res_bitshift, o);
		unsigned long mask;

		if (bitshiftcnt >= BITS_PER_LONG) {
			bitshiftcnt = 0;
			res_ptr++;
		}
		mask = RESMAP_MASK(bits_wanted) >> bitshiftcnt;

		DBG_RES("%s() o %ld %p", __func__, o, res_ptr);
		while(res_ptr < res_end)
		{ 
			DBG_RES("    %p %lx %lx\n", res_ptr, mask, *res_ptr);
			WARN_ON(mask == 0);
			tpide = ptr_to_pide(ioc, res_ptr, bitshiftcnt);
			ret = iommu_is_span_boundary(tpide, bits_wanted,
						     shift,
						     boundary_size);
			if ((((*res_ptr) & mask) == 0) && !ret) {
				*res_ptr |= mask;     /* mark resources busy! */
				pide = tpide;
				break;
			}
			mask >>= o;
			bitshiftcnt += o;
			if (mask == 0) {
				mask = RESMAP_MASK(bits_wanted);
				bitshiftcnt=0;
				res_ptr++;
			}
		}
		/* look in the same word on the next pass */
		ioc->res_bitshift = bitshiftcnt + bits_wanted;
	}

	/* wrapped ? */
	if (res_end <= res_ptr) {
		ioc->res_hint = (unsigned long *) ioc->res_map;
		ioc->res_bitshift = 0;
	} else {
		ioc->res_hint = res_ptr;
	}
	return (pide);
}


/**
 * sba_alloc_range - find free bits and mark them in IO PDIR resource bitmap
 * @ioc: IO MMU structure which owns the pdir we are interested in.
 * @size: number of bytes to create a mapping for
 *
 * Given a size, find consecutive unmarked and then mark those bits in the
 * resource bit map.
 */
static int
sba_alloc_range(struct ioc *ioc, struct device *dev, size_t size)
{
	unsigned int pages_needed = size >> IOVP_SHIFT;
#ifdef SBA_COLLECT_STATS
	unsigned long cr_start = mfctl(16);
#endif
	unsigned long pide;

	pide = sba_search_bitmap(ioc, dev, pages_needed);
	if (pide >= (ioc->res_size << 3)) {
		pide = sba_search_bitmap(ioc, dev, pages_needed);
		if (pide >= (ioc->res_size << 3))
			panic("%s: I/O MMU @ %p is out of mapping resources\n",
			      __FILE__, ioc->ioc_hpa);
	}

#ifdef ASSERT_PDIR_SANITY
	/* verify the first enable bit is clear */
	if(0x00 != ((u8 *) ioc->pdir_base)[pide*sizeof(u64) + 7]) {
		sba_dump_pdir_entry(ioc, "sba_search_bitmap() botched it?", pide);
	}
#endif

	DBG_RES("%s(%x) %d -> %lx hint %x/%x\n",
		__func__, size, pages_needed, pide,
		(uint) ((unsigned long) ioc->res_hint - (unsigned long) ioc->res_map),
		ioc->res_bitshift );

#ifdef SBA_COLLECT_STATS
	{
		unsigned long cr_end = mfctl(16);
		unsigned long tmp = cr_end - cr_start;
		/* check for roll over */
		cr_start = (cr_end < cr_start) ?  -(tmp) : (tmp);
	}
	ioc->avg_search[ioc->avg_idx++] = cr_start;
	ioc->avg_idx &= SBA_SEARCH_SAMPLE - 1;

	ioc->used_pages += pages_needed;
#endif

	return (pide);
}


/**
 * sba_free_range - unmark bits in IO PDIR resource bitmap
 * @ioc: IO MMU structure which owns the pdir we are interested in.
 * @iova: IO virtual address which was previously allocated.
 * @size: number of bytes to create a mapping for
 *
 * clear bits in the ioc's resource map
 */
static SBA_INLINE void
sba_free_range(struct ioc *ioc, dma_addr_t iova, size_t size)
{
	unsigned long iovp = SBA_IOVP(ioc, iova);
	unsigned int pide = PDIR_INDEX(iovp);
	unsigned int ridx = pide >> 3;	/* convert bit to byte address */
	unsigned long *res_ptr = (unsigned long *) &((ioc)->res_map[ridx & ~RESMAP_IDX_MASK]);

	int bits_not_wanted = size >> IOVP_SHIFT;

	/* 3-bits "bit" address plus 2 (or 3) bits for "byte" == bit in word */
	unsigned long m = RESMAP_MASK(bits_not_wanted) >> (pide & (BITS_PER_LONG - 1));

	DBG_RES("%s( ,%x,%x) %x/%lx %x %p %lx\n",
		__func__, (uint) iova, size,
		bits_not_wanted, m, pide, res_ptr, *res_ptr);

#ifdef SBA_COLLECT_STATS
	ioc->used_pages -= bits_not_wanted;
#endif

	*res_ptr &= ~m;
}


/**************************************************************
*
*   "Dynamic DMA Mapping" support (aka "Coherent I/O")
*
***************************************************************/

#ifdef SBA_HINT_SUPPORT
#define SBA_DMA_HINT(ioc, val) ((val) << (ioc)->hint_shift_pdir)
#endif

typedef unsigned long space_t;
#define KERNEL_SPACE 0

/**
 * sba_io_pdir_entry - fill in one IO PDIR entry
 * @pdir_ptr:  pointer to IO PDIR entry
 * @sid: process Space ID - currently only support KERNEL_SPACE
 * @vba: Virtual CPU address of buffer to map
 * @hint: DMA hint set to use for this mapping
 *
 * SBA Mapping Routine
 *
 * Given a virtual address (vba, arg2) and space id, (sid, arg1)
 * sba_io_pdir_entry() loads the I/O PDIR entry pointed to by
 * pdir_ptr (arg0). 
 * Using the bass-ackwards HP bit numbering, Each IO Pdir entry
 * for Astro/Ike looks like:
 *
 *
 *  0                    19                                 51   55       63
 * +-+---------------------+----------------------------------+----+--------+
 * |V|        U            |            PPN[43:12]            | U  |   VI   |
 * +-+---------------------+----------------------------------+----+--------+
 *
 * Pluto is basically identical, supports fewer physical address bits:
 *
 *  0                       23                              51   55       63
 * +-+------------------------+-------------------------------+----+--------+
 * |V|        U               |         PPN[39:12]            | U  |   VI   |
 * +-+------------------------+-------------------------------+----+--------+
 *
 *  V  == Valid Bit  (Most Significant Bit is bit 0)
 *  U  == Unused
 * PPN == Physical Page Number
 * VI  == Virtual Index (aka Coherent Index)
 *
 * LPA instruction output is put into PPN field.
 * LCI (Load Coherence Index) instruction provides the "VI" bits.
 *
 * We pre-swap the bytes since PCX-W is Big Endian and the
 * IOMMU uses little endian for the pdir.
 */

static void SBA_INLINE
sba_io_pdir_entry(u64 *pdir_ptr, space_t sid, unsigned long vba,
		  unsigned long hint)
{
	u64 pa; /* physical address */
	register unsigned ci; /* coherent index */

	pa = virt_to_phys(vba);
	pa &= IOVP_MASK;

	mtsp(sid,1);
	asm("lci 0(%%sr1, %1), %0" : "=r" (ci) : "r" (vba));
	pa |= (ci >> PAGE_SHIFT) & 0xff;  /* move CI (8 bits) into lowest byte */

	pa |= SBA_PDIR_VALID_BIT;	/* set "valid" bit */
	*pdir_ptr = cpu_to_le64(pa);	/* swap and store into I/O Pdir */

	/*
	 * If the PDC_MODEL capabilities has Non-coherent IO-PDIR bit set
	 * (bit #61, big endian), we have to flush and sync every time
	 * IO-PDIR is changed in Ike/Astro.
	 */
	asm_io_fdc(pdir_ptr);
}


/**
 * sba_mark_invalid - invalidate one or more IO PDIR entries
 * @ioc: IO MMU structure which owns the pdir we are interested in.
 * @iova:  IO Virtual Address mapped earlier
 * @byte_cnt:  number of bytes this mapping covers.
 *
 * Marking the IO PDIR entry(ies) as Invalid and invalidate
 * corresponding IO TLB entry. The Ike PCOM (Purge Command Register)
 * is to purge stale entries in the IO TLB when unmapping entries.
 *
 * The PCOM register supports purging of multiple pages, with a minium
 * of 1 page and a maximum of 2GB. Hardware requires the address be
 * aligned to the size of the range being purged. The size of the range
 * must be a power of 2. The "Cool perf optimization" in the
 * allocation routine helps keep that true.
 */
static SBA_INLINE void
sba_mark_invalid(struct ioc *ioc, dma_addr_t iova, size_t byte_cnt)
{
	u32 iovp = (u32) SBA_IOVP(ioc,iova);
	u64 *pdir_ptr = &ioc->pdir_base[PDIR_INDEX(iovp)];

#ifdef ASSERT_PDIR_SANITY
	/* Assert first pdir entry is set.
	**
	** Even though this is a big-endian machine, the entries
	** in the iopdir are little endian. That's why we look at
	** the byte at +7 instead of at +0.
	*/
	if (0x80 != (((u8 *) pdir_ptr)[7])) {
		sba_dump_pdir_entry(ioc,"sba_mark_invalid()", PDIR_INDEX(iovp));
	}
#endif

	if (byte_cnt > IOVP_SIZE)
	{
#if 0
		unsigned long entries_per_cacheline = ioc_needs_fdc ?
				L1_CACHE_ALIGN(((unsigned long) pdir_ptr))
					- (unsigned long) pdir_ptr;
				: 262144;
#endif

		/* set "size" field for PCOM */
		iovp |= get_order(byte_cnt) + PAGE_SHIFT;

		do {
			/* clear I/O Pdir entry "valid" bit first */
			((u8 *) pdir_ptr)[7] = 0;
			asm_io_fdc(pdir_ptr);
			if (ioc_needs_fdc) {
#if 0
				entries_per_cacheline = L1_CACHE_SHIFT - 3;
#endif
			}
			pdir_ptr++;
			byte_cnt -= IOVP_SIZE;
		} while (byte_cnt > IOVP_SIZE);
	} else
		iovp |= IOVP_SHIFT;     /* set "size" field for PCOM */

	/*
	** clear I/O PDIR entry "valid" bit.
	** We have to R/M/W the cacheline regardless how much of the
	** pdir entry that we clobber.
	** The rest of the entry would be useful for debugging if we
	** could dump core on HPMC.
	*/
	((u8 *) pdir_ptr)[7] = 0;
	asm_io_fdc(pdir_ptr);

	WRITE_REG( SBA_IOVA(ioc, iovp, 0, 0), ioc->ioc_hpa+IOC_PCOM);
}

/**
 * sba_dma_supported - PCI driver can query DMA support
 * @dev: instance of PCI owned by the driver that's asking
 * @mask:  number of address bits this PCI device can handle
 *
 * See Documentation/DMA-API-HOWTO.txt
 */
static int sba_dma_supported( struct device *dev, u64 mask)
{
	struct ioc *ioc;

	if (dev == NULL) {
		printk(KERN_ERR MODULE_NAME ": EISA/ISA/et al not supported\n");
		BUG();
		return(0);
	}

	/* Documentation/DMA-API-HOWTO.txt tells drivers to try 64-bit
	 * first, then fall back to 32-bit if that fails.
	 * We are just "encouraging" 32-bit DMA masks here since we can
	 * never allow IOMMU bypass unless we add special support for ZX1.
	 */
	if (mask > ~0U)
		return 0;

	ioc = GET_IOC(dev);
	if (!ioc)
		return 0;

	/*
	 * check if mask is >= than the current max IO Virt Address
	 * The max IO Virt address will *always* < 30 bits.
	 */
	return((int)(mask >= (ioc->ibase - 1 +
			(ioc->pdir_size / sizeof(u64) * IOVP_SIZE) )));
}


/**
 * sba_map_single - map one buffer and return IOVA for DMA
 * @dev: instance of PCI owned by the driver that's asking.
 * @addr:  driver buffer to map.
 * @size:  number of bytes to map in driver buffer.
 * @direction:  R/W or both.
 *
 * See Documentation/DMA-API-HOWTO.txt
 */
static dma_addr_t
sba_map_single(struct device *dev, void *addr, size_t size,
	       enum dma_data_direction direction)
{
	struct ioc *ioc;
	unsigned long flags; 
	dma_addr_t iovp;
	dma_addr_t offset;
	u64 *pdir_start;
	int pide;

	ioc = GET_IOC(dev);
	if (!ioc)
		return DMA_MAPPING_ERROR;

	/* save offset bits */
	offset = ((dma_addr_t) (long) addr) & ~IOVP_MASK;

	/* round up to nearest IOVP_SIZE */
	size = (size + offset + ~IOVP_MASK) & IOVP_MASK;

	spin_lock_irqsave(&ioc->res_lock, flags);
#ifdef ASSERT_PDIR_SANITY
	sba_check_pdir(ioc,"Check before sba_map_single()");
#endif

#ifdef SBA_COLLECT_STATS
	ioc->msingle_calls++;
	ioc->msingle_pages += size >> IOVP_SHIFT;
#endif
	pide = sba_alloc_range(ioc, dev, size);
	iovp = (dma_addr_t) pide << IOVP_SHIFT;

	DBG_RUN("%s() 0x%p -> 0x%lx\n",
		__func__, addr, (long) iovp | offset);

	pdir_start = &(ioc->pdir_base[pide]);

	while (size > 0) {
		sba_io_pdir_entry(pdir_start, KERNEL_SPACE, (unsigned long) addr, 0);

		DBG_RUN("	pdir 0x%p %02x%02x%02x%02x%02x%02x%02x%02x\n",
			pdir_start,
			(u8) (((u8 *) pdir_start)[7]),
			(u8) (((u8 *) pdir_start)[6]),
			(u8) (((u8 *) pdir_start)[5]),
			(u8) (((u8 *) pdir_start)[4]),
			(u8) (((u8 *) pdir_start)[3]),
			(u8) (((u8 *) pdir_start)[2]),
			(u8) (((u8 *) pdir_start)[1]),
			(u8) (((u8 *) pdir_start)[0])
			);

		addr += IOVP_SIZE;
		size -= IOVP_SIZE;
		pdir_start++;
	}

	/* force FDC ops in io_pdir_entry() to be visible to IOMMU */
	asm_io_sync();

#ifdef ASSERT_PDIR_SANITY
	sba_check_pdir(ioc,"Check after sba_map_single()");
#endif
	spin_unlock_irqrestore(&ioc->res_lock, flags);

	/* form complete address */
	return SBA_IOVA(ioc, iovp, offset, DEFAULT_DMA_HINT_REG);
}


static dma_addr_t
sba_map_page(struct device *dev, struct page *page, unsigned long offset,
		size_t size, enum dma_data_direction direction,
		unsigned long attrs)
{
	return sba_map_single(dev, page_address(page) + offset, size,
			direction);
}


/**
 * sba_unmap_page - unmap one IOVA and free resources
 * @dev: instance of PCI owned by the driver that's asking.
 * @iova:  IOVA of driver buffer previously mapped.
 * @size:  number of bytes mapped in driver buffer.
 * @direction:  R/W or both.
 *
 * See Documentation/DMA-API-HOWTO.txt
 */
static void
sba_unmap_page(struct device *dev, dma_addr_t iova, size_t size,
		enum dma_data_direction direction, unsigned long attrs)
{
	struct ioc *ioc;
#if DELAYED_RESOURCE_CNT > 0
	struct sba_dma_pair *d;
#endif
	unsigned long flags; 
	dma_addr_t offset;

	DBG_RUN("%s() iovp 0x%lx/%x\n", __func__, (long) iova, size);

	ioc = GET_IOC(dev);
	if (!ioc) {
		WARN_ON(!ioc);
		return;
	}
	offset = iova & ~IOVP_MASK;
	iova ^= offset;        /* clear offset bits */
	size += offset;
	size = ALIGN(size, IOVP_SIZE);

	spin_lock_irqsave(&ioc->res_lock, flags);

#ifdef SBA_COLLECT_STATS
	ioc->usingle_calls++;
	ioc->usingle_pages += size >> IOVP_SHIFT;
#endif

	sba_mark_invalid(ioc, iova, size);

#if DELAYED_RESOURCE_CNT > 0
	/* Delaying when we re-use a IO Pdir entry reduces the number
	 * of MMIO reads needed to flush writes to the PCOM register.
	 */
	d = &(ioc->saved[ioc->saved_cnt]);
	d->iova = iova;
	d->size = size;
	if (++(ioc->saved_cnt) >= DELAYED_RESOURCE_CNT) {
		int cnt = ioc->saved_cnt;
		while (cnt--) {
			sba_free_range(ioc, d->iova, d->size);
			d--;
		}
		ioc->saved_cnt = 0;

		READ_REG(ioc->ioc_hpa+IOC_PCOM);	/* flush purges */
	}
#else /* DELAYED_RESOURCE_CNT == 0 */
	sba_free_range(ioc, iova, size);

	/* If fdc's were issued, force fdc's to be visible now */
	asm_io_sync();

	READ_REG(ioc->ioc_hpa+IOC_PCOM);	/* flush purges */
#endif /* DELAYED_RESOURCE_CNT == 0 */

	spin_unlock_irqrestore(&ioc->res_lock, flags);

	/* XXX REVISIT for 2.5 Linux - need syncdma for zero-copy support.
	** For Astro based systems this isn't a big deal WRT performance.
	** As long as 2.4 kernels copyin/copyout data from/to userspace,
	** we don't need the syncdma. The issue here is I/O MMU cachelines
	** are *not* coherent in all cases.  May be hwrev dependent.
	** Need to investigate more.
	asm volatile("syncdma");	
	*/
}


/**
 * sba_alloc - allocate/map shared mem for DMA
 * @hwdev: instance of PCI owned by the driver that's asking.
 * @size:  number of bytes mapped in driver buffer.
 * @dma_handle:  IOVA of new buffer.
 *
 * See Documentation/DMA-API-HOWTO.txt
 */
static void *sba_alloc(struct device *hwdev, size_t size, dma_addr_t *dma_handle,
		gfp_t gfp, unsigned long attrs)
{
	void *ret;

	if (!hwdev) {
		/* only support PCI */
		*dma_handle = 0;
		return NULL;
	}

        ret = (void *) __get_free_pages(gfp, get_order(size));

	if (ret) {
		memset(ret, 0, size);
		*dma_handle = sba_map_single(hwdev, ret, size, 0);
	}

	return ret;
}


/**
 * sba_free - free/unmap shared mem for DMA
 * @hwdev: instance of PCI owned by the driver that's asking.
 * @size:  number of bytes mapped in driver buffer.
 * @vaddr:  virtual address IOVA of "consistent" buffer.
 * @dma_handler:  IO virtual address of "consistent" buffer.
 *
 * See Documentation/DMA-API-HOWTO.txt
 */
static void
sba_free(struct device *hwdev, size_t size, void *vaddr,
		    dma_addr_t dma_handle, unsigned long attrs)
{
	sba_unmap_page(hwdev, dma_handle, size, 0, 0);
	free_pages((unsigned long) vaddr, get_order(size));
}


/*
** Since 0 is a valid pdir_base index value, can't use that
** to determine if a value is valid or not. Use a flag to indicate
** the SG list entry contains a valid pdir index.
*/
#define PIDE_FLAG 0x80000000UL

#ifdef SBA_COLLECT_STATS
#define IOMMU_MAP_STATS
#endif
#include "iommu-helpers.h"

#ifdef DEBUG_LARGE_SG_ENTRIES
int dump_run_sg = 0;
#endif


/**
 * sba_map_sg - map Scatter/Gather list
 * @dev: instance of PCI owned by the driver that's asking.
 * @sglist:  array of buffer/length pairs
 * @nents:  number of entries in list
 * @direction:  R/W or both.
 *
 * See Documentation/DMA-API-HOWTO.txt
 */
static int
sba_map_sg(struct device *dev, struct scatterlist *sglist, int nents,
	   enum dma_data_direction direction, unsigned long attrs)
{
	struct ioc *ioc;
	int coalesced, filled = 0;
	unsigned long flags;

	DBG_RUN_SG("%s() START %d entries\n", __func__, nents);

	ioc = GET_IOC(dev);
	if (!ioc)
		return 0;

	/* Fast path single entry scatterlists. */
	if (nents == 1) {
		sg_dma_address(sglist) = sba_map_single(dev, sg_virt(sglist),
						sglist->length, direction);
		sg_dma_len(sglist)     = sglist->length;
		return 1;
	}

	spin_lock_irqsave(&ioc->res_lock, flags);

#ifdef ASSERT_PDIR_SANITY
	if (sba_check_pdir(ioc,"Check before sba_map_sg()"))
	{
		sba_dump_sg(ioc, sglist, nents);
		panic("Check before sba_map_sg()");
	}
#endif

#ifdef SBA_COLLECT_STATS
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
	coalesced = iommu_coalesce_chunks(ioc, dev, sglist, nents, sba_alloc_range);

	/*
	** Program the I/O Pdir
	**
	** map the virtual addresses to the I/O Pdir
	** o dma_address will contain the pdir index
	** o dma_len will contain the number of bytes to map 
	** o address contains the virtual address.
	*/
	filled = iommu_fill_pdir(ioc, sglist, nents, 0, sba_io_pdir_entry);

	/* force FDC ops in io_pdir_entry() to be visible to IOMMU */
	asm_io_sync();

#ifdef ASSERT_PDIR_SANITY
	if (sba_check_pdir(ioc,"Check after sba_map_sg()"))
	{
		sba_dump_sg(ioc, sglist, nents);
		panic("Check after sba_map_sg()\n");
	}
#endif

	spin_unlock_irqrestore(&ioc->res_lock, flags);

	DBG_RUN_SG("%s() DONE %d mappings\n", __func__, filled);

	return filled;
}


/**
 * sba_unmap_sg - unmap Scatter/Gather list
 * @dev: instance of PCI owned by the driver that's asking.
 * @sglist:  array of buffer/length pairs
 * @nents:  number of entries in list
 * @direction:  R/W or both.
 *
 * See Documentation/DMA-API-HOWTO.txt
 */
static void 
sba_unmap_sg(struct device *dev, struct scatterlist *sglist, int nents,
	     enum dma_data_direction direction, unsigned long attrs)
{
	struct ioc *ioc;
#ifdef ASSERT_PDIR_SANITY
	unsigned long flags;
#endif

	DBG_RUN_SG("%s() START %d entries,  %p,%x\n",
		__func__, nents, sg_virt(sglist), sglist->length);

	ioc = GET_IOC(dev);
	if (!ioc) {
		WARN_ON(!ioc);
		return;
	}

#ifdef SBA_COLLECT_STATS
	ioc->usg_calls++;
#endif

#ifdef ASSERT_PDIR_SANITY
	spin_lock_irqsave(&ioc->res_lock, flags);
	sba_check_pdir(ioc,"Check before sba_unmap_sg()");
	spin_unlock_irqrestore(&ioc->res_lock, flags);
#endif

	while (sg_dma_len(sglist) && nents--) {

		sba_unmap_page(dev, sg_dma_address(sglist), sg_dma_len(sglist),
				direction, 0);
#ifdef SBA_COLLECT_STATS
		ioc->usg_pages += ((sg_dma_address(sglist) & ~IOVP_MASK) + sg_dma_len(sglist) + IOVP_SIZE - 1) >> PAGE_SHIFT;
		ioc->usingle_calls--;	/* kluge since call is unmap_sg() */
#endif
		++sglist;
	}

	DBG_RUN_SG("%s() DONE (nents %d)\n", __func__,  nents);

#ifdef ASSERT_PDIR_SANITY
	spin_lock_irqsave(&ioc->res_lock, flags);
	sba_check_pdir(ioc,"Check after sba_unmap_sg()");
	spin_unlock_irqrestore(&ioc->res_lock, flags);
#endif

}

static const struct dma_map_ops sba_ops = {
	.dma_supported =	sba_dma_supported,
	.alloc =		sba_alloc,
	.free =			sba_free,
	.map_page =		sba_map_page,
	.unmap_page =		sba_unmap_page,
	.map_sg =		sba_map_sg,
	.unmap_sg =		sba_unmap_sg,
};


/**************************************************************************
**
**   SBA PAT PDC support
**
**   o call pdc_pat_cell_module()
**   o store ranges in PCI "resource" structures
**
**************************************************************************/

static void
sba_get_pat_resources(struct sba_device *sba_dev)
{
#if 0
/*
** TODO/REVISIT/FIXME: support for directed ranges requires calls to
**      PAT PDC to program the SBA/LBA directed range registers...this
**      burden may fall on the LBA code since it directly supports the
**      PCI subsystem. It's not clear yet. - ggg
*/
PAT_MOD(mod)->mod_info.mod_pages   = PAT_GET_MOD_PAGES(temp);
	FIXME : ???
PAT_MOD(mod)->mod_info.dvi         = PAT_GET_DVI(temp);
	Tells where the dvi bits are located in the address.
PAT_MOD(mod)->mod_info.ioc         = PAT_GET_IOC(temp);
	FIXME : ???
#endif
}


/**************************************************************
*
*   Initialization and claim
*
***************************************************************/
#define PIRANHA_ADDR_MASK	0x00160000UL /* bit 17,18,20 */
#define PIRANHA_ADDR_VAL	0x00060000UL /* bit 17,18 on */
static void *
sba_alloc_pdir(unsigned int pdir_size)
{
        unsigned long pdir_base;
	unsigned long pdir_order = get_order(pdir_size);

	pdir_base = __get_free_pages(GFP_KERNEL, pdir_order);
	if (NULL == (void *) pdir_base)	{
		panic("%s() could not allocate I/O Page Table\n",
			__func__);
	}

	/* If this is not PA8700 (PCX-W2)
	**	OR newer than ver 2.2
	**	OR in a system that doesn't need VINDEX bits from SBA,
	**
	** then we aren't exposed to the HW bug.
	*/
	if ( ((boot_cpu_data.pdc.cpuid >> 5) & 0x7f) != 0x13
			|| (boot_cpu_data.pdc.versions > 0x202)
			|| (boot_cpu_data.pdc.capabilities & 0x08L) )
		return (void *) pdir_base;

	/*
	 * PA8700 (PCX-W2, aka piranha) silent data corruption fix
	 *
	 * An interaction between PA8700 CPU (Ver 2.2 or older) and
	 * Ike/Astro can cause silent data corruption. This is only
	 * a problem if the I/O PDIR is located in memory such that
	 * (little-endian)  bits 17 and 18 are on and bit 20 is off.
	 *
	 * Since the max IO Pdir size is 2MB, by cleverly allocating the
	 * right physical address, we can either avoid (IOPDIR <= 1MB)
	 * or minimize (2MB IO Pdir) the problem if we restrict the
	 * IO Pdir to a maximum size of 2MB-128K (1902K).
	 *
	 * Because we always allocate 2^N sized IO pdirs, either of the
	 * "bad" regions will be the last 128K if at all. That's easy
	 * to test for.
	 * 
	 */
	if (pdir_order <= (19-12)) {
		if (((virt_to_phys(pdir_base)+pdir_size-1) & PIRANHA_ADDR_MASK) == PIRANHA_ADDR_VAL) {
			/* allocate a new one on 512k alignment */
			unsigned long new_pdir = __get_free_pages(GFP_KERNEL, (19-12));
			/* release original */
			free_pages(pdir_base, pdir_order);

			pdir_base = new_pdir;

			/* release excess */
			while (pdir_order < (19-12)) {
				new_pdir += pdir_size;
				free_pages(new_pdir, pdir_order);
				pdir_order +=1;
				pdir_size <<=1;
			}
		}
	} else {
		/*
		** 1MB or 2MB Pdir
		** Needs to be aligned on an "odd" 1MB boundary.
		*/
		unsigned long new_pdir = __get_free_pages(GFP_KERNEL, pdir_order+1); /* 2 or 4MB */

		/* release original */
		free_pages( pdir_base, pdir_order);

		/* release first 1MB */
		free_pages(new_pdir, 20-12);

		pdir_base = new_pdir + 1024*1024;

		if (pdir_order > (20-12)) {
			/*
			** 2MB Pdir.
			**
			** Flag tells init_bitmap() to mark bad 128k as used
			** and to reduce the size by 128k.
			*/
			piranha_bad_128k = 1;

			new_pdir += 3*1024*1024;
			/* release last 1MB */
			free_pages(new_pdir, 20-12);

			/* release unusable 128KB */
			free_pages(new_pdir - 128*1024 , 17-12);

			pdir_size -= 128*1024;
		}
	}

	memset((void *) pdir_base, 0, pdir_size);
	return (void *) pdir_base;
}

struct ibase_data_struct {
	struct ioc *ioc;
	int ioc_num;
};

static int setup_ibase_imask_callback(struct device *dev, void *data)
{
	/* lba_set_iregs() is in drivers/parisc/lba_pci.c */
        extern void lba_set_iregs(struct parisc_device *, u32, u32);
	struct parisc_device *lba = to_parisc_device(dev);
	struct ibase_data_struct *ibd = data;
	int rope_num = (lba->hpa.start >> 13) & 0xf;
	if (rope_num >> 3 == ibd->ioc_num)
		lba_set_iregs(lba, ibd->ioc->ibase, ibd->ioc->imask);
	return 0;
}

/* setup Mercury or Elroy IBASE/IMASK registers. */
static void 
setup_ibase_imask(struct parisc_device *sba, struct ioc *ioc, int ioc_num)
{
	struct ibase_data_struct ibase_data = {
		.ioc		= ioc,
		.ioc_num	= ioc_num,
	};

	device_for_each_child(&sba->dev, &ibase_data,
			      setup_ibase_imask_callback);
}

#ifdef SBA_AGP_SUPPORT
static int
sba_ioc_find_quicksilver(struct device *dev, void *data)
{
	int *agp_found = data;
	struct parisc_device *lba = to_parisc_device(dev);

	if (IS_QUICKSILVER(lba))
		*agp_found = 1;
	return 0;
}
#endif

static void
sba_ioc_init_pluto(struct parisc_device *sba, struct ioc *ioc, int ioc_num)
{
	u32 iova_space_mask;
	u32 iova_space_size;
	int iov_order, tcnfg;
#ifdef SBA_AGP_SUPPORT
	int agp_found = 0;
#endif
	/*
	** Firmware programs the base and size of a "safe IOVA space"
	** (one that doesn't overlap memory or LMMIO space) in the
	** IBASE and IMASK registers.
	*/
	ioc->ibase = READ_REG(ioc->ioc_hpa + IOC_IBASE);
	iova_space_size = ~(READ_REG(ioc->ioc_hpa + IOC_IMASK) & 0xFFFFFFFFUL) + 1;

	if ((ioc->ibase < 0xfed00000UL) && ((ioc->ibase + iova_space_size) > 0xfee00000UL)) {
		printk("WARNING: IOV space overlaps local config and interrupt message, truncating\n");
		iova_space_size /= 2;
	}

	/*
	** iov_order is always based on a 1GB IOVA space since we want to
	** turn on the other half for AGP GART.
	*/
	iov_order = get_order(iova_space_size >> (IOVP_SHIFT - PAGE_SHIFT));
	ioc->pdir_size = (iova_space_size / IOVP_SIZE) * sizeof(u64);

	DBG_INIT("%s() hpa 0x%p IOV %dMB (%d bits)\n",
		__func__, ioc->ioc_hpa, iova_space_size >> 20,
		iov_order + PAGE_SHIFT);

	ioc->pdir_base = (void *) __get_free_pages(GFP_KERNEL,
						   get_order(ioc->pdir_size));
	if (!ioc->pdir_base)
		panic("Couldn't allocate I/O Page Table\n");

	memset(ioc->pdir_base, 0, ioc->pdir_size);

	DBG_INIT("%s() pdir %p size %x\n",
			__func__, ioc->pdir_base, ioc->pdir_size);

#ifdef SBA_HINT_SUPPORT
	ioc->hint_shift_pdir = iov_order + PAGE_SHIFT;
	ioc->hint_mask_pdir = ~(0x3 << (iov_order + PAGE_SHIFT));

	DBG_INIT("	hint_shift_pdir %x hint_mask_pdir %lx\n",
		ioc->hint_shift_pdir, ioc->hint_mask_pdir);
#endif

	WARN_ON((((unsigned long) ioc->pdir_base) & PAGE_MASK) != (unsigned long) ioc->pdir_base);
	WRITE_REG(virt_to_phys(ioc->pdir_base), ioc->ioc_hpa + IOC_PDIR_BASE);

	/* build IMASK for IOC and Elroy */
	iova_space_mask =  0xffffffff;
	iova_space_mask <<= (iov_order + PAGE_SHIFT);
	ioc->imask = iova_space_mask;
#ifdef ZX1_SUPPORT
	ioc->iovp_mask = ~(iova_space_mask + PAGE_SIZE - 1);
#endif
	sba_dump_tlb(ioc->ioc_hpa);

	setup_ibase_imask(sba, ioc, ioc_num);

	WRITE_REG(ioc->imask, ioc->ioc_hpa + IOC_IMASK);

#ifdef CONFIG_64BIT
	/*
	** Setting the upper bits makes checking for bypass addresses
	** a little faster later on.
	*/
	ioc->imask |= 0xFFFFFFFF00000000UL;
#endif

	/* Set I/O PDIR Page size to system page size */
	switch (PAGE_SHIFT) {
		case 12: tcnfg = 0; break;	/*  4K */
		case 13: tcnfg = 1; break;	/*  8K */
		case 14: tcnfg = 2; break;	/* 16K */
		case 16: tcnfg = 3; break;	/* 64K */
		default:
			panic(__FILE__ "Unsupported system page size %d",
				1 << PAGE_SHIFT);
			break;
	}
	WRITE_REG(tcnfg, ioc->ioc_hpa + IOC_TCNFG);

	/*
	** Program the IOC's ibase and enable IOVA translation
	** Bit zero == enable bit.
	*/
	WRITE_REG(ioc->ibase | 1, ioc->ioc_hpa + IOC_IBASE);

	/*
	** Clear I/O TLB of any possible entries.
	** (Yes. This is a bit paranoid...but so what)
	*/
	WRITE_REG(ioc->ibase | 31, ioc->ioc_hpa + IOC_PCOM);

#ifdef SBA_AGP_SUPPORT

	/*
	** If an AGP device is present, only use half of the IOV space
	** for PCI DMA.  Unfortunately we can't know ahead of time
	** whether GART support will actually be used, for now we
	** can just key on any AGP device found in the system.
	** We program the next pdir index after we stop w/ a key for
	** the GART code to handshake on.
	*/
	device_for_each_child(&sba->dev, &agp_found, sba_ioc_find_quicksilver);

	if (agp_found && sba_reserve_agpgart) {
		printk(KERN_INFO "%s: reserving %dMb of IOVA space for agpgart\n",
		       __func__, (iova_space_size/2) >> 20);
		ioc->pdir_size /= 2;
		ioc->pdir_base[PDIR_INDEX(iova_space_size/2)] = SBA_AGPGART_COOKIE;
	}
#endif /*SBA_AGP_SUPPORT*/
}

static void
sba_ioc_init(struct parisc_device *sba, struct ioc *ioc, int ioc_num)
{
	u32 iova_space_size, iova_space_mask;
	unsigned int pdir_size, iov_order, tcnfg;

	/*
	** Determine IOVA Space size from memory size.
	**
	** Ideally, PCI drivers would register the maximum number
	** of DMA they can have outstanding for each device they
	** own.  Next best thing would be to guess how much DMA
	** can be outstanding based on PCI Class/sub-class. Both
	** methods still require some "extra" to support PCI
	** Hot-Plug/Removal of PCI cards. (aka PCI OLARD).
	**
	** While we have 32-bits "IOVA" space, top two 2 bits are used
	** for DMA hints - ergo only 30 bits max.
	*/

	iova_space_size = (u32) (totalram_pages()/global_ioc_cnt);

	/* limit IOVA space size to 1MB-1GB */
	if (iova_space_size < (1 << (20 - PAGE_SHIFT))) {
		iova_space_size = 1 << (20 - PAGE_SHIFT);
	}
	else if (iova_space_size > (1 << (30 - PAGE_SHIFT))) {
		iova_space_size = 1 << (30 - PAGE_SHIFT);
	}

	/*
	** iova space must be log2() in size.
	** thus, pdir/res_map will also be log2().
	** PIRANHA BUG: Exception is when IO Pdir is 2MB (gets reduced)
	*/
	iov_order = get_order(iova_space_size << PAGE_SHIFT);

	/* iova_space_size is now bytes, not pages */
	iova_space_size = 1 << (iov_order + PAGE_SHIFT);

	ioc->pdir_size = pdir_size = (iova_space_size/IOVP_SIZE) * sizeof(u64);

	DBG_INIT("%s() hpa 0x%lx mem %ldMB IOV %dMB (%d bits)\n",
			__func__,
			ioc->ioc_hpa,
			(unsigned long) totalram_pages() >> (20 - PAGE_SHIFT),
			iova_space_size>>20,
			iov_order + PAGE_SHIFT);

	ioc->pdir_base = sba_alloc_pdir(pdir_size);

	DBG_INIT("%s() pdir %p size %x\n",
			__func__, ioc->pdir_base, pdir_size);

#ifdef SBA_HINT_SUPPORT
	/* FIXME : DMA HINTs not used */
	ioc->hint_shift_pdir = iov_order + PAGE_SHIFT;
	ioc->hint_mask_pdir = ~(0x3 << (iov_order + PAGE_SHIFT));

	DBG_INIT("	hint_shift_pdir %x hint_mask_pdir %lx\n",
			ioc->hint_shift_pdir, ioc->hint_mask_pdir);
#endif

	WRITE_REG64(virt_to_phys(ioc->pdir_base), ioc->ioc_hpa + IOC_PDIR_BASE);

	/* build IMASK for IOC and Elroy */
	iova_space_mask =  0xffffffff;
	iova_space_mask <<= (iov_order + PAGE_SHIFT);

	/*
	** On C3000 w/512MB mem, HP-UX 10.20 reports:
	**     ibase=0, imask=0xFE000000, size=0x2000000.
	*/
	ioc->ibase = 0;
	ioc->imask = iova_space_mask;	/* save it */
#ifdef ZX1_SUPPORT
	ioc->iovp_mask = ~(iova_space_mask + PAGE_SIZE - 1);
#endif

	DBG_INIT("%s() IOV base 0x%lx mask 0x%0lx\n",
		__func__, ioc->ibase, ioc->imask);

	/*
	** FIXME: Hint registers are programmed with default hint
	** values during boot, so hints should be sane even if we
	** can't reprogram them the way drivers want.
	*/

	setup_ibase_imask(sba, ioc, ioc_num);

	/*
	** Program the IOC's ibase and enable IOVA translation
	*/
	WRITE_REG(ioc->ibase | 1, ioc->ioc_hpa+IOC_IBASE);
	WRITE_REG(ioc->imask, ioc->ioc_hpa+IOC_IMASK);

	/* Set I/O PDIR Page size to system page size */
	switch (PAGE_SHIFT) {
		case 12: tcnfg = 0; break;	/*  4K */
		case 13: tcnfg = 1; break;	/*  8K */
		case 14: tcnfg = 2; break;	/* 16K */
		case 16: tcnfg = 3; break;	/* 64K */
		default:
			panic(__FILE__ "Unsupported system page size %d",
				1 << PAGE_SHIFT);
			break;
	}
	/* Set I/O PDIR Page size to PAGE_SIZE (4k/16k/...) */
	WRITE_REG(tcnfg, ioc->ioc_hpa+IOC_TCNFG);

	/*
	** Clear I/O TLB of any possible entries.
	** (Yes. This is a bit paranoid...but so what)
	*/
	WRITE_REG(0 | 31, ioc->ioc_hpa+IOC_PCOM);

	ioc->ibase = 0; /* used by SBA_IOVA and related macros */	

	DBG_INIT("%s() DONE\n", __func__);
}



/**************************************************************************
**
**   SBA initialization code (HW and SW)
**
**   o identify SBA chip itself
**   o initialize SBA chip modes (HardFail)
**   o initialize SBA chip modes (HardFail)
**   o FIXME: initialize DMA hints for reasonable defaults
**
**************************************************************************/

static void __iomem *ioc_remap(struct sba_device *sba_dev, unsigned int offset)
{
	return ioremap_nocache(sba_dev->dev->hpa.start + offset, SBA_FUNC_SIZE);
}

static void sba_hw_init(struct sba_device *sba_dev)
{ 
	int i;
	int num_ioc;
	u64 ioc_ctl;

	if (!is_pdc_pat()) {
		/* Shutdown the USB controller on Astro-based workstations.
		** Once we reprogram the IOMMU, the next DMA performed by
		** USB will HPMC the box. USB is only enabled if a
		** keyboard is present and found.
		**
		** With serial console, j6k v5.0 firmware says:
		**   mem_kbd hpa 0xfee003f8 sba 0x0 pad 0x0 cl_class 0x7
		**
		** FIXME: Using GFX+USB console at power up but direct
		**	linux to serial console is still broken.
		**	USB could generate DMA so we must reset USB.
		**	The proper sequence would be:
		**	o block console output
		**	o reset USB device
		**	o reprogram serial port
		**	o unblock console output
		*/
		if (PAGE0->mem_kbd.cl_class == CL_KEYBD) {
			pdc_io_reset_devices();
		}

	}


#if 0
printk("sba_hw_init(): mem_boot 0x%x 0x%x 0x%x 0x%x\n", PAGE0->mem_boot.hpa,
	PAGE0->mem_boot.spa, PAGE0->mem_boot.pad, PAGE0->mem_boot.cl_class);

	/*
	** Need to deal with DMA from LAN.
	**	Maybe use page zero boot device as a handle to talk
	**	to PDC about which device to shutdown.
	**
	** Netbooting, j6k v5.0 firmware says:
	** 	mem_boot hpa 0xf4008000 sba 0x0 pad 0x0 cl_class 0x1002
	** ARGH! invalid class.
	*/
	if ((PAGE0->mem_boot.cl_class != CL_RANDOM)
		&& (PAGE0->mem_boot.cl_class != CL_SEQU)) {
			pdc_io_reset();
	}
#endif

	if (!IS_PLUTO(sba_dev->dev)) {
		ioc_ctl = READ_REG(sba_dev->sba_hpa+IOC_CTRL);
		DBG_INIT("%s() hpa 0x%lx ioc_ctl 0x%Lx ->",
			__func__, sba_dev->sba_hpa, ioc_ctl);
		ioc_ctl &= ~(IOC_CTRL_RM | IOC_CTRL_NC | IOC_CTRL_CE);
		ioc_ctl |= IOC_CTRL_DD | IOC_CTRL_D4 | IOC_CTRL_TC;
			/* j6700 v1.6 firmware sets 0x294f */
			/* A500 firmware sets 0x4d */

		WRITE_REG(ioc_ctl, sba_dev->sba_hpa+IOC_CTRL);

#ifdef DEBUG_SBA_INIT
		ioc_ctl = READ_REG64(sba_dev->sba_hpa+IOC_CTRL);
		DBG_INIT(" 0x%Lx\n", ioc_ctl);
#endif
	} /* if !PLUTO */

	if (IS_ASTRO(sba_dev->dev)) {
		int err;
		sba_dev->ioc[0].ioc_hpa = ioc_remap(sba_dev, ASTRO_IOC_OFFSET);
		num_ioc = 1;

		sba_dev->chip_resv.name = "Astro Intr Ack";
		sba_dev->chip_resv.start = PCI_F_EXTEND | 0xfef00000UL;
		sba_dev->chip_resv.end   = PCI_F_EXTEND | (0xff000000UL - 1) ;
		err = request_resource(&iomem_resource, &(sba_dev->chip_resv));
		BUG_ON(err < 0);

	} else if (IS_PLUTO(sba_dev->dev)) {
		int err;

		sba_dev->ioc[0].ioc_hpa = ioc_remap(sba_dev, PLUTO_IOC_OFFSET);
		num_ioc = 1;

		sba_dev->chip_resv.name = "Pluto Intr/PIOP/VGA";
		sba_dev->chip_resv.start = PCI_F_EXTEND | 0xfee00000UL;
		sba_dev->chip_resv.end   = PCI_F_EXTEND | (0xff200000UL - 1);
		err = request_resource(&iomem_resource, &(sba_dev->chip_resv));
		WARN_ON(err < 0);

		sba_dev->iommu_resv.name = "IOVA Space";
		sba_dev->iommu_resv.start = 0x40000000UL;
		sba_dev->iommu_resv.end   = 0x50000000UL - 1;
		err = request_resource(&iomem_resource, &(sba_dev->iommu_resv));
		WARN_ON(err < 0);
	} else {
		/* IKE, REO */
		sba_dev->ioc[0].ioc_hpa = ioc_remap(sba_dev, IKE_IOC_OFFSET(0));
		sba_dev->ioc[1].ioc_hpa = ioc_remap(sba_dev, IKE_IOC_OFFSET(1));
		num_ioc = 2;

		/* TODO - LOOKUP Ike/Stretch chipset mem map */
	}
	/* XXX: What about Reo Grande? */

	sba_dev->num_ioc = num_ioc;
	for (i = 0; i < num_ioc; i++) {
		void __iomem *ioc_hpa = sba_dev->ioc[i].ioc_hpa;
		unsigned int j;

		for (j=0; j < sizeof(u64) * ROPES_PER_IOC; j+=sizeof(u64)) {

			/*
			 * Clear ROPE(N)_CONFIG AO bit.
			 * Disables "NT Ordering" (~= !"Relaxed Ordering")
			 * Overrides bit 1 in DMA Hint Sets.
			 * Improves netperf UDP_STREAM by ~10% for bcm5701.
			 */
			if (IS_PLUTO(sba_dev->dev)) {
				void __iomem *rope_cfg;
				unsigned long cfg_val;

				rope_cfg = ioc_hpa + IOC_ROPE0_CFG + j;
				cfg_val = READ_REG(rope_cfg);
				cfg_val &= ~IOC_ROPE_AO;
				WRITE_REG(cfg_val, rope_cfg);
			}

			/*
			** Make sure the box crashes on rope errors.
			*/
			WRITE_REG(HF_ENABLE, ioc_hpa + ROPE0_CTL + j);
		}

		/* flush out the last writes */
		READ_REG(sba_dev->ioc[i].ioc_hpa + ROPE7_CTL);

		DBG_INIT("	ioc[%d] ROPE_CFG 0x%Lx  ROPE_DBG 0x%Lx\n",
				i,
				READ_REG(sba_dev->ioc[i].ioc_hpa + 0x40),
				READ_REG(sba_dev->ioc[i].ioc_hpa + 0x50)
			);
		DBG_INIT("	STATUS_CONTROL 0x%Lx  FLUSH_CTRL 0x%Lx\n",
				READ_REG(sba_dev->ioc[i].ioc_hpa + 0x108),
				READ_REG(sba_dev->ioc[i].ioc_hpa + 0x400)
			);

		if (IS_PLUTO(sba_dev->dev)) {
			sba_ioc_init_pluto(sba_dev->dev, &(sba_dev->ioc[i]), i);
		} else {
			sba_ioc_init(sba_dev->dev, &(sba_dev->ioc[i]), i);
		}
	}
}

static void
sba_common_init(struct sba_device *sba_dev)
{
	int i;

	/* add this one to the head of the list (order doesn't matter)
	** This will be useful for debugging - especially if we get coredumps
	*/
	sba_dev->next = sba_list;
	sba_list = sba_dev;

	for(i=0; i< sba_dev->num_ioc; i++) {
		int res_size;
#ifdef DEBUG_DMB_TRAP
		extern void iterate_pages(unsigned long , unsigned long ,
					  void (*)(pte_t * , unsigned long),
					  unsigned long );
		void set_data_memory_break(pte_t * , unsigned long);
#endif
		/* resource map size dictated by pdir_size */
		res_size = sba_dev->ioc[i].pdir_size/sizeof(u64); /* entries */

		/* Second part of PIRANHA BUG */
		if (piranha_bad_128k) {
			res_size -= (128*1024)/sizeof(u64);
		}

		res_size >>= 3;  /* convert bit count to byte count */
		DBG_INIT("%s() res_size 0x%x\n",
			__func__, res_size);

		sba_dev->ioc[i].res_size = res_size;
		sba_dev->ioc[i].res_map = (char *) __get_free_pages(GFP_KERNEL, get_order(res_size));

#ifdef DEBUG_DMB_TRAP
		iterate_pages( sba_dev->ioc[i].res_map, res_size,
				set_data_memory_break, 0);
#endif

		if (NULL == sba_dev->ioc[i].res_map)
		{
			panic("%s:%s() could not allocate resource map\n",
			      __FILE__, __func__ );
		}

		memset(sba_dev->ioc[i].res_map, 0, res_size);
		/* next available IOVP - circular search */
		sba_dev->ioc[i].res_hint = (unsigned long *)
				&(sba_dev->ioc[i].res_map[L1_CACHE_BYTES]);

#ifdef ASSERT_PDIR_SANITY
		/* Mark first bit busy - ie no IOVA 0 */
		sba_dev->ioc[i].res_map[0] = 0x80;
		sba_dev->ioc[i].pdir_base[0] = 0xeeffc0addbba0080ULL;
#endif

		/* Third (and last) part of PIRANHA BUG */
		if (piranha_bad_128k) {
			/* region from +1408K to +1536 is un-usable. */

			int idx_start = (1408*1024/sizeof(u64)) >> 3;
			int idx_end   = (1536*1024/sizeof(u64)) >> 3;
			long *p_start = (long *) &(sba_dev->ioc[i].res_map[idx_start]);
			long *p_end   = (long *) &(sba_dev->ioc[i].res_map[idx_end]);

			/* mark that part of the io pdir busy */
			while (p_start < p_end)
				*p_start++ = -1;
				
		}

#ifdef DEBUG_DMB_TRAP
		iterate_pages( sba_dev->ioc[i].res_map, res_size,
				set_data_memory_break, 0);
		iterate_pages( sba_dev->ioc[i].pdir_base, sba_dev->ioc[i].pdir_size,
				set_data_memory_break, 0);
#endif

		DBG_INIT("%s() %d res_map %x %p\n",
			__func__, i, res_size, sba_dev->ioc[i].res_map);
	}

	spin_lock_init(&sba_dev->sba_lock);
	ioc_needs_fdc = boot_cpu_data.pdc.capabilities & PDC_MODEL_IOPDIR_FDC;

#ifdef DEBUG_SBA_INIT
	/*
	 * If the PDC_MODEL capabilities has Non-coherent IO-PDIR bit set
	 * (bit #61, big endian), we have to flush and sync every time
	 * IO-PDIR is changed in Ike/Astro.
	 */
	if (ioc_needs_fdc) {
		printk(KERN_INFO MODULE_NAME " FDC/SYNC required.\n");
	} else {
		printk(KERN_INFO MODULE_NAME " IOC has cache coherent PDIR.\n");
	}
#endif
}

#ifdef CONFIG_PROC_FS
static int sba_proc_info(struct seq_file *m, void *p)
{
	struct sba_device *sba_dev = sba_list;
	struct ioc *ioc = &sba_dev->ioc[0];	/* FIXME: Multi-IOC support! */
	int total_pages = (int) (ioc->res_size << 3); /* 8 bits per byte */
#ifdef SBA_COLLECT_STATS
	unsigned long avg = 0, min, max;
#endif
	int i;

	seq_printf(m, "%s rev %d.%d\n",
		   sba_dev->name,
		   (sba_dev->hw_rev & 0x7) + 1,
		   (sba_dev->hw_rev & 0x18) >> 3);
	seq_printf(m, "IO PDIR size    : %d bytes (%d entries)\n",
		   (int)((ioc->res_size << 3) * sizeof(u64)), /* 8 bits/byte */
		   total_pages);

	seq_printf(m, "Resource bitmap : %d bytes (%d pages)\n",
		   ioc->res_size, ioc->res_size << 3);   /* 8 bits per byte */

	seq_printf(m, "LMMIO_BASE/MASK/ROUTE %08x %08x %08x\n",
		   READ_REG32(sba_dev->sba_hpa + LMMIO_DIST_BASE),
		   READ_REG32(sba_dev->sba_hpa + LMMIO_DIST_MASK),
		   READ_REG32(sba_dev->sba_hpa + LMMIO_DIST_ROUTE));

	for (i=0; i<4; i++)
		seq_printf(m, "DIR%d_BASE/MASK/ROUTE %08x %08x %08x\n",
			   i,
			   READ_REG32(sba_dev->sba_hpa + LMMIO_DIRECT0_BASE  + i*0x18),
			   READ_REG32(sba_dev->sba_hpa + LMMIO_DIRECT0_MASK  + i*0x18),
			   READ_REG32(sba_dev->sba_hpa + LMMIO_DIRECT0_ROUTE + i*0x18));

#ifdef SBA_COLLECT_STATS
	seq_printf(m, "IO PDIR entries : %ld free  %ld used (%d%%)\n",
		   total_pages - ioc->used_pages, ioc->used_pages,
		   (int)(ioc->used_pages * 100 / total_pages));

	min = max = ioc->avg_search[0];
	for (i = 0; i < SBA_SEARCH_SAMPLE; i++) {
		avg += ioc->avg_search[i];
		if (ioc->avg_search[i] > max) max = ioc->avg_search[i];
		if (ioc->avg_search[i] < min) min = ioc->avg_search[i];
	}
	avg /= SBA_SEARCH_SAMPLE;
	seq_printf(m, "  Bitmap search : %ld/%ld/%ld (min/avg/max CPU Cycles)\n",
		   min, avg, max);

	seq_printf(m, "pci_map_single(): %12ld calls  %12ld pages (avg %d/1000)\n",
		   ioc->msingle_calls, ioc->msingle_pages,
		   (int)((ioc->msingle_pages * 1000)/ioc->msingle_calls));

	/* KLUGE - unmap_sg calls unmap_single for each mapped page */
	min = ioc->usingle_calls;
	max = ioc->usingle_pages - ioc->usg_pages;
	seq_printf(m, "pci_unmap_single: %12ld calls  %12ld pages (avg %d/1000)\n",
		   min, max, (int)((max * 1000)/min));

	seq_printf(m, "pci_map_sg()    : %12ld calls  %12ld pages (avg %d/1000)\n",
		   ioc->msg_calls, ioc->msg_pages,
		   (int)((ioc->msg_pages * 1000)/ioc->msg_calls));

	seq_printf(m, "pci_unmap_sg()  : %12ld calls  %12ld pages (avg %d/1000)\n",
		   ioc->usg_calls, ioc->usg_pages,
		   (int)((ioc->usg_pages * 1000)/ioc->usg_calls));
#endif

	return 0;
}

static int
sba_proc_bitmap_info(struct seq_file *m, void *p)
{
	struct sba_device *sba_dev = sba_list;
	struct ioc *ioc = &sba_dev->ioc[0];	/* FIXME: Multi-IOC support! */

	seq_hex_dump(m, "   ", DUMP_PREFIX_NONE, 32, 4, ioc->res_map,
		     ioc->res_size, false);
	seq_putc(m, '\n');

	return 0;
}
#endif /* CONFIG_PROC_FS */

static const struct parisc_device_id sba_tbl[] __initconst = {
	{ HPHW_IOA, HVERSION_REV_ANY_ID, ASTRO_RUNWAY_PORT, 0xb },
	{ HPHW_BCPORT, HVERSION_REV_ANY_ID, IKE_MERCED_PORT, 0xc },
	{ HPHW_BCPORT, HVERSION_REV_ANY_ID, REO_MERCED_PORT, 0xc },
	{ HPHW_BCPORT, HVERSION_REV_ANY_ID, REOG_MERCED_PORT, 0xc },
	{ HPHW_IOA, HVERSION_REV_ANY_ID, PLUTO_MCKINLEY_PORT, 0xc },
	{ 0, }
};

static int sba_driver_callback(struct parisc_device *);

static struct parisc_driver sba_driver __refdata = {
	.name =		MODULE_NAME,
	.id_table =	sba_tbl,
	.probe =	sba_driver_callback,
};

/*
** Determine if sba should claim this chip (return 0) or not (return 1).
** If so, initialize the chip and tell other partners in crime they
** have work to do.
*/
static int __init sba_driver_callback(struct parisc_device *dev)
{
	struct sba_device *sba_dev;
	u32 func_class;
	int i;
	char *version;
	void __iomem *sba_addr = ioremap_nocache(dev->hpa.start, SBA_FUNC_SIZE);
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *root;
#endif

	sba_dump_ranges(sba_addr);

	/* Read HW Rev First */
	func_class = READ_REG(sba_addr + SBA_FCLASS);

	if (IS_ASTRO(dev)) {
		unsigned long fclass;
		static char astro_rev[]="Astro ?.?";

		/* Astro is broken...Read HW Rev First */
		fclass = READ_REG(sba_addr);

		astro_rev[6] = '1' + (char) (fclass & 0x7);
		astro_rev[8] = '0' + (char) ((fclass & 0x18) >> 3);
		version = astro_rev;

	} else if (IS_IKE(dev)) {
		static char ike_rev[] = "Ike rev ?";
		ike_rev[8] = '0' + (char) (func_class & 0xff);
		version = ike_rev;
	} else if (IS_PLUTO(dev)) {
		static char pluto_rev[]="Pluto ?.?";
		pluto_rev[6] = '0' + (char) ((func_class & 0xf0) >> 4); 
		pluto_rev[8] = '0' + (char) (func_class & 0x0f); 
		version = pluto_rev;
	} else {
		static char reo_rev[] = "REO rev ?";
		reo_rev[8] = '0' + (char) (func_class & 0xff);
		version = reo_rev;
	}

	if (!global_ioc_cnt) {
		global_ioc_cnt = count_parisc_driver(&sba_driver);

		/* Astro and Pluto have one IOC per SBA */
		if ((!IS_ASTRO(dev)) || (!IS_PLUTO(dev)))
			global_ioc_cnt *= 2;
	}

	printk(KERN_INFO "%s found %s at 0x%llx\n",
		MODULE_NAME, version, (unsigned long long)dev->hpa.start);

	sba_dev = kzalloc(sizeof(struct sba_device), GFP_KERNEL);
	if (!sba_dev) {
		printk(KERN_ERR MODULE_NAME " - couldn't alloc sba_device\n");
		return -ENOMEM;
	}

	parisc_set_drvdata(dev, sba_dev);

	for(i=0; i<MAX_IOC; i++)
		spin_lock_init(&(sba_dev->ioc[i].res_lock));

	sba_dev->dev = dev;
	sba_dev->hw_rev = func_class;
	sba_dev->name = dev->name;
	sba_dev->sba_hpa = sba_addr;

	sba_get_pat_resources(sba_dev);
	sba_hw_init(sba_dev);
	sba_common_init(sba_dev);

	hppa_dma_ops = &sba_ops;

#ifdef CONFIG_PROC_FS
	switch (dev->id.hversion) {
	case PLUTO_MCKINLEY_PORT:
		root = proc_mckinley_root;
		break;
	case ASTRO_RUNWAY_PORT:
	case IKE_MERCED_PORT:
	default:
		root = proc_runway_root;
		break;
	}

	proc_create_single("sba_iommu", 0, root, sba_proc_info);
	proc_create_single("sba_iommu-bitmap", 0, root, sba_proc_bitmap_info);
#endif
	return 0;
}

/*
** One time initialization to let the world know the SBA was found.
** This is the only routine which is NOT static.
** Must be called exactly once before pci_init().
*/
void __init sba_init(void)
{
	register_parisc_driver(&sba_driver);
}


/**
 * sba_get_iommu - Assign the iommu pointer for the pci bus controller.
 * @dev: The parisc device.
 *
 * Returns the appropriate IOMMU data for the given parisc PCI controller.
 * This is cached and used later for PCI DMA Mapping.
 */
void * sba_get_iommu(struct parisc_device *pci_hba)
{
	struct parisc_device *sba_dev = parisc_parent(pci_hba);
	struct sba_device *sba = dev_get_drvdata(&sba_dev->dev);
	char t = sba_dev->id.hw_type;
	int iocnum = (pci_hba->hw_path >> 3);	/* rope # */

	WARN_ON((t != HPHW_IOA) && (t != HPHW_BCPORT));

	return &(sba->ioc[iocnum]);
}


/**
 * sba_directed_lmmio - return first directed LMMIO range routed to rope
 * @pa_dev: The parisc device.
 * @r: resource PCI host controller wants start/end fields assigned.
 *
 * For the given parisc PCI controller, determine if any direct ranges
 * are routed down the corresponding rope.
 */
void sba_directed_lmmio(struct parisc_device *pci_hba, struct resource *r)
{
	struct parisc_device *sba_dev = parisc_parent(pci_hba);
	struct sba_device *sba = dev_get_drvdata(&sba_dev->dev);
	char t = sba_dev->id.hw_type;
	int i;
	int rope = (pci_hba->hw_path & (ROPES_PER_IOC-1));  /* rope # */

	BUG_ON((t!=HPHW_IOA) && (t!=HPHW_BCPORT));

	r->start = r->end = 0;

	/* Astro has 4 directed ranges. Not sure about Ike/Pluto/et al */
	for (i=0; i<4; i++) {
		int base, size;
		void __iomem *reg = sba->sba_hpa + i*0x18;

		base = READ_REG32(reg + LMMIO_DIRECT0_BASE);
		if ((base & 1) == 0)
			continue;	/* not enabled */

		size = READ_REG32(reg + LMMIO_DIRECT0_ROUTE);

		if ((size & (ROPES_PER_IOC-1)) != rope)
			continue;	/* directed down different rope */
		
		r->start = (base & ~1UL) | PCI_F_EXTEND;
		size = ~ READ_REG32(reg + LMMIO_DIRECT0_MASK);
		r->end = r->start + size;
		r->flags = IORESOURCE_MEM;
	}
}


/**
 * sba_distributed_lmmio - return portion of distributed LMMIO range
 * @pa_dev: The parisc device.
 * @r: resource PCI host controller wants start/end fields assigned.
 *
 * For the given parisc PCI controller, return portion of distributed LMMIO
 * range. The distributed LMMIO is always present and it's just a question
 * of the base address and size of the range.
 */
void sba_distributed_lmmio(struct parisc_device *pci_hba, struct resource *r )
{
	struct parisc_device *sba_dev = parisc_parent(pci_hba);
	struct sba_device *sba = dev_get_drvdata(&sba_dev->dev);
	char t = sba_dev->id.hw_type;
	int base, size;
	int rope = (pci_hba->hw_path & (ROPES_PER_IOC-1));  /* rope # */

	BUG_ON((t!=HPHW_IOA) && (t!=HPHW_BCPORT));

	r->start = r->end = 0;

	base = READ_REG32(sba->sba_hpa + LMMIO_DIST_BASE);
	if ((base & 1) == 0) {
		BUG();	/* Gah! Distr Range wasn't enabled! */
		return;
	}

	r->start = (base & ~1UL) | PCI_F_EXTEND;

	size = (~READ_REG32(sba->sba_hpa + LMMIO_DIST_MASK)) / ROPES_PER_IOC;
	r->start += rope * (size + 1);	/* adjust base for this rope */
	r->end = r->start + size;
	r->flags = IORESOURCE_MEM;
}

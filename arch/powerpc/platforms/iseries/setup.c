/*
 *    Copyright (c) 2000 Mike Corrigan <mikejc@us.ibm.com>
 *    Copyright (c) 1999-2000 Grant Erickson <grant@lcse.umn.edu>
 *
 *    Description:
 *      Architecture- / platform-specific boot-time initialization code for
 *      the IBM iSeries LPAR.  Adapted from original code by Grant Erickson and
 *      code by Gary Thomas, Cort Dougan <cort@fsmlabs.com>, and Dan Malek
 *      <dan@net4x.com>.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#undef DEBUG

#include <linux/init.h>
#include <linux/threads.h>
#include <linux/smp.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/seq_file.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/root_dev.h>
#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/tick.h>

#include <asm/processor.h>
#include <asm/machdep.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/mmu_context.h>
#include <asm/cputable.h>
#include <asm/sections.h>
#include <asm/iommu.h>
#include <asm/firmware.h>
#include <asm/system.h>
#include <asm/time.h>
#include <asm/paca.h>
#include <asm/cache.h>
#include <asm/abs_addr.h>
#include <asm/iseries/hv_lp_config.h>
#include <asm/iseries/hv_call_event.h>
#include <asm/iseries/hv_call_xm.h>
#include <asm/iseries/it_lp_queue.h>
#include <asm/iseries/mf.h>
#include <asm/iseries/hv_lp_event.h>
#include <asm/iseries/lpar_map.h>
#include <asm/udbg.h>
#include <asm/irq.h>

#include "naca.h"
#include "setup.h"
#include "irq.h"
#include "vpd_areas.h"
#include "processor_vpd.h"
#include "it_lp_naca.h"
#include "main_store.h"
#include "call_sm.h"
#include "call_hpt.h"

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

/* Function Prototypes */
static unsigned long build_iSeries_Memory_Map(void);
static void iseries_shared_idle(void);
static void iseries_dedicated_idle(void);
#ifdef CONFIG_PCI
extern void iSeries_pci_final_fixup(void);
#else
static void iSeries_pci_final_fixup(void) { }
#endif


struct MemoryBlock {
	unsigned long absStart;
	unsigned long absEnd;
	unsigned long logicalStart;
	unsigned long logicalEnd;
};

/*
 * Process the main store vpd to determine where the holes in memory are
 * and return the number of physical blocks and fill in the array of
 * block data.
 */
static unsigned long iSeries_process_Condor_mainstore_vpd(
		struct MemoryBlock *mb_array, unsigned long max_entries)
{
	unsigned long holeFirstChunk, holeSizeChunks;
	unsigned long numMemoryBlocks = 1;
	struct IoHriMainStoreSegment4 *msVpd =
		(struct IoHriMainStoreSegment4 *)xMsVpd;
	unsigned long holeStart = msVpd->nonInterleavedBlocksStartAdr;
	unsigned long holeEnd = msVpd->nonInterleavedBlocksEndAdr;
	unsigned long holeSize = holeEnd - holeStart;

	printk("Mainstore_VPD: Condor\n");
	/*
	 * Determine if absolute memory has any
	 * holes so that we can interpret the
	 * access map we get back from the hypervisor
	 * correctly.
	 */
	mb_array[0].logicalStart = 0;
	mb_array[0].logicalEnd = 0x100000000;
	mb_array[0].absStart = 0;
	mb_array[0].absEnd = 0x100000000;

	if (holeSize) {
		numMemoryBlocks = 2;
		holeStart = holeStart & 0x000fffffffffffff;
		holeStart = addr_to_chunk(holeStart);
		holeFirstChunk = holeStart;
		holeSize = addr_to_chunk(holeSize);
		holeSizeChunks = holeSize;
		printk( "Main store hole: start chunk = %0lx, size = %0lx chunks\n",
				holeFirstChunk, holeSizeChunks );
		mb_array[0].logicalEnd = holeFirstChunk;
		mb_array[0].absEnd = holeFirstChunk;
		mb_array[1].logicalStart = holeFirstChunk;
		mb_array[1].logicalEnd = 0x100000000 - holeSizeChunks;
		mb_array[1].absStart = holeFirstChunk + holeSizeChunks;
		mb_array[1].absEnd = 0x100000000;
	}
	return numMemoryBlocks;
}

#define MaxSegmentAreas			32
#define MaxSegmentAdrRangeBlocks	128
#define MaxAreaRangeBlocks		4

static unsigned long iSeries_process_Regatta_mainstore_vpd(
		struct MemoryBlock *mb_array, unsigned long max_entries)
{
	struct IoHriMainStoreSegment5 *msVpdP =
		(struct IoHriMainStoreSegment5 *)xMsVpd;
	unsigned long numSegmentBlocks = 0;
	u32 existsBits = msVpdP->msAreaExists;
	unsigned long area_num;

	printk("Mainstore_VPD: Regatta\n");

	for (area_num = 0; area_num < MaxSegmentAreas; ++area_num ) {
		unsigned long numAreaBlocks;
		struct IoHriMainStoreArea4 *currentArea;

		if (existsBits & 0x80000000) {
			unsigned long block_num;

			currentArea = &msVpdP->msAreaArray[area_num];
			numAreaBlocks = currentArea->numAdrRangeBlocks;
			printk("ms_vpd: processing area %2ld  blocks=%ld",
					area_num, numAreaBlocks);
			for (block_num = 0; block_num < numAreaBlocks;
					++block_num ) {
				/* Process an address range block */
				struct MemoryBlock tempBlock;
				unsigned long i;

				tempBlock.absStart =
					(unsigned long)currentArea->xAdrRangeBlock[block_num].blockStart;
				tempBlock.absEnd =
					(unsigned long)currentArea->xAdrRangeBlock[block_num].blockEnd;
				tempBlock.logicalStart = 0;
				tempBlock.logicalEnd   = 0;
				printk("\n          block %ld absStart=%016lx absEnd=%016lx",
						block_num, tempBlock.absStart,
						tempBlock.absEnd);

				for (i = 0; i < numSegmentBlocks; ++i) {
					if (mb_array[i].absStart ==
							tempBlock.absStart)
						break;
				}
				if (i == numSegmentBlocks) {
					if (numSegmentBlocks == max_entries)
						panic("iSeries_process_mainstore_vpd: too many memory blocks");
					mb_array[numSegmentBlocks] = tempBlock;
					++numSegmentBlocks;
				} else
					printk(" (duplicate)");
			}
			printk("\n");
		}
		existsBits <<= 1;
	}
	/* Now sort the blocks found into ascending sequence */
	if (numSegmentBlocks > 1) {
		unsigned long m, n;

		for (m = 0; m < numSegmentBlocks - 1; ++m) {
			for (n = numSegmentBlocks - 1; m < n; --n) {
				if (mb_array[n].absStart <
						mb_array[n-1].absStart) {
					struct MemoryBlock tempBlock;

					tempBlock = mb_array[n];
					mb_array[n] = mb_array[n-1];
					mb_array[n-1] = tempBlock;
				}
			}
		}
	}
	/*
	 * Assign "logical" addresses to each block.  These
	 * addresses correspond to the hypervisor "bitmap" space.
	 * Convert all addresses into units of 256K chunks.
	 */
	{
	unsigned long i, nextBitmapAddress;

	printk("ms_vpd: %ld sorted memory blocks\n", numSegmentBlocks);
	nextBitmapAddress = 0;
	for (i = 0; i < numSegmentBlocks; ++i) {
		unsigned long length = mb_array[i].absEnd -
			mb_array[i].absStart;

		mb_array[i].logicalStart = nextBitmapAddress;
		mb_array[i].logicalEnd = nextBitmapAddress + length;
		nextBitmapAddress += length;
		printk("          Bitmap range: %016lx - %016lx\n"
				"        Absolute range: %016lx - %016lx\n",
				mb_array[i].logicalStart,
				mb_array[i].logicalEnd,
				mb_array[i].absStart, mb_array[i].absEnd);
		mb_array[i].absStart = addr_to_chunk(mb_array[i].absStart &
				0x000fffffffffffff);
		mb_array[i].absEnd = addr_to_chunk(mb_array[i].absEnd &
				0x000fffffffffffff);
		mb_array[i].logicalStart =
			addr_to_chunk(mb_array[i].logicalStart);
		mb_array[i].logicalEnd = addr_to_chunk(mb_array[i].logicalEnd);
	}
	}

	return numSegmentBlocks;
}

static unsigned long iSeries_process_mainstore_vpd(struct MemoryBlock *mb_array,
		unsigned long max_entries)
{
	unsigned long i;
	unsigned long mem_blocks = 0;

	if (cpu_has_feature(CPU_FTR_SLB))
		mem_blocks = iSeries_process_Regatta_mainstore_vpd(mb_array,
				max_entries);
	else
		mem_blocks = iSeries_process_Condor_mainstore_vpd(mb_array,
				max_entries);

	printk("Mainstore_VPD: numMemoryBlocks = %ld \n", mem_blocks);
	for (i = 0; i < mem_blocks; ++i) {
		printk("Mainstore_VPD: block %3ld logical chunks %016lx - %016lx\n"
		       "                             abs chunks %016lx - %016lx\n",
			i, mb_array[i].logicalStart, mb_array[i].logicalEnd,
			mb_array[i].absStart, mb_array[i].absEnd);
	}
	return mem_blocks;
}

static void __init iSeries_get_cmdline(void)
{
	char *p, *q;

	/* copy the command line parameter from the primary VSP  */
	HvCallEvent_dmaToSp(cmd_line, 2 * 64* 1024, 256,
			HvLpDma_Direction_RemoteToLocal);

	p = cmd_line;
	q = cmd_line + 255;
	while(p < q) {
		if (!*p || *p == '\n')
			break;
		++p;
	}
	*p = 0;
}

static void __init iSeries_init_early(void)
{
	DBG(" -> iSeries_init_early()\n");

	/* Snapshot the timebase, for use in later recalibration */
	iSeries_time_init_early();

	/*
	 * Initialize the DMA/TCE management
	 */
	iommu_init_early_iSeries();

	/* Initialize machine-dependency vectors */
#ifdef CONFIG_SMP
	smp_init_iSeries();
#endif

	/* Associate Lp Event Queue 0 with processor 0 */
	HvCallEvent_setLpEventQueueInterruptProc(0, 0);

	mf_init();

	DBG(" <- iSeries_init_early()\n");
}

struct mschunks_map mschunks_map = {
	/* XXX We don't use these, but Piranha might need them. */
	.chunk_size  = MSCHUNKS_CHUNK_SIZE,
	.chunk_shift = MSCHUNKS_CHUNK_SHIFT,
	.chunk_mask  = MSCHUNKS_OFFSET_MASK,
};
EXPORT_SYMBOL(mschunks_map);

void mschunks_alloc(unsigned long num_chunks)
{
	klimit = _ALIGN(klimit, sizeof(u32));
	mschunks_map.mapping = (u32 *)klimit;
	klimit += num_chunks * sizeof(u32);
	mschunks_map.num_chunks = num_chunks;
}

/*
 * The iSeries may have very large memories ( > 128 GB ) and a partition
 * may get memory in "chunks" that may be anywhere in the 2**52 real
 * address space.  The chunks are 256K in size.  To map this to the
 * memory model Linux expects, the AS/400 specific code builds a
 * translation table to translate what Linux thinks are "physical"
 * addresses to the actual real addresses.  This allows us to make
 * it appear to Linux that we have contiguous memory starting at
 * physical address zero while in fact this could be far from the truth.
 * To avoid confusion, I'll let the words physical and/or real address
 * apply to the Linux addresses while I'll use "absolute address" to
 * refer to the actual hardware real address.
 *
 * build_iSeries_Memory_Map gets information from the Hypervisor and
 * looks at the Main Store VPD to determine the absolute addresses
 * of the memory that has been assigned to our partition and builds
 * a table used to translate Linux's physical addresses to these
 * absolute addresses.  Absolute addresses are needed when
 * communicating with the hypervisor (e.g. to build HPT entries)
 *
 * Returns the physical memory size
 */

static unsigned long __init build_iSeries_Memory_Map(void)
{
	u32 loadAreaFirstChunk, loadAreaLastChunk, loadAreaSize;
	u32 nextPhysChunk;
	u32 hptFirstChunk, hptLastChunk, hptSizeChunks, hptSizePages;
	u32 totalChunks,moreChunks;
	u32 currChunk, thisChunk, absChunk;
	u32 currDword;
	u32 chunkBit;
	u64 map;
	struct MemoryBlock mb[32];
	unsigned long numMemoryBlocks, curBlock;

	/* Chunk size on iSeries is 256K bytes */
	totalChunks = (u32)HvLpConfig_getMsChunks();
	mschunks_alloc(totalChunks);

	/*
	 * Get absolute address of our load area
	 * and map it to physical address 0
	 * This guarantees that the loadarea ends up at physical 0
	 * otherwise, it might not be returned by PLIC as the first
	 * chunks
	 */

	loadAreaFirstChunk = (u32)addr_to_chunk(itLpNaca.xLoadAreaAddr);
	loadAreaSize =  itLpNaca.xLoadAreaChunks;

	/*
	 * Only add the pages already mapped here.
	 * Otherwise we might add the hpt pages
	 * The rest of the pages of the load area
	 * aren't in the HPT yet and can still
	 * be assigned an arbitrary physical address
	 */
	if ((loadAreaSize * 64) > HvPagesToMap)
		loadAreaSize = HvPagesToMap / 64;

	loadAreaLastChunk = loadAreaFirstChunk + loadAreaSize - 1;

	/*
	 * TODO Do we need to do something if the HPT is in the 64MB load area?
	 * This would be required if the itLpNaca.xLoadAreaChunks includes
	 * the HPT size
	 */

	printk("Mapping load area - physical addr = 0000000000000000\n"
		"                    absolute addr = %016lx\n",
		chunk_to_addr(loadAreaFirstChunk));
	printk("Load area size %dK\n", loadAreaSize * 256);

	for (nextPhysChunk = 0; nextPhysChunk < loadAreaSize; ++nextPhysChunk)
		mschunks_map.mapping[nextPhysChunk] =
			loadAreaFirstChunk + nextPhysChunk;

	/*
	 * Get absolute address of our HPT and remember it so
	 * we won't map it to any physical address
	 */
	hptFirstChunk = (u32)addr_to_chunk(HvCallHpt_getHptAddress());
	hptSizePages = (u32)HvCallHpt_getHptPages();
	hptSizeChunks = hptSizePages >>
		(MSCHUNKS_CHUNK_SHIFT - HW_PAGE_SHIFT);
	hptLastChunk = hptFirstChunk + hptSizeChunks - 1;

	printk("HPT absolute addr = %016lx, size = %dK\n",
			chunk_to_addr(hptFirstChunk), hptSizeChunks * 256);

	/*
	 * Determine if absolute memory has any
	 * holes so that we can interpret the
	 * access map we get back from the hypervisor
	 * correctly.
	 */
	numMemoryBlocks = iSeries_process_mainstore_vpd(mb, 32);

	/*
	 * Process the main store access map from the hypervisor
	 * to build up our physical -> absolute translation table
	 */
	curBlock = 0;
	currChunk = 0;
	currDword = 0;
	moreChunks = totalChunks;

	while (moreChunks) {
		map = HvCallSm_get64BitsOfAccessMap(itLpNaca.xLpIndex,
				currDword);
		thisChunk = currChunk;
		while (map) {
			chunkBit = map >> 63;
			map <<= 1;
			if (chunkBit) {
				--moreChunks;
				while (thisChunk >= mb[curBlock].logicalEnd) {
					++curBlock;
					if (curBlock >= numMemoryBlocks)
						panic("out of memory blocks");
				}
				if (thisChunk < mb[curBlock].logicalStart)
					panic("memory block error");

				absChunk = mb[curBlock].absStart +
					(thisChunk - mb[curBlock].logicalStart);
				if (((absChunk < hptFirstChunk) ||
				     (absChunk > hptLastChunk)) &&
				    ((absChunk < loadAreaFirstChunk) ||
				     (absChunk > loadAreaLastChunk))) {
					mschunks_map.mapping[nextPhysChunk] =
						absChunk;
					++nextPhysChunk;
				}
			}
			++thisChunk;
		}
		++currDword;
		currChunk += 64;
	}

	/*
	 * main store size (in chunks) is
	 *   totalChunks - hptSizeChunks
	 * which should be equal to
	 *   nextPhysChunk
	 */
	return chunk_to_addr(nextPhysChunk);
}

/*
 * Document me.
 */
static void __init iSeries_setup_arch(void)
{
	if (get_lppaca()->shared_proc) {
		ppc_md.idle_loop = iseries_shared_idle;
		printk(KERN_DEBUG "Using shared processor idle loop\n");
	} else {
		ppc_md.idle_loop = iseries_dedicated_idle;
		printk(KERN_DEBUG "Using dedicated idle loop\n");
	}

	/* Setup the Lp Event Queue */
	setup_hvlpevent_queue();

	printk("Max  logical processors = %d\n",
			itVpdAreas.xSlicMaxLogicalProcs);
	printk("Max physical processors = %d\n",
			itVpdAreas.xSlicMaxPhysicalProcs);
}

static void iSeries_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "machine\t\t: 64-bit iSeries Logical Partition\n");
}

static void __init iSeries_progress(char * st, unsigned short code)
{
	printk("Progress: [%04x] - %s\n", (unsigned)code, st);
	mf_display_progress(code);
}

static void __init iSeries_fixup_klimit(void)
{
	/*
	 * Change klimit to take into account any ram disk
	 * that may be included
	 */
	if (naca.xRamDisk)
		klimit = KERNELBASE + (u64)naca.xRamDisk +
			(naca.xRamDiskSize * HW_PAGE_SIZE);
}

static int __init iSeries_src_init(void)
{
        /* clear the progress line */
	if (firmware_has_feature(FW_FEATURE_ISERIES))
		ppc_md.progress(" ", 0xffff);
        return 0;
}

late_initcall(iSeries_src_init);

static inline void process_iSeries_events(void)
{
	asm volatile ("li 0,0x5555; sc" : : : "r0", "r3");
}

static void yield_shared_processor(void)
{
	unsigned long tb;

	HvCall_setEnabledInterrupts(HvCall_MaskIPI |
				    HvCall_MaskLpEvent |
				    HvCall_MaskLpProd |
				    HvCall_MaskTimeout);

	tb = get_tb();
	/* Compute future tb value when yield should expire */
	HvCall_yieldProcessor(HvCall_YieldTimed, tb+tb_ticks_per_jiffy);

	/*
	 * The decrementer stops during the yield.  Force a fake decrementer
	 * here and let the timer_interrupt code sort out the actual time.
	 */
	get_lppaca()->int_dword.fields.decr_int = 1;
	ppc64_runlatch_on();
	process_iSeries_events();
}

static void iseries_shared_idle(void)
{
	while (1) {
		tick_nohz_stop_sched_tick();
		while (!need_resched() && !hvlpevent_is_pending()) {
			local_irq_disable();
			ppc64_runlatch_off();

			/* Recheck with irqs off */
			if (!need_resched() && !hvlpevent_is_pending())
				yield_shared_processor();

			HMT_medium();
			local_irq_enable();
		}

		ppc64_runlatch_on();
		tick_nohz_restart_sched_tick();

		if (hvlpevent_is_pending())
			process_iSeries_events();

		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

static void iseries_dedicated_idle(void)
{
	set_thread_flag(TIF_POLLING_NRFLAG);

	while (1) {
		tick_nohz_stop_sched_tick();
		if (!need_resched()) {
			while (!need_resched()) {
				ppc64_runlatch_off();
				HMT_low();

				if (hvlpevent_is_pending()) {
					HMT_medium();
					ppc64_runlatch_on();
					process_iSeries_events();
				}
			}

			HMT_medium();
		}

		ppc64_runlatch_on();
		tick_nohz_restart_sched_tick();
		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

#ifndef CONFIG_PCI
void __init iSeries_init_IRQ(void) { }
#endif

static void __iomem *iseries_ioremap(phys_addr_t address, unsigned long size,
				     unsigned long flags)
{
	return (void __iomem *)address;
}

static void iseries_iounmap(volatile void __iomem *token)
{
}

static int __init iseries_probe(void)
{
	unsigned long root = of_get_flat_dt_root();
	if (!of_flat_dt_is_compatible(root, "IBM,iSeries"))
		return 0;

	hpte_init_iSeries();
	/* iSeries does not support 16M pages */
	cur_cpu_spec->cpu_features &= ~CPU_FTR_16M_PAGE;

	return 1;
}

define_machine(iseries) {
	.name		= "iSeries",
	.setup_arch	= iSeries_setup_arch,
	.show_cpuinfo	= iSeries_show_cpuinfo,
	.init_IRQ	= iSeries_init_IRQ,
	.get_irq	= iSeries_get_irq,
	.init_early	= iSeries_init_early,
	.pcibios_fixup	= iSeries_pci_final_fixup,
	.restart	= mf_reboot,
	.power_off	= mf_power_off,
	.halt		= mf_power_off,
	.get_boot_time	= iSeries_get_boot_time,
	.set_rtc_time	= iSeries_set_rtc_time,
	.get_rtc_time	= iSeries_get_rtc_time,
	.calibrate_decr	= generic_calibrate_decr,
	.progress	= iSeries_progress,
	.probe		= iseries_probe,
	.ioremap	= iseries_ioremap,
	.iounmap	= iseries_iounmap,
	/* XXX Implement enable_pmcs for iSeries */
};

void * __init iSeries_early_setup(void)
{
	unsigned long phys_mem_size;

	/* Identify CPU type. This is done again by the common code later
	 * on but calling this function multiple times is fine.
	 */
	identify_cpu(0, mfspr(SPRN_PVR));

	powerpc_firmware_features |= FW_FEATURE_ISERIES;
	powerpc_firmware_features |= FW_FEATURE_LPAR;

	iSeries_fixup_klimit();

	/*
	 * Initialize the table which translate Linux physical addresses to
	 * AS/400 absolute addresses
	 */
	phys_mem_size = build_iSeries_Memory_Map();

	iSeries_get_cmdline();

	return (void *) __pa(build_flat_dt(phys_mem_size));
}

static void hvputc(char c)
{
	if (c == '\n')
		hvputc('\r');

	HvCall_writeLogBuffer(&c, 1);
}

void __init udbg_init_iseries(void)
{
	udbg_putc = hvputc;
}

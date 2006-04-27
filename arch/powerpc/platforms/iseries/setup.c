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

#include <linux/config.h>
#include <linux/init.h>
#include <linux/threads.h>
#include <linux/smp.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/initrd.h>
#include <linux/seq_file.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/root_dev.h>
#include <linux/kernel.h>
#include <linux/if_ether.h>	/* ETH_ALEN */

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
#include <asm/sections.h>
#include <asm/abs_addr.h>
#include <asm/iseries/hv_types.h>
#include <asm/iseries/hv_lp_config.h>
#include <asm/iseries/hv_call_event.h>
#include <asm/iseries/hv_call_xm.h>
#include <asm/iseries/it_lp_queue.h>
#include <asm/iseries/mf.h>
#include <asm/iseries/it_exp_vpd_panel.h>
#include <asm/iseries/hv_lp_event.h>
#include <asm/iseries/lpar_map.h>
#include <asm/udbg.h>
#include <asm/irq.h>

#include "naca.h"
#include "setup.h"
#include "irq.h"
#include "vpd_areas.h"
#include "processor_vpd.h"
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

extern int rd_size;		/* Defined in drivers/block/rd.c */
extern unsigned long embedded_sysmap_start;
extern unsigned long embedded_sysmap_end;

extern unsigned long iSeries_recal_tb;
extern unsigned long iSeries_recal_titan;

static unsigned long cmd_mem_limit;

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

	ppc64_interrupt_controller = IC_ISERIES;

#if defined(CONFIG_BLK_DEV_INITRD)
	/*
	 * If the init RAM disk has been configured and there is
	 * a non-zero starting address for it, set it up
	 */
	if (naca.xRamDisk) {
		initrd_start = (unsigned long)__va(naca.xRamDisk);
		initrd_end = initrd_start + naca.xRamDiskSize * HW_PAGE_SIZE;
		initrd_below_start_ok = 1;	// ramdisk in kernel space
		ROOT_DEV = Root_RAM0;
		if (((rd_size * 1024) / HW_PAGE_SIZE) < naca.xRamDiskSize)
			rd_size = (naca.xRamDiskSize * HW_PAGE_SIZE) / 1024;
	} else
#endif /* CONFIG_BLK_DEV_INITRD */
	{
	    /* ROOT_DEV = MKDEV(VIODASD_MAJOR, 1); */
	}

	iSeries_recal_tb = get_tb();
	iSeries_recal_titan = HvCallXm_loadTod();

	/*
	 * Initialize the hash table management pointers
	 */
	hpte_init_iSeries();

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

	/* If we were passed an initrd, set the ROOT_DEV properly if the values
	 * look sensible. If not, clear initrd reference.
	 */
#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start >= KERNELBASE && initrd_end >= KERNELBASE &&
	    initrd_end > initrd_start)
		ROOT_DEV = Root_RAM0;
	else
		initrd_start = initrd_end = 0;
#endif /* CONFIG_BLK_DEV_INITRD */

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
	else {
		/*
		 * No ram disk was included - check and see if there
		 * was an embedded system map.  Change klimit to take
		 * into account any embedded system map
		 */
		if (embedded_sysmap_end)
			klimit = KERNELBASE + ((embedded_sysmap_end + 4095) &
					0xfffffffffffff000);
	}
}

static int __init iSeries_src_init(void)
{
        /* clear the progress line */
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
		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

#ifndef CONFIG_PCI
void __init iSeries_init_IRQ(void) { }
#endif

static int __init iseries_probe(void)
{
	unsigned long root = of_get_flat_dt_root();
	if (!of_flat_dt_is_compatible(root, "IBM,iSeries"))
		return 0;

	powerpc_firmware_features |= FW_FEATURE_ISERIES;
	powerpc_firmware_features |= FW_FEATURE_LPAR;

	/*
	 * The Hypervisor only allows us up to 256 interrupt
	 * sources (the irq number is passed in a u8).
	 */
	virt_irq_max = 255;

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
	/* XXX Implement enable_pmcs for iSeries */
};

struct blob {
	unsigned char data[PAGE_SIZE * 2];
	unsigned long next;
};

struct iseries_flat_dt {
	struct boot_param_header header;
	u64 reserve_map[2];
	struct blob dt;
	struct blob strings;
};

struct iseries_flat_dt iseries_dt;

void dt_init(struct iseries_flat_dt *dt)
{
	dt->header.off_mem_rsvmap =
		offsetof(struct iseries_flat_dt, reserve_map);
	dt->header.off_dt_struct = offsetof(struct iseries_flat_dt, dt);
	dt->header.off_dt_strings = offsetof(struct iseries_flat_dt, strings);
	dt->header.totalsize = sizeof(struct iseries_flat_dt);
	dt->header.dt_strings_size = sizeof(struct blob);

	/* There is no notion of hardware cpu id on iSeries */
	dt->header.boot_cpuid_phys = smp_processor_id();

	dt->dt.next = (unsigned long)&dt->dt.data;
	dt->strings.next = (unsigned long)&dt->strings.data;

	dt->header.magic = OF_DT_HEADER;
	dt->header.version = 0x10;
	dt->header.last_comp_version = 0x10;

	dt->reserve_map[0] = 0;
	dt->reserve_map[1] = 0;
}

void dt_check_blob(struct blob *b)
{
	if (b->next >= (unsigned long)&b->next) {
		DBG("Ran out of space in flat device tree blob!\n");
		BUG();
	}
}

void dt_push_u32(struct iseries_flat_dt *dt, u32 value)
{
	*((u32*)dt->dt.next) = value;
	dt->dt.next += sizeof(u32);

	dt_check_blob(&dt->dt);
}

void dt_push_u64(struct iseries_flat_dt *dt, u64 value)
{
	*((u64*)dt->dt.next) = value;
	dt->dt.next += sizeof(u64);

	dt_check_blob(&dt->dt);
}

unsigned long dt_push_bytes(struct blob *blob, char *data, int len)
{
	unsigned long start = blob->next - (unsigned long)blob->data;

	memcpy((char *)blob->next, data, len);
	blob->next = _ALIGN(blob->next + len, 4);

	dt_check_blob(blob);

	return start;
}

void dt_start_node(struct iseries_flat_dt *dt, char *name)
{
	dt_push_u32(dt, OF_DT_BEGIN_NODE);
	dt_push_bytes(&dt->dt, name, strlen(name) + 1);
}

#define dt_end_node(dt) dt_push_u32(dt, OF_DT_END_NODE)

void dt_prop(struct iseries_flat_dt *dt, char *name, char *data, int len)
{
	unsigned long offset;

	dt_push_u32(dt, OF_DT_PROP);

	/* Length of the data */
	dt_push_u32(dt, len);

	/* Put the property name in the string blob. */
	offset = dt_push_bytes(&dt->strings, name, strlen(name) + 1);

	/* The offset of the properties name in the string blob. */
	dt_push_u32(dt, (u32)offset);

	/* The actual data. */
	dt_push_bytes(&dt->dt, data, len);
}

void dt_prop_str(struct iseries_flat_dt *dt, char *name, char *data)
{
	dt_prop(dt, name, data, strlen(data) + 1); /* + 1 for NULL */
}

void dt_prop_u32(struct iseries_flat_dt *dt, char *name, u32 data)
{
	dt_prop(dt, name, (char *)&data, sizeof(u32));
}

void dt_prop_u64(struct iseries_flat_dt *dt, char *name, u64 data)
{
	dt_prop(dt, name, (char *)&data, sizeof(u64));
}

void dt_prop_u64_list(struct iseries_flat_dt *dt, char *name, u64 *data, int n)
{
	dt_prop(dt, name, (char *)data, sizeof(u64) * n);
}

void dt_prop_u32_list(struct iseries_flat_dt *dt, char *name, u32 *data, int n)
{
	dt_prop(dt, name, (char *)data, sizeof(u32) * n);
}

void dt_prop_empty(struct iseries_flat_dt *dt, char *name)
{
	dt_prop(dt, name, NULL, 0);
}

void dt_cpus(struct iseries_flat_dt *dt)
{
	unsigned char buf[32];
	unsigned char *p;
	unsigned int i, index;
	struct IoHriProcessorVpd *d;
	u32 pft_size[2];

	/* yuck */
	snprintf(buf, 32, "PowerPC,%s", cur_cpu_spec->cpu_name);
	p = strchr(buf, ' ');
	if (!p) p = buf + strlen(buf);

	dt_start_node(dt, "cpus");
	dt_prop_u32(dt, "#address-cells", 1);
	dt_prop_u32(dt, "#size-cells", 0);

	pft_size[0] = 0; /* NUMA CEC cookie, 0 for non NUMA  */
	pft_size[1] = __ilog2(HvCallHpt_getHptPages() * HW_PAGE_SIZE);

	for (i = 0; i < NR_CPUS; i++) {
		if (lppaca[i].dyn_proc_status >= 2)
			continue;

		snprintf(p, 32 - (p - buf), "@%d", i);
		dt_start_node(dt, buf);

		dt_prop_str(dt, "device_type", "cpu");

		index = lppaca[i].dyn_hv_phys_proc_index;
		d = &xIoHriProcessorVpd[index];

		dt_prop_u32(dt, "i-cache-size", d->xInstCacheSize * 1024);
		dt_prop_u32(dt, "i-cache-line-size", d->xInstCacheOperandSize);

		dt_prop_u32(dt, "d-cache-size", d->xDataL1CacheSizeKB * 1024);
		dt_prop_u32(dt, "d-cache-line-size", d->xDataCacheOperandSize);

		/* magic conversions to Hz copied from old code */
		dt_prop_u32(dt, "clock-frequency",
			((1UL << 34) * 1000000) / d->xProcFreq);
		dt_prop_u32(dt, "timebase-frequency",
			((1UL << 32) * 1000000) / d->xTimeBaseFreq);

		dt_prop_u32(dt, "reg", i);

		dt_prop_u32_list(dt, "ibm,pft-size", pft_size, 2);

		dt_end_node(dt);
	}

	dt_end_node(dt);
}

void dt_model(struct iseries_flat_dt *dt)
{
	char buf[16] = "IBM,";

	/* "IBM," + mfgId[2:3] + systemSerial[1:5] */
	strne2a(buf + 4, xItExtVpdPanel.mfgID + 2, 2);
	strne2a(buf + 6, xItExtVpdPanel.systemSerial + 1, 5);
	buf[11] = '\0';
	dt_prop_str(dt, "system-id", buf);

	/* "IBM," + machineType[0:4] */
	strne2a(buf + 4, xItExtVpdPanel.machineType, 4);
	buf[8] = '\0';
	dt_prop_str(dt, "model", buf);

	dt_prop_str(dt, "compatible", "IBM,iSeries");
}

void dt_vdevices(struct iseries_flat_dt *dt)
{
	u32 reg = 0;
	HvLpIndexMap vlan_map;
	int i;
	char buf[32];

	dt_start_node(dt, "vdevice");
	dt_prop_u32(dt, "#address-cells", 1);
	dt_prop_u32(dt, "#size-cells", 0);

	snprintf(buf, sizeof(buf), "viocons@%08x", reg);
	dt_start_node(dt, buf);
	dt_prop_str(dt, "device_type", "serial");
	dt_prop_empty(dt, "compatible");
	dt_prop_u32(dt, "reg", reg);
	dt_end_node(dt);
	reg++;

	snprintf(buf, sizeof(buf), "v-scsi@%08x", reg);
	dt_start_node(dt, buf);
	dt_prop_str(dt, "device_type", "vscsi");
	dt_prop_str(dt, "compatible", "IBM,v-scsi");
	dt_prop_u32(dt, "reg", reg);
	dt_end_node(dt);
	reg++;

	vlan_map = HvLpConfig_getVirtualLanIndexMap();
	for (i = 0; i < HVMAXARCHITECTEDVIRTUALLANS; i++) {
		unsigned char mac_addr[ETH_ALEN];

		if ((vlan_map & (0x8000 >> i)) == 0)
			continue;
		snprintf(buf, 32, "vlan@%08x", reg + i);
		dt_start_node(dt, buf);
		dt_prop_str(dt, "device_type", "vlan");
		dt_prop_empty(dt, "compatible");
		dt_prop_u32(dt, "reg", reg + i);
		dt_prop_u32(dt, "linux,unit_address", i);

		mac_addr[0] = 0x02;
		mac_addr[1] = 0x01;
		mac_addr[2] = 0xff;
		mac_addr[3] = i;
		mac_addr[4] = 0xff;
		mac_addr[5] = HvLpConfig_getLpIndex_outline();
		dt_prop(dt, "local-mac-address", (char *)mac_addr, ETH_ALEN);
		dt_prop(dt, "mac-address", (char *)mac_addr, ETH_ALEN);

		dt_end_node(dt);
	}
	reg += HVMAXARCHITECTEDVIRTUALLANS;

	for (i = 0; i < HVMAXARCHITECTEDVIRTUALDISKS; i++) {
		snprintf(buf, 32, "viodasd@%08x", reg + i);
		dt_start_node(dt, buf);
		dt_prop_str(dt, "device_type", "viodasd");
		dt_prop_empty(dt, "compatible");
		dt_prop_u32(dt, "reg", reg + i);
		dt_prop_u32(dt, "linux,unit_address", i);
		dt_end_node(dt);
	}
	reg += HVMAXARCHITECTEDVIRTUALDISKS;
	for (i = 0; i < HVMAXARCHITECTEDVIRTUALCDROMS; i++) {
		snprintf(buf, 32, "viocd@%08x", reg + i);
		dt_start_node(dt, buf);
		dt_prop_str(dt, "device_type", "viocd");
		dt_prop_empty(dt, "compatible");
		dt_prop_u32(dt, "reg", reg + i);
		dt_prop_u32(dt, "linux,unit_address", i);
		dt_end_node(dt);
	}
	reg += HVMAXARCHITECTEDVIRTUALCDROMS;
	for (i = 0; i < HVMAXARCHITECTEDVIRTUALTAPES; i++) {
		snprintf(buf, 32, "viotape@%08x", reg + i);
		dt_start_node(dt, buf);
		dt_prop_str(dt, "device_type", "viotape");
		dt_prop_empty(dt, "compatible");
		dt_prop_u32(dt, "reg", reg + i);
		dt_prop_u32(dt, "linux,unit_address", i);
		dt_end_node(dt);
	}

	dt_end_node(dt);
}

void build_flat_dt(struct iseries_flat_dt *dt, unsigned long phys_mem_size)
{
	u64 tmp[2];

	dt_init(dt);

	dt_start_node(dt, "");

	dt_prop_u32(dt, "#address-cells", 2);
	dt_prop_u32(dt, "#size-cells", 2);
	dt_model(dt);

	/* /memory */
	dt_start_node(dt, "memory@0");
	dt_prop_str(dt, "name", "memory");
	dt_prop_str(dt, "device_type", "memory");
	tmp[0] = 0;
	tmp[1] = phys_mem_size;
	dt_prop_u64_list(dt, "reg", tmp, 2);
	dt_end_node(dt);

	/* /chosen */
	dt_start_node(dt, "chosen");
	dt_prop_str(dt, "bootargs", cmd_line);
	if (cmd_mem_limit)
		dt_prop_u64(dt, "linux,memory-limit", cmd_mem_limit);
	dt_end_node(dt);

	dt_cpus(dt);

	dt_vdevices(dt);

	dt_end_node(dt);

	dt_push_u32(dt, OF_DT_END);
}

void * __init iSeries_early_setup(void)
{
	unsigned long phys_mem_size;

	iSeries_fixup_klimit();

	/*
	 * Initialize the table which translate Linux physical addresses to
	 * AS/400 absolute addresses
	 */
	phys_mem_size = build_iSeries_Memory_Map();

	iSeries_get_cmdline();

	/* Save unparsed command line copy for /proc/cmdline */
	strlcpy(saved_command_line, cmd_line, COMMAND_LINE_SIZE);

	/* Parse early parameters, in particular mem=x */
	parse_early_param();

	build_flat_dt(&iseries_dt, phys_mem_size);

	return (void *) __pa(&iseries_dt);
}

/*
 * On iSeries we just parse the mem=X option from the command line.
 * On pSeries it's a bit more complicated, see prom_init_mem()
 */
static int __init early_parsemem(char *p)
{
	if (p)
		cmd_mem_limit = ALIGN(memparse(p, &p), PAGE_SIZE);
	return 0;
}
early_param("mem", early_parsemem);

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

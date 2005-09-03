/*
 *    Copyright (c) 2000 Mike Corrigan <mikejc@us.ibm.com>
 *    Copyright (c) 1999-2000 Grant Erickson <grant@lcse.umn.edu>
 *
 *    Module name: iSeries_setup.c
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

#include <asm/time.h>
#include "iSeries_setup.h"
#include <asm/naca.h>
#include <asm/paca.h>
#include <asm/cache.h>
#include <asm/sections.h>
#include <asm/abs_addr.h>
#include <asm/iSeries/HvCallHpt.h>
#include <asm/iSeries/HvLpConfig.h>
#include <asm/iSeries/HvCallEvent.h>
#include <asm/iSeries/HvCallSm.h>
#include <asm/iSeries/HvCallXm.h>
#include <asm/iSeries/ItLpQueue.h>
#include <asm/iSeries/IoHriMainStore.h>
#include <asm/iSeries/mf.h>
#include <asm/iSeries/HvLpEvent.h>
#include <asm/iSeries/iSeries_irq.h>
#include <asm/iSeries/IoHriProcessorVpd.h>
#include <asm/iSeries/ItVpdAreas.h>
#include <asm/iSeries/LparMap.h>

extern void hvlog(char *fmt, ...);

#ifdef DEBUG
#define DBG(fmt...) hvlog(fmt)
#else
#define DBG(fmt...)
#endif

/* Function Prototypes */
extern void ppcdbg_initialize(void);

static void build_iSeries_Memory_Map(void);
static void setup_iSeries_cache_sizes(void);
static void iSeries_bolt_kernel(unsigned long saddr, unsigned long eaddr);
#ifdef CONFIG_PCI
extern void iSeries_pci_final_fixup(void);
#else
static void iSeries_pci_final_fixup(void) { }
#endif

/* Global Variables */
static unsigned long procFreqHz;
static unsigned long procFreqMhz;
static unsigned long procFreqMhzHundreths;

static unsigned long tbFreqHz;
static unsigned long tbFreqMhz;
static unsigned long tbFreqMhzHundreths;

int piranha_simulator;

extern int rd_size;		/* Defined in drivers/block/rd.c */
extern unsigned long klimit;
extern unsigned long embedded_sysmap_start;
extern unsigned long embedded_sysmap_end;

extern unsigned long iSeries_recal_tb;
extern unsigned long iSeries_recal_titan;

static int mf_initialized;

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
	extern unsigned long memory_limit;

	DBG(" -> iSeries_init_early()\n");

	ppc64_firmware_features = FW_FEATURE_ISERIES;

	ppcdbg_initialize();

#if defined(CONFIG_BLK_DEV_INITRD)
	/*
	 * If the init RAM disk has been configured and there is
	 * a non-zero starting address for it, set it up
	 */
	if (naca.xRamDisk) {
		initrd_start = (unsigned long)__va(naca.xRamDisk);
		initrd_end = initrd_start + naca.xRamDiskSize * PAGE_SIZE;
		initrd_below_start_ok = 1;	// ramdisk in kernel space
		ROOT_DEV = Root_RAM0;
		if (((rd_size * 1024) / PAGE_SIZE) < naca.xRamDiskSize)
			rd_size = (naca.xRamDiskSize * PAGE_SIZE) / 1024;
	} else
#endif /* CONFIG_BLK_DEV_INITRD */
	{
	    /* ROOT_DEV = MKDEV(VIODASD_MAJOR, 1); */
	}

	iSeries_recal_tb = get_tb();
	iSeries_recal_titan = HvCallXm_loadTod();

	/*
	 * Cache sizes must be initialized before hpte_init_iSeries is called
	 * as the later need them for flush_icache_range()
	 */
	setup_iSeries_cache_sizes();

	/*
	 * Initialize the hash table management pointers
	 */
	hpte_init_iSeries();

	/*
	 * Initialize the DMA/TCE management
	 */
	iommu_init_early_iSeries();

	/*
	 * Initialize the table which translate Linux physical addresses to
	 * AS/400 absolute addresses
	 */
	build_iSeries_Memory_Map();

	iSeries_get_cmdline();

	/* Save unparsed command line copy for /proc/cmdline */
	strlcpy(saved_command_line, cmd_line, COMMAND_LINE_SIZE);

	/* Parse early parameters, in particular mem=x */
	parse_early_param();

	if (memory_limit) {
		if (memory_limit < systemcfg->physicalMemorySize)
			systemcfg->physicalMemorySize = memory_limit;
		else {
			printk("Ignoring mem=%lu >= ram_top.\n", memory_limit);
			memory_limit = 0;
		}
	}

	/* Bolt kernel mappings for all of memory (or just a bit if we've got a limit) */
	iSeries_bolt_kernel(0, systemcfg->physicalMemorySize);

	lmb_init();
	lmb_add(0, systemcfg->physicalMemorySize);
	lmb_analyze();
	lmb_reserve(0, __pa(klimit));

	/* Initialize machine-dependency vectors */
#ifdef CONFIG_SMP
	smp_init_iSeries();
#endif
	if (itLpNaca.xPirEnvironMode == 0)
		piranha_simulator = 1;

	/* Associate Lp Event Queue 0 with processor 0 */
	HvCallEvent_setLpEventQueueInterruptProc(0, 0);

	mf_init();
	mf_initialized = 1;
	mb();

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
 */

static void __init build_iSeries_Memory_Map(void)
{
	u32 loadAreaFirstChunk, loadAreaLastChunk, loadAreaSize;
	u32 nextPhysChunk;
	u32 hptFirstChunk, hptLastChunk, hptSizeChunks, hptSizePages;
	u32 num_ptegs;
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
	hptSizeChunks = hptSizePages >> (MSCHUNKS_CHUNK_SHIFT - PAGE_SHIFT);
	hptLastChunk = hptFirstChunk + hptSizeChunks - 1;

	printk("HPT absolute addr = %016lx, size = %dK\n",
			chunk_to_addr(hptFirstChunk), hptSizeChunks * 256);

	/* Fill in the hashed page table hash mask */
	num_ptegs = hptSizePages *
		(PAGE_SIZE / (sizeof(hpte_t) * HPTES_PER_GROUP));
	htab_hash_mask = num_ptegs - 1;

	/*
	 * The actual hashed page table is in the hypervisor,
	 * we have no direct access
	 */
	htab_address = NULL;

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
	systemcfg->physicalMemorySize = chunk_to_addr(nextPhysChunk);
}

/*
 * Set up the variables that describe the cache line sizes
 * for this machine.
 */
static void __init setup_iSeries_cache_sizes(void)
{
	unsigned int i, n;
	unsigned int procIx = get_paca()->lppaca.dyn_hv_phys_proc_index;

	systemcfg->icache_size =
	ppc64_caches.isize = xIoHriProcessorVpd[procIx].xInstCacheSize * 1024;
	systemcfg->icache_line_size =
	ppc64_caches.iline_size =
		xIoHriProcessorVpd[procIx].xInstCacheOperandSize;
	systemcfg->dcache_size =
	ppc64_caches.dsize =
		xIoHriProcessorVpd[procIx].xDataL1CacheSizeKB * 1024;
	systemcfg->dcache_line_size =
	ppc64_caches.dline_size =
		xIoHriProcessorVpd[procIx].xDataCacheOperandSize;
	ppc64_caches.ilines_per_page = PAGE_SIZE / ppc64_caches.iline_size;
	ppc64_caches.dlines_per_page = PAGE_SIZE / ppc64_caches.dline_size;

	i = ppc64_caches.iline_size;
	n = 0;
	while ((i = (i / 2)))
		++n;
	ppc64_caches.log_iline_size = n;

	i = ppc64_caches.dline_size;
	n = 0;
	while ((i = (i / 2)))
		++n;
	ppc64_caches.log_dline_size = n;

	printk("D-cache line size = %d\n",
			(unsigned int)ppc64_caches.dline_size);
	printk("I-cache line size = %d\n",
			(unsigned int)ppc64_caches.iline_size);
}

/*
 * Create a pte. Used during initialization only.
 */
static void iSeries_make_pte(unsigned long va, unsigned long pa,
			     int mode)
{
	hpte_t local_hpte, rhpte;
	unsigned long hash, vpn;
	long slot;

	vpn = va >> PAGE_SHIFT;
	hash = hpt_hash(vpn, 0);

	local_hpte.r = pa | mode;
	local_hpte.v = ((va >> 23) << HPTE_V_AVPN_SHIFT)
		| HPTE_V_BOLTED | HPTE_V_VALID;

	slot = HvCallHpt_findValid(&rhpte, vpn);
	if (slot < 0) {
		/* Must find space in primary group */
		panic("hash_page: hpte already exists\n");
	}
	HvCallHpt_addValidate(slot, 0, &local_hpte);
}

/*
 * Bolt the kernel addr space into the HPT
 */
static void __init iSeries_bolt_kernel(unsigned long saddr, unsigned long eaddr)
{
	unsigned long pa;
	unsigned long mode_rw = _PAGE_ACCESSED | _PAGE_COHERENT | PP_RWXX;
	hpte_t hpte;

	for (pa = saddr; pa < eaddr ;pa += PAGE_SIZE) {
		unsigned long ea = (unsigned long)__va(pa);
		unsigned long vsid = get_kernel_vsid(ea);
		unsigned long va = (vsid << 28) | (pa & 0xfffffff);
		unsigned long vpn = va >> PAGE_SHIFT;
		unsigned long slot = HvCallHpt_findValid(&hpte, vpn);

		/* Make non-kernel text non-executable */
		if (!in_kernel_text(ea))
			mode_rw |= HW_NO_EXEC;

		if (hpte.v & HPTE_V_VALID) {
			/* HPTE exists, so just bolt it */
			HvCallHpt_setSwBits(slot, 0x10, 0);
			/* And make sure the pp bits are correct */
			HvCallHpt_setPp(slot, PP_RWXX);
		} else
			/* No HPTE exists, so create a new bolted one */
			iSeries_make_pte(va, phys_to_abs(pa), mode_rw);
	}
}

/*
 * Document me.
 */
static void __init iSeries_setup_arch(void)
{
	unsigned procIx = get_paca()->lppaca.dyn_hv_phys_proc_index;

	/* Add an eye catcher and the systemcfg layout version number */
	strcpy(systemcfg->eye_catcher, "SYSTEMCFG:PPC64");
	systemcfg->version.major = SYSTEMCFG_MAJOR;
	systemcfg->version.minor = SYSTEMCFG_MINOR;

	/* Setup the Lp Event Queue */
	setup_hvlpevent_queue();

	/* Compute processor frequency */
	procFreqHz = ((1UL << 34) * 1000000) /
			xIoHriProcessorVpd[procIx].xProcFreq;
	procFreqMhz = procFreqHz / 1000000;
	procFreqMhzHundreths = (procFreqHz / 10000) - (procFreqMhz * 100);
	ppc_proc_freq = procFreqHz;

	/* Compute time base frequency */
	tbFreqHz = ((1UL << 32) * 1000000) /
		xIoHriProcessorVpd[procIx].xTimeBaseFreq;
	tbFreqMhz = tbFreqHz / 1000000;
	tbFreqMhzHundreths = (tbFreqHz / 10000) - (tbFreqMhz * 100);
	ppc_tb_freq = tbFreqHz;

	printk("Max  logical processors = %d\n",
			itVpdAreas.xSlicMaxLogicalProcs);
	printk("Max physical processors = %d\n",
			itVpdAreas.xSlicMaxPhysicalProcs);
	printk("Processor frequency = %lu.%02lu\n", procFreqMhz,
			procFreqMhzHundreths);
	printk("Time base frequency = %lu.%02lu\n", tbFreqMhz,
			tbFreqMhzHundreths);
	systemcfg->processor = xIoHriProcessorVpd[procIx].xPVR;
	printk("Processor version = %x\n", systemcfg->processor);
}

static void iSeries_get_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "machine\t\t: 64-bit iSeries Logical Partition\n");
}

/*
 * Document me.
 * and Implement me.
 */
static int iSeries_get_irq(struct pt_regs *regs)
{
	/* -2 means ignore this interrupt */
	return -2;
}

/*
 * Document me.
 */
static void iSeries_restart(char *cmd)
{
	mf_reboot();
}

/*
 * Document me.
 */
static void iSeries_power_off(void)
{
	mf_power_off();
}

/*
 * Document me.
 */
static void iSeries_halt(void)
{
	mf_power_off();
}

/*
 * void __init iSeries_calibrate_decr()
 *
 * Description:
 *   This routine retrieves the internal processor frequency from the VPD,
 *   and sets up the kernel timer decrementer based on that value.
 *
 */
static void __init iSeries_calibrate_decr(void)
{
	unsigned long	cyclesPerUsec;
	struct div_result divres;

	/* Compute decrementer (and TB) frequency in cycles/sec */
	cyclesPerUsec = ppc_tb_freq / 1000000;

	/*
	 * Set the amount to refresh the decrementer by.  This
	 * is the number of decrementer ticks it takes for
	 * 1/HZ seconds.
	 */
	tb_ticks_per_jiffy = ppc_tb_freq / HZ;

#if 0
	/* TEST CODE FOR ADJTIME */
	tb_ticks_per_jiffy += tb_ticks_per_jiffy / 5000;
	/* END OF TEST CODE */
#endif

	/*
	 * tb_ticks_per_sec = freq; would give better accuracy
	 * but tb_ticks_per_sec = tb_ticks_per_jiffy*HZ; assures
	 * that jiffies (and xtime) will match the time returned
	 * by do_gettimeofday.
	 */
	tb_ticks_per_sec = tb_ticks_per_jiffy * HZ;
	tb_ticks_per_usec = cyclesPerUsec;
	tb_to_us = mulhwu_scale_factor(ppc_tb_freq, 1000000);
	div128_by_32(1024 * 1024, 0, tb_ticks_per_sec, &divres);
	tb_to_xs = divres.result_low;
	setup_default_decr();
}

static void __init iSeries_progress(char * st, unsigned short code)
{
	printk("Progress: [%04x] - %s\n", (unsigned)code, st);
	if (!piranha_simulator && mf_initialized) {
		if (code != 0xffff)
			mf_display_progress(code);
		else
			mf_clear_src();
	}
}

static void __init iSeries_fixup_klimit(void)
{
	/*
	 * Change klimit to take into account any ram disk
	 * that may be included
	 */
	if (naca.xRamDisk)
		klimit = KERNELBASE + (u64)naca.xRamDisk +
			(naca.xRamDiskSize * PAGE_SIZE);
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
	get_paca()->lppaca.int_dword.fields.decr_int = 1;
	process_iSeries_events();
}

static int iseries_shared_idle(void)
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

		schedule();
	}

	return 0;
}

static int iseries_dedicated_idle(void)
{
	long oldval;

	while (1) {
		oldval = test_and_clear_thread_flag(TIF_NEED_RESCHED);

		if (!oldval) {
			set_thread_flag(TIF_POLLING_NRFLAG);

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
			clear_thread_flag(TIF_POLLING_NRFLAG);
		} else {
			set_need_resched();
		}

		ppc64_runlatch_on();
		schedule();
	}

	return 0;
}

#ifndef CONFIG_PCI
void __init iSeries_init_IRQ(void) { }
#endif

void __init iSeries_early_setup(void)
{
	iSeries_fixup_klimit();

	ppc_md.setup_arch = iSeries_setup_arch;
	ppc_md.get_cpuinfo = iSeries_get_cpuinfo;
	ppc_md.init_IRQ = iSeries_init_IRQ;
	ppc_md.get_irq = iSeries_get_irq;
	ppc_md.init_early = iSeries_init_early,

	ppc_md.pcibios_fixup  = iSeries_pci_final_fixup;

	ppc_md.restart = iSeries_restart;
	ppc_md.power_off = iSeries_power_off;
	ppc_md.halt = iSeries_halt;

	ppc_md.get_boot_time = iSeries_get_boot_time;
	ppc_md.set_rtc_time = iSeries_set_rtc_time;
	ppc_md.get_rtc_time = iSeries_get_rtc_time;
	ppc_md.calibrate_decr = iSeries_calibrate_decr;
	ppc_md.progress = iSeries_progress;

	/* XXX Implement enable_pmcs for iSeries */

	if (get_paca()->lppaca.shared_proc) {
		ppc_md.idle_loop = iseries_shared_idle;
		printk(KERN_INFO "Using shared processor idle loop\n");
	} else {
		ppc_md.idle_loop = iseries_dedicated_idle;
		printk(KERN_INFO "Using dedicated idle loop\n");
	}
}


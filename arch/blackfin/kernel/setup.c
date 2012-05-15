/*
 * Copyright 2004-2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/delay.h>
#include <linux/console.h>
#include <linux/bootmem.h>
#include <linux/seq_file.h>
#include <linux/cpu.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/pfn.h>

#ifdef CONFIG_MTD_UCLINUX
#include <linux/mtd/map.h>
#include <linux/ext2_fs.h>
#include <linux/cramfs_fs.h>
#include <linux/romfs_fs.h>
#endif

#include <asm/cplb.h>
#include <asm/cacheflush.h>
#include <asm/blackfin.h>
#include <asm/cplbinit.h>
#include <asm/clocks.h>
#include <asm/div64.h>
#include <asm/cpu.h>
#include <asm/fixed_code.h>
#include <asm/early_printk.h>
#include <asm/irq_handler.h>
#include <asm/pda.h>

u16 _bfin_swrst;
EXPORT_SYMBOL(_bfin_swrst);

unsigned long memory_start, memory_end, physical_mem_end;
unsigned long _rambase, _ramstart, _ramend;
unsigned long reserved_mem_dcache_on;
unsigned long reserved_mem_icache_on;
EXPORT_SYMBOL(memory_start);
EXPORT_SYMBOL(memory_end);
EXPORT_SYMBOL(physical_mem_end);
EXPORT_SYMBOL(_ramend);
EXPORT_SYMBOL(reserved_mem_dcache_on);

#ifdef CONFIG_MTD_UCLINUX
extern struct map_info uclinux_ram_map;
unsigned long memory_mtd_end, memory_mtd_start, mtd_size;
unsigned long _ebss;
EXPORT_SYMBOL(memory_mtd_end);
EXPORT_SYMBOL(memory_mtd_start);
EXPORT_SYMBOL(mtd_size);
#endif

char __initdata command_line[COMMAND_LINE_SIZE];
struct blackfin_initial_pda __initdata initial_pda;

/* boot memmap, for parsing "memmap=" */
#define BFIN_MEMMAP_MAX		128 /* number of entries in bfin_memmap */
#define BFIN_MEMMAP_RAM		1
#define BFIN_MEMMAP_RESERVED	2
static struct bfin_memmap {
	int nr_map;
	struct bfin_memmap_entry {
		unsigned long long addr; /* start of memory segment */
		unsigned long long size;
		unsigned long type;
	} map[BFIN_MEMMAP_MAX];
} bfin_memmap __initdata;

/* for memmap sanitization */
struct change_member {
	struct bfin_memmap_entry *pentry; /* pointer to original entry */
	unsigned long long addr; /* address for this change point */
};
static struct change_member change_point_list[2*BFIN_MEMMAP_MAX] __initdata;
static struct change_member *change_point[2*BFIN_MEMMAP_MAX] __initdata;
static struct bfin_memmap_entry *overlap_list[BFIN_MEMMAP_MAX] __initdata;
static struct bfin_memmap_entry new_map[BFIN_MEMMAP_MAX] __initdata;

DEFINE_PER_CPU(struct blackfin_cpudata, cpu_data);

static int early_init_clkin_hz(char *buf);

#if defined(CONFIG_BFIN_DCACHE) || defined(CONFIG_BFIN_ICACHE)
void __init generate_cplb_tables(void)
{
	unsigned int cpu;

	generate_cplb_tables_all();
	/* Generate per-CPU I&D CPLB tables */
	for (cpu = 0; cpu < num_possible_cpus(); ++cpu)
		generate_cplb_tables_cpu(cpu);
}
#endif

void __cpuinit bfin_setup_caches(unsigned int cpu)
{
#ifdef CONFIG_BFIN_ICACHE
	bfin_icache_init(icplb_tbl[cpu]);
#endif

#ifdef CONFIG_BFIN_DCACHE
	bfin_dcache_init(dcplb_tbl[cpu]);
#endif

	bfin_setup_cpudata(cpu);

	/*
	 * In cache coherence emulation mode, we need to have the
	 * D-cache enabled before running any atomic operation which
	 * might involve cache invalidation (i.e. spinlock, rwlock).
	 * So printk's are deferred until then.
	 */
#ifdef CONFIG_BFIN_ICACHE
	printk(KERN_INFO "Instruction Cache Enabled for CPU%u\n", cpu);
	printk(KERN_INFO "  External memory:"
# ifdef CONFIG_BFIN_EXTMEM_ICACHEABLE
	       " cacheable"
# else
	       " uncacheable"
# endif
	       " in instruction cache\n");
	if (L2_LENGTH)
		printk(KERN_INFO "  L2 SRAM        :"
# ifdef CONFIG_BFIN_L2_ICACHEABLE
		       " cacheable"
# else
		       " uncacheable"
# endif
		       " in instruction cache\n");

#else
	printk(KERN_INFO "Instruction Cache Disabled for CPU%u\n", cpu);
#endif

#ifdef CONFIG_BFIN_DCACHE
	printk(KERN_INFO "Data Cache Enabled for CPU%u\n", cpu);
	printk(KERN_INFO "  External memory:"
# if defined CONFIG_BFIN_EXTMEM_WRITEBACK
	       " cacheable (write-back)"
# elif defined CONFIG_BFIN_EXTMEM_WRITETHROUGH
	       " cacheable (write-through)"
# else
	       " uncacheable"
# endif
	       " in data cache\n");
	if (L2_LENGTH)
		printk(KERN_INFO "  L2 SRAM        :"
# if defined CONFIG_BFIN_L2_WRITEBACK
		       " cacheable (write-back)"
# elif defined CONFIG_BFIN_L2_WRITETHROUGH
		       " cacheable (write-through)"
# else
		       " uncacheable"
# endif
		       " in data cache\n");
#else
	printk(KERN_INFO "Data Cache Disabled for CPU%u\n", cpu);
#endif
}

void __cpuinit bfin_setup_cpudata(unsigned int cpu)
{
	struct blackfin_cpudata *cpudata = &per_cpu(cpu_data, cpu);

	cpudata->imemctl = bfin_read_IMEM_CONTROL();
	cpudata->dmemctl = bfin_read_DMEM_CONTROL();
}

void __init bfin_cache_init(void)
{
#if defined(CONFIG_BFIN_DCACHE) || defined(CONFIG_BFIN_ICACHE)
	generate_cplb_tables();
#endif
	bfin_setup_caches(0);
}

void __init bfin_relocate_l1_mem(void)
{
	unsigned long text_l1_len = (unsigned long)_text_l1_len;
	unsigned long data_l1_len = (unsigned long)_data_l1_len;
	unsigned long data_b_l1_len = (unsigned long)_data_b_l1_len;
	unsigned long l2_len = (unsigned long)_l2_len;

	early_shadow_stamp();

	/*
	 * due to the ALIGN(4) in the arch/blackfin/kernel/vmlinux.lds.S
	 * we know that everything about l1 text/data is nice and aligned,
	 * so copy by 4 byte chunks, and don't worry about overlapping
	 * src/dest.
	 *
	 * We can't use the dma_memcpy functions, since they can call
	 * scheduler functions which might be in L1 :( and core writes
	 * into L1 instruction cause bad access errors, so we are stuck,
	 * we are required to use DMA, but can't use the common dma
	 * functions. We can't use memcpy either - since that might be
	 * going to be in the relocated L1
	 */

	blackfin_dma_early_init();

	/* if necessary, copy L1 text to L1 instruction SRAM */
	if (L1_CODE_LENGTH && text_l1_len)
		early_dma_memcpy(_stext_l1, _text_l1_lma, text_l1_len);

	/* if necessary, copy L1 data to L1 data bank A SRAM */
	if (L1_DATA_A_LENGTH && data_l1_len)
		early_dma_memcpy(_sdata_l1, _data_l1_lma, data_l1_len);

	/* if necessary, copy L1 data B to L1 data bank B SRAM */
	if (L1_DATA_B_LENGTH && data_b_l1_len)
		early_dma_memcpy(_sdata_b_l1, _data_b_l1_lma, data_b_l1_len);

	early_dma_memcpy_done();

#if defined(CONFIG_SMP) && defined(CONFIG_ICACHE_FLUSH_L1)
	blackfin_iflush_l1_entry[0] = (unsigned long)blackfin_icache_flush_range_l1;
#endif

	/* if necessary, copy L2 text/data to L2 SRAM */
	if (L2_LENGTH && l2_len)
		memcpy(_stext_l2, _l2_lma, l2_len);
}

#ifdef CONFIG_SMP
void __init bfin_relocate_coreb_l1_mem(void)
{
	unsigned long text_l1_len = (unsigned long)_text_l1_len;
	unsigned long data_l1_len = (unsigned long)_data_l1_len;
	unsigned long data_b_l1_len = (unsigned long)_data_b_l1_len;

	blackfin_dma_early_init();

	/* if necessary, copy L1 text to L1 instruction SRAM */
	if (L1_CODE_LENGTH && text_l1_len)
		early_dma_memcpy((void *)COREB_L1_CODE_START, _text_l1_lma,
				text_l1_len);

	/* if necessary, copy L1 data to L1 data bank A SRAM */
	if (L1_DATA_A_LENGTH && data_l1_len)
		early_dma_memcpy((void *)COREB_L1_DATA_A_START, _data_l1_lma,
				data_l1_len);

	/* if necessary, copy L1 data B to L1 data bank B SRAM */
	if (L1_DATA_B_LENGTH && data_b_l1_len)
		early_dma_memcpy((void *)COREB_L1_DATA_B_START, _data_b_l1_lma,
				data_b_l1_len);

	early_dma_memcpy_done();

#ifdef CONFIG_ICACHE_FLUSH_L1
	blackfin_iflush_l1_entry[1] = (unsigned long)blackfin_icache_flush_range_l1 -
			(unsigned long)_stext_l1 + COREB_L1_CODE_START;
#endif
}
#endif

#ifdef CONFIG_ROMKERNEL
void __init bfin_relocate_xip_data(void)
{
	early_shadow_stamp();

	memcpy(_sdata, _data_lma, (unsigned long)_data_len - THREAD_SIZE + sizeof(struct thread_info));
	memcpy(_sinitdata, _init_data_lma, (unsigned long)_init_data_len);
}
#endif

/* add_memory_region to memmap */
static void __init add_memory_region(unsigned long long start,
			      unsigned long long size, int type)
{
	int i;

	i = bfin_memmap.nr_map;

	if (i == BFIN_MEMMAP_MAX) {
		printk(KERN_ERR "Ooops! Too many entries in the memory map!\n");
		return;
	}

	bfin_memmap.map[i].addr = start;
	bfin_memmap.map[i].size = size;
	bfin_memmap.map[i].type = type;
	bfin_memmap.nr_map++;
}

/*
 * Sanitize the boot memmap, removing overlaps.
 */
static int __init sanitize_memmap(struct bfin_memmap_entry *map, int *pnr_map)
{
	struct change_member *change_tmp;
	unsigned long current_type, last_type;
	unsigned long long last_addr;
	int chgidx, still_changing;
	int overlap_entries;
	int new_entry;
	int old_nr, new_nr, chg_nr;
	int i;

	/*
		Visually we're performing the following (1,2,3,4 = memory types)

		Sample memory map (w/overlaps):
		   ____22__________________
		   ______________________4_
		   ____1111________________
		   _44_____________________
		   11111111________________
		   ____________________33__
		   ___________44___________
		   __________33333_________
		   ______________22________
		   ___________________2222_
		   _________111111111______
		   _____________________11_
		   _________________4______

		Sanitized equivalent (no overlap):
		   1_______________________
		   _44_____________________
		   ___1____________________
		   ____22__________________
		   ______11________________
		   _________1______________
		   __________3_____________
		   ___________44___________
		   _____________33_________
		   _______________2________
		   ________________1_______
		   _________________4______
		   ___________________2____
		   ____________________33__
		   ______________________4_
	*/
	/* if there's only one memory region, don't bother */
	if (*pnr_map < 2)
		return -1;

	old_nr = *pnr_map;

	/* bail out if we find any unreasonable addresses in memmap */
	for (i = 0; i < old_nr; i++)
		if (map[i].addr + map[i].size < map[i].addr)
			return -1;

	/* create pointers for initial change-point information (for sorting) */
	for (i = 0; i < 2*old_nr; i++)
		change_point[i] = &change_point_list[i];

	/* record all known change-points (starting and ending addresses),
	   omitting those that are for empty memory regions */
	chgidx = 0;
	for (i = 0; i < old_nr; i++) {
		if (map[i].size != 0) {
			change_point[chgidx]->addr = map[i].addr;
			change_point[chgidx++]->pentry = &map[i];
			change_point[chgidx]->addr = map[i].addr + map[i].size;
			change_point[chgidx++]->pentry = &map[i];
		}
	}
	chg_nr = chgidx;	/* true number of change-points */

	/* sort change-point list by memory addresses (low -> high) */
	still_changing = 1;
	while (still_changing) {
		still_changing = 0;
		for (i = 1; i < chg_nr; i++) {
			/* if <current_addr> > <last_addr>, swap */
			/* or, if current=<start_addr> & last=<end_addr>, swap */
			if ((change_point[i]->addr < change_point[i-1]->addr) ||
				((change_point[i]->addr == change_point[i-1]->addr) &&
				 (change_point[i]->addr == change_point[i]->pentry->addr) &&
				 (change_point[i-1]->addr != change_point[i-1]->pentry->addr))
			   ) {
				change_tmp = change_point[i];
				change_point[i] = change_point[i-1];
				change_point[i-1] = change_tmp;
				still_changing = 1;
			}
		}
	}

	/* create a new memmap, removing overlaps */
	overlap_entries = 0;	/* number of entries in the overlap table */
	new_entry = 0;		/* index for creating new memmap entries */
	last_type = 0;		/* start with undefined memory type */
	last_addr = 0;		/* start with 0 as last starting address */
	/* loop through change-points, determining affect on the new memmap */
	for (chgidx = 0; chgidx < chg_nr; chgidx++) {
		/* keep track of all overlapping memmap entries */
		if (change_point[chgidx]->addr == change_point[chgidx]->pentry->addr) {
			/* add map entry to overlap list (> 1 entry implies an overlap) */
			overlap_list[overlap_entries++] = change_point[chgidx]->pentry;
		} else {
			/* remove entry from list (order independent, so swap with last) */
			for (i = 0; i < overlap_entries; i++) {
				if (overlap_list[i] == change_point[chgidx]->pentry)
					overlap_list[i] = overlap_list[overlap_entries-1];
			}
			overlap_entries--;
		}
		/* if there are overlapping entries, decide which "type" to use */
		/* (larger value takes precedence -- 1=usable, 2,3,4,4+=unusable) */
		current_type = 0;
		for (i = 0; i < overlap_entries; i++)
			if (overlap_list[i]->type > current_type)
				current_type = overlap_list[i]->type;
		/* continue building up new memmap based on this information */
		if (current_type != last_type) {
			if (last_type != 0) {
				new_map[new_entry].size =
					change_point[chgidx]->addr - last_addr;
				/* move forward only if the new size was non-zero */
				if (new_map[new_entry].size != 0)
					if (++new_entry >= BFIN_MEMMAP_MAX)
						break;	/* no more space left for new entries */
			}
			if (current_type != 0) {
				new_map[new_entry].addr = change_point[chgidx]->addr;
				new_map[new_entry].type = current_type;
				last_addr = change_point[chgidx]->addr;
			}
			last_type = current_type;
		}
	}
	new_nr = new_entry;	/* retain count for new entries */

	/* copy new mapping into original location */
	memcpy(map, new_map, new_nr*sizeof(struct bfin_memmap_entry));
	*pnr_map = new_nr;

	return 0;
}

static void __init print_memory_map(char *who)
{
	int i;

	for (i = 0; i < bfin_memmap.nr_map; i++) {
		printk(KERN_DEBUG " %s: %016Lx - %016Lx ", who,
			bfin_memmap.map[i].addr,
			bfin_memmap.map[i].addr + bfin_memmap.map[i].size);
		switch (bfin_memmap.map[i].type) {
		case BFIN_MEMMAP_RAM:
			printk(KERN_CONT "(usable)\n");
			break;
		case BFIN_MEMMAP_RESERVED:
			printk(KERN_CONT "(reserved)\n");
			break;
		default:
			printk(KERN_CONT "type %lu\n", bfin_memmap.map[i].type);
			break;
		}
	}
}

static __init int parse_memmap(char *arg)
{
	unsigned long long start_at, mem_size;

	if (!arg)
		return -EINVAL;

	mem_size = memparse(arg, &arg);
	if (*arg == '@') {
		start_at = memparse(arg+1, &arg);
		add_memory_region(start_at, mem_size, BFIN_MEMMAP_RAM);
	} else if (*arg == '$') {
		start_at = memparse(arg+1, &arg);
		add_memory_region(start_at, mem_size, BFIN_MEMMAP_RESERVED);
	}

	return 0;
}

/*
 * Initial parsing of the command line.  Currently, we support:
 *  - Controlling the linux memory size: mem=xxx[KMG]
 *  - Controlling the physical memory size: max_mem=xxx[KMG][$][#]
 *       $ -> reserved memory is dcacheable
 *       # -> reserved memory is icacheable
 *  - "memmap=XXX[KkmM][@][$]XXX[KkmM]" defines a memory region
 *       @ from <start> to <start>+<mem>, type RAM
 *       $ from <start> to <start>+<mem>, type RESERVED
 */
static __init void parse_cmdline_early(char *cmdline_p)
{
	char c = ' ', *to = cmdline_p;
	unsigned int memsize;
	for (;;) {
		if (c == ' ') {
			if (!memcmp(to, "mem=", 4)) {
				to += 4;
				memsize = memparse(to, &to);
				if (memsize)
					_ramend = memsize;

			} else if (!memcmp(to, "max_mem=", 8)) {
				to += 8;
				memsize = memparse(to, &to);
				if (memsize) {
					physical_mem_end = memsize;
					if (*to != ' ') {
						if (*to == '$'
						    || *(to + 1) == '$')
							reserved_mem_dcache_on = 1;
						if (*to == '#'
						    || *(to + 1) == '#')
							reserved_mem_icache_on = 1;
					}
				}
			} else if (!memcmp(to, "clkin_hz=", 9)) {
				to += 9;
				early_init_clkin_hz(to);
#ifdef CONFIG_EARLY_PRINTK
			} else if (!memcmp(to, "earlyprintk=", 12)) {
				to += 12;
				setup_early_printk(to);
#endif
			} else if (!memcmp(to, "memmap=", 7)) {
				to += 7;
				parse_memmap(to);
			}
		}
		c = *(to++);
		if (!c)
			break;
	}
}

/*
 * Setup memory defaults from user config.
 * The physical memory layout looks like:
 *
 *  [_rambase, _ramstart]:		kernel image
 *  [memory_start, memory_end]:		dynamic memory managed by kernel
 *  [memory_end, _ramend]:		reserved memory
 *  	[memory_mtd_start(memory_end),
 *  		memory_mtd_start + mtd_size]:	rootfs (if any)
 *	[_ramend - DMA_UNCACHED_REGION,
 *		_ramend]:			uncached DMA region
 *  [_ramend, physical_mem_end]:	memory not managed by kernel
 */
static __init void memory_setup(void)
{
#ifdef CONFIG_MTD_UCLINUX
	unsigned long mtd_phys = 0;
#endif
	unsigned long max_mem;

	_rambase = CONFIG_BOOT_LOAD;
	_ramstart = (unsigned long)_end;

	if (DMA_UNCACHED_REGION > (_ramend - _ramstart)) {
		console_init();
		panic("DMA region exceeds memory limit: %lu.",
			_ramend - _ramstart);
	}
	max_mem = memory_end = _ramend - DMA_UNCACHED_REGION;

#if (defined(CONFIG_BFIN_EXTMEM_ICACHEABLE) && ANOMALY_05000263)
	/* Due to a Hardware Anomaly we need to limit the size of usable
	 * instruction memory to max 60MB, 56 if HUNT_FOR_ZERO is on
	 * 05000263 - Hardware loop corrupted when taking an ICPLB exception
	 */
# if (defined(CONFIG_DEBUG_HUNT_FOR_ZERO))
	if (max_mem >= 56 * 1024 * 1024)
		max_mem = 56 * 1024 * 1024;
# else
	if (max_mem >= 60 * 1024 * 1024)
		max_mem = 60 * 1024 * 1024;
# endif				/* CONFIG_DEBUG_HUNT_FOR_ZERO */
#endif				/* ANOMALY_05000263 */


#ifdef CONFIG_MPU
	/* Round up to multiple of 4MB */
	memory_start = (_ramstart + 0x3fffff) & ~0x3fffff;
#else
	memory_start = PAGE_ALIGN(_ramstart);
#endif

#if defined(CONFIG_MTD_UCLINUX)
	/* generic memory mapped MTD driver */
	memory_mtd_end = memory_end;

	mtd_phys = _ramstart;
	mtd_size = PAGE_ALIGN(*((unsigned long *)(mtd_phys + 8)));

# if defined(CONFIG_EXT2_FS) || defined(CONFIG_EXT3_FS)
	if (*((unsigned short *)(mtd_phys + 0x438)) == EXT2_SUPER_MAGIC)
		mtd_size =
		    PAGE_ALIGN(*((unsigned long *)(mtd_phys + 0x404)) << 10);
# endif

# if defined(CONFIG_CRAMFS)
	if (*((unsigned long *)(mtd_phys)) == CRAMFS_MAGIC)
		mtd_size = PAGE_ALIGN(*((unsigned long *)(mtd_phys + 0x4)));
# endif

# if defined(CONFIG_ROMFS_FS)
	if (((unsigned long *)mtd_phys)[0] == ROMSB_WORD0
	    && ((unsigned long *)mtd_phys)[1] == ROMSB_WORD1) {
		mtd_size =
		    PAGE_ALIGN(be32_to_cpu(((unsigned long *)mtd_phys)[2]));

		/* ROM_FS is XIP, so if we found it, we need to limit memory */
		if (memory_end > max_mem) {
			pr_info("Limiting kernel memory to %liMB due to anomaly 05000263\n",
				(max_mem - CONFIG_PHY_RAM_BASE_ADDRESS) >> 20);
			memory_end = max_mem;
		}
	}
# endif				/* CONFIG_ROMFS_FS */

	/* Since the default MTD_UCLINUX has no magic number, we just blindly
	 * read 8 past the end of the kernel's image, and look at it.
	 * When no image is attached, mtd_size is set to a random number
	 * Do some basic sanity checks before operating on things
	 */
	if (mtd_size == 0 || memory_end <= mtd_size) {
		pr_emerg("Could not find valid ram mtd attached.\n");
	} else {
		memory_end -= mtd_size;

		/* Relocate MTD image to the top of memory after the uncached memory area */
		uclinux_ram_map.phys = memory_mtd_start = memory_end;
		uclinux_ram_map.size = mtd_size;
		pr_info("Found mtd parition at 0x%p, (len=0x%lx), moving to 0x%p\n",
			_end, mtd_size, (void *)memory_mtd_start);
		dma_memcpy((void *)uclinux_ram_map.phys, _end, uclinux_ram_map.size);
	}
#endif				/* CONFIG_MTD_UCLINUX */

	/* We need lo limit memory, since everything could have a text section
	 * of userspace in it, and expose anomaly 05000263. If the anomaly
	 * doesn't exist, or we don't need to - then dont.
	 */
	if (memory_end > max_mem) {
		pr_info("Limiting kernel memory to %liMB due to anomaly 05000263\n",
				(max_mem - CONFIG_PHY_RAM_BASE_ADDRESS) >> 20);
		memory_end = max_mem;
	}

#ifdef CONFIG_MPU
#if defined(CONFIG_ROMFS_ON_MTD) && defined(CONFIG_MTD_ROM)
	page_mask_nelts = (((_ramend + ASYNC_BANK3_BASE + ASYNC_BANK3_SIZE -
					ASYNC_BANK0_BASE) >> PAGE_SHIFT) + 31) / 32;
#else
	page_mask_nelts = ((_ramend >> PAGE_SHIFT) + 31) / 32;
#endif
	page_mask_order = get_order(3 * page_mask_nelts * sizeof(long));
#endif

	init_mm.start_code = (unsigned long)_stext;
	init_mm.end_code = (unsigned long)_etext;
	init_mm.end_data = (unsigned long)_edata;
	init_mm.brk = (unsigned long)0;

	printk(KERN_INFO "Board Memory: %ldMB\n", (physical_mem_end - CONFIG_PHY_RAM_BASE_ADDRESS) >> 20);
	printk(KERN_INFO "Kernel Managed Memory: %ldMB\n", (_ramend - CONFIG_PHY_RAM_BASE_ADDRESS) >> 20);

	printk(KERN_INFO "Memory map:\n"
	       "  fixedcode = 0x%p-0x%p\n"
	       "  text      = 0x%p-0x%p\n"
	       "  rodata    = 0x%p-0x%p\n"
	       "  bss       = 0x%p-0x%p\n"
	       "  data      = 0x%p-0x%p\n"
	       "    stack   = 0x%p-0x%p\n"
	       "  init      = 0x%p-0x%p\n"
	       "  available = 0x%p-0x%p\n"
#ifdef CONFIG_MTD_UCLINUX
	       "  rootfs    = 0x%p-0x%p\n"
#endif
#if DMA_UNCACHED_REGION > 0
	       "  DMA Zone  = 0x%p-0x%p\n"
#endif
		, (void *)FIXED_CODE_START, (void *)FIXED_CODE_END,
		_stext, _etext,
		__start_rodata, __end_rodata,
		__bss_start, __bss_stop,
		_sdata, _edata,
		(void *)&init_thread_union,
		(void *)((int)(&init_thread_union) + THREAD_SIZE),
		__init_begin, __init_end,
		(void *)_ramstart, (void *)memory_end
#ifdef CONFIG_MTD_UCLINUX
		, (void *)memory_mtd_start, (void *)(memory_mtd_start + mtd_size)
#endif
#if DMA_UNCACHED_REGION > 0
		, (void *)(_ramend - DMA_UNCACHED_REGION), (void *)(_ramend)
#endif
		);
}

/*
 * Find the lowest, highest page frame number we have available
 */
void __init find_min_max_pfn(void)
{
	int i;

	max_pfn = 0;
	min_low_pfn = PFN_DOWN(memory_end);

	for (i = 0; i < bfin_memmap.nr_map; i++) {
		unsigned long start, end;
		/* RAM? */
		if (bfin_memmap.map[i].type != BFIN_MEMMAP_RAM)
			continue;
		start = PFN_UP(bfin_memmap.map[i].addr);
		end = PFN_DOWN(bfin_memmap.map[i].addr +
				bfin_memmap.map[i].size);
		if (start >= end)
			continue;
		if (end > max_pfn)
			max_pfn = end;
		if (start < min_low_pfn)
			min_low_pfn = start;
	}
}

static __init void setup_bootmem_allocator(void)
{
	int bootmap_size;
	int i;
	unsigned long start_pfn, end_pfn;
	unsigned long curr_pfn, last_pfn, size;

	/* mark memory between memory_start and memory_end usable */
	add_memory_region(memory_start,
		memory_end - memory_start, BFIN_MEMMAP_RAM);
	/* sanity check for overlap */
	sanitize_memmap(bfin_memmap.map, &bfin_memmap.nr_map);
	print_memory_map("boot memmap");

	/* initialize globals in linux/bootmem.h */
	find_min_max_pfn();
	/* pfn of the last usable page frame */
	if (max_pfn > memory_end >> PAGE_SHIFT)
		max_pfn = memory_end >> PAGE_SHIFT;
	/* pfn of last page frame directly mapped by kernel */
	max_low_pfn = max_pfn;
	/* pfn of the first usable page frame after kernel image*/
	if (min_low_pfn < memory_start >> PAGE_SHIFT)
		min_low_pfn = memory_start >> PAGE_SHIFT;
	start_pfn = CONFIG_PHY_RAM_BASE_ADDRESS >> PAGE_SHIFT;
	end_pfn = memory_end >> PAGE_SHIFT;

	/*
	 * give all the memory to the bootmap allocator, tell it to put the
	 * boot mem_map at the start of memory.
	 */
	bootmap_size = init_bootmem_node(NODE_DATA(0),
			memory_start >> PAGE_SHIFT,	/* map goes here */
			start_pfn, end_pfn);

	/* register the memmap regions with the bootmem allocator */
	for (i = 0; i < bfin_memmap.nr_map; i++) {
		/*
		 * Reserve usable memory
		 */
		if (bfin_memmap.map[i].type != BFIN_MEMMAP_RAM)
			continue;
		/*
		 * We are rounding up the start address of usable memory:
		 */
		curr_pfn = PFN_UP(bfin_memmap.map[i].addr);
		if (curr_pfn >= end_pfn)
			continue;
		/*
		 * ... and at the end of the usable range downwards:
		 */
		last_pfn = PFN_DOWN(bfin_memmap.map[i].addr +
					 bfin_memmap.map[i].size);

		if (last_pfn > end_pfn)
			last_pfn = end_pfn;

		/*
		 * .. finally, did all the rounding and playing
		 * around just make the area go away?
		 */
		if (last_pfn <= curr_pfn)
			continue;

		size = last_pfn - curr_pfn;
		free_bootmem(PFN_PHYS(curr_pfn), PFN_PHYS(size));
	}

	/* reserve memory before memory_start, including bootmap */
	reserve_bootmem(CONFIG_PHY_RAM_BASE_ADDRESS,
		memory_start + bootmap_size + PAGE_SIZE - 1 - CONFIG_PHY_RAM_BASE_ADDRESS,
		BOOTMEM_DEFAULT);
}

#define EBSZ_TO_MEG(ebsz) \
({ \
	int meg = 0; \
	switch (ebsz & 0xf) { \
		case 0x1: meg =  16; break; \
		case 0x3: meg =  32; break; \
		case 0x5: meg =  64; break; \
		case 0x7: meg = 128; break; \
		case 0x9: meg = 256; break; \
		case 0xb: meg = 512; break; \
	} \
	meg; \
})
static inline int __init get_mem_size(void)
{
#if defined(EBIU_SDBCTL)
# if defined(BF561_FAMILY)
	int ret = 0;
	u32 sdbctl = bfin_read_EBIU_SDBCTL();
	ret += EBSZ_TO_MEG(sdbctl >>  0);
	ret += EBSZ_TO_MEG(sdbctl >>  8);
	ret += EBSZ_TO_MEG(sdbctl >> 16);
	ret += EBSZ_TO_MEG(sdbctl >> 24);
	return ret;
# else
	return EBSZ_TO_MEG(bfin_read_EBIU_SDBCTL());
# endif
#elif defined(EBIU_DDRCTL1)
	u32 ddrctl = bfin_read_EBIU_DDRCTL1();
	int ret = 0;
	switch (ddrctl & 0xc0000) {
	case DEVSZ_64:
		ret = 64 / 8;
		break;
	case DEVSZ_128:
		ret = 128 / 8;
		break;
	case DEVSZ_256:
		ret = 256 / 8;
		break;
	case DEVSZ_512:
		ret = 512 / 8;
		break;
	}
	switch (ddrctl & 0x30000) {
	case DEVWD_4:
		ret *= 2;
	case DEVWD_8:
		ret *= 2;
	case DEVWD_16:
		break;
	}
	if ((ddrctl & 0xc000) == 0x4000)
		ret *= 2;
	return ret;
#elif defined(CONFIG_BF60x)
	u32 ddrctl = bfin_read_DMC0_CFG();
	int ret;
	switch (ddrctl & 0xf00) {
	case DEVSZ_64:
		ret = 64 / 8;
		break;
	case DEVSZ_128:
		ret = 128 / 8;
		break;
	case DEVSZ_256:
		ret = 256 / 8;
		break;
	case DEVSZ_512:
		ret = 512 / 8;
		break;
	case DEVSZ_1G:
		ret = 1024 / 8;
		break;
	case DEVSZ_2G:
		ret = 2048 / 8;
		break;
	}
	return ret;
#endif
	BUG();
}

__attribute__((weak))
void __init native_machine_early_platform_add_devices(void)
{
}

#ifdef CONFIG_BF60x
static inline u_long bfin_get_clk(char *name)
{
	struct clk *clk;
	u_long clk_rate;

	clk = clk_get(NULL, name);
	if (IS_ERR(clk))
		return 0;

	clk_rate = clk_get_rate(clk);
	clk_put(clk);
	return clk_rate;
}
#endif

void __init setup_arch(char **cmdline_p)
{
	u32 mmr;
	unsigned long sclk, cclk;

	native_machine_early_platform_add_devices();

	enable_shadow_console();

	/* Check to make sure we are running on the right processor */
	mmr =  bfin_cpuid();
	if (unlikely(CPUID != bfin_cpuid()))
		printk(KERN_ERR "ERROR: Not running on ADSP-%s: unknown CPUID 0x%04x Rev 0.%d\n",
			CPU, bfin_cpuid(), bfin_revid());

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

#if defined(CONFIG_CMDLINE_BOOL)
	strncpy(&command_line[0], CONFIG_CMDLINE, sizeof(command_line));
	command_line[sizeof(command_line) - 1] = 0;
#endif

	/* Keep a copy of command line */
	*cmdline_p = &command_line[0];
	memcpy(boot_command_line, command_line, COMMAND_LINE_SIZE);
	boot_command_line[COMMAND_LINE_SIZE - 1] = '\0';

	memset(&bfin_memmap, 0, sizeof(bfin_memmap));

#ifdef CONFIG_BF60x
	/* Should init clock device before parse command early */
	clk_init();
#endif
	/* If the user does not specify things on the command line, use
	 * what the bootloader set things up as
	 */
	physical_mem_end = 0;
	parse_cmdline_early(&command_line[0]);

	if (_ramend == 0)
		_ramend = get_mem_size() * 1024 * 1024;

	if (physical_mem_end == 0)
		physical_mem_end = _ramend;

	memory_setup();

#ifndef CONFIG_BF60x
	/* Initialize Async memory banks */
	bfin_write_EBIU_AMBCTL0(AMBCTL0VAL);
	bfin_write_EBIU_AMBCTL1(AMBCTL1VAL);
	bfin_write_EBIU_AMGCTL(AMGCTLVAL);
#ifdef CONFIG_EBIU_MBSCTLVAL
	bfin_write_EBIU_MBSCTL(CONFIG_EBIU_MBSCTLVAL);
	bfin_write_EBIU_MODE(CONFIG_EBIU_MODEVAL);
	bfin_write_EBIU_FCTL(CONFIG_EBIU_FCTLVAL);
#endif
#endif
#ifdef CONFIG_BFIN_HYSTERESIS_CONTROL
	bfin_write_PORTF_HYSTERESIS(HYST_PORTF_0_15);
	bfin_write_PORTG_HYSTERESIS(HYST_PORTG_0_15);
	bfin_write_PORTH_HYSTERESIS(HYST_PORTH_0_15);
	bfin_write_MISCPORT_HYSTERESIS((bfin_read_MISCPORT_HYSTERESIS() &
					~HYST_NONEGPIO_MASK) | HYST_NONEGPIO);
#endif

	cclk = get_cclk();
	sclk = get_sclk();

	if ((ANOMALY_05000273 || ANOMALY_05000274) && (cclk >> 1) < sclk)
		panic("ANOMALY 05000273 or 05000274: CCLK must be >= 2*SCLK");

#ifdef BF561_FAMILY
	if (ANOMALY_05000266) {
		bfin_read_IMDMA_D0_IRQ_STATUS();
		bfin_read_IMDMA_D1_IRQ_STATUS();
	}
#endif

	mmr = bfin_read_TBUFCTL();
	printk(KERN_INFO "Hardware Trace %s and %sabled\n",
		(mmr & 0x1) ? "active" : "off",
		(mmr & 0x2) ? "en" : "dis");
#ifndef CONFIG_BF60x
	mmr = bfin_read_SYSCR();
	printk(KERN_INFO "Boot Mode: %i\n", mmr & 0xF);

	/* Newer parts mirror SWRST bits in SYSCR */
#if defined(CONFIG_BF53x) || defined(CONFIG_BF561) || \
    defined(CONFIG_BF538) || defined(CONFIG_BF539)
	_bfin_swrst = bfin_read_SWRST();
#else
	/* Clear boot mode field */
	_bfin_swrst = mmr & ~0xf;
#endif

#ifdef CONFIG_DEBUG_DOUBLEFAULT_PRINT
	bfin_write_SWRST(_bfin_swrst & ~DOUBLE_FAULT);
#endif
#ifdef CONFIG_DEBUG_DOUBLEFAULT_RESET
	bfin_write_SWRST(_bfin_swrst | DOUBLE_FAULT);
#endif

#ifdef CONFIG_SMP
	if (_bfin_swrst & SWRST_DBL_FAULT_A) {
#else
	if (_bfin_swrst & RESET_DOUBLE) {
#endif
		printk(KERN_EMERG "Recovering from DOUBLE FAULT event\n");
#ifdef CONFIG_DEBUG_DOUBLEFAULT
		/* We assume the crashing kernel, and the current symbol table match */
		printk(KERN_EMERG " While handling exception (EXCAUSE = %#x) at %pF\n",
			initial_pda.seqstat_doublefault & SEQSTAT_EXCAUSE,
			initial_pda.retx_doublefault);
		printk(KERN_NOTICE "   DCPLB_FAULT_ADDR: %pF\n",
			initial_pda.dcplb_doublefault_addr);
		printk(KERN_NOTICE "   ICPLB_FAULT_ADDR: %pF\n",
			initial_pda.icplb_doublefault_addr);
#endif
		printk(KERN_NOTICE " The instruction at %pF caused a double exception\n",
			initial_pda.retx);
	} else if (_bfin_swrst & RESET_WDOG)
		printk(KERN_INFO "Recovering from Watchdog event\n");
	else if (_bfin_swrst & RESET_SOFTWARE)
		printk(KERN_NOTICE "Reset caused by Software reset\n");
#endif
	printk(KERN_INFO "Blackfin support (C) 2004-2010 Analog Devices, Inc.\n");
	if (bfin_compiled_revid() == 0xffff)
		printk(KERN_INFO "Compiled for ADSP-%s Rev any, running on 0.%d\n", CPU, bfin_revid());
	else if (bfin_compiled_revid() == -1)
		printk(KERN_INFO "Compiled for ADSP-%s Rev none\n", CPU);
	else
		printk(KERN_INFO "Compiled for ADSP-%s Rev 0.%d\n", CPU, bfin_compiled_revid());

	if (likely(CPUID == bfin_cpuid())) {
		if (bfin_revid() != bfin_compiled_revid()) {
			if (bfin_compiled_revid() == -1)
				printk(KERN_ERR "Warning: Compiled for Rev none, but running on Rev %d\n",
				       bfin_revid());
			else if (bfin_compiled_revid() != 0xffff) {
				printk(KERN_ERR "Warning: Compiled for Rev %d, but running on Rev %d\n",
				       bfin_compiled_revid(), bfin_revid());
				if (bfin_compiled_revid() > bfin_revid())
					panic("Error: you are missing anomaly workarounds for this rev");
			}
		}
		if (bfin_revid() < CONFIG_BF_REV_MIN || bfin_revid() > CONFIG_BF_REV_MAX)
			printk(KERN_ERR "Warning: Unsupported Chip Revision ADSP-%s Rev 0.%d detected\n",
			       CPU, bfin_revid());
	}

	printk(KERN_INFO "Blackfin Linux support by http://blackfin.uclinux.org/\n");

#ifdef CONFIG_BF60x
	printk(KERN_INFO "Processor Speed: %lu MHz core clock, %lu MHz SCLk, %lu MHz SCLK0, %lu MHz SCLK1 and %lu MHz DCLK\n",
		cclk / 1000000, bfin_get_clk("SYSCLK") / 1000000, get_sclk0() / 1000000, get_sclk1() / 1000000, get_dclk() / 1000000);
#else
	printk(KERN_INFO "Processor Speed: %lu MHz core clock and %lu MHz System Clock\n",
	       cclk / 1000000, sclk / 1000000);
#endif

	setup_bootmem_allocator();

	paging_init();

	/* Copy atomic sequences to their fixed location, and sanity check that
	   these locations are the ones that we advertise to userspace.  */
	memcpy((void *)FIXED_CODE_START, &fixed_code_start,
	       FIXED_CODE_END - FIXED_CODE_START);
	BUG_ON((char *)&sigreturn_stub - (char *)&fixed_code_start
	       != SIGRETURN_STUB - FIXED_CODE_START);
	BUG_ON((char *)&atomic_xchg32 - (char *)&fixed_code_start
	       != ATOMIC_XCHG32 - FIXED_CODE_START);
	BUG_ON((char *)&atomic_cas32 - (char *)&fixed_code_start
	       != ATOMIC_CAS32 - FIXED_CODE_START);
	BUG_ON((char *)&atomic_add32 - (char *)&fixed_code_start
	       != ATOMIC_ADD32 - FIXED_CODE_START);
	BUG_ON((char *)&atomic_sub32 - (char *)&fixed_code_start
	       != ATOMIC_SUB32 - FIXED_CODE_START);
	BUG_ON((char *)&atomic_ior32 - (char *)&fixed_code_start
	       != ATOMIC_IOR32 - FIXED_CODE_START);
	BUG_ON((char *)&atomic_and32 - (char *)&fixed_code_start
	       != ATOMIC_AND32 - FIXED_CODE_START);
	BUG_ON((char *)&atomic_xor32 - (char *)&fixed_code_start
	       != ATOMIC_XOR32 - FIXED_CODE_START);
	BUG_ON((char *)&safe_user_instruction - (char *)&fixed_code_start
		!= SAFE_USER_INSTRUCTION - FIXED_CODE_START);

#ifdef CONFIG_SMP
	platform_init_cpus();
#endif
	init_exception_vectors();
	bfin_cache_init();	/* Initialize caches for the boot CPU */
}

static int __init topology_init(void)
{
	unsigned int cpu;

	for_each_possible_cpu(cpu) {
		register_cpu(&per_cpu(cpu_data, cpu).cpu, cpu);
	}

	return 0;
}

subsys_initcall(topology_init);

/* Get the input clock frequency */
static u_long cached_clkin_hz = CONFIG_CLKIN_HZ;
#ifndef CONFIG_BF60x
static u_long get_clkin_hz(void)
{
	return cached_clkin_hz;
}
#endif
static int __init early_init_clkin_hz(char *buf)
{
	cached_clkin_hz = simple_strtoul(buf, NULL, 0);
#ifdef BFIN_KERNEL_CLOCK
	if (cached_clkin_hz != CONFIG_CLKIN_HZ)
		panic("cannot change clkin_hz when reprogramming clocks");
#endif
	return 1;
}
early_param("clkin_hz=", early_init_clkin_hz);

#ifndef CONFIG_BF60x
/* Get the voltage input multiplier */
static u_long get_vco(void)
{
	static u_long cached_vco;
	u_long msel, pll_ctl;

	/* The assumption here is that VCO never changes at runtime.
	 * If, someday, we support that, then we'll have to change this.
	 */
	if (cached_vco)
		return cached_vco;

	pll_ctl = bfin_read_PLL_CTL();
	msel = (pll_ctl >> 9) & 0x3F;
	if (0 == msel)
		msel = 64;

	cached_vco = get_clkin_hz();
	cached_vco >>= (1 & pll_ctl);	/* DF bit */
	cached_vco *= msel;
	return cached_vco;
}
#endif

/* Get the Core clock */
u_long get_cclk(void)
{
#ifdef CONFIG_BF60x
	return bfin_get_clk("CCLK");
#else
	static u_long cached_cclk_pll_div, cached_cclk;
	u_long csel, ssel;

	if (bfin_read_PLL_STAT() & 0x1)
		return get_clkin_hz();

	ssel = bfin_read_PLL_DIV();
	if (ssel == cached_cclk_pll_div)
		return cached_cclk;
	else
		cached_cclk_pll_div = ssel;

	csel = ((ssel >> 4) & 0x03);
	ssel &= 0xf;
	if (ssel && ssel < (1 << csel))	/* SCLK > CCLK */
		cached_cclk = get_vco() / ssel;
	else
		cached_cclk = get_vco() >> csel;
	return cached_cclk;
#endif
}
EXPORT_SYMBOL(get_cclk);

#ifdef CONFIG_BF60x
/* Get the bf60x clock of SCLK0 domain */
u_long get_sclk0(void)
{
	return bfin_get_clk("SCLK0");
}
EXPORT_SYMBOL(get_sclk0);

/* Get the bf60x clock of SCLK1 domain */
u_long get_sclk1(void)
{
	return bfin_get_clk("SCLK1");
}
EXPORT_SYMBOL(get_sclk1);

/* Get the bf60x DRAM clock */
u_long get_dclk(void)
{
	return bfin_get_clk("DCLK");
}
EXPORT_SYMBOL(get_dclk);
#endif

/* Get the default system clock */
u_long get_sclk(void)
{
#ifdef CONFIG_BF60x
	return get_sclk0();
#else
	static u_long cached_sclk;
	u_long ssel;

	/* The assumption here is that SCLK never changes at runtime.
	 * If, someday, we support that, then we'll have to change this.
	 */
	if (cached_sclk)
		return cached_sclk;

	if (bfin_read_PLL_STAT() & 0x1)
		return get_clkin_hz();

	ssel = bfin_read_PLL_DIV() & 0xf;
	if (0 == ssel) {
		printk(KERN_WARNING "Invalid System Clock\n");
		ssel = 1;
	}

	cached_sclk = get_vco() / ssel;
	return cached_sclk;
#endif
}
EXPORT_SYMBOL(get_sclk);

unsigned long sclk_to_usecs(unsigned long sclk)
{
	u64 tmp = USEC_PER_SEC * (u64)sclk;
	do_div(tmp, get_sclk());
	return tmp;
}
EXPORT_SYMBOL(sclk_to_usecs);

unsigned long usecs_to_sclk(unsigned long usecs)
{
	u64 tmp = get_sclk() * (u64)usecs;
	do_div(tmp, USEC_PER_SEC);
	return tmp;
}
EXPORT_SYMBOL(usecs_to_sclk);

/*
 *	Get CPU information for use by the procfs.
 */
static int show_cpuinfo(struct seq_file *m, void *v)
{
	char *cpu, *mmu, *fpu, *vendor, *cache;
	uint32_t revid;
	int cpu_num = *(unsigned int *)v;
	u_long sclk, cclk;
	u_int icache_size = BFIN_ICACHESIZE / 1024, dcache_size = 0, dsup_banks = 0;
	struct blackfin_cpudata *cpudata = &per_cpu(cpu_data, cpu_num);

	cpu = CPU;
	mmu = "none";
	fpu = "none";
	revid = bfin_revid();

	sclk = get_sclk();
	cclk = get_cclk();

	switch (bfin_read_CHIPID() & CHIPID_MANUFACTURE) {
	case 0xca:
		vendor = "Analog Devices";
		break;
	default:
		vendor = "unknown";
		break;
	}

	seq_printf(m, "processor\t: %d\n" "vendor_id\t: %s\n", cpu_num, vendor);

	if (CPUID == bfin_cpuid())
		seq_printf(m, "cpu family\t: 0x%04x\n", CPUID);
	else
		seq_printf(m, "cpu family\t: Compiled for:0x%04x, running on:0x%04x\n",
			CPUID, bfin_cpuid());

	seq_printf(m, "model name\t: ADSP-%s %lu(MHz CCLK) %lu(MHz SCLK) (%s)\n"
		"stepping\t: %d ",
		cpu, cclk/1000000, sclk/1000000,
#ifdef CONFIG_MPU
		"mpu on",
#else
		"mpu off",
#endif
		revid);

	if (bfin_revid() != bfin_compiled_revid()) {
		if (bfin_compiled_revid() == -1)
			seq_printf(m, "(Compiled for Rev none)");
		else if (bfin_compiled_revid() == 0xffff)
			seq_printf(m, "(Compiled for Rev any)");
		else
			seq_printf(m, "(Compiled for Rev %d)", bfin_compiled_revid());
	}

	seq_printf(m, "\ncpu MHz\t\t: %lu.%03lu/%lu.%03lu\n",
		cclk/1000000, cclk%1000000,
		sclk/1000000, sclk%1000000);
	seq_printf(m, "bogomips\t: %lu.%02lu\n"
		"Calibration\t: %lu loops\n",
		(loops_per_jiffy * HZ) / 500000,
		((loops_per_jiffy * HZ) / 5000) % 100,
		(loops_per_jiffy * HZ));

	/* Check Cache configutation */
	switch (cpudata->dmemctl & (1 << DMC0_P | 1 << DMC1_P)) {
	case ACACHE_BSRAM:
		cache = "dbank-A/B\t: cache/sram";
		dcache_size = 16;
		dsup_banks = 1;
		break;
	case ACACHE_BCACHE:
		cache = "dbank-A/B\t: cache/cache";
		dcache_size = 32;
		dsup_banks = 2;
		break;
	case ASRAM_BSRAM:
		cache = "dbank-A/B\t: sram/sram";
		dcache_size = 0;
		dsup_banks = 0;
		break;
	default:
		cache = "unknown";
		dcache_size = 0;
		dsup_banks = 0;
		break;
	}

	/* Is it turned on? */
	if ((cpudata->dmemctl & (ENDCPLB | DMC_ENABLE)) != (ENDCPLB | DMC_ENABLE))
		dcache_size = 0;

	if ((cpudata->imemctl & (IMC | ENICPLB)) != (IMC | ENICPLB))
		icache_size = 0;

	seq_printf(m, "cache size\t: %d KB(L1 icache) "
		"%d KB(L1 dcache) %d KB(L2 cache)\n",
		icache_size, dcache_size, 0);
	seq_printf(m, "%s\n", cache);
	seq_printf(m, "external memory\t: "
#if defined(CONFIG_BFIN_EXTMEM_ICACHEABLE)
		   "cacheable"
#else
		   "uncacheable"
#endif
		   " in instruction cache\n");
	seq_printf(m, "external memory\t: "
#if defined(CONFIG_BFIN_EXTMEM_WRITEBACK)
		      "cacheable (write-back)"
#elif defined(CONFIG_BFIN_EXTMEM_WRITETHROUGH)
		      "cacheable (write-through)"
#else
		      "uncacheable"
#endif
		      " in data cache\n");

	if (icache_size)
		seq_printf(m, "icache setup\t: %d Sub-banks/%d Ways, %d Lines/Way\n",
			   BFIN_ISUBBANKS, BFIN_IWAYS, BFIN_ILINES);
	else
		seq_printf(m, "icache setup\t: off\n");

	seq_printf(m,
		   "dcache setup\t: %d Super-banks/%d Sub-banks/%d Ways, %d Lines/Way\n",
		   dsup_banks, BFIN_DSUBBANKS, BFIN_DWAYS,
		   BFIN_DLINES);
#ifdef __ARCH_SYNC_CORE_DCACHE
	seq_printf(m, "dcache flushes\t: %lu\n", dcache_invld_count[cpu_num]);
#endif
#ifdef __ARCH_SYNC_CORE_ICACHE
	seq_printf(m, "icache flushes\t: %lu\n", icache_invld_count[cpu_num]);
#endif

	seq_printf(m, "\n");

	if (cpu_num != num_possible_cpus() - 1)
		return 0;

	if (L2_LENGTH) {
		seq_printf(m, "L2 SRAM\t\t: %dKB\n", L2_LENGTH/0x400);
		seq_printf(m, "L2 SRAM\t\t: "
#if defined(CONFIG_BFIN_L2_ICACHEABLE)
			      "cacheable"
#else
			      "uncacheable"
#endif
			      " in instruction cache\n");
		seq_printf(m, "L2 SRAM\t\t: "
#if defined(CONFIG_BFIN_L2_WRITEBACK)
			      "cacheable (write-back)"
#elif defined(CONFIG_BFIN_L2_WRITETHROUGH)
			      "cacheable (write-through)"
#else
			      "uncacheable"
#endif
			      " in data cache\n");
	}
	seq_printf(m, "board name\t: %s\n", bfin_board_name);
	seq_printf(m, "board memory\t: %ld kB (0x%08lx -> 0x%08lx)\n",
		physical_mem_end >> 10, 0ul, physical_mem_end);
	seq_printf(m, "kernel memory\t: %d kB (0x%08lx -> 0x%08lx)\n",
		((int)memory_end - (int)_rambase) >> 10,
		_rambase, memory_end);

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	if (*pos == 0)
		*pos = cpumask_first(cpu_online_mask);
	if (*pos >= num_online_cpus())
		return NULL;

	return pos;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	*pos = cpumask_next(*pos, cpu_online_mask);

	return c_start(m, pos);
}

static void c_stop(struct seq_file *m, void *v)
{
}

const struct seq_operations cpuinfo_op = {
	.start = c_start,
	.next = c_next,
	.stop = c_stop,
	.show = show_cpuinfo,
};

void __init cmdline_init(const char *r0)
{
	early_shadow_stamp();
	if (r0)
		strncpy(command_line, r0, COMMAND_LINE_SIZE);
}

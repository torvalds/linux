/*
 * File:         arch/blackfin/kernel/setup.c
 * Based on:
 * Author:
 *
 * Created:
 * Description:
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/delay.h>
#include <linux/console.h>
#include <linux/bootmem.h>
#include <linux/seq_file.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/tty.h>

#include <linux/ext2_fs.h>
#include <linux/cramfs_fs.h>
#include <linux/romfs_fs.h>

#include <asm/cplb.h>
#include <asm/cacheflush.h>
#include <asm/blackfin.h>
#include <asm/cplbinit.h>
#include <asm/div64.h>
#include <asm/fixed_code.h>
#include <asm/early_printk.h>

u16 _bfin_swrst;

unsigned long memory_start, memory_end, physical_mem_end;
unsigned long reserved_mem_dcache_on;
unsigned long reserved_mem_icache_on;
EXPORT_SYMBOL(memory_start);
EXPORT_SYMBOL(memory_end);
EXPORT_SYMBOL(physical_mem_end);
EXPORT_SYMBOL(_ramend);

#ifdef CONFIG_MTD_UCLINUX
unsigned long memory_mtd_end, memory_mtd_start, mtd_size;
unsigned long _ebss;
EXPORT_SYMBOL(memory_mtd_end);
EXPORT_SYMBOL(memory_mtd_start);
EXPORT_SYMBOL(mtd_size);
#endif

char __initdata command_line[COMMAND_LINE_SIZE];

void __init bf53x_cache_init(void)
{
#if defined(CONFIG_BFIN_DCACHE) || defined(CONFIG_BFIN_ICACHE)
	generate_cpl_tables();
#endif

#ifdef CONFIG_BFIN_ICACHE
	bfin_icache_init();
	printk(KERN_INFO "Instruction Cache Enabled\n");
#endif

#ifdef CONFIG_BFIN_DCACHE
	bfin_dcache_init();
	printk(KERN_INFO "Data Cache Enabled"
# if defined CONFIG_BFIN_WB
		" (write-back)"
# elif defined CONFIG_BFIN_WT
		" (write-through)"
# endif
		"\n");
#endif
}

void __init bf53x_relocate_l1_mem(void)
{
	unsigned long l1_code_length;
	unsigned long l1_data_a_length;
	unsigned long l1_data_b_length;

	l1_code_length = _etext_l1 - _stext_l1;
	if (l1_code_length > L1_CODE_LENGTH)
		l1_code_length = L1_CODE_LENGTH;
	/* cannot complain as printk is not available as yet.
	 * But we can continue booting and complain later!
	 */

	/* Copy _stext_l1 to _etext_l1 to L1 instruction SRAM */
	dma_memcpy(_stext_l1, _l1_lma_start, l1_code_length);

	l1_data_a_length = _ebss_l1 - _sdata_l1;
	if (l1_data_a_length > L1_DATA_A_LENGTH)
		l1_data_a_length = L1_DATA_A_LENGTH;

	/* Copy _sdata_l1 to _ebss_l1 to L1 data bank A SRAM */
	dma_memcpy(_sdata_l1, _l1_lma_start + l1_code_length, l1_data_a_length);

	l1_data_b_length = _ebss_b_l1 - _sdata_b_l1;
	if (l1_data_b_length > L1_DATA_B_LENGTH)
		l1_data_b_length = L1_DATA_B_LENGTH;

	/* Copy _sdata_b_l1 to _ebss_b_l1 to L1 data bank B SRAM */
	dma_memcpy(_sdata_b_l1, _l1_lma_start + l1_code_length +
			l1_data_a_length, l1_data_b_length);

}

/*
 * Initial parsing of the command line.  Currently, we support:
 *  - Controlling the linux memory size: mem=xxx[KMG]
 *  - Controlling the physical memory size: max_mem=xxx[KMG][$][#]
 *       $ -> reserved memory is dcacheable
 *       # -> reserved memory is icacheable
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
							reserved_mem_dcache_on =
							    1;
						if (*to == '#'
						    || *(to + 1) == '#')
							reserved_mem_icache_on =
							    1;
					}
				}
			} else if (!memcmp(to, "earlyprintk=", 12)) {
				to += 12;
				setup_early_printk(to);
			}
		}
		c = *(to++);
		if (!c)
			break;
	}
}

void __init setup_arch(char **cmdline_p)
{
	int bootmap_size;
	unsigned long l1_length, sclk, cclk;
#ifdef CONFIG_MTD_UCLINUX
	unsigned long mtd_phys = 0;
#endif

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

	/* setup memory defaults from the user config */
	physical_mem_end = 0;
	_ramend = CONFIG_MEM_SIZE * 1024 * 1024;

	parse_cmdline_early(&command_line[0]);

	cclk = get_cclk();
	sclk = get_sclk();

#if !defined(CONFIG_BFIN_KERNEL_CLOCK)
	if (ANOMALY_05000273 && cclk == sclk)
		panic("ANOMALY 05000273, SCLK can not be same as CCLK");
#endif

#ifdef BF561_FAMILY
	if (ANOMALY_05000266) {
		bfin_read_IMDMA_D0_IRQ_STATUS();
		bfin_read_IMDMA_D1_IRQ_STATUS();
	}
#endif

	printk(KERN_INFO "Hardware Trace ");
	if (bfin_read_TBUFCTL() & 0x1 )
		printk("Active ");
	else
		printk("Off ");
	if (bfin_read_TBUFCTL() & 0x2)
		printk("and Enabled\n");
	else
	printk("and Disabled\n");


#if defined(CONFIG_CHR_DEV_FLASH) || defined(CONFIG_BLK_DEV_FLASH)
	/* we need to initialize the Flashrom device here since we might
	 * do things with flash early on in the boot
	 */
	flash_probe();
#endif

	if (physical_mem_end == 0)
		physical_mem_end = _ramend;

	/* by now the stack is part of the init task */
	memory_end = _ramend - DMA_UNCACHED_REGION;

	_ramstart = (unsigned long)__bss_stop;
	_rambase = (unsigned long)_stext;
#ifdef CONFIG_MPU
	/* Round up to multiple of 4MB.  */
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
	    && ((unsigned long *)mtd_phys)[1] == ROMSB_WORD1)
		mtd_size =
		    PAGE_ALIGN(be32_to_cpu(((unsigned long *)mtd_phys)[2]));
#  if (defined(CONFIG_BFIN_ICACHE) && ANOMALY_05000263)
	/* Due to a Hardware Anomaly we need to limit the size of usable
	 * instruction memory to max 60MB, 56 if HUNT_FOR_ZERO is on
	 * 05000263 - Hardware loop corrupted when taking an ICPLB exception
	 */
#   if (defined(CONFIG_DEBUG_HUNT_FOR_ZERO))
	if (memory_end >= 56 * 1024 * 1024)
		memory_end = 56 * 1024 * 1024;
#   else
	if (memory_end >= 60 * 1024 * 1024)
		memory_end = 60 * 1024 * 1024;
#   endif				/* CONFIG_DEBUG_HUNT_FOR_ZERO */
#  endif				/* ANOMALY_05000263 */
# endif				/* CONFIG_ROMFS_FS */

	memory_end -= mtd_size;

	if (mtd_size == 0) {
		console_init();
		panic("Don't boot kernel without rootfs attached.\n");
	}

	/* Relocate MTD image to the top of memory after the uncached memory area */
	dma_memcpy((char *)memory_end, __bss_stop, mtd_size);

	memory_mtd_start = memory_end;
	_ebss = memory_mtd_start;	/* define _ebss for compatible */
#endif				/* CONFIG_MTD_UCLINUX */

#if (defined(CONFIG_BFIN_ICACHE) && ANOMALY_05000263)
	/* Due to a Hardware Anomaly we need to limit the size of usable
	 * instruction memory to max 60MB, 56 if HUNT_FOR_ZERO is on
	 * 05000263 - Hardware loop corrupted when taking an ICPLB exception
	 */
#if (defined(CONFIG_DEBUG_HUNT_FOR_ZERO))
	if (memory_end >= 56 * 1024 * 1024)
		memory_end = 56 * 1024 * 1024;
#else
	if (memory_end >= 60 * 1024 * 1024)
		memory_end = 60 * 1024 * 1024;
#endif				/* CONFIG_DEBUG_HUNT_FOR_ZERO */
	printk(KERN_NOTICE "Warning: limiting memory to %liMB due to hardware anomaly 05000263\n", memory_end >> 20);
#endif				/* ANOMALY_05000263 */

#ifdef CONFIG_MPU
	page_mask_nelts = ((_ramend >> PAGE_SHIFT) + 31) / 32;
	page_mask_order = get_order(3 * page_mask_nelts * sizeof(long));
#endif

#if !defined(CONFIG_MTD_UCLINUX)
	memory_end -= SIZE_4K; /*In case there is no valid CPLB behind memory_end make sure we don't get to close*/
#endif
	init_mm.start_code = (unsigned long)_stext;
	init_mm.end_code = (unsigned long)_etext;
	init_mm.end_data = (unsigned long)_edata;
	init_mm.brk = (unsigned long)0;

	_bfin_swrst = bfin_read_SWRST();

	if (_bfin_swrst & RESET_DOUBLE)
		printk(KERN_INFO "Recovering from Double Fault event\n");
	else if (_bfin_swrst & RESET_WDOG)
		printk(KERN_INFO "Recovering from Watchdog event\n");
	else if (_bfin_swrst & RESET_SOFTWARE)
		printk(KERN_NOTICE "Reset caused by Software reset\n");

	printk(KERN_INFO "Blackfin support (C) 2004-2007 Analog Devices, Inc.\n");
	if (bfin_compiled_revid() == 0xffff)
		printk(KERN_INFO "Compiled for ADSP-%s Rev any\n", CPU);
	else if (bfin_compiled_revid() == -1)
		printk(KERN_INFO "Compiled for ADSP-%s Rev none\n", CPU);
	else
		printk(KERN_INFO "Compiled for ADSP-%s Rev 0.%d\n", CPU, bfin_compiled_revid());
	if (bfin_revid() != bfin_compiled_revid()) {
		if (bfin_compiled_revid() == -1)
			printk(KERN_ERR "Warning: Compiled for Rev none, but running on Rev %d\n",
			       bfin_revid());
		else if (bfin_compiled_revid() != 0xffff)
			printk(KERN_ERR "Warning: Compiled for Rev %d, but running on Rev %d\n",
			       bfin_compiled_revid(), bfin_revid());
	}
	if (bfin_revid() < SUPPORTED_REVID)
		printk(KERN_ERR "Warning: Unsupported Chip Revision ADSP-%s Rev 0.%d detected\n",
		       CPU, bfin_revid());
	printk(KERN_INFO "Blackfin Linux support by http://blackfin.uclinux.org/\n");

	printk(KERN_INFO "Processor Speed: %lu MHz core clock and %lu MHz System Clock\n",
	       cclk / 1000000,  sclk / 1000000);

	if (ANOMALY_05000273 && (cclk >> 1) <= sclk)
		printk("\n\n\nANOMALY_05000273: CCLK must be >= 2*SCLK !!!\n\n\n");

	printk(KERN_INFO "Board Memory: %ldMB\n", physical_mem_end >> 20);
	printk(KERN_INFO "Kernel Managed Memory: %ldMB\n", _ramend >> 20);

	printk(KERN_INFO "Memory map:\n"
	       KERN_INFO "  text      = 0x%p-0x%p\n"
	       KERN_INFO "  rodata    = 0x%p-0x%p\n"
	       KERN_INFO "  data      = 0x%p-0x%p\n"
	       KERN_INFO "    stack   = 0x%p-0x%p\n"
	       KERN_INFO "  init      = 0x%p-0x%p\n"
	       KERN_INFO "  bss       = 0x%p-0x%p\n"
	       KERN_INFO "  available = 0x%p-0x%p\n"
#ifdef CONFIG_MTD_UCLINUX
	       KERN_INFO "  rootfs    = 0x%p-0x%p\n"
#endif
#if DMA_UNCACHED_REGION > 0
	       KERN_INFO "  DMA Zone  = 0x%p-0x%p\n"
#endif
	       , _stext, _etext,
	       __start_rodata, __end_rodata,
	       _sdata, _edata,
	       (void *)&init_thread_union, (void *)((int)(&init_thread_union) + 0x2000),
	       __init_begin, __init_end,
	       __bss_start, __bss_stop,
	       (void *)_ramstart, (void *)memory_end
#ifdef CONFIG_MTD_UCLINUX
	       , (void *)memory_mtd_start, (void *)(memory_mtd_start + mtd_size)
#endif
#if DMA_UNCACHED_REGION > 0
	       , (void *)(_ramend - DMA_UNCACHED_REGION), (void *)(_ramend)
#endif
	       );

	/*
	 * give all the memory to the bootmap allocator,  tell it to put the
	 * boot mem_map at the start of memory
	 */
	bootmap_size = init_bootmem_node(NODE_DATA(0), memory_start >> PAGE_SHIFT,	/* map goes here */
					 PAGE_OFFSET >> PAGE_SHIFT,
					 memory_end >> PAGE_SHIFT);
	/*
	 * free the usable memory,  we have to make sure we do not free
	 * the bootmem bitmap so we then reserve it after freeing it :-)
	 */
	free_bootmem(memory_start, memory_end - memory_start);

	reserve_bootmem(memory_start, bootmap_size);
	/*
	 * get kmalloc into gear
	 */
	paging_init();

	/* check the size of the l1 area */
	l1_length = _etext_l1 - _stext_l1;
	if (l1_length > L1_CODE_LENGTH)
		panic("L1 code memory overflow\n");

	l1_length = _ebss_l1 - _sdata_l1;
	if (l1_length > L1_DATA_A_LENGTH)
		panic("L1 data memory overflow\n");

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

	init_exception_vectors();
	bf53x_cache_init();
}

static int __init topology_init(void)
{
#if defined (CONFIG_BF561)
	static struct cpu cpu[2];
	register_cpu(&cpu[0], 0);
	register_cpu(&cpu[1], 1);
	return 0;
#else
	static struct cpu cpu[1];
	return register_cpu(cpu, 0);
#endif
}

subsys_initcall(topology_init);

static u_long get_vco(void)
{
	u_long msel;
	u_long vco;

	msel = (bfin_read_PLL_CTL() >> 9) & 0x3F;
	if (0 == msel)
		msel = 64;

	vco = CONFIG_CLKIN_HZ;
	vco >>= (1 & bfin_read_PLL_CTL());	/* DF bit */
	vco = msel * vco;
	return vco;
}

/* Get the Core clock */
u_long get_cclk(void)
{
	u_long csel, ssel;
	if (bfin_read_PLL_STAT() & 0x1)
		return CONFIG_CLKIN_HZ;

	ssel = bfin_read_PLL_DIV();
	csel = ((ssel >> 4) & 0x03);
	ssel &= 0xf;
	if (ssel && ssel < (1 << csel))	/* SCLK > CCLK */
		return get_vco() / ssel;
	return get_vco() >> csel;
}
EXPORT_SYMBOL(get_cclk);

/* Get the System clock */
u_long get_sclk(void)
{
	u_long ssel;

	if (bfin_read_PLL_STAT() & 0x1)
		return CONFIG_CLKIN_HZ;

	ssel = (bfin_read_PLL_DIV() & 0xf);
	if (0 == ssel) {
		printk(KERN_WARNING "Invalid System Clock\n");
		ssel = 1;
	}

	return get_vco() / ssel;
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

	u_long cclk = 0, sclk = 0;
	u_int dcache_size = 0, dsup_banks = 0;

	cpu = CPU;
	mmu = "none";
	fpu = "none";
	revid = bfin_revid();

	cclk = get_cclk();
	sclk = get_sclk();

	switch (bfin_read_CHIPID() & CHIPID_MANUFACTURE) {
	case 0xca:
		vendor = "Analog Devices";
		break;
	default:
		vendor = "unknown";
		break;
	}

	seq_printf(m, "processor\t: %d\n"
		"vendor_id\t: %s\n"
		"cpu family\t: 0x%x\n"
		"model name\t: ADSP-%s %lu(MHz CCLK) %lu(MHz SCLK)\n"
		"stepping\t: %d\n",
		0,
		vendor,
		(bfin_read_CHIPID() & CHIPID_FAMILY),
		cpu, cclk/1000000, sclk/1000000,
		revid);

	seq_printf(m, "cpu MHz\t\t: %lu.%03lu/%lu.%03lu\n",
		cclk/1000000, cclk%1000000,
		sclk/1000000, sclk%1000000);
	seq_printf(m, "bogomips\t: %lu.%02lu\n"
		"Calibration\t: %lu loops\n",
		(loops_per_jiffy * HZ) / 500000,
		((loops_per_jiffy * HZ) / 5000) % 100,
		(loops_per_jiffy * HZ));

	/* Check Cache configutation */
	switch (bfin_read_DMEM_CONTROL() & (1 << DMC0_P | 1 << DMC1_P)) {
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
	if (!((bfin_read_DMEM_CONTROL()) & (ENDCPLB | DMC_ENABLE)))
		dcache_size = 0;

	seq_printf(m, "cache size\t: %d KB(L1 icache) "
		"%d KB(L1 dcache-%s) %d KB(L2 cache)\n",
		BFIN_ICACHESIZE / 1024, dcache_size,
#if defined CONFIG_BFIN_WB
		"wb"
#elif defined CONFIG_BFIN_WT
		"wt"
#endif
		"", 0);

	seq_printf(m, "%s\n", cache);

	seq_printf(m, "icache setup\t: %d Sub-banks/%d Ways, %d Lines/Way\n",
		   BFIN_ISUBBANKS, BFIN_IWAYS, BFIN_ILINES);
	seq_printf(m,
		   "dcache setup\t: %d Super-banks/%d Sub-banks/%d Ways, %d Lines/Way\n",
		   dsup_banks, BFIN_DSUBBANKS, BFIN_DWAYS,
		   BFIN_DLINES);
#ifdef CONFIG_BFIN_ICACHE_LOCK
	switch (read_iloc()) {
	case WAY0_L:
		seq_printf(m, "Way0 Locked-Down\n");
		break;
	case WAY1_L:
		seq_printf(m, "Way1 Locked-Down\n");
		break;
	case WAY01_L:
		seq_printf(m, "Way0,Way1 Locked-Down\n");
		break;
	case WAY2_L:
		seq_printf(m, "Way2 Locked-Down\n");
		break;
	case WAY02_L:
		seq_printf(m, "Way0,Way2 Locked-Down\n");
		break;
	case WAY12_L:
		seq_printf(m, "Way1,Way2 Locked-Down\n");
		break;
	case WAY012_L:
		seq_printf(m, "Way0,Way1 & Way2 Locked-Down\n");
		break;
	case WAY3_L:
		seq_printf(m, "Way3 Locked-Down\n");
		break;
	case WAY03_L:
		seq_printf(m, "Way0,Way3 Locked-Down\n");
		break;
	case WAY13_L:
		seq_printf(m, "Way1,Way3 Locked-Down\n");
		break;
	case WAY013_L:
		seq_printf(m, "Way 0,Way1,Way3 Locked-Down\n");
		break;
	case WAY32_L:
		seq_printf(m, "Way3,Way2 Locked-Down\n");
		break;
	case WAY320_L:
		seq_printf(m, "Way3,Way2,Way0 Locked-Down\n");
		break;
	case WAY321_L:
		seq_printf(m, "Way3,Way2,Way1 Locked-Down\n");
		break;
	case WAYALL_L:
		seq_printf(m, "All Ways are locked\n");
		break;
	default:
		seq_printf(m, "No Ways are locked\n");
	}
#endif

	seq_printf(m, "board name\t: %s\n", bfin_board_name);
	seq_printf(m, "board memory\t: %ld kB (0x%p -> 0x%p)\n",
		 physical_mem_end >> 10, (void *)0, (void *)physical_mem_end);
	seq_printf(m, "kernel memory\t: %d kB (0x%p -> 0x%p)\n",
		((int)memory_end - (int)_stext) >> 10,
		_stext,
		(void *)memory_end);

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < NR_CPUS ? ((void *)0x12345678) : NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}

static void c_stop(struct seq_file *m, void *v)
{
}

struct seq_operations cpuinfo_op = {
	.start = c_start,
	.next = c_next,
	.stop = c_stop,
	.show = show_cpuinfo,
};

void __init cmdline_init(const char *r0)
{
	if (r0)
		strncpy(command_line, r0, COMMAND_LINE_SIZE);
}

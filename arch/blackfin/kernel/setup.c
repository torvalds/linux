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

#include <asm/cacheflush.h>
#include <asm/blackfin.h>
#include <asm/cplbinit.h>

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

#if defined(CONFIG_BLKFIN_DCACHE) || defined(CONFIG_BLKFIN_CACHE)
static void generate_cpl_tables(void);
#endif

void __init bf53x_cache_init(void)
{
#if defined(CONFIG_BLKFIN_DCACHE) || defined(CONFIG_BLKFIN_CACHE)
	generate_cpl_tables();
#endif

#ifdef CONFIG_BLKFIN_CACHE
	bfin_icache_init();
	printk(KERN_INFO "Instruction Cache Enabled\n");
#endif

#ifdef CONFIG_BLKFIN_DCACHE
	bfin_dcache_init();
	printk(KERN_INFO "Data Cache Enabled"
# if defined CONFIG_BLKFIN_WB
		" (write-back)"
# elif defined CONFIG_BLKFIN_WT
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
	cclk = get_cclk();
	sclk = get_sclk();

#if !defined(CONFIG_BFIN_KERNEL_CLOCK) && defined(ANOMALY_05000273)
	if (cclk == sclk)
		panic("ANOMALY 05000273, SCLK can not be same as CCLK");
#endif

#if defined(ANOMALY_05000266)
	bfin_read_IMDMA_D0_IRQ_STATUS();
	bfin_read_IMDMA_D1_IRQ_STATUS();
#endif

#ifdef DEBUG_SERIAL_EARLY_INIT
	bfin_console_init();	/* early console registration */
	/* this give a chance to get printk() working before crash. */
#endif

#if defined(CONFIG_CHR_DEV_FLASH) || defined(CONFIG_BLK_DEV_FLASH)
	/* we need to initialize the Flashrom device here since we might
	 * do things with flash early on in the boot
	 */
	flash_probe();
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

	if (physical_mem_end == 0)
		physical_mem_end = _ramend;

	/* by now the stack is part of the init task */
	memory_end = _ramend - DMA_UNCACHED_REGION;

	_ramstart = (unsigned long)__bss_stop;
	memory_start = PAGE_ALIGN(_ramstart);

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
#  if (defined(CONFIG_BLKFIN_CACHE) && defined(ANOMALY_05000263))
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

#if (defined(CONFIG_BLKFIN_CACHE) && defined(ANOMALY_05000263))
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

#if !defined(CONFIG_MTD_UCLINUX)
	memory_end -= SIZE_4K; /*In case there is no valid CPLB behind memory_end make sure we don't get to close*/
#endif
	init_mm.start_code = (unsigned long)_stext;
	init_mm.end_code = (unsigned long)_etext;
	init_mm.end_data = (unsigned long)_edata;
	init_mm.brk = (unsigned long)0;

	init_leds();

	printk(KERN_INFO "Blackfin support (C) 2004-2007 Analog Devices, Inc.\n");
	printk(KERN_INFO "Compiled for ADSP-%s Rev 0.%d\n", CPU, bfin_compiled_revid());
	if (bfin_revid() != bfin_compiled_revid())
		printk(KERN_ERR "Warning: Compiled for Rev %d, but running on Rev %d\n",
		       bfin_compiled_revid(), bfin_revid());
	if (bfin_revid() < SUPPORTED_REVID)
		printk(KERN_ERR "Warning: Unsupported Chip Revision ADSP-%s Rev 0.%d detected\n",
		       CPU, bfin_revid());
	printk(KERN_INFO "Blackfin Linux support by http://blackfin.uclinux.org/\n");

	printk(KERN_INFO "Processor Speed: %lu MHz core clock and %lu Mhz System Clock\n",
	       cclk / 1000000,  sclk / 1000000);

#if defined(ANOMALY_05000273)
	if ((cclk >> 1) <= sclk)
		printk("\n\n\nANOMALY_05000273: CCLK must be >= 2*SCLK !!!\n\n\n");
#endif

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
	       (void*)&init_thread_union, (void*)((int)(&init_thread_union) + 0x2000),
	       __init_begin, __init_end,
	       __bss_start, __bss_stop,
	       (void*)_ramstart, (void*)memory_end
#ifdef CONFIG_MTD_UCLINUX
	       , (void*)memory_mtd_start, (void*)(memory_mtd_start + mtd_size)
#endif
#if DMA_UNCACHED_REGION > 0
	       , (void*)(_ramend - DMA_UNCACHED_REGION), (void*)(_ramend)
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
		panic("L1 memory overflow\n");

	l1_length = _ebss_l1 - _sdata_l1;
	if (l1_length > L1_DATA_A_LENGTH)
		panic("L1 memory overflow\n");

#ifdef BF561_FAMILY
	_bfin_swrst = bfin_read_SICA_SWRST();
#else
	_bfin_swrst = bfin_read_SWRST();
#endif

	bf53x_cache_init();

	printk(KERN_INFO "Hardware Trace Enabled\n");
	bfin_write_TBUFCTL(0x03);
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

#if defined(CONFIG_BLKFIN_DCACHE) || defined(CONFIG_BLKFIN_CACHE)
static u16 __init lock_kernel_check(u32 start, u32 end)
{
	if ((start <= (u32) _stext && end >= (u32) _end)
	    || (start >= (u32) _stext && end <= (u32) _end))
		return IN_KERNEL;
	return 0;
}

static unsigned short __init
fill_cplbtab(struct cplb_tab *table,
	     unsigned long start, unsigned long end,
	     unsigned long block_size, unsigned long cplb_data)
{
	int i;

	switch (block_size) {
	case SIZE_4M:
		i = 3;
		break;
	case SIZE_1M:
		i = 2;
		break;
	case SIZE_4K:
		i = 1;
		break;
	case SIZE_1K:
	default:
		i = 0;
		break;
	}

	cplb_data = (cplb_data & ~(3 << 16)) | (i << 16);

	while ((start < end) && (table->pos < table->size)) {

		table->tab[table->pos++] = start;

		if (lock_kernel_check(start, start + block_size) == IN_KERNEL)
			table->tab[table->pos++] =
			    cplb_data | CPLB_LOCK | CPLB_DIRTY;
		else
			table->tab[table->pos++] = cplb_data;

		start += block_size;
	}
	return 0;
}

static unsigned short __init
close_cplbtab(struct cplb_tab *table)
{

	while (table->pos < table->size) {

		table->tab[table->pos++] = 0;
		table->tab[table->pos++] = 0; /* !CPLB_VALID */
	}
	return 0;
}

/* helper function */
static void __fill_code_cplbtab(struct cplb_tab *t, int i,
				u32 a_start, u32 a_end)
{
	if (cplb_data[i].psize) {
		fill_cplbtab(t,
				cplb_data[i].start,
				cplb_data[i].end,
				cplb_data[i].psize,
				cplb_data[i].i_conf);
	} else {
#if (defined(CONFIG_BLKFIN_CACHE) && defined(ANOMALY_05000263))
		if (i == SDRAM_KERN) {
			fill_cplbtab(t,
					cplb_data[i].start,
					cplb_data[i].end,
					SIZE_4M,
					cplb_data[i].i_conf);
		} else {
#endif
			fill_cplbtab(t,
					cplb_data[i].start,
					a_start,
					SIZE_1M,
					cplb_data[i].i_conf);
			fill_cplbtab(t,
					a_start,
					a_end,
					SIZE_4M,
					cplb_data[i].i_conf);
			fill_cplbtab(t, a_end,
					cplb_data[i].end,
					SIZE_1M,
					cplb_data[i].i_conf);
		}
	}
}

static void __fill_data_cplbtab(struct cplb_tab *t, int i,
				u32 a_start, u32 a_end)
{
	if (cplb_data[i].psize) {
		fill_cplbtab(t,
				cplb_data[i].start,
				cplb_data[i].end,
				cplb_data[i].psize,
				cplb_data[i].d_conf);
	} else {
		fill_cplbtab(t,
				cplb_data[i].start,
				a_start, SIZE_1M,
				cplb_data[i].d_conf);
		fill_cplbtab(t, a_start,
				a_end, SIZE_4M,
				cplb_data[i].d_conf);
		fill_cplbtab(t, a_end,
				cplb_data[i].end,
				SIZE_1M,
				cplb_data[i].d_conf);
	}
}
static void __init generate_cpl_tables(void)
{

	u16 i, j, process;
	u32 a_start, a_end, as, ae, as_1m;

	struct cplb_tab *t_i = NULL;
	struct cplb_tab *t_d = NULL;
	struct s_cplb cplb;

	cplb.init_i.size = MAX_CPLBS;
	cplb.init_d.size = MAX_CPLBS;
	cplb.switch_i.size = MAX_SWITCH_I_CPLBS;
	cplb.switch_d.size = MAX_SWITCH_D_CPLBS;

	cplb.init_i.pos = 0;
	cplb.init_d.pos = 0;
	cplb.switch_i.pos = 0;
	cplb.switch_d.pos = 0;

	cplb.init_i.tab = icplb_table;
	cplb.init_d.tab = dcplb_table;
	cplb.switch_i.tab = ipdt_table;
	cplb.switch_d.tab = dpdt_table;

	cplb_data[SDRAM_KERN].end = memory_end;

#ifdef CONFIG_MTD_UCLINUX
	cplb_data[SDRAM_RAM_MTD].start = memory_mtd_start;
	cplb_data[SDRAM_RAM_MTD].end = memory_mtd_start + mtd_size;
	cplb_data[SDRAM_RAM_MTD].valid = mtd_size > 0;
# if defined(CONFIG_ROMFS_FS)
	cplb_data[SDRAM_RAM_MTD].attr |= I_CPLB;

	/*
	 * The ROMFS_FS size is often not multiple of 1MB.
	 * This can cause multiple CPLB sets covering the same memory area.
	 * This will then cause multiple CPLB hit exceptions.
	 * Workaround: We ensure a contiguous memory area by extending the kernel
	 * memory section over the mtd section.
	 * For ROMFS_FS memory must be covered with ICPLBs anyways.
	 * So there is no difference between kernel and mtd memory setup.
	 */

	cplb_data[SDRAM_KERN].end = memory_mtd_start + mtd_size;;
	cplb_data[SDRAM_RAM_MTD].valid = 0;

# endif
#else
	cplb_data[SDRAM_RAM_MTD].valid = 0;
#endif

	cplb_data[SDRAM_DMAZ].start = _ramend - DMA_UNCACHED_REGION;
	cplb_data[SDRAM_DMAZ].end = _ramend;

	cplb_data[RES_MEM].start = _ramend;
	cplb_data[RES_MEM].end = physical_mem_end;

	if (reserved_mem_dcache_on)
		cplb_data[RES_MEM].d_conf = SDRAM_DGENERIC;
	else
		cplb_data[RES_MEM].d_conf = SDRAM_DNON_CHBL;

	if (reserved_mem_icache_on)
		cplb_data[RES_MEM].i_conf = SDRAM_IGENERIC;
	else
		cplb_data[RES_MEM].i_conf = SDRAM_INON_CHBL;

	for (i = ZERO_P; i <= L2_MEM; i++) {
		if (!cplb_data[i].valid)
			continue;

		as_1m = cplb_data[i].start % SIZE_1M;

		/*
		 * We need to make sure all sections are properly 1M aligned
		 * However between Kernel Memory and the Kernel mtd section,
		 * depending on the rootfs size, there can be overlapping
		 * memory areas.
		 */

		if (as_1m && i != L1I_MEM && i != L1D_MEM) {
#ifdef CONFIG_MTD_UCLINUX
			if (i == SDRAM_RAM_MTD) {
				if ((cplb_data[SDRAM_KERN].end + 1) >
						cplb_data[SDRAM_RAM_MTD].start)
					cplb_data[SDRAM_RAM_MTD].start =
						(cplb_data[i].start &
						 (-2*SIZE_1M)) + SIZE_1M;
				else
					cplb_data[SDRAM_RAM_MTD].start =
						(cplb_data[i].start &
						 (-2*SIZE_1M));
			} else
#endif
				printk(KERN_WARNING
					"Unaligned Start of %s at 0x%X\n",
					cplb_data[i].name, cplb_data[i].start);
		}

		as = cplb_data[i].start % SIZE_4M;
		ae = cplb_data[i].end % SIZE_4M;

		if (as)
			a_start = cplb_data[i].start + (SIZE_4M - (as));
		else
			a_start = cplb_data[i].start;

		a_end = cplb_data[i].end - ae;

		for (j = INITIAL_T; j <= SWITCH_T; j++) {

			switch (j) {
			case INITIAL_T:
				if (cplb_data[i].attr & INITIAL_T) {
					t_i = &cplb.init_i;
					t_d = &cplb.init_d;
					process = 1;
				} else
					process = 0;
				break;
			case SWITCH_T:
				if (cplb_data[i].attr & SWITCH_T) {
					t_i = &cplb.switch_i;
					t_d = &cplb.switch_d;
					process = 1;
				} else
					process = 0;
				break;
			default:
					process = 0;
				break;
			}

			if (!process)
				continue;
			if (cplb_data[i].attr & I_CPLB)
				__fill_code_cplbtab(t_i, i, a_start, a_end);

			if (cplb_data[i].attr & D_CPLB)
				__fill_data_cplbtab(t_d, i, a_start, a_end);
		}
	}

/* close tables */

	close_cplbtab(&cplb.init_i);
	close_cplbtab(&cplb.init_d);

	cplb.init_i.tab[cplb.init_i.pos] = -1;
	cplb.init_d.tab[cplb.init_d.pos] = -1;
	cplb.switch_i.tab[cplb.switch_i.pos] = -1;
	cplb.switch_d.tab[cplb.switch_d.pos] = -1;

}

#endif

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

/*Get the Core clock*/
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

/*
 *	Get CPU information for use by the procfs.
 */
static int show_cpuinfo(struct seq_file *m, void *v)
{
	char *cpu, *mmu, *fpu, *name;
	uint32_t revid;

	u_long cclk = 0, sclk = 0;
	u_int dcache_size = 0, dsup_banks = 0;

	cpu = CPU;
	mmu = "none";
	fpu = "none";
	revid = bfin_revid();
	name = bfin_board_name;

	cclk = get_cclk();
	sclk = get_sclk();

	seq_printf(m, "CPU:\t\tADSP-%s Rev. 0.%d\n"
		   "MMU:\t\t%s\n"
		   "FPU:\t\t%s\n"
		   "Core Clock:\t%9lu Hz\n"
		   "System Clock:\t%9lu Hz\n"
		   "BogoMips:\t%lu.%02lu\n"
		   "Calibration:\t%lu loops\n",
		   cpu, revid, mmu, fpu,
		   cclk,
		   sclk,
		   (loops_per_jiffy * HZ) / 500000,
		   ((loops_per_jiffy * HZ) / 5000) % 100,
		   (loops_per_jiffy * HZ));
	seq_printf(m, "Board Name:\t%s\n", name);
	seq_printf(m, "Board Memory:\t%ld MB\n", physical_mem_end >> 20);
	seq_printf(m, "Kernel Memory:\t%ld MB\n", (unsigned long)_ramend >> 20);
	if (bfin_read_IMEM_CONTROL() & (ENICPLB | IMC))
		seq_printf(m, "I-CACHE:\tON\n");
	else
		seq_printf(m, "I-CACHE:\tOFF\n");
	if ((bfin_read_DMEM_CONTROL()) & (ENDCPLB | DMC_ENABLE))
		seq_printf(m, "D-CACHE:\tON"
#if defined CONFIG_BLKFIN_WB
			   " (write-back)"
#elif defined CONFIG_BLKFIN_WT
			   " (write-through)"
#endif
			   "\n");
	else
		seq_printf(m, "D-CACHE:\tOFF\n");


	switch(bfin_read_DMEM_CONTROL() & (1 << DMC0_P | 1 << DMC1_P)) {
		case ACACHE_BSRAM:
			seq_printf(m, "DBANK-A:\tCACHE\n" "DBANK-B:\tSRAM\n");
			dcache_size = 16;
			dsup_banks = 1;
			break;
		case ACACHE_BCACHE:
			seq_printf(m, "DBANK-A:\tCACHE\n" "DBANK-B:\tCACHE\n");
			dcache_size = 32;
			dsup_banks = 2;
			break;
		case ASRAM_BSRAM:
			seq_printf(m, "DBANK-A:\tSRAM\n" "DBANK-B:\tSRAM\n");
			dcache_size = 0;
			dsup_banks = 0;
			break;
		default:
		break;
	}


	seq_printf(m, "I-CACHE Size:\t%dKB\n", BLKFIN_ICACHESIZE / 1024);
	seq_printf(m, "D-CACHE Size:\t%dKB\n", dcache_size);
	seq_printf(m, "I-CACHE Setup:\t%d Sub-banks/%d Ways, %d Lines/Way\n",
		   BLKFIN_ISUBBANKS, BLKFIN_IWAYS, BLKFIN_ILINES);
	seq_printf(m,
		   "D-CACHE Setup:\t%d Super-banks/%d Sub-banks/%d Ways, %d Lines/Way\n",
		   dsup_banks, BLKFIN_DSUBBANKS, BLKFIN_DWAYS,
		   BLKFIN_DLINES);
#ifdef CONFIG_BLKFIN_CACHE_LOCK
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

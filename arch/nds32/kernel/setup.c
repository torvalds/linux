// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/cpu.h>
#include <linux/memblock.h>
#include <linux/seq_file.h>
#include <linux/console.h>
#include <linux/screen_info.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <asm/setup.h>
#include <asm/sections.h>
#include <asm/proc-fns.h>
#include <asm/cache_info.h>
#include <asm/elf.h>
#include <nds32_intrinsic.h>

#define HWCAP_MFUSR_PC		0x000001
#define HWCAP_EXT		0x000002
#define HWCAP_EXT2		0x000004
#define HWCAP_FPU		0x000008
#define HWCAP_AUDIO		0x000010
#define HWCAP_BASE16		0x000020
#define HWCAP_STRING		0x000040
#define HWCAP_REDUCED_REGS	0x000080
#define HWCAP_VIDEO		0x000100
#define HWCAP_ENCRYPT		0x000200
#define HWCAP_EDM		0x000400
#define HWCAP_LMDMA		0x000800
#define HWCAP_PFM		0x001000
#define HWCAP_HSMP		0x002000
#define HWCAP_TRACE		0x004000
#define HWCAP_DIV		0x008000
#define HWCAP_MAC		0x010000
#define HWCAP_L2C		0x020000
#define HWCAP_FPU_DP		0x040000
#define HWCAP_V2		0x080000
#define HWCAP_DX_REGS		0x100000

unsigned long cpu_id, cpu_rev, cpu_cfgid;
char cpu_series;
char *endianness = NULL;

unsigned int __atags_pointer __initdata;
unsigned int elf_hwcap;
EXPORT_SYMBOL(elf_hwcap);

/*
 * The following string table, must sync with HWCAP_xx bitmask,
 * which is defined in <asm/procinfo.h>
 */
static const char *hwcap_str[] = {
	"mfusr_pc",
	"perf1",
	"perf2",
	"fpu",
	"audio",
	"16b",
	"string",
	"reduced_regs",
	"video",
	"encrypt",
	"edm",
	"lmdma",
	"pfm",
	"hsmp",
	"trace",
	"div",
	"mac",
	"l2c",
	"dx_regs",
	"v2",
	NULL,
};

#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
#define WRITE_METHOD "write through"
#else
#define WRITE_METHOD "write back"
#endif

struct cache_info L1_cache_info[2];
static void __init dump_cpu_info(int cpu)
{
	int i, p = 0;
	char str[sizeof(hwcap_str) + 16];

	for (i = 0; hwcap_str[i]; i++) {
		if (elf_hwcap & (1 << i)) {
			sprintf(str + p, "%s ", hwcap_str[i]);
			p += strlen(hwcap_str[i]) + 1;
		}
	}

	pr_info("CPU%d Features: %s\n", cpu, str);

	L1_cache_info[ICACHE].ways = CACHE_WAY(ICACHE);
	L1_cache_info[ICACHE].line_size = CACHE_LINE_SIZE(ICACHE);
	L1_cache_info[ICACHE].sets = CACHE_SET(ICACHE);
	L1_cache_info[ICACHE].size =
	    L1_cache_info[ICACHE].ways * L1_cache_info[ICACHE].line_size *
	    L1_cache_info[ICACHE].sets / 1024;
	pr_info("L1I:%dKB/%dS/%dW/%dB\n", L1_cache_info[ICACHE].size,
		L1_cache_info[ICACHE].sets, L1_cache_info[ICACHE].ways,
		L1_cache_info[ICACHE].line_size);
	L1_cache_info[DCACHE].ways = CACHE_WAY(DCACHE);
	L1_cache_info[DCACHE].line_size = CACHE_LINE_SIZE(DCACHE);
	L1_cache_info[DCACHE].sets = CACHE_SET(DCACHE);
	L1_cache_info[DCACHE].size =
	    L1_cache_info[DCACHE].ways * L1_cache_info[DCACHE].line_size *
	    L1_cache_info[DCACHE].sets / 1024;
	pr_info("L1D:%dKB/%dS/%dW/%dB\n", L1_cache_info[DCACHE].size,
		L1_cache_info[DCACHE].sets, L1_cache_info[DCACHE].ways,
		L1_cache_info[DCACHE].line_size);
	pr_info("L1 D-Cache is %s\n", WRITE_METHOD);
	if (L1_cache_info[DCACHE].size != L1_CACHE_BYTES)
		pr_crit
		    ("The cache line size(%d) of this processor is not the same as L1_CACHE_BYTES(%d).\n",
		     L1_cache_info[DCACHE].size, L1_CACHE_BYTES);
#ifdef CONFIG_CPU_CACHE_ALIASING
	{
		int aliasing_num;
		aliasing_num =
		    L1_cache_info[ICACHE].size * 1024 / PAGE_SIZE /
		    L1_cache_info[ICACHE].ways;
		L1_cache_info[ICACHE].aliasing_num = aliasing_num;
		L1_cache_info[ICACHE].aliasing_mask =
		    (aliasing_num - 1) << PAGE_SHIFT;
		aliasing_num =
		    L1_cache_info[DCACHE].size * 1024 / PAGE_SIZE /
		    L1_cache_info[DCACHE].ways;
		L1_cache_info[DCACHE].aliasing_num = aliasing_num;
		L1_cache_info[DCACHE].aliasing_mask =
		    (aliasing_num - 1) << PAGE_SHIFT;
	}
#endif
}

static void __init setup_cpuinfo(void)
{
	unsigned long tmp = 0, cpu_name;

	cpu_dcache_inval_all();
	cpu_icache_inval_all();
	__nds32__isb();

	cpu_id = (__nds32__mfsr(NDS32_SR_CPU_VER) & CPU_VER_mskCPUID) >> CPU_VER_offCPUID;
	cpu_name = ((cpu_id) & 0xf0) >> 4;
	cpu_series = cpu_name ? cpu_name - 10 + 'A' : 'N';
	cpu_id = cpu_id & 0xf;
	cpu_rev = (__nds32__mfsr(NDS32_SR_CPU_VER) & CPU_VER_mskREV) >> CPU_VER_offREV;
	cpu_cfgid = (__nds32__mfsr(NDS32_SR_CPU_VER) & CPU_VER_mskCFGID) >> CPU_VER_offCFGID;

	pr_info("CPU:%c%ld, CPU_VER 0x%08x(id %lu, rev %lu, cfg %lu)\n",
		cpu_series, cpu_id, __nds32__mfsr(NDS32_SR_CPU_VER), cpu_id, cpu_rev, cpu_cfgid);

	elf_hwcap |= HWCAP_MFUSR_PC;

	if (((__nds32__mfsr(NDS32_SR_MSC_CFG) & MSC_CFG_mskBASEV) >> MSC_CFG_offBASEV) == 0) {
		if (__nds32__mfsr(NDS32_SR_MSC_CFG) & MSC_CFG_mskDIV)
			elf_hwcap |= HWCAP_DIV;

		if ((__nds32__mfsr(NDS32_SR_MSC_CFG) & MSC_CFG_mskMAC)
		    || (cpu_id == 12 && cpu_rev < 4))
			elf_hwcap |= HWCAP_MAC;
	} else {
		elf_hwcap |= HWCAP_V2;
		elf_hwcap |= HWCAP_DIV;
		elf_hwcap |= HWCAP_MAC;
	}

	if (cpu_cfgid & 0x0001)
		elf_hwcap |= HWCAP_EXT;

	if (cpu_cfgid & 0x0002)
		elf_hwcap |= HWCAP_BASE16;

	if (cpu_cfgid & 0x0004)
		elf_hwcap |= HWCAP_EXT2;

	if (cpu_cfgid & 0x0008)
		elf_hwcap |= HWCAP_FPU;

	if (cpu_cfgid & 0x0010)
		elf_hwcap |= HWCAP_STRING;

	if (__nds32__mfsr(NDS32_SR_MMU_CFG) & MMU_CFG_mskDE)
		endianness = "MSB";
	else
		endianness = "LSB";

	if (__nds32__mfsr(NDS32_SR_MSC_CFG) & MSC_CFG_mskEDM)
		elf_hwcap |= HWCAP_EDM;

	if (__nds32__mfsr(NDS32_SR_MSC_CFG) & MSC_CFG_mskLMDMA)
		elf_hwcap |= HWCAP_LMDMA;

	if (__nds32__mfsr(NDS32_SR_MSC_CFG) & MSC_CFG_mskPFM)
		elf_hwcap |= HWCAP_PFM;

	if (__nds32__mfsr(NDS32_SR_MSC_CFG) & MSC_CFG_mskHSMP)
		elf_hwcap |= HWCAP_HSMP;

	if (__nds32__mfsr(NDS32_SR_MSC_CFG) & MSC_CFG_mskTRACE)
		elf_hwcap |= HWCAP_TRACE;

	if (__nds32__mfsr(NDS32_SR_MSC_CFG) & MSC_CFG_mskAUDIO)
		elf_hwcap |= HWCAP_AUDIO;

	if (__nds32__mfsr(NDS32_SR_MSC_CFG) & MSC_CFG_mskL2C)
		elf_hwcap |= HWCAP_L2C;

	tmp = __nds32__mfsr(NDS32_SR_CACHE_CTL);
	if (!IS_ENABLED(CONFIG_CPU_DCACHE_DISABLE))
		tmp |= CACHE_CTL_mskDC_EN;

	if (!IS_ENABLED(CONFIG_CPU_ICACHE_DISABLE))
		tmp |= CACHE_CTL_mskIC_EN;
	__nds32__mtsr_isb(tmp, NDS32_SR_CACHE_CTL);

	dump_cpu_info(smp_processor_id());
}

static void __init setup_memory(void)
{
	unsigned long ram_start_pfn;
	unsigned long free_ram_start_pfn;
	phys_addr_t memory_start, memory_end;
	struct memblock_region *region;

	memory_end = memory_start = 0;

	/* Find main memory where is the kernel */
	for_each_memblock(memory, region) {
		memory_start = region->base;
		memory_end = region->base + region->size;
		pr_info("%s: Memory: 0x%x-0x%x\n", __func__,
			memory_start, memory_end);
	}

	if (!memory_end) {
		panic("No memory!");
	}

	ram_start_pfn = PFN_UP(memblock_start_of_DRAM());
	/* free_ram_start_pfn is first page after kernel */
	free_ram_start_pfn = PFN_UP(__pa(&_end));
	max_pfn = PFN_DOWN(memblock_end_of_DRAM());
	/* it could update max_pfn */
	if (max_pfn - ram_start_pfn <= MAXMEM_PFN)
		max_low_pfn = max_pfn;
	else {
		max_low_pfn = MAXMEM_PFN + ram_start_pfn;
		if (!IS_ENABLED(CONFIG_HIGHMEM))
			max_pfn = MAXMEM_PFN + ram_start_pfn;
	}
	/* high_memory is related with VMALLOC */
	high_memory = (void *)__va(max_low_pfn * PAGE_SIZE);
	min_low_pfn = free_ram_start_pfn;

	/*
	 * initialize the boot-time allocator (with low memory only).
	 *
	 * This makes the memory from the end of the kernel to the end of
	 * RAM usable.
	 */
	memblock_set_bottom_up(true);
	memblock_reserve(PFN_PHYS(ram_start_pfn), PFN_PHYS(free_ram_start_pfn - ram_start_pfn));

	early_init_fdt_reserve_self();
	early_init_fdt_scan_reserved_mem();

	memblock_dump_all();
}

void __init setup_arch(char **cmdline_p)
{
	early_init_devtree(__atags_pointer ? \
		phys_to_virt(__atags_pointer) : __dtb_start);

	setup_cpuinfo();

	init_mm.start_code = (unsigned long)&_stext;
	init_mm.end_code = (unsigned long)&_etext;
	init_mm.end_data = (unsigned long)&_edata;
	init_mm.brk = (unsigned long)&_end;

	/* setup bootmem allocator */
	setup_memory();

	/* paging_init() sets up the MMU and marks all pages as reserved */
	paging_init();

	/* invalidate all TLB entries because the new mapping is created */
	__nds32__tlbop_flua();

	/* use generic way to parse */
	parse_early_param();

	unflatten_and_copy_device_tree();

	if(IS_ENABLED(CONFIG_VT)) {
		if(IS_ENABLED(CONFIG_DUMMY_CONSOLE))
			conswitchp = &dummy_con;
	}

	*cmdline_p = boot_command_line;
	early_trap_init();
}

static int c_show(struct seq_file *m, void *v)
{
	int i;

	seq_printf(m, "Processor\t: %c%ld (id %lu, rev %lu, cfg %lu)\n",
		   cpu_series, cpu_id, cpu_id, cpu_rev, cpu_cfgid);

	seq_printf(m, "L1I\t\t: %luKB/%luS/%luW/%luB\n",
		   CACHE_SET(ICACHE) * CACHE_WAY(ICACHE) *
		   CACHE_LINE_SIZE(ICACHE) / 1024, CACHE_SET(ICACHE),
		   CACHE_WAY(ICACHE), CACHE_LINE_SIZE(ICACHE));

	seq_printf(m, "L1D\t\t: %luKB/%luS/%luW/%luB\n",
		   CACHE_SET(DCACHE) * CACHE_WAY(DCACHE) *
		   CACHE_LINE_SIZE(DCACHE) / 1024, CACHE_SET(DCACHE),
		   CACHE_WAY(DCACHE), CACHE_LINE_SIZE(DCACHE));

	seq_printf(m, "BogoMIPS\t: %lu.%02lu\n",
		   loops_per_jiffy / (500000 / HZ),
		   (loops_per_jiffy / (5000 / HZ)) % 100);

	/* dump out the processor features */
	seq_puts(m, "Features\t: ");

	for (i = 0; hwcap_str[i]; i++)
		if (elf_hwcap & (1 << i))
			seq_printf(m, "%s ", hwcap_str[i]);

	seq_puts(m, "\n\n");

	return 0;
}

static void *c_start(struct seq_file *m, loff_t * pos)
{
	return *pos < 1 ? (void *)1 : NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t * pos)
{
	++*pos;
	return NULL;
}

static void c_stop(struct seq_file *m, void *v)
{
}

struct seq_operations cpuinfo_op = {
	.start = c_start,
	.next = c_next,
	.stop = c_stop,
	.show = c_show
};

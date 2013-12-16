/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2006, 2009, 2010, 2011 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/dma-mapping.h>
#include <linux/memblock.h>
#include <linux/seq_file.h>
#include <linux/bootmem.h>
#include <linux/clkdev.h>
#include <linux/initrd.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_fdt.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/cache.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/fs.h>
#include <linux/of.h>


#include <asm/sections.h>
#include <asm/div64.h>
#include <asm/setup.h>
#include <asm/dscr.h>
#include <asm/clock.h>
#include <asm/soc.h>
#include <asm/special_insns.h>

static const char *c6x_soc_name;

int c6x_num_cores;
EXPORT_SYMBOL_GPL(c6x_num_cores);

unsigned int c6x_silicon_rev;
EXPORT_SYMBOL_GPL(c6x_silicon_rev);

/*
 * Device status register. This holds information
 * about device configuration needed by some drivers.
 */
unsigned int c6x_devstat;
EXPORT_SYMBOL_GPL(c6x_devstat);

/*
 * Some SoCs have fuse registers holding a unique MAC
 * address. This is parsed out of the device tree with
 * the resulting MAC being held here.
 */
unsigned char c6x_fuse_mac[6];

unsigned long memory_start;
unsigned long memory_end;

unsigned long ram_start;
unsigned long ram_end;

/* Uncached memory for DMA consistent use (memdma=) */
static unsigned long dma_start __initdata;
static unsigned long dma_size __initdata;

struct cpuinfo_c6x {
	const char *cpu_name;
	const char *cpu_voltage;
	const char *mmu;
	const char *fpu;
	char *cpu_rev;
	unsigned int core_id;
	char __cpu_rev[5];
};

static DEFINE_PER_CPU(struct cpuinfo_c6x, cpu_data);

unsigned int ticks_per_ns_scaled;
EXPORT_SYMBOL(ticks_per_ns_scaled);

unsigned int c6x_core_freq;

static void __init get_cpuinfo(void)
{
	unsigned cpu_id, rev_id, csr;
	struct clk *coreclk = clk_get_sys(NULL, "core");
	unsigned long core_khz;
	u64 tmp;
	struct cpuinfo_c6x *p;
	struct device_node *node, *np;

	p = &per_cpu(cpu_data, smp_processor_id());

	if (!IS_ERR(coreclk))
		c6x_core_freq = clk_get_rate(coreclk);
	else {
		printk(KERN_WARNING
		       "Cannot find core clock frequency. Using 700MHz\n");
		c6x_core_freq = 700000000;
	}

	core_khz = c6x_core_freq / 1000;

	tmp = (uint64_t)core_khz << C6X_NDELAY_SCALE;
	do_div(tmp, 1000000);
	ticks_per_ns_scaled = tmp;

	csr = get_creg(CSR);
	cpu_id = csr >> 24;
	rev_id = (csr >> 16) & 0xff;

	p->mmu = "none";
	p->fpu = "none";
	p->cpu_voltage = "unknown";

	switch (cpu_id) {
	case 0:
		p->cpu_name = "C67x";
		p->fpu = "yes";
		break;
	case 2:
		p->cpu_name = "C62x";
		break;
	case 8:
		p->cpu_name = "C64x";
		break;
	case 12:
		p->cpu_name = "C64x";
		break;
	case 16:
		p->cpu_name = "C64x+";
		p->cpu_voltage = "1.2";
		break;
	case 21:
		p->cpu_name = "C66X";
		p->cpu_voltage = "1.2";
		break;
	default:
		p->cpu_name = "unknown";
		break;
	}

	if (cpu_id < 16) {
		switch (rev_id) {
		case 0x1:
			if (cpu_id > 8) {
				p->cpu_rev = "DM640/DM641/DM642/DM643";
				p->cpu_voltage = "1.2 - 1.4";
			} else {
				p->cpu_rev = "C6201";
				p->cpu_voltage = "2.5";
			}
			break;
		case 0x2:
			p->cpu_rev = "C6201B/C6202/C6211";
			p->cpu_voltage = "1.8";
			break;
		case 0x3:
			p->cpu_rev = "C6202B/C6203/C6204/C6205";
			p->cpu_voltage = "1.5";
			break;
		case 0x201:
			p->cpu_rev = "C6701 revision 0 (early CPU)";
			p->cpu_voltage = "1.8";
			break;
		case 0x202:
			p->cpu_rev = "C6701/C6711/C6712";
			p->cpu_voltage = "1.8";
			break;
		case 0x801:
			p->cpu_rev = "C64x";
			p->cpu_voltage = "1.5";
			break;
		default:
			p->cpu_rev = "unknown";
		}
	} else {
		p->cpu_rev = p->__cpu_rev;
		snprintf(p->__cpu_rev, sizeof(p->__cpu_rev), "0x%x", cpu_id);
	}

	p->core_id = get_coreid();

	node = of_find_node_by_name(NULL, "cpus");
	if (node) {
		for_each_child_of_node(node, np)
			if (!strcmp("cpu", np->name))
				++c6x_num_cores;
		of_node_put(node);
	}

	node = of_find_node_by_name(NULL, "soc");
	if (node) {
		if (of_property_read_string(node, "model", &c6x_soc_name))
			c6x_soc_name = "unknown";
		of_node_put(node);
	} else
		c6x_soc_name = "unknown";

	printk(KERN_INFO "CPU%d: %s rev %s, %s volts, %uMHz\n",
	       p->core_id, p->cpu_name, p->cpu_rev,
	       p->cpu_voltage, c6x_core_freq / 1000000);
}

/*
 * Early parsing of the command line
 */
static u32 mem_size __initdata;

/* "mem=" parsing. */
static int __init early_mem(char *p)
{
	if (!p)
		return -EINVAL;

	mem_size = memparse(p, &p);
	/* don't remove all of memory when handling "mem={invalid}" */
	if (mem_size == 0)
		return -EINVAL;

	return 0;
}
early_param("mem", early_mem);

/* "memdma=<size>[@<address>]" parsing. */
static int __init early_memdma(char *p)
{
	if (!p)
		return -EINVAL;

	dma_size = memparse(p, &p);
	if (*p == '@')
		dma_start = memparse(p, &p);

	return 0;
}
early_param("memdma", early_memdma);

int __init c6x_add_memory(phys_addr_t start, unsigned long size)
{
	static int ram_found __initdata;

	/* We only handle one bank (the one with PAGE_OFFSET) for now */
	if (ram_found)
		return -EINVAL;

	if (start > PAGE_OFFSET || PAGE_OFFSET >= (start + size))
		return 0;

	ram_start = start;
	ram_end = start + size;

	ram_found = 1;
	return 0;
}

/*
 * Do early machine setup and device tree parsing. This is called very
 * early on the boot process.
 */
notrace void __init machine_init(unsigned long dt_ptr)
{
	struct boot_param_header *dtb = __va(dt_ptr);
	struct boot_param_header *fdt = (struct boot_param_header *)_fdt_start;

	/* interrupts must be masked */
	set_creg(IER, 2);

	/*
	 * Set the Interrupt Service Table (IST) to the beginning of the
	 * vector table.
	 */
	set_ist(_vectors_start);

	lockdep_init();

	/*
	 * dtb is passed in from bootloader.
	 * fdt is linked in blob.
	 */
	if (dtb && dtb != fdt)
		fdt = dtb;

	/* Do some early initialization based on the flat device tree */
	early_init_dt_scan(fdt);

	parse_early_param();
}

void __init setup_arch(char **cmdline_p)
{
	int bootmap_size;
	struct memblock_region *reg;

	printk(KERN_INFO "Initializing kernel\n");

	/* Initialize command line */
	*cmdline_p = boot_command_line;

	memory_end = ram_end;
	memory_end &= ~(PAGE_SIZE - 1);

	if (mem_size && (PAGE_OFFSET + PAGE_ALIGN(mem_size)) < memory_end)
		memory_end = PAGE_OFFSET + PAGE_ALIGN(mem_size);

	/* add block that this kernel can use */
	memblock_add(PAGE_OFFSET, memory_end - PAGE_OFFSET);

	/* reserve kernel text/data/bss */
	memblock_reserve(PAGE_OFFSET,
			 PAGE_ALIGN((unsigned long)&_end - PAGE_OFFSET));

	if (dma_size) {
		/* align to cacheability granularity */
		dma_size = CACHE_REGION_END(dma_size);

		if (!dma_start)
			dma_start = memory_end - dma_size;

		/* align to cacheability granularity */
		dma_start = CACHE_REGION_START(dma_start);

		/* reserve DMA memory taken from kernel memory */
		if (memblock_is_region_memory(dma_start, dma_size))
			memblock_reserve(dma_start, dma_size);
	}

	memory_start = PAGE_ALIGN((unsigned int) &_end);

	printk(KERN_INFO "Memory Start=%08lx, Memory End=%08lx\n",
	       memory_start, memory_end);

#ifdef CONFIG_BLK_DEV_INITRD
	/*
	 * Reserve initrd memory if in kernel memory.
	 */
	if (initrd_start < initrd_end)
		if (memblock_is_region_memory(initrd_start,
					      initrd_end - initrd_start))
			memblock_reserve(initrd_start,
					 initrd_end - initrd_start);
#endif

	init_mm.start_code = (unsigned long) &_stext;
	init_mm.end_code   = (unsigned long) &_etext;
	init_mm.end_data   = memory_start;
	init_mm.brk        = memory_start;

	/*
	 * Give all the memory to the bootmap allocator,  tell it to put the
	 * boot mem_map at the start of memory
	 */
	bootmap_size = init_bootmem_node(NODE_DATA(0),
					 memory_start >> PAGE_SHIFT,
					 PAGE_OFFSET >> PAGE_SHIFT,
					 memory_end >> PAGE_SHIFT);
	memblock_reserve(memory_start, bootmap_size);

	unflatten_device_tree();

	c6x_cache_init();

	/* Set the whole external memory as non-cacheable */
	disable_caching(ram_start, ram_end - 1);

	/* Set caching of external RAM used by Linux */
	for_each_memblock(memory, reg)
		enable_caching(CACHE_REGION_START(reg->base),
			       CACHE_REGION_START(reg->base + reg->size - 1));

#ifdef CONFIG_BLK_DEV_INITRD
	/*
	 * Enable caching for initrd which falls outside kernel memory.
	 */
	if (initrd_start < initrd_end) {
		if (!memblock_is_region_memory(initrd_start,
					       initrd_end - initrd_start))
			enable_caching(CACHE_REGION_START(initrd_start),
				       CACHE_REGION_START(initrd_end - 1));
	}
#endif

	/*
	 * Disable caching for dma coherent memory taken from kernel memory.
	 */
	if (dma_size && memblock_is_region_memory(dma_start, dma_size))
		disable_caching(dma_start,
				CACHE_REGION_START(dma_start + dma_size - 1));

	/* Initialize the coherent memory allocator */
	coherent_mem_init(dma_start, dma_size);

	/*
	 * Free all memory as a starting point.
	 */
	free_bootmem(PAGE_OFFSET, memory_end - PAGE_OFFSET);

	/*
	 * Then reserve memory which is already being used.
	 */
	for_each_memblock(reserved, reg) {
		pr_debug("reserved - 0x%08x-0x%08x\n",
			 (u32) reg->base, (u32) reg->size);
		reserve_bootmem(reg->base, reg->size, BOOTMEM_DEFAULT);
	}

	max_low_pfn = PFN_DOWN(memory_end);
	min_low_pfn = PFN_UP(memory_start);
	max_mapnr = max_low_pfn - min_low_pfn;

	/* Get kmalloc into gear */
	paging_init();

	/*
	 * Probe for Device State Configuration Registers.
	 * We have to do this early in case timer needs to be enabled
	 * through DSCR.
	 */
	dscr_probe();

	/* We do this early for timer and core clock frequency */
	c64x_setup_clocks();

	/* Get CPU info */
	get_cpuinfo();

#if defined(CONFIG_VT) && defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
}

#define cpu_to_ptr(n) ((void *)((long)(n)+1))
#define ptr_to_cpu(p) ((long)(p) - 1)

static int show_cpuinfo(struct seq_file *m, void *v)
{
	int n = ptr_to_cpu(v);
	struct cpuinfo_c6x *p = &per_cpu(cpu_data, n);

	if (n == 0) {
		seq_printf(m,
			   "soc\t\t: %s\n"
			   "soc revision\t: 0x%x\n"
			   "soc cores\t: %d\n",
			   c6x_soc_name, c6x_silicon_rev, c6x_num_cores);
	}

	seq_printf(m,
		   "\n"
		   "processor\t: %d\n"
		   "cpu\t\t: %s\n"
		   "core revision\t: %s\n"
		   "core voltage\t: %s\n"
		   "core id\t\t: %d\n"
		   "mmu\t\t: %s\n"
		   "fpu\t\t: %s\n"
		   "cpu MHz\t\t: %u\n"
		   "bogomips\t: %lu.%02lu\n\n",
		   n,
		   p->cpu_name, p->cpu_rev, p->cpu_voltage,
		   p->core_id, p->mmu, p->fpu,
		   (c6x_core_freq + 500000) / 1000000,
		   (loops_per_jiffy/(500000/HZ)),
		   (loops_per_jiffy/(5000/HZ))%100);

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < nr_cpu_ids ? cpu_to_ptr(*pos) : NULL;
}
static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return NULL;
}
static void c_stop(struct seq_file *m, void *v)
{
}

const struct seq_operations cpuinfo_op = {
	c_start,
	c_stop,
	c_next,
	show_cpuinfo
};

static struct cpu cpu_devices[NR_CPUS];

static int __init topology_init(void)
{
	int i;

	for_each_present_cpu(i)
		register_cpu(&cpu_devices[i], i);

	return 0;
}

subsys_initcall(topology_init);

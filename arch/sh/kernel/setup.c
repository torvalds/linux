/*
 * arch/sh/kernel/setup.c
 *
 * This file handles the architecture-dependent parts of initialization
 *
 *  Copyright (C) 1999  Niibe Yutaka
 *  Copyright (C) 2002 - 2010 Paul Mundt
 */
#include <linux/screen_info.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/bootmem.h>
#include <linux/console.h>
#include <linux/root_dev.h>
#include <linux/utsname.h>
#include <linux/nodemask.h>
#include <linux/cpu.h>
#include <linux/pfn.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/kexec.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/err.h>
#include <linux/crash_dump.h>
#include <linux/mmzone.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/memblock.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/elf.h>
#include <asm/sections.h>
#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/clock.h>
#include <asm/smp.h>
#include <asm/mmu_context.h>
#include <asm/mmzone.h>
#include <asm/sparsemem.h>

/*
 * Initialize loops_per_jiffy as 10000000 (1000MIPS).
 * This value will be used at the very early stage of serial setup.
 * The bigger value means no problem.
 */
struct sh_cpuinfo cpu_data[NR_CPUS] __read_mostly = {
	[0] = {
		.type			= CPU_SH_NONE,
		.family			= CPU_FAMILY_UNKNOWN,
		.loops_per_jiffy	= 10000000,
		.phys_bits		= MAX_PHYSMEM_BITS,
	},
};
EXPORT_SYMBOL(cpu_data);

/*
 * The machine vector. First entry in .machvec.init, or clobbered by
 * sh_mv= on the command line, prior to .machvec.init teardown.
 */
struct sh_machine_vector sh_mv = { .mv_name = "generic", };
EXPORT_SYMBOL(sh_mv);

#ifdef CONFIG_VT
struct screen_info screen_info;
#endif

extern int root_mountflags;

#define RAMDISK_IMAGE_START_MASK	0x07FF
#define RAMDISK_PROMPT_FLAG		0x8000
#define RAMDISK_LOAD_FLAG		0x4000

static char __initdata command_line[COMMAND_LINE_SIZE] = { 0, };

static struct resource code_resource = {
	.name = "Kernel code",
	.flags = IORESOURCE_BUSY | IORESOURCE_MEM,
};

static struct resource data_resource = {
	.name = "Kernel data",
	.flags = IORESOURCE_BUSY | IORESOURCE_MEM,
};

static struct resource bss_resource = {
	.name	= "Kernel bss",
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM,
};

unsigned long memory_start;
EXPORT_SYMBOL(memory_start);
unsigned long memory_end = 0;
EXPORT_SYMBOL(memory_end);
unsigned long memory_limit = 0;

static struct resource mem_resources[MAX_NUMNODES];

int l1i_cache_shape, l1d_cache_shape, l2_cache_shape;

static int __init early_parse_mem(char *p)
{
	if (!p)
		return 1;

	memory_limit = PAGE_ALIGN(memparse(p, &p));

	pr_notice("Memory limited to %ldMB\n", memory_limit >> 20);

	return 0;
}
early_param("mem", early_parse_mem);

void __init check_for_initrd(void)
{
#ifdef CONFIG_BLK_DEV_INITRD
	unsigned long start, end;

	/*
	 * Check for the rare cases where boot loaders adhere to the boot
	 * ABI.
	 */
	if (!LOADER_TYPE || !INITRD_START || !INITRD_SIZE)
		goto disable;

	start = INITRD_START + __MEMORY_START;
	end = start + INITRD_SIZE;

	if (unlikely(end <= start))
		goto disable;
	if (unlikely(start & ~PAGE_MASK)) {
		pr_err("initrd must be page aligned\n");
		goto disable;
	}

	if (unlikely(start < __MEMORY_START)) {
		pr_err("initrd start (%08lx) < __MEMORY_START(%x)\n",
			start, __MEMORY_START);
		goto disable;
	}

	if (unlikely(end > memblock_end_of_DRAM())) {
		pr_err("initrd extends beyond end of memory "
		       "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
		       end, (unsigned long)memblock_end_of_DRAM());
		goto disable;
	}

	/*
	 * If we got this far in spite of the boot loader's best efforts
	 * to the contrary, assume we actually have a valid initrd and
	 * fix up the root dev.
	 */
	ROOT_DEV = Root_RAM0;

	/*
	 * Address sanitization
	 */
	initrd_start = (unsigned long)__va(start);
	initrd_end = initrd_start + INITRD_SIZE;

	memblock_reserve(__pa(initrd_start), INITRD_SIZE);

	return;

disable:
	pr_info("initrd disabled\n");
	initrd_start = initrd_end = 0;
#endif
}

void __cpuinit calibrate_delay(void)
{
	struct clk *clk = clk_get(NULL, "cpu_clk");

	if (IS_ERR(clk))
		panic("Need a sane CPU clock definition!");

	loops_per_jiffy = (clk_get_rate(clk) >> 1) / HZ;

	printk(KERN_INFO "Calibrating delay loop (skipped)... "
			 "%lu.%02lu BogoMIPS PRESET (lpj=%lu)\n",
			 loops_per_jiffy/(500000/HZ),
			 (loops_per_jiffy/(5000/HZ)) % 100,
			 loops_per_jiffy);
}

void __init __add_active_range(unsigned int nid, unsigned long start_pfn,
						unsigned long end_pfn)
{
	struct resource *res = &mem_resources[nid];
	unsigned long start, end;

	WARN_ON(res->name); /* max one active range per node for now */

	start = start_pfn << PAGE_SHIFT;
	end = end_pfn << PAGE_SHIFT;

	res->name = "System RAM";
	res->start = start;
	res->end = end - 1;
	res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;

	if (request_resource(&iomem_resource, res)) {
		pr_err("unable to request memory_resource 0x%lx 0x%lx\n",
		       start_pfn, end_pfn);
		return;
	}

	/*
	 * We don't know which RAM region contains kernel data or
	 * the reserved crashkernel region, so try it repeatedly
	 * and let the resource manager test it.
	 */
	request_resource(res, &code_resource);
	request_resource(res, &data_resource);
	request_resource(res, &bss_resource);
#ifdef CONFIG_KEXEC
	request_resource(res, &crashk_res);
#endif

	/*
	 * Also make sure that there is a PMB mapping that covers this
	 * range before we attempt to activate it, to avoid reset by MMU.
	 * We can hit this path with NUMA or memory hot-add.
	 */
	pmb_bolt_mapping((unsigned long)__va(start), start, end - start,
			 PAGE_KERNEL);

	add_active_range(nid, start_pfn, end_pfn);
}

void __init __weak plat_early_device_setup(void)
{
}

void __init setup_arch(char **cmdline_p)
{
	enable_mmu();

	ROOT_DEV = old_decode_dev(ORIG_ROOT_DEV);

	printk(KERN_NOTICE "Boot params:\n"
			   "... MOUNT_ROOT_RDONLY - %08lx\n"
			   "... RAMDISK_FLAGS     - %08lx\n"
			   "... ORIG_ROOT_DEV     - %08lx\n"
			   "... LOADER_TYPE       - %08lx\n"
			   "... INITRD_START      - %08lx\n"
			   "... INITRD_SIZE       - %08lx\n",
			   MOUNT_ROOT_RDONLY, RAMDISK_FLAGS,
			   ORIG_ROOT_DEV, LOADER_TYPE,
			   INITRD_START, INITRD_SIZE);

#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = RAMDISK_FLAGS & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((RAMDISK_FLAGS & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((RAMDISK_FLAGS & RAMDISK_LOAD_FLAG) != 0);
#endif

	if (!MOUNT_ROOT_RDONLY)
		root_mountflags &= ~MS_RDONLY;
	init_mm.start_code = (unsigned long) _text;
	init_mm.end_code = (unsigned long) _etext;
	init_mm.end_data = (unsigned long) _edata;
	init_mm.brk = (unsigned long) _end;

	code_resource.start = virt_to_phys(_text);
	code_resource.end = virt_to_phys(_etext)-1;
	data_resource.start = virt_to_phys(_etext);
	data_resource.end = virt_to_phys(_edata)-1;
	bss_resource.start = virt_to_phys(__bss_start);
	bss_resource.end = virt_to_phys(_ebss)-1;

#ifdef CONFIG_CMDLINE_OVERWRITE
	strlcpy(command_line, CONFIG_CMDLINE, sizeof(command_line));
#else
	strlcpy(command_line, COMMAND_LINE, sizeof(command_line));
#ifdef CONFIG_CMDLINE_EXTEND
	strlcat(command_line, " ", sizeof(command_line));
	strlcat(command_line, CONFIG_CMDLINE, sizeof(command_line));
#endif
#endif

	/* Save unparsed command line copy for /proc/cmdline */
	memcpy(boot_command_line, command_line, COMMAND_LINE_SIZE);
	*cmdline_p = command_line;

	parse_early_param();

	plat_early_device_setup();

	sh_mv_setup();

	/* Let earlyprintk output early console messages */
	early_platform_driver_probe("earlyprintk", 1, 1);

	paging_init();

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	/* Perform the machine specific initialisation */
	if (likely(sh_mv.mv_setup))
		sh_mv.mv_setup(cmdline_p);

	plat_smp_setup();
}

/* processor boot mode configuration */
int generic_mode_pins(void)
{
	pr_warning("generic_mode_pins(): missing mode pin configuration\n");
	return 0;
}

int test_mode_pin(int pin)
{
	return sh_mv.mv_mode_pins() & pin;
}

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *    Initial setup-routines for HP 9000 based hardware.
 *
 *    Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *    Modifications for PA-RISC (C) 1999 Helge Deller <deller@gmx.de>
 *    Modifications copyright 1999 SuSE GmbH (Philipp Rumpf)
 *    Modifications copyright 2000 Martin K. Petersen <mkp@mkp.net>
 *    Modifications copyright 2000 Philipp Rumpf <prumpf@tux.org>
 *    Modifications copyright 2001 Ryan Bradetich <rbradetich@uswest.net>
 *
 *    Initial PA-RISC Version: 04-23-1999 by Helge Deller
 */

#include <linux/kernel.h>
#include <linux/initrd.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/seq_file.h>
#define PCI_DEBUG
#include <linux/pci.h>
#undef PCI_DEBUG
#include <linux/proc_fs.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/start_kernel.h>

#include <asm/cacheflush.h>
#include <asm/processor.h>
#include <asm/sections.h>
#include <asm/pdc.h>
#include <asm/led.h>
#include <asm/pdc_chassis.h>
#include <asm/io.h>
#include <asm/setup.h>
#include <asm/unwind.h>
#include <asm/smp.h>

static char __initdata command_line[COMMAND_LINE_SIZE];

static void __init setup_cmdline(char **cmdline_p)
{
	extern unsigned int boot_args[];
	char *p;

	*cmdline_p = command_line;

	/* boot_args[0] is free-mem start, boot_args[1] is ptr to command line */
	if (boot_args[0] < 64)
		return;	/* return if called from hpux boot loader */

	/* Collect stuff passed in from the boot loader */
	strscpy(boot_command_line, (char *)__va(boot_args[1]),
		COMMAND_LINE_SIZE);

	/* autodetect console type (if not done by palo yet) */
	p = boot_command_line;
	if (!str_has_prefix(p, "console=") && !strstr(p, " console=")) {
		strlcat(p, " console=", COMMAND_LINE_SIZE);
		if (PAGE0->mem_cons.cl_class == CL_DUPLEX)
			strlcat(p, "ttyS0", COMMAND_LINE_SIZE);
		else
			strlcat(p, "tty0", COMMAND_LINE_SIZE);
	}

	/* default to use early console */
	if (!strstr(p, "earlycon"))
		strlcat(p, " earlycon=pdc", COMMAND_LINE_SIZE);

#ifdef CONFIG_BLK_DEV_INITRD
	/* did palo pass us a ramdisk? */
	if (boot_args[2] != 0) {
		initrd_start = (unsigned long)__va(boot_args[2]);
		initrd_end = (unsigned long)__va(boot_args[3]);
	}
#endif

	strscpy(command_line, boot_command_line, COMMAND_LINE_SIZE);
}

#ifdef CONFIG_PA11
static void __init dma_ops_init(void)
{
	switch (boot_cpu_data.cpu_type) {
	case pcx:
		/*
		 * We've got way too many dependencies on 1.1 semantics
		 * to support 1.0 boxes at this point.
		 */
		panic(	"PA-RISC Linux currently only supports machines that conform to\n"
			"the PA-RISC 1.1 or 2.0 architecture specification.\n");

	case pcxl2:
	default:
		break;
	}
}
#endif

void __init setup_arch(char **cmdline_p)
{
#ifdef CONFIG_64BIT
	extern int parisc_narrow_firmware;
#endif
	unwind_init();

	init_per_cpu(smp_processor_id());	/* Set Modes & Enable FP */

#ifdef CONFIG_64BIT
	printk(KERN_INFO "The 64-bit Kernel has started...\n");
#else
	printk(KERN_INFO "The 32-bit Kernel has started...\n");
#endif

	printk(KERN_INFO "Kernel default page size is %d KB. Huge pages ",
		(int)(PAGE_SIZE / 1024));
#ifdef CONFIG_HUGETLB_PAGE
	printk(KERN_CONT "enabled with %d MB physical and %d MB virtual size",
		 1 << (REAL_HPAGE_SHIFT - 20), 1 << (HPAGE_SHIFT - 20));
#else
	printk(KERN_CONT "disabled");
#endif
	printk(KERN_CONT ".\n");

	/*
	 * Check if initial kernel page mappings are sufficient.
	 * panic early if not, else we may access kernel functions
	 * and variables which can't be reached.
	 */
	if (__pa((unsigned long) &_end) >= KERNEL_INITIAL_SIZE)
		panic("KERNEL_INITIAL_ORDER too small!");

#ifdef CONFIG_64BIT
	if(parisc_narrow_firmware) {
		printk(KERN_INFO "Kernel is using PDC in 32-bit mode.\n");
	}
#endif
	setup_pdc();
	setup_cmdline(cmdline_p);
	collect_boot_cpu_data();
	do_memory_inventory();  /* probe for physical memory */
	parisc_cache_init();
	paging_init();

#ifdef CONFIG_PA11
	dma_ops_init();
#endif

	clear_sched_clock_stable();
}

/*
 * Display CPU info for all CPUs.
 */
static void *
c_start (struct seq_file *m, loff_t *pos)
{
    	/* Looks like the caller will call repeatedly until we return
	 * 0, signaling EOF perhaps.  This could be used to sequence
	 * through CPUs for example.  Since we print all cpu info in our
	 * show_cpuinfo() disregarding 'pos' (which I assume is 'v' above)
	 * we only allow for one "position".  */
	return ((long)*pos < 1) ? (void *)1 : NULL;
}

static void *
c_next (struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}

static void
c_stop (struct seq_file *m, void *v)
{
}

const struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_cpuinfo
};

static struct resource central_bus = {
	.name	= "Central Bus",
	.start	= F_EXTEND(0xfff80000),
	.end    = F_EXTEND(0xfffaffff),
	.flags	= IORESOURCE_MEM,
};

static struct resource local_broadcast = {
	.name	= "Local Broadcast",
	.start	= F_EXTEND(0xfffb0000),
	.end	= F_EXTEND(0xfffdffff),
	.flags	= IORESOURCE_MEM,
};

static struct resource global_broadcast = {
	.name	= "Global Broadcast",
	.start	= F_EXTEND(0xfffe0000),
	.end	= F_EXTEND(0xffffffff),
	.flags	= IORESOURCE_MEM,
};

static int __init parisc_init_resources(void)
{
	int result;

	result = request_resource(&iomem_resource, &central_bus);
	if (result < 0) {
		printk(KERN_ERR 
		       "%s: failed to claim %s address space!\n", 
		       __FILE__, central_bus.name);
		return result;
	}

	result = request_resource(&iomem_resource, &local_broadcast);
	if (result < 0) {
		printk(KERN_ERR 
		       "%s: failed to claim %s address space!\n",
		       __FILE__, local_broadcast.name);
		return result;
	}

	result = request_resource(&iomem_resource, &global_broadcast);
	if (result < 0) {
		printk(KERN_ERR 
		       "%s: failed to claim %s address space!\n", 
		       __FILE__, global_broadcast.name);
		return result;
	}

	return 0;
}

static int __init parisc_init(void)
{
	u32 osid = (OS_ID_LINUX << 16);

	parisc_init_resources();
	do_device_inventory();                  /* probe for hardware */

	parisc_pdc_chassis_init();
	
	/* set up a new led state on systems shipped LED State panel */
	pdc_chassis_send_status(PDC_CHASSIS_DIRECT_BSTART);

	/* tell PDC we're Linux. Nevermind failure. */
	pdc_stable_write(0x40, &osid, sizeof(osid));
	
	/* start with known state */
	flush_cache_all_local();
	flush_tlb_all_local(NULL);

	processor_init();
#ifdef CONFIG_SMP
	pr_info("CPU(s): %d out of %d %s at %d.%06d MHz online\n",
		num_online_cpus(), num_present_cpus(),
#else
	pr_info("CPU(s): 1 x %s at %d.%06d MHz\n",
#endif
			boot_cpu_data.cpu_name,
			boot_cpu_data.cpu_hz / 1000000,
			boot_cpu_data.cpu_hz % 1000000	);

#if defined(CONFIG_64BIT) && defined(CONFIG_SMP)
	/* Don't serialize TLB flushes if we run on one CPU only. */
	if (num_online_cpus() == 1)
		pa_serialize_tlb_flushes = 0;
#endif

	apply_alternatives_all();
	parisc_setup_cache_timing();
	return 0;
}
arch_initcall(parisc_init);

void __init start_parisc(void)
{
	int ret, cpunum;
	struct pdc_coproc_cfg coproc_cfg;

	/* check QEMU/SeaBIOS marker in PAGE0 */
	running_on_qemu = (memcmp(&PAGE0->pad0, "SeaBIOS", 8) == 0);

	cpunum = smp_processor_id();

	init_cpu_topology();

	set_firmware_width_unlocked();

	ret = pdc_coproc_cfg_unlocked(&coproc_cfg);
	if (ret >= 0 && coproc_cfg.ccr_functional) {
		mtctl(coproc_cfg.ccr_functional, 10);

		per_cpu(cpu_data, cpunum).fp_rev = coproc_cfg.revision;
		per_cpu(cpu_data, cpunum).fp_model = coproc_cfg.model;

		asm volatile ("fstd	%fr0,8(%sp)");
	} else {
		panic("must have an fpu to boot linux");
	}

	early_trap_init(); /* initialize checksum of fault_vector */

	start_kernel();
	// not reached
}

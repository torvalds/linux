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
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
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

#include <asm/processor.h>
#include <asm/sections.h>
#include <asm/pdc.h>
#include <asm/led.h>
#include <asm/machdep.h>	/* for pa7300lc_init() proto */
#include <asm/pdc_chassis.h>
#include <asm/io.h>
#include <asm/setup.h>
#include <asm/unwind.h>

static char __initdata command_line[COMMAND_LINE_SIZE];

/* Intended for ccio/sba/cpu statistics under /proc/bus/{runway|gsc} */
struct proc_dir_entry * proc_runway_root __read_mostly = NULL;
struct proc_dir_entry * proc_gsc_root __read_mostly = NULL;
struct proc_dir_entry * proc_mckinley_root __read_mostly = NULL;

#if !defined(CONFIG_PA20) && (defined(CONFIG_IOMMU_CCIO) || defined(CONFIG_IOMMU_SBA))
int parisc_bus_is_phys __read_mostly = 1;	/* Assume no IOMMU is present */
EXPORT_SYMBOL(parisc_bus_is_phys);
#endif

void __init setup_cmdline(char **cmdline_p)
{
	extern unsigned int boot_args[];

	/* Collect stuff passed in from the boot loader */

	/* boot_args[0] is free-mem start, boot_args[1] is ptr to command line */
	if (boot_args[0] < 64) {
		/* called from hpux boot loader */
		boot_command_line[0] = '\0';
	} else {
		strlcpy(boot_command_line, (char *)__va(boot_args[1]),
			COMMAND_LINE_SIZE);

#ifdef CONFIG_BLK_DEV_INITRD
		if (boot_args[2] != 0) /* did palo pass us a ramdisk? */
		{
		    initrd_start = (unsigned long)__va(boot_args[2]);
		    initrd_end = (unsigned long)__va(boot_args[3]);
		}
#endif
	}

	strcpy(command_line, boot_command_line);
	*cmdline_p = command_line;
}

#ifdef CONFIG_PA11
void __init dma_ops_init(void)
{
	switch (boot_cpu_data.cpu_type) {
	case pcx:
		/*
		 * We've got way too many dependencies on 1.1 semantics
		 * to support 1.0 boxes at this point.
		 */
		panic(	"PA-RISC Linux currently only supports machines that conform to\n"
			"the PA-RISC 1.1 or 2.0 architecture specification.\n");

	case pcxs:
	case pcxt:
		hppa_dma_ops = &pcx_dma_ops;
		break;
	case pcxl2:
		pa7300lc_init();
	case pcxl: /* falls through */
		hppa_dma_ops = &pcxl_dma_ops;
		break;
	default:
		break;
	}
}
#endif

extern int init_per_cpu(int cpuid);
extern void collect_boot_cpu_data(void);

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

	pdc_console_init();

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

#ifdef CONFIG_CHASSIS_LCD_LED
	/* initialize the LCD/LED after boot_cpu_data is available ! */
	led_init();		/* LCD/LED initialization */
#endif

#ifdef CONFIG_PA11
	dma_ops_init();
#endif

#if defined(CONFIG_VT) && defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;	/* we use do_take_over_console() later ! */
#endif

}

/*
 * Display CPU info for all CPUs.
 * for parisc this is in processor.c
 */
extern int show_cpuinfo (struct seq_file *m, void *v);

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

static void __init parisc_proc_mkdir(void)
{
	/*
	** Can't call proc_mkdir() until after proc_root_init() has been
	** called by start_kernel(). In other words, this code can't
	** live in arch/.../setup.c because start_parisc() calls
	** start_kernel().
	*/
	switch (boot_cpu_data.cpu_type) {
	case pcxl:
	case pcxl2:
		if (NULL == proc_gsc_root)
		{
			proc_gsc_root = proc_mkdir("bus/gsc", NULL);
		}
		break;
        case pcxt_:
        case pcxu:
        case pcxu_:
        case pcxw:
        case pcxw_:
        case pcxw2:
                if (NULL == proc_runway_root)
                {
                        proc_runway_root = proc_mkdir("bus/runway", NULL);
                }
                break;
	case mako:
	case mako2:
                if (NULL == proc_mckinley_root)
                {
                        proc_mckinley_root = proc_mkdir("bus/mckinley", NULL);
                }
                break;
	default:
		/* FIXME: this was added to prevent the compiler 
		 * complaining about missing pcx, pcxs and pcxt
		 * I'm assuming they have neither gsc nor runway */
		break;
	}
}

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
		       "%s: failed to claim %saddress space!\n", 
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

extern void gsc_init(void);
extern void processor_init(void);
extern void ccio_init(void);
extern void hppb_init(void);
extern void dino_init(void);
extern void iosapic_init(void);
extern void lba_init(void);
extern void sba_init(void);
extern void eisa_init(void);

static int __init parisc_init(void)
{
	u32 osid = (OS_ID_LINUX << 16);

	parisc_proc_mkdir();
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

	parisc_setup_cache_timing();

	/* These are in a non-obvious order, will fix when we have an iotree */
#if defined(CONFIG_IOSAPIC)
	iosapic_init();
#endif
#if defined(CONFIG_IOMMU_SBA)
	sba_init();
#endif
#if defined(CONFIG_PCI_LBA)
	lba_init();
#endif

	/* CCIO before any potential subdevices */
#if defined(CONFIG_IOMMU_CCIO)
	ccio_init();
#endif

	/*
	 * Need to register Asp & Wax before the EISA adapters for the IRQ
	 * regions.  EISA must come before PCI to be sure it gets IRQ region
	 * 0.
	 */
#if defined(CONFIG_GSC_LASI) || defined(CONFIG_GSC_WAX)
	gsc_init();
#endif
#ifdef CONFIG_EISA
	eisa_init();
#endif

#if defined(CONFIG_HPPB)
	hppb_init();
#endif

#if defined(CONFIG_GSC_DINO)
	dino_init();
#endif

#ifdef CONFIG_CHASSIS_LCD_LED
	register_led_regions();	/* register LED port info in procfs */
#endif

	return 0;
}
arch_initcall(parisc_init);

void start_parisc(void)
{
	extern void start_kernel(void);
	extern void early_trap_init(void);

	int ret, cpunum;
	struct pdc_coproc_cfg coproc_cfg;

	cpunum = smp_processor_id();

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

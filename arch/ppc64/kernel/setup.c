/*
 * 
 * Common boot and setup code.
 *
 * Copyright (C) 2001 PPC64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#undef DEBUG

#include <linux/config.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/initrd.h>
#include <linux/ide.h>
#include <linux/seq_file.h>
#include <linux/ioport.h>
#include <linux/console.h>
#include <linux/utsname.h>
#include <linux/tty.h>
#include <linux/root_dev.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/unistd.h>
#include <linux/serial.h>
#include <linux/serial_8250.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/processor.h>
#include <asm/pgtable.h>
#include <asm/bootinfo.h>
#include <asm/smp.h>
#include <asm/elf.h>
#include <asm/machdep.h>
#include <asm/paca.h>
#include <asm/ppcdebug.h>
#include <asm/time.h>
#include <asm/cputable.h>
#include <asm/sections.h>
#include <asm/btext.h>
#include <asm/nvram.h>
#include <asm/setup.h>
#include <asm/system.h>
#include <asm/rtas.h>
#include <asm/iommu.h>
#include <asm/serial.h>
#include <asm/cache.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/lmb.h>
#include <asm/iSeries/ItLpNaca.h>

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

/*
 * Here are some early debugging facilities. You can enable one
 * but your kernel will not boot on anything else if you do so
 */

/* This one is for use on LPAR machines that support an HVC console
 * on vterm 0
 */
extern void udbg_init_debug_lpar(void);
/* This one is for use on Apple G5 machines
 */
extern void udbg_init_pmac_realmode(void);
/* That's RTAS panel debug */
extern void call_rtas_display_status_delay(unsigned char c);
/* Here's maple real mode debug */
extern void udbg_init_maple_realmode(void);

#define EARLY_DEBUG_INIT() do {} while(0)

#if 0
#define EARLY_DEBUG_INIT() udbg_init_debug_lpar()
#define EARLY_DEBUG_INIT() udbg_init_maple_realmode()
#define EARLY_DEBUG_INIT() udbg_init_pmac_realmode()
#define EARLY_DEBUG_INIT()						\
	do { udbg_putc = call_rtas_display_status_delay; } while(0)
#endif

/* extern void *stab; */
extern unsigned long klimit;

extern void mm_init_ppc64(void);
extern void stab_initialize(unsigned long stab);
extern void htab_initialize(void);
extern void early_init_devtree(void *flat_dt);
extern void unflatten_device_tree(void);

extern void smp_release_cpus(void);

int have_of = 1;
int boot_cpuid = 0;
int boot_cpuid_phys = 0;
dev_t boot_dev;
u64 ppc64_pft_size;

struct ppc64_caches ppc64_caches;
EXPORT_SYMBOL_GPL(ppc64_caches);

/*
 * These are used in binfmt_elf.c to put aux entries on the stack
 * for each elf executable being started.
 */
int dcache_bsize;
int icache_bsize;
int ucache_bsize;

/* The main machine-dep calls structure
 */
struct machdep_calls ppc_md;
EXPORT_SYMBOL(ppc_md);

#ifdef CONFIG_MAGIC_SYSRQ
unsigned long SYSRQ_KEY;
#endif /* CONFIG_MAGIC_SYSRQ */


static int ppc64_panic_event(struct notifier_block *, unsigned long, void *);
static struct notifier_block ppc64_panic_block = {
	.notifier_call = ppc64_panic_event,
	.priority = INT_MIN /* may not return; must be done last */
};

/*
 * Perhaps we can put the pmac screen_info[] here
 * on pmac as well so we don't need the ifdef's.
 * Until we get multiple-console support in here
 * that is.  -- Cort
 * Maybe tie it to serial consoles, since this is really what
 * these processors use on existing boards.  -- Dan
 */ 
struct screen_info screen_info = {
	.orig_x = 0,
	.orig_y = 25,
	.orig_video_cols = 80,
	.orig_video_lines = 25,
	.orig_video_isVGA = 1,
	.orig_video_points = 16
};

#if defined(CONFIG_PPC_MULTIPLATFORM) && defined(CONFIG_SMP)

static int smt_enabled_cmdline;

/* Look for ibm,smt-enabled OF option */
static void check_smt_enabled(void)
{
	struct device_node *dn;
	char *smt_option;

	/* Allow the command line to overrule the OF option */
	if (smt_enabled_cmdline)
		return;

	dn = of_find_node_by_path("/options");

	if (dn) {
		smt_option = (char *)get_property(dn, "ibm,smt-enabled", NULL);

                if (smt_option) {
			if (!strcmp(smt_option, "on"))
				smt_enabled_at_boot = 1;
			else if (!strcmp(smt_option, "off"))
				smt_enabled_at_boot = 0;
                }
        }
}

/* Look for smt-enabled= cmdline option */
static int __init early_smt_enabled(char *p)
{
	smt_enabled_cmdline = 1;

	if (!p)
		return 0;

	if (!strcmp(p, "on") || !strcmp(p, "1"))
		smt_enabled_at_boot = 1;
	else if (!strcmp(p, "off") || !strcmp(p, "0"))
		smt_enabled_at_boot = 0;

	return 0;
}
early_param("smt-enabled", early_smt_enabled);

/**
 * setup_cpu_maps - initialize the following cpu maps:
 *                  cpu_possible_map
 *                  cpu_present_map
 *                  cpu_sibling_map
 *
 * Having the possible map set up early allows us to restrict allocations
 * of things like irqstacks to num_possible_cpus() rather than NR_CPUS.
 *
 * We do not initialize the online map here; cpus set their own bits in
 * cpu_online_map as they come up.
 *
 * This function is valid only for Open Firmware systems.  finish_device_tree
 * must be called before using this.
 *
 * While we're here, we may as well set the "physical" cpu ids in the paca.
 */
static void __init setup_cpu_maps(void)
{
	struct device_node *dn = NULL;
	int cpu = 0;
	int swap_cpuid = 0;

	check_smt_enabled();

	while ((dn = of_find_node_by_type(dn, "cpu")) && cpu < NR_CPUS) {
		u32 *intserv;
		int j, len = sizeof(u32), nthreads;

		intserv = (u32 *)get_property(dn, "ibm,ppc-interrupt-server#s",
					      &len);
		if (!intserv)
			intserv = (u32 *)get_property(dn, "reg", NULL);

		nthreads = len / sizeof(u32);

		for (j = 0; j < nthreads && cpu < NR_CPUS; j++) {
			cpu_set(cpu, cpu_present_map);
			set_hard_smp_processor_id(cpu, intserv[j]);

			if (intserv[j] == boot_cpuid_phys)
				swap_cpuid = cpu;
			cpu_set(cpu, cpu_possible_map);
			cpu++;
		}
	}

	/* Swap CPU id 0 with boot_cpuid_phys, so we can always assume that
	 * boot cpu is logical 0.
	 */
	if (boot_cpuid_phys != get_hard_smp_processor_id(0)) {
		u32 tmp;
		tmp = get_hard_smp_processor_id(0);
		set_hard_smp_processor_id(0, boot_cpuid_phys);
		set_hard_smp_processor_id(swap_cpuid, tmp);
	}

	/*
	 * On pSeries LPAR, we need to know how many cpus
	 * could possibly be added to this partition.
	 */
	if (systemcfg->platform == PLATFORM_PSERIES_LPAR &&
				(dn = of_find_node_by_path("/rtas"))) {
		int num_addr_cell, num_size_cell, maxcpus;
		unsigned int *ireg;

		num_addr_cell = prom_n_addr_cells(dn);
		num_size_cell = prom_n_size_cells(dn);

		ireg = (unsigned int *)
			get_property(dn, "ibm,lrdr-capacity", NULL);

		if (!ireg)
			goto out;

		maxcpus = ireg[num_addr_cell + num_size_cell];

		/* Double maxcpus for processors which have SMT capability */
		if (cpu_has_feature(CPU_FTR_SMT))
			maxcpus *= 2;

		if (maxcpus > NR_CPUS) {
			printk(KERN_WARNING
			       "Partition configured for %d cpus, "
			       "operating system maximum is %d.\n",
			       maxcpus, NR_CPUS);
			maxcpus = NR_CPUS;
		} else
			printk(KERN_INFO "Partition configured for %d cpus.\n",
			       maxcpus);

		for (cpu = 0; cpu < maxcpus; cpu++)
			cpu_set(cpu, cpu_possible_map);
	out:
		of_node_put(dn);
	}

	/*
	 * Do the sibling map; assume only two threads per processor.
	 */
	for_each_cpu(cpu) {
		cpu_set(cpu, cpu_sibling_map[cpu]);
		if (cpu_has_feature(CPU_FTR_SMT))
			cpu_set(cpu ^ 0x1, cpu_sibling_map[cpu]);
	}

	systemcfg->processorCount = num_present_cpus();
}
#endif /* defined(CONFIG_PPC_MULTIPLATFORM) && defined(CONFIG_SMP) */


#ifdef CONFIG_PPC_MULTIPLATFORM

extern struct machdep_calls pSeries_md;
extern struct machdep_calls pmac_md;
extern struct machdep_calls maple_md;
extern struct machdep_calls bpa_md;

/* Ultimately, stuff them in an elf section like initcalls... */
static struct machdep_calls __initdata *machines[] = {
#ifdef CONFIG_PPC_PSERIES
	&pSeries_md,
#endif /* CONFIG_PPC_PSERIES */
#ifdef CONFIG_PPC_PMAC
	&pmac_md,
#endif /* CONFIG_PPC_PMAC */
#ifdef CONFIG_PPC_MAPLE
	&maple_md,
#endif /* CONFIG_PPC_MAPLE */
#ifdef CONFIG_PPC_BPA
	&bpa_md,
#endif
	NULL
};

/*
 * Early initialization entry point. This is called by head.S
 * with MMU translation disabled. We rely on the "feature" of
 * the CPU that ignores the top 2 bits of the address in real
 * mode so we can access kernel globals normally provided we
 * only toy with things in the RMO region. From here, we do
 * some early parsing of the device-tree to setup out LMB
 * data structures, and allocate & initialize the hash table
 * and segment tables so we can start running with translation
 * enabled.
 *
 * It is this function which will call the probe() callback of
 * the various platform types and copy the matching one to the
 * global ppc_md structure. Your platform can eventually do
 * some very early initializations from the probe() routine, but
 * this is not recommended, be very careful as, for example, the
 * device-tree is not accessible via normal means at this point.
 */

void __init early_setup(unsigned long dt_ptr)
{
	struct paca_struct *lpaca = get_paca();
	static struct machdep_calls **mach;

	/*
	 * Enable early debugging if any specified (see top of
	 * this file)
	 */
	EARLY_DEBUG_INIT();

	DBG(" -> early_setup()\n");

	/*
	 * Fill the default DBG level (do we want to keep
	 * that old mecanism around forever ?)
	 */
	ppcdbg_initialize();

	/*
	 * Do early initializations using the flattened device
	 * tree, like retreiving the physical memory map or
	 * calculating/retreiving the hash table size
	 */
	early_init_devtree(__va(dt_ptr));

	/*
	 * Iterate all ppc_md structures until we find the proper
	 * one for the current machine type
	 */
	DBG("Probing machine type for platform %x...\n",
	    systemcfg->platform);

	for (mach = machines; *mach; mach++) {
		if ((*mach)->probe(systemcfg->platform))
			break;
	}
	/* What can we do if we didn't find ? */
	if (*mach == NULL) {
		DBG("No suitable machine found !\n");
		for (;;);
	}
	ppc_md = **mach;

	DBG("Found, Initializing memory management...\n");

	/*
	 * Initialize stab / SLB management
	 */
	stab_initialize(lpaca->stab_real);

	/*
	 * Initialize the MMU Hash table and create the linear mapping
	 * of memory
	 */
	htab_initialize();

	DBG(" <- early_setup()\n");
}


/*
 * Initialize some remaining members of the ppc64_caches and systemcfg structures
 * (at least until we get rid of them completely). This is mostly some
 * cache informations about the CPU that will be used by cache flush
 * routines and/or provided to userland
 */
static void __init initialize_cache_info(void)
{
	struct device_node *np;
	unsigned long num_cpus = 0;

	DBG(" -> initialize_cache_info()\n");

	for (np = NULL; (np = of_find_node_by_type(np, "cpu"));) {
		num_cpus += 1;

		/* We're assuming *all* of the CPUs have the same
		 * d-cache and i-cache sizes... -Peter
		 */

		if ( num_cpus == 1 ) {
			u32 *sizep, *lsizep;
			u32 size, lsize;
			const char *dc, *ic;

			/* Then read cache informations */
			if (systemcfg->platform == PLATFORM_POWERMAC) {
				dc = "d-cache-block-size";
				ic = "i-cache-block-size";
			} else {
				dc = "d-cache-line-size";
				ic = "i-cache-line-size";
			}

			size = 0;
			lsize = cur_cpu_spec->dcache_bsize;
			sizep = (u32 *)get_property(np, "d-cache-size", NULL);
			if (sizep != NULL)
				size = *sizep;
			lsizep = (u32 *) get_property(np, dc, NULL);
			if (lsizep != NULL)
				lsize = *lsizep;
			if (sizep == 0 || lsizep == 0)
				DBG("Argh, can't find dcache properties ! "
				    "sizep: %p, lsizep: %p\n", sizep, lsizep);

			systemcfg->dcache_size = ppc64_caches.dsize = size;
			systemcfg->dcache_line_size =
				ppc64_caches.dline_size = lsize;
			ppc64_caches.log_dline_size = __ilog2(lsize);
			ppc64_caches.dlines_per_page = PAGE_SIZE / lsize;

			size = 0;
			lsize = cur_cpu_spec->icache_bsize;
			sizep = (u32 *)get_property(np, "i-cache-size", NULL);
			if (sizep != NULL)
				size = *sizep;
			lsizep = (u32 *)get_property(np, ic, NULL);
			if (lsizep != NULL)
				lsize = *lsizep;
			if (sizep == 0 || lsizep == 0)
				DBG("Argh, can't find icache properties ! "
				    "sizep: %p, lsizep: %p\n", sizep, lsizep);

			systemcfg->icache_size = ppc64_caches.isize = size;
			systemcfg->icache_line_size =
				ppc64_caches.iline_size = lsize;
			ppc64_caches.log_iline_size = __ilog2(lsize);
			ppc64_caches.ilines_per_page = PAGE_SIZE / lsize;
		}
	}

	/* Add an eye catcher and the systemcfg layout version number */
	strcpy(systemcfg->eye_catcher, "SYSTEMCFG:PPC64");
	systemcfg->version.major = SYSTEMCFG_MAJOR;
	systemcfg->version.minor = SYSTEMCFG_MINOR;
	systemcfg->processor = mfspr(SPRN_PVR);

	DBG(" <- initialize_cache_info()\n");
}

static void __init check_for_initrd(void)
{
#ifdef CONFIG_BLK_DEV_INITRD
	u64 *prop;

	DBG(" -> check_for_initrd()\n");

	if (of_chosen) {
		prop = (u64 *)get_property(of_chosen,
				"linux,initrd-start", NULL);
		if (prop != NULL) {
			initrd_start = (unsigned long)__va(*prop);
			prop = (u64 *)get_property(of_chosen,
					"linux,initrd-end", NULL);
			if (prop != NULL) {
				initrd_end = (unsigned long)__va(*prop);
				initrd_below_start_ok = 1;
			} else
				initrd_start = 0;
		}
	}

	/* If we were passed an initrd, set the ROOT_DEV properly if the values
	 * look sensible. If not, clear initrd reference.
	 */
	if (initrd_start >= KERNELBASE && initrd_end >= KERNELBASE &&
	    initrd_end > initrd_start)
		ROOT_DEV = Root_RAM0;
	else
		initrd_start = initrd_end = 0;

	if (initrd_start)
		printk("Found initrd at 0x%lx:0x%lx\n", initrd_start, initrd_end);

	DBG(" <- check_for_initrd()\n");
#endif /* CONFIG_BLK_DEV_INITRD */
}

#endif /* CONFIG_PPC_MULTIPLATFORM */

/*
 * Do some initial setup of the system.  The parameters are those which 
 * were passed in from the bootloader.
 */
void __init setup_system(void)
{
	DBG(" -> setup_system()\n");

#ifdef CONFIG_PPC_ISERIES
	/* pSeries systems are identified in prom.c via OF. */
	if (itLpNaca.xLparInstalled == 1)
		systemcfg->platform = PLATFORM_ISERIES_LPAR;

	ppc_md.init_early();
#else /* CONFIG_PPC_ISERIES */

	/*
	 * Unflatten the device-tree passed by prom_init or kexec
	 */
	unflatten_device_tree();

	/*
	 * Fill the ppc64_caches & systemcfg structures with informations
	 * retreived from the device-tree. Need to be called before
	 * finish_device_tree() since the later requires some of the
	 * informations filled up here to properly parse the interrupt
	 * tree.
	 * It also sets up the cache line sizes which allows to call
	 * routines like flush_icache_range (used by the hash init
	 * later on).
	 */
	initialize_cache_info();

#ifdef CONFIG_PPC_RTAS
	/*
	 * Initialize RTAS if available
	 */
	rtas_initialize();
#endif /* CONFIG_PPC_RTAS */

	/*
	 * Check if we have an initrd provided via the device-tree
	 */
	check_for_initrd();

	/*
	 * Do some platform specific early initializations, that includes
	 * setting up the hash table pointers. It also sets up some interrupt-mapping
	 * related options that will be used by finish_device_tree()
	 */
	ppc_md.init_early();

	/*
	 * "Finish" the device-tree, that is do the actual parsing of
	 * some of the properties like the interrupt map
	 */
	finish_device_tree();

	/*
	 * Initialize xmon
	 */
#ifdef CONFIG_XMON_DEFAULT
	xmon_init(1);
#endif
	/*
	 * Register early console
	 */
	register_early_udbg_console();

	/* Save unparsed command line copy for /proc/cmdline */
	strlcpy(saved_command_line, cmd_line, COMMAND_LINE_SIZE);

	parse_early_param();
#endif /* !CONFIG_PPC_ISERIES */

#if defined(CONFIG_SMP) && !defined(CONFIG_PPC_ISERIES)
	/*
	 * iSeries has already initialized the cpu maps at this point.
	 */
	setup_cpu_maps();

	/* Release secondary cpus out of their spinloops at 0x60 now that
	 * we can map physical -> logical CPU ids
	 */
	smp_release_cpus();
#endif /* defined(CONFIG_SMP) && !defined(CONFIG_PPC_ISERIES) */

	printk("Starting Linux PPC64 %s\n", system_utsname.version);

	printk("-----------------------------------------------------\n");
	printk("ppc64_pft_size                = 0x%lx\n", ppc64_pft_size);
	printk("ppc64_debug_switch            = 0x%lx\n", ppc64_debug_switch);
	printk("ppc64_interrupt_controller    = 0x%ld\n", ppc64_interrupt_controller);
	printk("systemcfg                     = 0x%p\n", systemcfg);
	printk("systemcfg->platform           = 0x%x\n", systemcfg->platform);
	printk("systemcfg->processorCount     = 0x%lx\n", systemcfg->processorCount);
	printk("systemcfg->physicalMemorySize = 0x%lx\n", systemcfg->physicalMemorySize);
	printk("ppc64_caches.dcache_line_size = 0x%x\n",
			ppc64_caches.dline_size);
	printk("ppc64_caches.icache_line_size = 0x%x\n",
			ppc64_caches.iline_size);
	printk("htab_address                  = 0x%p\n", htab_address);
	printk("htab_hash_mask                = 0x%lx\n", htab_hash_mask);
	printk("-----------------------------------------------------\n");

	mm_init_ppc64();

	DBG(" <- setup_system()\n");
}

/* also used by kexec */
void machine_shutdown(void)
{
	if (ppc_md.nvram_sync)
		ppc_md.nvram_sync();
}

void machine_restart(char *cmd)
{
	machine_shutdown();
	ppc_md.restart(cmd);
#ifdef CONFIG_SMP
	smp_send_stop();
#endif
	printk(KERN_EMERG "System Halted, OK to turn off power\n");
	local_irq_disable();
	while (1) ;
}

void machine_power_off(void)
{
	machine_shutdown();
	ppc_md.power_off();
#ifdef CONFIG_SMP
	smp_send_stop();
#endif
	printk(KERN_EMERG "System Halted, OK to turn off power\n");
	local_irq_disable();
	while (1) ;
}
/* Used by the G5 thermal driver */
EXPORT_SYMBOL_GPL(machine_power_off);

void machine_halt(void)
{
	machine_shutdown();
	ppc_md.halt();
#ifdef CONFIG_SMP
	smp_send_stop();
#endif
	printk(KERN_EMERG "System Halted, OK to turn off power\n");
	local_irq_disable();
	while (1) ;
}

static int ppc64_panic_event(struct notifier_block *this,
                             unsigned long event, void *ptr)
{
	ppc_md.panic((char *)ptr);  /* May not return */
	return NOTIFY_DONE;
}


#ifdef CONFIG_SMP
DEFINE_PER_CPU(unsigned int, pvr);
#endif

static int show_cpuinfo(struct seq_file *m, void *v)
{
	unsigned long cpu_id = (unsigned long)v - 1;
	unsigned int pvr;
	unsigned short maj;
	unsigned short min;

	if (cpu_id == NR_CPUS) {
		seq_printf(m, "timebase\t: %lu\n", ppc_tb_freq);

		if (ppc_md.get_cpuinfo != NULL)
			ppc_md.get_cpuinfo(m);

		return 0;
	}

	/* We only show online cpus: disable preempt (overzealous, I
	 * knew) to prevent cpu going down. */
	preempt_disable();
	if (!cpu_online(cpu_id)) {
		preempt_enable();
		return 0;
	}

#ifdef CONFIG_SMP
	pvr = per_cpu(pvr, cpu_id);
#else
	pvr = mfspr(SPRN_PVR);
#endif
	maj = (pvr >> 8) & 0xFF;
	min = pvr & 0xFF;

	seq_printf(m, "processor\t: %lu\n", cpu_id);
	seq_printf(m, "cpu\t\t: ");

	if (cur_cpu_spec->pvr_mask)
		seq_printf(m, "%s", cur_cpu_spec->cpu_name);
	else
		seq_printf(m, "unknown (%08x)", pvr);

#ifdef CONFIG_ALTIVEC
	if (cpu_has_feature(CPU_FTR_ALTIVEC))
		seq_printf(m, ", altivec supported");
#endif /* CONFIG_ALTIVEC */

	seq_printf(m, "\n");

	/*
	 * Assume here that all clock rates are the same in a
	 * smp system.  -- Cort
	 */
	seq_printf(m, "clock\t\t: %lu.%06luMHz\n", ppc_proc_freq / 1000000,
		   ppc_proc_freq % 1000000);

	seq_printf(m, "revision\t: %hd.%hd\n\n", maj, min);

	preempt_enable();
	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos <= NR_CPUS ? (void *)((*pos)+1) : NULL;
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
	.start =c_start,
	.next =	c_next,
	.stop =	c_stop,
	.show =	show_cpuinfo,
};

/*
 * These three variables are used to save values passed to us by prom_init()
 * via the device tree. The TCE variables are needed because with a memory_limit
 * in force we may need to explicitly map the TCE are at the top of RAM.
 */
unsigned long memory_limit;
unsigned long tce_alloc_start;
unsigned long tce_alloc_end;

#ifdef CONFIG_PPC_ISERIES
/*
 * On iSeries we just parse the mem=X option from the command line.
 * On pSeries it's a bit more complicated, see prom_init_mem()
 */
static int __init early_parsemem(char *p)
{
	if (!p)
		return 0;

	memory_limit = ALIGN(memparse(p, &p), PAGE_SIZE);

	return 0;
}
early_param("mem", early_parsemem);
#endif /* CONFIG_PPC_ISERIES */

#ifdef CONFIG_PPC_MULTIPLATFORM
static int __init set_preferred_console(void)
{
	struct device_node *prom_stdout = NULL;
	char *name;
	u32 *spd;
	int offset = 0;

	DBG(" -> set_preferred_console()\n");

	/* The user has requested a console so this is already set up. */
	if (strstr(saved_command_line, "console=")) {
		DBG(" console was specified !\n");
		return -EBUSY;
	}

	if (!of_chosen) {
		DBG(" of_chosen is NULL !\n");
		return -ENODEV;
	}
	/* We are getting a weird phandle from OF ... */
	/* ... So use the full path instead */
	name = (char *)get_property(of_chosen, "linux,stdout-path", NULL);
	if (name == NULL) {
		DBG(" no linux,stdout-path !\n");
		return -ENODEV;
	}
	prom_stdout = of_find_node_by_path(name);
	if (!prom_stdout) {
		DBG(" can't find stdout package %s !\n", name);
		return -ENODEV;
	}	
	DBG("stdout is %s\n", prom_stdout->full_name);

	name = (char *)get_property(prom_stdout, "name", NULL);
	if (!name) {
		DBG(" stdout package has no name !\n");
		goto not_found;
	}
	spd = (u32 *)get_property(prom_stdout, "current-speed", NULL);

	if (0)
		;
#ifdef CONFIG_SERIAL_8250_CONSOLE
	else if (strcmp(name, "serial") == 0) {
		int i;
		u32 *reg = (u32 *)get_property(prom_stdout, "reg", &i);
		if (i > 8) {
			switch (reg[1]) {
				case 0x3f8:
					offset = 0;
					break;
				case 0x2f8:
					offset = 1;
					break;
				case 0x898:
					offset = 2;
					break;
				case 0x890:
					offset = 3;
					break;
				default:
					/* We dont recognise the serial port */
					goto not_found;
			}
		}
	}
#endif /* CONFIG_SERIAL_8250_CONSOLE */
#ifdef CONFIG_PPC_PSERIES
	else if (strcmp(name, "vty") == 0) {
 		u32 *reg = (u32 *)get_property(prom_stdout, "reg", NULL);
 		char *compat = (char *)get_property(prom_stdout, "compatible", NULL);

 		if (reg && compat && (strcmp(compat, "hvterm-protocol") == 0)) {
 			/* Host Virtual Serial Interface */
 			int offset;
 			switch (reg[0]) {
 				case 0x30000000:
 					offset = 0;
 					break;
 				case 0x30000001:
 					offset = 1;
 					break;
 				default:
					goto not_found;
 			}
			of_node_put(prom_stdout);
			DBG("Found hvsi console at offset %d\n", offset);
 			return add_preferred_console("hvsi", offset, NULL);
 		} else {
 			/* pSeries LPAR virtual console */
			of_node_put(prom_stdout);
			DBG("Found hvc console\n");
 			return add_preferred_console("hvc", 0, NULL);
 		}
	}
#endif /* CONFIG_PPC_PSERIES */
#ifdef CONFIG_SERIAL_PMACZILOG_CONSOLE
	else if (strcmp(name, "ch-a") == 0)
		offset = 0;
	else if (strcmp(name, "ch-b") == 0)
		offset = 1;
#endif /* CONFIG_SERIAL_PMACZILOG_CONSOLE */
	else
		goto not_found;
	of_node_put(prom_stdout);

	DBG("Found serial console at ttyS%d\n", offset);

	if (spd) {
		static char __initdata opt[16];
		sprintf(opt, "%d", *spd);
		return add_preferred_console("ttyS", offset, opt);
	} else
		return add_preferred_console("ttyS", offset, NULL);

 not_found:
	DBG("No preferred console found !\n");
	of_node_put(prom_stdout);
	return -ENODEV;
}
console_initcall(set_preferred_console);
#endif /* CONFIG_PPC_MULTIPLATFORM */

#ifdef CONFIG_IRQSTACKS
static void __init irqstack_early_init(void)
{
	unsigned int i;

	/*
	 * interrupt stacks must be under 256MB, we cannot afford to take
	 * SLB misses on them.
	 */
	for_each_cpu(i) {
		softirq_ctx[i] = (struct thread_info *)__va(lmb_alloc_base(THREAD_SIZE,
					THREAD_SIZE, 0x10000000));
		hardirq_ctx[i] = (struct thread_info *)__va(lmb_alloc_base(THREAD_SIZE,
					THREAD_SIZE, 0x10000000));
	}
}
#else
#define irqstack_early_init()
#endif

/*
 * Stack space used when we detect a bad kernel stack pointer, and
 * early in SMP boots before relocation is enabled.
 */
static void __init emergency_stack_init(void)
{
	unsigned long limit;
	unsigned int i;

	/*
	 * Emergency stacks must be under 256MB, we cannot afford to take
	 * SLB misses on them. The ABI also requires them to be 128-byte
	 * aligned.
	 *
	 * Since we use these as temporary stacks during secondary CPU
	 * bringup, we need to get at them in real mode. This means they
	 * must also be within the RMO region.
	 */
	limit = min(0x10000000UL, lmb.rmo_size);

	for_each_cpu(i)
		paca[i].emergency_sp = __va(lmb_alloc_base(PAGE_SIZE, 128,
						limit)) + PAGE_SIZE;
}

/*
 * Called from setup_arch to initialize the bitmap of available
 * syscalls in the systemcfg page
 */
void __init setup_syscall_map(void)
{
	unsigned int i, count64 = 0, count32 = 0;
	extern unsigned long *sys_call_table;
	extern unsigned long *sys_call_table32;
	extern unsigned long sys_ni_syscall;


	for (i = 0; i < __NR_syscalls; i++) {
		if (sys_call_table[i] == sys_ni_syscall)
			continue;
		count64++;
		systemcfg->syscall_map_64[i >> 5] |= 0x80000000UL >> (i & 0x1f);
	}
	for (i = 0; i < __NR_syscalls; i++) {
		if (sys_call_table32[i] == sys_ni_syscall)
			continue;
		count32++;
		systemcfg->syscall_map_32[i >> 5] |= 0x80000000UL >> (i & 0x1f);
	}
	printk(KERN_INFO "Syscall map setup, %d 32 bits and %d 64 bits syscalls\n",
	       count32, count64);
}

/*
 * Called into from start_kernel, after lock_kernel has been called.
 * Initializes bootmem, which is unsed to manage page allocation until
 * mem_init is called.
 */
void __init setup_arch(char **cmdline_p)
{
	extern void do_init_bootmem(void);

	ppc64_boot_msg(0x12, "Setup Arch");

	*cmdline_p = cmd_line;

	/*
	 * Set cache line size based on type of cpu as a default.
	 * Systems with OF can look in the properties on the cpu node(s)
	 * for a possibly more accurate value.
	 */
	dcache_bsize = ppc64_caches.dline_size;
	icache_bsize = ppc64_caches.iline_size;

	/* reboot on panic */
	panic_timeout = 180;

	if (ppc_md.panic)
		notifier_chain_register(&panic_notifier_list, &ppc64_panic_block);

	init_mm.start_code = PAGE_OFFSET;
	init_mm.end_code = (unsigned long) _etext;
	init_mm.end_data = (unsigned long) _edata;
	init_mm.brk = klimit;
	
	irqstack_early_init();
	emergency_stack_init();

	stabs_alloc();

	/* set up the bootmem stuff with available memory */
	do_init_bootmem();
	sparse_init();

	/* initialize the syscall map in systemcfg */
	setup_syscall_map();

	ppc_md.setup_arch();

	/* Use the default idle loop if the platform hasn't provided one. */
	if (NULL == ppc_md.idle_loop) {
		ppc_md.idle_loop = default_idle;
		printk(KERN_INFO "Using default idle loop\n");
	}

	paging_init();
	ppc64_boot_msg(0x15, "Setup Done");
}


/* ToDo: do something useful if ppc_md is not yet setup. */
#define PPC64_LINUX_FUNCTION 0x0f000000
#define PPC64_IPL_MESSAGE 0xc0000000
#define PPC64_TERM_MESSAGE 0xb0000000

static void ppc64_do_msg(unsigned int src, const char *msg)
{
	if (ppc_md.progress) {
		char buf[128];

		sprintf(buf, "%08X\n", src);
		ppc_md.progress(buf, 0);
		snprintf(buf, 128, "%s", msg);
		ppc_md.progress(buf, 0);
	}
}

/* Print a boot progress message. */
void ppc64_boot_msg(unsigned int src, const char *msg)
{
	ppc64_do_msg(PPC64_LINUX_FUNCTION|PPC64_IPL_MESSAGE|src, msg);
	printk("[boot]%04x %s\n", src, msg);
}

/* Print a termination message (print only -- does not stop the kernel) */
void ppc64_terminate_msg(unsigned int src, const char *msg)
{
	ppc64_do_msg(PPC64_LINUX_FUNCTION|PPC64_TERM_MESSAGE|src, msg);
	printk("[terminate]%04x %s\n", src, msg);
}

/* This should only be called on processor 0 during calibrate decr */
void __init setup_default_decr(void)
{
	struct paca_struct *lpaca = get_paca();

	lpaca->default_decr = tb_ticks_per_jiffy;
	lpaca->next_jiffy_update_tb = get_tb() + tb_ticks_per_jiffy;
}

#ifndef CONFIG_PPC_ISERIES
/*
 * This function can be used by platforms to "find" legacy serial ports.
 * It works for "serial" nodes under an "isa" node, and will try to
 * respect the "ibm,aix-loc" property if any. It works with up to 8
 * ports.
 */

#define MAX_LEGACY_SERIAL_PORTS	8
static struct plat_serial8250_port serial_ports[MAX_LEGACY_SERIAL_PORTS+1];
static unsigned int old_serial_count;

void __init generic_find_legacy_serial_ports(u64 *physport,
		unsigned int *default_speed)
{
	struct device_node *np;
	u32 *sizeprop;

	struct isa_reg_property {
		u32 space;
		u32 address;
		u32 size;
	};
	struct pci_reg_property {
		struct pci_address addr;
		u32 size_hi;
		u32 size_lo;
	};                                                                        

	DBG(" -> generic_find_legacy_serial_port()\n");

	*physport = 0;
	if (default_speed)
		*default_speed = 0;

	np = of_find_node_by_path("/");
	if (!np)
		return;

	/* First fill our array */
	for (np = NULL; (np = of_find_node_by_type(np, "serial"));) {
		struct device_node *isa, *pci;
		struct isa_reg_property *reg;
		unsigned long phys_size, addr_size, io_base;
		u32 *rangesp;
		u32 *interrupts, *clk, *spd;
		char *typep;
		int index, rlen, rentsize;

		/* Ok, first check if it's under an "isa" parent */
		isa = of_get_parent(np);
		if (!isa || strcmp(isa->name, "isa")) {
			DBG("%s: no isa parent found\n", np->full_name);
			continue;
		}
		
		/* Now look for an "ibm,aix-loc" property that gives us ordering
		 * if any...
		 */
	 	typep = (char *)get_property(np, "ibm,aix-loc", NULL);

		/* Get the ISA port number */
		reg = (struct isa_reg_property *)get_property(np, "reg", NULL);	
		if (reg == NULL)
			goto next_port;
		/* We assume the interrupt number isn't translated ... */
		interrupts = (u32 *)get_property(np, "interrupts", NULL);
		/* get clock freq. if present */
		clk = (u32 *)get_property(np, "clock-frequency", NULL);
		/* get default speed if present */
		spd = (u32 *)get_property(np, "current-speed", NULL);
		/* Default to locate at end of array */
		index = old_serial_count; /* end of the array by default */

		/* If we have a location index, then use it */
		if (typep && *typep == 'S') {
			index = simple_strtol(typep+1, NULL, 0) - 1;
			/* if index is out of range, use end of array instead */
			if (index >= MAX_LEGACY_SERIAL_PORTS)
				index = old_serial_count;
			/* if our index is still out of range, that mean that
			 * array is full, we could scan for a free slot but that
			 * make little sense to bother, just skip the port
			 */
			if (index >= MAX_LEGACY_SERIAL_PORTS)
				goto next_port;
			if (index >= old_serial_count)
				old_serial_count = index + 1;
			/* Check if there is a port who already claimed our slot */
			if (serial_ports[index].iobase != 0) {
				/* if we still have some room, move it, else override */
				if (old_serial_count < MAX_LEGACY_SERIAL_PORTS) {
					DBG("Moved legacy port %d -> %d\n", index,
					    old_serial_count);
					serial_ports[old_serial_count++] =
						serial_ports[index];
				} else {
					DBG("Replacing legacy port %d\n", index);
				}
			}
		}
		if (index >= MAX_LEGACY_SERIAL_PORTS)
			goto next_port;
		if (index >= old_serial_count)
			old_serial_count = index + 1;

		/* Now fill the entry */
		memset(&serial_ports[index], 0, sizeof(struct plat_serial8250_port));
		serial_ports[index].uartclk = clk ? *clk : BASE_BAUD * 16;
		serial_ports[index].iobase = reg->address;
		serial_ports[index].irq = interrupts ? interrupts[0] : 0;
		serial_ports[index].flags = ASYNC_BOOT_AUTOCONF;

		DBG("Added legacy port, index: %d, port: %x, irq: %d, clk: %d\n",
		    index,
		    serial_ports[index].iobase,
		    serial_ports[index].irq,
		    serial_ports[index].uartclk);

		/* Get phys address of IO reg for port 1 */
		if (index != 0)
			goto next_port;

		pci = of_get_parent(isa);
		if (!pci) {
			DBG("%s: no pci parent found\n", np->full_name);
			goto next_port;
		}

		rangesp = (u32 *)get_property(pci, "ranges", &rlen);
		if (rangesp == NULL) {
			of_node_put(pci);
			goto next_port;
		}
		rlen /= 4;

		/* we need the #size-cells of the PCI bridge node itself */
		phys_size = 1;
		sizeprop = (u32 *)get_property(pci, "#size-cells", NULL);
		if (sizeprop != NULL)
			phys_size = *sizeprop;
		/* we need the parent #addr-cells */
		addr_size = prom_n_addr_cells(pci);
		rentsize = 3 + addr_size + phys_size;
		io_base = 0;
		for (;rlen >= rentsize; rlen -= rentsize,rangesp += rentsize) {
			if (((rangesp[0] >> 24) & 0x3) != 1)
				continue; /* not IO space */
			io_base = rangesp[3];
			if (addr_size == 2)
				io_base = (io_base << 32) | rangesp[4];
		}
		if (io_base != 0) {
			*physport = io_base + reg->address;
			if (default_speed && spd)
				*default_speed = *spd;
		}
		of_node_put(pci);
	next_port:
		of_node_put(isa);
	}

	DBG(" <- generic_find_legacy_serial_port()\n");
}

static struct platform_device serial_device = {
	.name	= "serial8250",
	.id	= PLAT8250_DEV_PLATFORM,
	.dev	= {
		.platform_data = serial_ports,
	},
};

static int __init serial_dev_init(void)
{
	return platform_device_register(&serial_device);
}
arch_initcall(serial_dev_init);

#endif /* CONFIG_PPC_ISERIES */

int check_legacy_ioport(unsigned long base_port)
{
	if (ppc_md.check_legacy_ioport == NULL)
		return 0;
	return ppc_md.check_legacy_ioport(base_port);
}
EXPORT_SYMBOL(check_legacy_ioport);

#ifdef CONFIG_XMON
static int __init early_xmon(char *p)
{
	/* ensure xmon is enabled */
	if (p) {
		if (strncmp(p, "on", 2) == 0)
			xmon_init(1);
		if (strncmp(p, "off", 3) == 0)
			xmon_init(0);
		if (strncmp(p, "early", 5) != 0)
			return 0;
	}
	xmon_init(1);
	debugger(NULL);

	return 0;
}
early_param("xmon", early_xmon);
#endif

void cpu_die(void)
{
	if (ppc_md.cpu_die)
		ppc_md.cpu_die();
}

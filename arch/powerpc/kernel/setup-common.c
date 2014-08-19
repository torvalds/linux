/*
 * Common boot and setup code for both 32-bit and 64-bit.
 * Extracted from arch/powerpc/kernel/setup_64.c.
 *
 * Copyright (C) 2001 PPC64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#undef DEBUG

#include <linux/export.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/initrd.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/ioport.h>
#include <linux/console.h>
#include <linux/screen_info.h>
#include <linux/root_dev.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/unistd.h>
#include <linux/serial.h>
#include <linux/serial_8250.h>
#include <linux/debugfs.h>
#include <linux/percpu.h>
#include <linux/memblock.h>
#include <linux/of_platform.h>
#include <asm/io.h>
#include <asm/paca.h>
#include <asm/prom.h>
#include <asm/processor.h>
#include <asm/vdso_datapage.h>
#include <asm/pgtable.h>
#include <asm/smp.h>
#include <asm/elf.h>
#include <asm/machdep.h>
#include <asm/time.h>
#include <asm/cputable.h>
#include <asm/sections.h>
#include <asm/firmware.h>
#include <asm/btext.h>
#include <asm/nvram.h>
#include <asm/setup.h>
#include <asm/rtas.h>
#include <asm/iommu.h>
#include <asm/serial.h>
#include <asm/cache.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/xmon.h>
#include <asm/cputhreads.h>
#include <mm/mmu_decl.h>
#include <asm/fadump.h>

#ifdef DEBUG
#include <asm/udbg.h>
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

/* The main machine-dep calls structure
 */
struct machdep_calls ppc_md;
EXPORT_SYMBOL(ppc_md);
struct machdep_calls *machine_id;
EXPORT_SYMBOL(machine_id);

int boot_cpuid = -1;
EXPORT_SYMBOL_GPL(boot_cpuid);

unsigned long klimit = (unsigned long) _end;

char cmd_line[COMMAND_LINE_SIZE];

/*
 * This still seems to be needed... -- paulus
 */ 
struct screen_info screen_info = {
	.orig_x = 0,
	.orig_y = 25,
	.orig_video_cols = 80,
	.orig_video_lines = 25,
	.orig_video_isVGA = 1,
	.orig_video_points = 16
};
#if defined(CONFIG_FB_VGA16_MODULE)
EXPORT_SYMBOL(screen_info);
#endif

/* Variables required to store legacy IO irq routing */
int of_i8042_kbd_irq;
EXPORT_SYMBOL_GPL(of_i8042_kbd_irq);
int of_i8042_aux_irq;
EXPORT_SYMBOL_GPL(of_i8042_aux_irq);

#ifdef __DO_IRQ_CANON
/* XXX should go elsewhere eventually */
int ppc_do_canonicalize_irqs;
EXPORT_SYMBOL(ppc_do_canonicalize_irqs);
#endif

/* also used by kexec */
void machine_shutdown(void)
{
#ifdef CONFIG_FA_DUMP
	/*
	 * if fadump is active, cleanup the fadump registration before we
	 * shutdown.
	 */
	fadump_cleanup();
#endif

	if (ppc_md.machine_shutdown)
		ppc_md.machine_shutdown();
}

void machine_restart(char *cmd)
{
	machine_shutdown();
	if (ppc_md.restart)
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
	if (ppc_md.power_off)
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

void (*pm_power_off)(void) = machine_power_off;
EXPORT_SYMBOL_GPL(pm_power_off);

void machine_halt(void)
{
	machine_shutdown();
	if (ppc_md.halt)
		ppc_md.halt();
#ifdef CONFIG_SMP
	smp_send_stop();
#endif
	printk(KERN_EMERG "System Halted, OK to turn off power\n");
	local_irq_disable();
	while (1) ;
}


#ifdef CONFIG_TAU
extern u32 cpu_temp(unsigned long cpu);
extern u32 cpu_temp_both(unsigned long cpu);
#endif /* CONFIG_TAU */

#ifdef CONFIG_SMP
DEFINE_PER_CPU(unsigned int, cpu_pvr);
#endif

static void show_cpuinfo_summary(struct seq_file *m)
{
	struct device_node *root;
	const char *model = NULL;
#if defined(CONFIG_SMP) && defined(CONFIG_PPC32)
	unsigned long bogosum = 0;
	int i;
	for_each_online_cpu(i)
		bogosum += loops_per_jiffy;
	seq_printf(m, "total bogomips\t: %lu.%02lu\n",
		   bogosum/(500000/HZ), bogosum/(5000/HZ) % 100);
#endif /* CONFIG_SMP && CONFIG_PPC32 */
	seq_printf(m, "timebase\t: %lu\n", ppc_tb_freq);
	if (ppc_md.name)
		seq_printf(m, "platform\t: %s\n", ppc_md.name);
	root = of_find_node_by_path("/");
	if (root)
		model = of_get_property(root, "model", NULL);
	if (model)
		seq_printf(m, "model\t\t: %s\n", model);
	of_node_put(root);

	if (ppc_md.show_cpuinfo != NULL)
		ppc_md.show_cpuinfo(m);

#ifdef CONFIG_PPC32
	/* Display the amount of memory */
	seq_printf(m, "Memory\t\t: %d MB\n",
		   (unsigned int)(total_memory / (1024 * 1024)));
#endif
}

static int show_cpuinfo(struct seq_file *m, void *v)
{
	unsigned long cpu_id = (unsigned long)v - 1;
	unsigned int pvr;
	unsigned long proc_freq;
	unsigned short maj;
	unsigned short min;

	/* We only show online cpus: disable preempt (overzealous, I
	 * knew) to prevent cpu going down. */
	preempt_disable();
	if (!cpu_online(cpu_id)) {
		preempt_enable();
		return 0;
	}

#ifdef CONFIG_SMP
	pvr = per_cpu(cpu_pvr, cpu_id);
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

#ifdef CONFIG_TAU
	if (cur_cpu_spec->cpu_features & CPU_FTR_TAU) {
#ifdef CONFIG_TAU_AVERAGE
		/* more straightforward, but potentially misleading */
		seq_printf(m,  "temperature \t: %u C (uncalibrated)\n",
			   cpu_temp(cpu_id));
#else
		/* show the actual temp sensor range */
		u32 temp;
		temp = cpu_temp_both(cpu_id);
		seq_printf(m, "temperature \t: %u-%u C (uncalibrated)\n",
			   temp & 0xff, temp >> 16);
#endif
	}
#endif /* CONFIG_TAU */

	/*
	 * Platforms that have variable clock rates, should implement
	 * the method ppc_md.get_proc_freq() that reports the clock
	 * rate of a given cpu. The rest can use ppc_proc_freq to
	 * report the clock rate that is same across all cpus.
	 */
	if (ppc_md.get_proc_freq)
		proc_freq = ppc_md.get_proc_freq(cpu_id);
	else
		proc_freq = ppc_proc_freq;

	if (proc_freq)
		seq_printf(m, "clock\t\t: %lu.%06luMHz\n",
			   proc_freq / 1000000, proc_freq % 1000000);

	if (ppc_md.show_percpuinfo != NULL)
		ppc_md.show_percpuinfo(m, cpu_id);

	/* If we are a Freescale core do a simple check so
	 * we dont have to keep adding cases in the future */
	if (PVR_VER(pvr) & 0x8000) {
		switch (PVR_VER(pvr)) {
		case 0x8000:	/* 7441/7450/7451, Voyager */
		case 0x8001:	/* 7445/7455, Apollo 6 */
		case 0x8002:	/* 7447/7457, Apollo 7 */
		case 0x8003:	/* 7447A, Apollo 7 PM */
		case 0x8004:	/* 7448, Apollo 8 */
		case 0x800c:	/* 7410, Nitro */
			maj = ((pvr >> 8) & 0xF);
			min = PVR_MIN(pvr);
			break;
		default:	/* e500/book-e */
			maj = PVR_MAJ(pvr);
			min = PVR_MIN(pvr);
			break;
		}
	} else {
		switch (PVR_VER(pvr)) {
			case 0x0020:	/* 403 family */
				maj = PVR_MAJ(pvr) + 1;
				min = PVR_MIN(pvr);
				break;
			case 0x1008:	/* 740P/750P ?? */
				maj = ((pvr >> 8) & 0xFF) - 1;
				min = pvr & 0xFF;
				break;
			default:
				maj = (pvr >> 8) & 0xFF;
				min = pvr & 0xFF;
				break;
		}
	}

	seq_printf(m, "revision\t: %hd.%hd (pvr %04x %04x)\n",
		   maj, min, PVR_VER(pvr), PVR_REV(pvr));

#ifdef CONFIG_PPC32
	seq_printf(m, "bogomips\t: %lu.%02lu\n",
		   loops_per_jiffy / (500000/HZ),
		   (loops_per_jiffy / (5000/HZ)) % 100);
#endif

#ifdef CONFIG_SMP
	seq_printf(m, "\n");
#endif

	preempt_enable();

	/* If this is the last cpu, print the summary */
	if (cpumask_next(cpu_id, cpu_online_mask) >= nr_cpu_ids)
		show_cpuinfo_summary(m);

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	if (*pos == 0)	/* just in case, cpu 0 is not the first */
		*pos = cpumask_first(cpu_online_mask);
	else
		*pos = cpumask_next(*pos - 1, cpu_online_mask);
	if ((*pos) < nr_cpu_ids)
		return (void *)(unsigned long)(*pos + 1);
	return NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	(*pos)++;
	return c_start(m, pos);
}

static void c_stop(struct seq_file *m, void *v)
{
}

const struct seq_operations cpuinfo_op = {
	.start =c_start,
	.next =	c_next,
	.stop =	c_stop,
	.show =	show_cpuinfo,
};

void __init check_for_initrd(void)
{
#ifdef CONFIG_BLK_DEV_INITRD
	DBG(" -> check_for_initrd()  initrd_start=0x%lx  initrd_end=0x%lx\n",
	    initrd_start, initrd_end);

	/* If we were passed an initrd, set the ROOT_DEV properly if the values
	 * look sensible. If not, clear initrd reference.
	 */
	if (is_kernel_addr(initrd_start) && is_kernel_addr(initrd_end) &&
	    initrd_end > initrd_start)
		ROOT_DEV = Root_RAM0;
	else
		initrd_start = initrd_end = 0;

	if (initrd_start)
		printk("Found initrd at 0x%lx:0x%lx\n", initrd_start, initrd_end);

	DBG(" <- check_for_initrd()\n");
#endif /* CONFIG_BLK_DEV_INITRD */
}

#ifdef CONFIG_SMP

int threads_per_core, threads_per_subcore, threads_shift;
cpumask_t threads_core_mask;
EXPORT_SYMBOL_GPL(threads_per_core);
EXPORT_SYMBOL_GPL(threads_per_subcore);
EXPORT_SYMBOL_GPL(threads_shift);
EXPORT_SYMBOL_GPL(threads_core_mask);

static void __init cpu_init_thread_core_maps(int tpc)
{
	int i;

	threads_per_core = tpc;
	threads_per_subcore = tpc;
	cpumask_clear(&threads_core_mask);

	/* This implementation only supports power of 2 number of threads
	 * for simplicity and performance
	 */
	threads_shift = ilog2(tpc);
	BUG_ON(tpc != (1 << threads_shift));

	for (i = 0; i < tpc; i++)
		cpumask_set_cpu(i, &threads_core_mask);

	printk(KERN_INFO "CPU maps initialized for %d thread%s per core\n",
	       tpc, tpc > 1 ? "s" : "");
	printk(KERN_DEBUG " (thread shift is %d)\n", threads_shift);
}


/**
 * setup_cpu_maps - initialize the following cpu maps:
 *                  cpu_possible_mask
 *                  cpu_present_mask
 *
 * Having the possible map set up early allows us to restrict allocations
 * of things like irqstacks to nr_cpu_ids rather than NR_CPUS.
 *
 * We do not initialize the online map here; cpus set their own bits in
 * cpu_online_mask as they come up.
 *
 * This function is valid only for Open Firmware systems.  finish_device_tree
 * must be called before using this.
 *
 * While we're here, we may as well set the "physical" cpu ids in the paca.
 *
 * NOTE: This must match the parsing done in early_init_dt_scan_cpus.
 */
void __init smp_setup_cpu_maps(void)
{
	struct device_node *dn = NULL;
	int cpu = 0;
	int nthreads = 1;

	DBG("smp_setup_cpu_maps()\n");

	while ((dn = of_find_node_by_type(dn, "cpu")) && cpu < nr_cpu_ids) {
		const __be32 *intserv;
		__be32 cpu_be;
		int j, len;

		DBG("  * %s...\n", dn->full_name);

		intserv = of_get_property(dn, "ibm,ppc-interrupt-server#s",
				&len);
		if (intserv) {
			DBG("    ibm,ppc-interrupt-server#s -> %d threads\n",
			    nthreads);
		} else {
			DBG("    no ibm,ppc-interrupt-server#s -> 1 thread\n");
			intserv = of_get_property(dn, "reg", &len);
			if (!intserv) {
				cpu_be = cpu_to_be32(cpu);
				intserv = &cpu_be;	/* assume logical == phys */
				len = 4;
			}
		}

		nthreads = len / sizeof(int);

		for (j = 0; j < nthreads && cpu < nr_cpu_ids; j++) {
			bool avail;

			DBG("    thread %d -> cpu %d (hard id %d)\n",
			    j, cpu, be32_to_cpu(intserv[j]));

			avail = of_device_is_available(dn);
			if (!avail)
				avail = !of_property_match_string(dn,
						"enable-method", "spin-table");

			set_cpu_present(cpu, avail);
			set_hard_smp_processor_id(cpu, be32_to_cpu(intserv[j]));
			set_cpu_possible(cpu, true);
			cpu++;
		}
	}

	/* If no SMT supported, nthreads is forced to 1 */
	if (!cpu_has_feature(CPU_FTR_SMT)) {
		DBG("  SMT disabled ! nthreads forced to 1\n");
		nthreads = 1;
	}

#ifdef CONFIG_PPC64
	/*
	 * On pSeries LPAR, we need to know how many cpus
	 * could possibly be added to this partition.
	 */
	if (machine_is(pseries) && firmware_has_feature(FW_FEATURE_LPAR) &&
	    (dn = of_find_node_by_path("/rtas"))) {
		int num_addr_cell, num_size_cell, maxcpus;
		const __be32 *ireg;

		num_addr_cell = of_n_addr_cells(dn);
		num_size_cell = of_n_size_cells(dn);

		ireg = of_get_property(dn, "ibm,lrdr-capacity", NULL);

		if (!ireg)
			goto out;

		maxcpus = be32_to_cpup(ireg + num_addr_cell + num_size_cell);

		/* Double maxcpus for processors which have SMT capability */
		if (cpu_has_feature(CPU_FTR_SMT))
			maxcpus *= nthreads;

		if (maxcpus > nr_cpu_ids) {
			printk(KERN_WARNING
			       "Partition configured for %d cpus, "
			       "operating system maximum is %d.\n",
			       maxcpus, nr_cpu_ids);
			maxcpus = nr_cpu_ids;
		} else
			printk(KERN_INFO "Partition configured for %d cpus.\n",
			       maxcpus);

		for (cpu = 0; cpu < maxcpus; cpu++)
			set_cpu_possible(cpu, true);
	out:
		of_node_put(dn);
	}
	vdso_data->processorCount = num_present_cpus();
#endif /* CONFIG_PPC64 */

        /* Initialize CPU <=> thread mapping/
	 *
	 * WARNING: We assume that the number of threads is the same for
	 * every CPU in the system. If that is not the case, then some code
	 * here will have to be reworked
	 */
	cpu_init_thread_core_maps(nthreads);

	/* Now that possible cpus are set, set nr_cpu_ids for later use */
	setup_nr_cpu_ids();

	free_unused_pacas();
}
#endif /* CONFIG_SMP */

#ifdef CONFIG_PCSPKR_PLATFORM
static __init int add_pcspkr(void)
{
	struct device_node *np;
	struct platform_device *pd;
	int ret;

	np = of_find_compatible_node(NULL, NULL, "pnpPNP,100");
	of_node_put(np);
	if (!np)
		return -ENODEV;

	pd = platform_device_alloc("pcspkr", -1);
	if (!pd)
		return -ENOMEM;

	ret = platform_device_add(pd);
	if (ret)
		platform_device_put(pd);

	return ret;
}
device_initcall(add_pcspkr);
#endif	/* CONFIG_PCSPKR_PLATFORM */

void probe_machine(void)
{
	extern struct machdep_calls __machine_desc_start;
	extern struct machdep_calls __machine_desc_end;

	/*
	 * Iterate all ppc_md structures until we find the proper
	 * one for the current machine type
	 */
	DBG("Probing machine type ...\n");

	for (machine_id = &__machine_desc_start;
	     machine_id < &__machine_desc_end;
	     machine_id++) {
		DBG("  %s ...", machine_id->name);
		memcpy(&ppc_md, machine_id, sizeof(struct machdep_calls));
		if (ppc_md.probe()) {
			DBG(" match !\n");
			break;
		}
		DBG("\n");
	}
	/* What can we do if we didn't find ? */
	if (machine_id >= &__machine_desc_end) {
		DBG("No suitable machine found !\n");
		for (;;);
	}

	printk(KERN_INFO "Using %s machine description\n", ppc_md.name);
}

/* Match a class of boards, not a specific device configuration. */
int check_legacy_ioport(unsigned long base_port)
{
	struct device_node *parent, *np = NULL;
	int ret = -ENODEV;

	switch(base_port) {
	case I8042_DATA_REG:
		if (!(np = of_find_compatible_node(NULL, NULL, "pnpPNP,303")))
			np = of_find_compatible_node(NULL, NULL, "pnpPNP,f03");
		if (np) {
			parent = of_get_parent(np);

			of_i8042_kbd_irq = irq_of_parse_and_map(parent, 0);
			if (!of_i8042_kbd_irq)
				of_i8042_kbd_irq = 1;

			of_i8042_aux_irq = irq_of_parse_and_map(parent, 1);
			if (!of_i8042_aux_irq)
				of_i8042_aux_irq = 12;

			of_node_put(np);
			np = parent;
			break;
		}
		np = of_find_node_by_type(NULL, "8042");
		/* Pegasos has no device_type on its 8042 node, look for the
		 * name instead */
		if (!np)
			np = of_find_node_by_name(NULL, "8042");
		if (np) {
			of_i8042_kbd_irq = 1;
			of_i8042_aux_irq = 12;
		}
		break;
	case FDC_BASE: /* FDC1 */
		np = of_find_node_by_type(NULL, "fdc");
		break;
	default:
		/* ipmi is supposed to fail here */
		break;
	}
	if (!np)
		return ret;
	parent = of_get_parent(np);
	if (parent) {
		if (strcmp(parent->type, "isa") == 0)
			ret = 0;
		of_node_put(parent);
	}
	of_node_put(np);
	return ret;
}
EXPORT_SYMBOL(check_legacy_ioport);

static int ppc_panic_event(struct notifier_block *this,
                             unsigned long event, void *ptr)
{
	/*
	 * If firmware-assisted dump has been registered then trigger
	 * firmware-assisted dump and let firmware handle everything else.
	 */
	crash_fadump(NULL, ptr);
	ppc_md.panic(ptr);  /* May not return */
	return NOTIFY_DONE;
}

static struct notifier_block ppc_panic_block = {
	.notifier_call = ppc_panic_event,
	.priority = INT_MIN /* may not return; must be done last */
};

void __init setup_panic(void)
{
	atomic_notifier_chain_register(&panic_notifier_list, &ppc_panic_block);
}

#ifdef CONFIG_CHECK_CACHE_COHERENCY
/*
 * For platforms that have configurable cache-coherency.  This function
 * checks that the cache coherency setting of the kernel matches the setting
 * left by the firmware, as indicated in the device tree.  Since a mismatch
 * will eventually result in DMA failures, we print * and error and call
 * BUG() in that case.
 */

#ifdef CONFIG_NOT_COHERENT_CACHE
#define KERNEL_COHERENCY	0
#else
#define KERNEL_COHERENCY	1
#endif

static int __init check_cache_coherency(void)
{
	struct device_node *np;
	const void *prop;
	int devtree_coherency;

	np = of_find_node_by_path("/");
	prop = of_get_property(np, "coherency-off", NULL);
	of_node_put(np);

	devtree_coherency = prop ? 0 : 1;

	if (devtree_coherency != KERNEL_COHERENCY) {
		printk(KERN_ERR
			"kernel coherency:%s != device tree_coherency:%s\n",
			KERNEL_COHERENCY ? "on" : "off",
			devtree_coherency ? "on" : "off");
		BUG();
	}

	return 0;
}

late_initcall(check_cache_coherency);
#endif /* CONFIG_CHECK_CACHE_COHERENCY */

#ifdef CONFIG_DEBUG_FS
struct dentry *powerpc_debugfs_root;
EXPORT_SYMBOL(powerpc_debugfs_root);

static int powerpc_debugfs_init(void)
{
	powerpc_debugfs_root = debugfs_create_dir("powerpc", NULL);

	return powerpc_debugfs_root == NULL;
}
arch_initcall(powerpc_debugfs_init);
#endif

void ppc_printk_progress(char *s, unsigned short hex)
{
	pr_info("%s\n", s);
}

void arch_setup_pdev_archdata(struct platform_device *pdev)
{
	pdev->archdata.dma_mask = DMA_BIT_MASK(32);
	pdev->dev.dma_mask = &pdev->archdata.dma_mask;
 	set_dma_ops(&pdev->dev, &dma_direct_ops);
}

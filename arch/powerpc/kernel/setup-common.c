// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Common boot and setup code for both 32-bit and 64-bit.
 * Extracted from arch/powerpc/kernel/setup_64.c.
 *
 * Copyright (C) 2001 PPC64 Team, IBM Corp
 */

#undef DEBUG

#include <linux/export.h>
#include <linux/panic_notifier.h>
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
#include <linux/percpu.h>
#include <linux/memblock.h>
#include <linux/of_platform.h>
#include <linux/hugetlb.h>
#include <linux/pgtable.h>
#include <asm/io.h>
#include <asm/paca.h>
#include <asm/prom.h>
#include <asm/processor.h>
#include <asm/vdso_datapage.h>
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
#include <asm/udbg.h>
#include <asm/hugetlb.h>
#include <asm/livepatch.h>
#include <asm/mmu_context.h>
#include <asm/cpu_has_feature.h>
#include <asm/kasan.h>
#include <asm/mce.h>

#include "setup.h"

#ifdef DEBUG
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

/*
 * These are used in binfmt_elf.c to put aux entries on the stack
 * for each elf executable being started.
 */
int dcache_bsize;
int icache_bsize;

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

#ifdef CONFIG_CRASH_CORE
/* This keeps a track of which one is the crashing cpu. */
int crashing_cpu = -1;
#endif

/* also used by kexec */
void machine_shutdown(void)
{
	/*
	 * if fadump is active, cleanup the fadump registration before we
	 * shutdown.
	 */
	fadump_cleanup();

	if (ppc_md.machine_shutdown)
		ppc_md.machine_shutdown();
}

static void machine_hang(void)
{
	pr_emerg("System Halted, OK to turn off power\n");
	local_irq_disable();
	while (1)
		;
}

void machine_restart(char *cmd)
{
	machine_shutdown();
	if (ppc_md.restart)
		ppc_md.restart(cmd);

	smp_send_stop();

	do_kernel_restart(cmd);
	mdelay(1000);

	machine_hang();
}

void machine_power_off(void)
{
	machine_shutdown();
	if (pm_power_off)
		pm_power_off();

	smp_send_stop();
	machine_hang();
}
/* Used by the G5 thermal driver */
EXPORT_SYMBOL_GPL(machine_power_off);

void (*pm_power_off)(void);
EXPORT_SYMBOL_GPL(pm_power_off);

void machine_halt(void)
{
	machine_shutdown();
	if (ppc_md.halt)
		ppc_md.halt();

	smp_send_stop();
	machine_hang();
}

#ifdef CONFIG_SMP
DEFINE_PER_CPU(unsigned int, cpu_pvr);
#endif

static void show_cpuinfo_summary(struct seq_file *m)
{
	struct device_node *root;
	const char *model = NULL;
	unsigned long bogosum = 0;
	int i;

	if (IS_ENABLED(CONFIG_SMP) && IS_ENABLED(CONFIG_PPC32)) {
		for_each_online_cpu(i)
			bogosum += loops_per_jiffy;
		seq_printf(m, "total bogomips\t: %lu.%02lu\n",
			   bogosum / (500000 / HZ), bogosum / (5000 / HZ) % 100);
	}
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

	/* Display the amount of memory */
	if (IS_ENABLED(CONFIG_PPC32))
		seq_printf(m, "Memory\t\t: %d MB\n",
			   (unsigned int)(total_memory / (1024 * 1024)));
}

static int show_cpuinfo(struct seq_file *m, void *v)
{
	unsigned long cpu_id = (unsigned long)v - 1;
	unsigned int pvr;
	unsigned long proc_freq;
	unsigned short maj;
	unsigned short min;

#ifdef CONFIG_SMP
	pvr = per_cpu(cpu_pvr, cpu_id);
#else
	pvr = mfspr(SPRN_PVR);
#endif
	maj = (pvr >> 8) & 0xFF;
	min = pvr & 0xFF;

	seq_printf(m, "processor\t: %lu\ncpu\t\t: ", cpu_id);

	if (cur_cpu_spec->pvr_mask && cur_cpu_spec->cpu_name)
		seq_puts(m, cur_cpu_spec->cpu_name);
	else
		seq_printf(m, "unknown (%08x)", pvr);

	if (cpu_has_feature(CPU_FTR_ALTIVEC))
		seq_puts(m, ", altivec supported");

	seq_putc(m, '\n');

#ifdef CONFIG_TAU
	if (cpu_has_feature(CPU_FTR_TAU)) {
		if (IS_ENABLED(CONFIG_TAU_AVERAGE)) {
			/* more straightforward, but potentially misleading */
			seq_printf(m,  "temperature \t: %u C (uncalibrated)\n",
				   cpu_temp(cpu_id));
		} else {
			/* show the actual temp sensor range */
			u32 temp;
			temp = cpu_temp_both(cpu_id);
			seq_printf(m, "temperature \t: %u-%u C (uncalibrated)\n",
				   temp & 0xff, temp >> 16);
		}
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
			case 0x1008:	/* 740P/750P ?? */
				maj = ((pvr >> 8) & 0xFF) - 1;
				min = pvr & 0xFF;
				break;
			case 0x004e: /* POWER9 bits 12-15 give chip type */
			case 0x0080: /* POWER10 bit 12 gives SMT8/4 */
				maj = (pvr >> 8) & 0x0F;
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

	if (IS_ENABLED(CONFIG_PPC32))
		seq_printf(m, "bogomips\t: %lu.%02lu\n", loops_per_jiffy / (500000 / HZ),
			   (loops_per_jiffy / (5000 / HZ)) % 100);

	seq_putc(m, '\n');

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
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_cpuinfo,
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
		pr_info("Found initrd at 0x%lx:0x%lx\n", initrd_start, initrd_end);

	DBG(" <- check_for_initrd()\n");
#endif /* CONFIG_BLK_DEV_INITRD */
}

#ifdef CONFIG_SMP

int threads_per_core, threads_per_subcore, threads_shift __read_mostly;
cpumask_t threads_core_mask __read_mostly;
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


u32 *cpu_to_phys_id = NULL;

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
	struct device_node *dn;
	int cpu = 0;
	int nthreads = 1;

	DBG("smp_setup_cpu_maps()\n");

	cpu_to_phys_id = memblock_alloc(nr_cpu_ids * sizeof(u32),
					__alignof__(u32));
	if (!cpu_to_phys_id)
		panic("%s: Failed to allocate %zu bytes align=0x%zx\n",
		      __func__, nr_cpu_ids * sizeof(u32), __alignof__(u32));

	for_each_node_by_type(dn, "cpu") {
		const __be32 *intserv;
		__be32 cpu_be;
		int j, len;

		DBG("  * %pOF...\n", dn);

		intserv = of_get_property(dn, "ibm,ppc-interrupt-server#s",
				&len);
		if (intserv) {
			DBG("    ibm,ppc-interrupt-server#s -> %lu threads\n",
			    (len / sizeof(int)));
		} else {
			DBG("    no ibm,ppc-interrupt-server#s -> 1 thread\n");
			intserv = of_get_property(dn, "reg", &len);
			if (!intserv) {
				cpu_be = cpu_to_be32(cpu);
				/* XXX: what is this? uninitialized?? */
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
			set_cpu_possible(cpu, true);
			cpu_to_phys_id[cpu] = be32_to_cpu(intserv[j]);
			cpu++;
		}

		if (cpu >= nr_cpu_ids) {
			of_node_put(dn);
			break;
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
	if (firmware_has_feature(FW_FEATURE_LPAR) &&
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
			       "operating system maximum is %u.\n",
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

static __init void probe_machine(void)
{
	extern struct machdep_calls __machine_desc_start;
	extern struct machdep_calls __machine_desc_end;
	unsigned int i;

	/*
	 * Iterate all ppc_md structures until we find the proper
	 * one for the current machine type
	 */
	DBG("Probing machine type ...\n");

	/*
	 * Check ppc_md is empty, if not we have a bug, ie, we setup an
	 * entry before probe_machine() which will be overwritten
	 */
	for (i = 0; i < (sizeof(ppc_md) / sizeof(void *)); i++) {
		if (((void **)&ppc_md)[i]) {
			printk(KERN_ERR "Entry %d in ppc_md non empty before"
			       " machine probe !\n", i);
		}
	}

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
		pr_err("No suitable machine description found !\n");
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
		if (of_node_is_type(parent, "isa"))
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
	 * panic does a local_irq_disable, but we really
	 * want interrupts to be hard disabled.
	 */
	hard_irq_disable();

	/*
	 * If firmware-assisted dump has been registered then trigger
	 * firmware-assisted dump and let firmware handle everything else.
	 */
	crash_fadump(NULL, ptr);
	if (ppc_md.panic)
		ppc_md.panic(ptr);  /* May not return */
	return NOTIFY_DONE;
}

static struct notifier_block ppc_panic_block = {
	.notifier_call = ppc_panic_event,
	.priority = INT_MIN /* may not return; must be done last */
};

/*
 * Dump out kernel offset information on panic.
 */
static int dump_kernel_offset(struct notifier_block *self, unsigned long v,
			      void *p)
{
	pr_emerg("Kernel Offset: 0x%lx from 0x%lx\n",
		 kaslr_offset(), KERNELBASE);

	return 0;
}

static struct notifier_block kernel_offset_notifier = {
	.notifier_call = dump_kernel_offset
};

void __init setup_panic(void)
{
	if (IS_ENABLED(CONFIG_RANDOMIZE_BASE) && kaslr_offset() > 0)
		atomic_notifier_chain_register(&panic_notifier_list,
					       &kernel_offset_notifier);

	/* PPC64 always does a hard irq disable in its panic handler */
	if (!IS_ENABLED(CONFIG_PPC64) && !ppc_md.panic)
		return;
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

#define KERNEL_COHERENCY	(!IS_ENABLED(CONFIG_NOT_COHERENT_CACHE))

static int __init check_cache_coherency(void)
{
	struct device_node *np;
	const void *prop;
	bool devtree_coherency;

	np = of_find_node_by_path("/");
	prop = of_get_property(np, "coherency-off", NULL);
	of_node_put(np);

	devtree_coherency = prop ? false : true;

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

void ppc_printk_progress(char *s, unsigned short hex)
{
	pr_info("%s\n", s);
}

static __init void print_system_info(void)
{
	pr_info("-----------------------------------------------------\n");
	pr_info("phys_mem_size     = 0x%llx\n",
		(unsigned long long)memblock_phys_mem_size());

	pr_info("dcache_bsize      = 0x%x\n", dcache_bsize);
	pr_info("icache_bsize      = 0x%x\n", icache_bsize);

	pr_info("cpu_features      = 0x%016lx\n", cur_cpu_spec->cpu_features);
	pr_info("  possible        = 0x%016lx\n",
		(unsigned long)CPU_FTRS_POSSIBLE);
	pr_info("  always          = 0x%016lx\n",
		(unsigned long)CPU_FTRS_ALWAYS);
	pr_info("cpu_user_features = 0x%08x 0x%08x\n",
		cur_cpu_spec->cpu_user_features,
		cur_cpu_spec->cpu_user_features2);
	pr_info("mmu_features      = 0x%08x\n", cur_cpu_spec->mmu_features);
#ifdef CONFIG_PPC64
	pr_info("firmware_features = 0x%016lx\n", powerpc_firmware_features);
#ifdef CONFIG_PPC_BOOK3S
	pr_info("vmalloc start     = 0x%lx\n", KERN_VIRT_START);
	pr_info("IO start          = 0x%lx\n", KERN_IO_START);
	pr_info("vmemmap start     = 0x%lx\n", (unsigned long)vmemmap);
#endif
#endif

	if (!early_radix_enabled())
		print_system_hash_info();

	if (PHYSICAL_START > 0)
		pr_info("physical_start    = 0x%llx\n",
		       (unsigned long long)PHYSICAL_START);
	pr_info("-----------------------------------------------------\n");
}

#ifdef CONFIG_SMP
static void __init smp_setup_pacas(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		if (cpu == smp_processor_id())
			continue;
		allocate_paca(cpu);
		set_hard_smp_processor_id(cpu, cpu_to_phys_id[cpu]);
	}

	memblock_free(cpu_to_phys_id, nr_cpu_ids * sizeof(u32));
	cpu_to_phys_id = NULL;
}
#endif

/*
 * Called into from start_kernel this initializes memblock, which is used
 * to manage page allocation until mem_init is called.
 */
void __init setup_arch(char **cmdline_p)
{
	kasan_init();

	*cmdline_p = boot_command_line;

	/* Set a half-reasonable default so udelay does something sensible */
	loops_per_jiffy = 500000000 / HZ;

	/* Unflatten the device-tree passed by prom_init or kexec */
	unflatten_device_tree();

	/*
	 * Initialize cache line/block info from device-tree (on ppc64) or
	 * just cputable (on ppc32).
	 */
	initialize_cache_info();

	/* Initialize RTAS if available. */
	rtas_initialize();

	/* Check if we have an initrd provided via the device-tree. */
	check_for_initrd();

	/* Probe the machine type, establish ppc_md. */
	probe_machine();

	/* Setup panic notifier if requested by the platform. */
	setup_panic();

	/*
	 * Configure ppc_md.power_save (ppc32 only, 64-bit machines do
	 * it from their respective probe() function.
	 */
	setup_power_save();

	/* Discover standard serial ports. */
	find_legacy_serial_ports();

	/* Register early console with the printk subsystem. */
	register_early_udbg_console();

	/* Setup the various CPU maps based on the device-tree. */
	smp_setup_cpu_maps();

	/* Initialize xmon. */
	xmon_setup();

	/* Check the SMT related command line arguments (ppc64). */
	check_smt_enabled();

	/* Parse memory topology */
	mem_topology_setup();

	/*
	 * Release secondary cpus out of their spinloops at 0x60 now that
	 * we can map physical -> logical CPU ids.
	 *
	 * Freescale Book3e parts spin in a loop provided by firmware,
	 * so smp_release_cpus() does nothing for them.
	 */
#ifdef CONFIG_SMP
	smp_setup_pacas();

	/* On BookE, setup per-core TLB data structures. */
	setup_tlb_core_data();
#endif

	/* Print various info about the machine that has been gathered so far. */
	print_system_info();

	/* Reserve large chunks of memory for use by CMA for KVM. */
	kvm_cma_reserve();

	/*  Reserve large chunks of memory for us by CMA for hugetlb */
	gigantic_hugetlb_cma_reserve();

	klp_init_thread_info(&init_task);

	setup_initial_init_mm(_stext, _etext, _edata, _end);

	mm_iommu_init(&init_mm);
	irqstack_early_init();
	exc_lvl_early_init();
	emergency_stack_init();

	mce_init();
	smp_release_cpus();

	initmem_init();

	early_memtest(min_low_pfn << PAGE_SHIFT, max_low_pfn << PAGE_SHIFT);

	if (ppc_md.setup_arch)
		ppc_md.setup_arch();

	setup_barrier_nospec();
	setup_spectre_v2();

	paging_init();

	/* Initialize the MMU context management stuff. */
	mmu_context_init();

	/* Interrupt code needs to be 64K-aligned. */
	if (IS_ENABLED(CONFIG_PPC64) && (unsigned long)_stext & 0xffff)
		panic("Kernelbase not 64K-aligned (0x%lx)!\n",
		      (unsigned long)_stext);
}

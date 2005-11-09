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
#include <asm/smp.h>
#include <asm/elf.h>
#include <asm/machdep.h>
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

#undef DEBUG

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

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

#ifdef __DO_IRQ_CANON
/* XXX should go elsewhere eventually */
int ppc_do_canonicalize_irqs;
EXPORT_SYMBOL(ppc_do_canonicalize_irqs);
#endif

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

void (*pm_power_off)(void) = machine_power_off;
EXPORT_SYMBOL_GPL(pm_power_off);

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


#ifdef CONFIG_TAU
extern u32 cpu_temp(unsigned long cpu);
extern u32 cpu_temp_both(unsigned long cpu);
#endif /* CONFIG_TAU */

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
#if defined(CONFIG_SMP) && defined(CONFIG_PPC32)
		unsigned long bogosum = 0;
		int i;
		for (i = 0; i < NR_CPUS; ++i)
			if (cpu_online(i))
				bogosum += loops_per_jiffy;
		seq_printf(m, "total bogomips\t: %lu.%02lu\n",
			   bogosum/(500000/HZ), bogosum/(5000/HZ) % 100);
#endif /* CONFIG_SMP && CONFIG_PPC32 */
		seq_printf(m, "timebase\t: %lu\n", ppc_tb_freq);

		if (ppc_md.show_cpuinfo != NULL)
			ppc_md.show_cpuinfo(m);

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
	 * Assume here that all clock rates are the same in a
	 * smp system.  -- Cort
	 */
	if (ppc_proc_freq)
		seq_printf(m, "clock\t\t: %lu.%06luMHz\n",
			   ppc_proc_freq / 1000000, ppc_proc_freq % 1000000);

	if (ppc_md.show_percpuinfo != NULL)
		ppc_md.show_percpuinfo(m, cpu_id);

	/* If we are a Freescale core do a simple check so
	 * we dont have to keep adding cases in the future */
	if (PVR_VER(pvr) & 0x8000) {
		maj = PVR_MAJ(pvr);
		min = PVR_MIN(pvr);
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
	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	unsigned long i = *pos;

	return i <= NR_CPUS ? (void *)(i + 1) : NULL;
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

void __init check_for_initrd(void)
{
#ifdef CONFIG_BLK_DEV_INITRD
	unsigned long *prop;

	DBG(" -> check_for_initrd()\n");

	if (of_chosen) {
		prop = (unsigned long *)get_property(of_chosen,
				"linux,initrd-start", NULL);
		if (prop != NULL) {
			initrd_start = (unsigned long)__va(*prop);
			prop = (unsigned long *)get_property(of_chosen,
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
	else {
		printk("Bogus initrd %08lx %08lx\n", initrd_start, initrd_end);
		initrd_start = initrd_end = 0;
	}

	if (initrd_start)
		printk("Found initrd at 0x%lx:0x%lx\n", initrd_start, initrd_end);

	DBG(" <- check_for_initrd()\n");
#endif /* CONFIG_BLK_DEV_INITRD */
}

#ifdef CONFIG_SMP

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
void __init smp_setup_cpu_maps(void)
{
	struct device_node *dn = NULL;
	int cpu = 0;
	int swap_cpuid = 0;

	while ((dn = of_find_node_by_type(dn, "cpu")) && cpu < NR_CPUS) {
		int *intserv;
		int j, len = sizeof(u32), nthreads = 1;

		intserv = (int *)get_property(dn, "ibm,ppc-interrupt-server#s",
					      &len);
		if (intserv)
			nthreads = len / sizeof(int);
		else {
			intserv = (int *) get_property(dn, "reg", NULL);
			if (!intserv)
				intserv = &cpu;	/* assume logical == phys */
		}

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

#ifdef CONFIG_PPC64
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
#endif /* CONFIG_PPC64 */
}
#endif /* CONFIG_SMP */

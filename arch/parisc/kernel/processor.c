/*    $Id: processor.c,v 1.1 2002/07/20 16:27:06 rhirst Exp $
 *
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
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/cpu.h>

#include <asm/cache.h>
#include <asm/hardware.h>	/* for register_parisc_driver() stuff */
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/pdc.h>
#include <asm/pdcpat.h>
#include <asm/irq.h>		/* for struct irq_region */
#include <asm/parisc-device.h>

struct system_cpuinfo_parisc boot_cpu_data __read_mostly;
EXPORT_SYMBOL(boot_cpu_data);

struct cpuinfo_parisc cpu_data[NR_CPUS] __read_mostly;

/*
**  	PARISC CPU driver - claim "device" and initialize CPU data structures.
**
** Consolidate per CPU initialization into (mostly) one module.
** Monarch CPU will initialize boot_cpu_data which shouldn't
** change once the system has booted.
**
** The callback *should* do per-instance initialization of
** everything including the monarch. "Per CPU" init code in
** setup.c:start_parisc() has migrated here and start_parisc()
** will call register_parisc_driver(&cpu_driver) before calling do_inventory().
**
** The goal of consolidating CPU initialization into one place is
** to make sure all CPU's get initialized the same way.
** The code path not shared is how PDC hands control of the CPU to the OS.
** The initialization of OS data structures is the same (done below).
*/

/**
 * processor_probe - Determine if processor driver should claim this device.
 * @dev: The device which has been found.
 *
 * Determine if processor driver should claim this chip (return 0) or not 
 * (return 1).  If so, initialize the chip and tell other partners in crime 
 * they have work to do.
 */
static int __init processor_probe(struct parisc_device *dev)
{
	unsigned long txn_addr;
	unsigned long cpuid;
	struct cpuinfo_parisc *p;

#ifndef CONFIG_SMP
	if (boot_cpu_data.cpu_count > 0) {
		printk(KERN_INFO "CONFIG_SMP=n  ignoring additional CPUs\n");
		return 1;
	}
#endif

	/* logical CPU ID and update global counter
	 * May get overwritten by PAT code.
	 */
	cpuid = boot_cpu_data.cpu_count;
	txn_addr = dev->hpa.start;	/* for legacy PDC */

#ifdef __LP64__
	if (is_pdc_pat()) {
		ulong status;
		unsigned long bytecnt;
	        pdc_pat_cell_mod_maddr_block_t pa_pdc_cell;
#undef USE_PAT_CPUID
#ifdef USE_PAT_CPUID
		struct pdc_pat_cpu_num cpu_info;
#endif

		status = pdc_pat_cell_module(&bytecnt, dev->pcell_loc,
			dev->mod_index, PA_VIEW, &pa_pdc_cell);

		BUG_ON(PDC_OK != status);

		/* verify it's the same as what do_pat_inventory() found */
		BUG_ON(dev->mod_info != pa_pdc_cell.mod_info);
		BUG_ON(dev->pmod_loc != pa_pdc_cell.mod_location);

		txn_addr = pa_pdc_cell.mod[0];   /* id_eid for IO sapic */

#ifdef USE_PAT_CPUID
/* We need contiguous numbers for cpuid. Firmware's notion
 * of cpuid is for physical CPUs and we just don't care yet.
 * We'll care when we need to query PAT PDC about a CPU *after*
 * boot time (ie shutdown a CPU from an OS perspective).
 */
		/* get the cpu number */
		status = pdc_pat_cpu_get_number(&cpu_info, dev->hpa.start);

		BUG_ON(PDC_OK != status);

		if (cpu_info.cpu_num >= NR_CPUS) {
			printk(KERN_WARNING "IGNORING CPU at 0x%x,"
				" cpu_slot_id > NR_CPUS"
				" (%ld > %d)\n",
				dev->hpa.start, cpu_info.cpu_num, NR_CPUS);
			/* Ignore CPU since it will only crash */
			boot_cpu_data.cpu_count--;
			return 1;
		} else {
			cpuid = cpu_info.cpu_num;
		}
#endif
	}
#endif

	p = &cpu_data[cpuid];
	boot_cpu_data.cpu_count++;

	/* initialize counters - CPU 0 gets it_value set in time_init() */
	if (cpuid)
		memset(p, 0, sizeof(struct cpuinfo_parisc));

	p->loops_per_jiffy = loops_per_jiffy;
	p->dev = dev;		/* Save IODC data in case we need it */
	p->hpa = dev->hpa.start;	/* save CPU hpa */
	p->cpuid = cpuid;	/* save CPU id */
	p->txn_addr = txn_addr;	/* save CPU IRQ address */
#ifdef CONFIG_SMP
	spin_lock_init(&p->lock);

	/*
	** FIXME: review if any other initialization is clobbered
	**	for boot_cpu by the above memset().
	*/

	/* stolen from init_percpu_prof() */
	cpu_data[cpuid].prof_counter = 1;
	cpu_data[cpuid].prof_multiplier = 1;
#endif

	/*
	** CONFIG_SMP: init_smp_config() will attempt to get CPU's into
	** OS control. RENDEZVOUS is the default state - see mem_set above.
	**	p->state = STATE_RENDEZVOUS;
	*/

#if 0
	/* CPU 0 IRQ table is statically allocated/initialized */
	if (cpuid) {
		struct irqaction actions[];

		/*
		** itimer and ipi IRQ handlers are statically initialized in
		** arch/parisc/kernel/irq.c. ie Don't need to register them.
		*/
		actions = kmalloc(sizeof(struct irqaction)*MAX_CPU_IRQ, GFP_ATOMIC);
		if (!actions) {
			/* not getting it's own table, share with monarch */
			actions = cpu_irq_actions[0];
		}

		cpu_irq_actions[cpuid] = actions;
	}
#endif

	/* 
	 * Bring this CPU up now! (ignore bootstrap cpuid == 0)
	 */
#ifdef CONFIG_SMP
	if (cpuid) {
		cpu_set(cpuid, cpu_present_map);
		cpu_up(cpuid);
	}
#endif

	return 0;
}

/**
 * collect_boot_cpu_data - Fill the boot_cpu_data structure.
 *
 * This function collects and stores the generic processor information
 * in the boot_cpu_data structure.
 */
void __init collect_boot_cpu_data(void)
{
	memset(&boot_cpu_data, 0, sizeof(boot_cpu_data));

	boot_cpu_data.cpu_hz = 100 * PAGE0->mem_10msec; /* Hz of this PARISC */

	/* get CPU-Model Information... */
#define p ((unsigned long *)&boot_cpu_data.pdc.model)
	if (pdc_model_info(&boot_cpu_data.pdc.model) == PDC_OK)
		printk(KERN_INFO 
			"model %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
			p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8]);
#undef p

	if (pdc_model_versions(&boot_cpu_data.pdc.versions, 0) == PDC_OK)
		printk(KERN_INFO "vers  %08lx\n", 
			boot_cpu_data.pdc.versions);

	if (pdc_model_cpuid(&boot_cpu_data.pdc.cpuid) == PDC_OK)
		printk(KERN_INFO "CPUID vers %ld rev %ld (0x%08lx)\n",
			(boot_cpu_data.pdc.cpuid >> 5) & 127,
			boot_cpu_data.pdc.cpuid & 31,
			boot_cpu_data.pdc.cpuid);

	if (pdc_model_capabilities(&boot_cpu_data.pdc.capabilities) == PDC_OK)
		printk(KERN_INFO "capabilities 0x%lx\n",
			boot_cpu_data.pdc.capabilities);

	if (pdc_model_sysmodel(boot_cpu_data.pdc.sys_model_name) == PDC_OK)
		printk(KERN_INFO "model %s\n",
			boot_cpu_data.pdc.sys_model_name);

	boot_cpu_data.hversion =  boot_cpu_data.pdc.model.hversion;
	boot_cpu_data.sversion =  boot_cpu_data.pdc.model.sversion;

	boot_cpu_data.cpu_type = parisc_get_cpu_type(boot_cpu_data.hversion);
	boot_cpu_data.cpu_name = cpu_name_version[boot_cpu_data.cpu_type][0];
	boot_cpu_data.family_name = cpu_name_version[boot_cpu_data.cpu_type][1];
}


/**
 * init_cpu_profiler - enable/setup per cpu profiling hooks.
 * @cpunum: The processor instance.
 *
 * FIXME: doesn't do much yet...
 */
static inline void __init
init_percpu_prof(int cpunum)
{
	cpu_data[cpunum].prof_counter = 1;
	cpu_data[cpunum].prof_multiplier = 1;
}


/**
 * init_per_cpu - Handle individual processor initializations.
 * @cpunum: logical processor number.
 *
 * This function handles initialization for *every* CPU
 * in the system:
 *
 * o Set "default" CPU width for trap handlers
 *
 * o Enable FP coprocessor
 *   REVISIT: this could be done in the "code 22" trap handler.
 *	(frowands idea - that way we know which processes need FP
 *	registers saved on the interrupt stack.)
 *   NEWS FLASH: wide kernels need FP coprocessor enabled to handle
 *	formatted printing of %lx for example (double divides I think)
 *
 * o Enable CPU profiling hooks.
 */
int __init init_per_cpu(int cpunum)
{
	int ret;
	struct pdc_coproc_cfg coproc_cfg;

	set_firmware_width();
	ret = pdc_coproc_cfg(&coproc_cfg);

	if(ret >= 0 && coproc_cfg.ccr_functional) {
		mtctl(coproc_cfg.ccr_functional, 10);  /* 10 == Coprocessor Control Reg */

		/* FWIW, FP rev/model is a more accurate way to determine
		** CPU type. CPU rev/model has some ambiguous cases.
		*/
		cpu_data[cpunum].fp_rev = coproc_cfg.revision;
		cpu_data[cpunum].fp_model = coproc_cfg.model;

		printk(KERN_INFO  "FP[%d] enabled: Rev %ld Model %ld\n",
			cpunum, coproc_cfg.revision, coproc_cfg.model);

		/*
		** store status register to stack (hopefully aligned)
		** and clear the T-bit.
		*/
		asm volatile ("fstd    %fr0,8(%sp)");

	} else {
		printk(KERN_WARNING  "WARNING: No FP CoProcessor?!"
			" (coproc_cfg.ccr_functional == 0x%lx, expected 0xc0)\n"
#ifdef __LP64__
			"Halting Machine - FP required\n"
#endif
			, coproc_cfg.ccr_functional);
#ifdef __LP64__
		mdelay(100);	/* previous chars get pushed to console */
		panic("FP CoProc not reported");
#endif
	}

	/* FUTURE: Enable Performance Monitor : ccr bit 0x20 */
	init_percpu_prof(cpunum);

	return ret;
}

/*
 * Display cpu info for all cpu's.
 */
int
show_cpuinfo (struct seq_file *m, void *v)
{
	int	n;

	for(n=0; n<boot_cpu_data.cpu_count; n++) {
#ifdef CONFIG_SMP
		if (0 == cpu_data[n].hpa)
			continue;
#ifdef ENTRY_SYS_CPUS
#error iCOD support wants to show CPU state here
#endif
#endif
		seq_printf(m, "processor\t: %d\n"
				"cpu family\t: PA-RISC %s\n",
				 n, boot_cpu_data.family_name);

		seq_printf(m, "cpu\t\t: %s\n",  boot_cpu_data.cpu_name );

		/* cpu MHz */
		seq_printf(m, "cpu MHz\t\t: %d.%06d\n",
				 boot_cpu_data.cpu_hz / 1000000,
				 boot_cpu_data.cpu_hz % 1000000  );

		seq_printf(m, "model\t\t: %s\n"
				"model name\t: %s\n",
				 boot_cpu_data.pdc.sys_model_name,
				 cpu_data[n].dev ? 
				 cpu_data[n].dev->name : "Unknown" );

		seq_printf(m, "hversion\t: 0x%08x\n"
			        "sversion\t: 0x%08x\n",
				 boot_cpu_data.hversion,
				 boot_cpu_data.sversion );

		/* print cachesize info */
		show_cache_info(m);

		seq_printf(m, "bogomips\t: %lu.%02lu\n",
			     cpu_data[n].loops_per_jiffy / (500000 / HZ),
			     (cpu_data[n].loops_per_jiffy / (5000 / HZ)) % 100);

		seq_printf(m, "software id\t: %ld\n\n",
				boot_cpu_data.pdc.model.sw_id);
	}
	return 0;
}

static struct parisc_device_id processor_tbl[] __read_mostly = {
	{ HPHW_NPROC, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, SVERSION_ANY_ID },
	{ 0, }
};

static struct parisc_driver cpu_driver __read_mostly = {
	.name		= "CPU",
	.id_table	= processor_tbl,
	.probe		= processor_probe
};

/**
 * processor_init - Processor initalization procedure.
 *
 * Register this driver.
 */
void __init processor_init(void)
{
	register_parisc_driver(&cpu_driver);
}

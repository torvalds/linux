/*
 * SMP support for power macintosh.
 *
 * We support both the old "powersurge" SMP architecture
 * and the current Core99 (G4 PowerMac) machines.
 *
 * Note that we don't support the very first rev. of
 * Apple/DayStar 2 CPUs board, the one with the funky
 * watchdog. Hopefully, none of these should be there except
 * maybe internally to Apple. I should probably still add some
 * code to detect this card though and disable SMP. --BenH.
 *
 * Support Macintosh G4 SMP by Troy Benjegerdes (hozer@drgw.net)
 * and Ben Herrenschmidt <benh@kernel.crashing.org>.
 *
 * Support for DayStar quad CPU cards
 * Copyright (C) XLR8, Inc. 1994-2000
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/hardirq.h>
#include <linux/cpu.h>
#include <linux/compiler.h>

#include <asm/ptrace.h>
#include <asm/atomic.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/sections.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/machdep.h>
#include <asm/pmac_feature.h>
#include <asm/time.h>
#include <asm/mpic.h>
#include <asm/cacheflush.h>
#include <asm/keylargo.h>
#include <asm/pmac_low_i2c.h>
#include <asm/pmac_pfunc.h>

#define DEBUG

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

extern void __secondary_start_pmac_0(void);
extern int pmac_pfunc_base_install(void);

#ifdef CONFIG_PPC32

/* Sync flag for HW tb sync */
static volatile int sec_tb_reset = 0;

/*
 * Powersurge (old powermac SMP) support.
 */

/* Addresses for powersurge registers */
#define HAMMERHEAD_BASE		0xf8000000
#define HHEAD_CONFIG		0x90
#define HHEAD_SEC_INTR		0xc0

/* register for interrupting the primary processor on the powersurge */
/* N.B. this is actually the ethernet ROM! */
#define PSURGE_PRI_INTR		0xf3019000

/* register for storing the start address for the secondary processor */
/* N.B. this is the PCI config space address register for the 1st bridge */
#define PSURGE_START		0xf2800000

/* Daystar/XLR8 4-CPU card */
#define PSURGE_QUAD_REG_ADDR	0xf8800000

#define PSURGE_QUAD_IRQ_SET	0
#define PSURGE_QUAD_IRQ_CLR	1
#define PSURGE_QUAD_IRQ_PRIMARY	2
#define PSURGE_QUAD_CKSTOP_CTL	3
#define PSURGE_QUAD_PRIMARY_ARB	4
#define PSURGE_QUAD_BOARD_ID	6
#define PSURGE_QUAD_WHICH_CPU	7
#define PSURGE_QUAD_CKSTOP_RDBK	8
#define PSURGE_QUAD_RESET_CTL	11

#define PSURGE_QUAD_OUT(r, v)	(out_8(quad_base + ((r) << 4) + 4, (v)))
#define PSURGE_QUAD_IN(r)	(in_8(quad_base + ((r) << 4) + 4) & 0x0f)
#define PSURGE_QUAD_BIS(r, v)	(PSURGE_QUAD_OUT((r), PSURGE_QUAD_IN(r) | (v)))
#define PSURGE_QUAD_BIC(r, v)	(PSURGE_QUAD_OUT((r), PSURGE_QUAD_IN(r) & ~(v)))

/* virtual addresses for the above */
static volatile u8 __iomem *hhead_base;
static volatile u8 __iomem *quad_base;
static volatile u32 __iomem *psurge_pri_intr;
static volatile u8 __iomem *psurge_sec_intr;
static volatile u32 __iomem *psurge_start;

/* values for psurge_type */
#define PSURGE_NONE		-1
#define PSURGE_DUAL		0
#define PSURGE_QUAD_OKEE	1
#define PSURGE_QUAD_COTTON	2
#define PSURGE_QUAD_ICEGRASS	3

/* what sort of powersurge board we have */
static int psurge_type = PSURGE_NONE;

/*
 * Set and clear IPIs for powersurge.
 */
static inline void psurge_set_ipi(int cpu)
{
	if (psurge_type == PSURGE_NONE)
		return;
	if (cpu == 0)
		in_be32(psurge_pri_intr);
	else if (psurge_type == PSURGE_DUAL)
		out_8(psurge_sec_intr, 0);
	else
		PSURGE_QUAD_OUT(PSURGE_QUAD_IRQ_SET, 1 << cpu);
}

static inline void psurge_clr_ipi(int cpu)
{
	if (cpu > 0) {
		switch(psurge_type) {
		case PSURGE_DUAL:
			out_8(psurge_sec_intr, ~0);
		case PSURGE_NONE:
			break;
		default:
			PSURGE_QUAD_OUT(PSURGE_QUAD_IRQ_CLR, 1 << cpu);
		}
	}
}

/*
 * On powersurge (old SMP powermac architecture) we don't have
 * separate IPIs for separate messages like openpic does.  Instead
 * we have a bitmap for each processor, where a 1 bit means that
 * the corresponding message is pending for that processor.
 * Ideally each cpu's entry would be in a different cache line.
 *  -- paulus.
 */
static unsigned long psurge_smp_message[NR_CPUS];

void psurge_smp_message_recv(void)
{
	int cpu = smp_processor_id();
	int msg;

	/* clear interrupt */
	psurge_clr_ipi(cpu);

	if (num_online_cpus() < 2)
		return;

	/* make sure there is a message there */
	for (msg = 0; msg < 4; msg++)
		if (test_and_clear_bit(msg, &psurge_smp_message[cpu]))
			smp_message_recv(msg);
}

irqreturn_t psurge_primary_intr(int irq, void *d)
{
	psurge_smp_message_recv();
	return IRQ_HANDLED;
}

static void smp_psurge_message_pass(int target, int msg)
{
	int i;

	if (num_online_cpus() < 2)
		return;

	for_each_online_cpu(i) {
		if (target == MSG_ALL
		    || (target == MSG_ALL_BUT_SELF && i != smp_processor_id())
		    || target == i) {
			set_bit(msg, &psurge_smp_message[i]);
			psurge_set_ipi(i);
		}
	}
}

/*
 * Determine a quad card presence. We read the board ID register, we
 * force the data bus to change to something else, and we read it again.
 * It it's stable, then the register probably exist (ugh !)
 */
static int __init psurge_quad_probe(void)
{
	int type;
	unsigned int i;

	type = PSURGE_QUAD_IN(PSURGE_QUAD_BOARD_ID);
	if (type < PSURGE_QUAD_OKEE || type > PSURGE_QUAD_ICEGRASS
	    || type != PSURGE_QUAD_IN(PSURGE_QUAD_BOARD_ID))
		return PSURGE_DUAL;

	/* looks OK, try a slightly more rigorous test */
	/* bogus is not necessarily cacheline-aligned,
	   though I don't suppose that really matters.  -- paulus */
	for (i = 0; i < 100; i++) {
		volatile u32 bogus[8];
		bogus[(0+i)%8] = 0x00000000;
		bogus[(1+i)%8] = 0x55555555;
		bogus[(2+i)%8] = 0xFFFFFFFF;
		bogus[(3+i)%8] = 0xAAAAAAAA;
		bogus[(4+i)%8] = 0x33333333;
		bogus[(5+i)%8] = 0xCCCCCCCC;
		bogus[(6+i)%8] = 0xCCCCCCCC;
		bogus[(7+i)%8] = 0x33333333;
		wmb();
		asm volatile("dcbf 0,%0" : : "r" (bogus) : "memory");
		mb();
		if (type != PSURGE_QUAD_IN(PSURGE_QUAD_BOARD_ID))
			return PSURGE_DUAL;
	}
	return type;
}

static void __init psurge_quad_init(void)
{
	int procbits;

	if (ppc_md.progress) ppc_md.progress("psurge_quad_init", 0x351);
	procbits = ~PSURGE_QUAD_IN(PSURGE_QUAD_WHICH_CPU);
	if (psurge_type == PSURGE_QUAD_ICEGRASS)
		PSURGE_QUAD_BIS(PSURGE_QUAD_RESET_CTL, procbits);
	else
		PSURGE_QUAD_BIC(PSURGE_QUAD_CKSTOP_CTL, procbits);
	mdelay(33);
	out_8(psurge_sec_intr, ~0);
	PSURGE_QUAD_OUT(PSURGE_QUAD_IRQ_CLR, procbits);
	PSURGE_QUAD_BIS(PSURGE_QUAD_RESET_CTL, procbits);
	if (psurge_type != PSURGE_QUAD_ICEGRASS)
		PSURGE_QUAD_BIS(PSURGE_QUAD_CKSTOP_CTL, procbits);
	PSURGE_QUAD_BIC(PSURGE_QUAD_PRIMARY_ARB, procbits);
	mdelay(33);
	PSURGE_QUAD_BIC(PSURGE_QUAD_RESET_CTL, procbits);
	mdelay(33);
	PSURGE_QUAD_BIS(PSURGE_QUAD_PRIMARY_ARB, procbits);
	mdelay(33);
}

static int __init smp_psurge_probe(void)
{
	int i, ncpus;
	struct device_node *dn;

	/* We don't do SMP on the PPC601 -- paulus */
	if (PVR_VER(mfspr(SPRN_PVR)) == 1)
		return 1;

	/*
	 * The powersurge cpu board can be used in the generation
	 * of powermacs that have a socket for an upgradeable cpu card,
	 * including the 7500, 8500, 9500, 9600.
	 * The device tree doesn't tell you if you have 2 cpus because
	 * OF doesn't know anything about the 2nd processor.
	 * Instead we look for magic bits in magic registers,
	 * in the hammerhead memory controller in the case of the
	 * dual-cpu powersurge board.  -- paulus.
	 */
	dn = of_find_node_by_name(NULL, "hammerhead");
	if (dn == NULL)
		return 1;
	of_node_put(dn);

	hhead_base = ioremap(HAMMERHEAD_BASE, 0x800);
	quad_base = ioremap(PSURGE_QUAD_REG_ADDR, 1024);
	psurge_sec_intr = hhead_base + HHEAD_SEC_INTR;

	psurge_type = psurge_quad_probe();
	if (psurge_type != PSURGE_DUAL) {
		psurge_quad_init();
		/* All released cards using this HW design have 4 CPUs */
		ncpus = 4;
	} else {
		iounmap(quad_base);
		if ((in_8(hhead_base + HHEAD_CONFIG) & 0x02) == 0) {
			/* not a dual-cpu card */
			iounmap(hhead_base);
			psurge_type = PSURGE_NONE;
			return 1;
		}
		ncpus = 2;
	}

	psurge_start = ioremap(PSURGE_START, 4);
	psurge_pri_intr = ioremap(PSURGE_PRI_INTR, 4);

	/*
	 * This is necessary because OF doesn't know about the
	 * secondary cpu(s), and thus there aren't nodes in the
	 * device tree for them, and smp_setup_cpu_maps hasn't
	 * set their bits in cpu_possible_map and cpu_present_map.
	 */
	if (ncpus > NR_CPUS)
		ncpus = NR_CPUS;
	for (i = 1; i < ncpus ; ++i) {
		cpu_set(i, cpu_present_map);
		cpu_set(i, cpu_possible_map);
		set_hard_smp_processor_id(i, i);
	}

	if (ppc_md.progress) ppc_md.progress("smp_psurge_probe - done", 0x352);

	return ncpus;
}

static void __init smp_psurge_kick_cpu(int nr)
{
	unsigned long start = __pa(__secondary_start_pmac_0) + nr * 8;
	unsigned long a;
	int i;

	/* may need to flush here if secondary bats aren't setup */
	for (a = KERNELBASE; a < KERNELBASE + 0x800000; a += 32)
		asm volatile("dcbf 0,%0" : : "r" (a) : "memory");
	asm volatile("sync");

	if (ppc_md.progress) ppc_md.progress("smp_psurge_kick_cpu", 0x353);

	out_be32(psurge_start, start);
	mb();

	psurge_set_ipi(nr);
	/*
	 * We can't use udelay here because the timebase is now frozen.
	 */
	for (i = 0; i < 2000; ++i)
		barrier();
	psurge_clr_ipi(nr);

	if (ppc_md.progress) ppc_md.progress("smp_psurge_kick_cpu - done", 0x354);
}

/*
 * With the dual-cpu powersurge board, the decrementers and timebases
 * of both cpus are frozen after the secondary cpu is started up,
 * until we give the secondary cpu another interrupt.  This routine
 * uses this to get the timebases synchronized.
 *  -- paulus.
 */
static void __init psurge_dual_sync_tb(int cpu_nr)
{
	int t;

	set_dec(tb_ticks_per_jiffy);
	/* XXX fixme */
	set_tb(0, 0);

	if (cpu_nr > 0) {
		mb();
		sec_tb_reset = 1;
		return;
	}

	/* wait for the secondary to have reset its TB before proceeding */
	for (t = 10000000; t > 0 && !sec_tb_reset; --t)
		;

	/* now interrupt the secondary, starting both TBs */
	psurge_set_ipi(1);
}

static struct irqaction psurge_irqaction = {
	.handler = psurge_primary_intr,
	.flags = IRQF_DISABLED,
	.mask = CPU_MASK_NONE,
	.name = "primary IPI",
};

static void __init smp_psurge_setup_cpu(int cpu_nr)
{

	if (cpu_nr == 0) {
		/* If we failed to start the second CPU, we should still
		 * send it an IPI to start the timebase & DEC or we might
		 * have them stuck.
		 */
		if (num_online_cpus() < 2) {
			if (psurge_type == PSURGE_DUAL)
				psurge_set_ipi(1);
			return;
		}
		/* reset the entry point so if we get another intr we won't
		 * try to startup again */
		out_be32(psurge_start, 0x100);
		if (setup_irq(30, &psurge_irqaction))
			printk(KERN_ERR "Couldn't get primary IPI interrupt");
	}

	if (psurge_type == PSURGE_DUAL)
		psurge_dual_sync_tb(cpu_nr);
}

void __init smp_psurge_take_timebase(void)
{
	/* Dummy implementation */
}

void __init smp_psurge_give_timebase(void)
{
	/* Dummy implementation */
}

/* PowerSurge-style Macs */
struct smp_ops_t psurge_smp_ops = {
	.message_pass	= smp_psurge_message_pass,
	.probe		= smp_psurge_probe,
	.kick_cpu	= smp_psurge_kick_cpu,
	.setup_cpu	= smp_psurge_setup_cpu,
	.give_timebase	= smp_psurge_give_timebase,
	.take_timebase	= smp_psurge_take_timebase,
};
#endif /* CONFIG_PPC32 - actually powersurge support */

/*
 * Core 99 and later support
 */

static void (*pmac_tb_freeze)(int freeze);
static u64 timebase;
static int tb_req;

static void smp_core99_give_timebase(void)
{
	unsigned long flags;

	local_irq_save(flags);

	while(!tb_req)
		barrier();
	tb_req = 0;
	(*pmac_tb_freeze)(1);
	mb();
	timebase = get_tb();
	mb();
	while (timebase)
		barrier();
	mb();
	(*pmac_tb_freeze)(0);
	mb();

	local_irq_restore(flags);
}


static void __devinit smp_core99_take_timebase(void)
{
	unsigned long flags;

	local_irq_save(flags);

	tb_req = 1;
	mb();
	while (!timebase)
		barrier();
	mb();
	set_tb(timebase >> 32, timebase & 0xffffffff);
	timebase = 0;
	mb();
	set_dec(tb_ticks_per_jiffy/2);

	local_irq_restore(flags);
}

#ifdef CONFIG_PPC64
/*
 * G5s enable/disable the timebase via an i2c-connected clock chip.
 */
static struct pmac_i2c_bus *pmac_tb_clock_chip_host;
static u8 pmac_tb_pulsar_addr;

static void smp_core99_cypress_tb_freeze(int freeze)
{
	u8 data;
	int rc;

	/* Strangely, the device-tree says address is 0xd2, but darwin
	 * accesses 0xd0 ...
	 */
	pmac_i2c_setmode(pmac_tb_clock_chip_host,
			 pmac_i2c_mode_combined);
	rc = pmac_i2c_xfer(pmac_tb_clock_chip_host,
			   0xd0 | pmac_i2c_read,
			   1, 0x81, &data, 1);
	if (rc != 0)
		goto bail;

	data = (data & 0xf3) | (freeze ? 0x00 : 0x0c);

       	pmac_i2c_setmode(pmac_tb_clock_chip_host, pmac_i2c_mode_stdsub);
	rc = pmac_i2c_xfer(pmac_tb_clock_chip_host,
			   0xd0 | pmac_i2c_write,
			   1, 0x81, &data, 1);

 bail:
	if (rc != 0) {
		printk("Cypress Timebase %s rc: %d\n",
		       freeze ? "freeze" : "unfreeze", rc);
		panic("Timebase freeze failed !\n");
	}
}


static void smp_core99_pulsar_tb_freeze(int freeze)
{
	u8 data;
	int rc;

	pmac_i2c_setmode(pmac_tb_clock_chip_host,
			 pmac_i2c_mode_combined);
	rc = pmac_i2c_xfer(pmac_tb_clock_chip_host,
			   pmac_tb_pulsar_addr | pmac_i2c_read,
			   1, 0x2e, &data, 1);
	if (rc != 0)
		goto bail;

	data = (data & 0x88) | (freeze ? 0x11 : 0x22);

	pmac_i2c_setmode(pmac_tb_clock_chip_host, pmac_i2c_mode_stdsub);
	rc = pmac_i2c_xfer(pmac_tb_clock_chip_host,
			   pmac_tb_pulsar_addr | pmac_i2c_write,
			   1, 0x2e, &data, 1);
 bail:
	if (rc != 0) {
		printk(KERN_ERR "Pulsar Timebase %s rc: %d\n",
		       freeze ? "freeze" : "unfreeze", rc);
		panic("Timebase freeze failed !\n");
	}
}

static void __init smp_core99_setup_i2c_hwsync(int ncpus)
{
	struct device_node *cc = NULL;	
	struct device_node *p;
	const char *name = NULL;
	const u32 *reg;
	int ok;

	/* Look for the clock chip */
	while ((cc = of_find_node_by_name(cc, "i2c-hwclock")) != NULL) {
		p = of_get_parent(cc);
		ok = p && of_device_is_compatible(p, "uni-n-i2c");
		of_node_put(p);
		if (!ok)
			continue;

		pmac_tb_clock_chip_host = pmac_i2c_find_bus(cc);
		if (pmac_tb_clock_chip_host == NULL)
			continue;
		reg = of_get_property(cc, "reg", NULL);
		if (reg == NULL)
			continue;
		switch (*reg) {
		case 0xd2:
			if (of_device_is_compatible(cc,"pulsar-legacy-slewing")) {
				pmac_tb_freeze = smp_core99_pulsar_tb_freeze;
				pmac_tb_pulsar_addr = 0xd2;
				name = "Pulsar";
			} else if (of_device_is_compatible(cc, "cy28508")) {
				pmac_tb_freeze = smp_core99_cypress_tb_freeze;
				name = "Cypress";
			}
			break;
		case 0xd4:
			pmac_tb_freeze = smp_core99_pulsar_tb_freeze;
			pmac_tb_pulsar_addr = 0xd4;
			name = "Pulsar";
			break;
		}
		if (pmac_tb_freeze != NULL)
			break;
	}
	if (pmac_tb_freeze != NULL) {
		/* Open i2c bus for synchronous access */
		if (pmac_i2c_open(pmac_tb_clock_chip_host, 1)) {
			printk(KERN_ERR "Failed top open i2c bus for clock"
			       " sync, fallback to software sync !\n");
			goto no_i2c_sync;
		}
		printk(KERN_INFO "Processor timebase sync using %s i2c clock\n",
		       name);
		return;
	}
 no_i2c_sync:
	pmac_tb_freeze = NULL;
	pmac_tb_clock_chip_host = NULL;
}



/*
 * Newer G5s uses a platform function
 */

static void smp_core99_pfunc_tb_freeze(int freeze)
{
	struct device_node *cpus;
	struct pmf_args args;

	cpus = of_find_node_by_path("/cpus");
	BUG_ON(cpus == NULL);
	args.count = 1;
	args.u[0].v = !freeze;
	pmf_call_function(cpus, "cpu-timebase", &args);
	of_node_put(cpus);
}

#else /* CONFIG_PPC64 */

/*
 * SMP G4 use a GPIO to enable/disable the timebase.
 */

static unsigned int core99_tb_gpio;	/* Timebase freeze GPIO */

static void smp_core99_gpio_tb_freeze(int freeze)
{
	if (freeze)
		pmac_call_feature(PMAC_FTR_WRITE_GPIO, NULL, core99_tb_gpio, 4);
	else
		pmac_call_feature(PMAC_FTR_WRITE_GPIO, NULL, core99_tb_gpio, 0);
	pmac_call_feature(PMAC_FTR_READ_GPIO, NULL, core99_tb_gpio, 0);
}


#endif /* !CONFIG_PPC64 */

/* L2 and L3 cache settings to pass from CPU0 to CPU1 on G4 cpus */
volatile static long int core99_l2_cache;
volatile static long int core99_l3_cache;

static void __devinit core99_init_caches(int cpu)
{
#ifndef CONFIG_PPC64
	if (!cpu_has_feature(CPU_FTR_L2CR))
		return;

	if (cpu == 0) {
		core99_l2_cache = _get_L2CR();
		printk("CPU0: L2CR is %lx\n", core99_l2_cache);
	} else {
		printk("CPU%d: L2CR was %lx\n", cpu, _get_L2CR());
		_set_L2CR(0);
		_set_L2CR(core99_l2_cache);
		printk("CPU%d: L2CR set to %lx\n", cpu, core99_l2_cache);
	}

	if (!cpu_has_feature(CPU_FTR_L3CR))
		return;

	if (cpu == 0){
		core99_l3_cache = _get_L3CR();
		printk("CPU0: L3CR is %lx\n", core99_l3_cache);
	} else {
		printk("CPU%d: L3CR was %lx\n", cpu, _get_L3CR());
		_set_L3CR(0);
		_set_L3CR(core99_l3_cache);
		printk("CPU%d: L3CR set to %lx\n", cpu, core99_l3_cache);
	}
#endif /* !CONFIG_PPC64 */
}

static void __init smp_core99_setup(int ncpus)
{
#ifdef CONFIG_PPC64

	/* i2c based HW sync on some G5s */
	if (machine_is_compatible("PowerMac7,2") ||
	    machine_is_compatible("PowerMac7,3") ||
	    machine_is_compatible("RackMac3,1"))
		smp_core99_setup_i2c_hwsync(ncpus);

	/* pfunc based HW sync on recent G5s */
	if (pmac_tb_freeze == NULL) {
		struct device_node *cpus =
			of_find_node_by_path("/cpus");
		if (cpus &&
		    of_get_property(cpus, "platform-cpu-timebase", NULL)) {
			pmac_tb_freeze = smp_core99_pfunc_tb_freeze;
			printk(KERN_INFO "Processor timebase sync using"
			       " platform function\n");
		}
	}

#else /* CONFIG_PPC64 */

	/* GPIO based HW sync on ppc32 Core99 */
	if (pmac_tb_freeze == NULL && !machine_is_compatible("MacRISC4")) {
		struct device_node *cpu;
		const u32 *tbprop = NULL;

		core99_tb_gpio = KL_GPIO_TB_ENABLE;	/* default value */
		cpu = of_find_node_by_type(NULL, "cpu");
		if (cpu != NULL) {
			tbprop = of_get_property(cpu, "timebase-enable", NULL);
			if (tbprop)
				core99_tb_gpio = *tbprop;
			of_node_put(cpu);
		}
		pmac_tb_freeze = smp_core99_gpio_tb_freeze;
		printk(KERN_INFO "Processor timebase sync using"
		       " GPIO 0x%02x\n", core99_tb_gpio);
	}

#endif /* CONFIG_PPC64 */

	/* No timebase sync, fallback to software */
	if (pmac_tb_freeze == NULL) {
		smp_ops->give_timebase = smp_generic_give_timebase;
		smp_ops->take_timebase = smp_generic_take_timebase;
		printk(KERN_INFO "Processor timebase sync using software\n");
	}

#ifndef CONFIG_PPC64
	{
		int i;

		/* XXX should get this from reg properties */
		for (i = 1; i < ncpus; ++i)
			smp_hw_index[i] = i;
	}
#endif

	/* 32 bits SMP can't NAP */
	if (!machine_is_compatible("MacRISC4"))
		powersave_nap = 0;
}

static int __init smp_core99_probe(void)
{
	struct device_node *cpus;
	int ncpus = 0;

	if (ppc_md.progress) ppc_md.progress("smp_core99_probe", 0x345);

	/* Count CPUs in the device-tree */
       	for (cpus = NULL; (cpus = of_find_node_by_type(cpus, "cpu")) != NULL;)
	       	++ncpus;

	printk(KERN_INFO "PowerMac SMP probe found %d cpus\n", ncpus);

	/* Nothing more to do if less than 2 of them */
	if (ncpus <= 1)
		return 1;

	/* We need to perform some early initialisations before we can start
	 * setting up SMP as we are running before initcalls
	 */
	pmac_pfunc_base_install();
	pmac_i2c_init();

	/* Setup various bits like timebase sync method, ability to nap, ... */
	smp_core99_setup(ncpus);

	/* Install IPIs */
	mpic_request_ipis();

	/* Collect l2cr and l3cr values from CPU 0 */
	core99_init_caches(0);

	return ncpus;
}

static void __devinit smp_core99_kick_cpu(int nr)
{
	unsigned int save_vector;
	unsigned long target, flags;
	volatile unsigned int *vector
		 = ((volatile unsigned int *)(KERNELBASE+0x100));

	if (nr < 0 || nr > 3)
		return;

	if (ppc_md.progress)
		ppc_md.progress("smp_core99_kick_cpu", 0x346);

	local_irq_save(flags);

	/* Save reset vector */
	save_vector = *vector;

	/* Setup fake reset vector that does
	 *   b __secondary_start_pmac_0 + nr*8 - KERNELBASE
	 */
	target = (unsigned long) __secondary_start_pmac_0 + nr * 8;
	create_branch((unsigned long)vector, target, BRANCH_SET_LINK);

	/* Put some life in our friend */
	pmac_call_feature(PMAC_FTR_RESET_CPU, NULL, nr, 0);

	/* FIXME: We wait a bit for the CPU to take the exception, I should
	 * instead wait for the entry code to set something for me. Well,
	 * ideally, all that crap will be done in prom.c and the CPU left
	 * in a RAM-based wait loop like CHRP.
	 */
	mdelay(1);

	/* Restore our exception vector */
	*vector = save_vector;
	flush_icache_range((unsigned long) vector, (unsigned long) vector + 4);

	local_irq_restore(flags);
	if (ppc_md.progress) ppc_md.progress("smp_core99_kick_cpu done", 0x347);
}

static void __devinit smp_core99_setup_cpu(int cpu_nr)
{
	/* Setup L2/L3 */
	if (cpu_nr != 0)
		core99_init_caches(cpu_nr);

	/* Setup openpic */
	mpic_setup_this_cpu();

	if (cpu_nr == 0) {
#ifdef CONFIG_PPC64
		extern void g5_phy_disable_cpu1(void);

		/* Close i2c bus if it was used for tb sync */
		if (pmac_tb_clock_chip_host) {
			pmac_i2c_close(pmac_tb_clock_chip_host);
			pmac_tb_clock_chip_host	= NULL;
		}

		/* If we didn't start the second CPU, we must take
		 * it off the bus
		 */
		if (machine_is_compatible("MacRISC4") &&
		    num_online_cpus() < 2)		
			g5_phy_disable_cpu1();
#endif /* CONFIG_PPC64 */

		if (ppc_md.progress)
			ppc_md.progress("core99_setup_cpu 0 done", 0x349);
	}
}


#if defined(CONFIG_HOTPLUG_CPU) && defined(CONFIG_PPC32)

int smp_core99_cpu_disable(void)
{
	cpu_clear(smp_processor_id(), cpu_online_map);

	/* XXX reset cpu affinity here */
	mpic_cpu_set_priority(0xf);
	asm volatile("mtdec %0" : : "r" (0x7fffffff));
	mb();
	udelay(20);
	asm volatile("mtdec %0" : : "r" (0x7fffffff));
	return 0;
}

extern void low_cpu_die(void) __attribute__((noreturn)); /* in sleep.S */
static int cpu_dead[NR_CPUS];

void cpu_die(void)
{
	local_irq_disable();
	cpu_dead[smp_processor_id()] = 1;
	mb();
	low_cpu_die();
}

void smp_core99_cpu_die(unsigned int cpu)
{
	int timeout;

	timeout = 1000;
	while (!cpu_dead[cpu]) {
		if (--timeout == 0) {
			printk("CPU %u refused to die!\n", cpu);
			break;
		}
		msleep(1);
	}
	cpu_dead[cpu] = 0;
}

#endif /* CONFIG_HOTPLUG_CPU && CONFIG_PP32 */

/* Core99 Macs (dual G4s and G5s) */
struct smp_ops_t core99_smp_ops = {
	.message_pass	= smp_mpic_message_pass,
	.probe		= smp_core99_probe,
	.kick_cpu	= smp_core99_kick_cpu,
	.setup_cpu	= smp_core99_setup_cpu,
	.give_timebase	= smp_core99_give_timebase,
	.take_timebase	= smp_core99_take_timebase,
#if defined(CONFIG_HOTPLUG_CPU)
# if defined(CONFIG_PPC32)
	.cpu_disable	= smp_core99_cpu_disable,
	.cpu_die	= smp_core99_cpu_die,
# endif
# if defined(CONFIG_PPC64)
	.cpu_disable	= generic_cpu_disable,
	.cpu_die	= generic_cpu_die,
	/* intentionally do *NOT* assign cpu_enable,
	 * the generic code will use kick_cpu then! */
# endif
#endif
};

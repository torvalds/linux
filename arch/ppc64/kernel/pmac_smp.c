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

#undef DEBUG

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/irq.h>

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
#include <asm/cacheflush.h>
#include <asm/keylargo.h>
#include <asm/pmac_low_i2c.h>

#include "mpic.h"

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

extern void pmac_secondary_start_1(void);
extern void pmac_secondary_start_2(void);
extern void pmac_secondary_start_3(void);

extern struct smp_ops_t *smp_ops;

static void (*pmac_tb_freeze)(int freeze);
static struct device_node *pmac_tb_clock_chip_host;
static u8 pmac_tb_pulsar_addr;
static DEFINE_SPINLOCK(timebase_lock);
static unsigned long timebase;

static void smp_core99_cypress_tb_freeze(int freeze)
{
	u8 data;
	int rc;

	/* Strangely, the device-tree says address is 0xd2, but darwin
	 * accesses 0xd0 ...
	 */
	pmac_low_i2c_setmode(pmac_tb_clock_chip_host, pmac_low_i2c_mode_combined);
	rc = pmac_low_i2c_xfer(pmac_tb_clock_chip_host,
			       0xd0 | pmac_low_i2c_read,
			       0x81, &data, 1);
	if (rc != 0)
		goto bail;

	data = (data & 0xf3) | (freeze ? 0x00 : 0x0c);

       	pmac_low_i2c_setmode(pmac_tb_clock_chip_host, pmac_low_i2c_mode_stdsub);
	rc = pmac_low_i2c_xfer(pmac_tb_clock_chip_host,
			       0xd0 | pmac_low_i2c_write,
			       0x81, &data, 1);

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

	pmac_low_i2c_setmode(pmac_tb_clock_chip_host, pmac_low_i2c_mode_combined);
	rc = pmac_low_i2c_xfer(pmac_tb_clock_chip_host,
			       pmac_tb_pulsar_addr | pmac_low_i2c_read,
			       0x2e, &data, 1);
	if (rc != 0)
		goto bail;

	data = (data & 0x88) | (freeze ? 0x11 : 0x22);

	pmac_low_i2c_setmode(pmac_tb_clock_chip_host, pmac_low_i2c_mode_stdsub);
	rc = pmac_low_i2c_xfer(pmac_tb_clock_chip_host,
			       pmac_tb_pulsar_addr | pmac_low_i2c_write,
			       0x2e, &data, 1);
 bail:
	if (rc != 0) {
		printk(KERN_ERR "Pulsar Timebase %s rc: %d\n",
		       freeze ? "freeze" : "unfreeze", rc);
		panic("Timebase freeze failed !\n");
	}
}


static void smp_core99_give_timebase(void)
{
	/* Open i2c bus for synchronous access */
	if (pmac_low_i2c_open(pmac_tb_clock_chip_host, 0))
		panic("Can't open i2c for TB sync !\n");

	spin_lock(&timebase_lock);
	(*pmac_tb_freeze)(1);
	mb();
	timebase = get_tb();
	spin_unlock(&timebase_lock);

	while (timebase)
		barrier();

	spin_lock(&timebase_lock);
	(*pmac_tb_freeze)(0);
	spin_unlock(&timebase_lock);

	/* Close i2c bus */
	pmac_low_i2c_close(pmac_tb_clock_chip_host);
}


static void __devinit smp_core99_take_timebase(void)
{
	while (!timebase)
		barrier();
	spin_lock(&timebase_lock);
	set_tb(timebase >> 32, timebase & 0xffffffff);
	timebase = 0;
	spin_unlock(&timebase_lock);
}


static int __init smp_core99_probe(void)
{
	struct device_node *cpus;	
	struct device_node *cc;	
	int ncpus = 0;

	/* Maybe use systemconfiguration here ? */
	if (ppc_md.progress) ppc_md.progress("smp_core99_probe", 0x345);

	/* Count CPUs in the device-tree */
       	for (cpus = NULL; (cpus = of_find_node_by_type(cpus, "cpu")) != NULL;)
	       	++ncpus;

	printk(KERN_INFO "PowerMac SMP probe found %d cpus\n", ncpus);

	/* Nothing more to do if less than 2 of them */
	if (ncpus <= 1)
		return 1;

	/* HW sync only on these platforms */
	if (!machine_is_compatible("PowerMac7,2") &&
	    !machine_is_compatible("PowerMac7,3") &&
	    !machine_is_compatible("RackMac3,1"))
		goto nohwsync;

	/* Look for the clock chip */
	for (cc = NULL; (cc = of_find_node_by_name(cc, "i2c-hwclock")) != NULL;) {
		struct device_node *p = of_get_parent(cc);
		u32 *reg;
		int ok;
		ok = p && device_is_compatible(p, "uni-n-i2c");
		if (!ok)
			goto next;
		reg = (u32 *)get_property(cc, "reg", NULL);
		if (reg == NULL)
			goto next;
		switch (*reg) {
		case 0xd2:
			if (device_is_compatible(cc, "pulsar-legacy-slewing")) {
				pmac_tb_freeze = smp_core99_pulsar_tb_freeze;
				pmac_tb_pulsar_addr = 0xd2;
				printk(KERN_INFO "Timebase clock is Pulsar chip\n");
			} else if (device_is_compatible(cc, "cy28508")) {
				pmac_tb_freeze = smp_core99_cypress_tb_freeze;
				printk(KERN_INFO "Timebase clock is Cypress chip\n");
			}
			break;
		case 0xd4:
			pmac_tb_freeze = smp_core99_pulsar_tb_freeze;
			pmac_tb_pulsar_addr = 0xd4;
			printk(KERN_INFO "Timebase clock is Pulsar chip\n");
			break;
		}
		if (pmac_tb_freeze != NULL) {
			pmac_tb_clock_chip_host = p;
			smp_ops->give_timebase = smp_core99_give_timebase;
			smp_ops->take_timebase = smp_core99_take_timebase;
			of_node_put(cc);
			of_node_put(p);
			break;
		}
	next:
		of_node_put(p);
	}

 nohwsync:
	mpic_request_ipis();

	return ncpus;
}

static void __init smp_core99_kick_cpu(int nr)
{
	int save_vector, j;
	unsigned long new_vector;
	unsigned long flags;
	volatile unsigned int *vector
		 = ((volatile unsigned int *)(KERNELBASE+0x100));

	if (nr < 1 || nr > 3)
		return;
	if (ppc_md.progress) ppc_md.progress("smp_core99_kick_cpu", 0x346);

	local_irq_save(flags);
	local_irq_disable();

	/* Save reset vector */
	save_vector = *vector;

	/* Setup fake reset vector that does	
	 *   b .pmac_secondary_start - KERNELBASE
	 */
	switch(nr) {
	case 1:
		new_vector = (unsigned long)pmac_secondary_start_1;
		break;
	case 2:
		new_vector = (unsigned long)pmac_secondary_start_2;
		break;			
	case 3:
	default:
		new_vector = (unsigned long)pmac_secondary_start_3;
		break;
	}
	*vector = 0x48000002 + (new_vector - KERNELBASE);

	/* flush data cache and inval instruction cache */
	flush_icache_range((unsigned long) vector, (unsigned long) vector + 4);

	/* Put some life in our friend */
	pmac_call_feature(PMAC_FTR_RESET_CPU, NULL, nr, 0);
	paca[nr].cpu_start = 1;

	/* FIXME: We wait a bit for the CPU to take the exception, I should
	 * instead wait for the entry code to set something for me. Well,
	 * ideally, all that crap will be done in prom.c and the CPU left
	 * in a RAM-based wait loop like CHRP.
	 */
	for (j = 1; j < 1000000; j++)
		mb();

	/* Restore our exception vector */
	*vector = save_vector;
	flush_icache_range((unsigned long) vector, (unsigned long) vector + 4);

	local_irq_restore(flags);
	if (ppc_md.progress) ppc_md.progress("smp_core99_kick_cpu done", 0x347);
}

static void __init smp_core99_setup_cpu(int cpu_nr)
{
	/* Setup MPIC */
	mpic_setup_this_cpu();

	if (cpu_nr == 0) {
		extern void g5_phy_disable_cpu1(void);

		/* If we didn't start the second CPU, we must take
		 * it off the bus
		 */
		if (num_online_cpus() < 2)		
			g5_phy_disable_cpu1();
		if (ppc_md.progress) ppc_md.progress("smp_core99_setup_cpu 0 done", 0x349);
	}
}

struct smp_ops_t core99_smp_ops __pmacdata = {
	.message_pass	= smp_mpic_message_pass,
	.probe		= smp_core99_probe,
	.kick_cpu	= smp_core99_kick_cpu,
	.setup_cpu	= smp_core99_setup_cpu,
	.give_timebase	= smp_generic_give_timebase,
	.take_timebase	= smp_generic_take_timebase,
};

void __init pmac_setup_smp(void)
{
	smp_ops = &core99_smp_ops;
#ifdef CONFIG_HOTPLUG_CPU
	smp_ops->cpu_enable = generic_cpu_enable;
	smp_ops->cpu_disable = generic_cpu_disable;
	smp_ops->cpu_die = generic_cpu_die;
#endif
}

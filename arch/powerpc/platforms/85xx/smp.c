/*
 * Author: Andy Fleming <afleming@freescale.com>
 * 	   Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2006-2008, 2011-2012 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/kexec.h>
#include <linux/highmem.h>
#include <linux/cpu.h>

#include <asm/machdep.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/mpic.h>
#include <asm/cacheflush.h>
#include <asm/dbell.h>
#include <asm/fsl_guts.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/mpic.h>
#include "smp.h"

struct epapr_spin_table {
	u32	addr_h;
	u32	addr_l;
	u32	r3_h;
	u32	r3_l;
	u32	reserved;
	u32	pir;
};

static struct ccsr_guts __iomem *guts;
static u64 timebase;
static int tb_req;
static int tb_valid;

static void mpc85xx_timebase_freeze(int freeze)
{
	uint32_t mask;

	mask = CCSR_GUTS_DEVDISR_TB0 | CCSR_GUTS_DEVDISR_TB1;
	if (freeze)
		setbits32(&guts->devdisr, mask);
	else
		clrbits32(&guts->devdisr, mask);

	in_be32(&guts->devdisr);
}

static void mpc85xx_give_timebase(void)
{
	unsigned long flags;

	local_irq_save(flags);

	while (!tb_req)
		barrier();
	tb_req = 0;

	mpc85xx_timebase_freeze(1);
	timebase = get_tb();
	mb();
	tb_valid = 1;

	while (tb_valid)
		barrier();

	mpc85xx_timebase_freeze(0);

	local_irq_restore(flags);
}

static void mpc85xx_take_timebase(void)
{
	unsigned long flags;

	local_irq_save(flags);

	tb_req = 1;
	while (!tb_valid)
		barrier();

	set_tb(timebase >> 32, timebase & 0xffffffff);
	isync();
	tb_valid = 0;

	local_irq_restore(flags);
}

#ifdef CONFIG_HOTPLUG_CPU
static void __cpuinit smp_85xx_mach_cpu_die(void)
{
	unsigned int cpu = smp_processor_id();
	u32 tmp;

	local_irq_disable();
	idle_task_exit();
	generic_set_cpu_dead(cpu);
	mb();

	mtspr(SPRN_TCR, 0);

	__flush_disable_L1();
	tmp = (mfspr(SPRN_HID0) & ~(HID0_DOZE|HID0_SLEEP)) | HID0_NAP;
	mtspr(SPRN_HID0, tmp);
	isync();

	/* Enter NAP mode. */
	tmp = mfmsr();
	tmp |= MSR_WE;
	mb();
	mtmsr(tmp);
	isync();

	while (1)
		;
}
#endif

static inline void flush_spin_table(void *spin_table)
{
	flush_dcache_range((ulong)spin_table,
		(ulong)spin_table + sizeof(struct epapr_spin_table));
}

static inline u32 read_spin_table_addr_l(void *spin_table)
{
	flush_dcache_range((ulong)spin_table,
		(ulong)spin_table + sizeof(struct epapr_spin_table));
	return in_be32(&((struct epapr_spin_table *)spin_table)->addr_l);
}

static int __cpuinit smp_85xx_kick_cpu(int nr)
{
	unsigned long flags;
	const u64 *cpu_rel_addr;
	__iomem struct epapr_spin_table *spin_table;
	struct device_node *np;
	int hw_cpu = get_hard_smp_processor_id(nr);
	int ioremappable;
	int ret = 0;

	WARN_ON(nr < 0 || nr >= NR_CPUS);
	WARN_ON(hw_cpu < 0 || hw_cpu >= NR_CPUS);

	pr_debug("smp_85xx_kick_cpu: kick CPU #%d\n", nr);

	np = of_get_cpu_node(nr, NULL);
	cpu_rel_addr = of_get_property(np, "cpu-release-addr", NULL);

	if (cpu_rel_addr == NULL) {
		printk(KERN_ERR "No cpu-release-addr for cpu %d\n", nr);
		return -ENOENT;
	}

	/*
	 * A secondary core could be in a spinloop in the bootpage
	 * (0xfffff000), somewhere in highmem, or somewhere in lowmem.
	 * The bootpage and highmem can be accessed via ioremap(), but
	 * we need to directly access the spinloop if its in lowmem.
	 */
	ioremappable = *cpu_rel_addr > virt_to_phys(high_memory);

	/* Map the spin table */
	if (ioremappable)
		spin_table = ioremap_prot(*cpu_rel_addr,
			sizeof(struct epapr_spin_table), _PAGE_COHERENT);
	else
		spin_table = phys_to_virt(*cpu_rel_addr);

	local_irq_save(flags);
#ifdef CONFIG_PPC32
#ifdef CONFIG_HOTPLUG_CPU
	/* Corresponding to generic_set_cpu_dead() */
	generic_set_cpu_up(nr);

	if (system_state == SYSTEM_RUNNING) {
		/*
		 * To keep it compatible with old boot program which uses
		 * cache-inhibit spin table, we need to flush the cache
		 * before accessing spin table to invalidate any staled data.
		 * We also need to flush the cache after writing to spin
		 * table to push data out.
		 */
		flush_spin_table(spin_table);
		out_be32(&spin_table->addr_l, 0);
		flush_spin_table(spin_table);

		/*
		 * We don't set the BPTR register here since it already points
		 * to the boot page properly.
		 */
		mpic_reset_core(nr);

		/*
		 * wait until core is ready...
		 * We need to invalidate the stale data, in case the boot
		 * loader uses a cache-inhibited spin table.
		 */
		if (!spin_event_timeout(
				read_spin_table_addr_l(spin_table) == 1,
				10000, 100)) {
			pr_err("%s: timeout waiting for core %d to reset\n",
							__func__, hw_cpu);
			ret = -ENOENT;
			goto out;
		}

		/*  clear the acknowledge status */
		__secondary_hold_acknowledge = -1;
	}
#endif
	flush_spin_table(spin_table);
	out_be32(&spin_table->pir, hw_cpu);
	out_be32(&spin_table->addr_l, __pa(__early_start));
	flush_spin_table(spin_table);

	/* Wait a bit for the CPU to ack. */
	if (!spin_event_timeout(__secondary_hold_acknowledge == hw_cpu,
					10000, 100)) {
		pr_err("%s: timeout waiting for core %d to ack\n",
						__func__, hw_cpu);
		ret = -ENOENT;
		goto out;
	}
out:
#else
	smp_generic_kick_cpu(nr);

	flush_spin_table(spin_table);
	out_be32(&spin_table->pir, hw_cpu);
	out_be64((u64 *)(&spin_table->addr_h),
	  __pa((u64)*((unsigned long long *)generic_secondary_smp_init)));
	flush_spin_table(spin_table);
#endif

	local_irq_restore(flags);

	if (ioremappable)
		iounmap(spin_table);

	return ret;
}

struct smp_ops_t smp_85xx_ops = {
	.kick_cpu = smp_85xx_kick_cpu,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable	= generic_cpu_disable,
	.cpu_die	= generic_cpu_die,
#endif
#ifdef CONFIG_KEXEC
	.give_timebase	= smp_generic_give_timebase,
	.take_timebase	= smp_generic_take_timebase,
#endif
};

#ifdef CONFIG_KEXEC
atomic_t kexec_down_cpus = ATOMIC_INIT(0);

void mpc85xx_smp_kexec_cpu_down(int crash_shutdown, int secondary)
{
	local_irq_disable();

	if (secondary) {
		atomic_inc(&kexec_down_cpus);
		/* loop forever */
		while (1);
	}
}

static void mpc85xx_smp_kexec_down(void *arg)
{
	if (ppc_md.kexec_cpu_down)
		ppc_md.kexec_cpu_down(0,1);
}

static void map_and_flush(unsigned long paddr)
{
	struct page *page = pfn_to_page(paddr >> PAGE_SHIFT);
	unsigned long kaddr  = (unsigned long)kmap(page);

	flush_dcache_range(kaddr, kaddr + PAGE_SIZE);
	kunmap(page);
}

/**
 * Before we reset the other cores, we need to flush relevant cache
 * out to memory so we don't get anything corrupted, some of these flushes
 * are performed out of an overabundance of caution as interrupts are not
 * disabled yet and we can switch cores
 */
static void mpc85xx_smp_flush_dcache_kexec(struct kimage *image)
{
	kimage_entry_t *ptr, entry;
	unsigned long paddr;
	int i;

	if (image->type == KEXEC_TYPE_DEFAULT) {
		/* normal kexec images are stored in temporary pages */
		for (ptr = &image->head; (entry = *ptr) && !(entry & IND_DONE);
		     ptr = (entry & IND_INDIRECTION) ?
				phys_to_virt(entry & PAGE_MASK) : ptr + 1) {
			if (!(entry & IND_DESTINATION)) {
				map_and_flush(entry);
			}
		}
		/* flush out last IND_DONE page */
		map_and_flush(entry);
	} else {
		/* crash type kexec images are copied to the crash region */
		for (i = 0; i < image->nr_segments; i++) {
			struct kexec_segment *seg = &image->segment[i];
			for (paddr = seg->mem; paddr < seg->mem + seg->memsz;
			     paddr += PAGE_SIZE) {
				map_and_flush(paddr);
			}
		}
	}

	/* also flush the kimage struct to be passed in as well */
	flush_dcache_range((unsigned long)image,
			   (unsigned long)image + sizeof(*image));
}

static void mpc85xx_smp_machine_kexec(struct kimage *image)
{
	int timeout = INT_MAX;
	int i, num_cpus = num_present_cpus();

	mpc85xx_smp_flush_dcache_kexec(image);

	if (image->type == KEXEC_TYPE_DEFAULT)
		smp_call_function(mpc85xx_smp_kexec_down, NULL, 0);

	while ( (atomic_read(&kexec_down_cpus) != (num_cpus - 1)) &&
		( timeout > 0 ) )
	{
		timeout--;
	}

	if ( !timeout )
		printk(KERN_ERR "Unable to bring down secondary cpu(s)");

	for_each_online_cpu(i)
	{
		if ( i == smp_processor_id() ) continue;
		mpic_reset_core(i);
	}

	default_machine_kexec(image);
}
#endif /* CONFIG_KEXEC */

static void __cpuinit smp_85xx_setup_cpu(int cpu_nr)
{
	if (smp_85xx_ops.probe == smp_mpic_probe)
		mpic_setup_this_cpu();

	if (cpu_has_feature(CPU_FTR_DBELL))
		doorbell_setup_this_cpu();
}

static const struct of_device_id mpc85xx_smp_guts_ids[] = {
	{ .compatible = "fsl,mpc8572-guts", },
	{ .compatible = "fsl,p1020-guts", },
	{ .compatible = "fsl,p1021-guts", },
	{ .compatible = "fsl,p1022-guts", },
	{ .compatible = "fsl,p1023-guts", },
	{ .compatible = "fsl,p2020-guts", },
	{},
};

void __init mpc85xx_smp_init(void)
{
	struct device_node *np;

	smp_85xx_ops.setup_cpu = smp_85xx_setup_cpu;

	np = of_find_node_by_type(NULL, "open-pic");
	if (np) {
		smp_85xx_ops.probe = smp_mpic_probe;
		smp_85xx_ops.message_pass = smp_mpic_message_pass;
	}

	if (cpu_has_feature(CPU_FTR_DBELL)) {
		/*
		 * If left NULL, .message_pass defaults to
		 * smp_muxed_ipi_message_pass
		 */
		smp_85xx_ops.message_pass = NULL;
		smp_85xx_ops.cause_ipi = doorbell_cause_ipi;
	}

	np = of_find_matching_node(NULL, mpc85xx_smp_guts_ids);
	if (np) {
		guts = of_iomap(np, 0);
		of_node_put(np);
		if (!guts) {
			pr_err("%s: Could not map guts node address\n",
								__func__);
			return;
		}
		smp_85xx_ops.give_timebase = mpc85xx_give_timebase;
		smp_85xx_ops.take_timebase = mpc85xx_take_timebase;
#ifdef CONFIG_HOTPLUG_CPU
		ppc_md.cpu_die = smp_85xx_mach_cpu_die;
#endif
	}

	smp_ops = &smp_85xx_ops;

#ifdef CONFIG_KEXEC
	ppc_md.kexec_cpu_down = mpc85xx_smp_kexec_cpu_down;
	ppc_md.machine_kexec = mpc85xx_smp_machine_kexec;
#endif
}

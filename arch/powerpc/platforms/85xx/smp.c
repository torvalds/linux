/*
 * Author: Andy Fleming <afleming@freescale.com>
 * 	   Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2006-2008 Freescale Semiconductor Inc.
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

#include <asm/machdep.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/mpic.h>
#include <asm/cacheflush.h>
#include <asm/dbell.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/mpic.h>

extern void __early_start(void);

#define BOOT_ENTRY_ADDR_UPPER	0
#define BOOT_ENTRY_ADDR_LOWER	1
#define BOOT_ENTRY_R3_UPPER	2
#define BOOT_ENTRY_R3_LOWER	3
#define BOOT_ENTRY_RESV		4
#define BOOT_ENTRY_PIR		5
#define BOOT_ENTRY_R6_UPPER	6
#define BOOT_ENTRY_R6_LOWER	7
#define NUM_BOOT_ENTRY		8
#define SIZE_BOOT_ENTRY		(NUM_BOOT_ENTRY * sizeof(u32))

static void __init
smp_85xx_kick_cpu(int nr)
{
	unsigned long flags;
	const u64 *cpu_rel_addr;
	__iomem u32 *bptr_vaddr;
	struct device_node *np;
	int n = 0;
	int ioremappable;

	WARN_ON (nr < 0 || nr >= NR_CPUS);

	pr_debug("smp_85xx_kick_cpu: kick CPU #%d\n", nr);

	np = of_get_cpu_node(nr, NULL);
	cpu_rel_addr = of_get_property(np, "cpu-release-addr", NULL);

	if (cpu_rel_addr == NULL) {
		printk(KERN_ERR "No cpu-release-addr for cpu %d\n", nr);
		return;
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
		bptr_vaddr = ioremap(*cpu_rel_addr, SIZE_BOOT_ENTRY);
	else
		bptr_vaddr = phys_to_virt(*cpu_rel_addr);

	local_irq_save(flags);

	out_be32(bptr_vaddr + BOOT_ENTRY_PIR, nr);
#ifdef CONFIG_PPC32
	out_be32(bptr_vaddr + BOOT_ENTRY_ADDR_LOWER, __pa(__early_start));

	if (!ioremappable)
		flush_dcache_range((ulong)bptr_vaddr,
				(ulong)(bptr_vaddr + SIZE_BOOT_ENTRY));

	/* Wait a bit for the CPU to ack. */
	while ((__secondary_hold_acknowledge != nr) && (++n < 1000))
		mdelay(1);
#else
	smp_generic_kick_cpu(nr);

	out_be64((u64 *)(bptr_vaddr + BOOT_ENTRY_ADDR_UPPER),
		__pa((u64)*((unsigned long long *) generic_secondary_smp_init)));

	if (!ioremappable)
		flush_dcache_range((ulong)bptr_vaddr,
				(ulong)(bptr_vaddr + SIZE_BOOT_ENTRY));
#endif

	local_irq_restore(flags);

	if (ioremappable)
		iounmap(bptr_vaddr);

	pr_debug("waited %d msecs for CPU #%d.\n", n, nr);
}

static void __init
smp_85xx_setup_cpu(int cpu_nr)
{
	mpic_setup_this_cpu();
	if (cpu_has_feature(CPU_FTR_DBELL))
		doorbell_setup_this_cpu();
}

struct smp_ops_t smp_85xx_ops = {
	.kick_cpu = smp_85xx_kick_cpu,
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

	for (i = 0; i < num_cpus; i++)
	{
		if ( i == smp_processor_id() ) continue;
		mpic_reset_core(i);
	}

	default_machine_kexec(image);
}
#endif /* CONFIG_KEXEC */

void __init mpc85xx_smp_init(void)
{
	struct device_node *np;

	np = of_find_node_by_type(NULL, "open-pic");
	if (np) {
		smp_85xx_ops.probe = smp_mpic_probe;
		smp_85xx_ops.setup_cpu = smp_85xx_setup_cpu;
		smp_85xx_ops.message_pass = smp_mpic_message_pass;
	}

	if (cpu_has_feature(CPU_FTR_DBELL))
		smp_85xx_ops.message_pass = doorbell_message_pass;

	BUG_ON(!smp_85xx_ops.message_pass);

	smp_ops = &smp_85xx_ops;

#ifdef CONFIG_KEXEC
	ppc_md.kexec_cpu_down = mpc85xx_smp_kexec_cpu_down;
	ppc_md.machine_kexec = mpc85xx_smp_machine_kexec;
#endif
}

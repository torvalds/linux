/*
 * Local APIC related interfaces to support IOAPIC, MSI, HT_IRQ etc.
 *
 * Copyright (C) 1997, 1998, 1999, 2000, 2009 Ingo Molnar, Hajnalka Szabo
 *	Moved from arch/x86/kernel/apic/io_apic.c.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/irqdomain.h>
#include <linux/slab.h>
#include <asm/hw_irq.h>
#include <asm/apic.h>
#include <asm/i8259.h>
#include <asm/desc.h>
#include <asm/irq_remapping.h>

static DEFINE_RAW_SPINLOCK(vector_lock);

void lock_vector_lock(void)
{
	/* Used to the online set of cpus does not change
	 * during assign_irq_vector.
	 */
	raw_spin_lock(&vector_lock);
}

void unlock_vector_lock(void)
{
	raw_spin_unlock(&vector_lock);
}

struct irq_cfg *irq_cfg(unsigned int irq)
{
	return irq_get_chip_data(irq);
}

struct irq_cfg *irqd_cfg(struct irq_data *irq_data)
{
	return irq_data->chip_data;
}

static struct irq_cfg *alloc_irq_cfg(unsigned int irq, int node)
{
	struct irq_cfg *cfg;

	cfg = kzalloc_node(sizeof(*cfg), GFP_KERNEL, node);
	if (!cfg)
		return NULL;
	if (!zalloc_cpumask_var_node(&cfg->domain, GFP_KERNEL, node))
		goto out_cfg;
	if (!zalloc_cpumask_var_node(&cfg->old_domain, GFP_KERNEL, node))
		goto out_domain;
#ifdef	CONFIG_X86_IO_APIC
	INIT_LIST_HEAD(&cfg->irq_2_pin);
#endif
	return cfg;
out_domain:
	free_cpumask_var(cfg->domain);
out_cfg:
	kfree(cfg);
	return NULL;
}

struct irq_cfg *alloc_irq_and_cfg_at(unsigned int at, int node)
{
	int res = irq_alloc_desc_at(at, node);
	struct irq_cfg *cfg;

	if (res < 0) {
		if (res != -EEXIST)
			return NULL;
		cfg = irq_cfg(at);
		if (cfg)
			return cfg;
	}

	cfg = alloc_irq_cfg(at, node);
	if (cfg)
		irq_set_chip_data(at, cfg);
	else
		irq_free_desc(at);
	return cfg;
}

static void free_irq_cfg(unsigned int at, struct irq_cfg *cfg)
{
	if (!cfg)
		return;
	irq_set_chip_data(at, NULL);
	free_cpumask_var(cfg->domain);
	free_cpumask_var(cfg->old_domain);
	kfree(cfg);
}

static int
__assign_irq_vector(int irq, struct irq_cfg *cfg, const struct cpumask *mask)
{
	/*
	 * NOTE! The local APIC isn't very good at handling
	 * multiple interrupts at the same interrupt level.
	 * As the interrupt level is determined by taking the
	 * vector number and shifting that right by 4, we
	 * want to spread these out a bit so that they don't
	 * all fall in the same interrupt level.
	 *
	 * Also, we've got to be careful not to trash gate
	 * 0x80, because int 0x80 is hm, kind of importantish. ;)
	 */
	static int current_vector = FIRST_EXTERNAL_VECTOR + VECTOR_OFFSET_START;
	static int current_offset = VECTOR_OFFSET_START % 16;
	int cpu, err;
	cpumask_var_t tmp_mask;

	if (cfg->move_in_progress)
		return -EBUSY;

	if (!alloc_cpumask_var(&tmp_mask, GFP_ATOMIC))
		return -ENOMEM;

	/* Only try and allocate irqs on cpus that are present */
	err = -ENOSPC;
	cpumask_clear(cfg->old_domain);
	cpu = cpumask_first_and(mask, cpu_online_mask);
	while (cpu < nr_cpu_ids) {
		int new_cpu, vector, offset;

		apic->vector_allocation_domain(cpu, tmp_mask, mask);

		if (cpumask_subset(tmp_mask, cfg->domain)) {
			err = 0;
			if (cpumask_equal(tmp_mask, cfg->domain))
				break;
			/*
			 * New cpumask using the vector is a proper subset of
			 * the current in use mask. So cleanup the vector
			 * allocation for the members that are not used anymore.
			 */
			cpumask_andnot(cfg->old_domain, cfg->domain, tmp_mask);
			cfg->move_in_progress =
			   cpumask_intersects(cfg->old_domain, cpu_online_mask);
			cpumask_and(cfg->domain, cfg->domain, tmp_mask);
			break;
		}

		vector = current_vector;
		offset = current_offset;
next:
		vector += 16;
		if (vector >= first_system_vector) {
			offset = (offset + 1) % 16;
			vector = FIRST_EXTERNAL_VECTOR + offset;
		}

		if (unlikely(current_vector == vector)) {
			cpumask_or(cfg->old_domain, cfg->old_domain, tmp_mask);
			cpumask_andnot(tmp_mask, mask, cfg->old_domain);
			cpu = cpumask_first_and(tmp_mask, cpu_online_mask);
			continue;
		}

		if (test_bit(vector, used_vectors))
			goto next;

		for_each_cpu_and(new_cpu, tmp_mask, cpu_online_mask) {
			if (per_cpu(vector_irq, new_cpu)[vector] >
			    VECTOR_UNDEFINED)
				goto next;
		}
		/* Found one! */
		current_vector = vector;
		current_offset = offset;
		if (cfg->vector) {
			cpumask_copy(cfg->old_domain, cfg->domain);
			cfg->move_in_progress =
			   cpumask_intersects(cfg->old_domain, cpu_online_mask);
		}
		for_each_cpu_and(new_cpu, tmp_mask, cpu_online_mask)
			per_cpu(vector_irq, new_cpu)[vector] = irq;
		cfg->vector = vector;
		cpumask_copy(cfg->domain, tmp_mask);
		err = 0;
		break;
	}
	free_cpumask_var(tmp_mask);

	return err;
}

int assign_irq_vector(int irq, struct irq_cfg *cfg, const struct cpumask *mask)
{
	int err;
	unsigned long flags;

	raw_spin_lock_irqsave(&vector_lock, flags);
	err = __assign_irq_vector(irq, cfg, mask);
	raw_spin_unlock_irqrestore(&vector_lock, flags);
	return err;
}

void clear_irq_vector(int irq, struct irq_cfg *cfg)
{
	int cpu, vector;
	unsigned long flags;

	raw_spin_lock_irqsave(&vector_lock, flags);
	BUG_ON(!cfg->vector);

	vector = cfg->vector;
	for_each_cpu_and(cpu, cfg->domain, cpu_online_mask)
		per_cpu(vector_irq, cpu)[vector] = VECTOR_UNDEFINED;

	cfg->vector = 0;
	cpumask_clear(cfg->domain);

	if (likely(!cfg->move_in_progress)) {
		raw_spin_unlock_irqrestore(&vector_lock, flags);
		return;
	}

	for_each_cpu_and(cpu, cfg->old_domain, cpu_online_mask) {
		for (vector = FIRST_EXTERNAL_VECTOR; vector < NR_VECTORS;
		     vector++) {
			if (per_cpu(vector_irq, cpu)[vector] != irq)
				continue;
			per_cpu(vector_irq, cpu)[vector] = VECTOR_UNDEFINED;
			break;
		}
	}
	cfg->move_in_progress = 0;
	raw_spin_unlock_irqrestore(&vector_lock, flags);
}

static void __setup_vector_irq(int cpu)
{
	/* Initialize vector_irq on a new cpu */
	int irq, vector;
	struct irq_cfg *cfg;

	/*
	 * vector_lock will make sure that we don't run into irq vector
	 * assignments that might be happening on another cpu in parallel,
	 * while we setup our initial vector to irq mappings.
	 */
	raw_spin_lock(&vector_lock);
	/* Mark the inuse vectors */
	for_each_active_irq(irq) {
		cfg = irq_cfg(irq);
		if (!cfg)
			continue;

		if (!cpumask_test_cpu(cpu, cfg->domain))
			continue;
		vector = cfg->vector;
		per_cpu(vector_irq, cpu)[vector] = irq;
	}
	/* Mark the free vectors */
	for (vector = 0; vector < NR_VECTORS; ++vector) {
		irq = per_cpu(vector_irq, cpu)[vector];
		if (irq <= VECTOR_UNDEFINED)
			continue;

		cfg = irq_cfg(irq);
		if (!cpumask_test_cpu(cpu, cfg->domain))
			per_cpu(vector_irq, cpu)[vector] = VECTOR_UNDEFINED;
	}
	raw_spin_unlock(&vector_lock);
}

/*
 * Setup the vector to irq mappings.
 */
void setup_vector_irq(int cpu)
{
	int irq;

	/*
	 * On most of the platforms, legacy PIC delivers the interrupts on the
	 * boot cpu. But there are certain platforms where PIC interrupts are
	 * delivered to multiple cpu's. If the legacy IRQ is handled by the
	 * legacy PIC, for the new cpu that is coming online, setup the static
	 * legacy vector to irq mapping:
	 */
	for (irq = 0; irq < nr_legacy_irqs(); irq++)
		per_cpu(vector_irq, cpu)[IRQ0_VECTOR + irq] = irq;

	__setup_vector_irq(cpu);
}

int apic_retrigger_irq(struct irq_data *data)
{
	struct irq_cfg *cfg = data->chip_data;
	unsigned long flags;
	int cpu;

	raw_spin_lock_irqsave(&vector_lock, flags);
	cpu = cpumask_first_and(cfg->domain, cpu_online_mask);
	apic->send_IPI_mask(cpumask_of(cpu), cfg->vector);
	raw_spin_unlock_irqrestore(&vector_lock, flags);

	return 1;
}

void apic_ack_edge(struct irq_data *data)
{
	irq_complete_move(data->chip_data);
	irq_move_irq(data);
	ack_APIC_irq();
}

/*
 * Either sets data->affinity to a valid value, and returns
 * ->cpu_mask_to_apicid of that in dest_id, or returns -1 and
 * leaves data->affinity untouched.
 */
int apic_set_affinity(struct irq_data *data, const struct cpumask *mask,
		      unsigned int *dest_id)
{
	struct irq_cfg *cfg = data->chip_data;
	unsigned int irq = data->irq;
	int err;

	if (!config_enabled(CONFIG_SMP))
		return -EPERM;

	if (!cpumask_intersects(mask, cpu_online_mask))
		return -EINVAL;

	err = assign_irq_vector(irq, cfg, mask);
	if (err)
		return err;

	err = apic->cpu_mask_to_apicid_and(mask, cfg->domain, dest_id);
	if (err) {
		if (assign_irq_vector(irq, cfg, data->affinity))
			pr_err("Failed to recover vector for irq %d\n", irq);
		return err;
	}

	cpumask_copy(data->affinity, mask);

	return 0;
}

#ifdef CONFIG_SMP
void send_cleanup_vector(struct irq_cfg *cfg)
{
	cpumask_var_t cleanup_mask;

	if (unlikely(!alloc_cpumask_var(&cleanup_mask, GFP_ATOMIC))) {
		unsigned int i;

		for_each_cpu_and(i, cfg->old_domain, cpu_online_mask)
			apic->send_IPI_mask(cpumask_of(i),
					    IRQ_MOVE_CLEANUP_VECTOR);
	} else {
		cpumask_and(cleanup_mask, cfg->old_domain, cpu_online_mask);
		apic->send_IPI_mask(cleanup_mask, IRQ_MOVE_CLEANUP_VECTOR);
		free_cpumask_var(cleanup_mask);
	}
	cfg->move_in_progress = 0;
}

asmlinkage __visible void smp_irq_move_cleanup_interrupt(void)
{
	unsigned vector, me;

	ack_APIC_irq();
	irq_enter();
	exit_idle();

	me = smp_processor_id();
	for (vector = FIRST_EXTERNAL_VECTOR; vector < NR_VECTORS; vector++) {
		int irq;
		unsigned int irr;
		struct irq_desc *desc;
		struct irq_cfg *cfg;

		irq = __this_cpu_read(vector_irq[vector]);

		if (irq <= VECTOR_UNDEFINED)
			continue;

		desc = irq_to_desc(irq);
		if (!desc)
			continue;

		cfg = irq_cfg(irq);
		if (!cfg)
			continue;

		raw_spin_lock(&desc->lock);

		/*
		 * Check if the irq migration is in progress. If so, we
		 * haven't received the cleanup request yet for this irq.
		 */
		if (cfg->move_in_progress)
			goto unlock;

		if (vector == cfg->vector && cpumask_test_cpu(me, cfg->domain))
			goto unlock;

		irr = apic_read(APIC_IRR + (vector / 32 * 0x10));
		/*
		 * Check if the vector that needs to be cleanedup is
		 * registered at the cpu's IRR. If so, then this is not
		 * the best time to clean it up. Lets clean it up in the
		 * next attempt by sending another IRQ_MOVE_CLEANUP_VECTOR
		 * to myself.
		 */
		if (irr  & (1 << (vector % 32))) {
			apic->send_IPI_self(IRQ_MOVE_CLEANUP_VECTOR);
			goto unlock;
		}
		__this_cpu_write(vector_irq[vector], VECTOR_UNDEFINED);
unlock:
		raw_spin_unlock(&desc->lock);
	}

	irq_exit();
}

static void __irq_complete_move(struct irq_cfg *cfg, unsigned vector)
{
	unsigned me;

	if (likely(!cfg->move_in_progress))
		return;

	me = smp_processor_id();

	if (vector == cfg->vector && cpumask_test_cpu(me, cfg->domain))
		send_cleanup_vector(cfg);
}

void irq_complete_move(struct irq_cfg *cfg)
{
	__irq_complete_move(cfg, ~get_irq_regs()->orig_ax);
}

void irq_force_complete_move(int irq)
{
	struct irq_cfg *cfg = irq_cfg(irq);

	if (!cfg)
		return;

	__irq_complete_move(cfg, cfg->vector);
}
#endif

/*
 * Dynamic irq allocate and deallocation. Should be replaced by irq domains!
 */
int arch_setup_hwirq(unsigned int irq, int node)
{
	struct irq_cfg *cfg;
	unsigned long flags;
	int ret;

	cfg = alloc_irq_cfg(irq, node);
	if (!cfg)
		return -ENOMEM;

	raw_spin_lock_irqsave(&vector_lock, flags);
	ret = __assign_irq_vector(irq, cfg, apic->target_cpus());
	raw_spin_unlock_irqrestore(&vector_lock, flags);

	if (!ret)
		irq_set_chip_data(irq, cfg);
	else
		free_irq_cfg(irq, cfg);
	return ret;
}

void arch_teardown_hwirq(unsigned int irq)
{
	struct irq_cfg *cfg = irq_cfg(irq);

	free_remapped_irq(irq);
	clear_irq_vector(irq, cfg);
	free_irq_cfg(irq, cfg);
}

static void __init print_APIC_field(int base)
{
	int i;

	printk(KERN_DEBUG);

	for (i = 0; i < 8; i++)
		pr_cont("%08x", apic_read(base + i*0x10));

	pr_cont("\n");
}

static void __init print_local_APIC(void *dummy)
{
	unsigned int i, v, ver, maxlvt;
	u64 icr;

	printk(KERN_DEBUG "printing local APIC contents on CPU#%d/%d:\n",
		smp_processor_id(), hard_smp_processor_id());
	v = apic_read(APIC_ID);
	printk(KERN_INFO "... APIC ID:      %08x (%01x)\n", v, read_apic_id());
	v = apic_read(APIC_LVR);
	printk(KERN_INFO "... APIC VERSION: %08x\n", v);
	ver = GET_APIC_VERSION(v);
	maxlvt = lapic_get_maxlvt();

	v = apic_read(APIC_TASKPRI);
	printk(KERN_DEBUG "... APIC TASKPRI: %08x (%02x)\n",
	       v, v & APIC_TPRI_MASK);

	/* !82489DX */
	if (APIC_INTEGRATED(ver)) {
		if (!APIC_XAPIC(ver)) {
			v = apic_read(APIC_ARBPRI);
			printk(KERN_DEBUG "... APIC ARBPRI: %08x (%02x)\n", v,
			       v & APIC_ARBPRI_MASK);
		}
		v = apic_read(APIC_PROCPRI);
		printk(KERN_DEBUG "... APIC PROCPRI: %08x\n", v);
	}

	/*
	 * Remote read supported only in the 82489DX and local APIC for
	 * Pentium processors.
	 */
	if (!APIC_INTEGRATED(ver) || maxlvt == 3) {
		v = apic_read(APIC_RRR);
		printk(KERN_DEBUG "... APIC RRR: %08x\n", v);
	}

	v = apic_read(APIC_LDR);
	printk(KERN_DEBUG "... APIC LDR: %08x\n", v);
	if (!x2apic_enabled()) {
		v = apic_read(APIC_DFR);
		printk(KERN_DEBUG "... APIC DFR: %08x\n", v);
	}
	v = apic_read(APIC_SPIV);
	printk(KERN_DEBUG "... APIC SPIV: %08x\n", v);

	printk(KERN_DEBUG "... APIC ISR field:\n");
	print_APIC_field(APIC_ISR);
	printk(KERN_DEBUG "... APIC TMR field:\n");
	print_APIC_field(APIC_TMR);
	printk(KERN_DEBUG "... APIC IRR field:\n");
	print_APIC_field(APIC_IRR);

	/* !82489DX */
	if (APIC_INTEGRATED(ver)) {
		/* Due to the Pentium erratum 3AP. */
		if (maxlvt > 3)
			apic_write(APIC_ESR, 0);

		v = apic_read(APIC_ESR);
		printk(KERN_DEBUG "... APIC ESR: %08x\n", v);
	}

	icr = apic_icr_read();
	printk(KERN_DEBUG "... APIC ICR: %08x\n", (u32)icr);
	printk(KERN_DEBUG "... APIC ICR2: %08x\n", (u32)(icr >> 32));

	v = apic_read(APIC_LVTT);
	printk(KERN_DEBUG "... APIC LVTT: %08x\n", v);

	if (maxlvt > 3) {
		/* PC is LVT#4. */
		v = apic_read(APIC_LVTPC);
		printk(KERN_DEBUG "... APIC LVTPC: %08x\n", v);
	}
	v = apic_read(APIC_LVT0);
	printk(KERN_DEBUG "... APIC LVT0: %08x\n", v);
	v = apic_read(APIC_LVT1);
	printk(KERN_DEBUG "... APIC LVT1: %08x\n", v);

	if (maxlvt > 2) {
		/* ERR is LVT#3. */
		v = apic_read(APIC_LVTERR);
		printk(KERN_DEBUG "... APIC LVTERR: %08x\n", v);
	}

	v = apic_read(APIC_TMICT);
	printk(KERN_DEBUG "... APIC TMICT: %08x\n", v);
	v = apic_read(APIC_TMCCT);
	printk(KERN_DEBUG "... APIC TMCCT: %08x\n", v);
	v = apic_read(APIC_TDCR);
	printk(KERN_DEBUG "... APIC TDCR: %08x\n", v);

	if (boot_cpu_has(X86_FEATURE_EXTAPIC)) {
		v = apic_read(APIC_EFEAT);
		maxlvt = (v >> 16) & 0xff;
		printk(KERN_DEBUG "... APIC EFEAT: %08x\n", v);
		v = apic_read(APIC_ECTRL);
		printk(KERN_DEBUG "... APIC ECTRL: %08x\n", v);
		for (i = 0; i < maxlvt; i++) {
			v = apic_read(APIC_EILVTn(i));
			printk(KERN_DEBUG "... APIC EILVT%d: %08x\n", i, v);
		}
	}
	pr_cont("\n");
}

static void __init print_local_APICs(int maxcpu)
{
	int cpu;

	if (!maxcpu)
		return;

	preempt_disable();
	for_each_online_cpu(cpu) {
		if (cpu >= maxcpu)
			break;
		smp_call_function_single(cpu, print_local_APIC, NULL, 1);
	}
	preempt_enable();
}

static void __init print_PIC(void)
{
	unsigned int v;
	unsigned long flags;

	if (!nr_legacy_irqs())
		return;

	printk(KERN_DEBUG "\nprinting PIC contents\n");

	raw_spin_lock_irqsave(&i8259A_lock, flags);

	v = inb(0xa1) << 8 | inb(0x21);
	printk(KERN_DEBUG "... PIC  IMR: %04x\n", v);

	v = inb(0xa0) << 8 | inb(0x20);
	printk(KERN_DEBUG "... PIC  IRR: %04x\n", v);

	outb(0x0b, 0xa0);
	outb(0x0b, 0x20);
	v = inb(0xa0) << 8 | inb(0x20);
	outb(0x0a, 0xa0);
	outb(0x0a, 0x20);

	raw_spin_unlock_irqrestore(&i8259A_lock, flags);

	printk(KERN_DEBUG "... PIC  ISR: %04x\n", v);

	v = inb(0x4d1) << 8 | inb(0x4d0);
	printk(KERN_DEBUG "... PIC ELCR: %04x\n", v);
}

static int show_lapic __initdata = 1;
static __init int setup_show_lapic(char *arg)
{
	int num = -1;

	if (strcmp(arg, "all") == 0) {
		show_lapic = CONFIG_NR_CPUS;
	} else {
		get_option(&arg, &num);
		if (num >= 0)
			show_lapic = num;
	}

	return 1;
}
__setup("show_lapic=", setup_show_lapic);

static int __init print_ICs(void)
{
	if (apic_verbosity == APIC_QUIET)
		return 0;

	print_PIC();

	/* don't print out if apic is not there */
	if (!cpu_has_apic && !apic_from_smp_config())
		return 0;

	print_local_APICs(show_lapic);
	print_IO_APICs();

	return 0;
}

late_initcall(print_ICs);

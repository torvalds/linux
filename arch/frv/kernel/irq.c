/* irq.c: FRV IRQ handling
 *
 * Copyright (C) 2003, 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*
 * (mostly architecture independent, will move to kernel/irq.c in 2.5.)
 *
 * IRQs are in fact implemented a bit like signal handlers for the kernel.
 * Naturally it's not a 1:1 relation, but there are similarities.
 */

#include <linux/config.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/irq.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/module.h>

#include <asm/atomic.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/delay.h>
#include <asm/irq.h>
#include <asm/irc-regs.h>
#include <asm/irq-routing.h>
#include <asm/gdb-stub.h>

extern void __init fpga_init(void);
extern void __init route_mb93493_irqs(void);

static void register_irq_proc (unsigned int irq);

/*
 * Special irq handlers.
 */

irqreturn_t no_action(int cpl, void *dev_id, struct pt_regs *regs) { return IRQ_HANDLED; }

atomic_t irq_err_count;

/*
 * Generic, controller-independent functions:
 */
int show_interrupts(struct seq_file *p, void *v)
{
	struct irqaction *action;
	struct irq_group *group;
	unsigned long flags;
	int level, grp, ix, i, j;

	i = *(loff_t *) v;

	switch (i) {
	case 0:
		seq_printf(p, "           ");
		for (j = 0; j < NR_CPUS; j++)
			if (cpu_online(j))
				seq_printf(p, "CPU%d       ",j);

		seq_putc(p, '\n');
		break;

	case 1 ... NR_IRQ_GROUPS * NR_IRQ_ACTIONS_PER_GROUP:
		local_irq_save(flags);

		grp = (i - 1) / NR_IRQ_ACTIONS_PER_GROUP;
		group = irq_groups[grp];
		if (!group)
			goto skip;

		ix = (i - 1) % NR_IRQ_ACTIONS_PER_GROUP;
		action = group->actions[ix];
		if (!action)
			goto skip;

		seq_printf(p, "%3d: ", i - 1);

#ifndef CONFIG_SMP
		seq_printf(p, "%10u ", kstat_irqs(i));
#else
		for (j = 0; j < NR_CPUS; j++)
			if (cpu_online(j))
				seq_printf(p, "%10u ", kstat_cpu(j).irqs[i - 1]);
#endif

		level = group->sources[ix]->level - frv_irq_levels;

		seq_printf(p, " %12s@%x", group->sources[ix]->muxname, level);
		seq_printf(p, "  %s", action->name);

		for (action = action->next; action; action = action->next)
			seq_printf(p, ", %s", action->name);

		seq_putc(p, '\n');
skip:
		local_irq_restore(flags);
		break;

	case NR_IRQ_GROUPS * NR_IRQ_ACTIONS_PER_GROUP + 1:
		seq_printf(p, "ERR: %10u\n", atomic_read(&irq_err_count));
		break;

	default:
		break;
	}

	return 0;
}


/*
 * Generic enable/disable code: this just calls
 * down into the PIC-specific version for the actual
 * hardware disable after having gotten the irq
 * controller lock.
 */

/**
 *	disable_irq_nosync - disable an irq without waiting
 *	@irq: Interrupt to disable
 *
 *	Disable the selected interrupt line.  Disables and Enables are
 *	nested.
 *	Unlike disable_irq(), this function does not ensure existing
 *	instances of the IRQ handler have completed before returning.
 *
 *	This function may be called from IRQ context.
 */

void disable_irq_nosync(unsigned int irq)
{
	struct irq_source *source;
	struct irq_group *group;
	struct irq_level *level;
	unsigned long flags;
	int idx = irq & (NR_IRQ_ACTIONS_PER_GROUP - 1);

	group = irq_groups[irq >> NR_IRQ_LOG2_ACTIONS_PER_GROUP];
	if (!group)
		BUG();

	source = group->sources[idx];
	if (!source)
		BUG();

	level = source->level;

	spin_lock_irqsave(&level->lock, flags);

	if (group->control) {
		if (!group->disable_cnt[idx]++)
			group->control(group, idx, 0);
	} else if (!level->disable_count++) {
		__set_MASK(level - frv_irq_levels);
	}

	spin_unlock_irqrestore(&level->lock, flags);
}

EXPORT_SYMBOL(disable_irq_nosync);

/**
 *	disable_irq - disable an irq and wait for completion
 *	@irq: Interrupt to disable
 *
 *	Disable the selected interrupt line.  Enables and Disables are
 *	nested.
 *	This function waits for any pending IRQ handlers for this interrupt
 *	to complete before returning. If you use this function while
 *	holding a resource the IRQ handler may need you will deadlock.
 *
 *	This function may be called - with care - from IRQ context.
 */

void disable_irq(unsigned int irq)
{
	disable_irq_nosync(irq);

#ifdef CONFIG_SMP
	if (!local_irq_count(smp_processor_id())) {
		do {
			barrier();
		} while (irq_desc[irq].status & IRQ_INPROGRESS);
	}
#endif
}

EXPORT_SYMBOL(disable_irq);

/**
 *	enable_irq - enable handling of an irq
 *	@irq: Interrupt to enable
 *
 *	Undoes the effect of one call to disable_irq().  If this
 *	matches the last disable, processing of interrupts on this
 *	IRQ line is re-enabled.
 *
 *	This function may be called from IRQ context.
 */

void enable_irq(unsigned int irq)
{
	struct irq_source *source;
	struct irq_group *group;
	struct irq_level *level;
	unsigned long flags;
	int idx = irq & (NR_IRQ_ACTIONS_PER_GROUP - 1);
	int count;

	group = irq_groups[irq >> NR_IRQ_LOG2_ACTIONS_PER_GROUP];
	if (!group)
		BUG();

	source = group->sources[idx];
	if (!source)
		BUG();

	level = source->level;

	spin_lock_irqsave(&level->lock, flags);

	if (group->control)
		count = group->disable_cnt[idx];
	else
		count = level->disable_count;

	switch (count) {
	case 1:
		if (group->control) {
			if (group->actions[idx])
				group->control(group, idx, 1);
		} else {
			if (level->usage)
				__clr_MASK(level - frv_irq_levels);
		}
		/* fall-through */

	default:
		count--;
		break;

	case 0:
		printk("enable_irq(%u) unbalanced from %p\n", irq, __builtin_return_address(0));
	}

	if (group->control)
		group->disable_cnt[idx] = count;
	else
		level->disable_count = count;

	spin_unlock_irqrestore(&level->lock, flags);
}

EXPORT_SYMBOL(enable_irq);

/*****************************************************************************/
/*
 * handles all normal device IRQ's
 * - registers are referred to by the __frame variable (GR28)
 * - IRQ distribution is complicated in this arch because of the many PICs, the
 *   way they work and the way they cascade
 */
asmlinkage void do_IRQ(void)
{
	struct irq_source *source;
	int level, cpu;

	level = (__frame->tbr >> 4) & 0xf;
	cpu = smp_processor_id();

#if 0
	{
		static u32 irqcount;
		*(volatile u32 *) 0xe1200004 = ~((irqcount++ << 8) | level);
		*(volatile u16 *) 0xffc00100 = (u16) ~0x9999;
		mb();
	}
#endif

	if ((unsigned long) __frame - (unsigned long) (current + 1) < 512)
		BUG();

	__set_MASK(level);
	__clr_RC(level);
	__clr_IRL();

	kstat_this_cpu.irqs[level]++;

	irq_enter();

	for (source = frv_irq_levels[level].sources; source; source = source->next)
		source->doirq(source);

	irq_exit();

	__clr_MASK(level);

	/* only process softirqs if we didn't interrupt another interrupt handler */
	if ((__frame->psr & PSR_PIL) == PSR_PIL_0)
		if (local_softirq_pending())
			do_softirq();

#ifdef CONFIG_PREEMPT
	local_irq_disable();
	while (--current->preempt_count == 0) {
		if (!(__frame->psr & PSR_S) ||
		    current->need_resched == 0 ||
		    in_interrupt())
			break;
		current->preempt_count++;
		local_irq_enable();
		preempt_schedule();
		local_irq_disable();
	}
#endif

#if 0
	{
		*(volatile u16 *) 0xffc00100 = (u16) ~0x6666;
		mb();
	}
#endif

} /* end do_IRQ() */

/*****************************************************************************/
/*
 * handles all NMIs when not co-opted by the debugger
 * - registers are referred to by the __frame variable (GR28)
 */
asmlinkage void do_NMI(void)
{
} /* end do_NMI() */

/*****************************************************************************/
/**
 *	request_irq - allocate an interrupt line
 *	@irq: Interrupt line to allocate
 *	@handler: Function to be called when the IRQ occurs
 *	@irqflags: Interrupt type flags
 *	@devname: An ascii name for the claiming device
 *	@dev_id: A cookie passed back to the handler function
 *
 *	This call allocates interrupt resources and enables the
 *	interrupt line and IRQ handling. From the point this
 *	call is made your handler function may be invoked. Since
 *	your handler function must clear any interrupt the board
 *	raises, you must take care both to initialise your hardware
 *	and to set up the interrupt handler in the right order.
 *
 *	Dev_id must be globally unique. Normally the address of the
 *	device data structure is used as the cookie. Since the handler
 *	receives this value it makes sense to use it.
 *
 *	If your interrupt is shared you must pass a non NULL dev_id
 *	as this is required when freeing the interrupt.
 *
 *	Flags:
 *
 *	SA_SHIRQ		Interrupt is shared
 *
 *	SA_INTERRUPT		Disable local interrupts while processing
 *
 *	SA_SAMPLE_RANDOM	The interrupt can be used for entropy
 *
 */

int request_irq(unsigned int irq,
		irqreturn_t (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags,
		const char * devname,
		void *dev_id)
{
	int retval;
	struct irqaction *action;

#if 1
	/*
	 * Sanity-check: shared interrupts should REALLY pass in
	 * a real dev-ID, otherwise we'll have trouble later trying
	 * to figure out which interrupt is which (messes up the
	 * interrupt freeing logic etc).
	 */
	if (irqflags & SA_SHIRQ) {
		if (!dev_id)
			printk("Bad boy: %s (at 0x%x) called us without a dev_id!\n",
			       devname, (&irq)[-1]);
	}
#endif

	if ((irq >> NR_IRQ_LOG2_ACTIONS_PER_GROUP) >= NR_IRQ_GROUPS)
		return -EINVAL;
	if (!handler)
		return -EINVAL;

	action = (struct irqaction *) kmalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->flags = irqflags;
	action->mask = CPU_MASK_NONE;
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	retval = setup_irq(irq, action);
	if (retval)
		kfree(action);
	return retval;
}

EXPORT_SYMBOL(request_irq);

/**
 *	free_irq - free an interrupt
 *	@irq: Interrupt line to free
 *	@dev_id: Device identity to free
 *
 *	Remove an interrupt handler. The handler is removed and if the
 *	interrupt line is no longer in use by any driver it is disabled.
 *	On a shared IRQ the caller must ensure the interrupt is disabled
 *	on the card it drives before calling this function. The function
 *	does not return until any executing interrupts for this IRQ
 *	have completed.
 *
 *	This function may be called from interrupt context.
 *
 *	Bugs: Attempting to free an irq in a handler for the same irq hangs
 *	      the machine.
 */

void free_irq(unsigned int irq, void *dev_id)
{
	struct irq_source *source;
	struct irq_group *group;
	struct irq_level *level;
	struct irqaction **p, **pp;
	unsigned long flags;

	if ((irq >> NR_IRQ_LOG2_ACTIONS_PER_GROUP) >= NR_IRQ_GROUPS)
		return;

	group = irq_groups[irq >> NR_IRQ_LOG2_ACTIONS_PER_GROUP];
	if (!group)
		BUG();

	source = group->sources[irq & (NR_IRQ_ACTIONS_PER_GROUP - 1)];
	if (!source)
		BUG();

	level = source->level;
	p = &group->actions[irq & (NR_IRQ_ACTIONS_PER_GROUP - 1)];

	spin_lock_irqsave(&level->lock, flags);

	for (pp = p; *pp; pp = &(*pp)->next) {
		struct irqaction *action = *pp;

		if (action->dev_id != dev_id)
			continue;

		/* found it - remove from the list of entries */
		*pp = action->next;

		level->usage--;

		if (p == pp && group->control)
			group->control(group, irq & (NR_IRQ_ACTIONS_PER_GROUP - 1), 0);

		if (level->usage == 0)
			__set_MASK(level - frv_irq_levels);

		spin_unlock_irqrestore(&level->lock,flags);

#ifdef CONFIG_SMP
		/* Wait to make sure it's not being used on another CPU */
		while (desc->status & IRQ_INPROGRESS)
			barrier();
#endif
		kfree(action);
		return;
	}
}

EXPORT_SYMBOL(free_irq);

/*
 * IRQ autodetection code..
 *
 * This depends on the fact that any interrupt that comes in on to an
 * unassigned IRQ will cause GxICR_DETECT to be set
 */

static DECLARE_MUTEX(probe_sem);

/**
 *	probe_irq_on	- begin an interrupt autodetect
 *
 *	Commence probing for an interrupt. The interrupts are scanned
 *	and a mask of potential interrupt lines is returned.
 *
 */

unsigned long probe_irq_on(void)
{
	down(&probe_sem);
	return 0;
}

EXPORT_SYMBOL(probe_irq_on);

/*
 * Return a mask of triggered interrupts (this
 * can handle only legacy ISA interrupts).
 */

/**
 *	probe_irq_mask - scan a bitmap of interrupt lines
 *	@val:	mask of interrupts to consider
 *
 *	Scan the ISA bus interrupt lines and return a bitmap of
 *	active interrupts. The interrupt probe logic state is then
 *	returned to its previous value.
 *
 *	Note: we need to scan all the irq's even though we will
 *	only return ISA irq numbers - just so that we reset them
 *	all to a known state.
 */
unsigned int probe_irq_mask(unsigned long xmask)
{
	up(&probe_sem);
	return 0;
}

EXPORT_SYMBOL(probe_irq_mask);

/*
 * Return the one interrupt that triggered (this can
 * handle any interrupt source).
 */

/**
 *	probe_irq_off	- end an interrupt autodetect
 *	@xmask: mask of potential interrupts (unused)
 *
 *	Scans the unused interrupt lines and returns the line which
 *	appears to have triggered the interrupt. If no interrupt was
 *	found then zero is returned. If more than one interrupt is
 *	found then minus the first candidate is returned to indicate
 *	their is doubt.
 *
 *	The interrupt probe logic state is returned to its previous
 *	value.
 *
 *	BUGS: When used in a module (which arguably shouldnt happen)
 *	nothing prevents two IRQ probe callers from overlapping. The
 *	results of this are non-optimal.
 */

int probe_irq_off(unsigned long xmask)
{
	up(&probe_sem);
	return -1;
}

EXPORT_SYMBOL(probe_irq_off);

/* this was setup_x86_irq but it seems pretty generic */
int setup_irq(unsigned int irq, struct irqaction *new)
{
	struct irq_source *source;
	struct irq_group *group;
	struct irq_level *level;
	struct irqaction **p, **pp;
	unsigned long flags;

	group = irq_groups[irq >> NR_IRQ_LOG2_ACTIONS_PER_GROUP];
	if (!group)
		BUG();

	source = group->sources[irq & (NR_IRQ_ACTIONS_PER_GROUP - 1)];
	if (!source)
		BUG();

	level = source->level;

	p = &group->actions[irq & (NR_IRQ_ACTIONS_PER_GROUP - 1)];

	/*
	 * Some drivers like serial.c use request_irq() heavily,
	 * so we have to be careful not to interfere with a
	 * running system.
	 */
	if (new->flags & SA_SAMPLE_RANDOM) {
		/*
		 * This function might sleep, we want to call it first,
		 * outside of the atomic block.
		 * Yes, this might clear the entropy pool if the wrong
		 * driver is attempted to be loaded, without actually
		 * installing a new handler, but is this really a problem,
		 * only the sysadmin is able to do this.
		 */
		rand_initialize_irq(irq);
	}

	/* must juggle the interrupt processing stuff with interrupts disabled */
	spin_lock_irqsave(&level->lock, flags);

	/* can't share interrupts unless all parties agree to */
	if (level->usage != 0 && !(level->flags & new->flags & SA_SHIRQ)) {
		spin_unlock_irqrestore(&level->lock,flags);
		return -EBUSY;
	}

	/* add new interrupt at end of irq queue */
	pp = p;
	while (*pp)
		pp = &(*pp)->next;

	*pp = new;

	level->usage++;
	level->flags = new->flags;

	/* turn the interrupts on */
	if (level->usage == 1)
		__clr_MASK(level - frv_irq_levels);

	if (p == pp && group->control)
		group->control(group, irq & (NR_IRQ_ACTIONS_PER_GROUP - 1), 1);

	spin_unlock_irqrestore(&level->lock, flags);
	register_irq_proc(irq);
	return 0;
}

static struct proc_dir_entry * root_irq_dir;
static struct proc_dir_entry * irq_dir [NR_IRQS];

#define HEX_DIGITS 8

static unsigned int parse_hex_value (const char *buffer,
				     unsigned long count, unsigned long *ret)
{
	unsigned char hexnum [HEX_DIGITS];
	unsigned long value;
	int i;

	if (!count)
		return -EINVAL;
	if (count > HEX_DIGITS)
		count = HEX_DIGITS;
	if (copy_from_user(hexnum, buffer, count))
		return -EFAULT;

	/*
	 * Parse the first 8 characters as a hex string, any non-hex char
	 * is end-of-string. '00e1', 'e1', '00E1', 'E1' are all the same.
	 */
	value = 0;

	for (i = 0; i < count; i++) {
		unsigned int c = hexnum[i];

		switch (c) {
			case '0' ... '9': c -= '0'; break;
			case 'a' ... 'f': c -= 'a'-10; break;
			case 'A' ... 'F': c -= 'A'-10; break;
		default:
			goto out;
		}
		value = (value << 4) | c;
	}
out:
	*ret = value;
	return 0;
}


static int prof_cpu_mask_read_proc (char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	unsigned long *mask = (unsigned long *) data;
	if (count < HEX_DIGITS+1)
		return -EINVAL;
	return sprintf (page, "%08lx\n", *mask);
}

static int prof_cpu_mask_write_proc (struct file *file, const char *buffer,
					unsigned long count, void *data)
{
	unsigned long *mask = (unsigned long *) data, full_count = count, err;
	unsigned long new_value;

	show_state();
	err = parse_hex_value(buffer, count, &new_value);
	if (err)
		return err;

	*mask = new_value;
	return full_count;
}

#define MAX_NAMELEN 10

static void register_irq_proc (unsigned int irq)
{
	char name [MAX_NAMELEN];

	if (!root_irq_dir || irq_dir[irq])
		return;

	memset(name, 0, MAX_NAMELEN);
	sprintf(name, "%d", irq);

	/* create /proc/irq/1234 */
	irq_dir[irq] = proc_mkdir(name, root_irq_dir);
}

unsigned long prof_cpu_mask = -1;

void init_irq_proc (void)
{
	struct proc_dir_entry *entry;
	int i;

	/* create /proc/irq */
	root_irq_dir = proc_mkdir("irq", 0);

	/* create /proc/irq/prof_cpu_mask */
	entry = create_proc_entry("prof_cpu_mask", 0600, root_irq_dir);
	if (!entry)
	    return;

	entry->nlink = 1;
	entry->data = (void *)&prof_cpu_mask;
	entry->read_proc = prof_cpu_mask_read_proc;
	entry->write_proc = prof_cpu_mask_write_proc;

	/*
	 * Create entries for all existing IRQs.
	 */
	for (i = 0; i < NR_IRQS; i++)
		register_irq_proc(i);
}

/*****************************************************************************/
/*
 * initialise the interrupt system
 */
void __init init_IRQ(void)
{
	route_cpu_irqs();
	fpga_init();
#ifdef CONFIG_FUJITSU_MB93493
	route_mb93493_irqs();
#endif
} /* end init_IRQ() */

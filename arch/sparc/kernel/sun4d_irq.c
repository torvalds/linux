/*
 *  arch/sparc/kernel/sun4d_irq.c:
 *			SS1000/SC2000 interrupt handling.
 *
 *  Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *  Heavily based on arch/sparc/kernel/irq.c.
 */

#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/seq_file.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/psr.h>
#include <asm/smp.h>
#include <asm/vaddrs.h>
#include <asm/timer.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/traps.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/sbi.h>
#include <asm/cacheflush.h>
#include <asm/irq_regs.h>

#include "kernel.h"
#include "irq.h"

/* If you trust current SCSI layer to handle different SCSI IRQs, enable this. I don't trust it... -jj */
/* #define DISTRIBUTE_IRQS */

struct sun4d_timer_regs {
	u32	l10_timer_limit;
	u32	l10_cur_countx;
	u32	l10_limit_noclear;
	u32	ctrl;
	u32	l10_cur_count;
};

static struct sun4d_timer_regs __iomem *sun4d_timers;

#define TIMER_IRQ	10

#define MAX_STATIC_ALLOC	4
extern int static_irq_count;
static unsigned char sbus_tid[32];

static struct irqaction *irq_action[NR_IRQS];
extern spinlock_t irq_action_lock;

static struct sbus_action {
	struct irqaction *action;
	/* For SMP this needs to be extended */
} *sbus_actions;

static int pil_to_sbus[] = {
	0, 0, 1, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7, 0, 0,
};

static int sbus_to_pil[] = {
	0, 2, 3, 5, 7, 9, 11, 13,
};

static int nsbi;

/* Exported for sun4d_smp.c */
DEFINE_SPINLOCK(sun4d_imsk_lock);

int show_sun4d_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v, j = 0, k = 0, sbusl;
	struct irqaction * action;
	unsigned long flags;
#ifdef CONFIG_SMP
	int x;
#endif

	spin_lock_irqsave(&irq_action_lock, flags);
	if (i < NR_IRQS) {
		sbusl = pil_to_sbus[i];
		if (!sbusl) {
	 		action = *(i + irq_action);
			if (!action) 
		        	goto out_unlock;
		} else {
			for (j = 0; j < nsbi; j++) {
				for (k = 0; k < 4; k++)
					if ((action = sbus_actions [(j << 5) + (sbusl << 2) + k].action))
						goto found_it;
			}
			goto out_unlock;
		}
found_it:	seq_printf(p, "%3d: ", i);
#ifndef CONFIG_SMP
		seq_printf(p, "%10u ", kstat_irqs(i));
#else
		for_each_online_cpu(x)
			seq_printf(p, "%10u ",
			       kstat_cpu(cpu_logical_map(x)).irqs[i]);
#endif
		seq_printf(p, "%c %s",
			(action->flags & IRQF_DISABLED) ? '+' : ' ',
			action->name);
		action = action->next;
		for (;;) {
			for (; action; action = action->next) {
				seq_printf(p, ",%s %s",
					(action->flags & IRQF_DISABLED) ? " +" : "",
					action->name);
			}
			if (!sbusl) break;
			k++;
			if (k < 4)
				action = sbus_actions [(j << 5) + (sbusl << 2) + k].action;
			else {
				j++;
				if (j == nsbi) break;
				k = 0;
				action = sbus_actions [(j << 5) + (sbusl << 2)].action;
			}
		}
		seq_putc(p, '\n');
	}
out_unlock:
	spin_unlock_irqrestore(&irq_action_lock, flags);
	return 0;
}

void sun4d_free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction *action, **actionp;
	struct irqaction *tmp = NULL;
        unsigned long flags;

	spin_lock_irqsave(&irq_action_lock, flags);
	if (irq < 15)
		actionp = irq + irq_action;
	else
		actionp = &(sbus_actions[irq - (1 << 5)].action);
	action = *actionp;
	if (!action) {
		printk("Trying to free free IRQ%d\n",irq);
		goto out_unlock;
	}
	if (dev_id) {
		for (; action; action = action->next) {
			if (action->dev_id == dev_id)
				break;
			tmp = action;
		}
		if (!action) {
			printk("Trying to free free shared IRQ%d\n",irq);
			goto out_unlock;
		}
	} else if (action->flags & IRQF_SHARED) {
		printk("Trying to free shared IRQ%d with NULL device ID\n", irq);
		goto out_unlock;
	}
	if (action->flags & SA_STATIC_ALLOC)
	{
		/* This interrupt is marked as specially allocated
		 * so it is a bad idea to free it.
		 */
		printk("Attempt to free statically allocated IRQ%d (%s)\n",
		       irq, action->name);
		goto out_unlock;
	}
	
	if (action && tmp)
		tmp->next = action->next;
	else
		*actionp = action->next;

	spin_unlock_irqrestore(&irq_action_lock, flags);

	synchronize_irq(irq);

	spin_lock_irqsave(&irq_action_lock, flags);

	kfree(action);

	if (!(*actionp))
		__disable_irq(irq);

out_unlock:
	spin_unlock_irqrestore(&irq_action_lock, flags);
}

extern void unexpected_irq(int, void *, struct pt_regs *);

void sun4d_handler_irq(int irq, struct pt_regs * regs)
{
	struct pt_regs *old_regs;
	struct irqaction * action;
	int cpu = smp_processor_id();
	/* SBUS IRQ level (1 - 7) */
	int sbusl = pil_to_sbus[irq];
	
	/* FIXME: Is this necessary?? */
	cc_get_ipen();
	
	cc_set_iclr(1 << irq);
	
	old_regs = set_irq_regs(regs);
	irq_enter();
	kstat_cpu(cpu).irqs[irq]++;
	if (!sbusl) {
		action = *(irq + irq_action);
		if (!action)
			unexpected_irq(irq, NULL, regs);
		do {
			action->handler(irq, action->dev_id);
			action = action->next;
		} while (action);
	} else {
		int bus_mask = bw_get_intr_mask(sbusl) & 0x3ffff;
		int sbino;
		struct sbus_action *actionp;
		unsigned mask, slot;
		int sbil = (sbusl << 2);
		
		bw_clear_intr_mask(sbusl, bus_mask);
		
		/* Loop for each pending SBI */
		for (sbino = 0; bus_mask; sbino++, bus_mask >>= 1)
			if (bus_mask & 1) {
				mask = acquire_sbi(SBI2DEVID(sbino), 0xf << sbil);
				mask &= (0xf << sbil);
				actionp = sbus_actions + (sbino << 5) + (sbil);
				/* Loop for each pending SBI slot */
				for (slot = (1 << sbil); mask; slot <<= 1, actionp++)
					if (mask & slot) {
						mask &= ~slot;
						action = actionp->action;
						
						if (!action)
							unexpected_irq(irq, NULL, regs);
						do {
							action->handler(irq, action->dev_id);
							action = action->next;
						} while (action);
						release_sbi(SBI2DEVID(sbino), slot);
					}
			}
	}
	irq_exit();
	set_irq_regs(old_regs);
}

int sun4d_request_irq(unsigned int irq,
		irq_handler_t handler,
		unsigned long irqflags, const char * devname, void *dev_id)
{
	struct irqaction *action, *tmp = NULL, **actionp;
	unsigned long flags;
	int ret;
	
	if(irq > 14 && irq < (1 << 5)) {
		ret = -EINVAL;
		goto out;
	}

	if (!handler) {
		ret = -EINVAL;
		goto out;
	}

	spin_lock_irqsave(&irq_action_lock, flags);

	if (irq >= (1 << 5))
		actionp = &(sbus_actions[irq - (1 << 5)].action);
	else
		actionp = irq + irq_action;
	action = *actionp;
	
	if (action) {
		if ((action->flags & IRQF_SHARED) && (irqflags & IRQF_SHARED)) {
			for (tmp = action; tmp->next; tmp = tmp->next);
		} else {
			ret = -EBUSY;
			goto out_unlock;
		}
		if ((action->flags & IRQF_DISABLED) ^ (irqflags & IRQF_DISABLED)) {
			printk("Attempt to mix fast and slow interrupts on IRQ%d denied\n", irq);
			ret = -EBUSY;
			goto out_unlock;
		}
		action = NULL;		/* Or else! */
	}

	/* If this is flagged as statically allocated then we use our
	 * private struct which is never freed.
	 */
	if (irqflags & SA_STATIC_ALLOC) {
		if (static_irq_count < MAX_STATIC_ALLOC)
			action = &static_irqaction[static_irq_count++];
		else
			printk("Request for IRQ%d (%s) SA_STATIC_ALLOC failed using kmalloc\n", irq, devname);
	}
	
	if (action == NULL)
		action = kmalloc(sizeof(struct irqaction),
						     GFP_ATOMIC);
	
	if (!action) { 
		ret = -ENOMEM;
		goto out_unlock;
	}

	action->handler = handler;
	action->flags = irqflags;
	cpus_clear(action->mask);
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	if (tmp)
		tmp->next = action;
	else
		*actionp = action;
		
	__enable_irq(irq);

	ret = 0;
out_unlock:
	spin_unlock_irqrestore(&irq_action_lock, flags);
out:
	return ret;
}

static void sun4d_disable_irq(unsigned int irq)
{
	int tid = sbus_tid[(irq >> 5) - 1];
	unsigned long flags;
	
	if (irq < NR_IRQS)
		return;

	spin_lock_irqsave(&sun4d_imsk_lock, flags);
	cc_set_imsk_other(tid, cc_get_imsk_other(tid) | (1 << sbus_to_pil[(irq >> 2) & 7]));
	spin_unlock_irqrestore(&sun4d_imsk_lock, flags);
}

static void sun4d_enable_irq(unsigned int irq)
{
	int tid = sbus_tid[(irq >> 5) - 1];
	unsigned long flags;
	
	if (irq < NR_IRQS)
		return;

	spin_lock_irqsave(&sun4d_imsk_lock, flags);
	cc_set_imsk_other(tid, cc_get_imsk_other(tid) & ~(1 << sbus_to_pil[(irq >> 2) & 7]));
	spin_unlock_irqrestore(&sun4d_imsk_lock, flags);
}

#ifdef CONFIG_SMP
static void sun4d_set_cpu_int(int cpu, int level)
{
	sun4d_send_ipi(cpu, level);
}

static void sun4d_clear_ipi(int cpu, int level)
{
}

static void sun4d_set_udt(int cpu)
{
}

/* Setup IRQ distribution scheme. */
void __init sun4d_distribute_irqs(void)
{
	struct device_node *dp;

#ifdef DISTRIBUTE_IRQS
	cpumask_t sbus_serving_map;

	sbus_serving_map = cpu_present_map;
	for_each_node_by_name(dp, "sbi") {
		int board = of_getintprop_default(dp, "board#", 0);

		if ((board * 2) == boot_cpu_id && cpu_isset(board * 2 + 1, cpu_present_map))
			sbus_tid[board] = (board * 2 + 1);
		else if (cpu_isset(board * 2, cpu_present_map))
			sbus_tid[board] = (board * 2);
		else if (cpu_isset(board * 2 + 1, cpu_present_map))
			sbus_tid[board] = (board * 2 + 1);
		else
			sbus_tid[board] = 0xff;
		if (sbus_tid[board] != 0xff)
			cpu_clear(sbus_tid[board], sbus_serving_map);
	}
	for_each_node_by_name(dp, "sbi") {
		int board = of_getintprop_default(dp, "board#", 0);
		if (sbus_tid[board] == 0xff) {
			int i = 31;
				
			if (cpus_empty(sbus_serving_map))
				sbus_serving_map = cpu_present_map;
			while (cpu_isset(i, sbus_serving_map))
				i--;
			sbus_tid[board] = i;
			cpu_clear(i, sbus_serving_map);
		}
	}
	for_each_node_by_name(dp, "sbi") {
		int devid = of_getintprop_default(dp, "device-id", 0);
		int board = of_getintprop_default(dp, "board#", 0);
		printk("sbus%d IRQs directed to CPU%d\n", board, sbus_tid[board]);
		set_sbi_tid(devid, sbus_tid[board] << 3);
	}
#else
	int cpuid = cpu_logical_map(1);

	if (cpuid == -1)
		cpuid = cpu_logical_map(0);
	for_each_node_by_name(dp, "sbi") {
		int devid = of_getintprop_default(dp, "device-id", 0);
		int board = of_getintprop_default(dp, "board#", 0);
		sbus_tid[board] = cpuid;
		set_sbi_tid(devid, cpuid << 3);
	}
	printk("All sbus IRQs directed to CPU%d\n", cpuid);
#endif
}
#endif
 
static void sun4d_clear_clock_irq(void)
{
	sbus_readl(&sun4d_timers->l10_timer_limit);
}

static void sun4d_load_profile_irq(int cpu, unsigned int limit)
{
	bw_set_prof_limit(cpu, limit);
}

static void __init sun4d_load_profile_irqs(void)
{
	int cpu = 0, mid;

	while (!cpu_find_by_instance(cpu, NULL, &mid)) {
		sun4d_load_profile_irq(mid >> 3, 0);
		cpu++;
	}
}

static void __init sun4d_fixup_trap_table(void)
{
#ifdef CONFIG_SMP
	unsigned long flags;
	extern unsigned long lvl14_save[4];
	struct tt_entry *trap_table = &sparc_ttable[SP_TRAP_IRQ1 + (14 - 1)];
	extern unsigned int real_irq_entry[], smp4d_ticker[];
	extern unsigned int patchme_maybe_smp_msg[];

	/* Adjust so that we jump directly to smp4d_ticker */
	lvl14_save[2] += smp4d_ticker - real_irq_entry;

	/* For SMP we use the level 14 ticker, however the bootup code
	 * has copied the firmware's level 14 vector into the boot cpu's
	 * trap table, we must fix this now or we get squashed.
	 */
	local_irq_save(flags);
	patchme_maybe_smp_msg[0] = 0x01000000; /* NOP out the branch */
	trap_table->inst_one = lvl14_save[0];
	trap_table->inst_two = lvl14_save[1];
	trap_table->inst_three = lvl14_save[2];
	trap_table->inst_four = lvl14_save[3];
	local_flush_cache_all();
	local_irq_restore(flags);
#endif
}

static void __init sun4d_init_timers(irq_handler_t counter_fn)
{
	struct device_node *dp;
	struct resource res;
	const u32 *reg;
	int err;

	dp = of_find_node_by_name(NULL, "cpu-unit");
	if (!dp) {
		prom_printf("sun4d_init_timers: Unable to find cpu-unit\n");
		prom_halt();
	}

	/* Which cpu-unit we use is arbitrary, we can view the bootbus timer
	 * registers via any cpu's mapping.  The first 'reg' property is the
	 * bootbus.
	 */
	reg = of_get_property(dp, "reg", NULL);
	of_node_put(dp);
	if (!reg) {
		prom_printf("sun4d_init_timers: No reg property\n");
		prom_halt();
	}

	res.start = reg[1];
	res.end = reg[2] - 1;
	res.flags = reg[0] & 0xff;
	sun4d_timers = of_ioremap(&res, BW_TIMER_LIMIT,
				  sizeof(struct sun4d_timer_regs), "user timer");
	if (!sun4d_timers) {
		prom_printf("sun4d_init_timers: Can't map timer regs\n");
		prom_halt();
	}

	sbus_writel((((1000000/HZ) + 1) << 10), &sun4d_timers->l10_timer_limit);

	master_l10_counter = &sun4d_timers->l10_cur_count;

	err = request_irq(TIMER_IRQ, counter_fn,
			  (IRQF_DISABLED | SA_STATIC_ALLOC),
			  "timer", NULL);
	if (err) {
		prom_printf("sun4d_init_timers: request_irq() failed with %d\n", err);
		prom_halt();
	}
	sun4d_load_profile_irqs();
	sun4d_fixup_trap_table();
}

void __init sun4d_init_sbi_irq(void)
{
	struct device_node *dp;
	int target_cpu = 0;

#ifdef CONFIG_SMP
	target_cpu = boot_cpu_id;
#endif

	nsbi = 0;
	for_each_node_by_name(dp, "sbi")
		nsbi++;
	sbus_actions = kzalloc (nsbi * 8 * 4 * sizeof(struct sbus_action), GFP_ATOMIC);
	if (!sbus_actions) {
		prom_printf("SUN4D: Cannot allocate sbus_actions, halting.\n");
		prom_halt();
	}
	for_each_node_by_name(dp, "sbi") {
		int devid = of_getintprop_default(dp, "device-id", 0);
		int board = of_getintprop_default(dp, "board#", 0);
		unsigned int mask;

		set_sbi_tid(devid, target_cpu << 3);
		sbus_tid[board] = target_cpu;

		/* Get rid of pending irqs from PROM */
		mask = acquire_sbi(devid, 0xffffffff);
		if (mask) {
			printk ("Clearing pending IRQs %08x on SBI %d\n", mask, board);
			release_sbi(devid, mask);
		}
	}
}

void __init sun4d_init_IRQ(void)
{
	local_irq_disable();

	BTFIXUPSET_CALL(enable_irq, sun4d_enable_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(disable_irq, sun4d_disable_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_clock_irq, sun4d_clear_clock_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(load_profile_irq, sun4d_load_profile_irq, BTFIXUPCALL_NORM);
	sparc_init_timers = sun4d_init_timers;
#ifdef CONFIG_SMP
	BTFIXUPSET_CALL(set_cpu_int, sun4d_set_cpu_int, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_cpu_int, sun4d_clear_ipi, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(set_irq_udt, sun4d_set_udt, BTFIXUPCALL_NOP);
#endif
	/* Cannot enable interrupts until OBP ticker is disabled. */
}

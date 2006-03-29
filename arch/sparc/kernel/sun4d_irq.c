/*  $Id: sun4d_irq.c,v 1.29 2001/12/11 04:55:51 davem Exp $
 *  arch/sparc/kernel/sun4d_irq.c:
 *			SS1000/SC2000 interrupt handling.
 *
 *  Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *  Heavily based on arch/sparc/kernel/irq.c.
 */

#include <linux/config.h>
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
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/seq_file.h>

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
#include <asm/sbus.h>
#include <asm/sbi.h>
#include <asm/cacheflush.h>

/* If you trust current SCSI layer to handle different SCSI IRQs, enable this. I don't trust it... -jj */
/* #define DISTRIBUTE_IRQS */

struct sun4d_timer_regs *sun4d_timers;
#define TIMER_IRQ	10

#define MAX_STATIC_ALLOC	4
extern struct irqaction static_irqaction[MAX_STATIC_ALLOC];
extern int static_irq_count;
unsigned char cpu_leds[32];
#ifdef CONFIG_SMP
unsigned char sbus_tid[32];
#endif

static struct irqaction *irq_action[NR_IRQS];
extern spinlock_t irq_action_lock;

struct sbus_action {
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
#ifdef CONFIG_SMP
DEFINE_SPINLOCK(sun4d_imsk_lock);
#endif

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
			(action->flags & SA_INTERRUPT) ? '+' : ' ',
			action->name);
		action = action->next;
		for (;;) {
			for (; action; action = action->next) {
				seq_printf(p, ",%s %s",
					(action->flags & SA_INTERRUPT) ? " +" : "",
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
	} else if (action->flags & SA_SHIRQ) {
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
		disable_irq(irq);

out_unlock:
	spin_unlock_irqrestore(&irq_action_lock, flags);
}

extern void unexpected_irq(int, void *, struct pt_regs *);

void sun4d_handler_irq(int irq, struct pt_regs * regs)
{
	struct irqaction * action;
	int cpu = smp_processor_id();
	/* SBUS IRQ level (1 - 7) */
	int sbusl = pil_to_sbus[irq];
	
	/* FIXME: Is this necessary?? */
	cc_get_ipen();
	
	cc_set_iclr(1 << irq);
	
	irq_enter();
	kstat_cpu(cpu).irqs[irq]++;
	if (!sbusl) {
		action = *(irq + irq_action);
		if (!action)
			unexpected_irq(irq, NULL, regs);
		do {
			action->handler(irq, action->dev_id, regs);
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
							action->handler(irq, action->dev_id, regs);
							action = action->next;
						} while (action);
						release_sbi(SBI2DEVID(sbino), slot);
					}
			}
	}
	irq_exit();
}

unsigned int sun4d_build_irq(struct sbus_dev *sdev, int irq)
{
	int sbusl = pil_to_sbus[irq];

	if (sbusl)
		return ((sdev->bus->board + 1) << 5) + (sbusl << 2) + sdev->slot;
	else
		return irq;
}

unsigned int sun4d_sbint_to_irq(struct sbus_dev *sdev, unsigned int sbint)
{
	if (sbint >= sizeof(sbus_to_pil)) {
		printk(KERN_ERR "%s: bogus SBINT %d\n", sdev->prom_name, sbint);
		BUG();
	}
	return sun4d_build_irq(sdev, sbus_to_pil[sbint]);
}

int sun4d_request_irq(unsigned int irq,
		irqreturn_t (*handler)(int, void *, struct pt_regs *),
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
		if ((action->flags & SA_SHIRQ) && (irqflags & SA_SHIRQ)) {
			for (tmp = action; tmp->next; tmp = tmp->next);
		} else {
			ret = -EBUSY;
			goto out_unlock;
		}
		if ((action->flags & SA_INTERRUPT) ^ (irqflags & SA_INTERRUPT)) {
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
		action = (struct irqaction *)kmalloc(sizeof(struct irqaction),
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
		
	enable_irq(irq);

	ret = 0;
out_unlock:
	spin_unlock_irqrestore(&irq_action_lock, flags);
out:
	return ret;
}

static void sun4d_disable_irq(unsigned int irq)
{
#ifdef CONFIG_SMP
	int tid = sbus_tid[(irq >> 5) - 1];
	unsigned long flags;
#endif	
	
	if (irq < NR_IRQS) return;
#ifdef CONFIG_SMP
	spin_lock_irqsave(&sun4d_imsk_lock, flags);
	cc_set_imsk_other(tid, cc_get_imsk_other(tid) | (1 << sbus_to_pil[(irq >> 2) & 7]));
	spin_unlock_irqrestore(&sun4d_imsk_lock, flags);
#else		
	cc_set_imsk(cc_get_imsk() | (1 << sbus_to_pil[(irq >> 2) & 7]));
#endif
}

static void sun4d_enable_irq(unsigned int irq)
{
#ifdef CONFIG_SMP
	int tid = sbus_tid[(irq >> 5) - 1];
	unsigned long flags;
#endif	
	
	if (irq < NR_IRQS) return;
#ifdef CONFIG_SMP
	spin_lock_irqsave(&sun4d_imsk_lock, flags);
	cc_set_imsk_other(tid, cc_get_imsk_other(tid) & ~(1 << sbus_to_pil[(irq >> 2) & 7]));
	spin_unlock_irqrestore(&sun4d_imsk_lock, flags);
#else		
	cc_set_imsk(cc_get_imsk() & ~(1 << sbus_to_pil[(irq >> 2) & 7]));
#endif
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
#ifdef DISTRIBUTE_IRQS
	struct sbus_bus *sbus;
	unsigned long sbus_serving_map;

	sbus_serving_map = cpu_present_map;
	for_each_sbus(sbus) {
		if ((sbus->board * 2) == boot_cpu_id && (cpu_present_map & (1 << (sbus->board * 2 + 1))))
			sbus_tid[sbus->board] = (sbus->board * 2 + 1);
		else if (cpu_present_map & (1 << (sbus->board * 2)))
			sbus_tid[sbus->board] = (sbus->board * 2);
		else if (cpu_present_map & (1 << (sbus->board * 2 + 1)))
			sbus_tid[sbus->board] = (sbus->board * 2 + 1);
		else
			sbus_tid[sbus->board] = 0xff;
		if (sbus_tid[sbus->board] != 0xff)
			sbus_serving_map &= ~(1 << sbus_tid[sbus->board]);
	}
	for_each_sbus(sbus)
		if (sbus_tid[sbus->board] == 0xff) {
			int i = 31;
				
			if (!sbus_serving_map)
				sbus_serving_map = cpu_present_map;
			while (!(sbus_serving_map & (1 << i)))
				i--;
			sbus_tid[sbus->board] = i;
			sbus_serving_map &= ~(1 << i);
		}
	for_each_sbus(sbus) {
		printk("sbus%d IRQs directed to CPU%d\n", sbus->board, sbus_tid[sbus->board]);
		set_sbi_tid(sbus->devid, sbus_tid[sbus->board] << 3);
	}
#else
	struct sbus_bus *sbus;
	int cpuid = cpu_logical_map(1);

	if (cpuid == -1)
		cpuid = cpu_logical_map(0);
	for_each_sbus(sbus) {
		sbus_tid[sbus->board] = cpuid;
		set_sbi_tid(sbus->devid, cpuid << 3);
	}
	printk("All sbus IRQs directed to CPU%d\n", cpuid);
#endif
}
#endif
 
static void sun4d_clear_clock_irq(void)
{
	volatile unsigned int clear_intr;
	clear_intr = sun4d_timers->l10_timer_limit;
}

static void sun4d_clear_profile_irq(int cpu)
{
	bw_get_prof_limit(cpu);
}

static void sun4d_load_profile_irq(int cpu, unsigned int limit)
{
	bw_set_prof_limit(cpu, limit);
}

static void __init sun4d_init_timers(irqreturn_t (*counter_fn)(int, void *, struct pt_regs *))
{
	int irq;
	int cpu;
	struct resource r;
	int mid;

	/* Map the User Timer registers. */
	memset(&r, 0, sizeof(r));
#ifdef CONFIG_SMP
	r.start = CSR_BASE(boot_cpu_id)+BW_TIMER_LIMIT;
#else
	r.start = CSR_BASE(0)+BW_TIMER_LIMIT;
#endif
	r.flags = 0xf;
	sun4d_timers = (struct sun4d_timer_regs *) sbus_ioremap(&r, 0,
	    PAGE_SIZE, "user timer");

	sun4d_timers->l10_timer_limit =  (((1000000/HZ) + 1) << 10);
	master_l10_counter = &sun4d_timers->l10_cur_count;
	master_l10_limit = &sun4d_timers->l10_timer_limit;

	irq = request_irq(TIMER_IRQ,
			  counter_fn,
			  (SA_INTERRUPT | SA_STATIC_ALLOC),
			  "timer", NULL);
	if (irq) {
		prom_printf("time_init: unable to attach IRQ%d\n",TIMER_IRQ);
		prom_halt();
	}
	
	/* Enable user timer free run for CPU 0 in BW */
	/* bw_set_ctrl(0, bw_get_ctrl(0) | BW_CTRL_USER_TIMER); */

	cpu = 0;
	while (!cpu_find_by_instance(cpu, NULL, &mid)) {
		sun4d_load_profile_irq(mid >> 3, 0);
		cpu++;
	}
		
#ifdef CONFIG_SMP
	{
		unsigned long flags;
		extern unsigned long lvl14_save[4];
		struct tt_entry *trap_table = &sparc_ttable[SP_TRAP_IRQ1 + (14 - 1)];
		extern unsigned int real_irq_entry[], smp4d_ticker[];
		extern unsigned int patchme_maybe_smp_msg[];

		/* Adjust so that we jump directly to smp4d_ticker */
		lvl14_save[2] += smp4d_ticker - real_irq_entry;

		/* For SMP we use the level 14 ticker, however the bootup code
		 * has copied the firmwares level 14 vector into boot cpu's
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
	}
#endif
}

void __init sun4d_init_sbi_irq(void)
{
	struct sbus_bus *sbus;
	unsigned mask;

	nsbi = 0;
	for_each_sbus(sbus)
		nsbi++;
	sbus_actions = (struct sbus_action *)kmalloc (nsbi * 8 * 4 * sizeof(struct sbus_action), GFP_ATOMIC);
	memset (sbus_actions, 0, (nsbi * 8 * 4 * sizeof(struct sbus_action)));
	for_each_sbus(sbus) {
#ifdef CONFIG_SMP	
		extern unsigned char boot_cpu_id;
		
		set_sbi_tid(sbus->devid, boot_cpu_id << 3);
		sbus_tid[sbus->board] = boot_cpu_id;
#endif
		/* Get rid of pending irqs from PROM */
		mask = acquire_sbi(sbus->devid, 0xffffffff);
		if (mask) {
			printk ("Clearing pending IRQs %08x on SBI %d\n", mask, sbus->board);
			release_sbi(sbus->devid, mask);
		}
	}
}

static char *sun4d_irq_itoa(unsigned int irq)
{
	static char buff[16];
	
	if (irq < (1 << 5))
		sprintf(buff, "%d", irq);
	else
		sprintf(buff, "%d,%x", sbus_to_pil[(irq >> 2) & 7], irq);
	return buff;
}

void __init sun4d_init_IRQ(void)
{
	local_irq_disable();

	BTFIXUPSET_CALL(sbint_to_irq, sun4d_sbint_to_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(enable_irq, sun4d_enable_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(disable_irq, sun4d_disable_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_clock_irq, sun4d_clear_clock_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_profile_irq, sun4d_clear_profile_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(load_profile_irq, sun4d_load_profile_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(__irq_itoa, sun4d_irq_itoa, BTFIXUPCALL_NORM);
	sparc_init_timers = sun4d_init_timers;
#ifdef CONFIG_SMP
	BTFIXUPSET_CALL(set_cpu_int, sun4d_set_cpu_int, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_cpu_int, sun4d_clear_ipi, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(set_irq_udt, sun4d_set_udt, BTFIXUPCALL_NOP);
#endif
	/* Cannot enable interrupts until OBP ticker is disabled. */
}

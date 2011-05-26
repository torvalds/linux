/*
 * Copyright (C) 2009 Daniel Hellstrom (daniel@gaisler.com) Aeroflex Gaisler AB
 * Copyright (C) 2009 Konrad Eisele (konrad@gaisler.com) Aeroflex Gaisler AB
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/interrupt.h>
#include <linux/of_device.h>

#include <asm/oplib.h>
#include <asm/timer.h>
#include <asm/prom.h>
#include <asm/leon.h>
#include <asm/leon_amba.h>
#include <asm/traps.h>
#include <asm/cacheflush.h>

#include "prom.h"
#include "irq.h"

struct leon3_irqctrl_regs_map *leon3_irqctrl_regs; /* interrupt controller base address */
struct leon3_gptimer_regs_map *leon3_gptimer_regs; /* timer controller base address */
struct amba_apb_device leon_percpu_timer_dev[16];

int leondebug_irq_disable;
int leon_debug_irqout;
static int dummy_master_l10_counter;
unsigned long amba_system_id;

unsigned long leon3_gptimer_irq; /* interrupt controller irq number */
unsigned long leon3_gptimer_idx; /* Timer Index (0..6) within Timer Core */
unsigned int sparc_leon_eirq;
#define LEON_IMASK ((&leon3_irqctrl_regs->mask[0]))

/* Return the IRQ of the pending IRQ on the extended IRQ controller */
int sparc_leon_eirq_get(int eirq, int cpu)
{
	return LEON3_BYPASS_LOAD_PA(&leon3_irqctrl_regs->intid[cpu]) & 0x1f;
}

irqreturn_t sparc_leon_eirq_isr(int dummy, void *dev_id)
{
	printk(KERN_ERR "sparc_leon_eirq_isr: ERROR EXTENDED IRQ\n");
	return IRQ_HANDLED;
}

/* The extended IRQ controller has been found, this function registers it */
void sparc_leon_eirq_register(int eirq)
{
	int irq;

	/* Register a "BAD" handler for this interrupt, it should never happen */
	irq = request_irq(eirq, sparc_leon_eirq_isr,
			  (IRQF_DISABLED | SA_STATIC_ALLOC), "extirq", NULL);

	if (irq) {
		printk(KERN_ERR
		       "sparc_leon_eirq_register: unable to attach IRQ%d\n",
		       eirq);
	} else {
		sparc_leon_eirq = eirq;
	}

}

static inline unsigned long get_irqmask(unsigned int irq)
{
	unsigned long mask;

	if (!irq || ((irq > 0xf) && !sparc_leon_eirq)
	    || ((irq > 0x1f) && sparc_leon_eirq)) {
		printk(KERN_ERR
		       "leon_get_irqmask: false irq number: %d\n", irq);
		mask = 0;
	} else {
		mask = LEON_HARD_INT(irq);
	}
	return mask;
}

static void leon_enable_irq(unsigned int irq_nr)
{
	unsigned long mask, flags;
	mask = get_irqmask(irq_nr);
	local_irq_save(flags);
	LEON3_BYPASS_STORE_PA(LEON_IMASK,
			      (LEON3_BYPASS_LOAD_PA(LEON_IMASK) | (mask)));
	local_irq_restore(flags);
}

static void leon_disable_irq(unsigned int irq_nr)
{
	unsigned long mask, flags;
	mask = get_irqmask(irq_nr);
	local_irq_save(flags);
	LEON3_BYPASS_STORE_PA(LEON_IMASK,
			      (LEON3_BYPASS_LOAD_PA(LEON_IMASK) & ~(mask)));
	local_irq_restore(flags);

}

void __init leon_init_timers(irq_handler_t counter_fn)
{
	int irq;
	struct device_node *rootnp, *np, *nnp;
	struct property *pp;
	int len;
	int cpu, icsel;
	int ampopts;

	leondebug_irq_disable = 0;
	leon_debug_irqout = 0;
	master_l10_counter = (unsigned int *)&dummy_master_l10_counter;
	dummy_master_l10_counter = 0;

	rootnp = of_find_node_by_path("/ambapp0");
	if (!rootnp)
		goto bad;

	/* Find System ID: GRLIB build ID and optional CHIP ID */
	pp = of_find_property(rootnp, "systemid", &len);
	if (pp)
		amba_system_id = *(unsigned long *)pp->value;

	/* Find IRQMP IRQ Controller Registers base adr otherwise bail out */
	np = of_find_node_by_name(rootnp, "GAISLER_IRQMP");
	if (!np) {
		np = of_find_node_by_name(rootnp, "01_00d");
		if (!np)
			goto bad;
	}
	pp = of_find_property(np, "reg", &len);
	if (!pp)
		goto bad;
	leon3_irqctrl_regs = *(struct leon3_irqctrl_regs_map **)pp->value;

	/* Find GPTIMER Timer Registers base address otherwise bail out. */
	nnp = rootnp;
	do {
		np = of_find_node_by_name(nnp, "GAISLER_GPTIMER");
		if (!np) {
			np = of_find_node_by_name(nnp, "01_011");
			if (!np)
				goto bad;
		}

		ampopts = 0;
		pp = of_find_property(np, "ampopts", &len);
		if (pp) {
			ampopts = *(int *)pp->value;
			if (ampopts == 0) {
				/* Skip this instance, resource already
				 * allocated by other OS */
				nnp = np;
				continue;
			}
		}

		/* Select Timer-Instance on Timer Core. Default is zero */
		leon3_gptimer_idx = ampopts & 0x7;

		pp = of_find_property(np, "reg", &len);
		if (pp)
			leon3_gptimer_regs = *(struct leon3_gptimer_regs_map **)
						pp->value;
		pp = of_find_property(np, "interrupts", &len);
		if (pp)
			leon3_gptimer_irq = *(unsigned int *)pp->value;
	} while (0);

	if (leon3_gptimer_regs && leon3_irqctrl_regs && leon3_gptimer_irq) {
		LEON3_BYPASS_STORE_PA(
			&leon3_gptimer_regs->e[leon3_gptimer_idx].val, 0);
		LEON3_BYPASS_STORE_PA(
			&leon3_gptimer_regs->e[leon3_gptimer_idx].rld,
			(((1000000 / HZ) - 1)));
		LEON3_BYPASS_STORE_PA(
			&leon3_gptimer_regs->e[leon3_gptimer_idx].ctrl, 0);

#ifdef CONFIG_SMP
		leon_percpu_timer_dev[0].start = (int)leon3_gptimer_regs;
		leon_percpu_timer_dev[0].irq = leon3_gptimer_irq + 1 +
					       leon3_gptimer_idx;

		if (!(LEON3_BYPASS_LOAD_PA(&leon3_gptimer_regs->config) &
		      (1<<LEON3_GPTIMER_SEPIRQ))) {
			prom_printf("irq timer not configured with separate irqs\n");
			BUG();
		}

		LEON3_BYPASS_STORE_PA(
			&leon3_gptimer_regs->e[leon3_gptimer_idx+1].val, 0);
		LEON3_BYPASS_STORE_PA(
			&leon3_gptimer_regs->e[leon3_gptimer_idx+1].rld,
			(((1000000/HZ) - 1)));
		LEON3_BYPASS_STORE_PA(
			&leon3_gptimer_regs->e[leon3_gptimer_idx+1].ctrl, 0);
# endif

		/*
		 * The IRQ controller may (if implemented) consist of multiple
		 * IRQ controllers, each mapped on a 4Kb boundary.
		 * Each CPU may be routed to different IRQCTRLs, however
		 * we assume that all CPUs (in SMP system) is routed to the
		 * same IRQ Controller, and for non-SMP only one IRQCTRL is
		 * accessed anyway.
		 * In AMP systems, Linux must run on CPU0 for the time being.
		 */
		cpu = sparc_leon3_cpuid();
		icsel = LEON3_BYPASS_LOAD_PA(&leon3_irqctrl_regs->icsel[cpu/8]);
		icsel = (icsel >> ((7 - (cpu&0x7)) * 4)) & 0xf;
		leon3_irqctrl_regs += icsel;
	} else {
		goto bad;
	}

	irq = request_irq(leon3_gptimer_irq+leon3_gptimer_idx,
			  counter_fn,
			  (IRQF_DISABLED | SA_STATIC_ALLOC), "timer", NULL);

	if (irq) {
		printk(KERN_ERR "leon_time_init: unable to attach IRQ%d\n",
		       LEON_INTERRUPT_TIMER1);
		prom_halt();
	}

# ifdef CONFIG_SMP
	{
		unsigned long flags;
		struct tt_entry *trap_table = &sparc_ttable[SP_TRAP_IRQ1 + (leon_percpu_timer_dev[0].irq - 1)];

		/* For SMP we use the level 14 ticker, however the bootup code
		 * has copied the firmwares level 14 vector into boot cpu's
		 * trap table, we must fix this now or we get squashed.
		 */
		local_irq_save(flags);

		patchme_maybe_smp_msg[0] = 0x01000000; /* NOP out the branch */

		/* Adjust so that we jump directly to smpleon_ticker */
		trap_table->inst_three += smpleon_ticker - real_irq_entry;

		local_flush_cache_all();
		local_irq_restore(flags);
	}
# endif

	if (leon3_gptimer_regs) {
		LEON3_BYPASS_STORE_PA(&leon3_gptimer_regs->e[leon3_gptimer_idx].ctrl,
				      LEON3_GPTIMER_EN |
				      LEON3_GPTIMER_RL |
				      LEON3_GPTIMER_LD | LEON3_GPTIMER_IRQEN);

#ifdef CONFIG_SMP
		LEON3_BYPASS_STORE_PA(&leon3_gptimer_regs->e[leon3_gptimer_idx+1].ctrl,
				      LEON3_GPTIMER_EN |
				      LEON3_GPTIMER_RL |
				      LEON3_GPTIMER_LD |
				      LEON3_GPTIMER_IRQEN);
#endif

	}
	return;
bad:
	printk(KERN_ERR "No Timer/irqctrl found\n");
	BUG();
	return;
}

void leon_clear_clock_irq(void)
{
}

void leon_load_profile_irq(int cpu, unsigned int limit)
{
	BUG();
}




void __init leon_trans_init(struct device_node *dp)
{
	if (strcmp(dp->type, "cpu") == 0 && strcmp(dp->name, "<NULL>") == 0) {
		struct property *p;
		p = of_find_property(dp, "mid", (void *)0);
		if (p) {
			int mid;
			dp->name = prom_early_alloc(5 + 1);
			memcpy(&mid, p->value, p->length);
			sprintf((char *)dp->name, "cpu%.2d", mid);
		}
	}
}

void __initdata (*prom_amba_init)(struct device_node *dp, struct device_node ***nextp) = 0;

void __init leon_node_init(struct device_node *dp, struct device_node ***nextp)
{
	if (prom_amba_init &&
	    strcmp(dp->type, "ambapp") == 0 &&
	    strcmp(dp->name, "ambapp0") == 0) {
		prom_amba_init(dp, nextp);
	}
}

#ifdef CONFIG_SMP

void leon_set_cpu_int(int cpu, int level)
{
	unsigned long mask;
	mask = get_irqmask(level);
	LEON3_BYPASS_STORE_PA(&leon3_irqctrl_regs->force[cpu], mask);
}

static void leon_clear_ipi(int cpu, int level)
{
	unsigned long mask;
	mask = get_irqmask(level);
	LEON3_BYPASS_STORE_PA(&leon3_irqctrl_regs->force[cpu], mask<<16);
}

static void leon_set_udt(int cpu)
{
}

void leon_clear_profile_irq(int cpu)
{
}

void leon_enable_irq_cpu(unsigned int irq_nr, unsigned int cpu)
{
	unsigned long mask, flags, *addr;
	mask = get_irqmask(irq_nr);
	local_irq_save(flags);
	addr = (unsigned long *)&(leon3_irqctrl_regs->mask[cpu]);
	LEON3_BYPASS_STORE_PA(addr, (LEON3_BYPASS_LOAD_PA(addr) | (mask)));
	local_irq_restore(flags);
}

#endif

void __init leon_init_IRQ(void)
{
	sparc_irq_config.init_timers = leon_init_timers;

	BTFIXUPSET_CALL(enable_irq, leon_enable_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(disable_irq, leon_disable_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(enable_pil_irq, leon_enable_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(disable_pil_irq, leon_disable_irq, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(clear_clock_irq, leon_clear_clock_irq,
			BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(load_profile_irq, leon_load_profile_irq,
			BTFIXUPCALL_NOP);

#ifdef CONFIG_SMP
	BTFIXUPSET_CALL(set_cpu_int, leon_set_cpu_int, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_cpu_int, leon_clear_ipi, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(set_irq_udt, leon_set_udt, BTFIXUPCALL_NORM);
#endif

}

void __init leon_init(void)
{
	of_pdt_build_more = &leon_node_init;
}

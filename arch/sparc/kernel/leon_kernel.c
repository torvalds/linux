/*
 * Copyright (C) 2009 Daniel Hellstrom (daniel@gaisler.com) Aeroflex Gaisler AB
 * Copyright (C) 2009 Konrad Eisele (konrad@gaisler.com) Aeroflex Gaisler AB
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/interrupt.h>
#include <linux/of_device.h>
#include <asm/oplib.h>
#include <asm/timer.h>
#include <asm/prom.h>
#include <asm/leon.h>
#include <asm/leon_amba.h>

#include "prom.h"
#include "irq.h"

struct leon3_irqctrl_regs_map *leon3_irqctrl_regs; /* interrupt controller base address, initialized by amba_init() */
struct leon3_gptimer_regs_map *leon3_gptimer_regs; /* timer controller base address, initialized by amba_init() */
struct amba_apb_device leon_percpu_timer_dev[16];

int leondebug_irq_disable;
int leon_debug_irqout;
static int dummy_master_l10_counter;

unsigned long leon3_gptimer_irq; /* interrupt controller irq number, initialized by amba_init() */
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

	leondebug_irq_disable = 0;
	leon_debug_irqout = 0;
	master_l10_counter = (unsigned int *)&dummy_master_l10_counter;
	dummy_master_l10_counter = 0;

	if (leon3_gptimer_regs && leon3_irqctrl_regs) {
		LEON3_BYPASS_STORE_PA(&leon3_gptimer_regs->e[0].val, 0);
		LEON3_BYPASS_STORE_PA(&leon3_gptimer_regs->e[0].rld,
				      (((1000000 / 100) - 1)));
		LEON3_BYPASS_STORE_PA(&leon3_gptimer_regs->e[0].ctrl, 0);

	} else {
		printk(KERN_ERR "No Timer/irqctrl found\n");
		BUG();
	}

	irq = request_irq(leon3_gptimer_irq,
			  counter_fn,
			  (IRQF_DISABLED | SA_STATIC_ALLOC), "timer", NULL);

	if (irq) {
		printk(KERN_ERR "leon_time_init: unable to attach IRQ%d\n",
		       LEON_INTERRUPT_TIMER1);
		prom_halt();
	}

	if (leon3_gptimer_regs) {
		LEON3_BYPASS_STORE_PA(&leon3_gptimer_regs->e[0].ctrl,
				      LEON3_GPTIMER_EN |
				      LEON3_GPTIMER_RL |
				      LEON3_GPTIMER_LD | LEON3_GPTIMER_IRQEN);
	}
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

void __init leon_init_IRQ(void)
{
	sparc_init_timers = leon_init_timers;

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
	prom_build_more = &leon_node_init;
}

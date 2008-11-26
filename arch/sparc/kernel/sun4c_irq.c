/*  sun4c_irq.c
 *  arch/sparc/kernel/sun4c_irq.c:
 *
 *  djhr: Hacked out of irq.c into a CPU dependent version.
 *
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1995 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *  Copyright (C) 1995 Pete A. Zaitcev (zaitcev@yahoo.com)
 *  Copyright (C) 1996 Dave Redman (djhr@tadpole.co.uk)
 */

#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include "irq.h"

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/psr.h>
#include <asm/vaddrs.h>
#include <asm/timer.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/traps.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/idprom.h>
#include <asm/machines.h>

/*
 * Bit field defines for the interrupt registers on various
 * Sparc machines.
 */

/* The sun4c interrupt register. */
#define SUN4C_INT_ENABLE  0x01     /* Allow interrupts. */
#define SUN4C_INT_E14     0x80     /* Enable level 14 IRQ. */
#define SUN4C_INT_E10     0x20     /* Enable level 10 IRQ. */
#define SUN4C_INT_E8      0x10     /* Enable level 8 IRQ. */
#define SUN4C_INT_E6      0x08     /* Enable level 6 IRQ. */
#define SUN4C_INT_E4      0x04     /* Enable level 4 IRQ. */
#define SUN4C_INT_E1      0x02     /* Enable level 1 IRQ. */

/* Pointer to the interrupt enable byte
 *
 * Dave Redman (djhr@tadpole.co.uk)
 * What you may not be aware of is that entry.S requires this variable.
 *
 *  --- linux_trap_nmi_sun4c --
 *
 * so don't go making it static, like I tried. sigh.
 */
unsigned char __iomem *interrupt_enable = NULL;

static void sun4c_disable_irq(unsigned int irq_nr)
{
	unsigned long flags;
	unsigned char current_mask, new_mask;
    
	local_irq_save(flags);
	irq_nr &= (NR_IRQS - 1);
	current_mask = sbus_readb(interrupt_enable);
	switch(irq_nr) {
	case 1:
		new_mask = ((current_mask) & (~(SUN4C_INT_E1)));
		break;
	case 8:
		new_mask = ((current_mask) & (~(SUN4C_INT_E8)));
		break;
	case 10:
		new_mask = ((current_mask) & (~(SUN4C_INT_E10)));
		break;
	case 14:
		new_mask = ((current_mask) & (~(SUN4C_INT_E14)));
		break;
	default:
		local_irq_restore(flags);
		return;
	}
	sbus_writeb(new_mask, interrupt_enable);
	local_irq_restore(flags);
}

static void sun4c_enable_irq(unsigned int irq_nr)
{
	unsigned long flags;
	unsigned char current_mask, new_mask;
    
	local_irq_save(flags);
	irq_nr &= (NR_IRQS - 1);
	current_mask = sbus_readb(interrupt_enable);
	switch(irq_nr) {
	case 1:
		new_mask = ((current_mask) | SUN4C_INT_E1);
		break;
	case 8:
		new_mask = ((current_mask) | SUN4C_INT_E8);
		break;
	case 10:
		new_mask = ((current_mask) | SUN4C_INT_E10);
		break;
	case 14:
		new_mask = ((current_mask) | SUN4C_INT_E14);
		break;
	default:
		local_irq_restore(flags);
		return;
	}
	sbus_writeb(new_mask, interrupt_enable);
	local_irq_restore(flags);
}

struct sun4c_timer_info {
	u32		l10_count;
	u32		l10_limit;
	u32		l14_count;
	u32		l14_limit;
};

static struct sun4c_timer_info __iomem *sun4c_timers;

static void sun4c_clear_clock_irq(void)
{
	sbus_readl(&sun4c_timers->l10_limit);
}

static void sun4c_load_profile_irq(int cpu, unsigned int limit)
{
	/* Errm.. not sure how to do this.. */
}

static void __init sun4c_init_timers(irq_handler_t counter_fn)
{
	const struct linux_prom_irqs *irq;
	struct device_node *dp;
	const u32 *addr;
	int err;

	dp = of_find_node_by_name(NULL, "counter-timer");
	if (!dp) {
		prom_printf("sun4c_init_timers: Unable to find counter-timer\n");
		prom_halt();
	}

	addr = of_get_property(dp, "address", NULL);
	if (!addr) {
		prom_printf("sun4c_init_timers: No address property\n");
		prom_halt();
	}

	sun4c_timers = (void __iomem *) (unsigned long) addr[0];

	irq = of_get_property(dp, "intr", NULL);
	if (!irq) {
		prom_printf("sun4c_init_timers: No intr property\n");
		prom_halt();
	}

	/* Have the level 10 timer tick at 100HZ.  We don't touch the
	 * level 14 timer limit since we are letting the prom handle
	 * them until we have a real console driver so L1-A works.
	 */
	sbus_writel((((1000000/HZ) + 1) << 10), &sun4c_timers->l10_limit);

	master_l10_counter = &sun4c_timers->l10_count;

	err = request_irq(irq[0].pri, counter_fn,
			  (IRQF_DISABLED | SA_STATIC_ALLOC),
			  "timer", NULL);
	if (err) {
		prom_printf("sun4c_init_timers: request_irq() fails with %d\n", err);
		prom_halt();
	}
    
	sun4c_disable_irq(irq[1].pri);
}

#ifdef CONFIG_SMP
static void sun4c_nop(void) {}
#endif

void __init sun4c_init_IRQ(void)
{
	struct device_node *dp;
	const u32 *addr;

	dp = of_find_node_by_name(NULL, "interrupt-enable");
	if (!dp) {
		prom_printf("sun4c_init_IRQ: Unable to find interrupt-enable\n");
		prom_halt();
	}

	addr = of_get_property(dp, "address", NULL);
	if (!addr) {
		prom_printf("sun4c_init_IRQ: No address property\n");
		prom_halt();
	}

	interrupt_enable = (void __iomem *) (unsigned long) addr[0];

	BTFIXUPSET_CALL(enable_irq, sun4c_enable_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(disable_irq, sun4c_disable_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(enable_pil_irq, sun4c_enable_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(disable_pil_irq, sun4c_disable_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_clock_irq, sun4c_clear_clock_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(load_profile_irq, sun4c_load_profile_irq, BTFIXUPCALL_NOP);
	sparc_init_timers = sun4c_init_timers;
#ifdef CONFIG_SMP
	BTFIXUPSET_CALL(set_cpu_int, sun4c_nop, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(clear_cpu_int, sun4c_nop, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(set_irq_udt, sun4c_nop, BTFIXUPCALL_NOP);
#endif
	sbus_writeb(SUN4C_INT_ENABLE, interrupt_enable);
	/* Cannot enable interrupts until OBP ticker is disabled. */
}

/*
 * Copyright (C) 2000,2001,2004 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/percpu.h>
#include <linux/spinlock.h>

#include <asm/addrspace.h>
#include <asm/time.h>
#include <asm/io.h>

#include <asm/sibyte/bcm1480_regs.h>
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/bcm1480_int.h>
#include <asm/sibyte/bcm1480_scd.h>

#include <asm/sibyte/sb1250.h>


#define IMR_IP2_VAL	K_BCM1480_INT_MAP_I0
#define IMR_IP3_VAL	K_BCM1480_INT_MAP_I1
#define IMR_IP4_VAL	K_BCM1480_INT_MAP_I2

/*
 * The general purpose timer ticks at 1MHz independent if
 * the rest of the system
 */
static void sibyte_set_mode(enum clock_event_mode mode,
                           struct clock_event_device *evt)
{
	unsigned int cpu = smp_processor_id();
	void __iomem *timer_cfg, *timer_init;

	timer_cfg = IOADDR(A_SCD_TIMER_REGISTER(cpu, R_SCD_TIMER_CFG));
	timer_init = IOADDR(A_SCD_TIMER_REGISTER(cpu, R_SCD_TIMER_INIT));

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		__raw_writeq(0, timer_cfg);
		__raw_writeq((V_SCD_TIMER_FREQ / HZ) - 1, timer_init);
		__raw_writeq(M_SCD_TIMER_ENABLE | M_SCD_TIMER_MODE_CONTINUOUS,
			     timer_cfg);
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		/* Stop the timer until we actually program a shot */
	case CLOCK_EVT_MODE_SHUTDOWN:
		__raw_writeq(0, timer_cfg);
		break;

	case CLOCK_EVT_MODE_UNUSED:	/* shuddup gcc */
	case CLOCK_EVT_MODE_RESUME:
		;
	}
}

static int sibyte_next_event(unsigned long delta, struct clock_event_device *cd)
{
	unsigned int cpu = smp_processor_id();
	void __iomem *timer_init;
	unsigned int cnt;
	int res;

	timer_init = IOADDR(A_SCD_TIMER_REGISTER(cpu, R_SCD_TIMER_INIT));
	cnt = __raw_readq(timer_init);
	cnt += delta;
	__raw_writeq(cnt, timer_init);
	res = ((long)(__raw_readq(timer_init) - cnt ) > 0) ? -ETIME : 0;

	return res;
}

static irqreturn_t sibyte_counter_handler(int irq, void *dev_id)
{
	unsigned int cpu = smp_processor_id();
	struct clock_event_device *cd = dev_id;
	void __iomem *timer_cfg;

	timer_cfg = IOADDR(A_SCD_TIMER_REGISTER(cpu, R_SCD_TIMER_CFG));

	/* Reset the timer */
	__raw_writeq(M_SCD_TIMER_ENABLE | M_SCD_TIMER_MODE_CONTINUOUS,
	             timer_cfg);
	cd->event_handler(cd);

	return IRQ_HANDLED;
}

static DEFINE_PER_CPU(struct clock_event_device, sibyte_hpt_clockevent);
static DEFINE_PER_CPU(struct irqaction, sibyte_hpt_irqaction);
static DEFINE_PER_CPU(char [18], sibyte_hpt_name);

void __cpuinit sb1480_clockevent_init(void)
{
	unsigned int cpu = smp_processor_id();
	unsigned int irq = K_BCM1480_INT_TIMER_0 + cpu;
	struct irqaction *action = &per_cpu(sibyte_hpt_irqaction, cpu);
	struct clock_event_device *cd = &per_cpu(sibyte_hpt_clockevent, cpu);
	unsigned char *name = per_cpu(sibyte_hpt_name, cpu);

	BUG_ON(cpu > 3);	/* Only have 4 general purpose timers */

	sprintf(name, "bcm1480-counter %d", cpu);
	cd->name		= name;
	cd->features		= CLOCK_EVT_FEAT_PERIODIC |
				  CLOCK_EVT_FEAT_ONESHOT;
	clockevent_set_clock(cd, V_SCD_TIMER_FREQ);
	cd->max_delta_ns	= clockevent_delta2ns(0x7fffff, cd);
	cd->min_delta_ns	= clockevent_delta2ns(1, cd);
	cd->rating		= 200;
	cd->irq			= irq;
	cd->cpumask		= cpumask_of_cpu(cpu);
	cd->set_next_event	= sibyte_next_event;
	cd->set_mode		= sibyte_set_mode;
	clockevents_register_device(cd);

	bcm1480_mask_irq(cpu, irq);

	/*
	 * Map timer interrupt to IP[4] of this cpu
	 */
	__raw_writeq(IMR_IP4_VAL,
		     IOADDR(A_BCM1480_IMR_REGISTER(cpu,
			R_BCM1480_IMR_INTERRUPT_MAP_BASE_H) + (irq << 3)));

	bcm1480_unmask_irq(cpu, irq);

	action->handler	= sibyte_counter_handler;
	action->flags	= IRQF_DISABLED | IRQF_PERCPU;
	action->name	= name;
	action->dev_id	= cd;
	setup_irq(irq, action);
}

static cycle_t bcm1480_hpt_read(void)
{
	return (cycle_t) __raw_readq(IOADDR(A_SCD_ZBBUS_CYCLE_COUNT));
}

struct clocksource bcm1480_clocksource = {
	.name	= "zbbus-cycles",
	.rating	= 200,
	.read	= bcm1480_hpt_read,
	.mask	= CLOCKSOURCE_MASK(64),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

void __init sb1480_clocksource_init(void)
{
	struct clocksource *cs = &bcm1480_clocksource;
	unsigned int plldiv;
	unsigned long zbbus;

	plldiv = G_BCM1480_SYS_PLL_DIV(__raw_readq(IOADDR(A_SCD_SYSTEM_CFG)));
	zbbus = ((plldiv >> 1) * 50000000) + ((plldiv & 1) * 25000000);
	clocksource_set_clock(cs, zbbus);
	clocksource_register(cs);
}

void __init plat_time_init(void)
{
	sb1480_clocksource_init();
	sb1480_clockevent_init();
}

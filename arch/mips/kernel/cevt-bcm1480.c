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
#include <linux/percpu.h>
#include <linux/smp.h>
#include <linux/irq.h>

#include <asm/addrspace.h>
#include <asm/io.h>
#include <asm/time.h>

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

static int sibyte_set_periodic(struct clock_event_device *evt)
{
	unsigned int cpu = smp_processor_id();
	void __iomem *cfg, *init;

	cfg = IOADDR(A_SCD_TIMER_REGISTER(cpu, R_SCD_TIMER_CFG));
	init = IOADDR(A_SCD_TIMER_REGISTER(cpu, R_SCD_TIMER_INIT));

	__raw_writeq(0, cfg);
	__raw_writeq((V_SCD_TIMER_FREQ / HZ) - 1, init);
	__raw_writeq(M_SCD_TIMER_ENABLE | M_SCD_TIMER_MODE_CONTINUOUS, cfg);
	return 0;
}

static int sibyte_shutdown(struct clock_event_device *evt)
{
	unsigned int cpu = smp_processor_id();
	void __iomem *cfg;

	cfg = IOADDR(A_SCD_TIMER_REGISTER(cpu, R_SCD_TIMER_CFG));

	/* Stop the timer until we actually program a shot */
	__raw_writeq(0, cfg);
	return 0;
}

static int sibyte_next_event(unsigned long delta, struct clock_event_device *cd)
{
	unsigned int cpu = smp_processor_id();
	void __iomem *cfg, *init;

	cfg = IOADDR(A_SCD_TIMER_REGISTER(cpu, R_SCD_TIMER_CFG));
	init = IOADDR(A_SCD_TIMER_REGISTER(cpu, R_SCD_TIMER_INIT));

	__raw_writeq(0, cfg);
	__raw_writeq(delta - 1, init);
	__raw_writeq(M_SCD_TIMER_ENABLE, cfg);

	return 0;
}

static irqreturn_t sibyte_counter_handler(int irq, void *dev_id)
{
	unsigned int cpu = smp_processor_id();
	struct clock_event_device *cd = dev_id;
	void __iomem *cfg;
	unsigned long tmode;

	if (clockevent_state_periodic(cd))
		tmode = M_SCD_TIMER_ENABLE | M_SCD_TIMER_MODE_CONTINUOUS;
	else
		tmode = 0;

	/* ACK interrupt */
	cfg = IOADDR(A_SCD_TIMER_REGISTER(cpu, R_SCD_TIMER_CFG));
	____raw_writeq(tmode, cfg);

	cd->event_handler(cd);

	return IRQ_HANDLED;
}

static DEFINE_PER_CPU(struct clock_event_device, sibyte_hpt_clockevent);
static DEFINE_PER_CPU(struct irqaction, sibyte_hpt_irqaction);
static DEFINE_PER_CPU(char [18], sibyte_hpt_name);

void sb1480_clockevent_init(void)
{
	unsigned int cpu = smp_processor_id();
	unsigned int irq = K_BCM1480_INT_TIMER_0 + cpu;
	struct irqaction *action = &per_cpu(sibyte_hpt_irqaction, cpu);
	struct clock_event_device *cd = &per_cpu(sibyte_hpt_clockevent, cpu);
	unsigned char *name = per_cpu(sibyte_hpt_name, cpu);

	BUG_ON(cpu > 3);	/* Only have 4 general purpose timers */

	sprintf(name, "bcm1480-counter-%d", cpu);
	cd->name		= name;
	cd->features		= CLOCK_EVT_FEAT_PERIODIC |
				  CLOCK_EVT_FEAT_ONESHOT;
	clockevent_set_clock(cd, V_SCD_TIMER_FREQ);
	cd->max_delta_ns	= clockevent_delta2ns(0x7fffff, cd);
	cd->max_delta_ticks	= 0x7fffff;
	cd->min_delta_ns	= clockevent_delta2ns(2, cd);
	cd->min_delta_ticks	= 2;
	cd->rating		= 200;
	cd->irq			= irq;
	cd->cpumask		= cpumask_of(cpu);
	cd->set_next_event	= sibyte_next_event;
	cd->set_state_shutdown	= sibyte_shutdown;
	cd->set_state_periodic	= sibyte_set_periodic;
	cd->set_state_oneshot	= sibyte_shutdown;
	clockevents_register_device(cd);

	bcm1480_mask_irq(cpu, irq);

	/*
	 * Map the timer interrupt to IP[4] of this cpu
	 */
	__raw_writeq(IMR_IP4_VAL,
		     IOADDR(A_BCM1480_IMR_REGISTER(cpu,
			R_BCM1480_IMR_INTERRUPT_MAP_BASE_H) + (irq << 3)));

	bcm1480_unmask_irq(cpu, irq);

	action->handler = sibyte_counter_handler;
	action->flags	= IRQF_PERCPU | IRQF_TIMER;
	action->name	= name;
	action->dev_id	= cd;

	irq_set_affinity(irq, cpumask_of(cpu));
	setup_irq(irq, action);
}

/*
 * i8253.c  8253/PIT functions
 *
 */
#include <linux/clocksource.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include <linux/sysdev.h>
#include <linux/module.h>
#include <linux/init.h>

#include <asm/smp.h>
#include <asm/delay.h>
#include <asm/i8253.h>
#include <asm/io.h>

#include "io_ports.h"

DEFINE_SPINLOCK(i8253_lock);
EXPORT_SYMBOL(i8253_lock);

void setup_pit_timer(void)
{
	unsigned long flags;

	spin_lock_irqsave(&i8253_lock, flags);
	outb_p(0x34,PIT_MODE);		/* binary, mode 2, LSB/MSB, ch 0 */
	udelay(10);
	outb_p(LATCH & 0xff , PIT_CH0);	/* LSB */
	udelay(10);
	outb(LATCH >> 8 , PIT_CH0);	/* MSB */
	spin_unlock_irqrestore(&i8253_lock, flags);
}

/*
 * Since the PIT overflows every tick, its not very useful
 * to just read by itself. So use jiffies to emulate a free
 * running counter:
 */
static cycle_t pit_read(void)
{
	unsigned long flags;
	int count;
	u64 jifs;

	spin_lock_irqsave(&i8253_lock, flags);
	outb_p(0x00, PIT_MODE);	/* latch the count ASAP */
	count = inb_p(PIT_CH0);	/* read the latched count */
	count |= inb_p(PIT_CH0) << 8;

	/* VIA686a test code... reset the latch if count > max + 1 */
	if (count > LATCH) {
		outb_p(0x34, PIT_MODE);
		outb_p(LATCH & 0xff, PIT_CH0);
		outb(LATCH >> 8, PIT_CH0);
		count = LATCH - 1;
	}
	spin_unlock_irqrestore(&i8253_lock, flags);

	jifs = jiffies_64;

	jifs -= INITIAL_JIFFIES;
	count = (LATCH-1) - count;

	return (cycle_t)(jifs * LATCH) + count;
}

static struct clocksource clocksource_pit = {
	.name	= "pit",
	.rating = 110,
	.read	= pit_read,
	.mask	= CLOCKSOURCE_MASK(64),
	.mult	= 0,
	.shift	= 20,
};

static int __init init_pit_clocksource(void)
{
	if (num_possible_cpus() > 4) /* PIT does not scale! */
		return 0;

	clocksource_pit.mult = clocksource_hz2mult(CLOCK_TICK_RATE, 20);
	return clocksource_register(&clocksource_pit);
}
module_init(init_pit_clocksource);

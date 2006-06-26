/*
 * i8253.c  8253/PIT functions
 *
 */
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

 /*
 * linux/arch/m68k/sun3/sun3ints.c -- Sun-3(x) Linux interrupt handling code
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <asm/intersil.h>
#include <asm/oplib.h>
#include <asm/sun3ints.h>
#include <asm/irq_regs.h>
#include <linux/seq_file.h>

#include "sun3.h"

void sun3_disable_interrupts(void)
{
	sun3_disable_irq(0);
}

void sun3_enable_interrupts(void)
{
	sun3_enable_irq(0);
}

static unsigned char led_pattern[8] = {
	(u8)~(0x80), (u8)~(0x01),
	(u8)~(0x40), (u8)~(0x02),
	(u8)~(0x20), (u8)~(0x04),
	(u8)~(0x10), (u8)~(0x08)
};

volatile unsigned char* sun3_intreg;

void sun3_enable_irq(unsigned int irq)
{
	*sun3_intreg |=  (1 << irq);
}

void sun3_disable_irq(unsigned int irq)
{
	*sun3_intreg &= ~(1 << irq);
}

static irqreturn_t sun3_int7(int irq, void *dev_id)
{
	unsigned int cnt;

	cnt = kstat_irqs_cpu(irq, 0);
	if (!(cnt % 2000))
		sun3_leds(led_pattern[cnt % 16000 / 2000]);
	return IRQ_HANDLED;
}

static irqreturn_t sun3_int5(int irq, void *dev_id)
{
	unsigned long flags;
	unsigned int cnt;

	local_irq_save(flags);
#ifdef CONFIG_SUN3
	intersil_clear();
#endif
	sun3_disable_irq(5);
	sun3_enable_irq(5);
#ifdef CONFIG_SUN3
	intersil_clear();
#endif
	legacy_timer_tick(1);
	cnt = kstat_irqs_cpu(irq, 0);
	if (!(cnt % 20))
		sun3_leds(led_pattern[cnt % 160 / 20]);
	local_irq_restore(flags);
	return IRQ_HANDLED;
}

static irqreturn_t sun3_vec255(int irq, void *dev_id)
{
	return IRQ_HANDLED;
}

void __init sun3_init_IRQ(void)
{
	*sun3_intreg = 1;

	m68k_setup_user_interrupt(VEC_USER, 128);

	if (request_irq(IRQ_AUTO_5, sun3_int5, 0, "clock", NULL))
		pr_err("Couldn't register %s interrupt\n", "int5");
	if (request_irq(IRQ_AUTO_7, sun3_int7, 0, "nmi", NULL))
		pr_err("Couldn't register %s interrupt\n", "int7");
	if (request_irq(IRQ_USER+127, sun3_vec255, 0, "vec255", NULL))
		pr_err("Couldn't register %s interrupt\n", "vec255");
}

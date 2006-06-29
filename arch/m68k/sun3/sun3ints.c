 /*
 * linux/arch/m68k/sun3/sun3ints.c -- Sun-3(x) Linux interrupt handling code
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <asm/segment.h>
#include <asm/intersil.h>
#include <asm/oplib.h>
#include <asm/sun3ints.h>
#include <linux/seq_file.h>

extern void sun3_leds (unsigned char);

void sun3_disable_interrupts(void)
{
	sun3_disable_irq(0);
}

void sun3_enable_interrupts(void)
{
	sun3_enable_irq(0);
}

int led_pattern[8] = {
       ~(0x80), ~(0x01),
       ~(0x40), ~(0x02),
       ~(0x20), ~(0x04),
       ~(0x10), ~(0x08)
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

static irqreturn_t sun3_int7(int irq, void *dev_id, struct pt_regs *fp)
{
	*sun3_intreg |=  (1 << irq);
	if (!(kstat_cpu(0).irqs[irq] % 2000))
		sun3_leds(led_pattern[(kstat_cpu(0).irqs[irq] % 16000) / 2000]);
	return IRQ_HANDLED;
}

static irqreturn_t sun3_int5(int irq, void *dev_id, struct pt_regs *fp)
{
#ifdef CONFIG_SUN3
	intersil_clear();
#endif
        *sun3_intreg |=  (1 << irq);
#ifdef CONFIG_SUN3
	intersil_clear();
#endif
        do_timer(fp);
#ifndef CONFIG_SMP
	update_process_times(user_mode(fp));
#endif
        if (!(kstat_cpu(0).irqs[irq] % 20))
                sun3_leds(led_pattern[(kstat_cpu(0).irqs[irq] % 160) / 20]);
	return IRQ_HANDLED;
}

static irqreturn_t sun3_vec255(int irq, void *dev_id, struct pt_regs *fp)
{
//	intersil_clear();
	return IRQ_HANDLED;
}

static void sun3_inthandle(unsigned int irq, struct pt_regs *fp)
{
        *sun3_intreg &= ~(1 << irq);

	m68k_handle_int(irq, fp);
}

static struct irq_controller sun3_irq_controller = {
	.name		= "sun3",
	.lock		= SPIN_LOCK_UNLOCKED,
	.startup	= m68k_irq_startup,
	.shutdown	= m68k_irq_shutdown,
	.enable		= sun3_enable_irq,
	.disable	= sun3_disable_irq,
};

void sun3_init_IRQ(void)
{
	*sun3_intreg = 1;

	m68k_setup_auto_interrupt(sun3_inthandle);
	m68k_setup_irq_controller(&sun3_irq_controller, IRQ_AUTO_1, 7);
	m68k_setup_user_interrupt(VEC_USER, 192, NULL);

	request_irq(IRQ_AUTO_5, sun3_int5, 0, "int5", NULL);
	request_irq(IRQ_AUTO_7, sun3_int7, 0, "int7", NULL);
	request_irq(IRQ_USER+127, sun3_vec255, 0, "vec255", NULL);
}

/*
 *  linux/arch/h8300/kernel/timer/timer16.c
 *
 *  Yoshinori Sato <ysato@users.sourcefoge.jp>
 *
 *  16bit Timer Handler
 *
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/timex.h>

#include <asm/segment.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/regs306x.h>

/* 16bit timer */
#if CONFIG_H8300_TIMER16_CH == 0
#define _16BASE	0xffff78
#define _16IRQ	24
#elif CONFIG_H8300_TIMER16_CH == 1
#define _16BASE	0xffff80
#define _16IRQ	28
#elif CONFIG_H8300_TIMER16_CH == 2
#define _16BASE	0xffff88
#define _16IRQ	32
#else
#error Unknown timer channel.
#endif

#define TCR	0
#define TIOR	1
#define TCNT	2
#define GRA	4
#define GRB	6

#define H8300_TIMER_FREQ CONFIG_CPU_CLOCK*10000 /* Timer input freq. */

static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	h8300_timer_tick();
	ctrl_bclr(CONFIG_H8300_TIMER16_CH, TISRA);
	return IRQ_HANDLED;
}

static struct irqaction timer16_irq = {
	.name		= "timer-16",
	.handler	= timer_interrupt,
	.flags		= IRQF_DISABLED | IRQF_TIMER,
};

static const int __initdata divide_rate[] = {1, 2, 4, 8};

void __init h8300_timer_setup(void)
{
	unsigned int div;
	unsigned int cnt;

	calc_param(cnt, div, divide_rate, 0x10000);

	setup_irq(_16IRQ, &timer16_irq);

	/* initialize timer */
	ctrl_outb(0, TSTR);
	ctrl_outb(CCLR0 | div, _16BASE + TCR);
	ctrl_outw(cnt, _16BASE + GRA);
	ctrl_bset(4 + CONFIG_H8300_TIMER16_CH, TISRA);
	ctrl_bset(CONFIG_H8300_TIMER16_CH, TSTR);
}

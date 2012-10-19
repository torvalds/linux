/*
 *  linux/arch/h8300/kernel/cpu/timer/timer8.c
 *
 *  Yoshinori Sato <ysato@users.sourcefoge.jp>
 *
 *  8bit Timer Handler
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
#include <linux/profile.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/timer.h>
#if defined(CONFIG_CPU_H8300H)
#include <asm/regs306x.h>
#endif
#if defined(CONFIG_CPU_H8S)
#include <asm/regs267x.h>
#endif

/* 8bit timer x2 */
#define CMFA	6

#if defined(CONFIG_H8300_TIMER8_CH0)
#define _8BASE	_8TCR0
#ifdef CONFIG_CPU_H8300H
#define _8IRQ	36
#endif
#ifdef CONFIG_CPU_H8S
#define _8IRQ	72
#endif
#elif defined(CONFIG_H8300_TIMER8_CH2)
#ifdef CONFIG_CPU_H8300H
#define _8BASE	_8TCR2
#define _8IRQ	40
#endif
#endif

#ifndef _8BASE
#error Unknown timer channel.
#endif

#define _8TCR	0
#define _8TCSR	2
#define TCORA	4
#define TCORB	6
#define _8TCNT	8

#define CMIEA	0x40
#define CCLR_CMA 0x08
#define CKS2	0x04

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "xtime_update()" routine every clocktick
 */

static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	h8300_timer_tick();
	ctrl_bclr(CMFA, _8BASE + _8TCSR);
	return IRQ_HANDLED;
}

static struct irqaction timer8_irq = {
	.name		= "timer-8",
	.handler	= timer_interrupt,
	.flags		= IRQF_DISABLED | IRQF_TIMER,
};

static const int __initconst divide_rate[] = {8, 64, 8192};

void __init h8300_timer_setup(void)
{
	unsigned int div;
	unsigned int cnt;

	calc_param(cnt, div, divide_rate, 0x10000);
	div++;

	setup_irq(_8IRQ, &timer8_irq);

#if defined(CONFIG_CPU_H8S)
	/* Timer module enable */
	ctrl_bclr(0, MSTPCRL)
#endif

	/* initialize timer */
	ctrl_outw(cnt, _8BASE + TCORA);
	ctrl_outw(0x0000, _8BASE + _8TCSR);
	ctrl_outw((CMIEA|CCLR_CMA|CKS2) << 8 | div,
		  _8BASE + _8TCR);
}

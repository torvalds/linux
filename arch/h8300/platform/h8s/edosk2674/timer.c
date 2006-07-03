/*
 *  linux/arch/h8300/platform/h8s/edosk2674/timer.c
 *
 *  Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 *  Platform depend Timer Handler
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
#include <asm/regs267x.h>

#define CMFA 6

#define CMIEA 0x40
#define CCLR_CMA 0x08
#define CLK_DIV8192 0x03

#define H8300_TIMER_FREQ CONFIG_CPU_CLOCK*1000/8192 /* Timer input freq. */

void __init platform_timer_setup(irqreturn_t (*timer_int)(int, void *, struct pt_regs *))
{
	/* 8bit timer module enabled */
	ctrl_outb(ctrl_inb(MSTPCRL) & ~0x01, MSTPCRL);
	/* setup 8bit timer ch1 */
	ctrl_outb(H8300_TIMER_FREQ / HZ, _8TCORA1);      /* set interval */
	ctrl_outb(0x00, _8TCSR1);                        /* no output */
	request_irq(76, timer_int, 0, "timer" ,0);
	ctrl_outb(CMIEA|CCLR_CMA|CLK_DIV8192, _8TCR1);   /* start count */
}

void platform_timer_eoi(void)
{
	*(volatile unsigned char *)_8TCSR1 &= ~(1 << CMFA);
}

void platform_gettod(int *year, int *mon, int *day, int *hour,
		 int *min, int *sec)
{
/* FIXME! not RTC support */
	*year = *mon = *day = *hour = *min = *sec = 0;
}

/*
 *  linux/arch/h8300/platform/h8300h/generic/timer.c
 *
 *  Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 *  Platform depend Timer Handler
 *
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

#include <asm/segment.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <linux/timex.h>

#if defined(CONFIG_H83007) || defined(CONFIG_H83068)
#include <asm/regs306x.h>
#define CMFA 6

#define CMIEA 0x40
#define CCLR_CMA 0x08
#define CLK_DIV8192 0x03

#define H8300_TIMER_FREQ CONFIG_CPU_CLOCK*1000/8192 /* Timer input freq. */

void __init platform_timer_setup(irqreturn_t (*timer_int)(int, void *, struct pt_regs *))
{
	/* setup 8bit timer ch2 */
	ctrl_outb(H8300_TIMER_FREQ / HZ, TCORA2);      /* set interval */
	ctrl_outb(0x00, _8TCSR2);                      /* no output */
	request_irq(40, timer_int, 0, "timer", 0);
	ctrl_outb(CMIEA|CCLR_CMA|CLK_DIV8192, _8TCR2); /* start count */
}

void platform_timer_eoi(void)
{
	*(volatile unsigned char *)_8TCSR2 &= ~(1 << CMFA);
}
#endif

#if defined(CONFIG_H83002) || defined(CONFIG_H83048)
/* FIXME! */
#define TSTR 0x00ffff60
#define TSNC 0x00ffff61
#define TMDR 0x00ffff62
#define TFCR 0x00ffff63
#define TOER 0x00ffff90
#define TOCR 0x00ffff91
/* ITU0 */
#define TCR  0x00ffff64
#define TIOR 0x00ffff65
#define TIER 0x00ffff66
#define TSR  0x00ffff67
#define TCNT 0x00ffff68
#define GRA  0x00ffff6a
#define GRB  0x00ffff6c

#define CCLR_CMGRA 0x20
#define CLK_DIV8 0x03

#define H8300_TIMER_FREQ CONFIG_CPU_CLOCK*1000/8 /* Timer input freq. */

void __init platform_timer_setup(irqreturn_t (*timer_int)(int, void *, struct pt_regs *))
{
	*(unsigned short *)GRA= H8300_TIMER_FREQ / HZ;  /* set interval */
	*(unsigned short *)TCNT=0;                      /* clear counter */
	ctrl_outb(0x80|CCLR_CMGRA|CLK_DIV8, TCR);       /* set ITU0 clock */
	ctrl_outb(0x88, TIOR);                          /* no output */
	request_irq(26, timer_int, 0, "timer", 0);
	ctrl_outb(0xf9, TIER);                          /* compare match GRA interrupt */
	ctrl_outb(ctrl_inb(TSNC) & ~0x01, TSNC);        /* ITU0 async */
	ctrl_outb(ctrl_inb(TMDR) & ~0x01, TMDR);        /* ITU0 normal mode */
	ctrl_outb(ctrl_inb(TSTR) | 0x01, TSTR);         /* ITU0 Start */
	return 0;
}

void platform_timer_eoi(void)
{
	ctrl_outb(ctrl_inb(TSR) & ~0x01,TSR);
}
#endif

void platform_gettod(int *year, int *mon, int *day, int *hour,
		 int *min, int *sec)
{
	*year = *mon = *day = *hour = *min = *sec = 0;
}

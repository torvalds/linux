/*
 * m8xx_wdt.c - MPC8xx watchdog driver
 *
 * Author: Florian Schirmer <jolt@tuxbox.org>
 *
 * 2002 (c) Florian Schirmer <jolt@tuxbox.org> This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/io.h>
#include <asm/8xx_immap.h>
#include <syslib/m8xx_wdt.h>

static int wdt_timeout;
int m8xx_has_internal_rtc = 0;

static irqreturn_t m8xx_wdt_interrupt(int, void *, struct pt_regs *);
static struct irqaction m8xx_wdt_irqaction = {
	.handler = m8xx_wdt_interrupt,
	.name = "watchdog",
};

void m8xx_wdt_reset(void)
{
	volatile immap_t *imap = (volatile immap_t *)IMAP_ADDR;

	out_be16(&imap->im_siu_conf.sc_swsr, 0x556c);	/* write magic1 */
	out_be16(&imap->im_siu_conf.sc_swsr, 0xaa39);	/* write magic2 */
}

static irqreturn_t m8xx_wdt_interrupt(int irq, void *dev, struct pt_regs *regs)
{
	volatile immap_t *imap = (volatile immap_t *)IMAP_ADDR;

	m8xx_wdt_reset();

	out_be16(&imap->im_sit.sit_piscr, in_be16(&imap->im_sit.sit_piscr) | PISCR_PS);	/* clear irq */

	return IRQ_HANDLED;
}

#define SYPCR_SWP 0x1
#define SYPCR_SWE 0x4


void __init m8xx_wdt_install_irq(volatile immap_t *imap, bd_t *binfo)
{
	u32 pitc;
	u32 pitrtclk;

	/*
	 * Fire trigger if half of the wdt ticked down 
	 */

	if (imap->im_sit.sit_rtcsc & RTCSC_38K)
		pitrtclk = 9600;
	else
		pitrtclk = 8192;

	if ((wdt_timeout) > (UINT_MAX / pitrtclk))
		pitc = wdt_timeout / binfo->bi_intfreq * pitrtclk / 2;
	else
		pitc = pitrtclk * wdt_timeout / binfo->bi_intfreq / 2;

	out_be32(&imap->im_sit.sit_pitc, pitc << 16);

	out_be16(&imap->im_sit.sit_piscr, (mk_int_int_mask(PIT_INTERRUPT) << 8) | PISCR_PIE | PISCR_PTE);

	if (setup_irq(PIT_INTERRUPT, &m8xx_wdt_irqaction))
		panic("m8xx_wdt: error setting up the watchdog irq!");

	printk(KERN_NOTICE
	       "m8xx_wdt: keep-alive trigger installed (PITC: 0x%04X)\n", pitc);

}

static void m8xx_wdt_timer_func(unsigned long data);

static struct timer_list m8xx_wdt_timer =
	TIMER_INITIALIZER(m8xx_wdt_timer_func, 0, 0);

void m8xx_wdt_stop_timer(void)
{
	del_timer(&m8xx_wdt_timer);
}

void m8xx_wdt_install_timer(void)
{
	m8xx_wdt_timer.expires = jiffies + (HZ/2);
	add_timer(&m8xx_wdt_timer);
}

static void m8xx_wdt_timer_func(unsigned long data)
{
	m8xx_wdt_reset();
	m8xx_wdt_install_timer();
}

void __init m8xx_wdt_handler_install(bd_t * binfo)
{
	volatile immap_t *imap = (volatile immap_t *)IMAP_ADDR;
	u32 sypcr;

	sypcr = in_be32(&imap->im_siu_conf.sc_sypcr);

	if (!(sypcr & SYPCR_SWE)) {
		printk(KERN_NOTICE "m8xx_wdt: wdt disabled (SYPCR: 0x%08X)\n",
		       sypcr);
		return;
	}

	m8xx_wdt_reset();

	printk(KERN_NOTICE
	       "m8xx_wdt: active wdt found (SWTC: 0x%04X, SWP: 0x%01X)\n",
	       (sypcr >> 16), sypcr & SYPCR_SWP);

	wdt_timeout = (sypcr >> 16) & 0xFFFF;

	if (!wdt_timeout)
		wdt_timeout = 0xFFFF;

	if (sypcr & SYPCR_SWP)
		wdt_timeout *= 2048;

	m8xx_has_internal_rtc = in_be16(&imap->im_sit.sit_rtcsc) & RTCSC_RTE;

	/* if the internal RTC is off use a kernel timer */
	if (!m8xx_has_internal_rtc) {
		if (wdt_timeout < (binfo->bi_intfreq/HZ))
			printk(KERN_ERR "m8xx_wdt: timeout too short for ktimer!\n");
		m8xx_wdt_install_timer();
	} else
		m8xx_wdt_install_irq(imap, binfo);

	wdt_timeout /= binfo->bi_intfreq;
}

int m8xx_wdt_get_timeout(void)
{
	return wdt_timeout;
}

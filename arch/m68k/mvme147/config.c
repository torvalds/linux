// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  arch/m68k/mvme147/config.c
 *
 *  Copyright (C) 1996 Dave Frascone [chaos@mindspring.com]
 *  Cloned from        Richard Hirst [richard@sleepie.demon.co.uk]
 *
 * Based on:
 *
 *  Copyright (C) 1993 Hamish Macdonald
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/clocksource.h>
#include <linux/console.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/rtc/m48t59.h>

#include <asm/bootinfo.h>
#include <asm/bootinfo-vme.h>
#include <asm/byteorder.h>
#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/machdep.h>
#include <asm/mvme147hw.h>
#include <asm/config.h>

#include "mvme147.h"

static void mvme147_get_model(char *model);
static void __init mvme147_sched_init(void);
extern void mvme147_reset (void);


int __init mvme147_parse_bootinfo(const struct bi_record *bi)
{
	uint16_t tag = be16_to_cpu(bi->tag);
	if (tag == BI_VME_TYPE || tag == BI_VME_BRDINFO)
		return 0;
	else
		return 1;
}

void mvme147_reset(void)
{
	pr_info("\r\n\nCalled mvme147_reset\r\n");
	m147_pcc->watchdog = 0x0a;	/* Clear timer */
	m147_pcc->watchdog = 0xa5;	/* Enable watchdog - 100ms to reset */
	while (1)
		;
}

static void mvme147_get_model(char *model)
{
	sprintf(model, "Motorola MVME147");
}

/*
 * This function is called during kernel startup to initialize
 * the mvme147 IRQ handling routines.
 */

static void __init mvme147_init_IRQ(void)
{
	m68k_setup_user_interrupt(VEC_USER, 192);
}

void __init config_mvme147(void)
{
	mach_sched_init		= mvme147_sched_init;
	mach_init_IRQ		= mvme147_init_IRQ;
	mach_reset		= mvme147_reset;
	mach_get_model		= mvme147_get_model;

	/* Board type is only set by newer versions of vmelilo/tftplilo */
	if (!vme_brdtype)
		vme_brdtype = VME_TYPE_MVME147;
}

static struct resource m48t59_rsrc[] = {
	DEFINE_RES_MEM(MVME147_RTC_BASE, 0x800),
};

static struct m48t59_plat_data m48t59_data = {
	.type = M48T59RTC_TYPE_M48T02,
	.yy_offset = 70,
};

static int __init mvme147_platform_init(void)
{
	if (!MACH_IS_MVME147)
		return 0;

	platform_device_register_resndata(NULL, "rtc-m48t59", -1,
					  m48t59_rsrc, ARRAY_SIZE(m48t59_rsrc),
					  &m48t59_data, sizeof(m48t59_data));
	return 0;
}

arch_initcall(mvme147_platform_init);

static u64 mvme147_read_clk(struct clocksource *cs);

static struct clocksource mvme147_clk = {
	.name   = "pcc",
	.rating = 250,
	.read   = mvme147_read_clk,
	.mask   = CLOCKSOURCE_MASK(32),
	.flags  = CLOCK_SOURCE_IS_CONTINUOUS,
};

static u32 clk_total;

#define PCC_TIMER_CLOCK_FREQ 160000
#define PCC_TIMER_CYCLES     (PCC_TIMER_CLOCK_FREQ / HZ)
#define PCC_TIMER_PRELOAD    (0x10000 - PCC_TIMER_CYCLES)

/* Using pcc tick timer 1 */

static irqreturn_t mvme147_timer_int (int irq, void *dev_id)
{
	unsigned long flags;

	local_irq_save(flags);
	m147_pcc->t1_cntrl = PCC_TIMER_CLR_OVF | PCC_TIMER_COC_EN |
			     PCC_TIMER_TIC_EN;
	m147_pcc->t1_int_cntrl = PCC_INT_ENAB | PCC_TIMER_INT_CLR |
				 PCC_LEVEL_TIMER1;
	clk_total += PCC_TIMER_CYCLES;
	legacy_timer_tick(1);
	local_irq_restore(flags);

	return IRQ_HANDLED;
}


static void __init mvme147_sched_init(void)
{
	if (request_irq(PCC_IRQ_TIMER1, mvme147_timer_int, IRQF_TIMER,
			"timer 1", NULL))
		pr_err("Couldn't register timer interrupt\n");

	/* Init the clock with a value */
	/* The clock counter increments until 0xFFFF then reloads */
	m147_pcc->t1_preload = PCC_TIMER_PRELOAD;
	m147_pcc->t1_cntrl = PCC_TIMER_CLR_OVF | PCC_TIMER_COC_EN |
			     PCC_TIMER_TIC_EN;
	m147_pcc->t1_int_cntrl = PCC_INT_ENAB | PCC_TIMER_INT_CLR |
				 PCC_LEVEL_TIMER1;

	clocksource_register_hz(&mvme147_clk, PCC_TIMER_CLOCK_FREQ);
}

static u64 mvme147_read_clk(struct clocksource *cs)
{
	unsigned long flags;
	u8 overflow, tmp;
	u16 count;
	u32 ticks;

	local_irq_save(flags);
	tmp = m147_pcc->t1_cntrl >> 4;
	count = m147_pcc->t1_count;
	overflow = m147_pcc->t1_cntrl >> 4;
	if (overflow != tmp)
		count = m147_pcc->t1_count;
	count -= PCC_TIMER_PRELOAD;
	ticks = count + overflow * PCC_TIMER_CYCLES;
	ticks += clk_total;
	local_irq_restore(flags);

	return ticks;
}

static void scc_delay(void)
{
	__asm__ __volatile__ ("nop; nop;");
}

static void scc_write(char ch)
{
	do {
		scc_delay();
	} while (!(in_8(M147_SCC_A_ADDR) & BIT(2)));
	scc_delay();
	out_8(M147_SCC_A_ADDR, 8);
	scc_delay();
	out_8(M147_SCC_A_ADDR, ch);
}

void mvme147_scc_write(struct console *co, const char *str, unsigned int count)
{
	unsigned long flags;

	local_irq_save(flags);
	while (count--)	{
		if (*str == '\n')
			scc_write('\r');
		scc_write(*str++);
	}
	local_irq_restore(flags);
}

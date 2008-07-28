/*
 * hp6x0 Power Management Routines
 *
 * Copyright (c) 2006 Andriy Skulysh <askulsyh@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License.
 */
#include <linux/init.h>
#include <linux/suspend.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <asm/io.h>
#include <asm/hd64461.h>
#include <asm/hp6xx.h>
#include <cpu/dac.h>
#include <asm/pm.h>

#define STBCR		0xffffff82
#define STBCR2		0xffffff88

static int hp6x0_pm_enter(suspend_state_t state)
{
	u8 stbcr, stbcr2;
#ifdef CONFIG_HD64461_ENABLER
	u8 scr;
	u16 hd64461_stbcr;
#endif

#ifdef CONFIG_HD64461_ENABLER
	outb(0, HD64461_PCC1CSCIER);

	scr = inb(HD64461_PCC1SCR);
	scr |= HD64461_PCCSCR_VCC1;
	outb(scr, HD64461_PCC1SCR);

	hd64461_stbcr = inw(HD64461_STBCR);
	hd64461_stbcr |= HD64461_STBCR_SPC1ST;
	outw(hd64461_stbcr, HD64461_STBCR);
#endif

	ctrl_outb(0x1f, DACR);

	stbcr = ctrl_inb(STBCR);
	ctrl_outb(0x01, STBCR);

	stbcr2 = ctrl_inb(STBCR2);
	ctrl_outb(0x7f , STBCR2);

	outw(0xf07f, HD64461_SCPUCR);

	pm_enter();

	outw(0, HD64461_SCPUCR);
	ctrl_outb(stbcr, STBCR);
	ctrl_outb(stbcr2, STBCR2);

#ifdef CONFIG_HD64461_ENABLER
	hd64461_stbcr = inw(HD64461_STBCR);
	hd64461_stbcr &= ~HD64461_STBCR_SPC1ST;
	outw(hd64461_stbcr, HD64461_STBCR);

	outb(0x4c, HD64461_PCC1CSCIER);
	outb(0x00, HD64461_PCC1CSCR);
#endif

	return 0;
}

static struct platform_suspend_ops hp6x0_pm_ops = {
	.enter		= hp6x0_pm_enter,
	.valid		= suspend_valid_only_mem,
};

static int __init hp6x0_pm_init(void)
{
	suspend_set_ops(&hp6x0_pm_ops);
	return 0;
}

late_initcall(hp6x0_pm_init);

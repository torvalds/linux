/*
 * wdt.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * IO dispatcher for a shared memory channel driver.
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */
#include <linux/types.h>

#include <dspbridge/dbdefs.h>
#include <dspbridge/dspdeh.h>
#include <dspbridge/dev.h>
#include <dspbridge/_chnl_sm.h>
#include <dspbridge/wdt.h>
#include <dspbridge/host_os.h>


#ifdef CONFIG_TIDSPBRIDGE_WDT3

#define OMAP34XX_WDT3_BASE 		(L4_PER_34XX_BASE + 0x30000)

static struct dsp_wdt_setting dsp_wdt;

void dsp_wdt_dpc(unsigned long data)
{
	struct deh_mgr *deh_mgr;
	dev_get_deh_mgr(dev_get_first(), &deh_mgr);
	if (deh_mgr)
		bridge_deh_notify(deh_mgr, DSP_WDTOVERFLOW, 0);
}

irqreturn_t dsp_wdt_isr(int irq, void *data)
{
	u32 value;
	/* ack wdt3 interrupt */
	value = __raw_readl(dsp_wdt.reg_base + OMAP3_WDT3_ISR_OFFSET);
	__raw_writel(value, dsp_wdt.reg_base + OMAP3_WDT3_ISR_OFFSET);

	tasklet_schedule(&dsp_wdt.wdt3_tasklet);
	return IRQ_HANDLED;
}

int dsp_wdt_init(void)
{
	int ret = 0;

	dsp_wdt.sm_wdt = NULL;
	dsp_wdt.reg_base = OMAP2_L4_IO_ADDRESS(OMAP34XX_WDT3_BASE);
	tasklet_init(&dsp_wdt.wdt3_tasklet, dsp_wdt_dpc, 0);

	dsp_wdt.fclk = clk_get(NULL, "wdt3_fck");

	if (dsp_wdt.fclk) {
		dsp_wdt.iclk = clk_get(NULL, "wdt3_ick");
		if (!dsp_wdt.iclk) {
			clk_put(dsp_wdt.fclk);
			dsp_wdt.fclk = NULL;
			ret = -EFAULT;
		}
	} else
		ret = -EFAULT;

	if (!ret)
		ret = request_irq(INT_34XX_WDT3_IRQ, dsp_wdt_isr, 0,
							"dsp_wdt", &dsp_wdt);

	/* Disable at this moment, it will be enabled when DSP starts */
	if (!ret)
		disable_irq(INT_34XX_WDT3_IRQ);

	return ret;
}

void dsp_wdt_sm_set(void *data)
{
	dsp_wdt.sm_wdt = data;
	dsp_wdt.sm_wdt->wdt_overflow = CONFIG_TIDSPBRIDGE_WDT_TIMEOUT;
}


void dsp_wdt_exit(void)
{
	free_irq(INT_34XX_WDT3_IRQ, &dsp_wdt);
	tasklet_kill(&dsp_wdt.wdt3_tasklet);

	if (dsp_wdt.fclk)
		clk_put(dsp_wdt.fclk);
	if (dsp_wdt.iclk)
		clk_put(dsp_wdt.iclk);

	dsp_wdt.fclk = NULL;
	dsp_wdt.iclk = NULL;
	dsp_wdt.sm_wdt = NULL;
	dsp_wdt.reg_base = NULL;
}

void dsp_wdt_enable(bool enable)
{
	u32 tmp;
	static bool wdt_enable;

	if (wdt_enable == enable || !dsp_wdt.fclk || !dsp_wdt.iclk)
		return;

	wdt_enable = enable;

	if (enable) {
		clk_enable(dsp_wdt.fclk);
		clk_enable(dsp_wdt.iclk);
		dsp_wdt.sm_wdt->wdt_setclocks = 1;
		tmp = __raw_readl(dsp_wdt.reg_base + OMAP3_WDT3_ISR_OFFSET);
		__raw_writel(tmp, dsp_wdt.reg_base + OMAP3_WDT3_ISR_OFFSET);
		enable_irq(INT_34XX_WDT3_IRQ);
	} else {
		disable_irq(INT_34XX_WDT3_IRQ);
		dsp_wdt.sm_wdt->wdt_setclocks = 0;
		clk_disable(dsp_wdt.iclk);
		clk_disable(dsp_wdt.fclk);
	}
}

#else
void dsp_wdt_enable(bool enable)
{
}

void dsp_wdt_sm_set(void *data)
{
}

int dsp_wdt_init(void)
{
	return 0;
}

void dsp_wdt_exit(void)
{
}
#endif


/*
 * Copyright 2009 Dmitriy Taychenachev <dimichxp@gmail.com>
 *
 * This file is released under the GPLv2 or later.
 */

#include <linux/delay.h>
#include <linux/io.h>

#include <asm/proc-fns.h>
#include <mach/hardware.h>

#include "crm_regs.h"

#define WDOG_WCR		MXC91231_IO_ADDRESS(MXC91231_WDOG1_BASE_ADDR)
#define WDOG_WCR_OUT_ENABLE	(1 << 6)
#define WDOG_WCR_ASSERT		(1 << 5)

void mxc91231_power_off(void)
{
	u16 wcr;

	wcr = __raw_readw(WDOG_WCR);
	wcr |= WDOG_WCR_OUT_ENABLE;
	wcr &= ~WDOG_WCR_ASSERT;
	__raw_writew(wcr, WDOG_WCR);
}

void mxc91231_arch_reset(char mode, const char *cmd)
{
	u32 amcr;

	/* Reset the AP using CRM */
	amcr = __raw_readl(MXC_CRMAP_AMCR);
	amcr &= ~MXC_CRMAP_AMCR_SW_AP;
	__raw_writel(amcr, MXC_CRMAP_AMCR);

	mdelay(10);
	cpu_reset(0);
}

void mxc91231_prepare_idle(void)
{
	u32 crm_ctl;

	/* Go to WAIT mode after WFI */
	crm_ctl = __raw_readl(MXC_DSM_CRM_CONTROL);
	crm_ctl &= ~(MXC_DSM_CRM_CTRL_LPMD0 | MXC_DSM_CRM_CTRL_LPMD1);
	crm_ctl |=  MXC_DSM_CRM_CTRL_LPMD_WAIT_MODE;
	__raw_writel(crm_ctl, MXC_DSM_CRM_CONTROL);
}

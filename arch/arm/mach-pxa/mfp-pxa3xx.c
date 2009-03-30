/*
 * linux/arch/arm/mach-pxa/mfp.c
 *
 * PXA3xx Multi-Function Pin Support
 *
 * Copyright (C) 2007 Marvell Internation Ltd.
 *
 * 2007-08-21: eric miao <eric.miao@marvell.com>
 *             initial version
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/sysdev.h>

#include <mach/hardware.h>
#include <mach/mfp-pxa3xx.h>
#include <mach/pxa3xx-regs.h>

#ifdef CONFIG_PM
/*
 * Configure the MFPs appropriately for suspend/resume.
 * FIXME: this should probably depend on which system state we're
 * entering - for instance, we might not want to place MFP pins in
 * a pull-down mode if they're an active low chip select, and we're
 * just entering standby.
 */
static int pxa3xx_mfp_suspend(struct sys_device *d, pm_message_t state)
{
	mfp_config_lpm();
	return 0;
}

static int pxa3xx_mfp_resume(struct sys_device *d)
{
	mfp_config_run();

	/* clear RDH bit when MFP settings are restored
	 *
	 * NOTE: the last 3 bits DxS are write-1-to-clear so carefully
	 * preserve them here in case they will be referenced later
	 */
	ASCR &= ~(ASCR_RDH | ASCR_D1S | ASCR_D2S | ASCR_D3S);
	return 0;
}
#else
#define pxa3xx_mfp_suspend	NULL
#define pxa3xx_mfp_resume	NULL
#endif

struct sysdev_class pxa3xx_mfp_sysclass = {
	.name		= "mfp",
	.suspend	= pxa3xx_mfp_suspend,
	.resume 	= pxa3xx_mfp_resume,
};

static int __init mfp_init_devicefs(void)
{
	if (cpu_is_pxa3xx())
		return sysdev_class_register(&pxa3xx_mfp_sysclass);

	return 0;
}
postcore_initcall(mfp_init_devicefs);

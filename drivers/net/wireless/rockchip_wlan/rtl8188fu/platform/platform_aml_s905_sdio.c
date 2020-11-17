/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2018 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#include <linux/printk.h>		/* pr_info(() */
#include <linux/delay.h>		/* msleep() */
#include "platform_aml_s905_sdio.h"	/* sdio_reinit() and etc */


/*
 * Return:
 *	0:	power on successfully
 *	others:	power on failed
 */
int platform_wifi_power_on(void)
{
	int ret = 0;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))
	ret = wifi_setup_dt();
	if (ret) {
		pr_err("%s: setup dt failed!!(%d)\n", __func__, ret);
		return -1;
	}
#endif /* kernel < 3.14.0 */

#if 0 /* Seems redundancy? Already done before insert driver */
	pr_info("######%s:\n", __func__);
	extern_wifi_set_enable(0);
	msleep(500);
	extern_wifi_set_enable(1);
	msleep(500);
	sdio_reinit();
#endif

	return ret;
}

void platform_wifi_power_off(void)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))
	wifi_teardown_dt();
#endif /* kernel < 3.14.0 */
}

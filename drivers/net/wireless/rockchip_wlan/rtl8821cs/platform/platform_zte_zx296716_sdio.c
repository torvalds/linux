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
#include "platform_zte_zx296716_sdio.h"	/* sdio_reinit() and etc */


/*
 * Return:
 *	0:	power on successfully
 *	others:	power on failed
 */
int platform_wifi_power_on(void)
{
	int ret = 0;

	pr_info("######%s: disable--1--\n", __func__);
	extern_wifi_set_enable(0);
	/*msleep(500);*/ /* add in function:extern_wifi_set_enable */
	pr_info("######%s: enable--2---\n", __func__);
	extern_wifi_set_enable(1);
	/*msleep(500);*/
	sdio_reinit();

	return ret;
}

void platform_wifi_power_off(void)
{
	int card_val;

	pr_info("######%s:\n", __func__);
#ifdef CONFIG_A16T03_BOARD
	card_val = sdio_host_is_null();
	if (card_val)
		remove_card();
#endif /* CONFIG_A16T03_BOARD */
	extern_wifi_set_enable(0);

	/*msleep(500);*/
}

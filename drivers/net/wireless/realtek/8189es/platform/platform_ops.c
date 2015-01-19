/******************************************************************************
 *
 * Copyright(c) 2013 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#include <linux/time.h>
#include <linux/delay.h>
#ifndef CONFIG_PLATFORM_OPS
extern void sdio_reinit(void);
extern void extern_wifi_set_enable(int is_on);
/*
 * Return:
 *	0:	power on successfully
 *	others: power on failed
 */
int platform_wifi_power_on(void)
{
	int ret = 0;

	printk("######%s: \n",__func__);
	extern_wifi_set_enable(0);
	msleep(500);
	extern_wifi_set_enable(1);
	msleep(500);
	sdio_reinit();
	return ret;
}

void platform_wifi_power_off(void)
{
//	extern_wifi_set_enable(0);
}
#endif // !CONFIG_PLATFORM_OPS

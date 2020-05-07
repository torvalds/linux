/******************************************************************************
 *
 * Copyright(c) 2013 - 2017 Realtek Corporation.
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
#include <drv_types.h>

int platform_wifi_power_on(void)
{
	int ret = 0;
	u32 tmp;
	tmp = readl((volatile unsigned int *)0xb801a608);
	tmp &= 0xffffff00;
	tmp |= 0x55;
	writel(tmp, (volatile unsigned int *)0xb801a608); /* write dummy register for 1055 */
	return ret;
}

void platform_wifi_power_off(void)
{
}

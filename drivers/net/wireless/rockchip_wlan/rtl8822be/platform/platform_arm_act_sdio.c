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
/*
 * Description:
 *	This file can be applied to following platforms:
 *    CONFIG_PLATFORM_ACTIONS_ATM703X
 */
#include <drv_types.h>

#ifdef CONFIG_PLATFORM_ACTIONS_ATM705X
	extern int acts_wifi_init(void);
	extern void acts_wifi_cleanup(void);
#endif

/*
 * Return:
 *	0:	power on successfully
 *	others: power on failed
 */
int platform_wifi_power_on(void)
{
	int ret = 0;

#ifdef CONFIG_PLATFORM_ACTIONS_ATM705X
	ret = acts_wifi_init();
	if (unlikely(ret < 0)) {
		pr_err("%s Failed to register the power control driver.\n", __FUNCTION__);
		goto exit;
	}
#endif

exit:
	return ret;
}

void platform_wifi_power_off(void)
{
#ifdef CONFIG_PLATFORM_ACTIONS_ATM705X
	acts_wifi_cleanup();
#endif
}

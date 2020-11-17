/* SPDX-License-Identifier: GPL-2.0 */
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
/*
 * Description:
 *	This file can be applied to following platforms:
 *	CONFIG_PLATFORM_ARM_SUNXI Series platform
 *
 */

#include <drv_types.h>
#include <mach/sys_config.h>

#ifdef CONFIG_PLATFORM_ARM_SUNxI
extern int sw_usb_disable_hcd(__u32 usbc_no);
extern int sw_usb_enable_hcd(__u32 usbc_no);
static int usb_wifi_host = 2;
#endif

#if defined(CONFIG_PLATFORM_ARM_SUN6I) || defined(CONFIG_PLATFORM_ARM_SUN7I)
extern int sw_usb_disable_hcd(__u32 usbc_no);
extern int sw_usb_enable_hcd(__u32 usbc_no);
extern void wifi_pm_power(int on);
static script_item_u item;
#endif

#ifdef CONFIG_PLATFORM_ARM_SUN8I
extern int sunxi_usb_disable_hcd(__u32 usbc_no);
extern int sunxi_usb_enable_hcd(__u32 usbc_no);
extern void wifi_pm_power(int on);
static script_item_u item;
#endif


int platform_wifi_power_on(void)
{
	int ret = 0;

#ifdef CONFIG_PLATFORM_ARM_SUNxI
#ifndef CONFIG_RTL8723A
	{
		/* ----------get usb_wifi_usbc_num------------- */
		ret = script_parser_fetch("usb_wifi_para", "usb_wifi_usbc_num", (int *)&usb_wifi_host, 64);
		if (ret != 0) {
			RTW_INFO("ERR: script_parser_fetch usb_wifi_usbc_num failed\n");
			ret = -ENOMEM;
			goto exit;
		}
		RTW_INFO("sw_usb_enable_hcd: usbc_num = %d\n", usb_wifi_host);
		sw_usb_enable_hcd(usb_wifi_host);
	}
#endif /* CONFIG_RTL8723A */
#endif /* CONFIG_PLATFORM_ARM_SUNxI */

#if defined(CONFIG_PLATFORM_ARM_SUN6I) || defined(CONFIG_PLATFORM_ARM_SUN7I)
	{
		script_item_value_type_e type;

		type = script_get_item("wifi_para", "wifi_usbc_id", &item);
		if (SCIRPT_ITEM_VALUE_TYPE_INT != type) {
			printk("ERR: script_get_item wifi_usbc_id failed\n");
			ret = -ENOMEM;
			goto exit;
		}

		printk("sw_usb_enable_hcd: usbc_num = %d\n", item.val);
		wifi_pm_power(1);
		mdelay(10);

#if !(defined(CONFIG_RTL8723A)) && !(defined(CONFIG_RTL8723B))
		sw_usb_enable_hcd(item.val);
#endif
	}
#endif /* defined(CONFIG_PLATFORM_ARM_SUN6I) || defined(CONFIG_PLATFORM_ARM_SUN7I) */

#if defined(CONFIG_PLATFORM_ARM_SUN8I)
	{
		script_item_value_type_e type;

		type = script_get_item("wifi_para", "wifi_usbc_id", &item);
		if (SCIRPT_ITEM_VALUE_TYPE_INT != type) {
			printk("ERR: script_get_item wifi_usbc_id failed\n");
			ret = -ENOMEM;
			goto exit;
		}

		printk("sw_usb_enable_hcd: usbc_num = %d\n", item.val);
		wifi_pm_power(1);
		mdelay(10);

#if !(defined(CONFIG_RTL8723A)) && !(defined(CONFIG_RTL8723B))
		sunxi_usb_enable_hcd(item.val);
#endif
	}
#endif /* CONFIG_PLATFORM_ARM_SUN8I */

exit:
	return ret;
}

void platform_wifi_power_off(void)
{

#ifdef CONFIG_PLATFORM_ARM_SUNxI
#ifndef CONFIG_RTL8723A
	RTW_INFO("sw_usb_disable_hcd: usbc_num = %d\n", usb_wifi_host);
	sw_usb_disable_hcd(usb_wifi_host);
#endif /* ifndef CONFIG_RTL8723A */
#endif /* CONFIG_PLATFORM_ARM_SUNxI */

#if defined(CONFIG_PLATFORM_ARM_SUN6I) || defined(CONFIG_PLATFORM_ARM_SUN7I)
	#if !(defined(CONFIG_RTL8723A)) && !(defined(CONFIG_RTL8723B))
	sw_usb_disable_hcd(item.val);
	#endif
	wifi_pm_power(0);
#endif /* defined(CONFIG_PLATFORM_ARM_SUN6I) || defined(CONFIG_PLATFORM_ARM_SUN7I) */

#if defined(CONFIG_PLATFORM_ARM_SUN8I)
	#if !(defined(CONFIG_RTL8723A)) && !(defined(CONFIG_RTL8723B))
	sunxi_usb_disable_hcd(item.val);
	#endif
	wifi_pm_power(0);
#endif /* defined(CONFIG_PLATFORM_ARM_SUN8I) */

}

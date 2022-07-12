/*
 * Platform Dependent file for Qualcomm MSM/APQ
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: dhd_custom_msm.c 695148 2017-04-19 04:15:17Z $
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/skbuff.h>
#include <linux/wlan_plat.h>
#include <linux/mmc/host.h>
#include <linux/msm_pcie.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/of_gpio.h>
#if defined(CONFIG_ARCH_MSM8996) || defined(CONFIG_ARCH_MSM8998) || \
	defined(CONFIG_ARCH_SDM845) || defined(CONFIG_ARCH_SM8150) || \
	defined(USE_CUSTOM_MSM_PCIE)
#include <linux/msm_pcie.h>
#endif /* CONFIG_ARCH_MSM8996 || CONFIG_ARCH_MSM8998 ||
	  CONFIG_ARCH_SDM845 || CONFIG_ARCH_SM8150
	*/

#ifdef CONFIG_BROADCOM_WIFI_RESERVED_MEM
extern int dhd_init_wlan_mem(void);
extern void *dhd_wlan_mem_prealloc(int section, unsigned long size);
#endif /* CONFIG_BROADCOM_WIFI_RESERVED_MEM */

#define WIFI_TURNON_DELAY       200
static int wlan_reg_on = -1;
#ifdef CUSTOM_DT_COMPAT_ENTRY
#define DHD_DT_COMPAT_ENTRY		CUSTOM_DT_COMPAT_ENTRY
#else /* CUSTOM_DT_COMPAT_ENTRY */
#define DHD_DT_COMPAT_ENTRY		"android,bcmdhd_wlan"
#endif /* CUSTOM_DT_COMPAT_ENTRY */

#ifdef CUSTOMER_HW2
#define WIFI_WL_REG_ON_PROPNAME		"wl_reg_on"
#else
#define WIFI_WL_REG_ON_PROPNAME		"wlan-en-gpio"
#endif /* CUSTOMER_HW2 */

#if defined(CONFIG_ARCH_MSM8996) || defined(CONFIG_ARCH_MSM8998) || \
	defined(CONFIG_ARCH_SDM845) || defined(CONFIG_ARCH_SM8150)
#define MSM_PCIE_CH_NUM			0
#elif defined(USE_CUSTOM_MSM_PCIE)
#define MSM_PCIE_CH_NUM			MSM_PCIE_CUSTOM_CH_NUM
#else
#define MSM_PCIE_CH_NUM			1
#endif /* CONFIG_ARCH_MSM8996 || CONFIG_ARCH_MSM8998
	  || CONFIG_ARCH_SDM845 || CONFIG_ARCH_SM8150
	*/

#ifdef CONFIG_BCMDHD_OOB_HOST_WAKE
static int wlan_host_wake_up = -1;
static int wlan_host_wake_irq = 0;
#ifdef CUSTOMER_HW2
#define WIFI_WLAN_HOST_WAKE_PROPNAME    "wl_host_wake"
#else
#define WIFI_WLAN_HOST_WAKE_PROPNAME    "wlan-host-wake-gpio"
#endif /* CUSTOMER_HW2 */
#endif /* CONFIG_BCMDHD_OOB_HOST_WAKE */

#ifndef USE_CUSTOM_MSM_PCIE
int __init
#else
int
#endif /* USE_CUSTOM_MSM_PCIE */
dhd_wifi_init_gpio(void)
{
	char *wlan_node = DHD_DT_COMPAT_ENTRY;
	struct device_node *root_node = NULL;

	root_node = of_find_compatible_node(NULL, NULL, wlan_node);
	if (!root_node) {
		WARN(1, "failed to get device node of BRCM WLAN\n");
		return -ENODEV;
	}

	/* ========== WLAN_PWR_EN ============ */
	wlan_reg_on = of_get_named_gpio(root_node, WIFI_WL_REG_ON_PROPNAME, 0);
	printk(KERN_INFO "%s: gpio_wlan_power : %d\n", __FUNCTION__, wlan_reg_on);

	if (gpio_request_one(wlan_reg_on, GPIOF_OUT_INIT_LOW, "WL_REG_ON")) {
		printk(KERN_ERR "%s: Faiiled to request gpio %d for WL_REG_ON\n",
			__FUNCTION__, wlan_reg_on);
	} else {
		printk(KERN_ERR "%s: gpio_request WL_REG_ON done - WLAN_EN: GPIO %d\n",
			__FUNCTION__, wlan_reg_on);
	}

	if (gpio_direction_output(wlan_reg_on, 1)) {
		printk(KERN_ERR "%s: WL_REG_ON failed to pull up\n", __FUNCTION__);
	} else {
		printk(KERN_ERR "%s: WL_REG_ON is pulled up\n", __FUNCTION__);
	}

	if (gpio_get_value(wlan_reg_on)) {
		printk(KERN_INFO "%s: Initial WL_REG_ON: [%d]\n",
			__FUNCTION__, gpio_get_value(wlan_reg_on));
	}

	/* Wait for WIFI_TURNON_DELAY due to power stability */
	msleep(WIFI_TURNON_DELAY);

#ifdef CONFIG_BCMDHD_OOB_HOST_WAKE
	/* ========== WLAN_HOST_WAKE ============ */
	wlan_host_wake_up = of_get_named_gpio(root_node, WIFI_WLAN_HOST_WAKE_PROPNAME, 0);
	printk(KERN_INFO "%s: gpio_wlan_host_wake : %d\n", __FUNCTION__, wlan_host_wake_up);

#ifndef CUSTOMER_HW2
	if (gpio_request_one(wlan_host_wake_up, GPIOF_IN, "WLAN_HOST_WAKE")) {
		printk(KERN_ERR "%s: Faiiled to request gpio %d for WLAN_HOST_WAKE\n",
			__FUNCTION__, wlan_host_wake_up);
			return -ENODEV;
	} else {
		printk(KERN_ERR "%s: gpio_request WLAN_HOST_WAKE done"
			" - WLAN_HOST_WAKE: GPIO %d\n",
			__FUNCTION__, wlan_host_wake_up);
	}
#endif /* !CUSTOMER_HW2 */

	gpio_direction_input(wlan_host_wake_up);
	wlan_host_wake_irq = gpio_to_irq(wlan_host_wake_up);
#endif /* CONFIG_BCMDHD_OOB_HOST_WAKE */

#if defined(CONFIG_BCM4359) || defined(CONFIG_BCM4361) || defined(CONFIG_BCM4375) || \
	defined(USE_CUSTOM_MSM_PCIE)
	printk(KERN_INFO "%s: Call msm_pcie_enumerate\n", __FUNCTION__);
	msm_pcie_enumerate(MSM_PCIE_CH_NUM);
#endif /* CONFIG_BCM4359 || CONFIG_BCM4361 || CONFIG_BCM4375 */

	return 0;
}

int
dhd_wlan_power(int onoff)
{
	printk(KERN_INFO"------------------------------------------------");
	printk(KERN_INFO"------------------------------------------------\n");
	printk(KERN_INFO"%s Enter: power %s\n", __func__, onoff ? "on" : "off");

	if (onoff) {
		if (gpio_direction_output(wlan_reg_on, 1)) {
			printk(KERN_ERR "%s: WL_REG_ON is failed to pull up\n", __FUNCTION__);
			return -EIO;
		}
		if (gpio_get_value(wlan_reg_on)) {
			printk(KERN_INFO"WL_REG_ON on-step-2 : [%d]\n",
				gpio_get_value(wlan_reg_on));
		} else {
			printk("[%s] gpio value is 0. We need reinit.\n", __func__);
			if (gpio_direction_output(wlan_reg_on, 1)) {
				printk(KERN_ERR "%s: WL_REG_ON is "
					"failed to pull up\n", __func__);
			}
		}
	} else {
		if (gpio_direction_output(wlan_reg_on, 0)) {
			printk(KERN_ERR "%s: WL_REG_ON is failed to pull up\n", __FUNCTION__);
			return -EIO;
		}
		if (gpio_get_value(wlan_reg_on)) {
			printk(KERN_INFO"WL_REG_ON on-step-2 : [%d]\n",
				gpio_get_value(wlan_reg_on));
		}
	}
	return 0;
}
EXPORT_SYMBOL(dhd_wlan_power);

void
post_power_operation(int on)
{
#if defined(CONFIG_BCM4359) || defined(CONFIG_BCM4361) || defined(CONFIG_BCM4375) || \
	defined(USE_CUSTOM_MSM_PCIE)
	printk(KERN_INFO "%s: Call msm_pcie_enumerate\n", __FUNCTION__);
	if (on) {
		msm_pcie_enumerate(MSM_PCIE_CH_NUM);
	}
#endif /* CONFIG_BCM4359 || CONFIG_BCM4361 || CONFIG_BCM4375 */
}

static int
dhd_wlan_reset(int onoff)
{
	return 0;
}

static int
dhd_wlan_set_carddetect(int val)
{
	return 0;
}

#if defined(CONFIG_BCMDHD_OOB_HOST_WAKE) && defined(CONFIG_BCMDHD_GET_OOB_STATE)
int
dhd_get_wlan_oob_gpio(void)
{
	return gpio_is_valid(wlan_host_wake_up) ?
		gpio_get_value(wlan_host_wake_up) : -1;
}
EXPORT_SYMBOL(dhd_get_wlan_oob_gpio);
#endif /* CONFIG_BCMDHD_OOB_HOST_WAKE && CONFIG_BCMDHD_GET_OOB_STATE */

struct resource dhd_wlan_resources = {
	.name	= "bcmdhd_wlan_irq",
	.start	= 0, /* Dummy */
	.end	= 0, /* Dummy */
	.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_SHAREABLE |
#ifdef CONFIG_BCMDHD_PCIE
	IORESOURCE_IRQ_HIGHEDGE,
#else
	IORESOURCE_IRQ_HIGHLEVEL,
#endif /* CONFIG_BCMDHD_PCIE */
};
EXPORT_SYMBOL(dhd_wlan_resources);

struct wifi_platform_data dhd_wlan_control = {
	.set_power	= dhd_wlan_power,
	.set_reset	= dhd_wlan_reset,
	.set_carddetect	= dhd_wlan_set_carddetect,
#ifdef CONFIG_BROADCOM_WIFI_RESERVED_MEM
	.mem_prealloc	= dhd_wlan_mem_prealloc,
#endif /* CONFIG_BROADCOM_WIFI_RESERVED_MEM */
};
EXPORT_SYMBOL(dhd_wlan_control);

#ifndef USE_CUSTOM_MSM_PCIE
int __init
#else
int
#endif /* USE_CUSTOM_MSM_PCIE */
dhd_wlan_init(void)
{
	int ret;

	printk(KERN_INFO"%s: START.......\n", __FUNCTION__);
	ret = dhd_wifi_init_gpio();
	if (ret < 0) {
		printk(KERN_ERR "%s: failed to initiate GPIO, ret=%d\n",
			__FUNCTION__, ret);
		goto fail;
	}

#ifdef CONFIG_BCMDHD_OOB_HOST_WAKE
	dhd_wlan_resources.start = wlan_host_wake_irq;
	dhd_wlan_resources.end = wlan_host_wake_irq;
#endif /* CONFIG_BCMDHD_OOB_HOST_WAKE */

#ifdef CONFIG_BROADCOM_WIFI_RESERVED_MEM
	ret = dhd_init_wlan_mem();
	if (ret < 0) {
		printk(KERN_ERR "%s: failed to alloc reserved memory,"
			" ret=%d\n", __FUNCTION__, ret);
	}
#endif /* CONFIG_BROADCOM_WIFI_RESERVED_MEM */

fail:
	printk(KERN_INFO"%s: FINISH.......\n", __FUNCTION__);
	return ret;
}
#if defined(CONFIG_ARCH_MSM8996) || defined(CONFIG_ARCH_MSM8998) || \
	defined(CONFIG_ARCH_SDM845) || defined(CONFIG_ARCH_SM8150)
#if defined(CONFIG_DEFERRED_INITCALLS)
deferred_module_init(dhd_wlan_init);
#else
late_initcall(dhd_wlan_init);
#endif /* CONFIG_DEFERRED_INITCALLS */
#else
#ifndef USE_CUSTOM_MSM_PCIE
device_initcall(dhd_wlan_init);
#endif /* !USE_CUSTOM_MSM_PCIE */
#endif /* CONFIG_ARCH_MSM8996 || CONFIG_ARCH_MSM8998
	* CONFIG_ARCH_SDM845 || CONFIG_ARCH_SM8150
	*/

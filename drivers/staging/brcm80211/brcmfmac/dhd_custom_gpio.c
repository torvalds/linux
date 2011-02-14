/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/netdevice.h>
#include <osl.h>
#include <bcmutils.h>

#include <dngl_stats.h>
#include <dhd.h>

#include <wlioctl.h>
#include <wl_iw.h>

#define WL_ERROR(fmt, args...) printk(fmt, ##args)
#define WL_TRACE(fmt, args...) no_printk(fmt, ##args)

#ifdef CUSTOMER_HW
extern void bcm_wlan_power_off(int);
extern void bcm_wlan_power_on(int);
#endif				/* CUSTOMER_HW */
#ifdef CUSTOMER_HW2
int wifi_set_carddetect(int on);
int wifi_set_power(int on, unsigned long msec);
int wifi_get_irq_number(unsigned long *irq_flags_ptr);
#endif

#if defined(OOB_INTR_ONLY)

#if defined(BCMLXSDMMC)
extern int sdioh_mmc_irq(int irq);
#endif				/* (BCMLXSDMMC)  */

#ifdef CUSTOMER_HW3
#include <mach/gpio.h>
#endif

/* Customer specific Host GPIO defintion  */
static int dhd_oob_gpio_num = -1;	/* GG 19 */

module_param(dhd_oob_gpio_num, int, 0644);
MODULE_PARM_DESC(dhd_oob_gpio_num, "DHD oob gpio number");

int dhd_customer_oob_irq_map(unsigned long *irq_flags_ptr)
{
	int host_oob_irq = 0;

#ifdef CUSTOMER_HW2
	host_oob_irq = wifi_get_irq_number(irq_flags_ptr);

#else				/* for NOT  CUSTOMER_HW2 */
#if defined(CUSTOM_OOB_GPIO_NUM)
	if (dhd_oob_gpio_num < 0)
		dhd_oob_gpio_num = CUSTOM_OOB_GPIO_NUM;
#endif

	if (dhd_oob_gpio_num < 0) {
		WL_ERROR("%s: ERROR customer specific Host GPIO is NOT defined\n",
			 __func__);
		return dhd_oob_gpio_num;
	}

	WL_ERROR("%s: customer specific Host GPIO number is (%d)\n",
		 __func__, dhd_oob_gpio_num);

#if defined CUSTOMER_HW
	host_oob_irq = MSM_GPIO_TO_INT(dhd_oob_gpio_num);
#elif defined CUSTOMER_HW3
	gpio_request(dhd_oob_gpio_num, "oob irq");
	host_oob_irq = gpio_to_irq(dhd_oob_gpio_num);
	gpio_direction_input(dhd_oob_gpio_num);
#endif				/* CUSTOMER_HW */
#endif				/* CUSTOMER_HW2 */

	return host_oob_irq;
}
#endif				/* defined(OOB_INTR_ONLY) */

/* Customer function to control hw specific wlan gpios */
void dhd_customer_gpio_wlan_ctrl(int onoff)
{
	switch (onoff) {
	case WLAN_RESET_OFF:
		WL_TRACE("%s: call customer specific GPIO to insert WLAN RESET\n",
			 __func__);
#ifdef CUSTOMER_HW
		bcm_wlan_power_off(2);
#endif				/* CUSTOMER_HW */
#ifdef CUSTOMER_HW2
		wifi_set_power(0, 0);
#endif
		WL_ERROR("=========== WLAN placed in RESET ========\n");
		break;

	case WLAN_RESET_ON:
		WL_TRACE("%s: callc customer specific GPIO to remove WLAN RESET\n",
			 __func__);
#ifdef CUSTOMER_HW
		bcm_wlan_power_on(2);
#endif				/* CUSTOMER_HW */
#ifdef CUSTOMER_HW2
		wifi_set_power(1, 0);
#endif
		WL_ERROR("=========== WLAN going back to live  ========\n");
		break;

	case WLAN_POWER_OFF:
		WL_TRACE("%s: call customer specific GPIO to turn off WL_REG_ON\n",
			 __func__);
#ifdef CUSTOMER_HW
		bcm_wlan_power_off(1);
#endif				/* CUSTOMER_HW */
		break;

	case WLAN_POWER_ON:
		WL_TRACE("%s: call customer specific GPIO to turn on WL_REG_ON\n",
			 __func__);
#ifdef CUSTOMER_HW
		bcm_wlan_power_on(1);
#endif				/* CUSTOMER_HW */
		/* Lets customer power to get stable */
		udelay(200);
		break;
	}
}

#ifdef GET_CUSTOM_MAC_ENABLE
/* Function to get custom MAC address */
int dhd_custom_get_mac_address(unsigned char *buf)
{
	WL_TRACE("%s Enter\n", __func__);
	if (!buf)
		return -EINVAL;

	/* Customer access to MAC address stored outside of DHD driver */

#ifdef EXAMPLE_GET_MAC
	/* EXAMPLE code */
	{
		u8 ea_example[ETH_ALEN] = {0x00, 0x11, 0x22, 0x33, 0x44, 0xFF};
		memcpy(buf, ea_example, ETH_ALEN);
	}
#endif				/* EXAMPLE_GET_MAC */

	return 0;
}
#endif				/* GET_CUSTOM_MAC_ENABLE */

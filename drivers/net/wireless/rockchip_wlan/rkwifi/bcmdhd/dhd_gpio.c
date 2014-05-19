/*
* Customer code to add GPIO control during WLAN start/stop
* Copyright (C) 1999-2011, Broadcom Corporation
* 
*         Unless you and Broadcom execute a separate written software license
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
* $Id: dhd_custom_gpio.c,v 1.2.42.1 2010-10-19 00:41:09 Exp $
*/

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/rfkill.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/rfkill-wlan.h>

#ifdef CUSTOMER_HW
#ifdef CUSTOMER_OOB
int bcm_wlan_get_oob_irq(void)
{
    return rockchip_wifi_get_oob_irq();
}
#endif

void bcm_wlan_power_on(int flag)
{
	if (flag == 1) {
		printk("======== PULL WL_REG_ON HIGH! ========\n");
		rockchip_wifi_power(1);
        rockchip_wifi_set_carddetect(1);
	} else {
		printk("======== PULL WL_REG_ON HIGH! (flag = %d) ========\n", flag);
		rockchip_wifi_power(1);
	}
}

void bcm_wlan_power_off(int flag)
{
	if (flag == 1) {
		printk("======== Card detection to remove SDIO card! ========\n");
		rockchip_wifi_power(1);
        rockchip_wifi_set_carddetect(0);
		rockchip_wifi_power(0);
	} else {
		printk("======== PULL WL_REG_ON LOW! (flag = %d) ========\n", flag);
		rockchip_wifi_power(0);
	}
}

#endif /* CUSTOMER_HW */

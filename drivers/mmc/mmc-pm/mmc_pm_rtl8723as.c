/*
; 9 - realtek rtl8723as(combo) sdio wifi + bt gpio config
;rtk_rtl8723as_wb_pwr       = port:PH12<1><default><default><0>
;rtk_rtl8723as_wl_dis       = port:PH11<1><default><default><0>
;rtk_rtl8723as_wl_wps       = port:PH09<0><default><default><0>
;rtk_rtl8723as_bt_dis       = port:PB05<1><default><default><0>*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <plat/sys_config.h>

#include "mmc_pm.h"

#define SDIO_MODULE_NAME "RTL8723AS"
#define rtw_msg(...)    do {printk("[RTL8723AS]: "__VA_ARGS__);} while(0)
static int rtl8723as_wl_on = 0;
static int rtl8723as_bt_on = 0;

static int rtk_suspend = 0;

static int rtl8723as_gpio_ctrl(char* name, int level)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    char* gpio_cmd[4] = {"rtk_rtl8723as_wb_pwr", "rtk_rtl8723as_wl_dis", "rtk_rtl8723as_bt_dis", "rtk_rtl8723as_wl_wps"};
    int i = 0;
    int ret = 0;
    
    for (i=0; i<4; i++) {
        if (strcmp(name, gpio_cmd[i])==0)
            break;
    }
    if (i==4) {
        rtw_msg("No gpio %s for %s module\n", name, SDIO_MODULE_NAME);
        return -1;
    }
    
    rtw_msg("Set GPIO %s to %d !\n", name, level);
    if (strcmp(name, "rtk_rtl8723as_wl_dis") == 0) {
        if ((level && !rtl8723as_bt_on)
            || (!level && !rtl8723as_bt_on)) {
            rtw_msg("%s is powered %s by wifi\n", SDIO_MODULE_NAME, level ? "up" : "down");
            goto power_change;
        } else {
            if (level) {
                rtw_msg("%s is already on by bt\n", SDIO_MODULE_NAME);
            } else {
                rtw_msg("%s should stay on because of bt\n", SDIO_MODULE_NAME);
            }
            goto state_change;
        }
    }
    if (strcmp(name, "rtk_rtl8723as_bt_dis") == 0) {
        if ((level && !rtl8723as_wl_on)
            || (!level && !rtl8723as_wl_on)) {
            rtw_msg("%s is powered %s by bt\n", SDIO_MODULE_NAME, level ? "up" : "down");
            goto power_change;
        } else {
            if (level) {
                rtw_msg("%s is already on by wifi\n", SDIO_MODULE_NAME);
            } else {
                rtw_msg("%s should stay on because of wifi\n", SDIO_MODULE_NAME);
            }
            goto state_change;
        }
    }

gpio_state_change:
    ret = gpio_write_one_pin_value(ops->pio_hdle, level, name);
    if (ret) {
        rtw_msg("Failed to set gpio %s to %d !\n", name, level);
        return -1;
    }
    
    return 0;
    
power_change:
    ret = gpio_write_one_pin_value(ops->pio_hdle, level, "rtk_rtl8723as_wb_pwr");
    if (ret) {
        rtw_msg("Failed to power off %s module!\n", SDIO_MODULE_NAME);
        return -1;
    }
    udelay(500);
    
state_change:
    if (strcmp(name, "rtk_rtl8723as_wl_dis")==0)
        rtl8723as_wl_on = level;
    if (strcmp(name, "rtk_rtl8723as_bt_dis")==0)
        rtl8723as_bt_on = level;
    rtw_msg("%s power state change: wifi %d, bt %d !!\n", SDIO_MODULE_NAME, rtl8723as_wl_on, rtl8723as_bt_on);
    
    goto gpio_state_change;
}

static int rtl8723as_get_gpio_value(char* name)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    
    if (strcmp(name, "rtk_rtl8723as_wl_wps")) {
        rtw_msg("No gpio %s for %s\n", name, SDIO_MODULE_NAME);
        return -1;
    }
    
    return gpio_read_one_pin_value(ops->pio_hdle, name);
}

void rtl8723as_power(int mode, int* updown)
{
    if (mode) {
        if (*updown) {
        	rtl8723as_gpio_ctrl("rtk_rtl8723as_wl_dis", 1);
        } else {
        	rtl8723as_gpio_ctrl("rtk_rtl8723as_wl_dis", 0);
        }
    } else {
        if (rtl8723as_wl_on)
            *updown = 1;
        else
            *updown = 0;
		rtw_msg("sdio wifi power state: %s\n", rtl8723as_wl_on ? "on" : "off");
    }
    return;
}

static void rtl8723as_standby(int instadby)
{
    if (instadby) {
        if (rtl8723as_wl_on) {
            rtl8723as_gpio_ctrl("rtk_rtl8723as_wl_dis", 0);
            printk("%s: mmc_pm_suspend.\n", __FUNCTION__);
            rtk_suspend = 1;
        }
    } else {
        if (rtk_suspend) {
            rtl8723as_gpio_ctrl("rtk_rtl8723as_wl_dis", 1);
            sunximmc_rescan_card(3, 1);
            printk("%s: mmc_pm_resume.\n", __FUNCTION__);
            rtk_suspend = 0;
        }
    }
}

void rtl8723as_gpio_init(void)
{
	struct mmc_pm_ops *ops = &mmc_card_pm_ops;
	int updown = 1;

	rtl8723as_wl_on = 0;
	rtl8723as_bt_on = 0;
	rtk_suspend	= 0;
	ops->gpio_ctrl	= rtl8723as_gpio_ctrl;
	ops->get_io_val = rtl8723as_get_gpio_value;
	ops->power	= rtl8723as_power;
	ops->standby	= rtl8723as_standby;
	rtl8723as_power(1, &updown);
	sunximmc_rescan_card(ops->sdio_cardid, 1);
	rtw_msg("power up, rescan card.\n");
}

#undef SDIO_MODULE_NAME

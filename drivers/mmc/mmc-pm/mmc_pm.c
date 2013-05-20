/*
 * drivers/mmc/mmc-pm/mmc_pm.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <plat/sys_config.h>
#include <linux/proc_fs.h>
#include "mmc_pm.h"

#define mmc_pm_msg(...)    do {printk("[mmc_pm]: "__VA_ARGS__);} while(0)


struct mmc_pm_ops mmc_card_pm_ops;
static char* wifi_para = "sdio_wifi_para";
static char* wifi_mod[] = {" ", 
							"swl-n20", 	 /* 1 - SWL-N20(Nanoradio NRX600)*/
							"usi-bm01a", /* 2 - USI-BM01A(BCM4329)*/
							"ar6302qfn", /* 3 - AR6302(Atheros 6xxx) */
							"apm6xxx", 	 /* 4 - APM6981/6658 */
							"swb-b23",	 /* 5 - SWB-B23(BCM4329) */
							"hw-mw269",	 /* 6 - HW-MW269X(269/269V2/269V3) */
							"bcm40181",  /* 7 - BCM40181(BCM4330) */
							"bcm40183",  /* 8 - BCM40183(BCM4330) */
							"rtl8723as",  /* 9 - RTL8723AS(RF-SM02B) */
							"rtl8189es"  /* 10 - RTL8189ES(SM89E00) */
							};

static int mmc_pm_get_res(void);

int mmc_pm_get_mod_type(void)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
	mmc_pm_get_res();
    if (ops->sdio_card_used)
        return ops->module_sel;
    else {
        mmc_pm_msg("No sdio card, please check your config !!\n");
        return 0;
    }
}
EXPORT_SYMBOL(mmc_pm_get_mod_type);

int mmc_pm_gpio_ctrl(char* name, int level)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    if (ops->sdio_card_used)
        return ops->gpio_ctrl(name, level);
    else {
        mmc_pm_msg("No sdio card, please check your config !!\n");
        return -1;
    }
}
EXPORT_SYMBOL(mmc_pm_gpio_ctrl);

int mmc_pm_get_io_val(char* name)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    if (ops->sdio_card_used)
        return ops->get_io_val(name);
    else {
        mmc_pm_msg("No sdio card, please check your config !!\n");
        return -1;
    }
}
EXPORT_SYMBOL(mmc_pm_get_io_val);

void mmc_pm_power(int mode, int* updown)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    if (ops->sdio_card_used && ops->power)
        return ops->power(mode, updown);
    else {
        mmc_pm_msg("No sdio card, please check your config !!\n");
        return;
    }
}
EXPORT_SYMBOL(mmc_pm_power);

int mmc_pm_io_shd_suspend_host(void)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    return (ops->module_sel!=2) && (ops->module_sel!=5)
    		 && (ops->module_sel!=6) && (ops->module_sel!=7
    		 && (ops->module_sel!=8));
}
EXPORT_SYMBOL(mmc_pm_io_shd_suspend_host);

#ifdef CONFIG_PROC_FS
static int mmc_pm_power_stat(char *page, char **start, off_t off, int count, int *eof, void *data)
{
    struct mmc_pm_ops *ops = (struct mmc_pm_ops *)data;
    char *p = page;
    int power = 0;
    
    if (ops->power)
        ops->power(0, &power);
    p += sprintf(p, "%s : power state %s\n", ops->mod_name, power ? "on" : "off");
    return p - page;
}

static int mmc_pm_power_ctrl(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
    struct mmc_pm_ops *ops = (struct mmc_pm_ops *)data;
    int power = simple_strtoul(buffer, NULL, 10);
    
    power = power ? 1 : 0;
    if (ops->power)
        ops->power(1, &power);
    else
        mmc_pm_msg("No power control for %s\n", ops->mod_name);
    return sizeof(power);
}

static inline void awsmc_procfs_attach(void)
{
    char proc_rootname[] = "driver/mmc-pm";
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;

    ops->proc_root = proc_mkdir(proc_rootname, NULL);
    if (IS_ERR(ops->proc_root))
    {
        mmc_pm_msg("failed to create procfs \"driver/mmc-pm\".\n");
    }

    ops->proc_power = create_proc_entry("power", 0644, ops->proc_root);
    if (IS_ERR(ops->proc_power))
    {
        mmc_pm_msg("failed to create procfs \"power\".\n");
    }
    ops->proc_power->data = ops;
    ops->proc_power->read_proc = mmc_pm_power_stat;
    ops->proc_power->write_proc = mmc_pm_power_ctrl;
}

static inline void awsmc_procfs_remove(void)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    char proc_rootname[] = "driver/mmc-pm";
    
    remove_proc_entry("power", ops->proc_root);
    remove_proc_entry(proc_rootname, NULL);
}
#else
static inline void awsmc_procfs_attach(void) {}
static inline void awsmc_procfs_remove(void) {}
#endif

static int __mmc_pm_get_res(void)
{
    int ret = 0;
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    
    ret = script_parser_fetch(wifi_para, "sdio_wifi_used", &ops->sdio_card_used, sizeof(unsigned)); 
    if (ret) {
        mmc_pm_msg("failed to fetch sdio card configuration!\n");
        return -1;
    }
    if (!ops->sdio_card_used) {
        mmc_pm_msg("no sdio card used in configuration\n");
        return -1;
    }
    
    ret = script_parser_fetch(wifi_para, "sdio_wifi_sdc_id", &ops->sdio_cardid, sizeof(unsigned));
    if (ret) {
        mmc_pm_msg("failed to fetch sdio card's sdcid\n");
        return -1;
    }

    ret = script_parser_fetch(wifi_para, "sdio_wifi_mod_sel", &ops->module_sel, sizeof(unsigned));
    if (ret) {
        mmc_pm_msg("failed to fetch sdio module select\n");
        return -1;
    }
    ops->mod_name = wifi_mod[ops->module_sel];
    printk("[wifi]: Select sdio wifi: %s !!\n", wifi_mod[ops->module_sel]);
    
    ops->pio_hdle = gpio_request_ex(wifi_para, NULL);
    if (!ops->pio_hdle) {
        mmc_pm_msg("failed to fetch sdio card's io handler, please check it !!\n");
        return -1;
    }
    
    return 0;
}

static int mmc_pm_get_res(void)
{
	static DEFINE_MUTEX(mmc_pm_get_res_mutex);
	static int get_res = 1;

	mutex_lock(&mmc_pm_get_res_mutex);
	if (get_res == 1)
		get_res = __mmc_pm_get_res();
	mutex_unlock(&mmc_pm_get_res_mutex);

	return get_res;
}

static int __devinit mmc_pm_probe(struct platform_device *pdev)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    
    switch (ops->module_sel) {
        case 1: /* nano wifi */
            nano_wifi_gpio_init();
            break;
        case 2: /* usi bm01a */
            usi_bm01a_gpio_init();
            break;
        case 3: /* ar6302qfn */
            //ar6302qfn_gpio_init();
            break;
        case 4: /* apm 6xxx */
            apm_6xxx_gpio_init();
            break;
        case 5: /* swb b23 */
            swbb23_gpio_init();
            break;
        case 6: /* huawei mw269x */
            hwmw269_gpio_init();
            break;
        case 7: /* BCM40181 */
            bcm40181_wifi_gpio_init();
            break;
	case 8: /* BCM40183 */
	    bcm40183_gpio_init();
	    break;
	case 9: /* rtl8723as */
	    rtl8723as_gpio_init();
	    break;
	case 10: /* rtl8189es */
	    rtl8189es_wifi_gpio_init();
	    break;
        default:
            mmc_pm_msg("Wrong sdio module select %d !!\n", ops->module_sel);
    }
    
    awsmc_procfs_attach();
    mmc_pm_msg("SDIO card gpio init is OK !!\n");
    return 0;
}

static int __devexit mmc_pm_remove(struct platform_device *pdev)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    
    switch (ops->module_sel) {
        case 1: /* nano wifi */
            nano_wifi_gpio_init();
            break;
        case 2: /* usi bm01a */
            usi_bm01a_gpio_init();
            break;
        case 3: /* ar6302qfn */
            //ar6302qfn_gpio_init();
            break;
        case 4: /* usi bm01a */
            apm_6xxx_gpio_init();
            break;
        case 5: /* swb b23 */
            swbb23_gpio_init();
            break;
        case 6: /* huawei mw269x */
            hwmw269_gpio_init();
            break;
        case 7: /* BCM40181 */
            bcm40181_wifi_gpio_init();
            break;
	case 8: /* BCM40183 */
	    bcm40183_gpio_init();
	    break;
	case 9: /* rtl8723as */
	    rtl8723as_gpio_init();
	    break;
	case 10: /* rtl8189es */
	    rtl8189es_wifi_gpio_init();
	    break;
        default:
            mmc_pm_msg("Wrong sdio module select %d !!\n", ops->module_sel);
    }
    
    awsmc_procfs_remove();
    mmc_pm_msg("SDIO card gpio is released !!\n");
    return 0;
}

#ifdef CONFIG_PM
static int mmc_pm_suspend(struct device *dev)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    
    if (ops->standby)
        ops->standby(1);
    return 0;
}
static int mmc_pm_resume(struct device *dev)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    
    if (ops->standby)
        ops->standby(0);
    return 0;
}

static struct dev_pm_ops mmc_pm_ops = {
    .suspend	= mmc_pm_suspend,
    .resume		= mmc_pm_resume,
};
#endif

static struct platform_device mmc_pm_dev = {
    .name           = "mmc_pm",
};

static struct platform_driver mmc_pm_driver = {
    .driver.name    = "mmc_pm",
    .driver.owner   = THIS_MODULE,
#ifdef CONFIG_PM
    .driver.pm	    = &mmc_pm_ops,
#endif
    .probe          = mmc_pm_probe,
    .remove         = __devexit_p(mmc_pm_remove),
};

static int __init mmc_pm_init(void)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    
    memset(ops, 0, sizeof(struct mmc_pm_ops));
    mmc_pm_get_res();
    if (!ops->sdio_card_used)
        return 0;
        
    platform_device_register(&mmc_pm_dev);
    return platform_driver_register(&mmc_pm_driver);
}

static void __exit mmc_pm_exit(void)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    if (!ops->sdio_card_used)
        return;
        
    if (ops->pio_hdle)
        gpio_release(ops->pio_hdle, 2);
    
    memset(ops, 0, sizeof(struct mmc_pm_ops));
    platform_driver_unregister(&mmc_pm_driver);
}

module_init(mmc_pm_init);
module_exit(mmc_pm_exit);


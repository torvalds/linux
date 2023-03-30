// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/module.h>
#include <linux/inetdevice.h>
#include "aicwf_usb.h"
#include "rwnx_version_gen.h"
#include "aicwf_rx_prealloc.h"
#include "aicwf_debug.h"
#include "aicwf_txq_prealloc.h"


#define DRV_CONFIG_FW_NAME             "fw.bin"
#define DRV_DESCRIPTION  "AIC BLUETOOTH"
#define DRV_COPYRIGHT    "Copyright(c) 2015-2020 AICSemi"
#define DRV_AUTHOR       "AICSemi"
#define DRV_VERS_MOD "1.0"

int testmode = FW_NORMAL_MODE;
int adap_test = 0;
char paringid[100];
int n_para = 1;
int ble_scan_wakeup_reboot_time = 1000;
int aicwf_dbg_level = LOGERROR|LOGINFO|LOGDEBUG|LOGTRACE;

module_param(aicwf_dbg_level, int, 0660);
module_param(ble_scan_wakeup_reboot_time, int, 0660);
module_param(testmode, int, 0660);
module_param(adap_test, int, 0660);
module_param_string(paringid, paringid, 100, 0660);


static void aicsmac_driver_register(void)
{
    aicwf_usb_register();
}

static int __init aic_bluetooth_mod_init(void)
{
    printk("%s \n", __func__);
    printk("RELEASE DATE:%s \r\n", RELEASE_DATE);
#ifdef CONFIG_PREALLOC_RX_SKB
    aicwf_prealloc_init();
#endif

    aicsmac_driver_register();
    return 0;
}

static void __exit aic_bluetooth_mod_exit(void)
{
    printk("%s\n", __func__);
    aicwf_usb_exit();
    
#ifdef CONFIG_PREALLOC_RX_SKB
    aicwf_prealloc_exit();
#endif

#ifdef CONFIG_PREALLOC_TXQ
    aicwf_prealloc_txq_free();
#endif
}


module_init(aic_bluetooth_mod_init);
module_exit(aic_bluetooth_mod_exit);

MODULE_FIRMWARE(DRV_CONFIG_FW_NAME);
MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_VERSION(DRV_VERS_MOD);
MODULE_AUTHOR(DRV_COPYRIGHT " " DRV_AUTHOR);
MODULE_LICENSE("GPL");


/* SPDX-License-Identifier: GPL-2.0 */
/* rk_wifi_config.c
 *
 * RKWIFI driver version.
 *
 * Define the firmware and nvram path
 *
 * Define default Country Code
 *
 * gwl @ Rockchip
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/rfkill-wlan.h>

/* 
 * Set Firmware Path
 */
 
#define VENDOR_ETC_FIRMWARE "/vendor/etc/firmware/"

int rkwifi_set_firmware(char *fw, char *nvram)
{

	sprintf(fw, "%s%s", VENDOR_ETC_FIRMWARE, "fw_bcmdhd.bin");
	sprintf(nvram, "%s%s", VENDOR_ETC_FIRMWARE, "nvram.txt");
	return 0;
}
EXPORT_SYMBOL(rkwifi_set_firmware);
MODULE_LICENSE("GPL");

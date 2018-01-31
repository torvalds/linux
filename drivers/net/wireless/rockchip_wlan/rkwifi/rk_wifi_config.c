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
#define SYSTEM_ETC_FIRMWARE "/system/etc/firmware/"
char ANDROID_FW_PATH[64] = {0};

extern int get_wifi_chip_type(void);
int rkwifi_set_firmware(char *fw, char *nvram)
{
    int chip = get_wifi_chip_type();
    struct file *filp = NULL;

    filp = filp_open(VENDOR_ETC_FIRMWARE, O_RDONLY, 0);

    if (!IS_ERR(filp)) {
	strcpy(ANDROID_FW_PATH, VENDOR_ETC_FIRMWARE);
    } else {
	strcpy(ANDROID_FW_PATH, SYSTEM_ETC_FIRMWARE);
    }

if (chip == WIFI_RK903) {
	sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_RK903b2.bin");
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_RK903_26M.cal");
}

if (chip == WIFI_RK901) {
	sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_RK901.bin");
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_RK901.txt");
}

#ifdef CONFIG_BCM4330
	sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_bcm4330.bin");
#ifdef CONFIG_RK_CHECK_UACCESS
    sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_4330_oob.txt");
#else
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_4330.txt");
#endif
#endif

if (chip == WIFI_AP6181) {
    sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_RK901.bin");
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_AP6181.txt");
}

if (chip == WIFI_AP6210) {
    sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_RK901.bin");
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_AP6210.txt");
}

if (chip == WIFI_AP6212) {
    sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_bcm43438a0.bin");
        sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_ap6212.txt");
}

if (chip == WIFI_AP6234) {
    sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_bcm43341b0_ag.bin");
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_AP6234.txt");
}

if (chip == WIFI_AP6255) {
    sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_bcm43455c0_ag.bin");
    sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_ap6255.txt");
}
if (chip == WIFI_AP6441) {
    sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_bcm43341b0_ag.bin");
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_AP6441.txt");
}

if (chip == WIFI_AP6335) {
    sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_bcm4339a0_ag.bin");
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_AP6335.txt");
}

if (chip == WIFI_AP6354) {
    sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_bcm4354a1_ag.bin");
        sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_ap6354.txt");
}

if (chip == WIFI_AP6476) {
    sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_RK901.bin");
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_AP6476.txt");
}

if (chip == WIFI_AP6493) {
    sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_RK903.bin");
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_AP6493.txt");
}

if (chip == WIFI_AP6330) {
    sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_RK903_ag.bin");
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_AP6330.txt");
}

#ifdef CONFIG_GB86302I
    sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_RK903_ag.bin");
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_GB86302I.txt");
#endif
	return 0;
}

EXPORT_SYMBOL(rkwifi_set_firmware);

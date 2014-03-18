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

/* 
 * Set Firmware Path
 */
 
#define ANDROID_FW_PATH "/system/etc/firmware/"

int rkwifi_set_firmware(char *fw, char *nvram)
{
#ifdef CONFIG_RK903
	sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_RK903b2.bin");
#ifdef CONFIG_RKWIFI_26M
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_RK903_26M.cal");
#endif
#ifdef CONFIG_RKWIFI_37_4M	
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_RK903.cal");
#endif	
#endif	

#ifdef CONFIG_RK901
	sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_RK901.bin");
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_RK901.txt");
#endif

#ifdef CONFIG_BCM4330
	sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_bcm4330.bin");
#ifdef CONFIG_RK_CHECK_UACCESS
    sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_4330_oob.txt");
#else
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_4330.txt");
#endif
#endif

#ifdef CONFIG_AP6181
    sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_RK901.bin");
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_AP6181.txt");
#endif

#ifdef CONFIG_AP6210
    sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_RK901.bin");
#ifdef CONFIG_RKWIFI_26M
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_AP6210.txt");
#endif
#ifdef CONFIG_RKWIFI_24M
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_AP6210_24M.txt");
#endif
#endif

#ifdef CONFIG_AP6234
    sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_bcm43341b0_ag.bin");
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_AP6234.txt");
#endif

#ifdef CONFIG_AP6441
    sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_bcm43341b0_ag.bin");
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_AP6441.txt");
#endif

#ifdef CONFIG_AP6335
    sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_bcm4339a0_ag.bin");
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_AP6335.txt");
#endif

#ifdef CONFIG_AP6476
    sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_RK901.bin");
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_AP6476.txt");
#endif

#ifdef CONFIG_AP6493
    sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_RK903.bin");
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_AP6493.txt");
#endif

#ifdef CONFIG_AP6330
    sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_RK903_ag.bin");
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_AP6330.txt");
#endif

#ifdef CONFIG_GB86302I
    sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_RK903_ag.bin");
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_GB86302I.txt");
#endif
	return 0;
}

EXPORT_SYMBOL(rkwifi_set_firmware);

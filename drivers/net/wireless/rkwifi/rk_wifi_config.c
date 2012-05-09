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

/* Set INIT_COUNTRY_CODE 
 * "US" ---> 11 channels, this is default setting. 
 * "EU" ---> 13 channels
 * "JP" ---> 14 channels
 */

int rkwifi_set_country_code(char *code)
{
	sprintf(code, "%s", "EU");
	return 0;
}

/* 
 * Set Firmware Path
 */
 
#define ANDROID_FW_PATH "/system/etc/firmware/"

int rkwifi_set_firmware(char *fw, char *nvram)
{
#ifdef CONFIG_RK903
	sprintf(fw, "%s%s", ANDROID_FW_PATH, "fw_RK903.bin");
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
	sprintf(nvram, "%s%s", ANDROID_FW_PATH, "nvram_4330.txt");
#endif

	return 0;
}

EXPORT_SYMBOL(rkwifi_set_country_code);
EXPORT_SYMBOL(rkwifi_set_firmware);

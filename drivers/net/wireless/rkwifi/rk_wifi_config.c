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


const char RKWIFI_DRV_VERSION[] = "4.02";

/* Set INIT_COUNTRY_CODE 
 * "US" ---> 11 channels, this is default setting. 
 * "EU" ---> 13 channels
 * "JP" ---> 14 channels
 */
const char INIT_COUNTRY_CODE[] = "EU";

#ifdef CONFIG_RK903
const char WIFI_MODULE_NAME[] = "RK903";
const char CONFIG_BCMDHD_FW_PATH[] = "/system/etc/firmware/fw_RK903.bin";
const char CONFIG_BCMDHD_NVRAM_PATH[] = "/system/etc/firmware/nvram_RK903.cal";
#endif

#ifdef CONFIG_RK901
const char WIFI_MODULE_NAME[] = "RK901";
const char CONFIG_BCMDHD_FW_PATH[] = "/system/etc/firmware/fw_RK901.bin";
const char CONFIG_BCMDHD_NVRAM_PATH[] = "/system/etc/firmware/nvram_RK901.txt";
#endif

#ifdef CONFIG_BCM4330
const char WIFI_MODULE_NAME[] = "BCM4330";
const char CONFIG_BCMDHD_FW_PATH[] = "/system/etc/firmware/fw_bcm4330.bin";
const char CONFIG_BCMDHD_NVRAM_PATH[] = "/system/etc/firmware/nvram_4330.txt";
#endif

#ifdef CONFIG_BCM4329
const char WIFI_MODULE_NAME[] = "BCM4329";
const char CONFIG_BCMDHD_FW_PATH[] = "/system/etc/firmware/fw_bcm4329.bin";
const char CONFIG_BCMDHD_NVRAM_PATH[] = "/system/etc/firmware/nvram_B23.txt";
#endif

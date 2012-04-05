#ifndef RK_WIFI_CONFIG_H
#define RK_WIFI_CONFIG_H

/*
 * Broadcom BCM4330 driver version.
 */
#define BCMDHD_DRV_VERSION "4.01"

/* Set INIT_COUNTRY_CODE 
 * "US" ---> 11 channels, this is default setting. 
 * "EU" ---> 13 channels
 * "JP" ---> 14 channels
 */
#define INIT_COUNTRY_CODE "EU"

#ifdef CONFIG_RK903
#define CONFIG_BCMDHD_FW_PATH  "/system/etc/firmware/fw_RK903.bin"
#define CONFIG_BCMDHD_NVRAM_PATH  "/system/etc/firmware/nvram_RK903.txt"
#endif

#ifdef CONFIG_RK901
#define CONFIG_BCMDHD_FW_PATH  "/system/etc/firmware/fw_RK901.bin"
#define CONFIG_BCMDHD_NVRAM_PATH  "/system/etc/firmware/nvram_RK901.txt"
#endif

#ifdef CONFIG_BCM4330
#define CONFIG_BCMDHD_FW_PATH  "/system/etc/firmware/fw_bcm4330.bin"
#define CONFIG_BCMDHD_NVRAM_PATH  "/system/etc/firmware/nvram_4330.txt"
#endif

#ifdef CONFIG_BCM4329
#define CONFIG_BCMDHD_FW_PATH  "/system/etc/firmware/fw_bcm4329.bin"
#define CONFIG_BCMDHD_NVRAM_PATH  "/system/etc/firmware/nvram_B23.txt"
#endif

#endif /* RK_WIFI_CONFIG_H */

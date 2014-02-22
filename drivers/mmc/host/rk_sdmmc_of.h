#ifndef __RK_SDMMC_OF_H
#define __RK_SDMMC_OF_H

#include <dt-bindings/mmc/rockchip-sdmmc.h>
#include <linux/of_gpio.h>
#include <linux/of_i2c.h>
#include <linux/types.h>
#include <linux/bitops.h>


#define DRIVER_NAME "rk_sdmmc"
#define DRIVER_PREFIX DRIVER_NAME ": "

enum MMC_DBG_MASK{
     MMC_DBG_NONE = 0,
     MMC_DBG_BOOT = BIT(0),
     MMC_DBG_INFO = BIT(1),
     MMC_DBG_ERROR= BIT(2), 
     MMC_DBG_WARN = BIT(3),
     MMC_DBG_CMD  = BIT(4),
     MMC_DBG_ALL  = ~0,
     
};

extern u32 mmc_debug_level ;//= MMC_DBG_BOOT | MMC_DBG_ERROR;/* | MMC_DBG_CMD | ...*/

#define mmc_error(fmt, arg...) \
        pr_err(DRIVER_PREFIX "ERROR " fmt "\n", ##arg)
#define mmc_warning(fmt, arg...) \
        pr_warning(DRIVER_PREFIX "WARNING " fmt "\n", ##arg)
    
#define mmc_cmd(fmt, arg...) \
        pr_info(DRIVER_PREFIX "CMD" fmt "\n", ##arg)
    
#define mmc_info(fmt, arg...) \
        pr_info(DRIVER_PREFIX fmt "\n", ##arg)

#if defined(CONFIG_DYNAMIC_DEBUG)
    #define mmc_debug(level, fmt, arg...) \
    do { \
        if (unlikely(level & mmc_debug_level)) \
            dynamic_pr_debug(DRIVER_PREFIX fmt "\n", ##arg); \
    } while (0)
#else
    #define mmc_debug(level, fmt, arg...) \
    do { \
        if (unlikely(level & mmc_debug_level)) \
            printk(KERN_DEBUG pr_fmt(DRIVER_PREFIX fmt "\n"), \
                   ##arg); \
    } while (0)
#endif

struct rk_sdmmc_of
{
    u32 mmc_caps;            
    u32 mmc_int_type;
    u32 mmc_ocr;
    u32 mmc_dma_is_used[2]; /*Bit 1: use dma or not ; Bit 2:general dma or idma*/
    u32 emmc_is_selected;
    u32 mmc_dma_chn;
    const char *mmc_dma_name;
};

#endif

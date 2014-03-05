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
     MMC_DBG_ERROR= BIT(1), 
     MMC_DBG_WARN = BIT(2),
     MMC_DBG_INFO = BIT(3),
     MMC_DBG_CMD  = BIT(4),
     MMC_DBG_DBG  = BIT(5),
     MMC_DBG_ALL  = ~0,
     
};
//extern u32 mmc_debug_level = MMC_DBG_ALL;

#define MMC_DBG_BOOT_FUNC(fmt, arg...) \
        if(mmc_debug_level >= MMC_DBG_BOOT){ printk(DRIVER_PREFIX "BOOT " fmt "\n", ##arg);}
#define MMC_DBG_ERR_FUNC(fmt, arg...) \
        if(mmc_debug_level >= MMC_DBG_ERROR){ printk(DRIVER_PREFIX "ERROR " fmt "\n", ##arg);}
#define MMC_DBG_WARN_FUNC(fmt, arg...) \
        if(mmc_debug_level >= MMC_DBG_WARN){ printk(DRIVER_PREFIX "WARNING " fmt "\n", ##arg);}        
#define MMC_DBG_INFO_FUNC(fmt, arg...) \
        if(mmc_debug_level >= MMC_DBG_INFO){ printk(DRIVER_PREFIX fmt "\n", ##arg);}          
#define MMC_DBG_CMD_FUNC(fmt, arg...) \
        if(mmc_debug_level >= MMC_DBG_CMD){ printk(DRIVER_PREFIX "CMD" fmt "\n", ##arg);}
    


#if defined(CONFIG_DYNAMIC_DEBUG)
    #define MMC_DBG_DEBUG_FUNC(level, fmt, arg...) \
    do { \
        if (unlikely(level & mmc_debug_level)) \
            dynamic_pr_debug(DRIVER_PREFIX fmt "\n", ##arg); \
    } while (0)
#else
    #define MMC_DBG_DEBUG_FUNC(level, fmt, arg...) \
    do { \
        if (unlikely(level & mmc_debug_level)) \
            if(mmc_debug_level >= MMC_DBG_DBG){ printk(DRIVER_PREFIX fmt "\n"), ##arg);} \
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

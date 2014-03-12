/*
 * Synopsys DesignWare Multimedia Card Interface driver
 *
 * Copyright (C) 2014 Fuzhou Rockchip Electronics Co.Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#include "rk_sdmmc_of.h"

u32 mmc_debug_level;

static void rockchip_mmc_of_dump(struct rk_sdmmc_of *rk_mmc_property)
{
    mmc_debug_level = MMC_DBG_ERROR;//MMC_DBG_ALL;//set the value refer to file rk_sdmmc_of.h
    
    MMC_DBG_BOOT_FUNC("=========rockchip mmc dts dump info start== 2014-03-12 14:23 ======");
 /*   
    MMC_DBG_BOOT_FUNC("mmc,caps: 0x%x",rk_mmc_property->mmc_caps);
    MMC_DBG_BOOT_FUNC("mmc,ocr:  0x%x",rk_mmc_property->mmc_ocr);
    MMC_DBG_BOOT_FUNC("mmc,int:  0x%x",rk_mmc_property->mmc_int_type);
    MMC_DBG_BOOT_FUNC("mmc,emmc_is_selected: 0x%x",rk_mmc_property->emmc_is_selected);
    MMC_DBG_BOOT_FUNC("mmc,use_dma:  %d %d",rk_mmc_property->mmc_dma_is_used[0],
                                                   rk_mmc_property->mmc_dma_is_used[1]);
    MMC_DBG_BOOT_FUNC("mmc,dma_ch: %d",rk_mmc_property->mmc_dma_chn);
    MMC_DBG_BOOT_FUNC("=========rockchip mmc dts dump info end================");
  */  
}


void rockchip_mmc_of_probe(struct device_node *np,struct rk_sdmmc_of *rk_mmc_property)
{
    of_property_read_u32(np, "mmc,caps", &rk_mmc_property->mmc_caps);
    of_property_read_u32(np, "mmc,ocr", &rk_mmc_property->mmc_ocr);
    of_property_read_u32(np, "mmc,int", &rk_mmc_property->mmc_int_type);
    of_property_read_u32(np, "mmc,emmc_is_selected", &rk_mmc_property->emmc_is_selected);
    of_property_read_u32_array(np, "mmc,use_dma", rk_mmc_property->mmc_dma_is_used,2);
    if((&rk_mmc_property->mmc_dma_is_used[0] == MMC_USE_DMA))
    {   
           if(rk_mmc_property->mmc_dma_is_used[1] == 0)
                rk_mmc_property->mmc_dma_name = "dma_mmc0";
           else if(rk_mmc_property->mmc_dma_is_used[1] == 1)
                rk_mmc_property->mmc_dma_name = "dma_mmc1";
           else 
                rk_mmc_property->mmc_dma_name = "dma_mmc2";

           //of_property_read_string(np, "mmc,dma_name", &(rk_mmc_property->mmc_dma_name));    
           of_property_read_u32(np, "mmc,dma_ch", &rk_mmc_property->mmc_dma_chn);
        
    }else{
        MMC_DBG_WARN_FUNC("Device Tree configure mmc drivers to use pio!\n");
    }
    rockchip_mmc_of_dump(rk_mmc_property);
    return ;

}
EXPORT_SYSMBOL(rockchip_mmc_of_probe);



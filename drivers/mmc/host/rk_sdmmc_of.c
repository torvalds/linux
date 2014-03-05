

#include "rk_sdmmc_of.h"

u32 mmc_debug_level;

static void rockchip_mmc_of_dump(struct rk_sdmmc_of *rk_mmc_property)
{

printk("%d..%s:  ===test ==\n", __LINE__, __FUNCTION__);
    MMC_DBG_BOOT_FUNC("=========rockchip mmc dts dump info start== 2014-03-05 16:08 ======\n");
    MMC_DBG_BOOT_FUNC("mmc,caps: 0x%x\n",rk_mmc_property->mmc_caps);
    MMC_DBG_BOOT_FUNC("mmc,ocr:  0x%x\n",rk_mmc_property->mmc_ocr);
    MMC_DBG_BOOT_FUNC("mmc,int:  0x%x\n",rk_mmc_property->mmc_int_type);
    MMC_DBG_BOOT_FUNC("mmc,emmc_is_selected: 0x%x\n",rk_mmc_property->emmc_is_selected);
    MMC_DBG_BOOT_FUNC("mmc,use_dma:  %d %d\n",rk_mmc_property->mmc_dma_is_used[0],
                                                   rk_mmc_property->mmc_dma_is_used[1]);
    MMC_DBG_BOOT_FUNC("mmc,dma_ch: %d\n",rk_mmc_property->mmc_dma_chn);
    MMC_DBG_BOOT_FUNC("=========rockchip mmc dts dump info end================\n");
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



#include "rk_sdmmc_of.h"

static void rockchip_mmc_of_dump(struct rk_sdmmc_of *rk_mmc_property)
{
printk("%d..%s: =====test====\n", __LINE__, __FUNCTION__);
//dump_stack();
/*
    mmc_debug(MMC_DBG_BOOT,"=========rockchip mmc dts dump info start==============\n");
    mmc_debug(MMC_DBG_BOOT,"mmc,caps: 0x%x\n",rk_mmc_property->mmc_caps);
    mmc_debug(MMC_DBG_BOOT,"mmc,ocr:  0x%x\n",rk_mmc_property->mmc_ocr);
    mmc_debug(MMC_DBG_BOOT,"mmc,int:  0x%x\n",rk_mmc_property->mmc_int_type);
    mmc_debug(MMC_DBG_BOOT,"mmc,emmc_is_selected: 0x%x\n",rk_mmc_property->emmc_is_selected);
    mmc_debug(MMC_DBG_BOOT,"mmc,use_dma:  %d %d\n",rk_mmc_property->mmc_dma_is_used[0],
                                                   rk_mmc_property->mmc_dma_is_used[1]);
    mmc_debug(MMC_DBG_BOOT,"mmc,dma_ch: %d\n",rk_mmc_property->mmc_dma_chn);
    mmc_debug(MMC_DBG_BOOT,"=========rockchip mmc dts dump info end================\n");
    */
      printk("=========rockchip mmc dts dump info start==============\n");
    printk("mmc,caps: 0x%x\n",rk_mmc_property->mmc_caps);
    printk("mmc,ocr:  0x%x\n",rk_mmc_property->mmc_ocr);
    printk("mmc,int:  0x%x\n",rk_mmc_property->mmc_int_type);
    printk("mmc,emmc_is_selected: 0x%x\n",rk_mmc_property->emmc_is_selected);
    printk("mmc,use_dma:  %d %d\n",rk_mmc_property->mmc_dma_is_used[0],
                                                   rk_mmc_property->mmc_dma_is_used[1]);
    printk("mmc,dma_ch: %d\n",rk_mmc_property->mmc_dma_chn);
    printk("=========rockchip mmc dts dump info end================\n");  
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
        mmc_debug(MMC_DBG_WARN,"Device Tree configure mmc drivers to use pio!\n");
    }
    rockchip_mmc_of_dump(rk_mmc_property);
    return ;

}
EXPORT_SYSMBOL(rockchip_mmc_of_probe);



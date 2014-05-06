#include "camsys_soc_priv.h"
#include "camsys_mipicsi_phy.h"

static int camsys_mipiphy_clkin_cb(void *ptr, unsigned int on)
{
    camsys_mipiphy_clk_t *clk;
    camsys_dev_t *camsys_dev = (camsys_dev_t*)ptr;
    unsigned int i,phycnt;
    
    if (camsys_dev->mipiphy != NULL) {
        phycnt = camsys_dev->mipiphy[0].phycnt;
         
        for (i=0; i<phycnt; i++) {
            if (camsys_dev->mipiphy[i].clk != NULL) {
                clk = (camsys_mipiphy_clk_t*)camsys_dev->mipiphy[i].clk;
                if (on && !clk->on) {
                    if (!IS_ERR_OR_NULL(clk->hclk))
                        clk_prepare_enable(clk->hclk);  
                    clk->on = on;
                } else if (!on && clk->on) {
                    if (!IS_ERR_OR_NULL(clk->hclk))
                        clk_disable_unprepare(clk->hclk);                    
                    clk->on = on;
                }
            }
        }
    }
    if (on)
        camsys_trace(1, "%s mipi phy clk in turn on",dev_name(camsys_dev->miscdev.this_device));
    else 
        camsys_trace(1, "%s mipi phy clk in turn off",dev_name(camsys_dev->miscdev.this_device));
    
    return 0;
}

static int camsys_mipiphy_ops (void * ptr, camsys_mipiphy_t *phy)
{
    camsys_dev_t *camsys_dev = (camsys_dev_t*)ptr;
    camsys_mipiphy_soc_para_t para;
    camsys_soc_priv_t *soc;
    
    if (camsys_dev->soc) {
        soc = (camsys_soc_priv_t*)camsys_dev->soc;
        if (soc->soc_cfg) { 
            para.camsys_dev = camsys_dev;
            para.phy = phy;
            (soc->soc_cfg)(Mipi_Phy_Cfg,(void*)&para);
        } else {
            camsys_err("camsys_dev->soc->soc_cfg is NULL!");
        }
    } else {
        camsys_err("camsys_dev->soc is NULL!");
    }
    
    return 0;
}

static int camsys_mipiphy_remove_cb(struct platform_device *pdev)
{
    camsys_dev_t *camsys_dev = platform_get_drvdata(pdev);
    camsys_mipiphy_clk_t *phyclk;
    unsigned int i;
    
    if (camsys_dev->mipiphy != NULL) {
        for (i=0; i<camsys_dev->mipiphy[0].phycnt; i++) {
            if (camsys_dev->mipiphy[i].reg != NULL) {
                if (camsys_dev->mipiphy[i].reg->vir_base != 0) {
                    iounmap((void __iomem *)camsys_dev->mipiphy[i].reg->vir_base);
                    camsys_dev->mipiphy[i].reg->vir_base = 0;
                }
                kfree(camsys_dev->mipiphy[i].reg);
                camsys_dev->mipiphy[i].reg = NULL;
            }

            if (camsys_dev->mipiphy[i].clk != NULL) {
                phyclk = (camsys_mipiphy_clk_t*)camsys_dev->mipiphy[i].clk;
                devm_clk_put(&pdev->dev,phyclk->hclk);

                kfree(camsys_dev->mipiphy[i].clk);
                camsys_dev->mipiphy[i].clk = NULL;
            }
        }
    }

    
    return 0;
}
int camsys_mipiphy_probe_cb(struct platform_device *pdev, camsys_dev_t *camsys_dev)
{
    struct device *dev = &pdev->dev;
    camsys_meminfo_t *meminfo;
    camsys_phyinfo_t *mipiphy;
    unsigned int mipiphy_cnt,phyreg[2];
    char str[31];
    struct clk* clk;
    camsys_mipiphy_clk_t *phyclk;
    int err,i;

    err = of_property_read_u32(dev->of_node,"rockchip,isp,mipiphy",&mipiphy_cnt);
    if (err < 0) {
        camsys_err("get property(rockchip,isp,mipiphy) failed!");
        goto fail;
    } else {
        camsys_trace(2, "%s have %d mipi phy\n",dev_name(&pdev->dev),mipiphy_cnt);
    }
    
    mipiphy = kzalloc(sizeof(camsys_phyinfo_t)*mipiphy_cnt,GFP_KERNEL);
    if (mipiphy == NULL) {
        err = -ENOMEM;
        camsys_err("malloc camsys_phyinfo_t failed!");
        goto fail;
    }

    camsys_dev->mipiphy = mipiphy;

    memset(str,0x00,sizeof(str));
    for (i=0; i<mipiphy_cnt; i++) {
        meminfo = NULL;
        sprintf(str,"rockchip,isp,mipiphy%d,reg",i);
        if (of_property_read_u32_array(dev->of_node,str,phyreg,2) == 0) {
            meminfo = kzalloc(sizeof(camsys_meminfo_t),GFP_KERNEL);
            if (meminfo == NULL) {                
                camsys_err("malloc camsys_meminfo_t for mipiphy%d failed!",i);                
            } else {
                meminfo->vir_base = (unsigned int)ioremap(phyreg[0],phyreg[1]);
                if (!meminfo->vir_base){
                    camsys_err("%s ioremap %s failed",dev_name(&pdev->dev), str);                    
                } else {
                    strlcpy(meminfo->name, CAMSYS_MIPIPHY_MEM_NAME,sizeof(meminfo->name));
                    meminfo->phy_base = phyreg[0];
                    meminfo->size = phyreg[1];
                }
               
                camsys_dev->mipiphy[i].reg = meminfo;
            }
        }

        memset(str,sizeof(str),0x00);
        sprintf(str,"hclk_mipiphy%d",i);

        clk = devm_clk_get(&pdev->dev, str);
        if (!IS_ERR_OR_NULL(clk)) {
            phyclk = kzalloc(sizeof(camsys_mipiphy_clk_t),GFP_KERNEL);
            if (phyclk == NULL) {
                camsys_err("malloc camsys_mipiphy_clk_t for %s failed!",str);
            } else {
                phyclk->hclk = clk;
            }

            camsys_dev->mipiphy[i].clk = (void*)phyclk;
        }

        camsys_dev->mipiphy[i].phycnt = mipiphy_cnt;
        camsys_dev->mipiphy[i].clkin_cb = camsys_mipiphy_clkin_cb;
        camsys_dev->mipiphy[i].ops = camsys_mipiphy_ops;
        camsys_dev->mipiphy[i].remove = camsys_mipiphy_remove_cb;

        if (meminfo != NULL) {
            camsys_trace(1,"%s mipi phy%d probe success(reg_phy: 0x%x  reg_vir: 0x%x  size: 0x%x)",
                dev_name(&pdev->dev), i,meminfo->phy_base,meminfo->vir_base, meminfo->size);
        } else {
            camsys_trace(1,"%s mipi phy%d probe success(reg_phy: 0x%x  reg_vir: 0x%x  size: 0x%x)",
                dev_name(&pdev->dev), i,0,0,0);
        }

    }   
    
    return 0;

fail:

    return err;
}


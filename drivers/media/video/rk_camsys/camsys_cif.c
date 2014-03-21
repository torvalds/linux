#include "camsys_cif.h"

static const char miscdev_cif0_name[] = CAMSYS_CIF0_DEVNAME;
static const char miscdev_cif1_name[] = CAMSYS_CIF1_DEVNAME;

static int camsys_cif_iomux_cb(camsys_extdev_t *extdev,void *ptr)
{
    unsigned int cif_index;
    camsys_dev_t *camsys_dev = (camsys_dev_t*)ptr;

    if (strcmp(dev_name(camsys_dev->miscdev.this_device), CAMSYS_CIF1_DEVNAME)==0) {
        cif_index = 1;
    } else {
        cif_index = 0;
    }
    
#if defined(CONFIG_ARCH_RK3066B) || defined(CONFIG_ARCH_RK3188)
    switch(cif_index){
        case 0:
        {
		    iomux_set(CIF0_CLKOUT);
            write_grf_reg(GRF_IO_CON3, (CIF_DRIVER_STRENGTH_MASK|CIF_DRIVER_STRENGTH_8MA));
            write_grf_reg(GRF_IO_CON4, (CIF_CLKOUT_AMP_MASK|CIF_CLKOUT_AMP_1V8));            
            break;
        }
        default:
            camsys_err("Cif index(%d) is invalidate!!!\n",cif_index);
            break;
    }
#elif defined(CONFIG_ARCH_RK30)
    switch(cif_index){
        case 0:
        {
            rk30_mux_api_set(GPIO1B3_CIF0CLKOUT_NAME, GPIO1B_CIF0_CLKOUT);            
            break;
        }
        case 1:
        {
            rk30_mux_api_set(GPIO1C0_CIF1DATA2_RMIICLKOUT_RMIICLKIN_NAME,GPIO1C_CIF1_DATA2);
            rk30_mux_api_set(GPIO1C1_CIFDATA3_RMIITXEN_NAME,GPIO1C_CIF_DATA3);
            rk30_mux_api_set(GPIO1C2_CIF1DATA4_RMIITXD1_NAME,GPIO1C_CIF1_DATA4);
            rk30_mux_api_set(GPIO1C3_CIFDATA5_RMIITXD0_NAME,GPIO1C_CIF_DATA5);
            rk30_mux_api_set(GPIO1C4_CIFDATA6_RMIIRXERR_NAME,GPIO1C_CIF_DATA6);
            rk30_mux_api_set(GPIO1C5_CIFDATA7_RMIICRSDVALID_NAME,GPIO1C_CIF_DATA7);
            rk30_mux_api_set(GPIO1C6_CIFDATA8_RMIIRXD1_NAME,GPIO1C_CIF_DATA8);
            rk30_mux_api_set(GPIO1C7_CIFDATA9_RMIIRXD0_NAME,GPIO1C_CIF_DATA9);
            
            rk30_mux_api_set(GPIO1D0_CIF1VSYNC_MIIMD_NAME,GPIO1D_CIF1_VSYNC);
            rk30_mux_api_set(GPIO1D1_CIF1HREF_MIIMDCLK_NAME,GPIO1D_CIF1_HREF);
            rk30_mux_api_set(GPIO1D2_CIF1CLKIN_NAME,GPIO1D_CIF1_CLKIN);
            rk30_mux_api_set(GPIO1D3_CIF1DATA0_NAME,GPIO1D_CIF1_DATA0);
            rk30_mux_api_set(GPIO1D4_CIF1DATA1_NAME,GPIO1D_CIF1_DATA1);
            rk30_mux_api_set(GPIO1D5_CIF1DATA10_NAME,GPIO1D_CIF1_DATA10);
            rk30_mux_api_set(GPIO1D6_CIF1DATA11_NAME,GPIO1D_CIF1_DATA11);
            rk30_mux_api_set(GPIO1D7_CIF1CLKOUT_NAME,GPIO1D_CIF1_CLKOUT);
            break;
        }
        default:
            camsys_err("Cif index(%d) is invalidate!!!\n", cif_index);
            break;
        }
#elif defined(CONFIG_ARCH_RK319X)
    switch(cif_index){
        case 0:
        {
            unsigned int cif_vol_sel;
            //set cif vol domain
            cif_vol_sel = __raw_readl(RK30_GRF_BASE+0x018c);
        	__raw_writel( (cif_vol_sel |0x20002),RK30_GRF_BASE+0x018c);
            //set driver strength
        	__raw_writel(0xffffffff, RK30_GRF_BASE+0x01dc);
        	
            iomux_set(CIF0_CLKOUT);
            iomux_set(CIF0_CLKIN);
            iomux_set(CIF0_HREF);
            iomux_set(CIF0_VSYNC);
            iomux_set(CIF0_D0);
            iomux_set(CIF0_D1);
            iomux_set(CIF0_D2);
            iomux_set(CIF0_D3);
            iomux_set(CIF0_D4);
            iomux_set(CIF0_D5);
            iomux_set(CIF0_D6);
            iomux_set(CIF0_D7);
            iomux_set(CIF0_D8);
            iomux_set(CIF0_D9);
            camsys_trace(1, "%s cif iomux success\n",dev_name(camsys_dev->miscdev.this_device));
            break;
        }
        case 1:
        default:
            camsys_err("Cif index(%d) is invalidate!!!\n", cif_index);
            break;
        }
#endif
                
    return 0;
}
static int camsys_cif_clkin_cb(void *ptr, unsigned int on)
{
    camsys_dev_t *camsys_dev = (camsys_dev_t*)ptr;
    camsys_cif_clk_t *clk = (camsys_cif_clk_t*)camsys_dev->clk;
    
    spin_lock(&clk->lock);
    if (on && !clk->in_on) {        
        clk_enable(clk->pd_cif);
        clk_enable(clk->aclk_cif);
    	clk_enable(clk->hclk_cif);
    	clk_enable(clk->cif_clk_in);
    	
        clk->in_on = true;
        camsys_trace(1, "%s clock in turn on",dev_name(camsys_dev->miscdev.this_device));
    } else if (!on && clk->in_on) {
        clk_disable(clk->aclk_cif);
    	clk_disable(clk->hclk_cif);
    	clk_disable(clk->cif_clk_in);    	
    	clk_disable(clk->pd_cif);
        clk->in_on = false;
        camsys_trace(1, "%s clock in turn off",dev_name(camsys_dev->miscdev.this_device));
    }
    spin_unlock(&clk->lock);
    return 0;
}

static int camsys_cif_clkout_cb(void *ptr, unsigned int on)
{
    camsys_dev_t *camsys_dev = (camsys_dev_t*)ptr;
    camsys_cif_clk_t *clk = (camsys_cif_clk_t*)camsys_dev->clk;
    struct clk *cif_clk_out_div;
    
    
    spin_lock(&clk->lock);
    if (on && (clk->out_on != on)) {        
        clk_enable(clk->cif_clk_out);
        clk_set_rate(clk->cif_clk_out,on);
    	
        clk->out_on = on;
        camsys_trace(1, "%s clock out(rate: %dHz) turn on",dev_name(camsys_dev->miscdev.this_device),
                    clk->out_on);
    } else if (!on && clk->out_on) {
        if (strcmp(dev_name(camsys_dev->miscdev.this_device),miscdev_cif1_name)==0) {
            cif_clk_out_div =  clk_get(NULL, "cif1_out_div");
        } else{
            cif_clk_out_div =  clk_get(NULL, "cif0_out_div");
            if(IS_ERR_OR_NULL(cif_clk_out_div)) {
                cif_clk_out_div =  clk_get(NULL, "cif_out_div");
            }
        }

        if(!IS_ERR_OR_NULL(cif_clk_out_div)) {
            clk_set_parent(clk->cif_clk_out, cif_clk_out_div);
            clk_put(cif_clk_out_div);
        } else {
            camsys_warn("%s clock out may be not off!", dev_name(camsys_dev->miscdev.this_device));
        }
        clk_disable(clk->cif_clk_out);
        clk->out_on = 0;

        camsys_trace(1, "%s clock out turn off",dev_name(camsys_dev->miscdev.this_device));
    }
    spin_unlock(&clk->lock);

    {
  //  __raw_writel(0x00, CRU_PCLK_REG30+RK30_CRU_BASE);
    }

    return 0;
}

static irqreturn_t camsys_cif_irq(int irq, void *data)
{
    camsys_dev_t *camsys_dev = (camsys_dev_t*)data;
    camsys_irqstas_t *irqsta;
    camsys_irqpool_t *irqpool;
    unsigned int intsta,frmsta;

    intsta = __raw_readl(camsys_dev->devmems.registermem->vir_base + CIF_INITSTA);
    frmsta = __raw_readl(camsys_dev->devmems.registermem->vir_base + CIF_FRAME_STATUS);
   printk("get oneframe,intsta = 0x%x \n",intsta);
 
    if (intsta & 0x200) {
        __raw_writel(0x200,camsys_dev->devmems.registermem->vir_base + CIF_INITSTA);
        __raw_writel(0xf000,camsys_dev->devmems.registermem->vir_base + CIF_CTRL);
    }

    if (intsta &0x01) {
        __raw_writel(0x01,camsys_dev->devmems.registermem->vir_base + CIF_INITSTA);
        __raw_writel(0x02,camsys_dev->devmems.registermem->vir_base + CIF_FRAME_STATUS);
        __raw_writel(0xf001,camsys_dev->devmems.registermem->vir_base + CIF_CTRL);
    }    
    
    spin_lock(&camsys_dev->irq.lock);
    list_for_each_entry(irqpool, &camsys_dev->irq.irq_pool, list) {
        spin_lock(&irqpool->lock);
        if (!list_empty(&irqpool->deactive)) {
            irqsta = list_first_entry(&irqpool->deactive, camsys_irqstas_t, list);
            irqsta->sta.mis = intsta;
            irqsta->sta.ris = intsta;
            list_del_init(&irqsta->list);            
            list_add_tail(&irqsta->list,&irqpool->active);
            irqsta = list_first_entry(&irqpool->active, camsys_irqstas_t, list);
            //wake_up_all(&camsys_dev->irq.irq_done);
            wake_up(&irqpool->done);
        }
        spin_unlock(&irqpool->lock);
    }
    spin_unlock(&camsys_dev->irq.lock);
   
    return IRQ_HANDLED;
}

static int camsys_cif_remove(struct platform_device *pdev)
{
    camsys_dev_t *camsys_dev = platform_get_drvdata(pdev);
    camsys_cif_clk_t *cif_clk;
    
    if (camsys_dev->clk != NULL) {
        cif_clk = (camsys_cif_clk_t*)camsys_dev->clk;
        if (cif_clk->out_on) 
            camsys_cif_clkout_cb(camsys_dev->clk, 0);
        if (cif_clk->in_on)
            camsys_cif_clkin_cb(camsys_dev->clk, 0);

        if (cif_clk->pd_cif)
            clk_put(cif_clk->pd_cif);
        if (cif_clk->aclk_cif)
            clk_put(cif_clk->aclk_cif);
        if (cif_clk->hclk_cif)
            clk_put(cif_clk->hclk_cif);
        if (cif_clk->cif_clk_in)
            clk_put(cif_clk->cif_clk_in);
        if (cif_clk->cif_clk_out)
            clk_put(cif_clk->cif_clk_out);
    
        kfree(cif_clk);
        cif_clk = NULL;
    }

    return 0;
}

int camsys_cif_probe_cb(struct platform_device *pdev, camsys_dev_t *camsys_dev)
{
    int err = 0;   
    camsys_cif_clk_t *cif_clk;

    //Irq init
    err = request_irq(camsys_dev->irq.irq_id, camsys_cif_irq, 0, CAMSYS_CIF_IRQNAME,camsys_dev);
    if (err) {
        camsys_err("request irq for %s failed",CAMSYS_CIF_IRQNAME);
        goto end;
    }

    //Clk and Iomux init
    cif_clk = kzalloc(sizeof(camsys_cif_clk_t),GFP_KERNEL);
    if (cif_clk == NULL) {
        camsys_err("Allocate camsys_cif_clk_t failed!");
        err = -EINVAL;
        goto end;
    }
    
    if (strcmp(dev_name(&pdev->dev),CAMSYS_PLATFORM_CIF1_NAME) == 0) {
        cif_clk->pd_cif = clk_get(NULL, "pd_cif1");
        cif_clk->aclk_cif = clk_get(NULL, "aclk_cif1");
        cif_clk->hclk_cif = clk_get(NULL, "hclk_cif1");
        cif_clk->cif_clk_in = clk_get(NULL, "cif1_in");
        cif_clk->cif_clk_out = clk_get(NULL, "cif1_out");
        spin_lock_init(&cif_clk->lock);
        cif_clk->in_on = false;
        cif_clk->out_on = false;
    } else {           
        cif_clk->pd_cif = clk_get(NULL, "pd_cif0");
        cif_clk->aclk_cif = clk_get(NULL, "aclk_cif0");
        cif_clk->hclk_cif = clk_get(NULL, "hclk_cif0");
        cif_clk->cif_clk_in = clk_get(NULL, "pclkin_cif0");
        cif_clk->cif_clk_out = clk_get(NULL, "cif0_out");
        spin_lock_init(&cif_clk->lock);
        cif_clk->in_on = false;
        cif_clk->out_on = false;
    }
    camsys_dev->clk = (void*)cif_clk;
    camsys_dev->clkin_cb = camsys_cif_clkin_cb;
    camsys_dev->clkout_cb = camsys_cif_clkout_cb;
    camsys_dev->iomux = camsys_cif_iomux_cb;
    
    //Misc device init
    camsys_dev->miscdev.minor = MISC_DYNAMIC_MINOR;
    if (strcmp(dev_name(&pdev->dev),CAMSYS_PLATFORM_CIF1_NAME) == 0) {
        camsys_dev->miscdev.name = miscdev_cif1_name;
        camsys_dev->miscdev.nodename = miscdev_cif1_name;        
    } else {
        camsys_dev->miscdev.name = miscdev_cif0_name;
        camsys_dev->miscdev.nodename = miscdev_cif0_name;
    }
    camsys_dev->miscdev.fops = &camsys_fops;
    err = misc_register(&camsys_dev->miscdev);
    if (err < 0) {
        camsys_trace(1,"Register /dev/%s misc device failed",camsys_dev->miscdev.name);
        goto misc_register_failed;
    } else {
        camsys_trace(1,"Register /dev/%s misc device success",camsys_dev->miscdev.name);
    }

    //Variable init
    if (strcmp(dev_name(&pdev->dev),CAMSYS_PLATFORM_CIF1_NAME) == 0) {
        camsys_dev->dev_id = CAMSYS_DEVID_CIF_1;
    } else {
        camsys_dev->dev_id = CAMSYS_DEVID_CIF_0;
    }
    camsys_dev->platform_remove = camsys_cif_remove;

    return 0;

misc_register_failed:
    if (!IS_ERR(camsys_dev->miscdev.this_device)) {
        misc_deregister(&camsys_dev->miscdev);
    }

    if (cif_clk) {

        if (cif_clk->pd_cif)
            clk_put(cif_clk->pd_cif);
        if (cif_clk->aclk_cif)
            clk_put(cif_clk->aclk_cif);
        if (cif_clk->hclk_cif)
            clk_put(cif_clk->hclk_cif);
        if (cif_clk->cif_clk_in)
            clk_put(cif_clk->cif_clk_in);
        if (cif_clk->cif_clk_out)
            clk_put(cif_clk->cif_clk_out);
    
        kfree(cif_clk);
        cif_clk = NULL;
    }
    
end:
    return err;
}
EXPORT_SYMBOL_GPL(camsys_cif_probe_cb);


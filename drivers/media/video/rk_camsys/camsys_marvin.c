#include "camsys_marvin.h"

static const char miscdev_name[] = CAMSYS_MARVIN_DEVNAME;

static int camsys_mrv_iomux_cb(camsys_extdev_t *extdev,void *ptr)
{  
    unsigned int cif_vol_sel;
#if 0    
    if (extdev->dev_cfg & CAMSYS_DEVCFG_FLASHLIGHT) {
        iomux_set(ISP_FLASH_TRIG);  
        if (extdev->fl.fl.io != 0xffffffff) {
            iomux_set(ISP_FL_TRIG);
        }
    } 

    if (extdev->dev_cfg & CAMSYS_DEVCFG_PREFLASHLIGHT) {
        iomux_set(ISP_PRELIGHT_TRIG);
    }
    
    if (extdev->dev_cfg & CAMSYS_DEVCFG_SHUTTER) {
        iomux_set(ISP_SHUTTER_OPEN);
        iomux_set(ISP_SHUTTER_TRIG);
    }

    iomux_set(CIF0_CLKOUT);
#endif

    struct pinctrl      *pinctrl;
    struct pinctrl_state    *state;
    int retval = 0;
    char state_str[20] = {0};

    struct device *dev = &(extdev->pdev->dev);
    
    if (extdev->phy.type == CamSys_Phy_Cif) {
        if ((extdev->phy.info.cif.fmt >= CamSys_Fmt_Raw_8b)&& (extdev->phy.info.cif.fmt <= CamSys_Fmt_Raw_12b)) {

           strcpy(state_str,"isp_dvp8bit");

        }

        if ((extdev->phy.info.cif.fmt >= CamSys_Fmt_Raw_10b)&& (extdev->phy.info.cif.fmt <= CamSys_Fmt_Raw_12b)) {
           strcpy(state_str,"isp_dvp10bit");
        }

        if (extdev->phy.info.cif.fmt == CamSys_Fmt_Raw_12b) {
           strcpy(state_str,"isp_dvp12bit");

        }
    }else{
           strcpy(state_str,"default");
    }

    //mux CIF0_CLKOUT

    pinctrl = devm_pinctrl_get(dev);
    if (IS_ERR(pinctrl)) {
        camsys_err("%s:Get pinctrl failed!\n",__func__);
        return -1;
    }
    state = pinctrl_lookup_state(pinctrl,
                         state_str);
    if (IS_ERR(state)){
        dev_err(dev, "%s:could not get %s pinstate\n",__func__,state_str);
        return -1;
        }

    if (!IS_ERR(state)) {
        retval = pinctrl_select_state(pinctrl, state);
        if (retval){
            dev_err(dev,
                "%s:could not set %s pins\n",__func__,state_str);
                return -1;

                }
    }

    //set 1.8v vol domain for rk32
    __raw_writel(((1<<1)|(1<<(1+16))),RK_GRF_VIRT+0x0380);
   __raw_writel(0xffffffff, RK_GRF_VIRT+0x01d4);   

    //set cif vol domain
    if (extdev->phy.type == CamSys_Phy_Cif) {

        #if 0
        if (!IS_ERR_OR_NULL(extdev->dovdd.ldo)) {
            if (extdev->dovdd.max_uv >= 25000000) {
                __raw_writel(((1<<1)|(1<<(1+16))),RK30_GRF_BASE+0x018c);
            } else {
                __raw_writel((1<<(1+16)),RK30_GRF_BASE+0x018c);
            }
        } else {
            __raw_writel(((1<<1)|(1<<(1+16))),RK30_GRF_BASE+0x018c);
        }
        #else

        //set 1.8v vol domain
        __raw_writel(((1<<1)|(1<<(1+16))),RK_GRF_VIRT+0x0380);
        #endif
        
        //set driver strength
      //  __raw_writel(0xffffffff, RK_GRF_VIRT+0x01dc);   
    }
    
    return 0;
}

static int camsys_mrv_reset_cb(void *ptr)
{
    camsys_dev_t *camsys_dev = (camsys_dev_t*)ptr;
    #if 0 //do nothing ,zyc
    cru_set_soft_reset(SOFT_RST_ISP,true);
    udelay(100);
    cru_set_soft_reset(SOFT_RST_ISP,false);
    #endif
    camsys_trace(1, "%s soft reset\n",dev_name(camsys_dev->miscdev.this_device));
    return 0;
}

static int camsys_mrv_clkin_cb(void *ptr, unsigned int on)
{
    camsys_dev_t *camsys_dev = (camsys_dev_t*)ptr;
    camsys_mrv_clk_t *clk = (camsys_mrv_clk_t*)camsys_dev->clk;
    struct clk *cif_clk_out_div;
	//  spin_lock(&clk->lock);
    if (on && !clk->in_on) {
        
		clk_set_rate(clk->isp,180000000);
        clk_set_rate(clk->isp_jpe, 180000000);
   //     clk_set_rate(clk->aclk_isp,24000000);
   //     clk_set_rate(clk->hclk_isp,24000000);


        clk_prepare_enable(clk->aclk_isp);
        clk_prepare_enable(clk->hclk_isp);
        clk_prepare_enable(clk->isp);
        clk_prepare_enable(clk->isp_jpe);
        clk_prepare_enable(clk->clk_mipi_24m); 
        clk_prepare_enable(clk->pclkin_isp); 
		clk_prepare_enable(clk->pd_isp);


 //       clk_enable(clk->pd_isp);
  //      clk_enable(clk->aclk_isp);
  //  	clk_enable(clk->hclk_isp);    	
  //  	clk_enable(clk->isp);
 //   	clk_enable(clk->isp_jpe);
 //   	clk_enable(clk->pclkin_isp);
    	
        clk->in_on = true;

        camsys_trace(1, "%s clock in turn on",dev_name(camsys_dev->miscdev.this_device));
        camsys_mrv_reset_cb(ptr);       
        
    } else if (!on && clk->in_on) {


        clk_disable_unprepare(clk->aclk_isp);
        clk_disable_unprepare(clk->hclk_isp);
        clk_disable_unprepare(clk->isp);
        clk_disable_unprepare(clk->isp_jpe);
        clk_disable_unprepare(clk->clk_mipi_24m); 
        clk_disable_unprepare(clk->pclkin_isp); 
		clk_disable_unprepare(clk->pd_isp);
  //      clk_disable(clk->pd_isp);
  //      clk_disable(clk->aclk_isp);
   // 	clk_disable(clk->hclk_isp);
  //  	clk_disable(clk->isp);
  //  	clk_disable(clk->isp_jpe);
 //   	clk_disable(clk->pclkin_isp);


        clk->in_on = false;
        camsys_trace(1, "%s clock in turn off",dev_name(camsys_dev->miscdev.this_device));
    }
  //  spin_unlock(&clk->lock);
    return 0;
}

static int camsys_mrv_clkout_cb(void *ptr, unsigned int on,unsigned int inclk)
{
    camsys_dev_t *camsys_dev = (camsys_dev_t*)ptr;
    camsys_mrv_clk_t *clk = (camsys_mrv_clk_t*)camsys_dev->clk;
    struct clk *cif_clk_out_div;
    
    spin_lock(&clk->lock);
    if (on && (clk->out_on != on)) {  

        clk_set_rate(clk->cif_clk_out,inclk);
        clk_prepare_enable(clk->cif_clk_out);
        
		clk->out_on = on;
        camsys_trace(1, "%s clock out(rate: %dHz) turn on",dev_name(camsys_dev->miscdev.this_device),
                    clk->out_on);
    } else if (!on && clk->out_on) {
        cif_clk_out_div =  clk_get(NULL, "cif0_out_div");
        if(IS_ERR_OR_NULL(cif_clk_out_div)) {
            cif_clk_out_div =  clk_get(NULL, "cif_out_div");
			printk("can't get clk_cif_pll");
        }

        if(!IS_ERR_OR_NULL(cif_clk_out_div)) {
            clk_set_parent(clk->cif_clk_out, cif_clk_out_div);
            clk_put(cif_clk_out_div);
        } else {
            camsys_warn("%s clock out may be not off!", dev_name(camsys_dev->miscdev.this_device));
        }

        clk_disable_unprepare( clk->cif_clk_out);
//        clk_disable(clk->cif_clk_out);

        clk->out_on = 0;

        camsys_trace(1, "%s clock out turn off",dev_name(camsys_dev->miscdev.this_device));
    }
    spin_unlock(&clk->lock);    

    return 0;
}
static irqreturn_t camsys_mrv_irq(int irq, void *data)
{
    camsys_dev_t *camsys_dev = (camsys_dev_t*)data;
    camsys_irqstas_t *irqsta;
    camsys_irqpool_t *irqpool;
    unsigned int isp_mis,mipi_mis,mi_mis,*mis;

    isp_mis = __raw_readl((void volatile *)(camsys_dev->devmems.registermem->vir_base + MRV_ISP_MIS));
    mipi_mis = __raw_readl((void volatile *)(camsys_dev->devmems.registermem->vir_base + MRV_MIPI_MIS));
    mi_mis = __raw_readl((void volatile *)(camsys_dev->devmems.registermem->vir_base + MRV_MI_MIS));    

    __raw_writel(isp_mis, (void volatile *)(camsys_dev->devmems.registermem->vir_base + MRV_ISP_ICR)); 
    __raw_writel(mipi_mis, (void volatile *)(camsys_dev->devmems.registermem->vir_base + MRV_MIPI_ICR)); 
    __raw_writel(mi_mis, (void volatile *)(camsys_dev->devmems.registermem->vir_base + MRV_MI_ICR)); 

    spin_lock(&camsys_dev->irq.lock);
    if (!list_empty(&camsys_dev->irq.irq_pool)) {
        list_for_each_entry(irqpool, &camsys_dev->irq.irq_pool, list) {
            if (irqpool->pid != 0) {
                switch(irqpool->mis)
                {
                    case MRV_ISP_MIS:
                    {
                        mis = &isp_mis;
                        break;
                    }

                    case MRV_MIPI_MIS:
                    {
                        mis = &mipi_mis;
                        break;
                    }
                    case MRV_MI_MIS:
                    {
                        mis = &mi_mis;
                        break;
                    }

                    default:     
                    {
                        camsys_trace(2,"Thread(pid:%d) irqpool mis(%d) is invalidate",irqpool->pid,irqpool->mis);
                        goto end;
                    }
                }

                if (*mis != 0) {
                    spin_lock(&irqpool->lock);
                    if (!list_empty(&irqpool->deactive)) {
                        irqsta = list_first_entry(&irqpool->deactive, camsys_irqstas_t, list);
                        irqsta->sta.mis = *mis;                                                 
                        list_del_init(&irqsta->list);            
                        list_add_tail(&irqsta->list,&irqpool->active);                        
                        wake_up(&irqpool->done);
                    }
                    spin_unlock(&irqpool->lock);
                }
            }
        }
    }
end:    
    spin_unlock(&camsys_dev->irq.lock);

    return IRQ_HANDLED;
}
static int camsys_mrv_remove_cb(struct platform_device *pdev)
{
    camsys_dev_t *camsys_dev = platform_get_drvdata(pdev);
    camsys_mrv_clk_t *mrv_clk=NULL;

    if (camsys_dev->clk != NULL) {

        mrv_clk = (camsys_mrv_clk_t*)camsys_dev->clk;
        if (mrv_clk->out_on)
            camsys_mrv_clkout_cb(mrv_clk,0,0);
        if (mrv_clk->in_on)
            camsys_mrv_clkin_cb(mrv_clk,0);
    
        if (!IS_ERR_OR_NULL(mrv_clk->pd_isp)) {
		 	clk_put(mrv_clk->pd_isp);
        }
        if (!IS_ERR_OR_NULL(mrv_clk->aclk_isp)) {
            clk_put(mrv_clk->aclk_isp);
        }
        if (!IS_ERR_OR_NULL(mrv_clk->hclk_isp)) {
            clk_put(mrv_clk->hclk_isp);
        }
        if (!IS_ERR_OR_NULL(mrv_clk->isp)) {
            clk_put(mrv_clk->isp);
        }
        if (!IS_ERR_OR_NULL(mrv_clk->isp_jpe)) {
            clk_put(mrv_clk->isp_jpe);
        }
        if (!IS_ERR_OR_NULL(mrv_clk->pclkin_isp)) {
            clk_put(mrv_clk->pclkin_isp);
        }
        if (!IS_ERR_OR_NULL(mrv_clk->cif_clk_out)) {
            clk_put(mrv_clk->cif_clk_out);
        }

        kfree(mrv_clk);
        mrv_clk = NULL;
    }

    return 0;
}
int camsys_mrv_probe_cb(struct platform_device *pdev, camsys_dev_t *camsys_dev)
{
    int err = 0;   
    camsys_mrv_clk_t *mrv_clk=NULL;
    //struct clk *clk_parent; 
    struct clk *cif_clk_out_div;
    
	err = request_irq(camsys_dev->irq.irq_id, camsys_mrv_irq, 0, CAMSYS_MARVIN_IRQNAME,camsys_dev);
    if (err) {
        camsys_err("request irq for %s failed",CAMSYS_MARVIN_IRQNAME);
        goto end;
    }

    //Clk and Iomux init
    mrv_clk = kzalloc(sizeof(camsys_mrv_clk_t),GFP_KERNEL);
    if (mrv_clk == NULL) {
        camsys_err("Allocate camsys_mrv_clk_t failed!");
        err = -EINVAL;
        goto clk_failed;
    }
     
    mrv_clk->pd_isp = devm_clk_get(&pdev->dev, "pd_isp");
    mrv_clk->aclk_isp = devm_clk_get(&pdev->dev, "aclk_isp");
    mrv_clk->hclk_isp = devm_clk_get(&pdev->dev, "hclk_isp");
    mrv_clk->isp = devm_clk_get(&pdev->dev, "clk_isp");
    mrv_clk->isp_jpe = devm_clk_get(&pdev->dev, "clk_isp_jpe");
    mrv_clk->pclkin_isp = devm_clk_get(&pdev->dev, "pclkin_isp");
    mrv_clk->cif_clk_out = devm_clk_get(&pdev->dev, "clk_vipout");
    mrv_clk->clk_mipi_24m = devm_clk_get(&pdev->dev,"clk_mipi_24m"); 
    
	if (IS_ERR_OR_NULL(mrv_clk->pd_isp) || IS_ERR_OR_NULL(mrv_clk->aclk_isp) || IS_ERR_OR_NULL(mrv_clk->hclk_isp) ||
        IS_ERR_OR_NULL(mrv_clk->isp) || IS_ERR_OR_NULL(mrv_clk->isp_jpe) || IS_ERR_OR_NULL(mrv_clk->pclkin_isp) || 
        IS_ERR_OR_NULL(mrv_clk->cif_clk_out) || IS_ERR_OR_NULL(mrv_clk->clk_mipi_24m)) {
        camsys_err("Get %s clock resouce failed!\n",miscdev_name);
        err = -EINVAL;
        goto clk_failed;
    }

   
//    clk_set_rate(mrv_clk->isp,1800000000);
//    clk_set_rate(mrv_clk->isp_jpe,180000000);
    
    spin_lock_init(&mrv_clk->lock);
    
    mrv_clk->in_on = false;
    mrv_clk->out_on = 0;
        
    camsys_dev->clk = (void*)mrv_clk;
    camsys_dev->clkin_cb = camsys_mrv_clkin_cb;
    camsys_dev->clkout_cb = camsys_mrv_clkout_cb;
    camsys_dev->reset_cb = camsys_mrv_reset_cb;
    camsys_dev->iomux = camsys_mrv_iomux_cb;
    
    camsys_dev->miscdev.minor = MISC_DYNAMIC_MINOR;
    camsys_dev->miscdev.name = miscdev_name;
    camsys_dev->miscdev.nodename = miscdev_name;
    camsys_dev->miscdev.fops = &camsys_fops;

    err = misc_register(&camsys_dev->miscdev);
    if (err < 0) {
        camsys_err("misc register %s failed!",miscdev_name);
        goto misc_register_failed;
    }   

    //Variable init
    camsys_dev->dev_id = CAMSYS_DEVID_MARVIN;
    camsys_dev->platform_remove = camsys_mrv_remove_cb;
   
   	 
    return 0;
misc_register_failed:
    if (!IS_ERR_OR_NULL(camsys_dev->miscdev.this_device)) {
        misc_deregister(&camsys_dev->miscdev);
    }

clk_failed:
    if (mrv_clk != NULL) {
        if (!IS_ERR_OR_NULL(mrv_clk->pd_isp)) {
            clk_put(mrv_clk->pd_isp);
        }
        if (!IS_ERR_OR_NULL(mrv_clk->aclk_isp)) {
            clk_put(mrv_clk->aclk_isp);
        }
        if (!IS_ERR_OR_NULL(mrv_clk->hclk_isp)) {
            clk_put(mrv_clk->hclk_isp);
        }
        if (!IS_ERR_OR_NULL(mrv_clk->isp)) {
            clk_put(mrv_clk->isp);
        }
        if (!IS_ERR_OR_NULL(mrv_clk->isp_jpe)) {
            clk_put(mrv_clk->isp_jpe);
        }
        if (!IS_ERR_OR_NULL(mrv_clk->pclkin_isp)) {
            clk_put(mrv_clk->pclkin_isp);
        }
        if (!IS_ERR_OR_NULL(mrv_clk->cif_clk_out)) {
            clk_put(mrv_clk->cif_clk_out);
        }

        kfree(mrv_clk);
        mrv_clk = NULL;
    }
    
end:
    return err;
}
EXPORT_SYMBOL_GPL(camsys_mrv_probe_cb);


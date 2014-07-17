#include "camsys_marvin.h"
#include "camsys_soc_priv.h"
#include "camsys_gpio.h"

#include <linux/rockchip/common.h> 
#include <dt-bindings/clock/rk_system_status.h>

extern int rockchip_set_system_status(unsigned long status);
extern int rockchip_clear_system_status(unsigned long status);

static const char miscdev_name[] = CAMSYS_MARVIN_DEVNAME;


static int camsys_mrv_iomux_cb(camsys_extdev_t *extdev,void *ptr)
{
    struct pinctrl      *pinctrl;
    struct pinctrl_state    *state;
    int retval = 0;
    char state_str[20] = {0};
    camsys_dev_t *camsys_dev = (camsys_dev_t*)ptr;
    struct device *dev = &(extdev->pdev->dev);
    camsys_soc_priv_t *soc;

    // DVP IO Config
    
    if (extdev->phy.type == CamSys_Phy_Cif) {

        switch (extdev->phy.info.cif.fmt)
        {
            case CamSys_Fmt_Raw_8b:
            case CamSys_Fmt_Yuv420_8b:
            case CamSys_Fmt_Yuv422_8b:
            {
                if (extdev->phy.info.cif.cifio == CamSys_SensorBit0_CifBit0) {
                    strcpy(state_str,"isp_dvp8bit0");
                } else if (extdev->phy.info.cif.cifio == CamSys_SensorBit0_CifBit2) {
                    strcpy(state_str,"isp_dvp8bit2");
                } else {
                    camsys_err("extdev->phy.info.cif.cifio: 0x%x is invalidate!", extdev->phy.info.cif.cifio);
                    goto fail;
                }

                break;
            }

            case CamSys_Fmt_Raw_10b:
            {
                strcpy(state_str,"isp_dvp10bit");
                break;
            }

            case CamSys_Fmt_Raw_12b:
            {
                strcpy(state_str,"isp_dvp12bit");
                break;
            }

            default:
            {
                camsys_err("extdev->phy.info.cif.fmt: 0x%x is invalidate!",extdev->phy.info.cif.fmt);
                goto fail;
            }
        }        
    } else {
        if (extdev->dev_cfg & CAMSYS_DEVCFG_FLASHLIGHT) {
            if (extdev->dev_cfg & CAMSYS_DEVCFG_PREFLASHLIGHT) {
                strcpy(state_str,"isp_mipi_fl_prefl");
            } else {
                strcpy(state_str,"isp_mipi_fl");
            }
        } else {
            strcpy(state_str,"default");
        }
    }

    camsys_trace(1,"marvin pinctrl select: %s", state_str);
    
    pinctrl = devm_pinctrl_get(dev);
    if (IS_ERR(pinctrl)) {
        camsys_err("devm_pinctrl_get failed!");
        goto fail;
    }
    state = pinctrl_lookup_state(pinctrl,
                         state_str);
    if (IS_ERR(state)){
        camsys_err("pinctrl_lookup_state failed!");
        goto fail;
    }

    if (!IS_ERR(state)) {
        retval = pinctrl_select_state(pinctrl, state);
        if (retval){
            camsys_err("pinctrl_select_state failed!");
            goto fail;
        }
    }

    if (camsys_dev->soc) {
        soc = (camsys_soc_priv_t*)camsys_dev->soc;
        if (soc->soc_cfg) {
            (soc->soc_cfg)(Cif_IoDomain_Cfg,(void*)&extdev->dovdd.min_uv);
            (soc->soc_cfg)(Clk_DriverStrength_Cfg,(void*)&extdev->clk.driver_strength);
        } else {
            camsys_err("camsys_dev->soc->soc_cfg is NULL!");
        }
    } else {
        camsys_err("camsys_dev->soc is NULL!");
    }
    
    return 0;
fail:
    return -1;
}

static int camsys_mrv_flash_trigger_cb(void *ptr,unsigned int on)
{
    camsys_dev_t *camsys_dev = (camsys_dev_t*)ptr;
    struct device *dev = &(camsys_dev->pdev->dev);
    int flash_trigger_io ;
    struct pinctrl      *pinctrl;
    struct pinctrl_state    *state;
    char state_str[20] = {0};
    int retval = 0;
    enum of_gpio_flags flags;

    if(!on){
        strcpy(state_str,"isp_flash_as_gpio");
        pinctrl = devm_pinctrl_get(dev);
        if (IS_ERR(pinctrl)) {
            camsys_err("devm_pinctrl_get failed!");
        }
        state = pinctrl_lookup_state(pinctrl,
                             state_str);
        if (IS_ERR(state)){
            camsys_err("pinctrl_lookup_state failed!");
        }

        if (!IS_ERR(state)) {
            retval = pinctrl_select_state(pinctrl, state);
            if (retval){
                camsys_err("pinctrl_select_state failed!");
            }

        }

        //get gpio index
        flash_trigger_io = of_get_named_gpio_flags(camsys_dev->pdev->dev.of_node, "rockchip,gpios", 0, &flags);
        if(gpio_is_valid(flash_trigger_io)){
            flash_trigger_io = of_get_named_gpio_flags(camsys_dev->pdev->dev.of_node, "rockchip,gpios", 0, &flags);
            gpio_request(flash_trigger_io,"camsys_gpio");
            gpio_direction_output(flash_trigger_io, 1);
            }

    }else{
        strcpy(state_str,"isp_flash_as_trigger_out");
        pinctrl = devm_pinctrl_get(dev);
        if (IS_ERR(pinctrl)) {
            camsys_err("devm_pinctrl_get failed!");
        }
        state = pinctrl_lookup_state(pinctrl,
                             state_str);
        if (IS_ERR(state)){
            camsys_err("pinctrl_lookup_state failed!");
        }

        if (!IS_ERR(state)) {
            retval = pinctrl_select_state(pinctrl, state);
            if (retval){
                camsys_err("pinctrl_select_state failed!");
            }

        }
    }
    return retval;
}


static int camsys_mrv_reset_cb(void *ptr,unsigned int on)
{
    camsys_dev_t *camsys_dev = (camsys_dev_t*)ptr;
    camsys_soc_priv_t *soc;


    if (camsys_dev->soc) {
        soc = (camsys_soc_priv_t*)camsys_dev->soc;
        if (soc->soc_cfg) {
            (soc->soc_cfg)(Isp_SoftRst,(void*)on);
        } else {
            camsys_err("camsys_dev->soc->soc_cfg is NULL!");
        }
    } else {
        camsys_err("camsys_dev->soc is NULL!");
    }
    
    return 0;
}

static int camsys_mrv_clkin_cb(void *ptr, unsigned int on)
{
    camsys_dev_t *camsys_dev = (camsys_dev_t*)ptr;
    camsys_mrv_clk_t *clk = (camsys_mrv_clk_t*)camsys_dev->clk;
    unsigned long isp_clk;
	
    if (on && !clk->in_on) {
		rockchip_set_system_status(SYS_STATUS_ISP);

		if (on == 1) {
		    isp_clk = 210000000;           
		} else {
		    isp_clk = 420000000;            
		}

		clk_set_rate(clk->isp,isp_clk);
        clk_set_rate(clk->isp_jpe, isp_clk);

        clk_prepare_enable(clk->aclk_isp);
        clk_prepare_enable(clk->hclk_isp);
        clk_prepare_enable(clk->isp);
        clk_prepare_enable(clk->isp_jpe);
        clk_prepare_enable(clk->clk_mipi_24m); 
        clk_prepare_enable(clk->pclkin_isp); 
		clk_prepare_enable(clk->pd_isp);

        clk->in_on = true;

        camsys_trace(1, "%s clock(f: %ld Hz) in turn on",dev_name(camsys_dev->miscdev.this_device),isp_clk);
        camsys_mrv_reset_cb(ptr,1);
        udelay(100);
        camsys_mrv_reset_cb(ptr,0);
        
    } else if (!on && clk->in_on) {

        clk_disable_unprepare(clk->aclk_isp);
        clk_disable_unprepare(clk->hclk_isp);
        clk_disable_unprepare(clk->isp);
        clk_disable_unprepare(clk->isp_jpe);
        clk_disable_unprepare(clk->clk_mipi_24m); 
        clk_disable_unprepare(clk->pclkin_isp); 
		clk_disable_unprepare(clk->pd_isp);

		rockchip_clear_system_status(SYS_STATUS_ISP);
        clk->in_on = false;
        camsys_trace(1, "%s clock in turn off",dev_name(camsys_dev->miscdev.this_device));
    }
    
    return 0;
}
static int camsys_mrv_clkout_cb(void *ptr, unsigned int on,unsigned int inclk)
{
    camsys_dev_t *camsys_dev = (camsys_dev_t*)ptr;
    camsys_mrv_clk_t *clk = (camsys_mrv_clk_t*)camsys_dev->clk;
    
    mutex_lock(&clk->lock);
    if (on && (clk->out_on != on)) {  

        clk_set_rate(clk->cif_clk_out,inclk);
        clk_prepare_enable(clk->cif_clk_out);
        
		clk->out_on = on;
        camsys_trace(1, "%s clock out(rate: %dHz) turn on",dev_name(camsys_dev->miscdev.this_device),
                    inclk);
    } else if (!on && clk->out_on) {
        if(!IS_ERR_OR_NULL(clk->cif_clk_pll)) {
            clk_set_parent(clk->cif_clk_out, clk->cif_clk_pll);
        } else {
            camsys_warn("%s clock out may be not off!", dev_name(camsys_dev->miscdev.this_device));
        }

        clk_disable_unprepare( clk->cif_clk_out);

        clk->out_on = 0;

        camsys_trace(1, "%s clock out turn off",dev_name(camsys_dev->miscdev.this_device));
    }
    mutex_unlock(&clk->lock);    

    return 0;
}
static irqreturn_t camsys_mrv_irq(int irq, void *data)
{
    camsys_dev_t *camsys_dev = (camsys_dev_t*)data;
    camsys_irqstas_t *irqsta;
    camsys_irqpool_t *irqpool;
    unsigned int isp_mis,mipi_mis,mi_mis,*mis;
	
	unsigned int mi_ris,mi_imis;

    isp_mis = __raw_readl((void volatile *)(camsys_dev->devmems.registermem->vir_base + MRV_ISP_MIS));
    mipi_mis = __raw_readl((void volatile *)(camsys_dev->devmems.registermem->vir_base + MRV_MIPI_MIS));

	mi_mis = __raw_readl((void volatile *)(camsys_dev->devmems.registermem->vir_base + MRV_MI_MIS));
#if 1	
	mi_ris =  __raw_readl((void volatile *)(camsys_dev->devmems.registermem->vir_base + MRV_MI_RIS));
	mi_imis = __raw_readl((void volatile *)(camsys_dev->devmems.registermem->vir_base + MRV_MI_IMIS));
	while((mi_ris & mi_imis) != mi_mis){
		camsys_trace(2,"mi_mis status erro,mi_mis 0x%x,mi_ris 0x%x,imis 0x%x\n",mi_mis,mi_ris,mi_imis);
		mi_mis = __raw_readl((void volatile *)(camsys_dev->devmems.registermem->vir_base + MRV_MI_MIS));
		mi_ris =  __raw_readl((void volatile *)(camsys_dev->devmems.registermem->vir_base + MRV_MI_RIS));
    	mi_imis = __raw_readl((void volatile *)(camsys_dev->devmems.registermem->vir_base + MRV_MI_IMIS));
	}

#endif

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
		 	devm_clk_put(&pdev->dev,mrv_clk->pd_isp);
        }
        if (!IS_ERR_OR_NULL(mrv_clk->aclk_isp)) {
            devm_clk_put(&pdev->dev,mrv_clk->aclk_isp);
        }
        if (!IS_ERR_OR_NULL(mrv_clk->hclk_isp)) {
            devm_clk_put(&pdev->dev,mrv_clk->hclk_isp);
        }
        if (!IS_ERR_OR_NULL(mrv_clk->isp)) {
            devm_clk_put(&pdev->dev,mrv_clk->isp);
        }
        if (!IS_ERR_OR_NULL(mrv_clk->isp_jpe)) {
            devm_clk_put(&pdev->dev,mrv_clk->isp_jpe);
        }
        if (!IS_ERR_OR_NULL(mrv_clk->pclkin_isp)) {
            devm_clk_put(&pdev->dev,mrv_clk->pclkin_isp);
        }
        if (!IS_ERR_OR_NULL(mrv_clk->cif_clk_out)) {
            devm_clk_put(&pdev->dev,mrv_clk->cif_clk_out);
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
    mrv_clk->cif_clk_out = devm_clk_get(&pdev->dev, "clk_cif_out");
    mrv_clk->cif_clk_pll = devm_clk_get(&pdev->dev, "clk_cif_pll");
    mrv_clk->clk_mipi_24m = devm_clk_get(&pdev->dev,"clk_mipi_24m"); 
    
	if (IS_ERR_OR_NULL(mrv_clk->pd_isp) || IS_ERR_OR_NULL(mrv_clk->aclk_isp) || IS_ERR_OR_NULL(mrv_clk->hclk_isp) ||
        IS_ERR_OR_NULL(mrv_clk->isp) || IS_ERR_OR_NULL(mrv_clk->isp_jpe) || IS_ERR_OR_NULL(mrv_clk->pclkin_isp) || 
        IS_ERR_OR_NULL(mrv_clk->cif_clk_out) || IS_ERR_OR_NULL(mrv_clk->clk_mipi_24m)) {
        camsys_err("Get %s clock resouce failed!\n",miscdev_name);
        err = -EINVAL;
        goto clk_failed;
    }
    
    clk_set_rate(mrv_clk->isp,210000000);
    clk_set_rate(mrv_clk->isp_jpe, 210000000);
    
    mutex_init(&mrv_clk->lock);
    
    mrv_clk->in_on = false;
    mrv_clk->out_on = 0;
        
    camsys_dev->clk = (void*)mrv_clk;
    camsys_dev->clkin_cb = camsys_mrv_clkin_cb;
    camsys_dev->clkout_cb = camsys_mrv_clkout_cb;
    camsys_dev->reset_cb = camsys_mrv_reset_cb;
    camsys_dev->iomux = camsys_mrv_iomux_cb;
    camsys_dev->flash_trigger_cb = camsys_mrv_flash_trigger_cb;
    
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


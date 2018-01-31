/* SPDX-License-Identifier: GPL-2.0 */
#include "camsys_cif.h"

static const char miscdev_cif0_name[] = CAMSYS_CIF0_DEVNAME;
static const char miscdev_cif1_name[] = CAMSYS_CIF1_DEVNAME;

static int camsys_cif_iomux_cb(camsys_extdev_t *extdev, void *ptr)
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
		if ((extdev->phy.info.cif.fmt >= CamSys_Fmt_Raw_8b) &&
			(extdev->phy.info.cif.fmt <= CamSys_Fmt_Raw_12b)) {

			strcpy(state_str, "isp_dvp8bit");

		}

		if ((extdev->phy.info.cif.fmt >= CamSys_Fmt_Raw_10b) &&
			(extdev->phy.info.cif.fmt <= CamSys_Fmt_Raw_12b)) {
			strcpy(state_str, "isp_dvp10bit");
		}

		if (extdev->phy.info.cif.fmt == CamSys_Fmt_Raw_12b) {
			strcpy(state_str, "isp_dvp12bit");

		}
	} else {
		strcpy(state_str, "default");
    }

    /*mux CIF0_CLKOUT*/

    pinctrl = devm_pinctrl_get(dev);
    if (IS_ERR(pinctrl)) {
		camsys_err("%s:Get pinctrl failed!\n", __func__);
		return -1;
    }
    state = pinctrl_lookup_state(pinctrl,
								state_str);
	if (IS_ERR(state)) {
		dev_err(dev,
			"%s:could not get %s pinstate\n",
			__func__, state_str);
		return -1;
	}

    if (!IS_ERR(state)) {
		retval = pinctrl_select_state(pinctrl, state);
		if (retval) {
			dev_err(dev,
				"%s:could not set %s pins\n",
				__func__, state_str);
				return -1;

			}
    }

    /*set 1.8v vol domain for rk32*/
	__raw_writel(((1<<1)|(1<<(1+16))), RK_GRF_VIRT+0x0380);
	__raw_writel(0xffffffff,  RK_GRF_VIRT+0x01d4);

    /*set cif vol domain*/
    if (extdev->phy.type == CamSys_Phy_Cif) {

		#if 0
		if (!IS_ERR_OR_NULL(extdev->dovdd.ldo)) {
			if (extdev->dovdd.max_uv >= 25000000) {
				__raw_writel(((1<<1)|(1<<(1+16))),
					RK30_GRF_BASE+0x018c);
			} else {
				__raw_writel((1<<(1+16)), RK30_GRF_BASE+0x018c);
			}
		} else {
			 __raw_writel(((1<<1)|(1<<(1+16))),
					RK30_GRF_BASE+0x018c);
		}
		#else

		/*set 1.8v vol domain*/
		__raw_writel(((1<<1)|(1<<(1+16))), RK_GRF_VIRT+0x0380);
		#endif

		/*set driver strength*/
		/*  __raw_writel(0xffffffff,  RK_GRF_VIRT+0x01dc);*/
	}

    return 0;
}
static int camsys_cif_clkin_cb(void *ptr,  unsigned int on)
{
	camsys_dev_t *camsys_dev = (camsys_dev_t *)ptr;
	camsys_cif_clk_t *clk = (camsys_cif_clk_t *)camsys_dev->clk;

    spin_lock(&clk->lock);
	if (on && !clk->in_on) {
		clk_prepare_enable(clk->aclk_cif);
		clk_prepare_enable(clk->hclk_cif);
		clk_prepare_enable(clk->cif_clk_in);

		clk->in_on = true;
		camsys_trace(1,  "%s clock in turn on",
			dev_name(camsys_dev->miscdev.this_device));
    } else if (!on && clk->in_on) {
		clk_disable_unprepare(clk->hclk_cif);
		clk_disable_unprepare(clk->cif_clk_in);
		clk_disable_unprepare(clk->pd_cif);
		clk->in_on = false;
		camsys_trace(1,  "%s clock in turn off",
			dev_name(camsys_dev->miscdev.this_device));
	}
	spin_unlock(&clk->lock);
	return 0;
}

static int camsys_cif_clkout_cb(void *ptr, unsigned int on, unsigned int clkin)
{
	camsys_dev_t *camsys_dev = (camsys_dev_t *)ptr;
	camsys_cif_clk_t *clk = (camsys_cif_clk_t *)camsys_dev->clk;
    struct clk *cif_clk_out_div;

	spin_lock(&clk->lock);
	if (on && (clk->out_on != on)) {
		clk_prepare_enable(clk->cif_clk_out);
		clk_set_rate(clk->cif_clk_out, clkin);

		clk->out_on = on;
		camsys_trace(1,  "%s clock out(rate: %dHz) turn on",
			dev_name(camsys_dev->miscdev.this_device),
					clk->out_on);
	} else if (!on && clk->out_on) {
		if (strcmp(dev_name(camsys_dev->miscdev.this_device),
			miscdev_cif1_name) == 0) {
			cif_clk_out_div =  clk_get(NULL,  "cif1_out_div");
		} else{
			cif_clk_out_div =  clk_get(NULL,  "cif0_out_div");
			if (IS_ERR_OR_NULL(cif_clk_out_div)) {
				cif_clk_out_div =
					clk_get(NULL,  "cif_out_div");
			}
		}

		if (!IS_ERR_OR_NULL(cif_clk_out_div)) {
			clk_set_parent(clk->cif_clk_out,  cif_clk_out_div);
			clk_put(cif_clk_out_div);
		} else {
			camsys_warn("%s clock out may be not off!",
				dev_name(camsys_dev->miscdev.this_device));
		}
		clk_disable_unprepare(clk->cif_clk_out);
		clk->out_on = 0;

		camsys_trace(1,  "%s clock out turn off",
			dev_name(camsys_dev->miscdev.this_device));
	}
	spin_unlock(&clk->lock);

	/*  __raw_writel(0x00,  CRU_PCLK_REG30+RK30_CRU_BASE);*/

	return 0;
}

static irqreturn_t camsys_cif_irq(int irq,  void *data)
{
    camsys_dev_t *camsys_dev = (camsys_dev_t *)data;
    camsys_irqstas_t *irqsta;
    camsys_irqpool_t *irqpool;
    unsigned int intsta, frmsta;

    intsta = __raw_readl(camsys_dev->devmems.registermem->vir_base +
		CIF_INITSTA);
    frmsta = __raw_readl(camsys_dev->devmems.registermem->vir_base +
		CIF_FRAME_STATUS);
   printk("get oneframe, intsta = 0x%x \n", intsta);

    if (intsta & 0x200) {
		__raw_writel(0x200,
			camsys_dev->devmems.registermem->vir_base +
			CIF_INITSTA);
		__raw_writel(0xf000,
			camsys_dev->devmems.registermem->vir_base +
			CIF_CTRL);
	}

	if (intsta &0x01) {
		__raw_writel(0x01,
			camsys_dev->devmems.registermem->vir_base +
			CIF_INITSTA);
		__raw_writel(0x02,
			camsys_dev->devmems.registermem->vir_base +
			CIF_FRAME_STATUS);
		__raw_writel(0xf001,
			camsys_dev->devmems.registermem->vir_base +
			CIF_CTRL);
	}

	spin_lock(&camsys_dev->irq.lock);
	list_for_each_entry(irqpool,  &camsys_dev->irq.irq_pool,  list) {
		spin_lock(&irqpool->lock);
		if (!list_empty(&irqpool->deactive)) {
			irqsta = list_first_entry
				(&irqpool->deactive,  camsys_irqstas_t,  list);
			irqsta->sta.mis = intsta;
			irqsta->sta.ris = intsta;
			list_del_init(&irqsta->list);
			list_add_tail(&irqsta->list, &irqpool->active);
			irqsta = list_first_entry
				(&irqpool->active,  camsys_irqstas_t,  list);
			/*wake_up_all(&camsys_dev->irq.irq_done);*/
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
		cif_clk = (camsys_cif_clk_t *)camsys_dev->clk;
		if (cif_clk->out_on)
			camsys_cif_clkout_cb(camsys_dev->clk, 0, 0);
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

    /*Irq init*/
	err = request_irq(camsys_dev->irq.irq_id,
		camsys_cif_irq,  0,  CAMSYS_CIF_IRQNAME,
		camsys_dev);
	if (err) {
		camsys_err("request irq for %s failed", CAMSYS_CIF_IRQNAME);
		goto end;
	}

    /*Clk and Iomux init*/
	cif_clk = kzalloc(sizeof(camsys_cif_clk_t), GFP_KERNEL);
	if (cif_clk == NULL) {
		camsys_err("Allocate camsys_cif_clk_t failed!");
		err = -EINVAL;
		goto end;
	}

	if (strcmp(dev_name(&pdev->dev), CAMSYS_PLATFORM_CIF1_NAME) == 0) {
		cif_clk->aclk_cif = devm_clk_get(&pdev->dev,  "g_aclk_vip");
		cif_clk->hclk_cif = devm_clk_get(&pdev->dev,  "g_hclk_vip");
		cif_clk->cif_clk_in = devm_clk_get(&pdev->dev,  "g_pclkin_cif");
		cif_clk->cif_clk_out = devm_clk_get(&pdev->dev,  "clk_cif_out");
		spin_lock_init(&cif_clk->lock);
		cif_clk->in_on = false;
		cif_clk->out_on = false;
	} else {
		cif_clk->aclk_cif = devm_clk_get(&pdev->dev,  "g_aclk_vip");
		cif_clk->hclk_cif = devm_clk_get(&pdev->dev,  "g_hclk_vip");
		cif_clk->cif_clk_in = devm_clk_get(&pdev->dev,  "g_pclkin_ci");
		cif_clk->cif_clk_out = devm_clk_get(&pdev->dev,  "clk_cif_out");
		spin_lock_init(&cif_clk->lock);
		cif_clk->in_on = false;
		cif_clk->out_on = false;
	}

	/*
	*clk_prepare_enable(cif_clk->aclk_cif);
	clk_prepare_enable(cif_clk->hclk_cif);
	clk_prepare_enable(cif_clk->cif_clk_in);
	clk_prepare_enable(cif_clk->cif_clk_out);
	*/

	camsys_dev->clk = (void *)cif_clk;
	camsys_dev->clkin_cb = camsys_cif_clkin_cb;
	camsys_dev->clkout_cb = camsys_cif_clkout_cb;
	camsys_dev->iomux = camsys_cif_iomux_cb;

    /*Misc device init*/
	camsys_dev->miscdev.minor = MISC_DYNAMIC_MINOR;
	if (strcmp(dev_name(&pdev->dev), CAMSYS_PLATFORM_CIF1_NAME) == 0) {
	    camsys_dev->miscdev.name = miscdev_cif1_name;
	    camsys_dev->miscdev.nodename = miscdev_cif1_name;
	} else {
	    camsys_dev->miscdev.name = miscdev_cif0_name;
	    camsys_dev->miscdev.nodename = miscdev_cif0_name;
	}
	camsys_dev->miscdev.fops = &camsys_fops;
	err = misc_register(&camsys_dev->miscdev);
	if (err < 0) {
	    camsys_trace(1,
			"Register /dev/%s misc device failed",
			camsys_dev->miscdev.name);
	    goto misc_register_failed;
	} else {
	    camsys_trace(1,
			"Register /dev/%s misc device success",
			camsys_dev->miscdev.name);
	}

	/*Variable init*/
	if (strcmp(dev_name(&pdev->dev), CAMSYS_PLATFORM_CIF1_NAME) == 0) {
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


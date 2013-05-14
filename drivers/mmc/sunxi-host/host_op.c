/*
 * drivers/mmc/sunxi-host/host_op.c
 * (C) Copyright 2007-2011
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Aaron.Maoye <leafy.myeh@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#include "smc_syscall.h"
#include "host_op.h"
#include "sdxc.h"
#include <asm/uaccess.h>
#include <mach/clock.h>

static DEFINE_MUTEX(sw_host_rescan_mutex);
static int sw_host_rescan_pending[4] = { 0, };
struct sunxi_mmc_host* sw_host[4] = {NULL, NULL, NULL, NULL};

static int sdc_used;

unsigned int smc_debug = 0;
module_param_named(debuglevel, smc_debug, int, 0);

s32 sunximmc_init_controller(struct sunxi_mmc_host* smc_host)
{
    SMC_INFO("MMC Driver init host %d\n", smc_host->pdev->id);

    sdxc_init(smc_host);

    return 0;
}

/* static s32 sunximmc_set_src_clk(struct sunxi_mmc_host* smc_host)
 * 设置SD卡控制器源时钟频率, 目标为100MHz，clock源有smc_host的clk_source决定
 * clk_source: 0-video PLL, 2-dram PLL, 3-core pll
 */
static int sunximmc_set_src_clk(struct sunxi_mmc_host* smc_host)
{
    struct clk *source_clock = NULL;
    char* name[] = {"hosc", "sata_pll_2", "sdram_pll_p", "hosc"};
    int ret;

    switch (smc_host->clk_source)
    {
        case 0:
        case 3:
            source_clock = clk_get(&smc_host->pdev->dev, "hosc");
            break;
        case 1:
            source_clock = clk_get(&smc_host->pdev->dev, "sata_pll_2");
            break;
        case 2:
            source_clock = clk_get(&smc_host->pdev->dev, "sdram_pll_p");
            break;
    }
    if (IS_ERR(source_clock))
    {
    	ret = PTR_ERR(source_clock);
    	SMC_ERR("Error to get source clock %s\n", name[smc_host->clk_source]);
    	return ret;
    }

    clk_set_parent(smc_host->mclk, source_clock);
    clk_set_rate(smc_host->mclk, smc_host->mod_clk);
    clk_enable(smc_host->mclk);

    smc_host->mod_clk = clk_get_rate(smc_host->mclk);
    clk_enable(smc_host->hclk);

    SMC_INFO("smc %d, source = %s, src_clk = %u, mclk %u, \n", smc_host->pdev->id, name[smc_host->clk_source], (unsigned)clk_get_rate(source_clock), smc_host->mod_clk);
    clk_put(source_clock);

    return 0;
}

static int sunximmc_resource_request(struct sunxi_mmc_host *smc_host)
{
    struct platform_device *pdev = smc_host->pdev;
    u32 smc_no = pdev->id;
    char hclk_name[16] = {0};
    char mclk_name[8] = {0};
    char pio_para[16] = {0};
    u32 pio_hdle = 0;
    s32 ret = 0;

    sprintf(pio_para, "mmc%d_para", smc_no);
    pio_hdle = gpio_request_ex(pio_para, NULL);
    if (!pio_hdle)
    {
        SMC_ERR("sdc %d request pio parameter failed\n", smc_no);
        goto out;
    }
    smc_host->pio_hdle = pio_hdle;

    //iomap
    smc_host->smc_base_res  = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (!smc_host->smc_base_res)
    {
    	SMC_ERR("Failed to get io memory region resouce.\n");

    	ret = -ENOENT;
    	goto release_pin;
    }
    /* smc address remap */
    smc_host->smc_base_res = request_mem_region(smc_host->smc_base_res->start, RESSIZE(smc_host->smc_base_res), pdev->name);
    if (!smc_host->smc_base_res)
    {
    	SMC_ERR("Failed to request io memory region.\n");
    	ret = -ENOENT;
    	goto release_pin;
    }
    smc_host->smc_base = ioremap(smc_host->smc_base_res->start, RESSIZE(smc_host->smc_base_res));
    if (!smc_host->smc_base)
    {
    	SMC_ERR("Failed to ioremap() io memory region.\n");
    	ret = -EINVAL;
    	goto free_mem_region;
    }

    //get hclock
    sprintf(hclk_name, "ahb_sdc%d", smc_no);
    smc_host->hclk = clk_get(&pdev->dev, hclk_name);
    if (IS_ERR(smc_host->hclk))
    {
    	ret = PTR_ERR(smc_host->hclk);
    	SMC_ERR("Error to get ahb clk for %s\n", hclk_name);
    	goto iounmap;
    }

    sprintf(mclk_name, "sdc%d", smc_no);
    smc_host->mclk = clk_get(&pdev->dev, mclk_name);
    if (IS_ERR(smc_host->mclk))
    {
    	ret = PTR_ERR(smc_host->mclk);
    	SMC_ERR("Error to get clk for mux_mmc\n");
    	goto free_hclk;
    }

    goto out;

free_hclk:
    clk_put(smc_host->hclk);

iounmap:
    iounmap(smc_host->smc_base);

free_mem_region:
    release_mem_region(smc_host->smc_base_res->start, RESSIZE(smc_host->smc_base_res));

release_pin:
    gpio_release(smc_host->pio_hdle, 1);

out:
    return ret;
}


static int sunximmc_resource_release(struct sunxi_mmc_host *smc_host)
{
    //close clock resource
    clk_disable(smc_host->hclk);
    clk_put(smc_host->hclk);

    clk_disable(smc_host->mclk);
    clk_put(smc_host->mclk);

    //free memory region
    iounmap(smc_host->smc_base);
    release_mem_region(smc_host->smc_base_res->start, RESSIZE(smc_host->smc_base_res));

    gpio_release(smc_host->pio_hdle, 1);
    return 0;
}


static inline void sunximmc_suspend_pins(struct sunxi_mmc_host* smc_host)
{
    int ret;
    user_gpio_set_t suspend_gpio_set = {"suspend_pins_sdio", 0, 0, 0, 2, 1, 0};     //for sdio
    user_gpio_set_t suspend_gpio_set_card = {"suspend_pins_mmc", 0, 0, 0, 0, 1, 0};    //for mmc card
    u32 i;

    SMC_DBG("mmc %d suspend pins\n", smc_host->pdev->id);
    /* backup gpios' current config */
    ret = gpio_get_all_pin_status(smc_host->pio_hdle, smc_host->bak_gpios, 6, 1);
    if (ret)
    {
        SMC_ERR("fail to fetch current gpio cofiguration\n");
        return;
    }

//    {
//        SMC_MSG("printk backup gpio configuration: \n");
//        for (i=0; i<6; i++)
//        {
//            SMC_MSG("gpio[%d]: name %s, port %c[%d], cfg %d, pull %d, drvl %d, data %d\n",
//                         i, smc_host->bak_gpios[i].gpio_name,
//                            smc_host->bak_gpios[i].port + 'A' - 1,
//                            smc_host->bak_gpios[i].port_num,
//                            smc_host->bak_gpios[i].mul_sel,
//                            smc_host->bak_gpios[i].pull,
//                            smc_host->bak_gpios[i].drv_level,
//                            smc_host->bak_gpios[i].data);
//        }
//    }

    switch(smc_host->pdev->id)
    {
        case 0:
        case 1:
        case 2:
            /* setup all pins to input and no pull to save power */
            for (i=0; i<6; i++)
            {
                ret = gpio_set_one_pin_status(smc_host->pio_hdle, &suspend_gpio_set_card, smc_host->bak_gpios[i].gpio_name, 1);
                if (ret)
                {
                    SMC_ERR("fail to set IO(%s) into suspend status\n", smc_host->bak_gpios[i].gpio_name);
                }
            }
            break;
        case 3:
            /* setup all pins to input and pulldown to save power */
            for (i=0; i<6; i++)
            {
                ret = gpio_set_one_pin_status(smc_host->pio_hdle, &suspend_gpio_set, smc_host->bak_gpios[i].gpio_name, 1);
                if (ret)
                {
                    SMC_ERR("fail to set IO(%s) into suspend status\n", smc_host->bak_gpios[i].gpio_name);
                }
            }
            break;
    }

//    {
//        user_gpio_set_t post_cfg[6];
//
//        gpio_get_all_pin_status(smc_host->pio_hdle, post_cfg, 6, 1);
//        for (i=0; i<6; i++)
//        {
//            SMC_MSG("post suspend, gpio[%d]: name %s, port %c[%d], cfg %d, pull %d, drvl %d, data %d\n",
//                         i, post_cfg[i].gpio_name,
//                            post_cfg[i].port + 'A' - 1,
//                            post_cfg[i].port_num,
//                            post_cfg[i].mul_sel,
//                            post_cfg[i].pull,
//                            post_cfg[i].drv_level,
//                            post_cfg[i].data);
//        }
//    }

    smc_host->gpio_suspend_ok = 1;
    return;
}

static inline void sunximmc_resume_pins(struct sunxi_mmc_host* smc_host)
{
    int ret;
    u32 i;

    SMC_DBG("mmc %d resume pins\n", smc_host->pdev->id);
    switch(smc_host->pdev->id)
    {
        case 0:
        case 1:
        case 2:
        case 3:
            /* restore gpios' backup configuration */
            if (smc_host->gpio_suspend_ok)
            {
                smc_host->gpio_suspend_ok = 0;
                for (i=0; i<6; i++)
                {
                    ret = gpio_set_one_pin_status(smc_host->pio_hdle, &smc_host->bak_gpios[i], smc_host->bak_gpios[i].gpio_name, 1);
                    if (ret)
                    {
                        SMC_ERR("fail to restore IO(%s) to resume status\n", smc_host->bak_gpios[i].gpio_name);
                    }
                }
            }

            break;
    }
}


static void sunximmc_finalize_request(struct sunxi_mmc_host *smc_host)
{
    struct mmc_request* mrq = smc_host->mrq;

    if (smc_host->wait != SDC_WAIT_FINALIZE)
    {
	    SMC_MSG("nothing finalize\n");
        return;
	}

    SMC_DBG("request finalize !!\n");
    sdxc_request_done(smc_host);

    if (smc_host->error)
    {
        mrq->cmd->error = ETIMEDOUT;
        if (mrq->data)
        	mrq->data->error = ETIMEDOUT;
        if (mrq->stop)
            mrq->data->error = ETIMEDOUT;
    }
    else
    {
    	if (mrq->data)
    	    mrq->data->bytes_xfered = (mrq->data->blocks * mrq->data->blksz);
    }

    smc_host->wait = SDC_WAIT_NONE;
    smc_host->mrq = NULL;
    smc_host->error = 0;
    smc_host->todma = 0;
    smc_host->pio_active = XFER_NONE;
    mmc_request_done(smc_host->mmc, mrq);

    return;
}

static s32 sunximmc_get_ro(struct mmc_host *mmc)
{
    struct sunxi_mmc_host *smc_host = mmc_priv(mmc);
    char mmc_para[16] = {0};
    int card_wp = 0;
    int ret;
    u32 gpio_val;

    sprintf(mmc_para, "mmc%d_para", smc_host->pdev->id);
    ret = script_parser_fetch(mmc_para, "sdc_use_wp", &card_wp, sizeof(int));
    if (ret)
    {
    	SMC_ERR("sdc fetch card write protect mode failed\n");
    }
    if (card_wp)
    {
        gpio_val = gpio_read_one_pin_value(smc_host->pio_hdle, "sdc_wp");
        SMC_DBG("sdc fetch card write protect pin status val = %d \n", gpio_val);
        if (!gpio_val)
        {
            smc_host->read_only = 0;
            return 0;
        }
        else
        {
            SMC_MSG("Card is write-protected\n");
            smc_host->read_only = 1;
            return 1;
        }
    }
    else
    {
        smc_host->read_only = 0;
        return 0;
    }
}

static void sunximmc_cd_timer(unsigned long data)
{
    struct sunxi_mmc_host *smc_host = (struct sunxi_mmc_host *)data;
    u32 gpio_val;
    u32 present;

    gpio_val = gpio_read_one_pin_value(smc_host->pio_hdle, "sdc_det");
    gpio_val += gpio_read_one_pin_value(smc_host->pio_hdle, "sdc_det");
    gpio_val += gpio_read_one_pin_value(smc_host->pio_hdle, "sdc_det");
    gpio_val += gpio_read_one_pin_value(smc_host->pio_hdle, "sdc_det");
    gpio_val += gpio_read_one_pin_value(smc_host->pio_hdle, "sdc_det");
    if (gpio_val==5)
        present = 0;
    else if (gpio_val==0)
        present = 1;
    else
        goto modtimer;
//    SMC_DBG("cd %d, host present %d, cur present %d\n", gpio_val, smc_host->present, present);

    if (smc_host->present ^ present) {
        SMC_MSG("mmc %d detect change, present %d\n", smc_host->pdev->id, present);
        smc_host->present = present;
        if (smc_host->present)
            mmc_detect_change(smc_host->mmc, msecs_to_jiffies(300));
        else
            mmc_detect_change(smc_host->mmc, msecs_to_jiffies(10));
    } else {
//        SMC_DBG("card detect no change\n");
    }

modtimer:
    mod_timer(&smc_host->cd_timer, jiffies + 30);
    return;
}

static int sunximmc_card_present(struct mmc_host *mmc)
{
    struct sunxi_mmc_host *smc_host = mmc_priv(mmc);

    if (smc_host->cd_mode == CARD_ALWAYS_PRESENT) {
        return 1;
    }
    else
        return smc_host->present;
}

static irqreturn_t sunximmc_irq(int irq, void *dev_id)
{
    struct sunxi_mmc_host *smc_host = dev_id;
    unsigned long iflags;

    spin_lock_irqsave(&smc_host->lock, iflags);

    smc_host->sdio_int = 0;
    if (smc_host->cd_mode == CARD_DETECT_BY_DATA3)
    {
        smc_host->change = 0;
    }

    sdxc_check_status(smc_host);

    if (smc_host->wait == SDC_WAIT_FINALIZE)
    {
        tasklet_schedule(&smc_host->tasklet);
    }

    spin_unlock_irqrestore(&smc_host->lock, iflags);

    /* sdio interrupt call */
    if (smc_host->sdio_int)
    {
        mmc_signal_sdio_irq(smc_host->mmc);
//    	SMC_MSG("- sdio int -\n");
    }

    /* card detect change */
    if (smc_host->cd_mode == CARD_DETECT_BY_DATA3)
    {
        if (smc_host->change)
        {
            mmc_detect_change(smc_host->mmc, msecs_to_jiffies(300));
        }
    }

    return IRQ_HANDLED;
}

static void sunximmc_tasklet(unsigned long data)
{
    struct sunxi_mmc_host *smc_host = (struct sunxi_mmc_host *) data;

	sdxc_int_disable(smc_host);

	if (smc_host->pio_active == XFER_WRITE)
		sdxc_do_pio_write(smc_host);

	if (smc_host->pio_active == XFER_READ)
		sdxc_do_pio_read(smc_host);

    if (smc_host->wait == SDC_WAIT_FINALIZE)
    {
        sdxc_int_enable(smc_host);
        sunximmc_finalize_request(smc_host);
    }
    else
        sdxc_int_enable(smc_host);
}

static void sunximmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
    struct sunxi_mmc_host *smc_host = mmc_priv(mmc);
    s32 ret = -1;

    /* Set the power state */
    switch (ios->power_mode)
    {
        case MMC_POWER_ON:
        case MMC_POWER_UP:
            if (!smc_host->power_on)
            {
                SMC_MSG("mmc %d power on !!\n", smc_host->pdev->id);
                /* resume pins to correct status */
                sunximmc_resume_pins(smc_host);
            	/* enable mmc hclk */
            	clk_enable(smc_host->hclk);
            	/* enable mmc mclk */
            	clk_enable(smc_host->mclk);
                /* restore registers */
                sdxc_regs_restore(smc_host);
                sdxc_program_clk(smc_host);
                /* enable irq */
                enable_irq(smc_host->irq);
                smc_host->power_on = 1;
            }
        	break;
        case MMC_POWER_OFF:
            if (smc_host->power_on)
            {
                SMC_MSG("mmc %d power off !!\n", smc_host->pdev->id);
                /* disable irq */
                disable_irq(smc_host->irq);
                /* backup registers */
                sdxc_regs_save(smc_host);
            	/* disable mmc mclk */
            	clk_disable(smc_host->mclk);
            	/* disable mmc hclk */
            	clk_disable(smc_host->hclk);
                /* suspend pins to save power */
                sunximmc_suspend_pins(smc_host);
                smc_host->power_on = 0;
                smc_host->ferror = 0;
            }
        default:
        	break;
    }

    /* set clock */
    if (smc_host->power_on)
    {
        /* set clock */
        if (ios->clock)
        {
            smc_host->cclk = ios->clock;
            ret = sdxc_update_clk(smc_host, smc_host->mod_clk, smc_host->cclk);
            if (ret == -1) {
                SMC_ERR("Fatal error, please check your pin configuration.\n");
                smc_host->ferror = 1;
            }
            if ((ios->power_mode == MMC_POWER_ON) || (ios->power_mode == MMC_POWER_UP))
            {
            	SMC_DBG("running at %dkHz (requested: %dkHz).\n", smc_host->real_cclk/1000, ios->clock/1000);
            }
            else
            {
            	SMC_DBG("powered down.\n");
            }
        }

        /* set bus width */
        if (smc_host->bus_width != (1<<ios->bus_width))
        {
            sdxc_set_buswidth(smc_host, 1<<ios->bus_width);
            smc_host->bus_width = 1<<ios->bus_width;
        }
    }
}

static void sunximmc_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
    struct sunxi_mmc_host *smc_host = mmc_priv(mmc);
    unsigned long flags;

    spin_lock_irqsave(&smc_host->lock, flags);
    sdxc_enable_sdio_irq(smc_host, enable);
    spin_unlock_irqrestore(&smc_host->lock, flags);
}

static void sunximmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
    struct sunxi_mmc_host *smc_host = mmc_priv(mmc);

    smc_host->mrq = mrq;

    if (sunximmc_card_present(mmc) == 0
        || smc_host->ferror || !smc_host->power_on)
    {
    	SMC_DBG("no medium present, ferr %d, pwd %d\n",
    	        smc_host->ferror, smc_host->power_on);
    	smc_host->mrq->cmd->error = -ENOMEDIUM;
    	mmc_request_done(mmc, mrq);
    }
    else
    {
        sdxc_request(smc_host, mrq);
    }
}

void sunximmc_rescan_card(unsigned id, unsigned insert)
{
	struct sunxi_mmc_host *smc_host = NULL;
	if (id > 3) {
		pr_err("%s: card id more than 3.\n", __func__);
		return;
	}

	mutex_lock(&sw_host_rescan_mutex);
	smc_host = sw_host[id];
	if (!smc_host)
		sw_host_rescan_pending[id] = insert;
	mutex_unlock(&sw_host_rescan_mutex);
	if (!smc_host)
		return;

	smc_host->present = insert ? 1 : 0;
	mmc_detect_change(smc_host->mmc, 0);
	return;
}
EXPORT_SYMBOL_GPL(sunximmc_rescan_card);

int sunximmc_check_r1_ready(struct mmc_host *mmc)
{
    struct sunxi_mmc_host *smc_host = mmc_priv(mmc);
    return sdxc_check_r1_ready(smc_host);
}
EXPORT_SYMBOL_GPL(sunximmc_check_r1_ready);

static struct mmc_host_ops sunximmc_ops = {
    .request	     = sunximmc_request,
    .set_ios	     = sunximmc_set_ios,
    .get_ro		     = sunximmc_get_ro,
    .get_cd		     = sunximmc_card_present,
    .enable_sdio_irq = sunximmc_enable_sdio_irq
};

#ifdef CONFIG_MMC_SUNXI_POWER_CONTROL
extern int mmc_pm_io_shd_suspend_host(void);
#else
static inline int mmc_pm_io_shd_suspend_host(void) {return 1;}
#endif

static int __devinit sunximmc_probe(struct platform_device *pdev)
{
    struct sunxi_mmc_host *smc_host = NULL;
    struct mmc_host	*mmc = NULL;
    int ret = 0;
    char mmc_para[16] = {0};
    int card_detmode = 0;

    SMC_MSG("%s: pdev->name: %s, pdev->id: %d\n", dev_name(&pdev->dev), pdev->name, pdev->id);
    mmc = mmc_alloc_host(sizeof(struct sunxi_mmc_host), &pdev->dev);
    if (!mmc)
    {
        SMC_ERR("mmc alloc host failed\n");
    	ret = -ENOMEM;
    	goto probe_out;
    }

    smc_host = mmc_priv(mmc);
    memset((void*)smc_host, 0, sizeof(smc_host));
    smc_host->mmc = mmc;
    smc_host->pdev = pdev;

    spin_lock_init(&smc_host->lock);
    tasklet_init(&smc_host->tasklet, sunximmc_tasklet, (unsigned long) smc_host);

    smc_host->cclk  = 400000;
    smc_host->mod_clk = SMC_MAX_MOD_CLOCK(pdev->id);
    smc_host->clk_source = SMC_MOD_CLK_SRC(pdev->id);

    mmc->ops        = &sunximmc_ops;
    mmc->ocr_avail	= MMC_VDD_32_33 | MMC_VDD_33_34;
    mmc->caps	    = MMC_CAP_4_BIT_DATA|MMC_CAP_MMC_HIGHSPEED|MMC_CAP_SD_HIGHSPEED|MMC_CAP_SDIO_IRQ;
    mmc->f_min 	    = 400000;
    mmc->f_max      = SMC_MAX_IO_CLOCK(pdev->id);
#ifdef MMC_PM_IGNORE_PM_NOTIFY
    if (pdev->id==3 && !mmc_pm_io_shd_suspend_host())
        mmc->pm_flags = MMC_PM_IGNORE_PM_NOTIFY;
#endif

    mmc->max_blk_count	= 4095;
    mmc->max_blk_size	= 4095;
    mmc->max_req_size	= 4095 * 512;              //32bit byte counter = 2^32 - 1
    mmc->max_seg_size	= mmc->max_req_size;
    mmc->max_segs	    = 256;

    if (sunximmc_resource_request(smc_host))
    {
        SMC_ERR("%s: Failed to get resouce.\n", dev_name(&pdev->dev));
        goto probe_free_host;
    }

    if (sunximmc_set_src_clk(smc_host))
    {
        goto probe_free_host;
    }
    sunximmc_init_controller(smc_host);
    smc_host->power_on = 1;
    sunximmc_procfs_attach(smc_host);

    /* irq */
    smc_host->irq = platform_get_irq(pdev, 0);
    if (smc_host->irq == 0)
    {
    	dev_err(&pdev->dev, "Failed to get interrupt resouce.\n");
    	ret = -EINVAL;
    	goto probe_free_resource;
    }

    if (request_irq(smc_host->irq, sunximmc_irq, 0, DRIVER_NAME, smc_host))
    {
    	dev_err(&pdev->dev, "Failed to request smc card interrupt.\n");
    	ret = -ENOENT;
    	goto probe_free_irq;
    }
    disable_irq(smc_host->irq);

    /* add host */
    ret = mmc_add_host(mmc);
    if (ret)
    {
    	dev_err(&pdev->dev, "Failed to add mmc host.\n");
    	goto probe_free_irq;
    }
    platform_set_drvdata(pdev, mmc);

    //fetch card detecetd mode
    sprintf(mmc_para, "mmc%d_para", pdev->id);
    ret = script_parser_fetch(mmc_para, "sdc_detmode", &card_detmode, sizeof(int));
    if (ret)
    {
    	SMC_ERR("sdc fetch card detect mode failed\n");
    }

    smc_host->cd_mode = card_detmode;
    if (smc_host->cd_mode == CARD_DETECT_BY_GPIO)
    {
        //initial card detect timer
        init_timer(&smc_host->cd_timer);
        smc_host->cd_timer.expires = jiffies + 1*HZ;
        smc_host->cd_timer.function = &sunximmc_cd_timer;
        smc_host->cd_timer.data = (unsigned long)smc_host;
        add_timer(&smc_host->cd_timer);
        smc_host->present = 0;
    }

    enable_irq(smc_host->irq);

	mutex_lock(&sw_host_rescan_mutex);
	if (smc_host->cd_mode == CARD_ALWAYS_PRESENT ||
	    sw_host_rescan_pending[pdev->id]) {
		smc_host->present = 1;
        mmc_detect_change(smc_host->mmc, msecs_to_jiffies(300));
    }

    sw_host[pdev->id] = smc_host;
	mutex_unlock(&sw_host_rescan_mutex);

    SMC_MSG("mmc%d Probe: base:0x%p irq:%u dma:%u pdes:0x%p, ret %d.\n",
            pdev->id, smc_host->smc_base, smc_host->irq, smc_host->dma_no, smc_host->pdes, ret);

    goto probe_out;

probe_free_irq:
    if (smc_host->irq)
    {
        free_irq(smc_host->irq, smc_host);
    }

probe_free_resource:
    sunximmc_resource_release(smc_host);

probe_free_host:
    mmc_free_host(mmc);

probe_out:
    return ret;
}

static void sunximmc_shutdown(struct platform_device *pdev)
{
    struct mmc_host    *mmc = platform_get_drvdata(pdev);
    struct sunxi_mmc_host *smc_host = mmc_priv(mmc);

    SMC_MSG("%s: ShutDown.\n", dev_name(&pdev->dev));

    sunximmc_procfs_remove(smc_host);
    mmc_remove_host(mmc);
}

static int __devexit sunximmc_remove(struct platform_device *pdev)
{
    struct mmc_host    	*mmc  = platform_get_drvdata(pdev);
    struct sunxi_mmc_host	*smc_host = mmc_priv(mmc);

    SMC_MSG("%s: Remove.\n", dev_name(&pdev->dev));

	sdxc_exit(smc_host);

	sunximmc_shutdown(pdev);

    //dma
    tasklet_disable(&smc_host->tasklet);

    //irq
    free_irq(smc_host->irq, smc_host);

    if (smc_host->cd_mode == CARD_DETECT_BY_GPIO)
    {
        del_timer(&smc_host->cd_timer);
    }

    sunximmc_resource_release(smc_host);

    mmc_free_host(mmc);
    sw_host[pdev->id] = NULL;

    return 0;
}

#ifdef CONFIG_PM
static int sunximmc_suspend(struct device *dev)
{
    struct platform_device *pdev = to_platform_device(dev);
    struct mmc_host *mmc = platform_get_drvdata(pdev);
    int ret = 0;

    if (mmc)
    {
        struct sunxi_mmc_host *smc_host = mmc_priv(mmc);

        if (mmc->card && (mmc->card->type!=MMC_TYPE_SDIO || mmc_pm_io_shd_suspend_host()))
            ret = mmc_suspend_host(mmc);

        if (smc_host->power_on) {
            /* disable irq */
            disable_irq(smc_host->irq);

            /* backup registers */
            sdxc_regs_save(smc_host);

        	/* disable mmc mclk */
        	clk_disable(smc_host->mclk);

        	/* disable mmc hclk */
            if (mmc->card && mmc->card->type!=MMC_TYPE_SDIO)
        	    clk_disable(smc_host->hclk);

            /* suspend pins to save power */
            sunximmc_suspend_pins(smc_host);
        }
    }

    SMC_DBG("smc %d suspend\n", pdev->id);
    return ret;
}

static int sunximmc_resume(struct device *dev)
{
    struct platform_device *pdev = to_platform_device(dev);
    struct mmc_host *mmc = platform_get_drvdata(pdev);
    int ret = 0;

    if (mmc)
    {
        struct sunxi_mmc_host *smc_host = mmc_priv(mmc);

        if (smc_host->power_on) {
            /* resume pins to correct status */
            sunximmc_resume_pins(smc_host);

        	/* enable mmc hclk */
            if (mmc->card && mmc->card->type!=MMC_TYPE_SDIO)
        	    clk_enable(smc_host->hclk);

        	/* enable mmc mclk */
        	clk_enable(smc_host->mclk);

            /* restore registers */
            if (mmc->card && mmc->card->type!=MMC_TYPE_SDIO)
                sdxc_regs_restore(smc_host);
            sdxc_program_clk(smc_host);

            /* enable irq */
            enable_irq(smc_host->irq);
        }

        if (mmc->card && (mmc->card->type!=MMC_TYPE_SDIO || mmc_pm_io_shd_suspend_host()))
            ret = mmc_resume_host(mmc);
    }

    SMC_DBG("smc %d resume\n", pdev->id);
    return ret;
}

static const struct dev_pm_ops sunximmc_pm = {
    .suspend	= sunximmc_suspend,
    .resume		= sunximmc_resume,
};
#define sunximmc_pm_ops &sunximmc_pm

#else /* CONFIG_PM */

#define sunximmc_pm_ops NULL

#endif /* CONFIG_PM */

static struct resource sunximmc_resources[SUNXI_MMC_HOST_NUM][2] = {
    #if (SUNXI_MMC_USED_CTRL & SUNXI_MMC0_USED)
        {/* mmc0 */
            { .start	= SMC_BASE(0),      .end = SMC_BASE(0)+0x1000-1,  .flags	= IORESOURCE_MEM},  /* reg resource */
            { .start	= INTC_IRQNO_SMC0,  .end = INTC_IRQNO_SMC0,     .flags	= IORESOURCE_IRQ},  /* irq resource */
        },
    #endif
    #if (SUNXI_MMC_USED_CTRL & SUNXI_MMC1_USED)
        {/* mmc1 */
            { .start	= SMC_BASE(1),      .end = SMC_BASE(1)+0x1000-1,  .flags	= IORESOURCE_MEM},  /* reg resource */
            { .start	= INTC_IRQNO_SMC1,  .end = INTC_IRQNO_SMC1,     .flags	= IORESOURCE_IRQ},  /* irq resource */
        },
    #endif
    #if (SUNXI_MMC_USED_CTRL & SUNXI_MMC2_USED)
        {/* mmc2 */
            { .start	= SMC_BASE(2),      .end = SMC_BASE(2)+0x1000-1,  .flags	= IORESOURCE_MEM},  /* reg resource */
            { .start	= INTC_IRQNO_SMC2,  .end = INTC_IRQNO_SMC2,     .flags	= IORESOURCE_IRQ},  /* irq resource */
        },
    #endif
    #if (SUNXI_MMC_USED_CTRL & SUNXI_MMC3_USED)
        {/* mmc3 */
            { .start	= SMC_BASE(3),      .end = SMC_BASE(3)+0x1000-1,  .flags	= IORESOURCE_MEM},  /* reg resource */
            { .start	= INTC_IRQNO_SMC3,  .end = INTC_IRQNO_SMC3,     .flags	= IORESOURCE_IRQ},  /* irq resource */
        },
    #endif
};

static struct platform_device awmmc_device[SUNXI_MMC_HOST_NUM] = {
    #if (SUNXI_MMC_USED_CTRL & SUNXI_MMC0_USED)
    [0] = {.name = DRIVER_NAME, .id = 0, .num_resources	= ARRAY_SIZE(sunximmc_resources[0]), .resource = &sunximmc_resources[0][0], .dev = {}},
    #endif
    #if (SUNXI_MMC_USED_CTRL & SUNXI_MMC1_USED)
    [1] = {.name = DRIVER_NAME, .id = 1, .num_resources	= ARRAY_SIZE(sunximmc_resources[1]), .resource = &sunximmc_resources[1][0], .dev = {}},
    #endif
    #if (SUNXI_MMC_USED_CTRL & SUNXI_MMC2_USED)
    [2] = {.name = DRIVER_NAME, .id = 2, .num_resources	= ARRAY_SIZE(sunximmc_resources[2]), .resource = &sunximmc_resources[2][0], .dev = {}},
    #endif
    #if (SUNXI_MMC_USED_CTRL & SUNXI_MMC3_USED)
    [3] = {.name = DRIVER_NAME, .id = 3, .num_resources	= ARRAY_SIZE(sunximmc_resources[3]), .resource = &sunximmc_resources[3][0], .dev = {}},
    #endif
};

static struct platform_driver sunximmc_driver = {
    .driver.name    = DRIVER_NAME,
    .driver.owner   = THIS_MODULE,
    .driver.pm	    = sunximmc_pm_ops,
    .probe          = sunximmc_probe,
    .remove         = __devexit_p(sunximmc_remove),
};

static int __init sunximmc_init(void)
{
    int ret;
    int i;
    char mmc_para[16] = {0};
    int used = 0;

    SMC_MSG("sunximmc_init\n");
    for (i=0; i<SUNXI_MMC_HOST_NUM; i++)
    {
        memset(mmc_para, 0, sizeof(mmc_para));
        sprintf(mmc_para, "mmc%d_para", i);
        used = 0;

        ret = script_parser_fetch(mmc_para,"sdc_used", &used, sizeof(int));
        if (ret)
        {
        	printk("sunximmc_init fetch mmc%d using configuration failed\n", i);
        }

        if (used)
        {
            sdc_used |= 1 << i;
            platform_device_register(&awmmc_device[i]);
        }

    }

    SMC_MSG("sunxi mmc controller using config : 0x%x\n", sdc_used);

    if (sdc_used)
    {
        return platform_driver_register(&sunximmc_driver);
    }
    else
    {
        SMC_ERR("cannot find any using configuration for controllers, return directly!\n");
        return 0;
    }
}

static void __exit sunximmc_exit(void)
{
    if (sdc_used)
    {
        sdc_used = 0;
        platform_driver_unregister(&sunximmc_driver);
    }
}


module_init(sunximmc_init);
module_exit(sunximmc_exit);

MODULE_DESCRIPTION("Winner's SD/MMC Card Controller Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Aaron.maoye<leafy.myeh@allwinnertech.com>");
MODULE_ALIAS("platform:sunxi-mmc");

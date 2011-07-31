/* Copyright (c) 2008-2009, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <mach/hardware.h>
#include <asm/io.h>

#include <asm/system.h>
#include <asm/mach-types.h>
#include <linux/semaphore.h>
#include <linux/uaccess.h>
#include <linux/clk.h>
#include <linux/platform_device.h>

#include "msm_fb.h"
#include "mddihosti.h"
#include "mddihost.h"
#include <mach/gpio.h>
#include <mach/clk.h>

static int mddi_probe(struct platform_device *pdev);
static int mddi_remove(struct platform_device *pdev);

static int mddi_off(struct platform_device *pdev);
static int mddi_on(struct platform_device *pdev);

static int mddi_suspend(struct platform_device *pdev, pm_message_t state);
static int mddi_resume(struct platform_device *pdev);

#ifdef CONFIG_HAS_EARLYSUSPEND
static void mddi_early_suspend(struct early_suspend *h);
static void mddi_early_resume(struct early_suspend *h);
#endif

static struct platform_device *pdev_list[MSM_FB_MAX_DEV_LIST];
static int pdev_list_cnt;
static struct clk *mddi_clk;
static struct clk *mddi_pclk;
static struct mddi_platform_data *mddi_pdata;

static struct platform_driver mddi_driver = {
	.probe = mddi_probe,
	.remove = mddi_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
#ifdef CONFIG_PM
	.suspend = mddi_suspend,
	.resume = mddi_resume,
#endif
#endif
	.suspend_late = NULL,
	.resume_early = NULL,
	.shutdown = NULL,
	.driver = {
		   .name = "mddi",
		   },
};

extern int int_mddi_pri_flag;

static int mddi_off(struct platform_device *pdev)
{
	int ret = 0;

	ret = panel_next_off(pdev);

	if (mddi_pdata && mddi_pdata->mddi_power_save)
		mddi_pdata->mddi_power_save(0);

	return ret;
}

static int mddi_on(struct platform_device *pdev)
{
	int ret = 0;
	u32 clk_rate;
	struct msm_fb_data_type *mfd;

	mfd = platform_get_drvdata(pdev);

	if (mddi_pdata && mddi_pdata->mddi_power_save)
		mddi_pdata->mddi_power_save(1);

	clk_rate = mfd->fbi->var.pixclock;
	clk_rate = min(clk_rate, mfd->panel_info.clk_max);

	if (mddi_pdata &&
	    mddi_pdata->mddi_sel_clk &&
	    mddi_pdata->mddi_sel_clk(&clk_rate))
			printk(KERN_ERR
			  "%s: can't select mddi io clk targate rate = %d\n",
			  __func__, clk_rate);

	if (clk_set_min_rate(mddi_clk, clk_rate) < 0)
		printk(KERN_ERR "%s: clk_set_min_rate failed\n",
			__func__);

	ret = panel_next_on(pdev);

	return ret;
}

static int mddi_resource_initialized;

static int mddi_probe(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;
	struct platform_device *mdp_dev = NULL;
	struct msm_fb_panel_data *pdata = NULL;
	int rc;
	resource_size_t size ;
	u32 clk_rate;

	if ((pdev->id == 0) && (pdev->num_resources >= 0)) {
		mddi_pdata = pdev->dev.platform_data;

		size =  resource_size(&pdev->resource[0]);
		msm_pmdh_base =  ioremap(pdev->resource[0].start, size);

		MSM_FB_INFO("primary mddi base phy_addr = 0x%x virt = 0x%x\n",
				pdev->resource[0].start, (int) msm_pmdh_base);

		if (unlikely(!msm_pmdh_base))
			return -ENOMEM;

		if (mddi_pdata && mddi_pdata->mddi_power_save)
			mddi_pdata->mddi_power_save(1);

		mddi_resource_initialized = 1;
		return 0;
	}

	if (!mddi_resource_initialized)
		return -EPERM;

	mfd = platform_get_drvdata(pdev);

	if (!mfd)
		return -ENODEV;

	if (mfd->key != MFD_KEY)
		return -EINVAL;

	if (pdev_list_cnt >= MSM_FB_MAX_DEV_LIST)
		return -ENOMEM;

	mdp_dev = platform_device_alloc("mdp", pdev->id);
	if (!mdp_dev)
		return -ENOMEM;

	/*
	 * link to the latest pdev
	 */
	mfd->pdev = mdp_dev;
	mfd->dest = DISPLAY_LCD;

	/*
	 * alloc panel device data
	 */
	if (platform_device_add_data
	    (mdp_dev, pdev->dev.platform_data,
	     sizeof(struct msm_fb_panel_data))) {
		printk(KERN_ERR "mddi_probe: platform_device_add_data failed!\n");
		platform_device_put(mdp_dev);
		return -ENOMEM;
	}
	/*
	 * data chain
	 */
	pdata = mdp_dev->dev.platform_data;
	pdata->on = mddi_on;
	pdata->off = mddi_off;
	pdata->next = pdev;

	/*
	 * get/set panel specific fb info
	 */
	mfd->panel_info = pdata->panel_info;
	mfd->fb_imgType = MDP_RGB_565;

	clk_rate = mfd->panel_info.clk_max;
	if (mddi_pdata &&
	    mddi_pdata->mddi_sel_clk &&
	    mddi_pdata->mddi_sel_clk(&clk_rate))
			printk(KERN_ERR
			  "%s: can't select mddi io clk targate rate = %d\n",
			  __func__, clk_rate);

	if (clk_set_max_rate(mddi_clk, clk_rate) < 0)
		printk(KERN_ERR "%s: clk_set_max_rate failed\n", __func__);
	mfd->panel_info.clk_rate = mfd->panel_info.clk_min;

	/*
	 * set driver data
	 */
	platform_set_drvdata(mdp_dev, mfd);

	/*
	 * register in mdp driver
	 */
	rc = platform_device_add(mdp_dev);
	if (rc)
		goto mddi_probe_err;

	pdev_list[pdev_list_cnt++] = pdev;

#ifdef CONFIG_HAS_EARLYSUSPEND
	mfd->mddi_early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB;
	mfd->mddi_early_suspend.suspend = mddi_early_suspend;
	mfd->mddi_early_suspend.resume = mddi_early_resume;
	register_early_suspend(&mfd->mddi_early_suspend);
#endif

	return 0;

mddi_probe_err:
	platform_device_put(mdp_dev);
	return rc;
}

static int mddi_pad_ctrl;
static int mddi_power_locked;
static int mddi_is_in_suspend;

void mddi_disable(int lock)
{
	mddi_host_type host_idx = MDDI_HOST_PRIM;

	if (mddi_power_locked)
		return;

	if (lock)
		mddi_power_locked = 1;

	if (mddi_host_timer.function)
		del_timer_sync(&mddi_host_timer);

	mddi_pad_ctrl = mddi_host_reg_in(PAD_CTL);
	mddi_host_reg_out(PAD_CTL, 0x0);

	if (clk_set_min_rate(mddi_clk, 0) < 0)
		printk(KERN_ERR "%s: clk_set_min_rate failed\n", __func__);

	clk_disable(mddi_clk);
	if (mddi_pclk)
		clk_disable(mddi_pclk);
	disable_irq(INT_MDDI_PRI);

	if (mddi_pdata && mddi_pdata->mddi_power_save)
		mddi_pdata->mddi_power_save(0);
}

static int mddi_suspend(struct platform_device *pdev, pm_message_t state)
{
	if (mddi_is_in_suspend)
		return 0;

	mddi_is_in_suspend = 1;
	mddi_disable(0);
	return 0;
}

static int mddi_resume(struct platform_device *pdev)
{
	mddi_host_type host_idx = MDDI_HOST_PRIM;

	if (!mddi_is_in_suspend)
		return 0;

	mddi_is_in_suspend = 0;

	if (mddi_power_locked)
		return 0;

	enable_irq(INT_MDDI_PRI);
	clk_enable(mddi_clk);
	if (mddi_pclk)
		clk_enable(mddi_pclk);
	mddi_host_reg_out(PAD_CTL, mddi_pad_ctrl);

	if (mddi_host_timer.function)
		mddi_host_timer_service(0);

	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void mddi_early_suspend(struct early_suspend *h)
{
	pm_message_t state;
	struct msm_fb_data_type *mfd = container_of(h, struct msm_fb_data_type,
							mddi_early_suspend);

	state.event = PM_EVENT_SUSPEND;
	mddi_suspend(mfd->pdev, state);
}

static void mddi_early_resume(struct early_suspend *h)
{
	struct msm_fb_data_type *mfd = container_of(h, struct msm_fb_data_type,
							mddi_early_suspend);
	mddi_resume(mfd->pdev);
}
#endif

static int mddi_remove(struct platform_device *pdev)
{
	if (mddi_host_timer.function)
		del_timer_sync(&mddi_host_timer);

	iounmap(msm_pmdh_base);

	return 0;
}

static int mddi_register_driver(void)
{
	return platform_driver_register(&mddi_driver);
}

static int __init mddi_driver_init(void)
{
	int ret;

	mddi_clk = clk_get(NULL, "mddi_clk");
	if (IS_ERR(mddi_clk)) {
		printk(KERN_ERR "can't find mddi_clk \n");
		return PTR_ERR(mddi_clk);
	}
	clk_enable(mddi_clk);

	mddi_pclk = clk_get(NULL, "mddi_pclk");
	if (IS_ERR(mddi_pclk))
		mddi_pclk = NULL;
	else
		clk_enable(mddi_pclk);

	ret = mddi_register_driver();
	if (ret) {
		clk_disable(mddi_clk);
		clk_put(mddi_clk);
		if (mddi_pclk) {
			clk_disable(mddi_pclk);
			clk_put(mddi_pclk);
		}
		printk(KERN_ERR "mddi_register_driver() failed!\n");
		return ret;
	}

	mddi_init();

	return ret;
}

module_init(mddi_driver_init);

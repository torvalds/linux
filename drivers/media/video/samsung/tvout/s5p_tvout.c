/* linux/drivers/media/video/samsung/tvout/s5p_tvout.c
 *
 * Copyright (c) 2009 Samsung Electronics
 *		http://www.samsung.com/
 *
 * Entry file for Samsung TVOut driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mm.h>

#if defined(CONFIG_S5P_SYSMMU_TV)
#include <plat/sysmmu.h>
#endif

#include "s5p_tvout_common_lib.h"
#include "s5p_tvout_ctrl.h"
#include "s5p_tvout_fb.h"
#include "s5p_tvout_v4l2.h"

#define TV_CLK_GET_WITH_ERR_CHECK(clk, pdev, clk_name)			\
		do {							\
			clk = clk_get(&pdev->dev, clk_name);		\
			if (IS_ERR(clk)) {				\
				printk(KERN_ERR				\
				"failed to find clock %s\n", clk_name);	\
				return -ENOENT;				\
			}						\
		} while (0);

struct s5p_tvout_status s5ptv_status;
bool on_stop_process;
bool on_start_process;
#ifdef CONFIG_PM
static struct workqueue_struct *tvout_resume_wq;
struct work_struct      tvout_resume_work;
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
static struct early_suspend    s5ptv_early_suspend;
static DEFINE_MUTEX(s5p_tvout_mutex);
unsigned int suspend_status;
static void s5p_tvout_early_suspend(struct early_suspend *h);
static void s5p_tvout_late_resume(struct early_suspend *h);
#endif

static int __devinit s5p_tvout_clk_get(struct platform_device *pdev,
					struct s5p_tvout_status *ctrl)
{
	struct clk	*ext_xtal_clk,
			*mout_vpll_src,
			*fout_vpll,
			*mout_vpll;

	TV_CLK_GET_WITH_ERR_CHECK(ctrl->i2c_phy_clk,	pdev, "i2c-hdmiphy");

	TV_CLK_GET_WITH_ERR_CHECK(ctrl->sclk_dac,	pdev, "sclk_dac");
	TV_CLK_GET_WITH_ERR_CHECK(ctrl->sclk_hdmi,	pdev, "sclk_hdmi");

	TV_CLK_GET_WITH_ERR_CHECK(ctrl->sclk_pixel,	pdev, "sclk_pixel");
	TV_CLK_GET_WITH_ERR_CHECK(ctrl->sclk_hdmiphy,	pdev, "sclk_hdmiphy");

	TV_CLK_GET_WITH_ERR_CHECK(ext_xtal_clk,		pdev, "ext_xtal");
	TV_CLK_GET_WITH_ERR_CHECK(mout_vpll_src,	pdev, "vpll_src");
	TV_CLK_GET_WITH_ERR_CHECK(fout_vpll,		pdev, "fout_vpll");
	TV_CLK_GET_WITH_ERR_CHECK(mout_vpll,		pdev, "sclk_vpll");

#ifdef CONFIG_VPLL_USE_FOR_TVENC
	if (clk_set_rate(fout_vpll, 54000000)) {
		tvout_err("%s rate change failed: %lu\n", fout_vpll->name, 54000000);
		return -1;
	}

	if (clk_set_parent(mout_vpll_src, ext_xtal_clk)) {
		tvout_err("unable to set parent %s of clock %s.\n",
			   ext_xtal_clk->name, mout_vpll_src->name);
		return -1;
	}

	if (clk_set_parent(mout_vpll, fout_vpll)) {
		tvout_err("unable to set parent %s of clock %s.\n",
			   fout_vpll->name, mout_vpll->name);
		return -1;
	}

	/* sclk_dac's parent is fixed as mout_vpll */
	if (clk_set_parent(ctrl->sclk_dac, mout_vpll)) {
		tvout_err("unable to set parent %s of clock %s.\n",
			   mout_vpll->name, ctrl->sclk_dac->name);
		return -1;
	}

	/* It'll be moved in the future */
	if (clk_enable(mout_vpll_src) < 0)
		return -1;

	if (clk_enable(fout_vpll) < 0)
		return -1;

	if (clk_enable(mout_vpll) < 0)
		return -1;

	clk_put(ext_xtal_clk);
	clk_put(mout_vpll_src);
	clk_put(fout_vpll);
	clk_put(mout_vpll);
#endif

	return 0;
}


static int __devinit s5p_tvout_probe(struct platform_device *pdev)
{
	s5p_tvout_pm_runtime_enable(&pdev->dev);

#if defined(CONFIG_S5P_SYSMMU_TV) && defined(CONFIG_VCM)
	if (s5p_tvout_vcm_create_unified() < 0)
		goto err;

	if (s5p_tvout_vcm_init() < 0)
		goto err;
#elif defined(CONFIG_S5P_SYSMMU_TV) && defined(CONFIG_S5P_VMEM)
	s5p_sysmmu_enable(&pdev->dev);
	printk("sysmmu on\n");
	s5p_sysmmu_set_tablebase_pgd(&pdev->dev, __pa(swapper_pg_dir));
#endif
	if (s5p_tvout_clk_get(pdev, &s5ptv_status) < 0)
		goto err;

	if (s5p_vp_ctrl_constructor(pdev) < 0)
		goto err;

	/* s5p_mixer_ctrl_constructor must be called
		before s5p_tvif_ctrl_constructor */
	if (s5p_mixer_ctrl_constructor(pdev) < 0)
		goto err;

	if (s5p_tvif_ctrl_constructor(pdev) < 0)
		goto err;

	if (s5p_tvout_v4l2_constructor(pdev) < 0)
		goto err;

#ifdef CONFIG_HAS_EARLYSUSPEND
	spin_lock_init(&s5ptv_status.tvout_lock);
	s5ptv_early_suspend.suspend = s5p_tvout_early_suspend;
	s5ptv_early_suspend.resume = s5p_tvout_late_resume;
	s5ptv_early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB - 4;
	register_early_suspend(&s5ptv_early_suspend);
	suspend_status = 0;
#endif

#ifdef CONFIG_TV_FB
#ifndef CONFIG_USER_ALLOC_TVOUT
	s5p_hdmi_phy_power(true);
	if (s5p_tvif_ctrl_start(TVOUT_720P_60, TVOUT_HDMI) < 0)
		goto err;
#endif

	/* prepare memory */
	if (s5p_tvout_fb_alloc_framebuffer(&pdev->dev))
		goto err;

	if (s5p_tvout_fb_register_framebuffer(&pdev->dev))
		goto err;
#endif
	on_stop_process = false;
	on_start_process = false;

	return 0;

err:
	return -ENODEV;
}

static int s5p_tvout_remove(struct platform_device *pdev)
{
#if defined(CONFIG_S5P_SYSMMU_TV) && defined(CONFIG_S5P_VMEM)
	s5p_sysmmu_off(&pdev->dev);
	tvout_dbg("sysmmu off\n");
#endif
	s5p_vp_ctrl_destructor();
	s5p_tvif_ctrl_destructor();
	s5p_mixer_ctrl_destructor();

	s5p_tvout_v4l2_destructor();

	clk_disable(s5ptv_status.sclk_hdmi);

	clk_put(s5ptv_status.sclk_hdmi);
	clk_put(s5ptv_status.sclk_dac);
	clk_put(s5ptv_status.sclk_pixel);
	clk_put(s5ptv_status.sclk_hdmiphy);

	s5p_tvout_pm_runtime_disable(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM
#ifdef CONFIG_HAS_EARLYSUSPEND
static void s5p_tvout_early_suspend(struct early_suspend *h)
{
	tvout_dbg("\n");
	mutex_lock(&s5p_tvout_mutex);
	s5p_vp_ctrl_suspend();
	s5p_mixer_ctrl_suspend();
	s5p_tvif_ctrl_suspend();
	suspend_status = 1;
	tvout_dbg("suspend_status is true\n");
	mutex_unlock(&s5p_tvout_mutex);

	return;
}

static void s5p_tvout_late_resume(struct early_suspend *h)
{
	tvout_dbg("\n");
	mutex_lock(&s5p_tvout_mutex);
	suspend_status = 0;
	tvout_dbg("suspend_status is false\n");
	s5p_tvif_ctrl_resume();
	s5p_mixer_ctrl_resume();
	s5p_vp_ctrl_resume();
	mutex_unlock(&s5p_tvout_mutex);

	return;
}

void s5p_tvout_mutex_lock()
{
	mutex_lock(&s5p_tvout_mutex);
}

void s5p_tvout_mutex_unlock()
{
	mutex_unlock(&s5p_tvout_mutex);
}
#endif

static void s5p_tvout_resume_work(void *arg)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
	mutex_lock(&s5p_tvout_mutex);
#endif
	s5p_hdmi_ctrl_phy_power_resume();
#ifdef CONFIG_HAS_EARLYSUSPEND
	mutex_unlock(&s5p_tvout_mutex);
#endif
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static int s5p_tvout_suspend(struct device *dev)
{
	tvout_dbg("\n");
	return 0;
}
static int s5p_tvout_resume(struct device *dev)
{
	tvout_dbg("\n");
	queue_work_on(0, tvout_resume_wq, &tvout_resume_work);
	return 0;
}
#else
static int s5p_tvout_suspend(struct device *dev)
{
	s5p_vp_ctrl_suspend();
	s5p_mixer_ctrl_suspend();
	s5p_tvif_ctrl_suspend();
	return 0;
}
static int s5p_tvout_resume(struct device *dev)
{
	s5p_tvif_ctrl_resume();
	s5p_mixer_ctrl_resume();
	s5p_vp_ctrl_resume();
	return 0;
}
#endif
static int s5p_tvout_runtime_suspend(struct device *dev)
{
	tvout_dbg("\n");
	return 0;
}

static int s5p_tvout_runtime_resume(struct device *dev)
{
	tvout_dbg("\n");
	return 0;
}
#else
#define s5p_tvout_suspend		NULL
#define s5p_tvout_resume		NULL
#define s5p_tvout_runtime_suspend	NULL
#define s5p_tvout_runtime_resume	NULL
#endif

static const struct dev_pm_ops s5p_tvout_pm_ops = {
	.suspend		= s5p_tvout_suspend,
	.resume			= s5p_tvout_resume,
	.runtime_suspend	= s5p_tvout_runtime_suspend,
	.runtime_resume		= s5p_tvout_runtime_resume
};

static struct platform_driver s5p_tvout_driver = {
	.probe	= s5p_tvout_probe,
	.remove	= s5p_tvout_remove,
	.driver	= {
		.name	= "s5p-tvout",
		.owner	= THIS_MODULE,
		.pm	= &s5p_tvout_pm_ops
	},
};

static char banner[] __initdata =
	KERN_INFO "S5P TVOUT Driver v3.0 (c) 2010 Samsung Electronics\n";

static int __init s5p_tvout_init(void)
{
	int ret;

	printk(banner);

	ret = platform_driver_register(&s5p_tvout_driver);

	if (ret) {
		printk(KERN_ERR "Platform Device Register Failed %d\n", ret);

		return -1;
	}
#ifdef CONFIG_PM
	tvout_resume_wq = create_freezable_workqueue("tvout resume work");
	if (!tvout_resume_wq) {
		printk(KERN_ERR "Platform Device Register Failed %d\n", ret);
		platform_driver_unregister(&s5p_tvout_driver);
		return -1;
	}

	INIT_WORK(&tvout_resume_work, (work_func_t) s5p_tvout_resume_work);
#endif

	return 0;
}

static void __exit s5p_tvout_exit(void)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
	mutex_destroy(&s5p_tvout_mutex);
#endif
	platform_driver_unregister(&s5p_tvout_driver);
}

late_initcall(s5p_tvout_init);
module_exit(s5p_tvout_exit);

MODULE_AUTHOR("SangPil Moon");
MODULE_DESCRIPTION("S5P TVOUT driver");
MODULE_LICENSE("GPL");

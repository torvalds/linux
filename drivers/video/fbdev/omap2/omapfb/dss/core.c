// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/drivers/video/omap2/dss/core.c
 *
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * Some code and ideas taken from drivers/video/omap/ driver
 * by Imre Deak.
 */

#define DSS_SUBSYS_NAME "CORE"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/regulator/consumer.h>
#include <linux/suspend.h>
#include <linux/slab.h>

#include <video/omapfb_dss.h>

#include "dss.h"
#include "dss_features.h"

static struct {
	struct platform_device *pdev;

	const char *default_display_name;
} core;

static char *def_disp_name;
module_param_named(def_disp, def_disp_name, charp, 0);
MODULE_PARM_DESC(def_disp, "default display name");

const char *omapdss_get_default_display_name(void)
{
	return core.default_display_name;
}
EXPORT_SYMBOL(omapdss_get_default_display_name);

enum omapdss_version omapdss_get_version(void)
{
	struct omap_dss_board_info *pdata = core.pdev->dev.platform_data;
	return pdata->version;
}
EXPORT_SYMBOL(omapdss_get_version);

struct platform_device *dss_get_core_pdev(void)
{
	return core.pdev;
}

int dss_dsi_enable_pads(int dsi_id, unsigned lane_mask)
{
	struct omap_dss_board_info *board_data = core.pdev->dev.platform_data;

	if (!board_data->dsi_enable_pads)
		return -ENOENT;

	return board_data->dsi_enable_pads(dsi_id, lane_mask);
}

void dss_dsi_disable_pads(int dsi_id, unsigned lane_mask)
{
	struct omap_dss_board_info *board_data = core.pdev->dev.platform_data;

	if (!board_data->dsi_disable_pads)
		return;

	return board_data->dsi_disable_pads(dsi_id, lane_mask);
}

int dss_set_min_bus_tput(struct device *dev, unsigned long tput)
{
	struct omap_dss_board_info *pdata = core.pdev->dev.platform_data;

	if (pdata->set_min_bus_tput)
		return pdata->set_min_bus_tput(dev, tput);
	else
		return 0;
}

#if defined(CONFIG_FB_OMAP2_DSS_DEBUGFS)
static int dss_show(struct seq_file *s, void *unused)
{
	void (*func)(struct seq_file *) = s->private;
	func(s);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(dss);

static struct dentry *dss_debugfs_dir;

static void dss_initialize_debugfs(void)
{
	dss_debugfs_dir = debugfs_create_dir("omapdss", NULL);

	debugfs_create_file("clk", S_IRUGO, dss_debugfs_dir,
			&dss_debug_dump_clocks, &dss_fops);
}

static void dss_uninitialize_debugfs(void)
{
	debugfs_remove_recursive(dss_debugfs_dir);
}

void dss_debugfs_create_file(const char *name, void (*write)(struct seq_file *))
{
	debugfs_create_file(name, S_IRUGO, dss_debugfs_dir, write, &dss_fops);
}
#else /* CONFIG_FB_OMAP2_DSS_DEBUGFS */
static inline void dss_initialize_debugfs(void)
{
}
static inline void dss_uninitialize_debugfs(void)
{
}
void dss_debugfs_create_file(const char *name, void (*write)(struct seq_file *))
{
}
#endif /* CONFIG_FB_OMAP2_DSS_DEBUGFS */

/* PLATFORM DEVICE */
static int omap_dss_pm_notif(struct notifier_block *b, unsigned long v, void *d)
{
	DSSDBG("pm notif %lu\n", v);

	switch (v) {
	case PM_SUSPEND_PREPARE:
	case PM_HIBERNATION_PREPARE:
	case PM_RESTORE_PREPARE:
		DSSDBG("suspending displays\n");
		return dss_suspend_all_devices();

	case PM_POST_SUSPEND:
	case PM_POST_HIBERNATION:
	case PM_POST_RESTORE:
		DSSDBG("resuming displays\n");
		return dss_resume_all_devices();

	default:
		return 0;
	}
}

static struct notifier_block omap_dss_pm_notif_block = {
	.notifier_call = omap_dss_pm_notif,
};

static int __init omap_dss_probe(struct platform_device *pdev)
{
	core.pdev = pdev;

	dss_features_init(omapdss_get_version());

	dss_initialize_debugfs();

	if (def_disp_name)
		core.default_display_name = def_disp_name;

	register_pm_notifier(&omap_dss_pm_notif_block);

	return 0;
}

static int omap_dss_remove(struct platform_device *pdev)
{
	unregister_pm_notifier(&omap_dss_pm_notif_block);

	dss_uninitialize_debugfs();

	return 0;
}

static void omap_dss_shutdown(struct platform_device *pdev)
{
	DSSDBG("shutdown\n");
	dss_disable_all_devices();
}

static struct platform_driver omap_dss_driver = {
	.remove         = omap_dss_remove,
	.shutdown	= omap_dss_shutdown,
	.driver         = {
		.name   = "omapdss",
	},
};

/* INIT */
static int (*dss_output_drv_reg_funcs[])(void) __initdata = {
	dss_init_platform_driver,
	dispc_init_platform_driver,
#ifdef CONFIG_FB_OMAP2_DSS_DSI
	dsi_init_platform_driver,
#endif
#ifdef CONFIG_FB_OMAP2_DSS_DPI
	dpi_init_platform_driver,
#endif
#ifdef CONFIG_FB_OMAP2_DSS_SDI
	sdi_init_platform_driver,
#endif
#ifdef CONFIG_FB_OMAP2_DSS_VENC
	venc_init_platform_driver,
#endif
#ifdef CONFIG_FB_OMAP4_DSS_HDMI
	hdmi4_init_platform_driver,
#endif
#ifdef CONFIG_FB_OMAP5_DSS_HDMI
	hdmi5_init_platform_driver,
#endif
};

static void (*dss_output_drv_unreg_funcs[])(void) = {
#ifdef CONFIG_FB_OMAP5_DSS_HDMI
	hdmi5_uninit_platform_driver,
#endif
#ifdef CONFIG_FB_OMAP4_DSS_HDMI
	hdmi4_uninit_platform_driver,
#endif
#ifdef CONFIG_FB_OMAP2_DSS_VENC
	venc_uninit_platform_driver,
#endif
#ifdef CONFIG_FB_OMAP2_DSS_SDI
	sdi_uninit_platform_driver,
#endif
#ifdef CONFIG_FB_OMAP2_DSS_DPI
	dpi_uninit_platform_driver,
#endif
#ifdef CONFIG_FB_OMAP2_DSS_DSI
	dsi_uninit_platform_driver,
#endif
	dispc_uninit_platform_driver,
	dss_uninit_platform_driver,
};

static int __init omap_dss_init(void)
{
	int r;
	int i;

	r = platform_driver_probe(&omap_dss_driver, omap_dss_probe);
	if (r)
		return r;

	for (i = 0; i < ARRAY_SIZE(dss_output_drv_reg_funcs); ++i) {
		r = dss_output_drv_reg_funcs[i]();
		if (r)
			goto err_reg;
	}

	return 0;

err_reg:
	for (i = ARRAY_SIZE(dss_output_drv_reg_funcs) - i;
			i < ARRAY_SIZE(dss_output_drv_reg_funcs);
			++i)
		dss_output_drv_unreg_funcs[i]();

	platform_driver_unregister(&omap_dss_driver);

	return r;
}

static void __exit omap_dss_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dss_output_drv_unreg_funcs); ++i)
		dss_output_drv_unreg_funcs[i]();

	platform_driver_unregister(&omap_dss_driver);
}

module_init(omap_dss_init);
module_exit(omap_dss_exit);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@nokia.com>");
MODULE_DESCRIPTION("OMAP2/3 Display Subsystem");
MODULE_LICENSE("GPL v2");


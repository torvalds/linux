/*
 * linux/drivers/video/omap2/dss/core.c
 *
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * Some code and ideas taken from drivers/video/omap/ driver
 * by Imre Deak.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
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

#include <video/omapdss.h>

#include "dss.h"
#include "dss_features.h"

static struct {
	struct platform_device *pdev;

	const char *default_display_name;
} core;

static char *def_disp_name;
module_param_named(def_disp, def_disp_name, charp, 0);
MODULE_PARM_DESC(def_disp, "default display name");

static bool dss_initialized;

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

bool omapdss_is_initialized(void)
{
	return dss_initialized;
}
EXPORT_SYMBOL(omapdss_is_initialized);

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

#if defined(CONFIG_OMAP2_DSS_DEBUGFS)
static int dss_debug_show(struct seq_file *s, void *unused)
{
	void (*func)(struct seq_file *) = s->private;
	func(s);
	return 0;
}

static int dss_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, dss_debug_show, inode->i_private);
}

static const struct file_operations dss_debug_fops = {
	.open           = dss_debug_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static struct dentry *dss_debugfs_dir;

static int dss_initialize_debugfs(void)
{
	dss_debugfs_dir = debugfs_create_dir("omapdss", NULL);
	if (IS_ERR(dss_debugfs_dir)) {
		int err = PTR_ERR(dss_debugfs_dir);
		dss_debugfs_dir = NULL;
		return err;
	}

	debugfs_create_file("clk", S_IRUGO, dss_debugfs_dir,
			&dss_debug_dump_clocks, &dss_debug_fops);

	return 0;
}

static void dss_uninitialize_debugfs(void)
{
	if (dss_debugfs_dir)
		debugfs_remove_recursive(dss_debugfs_dir);
}

int dss_debugfs_create_file(const char *name, void (*write)(struct seq_file *))
{
	struct dentry *d;

	d = debugfs_create_file(name, S_IRUGO, dss_debugfs_dir,
			write, &dss_debug_fops);

	return PTR_RET(d);
}
#else /* CONFIG_OMAP2_DSS_DEBUGFS */
static inline int dss_initialize_debugfs(void)
{
	return 0;
}
static inline void dss_uninitialize_debugfs(void)
{
}
int dss_debugfs_create_file(const char *name, void (*write)(struct seq_file *))
{
	return 0;
}
#endif /* CONFIG_OMAP2_DSS_DEBUGFS */

/* PLATFORM DEVICE */
static int omap_dss_pm_notif(struct notifier_block *b, unsigned long v, void *d)
{
	DSSDBG("pm notif %lu\n", v);

	switch (v) {
	case PM_SUSPEND_PREPARE:
		DSSDBG("suspending displays\n");
		return dss_suspend_all_devices();

	case PM_POST_SUSPEND:
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
	struct omap_dss_board_info *pdata = pdev->dev.platform_data;
	int r;

	core.pdev = pdev;

	dss_features_init(omapdss_get_version());

	r = dss_initialize_debugfs();
	if (r)
		goto err_debugfs;

	if (def_disp_name)
		core.default_display_name = def_disp_name;
	else if (pdata->default_display_name)
		core.default_display_name = pdata->default_display_name;
	else if (pdata->default_device)
		core.default_display_name = pdata->default_device->name;

	register_pm_notifier(&omap_dss_pm_notif_block);

	return 0;

err_debugfs:

	return r;
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
		.owner  = THIS_MODULE,
	},
};

/* BUS */
static int dss_bus_match(struct device *dev, struct device_driver *driver)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);

	DSSDBG("bus_match. dev %s/%s, drv %s\n",
			dev_name(dev), dssdev->driver_name, driver->name);

	return strcmp(dssdev->driver_name, driver->name) == 0;
}

static struct bus_type dss_bus_type = {
	.name = "omapdss",
	.match = dss_bus_match,
};

static void dss_bus_release(struct device *dev)
{
	DSSDBG("bus_release\n");
}

static struct device dss_bus = {
	.release = dss_bus_release,
};

struct bus_type *dss_get_bus(void)
{
	return &dss_bus_type;
}

/* DRIVER */
static int dss_driver_probe(struct device *dev)
{
	int r;
	struct omap_dss_driver *dssdrv = to_dss_driver(dev->driver);
	struct omap_dss_device *dssdev = to_dss_device(dev);

	DSSDBG("driver_probe: dev %s/%s, drv %s\n",
				dev_name(dev), dssdev->driver_name,
				dssdrv->driver.name);

	r = dssdrv->probe(dssdev);

	if (r) {
		DSSERR("driver probe failed: %d\n", r);
		return r;
	}

	DSSDBG("probe done for device %s\n", dev_name(dev));

	dssdev->driver = dssdrv;

	return 0;
}

static int dss_driver_remove(struct device *dev)
{
	struct omap_dss_driver *dssdrv = to_dss_driver(dev->driver);
	struct omap_dss_device *dssdev = to_dss_device(dev);

	DSSDBG("driver_remove: dev %s/%s\n", dev_name(dev),
			dssdev->driver_name);

	dssdrv->remove(dssdev);

	dssdev->driver = NULL;

	return 0;
}

static int omapdss_default_connect(struct omap_dss_device *dssdev)
{
	struct omap_dss_device *out;
	struct omap_overlay_manager *mgr;
	int r;

	out = dssdev->output;

	if (out == NULL)
		return -ENODEV;

	mgr = omap_dss_get_overlay_manager(out->dispc_channel);
	if (!mgr)
		return -ENODEV;

	r = dss_mgr_connect(mgr, out);
	if (r)
		return r;

	return 0;
}

static void omapdss_default_disconnect(struct omap_dss_device *dssdev)
{
	struct omap_dss_device *out;
	struct omap_overlay_manager *mgr;

	out = dssdev->output;

	if (out == NULL)
		return;

	mgr = out->manager;

	if (mgr == NULL)
		return;

	dss_mgr_disconnect(mgr, out);
}

int omap_dss_register_driver(struct omap_dss_driver *dssdriver)
{
	dssdriver->driver.bus = &dss_bus_type;
	dssdriver->driver.probe = dss_driver_probe;
	dssdriver->driver.remove = dss_driver_remove;

	if (dssdriver->get_resolution == NULL)
		dssdriver->get_resolution = omapdss_default_get_resolution;
	if (dssdriver->get_recommended_bpp == NULL)
		dssdriver->get_recommended_bpp =
			omapdss_default_get_recommended_bpp;
	if (dssdriver->get_timings == NULL)
		dssdriver->get_timings = omapdss_default_get_timings;
	if (dssdriver->connect == NULL)
		dssdriver->connect = omapdss_default_connect;
	if (dssdriver->disconnect == NULL)
		dssdriver->disconnect = omapdss_default_disconnect;

	return driver_register(&dssdriver->driver);
}
EXPORT_SYMBOL(omap_dss_register_driver);

void omap_dss_unregister_driver(struct omap_dss_driver *dssdriver)
{
	driver_unregister(&dssdriver->driver);
}
EXPORT_SYMBOL(omap_dss_unregister_driver);

/* DEVICE */

static void omap_dss_dev_release(struct device *dev)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	kfree(dssdev);
}

static int disp_num_counter;

struct omap_dss_device *dss_alloc_and_init_device(struct device *parent)
{
	struct omap_dss_device *dssdev;

	dssdev = kzalloc(sizeof(*dssdev), GFP_KERNEL);
	if (!dssdev)
		return NULL;

	dssdev->old_dev.bus = &dss_bus_type;
	dssdev->old_dev.parent = parent;
	dssdev->old_dev.release = omap_dss_dev_release;
	dev_set_name(&dssdev->old_dev, "display%d", disp_num_counter++);

	device_initialize(&dssdev->old_dev);

	return dssdev;
}

int dss_add_device(struct omap_dss_device *dssdev)
{
	dssdev->dev = &dssdev->old_dev;

	omapdss_register_display(dssdev);
	return device_add(&dssdev->old_dev);
}

void dss_put_device(struct omap_dss_device *dssdev)
{
	put_device(&dssdev->old_dev);
}

void dss_unregister_device(struct omap_dss_device *dssdev)
{
	device_unregister(&dssdev->old_dev);
	omapdss_unregister_display(dssdev);
}

static int dss_unregister_dss_dev(struct device *dev, void *data)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	dss_unregister_device(dssdev);
	return 0;
}

void dss_unregister_child_devices(struct device *parent)
{
	device_for_each_child(parent, NULL, dss_unregister_dss_dev);
}

void dss_copy_device_pdata(struct omap_dss_device *dst,
		const struct omap_dss_device *src)
{
	u8 *d = (u8 *)dst;
	u8 *s = (u8 *)src;
	size_t dsize = sizeof(struct device);

	memcpy(d + dsize, s + dsize, sizeof(struct omap_dss_device) - dsize);
}

/* BUS */
static int __init omap_dss_bus_register(void)
{
	int r;

	r = bus_register(&dss_bus_type);
	if (r) {
		DSSERR("bus register failed\n");
		return r;
	}

	dev_set_name(&dss_bus, "omapdss");
	r = device_register(&dss_bus);
	if (r) {
		DSSERR("bus driver register failed\n");
		bus_unregister(&dss_bus_type);
		return r;
	}

	return 0;
}

/* INIT */
static int (*dss_output_drv_reg_funcs[])(void) __initdata = {
#ifdef CONFIG_OMAP2_DSS_DSI
	dsi_init_platform_driver,
#endif
#ifdef CONFIG_OMAP2_DSS_DPI
	dpi_init_platform_driver,
#endif
#ifdef CONFIG_OMAP2_DSS_SDI
	sdi_init_platform_driver,
#endif
#ifdef CONFIG_OMAP2_DSS_RFBI
	rfbi_init_platform_driver,
#endif
#ifdef CONFIG_OMAP2_DSS_VENC
	venc_init_platform_driver,
#endif
#ifdef CONFIG_OMAP4_DSS_HDMI
	hdmi_init_platform_driver,
#endif
};

static void (*dss_output_drv_unreg_funcs[])(void) __exitdata = {
#ifdef CONFIG_OMAP2_DSS_DSI
	dsi_uninit_platform_driver,
#endif
#ifdef CONFIG_OMAP2_DSS_DPI
	dpi_uninit_platform_driver,
#endif
#ifdef CONFIG_OMAP2_DSS_SDI
	sdi_uninit_platform_driver,
#endif
#ifdef CONFIG_OMAP2_DSS_RFBI
	rfbi_uninit_platform_driver,
#endif
#ifdef CONFIG_OMAP2_DSS_VENC
	venc_uninit_platform_driver,
#endif
#ifdef CONFIG_OMAP4_DSS_HDMI
	hdmi_uninit_platform_driver,
#endif
};

static bool dss_output_drv_loaded[ARRAY_SIZE(dss_output_drv_reg_funcs)];

static int __init omap_dss_register_drivers(void)
{
	int r;
	int i;

	r = platform_driver_probe(&omap_dss_driver, omap_dss_probe);
	if (r)
		return r;

	r = dss_init_platform_driver();
	if (r) {
		DSSERR("Failed to initialize DSS platform driver\n");
		goto err_dss;
	}

	r = dispc_init_platform_driver();
	if (r) {
		DSSERR("Failed to initialize dispc platform driver\n");
		goto err_dispc;
	}

	/*
	 * It's ok if the output-driver register fails. It happens, for example,
	 * when there is no output-device (e.g. SDI for OMAP4).
	 */
	for (i = 0; i < ARRAY_SIZE(dss_output_drv_reg_funcs); ++i) {
		r = dss_output_drv_reg_funcs[i]();
		if (r == 0)
			dss_output_drv_loaded[i] = true;
	}

	return 0;

err_dispc:
	dss_uninit_platform_driver();
err_dss:
	platform_driver_unregister(&omap_dss_driver);

	return r;
}

static void __exit omap_dss_unregister_drivers(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dss_output_drv_unreg_funcs); ++i) {
		if (dss_output_drv_loaded[i])
			dss_output_drv_unreg_funcs[i]();
	}

	dispc_uninit_platform_driver();
	dss_uninit_platform_driver();

	platform_driver_unregister(&omap_dss_driver);
}

#ifdef CONFIG_OMAP2_DSS_MODULE
static void omap_dss_bus_unregister(void)
{
	device_unregister(&dss_bus);

	bus_unregister(&dss_bus_type);
}

static int __init omap_dss_init(void)
{
	int r;

	r = omap_dss_bus_register();
	if (r)
		return r;

	r = omap_dss_register_drivers();
	if (r) {
		omap_dss_bus_unregister();
		return r;
	}

	dss_initialized = true;

	return 0;
}

static void __exit omap_dss_exit(void)
{
	omap_dss_unregister_drivers();

	omap_dss_bus_unregister();
}

module_init(omap_dss_init);
module_exit(omap_dss_exit);
#else
static int __init omap_dss_init(void)
{
	return omap_dss_bus_register();
}

static int __init omap_dss_init2(void)
{
	int r;

	r = omap_dss_register_drivers();
	if (r)
		return r;

	dss_initialized = true;

	return 0;
}

core_initcall(omap_dss_init);
device_initcall(omap_dss_init2);
#endif

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@nokia.com>");
MODULE_DESCRIPTION("OMAP2/3 Display Subsystem");
MODULE_LICENSE("GPL v2");


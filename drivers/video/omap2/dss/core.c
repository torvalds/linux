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

#include <video/omapdss.h>

#include "dss.h"
#include "dss_features.h"

static struct {
	struct platform_device *pdev;

	struct regulator *vdds_dsi_reg;
	struct regulator *vdds_sdi_reg;

	const char *default_display_name;
} core;

static char *def_disp_name;
module_param_named(def_disp, def_disp_name, charp, 0);
MODULE_PARM_DESC(def_disp, "default display name");

#ifdef DEBUG
bool dss_debug;
module_param_named(debug, dss_debug, bool, 0644);
#endif

static int omap_dss_register_device(struct omap_dss_device *);
static void omap_dss_unregister_device(struct omap_dss_device *);

/* REGULATORS */

struct regulator *dss_get_vdds_dsi(void)
{
	struct regulator *reg;

	if (core.vdds_dsi_reg != NULL)
		return core.vdds_dsi_reg;

	reg = regulator_get(&core.pdev->dev, "vdds_dsi");
	if (!IS_ERR(reg))
		core.vdds_dsi_reg = reg;

	return reg;
}

struct regulator *dss_get_vdds_sdi(void)
{
	struct regulator *reg;

	if (core.vdds_sdi_reg != NULL)
		return core.vdds_sdi_reg;

	reg = regulator_get(&core.pdev->dev, "vdds_sdi");
	if (!IS_ERR(reg))
		core.vdds_sdi_reg = reg;

	return reg;
}

int dss_get_ctx_loss_count(struct device *dev)
{
	struct omap_dss_board_info *board_data = core.pdev->dev.platform_data;
	int cnt;

	if (!board_data->get_context_loss_count)
		return -ENOENT;

	cnt = board_data->get_context_loss_count(dev);

	WARN_ONCE(cnt < 0, "get_context_loss_count failed: %d\n", cnt);

	return cnt;
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

	if (!board_data->dsi_enable_pads)
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

#if defined(CONFIG_DEBUG_FS) && defined(CONFIG_OMAP2_DSS_DEBUG_SUPPORT)
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

	if (IS_ERR(d))
		return PTR_ERR(d);

	return 0;
}
#else /* CONFIG_DEBUG_FS && CONFIG_OMAP2_DSS_DEBUG_SUPPORT */
static inline int dss_initialize_debugfs(void)
{
	return 0;
}
static inline void dss_uninitialize_debugfs(void)
{
}
static inline int dss_debugfs_create_file(const char *name,
		void (*write)(struct seq_file *))
{
	return 0;
}
#endif /* CONFIG_DEBUG_FS && CONFIG_OMAP2_DSS_DEBUG_SUPPORT */

/* PLATFORM DEVICE */
static int __init omap_dss_probe(struct platform_device *pdev)
{
	struct omap_dss_board_info *pdata = pdev->dev.platform_data;
	int r;
	int i;

	core.pdev = pdev;

	dss_features_init();

	dss_apply_init();

	dss_init_overlay_managers(pdev);
	dss_init_overlays(pdev);

	r = dss_initialize_debugfs();
	if (r)
		goto err_debugfs;

	if (def_disp_name)
		core.default_display_name = def_disp_name;
	else if (pdata->default_device)
		core.default_display_name = pdata->default_device->name;

	for (i = 0; i < pdata->num_devices; ++i) {
		struct omap_dss_device *dssdev = pdata->devices[i];

		r = omap_dss_register_device(dssdev);
		if (r) {
			DSSERR("device %d %s register failed %d\n", i,
				dssdev->name ?: "unnamed", r);

			while (--i >= 0)
				omap_dss_unregister_device(pdata->devices[i]);

			goto err_register;
		}
	}

	return 0;

err_register:
	dss_uninitialize_debugfs();
err_debugfs:

	return r;
}

static int omap_dss_remove(struct platform_device *pdev)
{
	struct omap_dss_board_info *pdata = pdev->dev.platform_data;
	int i;

	dss_uninitialize_debugfs();

	dss_uninit_overlays(pdev);
	dss_uninit_overlay_managers(pdev);

	for (i = 0; i < pdata->num_devices; ++i)
		omap_dss_unregister_device(pdata->devices[i]);

	return 0;
}

static void omap_dss_shutdown(struct platform_device *pdev)
{
	DSSDBG("shutdown\n");
	dss_disable_all_devices();
}

static int omap_dss_suspend(struct platform_device *pdev, pm_message_t state)
{
	DSSDBG("suspend %d\n", state.event);

	return dss_suspend_all_devices();
}

static int omap_dss_resume(struct platform_device *pdev)
{
	DSSDBG("resume\n");

	return dss_resume_all_devices();
}

static struct platform_driver omap_dss_driver = {
	.remove         = omap_dss_remove,
	.shutdown	= omap_dss_shutdown,
	.suspend	= omap_dss_suspend,
	.resume		= omap_dss_resume,
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

static ssize_t device_name_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	return snprintf(buf, PAGE_SIZE, "%s\n",
			dssdev->name ?
			dssdev->name : "");
}

static struct device_attribute default_dev_attrs[] = {
	__ATTR(name, S_IRUGO, device_name_show, NULL),
	__ATTR_NULL,
};

static ssize_t driver_name_show(struct device_driver *drv, char *buf)
{
	struct omap_dss_driver *dssdrv = to_dss_driver(drv);
	return snprintf(buf, PAGE_SIZE, "%s\n",
			dssdrv->driver.name ?
			dssdrv->driver.name : "");
}
static struct driver_attribute default_drv_attrs[] = {
	__ATTR(name, S_IRUGO, driver_name_show, NULL),
	__ATTR_NULL,
};

static struct bus_type dss_bus_type = {
	.name = "omapdss",
	.match = dss_bus_match,
	.dev_attrs = default_dev_attrs,
	.drv_attrs = default_drv_attrs,
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
	bool force;

	DSSDBG("driver_probe: dev %s/%s, drv %s\n",
				dev_name(dev), dssdev->driver_name,
				dssdrv->driver.name);

	dss_init_device(core.pdev, dssdev);

	force = core.default_display_name &&
		strcmp(core.default_display_name, dssdev->name) == 0;
	dss_recheck_connections(dssdev, force);

	r = dssdrv->probe(dssdev);

	if (r) {
		DSSERR("driver probe failed: %d\n", r);
		dss_uninit_device(core.pdev, dssdev);
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

	dss_uninit_device(core.pdev, dssdev);

	dssdev->driver = NULL;

	return 0;
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

	return driver_register(&dssdriver->driver);
}
EXPORT_SYMBOL(omap_dss_register_driver);

void omap_dss_unregister_driver(struct omap_dss_driver *dssdriver)
{
	driver_unregister(&dssdriver->driver);
}
EXPORT_SYMBOL(omap_dss_unregister_driver);

/* DEVICE */
static void reset_device(struct device *dev, int check)
{
	u8 *dev_p = (u8 *)dev;
	u8 *dev_end = dev_p + sizeof(*dev);
	void *saved_pdata;

	saved_pdata = dev->platform_data;
	if (check) {
		/*
		 * Check if there is any other setting than platform_data
		 * in struct device; warn that these will be reset by our
		 * init.
		 */
		dev->platform_data = NULL;
		while (dev_p < dev_end) {
			if (*dev_p) {
				WARN("%s: struct device fields will be "
						"discarded\n",
				     __func__);
				break;
			}
			dev_p++;
		}
	}
	memset(dev, 0, sizeof(*dev));
	dev->platform_data = saved_pdata;
}


static void omap_dss_dev_release(struct device *dev)
{
	reset_device(dev, 0);
}

static int omap_dss_register_device(struct omap_dss_device *dssdev)
{
	static int dev_num;

	WARN_ON(!dssdev->driver_name);

	reset_device(&dssdev->dev, 1);
	dssdev->dev.bus = &dss_bus_type;
	dssdev->dev.parent = &dss_bus;
	dssdev->dev.release = omap_dss_dev_release;
	dev_set_name(&dssdev->dev, "display%d", dev_num++);
	return device_register(&dssdev->dev);
}

static void omap_dss_unregister_device(struct omap_dss_device *dssdev)
{
	device_unregister(&dssdev->dev);
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
#ifdef CONFIG_OMAP2_DSS_DSI
	dsi_init_platform_driver,
#endif
#ifdef CONFIG_OMAP4_DSS_HDMI
	hdmi_init_platform_driver,
#endif
};

static void (*dss_output_drv_unreg_funcs[])(void) __exitdata = {
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
#ifdef CONFIG_OMAP2_DSS_DSI
	dsi_uninit_platform_driver,
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

	return 0;
}

static void __exit omap_dss_exit(void)
{
	if (core.vdds_dsi_reg != NULL) {
		regulator_put(core.vdds_dsi_reg);
		core.vdds_dsi_reg = NULL;
	}

	if (core.vdds_sdi_reg != NULL) {
		regulator_put(core.vdds_sdi_reg);
		core.vdds_sdi_reg = NULL;
	}

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
	return omap_dss_register_drivers();
}

core_initcall(omap_dss_init);
device_initcall(omap_dss_init2);
#endif

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@nokia.com>");
MODULE_DESCRIPTION("OMAP2/3 Display Subsystem");
MODULE_LICENSE("GPL v2");


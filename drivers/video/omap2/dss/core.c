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

#include <plat/display.h>
#include <plat/clock.h>

#include "dss.h"

static struct {
	struct platform_device *pdev;
	int		ctx_id;

	struct clk      *dss_ick;
	struct clk	*dss1_fck;
	struct clk	*dss2_fck;
	struct clk      *dss_54m_fck;
	struct clk	*dss_96m_fck;
	unsigned	num_clks_enabled;

	struct regulator *vdds_dsi_reg;
	struct regulator *vdds_sdi_reg;
	struct regulator *vdda_dac_reg;
} core;

static void dss_clk_enable_all_no_ctx(void);
static void dss_clk_disable_all_no_ctx(void);
static void dss_clk_enable_no_ctx(enum dss_clock clks);
static void dss_clk_disable_no_ctx(enum dss_clock clks);

static char *def_disp_name;
module_param_named(def_disp, def_disp_name, charp, 0);
MODULE_PARM_DESC(def_disp_name, "default display name");

#ifdef DEBUG
unsigned int dss_debug;
module_param_named(debug, dss_debug, bool, 0644);
#endif

/* CONTEXT */
static int dss_get_ctx_id(void)
{
	struct omap_dss_board_info *pdata = core.pdev->dev.platform_data;
	int r;

	if (!pdata->get_last_off_on_transaction_id)
		return 0;
	r = pdata->get_last_off_on_transaction_id(&core.pdev->dev);
	if (r < 0) {
		dev_err(&core.pdev->dev, "getting transaction ID failed, "
				"will force context restore\n");
		r = -1;
	}
	return r;
}

int dss_need_ctx_restore(void)
{
	int id = dss_get_ctx_id();

	if (id < 0 || id != core.ctx_id) {
		DSSDBG("ctx id %d -> id %d\n",
				core.ctx_id, id);
		core.ctx_id = id;
		return 1;
	} else {
		return 0;
	}
}

static void save_all_ctx(void)
{
	DSSDBG("save context\n");

	dss_clk_enable_no_ctx(DSS_CLK_ICK | DSS_CLK_FCK1);

	dss_save_context();
	dispc_save_context();
#ifdef CONFIG_OMAP2_DSS_DSI
	dsi_save_context();
#endif

	dss_clk_disable_no_ctx(DSS_CLK_ICK | DSS_CLK_FCK1);
}

static void restore_all_ctx(void)
{
	DSSDBG("restore context\n");

	dss_clk_enable_all_no_ctx();

	dss_restore_context();
	dispc_restore_context();
#ifdef CONFIG_OMAP2_DSS_DSI
	dsi_restore_context();
#endif

	dss_clk_disable_all_no_ctx();
}

#if defined(CONFIG_DEBUG_FS) && defined(CONFIG_OMAP2_DSS_DEBUG_SUPPORT)
/* CLOCKS */
static void core_dump_clocks(struct seq_file *s)
{
	int i;
	struct clk *clocks[5] = {
		core.dss_ick,
		core.dss1_fck,
		core.dss2_fck,
		core.dss_54m_fck,
		core.dss_96m_fck
	};

	seq_printf(s, "- CORE -\n");

	seq_printf(s, "internal clk count\t\t%u\n", core.num_clks_enabled);

	for (i = 0; i < 5; i++) {
		if (!clocks[i])
			continue;
		seq_printf(s, "%-15s\t%lu\t%d\n",
				clocks[i]->name,
				clk_get_rate(clocks[i]),
				clocks[i]->usecount);
	}
}
#endif /* defined(CONFIG_DEBUG_FS) && defined(CONFIG_OMAP2_DSS_DEBUG_SUPPORT) */

static int dss_get_clock(struct clk **clock, const char *clk_name)
{
	struct clk *clk;

	clk = clk_get(&core.pdev->dev, clk_name);

	if (IS_ERR(clk)) {
		DSSERR("can't get clock %s", clk_name);
		return PTR_ERR(clk);
	}

	*clock = clk;

	DSSDBG("clk %s, rate %ld\n", clk_name, clk_get_rate(clk));

	return 0;
}

static int dss_get_clocks(void)
{
	int r;

	core.dss_ick = NULL;
	core.dss1_fck = NULL;
	core.dss2_fck = NULL;
	core.dss_54m_fck = NULL;
	core.dss_96m_fck = NULL;

	r = dss_get_clock(&core.dss_ick, "ick");
	if (r)
		goto err;

	r = dss_get_clock(&core.dss1_fck, "dss1_fck");
	if (r)
		goto err;

	r = dss_get_clock(&core.dss2_fck, "dss2_fck");
	if (r)
		goto err;

	r = dss_get_clock(&core.dss_54m_fck, "tv_fck");
	if (r)
		goto err;

	r = dss_get_clock(&core.dss_96m_fck, "video_fck");
	if (r)
		goto err;

	return 0;

err:
	if (core.dss_ick)
		clk_put(core.dss_ick);
	if (core.dss1_fck)
		clk_put(core.dss1_fck);
	if (core.dss2_fck)
		clk_put(core.dss2_fck);
	if (core.dss_54m_fck)
		clk_put(core.dss_54m_fck);
	if (core.dss_96m_fck)
		clk_put(core.dss_96m_fck);

	return r;
}

static void dss_put_clocks(void)
{
	if (core.dss_96m_fck)
		clk_put(core.dss_96m_fck);
	clk_put(core.dss_54m_fck);
	clk_put(core.dss1_fck);
	clk_put(core.dss2_fck);
	clk_put(core.dss_ick);
}

unsigned long dss_clk_get_rate(enum dss_clock clk)
{
	switch (clk) {
	case DSS_CLK_ICK:
		return clk_get_rate(core.dss_ick);
	case DSS_CLK_FCK1:
		return clk_get_rate(core.dss1_fck);
	case DSS_CLK_FCK2:
		return clk_get_rate(core.dss2_fck);
	case DSS_CLK_54M:
		return clk_get_rate(core.dss_54m_fck);
	case DSS_CLK_96M:
		return clk_get_rate(core.dss_96m_fck);
	}

	BUG();
	return 0;
}

static unsigned count_clk_bits(enum dss_clock clks)
{
	unsigned num_clks = 0;

	if (clks & DSS_CLK_ICK)
		++num_clks;
	if (clks & DSS_CLK_FCK1)
		++num_clks;
	if (clks & DSS_CLK_FCK2)
		++num_clks;
	if (clks & DSS_CLK_54M)
		++num_clks;
	if (clks & DSS_CLK_96M)
		++num_clks;

	return num_clks;
}

static void dss_clk_enable_no_ctx(enum dss_clock clks)
{
	unsigned num_clks = count_clk_bits(clks);

	if (clks & DSS_CLK_ICK)
		clk_enable(core.dss_ick);
	if (clks & DSS_CLK_FCK1)
		clk_enable(core.dss1_fck);
	if (clks & DSS_CLK_FCK2)
		clk_enable(core.dss2_fck);
	if (clks & DSS_CLK_54M)
		clk_enable(core.dss_54m_fck);
	if (clks & DSS_CLK_96M)
		clk_enable(core.dss_96m_fck);

	core.num_clks_enabled += num_clks;
}

void dss_clk_enable(enum dss_clock clks)
{
	bool check_ctx = core.num_clks_enabled == 0;

	dss_clk_enable_no_ctx(clks);

	if (check_ctx && cpu_is_omap34xx() && dss_need_ctx_restore())
		restore_all_ctx();
}

static void dss_clk_disable_no_ctx(enum dss_clock clks)
{
	unsigned num_clks = count_clk_bits(clks);

	if (clks & DSS_CLK_ICK)
		clk_disable(core.dss_ick);
	if (clks & DSS_CLK_FCK1)
		clk_disable(core.dss1_fck);
	if (clks & DSS_CLK_FCK2)
		clk_disable(core.dss2_fck);
	if (clks & DSS_CLK_54M)
		clk_disable(core.dss_54m_fck);
	if (clks & DSS_CLK_96M)
		clk_disable(core.dss_96m_fck);

	core.num_clks_enabled -= num_clks;
}

void dss_clk_disable(enum dss_clock clks)
{
	if (cpu_is_omap34xx()) {
		unsigned num_clks = count_clk_bits(clks);

		BUG_ON(core.num_clks_enabled < num_clks);

		if (core.num_clks_enabled == num_clks)
			save_all_ctx();
	}

	dss_clk_disable_no_ctx(clks);
}

static void dss_clk_enable_all_no_ctx(void)
{
	enum dss_clock clks;

	clks = DSS_CLK_ICK | DSS_CLK_FCK1 | DSS_CLK_FCK2 | DSS_CLK_54M;
	if (cpu_is_omap34xx())
		clks |= DSS_CLK_96M;
	dss_clk_enable_no_ctx(clks);
}

static void dss_clk_disable_all_no_ctx(void)
{
	enum dss_clock clks;

	clks = DSS_CLK_ICK | DSS_CLK_FCK1 | DSS_CLK_FCK2 | DSS_CLK_54M;
	if (cpu_is_omap34xx())
		clks |= DSS_CLK_96M;
	dss_clk_disable_no_ctx(clks);
}

static void dss_clk_disable_all(void)
{
	enum dss_clock clks;

	clks = DSS_CLK_ICK | DSS_CLK_FCK1 | DSS_CLK_FCK2 | DSS_CLK_54M;
	if (cpu_is_omap34xx())
		clks |= DSS_CLK_96M;
	dss_clk_disable(clks);
}

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

struct regulator *dss_get_vdda_dac(void)
{
	struct regulator *reg;

	if (core.vdda_dac_reg != NULL)
		return core.vdda_dac_reg;

	reg = regulator_get(&core.pdev->dev, "vdda_dac");
	if (!IS_ERR(reg))
		core.vdda_dac_reg = reg;

	return reg;
}

/* DEBUGFS */
#if defined(CONFIG_DEBUG_FS) && defined(CONFIG_OMAP2_DSS_DEBUG_SUPPORT)
static void dss_debug_dump_clocks(struct seq_file *s)
{
	core_dump_clocks(s);
	dss_dump_clocks(s);
	dispc_dump_clocks(s);
#ifdef CONFIG_OMAP2_DSS_DSI
	dsi_dump_clocks(s);
#endif
}

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

#ifdef CONFIG_OMAP2_DSS_COLLECT_IRQ_STATS
	debugfs_create_file("dispc_irq", S_IRUGO, dss_debugfs_dir,
			&dispc_dump_irqs, &dss_debug_fops);
#endif

#if defined(CONFIG_OMAP2_DSS_DSI) && defined(CONFIG_OMAP2_DSS_COLLECT_IRQ_STATS)
	debugfs_create_file("dsi_irq", S_IRUGO, dss_debugfs_dir,
			&dsi_dump_irqs, &dss_debug_fops);
#endif

	debugfs_create_file("dss", S_IRUGO, dss_debugfs_dir,
			&dss_dump_regs, &dss_debug_fops);
	debugfs_create_file("dispc", S_IRUGO, dss_debugfs_dir,
			&dispc_dump_regs, &dss_debug_fops);
#ifdef CONFIG_OMAP2_DSS_RFBI
	debugfs_create_file("rfbi", S_IRUGO, dss_debugfs_dir,
			&rfbi_dump_regs, &dss_debug_fops);
#endif
#ifdef CONFIG_OMAP2_DSS_DSI
	debugfs_create_file("dsi", S_IRUGO, dss_debugfs_dir,
			&dsi_dump_regs, &dss_debug_fops);
#endif
#ifdef CONFIG_OMAP2_DSS_VENC
	debugfs_create_file("venc", S_IRUGO, dss_debugfs_dir,
			&venc_dump_regs, &dss_debug_fops);
#endif
	return 0;
}

static void dss_uninitialize_debugfs(void)
{
	if (dss_debugfs_dir)
		debugfs_remove_recursive(dss_debugfs_dir);
}
#else /* CONFIG_DEBUG_FS && CONFIG_OMAP2_DSS_DEBUG_SUPPORT */
static inline int dss_initialize_debugfs(void)
{
	return 0;
}
static inline void dss_uninitialize_debugfs(void)
{
}
#endif /* CONFIG_DEBUG_FS && CONFIG_OMAP2_DSS_DEBUG_SUPPORT */

/* PLATFORM DEVICE */
static int omap_dss_probe(struct platform_device *pdev)
{
	struct omap_dss_board_info *pdata = pdev->dev.platform_data;
	int skip_init = 0;
	int r;
	int i;

	core.pdev = pdev;

	dss_init_overlay_managers(pdev);
	dss_init_overlays(pdev);

	r = dss_get_clocks();
	if (r)
		goto err_clocks;

	dss_clk_enable_all_no_ctx();

	core.ctx_id = dss_get_ctx_id();
	DSSDBG("initial ctx id %u\n", core.ctx_id);

#ifdef CONFIG_FB_OMAP_BOOTLOADER_INIT
	/* DISPC_CONTROL */
	if (omap_readl(0x48050440) & 1)	/* LCD enabled? */
		skip_init = 1;
#endif

	r = dss_init(skip_init);
	if (r) {
		DSSERR("Failed to initialize DSS\n");
		goto err_dss;
	}

	r = rfbi_init();
	if (r) {
		DSSERR("Failed to initialize rfbi\n");
		goto err_rfbi;
	}

	r = dpi_init(pdev);
	if (r) {
		DSSERR("Failed to initialize dpi\n");
		goto err_dpi;
	}

	r = dispc_init();
	if (r) {
		DSSERR("Failed to initialize dispc\n");
		goto err_dispc;
	}

	r = venc_init(pdev);
	if (r) {
		DSSERR("Failed to initialize venc\n");
		goto err_venc;
	}

	if (cpu_is_omap34xx()) {
		r = sdi_init(skip_init);
		if (r) {
			DSSERR("Failed to initialize SDI\n");
			goto err_sdi;
		}

		r = dsi_init(pdev);
		if (r) {
			DSSERR("Failed to initialize DSI\n");
			goto err_dsi;
		}
	}

	r = dss_initialize_debugfs();
	if (r)
		goto err_debugfs;

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

		if (def_disp_name && strcmp(def_disp_name, dssdev->name) == 0)
			pdata->default_device = dssdev;
	}

	dss_clk_disable_all();

	return 0;

err_register:
	dss_uninitialize_debugfs();
err_debugfs:
	if (cpu_is_omap34xx())
		dsi_exit();
err_dsi:
	if (cpu_is_omap34xx())
		sdi_exit();
err_sdi:
	venc_exit();
err_venc:
	dispc_exit();
err_dispc:
	dpi_exit();
err_dpi:
	rfbi_exit();
err_rfbi:
	dss_exit();
err_dss:
	dss_clk_disable_all_no_ctx();
	dss_put_clocks();
err_clocks:

	return r;
}

static int omap_dss_remove(struct platform_device *pdev)
{
	struct omap_dss_board_info *pdata = pdev->dev.platform_data;
	int i;
	int c;

	dss_uninitialize_debugfs();

	venc_exit();
	dispc_exit();
	dpi_exit();
	rfbi_exit();
	if (cpu_is_omap34xx()) {
		dsi_exit();
		sdi_exit();
	}

	dss_exit();

	/* these should be removed at some point */
	c = core.dss_ick->usecount;
	if (c > 0) {
		DSSERR("warning: dss_ick usecount %d, disabling\n", c);
		while (c-- > 0)
			clk_disable(core.dss_ick);
	}

	c = core.dss1_fck->usecount;
	if (c > 0) {
		DSSERR("warning: dss1_fck usecount %d, disabling\n", c);
		while (c-- > 0)
			clk_disable(core.dss1_fck);
	}

	c = core.dss2_fck->usecount;
	if (c > 0) {
		DSSERR("warning: dss2_fck usecount %d, disabling\n", c);
		while (c-- > 0)
			clk_disable(core.dss2_fck);
	}

	c = core.dss_54m_fck->usecount;
	if (c > 0) {
		DSSERR("warning: dss_54m_fck usecount %d, disabling\n", c);
		while (c-- > 0)
			clk_disable(core.dss_54m_fck);
	}

	if (core.dss_96m_fck) {
		c = core.dss_96m_fck->usecount;
		if (c > 0) {
			DSSERR("warning: dss_96m_fck usecount %d, disabling\n",
					c);
			while (c-- > 0)
				clk_disable(core.dss_96m_fck);
		}
	}

	dss_put_clocks();

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
	.probe          = omap_dss_probe,
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
	struct omap_dss_board_info *pdata = core.pdev->dev.platform_data;
	bool force;

	DSSDBG("driver_probe: dev %s/%s, drv %s\n",
				dev_name(dev), dssdev->driver_name,
				dssdrv->driver.name);

	dss_init_device(core.pdev, dssdev);

	force = pdata->default_device == dssdev;
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

int omap_dss_register_device(struct omap_dss_device *dssdev)
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

void omap_dss_unregister_device(struct omap_dss_device *dssdev)
{
	device_unregister(&dssdev->dev);
}

/* BUS */
static int omap_dss_bus_register(void)
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

	r = platform_driver_register(&omap_dss_driver);
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

	if (core.vdda_dac_reg != NULL) {
		regulator_put(core.vdda_dac_reg);
		core.vdda_dac_reg = NULL;
	}

	platform_driver_unregister(&omap_dss_driver);

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
	return platform_driver_register(&omap_dss_driver);
}

core_initcall(omap_dss_init);
device_initcall(omap_dss_init2);
#endif

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@nokia.com>");
MODULE_DESCRIPTION("OMAP2/3 Display Subsystem");
MODULE_LICENSE("GPL v2");


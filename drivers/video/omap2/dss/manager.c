/*
 * linux/drivers/video/omap2/dss/manager.c
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

#define DSS_SUBSYS_NAME "MANAGER"

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/jiffies.h>

#include <video/omapdss.h>

#include "dss.h"
#include "dss_features.h"

static int num_managers;
static struct omap_overlay_manager *managers;

static int dss_mgr_wait_for_vsync(struct omap_overlay_manager *mgr)
{
	unsigned long timeout = msecs_to_jiffies(500);
	u32 irq;
	int r;

	r = dispc_runtime_get();
	if (r)
		return r;

	if (mgr->device->type == OMAP_DISPLAY_TYPE_VENC)
		irq = DISPC_IRQ_EVSYNC_ODD;
	else if (mgr->device->type == OMAP_DISPLAY_TYPE_HDMI)
		irq = DISPC_IRQ_EVSYNC_EVEN;
	else
		irq = dispc_mgr_get_vsync_irq(mgr->id);

	r = omap_dispc_wait_for_irq_interruptible_timeout(irq, timeout);

	dispc_runtime_put();

	return r;
}

int dss_init_overlay_managers(struct platform_device *pdev)
{
	int i, r;

	num_managers = dss_feat_get_num_mgrs();

	managers = kzalloc(sizeof(struct omap_overlay_manager) * num_managers,
			GFP_KERNEL);

	BUG_ON(managers == NULL);

	for (i = 0; i < num_managers; ++i) {
		struct omap_overlay_manager *mgr = &managers[i];

		switch (i) {
		case 0:
			mgr->name = "lcd";
			mgr->id = OMAP_DSS_CHANNEL_LCD;
			break;
		case 1:
			mgr->name = "tv";
			mgr->id = OMAP_DSS_CHANNEL_DIGIT;
			break;
		case 2:
			mgr->name = "lcd2";
			mgr->id = OMAP_DSS_CHANNEL_LCD2;
			break;
		case 3:
			mgr->name = "lcd3";
			mgr->id = OMAP_DSS_CHANNEL_LCD3;
			break;
		}

		mgr->set_device = &dss_mgr_set_device;
		mgr->unset_device = &dss_mgr_unset_device;
		mgr->apply = &omap_dss_mgr_apply;
		mgr->set_manager_info = &dss_mgr_set_info;
		mgr->get_manager_info = &dss_mgr_get_info;
		mgr->wait_for_go = &dss_mgr_wait_for_go;
		mgr->wait_for_vsync = &dss_mgr_wait_for_vsync;

		mgr->caps = 0;
		mgr->supported_displays =
			dss_feat_get_supported_displays(mgr->id);

		INIT_LIST_HEAD(&mgr->overlays);

		r = dss_manager_kobj_init(mgr, pdev);
		if (r)
			DSSERR("failed to create sysfs file\n");
	}

	return 0;
}

void dss_uninit_overlay_managers(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < num_managers; ++i) {
		struct omap_overlay_manager *mgr = &managers[i];
		dss_manager_kobj_uninit(mgr);
	}

	kfree(managers);
	managers = NULL;
	num_managers = 0;
}

int omap_dss_get_num_overlay_managers(void)
{
	return num_managers;
}
EXPORT_SYMBOL(omap_dss_get_num_overlay_managers);

struct omap_overlay_manager *omap_dss_get_overlay_manager(int num)
{
	if (num >= num_managers)
		return NULL;

	return &managers[num];
}
EXPORT_SYMBOL(omap_dss_get_overlay_manager);

int dss_mgr_simple_check(struct omap_overlay_manager *mgr,
		const struct omap_overlay_manager_info *info)
{
	if (dss_has_feature(FEAT_ALPHA_FIXED_ZORDER)) {
		/*
		 * OMAP3 supports only graphics source transparency color key
		 * and alpha blending simultaneously. See TRM 15.4.2.4.2.2
		 * Alpha Mode.
		 */
		if (info->partial_alpha_enabled && info->trans_enabled
			&& info->trans_key_type != OMAP_DSS_COLOR_KEY_GFX_DST) {
			DSSERR("check_manager: illegal transparency key\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int dss_mgr_check_zorder(struct omap_overlay_manager *mgr,
		struct omap_overlay_info **overlay_infos)
{
	struct omap_overlay *ovl1, *ovl2;
	struct omap_overlay_info *info1, *info2;

	list_for_each_entry(ovl1, &mgr->overlays, list) {
		info1 = overlay_infos[ovl1->id];

		if (info1 == NULL)
			continue;

		list_for_each_entry(ovl2, &mgr->overlays, list) {
			if (ovl1 == ovl2)
				continue;

			info2 = overlay_infos[ovl2->id];

			if (info2 == NULL)
				continue;

			if (info1->zorder == info2->zorder) {
				DSSERR("overlays %d and %d have the same "
						"zorder %d\n",
					ovl1->id, ovl2->id, info1->zorder);
				return -EINVAL;
			}
		}
	}

	return 0;
}

int dss_mgr_check_timings(struct omap_overlay_manager *mgr,
		const struct omap_video_timings *timings)
{
	if (!dispc_mgr_timings_ok(mgr->id, timings)) {
		DSSERR("check_manager: invalid timings\n");
		return -EINVAL;
	}

	return 0;
}

static int dss_mgr_check_lcd_config(struct omap_overlay_manager *mgr,
		const struct dss_lcd_mgr_config *config)
{
	struct dispc_clock_info cinfo = config->clock_info;
	int dl = config->video_port_width;
	bool stallmode = config->stallmode;
	bool fifohandcheck = config->fifohandcheck;

	if (cinfo.lck_div < 1 || cinfo.lck_div > 255)
		return -EINVAL;

	if (cinfo.pck_div < 1 || cinfo.pck_div > 255)
		return -EINVAL;

	if (dl != 12 && dl != 16 && dl != 18 && dl != 24)
		return -EINVAL;

	/* fifohandcheck should be used only with stallmode */
	if (stallmode == false && fifohandcheck == true)
		return -EINVAL;

	/*
	 * io pad mode can be only checked by using dssdev connected to the
	 * manager. Ignore checking these for now, add checks when manager
	 * is capable of holding information related to the connected interface
	 */

	return 0;
}

int dss_mgr_check(struct omap_overlay_manager *mgr,
		struct omap_overlay_manager_info *info,
		const struct omap_video_timings *mgr_timings,
		const struct dss_lcd_mgr_config *lcd_config,
		struct omap_overlay_info **overlay_infos)
{
	struct omap_overlay *ovl;
	int r;

	if (dss_has_feature(FEAT_ALPHA_FREE_ZORDER)) {
		r = dss_mgr_check_zorder(mgr, overlay_infos);
		if (r)
			return r;
	}

	r = dss_mgr_check_timings(mgr, mgr_timings);
	if (r)
		return r;

	r = dss_mgr_check_lcd_config(mgr, lcd_config);
	if (r)
		return r;

	list_for_each_entry(ovl, &mgr->overlays, list) {
		struct omap_overlay_info *oi;
		int r;

		oi = overlay_infos[ovl->id];

		if (oi == NULL)
			continue;

		r = dss_ovl_check(ovl, oi, mgr_timings);
		if (r)
			return r;
	}

	return 0;
}

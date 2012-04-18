/*
 * include/drm/omap_priv.h
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob@ti.com>
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

#ifndef __OMAP_PRIV_H__
#define __OMAP_PRIV_H__

/* Non-userspace facing APIs
 */

/* optional platform data to configure the default configuration of which
 * pipes/overlays/CRTCs are used.. if this is not provided, then instead the
 * first CONFIG_DRM_OMAP_NUM_CRTCS are used, and they are each connected to
 * one manager, with priority given to managers that are connected to
 * detected devices.  Remaining overlays are used as video planes.  This
 * should be a good default behavior for most cases, but yet there still
 * might be times when you wish to do something different.
 */
struct omap_kms_platform_data {
	/* overlays to use as CRTCs: */
	int ovl_cnt;
	const int *ovl_ids;

	/* overlays to use as video planes: */
	int pln_cnt;
	const int *pln_ids;

	int mgr_cnt;
	const int *mgr_ids;

	int dev_cnt;
	const char **dev_names;
};

struct omap_drm_platform_data {
	struct omap_kms_platform_data *kms_pdata;
	struct omap_dmm_platform_data *dmm_pdata;
};

#endif /* __OMAP_DRM_H__ */

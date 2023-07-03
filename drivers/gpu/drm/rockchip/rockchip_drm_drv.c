// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 *
 * based on exynos_drm_drv.c
 */

#include <linux/dma-buf-cache.h>
#include <linux/dma-mapping.h>
#include <linux/dma-iommu.h>
#include <linux/genalloc.h>
#include <linux/pm_runtime.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/console.h>
#include <linux/iommu.h>
#include <linux/of_reserved_mem.h>

#include <drm/drm_debugfs.h>
#include <drm/drm_drv.h>
#include <drm/drm_displayid.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_fb.h"
#include "rockchip_drm_fbdev.h"
#include "rockchip_drm_gem.h"
#include "rockchip_drm_logo.h"

#include "../drm_crtc_internal.h"
#include "../drivers/clk/rockchip/clk.h"

#define DRIVER_NAME	"rockchip"
#define DRIVER_DESC	"RockChip Soc DRM"
#define DRIVER_DATE	"20140818"
#define DRIVER_MAJOR	3
#define DRIVER_MINOR	0

#if IS_ENABLED(CONFIG_DRM_ROCKCHIP_VVOP)
static bool is_support_iommu = false;
#else
static bool is_support_iommu = true;
#endif
static bool iommu_reserve_map;

static struct drm_driver rockchip_drm_driver;

static unsigned int drm_debug;
module_param_named(debug, drm_debug, int, 0600);

static inline bool rockchip_drm_debug_enabled(enum rockchip_drm_debug_category category)
{
	return unlikely(drm_debug & category);
}

__printf(3, 4)
void rockchip_drm_dbg(const struct device *dev, enum rockchip_drm_debug_category category,
		      const char *format, ...)
{
	struct va_format vaf;
	va_list args;

	if (!rockchip_drm_debug_enabled(category))
		return;

	va_start(args, format);
	vaf.fmt = format;
	vaf.va = &args;

	if (dev)
		dev_printk(KERN_DEBUG, dev, "%pV", &vaf);
	else
		printk(KERN_DEBUG "%pV", &vaf);

	va_end(args);
}

/**
 * rockchip_drm_wait_vact_end
 * @crtc: CRTC to enable line flag
 * @mstimeout: millisecond for timeout
 *
 * Wait for vact_end line flag irq or timeout.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int rockchip_drm_wait_vact_end(struct drm_crtc *crtc, unsigned int mstimeout)
{
	struct rockchip_drm_private *priv;
	int pipe, ret = 0;

	if (!crtc)
		return -ENODEV;

	if (mstimeout <= 0)
		return -EINVAL;

	priv = crtc->dev->dev_private;
	pipe = drm_crtc_index(crtc);

	if (priv->crtc_funcs[pipe] && priv->crtc_funcs[pipe]->wait_vact_end)
		ret = priv->crtc_funcs[pipe]->wait_vact_end(crtc, mstimeout);

	return ret;
}
EXPORT_SYMBOL(rockchip_drm_wait_vact_end);

void drm_mode_convert_to_split_mode(struct drm_display_mode *mode)
{
	u16 hactive, hfp, hsync, hbp;

	hactive = mode->hdisplay;
	hfp = mode->hsync_start - mode->hdisplay;
	hsync = mode->hsync_end - mode->hsync_start;
	hbp = mode->htotal - mode->hsync_end;

	mode->clock *= 2;
	mode->hdisplay = hactive * 2;
	mode->hsync_start = mode->hdisplay + hfp * 2;
	mode->hsync_end = mode->hsync_start + hsync * 2;
	mode->htotal = mode->hsync_end + hbp * 2;
	drm_mode_set_name(mode);
}
EXPORT_SYMBOL(drm_mode_convert_to_split_mode);

void drm_mode_convert_to_origin_mode(struct drm_display_mode *mode)
{
	u16 hactive, hfp, hsync, hbp;

	hactive = mode->hdisplay;
	hfp = mode->hsync_start - mode->hdisplay;
	hsync = mode->hsync_end - mode->hsync_start;
	hbp = mode->htotal - mode->hsync_end;

	mode->clock /= 2;
	mode->hdisplay = hactive / 2;
	mode->hsync_start = mode->hdisplay + hfp / 2;
	mode->hsync_end = mode->hsync_start + hsync / 2;
	mode->htotal = mode->hsync_end + hbp / 2;
}
EXPORT_SYMBOL(drm_mode_convert_to_origin_mode);

/**
 * drm_connector_oob_hotplug_event - Report out-of-band hotplug event to connector
 * @connector: connector to report the event on
 *
 * On some hardware a hotplug event notification may come from outside the display
 * driver / device. An example of this is some USB Type-C setups where the hardware
 * muxes the DisplayPort data and aux-lines but does not pass the altmode HPD
 * status bit to the GPU's DP HPD pin.
 *
 * This function can be used to report these out-of-band events after obtaining
 * a drm_connector reference through calling drm_connector_find_by_fwnode().
 */
void drm_connector_oob_hotplug_event(struct fwnode_handle *connector_fwnode)
{
	struct rockchip_drm_sub_dev *sub_dev;

	if (!connector_fwnode || !connector_fwnode->dev)
		return;

	sub_dev = rockchip_drm_get_sub_dev(dev_of_node(connector_fwnode->dev));

	if (sub_dev && sub_dev->connector && sub_dev->oob_hotplug_event)
		sub_dev->oob_hotplug_event(sub_dev->connector);
}
EXPORT_SYMBOL(drm_connector_oob_hotplug_event);

uint32_t rockchip_drm_get_bpp(const struct drm_format_info *info)
{
	/* use whatever a driver has set */
	if (info->cpp[0])
		return info->cpp[0] * 8;

	switch (info->format) {
	case DRM_FORMAT_YUV420_8BIT:
		return 12;
	case DRM_FORMAT_YUV420_10BIT:
		return 15;
	case DRM_FORMAT_VUY101010:
		return 30;
	default:
		break;
	}

	/* all attempts failed */
	return 0;
}
EXPORT_SYMBOL(rockchip_drm_get_bpp);

/**
 * rockchip_drm_of_find_possible_crtcs - find the possible CRTCs for an active
 * encoder port
 * @dev: DRM device
 * @port: encoder port to scan for endpoints
 *
 * Scan all active endpoints attached to a port, locate their attached CRTCs,
 * and generate the DRM mask of CRTCs which may be attached to this
 * encoder.
 *
 * See Documentation/devicetree/bindings/graph.txt for the bindings.
 */
uint32_t rockchip_drm_of_find_possible_crtcs(struct drm_device *dev,
					     struct device_node *port)
{
	struct device_node *remote_port, *ep;
	uint32_t possible_crtcs = 0;

	for_each_endpoint_of_node(port, ep) {
		if (!of_device_is_available(ep))
			continue;

		remote_port = of_graph_get_remote_port(ep);
		if (!remote_port) {
			of_node_put(ep);
			continue;
		}

		possible_crtcs |= drm_of_crtc_port_mask(dev, remote_port);

		of_node_put(remote_port);
	}

	return possible_crtcs;
}
EXPORT_SYMBOL(rockchip_drm_of_find_possible_crtcs);

static DEFINE_MUTEX(rockchip_drm_sub_dev_lock);
static LIST_HEAD(rockchip_drm_sub_dev_list);

void rockchip_connector_update_vfp_for_vrr(struct drm_crtc *crtc, struct drm_display_mode *mode,
					   int vfp)
{
	struct rockchip_drm_sub_dev *sub_dev;

	mutex_lock(&rockchip_drm_sub_dev_lock);
	list_for_each_entry(sub_dev, &rockchip_drm_sub_dev_list, list) {
		if (sub_dev->connector->state->crtc == crtc) {
			if (sub_dev->update_vfp_for_vrr)
				sub_dev->update_vfp_for_vrr(sub_dev->connector, mode, vfp);
		}
	}
	mutex_unlock(&rockchip_drm_sub_dev_lock);
}
EXPORT_SYMBOL(rockchip_connector_update_vfp_for_vrr);

void rockchip_drm_register_sub_dev(struct rockchip_drm_sub_dev *sub_dev)
{
	mutex_lock(&rockchip_drm_sub_dev_lock);
	list_add_tail(&sub_dev->list, &rockchip_drm_sub_dev_list);
	mutex_unlock(&rockchip_drm_sub_dev_lock);
}
EXPORT_SYMBOL(rockchip_drm_register_sub_dev);

void rockchip_drm_unregister_sub_dev(struct rockchip_drm_sub_dev *sub_dev)
{
	mutex_lock(&rockchip_drm_sub_dev_lock);
	list_del(&sub_dev->list);
	mutex_unlock(&rockchip_drm_sub_dev_lock);
}
EXPORT_SYMBOL(rockchip_drm_unregister_sub_dev);

struct rockchip_drm_sub_dev *rockchip_drm_get_sub_dev(struct device_node *node)
{
	struct rockchip_drm_sub_dev *sub_dev = NULL;
	bool found = false;

	mutex_lock(&rockchip_drm_sub_dev_lock);
	list_for_each_entry(sub_dev, &rockchip_drm_sub_dev_list, list) {
		if (sub_dev->of_node == node) {
			found = true;
			break;
		}
	}
	mutex_unlock(&rockchip_drm_sub_dev_lock);

	return found ? sub_dev : NULL;
}
EXPORT_SYMBOL(rockchip_drm_get_sub_dev);

int rockchip_drm_get_sub_dev_type(void)
{
	int connector_type = DRM_MODE_CONNECTOR_Unknown;
	struct rockchip_drm_sub_dev *sub_dev = NULL;

	mutex_lock(&rockchip_drm_sub_dev_lock);
	list_for_each_entry(sub_dev, &rockchip_drm_sub_dev_list, list) {
		if (sub_dev->connector->encoder) {
			connector_type = sub_dev->connector->connector_type;
			break;
		}
	}
	mutex_unlock(&rockchip_drm_sub_dev_lock);

	return connector_type;
}
EXPORT_SYMBOL(rockchip_drm_get_sub_dev_type);

u32 rockchip_drm_get_scan_line_time_ns(void)
{
	struct rockchip_drm_sub_dev *sub_dev = NULL;
	struct drm_display_mode *mode;
	int linedur_ns = 0;

	mutex_lock(&rockchip_drm_sub_dev_lock);
	list_for_each_entry(sub_dev, &rockchip_drm_sub_dev_list, list) {
		if (sub_dev->connector->encoder && sub_dev->connector->state->crtc) {
			mode = &sub_dev->connector->state->crtc->state->adjusted_mode;
			linedur_ns  = div_u64((u64) mode->crtc_htotal * 1000000, mode->crtc_clock);
			break;
		}
	}
	mutex_unlock(&rockchip_drm_sub_dev_lock);

	return linedur_ns;
}
EXPORT_SYMBOL(rockchip_drm_get_scan_line_time_ns);

void rockchip_drm_te_handle(struct drm_crtc *crtc)
{
	struct rockchip_drm_private *priv = crtc->dev->dev_private;
	int pipe = drm_crtc_index(crtc);

	if (priv->crtc_funcs[pipe] && priv->crtc_funcs[pipe]->te_handler)
		priv->crtc_funcs[pipe]->te_handler(crtc);
}
EXPORT_SYMBOL(rockchip_drm_te_handle);

static const struct drm_display_mode rockchip_drm_default_modes[] = {
	/* 4 - 1280x720@60Hz 16:9 */
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 1390,
		   1430, 1650, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 16 - 1920x1080@60Hz 16:9 */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2008,
		   2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 31 - 1920x1080@50Hz 16:9 */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2448,
		   2492, 2640, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 19 - 1280x720@50Hz 16:9 */
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 1720,
		   1760, 1980, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 0x10 - 1024x768@60Hz */
	{ DRM_MODE("1024x768", DRM_MODE_TYPE_DRIVER, 65000, 1024, 1048,
		   1184, 1344, 0,  768, 771, 777, 806, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* 17 - 720x576@50Hz 4:3 */
	{ DRM_MODE("720x576", DRM_MODE_TYPE_DRIVER, 27000, 720, 732,
		   796, 864, 0, 576, 581, 586, 625, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_4_3, },
	/* 2 - 720x480@60Hz 4:3 */
	{ DRM_MODE("720x480", DRM_MODE_TYPE_DRIVER, 27000, 720, 736,
		   798, 858, 0, 480, 489, 495, 525, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_4_3, },
};

int rockchip_drm_add_modes_noedid(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode;
	int i, count, num_modes = 0;

	mutex_lock(&rockchip_drm_sub_dev_lock);
	count = ARRAY_SIZE(rockchip_drm_default_modes);

	for (i = 0; i < count; i++) {
		const struct drm_display_mode *ptr = &rockchip_drm_default_modes[i];

		mode = drm_mode_duplicate(dev, ptr);
		if (mode) {
			if (!i)
				mode->type = DRM_MODE_TYPE_PREFERRED;
			drm_mode_probed_add(connector, mode);
			num_modes++;
		}
	}
	mutex_unlock(&rockchip_drm_sub_dev_lock);

	return num_modes;
}
EXPORT_SYMBOL(rockchip_drm_add_modes_noedid);

static const struct rockchip_drm_width_dclk {
	int width;
	u32 dclk_khz;
} rockchip_drm_dclk[] = {
	{1920, 148500},
	{2048, 200000},
	{2560, 280000},
	{3840, 594000},
	{4096, 594000},
	{7680, 2376000},
};

u32 rockchip_drm_get_dclk_by_width(int width)
{
	int i = 0;
	u32 dclk_khz;

	for (i = 0; i < ARRAY_SIZE(rockchip_drm_dclk); i++) {
		if (width == rockchip_drm_dclk[i].width) {
			dclk_khz = rockchip_drm_dclk[i].dclk_khz;
			break;
		}
	}

	if (i == ARRAY_SIZE(rockchip_drm_dclk)) {
		DRM_ERROR("Can't not find %d width solution and use 148500 khz as max dclk\n", width);

		dclk_khz = 148500;
	}

	return dclk_khz;
}
EXPORT_SYMBOL(rockchip_drm_get_dclk_by_width);

static int
cea_db_tag(const u8 *db)
{
	return db[0] >> 5;
}

static int
cea_db_payload_len(const u8 *db)
{
	return db[0] & 0x1f;
}

#define for_each_cea_db(cea, i, start, end) \
	for ((i) = (start); \
	     (i) < (end) && (i) + cea_db_payload_len(&(cea)[(i)]) < (end); \
	     (i) += cea_db_payload_len(&(cea)[(i)]) + 1)

#define HDMI_NEXT_HDR_VSDB_OUI 0xd04601

static bool cea_db_is_hdmi_next_hdr_block(const u8 *db)
{
	unsigned int oui;

	if (cea_db_tag(db) != 0x07)
		return false;

	if (cea_db_payload_len(db) < 11)
		return false;

	oui = db[3] << 16 | db[2] << 8 | db[1];

	return oui == HDMI_NEXT_HDR_VSDB_OUI;
}

static bool cea_db_is_hdmi_forum_vsdb(const u8 *db)
{
	unsigned int oui;

	if (cea_db_tag(db) != 0x03)
		return false;

	if (cea_db_payload_len(db) < 7)
		return false;

	oui = db[3] << 16 | db[2] << 8 | db[1];

	return oui == HDMI_FORUM_IEEE_OUI;
}

static int
cea_db_offsets(const u8 *cea, int *start, int *end)
{
	/* DisplayID CTA extension blocks and top-level CEA EDID
	 * block header definitions differ in the following bytes:
	 *   1) Byte 2 of the header specifies length differently,
	 *   2) Byte 3 is only present in the CEA top level block.
	 *
	 * The different definitions for byte 2 follow.
	 *
	 * DisplayID CTA extension block defines byte 2 as:
	 *   Number of payload bytes
	 *
	 * CEA EDID block defines byte 2 as:
	 *   Byte number (decimal) within this block where the 18-byte
	 *   DTDs begin. If no non-DTD data is present in this extension
	 *   block, the value should be set to 04h (the byte after next).
	 *   If set to 00h, there are no DTDs present in this block and
	 *   no non-DTD data.
	 */
	if (cea[0] == 0x81) {
		/*
		 * for_each_displayid_db() has already verified
		 * that these stay within expected bounds.
		 */
		*start = 3;
		*end = *start + cea[2];
	} else if (cea[0] == 0x02) {
		/* Data block offset in CEA extension block */
		*start = 4;
		*end = cea[2];
		if (*end == 0)
			*end = 127;
		if (*end < 4 || *end > 127)
			return -ERANGE;
	} else {
		return -EOPNOTSUPP;
	}

	return 0;
}

static u8 *find_edid_extension(const struct edid *edid,
			       int ext_id, int *ext_index)
{
	u8 *edid_ext = NULL;
	int i;

	/* No EDID or EDID extensions */
	if (edid == NULL || edid->extensions == 0)
		return NULL;

	/* Find CEA extension */
	for (i = *ext_index; i < edid->extensions; i++) {
		edid_ext = (u8 *)edid + EDID_LENGTH * (i + 1);
		if (edid_ext[0] == ext_id)
			break;
	}

	if (i >= edid->extensions)
		return NULL;

	*ext_index = i + 1;

	return edid_ext;
}

static int validate_displayid(u8 *displayid, int length, int idx)
{
	int i, dispid_length;
	u8 csum = 0;
	struct displayid_hdr *base;

	base = (struct displayid_hdr *)&displayid[idx];

	DRM_DEBUG_KMS("base revision 0x%x, length %d, %d %d\n",
		      base->rev, base->bytes, base->prod_id, base->ext_count);

	/* +1 for DispID checksum */
	dispid_length = sizeof(*base) + base->bytes + 1;
	if (dispid_length > length - idx)
		return -EINVAL;

	for (i = 0; i < dispid_length; i++)
		csum += displayid[idx + i];
	if (csum) {
		DRM_NOTE("DisplayID checksum invalid, remainder is %d\n", csum);
		return -EINVAL;
	}

	return 0;
}

static u8 *find_displayid_extension(const struct edid *edid,
				    int *length, int *idx,
				    int *ext_index)
{
	u8 *displayid = find_edid_extension(edid, 0x70, ext_index);
	struct displayid_hdr *base;
	int ret;

	if (!displayid)
		return NULL;

	/* EDID extensions block checksum isn't for us */
	*length = EDID_LENGTH - 1;
	*idx = 1;

	ret = validate_displayid(displayid, *length, *idx);
	if (ret)
		return NULL;

	base = (struct displayid_hdr *)&displayid[*idx];
	*length = *idx + sizeof(*base) + base->bytes;

	return displayid;
}

static u8 *find_cea_extension(const struct edid *edid)
{
	int length, idx;
	struct displayid_block *block;
	u8 *cea;
	u8 *displayid;
	int ext_index;

	/* Look for a top level CEA extension block */
	/* FIXME: make callers iterate through multiple CEA ext blocks? */
	ext_index = 0;
	cea = find_edid_extension(edid, 0x02, &ext_index);
	if (cea)
		return cea;

	/* CEA blocks can also be found embedded in a DisplayID block */
	ext_index = 0;
	for (;;) {
		displayid = find_displayid_extension(edid, &length, &idx,
						     &ext_index);
		if (!displayid)
			return NULL;

		idx += sizeof(struct displayid_hdr);
		for_each_displayid_db(displayid, block, idx, length) {
			if (block->tag == 0x81)
				return (u8 *)block;
		}
	}

	return NULL;
}

#define EDID_CEA_YCRCB422	(1 << 4)

int rockchip_drm_get_yuv422_format(struct drm_connector *connector,
				   struct edid *edid)
{
	struct drm_display_info *info;
	const u8 *edid_ext;

	if (!connector || !edid)
		return -EINVAL;

	info = &connector->display_info;

	edid_ext = find_cea_extension(edid);
	if (!edid_ext)
		return -EINVAL;

	if (edid_ext[3] & EDID_CEA_YCRCB422)
		info->color_formats |= DRM_COLOR_FORMAT_YCRCB422;

	return 0;
}
EXPORT_SYMBOL(rockchip_drm_get_yuv422_format);

static
void get_max_frl_rate(int max_frl_rate, u8 *max_lanes, u8 *max_rate_per_lane)
{
	switch (max_frl_rate) {
	case 1:
		*max_lanes = 3;
		*max_rate_per_lane = 3;
		break;
	case 2:
		*max_lanes = 3;
		*max_rate_per_lane = 6;
		break;
	case 3:
		*max_lanes = 4;
		*max_rate_per_lane = 6;
		break;
	case 4:
		*max_lanes = 4;
		*max_rate_per_lane = 8;
		break;
	case 5:
		*max_lanes = 4;
		*max_rate_per_lane = 10;
		break;
	case 6:
		*max_lanes = 4;
		*max_rate_per_lane = 12;
		break;
	case 0:
	default:
		*max_lanes = 0;
		*max_rate_per_lane = 0;
	}
}

#define EDID_DSC_10BPC			(1 << 0)
#define EDID_DSC_12BPC			(1 << 1)
#define EDID_DSC_16BPC			(1 << 2)
#define EDID_DSC_ALL_BPP		(1 << 3)
#define EDID_DSC_NATIVE_420		(1 << 6)
#define EDID_DSC_1P2			(1 << 7)
#define EDID_DSC_MAX_FRL_RATE_MASK	0xf0
#define EDID_DSC_MAX_SLICES		0xf
#define EDID_DSC_TOTAL_CHUNK_KBYTES	0x3f
#define EDID_MAX_FRL_RATE_MASK		0xf0

static
void parse_edid_forum_vsdb(struct rockchip_drm_dsc_cap *dsc_cap,
			   u8 *max_frl_rate_per_lane, u8 *max_lanes, u8 *add_func,
			   const u8 *hf_vsdb)
{
	u8 max_frl_rate;
	u8 dsc_max_frl_rate;
	u8 dsc_max_slices;

	if (!hf_vsdb[7])
		return;

	DRM_DEBUG_KMS("hdmi_21 sink detected. parsing edid\n");
	max_frl_rate = (hf_vsdb[7] & EDID_MAX_FRL_RATE_MASK) >> 4;
	get_max_frl_rate(max_frl_rate, max_lanes,
			 max_frl_rate_per_lane);

	*add_func = hf_vsdb[8];

	if (cea_db_payload_len(hf_vsdb) < 13)
		return;

	dsc_cap->v_1p2 = hf_vsdb[11] & EDID_DSC_1P2;

	if (!dsc_cap->v_1p2)
		return;

	dsc_cap->native_420 = hf_vsdb[11] & EDID_DSC_NATIVE_420;
	dsc_cap->all_bpp = hf_vsdb[11] & EDID_DSC_ALL_BPP;

	if (hf_vsdb[11] & EDID_DSC_16BPC)
		dsc_cap->bpc_supported = 16;
	else if (hf_vsdb[11] & EDID_DSC_12BPC)
		dsc_cap->bpc_supported = 12;
	else if (hf_vsdb[11] & EDID_DSC_10BPC)
		dsc_cap->bpc_supported = 10;
	else
		dsc_cap->bpc_supported = 0;

	dsc_max_frl_rate = (hf_vsdb[12] & EDID_DSC_MAX_FRL_RATE_MASK) >> 4;
	get_max_frl_rate(dsc_max_frl_rate, &dsc_cap->max_lanes,
			 &dsc_cap->max_frl_rate_per_lane);
	dsc_cap->total_chunk_kbytes = hf_vsdb[13] & EDID_DSC_TOTAL_CHUNK_KBYTES;

	dsc_max_slices = hf_vsdb[12] & EDID_DSC_MAX_SLICES;
	switch (dsc_max_slices) {
	case 1:
		dsc_cap->max_slices = 1;
		dsc_cap->clk_per_slice = 340;
		break;
	case 2:
		dsc_cap->max_slices = 2;
		dsc_cap->clk_per_slice = 340;
		break;
	case 3:
		dsc_cap->max_slices = 4;
		dsc_cap->clk_per_slice = 340;
		break;
	case 4:
		dsc_cap->max_slices = 8;
		dsc_cap->clk_per_slice = 340;
		break;
	case 5:
		dsc_cap->max_slices = 8;
		dsc_cap->clk_per_slice = 400;
		break;
	case 6:
		dsc_cap->max_slices = 12;
		dsc_cap->clk_per_slice = 400;
		break;
	case 7:
		dsc_cap->max_slices = 16;
		dsc_cap->clk_per_slice = 400;
		break;
	case 0:
	default:
		dsc_cap->max_slices = 0;
		dsc_cap->clk_per_slice = 0;
	}
}

enum {
	VER_26_BYTE_V0,
	VER_15_BYTE_V1,
	VER_12_BYTE_V1,
	VER_12_BYTE_V2,
};

static int check_next_hdr_version(const u8 *next_hdr_db)
{
	u16 ver;

	ver = (next_hdr_db[5] & 0xf0) << 8 | next_hdr_db[0];

	switch (ver) {
	case 0x00f9:
		return VER_26_BYTE_V0;
	case 0x20ee:
		return VER_15_BYTE_V1;
	case 0x20eb:
		return VER_12_BYTE_V1;
	case 0x40eb:
		return VER_12_BYTE_V2;
	default:
		return -ENOENT;
	}
}

static void parse_ver_26_v0_data(struct ver_26_v0 *hdr, const u8 *data)
{
	hdr->yuv422_12bit = data[5] & BIT(0);
	hdr->support_2160p_60 = (data[5] & BIT(1)) >> 1;
	hdr->global_dimming = (data[5] & BIT(2)) >> 2;

	hdr->dm_major_ver = (data[21] & 0xf0) >> 4;
	hdr->dm_minor_ver = data[21] & 0xf;

	hdr->t_min_pq = (data[19] << 4) | ((data[18] & 0xf0) >> 4);
	hdr->t_max_pq = (data[20] << 4) | (data[18] & 0xf);

	hdr->rx = (data[7] << 4) | ((data[6] & 0xf0) >> 4);
	hdr->ry = (data[8] << 4) | (data[6] & 0xf);
	hdr->gx = (data[10] << 4) | ((data[9] & 0xf0) >> 4);
	hdr->gy = (data[11] << 4) | (data[9] & 0xf);
	hdr->bx = (data[13] << 4) | ((data[12] & 0xf0) >> 4);
	hdr->by = (data[14] << 4) | (data[12] & 0xf);
	hdr->wx = (data[16] << 4) | ((data[15] & 0xf0) >> 4);
	hdr->wy = (data[17] << 4) | (data[15] & 0xf);
}

static void parse_ver_15_v1_data(struct ver_15_v1 *hdr, const u8 *data)
{
	hdr->yuv422_12bit = data[5] & BIT(0);
	hdr->support_2160p_60 = (data[5] & BIT(1)) >> 1;
	hdr->global_dimming = data[6] & BIT(0);

	hdr->dm_version = (data[5] & 0x1c) >> 2;

	hdr->colorimetry = data[7] & BIT(0);

	hdr->t_max_lum = (data[6] & 0xfe) >> 1;
	hdr->t_min_lum = (data[7] & 0xfe) >> 1;

	hdr->rx = data[9];
	hdr->ry = data[10];
	hdr->gx = data[11];
	hdr->gy = data[12];
	hdr->bx = data[13];
	hdr->by = data[14];
}

static void parse_ver_12_v1_data(struct ver_12_v1 *hdr, const u8 *data)
{
	hdr->yuv422_12bit = data[5] & BIT(0);
	hdr->support_2160p_60 = (data[5] & BIT(1)) >> 1;
	hdr->global_dimming = data[6] & BIT(0);

	hdr->dm_version = (data[5] & 0x1c) >> 2;

	hdr->colorimetry = data[7] & BIT(0);

	hdr->t_max_lum = (data[6] & 0xfe) >> 1;
	hdr->t_min_lum = (data[7] & 0xfe) >> 1;

	hdr->low_latency = data[8] & 0x3;

	hdr->unique_rx = (data[11] & 0xf8) >> 3;
	hdr->unique_ry = (data[11] & 0x7) << 2 | (data[10] & BIT(0)) << 1 |
		(data[9] & BIT(0));
	hdr->unique_gx = (data[9] & 0xfe) >> 1;
	hdr->unique_gy = (data[10] & 0xfe) >> 1;
	hdr->unique_bx = (data[8] & 0xe0) >> 5;
	hdr->unique_by = (data[8] & 0x1c) >> 2;
}

static void parse_ver_12_v2_data(struct ver_12_v2 *hdr, const u8 *data)
{
	hdr->yuv422_12bit = data[5] & BIT(0);
	hdr->backlt_ctrl = (data[5] & BIT(1)) >> 1;
	hdr->global_dimming = (data[6] & BIT(2)) >> 2;

	hdr->dm_version = (data[5] & 0x1c) >> 2;
	hdr->backlt_min_luma = data[6] & 0x3;
	hdr->interface = data[7] & 0x3;
	hdr->yuv444_10b_12b = (data[8] & BIT(0)) << 1 | (data[9] & BIT(0));

	hdr->t_min_pq_v2 = (data[6] & 0xf8) >> 3;
	hdr->t_max_pq_v2 = (data[7] & 0xf8) >> 3;

	hdr->unique_rx = (data[10] & 0xf8) >> 3;
	hdr->unique_ry = (data[11] & 0xf8) >> 3;
	hdr->unique_gx = (data[8] & 0xfe) >> 1;
	hdr->unique_gy = (data[9] & 0xfe) >> 1;
	hdr->unique_bx = data[10] & 0x7;
	hdr->unique_by = data[11] & 0x7;
}

static
void parse_next_hdr_block(struct next_hdr_sink_data *sink_data,
			  const u8 *next_hdr_db)
{
	int version;

	version = check_next_hdr_version(next_hdr_db);
	if (version < 0)
		return;

	sink_data->version = version;

	switch (version) {
	case VER_26_BYTE_V0:
		parse_ver_26_v0_data(&sink_data->ver_26_v0, next_hdr_db);
		break;
	case VER_15_BYTE_V1:
		parse_ver_15_v1_data(&sink_data->ver_15_v1, next_hdr_db);
		break;
	case VER_12_BYTE_V1:
		parse_ver_12_v1_data(&sink_data->ver_12_v1, next_hdr_db);
		break;
	case VER_12_BYTE_V2:
		parse_ver_12_v2_data(&sink_data->ver_12_v2, next_hdr_db);
		break;
	default:
		break;
	}
}

int rockchip_drm_parse_cea_ext(struct rockchip_drm_dsc_cap *dsc_cap,
			       u8 *max_frl_rate_per_lane, u8 *max_lanes, u8 *add_func,
			       const struct edid *edid)
{
	const u8 *edid_ext;
	int i, start, end;

	if (!dsc_cap || !max_frl_rate_per_lane || !max_lanes || !edid || !add_func)
		return -EINVAL;

	edid_ext = find_cea_extension(edid);
	if (!edid_ext)
		return -EINVAL;

	if (cea_db_offsets(edid_ext, &start, &end))
		return -EINVAL;

	for_each_cea_db(edid_ext, i, start, end) {
		const u8 *db = &edid_ext[i];

		if (cea_db_is_hdmi_forum_vsdb(db))
			parse_edid_forum_vsdb(dsc_cap, max_frl_rate_per_lane,
					      max_lanes, add_func, db);
	}

	return 0;
}
EXPORT_SYMBOL(rockchip_drm_parse_cea_ext);

int rockchip_drm_parse_next_hdr(struct next_hdr_sink_data *sink_data,
				const struct edid *edid)
{
	const u8 *edid_ext;
	int i, start, end;

	if (!sink_data || !edid)
		return -EINVAL;

	memset(sink_data, 0, sizeof(struct next_hdr_sink_data));

	edid_ext = find_cea_extension(edid);
	if (!edid_ext)
		return -EINVAL;

	if (cea_db_offsets(edid_ext, &start, &end))
		return -EINVAL;

	for_each_cea_db(edid_ext, i, start, end) {
		const u8 *db = &edid_ext[i];

		if (cea_db_is_hdmi_next_hdr_block(db))
			parse_next_hdr_block(sink_data, db);
	}

	return 0;
}
EXPORT_SYMBOL(rockchip_drm_parse_next_hdr);

#define COLORIMETRY_DATA_BLOCK		0x5
#define USE_EXTENDED_TAG		0x07

static bool cea_db_is_hdmi_colorimetry_data_block(const u8 *db)
{
	if (cea_db_tag(db) != USE_EXTENDED_TAG)
		return false;

	if (db[1] != COLORIMETRY_DATA_BLOCK)
		return false;

	return true;
}

int
rockchip_drm_parse_colorimetry_data_block(u8 *colorimetry, const struct edid *edid)
{
	const u8 *edid_ext;
	int i, start, end;

	if (!colorimetry || !edid)
		return -EINVAL;

	*colorimetry = 0;

	edid_ext = find_cea_extension(edid);
	if (!edid_ext)
		return -EINVAL;

	if (cea_db_offsets(edid_ext, &start, &end))
		return -EINVAL;

	for_each_cea_db(edid_ext, i, start, end) {
		const u8 *db = &edid_ext[i];

		if (cea_db_is_hdmi_colorimetry_data_block(db))
			/* As per CEA 861-G spec */
			*colorimetry = ((db[3] & (0x1 << 7)) << 1) | db[2];
	}

	return 0;
}
EXPORT_SYMBOL(rockchip_drm_parse_colorimetry_data_block);

/*
 * Attach a (component) device to the shared drm dma mapping from master drm
 * device.  This is used by the VOPs to map GEM buffers to a common DMA
 * mapping.
 */
int rockchip_drm_dma_attach_device(struct drm_device *drm_dev,
				   struct device *dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;
	int ret;

	if (!is_support_iommu)
		return 0;

	ret = iommu_attach_device(private->domain, dev);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to attach iommu device\n");
		return ret;
	}

	return 0;
}

void rockchip_drm_dma_detach_device(struct drm_device *drm_dev,
				    struct device *dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct iommu_domain *domain = private->domain;

	if (!is_support_iommu)
		return;

	iommu_detach_device(domain, dev);
}

void rockchip_drm_crtc_standby(struct drm_crtc *crtc, bool standby)
{
	struct rockchip_drm_private *priv = crtc->dev->dev_private;
	int pipe = drm_crtc_index(crtc);

	if (pipe < ROCKCHIP_MAX_CRTC &&
	    priv->crtc_funcs[pipe] &&
	    priv->crtc_funcs[pipe]->crtc_standby)
		priv->crtc_funcs[pipe]->crtc_standby(crtc, standby);
}

int rockchip_register_crtc_funcs(struct drm_crtc *crtc,
				 const struct rockchip_crtc_funcs *crtc_funcs)
{
	int pipe = drm_crtc_index(crtc);
	struct rockchip_drm_private *priv = crtc->dev->dev_private;

	if (pipe >= ROCKCHIP_MAX_CRTC)
		return -EINVAL;

	priv->crtc_funcs[pipe] = crtc_funcs;

	return 0;
}

void rockchip_unregister_crtc_funcs(struct drm_crtc *crtc)
{
	int pipe = drm_crtc_index(crtc);
	struct rockchip_drm_private *priv = crtc->dev->dev_private;

	if (pipe >= ROCKCHIP_MAX_CRTC)
		return;

	priv->crtc_funcs[pipe] = NULL;
}

static int rockchip_drm_fault_handler(struct iommu_domain *iommu,
				      struct device *dev,
				      unsigned long iova, int flags, void *arg)
{
	struct drm_device *drm_dev = arg;
	struct rockchip_drm_private *priv = drm_dev->dev_private;
	struct drm_crtc *crtc;

	DRM_ERROR("iommu fault handler flags: 0x%x\n", flags);
	drm_for_each_crtc(crtc, drm_dev) {
		int pipe = drm_crtc_index(crtc);

		if (priv->crtc_funcs[pipe] &&
		    priv->crtc_funcs[pipe]->regs_dump)
			priv->crtc_funcs[pipe]->regs_dump(crtc, NULL);

		if (priv->crtc_funcs[pipe] &&
		    priv->crtc_funcs[pipe]->debugfs_dump)
			priv->crtc_funcs[pipe]->debugfs_dump(crtc, NULL);
	}

	return 0;
}

static int rockchip_drm_init_iommu(struct drm_device *drm_dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct iommu_domain_geometry *geometry;
	u64 start, end;
	int ret = 0;

	if (!is_support_iommu)
		return 0;

	private->domain = iommu_domain_alloc(&platform_bus_type);
	if (!private->domain)
		return -ENOMEM;

	geometry = &private->domain->geometry;
	start = geometry->aperture_start;
	end = geometry->aperture_end;

	DRM_DEBUG("IOMMU context initialized (aperture: %#llx-%#llx)\n",
		  start, end);
	drm_mm_init(&private->mm, start, end - start + 1);
	mutex_init(&private->mm_lock);

	iommu_set_fault_handler(private->domain, rockchip_drm_fault_handler,
				drm_dev);

	if (iommu_reserve_map) {
		/*
		 * At 32 bit platform size_t maximum value is 0xffffffff, SZ_4G(0x100000000) will be
		 * cliped to 0, so we split into two mapping
		 */
		ret = iommu_map(private->domain, 0, 0, (size_t)SZ_2G,
				IOMMU_WRITE | IOMMU_READ | IOMMU_PRIV);
		if (ret)
			dev_err(drm_dev->dev, "failed to create 0-2G pre mapping\n");

		ret = iommu_map(private->domain, SZ_2G, SZ_2G, (size_t)SZ_2G,
				IOMMU_WRITE | IOMMU_READ | IOMMU_PRIV);
		if (ret)
			dev_err(drm_dev->dev, "failed to create 2G-4G pre mapping\n");
	}

	return ret;
}

static void rockchip_iommu_cleanup(struct drm_device *drm_dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;

	if (!is_support_iommu)
		return;

	if (iommu_reserve_map) {
		iommu_unmap(private->domain, 0, (size_t)SZ_2G);
		iommu_unmap(private->domain, SZ_2G, (size_t)SZ_2G);
	}
	drm_mm_takedown(&private->mm);
	iommu_domain_free(private->domain);
}

#ifdef CONFIG_DEBUG_FS
static int rockchip_drm_mm_dump(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct drm_minor *minor = node->minor;
	struct drm_device *drm_dev = minor->dev;
	struct rockchip_drm_private *priv = drm_dev->dev_private;
	struct drm_printer p = drm_seq_file_printer(s);

	if (!priv->domain)
		return 0;
	mutex_lock(&priv->mm_lock);
	drm_mm_print(&priv->mm, &p);
	mutex_unlock(&priv->mm_lock);

	return 0;
}

static int rockchip_drm_summary_show(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct drm_minor *minor = node->minor;
	struct drm_device *drm_dev = minor->dev;
	struct rockchip_drm_private *priv = drm_dev->dev_private;
	struct drm_crtc *crtc;

	drm_for_each_crtc(crtc, drm_dev) {
		int pipe = drm_crtc_index(crtc);

		if (priv->crtc_funcs[pipe] &&
		    priv->crtc_funcs[pipe]->debugfs_dump)
			priv->crtc_funcs[pipe]->debugfs_dump(crtc, s);
	}

	return 0;
}

static int rockchip_drm_regs_dump(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct drm_minor *minor = node->minor;
	struct drm_device *drm_dev = minor->dev;
	struct rockchip_drm_private *priv = drm_dev->dev_private;
	struct drm_crtc *crtc;

	drm_for_each_crtc(crtc, drm_dev) {
		int pipe = drm_crtc_index(crtc);

		if (priv->crtc_funcs[pipe] &&
		    priv->crtc_funcs[pipe]->regs_dump)
			priv->crtc_funcs[pipe]->regs_dump(crtc, s);
	}

	return 0;
}

static int rockchip_drm_active_regs_dump(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct drm_minor *minor = node->minor;
	struct drm_device *drm_dev = minor->dev;
	struct rockchip_drm_private *priv = drm_dev->dev_private;
	struct drm_crtc *crtc;

	drm_for_each_crtc(crtc, drm_dev) {
		int pipe = drm_crtc_index(crtc);

		if (priv->crtc_funcs[pipe] &&
		    priv->crtc_funcs[pipe]->active_regs_dump)
			priv->crtc_funcs[pipe]->active_regs_dump(crtc, s);
	}

	return 0;
}

static struct drm_info_list rockchip_debugfs_files[] = {
	{ "active_regs", rockchip_drm_active_regs_dump, 0, NULL },
	{ "regs", rockchip_drm_regs_dump, 0, NULL },
	{ "summary", rockchip_drm_summary_show, 0, NULL },
	{ "mm_dump", rockchip_drm_mm_dump, 0, NULL },
};

static void rockchip_drm_debugfs_init(struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	struct rockchip_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc;

	drm_debugfs_create_files(rockchip_debugfs_files,
				 ARRAY_SIZE(rockchip_debugfs_files),
				 minor->debugfs_root, minor);

	drm_for_each_crtc(crtc, dev) {
		int pipe = drm_crtc_index(crtc);

		if (priv->crtc_funcs[pipe] &&
		    priv->crtc_funcs[pipe]->debugfs_init)
			priv->crtc_funcs[pipe]->debugfs_init(minor, crtc);
	}
}
#endif

static const struct drm_prop_enum_list split_area[] = {
	{ ROCKCHIP_DRM_SPLIT_UNSET, "UNSET" },
	{ ROCKCHIP_DRM_SPLIT_LEFT_SIDE, "LEFT" },
	{ ROCKCHIP_DRM_SPLIT_RIGHT_SIDE, "RIGHT" },
};

static int rockchip_drm_create_properties(struct drm_device *dev)
{
	struct drm_property *prop;
	struct rockchip_drm_private *private = dev->dev_private;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "EOTF", 0, 5);
	if (!prop)
		return -ENOMEM;
	private->eotf_prop = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "COLOR_SPACE", 0, 12);
	if (!prop)
		return -ENOMEM;
	private->color_space_prop = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "ASYNC_COMMIT", 0, 1);
	if (!prop)
		return -ENOMEM;
	private->async_commit_prop = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "SHARE_ID", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	private->share_id_prop = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_IMMUTABLE,
					 "CONNECTOR_ID", 0, 0xf);
	if (!prop)
		return -ENOMEM;
	private->connector_id_prop = prop;

	prop = drm_property_create_enum(dev, DRM_MODE_PROP_ENUM, "SPLIT_AREA",
					split_area,
					ARRAY_SIZE(split_area));
	private->split_area_prop = prop;

	prop = drm_property_create_object(dev,
					  DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_IMMUTABLE,
					  "SOC_ID", DRM_MODE_OBJECT_CRTC);
	private->soc_id_prop = prop;

	prop = drm_property_create_object(dev,
					  DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_IMMUTABLE,
					  "PORT_ID", DRM_MODE_OBJECT_CRTC);
	private->port_id_prop = prop;

	private->aclk_prop = drm_property_create_range(dev, 0, "ACLK", 0, UINT_MAX);
	private->bg_prop = drm_property_create_range(dev, 0, "BACKGROUND", 0, UINT_MAX);
	private->line_flag_prop = drm_property_create_range(dev, 0, "LINE_FLAG1", 0, UINT_MAX);
	private->cubic_lut_prop = drm_property_create(dev, DRM_MODE_PROP_BLOB, "CUBIC_LUT", 0);
	private->cubic_lut_size_prop = drm_property_create_range(dev, DRM_MODE_PROP_IMMUTABLE,
								 "CUBIC_LUT_SIZE", 0, UINT_MAX);

	return drm_mode_create_tv_properties(dev, 0, NULL);
}

static void rockchip_attach_connector_property(struct drm_device *drm)
{
	struct drm_connector *connector;
	struct drm_mode_config *conf = &drm->mode_config;
	struct drm_connector_list_iter conn_iter;

	mutex_lock(&drm->mode_config.mutex);

#define ROCKCHIP_PROP_ATTACH(prop, v) \
		drm_object_attach_property(&connector->base, prop, v)

	drm_connector_list_iter_begin(drm, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		ROCKCHIP_PROP_ATTACH(conf->tv_brightness_property, 50);
		ROCKCHIP_PROP_ATTACH(conf->tv_contrast_property, 50);
		ROCKCHIP_PROP_ATTACH(conf->tv_saturation_property, 50);
		ROCKCHIP_PROP_ATTACH(conf->tv_hue_property, 50);
	}
	drm_connector_list_iter_end(&conn_iter);
#undef ROCKCHIP_PROP_ATTACH

	mutex_unlock(&drm->mode_config.mutex);
}

static void rockchip_drm_set_property_default(struct drm_device *drm)
{
	struct drm_connector *connector;
	struct drm_mode_config *conf = &drm->mode_config;
	struct drm_atomic_state *state;
	int ret;
	struct drm_connector_list_iter conn_iter;

	drm_modeset_lock_all(drm);

	state = drm_atomic_helper_duplicate_state(drm, conf->acquire_ctx);
	if (IS_ERR(state)) {
		DRM_ERROR("failed to alloc atomic state\n");
		goto err_unlock;
	}
	state->acquire_ctx = conf->acquire_ctx;

	drm_connector_list_iter_begin(drm, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		struct drm_connector_state *connector_state;

		connector_state = drm_atomic_get_connector_state(state,
								 connector);
		if (IS_ERR(connector_state)) {
			DRM_ERROR("Connector[%d]: Failed to get state\n", connector->base.id);
			continue;
		}

		connector_state->tv.brightness = 50;
		connector_state->tv.contrast = 50;
		connector_state->tv.saturation = 50;
		connector_state->tv.hue = 50;
	}
	drm_connector_list_iter_end(&conn_iter);

	ret = drm_atomic_commit(state);
	WARN_ON(ret == -EDEADLK);
	if (ret)
		DRM_ERROR("Failed to update properties\n");
	drm_atomic_state_put(state);

err_unlock:
	drm_modeset_unlock_all(drm);
}

static int rockchip_gem_pool_init(struct drm_device *drm)
{
	struct rockchip_drm_private *private = drm->dev_private;
	struct device_node *np = drm->dev->of_node;
	struct device_node *node;
	phys_addr_t start, size;
	struct resource res;
	int ret;

	node = of_parse_phandle(np, "secure-memory-region", 0);
	if (!node)
		return -ENXIO;

	ret = of_address_to_resource(node, 0, &res);
	if (ret)
		return ret;
	start = res.start;
	size = resource_size(&res);
	if (!size)
		return -ENOMEM;

	private->secure_buffer_pool = gen_pool_create(PAGE_SHIFT, -1);
	if (!private->secure_buffer_pool)
		return -ENOMEM;

	gen_pool_add(private->secure_buffer_pool, start, size, -1);

	return 0;
}

static void rockchip_gem_pool_destroy(struct drm_device *drm)
{
	struct rockchip_drm_private *private = drm->dev_private;

	if (!private->secure_buffer_pool)
		return;

	gen_pool_destroy(private->secure_buffer_pool);
}

static int rockchip_drm_bind(struct device *dev)
{
	struct drm_device *drm_dev;
	struct rockchip_drm_private *private;
	int ret;

	drm_dev = drm_dev_alloc(&rockchip_drm_driver, dev);
	if (IS_ERR(drm_dev))
		return PTR_ERR(drm_dev);

	dev_set_drvdata(dev, drm_dev);

	private = devm_kzalloc(drm_dev->dev, sizeof(*private), GFP_KERNEL);
	if (!private) {
		ret = -ENOMEM;
		goto err_free;
	}

	mutex_init(&private->ovl_lock);

	drm_dev->dev_private = private;

	INIT_LIST_HEAD(&private->psr_list);
	mutex_init(&private->psr_list_lock);
	mutex_init(&private->commit_lock);

	private->hdmi_pll.pll = devm_clk_get_optional(dev, "hdmi-tmds-pll");
	if (PTR_ERR(private->hdmi_pll.pll) == -EPROBE_DEFER) {
		ret = -EPROBE_DEFER;
		goto err_free;
	} else if (IS_ERR(private->hdmi_pll.pll)) {
		dev_err(dev, "failed to get hdmi-tmds-pll\n");
		ret = PTR_ERR(private->hdmi_pll.pll);
		goto err_free;
	}
	private->default_pll.pll = devm_clk_get_optional(dev, "default-vop-pll");
	if (PTR_ERR(private->default_pll.pll) == -EPROBE_DEFER) {
		ret = -EPROBE_DEFER;
		goto err_free;
	} else if (IS_ERR(private->default_pll.pll)) {
		dev_err(dev, "failed to get default vop pll\n");
		ret = PTR_ERR(private->default_pll.pll);
		goto err_free;
	}

	ret = drmm_mode_config_init(drm_dev);
	if (ret)
		goto err_free;

	rockchip_drm_mode_config_init(drm_dev);
	rockchip_drm_create_properties(drm_dev);
	/* Try to bind all sub drivers. */
	ret = component_bind_all(dev, drm_dev);
	if (ret)
		goto err_mode_config_cleanup;

	rockchip_attach_connector_property(drm_dev);
	ret = drm_vblank_init(drm_dev, drm_dev->mode_config.num_crtc);
	if (ret)
		goto err_unbind_all;

	drm_mode_config_reset(drm_dev);
	rockchip_drm_set_property_default(drm_dev);

	/*
	 * enable drm irq mode.
	 * - with irq_enabled = true, we can use the vblank feature.
	 */
	drm_dev->irq_enabled = true;

	/* init kms poll for handling hpd */
	drm_kms_helper_poll_init(drm_dev);

	ret = rockchip_drm_init_iommu(drm_dev);
	if (ret)
		goto err_unbind_all;

	rockchip_gem_pool_init(drm_dev);
	ret = of_reserved_mem_device_init(drm_dev->dev);
	if (ret)
		DRM_DEBUG_KMS("No reserved memory region assign to drm\n");

	rockchip_drm_show_logo(drm_dev);

	ret = rockchip_drm_fbdev_init(drm_dev);
	if (ret)
		goto err_iommu_cleanup;

	drm_dev->mode_config.allow_fb_modifiers = true;

	ret = drm_dev_register(drm_dev, 0);
	if (ret)
		goto err_kms_helper_poll_fini;

	rockchip_clk_unprotect();

	return 0;
err_kms_helper_poll_fini:
	rockchip_gem_pool_destroy(drm_dev);
	drm_kms_helper_poll_fini(drm_dev);
	rockchip_drm_fbdev_fini(drm_dev);
err_iommu_cleanup:
	rockchip_iommu_cleanup(drm_dev);
err_unbind_all:
	component_unbind_all(dev, drm_dev);
err_mode_config_cleanup:
	drm_mode_config_cleanup(drm_dev);
err_free:
	drm_dev->dev_private = NULL;
	dev_set_drvdata(dev, NULL);
	drm_dev_put(drm_dev);
	return ret;
}

static void rockchip_drm_unbind(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	drm_dev_unregister(drm_dev);

	rockchip_drm_fbdev_fini(drm_dev);
	rockchip_gem_pool_destroy(drm_dev);
	drm_kms_helper_poll_fini(drm_dev);

	drm_atomic_helper_shutdown(drm_dev);
	component_unbind_all(dev, drm_dev);
	drm_mode_config_cleanup(drm_dev);
	rockchip_iommu_cleanup(drm_dev);

	drm_dev->dev_private = NULL;
	dev_set_drvdata(dev, NULL);
	drm_dev_put(drm_dev);
}

static void rockchip_drm_crtc_cancel_pending_vblank(struct drm_crtc *crtc,
						    struct drm_file *file_priv)
{
	struct rockchip_drm_private *priv = crtc->dev->dev_private;
	int pipe = drm_crtc_index(crtc);

	if (pipe < ROCKCHIP_MAX_CRTC &&
	    priv->crtc_funcs[pipe] &&
	    priv->crtc_funcs[pipe]->cancel_pending_vblank)
		priv->crtc_funcs[pipe]->cancel_pending_vblank(crtc, file_priv);
}

static int rockchip_drm_open(struct drm_device *dev, struct drm_file *file)
{
	struct drm_crtc *crtc;

	drm_for_each_crtc(crtc, dev)
		crtc->primary->fb = NULL;

	return 0;
}

static void rockchip_drm_postclose(struct drm_device *dev,
				   struct drm_file *file_priv)
{
	struct drm_crtc *crtc;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head)
		rockchip_drm_crtc_cancel_pending_vblank(crtc, file_priv);
}

static void rockchip_drm_lastclose(struct drm_device *dev)
{
	struct rockchip_drm_private *priv = dev->dev_private;

	if (!priv->logo)
		drm_fb_helper_restore_fbdev_mode_unlocked(priv->fbdev_helper);
}

static struct drm_pending_vblank_event *
rockchip_drm_add_vcnt_event(struct drm_crtc *crtc, union drm_wait_vblank *vblwait,
			    struct drm_file *file_priv)
{
	struct drm_pending_vblank_event *e;
	struct drm_device *dev = crtc->dev;
	unsigned long flags;

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e)
		return NULL;

	e->pipe = drm_crtc_index(crtc);
	e->event.base.type = DRM_EVENT_ROCKCHIP_CRTC_VCNT;
	e->event.base.length = sizeof(e->event.vbl);
	e->event.vbl.crtc_id = crtc->base.id;
	e->event.vbl.user_data = vblwait->request.signal;

	spin_lock_irqsave(&dev->event_lock, flags);
	drm_event_reserve_init_locked(dev, file_priv, &e->base, &e->event.base);
	spin_unlock_irqrestore(&dev->event_lock, flags);

	return e;
}

static int rockchip_drm_get_vcnt_event_ioctl(struct drm_device *dev, void *data,
					     struct drm_file *file_priv)
{
	struct rockchip_drm_private *priv = dev->dev_private;
	union drm_wait_vblank *vblwait = data;
	struct drm_pending_vblank_event *e;
	struct drm_crtc *crtc;
	unsigned int flags, pipe;

	flags = vblwait->request.type & (_DRM_VBLANK_FLAGS_MASK | _DRM_ROCKCHIP_VCNT_EVENT);
	pipe = (vblwait->request.type & _DRM_VBLANK_HIGH_CRTC_MASK);
	if (pipe)
		pipe = pipe >> _DRM_VBLANK_HIGH_CRTC_SHIFT;
	else
		pipe = flags & _DRM_VBLANK_SECONDARY ? 1 : 0;

	crtc = drm_crtc_from_index(dev, pipe);

	if (flags & _DRM_ROCKCHIP_VCNT_EVENT) {
		e = rockchip_drm_add_vcnt_event(crtc, vblwait, file_priv);
		priv->vcnt[pipe].event = e;
	}

	return 0;
}

static const struct drm_ioctl_desc rockchip_ioctls[] = {
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GEM_CREATE, rockchip_gem_create_ioctl,
			  DRM_UNLOCKED | DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GEM_MAP_OFFSET,
			  rockchip_gem_map_offset_ioctl,
			  DRM_UNLOCKED | DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GEM_GET_PHYS, rockchip_gem_get_phys_ioctl,
			  DRM_UNLOCKED | DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GET_VCNT_EVENT, rockchip_drm_get_vcnt_event_ioctl,
			  DRM_UNLOCKED),
};

static const struct file_operations rockchip_drm_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.mmap = rockchip_gem_mmap,
	.poll = drm_poll,
	.read = drm_read,
	.unlocked_ioctl = drm_ioctl,
	.compat_ioctl = drm_compat_ioctl,
	.release = drm_release,
};

static int rockchip_drm_gem_dmabuf_begin_cpu_access(struct dma_buf *dma_buf,
						    enum dma_data_direction dir)
{
	struct drm_gem_object *obj = dma_buf->priv;

	return rockchip_gem_prime_begin_cpu_access(obj, dir);
}

static int rockchip_drm_gem_dmabuf_end_cpu_access(struct dma_buf *dma_buf,
						  enum dma_data_direction dir)
{
	struct drm_gem_object *obj = dma_buf->priv;

	return rockchip_gem_prime_end_cpu_access(obj, dir);
}

static const struct dma_buf_ops rockchip_drm_gem_prime_dmabuf_ops = {
	.cache_sgt_mapping = true,
	.attach = drm_gem_map_attach,
	.detach = drm_gem_map_detach,
	.map_dma_buf = drm_gem_map_dma_buf,
	.unmap_dma_buf = drm_gem_unmap_dma_buf,
	.release = drm_gem_dmabuf_release,
	.mmap = drm_gem_dmabuf_mmap,
	.vmap = drm_gem_dmabuf_vmap,
	.vunmap = drm_gem_dmabuf_vunmap,
	.get_uuid = drm_gem_dmabuf_get_uuid,
	.begin_cpu_access = rockchip_drm_gem_dmabuf_begin_cpu_access,
	.end_cpu_access = rockchip_drm_gem_dmabuf_end_cpu_access,
};

static struct drm_gem_object *rockchip_drm_gem_prime_import_dev(struct drm_device *dev,
								struct dma_buf *dma_buf,
								struct device *attach_dev)
{
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct drm_gem_object *obj;
	int ret;

	if (dma_buf->ops == &rockchip_drm_gem_prime_dmabuf_ops) {
		obj = dma_buf->priv;
		if (obj->dev == dev) {
			/*
			 * Importing dmabuf exported from out own gem increases
			 * refcount on gem itself instead of f_count of dmabuf.
			 */
			drm_gem_object_get(obj);
			return obj;
		}
	}

	if (!dev->driver->gem_prime_import_sg_table)
		return ERR_PTR(-EINVAL);

	attach = dma_buf_attach(dma_buf, attach_dev);
	if (IS_ERR(attach))
		return ERR_CAST(attach);

	get_dma_buf(dma_buf);

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto fail_detach;
	}

	obj = dev->driver->gem_prime_import_sg_table(dev, attach, sgt);
	if (IS_ERR(obj)) {
		ret = PTR_ERR(obj);
		goto fail_unmap;
	}

	obj->import_attach = attach;
	obj->resv = dma_buf->resv;

	return obj;

fail_unmap:
	dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
fail_detach:
	dma_buf_detach(dma_buf, attach);
	dma_buf_put(dma_buf);

	return ERR_PTR(ret);
}

static struct drm_gem_object *rockchip_drm_gem_prime_import(struct drm_device *dev,
							    struct dma_buf *dma_buf)
{
	return rockchip_drm_gem_prime_import_dev(dev, dma_buf, dev->dev);
}

static struct dma_buf *rockchip_drm_gem_prime_export(struct drm_gem_object *obj,
						     int flags)
{
	struct drm_device *dev = obj->dev;
	struct dma_buf_export_info exp_info = {
		.exp_name = KBUILD_MODNAME, /* white lie for debug */
		.owner = dev->driver->fops->owner,
		.ops = &rockchip_drm_gem_prime_dmabuf_ops,
		.size = obj->size,
		.flags = flags,
		.priv = obj,
		.resv = obj->resv,
	};

	return drm_gem_dmabuf_export(dev, &exp_info);
}

static struct drm_driver rockchip_drm_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC | DRIVER_RENDER,
	.postclose		= rockchip_drm_postclose,
	.lastclose		= rockchip_drm_lastclose,
	.open			= rockchip_drm_open,
	.gem_vm_ops		= &drm_gem_cma_vm_ops,
	.gem_free_object_unlocked = rockchip_gem_free_object,
	.dumb_create		= rockchip_gem_dumb_create,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_import	= rockchip_drm_gem_prime_import,
	.gem_prime_export	= rockchip_drm_gem_prime_export,
	.gem_prime_get_sg_table	= rockchip_gem_prime_get_sg_table,
	.gem_prime_import_sg_table	= rockchip_gem_prime_import_sg_table,
	.gem_prime_vmap		= rockchip_gem_prime_vmap,
	.gem_prime_vunmap	= rockchip_gem_prime_vunmap,
	.gem_prime_mmap		= rockchip_gem_mmap_buf,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init		= rockchip_drm_debugfs_init,
#endif
	.ioctls			= rockchip_ioctls,
	.num_ioctls		= ARRAY_SIZE(rockchip_ioctls),
	.fops			= &rockchip_drm_driver_fops,
	.name	= DRIVER_NAME,
	.desc	= DRIVER_DESC,
	.date	= DRIVER_DATE,
	.major	= DRIVER_MAJOR,
	.minor	= DRIVER_MINOR,
};

#ifdef CONFIG_PM_SLEEP
static int rockchip_drm_sys_suspend(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(drm);
}

static int rockchip_drm_sys_resume(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	return drm_mode_config_helper_resume(drm);
}
#endif

static const struct dev_pm_ops rockchip_drm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rockchip_drm_sys_suspend,
				rockchip_drm_sys_resume)
};

#define MAX_ROCKCHIP_SUB_DRIVERS 16
static struct platform_driver *rockchip_sub_drivers[MAX_ROCKCHIP_SUB_DRIVERS];
static int num_rockchip_sub_drivers;

/*
 * Check if a vop endpoint is leading to a rockchip subdriver or bridge.
 * Should be called from the component bind stage of the drivers
 * to ensure that all subdrivers are probed.
 *
 * @ep: endpoint of a rockchip vop
 *
 * returns true if subdriver, false if external bridge and -ENODEV
 * if remote port does not contain a device.
 */
int rockchip_drm_endpoint_is_subdriver(struct device_node *ep)
{
	struct device_node *node = of_graph_get_remote_port_parent(ep);
	struct platform_device *pdev;
	struct device_driver *drv;
	int i;

	if (!node)
		return -ENODEV;

	/* status disabled will prevent creation of platform-devices */
	pdev = of_find_device_by_node(node);
	of_node_put(node);
	if (!pdev)
		return -ENODEV;

	/*
	 * All rockchip subdrivers have probed at this point, so
	 * any device not having a driver now is an external bridge.
	 */
	drv = pdev->dev.driver;
	if (!drv) {
		platform_device_put(pdev);
		return false;
	}

	for (i = 0; i < num_rockchip_sub_drivers; i++) {
		if (rockchip_sub_drivers[i] == to_platform_driver(drv)) {
			platform_device_put(pdev);
			return true;
		}
	}

	platform_device_put(pdev);
	return false;
}

static int compare_dev(struct device *dev, void *data)
{
	return dev == (struct device *)data;
}

static void rockchip_drm_match_remove(struct device *dev)
{
	struct device_link *link;

	list_for_each_entry(link, &dev->links.consumers, s_node)
		device_link_del(link);
}

static struct component_match *rockchip_drm_match_add(struct device *dev)
{
	struct component_match *match = NULL;
	int i;

	for (i = 0; i < num_rockchip_sub_drivers; i++) {
		struct platform_driver *drv = rockchip_sub_drivers[i];
		struct device *p = NULL, *d;

		do {
			d = platform_find_device_by_driver(p, &drv->driver);
			put_device(p);
			p = d;

			if (!d)
				break;

			device_link_add(dev, d, DL_FLAG_STATELESS);
			component_match_add(dev, &match, compare_dev, d);
		} while (true);
	}

	if (IS_ERR(match))
		rockchip_drm_match_remove(dev);

	return match ?: ERR_PTR(-ENODEV);
}

static const struct component_master_ops rockchip_drm_ops = {
	.bind = rockchip_drm_bind,
	.unbind = rockchip_drm_unbind,
};

static int rockchip_drm_platform_of_probe(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct device_node *port;
	bool found = false;
	int i;

	if (!np)
		return -ENODEV;

	for (i = 0;; i++) {
		struct device_node *iommu;

		port = of_parse_phandle(np, "ports", i);
		if (!port)
			break;

		if (!of_device_is_available(port->parent)) {
			of_node_put(port);
			continue;
		}

		iommu = of_parse_phandle(port->parent, "iommus", 0);
		if (!iommu || !of_device_is_available(iommu)) {
			DRM_DEV_DEBUG(dev,
				      "no iommu attached for %pOF, using non-iommu buffers\n",
				      port->parent);
			/*
			 * if there is a crtc not support iommu, force set all
			 * crtc use non-iommu buffer.
			 */
			is_support_iommu = false;
		}

		found = true;

		iommu_reserve_map |= of_property_read_bool(iommu, "rockchip,reserve-map");
		of_node_put(iommu);
		of_node_put(port);
	}

	if (i == 0) {
		DRM_DEV_ERROR(dev, "missing 'ports' property\n");
		return -ENODEV;
	}

	if (!found) {
		DRM_DEV_ERROR(dev,
			      "No available vop found for display-subsystem.\n");
		return -ENODEV;
	}

	return 0;
}

static int rockchip_drm_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct component_match *match = NULL;
	int ret;

	ret = rockchip_drm_platform_of_probe(dev);
#if !IS_ENABLED(CONFIG_DRM_ROCKCHIP_VVOP)
	if (ret)
		return ret;
#endif

	match = rockchip_drm_match_add(dev);
	if (IS_ERR(match))
		return PTR_ERR(match);

	ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret)
		goto err;

	ret = component_master_add_with_match(dev, &rockchip_drm_ops, match);
	if (ret < 0)
		goto err;

	return 0;
err:
	rockchip_drm_match_remove(dev);

	return ret;
}

static int rockchip_drm_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &rockchip_drm_ops);

	rockchip_drm_match_remove(&pdev->dev);

	return 0;
}

static void rockchip_drm_platform_shutdown(struct platform_device *pdev)
{
	struct drm_device *drm = platform_get_drvdata(pdev);

	if (drm)
		drm_atomic_helper_shutdown(drm);
}

static const struct of_device_id rockchip_drm_dt_ids[] = {
	{ .compatible = "rockchip,display-subsystem", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, rockchip_drm_dt_ids);

static struct platform_driver rockchip_drm_platform_driver = {
	.probe = rockchip_drm_platform_probe,
	.remove = rockchip_drm_platform_remove,
	.shutdown = rockchip_drm_platform_shutdown,
	.driver = {
		.name = "rockchip-drm",
		.of_match_table = rockchip_drm_dt_ids,
		.pm = &rockchip_drm_pm_ops,
	},
};

#define ADD_ROCKCHIP_SUB_DRIVER(drv, cond) { \
	if (IS_ENABLED(cond) && \
	    !WARN_ON(num_rockchip_sub_drivers >= MAX_ROCKCHIP_SUB_DRIVERS)) \
		rockchip_sub_drivers[num_rockchip_sub_drivers++] = &drv; \
}

static int __init rockchip_drm_init(void)
{
	int ret;

	num_rockchip_sub_drivers = 0;
#if IS_ENABLED(CONFIG_DRM_ROCKCHIP_VVOP)
	ADD_ROCKCHIP_SUB_DRIVER(vvop_platform_driver, CONFIG_DRM_ROCKCHIP_VVOP);
#else
	ADD_ROCKCHIP_SUB_DRIVER(vop_platform_driver, CONFIG_ROCKCHIP_VOP);
	ADD_ROCKCHIP_SUB_DRIVER(vop2_platform_driver, CONFIG_ROCKCHIP_VOP2);
	ADD_ROCKCHIP_SUB_DRIVER(vconn_platform_driver, CONFIG_ROCKCHIP_VCONN);
	ADD_ROCKCHIP_SUB_DRIVER(rockchip_lvds_driver,
				CONFIG_ROCKCHIP_LVDS);
	ADD_ROCKCHIP_SUB_DRIVER(rockchip_dp_driver,
				CONFIG_ROCKCHIP_ANALOGIX_DP);
	ADD_ROCKCHIP_SUB_DRIVER(cdn_dp_driver, CONFIG_ROCKCHIP_CDN_DP);
	ADD_ROCKCHIP_SUB_DRIVER(dw_hdmi_rockchip_pltfm_driver,
				CONFIG_ROCKCHIP_DW_HDMI);
	ADD_ROCKCHIP_SUB_DRIVER(dw_mipi_dsi_rockchip_driver,
				CONFIG_ROCKCHIP_DW_MIPI_DSI);
	ADD_ROCKCHIP_SUB_DRIVER(dw_mipi_dsi2_rockchip_driver,
				CONFIG_ROCKCHIP_DW_MIPI_DSI);
	ADD_ROCKCHIP_SUB_DRIVER(inno_hdmi_driver, CONFIG_ROCKCHIP_INNO_HDMI);
	ADD_ROCKCHIP_SUB_DRIVER(rk3066_hdmi_driver,
				CONFIG_ROCKCHIP_RK3066_HDMI);
	ADD_ROCKCHIP_SUB_DRIVER(rockchip_rgb_driver, CONFIG_ROCKCHIP_RGB);
	ADD_ROCKCHIP_SUB_DRIVER(rockchip_tve_driver, CONFIG_ROCKCHIP_DRM_TVE);
	ADD_ROCKCHIP_SUB_DRIVER(dw_dp_driver, CONFIG_ROCKCHIP_DW_DP);

#endif
	ret = platform_register_drivers(rockchip_sub_drivers,
					num_rockchip_sub_drivers);
	if (ret)
		return ret;

	ret = platform_driver_register(&rockchip_drm_platform_driver);
	if (ret)
		goto err_unreg_drivers;

	rockchip_gem_get_ddr_info();

	return 0;

err_unreg_drivers:
	platform_unregister_drivers(rockchip_sub_drivers,
				    num_rockchip_sub_drivers);
	return ret;
}

static void __exit rockchip_drm_fini(void)
{
	platform_driver_unregister(&rockchip_drm_platform_driver);

	platform_unregister_drivers(rockchip_sub_drivers,
				    num_rockchip_sub_drivers);
}

#ifdef CONFIG_VIDEO_REVERSE_IMAGE
fs_initcall(rockchip_drm_init);
#else
module_init(rockchip_drm_init);
#endif
module_exit(rockchip_drm_fini);

MODULE_AUTHOR("Mark Yao <mark.yao@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP DRM Driver");
MODULE_LICENSE("GPL v2");

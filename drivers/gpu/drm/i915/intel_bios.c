/*
 * Copyright © 2006 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */
#include <linux/dmi.h>
#include <drm/drm_dp_helper.h>
#include <drm/drmP.h>
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include "intel_bios.h"

#define	SLAVE_ADDR1	0x70
#define	SLAVE_ADDR2	0x72

static int panel_type;

static void *
find_section(struct bdb_header *bdb, int section_id)
{
	u8 *base = (u8 *)bdb;
	int index = 0;
	u16 total, current_size;
	u8 current_id;

	/* skip to first section */
	index += bdb->header_size;
	total = bdb->bdb_size;

	/* walk the sections looking for section_id */
	while (index + 3 < total) {
		current_id = *(base + index);
		index++;

		current_size = *((u16 *)(base + index));
		index += 2;

		if (index + current_size > total)
			return NULL;

		if (current_id == section_id)
			return base + index;

		index += current_size;
	}

	return NULL;
}

static u16
get_blocksize(void *p)
{
	u16 *block_ptr, block_size;

	block_ptr = (u16 *)((char *)p - 2);
	block_size = *block_ptr;
	return block_size;
}

static void
fill_detail_timing_data(struct drm_display_mode *panel_fixed_mode,
			const struct lvds_dvo_timing *dvo_timing)
{
	panel_fixed_mode->hdisplay = (dvo_timing->hactive_hi << 8) |
		dvo_timing->hactive_lo;
	panel_fixed_mode->hsync_start = panel_fixed_mode->hdisplay +
		((dvo_timing->hsync_off_hi << 8) | dvo_timing->hsync_off_lo);
	panel_fixed_mode->hsync_end = panel_fixed_mode->hsync_start +
		dvo_timing->hsync_pulse_width;
	panel_fixed_mode->htotal = panel_fixed_mode->hdisplay +
		((dvo_timing->hblank_hi << 8) | dvo_timing->hblank_lo);

	panel_fixed_mode->vdisplay = (dvo_timing->vactive_hi << 8) |
		dvo_timing->vactive_lo;
	panel_fixed_mode->vsync_start = panel_fixed_mode->vdisplay +
		dvo_timing->vsync_off;
	panel_fixed_mode->vsync_end = panel_fixed_mode->vsync_start +
		dvo_timing->vsync_pulse_width;
	panel_fixed_mode->vtotal = panel_fixed_mode->vdisplay +
		((dvo_timing->vblank_hi << 8) | dvo_timing->vblank_lo);
	panel_fixed_mode->clock = dvo_timing->clock * 10;
	panel_fixed_mode->type = DRM_MODE_TYPE_PREFERRED;

	if (dvo_timing->hsync_positive)
		panel_fixed_mode->flags |= DRM_MODE_FLAG_PHSYNC;
	else
		panel_fixed_mode->flags |= DRM_MODE_FLAG_NHSYNC;

	if (dvo_timing->vsync_positive)
		panel_fixed_mode->flags |= DRM_MODE_FLAG_PVSYNC;
	else
		panel_fixed_mode->flags |= DRM_MODE_FLAG_NVSYNC;

	/* Some VBTs have bogus h/vtotal values */
	if (panel_fixed_mode->hsync_end > panel_fixed_mode->htotal)
		panel_fixed_mode->htotal = panel_fixed_mode->hsync_end + 1;
	if (panel_fixed_mode->vsync_end > panel_fixed_mode->vtotal)
		panel_fixed_mode->vtotal = panel_fixed_mode->vsync_end + 1;

	drm_mode_set_name(panel_fixed_mode);
}

static bool
lvds_dvo_timing_equal_size(const struct lvds_dvo_timing *a,
			   const struct lvds_dvo_timing *b)
{
	if (a->hactive_hi != b->hactive_hi ||
	    a->hactive_lo != b->hactive_lo)
		return false;

	if (a->hsync_off_hi != b->hsync_off_hi ||
	    a->hsync_off_lo != b->hsync_off_lo)
		return false;

	if (a->hsync_pulse_width != b->hsync_pulse_width)
		return false;

	if (a->hblank_hi != b->hblank_hi ||
	    a->hblank_lo != b->hblank_lo)
		return false;

	if (a->vactive_hi != b->vactive_hi ||
	    a->vactive_lo != b->vactive_lo)
		return false;

	if (a->vsync_off != b->vsync_off)
		return false;

	if (a->vsync_pulse_width != b->vsync_pulse_width)
		return false;

	if (a->vblank_hi != b->vblank_hi ||
	    a->vblank_lo != b->vblank_lo)
		return false;

	return true;
}

static const struct lvds_dvo_timing *
get_lvds_dvo_timing(const struct bdb_lvds_lfp_data *lvds_lfp_data,
		    const struct bdb_lvds_lfp_data_ptrs *lvds_lfp_data_ptrs,
		    int index)
{
	/*
	 * the size of fp_timing varies on the different platform.
	 * So calculate the DVO timing relative offset in LVDS data
	 * entry to get the DVO timing entry
	 */

	int lfp_data_size =
		lvds_lfp_data_ptrs->ptr[1].dvo_timing_offset -
		lvds_lfp_data_ptrs->ptr[0].dvo_timing_offset;
	int dvo_timing_offset =
		lvds_lfp_data_ptrs->ptr[0].dvo_timing_offset -
		lvds_lfp_data_ptrs->ptr[0].fp_timing_offset;
	char *entry = (char *)lvds_lfp_data->data + lfp_data_size * index;

	return (struct lvds_dvo_timing *)(entry + dvo_timing_offset);
}

/* get lvds_fp_timing entry
 * this function may return NULL if the corresponding entry is invalid
 */
static const struct lvds_fp_timing *
get_lvds_fp_timing(const struct bdb_header *bdb,
		   const struct bdb_lvds_lfp_data *data,
		   const struct bdb_lvds_lfp_data_ptrs *ptrs,
		   int index)
{
	size_t data_ofs = (const u8 *)data - (const u8 *)bdb;
	u16 data_size = ((const u16 *)data)[-1]; /* stored in header */
	size_t ofs;

	if (index >= ARRAY_SIZE(ptrs->ptr))
		return NULL;
	ofs = ptrs->ptr[index].fp_timing_offset;
	if (ofs < data_ofs ||
	    ofs + sizeof(struct lvds_fp_timing) > data_ofs + data_size)
		return NULL;
	return (const struct lvds_fp_timing *)((const u8 *)bdb + ofs);
}

/* Try to find integrated panel data */
static void
parse_lfp_panel_data(struct drm_i915_private *dev_priv,
			    struct bdb_header *bdb)
{
	const struct bdb_lvds_options *lvds_options;
	const struct bdb_lvds_lfp_data *lvds_lfp_data;
	const struct bdb_lvds_lfp_data_ptrs *lvds_lfp_data_ptrs;
	const struct lvds_dvo_timing *panel_dvo_timing;
	const struct lvds_fp_timing *fp_timing;
	struct drm_display_mode *panel_fixed_mode;
	int i, downclock, drrs_mode;

	lvds_options = find_section(bdb, BDB_LVDS_OPTIONS);
	if (!lvds_options)
		return;

	dev_priv->vbt.lvds_dither = lvds_options->pixel_dither;
	if (lvds_options->panel_type == 0xff)
		return;

	panel_type = lvds_options->panel_type;

	drrs_mode = (lvds_options->dps_panel_type_bits
				>> (panel_type * 2)) & MODE_MASK;
	/*
	 * VBT has static DRRS = 0 and seamless DRRS = 2.
	 * The below piece of code is required to adjust vbt.drrs_type
	 * to match the enum drrs_support_type.
	 */
	switch (drrs_mode) {
	case 0:
		dev_priv->vbt.drrs_type = STATIC_DRRS_SUPPORT;
		DRM_DEBUG_KMS("DRRS supported mode is static\n");
		break;
	case 2:
		dev_priv->vbt.drrs_type = SEAMLESS_DRRS_SUPPORT;
		DRM_DEBUG_KMS("DRRS supported mode is seamless\n");
		break;
	default:
		dev_priv->vbt.drrs_type = DRRS_NOT_SUPPORTED;
		DRM_DEBUG_KMS("DRRS not supported (VBT input)\n");
		break;
	}

	lvds_lfp_data = find_section(bdb, BDB_LVDS_LFP_DATA);
	if (!lvds_lfp_data)
		return;

	lvds_lfp_data_ptrs = find_section(bdb, BDB_LVDS_LFP_DATA_PTRS);
	if (!lvds_lfp_data_ptrs)
		return;

	dev_priv->vbt.lvds_vbt = 1;

	panel_dvo_timing = get_lvds_dvo_timing(lvds_lfp_data,
					       lvds_lfp_data_ptrs,
					       lvds_options->panel_type);

	panel_fixed_mode = kzalloc(sizeof(*panel_fixed_mode), GFP_KERNEL);
	if (!panel_fixed_mode)
		return;

	fill_detail_timing_data(panel_fixed_mode, panel_dvo_timing);

	dev_priv->vbt.lfp_lvds_vbt_mode = panel_fixed_mode;

	DRM_DEBUG_KMS("Found panel mode in BIOS VBT tables:\n");
	drm_mode_debug_printmodeline(panel_fixed_mode);

	/*
	 * Iterate over the LVDS panel timing info to find the lowest clock
	 * for the native resolution.
	 */
	downclock = panel_dvo_timing->clock;
	for (i = 0; i < 16; i++) {
		const struct lvds_dvo_timing *dvo_timing;

		dvo_timing = get_lvds_dvo_timing(lvds_lfp_data,
						 lvds_lfp_data_ptrs,
						 i);
		if (lvds_dvo_timing_equal_size(dvo_timing, panel_dvo_timing) &&
		    dvo_timing->clock < downclock)
			downclock = dvo_timing->clock;
	}

	if (downclock < panel_dvo_timing->clock && i915.lvds_downclock) {
		dev_priv->lvds_downclock_avail = 1;
		dev_priv->lvds_downclock = downclock * 10;
		DRM_DEBUG_KMS("LVDS downclock is found in VBT. "
			      "Normal Clock %dKHz, downclock %dKHz\n",
			      panel_fixed_mode->clock, 10*downclock);
	}

	fp_timing = get_lvds_fp_timing(bdb, lvds_lfp_data,
				       lvds_lfp_data_ptrs,
				       lvds_options->panel_type);
	if (fp_timing) {
		/* check the resolution, just to be sure */
		if (fp_timing->x_res == panel_fixed_mode->hdisplay &&
		    fp_timing->y_res == panel_fixed_mode->vdisplay) {
			dev_priv->vbt.bios_lvds_val = fp_timing->lvds_reg_val;
			DRM_DEBUG_KMS("VBT initial LVDS value %x\n",
				      dev_priv->vbt.bios_lvds_val);
		}
	}
}

static void
parse_lfp_backlight(struct drm_i915_private *dev_priv, struct bdb_header *bdb)
{
	const struct bdb_lfp_backlight_data *backlight_data;
	const struct bdb_lfp_backlight_data_entry *entry;

	backlight_data = find_section(bdb, BDB_LVDS_BACKLIGHT);
	if (!backlight_data)
		return;

	if (backlight_data->entry_size != sizeof(backlight_data->data[0])) {
		DRM_DEBUG_KMS("Unsupported backlight data entry size %u\n",
			      backlight_data->entry_size);
		return;
	}

	entry = &backlight_data->data[panel_type];

	dev_priv->vbt.backlight.present = entry->type == BDB_BACKLIGHT_TYPE_PWM;
	if (!dev_priv->vbt.backlight.present) {
		DRM_DEBUG_KMS("PWM backlight not present in VBT (type %u)\n",
			      entry->type);
		return;
	}

	dev_priv->vbt.backlight.pwm_freq_hz = entry->pwm_freq_hz;
	dev_priv->vbt.backlight.active_low_pwm = entry->active_low_pwm;
	dev_priv->vbt.backlight.min_brightness = entry->min_brightness;
	DRM_DEBUG_KMS("VBT backlight PWM modulation frequency %u Hz, "
		      "active %s, min brightness %u, level %u\n",
		      dev_priv->vbt.backlight.pwm_freq_hz,
		      dev_priv->vbt.backlight.active_low_pwm ? "low" : "high",
		      dev_priv->vbt.backlight.min_brightness,
		      backlight_data->level[panel_type]);
}

/* Try to find sdvo panel data */
static void
parse_sdvo_panel_data(struct drm_i915_private *dev_priv,
		      struct bdb_header *bdb)
{
	struct lvds_dvo_timing *dvo_timing;
	struct drm_display_mode *panel_fixed_mode;
	int index;

	index = i915.vbt_sdvo_panel_type;
	if (index == -2) {
		DRM_DEBUG_KMS("Ignore SDVO panel mode from BIOS VBT tables.\n");
		return;
	}

	if (index == -1) {
		struct bdb_sdvo_lvds_options *sdvo_lvds_options;

		sdvo_lvds_options = find_section(bdb, BDB_SDVO_LVDS_OPTIONS);
		if (!sdvo_lvds_options)
			return;

		index = sdvo_lvds_options->panel_type;
	}

	dvo_timing = find_section(bdb, BDB_SDVO_PANEL_DTDS);
	if (!dvo_timing)
		return;

	panel_fixed_mode = kzalloc(sizeof(*panel_fixed_mode), GFP_KERNEL);
	if (!panel_fixed_mode)
		return;

	fill_detail_timing_data(panel_fixed_mode, dvo_timing + index);

	dev_priv->vbt.sdvo_lvds_vbt_mode = panel_fixed_mode;

	DRM_DEBUG_KMS("Found SDVO panel mode in BIOS VBT tables:\n");
	drm_mode_debug_printmodeline(panel_fixed_mode);
}

static int intel_bios_ssc_frequency(struct drm_device *dev,
				    bool alternate)
{
	switch (INTEL_INFO(dev)->gen) {
	case 2:
		return alternate ? 66667 : 48000;
	case 3:
	case 4:
		return alternate ? 100000 : 96000;
	default:
		return alternate ? 100000 : 120000;
	}
}

static void
parse_general_features(struct drm_i915_private *dev_priv,
		       struct bdb_header *bdb)
{
	struct drm_device *dev = dev_priv->dev;
	struct bdb_general_features *general;

	general = find_section(bdb, BDB_GENERAL_FEATURES);
	if (general) {
		dev_priv->vbt.int_tv_support = general->int_tv_support;
		dev_priv->vbt.int_crt_support = general->int_crt_support;
		dev_priv->vbt.lvds_use_ssc = general->enable_ssc;
		dev_priv->vbt.lvds_ssc_freq =
			intel_bios_ssc_frequency(dev, general->ssc_freq);
		dev_priv->vbt.display_clock_mode = general->display_clock_mode;
		dev_priv->vbt.fdi_rx_polarity_inverted = general->fdi_rx_polarity_inverted;
		DRM_DEBUG_KMS("BDB_GENERAL_FEATURES int_tv_support %d int_crt_support %d lvds_use_ssc %d lvds_ssc_freq %d display_clock_mode %d fdi_rx_polarity_inverted %d\n",
			      dev_priv->vbt.int_tv_support,
			      dev_priv->vbt.int_crt_support,
			      dev_priv->vbt.lvds_use_ssc,
			      dev_priv->vbt.lvds_ssc_freq,
			      dev_priv->vbt.display_clock_mode,
			      dev_priv->vbt.fdi_rx_polarity_inverted);
	}
}

static void
parse_general_definitions(struct drm_i915_private *dev_priv,
			  struct bdb_header *bdb)
{
	struct bdb_general_definitions *general;

	general = find_section(bdb, BDB_GENERAL_DEFINITIONS);
	if (general) {
		u16 block_size = get_blocksize(general);
		if (block_size >= sizeof(*general)) {
			int bus_pin = general->crt_ddc_gmbus_pin;
			DRM_DEBUG_KMS("crt_ddc_bus_pin: %d\n", bus_pin);
			if (intel_gmbus_is_valid_pin(dev_priv, bus_pin))
				dev_priv->vbt.crt_ddc_pin = bus_pin;
		} else {
			DRM_DEBUG_KMS("BDB_GD too small (%d). Invalid.\n",
				      block_size);
		}
	}
}

static void
parse_sdvo_device_mapping(struct drm_i915_private *dev_priv,
			  struct bdb_header *bdb)
{
	struct sdvo_device_mapping *p_mapping;
	struct bdb_general_definitions *p_defs;
	union child_device_config *p_child;
	int i, child_device_num, count;
	u16	block_size;

	p_defs = find_section(bdb, BDB_GENERAL_DEFINITIONS);
	if (!p_defs) {
		DRM_DEBUG_KMS("No general definition block is found, unable to construct sdvo mapping.\n");
		return;
	}
	/* judge whether the size of child device meets the requirements.
	 * If the child device size obtained from general definition block
	 * is different with sizeof(struct child_device_config), skip the
	 * parsing of sdvo device info
	 */
	if (p_defs->child_dev_size != sizeof(*p_child)) {
		/* different child dev size . Ignore it */
		DRM_DEBUG_KMS("different child size is found. Invalid.\n");
		return;
	}
	/* get the block size of general definitions */
	block_size = get_blocksize(p_defs);
	/* get the number of child device */
	child_device_num = (block_size - sizeof(*p_defs)) /
				sizeof(*p_child);
	count = 0;
	for (i = 0; i < child_device_num; i++) {
		p_child = &(p_defs->devices[i]);
		if (!p_child->old.device_type) {
			/* skip the device block if device type is invalid */
			continue;
		}
		if (p_child->old.slave_addr != SLAVE_ADDR1 &&
			p_child->old.slave_addr != SLAVE_ADDR2) {
			/*
			 * If the slave address is neither 0x70 nor 0x72,
			 * it is not a SDVO device. Skip it.
			 */
			continue;
		}
		if (p_child->old.dvo_port != DEVICE_PORT_DVOB &&
			p_child->old.dvo_port != DEVICE_PORT_DVOC) {
			/* skip the incorrect SDVO port */
			DRM_DEBUG_KMS("Incorrect SDVO port. Skip it\n");
			continue;
		}
		DRM_DEBUG_KMS("the SDVO device with slave addr %2x is found on"
				" %s port\n",
				p_child->old.slave_addr,
				(p_child->old.dvo_port == DEVICE_PORT_DVOB) ?
					"SDVOB" : "SDVOC");
		p_mapping = &(dev_priv->sdvo_mappings[p_child->old.dvo_port - 1]);
		if (!p_mapping->initialized) {
			p_mapping->dvo_port = p_child->old.dvo_port;
			p_mapping->slave_addr = p_child->old.slave_addr;
			p_mapping->dvo_wiring = p_child->old.dvo_wiring;
			p_mapping->ddc_pin = p_child->old.ddc_pin;
			p_mapping->i2c_pin = p_child->old.i2c_pin;
			p_mapping->initialized = 1;
			DRM_DEBUG_KMS("SDVO device: dvo=%x, addr=%x, wiring=%d, ddc_pin=%d, i2c_pin=%d\n",
				      p_mapping->dvo_port,
				      p_mapping->slave_addr,
				      p_mapping->dvo_wiring,
				      p_mapping->ddc_pin,
				      p_mapping->i2c_pin);
		} else {
			DRM_DEBUG_KMS("Maybe one SDVO port is shared by "
					 "two SDVO device.\n");
		}
		if (p_child->old.slave2_addr) {
			/* Maybe this is a SDVO device with multiple inputs */
			/* And the mapping info is not added */
			DRM_DEBUG_KMS("there exists the slave2_addr. Maybe this"
				" is a SDVO device with multiple inputs.\n");
		}
		count++;
	}

	if (!count) {
		/* No SDVO device info is found */
		DRM_DEBUG_KMS("No SDVO device info is found in VBT\n");
	}
	return;
}

static void
parse_driver_features(struct drm_i915_private *dev_priv,
		       struct bdb_header *bdb)
{
	struct bdb_driver_features *driver;

	driver = find_section(bdb, BDB_DRIVER_FEATURES);
	if (!driver)
		return;

	if (driver->lvds_config == BDB_DRIVER_FEATURE_EDP)
		dev_priv->vbt.edp_support = 1;

	if (driver->dual_frequency)
		dev_priv->render_reclock_avail = true;

	DRM_DEBUG_KMS("DRRS State Enabled:%d\n", driver->drrs_enabled);
	/*
	 * If DRRS is not supported, drrs_type has to be set to 0.
	 * This is because, VBT is configured in such a way that
	 * static DRRS is 0 and DRRS not supported is represented by
	 * driver->drrs_enabled=false
	 */
	if (!driver->drrs_enabled)
		dev_priv->vbt.drrs_type = DRRS_NOT_SUPPORTED;
}

static void
parse_edp(struct drm_i915_private *dev_priv, struct bdb_header *bdb)
{
	struct bdb_edp *edp;
	struct edp_power_seq *edp_pps;
	struct edp_link_params *edp_link_params;

	edp = find_section(bdb, BDB_EDP);
	if (!edp) {
		if (dev_priv->vbt.edp_support)
			DRM_DEBUG_KMS("No eDP BDB found but eDP panel supported.\n");
		return;
	}

	switch ((edp->color_depth >> (panel_type * 2)) & 3) {
	case EDP_18BPP:
		dev_priv->vbt.edp_bpp = 18;
		break;
	case EDP_24BPP:
		dev_priv->vbt.edp_bpp = 24;
		break;
	case EDP_30BPP:
		dev_priv->vbt.edp_bpp = 30;
		break;
	}

	/* Get the eDP sequencing and link info */
	edp_pps = &edp->power_seqs[panel_type];
	edp_link_params = &edp->link_params[panel_type];

	dev_priv->vbt.edp_pps = *edp_pps;

	switch (edp_link_params->rate) {
	case EDP_RATE_1_62:
		dev_priv->vbt.edp_rate = DP_LINK_BW_1_62;
		break;
	case EDP_RATE_2_7:
		dev_priv->vbt.edp_rate = DP_LINK_BW_2_7;
		break;
	default:
		DRM_DEBUG_KMS("VBT has unknown eDP link rate value %u\n",
			      edp_link_params->rate);
		break;
	}

	switch (edp_link_params->lanes) {
	case EDP_LANE_1:
		dev_priv->vbt.edp_lanes = 1;
		break;
	case EDP_LANE_2:
		dev_priv->vbt.edp_lanes = 2;
		break;
	case EDP_LANE_4:
		dev_priv->vbt.edp_lanes = 4;
		break;
	default:
		DRM_DEBUG_KMS("VBT has unknown eDP lane count value %u\n",
			      edp_link_params->lanes);
		break;
	}

	switch (edp_link_params->preemphasis) {
	case EDP_PREEMPHASIS_NONE:
		dev_priv->vbt.edp_preemphasis = DP_TRAIN_PRE_EMPH_LEVEL_0;
		break;
	case EDP_PREEMPHASIS_3_5dB:
		dev_priv->vbt.edp_preemphasis = DP_TRAIN_PRE_EMPH_LEVEL_1;
		break;
	case EDP_PREEMPHASIS_6dB:
		dev_priv->vbt.edp_preemphasis = DP_TRAIN_PRE_EMPH_LEVEL_2;
		break;
	case EDP_PREEMPHASIS_9_5dB:
		dev_priv->vbt.edp_preemphasis = DP_TRAIN_PRE_EMPH_LEVEL_3;
		break;
	default:
		DRM_DEBUG_KMS("VBT has unknown eDP pre-emphasis value %u\n",
			      edp_link_params->preemphasis);
		break;
	}

	switch (edp_link_params->vswing) {
	case EDP_VSWING_0_4V:
		dev_priv->vbt.edp_vswing = DP_TRAIN_VOLTAGE_SWING_LEVEL_0;
		break;
	case EDP_VSWING_0_6V:
		dev_priv->vbt.edp_vswing = DP_TRAIN_VOLTAGE_SWING_LEVEL_1;
		break;
	case EDP_VSWING_0_8V:
		dev_priv->vbt.edp_vswing = DP_TRAIN_VOLTAGE_SWING_LEVEL_2;
		break;
	case EDP_VSWING_1_2V:
		dev_priv->vbt.edp_vswing = DP_TRAIN_VOLTAGE_SWING_LEVEL_3;
		break;
	default:
		DRM_DEBUG_KMS("VBT has unknown eDP voltage swing value %u\n",
			      edp_link_params->vswing);
		break;
	}

	if (bdb->version >= 173) {
		uint8_t vswing;

		vswing = (edp->edp_vswing_preemph >> (panel_type * 4)) & 0xF;
		dev_priv->vbt.edp_low_vswing = vswing == 0;
	}
}

static void
parse_psr(struct drm_i915_private *dev_priv, struct bdb_header *bdb)
{
	struct bdb_psr *psr;
	struct psr_table *psr_table;

	psr = find_section(bdb, BDB_PSR);
	if (!psr) {
		DRM_DEBUG_KMS("No PSR BDB found.\n");
		return;
	}

	psr_table = &psr->psr_table[panel_type];

	dev_priv->vbt.psr.full_link = psr_table->full_link;
	dev_priv->vbt.psr.require_aux_wakeup = psr_table->require_aux_to_wakeup;

	/* Allowed VBT values goes from 0 to 15 */
	dev_priv->vbt.psr.idle_frames = psr_table->idle_frames < 0 ? 0 :
		psr_table->idle_frames > 15 ? 15 : psr_table->idle_frames;

	switch (psr_table->lines_to_wait) {
	case 0:
		dev_priv->vbt.psr.lines_to_wait = PSR_0_LINES_TO_WAIT;
		break;
	case 1:
		dev_priv->vbt.psr.lines_to_wait = PSR_1_LINE_TO_WAIT;
		break;
	case 2:
		dev_priv->vbt.psr.lines_to_wait = PSR_4_LINES_TO_WAIT;
		break;
	case 3:
		dev_priv->vbt.psr.lines_to_wait = PSR_8_LINES_TO_WAIT;
		break;
	default:
		DRM_DEBUG_KMS("VBT has unknown PSR lines to wait %u\n",
			      psr_table->lines_to_wait);
		break;
	}

	dev_priv->vbt.psr.tp1_wakeup_time = psr_table->tp1_wakeup_time;
	dev_priv->vbt.psr.tp2_tp3_wakeup_time = psr_table->tp2_tp3_wakeup_time;
}

static u8 *goto_next_sequence(u8 *data, int *size)
{
	u16 len;
	int tmp = *size;

	if (--tmp < 0)
		return NULL;

	/* goto first element */
	data++;
	while (1) {
		switch (*data) {
		case MIPI_SEQ_ELEM_SEND_PKT:
			/*
			 * skip by this element payload size
			 * skip elem id, command flag and data type
			 */
			tmp -= 5;
			if (tmp < 0)
				return NULL;

			data += 3;
			len = *((u16 *)data);

			tmp -= len;
			if (tmp < 0)
				return NULL;

			/* skip by len */
			data = data + 2 + len;
			break;
		case MIPI_SEQ_ELEM_DELAY:
			/* skip by elem id, and delay is 4 bytes */
			tmp -= 5;
			if (tmp < 0)
				return NULL;

			data += 5;
			break;
		case MIPI_SEQ_ELEM_GPIO:
			tmp -= 3;
			if (tmp < 0)
				return NULL;

			data += 3;
			break;
		default:
			DRM_ERROR("Unknown element\n");
			return NULL;
		}

		/* end of sequence ? */
		if (*data == 0)
			break;
	}

	/* goto next sequence or end of block byte */
	if (--tmp < 0)
		return NULL;

	data++;

	/* update amount of data left for the sequence block to be parsed */
	*size = tmp;
	return data;
}

static void
parse_mipi(struct drm_i915_private *dev_priv, struct bdb_header *bdb)
{
	struct bdb_mipi_config *start;
	struct bdb_mipi_sequence *sequence;
	struct mipi_config *config;
	struct mipi_pps_data *pps;
	u8 *data, *seq_data;
	int i, panel_id, seq_size;
	u16 block_size;

	/* parse MIPI blocks only if LFP type is MIPI */
	if (!dev_priv->vbt.has_mipi)
		return;

	/* Initialize this to undefined indicating no generic MIPI support */
	dev_priv->vbt.dsi.panel_id = MIPI_DSI_UNDEFINED_PANEL_ID;

	/* Block #40 is already parsed and panel_fixed_mode is
	 * stored in dev_priv->lfp_lvds_vbt_mode
	 * resuse this when needed
	 */

	/* Parse #52 for panel index used from panel_type already
	 * parsed
	 */
	start = find_section(bdb, BDB_MIPI_CONFIG);
	if (!start) {
		DRM_DEBUG_KMS("No MIPI config BDB found");
		return;
	}

	DRM_DEBUG_DRIVER("Found MIPI Config block, panel index = %d\n",
								panel_type);

	/*
	 * get hold of the correct configuration block and pps data as per
	 * the panel_type as index
	 */
	config = &start->config[panel_type];
	pps = &start->pps[panel_type];

	/* store as of now full data. Trim when we realise all is not needed */
	dev_priv->vbt.dsi.config = kmemdup(config, sizeof(struct mipi_config), GFP_KERNEL);
	if (!dev_priv->vbt.dsi.config)
		return;

	dev_priv->vbt.dsi.pps = kmemdup(pps, sizeof(struct mipi_pps_data), GFP_KERNEL);
	if (!dev_priv->vbt.dsi.pps) {
		kfree(dev_priv->vbt.dsi.config);
		return;
	}

	/* We have mandatory mipi config blocks. Initialize as generic panel */
	dev_priv->vbt.dsi.panel_id = MIPI_DSI_GENERIC_PANEL_ID;

	/* Check if we have sequence block as well */
	sequence = find_section(bdb, BDB_MIPI_SEQUENCE);
	if (!sequence) {
		DRM_DEBUG_KMS("No MIPI Sequence found, parsing complete\n");
		return;
	}

	DRM_DEBUG_DRIVER("Found MIPI sequence block\n");

	block_size = get_blocksize(sequence);

	/*
	 * parse the sequence block for individual sequences
	 */
	dev_priv->vbt.dsi.seq_version = sequence->version;

	seq_data = &sequence->data[0];

	/*
	 * sequence block is variable length and hence we need to parse and
	 * get the sequence data for specific panel id
	 */
	for (i = 0; i < MAX_MIPI_CONFIGURATIONS; i++) {
		panel_id = *seq_data;
		seq_size = *((u16 *) (seq_data + 1));
		if (panel_id == panel_type)
			break;

		/* skip the sequence including seq header of 3 bytes */
		seq_data = seq_data + 3 + seq_size;
		if ((seq_data - &sequence->data[0]) > block_size) {
			DRM_ERROR("Sequence start is beyond sequence block size, corrupted sequence block\n");
			return;
		}
	}

	if (i == MAX_MIPI_CONFIGURATIONS) {
		DRM_ERROR("Sequence block detected but no valid configuration\n");
		return;
	}

	/* check if found sequence is completely within the sequence block
	 * just being paranoid */
	if (seq_size > block_size) {
		DRM_ERROR("Corrupted sequence/size, bailing out\n");
		return;
	}

	/* skip the panel id(1 byte) and seq size(2 bytes) */
	dev_priv->vbt.dsi.data = kmemdup(seq_data + 3, seq_size, GFP_KERNEL);
	if (!dev_priv->vbt.dsi.data)
		return;

	/*
	 * loop into the sequence data and split into multiple sequneces
	 * There are only 5 types of sequences as of now
	 */
	data = dev_priv->vbt.dsi.data;
	dev_priv->vbt.dsi.size = seq_size;

	/* two consecutive 0x00 indicate end of all sequences */
	while (1) {
		int seq_id = *data;
		if (MIPI_SEQ_MAX > seq_id && seq_id > MIPI_SEQ_UNDEFINED) {
			dev_priv->vbt.dsi.sequence[seq_id] = data;
			DRM_DEBUG_DRIVER("Found mipi sequence - %d\n", seq_id);
		} else {
			DRM_ERROR("undefined sequence\n");
			goto err;
		}

		/* partial parsing to skip elements */
		data = goto_next_sequence(data, &seq_size);

		if (data == NULL) {
			DRM_ERROR("Sequence elements going beyond block itself. Sequence block parsing failed\n");
			goto err;
		}

		if (*data == 0)
			break; /* end of sequence reached */
	}

	DRM_DEBUG_DRIVER("MIPI related vbt parsing complete\n");
	return;
err:
	kfree(dev_priv->vbt.dsi.data);
	dev_priv->vbt.dsi.data = NULL;

	/* error during parsing so set all pointers to null
	 * because of partial parsing */
	memset(dev_priv->vbt.dsi.sequence, 0, sizeof(dev_priv->vbt.dsi.sequence));
}

static void parse_ddi_port(struct drm_i915_private *dev_priv, enum port port,
			   struct bdb_header *bdb)
{
	union child_device_config *it, *child = NULL;
	struct ddi_vbt_port_info *info = &dev_priv->vbt.ddi_port_info[port];
	uint8_t hdmi_level_shift;
	int i, j;
	bool is_dvi, is_hdmi, is_dp, is_edp, is_crt;
	uint8_t aux_channel;
	/* Each DDI port can have more than one value on the "DVO Port" field,
	 * so look for all the possible values for each port and abort if more
	 * than one is found. */
	int dvo_ports[][2] = {
		{DVO_PORT_HDMIA, DVO_PORT_DPA},
		{DVO_PORT_HDMIB, DVO_PORT_DPB},
		{DVO_PORT_HDMIC, DVO_PORT_DPC},
		{DVO_PORT_HDMID, DVO_PORT_DPD},
		{DVO_PORT_CRT, -1 /* Port E can only be DVO_PORT_CRT */ },
	};

	/* Find the child device to use, abort if more than one found. */
	for (i = 0; i < dev_priv->vbt.child_dev_num; i++) {
		it = dev_priv->vbt.child_dev + i;

		for (j = 0; j < 2; j++) {
			if (dvo_ports[port][j] == -1)
				break;

			if (it->common.dvo_port == dvo_ports[port][j]) {
				if (child) {
					DRM_DEBUG_KMS("More than one child device for port %c in VBT.\n",
						      port_name(port));
					return;
				}
				child = it;
			}
		}
	}
	if (!child)
		return;

	aux_channel = child->raw[25];

	is_dvi = child->common.device_type & DEVICE_TYPE_TMDS_DVI_SIGNALING;
	is_dp = child->common.device_type & DEVICE_TYPE_DISPLAYPORT_OUTPUT;
	is_crt = child->common.device_type & DEVICE_TYPE_ANALOG_OUTPUT;
	is_hdmi = is_dvi && (child->common.device_type & DEVICE_TYPE_NOT_HDMI_OUTPUT) == 0;
	is_edp = is_dp && (child->common.device_type & DEVICE_TYPE_INTERNAL_CONNECTOR);

	info->supports_dvi = is_dvi;
	info->supports_hdmi = is_hdmi;
	info->supports_dp = is_dp;

	DRM_DEBUG_KMS("Port %c VBT info: DP:%d HDMI:%d DVI:%d EDP:%d CRT:%d\n",
		      port_name(port), is_dp, is_hdmi, is_dvi, is_edp, is_crt);

	if (is_edp && is_dvi)
		DRM_DEBUG_KMS("Internal DP port %c is TMDS compatible\n",
			      port_name(port));
	if (is_crt && port != PORT_E)
		DRM_DEBUG_KMS("Port %c is analog\n", port_name(port));
	if (is_crt && (is_dvi || is_dp))
		DRM_DEBUG_KMS("Analog port %c is also DP or TMDS compatible\n",
			      port_name(port));
	if (is_dvi && (port == PORT_A || port == PORT_E))
		DRM_DEBUG_KMS("Port %c is TMDS compatible\n", port_name(port));
	if (!is_dvi && !is_dp && !is_crt)
		DRM_DEBUG_KMS("Port %c is not DP/TMDS/CRT compatible\n",
			      port_name(port));
	if (is_edp && (port == PORT_B || port == PORT_C || port == PORT_E))
		DRM_DEBUG_KMS("Port %c is internal DP\n", port_name(port));

	if (is_dvi) {
		if (child->common.ddc_pin == 0x05 && port != PORT_B)
			DRM_DEBUG_KMS("Unexpected DDC pin for port B\n");
		if (child->common.ddc_pin == 0x04 && port != PORT_C)
			DRM_DEBUG_KMS("Unexpected DDC pin for port C\n");
		if (child->common.ddc_pin == 0x06 && port != PORT_D)
			DRM_DEBUG_KMS("Unexpected DDC pin for port D\n");
	}

	if (is_dp) {
		if (aux_channel == 0x40 && port != PORT_A)
			DRM_DEBUG_KMS("Unexpected AUX channel for port A\n");
		if (aux_channel == 0x10 && port != PORT_B)
			DRM_DEBUG_KMS("Unexpected AUX channel for port B\n");
		if (aux_channel == 0x20 && port != PORT_C)
			DRM_DEBUG_KMS("Unexpected AUX channel for port C\n");
		if (aux_channel == 0x30 && port != PORT_D)
			DRM_DEBUG_KMS("Unexpected AUX channel for port D\n");
	}

	if (bdb->version >= 158) {
		/* The VBT HDMI level shift values match the table we have. */
		hdmi_level_shift = child->raw[7] & 0xF;
		DRM_DEBUG_KMS("VBT HDMI level shift for port %c: %d\n",
			      port_name(port),
			      hdmi_level_shift);
		info->hdmi_level_shift = hdmi_level_shift;
	}
}

static void parse_ddi_ports(struct drm_i915_private *dev_priv,
			    struct bdb_header *bdb)
{
	struct drm_device *dev = dev_priv->dev;
	enum port port;

	if (!HAS_DDI(dev))
		return;

	if (!dev_priv->vbt.child_dev_num)
		return;

	if (bdb->version < 155)
		return;

	for (port = PORT_A; port < I915_MAX_PORTS; port++)
		parse_ddi_port(dev_priv, port, bdb);
}

static void
parse_device_mapping(struct drm_i915_private *dev_priv,
		       struct bdb_header *bdb)
{
	struct bdb_general_definitions *p_defs;
	union child_device_config *p_child, *child_dev_ptr;
	int i, child_device_num, count;
	u16	block_size;

	p_defs = find_section(bdb, BDB_GENERAL_DEFINITIONS);
	if (!p_defs) {
		DRM_DEBUG_KMS("No general definition block is found, no devices defined.\n");
		return;
	}
	/* judge whether the size of child device meets the requirements.
	 * If the child device size obtained from general definition block
	 * is different with sizeof(struct child_device_config), skip the
	 * parsing of sdvo device info
	 */
	if (p_defs->child_dev_size != sizeof(*p_child)) {
		/* different child dev size . Ignore it */
		DRM_DEBUG_KMS("different child size is found. Invalid.\n");
		return;
	}
	/* get the block size of general definitions */
	block_size = get_blocksize(p_defs);
	/* get the number of child device */
	child_device_num = (block_size - sizeof(*p_defs)) /
				sizeof(*p_child);
	count = 0;
	/* get the number of child device that is present */
	for (i = 0; i < child_device_num; i++) {
		p_child = &(p_defs->devices[i]);
		if (!p_child->common.device_type) {
			/* skip the device block if device type is invalid */
			continue;
		}
		count++;
	}
	if (!count) {
		DRM_DEBUG_KMS("no child dev is parsed from VBT\n");
		return;
	}
	dev_priv->vbt.child_dev = kcalloc(count, sizeof(*p_child), GFP_KERNEL);
	if (!dev_priv->vbt.child_dev) {
		DRM_DEBUG_KMS("No memory space for child device\n");
		return;
	}

	dev_priv->vbt.child_dev_num = count;
	count = 0;
	for (i = 0; i < child_device_num; i++) {
		p_child = &(p_defs->devices[i]);
		if (!p_child->common.device_type) {
			/* skip the device block if device type is invalid */
			continue;
		}

		if (p_child->common.dvo_port >= DVO_PORT_MIPIA
		    && p_child->common.dvo_port <= DVO_PORT_MIPID
		    &&p_child->common.device_type & DEVICE_TYPE_MIPI_OUTPUT) {
			DRM_DEBUG_KMS("Found MIPI as LFP\n");
			dev_priv->vbt.has_mipi = 1;
			dev_priv->vbt.dsi.port = p_child->common.dvo_port;
		}

		child_dev_ptr = dev_priv->vbt.child_dev + count;
		count++;
		memcpy((void *)child_dev_ptr, (void *)p_child,
					sizeof(*p_child));
	}
	return;
}

static void
init_vbt_defaults(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	enum port port;

	dev_priv->vbt.crt_ddc_pin = GMBUS_PIN_VGADDC;

	/* Default to having backlight */
	dev_priv->vbt.backlight.present = true;

	/* LFP panel data */
	dev_priv->vbt.lvds_dither = 1;
	dev_priv->vbt.lvds_vbt = 0;

	/* SDVO panel data */
	dev_priv->vbt.sdvo_lvds_vbt_mode = NULL;

	/* general features */
	dev_priv->vbt.int_tv_support = 1;
	dev_priv->vbt.int_crt_support = 1;

	/* Default to using SSC */
	dev_priv->vbt.lvds_use_ssc = 1;
	/*
	 * Core/SandyBridge/IvyBridge use alternative (120MHz) reference
	 * clock for LVDS.
	 */
	dev_priv->vbt.lvds_ssc_freq = intel_bios_ssc_frequency(dev,
			!HAS_PCH_SPLIT(dev));
	DRM_DEBUG_KMS("Set default to SSC at %d kHz\n", dev_priv->vbt.lvds_ssc_freq);

	for (port = PORT_A; port < I915_MAX_PORTS; port++) {
		struct ddi_vbt_port_info *info =
			&dev_priv->vbt.ddi_port_info[port];

		info->hdmi_level_shift = HDMI_LEVEL_SHIFT_UNKNOWN;

		info->supports_dvi = (port != PORT_A && port != PORT_E);
		info->supports_hdmi = info->supports_dvi;
		info->supports_dp = (port != PORT_E);
	}
}

static int intel_no_opregion_vbt_callback(const struct dmi_system_id *id)
{
	DRM_DEBUG_KMS("Falling back to manually reading VBT from "
		      "VBIOS ROM for %s\n",
		      id->ident);
	return 1;
}

static const struct dmi_system_id intel_no_opregion_vbt[] = {
	{
		.callback = intel_no_opregion_vbt_callback,
		.ident = "ThinkCentre A57",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "97027RG"),
		},
	},
	{ }
};

static struct bdb_header *validate_vbt(char *base, size_t size,
				       struct vbt_header *vbt,
				       const char *source)
{
	size_t offset;
	struct bdb_header *bdb;

	if (vbt == NULL) {
		DRM_DEBUG_DRIVER("VBT signature missing\n");
		return NULL;
	}

	offset = (char *)vbt - base;
	if (offset + sizeof(struct vbt_header) > size) {
		DRM_DEBUG_DRIVER("VBT header incomplete\n");
		return NULL;
	}

	if (memcmp(vbt->signature, "$VBT", 4)) {
		DRM_DEBUG_DRIVER("VBT invalid signature\n");
		return NULL;
	}

	offset += vbt->bdb_offset;
	if (offset + sizeof(struct bdb_header) > size) {
		DRM_DEBUG_DRIVER("BDB header incomplete\n");
		return NULL;
	}

	bdb = (struct bdb_header *)(base + offset);
	if (offset + bdb->bdb_size > size) {
		DRM_DEBUG_DRIVER("BDB incomplete\n");
		return NULL;
	}

	DRM_DEBUG_KMS("Using VBT from %s: %20s\n",
		      source, vbt->signature);
	return bdb;
}

/**
 * intel_parse_bios - find VBT and initialize settings from the BIOS
 * @dev: DRM device
 *
 * Loads the Video BIOS and checks that the VBT exists.  Sets scratch registers
 * to appropriate values.
 *
 * Returns 0 on success, nonzero on failure.
 */
int
intel_parse_bios(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct pci_dev *pdev = dev->pdev;
	struct bdb_header *bdb = NULL;
	u8 __iomem *bios = NULL;

	if (HAS_PCH_NOP(dev))
		return -ENODEV;

	init_vbt_defaults(dev_priv);

	/* XXX Should this validation be moved to intel_opregion.c? */
	if (!dmi_check_system(intel_no_opregion_vbt) && dev_priv->opregion.vbt)
		bdb = validate_vbt((char *)dev_priv->opregion.header, OPREGION_SIZE,
				   (struct vbt_header *)dev_priv->opregion.vbt,
				   "OpRegion");

	if (bdb == NULL) {
		size_t i, size;

		bios = pci_map_rom(pdev, &size);
		if (!bios)
			return -1;

		/* Scour memory looking for the VBT signature */
		for (i = 0; i + 4 < size; i++) {
			if (memcmp(bios + i, "$VBT", 4) == 0) {
				bdb = validate_vbt(bios, size,
						   (struct vbt_header *)(bios + i),
						   "PCI ROM");
				break;
			}
		}

		if (!bdb) {
			pci_unmap_rom(pdev, bios);
			return -1;
		}
	}

	/* Grab useful general definitions */
	parse_general_features(dev_priv, bdb);
	parse_general_definitions(dev_priv, bdb);
	parse_lfp_panel_data(dev_priv, bdb);
	parse_lfp_backlight(dev_priv, bdb);
	parse_sdvo_panel_data(dev_priv, bdb);
	parse_sdvo_device_mapping(dev_priv, bdb);
	parse_device_mapping(dev_priv, bdb);
	parse_driver_features(dev_priv, bdb);
	parse_edp(dev_priv, bdb);
	parse_psr(dev_priv, bdb);
	parse_mipi(dev_priv, bdb);
	parse_ddi_ports(dev_priv, bdb);

	if (bios)
		pci_unmap_rom(pdev, bios);

	return 0;
}

/* Ensure that vital registers have been initialised, even if the BIOS
 * is absent or just failing to do its job.
 */
void intel_setup_bios(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	 /* Set the Panel Power On/Off timings if uninitialized. */
	if (!HAS_PCH_SPLIT(dev) &&
	    I915_READ(PP_ON_DELAYS) == 0 && I915_READ(PP_OFF_DELAYS) == 0) {
		/* Set T2 to 40ms and T5 to 200ms */
		I915_WRITE(PP_ON_DELAYS, 0x019007d0);

		/* Set T3 to 35ms and Tx to 200ms */
		I915_WRITE(PP_OFF_DELAYS, 0x015e07d0);
	}
}

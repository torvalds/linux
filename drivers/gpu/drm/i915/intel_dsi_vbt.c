/*
 * Copyright © 2014 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Author: Shobhit Kumar <shobhit.kumar@intel.com>
 *
 */

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/i915_drm.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>
#include <video/mipi_display.h>
#include <asm/intel-mid.h>
#include <video/mipi_display.h>
#include "i915_drv.h"
#include "intel_drv.h"
#include "intel_dsi.h"

#define MIPI_TRANSFER_MODE_SHIFT	0
#define MIPI_VIRTUAL_CHANNEL_SHIFT	1
#define MIPI_PORT_SHIFT			3

#define PREPARE_CNT_MAX		0x3F
#define EXIT_ZERO_CNT_MAX	0x3F
#define CLK_ZERO_CNT_MAX	0xFF
#define TRAIL_CNT_MAX		0x1F

#define NS_KHZ_RATIO 1000000

/* base offsets for gpio pads */
#define VLV_GPIO_NC_0_HV_DDI0_HPD	0x4130
#define VLV_GPIO_NC_1_HV_DDI0_DDC_SDA	0x4120
#define VLV_GPIO_NC_2_HV_DDI0_DDC_SCL	0x4110
#define VLV_GPIO_NC_3_PANEL0_VDDEN	0x4140
#define VLV_GPIO_NC_4_PANEL0_BKLTEN	0x4150
#define VLV_GPIO_NC_5_PANEL0_BKLTCTL	0x4160
#define VLV_GPIO_NC_6_HV_DDI1_HPD	0x4180
#define VLV_GPIO_NC_7_HV_DDI1_DDC_SDA	0x4190
#define VLV_GPIO_NC_8_HV_DDI1_DDC_SCL	0x4170
#define VLV_GPIO_NC_9_PANEL1_VDDEN	0x4100
#define VLV_GPIO_NC_10_PANEL1_BKLTEN	0x40E0
#define VLV_GPIO_NC_11_PANEL1_BKLTCTL	0x40F0

#define VLV_GPIO_PCONF0(base_offset)	(base_offset)
#define VLV_GPIO_PAD_VAL(base_offset)	((base_offset) + 8)

struct gpio_map {
	u16 base_offset;
	bool init;
};

static struct gpio_map vlv_gpio_table[] = {
	{ VLV_GPIO_NC_0_HV_DDI0_HPD },
	{ VLV_GPIO_NC_1_HV_DDI0_DDC_SDA },
	{ VLV_GPIO_NC_2_HV_DDI0_DDC_SCL },
	{ VLV_GPIO_NC_3_PANEL0_VDDEN },
	{ VLV_GPIO_NC_4_PANEL0_BKLTEN },
	{ VLV_GPIO_NC_5_PANEL0_BKLTCTL },
	{ VLV_GPIO_NC_6_HV_DDI1_HPD },
	{ VLV_GPIO_NC_7_HV_DDI1_DDC_SDA },
	{ VLV_GPIO_NC_8_HV_DDI1_DDC_SCL },
	{ VLV_GPIO_NC_9_PANEL1_VDDEN },
	{ VLV_GPIO_NC_10_PANEL1_BKLTEN },
	{ VLV_GPIO_NC_11_PANEL1_BKLTCTL },
};

#define CHV_GPIO_IDX_START_N		0
#define CHV_GPIO_IDX_START_E		73
#define CHV_GPIO_IDX_START_SW		100
#define CHV_GPIO_IDX_START_SE		198

#define CHV_VBT_MAX_PINS_PER_FMLY	15

#define CHV_GPIO_PAD_CFG0(f, i)		(0x4400 + (f) * 0x400 + (i) * 8)
#define  CHV_GPIO_GPIOEN		(1 << 15)
#define  CHV_GPIO_GPIOCFG_GPIO		(0 << 8)
#define  CHV_GPIO_GPIOCFG_GPO		(1 << 8)
#define  CHV_GPIO_GPIOCFG_GPI		(2 << 8)
#define  CHV_GPIO_GPIOCFG_HIZ		(3 << 8)
#define  CHV_GPIO_GPIOTXSTATE(state)	((!!(state)) << 1)

#define CHV_GPIO_PAD_CFG1(f, i)		(0x4400 + (f) * 0x400 + (i) * 8 + 4)
#define  CHV_GPIO_CFGLOCK		(1 << 31)

/* ICL DSI Display GPIO Pins */
#define  ICL_GPIO_DDSP_HPD_A		0
#define  ICL_GPIO_L_VDDEN_1		1
#define  ICL_GPIO_L_BKLTEN_1		2
#define  ICL_GPIO_DDPA_CTRLCLK_1	3
#define  ICL_GPIO_DDPA_CTRLDATA_1	4
#define  ICL_GPIO_DDSP_HPD_B		5
#define  ICL_GPIO_L_VDDEN_2		6
#define  ICL_GPIO_L_BKLTEN_2		7
#define  ICL_GPIO_DDPA_CTRLCLK_2	8
#define  ICL_GPIO_DDPA_CTRLDATA_2	9

static inline enum port intel_dsi_seq_port_to_port(u8 port)
{
	return port ? PORT_C : PORT_A;
}

static const u8 *mipi_exec_send_packet(struct intel_dsi *intel_dsi,
				       const u8 *data)
{
	struct drm_i915_private *dev_priv = to_i915(intel_dsi->base.base.dev);
	struct mipi_dsi_device *dsi_device;
	u8 type, flags, seq_port;
	u16 len;
	enum port port;

	DRM_DEBUG_KMS("\n");

	flags = *data++;
	type = *data++;

	len = *((u16 *) data);
	data += 2;

	seq_port = (flags >> MIPI_PORT_SHIFT) & 3;

	/* For DSI single link on Port A & C, the seq_port value which is
	 * parsed from Sequence Block#53 of VBT has been set to 0
	 * Now, read/write of packets for the DSI single link on Port A and
	 * Port C will based on the DVO port from VBT block 2.
	 */
	if (intel_dsi->ports == (1 << PORT_C))
		port = PORT_C;
	else
		port = intel_dsi_seq_port_to_port(seq_port);

	dsi_device = intel_dsi->dsi_hosts[port]->device;
	if (!dsi_device) {
		DRM_DEBUG_KMS("no dsi device for port %c\n", port_name(port));
		goto out;
	}

	if ((flags >> MIPI_TRANSFER_MODE_SHIFT) & 1)
		dsi_device->mode_flags &= ~MIPI_DSI_MODE_LPM;
	else
		dsi_device->mode_flags |= MIPI_DSI_MODE_LPM;

	dsi_device->channel = (flags >> MIPI_VIRTUAL_CHANNEL_SHIFT) & 3;

	switch (type) {
	case MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM:
		mipi_dsi_generic_write(dsi_device, NULL, 0);
		break;
	case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
		mipi_dsi_generic_write(dsi_device, data, 1);
		break;
	case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
		mipi_dsi_generic_write(dsi_device, data, 2);
		break;
	case MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM:
		DRM_DEBUG_DRIVER("Generic Read not yet implemented or used\n");
		break;
	case MIPI_DSI_GENERIC_LONG_WRITE:
		mipi_dsi_generic_write(dsi_device, data, len);
		break;
	case MIPI_DSI_DCS_SHORT_WRITE:
		mipi_dsi_dcs_write_buffer(dsi_device, data, 1);
		break;
	case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
		mipi_dsi_dcs_write_buffer(dsi_device, data, 2);
		break;
	case MIPI_DSI_DCS_READ:
		DRM_DEBUG_DRIVER("DCS Read not yet implemented or used\n");
		break;
	case MIPI_DSI_DCS_LONG_WRITE:
		mipi_dsi_dcs_write_buffer(dsi_device, data, len);
		break;
	}

	if (!IS_ICELAKE(dev_priv))
		vlv_dsi_wait_for_fifo_empty(intel_dsi, port);

out:
	data += len;

	return data;
}

static const u8 *mipi_exec_delay(struct intel_dsi *intel_dsi, const u8 *data)
{
	u32 delay = *((const u32 *) data);

	DRM_DEBUG_KMS("\n");

	usleep_range(delay, delay + 10);
	data += 4;

	return data;
}

static void vlv_exec_gpio(struct drm_i915_private *dev_priv,
			  u8 gpio_source, u8 gpio_index, bool value)
{
	struct gpio_map *map;
	u16 pconf0, padval;
	u32 tmp;
	u8 port;

	if (gpio_index >= ARRAY_SIZE(vlv_gpio_table)) {
		DRM_DEBUG_KMS("unknown gpio index %u\n", gpio_index);
		return;
	}

	map = &vlv_gpio_table[gpio_index];

	if (dev_priv->vbt.dsi.seq_version >= 3) {
		/* XXX: this assumes vlv_gpio_table only has NC GPIOs. */
		port = IOSF_PORT_GPIO_NC;
	} else {
		if (gpio_source == 0) {
			port = IOSF_PORT_GPIO_NC;
		} else if (gpio_source == 1) {
			DRM_DEBUG_KMS("SC gpio not supported\n");
			return;
		} else {
			DRM_DEBUG_KMS("unknown gpio source %u\n", gpio_source);
			return;
		}
	}

	pconf0 = VLV_GPIO_PCONF0(map->base_offset);
	padval = VLV_GPIO_PAD_VAL(map->base_offset);

	mutex_lock(&dev_priv->sb_lock);
	if (!map->init) {
		/* FIXME: remove constant below */
		vlv_iosf_sb_write(dev_priv, port, pconf0, 0x2000CC00);
		map->init = true;
	}

	tmp = 0x4 | value;
	vlv_iosf_sb_write(dev_priv, port, padval, tmp);
	mutex_unlock(&dev_priv->sb_lock);
}

static void chv_exec_gpio(struct drm_i915_private *dev_priv,
			  u8 gpio_source, u8 gpio_index, bool value)
{
	u16 cfg0, cfg1;
	u16 family_num;
	u8 port;

	if (dev_priv->vbt.dsi.seq_version >= 3) {
		if (gpio_index >= CHV_GPIO_IDX_START_SE) {
			/* XXX: it's unclear whether 255->57 is part of SE. */
			gpio_index -= CHV_GPIO_IDX_START_SE;
			port = CHV_IOSF_PORT_GPIO_SE;
		} else if (gpio_index >= CHV_GPIO_IDX_START_SW) {
			gpio_index -= CHV_GPIO_IDX_START_SW;
			port = CHV_IOSF_PORT_GPIO_SW;
		} else if (gpio_index >= CHV_GPIO_IDX_START_E) {
			gpio_index -= CHV_GPIO_IDX_START_E;
			port = CHV_IOSF_PORT_GPIO_E;
		} else {
			port = CHV_IOSF_PORT_GPIO_N;
		}
	} else {
		/* XXX: The spec is unclear about CHV GPIO on seq v2 */
		if (gpio_source != 0) {
			DRM_DEBUG_KMS("unknown gpio source %u\n", gpio_source);
			return;
		}

		if (gpio_index >= CHV_GPIO_IDX_START_E) {
			DRM_DEBUG_KMS("invalid gpio index %u for GPIO N\n",
				      gpio_index);
			return;
		}

		port = CHV_IOSF_PORT_GPIO_N;
	}

	family_num = gpio_index / CHV_VBT_MAX_PINS_PER_FMLY;
	gpio_index = gpio_index % CHV_VBT_MAX_PINS_PER_FMLY;

	cfg0 = CHV_GPIO_PAD_CFG0(family_num, gpio_index);
	cfg1 = CHV_GPIO_PAD_CFG1(family_num, gpio_index);

	mutex_lock(&dev_priv->sb_lock);
	vlv_iosf_sb_write(dev_priv, port, cfg1, 0);
	vlv_iosf_sb_write(dev_priv, port, cfg0,
			  CHV_GPIO_GPIOEN | CHV_GPIO_GPIOCFG_GPO |
			  CHV_GPIO_GPIOTXSTATE(value));
	mutex_unlock(&dev_priv->sb_lock);
}

static void bxt_exec_gpio(struct drm_i915_private *dev_priv,
			  u8 gpio_source, u8 gpio_index, bool value)
{
	/* XXX: this table is a quick ugly hack. */
	static struct gpio_desc *bxt_gpio_table[U8_MAX + 1];
	struct gpio_desc *gpio_desc = bxt_gpio_table[gpio_index];

	if (!gpio_desc) {
		gpio_desc = devm_gpiod_get_index(dev_priv->drm.dev,
						 NULL, gpio_index,
						 value ? GPIOD_OUT_LOW :
						 GPIOD_OUT_HIGH);

		if (IS_ERR_OR_NULL(gpio_desc)) {
			DRM_ERROR("GPIO index %u request failed (%ld)\n",
				  gpio_index, PTR_ERR(gpio_desc));
			return;
		}

		bxt_gpio_table[gpio_index] = gpio_desc;
	}

	gpiod_set_value(gpio_desc, value);
}

static const u8 *mipi_exec_gpio(struct intel_dsi *intel_dsi, const u8 *data)
{
	struct drm_device *dev = intel_dsi->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	u8 gpio_source, gpio_index = 0, gpio_number;
	bool value;

	DRM_DEBUG_KMS("\n");

	if (dev_priv->vbt.dsi.seq_version >= 3)
		gpio_index = *data++;

	gpio_number = *data++;

	/* gpio source in sequence v2 only */
	if (dev_priv->vbt.dsi.seq_version == 2)
		gpio_source = (*data >> 1) & 3;
	else
		gpio_source = 0;

	/* pull up/down */
	value = *data++ & 1;

	if (IS_VALLEYVIEW(dev_priv))
		vlv_exec_gpio(dev_priv, gpio_source, gpio_number, value);
	else if (IS_CHERRYVIEW(dev_priv))
		chv_exec_gpio(dev_priv, gpio_source, gpio_number, value);
	else
		bxt_exec_gpio(dev_priv, gpio_source, gpio_index, value);

	return data;
}

static const u8 *mipi_exec_i2c(struct intel_dsi *intel_dsi, const u8 *data)
{
	DRM_DEBUG_KMS("Skipping I2C element execution\n");

	return data + *(data + 6) + 7;
}

static const u8 *mipi_exec_spi(struct intel_dsi *intel_dsi, const u8 *data)
{
	DRM_DEBUG_KMS("Skipping SPI element execution\n");

	return data + *(data + 5) + 6;
}

static const u8 *mipi_exec_pmic(struct intel_dsi *intel_dsi, const u8 *data)
{
	DRM_DEBUG_KMS("Skipping PMIC element execution\n");

	return data + 15;
}

typedef const u8 * (*fn_mipi_elem_exec)(struct intel_dsi *intel_dsi,
					const u8 *data);
static const fn_mipi_elem_exec exec_elem[] = {
	[MIPI_SEQ_ELEM_SEND_PKT] = mipi_exec_send_packet,
	[MIPI_SEQ_ELEM_DELAY] = mipi_exec_delay,
	[MIPI_SEQ_ELEM_GPIO] = mipi_exec_gpio,
	[MIPI_SEQ_ELEM_I2C] = mipi_exec_i2c,
	[MIPI_SEQ_ELEM_SPI] = mipi_exec_spi,
	[MIPI_SEQ_ELEM_PMIC] = mipi_exec_pmic,
};

/*
 * MIPI Sequence from VBT #53 parsing logic
 * We have already separated each seqence during bios parsing
 * Following is generic execution function for any sequence
 */

static const char * const seq_name[] = {
	[MIPI_SEQ_DEASSERT_RESET] = "MIPI_SEQ_DEASSERT_RESET",
	[MIPI_SEQ_INIT_OTP] = "MIPI_SEQ_INIT_OTP",
	[MIPI_SEQ_DISPLAY_ON] = "MIPI_SEQ_DISPLAY_ON",
	[MIPI_SEQ_DISPLAY_OFF]  = "MIPI_SEQ_DISPLAY_OFF",
	[MIPI_SEQ_ASSERT_RESET] = "MIPI_SEQ_ASSERT_RESET",
	[MIPI_SEQ_BACKLIGHT_ON] = "MIPI_SEQ_BACKLIGHT_ON",
	[MIPI_SEQ_BACKLIGHT_OFF] = "MIPI_SEQ_BACKLIGHT_OFF",
	[MIPI_SEQ_TEAR_ON] = "MIPI_SEQ_TEAR_ON",
	[MIPI_SEQ_TEAR_OFF] = "MIPI_SEQ_TEAR_OFF",
	[MIPI_SEQ_POWER_ON] = "MIPI_SEQ_POWER_ON",
	[MIPI_SEQ_POWER_OFF] = "MIPI_SEQ_POWER_OFF",
};

static const char *sequence_name(enum mipi_seq seq_id)
{
	if (seq_id < ARRAY_SIZE(seq_name) && seq_name[seq_id])
		return seq_name[seq_id];
	else
		return "(unknown)";
}

void intel_dsi_vbt_exec_sequence(struct intel_dsi *intel_dsi,
				 enum mipi_seq seq_id)
{
	struct drm_i915_private *dev_priv = to_i915(intel_dsi->base.base.dev);
	const u8 *data;
	fn_mipi_elem_exec mipi_elem_exec;

	if (WARN_ON(seq_id >= ARRAY_SIZE(dev_priv->vbt.dsi.sequence)))
		return;

	data = dev_priv->vbt.dsi.sequence[seq_id];
	if (!data)
		return;

	WARN_ON(*data != seq_id);

	DRM_DEBUG_KMS("Starting MIPI sequence %d - %s\n",
		      seq_id, sequence_name(seq_id));

	/* Skip Sequence Byte. */
	data++;

	/* Skip Size of Sequence. */
	if (dev_priv->vbt.dsi.seq_version >= 3)
		data += 4;

	while (1) {
		u8 operation_byte = *data++;
		u8 operation_size = 0;

		if (operation_byte == MIPI_SEQ_ELEM_END)
			break;

		if (operation_byte < ARRAY_SIZE(exec_elem))
			mipi_elem_exec = exec_elem[operation_byte];
		else
			mipi_elem_exec = NULL;

		/* Size of Operation. */
		if (dev_priv->vbt.dsi.seq_version >= 3)
			operation_size = *data++;

		if (mipi_elem_exec) {
			const u8 *next = data + operation_size;

			data = mipi_elem_exec(intel_dsi, data);

			/* Consistency check if we have size. */
			if (operation_size && data != next) {
				DRM_ERROR("Inconsistent operation size\n");
				return;
			}
		} else if (operation_size) {
			/* We have size, skip. */
			DRM_DEBUG_KMS("Unsupported MIPI operation byte %u\n",
				      operation_byte);
			data += operation_size;
		} else {
			/* No size, can't skip without parsing. */
			DRM_ERROR("Unsupported MIPI operation byte %u\n",
				  operation_byte);
			return;
		}
	}
}

void intel_dsi_msleep(struct intel_dsi *intel_dsi, int msec)
{
	struct drm_i915_private *dev_priv = to_i915(intel_dsi->base.base.dev);

	/* For v3 VBTs in vid-mode the delays are part of the VBT sequences */
	if (is_vid_mode(intel_dsi) && dev_priv->vbt.dsi.seq_version >= 3)
		return;

	msleep(msec);
}

int intel_dsi_vbt_get_modes(struct intel_dsi *intel_dsi)
{
	struct intel_connector *connector = intel_dsi->attached_connector;
	struct drm_device *dev = intel_dsi->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(dev, dev_priv->vbt.lfp_lvds_vbt_mode);
	if (!mode)
		return 0;

	mode->type |= DRM_MODE_TYPE_PREFERRED;

	drm_mode_probed_add(&connector->base, mode);

	return 1;
}

#define ICL_PREPARE_CNT_MAX	0x7
#define ICL_CLK_ZERO_CNT_MAX	0xf
#define ICL_TRAIL_CNT_MAX	0x7
#define ICL_TCLK_PRE_CNT_MAX	0x3
#define ICL_TCLK_POST_CNT_MAX	0x7
#define ICL_HS_ZERO_CNT_MAX	0xf
#define ICL_EXIT_ZERO_CNT_MAX	0x7

static void icl_dphy_param_init(struct intel_dsi *intel_dsi)
{
	struct drm_device *dev = intel_dsi->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct mipi_config *mipi_config = dev_priv->vbt.dsi.config;
	u32 tlpx_ns;
	u32 prepare_cnt, exit_zero_cnt, clk_zero_cnt, trail_cnt;
	u32 ths_prepare_ns, tclk_trail_ns;
	u32 hs_zero_cnt;
	u32 tclk_pre_cnt, tclk_post_cnt;

	tlpx_ns = intel_dsi_tlpx_ns(intel_dsi);

	tclk_trail_ns = max(mipi_config->tclk_trail, mipi_config->ths_trail);
	ths_prepare_ns = max(mipi_config->ths_prepare,
			     mipi_config->tclk_prepare);

	/*
	 * prepare cnt in escape clocks
	 * this field represents a hexadecimal value with a precision
	 * of 1.2 – i.e. the most significant bit is the integer
	 * and the least significant 2 bits are fraction bits.
	 * so, the field can represent a range of 0.25 to 1.75
	 */
	prepare_cnt = DIV_ROUND_UP(ths_prepare_ns * 4, tlpx_ns);
	if (prepare_cnt > ICL_PREPARE_CNT_MAX) {
		DRM_DEBUG_KMS("prepare_cnt out of range (%d)\n", prepare_cnt);
		prepare_cnt = ICL_PREPARE_CNT_MAX;
	}

	/* clk zero count in escape clocks */
	clk_zero_cnt = DIV_ROUND_UP(mipi_config->tclk_prepare_clkzero -
				    ths_prepare_ns, tlpx_ns);
	if (clk_zero_cnt > ICL_CLK_ZERO_CNT_MAX) {
		DRM_DEBUG_KMS("clk_zero_cnt out of range (%d)\n", clk_zero_cnt);
		clk_zero_cnt = ICL_CLK_ZERO_CNT_MAX;
	}

	/* trail cnt in escape clocks*/
	trail_cnt = DIV_ROUND_UP(tclk_trail_ns, tlpx_ns);
	if (trail_cnt > ICL_TRAIL_CNT_MAX) {
		DRM_DEBUG_KMS("trail_cnt out of range (%d)\n", trail_cnt);
		trail_cnt = ICL_TRAIL_CNT_MAX;
	}

	/* tclk pre count in escape clocks */
	tclk_pre_cnt = DIV_ROUND_UP(mipi_config->tclk_pre, tlpx_ns);
	if (tclk_pre_cnt > ICL_TCLK_PRE_CNT_MAX) {
		DRM_DEBUG_KMS("tclk_pre_cnt out of range (%d)\n", tclk_pre_cnt);
		tclk_pre_cnt = ICL_TCLK_PRE_CNT_MAX;
	}

	/* tclk post count in escape clocks */
	tclk_post_cnt = DIV_ROUND_UP(mipi_config->tclk_post, tlpx_ns);
	if (tclk_post_cnt > ICL_TCLK_POST_CNT_MAX) {
		DRM_DEBUG_KMS("tclk_post_cnt out of range (%d)\n", tclk_post_cnt);
		tclk_post_cnt = ICL_TCLK_POST_CNT_MAX;
	}

	/* hs zero cnt in escape clocks */
	hs_zero_cnt = DIV_ROUND_UP(mipi_config->ths_prepare_hszero -
				   ths_prepare_ns, tlpx_ns);
	if (hs_zero_cnt > ICL_HS_ZERO_CNT_MAX) {
		DRM_DEBUG_KMS("hs_zero_cnt out of range (%d)\n", hs_zero_cnt);
		hs_zero_cnt = ICL_HS_ZERO_CNT_MAX;
	}

	/* hs exit zero cnt in escape clocks */
	exit_zero_cnt = DIV_ROUND_UP(mipi_config->ths_exit, tlpx_ns);
	if (exit_zero_cnt > ICL_EXIT_ZERO_CNT_MAX) {
		DRM_DEBUG_KMS("exit_zero_cnt out of range (%d)\n", exit_zero_cnt);
		exit_zero_cnt = ICL_EXIT_ZERO_CNT_MAX;
	}

	/* clock lane dphy timings */
	intel_dsi->dphy_reg = (CLK_PREPARE_OVERRIDE |
			       CLK_PREPARE(prepare_cnt) |
			       CLK_ZERO_OVERRIDE |
			       CLK_ZERO(clk_zero_cnt) |
			       CLK_PRE_OVERRIDE |
			       CLK_PRE(tclk_pre_cnt) |
			       CLK_POST_OVERRIDE |
			       CLK_POST(tclk_post_cnt) |
			       CLK_TRAIL_OVERRIDE |
			       CLK_TRAIL(trail_cnt));

	/* data lanes dphy timings */
	intel_dsi->dphy_data_lane_reg = (HS_PREPARE_OVERRIDE |
					 HS_PREPARE(prepare_cnt) |
					 HS_ZERO_OVERRIDE |
					 HS_ZERO(hs_zero_cnt) |
					 HS_TRAIL_OVERRIDE |
					 HS_TRAIL(trail_cnt) |
					 HS_EXIT_OVERRIDE |
					 HS_EXIT(exit_zero_cnt));
}

static void vlv_dphy_param_init(struct intel_dsi *intel_dsi)
{
	struct drm_device *dev = intel_dsi->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct mipi_config *mipi_config = dev_priv->vbt.dsi.config;
	u32 tlpx_ns, extra_byte_count, tlpx_ui;
	u32 ui_num, ui_den;
	u32 prepare_cnt, exit_zero_cnt, clk_zero_cnt, trail_cnt;
	u32 ths_prepare_ns, tclk_trail_ns;
	u32 tclk_prepare_clkzero, ths_prepare_hszero;
	u32 lp_to_hs_switch, hs_to_lp_switch;
	u32 mul;

	tlpx_ns = intel_dsi_tlpx_ns(intel_dsi);

	switch (intel_dsi->lane_count) {
	case 1:
	case 2:
		extra_byte_count = 2;
		break;
	case 3:
		extra_byte_count = 4;
		break;
	case 4:
	default:
		extra_byte_count = 3;
		break;
	}

	/* in Kbps */
	ui_num = NS_KHZ_RATIO;
	ui_den = intel_dsi_bitrate(intel_dsi);

	tclk_prepare_clkzero = mipi_config->tclk_prepare_clkzero;
	ths_prepare_hszero = mipi_config->ths_prepare_hszero;

	/*
	 * B060
	 * LP byte clock = TLPX/ (8UI)
	 */
	intel_dsi->lp_byte_clk = DIV_ROUND_UP(tlpx_ns * ui_den, 8 * ui_num);

	/* DDR clock period = 2 * UI
	 * UI(sec) = 1/(bitrate * 10^3) (bitrate is in KHZ)
	 * UI(nsec) = 10^6 / bitrate
	 * DDR clock period (nsec) = 2 * UI = (2 * 10^6)/ bitrate
	 * DDR clock count  = ns_value / DDR clock period
	 *
	 * For GEMINILAKE dphy_param_reg will be programmed in terms of
	 * HS byte clock count for other platform in HS ddr clock count
	 */
	mul = IS_GEMINILAKE(dev_priv) ? 8 : 2;
	ths_prepare_ns = max(mipi_config->ths_prepare,
			     mipi_config->tclk_prepare);

	/* prepare count */
	prepare_cnt = DIV_ROUND_UP(ths_prepare_ns * ui_den, ui_num * mul);

	if (prepare_cnt > PREPARE_CNT_MAX) {
		DRM_DEBUG_KMS("prepare count too high %u\n", prepare_cnt);
		prepare_cnt = PREPARE_CNT_MAX;
	}

	/* exit zero count */
	exit_zero_cnt = DIV_ROUND_UP(
				(ths_prepare_hszero - ths_prepare_ns) * ui_den,
				ui_num * mul
				);

	/*
	 * Exit zero is unified val ths_zero and ths_exit
	 * minimum value for ths_exit = 110ns
	 * min (exit_zero_cnt * 2) = 110/UI
	 * exit_zero_cnt = 55/UI
	 */
	if (exit_zero_cnt < (55 * ui_den / ui_num) && (55 * ui_den) % ui_num)
		exit_zero_cnt += 1;

	if (exit_zero_cnt > EXIT_ZERO_CNT_MAX) {
		DRM_DEBUG_KMS("exit zero count too high %u\n", exit_zero_cnt);
		exit_zero_cnt = EXIT_ZERO_CNT_MAX;
	}

	/* clk zero count */
	clk_zero_cnt = DIV_ROUND_UP(
				(tclk_prepare_clkzero -	ths_prepare_ns)
				* ui_den, ui_num * mul);

	if (clk_zero_cnt > CLK_ZERO_CNT_MAX) {
		DRM_DEBUG_KMS("clock zero count too high %u\n", clk_zero_cnt);
		clk_zero_cnt = CLK_ZERO_CNT_MAX;
	}

	/* trail count */
	tclk_trail_ns = max(mipi_config->tclk_trail, mipi_config->ths_trail);
	trail_cnt = DIV_ROUND_UP(tclk_trail_ns * ui_den, ui_num * mul);

	if (trail_cnt > TRAIL_CNT_MAX) {
		DRM_DEBUG_KMS("trail count too high %u\n", trail_cnt);
		trail_cnt = TRAIL_CNT_MAX;
	}

	/* B080 */
	intel_dsi->dphy_reg = exit_zero_cnt << 24 | trail_cnt << 16 |
						clk_zero_cnt << 8 | prepare_cnt;

	/*
	 * LP to HS switch count = 4TLPX + PREP_COUNT * mul + EXIT_ZERO_COUNT *
	 *					mul + 10UI + Extra Byte Count
	 *
	 * HS to LP switch count = THS-TRAIL + 2TLPX + Extra Byte Count
	 * Extra Byte Count is calculated according to number of lanes.
	 * High Low Switch Count is the Max of LP to HS and
	 * HS to LP switch count
	 *
	 */
	tlpx_ui = DIV_ROUND_UP(tlpx_ns * ui_den, ui_num);

	/* B044 */
	/* FIXME:
	 * The comment above does not match with the code */
	lp_to_hs_switch = DIV_ROUND_UP(4 * tlpx_ui + prepare_cnt * mul +
						exit_zero_cnt * mul + 10, 8);

	hs_to_lp_switch = DIV_ROUND_UP(mipi_config->ths_trail + 2 * tlpx_ui, 8);

	intel_dsi->hs_to_lp_count = max(lp_to_hs_switch, hs_to_lp_switch);
	intel_dsi->hs_to_lp_count += extra_byte_count;

	/* B088 */
	/* LP -> HS for clock lanes
	 * LP clk sync + LP11 + LP01 + tclk_prepare + tclk_zero +
	 *						extra byte count
	 * 2TPLX + 1TLPX + 1 TPLX(in ns) + prepare_cnt * 2 + clk_zero_cnt *
	 *					2(in UI) + extra byte count
	 * In byteclks = (4TLPX + prepare_cnt * 2 + clk_zero_cnt *2 (in UI)) /
	 *					8 + extra byte count
	 */
	intel_dsi->clk_lp_to_hs_count =
		DIV_ROUND_UP(
			4 * tlpx_ui + prepare_cnt * 2 +
			clk_zero_cnt * 2,
			8);

	intel_dsi->clk_lp_to_hs_count += extra_byte_count;

	/* HS->LP for Clock Lanes
	 * Low Power clock synchronisations + 1Tx byteclk + tclk_trail +
	 *						Extra byte count
	 * 2TLPX + 8UI + (trail_count*2)(in UI) + Extra byte count
	 * In byteclks = (2*TLpx(in UI) + trail_count*2 +8)(in UI)/8 +
	 *						Extra byte count
	 */
	intel_dsi->clk_hs_to_lp_count =
		DIV_ROUND_UP(2 * tlpx_ui + trail_cnt * 2 + 8,
			8);
	intel_dsi->clk_hs_to_lp_count += extra_byte_count;
}

bool intel_dsi_vbt_init(struct intel_dsi *intel_dsi, u16 panel_id)
{
	struct drm_device *dev = intel_dsi->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct mipi_config *mipi_config = dev_priv->vbt.dsi.config;
	struct mipi_pps_data *pps = dev_priv->vbt.dsi.pps;
	struct drm_display_mode *mode = dev_priv->vbt.lfp_lvds_vbt_mode;
	u16 burst_mode_ratio;
	enum port port;

	DRM_DEBUG_KMS("\n");

	intel_dsi->eotp_pkt = mipi_config->eot_pkt_disabled ? 0 : 1;
	intel_dsi->clock_stop = mipi_config->enable_clk_stop ? 1 : 0;
	intel_dsi->lane_count = mipi_config->lane_cnt + 1;
	intel_dsi->pixel_format =
			pixel_format_from_register_bits(
				mipi_config->videomode_color_format << 7);

	intel_dsi->dual_link = mipi_config->dual_link;
	intel_dsi->pixel_overlap = mipi_config->pixel_overlap;
	intel_dsi->operation_mode = mipi_config->is_cmd_mode;
	intel_dsi->video_mode_format = mipi_config->video_transfer_mode;
	intel_dsi->escape_clk_div = mipi_config->byte_clk_sel;
	intel_dsi->lp_rx_timeout = mipi_config->lp_rx_timeout;
	intel_dsi->hs_tx_timeout = mipi_config->hs_tx_timeout;
	intel_dsi->turn_arnd_val = mipi_config->turn_around_timeout;
	intel_dsi->rst_timer_val = mipi_config->device_reset_timer;
	intel_dsi->init_count = mipi_config->master_init_timer;
	intel_dsi->bw_timer = mipi_config->dbi_bw_timer;
	intel_dsi->video_frmt_cfg_bits =
		mipi_config->bta_enabled ? DISABLE_VIDEO_BTA : 0;
	intel_dsi->bgr_enabled = mipi_config->rgb_flip;

	/* Starting point, adjusted depending on dual link and burst mode */
	intel_dsi->pclk = mode->clock;

	/* In dual link mode each port needs half of pixel clock */
	if (intel_dsi->dual_link) {
		intel_dsi->pclk /= 2;

		/* we can enable pixel_overlap if needed by panel. In this
		 * case we need to increase the pixelclock for extra pixels
		 */
		if (intel_dsi->dual_link == DSI_DUAL_LINK_FRONT_BACK) {
			intel_dsi->pclk += DIV_ROUND_UP(mode->vtotal * intel_dsi->pixel_overlap * 60, 1000);
		}
	}

	/* Burst Mode Ratio
	 * Target ddr frequency from VBT / non burst ddr freq
	 * multiply by 100 to preserve remainder
	 */
	if (intel_dsi->video_mode_format == VIDEO_MODE_BURST) {
		if (mipi_config->target_burst_mode_freq) {
			u32 bitrate = intel_dsi_bitrate(intel_dsi);

			if (mipi_config->target_burst_mode_freq < bitrate) {
				DRM_ERROR("Burst mode freq is less than computed\n");
				return false;
			}

			burst_mode_ratio = DIV_ROUND_UP(
				mipi_config->target_burst_mode_freq * 100,
				bitrate);

			intel_dsi->pclk = DIV_ROUND_UP(intel_dsi->pclk * burst_mode_ratio, 100);
		} else {
			DRM_ERROR("Burst mode target is not set\n");
			return false;
		}
	} else
		burst_mode_ratio = 100;

	intel_dsi->burst_mode_ratio = burst_mode_ratio;

	if (IS_ICELAKE(dev_priv))
		icl_dphy_param_init(intel_dsi);
	else
		vlv_dphy_param_init(intel_dsi);

	DRM_DEBUG_KMS("Pclk %d\n", intel_dsi->pclk);
	DRM_DEBUG_KMS("Pixel overlap %d\n", intel_dsi->pixel_overlap);
	DRM_DEBUG_KMS("Lane count %d\n", intel_dsi->lane_count);
	DRM_DEBUG_KMS("DPHY param reg 0x%x\n", intel_dsi->dphy_reg);
	DRM_DEBUG_KMS("Video mode format %s\n",
		      intel_dsi->video_mode_format == VIDEO_MODE_NON_BURST_WITH_SYNC_PULSE ?
		      "non-burst with sync pulse" :
		      intel_dsi->video_mode_format == VIDEO_MODE_NON_BURST_WITH_SYNC_EVENTS ?
		      "non-burst with sync events" :
		      intel_dsi->video_mode_format == VIDEO_MODE_BURST ?
		      "burst" : "<unknown>");
	DRM_DEBUG_KMS("Burst mode ratio %d\n", intel_dsi->burst_mode_ratio);
	DRM_DEBUG_KMS("Reset timer %d\n", intel_dsi->rst_timer_val);
	DRM_DEBUG_KMS("Eot %s\n", enableddisabled(intel_dsi->eotp_pkt));
	DRM_DEBUG_KMS("Clockstop %s\n", enableddisabled(!intel_dsi->clock_stop));
	DRM_DEBUG_KMS("Mode %s\n", intel_dsi->operation_mode ? "command" : "video");
	if (intel_dsi->dual_link == DSI_DUAL_LINK_FRONT_BACK)
		DRM_DEBUG_KMS("Dual link: DSI_DUAL_LINK_FRONT_BACK\n");
	else if (intel_dsi->dual_link == DSI_DUAL_LINK_PIXEL_ALT)
		DRM_DEBUG_KMS("Dual link: DSI_DUAL_LINK_PIXEL_ALT\n");
	else
		DRM_DEBUG_KMS("Dual link: NONE\n");
	DRM_DEBUG_KMS("Pixel Format %d\n", intel_dsi->pixel_format);
	DRM_DEBUG_KMS("TLPX %d\n", intel_dsi->escape_clk_div);
	DRM_DEBUG_KMS("LP RX Timeout 0x%x\n", intel_dsi->lp_rx_timeout);
	DRM_DEBUG_KMS("Turnaround Timeout 0x%x\n", intel_dsi->turn_arnd_val);
	DRM_DEBUG_KMS("Init Count 0x%x\n", intel_dsi->init_count);
	DRM_DEBUG_KMS("HS to LP Count 0x%x\n", intel_dsi->hs_to_lp_count);
	DRM_DEBUG_KMS("LP Byte Clock %d\n", intel_dsi->lp_byte_clk);
	DRM_DEBUG_KMS("DBI BW Timer 0x%x\n", intel_dsi->bw_timer);
	DRM_DEBUG_KMS("LP to HS Clock Count 0x%x\n", intel_dsi->clk_lp_to_hs_count);
	DRM_DEBUG_KMS("HS to LP Clock Count 0x%x\n", intel_dsi->clk_hs_to_lp_count);
	DRM_DEBUG_KMS("BTA %s\n",
			enableddisabled(!(intel_dsi->video_frmt_cfg_bits & DISABLE_VIDEO_BTA)));

	/* delays in VBT are in unit of 100us, so need to convert
	 * here in ms
	 * Delay (100us) * 100 /1000 = Delay / 10 (ms) */
	intel_dsi->backlight_off_delay = pps->bl_disable_delay / 10;
	intel_dsi->backlight_on_delay = pps->bl_enable_delay / 10;
	intel_dsi->panel_on_delay = pps->panel_on_delay / 10;
	intel_dsi->panel_off_delay = pps->panel_off_delay / 10;
	intel_dsi->panel_pwr_cycle_delay = pps->panel_power_cycle_delay / 10;

	/* a regular driver would get the device in probe */
	for_each_dsi_port(port, intel_dsi->ports) {
		mipi_dsi_attach(intel_dsi->dsi_hosts[port]->device);
	}

	return true;
}

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
#include <linux/slab.h>
#include <video/mipi_display.h>
#include <asm/intel-mid.h>
#include <video/mipi_display.h>
#include "i915_drv.h"
#include "intel_drv.h"
#include "intel_dsi.h"
#include "intel_dsi_cmd.h"

#define MIPI_TRANSFER_MODE_SHIFT	0
#define MIPI_VIRTUAL_CHANNEL_SHIFT	1
#define MIPI_PORT_SHIFT			3

#define PREPARE_CNT_MAX		0x3F
#define EXIT_ZERO_CNT_MAX	0x3F
#define CLK_ZERO_CNT_MAX	0xFF
#define TRAIL_CNT_MAX		0x1F

#define NS_KHZ_RATIO 1000000

#define GPI0_NC_0_HV_DDI0_HPD           0x4130
#define GPIO_NC_0_HV_DDI0_PAD           0x4138
#define GPIO_NC_1_HV_DDI0_DDC_SDA       0x4120
#define GPIO_NC_1_HV_DDI0_DDC_SDA_PAD   0x4128
#define GPIO_NC_2_HV_DDI0_DDC_SCL       0x4110
#define GPIO_NC_2_HV_DDI0_DDC_SCL_PAD   0x4118
#define GPIO_NC_3_PANEL0_VDDEN          0x4140
#define GPIO_NC_3_PANEL0_VDDEN_PAD      0x4148
#define GPIO_NC_4_PANEL0_BLKEN          0x4150
#define GPIO_NC_4_PANEL0_BLKEN_PAD      0x4158
#define GPIO_NC_5_PANEL0_BLKCTL         0x4160
#define GPIO_NC_5_PANEL0_BLKCTL_PAD     0x4168
#define GPIO_NC_6_PCONF0                0x4180
#define GPIO_NC_6_PAD                   0x4188
#define GPIO_NC_7_PCONF0                0x4190
#define GPIO_NC_7_PAD                   0x4198
#define GPIO_NC_8_PCONF0                0x4170
#define GPIO_NC_8_PAD                   0x4178
#define GPIO_NC_9_PCONF0                0x4100
#define GPIO_NC_9_PAD                   0x4108
#define GPIO_NC_10_PCONF0               0x40E0
#define GPIO_NC_10_PAD                  0x40E8
#define GPIO_NC_11_PCONF0               0x40F0
#define GPIO_NC_11_PAD                  0x40F8

struct gpio_table {
	u16 function_reg;
	u16 pad_reg;
	u8 init;
};

static struct gpio_table gtable[] = {
	{ GPI0_NC_0_HV_DDI0_HPD, GPIO_NC_0_HV_DDI0_PAD, 0 },
	{ GPIO_NC_1_HV_DDI0_DDC_SDA, GPIO_NC_1_HV_DDI0_DDC_SDA_PAD, 0 },
	{ GPIO_NC_2_HV_DDI0_DDC_SCL, GPIO_NC_2_HV_DDI0_DDC_SCL_PAD, 0 },
	{ GPIO_NC_3_PANEL0_VDDEN, GPIO_NC_3_PANEL0_VDDEN_PAD, 0 },
	{ GPIO_NC_4_PANEL0_BLKEN, GPIO_NC_4_PANEL0_BLKEN_PAD, 0 },
	{ GPIO_NC_5_PANEL0_BLKCTL, GPIO_NC_5_PANEL0_BLKCTL_PAD, 0 },
	{ GPIO_NC_6_PCONF0, GPIO_NC_6_PAD, 0 },
	{ GPIO_NC_7_PCONF0, GPIO_NC_7_PAD, 0 },
	{ GPIO_NC_8_PCONF0, GPIO_NC_8_PAD, 0 },
	{ GPIO_NC_9_PCONF0, GPIO_NC_9_PAD, 0 },
	{ GPIO_NC_10_PCONF0, GPIO_NC_10_PAD, 0},
	{ GPIO_NC_11_PCONF0, GPIO_NC_11_PAD, 0}
};

static inline enum port intel_dsi_seq_port_to_port(u8 port)
{
	return port ? PORT_C : PORT_A;
}

static u8 *mipi_exec_send_packet(struct intel_dsi *intel_dsi, u8 *data)
{
	u8 type, byte, mode, vc, seq_port;
	u16 len;
	enum port port;

	byte = *data++;
	mode = (byte >> MIPI_TRANSFER_MODE_SHIFT) & 0x1;
	vc = (byte >> MIPI_VIRTUAL_CHANNEL_SHIFT) & 0x3;
	seq_port = (byte >> MIPI_PORT_SHIFT) & 0x3;

	/* For DSI single link on Port A & C, the seq_port value which is
	 * parsed from Sequence Block#53 of VBT has been set to 0
	 * Now, read/write of packets for the DSI single link on Port A and
	 * Port C will based on the DVO port from VBT block 2.
	 */
	if (intel_dsi->ports == (1 << PORT_C))
		port = PORT_C;
	else
		port = intel_dsi_seq_port_to_port(seq_port);
	/* LP or HS mode */
	intel_dsi->hs = mode;

	/* get packet type and increment the pointer */
	type = *data++;

	len = *((u16 *) data);
	data += 2;

	switch (type) {
	case MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM:
		dsi_vc_generic_write_0(intel_dsi, vc, port);
		break;
	case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
		dsi_vc_generic_write_1(intel_dsi, vc, *data, port);
		break;
	case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
		dsi_vc_generic_write_2(intel_dsi, vc, *data, *(data + 1), port);
		break;
	case MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM:
		DRM_DEBUG_DRIVER("Generic Read not yet implemented or used\n");
		break;
	case MIPI_DSI_GENERIC_LONG_WRITE:
		dsi_vc_generic_write(intel_dsi, vc, data, len, port);
		break;
	case MIPI_DSI_DCS_SHORT_WRITE:
		dsi_vc_dcs_write_0(intel_dsi, vc, *data, port);
		break;
	case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
		dsi_vc_dcs_write_1(intel_dsi, vc, *data, *(data + 1), port);
		break;
	case MIPI_DSI_DCS_READ:
		DRM_DEBUG_DRIVER("DCS Read not yet implemented or used\n");
		break;
	case MIPI_DSI_DCS_LONG_WRITE:
		dsi_vc_dcs_write(intel_dsi, vc, data, len, port);
		break;
	}

	data += len;

	return data;
}

static u8 *mipi_exec_delay(struct intel_dsi *intel_dsi, u8 *data)
{
	u32 delay = *((u32 *) data);

	usleep_range(delay, delay + 10);
	data += 4;

	return data;
}

static u8 *mipi_exec_gpio(struct intel_dsi *intel_dsi, u8 *data)
{
	u8 gpio, action;
	u16 function, pad;
	u32 val;
	struct drm_device *dev = intel_dsi->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	gpio = *data++;

	/* pull up/down */
	action = *data++;

	function = gtable[gpio].function_reg;
	pad = gtable[gpio].pad_reg;

	mutex_lock(&dev_priv->dpio_lock);
	if (!gtable[gpio].init) {
		/* program the function */
		/* FIXME: remove constant below */
		vlv_gpio_nc_write(dev_priv, function, 0x2000CC00);
		gtable[gpio].init = 1;
	}

	val = 0x4 | action;

	/* pull up/down */
	vlv_gpio_nc_write(dev_priv, pad, val);
	mutex_unlock(&dev_priv->dpio_lock);

	return data;
}

typedef u8 * (*fn_mipi_elem_exec)(struct intel_dsi *intel_dsi, u8 *data);
static const fn_mipi_elem_exec exec_elem[] = {
	NULL, /* reserved */
	mipi_exec_send_packet,
	mipi_exec_delay,
	mipi_exec_gpio,
	NULL, /* status read; later */
};

/*
 * MIPI Sequence from VBT #53 parsing logic
 * We have already separated each seqence during bios parsing
 * Following is generic execution function for any sequence
 */

static const char * const seq_name[] = {
	"UNDEFINED",
	"MIPI_SEQ_ASSERT_RESET",
	"MIPI_SEQ_INIT_OTP",
	"MIPI_SEQ_DISPLAY_ON",
	"MIPI_SEQ_DISPLAY_OFF",
	"MIPI_SEQ_DEASSERT_RESET"
};

static void generic_exec_sequence(struct intel_dsi *intel_dsi, char *sequence)
{
	u8 *data = sequence;
	fn_mipi_elem_exec mipi_elem_exec;
	int index;

	if (!sequence)
		return;

	DRM_DEBUG_DRIVER("Starting MIPI sequence - %s\n", seq_name[*data]);

	/* go to the first element of the sequence */
	data++;

	/* parse each byte till we reach end of sequence byte - 0x00 */
	while (1) {
		index = *data;
		mipi_elem_exec = exec_elem[index];
		if (!mipi_elem_exec) {
			DRM_ERROR("Unsupported MIPI element, skipping sequence execution\n");
			return;
		}

		/* goto element payload */
		data++;

		/* execute the element specific rotines */
		data = mipi_elem_exec(intel_dsi, data);

		/*
		 * After processing the element, data should point to
		 * next element or end of sequence
		 * check if have we reached end of sequence
		 */
		if (*data == 0x00)
			break;
	}
}

static bool generic_init(struct intel_dsi_device *dsi)
{
	struct intel_dsi *intel_dsi = container_of(dsi, struct intel_dsi, dev);
	struct drm_device *dev = intel_dsi->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct mipi_config *mipi_config = dev_priv->vbt.dsi.config;
	struct mipi_pps_data *pps = dev_priv->vbt.dsi.pps;
	struct drm_display_mode *mode = dev_priv->vbt.lfp_lvds_vbt_mode;
	u32 bits_per_pixel = 24;
	u32 tlpx_ns, extra_byte_count, bitrate, tlpx_ui;
	u32 ui_num, ui_den;
	u32 prepare_cnt, exit_zero_cnt, clk_zero_cnt, trail_cnt;
	u32 ths_prepare_ns, tclk_trail_ns;
	u32 tclk_prepare_clkzero, ths_prepare_hszero;
	u32 lp_to_hs_switch, hs_to_lp_switch;
	u32 pclk, computed_ddr;
	u16 burst_mode_ratio;

	DRM_DEBUG_KMS("\n");

	intel_dsi->eotp_pkt = mipi_config->eot_pkt_disabled ? 0 : 1;
	intel_dsi->clock_stop = mipi_config->enable_clk_stop ? 1 : 0;
	intel_dsi->lane_count = mipi_config->lane_cnt + 1;
	intel_dsi->pixel_format = mipi_config->videomode_color_format << 7;
	intel_dsi->dual_link = mipi_config->dual_link;
	intel_dsi->pixel_overlap = mipi_config->pixel_overlap;

	if (intel_dsi->dual_link)
		intel_dsi->ports = ((1 << PORT_A) | (1 << PORT_C));

	if (intel_dsi->pixel_format == VID_MODE_FORMAT_RGB666)
		bits_per_pixel = 18;
	else if (intel_dsi->pixel_format == VID_MODE_FORMAT_RGB565)
		bits_per_pixel = 16;

	intel_dsi->operation_mode = mipi_config->is_cmd_mode;
	intel_dsi->video_mode_format = mipi_config->video_transfer_mode;
	intel_dsi->escape_clk_div = mipi_config->byte_clk_sel;
	intel_dsi->lp_rx_timeout = mipi_config->lp_rx_timeout;
	intel_dsi->turn_arnd_val = mipi_config->turn_around_timeout;
	intel_dsi->rst_timer_val = mipi_config->device_reset_timer;
	intel_dsi->init_count = mipi_config->master_init_timer;
	intel_dsi->bw_timer = mipi_config->dbi_bw_timer;
	intel_dsi->video_frmt_cfg_bits =
		mipi_config->bta_enabled ? DISABLE_VIDEO_BTA : 0;

	pclk = mode->clock;

	/* In dual link mode each port needs half of pixel clock */
	if (intel_dsi->dual_link) {
		pclk = pclk / 2;

		/* we can enable pixel_overlap if needed by panel. In this
		 * case we need to increase the pixelclock for extra pixels
		 */
		if (intel_dsi->dual_link == DSI_DUAL_LINK_FRONT_BACK) {
			pclk += DIV_ROUND_UP(mode->vtotal *
						intel_dsi->pixel_overlap *
						60, 1000);
		}
	}

	/* Burst Mode Ratio
	 * Target ddr frequency from VBT / non burst ddr freq
	 * multiply by 100 to preserve remainder
	 */
	if (intel_dsi->video_mode_format == VIDEO_MODE_BURST) {
		if (mipi_config->target_burst_mode_freq) {
			computed_ddr =
				(pclk * bits_per_pixel) / intel_dsi->lane_count;

			if (mipi_config->target_burst_mode_freq <
								computed_ddr) {
				DRM_ERROR("Burst mode freq is less than computed\n");
				return false;
			}

			burst_mode_ratio = DIV_ROUND_UP(
				mipi_config->target_burst_mode_freq * 100,
				computed_ddr);

			pclk = DIV_ROUND_UP(pclk * burst_mode_ratio, 100);
		} else {
			DRM_ERROR("Burst mode target is not set\n");
			return false;
		}
	} else
		burst_mode_ratio = 100;

	intel_dsi->burst_mode_ratio = burst_mode_ratio;
	intel_dsi->pclk = pclk;

	bitrate = (pclk * bits_per_pixel) / intel_dsi->lane_count;

	switch (intel_dsi->escape_clk_div) {
	case 0:
		tlpx_ns = 50;
		break;
	case 1:
		tlpx_ns = 100;
		break;

	case 2:
		tlpx_ns = 200;
		break;
	default:
		tlpx_ns = 50;
		break;
	}

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

	/*
	 * ui(s) = 1/f [f in hz]
	 * ui(ns) = 10^9 / (f*10^6) [f in Mhz] -> 10^3/f(Mhz)
	 */

	/* in Kbps */
	ui_num = NS_KHZ_RATIO;
	ui_den = bitrate;

	tclk_prepare_clkzero = mipi_config->tclk_prepare_clkzero;
	ths_prepare_hszero = mipi_config->ths_prepare_hszero;

	/*
	 * B060
	 * LP byte clock = TLPX/ (8UI)
	 */
	intel_dsi->lp_byte_clk = DIV_ROUND_UP(tlpx_ns * ui_den, 8 * ui_num);

	/* count values in UI = (ns value) * (bitrate / (2 * 10^6))
	 *
	 * Since txddrclkhs_i is 2xUI, all the count values programmed in
	 * DPHY param register are divided by 2
	 *
	 * prepare count
	 */
	ths_prepare_ns = max(mipi_config->ths_prepare,
			     mipi_config->tclk_prepare);
	prepare_cnt = DIV_ROUND_UP(ths_prepare_ns * ui_den, ui_num * 2);

	/* exit zero count */
	exit_zero_cnt = DIV_ROUND_UP(
				(ths_prepare_hszero - ths_prepare_ns) * ui_den,
				ui_num * 2
				);

	/*
	 * Exit zero  is unified val ths_zero and ths_exit
	 * minimum value for ths_exit = 110ns
	 * min (exit_zero_cnt * 2) = 110/UI
	 * exit_zero_cnt = 55/UI
	 */
	 if (exit_zero_cnt < (55 * ui_den / ui_num))
		if ((55 * ui_den) % ui_num)
			exit_zero_cnt += 1;

	/* clk zero count */
	clk_zero_cnt = DIV_ROUND_UP(
			(tclk_prepare_clkzero -	ths_prepare_ns)
			* ui_den, 2 * ui_num);

	/* trail count */
	tclk_trail_ns = max(mipi_config->tclk_trail, mipi_config->ths_trail);
	trail_cnt = DIV_ROUND_UP(tclk_trail_ns * ui_den, 2 * ui_num);

	if (prepare_cnt > PREPARE_CNT_MAX ||
		exit_zero_cnt > EXIT_ZERO_CNT_MAX ||
		clk_zero_cnt > CLK_ZERO_CNT_MAX ||
		trail_cnt > TRAIL_CNT_MAX)
		DRM_DEBUG_DRIVER("Values crossing maximum limits, restricting to max values\n");

	if (prepare_cnt > PREPARE_CNT_MAX)
		prepare_cnt = PREPARE_CNT_MAX;

	if (exit_zero_cnt > EXIT_ZERO_CNT_MAX)
		exit_zero_cnt = EXIT_ZERO_CNT_MAX;

	if (clk_zero_cnt > CLK_ZERO_CNT_MAX)
		clk_zero_cnt = CLK_ZERO_CNT_MAX;

	if (trail_cnt > TRAIL_CNT_MAX)
		trail_cnt = TRAIL_CNT_MAX;

	/* B080 */
	intel_dsi->dphy_reg = exit_zero_cnt << 24 | trail_cnt << 16 |
						clk_zero_cnt << 8 | prepare_cnt;

	/*
	 * LP to HS switch count = 4TLPX + PREP_COUNT * 2 + EXIT_ZERO_COUNT * 2
	 *					+ 10UI + Extra Byte Count
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
	lp_to_hs_switch = DIV_ROUND_UP(4 * tlpx_ui + prepare_cnt * 2 +
						exit_zero_cnt * 2 + 10, 8);

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

	DRM_DEBUG_KMS("Eot %s\n", intel_dsi->eotp_pkt ? "enabled" : "disabled");
	DRM_DEBUG_KMS("Clockstop %s\n", intel_dsi->clock_stop ?
						"disabled" : "enabled");
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
			intel_dsi->video_frmt_cfg_bits & DISABLE_VIDEO_BTA ?
			"disabled" : "enabled");

	/* delays in VBT are in unit of 100us, so need to convert
	 * here in ms
	 * Delay (100us) * 100 /1000 = Delay / 10 (ms) */
	intel_dsi->backlight_off_delay = pps->bl_disable_delay / 10;
	intel_dsi->backlight_on_delay = pps->bl_enable_delay / 10;
	intel_dsi->panel_on_delay = pps->panel_on_delay / 10;
	intel_dsi->panel_off_delay = pps->panel_off_delay / 10;
	intel_dsi->panel_pwr_cycle_delay = pps->panel_power_cycle_delay / 10;

	return true;
}

static int generic_mode_valid(struct intel_dsi_device *dsi,
		   struct drm_display_mode *mode)
{
	return MODE_OK;
}

static bool generic_mode_fixup(struct intel_dsi_device *dsi,
		    const struct drm_display_mode *mode,
		    struct drm_display_mode *adjusted_mode) {
	return true;
}

static void generic_panel_reset(struct intel_dsi_device *dsi)
{
	struct intel_dsi *intel_dsi = container_of(dsi, struct intel_dsi, dev);
	struct drm_device *dev = intel_dsi->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	char *sequence = dev_priv->vbt.dsi.sequence[MIPI_SEQ_ASSERT_RESET];

	generic_exec_sequence(intel_dsi, sequence);
}

static void generic_disable_panel_power(struct intel_dsi_device *dsi)
{
	struct intel_dsi *intel_dsi = container_of(dsi, struct intel_dsi, dev);
	struct drm_device *dev = intel_dsi->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	char *sequence = dev_priv->vbt.dsi.sequence[MIPI_SEQ_DEASSERT_RESET];

	generic_exec_sequence(intel_dsi, sequence);
}

static void generic_send_otp_cmds(struct intel_dsi_device *dsi)
{
	struct intel_dsi *intel_dsi = container_of(dsi, struct intel_dsi, dev);
	struct drm_device *dev = intel_dsi->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	char *sequence = dev_priv->vbt.dsi.sequence[MIPI_SEQ_INIT_OTP];

	generic_exec_sequence(intel_dsi, sequence);
}

static void generic_enable(struct intel_dsi_device *dsi)
{
	struct intel_dsi *intel_dsi = container_of(dsi, struct intel_dsi, dev);
	struct drm_device *dev = intel_dsi->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	char *sequence = dev_priv->vbt.dsi.sequence[MIPI_SEQ_DISPLAY_ON];

	generic_exec_sequence(intel_dsi, sequence);
}

static void generic_disable(struct intel_dsi_device *dsi)
{
	struct intel_dsi *intel_dsi = container_of(dsi, struct intel_dsi, dev);
	struct drm_device *dev = intel_dsi->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	char *sequence = dev_priv->vbt.dsi.sequence[MIPI_SEQ_DISPLAY_OFF];

	generic_exec_sequence(intel_dsi, sequence);
}

static enum drm_connector_status generic_detect(struct intel_dsi_device *dsi)
{
	return connector_status_connected;
}

static bool generic_get_hw_state(struct intel_dsi_device *dev)
{
	return true;
}

static struct drm_display_mode *generic_get_modes(struct intel_dsi_device *dsi)
{
	struct intel_dsi *intel_dsi = container_of(dsi, struct intel_dsi, dev);
	struct drm_device *dev = intel_dsi->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	dev_priv->vbt.lfp_lvds_vbt_mode->type |= DRM_MODE_TYPE_PREFERRED;
	return dev_priv->vbt.lfp_lvds_vbt_mode;
}

static void generic_destroy(struct intel_dsi_device *dsi) { }

/* Callbacks. We might not need them all. */
struct intel_dsi_dev_ops vbt_generic_dsi_display_ops = {
	.init = generic_init,
	.mode_valid = generic_mode_valid,
	.mode_fixup = generic_mode_fixup,
	.panel_reset = generic_panel_reset,
	.disable_panel_power = generic_disable_panel_power,
	.send_otp_cmds = generic_send_otp_cmds,
	.enable = generic_enable,
	.disable = generic_disable,
	.detect = generic_detect,
	.get_hw_state = generic_get_hw_state,
	.get_modes = generic_get_modes,
	.destroy = generic_destroy,
};

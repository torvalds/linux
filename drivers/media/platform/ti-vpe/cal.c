// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI Camera Access Layer (CAL) - Driver
 *
 * Copyright (c) 2015-2020 Texas Instruments Inc.
 *
 * Authors:
 *	Benoit Parrot <bparrot@ti.com>
 *	Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <media/media-device.h>
#include <media/v4l2-async.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "cal.h"
#include "cal_regs.h"

MODULE_DESCRIPTION("TI CAL driver");
MODULE_AUTHOR("Benoit Parrot, <bparrot@ti.com>");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("0.1.0");

int cal_video_nr = -1;
module_param_named(video_nr, cal_video_nr, uint, 0644);
MODULE_PARM_DESC(video_nr, "videoX start number, -1 is autodetect");

unsigned int cal_debug;
module_param_named(debug, cal_debug, uint, 0644);
MODULE_PARM_DESC(debug, "activates debug info");

/* ------------------------------------------------------------------
 *	Platform Data
 * ------------------------------------------------------------------
 */

static const struct cal_camerarx_data dra72x_cal_camerarx[] = {
	{
		.fields = {
			[F_CTRLCLKEN] = { 10, 10 },
			[F_CAMMODE] = { 11, 12 },
			[F_LANEENABLE] = { 13, 16 },
			[F_CSI_MODE] = { 17, 17 },
		},
		.num_lanes = 4,
	},
	{
		.fields = {
			[F_CTRLCLKEN] = { 0, 0 },
			[F_CAMMODE] = { 1, 2 },
			[F_LANEENABLE] = { 3, 4 },
			[F_CSI_MODE] = { 5, 5 },
		},
		.num_lanes = 2,
	},
};

static const struct cal_data dra72x_cal_data = {
	.camerarx = dra72x_cal_camerarx,
	.num_csi2_phy = ARRAY_SIZE(dra72x_cal_camerarx),
};

static const struct cal_data dra72x_es1_cal_data = {
	.camerarx = dra72x_cal_camerarx,
	.num_csi2_phy = ARRAY_SIZE(dra72x_cal_camerarx),
	.flags = DRA72_CAL_PRE_ES2_LDO_DISABLE,
};

static const struct cal_camerarx_data dra76x_cal_csi_phy[] = {
	{
		.fields = {
			[F_CTRLCLKEN] = { 8, 8 },
			[F_CAMMODE] = { 9, 10 },
			[F_CSI_MODE] = { 11, 11 },
			[F_LANEENABLE] = { 27, 31 },
		},
		.num_lanes = 5,
	},
	{
		.fields = {
			[F_CTRLCLKEN] = { 0, 0 },
			[F_CAMMODE] = { 1, 2 },
			[F_CSI_MODE] = { 3, 3 },
			[F_LANEENABLE] = { 24, 26 },
		},
		.num_lanes = 3,
	},
};

static const struct cal_data dra76x_cal_data = {
	.camerarx = dra76x_cal_csi_phy,
	.num_csi2_phy = ARRAY_SIZE(dra76x_cal_csi_phy),
};

static const struct cal_camerarx_data am654_cal_csi_phy[] = {
	{
		.fields = {
			[F_CTRLCLKEN] = { 15, 15 },
			[F_CAMMODE] = { 24, 25 },
			[F_LANEENABLE] = { 0, 4 },
		},
		.num_lanes = 5,
	},
};

static const struct cal_data am654_cal_data = {
	.camerarx = am654_cal_csi_phy,
	.num_csi2_phy = ARRAY_SIZE(am654_cal_csi_phy),
};

/* ------------------------------------------------------------------
 *	I/O Register Accessors
 * ------------------------------------------------------------------
 */

void cal_quickdump_regs(struct cal_dev *cal)
{
	unsigned int i;

	cal_info(cal, "CAL Registers @ 0x%pa:\n", &cal->res->start);
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET, 16, 4,
		       (__force const void *)cal->base,
		       resource_size(cal->res), false);

	for (i = 0; i < ARRAY_SIZE(cal->phy); ++i) {
		struct cal_camerarx *phy = cal->phy[i];

		if (!phy)
			continue;

		cal_info(cal, "CSI2 Core %u Registers @ %pa:\n", i,
			 &phy->res->start);
		print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET, 16, 4,
			       (__force const void *)phy->base,
			       resource_size(phy->res),
			       false);
	}
}

/* ------------------------------------------------------------------
 *	CAMERARX Management
 * ------------------------------------------------------------------
 */

static inline u32 camerarx_read(struct cal_camerarx *phy, u32 offset)
{
	return ioread32(phy->base + offset);
}

static inline void camerarx_write(struct cal_camerarx *phy, u32 offset, u32 val)
{
	iowrite32(val, phy->base + offset);
}

static s64 cal_camerarx_get_external_rate(struct cal_camerarx *phy)
{
	struct v4l2_ctrl *ctrl;
	s64 rate;

	ctrl = v4l2_ctrl_find(phy->sensor->ctrl_handler, V4L2_CID_PIXEL_RATE);
	if (!ctrl) {
		phy_err(phy, "no pixel rate control in subdev: %s\n",
			phy->sensor->name);
		return -EPIPE;
	}

	rate = v4l2_ctrl_g_ctrl_int64(ctrl);
	phy_dbg(3, phy, "sensor Pixel Rate: %llu\n", rate);

	return rate;
}

static void cal_camerarx_lane_config(struct cal_camerarx *phy)
{
	u32 val = cal_read(phy->cal, CAL_CSI2_COMPLEXIO_CFG(phy->instance));
	u32 lane_mask = CAL_CSI2_COMPLEXIO_CFG_CLOCK_POSITION_MASK;
	u32 polarity_mask = CAL_CSI2_COMPLEXIO_CFG_CLOCK_POL_MASK;
	struct v4l2_fwnode_bus_mipi_csi2 *mipi_csi2 =
		&phy->endpoint.bus.mipi_csi2;
	int lane;

	cal_set_field(&val, mipi_csi2->clock_lane + 1, lane_mask);
	cal_set_field(&val, mipi_csi2->lane_polarities[0], polarity_mask);
	for (lane = 0; lane < mipi_csi2->num_data_lanes; lane++) {
		/*
		 * Every lane are one nibble apart starting with the
		 * clock followed by the data lanes so shift masks by 4.
		 */
		lane_mask <<= 4;
		polarity_mask <<= 4;
		cal_set_field(&val, mipi_csi2->data_lanes[lane] + 1, lane_mask);
		cal_set_field(&val, mipi_csi2->lane_polarities[lane + 1],
			      polarity_mask);
	}

	cal_write(phy->cal, CAL_CSI2_COMPLEXIO_CFG(phy->instance), val);
	phy_dbg(3, phy, "CAL_CSI2_COMPLEXIO_CFG(%d) = 0x%08x\n",
		phy->instance, val);
}

static void cal_camerarx_enable(struct cal_camerarx *phy)
{
	u32 num_lanes = phy->cal->data->camerarx[phy->instance].num_lanes;

	regmap_field_write(phy->fields[F_CAMMODE], 0);
	/* Always enable all lanes at the phy control level */
	regmap_field_write(phy->fields[F_LANEENABLE], (1 << num_lanes) - 1);
	/* F_CSI_MODE is not present on every architecture */
	if (phy->fields[F_CSI_MODE])
		regmap_field_write(phy->fields[F_CSI_MODE], 1);
	regmap_field_write(phy->fields[F_CTRLCLKEN], 1);
}

static void cal_camerarx_disable(struct cal_camerarx *phy)
{
	regmap_field_write(phy->fields[F_CTRLCLKEN], 0);
}

/*
 * TCLK values are OK at their reset values
 */
#define TCLK_TERM	0
#define TCLK_MISS	1
#define TCLK_SETTLE	14

static void cal_camerarx_config(struct cal_camerarx *phy, s64 external_rate,
				const struct cal_fmt *fmt)
{
	unsigned int reg0, reg1;
	unsigned int ths_term, ths_settle;
	unsigned int csi2_ddrclk_khz;
	struct v4l2_fwnode_bus_mipi_csi2 *mipi_csi2 =
			&phy->endpoint.bus.mipi_csi2;
	u32 num_lanes = mipi_csi2->num_data_lanes;

	/* DPHY timing configuration */

	/*
	 * CSI-2 is DDR and we only count used lanes.
	 *
	 * csi2_ddrclk_khz = external_rate / 1000
	 *		   / (2 * num_lanes) * fmt->bpp;
	 */
	csi2_ddrclk_khz = div_s64(external_rate * fmt->bpp,
				  2 * num_lanes * 1000);

	phy_dbg(1, phy, "csi2_ddrclk_khz: %d\n", csi2_ddrclk_khz);

	/* THS_TERM: Programmed value = floor(20 ns/DDRClk period) */
	ths_term = 20 * csi2_ddrclk_khz / 1000000;
	phy_dbg(1, phy, "ths_term: %d (0x%02x)\n", ths_term, ths_term);

	/* THS_SETTLE: Programmed value = floor(105 ns/DDRClk period) + 4 */
	ths_settle = (105 * csi2_ddrclk_khz / 1000000) + 4;
	phy_dbg(1, phy, "ths_settle: %d (0x%02x)\n", ths_settle, ths_settle);

	reg0 = camerarx_read(phy, CAL_CSI2_PHY_REG0);
	cal_set_field(&reg0, CAL_CSI2_PHY_REG0_HSCLOCKCONFIG_DISABLE,
		      CAL_CSI2_PHY_REG0_HSCLOCKCONFIG_MASK);
	cal_set_field(&reg0, ths_term, CAL_CSI2_PHY_REG0_THS_TERM_MASK);
	cal_set_field(&reg0, ths_settle, CAL_CSI2_PHY_REG0_THS_SETTLE_MASK);

	phy_dbg(1, phy, "CSI2_%d_REG0 = 0x%08x\n", phy->instance, reg0);
	camerarx_write(phy, CAL_CSI2_PHY_REG0, reg0);

	reg1 = camerarx_read(phy, CAL_CSI2_PHY_REG1);
	cal_set_field(&reg1, TCLK_TERM, CAL_CSI2_PHY_REG1_TCLK_TERM_MASK);
	cal_set_field(&reg1, 0xb8, CAL_CSI2_PHY_REG1_DPHY_HS_SYNC_PATTERN_MASK);
	cal_set_field(&reg1, TCLK_MISS,
		      CAL_CSI2_PHY_REG1_CTRLCLK_DIV_FACTOR_MASK);
	cal_set_field(&reg1, TCLK_SETTLE, CAL_CSI2_PHY_REG1_TCLK_SETTLE_MASK);

	phy_dbg(1, phy, "CSI2_%d_REG1 = 0x%08x\n", phy->instance, reg1);
	camerarx_write(phy, CAL_CSI2_PHY_REG1, reg1);
}

static void cal_camerarx_power(struct cal_camerarx *phy, bool enable)
{
	u32 target_state;
	unsigned int i;

	target_state = enable ? CAL_CSI2_COMPLEXIO_CFG_PWR_CMD_STATE_ON :
		       CAL_CSI2_COMPLEXIO_CFG_PWR_CMD_STATE_OFF;

	cal_write_field(phy->cal, CAL_CSI2_COMPLEXIO_CFG(phy->instance),
			target_state, CAL_CSI2_COMPLEXIO_CFG_PWR_CMD_MASK);

	for (i = 0; i < 10; i++) {
		u32 current_state;

		current_state = cal_read_field(phy->cal,
					       CAL_CSI2_COMPLEXIO_CFG(phy->instance),
					       CAL_CSI2_COMPLEXIO_CFG_PWR_STATUS_MASK);

		if (current_state == target_state)
			break;

		usleep_range(1000, 1100);
	}

	if (i == 10)
		phy_err(phy, "Failed to power %s complexio\n",
			enable ? "up" : "down");
}

static void cal_camerarx_wait_reset(struct cal_camerarx *phy)
{
	unsigned long timeout;

	timeout = jiffies + msecs_to_jiffies(750);
	while (time_before(jiffies, timeout)) {
		if (cal_read_field(phy->cal,
				   CAL_CSI2_COMPLEXIO_CFG(phy->instance),
				   CAL_CSI2_COMPLEXIO_CFG_RESET_DONE_MASK) ==
		    CAL_CSI2_COMPLEXIO_CFG_RESET_DONE_RESETCOMPLETED)
			break;
		usleep_range(500, 5000);
	}

	if (cal_read_field(phy->cal, CAL_CSI2_COMPLEXIO_CFG(phy->instance),
			   CAL_CSI2_COMPLEXIO_CFG_RESET_DONE_MASK) !=
			   CAL_CSI2_COMPLEXIO_CFG_RESET_DONE_RESETCOMPLETED)
		phy_err(phy, "Timeout waiting for Complex IO reset done\n");
}

static void cal_camerarx_wait_stop_state(struct cal_camerarx *phy)
{
	unsigned long timeout;

	timeout = jiffies + msecs_to_jiffies(750);
	while (time_before(jiffies, timeout)) {
		if (cal_read_field(phy->cal,
				   CAL_CSI2_TIMING(phy->instance),
				   CAL_CSI2_TIMING_FORCE_RX_MODE_IO1_MASK) == 0)
			break;
		usleep_range(500, 5000);
	}

	if (cal_read_field(phy->cal, CAL_CSI2_TIMING(phy->instance),
			   CAL_CSI2_TIMING_FORCE_RX_MODE_IO1_MASK) != 0)
		phy_err(phy, "Timeout waiting for stop state\n");
}

int cal_camerarx_start(struct cal_camerarx *phy, const struct cal_fmt *fmt)
{
	s64 external_rate;
	u32 sscounter;
	u32 val;
	int ret;

	external_rate = cal_camerarx_get_external_rate(phy);
	if (external_rate < 0)
		return external_rate;

	ret = v4l2_subdev_call(phy->sensor, core, s_power, 1);
	if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV) {
		phy_err(phy, "power on failed in subdev\n");
		return ret;
	}

	/*
	 * CSI-2 PHY Link Initialization Sequence, according to the DRA74xP /
	 * DRA75xP / DRA76xP / DRA77xP TRM. The DRA71x / DRA72x and the AM65x /
	 * DRA80xM TRMs have a a slightly simplified sequence.
	 */

	/*
	 * 1. Configure all CSI-2 low level protocol registers to be ready to
	 *    receive signals/data from the CSI-2 PHY.
	 *
	 *    i.-v. Configure the lanes position and polarity.
	 */
	cal_camerarx_lane_config(phy);

	/*
	 *    vi.-vii. Configure D-PHY mode, enable the required lanes and
	 *             enable the CAMERARX clock.
	 */
	cal_camerarx_enable(phy);

	/*
	 * 2. CSI PHY and link initialization sequence.
	 *
	 *    a. Deassert the CSI-2 PHY reset. Do not wait for reset completion
	 *       at this point, as it requires the external sensor to send the
	 *       CSI-2 HS clock.
	 */
	cal_write_field(phy->cal, CAL_CSI2_COMPLEXIO_CFG(phy->instance),
			CAL_CSI2_COMPLEXIO_CFG_RESET_CTRL_OPERATIONAL,
			CAL_CSI2_COMPLEXIO_CFG_RESET_CTRL_MASK);
	phy_dbg(3, phy, "CAL_CSI2_COMPLEXIO_CFG(%d) = 0x%08x De-assert Complex IO Reset\n",
		phy->instance,
		cal_read(phy->cal, CAL_CSI2_COMPLEXIO_CFG(phy->instance)));

	/* Dummy read to allow SCP reset to complete. */
	camerarx_read(phy, CAL_CSI2_PHY_REG0);

	/* Program the PHY timing parameters. */
	cal_camerarx_config(phy, external_rate, fmt);

	/*
	 *    b. Assert the FORCERXMODE signal.
	 *
	 * The stop-state-counter is based on fclk cycles, and we always use
	 * the x16 and x4 settings, so stop-state-timeout =
	 * fclk-cycle * 16 * 4 * counter.
	 *
	 * Stop-state-timeout must be more than 100us as per CSI-2 spec, so we
	 * calculate a timeout that's 100us (rounding up).
	 */
	sscounter = DIV_ROUND_UP(clk_get_rate(phy->cal->fclk), 10000 *  16 * 4);

	val = cal_read(phy->cal, CAL_CSI2_TIMING(phy->instance));
	cal_set_field(&val, 1, CAL_CSI2_TIMING_STOP_STATE_X16_IO1_MASK);
	cal_set_field(&val, 1, CAL_CSI2_TIMING_STOP_STATE_X4_IO1_MASK);
	cal_set_field(&val, sscounter,
		      CAL_CSI2_TIMING_STOP_STATE_COUNTER_IO1_MASK);
	cal_write(phy->cal, CAL_CSI2_TIMING(phy->instance), val);
	phy_dbg(3, phy, "CAL_CSI2_TIMING(%d) = 0x%08x Stop States\n",
		phy->instance,
		cal_read(phy->cal, CAL_CSI2_TIMING(phy->instance)));

	/* Assert the FORCERXMODE signal. */
	cal_write_field(phy->cal, CAL_CSI2_TIMING(phy->instance),
			1, CAL_CSI2_TIMING_FORCE_RX_MODE_IO1_MASK);
	phy_dbg(3, phy, "CAL_CSI2_TIMING(%d) = 0x%08x Force RXMODE\n",
		phy->instance,
		cal_read(phy->cal, CAL_CSI2_TIMING(phy->instance)));

	/*
	 * c. Connect pull-down on CSI-2 PHY link (using pad control).
	 *
	 * This is not required on DRA71x, DRA72x, AM65x and DRA80xM. Not
	 * implemented.
	 */

	/*
	 * d. Power up the CSI-2 PHY.
	 * e. Check whether the state status reaches the ON state.
	 */
	cal_camerarx_power(phy, true);

	/*
	 * Start the sensor to enable the CSI-2 HS clock. We can now wait for
	 * CSI-2 PHY reset to complete.
	 */
	ret = v4l2_subdev_call(phy->sensor, video, s_stream, 1);
	if (ret) {
		v4l2_subdev_call(phy->sensor, core, s_power, 0);
		phy_err(phy, "stream on failed in subdev\n");
		return ret;
	}

	cal_camerarx_wait_reset(phy);

	/* f. Wait for STOPSTATE=1 for all enabled lane modules. */
	cal_camerarx_wait_stop_state(phy);

	phy_dbg(1, phy, "CSI2_%u_REG1 = 0x%08x (bits 31-28 should be set)\n",
		phy->instance, camerarx_read(phy, CAL_CSI2_PHY_REG1));

	/*
	 * g. Disable pull-down on CSI-2 PHY link (using pad control).
	 *
	 * This is not required on DRA71x, DRA72x, AM65x and DRA80xM. Not
	 * implemented.
	 */

	return 0;
}

void cal_camerarx_stop(struct cal_camerarx *phy)
{
	unsigned int i;
	int ret;

	cal_camerarx_power(phy, false);

	/* Assert Complex IO Reset */
	cal_write_field(phy->cal, CAL_CSI2_COMPLEXIO_CFG(phy->instance),
			CAL_CSI2_COMPLEXIO_CFG_RESET_CTRL,
			CAL_CSI2_COMPLEXIO_CFG_RESET_CTRL_MASK);

	/* Wait for power down completion */
	for (i = 0; i < 10; i++) {
		if (cal_read_field(phy->cal,
				   CAL_CSI2_COMPLEXIO_CFG(phy->instance),
				   CAL_CSI2_COMPLEXIO_CFG_RESET_DONE_MASK) ==
		    CAL_CSI2_COMPLEXIO_CFG_RESET_DONE_RESETONGOING)
			break;
		usleep_range(1000, 1100);
	}
	phy_dbg(3, phy, "CAL_CSI2_COMPLEXIO_CFG(%d) = 0x%08x Complex IO in Reset (%d) %s\n",
		phy->instance,
		cal_read(phy->cal, CAL_CSI2_COMPLEXIO_CFG(phy->instance)), i,
		(i >= 10) ? "(timeout)" : "");

	/* Disable the phy */
	cal_camerarx_disable(phy);

	if (v4l2_subdev_call(phy->sensor, video, s_stream, 0))
		phy_err(phy, "stream off failed in subdev\n");

	ret = v4l2_subdev_call(phy->sensor, core, s_power, 0);
	if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
		phy_err(phy, "power off failed in subdev\n");
}

/*
 *   Errata i913: CSI2 LDO Needs to be disabled when module is powered on
 *
 *   Enabling CSI2 LDO shorts it to core supply. It is crucial the 2 CSI2
 *   LDOs on the device are disabled if CSI-2 module is powered on
 *   (0x4845 B304 | 0x4845 B384 [28:27] = 0x1) or in ULPS (0x4845 B304
 *   | 0x4845 B384 [28:27] = 0x2) mode. Common concerns include: high
 *   current draw on the module supply in active mode.
 *
 *   Errata does not apply when CSI-2 module is powered off
 *   (0x4845 B304 | 0x4845 B384 [28:27] = 0x0).
 *
 * SW Workaround:
 *	Set the following register bits to disable the LDO,
 *	which is essentially CSI2 REG10 bit 6:
 *
 *		Core 0:  0x4845 B828 = 0x0000 0040
 *		Core 1:  0x4845 B928 = 0x0000 0040
 */
static void cal_camerarx_i913_errata(struct cal_camerarx *phy)
{
	u32 reg10 = camerarx_read(phy, CAL_CSI2_PHY_REG10);

	cal_set_field(&reg10, 1, CAL_CSI2_PHY_REG10_I933_LDO_DISABLE_MASK);

	phy_dbg(1, phy, "CSI2_%d_REG10 = 0x%08x\n", phy->instance, reg10);
	camerarx_write(phy, CAL_CSI2_PHY_REG10, reg10);
}

/*
 * Enable the expected IRQ sources
 */
void cal_camerarx_enable_irqs(struct cal_camerarx *phy)
{
	u32 val;

	const u32 cio_err_mask =
		CAL_CSI2_COMPLEXIO_IRQ_LANE_ERRORS_MASK |
		CAL_CSI2_COMPLEXIO_IRQ_FIFO_OVR_MASK |
		CAL_CSI2_COMPLEXIO_IRQ_SHORT_PACKET_MASK |
		CAL_CSI2_COMPLEXIO_IRQ_ECC_NO_CORRECTION_MASK;

	/* Enable CIO error irqs */
	cal_write(phy->cal, CAL_HL_IRQENABLE_SET(0),
		  CAL_HL_IRQ_CIO_MASK(phy->instance));
	cal_write(phy->cal, CAL_CSI2_COMPLEXIO_IRQENABLE(phy->instance),
		  cio_err_mask);

	/* Always enable OCPO error */
	cal_write(phy->cal, CAL_HL_IRQENABLE_SET(0), CAL_HL_IRQ_OCPO_ERR_MASK);

	/* Enable IRQ_WDMA_END 0/1 */
	val = 0;
	cal_set_field(&val, 1, CAL_HL_IRQ_MASK(phy->instance));
	cal_write(phy->cal, CAL_HL_IRQENABLE_SET(1), val);
	/* Enable IRQ_WDMA_START 0/1 */
	val = 0;
	cal_set_field(&val, 1, CAL_HL_IRQ_MASK(phy->instance));
	cal_write(phy->cal, CAL_HL_IRQENABLE_SET(2), val);
	/* Todo: Add VC_IRQ and CSI2_COMPLEXIO_IRQ handling */
	cal_write(phy->cal, CAL_CSI2_VC_IRQENABLE(0), 0xFF000000);
}

void cal_camerarx_disable_irqs(struct cal_camerarx *phy)
{
	u32 val;

	/* Disable CIO error irqs */
	cal_write(phy->cal, CAL_HL_IRQENABLE_CLR(0),
		  CAL_HL_IRQ_CIO_MASK(phy->instance));
	cal_write(phy->cal, CAL_CSI2_COMPLEXIO_IRQENABLE(phy->instance),
		  0);

	/* Disable IRQ_WDMA_END 0/1 */
	val = 0;
	cal_set_field(&val, 1, CAL_HL_IRQ_MASK(phy->instance));
	cal_write(phy->cal, CAL_HL_IRQENABLE_CLR(1), val);
	/* Disable IRQ_WDMA_START 0/1 */
	val = 0;
	cal_set_field(&val, 1, CAL_HL_IRQ_MASK(phy->instance));
	cal_write(phy->cal, CAL_HL_IRQENABLE_CLR(2), val);
	/* Todo: Add VC_IRQ and CSI2_COMPLEXIO_IRQ handling */
	cal_write(phy->cal, CAL_CSI2_VC_IRQENABLE(0), 0);
}

void cal_camerarx_ppi_enable(struct cal_camerarx *phy)
{
	cal_write(phy->cal, CAL_CSI2_PPI_CTRL(phy->instance), BIT(3));
	cal_write_field(phy->cal, CAL_CSI2_PPI_CTRL(phy->instance),
			1, CAL_CSI2_PPI_CTRL_IF_EN_MASK);
}

void cal_camerarx_ppi_disable(struct cal_camerarx *phy)
{
	cal_write_field(phy->cal, CAL_CSI2_PPI_CTRL(phy->instance),
			0, CAL_CSI2_PPI_CTRL_IF_EN_MASK);
}

static int cal_camerarx_regmap_init(struct cal_dev *cal,
				    struct cal_camerarx *phy)
{
	const struct cal_camerarx_data *phy_data;
	unsigned int i;

	if (!cal->data)
		return -EINVAL;

	phy_data = &cal->data->camerarx[phy->instance];

	for (i = 0; i < F_MAX_FIELDS; i++) {
		struct reg_field field = {
			.reg = cal->syscon_camerrx_offset,
			.lsb = phy_data->fields[i].lsb,
			.msb = phy_data->fields[i].msb,
		};

		/*
		 * Here we update the reg offset with the
		 * value found in DT
		 */
		phy->fields[i] = devm_regmap_field_alloc(cal->dev,
							 cal->syscon_camerrx,
							 field);
		if (IS_ERR(phy->fields[i])) {
			cal_err(cal, "Unable to allocate regmap fields\n");
			return PTR_ERR(phy->fields[i]);
		}
	}

	return 0;
}

static int cal_camerarx_parse_dt(struct cal_camerarx *phy)
{
	struct v4l2_fwnode_endpoint *endpoint = &phy->endpoint;
	struct device_node *ep_node;
	char data_lanes[V4L2_FWNODE_CSI2_MAX_DATA_LANES * 2];
	unsigned int i;
	int ret;

	/*
	 * Find the endpoint node for the port corresponding to the PHY
	 * instance, and parse its CSI-2-related properties.
	 */
	ep_node = of_graph_get_endpoint_by_regs(phy->cal->dev->of_node,
						phy->instance, 0);
	if (!ep_node) {
		/*
		 * The endpoint is not mandatory, not all PHY instances need to
		 * be connected in DT.
		 */
		phy_dbg(3, phy, "Port has no endpoint\n");
		return 0;
	}

	endpoint->bus_type = V4L2_MBUS_CSI2_DPHY;
	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(ep_node), endpoint);
	if (ret < 0) {
		phy_err(phy, "Failed to parse endpoint\n");
		goto done;
	}

	for (i = 0; i < endpoint->bus.mipi_csi2.num_data_lanes; i++) {
		unsigned int lane = endpoint->bus.mipi_csi2.data_lanes[i];

		if (lane > 4) {
			phy_err(phy, "Invalid position %u for data lane %u\n",
				lane, i);
			ret = -EINVAL;
			goto done;
		}

		data_lanes[i*2] = '0' + lane;
		data_lanes[i*2+1] = ' ';
	}

	data_lanes[i*2-1] = '\0';

	phy_dbg(3, phy,
		"CSI-2 bus: clock lane <%u>, data lanes <%s>, flags 0x%08x\n",
		endpoint->bus.mipi_csi2.clock_lane, data_lanes,
		endpoint->bus.mipi_csi2.flags);

	/* Retrieve the connected device and store it for later use. */
	phy->sensor_node = of_graph_get_remote_port_parent(ep_node);
	if (!phy->sensor_node) {
		phy_dbg(3, phy, "Can't get remote parent\n");
		ret = -EINVAL;
		goto done;
	}

	phy_dbg(1, phy, "Found connected device %pOFn\n", phy->sensor_node);

done:
	of_node_put(ep_node);
	return ret;
}

static struct cal_camerarx *cal_camerarx_create(struct cal_dev *cal,
						unsigned int instance)
{
	struct platform_device *pdev = to_platform_device(cal->dev);
	struct cal_camerarx *phy;
	int ret;

	phy = kzalloc(sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return ERR_PTR(-ENOMEM);

	phy->cal = cal;
	phy->instance = instance;

	phy->res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						(instance == 0) ?
						"cal_rx_core0" :
						"cal_rx_core1");
	phy->base = devm_ioremap_resource(cal->dev, phy->res);
	if (IS_ERR(phy->base)) {
		cal_err(cal, "failed to ioremap\n");
		ret = PTR_ERR(phy->base);
		goto error;
	}

	cal_dbg(1, cal, "ioresource %s at %pa - %pa\n",
		phy->res->name, &phy->res->start, &phy->res->end);

	ret = cal_camerarx_regmap_init(cal, phy);
	if (ret)
		goto error;

	ret = cal_camerarx_parse_dt(phy);
	if (ret)
		goto error;

	return phy;

error:
	kfree(phy);
	return ERR_PTR(ret);
}

static void cal_camerarx_destroy(struct cal_camerarx *phy)
{
	if (!phy)
		return;

	of_node_put(phy->sensor_node);
	kfree(phy);
}

static int cal_camerarx_init_regmap(struct cal_dev *cal)
{
	struct platform_device *pdev = to_platform_device(cal->dev);
	struct device_node *np = cal->dev->of_node;
	struct regmap_config config = { };
	struct regmap *syscon;
	struct resource *res;
	unsigned int offset;
	void __iomem *base;

	syscon = syscon_regmap_lookup_by_phandle_args(np, "ti,camerrx-control",
						      1, &offset);
	if (!IS_ERR(syscon)) {
		cal->syscon_camerrx = syscon;
		cal->syscon_camerrx_offset = offset;
		return 0;
	}

	dev_warn(cal->dev, "failed to get ti,camerrx-control: %ld\n",
		 PTR_ERR(syscon));

	/*
	 * Backward DTS compatibility. If syscon entry is not present then
	 * check if the camerrx_control resource is present.
	 */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "camerrx_control");
	base = devm_ioremap_resource(cal->dev, res);
	if (IS_ERR(base)) {
		cal_err(cal, "failed to ioremap camerrx_control\n");
		return PTR_ERR(base);
	}

	cal_dbg(1, cal, "ioresource %s at %pa - %pa\n",
		res->name, &res->start, &res->end);

	config.reg_bits = 32;
	config.reg_stride = 4;
	config.val_bits = 32;
	config.max_register = resource_size(res) - 4;

	syscon = regmap_init_mmio(NULL, base, &config);
	if (IS_ERR(syscon)) {
		pr_err("regmap init failed\n");
		return PTR_ERR(syscon);
	}

	/*
	 * In this case the base already point to the direct CM register so no
	 * need for an offset.
	 */
	cal->syscon_camerrx = syscon;
	cal->syscon_camerrx_offset = 0;

	return 0;
}

/* ------------------------------------------------------------------
 *	Context Management
 * ------------------------------------------------------------------
 */

void cal_ctx_csi2_config(struct cal_ctx *ctx)
{
	u32 val;

	val = cal_read(ctx->cal, CAL_CSI2_CTX0(ctx->index));
	cal_set_field(&val, ctx->cport, CAL_CSI2_CTX_CPORT_MASK);
	/*
	 * DT type: MIPI CSI-2 Specs
	 *   0x1: All - DT filter is disabled
	 *  0x24: RGB888 1 pixel  = 3 bytes
	 *  0x2B: RAW10  4 pixels = 5 bytes
	 *  0x2A: RAW8   1 pixel  = 1 byte
	 *  0x1E: YUV422 2 pixels = 4 bytes
	 */
	cal_set_field(&val, 0x1, CAL_CSI2_CTX_DT_MASK);
	cal_set_field(&val, 0, CAL_CSI2_CTX_VC_MASK);
	cal_set_field(&val, ctx->v_fmt.fmt.pix.height, CAL_CSI2_CTX_LINES_MASK);
	cal_set_field(&val, CAL_CSI2_CTX_ATT_PIX, CAL_CSI2_CTX_ATT_MASK);
	cal_set_field(&val, CAL_CSI2_CTX_PACK_MODE_LINE,
		      CAL_CSI2_CTX_PACK_MODE_MASK);
	cal_write(ctx->cal, CAL_CSI2_CTX0(ctx->index), val);
	ctx_dbg(3, ctx, "CAL_CSI2_CTX0(%d) = 0x%08x\n", ctx->index,
		cal_read(ctx->cal, CAL_CSI2_CTX0(ctx->index)));
}

void cal_ctx_pix_proc_config(struct cal_ctx *ctx)
{
	u32 val, extract, pack;

	switch (ctx->fmt->bpp) {
	case 8:
		extract = CAL_PIX_PROC_EXTRACT_B8;
		pack = CAL_PIX_PROC_PACK_B8;
		break;
	case 10:
		extract = CAL_PIX_PROC_EXTRACT_B10_MIPI;
		pack = CAL_PIX_PROC_PACK_B16;
		break;
	case 12:
		extract = CAL_PIX_PROC_EXTRACT_B12_MIPI;
		pack = CAL_PIX_PROC_PACK_B16;
		break;
	case 16:
		extract = CAL_PIX_PROC_EXTRACT_B16_LE;
		pack = CAL_PIX_PROC_PACK_B16;
		break;
	default:
		/*
		 * If you see this warning then it means that you added
		 * some new entry in the cal_formats[] array with a different
		 * bit per pixel values then the one supported below.
		 * Either add support for the new bpp value below or adjust
		 * the new entry to use one of the value below.
		 *
		 * Instead of failing here just use 8 bpp as a default.
		 */
		dev_warn_once(ctx->cal->dev,
			      "%s:%d:%s: bpp:%d unsupported! Overwritten with 8.\n",
			      __FILE__, __LINE__, __func__, ctx->fmt->bpp);
		extract = CAL_PIX_PROC_EXTRACT_B8;
		pack = CAL_PIX_PROC_PACK_B8;
		break;
	}

	val = cal_read(ctx->cal, CAL_PIX_PROC(ctx->index));
	cal_set_field(&val, extract, CAL_PIX_PROC_EXTRACT_MASK);
	cal_set_field(&val, CAL_PIX_PROC_DPCMD_BYPASS, CAL_PIX_PROC_DPCMD_MASK);
	cal_set_field(&val, CAL_PIX_PROC_DPCME_BYPASS, CAL_PIX_PROC_DPCME_MASK);
	cal_set_field(&val, pack, CAL_PIX_PROC_PACK_MASK);
	cal_set_field(&val, ctx->cport, CAL_PIX_PROC_CPORT_MASK);
	cal_set_field(&val, 1, CAL_PIX_PROC_EN_MASK);
	cal_write(ctx->cal, CAL_PIX_PROC(ctx->index), val);
	ctx_dbg(3, ctx, "CAL_PIX_PROC(%d) = 0x%08x\n", ctx->index,
		cal_read(ctx->cal, CAL_PIX_PROC(ctx->index)));
}

void cal_ctx_wr_dma_config(struct cal_ctx *ctx, unsigned int width,
			    unsigned int height)
{
	u32 val;

	val = cal_read(ctx->cal, CAL_WR_DMA_CTRL(ctx->index));
	cal_set_field(&val, ctx->cport, CAL_WR_DMA_CTRL_CPORT_MASK);
	cal_set_field(&val, height, CAL_WR_DMA_CTRL_YSIZE_MASK);
	cal_set_field(&val, CAL_WR_DMA_CTRL_DTAG_PIX_DAT,
		      CAL_WR_DMA_CTRL_DTAG_MASK);
	cal_set_field(&val, CAL_WR_DMA_CTRL_MODE_CONST,
		      CAL_WR_DMA_CTRL_MODE_MASK);
	cal_set_field(&val, CAL_WR_DMA_CTRL_PATTERN_LINEAR,
		      CAL_WR_DMA_CTRL_PATTERN_MASK);
	cal_set_field(&val, 1, CAL_WR_DMA_CTRL_STALL_RD_MASK);
	cal_write(ctx->cal, CAL_WR_DMA_CTRL(ctx->index), val);
	ctx_dbg(3, ctx, "CAL_WR_DMA_CTRL(%d) = 0x%08x\n", ctx->index,
		cal_read(ctx->cal, CAL_WR_DMA_CTRL(ctx->index)));

	/*
	 * width/16 not sure but giving it a whirl.
	 * zero does not work right
	 */
	cal_write_field(ctx->cal,
			CAL_WR_DMA_OFST(ctx->index),
			(width / 16),
			CAL_WR_DMA_OFST_MASK);
	ctx_dbg(3, ctx, "CAL_WR_DMA_OFST(%d) = 0x%08x\n", ctx->index,
		cal_read(ctx->cal, CAL_WR_DMA_OFST(ctx->index)));

	val = cal_read(ctx->cal, CAL_WR_DMA_XSIZE(ctx->index));
	/* 64 bit word means no skipping */
	cal_set_field(&val, 0, CAL_WR_DMA_XSIZE_XSKIP_MASK);
	/*
	 * (width*8)/64 this should be size of an entire line
	 * in 64bit word but 0 means all data until the end
	 * is detected automagically
	 */
	cal_set_field(&val, (width / 8), CAL_WR_DMA_XSIZE_MASK);
	cal_write(ctx->cal, CAL_WR_DMA_XSIZE(ctx->index), val);
	ctx_dbg(3, ctx, "CAL_WR_DMA_XSIZE(%d) = 0x%08x\n", ctx->index,
		cal_read(ctx->cal, CAL_WR_DMA_XSIZE(ctx->index)));

	val = cal_read(ctx->cal, CAL_CTRL);
	cal_set_field(&val, CAL_CTRL_BURSTSIZE_BURST128,
		      CAL_CTRL_BURSTSIZE_MASK);
	cal_set_field(&val, 0xF, CAL_CTRL_TAGCNT_MASK);
	cal_set_field(&val, CAL_CTRL_POSTED_WRITES_NONPOSTED,
		      CAL_CTRL_POSTED_WRITES_MASK);
	cal_set_field(&val, 0xFF, CAL_CTRL_MFLAGL_MASK);
	cal_set_field(&val, 0xFF, CAL_CTRL_MFLAGH_MASK);
	cal_write(ctx->cal, CAL_CTRL, val);
	ctx_dbg(3, ctx, "CAL_CTRL = 0x%08x\n", cal_read(ctx->cal, CAL_CTRL));
}

void cal_ctx_wr_dma_addr(struct cal_ctx *ctx, unsigned int dmaaddr)
{
	cal_write(ctx->cal, CAL_WR_DMA_ADDR(ctx->index), dmaaddr);
}

/* ------------------------------------------------------------------
 *	IRQ Handling
 * ------------------------------------------------------------------
 */

static inline void cal_schedule_next_buffer(struct cal_ctx *ctx)
{
	struct cal_dmaqueue *dma_q = &ctx->vidq;
	struct cal_buffer *buf;
	unsigned long addr;

	buf = list_entry(dma_q->active.next, struct cal_buffer, list);
	ctx->next_frm = buf;
	list_del(&buf->list);

	addr = vb2_dma_contig_plane_dma_addr(&buf->vb.vb2_buf, 0);
	cal_ctx_wr_dma_addr(ctx, addr);
}

static inline void cal_process_buffer_complete(struct cal_ctx *ctx)
{
	ctx->cur_frm->vb.vb2_buf.timestamp = ktime_get_ns();
	ctx->cur_frm->vb.field = ctx->m_fmt.field;
	ctx->cur_frm->vb.sequence = ctx->sequence++;

	vb2_buffer_done(&ctx->cur_frm->vb.vb2_buf, VB2_BUF_STATE_DONE);
	ctx->cur_frm = ctx->next_frm;
}

static irqreturn_t cal_irq(int irq_cal, void *data)
{
	struct cal_dev *cal = data;
	struct cal_ctx *ctx;
	struct cal_dmaqueue *dma_q;
	u32 status;

	status = cal_read(cal, CAL_HL_IRQSTATUS(0));
	if (status) {
		unsigned int i;

		cal_write(cal, CAL_HL_IRQSTATUS(0), status);

		if (status & CAL_HL_IRQ_OCPO_ERR_MASK)
			dev_err_ratelimited(cal->dev, "OCPO ERROR\n");

		for (i = 0; i < CAL_NUM_CSI2_PORTS; ++i) {
			if (status & CAL_HL_IRQ_CIO_MASK(i)) {
				u32 cio_stat = cal_read(cal,
							CAL_CSI2_COMPLEXIO_IRQSTATUS(i));

				dev_err_ratelimited(cal->dev,
						    "CIO%u error: %#08x\n", i, cio_stat);

				cal_write(cal, CAL_CSI2_COMPLEXIO_IRQSTATUS(i),
					  cio_stat);
			}
		}
	}

	/* Check which DMA just finished */
	status = cal_read(cal, CAL_HL_IRQSTATUS(1));
	if (status) {
		unsigned int i;

		/* Clear Interrupt status */
		cal_write(cal, CAL_HL_IRQSTATUS(1), status);

		for (i = 0; i < ARRAY_SIZE(cal->ctx); ++i) {
			if (status & CAL_HL_IRQ_MASK(i)) {
				ctx = cal->ctx[i];

				spin_lock(&ctx->slock);
				ctx->dma_act = false;

				if (ctx->cur_frm != ctx->next_frm)
					cal_process_buffer_complete(ctx);

				spin_unlock(&ctx->slock);
			}
		}
	}

	/* Check which DMA just started */
	status = cal_read(cal, CAL_HL_IRQSTATUS(2));
	if (status) {
		unsigned int i;

		/* Clear Interrupt status */
		cal_write(cal, CAL_HL_IRQSTATUS(2), status);

		for (i = 0; i < ARRAY_SIZE(cal->ctx); ++i) {
			if (status & CAL_HL_IRQ_MASK(i)) {
				ctx = cal->ctx[i];
				dma_q = &ctx->vidq;

				spin_lock(&ctx->slock);
				ctx->dma_act = true;
				if (!list_empty(&dma_q->active) &&
				    ctx->cur_frm == ctx->next_frm)
					cal_schedule_next_buffer(ctx);
				spin_unlock(&ctx->slock);
			}
		}
	}

	return IRQ_HANDLED;
}

/* ------------------------------------------------------------------
 *	Asynchronous V4L2 subdev binding
 * ------------------------------------------------------------------
 */

struct cal_v4l2_async_subdev {
	struct v4l2_async_subdev asd;
	struct cal_camerarx *phy;
};

static inline struct cal_v4l2_async_subdev *
to_cal_asd(struct v4l2_async_subdev *asd)
{
	return container_of(asd, struct cal_v4l2_async_subdev, asd);
}

static int cal_async_notifier_bound(struct v4l2_async_notifier *notifier,
				    struct v4l2_subdev *subdev,
				    struct v4l2_async_subdev *asd)
{
	struct cal_camerarx *phy = to_cal_asd(asd)->phy;

	if (phy->sensor) {
		phy_info(phy, "Rejecting subdev %s (Already set!!)",
			 subdev->name);
		return 0;
	}

	phy->sensor = subdev;
	phy_dbg(1, phy, "Using sensor %s for capture\n", subdev->name);

	return 0;
}

static int cal_async_notifier_complete(struct v4l2_async_notifier *notifier)
{
	struct cal_dev *cal = container_of(notifier, struct cal_dev, notifier);
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(cal->ctx); ++i) {
		if (cal->ctx[i])
			cal_ctx_v4l2_register(cal->ctx[i]);
	}

	return 0;
}

static const struct v4l2_async_notifier_operations cal_async_notifier_ops = {
	.bound = cal_async_notifier_bound,
	.complete = cal_async_notifier_complete,
};

static int cal_async_notifier_register(struct cal_dev *cal)
{
	unsigned int i;
	int ret;

	v4l2_async_notifier_init(&cal->notifier);
	cal->notifier.ops = &cal_async_notifier_ops;

	for (i = 0; i < ARRAY_SIZE(cal->phy); ++i) {
		struct cal_camerarx *phy = cal->phy[i];
		struct cal_v4l2_async_subdev *casd;
		struct v4l2_async_subdev *asd;
		struct fwnode_handle *fwnode;

		if (!phy || !phy->sensor_node)
			continue;

		fwnode = of_fwnode_handle(phy->sensor_node);
		asd = v4l2_async_notifier_add_fwnode_subdev(&cal->notifier,
							    fwnode,
							    sizeof(*asd));
		if (IS_ERR(asd)) {
			phy_err(phy, "Failed to add subdev to notifier\n");
			ret = PTR_ERR(asd);
			goto error;
		}

		casd = to_cal_asd(asd);
		casd->phy = phy;
	}

	ret = v4l2_async_notifier_register(&cal->v4l2_dev, &cal->notifier);
	if (ret) {
		cal_err(cal, "Error registering async notifier\n");
		goto error;
	}

	return 0;

error:
	v4l2_async_notifier_cleanup(&cal->notifier);
	return ret;
}

static void cal_async_notifier_unregister(struct cal_dev *cal)
{
	v4l2_async_notifier_unregister(&cal->notifier);
	v4l2_async_notifier_cleanup(&cal->notifier);
}

/* ------------------------------------------------------------------
 *	Media and V4L2 device handling
 * ------------------------------------------------------------------
 */

/*
 * Register user-facing devices. To be called at the end of the probe function
 * when all resources are initialized and ready.
 */
static int cal_media_register(struct cal_dev *cal)
{
	int ret;

	ret = media_device_register(&cal->mdev);
	if (ret) {
		cal_err(cal, "Failed to register media device\n");
		return ret;
	}

	/*
	 * Register the async notifier. This may trigger registration of the
	 * V4L2 video devices if all subdevs are ready.
	 */
	ret = cal_async_notifier_register(cal);
	if (ret) {
		media_device_unregister(&cal->mdev);
		return ret;
	}

	return 0;
}

/*
 * Unregister the user-facing devices, but don't free memory yet. To be called
 * at the beginning of the remove function, to disallow access from userspace.
 */
static void cal_media_unregister(struct cal_dev *cal)
{
	unsigned int i;

	/* Unregister all the V4L2 video devices. */
	for (i = 0; i < ARRAY_SIZE(cal->ctx); i++) {
		if (cal->ctx[i])
			cal_ctx_v4l2_unregister(cal->ctx[i]);
	}

	cal_async_notifier_unregister(cal);
	media_device_unregister(&cal->mdev);
}

/*
 * Initialize the in-kernel objects. To be called at the beginning of the probe
 * function, before the V4L2 device is used by the driver.
 */
static int cal_media_init(struct cal_dev *cal)
{
	struct media_device *mdev = &cal->mdev;
	int ret;

	mdev->dev = cal->dev;
	mdev->hw_revision = cal->revision;
	strscpy(mdev->model, "CAL", sizeof(mdev->model));
	snprintf(mdev->bus_info, sizeof(mdev->bus_info), "platform:%s",
		 dev_name(mdev->dev));
	media_device_init(mdev);

	/*
	 * Initialize the V4L2 device (despite the function name, this performs
	 * initialization, not registration).
	 */
	cal->v4l2_dev.mdev = mdev;
	ret = v4l2_device_register(cal->dev, &cal->v4l2_dev);
	if (ret) {
		cal_err(cal, "Failed to register V4L2 device\n");
		return ret;
	}

	vb2_dma_contig_set_max_seg_size(cal->dev, DMA_BIT_MASK(32));

	return 0;
}

/*
 * Cleanup the in-kernel objects, freeing memory. To be called at the very end
 * of the remove sequence, when nothing (including userspace) can access the
 * objects anymore.
 */
static void cal_media_cleanup(struct cal_dev *cal)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(cal->ctx); i++) {
		if (cal->ctx[i])
			cal_ctx_v4l2_cleanup(cal->ctx[i]);
	}

	v4l2_device_unregister(&cal->v4l2_dev);
	media_device_cleanup(&cal->mdev);

	vb2_dma_contig_clear_max_seg_size(cal->dev);
}

/* ------------------------------------------------------------------
 *	Initialization and module stuff
 * ------------------------------------------------------------------
 */

static struct cal_ctx *cal_ctx_create(struct cal_dev *cal, int inst)
{
	struct cal_ctx *ctx;
	int ret;

	ctx = devm_kzalloc(cal->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	ctx->cal = cal;
	ctx->phy = cal->phy[inst];
	ctx->index = inst;
	ctx->cport = inst;

	ret = cal_ctx_v4l2_init(ctx);
	if (ret)
		return NULL;

	return ctx;
}

static const struct of_device_id cal_of_match[] = {
	{
		.compatible = "ti,dra72-cal",
		.data = (void *)&dra72x_cal_data,
	},
	{
		.compatible = "ti,dra72-pre-es2-cal",
		.data = (void *)&dra72x_es1_cal_data,
	},
	{
		.compatible = "ti,dra76-cal",
		.data = (void *)&dra76x_cal_data,
	},
	{
		.compatible = "ti,am654-cal",
		.data = (void *)&am654_cal_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, cal_of_match);

/* Get hardware revision and info. */

#define CAL_HL_HWINFO_VALUE		0xa3c90469

static void cal_get_hwinfo(struct cal_dev *cal)
{
	u32 hwinfo;

	cal->revision = cal_read(cal, CAL_HL_REVISION);
	switch (FIELD_GET(CAL_HL_REVISION_SCHEME_MASK, cal->revision)) {
	case CAL_HL_REVISION_SCHEME_H08:
		cal_dbg(3, cal, "CAL HW revision %lu.%lu.%lu (0x%08x)\n",
			FIELD_GET(CAL_HL_REVISION_MAJOR_MASK, cal->revision),
			FIELD_GET(CAL_HL_REVISION_MINOR_MASK, cal->revision),
			FIELD_GET(CAL_HL_REVISION_RTL_MASK, cal->revision),
			cal->revision);
		break;

	case CAL_HL_REVISION_SCHEME_LEGACY:
	default:
		cal_info(cal, "Unexpected CAL HW revision 0x%08x\n",
			 cal->revision);
		break;
	}

	hwinfo = cal_read(cal, CAL_HL_HWINFO);
	if (hwinfo != CAL_HL_HWINFO_VALUE)
		cal_info(cal, "CAL_HL_HWINFO = 0x%08x, expected 0x%08x\n",
			 hwinfo, CAL_HL_HWINFO_VALUE);
}

static int cal_probe(struct platform_device *pdev)
{
	struct cal_dev *cal;
	struct cal_ctx *ctx;
	bool connected = false;
	unsigned int i;
	int ret;
	int irq;

	cal = devm_kzalloc(&pdev->dev, sizeof(*cal), GFP_KERNEL);
	if (!cal)
		return -ENOMEM;

	cal->data = of_device_get_match_data(&pdev->dev);
	if (!cal->data) {
		dev_err(&pdev->dev, "Could not get feature data based on compatible version\n");
		return -ENODEV;
	}

	cal->dev = &pdev->dev;
	platform_set_drvdata(pdev, cal);

	/* Acquire resources: clocks, CAMERARX regmap, I/O memory and IRQ. */
	cal->fclk = devm_clk_get(&pdev->dev, "fck");
	if (IS_ERR(cal->fclk)) {
		dev_err(&pdev->dev, "cannot get CAL fclk\n");
		return PTR_ERR(cal->fclk);
	}

	ret = cal_camerarx_init_regmap(cal);
	if (ret < 0)
		return ret;

	cal->res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"cal_top");
	cal->base = devm_ioremap_resource(&pdev->dev, cal->res);
	if (IS_ERR(cal->base))
		return PTR_ERR(cal->base);

	cal_dbg(1, cal, "ioresource %s at %pa - %pa\n",
		cal->res->name, &cal->res->start, &cal->res->end);

	irq = platform_get_irq(pdev, 0);
	cal_dbg(1, cal, "got irq# %d\n", irq);
	ret = devm_request_irq(&pdev->dev, irq, cal_irq, 0, CAL_MODULE_NAME,
			       cal);
	if (ret)
		return ret;

	/* Read the revision and hardware info to verify hardware access. */
	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret)
		goto error_pm_runtime;

	cal_get_hwinfo(cal);
	pm_runtime_put_sync(&pdev->dev);

	/* Create CAMERARX PHYs. */
	for (i = 0; i < cal->data->num_csi2_phy; ++i) {
		cal->phy[i] = cal_camerarx_create(cal, i);
		if (IS_ERR(cal->phy[i])) {
			ret = PTR_ERR(cal->phy[i]);
			cal->phy[i] = NULL;
			goto error_camerarx;
		}

		if (cal->phy[i]->sensor_node)
			connected = true;
	}

	if (!connected) {
		cal_err(cal, "Neither port is configured, no point in staying up\n");
		ret = -ENODEV;
		goto error_camerarx;
	}

	/* Initialize the media device. */
	ret = cal_media_init(cal);
	if (ret < 0)
		goto error_camerarx;

	/* Create contexts. */
	for (i = 0; i < cal->data->num_csi2_phy; ++i) {
		if (!cal->phy[i]->sensor_node)
			continue;

		cal->ctx[i] = cal_ctx_create(cal, i);
		if (!cal->ctx[i]) {
			cal_err(cal, "Failed to create context %u\n", i);
			ret = -ENODEV;
			goto error_context;
		}
	}

	/* Register the media device. */
	ret = cal_media_register(cal);
	if (ret)
		goto error_context;

	return 0;

error_context:
	for (i = 0; i < ARRAY_SIZE(cal->ctx); i++) {
		ctx = cal->ctx[i];
		if (ctx)
			cal_ctx_v4l2_cleanup(ctx);
	}

	cal_media_cleanup(cal);

error_camerarx:
	for (i = 0; i < ARRAY_SIZE(cal->phy); i++)
		cal_camerarx_destroy(cal->phy[i]);

error_pm_runtime:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int cal_remove(struct platform_device *pdev)
{
	struct cal_dev *cal = platform_get_drvdata(pdev);
	unsigned int i;

	cal_dbg(1, cal, "Removing %s\n", CAL_MODULE_NAME);

	pm_runtime_get_sync(&pdev->dev);

	cal_media_unregister(cal);

	for (i = 0; i < ARRAY_SIZE(cal->phy); i++) {
		if (cal->phy[i])
			cal_camerarx_disable(cal->phy[i]);
	}

	cal_media_cleanup(cal);

	for (i = 0; i < ARRAY_SIZE(cal->phy); i++)
		cal_camerarx_destroy(cal->phy[i]);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static int cal_runtime_resume(struct device *dev)
{
	struct cal_dev *cal = dev_get_drvdata(dev);

	if (cal->data->flags & DRA72_CAL_PRE_ES2_LDO_DISABLE) {
		/*
		 * Apply errata on both port everytime we (re-)enable
		 * the clock
		 */
		cal_camerarx_i913_errata(cal->phy[0]);
		cal_camerarx_i913_errata(cal->phy[1]);
	}

	return 0;
}

static const struct dev_pm_ops cal_pm_ops = {
	.runtime_resume = cal_runtime_resume,
};

static struct platform_driver cal_pdrv = {
	.probe		= cal_probe,
	.remove		= cal_remove,
	.driver		= {
		.name	= CAL_MODULE_NAME,
		.pm	= &cal_pm_ops,
		.of_match_table = cal_of_match,
	},
};

module_platform_driver(cal_pdrv);

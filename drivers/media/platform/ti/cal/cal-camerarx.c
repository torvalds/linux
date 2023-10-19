// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI Camera Access Layer (CAL) - CAMERARX
 *
 * Copyright (c) 2015-2020 Texas Instruments Inc.
 *
 * Authors:
 *	Benoit Parrot <bparrot@ti.com>
 *	Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#include "cal.h"
#include "cal_regs.h"

/* ------------------------------------------------------------------
 *	I/O Register Accessors
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

/* ------------------------------------------------------------------
 *	CAMERARX Management
 * ------------------------------------------------------------------
 */

static s64 cal_camerarx_get_ext_link_freq(struct cal_camerarx *phy)
{
	struct v4l2_mbus_config_mipi_csi2 *mipi_csi2 = &phy->endpoint.bus.mipi_csi2;
	u32 num_lanes = mipi_csi2->num_data_lanes;
	const struct cal_format_info *fmtinfo;
	u32 bpp;
	s64 freq;

	fmtinfo = cal_format_by_code(phy->formats[CAL_CAMERARX_PAD_SINK].code);
	if (!fmtinfo)
		return -EINVAL;

	bpp = fmtinfo->bpp;

	freq = v4l2_get_link_freq(phy->source->ctrl_handler, bpp, 2 * num_lanes);
	if (freq < 0) {
		phy_err(phy, "failed to get link freq for subdev '%s'\n",
			phy->source->name);
		return freq;
	}

	phy_dbg(3, phy, "Source Link Freq: %llu\n", freq);

	return freq;
}

static void cal_camerarx_lane_config(struct cal_camerarx *phy)
{
	u32 val = cal_read(phy->cal, CAL_CSI2_COMPLEXIO_CFG(phy->instance));
	u32 lane_mask = CAL_CSI2_COMPLEXIO_CFG_CLOCK_POSITION_MASK;
	u32 polarity_mask = CAL_CSI2_COMPLEXIO_CFG_CLOCK_POL_MASK;
	struct v4l2_mbus_config_mipi_csi2 *mipi_csi2 =
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

void cal_camerarx_disable(struct cal_camerarx *phy)
{
	regmap_field_write(phy->fields[F_CTRLCLKEN], 0);
}

/*
 * TCLK values are OK at their reset values
 */
#define TCLK_TERM	0
#define TCLK_MISS	1
#define TCLK_SETTLE	14

static void cal_camerarx_config(struct cal_camerarx *phy, s64 link_freq)
{
	unsigned int reg0, reg1;
	unsigned int ths_term, ths_settle;

	/* DPHY timing configuration */

	/* THS_TERM: Programmed value = floor(20 ns/DDRClk period) */
	ths_term = div_s64(20 * link_freq, 1000 * 1000 * 1000);
	phy_dbg(1, phy, "ths_term: %d (0x%02x)\n", ths_term, ths_term);

	/* THS_SETTLE: Programmed value = floor(105 ns/DDRClk period) + 4 */
	ths_settle = div_s64(105 * link_freq, 1000 * 1000 * 1000) + 4;
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

static void cal_camerarx_enable_irqs(struct cal_camerarx *phy)
{
	const u32 cio_err_mask =
		CAL_CSI2_COMPLEXIO_IRQ_LANE_ERRORS_MASK |
		CAL_CSI2_COMPLEXIO_IRQ_FIFO_OVR_MASK |
		CAL_CSI2_COMPLEXIO_IRQ_SHORT_PACKET_MASK |
		CAL_CSI2_COMPLEXIO_IRQ_ECC_NO_CORRECTION_MASK;
	const u32 vc_err_mask =
		CAL_CSI2_VC_IRQ_CS_IRQ_MASK(0) |
		CAL_CSI2_VC_IRQ_CS_IRQ_MASK(1) |
		CAL_CSI2_VC_IRQ_CS_IRQ_MASK(2) |
		CAL_CSI2_VC_IRQ_CS_IRQ_MASK(3) |
		CAL_CSI2_VC_IRQ_ECC_CORRECTION_IRQ_MASK(0) |
		CAL_CSI2_VC_IRQ_ECC_CORRECTION_IRQ_MASK(1) |
		CAL_CSI2_VC_IRQ_ECC_CORRECTION_IRQ_MASK(2) |
		CAL_CSI2_VC_IRQ_ECC_CORRECTION_IRQ_MASK(3);

	/* Enable CIO & VC error IRQs. */
	cal_write(phy->cal, CAL_HL_IRQENABLE_SET(0),
		  CAL_HL_IRQ_CIO_MASK(phy->instance) |
		  CAL_HL_IRQ_VC_MASK(phy->instance));
	cal_write(phy->cal, CAL_CSI2_COMPLEXIO_IRQENABLE(phy->instance),
		  cio_err_mask);
	cal_write(phy->cal, CAL_CSI2_VC_IRQENABLE(phy->instance),
		  vc_err_mask);
}

static void cal_camerarx_disable_irqs(struct cal_camerarx *phy)
{
	/* Disable CIO error irqs */
	cal_write(phy->cal, CAL_HL_IRQENABLE_CLR(0),
		  CAL_HL_IRQ_CIO_MASK(phy->instance) |
		  CAL_HL_IRQ_VC_MASK(phy->instance));
	cal_write(phy->cal, CAL_CSI2_COMPLEXIO_IRQENABLE(phy->instance), 0);
	cal_write(phy->cal, CAL_CSI2_VC_IRQENABLE(phy->instance), 0);
}

static void cal_camerarx_ppi_enable(struct cal_camerarx *phy)
{
	cal_write_field(phy->cal, CAL_CSI2_PPI_CTRL(phy->instance),
			1, CAL_CSI2_PPI_CTRL_ECC_EN_MASK);

	cal_write_field(phy->cal, CAL_CSI2_PPI_CTRL(phy->instance),
			1, CAL_CSI2_PPI_CTRL_IF_EN_MASK);
}

static void cal_camerarx_ppi_disable(struct cal_camerarx *phy)
{
	cal_write_field(phy->cal, CAL_CSI2_PPI_CTRL(phy->instance),
			0, CAL_CSI2_PPI_CTRL_IF_EN_MASK);
}

static int cal_camerarx_start(struct cal_camerarx *phy)
{
	s64 link_freq;
	u32 sscounter;
	u32 val;
	int ret;

	if (phy->enable_count > 0) {
		phy->enable_count++;
		return 0;
	}

	link_freq = cal_camerarx_get_ext_link_freq(phy);
	if (link_freq < 0)
		return link_freq;

	ret = v4l2_subdev_call(phy->source, core, s_power, 1);
	if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV) {
		phy_err(phy, "power on failed in subdev\n");
		return ret;
	}

	cal_camerarx_enable_irqs(phy);

	/*
	 * CSI-2 PHY Link Initialization Sequence, according to the DRA74xP /
	 * DRA75xP / DRA76xP / DRA77xP TRM. The DRA71x / DRA72x and the AM65x /
	 * DRA80xM TRMs have a slightly simplified sequence.
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
	 *       at this point, as it requires the external source to send the
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
	cal_camerarx_config(phy, link_freq);

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
	 * Start the source to enable the CSI-2 HS clock. We can now wait for
	 * CSI-2 PHY reset to complete.
	 */
	ret = v4l2_subdev_call(phy->source, video, s_stream, 1);
	if (ret) {
		v4l2_subdev_call(phy->source, core, s_power, 0);
		cal_camerarx_disable_irqs(phy);
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

	/* Finally, enable the PHY Protocol Interface (PPI). */
	cal_camerarx_ppi_enable(phy);

	phy->enable_count++;

	return 0;
}

static void cal_camerarx_stop(struct cal_camerarx *phy)
{
	int ret;

	if (--phy->enable_count > 0)
		return;

	cal_camerarx_ppi_disable(phy);

	cal_camerarx_disable_irqs(phy);

	cal_camerarx_power(phy, false);

	/* Assert Complex IO Reset */
	cal_write_field(phy->cal, CAL_CSI2_COMPLEXIO_CFG(phy->instance),
			CAL_CSI2_COMPLEXIO_CFG_RESET_CTRL,
			CAL_CSI2_COMPLEXIO_CFG_RESET_CTRL_MASK);

	phy_dbg(3, phy, "CAL_CSI2_COMPLEXIO_CFG(%d) = 0x%08x Complex IO in Reset\n",
		phy->instance,
		cal_read(phy->cal, CAL_CSI2_COMPLEXIO_CFG(phy->instance)));

	/* Disable the phy */
	cal_camerarx_disable(phy);

	if (v4l2_subdev_call(phy->source, video, s_stream, 0))
		phy_err(phy, "stream off failed in subdev\n");

	ret = v4l2_subdev_call(phy->source, core, s_power, 0);
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
void cal_camerarx_i913_errata(struct cal_camerarx *phy)
{
	u32 reg10 = camerarx_read(phy, CAL_CSI2_PHY_REG10);

	cal_set_field(&reg10, 1, CAL_CSI2_PHY_REG10_I933_LDO_DISABLE_MASK);

	phy_dbg(1, phy, "CSI2_%d_REG10 = 0x%08x\n", phy->instance, reg10);
	camerarx_write(phy, CAL_CSI2_PHY_REG10, reg10);
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
	char data_lanes[V4L2_MBUS_CSI2_MAX_DATA_LANES * 2];
	struct device_node *ep_node;
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
	phy->source_ep_node = of_graph_get_remote_endpoint(ep_node);
	phy->source_node = of_graph_get_port_parent(phy->source_ep_node);
	if (!phy->source_node) {
		phy_dbg(3, phy, "Can't get remote parent\n");
		of_node_put(phy->source_ep_node);
		ret = -EINVAL;
		goto done;
	}

	phy_dbg(1, phy, "Found connected device %pOFn\n", phy->source_node);

done:
	of_node_put(ep_node);
	return ret;
}

int cal_camerarx_get_remote_frame_desc(struct cal_camerarx *phy,
				       struct v4l2_mbus_frame_desc *desc)
{
	struct media_pad *pad;
	int ret;

	if (!phy->source)
		return -EPIPE;

	pad = media_pad_remote_pad_first(&phy->pads[CAL_CAMERARX_PAD_SINK]);
	if (!pad)
		return -EPIPE;

	ret = v4l2_subdev_call(phy->source, pad, get_frame_desc, pad->index,
			       desc);
	if (ret)
		return ret;

	if (desc->type != V4L2_MBUS_FRAME_DESC_TYPE_CSI2) {
		dev_err(phy->cal->dev,
			"Frame descriptor does not describe CSI-2 link");
		return -EINVAL;
	}

	return 0;
}

/* ------------------------------------------------------------------
 *	V4L2 Subdev Operations
 * ------------------------------------------------------------------
 */

static inline struct cal_camerarx *to_cal_camerarx(struct v4l2_subdev *sd)
{
	return container_of(sd, struct cal_camerarx, subdev);
}

static struct v4l2_mbus_framefmt *
cal_camerarx_get_pad_format(struct cal_camerarx *phy,
			    struct v4l2_subdev_state *state,
			    unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(&phy->subdev, state, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &phy->formats[pad];
	default:
		return NULL;
	}
}

static int cal_camerarx_sd_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct cal_camerarx *phy = to_cal_camerarx(sd);
	int ret = 0;

	mutex_lock(&phy->mutex);

	if (enable)
		ret = cal_camerarx_start(phy);
	else
		cal_camerarx_stop(phy);

	mutex_unlock(&phy->mutex);

	return ret;
}

static int cal_camerarx_sd_enum_mbus_code(struct v4l2_subdev *sd,
					  struct v4l2_subdev_state *state,
					  struct v4l2_subdev_mbus_code_enum *code)
{
	struct cal_camerarx *phy = to_cal_camerarx(sd);
	int ret = 0;

	mutex_lock(&phy->mutex);

	/* No transcoding, source and sink codes must match. */
	if (cal_rx_pad_is_source(code->pad)) {
		struct v4l2_mbus_framefmt *fmt;

		if (code->index > 0) {
			ret = -EINVAL;
			goto out;
		}

		fmt = cal_camerarx_get_pad_format(phy, state,
						  CAL_CAMERARX_PAD_SINK,
						  code->which);
		code->code = fmt->code;
	} else {
		if (code->index >= cal_num_formats) {
			ret = -EINVAL;
			goto out;
		}

		code->code = cal_formats[code->index].code;
	}

out:
	mutex_unlock(&phy->mutex);

	return ret;
}

static int cal_camerarx_sd_enum_frame_size(struct v4l2_subdev *sd,
					   struct v4l2_subdev_state *state,
					   struct v4l2_subdev_frame_size_enum *fse)
{
	struct cal_camerarx *phy = to_cal_camerarx(sd);
	const struct cal_format_info *fmtinfo;
	int ret = 0;

	if (fse->index > 0)
		return -EINVAL;

	mutex_lock(&phy->mutex);

	/* No transcoding, source and sink formats must match. */
	if (cal_rx_pad_is_source(fse->pad)) {
		struct v4l2_mbus_framefmt *fmt;

		fmt = cal_camerarx_get_pad_format(phy, state,
						  CAL_CAMERARX_PAD_SINK,
						  fse->which);
		if (fse->code != fmt->code) {
			ret = -EINVAL;
			goto out;
		}

		fse->min_width = fmt->width;
		fse->max_width = fmt->width;
		fse->min_height = fmt->height;
		fse->max_height = fmt->height;
	} else {
		fmtinfo = cal_format_by_code(fse->code);
		if (!fmtinfo) {
			ret = -EINVAL;
			goto out;
		}

		fse->min_width = CAL_MIN_WIDTH_BYTES * 8 / ALIGN(fmtinfo->bpp, 8);
		fse->max_width = CAL_MAX_WIDTH_BYTES * 8 / ALIGN(fmtinfo->bpp, 8);
		fse->min_height = CAL_MIN_HEIGHT_LINES;
		fse->max_height = CAL_MAX_HEIGHT_LINES;
	}

out:
	mutex_unlock(&phy->mutex);

	return ret;
}

static int cal_camerarx_sd_get_fmt(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_format *format)
{
	struct cal_camerarx *phy = to_cal_camerarx(sd);
	struct v4l2_mbus_framefmt *fmt;

	mutex_lock(&phy->mutex);

	fmt = cal_camerarx_get_pad_format(phy, state, format->pad,
					  format->which);
	format->format = *fmt;

	mutex_unlock(&phy->mutex);

	return 0;
}

static int cal_camerarx_sd_set_fmt(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_format *format)
{
	struct cal_camerarx *phy = to_cal_camerarx(sd);
	const struct cal_format_info *fmtinfo;
	struct v4l2_mbus_framefmt *fmt;
	unsigned int bpp;

	/* No transcoding, source and sink formats must match. */
	if (cal_rx_pad_is_source(format->pad))
		return cal_camerarx_sd_get_fmt(sd, state, format);

	/*
	 * Default to the first format if the requested media bus code isn't
	 * supported.
	 */
	fmtinfo = cal_format_by_code(format->format.code);
	if (!fmtinfo)
		fmtinfo = &cal_formats[0];

	/* Clamp the size, update the code. The colorspace is accepted as-is. */
	bpp = ALIGN(fmtinfo->bpp, 8);

	format->format.width = clamp_t(unsigned int, format->format.width,
				       CAL_MIN_WIDTH_BYTES * 8 / bpp,
				       CAL_MAX_WIDTH_BYTES * 8 / bpp);
	format->format.height = clamp_t(unsigned int, format->format.height,
					CAL_MIN_HEIGHT_LINES,
					CAL_MAX_HEIGHT_LINES);
	format->format.code = fmtinfo->code;
	format->format.field = V4L2_FIELD_NONE;

	/* Store the format and propagate it to the source pad. */

	mutex_lock(&phy->mutex);

	fmt = cal_camerarx_get_pad_format(phy, state,
					  CAL_CAMERARX_PAD_SINK,
					  format->which);
	*fmt = format->format;

	fmt = cal_camerarx_get_pad_format(phy, state,
					  CAL_CAMERARX_PAD_FIRST_SOURCE,
					  format->which);
	*fmt = format->format;

	mutex_unlock(&phy->mutex);

	return 0;
}

static int cal_camerarx_sd_init_cfg(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *state)
{
	struct v4l2_subdev_format format = {
		.which = state ? V4L2_SUBDEV_FORMAT_TRY
		: V4L2_SUBDEV_FORMAT_ACTIVE,
		.pad = CAL_CAMERARX_PAD_SINK,
		.format = {
			.width = 640,
			.height = 480,
			.code = MEDIA_BUS_FMT_UYVY8_2X8,
			.field = V4L2_FIELD_NONE,
			.colorspace = V4L2_COLORSPACE_SRGB,
			.ycbcr_enc = V4L2_YCBCR_ENC_601,
			.quantization = V4L2_QUANTIZATION_LIM_RANGE,
			.xfer_func = V4L2_XFER_FUNC_SRGB,
		},
	};

	return cal_camerarx_sd_set_fmt(sd, state, &format);
}

static const struct v4l2_subdev_video_ops cal_camerarx_video_ops = {
	.s_stream = cal_camerarx_sd_s_stream,
};

static const struct v4l2_subdev_pad_ops cal_camerarx_pad_ops = {
	.init_cfg = cal_camerarx_sd_init_cfg,
	.enum_mbus_code = cal_camerarx_sd_enum_mbus_code,
	.enum_frame_size = cal_camerarx_sd_enum_frame_size,
	.get_fmt = cal_camerarx_sd_get_fmt,
	.set_fmt = cal_camerarx_sd_set_fmt,
};

static const struct v4l2_subdev_ops cal_camerarx_subdev_ops = {
	.video = &cal_camerarx_video_ops,
	.pad = &cal_camerarx_pad_ops,
};

static struct media_entity_operations cal_camerarx_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/* ------------------------------------------------------------------
 *	Create and Destroy
 * ------------------------------------------------------------------
 */

struct cal_camerarx *cal_camerarx_create(struct cal_dev *cal,
					 unsigned int instance)
{
	struct platform_device *pdev = to_platform_device(cal->dev);
	struct cal_camerarx *phy;
	struct v4l2_subdev *sd;
	unsigned int i;
	int ret;

	phy = kzalloc(sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return ERR_PTR(-ENOMEM);

	phy->cal = cal;
	phy->instance = instance;

	spin_lock_init(&phy->vc_lock);
	mutex_init(&phy->mutex);

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

	/* Initialize the V4L2 subdev and media entity. */
	sd = &phy->subdev;
	v4l2_subdev_init(sd, &cal_camerarx_subdev_ops);
	sd->entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	sd->flags = V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name), "CAMERARX%u", instance);
	sd->dev = cal->dev;

	phy->pads[CAL_CAMERARX_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	for (i = CAL_CAMERARX_PAD_FIRST_SOURCE; i < CAL_CAMERARX_NUM_PADS; ++i)
		phy->pads[i].flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.ops = &cal_camerarx_media_ops;
	ret = media_entity_pads_init(&sd->entity, ARRAY_SIZE(phy->pads),
				     phy->pads);
	if (ret)
		goto error;

	ret = cal_camerarx_sd_init_cfg(sd, NULL);
	if (ret)
		goto error;

	ret = v4l2_device_register_subdev(&cal->v4l2_dev, sd);
	if (ret)
		goto error;

	return phy;

error:
	media_entity_cleanup(&phy->subdev.entity);
	kfree(phy);
	return ERR_PTR(ret);
}

void cal_camerarx_destroy(struct cal_camerarx *phy)
{
	if (!phy)
		return;

	v4l2_device_unregister_subdev(&phy->subdev);
	media_entity_cleanup(&phy->subdev.entity);
	of_node_put(phy->source_ep_node);
	of_node_put(phy->source_node);
	mutex_destroy(&phy->mutex);
	kfree(phy);
}

// SPDX-License-Identifier: GPL-2.0
/*
 * ZynqMP DisplayPort Driver
 *
 * Copyright (C) 2017 - 2020 Xilinx, Inc.
 *
 * Authors:
 * - Hyun Woo Kwon <hyun.kwon@xilinx.com>
 * - Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <drm/display/drm_dp_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_edid.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_modes.h>
#include <drm/drm_of.h>

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/phy/phy.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include "zynqmp_disp.h"
#include "zynqmp_dp.h"
#include "zynqmp_dpsub.h"
#include "zynqmp_kms.h"

static uint zynqmp_dp_aux_timeout_ms = 50;
module_param_named(aux_timeout_ms, zynqmp_dp_aux_timeout_ms, uint, 0444);
MODULE_PARM_DESC(aux_timeout_ms, "DP aux timeout value in msec (default: 50)");

/*
 * Some sink requires a delay after power on request
 */
static uint zynqmp_dp_power_on_delay_ms = 4;
module_param_named(power_on_delay_ms, zynqmp_dp_power_on_delay_ms, uint, 0444);
MODULE_PARM_DESC(power_on_delay_ms, "DP power on delay in msec (default: 4)");

/* Link configuration registers */
#define ZYNQMP_DP_LINK_BW_SET				0x0
#define ZYNQMP_DP_LANE_COUNT_SET			0x4
#define ZYNQMP_DP_ENHANCED_FRAME_EN			0x8
#define ZYNQMP_DP_TRAINING_PATTERN_SET			0xc
#define ZYNQMP_DP_SCRAMBLING_DISABLE			0x14
#define ZYNQMP_DP_DOWNSPREAD_CTL			0x18
#define ZYNQMP_DP_SOFTWARE_RESET			0x1c
#define ZYNQMP_DP_SOFTWARE_RESET_STREAM1		BIT(0)
#define ZYNQMP_DP_SOFTWARE_RESET_STREAM2		BIT(1)
#define ZYNQMP_DP_SOFTWARE_RESET_STREAM3		BIT(2)
#define ZYNQMP_DP_SOFTWARE_RESET_STREAM4		BIT(3)
#define ZYNQMP_DP_SOFTWARE_RESET_AUX			BIT(7)
#define ZYNQMP_DP_SOFTWARE_RESET_ALL			(ZYNQMP_DP_SOFTWARE_RESET_STREAM1 | \
							 ZYNQMP_DP_SOFTWARE_RESET_STREAM2 | \
							 ZYNQMP_DP_SOFTWARE_RESET_STREAM3 | \
							 ZYNQMP_DP_SOFTWARE_RESET_STREAM4 | \
							 ZYNQMP_DP_SOFTWARE_RESET_AUX)

/* Core enable registers */
#define ZYNQMP_DP_TRANSMITTER_ENABLE			0x80
#define ZYNQMP_DP_MAIN_STREAM_ENABLE			0x84
#define ZYNQMP_DP_FORCE_SCRAMBLER_RESET			0xc0
#define ZYNQMP_DP_VERSION				0xf8
#define ZYNQMP_DP_VERSION_MAJOR_MASK			GENMASK(31, 24)
#define ZYNQMP_DP_VERSION_MAJOR_SHIFT			24
#define ZYNQMP_DP_VERSION_MINOR_MASK			GENMASK(23, 16)
#define ZYNQMP_DP_VERSION_MINOR_SHIFT			16
#define ZYNQMP_DP_VERSION_REVISION_MASK			GENMASK(15, 12)
#define ZYNQMP_DP_VERSION_REVISION_SHIFT		12
#define ZYNQMP_DP_VERSION_PATCH_MASK			GENMASK(11, 8)
#define ZYNQMP_DP_VERSION_PATCH_SHIFT			8
#define ZYNQMP_DP_VERSION_INTERNAL_MASK			GENMASK(7, 0)
#define ZYNQMP_DP_VERSION_INTERNAL_SHIFT		0

/* Core ID registers */
#define ZYNQMP_DP_CORE_ID				0xfc
#define ZYNQMP_DP_CORE_ID_MAJOR_MASK			GENMASK(31, 24)
#define ZYNQMP_DP_CORE_ID_MAJOR_SHIFT			24
#define ZYNQMP_DP_CORE_ID_MINOR_MASK			GENMASK(23, 16)
#define ZYNQMP_DP_CORE_ID_MINOR_SHIFT			16
#define ZYNQMP_DP_CORE_ID_REVISION_MASK			GENMASK(15, 8)
#define ZYNQMP_DP_CORE_ID_REVISION_SHIFT		8
#define ZYNQMP_DP_CORE_ID_DIRECTION			GENMASK(1)

/* AUX channel interface registers */
#define ZYNQMP_DP_AUX_COMMAND				0x100
#define ZYNQMP_DP_AUX_COMMAND_CMD_SHIFT			8
#define ZYNQMP_DP_AUX_COMMAND_ADDRESS_ONLY		BIT(12)
#define ZYNQMP_DP_AUX_COMMAND_BYTES_SHIFT		0
#define ZYNQMP_DP_AUX_WRITE_FIFO			0x104
#define ZYNQMP_DP_AUX_ADDRESS				0x108
#define ZYNQMP_DP_AUX_CLK_DIVIDER			0x10c
#define ZYNQMP_DP_AUX_CLK_DIVIDER_AUX_FILTER_SHIFT	8
#define ZYNQMP_DP_INTERRUPT_SIGNAL_STATE		0x130
#define ZYNQMP_DP_INTERRUPT_SIGNAL_STATE_HPD		BIT(0)
#define ZYNQMP_DP_INTERRUPT_SIGNAL_STATE_REQUEST	BIT(1)
#define ZYNQMP_DP_INTERRUPT_SIGNAL_STATE_REPLY		BIT(2)
#define ZYNQMP_DP_INTERRUPT_SIGNAL_STATE_REPLY_TIMEOUT	BIT(3)
#define ZYNQMP_DP_AUX_REPLY_DATA			0x134
#define ZYNQMP_DP_AUX_REPLY_CODE			0x138
#define ZYNQMP_DP_AUX_REPLY_CODE_AUX_ACK		(0)
#define ZYNQMP_DP_AUX_REPLY_CODE_AUX_NACK		BIT(0)
#define ZYNQMP_DP_AUX_REPLY_CODE_AUX_DEFER		BIT(1)
#define ZYNQMP_DP_AUX_REPLY_CODE_I2C_ACK		(0)
#define ZYNQMP_DP_AUX_REPLY_CODE_I2C_NACK		BIT(2)
#define ZYNQMP_DP_AUX_REPLY_CODE_I2C_DEFER		BIT(3)
#define ZYNQMP_DP_AUX_REPLY_COUNT			0x13c
#define ZYNQMP_DP_REPLY_DATA_COUNT			0x148
#define ZYNQMP_DP_REPLY_DATA_COUNT_MASK			0xff
#define ZYNQMP_DP_INT_STATUS				0x3a0
#define ZYNQMP_DP_INT_MASK				0x3a4
#define ZYNQMP_DP_INT_EN				0x3a8
#define ZYNQMP_DP_INT_DS				0x3ac
#define ZYNQMP_DP_INT_HPD_IRQ				BIT(0)
#define ZYNQMP_DP_INT_HPD_EVENT				BIT(1)
#define ZYNQMP_DP_INT_REPLY_RECEIVED			BIT(2)
#define ZYNQMP_DP_INT_REPLY_TIMEOUT			BIT(3)
#define ZYNQMP_DP_INT_HPD_PULSE_DET			BIT(4)
#define ZYNQMP_DP_INT_EXT_PKT_TXD			BIT(5)
#define ZYNQMP_DP_INT_LIV_ABUF_UNDRFLW			BIT(12)
#define ZYNQMP_DP_INT_VBLANK_START			BIT(13)
#define ZYNQMP_DP_INT_PIXEL1_MATCH			BIT(14)
#define ZYNQMP_DP_INT_PIXEL0_MATCH			BIT(15)
#define ZYNQMP_DP_INT_CHBUF_UNDERFLW_MASK		0x3f0000
#define ZYNQMP_DP_INT_CHBUF_OVERFLW_MASK		0xfc00000
#define ZYNQMP_DP_INT_CUST_TS_2				BIT(28)
#define ZYNQMP_DP_INT_CUST_TS				BIT(29)
#define ZYNQMP_DP_INT_EXT_VSYNC_TS			BIT(30)
#define ZYNQMP_DP_INT_VSYNC_TS				BIT(31)
#define ZYNQMP_DP_INT_ALL				(ZYNQMP_DP_INT_HPD_IRQ | \
							 ZYNQMP_DP_INT_HPD_EVENT | \
							 ZYNQMP_DP_INT_CHBUF_UNDERFLW_MASK | \
							 ZYNQMP_DP_INT_CHBUF_OVERFLW_MASK)

/* Main stream attribute registers */
#define ZYNQMP_DP_MAIN_STREAM_HTOTAL			0x180
#define ZYNQMP_DP_MAIN_STREAM_VTOTAL			0x184
#define ZYNQMP_DP_MAIN_STREAM_POLARITY			0x188
#define ZYNQMP_DP_MAIN_STREAM_POLARITY_HSYNC_SHIFT	0
#define ZYNQMP_DP_MAIN_STREAM_POLARITY_VSYNC_SHIFT	1
#define ZYNQMP_DP_MAIN_STREAM_HSWIDTH			0x18c
#define ZYNQMP_DP_MAIN_STREAM_VSWIDTH			0x190
#define ZYNQMP_DP_MAIN_STREAM_HRES			0x194
#define ZYNQMP_DP_MAIN_STREAM_VRES			0x198
#define ZYNQMP_DP_MAIN_STREAM_HSTART			0x19c
#define ZYNQMP_DP_MAIN_STREAM_VSTART			0x1a0
#define ZYNQMP_DP_MAIN_STREAM_MISC0			0x1a4
#define ZYNQMP_DP_MAIN_STREAM_MISC0_SYNC_LOCK		BIT(0)
#define ZYNQMP_DP_MAIN_STREAM_MISC0_COMP_FORMAT_RGB	(0 << 1)
#define ZYNQMP_DP_MAIN_STREAM_MISC0_COMP_FORMAT_YCRCB_422	(5 << 1)
#define ZYNQMP_DP_MAIN_STREAM_MISC0_COMP_FORMAT_YCRCB_444	(6 << 1)
#define ZYNQMP_DP_MAIN_STREAM_MISC0_COMP_FORMAT_MASK	(7 << 1)
#define ZYNQMP_DP_MAIN_STREAM_MISC0_DYNAMIC_RANGE	BIT(3)
#define ZYNQMP_DP_MAIN_STREAM_MISC0_YCBCR_COLR		BIT(4)
#define ZYNQMP_DP_MAIN_STREAM_MISC0_BPC_6		(0 << 5)
#define ZYNQMP_DP_MAIN_STREAM_MISC0_BPC_8		(1 << 5)
#define ZYNQMP_DP_MAIN_STREAM_MISC0_BPC_10		(2 << 5)
#define ZYNQMP_DP_MAIN_STREAM_MISC0_BPC_12		(3 << 5)
#define ZYNQMP_DP_MAIN_STREAM_MISC0_BPC_16		(4 << 5)
#define ZYNQMP_DP_MAIN_STREAM_MISC0_BPC_MASK		(7 << 5)
#define ZYNQMP_DP_MAIN_STREAM_MISC1			0x1a8
#define ZYNQMP_DP_MAIN_STREAM_MISC1_Y_ONLY_EN		BIT(7)
#define ZYNQMP_DP_MAIN_STREAM_M_VID			0x1ac
#define ZYNQMP_DP_MSA_TRANSFER_UNIT_SIZE		0x1b0
#define ZYNQMP_DP_MSA_TRANSFER_UNIT_SIZE_TU_SIZE_DEF	64
#define ZYNQMP_DP_MAIN_STREAM_N_VID			0x1b4
#define ZYNQMP_DP_USER_PIX_WIDTH			0x1b8
#define ZYNQMP_DP_USER_DATA_COUNT_PER_LANE		0x1bc
#define ZYNQMP_DP_MIN_BYTES_PER_TU			0x1c4
#define ZYNQMP_DP_FRAC_BYTES_PER_TU			0x1c8
#define ZYNQMP_DP_INIT_WAIT				0x1cc

/* PHY configuration and status registers */
#define ZYNQMP_DP_PHY_RESET				0x200
#define ZYNQMP_DP_PHY_RESET_PHY_RESET			BIT(0)
#define ZYNQMP_DP_PHY_RESET_GTTX_RESET			BIT(1)
#define ZYNQMP_DP_PHY_RESET_PHY_PMA_RESET		BIT(8)
#define ZYNQMP_DP_PHY_RESET_PHY_PCS_RESET		BIT(9)
#define ZYNQMP_DP_PHY_RESET_ALL_RESET			(ZYNQMP_DP_PHY_RESET_PHY_RESET | \
							 ZYNQMP_DP_PHY_RESET_GTTX_RESET | \
							 ZYNQMP_DP_PHY_RESET_PHY_PMA_RESET | \
							 ZYNQMP_DP_PHY_RESET_PHY_PCS_RESET)
#define ZYNQMP_DP_PHY_PREEMPHASIS_LANE_0		0x210
#define ZYNQMP_DP_PHY_PREEMPHASIS_LANE_1		0x214
#define ZYNQMP_DP_PHY_PREEMPHASIS_LANE_2		0x218
#define ZYNQMP_DP_PHY_PREEMPHASIS_LANE_3		0x21c
#define ZYNQMP_DP_PHY_VOLTAGE_DIFF_LANE_0		0x220
#define ZYNQMP_DP_PHY_VOLTAGE_DIFF_LANE_1		0x224
#define ZYNQMP_DP_PHY_VOLTAGE_DIFF_LANE_2		0x228
#define ZYNQMP_DP_PHY_VOLTAGE_DIFF_LANE_3		0x22c
#define ZYNQMP_DP_PHY_CLOCK_SELECT			0x234
#define ZYNQMP_DP_PHY_CLOCK_SELECT_1_62G		0x1
#define ZYNQMP_DP_PHY_CLOCK_SELECT_2_70G		0x3
#define ZYNQMP_DP_PHY_CLOCK_SELECT_5_40G		0x5
#define ZYNQMP_DP_TX_PHY_POWER_DOWN			0x238
#define ZYNQMP_DP_TX_PHY_POWER_DOWN_LANE_0		BIT(0)
#define ZYNQMP_DP_TX_PHY_POWER_DOWN_LANE_1		BIT(1)
#define ZYNQMP_DP_TX_PHY_POWER_DOWN_LANE_2		BIT(2)
#define ZYNQMP_DP_TX_PHY_POWER_DOWN_LANE_3		BIT(3)
#define ZYNQMP_DP_TX_PHY_POWER_DOWN_ALL			0xf
#define ZYNQMP_DP_PHY_PRECURSOR_LANE_0			0x23c
#define ZYNQMP_DP_PHY_PRECURSOR_LANE_1			0x240
#define ZYNQMP_DP_PHY_PRECURSOR_LANE_2			0x244
#define ZYNQMP_DP_PHY_PRECURSOR_LANE_3			0x248
#define ZYNQMP_DP_PHY_POSTCURSOR_LANE_0			0x24c
#define ZYNQMP_DP_PHY_POSTCURSOR_LANE_1			0x250
#define ZYNQMP_DP_PHY_POSTCURSOR_LANE_2			0x254
#define ZYNQMP_DP_PHY_POSTCURSOR_LANE_3			0x258
#define ZYNQMP_DP_SUB_TX_PHY_PRECURSOR_LANE_0		0x24c
#define ZYNQMP_DP_SUB_TX_PHY_PRECURSOR_LANE_1		0x250
#define ZYNQMP_DP_PHY_STATUS				0x280
#define ZYNQMP_DP_PHY_STATUS_PLL_LOCKED_SHIFT		4
#define ZYNQMP_DP_PHY_STATUS_FPGA_PLL_LOCKED		BIT(6)

/* Audio registers */
#define ZYNQMP_DP_TX_AUDIO_CONTROL			0x300
#define ZYNQMP_DP_TX_AUDIO_CHANNELS			0x304
#define ZYNQMP_DP_TX_AUDIO_INFO_DATA			0x308
#define ZYNQMP_DP_TX_M_AUD				0x328
#define ZYNQMP_DP_TX_N_AUD				0x32c
#define ZYNQMP_DP_TX_AUDIO_EXT_DATA			0x330

#define ZYNQMP_DP_MAX_LANES				2
#define ZYNQMP_MAX_FREQ					3000000

#define DP_REDUCED_BIT_RATE				162000
#define DP_HIGH_BIT_RATE				270000
#define DP_HIGH_BIT_RATE2				540000
#define DP_MAX_TRAINING_TRIES				5
#define DP_V1_2						0x12

/**
 * struct zynqmp_dp_link_config - Common link config between source and sink
 * @max_rate: maximum link rate
 * @max_lanes: maximum number of lanes
 */
struct zynqmp_dp_link_config {
	int max_rate;
	u8 max_lanes;
};

/**
 * struct zynqmp_dp_mode - Configured mode of DisplayPort
 * @bw_code: code for bandwidth(link rate)
 * @lane_cnt: number of lanes
 * @pclock: pixel clock frequency of current mode
 * @fmt: format identifier string
 */
struct zynqmp_dp_mode {
	u8 bw_code;
	u8 lane_cnt;
	int pclock;
	const char *fmt;
};

/**
 * struct zynqmp_dp_config - Configuration of DisplayPort from DTS
 * @misc0: misc0 configuration (per DP v1.2 spec)
 * @misc1: misc1 configuration (per DP v1.2 spec)
 * @bpp: bits per pixel
 */
struct zynqmp_dp_config {
	u8 misc0;
	u8 misc1;
	u8 bpp;
};

/**
 * struct zynqmp_dp - Xilinx DisplayPort core
 * @dev: device structure
 * @dpsub: Display subsystem
 * @iomem: device I/O memory for register access
 * @reset: reset controller
 * @irq: irq
 * @bridge: DRM bridge for the DP encoder
 * @next_bridge: The downstream bridge
 * @config: IP core configuration from DTS
 * @aux: aux channel
 * @phy: PHY handles for DP lanes
 * @num_lanes: number of enabled phy lanes
 * @hpd_work: hot plug detection worker
 * @status: connection status
 * @enabled: flag to indicate if the device is enabled
 * @dpcd: DP configuration data from currently connected sink device
 * @link_config: common link configuration between IP core and sink device
 * @mode: current mode between IP core and sink device
 * @train_set: set of training data
 */
struct zynqmp_dp {
	struct device *dev;
	struct zynqmp_dpsub *dpsub;
	void __iomem *iomem;
	struct reset_control *reset;
	int irq;

	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;

	struct zynqmp_dp_config config;
	struct drm_dp_aux aux;
	struct phy *phy[ZYNQMP_DP_MAX_LANES];
	u8 num_lanes;
	struct delayed_work hpd_work;
	enum drm_connector_status status;
	bool enabled;

	u8 dpcd[DP_RECEIVER_CAP_SIZE];
	struct zynqmp_dp_link_config link_config;
	struct zynqmp_dp_mode mode;
	u8 train_set[ZYNQMP_DP_MAX_LANES];
};

static inline struct zynqmp_dp *bridge_to_dp(struct drm_bridge *bridge)
{
	return container_of(bridge, struct zynqmp_dp, bridge);
}

static void zynqmp_dp_write(struct zynqmp_dp *dp, int offset, u32 val)
{
	writel(val, dp->iomem + offset);
}

static u32 zynqmp_dp_read(struct zynqmp_dp *dp, int offset)
{
	return readl(dp->iomem + offset);
}

static void zynqmp_dp_clr(struct zynqmp_dp *dp, int offset, u32 clr)
{
	zynqmp_dp_write(dp, offset, zynqmp_dp_read(dp, offset) & ~clr);
}

static void zynqmp_dp_set(struct zynqmp_dp *dp, int offset, u32 set)
{
	zynqmp_dp_write(dp, offset, zynqmp_dp_read(dp, offset) | set);
}

/* -----------------------------------------------------------------------------
 * PHY Handling
 */

#define RST_TIMEOUT_MS			1000

static int zynqmp_dp_reset(struct zynqmp_dp *dp, bool assert)
{
	unsigned long timeout;

	if (assert)
		reset_control_assert(dp->reset);
	else
		reset_control_deassert(dp->reset);

	/* Wait for the (de)assert to complete. */
	timeout = jiffies + msecs_to_jiffies(RST_TIMEOUT_MS);
	while (!time_after_eq(jiffies, timeout)) {
		bool status = !!reset_control_status(dp->reset);

		if (assert == status)
			return 0;

		cpu_relax();
	}

	dev_err(dp->dev, "reset %s timeout\n", assert ? "assert" : "deassert");
	return -ETIMEDOUT;
}

/**
 * zynqmp_dp_phy_init - Initialize the phy
 * @dp: DisplayPort IP core structure
 *
 * Initialize the phy.
 *
 * Return: 0 if the phy instances are initialized correctly, or the error code
 * returned from the callee functions.
 */
static int zynqmp_dp_phy_init(struct zynqmp_dp *dp)
{
	int ret;
	int i;

	for (i = 0; i < dp->num_lanes; i++) {
		ret = phy_init(dp->phy[i]);
		if (ret) {
			dev_err(dp->dev, "failed to init phy lane %d\n", i);
			return ret;
		}
	}

	zynqmp_dp_clr(dp, ZYNQMP_DP_PHY_RESET, ZYNQMP_DP_PHY_RESET_ALL_RESET);

	/*
	 * Power on lanes in reverse order as only lane 0 waits for the PLL to
	 * lock.
	 */
	for (i = dp->num_lanes - 1; i >= 0; i--) {
		ret = phy_power_on(dp->phy[i]);
		if (ret) {
			dev_err(dp->dev, "failed to power on phy lane %d\n", i);
			return ret;
		}
	}

	return 0;
}

/**
 * zynqmp_dp_phy_exit - Exit the phy
 * @dp: DisplayPort IP core structure
 *
 * Exit the phy.
 */
static void zynqmp_dp_phy_exit(struct zynqmp_dp *dp)
{
	unsigned int i;
	int ret;

	for (i = 0; i < dp->num_lanes; i++) {
		ret = phy_power_off(dp->phy[i]);
		if (ret)
			dev_err(dp->dev, "failed to power off phy(%d) %d\n", i,
				ret);
	}

	for (i = 0; i < dp->num_lanes; i++) {
		ret = phy_exit(dp->phy[i]);
		if (ret)
			dev_err(dp->dev, "failed to exit phy(%d) %d\n", i, ret);
	}
}

/**
 * zynqmp_dp_phy_probe - Probe the PHYs
 * @dp: DisplayPort IP core structure
 *
 * Probe PHYs for all lanes. Less PHYs may be available than the number of
 * lanes, which is not considered an error as long as at least one PHY is
 * found. The caller can check dp->num_lanes to check how many PHYs were found.
 *
 * Return:
 * * 0				- Success
 * * -ENXIO			- No PHY found
 * * -EPROBE_DEFER		- Probe deferral requested
 * * Other negative value	- PHY retrieval failure
 */
static int zynqmp_dp_phy_probe(struct zynqmp_dp *dp)
{
	unsigned int i;

	for (i = 0; i < ZYNQMP_DP_MAX_LANES; i++) {
		char phy_name[16];
		struct phy *phy;

		snprintf(phy_name, sizeof(phy_name), "dp-phy%d", i);
		phy = devm_phy_get(dp->dev, phy_name);

		if (IS_ERR(phy)) {
			switch (PTR_ERR(phy)) {
			case -ENODEV:
				if (dp->num_lanes)
					return 0;

				dev_err(dp->dev, "no PHY found\n");
				return -ENXIO;

			case -EPROBE_DEFER:
				return -EPROBE_DEFER;

			default:
				dev_err(dp->dev, "failed to get PHY lane %u\n",
					i);
				return PTR_ERR(phy);
			}
		}

		dp->phy[i] = phy;
		dp->num_lanes++;
	}

	return 0;
}

/**
 * zynqmp_dp_phy_ready - Check if PHY is ready
 * @dp: DisplayPort IP core structure
 *
 * Check if PHY is ready. If PHY is not ready, wait 1ms to check for 100 times.
 * This amount of delay was suggested by IP designer.
 *
 * Return: 0 if PHY is ready, or -ENODEV if PHY is not ready.
 */
static int zynqmp_dp_phy_ready(struct zynqmp_dp *dp)
{
	u32 i, reg, ready;

	ready = (1 << dp->num_lanes) - 1;

	/* Wait for 100 * 1ms. This should be enough time for PHY to be ready */
	for (i = 0; ; i++) {
		reg = zynqmp_dp_read(dp, ZYNQMP_DP_PHY_STATUS);
		if ((reg & ready) == ready)
			return 0;

		if (i == 100) {
			dev_err(dp->dev, "PHY isn't ready\n");
			return -ENODEV;
		}

		usleep_range(1000, 1100);
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * DisplayPort Link Training
 */

/**
 * zynqmp_dp_max_rate - Calculate and return available max pixel clock
 * @link_rate: link rate (Kilo-bytes / sec)
 * @lane_num: number of lanes
 * @bpp: bits per pixel
 *
 * Return: max pixel clock (KHz) supported by current link config.
 */
static inline int zynqmp_dp_max_rate(int link_rate, u8 lane_num, u8 bpp)
{
	return link_rate * lane_num * 8 / bpp;
}

/**
 * zynqmp_dp_mode_configure - Configure the link values
 * @dp: DisplayPort IP core structure
 * @pclock: pixel clock for requested display mode
 * @current_bw: current link rate
 *
 * Find the link configuration values, rate and lane count for requested pixel
 * clock @pclock. The @pclock is stored in the mode to be used in other
 * functions later. The returned rate is downshifted from the current rate
 * @current_bw.
 *
 * Return: Current link rate code, or -EINVAL.
 */
static int zynqmp_dp_mode_configure(struct zynqmp_dp *dp, int pclock,
				    u8 current_bw)
{
	int max_rate = dp->link_config.max_rate;
	u8 bw_code;
	u8 max_lanes = dp->link_config.max_lanes;
	u8 max_link_rate_code = drm_dp_link_rate_to_bw_code(max_rate);
	u8 bpp = dp->config.bpp;
	u8 lane_cnt;

	/* Downshift from current bandwidth */
	switch (current_bw) {
	case DP_LINK_BW_5_4:
		bw_code = DP_LINK_BW_2_7;
		break;
	case DP_LINK_BW_2_7:
		bw_code = DP_LINK_BW_1_62;
		break;
	case DP_LINK_BW_1_62:
		dev_err(dp->dev, "can't downshift. already lowest link rate\n");
		return -EINVAL;
	default:
		/* If not given, start with max supported */
		bw_code = max_link_rate_code;
		break;
	}

	for (lane_cnt = 1; lane_cnt <= max_lanes; lane_cnt <<= 1) {
		int bw;
		u32 rate;

		bw = drm_dp_bw_code_to_link_rate(bw_code);
		rate = zynqmp_dp_max_rate(bw, lane_cnt, bpp);
		if (pclock <= rate) {
			dp->mode.bw_code = bw_code;
			dp->mode.lane_cnt = lane_cnt;
			dp->mode.pclock = pclock;
			return dp->mode.bw_code;
		}
	}

	dev_err(dp->dev, "failed to configure link values\n");

	return -EINVAL;
}

/**
 * zynqmp_dp_adjust_train - Adjust train values
 * @dp: DisplayPort IP core structure
 * @link_status: link status from sink which contains requested training values
 */
static void zynqmp_dp_adjust_train(struct zynqmp_dp *dp,
				   u8 link_status[DP_LINK_STATUS_SIZE])
{
	u8 *train_set = dp->train_set;
	u8 voltage = 0, preemphasis = 0;
	u8 i;

	for (i = 0; i < dp->mode.lane_cnt; i++) {
		u8 v = drm_dp_get_adjust_request_voltage(link_status, i);
		u8 p = drm_dp_get_adjust_request_pre_emphasis(link_status, i);

		if (v > voltage)
			voltage = v;

		if (p > preemphasis)
			preemphasis = p;
	}

	if (voltage >= DP_TRAIN_VOLTAGE_SWING_LEVEL_3)
		voltage |= DP_TRAIN_MAX_SWING_REACHED;

	if (preemphasis >= DP_TRAIN_PRE_EMPH_LEVEL_2)
		preemphasis |= DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;

	for (i = 0; i < dp->mode.lane_cnt; i++)
		train_set[i] = voltage | preemphasis;
}

/**
 * zynqmp_dp_update_vs_emph - Update the training values
 * @dp: DisplayPort IP core structure
 *
 * Update the training values based on the request from sink. The mapped values
 * are predefined, and values(vs, pe, pc) are from the device manual.
 *
 * Return: 0 if vs and emph are updated successfully, or the error code returned
 * by drm_dp_dpcd_write().
 */
static int zynqmp_dp_update_vs_emph(struct zynqmp_dp *dp)
{
	unsigned int i;
	int ret;

	ret = drm_dp_dpcd_write(&dp->aux, DP_TRAINING_LANE0_SET, dp->train_set,
				dp->mode.lane_cnt);
	if (ret < 0)
		return ret;

	for (i = 0; i < dp->mode.lane_cnt; i++) {
		u32 reg = ZYNQMP_DP_SUB_TX_PHY_PRECURSOR_LANE_0 + i * 4;
		union phy_configure_opts opts = { 0 };
		u8 train = dp->train_set[i];

		opts.dp.voltage[0] = (train & DP_TRAIN_VOLTAGE_SWING_MASK)
				   >> DP_TRAIN_VOLTAGE_SWING_SHIFT;
		opts.dp.pre[0] = (train & DP_TRAIN_PRE_EMPHASIS_MASK)
			       >> DP_TRAIN_PRE_EMPHASIS_SHIFT;

		phy_configure(dp->phy[i], &opts);

		zynqmp_dp_write(dp, reg, 0x2);
	}

	return 0;
}

/**
 * zynqmp_dp_link_train_cr - Train clock recovery
 * @dp: DisplayPort IP core structure
 *
 * Return: 0 if clock recovery train is done successfully, or corresponding
 * error code.
 */
static int zynqmp_dp_link_train_cr(struct zynqmp_dp *dp)
{
	u8 link_status[DP_LINK_STATUS_SIZE];
	u8 lane_cnt = dp->mode.lane_cnt;
	u8 vs = 0, tries = 0;
	u16 max_tries, i;
	bool cr_done;
	int ret;

	zynqmp_dp_write(dp, ZYNQMP_DP_TRAINING_PATTERN_SET,
			DP_TRAINING_PATTERN_1);
	ret = drm_dp_dpcd_writeb(&dp->aux, DP_TRAINING_PATTERN_SET,
				 DP_TRAINING_PATTERN_1 |
				 DP_LINK_SCRAMBLING_DISABLE);
	if (ret < 0)
		return ret;

	/*
	 * 256 loops should be maximum iterations for 4 lanes and 4 values.
	 * So, This loop should exit before 512 iterations
	 */
	for (max_tries = 0; max_tries < 512; max_tries++) {
		ret = zynqmp_dp_update_vs_emph(dp);
		if (ret)
			return ret;

		drm_dp_link_train_clock_recovery_delay(&dp->aux, dp->dpcd);
		ret = drm_dp_dpcd_read_link_status(&dp->aux, link_status);
		if (ret < 0)
			return ret;

		cr_done = drm_dp_clock_recovery_ok(link_status, lane_cnt);
		if (cr_done)
			break;

		for (i = 0; i < lane_cnt; i++)
			if (!(dp->train_set[i] & DP_TRAIN_MAX_SWING_REACHED))
				break;
		if (i == lane_cnt)
			break;

		if ((dp->train_set[0] & DP_TRAIN_VOLTAGE_SWING_MASK) == vs)
			tries++;
		else
			tries = 0;

		if (tries == DP_MAX_TRAINING_TRIES)
			break;

		vs = dp->train_set[0] & DP_TRAIN_VOLTAGE_SWING_MASK;
		zynqmp_dp_adjust_train(dp, link_status);
	}

	if (!cr_done)
		return -EIO;

	return 0;
}

/**
 * zynqmp_dp_link_train_ce - Train channel equalization
 * @dp: DisplayPort IP core structure
 *
 * Return: 0 if channel equalization train is done successfully, or
 * corresponding error code.
 */
static int zynqmp_dp_link_train_ce(struct zynqmp_dp *dp)
{
	u8 link_status[DP_LINK_STATUS_SIZE];
	u8 lane_cnt = dp->mode.lane_cnt;
	u32 pat, tries;
	int ret;
	bool ce_done;

	if (dp->dpcd[DP_DPCD_REV] >= DP_V1_2 &&
	    dp->dpcd[DP_MAX_LANE_COUNT] & DP_TPS3_SUPPORTED)
		pat = DP_TRAINING_PATTERN_3;
	else
		pat = DP_TRAINING_PATTERN_2;

	zynqmp_dp_write(dp, ZYNQMP_DP_TRAINING_PATTERN_SET, pat);
	ret = drm_dp_dpcd_writeb(&dp->aux, DP_TRAINING_PATTERN_SET,
				 pat | DP_LINK_SCRAMBLING_DISABLE);
	if (ret < 0)
		return ret;

	for (tries = 0; tries < DP_MAX_TRAINING_TRIES; tries++) {
		ret = zynqmp_dp_update_vs_emph(dp);
		if (ret)
			return ret;

		drm_dp_link_train_channel_eq_delay(&dp->aux, dp->dpcd);
		ret = drm_dp_dpcd_read_link_status(&dp->aux, link_status);
		if (ret < 0)
			return ret;

		ce_done = drm_dp_channel_eq_ok(link_status, lane_cnt);
		if (ce_done)
			break;

		zynqmp_dp_adjust_train(dp, link_status);
	}

	if (!ce_done)
		return -EIO;

	return 0;
}

/**
 * zynqmp_dp_train - Train the link
 * @dp: DisplayPort IP core structure
 *
 * Return: 0 if all trains are done successfully, or corresponding error code.
 */
static int zynqmp_dp_train(struct zynqmp_dp *dp)
{
	u32 reg;
	u8 bw_code = dp->mode.bw_code;
	u8 lane_cnt = dp->mode.lane_cnt;
	u8 aux_lane_cnt = lane_cnt;
	bool enhanced;
	int ret;

	zynqmp_dp_write(dp, ZYNQMP_DP_LANE_COUNT_SET, lane_cnt);
	enhanced = drm_dp_enhanced_frame_cap(dp->dpcd);
	if (enhanced) {
		zynqmp_dp_write(dp, ZYNQMP_DP_ENHANCED_FRAME_EN, 1);
		aux_lane_cnt |= DP_LANE_COUNT_ENHANCED_FRAME_EN;
	}

	if (dp->dpcd[3] & 0x1) {
		zynqmp_dp_write(dp, ZYNQMP_DP_DOWNSPREAD_CTL, 1);
		drm_dp_dpcd_writeb(&dp->aux, DP_DOWNSPREAD_CTRL,
				   DP_SPREAD_AMP_0_5);
	} else {
		zynqmp_dp_write(dp, ZYNQMP_DP_DOWNSPREAD_CTL, 0);
		drm_dp_dpcd_writeb(&dp->aux, DP_DOWNSPREAD_CTRL, 0);
	}

	ret = drm_dp_dpcd_writeb(&dp->aux, DP_LANE_COUNT_SET, aux_lane_cnt);
	if (ret < 0) {
		dev_err(dp->dev, "failed to set lane count\n");
		return ret;
	}

	ret = drm_dp_dpcd_writeb(&dp->aux, DP_MAIN_LINK_CHANNEL_CODING_SET,
				 DP_SET_ANSI_8B10B);
	if (ret < 0) {
		dev_err(dp->dev, "failed to set ANSI 8B/10B encoding\n");
		return ret;
	}

	ret = drm_dp_dpcd_writeb(&dp->aux, DP_LINK_BW_SET, bw_code);
	if (ret < 0) {
		dev_err(dp->dev, "failed to set DP bandwidth\n");
		return ret;
	}

	zynqmp_dp_write(dp, ZYNQMP_DP_LINK_BW_SET, bw_code);
	switch (bw_code) {
	case DP_LINK_BW_1_62:
		reg = ZYNQMP_DP_PHY_CLOCK_SELECT_1_62G;
		break;
	case DP_LINK_BW_2_7:
		reg = ZYNQMP_DP_PHY_CLOCK_SELECT_2_70G;
		break;
	case DP_LINK_BW_5_4:
	default:
		reg = ZYNQMP_DP_PHY_CLOCK_SELECT_5_40G;
		break;
	}

	zynqmp_dp_write(dp, ZYNQMP_DP_PHY_CLOCK_SELECT, reg);
	ret = zynqmp_dp_phy_ready(dp);
	if (ret < 0)
		return ret;

	zynqmp_dp_write(dp, ZYNQMP_DP_SCRAMBLING_DISABLE, 1);
	memset(dp->train_set, 0, sizeof(dp->train_set));
	ret = zynqmp_dp_link_train_cr(dp);
	if (ret)
		return ret;

	ret = zynqmp_dp_link_train_ce(dp);
	if (ret)
		return ret;

	ret = drm_dp_dpcd_writeb(&dp->aux, DP_TRAINING_PATTERN_SET,
				 DP_TRAINING_PATTERN_DISABLE);
	if (ret < 0) {
		dev_err(dp->dev, "failed to disable training pattern\n");
		return ret;
	}
	zynqmp_dp_write(dp, ZYNQMP_DP_TRAINING_PATTERN_SET,
			DP_TRAINING_PATTERN_DISABLE);

	zynqmp_dp_write(dp, ZYNQMP_DP_SCRAMBLING_DISABLE, 0);

	return 0;
}

/**
 * zynqmp_dp_train_loop - Downshift the link rate during training
 * @dp: DisplayPort IP core structure
 *
 * Train the link by downshifting the link rate if training is not successful.
 */
static void zynqmp_dp_train_loop(struct zynqmp_dp *dp)
{
	struct zynqmp_dp_mode *mode = &dp->mode;
	u8 bw = mode->bw_code;
	int ret;

	do {
		if (dp->status == connector_status_disconnected ||
		    !dp->enabled)
			return;

		ret = zynqmp_dp_train(dp);
		if (!ret)
			return;

		ret = zynqmp_dp_mode_configure(dp, mode->pclock, bw);
		if (ret < 0)
			goto err_out;

		bw = ret;
	} while (bw >= DP_LINK_BW_1_62);

err_out:
	dev_err(dp->dev, "failed to train the DP link\n");
}

/* -----------------------------------------------------------------------------
 * DisplayPort AUX
 */

#define AUX_READ_BIT	0x1

/**
 * zynqmp_dp_aux_cmd_submit - Submit aux command
 * @dp: DisplayPort IP core structure
 * @cmd: aux command
 * @addr: aux address
 * @buf: buffer for command data
 * @bytes: number of bytes for @buf
 * @reply: reply code to be returned
 *
 * Submit an aux command. All aux related commands, native or i2c aux
 * read/write, are submitted through this function. The function is mapped to
 * the transfer function of struct drm_dp_aux. This function involves in
 * multiple register reads/writes, thus synchronization is needed, and it is
 * done by drm_dp_helper using @hw_mutex. The calling thread goes into sleep
 * if there's no immediate reply to the command submission. The reply code is
 * returned at @reply if @reply != NULL.
 *
 * Return: 0 if the command is submitted properly, or corresponding error code:
 * -EBUSY when there is any request already being processed
 * -ETIMEDOUT when receiving reply is timed out
 * -EIO when received bytes are less than requested
 */
static int zynqmp_dp_aux_cmd_submit(struct zynqmp_dp *dp, u32 cmd, u16 addr,
				    u8 *buf, u8 bytes, u8 *reply)
{
	bool is_read = (cmd & AUX_READ_BIT) ? true : false;
	u32 reg, i;

	reg = zynqmp_dp_read(dp, ZYNQMP_DP_INTERRUPT_SIGNAL_STATE);
	if (reg & ZYNQMP_DP_INTERRUPT_SIGNAL_STATE_REQUEST)
		return -EBUSY;

	zynqmp_dp_write(dp, ZYNQMP_DP_AUX_ADDRESS, addr);
	if (!is_read)
		for (i = 0; i < bytes; i++)
			zynqmp_dp_write(dp, ZYNQMP_DP_AUX_WRITE_FIFO,
					buf[i]);

	reg = cmd << ZYNQMP_DP_AUX_COMMAND_CMD_SHIFT;
	if (!buf || !bytes)
		reg |= ZYNQMP_DP_AUX_COMMAND_ADDRESS_ONLY;
	else
		reg |= (bytes - 1) << ZYNQMP_DP_AUX_COMMAND_BYTES_SHIFT;
	zynqmp_dp_write(dp, ZYNQMP_DP_AUX_COMMAND, reg);

	/* Wait for reply to be delivered upto 2ms */
	for (i = 0; ; i++) {
		reg = zynqmp_dp_read(dp, ZYNQMP_DP_INTERRUPT_SIGNAL_STATE);
		if (reg & ZYNQMP_DP_INTERRUPT_SIGNAL_STATE_REPLY)
			break;

		if (reg & ZYNQMP_DP_INTERRUPT_SIGNAL_STATE_REPLY_TIMEOUT ||
		    i == 2)
			return -ETIMEDOUT;

		usleep_range(1000, 1100);
	}

	reg = zynqmp_dp_read(dp, ZYNQMP_DP_AUX_REPLY_CODE);
	if (reply)
		*reply = reg;

	if (is_read &&
	    (reg == ZYNQMP_DP_AUX_REPLY_CODE_AUX_ACK ||
	     reg == ZYNQMP_DP_AUX_REPLY_CODE_I2C_ACK)) {
		reg = zynqmp_dp_read(dp, ZYNQMP_DP_REPLY_DATA_COUNT);
		if ((reg & ZYNQMP_DP_REPLY_DATA_COUNT_MASK) != bytes)
			return -EIO;

		for (i = 0; i < bytes; i++)
			buf[i] = zynqmp_dp_read(dp, ZYNQMP_DP_AUX_REPLY_DATA);
	}

	return 0;
}

static ssize_t
zynqmp_dp_aux_transfer(struct drm_dp_aux *aux, struct drm_dp_aux_msg *msg)
{
	struct zynqmp_dp *dp = container_of(aux, struct zynqmp_dp, aux);
	int ret;
	unsigned int i, iter;

	/* Number of loops = timeout in msec / aux delay (400 usec) */
	iter = zynqmp_dp_aux_timeout_ms * 1000 / 400;
	iter = iter ? iter : 1;

	for (i = 0; i < iter; i++) {
		ret = zynqmp_dp_aux_cmd_submit(dp, msg->request, msg->address,
					       msg->buffer, msg->size,
					       &msg->reply);
		if (!ret) {
			dev_dbg(dp->dev, "aux %d retries\n", i);
			return msg->size;
		}

		if (dp->status == connector_status_disconnected) {
			dev_dbg(dp->dev, "no connected aux device\n");
			return -ENODEV;
		}

		usleep_range(400, 500);
	}

	dev_dbg(dp->dev, "failed to do aux transfer (%d)\n", ret);

	return ret;
}

/**
 * zynqmp_dp_aux_init - Initialize and register the DP AUX
 * @dp: DisplayPort IP core structure
 *
 * Program the AUX clock divider and filter and register the DP AUX adapter.
 *
 * Return: 0 on success, error value otherwise
 */
static int zynqmp_dp_aux_init(struct zynqmp_dp *dp)
{
	unsigned long rate;
	unsigned int w;

	/*
	 * The AUX_SIGNAL_WIDTH_FILTER is the number of APB clock cycles
	 * corresponding to the AUX pulse. Allowable values are 8, 16, 24, 32,
	 * 40 and 48. The AUX pulse width must be between 0.4µs and 0.6µs,
	 * compute the w / 8 value corresponding to 0.4µs rounded up, and make
	 * sure it stays below 0.6µs and within the allowable values.
	 */
	rate = clk_get_rate(dp->dpsub->apb_clk);
	w = DIV_ROUND_UP(4 * rate, 1000 * 1000 * 10 * 8) * 8;
	if (w > 6 * rate / (1000 * 1000 * 10) || w > 48) {
		dev_err(dp->dev, "aclk frequency too high\n");
		return -EINVAL;
	}

	zynqmp_dp_write(dp, ZYNQMP_DP_AUX_CLK_DIVIDER,
			(w << ZYNQMP_DP_AUX_CLK_DIVIDER_AUX_FILTER_SHIFT) |
			(rate / (1000 * 1000)));

	dp->aux.name = "ZynqMP DP AUX";
	dp->aux.dev = dp->dev;
	dp->aux.drm_dev = dp->bridge.dev;
	dp->aux.transfer = zynqmp_dp_aux_transfer;

	return drm_dp_aux_register(&dp->aux);
}

/**
 * zynqmp_dp_aux_cleanup - Cleanup the DP AUX
 * @dp: DisplayPort IP core structure
 *
 * Unregister the DP AUX adapter.
 */
static void zynqmp_dp_aux_cleanup(struct zynqmp_dp *dp)
{
	drm_dp_aux_unregister(&dp->aux);
}

/* -----------------------------------------------------------------------------
 * DisplayPort Generic Support
 */

/**
 * zynqmp_dp_update_misc - Write the misc registers
 * @dp: DisplayPort IP core structure
 *
 * The misc register values are stored in the structure, and this
 * function applies the values into the registers.
 */
static void zynqmp_dp_update_misc(struct zynqmp_dp *dp)
{
	zynqmp_dp_write(dp, ZYNQMP_DP_MAIN_STREAM_MISC0, dp->config.misc0);
	zynqmp_dp_write(dp, ZYNQMP_DP_MAIN_STREAM_MISC1, dp->config.misc1);
}

/**
 * zynqmp_dp_set_format - Set the input format
 * @dp: DisplayPort IP core structure
 * @info: Display info
 * @format: input format
 * @bpc: bits per component
 *
 * Update misc register values based on input @format and @bpc.
 *
 * Return: 0 on success, or -EINVAL.
 */
static int zynqmp_dp_set_format(struct zynqmp_dp *dp,
				const struct drm_display_info *info,
				enum zynqmp_dpsub_format format,
				unsigned int bpc)
{
	struct zynqmp_dp_config *config = &dp->config;
	unsigned int num_colors;

	config->misc0 &= ~ZYNQMP_DP_MAIN_STREAM_MISC0_COMP_FORMAT_MASK;
	config->misc1 &= ~ZYNQMP_DP_MAIN_STREAM_MISC1_Y_ONLY_EN;

	switch (format) {
	case ZYNQMP_DPSUB_FORMAT_RGB:
		config->misc0 |= ZYNQMP_DP_MAIN_STREAM_MISC0_COMP_FORMAT_RGB;
		num_colors = 3;
		break;

	case ZYNQMP_DPSUB_FORMAT_YCRCB444:
		config->misc0 |= ZYNQMP_DP_MAIN_STREAM_MISC0_COMP_FORMAT_YCRCB_444;
		num_colors = 3;
		break;

	case ZYNQMP_DPSUB_FORMAT_YCRCB422:
		config->misc0 |= ZYNQMP_DP_MAIN_STREAM_MISC0_COMP_FORMAT_YCRCB_422;
		num_colors = 2;
		break;

	case ZYNQMP_DPSUB_FORMAT_YONLY:
		config->misc1 |= ZYNQMP_DP_MAIN_STREAM_MISC1_Y_ONLY_EN;
		num_colors = 1;
		break;

	default:
		dev_err(dp->dev, "Invalid colormetry in DT\n");
		return -EINVAL;
	}

	if (info && info->bpc && bpc > info->bpc) {
		dev_warn(dp->dev,
			 "downgrading requested %ubpc to display limit %ubpc\n",
			 bpc, info->bpc);
		bpc = info->bpc;
	}

	config->misc0 &= ~ZYNQMP_DP_MAIN_STREAM_MISC0_BPC_MASK;

	switch (bpc) {
	case 6:
		config->misc0 |= ZYNQMP_DP_MAIN_STREAM_MISC0_BPC_6;
		break;
	case 8:
		config->misc0 |= ZYNQMP_DP_MAIN_STREAM_MISC0_BPC_8;
		break;
	case 10:
		config->misc0 |= ZYNQMP_DP_MAIN_STREAM_MISC0_BPC_10;
		break;
	case 12:
		config->misc0 |= ZYNQMP_DP_MAIN_STREAM_MISC0_BPC_12;
		break;
	case 16:
		config->misc0 |= ZYNQMP_DP_MAIN_STREAM_MISC0_BPC_16;
		break;
	default:
		dev_warn(dp->dev, "Not supported bpc (%u). fall back to 8bpc\n",
			 bpc);
		config->misc0 |= ZYNQMP_DP_MAIN_STREAM_MISC0_BPC_8;
		bpc = 8;
		break;
	}

	/* Update the current bpp based on the format. */
	config->bpp = bpc * num_colors;

	return 0;
}

/**
 * zynqmp_dp_encoder_mode_set_transfer_unit - Set the transfer unit values
 * @dp: DisplayPort IP core structure
 * @mode: requested display mode
 *
 * Set the transfer unit, and calculate all transfer unit size related values.
 * Calculation is based on DP and IP core specification.
 */
static void
zynqmp_dp_encoder_mode_set_transfer_unit(struct zynqmp_dp *dp,
					 const struct drm_display_mode *mode)
{
	u32 tu = ZYNQMP_DP_MSA_TRANSFER_UNIT_SIZE_TU_SIZE_DEF;
	u32 bw, vid_kbytes, avg_bytes_per_tu, init_wait;

	/* Use the max transfer unit size (default) */
	zynqmp_dp_write(dp, ZYNQMP_DP_MSA_TRANSFER_UNIT_SIZE, tu);

	vid_kbytes = mode->clock * (dp->config.bpp / 8);
	bw = drm_dp_bw_code_to_link_rate(dp->mode.bw_code);
	avg_bytes_per_tu = vid_kbytes * tu / (dp->mode.lane_cnt * bw / 1000);
	zynqmp_dp_write(dp, ZYNQMP_DP_MIN_BYTES_PER_TU,
			avg_bytes_per_tu / 1000);
	zynqmp_dp_write(dp, ZYNQMP_DP_FRAC_BYTES_PER_TU,
			avg_bytes_per_tu % 1000);

	/* Configure the initial wait cycle based on transfer unit size */
	if (tu < (avg_bytes_per_tu / 1000))
		init_wait = 0;
	else if ((avg_bytes_per_tu / 1000) <= 4)
		init_wait = tu;
	else
		init_wait = tu - avg_bytes_per_tu / 1000;

	zynqmp_dp_write(dp, ZYNQMP_DP_INIT_WAIT, init_wait);
}

/**
 * zynqmp_dp_encoder_mode_set_stream - Configure the main stream
 * @dp: DisplayPort IP core structure
 * @mode: requested display mode
 *
 * Configure the main stream based on the requested mode @mode. Calculation is
 * based on IP core specification.
 */
static void zynqmp_dp_encoder_mode_set_stream(struct zynqmp_dp *dp,
					      const struct drm_display_mode *mode)
{
	u8 lane_cnt = dp->mode.lane_cnt;
	u32 reg, wpl;
	unsigned int rate;

	zynqmp_dp_write(dp, ZYNQMP_DP_MAIN_STREAM_HTOTAL, mode->htotal);
	zynqmp_dp_write(dp, ZYNQMP_DP_MAIN_STREAM_VTOTAL, mode->vtotal);
	zynqmp_dp_write(dp, ZYNQMP_DP_MAIN_STREAM_POLARITY,
			(!!(mode->flags & DRM_MODE_FLAG_PVSYNC) <<
			 ZYNQMP_DP_MAIN_STREAM_POLARITY_VSYNC_SHIFT) |
			(!!(mode->flags & DRM_MODE_FLAG_PHSYNC) <<
			 ZYNQMP_DP_MAIN_STREAM_POLARITY_HSYNC_SHIFT));
	zynqmp_dp_write(dp, ZYNQMP_DP_MAIN_STREAM_HSWIDTH,
			mode->hsync_end - mode->hsync_start);
	zynqmp_dp_write(dp, ZYNQMP_DP_MAIN_STREAM_VSWIDTH,
			mode->vsync_end - mode->vsync_start);
	zynqmp_dp_write(dp, ZYNQMP_DP_MAIN_STREAM_HRES, mode->hdisplay);
	zynqmp_dp_write(dp, ZYNQMP_DP_MAIN_STREAM_VRES, mode->vdisplay);
	zynqmp_dp_write(dp, ZYNQMP_DP_MAIN_STREAM_HSTART,
			mode->htotal - mode->hsync_start);
	zynqmp_dp_write(dp, ZYNQMP_DP_MAIN_STREAM_VSTART,
			mode->vtotal - mode->vsync_start);

	/* In synchronous mode, set the dividers */
	if (dp->config.misc0 & ZYNQMP_DP_MAIN_STREAM_MISC0_SYNC_LOCK) {
		reg = drm_dp_bw_code_to_link_rate(dp->mode.bw_code);
		zynqmp_dp_write(dp, ZYNQMP_DP_MAIN_STREAM_N_VID, reg);
		zynqmp_dp_write(dp, ZYNQMP_DP_MAIN_STREAM_M_VID, mode->clock);
		rate = zynqmp_dpsub_get_audio_clk_rate(dp->dpsub);
		if (rate) {
			dev_dbg(dp->dev, "Audio rate: %d\n", rate / 512);
			zynqmp_dp_write(dp, ZYNQMP_DP_TX_N_AUD, reg);
			zynqmp_dp_write(dp, ZYNQMP_DP_TX_M_AUD, rate / 1000);
		}
	}

	/* Only 2 channel audio is supported now */
	if (zynqmp_dpsub_audio_enabled(dp->dpsub))
		zynqmp_dp_write(dp, ZYNQMP_DP_TX_AUDIO_CHANNELS, 1);

	zynqmp_dp_write(dp, ZYNQMP_DP_USER_PIX_WIDTH, 1);

	/* Translate to the native 16 bit datapath based on IP core spec */
	wpl = (mode->hdisplay * dp->config.bpp + 15) / 16;
	reg = wpl + wpl % lane_cnt - lane_cnt;
	zynqmp_dp_write(dp, ZYNQMP_DP_USER_DATA_COUNT_PER_LANE, reg);
}

/* -----------------------------------------------------------------------------
 * DISP Configuration
 */

static void zynqmp_dp_disp_enable(struct zynqmp_dp *dp,
				  struct drm_bridge_state *old_bridge_state)
{
	enum zynqmp_dpsub_layer_id layer_id;
	struct zynqmp_disp_layer *layer;
	const struct drm_format_info *info;

	if (dp->dpsub->connected_ports & BIT(ZYNQMP_DPSUB_PORT_LIVE_VIDEO))
		layer_id = ZYNQMP_DPSUB_LAYER_VID;
	else if (dp->dpsub->connected_ports & BIT(ZYNQMP_DPSUB_PORT_LIVE_GFX))
		layer_id = ZYNQMP_DPSUB_LAYER_GFX;
	else
		return;

	layer = dp->dpsub->layers[layer_id];

	/* TODO: Make the format configurable. */
	info = drm_format_info(DRM_FORMAT_YUV422);
	zynqmp_disp_layer_set_format(layer, info);
	zynqmp_disp_layer_enable(layer, ZYNQMP_DPSUB_LAYER_LIVE);

	if (layer_id == ZYNQMP_DPSUB_LAYER_GFX)
		zynqmp_disp_blend_set_global_alpha(dp->dpsub->disp, true, 255);
	else
		zynqmp_disp_blend_set_global_alpha(dp->dpsub->disp, false, 0);

	zynqmp_disp_enable(dp->dpsub->disp);
}

static void zynqmp_dp_disp_disable(struct zynqmp_dp *dp,
				   struct drm_bridge_state *old_bridge_state)
{
	struct zynqmp_disp_layer *layer;

	if (dp->dpsub->connected_ports & BIT(ZYNQMP_DPSUB_PORT_LIVE_VIDEO))
		layer = dp->dpsub->layers[ZYNQMP_DPSUB_LAYER_VID];
	else if (dp->dpsub->connected_ports & BIT(ZYNQMP_DPSUB_PORT_LIVE_GFX))
		layer = dp->dpsub->layers[ZYNQMP_DPSUB_LAYER_GFX];
	else
		return;

	zynqmp_disp_disable(dp->dpsub->disp);
	zynqmp_disp_layer_disable(layer);
}

/* -----------------------------------------------------------------------------
 * DRM Bridge
 */

static int zynqmp_dp_bridge_attach(struct drm_bridge *bridge,
				   enum drm_bridge_attach_flags flags)
{
	struct zynqmp_dp *dp = bridge_to_dp(bridge);
	int ret;

	/* Initialize and register the AUX adapter. */
	ret = zynqmp_dp_aux_init(dp);
	if (ret) {
		dev_err(dp->dev, "failed to initialize DP aux\n");
		return ret;
	}

	if (dp->next_bridge) {
		ret = drm_bridge_attach(bridge->encoder, dp->next_bridge,
					bridge, flags);
		if (ret < 0)
			goto error;
	}

	/* Now that initialisation is complete, enable interrupts. */
	zynqmp_dp_write(dp, ZYNQMP_DP_INT_EN, ZYNQMP_DP_INT_ALL);

	return 0;

error:
	zynqmp_dp_aux_cleanup(dp);
	return ret;
}

static void zynqmp_dp_bridge_detach(struct drm_bridge *bridge)
{
	struct zynqmp_dp *dp = bridge_to_dp(bridge);

	zynqmp_dp_aux_cleanup(dp);
}

static enum drm_mode_status
zynqmp_dp_bridge_mode_valid(struct drm_bridge *bridge,
			    const struct drm_display_info *info,
			    const struct drm_display_mode *mode)
{
	struct zynqmp_dp *dp = bridge_to_dp(bridge);
	int rate;

	if (mode->clock > ZYNQMP_MAX_FREQ) {
		dev_dbg(dp->dev, "filtered mode %s for high pixel rate\n",
			mode->name);
		drm_mode_debug_printmodeline(mode);
		return MODE_CLOCK_HIGH;
	}

	/* Check with link rate and lane count */
	rate = zynqmp_dp_max_rate(dp->link_config.max_rate,
				  dp->link_config.max_lanes, dp->config.bpp);
	if (mode->clock > rate) {
		dev_dbg(dp->dev, "filtered mode %s for high pixel rate\n",
			mode->name);
		drm_mode_debug_printmodeline(mode);
		return MODE_CLOCK_HIGH;
	}

	return MODE_OK;
}

static void zynqmp_dp_bridge_atomic_enable(struct drm_bridge *bridge,
					   struct drm_bridge_state *old_bridge_state)
{
	struct zynqmp_dp *dp = bridge_to_dp(bridge);
	struct drm_atomic_state *state = old_bridge_state->base.state;
	const struct drm_crtc_state *crtc_state;
	const struct drm_display_mode *adjusted_mode;
	const struct drm_display_mode *mode;
	struct drm_connector *connector;
	struct drm_crtc *crtc;
	unsigned int i;
	int rate;
	int ret;

	pm_runtime_get_sync(dp->dev);

	zynqmp_dp_disp_enable(dp, old_bridge_state);

	/*
	 * Retrieve the CRTC mode and adjusted mode. This requires a little
	 * dance to go from the bridge to the encoder, to the connector and to
	 * the CRTC.
	 */
	connector = drm_atomic_get_new_connector_for_encoder(state,
							     bridge->encoder);
	crtc = drm_atomic_get_new_connector_state(state, connector)->crtc;
	crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	adjusted_mode = &crtc_state->adjusted_mode;
	mode = &crtc_state->mode;

	zynqmp_dp_set_format(dp, &connector->display_info,
			     ZYNQMP_DPSUB_FORMAT_RGB, 8);

	/* Check again as bpp or format might have been changed */
	rate = zynqmp_dp_max_rate(dp->link_config.max_rate,
				  dp->link_config.max_lanes, dp->config.bpp);
	if (mode->clock > rate) {
		dev_err(dp->dev, "mode %s has too high pixel rate\n",
			mode->name);
		drm_mode_debug_printmodeline(mode);
	}

	/* Configure the mode */
	ret = zynqmp_dp_mode_configure(dp, adjusted_mode->clock, 0);
	if (ret < 0) {
		pm_runtime_put_sync(dp->dev);
		return;
	}

	zynqmp_dp_encoder_mode_set_transfer_unit(dp, adjusted_mode);
	zynqmp_dp_encoder_mode_set_stream(dp, adjusted_mode);

	/* Enable the encoder */
	dp->enabled = true;
	zynqmp_dp_update_misc(dp);
	if (zynqmp_dpsub_audio_enabled(dp->dpsub))
		zynqmp_dp_write(dp, ZYNQMP_DP_TX_AUDIO_CONTROL, 1);
	zynqmp_dp_write(dp, ZYNQMP_DP_TX_PHY_POWER_DOWN, 0);
	if (dp->status == connector_status_connected) {
		for (i = 0; i < 3; i++) {
			ret = drm_dp_dpcd_writeb(&dp->aux, DP_SET_POWER,
						 DP_SET_POWER_D0);
			if (ret == 1)
				break;
			usleep_range(300, 500);
		}
		/* Some monitors take time to wake up properly */
		msleep(zynqmp_dp_power_on_delay_ms);
	}
	if (ret != 1)
		dev_dbg(dp->dev, "DP aux failed\n");
	else
		zynqmp_dp_train_loop(dp);
	zynqmp_dp_write(dp, ZYNQMP_DP_SOFTWARE_RESET,
			ZYNQMP_DP_SOFTWARE_RESET_ALL);
	zynqmp_dp_write(dp, ZYNQMP_DP_MAIN_STREAM_ENABLE, 1);
}

static void zynqmp_dp_bridge_atomic_disable(struct drm_bridge *bridge,
					    struct drm_bridge_state *old_bridge_state)
{
	struct zynqmp_dp *dp = bridge_to_dp(bridge);

	dp->enabled = false;
	cancel_delayed_work(&dp->hpd_work);
	zynqmp_dp_write(dp, ZYNQMP_DP_MAIN_STREAM_ENABLE, 0);
	drm_dp_dpcd_writeb(&dp->aux, DP_SET_POWER, DP_SET_POWER_D3);
	zynqmp_dp_write(dp, ZYNQMP_DP_TX_PHY_POWER_DOWN,
			ZYNQMP_DP_TX_PHY_POWER_DOWN_ALL);
	if (zynqmp_dpsub_audio_enabled(dp->dpsub))
		zynqmp_dp_write(dp, ZYNQMP_DP_TX_AUDIO_CONTROL, 0);

	zynqmp_dp_disp_disable(dp, old_bridge_state);

	pm_runtime_put_sync(dp->dev);
}

#define ZYNQMP_DP_MIN_H_BACKPORCH	20

static int zynqmp_dp_bridge_atomic_check(struct drm_bridge *bridge,
					 struct drm_bridge_state *bridge_state,
					 struct drm_crtc_state *crtc_state,
					 struct drm_connector_state *conn_state)
{
	struct zynqmp_dp *dp = bridge_to_dp(bridge);
	struct drm_display_mode *mode = &crtc_state->mode;
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;
	int diff = mode->htotal - mode->hsync_end;

	/*
	 * ZynqMP DP requires horizontal backporch to be greater than 12.
	 * This limitation may not be compatible with the sink device.
	 */
	if (diff < ZYNQMP_DP_MIN_H_BACKPORCH) {
		int vrefresh = (adjusted_mode->clock * 1000) /
			       (adjusted_mode->vtotal * adjusted_mode->htotal);

		dev_dbg(dp->dev, "hbackporch adjusted: %d to %d",
			diff, ZYNQMP_DP_MIN_H_BACKPORCH - diff);
		diff = ZYNQMP_DP_MIN_H_BACKPORCH - diff;
		adjusted_mode->htotal += diff;
		adjusted_mode->clock = adjusted_mode->vtotal *
				       adjusted_mode->htotal * vrefresh / 1000;
	}

	return 0;
}

static enum drm_connector_status zynqmp_dp_bridge_detect(struct drm_bridge *bridge)
{
	struct zynqmp_dp *dp = bridge_to_dp(bridge);
	struct zynqmp_dp_link_config *link_config = &dp->link_config;
	u32 state, i;
	int ret;

	/*
	 * This is from heuristic. It takes some delay (ex, 100 ~ 500 msec) to
	 * get the HPD signal with some monitors.
	 */
	for (i = 0; i < 10; i++) {
		state = zynqmp_dp_read(dp, ZYNQMP_DP_INTERRUPT_SIGNAL_STATE);
		if (state & ZYNQMP_DP_INTERRUPT_SIGNAL_STATE_HPD)
			break;
		msleep(100);
	}

	if (state & ZYNQMP_DP_INTERRUPT_SIGNAL_STATE_HPD) {
		ret = drm_dp_dpcd_read(&dp->aux, 0x0, dp->dpcd,
				       sizeof(dp->dpcd));
		if (ret < 0) {
			dev_dbg(dp->dev, "DPCD read failed");
			goto disconnected;
		}

		link_config->max_rate = min_t(int,
					      drm_dp_max_link_rate(dp->dpcd),
					      DP_HIGH_BIT_RATE2);
		link_config->max_lanes = min_t(u8,
					       drm_dp_max_lane_count(dp->dpcd),
					       dp->num_lanes);

		dp->status = connector_status_connected;
		return connector_status_connected;
	}

disconnected:
	dp->status = connector_status_disconnected;
	return connector_status_disconnected;
}

static struct edid *zynqmp_dp_bridge_get_edid(struct drm_bridge *bridge,
					      struct drm_connector *connector)
{
	struct zynqmp_dp *dp = bridge_to_dp(bridge);

	return drm_get_edid(connector, &dp->aux.ddc);
}

static const struct drm_bridge_funcs zynqmp_dp_bridge_funcs = {
	.attach = zynqmp_dp_bridge_attach,
	.detach = zynqmp_dp_bridge_detach,
	.mode_valid = zynqmp_dp_bridge_mode_valid,
	.atomic_enable = zynqmp_dp_bridge_atomic_enable,
	.atomic_disable = zynqmp_dp_bridge_atomic_disable,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.atomic_check = zynqmp_dp_bridge_atomic_check,
	.detect = zynqmp_dp_bridge_detect,
	.get_edid = zynqmp_dp_bridge_get_edid,
};

/* -----------------------------------------------------------------------------
 * Interrupt Handling
 */

/**
 * zynqmp_dp_enable_vblank - Enable vblank
 * @dp: DisplayPort IP core structure
 *
 * Enable vblank interrupt
 */
void zynqmp_dp_enable_vblank(struct zynqmp_dp *dp)
{
	zynqmp_dp_write(dp, ZYNQMP_DP_INT_EN, ZYNQMP_DP_INT_VBLANK_START);
}

/**
 * zynqmp_dp_disable_vblank - Disable vblank
 * @dp: DisplayPort IP core structure
 *
 * Disable vblank interrupt
 */
void zynqmp_dp_disable_vblank(struct zynqmp_dp *dp)
{
	zynqmp_dp_write(dp, ZYNQMP_DP_INT_DS, ZYNQMP_DP_INT_VBLANK_START);
}

static void zynqmp_dp_hpd_work_func(struct work_struct *work)
{
	struct zynqmp_dp *dp = container_of(work, struct zynqmp_dp,
					    hpd_work.work);
	enum drm_connector_status status;

	status = zynqmp_dp_bridge_detect(&dp->bridge);
	drm_bridge_hpd_notify(&dp->bridge, status);
}

static irqreturn_t zynqmp_dp_irq_handler(int irq, void *data)
{
	struct zynqmp_dp *dp = (struct zynqmp_dp *)data;
	u32 status, mask;

	status = zynqmp_dp_read(dp, ZYNQMP_DP_INT_STATUS);
	mask = zynqmp_dp_read(dp, ZYNQMP_DP_INT_MASK);
	if (!(status & ~mask))
		return IRQ_NONE;

	/* dbg for diagnostic, but not much that the driver can do */
	if (status & ZYNQMP_DP_INT_CHBUF_UNDERFLW_MASK)
		dev_dbg_ratelimited(dp->dev, "underflow interrupt\n");
	if (status & ZYNQMP_DP_INT_CHBUF_OVERFLW_MASK)
		dev_dbg_ratelimited(dp->dev, "overflow interrupt\n");

	zynqmp_dp_write(dp, ZYNQMP_DP_INT_STATUS, status);

	if (status & ZYNQMP_DP_INT_VBLANK_START)
		zynqmp_dpsub_drm_handle_vblank(dp->dpsub);

	if (status & ZYNQMP_DP_INT_HPD_EVENT)
		schedule_delayed_work(&dp->hpd_work, 0);

	if (status & ZYNQMP_DP_INT_HPD_IRQ) {
		int ret;
		u8 status[DP_LINK_STATUS_SIZE + 2];

		ret = drm_dp_dpcd_read(&dp->aux, DP_SINK_COUNT, status,
				       DP_LINK_STATUS_SIZE + 2);
		if (ret < 0)
			goto handled;

		if (status[4] & DP_LINK_STATUS_UPDATED ||
		    !drm_dp_clock_recovery_ok(&status[2], dp->mode.lane_cnt) ||
		    !drm_dp_channel_eq_ok(&status[2], dp->mode.lane_cnt)) {
			zynqmp_dp_train_loop(dp);
		}
	}

handled:
	return IRQ_HANDLED;
}

/* -----------------------------------------------------------------------------
 * Initialization & Cleanup
 */

int zynqmp_dp_probe(struct zynqmp_dpsub *dpsub)
{
	struct platform_device *pdev = to_platform_device(dpsub->dev);
	struct drm_bridge *bridge;
	struct zynqmp_dp *dp;
	struct resource *res;
	int ret;

	dp = kzalloc(sizeof(*dp), GFP_KERNEL);
	if (!dp)
		return -ENOMEM;

	dp->dev = &pdev->dev;
	dp->dpsub = dpsub;
	dp->status = connector_status_disconnected;

	INIT_DELAYED_WORK(&dp->hpd_work, zynqmp_dp_hpd_work_func);

	/* Acquire all resources (IOMEM, IRQ and PHYs). */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dp");
	dp->iomem = devm_ioremap_resource(dp->dev, res);
	if (IS_ERR(dp->iomem)) {
		ret = PTR_ERR(dp->iomem);
		goto err_free;
	}

	dp->irq = platform_get_irq(pdev, 0);
	if (dp->irq < 0) {
		ret = dp->irq;
		goto err_free;
	}

	dp->reset = devm_reset_control_get(dp->dev, NULL);
	if (IS_ERR(dp->reset)) {
		if (PTR_ERR(dp->reset) != -EPROBE_DEFER)
			dev_err(dp->dev, "failed to get reset: %ld\n",
				PTR_ERR(dp->reset));
		ret = PTR_ERR(dp->reset);
		goto err_free;
	}

	ret = zynqmp_dp_reset(dp, false);
	if (ret < 0)
		goto err_free;

	ret = zynqmp_dp_phy_probe(dp);
	if (ret)
		goto err_reset;

	/* Initialize the bridge. */
	bridge = &dp->bridge;
	bridge->funcs = &zynqmp_dp_bridge_funcs;
	bridge->ops = DRM_BRIDGE_OP_DETECT | DRM_BRIDGE_OP_EDID
		    | DRM_BRIDGE_OP_HPD;
	bridge->type = DRM_MODE_CONNECTOR_DisplayPort;
	dpsub->bridge = bridge;

	/*
	 * Acquire the next bridge in the chain. Ignore errors caused by port@5
	 * not being connected for backward-compatibility with older DTs.
	 */
	ret = drm_of_find_panel_or_bridge(dp->dev->of_node, 5, 0, NULL,
					  &dp->next_bridge);
	if (ret < 0 && ret != -ENODEV)
		goto err_reset;

	/* Initialize the hardware. */
	dp->config.misc0 &= ~ZYNQMP_DP_MAIN_STREAM_MISC0_SYNC_LOCK;
	zynqmp_dp_set_format(dp, NULL, ZYNQMP_DPSUB_FORMAT_RGB, 8);

	zynqmp_dp_write(dp, ZYNQMP_DP_TX_PHY_POWER_DOWN,
			ZYNQMP_DP_TX_PHY_POWER_DOWN_ALL);
	zynqmp_dp_set(dp, ZYNQMP_DP_PHY_RESET, ZYNQMP_DP_PHY_RESET_ALL_RESET);
	zynqmp_dp_write(dp, ZYNQMP_DP_FORCE_SCRAMBLER_RESET, 1);
	zynqmp_dp_write(dp, ZYNQMP_DP_TRANSMITTER_ENABLE, 0);
	zynqmp_dp_write(dp, ZYNQMP_DP_INT_DS, 0xffffffff);

	ret = zynqmp_dp_phy_init(dp);
	if (ret)
		goto err_reset;

	zynqmp_dp_write(dp, ZYNQMP_DP_TRANSMITTER_ENABLE, 1);

	/*
	 * Now that the hardware is initialized and won't generate spurious
	 * interrupts, request the IRQ.
	 */
	ret = devm_request_threaded_irq(dp->dev, dp->irq, NULL,
					zynqmp_dp_irq_handler, IRQF_ONESHOT,
					dev_name(dp->dev), dp);
	if (ret < 0)
		goto err_phy_exit;

	dpsub->dp = dp;

	dev_dbg(dp->dev, "ZynqMP DisplayPort Tx probed with %u lanes\n",
		dp->num_lanes);

	return 0;

err_phy_exit:
	zynqmp_dp_phy_exit(dp);
err_reset:
	zynqmp_dp_reset(dp, true);
err_free:
	kfree(dp);
	return ret;
}

void zynqmp_dp_remove(struct zynqmp_dpsub *dpsub)
{
	struct zynqmp_dp *dp = dpsub->dp;

	zynqmp_dp_write(dp, ZYNQMP_DP_INT_DS, ZYNQMP_DP_INT_ALL);
	disable_irq(dp->irq);

	cancel_delayed_work_sync(&dp->hpd_work);

	zynqmp_dp_write(dp, ZYNQMP_DP_TRANSMITTER_ENABLE, 0);
	zynqmp_dp_write(dp, ZYNQMP_DP_INT_DS, 0xffffffff);

	zynqmp_dp_phy_exit(dp);
	zynqmp_dp_reset(dp, true);
}

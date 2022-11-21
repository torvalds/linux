/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cadence MHDP8546 DP bridge driver.
 *
 * Copyright (C) 2020 Cadence Design Systems, Inc.
 *
 * Author: Quentin Schulz <quentin.schulz@free-electrons.com>
 *         Swapnil Jakhade <sjakhade@cadence.com>
 */

#ifndef CDNS_MHDP8546_CORE_H
#define CDNS_MHDP8546_CORE_H

#include <linux/bits.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/dp/drm_dp_helper.h>

struct clk;
struct device;
struct phy;

/* Register offsets */
#define CDNS_APB_CTRL				0x00000
#define CDNS_CPU_STALL				BIT(3)

#define CDNS_MAILBOX_FULL			0x00008
#define CDNS_MAILBOX_EMPTY			0x0000c
#define CDNS_MAILBOX_TX_DATA			0x00010
#define CDNS_MAILBOX_RX_DATA			0x00014
#define CDNS_KEEP_ALIVE				0x00018
#define CDNS_KEEP_ALIVE_MASK			GENMASK(7, 0)

#define CDNS_VER_L				0x0001C
#define CDNS_VER_H				0x00020
#define CDNS_LIB_L_ADDR				0x00024
#define CDNS_LIB_H_ADDR				0x00028

#define CDNS_MB_INT_MASK			0x00034
#define CDNS_MB_INT_STATUS			0x00038

#define CDNS_SW_CLK_L				0x0003c
#define CDNS_SW_CLK_H				0x00040

#define CDNS_SW_EVENT0				0x00044
#define CDNS_DPTX_HPD				BIT(0)
#define CDNS_HDCP_TX_STATUS			BIT(4)
#define CDNS_HDCP2_TX_IS_KM_STORED		BIT(5)
#define CDNS_HDCP2_TX_STORE_KM			BIT(6)
#define CDNS_HDCP_TX_IS_RCVR_ID_VALID		BIT(7)

#define CDNS_SW_EVENT1				0x00048
#define CDNS_SW_EVENT2				0x0004c
#define CDNS_SW_EVENT3				0x00050

#define CDNS_APB_INT_MASK			0x0006C
#define CDNS_APB_INT_MASK_MAILBOX_INT		BIT(0)
#define CDNS_APB_INT_MASK_SW_EVENT_INT		BIT(1)

#define CDNS_APB_INT_STATUS			0x00070

#define CDNS_DPTX_CAR				0x00904
#define CDNS_VIF_CLK_EN				BIT(0)
#define CDNS_VIF_CLK_RSTN			BIT(1)

#define CDNS_SOURCE_VIDEO_IF(s)			(0x00b00 + ((s) * 0x20))
#define CDNS_BND_HSYNC2VSYNC(s)			(CDNS_SOURCE_VIDEO_IF(s) + \
						 0x00)
#define CDNS_IP_DTCT_WIN			GENMASK(11, 0)
#define CDNS_IP_DET_INTERLACE_FORMAT		BIT(12)
#define CDNS_IP_BYPASS_V_INTERFACE		BIT(13)

#define CDNS_HSYNC2VSYNC_POL_CTRL(s)		(CDNS_SOURCE_VIDEO_IF(s) + \
						 0x10)
#define CDNS_H2V_HSYNC_POL_ACTIVE_LOW		BIT(1)
#define CDNS_H2V_VSYNC_POL_ACTIVE_LOW		BIT(2)

#define CDNS_DPTX_PHY_CONFIG			0x02000
#define CDNS_PHY_TRAINING_EN			BIT(0)
#define CDNS_PHY_TRAINING_TYPE(x)		(((x) & GENMASK(3, 0)) << 1)
#define CDNS_PHY_SCRAMBLER_BYPASS		BIT(5)
#define CDNS_PHY_ENCODER_BYPASS			BIT(6)
#define CDNS_PHY_SKEW_BYPASS			BIT(7)
#define CDNS_PHY_TRAINING_AUTO			BIT(8)
#define CDNS_PHY_LANE0_SKEW(x)			(((x) & GENMASK(2, 0)) << 9)
#define CDNS_PHY_LANE1_SKEW(x)			(((x) & GENMASK(2, 0)) << 12)
#define CDNS_PHY_LANE2_SKEW(x)			(((x) & GENMASK(2, 0)) << 15)
#define CDNS_PHY_LANE3_SKEW(x)			(((x) & GENMASK(2, 0)) << 18)
#define CDNS_PHY_COMMON_CONFIG			(CDNS_PHY_LANE1_SKEW(1) | \
						CDNS_PHY_LANE2_SKEW(2) |  \
						CDNS_PHY_LANE3_SKEW(3))
#define CDNS_PHY_10BIT_EN			BIT(21)

#define CDNS_DP_FRAMER_GLOBAL_CONFIG		0x02200
#define CDNS_DP_NUM_LANES(x)			((x) - 1)
#define CDNS_DP_MST_EN				BIT(2)
#define CDNS_DP_FRAMER_EN			BIT(3)
#define CDNS_DP_RATE_GOVERNOR_EN		BIT(4)
#define CDNS_DP_NO_VIDEO_MODE			BIT(5)
#define CDNS_DP_DISABLE_PHY_RST			BIT(6)
#define CDNS_DP_WR_FAILING_EDGE_VSYNC		BIT(7)

#define CDNS_DP_FRAMER_TU			0x02208
#define CDNS_DP_FRAMER_TU_SIZE(x)		(((x) & GENMASK(6, 0)) << 8)
#define CDNS_DP_FRAMER_TU_VS(x)			((x) & GENMASK(5, 0))
#define CDNS_DP_FRAMER_TU_CNT_RST_EN		BIT(15)

#define CDNS_DP_MTPH_CONTROL			0x02264
#define CDNS_DP_MTPH_ECF_EN			BIT(0)
#define CDNS_DP_MTPH_ACT_EN			BIT(1)
#define CDNS_DP_MTPH_LVP_EN			BIT(2)

#define CDNS_DP_MTPH_STATUS			0x0226C
#define CDNS_DP_MTPH_ACT_STATUS			BIT(0)

#define CDNS_DP_LANE_EN				0x02300
#define CDNS_DP_LANE_EN_LANES(x)		GENMASK((x) - 1, 0)

#define CDNS_DP_ENHNCD				0x02304

#define CDNS_DPTX_STREAM(s)			(0x03000 + (s) * 0x80)
#define CDNS_DP_MSA_HORIZONTAL_0(s)		(CDNS_DPTX_STREAM(s) + 0x00)
#define CDNS_DP_MSAH0_H_TOTAL(x)		(x)
#define CDNS_DP_MSAH0_HSYNC_START(x)		((x) << 16)

#define CDNS_DP_MSA_HORIZONTAL_1(s)		(CDNS_DPTX_STREAM(s) + 0x04)
#define CDNS_DP_MSAH1_HSYNC_WIDTH(x)		(x)
#define CDNS_DP_MSAH1_HSYNC_POL_LOW		BIT(15)
#define CDNS_DP_MSAH1_HDISP_WIDTH(x)		((x) << 16)

#define CDNS_DP_MSA_VERTICAL_0(s)		(CDNS_DPTX_STREAM(s) + 0x08)
#define CDNS_DP_MSAV0_V_TOTAL(x)		(x)
#define CDNS_DP_MSAV0_VSYNC_START(x)		((x) << 16)

#define CDNS_DP_MSA_VERTICAL_1(s)		(CDNS_DPTX_STREAM(s) + 0x0c)
#define CDNS_DP_MSAV1_VSYNC_WIDTH(x)		(x)
#define CDNS_DP_MSAV1_VSYNC_POL_LOW		BIT(15)
#define CDNS_DP_MSAV1_VDISP_WIDTH(x)		((x) << 16)

#define CDNS_DP_MSA_MISC(s)			(CDNS_DPTX_STREAM(s) + 0x10)
#define CDNS_DP_STREAM_CONFIG(s)		(CDNS_DPTX_STREAM(s) + 0x14)
#define CDNS_DP_STREAM_CONFIG_2(s)		(CDNS_DPTX_STREAM(s) + 0x2c)
#define CDNS_DP_SC2_TU_VS_DIFF(x)		((x) << 8)

#define CDNS_DP_HORIZONTAL(s)			(CDNS_DPTX_STREAM(s) + 0x30)
#define CDNS_DP_H_HSYNC_WIDTH(x)		(x)
#define CDNS_DP_H_H_TOTAL(x)			((x) << 16)

#define CDNS_DP_VERTICAL_0(s)			(CDNS_DPTX_STREAM(s) + 0x34)
#define CDNS_DP_V0_VHEIGHT(x)			(x)
#define CDNS_DP_V0_VSTART(x)			((x) << 16)

#define CDNS_DP_VERTICAL_1(s)			(CDNS_DPTX_STREAM(s) + 0x38)
#define CDNS_DP_V1_VTOTAL(x)			(x)
#define CDNS_DP_V1_VTOTAL_EVEN			BIT(16)

#define CDNS_DP_MST_SLOT_ALLOCATE(s)		(CDNS_DPTX_STREAM(s) + 0x44)
#define CDNS_DP_S_ALLOC_START_SLOT(x)		(x)
#define CDNS_DP_S_ALLOC_END_SLOT(x)		((x) << 8)

#define CDNS_DP_RATE_GOVERNING(s)		(CDNS_DPTX_STREAM(s) + 0x48)
#define CDNS_DP_RG_TARG_AV_SLOTS_Y(x)		(x)
#define CDNS_DP_RG_TARG_AV_SLOTS_X(x)		((x) << 4)
#define CDNS_DP_RG_ENABLE			BIT(10)

#define CDNS_DP_FRAMER_PXL_REPR(s)		(CDNS_DPTX_STREAM(s) + 0x4c)
#define CDNS_DP_FRAMER_6_BPC			BIT(0)
#define CDNS_DP_FRAMER_8_BPC			BIT(1)
#define CDNS_DP_FRAMER_10_BPC			BIT(2)
#define CDNS_DP_FRAMER_12_BPC			BIT(3)
#define CDNS_DP_FRAMER_16_BPC			BIT(4)
#define CDNS_DP_FRAMER_PXL_FORMAT		0x8
#define CDNS_DP_FRAMER_RGB			BIT(0)
#define CDNS_DP_FRAMER_YCBCR444			BIT(1)
#define CDNS_DP_FRAMER_YCBCR422			BIT(2)
#define CDNS_DP_FRAMER_YCBCR420			BIT(3)
#define CDNS_DP_FRAMER_Y_ONLY			BIT(4)

#define CDNS_DP_FRAMER_SP(s)			(CDNS_DPTX_STREAM(s) + 0x50)
#define CDNS_DP_FRAMER_VSYNC_POL_LOW		BIT(0)
#define CDNS_DP_FRAMER_HSYNC_POL_LOW		BIT(1)
#define CDNS_DP_FRAMER_INTERLACE		BIT(2)

#define CDNS_DP_LINE_THRESH(s)			(CDNS_DPTX_STREAM(s) + 0x64)
#define CDNS_DP_ACTIVE_LINE_THRESH(x)		(x)

#define CDNS_DP_VB_ID(s)			(CDNS_DPTX_STREAM(s) + 0x68)
#define CDNS_DP_VB_ID_INTERLACED		BIT(2)
#define CDNS_DP_VB_ID_COMPRESSED		BIT(6)

#define CDNS_DP_FRONT_BACK_PORCH(s)		(CDNS_DPTX_STREAM(s) + 0x78)
#define CDNS_DP_BACK_PORCH(x)			(x)
#define CDNS_DP_FRONT_PORCH(x)			((x) << 16)

#define CDNS_DP_BYTE_COUNT(s)			(CDNS_DPTX_STREAM(s) + 0x7c)
#define CDNS_DP_BYTE_COUNT_BYTES_IN_CHUNK_SHIFT	16

/* mailbox */
#define MAILBOX_RETRY_US			1000
#define MAILBOX_TIMEOUT_US			2000000

#define MB_OPCODE_ID				0
#define MB_MODULE_ID				1
#define MB_SIZE_MSB_ID				2
#define MB_SIZE_LSB_ID				3
#define MB_DATA_ID				4

#define MB_MODULE_ID_DP_TX			0x01
#define MB_MODULE_ID_HDCP_TX			0x07
#define MB_MODULE_ID_HDCP_RX			0x08
#define MB_MODULE_ID_HDCP_GENERAL		0x09
#define MB_MODULE_ID_GENERAL			0x0a

/* firmware and opcodes */
#define FW_NAME					"cadence/mhdp8546.bin"
#define CDNS_MHDP_IMEM				0x10000

#define GENERAL_MAIN_CONTROL			0x01
#define GENERAL_TEST_ECHO			0x02
#define GENERAL_BUS_SETTINGS			0x03
#define GENERAL_TEST_ACCESS			0x04
#define GENERAL_REGISTER_READ			0x07

#define DPTX_SET_POWER_MNG			0x00
#define DPTX_GET_EDID				0x02
#define DPTX_READ_DPCD				0x03
#define DPTX_WRITE_DPCD				0x04
#define DPTX_ENABLE_EVENT			0x05
#define DPTX_WRITE_REGISTER			0x06
#define DPTX_READ_REGISTER			0x07
#define DPTX_WRITE_FIELD			0x08
#define DPTX_READ_EVENT				0x0a
#define DPTX_GET_LAST_AUX_STAUS			0x0e
#define DPTX_HPD_STATE				0x11
#define DPTX_ADJUST_LT				0x12

#define FW_STANDBY				0
#define FW_ACTIVE				1

/* HPD */
#define DPTX_READ_EVENT_HPD_TO_HIGH             BIT(0)
#define DPTX_READ_EVENT_HPD_TO_LOW              BIT(1)
#define DPTX_READ_EVENT_HPD_PULSE               BIT(2)
#define DPTX_READ_EVENT_HPD_STATE               BIT(3)

/* general */
#define CDNS_DP_TRAINING_PATTERN_4		0x7

#define CDNS_KEEP_ALIVE_TIMEOUT			2000

#define CDNS_VOLT_SWING(x)			((x) & GENMASK(1, 0))
#define CDNS_FORCE_VOLT_SWING			BIT(2)

#define CDNS_PRE_EMPHASIS(x)			((x) & GENMASK(1, 0))
#define CDNS_FORCE_PRE_EMPHASIS			BIT(2)

#define CDNS_SUPPORT_TPS(x)			BIT((x) - 1)

#define CDNS_FAST_LINK_TRAINING			BIT(0)

#define CDNS_LANE_MAPPING_TYPE_C_LANE_0(x)	((x) & GENMASK(1, 0))
#define CDNS_LANE_MAPPING_TYPE_C_LANE_1(x)	((x) & GENMASK(3, 2))
#define CDNS_LANE_MAPPING_TYPE_C_LANE_2(x)	((x) & GENMASK(5, 4))
#define CDNS_LANE_MAPPING_TYPE_C_LANE_3(x)	((x) & GENMASK(7, 6))
#define CDNS_LANE_MAPPING_NORMAL		0xe4
#define CDNS_LANE_MAPPING_FLIPPED		0x1b

#define CDNS_DP_MAX_NUM_LANES			4
#define CDNS_DP_TEST_VSC_SDP			BIT(6) /* 1.3+ */
#define CDNS_DP_TEST_COLOR_FORMAT_RAW_Y_ONLY	BIT(7)

#define CDNS_MHDP_MAX_STREAMS			4

#define DP_LINK_CAP_ENHANCED_FRAMING		BIT(0)

struct cdns_mhdp_link {
	unsigned char revision;
	unsigned int rate;
	unsigned int num_lanes;
	unsigned long capabilities;
};

struct cdns_mhdp_host {
	unsigned int link_rate;
	u8 lanes_cnt;
	u8 volt_swing;
	u8 pre_emphasis;
	u8 pattern_supp;
	u8 lane_mapping;
	bool fast_link;
	bool enhanced;
	bool scrambler;
	bool ssc;
};

struct cdns_mhdp_sink {
	unsigned int link_rate;
	u8 lanes_cnt;
	u8 pattern_supp;
	bool fast_link;
	bool enhanced;
	bool ssc;
};

struct cdns_mhdp_display_fmt {
	u32 color_format;
	u32 bpc;
	bool y_only;
};

/*
 * These enums present MHDP hw initialization state
 * Legal state transitions are:
 * MHDP_HW_READY <-> MHDP_HW_STOPPED
 */
enum mhdp_hw_state {
	MHDP_HW_READY = 1,	/* HW ready, FW active */
	MHDP_HW_STOPPED		/* Driver removal FW to be stopped */
};

struct cdns_mhdp_device;

struct mhdp_platform_ops {
	int (*init)(struct cdns_mhdp_device *mhdp);
	void (*exit)(struct cdns_mhdp_device *mhdp);
	void (*enable)(struct cdns_mhdp_device *mhdp);
	void (*disable)(struct cdns_mhdp_device *mhdp);
};

struct cdns_mhdp_bridge_state {
	struct drm_bridge_state base;
	struct drm_display_mode *current_mode;
};

struct cdns_mhdp_platform_info {
	const struct drm_bridge_timings *timings;
	const struct mhdp_platform_ops *ops;
};

#define to_cdns_mhdp_bridge_state(s) \
		container_of(s, struct cdns_mhdp_bridge_state, base)

struct cdns_mhdp_hdcp {
	struct delayed_work check_work;
	struct work_struct prop_work;
	struct mutex mutex; /* mutex to protect hdcp.value */
	u32 value;
	u8 hdcp_content_type;
};

struct cdns_mhdp_device {
	void __iomem *regs;
	void __iomem *sapb_regs;
	void __iomem *j721e_regs;

	struct device *dev;
	struct clk *clk;
	struct phy *phy;

	const struct cdns_mhdp_platform_info *info;

	/* This is to protect mailbox communications with the firmware */
	struct mutex mbox_mutex;

	/*
	 * "link_mutex" protects the access to all the link parameters
	 * including the link training process. Link training will be
	 * invoked both from threaded interrupt handler and from atomic
	 * callbacks when link_up is not set. So this mutex protects
	 * flags such as link_up, bridge_enabled, link.num_lanes,
	 * link.rate etc.
	 */
	struct mutex link_mutex;

	struct drm_connector connector;
	struct drm_bridge bridge;

	struct cdns_mhdp_link link;
	struct drm_dp_aux aux;

	struct cdns_mhdp_host host;
	struct cdns_mhdp_sink sink;
	struct cdns_mhdp_display_fmt display_fmt;
	u8 stream_id;

	bool link_up;
	bool plugged;

	/*
	 * "start_lock" protects the access to bridge_attached and
	 * hw_state data members that control the delayed firmware
	 * loading and attaching the bridge. They are accessed from
	 * both the DRM core and cdns_mhdp_fw_cb(). In most cases just
	 * protecting the data members is enough, but the irq mask
	 * setting needs to be protected when enabling the FW.
	 */
	spinlock_t start_lock;
	bool bridge_attached;
	bool bridge_enabled;
	enum mhdp_hw_state hw_state;
	wait_queue_head_t fw_load_wq;

	/* Work struct to schedule a uevent on link train failure */
	struct work_struct modeset_retry_work;
	struct work_struct hpd_work;

	wait_queue_head_t sw_events_wq;
	u32 sw_events;

	struct cdns_mhdp_hdcp hdcp;
	bool hdcp_supported;
};

#define connector_to_mhdp(x) container_of(x, struct cdns_mhdp_device, connector)
#define bridge_to_mhdp(x) container_of(x, struct cdns_mhdp_device, bridge)

u32 cdns_mhdp_wait_for_sw_event(struct cdns_mhdp_device *mhdp, uint32_t event);

#endif

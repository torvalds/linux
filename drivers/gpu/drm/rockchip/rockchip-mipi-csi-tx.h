/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2018 Fuzhou Rockchip Electronics Co., Ltd
 */

#ifndef ROCKCHIP_MIPI_CSI_TX
#define ROCKCHIP_MIPI_CSI_TX

#define DRIVER_NAME    "rockchip-mipi-csi"

#define CSITX_CONFIG_DONE		0x0000
#define m_CONFIG_DONE			BIT(0)
#define m_CONFIG_DONE_IMD		BIT(4)
#define m_CONFIG_DONE_MODE		BIT(8)
#define v_CONFIG_DONE(x)		(((x) & 0x1) << 0)
#define v_CONFIG_DONE_IMD(x)		(((x) & 0x1) << 4)
#define v_CONFIG_DONE_MODE(x)		(((x) & 0x1) << 8)
enum CONFIG_DONE_MODE {
	FRAME_END_RX_MODE,
	FRAME_END_TX_MODE
};

#define CSITX_ENABLE			0x0004
#define m_CSITX_EN			BIT(0)
#define m_CPHY_EN			BIT(1)
#define m_DPHY_EN			BIT(2)
#define m_LANE_NUM			GENMASK(5, 4)
#define m_IDI_48BIT_EN			BIT(9)
#define v_CSITX_EN(x)			(((x) & 0x1) << 0)
#define v_CPHY_EN(x)			(((x) & 0x1) << 1)
#define v_DPHY_EN(x)			(((x) & 0x1) << 2)
#define v_LANE_NUM(x)			(((x) & 0x3) << 4)
#define v_IDI_48BIT_EN(x)		(((x) & 0x1) << 9)

#define CSITX_VERSION			0x0008
#define CSITX_SYS_CTRL0			0x0010
#define m_SOFT_RESET			BIT(0)
#define v_SOFT_RESET(x)			(((x) & 0x1) << 0)

#define CSITX_SYS_CTRL1			0x0014
#define m_BYPASS_SELECT			BIT(0)
#define v_BYPASS_SELECT(x)		(((x) & 0x1) << 0)

#define CSITX_SYS_CTRL2			0x0018
#define m_VSYNC_ENABLE			BIT(0)
#define m_HSYNC_ENABLE			BIT(1)
#define m_IDI_WHOLE_FRM_EN		BIT(4)
#define m_VOP_WHOLE_FRM_EN		BIT(5)
#define v_VSYNC_ENABLE(x)		(((x) & 0x1) << 0)
#define v_HSYNC_ENABLE(x)		(((x) & 0x1) << 1)
#define v_IDI_WHOLE_FRM_EN(x)		(((x) & 0x1) << 4)
#define v_VOP_WHOLE_FRM_EN(x)		(((x) & 0x1) << 5)

#define CSITX_SYS_CTRL3			0x001c
#define m_NON_CONTINUES_MODE_EN		BIT(0)
#define m_CONT_MODE_CLK_SET		BIT(4)
#define m_CONT_MODE_CLK_CLR		BIT(8)
#define v_NON_CONTINUES_MODE_EN(x)	(((x) & 0x1) << 0)
#define v_CONT_MODE_CLK_SET(x)		(((x) & 0x1) << 4)
#define v_CONT_MODE_CLK_CLR(x)		(((x) & 0x1) << 8)

#define CSITX_TIMING_CTRL		0x0020
#define CSITX_TIMING_VPW_NUM		0x0024
#define CSITX_TIMING_VBP_NUM		0x0028
#define CSITX_TIMING_VFP_NUM		0x002c
#define CSITX_TIMING_HPW_PADDING_NUM	0x0030

#define CSITX_VOP_PATH_CTRL		0x0040
#define m_VOP_PATH_EN			BIT(0)
#define m_VOP_DT_USERDEFINE_EN		BIT(1)
#define m_VOP_VC_USERDEFINE_EN		BIT(2)
#define m_VOP_WC_USERDEFINE_EN		BIT(3)
#define m_PIXEL_FORMAT			GENMASK(7, 4)
#define m_VOP_DT_USERDEFINE		GENMASK(13, 8)
#define m_VOP_VC_USERDEFINE		GENMASK(15, 14)
#define m_VOP_WC_USERDEFINE		GENMASK(31, 16)
#define v_VOP_PATH_EN(x)		(((x) & 0x1) << 0)
#define v_VOP_DT_USERDEFINE_EN(x)	(((x) & 0x1) << 1)
#define v_VOP_VC_USERDEFINE_EN(x)	(((x) & 0x1) << 2)
#define v_VOP_WC_USERDEFINE_EN(x)	(((x) & 0x1) << 3)
#define v_PIXEL_FORMAT(x)		(((x) & 0xf) << 4)
#define v_VOP_DT_USERDEFINE(x)		(((x) & 0x3f) << 8)
#define v_VOP_VC_USERDEFINE(x)		(((x) & 0x3) << 14)
#define v_VOP_WC_USERDEFINE(x)		(((x) & 0xffff) << 16)

#define CSITX_VOP_PATH_PKT_CTRL		0x0050
#define m_VOP_LINE_PADDING_EN		BIT(4)
#define m_VOP_LINE_PADDING_NUM		GENMASK(7, 5)
#define m_VOP_PKT_PADDING_EN		BIT(8)
#define m_VOP_WC_ACTIVE			GENMASK(31, 16)
#define v_VOP_LINE_PADDING_EN(x)	(((x) & 0x1) << 4)
#define v_VOP_LINE_PADDING_NUM(x)	(((x) & 0x7) << 5)
#define v_VOP_PKT_PADDING_EN(x)		(((x) & 0x1) << 8)
#define v_VOP_WC_ACTIVE(x)		(((x) & 0xff) << 16)

#define CSITX_BYPASS_PATH_CTRL		0x0060
#define m_BYPASS_PATH_EN		BIT(0)
#define m_BYPASS_DT_USERDEFINE_EN	BIT(1)
#define m_BYPASS_VC_USERDEFINE_EN	BIT(2)
#define m_BYPASS_WC_USERDEFINE_EN	BIT(3)
#define m_CAM_FORMAT			GENMASK(7, 4)
#define m_BYPASS_DT_USERDEFINE		GENMASK(13, 8)
#define m_BYPASS_VC_USERDEFINE		GENMASK(15, 14)
#define m_BYPASS_WC_USERDEFINE		GENMASK(31, 16)
#define v_BYPASS_PATH_EN(x)		(((x) & 0x1) << 0)
#define v_BYPASS_DT_USERDEFINE_EN(x)	(((x) & 0x1) << 1)
#define v_BYPASS_VC_USERDEFINE_EN(x)	(((x) & 0x1) << 2)
#define v_BYPASS_WC_USERDEFINE_EN(x)	(((x) & 0x1) << 3)
#define v_CAM_FORMAT(x)			(((x) & 0xf) << 4)
#define v_BYPASS_DT_USERDEFINE(x)	(((x) & 0x3f) << 8)
#define v_BYPASS_VC_USERDEFINE(x)	(((x) & 0x3) << 14)
#define v_BYPASS_WC_USERDEFINE(x)	(((x) & 0xff) << 16)

#define CSITX_BYPASS_PATH_PKT_CTRL	0x0064
#define m_BYPASS_LINE_PADDING_EN	BIT(4)
#define m_BYPASS_LINE_PADDING_NUM	GENMASK(7, 5)
#define m_BYPASS_PKT_PADDING_EN		BIT(8)
#define m_BYPASS_WC_ACTIVE		GENMASK(31, 16)
#define v_BYPASS_LINE_PADDING_EN(x)	(((x) & 0x1) << 4)
#define v_BYPASS_LINE_PADDING_NUM(x)	(((x) & 0x7) << 5)
#define v_BYPASS_PKT_PADDING_EN(x)	(((x) & 0x1) << 8)
#define v_BYPASS_WC_ACTIVE(x)		(((x) & 0xff) << 16)

#define CSITX_STATUS0			0x0070
#define CSITX_STATUS1			0x0074
#define m_DPHY_PLL_LOCK			BIT(0)
#define m_STOPSTATE_CLK			BIT(1)
#define m_STOPSTATE_LANE		GENMASK(7, 4)
#define PHY_STOPSTATELANE		(m_STOPSTATE_CLK | m_STOPSTATE_LANE)

#define CSITX_STATUS2			0x0078
#define CSITX_LINE_FLAG_NUM		0x007c
#define CSITX_INTR_EN			0x0080
#define m_INTR_MASK			GENMASK(26, 16)
#define m_FRM_ST_RX			BIT(0 + 16)
#define m_FRM_END_RX			BIT(1 + 16)
#define m_LINE_END_TX			BIT(2 + 16)
#define m_FRM_ST_TX			BIT(3 + 16)
#define m_FRM_END_TX			BIT(4 + 16)
#define m_LINE_END_RX			BIT(5 + 16)
#define m_LINE_FLAG0			BIT(6 + 16)
#define m_LINE_FLAG1			BIT(7 + 16)
#define m_STOP_STATE			BIT(8 + 16)
#define m_PLL_LOCK			BIT(9 + 16)
#define m_CSITX_IDLE			BIT(10 + 16)
#define v_FRM_ST_RX(x)			(((x) & 0x1) << 0)
#define v_FRM_END_RX(x)			(((x) & 0x1) << 1)
#define v_LINE_END_TX(x)		(((x) & 0x1) << 2)
#define v_FRM_ST_TX(x)			(((x) & 0x1) << 3)
#define v_FRM_END_TX(x)			(((x) & 0x1) << 4)
#define v_LINE_END_RX(x)		(((x) & 0x1) << 5)
#define v_LINE_FLAG0(x)			(((x) & 0x1) << 6)
#define v_LINE_FLAG1(x)			(((x) & 0x1) << 7)
#define v_STOP_STATE(x)			(((x) & 0x1) << 8)
#define v_PLL_LOCK(x)			(((x) & 0x1) << 9)
#define v_CSITX_IDLE(x)			(((x) & 0x1) << 10)

#define CSITX_INTR_CLR			0x0084
#define CSITX_INTR_STATUS		0x0088
#define CSITX_INTR_RAW_STATUS		0x008c

#define CSITX_ERR_INTR_EN		0x0090
#define m_ERR_INTR_EN			GENMASK(11, 0)
#define m_ERR_INTR_MASK			GENMASK(27, 16)
#define m_IDI_HDR_FIFO_OVERFLOW		BIT(0 + 16)
#define m_IDI_HDR_FIFO_UNDERFLOW	BIT(1 + 16)
#define m_IDI_PLD_FIFO_OVERFLOW		BIT(2 + 16)
#define m_IDI_PLD_FIFO_UNDERFLOW	BIT(3 + 16)
#define m_HDR_FIFO_OVERFLOW		BIT(4 + 16)
#define m_HDR_FIFO_UNDERFLOW		BIT(5 + 16)
#define m_PLD_FIFO_OVERFLOW		BIT(6 + 16)
#define m_PLD_FIFO_UNDERFLOW		BIT(7 + 16)
#define m_OUTBUFFER_OVERFLOW		BIT(8 + 16)
#define m_OUTBUFFER_UNDERFLOW		BIT(9 + 16)
#define m_TX_TXREADYHS_OVERFLOW		BIT(10 + 16)
#define m_TX_TXREADYHS_UNDERFLOW	BIT(11 + 16)

#define CSITX_ERR_INTR_CLR		0x0094
#define CSITX_ERR_INTR_STATUS		0x0098
#define CSITX_ERR_INTR_RAW_STATUS	0x009c
#define CSITX_ULPS_CTRL			0x00a0
#define CSITX_LPDT_CTRL			0x00a4
#define CSITX_LPDT_DATA			0x00a8
#define CSITX_DPHY_CTRL			0x00b0
#define m_CSITX_ENABLE_PHY		GENMASK(7, 3)
#define v_CSITX_ENABLE_PHY(x)		(((x) & 0x1f) << 3)
#define CSITX_DPHY_PPI_CTRL		0x00b4
#define CSITX_DPHY_TEST_CTRL		0x00b8
#define CSITX_DPHY_ERROR		0x00bc
#define CSITX_DPHY_SCAN_CTRL		0x00c0
#define CSITX_DPHY_SCANIN		0x00c4
#define CSITX_DPHY_SCANOUT		0x00c8
#define CSITX_DPHY_BIST			0x00d0

#define MIPI_CSI_FMT_RAW8		0x10
#define MIPI_CSI_FMT_RAW10		0x11

#define PHY_STATUS_TIMEOUT_US		10000
#define CMD_PKT_STATUS_TIMEOUT_US	20000

#define RK_CSI_TX_MAX_RESET		5

enum soc_type {
	RK1808,
};

enum csi_path_mode {
	VOP_PATH,
	BYPASS_PATH
};

#define GRF_REG_FIELD(reg, lsb, msb)	((reg << 16) | (lsb << 8) | (msb))

enum grf_reg_fields {
	DPIUPDATECFG,
	DPISHUTDN,
	DPICOLORM,
	VOPSEL,
	TURNREQUEST,
	TURNDISABLE,
	FORCETXSTOPMODE,
	FORCERXMODE,
	ENABLE_N,
	MASTERSLAVEZ,
	ENABLECLK,
	BASEDIR,
	DPHY_SEL,
	TXSKEWCALHS,
	MAX_FIELDS,
};

struct rockchip_mipi_csi_plat_data {
	const u32 *csi0_grf_reg_fields;
	const u32 *csi1_grf_reg_fields;
	unsigned long max_bit_rate_per_lane;
	enum soc_type soc_type;
	const char * const *rsts;
	int rsts_num;
};

struct mipi_dphy {
	/* SNPS PHY */
	struct clk *cfg_clk;
	struct clk *ref_clk;
	u16 input_div;
	u16 feedback_div;

	/* Non-SNPS PHY */
	struct phy *phy;
	struct clk *hs_clk;
};

struct rockchip_mipi_csi {
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct device_node *client;
	struct mipi_dsi_host dsi_host;
	struct mipi_dphy dphy;
	struct drm_panel *panel;
	struct device *dev;
	struct regmap *grf;
	struct reset_control *tx_rsts[RK_CSI_TX_MAX_RESET];
	void __iomem *regs;
	void __iomem *test_code_regs;
	struct regmap *regmap;
	u32 *regsbak;
	u32 regs_len;
	struct clk *pclk;
	struct clk *ref_clk;
	int irq;

	unsigned long mode_flags;
	unsigned int lane_mbps; /* per lane */
	u32 channel;
	u32 lanes;
	u32 format;
	struct drm_display_mode mode;
	u32 path_mode; /* vop path or bypass path */
	struct drm_property *csi_tx_path_property;

	const struct rockchip_mipi_csi_plat_data *pdata;
};

enum rockchip_mipi_csi_mode {
	DSI_COMMAND_MODE,
	DSI_VIDEO_MODE,
};

#endif

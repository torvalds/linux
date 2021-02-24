/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright © 2019-2020 Intel Corporation
 */

#ifndef __KMB_DSI_H__
#define __KMB_DSI_H__

#include <drm/drm_encoder.h>
#include <drm/drm_mipi_dsi.h>

/* MIPI TX CFG */
#define MIPI_TX_LANE_DATA_RATE_MBPS 891
#define MIPI_TX_REF_CLK_KHZ         24000
#define MIPI_TX_CFG_CLK_KHZ         24000
#define MIPI_TX_BPP		    24

/* DPHY Tx test codes*/
#define TEST_CODE_FSM_CONTROL				0x03
#define TEST_CODE_MULTIPLE_PHY_CTRL			0x0C
#define TEST_CODE_PLL_PROPORTIONAL_CHARGE_PUMP_CTRL	0x0E
#define TEST_CODE_PLL_INTEGRAL_CHARGE_PUMP_CTRL		0x0F
#define TEST_CODE_PLL_VCO_CTRL				0x12
#define TEST_CODE_PLL_GMP_CTRL				0x13
#define TEST_CODE_PLL_PHASE_ERR_CTRL			0x14
#define TEST_CODE_PLL_LOCK_FILTER			0x15
#define TEST_CODE_PLL_UNLOCK_FILTER			0x16
#define TEST_CODE_PLL_INPUT_DIVIDER			0x17
#define TEST_CODE_PLL_FEEDBACK_DIVIDER			0x18
#define   PLL_FEEDBACK_DIVIDER_HIGH			BIT(7)
#define TEST_CODE_PLL_OUTPUT_CLK_SEL			0x19
#define   PLL_N_OVR_EN					BIT(4)
#define   PLL_M_OVR_EN					BIT(5)
#define TEST_CODE_VOD_LEVEL				0x24
#define TEST_CODE_PLL_CHARGE_PUMP_BIAS			0x1C
#define TEST_CODE_PLL_LOCK_DETECTOR			0x1D
#define TEST_CODE_HS_FREQ_RANGE_CFG			0x44
#define TEST_CODE_PLL_ANALOG_PROG			0x1F
#define TEST_CODE_SLEW_RATE_OVERRIDE_CTRL		0xA0
#define TEST_CODE_SLEW_RATE_DDL_LOOP_CTRL		0xA3
#define TEST_CODE_SLEW_RATE_DDL_CYCLES			0xA4

/* DPHY params */
#define PLL_N_MIN	0
#define PLL_N_MAX	15
#define PLL_M_MIN	62
#define PLL_M_MAX	623
#define PLL_FVCO_MAX	1250

#define TIMEOUT		600

#define MIPI_TX_FRAME_GEN				4
#define MIPI_TX_FRAME_GEN_SECTIONS			4
#define MIPI_CTRL_VIRTUAL_CHANNELS			4
#define MIPI_D_LANES_PER_DPHY				2
#define MIPI_CTRL_2LANE_MAX_MC_FIFO_LOC			255
#define MIPI_CTRL_4LANE_MAX_MC_FIFO_LOC			511
/* 2 Data Lanes per D-PHY */
#define MIPI_DPHY_D_LANES				2
#define MIPI_DPHY_DEFAULT_BIT_RATES			63

#define KMB_MIPI_DEFAULT_CLK				24000000
#define KMB_MIPI_DEFAULT_CFG_CLK			24000000

#define to_kmb_dsi(x) container_of(x, struct kmb_dsi, base)

struct kmb_dsi {
	struct drm_encoder base;
	struct device *dev;
	struct platform_device *pdev;
	struct mipi_dsi_host *host;
	struct mipi_dsi_device *device;
	struct drm_bridge *adv_bridge;
	void __iomem *mipi_mmio;
	struct clk *clk_mipi;
	struct clk *clk_mipi_ecfg;
	struct clk *clk_mipi_cfg;
	int sys_clk_mhz;
};

/* DPHY Tx test codes */

enum mipi_ctrl_num {
	MIPI_CTRL0 = 0,
	MIPI_CTRL1,
	MIPI_CTRL2,
	MIPI_CTRL3,
	MIPI_CTRL4,
	MIPI_CTRL5,
	MIPI_CTRL6,
	MIPI_CTRL7,
	MIPI_CTRL8,
	MIPI_CTRL9,
	MIPI_CTRL_NA
};

enum mipi_dphy_num {
	MIPI_DPHY0 = 0,
	MIPI_DPHY1,
	MIPI_DPHY2,
	MIPI_DPHY3,
	MIPI_DPHY4,
	MIPI_DPHY5,
	MIPI_DPHY6,
	MIPI_DPHY7,
	MIPI_DPHY8,
	MIPI_DPHY9,
	MIPI_DPHY_NA
};

enum mipi_dir {
	MIPI_RX,
	MIPI_TX
};

enum mipi_ctrl_type {
	MIPI_DSI,
	MIPI_CSI
};

enum mipi_data_if {
	MIPI_IF_DMA,
	MIPI_IF_PARALLEL
};

enum mipi_data_mode {
	MIPI_DATA_MODE0,
	MIPI_DATA_MODE1,
	MIPI_DATA_MODE2,
	MIPI_DATA_MODE3
};

enum mipi_dsi_video_mode {
	DSI_VIDEO_MODE_NO_BURST_PULSE,
	DSI_VIDEO_MODE_NO_BURST_EVENT,
	DSI_VIDEO_MODE_BURST
};

enum mipi_dsi_blanking_mode {
	TRANSITION_TO_LOW_POWER,
	SEND_BLANK_PACKET
};

enum mipi_dsi_eotp {
	DSI_EOTP_DISABLED,
	DSI_EOTP_ENABLES
};

enum mipi_dsi_data_type {
	DSI_SP_DT_RESERVED_00 = 0x00,
	DSI_SP_DT_VSYNC_START = 0x01,
	DSI_SP_DT_COLOR_MODE_OFF = 0x02,
	DSI_SP_DT_GENERIC_SHORT_WR = 0x03,
	DSI_SP_DT_GENERIC_RD = 0x04,
	DSI_SP_DT_DCS_SHORT_WR = 0x05,
	DSI_SP_DT_DCS_RD = 0x06,
	DSI_SP_DT_EOTP = 0x08,
	DSI_LP_DT_NULL = 0x09,
	DSI_LP_DT_RESERVED_0A = 0x0a,
	DSI_LP_DT_RESERVED_0B = 0x0b,
	DSI_LP_DT_LPPS_YCBCR422_20B = 0x0c,
	DSI_LP_DT_PPS_RGB101010_30B = 0x0d,
	DSI_LP_DT_PPS_RGB565_16B = 0x0e,
	DSI_LP_DT_RESERVED_0F = 0x0f,

	DSI_SP_DT_RESERVED_10 = 0x10,
	DSI_SP_DT_VSYNC_END = 0x11,
	DSI_SP_DT_COLOR_MODE_ON = 0x12,
	DSI_SP_DT_GENERIC_SHORT_WR_1PAR = 0x13,
	DSI_SP_DT_GENERIC_RD_1PAR = 0x14,
	DSI_SP_DT_DCS_SHORT_WR_1PAR = 0x15,
	DSI_SP_DT_RESERVED_16 = 0x16,
	DSI_SP_DT_RESERVED_17 = 0x17,
	DSI_SP_DT_RESERVED_18 = 0x18,
	DSI_LP_DT_BLANK = 0x19,
	DSI_LP_DT_RESERVED_1A = 0x1a,
	DSI_LP_DT_RESERVED_1B = 0x1b,
	DSI_LP_DT_PPS_YCBCR422_24B = 0x1c,
	DSI_LP_DT_PPS_RGB121212_36B = 0x1d,
	DSI_LP_DT_PPS_RGB666_18B = 0x1e,
	DSI_LP_DT_RESERVED_1F = 0x1f,

	DSI_SP_DT_RESERVED_20 = 0x20,
	DSI_SP_DT_HSYNC_START = 0x21,
	DSI_SP_DT_SHUT_DOWN_PERIPH_CMD = 0x22,
	DSI_SP_DT_GENERIC_SHORT_WR_2PAR = 0x23,
	DSI_SP_DT_GENERIC_RD_2PAR = 0x24,
	DSI_SP_DT_RESERVED_25 = 0x25,
	DSI_SP_DT_RESERVED_26 = 0x26,
	DSI_SP_DT_RESERVED_27 = 0x27,
	DSI_SP_DT_RESERVED_28 = 0x28,
	DSI_LP_DT_GENERIC_LONG_WR = 0x29,
	DSI_LP_DT_RESERVED_2A = 0x2a,
	DSI_LP_DT_RESERVED_2B = 0x2b,
	DSI_LP_DT_PPS_YCBCR422_16B = 0x2c,
	DSI_LP_DT_RESERVED_2D = 0x2d,
	DSI_LP_DT_LPPS_RGB666_18B = 0x2e,
	DSI_LP_DT_RESERVED_2F = 0x2f,

	DSI_SP_DT_RESERVED_30 = 0x30,
	DSI_SP_DT_HSYNC_END = 0x31,
	DSI_SP_DT_TURN_ON_PERIPH_CMD = 0x32,
	DSI_SP_DT_RESERVED_33 = 0x33,
	DSI_SP_DT_RESERVED_34 = 0x34,
	DSI_SP_DT_RESERVED_35 = 0x35,
	DSI_SP_DT_RESERVED_36 = 0x36,
	DSI_SP_DT_SET_MAX_RETURN_PKT_SIZE = 0x37,
	DSI_SP_DT_RESERVED_38 = 0x38,
	DSI_LP_DT_DSC_LONG_WR = 0x39,
	DSI_LP_DT_RESERVED_3A = 0x3a,
	DSI_LP_DT_RESERVED_3B = 0x3b,
	DSI_LP_DT_RESERVED_3C = 0x3c,
	DSI_LP_DT_PPS_YCBCR420_12B = 0x3d,
	DSI_LP_DT_PPS_RGB888_24B = 0x3e,
	DSI_LP_DT_RESERVED_3F = 0x3f
};

enum mipi_tx_hs_tp_sel {
	MIPI_TX_HS_TP_WHOLE_FRAME_COLOR0 = 0,
	MIPI_TX_HS_TP_WHOLE_FRAME_COLOR1,
	MIPI_TX_HS_TP_V_STRIPES,
	MIPI_TX_HS_TP_H_STRIPES,
};

enum dphy_mode {
	MIPI_DPHY_SLAVE = 0,
	MIPI_DPHY_MASTER
};

enum dphy_tx_fsm {
	DPHY_TX_POWERDWN = 0,
	DPHY_TX_BGPON,
	DPHY_TX_TERMCAL,
	DPHY_TX_TERMCALUP,
	DPHY_TX_OFFSETCAL,
	DPHY_TX_LOCK,
	DPHY_TX_SRCAL,
	DPHY_TX_IDLE,
	DPHY_TX_ULP,
	DPHY_TX_LANESTART,
	DPHY_TX_CLKALIGN,
	DPHY_TX_DDLTUNNING,
	DPHY_TX_ULP_FORCE_PLL,
	DPHY_TX_LOCK_LOSS
};

struct mipi_data_type_params {
	u8 size_constraint_pixels;
	u8 size_constraint_bytes;
	u8 pixels_per_pclk;
	u8 bits_per_pclk;
};

struct mipi_tx_dsi_cfg {
	u8 hfp_blank_en;	/* Horizontal front porch blanking enable */
	u8 eotp_en;		/* End of transmission packet enable */
	/* Last vertical front porch blanking mode */
	u8 lpm_last_vfp_line;
	/* First vertical sync active blanking mode */
	u8 lpm_first_vsa_line;
	u8 sync_pulse_eventn;	/* Sync type */
	u8 hfp_blanking;	/* Horizontal front porch blanking mode */
	u8 hbp_blanking;	/* Horizontal back porch blanking mode */
	u8 hsa_blanking;	/* Horizontal sync active blanking mode */
	u8 v_blanking;		/* Vertical timing blanking mode */
};

struct mipi_tx_frame_section_cfg {
	u32 dma_v_stride;
	u16 dma_v_scale_cfg;
	u16 width_pixels;
	u16 height_lines;
	u8 dma_packed;
	u8 bpp;
	u8 bpp_unpacked;
	u8 dma_h_stride;
	u8 data_type;
	u8 data_mode;
	u8 dma_flip_rotate_sel;
};

struct mipi_tx_frame_timing_cfg {
	u32 bpp;
	u32 lane_rate_mbps;
	u32 hsync_width;
	u32 h_backporch;
	u32 h_frontporch;
	u32 h_active;
	u16 vsync_width;
	u16 v_backporch;
	u16 v_frontporch;
	u16 v_active;
	u8 active_lanes;
};

struct mipi_tx_frame_sect_phcfg {
	u32 wc;
	enum mipi_data_mode data_mode;
	enum mipi_dsi_data_type data_type;
	u8 vchannel;
	u8 dma_packed;
};

struct mipi_tx_frame_cfg {
	struct mipi_tx_frame_section_cfg *sections[MIPI_TX_FRAME_GEN_SECTIONS];
	u32 hsync_width;	/* in pixels */
	u32 h_backporch;	/* in pixels */
	u32 h_frontporch;	/* in pixels */
	u16 vsync_width;	/* in lines */
	u16 v_backporch;	/* in lines */
	u16 v_frontporch;	/* in lines */
};

struct mipi_tx_ctrl_cfg {
	struct mipi_tx_frame_cfg *frames[MIPI_TX_FRAME_GEN];
	const struct mipi_tx_dsi_cfg *tx_dsi_cfg;
	u8 line_sync_pkt_en;
	u8 line_counter_active;
	u8 frame_counter_active;
	u8 tx_hsclkkidle_cnt;
	u8 tx_hsexit_cnt;
	u8 tx_crc_en;
	u8 tx_hact_wait_stop;
	u8 tx_always_use_hact;
	u8 tx_wait_trig;
	u8 tx_wait_all_sect;
};

/* configuration structure for MIPI control */
struct mipi_ctrl_cfg {
	u8 active_lanes;	/* # active lanes per controller 2/4 */
	u32 lane_rate_mbps;	/* MBPS */
	u32 ref_clk_khz;
	u32 cfg_clk_khz;
	struct mipi_tx_ctrl_cfg tx_ctrl_cfg;
};

static inline void kmb_write_mipi(struct kmb_dsi *kmb_dsi,
				  unsigned int reg, u32 value)
{
	writel(value, (kmb_dsi->mipi_mmio + reg));
}

static inline u32 kmb_read_mipi(struct kmb_dsi *kmb_dsi, unsigned int reg)
{
	return readl(kmb_dsi->mipi_mmio + reg);
}

static inline void kmb_write_bits_mipi(struct kmb_dsi *kmb_dsi,
				       unsigned int reg, u32 offset,
				       u32 num_bits, u32 value)
{
	u32 reg_val = kmb_read_mipi(kmb_dsi, reg);
	u32 mask = (1 << num_bits) - 1;

	value &= mask;
	mask <<= offset;
	reg_val &= (~mask);
	reg_val |= (value << offset);
	kmb_write_mipi(kmb_dsi, reg, reg_val);
}

static inline void kmb_set_bit_mipi(struct kmb_dsi *kmb_dsi,
				    unsigned int reg, u32 offset)
{
	u32 reg_val = kmb_read_mipi(kmb_dsi, reg);

	kmb_write_mipi(kmb_dsi, reg, reg_val | (1 << offset));
}

static inline void kmb_clr_bit_mipi(struct kmb_dsi *kmb_dsi,
				    unsigned int reg, u32 offset)
{
	u32 reg_val = kmb_read_mipi(kmb_dsi, reg);

	kmb_write_mipi(kmb_dsi, reg, reg_val & (~(1 << offset)));
}

int kmb_dsi_host_bridge_init(struct device *dev);
struct kmb_dsi *kmb_dsi_init(struct platform_device *pdev);
void kmb_dsi_host_unregister(struct kmb_dsi *kmb_dsi);
int kmb_dsi_mode_set(struct kmb_dsi *kmb_dsi, struct drm_display_mode *mode,
		     int sys_clk_mhz);
int kmb_dsi_map_mmio(struct kmb_dsi *kmb_dsi);
int kmb_dsi_clk_init(struct kmb_dsi *kmb_dsi);
int kmb_dsi_encoder_init(struct drm_device *dev, struct kmb_dsi *kmb_dsi);
#endif /* __KMB_DSI_H__ */

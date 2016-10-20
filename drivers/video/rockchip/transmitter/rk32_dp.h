#ifndef __RK32_DP_H
#define __RK32_DP_H

#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/rk_fb.h>

#include "dpcd_edid.h"

#define DP_VERSION				0x10

#define TX_SW_RST				0x14

#define FUNC_EN_1				0x18
#define VID_CAP_FUNC_EN_N			(0x1 << 6)
#define VID_FIFO_FUNC_EN_N			(0x1 << 5)
#define AUD_FIFO_FUNC_EN_N			(0x1 << 4)
#define AUD_FUNC_EN_N				(0x1 << 3)
#define HDCP_FUNC_EN_N				(0x1 << 2)
#define SW_FUNC_EN_N				(0x1 << 0)

#define FUNC_EN_2				0x1C
#define SSC_FUNC_EN_N				(0x1 << 7)
#define AUX_FUNC_EN_N				(0x1 << 2)
#define SERDES_FIFO_FUNC_EN_N			(0x1 << 1)
#define LS_CLK_DOMAIN_FUNC_EN_N			(0x1 << 0)

#define VIDEO_CTL_1				0x20
#define VIDEO_EN				(0x1 << 7)
#define VIDEO_MUTE				(0x1 << 6)

#define VIDEO_CTL_2				0x24
#define IN_D_RANGE_MASK				(0x1 << 7)
#define IN_D_RANGE_SHIFT			(7)
#define IN_D_RANGE_CEA				(0x1 << 7)
#define IN_D_RANGE_VESA				(0x0 << 7)
#define IN_BPC_MASK				(0x7 << 4)
#define IN_BPC_SHIFT				(4)
#define IN_BPC_12_BITS				(0x3 << 4)
#define IN_BPC_10_BITS				(0x2 << 4)
#define IN_BPC_8_BITS				(0x1 << 4)
#define IN_BPC_6_BITS				(0x0 << 4)
#define IN_COLOR_F_MASK				(0x3 << 0)
#define IN_COLOR_F_SHIFT			(0)
#define IN_COLOR_F_YCBCR444			(0x2 << 0)
#define IN_COLOR_F_YCBCR422			(0x1 << 0)
#define IN_COLOR_F_RGB				(0x0 << 0)

#define VIDEO_CTL_3				0x28
#define IN_YC_COEFFI_MASK			(0x1 << 7)
#define IN_YC_COEFFI_SHIFT			(7)
#define IN_YC_COEFFI_ITU709			(0x1 << 7)
#define IN_YC_COEFFI_ITU601			(0x0 << 7)
#define VID_CHK_UPDATE_TYPE_MASK		(0x1 << 4)
#define VID_CHK_UPDATE_TYPE_SHIFT		(4)
#define VID_CHK_UPDATE_TYPE_1			(0x1 << 4)
#define VID_CHK_UPDATE_TYPE_0			(0x0 << 4)

#define VIDEO_CTL_4				0x2c
#define BIST_EN					(0x1 << 3)
#define BIST_WH_64				(0x1 << 2)
#define BIST_WH_32				(0x0 << 2)
#define BIST_TYPE_COLR_BAR			(0x0 << 0)
#define BIST_TYPE_GRAY_BAR			(0x1 << 0)
#define BIST_TYPE_MOBILE_BAR			(0x2 << 0)

#define VIDEO_CTL_8				0x3C
#define VID_HRES_TH(x)				(((x) & 0xf) << 4)
#define VID_VRES_TH(x)				(((x) & 0xf) << 0)

#define VIDEO_CTL_10				0x44
#define F_SEL					(0x1 << 4)
#define INTERACE_SCAN_CFG			(0x1 << 2)
#define VSYNC_POLARITY_CFG			(0x1 << 1)
#define HSYNC_POLARITY_CFG			(0x1 << 0)

#define TOTAL_LINE_CFG_L			0x48
#define TOTAL_LINE_CFG_H			0x4c
#define ATV_LINE_CFG_L				0x50
#define ATV_LINE_CFG_H				0x54
#define VF_PORCH_REG				0x58
#define VSYNC_CFG_REG				0x5c
#define VB_PORCH_REG				0x60
#define TOTAL_PIXELL_REG			0x64
#define TOTAL_PIXELH_REG			0x68
#define ATV_PIXELL_REG				0x6c
#define ATV_PIXELH_REG				0x70
#define HF_PORCHL_REG				0x74
#define HF_PORCHH_REG				0x78
#define HSYNC_CFGL_REG				0x7c
#define HSYNC_CFGH_REG				0x80
#define HB_PORCHL_REG				0x84
#define HB_PORCHH_REG				0x88


#define SSC_REG					0x104
#define TX_REG_COMMON				0x114
#define DP_AUX					0x120
#define DP_BIAS					0x124

#define PLL_REG_1				0xfc
#define REF_CLK_24M				(0x1 << 1)
#define REF_CLK_27M				(0x0 << 1)

#define PLL_REG_2				0x9e4
#define PLL_REG_3				0x9e8
#define PLL_REG_4				0x9ec
#define PLL_REG_5				0xa00
#define DP_PWRDN				0x12c
#define PD_INC_BG				(0x1 << 7)
#define PD_EXP_BG				(0x1 << 6)
#define PD_AUX					(0x1 << 5)
#define PD_PLL					(0x1 << 4)
#define PD_CH3					(0x1 << 3)
#define PD_CH2					(0x1 << 2)
#define PD_CH1					(0x1 << 1)
#define PD_CH0					(0x1 << 0)

#define DP_RESERVE2				0x134

#define LANE_MAP				0x35C
#define LANE3_MAP_LOGIC_LANE_0			(0x0 << 6)
#define LANE3_MAP_LOGIC_LANE_1			(0x1 << 6)
#define LANE3_MAP_LOGIC_LANE_2			(0x2 << 6)
#define LANE3_MAP_LOGIC_LANE_3			(0x3 << 6)
#define LANE2_MAP_LOGIC_LANE_0			(0x0 << 4)
#define LANE2_MAP_LOGIC_LANE_1			(0x1 << 4)
#define LANE2_MAP_LOGIC_LANE_2			(0x2 << 4)
#define LANE2_MAP_LOGIC_LANE_3			(0x3 << 4)
#define LANE1_MAP_LOGIC_LANE_0			(0x0 << 2)
#define LANE1_MAP_LOGIC_LANE_1			(0x1 << 2)
#define LANE1_MAP_LOGIC_LANE_2			(0x2 << 2)
#define LANE1_MAP_LOGIC_LANE_3			(0x3 << 2)
#define LANE0_MAP_LOGIC_LANE_0			(0x0 << 0)
#define LANE0_MAP_LOGIC_LANE_1			(0x1 << 0)
#define LANE0_MAP_LOGIC_LANE_2			(0x2 << 0)
#define LANE0_MAP_LOGIC_LANE_3			(0x3 << 0)

#define ANALOG_CTL_2				0x374
#define SEL_24M					(0x1 << 3)

/*#define ANALOG_CTL_3				0x378
#define PLL_FILTER_CTL_1			0x37C
#define TX_AMP_TUNING_CTL			0x380*/

#define AUX_HW_RETRY_CTL			0x390

#define INT_STA					0x3c0

#define COMMON_INT_STA_1			0x3C4
#define VSYNC_DET				(0x1 << 7)
#define PLL_LOCK_CHG				(0x1 << 6)
#define SPDIF_ERR				(0x1 << 5)
#define SPDIF_UNSTBL				(0x1 << 4)
#define VID_FORMAT_CHG				(0x1 << 3)
#define AUD_CLK_CHG				(0x1 << 2)
#define VID_CLK_CHG				(0x1 << 1)
#define SW_INT					(0x1 << 0)

#define COMMON_INT_STA_2			0x3C8
#define ENC_EN_CHG				(0x1 << 6)
#define HW_BKSV_RDY				(0x1 << 3)
#define HW_SHA_DONE				(0x1 << 2)
#define HW_AUTH_STATE_CHG			(0x1 << 1)
#define HW_AUTH_DONE				(0x1 << 0)

#define COMMON_INT_STA_3			0x3CC
#define AFIFO_UNDER				(0x1 << 7)
#define AFIFO_OVER				(0x1 << 6)
#define R0_CHK_FLAG				(0x1 << 5)

#define COMMON_INT_STA_4			0x3D0
#define PSR_ACTIVE				(0x1 << 7)
#define PSR_INACTIVE				(0x1 << 6)
#define SPDIF_BI_PHASE_ERR			(0x1 << 5)
#define HOTPLUG_CHG				(0x1 << 2)
#define HPD_LOST				(0x1 << 1)
#define PLUG					(0x1 << 0)

#define DP_INT_STA				0x3DC
#define INT_HPD					(0x1 << 6)
#define HW_LT_DONE				(0x1 << 5)
#define SINK_LOST				(0x1 << 3)
#define LINK_LOST				(0x1 << 2)
#define RPLY_RECEIV				(0x1 << 1)
#define AUX_ERR					(0x1 << 0)

#define COMMON_INT_MASK_1			0x3E0
#define COMMON_INT_MASK_2			0x3E4
#define COMMON_INT_MASK_3			0x3E8
#define COMMON_INT_MASK_4			0x3EC
#define DP_INT_STA_MASK				0x3F8

#define INT_CTL					0x3FC
#define SOFT_INT_CTRL				(0x1 << 2)
#define INT_POL					(0x1 << 0)

#define SYS_CTL_1				0x600
#define DET_STA					(0x1 << 2)
#define FORCE_DET				(0x1 << 1)
#define DET_CTRL				(0x1 << 0)

#define SYS_CTL_2				0x604
#define CHA_CRI(x)				(((x) & 0xf) << 4)
#define CHA_STA					(0x1 << 2)
#define FORCE_CHA				(0x1 << 1)
#define CHA_CTRL				(0x1 << 0)

#define SYS_CTL_3				0x608
#define HPD_STATUS				(0x1 << 6)
#define F_HPD					(0x1 << 5)
#define HPD_CTRL				(0x1 << 4)
#define HDCP_RDY				(0x1 << 3)
#define STRM_VALID				(0x1 << 2)
#define F_VALID					(0x1 << 1)
#define VALID_CTRL				(0x1 << 0)

#define SYS_CTL_4				0x60C
#define FIX_M_AUD				(0x1 << 4)
#define ENHANCED				(0x1 << 3)
#define FIX_M_VID				(0x1 << 2)
#define M_VID_UPDATE_CTRL			(0x3 << 0)


#define PKT_SEND_CTL				0x640
#define HDCP_CTL				0x648

#define LINK_BW_SET				0x680
#define LANE_CNT_SET				0x684

#define TRAINING_PTN_SET			0x688
#define SCRAMBLING_DISABLE			(0x1 << 5)
#define SCRAMBLING_ENABLE			(0x0 << 5)
#define LINK_QUAL_PATTERN_SET_MASK		(0x7 << 2)
#define LINK_QUAL_PATTERN_SET_HBR2		(0x5 << 2)
#define LINK_QUAL_PATTERN_SET_80BIT		(0x4 << 2)
#define LINK_QUAL_PATTERN_SET_PRBS7		(0x3 << 2)
#define LINK_QUAL_PATTERN_SET_D10_2		(0x1 << 2)
#define LINK_QUAL_PATTERN_SET_DISABLE		(0x0 << 2)
#define SW_TRAINING_PATTERN_SET_MASK		(0x3 << 0)
#define SW_TRAINING_PATTERN_SET_PTN2		(0x2 << 0)
#define SW_TRAINING_PATTERN_SET_PTN1		(0x1 << 0)
#define SW_TRAINING_PATTERN_SET_DISABLE		(0x0 << 0)

#define LN0_LINK_TRAINING_CTL			0x68C
#define LN1_LINK_TRAINING_CTL			0x690
#define LN2_LINK_TRAINING_CTL			0x694
#define LN3_LINK_TRAINING_CTL			0x698

#define HW_LT_CTL				0x6a0
#define HW_LT_ERR_CODE_MASK			0x70
#define HW_LT_EN				(0x1 << 0)

#define DEBUG_CTL				0x6C0
#define PLL_LOCK				(0x1 << 4)
#define F_PLL_LOCK				(0x1 << 3)
#define PLL_LOCK_CTRL				(0x1 << 2)
#define POLL_EN					(0x1 << 1)
#define PN_INV					(0x1 << 0)

#define HPD_DEGLITCH_L				0x6C4
#define HPD_DEGLITCH_H				0x6C8
#define LINK_DEBUG_CTL				0x6E0

#define M_VID_0					0x700
#define M_VID_1					0x704
#define M_VID_2					0x708
#define N_VID_0					0x70C
#define N_VID_1					0x710
#define N_VID_2					0x714

#define VIDEO_FIFO_THRD				0x730
#define AUDIO_MARGIN				0x73C

#define M_VID_GEN_FILTER_TH			0x764
#define M_AUD_GEN_FILTER_TH			0x778

#define AUX_CH_STA				0x780
#define AUX_BUSY				(0x1 << 4)
#define AUX_STATUS_MASK				(0xf << 0)

#define AUX_CH_DEFER_CTL			0x788
#define DEFER_CTRL_EN				(0x1 << 7)
#define DEFER_COUNT(x)				(((x) & 0x7f) << 0)

#define AUX_RX_COMM				0x78C
#define BUFFER_DATA_CTL				0x790
#define BUF_CLR					(0x1 << 7)
#define BUF_HAVE_DATA				(0x1 << 4)
#define BUF_DATA_COUNT(x)			(((x) & 0xf) << 0)

#define AUX_CH_CTL_1				0x794
#define AUX_LENGTH(x)				(((x - 1) & 0xf) << 4)
#define AUX_TX_COMM_MASK			(0xf << 0)
#define AUX_TX_COMM_DP_TRANSACTION		(0x1 << 3)
#define AUX_TX_COMM_I2C_TRANSACTION		(0x0 << 3)
#define AUX_TX_COMM_MOT				(0x1 << 2)
#define AUX_TX_COMM_WRITE			(0x0 << 0)
#define AUX_TX_COMM_READ			(0x1 << 0)

#define DP_AUX_ADDR_7_0				0x798
#define DP_AUX_ADDR_15_8			0x79C
#define DP_AUX_ADDR_19_16			0x7A0

#define AUX_CH_CTL_2				0x7A4
#define PD_AUX_IDLE				(0x1 << 3)
#define ADDR_ONLY				(0x1 << 1)
#define AUX_EN					(0x1 << 0)

#define BUF_DATA_0				0x7C0

#define SOC_GENERAL_CTL				0x800

/* TX_SW_RESET */
#define RST_DP_TX				(0x1 << 0)

/* ANALOG_CTL_1 */
#define TX_TERMINAL_CTRL_50_OHM			(0x1 << 4)



/* ANALOG_CTL_3 */
#define DRIVE_DVDD_BIT_1_0625V			(0x4 << 5)
#define VCO_BIT_600_MICRO			(0x5 << 0)

/* PLL_FILTER_CTL_1 */
#define PD_RING_OSC				(0x1 << 6)
#define AUX_TERMINAL_CTRL_37_5_OHM		(0x0 << 4)
#define AUX_TERMINAL_CTRL_45_OHM		(0x1 << 4)
#define AUX_TERMINAL_CTRL_50_OHM		(0x2 << 4)
#define AUX_TERMINAL_CTRL_65_OHM		(0x3 << 4)
#define TX_CUR1_2X				(0x1 << 2)
#define TX_CUR_16_MA				(0x3 << 0)

/* TX_AMP_TUNING_CTL */
#define CH3_AMP_SHIFT				(24)
#define CH3_AMP_400_MV				(0x0 << 24)
#define CH2_AMP_SHIFT				(16)
#define CH2_AMP_400_MV				(0x0 << 16)
#define CH1_AMP_SHIFT				(8)
#define CH1_AMP_400_MV				(0x0 << 8)
#define CH0_AMP_SHIFT				(0)
#define CH0_AMP_400_MV				(0x0 << 0)

/* AUX_HW_RETRY_CTL */
#define AUX_BIT_PERIOD_EXPECTED_DELAY(x)	(((x) & 0x7) << 8)
#define AUX_HW_RETRY_INTERVAL_MASK		(0x3 << 3)
#define AUX_HW_RETRY_INTERVAL_600_MICROSECONDS	(0x0 << 3)
#define AUX_HW_RETRY_INTERVAL_800_MICROSECONDS	(0x1 << 3)
#define AUX_HW_RETRY_INTERVAL_1000_MICROSECONDS	(0x2 << 3)
#define AUX_HW_RETRY_INTERVAL_1800_MICROSECONDS	(0x3 << 3)
#define AUX_HW_RETRY_COUNT_SEL(x)		(((x) & 0x7) << 0)



/* LN0_LINK_TRAINING_CTL */
#define PRE_EMPHASIS_SET_MASK			(0x3 << 3)
#define PRE_EMPHASIS_SET_SHIFT			(3)


/* PLL_CTL */
#define DP_PLL_PD				(0x1 << 7)
#define DP_PLL_RESET				(0x1 << 6)
#define DP_PLL_LOOP_BIT_DEFAULT			(0x1 << 4)
#define DP_PLL_REF_BIT_1_1250V			(0x5 << 0)
#define DP_PLL_REF_BIT_1_2500V			(0x7 << 0)

/* PHY_TEST */
#define MACRO_RST				(0x1 << 5)
#define CH1_TEST				(0x1 << 1)
#define CH0_TEST				(0x1 << 0)







#define DP_TIMEOUT_LOOP_CNT 100
#define MAX_CR_LOOP 5
#define MAX_EQ_LOOP 5



#define GRF_EDP_REF_CLK_SEL_INTER		(1 << 4)
#define GRF_EDP_HDCP_EN				(1 << 15)
#define GRF_EDP_BIST_EN				(1 << 14)
#define GRF_EDP_MEM_CTL_BY_EDP			(1 << 13)
#define GRF_EDP_SECURE_EN			(1 << 3)
#define EDP_SEL_VOP_LIT				(1 << 5)

/* PSR */
#define PANEL_SELF_REFRESH_CAPABILITY_SUPPORTED_AND_VERSION 0x70
#define PANEL_SELF_REFRESH_CAPABILITIES 0x71
#define PSR_SUPPORT 0x1
#define PSR_ENABLE 0x170
#define SUORPSR_EVENT_STATUS_INDICATOR 0x2007
#define SINK_DEVICE_PANEL_SELF_REFRESH_STATUS 0x2008
#define LAST_RECEIVED_PSR_SDP 0x200a
#define DEFINITION_WITHIN_LINKORSINK_DEVICE_POWER_CONTROL_FIELD 0x600

#define HB0 0x02F8
#define HB1 0x02FC
#define HB2 0x0300
#define HB3 0x0304
#define PB0 0x0308
#define PB1 0x030C
#define PB2 0x0310
#define PB3 0x0314
#define DB0 0x0254
#define DB1 0x0258
#define DB2 0x025C
#define DB3 0x0260
#define DB4 0x0264
#define DB5 0x0268
#define DB6 0x026c
#define DB7 0x0270
#define DP_PD 0x012C
#define IF_TYPE 0x0244
#define VSC_SHADOW_DB1 0x0320
#define PSR_FRAME_UPDATA_CTRL 0x0318
#define SPDIF_AUDIO_CTL_0 0x00D8
/* PSR END */

enum dp_irq_type {
	DP_IRQ_TYPE_HP_CABLE_IN,
	DP_IRQ_TYPE_HP_CABLE_OUT,
	DP_IRQ_TYPE_HP_CHANGE,
	DP_IRQ_TYPE_UNKNOWN,
};

enum color_coefficient {
	COLOR_YCBCR601,
	COLOR_YCBCR709
};

enum dynamic_range {
	VESA,
	CEA
};

enum pll_status {
	DP_PLL_UNLOCKED,
	DP_PLL_LOCKED
};

enum clock_recovery_m_value_type {
	CALCULATED_M,
	REGISTER_M
};

enum video_timing_recognition_type {
	VIDEO_TIMING_FROM_CAPTURE,
	VIDEO_TIMING_FROM_REGISTER
};

enum pattern_set {
	PRBS7,
	D10_2,
	TRAINING_PTN1,
	TRAINING_PTN2,
	DP_NONE
};

enum color_space {
	CS_RGB,
	CS_YCBCR422,
	CS_YCBCR444
};

enum color_depth {
	COLOR_6,
	COLOR_8,
	COLOR_10,
	COLOR_12
};

enum link_rate_type {
	LINK_RATE_1_62GBPS = 0x06,
	LINK_RATE_2_70GBPS = 0x0a
};

enum link_lane_count_type {
	LANE_CNT1 = 1,
	LANE_CNT2 = 2,
	LANE_CNT4 = 4
};

enum link_training_state {
	LT_START,
	LT_CLK_RECOVERY,
	LT_EQ_TRAINING,
	FINISHED,
	FAILED
};

enum voltage_swing_level {
	VOLTAGE_LEVEL_0,
	VOLTAGE_LEVEL_1,
	VOLTAGE_LEVEL_2,
	VOLTAGE_LEVEL_3,
};

enum pre_emphasis_level {
	PRE_EMPHASIS_LEVEL_0,
	PRE_EMPHASIS_LEVEL_1,
	PRE_EMPHASIS_LEVEL_2,
	PRE_EMPHASIS_LEVEL_3,
};

enum analog_power_block {
	AUX_BLOCK,
	CH0_BLOCK,
	CH1_BLOCK,
	CH2_BLOCK,
	CH3_BLOCK,
	ANALOG_TOTAL,
	POWER_ALL
};

struct video_info {
	char *name;

	bool h_sync_polarity;
	bool v_sync_polarity;
	bool interlaced;

	enum color_space color_space;
	enum dynamic_range dynamic_range;
	enum color_coefficient ycbcr_coeff;
	enum color_depth color_depth;

	enum link_rate_type link_rate;
	enum link_lane_count_type lane_count;
};

struct link_train {
	int eq_loop;
	int cr_loop[4];

	u8 link_rate;
	u8 lane_count;
	u8 training_lane[4];

	enum link_training_state lt_state;
};

enum {
	SOC_COMMON = 0,
	SOC_RK3399
};

struct rk32_edp {
	struct device		*dev;
	void __iomem		*regs;
	struct regmap		*grf;
	unsigned int		irq;
	struct clk		*grf_clk;
	struct clk		*pd;
	struct clk		*clk_edp;  /*clk for edp controller*/
	struct clk		*clk_24m;  /*clk for edp phy*/
	struct clk		*pclk;	   /*clk for phb bus*/
	struct reset_control    *rst_24m;
	struct reset_control    *rst_apb;
	struct link_train	link_train;
	struct video_info	video_info;
	struct rk_screen	screen;
	struct fb_monspecs      specs;
	bool clk_on;
	bool edp_en;
	int soctype;
	struct dentry *debugfs_dir;
};


void rk32_edp_enable_video_mute(struct rk32_edp *edp, bool enable);
void rk32_edp_stop_video(struct rk32_edp *edp);
void rk32_edp_lane_swap(struct rk32_edp *edp, bool enable);
void rk32_edp_init_refclk(struct rk32_edp *edp);
void rk32_edp_init_interrupt(struct rk32_edp *edp);
void rk32_edp_reset(struct rk32_edp *edp);
void rk32_edp_config_interrupt(struct rk32_edp *edp);
u32 rk32_edp_get_pll_lock_status(struct rk32_edp *edp);
void rk32_edp_analog_power_ctr(struct rk32_edp *edp, bool enable);
void rk32_edp_init_analog_func(struct rk32_edp *edp);
void rk32_edp_init_hpd(struct rk32_edp *edp);
void rk32_edp_reset_aux(struct rk32_edp *edp);
void rk32_edp_init_aux(struct rk32_edp *edp);
int rk32_edp_get_plug_in_status(struct rk32_edp *edp);
void rk32_edp_enable_sw_function(struct rk32_edp *edp);
int rk32_edp_start_aux_transaction(struct rk32_edp *edp);
int rk32_edp_write_byte_to_dpcd(struct rk32_edp *edp,
				unsigned int reg_addr,
				unsigned char data);
int rk32_edp_read_byte_from_dpcd(struct rk32_edp *edp,
				 unsigned int reg_addr,
				 unsigned char *data);
int rk32_edp_write_bytes_to_dpcd(struct rk32_edp *edp,
				 unsigned int reg_addr,
				 unsigned int count,
				 unsigned char data[]);
int rk32_edp_read_bytes_from_dpcd(struct rk32_edp *edp,
				  unsigned int reg_addr,
				  unsigned int count,
				  unsigned char data[]);
int rk32_edp_select_i2c_device(struct rk32_edp *edp,
			       unsigned int device_addr,
			       unsigned int reg_addr);
int rk32_edp_read_byte_from_i2c(struct rk32_edp *edp,
				unsigned int device_addr,
				unsigned int reg_addr,
				unsigned int *data);
int rk32_edp_read_bytes_from_i2c(struct rk32_edp *edp,
				 unsigned int device_addr,
				 unsigned int reg_addr,
				 unsigned int count,
				 unsigned char edid[]);
void rk32_edp_set_link_bandwidth(struct rk32_edp *edp, u32 bwtype);
void rk32_edp_get_link_bandwidth(struct rk32_edp *edp, u32 *bwtype);
void rk32_edp_set_lane_count(struct rk32_edp *edp, u32 count);
void rk32_edp_get_lane_count(struct rk32_edp *edp, u32 *count);
void rk32_edp_enable_enhanced_mode(struct rk32_edp *edp, bool enable);
void rk32_edp_set_training_pattern(struct rk32_edp *edp,
				   enum pattern_set pattern);
void rk32_edp_set_lane0_pre_emphasis(struct rk32_edp *edp, u32 level);
void rk32_edp_set_lane1_pre_emphasis(struct rk32_edp *edp, u32 level);
void rk32_edp_set_lane2_pre_emphasis(struct rk32_edp *edp, u32 level);
void rk32_edp_set_lane3_pre_emphasis(struct rk32_edp *edp, u32 level);
void rk32_edp_set_lane0_link_training(struct rk32_edp *edp,
				      u32 training_lane);
void rk32_edp_set_lane1_link_training(struct rk32_edp *edp,
				      u32 training_lane);
void rk32_edp_set_lane2_link_training(struct rk32_edp *edp,
				      u32 training_lane);
void rk32_edp_set_lane3_link_training(struct rk32_edp *edp,
				      u32 training_lane);
u32 rk32_edp_get_lane0_link_training(struct rk32_edp *edp);
u32 rk32_edp_get_lane1_link_training(struct rk32_edp *edp);
u32 rk32_edp_get_lane2_link_training(struct rk32_edp *edp);
u32 rk32_edp_get_lane3_link_training(struct rk32_edp *edp);
void rk32_edp_reset_macro(struct rk32_edp *edp);
int rk32_edp_init_video(struct rk32_edp *edp);

void rk32_edp_set_video_color_format(struct rk32_edp *edp,
				     u32 color_depth,
				     u32 color_space,
				     u32 dynamic_range,
				     u32 coeff);
int rk32_edp_is_slave_video_stream_clock_on(struct rk32_edp *edp);
void rk32_edp_set_video_cr_mn(struct rk32_edp *edp,
			      enum clock_recovery_m_value_type type,
			      u32 m_value,
			      u32 n_value);
void rk32_edp_set_video_timing_mode(struct rk32_edp *edp, u32 type);
void rk32_edp_enable_video_master(struct rk32_edp *edp, bool enable);
void rk32_edp_start_video(struct rk32_edp *edp);
int rk32_edp_is_video_stream_on(struct rk32_edp *edp);
void rk32_edp_config_video_slave_mode(struct rk32_edp *edp,
				      struct video_info *video_info);
void rk32_edp_enable_scrambling(struct rk32_edp *edp);
void rk32_edp_disable_scrambling(struct rk32_edp *edp);
void rk32_edp_rx_control(struct rk32_edp *edp, bool enable);
int rk32_edp_bist_cfg(struct rk32_edp *edp);
void rk32_edp_hw_link_training_en(struct rk32_edp *edp);
int rk32_edp_get_hw_lt_status(struct rk32_edp *edp);
int rk32_edp_wait_hw_lt_done(struct rk32_edp *edp);
enum dp_irq_type rk32_edp_get_irq_type(struct rk32_edp *edp);
void rk32_edp_clear_hotplug_interrupts(struct rk32_edp *edp);

#endif

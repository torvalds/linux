#ifndef __RK32_DP_H
#define __RK32_DP_H

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

#define PLL_REG_1				0xfc
#define REF_CLK_24M				(0x01 << 1)
#define REF_CLK_27M				(0x0 << 1)

#define DP_PWRDN				0x12c
#define PD_INC_BG				(0x1 << 7)
#define PD_EXP_BG				(0x1 << 6)
#define PD_AUX					(0x1 << 5)
#define PD_PLL					(0x1 << 4)
#define PD_CH3					(0x1 << 3)
#define PD_CH2					(0x1 << 2)
#define PD_CH1					(0x1 << 1)
#define PD_CH0					(0x1 << 0)

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
#define AUX_RX_COMM_I2C_DEFER			(0x2 << 2)
#define AUX_RX_COMM_AUX_DEFER			(0x2 << 0)

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



/* AUX_ADDR_7_0 */
#define AUX_ADDR_7_0(x)				(((x) >> 0) & 0xff)

/* AUX_ADDR_15_8 */
#define AUX_ADDR_15_8(x)			(((x) >> 8) & 0xff)

/* AUX_ADDR_19_16 */
#define AUX_ADDR_19_16(x)			(((x) >> 16) & 0x0f)


/* I2C EDID Chip ID, Slave Address */
#define I2C_EDID_DEVICE_ADDR			0x50
#define I2C_E_EDID_DEVICE_ADDR			0x30

#define EDID_BLOCK_LENGTH			0x80
#define EDID_HEADER_PATTERN			0x00
#define EDID_EXTENSION_FLAG			0x7e
#define EDID_CHECKSUM				0x7f


/* Definition for DPCD Register */
#define DPCD_ADDR_DPCD_REV			0x0000
#define DPCD_ADDR_MAX_LINK_RATE			0x0001
#define DPCD_ADDR_MAX_LANE_COUNT		0x0002
#define DPCD_ADDR_LINK_BW_SET			0x0100
#define DPCD_ADDR_LANE_COUNT_SET		0x0101
#define DPCD_ADDR_TRAINING_PATTERN_SET		0x0102
#define DPCD_ADDR_TRAINING_LANE0_SET		0x0103
#define DPCD_ADDR_LANE0_1_STATUS		0x0202
#define DPCD_ADDR_LANE_ALIGN_STATUS_UPDATED	0x0204
#define DPCD_ADDR_ADJUST_REQUEST_LANE0_1	0x0206
#define DPCD_ADDR_ADJUST_REQUEST_LANE2_3	0x0207
#define DPCD_ADDR_TEST_REQUEST			0x0218
#define DPCD_ADDR_TEST_RESPONSE			0x0260
#define DPCD_ADDR_TEST_EDID_CHECKSUM		0x0261
#define DPCD_ADDR_SINK_POWER_STATE		0x0600

/* DPCD_ADDR_MAX_LANE_COUNT */
#define DPCD_ENHANCED_FRAME_CAP(x)		(((x) >> 7) & 0x1)
#define DPCD_MAX_LANE_COUNT(x)			((x) & 0x1f)

/* DPCD_ADDR_LANE_COUNT_SET */
#define DPCD_ENHANCED_FRAME_EN			(0x1 << 7)
#define DPCD_LANE_COUNT_SET(x)			((x) & 0x1f)

/* DPCD_ADDR_TRAINING_PATTERN_SET */
#define DPCD_SCRAMBLING_DISABLED		(0x1 << 5)
#define DPCD_SCRAMBLING_ENABLED			(0x0 << 5)
#define DPCD_TRAINING_PATTERN_2			(0x2 << 0)
#define DPCD_TRAINING_PATTERN_1			(0x1 << 0)
#define DPCD_TRAINING_PATTERN_DISABLED		(0x0 << 0)

/* DPCD_ADDR_TRAINING_LANE0_SET */
#define DPCD_MAX_PRE_EMPHASIS_REACHED		(0x1 << 5)
#define DPCD_PRE_EMPHASIS_SET(x)		(((x) & 0x3) << 3)
#define DPCD_PRE_EMPHASIS_GET(x)		(((x) >> 3) & 0x3)
#define DPCD_PRE_EMPHASIS_PATTERN2_LEVEL0	(0x0 << 3)
#define DPCD_MAX_SWING_REACHED			(0x1 << 2)
#define DPCD_VOLTAGE_SWING_SET(x)		(((x) & 0x3) << 0)
#define DPCD_VOLTAGE_SWING_GET(x)		(((x) >> 0) & 0x3)
#define DPCD_VOLTAGE_SWING_PATTERN1_LEVEL0	(0x0 << 0)

/* DPCD_ADDR_LANE0_1_STATUS */
#define DPCD_LANE_SYMBOL_LOCKED			(0x1 << 2)
#define DPCD_LANE_CHANNEL_EQ_DONE		(0x1 << 1)
#define DPCD_LANE_CR_DONE			(0x1 << 0)
#define DPCD_CHANNEL_EQ_BITS			(DPCD_LANE_CR_DONE|	\
						 DPCD_LANE_CHANNEL_EQ_DONE|\
						 DPCD_LANE_SYMBOL_LOCKED)

/* DPCD_ADDR_LANE_ALIGN__STATUS_UPDATED */
#define DPCD_LINK_STATUS_UPDATED		(0x1 << 7)
#define DPCD_DOWNSTREAM_PORT_STATUS_CHANGED	(0x1 << 6)
#define DPCD_INTERLANE_ALIGN_DONE		(0x1 << 0)

/* DPCD_ADDR_TEST_REQUEST */
#define DPCD_TEST_EDID_READ			(0x1 << 2)

/* DPCD_ADDR_TEST_RESPONSE */
#define DPCD_TEST_EDID_CHECKSUM_WRITE		(0x1 << 2)

/* DPCD_ADDR_SINK_POWER_STATE */
#define DPCD_SET_POWER_STATE_D0			(0x1 << 0)
#define DPCD_SET_POWER_STATE_D4			(0x2 << 0)

#define DP_TIMEOUT_LOOP_CNT 100
#define MAX_CR_LOOP 5
#define MAX_EQ_LOOP 5

#define REF_CLK_FROM_INTER			(1 << 4)
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



struct rk32_edp {
	struct device 		*dev;
	void __iomem  		*regs;
	struct clk    		*clk_edp;
	struct clk    		*clk_24m;
	struct link_train	link_train;
	struct video_info	video_info;
	int 			enabled;
};


void rk32_edp_enable_video_mute(struct rk32_edp *edp, bool enable);
void rk32_edp_stop_video(struct rk32_edp *edp);
void rk32_edp_lane_swap(struct rk32_edp *edp, bool enable);
void rk32_edp_init_analog_param(struct rk32_edp *edp);
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


#endif

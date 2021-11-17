/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2020, Analogix Semiconductor. All rights reserved.
 *
 */

#ifndef __ANX7625_H__
#define __ANX7625_H__

#define ANX7625_DRV_VERSION "0.1.04"

/* Loading OCM re-trying times */
#define OCM_LOADING_TIME 10

/*********  ANX7625 Register  **********/
#define TX_P0_ADDR				0x70
#define TX_P1_ADDR				0x7A
#define TX_P2_ADDR				0x72

#define RX_P0_ADDR				0x7e
#define RX_P1_ADDR				0x84
#define RX_P2_ADDR				0x54

#define RSVD_00_ADDR				0x00
#define RSVD_D1_ADDR				0xD1
#define RSVD_60_ADDR				0x60
#define RSVD_39_ADDR				0x39
#define RSVD_7F_ADDR				0x7F

#define TCPC_INTERFACE_ADDR			0x58

/* Clock frequency in Hz */
#define XTAL_FRQ        (27 * 1000000)

#define  POST_DIVIDER_MIN	1
#define  POST_DIVIDER_MAX	16
#define  PLL_OUT_FREQ_MIN	520000000UL
#define  PLL_OUT_FREQ_MAX	730000000UL
#define  PLL_OUT_FREQ_ABS_MIN	300000000UL
#define  PLL_OUT_FREQ_ABS_MAX	800000000UL
#define  MAX_UNSIGNED_24BIT	16777215UL

/***************************************************************/
/* Register definition of device address 0x58 */

#define PRODUCT_ID_L 0x02
#define PRODUCT_ID_H 0x03

#define INTR_ALERT_1  0xCC
#define INTR_SOFTWARE_INT BIT(3)
#define INTR_RECEIVED_MSG BIT(5)

#define SYSTEM_STSTUS 0x45
#define INTERFACE_CHANGE_INT 0x44
#define HPD_STATUS_CHANGE 0x80
#define HPD_STATUS 0x80

/******** END of I2C Address 0x58 ********/

/***************************************************************/
/* Register definition of device address 0x70 */
#define  I2C_ADDR_70_DPTX              0x70

#define SP_TX_LINK_BW_SET_REG 0xA0
#define SP_TX_LANE_COUNT_SET_REG 0xA1

#define M_VID_0 0xC0
#define M_VID_1 0xC1
#define M_VID_2 0xC2
#define N_VID_0 0xC3
#define N_VID_1 0xC4
#define N_VID_2 0xC5

/***************************************************************/
/* Register definition of device address 0x72 */
#define AUX_RST	0x04
#define RST_CTRL2 0x07

#define SP_TX_TOTAL_LINE_STA_L 0x24
#define SP_TX_TOTAL_LINE_STA_H 0x25
#define SP_TX_ACT_LINE_STA_L 0x26
#define SP_TX_ACT_LINE_STA_H 0x27
#define SP_TX_V_F_PORCH_STA 0x28
#define SP_TX_V_SYNC_STA 0x29
#define SP_TX_V_B_PORCH_STA 0x2A
#define SP_TX_TOTAL_PIXEL_STA_L 0x2B
#define SP_TX_TOTAL_PIXEL_STA_H 0x2C
#define SP_TX_ACT_PIXEL_STA_L 0x2D
#define SP_TX_ACT_PIXEL_STA_H 0x2E
#define SP_TX_H_F_PORCH_STA_L 0x2F
#define SP_TX_H_F_PORCH_STA_H 0x30
#define SP_TX_H_SYNC_STA_L 0x31
#define SP_TX_H_SYNC_STA_H 0x32
#define SP_TX_H_B_PORCH_STA_L 0x33
#define SP_TX_H_B_PORCH_STA_H 0x34

#define SP_TX_VID_CTRL 0x84
#define SP_TX_BPC_MASK 0xE0
#define SP_TX_BPC_6    0x00
#define SP_TX_BPC_8    0x20
#define SP_TX_BPC_10   0x40
#define SP_TX_BPC_12   0x60

#define VIDEO_BIT_MATRIX_12 0x4c

#define AUDIO_CHANNEL_STATUS_1 0xd0
#define AUDIO_CHANNEL_STATUS_2 0xd1
#define AUDIO_CHANNEL_STATUS_3 0xd2
#define AUDIO_CHANNEL_STATUS_4 0xd3
#define AUDIO_CHANNEL_STATUS_5 0xd4
#define AUDIO_CHANNEL_STATUS_6 0xd5
#define TDM_SLAVE_MODE 0x10
#define I2S_SLAVE_MODE 0x08

#define AUDIO_CONTROL_REGISTER 0xe6
#define TDM_TIMING_MODE 0x08

#define I2C_ADDR_72_DPTX              0x72

#define HP_MIN			8
#define HBLANKING_MIN		80
#define SYNC_LEN_DEF		32
#define HFP_HBP_DEF		((HBLANKING_MIN - SYNC_LEN_DEF) / 2)
#define VIDEO_CONTROL_0	0x08

#define  ACTIVE_LINES_L         0x14
#define  ACTIVE_LINES_H         0x15  /* Bit[7:6] are reserved */
#define  VERTICAL_FRONT_PORCH   0x16
#define  VERTICAL_SYNC_WIDTH    0x17
#define  VERTICAL_BACK_PORCH    0x18

#define  HORIZONTAL_TOTAL_PIXELS_L    0x19
#define  HORIZONTAL_TOTAL_PIXELS_H    0x1A  /* Bit[7:6] are reserved */
#define  HORIZONTAL_ACTIVE_PIXELS_L   0x1B
#define  HORIZONTAL_ACTIVE_PIXELS_H   0x1C  /* Bit[7:6] are reserved */
#define  HORIZONTAL_FRONT_PORCH_L     0x1D
#define  HORIZONTAL_FRONT_PORCH_H     0x1E  /* Bit[7:4] are reserved */
#define  HORIZONTAL_SYNC_WIDTH_L      0x1F
#define  HORIZONTAL_SYNC_WIDTH_H      0x20  /* Bit[7:4] are reserved */
#define  HORIZONTAL_BACK_PORCH_L      0x21
#define  HORIZONTAL_BACK_PORCH_H      0x22  /* Bit[7:4] are reserved */

/******** END of I2C Address 0x72 *********/
/***************************************************************/
/* Register definition of device address 0x7e */

#define  I2C_ADDR_7E_FLASH_CONTROLLER  0x7E

#define FLASH_LOAD_STA 0x05
#define FLASH_LOAD_STA_CHK	BIT(7)

#define  XTAL_FRQ_SEL    0x3F
/* bit field positions */
#define  XTAL_FRQ_SEL_POS    5
/* bit field values */
#define  XTAL_FRQ_19M2   (0 << XTAL_FRQ_SEL_POS)
#define  XTAL_FRQ_27M    (4 << XTAL_FRQ_SEL_POS)

#define  R_DSC_CTRL_0    0x40
#define  READ_STATUS_EN  7
#define  CLK_1MEG_RB     6  /* 1MHz clock reset; 0=reset, 0=reset release */
#define  DSC_BIST_DONE   1  /* Bit[5:1]: 1=DSC MBIST pass */
#define  DSC_EN          0x01  /* 1=DSC enabled, 0=DSC disabled */

#define OCM_FW_VERSION   0x31
#define OCM_FW_REVERSION 0x32

#define AP_AUX_ADDR_7_0   0x11
#define AP_AUX_ADDR_15_8  0x12
#define AP_AUX_ADDR_19_16 0x13

/* Bit[0:3] AUX status, bit 4 op_en, bit 5 address only */
#define AP_AUX_CTRL_STATUS 0x14
#define AP_AUX_CTRL_OP_EN 0x10
#define AP_AUX_CTRL_ADDRONLY 0x20

#define AP_AUX_BUFF_START 0x15
#define PIXEL_CLOCK_L 0x25
#define PIXEL_CLOCK_H 0x26

#define AP_AUX_COMMAND 0x27  /* com+len */
/* Bit 0&1: 3D video structure */
/* 0x01: frame packing,  0x02:Line alternative, 0x03:Side-by-side(full) */
#define AP_AV_STATUS 0x28
#define AP_VIDEO_CHG  BIT(2)
#define AP_AUDIO_CHG  BIT(3)
#define AP_MIPI_MUTE  BIT(4) /* 1:MIPI input mute, 0: ummute */
#define AP_MIPI_RX_EN BIT(5) /* 1: MIPI RX input in  0: no RX in */
#define AP_DISABLE_PD BIT(6)
#define AP_DISABLE_DISPLAY BIT(7)
/***************************************************************/
/* Register definition of device address 0x84 */
#define  MIPI_PHY_CONTROL_3            0x03
#define  MIPI_HS_PWD_CLK               7
#define  MIPI_HS_RT_CLK                6
#define  MIPI_PD_CLK                   5
#define  MIPI_CLK_RT_MANUAL_PD_EN      4
#define  MIPI_CLK_HS_MANUAL_PD_EN      3
#define  MIPI_CLK_DET_DET_BYPASS       2
#define  MIPI_CLK_MISS_CTRL            1
#define  MIPI_PD_LPTX_CH_MANUAL_PD_EN  0

#define  MIPI_LANE_CTRL_0		0x05
#define  MIPI_TIME_HS_PRPR		0x08

/*
 * After MIPI RX protocol layer received video frames,
 * Protocol layer starts to reconstruct video stream from PHY
 */
#define  MIPI_VIDEO_STABLE_CNT           0x0A

#define  MIPI_LANE_CTRL_10               0x0F
#define  MIPI_DIGITAL_ADJ_1     0x1B
#define  IVO_MID0               0x26
#define  IVO_MID1               0xCF

#define  MIPI_PLL_M_NUM_23_16   0x1E
#define  MIPI_PLL_M_NUM_15_8    0x1F
#define  MIPI_PLL_M_NUM_7_0     0x20
#define  MIPI_PLL_N_NUM_23_16   0x21
#define  MIPI_PLL_N_NUM_15_8    0x22
#define  MIPI_PLL_N_NUM_7_0     0x23

#define  MIPI_DIGITAL_PLL_6     0x2A
/* Bit[7:6]: VCO band control, only effective */
#define  MIPI_M_NUM_READY        0x10
#define  MIPI_N_NUM_READY        0x08
#define  STABLE_INTEGER_CNT_EN   0x04
#define  MIPI_PLL_TEST_BIT       0
/* Bit[1:0]: test point output select - */
/* 00: VCO power, 01: dvdd_pdt, 10: dvdd, 11: vcox */

#define  MIPI_DIGITAL_PLL_7      0x2B
#define  MIPI_PLL_FORCE_N_EN     7
#define  MIPI_PLL_FORCE_BAND_EN  6

#define  MIPI_PLL_VCO_TUNE_REG   4
/* Bit[5:4]: VCO metal capacitance - */
/* 00: +20% fast, 01: +10% fast (default), 10: typical, 11: -10% slow */
#define  MIPI_PLL_VCO_TUNE_REG_VAL   0x30

#define  MIPI_PLL_PLL_LDO_BIT    2
/* Bit[3:2]: vco_v2i power - */
/* 00: 1.40V, 01: 1.45V (default), 10: 1.50V, 11: 1.55V */
#define  MIPI_PLL_RESET_N        0x02
#define  MIPI_FRQ_FORCE_NDET     0

#define  MIPI_ALERT_CLR_0        0x2D
#define  HS_link_error_clear     7
/* This bit itself is S/C, and it clears 0x84:0x31[7] */

#define  MIPI_ALERT_OUT_0        0x31
#define  check_sum_err_hs_sync   7
/* This bit is cleared by 0x84:0x2D[7] */

#define  MIPI_DIGITAL_PLL_8    0x33
#define  MIPI_POST_DIV_VAL     4
/* N means divided by (n+1), n = 0~15 */
#define  MIPI_EN_LOCK_FRZ      3
#define  MIPI_FRQ_COUNTER_RST  2
#define  MIPI_FRQ_SET_REG_8    1
/* Bit 0 is reserved */

#define  MIPI_DIGITAL_PLL_9    0x34

#define  MIPI_DIGITAL_PLL_16   0x3B
#define  MIPI_FRQ_FREEZE_NDET          7
#define  MIPI_FRQ_REG_SET_ENABLE       6
#define  MIPI_REG_FORCE_SEL_EN         5
#define  MIPI_REG_SEL_DIV_REG          4
#define  MIPI_REG_FORCE_PRE_DIV_EN     3
/* Bit 2 is reserved */
#define  MIPI_FREF_D_IND               1
#define  REF_CLK_27000KHZ    1
#define  REF_CLK_19200KHZ    0
#define  MIPI_REG_PLL_PLL_TEST_ENABLE  0

#define  MIPI_DIGITAL_PLL_18  0x3D
#define  FRQ_COUNT_RB_SEL       7
#define  REG_FORCE_POST_DIV_EN  6
#define  MIPI_DPI_SELECT        5
#define  SELECT_DSI  1
#define  SELECT_DPI  0
#define  REG_BAUD_DIV_RATIO     0

#define  H_BLANK_L            0x3E
/* For DSC only */
#define  H_BLANK_H            0x3F
/* For DSC only; note: bit[7:6] are reserved */
#define  MIPI_SWAP  0x4A
#define  MIPI_SWAP_CH0    7
#define  MIPI_SWAP_CH1    6
#define  MIPI_SWAP_CH2    5
#define  MIPI_SWAP_CH3    4
#define  MIPI_SWAP_CLK    3
/* Bit[2:0] are reserved */

/******** END of I2C Address 0x84 *********/

/* DPCD regs */
#define DPCD_DPCD_REV                  0x00
#define DPCD_MAX_LINK_RATE             0x01
#define DPCD_MAX_LANE_COUNT            0x02

/*********  ANX7625 Register End  **********/

/***************** Display *****************/
enum audio_fs {
	AUDIO_FS_441K  = 0x00,
	AUDIO_FS_48K   = 0x02,
	AUDIO_FS_32K   = 0x03,
	AUDIO_FS_882K  = 0x08,
	AUDIO_FS_96K   = 0x0a,
	AUDIO_FS_1764K = 0x0c,
	AUDIO_FS_192K  = 0x0e
};

enum audio_wd_len {
	AUDIO_W_LEN_16_20MAX = 0x02,
	AUDIO_W_LEN_18_20MAX = 0x04,
	AUDIO_W_LEN_17_20MAX = 0x0c,
	AUDIO_W_LEN_19_20MAX = 0x08,
	AUDIO_W_LEN_20_20MAX = 0x0a,
	AUDIO_W_LEN_20_24MAX = 0x03,
	AUDIO_W_LEN_22_24MAX = 0x05,
	AUDIO_W_LEN_21_24MAX = 0x0d,
	AUDIO_W_LEN_23_24MAX = 0x09,
	AUDIO_W_LEN_24_24MAX = 0x0b
};

#define I2S_CH_2	0x01
#define TDM_CH_4	0x03
#define TDM_CH_6	0x05
#define TDM_CH_8	0x07

#define MAX_DPCD_BUFFER_SIZE	16

#define ONE_BLOCK_SIZE      128
#define FOUR_BLOCK_SIZE     (128 * 4)

#define MAX_EDID_BLOCK	3
#define EDID_TRY_CNT	3
#define SUPPORT_PIXEL_CLOCK	300000

struct s_edid_data {
	int edid_block_num;
	u8 edid_raw_data[FOUR_BLOCK_SIZE];
};

/***************** Display End *****************/

struct anx7625_platform_data {
	struct gpio_desc *gpio_p_on;
	struct gpio_desc *gpio_reset;
	struct regulator_bulk_data supplies[3];
	struct drm_bridge *panel_bridge;
	int intp_irq;
	u32 low_power_mode;
	struct device_node *mipi_host_node;
};

struct anx7625_i2c_client {
	struct i2c_client *tx_p0_client;
	struct i2c_client *tx_p1_client;
	struct i2c_client *tx_p2_client;
	struct i2c_client *rx_p0_client;
	struct i2c_client *rx_p1_client;
	struct i2c_client *rx_p2_client;
	struct i2c_client *tcpc_client;
};

struct anx7625_data {
	struct anx7625_platform_data pdata;
	int hpd_status;
	int hpd_high_cnt;
	/* Lock for work queue */
	struct mutex lock;
	struct i2c_client *client;
	struct anx7625_i2c_client i2c;
	struct i2c_client *last_client;
	struct s_edid_data slimport_edid_p;
	struct work_struct work;
	struct workqueue_struct *workqueue;
	char edid_block;
	struct display_timing dt;
	u8 display_timing_valid;
	struct drm_bridge bridge;
	u8 bridge_attached;
	struct mipi_dsi_device *dsi;
};

#endif  /* __ANX7625_H__ */

// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2013-2017 Broadcom

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "../core.h"
#include "../pinctrl-utils.h"

/* BCM281XX Pin Control Registers Definitions */

/* Function Select bits are the same for all pin control registers */
#define BCM281XX_PIN_REG_F_SEL_MASK		0x0700
#define BCM281XX_PIN_REG_F_SEL_SHIFT		8

/* Standard pin register */
#define BCM281XX_STD_PIN_REG_DRV_STR_MASK	0x0007
#define BCM281XX_STD_PIN_REG_DRV_STR_SHIFT	0
#define BCM281XX_STD_PIN_REG_INPUT_DIS_MASK	0x0008
#define BCM281XX_STD_PIN_REG_INPUT_DIS_SHIFT	3
#define BCM281XX_STD_PIN_REG_SLEW_MASK		0x0010
#define BCM281XX_STD_PIN_REG_SLEW_SHIFT		4
#define BCM281XX_STD_PIN_REG_PULL_UP_MASK	0x0020
#define BCM281XX_STD_PIN_REG_PULL_UP_SHIFT	5
#define BCM281XX_STD_PIN_REG_PULL_DN_MASK	0x0040
#define BCM281XX_STD_PIN_REG_PULL_DN_SHIFT	6
#define BCM281XX_STD_PIN_REG_HYST_MASK		0x0080
#define BCM281XX_STD_PIN_REG_HYST_SHIFT		7

/* I2C pin register */
#define BCM281XX_I2C_PIN_REG_INPUT_DIS_MASK	0x0004
#define BCM281XX_I2C_PIN_REG_INPUT_DIS_SHIFT	2
#define BCM281XX_I2C_PIN_REG_SLEW_MASK		0x0008
#define BCM281XX_I2C_PIN_REG_SLEW_SHIFT		3
#define BCM281XX_I2C_PIN_REG_PULL_UP_STR_MASK	0x0070
#define BCM281XX_I2C_PIN_REG_PULL_UP_STR_SHIFT	4

/* HDMI pin register */
#define BCM281XX_HDMI_PIN_REG_INPUT_DIS_MASK	0x0008
#define BCM281XX_HDMI_PIN_REG_INPUT_DIS_SHIFT	3
#define BCM281XX_HDMI_PIN_REG_MODE_MASK		0x0010
#define BCM281XX_HDMI_PIN_REG_MODE_SHIFT	4

/*
 * bcm281xx_pin_type - types of pin register
 */
enum bcm281xx_pin_type {
	BCM281XX_PIN_TYPE_UNKNOWN = 0,
	BCM281XX_PIN_TYPE_STD,
	BCM281XX_PIN_TYPE_I2C,
	BCM281XX_PIN_TYPE_HDMI,
};

static enum bcm281xx_pin_type std_pin = BCM281XX_PIN_TYPE_STD;
static enum bcm281xx_pin_type i2c_pin = BCM281XX_PIN_TYPE_I2C;
static enum bcm281xx_pin_type hdmi_pin = BCM281XX_PIN_TYPE_HDMI;

/*
 * bcm281xx_pin_function- define pin function
 */
struct bcm281xx_pin_function {
	const char *name;
	const char * const *groups;
	const unsigned ngroups;
};

/*
 * bcm281xx_pinctrl_data - Broadcom-specific pinctrl data
 * @reg_base - base of pinctrl registers
 */
struct bcm281xx_pinctrl_data {
	void __iomem *reg_base;

	/* List of all pins */
	const struct pinctrl_pin_desc *pins;
	const unsigned npins;

	const struct bcm281xx_pin_function *functions;
	const unsigned nfunctions;

	struct regmap *regmap;
};

/*
 * Pin number definition.  The order here must be the same as defined in the
 * PADCTRLREG block in the RDB.
 */
#define BCM281XX_PIN_ADCSYNC		0
#define BCM281XX_PIN_BAT_RM		1
#define BCM281XX_PIN_BSC1_SCL		2
#define BCM281XX_PIN_BSC1_SDA		3
#define BCM281XX_PIN_BSC2_SCL		4
#define BCM281XX_PIN_BSC2_SDA		5
#define BCM281XX_PIN_CLASSGPWR		6
#define BCM281XX_PIN_CLK_CX8		7
#define BCM281XX_PIN_CLKOUT_0		8
#define BCM281XX_PIN_CLKOUT_1		9
#define BCM281XX_PIN_CLKOUT_2		10
#define BCM281XX_PIN_CLKOUT_3		11
#define BCM281XX_PIN_CLKREQ_IN_0	12
#define BCM281XX_PIN_CLKREQ_IN_1	13
#define BCM281XX_PIN_CWS_SYS_REQ1	14
#define BCM281XX_PIN_CWS_SYS_REQ2	15
#define BCM281XX_PIN_CWS_SYS_REQ3	16
#define BCM281XX_PIN_DIGMIC1_CLK	17
#define BCM281XX_PIN_DIGMIC1_DQ		18
#define BCM281XX_PIN_DIGMIC2_CLK	19
#define BCM281XX_PIN_DIGMIC2_DQ		20
#define BCM281XX_PIN_GPEN13		21
#define BCM281XX_PIN_GPEN14		22
#define BCM281XX_PIN_GPEN15		23
#define BCM281XX_PIN_GPIO00		24
#define BCM281XX_PIN_GPIO01		25
#define BCM281XX_PIN_GPIO02		26
#define BCM281XX_PIN_GPIO03		27
#define BCM281XX_PIN_GPIO04		28
#define BCM281XX_PIN_GPIO05		29
#define BCM281XX_PIN_GPIO06		30
#define BCM281XX_PIN_GPIO07		31
#define BCM281XX_PIN_GPIO08		32
#define BCM281XX_PIN_GPIO09		33
#define BCM281XX_PIN_GPIO10		34
#define BCM281XX_PIN_GPIO11		35
#define BCM281XX_PIN_GPIO12		36
#define BCM281XX_PIN_GPIO13		37
#define BCM281XX_PIN_GPIO14		38
#define BCM281XX_PIN_GPS_PABLANK	39
#define BCM281XX_PIN_GPS_TMARK		40
#define BCM281XX_PIN_HDMI_SCL		41
#define BCM281XX_PIN_HDMI_SDA		42
#define BCM281XX_PIN_IC_DM		43
#define BCM281XX_PIN_IC_DP		44
#define BCM281XX_PIN_KP_COL_IP_0	45
#define BCM281XX_PIN_KP_COL_IP_1	46
#define BCM281XX_PIN_KP_COL_IP_2	47
#define BCM281XX_PIN_KP_COL_IP_3	48
#define BCM281XX_PIN_KP_ROW_OP_0	49
#define BCM281XX_PIN_KP_ROW_OP_1	50
#define BCM281XX_PIN_KP_ROW_OP_2	51
#define BCM281XX_PIN_KP_ROW_OP_3	52
#define BCM281XX_PIN_LCD_B_0		53
#define BCM281XX_PIN_LCD_B_1		54
#define BCM281XX_PIN_LCD_B_2		55
#define BCM281XX_PIN_LCD_B_3		56
#define BCM281XX_PIN_LCD_B_4		57
#define BCM281XX_PIN_LCD_B_5		58
#define BCM281XX_PIN_LCD_B_6		59
#define BCM281XX_PIN_LCD_B_7		60
#define BCM281XX_PIN_LCD_G_0		61
#define BCM281XX_PIN_LCD_G_1		62
#define BCM281XX_PIN_LCD_G_2		63
#define BCM281XX_PIN_LCD_G_3		64
#define BCM281XX_PIN_LCD_G_4		65
#define BCM281XX_PIN_LCD_G_5		66
#define BCM281XX_PIN_LCD_G_6		67
#define BCM281XX_PIN_LCD_G_7		68
#define BCM281XX_PIN_LCD_HSYNC		69
#define BCM281XX_PIN_LCD_OE		70
#define BCM281XX_PIN_LCD_PCLK		71
#define BCM281XX_PIN_LCD_R_0		72
#define BCM281XX_PIN_LCD_R_1		73
#define BCM281XX_PIN_LCD_R_2		74
#define BCM281XX_PIN_LCD_R_3		75
#define BCM281XX_PIN_LCD_R_4		76
#define BCM281XX_PIN_LCD_R_5		77
#define BCM281XX_PIN_LCD_R_6		78
#define BCM281XX_PIN_LCD_R_7		79
#define BCM281XX_PIN_LCD_VSYNC		80
#define BCM281XX_PIN_MDMGPIO0		81
#define BCM281XX_PIN_MDMGPIO1		82
#define BCM281XX_PIN_MDMGPIO2		83
#define BCM281XX_PIN_MDMGPIO3		84
#define BCM281XX_PIN_MDMGPIO4		85
#define BCM281XX_PIN_MDMGPIO5		86
#define BCM281XX_PIN_MDMGPIO6		87
#define BCM281XX_PIN_MDMGPIO7		88
#define BCM281XX_PIN_MDMGPIO8		89
#define BCM281XX_PIN_MPHI_DATA_0	90
#define BCM281XX_PIN_MPHI_DATA_1	91
#define BCM281XX_PIN_MPHI_DATA_2	92
#define BCM281XX_PIN_MPHI_DATA_3	93
#define BCM281XX_PIN_MPHI_DATA_4	94
#define BCM281XX_PIN_MPHI_DATA_5	95
#define BCM281XX_PIN_MPHI_DATA_6	96
#define BCM281XX_PIN_MPHI_DATA_7	97
#define BCM281XX_PIN_MPHI_DATA_8	98
#define BCM281XX_PIN_MPHI_DATA_9	99
#define BCM281XX_PIN_MPHI_DATA_10	100
#define BCM281XX_PIN_MPHI_DATA_11	101
#define BCM281XX_PIN_MPHI_DATA_12	102
#define BCM281XX_PIN_MPHI_DATA_13	103
#define BCM281XX_PIN_MPHI_DATA_14	104
#define BCM281XX_PIN_MPHI_DATA_15	105
#define BCM281XX_PIN_MPHI_HA0		106
#define BCM281XX_PIN_MPHI_HAT0		107
#define BCM281XX_PIN_MPHI_HAT1		108
#define BCM281XX_PIN_MPHI_HCE0_N	109
#define BCM281XX_PIN_MPHI_HCE1_N	110
#define BCM281XX_PIN_MPHI_HRD_N		111
#define BCM281XX_PIN_MPHI_HWR_N		112
#define BCM281XX_PIN_MPHI_RUN0		113
#define BCM281XX_PIN_MPHI_RUN1		114
#define BCM281XX_PIN_MTX_SCAN_CLK	115
#define BCM281XX_PIN_MTX_SCAN_DATA	116
#define BCM281XX_PIN_NAND_AD_0		117
#define BCM281XX_PIN_NAND_AD_1		118
#define BCM281XX_PIN_NAND_AD_2		119
#define BCM281XX_PIN_NAND_AD_3		120
#define BCM281XX_PIN_NAND_AD_4		121
#define BCM281XX_PIN_NAND_AD_5		122
#define BCM281XX_PIN_NAND_AD_6		123
#define BCM281XX_PIN_NAND_AD_7		124
#define BCM281XX_PIN_NAND_ALE		125
#define BCM281XX_PIN_NAND_CEN_0		126
#define BCM281XX_PIN_NAND_CEN_1		127
#define BCM281XX_PIN_NAND_CLE		128
#define BCM281XX_PIN_NAND_OEN		129
#define BCM281XX_PIN_NAND_RDY_0		130
#define BCM281XX_PIN_NAND_RDY_1		131
#define BCM281XX_PIN_NAND_WEN		132
#define BCM281XX_PIN_NAND_WP		133
#define BCM281XX_PIN_PC1		134
#define BCM281XX_PIN_PC2		135
#define BCM281XX_PIN_PMU_INT		136
#define BCM281XX_PIN_PMU_SCL		137
#define BCM281XX_PIN_PMU_SDA		138
#define BCM281XX_PIN_RFST2G_MTSLOTEN3G	139
#define BCM281XX_PIN_RGMII_0_RX_CTL	140
#define BCM281XX_PIN_RGMII_0_RXC	141
#define BCM281XX_PIN_RGMII_0_RXD_0	142
#define BCM281XX_PIN_RGMII_0_RXD_1	143
#define BCM281XX_PIN_RGMII_0_RXD_2	144
#define BCM281XX_PIN_RGMII_0_RXD_3	145
#define BCM281XX_PIN_RGMII_0_TX_CTL	146
#define BCM281XX_PIN_RGMII_0_TXC	147
#define BCM281XX_PIN_RGMII_0_TXD_0	148
#define BCM281XX_PIN_RGMII_0_TXD_1	149
#define BCM281XX_PIN_RGMII_0_TXD_2	150
#define BCM281XX_PIN_RGMII_0_TXD_3	151
#define BCM281XX_PIN_RGMII_1_RX_CTL	152
#define BCM281XX_PIN_RGMII_1_RXC	153
#define BCM281XX_PIN_RGMII_1_RXD_0	154
#define BCM281XX_PIN_RGMII_1_RXD_1	155
#define BCM281XX_PIN_RGMII_1_RXD_2	156
#define BCM281XX_PIN_RGMII_1_RXD_3	157
#define BCM281XX_PIN_RGMII_1_TX_CTL	158
#define BCM281XX_PIN_RGMII_1_TXC	159
#define BCM281XX_PIN_RGMII_1_TXD_0	160
#define BCM281XX_PIN_RGMII_1_TXD_1	161
#define BCM281XX_PIN_RGMII_1_TXD_2	162
#define BCM281XX_PIN_RGMII_1_TXD_3	163
#define BCM281XX_PIN_RGMII_GPIO_0	164
#define BCM281XX_PIN_RGMII_GPIO_1	165
#define BCM281XX_PIN_RGMII_GPIO_2	166
#define BCM281XX_PIN_RGMII_GPIO_3	167
#define BCM281XX_PIN_RTXDATA2G_TXDATA3G1	168
#define BCM281XX_PIN_RTXEN2G_TXDATA3G2	169
#define BCM281XX_PIN_RXDATA3G0		170
#define BCM281XX_PIN_RXDATA3G1		171
#define BCM281XX_PIN_RXDATA3G2		172
#define BCM281XX_PIN_SDIO1_CLK		173
#define BCM281XX_PIN_SDIO1_CMD		174
#define BCM281XX_PIN_SDIO1_DATA_0	175
#define BCM281XX_PIN_SDIO1_DATA_1	176
#define BCM281XX_PIN_SDIO1_DATA_2	177
#define BCM281XX_PIN_SDIO1_DATA_3	178
#define BCM281XX_PIN_SDIO4_CLK		179
#define BCM281XX_PIN_SDIO4_CMD		180
#define BCM281XX_PIN_SDIO4_DATA_0	181
#define BCM281XX_PIN_SDIO4_DATA_1	182
#define BCM281XX_PIN_SDIO4_DATA_2	183
#define BCM281XX_PIN_SDIO4_DATA_3	184
#define BCM281XX_PIN_SIM_CLK		185
#define BCM281XX_PIN_SIM_DATA		186
#define BCM281XX_PIN_SIM_DET		187
#define BCM281XX_PIN_SIM_RESETN		188
#define BCM281XX_PIN_SIM2_CLK		189
#define BCM281XX_PIN_SIM2_DATA		190
#define BCM281XX_PIN_SIM2_DET		191
#define BCM281XX_PIN_SIM2_RESETN	192
#define BCM281XX_PIN_SRI_C		193
#define BCM281XX_PIN_SRI_D		194
#define BCM281XX_PIN_SRI_E		195
#define BCM281XX_PIN_SSP_EXTCLK		196
#define BCM281XX_PIN_SSP0_CLK		197
#define BCM281XX_PIN_SSP0_FS		198
#define BCM281XX_PIN_SSP0_RXD		199
#define BCM281XX_PIN_SSP0_TXD		200
#define BCM281XX_PIN_SSP2_CLK		201
#define BCM281XX_PIN_SSP2_FS_0		202
#define BCM281XX_PIN_SSP2_FS_1		203
#define BCM281XX_PIN_SSP2_FS_2		204
#define BCM281XX_PIN_SSP2_FS_3		205
#define BCM281XX_PIN_SSP2_RXD_0		206
#define BCM281XX_PIN_SSP2_RXD_1		207
#define BCM281XX_PIN_SSP2_TXD_0		208
#define BCM281XX_PIN_SSP2_TXD_1		209
#define BCM281XX_PIN_SSP3_CLK		210
#define BCM281XX_PIN_SSP3_FS		211
#define BCM281XX_PIN_SSP3_RXD		212
#define BCM281XX_PIN_SSP3_TXD		213
#define BCM281XX_PIN_SSP4_CLK		214
#define BCM281XX_PIN_SSP4_FS		215
#define BCM281XX_PIN_SSP4_RXD		216
#define BCM281XX_PIN_SSP4_TXD		217
#define BCM281XX_PIN_SSP5_CLK		218
#define BCM281XX_PIN_SSP5_FS		219
#define BCM281XX_PIN_SSP5_RXD		220
#define BCM281XX_PIN_SSP5_TXD		221
#define BCM281XX_PIN_SSP6_CLK		222
#define BCM281XX_PIN_SSP6_FS		223
#define BCM281XX_PIN_SSP6_RXD		224
#define BCM281XX_PIN_SSP6_TXD		225
#define BCM281XX_PIN_STAT_1		226
#define BCM281XX_PIN_STAT_2		227
#define BCM281XX_PIN_SYSCLKEN		228
#define BCM281XX_PIN_TRACECLK		229
#define BCM281XX_PIN_TRACEDT00		230
#define BCM281XX_PIN_TRACEDT01		231
#define BCM281XX_PIN_TRACEDT02		232
#define BCM281XX_PIN_TRACEDT03		233
#define BCM281XX_PIN_TRACEDT04		234
#define BCM281XX_PIN_TRACEDT05		235
#define BCM281XX_PIN_TRACEDT06		236
#define BCM281XX_PIN_TRACEDT07		237
#define BCM281XX_PIN_TRACEDT08		238
#define BCM281XX_PIN_TRACEDT09		239
#define BCM281XX_PIN_TRACEDT10		240
#define BCM281XX_PIN_TRACEDT11		241
#define BCM281XX_PIN_TRACEDT12		242
#define BCM281XX_PIN_TRACEDT13		243
#define BCM281XX_PIN_TRACEDT14		244
#define BCM281XX_PIN_TRACEDT15		245
#define BCM281XX_PIN_TXDATA3G0		246
#define BCM281XX_PIN_TXPWRIND		247
#define BCM281XX_PIN_UARTB1_UCTS	248
#define BCM281XX_PIN_UARTB1_URTS	249
#define BCM281XX_PIN_UARTB1_URXD	250
#define BCM281XX_PIN_UARTB1_UTXD	251
#define BCM281XX_PIN_UARTB2_URXD	252
#define BCM281XX_PIN_UARTB2_UTXD	253
#define BCM281XX_PIN_UARTB3_UCTS	254
#define BCM281XX_PIN_UARTB3_URTS	255
#define BCM281XX_PIN_UARTB3_URXD	256
#define BCM281XX_PIN_UARTB3_UTXD	257
#define BCM281XX_PIN_UARTB4_UCTS	258
#define BCM281XX_PIN_UARTB4_URTS	259
#define BCM281XX_PIN_UARTB4_URXD	260
#define BCM281XX_PIN_UARTB4_UTXD	261
#define BCM281XX_PIN_VC_CAM1_SCL	262
#define BCM281XX_PIN_VC_CAM1_SDA	263
#define BCM281XX_PIN_VC_CAM2_SCL	264
#define BCM281XX_PIN_VC_CAM2_SDA	265
#define BCM281XX_PIN_VC_CAM3_SCL	266
#define BCM281XX_PIN_VC_CAM3_SDA	267

#define BCM281XX_PIN_DESC(a, b, c) \
	{ .number = a, .name = b, .drv_data = &c##_pin }

/*
 * Pin description definition.  The order here must be the same as defined in
 * the PADCTRLREG block in the RDB, since the pin number is used as an index
 * into this array.
 */
static const struct pinctrl_pin_desc bcm281xx_pinctrl_pins[] = {
	BCM281XX_PIN_DESC(BCM281XX_PIN_ADCSYNC, "adcsync", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_BAT_RM, "bat_rm", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_BSC1_SCL, "bsc1_scl", i2c),
	BCM281XX_PIN_DESC(BCM281XX_PIN_BSC1_SDA, "bsc1_sda", i2c),
	BCM281XX_PIN_DESC(BCM281XX_PIN_BSC2_SCL, "bsc2_scl", i2c),
	BCM281XX_PIN_DESC(BCM281XX_PIN_BSC2_SDA, "bsc2_sda", i2c),
	BCM281XX_PIN_DESC(BCM281XX_PIN_CLASSGPWR, "classgpwr", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_CLK_CX8, "clk_cx8", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_CLKOUT_0, "clkout_0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_CLKOUT_1, "clkout_1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_CLKOUT_2, "clkout_2", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_CLKOUT_3, "clkout_3", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_CLKREQ_IN_0, "clkreq_in_0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_CLKREQ_IN_1, "clkreq_in_1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_CWS_SYS_REQ1, "cws_sys_req1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_CWS_SYS_REQ2, "cws_sys_req2", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_CWS_SYS_REQ3, "cws_sys_req3", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_DIGMIC1_CLK, "digmic1_clk", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_DIGMIC1_DQ, "digmic1_dq", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_DIGMIC2_CLK, "digmic2_clk", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_DIGMIC2_DQ, "digmic2_dq", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_GPEN13, "gpen13", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_GPEN14, "gpen14", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_GPEN15, "gpen15", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_GPIO00, "gpio00", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_GPIO01, "gpio01", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_GPIO02, "gpio02", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_GPIO03, "gpio03", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_GPIO04, "gpio04", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_GPIO05, "gpio05", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_GPIO06, "gpio06", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_GPIO07, "gpio07", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_GPIO08, "gpio08", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_GPIO09, "gpio09", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_GPIO10, "gpio10", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_GPIO11, "gpio11", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_GPIO12, "gpio12", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_GPIO13, "gpio13", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_GPIO14, "gpio14", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_GPS_PABLANK, "gps_pablank", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_GPS_TMARK, "gps_tmark", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_HDMI_SCL, "hdmi_scl", hdmi),
	BCM281XX_PIN_DESC(BCM281XX_PIN_HDMI_SDA, "hdmi_sda", hdmi),
	BCM281XX_PIN_DESC(BCM281XX_PIN_IC_DM, "ic_dm", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_IC_DP, "ic_dp", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_KP_COL_IP_0, "kp_col_ip_0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_KP_COL_IP_1, "kp_col_ip_1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_KP_COL_IP_2, "kp_col_ip_2", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_KP_COL_IP_3, "kp_col_ip_3", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_KP_ROW_OP_0, "kp_row_op_0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_KP_ROW_OP_1, "kp_row_op_1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_KP_ROW_OP_2, "kp_row_op_2", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_KP_ROW_OP_3, "kp_row_op_3", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_B_0, "lcd_b_0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_B_1, "lcd_b_1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_B_2, "lcd_b_2", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_B_3, "lcd_b_3", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_B_4, "lcd_b_4", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_B_5, "lcd_b_5", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_B_6, "lcd_b_6", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_B_7, "lcd_b_7", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_G_0, "lcd_g_0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_G_1, "lcd_g_1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_G_2, "lcd_g_2", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_G_3, "lcd_g_3", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_G_4, "lcd_g_4", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_G_5, "lcd_g_5", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_G_6, "lcd_g_6", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_G_7, "lcd_g_7", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_HSYNC, "lcd_hsync", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_OE, "lcd_oe", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_PCLK, "lcd_pclk", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_R_0, "lcd_r_0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_R_1, "lcd_r_1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_R_2, "lcd_r_2", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_R_3, "lcd_r_3", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_R_4, "lcd_r_4", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_R_5, "lcd_r_5", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_R_6, "lcd_r_6", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_R_7, "lcd_r_7", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_LCD_VSYNC, "lcd_vsync", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MDMGPIO0, "mdmgpio0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MDMGPIO1, "mdmgpio1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MDMGPIO2, "mdmgpio2", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MDMGPIO3, "mdmgpio3", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MDMGPIO4, "mdmgpio4", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MDMGPIO5, "mdmgpio5", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MDMGPIO6, "mdmgpio6", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MDMGPIO7, "mdmgpio7", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MDMGPIO8, "mdmgpio8", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MPHI_DATA_0, "mphi_data_0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MPHI_DATA_1, "mphi_data_1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MPHI_DATA_2, "mphi_data_2", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MPHI_DATA_3, "mphi_data_3", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MPHI_DATA_4, "mphi_data_4", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MPHI_DATA_5, "mphi_data_5", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MPHI_DATA_6, "mphi_data_6", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MPHI_DATA_7, "mphi_data_7", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MPHI_DATA_8, "mphi_data_8", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MPHI_DATA_9, "mphi_data_9", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MPHI_DATA_10, "mphi_data_10", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MPHI_DATA_11, "mphi_data_11", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MPHI_DATA_12, "mphi_data_12", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MPHI_DATA_13, "mphi_data_13", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MPHI_DATA_14, "mphi_data_14", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MPHI_DATA_15, "mphi_data_15", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MPHI_HA0, "mphi_ha0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MPHI_HAT0, "mphi_hat0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MPHI_HAT1, "mphi_hat1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MPHI_HCE0_N, "mphi_hce0_n", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MPHI_HCE1_N, "mphi_hce1_n", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MPHI_HRD_N, "mphi_hrd_n", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MPHI_HWR_N, "mphi_hwr_n", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MPHI_RUN0, "mphi_run0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MPHI_RUN1, "mphi_run1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MTX_SCAN_CLK, "mtx_scan_clk", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_MTX_SCAN_DATA, "mtx_scan_data", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_NAND_AD_0, "nand_ad_0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_NAND_AD_1, "nand_ad_1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_NAND_AD_2, "nand_ad_2", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_NAND_AD_3, "nand_ad_3", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_NAND_AD_4, "nand_ad_4", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_NAND_AD_5, "nand_ad_5", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_NAND_AD_6, "nand_ad_6", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_NAND_AD_7, "nand_ad_7", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_NAND_ALE, "nand_ale", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_NAND_CEN_0, "nand_cen_0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_NAND_CEN_1, "nand_cen_1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_NAND_CLE, "nand_cle", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_NAND_OEN, "nand_oen", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_NAND_RDY_0, "nand_rdy_0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_NAND_RDY_1, "nand_rdy_1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_NAND_WEN, "nand_wen", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_NAND_WP, "nand_wp", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_PC1, "pc1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_PC2, "pc2", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_PMU_INT, "pmu_int", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_PMU_SCL, "pmu_scl", i2c),
	BCM281XX_PIN_DESC(BCM281XX_PIN_PMU_SDA, "pmu_sda", i2c),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RFST2G_MTSLOTEN3G, "rfst2g_mtsloten3g",
		std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_0_RX_CTL, "rgmii_0_rx_ctl", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_0_RXC, "rgmii_0_rxc", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_0_RXD_0, "rgmii_0_rxd_0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_0_RXD_1, "rgmii_0_rxd_1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_0_RXD_2, "rgmii_0_rxd_2", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_0_RXD_3, "rgmii_0_rxd_3", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_0_TX_CTL, "rgmii_0_tx_ctl", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_0_TXC, "rgmii_0_txc", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_0_TXD_0, "rgmii_0_txd_0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_0_TXD_1, "rgmii_0_txd_1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_0_TXD_2, "rgmii_0_txd_2", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_0_TXD_3, "rgmii_0_txd_3", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_1_RX_CTL, "rgmii_1_rx_ctl", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_1_RXC, "rgmii_1_rxc", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_1_RXD_0, "rgmii_1_rxd_0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_1_RXD_1, "rgmii_1_rxd_1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_1_RXD_2, "rgmii_1_rxd_2", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_1_RXD_3, "rgmii_1_rxd_3", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_1_TX_CTL, "rgmii_1_tx_ctl", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_1_TXC, "rgmii_1_txc", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_1_TXD_0, "rgmii_1_txd_0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_1_TXD_1, "rgmii_1_txd_1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_1_TXD_2, "rgmii_1_txd_2", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_1_TXD_3, "rgmii_1_txd_3", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_GPIO_0, "rgmii_gpio_0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_GPIO_1, "rgmii_gpio_1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_GPIO_2, "rgmii_gpio_2", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RGMII_GPIO_3, "rgmii_gpio_3", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RTXDATA2G_TXDATA3G1,
		"rtxdata2g_txdata3g1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RTXEN2G_TXDATA3G2, "rtxen2g_txdata3g2",
		std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RXDATA3G0, "rxdata3g0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RXDATA3G1, "rxdata3g1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_RXDATA3G2, "rxdata3g2", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SDIO1_CLK, "sdio1_clk", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SDIO1_CMD, "sdio1_cmd", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SDIO1_DATA_0, "sdio1_data_0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SDIO1_DATA_1, "sdio1_data_1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SDIO1_DATA_2, "sdio1_data_2", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SDIO1_DATA_3, "sdio1_data_3", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SDIO4_CLK, "sdio4_clk", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SDIO4_CMD, "sdio4_cmd", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SDIO4_DATA_0, "sdio4_data_0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SDIO4_DATA_1, "sdio4_data_1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SDIO4_DATA_2, "sdio4_data_2", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SDIO4_DATA_3, "sdio4_data_3", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SIM_CLK, "sim_clk", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SIM_DATA, "sim_data", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SIM_DET, "sim_det", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SIM_RESETN, "sim_resetn", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SIM2_CLK, "sim2_clk", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SIM2_DATA, "sim2_data", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SIM2_DET, "sim2_det", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SIM2_RESETN, "sim2_resetn", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SRI_C, "sri_c", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SRI_D, "sri_d", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SRI_E, "sri_e", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP_EXTCLK, "ssp_extclk", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP0_CLK, "ssp0_clk", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP0_FS, "ssp0_fs", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP0_RXD, "ssp0_rxd", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP0_TXD, "ssp0_txd", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP2_CLK, "ssp2_clk", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP2_FS_0, "ssp2_fs_0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP2_FS_1, "ssp2_fs_1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP2_FS_2, "ssp2_fs_2", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP2_FS_3, "ssp2_fs_3", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP2_RXD_0, "ssp2_rxd_0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP2_RXD_1, "ssp2_rxd_1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP2_TXD_0, "ssp2_txd_0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP2_TXD_1, "ssp2_txd_1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP3_CLK, "ssp3_clk", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP3_FS, "ssp3_fs", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP3_RXD, "ssp3_rxd", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP3_TXD, "ssp3_txd", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP4_CLK, "ssp4_clk", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP4_FS, "ssp4_fs", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP4_RXD, "ssp4_rxd", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP4_TXD, "ssp4_txd", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP5_CLK, "ssp5_clk", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP5_FS, "ssp5_fs", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP5_RXD, "ssp5_rxd", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP5_TXD, "ssp5_txd", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP6_CLK, "ssp6_clk", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP6_FS, "ssp6_fs", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP6_RXD, "ssp6_rxd", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SSP6_TXD, "ssp6_txd", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_STAT_1, "stat_1", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_STAT_2, "stat_2", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_SYSCLKEN, "sysclken", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_TRACECLK, "traceclk", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_TRACEDT00, "tracedt00", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_TRACEDT01, "tracedt01", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_TRACEDT02, "tracedt02", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_TRACEDT03, "tracedt03", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_TRACEDT04, "tracedt04", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_TRACEDT05, "tracedt05", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_TRACEDT06, "tracedt06", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_TRACEDT07, "tracedt07", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_TRACEDT08, "tracedt08", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_TRACEDT09, "tracedt09", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_TRACEDT10, "tracedt10", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_TRACEDT11, "tracedt11", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_TRACEDT12, "tracedt12", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_TRACEDT13, "tracedt13", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_TRACEDT14, "tracedt14", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_TRACEDT15, "tracedt15", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_TXDATA3G0, "txdata3g0", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_TXPWRIND, "txpwrind", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_UARTB1_UCTS, "uartb1_ucts", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_UARTB1_URTS, "uartb1_urts", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_UARTB1_URXD, "uartb1_urxd", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_UARTB1_UTXD, "uartb1_utxd", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_UARTB2_URXD, "uartb2_urxd", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_UARTB2_UTXD, "uartb2_utxd", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_UARTB3_UCTS, "uartb3_ucts", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_UARTB3_URTS, "uartb3_urts", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_UARTB3_URXD, "uartb3_urxd", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_UARTB3_UTXD, "uartb3_utxd", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_UARTB4_UCTS, "uartb4_ucts", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_UARTB4_URTS, "uartb4_urts", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_UARTB4_URXD, "uartb4_urxd", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_UARTB4_UTXD, "uartb4_utxd", std),
	BCM281XX_PIN_DESC(BCM281XX_PIN_VC_CAM1_SCL, "vc_cam1_scl", i2c),
	BCM281XX_PIN_DESC(BCM281XX_PIN_VC_CAM1_SDA, "vc_cam1_sda", i2c),
	BCM281XX_PIN_DESC(BCM281XX_PIN_VC_CAM2_SCL, "vc_cam2_scl", i2c),
	BCM281XX_PIN_DESC(BCM281XX_PIN_VC_CAM2_SDA, "vc_cam2_sda", i2c),
	BCM281XX_PIN_DESC(BCM281XX_PIN_VC_CAM3_SCL, "vc_cam3_scl", i2c),
	BCM281XX_PIN_DESC(BCM281XX_PIN_VC_CAM3_SDA, "vc_cam3_sda", i2c),
};

static const char * const bcm281xx_alt_groups[] = {
	"adcsync",
	"bat_rm",
	"bsc1_scl",
	"bsc1_sda",
	"bsc2_scl",
	"bsc2_sda",
	"classgpwr",
	"clk_cx8",
	"clkout_0",
	"clkout_1",
	"clkout_2",
	"clkout_3",
	"clkreq_in_0",
	"clkreq_in_1",
	"cws_sys_req1",
	"cws_sys_req2",
	"cws_sys_req3",
	"digmic1_clk",
	"digmic1_dq",
	"digmic2_clk",
	"digmic2_dq",
	"gpen13",
	"gpen14",
	"gpen15",
	"gpio00",
	"gpio01",
	"gpio02",
	"gpio03",
	"gpio04",
	"gpio05",
	"gpio06",
	"gpio07",
	"gpio08",
	"gpio09",
	"gpio10",
	"gpio11",
	"gpio12",
	"gpio13",
	"gpio14",
	"gps_pablank",
	"gps_tmark",
	"hdmi_scl",
	"hdmi_sda",
	"ic_dm",
	"ic_dp",
	"kp_col_ip_0",
	"kp_col_ip_1",
	"kp_col_ip_2",
	"kp_col_ip_3",
	"kp_row_op_0",
	"kp_row_op_1",
	"kp_row_op_2",
	"kp_row_op_3",
	"lcd_b_0",
	"lcd_b_1",
	"lcd_b_2",
	"lcd_b_3",
	"lcd_b_4",
	"lcd_b_5",
	"lcd_b_6",
	"lcd_b_7",
	"lcd_g_0",
	"lcd_g_1",
	"lcd_g_2",
	"lcd_g_3",
	"lcd_g_4",
	"lcd_g_5",
	"lcd_g_6",
	"lcd_g_7",
	"lcd_hsync",
	"lcd_oe",
	"lcd_pclk",
	"lcd_r_0",
	"lcd_r_1",
	"lcd_r_2",
	"lcd_r_3",
	"lcd_r_4",
	"lcd_r_5",
	"lcd_r_6",
	"lcd_r_7",
	"lcd_vsync",
	"mdmgpio0",
	"mdmgpio1",
	"mdmgpio2",
	"mdmgpio3",
	"mdmgpio4",
	"mdmgpio5",
	"mdmgpio6",
	"mdmgpio7",
	"mdmgpio8",
	"mphi_data_0",
	"mphi_data_1",
	"mphi_data_2",
	"mphi_data_3",
	"mphi_data_4",
	"mphi_data_5",
	"mphi_data_6",
	"mphi_data_7",
	"mphi_data_8",
	"mphi_data_9",
	"mphi_data_10",
	"mphi_data_11",
	"mphi_data_12",
	"mphi_data_13",
	"mphi_data_14",
	"mphi_data_15",
	"mphi_ha0",
	"mphi_hat0",
	"mphi_hat1",
	"mphi_hce0_n",
	"mphi_hce1_n",
	"mphi_hrd_n",
	"mphi_hwr_n",
	"mphi_run0",
	"mphi_run1",
	"mtx_scan_clk",
	"mtx_scan_data",
	"nand_ad_0",
	"nand_ad_1",
	"nand_ad_2",
	"nand_ad_3",
	"nand_ad_4",
	"nand_ad_5",
	"nand_ad_6",
	"nand_ad_7",
	"nand_ale",
	"nand_cen_0",
	"nand_cen_1",
	"nand_cle",
	"nand_oen",
	"nand_rdy_0",
	"nand_rdy_1",
	"nand_wen",
	"nand_wp",
	"pc1",
	"pc2",
	"pmu_int",
	"pmu_scl",
	"pmu_sda",
	"rfst2g_mtsloten3g",
	"rgmii_0_rx_ctl",
	"rgmii_0_rxc",
	"rgmii_0_rxd_0",
	"rgmii_0_rxd_1",
	"rgmii_0_rxd_2",
	"rgmii_0_rxd_3",
	"rgmii_0_tx_ctl",
	"rgmii_0_txc",
	"rgmii_0_txd_0",
	"rgmii_0_txd_1",
	"rgmii_0_txd_2",
	"rgmii_0_txd_3",
	"rgmii_1_rx_ctl",
	"rgmii_1_rxc",
	"rgmii_1_rxd_0",
	"rgmii_1_rxd_1",
	"rgmii_1_rxd_2",
	"rgmii_1_rxd_3",
	"rgmii_1_tx_ctl",
	"rgmii_1_txc",
	"rgmii_1_txd_0",
	"rgmii_1_txd_1",
	"rgmii_1_txd_2",
	"rgmii_1_txd_3",
	"rgmii_gpio_0",
	"rgmii_gpio_1",
	"rgmii_gpio_2",
	"rgmii_gpio_3",
	"rtxdata2g_txdata3g1",
	"rtxen2g_txdata3g2",
	"rxdata3g0",
	"rxdata3g1",
	"rxdata3g2",
	"sdio1_clk",
	"sdio1_cmd",
	"sdio1_data_0",
	"sdio1_data_1",
	"sdio1_data_2",
	"sdio1_data_3",
	"sdio4_clk",
	"sdio4_cmd",
	"sdio4_data_0",
	"sdio4_data_1",
	"sdio4_data_2",
	"sdio4_data_3",
	"sim_clk",
	"sim_data",
	"sim_det",
	"sim_resetn",
	"sim2_clk",
	"sim2_data",
	"sim2_det",
	"sim2_resetn",
	"sri_c",
	"sri_d",
	"sri_e",
	"ssp_extclk",
	"ssp0_clk",
	"ssp0_fs",
	"ssp0_rxd",
	"ssp0_txd",
	"ssp2_clk",
	"ssp2_fs_0",
	"ssp2_fs_1",
	"ssp2_fs_2",
	"ssp2_fs_3",
	"ssp2_rxd_0",
	"ssp2_rxd_1",
	"ssp2_txd_0",
	"ssp2_txd_1",
	"ssp3_clk",
	"ssp3_fs",
	"ssp3_rxd",
	"ssp3_txd",
	"ssp4_clk",
	"ssp4_fs",
	"ssp4_rxd",
	"ssp4_txd",
	"ssp5_clk",
	"ssp5_fs",
	"ssp5_rxd",
	"ssp5_txd",
	"ssp6_clk",
	"ssp6_fs",
	"ssp6_rxd",
	"ssp6_txd",
	"stat_1",
	"stat_2",
	"sysclken",
	"traceclk",
	"tracedt00",
	"tracedt01",
	"tracedt02",
	"tracedt03",
	"tracedt04",
	"tracedt05",
	"tracedt06",
	"tracedt07",
	"tracedt08",
	"tracedt09",
	"tracedt10",
	"tracedt11",
	"tracedt12",
	"tracedt13",
	"tracedt14",
	"tracedt15",
	"txdata3g0",
	"txpwrind",
	"uartb1_ucts",
	"uartb1_urts",
	"uartb1_urxd",
	"uartb1_utxd",
	"uartb2_urxd",
	"uartb2_utxd",
	"uartb3_ucts",
	"uartb3_urts",
	"uartb3_urxd",
	"uartb3_utxd",
	"uartb4_ucts",
	"uartb4_urts",
	"uartb4_urxd",
	"uartb4_utxd",
	"vc_cam1_scl",
	"vc_cam1_sda",
	"vc_cam2_scl",
	"vc_cam2_sda",
	"vc_cam3_scl",
	"vc_cam3_sda",
};

/* Every pin can implement all ALT1-ALT4 functions */
#define BCM281XX_PIN_FUNCTION(fcn_name)			\
{							\
	.name = #fcn_name,				\
	.groups = bcm281xx_alt_groups,			\
	.ngroups = ARRAY_SIZE(bcm281xx_alt_groups),	\
}

static const struct bcm281xx_pin_function bcm281xx_functions[] = {
	BCM281XX_PIN_FUNCTION(alt1),
	BCM281XX_PIN_FUNCTION(alt2),
	BCM281XX_PIN_FUNCTION(alt3),
	BCM281XX_PIN_FUNCTION(alt4),
};

static struct bcm281xx_pinctrl_data bcm281xx_pinctrl = {
	.pins = bcm281xx_pinctrl_pins,
	.npins = ARRAY_SIZE(bcm281xx_pinctrl_pins),
	.functions = bcm281xx_functions,
	.nfunctions = ARRAY_SIZE(bcm281xx_functions),
};

static inline enum bcm281xx_pin_type pin_type_get(struct pinctrl_dev *pctldev,
						  unsigned pin)
{
	struct bcm281xx_pinctrl_data *pdata = pinctrl_dev_get_drvdata(pctldev);

	if (pin >= pdata->npins)
		return BCM281XX_PIN_TYPE_UNKNOWN;

	return *(enum bcm281xx_pin_type *)(pdata->pins[pin].drv_data);
}

#define BCM281XX_PIN_SHIFT(type, param) \
	(BCM281XX_ ## type ## _PIN_REG_ ## param ## _SHIFT)

#define BCM281XX_PIN_MASK(type, param) \
	(BCM281XX_ ## type ## _PIN_REG_ ## param ## _MASK)

/*
 * This helper function is used to build up the value and mask used to write to
 * a pin register, but does not actually write to the register.
 */
static inline void bcm281xx_pin_update(u32 *reg_val, u32 *reg_mask,
				       u32 param_val, u32 param_shift,
				       u32 param_mask)
{
	*reg_val &= ~param_mask;
	*reg_val |= (param_val << param_shift) & param_mask;
	*reg_mask |= param_mask;
}

static const struct regmap_config bcm281xx_pinctrl_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = BCM281XX_PIN_VC_CAM3_SDA * 4,
};

static int bcm281xx_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct bcm281xx_pinctrl_data *pdata = pinctrl_dev_get_drvdata(pctldev);

	return pdata->npins;
}

static const char *bcm281xx_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						   unsigned group)
{
	struct bcm281xx_pinctrl_data *pdata = pinctrl_dev_get_drvdata(pctldev);

	return pdata->pins[group].name;
}

static int bcm281xx_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					   unsigned group,
					   const unsigned **pins,
					   unsigned *num_pins)
{
	struct bcm281xx_pinctrl_data *pdata = pinctrl_dev_get_drvdata(pctldev);

	*pins = &pdata->pins[group].number;
	*num_pins = 1;

	return 0;
}

static void bcm281xx_pinctrl_pin_dbg_show(struct pinctrl_dev *pctldev,
					  struct seq_file *s,
					  unsigned offset)
{
	seq_printf(s, " %s", dev_name(pctldev->dev));
}

static const struct pinctrl_ops bcm281xx_pinctrl_ops = {
	.get_groups_count = bcm281xx_pinctrl_get_groups_count,
	.get_group_name = bcm281xx_pinctrl_get_group_name,
	.get_group_pins = bcm281xx_pinctrl_get_group_pins,
	.pin_dbg_show = bcm281xx_pinctrl_pin_dbg_show,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinctrl_utils_free_map,
};

static int bcm281xx_pinctrl_get_fcns_count(struct pinctrl_dev *pctldev)
{
	struct bcm281xx_pinctrl_data *pdata = pinctrl_dev_get_drvdata(pctldev);

	return pdata->nfunctions;
}

static const char *bcm281xx_pinctrl_get_fcn_name(struct pinctrl_dev *pctldev,
						 unsigned function)
{
	struct bcm281xx_pinctrl_data *pdata = pinctrl_dev_get_drvdata(pctldev);

	return pdata->functions[function].name;
}

static int bcm281xx_pinctrl_get_fcn_groups(struct pinctrl_dev *pctldev,
					   unsigned function,
					   const char * const **groups,
					   unsigned * const num_groups)
{
	struct bcm281xx_pinctrl_data *pdata = pinctrl_dev_get_drvdata(pctldev);

	*groups = pdata->functions[function].groups;
	*num_groups = pdata->functions[function].ngroups;

	return 0;
}

static int bcm281xx_pinmux_set(struct pinctrl_dev *pctldev,
			       unsigned function,
			       unsigned group)
{
	struct bcm281xx_pinctrl_data *pdata = pinctrl_dev_get_drvdata(pctldev);
	const struct bcm281xx_pin_function *f = &pdata->functions[function];
	u32 offset = 4 * pdata->pins[group].number;
	int rc = 0;

	dev_dbg(pctldev->dev,
		"%s(): Enable function %s (%d) of pin %s (%d) @offset 0x%x.\n",
		__func__, f->name, function, pdata->pins[group].name,
		pdata->pins[group].number, offset);

	rc = regmap_update_bits(pdata->regmap, offset,
		BCM281XX_PIN_REG_F_SEL_MASK,
		function << BCM281XX_PIN_REG_F_SEL_SHIFT);
	if (rc)
		dev_err(pctldev->dev,
			"Error updating register for pin %s (%d).\n",
			pdata->pins[group].name, pdata->pins[group].number);

	return rc;
}

static const struct pinmux_ops bcm281xx_pinctrl_pinmux_ops = {
	.get_functions_count = bcm281xx_pinctrl_get_fcns_count,
	.get_function_name = bcm281xx_pinctrl_get_fcn_name,
	.get_function_groups = bcm281xx_pinctrl_get_fcn_groups,
	.set_mux = bcm281xx_pinmux_set,
};

static int bcm281xx_pinctrl_pin_config_get(struct pinctrl_dev *pctldev,
					   unsigned pin,
					   unsigned long *config)
{
	return -ENOTSUPP;
}


/* Goes through the configs and update register val/mask */
static int bcm281xx_std_pin_update(struct pinctrl_dev *pctldev,
				   unsigned pin,
				   unsigned long *configs,
				   unsigned num_configs,
				   u32 *val,
				   u32 *mask)
{
	struct bcm281xx_pinctrl_data *pdata = pinctrl_dev_get_drvdata(pctldev);
	int i;
	enum pin_config_param param;
	u32 arg;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			arg = (arg >= 1 ? 1 : 0);
			bcm281xx_pin_update(val, mask, arg,
				BCM281XX_PIN_SHIFT(STD, HYST),
				BCM281XX_PIN_MASK(STD, HYST));
			break;
		/*
		 * The pin bias can only be one of pull-up, pull-down, or
		 * disable.  The user does not need to specify a value for the
		 * property, and the default value from pinconf-generic is
		 * ignored.
		 */
		case PIN_CONFIG_BIAS_DISABLE:
			bcm281xx_pin_update(val, mask, 0,
				BCM281XX_PIN_SHIFT(STD, PULL_UP),
				BCM281XX_PIN_MASK(STD, PULL_UP));
			bcm281xx_pin_update(val, mask, 0,
				BCM281XX_PIN_SHIFT(STD, PULL_DN),
				BCM281XX_PIN_MASK(STD, PULL_DN));
			break;

		case PIN_CONFIG_BIAS_PULL_UP:
			bcm281xx_pin_update(val, mask, 1,
				BCM281XX_PIN_SHIFT(STD, PULL_UP),
				BCM281XX_PIN_MASK(STD, PULL_UP));
			bcm281xx_pin_update(val, mask, 0,
				BCM281XX_PIN_SHIFT(STD, PULL_DN),
				BCM281XX_PIN_MASK(STD, PULL_DN));
			break;

		case PIN_CONFIG_BIAS_PULL_DOWN:
			bcm281xx_pin_update(val, mask, 0,
				BCM281XX_PIN_SHIFT(STD, PULL_UP),
				BCM281XX_PIN_MASK(STD, PULL_UP));
			bcm281xx_pin_update(val, mask, 1,
				BCM281XX_PIN_SHIFT(STD, PULL_DN),
				BCM281XX_PIN_MASK(STD, PULL_DN));
			break;

		case PIN_CONFIG_SLEW_RATE:
			arg = (arg >= 1 ? 1 : 0);
			bcm281xx_pin_update(val, mask, arg,
				BCM281XX_PIN_SHIFT(STD, SLEW),
				BCM281XX_PIN_MASK(STD, SLEW));
			break;

		case PIN_CONFIG_INPUT_ENABLE:
			/* inversed since register is for input _disable_ */
			arg = (arg >= 1 ? 0 : 1);
			bcm281xx_pin_update(val, mask, arg,
				BCM281XX_PIN_SHIFT(STD, INPUT_DIS),
				BCM281XX_PIN_MASK(STD, INPUT_DIS));
			break;

		case PIN_CONFIG_DRIVE_STRENGTH:
			/* Valid range is 2-16 mA, even numbers only */
			if ((arg < 2) || (arg > 16) || (arg % 2)) {
				dev_err(pctldev->dev,
					"Invalid Drive Strength value (%d) for "
					"pin %s (%d). Valid values are "
					"(2..16) mA, even numbers only.\n",
					arg, pdata->pins[pin].name, pin);
				return -EINVAL;
			}
			bcm281xx_pin_update(val, mask, (arg/2)-1,
				BCM281XX_PIN_SHIFT(STD, DRV_STR),
				BCM281XX_PIN_MASK(STD, DRV_STR));
			break;

		default:
			dev_err(pctldev->dev,
				"Unrecognized pin config %d for pin %s (%d).\n",
				param, pdata->pins[pin].name, pin);
			return -EINVAL;

		} /* switch config */
	} /* for each config */

	return 0;
}

/*
 * The pull-up strength for an I2C pin is represented by bits 4-6 in the
 * register with the following mapping:
 *   0b000: No pull-up
 *   0b001: 1200 Ohm
 *   0b010: 1800 Ohm
 *   0b011: 720 Ohm
 *   0b100: 2700 Ohm
 *   0b101: 831 Ohm
 *   0b110: 1080 Ohm
 *   0b111: 568 Ohm
 * This array maps pull-up strength in Ohms to register values (1+index).
 */
static const u16 bcm281xx_pullup_map[] = {
	1200, 1800, 720, 2700, 831, 1080, 568
};

/* Goes through the configs and update register val/mask */
static int bcm281xx_i2c_pin_update(struct pinctrl_dev *pctldev,
				   unsigned pin,
				   unsigned long *configs,
				   unsigned num_configs,
				   u32 *val,
				   u32 *mask)
{
	struct bcm281xx_pinctrl_data *pdata = pinctrl_dev_get_drvdata(pctldev);
	int i, j;
	enum pin_config_param param;
	u32 arg;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_PULL_UP:
			for (j = 0; j < ARRAY_SIZE(bcm281xx_pullup_map); j++)
				if (bcm281xx_pullup_map[j] == arg)
					break;

			if (j == ARRAY_SIZE(bcm281xx_pullup_map)) {
				dev_err(pctldev->dev,
					"Invalid pull-up value (%d) for pin %s "
					"(%d). Valid values are 568, 720, 831, "
					"1080, 1200, 1800, 2700 Ohms.\n",
					arg, pdata->pins[pin].name, pin);
				return -EINVAL;
			}

			bcm281xx_pin_update(val, mask, j+1,
				BCM281XX_PIN_SHIFT(I2C, PULL_UP_STR),
				BCM281XX_PIN_MASK(I2C, PULL_UP_STR));
			break;

		case PIN_CONFIG_BIAS_DISABLE:
			bcm281xx_pin_update(val, mask, 0,
				BCM281XX_PIN_SHIFT(I2C, PULL_UP_STR),
				BCM281XX_PIN_MASK(I2C, PULL_UP_STR));
			break;

		case PIN_CONFIG_SLEW_RATE:
			arg = (arg >= 1 ? 1 : 0);
			bcm281xx_pin_update(val, mask, arg,
				BCM281XX_PIN_SHIFT(I2C, SLEW),
				BCM281XX_PIN_MASK(I2C, SLEW));
			break;

		case PIN_CONFIG_INPUT_ENABLE:
			/* inversed since register is for input _disable_ */
			arg = (arg >= 1 ? 0 : 1);
			bcm281xx_pin_update(val, mask, arg,
				BCM281XX_PIN_SHIFT(I2C, INPUT_DIS),
				BCM281XX_PIN_MASK(I2C, INPUT_DIS));
			break;

		default:
			dev_err(pctldev->dev,
				"Unrecognized pin config %d for pin %s (%d).\n",
				param, pdata->pins[pin].name, pin);
			return -EINVAL;

		} /* switch config */
	} /* for each config */

	return 0;
}

/* Goes through the configs and update register val/mask */
static int bcm281xx_hdmi_pin_update(struct pinctrl_dev *pctldev,
				    unsigned pin,
				    unsigned long *configs,
				    unsigned num_configs,
				    u32 *val,
				    u32 *mask)
{
	struct bcm281xx_pinctrl_data *pdata = pinctrl_dev_get_drvdata(pctldev);
	int i;
	enum pin_config_param param;
	u32 arg;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_SLEW_RATE:
			arg = (arg >= 1 ? 1 : 0);
			bcm281xx_pin_update(val, mask, arg,
				BCM281XX_PIN_SHIFT(HDMI, MODE),
				BCM281XX_PIN_MASK(HDMI, MODE));
			break;

		case PIN_CONFIG_INPUT_ENABLE:
			/* inversed since register is for input _disable_ */
			arg = (arg >= 1 ? 0 : 1);
			bcm281xx_pin_update(val, mask, arg,
				BCM281XX_PIN_SHIFT(HDMI, INPUT_DIS),
				BCM281XX_PIN_MASK(HDMI, INPUT_DIS));
			break;

		default:
			dev_err(pctldev->dev,
				"Unrecognized pin config %d for pin %s (%d).\n",
				param, pdata->pins[pin].name, pin);
			return -EINVAL;

		} /* switch config */
	} /* for each config */

	return 0;
}

static int bcm281xx_pinctrl_pin_config_set(struct pinctrl_dev *pctldev,
					   unsigned pin,
					   unsigned long *configs,
					   unsigned num_configs)
{
	struct bcm281xx_pinctrl_data *pdata = pinctrl_dev_get_drvdata(pctldev);
	enum bcm281xx_pin_type pin_type;
	u32 offset = 4 * pin;
	u32 cfg_val, cfg_mask;
	int rc;

	cfg_val = 0;
	cfg_mask = 0;
	pin_type = pin_type_get(pctldev, pin);

	/* Different pins have different configuration options */
	switch (pin_type) {
	case BCM281XX_PIN_TYPE_STD:
		rc = bcm281xx_std_pin_update(pctldev, pin, configs,
			num_configs, &cfg_val, &cfg_mask);
		break;

	case BCM281XX_PIN_TYPE_I2C:
		rc = bcm281xx_i2c_pin_update(pctldev, pin, configs,
			num_configs, &cfg_val, &cfg_mask);
		break;

	case BCM281XX_PIN_TYPE_HDMI:
		rc = bcm281xx_hdmi_pin_update(pctldev, pin, configs,
			num_configs, &cfg_val, &cfg_mask);
		break;

	default:
		dev_err(pctldev->dev, "Unknown pin type for pin %s (%d).\n",
			pdata->pins[pin].name, pin);
		return -EINVAL;

	} /* switch pin type */

	if (rc)
		return rc;

	dev_dbg(pctldev->dev,
		"%s(): Set pin %s (%d) with config 0x%x, mask 0x%x\n",
		__func__, pdata->pins[pin].name, pin, cfg_val, cfg_mask);

	rc = regmap_update_bits(pdata->regmap, offset, cfg_mask, cfg_val);
	if (rc) {
		dev_err(pctldev->dev,
			"Error updating register for pin %s (%d).\n",
			pdata->pins[pin].name, pin);
		return rc;
	}

	return 0;
}

static const struct pinconf_ops bcm281xx_pinctrl_pinconf_ops = {
	.pin_config_get = bcm281xx_pinctrl_pin_config_get,
	.pin_config_set = bcm281xx_pinctrl_pin_config_set,
};

static struct pinctrl_desc bcm281xx_pinctrl_desc = {
	/* name, pins, npins members initialized in probe function */
	.pctlops = &bcm281xx_pinctrl_ops,
	.pmxops = &bcm281xx_pinctrl_pinmux_ops,
	.confops = &bcm281xx_pinctrl_pinconf_ops,
	.owner = THIS_MODULE,
};

static int __init bcm281xx_pinctrl_probe(struct platform_device *pdev)
{
	struct bcm281xx_pinctrl_data *pdata = &bcm281xx_pinctrl;
	struct pinctrl_dev *pctl;

	/* So far We can assume there is only 1 bank of registers */
	pdata->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pdata->reg_base)) {
		dev_err(&pdev->dev, "Failed to ioremap MEM resource\n");
		return PTR_ERR(pdata->reg_base);
	}

	/* Initialize the dynamic part of pinctrl_desc */
	pdata->regmap = devm_regmap_init_mmio(&pdev->dev, pdata->reg_base,
		&bcm281xx_pinctrl_regmap_config);
	if (IS_ERR(pdata->regmap)) {
		dev_err(&pdev->dev, "Regmap MMIO init failed.\n");
		return -ENODEV;
	}

	bcm281xx_pinctrl_desc.name = dev_name(&pdev->dev);
	bcm281xx_pinctrl_desc.pins = bcm281xx_pinctrl.pins;
	bcm281xx_pinctrl_desc.npins = bcm281xx_pinctrl.npins;

	pctl = devm_pinctrl_register(&pdev->dev, &bcm281xx_pinctrl_desc, pdata);
	if (IS_ERR(pctl)) {
		dev_err(&pdev->dev, "Failed to register pinctrl\n");
		return PTR_ERR(pctl);
	}

	platform_set_drvdata(pdev, pdata);

	return 0;
}

static const struct of_device_id bcm281xx_pinctrl_of_match[] = {
	{ .compatible = "brcm,bcm11351-pinctrl", },
	{ },
};

static struct platform_driver bcm281xx_pinctrl_driver = {
	.driver = {
		.name = "bcm281xx-pinctrl",
		.of_match_table = bcm281xx_pinctrl_of_match,
	},
};
builtin_platform_driver_probe(bcm281xx_pinctrl_driver, bcm281xx_pinctrl_probe);

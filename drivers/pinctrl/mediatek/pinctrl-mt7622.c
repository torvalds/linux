// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017-2018 MediaTek Inc.
 *
 * Author: Sean Wang <sean.wang@mediatek.com>
 *
 */

#include "pinctrl-moore.h"

#define MT7622_PIN(_number, _name)					\
	MTK_PIN(_number, _name, 1, _number, DRV_GRP0)

static const struct mtk_pin_field_calc mt7622_pin_mode_range[] = {
	PIN_FIELD(0, 0, 0x320, 0x10, 16, 4),
	PIN_FIELD(1, 4, 0x3a0, 0x10, 16, 4),
	PIN_FIELD(5, 5, 0x320, 0x10, 0, 4),
	PINS_FIELD(6, 7, 0x300, 0x10, 4, 4),
	PIN_FIELD(8, 9, 0x350, 0x10, 20, 4),
	PINS_FIELD(10, 13, 0x300, 0x10, 8, 4),
	PIN_FIELD(14, 15, 0x320, 0x10, 4, 4),
	PIN_FIELD(16, 17, 0x320, 0x10, 20, 4),
	PIN_FIELD(18, 21, 0x310, 0x10, 16, 4),
	PIN_FIELD(22, 22, 0x380, 0x10, 16, 4),
	PINS_FIELD(23, 24, 0x300, 0x10, 24, 4),
	PINS_FIELD(25, 36, 0x300, 0x10, 12, 4),
	PINS_FIELD(37, 50, 0x300, 0x10, 20, 4),
	PIN_FIELD(51, 70, 0x330, 0x10, 4, 4),
	PINS_FIELD(71, 72, 0x300, 0x10, 16, 4),
	PIN_FIELD(73, 76, 0x310, 0x10, 0, 4),
	PIN_FIELD(77, 77, 0x320, 0x10, 28, 4),
	PIN_FIELD(78, 78, 0x320, 0x10, 12, 4),
	PIN_FIELD(79, 82, 0x3a0, 0x10, 0, 4),
	PIN_FIELD(83, 83, 0x350, 0x10, 28, 4),
	PIN_FIELD(84, 84, 0x330, 0x10, 0, 4),
	PIN_FIELD(85, 90, 0x360, 0x10, 4, 4),
	PIN_FIELD(91, 94, 0x390, 0x10, 16, 4),
	PIN_FIELD(95, 97, 0x380, 0x10, 20, 4),
	PIN_FIELD(98, 101, 0x390, 0x10, 0, 4),
	PIN_FIELD(102, 102, 0x360, 0x10, 0, 4),
};

static const struct mtk_pin_field_calc mt7622_pin_dir_range[] = {
	PIN_FIELD(0, 102, 0x0, 0x10, 0, 1),
};

static const struct mtk_pin_field_calc mt7622_pin_di_range[] = {
	PIN_FIELD(0, 102, 0x200, 0x10, 0, 1),
};

static const struct mtk_pin_field_calc mt7622_pin_do_range[] = {
	PIN_FIELD(0, 102, 0x100, 0x10, 0, 1),
};

static const struct mtk_pin_field_calc mt7622_pin_sr_range[] = {
	PIN_FIELD(0, 31, 0x910, 0x10, 0, 1),
	PIN_FIELD(32, 50, 0xa10, 0x10, 0, 1),
	PIN_FIELD(51, 70, 0x810, 0x10, 0, 1),
	PIN_FIELD(71, 72, 0xb10, 0x10, 0, 1),
	PIN_FIELD(73, 86, 0xb10, 0x10, 4, 1),
	PIN_FIELD(87, 90, 0xc10, 0x10, 0, 1),
	PIN_FIELD(91, 102, 0xb10, 0x10, 18, 1),
};

static const struct mtk_pin_field_calc mt7622_pin_smt_range[] = {
	PIN_FIELD(0, 31, 0x920, 0x10, 0, 1),
	PIN_FIELD(32, 50, 0xa20, 0x10, 0, 1),
	PIN_FIELD(51, 70, 0x820, 0x10, 0, 1),
	PIN_FIELD(71, 72, 0xb20, 0x10, 0, 1),
	PIN_FIELD(73, 86, 0xb20, 0x10, 4, 1),
	PIN_FIELD(87, 90, 0xc20, 0x10, 0, 1),
	PIN_FIELD(91, 102, 0xb20, 0x10, 18, 1),
};

static const struct mtk_pin_field_calc mt7622_pin_pu_range[] = {
	PIN_FIELD(0, 31, 0x930, 0x10, 0, 1),
	PIN_FIELD(32, 50, 0xa30, 0x10, 0, 1),
	PIN_FIELD(51, 70, 0x830, 0x10, 0, 1),
	PIN_FIELD(71, 72, 0xb30, 0x10, 0, 1),
	PIN_FIELD(73, 86, 0xb30, 0x10, 4, 1),
	PIN_FIELD(87, 90, 0xc30, 0x10, 0, 1),
	PIN_FIELD(91, 102, 0xb30, 0x10, 18, 1),
};

static const struct mtk_pin_field_calc mt7622_pin_pd_range[] = {
	PIN_FIELD(0, 31, 0x940, 0x10, 0, 1),
	PIN_FIELD(32, 50, 0xa40, 0x10, 0, 1),
	PIN_FIELD(51, 70, 0x840, 0x10, 0, 1),
	PIN_FIELD(71, 72, 0xb40, 0x10, 0, 1),
	PIN_FIELD(73, 86, 0xb40, 0x10, 4, 1),
	PIN_FIELD(87, 90, 0xc40, 0x10, 0, 1),
	PIN_FIELD(91, 102, 0xb40, 0x10, 18, 1),
};

static const struct mtk_pin_field_calc mt7622_pin_e4_range[] = {
	PIN_FIELD(0, 31, 0x960, 0x10, 0, 1),
	PIN_FIELD(32, 50, 0xa60, 0x10, 0, 1),
	PIN_FIELD(51, 70, 0x860, 0x10, 0, 1),
	PIN_FIELD(71, 72, 0xb60, 0x10, 0, 1),
	PIN_FIELD(73, 86, 0xb60, 0x10, 4, 1),
	PIN_FIELD(87, 90, 0xc60, 0x10, 0, 1),
	PIN_FIELD(91, 102, 0xb60, 0x10, 18, 1),
};

static const struct mtk_pin_field_calc mt7622_pin_e8_range[] = {
	PIN_FIELD(0, 31, 0x970, 0x10, 0, 1),
	PIN_FIELD(32, 50, 0xa70, 0x10, 0, 1),
	PIN_FIELD(51, 70, 0x870, 0x10, 0, 1),
	PIN_FIELD(71, 72, 0xb70, 0x10, 0, 1),
	PIN_FIELD(73, 86, 0xb70, 0x10, 4, 1),
	PIN_FIELD(87, 90, 0xc70, 0x10, 0, 1),
	PIN_FIELD(91, 102, 0xb70, 0x10, 18, 1),
};

static const struct mtk_pin_field_calc mt7622_pin_tdsel_range[] = {
	PIN_FIELD(0, 31, 0x980, 0x4, 0, 4),
	PIN_FIELD(32, 50, 0xa80, 0x4, 0, 4),
	PIN_FIELD(51, 70, 0x880, 0x4, 0, 4),
	PIN_FIELD(71, 72, 0xb80, 0x4, 0, 4),
	PIN_FIELD(73, 86, 0xb80, 0x4, 16, 4),
	PIN_FIELD(87, 90, 0xc80, 0x4, 0, 4),
	PIN_FIELD(91, 102, 0xb88, 0x4, 8, 4),
};

static const struct mtk_pin_field_calc mt7622_pin_rdsel_range[] = {
	PIN_FIELD(0, 31, 0x990, 0x4, 0, 6),
	PIN_FIELD(32, 50, 0xa90, 0x4, 0, 6),
	PIN_FIELD(51, 58, 0x890, 0x4, 0, 6),
	PIN_FIELD(59, 60, 0x894, 0x4, 28, 6),
	PIN_FIELD(61, 62, 0x894, 0x4, 16, 6),
	PIN_FIELD(63, 66, 0x898, 0x4, 8, 6),
	PIN_FIELD(67, 68, 0x89c, 0x4, 12, 6),
	PIN_FIELD(69, 70, 0x89c, 0x4, 0, 6),
	PIN_FIELD(71, 72, 0xb90, 0x4, 0, 6),
	PIN_FIELD(73, 86, 0xb90, 0x4, 24, 6),
	PIN_FIELD(87, 90, 0xc90, 0x4, 0, 6),
	PIN_FIELD(91, 102, 0xb9c, 0x4, 12, 6),
};

static const struct mtk_pin_reg_calc mt7622_reg_cals[PINCTRL_PIN_REG_MAX] = {
	[PINCTRL_PIN_REG_MODE] = MTK_RANGE(mt7622_pin_mode_range),
	[PINCTRL_PIN_REG_DIR] = MTK_RANGE(mt7622_pin_dir_range),
	[PINCTRL_PIN_REG_DI] = MTK_RANGE(mt7622_pin_di_range),
	[PINCTRL_PIN_REG_DO] = MTK_RANGE(mt7622_pin_do_range),
	[PINCTRL_PIN_REG_SR] = MTK_RANGE(mt7622_pin_sr_range),
	[PINCTRL_PIN_REG_SMT] = MTK_RANGE(mt7622_pin_smt_range),
	[PINCTRL_PIN_REG_PU] = MTK_RANGE(mt7622_pin_pu_range),
	[PINCTRL_PIN_REG_PD] = MTK_RANGE(mt7622_pin_pd_range),
	[PINCTRL_PIN_REG_E4] = MTK_RANGE(mt7622_pin_e4_range),
	[PINCTRL_PIN_REG_E8] = MTK_RANGE(mt7622_pin_e8_range),
	[PINCTRL_PIN_REG_TDSEL] = MTK_RANGE(mt7622_pin_tdsel_range),
	[PINCTRL_PIN_REG_RDSEL] = MTK_RANGE(mt7622_pin_rdsel_range),
};

static const struct mtk_pin_desc mt7622_pins[] = {
	MT7622_PIN(0, "GPIO_A"),
	MT7622_PIN(1, "I2S1_IN"),
	MT7622_PIN(2, "I2S1_OUT"),
	MT7622_PIN(3, "I2S_BCLK"),
	MT7622_PIN(4, "I2S_WS"),
	MT7622_PIN(5, "I2S_MCLK"),
	MT7622_PIN(6, "TXD0"),
	MT7622_PIN(7, "RXD0"),
	MT7622_PIN(8, "SPI_WP"),
	MT7622_PIN(9, "SPI_HOLD"),
	MT7622_PIN(10, "SPI_CLK"),
	MT7622_PIN(11, "SPI_MOSI"),
	MT7622_PIN(12, "SPI_MISO"),
	MT7622_PIN(13, "SPI_CS"),
	MT7622_PIN(14, "I2C_SDA"),
	MT7622_PIN(15, "I2C_SCL"),
	MT7622_PIN(16, "I2S2_IN"),
	MT7622_PIN(17, "I2S3_IN"),
	MT7622_PIN(18, "I2S4_IN"),
	MT7622_PIN(19, "I2S2_OUT"),
	MT7622_PIN(20, "I2S3_OUT"),
	MT7622_PIN(21, "I2S4_OUT"),
	MT7622_PIN(22, "GPIO_B"),
	MT7622_PIN(23, "MDC"),
	MT7622_PIN(24, "MDIO"),
	MT7622_PIN(25, "G2_TXD0"),
	MT7622_PIN(26, "G2_TXD1"),
	MT7622_PIN(27, "G2_TXD2"),
	MT7622_PIN(28, "G2_TXD3"),
	MT7622_PIN(29, "G2_TXEN"),
	MT7622_PIN(30, "G2_TXC"),
	MT7622_PIN(31, "G2_RXD0"),
	MT7622_PIN(32, "G2_RXD1"),
	MT7622_PIN(33, "G2_RXD2"),
	MT7622_PIN(34, "G2_RXD3"),
	MT7622_PIN(35, "G2_RXDV"),
	MT7622_PIN(36, "G2_RXC"),
	MT7622_PIN(37, "NCEB"),
	MT7622_PIN(38, "NWEB"),
	MT7622_PIN(39, "NREB"),
	MT7622_PIN(40, "NDL4"),
	MT7622_PIN(41, "NDL5"),
	MT7622_PIN(42, "NDL6"),
	MT7622_PIN(43, "NDL7"),
	MT7622_PIN(44, "NRB"),
	MT7622_PIN(45, "NCLE"),
	MT7622_PIN(46, "NALE"),
	MT7622_PIN(47, "NDL0"),
	MT7622_PIN(48, "NDL1"),
	MT7622_PIN(49, "NDL2"),
	MT7622_PIN(50, "NDL3"),
	MT7622_PIN(51, "MDI_TP_P0"),
	MT7622_PIN(52, "MDI_TN_P0"),
	MT7622_PIN(53, "MDI_RP_P0"),
	MT7622_PIN(54, "MDI_RN_P0"),
	MT7622_PIN(55, "MDI_TP_P1"),
	MT7622_PIN(56, "MDI_TN_P1"),
	MT7622_PIN(57, "MDI_RP_P1"),
	MT7622_PIN(58, "MDI_RN_P1"),
	MT7622_PIN(59, "MDI_RP_P2"),
	MT7622_PIN(60, "MDI_RN_P2"),
	MT7622_PIN(61, "MDI_TP_P2"),
	MT7622_PIN(62, "MDI_TN_P2"),
	MT7622_PIN(63, "MDI_TP_P3"),
	MT7622_PIN(64, "MDI_TN_P3"),
	MT7622_PIN(65, "MDI_RP_P3"),
	MT7622_PIN(66, "MDI_RN_P3"),
	MT7622_PIN(67, "MDI_RP_P4"),
	MT7622_PIN(68, "MDI_RN_P4"),
	MT7622_PIN(69, "MDI_TP_P4"),
	MT7622_PIN(70, "MDI_TN_P4"),
	MT7622_PIN(71, "PMIC_SCL"),
	MT7622_PIN(72, "PMIC_SDA"),
	MT7622_PIN(73, "SPIC1_CLK"),
	MT7622_PIN(74, "SPIC1_MOSI"),
	MT7622_PIN(75, "SPIC1_MISO"),
	MT7622_PIN(76, "SPIC1_CS"),
	MT7622_PIN(77, "GPIO_D"),
	MT7622_PIN(78, "WATCHDOG"),
	MT7622_PIN(79, "RTS3_N"),
	MT7622_PIN(80, "CTS3_N"),
	MT7622_PIN(81, "TXD3"),
	MT7622_PIN(82, "RXD3"),
	MT7622_PIN(83, "PERST0_N"),
	MT7622_PIN(84, "PERST1_N"),
	MT7622_PIN(85, "WLED_N"),
	MT7622_PIN(86, "EPHY_LED0_N"),
	MT7622_PIN(87, "AUXIN0"),
	MT7622_PIN(88, "AUXIN1"),
	MT7622_PIN(89, "AUXIN2"),
	MT7622_PIN(90, "AUXIN3"),
	MT7622_PIN(91, "TXD4"),
	MT7622_PIN(92, "RXD4"),
	MT7622_PIN(93, "RTS4_N"),
	MT7622_PIN(94, "CTS4_N"),
	MT7622_PIN(95, "PWM1"),
	MT7622_PIN(96, "PWM2"),
	MT7622_PIN(97, "PWM3"),
	MT7622_PIN(98, "PWM4"),
	MT7622_PIN(99, "PWM5"),
	MT7622_PIN(100, "PWM6"),
	MT7622_PIN(101, "PWM7"),
	MT7622_PIN(102, "GPIO_E"),
};

/* List all groups consisting of these pins dedicated to the enablement of
 * certain hardware block and the corresponding mode for all of the pins. The
 * hardware probably has multiple combinations of these pinouts.
 */

/* EMMC */
static int mt7622_emmc_pins[] = { 40, 41, 42, 43, 44, 45, 47, 48, 49, 50, };
static int mt7622_emmc_funcs[] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, };

static int mt7622_emmc_rst_pins[] = { 37, };
static int mt7622_emmc_rst_funcs[] = { 1, };

/* LED for EPHY */
static int mt7622_ephy_leds_pins[] = { 86, 91, 92, 93, 94, };
static int mt7622_ephy_leds_funcs[] = { 0, 0, 0, 0, 0, };
static int mt7622_ephy0_led_pins[] = { 86, };
static int mt7622_ephy0_led_funcs[] = { 0, };
static int mt7622_ephy1_led_pins[] = { 91, };
static int mt7622_ephy1_led_funcs[] = { 2, };
static int mt7622_ephy2_led_pins[] = { 92, };
static int mt7622_ephy2_led_funcs[] = { 2, };
static int mt7622_ephy3_led_pins[] = { 93, };
static int mt7622_ephy3_led_funcs[] = { 2, };
static int mt7622_ephy4_led_pins[] = { 94, };
static int mt7622_ephy4_led_funcs[] = { 2, };

/* Embedded Switch */
static int mt7622_esw_pins[] = { 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
				 62, 63, 64, 65, 66, 67, 68, 69, 70, };
static int mt7622_esw_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				  0, 0, 0, 0, 0, 0, 0, 0, 0, };
static int mt7622_esw_p0_p1_pins[] = { 51, 52, 53, 54, 55, 56, 57, 58, };
static int mt7622_esw_p0_p1_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, };
static int mt7622_esw_p2_p3_p4_pins[] = { 59, 60, 61, 62, 63, 64, 65, 66, 67,
					  68, 69, 70, };
static int mt7622_esw_p2_p3_p4_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0,
					   0, 0, 0, };
/* RGMII via ESW */
static int mt7622_rgmii_via_esw_pins[] = { 59, 60, 61, 62, 63, 64, 65, 66,
					   67, 68, 69, 70, };
static int mt7622_rgmii_via_esw_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
					    0, };

/* RGMII via GMAC1 */
static int mt7622_rgmii_via_gmac1_pins[] = { 59, 60, 61, 62, 63, 64, 65, 66,
					     67, 68, 69, 70, };
static int mt7622_rgmii_via_gmac1_funcs[] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
					      2, };

/* RGMII via GMAC2 */
static int mt7622_rgmii_via_gmac2_pins[] = { 25, 26, 27, 28, 29, 30, 31, 32,
					     33, 34, 35, 36, };
static int mt7622_rgmii_via_gmac2_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
					      0, };

/* I2C */
static int mt7622_i2c0_pins[] = { 14, 15, };
static int mt7622_i2c0_funcs[] = { 0, 0, };
static int mt7622_i2c1_0_pins[] = { 55, 56, };
static int mt7622_i2c1_0_funcs[] = { 0, 0, };
static int mt7622_i2c1_1_pins[] = { 73, 74, };
static int mt7622_i2c1_1_funcs[] = { 3, 3, };
static int mt7622_i2c1_2_pins[] = { 87, 88, };
static int mt7622_i2c1_2_funcs[] = { 0, 0, };
static int mt7622_i2c2_0_pins[] = { 57, 58, };
static int mt7622_i2c2_0_funcs[] = { 0, 0, };
static int mt7622_i2c2_1_pins[] = { 75, 76, };
static int mt7622_i2c2_1_funcs[] = { 3, 3, };
static int mt7622_i2c2_2_pins[] = { 89, 90, };
static int mt7622_i2c2_2_funcs[] = { 0, 0, };

/* I2S */
static int mt7622_i2s_in_mclk_bclk_ws_pins[] = { 3, 4, 5, };
static int mt7622_i2s_in_mclk_bclk_ws_funcs[] = { 3, 3, 0, };
static int mt7622_i2s1_in_data_pins[] = { 1, };
static int mt7622_i2s1_in_data_funcs[] = { 0, };
static int mt7622_i2s2_in_data_pins[] = { 16, };
static int mt7622_i2s2_in_data_funcs[] = { 0, };
static int mt7622_i2s3_in_data_pins[] = { 17, };
static int mt7622_i2s3_in_data_funcs[] = { 0, };
static int mt7622_i2s4_in_data_pins[] = { 18, };
static int mt7622_i2s4_in_data_funcs[] = { 0, };
static int mt7622_i2s_out_mclk_bclk_ws_pins[] = { 3, 4, 5, };
static int mt7622_i2s_out_mclk_bclk_ws_funcs[] = { 0, 0, 0, };
static int mt7622_i2s1_out_data_pins[] = { 2, };
static int mt7622_i2s1_out_data_funcs[] = { 0, };
static int mt7622_i2s2_out_data_pins[] = { 19, };
static int mt7622_i2s2_out_data_funcs[] = { 0, };
static int mt7622_i2s3_out_data_pins[] = { 20, };
static int mt7622_i2s3_out_data_funcs[] = { 0, };
static int mt7622_i2s4_out_data_pins[] = { 21, };
static int mt7622_i2s4_out_data_funcs[] = { 0, };

/* IR */
static int mt7622_ir_0_tx_pins[] = { 16, };
static int mt7622_ir_0_tx_funcs[] = { 4, };
static int mt7622_ir_1_tx_pins[] = { 59, };
static int mt7622_ir_1_tx_funcs[] = { 5, };
static int mt7622_ir_2_tx_pins[] = { 99, };
static int mt7622_ir_2_tx_funcs[] = { 3, };
static int mt7622_ir_0_rx_pins[] = { 17, };
static int mt7622_ir_0_rx_funcs[] = { 4, };
static int mt7622_ir_1_rx_pins[] = { 60, };
static int mt7622_ir_1_rx_funcs[] = { 5, };
static int mt7622_ir_2_rx_pins[] = { 100, };
static int mt7622_ir_2_rx_funcs[] = { 3, };

/* MDIO */
static int mt7622_mdc_mdio_pins[] = { 23, 24, };
static int mt7622_mdc_mdio_funcs[] = { 0, 0, };

/* PCIE */
static int mt7622_pcie0_0_waken_pins[] = { 14, };
static int mt7622_pcie0_0_waken_funcs[] = { 2, };
static int mt7622_pcie0_0_clkreq_pins[] = { 15, };
static int mt7622_pcie0_0_clkreq_funcs[] = { 2, };
static int mt7622_pcie0_1_waken_pins[] = { 79, };
static int mt7622_pcie0_1_waken_funcs[] = { 4, };
static int mt7622_pcie0_1_clkreq_pins[] = { 80, };
static int mt7622_pcie0_1_clkreq_funcs[] = { 4, };
static int mt7622_pcie1_0_waken_pins[] = { 14, };
static int mt7622_pcie1_0_waken_funcs[] = { 3, };
static int mt7622_pcie1_0_clkreq_pins[] = { 15, };
static int mt7622_pcie1_0_clkreq_funcs[] = { 3, };

static int mt7622_pcie0_pad_perst_pins[] = { 83, };
static int mt7622_pcie0_pad_perst_funcs[] = { 0, };
static int mt7622_pcie1_pad_perst_pins[] = { 84, };
static int mt7622_pcie1_pad_perst_funcs[] = { 0, };

/* PMIC bus */
static int mt7622_pmic_bus_pins[] = { 71, 72, };
static int mt7622_pmic_bus_funcs[] = { 0, 0, };

/* Parallel NAND */
static int mt7622_pnand_pins[] = { 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
				   48, 49, 50, };
static int mt7622_pnand_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				    0, };

/* PWM */
static int mt7622_pwm_ch1_0_pins[] = { 51, };
static int mt7622_pwm_ch1_0_funcs[] = { 3, };
static int mt7622_pwm_ch1_1_pins[] = { 73, };
static int mt7622_pwm_ch1_1_funcs[] = { 4, };
static int mt7622_pwm_ch1_2_pins[] = { 95, };
static int mt7622_pwm_ch1_2_funcs[] = { 0, };
static int mt7622_pwm_ch2_0_pins[] = { 52, };
static int mt7622_pwm_ch2_0_funcs[] = { 3, };
static int mt7622_pwm_ch2_1_pins[] = { 74, };
static int mt7622_pwm_ch2_1_funcs[] = { 4, };
static int mt7622_pwm_ch2_2_pins[] = { 96, };
static int mt7622_pwm_ch2_2_funcs[] = { 0, };
static int mt7622_pwm_ch3_0_pins[] = { 53, };
static int mt7622_pwm_ch3_0_funcs[] = { 3, };
static int mt7622_pwm_ch3_1_pins[] = { 75, };
static int mt7622_pwm_ch3_1_funcs[] = { 4, };
static int mt7622_pwm_ch3_2_pins[] = { 97, };
static int mt7622_pwm_ch3_2_funcs[] = { 0, };
static int mt7622_pwm_ch4_0_pins[] = { 54, };
static int mt7622_pwm_ch4_0_funcs[] = { 3, };
static int mt7622_pwm_ch4_1_pins[] = { 67, };
static int mt7622_pwm_ch4_1_funcs[] = { 3, };
static int mt7622_pwm_ch4_2_pins[] = { 76, };
static int mt7622_pwm_ch4_2_funcs[] = { 4, };
static int mt7622_pwm_ch4_3_pins[] = { 98, };
static int mt7622_pwm_ch4_3_funcs[] = { 0, };
static int mt7622_pwm_ch5_0_pins[] = { 68, };
static int mt7622_pwm_ch5_0_funcs[] = { 3, };
static int mt7622_pwm_ch5_1_pins[] = { 77, };
static int mt7622_pwm_ch5_1_funcs[] = { 4, };
static int mt7622_pwm_ch5_2_pins[] = { 99, };
static int mt7622_pwm_ch5_2_funcs[] = { 0, };
static int mt7622_pwm_ch6_0_pins[] = { 69, };
static int mt7622_pwm_ch6_0_funcs[] = { 3, };
static int mt7622_pwm_ch6_1_pins[] = { 78, };
static int mt7622_pwm_ch6_1_funcs[] = { 4, };
static int mt7622_pwm_ch6_2_pins[] = { 81, };
static int mt7622_pwm_ch6_2_funcs[] = { 4, };
static int mt7622_pwm_ch6_3_pins[] = { 100, };
static int mt7622_pwm_ch6_3_funcs[] = { 0, };
static int mt7622_pwm_ch7_0_pins[] = { 70, };
static int mt7622_pwm_ch7_0_funcs[] = { 3, };
static int mt7622_pwm_ch7_1_pins[] = { 82, };
static int mt7622_pwm_ch7_1_funcs[] = { 4, };
static int mt7622_pwm_ch7_2_pins[] = { 101, };
static int mt7622_pwm_ch7_2_funcs[] = { 0, };

/* SD */
static int mt7622_sd_0_pins[] = { 16, 17, 18, 19, 20, 21, };
static int mt7622_sd_0_funcs[] = { 2, 2, 2, 2, 2, 2, };
static int mt7622_sd_1_pins[] = { 25, 26, 27, 28, 29, 30, };
static int mt7622_sd_1_funcs[] = { 2, 2, 2, 2, 2, 2, };

/* Serial NAND */
static int mt7622_snfi_pins[] = { 8, 9, 10, 11, 12, 13, };
static int mt7622_snfi_funcs[] = { 2, 2, 2, 2, 2, 2, };

/* SPI NOR */
static int mt7622_spi_pins[] = { 8, 9, 10, 11, 12, 13 };
static int mt7622_spi_funcs[] = { 0, 0, 0, 0, 0, 0, };

/* SPIC */
static int mt7622_spic0_0_pins[] = { 63, 64, 65, 66, };
static int mt7622_spic0_0_funcs[] = { 4, 4, 4, 4, };
static int mt7622_spic0_1_pins[] = { 79, 80, 81, 82, };
static int mt7622_spic0_1_funcs[] = { 3, 3, 3, 3, };
static int mt7622_spic1_0_pins[] = { 67, 68, 69, 70, };
static int mt7622_spic1_0_funcs[] = { 4, 4, 4, 4, };
static int mt7622_spic1_1_pins[] = { 73, 74, 75, 76, };
static int mt7622_spic1_1_funcs[] = { 0, 0, 0, 0, };
static int mt7622_spic2_0_pins[] = { 10, 11, 12, 13, };
static int mt7622_spic2_0_funcs[] = { 0, 0, 0, 0, };
static int mt7622_spic2_0_wp_hold_pins[] = { 8, 9, };
static int mt7622_spic2_0_wp_hold_funcs[] = { 0, 0, };

/* TDM */
static int mt7622_tdm_0_out_mclk_bclk_ws_pins[] = { 8, 9, 10, };
static int mt7622_tdm_0_out_mclk_bclk_ws_funcs[] = { 3, 3, 3, };
static int mt7622_tdm_0_in_mclk_bclk_ws_pins[] = { 11, 12, 13, };
static int mt7622_tdm_0_in_mclk_bclk_ws_funcs[] = { 3, 3, 3, };
static int mt7622_tdm_0_out_data_pins[] = { 20, };
static int mt7622_tdm_0_out_data_funcs[] = { 3, };
static int mt7622_tdm_0_in_data_pins[] = { 21, };
static int mt7622_tdm_0_in_data_funcs[] = { 3, };
static int mt7622_tdm_1_out_mclk_bclk_ws_pins[] = { 57, 58, 59, };
static int mt7622_tdm_1_out_mclk_bclk_ws_funcs[] = { 3, 3, 3, };
static int mt7622_tdm_1_in_mclk_bclk_ws_pins[] = { 60, 61, 62, };
static int mt7622_tdm_1_in_mclk_bclk_ws_funcs[] = { 3, 3, 3, };
static int mt7622_tdm_1_out_data_pins[] = { 55, };
static int mt7622_tdm_1_out_data_funcs[] = { 3, };
static int mt7622_tdm_1_in_data_pins[] = { 56, };
static int mt7622_tdm_1_in_data_funcs[] = { 3, };

/* UART */
static int mt7622_uart0_0_tx_rx_pins[] = { 6, 7, };
static int mt7622_uart0_0_tx_rx_funcs[] = { 0, 0, };
static int mt7622_uart1_0_tx_rx_pins[] = { 55, 56, };
static int mt7622_uart1_0_tx_rx_funcs[] = { 2, 2, };
static int mt7622_uart1_0_rts_cts_pins[] = { 57, 58, };
static int mt7622_uart1_0_rts_cts_funcs[] = { 2, 2, };
static int mt7622_uart1_1_tx_rx_pins[] = { 73, 74, };
static int mt7622_uart1_1_tx_rx_funcs[] = { 2, 2, };
static int mt7622_uart1_1_rts_cts_pins[] = { 75, 76, };
static int mt7622_uart1_1_rts_cts_funcs[] = { 2, 2, };
static int mt7622_uart2_0_tx_rx_pins[] = { 3, 4, };
static int mt7622_uart2_0_tx_rx_funcs[] = { 2, 2, };
static int mt7622_uart2_0_rts_cts_pins[] = { 1, 2, };
static int mt7622_uart2_0_rts_cts_funcs[] = { 2, 2, };
static int mt7622_uart2_1_tx_rx_pins[] = { 51, 52, };
static int mt7622_uart2_1_tx_rx_funcs[] = { 0, 0, };
static int mt7622_uart2_1_rts_cts_pins[] = { 53, 54, };
static int mt7622_uart2_1_rts_cts_funcs[] = { 0, 0, };
static int mt7622_uart2_2_tx_rx_pins[] = { 59, 60, };
static int mt7622_uart2_2_tx_rx_funcs[] = { 4, 4, };
static int mt7622_uart2_2_rts_cts_pins[] = { 61, 62, };
static int mt7622_uart2_2_rts_cts_funcs[] = { 4, 4, };
static int mt7622_uart2_3_tx_rx_pins[] = { 95, 96, };
static int mt7622_uart2_3_tx_rx_funcs[] = { 3, 3, };
static int mt7622_uart3_0_tx_rx_pins[] = { 57, 58, };
static int mt7622_uart3_0_tx_rx_funcs[] = { 5, 5, };
static int mt7622_uart3_1_tx_rx_pins[] = { 81, 82, };
static int mt7622_uart3_1_tx_rx_funcs[] = { 0, 0, };
static int mt7622_uart3_1_rts_cts_pins[] = { 79, 80, };
static int mt7622_uart3_1_rts_cts_funcs[] = { 0, 0, };
static int mt7622_uart4_0_tx_rx_pins[] = { 61, 62, };
static int mt7622_uart4_0_tx_rx_funcs[] = { 5, 5, };
static int mt7622_uart4_1_tx_rx_pins[] = { 91, 92, };
static int mt7622_uart4_1_tx_rx_funcs[] = { 0, 0, };
static int mt7622_uart4_1_rts_cts_pins[] = { 93, 94 };
static int mt7622_uart4_1_rts_cts_funcs[] = { 0, 0, };
static int mt7622_uart4_2_tx_rx_pins[] = { 97, 98, };
static int mt7622_uart4_2_tx_rx_funcs[] = { 2, 2, };
static int mt7622_uart4_2_rts_cts_pins[] = { 95, 96 };
static int mt7622_uart4_2_rts_cts_funcs[] = { 2, 2, };

/* Watchdog */
static int mt7622_watchdog_pins[] = { 78, };
static int mt7622_watchdog_funcs[] = { 0, };

/* WLAN LED */
static int mt7622_wled_pins[] = { 85, };
static int mt7622_wled_funcs[] = { 0, };

static const struct group_desc mt7622_groups[] = {
	PINCTRL_PIN_GROUP("emmc", mt7622_emmc),
	PINCTRL_PIN_GROUP("emmc_rst", mt7622_emmc_rst),
	PINCTRL_PIN_GROUP("ephy_leds", mt7622_ephy_leds),
	PINCTRL_PIN_GROUP("ephy0_led", mt7622_ephy0_led),
	PINCTRL_PIN_GROUP("ephy1_led", mt7622_ephy1_led),
	PINCTRL_PIN_GROUP("ephy2_led", mt7622_ephy2_led),
	PINCTRL_PIN_GROUP("ephy3_led", mt7622_ephy3_led),
	PINCTRL_PIN_GROUP("ephy4_led", mt7622_ephy4_led),
	PINCTRL_PIN_GROUP("esw", mt7622_esw),
	PINCTRL_PIN_GROUP("esw_p0_p1", mt7622_esw_p0_p1),
	PINCTRL_PIN_GROUP("esw_p2_p3_p4", mt7622_esw_p2_p3_p4),
	PINCTRL_PIN_GROUP("rgmii_via_esw", mt7622_rgmii_via_esw),
	PINCTRL_PIN_GROUP("rgmii_via_gmac1", mt7622_rgmii_via_gmac1),
	PINCTRL_PIN_GROUP("rgmii_via_gmac2", mt7622_rgmii_via_gmac2),
	PINCTRL_PIN_GROUP("i2c0", mt7622_i2c0),
	PINCTRL_PIN_GROUP("i2c1_0", mt7622_i2c1_0),
	PINCTRL_PIN_GROUP("i2c1_1", mt7622_i2c1_1),
	PINCTRL_PIN_GROUP("i2c1_2", mt7622_i2c1_2),
	PINCTRL_PIN_GROUP("i2c2_0", mt7622_i2c2_0),
	PINCTRL_PIN_GROUP("i2c2_1", mt7622_i2c2_1),
	PINCTRL_PIN_GROUP("i2c2_2", mt7622_i2c2_2),
	PINCTRL_PIN_GROUP("i2s_out_mclk_bclk_ws", mt7622_i2s_out_mclk_bclk_ws),
	PINCTRL_PIN_GROUP("i2s_in_mclk_bclk_ws", mt7622_i2s_in_mclk_bclk_ws),
	PINCTRL_PIN_GROUP("i2s1_in_data", mt7622_i2s1_in_data),
	PINCTRL_PIN_GROUP("i2s2_in_data", mt7622_i2s2_in_data),
	PINCTRL_PIN_GROUP("i2s3_in_data", mt7622_i2s3_in_data),
	PINCTRL_PIN_GROUP("i2s4_in_data", mt7622_i2s4_in_data),
	PINCTRL_PIN_GROUP("i2s1_out_data", mt7622_i2s1_out_data),
	PINCTRL_PIN_GROUP("i2s2_out_data", mt7622_i2s2_out_data),
	PINCTRL_PIN_GROUP("i2s3_out_data", mt7622_i2s3_out_data),
	PINCTRL_PIN_GROUP("i2s4_out_data", mt7622_i2s4_out_data),
	PINCTRL_PIN_GROUP("ir_0_tx", mt7622_ir_0_tx),
	PINCTRL_PIN_GROUP("ir_1_tx", mt7622_ir_1_tx),
	PINCTRL_PIN_GROUP("ir_2_tx", mt7622_ir_2_tx),
	PINCTRL_PIN_GROUP("ir_0_rx", mt7622_ir_0_rx),
	PINCTRL_PIN_GROUP("ir_1_rx", mt7622_ir_1_rx),
	PINCTRL_PIN_GROUP("ir_2_rx", mt7622_ir_2_rx),
	PINCTRL_PIN_GROUP("mdc_mdio", mt7622_mdc_mdio),
	PINCTRL_PIN_GROUP("pcie0_0_waken", mt7622_pcie0_0_waken),
	PINCTRL_PIN_GROUP("pcie0_0_clkreq", mt7622_pcie0_0_clkreq),
	PINCTRL_PIN_GROUP("pcie0_1_waken", mt7622_pcie0_1_waken),
	PINCTRL_PIN_GROUP("pcie0_1_clkreq", mt7622_pcie0_1_clkreq),
	PINCTRL_PIN_GROUP("pcie1_0_waken", mt7622_pcie1_0_waken),
	PINCTRL_PIN_GROUP("pcie1_0_clkreq", mt7622_pcie1_0_clkreq),
	PINCTRL_PIN_GROUP("pcie0_pad_perst", mt7622_pcie0_pad_perst),
	PINCTRL_PIN_GROUP("pcie1_pad_perst", mt7622_pcie1_pad_perst),
	PINCTRL_PIN_GROUP("par_nand", mt7622_pnand),
	PINCTRL_PIN_GROUP("pmic_bus", mt7622_pmic_bus),
	PINCTRL_PIN_GROUP("pwm_ch1_0", mt7622_pwm_ch1_0),
	PINCTRL_PIN_GROUP("pwm_ch1_1", mt7622_pwm_ch1_1),
	PINCTRL_PIN_GROUP("pwm_ch1_2", mt7622_pwm_ch1_2),
	PINCTRL_PIN_GROUP("pwm_ch2_0", mt7622_pwm_ch2_0),
	PINCTRL_PIN_GROUP("pwm_ch2_1", mt7622_pwm_ch2_1),
	PINCTRL_PIN_GROUP("pwm_ch2_2", mt7622_pwm_ch2_2),
	PINCTRL_PIN_GROUP("pwm_ch3_0", mt7622_pwm_ch3_0),
	PINCTRL_PIN_GROUP("pwm_ch3_1", mt7622_pwm_ch3_1),
	PINCTRL_PIN_GROUP("pwm_ch3_2", mt7622_pwm_ch3_2),
	PINCTRL_PIN_GROUP("pwm_ch4_0", mt7622_pwm_ch4_0),
	PINCTRL_PIN_GROUP("pwm_ch4_1", mt7622_pwm_ch4_1),
	PINCTRL_PIN_GROUP("pwm_ch4_2", mt7622_pwm_ch4_2),
	PINCTRL_PIN_GROUP("pwm_ch4_3", mt7622_pwm_ch4_3),
	PINCTRL_PIN_GROUP("pwm_ch5_0", mt7622_pwm_ch5_0),
	PINCTRL_PIN_GROUP("pwm_ch5_1", mt7622_pwm_ch5_1),
	PINCTRL_PIN_GROUP("pwm_ch5_2", mt7622_pwm_ch5_2),
	PINCTRL_PIN_GROUP("pwm_ch6_0", mt7622_pwm_ch6_0),
	PINCTRL_PIN_GROUP("pwm_ch6_1", mt7622_pwm_ch6_1),
	PINCTRL_PIN_GROUP("pwm_ch6_2", mt7622_pwm_ch6_2),
	PINCTRL_PIN_GROUP("pwm_ch6_3", mt7622_pwm_ch6_3),
	PINCTRL_PIN_GROUP("pwm_ch7_0", mt7622_pwm_ch7_0),
	PINCTRL_PIN_GROUP("pwm_ch7_1", mt7622_pwm_ch7_1),
	PINCTRL_PIN_GROUP("pwm_ch7_2", mt7622_pwm_ch7_2),
	PINCTRL_PIN_GROUP("sd_0", mt7622_sd_0),
	PINCTRL_PIN_GROUP("sd_1", mt7622_sd_1),
	PINCTRL_PIN_GROUP("snfi", mt7622_snfi),
	PINCTRL_PIN_GROUP("spi_nor", mt7622_spi),
	PINCTRL_PIN_GROUP("spic0_0", mt7622_spic0_0),
	PINCTRL_PIN_GROUP("spic0_1", mt7622_spic0_1),
	PINCTRL_PIN_GROUP("spic1_0", mt7622_spic1_0),
	PINCTRL_PIN_GROUP("spic1_1", mt7622_spic1_1),
	PINCTRL_PIN_GROUP("spic2_0", mt7622_spic2_0),
	PINCTRL_PIN_GROUP("spic2_0_wp_hold", mt7622_spic2_0_wp_hold),
	PINCTRL_PIN_GROUP("tdm_0_out_mclk_bclk_ws",
			  mt7622_tdm_0_out_mclk_bclk_ws),
	PINCTRL_PIN_GROUP("tdm_0_in_mclk_bclk_ws",
			  mt7622_tdm_0_in_mclk_bclk_ws),
	PINCTRL_PIN_GROUP("tdm_0_out_data",  mt7622_tdm_0_out_data),
	PINCTRL_PIN_GROUP("tdm_0_in_data", mt7622_tdm_0_in_data),
	PINCTRL_PIN_GROUP("tdm_1_out_mclk_bclk_ws",
			  mt7622_tdm_1_out_mclk_bclk_ws),
	PINCTRL_PIN_GROUP("tdm_1_in_mclk_bclk_ws",
			  mt7622_tdm_1_in_mclk_bclk_ws),
	PINCTRL_PIN_GROUP("tdm_1_out_data",  mt7622_tdm_1_out_data),
	PINCTRL_PIN_GROUP("tdm_1_in_data", mt7622_tdm_1_in_data),
	PINCTRL_PIN_GROUP("uart0_0_tx_rx", mt7622_uart0_0_tx_rx),
	PINCTRL_PIN_GROUP("uart1_0_tx_rx", mt7622_uart1_0_tx_rx),
	PINCTRL_PIN_GROUP("uart1_0_rts_cts", mt7622_uart1_0_rts_cts),
	PINCTRL_PIN_GROUP("uart1_1_tx_rx", mt7622_uart1_1_tx_rx),
	PINCTRL_PIN_GROUP("uart1_1_rts_cts", mt7622_uart1_1_rts_cts),
	PINCTRL_PIN_GROUP("uart2_0_tx_rx", mt7622_uart2_0_tx_rx),
	PINCTRL_PIN_GROUP("uart2_0_rts_cts", mt7622_uart2_0_rts_cts),
	PINCTRL_PIN_GROUP("uart2_1_tx_rx", mt7622_uart2_1_tx_rx),
	PINCTRL_PIN_GROUP("uart2_1_rts_cts", mt7622_uart2_1_rts_cts),
	PINCTRL_PIN_GROUP("uart2_2_tx_rx", mt7622_uart2_2_tx_rx),
	PINCTRL_PIN_GROUP("uart2_2_rts_cts", mt7622_uart2_2_rts_cts),
	PINCTRL_PIN_GROUP("uart2_3_tx_rx", mt7622_uart2_3_tx_rx),
	PINCTRL_PIN_GROUP("uart3_0_tx_rx", mt7622_uart3_0_tx_rx),
	PINCTRL_PIN_GROUP("uart3_1_tx_rx", mt7622_uart3_1_tx_rx),
	PINCTRL_PIN_GROUP("uart3_1_rts_cts", mt7622_uart3_1_rts_cts),
	PINCTRL_PIN_GROUP("uart4_0_tx_rx", mt7622_uart4_0_tx_rx),
	PINCTRL_PIN_GROUP("uart4_1_tx_rx", mt7622_uart4_1_tx_rx),
	PINCTRL_PIN_GROUP("uart4_1_rts_cts", mt7622_uart4_1_rts_cts),
	PINCTRL_PIN_GROUP("uart4_2_tx_rx", mt7622_uart4_2_tx_rx),
	PINCTRL_PIN_GROUP("uart4_2_rts_cts", mt7622_uart4_2_rts_cts),
	PINCTRL_PIN_GROUP("watchdog", mt7622_watchdog),
	PINCTRL_PIN_GROUP("wled", mt7622_wled),
};

/* Joint those groups owning the same capability in user point of view which
 * allows that people tend to use through the device tree.
 */
static const char *mt7622_emmc_groups[] = { "emmc", "emmc_rst", };
static const char *mt7622_ethernet_groups[] = { "esw", "esw_p0_p1",
						"esw_p2_p3_p4", "mdc_mdio",
						"rgmii_via_gmac1",
						"rgmii_via_gmac2",
						"rgmii_via_esw", };
static const char *mt7622_i2c_groups[] = { "i2c0", "i2c1_0", "i2c1_1",
					   "i2c1_2", "i2c2_0", "i2c2_1",
					   "i2c2_2", };
static const char *mt7622_i2s_groups[] = { "i2s_out_mclk_bclk_ws",
					   "i2s_in_mclk_bclk_ws",
					   "i2s1_in_data", "i2s2_in_data",
					   "i2s3_in_data", "i2s4_in_data",
					   "i2s1_out_data", "i2s2_out_data",
					   "i2s3_out_data", "i2s4_out_data", };
static const char *mt7622_ir_groups[] = { "ir_0_tx", "ir_1_tx", "ir_2_tx",
					  "ir_0_rx", "ir_1_rx", "ir_2_rx"};
static const char *mt7622_led_groups[] = { "ephy_leds", "ephy0_led",
					   "ephy1_led", "ephy2_led",
					   "ephy3_led", "ephy4_led",
					   "wled", };
static const char *mt7622_flash_groups[] = { "par_nand", "snfi", "spi_nor"};
static const char *mt7622_pcie_groups[] = { "pcie0_0_waken", "pcie0_0_clkreq",
					    "pcie0_1_waken", "pcie0_1_clkreq",
					    "pcie1_0_waken", "pcie1_0_clkreq",
					    "pcie0_pad_perst",
					    "pcie1_pad_perst", };
static const char *mt7622_pmic_bus_groups[] = { "pmic_bus", };
static const char *mt7622_pwm_groups[] = { "pwm_ch1_0", "pwm_ch1_1",
					   "pwm_ch1_2", "pwm_ch2_0",
					   "pwm_ch2_1", "pwm_ch2_2",
					   "pwm_ch3_0", "pwm_ch3_1",
					   "pwm_ch3_2", "pwm_ch4_0",
					   "pwm_ch4_1", "pwm_ch4_2",
					   "pwm_ch4_3", "pwm_ch5_0",
					   "pwm_ch5_1", "pwm_ch5_2",
					   "pwm_ch6_0", "pwm_ch6_1",
					   "pwm_ch6_2", "pwm_ch6_3",
					   "pwm_ch7_0", "pwm_ch7_1",
					   "pwm_ch7_2", };
static const char *mt7622_sd_groups[] = { "sd_0", "sd_1", };
static const char *mt7622_spic_groups[] = { "spic0_0", "spic0_1", "spic1_0",
					    "spic1_1", "spic2_0",
					    "spic2_0_wp_hold", };
static const char *mt7622_tdm_groups[] = { "tdm_0_out_mclk_bclk_ws",
					   "tdm_0_in_mclk_bclk_ws",
					   "tdm_0_out_data",
					   "tdm_0_in_data",
					   "tdm_1_out_mclk_bclk_ws",
					   "tdm_1_in_mclk_bclk_ws",
					   "tdm_1_out_data",
					   "tdm_1_in_data", };

static const char *mt7622_uart_groups[] = { "uart0_0_tx_rx",
					    "uart1_0_tx_rx", "uart1_0_rts_cts",
					    "uart1_1_tx_rx", "uart1_1_rts_cts",
					    "uart2_0_tx_rx", "uart2_0_rts_cts",
					    "uart2_1_tx_rx", "uart2_1_rts_cts",
					    "uart2_2_tx_rx", "uart2_2_rts_cts",
					    "uart2_3_tx_rx",
					    "uart3_0_tx_rx",
					    "uart3_1_tx_rx", "uart3_1_rts_cts",
					    "uart4_0_tx_rx",
					    "uart4_1_tx_rx", "uart4_1_rts_cts",
					    "uart4_2_tx_rx",
					    "uart4_2_rts_cts",};
static const char *mt7622_wdt_groups[] = { "watchdog", };

static const struct function_desc mt7622_functions[] = {
	{"emmc", mt7622_emmc_groups, ARRAY_SIZE(mt7622_emmc_groups)},
	{"eth",	mt7622_ethernet_groups, ARRAY_SIZE(mt7622_ethernet_groups)},
	{"i2c", mt7622_i2c_groups, ARRAY_SIZE(mt7622_i2c_groups)},
	{"i2s",	mt7622_i2s_groups, ARRAY_SIZE(mt7622_i2s_groups)},
	{"ir", mt7622_ir_groups, ARRAY_SIZE(mt7622_ir_groups)},
	{"led",	mt7622_led_groups, ARRAY_SIZE(mt7622_led_groups)},
	{"flash", mt7622_flash_groups, ARRAY_SIZE(mt7622_flash_groups)},
	{"pcie", mt7622_pcie_groups, ARRAY_SIZE(mt7622_pcie_groups)},
	{"pmic", mt7622_pmic_bus_groups, ARRAY_SIZE(mt7622_pmic_bus_groups)},
	{"pwm",	mt7622_pwm_groups, ARRAY_SIZE(mt7622_pwm_groups)},
	{"sd", mt7622_sd_groups, ARRAY_SIZE(mt7622_sd_groups)},
	{"spi",	mt7622_spic_groups, ARRAY_SIZE(mt7622_spic_groups)},
	{"tdm",	mt7622_tdm_groups, ARRAY_SIZE(mt7622_tdm_groups)},
	{"uart", mt7622_uart_groups, ARRAY_SIZE(mt7622_uart_groups)},
	{"watchdog", mt7622_wdt_groups, ARRAY_SIZE(mt7622_wdt_groups)},
};

static const struct mtk_eint_hw mt7622_eint_hw = {
	.port_mask = 7,
	.ports     = 7,
	.ap_num    = ARRAY_SIZE(mt7622_pins),
	.db_cnt    = 20,
};

static const struct mtk_pin_soc mt7622_data = {
	.reg_cal = mt7622_reg_cals,
	.pins = mt7622_pins,
	.npins = ARRAY_SIZE(mt7622_pins),
	.grps = mt7622_groups,
	.ngrps = ARRAY_SIZE(mt7622_groups),
	.funcs = mt7622_functions,
	.nfuncs = ARRAY_SIZE(mt7622_functions),
	.eint_hw = &mt7622_eint_hw,
	.gpio_m	= 1,
	.ies_present = false,
	.base_names = mtk_default_register_base_names,
	.nbase_names = ARRAY_SIZE(mtk_default_register_base_names),
	.bias_disable_set = mtk_pinconf_bias_disable_set,
	.bias_disable_get = mtk_pinconf_bias_disable_get,
	.bias_set = mtk_pinconf_bias_set,
	.bias_get = mtk_pinconf_bias_get,
	.drive_set = mtk_pinconf_drive_set,
	.drive_get = mtk_pinconf_drive_get,
};

static const struct of_device_id mt7622_pinctrl_of_match[] = {
	{ .compatible = "mediatek,mt7622-pinctrl", },
	{ }
};

static int mt7622_pinctrl_probe(struct platform_device *pdev)
{
	return mtk_moore_pinctrl_probe(pdev, &mt7622_data);
}

static struct platform_driver mt7622_pinctrl_driver = {
	.driver = {
		.name = "mt7622-pinctrl",
		.of_match_table = mt7622_pinctrl_of_match,
	},
	.probe = mt7622_pinctrl_probe,
};

static int __init mt7622_pinctrl_init(void)
{
	return platform_driver_register(&mt7622_pinctrl_driver);
}
arch_initcall(mt7622_pinctrl_init);

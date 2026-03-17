// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Realtek DHC 1625 pin controller driver
 *
 * Copyright (c) 2023 Realtek Semiconductor Corp.
 *
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-rtd.h"

enum rtd1625_iso_pins_enum {
	RTD1625_ISO_GPIO_8 = 0,
	RTD1625_ISO_GPIO_9,
	RTD1625_ISO_GPIO_10,
	RTD1625_ISO_GPIO_11,
	RTD1625_ISO_USB_CC1,
	RTD1625_ISO_USB_CC2,
	RTD1625_ISO_GPIO_45,
	RTD1625_ISO_GPIO_46,
	RTD1625_ISO_GPIO_47,
	RTD1625_ISO_GPIO_48,
	RTD1625_ISO_GPIO_49,
	RTD1625_ISO_GPIO_50,
	RTD1625_ISO_GPIO_52,
	RTD1625_ISO_GPIO_94,
	RTD1625_ISO_GPIO_95,
	RTD1625_ISO_GPIO_96,
	RTD1625_ISO_GPIO_97,
	RTD1625_ISO_GPIO_98,
	RTD1625_ISO_GPIO_99,
	RTD1625_ISO_GPIO_100,
	RTD1625_ISO_GPIO_101,
	RTD1625_ISO_GPIO_102,
	RTD1625_ISO_GPIO_103,
	RTD1625_ISO_GPIO_104,
	RTD1625_ISO_GPIO_105,
	RTD1625_ISO_GPIO_106,
	RTD1625_ISO_GPIO_107,
	RTD1625_ISO_GPIO_108,
	RTD1625_ISO_GPIO_109,
	RTD1625_ISO_GPIO_110,
	RTD1625_ISO_GPIO_111,
	RTD1625_ISO_GPIO_112,
	RTD1625_ISO_GPIO_128,
	RTD1625_ISO_GPIO_129,
	RTD1625_ISO_GPIO_130,
	RTD1625_ISO_GPIO_131,
	RTD1625_ISO_GPIO_145,
	RTD1625_ISO_GPIO_146,
	RTD1625_ISO_GPIO_147,
	RTD1625_ISO_GPIO_148,
	RTD1625_ISO_GPIO_149,
	RTD1625_ISO_GPIO_150,
	RTD1625_ISO_GPIO_151,
	RTD1625_ISO_GPIO_152,
	RTD1625_ISO_GPIO_153,
	RTD1625_ISO_GPIO_154,
	RTD1625_ISO_GPIO_155,
	RTD1625_ISO_GPIO_156,
	RTD1625_ISO_GPIO_157,
	RTD1625_ISO_GPIO_158,
	RTD1625_ISO_GPIO_159,
	RTD1625_ISO_GPIO_160,
	RTD1625_ISO_GPIO_161,
	RTD1625_ISO_GPIO_162,
	RTD1625_ISO_GPIO_163,
	RTD1625_ISO_HI_WIDTH,
	RTD1625_ISO_SF_EN,
	RTD1625_ISO_ARM_TRACE_DBG_EN,
	RTD1625_ISO_EJTAG_AUCPU0_LOC,
	RTD1625_ISO_EJTAG_AUCPU1_LOC,
	RTD1625_ISO_EJTAG_VE2_LOC,
	RTD1625_ISO_EJTAG_SCPU_LOC,
	RTD1625_ISO_EJTAG_PCPU_LOC,
	RTD1625_ISO_EJTAG_ACPU_LOC,
	RTD1625_ISO_I2C6_LOC,
	RTD1625_ISO_UART0_LOC,
	RTD1625_ISO_AI_I2S1_LOC,
	RTD1625_ISO_AO_I2S1_LOC,
	RTD1625_ISO_ETN_PHY_LOC,
	RTD1625_ISO_SPDIF_LOC,
	RTD1625_ISO_RGMII_VDSEL,
	RTD1625_ISO_CSI_VDSEL,
	RTD1625_ISO_SPDIF_IN_MODE,
};

static const struct pinctrl_pin_desc rtd1625_iso_pins[] = {
	PINCTRL_PIN(RTD1625_ISO_GPIO_8, "gpio_8"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_9, "gpio_9"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_10, "gpio_10"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_11, "gpio_11"),
	PINCTRL_PIN(RTD1625_ISO_USB_CC1, "usb_cc1"),
	PINCTRL_PIN(RTD1625_ISO_USB_CC2, "usb_cc2"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_45, "gpio_45"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_46, "gpio_46"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_47, "gpio_47"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_48, "gpio_48"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_49, "gpio_49"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_50, "gpio_50"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_52, "gpio_52"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_94, "gpio_94"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_95, "gpio_95"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_96, "gpio_96"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_97, "gpio_97"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_98, "gpio_98"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_99, "gpio_99"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_100, "gpio_100"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_101, "gpio_101"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_102, "gpio_102"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_103, "gpio_103"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_104, "gpio_104"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_105, "gpio_105"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_106, "gpio_106"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_107, "gpio_107"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_108, "gpio_108"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_109, "gpio_109"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_110, "gpio_110"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_111, "gpio_111"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_112, "gpio_112"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_128, "gpio_128"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_129, "gpio_129"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_130, "gpio_130"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_131, "gpio_131"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_145, "gpio_145"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_146, "gpio_146"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_147, "gpio_147"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_148, "gpio_148"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_149, "gpio_149"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_150, "gpio_150"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_151, "gpio_151"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_152, "gpio_152"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_153, "gpio_153"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_154, "gpio_154"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_155, "gpio_155"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_156, "gpio_156"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_157, "gpio_157"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_158, "gpio_158"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_159, "gpio_159"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_160, "gpio_160"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_161, "gpio_161"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_162, "gpio_162"),
	PINCTRL_PIN(RTD1625_ISO_GPIO_163, "gpio_163"),
	PINCTRL_PIN(RTD1625_ISO_HI_WIDTH, "hi_width"),
	PINCTRL_PIN(RTD1625_ISO_SF_EN, "sf_en"),
	PINCTRL_PIN(RTD1625_ISO_ARM_TRACE_DBG_EN, "arm_trace_dbg_en"),
	PINCTRL_PIN(RTD1625_ISO_EJTAG_AUCPU0_LOC, "ejtag_aucpu0_loc"),
	PINCTRL_PIN(RTD1625_ISO_EJTAG_AUCPU1_LOC, "ejtag_aucpu1_loc"),
	PINCTRL_PIN(RTD1625_ISO_EJTAG_VE2_LOC, "ejtag_ve2_loc"),
	PINCTRL_PIN(RTD1625_ISO_EJTAG_SCPU_LOC, "ejtag_scpu_loc"),
	PINCTRL_PIN(RTD1625_ISO_EJTAG_PCPU_LOC, "ejtag_pcpu_loc"),
	PINCTRL_PIN(RTD1625_ISO_EJTAG_ACPU_LOC, "ejtag_acpu_loc"),
	PINCTRL_PIN(RTD1625_ISO_I2C6_LOC, "i2c6_loc"),
	PINCTRL_PIN(RTD1625_ISO_UART0_LOC, "uart0_loc"),
	PINCTRL_PIN(RTD1625_ISO_AI_I2S1_LOC, "ai_i2s1_loc"),
	PINCTRL_PIN(RTD1625_ISO_AO_I2S1_LOC, "ao_i2s1_loc"),
	PINCTRL_PIN(RTD1625_ISO_ETN_PHY_LOC, "etn_phy_loc"),
	PINCTRL_PIN(RTD1625_ISO_SPDIF_LOC, "spdif_loc"),
	PINCTRL_PIN(RTD1625_ISO_RGMII_VDSEL, "rgmii_vdsel"),
	PINCTRL_PIN(RTD1625_ISO_CSI_VDSEL, "csi_vdsel"),
	PINCTRL_PIN(RTD1625_ISO_SPDIF_IN_MODE, "spdif_in_mode"),
};

enum rtd1625_isom_pins_enum {
	RTD1625_ISOM_GPIO_0 = 0,
	RTD1625_ISOM_GPIO_1,
	RTD1625_ISOM_GPIO_28,
	RTD1625_ISOM_GPIO_29,
	RTD1625_ISOM_IR_RX_LOC,
};

static const struct pinctrl_pin_desc rtd1625_isom_pins[] = {
	PINCTRL_PIN(RTD1625_ISOM_GPIO_0, "gpio_0"),
	PINCTRL_PIN(RTD1625_ISOM_GPIO_1, "gpio_1"),
	PINCTRL_PIN(RTD1625_ISOM_GPIO_28, "gpio_28"),
	PINCTRL_PIN(RTD1625_ISOM_GPIO_29, "gpio_29"),
	PINCTRL_PIN(RTD1625_ISOM_IR_RX_LOC, "ir_rx_loc"),
};

enum rtd1625_ve4_pins_enum {
	RTD1625_VE4_GPIO_2 = 0,
	RTD1625_VE4_GPIO_3,
	RTD1625_VE4_GPIO_4,
	RTD1625_VE4_GPIO_5,
	RTD1625_VE4_GPIO_6,
	RTD1625_VE4_GPIO_7,
	RTD1625_VE4_GPIO_12,
	RTD1625_VE4_GPIO_13,
	RTD1625_VE4_GPIO_16,
	RTD1625_VE4_GPIO_17,
	RTD1625_VE4_GPIO_18,
	RTD1625_VE4_GPIO_19,
	RTD1625_VE4_GPIO_23,
	RTD1625_VE4_GPIO_24,
	RTD1625_VE4_GPIO_25,
	RTD1625_VE4_GPIO_30,
	RTD1625_VE4_GPIO_31,
	RTD1625_VE4_GPIO_32,
	RTD1625_VE4_GPIO_33,
	RTD1625_VE4_GPIO_34,
	RTD1625_VE4_GPIO_35,
	RTD1625_VE4_GPIO_42,
	RTD1625_VE4_GPIO_43,
	RTD1625_VE4_GPIO_44,
	RTD1625_VE4_GPIO_51,
	RTD1625_VE4_GPIO_53,
	RTD1625_VE4_GPIO_54,
	RTD1625_VE4_GPIO_55,
	RTD1625_VE4_GPIO_56,
	RTD1625_VE4_GPIO_57,
	RTD1625_VE4_GPIO_58,
	RTD1625_VE4_GPIO_59,
	RTD1625_VE4_GPIO_60,
	RTD1625_VE4_GPIO_61,
	RTD1625_VE4_GPIO_62,
	RTD1625_VE4_GPIO_63,
	RTD1625_VE4_GPIO_92,
	RTD1625_VE4_GPIO_93,
	RTD1625_VE4_GPIO_132,
	RTD1625_VE4_GPIO_133,
	RTD1625_VE4_GPIO_134,
	RTD1625_VE4_GPIO_135,
	RTD1625_VE4_GPIO_136,
	RTD1625_VE4_GPIO_137,
	RTD1625_VE4_GPIO_138,
	RTD1625_VE4_GPIO_139,
	RTD1625_VE4_GPIO_140,
	RTD1625_VE4_GPIO_141,
	RTD1625_VE4_GPIO_142,
	RTD1625_VE4_GPIO_143,
	RTD1625_VE4_GPIO_144,
	RTD1625_VE4_GPIO_164,
	RTD1625_VE4_GPIO_165,
	RTD1625_VE4_UART_LOC,
};

static const struct pinctrl_pin_desc rtd1625_ve4_pins[] = {
	PINCTRL_PIN(RTD1625_VE4_GPIO_2, "gpio_2"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_3, "gpio_3"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_4, "gpio_4"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_5, "gpio_5"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_6, "gpio_6"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_7, "gpio_7"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_12, "gpio_12"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_13, "gpio_13"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_16, "gpio_16"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_17, "gpio_17"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_18, "gpio_18"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_19, "gpio_19"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_23, "gpio_23"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_24, "gpio_24"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_25, "gpio_25"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_30, "gpio_30"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_31, "gpio_31"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_32, "gpio_32"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_33, "gpio_33"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_34, "gpio_34"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_35, "gpio_35"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_42, "gpio_42"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_43, "gpio_43"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_44, "gpio_44"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_51, "gpio_51"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_53, "gpio_53"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_54, "gpio_54"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_55, "gpio_55"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_56, "gpio_56"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_57, "gpio_57"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_58, "gpio_58"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_59, "gpio_59"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_60, "gpio_60"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_61, "gpio_61"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_62, "gpio_62"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_63, "gpio_63"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_92, "gpio_92"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_93, "gpio_93"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_132, "gpio_132"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_133, "gpio_133"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_134, "gpio_134"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_135, "gpio_135"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_136, "gpio_136"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_137, "gpio_137"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_138, "gpio_138"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_139, "gpio_139"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_140, "gpio_140"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_141, "gpio_141"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_142, "gpio_142"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_143, "gpio_143"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_144, "gpio_144"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_164, "gpio_164"),
	PINCTRL_PIN(RTD1625_VE4_GPIO_165, "gpio_165"),
	PINCTRL_PIN(RTD1625_VE4_UART_LOC, "ve4_uart_loc"),
};

enum rtd1625_main2_pins_enum {
	RTD1625_MAIN2_GPIO_14 = 0,
	RTD1625_MAIN2_GPIO_15,
	RTD1625_MAIN2_GPIO_20,
	RTD1625_MAIN2_GPIO_21,
	RTD1625_MAIN2_GPIO_22,
	RTD1625_MAIN2_HIF_DATA,
	RTD1625_MAIN2_HIF_EN,
	RTD1625_MAIN2_HIF_RDY,
	RTD1625_MAIN2_HIF_CLK,
	RTD1625_MAIN2_GPIO_40,
	RTD1625_MAIN2_GPIO_41,
	RTD1625_MAIN2_GPIO_64,
	RTD1625_MAIN2_GPIO_65,
	RTD1625_MAIN2_GPIO_66,
	RTD1625_MAIN2_GPIO_67,
	RTD1625_MAIN2_EMMC_DATA_0,
	RTD1625_MAIN2_EMMC_DATA_1,
	RTD1625_MAIN2_EMMC_DATA_2,
	RTD1625_MAIN2_EMMC_DATA_3,
	RTD1625_MAIN2_EMMC_DATA_4,
	RTD1625_MAIN2_EMMC_DATA_5,
	RTD1625_MAIN2_EMMC_DATA_6,
	RTD1625_MAIN2_EMMC_DATA_7,
	RTD1625_MAIN2_EMMC_RST_N,
	RTD1625_MAIN2_EMMC_CMD,
	RTD1625_MAIN2_EMMC_CLK,
	RTD1625_MAIN2_EMMC_DD_SB,
	RTD1625_MAIN2_GPIO_80,
	RTD1625_MAIN2_GPIO_81,
	RTD1625_MAIN2_GPIO_82,
	RTD1625_MAIN2_GPIO_83,
	RTD1625_MAIN2_GPIO_84,
	RTD1625_MAIN2_GPIO_85,
	RTD1625_MAIN2_GPIO_86,
	RTD1625_MAIN2_GPIO_87,
	RTD1625_MAIN2_GPIO_88,
	RTD1625_MAIN2_GPIO_89,
	RTD1625_MAIN2_GPIO_90,
	RTD1625_MAIN2_GPIO_91,
};

static const struct pinctrl_pin_desc rtd1625_main2_pins[] = {
	PINCTRL_PIN(RTD1625_MAIN2_GPIO_14, "gpio_14"),
	PINCTRL_PIN(RTD1625_MAIN2_GPIO_15, "gpio_15"),
	PINCTRL_PIN(RTD1625_MAIN2_GPIO_20, "gpio_20"),
	PINCTRL_PIN(RTD1625_MAIN2_GPIO_21, "gpio_21"),
	PINCTRL_PIN(RTD1625_MAIN2_GPIO_22, "gpio_22"),
	PINCTRL_PIN(RTD1625_MAIN2_HIF_DATA, "hif_data"),
	PINCTRL_PIN(RTD1625_MAIN2_HIF_EN, "hif_en"),
	PINCTRL_PIN(RTD1625_MAIN2_HIF_RDY, "hif_rdy"),
	PINCTRL_PIN(RTD1625_MAIN2_HIF_CLK, "hif_clk"),
	PINCTRL_PIN(RTD1625_MAIN2_GPIO_40, "gpio_40"),
	PINCTRL_PIN(RTD1625_MAIN2_GPIO_41, "gpio_41"),
	PINCTRL_PIN(RTD1625_MAIN2_GPIO_64, "gpio_64"),
	PINCTRL_PIN(RTD1625_MAIN2_GPIO_65, "gpio_65"),
	PINCTRL_PIN(RTD1625_MAIN2_GPIO_66, "gpio_66"),
	PINCTRL_PIN(RTD1625_MAIN2_GPIO_67, "gpio_67"),
	PINCTRL_PIN(RTD1625_MAIN2_EMMC_DATA_0, "emmc_data_0"),
	PINCTRL_PIN(RTD1625_MAIN2_EMMC_DATA_1, "emmc_data_1"),
	PINCTRL_PIN(RTD1625_MAIN2_EMMC_DATA_2, "emmc_data_2"),
	PINCTRL_PIN(RTD1625_MAIN2_EMMC_DATA_3, "emmc_data_3"),
	PINCTRL_PIN(RTD1625_MAIN2_EMMC_DATA_4, "emmc_data_4"),
	PINCTRL_PIN(RTD1625_MAIN2_EMMC_DATA_5, "emmc_data_5"),
	PINCTRL_PIN(RTD1625_MAIN2_EMMC_DATA_6, "emmc_data_6"),
	PINCTRL_PIN(RTD1625_MAIN2_EMMC_DATA_7, "emmc_data_7"),
	PINCTRL_PIN(RTD1625_MAIN2_EMMC_RST_N, "emmc_rst_n"),
	PINCTRL_PIN(RTD1625_MAIN2_EMMC_CMD, "emmc_cmd"),
	PINCTRL_PIN(RTD1625_MAIN2_EMMC_CLK, "emmc_clk"),
	PINCTRL_PIN(RTD1625_MAIN2_EMMC_DD_SB, "emmc_dd_sb"),
	PINCTRL_PIN(RTD1625_MAIN2_GPIO_80, "gpio_80"),
	PINCTRL_PIN(RTD1625_MAIN2_GPIO_81, "gpio_81"),
	PINCTRL_PIN(RTD1625_MAIN2_GPIO_82, "gpio_82"),
	PINCTRL_PIN(RTD1625_MAIN2_GPIO_83, "gpio_83"),
	PINCTRL_PIN(RTD1625_MAIN2_GPIO_84, "gpio_84"),
	PINCTRL_PIN(RTD1625_MAIN2_GPIO_85, "gpio_85"),
	PINCTRL_PIN(RTD1625_MAIN2_GPIO_86, "gpio_86"),
	PINCTRL_PIN(RTD1625_MAIN2_GPIO_87, "gpio_87"),
	PINCTRL_PIN(RTD1625_MAIN2_GPIO_88, "gpio_88"),
	PINCTRL_PIN(RTD1625_MAIN2_GPIO_89, "gpio_89"),
	PINCTRL_PIN(RTD1625_MAIN2_GPIO_90, "gpio_90"),
	PINCTRL_PIN(RTD1625_MAIN2_GPIO_91, "gpio_91"),
};

#define DECLARE_RTD1625_PIN(_pin, _name) \
	static const unsigned int rtd1625_##_name##_pins[] = { _pin }

DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_8, gpio_8);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_9, gpio_9);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_10, gpio_10);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_11, gpio_11);
DECLARE_RTD1625_PIN(RTD1625_ISO_USB_CC1, usb_cc1);
DECLARE_RTD1625_PIN(RTD1625_ISO_USB_CC2, usb_cc2);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_45, gpio_45);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_46, gpio_46);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_47, gpio_47);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_48, gpio_48);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_49, gpio_49);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_50, gpio_50);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_52, gpio_52);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_94, gpio_94);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_95, gpio_95);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_96, gpio_96);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_97, gpio_97);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_98, gpio_98);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_99, gpio_99);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_100, gpio_100);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_101, gpio_101);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_102, gpio_102);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_103, gpio_103);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_104, gpio_104);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_105, gpio_105);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_106, gpio_106);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_107, gpio_107);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_108, gpio_108);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_109, gpio_109);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_110, gpio_110);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_111, gpio_111);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_112, gpio_112);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_128, gpio_128);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_129, gpio_129);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_130, gpio_130);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_131, gpio_131);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_145, gpio_145);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_146, gpio_146);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_147, gpio_147);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_148, gpio_148);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_149, gpio_149);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_150, gpio_150);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_151, gpio_151);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_152, gpio_152);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_153, gpio_153);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_154, gpio_154);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_155, gpio_155);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_156, gpio_156);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_157, gpio_157);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_158, gpio_158);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_159, gpio_159);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_160, gpio_160);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_161, gpio_161);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_162, gpio_162);
DECLARE_RTD1625_PIN(RTD1625_ISO_GPIO_163, gpio_163);

DECLARE_RTD1625_PIN(RTD1625_ISO_HI_WIDTH, hi_width);
DECLARE_RTD1625_PIN(RTD1625_ISO_SF_EN, sf_en);
DECLARE_RTD1625_PIN(RTD1625_ISO_ARM_TRACE_DBG_EN, arm_trace_dbg_en);
DECLARE_RTD1625_PIN(RTD1625_ISO_EJTAG_AUCPU0_LOC, ejtag_aucpu0_loc);
DECLARE_RTD1625_PIN(RTD1625_ISO_EJTAG_AUCPU1_LOC, ejtag_aucpu1_loc);
DECLARE_RTD1625_PIN(RTD1625_ISO_EJTAG_VE2_LOC, ejtag_ve2_loc);
DECLARE_RTD1625_PIN(RTD1625_ISO_EJTAG_SCPU_LOC, ejtag_scpu_loc);
DECLARE_RTD1625_PIN(RTD1625_ISO_EJTAG_PCPU_LOC, ejtag_pcpu_loc);
DECLARE_RTD1625_PIN(RTD1625_ISO_EJTAG_ACPU_LOC, ejtag_acpu_loc);

DECLARE_RTD1625_PIN(RTD1625_ISO_I2C6_LOC, i2c6_loc);
DECLARE_RTD1625_PIN(RTD1625_ISO_UART0_LOC, uart0_loc);
DECLARE_RTD1625_PIN(RTD1625_ISO_AI_I2S1_LOC, ai_i2s1_loc);
DECLARE_RTD1625_PIN(RTD1625_ISO_AO_I2S1_LOC, ao_i2s1_loc);
DECLARE_RTD1625_PIN(RTD1625_ISO_ETN_PHY_LOC, etn_phy_loc);
DECLARE_RTD1625_PIN(RTD1625_ISO_SPDIF_LOC, spdif_loc);
DECLARE_RTD1625_PIN(RTD1625_ISO_RGMII_VDSEL, rgmii_vdsel);
DECLARE_RTD1625_PIN(RTD1625_ISO_CSI_VDSEL, csi_vdsel);
DECLARE_RTD1625_PIN(RTD1625_ISO_SPDIF_IN_MODE, spdif_in_mode);

DECLARE_RTD1625_PIN(RTD1625_ISOM_GPIO_0, gpio_0);
DECLARE_RTD1625_PIN(RTD1625_ISOM_GPIO_1, gpio_1);
DECLARE_RTD1625_PIN(RTD1625_ISOM_GPIO_28, gpio_28);
DECLARE_RTD1625_PIN(RTD1625_ISOM_GPIO_29, gpio_29);
DECLARE_RTD1625_PIN(RTD1625_ISOM_IR_RX_LOC, ir_rx_loc);

DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_2, gpio_2);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_3, gpio_3);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_4, gpio_4);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_5, gpio_5);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_6, gpio_6);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_7, gpio_7);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_12, gpio_12);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_13, gpio_13);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_16, gpio_16);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_17, gpio_17);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_18, gpio_18);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_19, gpio_19);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_23, gpio_23);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_24, gpio_24);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_25, gpio_25);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_30, gpio_30);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_31, gpio_31);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_32, gpio_32);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_33, gpio_33);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_34, gpio_34);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_35, gpio_35);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_42, gpio_42);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_43, gpio_43);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_44, gpio_44);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_51, gpio_51);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_53, gpio_53);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_54, gpio_54);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_55, gpio_55);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_56, gpio_56);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_57, gpio_57);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_58, gpio_58);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_59, gpio_59);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_60, gpio_60);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_61, gpio_61);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_62, gpio_62);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_63, gpio_63);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_92, gpio_92);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_93, gpio_93);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_132, gpio_132);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_133, gpio_133);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_134, gpio_134);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_135, gpio_135);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_136, gpio_136);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_137, gpio_137);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_138, gpio_138);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_139, gpio_139);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_140, gpio_140);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_141, gpio_141);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_142, gpio_142);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_143, gpio_143);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_144, gpio_144);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_164, gpio_164);
DECLARE_RTD1625_PIN(RTD1625_VE4_GPIO_165, gpio_165);
DECLARE_RTD1625_PIN(RTD1625_VE4_UART_LOC, ve4_uart_loc);

DECLARE_RTD1625_PIN(RTD1625_MAIN2_EMMC_RST_N, emmc_rst_n);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_EMMC_DD_SB, emmc_dd_sb);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_EMMC_CLK, emmc_clk);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_EMMC_CMD, emmc_cmd);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_EMMC_DATA_0, emmc_data_0);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_EMMC_DATA_1, emmc_data_1);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_EMMC_DATA_2, emmc_data_2);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_EMMC_DATA_3, emmc_data_3);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_EMMC_DATA_4, emmc_data_4);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_EMMC_DATA_5, emmc_data_5);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_EMMC_DATA_6, emmc_data_6);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_EMMC_DATA_7, emmc_data_7);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_GPIO_14, gpio_14);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_GPIO_15, gpio_15);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_GPIO_20, gpio_20);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_GPIO_21, gpio_21);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_GPIO_22, gpio_22);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_HIF_DATA, hif_data);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_HIF_EN, hif_en);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_HIF_RDY, hif_rdy);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_HIF_CLK, hif_clk);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_GPIO_40, gpio_40);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_GPIO_41, gpio_41);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_GPIO_64, gpio_64);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_GPIO_65, gpio_65);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_GPIO_66, gpio_66);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_GPIO_67, gpio_67);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_GPIO_80, gpio_80);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_GPIO_81, gpio_81);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_GPIO_82, gpio_82);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_GPIO_83, gpio_83);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_GPIO_84, gpio_84);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_GPIO_85, gpio_85);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_GPIO_86, gpio_86);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_GPIO_87, gpio_87);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_GPIO_88, gpio_88);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_GPIO_89, gpio_89);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_GPIO_90, gpio_90);
DECLARE_RTD1625_PIN(RTD1625_MAIN2_GPIO_91, gpio_91);

#define RTD1625_GROUP(_name) \
	{ \
		.name = # _name, \
		.pins = rtd1625_ ## _name ## _pins, \
		.num_pins = ARRAY_SIZE(rtd1625_ ## _name ## _pins), \
	}

static const struct rtd_pin_group_desc rtd1625_iso_pin_groups[] = {
	RTD1625_GROUP(gpio_8),
	RTD1625_GROUP(gpio_9),
	RTD1625_GROUP(gpio_10),
	RTD1625_GROUP(gpio_11),
	RTD1625_GROUP(usb_cc1),
	RTD1625_GROUP(usb_cc2),
	RTD1625_GROUP(gpio_45),
	RTD1625_GROUP(gpio_46),
	RTD1625_GROUP(gpio_47),
	RTD1625_GROUP(gpio_48),
	RTD1625_GROUP(gpio_49),
	RTD1625_GROUP(gpio_50),
	RTD1625_GROUP(gpio_52),
	RTD1625_GROUP(gpio_94),
	RTD1625_GROUP(gpio_95),
	RTD1625_GROUP(gpio_96),
	RTD1625_GROUP(gpio_97),
	RTD1625_GROUP(gpio_98),
	RTD1625_GROUP(gpio_99),
	RTD1625_GROUP(gpio_100),
	RTD1625_GROUP(gpio_101),
	RTD1625_GROUP(gpio_102),
	RTD1625_GROUP(gpio_103),
	RTD1625_GROUP(gpio_104),
	RTD1625_GROUP(gpio_105),
	RTD1625_GROUP(gpio_106),
	RTD1625_GROUP(gpio_107),
	RTD1625_GROUP(gpio_108),
	RTD1625_GROUP(gpio_109),
	RTD1625_GROUP(gpio_110),
	RTD1625_GROUP(gpio_111),
	RTD1625_GROUP(gpio_112),
	RTD1625_GROUP(gpio_128),
	RTD1625_GROUP(gpio_129),
	RTD1625_GROUP(gpio_130),
	RTD1625_GROUP(gpio_131),
	RTD1625_GROUP(gpio_145),
	RTD1625_GROUP(gpio_146),
	RTD1625_GROUP(gpio_147),
	RTD1625_GROUP(gpio_148),
	RTD1625_GROUP(gpio_149),
	RTD1625_GROUP(gpio_150),
	RTD1625_GROUP(gpio_151),
	RTD1625_GROUP(gpio_152),
	RTD1625_GROUP(gpio_153),
	RTD1625_GROUP(gpio_154),
	RTD1625_GROUP(gpio_155),
	RTD1625_GROUP(gpio_156),
	RTD1625_GROUP(gpio_157),
	RTD1625_GROUP(gpio_158),
	RTD1625_GROUP(gpio_159),
	RTD1625_GROUP(gpio_160),
	RTD1625_GROUP(gpio_161),
	RTD1625_GROUP(gpio_162),
	RTD1625_GROUP(gpio_163),
	RTD1625_GROUP(hi_width),
	RTD1625_GROUP(sf_en),
	RTD1625_GROUP(arm_trace_dbg_en),
	RTD1625_GROUP(ejtag_aucpu0_loc),
	RTD1625_GROUP(ejtag_aucpu1_loc),
	RTD1625_GROUP(ejtag_ve2_loc),
	RTD1625_GROUP(ejtag_scpu_loc),
	RTD1625_GROUP(ejtag_pcpu_loc),
	RTD1625_GROUP(ejtag_acpu_loc),
	RTD1625_GROUP(i2c6_loc),
	RTD1625_GROUP(uart0_loc),
	RTD1625_GROUP(ai_i2s1_loc),
	RTD1625_GROUP(ao_i2s1_loc),
	RTD1625_GROUP(etn_phy_loc),
	RTD1625_GROUP(spdif_loc),
	RTD1625_GROUP(rgmii_vdsel),
	RTD1625_GROUP(csi_vdsel),
	RTD1625_GROUP(spdif_in_mode),
};

static const struct rtd_pin_group_desc rtd1625_isom_pin_groups[] = {
	RTD1625_GROUP(gpio_0),
	RTD1625_GROUP(gpio_1),
	RTD1625_GROUP(gpio_28),
	RTD1625_GROUP(gpio_29),
	RTD1625_GROUP(ir_rx_loc),
};

static const struct rtd_pin_group_desc rtd1625_ve4_pin_groups[] = {
	RTD1625_GROUP(gpio_2),
	RTD1625_GROUP(gpio_3),
	RTD1625_GROUP(gpio_4),
	RTD1625_GROUP(gpio_5),
	RTD1625_GROUP(gpio_6),
	RTD1625_GROUP(gpio_7),
	RTD1625_GROUP(gpio_12),
	RTD1625_GROUP(gpio_13),
	RTD1625_GROUP(gpio_16),
	RTD1625_GROUP(gpio_17),
	RTD1625_GROUP(gpio_18),
	RTD1625_GROUP(gpio_19),
	RTD1625_GROUP(gpio_23),
	RTD1625_GROUP(gpio_24),
	RTD1625_GROUP(gpio_25),
	RTD1625_GROUP(gpio_30),
	RTD1625_GROUP(gpio_31),
	RTD1625_GROUP(gpio_32),
	RTD1625_GROUP(gpio_33),
	RTD1625_GROUP(gpio_34),
	RTD1625_GROUP(gpio_35),
	RTD1625_GROUP(gpio_42),
	RTD1625_GROUP(gpio_43),
	RTD1625_GROUP(gpio_44),
	RTD1625_GROUP(gpio_51),
	RTD1625_GROUP(gpio_53),
	RTD1625_GROUP(gpio_54),
	RTD1625_GROUP(gpio_55),
	RTD1625_GROUP(gpio_56),
	RTD1625_GROUP(gpio_57),
	RTD1625_GROUP(gpio_58),
	RTD1625_GROUP(gpio_59),
	RTD1625_GROUP(gpio_60),
	RTD1625_GROUP(gpio_61),
	RTD1625_GROUP(gpio_62),
	RTD1625_GROUP(gpio_63),
	RTD1625_GROUP(gpio_92),
	RTD1625_GROUP(gpio_93),
	RTD1625_GROUP(gpio_132),
	RTD1625_GROUP(gpio_133),
	RTD1625_GROUP(gpio_134),
	RTD1625_GROUP(gpio_135),
	RTD1625_GROUP(gpio_136),
	RTD1625_GROUP(gpio_137),
	RTD1625_GROUP(gpio_138),
	RTD1625_GROUP(gpio_139),
	RTD1625_GROUP(gpio_140),
	RTD1625_GROUP(gpio_141),
	RTD1625_GROUP(gpio_142),
	RTD1625_GROUP(gpio_143),
	RTD1625_GROUP(gpio_144),
	RTD1625_GROUP(gpio_164),
	RTD1625_GROUP(gpio_165),
	RTD1625_GROUP(ve4_uart_loc),
};

static const struct rtd_pin_group_desc rtd1625_main2_pin_groups[] = {
	RTD1625_GROUP(gpio_14),
	RTD1625_GROUP(gpio_15),
	RTD1625_GROUP(gpio_20),
	RTD1625_GROUP(gpio_21),
	RTD1625_GROUP(gpio_22),
	RTD1625_GROUP(hif_data),
	RTD1625_GROUP(hif_en),
	RTD1625_GROUP(hif_rdy),
	RTD1625_GROUP(hif_clk),
	RTD1625_GROUP(gpio_40),
	RTD1625_GROUP(gpio_41),
	RTD1625_GROUP(gpio_64),
	RTD1625_GROUP(gpio_65),
	RTD1625_GROUP(gpio_66),
	RTD1625_GROUP(gpio_67),
	RTD1625_GROUP(emmc_data_0),
	RTD1625_GROUP(emmc_data_1),
	RTD1625_GROUP(emmc_data_2),
	RTD1625_GROUP(emmc_data_3),
	RTD1625_GROUP(emmc_data_4),
	RTD1625_GROUP(emmc_data_5),
	RTD1625_GROUP(emmc_data_6),
	RTD1625_GROUP(emmc_data_7),
	RTD1625_GROUP(emmc_rst_n),
	RTD1625_GROUP(emmc_cmd),
	RTD1625_GROUP(emmc_clk),
	RTD1625_GROUP(emmc_dd_sb),
	RTD1625_GROUP(gpio_80),
	RTD1625_GROUP(gpio_81),
	RTD1625_GROUP(gpio_82),
	RTD1625_GROUP(gpio_83),
	RTD1625_GROUP(gpio_84),
	RTD1625_GROUP(gpio_85),
	RTD1625_GROUP(gpio_86),
	RTD1625_GROUP(gpio_87),
	RTD1625_GROUP(gpio_88),
	RTD1625_GROUP(gpio_89),
	RTD1625_GROUP(gpio_90),
	RTD1625_GROUP(gpio_91),
};

static const char * const rtd1625_iso_gpio_groups[] = {
	"gpio_10", "gpio_100", "gpio_101", "gpio_102", "gpio_103", "gpio_104",
	"gpio_105", "gpio_106", "gpio_107", "gpio_108", "gpio_109", "gpio_11",
	"gpio_110", "gpio_111", "gpio_112", "gpio_128", "gpio_129", "gpio_130",
	"gpio_131", "gpio_145", "gpio_146", "gpio_147", "gpio_148", "gpio_149",
	"gpio_150", "gpio_151", "gpio_152", "gpio_153", "gpio_154", "gpio_155",
	"gpio_156", "gpio_157", "gpio_158", "gpio_159", "gpio_160", "gpio_161",
	"gpio_162", "gpio_163", "gpio_45", "gpio_46", "gpio_47", "gpio_48",
	"gpio_49", "gpio_50", "gpio_52", "gpio_8", "gpio_9", "gpio_94", "gpio_95",
	"gpio_96", "gpio_97", "gpio_98", "gpio_99", "usb_cc1", "usb_cc2"
};

static const char * const rtd1625_iso_uart1_groups[] = {
	"gpio_10", "gpio_11", "gpio_8", "gpio_9"
};

static const char * const rtd1625_iso_iso_tristate_groups[] = {
	"gpio_10", "gpio_100", "gpio_101", "gpio_102", "gpio_103", "gpio_104",
	"gpio_105", "gpio_106", "gpio_107", "gpio_108", "gpio_109", "gpio_11",
	"gpio_110", "gpio_111", "gpio_112", "gpio_128", "gpio_129", "gpio_130",
	"gpio_131", "gpio_145", "gpio_146", "gpio_147", "gpio_148", "gpio_149",
	"gpio_150", "gpio_151", "gpio_152", "gpio_153", "gpio_154", "gpio_155",
	"gpio_156", "gpio_157", "gpio_158", "gpio_159", "gpio_160", "gpio_161",
	"gpio_162", "gpio_163", "gpio_45", "gpio_46", "gpio_47", "gpio_48",
	"gpio_49", "gpio_50", "gpio_52", "gpio_8", "gpio_9", "gpio_94", "gpio_95",
	"gpio_96", "gpio_97", "gpio_98", "gpio_99", "usb_cc1", "usb_cc2"
};

static const char * const rtd1625_iso_usb_cc1_groups[] = {
	"usb_cc1"
};

static const char * const rtd1625_iso_usb_cc2_groups[] = {
	"usb_cc2"
};

static const char * const rtd1625_iso_sdio_groups[] = {
	"gpio_45", "gpio_46", "gpio_47", "gpio_48", "gpio_49", "gpio_50"
};

static const char * const rtd1625_iso_scpu_ejtag_loc2_groups[] = {
	"ejtag_scpu_loc", "gpio_45", "gpio_46", "gpio_47", "gpio_48", "gpio_49"
};

static const char * const rtd1625_iso_acpu_ejtag_loc2_groups[] = {
	"ejtag_acpu_loc", "gpio_45", "gpio_46", "gpio_47", "gpio_48", "gpio_49"
};

static const char * const rtd1625_iso_pcpu_ejtag_loc2_groups[] = {
	"ejtag_pcpu_loc", "gpio_45", "gpio_46", "gpio_47", "gpio_48", "gpio_49"
};

static const char * const rtd1625_iso_aucpu0_ejtag_loc2_groups[] = {
	"ejtag_aucpu0_loc", "gpio_45", "gpio_46", "gpio_47", "gpio_48", "gpio_49"
};

static const char * const rtd1625_iso_ve2_ejtag_loc2_groups[] = {
	"ejtag_ve2_loc", "gpio_45", "gpio_46", "gpio_47", "gpio_48", "gpio_49"
};

static const char * const rtd1625_iso_aucpu1_ejtag_loc2_groups[] = {
	"ejtag_aucpu1_loc", "gpio_45", "gpio_46", "gpio_47", "gpio_48", "gpio_49"
};

static const char * const rtd1625_iso_pwm4_groups[] = {
	"gpio_52"
};

static const char * const rtd1625_iso_uart7_groups[] = {
	"gpio_94", "gpio_95"
};

static const char * const rtd1625_iso_pwm2_loc1_groups[] = {
	"gpio_95"
};

static const char * const rtd1625_iso_uart8_groups[] = {
	"gpio_96", "gpio_97"
};

static const char * const rtd1625_iso_pwm3_loc1_groups[] = {
	"gpio_97"
};

static const char * const rtd1625_iso_ai_tdm0_groups[] = {
	"gpio_100", "gpio_101", "gpio_102", "gpio_98"
};

static const char * const rtd1625_iso_vtc_i2s_groups[] = {
	"gpio_100", "gpio_101", "gpio_102", "gpio_161", "gpio_98"
};

static const char * const rtd1625_iso_ai_i2s0_groups[] = {
	"gpio_100", "gpio_101", "gpio_102", "gpio_156", "gpio_160", "gpio_161",
	"gpio_98"
};

static const char * const rtd1625_iso_ao_tdm0_groups[] = {
	"gpio_100", "gpio_101", "gpio_102", "gpio_99"
};

static const char * const rtd1625_iso_ao_i2s0_groups[] = {
	"gpio_100", "gpio_101", "gpio_102", "gpio_112", "gpio_99"
};

static const char * const rtd1625_iso_ai_tdm1_groups[] = {
	"gpio_103", "gpio_105", "gpio_106", "gpio_107"
};

static const char * const rtd1625_iso_ai_i2s1_loc0_groups[] = {
	"ai_i2s1_loc", "gpio_103", "gpio_105", "gpio_106", "gpio_107"
};

static const char * const rtd1625_iso_ao_i2s0_loc1_groups[] = {
	"gpio_103", "gpio_107"
};

static const char * const rtd1625_iso_ao_tdm1_loc0_groups[] = {
	"gpio_104"
};

static const char * const rtd1625_iso_ao_i2s1_loc0_groups[] = {
	"ao_i2s1_loc", "gpio_104", "gpio_105", "gpio_106", "gpio_107"
};

static const char * const rtd1625_iso_ao_tdm1_groups[] = {
	"gpio_105", "gpio_106", "gpio_107"
};

static const char * const rtd1625_iso_ai_tdm2_groups[] = {
	"gpio_108", "gpio_110", "gpio_111", "gpio_112"
};

static const char * const rtd1625_iso_pcm_groups[] = {
	"gpio_108", "gpio_109", "gpio_110", "gpio_111"
};

static const char * const rtd1625_iso_ai_i2s2_groups[] = {
	"gpio_108", "gpio_110", "gpio_111", "gpio_112"
};

static const char * const rtd1625_iso_ao_tdm2_groups[] = {
	"gpio_109", "gpio_110", "gpio_111", "gpio_112"
};

static const char * const rtd1625_iso_ao_i2s2_groups[] = {
	"gpio_109", "gpio_110", "gpio_111", "gpio_112"
};

static const char * const rtd1625_iso_vtc_ao_i2s_groups[] = {
	"gpio_109", "gpio_110", "gpio_111", "gpio_112"
};

static const char * const rtd1625_iso_scpu_ejtag_loc0_groups[] = {
	"ejtag_scpu_loc", "gpio_112"
};

static const char * const rtd1625_iso_acpu_ejtag_loc0_groups[] = {
	"ejtag_acpu_loc", "gpio_112"
};

static const char * const rtd1625_iso_pcpu_ejtag_loc0_groups[] = {
	"ejtag_pcpu_loc", "gpio_112"
};

static const char * const rtd1625_iso_aucpu0_ejtag_loc0_groups[] = {
	"ejtag_aucpu0_loc", "gpio_112"
};

static const char * const rtd1625_iso_ve2_ejtag_loc0_groups[] = {
	"ejtag_ve2_loc", "gpio_112"
};

static const char * const rtd1625_iso_gpu_ejtag_loc0_groups[] = {
	"gpio_112"
};

static const char * const rtd1625_iso_ao_tdm1_loc1_groups[] = {
	"gpio_112"
};

static const char * const rtd1625_iso_aucpu1_ejtag_loc0_groups[] = {
	"ejtag_aucpu1_loc", "gpio_112"
};

static const char * const rtd1625_iso_edptx_hdp_groups[] = {
	"gpio_128"
};

static const char * const rtd1625_iso_pwm5_groups[] = {
	"gpio_130"
};

static const char * const rtd1625_iso_vi0_dtv_groups[] = {
	"gpio_145", "gpio_146", "gpio_147", "gpio_148", "gpio_149", "gpio_150",
	"gpio_151", "gpio_152", "gpio_153", "gpio_154", "gpio_155", "gpio_156",
	"gpio_157", "gpio_158", "gpio_159", "gpio_160", "gpio_161"
};

static const char * const rtd1625_iso_vi1_dtv_groups[] = {
	"gpio_154", "gpio_155", "gpio_156", "gpio_157", "gpio_158", "gpio_159",
	"gpio_160", "gpio_161", "gpio_162"
};

static const char * const rtd1625_iso_ao_i2s0_loc0_groups[] = {
	"gpio_154", "gpio_155"
};

static const char * const rtd1625_iso_dmic0_groups[] = {
	"gpio_156", "gpio_157"
};

static const char * const rtd1625_iso_vtc_dmic_groups[] = {
	"gpio_156", "gpio_157", "gpio_158", "gpio_159"
};

static const char * const rtd1625_iso_ai_i2s1_loc1_groups[] = {
	"ai_i2s1_loc", "gpio_157", "gpio_158", "gpio_159", "gpio_160"
};

static const char * const rtd1625_iso_ao_i2s1_loc1_groups[] = {
	"ao_i2s1_loc", "gpio_157", "gpio_158", "gpio_159", "gpio_161"
};

static const char * const rtd1625_iso_dmic1_groups[] = {
	"gpio_158", "gpio_159"
};

static const char * const rtd1625_iso_dmic2_groups[] = {
	"gpio_160", "gpio_161"
};

static const char * const rtd1625_iso_pwm0_loc2_groups[] = {
	"gpio_162"
};

static const char * const rtd1625_iso_spdif_in_coaxial_groups[] = {
	"gpio_163", "spdif_sel", "spdif_in_mode"
};

static const char * const rtd1625_iso_spdif_in_gpio_groups[] = {
	"spdif_in_mode"
};

static const char * const rtd1625_iso_hi_width_disable_groups[] = {
	"hi_width"
};

static const char * const rtd1625_iso_hi_width_1bit_groups[] = {
	"hi_width"
};

static const char * const rtd1625_iso_sf_disable_groups[] = {
	"sf_en"
};

static const char * const rtd1625_iso_sf_enable_groups[] = {
	"sf_en"
};

static const char * const rtd1625_iso_arm_trace_debug_disable_groups[] = {
	"arm_trace_dbg_en"
};

static const char * const rtd1625_iso_arm_trace_debug_enable_groups[] = {
	"arm_trace_dbg_en"
};

static const char * const rtd1625_iso_aucpu0_ejtag_loc1_groups[] = {
	"ejtag_aucpu0_loc"
};

static const char * const rtd1625_iso_aucpu1_ejtag_loc1_groups[] = {
	"ejtag_aucpu1_loc"
};

static const char * const rtd1625_iso_ve2_ejtag_loc1_groups[] = {
	"ejtag_ve2_loc"
};

static const char * const rtd1625_iso_scpu_ejtag_loc1_groups[] = {
	"ejtag_scpu_loc"
};

static const char * const rtd1625_iso_pcpu_ejtag_loc1_groups[] = {
	"ejtag_pcpu_loc"
};

static const char * const rtd1625_iso_acpu_ejtag_loc1_groups[] = {
	"ejtag_acpu_loc"
};

static const char * const rtd1625_iso_i2c6_loc0_groups[] = {
	"i2c6_loc"
};

static const char * const rtd1625_iso_i2c6_loc1_groups[] = {
	"i2c6_loc"
};

static const char * const rtd1625_iso_uart0_loc0_groups[] = {
	"uart0_loc"
};

static const char * const rtd1625_iso_uart0_loc1_groups[] = {
	"uart0_loc"
};

static const char * const rtd1625_iso_etn_phy_loc0_groups[] = {
	"etn_phy_loc"
};

static const char * const rtd1625_iso_etn_phy_loc1_groups[] = {
	"etn_phy_loc"
};

static const char * const rtd1625_iso_spdif_loc0_groups[] = {
	"spdif_loc"
};

static const char * const rtd1625_iso_spdif_loc1_groups[] = {
	"spdif_loc"
};

static const char * const rtd1625_iso_rgmii_1v2_groups[] = {
	"rgmii_vdsel"
};

static const char * const rtd1625_iso_rgmii_1v8_groups[] = {
	"rgmii_vdsel"
};

static const char * const rtd1625_iso_rgmii_2v5_groups[] = {
	"rgmii_vdsel"
};

static const char * const rtd1625_iso_rgmii_3v3_groups[] = {
	"rgmii_vdsel"
};

static const char * const rtd1625_iso_csi_1v2_groups[] = {
	"csi_vdsel"
};

static const char * const rtd1625_iso_csi_1v8_groups[] = {
	"csi_vdsel"
};

static const char * const rtd1625_iso_csi_2v5_groups[] = {
	"csi_vdsel"
};

static const char * const rtd1625_iso_csi_3v3_groups[] = {
	"csi_vdsel"
};

static const char * const rtd1625_isom_gpio_groups[] = {
	"gpio_0", "gpio_1", "gpio_28", "gpio_29"
};

static const char * const rtd1625_isom_pctrl_groups[] = {
	"gpio_0", "gpio_1", "gpio_28", "gpio_29"
};

static const char * const rtd1625_isom_iso_tristate_groups[] = {
	"gpio_0", "gpio_1", "gpio_28", "gpio_29"
};

static const char * const rtd1625_isom_ir_rx_loc1_groups[] = {
	"gpio_1", "ir_rx_loc"
};

static const char * const rtd1625_isom_uart10_groups[] = {
	"gpio_28", "gpio_29"
};

static const char * const rtd1625_isom_isom_dbg_out_groups[] = {
	"gpio_28", "gpio_29"
};

static const char * const rtd1625_isom_ir_rx_loc0_groups[] = {
	"gpio_29", "ir_rx_loc"
};

static const char * const rtd1625_ve4_gpio_groups[] = {
	"gpio_12", "gpio_13", "gpio_132", "gpio_133", "gpio_134", "gpio_135",
	"gpio_136", "gpio_137", "gpio_138", "gpio_139", "gpio_140", "gpio_141",
	"gpio_142", "gpio_143", "gpio_144", "gpio_16", "gpio_164", "gpio_165",
	"gpio_17", "gpio_18", "gpio_19", "gpio_2", "gpio_23", "gpio_24", "gpio_25",
	"gpio_3", "gpio_30", "gpio_31", "gpio_32", "gpio_33", "gpio_34", "gpio_35",
	"gpio_4", "gpio_42", "gpio_43", "gpio_44", "gpio_5", "gpio_51", "gpio_53",
	"gpio_54", "gpio_55", "gpio_56", "gpio_57", "gpio_58", "gpio_59", "gpio_6",
	"gpio_60", "gpio_61", "gpio_62", "gpio_63", "gpio_7", "gpio_92", "gpio_93"
};

static const char * const rtd1625_ve4_uart0_loc0_groups[] = {
	"gpio_2", "gpio_3"
};

static const char * const rtd1625_ve4_iso_tristate_groups[] = {
	"gpio_12", "gpio_13", "gpio_132", "gpio_133", "gpio_134", "gpio_135",
	"gpio_136", "gpio_137", "gpio_138", "gpio_139", "gpio_140", "gpio_141",
	"gpio_142", "gpio_143", "gpio_144", "gpio_16", "gpio_164", "gpio_165",
	"gpio_17", "gpio_18", "gpio_19", "gpio_2", "gpio_23", "gpio_24", "gpio_25",
	"gpio_3", "gpio_30", "gpio_31", "gpio_32", "gpio_33", "gpio_34", "gpio_35",
	"gpio_4", "gpio_42", "gpio_43", "gpio_44", "gpio_5", "gpio_51", "gpio_53",
	"gpio_54", "gpio_55", "gpio_56", "gpio_57", "gpio_58", "gpio_59", "gpio_6",
	"gpio_60", "gpio_61", "gpio_62", "gpio_63", "gpio_7", "gpio_92", "gpio_93"
};

static const char * const rtd1625_ve4_uart2_groups[] = {
	"gpio_4", "gpio_5", "gpio_6", "gpio_7"
};

static const char * const rtd1625_ve4_gspi0_groups[] = {
	"gpio_4", "gpio_5", "gpio_6", "gpio_7"
};

static const char * const rtd1625_ve4_scpu_ejtag_loc0_groups[] = {
	"gpio_4", "gpio_5", "gpio_6", "gpio_7"
};

static const char * const rtd1625_ve4_acpu_ejtag_loc0_groups[] = {
	"gpio_4", "gpio_5", "gpio_6", "gpio_7"
};

static const char * const rtd1625_ve4_pcpu_ejtag_loc0_groups[] = {
	"gpio_4", "gpio_5", "gpio_6", "gpio_7"
};

static const char * const rtd1625_ve4_aucpu0_ejtag_loc0_groups[] = {
	"gpio_4", "gpio_5", "gpio_6", "gpio_7"
};

static const char * const rtd1625_ve4_ve2_ejtag_loc0_groups[] = {
	"gpio_4", "gpio_5", "gpio_6", "gpio_7"
};

static const char * const rtd1625_ve4_gpu_ejtag_loc0_groups[] = {
	"gpio_4", "gpio_5", "gpio_6", "gpio_7"
};

static const char * const rtd1625_ve4_aucpu1_ejtag_loc0_groups[] = {
	"gpio_4", "gpio_5", "gpio_6", "gpio_7"
};

static const char * const rtd1625_ve4_pwm0_loc1_groups[] = {
	"gpio_6"
};

static const char * const rtd1625_ve4_pwm1_loc0_groups[] = {
	"gpio_7"
};

static const char * const rtd1625_ve4_i2c0_groups[] = {
	"gpio_12", "gpio_13"
};

static const char * const rtd1625_ve4_pwm0_loc3_groups[] = {
	"gpio_12"
};

static const char * const rtd1625_ve4_dptx_hpd_groups[] = {
	"gpio_16"
};

static const char * const rtd1625_ve4_pwm2_loc0_groups[] = {
	"gpio_16"
};

static const char * const rtd1625_ve4_pcie0_groups[] = {
	"gpio_18"
};

static const char * const rtd1625_ve4_pwm3_loc0_groups[] = {
	"gpio_19"
};

static const char * const rtd1625_ve4_i2c3_groups[] = {
	"gpio_24", "gpio_25"
};

static const char * const rtd1625_ve4_pcie1_groups[] = {
	"gpio_30"
};

static const char * const rtd1625_ve4_uart9_groups[] = {
	"gpio_32", "gpio_33"
};

static const char * const rtd1625_ve4_ve4_uart_loc2_groups[] = {
	"gpio_32", "gpio_33", "ve4_uart_loc"
};

static const char * const rtd1625_ve4_sd_groups[] = {
	"gpio_42", "gpio_43"
};

static const char * const rtd1625_ve4_i2c6_loc1_groups[] = {
	"gpio_51", "gpio_61"
};

static const char * const rtd1625_ve4_uart3_groups[] = {
	"gpio_53", "gpio_54", "gpio_55", "gpio_56"
};

static const char * const rtd1625_ve4_ts0_groups[] = {
	"gpio_53", "gpio_54", "gpio_55", "gpio_56"
};

static const char * const rtd1625_ve4_gspi2_groups[] = {
	"gpio_53", "gpio_54", "gpio_55", "gpio_56"
};

static const char * const rtd1625_ve4_ve4_uart_loc0_groups[] = {
	"gpio_53", "gpio_54", "ve4_uart_loc"
};

static const char * const rtd1625_ve4_uart5_groups[] = {
	"gpio_57", "gpio_58"
};

static const char * const rtd1625_ve4_uart0_loc1_groups[] = {
	"gpio_57", "gpio_58"
};

static const char * const rtd1625_ve4_gspi1_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60"
};

static const char * const rtd1625_ve4_uart4_groups[] = {
	"gpio_59", "gpio_60"
};

static const char * const rtd1625_ve4_i2c4_groups[] = {
	"gpio_59", "gpio_60"
};

static const char * const rtd1625_ve4_spdif_out_groups[] = {
	"gpio_61"
};

static const char * const rtd1625_ve4_spdif_in_optical_groups[] = {
	"gpio_61"
};

static const char * const rtd1625_ve4_pll_test_loc0_groups[] = {
	"gpio_62", "gpio_63"
};

static const char * const rtd1625_ve4_uart6_groups[] = {
	"gpio_92", "gpio_93"
};

static const char * const rtd1625_ve4_i2c7_groups[] = {
	"gpio_92", "gpio_93"
};

static const char * const rtd1625_ve4_ve4_uart_loc1_groups[] = {
	"gpio_92", "gpio_93", "ve4_uart_loc"
};

static const char * const rtd1625_ve4_pwm1_loc1_groups[] = {
	"gpio_93"
};

static const char * const rtd1625_ve4_pwm6_groups[] = {
	"gpio_132"
};

static const char * const rtd1625_ve4_ts1_groups[] = {
	"gpio_133", "gpio_134", "gpio_135", "gpio_136"
};

static const char * const rtd1625_ve4_pwm0_loc0_groups[] = {
	"gpio_136"
};

static const char * const rtd1625_ve4_i2c6_loc0_groups[] = {
	"gpio_137", "gpio_138"
};

static const char * const rtd1625_ve4_csi0_groups[] = {
	"gpio_141"
};

static const char * const rtd1625_ve4_csi1_groups[] = {
	"gpio_144"
};

static const char * const rtd1625_ve4_etn_led_loc1_groups[] = {
	"gpio_164", "gpio_165"
};

static const char * const rtd1625_ve4_etn_phy_loc1_groups[] = {
	"gpio_164", "gpio_165"
};

static const char * const rtd1625_ve4_i2c5_groups[] = {
	"gpio_164", "gpio_165"
};

static const char * const rtd1625_main2_gpio_groups[] = {
	"emmc_clk", "emmc_cmd", "emmc_data_0", "emmc_data_1", "emmc_data_2",
	"emmc_data_3", "emmc_data_4", "emmc_data_5", "emmc_data_6", "emmc_data_7",
	"emmc_dd_sb", "emmc_rst_n", "gpio_14", "gpio_15", "gpio_20", "gpio_21",
	"gpio_22", "gpio_40", "gpio_41", "gpio_64", "gpio_65", "gpio_66", "gpio_67",
	"gpio_80", "gpio_81", "gpio_82", "gpio_83", "gpio_84", "gpio_85", "gpio_86",
	"gpio_87", "gpio_88", "gpio_89", "gpio_90", "gpio_91", "hif_clk",
	"hif_data", "hif_en", "hif_rdy"
};

static const char * const rtd1625_main2_emmc_groups[] = {
	"emmc_clk", "emmc_cmd", "emmc_data_0", "emmc_data_1", "emmc_data_2",
	"emmc_data_3", "emmc_data_4", "emmc_data_5", "emmc_data_6", "emmc_data_7",
	"emmc_dd_sb", "emmc_rst_n"
};

static const char * const rtd1625_main2_iso_tristate_groups[] = {
	"emmc_clk", "emmc_cmd", "emmc_data_0", "emmc_data_1", "emmc_data_2",
	"emmc_data_3", "emmc_data_4", "emmc_data_5", "emmc_data_6", "emmc_data_7",
	"emmc_dd_sb", "emmc_rst_n", "gpio_14", "gpio_15", "gpio_20", "gpio_21",
	"gpio_40", "gpio_41", "gpio_64", "gpio_65", "gpio_66", "gpio_67", "gpio_80",
	"gpio_81", "gpio_82", "gpio_83", "gpio_84", "gpio_85", "gpio_86", "gpio_87",
	"gpio_88", "gpio_89", "gpio_90", "gpio_91", "hif_clk", "hif_data", "hif_en",
	"hif_rdy"
};

static const char * const rtd1625_main2_nf_groups[] = {
	"emmc_data_0", "emmc_data_1", "emmc_data_2", "emmc_data_3", "emmc_data_4",
	"emmc_data_5"
};

static const char * const rtd1625_main2_etn_led_loc0_groups[] = {
	"gpio_14", "gpio_15"
};

static const char * const rtd1625_main2_etn_phy_loc0_groups[] = {
	"gpio_14", "gpio_15"
};

static const char * const rtd1625_main2_rgmii_groups[] = {
	"gpio_14", "gpio_15", "gpio_80", "gpio_81", "gpio_82", "gpio_83", "gpio_84",
	"gpio_85", "gpio_86", "gpio_87", "gpio_88", "gpio_89", "gpio_90", "gpio_91"
};

static const char * const rtd1625_main2_i2c1_groups[] = {
	"gpio_20", "gpio_21"
};

static const char * const rtd1625_main2_dbg_out1_groups[] = {
	"gpio_22"
};

static const char * const rtd1625_main2_sd_groups[] = {
	"gpio_40", "gpio_41", "hif_clk", "hif_data", "hif_en", "hif_rdy"
};

static const char * const rtd1625_main2_scpu_ejtag_loc1_groups[] = {
	"gpio_40", "hif_clk", "hif_data", "hif_en", "hif_rdy"
};

static const char * const rtd1625_main2_acpu_ejtag_loc1_groups[] = {
	"gpio_40", "hif_clk", "hif_data", "hif_en", "hif_rdy"
};

static const char * const rtd1625_main2_pcpu_ejtag_loc1_groups[] = {
	"gpio_40", "hif_clk", "hif_data", "hif_en", "hif_rdy"
};

static const char * const rtd1625_main2_aupu0_ejtag_loc1_groups[] = {
	"gpio_40", "hif_clk", "hif_data", "hif_en", "hif_rdy"
};

static const char * const rtd1625_main2_ve2_ejtag_loc1_groups[] = {
	"gpio_40", "hif_clk", "hif_data", "hif_en", "hif_rdy"
};

static const char * const rtd1625_main2_hi_loc0_groups[] = {
	"hif_clk", "hif_data", "hif_en", "hif_rdy"
};

static const char * const rtd1625_main2_hi_m_groups[] = {
	"hif_clk", "hif_data", "hif_en", "hif_rdy"
};

static const char * const rtd1625_main2_aupu1_ejtag_loc1_groups[] = {
	"gpio_40", "hif_clk", "hif_data", "hif_en", "hif_rdy"
};

static const char * const rtd1625_main2_spi_groups[] = {
	"gpio_64", "gpio_65", "gpio_66", "gpio_67"
};

static const char * const rtd1625_main2_pll_test_loc1_groups[] = {
	"gpio_65", "gpio_66"
};

static const char * const rtd1625_main2_rmii_groups[] = {
	"gpio_80", "gpio_81", "gpio_82", "gpio_83", "gpio_84", "gpio_87", "gpio_88",
	"gpio_89"
};

#define RTD1625_FUNC(_group, _name) \
	{ \
		.name = # _name, \
		.groups = rtd1625_ ## _group ## _ ## _name ## _groups, \
		.num_groups = ARRAY_SIZE(rtd1625_ ## _group ## _ ## _name ## _groups), \
	}

static const struct rtd_pin_func_desc rtd1625_iso_pin_functions[] = {
	RTD1625_FUNC(iso, gpio),
	RTD1625_FUNC(iso, uart1),
	RTD1625_FUNC(iso, iso_tristate),
	RTD1625_FUNC(iso, usb_cc1),
	RTD1625_FUNC(iso, usb_cc2),
	RTD1625_FUNC(iso, sdio),
	RTD1625_FUNC(iso, scpu_ejtag_loc2),
	RTD1625_FUNC(iso, acpu_ejtag_loc2),
	RTD1625_FUNC(iso, pcpu_ejtag_loc2),
	RTD1625_FUNC(iso, aucpu0_ejtag_loc2),
	RTD1625_FUNC(iso, ve2_ejtag_loc2),
	RTD1625_FUNC(iso, aucpu1_ejtag_loc2),
	RTD1625_FUNC(iso, pwm4),
	RTD1625_FUNC(iso, uart7),
	RTD1625_FUNC(iso, pwm2_loc1),
	RTD1625_FUNC(iso, uart8),
	RTD1625_FUNC(iso, pwm3_loc1),
	RTD1625_FUNC(iso, ai_tdm0),
	RTD1625_FUNC(iso, vtc_i2s),
	RTD1625_FUNC(iso, ai_i2s0),
	RTD1625_FUNC(iso, ao_tdm0),
	RTD1625_FUNC(iso, ao_i2s0),
	RTD1625_FUNC(iso, ai_tdm1),
	RTD1625_FUNC(iso, ai_i2s1_loc0),
	RTD1625_FUNC(iso, ao_i2s0_loc1),
	RTD1625_FUNC(iso, ao_tdm1_loc0),
	RTD1625_FUNC(iso, ao_i2s1_loc0),
	RTD1625_FUNC(iso, ao_tdm1),
	RTD1625_FUNC(iso, ai_tdm2),
	RTD1625_FUNC(iso, pcm),
	RTD1625_FUNC(iso, ai_i2s2),
	RTD1625_FUNC(iso, ao_tdm2),
	RTD1625_FUNC(iso, ao_i2s2),
	RTD1625_FUNC(iso, vtc_ao_i2s),
	RTD1625_FUNC(iso, scpu_ejtag_loc0),
	RTD1625_FUNC(iso, acpu_ejtag_loc0),
	RTD1625_FUNC(iso, pcpu_ejtag_loc0),
	RTD1625_FUNC(iso, aucpu0_ejtag_loc0),
	RTD1625_FUNC(iso, ve2_ejtag_loc0),
	RTD1625_FUNC(iso, gpu_ejtag_loc0),
	RTD1625_FUNC(iso, ao_tdm1_loc1),
	RTD1625_FUNC(iso, aucpu1_ejtag_loc0),
	RTD1625_FUNC(iso, edptx_hdp),
	RTD1625_FUNC(iso, pwm5),
	RTD1625_FUNC(iso, vi0_dtv),
	RTD1625_FUNC(iso, vi1_dtv),
	RTD1625_FUNC(iso, ao_i2s0_loc0),
	RTD1625_FUNC(iso, dmic0),
	RTD1625_FUNC(iso, vtc_dmic),
	RTD1625_FUNC(iso, ai_i2s1_loc1),
	RTD1625_FUNC(iso, ao_i2s1_loc1),
	RTD1625_FUNC(iso, dmic1),
	RTD1625_FUNC(iso, dmic2),
	RTD1625_FUNC(iso, pwm0_loc2),
	RTD1625_FUNC(iso, spdif_in_coaxial),
	RTD1625_FUNC(iso, spdif_in_gpio),
	RTD1625_FUNC(iso, hi_width_disable),
	RTD1625_FUNC(iso, hi_width_1bit),
	RTD1625_FUNC(iso, sf_disable),
	RTD1625_FUNC(iso, sf_enable),
	RTD1625_FUNC(iso, arm_trace_debug_disable),
	RTD1625_FUNC(iso, arm_trace_debug_enable),
	RTD1625_FUNC(iso, aucpu0_ejtag_loc1),
	RTD1625_FUNC(iso, aucpu1_ejtag_loc1),
	RTD1625_FUNC(iso, ve2_ejtag_loc1),
	RTD1625_FUNC(iso, scpu_ejtag_loc1),
	RTD1625_FUNC(iso, pcpu_ejtag_loc1),
	RTD1625_FUNC(iso, acpu_ejtag_loc1),
	RTD1625_FUNC(iso, i2c6_loc0),
	RTD1625_FUNC(iso, i2c6_loc1),
	RTD1625_FUNC(iso, uart0_loc0),
	RTD1625_FUNC(iso, uart0_loc1),
	RTD1625_FUNC(iso, etn_phy_loc0),
	RTD1625_FUNC(iso, etn_phy_loc1),
	RTD1625_FUNC(iso, spdif_loc0),
	RTD1625_FUNC(iso, spdif_loc1),
	RTD1625_FUNC(iso, rgmii_1v2),
	RTD1625_FUNC(iso, rgmii_1v8),
	RTD1625_FUNC(iso, rgmii_2v5),
	RTD1625_FUNC(iso, rgmii_3v3),
	RTD1625_FUNC(iso, csi_1v2),
	RTD1625_FUNC(iso, csi_1v8),
	RTD1625_FUNC(iso, csi_2v5),
	RTD1625_FUNC(iso, csi_3v3),
};

static const struct rtd_pin_func_desc rtd1625_isom_pin_functions[] = {
	RTD1625_FUNC(isom, gpio),
	RTD1625_FUNC(isom, pctrl),
	RTD1625_FUNC(isom, iso_tristate),
	RTD1625_FUNC(isom, ir_rx_loc1),
	RTD1625_FUNC(isom, uart10),
	RTD1625_FUNC(isom, isom_dbg_out),
	RTD1625_FUNC(isom, ir_rx_loc0),
};

static const struct rtd_pin_func_desc rtd1625_ve4_pin_functions[] = {
	RTD1625_FUNC(ve4, gpio),
	RTD1625_FUNC(ve4, uart0_loc0),
	RTD1625_FUNC(ve4, iso_tristate),
	RTD1625_FUNC(ve4, uart2),
	RTD1625_FUNC(ve4, gspi0),
	RTD1625_FUNC(ve4, scpu_ejtag_loc0),
	RTD1625_FUNC(ve4, acpu_ejtag_loc0),
	RTD1625_FUNC(ve4, pcpu_ejtag_loc0),
	RTD1625_FUNC(ve4, aucpu0_ejtag_loc0),
	RTD1625_FUNC(ve4, ve2_ejtag_loc0),
	RTD1625_FUNC(ve4, gpu_ejtag_loc0),
	RTD1625_FUNC(ve4, aucpu1_ejtag_loc0),
	RTD1625_FUNC(ve4, pwm0_loc1),
	RTD1625_FUNC(ve4, pwm1_loc0),
	RTD1625_FUNC(ve4, i2c0),
	RTD1625_FUNC(ve4, pwm0_loc3),
	RTD1625_FUNC(ve4, dptx_hpd),
	RTD1625_FUNC(ve4, pwm2_loc0),
	RTD1625_FUNC(ve4, pcie0),
	RTD1625_FUNC(ve4, pwm3_loc0),
	RTD1625_FUNC(ve4, i2c3),
	RTD1625_FUNC(ve4, pcie1),
	RTD1625_FUNC(ve4, uart9),
	RTD1625_FUNC(ve4, ve4_uart_loc2),
	RTD1625_FUNC(ve4, sd),
	RTD1625_FUNC(ve4, i2c6_loc1),
	RTD1625_FUNC(ve4, uart3),
	RTD1625_FUNC(ve4, ts0),
	RTD1625_FUNC(ve4, gspi2),
	RTD1625_FUNC(ve4, ve4_uart_loc0),
	RTD1625_FUNC(ve4, uart5),
	RTD1625_FUNC(ve4, uart0_loc1),
	RTD1625_FUNC(ve4, gspi1),
	RTD1625_FUNC(ve4, uart4),
	RTD1625_FUNC(ve4, i2c4),
	RTD1625_FUNC(ve4, spdif_out),
	RTD1625_FUNC(ve4, spdif_in_optical),
	RTD1625_FUNC(ve4, pll_test_loc0),
	RTD1625_FUNC(ve4, uart6),
	RTD1625_FUNC(ve4, i2c7),
	RTD1625_FUNC(ve4, ve4_uart_loc1),
	RTD1625_FUNC(ve4, pwm1_loc1),
	RTD1625_FUNC(ve4, pwm6),
	RTD1625_FUNC(ve4, ts1),
	RTD1625_FUNC(ve4, pwm0_loc0),
	RTD1625_FUNC(ve4, i2c6_loc0),
	RTD1625_FUNC(ve4, csi0),
	RTD1625_FUNC(ve4, csi1),
	RTD1625_FUNC(ve4, etn_led_loc1),
	RTD1625_FUNC(ve4, etn_phy_loc1),
	RTD1625_FUNC(ve4, i2c5),
};

static const struct rtd_pin_func_desc rtd1625_main2_pin_functions[] = {
	RTD1625_FUNC(main2, gpio),
	RTD1625_FUNC(main2, emmc),
	RTD1625_FUNC(main2, iso_tristate),
	RTD1625_FUNC(main2, nf),
	RTD1625_FUNC(main2, etn_led_loc0),
	RTD1625_FUNC(main2, etn_phy_loc0),
	RTD1625_FUNC(main2, rgmii),
	RTD1625_FUNC(main2, i2c1),
	RTD1625_FUNC(main2, dbg_out1),
	RTD1625_FUNC(main2, sd),
	RTD1625_FUNC(main2, scpu_ejtag_loc1),
	RTD1625_FUNC(main2, acpu_ejtag_loc1),
	RTD1625_FUNC(main2, pcpu_ejtag_loc1),
	RTD1625_FUNC(main2, aupu0_ejtag_loc1),
	RTD1625_FUNC(main2, ve2_ejtag_loc1),
	RTD1625_FUNC(main2, hi_loc0),
	RTD1625_FUNC(main2, hi_m),
	RTD1625_FUNC(main2, aupu1_ejtag_loc1),
	RTD1625_FUNC(main2, spi),
	RTD1625_FUNC(main2, pll_test_loc1),
	RTD1625_FUNC(main2, rmii),
};

#undef RTD1625_FUNC

static const struct rtd_pin_desc rtd1625_iso_muxes[] = {
	[RTD1625_ISO_GPIO_8] = RTK_PIN_MUX(gpio_8, 0x0, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "uart1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_9] = RTK_PIN_MUX(gpio_9, 0x0, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "uart1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_10] = RTK_PIN_MUX(gpio_10, 0x0, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "uart1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_11] = RTK_PIN_MUX(gpio_11, 0x0, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "uart1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")
	),
	[RTD1625_ISO_USB_CC1] = RTK_PIN_MUX(usb_cc1, 0x0, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "usb_cc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")
	),
	[RTD1625_ISO_USB_CC2] = RTK_PIN_MUX(usb_cc2, 0x0, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "usb_cc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_45] = RTK_PIN_MUX(gpio_45, 0x0, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "sdio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 24), "scpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 24), "acpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 24), "pcpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 24), "aucpu0_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 24), "ve2_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 24), "aucpu1_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_46] = RTK_PIN_MUX(gpio_46, 0x0, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 28), "sdio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 28), "scpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 28), "acpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 28), "pcpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 28), "aucpu0_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 28), "ve2_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 28), "aucpu1_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "iso_tristate")
	),

	[RTD1625_ISO_GPIO_47] = RTK_PIN_MUX(gpio_47, 0x4, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "sdio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 0), "scpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 0), "acpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 0), "pcpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 0), "aucpu0_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 0), "ve2_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 0), "aucpu1_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_48] = RTK_PIN_MUX(gpio_48, 0x4, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "sdio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 4), "scpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 4), "acpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 4), "pcpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 4), "aucpu0_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 4), "ve2_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 4), "aucpu1_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_49] = RTK_PIN_MUX(gpio_49, 0x4, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "sdio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 8), "scpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 8), "acpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 8), "pcpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 8), "aucpu0_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 8), "ve2_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 8), "aucpu1_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_50] = RTK_PIN_MUX(gpio_50, 0x4, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "sdio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_52] = RTK_PIN_MUX(gpio_52, 0x4, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 16), "pwm4"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_94] = RTK_PIN_MUX(gpio_94, 0x4, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "uart7"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_95] = RTK_PIN_MUX(gpio_95, 0x4, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "uart7"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 24), "pwm2_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_96] = RTK_PIN_MUX(gpio_96, 0x4, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 28), "uart8"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "iso_tristate")
	),

	[RTD1625_ISO_GPIO_97] = RTK_PIN_MUX(gpio_97, 0x8, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "uart8"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 0), "pwm3_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_98] = RTK_PIN_MUX(gpio_98, 0x8, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "ai_tdm0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 4), "vtc_i2s"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 4), "ai_i2s0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_99] = RTK_PIN_MUX(gpio_99, 0x8, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "ao_tdm0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 8), "ao_i2s0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_100] = RTK_PIN_MUX(gpio_100, 0x8, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "ai_tdm0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "ao_tdm0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 12), "vtc_i2s"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 12), "ai_i2s0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 12), "ao_i2s0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_101] = RTK_PIN_MUX(gpio_101, 0x8, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "ai_tdm0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "ao_tdm0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 16), "vtc_i2s"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 16), "ai_i2s0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 16), "ao_i2s0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_102] = RTK_PIN_MUX(gpio_102, 0x8, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "ai_tdm0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "ao_tdm0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 20), "vtc_i2s"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 20), "ai_i2s0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 20), "ao_i2s0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_103] = RTK_PIN_MUX(gpio_103, 0x8, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "ai_tdm1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 24), "ai_i2s1_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 24), "ao_i2s0_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_104] = RTK_PIN_MUX(gpio_104, 0x8, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 28), "ao_tdm1_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 28), "ao_i2s1_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "iso_tristate")
	),

	[RTD1625_ISO_GPIO_105] = RTK_PIN_MUX(gpio_105, 0xc, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "ai_tdm1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "ao_tdm1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 0), "ai_i2s1_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 0), "ao_i2s1_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_106] = RTK_PIN_MUX(gpio_106, 0xc, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "ai_tdm1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "ao_tdm1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 4), "ai_i2s1_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 4), "ao_i2s1_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_107] = RTK_PIN_MUX(gpio_107, 0xc, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "ai_tdm1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "ao_tdm1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 8), "ai_i2s1_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 8), "ao_i2s1_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 8), "ao_i2s0_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_108] = RTK_PIN_MUX(gpio_108, 0xc, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "ai_tdm2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 12), "pcm"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 12), "ai_i2s2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_109] = RTK_PIN_MUX(gpio_109, 0xc, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "ao_tdm2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 16), "pcm"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 16), "ao_i2s2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 16), "vtc_ao_i2s"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_110] = RTK_PIN_MUX(gpio_110, 0xc, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "ai_tdm2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "ao_tdm2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 20), "pcm"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 20), "ai_i2s2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 20), "ao_i2s2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 20), "vtc_ao_i2s"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_111] = RTK_PIN_MUX(gpio_111, 0xc, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "ai_tdm2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 24), "ao_tdm2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 24), "pcm"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 24), "ai_i2s2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 24), "ao_i2s2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 24), "vtc_ao_i2s"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")
	),

	[RTD1625_ISO_GPIO_112] = RTK_PIN_MUX(gpio_112, 0x10, GENMASK(4, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "ai_tdm2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "ao_tdm2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 0), "scpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 0), "acpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 0), "pcpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 0), "aucpu0_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 0), "ve2_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 0), "ai_i2s2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 0), "ao_i2s2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 0), "gpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 0), "vtc_ao_i2s"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x10, 0), "ao_tdm1_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x11, 0), "aucpu1_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x13, 0), "ao_i2s0")
	),
	[RTD1625_ISO_GPIO_128] = RTK_PIN_MUX(gpio_128, 0x10, GENMASK(8, 5),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 5), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 5), "edptx_hdp"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 5), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_129] = RTK_PIN_MUX(gpio_129, 0x10, GENMASK(12, 9),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 9), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 9), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_130] = RTK_PIN_MUX(gpio_130, 0x10, GENMASK(16, 13),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 13), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 13), "pwm5"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 13), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_131] = RTK_PIN_MUX(gpio_131, 0x10, GENMASK(20, 17),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 17), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 17), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_145] = RTK_PIN_MUX(gpio_145, 0x10, GENMASK(24, 21),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 21), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 21), "vi0_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 21), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_146] = RTK_PIN_MUX(gpio_146, 0x10, GENMASK(28, 25),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 25), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 25), "vi0_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 25), "iso_tristate")
	),

	[RTD1625_ISO_GPIO_147] = RTK_PIN_MUX(gpio_147, 0x14, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "vi0_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_148] = RTK_PIN_MUX(gpio_148, 0x14, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "vi0_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_149] = RTK_PIN_MUX(gpio_149, 0x14, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "vi0_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_150] = RTK_PIN_MUX(gpio_150, 0x14, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "vi0_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_151] = RTK_PIN_MUX(gpio_151, 0x14, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "vi0_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_152] = RTK_PIN_MUX(gpio_152, 0x14, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "vi0_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_153] = RTK_PIN_MUX(gpio_153, 0x14, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "vi0_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_154] = RTK_PIN_MUX(gpio_154, 0x14, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 28), "vi0_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 28), "vi1_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 28), "ao_i2s0_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "iso_tristate")
	),

	[RTD1625_ISO_GPIO_155] = RTK_PIN_MUX(gpio_155, 0x18, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "vi0_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "vi1_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 0), "ao_i2s0_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_156] = RTK_PIN_MUX(gpio_156, 0x18, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "vi0_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "vi1_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 4), "dmic0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 4), "vtc_dmic"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 4), "ai_i2s0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_157] = RTK_PIN_MUX(gpio_157, 0x18, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "vi0_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "vi1_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 8), "dmic0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 8), "vtc_dmic"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 8), "ai_i2s1_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 8), "ao_i2s1_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_158] = RTK_PIN_MUX(gpio_158, 0x18, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "vi0_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "vi1_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 12), "dmic1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 12), "vtc_dmic"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 12), "ai_i2s1_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 12), "ao_i2s1_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_159] = RTK_PIN_MUX(gpio_159, 0x18, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "vi0_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "vi1_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 16), "dmic1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 16), "vtc_dmic"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 16), "ai_i2s1_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 16), "ao_i2s1_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_160] = RTK_PIN_MUX(gpio_160, 0x18, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "vi0_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "vi1_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 20), "dmic2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 20), "ai_i2s0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 20), "ai_i2s1_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_161] = RTK_PIN_MUX(gpio_161, 0x18, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "vi0_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 24), "vi1_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 24), "dmic2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 24), "vtc_i2s"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 24), "ai_i2s0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 24), "ao_i2s1_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")
	),
	[RTD1625_ISO_GPIO_162] = RTK_PIN_MUX(gpio_162, 0x18, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 28), "vi1_dtv"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 28), "pwm0_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "iso_tristate")
	),

	[RTD1625_ISO_GPIO_163] = RTK_PIN_MUX(gpio_163, 0x1c, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 0), "spdif_in_coaxial"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")
	),

	[RTD1625_ISO_HI_WIDTH] = RTK_PIN_MUX(hi_width, 0x120, GENMASK(9, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "hi_width_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "hi_width_1bit")
	),
	[RTD1625_ISO_SF_EN] = RTK_PIN_MUX(sf_en, 0x120, GENMASK(11, 11),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 11), "sf_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 11), "sf_enable")
	),
	[RTD1625_ISO_ARM_TRACE_DBG_EN] = RTK_PIN_MUX(arm_trace_dbg_en, 0x120, GENMASK(13, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "arm_trace_debug_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "arm_trace_debug_enable")
	),
	[RTD1625_ISO_EJTAG_AUCPU0_LOC] = RTK_PIN_MUX(ejtag_aucpu0_loc, 0x120, GENMASK(16, 14),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 14), "aucpu0_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 14), "aucpu0_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 14), "aucpu0_ejtag_loc2")
	),
	[RTD1625_ISO_EJTAG_AUCPU1_LOC] = RTK_PIN_MUX(ejtag_aucpu1_loc, 0x120, GENMASK(19, 17),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 17), "aucpu1_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 17), "aucpu1_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 17), "aucpu1_ejtag_loc2")
	),
	[RTD1625_ISO_EJTAG_VE2_LOC] = RTK_PIN_MUX(ejtag_ve2_loc, 0x120, GENMASK(22, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "ve2_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "ve2_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 20), "ve2_ejtag_loc2")
	),
	[RTD1625_ISO_EJTAG_SCPU_LOC] = RTK_PIN_MUX(ejtag_scpu_loc, 0x120, GENMASK(25, 23),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 23), "scpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 23), "scpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 23), "scpu_ejtag_loc2")
	),
	[RTD1625_ISO_EJTAG_PCPU_LOC] = RTK_PIN_MUX(ejtag_pcpu_loc, 0x120, GENMASK(28, 26),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 26), "pcpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 26), "pcpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 26), "pcpu_ejtag_loc2")
	),
	[RTD1625_ISO_EJTAG_ACPU_LOC] = RTK_PIN_MUX(ejtag_acpu_loc, 0x120, GENMASK(31, 29),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 29), "acpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 29), "acpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 29), "acpu_ejtag_loc2")
	),
	[RTD1625_ISO_I2C6_LOC] = RTK_PIN_MUX(i2c6_loc, 0x128, GENMASK(1, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "i2c6_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "i2c6_loc1")
	),
	[RTD1625_ISO_UART0_LOC] = RTK_PIN_MUX(uart0_loc, 0x128, GENMASK(3, 2),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 2), "uart0_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 2), "uart0_loc1")
	),
	[RTD1625_ISO_AI_I2S1_LOC] = RTK_PIN_MUX(ai_i2s1_loc, 0x128, GENMASK(5, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "ai_i2s1_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "ai_i2s1_loc1")
	),
	[RTD1625_ISO_AO_I2S1_LOC] = RTK_PIN_MUX(ao_i2s1_loc, 0x128, GENMASK(7, 6),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 6), "ao_i2s1_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 6), "ao_i2s1_loc1")
	),
	[RTD1625_ISO_ETN_PHY_LOC] = RTK_PIN_MUX(etn_phy_loc, 0x128, GENMASK(9, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "etn_phy_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "etn_phy_loc1")
	),
	[RTD1625_ISO_SPDIF_LOC] = RTK_PIN_MUX(spdif_loc, 0x128, GENMASK(14, 13),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 13), "spdif_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 13), "spdif_loc1")
	),
	[RTD1625_ISO_RGMII_VDSEL] = RTK_PIN_MUX(rgmii_vdsel, 0x188, GENMASK(17, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "rgmii_3v3"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "rgmii_2v5"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "rgmii_1v8"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 16), "rgmii_1v2")
	),
	[RTD1625_ISO_CSI_VDSEL] = RTK_PIN_MUX(csi_vdsel, 0x188, GENMASK(19, 18),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 18), "csi_3v3"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 18), "csi_2v5"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 18), "csi_1v8"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 18), "csi_1v2")
	),

	[RTD1625_ISO_SPDIF_IN_MODE] = RTK_PIN_MUX(spdif_in_mode, 0x188, GENMASK(20, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "spdif_in_gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "spdif_in_coaxial")
	),
};

static const struct rtd_pin_desc rtd1625_isom_muxes[] = {
	[RTD1625_ISOM_GPIO_0] = RTK_PIN_MUX(gpio_0, 0x0, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "pctrl"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")
	),
	[RTD1625_ISOM_GPIO_1] = RTK_PIN_MUX(gpio_1, 0x0, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "pctrl"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 4), "ir_rx_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")
	),
	[RTD1625_ISOM_GPIO_28] = RTK_PIN_MUX(gpio_28, 0x0, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "uart10"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "pctrl"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 8), "isom_dbg_out"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")
	),
	[RTD1625_ISOM_GPIO_29] = RTK_PIN_MUX(gpio_29, 0x0, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "uart10"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "pctrl"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 12), "ir_rx_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 12), "isom_dbg_out"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")
	),
	[RTD1625_ISOM_IR_RX_LOC] = RTK_PIN_MUX(ir_rx_loc, 0x30, GENMASK(1, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "ir_rx_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "ir_rx_loc1")
	),
};

static const struct rtd_pin_desc rtd1625_ve4_muxes[] = {
	[RTD1625_VE4_GPIO_2] = RTK_PIN_MUX(gpio_2, 0x0, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "uart0_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_3] = RTK_PIN_MUX(gpio_3, 0x0, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "uart0_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_4] = RTK_PIN_MUX(gpio_4, 0x0, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "uart2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "gspi0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 8), "scpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 8), "acpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 8), "pcpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 8), "aucpu0_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 8), "ve2_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 8), "gpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 8), "aucpu1_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_5] = RTK_PIN_MUX(gpio_5, 0x0, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "uart2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "gspi0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 12), "scpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 12), "acpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 12), "pcpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 12), "aucpu0_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 12), "ve2_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 12), "gpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 12), "aucpu1_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_6] = RTK_PIN_MUX(gpio_6, 0x0, GENMASK(20, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "uart2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "gspi0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 16), "scpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 16), "acpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 16), "pcpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 16), "aucpu0_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 16), "ve2_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 16), "gpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 16), "pwm0_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 16), "aucpu1_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_7] = RTK_PIN_MUX(gpio_7, 0x0, GENMASK(24, 21),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 21), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 21), "uart2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 21), "gspi0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 21), "scpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 21), "acpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 21), "pcpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 21), "aucpu0_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 21), "ve2_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 21), "gpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 21), "pwm1_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 21), "aucpu1_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 21), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_12] = RTK_PIN_MUX(gpio_12, 0x0, GENMASK(28, 25),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 25), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 25), "i2c0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 25), "pwm0_loc3"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 25), "iso_tristate")
	),

	[RTD1625_VE4_GPIO_13] = RTK_PIN_MUX(gpio_13, 0x4, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "i2c0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_16] = RTK_PIN_MUX(gpio_16, 0x4, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "dptx_hpd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 4), "pwm2_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_17] = RTK_PIN_MUX(gpio_17, 0x4, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_18] = RTK_PIN_MUX(gpio_18, 0x4, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "pcie0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_19] = RTK_PIN_MUX(gpio_19, 0x4, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 16), "pwm3_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_23] = RTK_PIN_MUX(gpio_23, 0x4, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_24] = RTK_PIN_MUX(gpio_24, 0x4, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 24), "i2c3"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_25] = RTK_PIN_MUX(gpio_25, 0x4, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 28), "i2c3"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "iso_tristate")
	),

	[RTD1625_VE4_GPIO_30] = RTK_PIN_MUX(gpio_30, 0x8, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "pcie1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_31] = RTK_PIN_MUX(gpio_31, 0x8, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_32] = RTK_PIN_MUX(gpio_32, 0x8, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "uart9"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 8), "ve4_uart_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_33] = RTK_PIN_MUX(gpio_33, 0x8, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "uart9"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 12), "ve4_uart_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_34] = RTK_PIN_MUX(gpio_34, 0x8, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_35] = RTK_PIN_MUX(gpio_35, 0x8, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_42] = RTK_PIN_MUX(gpio_42, 0x8, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_43] = RTK_PIN_MUX(gpio_43, 0x8, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 28), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "iso_tristate")
	),

	[RTD1625_VE4_GPIO_44] = RTK_PIN_MUX(gpio_44, 0xc, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_51] = RTK_PIN_MUX(gpio_51, 0xc, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "i2c6_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_53] = RTK_PIN_MUX(gpio_53, 0xc, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "uart3"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "ts0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 8), "gspi2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 8), "ve4_uart_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_54] = RTK_PIN_MUX(gpio_54, 0xc, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "uart3"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "ts0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 12), "gspi2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 12), "ve4_uart_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_55] = RTK_PIN_MUX(gpio_55, 0xc, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "uart3"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "ts0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 16), "gspi2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_56] = RTK_PIN_MUX(gpio_56, 0xc, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "uart3"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "ts0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 20), "gspi2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_57] = RTK_PIN_MUX(gpio_57, 0xc, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "uart5"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 24), "uart0_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 24), "gspi1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_58] = RTK_PIN_MUX(gpio_58, 0xc, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 28), "uart5"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 28), "uart0_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 28), "gspi1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "iso_tristate")
	),

	[RTD1625_VE4_GPIO_59] = RTK_PIN_MUX(gpio_59, 0x10, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "uart4"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "i2c4"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 0), "gspi1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_60] = RTK_PIN_MUX(gpio_60, 0x10, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "uart4"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "i2c4"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 4), "gspi1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_61] = RTK_PIN_MUX(gpio_61, 0x10, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "i2c6_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 8), "spdif_out"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 8), "spdif_in_optical"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_62] = RTK_PIN_MUX(gpio_62, 0x10, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "pll_test_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_63] = RTK_PIN_MUX(gpio_63, 0x10, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "pll_test_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_92] = RTK_PIN_MUX(gpio_92, 0x10, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "uart6"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "i2c7"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 20), "ve4_uart_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_93] = RTK_PIN_MUX(gpio_93, 0x10, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "uart6"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 24), "i2c7"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 24), "ve4_uart_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 24), "pwm1_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_132] = RTK_PIN_MUX(gpio_132, 0x10, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 28), "pwm6"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "iso_tristate")
	),

	[RTD1625_VE4_GPIO_133] = RTK_PIN_MUX(gpio_133, 0x14, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "ts1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_134] = RTK_PIN_MUX(gpio_134, 0x14, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "ts1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_135] = RTK_PIN_MUX(gpio_135, 0x14, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "ts1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_136] = RTK_PIN_MUX(gpio_136, 0x14, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "ts1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 12), "pwm0_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_137] = RTK_PIN_MUX(gpio_137, 0x14, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "i2c6_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_138] = RTK_PIN_MUX(gpio_138, 0x14, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "i2c6_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_139] = RTK_PIN_MUX(gpio_139, 0x14, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_140] = RTK_PIN_MUX(gpio_140, 0x14, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "iso_tristate")
	),

	[RTD1625_VE4_GPIO_141] = RTK_PIN_MUX(gpio_141, 0x18, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "csi0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_142] = RTK_PIN_MUX(gpio_142, 0x18, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_143] = RTK_PIN_MUX(gpio_143, 0x18, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_144] = RTK_PIN_MUX(gpio_144, 0x18, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "csi1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_164] = RTK_PIN_MUX(gpio_164, 0x18, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "etn_led_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "etn_phy_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 16), "i2c5"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")
	),
	[RTD1625_VE4_GPIO_165] = RTK_PIN_MUX(gpio_165, 0x18, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "etn_led_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "etn_phy_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 20), "i2c5"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")
	),
	[RTD1625_VE4_UART_LOC] = RTK_PIN_MUX(ve4_uart_loc, 0x80, GENMASK(2, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "ve4_uart_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "ve4_uart_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 0), "ve4_uart_loc2")
	),
};

static const struct rtd_pin_desc rtd1625_main2_muxes[] = {
	[RTD1625_MAIN2_EMMC_RST_N] = RTK_PIN_MUX(emmc_rst_n, 0x0, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")
	),
	[RTD1625_MAIN2_EMMC_DD_SB] = RTK_PIN_MUX(emmc_dd_sb, 0x0, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")
	),
	[RTD1625_MAIN2_EMMC_CLK] = RTK_PIN_MUX(emmc_clk, 0x0, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")
	),
	[RTD1625_MAIN2_EMMC_CMD] = RTK_PIN_MUX(emmc_cmd, 0x0, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")
	),
	[RTD1625_MAIN2_EMMC_DATA_0] = RTK_PIN_MUX(emmc_data_0, 0x0, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 16), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")
	),
	[RTD1625_MAIN2_EMMC_DATA_1] = RTK_PIN_MUX(emmc_data_1, 0x0, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 20), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")
	),
	[RTD1625_MAIN2_EMMC_DATA_2] = RTK_PIN_MUX(emmc_data_2, 0x0, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 24), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 24), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")
	),
	[RTD1625_MAIN2_EMMC_DATA_3] = RTK_PIN_MUX(emmc_data_3, 0x0, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 28), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 28), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "iso_tristate")
	),

	[RTD1625_MAIN2_EMMC_DATA_4] = RTK_PIN_MUX(emmc_data_4, 0x4, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 0), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")
	),
	[RTD1625_MAIN2_EMMC_DATA_5] = RTK_PIN_MUX(emmc_data_5, 0x4, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 4), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")
	),
	[RTD1625_MAIN2_EMMC_DATA_6] = RTK_PIN_MUX(emmc_data_6, 0x4, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")
	),
	[RTD1625_MAIN2_EMMC_DATA_7] = RTK_PIN_MUX(emmc_data_7, 0x4, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")
	),
	[RTD1625_MAIN2_GPIO_14] = RTK_PIN_MUX(gpio_14, 0x4, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "etn_led_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "etn_phy_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 16), "rgmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")
	),
	[RTD1625_MAIN2_GPIO_15] = RTK_PIN_MUX(gpio_15, 0x4, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "etn_led_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "etn_phy_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 20), "rgmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")
	),
	[RTD1625_MAIN2_GPIO_20] = RTK_PIN_MUX(gpio_20, 0x4, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 24), "i2c1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")
	),
	[RTD1625_MAIN2_GPIO_21] = RTK_PIN_MUX(gpio_21, 0x4, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 28), "i2c1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "iso_tristate")
	),

	[RTD1625_MAIN2_GPIO_22] = RTK_PIN_MUX(gpio_22, 0x8, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "dbg_out1")
	),
	[RTD1625_MAIN2_HIF_DATA] = RTK_PIN_MUX(hif_data, 0x8, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 4), "scpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 4), "acpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 4), "pcpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 4), "aupu0_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 4), "ve2_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 4), "hi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 4), "hi_m"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 4), "aupu1_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")
	),
	[RTD1625_MAIN2_HIF_EN] = RTK_PIN_MUX(hif_en, 0x8, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 8), "scpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 8), "acpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 8), "pcpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 8), "aupu0_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 8), "ve2_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 8), "hi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 8), "hi_m"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 8), "aupu1_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")
	),
	[RTD1625_MAIN2_HIF_RDY] = RTK_PIN_MUX(hif_rdy, 0x8, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 12), "scpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 12), "acpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 12), "pcpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 12), "aupu0_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 12), "ve2_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 12), "hi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 12), "hi_m"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 12), "aupu1_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")
	),
	[RTD1625_MAIN2_HIF_CLK] = RTK_PIN_MUX(hif_clk, 0x8, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 16), "scpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 16), "acpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 16), "pcpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 16), "aupu0_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 16), "ve2_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 16), "hi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 16), "hi_m"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 16), "aupu1_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")
	),
	[RTD1625_MAIN2_GPIO_40] = RTK_PIN_MUX(gpio_40, 0x8, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 20), "scpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 20), "acpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 20), "pcpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 20), "aupu0_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 20), "ve2_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 20), "aupu1_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")
	),
	[RTD1625_MAIN2_GPIO_41] = RTK_PIN_MUX(gpio_41, 0x8, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")
	),
	[RTD1625_MAIN2_GPIO_64] = RTK_PIN_MUX(gpio_64, 0x8, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 28), "spi"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "iso_tristate")
	),

	[RTD1625_MAIN2_GPIO_65] = RTK_PIN_MUX(gpio_65, 0xc, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "pll_test_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "spi"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")
	),
	[RTD1625_MAIN2_GPIO_66] = RTK_PIN_MUX(gpio_66, 0xc, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "pll_test_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "spi"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")
	),
	[RTD1625_MAIN2_GPIO_67] = RTK_PIN_MUX(gpio_67, 0xc, GENMASK(13, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "spi"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")
	),
	[RTD1625_MAIN2_GPIO_80] = RTK_PIN_MUX(gpio_80, 0xc, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "rmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 12), "rgmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")
	),
	[RTD1625_MAIN2_GPIO_81] = RTK_PIN_MUX(gpio_81, 0xc, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "rmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 16), "rgmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")
	),
	[RTD1625_MAIN2_GPIO_82] = RTK_PIN_MUX(gpio_82, 0xc, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "rmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 20), "rgmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")
	),
	[RTD1625_MAIN2_GPIO_83] = RTK_PIN_MUX(gpio_83, 0xc, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 24), "rmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 24), "rgmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")
	),
	[RTD1625_MAIN2_GPIO_84] = RTK_PIN_MUX(gpio_84, 0xc, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 28), "rmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 28), "rgmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "iso_tristate")
	),

	[RTD1625_MAIN2_GPIO_85] = RTK_PIN_MUX(gpio_85, 0x10, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 0), "rgmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")
	),
	[RTD1625_MAIN2_GPIO_86] = RTK_PIN_MUX(gpio_86, 0x10, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 4), "rgmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")
	),
	[RTD1625_MAIN2_GPIO_87] = RTK_PIN_MUX(gpio_87, 0x10, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "rmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 8), "rgmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")
	),
	[RTD1625_MAIN2_GPIO_88] = RTK_PIN_MUX(gpio_88, 0x10, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "rmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 12), "rgmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")
	),
	[RTD1625_MAIN2_GPIO_89] = RTK_PIN_MUX(gpio_89, 0x10, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "rmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 16), "rgmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")
	),
	[RTD1625_MAIN2_GPIO_90] = RTK_PIN_MUX(gpio_90, 0x10, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 20), "rgmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")
	),
	[RTD1625_MAIN2_GPIO_91] = RTK_PIN_MUX(gpio_91, 0x10, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 24), "rgmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")
	),
};

static const struct rtd_pin_config_desc rtd1625_iso_configs[] = {
	[RTD1625_ISO_GPIO_8] = RTK_PIN_CONFIG_V2(gpio_8, 0x20, 0, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_ISO_GPIO_9] = RTK_PIN_CONFIG_V2(gpio_9, 0x20, 6, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_ISO_GPIO_10] = RTK_PIN_CONFIG_V2(gpio_10, 0x20, 12, 1, 2, 0, 3, 4, 5,
						  PADDRI_4_8),
	[RTD1625_ISO_GPIO_11] = RTK_PIN_CONFIG_V2(gpio_11, 0x20, 18, 1, 2, 0, 3, 4, 5,
						  PADDRI_4_8),
	[RTD1625_ISO_GPIO_45] = RTK_PIN_CONFIG(gpio_45, 0x24, 0, 0, 1, NA, 2, 12, NA),
	[RTD1625_ISO_GPIO_46] = RTK_PIN_CONFIG(gpio_46, 0x24, 13, 0, 1, NA, 2, 12, NA),
	[RTD1625_ISO_GPIO_47] = RTK_PIN_CONFIG(gpio_47, 0x28, 0, 0, 1, NA, 2, 12, NA),
	[RTD1625_ISO_GPIO_48] = RTK_PIN_CONFIG(gpio_48, 0x28, 13, 0, 1, NA, 2, 12, NA),
	[RTD1625_ISO_GPIO_49] = RTK_PIN_CONFIG(gpio_49, 0x2c, 0, 0, 1, NA, 2, 12, NA),
	[RTD1625_ISO_GPIO_50] = RTK_PIN_CONFIG(gpio_50, 0x2c, 13, 0, 1, NA, 2, 12, NA),
	[RTD1625_ISO_GPIO_52] = RTK_PIN_CONFIG_V2(gpio_52, 0x2c, 26, 1, 2, 0, 3, 4, 5,
						  PADDRI_4_8),
	[RTD1625_ISO_GPIO_94] = RTK_PIN_CONFIG_V2(gpio_94, 0x30, 0, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_ISO_GPIO_95] = RTK_PIN_CONFIG_V2(gpio_95, 0x30, 6, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_ISO_GPIO_96] = RTK_PIN_CONFIG_V2(gpio_96, 0x30, 12, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_ISO_GPIO_97] = RTK_PIN_CONFIG_V2(gpio_97, 0x30, 18, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_ISO_GPIO_98] = RTK_PIN_CONFIG_V2(gpio_98, 0x30, 24, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_ISO_GPIO_99] = RTK_PIN_CONFIG_V2(gpio_99, 0x34, 0, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_ISO_GPIO_100] = RTK_PIN_CONFIG_V2(gpio_100, 0x34, 6, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_101] = RTK_PIN_CONFIG_V2(gpio_101, 0x34, 12, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_102] = RTK_PIN_CONFIG_V2(gpio_102, 0x34, 18, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_103] = RTK_PIN_CONFIG_V2(gpio_103, 0x34, 24, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_104] = RTK_PIN_CONFIG_V2(gpio_104, 0x38, 0, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_105] = RTK_PIN_CONFIG_V2(gpio_105, 0x38, 6, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_106] = RTK_PIN_CONFIG_V2(gpio_106, 0x38, 12, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_107] = RTK_PIN_CONFIG_V2(gpio_107, 0x38, 18, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_108] = RTK_PIN_CONFIG_V2(gpio_108, 0x38, 24, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_109] = RTK_PIN_CONFIG_V2(gpio_109, 0x3c, 0, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_110] = RTK_PIN_CONFIG_V2(gpio_110, 0x3c, 6, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_111] = RTK_PIN_CONFIG_V2(gpio_111, 0x3c, 12, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_112] = RTK_PIN_CONFIG_V2(gpio_112, 0x3c, 18, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_128] = RTK_PIN_CONFIG_V2(gpio_128, 0x3c, 24, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_129] = RTK_PIN_CONFIG_V2(gpio_129, 0x40, 0, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_130] = RTK_PIN_CONFIG_V2(gpio_130, 0x40, 6, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_131] = RTK_PIN_CONFIG_V2(gpio_131, 0x40, 12, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_145] = RTK_PIN_CONFIG_V2(gpio_145, 0x40, 18, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_146] = RTK_PIN_CONFIG_V2(gpio_146, 0x40, 24, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_147] = RTK_PIN_CONFIG_V2(gpio_147, 0x44, 0, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_148] = RTK_PIN_CONFIG_V2(gpio_148, 0x44, 6, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_149] = RTK_PIN_CONFIG_V2(gpio_149, 0x44, 12, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_150] = RTK_PIN_CONFIG_V2(gpio_150, 0x44, 18, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_151] = RTK_PIN_CONFIG_V2(gpio_151, 0x44, 24, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_152] = RTK_PIN_CONFIG_V2(gpio_152, 0x48, 0, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_153] = RTK_PIN_CONFIG_V2(gpio_153, 0x48, 6, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_154] = RTK_PIN_CONFIG_V2(gpio_154, 0x48, 12, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_155] = RTK_PIN_CONFIG_V2(gpio_155, 0x48, 18, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_156] = RTK_PIN_CONFIG_V2(gpio_156, 0x48, 24, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_157] = RTK_PIN_CONFIG_V2(gpio_157, 0x4c, 0, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_158] = RTK_PIN_CONFIG_V2(gpio_158, 0x4c, 6, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_159] = RTK_PIN_CONFIG_V2(gpio_159, 0x4c, 12, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_160] = RTK_PIN_CONFIG_V2(gpio_160, 0x4c, 18, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_161] = RTK_PIN_CONFIG_V2(gpio_161, 0x4c, 24, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_162] = RTK_PIN_CONFIG_V2(gpio_162, 0x50, 0, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_GPIO_163] = RTK_PIN_CONFIG_V2(gpio_163, 0x50, 6, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_ISO_USB_CC1] = RTK_PIN_CONFIG_V2(usb_cc1, 0x50, 12, NA, NA, 0, 1, 2, 3,
						  PADDRI_4_8),
	[RTD1625_ISO_USB_CC2] = RTK_PIN_CONFIG_V2(usb_cc2, 0x50, 16, NA, NA, 0, 1, 2, 3,
						  PADDRI_4_8),
};

static const struct rtd_pin_config_desc rtd1625_isom_configs[] = {
	[RTD1625_ISOM_GPIO_0] = RTK_PIN_CONFIG_V2(gpio_0, 0x4, 5, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_ISOM_GPIO_1] = RTK_PIN_CONFIG_V2(gpio_1, 0x4, 11, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_ISOM_GPIO_28] = RTK_PIN_CONFIG_V2(gpio_28, 0x4, 17, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_ISOM_GPIO_29] = RTK_PIN_CONFIG_V2(gpio_29, 0x4, 23, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
};

static const struct rtd_pin_config_desc rtd1625_ve4_configs[] = {
	[RTD1625_VE4_GPIO_2] = RTK_PIN_CONFIG_V2(gpio_2, 0x1c, 0, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_3] = RTK_PIN_CONFIG_V2(gpio_3, 0x1c, 6, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_4] = RTK_PIN_CONFIG_V2(gpio_4, 0x1c, 12, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_5] = RTK_PIN_CONFIG_V2(gpio_5, 0x1c, 18, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_6] = RTK_PIN_CONFIG_V2(gpio_6, 0x1c, 24, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_7] = RTK_PIN_CONFIG_V2(gpio_7, 0x20, 0, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_12] = RTK_PIN_CONFIG_V2(gpio_12, 0x20, 6, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_13] = RTK_PIN_CONFIG_V2(gpio_13, 0x20, 18, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_16] = RTK_PIN_CONFIG_V2(gpio_16, 0x20, 18, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_17] = RTK_PIN_CONFIG_V2(gpio_17, 0x20, 24, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_18] = RTK_PIN_CONFIG_V2(gpio_18, 0x24, 0, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_19] = RTK_PIN_CONFIG_V2(gpio_19, 0x24, 6, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_23] = RTK_PIN_CONFIG_V2(gpio_23, 0x24, 12, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_24] = RTK_PIN_CONFIG_V2(gpio_24, 0x24, 18, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_25] = RTK_PIN_CONFIG_V2(gpio_25, 0x24, 24, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_30] = RTK_PIN_CONFIG_V2(gpio_30, 0x28, 0, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_31] = RTK_PIN_CONFIG_V2(gpio_31, 0x28, 6, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_32] = RTK_PIN_CONFIG_V2(gpio_32, 0x28, 12, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_33] = RTK_PIN_CONFIG_V2(gpio_33, 0x28, 18, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_34] = RTK_PIN_CONFIG_V2(gpio_34, 0x28, 24, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_35] = RTK_PIN_CONFIG_V2(gpio_35, 0x2c, 0, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_42] = RTK_PIN_CONFIG_V2(gpio_42, 0x2c, 6, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_43] = RTK_PIN_CONFIG_V2(gpio_43, 0x2c, 12, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_44] = RTK_PIN_CONFIG_V2(gpio_44, 0x2c, 18, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_51] = RTK_PIN_CONFIG_V2(gpio_51, 0x2c, 24, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_53] = RTK_PIN_CONFIG_V2(gpio_53, 0x30, 0, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_54] = RTK_PIN_CONFIG_V2(gpio_54, 0x30, 6, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_55] = RTK_PIN_CONFIG_V2(gpio_55, 0x30, 12, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_56] = RTK_PIN_CONFIG_V2(gpio_56, 0x30, 18, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_57] = RTK_PIN_CONFIG_V2(gpio_57, 0x30, 24, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_58] = RTK_PIN_CONFIG_V2(gpio_58, 0x34, 0, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_59] = RTK_PIN_CONFIG_V2(gpio_59, 0x34, 6, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_60] = RTK_PIN_CONFIG_V2(gpio_60, 0x34, 12, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_61] = RTK_PIN_CONFIG_V2(gpio_61, 0x34, 18, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_62] = RTK_PIN_CONFIG_V2(gpio_62, 0x34, 24, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_63] = RTK_PIN_CONFIG_V2(gpio_63, 0x38, 0, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_92] = RTK_PIN_CONFIG_V2(gpio_92, 0x38, 6, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_93] = RTK_PIN_CONFIG_V2(gpio_93, 0x38, 12, 1, 2, 0, 3, 4, 5, PADDRI_4_8),
	[RTD1625_VE4_GPIO_132] = RTK_PIN_CONFIG_V2(gpio_132, 0x38, 18, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_VE4_GPIO_133] = RTK_PIN_CONFIG_V2(gpio_133, 0x38, 24, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_VE4_GPIO_134] = RTK_PIN_CONFIG_V2(gpio_134, 0x3c, 0, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_VE4_GPIO_135] = RTK_PIN_CONFIG_V2(gpio_135, 0x3c, 6, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_VE4_GPIO_136] = RTK_PIN_CONFIG_V2(gpio_136, 0x3c, 12, 1, 2, 0, 3, 4, 5,
						   PADDRI_4_8),
	[RTD1625_VE4_GPIO_137] = RTK_PIN_CONFIG(gpio_137, 0x3c, 18, 1, 2, 0, NA, NA, PADDRI_4_8),
	[RTD1625_VE4_GPIO_138] = RTK_PIN_CONFIG(gpio_138, 0x3c, 21, 1, 2, 0, NA, NA, PADDRI_4_8),
	[RTD1625_VE4_GPIO_139] = RTK_PIN_CONFIG(gpio_139, 0x3c, 24, 1, 2, 0, NA, NA, PADDRI_4_8),
	[RTD1625_VE4_GPIO_140] = RTK_PIN_CONFIG(gpio_140, 0x3c, 27, 1, 2, 0, NA, NA, PADDRI_4_8),
	[RTD1625_VE4_GPIO_141] = RTK_PIN_CONFIG(gpio_141, 0x40, 0, 1, 2, 0, NA, NA, PADDRI_4_8),
	[RTD1625_VE4_GPIO_142] = RTK_PIN_CONFIG(gpio_142, 0x40, 3, 1, 2, 0, NA, NA, PADDRI_4_8),
	[RTD1625_VE4_GPIO_143] = RTK_PIN_CONFIG(gpio_143, 0x40, 6, 1, 2, 0, NA, NA, PADDRI_4_8),
	[RTD1625_VE4_GPIO_144] = RTK_PIN_CONFIG(gpio_144, 0x40, 9, 1, 2, 0, NA, NA, PADDRI_4_8),
	[RTD1625_VE4_GPIO_164] = RTK_PIN_CONFIG(gpio_164, 0x40, 12, 1, 2, 0, NA, NA, PADDRI_4_8),
	[RTD1625_VE4_GPIO_165] = RTK_PIN_CONFIG(gpio_165, 0x40, 15, 1, 2, 0, NA, NA, PADDRI_4_8),
};

static const struct rtd_pin_config_desc rtd1625_main2_configs[] = {
	[RTD1625_MAIN2_EMMC_CLK] = RTK_PIN_CONFIG(emmc_clk, 0x14, 0, 0, 1, NA, 2, 12, NA),
	[RTD1625_MAIN2_EMMC_CMD] = RTK_PIN_CONFIG(emmc_cmd, 0x14, 13, 0, 1, NA, 2, 13, NA),
	[RTD1625_MAIN2_EMMC_DATA_0] = RTK_PIN_CONFIG(emmc_data_0, 0x18, 0, 0, 1, NA, 2, 12, NA),
	[RTD1625_MAIN2_EMMC_DATA_1] = RTK_PIN_CONFIG(emmc_data_1, 0x18, 13, 0, 1, NA, 2, 12, NA),
	[RTD1625_MAIN2_EMMC_DATA_2] = RTK_PIN_CONFIG(emmc_data_2, 0x1c, 0, 0, 1, NA, 2, 12, NA),
	[RTD1625_MAIN2_EMMC_DATA_3] = RTK_PIN_CONFIG(emmc_data_3, 0x1c, 13, 0, 1, NA, 2, 12, NA),
	[RTD1625_MAIN2_EMMC_DATA_4] = RTK_PIN_CONFIG(emmc_data_4, 0x20, 0, 0, 1, NA, 2, 12, NA),
	[RTD1625_MAIN2_EMMC_DATA_5] = RTK_PIN_CONFIG(emmc_data_5, 0x20, 13, 0, 1, NA, 2, 12, NA),
	[RTD1625_MAIN2_EMMC_DATA_6] = RTK_PIN_CONFIG(emmc_data_6, 0x24, 0, 0, 1, NA, 2, 12, NA),
	[RTD1625_MAIN2_EMMC_DATA_7] = RTK_PIN_CONFIG(emmc_data_7, 0x24, 13, 0, 1, NA, 2, 12, NA),
	[RTD1625_MAIN2_EMMC_DD_SB] = RTK_PIN_CONFIG(emmc_dd_sb, 0x28, 0, 0, 1, NA, 2, 12, NA),
	[RTD1625_MAIN2_EMMC_RST_N] = RTK_PIN_CONFIG(emmc_rst_n, 0x28, 13, 0, 1, NA, 2, 12, NA),
	[RTD1625_MAIN2_GPIO_14] = RTK_PIN_CONFIG(gpio_14, 0x28, 26, 1, 2, 0, NA, NA, PADDRI_4_8),
	[RTD1625_MAIN2_GPIO_15] = RTK_PIN_CONFIG(gpio_15, 0x28, 29, 1, 2, 0, NA, NA, PADDRI_4_8),
	[RTD1625_MAIN2_GPIO_20] = RTK_PIN_CONFIG_I2C(gpio_20, 0x2c, 0, 1, 2, 0, 3, 4, 5, 7, 8,
						     PADDRI_4_8),
	[RTD1625_MAIN2_GPIO_21] = RTK_PIN_CONFIG_I2C(gpio_21, 0x2c, 9, 1, 2, 0, 3, 4, 5, 7, 8,
						     PADDRI_4_8),
	[RTD1625_MAIN2_GPIO_22] = RTK_PIN_CONFIG_V2(gpio_22, 0x2c, 18, 1, 2, 0, 3, 7, 8,
						     PADDRI_4_8),
	[RTD1625_MAIN2_GPIO_40] = RTK_PIN_CONFIG(gpio_40, 0x30, 0, 0, 1, NA, 2, 12, NA),
	[RTD1625_MAIN2_GPIO_41] = RTK_PIN_CONFIG(gpio_41, 0x30, 13, 0, 1, NA, 2, 12, NA),
	[RTD1625_MAIN2_GPIO_64] = RTK_PIN_CONFIG(gpio_64, 0x34, 0, 0, 1, NA, 2, 12, NA),
	[RTD1625_MAIN2_GPIO_65] = RTK_PIN_CONFIG(gpio_65, 0x34, 13, 0, 1, NA, 2, 12, NA),
	[RTD1625_MAIN2_GPIO_66] = RTK_PIN_CONFIG(gpio_66, 0x38, 0, 0, 1, NA, 2, 12, NA),
	[RTD1625_MAIN2_GPIO_67] = RTK_PIN_CONFIG(gpio_67, 0x38, 13, 0, 1, NA, 2, 12, NA),
	[RTD1625_MAIN2_GPIO_80] = RTK_PIN_CONFIG(gpio_80, 0x38, 26, 1, 2, 0, NA, NA, PADDRI_4_8),
	[RTD1625_MAIN2_GPIO_81] = RTK_PIN_CONFIG(gpio_81, 0x38, 29, 1, 2, 0, NA, NA, PADDRI_4_8),
	[RTD1625_MAIN2_GPIO_82] = RTK_PIN_CONFIG(gpio_82, 0x3c, 0, 1, 2, 0, NA, NA, PADDRI_4_8),
	[RTD1625_MAIN2_GPIO_83] = RTK_PIN_CONFIG(gpio_83, 0x3c, 3, 1, 2, 0, NA, NA, PADDRI_4_8),
	[RTD1625_MAIN2_GPIO_84] = RTK_PIN_CONFIG(gpio_84, 0x3c, 6, 1, 2, 0, NA, NA, PADDRI_4_8),
	[RTD1625_MAIN2_GPIO_85] = RTK_PIN_CONFIG(gpio_85, 0x3c, 9, 1, 2, NA, NA, NA, PADDRI_4_8),
	[RTD1625_MAIN2_GPIO_86] = RTK_PIN_CONFIG(gpio_86, 0x3c, 12, 1, 2, NA, NA, NA, PADDRI_4_8),
	[RTD1625_MAIN2_GPIO_87] = RTK_PIN_CONFIG(gpio_87, 0x3c, 22, 1, 2, NA, NA, NA, PADDRI_4_8),
	[RTD1625_MAIN2_GPIO_88] = RTK_PIN_CONFIG(gpio_88, 0x40, 0, 1, 2, NA, NA, NA, PADDRI_4_8),
	[RTD1625_MAIN2_GPIO_89] = RTK_PIN_CONFIG(gpio_89, 0x40, 10, 1, 2, NA, NA, NA, PADDRI_4_8),
	[RTD1625_MAIN2_GPIO_90] = RTK_PIN_CONFIG(gpio_90, 0x40, 20, 1, 2, NA, NA, NA, PADDRI_4_8),
	[RTD1625_MAIN2_GPIO_91] = RTK_PIN_CONFIG(gpio_91, 0x44, 0, 1, 2, NA, NA, NA, PADDRI_4_8),
	[RTD1625_MAIN2_HIF_CLK] = RTK_PIN_CONFIG(hif_clk, 0x44, 10, 0, 1, NA, 2, 12, NA),
	[RTD1625_MAIN2_HIF_DATA] = RTK_PIN_CONFIG(hif_data, 0x48, 0, 0, 1, NA, 2, 12, NA),
	[RTD1625_MAIN2_HIF_EN] = RTK_PIN_CONFIG(hif_en, 0x48, 13, 0, 1, NA, 2, 12, NA),
	[RTD1625_MAIN2_HIF_RDY] = RTK_PIN_CONFIG(hif_rdy, 0x4c, 0, 0, 1, NA, 2, 12, NA),
};

static const struct rtd_pin_sconfig_desc rtd1625_iso_sconfigs[] = {
	RTK_PIN_SCONFIG(gpio_45, 0x24, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(gpio_46, 0x24, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(gpio_47, 0x28, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(gpio_48, 0x28, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(gpio_49, 0x2c, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(gpio_50, 0x2c, 16, 3, 19, 3, 22, 3),
};

static const struct rtd_pin_sconfig_desc rtd1625_main2_sconfigs[] = {
	RTK_PIN_SCONFIG(emmc_clk, 0x14, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(emmc_cmd, 0x14, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(emmc_data_0, 0x18, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(emmc_data_1, 0x18, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(emmc_data_2, 0x1c, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(emmc_data_3, 0x1c, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(emmc_data_4, 0x20, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(emmc_data_5, 0x20, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(emmc_data_6, 0x24, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(emmc_data_7, 0x24, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(emmc_dd_sb, 0x28, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(emmc_rst_n, 0x28, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(gpio_40, 0x30, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(gpio_41, 0x30, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(gpio_64, 0x34, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(gpio_65, 0x34, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(gpio_66, 0x38, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(gpio_67, 0x38, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(gpio_86, 0x3c, 0, 0, 14, 4, 18, 4),
	RTK_PIN_SCONFIG(gpio_87, 0x3c, 0, 0, 24, 4, 28, 4),
	RTK_PIN_SCONFIG(gpio_88, 0x40, 0, 0, 2, 4, 6, 4),
	RTK_PIN_SCONFIG(gpio_89, 0x40, 0, 0, 12, 4, 16, 4),
	RTK_PIN_SCONFIG(gpio_90, 0x40, 0, 0, 22, 4, 26, 4),
	RTK_PIN_SCONFIG(gpio_91, 0x44, 0, 0, 2, 4, 6, 4),
	RTK_PIN_SCONFIG(hif_clk, 0x44, 13, 3, 16, 3, 19, 3),
	RTK_PIN_SCONFIG(hif_data, 0x48, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(hif_en, 0x48, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(hif_rdy, 0x4c, 3, 3, 6, 3, 9, 3),
};

static const struct rtd_reg_range rtd1625_iso_reg_ranges[] = {
	{ .offset = 0x0,  .len = 0x58 },
	{ .offset = 0x120, .len = 0x10 },
	{ .offset = 0x180, .len = 0xc },
	{ .offset = 0x1A0, .len = 0xc },
};

static const struct rtd_pin_range rtd1625_iso_pin_ranges = {
	.ranges = rtd1625_iso_reg_ranges,
	.num_ranges = ARRAY_SIZE(rtd1625_iso_reg_ranges),
};

static const struct rtd_reg_range rtd1625_isom_reg_ranges[] = {
	{ .offset = 0x0,  .len = 0xc },
	{ .offset = 0x30, .len = 0x4 },
};

static const struct rtd_pin_range rtd1625_isom_pin_ranges = {
	.ranges = rtd1625_isom_reg_ranges,
	.num_ranges = ARRAY_SIZE(rtd1625_isom_reg_ranges),
};

static const struct rtd_reg_range rtd1625_ve4_reg_ranges[] = {
	{ .offset = 0x0,  .len = 0x48 },
	{ .offset = 0x80, .len = 0x4 },
};

static const struct rtd_pin_range rtd1625_ve4_pin_ranges = {
	.ranges = rtd1625_ve4_reg_ranges,
	.num_ranges = ARRAY_SIZE(rtd1625_ve4_reg_ranges),
};

static const struct rtd_reg_range rtd1625_main2_reg_ranges[] = {
	{ .offset = 0x0,  .len = 0x50 },
};

static const struct rtd_pin_range rtd1625_main2_pin_ranges = {
	.ranges = rtd1625_main2_reg_ranges,
	.num_ranges = ARRAY_SIZE(rtd1625_main2_reg_ranges),
};

static const struct rtd_pinctrl_desc rtd1625_iso_pinctrl_desc = {
	.pins = rtd1625_iso_pins,
	.num_pins = ARRAY_SIZE(rtd1625_iso_pins),
	.groups = rtd1625_iso_pin_groups,
	.num_groups = ARRAY_SIZE(rtd1625_iso_pin_groups),
	.functions = rtd1625_iso_pin_functions,
	.num_functions = ARRAY_SIZE(rtd1625_iso_pin_functions),
	.muxes = rtd1625_iso_muxes,
	.num_muxes = ARRAY_SIZE(rtd1625_iso_muxes),
	.configs = rtd1625_iso_configs,
	.num_configs = ARRAY_SIZE(rtd1625_iso_configs),
	.sconfigs = rtd1625_iso_sconfigs,
	.num_sconfigs = ARRAY_SIZE(rtd1625_iso_sconfigs),
	.pin_range = &rtd1625_iso_pin_ranges,
};

static const struct rtd_pinctrl_desc rtd1625_isom_pinctrl_desc = {
	.pins = rtd1625_isom_pins,
	.num_pins = ARRAY_SIZE(rtd1625_isom_pins),
	.groups = rtd1625_isom_pin_groups,
	.num_groups = ARRAY_SIZE(rtd1625_isom_pin_groups),
	.functions = rtd1625_isom_pin_functions,
	.num_functions = ARRAY_SIZE(rtd1625_isom_pin_functions),
	.muxes = rtd1625_isom_muxes,
	.num_muxes = ARRAY_SIZE(rtd1625_isom_muxes),
	.configs = rtd1625_isom_configs,
	.num_configs = ARRAY_SIZE(rtd1625_isom_configs),
	.pin_range = &rtd1625_isom_pin_ranges,
};

static const struct rtd_pinctrl_desc rtd1625_ve4_pinctrl_desc = {
	.pins = rtd1625_ve4_pins,
	.num_pins = ARRAY_SIZE(rtd1625_ve4_pins),
	.groups = rtd1625_ve4_pin_groups,
	.num_groups = ARRAY_SIZE(rtd1625_ve4_pin_groups),
	.functions = rtd1625_ve4_pin_functions,
	.num_functions = ARRAY_SIZE(rtd1625_ve4_pin_functions),
	.muxes = rtd1625_ve4_muxes,
	.num_muxes = ARRAY_SIZE(rtd1625_ve4_muxes),
	.configs = rtd1625_ve4_configs,
	.num_configs = ARRAY_SIZE(rtd1625_ve4_configs),
	.pin_range = &rtd1625_ve4_pin_ranges,
};

static const struct rtd_pinctrl_desc rtd1625_main2_pinctrl_desc = {
	.pins = rtd1625_main2_pins,
	.num_pins = ARRAY_SIZE(rtd1625_main2_pins),
	.groups = rtd1625_main2_pin_groups,
	.num_groups = ARRAY_SIZE(rtd1625_main2_pin_groups),
	.functions = rtd1625_main2_pin_functions,
	.num_functions = ARRAY_SIZE(rtd1625_main2_pin_functions),
	.muxes = rtd1625_main2_muxes,
	.num_muxes = ARRAY_SIZE(rtd1625_main2_muxes),
	.configs = rtd1625_main2_configs,
	.num_configs = ARRAY_SIZE(rtd1625_main2_configs),
	.sconfigs = rtd1625_main2_sconfigs,
	.num_sconfigs = ARRAY_SIZE(rtd1625_main2_sconfigs),
	.pin_range = &rtd1625_main2_pin_ranges,
};

static int rtd1625_pinctrl_probe(struct platform_device *pdev)
{
	const struct rtd_pinctrl_desc *desc = device_get_match_data(&pdev->dev);

	return rtd_pinctrl_probe(pdev, desc);
}

static const struct of_device_id rtd1625_pinctrl_of_match[] = {
	{.compatible = "realtek,rtd1625-iso-pinctrl", .data = &rtd1625_iso_pinctrl_desc},
	{.compatible = "realtek,rtd1625-isom-pinctrl", .data = &rtd1625_isom_pinctrl_desc},
	{.compatible = "realtek,rtd1625-ve4-pinctrl", .data = &rtd1625_ve4_pinctrl_desc},
	{.compatible = "realtek,rtd1625-main2-pinctrl", .data = &rtd1625_main2_pinctrl_desc},
	{},
};
MODULE_DEVICE_TABLE(of, rtd1625_pinctrl_of_match);

static struct platform_driver rtd1625_pinctrl_driver = {
	.driver = {
		.name = "rtd1625-pinctrl",
		.of_match_table = rtd1625_pinctrl_of_match,
		.pm = &realtek_pinctrl_pm_ops,
	},
	.probe = rtd1625_pinctrl_probe,
};

module_platform_driver(rtd1625_pinctrl_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Realtek Semiconductor Corporation");
MODULE_DESCRIPTION("Realtek DHC SoC RTD1625 pinctrl driver");

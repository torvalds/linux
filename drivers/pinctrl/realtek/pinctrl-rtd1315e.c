// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Realtek DHC 1315E pin controller driver
 *
 * Copyright (c) 2023 Realtek Semiconductor Corp.
 *
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-rtd.h"

enum rtd13xxe_iso_pins {
	RTD1315E_ISO_GPIO_0 = 0,
	RTD1315E_ISO_GPIO_1,
	RTD1315E_ISO_EMMC_RST_N,
	RTD1315E_ISO_EMMC_DD_SB,
	RTD1315E_ISO_EMMC_CLK,
	RTD1315E_ISO_EMMC_CMD,
	RTD1315E_ISO_GPIO_6,
	RTD1315E_ISO_GPIO_7,
	RTD1315E_ISO_GPIO_8,
	RTD1315E_ISO_GPIO_9,
	RTD1315E_ISO_GPIO_10,
	RTD1315E_ISO_GPIO_11,
	RTD1315E_ISO_GPIO_12,
	RTD1315E_ISO_GPIO_13,
	RTD1315E_ISO_GPIO_14,
	RTD1315E_ISO_GPIO_15,
	RTD1315E_ISO_GPIO_16,
	RTD1315E_ISO_GPIO_17,
	RTD1315E_ISO_GPIO_18,
	RTD1315E_ISO_GPIO_19,
	RTD1315E_ISO_GPIO_20,
	RTD1315E_ISO_EMMC_DATA_0,
	RTD1315E_ISO_EMMC_DATA_1,
	RTD1315E_ISO_EMMC_DATA_2,
	RTD1315E_ISO_USB_CC2,
	RTD1315E_ISO_GPIO_25,
	RTD1315E_ISO_GPIO_26,
	RTD1315E_ISO_GPIO_27,
	RTD1315E_ISO_GPIO_28,
	RTD1315E_ISO_GPIO_29,
	RTD1315E_ISO_GPIO_30,
	RTD1315E_ISO_GPIO_31,
	RTD1315E_ISO_GPIO_32,
	RTD1315E_ISO_GPIO_33,
	RTD1315E_ISO_GPIO_34,
	RTD1315E_ISO_GPIO_35,
	RTD1315E_ISO_HIF_DATA,
	RTD1315E_ISO_HIF_EN,
	RTD1315E_ISO_HIF_RDY,
	RTD1315E_ISO_HIF_CLK,
	RTD1315E_ISO_GPIO_DUMMY_40,
	RTD1315E_ISO_GPIO_DUMMY_41,
	RTD1315E_ISO_GPIO_DUMMY_42,
	RTD1315E_ISO_GPIO_DUMMY_43,
	RTD1315E_ISO_GPIO_DUMMY_44,
	RTD1315E_ISO_GPIO_DUMMY_45,
	RTD1315E_ISO_GPIO_46,
	RTD1315E_ISO_GPIO_47,
	RTD1315E_ISO_GPIO_48,
	RTD1315E_ISO_GPIO_49,
	RTD1315E_ISO_GPIO_50,
	RTD1315E_ISO_USB_CC1,
	RTD1315E_ISO_EMMC_DATA_3,
	RTD1315E_ISO_EMMC_DATA_4,
	RTD1315E_ISO_IR_RX,
	RTD1315E_ISO_UR0_RX,
	RTD1315E_ISO_UR0_TX,
	RTD1315E_ISO_GPIO_57,
	RTD1315E_ISO_GPIO_58,
	RTD1315E_ISO_GPIO_59,
	RTD1315E_ISO_GPIO_60,
	RTD1315E_ISO_GPIO_61,
	RTD1315E_ISO_GPIO_62,
	RTD1315E_ISO_GPIO_DUMMY_63,
	RTD1315E_ISO_GPIO_DUMMY_64,
	RTD1315E_ISO_GPIO_DUMMY_65,
	RTD1315E_ISO_GPIO_66,
	RTD1315E_ISO_GPIO_67,
	RTD1315E_ISO_GPIO_68,
	RTD1315E_ISO_GPIO_69,
	RTD1315E_ISO_GPIO_70,
	RTD1315E_ISO_GPIO_71,
	RTD1315E_ISO_GPIO_72,
	RTD1315E_ISO_GPIO_DUMMY_73,
	RTD1315E_ISO_EMMC_DATA_5,
	RTD1315E_ISO_EMMC_DATA_6,
	RTD1315E_ISO_EMMC_DATA_7,
	RTD1315E_ISO_GPIO_DUMMY_77,
	RTD1315E_ISO_GPIO_78,
	RTD1315E_ISO_GPIO_79,
	RTD1315E_ISO_GPIO_80,
	RTD1315E_ISO_GPIO_81,
	RTD1315E_ISO_UR2_LOC,
	RTD1315E_ISO_GSPI_LOC,
	RTD1315E_ISO_HI_WIDTH,
	RTD1315E_ISO_SF_EN,
	RTD1315E_ISO_ARM_TRACE_DBG_EN,
	RTD1315E_ISO_EJTAG_AUCPU_LOC,
	RTD1315E_ISO_EJTAG_ACPU_LOC,
	RTD1315E_ISO_EJTAG_VCPU_LOC,
	RTD1315E_ISO_EJTAG_SCPU_LOC,
	RTD1315E_ISO_DMIC_LOC,
	RTD1315E_ISO_VTC_DMIC_LOC,
	RTD1315E_ISO_VTC_TDM_LOC,
	RTD1315E_ISO_VTC_I2SI_LOC,
	RTD1315E_ISO_TDM_AI_LOC,
	RTD1315E_ISO_AI_LOC,
	RTD1315E_ISO_SPDIF_LOC,
	RTD1315E_ISO_HIF_EN_LOC,
	RTD1315E_ISO_SCAN_SWITCH,
	RTD1315E_ISO_WD_RSET,
	RTD1315E_ISO_BOOT_SEL,
	RTD1315E_ISO_RESET_N,
	RTD1315E_ISO_TESTMODE,
};

static const struct pinctrl_pin_desc rtd1315e_iso_pins[] = {
	PINCTRL_PIN(RTD1315E_ISO_GPIO_0, "gpio_0"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_1, "gpio_1"),
	PINCTRL_PIN(RTD1315E_ISO_EMMC_RST_N, "emmc_rst_n"),
	PINCTRL_PIN(RTD1315E_ISO_EMMC_DD_SB, "emmc_dd_sb"),
	PINCTRL_PIN(RTD1315E_ISO_EMMC_CLK, "emmc_clk"),
	PINCTRL_PIN(RTD1315E_ISO_EMMC_CMD, "emmc_cmd"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_6, "gpio_6"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_7, "gpio_7"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_8, "gpio_8"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_9, "gpio_9"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_10, "gpio_10"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_11, "gpio_11"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_12, "gpio_12"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_13, "gpio_13"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_14, "gpio_14"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_15, "gpio_15"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_16, "gpio_16"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_17, "gpio_17"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_18, "gpio_18"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_19, "gpio_19"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_20, "gpio_20"),
	PINCTRL_PIN(RTD1315E_ISO_EMMC_DATA_0, "emmc_data_0"),
	PINCTRL_PIN(RTD1315E_ISO_EMMC_DATA_1, "emmc_data_1"),
	PINCTRL_PIN(RTD1315E_ISO_EMMC_DATA_2, "emmc_data_2"),
	PINCTRL_PIN(RTD1315E_ISO_USB_CC2, "usb_cc2"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_25, "gpio_25"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_26, "gpio_26"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_27, "gpio_27"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_28, "gpio_28"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_29, "gpio_29"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_30, "gpio_30"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_31, "gpio_31"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_32, "gpio_32"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_33, "gpio_33"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_34, "gpio_34"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_35, "gpio_35"),
	PINCTRL_PIN(RTD1315E_ISO_HIF_DATA, "hif_data"),
	PINCTRL_PIN(RTD1315E_ISO_HIF_EN, "hif_en"),
	PINCTRL_PIN(RTD1315E_ISO_HIF_RDY, "hif_rdy"),
	PINCTRL_PIN(RTD1315E_ISO_HIF_CLK, "hif_clk"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_DUMMY_40, "gpio_dummy_40"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_DUMMY_41, "gpio_dummy_41"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_DUMMY_42, "gpio_dummy_42"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_DUMMY_43, "gpio_dummy_43"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_DUMMY_44, "gpio_dummy_44"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_DUMMY_45, "gpio_dummy_45"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_46, "gpio_46"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_47, "gpio_47"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_48, "gpio_48"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_49, "gpio_49"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_50, "gpio_50"),
	PINCTRL_PIN(RTD1315E_ISO_USB_CC1, "usb_cc1"),
	PINCTRL_PIN(RTD1315E_ISO_EMMC_DATA_3, "emmc_data_3"),
	PINCTRL_PIN(RTD1315E_ISO_EMMC_DATA_4, "emmc_data_4"),
	PINCTRL_PIN(RTD1315E_ISO_IR_RX, "ir_rx"),
	PINCTRL_PIN(RTD1315E_ISO_UR0_RX, "ur0_rx"),
	PINCTRL_PIN(RTD1315E_ISO_UR0_TX, "ur0_tx"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_57, "gpio_57"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_58, "gpio_58"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_59, "gpio_59"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_60, "gpio_60"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_61, "gpio_61"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_62, "gpio_62"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_DUMMY_63, "gpio_dummy_63"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_DUMMY_64, "gpio_dummy_64"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_DUMMY_65, "gpio_dummy_65"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_66, "gpio_66"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_67, "gpio_67"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_68, "gpio_68"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_69, "gpio_69"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_70, "gpio_70"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_71, "gpio_71"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_72, "gpio_72"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_DUMMY_73, "gpio_dummy_73"),
	PINCTRL_PIN(RTD1315E_ISO_EMMC_DATA_5, "emmc_data_5"),
	PINCTRL_PIN(RTD1315E_ISO_EMMC_DATA_6, "emmc_data_6"),
	PINCTRL_PIN(RTD1315E_ISO_EMMC_DATA_7, "emmc_data_7"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_DUMMY_77, "gpio_dummy_77"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_78, "gpio_78"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_79, "gpio_79"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_80, "gpio_80"),
	PINCTRL_PIN(RTD1315E_ISO_GPIO_81, "gpio_81"),
	PINCTRL_PIN(RTD1315E_ISO_UR2_LOC, "ur2_loc"),
	PINCTRL_PIN(RTD1315E_ISO_GSPI_LOC, "gspi_loc"),
	PINCTRL_PIN(RTD1315E_ISO_HI_WIDTH, "hi_width"),
	PINCTRL_PIN(RTD1315E_ISO_SF_EN, "sf_en"),
	PINCTRL_PIN(RTD1315E_ISO_ARM_TRACE_DBG_EN, "arm_trace_dbg_en"),
	PINCTRL_PIN(RTD1315E_ISO_EJTAG_AUCPU_LOC, "ejtag_aucpu_loc"),
	PINCTRL_PIN(RTD1315E_ISO_EJTAG_ACPU_LOC, "ejtag_acpu_loc"),
	PINCTRL_PIN(RTD1315E_ISO_EJTAG_VCPU_LOC, "ejtag_vcpu_loc"),
	PINCTRL_PIN(RTD1315E_ISO_EJTAG_SCPU_LOC, "ejtag_scpu_loc"),
	PINCTRL_PIN(RTD1315E_ISO_DMIC_LOC, "dmic_loc"),
	PINCTRL_PIN(RTD1315E_ISO_VTC_DMIC_LOC, "vtc_dmic_loc"),
	PINCTRL_PIN(RTD1315E_ISO_VTC_TDM_LOC, "vtc_tdm_loc"),
	PINCTRL_PIN(RTD1315E_ISO_VTC_I2SI_LOC, "vtc_i2si_loc"),
	PINCTRL_PIN(RTD1315E_ISO_TDM_AI_LOC, "tdm_ai_loc"),
	PINCTRL_PIN(RTD1315E_ISO_AI_LOC, "ai_loc"),
	PINCTRL_PIN(RTD1315E_ISO_SPDIF_LOC, "spdif_loc"),
	PINCTRL_PIN(RTD1315E_ISO_HIF_EN_LOC, "hif_en_loc"),
	PINCTRL_PIN(RTD1315E_ISO_SCAN_SWITCH, "scan_switch"),
	PINCTRL_PIN(RTD1315E_ISO_WD_RSET, "wd_rset"),
	PINCTRL_PIN(RTD1315E_ISO_BOOT_SEL, "boot_sel"),
	PINCTRL_PIN(RTD1315E_ISO_RESET_N, "reset_n"),
	PINCTRL_PIN(RTD1315E_ISO_TESTMODE, "testmode"),
};

/* Tagged as __maybe_unused since there are pins we may use in the future */
#define DECLARE_RTD1315E_PIN(_pin, _name) \
	static const unsigned int rtd1315e_## _name ##_pins[] __maybe_unused = { _pin }

DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_0, gpio_0);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_1, gpio_1);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_EMMC_RST_N, emmc_rst_n);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_EMMC_DD_SB, emmc_dd_sb);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_EMMC_CLK, emmc_clk);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_EMMC_CMD, emmc_cmd);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_6, gpio_6);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_7, gpio_7);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_8, gpio_8);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_9, gpio_9);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_10, gpio_10);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_11, gpio_11);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_12, gpio_12);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_13, gpio_13);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_14, gpio_14);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_15, gpio_15);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_16, gpio_16);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_17, gpio_17);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_18, gpio_18);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_19, gpio_19);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_20, gpio_20);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_EMMC_DATA_0, emmc_data_0);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_EMMC_DATA_1, emmc_data_1);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_EMMC_DATA_2, emmc_data_2);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_USB_CC2, usb_cc2);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_25, gpio_25);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_26, gpio_26);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_27, gpio_27);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_28, gpio_28);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_29, gpio_29);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_30, gpio_30);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_31, gpio_31);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_32, gpio_32);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_33, gpio_33);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_34, gpio_34);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_35, gpio_35);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_HIF_DATA, hif_data);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_HIF_EN, hif_en);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_HIF_RDY, hif_rdy);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_HIF_CLK, hif_clk);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_DUMMY_40, gpio_dummy_40);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_DUMMY_41, gpio_dummy_41);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_DUMMY_42, gpio_dummy_42);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_DUMMY_43, gpio_dummy_43);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_DUMMY_44, gpio_dummy_44);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_DUMMY_45, gpio_dummy_45);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_46, gpio_46);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_47, gpio_47);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_48, gpio_48);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_49, gpio_49);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_50, gpio_50);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_USB_CC1, usb_cc1);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_EMMC_DATA_3, emmc_data_3);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_EMMC_DATA_4, emmc_data_4);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_IR_RX, ir_rx);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_UR0_RX, ur0_rx);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_UR0_TX, ur0_tx);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_57, gpio_57);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_58, gpio_58);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_59, gpio_59);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_60, gpio_60);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_61, gpio_61);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_62, gpio_62);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_DUMMY_63, gpio_dummy_63);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_DUMMY_64, gpio_dummy_64);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_DUMMY_65, gpio_dummy_65);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_66, gpio_66);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_67, gpio_67);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_68, gpio_68);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_69, gpio_69);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_70, gpio_70);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_71, gpio_71);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_72, gpio_72);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_DUMMY_73, gpio_dummy_73);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_EMMC_DATA_5, emmc_data_5);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_EMMC_DATA_6, emmc_data_6);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_EMMC_DATA_7, emmc_data_7);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_DUMMY_77, gpio_dummy_77);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_78, gpio_78);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_79, gpio_79);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_80, gpio_80);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GPIO_81, gpio_81);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_UR2_LOC, ur2_loc);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_GSPI_LOC, gspi_loc);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_HI_WIDTH, hi_width);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_SF_EN, sf_en);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_ARM_TRACE_DBG_EN, arm_trace_dbg_en);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_EJTAG_AUCPU_LOC, ejtag_aucpu_loc);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_EJTAG_ACPU_LOC, ejtag_acpu_loc);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_EJTAG_VCPU_LOC, ejtag_vcpu_loc);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_EJTAG_SCPU_LOC, ejtag_scpu_loc);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_DMIC_LOC, dmic_loc);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_VTC_DMIC_LOC, vtc_dmic_loc);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_VTC_TDM_LOC, vtc_tdm_loc);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_VTC_I2SI_LOC, vtc_i2si_loc);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_TDM_AI_LOC, tdm_ai_loc);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_AI_LOC, ai_loc);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_SPDIF_LOC, spdif_loc);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_HIF_EN_LOC, hif_en_loc);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_SCAN_SWITCH, scan_switch);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_WD_RSET, wd_rset);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_BOOT_SEL, boot_sel);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_RESET_N, reset_n);
DECLARE_RTD1315E_PIN(RTD1315E_ISO_TESTMODE, testmode);

#define RTD1315E_GROUP(_name) \
	{ \
		.name = # _name, \
		.pins = rtd1315e_ ## _name ## _pins, \
		.num_pins = ARRAY_SIZE(rtd1315e_ ## _name ## _pins), \
	}

static const struct rtd_pin_group_desc rtd1315e_pin_groups[] = {
	RTD1315E_GROUP(gpio_0),
	RTD1315E_GROUP(gpio_1),
	RTD1315E_GROUP(emmc_rst_n),
	RTD1315E_GROUP(emmc_dd_sb),
	RTD1315E_GROUP(emmc_clk),
	RTD1315E_GROUP(emmc_cmd),
	RTD1315E_GROUP(gpio_6),
	RTD1315E_GROUP(gpio_7),
	RTD1315E_GROUP(gpio_8),
	RTD1315E_GROUP(gpio_9),
	RTD1315E_GROUP(gpio_10),
	RTD1315E_GROUP(gpio_11),
	RTD1315E_GROUP(gpio_12),
	RTD1315E_GROUP(gpio_13),
	RTD1315E_GROUP(gpio_14),
	RTD1315E_GROUP(gpio_15),
	RTD1315E_GROUP(gpio_16),
	RTD1315E_GROUP(gpio_17),
	RTD1315E_GROUP(gpio_18),
	RTD1315E_GROUP(gpio_19),
	RTD1315E_GROUP(gpio_20),
	RTD1315E_GROUP(emmc_data_0),
	RTD1315E_GROUP(emmc_data_1),
	RTD1315E_GROUP(emmc_data_2),
	RTD1315E_GROUP(usb_cc2),
	RTD1315E_GROUP(gpio_25),
	RTD1315E_GROUP(gpio_26),
	RTD1315E_GROUP(gpio_27),
	RTD1315E_GROUP(gpio_28),
	RTD1315E_GROUP(gpio_29),
	RTD1315E_GROUP(gpio_30),
	RTD1315E_GROUP(gpio_31),
	RTD1315E_GROUP(gpio_32),
	RTD1315E_GROUP(gpio_33),
	RTD1315E_GROUP(gpio_34),
	RTD1315E_GROUP(gpio_35),
	RTD1315E_GROUP(hif_data),
	RTD1315E_GROUP(hif_en),
	RTD1315E_GROUP(hif_rdy),
	RTD1315E_GROUP(hif_clk),
	RTD1315E_GROUP(gpio_dummy_40),
	RTD1315E_GROUP(gpio_dummy_41),
	RTD1315E_GROUP(gpio_dummy_42),
	RTD1315E_GROUP(gpio_dummy_43),
	RTD1315E_GROUP(gpio_dummy_44),
	RTD1315E_GROUP(gpio_dummy_45),
	RTD1315E_GROUP(gpio_46),
	RTD1315E_GROUP(gpio_47),
	RTD1315E_GROUP(gpio_48),
	RTD1315E_GROUP(gpio_49),
	RTD1315E_GROUP(gpio_50),
	RTD1315E_GROUP(usb_cc1),
	RTD1315E_GROUP(emmc_data_3),
	RTD1315E_GROUP(emmc_data_4),
	RTD1315E_GROUP(ir_rx),
	RTD1315E_GROUP(ur0_rx),
	RTD1315E_GROUP(ur0_tx),
	RTD1315E_GROUP(gpio_57),
	RTD1315E_GROUP(gpio_58),
	RTD1315E_GROUP(gpio_59),
	RTD1315E_GROUP(gpio_60),
	RTD1315E_GROUP(gpio_61),
	RTD1315E_GROUP(gpio_62),
	RTD1315E_GROUP(gpio_dummy_63),
	RTD1315E_GROUP(gpio_dummy_64),
	RTD1315E_GROUP(gpio_dummy_65),
	RTD1315E_GROUP(gpio_66),
	RTD1315E_GROUP(gpio_67),
	RTD1315E_GROUP(gpio_68),
	RTD1315E_GROUP(gpio_69),
	RTD1315E_GROUP(gpio_70),
	RTD1315E_GROUP(gpio_71),
	RTD1315E_GROUP(gpio_72),
	RTD1315E_GROUP(gpio_dummy_73),
	RTD1315E_GROUP(emmc_data_5),
	RTD1315E_GROUP(emmc_data_6),
	RTD1315E_GROUP(emmc_data_7),
	RTD1315E_GROUP(gpio_dummy_77),
	RTD1315E_GROUP(gpio_78),
	RTD1315E_GROUP(gpio_79),
	RTD1315E_GROUP(gpio_80),
	RTD1315E_GROUP(gpio_81),
	RTD1315E_GROUP(ur2_loc),
	RTD1315E_GROUP(gspi_loc),
	RTD1315E_GROUP(hi_width),
	RTD1315E_GROUP(sf_en),
	RTD1315E_GROUP(arm_trace_dbg_en),
	RTD1315E_GROUP(ejtag_aucpu_loc),
	RTD1315E_GROUP(ejtag_acpu_loc),
	RTD1315E_GROUP(ejtag_vcpu_loc),
	RTD1315E_GROUP(ejtag_scpu_loc),
	RTD1315E_GROUP(dmic_loc),
	RTD1315E_GROUP(vtc_dmic_loc),
	RTD1315E_GROUP(vtc_tdm_loc),
	RTD1315E_GROUP(vtc_i2si_loc),
	RTD1315E_GROUP(tdm_ai_loc),
	RTD1315E_GROUP(ai_loc),
	RTD1315E_GROUP(spdif_loc),
	RTD1315E_GROUP(hif_en_loc),

};

static const char * const rtd1315e_gpio_groups[] = {
	"gpio_0", "gpio_1", "emmc_rst_n", "emmc_dd_sb", "emmc_clk",
	"emmc_cmd", "gpio_6", "gpio_7", "gpio_8", "gpio_9",
	"gpio_10", "gpio_11", "gpio_12", "gpio_13", "gpio_14",
	"gpio_15", "gpio_16", "gpio_17", "gpio_18", "gpio_19",
	"gpio_20", "emmc_data_0", "emmc_data_1", "emmc_data_2", "usb_cc2",
	"gpio_25", "gpio_26", "gpio_27", "gpio_28", "gpio_29",
	"gpio_30", "gpio_31", "gpio_32", "gpio_33", "gpio_34",
	"gpio_35", "hif_data", "hif_en", "hif_rdy", "hif_clk",
	"gpio_46", "gpio_47", "gpio_48", "gpio_49",
	"gpio_50", "usb_cc1", "emmc_data_3", "emmc_data_4", "ir_rx",
	"ur0_rx", "ur0_tx", "gpio_57", "gpio_58", "gpio_59",
	"gpio_60", "gpio_61", "gpio_62", "gpio_66", "gpio_67",
	"gpio_68", "gpio_69", "gpio_70", "gpio_71", "gpio_72",
	"emmc_data_5", "emmc_data_6", "emmc_data_7",
	"gpio_78", "gpio_79", "gpio_80", "gpio_81" };
static const char * const rtd1315e_nf_groups[] = {
	"emmc_rst_n", "emmc_clk", "emmc_cmd", "emmc_data_0",
	"emmc_data_1", "emmc_data_2", "emmc_data_3", "emmc_data_4",
	"emmc_data_5", "emmc_data_6", "emmc_data_7",
	"gpio_78", "gpio_79", "gpio_80", "gpio_81" };
static const char * const rtd1315e_emmc_groups[] = {
	"emmc_rst_n", "emmc_dd_sb", "emmc_clk", "emmc_cmd",
	"emmc_data_0", "emmc_data_1", "emmc_data_2", "emmc_data_3",
	"emmc_data_4", "emmc_data_5", "emmc_data_6", "emmc_data_7" };

static const char * const rtd1315e_ao_groups[] = {
	"gpio_66", "gpio_67", "gpio_68", "gpio_69", "gpio_70",
	"gpio_71", "gpio_72" };
static const char * const rtd1315e_gspi_loc0_groups[] = {
	"gpio_18", "gpio_19", "gpio_20", "gpio_31", "gspi_loc" };
static const char * const rtd1315e_gspi_loc1_groups[] = {
	"gpio_8", "gpio_9", "gpio_10", "gpio_11", "gspi_loc" };
static const char * const rtd1315e_uart0_groups[] = { "ur0_rx", "ur0_tx"};
static const char * const rtd1315e_uart1_groups[] = {
	"gpio_8", "gpio_9", "gpio_10", "gpio_11" };
static const char * const rtd1315e_uart2_loc0_groups[] = {
	"gpio_18", "gpio_19", "gpio_20", "gpio_31", "ur2_loc" };
static const char * const rtd1315e_uart2_loc1_groups[] = {
	"gpio_25", "gpio_26", "gpio_27", "gpio_28", "ur2_loc" };
static const char * const rtd1315e_i2c0_groups[] = { "gpio_12", "gpio_13" };
static const char * const rtd1315e_i2c1_groups[] = { "gpio_16", "gpio_17" };
static const char * const rtd1315e_i2c4_groups[] = { "gpio_34", "gpio_35" };
static const char * const rtd1315e_i2c5_groups[] = { "gpio_29", "gpio_46" };
static const char * const rtd1315e_pcie1_groups[] = { "gpio_25" };
static const char * const rtd1315e_etn_led_groups[] = { "gpio_14", "gpio_15" };
static const char * const rtd1315e_etn_phy_groups[] = { "gpio_14", "gpio_15" };
static const char * const rtd1315e_spi_groups[] = {
	"gpio_78", "gpio_79", "gpio_80", "gpio_81" };
static const char * const rtd1315e_pwm0_loc0_groups[] = { "gpio_26" };
static const char * const rtd1315e_pwm0_loc1_groups[] = { "gpio_20" };
static const char * const rtd1315e_pwm1_loc0_groups[] = { "gpio_27" };
static const char * const rtd1315e_pwm1_loc1_groups[] = { "gpio_29" };

static const char * const rtd1315e_pwm2_loc0_groups[] = { "gpio_28" };
static const char * const rtd1315e_pwm2_loc1_groups[] = { "gpio_30" };
static const char * const rtd1315e_pwm3_loc0_groups[] = { "gpio_47" };
static const char * const rtd1315e_pwm3_loc1_groups[] = { "gpio_31" };
static const char * const rtd1315e_spdif_optical_loc0_groups[] = { "gpio_20", "spdif_loc" };
static const char * const rtd1315e_spdif_optical_loc1_groups[] = { "gpio_6", "spdif_loc" };
static const char * const rtd1315e_usb_cc1_groups[] = { "usb_cc1" };
static const char * const rtd1315e_usb_cc2_groups[] = { "usb_cc2" };

static const char * const rtd1315e_sd_groups[] = {
	"gpio_32", "gpio_33", "gpio_34", "gpio_35",
	"hif_data", "hif_en", "hif_rdy", "hif_clk" };
static const char * const rtd1315e_dmic_loc0_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60", "gpio_61",
	"gpio_62", "gpio_1", "gpio_6", "dmic_loc" };
static const char * const rtd1315e_dmic_loc1_groups[] = {
	"gpio_32", "gpio_33", "gpio_34", "gpio_35",
	"hif_data", "hif_en", "hif_rdy", "hif_clk",
	"dmic_loc" };
static const char * const rtd1315e_ai_loc0_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60", "gpio_61",
	"gpio_62", "gpio_1", "ai_loc" };
static const char * const rtd1315e_ai_loc1_groups[] = {
	"gpio_32", "gpio_33", "gpio_34", "hif_data",
	"hif_en", "hif_rdy", "hif_clk", "ai_loc" };
static const char * const rtd1315e_tdm_ai_loc0_groups[] = {
	"gpio_57", "gpio_58", "gpio_59",
	"gpio_60", "tdm_ai_loc" };
static const char * const rtd1315e_tdm_ai_loc1_groups[] = {
	"hif_data", "hif_en", "hif_rdy", "hif_clk", "tdm_ai_loc" };
static const char * const rtd1315e_hi_loc0_groups[] = {
	"hif_data", "hif_en", "hif_rdy", "hif_clk" };
static const char * const rtd1315e_hi_m_groups[] = {
	"hif_data", "hif_en", "hif_rdy", "hif_clk" };
static const char * const rtd1315e_vtc_i2so_groups[] = {
	"gpio_67", "gpio_68", "gpio_69", "gpio_70"};
static const char * const rtd1315e_vtc_i2si_loc0_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60", "gpio_61",
	"vtc_i2si_loc" };
static const char * const rtd1315e_vtc_i2si_loc1_groups[] = {
	"gpio_32", "hif_data", "hif_en", "hif_rdy", "hif_clk",
	"vtc_i2si_loc" };
static const char * const rtd1315e_vtc_dmic_loc0_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60",
	"vtc_dmic_loc" };
static const char * const rtd1315e_vtc_dmic_loc1_groups[] = {
	"hif_data", "hif_en", "hif_rdy", "hif_clk",
	"vtc_dmic_loc" };
static const char * const rtd1315e_vtc_tdm_loc0_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60",
	"vtc_tdm_loc" };
static const char * const rtd1315e_vtc_tdm_loc1_groups[] = {
	"hif_data", "hif_en", "hif_rdy", "hif_clk",
	"vtc_tdm_loc" };
static const char * const rtd1315e_dc_fan_groups[] = { "gpio_47" };
static const char * const rtd1315e_pll_test_loc0_groups[] = { "gpio_0", "gpio_1" };
static const char * const rtd1315e_pll_test_loc1_groups[] = { "gpio_48", "gpio_49" };
static const char * const rtd1315e_spdif_groups[] = { "gpio_50" };
static const char * const rtd1315e_ir_rx_groups[] = { "ir_rx" };
static const char * const rtd1315e_uart2_disable_groups[] = { "ur2_loc" };
static const char * const rtd1315e_gspi_disable_groups[] = { "gspi_loc" };
static const char * const rtd1315e_hi_width_disable_groups[] = { "hi_width" };
static const char * const rtd1315e_hi_width_1bit_groups[] = { "hi_width" };
static const char * const rtd1315e_sf_disable_groups[] = { "sf_en" };
static const char * const rtd1315e_sf_enable_groups[] = { "sf_en" };
static const char * const rtd1315e_scpu_ejtag_loc0_groups[] = {
	"gpio_68", "gpio_69", "gpio_70", "gpio_71", "gpio_72",
	"ejtag_scpu_loc" };
static const char * const rtd1315e_scpu_ejtag_loc1_groups[] = {
	"gpio_32", "gpio_33", "hif_data", "hif_en", "hif_clk",
	"ejtag_scpu_loc" };
static const char * const rtd1315e_scpu_ejtag_loc2_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60", "gpio_61",
	"ejtag_scpu_loc" };
static const char * const rtd1315e_scpu_ejtag_loc3_groups[] = {
	"hif_data" };
static const char * const rtd1315e_acpu_ejtag_loc0_groups[] = {
	"gpio_68", "gpio_69", "gpio_70", "gpio_71", "gpio_72",
	"ejtag_acpu_loc" };
static const char * const rtd1315e_acpu_ejtag_loc1_groups[] = {
	"gpio_32", "gpio_33", "hif_data", "hif_en", "hif_clk",
	"ejtag_acpu_loc" };
static const char * const rtd1315e_acpu_ejtag_loc2_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60", "gpio_61",
	"ejtag_acpu_loc" };
static const char * const rtd1315e_vcpu_ejtag_loc0_groups[] = {
	"gpio_68", "gpio_69", "gpio_70", "gpio_71", "gpio_72",
	"ejtag_vcpu_loc" };
static const char * const rtd1315e_vcpu_ejtag_loc1_groups[] = {
	"gpio_32", "gpio_33", "hif_data", "hif_en", "hif_clk",
	"ejtag_vcpu_loc" };
static const char * const rtd1315e_vcpu_ejtag_loc2_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60", "gpio_61",
	"ejtag_vcpu_loc" };
static const char * const rtd1315e_aucpu_ejtag_loc0_groups[] = {
	"gpio_68", "gpio_69", "gpio_70", "gpio_71", "gpio_72",
	"ejtag_aucpu_loc" };
static const char * const rtd1315e_aucpu_ejtag_loc1_groups[] = {
	"gpio_32", "gpio_33", "hif_data", "hif_en", "hif_clk",
	"ejtag_aucpu_loc" };
static const char * const rtd1315e_aucpu_ejtag_loc2_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60", "gpio_61",
	"ejtag_aucpu_loc" };
static const char * const rtd1315e_gpu_ejtag_groups[] = {
	"gpio_68", "gpio_69", "gpio_70", "gpio_71", "gpio_72" };

static const char * const rtd1315e_iso_tristate_groups[] = {
	"emmc_rst_n", "emmc_dd_sb", "emmc_clk", "emmc_cmd",
	"emmc_data_0", "emmc_data_1", "emmc_data_2", "emmc_data_3",
	"emmc_data_4", "emmc_data_5", "emmc_data_6", "emmc_data_7",
	"gpio_1", "gpio_7", "gpio_8", "gpio_9", "gpio_10",
	"gpio_11", "usb_cc2", "gpio_32", "gpio_33", "hif_data",
	"hif_en", "hif_rdy", "hif_clk", "ir_rx", "ur0_rx",
	"ur0_tx", "gpio_66", "gpio_67", "gpio_68", "gpio_69", "gpio_70",
	"gpio_71", "gpio_72", "gpio_78", "gpio_79", "gpio_80", "gpio_81" };
static const char * const rtd1315e_dbg_out0_groups[] = {
	"gpio_0", "gpio_12", "gpio_13", "gpio_16", "gpio_17", "gpio_26",
	"gpio_27", "gpio_28", "gpio_29", "gpio_30", "gpio_34", "gpio_35",
	"gpio_46", "gpio_48", "gpio_49", "usb_cc1", "gpio_57", "gpio_58", "gpio_59", "gpio_60" };
static const char * const rtd1315e_dbg_out1_groups[] = {
	"gpio_6", "gpio_14", "gpio_15", "gpio_18", "gpio_19", "gpio_20",
	"gpio_25", "gpio_31", "gpio_47", "gpio_50", "gpio_59", "gpio_61",
	"gpio_62" };
static const char * const rtd1315e_standby_dbg_groups[] = {
	"gpio_1", "gpio_6", "ir_rx" };
static const char * const rtd1315e_arm_trace_debug_disable_groups[] = { "arm_trace_dbg_en" };
static const char * const rtd1315e_arm_trace_debug_enable_groups[] = { "arm_trace_dbg_en" };
static const char * const rtd1315e_aucpu_ejtag_disable_groups[] = { "ejtag_aucpu_loc" };
static const char * const rtd1315e_acpu_ejtag_disable_groups[] = { "ejtag_acpu_loc" };
static const char * const rtd1315e_vcpu_ejtag_disable_groups[] = { "ejtag_vcpu_loc" };
static const char * const rtd1315e_scpu_ejtag_disable_groups[] = { "ejtag_scpu_loc" };
static const char * const rtd1315e_vtc_dmic_loc_disable_groups[] = { "vtc_dmic_loc" };
static const char * const rtd1315e_vtc_tdm_disable_groups[] = { "vtc_tdm_loc" };
static const char * const rtd1315e_vtc_i2si_disable_groups[] = { "vtc_i2si_loc" };
static const char * const rtd1315e_tdm_ai_disable_groups[] = { "tdm_ai_loc" };
static const char * const rtd1315e_ai_disable_groups[] = { "ai_loc" };
static const char * const rtd1315e_spdif_disable_groups[] = { "spdif_loc" };
static const char * const rtd1315e_hif_disable_groups[] = { "hif_en_loc" };
static const char * const rtd1315e_hif_enable_groups[] = { "hif_en_loc" };
static const char * const rtd1315e_test_loop_groups[] = { "gpio_50" };
static const char * const rtd1315e_pmic_pwrup_groups[] = { "gpio_78" };

#define RTD1315E_FUNC(_name) \
	{ \
		.name = # _name, \
		.groups = rtd1315e_ ## _name ## _groups, \
		.num_groups = ARRAY_SIZE(rtd1315e_ ## _name ## _groups), \
	}

static const struct rtd_pin_func_desc rtd1315e_pin_functions[] = {
	RTD1315E_FUNC(gpio),
	RTD1315E_FUNC(nf),
	RTD1315E_FUNC(emmc),
	RTD1315E_FUNC(ao),
	RTD1315E_FUNC(gspi_loc0),
	RTD1315E_FUNC(gspi_loc1),
	RTD1315E_FUNC(uart0),
	RTD1315E_FUNC(uart1),
	RTD1315E_FUNC(uart2_loc0),
	RTD1315E_FUNC(uart2_loc1),
	RTD1315E_FUNC(i2c0),
	RTD1315E_FUNC(i2c1),
	RTD1315E_FUNC(i2c4),
	RTD1315E_FUNC(i2c5),
	RTD1315E_FUNC(pcie1),
	RTD1315E_FUNC(etn_led),
	RTD1315E_FUNC(etn_phy),
	RTD1315E_FUNC(spi),
	RTD1315E_FUNC(pwm0_loc0),
	RTD1315E_FUNC(pwm0_loc1),
	RTD1315E_FUNC(pwm1_loc0),
	RTD1315E_FUNC(pwm1_loc1),
	RTD1315E_FUNC(pwm2_loc0),
	RTD1315E_FUNC(pwm2_loc1),
	RTD1315E_FUNC(pwm3_loc0),
	RTD1315E_FUNC(pwm3_loc1),
	RTD1315E_FUNC(spdif_optical_loc0),
	RTD1315E_FUNC(spdif_optical_loc1),
	RTD1315E_FUNC(usb_cc1),
	RTD1315E_FUNC(usb_cc2),
	RTD1315E_FUNC(sd),
	RTD1315E_FUNC(dmic_loc0),
	RTD1315E_FUNC(dmic_loc1),
	RTD1315E_FUNC(ai_loc0),
	RTD1315E_FUNC(ai_loc1),
	RTD1315E_FUNC(tdm_ai_loc0),
	RTD1315E_FUNC(tdm_ai_loc1),
	RTD1315E_FUNC(hi_loc0),
	RTD1315E_FUNC(hi_m),
	RTD1315E_FUNC(vtc_i2so),
	RTD1315E_FUNC(vtc_i2si_loc0),
	RTD1315E_FUNC(vtc_i2si_loc1),
	RTD1315E_FUNC(vtc_dmic_loc0),
	RTD1315E_FUNC(vtc_dmic_loc1),
	RTD1315E_FUNC(vtc_tdm_loc0),
	RTD1315E_FUNC(vtc_tdm_loc1),
	RTD1315E_FUNC(dc_fan),
	RTD1315E_FUNC(pll_test_loc0),
	RTD1315E_FUNC(pll_test_loc1),
	RTD1315E_FUNC(ir_rx),
	RTD1315E_FUNC(uart2_disable),
	RTD1315E_FUNC(gspi_disable),
	RTD1315E_FUNC(hi_width_disable),
	RTD1315E_FUNC(hi_width_1bit),
	RTD1315E_FUNC(sf_disable),
	RTD1315E_FUNC(sf_enable),
	RTD1315E_FUNC(scpu_ejtag_loc0),
	RTD1315E_FUNC(scpu_ejtag_loc1),
	RTD1315E_FUNC(scpu_ejtag_loc2),
	RTD1315E_FUNC(scpu_ejtag_loc3),
	RTD1315E_FUNC(acpu_ejtag_loc0),
	RTD1315E_FUNC(acpu_ejtag_loc1),
	RTD1315E_FUNC(acpu_ejtag_loc2),
	RTD1315E_FUNC(vcpu_ejtag_loc0),
	RTD1315E_FUNC(vcpu_ejtag_loc1),
	RTD1315E_FUNC(vcpu_ejtag_loc2),
	RTD1315E_FUNC(aucpu_ejtag_loc0),
	RTD1315E_FUNC(aucpu_ejtag_loc1),
	RTD1315E_FUNC(aucpu_ejtag_loc2),
	RTD1315E_FUNC(gpu_ejtag),
	RTD1315E_FUNC(iso_tristate),
	RTD1315E_FUNC(dbg_out0),
	RTD1315E_FUNC(dbg_out1),
	RTD1315E_FUNC(standby_dbg),
	RTD1315E_FUNC(spdif),
	RTD1315E_FUNC(arm_trace_debug_disable),
	RTD1315E_FUNC(arm_trace_debug_enable),
	RTD1315E_FUNC(aucpu_ejtag_disable),
	RTD1315E_FUNC(acpu_ejtag_disable),
	RTD1315E_FUNC(vcpu_ejtag_disable),
	RTD1315E_FUNC(scpu_ejtag_disable),
	RTD1315E_FUNC(vtc_dmic_loc_disable),
	RTD1315E_FUNC(vtc_tdm_disable),
	RTD1315E_FUNC(vtc_i2si_disable),
	RTD1315E_FUNC(tdm_ai_disable),
	RTD1315E_FUNC(ai_disable),
	RTD1315E_FUNC(spdif_disable),
	RTD1315E_FUNC(hif_disable),
	RTD1315E_FUNC(hif_enable),
	RTD1315E_FUNC(test_loop),
	RTD1315E_FUNC(pmic_pwrup),
};

#undef RTD1315E_FUNC

static const struct rtd_pin_desc rtd1315e_iso_muxes[ARRAY_SIZE(rtd1315e_iso_pins)] = {
	[RTD1315E_ISO_EMMC_RST_N] = RTK_PIN_MUX(emmc_rst_n, 0x0, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")),
	[RTD1315E_ISO_EMMC_DD_SB] = RTK_PIN_MUX(emmc_dd_sb, 0x0, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")),
	[RTD1315E_ISO_EMMC_CLK] = RTK_PIN_MUX(emmc_clk, 0x0, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")),
	[RTD1315E_ISO_EMMC_CMD] = RTK_PIN_MUX(emmc_cmd, 0x0, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")),
	[RTD1315E_ISO_EMMC_DATA_0] = RTK_PIN_MUX(emmc_data_0, 0x0, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")),
	[RTD1315E_ISO_EMMC_DATA_1] = RTK_PIN_MUX(emmc_data_1, 0x0, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")),
	[RTD1315E_ISO_EMMC_DATA_2] = RTK_PIN_MUX(emmc_data_2, 0x0, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 24), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")),
	[RTD1315E_ISO_EMMC_DATA_3] = RTK_PIN_MUX(emmc_data_3, 0x0, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 28), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 28), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "iso_tristate")),

	[RTD1315E_ISO_EMMC_DATA_4] = RTK_PIN_MUX(emmc_data_4, 0x4, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")),
	[RTD1315E_ISO_EMMC_DATA_5] = RTK_PIN_MUX(emmc_data_5, 0x4, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")),
	[RTD1315E_ISO_EMMC_DATA_6] = RTK_PIN_MUX(emmc_data_6, 0x4, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")),
	[RTD1315E_ISO_EMMC_DATA_7] = RTK_PIN_MUX(emmc_data_7, 0x4, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")),
	[RTD1315E_ISO_GPIO_0] = RTK_PIN_MUX(gpio_0, 0x4, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "pll_test_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "dbg_out0")),
	[RTD1315E_ISO_GPIO_1] = RTK_PIN_MUX(gpio_1, 0x4, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "standby_dbg"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "pll_test_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 20), "dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 20), "ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")),
	[RTD1315E_ISO_GPIO_6] = RTK_PIN_MUX(gpio_6, 0x4, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "standby_dbg"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 24), "dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 24), "spdif_optical_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "dbg_out1")),
	[RTD1315E_ISO_GPIO_7] = RTK_PIN_MUX(gpio_7, 0x4, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "iso_tristate")),

	[RTD1315E_ISO_GPIO_8] = RTK_PIN_MUX(gpio_8, 0x8, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "uart1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 0), "gspi_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")),
	[RTD1315E_ISO_GPIO_9] = RTK_PIN_MUX(gpio_9, 0x8, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "uart1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 4), "gspi_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")),
	[RTD1315E_ISO_GPIO_10] = RTK_PIN_MUX(gpio_10, 0x8, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "uart1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 8), "gspi_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")),
	[RTD1315E_ISO_GPIO_11] = RTK_PIN_MUX(gpio_11, 0x8, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "uart1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 12), "gspi_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")),
	[RTD1315E_ISO_GPIO_12] = RTK_PIN_MUX(gpio_12, 0x8, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "i2c0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "dbg_out0")),
	[RTD1315E_ISO_GPIO_13] = RTK_PIN_MUX(gpio_13, 0x8, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "i2c0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "dbg_out0")),
	[RTD1315E_ISO_GPIO_14] = RTK_PIN_MUX(gpio_14, 0x8, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "etn_led"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 24), "etn_phy"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "dbg_out1")),
	[RTD1315E_ISO_GPIO_15] = RTK_PIN_MUX(gpio_15, 0x8, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 28), "etn_led"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 28), "etn_phy"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "dbg_out1")),

	[RTD1315E_ISO_GPIO_16] = RTK_PIN_MUX(gpio_16, 0xc, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "i2c1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "dbg_out0")),
	[RTD1315E_ISO_GPIO_17] = RTK_PIN_MUX(gpio_17, 0xc, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "i2c1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "dbg_out0")),
	[RTD1315E_ISO_GPIO_18] = RTK_PIN_MUX(gpio_18, 0xc, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "uart2_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 8), "gspi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "dbg_out1")),
	[RTD1315E_ISO_GPIO_19] = RTK_PIN_MUX(gpio_19, 0xc, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "uart2_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 12), "gspi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "dbg_out1")),
	[RTD1315E_ISO_GPIO_20] = RTK_PIN_MUX(gpio_20, 0xc, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "uart2_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "pwm0_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 16), "gspi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 16), "spdif_optical_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "dbg_out1")),
	[RTD1315E_ISO_USB_CC2] = RTK_PIN_MUX(usb_cc2, 0xc, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "usb_cc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")),
	[RTD1315E_ISO_GPIO_25] = RTK_PIN_MUX(gpio_25, 0xc, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "uart2_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 24), "pcie1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "dbg_out1")),
	[RTD1315E_ISO_GPIO_26] = RTK_PIN_MUX(gpio_26, 0xc, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 28), "uart2_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 28), "pwm0_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "dbg_out0")),

	[RTD1315E_ISO_GPIO_27] = RTK_PIN_MUX(gpio_27, 0x10, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "uart2_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 0), "pwm1_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "dbg_out0")),
	[RTD1315E_ISO_GPIO_28] = RTK_PIN_MUX(gpio_28, 0x10, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "uart2_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 4), "pwm2_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "dbg_out0")),
	[RTD1315E_ISO_GPIO_29] = RTK_PIN_MUX(gpio_29, 0x10, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "i2c5"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "pwm1_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "dbg_out0")),
	[RTD1315E_ISO_GPIO_30] = RTK_PIN_MUX(gpio_30, 0x10, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "pwm2_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "dbg_out0")),
	[RTD1315E_ISO_GPIO_31] = RTK_PIN_MUX(gpio_31, 0x10, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "uart2_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "pwm3_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 16), "gspi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "dbg_out1")),
	[RTD1315E_ISO_GPIO_32] = RTK_PIN_MUX(gpio_32, 0x10, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "aucpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 20), "dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 20), "scpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 20), "acpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 20), "vcpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 20), "ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 20), "vtc_i2si_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")),
	[RTD1315E_ISO_GPIO_33] = RTK_PIN_MUX(gpio_33, 0x10, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 24), "aucpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 24), "dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 24), "scpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 24), "acpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 24), "vcpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 24), "ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")),
	[RTD1315E_ISO_GPIO_34] = RTK_PIN_MUX(gpio_34, 0x10, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 28), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 28), "dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 28), "i2c4"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 28), "ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "dbg_out0")),

	[RTD1315E_ISO_GPIO_35] = RTK_PIN_MUX(gpio_35, 0x14, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 0), "dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 0), "i2c4"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "dbg_out0")),
	[RTD1315E_ISO_HIF_DATA] = RTK_PIN_MUX(hif_data, 0x14, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "aucpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 4), "dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 4), "tdm_ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 4), "scpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 4), "acpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 4), "vcpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 4), "ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 4), "hi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 4), "hi_m"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 4), "vtc_i2si_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 4), "vtc_tdm_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 4), "vtc_dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 4), "scpu_ejtag_loc3"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")),
	[RTD1315E_ISO_HIF_EN] = RTK_PIN_MUX(hif_en, 0x14, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "aucpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 8), "dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 8), "tdm_ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 8), "scpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 8), "acpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 8), "vcpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 8), "ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 8), "hi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 8), "hi_m"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 8), "vtc_i2si_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 8), "vtc_tdm_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 8), "vtc_dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")),
	[RTD1315E_ISO_HIF_RDY] = RTK_PIN_MUX(hif_rdy, 0x14, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 12), "dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 12), "tdm_ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 12), "ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 12), "hi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 12), "hi_m"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 12), "vtc_i2si_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 12), "vtc_tdm_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 12), "vtc_dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")),
	[RTD1315E_ISO_HIF_CLK] = RTK_PIN_MUX(hif_clk, 0x14, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "aucpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 16), "dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 16), "tdm_ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 16), "scpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 16), "acpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 16), "vcpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 16), "ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 16), "hi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 16), "hi_m"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 16), "vtc_i2si_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 16), "vtc_tdm_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 16), "vtc_dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")),
	[RTD1315E_ISO_GPIO_46] = RTK_PIN_MUX(gpio_46, 0x14, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "i2c5"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "dbg_out0")),
	[RTD1315E_ISO_GPIO_47] = RTK_PIN_MUX(gpio_47, 0x14, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 24), "dc_fan"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 24), "pwm3_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "dbg_out1")),
	[RTD1315E_ISO_GPIO_48] = RTK_PIN_MUX(gpio_48, 0x14, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 28), "pll_test_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "dbg_out0")),

	[RTD1315E_ISO_GPIO_49] = RTK_PIN_MUX(gpio_49, 0x18, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "pll_test_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "dbg_out0")),
	[RTD1315E_ISO_GPIO_50] = RTK_PIN_MUX(gpio_50, 0x18, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "spdif"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 4), "test_loop"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "dbg_out1")),
	[RTD1315E_ISO_USB_CC1] = RTK_PIN_MUX(usb_cc1, 0x18, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "usb_cc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "dbg_out0")),
	[RTD1315E_ISO_IR_RX] = RTK_PIN_MUX(ir_rx, 0x18, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "ir_rx"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "standby_dbg"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")),
	[RTD1315E_ISO_UR0_RX] = RTK_PIN_MUX(ur0_rx, 0x18, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "uart0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")),
	[RTD1315E_ISO_UR0_TX] = RTK_PIN_MUX(ur0_tx, 0x18, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "uart0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")),
	[RTD1315E_ISO_GPIO_57] = RTK_PIN_MUX(gpio_57, 0x18, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "tdm_ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 24), "ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 24), "dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 24), "acpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 24), "vcpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 24), "aucpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 24), "vtc_i2si_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 24), "vtc_tdm_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 24), "vtc_dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 24), "scpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "dbg_out0")),
	[RTD1315E_ISO_GPIO_58] = RTK_PIN_MUX(gpio_58, 0x18, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 28), "tdm_ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 28), "ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 28), "dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 28), "acpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 28), "vcpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 28), "aucpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 28), "vtc_i2si_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 28), "vtc_tdm_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 28), "vtc_dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 28), "scpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "dbg_out0")),

	[RTD1315E_ISO_GPIO_59] = RTK_PIN_MUX(gpio_59, 0x1c, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "tdm_ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 0), "dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 0), "acpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 0), "vcpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 0), "aucpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 0), "vtc_i2si_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 0), "vtc_tdm_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 0), "vtc_dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 0), "scpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "dbg_out1")),
	[RTD1315E_ISO_GPIO_60] = RTK_PIN_MUX(gpio_60, 0x1c, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "tdm_ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 4), "dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 4), "acpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 4), "vcpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 4), "aucpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 4), "vtc_i2si_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 4), "vtc_tdm_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 4), "vtc_dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 4), "scpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "dbg_out0")),
	[RTD1315E_ISO_GPIO_61] = RTK_PIN_MUX(gpio_61, 0x1c, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 8), "dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 8), "acpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 8), "vcpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 8), "aucpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 8), "vtc_i2si_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 8), "scpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "dbg_out1")),
	[RTD1315E_ISO_GPIO_62] = RTK_PIN_MUX(gpio_62, 0x1c, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 12), "dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "dbg_out1")),
	[RTD1315E_ISO_GPIO_66] = RTK_PIN_MUX(gpio_66, 0x1c, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 16), "ao"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")),
	[RTD1315E_ISO_GPIO_67] = RTK_PIN_MUX(gpio_67, 0x1c, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 20), "vtc_i2so"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 20), "ao"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")),
	[RTD1315E_ISO_GPIO_68] = RTK_PIN_MUX(gpio_68, 0x1c, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 24), "aucpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 24), "gpu_ejtag"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 24), "scpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 24), "acpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 24), "vcpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 24), "vtc_i2so"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 24), "ao"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")),
	[RTD1315E_ISO_GPIO_69] = RTK_PIN_MUX(gpio_69, 0x1c, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 28), "aucpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 28), "gpu_ejtag"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 28), "scpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 28), "acpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 28), "vcpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 28), "vtc_i2so"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 28), "ao"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "iso_tristate")),

	[RTD1315E_ISO_GPIO_70] = RTK_PIN_MUX(gpio_70, 0x20, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 0), "aucpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 0), "gpu_ejtag"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 0), "scpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 0), "acpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 0), "vcpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 0), "vtc_i2so"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 0), "ao"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")),
	[RTD1315E_ISO_GPIO_71] = RTK_PIN_MUX(gpio_71, 0x20, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 4), "aucpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 4), "gpu_ejtag"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 4), "scpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 4), "acpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 4), "vcpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 4), "ao"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")),
	[RTD1315E_ISO_GPIO_72] = RTK_PIN_MUX(gpio_72, 0x20, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 8), "aucpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 8), "gpu_ejtag"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 8), "scpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 8), "acpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 8), "vcpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 8), "ao"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")),
	[RTD1315E_ISO_GPIO_78] = RTK_PIN_MUX(gpio_78, 0x20, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 12), "pmic_pwrup"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 12), "spi"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")),
	[RTD1315E_ISO_GPIO_79] = RTK_PIN_MUX(gpio_79, 0x20, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 16), "spi"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")),
	[RTD1315E_ISO_GPIO_80] = RTK_PIN_MUX(gpio_80, 0x20, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 20), "spi"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")),
	[RTD1315E_ISO_GPIO_81] = RTK_PIN_MUX(gpio_81, 0x20, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 24), "spi"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")),

	[RTD1315E_ISO_UR2_LOC] = RTK_PIN_MUX(ur2_loc, 0x120, GENMASK(1, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "uart2_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "uart2_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "uart2_loc1")),
	[RTD1315E_ISO_GSPI_LOC] = RTK_PIN_MUX(gspi_loc, 0x120, GENMASK(3, 2),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 2), "gspi_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 2), "gspi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 2), "gspi_loc1")),
	[RTD1315E_ISO_HI_WIDTH] = RTK_PIN_MUX(hi_width, 0x120, GENMASK(9, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "hi_width_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "hi_width_1bit")),
	[RTD1315E_ISO_SF_EN] = RTK_PIN_MUX(sf_en, 0x120, GENMASK(11, 11),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 11), "sf_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 11), "sf_enable")),
	[RTD1315E_ISO_ARM_TRACE_DBG_EN] = RTK_PIN_MUX(arm_trace_dbg_en, 0x120, GENMASK(12, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "arm_trace_debug_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "arm_trace_debug_enable")),
	[RTD1315E_ISO_EJTAG_AUCPU_LOC] = RTK_PIN_MUX(ejtag_aucpu_loc, 0x120, GENMASK(16, 14),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 14), "aucpu_ejtag_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 14), "aucpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 14), "aucpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 14), "aucpu_ejtag_loc2")),
	[RTD1315E_ISO_EJTAG_ACPU_LOC] = RTK_PIN_MUX(ejtag_acpu_loc, 0x120, GENMASK(19, 17),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 17), "acpu_ejtag_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 17), "acpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 17), "acpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 17), "acpu_ejtag_loc2")),
	[RTD1315E_ISO_EJTAG_VCPU_LOC] = RTK_PIN_MUX(ejtag_vcpu_loc, 0x120, GENMASK(22, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "vcpu_ejtag_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "vcpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "vcpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 20), "vcpu_ejtag_loc2")),
	[RTD1315E_ISO_EJTAG_SCPU_LOC] = RTK_PIN_MUX(ejtag_scpu_loc, 0x120, GENMASK(25, 23),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 23), "scpu_ejtag_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 23), "scpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 23), "scpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 23), "scpu_ejtag_loc2")),
	[RTD1315E_ISO_DMIC_LOC] = RTK_PIN_MUX(dmic_loc, 0x120, GENMASK(27, 26),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 26), "dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 26), "dmic_loc1")),

	[RTD1315E_ISO_VTC_DMIC_LOC] = RTK_PIN_MUX(vtc_dmic_loc, 0x128, GENMASK(1, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "vtc_dmic_loc_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "vtc_dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "vtc_dmic_loc1")),
	[RTD1315E_ISO_VTC_TDM_LOC] = RTK_PIN_MUX(vtc_tdm_loc, 0x128, GENMASK(3, 2),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 2), "vtc_tdm_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 2), "vtc_tdm_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 2), "vtc_tdm_loc1")),
	[RTD1315E_ISO_VTC_I2SI_LOC] = RTK_PIN_MUX(vtc_i2si_loc, 0x128, GENMASK(5, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "vtc_i2si_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "vtc_i2si_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "vtc_i2si_loc1")),
	[RTD1315E_ISO_TDM_AI_LOC] = RTK_PIN_MUX(tdm_ai_loc, 0x128, GENMASK(7, 6),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 6), "tdm_ai_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 6), "tdm_ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 6), "tdm_ai_loc1")),
	[RTD1315E_ISO_AI_LOC] = RTK_PIN_MUX(ai_loc, 0x128, GENMASK(9, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "ai_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "ai_loc1")),
	[RTD1315E_ISO_SPDIF_LOC] = RTK_PIN_MUX(spdif_loc, 0x128, GENMASK(11, 10),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 10), "spdif_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 10), "spdif_optical_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 10), "spdif_optical_loc1")),

	[RTD1315E_ISO_HIF_EN_LOC] = RTK_PIN_MUX(hif_en_loc, 0x12c, GENMASK(2, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "hif_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 0), "hif_enable")),
};

static const struct rtd_pin_config_desc rtd1315e_iso_configs[ARRAY_SIZE(rtd1315e_iso_pins)] = {
	[RTD1315E_ISO_BOOT_SEL] = RTK_PIN_CONFIG(boot_sel, 0x24, 0, 0, 1, NA, 2, 3, NA),
	[RTD1315E_ISO_EMMC_CLK] = RTK_PIN_CONFIG(emmc_clk, 0x24, 4, 0, 1, NA, 2, 12, NA),
	[RTD1315E_ISO_EMMC_CMD] = RTK_PIN_CONFIG(emmc_cmd, 0x24, 17, 0, 1, NA, 2, 13, NA),
	[RTD1315E_ISO_EMMC_DATA_0] = RTK_PIN_CONFIG(emmc_data_0, 0x28, 0, 0, 1, NA, 2, 12, NA),
	[RTD1315E_ISO_EMMC_DATA_1] = RTK_PIN_CONFIG(emmc_data_1, 0x28, 13, 0, 1, NA, 2, 12, NA),
	[RTD1315E_ISO_EMMC_DATA_2] = RTK_PIN_CONFIG(emmc_data_2, 0x2c, 0, 0, 1, NA, 2, 12, NA),
	[RTD1315E_ISO_EMMC_DATA_3] = RTK_PIN_CONFIG(emmc_data_3, 0x2c, 13, 0, 1, NA, 2, 12, NA),
	[RTD1315E_ISO_EMMC_DATA_4] = RTK_PIN_CONFIG(emmc_data_4, 0x30, 0, 0, 1, NA, 2, 12, NA),
	[RTD1315E_ISO_EMMC_DATA_5] = RTK_PIN_CONFIG(emmc_data_5, 0x30, 13, 0, 1, NA, 2, 12, NA),
	[RTD1315E_ISO_EMMC_DATA_6] = RTK_PIN_CONFIG(emmc_data_6, 0x34, 0, 0, 1, NA, 2, 12, NA),
	[RTD1315E_ISO_EMMC_DATA_7] = RTK_PIN_CONFIG(emmc_data_7, 0x34, 13, 0, 1, NA, 2, 12, NA),
	[RTD1315E_ISO_EMMC_DD_SB] = RTK_PIN_CONFIG(emmc_dd_sb, 0x38, 0, 0, 1, NA, 2, 12, NA),
	[RTD1315E_ISO_EMMC_RST_N] = RTK_PIN_CONFIG(emmc_rst_n, 0x38, 13, 0, 1, NA, 2, 12, NA),
	[RTD1315E_ISO_GPIO_1] = RTK_PIN_CONFIG(gpio_1, 0x3c, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_6] = RTK_PIN_CONFIG(gpio_6, 0x3c, 5, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_7] = RTK_PIN_CONFIG(gpio_7, 0x3c, 10, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_8] = RTK_PIN_CONFIG(gpio_8, 0x3c, 15, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_9] = RTK_PIN_CONFIG(gpio_9, 0x3c, 20, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_10] = RTK_PIN_CONFIG(gpio_10, 0x3c, 25, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_11] = RTK_PIN_CONFIG(gpio_11, 0x40, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_12] = RTK_PIN_CONFIG(gpio_12, 0x40, 5, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_13] = RTK_PIN_CONFIG(gpio_13, 0x40, 10, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_14] = RTK_PIN_CONFIG(gpio_14, 0x40, 15, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_15] = RTK_PIN_CONFIG(gpio_15, 0x40, 20, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_16] = RTK_PIN_CONFIG(gpio_16, 0x40, 25, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_17] = RTK_PIN_CONFIG(gpio_17, 0x44, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_18] = RTK_PIN_CONFIG(gpio_18, 0x44, 5, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_19] = RTK_PIN_CONFIG(gpio_19, 0x44, 10, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_20] = RTK_PIN_CONFIG(gpio_20, 0x44, 15, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_25] = RTK_PIN_CONFIG(gpio_25, 0x44, 20, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_26] = RTK_PIN_CONFIG(gpio_26, 0x44, 25, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_27] = RTK_PIN_CONFIG(gpio_27, 0x48, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_28] = RTK_PIN_CONFIG(gpio_28, 0x48, 6, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_29] = RTK_PIN_CONFIG(gpio_29, 0x48, 12, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_30] = RTK_PIN_CONFIG(gpio_30, 0x48, 17, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_31] = RTK_PIN_CONFIG(gpio_31, 0x4c, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_32] = RTK_PIN_CONFIG(gpio_32, 0x4c, 5, 0, 1, NA, 2, 12, NA),
	[RTD1315E_ISO_GPIO_33] = RTK_PIN_CONFIG(gpio_33, 0x4c, 18, 0, 1, NA, 2, 12, NA),
	[RTD1315E_ISO_GPIO_34] = RTK_PIN_CONFIG(gpio_34, 0x50, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_35] = RTK_PIN_CONFIG(gpio_35, 0x50, 5, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_46] = RTK_PIN_CONFIG(gpio_46, 0x50, 10, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_47] = RTK_PIN_CONFIG(gpio_47, 0x50, 15, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_48] = RTK_PIN_CONFIG(gpio_48, 0x50, 20, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_49] = RTK_PIN_CONFIG(gpio_49, 0x50, 25, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_50] = RTK_PIN_CONFIG(gpio_50, 0x54, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_57] = RTK_PIN_CONFIG(gpio_57, 0x54, 5, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_58] = RTK_PIN_CONFIG(gpio_58, 0x54, 10, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_59] = RTK_PIN_CONFIG(gpio_59, 0x54, 15, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_60] = RTK_PIN_CONFIG(gpio_60, 0x54, 20, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_61] = RTK_PIN_CONFIG(gpio_61, 0x54, 25, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_62] = RTK_PIN_CONFIG(gpio_62, 0x58, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_66] = RTK_PIN_CONFIG(gpio_66, 0x58, 5, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_67] = RTK_PIN_CONFIG(gpio_67, 0x58, 10, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_68] = RTK_PIN_CONFIG(gpio_68, 0x58, 15, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_69] = RTK_PIN_CONFIG(gpio_69, 0x58, 20, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_70] = RTK_PIN_CONFIG(gpio_70, 0x58, 25, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_71] = RTK_PIN_CONFIG(gpio_71, 0x5c, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_72] = RTK_PIN_CONFIG(gpio_72, 0x5c, 5, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_GPIO_78] = RTK_PIN_CONFIG(gpio_78, 0x5c, 10, 0, 1, NA, 2, 12, NA),
	[RTD1315E_ISO_GPIO_79] = RTK_PIN_CONFIG(gpio_79, 0x60, 0, 0, 1, NA, 2, 12, NA),
	[RTD1315E_ISO_GPIO_80] = RTK_PIN_CONFIG(gpio_80, 0x60, 13, 0, 1, NA, 2, 12, NA),
	[RTD1315E_ISO_GPIO_81] = RTK_PIN_CONFIG(gpio_81, 0x64, 0, 0, 1, NA, 2, 12, NA),
	[RTD1315E_ISO_HIF_CLK] = RTK_PIN_CONFIG(hif_clk, 0x64, 13, 0, 1, NA, 2, 12, NA),
	[RTD1315E_ISO_HIF_DATA] = RTK_PIN_CONFIG(hif_data, 0x68, 0, 0, 1, NA, 2, 12, NA),
	[RTD1315E_ISO_HIF_EN] = RTK_PIN_CONFIG(hif_en, 0x68, 13, 0, 1, NA, 2, 12, NA),
	[RTD1315E_ISO_HIF_RDY] = RTK_PIN_CONFIG(hif_rdy, 0x68, 26, 0, 1, NA, 2, 12, NA),
	[RTD1315E_ISO_IR_RX] = RTK_PIN_CONFIG(ir_rx, 0x6c, 7, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_RESET_N] = RTK_PIN_CONFIG(reset_n, 0x6c, 12, 0, 1, NA, 2, 3, PADDRI_4_8),
	[RTD1315E_ISO_SCAN_SWITCH] = RTK_PIN_CONFIG(scan_switch, 0x6c, 16, NA, NA, 0, 1, 2, PADDRI_4_8),
	[RTD1315E_ISO_TESTMODE] = RTK_PIN_CONFIG(testmode, 0x6c, 19, 0, 1, NA, 2, 3, PADDRI_4_8),
	[RTD1315E_ISO_UR0_RX] = RTK_PIN_CONFIG(ur0_rx, 0x6c, 23, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_UR0_TX] = RTK_PIN_CONFIG(ur0_tx, 0x6c, 28, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1315E_ISO_USB_CC1] = RTK_PIN_CONFIG(usb_cc1, 0x70, 1, NA, NA, 0, 1, 2, PADDRI_4_8),
	[RTD1315E_ISO_USB_CC2] = RTK_PIN_CONFIG(usb_cc2, 0x70, 4, NA, NA, 0, 1, 2, PADDRI_4_8),
	[RTD1315E_ISO_WD_RSET] = RTK_PIN_CONFIG(wd_rset, 0x70, 7, 1, 2, 0, 3, 4, PADDRI_4_8),
};

static const struct rtd_pin_sconfig_desc rtd1315e_iso_sconfigs[] = {
	RTK_PIN_SCONFIG(emmc_clk, 0x24, 7, 3, 10, 3, 13, 3),
	RTK_PIN_SCONFIG(emmc_cmd, 0x24, 20, 3, 23, 3, 26, 3),
	RTK_PIN_SCONFIG(emmc_data_0, 0x28, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(emmc_data_1, 0x28, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(emmc_data_2, 0x2c, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(emmc_data_3, 0x2c, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(emmc_data_4, 0x30, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(emmc_data_5, 0x30, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(emmc_data_6, 0x34, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(emmc_data_7, 0x34, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(emmc_dd_sb, 0x38, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(emmc_rst_n, 0x38, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(gpio_32, 0x4c, 8, 3, 11, 3, 14, 3),
	RTK_PIN_SCONFIG(gpio_33, 0x4c, 21, 3, 24, 3, 27, 3),
	RTK_PIN_SCONFIG(gpio_78, 0x5c, 13, 3, 16, 3, 19, 3),
	RTK_PIN_SCONFIG(gpio_79, 0x60, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(gpio_80, 0x60, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(gpio_81, 0x64, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(hif_clk, 0x64, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(hif_data, 0x68, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(hif_en, 0x68, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(hif_rdy, 0x68, 29, 3, 32, 3, 35, 3),

};

static const struct rtd_pinctrl_desc rtd1315e_iso_pinctrl_desc = {
	.pins = rtd1315e_iso_pins,
	.num_pins = ARRAY_SIZE(rtd1315e_iso_pins),
	.groups = rtd1315e_pin_groups,
	.num_groups = ARRAY_SIZE(rtd1315e_pin_groups),
	.functions = rtd1315e_pin_functions,
	.num_functions = ARRAY_SIZE(rtd1315e_pin_functions),
	.muxes = rtd1315e_iso_muxes,
	.num_muxes = ARRAY_SIZE(rtd1315e_iso_muxes),
	.configs = rtd1315e_iso_configs,
	.num_configs = ARRAY_SIZE(rtd1315e_iso_configs),
	.sconfigs = rtd1315e_iso_sconfigs,
	.num_sconfigs = ARRAY_SIZE(rtd1315e_iso_sconfigs),
};

static int rtd1315e_pinctrl_probe(struct platform_device *pdev)
{
	return rtd_pinctrl_probe(pdev, &rtd1315e_iso_pinctrl_desc);
}

static const struct of_device_id rtd1315e_pinctrl_of_match[] = {
	{ .compatible = "realtek,rtd1315e-pinctrl", },
	{},
};

static struct platform_driver rtd1315e_pinctrl_driver = {
	.driver = {
		.name = "rtd1315e-pinctrl",
		.of_match_table = rtd1315e_pinctrl_of_match,
	},
	.probe = rtd1315e_pinctrl_probe,
};

static int __init rtd1315e_pinctrl_init(void)
{
	return platform_driver_register(&rtd1315e_pinctrl_driver);
}
arch_initcall(rtd1315e_pinctrl_init);

static void __exit rtd1315e_pinctrl_exit(void)
{
	platform_driver_unregister(&rtd1315e_pinctrl_driver);
}
module_exit(rtd1315e_pinctrl_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Realtek Semiconductor Corporation");
MODULE_DESCRIPTION("Realtek DHC SoC RTD1315E pinctrl driver");

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Realtek DHC 1319D pin controller driver
 *
 * Copyright (c) 2023 Realtek Semiconductor Corp.
 *
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-rtd.h"

enum rtd13xxd_iso_pins {
	RTD1319D_ISO_GPIO_0 = 0,
	RTD1319D_ISO_GPIO_1,
	RTD1319D_ISO_GPIO_2,
	RTD1319D_ISO_GPIO_3,
	RTD1319D_ISO_GPIO_4,
	RTD1319D_ISO_GPIO_5,
	RTD1319D_ISO_GPIO_6,
	RTD1319D_ISO_GPIO_7,
	RTD1319D_ISO_GPIO_8,
	RTD1319D_ISO_GPIO_9,
	RTD1319D_ISO_GPIO_10,
	RTD1319D_ISO_GPIO_11,
	RTD1319D_ISO_GPIO_12,
	RTD1319D_ISO_GPIO_13,
	RTD1319D_ISO_GPIO_14,
	RTD1319D_ISO_GPIO_15,
	RTD1319D_ISO_GPIO_16,
	RTD1319D_ISO_GPIO_17,
	RTD1319D_ISO_GPIO_18,
	RTD1319D_ISO_GPIO_19,
	RTD1319D_ISO_GPIO_20,
	RTD1319D_ISO_GPIO_21,
	RTD1319D_ISO_GPIO_22,
	RTD1319D_ISO_GPIO_23,
	RTD1319D_ISO_USB_CC2,
	RTD1319D_ISO_GPIO_25,
	RTD1319D_ISO_GPIO_26,
	RTD1319D_ISO_GPIO_27,
	RTD1319D_ISO_GPIO_28,
	RTD1319D_ISO_GPIO_29,
	RTD1319D_ISO_GPIO_30,
	RTD1319D_ISO_GPIO_31,
	RTD1319D_ISO_GPIO_32,
	RTD1319D_ISO_GPIO_33,
	RTD1319D_ISO_GPIO_34,
	RTD1319D_ISO_GPIO_35,
	RTD1319D_ISO_HIF_DATA,
	RTD1319D_ISO_HIF_EN,
	RTD1319D_ISO_HIF_RDY,
	RTD1319D_ISO_HIF_CLK,
	RTD1319D_ISO_GPIO_40,
	RTD1319D_ISO_GPIO_41,
	RTD1319D_ISO_GPIO_42,
	RTD1319D_ISO_GPIO_43,
	RTD1319D_ISO_GPIO_44,
	RTD1319D_ISO_GPIO_45,
	RTD1319D_ISO_GPIO_46,
	RTD1319D_ISO_GPIO_47,
	RTD1319D_ISO_GPIO_48,
	RTD1319D_ISO_GPIO_49,
	RTD1319D_ISO_GPIO_50,
	RTD1319D_ISO_USB_CC1,
	RTD1319D_ISO_GPIO_52,
	RTD1319D_ISO_GPIO_53,
	RTD1319D_ISO_IR_RX,
	RTD1319D_ISO_UR0_RX,
	RTD1319D_ISO_UR0_TX,
	RTD1319D_ISO_GPIO_57,
	RTD1319D_ISO_GPIO_58,
	RTD1319D_ISO_GPIO_59,
	RTD1319D_ISO_GPIO_60,
	RTD1319D_ISO_GPIO_61,
	RTD1319D_ISO_GPIO_62,
	RTD1319D_ISO_GPIO_63,
	RTD1319D_ISO_GPIO_64,
	RTD1319D_ISO_EMMC_RST_N,
	RTD1319D_ISO_EMMC_DD_SB,
	RTD1319D_ISO_EMMC_CLK,
	RTD1319D_ISO_EMMC_CMD,
	RTD1319D_ISO_EMMC_DATA_0,
	RTD1319D_ISO_EMMC_DATA_1,
	RTD1319D_ISO_EMMC_DATA_2,
	RTD1319D_ISO_EMMC_DATA_3,
	RTD1319D_ISO_EMMC_DATA_4,
	RTD1319D_ISO_EMMC_DATA_5,
	RTD1319D_ISO_EMMC_DATA_6,
	RTD1319D_ISO_EMMC_DATA_7,
	RTD1319D_ISO_GPIO_DUMMY_77,
	RTD1319D_ISO_GPIO_78,
	RTD1319D_ISO_GPIO_79,
	RTD1319D_ISO_GPIO_80,
	RTD1319D_ISO_GPIO_81,
	RTD1319D_ISO_UR2_LOC,
	RTD1319D_ISO_GSPI_LOC,
	RTD1319D_ISO_HI_WIDTH,
	RTD1319D_ISO_SF_EN,
	RTD1319D_ISO_ARM_TRACE_DBG_EN,
	RTD1319D_ISO_EJTAG_AUCPU_LOC,
	RTD1319D_ISO_EJTAG_ACPU_LOC,
	RTD1319D_ISO_EJTAG_VCPU_LOC,
	RTD1319D_ISO_EJTAG_SCPU_LOC,
	RTD1319D_ISO_DMIC_LOC,
	RTD1319D_ISO_EJTAG_SECPU_LOC,
	RTD1319D_ISO_VTC_DMIC_LOC,
	RTD1319D_ISO_VTC_TDM_LOC,
	RTD1319D_ISO_VTC_I2SI_LOC,
	RTD1319D_ISO_TDM_AI_LOC,
	RTD1319D_ISO_AI_LOC,
	RTD1319D_ISO_SPDIF_LOC,
	RTD1319D_ISO_HIF_EN_LOC,
	RTD1319D_ISO_SC0_LOC,
	RTD1319D_ISO_SC1_LOC,
	RTD1319D_ISO_SCAN_SWITCH,
	RTD1319D_ISO_WD_RSET,
	RTD1319D_ISO_BOOT_SEL,
	RTD1319D_ISO_RESET_N,
	RTD1319D_ISO_TESTMODE,
};

static const struct pinctrl_pin_desc rtd1319d_iso_pins[] = {
	PINCTRL_PIN(RTD1319D_ISO_GPIO_0, "gpio_0"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_1, "gpio_1"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_2, "gpio_2"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_3, "gpio_3"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_4, "gpio_4"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_5, "gpio_5"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_6, "gpio_6"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_7, "gpio_7"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_8, "gpio_8"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_9, "gpio_9"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_10, "gpio_10"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_11, "gpio_11"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_12, "gpio_12"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_13, "gpio_13"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_14, "gpio_14"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_15, "gpio_15"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_16, "gpio_16"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_17, "gpio_17"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_18, "gpio_18"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_19, "gpio_19"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_20, "gpio_20"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_21, "gpio_21"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_22, "gpio_22"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_23, "gpio_23"),
	PINCTRL_PIN(RTD1319D_ISO_USB_CC2, "usb_cc2"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_25, "gpio_25"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_26, "gpio_26"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_27, "gpio_27"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_28, "gpio_28"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_29, "gpio_29"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_30, "gpio_30"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_31, "gpio_31"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_32, "gpio_32"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_33, "gpio_33"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_34, "gpio_34"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_35, "gpio_35"),
	PINCTRL_PIN(RTD1319D_ISO_HIF_DATA, "hif_data"),
	PINCTRL_PIN(RTD1319D_ISO_HIF_EN, "hif_en"),
	PINCTRL_PIN(RTD1319D_ISO_HIF_RDY, "hif_rdy"),
	PINCTRL_PIN(RTD1319D_ISO_HIF_CLK, "hif_clk"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_40, "gpio_40"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_41, "gpio_41"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_42, "gpio_42"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_43, "gpio_43"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_44, "gpio_44"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_45, "gpio_45"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_46, "gpio_46"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_47, "gpio_47"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_48, "gpio_48"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_49, "gpio_49"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_50, "gpio_50"),
	PINCTRL_PIN(RTD1319D_ISO_USB_CC1, "usb_cc1"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_52, "gpio_52"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_53, "gpio_53"),
	PINCTRL_PIN(RTD1319D_ISO_IR_RX, "ir_rx"),
	PINCTRL_PIN(RTD1319D_ISO_UR0_RX, "ur0_rx"),
	PINCTRL_PIN(RTD1319D_ISO_UR0_TX, "ur0_tx"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_57, "gpio_57"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_58, "gpio_58"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_59, "gpio_59"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_60, "gpio_60"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_61, "gpio_61"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_62, "gpio_62"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_63, "gpio_63"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_64, "gpio_64"),
	PINCTRL_PIN(RTD1319D_ISO_EMMC_RST_N, "emmc_rst_n"),
	PINCTRL_PIN(RTD1319D_ISO_EMMC_DD_SB, "emmc_dd_sb"),
	PINCTRL_PIN(RTD1319D_ISO_EMMC_CLK, "emmc_clk"),
	PINCTRL_PIN(RTD1319D_ISO_EMMC_CMD, "emmc_cmd"),
	PINCTRL_PIN(RTD1319D_ISO_EMMC_DATA_0, "emmc_data_0"),
	PINCTRL_PIN(RTD1319D_ISO_EMMC_DATA_1, "emmc_data_1"),
	PINCTRL_PIN(RTD1319D_ISO_EMMC_DATA_2, "emmc_data_2"),
	PINCTRL_PIN(RTD1319D_ISO_EMMC_DATA_3, "emmc_data_3"),
	PINCTRL_PIN(RTD1319D_ISO_EMMC_DATA_4, "emmc_data_4"),
	PINCTRL_PIN(RTD1319D_ISO_EMMC_DATA_5, "emmc_data_5"),
	PINCTRL_PIN(RTD1319D_ISO_EMMC_DATA_6, "emmc_data_6"),
	PINCTRL_PIN(RTD1319D_ISO_EMMC_DATA_7, "emmc_data_7"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_DUMMY_77, "dummy"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_78, "gpio_78"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_79, "gpio_79"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_80, "gpio_80"),
	PINCTRL_PIN(RTD1319D_ISO_GPIO_81, "gpio_81"),
	PINCTRL_PIN(RTD1319D_ISO_UR2_LOC, "ur2_loc"),
	PINCTRL_PIN(RTD1319D_ISO_GSPI_LOC, "gspi_loc"),
	PINCTRL_PIN(RTD1319D_ISO_HI_WIDTH, "hi_width"),
	PINCTRL_PIN(RTD1319D_ISO_SF_EN, "sf_en"),
	PINCTRL_PIN(RTD1319D_ISO_ARM_TRACE_DBG_EN, "arm_trace_dbg_en"),
	PINCTRL_PIN(RTD1319D_ISO_EJTAG_AUCPU_LOC, "ejtag_aucpu_loc"),
	PINCTRL_PIN(RTD1319D_ISO_EJTAG_ACPU_LOC, "ejtag_acpu_loc"),
	PINCTRL_PIN(RTD1319D_ISO_EJTAG_VCPU_LOC, "ejtag_vcpu_loc"),
	PINCTRL_PIN(RTD1319D_ISO_EJTAG_SCPU_LOC, "ejtag_scpu_loc"),
	PINCTRL_PIN(RTD1319D_ISO_DMIC_LOC, "dmic_loc"),
	PINCTRL_PIN(RTD1319D_ISO_EJTAG_SECPU_LOC, "ejtag_secpu_loc"),
	PINCTRL_PIN(RTD1319D_ISO_VTC_DMIC_LOC, "vtc_dmic_loc"),
	PINCTRL_PIN(RTD1319D_ISO_VTC_TDM_LOC, "vtc_tdm_loc"),
	PINCTRL_PIN(RTD1319D_ISO_VTC_I2SI_LOC, "vtc_i2si_loc"),
	PINCTRL_PIN(RTD1319D_ISO_TDM_AI_LOC, "tdm_ai_loc"),
	PINCTRL_PIN(RTD1319D_ISO_AI_LOC, "ai_loc"),
	PINCTRL_PIN(RTD1319D_ISO_SPDIF_LOC, "spdif_loc"),
	PINCTRL_PIN(RTD1319D_ISO_HIF_EN_LOC, "hif_en_loc"),
	PINCTRL_PIN(RTD1319D_ISO_SC0_LOC, "sc0_loc"),
	PINCTRL_PIN(RTD1319D_ISO_SC1_LOC, "sc1_loc"),
	PINCTRL_PIN(RTD1319D_ISO_SCAN_SWITCH, "scan_switch"),
	PINCTRL_PIN(RTD1319D_ISO_WD_RSET, "wd_rset"),
	PINCTRL_PIN(RTD1319D_ISO_BOOT_SEL, "boot_sel"),
	PINCTRL_PIN(RTD1319D_ISO_RESET_N, "reset_n"),
	PINCTRL_PIN(RTD1319D_ISO_TESTMODE, "testmode"),
};

/* Tagged as __maybe_unused since there are pins we may use in the future */
#define DECLARE_RTD1319D_PIN(_pin, _name) \
	static const unsigned int rtd1319d_## _name ##_pins[] __maybe_unused = { _pin }

DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_0, gpio_0);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_1, gpio_1);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_2, gpio_2);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_3, gpio_3);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_4, gpio_4);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_5, gpio_5);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_6, gpio_6);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_7, gpio_7);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_8, gpio_8);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_9, gpio_9);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_10, gpio_10);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_11, gpio_11);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_12, gpio_12);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_13, gpio_13);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_14, gpio_14);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_15, gpio_15);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_16, gpio_16);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_17, gpio_17);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_18, gpio_18);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_19, gpio_19);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_20, gpio_20);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_21, gpio_21);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_22, gpio_22);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_23, gpio_23);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_USB_CC2, usb_cc2);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_25, gpio_25);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_26, gpio_26);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_27, gpio_27);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_28, gpio_28);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_29, gpio_29);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_30, gpio_30);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_31, gpio_31);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_32, gpio_32);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_33, gpio_33);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_34, gpio_34);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_35, gpio_35);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_HIF_DATA, hif_data);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_HIF_EN, hif_en);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_HIF_RDY, hif_rdy);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_HIF_CLK, hif_clk);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_40, gpio_40);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_41, gpio_41);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_42, gpio_42);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_43, gpio_43);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_44, gpio_44);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_45, gpio_45);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_46, gpio_46);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_47, gpio_47);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_48, gpio_48);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_49, gpio_49);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_50, gpio_50);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_USB_CC1, usb_cc1);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_52, gpio_52);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_53, gpio_53);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_IR_RX, ir_rx);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_UR0_RX, ur0_rx);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_UR0_TX, ur0_tx);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_57, gpio_57);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_58, gpio_58);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_59, gpio_59);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_60, gpio_60);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_61, gpio_61);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_62, gpio_62);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_63, gpio_63);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_64, gpio_64);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_EMMC_RST_N, emmc_rst_n);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_EMMC_DD_SB, emmc_dd_sb);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_EMMC_CLK, emmc_clk);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_EMMC_CMD, emmc_cmd);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_EMMC_DATA_0, emmc_data_0);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_EMMC_DATA_1, emmc_data_1);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_EMMC_DATA_2, emmc_data_2);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_EMMC_DATA_3, emmc_data_3);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_EMMC_DATA_4, emmc_data_4);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_EMMC_DATA_5, emmc_data_5);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_EMMC_DATA_6, emmc_data_6);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_EMMC_DATA_7, emmc_data_7);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_78, gpio_78);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_79, gpio_79);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_80, gpio_80);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GPIO_81, gpio_81);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_UR2_LOC, ur2_loc);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_GSPI_LOC, gspi_loc);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_HI_WIDTH, hi_width);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_SF_EN, sf_en);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_ARM_TRACE_DBG_EN, arm_trace_dbg_en);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_EJTAG_AUCPU_LOC, ejtag_aucpu_loc);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_EJTAG_ACPU_LOC, ejtag_acpu_loc);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_EJTAG_VCPU_LOC, ejtag_vcpu_loc);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_EJTAG_SCPU_LOC, ejtag_scpu_loc);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_DMIC_LOC, dmic_loc);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_EJTAG_SECPU_LOC, ejtag_secpu_loc);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_VTC_DMIC_LOC, vtc_dmic_loc);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_VTC_TDM_LOC, vtc_tdm_loc);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_VTC_I2SI_LOC, vtc_i2si_loc);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_TDM_AI_LOC, tdm_ai_loc);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_AI_LOC, ai_loc);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_SPDIF_LOC, spdif_loc);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_HIF_EN_LOC, hif_en_loc);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_SC0_LOC, sc0_loc);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_SC1_LOC, sc1_loc);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_SCAN_SWITCH, scan_switch);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_WD_RSET, wd_rset);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_BOOT_SEL, boot_sel);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_RESET_N, reset_n);
DECLARE_RTD1319D_PIN(RTD1319D_ISO_TESTMODE, testmode);

#define RTD1319D_GROUP(_name) \
	{ \
		.name = # _name, \
		.pins = rtd1319d_ ## _name ## _pins, \
		.num_pins = ARRAY_SIZE(rtd1319d_ ## _name ## _pins), \
	}

static const struct rtd_pin_group_desc rtd1319d_pin_groups[] = {
	RTD1319D_GROUP(gpio_0),
	RTD1319D_GROUP(gpio_1),
	RTD1319D_GROUP(gpio_2),
	RTD1319D_GROUP(gpio_3),
	RTD1319D_GROUP(gpio_4),
	RTD1319D_GROUP(gpio_5),
	RTD1319D_GROUP(gpio_6),
	RTD1319D_GROUP(gpio_7),
	RTD1319D_GROUP(gpio_8),
	RTD1319D_GROUP(gpio_9),
	RTD1319D_GROUP(gpio_10),
	RTD1319D_GROUP(gpio_11),
	RTD1319D_GROUP(gpio_12),
	RTD1319D_GROUP(gpio_13),
	RTD1319D_GROUP(gpio_14),
	RTD1319D_GROUP(gpio_15),
	RTD1319D_GROUP(gpio_16),
	RTD1319D_GROUP(gpio_17),
	RTD1319D_GROUP(gpio_18),
	RTD1319D_GROUP(gpio_19),
	RTD1319D_GROUP(gpio_20),
	RTD1319D_GROUP(gpio_21),
	RTD1319D_GROUP(gpio_22),
	RTD1319D_GROUP(gpio_23),
	RTD1319D_GROUP(usb_cc2),
	RTD1319D_GROUP(gpio_25),
	RTD1319D_GROUP(gpio_26),
	RTD1319D_GROUP(gpio_27),
	RTD1319D_GROUP(gpio_28),
	RTD1319D_GROUP(gpio_29),
	RTD1319D_GROUP(gpio_30),
	RTD1319D_GROUP(gpio_31),
	RTD1319D_GROUP(gpio_32),
	RTD1319D_GROUP(gpio_33),
	RTD1319D_GROUP(gpio_34),
	RTD1319D_GROUP(gpio_35),
	RTD1319D_GROUP(hif_data),
	RTD1319D_GROUP(hif_en),
	RTD1319D_GROUP(hif_rdy),
	RTD1319D_GROUP(hif_clk),
	RTD1319D_GROUP(gpio_40),
	RTD1319D_GROUP(gpio_41),
	RTD1319D_GROUP(gpio_42),
	RTD1319D_GROUP(gpio_43),
	RTD1319D_GROUP(gpio_44),
	RTD1319D_GROUP(gpio_45),
	RTD1319D_GROUP(gpio_46),
	RTD1319D_GROUP(gpio_47),
	RTD1319D_GROUP(gpio_48),
	RTD1319D_GROUP(gpio_49),
	RTD1319D_GROUP(gpio_50),
	RTD1319D_GROUP(usb_cc1),
	RTD1319D_GROUP(gpio_52),
	RTD1319D_GROUP(gpio_53),
	RTD1319D_GROUP(ir_rx),
	RTD1319D_GROUP(ur0_rx),
	RTD1319D_GROUP(ur0_tx),
	RTD1319D_GROUP(gpio_57),
	RTD1319D_GROUP(gpio_58),
	RTD1319D_GROUP(gpio_59),
	RTD1319D_GROUP(gpio_60),
	RTD1319D_GROUP(gpio_61),
	RTD1319D_GROUP(gpio_62),
	RTD1319D_GROUP(gpio_63),
	RTD1319D_GROUP(gpio_64),
	RTD1319D_GROUP(emmc_rst_n),
	RTD1319D_GROUP(emmc_dd_sb),
	RTD1319D_GROUP(emmc_clk),
	RTD1319D_GROUP(emmc_cmd),
	RTD1319D_GROUP(emmc_data_0),
	RTD1319D_GROUP(emmc_data_1),
	RTD1319D_GROUP(emmc_data_2),
	RTD1319D_GROUP(emmc_data_3),
	RTD1319D_GROUP(emmc_data_4),
	RTD1319D_GROUP(emmc_data_5),
	RTD1319D_GROUP(emmc_data_6),
	RTD1319D_GROUP(emmc_data_7),
	RTD1319D_GROUP(gpio_78),
	RTD1319D_GROUP(gpio_79),
	RTD1319D_GROUP(gpio_80),
	RTD1319D_GROUP(gpio_81),
	RTD1319D_GROUP(ur2_loc),
	RTD1319D_GROUP(gspi_loc),
	RTD1319D_GROUP(hi_width),
	RTD1319D_GROUP(sf_en),
	RTD1319D_GROUP(arm_trace_dbg_en),
	RTD1319D_GROUP(ejtag_aucpu_loc),
	RTD1319D_GROUP(ejtag_acpu_loc),
	RTD1319D_GROUP(ejtag_vcpu_loc),
	RTD1319D_GROUP(ejtag_scpu_loc),
	RTD1319D_GROUP(dmic_loc),
	RTD1319D_GROUP(ejtag_secpu_loc),
	RTD1319D_GROUP(vtc_dmic_loc),
	RTD1319D_GROUP(vtc_tdm_loc),
	RTD1319D_GROUP(vtc_i2si_loc),
	RTD1319D_GROUP(tdm_ai_loc),
	RTD1319D_GROUP(ai_loc),
	RTD1319D_GROUP(spdif_loc),
	RTD1319D_GROUP(hif_en_loc),
	RTD1319D_GROUP(sc0_loc),
	RTD1319D_GROUP(sc1_loc),
};

static const char * const rtd1319d_gpio_groups[] = {
	"gpio_0", "gpio_1", "gpio_2", "gpio_3", "gpio_4",
	"gpio_5", "gpio_6", "gpio_7", "gpio_8", "gpio_9",
	"gpio_10", "gpio_11", "gpio_12", "gpio_13", "gpio_14",
	"gpio_15", "gpio_16", "gpio_17", "gpio_18", "gpio_19",
	"gpio_20", "gpio_21", "gpio_22", "gpio_23", "usb_cc2",
	"gpio_25", "gpio_26", "gpio_27", "gpio_28", "gpio_29",
	"gpio_30", "gpio_31", "gpio_32", "gpio_33", "gpio_34",
	"gpio_35", "hif_data", "hif_en", "hif_rdy", "hif_clk",
	"gpio_40", "gpio_41", "gpio_42", "gpio_43", "gpio_44",
	"gpio_45", "gpio_46", "gpio_47", "gpio_48", "gpio_49",
	"gpio_50", "usb_cc1", "gpio_52", "gpio_53", "ir_rx",
	"ur0_rx", "ur0_tx", "gpio_57", "gpio_58", "gpio_59",
	"gpio_60", "gpio_61", "gpio_62", "gpio_63", "gpio_64",
	"emmc_rst_n", "emmc_dd_sb", "emmc_clk", "emmc_cmd",
	"emmc_data_0", "emmc_data_1", "emmc_data_2", "emmc_data_3",
	"emmc_data_4", "emmc_data_5", "emmc_data_6", "emmc_data_7",
	"gpio_78", "gpio_79", "gpio_80", "gpio_81" };
static const char * const rtd1319d_nf_groups[] = {
	"emmc_rst_n", "emmc_clk", "emmc_cmd", "emmc_data_0",
	"emmc_data_1", "emmc_data_2", "emmc_data_3", "emmc_data_4",
	"emmc_data_5", "emmc_data_6", "emmc_data_7",
	"gpio_78", "gpio_79", "gpio_80", "gpio_81" };
static const char * const rtd1319d_emmc_groups[] = {
	"emmc_rst_n", "emmc_dd_sb", "emmc_clk", "emmc_cmd",
	"emmc_data_0", "emmc_data_1", "emmc_data_2", "emmc_data_3",
	"emmc_data_4", "emmc_data_5", "emmc_data_6", "emmc_data_7" };
static const char * const rtd1319d_tp0_groups[] = {
	"gpio_2", "gpio_3", "gpio_4", "gpio_57", "gpio_58",
	"gpio_59", "gpio_60", "gpio_61", "gpio_62", "gpio_63",
	"gpio_64" };
static const char * const rtd1319d_tp1_groups[] = {
	"gpio_61", "gpio_62", "gpio_63", "gpio_64" };
static const char * const rtd1319d_sc0_groups[] = {
	"gpio_18", "gpio_19", "gpio_31" };
static const char * const rtd1319d_sc0_data0_groups[] = { "gpio_20", "sc0_loc" };
static const char * const rtd1319d_sc0_data1_groups[] = { "gpio_30", "sc0_loc" };
static const char * const rtd1319d_sc0_data2_groups[] = { "gpio_47", "sc0_loc" };
static const char * const rtd1319d_sc1_groups[] = {
	"gpio_2", "gpio_3", "gpio_5" };
static const char * const rtd1319d_sc1_data0_groups[] = { "gpio_52", "sc1_loc" };
static const char * const rtd1319d_sc1_data1_groups[] = { "gpio_34", "sc1_loc" };
static const char * const rtd1319d_sc1_data2_groups[] = { "gpio_35", "sc1_loc" };
static const char * const rtd1319d_ao_groups[] = {
	"gpio_2", "gpio_3", "gpio_4", "gpio_61", "gpio_62",
	"gpio_63", "gpio_64" };
static const char * const rtd1319d_gspi_loc0_groups[] = {
	"gpio_18", "gpio_19", "gpio_20", "gpio_31", "gspi_loc" };
static const char * const rtd1319d_gspi_loc1_groups[] = {
	"gpio_8", "gpio_9", "gpio_10", "gpio_11", "gspi_loc" };
static const char * const rtd1319d_uart0_groups[] = { "ur0_rx", "ur0_tx"};
static const char * const rtd1319d_uart1_groups[] = {
	"gpio_8", "gpio_9", "gpio_10", "gpio_11" };
static const char * const rtd1319d_uart2_loc0_groups[] = {
	"gpio_18", "gpio_19", "gpio_20", "gpio_31", "ur2_loc" };
static const char * const rtd1319d_uart2_loc1_groups[] = {
	"gpio_25", "gpio_26", "gpio_27", "gpio_28", "ur2_loc" };
static const char * const rtd1319d_i2c0_groups[] = { "gpio_12", "gpio_13" };
static const char * const rtd1319d_i2c1_groups[] = { "gpio_16", "gpio_17" };
static const char * const rtd1319d_i2c3_groups[] = { "gpio_26", "gpio_27" };
static const char * const rtd1319d_i2c4_groups[] = { "gpio_34", "gpio_35" };
static const char * const rtd1319d_i2c5_groups[] = { "gpio_29", "gpio_46" };
static const char * const rtd1319d_pcie1_groups[] = { "gpio_22" };
static const char * const rtd1319d_sdio_groups[] = {
	"gpio_40", "gpio_41", "gpio_42", "gpio_43", "gpio_44",
	"gpio_45" };
static const char * const rtd1319d_etn_led_groups[] = { "gpio_14", "gpio_15" };
static const char * const rtd1319d_etn_phy_groups[] = { "gpio_14", "gpio_15" };
static const char * const rtd1319d_spi_groups[] = {
	"gpio_18", "gpio_19", "gpio_20", "gpio_31" };
static const char * const rtd1319d_pwm0_loc0_groups[] = { "gpio_26" };
static const char * const rtd1319d_pwm0_loc1_groups[] = { "gpio_20" };
static const char * const rtd1319d_pwm1_loc0_groups[] = { "gpio_27" };
static const char * const rtd1319d_pwm1_loc1_groups[] = { "gpio_21" };

static const char * const rtd1319d_pwm2_loc0_groups[] = { "gpio_28" };
static const char * const rtd1319d_pwm2_loc1_groups[] = { "gpio_22" };
static const char * const rtd1319d_pwm3_loc0_groups[] = { "gpio_47" };
static const char * const rtd1319d_pwm3_loc1_groups[] = { "gpio_23" };
static const char * const rtd1319d_qam_agc_if0_groups[] = { "gpio_21" };
static const char * const rtd1319d_qam_agc_if1_groups[] = { "gpio_23" };
static const char * const rtd1319d_spdif_optical_loc0_groups[] = { "gpio_21", "spdif_loc" };
static const char * const rtd1319d_spdif_optical_loc1_groups[] = { "gpio_6", "spdif_loc" };
static const char * const rtd1319d_usb_cc1_groups[] = { "usb_cc1" };
static const char * const rtd1319d_usb_cc2_groups[] = { "usb_cc2" };
static const char * const rtd1319d_vfd_groups[] = {
	"gpio_26", "gpio_27", "gpio_28" };
static const char * const rtd1319d_sd_groups[] = {
	"gpio_32", "gpio_33", "gpio_34", "gpio_35",
	"hif_data", "hif_en", "hif_rdy", "hif_clk" };
static const char * const rtd1319d_dmic_loc0_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60", "gpio_61",
	"gpio_62", "gpio_63", "gpio_64", "dmic_loc" };
static const char * const rtd1319d_dmic_loc1_groups[] = {
	"gpio_32", "gpio_33", "gpio_34", "gpio_35",
	"hif_data", "hif_en", "hif_rdy", "hif_clk",
	"dmic_loc" };
static const char * const rtd1319d_ai_loc0_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60", "gpio_61",
	"gpio_62", "gpio_63", "ai_loc" };
static const char * const rtd1319d_ai_loc1_groups[] = {
	"gpio_32", "gpio_33", "gpio_34", "hif_data",
	"hif_en", "hif_rdy", "hif_clk", "ai_loc" };
static const char * const rtd1319d_tdm_ai_loc0_groups[] = {
	"gpio_57", "gpio_58", "gpio_59",
	"gpio_60", "tdm_ai_loc" };
static const char * const rtd1319d_tdm_ai_loc1_groups[] = {
	"hif_data", "hif_en", "hif_rdy", "hif_clk", "tdm_ai_loc" };
static const char * const rtd1319d_hi_loc0_groups[] = {
	"hif_data", "hif_en", "hif_rdy", "hif_clk" };
static const char * const rtd1319d_hi_m_groups[] = {
	"hif_data", "hif_en", "hif_rdy", "hif_clk" };
static const char * const rtd1319d_vtc_i2so_groups[] = {
	"gpio_2", "gpio_3", "gpio_4", "gpio_64"};
static const char * const rtd1319d_vtc_i2si_loc0_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60", "gpio_61",
	"vtc_i2si_loc" };
static const char * const rtd1319d_vtc_i2si_loc1_groups[] = {
	"gpio_32", "hif_data", "hif_en", "hif_rdy", "hif_clk",
	"vtc_i2si_loc" };
static const char * const rtd1319d_vtc_dmic_loc0_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60",
	"vtc_dmic_loc" };
static const char * const rtd1319d_vtc_dmic_loc1_groups[] = {
	"hif_data", "hif_en", "hif_rdy", "hif_clk",
	"vtc_dmic_loc" };
static const char * const rtd1319d_vtc_tdm_loc0_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60",
	"vtc_tdm_loc" };
static const char * const rtd1319d_vtc_tdm_loc1_groups[] = {
	"hif_data", "hif_en", "hif_rdy", "hif_clk",
	"vtc_tdm_loc" };
static const char * const rtd1319d_dc_fan_groups[] = { "gpio_47" };
static const char * const rtd1319d_pll_test_loc0_groups[] = { "gpio_52", "gpio_53" };
static const char * const rtd1319d_pll_test_loc1_groups[] = { "gpio_48", "gpio_49" };
static const char * const rtd1319d_spdif_groups[] = { "gpio_50" };
static const char * const rtd1319d_ir_rx_groups[] = { "ir_rx" };
static const char * const rtd1319d_uart2_disable_groups[] = { "ur2_loc" };
static const char * const rtd1319d_gspi_disable_groups[] = { "gspi_loc" };
static const char * const rtd1319d_hi_width_disable_groups[] = { "hi_width" };
static const char * const rtd1319d_hi_width_1bit_groups[] = { "hi_width" };
static const char * const rtd1319d_sf_disable_groups[] = { "sf_en" };
static const char * const rtd1319d_sf_enable_groups[] = { "sf_en" };
static const char * const rtd1319d_scpu_ejtag_loc0_groups[] = {
	"gpio_2", "gpio_3", "gpio_4", "gpio_5", "gpio_6",
	"ejtag_scpu_loc" };
static const char * const rtd1319d_scpu_ejtag_loc1_groups[] = {
	"gpio_32", "gpio_33", "hif_data", "hif_en", "hif_clk",
	"ejtag_scpu_loc" };
static const char * const rtd1319d_scpu_ejtag_loc2_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60", "gpio_61",
	"ejtag_scpu_loc" };
static const char * const rtd1319d_acpu_ejtag_loc0_groups[] = {
	"gpio_2", "gpio_3", "gpio_4", "gpio_5", "gpio_6",
	"ejtag_acpu_loc" };
static const char * const rtd1319d_acpu_ejtag_loc1_groups[] = {
	"gpio_32", "gpio_33", "hif_data", "hif_en", "hif_clk",
	"ejtag_acpu_loc" };
static const char * const rtd1319d_acpu_ejtag_loc2_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60", "gpio_61",
	"ejtag_acpu_loc" };
static const char * const rtd1319d_vcpu_ejtag_loc0_groups[] = {
	"gpio_2", "gpio_3", "gpio_4", "gpio_5", "gpio_6",
	"ejtag_vcpu_loc" };
static const char * const rtd1319d_vcpu_ejtag_loc1_groups[] = {
	"gpio_32", "gpio_33", "hif_data", "hif_en", "hif_clk",
	"ejtag_vcpu_loc" };
static const char * const rtd1319d_vcpu_ejtag_loc2_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60", "gpio_61",
	"ejtag_vcpu_loc" };
static const char * const rtd1319d_secpu_ejtag_loc0_groups[] = {
	"gpio_2", "gpio_3", "gpio_4", "gpio_5", "gpio_6",
	"ejtag_secpu_loc" };
static const char * const rtd1319d_secpu_ejtag_loc1_groups[] = {
	"gpio_32", "gpio_33", "hif_data", "hif_en", "hif_clk",
	"ejtag_secpu_loc" };
static const char * const rtd1319d_secpu_ejtag_loc2_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60", "gpio_61",
	"ejtag_secpu_loc" };
static const char * const rtd1319d_aucpu_ejtag_loc0_groups[] = {
	"gpio_2", "gpio_3", "gpio_4", "gpio_5", "gpio_6",
	"ejtag_aucpu_loc" };
static const char * const rtd1319d_aucpu_ejtag_loc1_groups[] = {
	"gpio_32", "gpio_33", "hif_data", "hif_en", "hif_clk",
	"ejtag_aucpu_loc" };
static const char * const rtd1319d_aucpu_ejtag_loc2_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60", "gpio_61",
	"ejtag_aucpu_loc" };
static const char * const rtd1319d_iso_tristate_groups[] = {
	"emmc_rst_n", "emmc_dd_sb", "emmc_clk", "emmc_cmd",
	"emmc_data_0", "emmc_data_1", "emmc_data_2", "emmc_data_3",
	"emmc_data_4", "emmc_data_5", "emmc_data_6", "emmc_data_7",
	"gpio_78", "gpio_79", "gpio_80", "gpio_81", "gpio_1",
	"gpio_8", "gpio_9", "gpio_10", "gpio_11", "gpio_22",
	"gpio_23", "usb_cc2", "gpio_25", "gpio_28", "gpio_29",
	"gpio_30", "gpio_32", "gpio_33", "hif_data", "hif_en",
	"hif_rdy", "hif_clk", "gpio_40", "gpio_41", "gpio_42",
	"gpio_43", "gpio_44", "gpio_45", "gpio_46", "usb_cc1",
	"ir_rx", "ur0_rx", "ur0_tx", "gpio_62", "gpio_63", "gpio_64" };
static const char * const rtd1319d_dbg_out0_groups[] = {
	"gpio_12", "gpio_13", "gpio_16", "gpio_17", "gpio_26", "gpio_27",
	"gpio_34", "gpio_35", "gpio_48", "gpio_49", "gpio_57", "gpio_58",
	"gpio_59", "gpio_60", "gpio_61" };
static const char * const rtd1319d_dbg_out1_groups[] = {
	"gpio_0", "gpio_2", "gpio_3", "gpio_4", "gpio_5", "gpio_6",
	"gpio_7", "gpio_14", "gpio_15", "gpio_18", "gpio_19", "gpio_20",
	"gpio_21", "gpio_31", "gpio_47", "gpio_50", "gpio_52", "gpio_53" };
static const char * const rtd1319d_standby_dbg_groups[] = {
	"gpio_2", "gpio_3", "ir_rx" };
static const char * const rtd1319d_arm_trace_debug_disable_groups[] = { "arm_trace_dbg_en" };
static const char * const rtd1319d_arm_trace_debug_enable_groups[] = { "arm_trace_dbg_en" };
static const char * const rtd1319d_aucpu_ejtag_disable_groups[] = { "ejtag_aucpu_loc" };
static const char * const rtd1319d_acpu_ejtag_disable_groups[] = { "ejtag_acpu_loc" };
static const char * const rtd1319d_vcpu_ejtag_disable_groups[] = { "ejtag_vcpu_loc" };
static const char * const rtd1319d_scpu_ejtag_disable_groups[] = { "ejtag_scpu_loc" };
static const char * const rtd1319d_secpu_ejtag_disable_groups[] = { "ejtag_secpu_loc" };
static const char * const rtd1319d_vtc_dmic_loc_disable_groups[] = { "vtc_dmic_loc" };
static const char * const rtd1319d_vtc_tdm_disable_groups[] = { "vtc_tdm_loc" };
static const char * const rtd1319d_vtc_i2si_disable_groups[] = { "vtc_i2si_loc" };
static const char * const rtd1319d_tdm_ai_disable_groups[] = { "tdm_ai_loc" };
static const char * const rtd1319d_ai_disable_groups[] = { "ai_loc" };
static const char * const rtd1319d_spdif_disable_groups[] = { "spdif_loc" };
static const char * const rtd1319d_hif_disable_groups[] = { "hif_en_loc" };
static const char * const rtd1319d_hif_enable_groups[] = { "hif_en_loc" };
static const char * const rtd1319d_test_loop_groups[] = { "gpio_27" };
static const char * const rtd1319d_pmic_pwrup_groups[] = { "gpio_78" };

#define RTD1319D_FUNC(_name) \
	{ \
		.name = # _name, \
		.groups = rtd1319d_ ## _name ## _groups, \
		.num_groups = ARRAY_SIZE(rtd1319d_ ## _name ## _groups), \
	}

static const struct rtd_pin_func_desc rtd1319d_pin_functions[] = {
	RTD1319D_FUNC(gpio),
	RTD1319D_FUNC(nf),
	RTD1319D_FUNC(emmc),
	RTD1319D_FUNC(tp0),
	RTD1319D_FUNC(tp1),
	RTD1319D_FUNC(sc0),
	RTD1319D_FUNC(sc0_data0),
	RTD1319D_FUNC(sc0_data1),
	RTD1319D_FUNC(sc0_data2),
	RTD1319D_FUNC(sc1),
	RTD1319D_FUNC(sc1_data0),
	RTD1319D_FUNC(sc1_data1),
	RTD1319D_FUNC(sc1_data2),
	RTD1319D_FUNC(ao),
	RTD1319D_FUNC(gspi_loc0),
	RTD1319D_FUNC(gspi_loc1),
	RTD1319D_FUNC(uart0),
	RTD1319D_FUNC(uart1),
	RTD1319D_FUNC(uart2_loc0),
	RTD1319D_FUNC(uart2_loc1),
	RTD1319D_FUNC(i2c0),
	RTD1319D_FUNC(i2c1),
	RTD1319D_FUNC(i2c3),
	RTD1319D_FUNC(i2c4),
	RTD1319D_FUNC(i2c5),
	RTD1319D_FUNC(pcie1),
	RTD1319D_FUNC(sdio),
	RTD1319D_FUNC(etn_led),
	RTD1319D_FUNC(etn_phy),
	RTD1319D_FUNC(spi),
	RTD1319D_FUNC(pwm0_loc0),
	RTD1319D_FUNC(pwm0_loc1),
	RTD1319D_FUNC(pwm1_loc0),
	RTD1319D_FUNC(pwm1_loc1),
	RTD1319D_FUNC(pwm2_loc0),
	RTD1319D_FUNC(pwm2_loc1),
	RTD1319D_FUNC(pwm3_loc0),
	RTD1319D_FUNC(pwm3_loc1),
	RTD1319D_FUNC(qam_agc_if0),
	RTD1319D_FUNC(qam_agc_if1),
	RTD1319D_FUNC(spdif_optical_loc0),
	RTD1319D_FUNC(spdif_optical_loc1),
	RTD1319D_FUNC(usb_cc1),
	RTD1319D_FUNC(usb_cc2),
	RTD1319D_FUNC(vfd),
	RTD1319D_FUNC(sd),
	RTD1319D_FUNC(dmic_loc0),
	RTD1319D_FUNC(dmic_loc1),
	RTD1319D_FUNC(ai_loc0),
	RTD1319D_FUNC(ai_loc1),
	RTD1319D_FUNC(tdm_ai_loc0),
	RTD1319D_FUNC(tdm_ai_loc1),
	RTD1319D_FUNC(hi_loc0),
	RTD1319D_FUNC(hi_m),
	RTD1319D_FUNC(vtc_i2so),
	RTD1319D_FUNC(vtc_i2si_loc0),
	RTD1319D_FUNC(vtc_i2si_loc1),
	RTD1319D_FUNC(vtc_dmic_loc0),
	RTD1319D_FUNC(vtc_dmic_loc1),
	RTD1319D_FUNC(vtc_tdm_loc0),
	RTD1319D_FUNC(vtc_tdm_loc1),
	RTD1319D_FUNC(dc_fan),
	RTD1319D_FUNC(pll_test_loc0),
	RTD1319D_FUNC(pll_test_loc1),
	RTD1319D_FUNC(ir_rx),
	RTD1319D_FUNC(uart2_disable),
	RTD1319D_FUNC(gspi_disable),
	RTD1319D_FUNC(hi_width_disable),
	RTD1319D_FUNC(hi_width_1bit),
	RTD1319D_FUNC(sf_disable),
	RTD1319D_FUNC(sf_enable),
	RTD1319D_FUNC(scpu_ejtag_loc0),
	RTD1319D_FUNC(scpu_ejtag_loc1),
	RTD1319D_FUNC(scpu_ejtag_loc2),
	RTD1319D_FUNC(acpu_ejtag_loc0),
	RTD1319D_FUNC(acpu_ejtag_loc1),
	RTD1319D_FUNC(acpu_ejtag_loc2),
	RTD1319D_FUNC(vcpu_ejtag_loc0),
	RTD1319D_FUNC(vcpu_ejtag_loc1),
	RTD1319D_FUNC(vcpu_ejtag_loc2),
	RTD1319D_FUNC(secpu_ejtag_loc0),
	RTD1319D_FUNC(secpu_ejtag_loc1),
	RTD1319D_FUNC(secpu_ejtag_loc2),
	RTD1319D_FUNC(aucpu_ejtag_loc0),
	RTD1319D_FUNC(aucpu_ejtag_loc1),
	RTD1319D_FUNC(aucpu_ejtag_loc2),
	RTD1319D_FUNC(iso_tristate),
	RTD1319D_FUNC(dbg_out0),
	RTD1319D_FUNC(dbg_out1),
	RTD1319D_FUNC(standby_dbg),
	RTD1319D_FUNC(spdif),
	RTD1319D_FUNC(arm_trace_debug_disable),
	RTD1319D_FUNC(arm_trace_debug_enable),
	RTD1319D_FUNC(aucpu_ejtag_disable),
	RTD1319D_FUNC(acpu_ejtag_disable),
	RTD1319D_FUNC(vcpu_ejtag_disable),
	RTD1319D_FUNC(scpu_ejtag_disable),
	RTD1319D_FUNC(secpu_ejtag_disable),
	RTD1319D_FUNC(vtc_dmic_loc_disable),
	RTD1319D_FUNC(vtc_tdm_disable),
	RTD1319D_FUNC(vtc_i2si_disable),
	RTD1319D_FUNC(tdm_ai_disable),
	RTD1319D_FUNC(ai_disable),
	RTD1319D_FUNC(spdif_disable),
	RTD1319D_FUNC(hif_disable),
	RTD1319D_FUNC(hif_enable),
	RTD1319D_FUNC(test_loop),
	RTD1319D_FUNC(pmic_pwrup),
};

#undef RTD1319D_FUNC

static const struct rtd_pin_desc rtd1319d_iso_muxes[] = {
	[RTD1319D_ISO_EMMC_RST_N] = RTK_PIN_MUX(emmc_rst_n, 0x0, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")),
	[RTD1319D_ISO_EMMC_DD_SB] = RTK_PIN_MUX(emmc_dd_sb, 0x0, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")),
	[RTD1319D_ISO_EMMC_CLK] = RTK_PIN_MUX(emmc_clk, 0x0, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")),
	[RTD1319D_ISO_EMMC_CMD] = RTK_PIN_MUX(emmc_cmd, 0x0, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")),
	[RTD1319D_ISO_EMMC_DATA_0] = RTK_PIN_MUX(emmc_data_0, 0x0, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")),
	[RTD1319D_ISO_EMMC_DATA_1] = RTK_PIN_MUX(emmc_data_1, 0x0, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")),
	[RTD1319D_ISO_EMMC_DATA_2] = RTK_PIN_MUX(emmc_data_2, 0x0, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 24), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")),
	[RTD1319D_ISO_EMMC_DATA_3] = RTK_PIN_MUX(emmc_data_3, 0x0, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 28), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 28), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "iso_tristate")),

	[RTD1319D_ISO_EMMC_DATA_4] = RTK_PIN_MUX(emmc_data_4, 0x4, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")),
	[RTD1319D_ISO_EMMC_DATA_5] = RTK_PIN_MUX(emmc_data_5, 0x4, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")),
	[RTD1319D_ISO_EMMC_DATA_6] = RTK_PIN_MUX(emmc_data_6, 0x4, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")),
	[RTD1319D_ISO_EMMC_DATA_7] = RTK_PIN_MUX(emmc_data_7, 0x4, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")),
	[RTD1319D_ISO_GPIO_78] = RTK_PIN_MUX(gpio_78, 0x4, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 16), "pmic_pwrup"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")),
	[RTD1319D_ISO_GPIO_79] = RTK_PIN_MUX(gpio_79, 0x4, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")),
	[RTD1319D_ISO_GPIO_80] = RTK_PIN_MUX(gpio_80, 0x4, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")),
	[RTD1319D_ISO_GPIO_81] = RTK_PIN_MUX(gpio_81, 0x4, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 28), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "iso_tristate")),

	[RTD1319D_ISO_GPIO_0] = RTK_PIN_MUX(gpio_0, 0x8, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "dbg_out1")),
	[RTD1319D_ISO_GPIO_1] = RTK_PIN_MUX(gpio_1, 0x8, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")),
	[RTD1319D_ISO_GPIO_2] = RTK_PIN_MUX(gpio_2, 0x8, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "standby_dbg"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 8), "aucpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 8), "sc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 8), "scpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 8), "acpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 8), "vcpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 8), "secpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 8), "vtc_i2so"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 8), "ao"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "dbg_out1")),
	[RTD1319D_ISO_GPIO_3] = RTK_PIN_MUX(gpio_3, 0x8, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "standby_dbg"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 12), "aucpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 12), "sc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 12), "scpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 12), "acpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 12), "vcpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 12), "secpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 12), "vtc_i2so"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 12), "ao"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "dbg_out1")),
	[RTD1319D_ISO_GPIO_4] = RTK_PIN_MUX(gpio_4, 0x8, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 16), "aucpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 16), "scpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 16), "acpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 16), "vcpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 16), "secpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 16), "vtc_i2so"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 16), "ao"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "dbg_out1")),
	[RTD1319D_ISO_GPIO_5] = RTK_PIN_MUX(gpio_5, 0x8, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 20), "aucpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 20), "sc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 20), "scpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 20), "acpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 20), "vcpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 20), "secpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "dbg_out1")),
	[RTD1319D_ISO_GPIO_6] = RTK_PIN_MUX(gpio_6, 0x8, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 24), "aucpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 24), "spdif_optical_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 24), "scpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 24), "acpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 24), "vcpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 24), "secpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "dbg_out1")),
	[RTD1319D_ISO_GPIO_7] = RTK_PIN_MUX(gpio_7, 0x8, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "dbg_out1")),

	[RTD1319D_ISO_GPIO_8] = RTK_PIN_MUX(gpio_8, 0xc, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "uart1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 0), "gspi_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")),
	[RTD1319D_ISO_GPIO_9] = RTK_PIN_MUX(gpio_9, 0xc, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "uart1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 4), "gspi_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")),
	[RTD1319D_ISO_GPIO_10] = RTK_PIN_MUX(gpio_10, 0xc, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "uart1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 8), "gspi_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")),
	[RTD1319D_ISO_GPIO_11] = RTK_PIN_MUX(gpio_11, 0xc, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "uart1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 12), "gspi_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")),
	[RTD1319D_ISO_GPIO_12] = RTK_PIN_MUX(gpio_12, 0xc, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "i2c0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "dbg_out0")),
	[RTD1319D_ISO_GPIO_13] = RTK_PIN_MUX(gpio_13, 0xc, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "i2c0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "dbg_out0")),
	[RTD1319D_ISO_GPIO_14] = RTK_PIN_MUX(gpio_14, 0xc, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "etn_led"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 24), "etn_phy"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "dbg_out1")),
	[RTD1319D_ISO_GPIO_15] = RTK_PIN_MUX(gpio_15, 0xc, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 28), "etn_led"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 28), "etn_phy"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "dbg_out1")),

	[RTD1319D_ISO_GPIO_16] = RTK_PIN_MUX(gpio_16, 0x10, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "i2c1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "dbg_out0")),
	[RTD1319D_ISO_GPIO_17] = RTK_PIN_MUX(gpio_17, 0x10, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "i2c1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "dbg_out0")),
	[RTD1319D_ISO_GPIO_18] = RTK_PIN_MUX(gpio_18, 0x10, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "uart2_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "sc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 8), "gspi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 8), "spi"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "dbg_out1")),
	[RTD1319D_ISO_GPIO_19] = RTK_PIN_MUX(gpio_19, 0x10, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "uart2_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "sc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 12), "gspi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 12), "spi"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "dbg_out1")),
	[RTD1319D_ISO_GPIO_20] = RTK_PIN_MUX(gpio_20, 0x10, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "uart2_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "pwm0_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 16), "gspi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 16), "sc0_data0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 16), "spi"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "dbg_out1")),
	[RTD1319D_ISO_GPIO_21] = RTK_PIN_MUX(gpio_21, 0x10, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "pwm1_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 20), "qam_agc_if0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 20), "spdif_optical_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "dbg_out1")),
	[RTD1319D_ISO_GPIO_22] = RTK_PIN_MUX(gpio_22, 0x10, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 24), "pwm2_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 24), "pcie1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")),
	[RTD1319D_ISO_GPIO_23] = RTK_PIN_MUX(gpio_23, 0x10, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 28), "pwm3_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 28), "qam_agc_if1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "iso_tristate")),

	[RTD1319D_ISO_USB_CC2] = RTK_PIN_MUX(usb_cc2, 0x14, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "usb_cc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")),
	[RTD1319D_ISO_GPIO_25] = RTK_PIN_MUX(gpio_25, 0x14, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "uart2_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")),
	[RTD1319D_ISO_GPIO_26] = RTK_PIN_MUX(gpio_26, 0x14, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "uart2_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "vfd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 8), "pwm0_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 8), "i2c3"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "dbg_out0")),
	[RTD1319D_ISO_GPIO_27] = RTK_PIN_MUX(gpio_27, 0x14, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "uart2_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "vfd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 12), "pwm1_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 12), "i2c3"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 12), "test_loop"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "dbg_out0")),
	[RTD1319D_ISO_GPIO_28] = RTK_PIN_MUX(gpio_28, 0x14, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "uart2_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "vfd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 16), "pwm2_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")),
	[RTD1319D_ISO_GPIO_29] = RTK_PIN_MUX(gpio_29, 0x14, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "i2c5"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")),
	[RTD1319D_ISO_GPIO_30] = RTK_PIN_MUX(gpio_30, 0x14, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 24), "sc0_data1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")),
	[RTD1319D_ISO_GPIO_31] = RTK_PIN_MUX(gpio_31, 0x14, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 28), "uart2_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 28), "sc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 28), "gspi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 28), "spi"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "dbg_out1")),

	[RTD1319D_ISO_GPIO_32] = RTK_PIN_MUX(gpio_32, 0x18, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "aucpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 0), "dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 0), "ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 0), "scpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 0), "acpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 0), "vcpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 0), "vtc_i2si_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 0), "secpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")),
	[RTD1319D_ISO_GPIO_33] = RTK_PIN_MUX(gpio_33, 0x18, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "aucpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 4), "dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 4), "ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 4), "scpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 4), "acpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 4), "vcpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 4), "secpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")),
	[RTD1319D_ISO_GPIO_34] = RTK_PIN_MUX(gpio_34, 0x18, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 8), "dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 8), "ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 8), "i2c4"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 8), "sc1_data1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "dbg_out0")),
	[RTD1319D_ISO_GPIO_35] = RTK_PIN_MUX(gpio_35, 0x18, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 12), "dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 12), "i2c4"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 12), "sc1_data2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "dbg_out0")),
	[RTD1319D_ISO_HIF_DATA] = RTK_PIN_MUX(hif_data, 0x18, GENMASK(19, 16),
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
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 16), "secpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")),
	[RTD1319D_ISO_HIF_EN] = RTK_PIN_MUX(hif_en, 0x18, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "aucpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 20), "dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 20), "tdm_ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 20), "scpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 20), "acpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 20), "vcpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 20), "ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 20), "hi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 20), "hi_m"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 20), "vtc_i2si_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 20), "vtc_tdm_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 20), "vtc_dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 20), "secpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")),
	[RTD1319D_ISO_HIF_RDY] = RTK_PIN_MUX(hif_rdy, 0x18, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 24), "dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 24), "tdm_ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 24), "ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 24), "hi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 24), "hi_m"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 24), "vtc_i2si_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 24), "vtc_tdm_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 24), "vtc_dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")),
	[RTD1319D_ISO_HIF_CLK] = RTK_PIN_MUX(hif_clk, 0x18, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 28), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 28), "aucpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 28), "dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 28), "tdm_ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 28), "scpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 28), "acpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 28), "vcpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 28), "ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 28), "hi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xa, 28), "hi_m"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 28), "vtc_i2si_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 28), "vtc_tdm_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 28), "vtc_dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 28), "secpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "iso_tristate")),

	[RTD1319D_ISO_GPIO_40] = RTK_PIN_MUX(gpio_40, 0x1c, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "sdio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")),
	[RTD1319D_ISO_GPIO_41] = RTK_PIN_MUX(gpio_41, 0x1c, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "sdio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "iso_tristate")),
	[RTD1319D_ISO_GPIO_42] = RTK_PIN_MUX(gpio_42, 0x1c, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "sdio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "iso_tristate")),
	[RTD1319D_ISO_GPIO_43] = RTK_PIN_MUX(gpio_43, 0x1c, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "sdio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")),
	[RTD1319D_ISO_GPIO_44] = RTK_PIN_MUX(gpio_44, 0x1c, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "sdio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "iso_tristate")),
	[RTD1319D_ISO_GPIO_45] = RTK_PIN_MUX(gpio_45, 0x1c, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "sdio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "iso_tristate")),
	[RTD1319D_ISO_GPIO_46] = RTK_PIN_MUX(gpio_46, 0x1c, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "i2c5"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")),
	[RTD1319D_ISO_GPIO_47] = RTK_PIN_MUX(gpio_47, 0x1c, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 28), "dc_fan"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 28), "pwm3_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 28), "sc0_data2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "dbg_out1")),

	[RTD1319D_ISO_GPIO_48] = RTK_PIN_MUX(gpio_48, 0x20, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "pll_test_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "dbg_out0")),
	[RTD1319D_ISO_GPIO_49] = RTK_PIN_MUX(gpio_49, 0x20, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "pll_test_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "dbg_out0")),
	[RTD1319D_ISO_GPIO_50] = RTK_PIN_MUX(gpio_50, 0x20, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "spdif"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "dbg_out1")),
	[RTD1319D_ISO_USB_CC1] = RTK_PIN_MUX(usb_cc1, 0x20, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "usb_cc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "iso_tristate")),
	[RTD1319D_ISO_GPIO_52] = RTK_PIN_MUX(gpio_52, 0x20, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "pll_test_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 16), "sc1_data0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "dbg_out1")),
	[RTD1319D_ISO_GPIO_53] = RTK_PIN_MUX(gpio_53, 0x20, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "pll_test_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "dbg_out1")),
	[RTD1319D_ISO_IR_RX] = RTK_PIN_MUX(ir_rx, 0x20, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "ir_rx"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 24), "standby_dbg"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")),
	[RTD1319D_ISO_UR0_RX] = RTK_PIN_MUX(ur0_rx, 0x20, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 28), "uart0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "iso_tristate")),

	[RTD1319D_ISO_UR0_TX] = RTK_PIN_MUX(ur0_tx, 0x24, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "uart0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")),
	[RTD1319D_ISO_GPIO_57] = RTK_PIN_MUX(gpio_57, 0x24, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "tdm_ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 4), "dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 4), "tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 4), "acpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 4), "vcpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 4), "secpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 4), "aucpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 4), "vtc_i2si_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 4), "vtc_tdm_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 4), "vtc_dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 4), "scpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 4), "dbg_out0")),
	[RTD1319D_ISO_GPIO_58] = RTK_PIN_MUX(gpio_58, 0x24, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "tdm_ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 8), "dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 8), "tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 8), "acpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 8), "vcpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 8), "secpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 8), "aucpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 8), "vtc_i2si_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 8), "vtc_tdm_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 8), "vtc_dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 8), "scpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 8), "dbg_out0")),
	[RTD1319D_ISO_GPIO_59] = RTK_PIN_MUX(gpio_59, 0x24, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "tdm_ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 12), "dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 12), "tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 12), "acpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 12), "vcpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 12), "secpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 12), "aucpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 12), "vtc_i2si_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 12), "vtc_tdm_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 12), "vtc_dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 12), "scpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 12), "dbg_out0")),
	[RTD1319D_ISO_GPIO_60] = RTK_PIN_MUX(gpio_60, 0x24, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "tdm_ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 16), "dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 16), "tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 16), "acpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 16), "vcpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 16), "secpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 16), "aucpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 16), "vtc_i2si_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xc, 16), "vtc_tdm_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xd, 16), "vtc_dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 16), "scpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 16), "dbg_out0")),
	[RTD1319D_ISO_GPIO_61] = RTK_PIN_MUX(gpio_61, 0x24, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "ao"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 20), "dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 20), "tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 20), "tp1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 20), "acpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 20), "vcpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 20), "secpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 20), "aucpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 20), "vtc_i2si_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xe, 20), "scpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 20), "dbg_out0")),
	[RTD1319D_ISO_GPIO_62] = RTK_PIN_MUX(gpio_62, 0x24, GENMASK(27, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 24), "ao"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 24), "dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 24), "tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 24), "tp1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 24), "iso_tristate")),
	[RTD1319D_ISO_GPIO_63] = RTK_PIN_MUX(gpio_63, 0x24, GENMASK(31, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 28), "ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 28), "ao"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 28), "dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 28), "tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 28), "tp1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 28), "iso_tristate")),

	[RTD1319D_ISO_GPIO_64] = RTK_PIN_MUX(gpio_64, 0x28, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "ao"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 0), "dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 0), "tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 0), "tp1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xb, 0), "vtc_i2so"),
		RTK_PIN_FUNC(SHIFT_LEFT(0xf, 0), "iso_tristate")),

	[RTD1319D_ISO_UR2_LOC] = RTK_PIN_MUX(ur2_loc, 0x120, GENMASK(1, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "uart2_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "uart2_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "uart2_loc1")),
	[RTD1319D_ISO_GSPI_LOC] = RTK_PIN_MUX(gspi_loc, 0x120, GENMASK(3, 2),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 2), "gspi_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 2), "gspi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 2), "gspi_loc1")),
	[RTD1319D_ISO_HI_WIDTH] = RTK_PIN_MUX(hi_width, 0x120, GENMASK(9, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "hi_width_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "hi_width_1bit")),
	[RTD1319D_ISO_SF_EN] = RTK_PIN_MUX(sf_en, 0x120, GENMASK(11, 11),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 11), "sf_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 11), "sf_enable")),
	[RTD1319D_ISO_ARM_TRACE_DBG_EN] = RTK_PIN_MUX(arm_trace_dbg_en, 0x120, GENMASK(12, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "arm_trace_debug_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "arm_trace_debug_enable")),
	[RTD1319D_ISO_EJTAG_AUCPU_LOC] = RTK_PIN_MUX(ejtag_aucpu_loc, 0x120, GENMASK(16, 14),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 14), "aucpu_ejtag_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 14), "aucpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 14), "aucpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 14), "aucpu_ejtag_loc2")),
	[RTD1319D_ISO_EJTAG_ACPU_LOC] = RTK_PIN_MUX(ejtag_acpu_loc, 0x120, GENMASK(19, 17),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 17), "acpu_ejtag_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 17), "acpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 17), "acpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 17), "acpu_ejtag_loc2")),
	[RTD1319D_ISO_EJTAG_VCPU_LOC] = RTK_PIN_MUX(ejtag_vcpu_loc, 0x120, GENMASK(22, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "vcpu_ejtag_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "vcpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "vcpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 20), "vcpu_ejtag_loc2")),
	[RTD1319D_ISO_EJTAG_SCPU_LOC] = RTK_PIN_MUX(ejtag_scpu_loc, 0x120, GENMASK(25, 23),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 23), "scpu_ejtag_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 23), "scpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 23), "scpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 23), "scpu_ejtag_loc2")),
	[RTD1319D_ISO_DMIC_LOC] = RTK_PIN_MUX(dmic_loc, 0x120, GENMASK(27, 26),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 26), "dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 26), "dmic_loc1")),

	[RTD1319D_ISO_EJTAG_SECPU_LOC] = RTK_PIN_MUX(ejtag_secpu_loc, 0x124, GENMASK(20, 18),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 18), "secpu_ejtag_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 18), "secpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 18), "secpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 18), "secpu_ejtag_loc2")),

	[RTD1319D_ISO_VTC_DMIC_LOC] = RTK_PIN_MUX(vtc_dmic_loc, 0x128, GENMASK(1, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "vtc_dmic_loc_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "vtc_dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "vtc_dmic_loc1")),
	[RTD1319D_ISO_VTC_TDM_LOC] = RTK_PIN_MUX(vtc_tdm_loc, 0x128, GENMASK(3, 2),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 2), "vtc_tdm_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 2), "vtc_tdm_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 2), "vtc_tdm_loc1")),
	[RTD1319D_ISO_VTC_I2SI_LOC] = RTK_PIN_MUX(vtc_i2si_loc, 0x128, GENMASK(5, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "vtc_i2si_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "vtc_i2si_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "vtc_i2si_loc1")),
	[RTD1319D_ISO_TDM_AI_LOC] = RTK_PIN_MUX(tdm_ai_loc, 0x128, GENMASK(7, 6),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 6), "tdm_ai_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 6), "tdm_ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 6), "tdm_ai_loc1")),
	[RTD1319D_ISO_AI_LOC] = RTK_PIN_MUX(ai_loc, 0x128, GENMASK(9, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "ai_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "ai_loc1")),
	[RTD1319D_ISO_SPDIF_LOC] = RTK_PIN_MUX(spdif_loc, 0x128, GENMASK(11, 10),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 10), "spdif_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 10), "spdif_optical_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 10), "spdif_optical_loc1")),

	[RTD1319D_ISO_HIF_EN_LOC] = RTK_PIN_MUX(hif_en_loc, 0x12c, GENMASK(2, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "hif_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 0), "hif_enable")),
	[RTD1319D_ISO_SC0_LOC] = RTK_PIN_MUX(sc0_loc, 0x188, GENMASK(9, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "sc0_data0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "sc0_data1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "sc0_data2")),
	[RTD1319D_ISO_SC1_LOC] = RTK_PIN_MUX(sc1_loc, 0x188, GENMASK(11, 10),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 10), "sc1_data0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 10), "sc1_data1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 10), "sc1_data2")),

	[RTD1319D_ISO_TESTMODE] = {0},
};

static const struct rtd_pin_config_desc rtd1319d_iso_configs[] = {
	[RTD1319D_ISO_SCAN_SWITCH] = RTK_PIN_CONFIG(scan_switch, 0x2c, 0, NA, NA, 0, 1, 2, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_18] = RTK_PIN_CONFIG(gpio_18, 0x2c, 3, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_19] = RTK_PIN_CONFIG(gpio_19, 0x2c, 8, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_20] = RTK_PIN_CONFIG(gpio_20, 0x2c, 13, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_31] = RTK_PIN_CONFIG(gpio_31, 0x2c, 18, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_8] = RTK_PIN_CONFIG(gpio_8, 0x2c, 23, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_9] = RTK_PIN_CONFIG(gpio_9, 0x30, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_10] = RTK_PIN_CONFIG(gpio_10, 0x30, 5, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_11] = RTK_PIN_CONFIG(gpio_11, 0x30, 10, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_0] = RTK_PIN_CONFIG(gpio_0, 0x30, 15, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_1] = RTK_PIN_CONFIG(gpio_1, 0x30, 20, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_5] = RTK_PIN_CONFIG(gpio_5, 0x30, 25, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_6] = RTK_PIN_CONFIG(gpio_6, 0x34, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_12] = RTK_PIN_CONFIG(gpio_12, 0x34, 5, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_13] = RTK_PIN_CONFIG(gpio_13, 0x34, 10, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_22] = RTK_PIN_CONFIG(gpio_22, 0x34, 15, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_USB_CC2] = RTK_PIN_CONFIG(usb_cc2, 0x34, 20, NA, NA, 0, 1, 2, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_29] = RTK_PIN_CONFIG(gpio_29, 0x34, 23, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_46] = RTK_PIN_CONFIG(gpio_46, 0x38, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_47] = RTK_PIN_CONFIG(gpio_47, 0x38, 5, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_USB_CC1] = RTK_PIN_CONFIG(usb_cc1, 0x38, 10, NA, NA, 0, 1, 2, PADDRI_4_8),
	[RTD1319D_ISO_WD_RSET] = RTK_PIN_CONFIG(wd_rset, 0x38, 13, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_IR_RX] = RTK_PIN_CONFIG(ir_rx, 0x38, 18, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_BOOT_SEL] = RTK_PIN_CONFIG(boot_sel, 0x38, 23, 0, 1, NA, 2, 3, PADDRI_4_8),
	[RTD1319D_ISO_RESET_N] = RTK_PIN_CONFIG(reset_n, 0x38, 27, 0, 1, NA, 2, 3, PADDRI_4_8),
	[RTD1319D_ISO_TESTMODE] = RTK_PIN_CONFIG(testmode, 0x3c, 0, 0, 1, NA, 2, 3, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_40] = RTK_PIN_CONFIG(gpio_40, 0x3c, 4, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_GPIO_41] = RTK_PIN_CONFIG(gpio_41, 0x3c, 17, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_GPIO_42] = RTK_PIN_CONFIG(gpio_42, 0x40, 0, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_GPIO_43] = RTK_PIN_CONFIG(gpio_43, 0x40, 13, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_GPIO_44] = RTK_PIN_CONFIG(gpio_44, 0x44, 0, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_GPIO_45] = RTK_PIN_CONFIG(gpio_45, 0x44, 13, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_EMMC_DATA_0] = RTK_PIN_CONFIG(emmc_data_0, 0x48, 0, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_EMMC_DATA_1] = RTK_PIN_CONFIG(emmc_data_1, 0x48, 13, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_EMMC_DATA_2] = RTK_PIN_CONFIG(emmc_data_2, 0x4c, 0, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_EMMC_DATA_3] = RTK_PIN_CONFIG(emmc_data_3, 0x4c, 13, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_EMMC_DATA_4] = RTK_PIN_CONFIG(emmc_data_4, 0x50, 0, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_EMMC_DATA_5] = RTK_PIN_CONFIG(emmc_data_5, 0x50, 13, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_EMMC_DATA_6] = RTK_PIN_CONFIG(emmc_data_6, 0x54, 0, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_EMMC_DATA_7] = RTK_PIN_CONFIG(emmc_data_7, 0x54, 13, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_EMMC_DD_SB] = RTK_PIN_CONFIG(emmc_dd_sb, 0x58, 0, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_EMMC_RST_N] = RTK_PIN_CONFIG(emmc_rst_n, 0x58, 13, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_EMMC_CMD] = RTK_PIN_CONFIG(emmc_cmd, 0x5c, 0, 0, 1, NA, 2, 13, NA),
	[RTD1319D_ISO_EMMC_CLK] = RTK_PIN_CONFIG(emmc_clk, 0x5c, 14, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_GPIO_80] = RTK_PIN_CONFIG(gpio_80, 0x60, 0, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_GPIO_78] = RTK_PIN_CONFIG(gpio_78, 0x60, 13, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_GPIO_79] = RTK_PIN_CONFIG(gpio_79, 0x64, 0, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_GPIO_81] = RTK_PIN_CONFIG(gpio_81, 0x64, 13, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_GPIO_2] = RTK_PIN_CONFIG(gpio_2, 0x64, 26, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_3] = RTK_PIN_CONFIG(gpio_3, 0x68, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_4] = RTK_PIN_CONFIG(gpio_4, 0x68, 5, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_57] = RTK_PIN_CONFIG(gpio_57, 0x68, 10, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_58] = RTK_PIN_CONFIG(gpio_58, 0x68, 15, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_59] = RTK_PIN_CONFIG(gpio_59, 0x68, 20, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_60] = RTK_PIN_CONFIG(gpio_60, 0x68, 25, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_61] = RTK_PIN_CONFIG(gpio_61, 0x6c, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_62] = RTK_PIN_CONFIG(gpio_62, 0x6c, 5, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_63] = RTK_PIN_CONFIG(gpio_63, 0x6c, 10, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_64] = RTK_PIN_CONFIG(gpio_64, 0x6c, 15, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_7] = RTK_PIN_CONFIG(gpio_7, 0x6c, 20, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_16] = RTK_PIN_CONFIG(gpio_16, 0x6c, 25, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_17] = RTK_PIN_CONFIG(gpio_17, 0x70, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_21] = RTK_PIN_CONFIG(gpio_21, 0x70, 5, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_23] = RTK_PIN_CONFIG(gpio_23, 0x70, 10, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_50] = RTK_PIN_CONFIG(gpio_50, 0x70, 15, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_HIF_EN] = RTK_PIN_CONFIG(hif_en, 0x74, 0, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_HIF_DATA] = RTK_PIN_CONFIG(hif_data, 0x74, 13, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_GPIO_33] = RTK_PIN_CONFIG(gpio_33, 0x78, 0, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_GPIO_32] = RTK_PIN_CONFIG(gpio_32, 0x78, 13, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_HIF_CLK] = RTK_PIN_CONFIG(hif_clk, 0x7c, 0, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_HIF_RDY] = RTK_PIN_CONFIG(hif_rdy, 0x7c, 13, 0, 1, NA, 2, 12, NA),
	[RTD1319D_ISO_GPIO_14] = RTK_PIN_CONFIG(gpio_14, 0x7c, 26, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_15] = RTK_PIN_CONFIG(gpio_15, 0x80, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_25] = RTK_PIN_CONFIG(gpio_25, 0x80, 5, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_26] = RTK_PIN_CONFIG(gpio_26, 0x80, 10, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_27] = RTK_PIN_CONFIG(gpio_27, 0x80, 16, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_28] = RTK_PIN_CONFIG(gpio_28, 0x80, 22, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_30] = RTK_PIN_CONFIG(gpio_30, 0x84, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_34] = RTK_PIN_CONFIG(gpio_34, 0x84, 5, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_35] = RTK_PIN_CONFIG(gpio_35, 0x84, 10, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_UR0_TX] = RTK_PIN_CONFIG(ur0_tx, 0x84, 15, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_UR0_RX] = RTK_PIN_CONFIG(ur0_rx, 0x84, 20, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_48] = RTK_PIN_CONFIG(gpio_48, 0x84, 25, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_49] = RTK_PIN_CONFIG(gpio_49, 0x88, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_52] = RTK_PIN_CONFIG(gpio_52, 0x88, 5, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1319D_ISO_GPIO_53] = RTK_PIN_CONFIG(gpio_53, 0x88, 10, 1, 2, 0, 3, 4, PADDRI_4_8),
};

static const struct rtd_pin_sconfig_desc rtd1319d_iso_sconfigs[] = {
	RTK_PIN_SCONFIG(gpio_40, 0x3c, 7, 3, 10, 3, 13, 3),
	RTK_PIN_SCONFIG(gpio_41, 0x3c, 20, 3, 23, 3, 26, 3),
	RTK_PIN_SCONFIG(gpio_42, 0x40, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(gpio_43, 0x40, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(gpio_44, 0x44, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(gpio_45, 0x44, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(emmc_data_0, 0x48, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(emmc_data_1, 0x48, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(emmc_data_2, 0x4c, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(emmc_data_3, 0x4c, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(emmc_data_4, 0x50, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(emmc_data_5, 0x50, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(emmc_data_6, 0x54, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(emmc_data_7, 0x54, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(emmc_dd_sb, 0x58, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(emmc_rst_n, 0x58, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(emmc_cmd, 0x5c, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(emmc_clk, 0x5c, 17, 3, 20, 3, 23, 3),
	RTK_PIN_SCONFIG(gpio_80, 0x60, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(gpio_78, 0x60, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(gpio_79, 0x64, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(gpio_81, 0x64, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(hif_en, 0x74, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(hif_data, 0x74, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(gpio_33, 0x78, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(gpio_32, 0x78, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(hif_clk, 0x7c, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(hif_rdy, 0x7c, 16, 3, 19, 3, 22, 3),
};

static const struct rtd_pinctrl_desc rtd1319d_iso_pinctrl_desc = {
	.pins = rtd1319d_iso_pins,
	.num_pins = ARRAY_SIZE(rtd1319d_iso_pins),
	.groups = rtd1319d_pin_groups,
	.num_groups = ARRAY_SIZE(rtd1319d_pin_groups),
	.functions = rtd1319d_pin_functions,
	.num_functions = ARRAY_SIZE(rtd1319d_pin_functions),
	.muxes = rtd1319d_iso_muxes,
	.num_muxes = ARRAY_SIZE(rtd1319d_iso_muxes),
	.configs = rtd1319d_iso_configs,
	.num_configs = ARRAY_SIZE(rtd1319d_iso_configs),
	.sconfigs = rtd1319d_iso_sconfigs,
	.num_sconfigs = ARRAY_SIZE(rtd1319d_iso_sconfigs),
};

static int rtd1319d_pinctrl_probe(struct platform_device *pdev)
{
	return rtd_pinctrl_probe(pdev, &rtd1319d_iso_pinctrl_desc);
}

static const struct of_device_id rtd1319d_pinctrl_of_match[] = {
	{ .compatible = "realtek,rtd1319d-pinctrl", },
	{},
};

static struct platform_driver rtd1319d_pinctrl_driver = {
	.driver = {
		.name = "rtd1319d-pinctrl",
		.of_match_table = rtd1319d_pinctrl_of_match,
	},
	.probe = rtd1319d_pinctrl_probe,
};

static int __init rtd1319d_pinctrl_init(void)
{
	return platform_driver_register(&rtd1319d_pinctrl_driver);
}
arch_initcall(rtd1319d_pinctrl_init);

static void __exit rtd1319d_pinctrl_exit(void)
{
	platform_driver_unregister(&rtd1319d_pinctrl_driver);
}
module_exit(rtd1319d_pinctrl_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Realtek Semiconductor Corporation");
MODULE_DESCRIPTION("Realtek DHC SoC RTD1319D pinctrl driver");

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Realtek DHC 1619B pin controller driver
 *
 * Copyright (c) 2023 Realtek Semiconductor Corp.
 *
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-rtd.h"

enum rtd16xxb_iso_pins {
	RTD1619B_ISO_GPIO_0 = 0,
	RTD1619B_ISO_GPIO_1,
	RTD1619B_ISO_GPIO_2,
	RTD1619B_ISO_GPIO_3,
	RTD1619B_ISO_GPIO_4,
	RTD1619B_ISO_GPIO_5,
	RTD1619B_ISO_GPIO_6,
	RTD1619B_ISO_GPIO_7,
	RTD1619B_ISO_GPIO_8,
	RTD1619B_ISO_GPIO_9,
	RTD1619B_ISO_GPIO_10,
	RTD1619B_ISO_GPIO_11,
	RTD1619B_ISO_GPIO_12,
	RTD1619B_ISO_GPIO_13,
	RTD1619B_ISO_GPIO_14,
	RTD1619B_ISO_GPIO_15,
	RTD1619B_ISO_GPIO_16,
	RTD1619B_ISO_GPIO_17,
	RTD1619B_ISO_GPIO_18,
	RTD1619B_ISO_GPIO_19,
	RTD1619B_ISO_GPIO_20,
	RTD1619B_ISO_GPIO_21,
	RTD1619B_ISO_GPIO_22,
	RTD1619B_ISO_GPIO_23,
	RTD1619B_ISO_USB_CC2,
	RTD1619B_ISO_GPIO_25,
	RTD1619B_ISO_GPIO_26,
	RTD1619B_ISO_GPIO_27,
	RTD1619B_ISO_GPIO_28,
	RTD1619B_ISO_GPIO_29,
	RTD1619B_ISO_GPIO_30,
	RTD1619B_ISO_GPIO_31,
	RTD1619B_ISO_GPIO_32,
	RTD1619B_ISO_GPIO_33,
	RTD1619B_ISO_GPIO_34,
	RTD1619B_ISO_GPIO_35,
	RTD1619B_ISO_HIF_DATA,
	RTD1619B_ISO_HIF_EN,
	RTD1619B_ISO_HIF_RDY,
	RTD1619B_ISO_HIF_CLK,
	RTD1619B_ISO_GPIO_40,
	RTD1619B_ISO_GPIO_41,
	RTD1619B_ISO_GPIO_42,
	RTD1619B_ISO_GPIO_43,
	RTD1619B_ISO_GPIO_44,
	RTD1619B_ISO_GPIO_45,
	RTD1619B_ISO_GPIO_46,
	RTD1619B_ISO_GPIO_47,
	RTD1619B_ISO_GPIO_48,
	RTD1619B_ISO_GPIO_49,
	RTD1619B_ISO_GPIO_50,
	RTD1619B_ISO_USB_CC1,
	RTD1619B_ISO_GPIO_52,
	RTD1619B_ISO_GPIO_53,
	RTD1619B_ISO_IR_RX,
	RTD1619B_ISO_UR0_RX,
	RTD1619B_ISO_UR0_TX,
	RTD1619B_ISO_GPIO_57,
	RTD1619B_ISO_GPIO_58,
	RTD1619B_ISO_GPIO_59,
	RTD1619B_ISO_GPIO_60,
	RTD1619B_ISO_GPIO_61,
	RTD1619B_ISO_GPIO_62,
	RTD1619B_ISO_GPIO_63,
	RTD1619B_ISO_GPIO_64,
	RTD1619B_ISO_GPIO_65,
	RTD1619B_ISO_GPIO_66,
	RTD1619B_ISO_GPIO_67,
	RTD1619B_ISO_GPIO_68,
	RTD1619B_ISO_GPIO_69,
	RTD1619B_ISO_GPIO_70,
	RTD1619B_ISO_GPIO_71,
	RTD1619B_ISO_GPIO_72,
	RTD1619B_ISO_GPIO_73,
	RTD1619B_ISO_GPIO_74,
	RTD1619B_ISO_GPIO_75,
	RTD1619B_ISO_GPIO_76,
	RTD1619B_ISO_EMMC_CMD,
	RTD1619B_ISO_SPI_CE_N,
	RTD1619B_ISO_SPI_SCK,
	RTD1619B_ISO_SPI_SO,
	RTD1619B_ISO_SPI_SI,
	RTD1619B_ISO_EMMC_RST_N,
	RTD1619B_ISO_EMMC_DD_SB,
	RTD1619B_ISO_EMMC_CLK,
	RTD1619B_ISO_EMMC_DATA_0,
	RTD1619B_ISO_EMMC_DATA_1,
	RTD1619B_ISO_EMMC_DATA_2,
	RTD1619B_ISO_EMMC_DATA_3,
	RTD1619B_ISO_EMMC_DATA_4,
	RTD1619B_ISO_EMMC_DATA_5,
	RTD1619B_ISO_EMMC_DATA_6,
	RTD1619B_ISO_EMMC_DATA_7,
	RTD1619B_ISO_UR2_LOC,
	RTD1619B_ISO_GSPI_LOC,
	RTD1619B_ISO_SDIO_LOC,
	RTD1619B_ISO_HI_LOC,
	RTD1619B_ISO_HI_WIDTH,
	RTD1619B_ISO_SF_EN,
	RTD1619B_ISO_ARM_TRACE_DBG_EN,
	RTD1619B_ISO_PWM_01_OPEN_DRAIN_EN_LOC0,
	RTD1619B_ISO_PWM_23_OPEN_DRAIN_EN_LOC0,
	RTD1619B_ISO_PWM_01_OPEN_DRAIN_EN_LOC1,
	RTD1619B_ISO_PWM_23_OPEN_DRAIN_EN_LOC1,
	RTD1619B_ISO_EJTAG_ACPU_LOC,
	RTD1619B_ISO_EJTAG_VCPU_LOC,
	RTD1619B_ISO_EJTAG_SCPU_LOC,
	RTD1619B_ISO_DMIC_LOC,
	RTD1619B_ISO_ISO_GSPI_LOC,
	RTD1619B_ISO_EJTAG_VE3_LOC,
	RTD1619B_ISO_EJTAG_AUCPU0_LOC,
	RTD1619B_ISO_EJTAG_AUCPU1_LOC,
};

static const struct pinctrl_pin_desc rtd1619b_iso_pins[] = {
	PINCTRL_PIN(RTD1619B_ISO_GPIO_0, "gpio_0"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_1, "gpio_1"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_2, "gpio_2"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_3, "gpio_3"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_4, "gpio_4"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_5, "gpio_5"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_6, "gpio_6"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_7, "gpio_7"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_8, "gpio_8"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_9, "gpio_9"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_10, "gpio_10"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_11, "gpio_11"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_12, "gpio_12"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_13, "gpio_13"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_14, "gpio_14"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_15, "gpio_15"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_16, "gpio_16"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_17, "gpio_17"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_18, "gpio_18"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_19, "gpio_19"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_20, "gpio_20"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_21, "gpio_21"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_22, "gpio_22"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_23, "gpio_23"),
	PINCTRL_PIN(RTD1619B_ISO_USB_CC2, "usb_cc2"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_25, "gpio_25"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_26, "gpio_26"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_27, "gpio_27"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_28, "gpio_28"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_29, "gpio_29"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_30, "gpio_30"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_31, "gpio_31"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_32, "gpio_32"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_33, "gpio_33"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_34, "gpio_34"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_35, "gpio_35"),
	PINCTRL_PIN(RTD1619B_ISO_HIF_DATA, "hif_data"),
	PINCTRL_PIN(RTD1619B_ISO_HIF_EN, "hif_en"),
	PINCTRL_PIN(RTD1619B_ISO_HIF_RDY, "hif_rdy"),
	PINCTRL_PIN(RTD1619B_ISO_HIF_CLK, "hif_clk"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_40, "gpio_40"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_41, "gpio_41"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_42, "gpio_42"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_43, "gpio_43"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_44, "gpio_44"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_45, "gpio_45"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_46, "gpio_46"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_47, "gpio_47"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_48, "gpio_48"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_49, "gpio_49"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_50, "gpio_50"),
	PINCTRL_PIN(RTD1619B_ISO_USB_CC1, "usb_cc1"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_52, "gpio_52"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_53, "gpio_53"),
	PINCTRL_PIN(RTD1619B_ISO_IR_RX, "ir_rx"),
	PINCTRL_PIN(RTD1619B_ISO_UR0_RX, "ur0_rx"),
	PINCTRL_PIN(RTD1619B_ISO_UR0_TX, "ur0_tx"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_57, "gpio_57"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_58, "gpio_58"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_59, "gpio_59"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_60, "gpio_60"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_61, "gpio_61"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_62, "gpio_62"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_63, "gpio_63"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_64, "gpio_64"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_65, "gpio_65"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_66, "gpio_66"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_67, "gpio_67"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_68, "gpio_68"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_69, "gpio_69"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_70, "gpio_70"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_71, "gpio_71"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_72, "gpio_72"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_73, "gpio_73"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_74, "gpio_74"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_75, "gpio_75"),
	PINCTRL_PIN(RTD1619B_ISO_GPIO_76, "gpio_76"),
	PINCTRL_PIN(RTD1619B_ISO_EMMC_CMD, "emmc_cmd"),
	PINCTRL_PIN(RTD1619B_ISO_SPI_CE_N, "spi_ce_n"),
	PINCTRL_PIN(RTD1619B_ISO_SPI_SCK, "spi_sck"),
	PINCTRL_PIN(RTD1619B_ISO_SPI_SO, "spi_so"),
	PINCTRL_PIN(RTD1619B_ISO_SPI_SI, "spi_si"),
	PINCTRL_PIN(RTD1619B_ISO_EMMC_RST_N, "emmc_rst_n"),
	PINCTRL_PIN(RTD1619B_ISO_EMMC_DD_SB, "emmc_dd_sb"),
	PINCTRL_PIN(RTD1619B_ISO_EMMC_CLK, "emmc_clk"),
	PINCTRL_PIN(RTD1619B_ISO_EMMC_DATA_0, "emmc_data_0"),
	PINCTRL_PIN(RTD1619B_ISO_EMMC_DATA_1, "emmc_data_1"),
	PINCTRL_PIN(RTD1619B_ISO_EMMC_DATA_2, "emmc_data_2"),
	PINCTRL_PIN(RTD1619B_ISO_EMMC_DATA_3, "emmc_data_3"),
	PINCTRL_PIN(RTD1619B_ISO_EMMC_DATA_4, "emmc_data_4"),
	PINCTRL_PIN(RTD1619B_ISO_EMMC_DATA_5, "emmc_data_5"),
	PINCTRL_PIN(RTD1619B_ISO_EMMC_DATA_6, "emmc_data_6"),
	PINCTRL_PIN(RTD1619B_ISO_EMMC_DATA_7, "emmc_data_7"),
	PINCTRL_PIN(RTD1619B_ISO_UR2_LOC, "ur2_loc"),
	PINCTRL_PIN(RTD1619B_ISO_GSPI_LOC, "gspi_loc"),
	PINCTRL_PIN(RTD1619B_ISO_SDIO_LOC, "sdio_loc"),
	PINCTRL_PIN(RTD1619B_ISO_HI_LOC, "hi_loc"),
	PINCTRL_PIN(RTD1619B_ISO_HI_WIDTH, "hi_width"),
	PINCTRL_PIN(RTD1619B_ISO_SF_EN, "sf_en"),
	PINCTRL_PIN(RTD1619B_ISO_ARM_TRACE_DBG_EN, "arm_trace_dbg_en"),
	PINCTRL_PIN(RTD1619B_ISO_PWM_01_OPEN_DRAIN_EN_LOC0, "pwm_01_open_drain_en_loc0"),
	PINCTRL_PIN(RTD1619B_ISO_PWM_23_OPEN_DRAIN_EN_LOC0, "pwm_23_open_drain_en_loc0"),
	PINCTRL_PIN(RTD1619B_ISO_PWM_01_OPEN_DRAIN_EN_LOC1, "pwm_01_open_drain_en_loc1"),
	PINCTRL_PIN(RTD1619B_ISO_PWM_23_OPEN_DRAIN_EN_LOC1, "pwm_23_open_drain_en_loc1"),
	PINCTRL_PIN(RTD1619B_ISO_EJTAG_ACPU_LOC, "ejtag_acpu_loc"),
	PINCTRL_PIN(RTD1619B_ISO_EJTAG_VCPU_LOC, "ejtag_vcpu_loc"),
	PINCTRL_PIN(RTD1619B_ISO_EJTAG_SCPU_LOC, "ejtag_scpu_loc"),
	PINCTRL_PIN(RTD1619B_ISO_DMIC_LOC, "dmic_loc"),
	PINCTRL_PIN(RTD1619B_ISO_ISO_GSPI_LOC, "iso_gspi_loc"),
	PINCTRL_PIN(RTD1619B_ISO_EJTAG_VE3_LOC, "ejtag_ve3_loc"),
	PINCTRL_PIN(RTD1619B_ISO_EJTAG_AUCPU0_LOC, "ejtag_aucpu0_loc"),
	PINCTRL_PIN(RTD1619B_ISO_EJTAG_AUCPU1_LOC, "ejtag_aucpu1_loc"),
};

#define DECLARE_RTD1619B_PIN(_pin, _name) \
	static const unsigned int rtd1619b_## _name ##_pins[] = { _pin }

DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_0, gpio_0);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_1, gpio_1);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_2, gpio_2);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_3, gpio_3);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_4, gpio_4);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_5, gpio_5);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_6, gpio_6);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_7, gpio_7);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_8, gpio_8);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_9, gpio_9);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_10, gpio_10);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_11, gpio_11);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_12, gpio_12);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_13, gpio_13);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_14, gpio_14);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_15, gpio_15);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_16, gpio_16);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_17, gpio_17);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_18, gpio_18);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_19, gpio_19);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_20, gpio_20);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_21, gpio_21);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_22, gpio_22);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_23, gpio_23);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_USB_CC2, usb_cc2);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_25, gpio_25);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_26, gpio_26);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_27, gpio_27);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_28, gpio_28);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_29, gpio_29);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_30, gpio_30);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_31, gpio_31);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_32, gpio_32);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_33, gpio_33);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_34, gpio_34);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_35, gpio_35);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_HIF_DATA, hif_data);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_HIF_EN, hif_en);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_HIF_RDY, hif_rdy);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_HIF_CLK, hif_clk);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_40, gpio_40);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_41, gpio_41);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_42, gpio_42);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_43, gpio_43);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_44, gpio_44);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_45, gpio_45);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_46, gpio_46);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_47, gpio_47);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_48, gpio_48);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_49, gpio_49);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_50, gpio_50);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_USB_CC1, usb_cc1);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_52, gpio_52);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_53, gpio_53);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_IR_RX, ir_rx);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_UR0_RX, ur0_rx);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_UR0_TX, ur0_tx);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_57, gpio_57);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_58, gpio_58);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_59, gpio_59);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_60, gpio_60);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_61, gpio_61);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_62, gpio_62);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_63, gpio_63);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_64, gpio_64);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_65, gpio_65);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_66, gpio_66);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_67, gpio_67);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_68, gpio_68);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_69, gpio_69);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_70, gpio_70);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_71, gpio_71);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_72, gpio_72);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_73, gpio_73);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_74, gpio_74);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_75, gpio_75);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GPIO_76, gpio_76);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_EMMC_CMD, emmc_cmd);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_SPI_CE_N, spi_ce_n);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_SPI_SCK, spi_sck);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_SPI_SO, spi_so);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_SPI_SI, spi_si);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_EMMC_RST_N, emmc_rst_n);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_EMMC_DD_SB, emmc_dd_sb);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_EMMC_CLK, emmc_clk);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_EMMC_DATA_0, emmc_data_0);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_EMMC_DATA_1, emmc_data_1);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_EMMC_DATA_2, emmc_data_2);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_EMMC_DATA_3, emmc_data_3);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_EMMC_DATA_4, emmc_data_4);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_EMMC_DATA_5, emmc_data_5);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_EMMC_DATA_6, emmc_data_6);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_EMMC_DATA_7, emmc_data_7);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_UR2_LOC, ur2_loc);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_GSPI_LOC, gspi_loc);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_SDIO_LOC, sdio_loc);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_HI_LOC, hi_loc);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_HI_WIDTH, hi_width);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_SF_EN, sf_en);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_ARM_TRACE_DBG_EN, arm_trace_dbg_en);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_PWM_01_OPEN_DRAIN_EN_LOC0, pwm_01_open_drain_en_loc0);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_PWM_23_OPEN_DRAIN_EN_LOC0, pwm_23_open_drain_en_loc0);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_PWM_01_OPEN_DRAIN_EN_LOC1, pwm_01_open_drain_en_loc1);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_PWM_23_OPEN_DRAIN_EN_LOC1, pwm_23_open_drain_en_loc1);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_EJTAG_ACPU_LOC, ejtag_acpu_loc);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_EJTAG_VCPU_LOC, ejtag_vcpu_loc);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_EJTAG_SCPU_LOC, ejtag_scpu_loc);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_DMIC_LOC, dmic_loc);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_ISO_GSPI_LOC, iso_gspi_loc);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_EJTAG_VE3_LOC, ejtag_ve3_loc);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_EJTAG_AUCPU0_LOC, ejtag_aucpu0_loc);
DECLARE_RTD1619B_PIN(RTD1619B_ISO_EJTAG_AUCPU1_LOC, ejtag_aucpu1_loc);

#define RTD1619B_GROUP(_name) \
	{ \
		.name = # _name, \
		.pins = rtd1619b_ ## _name ## _pins, \
		.num_pins = ARRAY_SIZE(rtd1619b_ ## _name ## _pins), \
	}

static const struct rtd_pin_group_desc rtd1619b_pin_groups[] = {
	RTD1619B_GROUP(gpio_0),
	RTD1619B_GROUP(gpio_1),
	RTD1619B_GROUP(gpio_2),
	RTD1619B_GROUP(gpio_3),
	RTD1619B_GROUP(gpio_4),
	RTD1619B_GROUP(gpio_5),
	RTD1619B_GROUP(gpio_6),
	RTD1619B_GROUP(gpio_7),
	RTD1619B_GROUP(gpio_8),
	RTD1619B_GROUP(gpio_9),
	RTD1619B_GROUP(gpio_10),
	RTD1619B_GROUP(gpio_11),
	RTD1619B_GROUP(gpio_12),
	RTD1619B_GROUP(gpio_13),
	RTD1619B_GROUP(gpio_14),
	RTD1619B_GROUP(gpio_15),
	RTD1619B_GROUP(gpio_16),
	RTD1619B_GROUP(gpio_17),
	RTD1619B_GROUP(gpio_18),
	RTD1619B_GROUP(gpio_19),
	RTD1619B_GROUP(gpio_20),
	RTD1619B_GROUP(gpio_21),
	RTD1619B_GROUP(gpio_22),
	RTD1619B_GROUP(gpio_23),
	RTD1619B_GROUP(usb_cc2),
	RTD1619B_GROUP(gpio_25),
	RTD1619B_GROUP(gpio_26),
	RTD1619B_GROUP(gpio_27),
	RTD1619B_GROUP(gpio_28),
	RTD1619B_GROUP(gpio_29),
	RTD1619B_GROUP(gpio_30),
	RTD1619B_GROUP(gpio_31),
	RTD1619B_GROUP(gpio_32),
	RTD1619B_GROUP(gpio_33),
	RTD1619B_GROUP(gpio_34),
	RTD1619B_GROUP(gpio_35),
	RTD1619B_GROUP(hif_data),
	RTD1619B_GROUP(hif_en),
	RTD1619B_GROUP(hif_rdy),
	RTD1619B_GROUP(hif_clk),
	RTD1619B_GROUP(gpio_40),
	RTD1619B_GROUP(gpio_41),
	RTD1619B_GROUP(gpio_42),
	RTD1619B_GROUP(gpio_43),
	RTD1619B_GROUP(gpio_44),
	RTD1619B_GROUP(gpio_45),
	RTD1619B_GROUP(gpio_46),
	RTD1619B_GROUP(gpio_47),
	RTD1619B_GROUP(gpio_48),
	RTD1619B_GROUP(gpio_49),
	RTD1619B_GROUP(gpio_50),
	RTD1619B_GROUP(usb_cc1),
	RTD1619B_GROUP(gpio_52),
	RTD1619B_GROUP(gpio_53),
	RTD1619B_GROUP(ir_rx),
	RTD1619B_GROUP(ur0_rx),
	RTD1619B_GROUP(ur0_tx),
	RTD1619B_GROUP(gpio_57),
	RTD1619B_GROUP(gpio_58),
	RTD1619B_GROUP(gpio_59),
	RTD1619B_GROUP(gpio_60),
	RTD1619B_GROUP(gpio_61),
	RTD1619B_GROUP(gpio_62),
	RTD1619B_GROUP(gpio_63),
	RTD1619B_GROUP(gpio_64),
	RTD1619B_GROUP(gpio_65),
	RTD1619B_GROUP(gpio_66),
	RTD1619B_GROUP(gpio_67),
	RTD1619B_GROUP(gpio_68),
	RTD1619B_GROUP(gpio_69),
	RTD1619B_GROUP(gpio_70),
	RTD1619B_GROUP(gpio_71),
	RTD1619B_GROUP(gpio_72),
	RTD1619B_GROUP(gpio_73),
	RTD1619B_GROUP(gpio_74),
	RTD1619B_GROUP(gpio_75),
	RTD1619B_GROUP(gpio_76),
	RTD1619B_GROUP(emmc_cmd),
	RTD1619B_GROUP(spi_ce_n),
	RTD1619B_GROUP(spi_sck),
	RTD1619B_GROUP(spi_so),
	RTD1619B_GROUP(spi_si),
	RTD1619B_GROUP(emmc_rst_n),
	RTD1619B_GROUP(emmc_dd_sb),
	RTD1619B_GROUP(emmc_clk),
	RTD1619B_GROUP(emmc_data_0),
	RTD1619B_GROUP(emmc_data_1),
	RTD1619B_GROUP(emmc_data_2),
	RTD1619B_GROUP(emmc_data_3),
	RTD1619B_GROUP(emmc_data_4),
	RTD1619B_GROUP(emmc_data_5),
	RTD1619B_GROUP(emmc_data_6),
	RTD1619B_GROUP(emmc_data_7),
	RTD1619B_GROUP(ur2_loc),
	RTD1619B_GROUP(gspi_loc),
	RTD1619B_GROUP(sdio_loc),
	RTD1619B_GROUP(hi_loc),
	RTD1619B_GROUP(hi_width),
	RTD1619B_GROUP(sf_en),
	RTD1619B_GROUP(arm_trace_dbg_en),
	RTD1619B_GROUP(pwm_01_open_drain_en_loc0),
	RTD1619B_GROUP(pwm_23_open_drain_en_loc0),
	RTD1619B_GROUP(pwm_01_open_drain_en_loc1),
	RTD1619B_GROUP(pwm_23_open_drain_en_loc1),
	RTD1619B_GROUP(ejtag_acpu_loc),
	RTD1619B_GROUP(ejtag_vcpu_loc),
	RTD1619B_GROUP(ejtag_scpu_loc),
	RTD1619B_GROUP(dmic_loc),
	RTD1619B_GROUP(iso_gspi_loc),
	RTD1619B_GROUP(ejtag_ve3_loc),
	RTD1619B_GROUP(ejtag_aucpu0_loc),
	RTD1619B_GROUP(ejtag_aucpu1_loc),
};

static const char * const rtd1619b_gpio_groups[] = {
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
	"gpio_65", "gpio_66", "gpio_67", "gpio_68", "gpio_69",
	"gpio_70", "gpio_71", "gpio_72", "gpio_73", "gpio_74",
	"gpio_75", "gpio_76", "emmc_cmd", "spi_ce_n", "spi_sck",
	"spi_so", "spi_si"};
static const char * const rtd1619b_nf_groups[] = {
	"emmc_rst_n", "emmc_clk", "emmc_cmd", "emmc_data_0", "emmc_data_1",
	"emmc_data_2", "emmc_data_3", "emmc_data_4",
	"emmc_data_5", "emmc_data_6", "emmc_data_7", "spi_ce_n",
	"spi_sck", "spi_so", "spi_si" };
static const char * const rtd1619b_nf_spi_groups[] = {
	"emmc_data_0", "emmc_data_1",
	"emmc_data_2", "emmc_data_3", "emmc_data_4",
	"emmc_data_5" };
static const char * const rtd1619b_spi_groups[] = {
	"spi_ce_n", "spi_sck", "spi_so", "spi_si" };
static const char * const rtd1619b_emmc_groups[] = {
	"emmc_rst_n", "emmc_clk", "emmc_data_0", "emmc_data_1",
	"emmc_data_2", "emmc_data_3", "emmc_data_4",
	"emmc_data_5", "emmc_data_6", "emmc_data_7",
	"emmc_dd_sb", "emmc_cmd"};

static const char * const rtd1619b_pmic_groups[] = { "spi_ce_n" };
static const char * const rtd1619b_spdif_groups[] = { "gpio_50" };
static const char * const rtd1619b_spdif_coaxial_groups[] = { "gpio_1" };
static const char * const rtd1619b_spdif_optical_loc0_groups[] = { "gpio_6" };
static const char * const rtd1619b_spdif_optical_loc1_groups[] = { "gpio_21" };

static const char * const rtd1619b_emmc_spi_groups[] = {
	"gpio_1", "gpio_2", "gpio_3", "gpio_4", "gpio_5", "gpio_6" };
static const char * const rtd1619b_sc1_groups[] = {
	"gpio_2", "gpio_3", "gpio_4", "gpio_5" };
static const char * const rtd1619b_uart0_groups[] = { "ur0_rx", "ur0_tx" };
static const char * const rtd1619b_uart1_groups[] = {
	"gpio_8", "gpio_9", "gpio_10", "gpio_11" };
static const char * const rtd1619b_uart2_loc0_groups[] = {
	"gpio_18", "gpio_19", "gpio_20", "gpio_31", "ur2_loc" };
static const char * const rtd1619b_uart2_loc1_groups[] = {
	"gpio_25", "gpio_26", "gpio_27", "gpio_28", "ur2_loc" };
static const char * const rtd1619b_gspi_loc1_groups[] = {
	"gpio_8", "gpio_9", "gpio_10", "gpio_11", "gspi_loc" };
static const char * const rtd1619b_iso_gspi_loc1_groups[] = {
	"gpio_8", "gpio_9", "gpio_10", "gpio_11", "iso_gspi_loc" };
static const char * const rtd1619b_i2c0_groups[] = { "gpio_12", "gpio_13" };
static const char * const rtd1619b_i2c1_groups[] = { "gpio_16", "gpio_17" };
static const char * const rtd1619b_i2c3_groups[] = { "gpio_63", "gpio_64" };
static const char * const rtd1619b_i2c4_groups[] = { "gpio_34", "gpio_35" };
static const char * const rtd1619b_i2c5_groups[] = { "gpio_29", "gpio_46" };
static const char * const rtd1619b_pwm0_groups[] = { "gpio_20", "gpio_26" };
static const char * const rtd1619b_pwm1_groups[] = { "gpio_21", "gpio_27" };
static const char * const rtd1619b_pwm2_groups[] = { "gpio_22", "gpio_28" };
static const char * const rtd1619b_pwm3_groups[] = { "gpio_23", "gpio_47" };
static const char * const rtd1619b_etn_led_groups[] = { "gpio_14", "gpio_15", "gpio_23" };
static const char * const rtd1619b_etn_phy_groups[] = { "gpio_14", "gpio_15" };
static const char * const rtd1619b_etn_clk_groups[] = { "gpio_14" };
static const char * const rtd1619b_sc0_groups[] = {
	"gpio_18", "gpio_19", "gpio_20", "gpio_31" };
static const char * const rtd1619b_vfd_groups[] = {
	"gpio_26", "gpio_27", "gpio_28" };

static const char * const rtd1619b_gspi_loc0_groups[] = {
	"gpio_18", "gpio_19", "gpio_20", "gpio_31", "gspi_loc" };
static const char * const rtd1619b_iso_gspi_loc0_groups[] = {
	"gpio_18", "gpio_19", "gpio_20", "gpio_31", "iso_gspi_loc" };
static const char * const rtd1619b_pcie1_groups[] = { "gpio_25" };
static const char * const rtd1619b_pcie2_groups[] = { "gpio_52" };
static const char * const rtd1619b_sd_groups[] = {
	"gpio_32", "gpio_33", "gpio_34", "gpio_35", "hif_data",
	"hif_en", "hif_rdy", "hif_clk" };
static const char * const rtd1619b_sdio_loc0_groups[] = {
	"gpio_32", "gpio_33", "hif_data", "hif_en",
	"hif_rdy", "hif_clk", "sdio_loc" };
static const char * const rtd1619b_sdio_loc1_groups[] = {
	"gpio_40", "gpio_41", "gpio_42", "gpio_43", "gpio_44",
	"gpio_45", "sdio_loc" };
static const char * const rtd1619b_hi_groups[] = {
	"hif_data", "hif_en", "hif_rdy", "hif_clk" };
static const char * const rtd1619b_hi_m_groups[] = {
	"hif_data", "hif_en", "hif_rdy", "hif_clk" };
static const char * const rtd1619b_dc_fan_groups[] = { "gpio_47" };
static const char * const rtd1619b_pll_test_loc0_groups[] = { "gpio_52", "gpio_53" };
static const char * const rtd1619b_pll_test_loc1_groups[] = { "gpio_48", "gpio_49" };
static const char * const rtd1619b_tdm_ai_loc0_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60" };
static const char * const rtd1619b_tdm_ai_loc1_groups[] = {
	"hif_data", "hif_en", "hif_rdy", "hif_clk" };
static const char * const rtd1619b_dmic_loc0_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60", "gpio_61",
	"gpio_62", "gpio_63", "gpio_64", "dmic_loc"};
static const char * const rtd1619b_dmic_loc1_groups[] = {
	"gpio_32", "gpio_33", "gpio_34", "gpio_35", "hif_data",
	"hif_en", "hif_rdy", "hif_clk", "dmic_loc" };
static const char * const rtd1619b_ai_loc0_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60", "gpio_61",
	"gpio_62", "gpio_63" };
static const char * const rtd1619b_ai_loc1_groups[] = {
	"gpio_32", "gpio_33", "gpio_34", "hif_data",
	"hif_en", "hif_rdy", "hif_clk"};
static const char * const rtd1619b_tp0_groups[] = {
	"gpio_66", "gpio_67", "gpio_68", "gpio_69",
	"gpio_70", "gpio_71", "gpio_72", "gpio_73", "gpio_74",
	"gpio_75", "gpio_76" };
static const char * const rtd1619b_tp1_groups[] = {
	"gpio_69", "gpio_70", "gpio_71", "gpio_72" };
static const char * const rtd1619b_ao_groups[] = {
	"gpio_66", "gpio_67", "gpio_68", "gpio_69",
	"gpio_70", "gpio_71", "gpio_72" };
static const char * const rtd1619b_uart2_disable_groups[] = { "ur2_loc" };
static const char * const rtd1619b_gspi_disable_groups[] = { "gspi_loc" };
static const char * const rtd1619b_sdio_disable_groups[] = { "sdio_loc" };
static const char * const rtd1619b_hi_loc_disable_groups[] = { "hi_loc" };
static const char * const rtd1619b_hi_loc0_groups[] = { "hi_loc" };
static const char * const rtd1619b_hi_width_disable_groups[] = { "hi_width" };
static const char * const rtd1619b_hi_width_1bit_groups[] = { "hi_width" };

static const char * const rtd1619b_vtc_i2si_loc0_groups[] = {
	"gpio_32", "hif_data", "hif_en", "hif_rdy", "hif_clk" };
static const char * const rtd1619b_vtc_tdm_loc0_groups[] = {
	"hif_data", "hif_en", "hif_rdy", "hif_clk" };
static const char * const rtd1619b_vtc_dmic_loc0_groups[] = {
	"hif_data", "hif_en", "hif_rdy", "hif_clk" };
static const char * const rtd1619b_vtc_i2si_loc1_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60", "gpio_61" };
static const char * const rtd1619b_vtc_tdm_loc1_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60" };
static const char * const rtd1619b_vtc_dmic_loc1_groups[] = {
	"gpio_57", "gpio_58", "gpio_59", "gpio_60" };
static const char * const rtd1619b_vtc_i2so_groups[] = {
	"gpio_66", "gpio_67", "gpio_68", "gpio_69" };
static const char * const rtd1619b_ve3_ejtag_loc0_groups[] = {
	"gpio_2", "gpio_3", "gpio_4",
	"gpio_5", "gpio_6", "ejtag_ve3_loc" };
static const char * const rtd1619b_aucpu0_ejtag_loc0_groups[] = {
	"gpio_2", "gpio_3", "gpio_4",
	"gpio_5", "gpio_6", "ejtag_aucpu0_loc" };
static const char * const rtd1619b_aucpu1_ejtag_loc0_groups[] = {
	"gpio_2", "gpio_3", "gpio_4",
	"gpio_5", "gpio_6", "ejtag_aucpu1_loc" };
static const char * const rtd1619b_ve3_ejtag_loc1_groups[] = {
	"gpio_32", "gpio_33", "hif_data", "hif_en",
	"hif_clk", "ejtag_ve3_loc" };
static const char * const rtd1619b_aucpu0_ejtag_loc1_groups[] = {
	"gpio_32", "gpio_33", "hif_data", "hif_en",
	"hif_clk", "ejtag_aucpu0_loc" };
static const char * const rtd1619b_aucpu1_ejtag_loc1_groups[] = {
	"gpio_32", "gpio_33", "hif_data", "hif_en",
	"hif_clk", "ejtag_aucpu1_loc" };

static const char * const rtd1619b_ve3_ejtag_loc2_groups[] = {
	"gpio_71", "gpio_72", "gpio_73", "gpio_74",
	"gpio_75", "gpio_76", "ejtag_ve3_loc" };
static const char * const rtd1619b_aucpu0_ejtag_loc2_groups[] = {
	"gpio_71", "gpio_72", "gpio_73", "gpio_74",
	"gpio_75", "gpio_76", "ejtag_aucpu0_loc" };
static const char * const rtd1619b_aucpu1_ejtag_loc2_groups[] = {
	"gpio_71", "gpio_72", "gpio_73", "gpio_74",
	"gpio_75", "gpio_76", "ejtag_aucpu1_loc" };

static const char * const rtd1619b_scpu_ejtag_loc0_groups[] = {
	"gpio_2", "gpio_3", "gpio_4",
	"gpio_5", "gpio_6", "ejtag_scpu_loc" };
static const char * const rtd1619b_acpu_ejtag_loc0_groups[] = {
	"gpio_2", "gpio_3", "gpio_4",
	"gpio_5", "gpio_6", "ejtag_acpu_loc" };
static const char * const rtd1619b_vcpu_ejtag_loc0_groups[] = {
	"gpio_2", "gpio_3", "gpio_4",
	"gpio_5", "gpio_6", "ejtag_vcpu_loc" };
static const char * const rtd1619b_scpu_ejtag_loc1_groups[] = {
	"gpio_32", "gpio_33", "hif_data", "hif_en",
	"hif_clk", "ejtag_scpu_loc" };
static const char * const rtd1619b_acpu_ejtag_loc1_groups[] = {
	"gpio_32", "gpio_33", "hif_data",  "hif_en",
	"hif_clk", "ejtag_acpu_loc" };
static const char * const rtd1619b_vcpu_ejtag_loc1_groups[] = {
	"gpio_32", "gpio_33", "hif_data", "hif_en",
	"hif_clk", "ejtag_vcpu_loc" };

static const char * const rtd1619b_scpu_ejtag_loc2_groups[] = {
	"gpio_71", "gpio_72", "gpio_73", "gpio_74",
	"gpio_75", "gpio_76", "ejtag_scpu_loc" };
static const char * const rtd1619b_acpu_ejtag_loc2_groups[] = {
	"gpio_71", "gpio_72", "gpio_73", "gpio_74",
	"gpio_75", "gpio_76", "ejtag_acpu_loc" };
static const char * const rtd1619b_vcpu_ejtag_loc2_groups[] = {
	"gpio_71", "gpio_72", "gpio_73", "gpio_74",
	"gpio_75", "gpio_76", "ejtag_vcpu_loc"};
static const char * const rtd1619b_ve3_ejtag_disable_groups[] = { "ejtag_ve3_loc" };
static const char * const rtd1619b_aucpu0_ejtag_disable_groups[] = { "ejtag_aucpu0_loc" };
static const char * const rtd1619b_aucpu1_ejtag_disable_groups[] = { "ejtag_aucpu1_loc" };

static const char * const rtd1619b_acpu_ejtag_disable_groups[] = { "ejtag_acpu_loc" };
static const char * const rtd1619b_vcpu_ejtag_disable_groups[] = { "ejtag_vcpu_loc" };
static const char * const rtd1619b_scpu_ejtag_disable_groups[] = { "ejtag_scpu_loc" };
static const char * const rtd1619b_sf_disable_groups[] = { "sf_en" };
static const char * const rtd1619b_sf_enable_groups[] = { "sf_en" };
static const char * const rtd1619b_iso_gspi_disable_groups[] = { "iso_gspi_loc" };
static const char * const rtd1619b_arm_trace_debug_disable_groups[] = { "arm_trace_dbg_en" };
static const char * const rtd1619b_arm_trace_debug_enable_groups[] = { "arm_trace_dbg_en" };
static const char * const rtd1619b_pwm_normal_groups[] = {
	"pwm_01_open_drain_en_loc0", "pwm_23_open_drain_en_loc0",
	"pwm_01_open_drain_en_loc1", "pwm_23_open_drain_en_loc1" };
static const char * const rtd1619b_pwm_open_drain_groups[] = {
	"pwm_01_open_drain_en_loc0", "pwm_23_open_drain_en_loc0",
	"pwm_01_open_drain_en_loc1", "pwm_23_open_drain_en_loc1" };
static const char * const rtd1619b_standby_dbg_groups[] = {
	"gpio_2", "gpio_3", "ir_rx" };

static const char * const rtd1619b_usb_cc1_groups[] = { "usb_cc1" };
static const char * const rtd1619b_usb_cc2_groups[] = { "usb_cc2" };
static const char * const rtd1619b_ir_rx_groups[] = { "ir_rx" };
static const char * const rtd1619b_test_loop_dis_groups[] = { "gpio_50" };

#define RTD1619B_FUNC(_name) \
	{ \
		.name = # _name, \
		.groups = rtd1619b_ ## _name ## _groups, \
		.num_groups = ARRAY_SIZE(rtd1619b_ ## _name ## _groups), \
	}

static const struct rtd_pin_func_desc rtd1619b_pin_functions[] = {
	RTD1619B_FUNC(gpio),
	RTD1619B_FUNC(nf),
	RTD1619B_FUNC(nf_spi),
	RTD1619B_FUNC(spi),
	RTD1619B_FUNC(pmic),
	RTD1619B_FUNC(spdif),
	RTD1619B_FUNC(spdif_coaxial),
	RTD1619B_FUNC(spdif_optical_loc0),
	RTD1619B_FUNC(spdif_optical_loc1),
	RTD1619B_FUNC(emmc_spi),
	RTD1619B_FUNC(emmc),
	RTD1619B_FUNC(sc1),
	RTD1619B_FUNC(uart0),
	RTD1619B_FUNC(uart1),
	RTD1619B_FUNC(uart2_loc0),
	RTD1619B_FUNC(uart2_loc1),
	RTD1619B_FUNC(gspi_loc1),
	RTD1619B_FUNC(iso_gspi_loc1),
	RTD1619B_FUNC(i2c0),
	RTD1619B_FUNC(i2c1),
	RTD1619B_FUNC(i2c3),
	RTD1619B_FUNC(i2c4),
	RTD1619B_FUNC(i2c5),
	RTD1619B_FUNC(pwm0),
	RTD1619B_FUNC(pwm1),
	RTD1619B_FUNC(pwm2),
	RTD1619B_FUNC(pwm3),
	RTD1619B_FUNC(etn_led),
	RTD1619B_FUNC(etn_phy),
	RTD1619B_FUNC(etn_clk),
	RTD1619B_FUNC(sc0),
	RTD1619B_FUNC(vfd),
	RTD1619B_FUNC(gspi_loc0),
	RTD1619B_FUNC(iso_gspi_loc0),
	RTD1619B_FUNC(pcie1),
	RTD1619B_FUNC(pcie2),
	RTD1619B_FUNC(sd),
	RTD1619B_FUNC(sdio_loc0),
	RTD1619B_FUNC(sdio_loc1),
	RTD1619B_FUNC(hi),
	RTD1619B_FUNC(hi_m),
	RTD1619B_FUNC(dc_fan),
	RTD1619B_FUNC(pll_test_loc0),
	RTD1619B_FUNC(pll_test_loc1),
	RTD1619B_FUNC(usb_cc1),
	RTD1619B_FUNC(usb_cc2),
	RTD1619B_FUNC(ir_rx),
	RTD1619B_FUNC(tdm_ai_loc0),
	RTD1619B_FUNC(tdm_ai_loc1),
	RTD1619B_FUNC(dmic_loc0),
	RTD1619B_FUNC(dmic_loc1),
	RTD1619B_FUNC(ai_loc0),
	RTD1619B_FUNC(ai_loc1),
	RTD1619B_FUNC(tp0),
	RTD1619B_FUNC(tp1),
	RTD1619B_FUNC(ao),
	RTD1619B_FUNC(uart2_disable),
	RTD1619B_FUNC(gspi_disable),
	RTD1619B_FUNC(sdio_disable),
	RTD1619B_FUNC(hi_loc_disable),
	RTD1619B_FUNC(hi_loc0),
	RTD1619B_FUNC(hi_width_disable),
	RTD1619B_FUNC(hi_width_1bit),
	RTD1619B_FUNC(vtc_i2si_loc0),
	RTD1619B_FUNC(vtc_tdm_loc0),
	RTD1619B_FUNC(vtc_dmic_loc0),
	RTD1619B_FUNC(vtc_i2si_loc1),
	RTD1619B_FUNC(vtc_tdm_loc1),
	RTD1619B_FUNC(vtc_dmic_loc1),
	RTD1619B_FUNC(vtc_i2so),
	RTD1619B_FUNC(ve3_ejtag_loc0),
	RTD1619B_FUNC(aucpu0_ejtag_loc0),
	RTD1619B_FUNC(aucpu1_ejtag_loc0),
	RTD1619B_FUNC(ve3_ejtag_loc1),
	RTD1619B_FUNC(aucpu0_ejtag_loc1),
	RTD1619B_FUNC(aucpu1_ejtag_loc1),
	RTD1619B_FUNC(ve3_ejtag_loc2),
	RTD1619B_FUNC(aucpu0_ejtag_loc2),
	RTD1619B_FUNC(aucpu1_ejtag_loc2),
	RTD1619B_FUNC(scpu_ejtag_loc0),
	RTD1619B_FUNC(acpu_ejtag_loc0),
	RTD1619B_FUNC(vcpu_ejtag_loc0),
	RTD1619B_FUNC(scpu_ejtag_loc1),
	RTD1619B_FUNC(acpu_ejtag_loc1),
	RTD1619B_FUNC(vcpu_ejtag_loc1),
	RTD1619B_FUNC(scpu_ejtag_loc2),
	RTD1619B_FUNC(acpu_ejtag_loc2),
	RTD1619B_FUNC(vcpu_ejtag_loc2),
	RTD1619B_FUNC(ve3_ejtag_disable),
	RTD1619B_FUNC(aucpu0_ejtag_disable),
	RTD1619B_FUNC(aucpu1_ejtag_disable),
	RTD1619B_FUNC(acpu_ejtag_disable),
	RTD1619B_FUNC(vcpu_ejtag_disable),
	RTD1619B_FUNC(scpu_ejtag_disable),
	RTD1619B_FUNC(iso_gspi_disable),
	RTD1619B_FUNC(sf_disable),
	RTD1619B_FUNC(sf_enable),
	RTD1619B_FUNC(arm_trace_debug_disable),
	RTD1619B_FUNC(arm_trace_debug_enable),
	RTD1619B_FUNC(pwm_normal),
	RTD1619B_FUNC(pwm_open_drain),
	RTD1619B_FUNC(standby_dbg),
	RTD1619B_FUNC(test_loop_dis),
};

#undef RTD1619B_FUNC

static const struct rtd_pin_desc rtd1619b_iso_muxes[] = {
	[RTD1619B_ISO_EMMC_RST_N] = RTK_PIN_MUX(emmc_rst_n, 0x0, GENMASK(1, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "emmc")),
	[RTD1619B_ISO_EMMC_DD_SB] = RTK_PIN_MUX(emmc_dd_sb, 0x0, GENMASK(3, 2),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 2), "emmc")),
	[RTD1619B_ISO_EMMC_CLK] = RTK_PIN_MUX(emmc_clk, 0x0, GENMASK(5, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "emmc")),
	[RTD1619B_ISO_EMMC_CMD] = RTK_PIN_MUX(emmc_cmd, 0x0, GENMASK(7, 6),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 6), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 6), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 6), "emmc")),
	[RTD1619B_ISO_EMMC_DATA_0] = RTK_PIN_MUX(emmc_data_0, 0x0, GENMASK(9, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 8), "nf_spi")),
	[RTD1619B_ISO_EMMC_DATA_1] = RTK_PIN_MUX(emmc_data_1, 0x0, GENMASK(11, 10),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 10), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 10), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 10), "nf_spi")),
	[RTD1619B_ISO_EMMC_DATA_2] = RTK_PIN_MUX(emmc_data_2, 0x0, GENMASK(13, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 12), "nf_spi")),
	[RTD1619B_ISO_EMMC_DATA_3] = RTK_PIN_MUX(emmc_data_3, 0x0, GENMASK(15, 14),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 14), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 14), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 14), "nf_spi")),
	[RTD1619B_ISO_EMMC_DATA_4] = RTK_PIN_MUX(emmc_data_4, 0x0, GENMASK(17, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 16), "nf_spi")),
	[RTD1619B_ISO_EMMC_DATA_5] = RTK_PIN_MUX(emmc_data_5, 0x0, GENMASK(19, 18),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 18), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 18), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 18), "nf_spi")),
	[RTD1619B_ISO_EMMC_DATA_6] = RTK_PIN_MUX(emmc_data_6, 0x0, GENMASK(21, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "emmc")),
	[RTD1619B_ISO_EMMC_DATA_7] = RTK_PIN_MUX(emmc_data_7, 0x0, GENMASK(23, 22),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 22), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 22), "emmc")),
	[RTD1619B_ISO_SPI_CE_N] = RTK_PIN_MUX(spi_ce_n, 0x0, GENMASK(25, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 24), "spi"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 24), "pmic")),
	[RTD1619B_ISO_SPI_SCK] = RTK_PIN_MUX(spi_sck, 0x0, GENMASK(27, 26),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 26), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 26), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 26), "spi")),
	[RTD1619B_ISO_SPI_SO] = RTK_PIN_MUX(spi_so, 0x0, GENMASK(29, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 28), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 28), "spi")),
	[RTD1619B_ISO_SPI_SI] = RTK_PIN_MUX(spi_si, 0x0, GENMASK(31, 30),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 30), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 30), "nf"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 30), "spi")),

	[RTD1619B_ISO_GPIO_0] = RTK_PIN_MUX(gpio_0, 0x4, GENMASK(0, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio")),
	[RTD1619B_ISO_GPIO_1] = RTK_PIN_MUX(gpio_1, 0x4, GENMASK(3, 1),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 1), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 1), "emmc_spi"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 1), "spdif_coaxial")),
	[RTD1619B_ISO_GPIO_2] = RTK_PIN_MUX(gpio_2, 0x4, GENMASK(7, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "standby_dbg"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 4), "emmc_spi"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 4), "sc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 4), "scpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 4), "acpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 4), "vcpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 4), "ve3_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 4), "aucpu0_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x10, 4), "aucpu1_ejtag_loc0")),
	[RTD1619B_ISO_GPIO_3] = RTK_PIN_MUX(gpio_3, 0x4, GENMASK(11, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "standby_dbg"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 8), "emmc_spi"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 8), "sc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 8), "scpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 8), "acpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 8), "vcpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 8), "ve3_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 8), "aucpu0_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x10, 8), "aucpu1_ejtag_loc0")),
	[RTD1619B_ISO_GPIO_4] = RTK_PIN_MUX(gpio_4, 0x4, GENMASK(15, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 12), "emmc_spi"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 12), "sc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 12), "scpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 12), "acpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 12), "vcpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 12), "ve3_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 12), "aucpu0_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x10, 12), "aucpu1_ejtag_loc0")),
	[RTD1619B_ISO_GPIO_5] = RTK_PIN_MUX(gpio_5, 0x4, GENMASK(19, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 16), "emmc_spi"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 16), "sc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 16), "scpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 16), "acpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 16), "vcpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 16), "ve3_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 16), "aucpu0_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x10, 16), "aucpu1_ejtag_loc0")),
	[RTD1619B_ISO_GPIO_6] = RTK_PIN_MUX(gpio_6, 0x4, GENMASK(23, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 20), "emmc_spi"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 20), "spdif_optical_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 20), "scpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 20), "acpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 20), "vcpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 20), "ve3_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 20), "aucpu0_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x10, 20), "aucpu1_ejtag_loc0")),
	[RTD1619B_ISO_GPIO_7] = RTK_PIN_MUX(gpio_7, 0x4, GENMASK(24, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio")),
	[RTD1619B_ISO_GPIO_8] = RTK_PIN_MUX(gpio_8, 0x4, GENMASK(27, 25),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 25), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 25), "uart1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 25), "gspi_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 25), "iso_gspi_loc1")),
	[RTD1619B_ISO_GPIO_9] = RTK_PIN_MUX(gpio_9, 0x4, GENMASK(30, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 28), "uart1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 28), "gspi_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 28), "iso_gspi_loc1")),
	[RTD1619B_ISO_GPIO_10] = RTK_PIN_MUX(gpio_10, 0x8, GENMASK(2, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "uart1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 0), "gspi_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 0), "iso_gspi_loc1")),
	[RTD1619B_ISO_GPIO_11] = RTK_PIN_MUX(gpio_11, 0x8, GENMASK(5, 3),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 3), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 3), "uart1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 3), "gspi_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 3), "iso_gspi_loc1")),

	[RTD1619B_ISO_GPIO_12] = RTK_PIN_MUX(gpio_12, 0x8, GENMASK(6, 6),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 6), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 6), "i2c0")),
	[RTD1619B_ISO_GPIO_13] = RTK_PIN_MUX(gpio_13, 0x8, GENMASK(7, 7),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 7), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 7), "i2c0")),
	[RTD1619B_ISO_GPIO_14] = RTK_PIN_MUX(gpio_14, 0x8, GENMASK(10, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "etn_led"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 8), "etn_phy"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 8), "etn_clk")),
	[RTD1619B_ISO_GPIO_15] = RTK_PIN_MUX(gpio_15, 0x8, GENMASK(12, 11),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 11), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 11), "etn_led"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 11), "etn_phy")),
	[RTD1619B_ISO_GPIO_16] = RTK_PIN_MUX(gpio_16, 0x8, GENMASK(13, 13),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 13), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 13), "i2c1")),
	[RTD1619B_ISO_GPIO_17] = RTK_PIN_MUX(gpio_17, 0x8, GENMASK(14, 14),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 14), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 14), "i2c1")),
	[RTD1619B_ISO_GPIO_18] = RTK_PIN_MUX(gpio_18, 0x8, GENMASK(17, 15),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 15), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 15), "uart2_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 15), "sc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 15), "gspi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 15), "iso_gspi_loc0")),
	[RTD1619B_ISO_GPIO_19] = RTK_PIN_MUX(gpio_19, 0x8, GENMASK(20, 18),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 18), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 18), "uart2_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 18), "sc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 18), "gspi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 18), "iso_gspi_loc0")),
	[RTD1619B_ISO_GPIO_20] = RTK_PIN_MUX(gpio_20, 0x8, GENMASK(23, 21),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 21), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 21), "uart2_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 21), "pwm0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 21), "gspi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 21), "sc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 21), "iso_gspi_loc0")),
	[RTD1619B_ISO_GPIO_21] = RTK_PIN_MUX(gpio_21, 0x8, GENMASK(26, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 24), "pwm1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 24), "spdif_optical_loc1")),
	[RTD1619B_ISO_GPIO_22] = RTK_PIN_MUX(gpio_22, 0x8, GENMASK(28, 27),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 27), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 27), "pwm2")),
	[RTD1619B_ISO_GPIO_23] = RTK_PIN_MUX(gpio_23, 0x8, GENMASK(30, 29),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 29), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 29), "etn_led"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 29), "pwm3")),
	[RTD1619B_ISO_USB_CC2] = RTK_PIN_MUX(usb_cc2, 0x8, GENMASK(31, 31),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 31), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 31), "usb_cc2")),
	[RTD1619B_ISO_GPIO_25] = RTK_PIN_MUX(gpio_25, 0xc, GENMASK(1, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "uart2_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "pcie1")),
	[RTD1619B_ISO_GPIO_26] = RTK_PIN_MUX(gpio_26, 0xc, GENMASK(3, 2),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 2), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 2), "uart2_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 2), "vfd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 2), "pwm0")),
	[RTD1619B_ISO_GPIO_27] = RTK_PIN_MUX(gpio_27, 0xc, GENMASK(5, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "uart2_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "vfd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 4), "pwm1")),

	[RTD1619B_ISO_GPIO_28] = RTK_PIN_MUX(gpio_28, 0xc, GENMASK(7, 6),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 6), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 6), "uart2_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 6), "vfd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 6), "pwm2")),
	[RTD1619B_ISO_GPIO_29] = RTK_PIN_MUX(gpio_29, 0xc, GENMASK(8, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "i2c5")),
	[RTD1619B_ISO_GPIO_30] = RTK_PIN_MUX(gpio_30, 0xc, GENMASK(9, 9),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 9), "gpio")),
	[RTD1619B_ISO_GPIO_31] = RTK_PIN_MUX(gpio_31, 0xc, GENMASK(12, 10),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 10), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 10), "uart2_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 10), "sc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 10), "gspi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 10), "iso_gspi_loc0")),
	[RTD1619B_ISO_GPIO_32] = RTK_PIN_MUX(gpio_32, 0xc, GENMASK(17, 13),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 13), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 13), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 13), "sdio_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 13), "dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 13), "ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 13), "scpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 13), "acpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 13), "vcpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x11, 13), "vtc_i2si_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x14, 13), "ve3_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x15, 13), "aucpu0_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x16, 13), "aucpu1_ejtag_loc1")),
	[RTD1619B_ISO_GPIO_33] = RTK_PIN_MUX(gpio_33, 0xc, GENMASK(22, 18),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 18), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 18), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 18), "sdio_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 18), "dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 18), "ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 18), "scpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 18), "acpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 18), "vcpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x14, 18), "ve3_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x15, 18), "aucpu0_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x16, 18), "aucpu1_ejtag_loc1")),
	[RTD1619B_ISO_GPIO_34] = RTK_PIN_MUX(gpio_34, 0xc, GENMASK(25, 23),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 23), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 23), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 23), "dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 23), "ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 23), "i2c4")),
	[RTD1619B_ISO_GPIO_35] = RTK_PIN_MUX(gpio_35, 0xc, GENMASK(28, 26),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 26), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 26), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 26), "dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 26), "i2c4")),
	[RTD1619B_ISO_HIF_DATA] = RTK_PIN_MUX(hif_data, 0x10, GENMASK(4, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "sdio_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 0), "dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 0), "tdm_ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 0), "scpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 0), "acpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 0), "vcpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 0), "ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 0), "hi"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x10, 0), "hi_m"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x11, 0), "vtc_i2si_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x12, 0), "vtc_tdm_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x13, 0), "vtc_dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x14, 0), "ve3_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x15, 0), "aucpu0_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x16, 0), "aucpu1_ejtag_loc1")),
	[RTD1619B_ISO_HIF_EN] = RTK_PIN_MUX(hif_en, 0x10, GENMASK(9, 5),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 5), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 5), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 5), "sdio_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 5), "dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 5), "tdm_ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 5), "scpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 5), "acpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 5), "vcpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 5), "ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 5), "hi"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x10, 5), "hi_m"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x11, 5), "vtc_i2si_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x12, 5), "vtc_tdm_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x13, 5), "vtc_dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x14, 5), "ve3_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x15, 5), "aucpu0_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x16, 5), "aucpu1_ejtag_loc1")),
	[RTD1619B_ISO_HIF_RDY] = RTK_PIN_MUX(hif_rdy, 0x10, GENMASK(13, 10),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 10), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 10), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 10), "sdio_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 10), "dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 10), "tdm_ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 10), "ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 10), "hi"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x10, 10), "hi_m"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x11, 10), "vtc_i2si_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x12, 10), "vtc_tdm_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x13, 10), "vtc_dmic_loc0")),

	[RTD1619B_ISO_HIF_CLK] = RTK_PIN_MUX(hif_clk, 0x10, GENMASK(18, 14),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 14), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 14), "sd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 14), "sdio_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 14), "dmic_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 14), "tdm_ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 14), "scpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 14), "acpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 14), "vcpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 14), "ai_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 14), "hi"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x10, 14), "hi_m"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x11, 14), "vtc_i2si_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x12, 14), "vtc_tdm_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x13, 14), "vtc_dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x14, 14), "ve3_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x15, 14), "aucpu0_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x16, 14), "aucpu1_ejtag_loc1")),
	[RTD1619B_ISO_GPIO_40] = RTK_PIN_MUX(gpio_40, 0x10, GENMASK(20, 19),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 19), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 19), "sdio_loc1")),
	[RTD1619B_ISO_GPIO_41] = RTK_PIN_MUX(gpio_41, 0x10, GENMASK(22, 21),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 21), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 21), "sdio_loc1")),
	[RTD1619B_ISO_GPIO_42] = RTK_PIN_MUX(gpio_42, 0x10, GENMASK(24, 23),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 23), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 23), "sdio_loc1")),
	[RTD1619B_ISO_GPIO_43] = RTK_PIN_MUX(gpio_43, 0x10, GENMASK(26, 25),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 25), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 25), "sdio_loc1")),
	[RTD1619B_ISO_GPIO_44] = RTK_PIN_MUX(gpio_44, 0x10, GENMASK(28, 27),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 27), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 27), "sdio_loc1")),
	[RTD1619B_ISO_GPIO_45] = RTK_PIN_MUX(gpio_45, 0x10, GENMASK(30, 29),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 29), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 29), "sdio_loc1")),
	[RTD1619B_ISO_GPIO_46] = RTK_PIN_MUX(gpio_46, 0x10, GENMASK(31, 31),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 31), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 31), "i2c5")),
	[RTD1619B_ISO_GPIO_47] = RTK_PIN_MUX(gpio_47, 0x14, GENMASK(1, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "dc_fan"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 0), "pwm3")),
	[RTD1619B_ISO_GPIO_48] = RTK_PIN_MUX(gpio_48, 0x14, GENMASK(2, 2),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 2), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 2), "pll_test_loc1")),
	[RTD1619B_ISO_GPIO_49] = RTK_PIN_MUX(gpio_49, 0x14, GENMASK(3, 3),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 3), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 3), "pll_test_loc1")),
	[RTD1619B_ISO_GPIO_50] = RTK_PIN_MUX(gpio_50, 0x14, GENMASK(5, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "spdif"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "test_loop_dis")),
	[RTD1619B_ISO_USB_CC1] = RTK_PIN_MUX(usb_cc1, 0x14, GENMASK(6, 6),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 6), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 6), "usb_cc1")),
	[RTD1619B_ISO_GPIO_52] = RTK_PIN_MUX(gpio_52, 0x14, GENMASK(8, 7),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 7), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 7), "pll_test_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 7), "pcie2")),
	[RTD1619B_ISO_GPIO_53] = RTK_PIN_MUX(gpio_53, 0x14, GENMASK(9, 9),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 9), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 9), "pll_test_loc0")),
	[RTD1619B_ISO_IR_RX] = RTK_PIN_MUX(ir_rx, 0x14, GENMASK(11, 10),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 10), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 10), "ir_rx"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 10), "standby_dbg")),
	[RTD1619B_ISO_UR0_RX] = RTK_PIN_MUX(ur0_rx, 0x14, GENMASK(12, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "uart0")),

	[RTD1619B_ISO_UR0_TX] = RTK_PIN_MUX(ur0_tx, 0x14, GENMASK(13, 13),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 13), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 13), "uart0")),
	[RTD1619B_ISO_GPIO_57] = RTK_PIN_MUX(gpio_57, 0x14, GENMASK(17, 14),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 14), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 14), "tdm_ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 14), "ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 14), "dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x11, 14), "vtc_i2si_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x12, 14), "vtc_tdm_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x13, 14), "vtc_dmic_loc1")),
	[RTD1619B_ISO_GPIO_58] = RTK_PIN_MUX(gpio_58, 0x14, GENMASK(21, 18),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 18), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 18), "tdm_ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 18), "ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 18), "dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x11, 18), "vtc_i2si_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x12, 18), "vtc_tdm_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x13, 18), "vtc_dmic_loc1")),
	[RTD1619B_ISO_GPIO_59] = RTK_PIN_MUX(gpio_59, 0x14, GENMASK(25, 22),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 22), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 22), "tdm_ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 22), "ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 22), "dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x11, 22), "vtc_i2si_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x12, 22), "vtc_tdm_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x13, 22), "vtc_dmic_loc1")),
	[RTD1619B_ISO_GPIO_60] = RTK_PIN_MUX(gpio_60, 0x14, GENMASK(29, 26),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 26), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 26), "tdm_ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 26), "ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 26), "dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x11, 26), "vtc_i2si_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x12, 26), "vtc_tdm_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x13, 26), "vtc_dmic_loc1")),
	[RTD1619B_ISO_GPIO_61] = RTK_PIN_MUX(gpio_61, 0x18, GENMASK(3, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 0), "dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x11, 0), "vtc_i2si_loc1")),
	[RTD1619B_ISO_GPIO_62] = RTK_PIN_MUX(gpio_62, 0x18, GENMASK(5, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 4), "dmic_loc0")),
	[RTD1619B_ISO_GPIO_63] = RTK_PIN_MUX(gpio_63, 0x18, GENMASK(7, 6),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 6), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 6), "ai_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 6), "i2c3"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 6), "dmic_loc0")),
	[RTD1619B_ISO_GPIO_64] = RTK_PIN_MUX(gpio_64, 0x18, GENMASK(9, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "i2c3"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 8), "dmic_loc0")),
	[RTD1619B_ISO_GPIO_65] = RTK_PIN_MUX(gpio_65, 0x18, GENMASK(10, 10),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 10), "gpio")),
	[RTD1619B_ISO_GPIO_66] = RTK_PIN_MUX(gpio_66, 0x18, GENMASK(14, 11),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 11), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 11), "tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 11), "ao"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x11, 11), "vtc_i2so")),
	[RTD1619B_ISO_GPIO_67] = RTK_PIN_MUX(gpio_67, 0x18, GENMASK(18, 15),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 15), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 15), "tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 15), "ao"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x11, 15), "vtc_i2so")),
	[RTD1619B_ISO_GPIO_68] = RTK_PIN_MUX(gpio_68, 0x18, GENMASK(22, 19),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 19), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 19), "tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 19), "ao"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x11, 19), "vtc_i2so")),
	[RTD1619B_ISO_GPIO_69] = RTK_PIN_MUX(gpio_69, 0x18, GENMASK(26, 23),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 23), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 23), "tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 23), "tp1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 23), "ao"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x11, 23), "vtc_i2so")),

	[RTD1619B_ISO_GPIO_70] = RTK_PIN_MUX(gpio_70, 0x18, GENMASK(29, 27),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 27), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 27), "tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 27), "tp1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 27), "ao")),
	[RTD1619B_ISO_GPIO_71] = RTK_PIN_MUX(gpio_71, 0x1c, GENMASK(2, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 0), "tp1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 0), "ao"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 0), "scpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 0), "acpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 0), "vcpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 0), "ve3_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 0), "aucpu0_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x10, 0), "aucpu1_ejtag_loc2")),
	[RTD1619B_ISO_GPIO_72] = RTK_PIN_MUX(gpio_72, 0x1c, GENMASK(6, 3),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 3), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 3), "tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 3), "tp1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 3), "ao"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 3), "scpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 3), "acpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 3), "vcpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 3), "ve3_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 3), "aucpu0_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x10, 3), "aucpu1_ejtag_loc2")),
	[RTD1619B_ISO_GPIO_73] = RTK_PIN_MUX(gpio_73, 0x1c, GENMASK(10, 7),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 7), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 7), "tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 7), "scpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 7), "acpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 7), "vcpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 7), "ve3_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 7), "aucpu0_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x10, 7), "aucpu1_ejtag_loc2")),
	[RTD1619B_ISO_GPIO_74] = RTK_PIN_MUX(gpio_74, 0x1c, GENMASK(14, 11),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 11), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 11), "tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 11), "scpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 11), "acpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 11), "vcpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 11), "ve3_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 11), "aucpu0_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x10, 11), "aucpu1_ejtag_loc2")),
	[RTD1619B_ISO_GPIO_75] = RTK_PIN_MUX(gpio_75, 0x1c, GENMASK(18, 15),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 15), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 15), "tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 15), "scpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 15), "acpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 15), "vcpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 15), "ve3_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 15), "aucpu0_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x10, 15), "aucpu1_ejtag_loc2")),
	[RTD1619B_ISO_GPIO_76] = RTK_PIN_MUX(gpio_76, 0x1c, GENMASK(22, 19),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 19), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 19), "tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x5, 19), "scpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x6, 19), "acpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x7, 19), "vcpu_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x8, 19), "ve3_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x9, 19), "aucpu0_ejtag_loc2"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x10, 19), "aucpu1_ejtag_loc2")),

	[RTD1619B_ISO_UR2_LOC] = RTK_PIN_MUX(ur2_loc, 0x120, GENMASK(1, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "uart2_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "uart2_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "uart2_loc1")),
	[RTD1619B_ISO_GSPI_LOC] = RTK_PIN_MUX(gspi_loc, 0x120, GENMASK(3, 2),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 2), "gspi_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 2), "gspi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 2), "gspi_loc1")),
	[RTD1619B_ISO_SDIO_LOC] = RTK_PIN_MUX(sdio_loc, 0x120, GENMASK(5, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "sdio_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "sdio_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "sdio_loc1")),
	[RTD1619B_ISO_HI_LOC] = RTK_PIN_MUX(hi_loc, 0x120, GENMASK(7, 6),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 6), "hi_loc_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 6), "hi_loc0")),
	[RTD1619B_ISO_HI_WIDTH] = RTK_PIN_MUX(hi_width, 0x120, GENMASK(9, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "hi_width_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "hi_width_1bit")),
	[RTD1619B_ISO_SF_EN] = RTK_PIN_MUX(sf_en, 0x120, GENMASK(11, 11),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 11), "sf_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 11), "sf_enable")),
	[RTD1619B_ISO_ARM_TRACE_DBG_EN] = RTK_PIN_MUX(arm_trace_dbg_en, 0x120, GENMASK(12, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "arm_trace_debug_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "arm_trace_debug_enable")),
	[RTD1619B_ISO_PWM_01_OPEN_DRAIN_EN_LOC0] = RTK_PIN_MUX(pwm_01_open_drain_en_loc0, 0x120,
		GENMASK(13, 13),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 13), "pwm_normal"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 13), "pwm_open_drain")),
	[RTD1619B_ISO_PWM_23_OPEN_DRAIN_EN_LOC0] = RTK_PIN_MUX(pwm_23_open_drain_en_loc0, 0x120,
		GENMASK(14, 14),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 14), "pwm_normal"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 14), "pwm_open_drain")),
	[RTD1619B_ISO_PWM_01_OPEN_DRAIN_EN_LOC1] = RTK_PIN_MUX(pwm_01_open_drain_en_loc1, 0x120,
		GENMASK(15, 15),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 15), "pwm_normal"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 15), "pwm_open_drain")),
	[RTD1619B_ISO_PWM_23_OPEN_DRAIN_EN_LOC1] = RTK_PIN_MUX(pwm_23_open_drain_en_loc1, 0x120,
		GENMASK(16, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "pwm_normal"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "pwm_open_drain")),
	[RTD1619B_ISO_EJTAG_ACPU_LOC] = RTK_PIN_MUX(ejtag_acpu_loc, 0x120, GENMASK(19, 17),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 17), "acpu_ejtag_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 17), "acpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 17), "acpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 17), "acpu_ejtag_loc2")),
	[RTD1619B_ISO_EJTAG_VCPU_LOC] = RTK_PIN_MUX(ejtag_vcpu_loc, 0x120, GENMASK(22, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "vcpu_ejtag_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "vcpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "vcpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 20), "vcpu_ejtag_loc2")),
	[RTD1619B_ISO_EJTAG_SCPU_LOC] = RTK_PIN_MUX(ejtag_scpu_loc, 0x120, GENMASK(25, 23),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 23), "scpu_ejtag_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 23), "scpu_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 23), "scpu_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 23), "scpu_ejtag_loc2")),
	[RTD1619B_ISO_DMIC_LOC] = RTK_PIN_MUX(dmic_loc, 0x120, GENMASK(27, 26),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 26), "dmic_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 26), "dmic_loc1")),
	[RTD1619B_ISO_ISO_GSPI_LOC] = RTK_PIN_MUX(iso_gspi_loc, 0x120, GENMASK(29, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "iso_gspi_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 28), "iso_gspi_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 28), "iso_gspi_loc1")),
	[RTD1619B_ISO_EJTAG_VE3_LOC] = RTK_PIN_MUX(ejtag_ve3_loc, 0x124, GENMASK(20, 18),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 18), "ve3_ejtag_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 18), "ve3_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 18), "ve3_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 18), "ve3_ejtag_loc2")),
	[RTD1619B_ISO_EJTAG_AUCPU1_LOC] = RTK_PIN_MUX(ejtag_aucpu1_loc, 0x124, GENMASK(23, 21),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 21), "aucpu1_ejtag_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 21), "aucpu1_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 21), "aucpu1_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 21), "aucpu1_ejtag_loc2")),
	[RTD1619B_ISO_EJTAG_AUCPU0_LOC] = RTK_PIN_MUX(ejtag_aucpu0_loc, 0x124, GENMASK(26, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "aucpu0_ejtag_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "aucpu0_ejtag_loc0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 24), "aucpu0_ejtag_loc1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 24), "aucpu0_ejtag_loc2")),
};

static const struct rtd_pin_config_desc rtd1619b_iso_configs[] = {
	[RTD1619B_ISO_GPIO_17] = RTK_PIN_CONFIG(gpio_17, 0x20, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_19] = RTK_PIN_CONFIG(gpio_19, 0x20, 5, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_50] = RTK_PIN_CONFIG(gpio_50, 0x20, 10, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_21] = RTK_PIN_CONFIG(gpio_21, 0x20, 15, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_66] = RTK_PIN_CONFIG(gpio_66, 0x20, 20, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_67] = RTK_PIN_CONFIG(gpio_67, 0x20, 25, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_68] = RTK_PIN_CONFIG(gpio_68, 0x24, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_69] = RTK_PIN_CONFIG(gpio_69, 0x24, 5, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_70] = RTK_PIN_CONFIG(gpio_70, 0x24, 10, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_71] = RTK_PIN_CONFIG(gpio_71, 0x24, 15, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_72] = RTK_PIN_CONFIG(gpio_72, 0x24, 20, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_73] = RTK_PIN_CONFIG(gpio_73, 0x24, 25, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_74] = RTK_PIN_CONFIG(gpio_74, 0x28, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_75] = RTK_PIN_CONFIG(gpio_75, 0x28, 5, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_76] = RTK_PIN_CONFIG(gpio_76, 0x28, 10, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_SPI_SI] = RTK_PIN_CONFIG(spi_si, 0x28, 15, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_SPI_SCK] = RTK_PIN_CONFIG(spi_sck, 0x2c, 0, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_SPI_CE_N] = RTK_PIN_CONFIG(spi_ce_n, 0x2c, 13, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_SPI_SO] = RTK_PIN_CONFIG(spi_so, 0x2c, 26, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_EMMC_CLK] = RTK_PIN_CONFIG(emmc_clk, 0x30, 7, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_EMMC_CMD] = RTK_PIN_CONFIG(emmc_cmd, 0x34, 0, 0, 1, 3, 2, 13, NA),
	[RTD1619B_ISO_EMMC_RST_N] = RTK_PIN_CONFIG(emmc_rst_n, 0x34, 14, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_EMMC_DD_SB] = RTK_PIN_CONFIG(emmc_dd_sb, 0x34, 27, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_EMMC_DATA_5] = RTK_PIN_CONFIG(emmc_data_5, 0x38, 8, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_EMMC_DATA_3] = RTK_PIN_CONFIG(emmc_data_3, 0x3c, 0, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_EMMC_DATA_4] = RTK_PIN_CONFIG(emmc_data_4, 0x3c, 13, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_EMMC_DATA_0] = RTK_PIN_CONFIG(emmc_data_0, 0x3c, 26, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_EMMC_DATA_1] = RTK_PIN_CONFIG(emmc_data_1, 0x40, 7, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_EMMC_DATA_2] = RTK_PIN_CONFIG(emmc_data_2, 0x44, 0, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_EMMC_DATA_7] = RTK_PIN_CONFIG(emmc_data_7, 0x44, 13, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_EMMC_DATA_6] = RTK_PIN_CONFIG(emmc_data_6, 0x44, 26, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_USB_CC1] = RTK_PIN_CONFIG(usb_cc1, 0x48, 7, NA, NA, 0, 1, 9, PADDRI_4_8),
	[RTD1619B_ISO_USB_CC2] = RTK_PIN_CONFIG(usb_cc2, 0x48, 10, NA, NA, 0, 1, 9, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_26] = RTK_PIN_CONFIG(gpio_26, 0x48, 13, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_27] = RTK_PIN_CONFIG(gpio_27, 0x48, 18, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_28] = RTK_PIN_CONFIG(gpio_28, 0x48, 23, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_29] = RTK_PIN_CONFIG(gpio_29, 0x4c, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_46] = RTK_PIN_CONFIG(gpio_46, 0x4c, 5, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_48] = RTK_PIN_CONFIG(gpio_48, 0x4c, 10, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_49] = RTK_PIN_CONFIG(gpio_49, 0x4c, 15, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_53] = RTK_PIN_CONFIG(gpio_53, 0x4c, 20, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_65] = RTK_PIN_CONFIG(gpio_65, 0x4c, 25, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_14] = RTK_PIN_CONFIG(gpio_14, 0x50, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_15] = RTK_PIN_CONFIG(gpio_15, 0x50, 5, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_23] = RTK_PIN_CONFIG(gpio_23, 0x50, 10, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_IR_RX] = RTK_PIN_CONFIG(ir_rx, 0x50, 20, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_57] = RTK_PIN_CONFIG(gpio_57, 0x50, 25, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_58] = RTK_PIN_CONFIG(gpio_58, 0x50, 30, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_59] = RTK_PIN_CONFIG(gpio_59, 0x54, 3, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_60] = RTK_PIN_CONFIG(gpio_60, 0x54, 8, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_61] = RTK_PIN_CONFIG(gpio_61, 0x54, 13, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_62] = RTK_PIN_CONFIG(gpio_62, 0x54, 18, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_63] = RTK_PIN_CONFIG(gpio_63, 0x54, 23, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_64] = RTK_PIN_CONFIG(gpio_64, 0x58, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_UR0_TX] = RTK_PIN_CONFIG(ur0_tx, 0x58, 5, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_UR0_RX] = RTK_PIN_CONFIG(ur0_rx, 0x58, 13, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_12] = RTK_PIN_CONFIG(gpio_12, 0x58, 18, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_13] = RTK_PIN_CONFIG(gpio_13, 0x58, 23, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_30] = RTK_PIN_CONFIG(gpio_30, 0x58, 28, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_7] = RTK_PIN_CONFIG(gpio_7, 0x5c, 1, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_16] = RTK_PIN_CONFIG(gpio_16, 0x5c, 6, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_18] = RTK_PIN_CONFIG(gpio_18, 0x5c, 11, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_20] = RTK_PIN_CONFIG(gpio_20, 0x5c, 16, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_22] = RTK_PIN_CONFIG(gpio_22, 0x5c, 21, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_31] = RTK_PIN_CONFIG(gpio_31, 0x5c, 26, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_47] = RTK_PIN_CONFIG(gpio_47, 0x60, 12, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_25] = RTK_PIN_CONFIG(gpio_25, 0x60, 17, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_6] = RTK_PIN_CONFIG(gpio_6, 0x60, 22, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_5] = RTK_PIN_CONFIG(gpio_5, 0x60, 27, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_4] = RTK_PIN_CONFIG(gpio_4, 0x64, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_3] = RTK_PIN_CONFIG(gpio_3, 0x64, 5, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_2] = RTK_PIN_CONFIG(gpio_2, 0x64, 10, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_1] = RTK_PIN_CONFIG(gpio_1, 0x64, 15, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_0] = RTK_PIN_CONFIG(gpio_0, 0x64, 20, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_52] = RTK_PIN_CONFIG(gpio_52, 0x64, 25, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_35] = RTK_PIN_CONFIG(gpio_35, 0x68, 0, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_34] = RTK_PIN_CONFIG(gpio_34, 0x68, 5, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_40] = RTK_PIN_CONFIG(gpio_40, 0x68, 10, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_GPIO_41] = RTK_PIN_CONFIG(gpio_41, 0x6c, 0, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_GPIO_42] = RTK_PIN_CONFIG(gpio_42, 0x6c, 13, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_GPIO_43] = RTK_PIN_CONFIG(gpio_43, 0x70, 0, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_GPIO_44] = RTK_PIN_CONFIG(gpio_44, 0x70, 13, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_GPIO_45] = RTK_PIN_CONFIG(gpio_45, 0x70, 26, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_GPIO_8] = RTK_PIN_CONFIG(gpio_8, 0x74, 7, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_9] = RTK_PIN_CONFIG(gpio_9, 0x74, 12, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_10] = RTK_PIN_CONFIG(gpio_10, 0x74, 17, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_GPIO_11] = RTK_PIN_CONFIG(gpio_11, 0x74, 22, 1, 2, 0, 3, 4, PADDRI_4_8),
	[RTD1619B_ISO_HIF_RDY] = RTK_PIN_CONFIG(hif_rdy, 0x78, 0, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_HIF_CLK] = RTK_PIN_CONFIG(hif_clk, 0x78, 13, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_GPIO_32] = RTK_PIN_CONFIG(gpio_32, 0x78, 26, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_GPIO_33] = RTK_PIN_CONFIG(gpio_33, 0x7c, 7, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_HIF_DATA] = RTK_PIN_CONFIG(hif_data, 0x80, 0, 0, 1, 3, 2, 12, NA),
	[RTD1619B_ISO_HIF_EN] = RTK_PIN_CONFIG(hif_en, 0x80, 13, 0, 1, 3, 2, 12, NA),
};

static const struct rtd_pin_sconfig_desc rtd1619b_iso_sconfigs[] = {
	RTK_PIN_SCONFIG(spi_si, 0x28, 18, 3, 21, 3, 24, 3),
	RTK_PIN_SCONFIG(spi_sck, 0x2c, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(spi_ce_n, 0x2c, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(spi_so, 0x2c, 29, 3, 32, 3, 35, 3),
	RTK_PIN_SCONFIG(emmc_clk, 0x30, 10, 3, 13, 3, 16, 3),
	RTK_PIN_SCONFIG(emmc_cmd, 0x34, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(emmc_rst_n, 0x34, 17, 3, 20, 3, 23, 3),
	RTK_PIN_SCONFIG(emmc_dd_sb, 0x34, 30, 3, 33, 3, 36, 3),
	RTK_PIN_SCONFIG(emmc_data_5, 0x38, 11, 3, 14, 3, 17, 3),
	RTK_PIN_SCONFIG(emmc_data_3, 0x3c, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(emmc_data_4, 0x3c, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(emmc_data_0, 0x3c, 29, 3, 32, 3, 35, 3),
	RTK_PIN_SCONFIG(emmc_data_1, 0x40, 10, 3, 13, 3, 16, 3),
	RTK_PIN_SCONFIG(emmc_data_2, 0x44, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(emmc_data_7, 0x44, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(emmc_data_6, 0x44, 29, 3, 32, 3, 35, 3),
	RTK_PIN_SCONFIG(gpio_40, 0x68, 13, 3, 16, 3, 19, 3),
	RTK_PIN_SCONFIG(gpio_41, 0x6c, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(gpio_42, 0x6c, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(gpio_43, 0x70, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(gpio_44, 0x70, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(gpio_45, 0x70, 29, 3, 32, 3, 35, 3),
	RTK_PIN_SCONFIG(hif_rdy, 0x78, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(hif_clk, 0x78, 16, 3, 19, 3, 22, 3),
	RTK_PIN_SCONFIG(gpio_32, 0x78, 29, 3, 32, 3, 35, 3),
	RTK_PIN_SCONFIG(gpio_33, 0x7c, 10, 3, 13, 3, 16, 3),
	RTK_PIN_SCONFIG(hif_data, 0x80, 3, 3, 6, 3, 9, 3),
	RTK_PIN_SCONFIG(hif_en, 0x80, 16, 3, 19, 3, 22, 3),
};

static const struct rtd_pinctrl_desc rtd1619b_iso_pinctrl_desc = {
	.pins = rtd1619b_iso_pins,
	.num_pins = ARRAY_SIZE(rtd1619b_iso_pins),
	.groups = rtd1619b_pin_groups,
	.num_groups = ARRAY_SIZE(rtd1619b_pin_groups),
	.functions = rtd1619b_pin_functions,
	.num_functions = ARRAY_SIZE(rtd1619b_pin_functions),
	.muxes = rtd1619b_iso_muxes,
	.num_muxes = ARRAY_SIZE(rtd1619b_iso_muxes),
	.configs = rtd1619b_iso_configs,
	.num_configs = ARRAY_SIZE(rtd1619b_iso_configs),
	.sconfigs = rtd1619b_iso_sconfigs,
	.num_sconfigs = ARRAY_SIZE(rtd1619b_iso_sconfigs),
};

static int rtd1619b_pinctrl_probe(struct platform_device *pdev)
{
	return rtd_pinctrl_probe(pdev, &rtd1619b_iso_pinctrl_desc);
}

static const struct of_device_id rtd1619b_pinctrl_of_match[] = {
	{ .compatible = "realtek,rtd1619b-pinctrl", },
	{},
};

static struct platform_driver rtd1619b_pinctrl_driver = {
	.driver = {
		.name = "rtd1619b-pinctrl",
		.of_match_table = rtd1619b_pinctrl_of_match,
	},
	.probe = rtd1619b_pinctrl_probe,
};

static int __init rtd1619b_pinctrl_init(void)
{
	return platform_driver_register(&rtd1619b_pinctrl_driver);
}
arch_initcall(rtd1619b_pinctrl_init);

static void __exit rtd1619b_pinctrl_exit(void)
{
	platform_driver_unregister(&rtd1619b_pinctrl_driver);
}
module_exit(rtd1619b_pinctrl_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Realtek Semiconductor Corporation");
MODULE_DESCRIPTION("Realtek DHC SoC RTD1619B pinctrl driver");

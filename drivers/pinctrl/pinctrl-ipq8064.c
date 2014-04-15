/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-msm.h"

static const struct pinctrl_pin_desc ipq8064_pins[] = {
	PINCTRL_PIN(0, "GPIO_1"),
	PINCTRL_PIN(1, "GPIO_1"),
	PINCTRL_PIN(2, "GPIO_2"),
	PINCTRL_PIN(3, "GPIO_3"),
	PINCTRL_PIN(4, "GPIO_4"),
	PINCTRL_PIN(5, "GPIO_5"),
	PINCTRL_PIN(6, "GPIO_6"),
	PINCTRL_PIN(7, "GPIO_7"),
	PINCTRL_PIN(8, "GPIO_8"),
	PINCTRL_PIN(9, "GPIO_9"),
	PINCTRL_PIN(10, "GPIO_10"),
	PINCTRL_PIN(11, "GPIO_11"),
	PINCTRL_PIN(12, "GPIO_12"),
	PINCTRL_PIN(13, "GPIO_13"),
	PINCTRL_PIN(14, "GPIO_14"),
	PINCTRL_PIN(15, "GPIO_15"),
	PINCTRL_PIN(16, "GPIO_16"),
	PINCTRL_PIN(17, "GPIO_17"),
	PINCTRL_PIN(18, "GPIO_18"),
	PINCTRL_PIN(19, "GPIO_19"),
	PINCTRL_PIN(20, "GPIO_20"),
	PINCTRL_PIN(21, "GPIO_21"),
	PINCTRL_PIN(22, "GPIO_22"),
	PINCTRL_PIN(23, "GPIO_23"),
	PINCTRL_PIN(24, "GPIO_24"),
	PINCTRL_PIN(25, "GPIO_25"),
	PINCTRL_PIN(26, "GPIO_26"),
	PINCTRL_PIN(27, "GPIO_27"),
	PINCTRL_PIN(28, "GPIO_28"),
	PINCTRL_PIN(29, "GPIO_29"),
	PINCTRL_PIN(30, "GPIO_30"),
	PINCTRL_PIN(31, "GPIO_31"),
	PINCTRL_PIN(32, "GPIO_32"),
	PINCTRL_PIN(33, "GPIO_33"),
	PINCTRL_PIN(34, "GPIO_34"),
	PINCTRL_PIN(35, "GPIO_35"),
	PINCTRL_PIN(36, "GPIO_36"),
	PINCTRL_PIN(37, "GPIO_37"),
	PINCTRL_PIN(38, "GPIO_38"),
	PINCTRL_PIN(39, "GPIO_39"),
	PINCTRL_PIN(40, "GPIO_40"),
	PINCTRL_PIN(41, "GPIO_41"),
	PINCTRL_PIN(42, "GPIO_42"),
	PINCTRL_PIN(43, "GPIO_43"),
	PINCTRL_PIN(44, "GPIO_44"),
	PINCTRL_PIN(45, "GPIO_45"),
	PINCTRL_PIN(46, "GPIO_46"),
	PINCTRL_PIN(47, "GPIO_47"),
	PINCTRL_PIN(48, "GPIO_48"),
	PINCTRL_PIN(49, "GPIO_49"),
	PINCTRL_PIN(50, "GPIO_50"),
	PINCTRL_PIN(51, "GPIO_51"),
	PINCTRL_PIN(52, "GPIO_52"),
	PINCTRL_PIN(53, "GPIO_53"),
	PINCTRL_PIN(54, "GPIO_54"),
	PINCTRL_PIN(55, "GPIO_55"),
	PINCTRL_PIN(56, "GPIO_56"),
	PINCTRL_PIN(57, "GPIO_57"),
	PINCTRL_PIN(58, "GPIO_58"),
	PINCTRL_PIN(59, "GPIO_59"),
	PINCTRL_PIN(60, "GPIO_60"),
	PINCTRL_PIN(61, "GPIO_61"),
	PINCTRL_PIN(62, "GPIO_62"),
	PINCTRL_PIN(63, "GPIO_63"),
	PINCTRL_PIN(64, "GPIO_64"),
	PINCTRL_PIN(65, "GPIO_65"),
	PINCTRL_PIN(66, "GPIO_66"),
	PINCTRL_PIN(67, "GPIO_67"),
	PINCTRL_PIN(68, "GPIO_68"),

	PINCTRL_PIN(69, "SDC3_CLK"),
	PINCTRL_PIN(70, "SDC3_CMD"),
	PINCTRL_PIN(71, "SDC3_DATA"),
};

#define DECLARE_IPQ_GPIO_PINS(pin) static const unsigned int gpio##pin##_pins[] = { pin }
DECLARE_IPQ_GPIO_PINS(0);
DECLARE_IPQ_GPIO_PINS(1);
DECLARE_IPQ_GPIO_PINS(2);
DECLARE_IPQ_GPIO_PINS(3);
DECLARE_IPQ_GPIO_PINS(4);
DECLARE_IPQ_GPIO_PINS(5);
DECLARE_IPQ_GPIO_PINS(6);
DECLARE_IPQ_GPIO_PINS(7);
DECLARE_IPQ_GPIO_PINS(8);
DECLARE_IPQ_GPIO_PINS(9);
DECLARE_IPQ_GPIO_PINS(10);
DECLARE_IPQ_GPIO_PINS(11);
DECLARE_IPQ_GPIO_PINS(12);
DECLARE_IPQ_GPIO_PINS(13);
DECLARE_IPQ_GPIO_PINS(14);
DECLARE_IPQ_GPIO_PINS(15);
DECLARE_IPQ_GPIO_PINS(16);
DECLARE_IPQ_GPIO_PINS(17);
DECLARE_IPQ_GPIO_PINS(18);
DECLARE_IPQ_GPIO_PINS(19);
DECLARE_IPQ_GPIO_PINS(20);
DECLARE_IPQ_GPIO_PINS(21);
DECLARE_IPQ_GPIO_PINS(22);
DECLARE_IPQ_GPIO_PINS(23);
DECLARE_IPQ_GPIO_PINS(24);
DECLARE_IPQ_GPIO_PINS(25);
DECLARE_IPQ_GPIO_PINS(26);
DECLARE_IPQ_GPIO_PINS(27);
DECLARE_IPQ_GPIO_PINS(28);
DECLARE_IPQ_GPIO_PINS(29);
DECLARE_IPQ_GPIO_PINS(30);
DECLARE_IPQ_GPIO_PINS(31);
DECLARE_IPQ_GPIO_PINS(32);
DECLARE_IPQ_GPIO_PINS(33);
DECLARE_IPQ_GPIO_PINS(34);
DECLARE_IPQ_GPIO_PINS(35);
DECLARE_IPQ_GPIO_PINS(36);
DECLARE_IPQ_GPIO_PINS(37);
DECLARE_IPQ_GPIO_PINS(38);
DECLARE_IPQ_GPIO_PINS(39);
DECLARE_IPQ_GPIO_PINS(40);
DECLARE_IPQ_GPIO_PINS(41);
DECLARE_IPQ_GPIO_PINS(42);
DECLARE_IPQ_GPIO_PINS(43);
DECLARE_IPQ_GPIO_PINS(44);
DECLARE_IPQ_GPIO_PINS(45);
DECLARE_IPQ_GPIO_PINS(46);
DECLARE_IPQ_GPIO_PINS(47);
DECLARE_IPQ_GPIO_PINS(48);
DECLARE_IPQ_GPIO_PINS(49);
DECLARE_IPQ_GPIO_PINS(50);
DECLARE_IPQ_GPIO_PINS(51);
DECLARE_IPQ_GPIO_PINS(52);
DECLARE_IPQ_GPIO_PINS(53);
DECLARE_IPQ_GPIO_PINS(54);
DECLARE_IPQ_GPIO_PINS(55);
DECLARE_IPQ_GPIO_PINS(56);
DECLARE_IPQ_GPIO_PINS(57);
DECLARE_IPQ_GPIO_PINS(58);
DECLARE_IPQ_GPIO_PINS(59);
DECLARE_IPQ_GPIO_PINS(60);
DECLARE_IPQ_GPIO_PINS(61);
DECLARE_IPQ_GPIO_PINS(62);
DECLARE_IPQ_GPIO_PINS(63);
DECLARE_IPQ_GPIO_PINS(64);
DECLARE_IPQ_GPIO_PINS(65);
DECLARE_IPQ_GPIO_PINS(66);
DECLARE_IPQ_GPIO_PINS(67);
DECLARE_IPQ_GPIO_PINS(68);

static const unsigned int sdc3_clk_pins[] = { 69 };
static const unsigned int sdc3_cmd_pins[] = { 70 };
static const unsigned int sdc3_data_pins[] = { 71 };

#define FUNCTION(fname)					\
	[IPQ_MUX_##fname] = {				\
		.name = #fname,				\
		.groups = fname##_groups,		\
		.ngroups = ARRAY_SIZE(fname##_groups),	\
	}

#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10) \
	{						\
		.name = "gpio" #id,			\
		.pins = gpio##id##_pins,		\
		.npins = ARRAY_SIZE(gpio##id##_pins),	\
		.funcs = (int[]){			\
			IPQ_MUX_NA, /* gpio mode */	\
			IPQ_MUX_##f1,			\
			IPQ_MUX_##f2,			\
			IPQ_MUX_##f3,			\
			IPQ_MUX_##f4,			\
			IPQ_MUX_##f5,			\
			IPQ_MUX_##f6,			\
			IPQ_MUX_##f7,			\
			IPQ_MUX_##f8,			\
			IPQ_MUX_##f9,			\
			IPQ_MUX_##f10,			\
		},					\
		.nfuncs = 11,				\
		.ctl_reg = 0x1000 + 0x10 * id,		\
		.io_reg = 0x1004 + 0x10 * id,		\
		.intr_cfg_reg = 0x1008 + 0x10 * id,	\
		.intr_status_reg = 0x100c + 0x10 * id,	\
		.intr_target_reg = 0x400 + 0x4 * id,	\
		.mux_bit = 2,				\
		.pull_bit = 0,				\
		.drv_bit = 6,				\
		.oe_bit = 9,				\
		.in_bit = 0,				\
		.out_bit = 1,				\
		.intr_enable_bit = 0,			\
		.intr_status_bit = 0,			\
		.intr_ack_high = 1,			\
		.intr_target_bit = 0,			\
		.intr_raw_status_bit = 3,		\
		.intr_polarity_bit = 1,			\
		.intr_detection_bit = 2,		\
		.intr_detection_width = 1,		\
	}

#define SDC_PINGROUP(pg_name, ctl, pull, drv)		\
	{						\
		.name = #pg_name,	                \
		.pins = pg_name##_pins,                 \
		.npins = ARRAY_SIZE(pg_name##_pins),    \
		.ctl_reg = ctl,                         \
		.io_reg = 0,                            \
		.intr_cfg_reg = 0,                      \
		.intr_status_reg = 0,                   \
		.intr_target_reg = 0,                   \
		.mux_bit = -1,                          \
		.pull_bit = pull,                       \
		.drv_bit = drv,                         \
		.oe_bit = -1,                           \
		.in_bit = -1,                           \
		.out_bit = -1,                          \
		.intr_enable_bit = -1,                  \
		.intr_status_bit = -1,                  \
		.intr_target_bit = -1,                  \
		.intr_raw_status_bit = -1,              \
		.intr_polarity_bit = -1,                \
		.intr_detection_bit = -1,               \
		.intr_detection_width = -1,             \
	}

enum ipq8064_functions {
	IPQ_MUX_mdio,
	IPQ_MUX_mi2s,
	IPQ_MUX_pdm,
	IPQ_MUX_ssbi,
	IPQ_MUX_spmi,
	IPQ_MUX_audio_pcm,
	IPQ_MUX_gsbi1,
	IPQ_MUX_gsbi2,
	IPQ_MUX_gsbi4,
	IPQ_MUX_gsbi5,
	IPQ_MUX_gsbi5_spi_cs1,
	IPQ_MUX_gsbi5_spi_cs2,
	IPQ_MUX_gsbi5_spi_cs3,
	IPQ_MUX_gsbi6,
	IPQ_MUX_gsbi7,
	IPQ_MUX_nss_spi,
	IPQ_MUX_sdc1,
	IPQ_MUX_spdif,
	IPQ_MUX_nand,
	IPQ_MUX_tsif1,
	IPQ_MUX_tsif2,
	IPQ_MUX_usb_fs_n,
	IPQ_MUX_usb_fs,
	IPQ_MUX_usb2_hsic,
	IPQ_MUX_rgmii2,
	IPQ_MUX_sata,
	IPQ_MUX_pcie1_rst,
	IPQ_MUX_pcie1_prsnt,
	IPQ_MUX_pcie1_pwrflt,
	IPQ_MUX_pcie1_pwren_n,
	IPQ_MUX_pcie1_pwren,
	IPQ_MUX_pcie1_clk_req,
	IPQ_MUX_pcie2_rst,
	IPQ_MUX_pcie2_prsnt,
	IPQ_MUX_pcie2_pwrflt,
	IPQ_MUX_pcie2_pwren_n,
	IPQ_MUX_pcie2_pwren,
	IPQ_MUX_pcie2_clk_req,
	IPQ_MUX_pcie3_rst,
	IPQ_MUX_pcie3_prsnt,
	IPQ_MUX_pcie3_pwrflt,
	IPQ_MUX_pcie3_pwren_n,
	IPQ_MUX_pcie3_pwren,
	IPQ_MUX_pcie3_clk_req,
	IPQ_MUX_ps_hold,
	IPQ_MUX_NA,
};

static const char * const mdio_groups[] = {
	"gpio0", "gpio1", "gpio10", "gpio11",
};

static const char * const mi2s_groups[] = {
	"gpio27", "gpio28", "gpio29", "gpio30", "gpio31", "gpio32",
	"gpio33", "gpio55", "gpio56", "gpio57", "gpio58",
};

static const char * const pdm_groups[] = {
	"gpio3", "gpio16", "gpio17", "gpio22", "gpio30", "gpio31",
	"gpio34", "gpio35", "gpio52", "gpio55", "gpio56", "gpio58",
	"gpio59",
};

static const char * const ssbi_groups[] = {
	"gpio10", "gpio11",
};

static const char * const spmi_groups[] = {
	"gpio10", "gpio11",
};

static const char * const audio_pcm_groups[] = {
	"gpio14", "gpio15", "gpio16", "gpio17",
};

static const char * const gsbi1_groups[] = {
	"gpio51", "gpio52", "gpio53", "gpio54",
};

static const char * const gsbi2_groups[] = {
	"gpio22", "gpio23", "gpio24", "gpio25",
};

static const char * const gsbi4_groups[] = {
	"gpio10", "gpio11", "gpio12", "gpio13",
};

static const char * const gsbi5_groups[] = {
	"gpio18", "gpio19", "gpio20", "gpio21",
};

static const char * const gsbi5_spi_cs1_groups[] = {
	"gpio6", "gpio61",
};

static const char * const gsbi5_spi_cs2_groups[] = {
	"gpio7", "gpio62",
};

static const char * const gsbi5_spi_cs3_groups[] = {
	"gpio2",
};

static const char * const gsbi6_groups[] = {
	"gpio27", "gpio28", "gpio29", "gpio30", "gpio55", "gpio56",
	"gpio57", "gpio58",
};

static const char * const gsbi7_groups[] = {
	"gpio6", "gpio7", "gpio8", "gpio9",
};

static const char * const nss_spi_groups[] = {
	"gpio14", "gpio15", "gpio16", "gpio17", "gpio55", "gpio56",
	"gpio57", "gpio58",
};

static const char * const sdc1_groups[] = {
	"gpio38", "gpio39", "gpio40", "gpio41", "gpio42", "gpio43",
	"gpio44", "gpio45", "gpio46", "gpio47",
};

static const char * const spdif_groups[] = {
	"gpio_10", "gpio_48",
};

static const char * const nand_groups[] = {
	"gpio34", "gpio35", "gpio36", "gpio37", "gpio38", "gpio39",
	"gpio40", "gpio41", "gpio42", "gpio43", "gpio44", "gpio45",
	"gpio46", "gpio47",
};

static const char * const tsif1_groups[] = {
	"gpio55", "gpio56", "gpio57", "gpio58",
};

static const char * const tsif2_groups[] = {
	"gpio59", "gpio60", "gpio61", "gpio62",
};

static const char * const usb_fs_n_groups[] = {
	"gpio6",
};

static const char * const usb_fs_groups[] = {
	"gpio6", "gpio7", "gpio8",
};

static const char * const usb2_hsic_groups[] = {
	"gpio67", "gpio68",
};

static const char * const rgmii2_groups[] = {
	"gpio27", "gpio28", "gpio29", "gpio30", "gpio31", "gpio32",
	"gpio51", "gpio52", "gpio59", "gpio60", "gpio61", "gpio62",
};

static const char * const sata_groups[] = {
	"gpio10",
};

static const char * const pcie1_rst_groups[] = {
	"gpio3",
};

static const char * const pcie1_prsnt_groups[] = {
	"gpio3", "gpio11",
};

static const char * const pcie1_pwren_n_groups[] = {
	"gpio4", "gpio12",
};

static const char * const pcie1_pwren_groups[] = {
	"gpio4", "gpio12",
};

static const char * const pcie1_pwrflt_groups[] = {
	"gpio5", "gpio13",
};

static const char * const pcie1_clk_req_groups[] = {
	"gpio5",
};

static const char * const pcie2_rst_groups[] = {
	"gpio48",
};

static const char * const pcie2_prsnt_groups[] = {
	"gpio11", "gpio48",
};

static const char * const pcie2_pwren_n_groups[] = {
	"gpio12", "gpio49",
};

static const char * const pcie2_pwren_groups[] = {
	"gpio12", "gpio49",
};

static const char * const pcie2_pwrflt_groups[] = {
	"gpio13", "gpio50",
};

static const char * const pcie2_clk_req_groups[] = {
	"gpio50",
};

static const char * const pcie3_rst_groups[] = {
	"gpio63",
};

static const char * const pcie3_prsnt_groups[] = {
	"gpio11",
};

static const char * const pcie3_pwren_n_groups[] = {
	"gpio12",
};

static const char * const pcie3_pwren_groups[] = {
	"gpio12",
};

static const char * const pcie3_pwrflt_groups[] = {
	"gpio13",
};

static const char * const pcie3_clk_req_groups[] = {
	"gpio65",
};

static const char * const ps_hold_groups[] = {
	"gpio26",
};

static const struct msm_function ipq8064_functions[] = {
	FUNCTION(mdio),
	FUNCTION(ssbi),
	FUNCTION(spmi),
	FUNCTION(mi2s),
	FUNCTION(pdm),
	FUNCTION(audio_pcm),
	FUNCTION(gsbi1),
	FUNCTION(gsbi2),
	FUNCTION(gsbi4),
	FUNCTION(gsbi5),
	FUNCTION(gsbi5_spi_cs1),
	FUNCTION(gsbi5_spi_cs2),
	FUNCTION(gsbi5_spi_cs3),
	FUNCTION(gsbi6),
	FUNCTION(gsbi7),
	FUNCTION(nss_spi),
	FUNCTION(sdc1),
	FUNCTION(spdif),
	FUNCTION(nand),
	FUNCTION(tsif1),
	FUNCTION(tsif2),
	FUNCTION(usb_fs_n),
	FUNCTION(usb_fs),
	FUNCTION(usb2_hsic),
	FUNCTION(rgmii2),
	FUNCTION(sata),
	FUNCTION(pcie1_rst),
	FUNCTION(pcie1_prsnt),
	FUNCTION(pcie1_pwren_n),
	FUNCTION(pcie1_pwren),
	FUNCTION(pcie1_pwrflt),
	FUNCTION(pcie1_clk_req),
	FUNCTION(pcie2_rst),
	FUNCTION(pcie2_prsnt),
	FUNCTION(pcie2_pwren_n),
	FUNCTION(pcie2_pwren),
	FUNCTION(pcie2_pwrflt),
	FUNCTION(pcie2_clk_req),
	FUNCTION(pcie3_rst),
	FUNCTION(pcie3_prsnt),
	FUNCTION(pcie3_pwren_n),
	FUNCTION(pcie3_pwren),
	FUNCTION(pcie3_pwrflt),
	FUNCTION(pcie3_clk_req),
	FUNCTION(ps_hold),
};

static const struct msm_pingroup ipq8064_groups[] = {
	PINGROUP(0, mdio, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(1, mdio, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(2, gsbi5_spi_cs3, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(3, pcie1_rst, pcie1_prsnt, pdm, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(4, pcie1_pwren_n, pcie1_pwren, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(5, pcie1_clk_req, pcie1_pwrflt, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(6, gsbi7, usb_fs, gsbi5_spi_cs1, usb_fs_n, NA, NA, NA, NA, NA, NA),
	PINGROUP(7, gsbi7, usb_fs, gsbi5_spi_cs2, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(8, gsbi7, usb_fs, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(9, gsbi7, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(10, gsbi4, spdif, sata, ssbi, mdio, spmi, NA, NA, NA, NA),
	PINGROUP(11, gsbi4, pcie2_prsnt, pcie1_prsnt, pcie3_prsnt, ssbi, mdio, spmi, NA, NA, NA),
	PINGROUP(12, gsbi4, pcie2_pwren_n, pcie1_pwren_n, pcie3_pwren_n, pcie2_pwren, pcie1_pwren, pcie3_pwren, NA, NA, NA),
	PINGROUP(13, gsbi4, pcie2_pwrflt, pcie1_pwrflt, pcie3_pwrflt, NA, NA, NA, NA, NA, NA),
	PINGROUP(14, audio_pcm, nss_spi, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(15, audio_pcm, nss_spi, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(16, audio_pcm, nss_spi, pdm, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(17, audio_pcm, nss_spi, pdm, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(18, gsbi5, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(19, gsbi5, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(20, gsbi5, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(21, gsbi5, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(22, gsbi2, pdm, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(23, gsbi2, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(24, gsbi2, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(25, gsbi2, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(26, ps_hold, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(27, mi2s, rgmii2, gsbi6, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(28, mi2s, rgmii2, gsbi6, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(29, mi2s, rgmii2, gsbi6, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(30, mi2s, rgmii2, gsbi6, pdm, NA, NA, NA, NA, NA, NA),
	PINGROUP(31, mi2s, rgmii2, pdm, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(32, mi2s, rgmii2, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(33, mi2s, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(34, nand, pdm, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(35, nand, pdm, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(36, nand, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(37, nand, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(38, nand, sdc1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(39, nand, sdc1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(40, nand, sdc1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(41, nand, sdc1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(42, nand, sdc1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(43, nand, sdc1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(44, nand, sdc1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(45, nand, sdc1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(46, nand, sdc1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(47, nand, sdc1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(48, pcie2_rst, spdif, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(49, pcie2_pwren_n, pcie2_pwren, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(50, pcie2_clk_req, pcie2_pwrflt, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(51, gsbi1, rgmii2, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(52, gsbi1, rgmii2, pdm, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(53, gsbi1, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(54, gsbi1, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(55, tsif1, mi2s, gsbi6, pdm, nss_spi, NA, NA, NA, NA, NA),
	PINGROUP(56, tsif1, mi2s, gsbi6, pdm, nss_spi, NA, NA, NA, NA, NA),
	PINGROUP(57, tsif1, mi2s, gsbi6, nss_spi, NA, NA, NA, NA, NA, NA),
	PINGROUP(58, tsif1, mi2s, gsbi6, pdm, nss_spi, NA, NA, NA, NA, NA),
	PINGROUP(59, tsif2, rgmii2, pdm, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(60, tsif2, rgmii2, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(61, tsif2, rgmii2, gsbi5_spi_cs1, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(62, tsif2, rgmii2, gsbi5_spi_cs2, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(63, pcie3_rst, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(64, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(65, pcie3_clk_req, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(66, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(67, usb2_hsic, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(68, usb2_hsic, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	SDC_PINGROUP(sdc3_clk, 0x204a, 14, 6),
	SDC_PINGROUP(sdc3_cmd, 0x204a, 11, 3),
	SDC_PINGROUP(sdc3_data, 0x204a, 9, 0),
};

#define NUM_GPIO_PINGROUPS 69

static const struct msm_pinctrl_soc_data ipq8064_pinctrl = {
	.pins = ipq8064_pins,
	.npins = ARRAY_SIZE(ipq8064_pins),
	.functions = ipq8064_functions,
	.nfunctions = ARRAY_SIZE(ipq8064_functions),
	.groups = ipq8064_groups,
	.ngroups = ARRAY_SIZE(ipq8064_groups),
	.ngpios = NUM_GPIO_PINGROUPS,
};

static int ipq8064_pinctrl_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &ipq8064_pinctrl);
}

static const struct of_device_id ipq8064_pinctrl_of_match[] = {
	{ .compatible = "qcom,ipq8064-pinctrl", },
	{ },
};

static struct platform_driver ipq8064_pinctrl_driver = {
	.driver = {
		.name = "ipq8064-pinctrl",
		.owner = THIS_MODULE,
		.of_match_table = ipq8064_pinctrl_of_match,
	},
	.probe = ipq8064_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init ipq8064_pinctrl_init(void)
{
	return platform_driver_register(&ipq8064_pinctrl_driver);
}
arch_initcall(ipq8064_pinctrl_init);

static void __exit ipq8064_pinctrl_exit(void)
{
	platform_driver_unregister(&ipq8064_pinctrl_driver);
}
module_exit(ipq8064_pinctrl_exit);

MODULE_AUTHOR("Andy Gross <agross@codeaurora.org>");
MODULE_DESCRIPTION("Qualcomm IPQ8064 pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, ipq8064_pinctrl_of_match);

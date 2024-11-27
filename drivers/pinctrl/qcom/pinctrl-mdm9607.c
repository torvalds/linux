// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, Konrad Dybcio <konrad.dybcio@somainline.org>
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * based on pinctrl-msm8916.c
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-msm.h"
static const struct pinctrl_pin_desc mdm9607_pins[] = {
	PINCTRL_PIN(0, "GPIO_0"),
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
	PINCTRL_PIN(69, "GPIO_69"),
	PINCTRL_PIN(70, "GPIO_70"),
	PINCTRL_PIN(71, "GPIO_71"),
	PINCTRL_PIN(72, "GPIO_72"),
	PINCTRL_PIN(73, "GPIO_73"),
	PINCTRL_PIN(74, "GPIO_74"),
	PINCTRL_PIN(75, "GPIO_75"),
	PINCTRL_PIN(76, "GPIO_76"),
	PINCTRL_PIN(77, "GPIO_77"),
	PINCTRL_PIN(78, "GPIO_78"),
	PINCTRL_PIN(79, "GPIO_79"),
	PINCTRL_PIN(80, "SDC1_RCLK"),
	PINCTRL_PIN(81, "SDC1_CLK"),
	PINCTRL_PIN(82, "SDC1_CMD"),
	PINCTRL_PIN(83, "SDC1_DATA"),
	PINCTRL_PIN(84, "SDC2_CLK"),
	PINCTRL_PIN(85, "SDC2_CMD"),
	PINCTRL_PIN(86, "SDC2_DATA"),
	PINCTRL_PIN(87, "QDSD_CLK"),
	PINCTRL_PIN(88, "QDSD_CMD"),
	PINCTRL_PIN(89, "QDSD_DATA0"),
	PINCTRL_PIN(90, "QDSD_DATA1"),
	PINCTRL_PIN(91, "QDSD_DATA2"),
	PINCTRL_PIN(92, "QDSD_DATA3"),
};

#define DECLARE_MSM_GPIO_PINS(pin) \
	static const unsigned int gpio##pin##_pins[] = { pin }
DECLARE_MSM_GPIO_PINS(0);
DECLARE_MSM_GPIO_PINS(1);
DECLARE_MSM_GPIO_PINS(2);
DECLARE_MSM_GPIO_PINS(3);
DECLARE_MSM_GPIO_PINS(4);
DECLARE_MSM_GPIO_PINS(5);
DECLARE_MSM_GPIO_PINS(6);
DECLARE_MSM_GPIO_PINS(7);
DECLARE_MSM_GPIO_PINS(8);
DECLARE_MSM_GPIO_PINS(9);
DECLARE_MSM_GPIO_PINS(10);
DECLARE_MSM_GPIO_PINS(11);
DECLARE_MSM_GPIO_PINS(12);
DECLARE_MSM_GPIO_PINS(13);
DECLARE_MSM_GPIO_PINS(14);
DECLARE_MSM_GPIO_PINS(15);
DECLARE_MSM_GPIO_PINS(16);
DECLARE_MSM_GPIO_PINS(17);
DECLARE_MSM_GPIO_PINS(18);
DECLARE_MSM_GPIO_PINS(19);
DECLARE_MSM_GPIO_PINS(20);
DECLARE_MSM_GPIO_PINS(21);
DECLARE_MSM_GPIO_PINS(22);
DECLARE_MSM_GPIO_PINS(23);
DECLARE_MSM_GPIO_PINS(24);
DECLARE_MSM_GPIO_PINS(25);
DECLARE_MSM_GPIO_PINS(26);
DECLARE_MSM_GPIO_PINS(27);
DECLARE_MSM_GPIO_PINS(28);
DECLARE_MSM_GPIO_PINS(29);
DECLARE_MSM_GPIO_PINS(30);
DECLARE_MSM_GPIO_PINS(31);
DECLARE_MSM_GPIO_PINS(32);
DECLARE_MSM_GPIO_PINS(33);
DECLARE_MSM_GPIO_PINS(34);
DECLARE_MSM_GPIO_PINS(35);
DECLARE_MSM_GPIO_PINS(36);
DECLARE_MSM_GPIO_PINS(37);
DECLARE_MSM_GPIO_PINS(38);
DECLARE_MSM_GPIO_PINS(39);
DECLARE_MSM_GPIO_PINS(40);
DECLARE_MSM_GPIO_PINS(41);
DECLARE_MSM_GPIO_PINS(42);
DECLARE_MSM_GPIO_PINS(43);
DECLARE_MSM_GPIO_PINS(44);
DECLARE_MSM_GPIO_PINS(45);
DECLARE_MSM_GPIO_PINS(46);
DECLARE_MSM_GPIO_PINS(47);
DECLARE_MSM_GPIO_PINS(48);
DECLARE_MSM_GPIO_PINS(49);
DECLARE_MSM_GPIO_PINS(50);
DECLARE_MSM_GPIO_PINS(51);
DECLARE_MSM_GPIO_PINS(52);
DECLARE_MSM_GPIO_PINS(53);
DECLARE_MSM_GPIO_PINS(54);
DECLARE_MSM_GPIO_PINS(55);
DECLARE_MSM_GPIO_PINS(56);
DECLARE_MSM_GPIO_PINS(57);
DECLARE_MSM_GPIO_PINS(58);
DECLARE_MSM_GPIO_PINS(59);
DECLARE_MSM_GPIO_PINS(60);
DECLARE_MSM_GPIO_PINS(61);
DECLARE_MSM_GPIO_PINS(62);
DECLARE_MSM_GPIO_PINS(63);
DECLARE_MSM_GPIO_PINS(64);
DECLARE_MSM_GPIO_PINS(65);
DECLARE_MSM_GPIO_PINS(66);
DECLARE_MSM_GPIO_PINS(67);
DECLARE_MSM_GPIO_PINS(68);
DECLARE_MSM_GPIO_PINS(69);
DECLARE_MSM_GPIO_PINS(70);
DECLARE_MSM_GPIO_PINS(71);
DECLARE_MSM_GPIO_PINS(72);
DECLARE_MSM_GPIO_PINS(73);
DECLARE_MSM_GPIO_PINS(74);
DECLARE_MSM_GPIO_PINS(75);
DECLARE_MSM_GPIO_PINS(76);
DECLARE_MSM_GPIO_PINS(77);
DECLARE_MSM_GPIO_PINS(78);
DECLARE_MSM_GPIO_PINS(79);

static const unsigned int sdc1_rclk_pins[] = { 80 };
static const unsigned int sdc1_clk_pins[] = { 81 };
static const unsigned int sdc1_cmd_pins[] = { 82 };
static const unsigned int sdc1_data_pins[] = { 83 };
static const unsigned int sdc2_clk_pins[] = { 84 };
static const unsigned int sdc2_cmd_pins[] = { 85 };
static const unsigned int sdc2_data_pins[] = { 86 };
static const unsigned int qdsd_clk_pins[] = { 87 };
static const unsigned int qdsd_cmd_pins[] = { 88 };
static const unsigned int qdsd_data0_pins[] = { 89 };
static const unsigned int qdsd_data1_pins[] = { 90 };
static const unsigned int qdsd_data2_pins[] = { 91 };
static const unsigned int qdsd_data3_pins[] = { 92 };

#define FUNCTION(fname)			                \
	[msm_mux_##fname] = {		                \
		.name = #fname,				\
		.groups = fname##_groups,               \
		.ngroups = ARRAY_SIZE(fname##_groups),	\
	}

#define REG_BASE 0x0
#define REG_SIZE 0x1000
#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, wake_off, bit)	\
	{					        \
		.name = "gpio" #id,			\
		.pins = gpio##id##_pins,		\
		.npins = (unsigned int)ARRAY_SIZE(gpio##id##_pins),	\
		.ctl_reg = REG_BASE + REG_SIZE * id,			\
		.io_reg = REG_BASE + 0x4 + REG_SIZE * id,		\
		.intr_cfg_reg = REG_BASE + 0x8 + REG_SIZE * id,		\
		.intr_status_reg = REG_BASE + 0xc + REG_SIZE * id,	\
		.intr_target_reg = REG_BASE + 0x8 + REG_SIZE * id,	\
		.mux_bit = 2,			\
		.pull_bit = 0,			\
		.drv_bit = 6,			\
		.egpio_enable = 12,		\
		.egpio_present = 11,	\
		.oe_bit = 9,			\
		.in_bit = 0,			\
		.out_bit = 1,			\
		.intr_enable_bit = 0,		\
		.intr_status_bit = 0,		\
		.intr_target_bit = 5,		\
		.intr_target_kpss_val = 4,	\
		.intr_raw_status_bit = 4,	\
		.intr_polarity_bit = 1,		\
		.intr_detection_bit = 2,	\
		.intr_detection_width = 2,	\
		.wake_reg = REG_BASE + wake_off,	\
		.wake_bit = bit,		\
		.funcs = (int[]){			\
			msm_mux_gpio, /* gpio mode */	\
			msm_mux_##f1,			\
			msm_mux_##f2,			\
			msm_mux_##f3,			\
			msm_mux_##f4,			\
			msm_mux_##f5,			\
			msm_mux_##f6,			\
			msm_mux_##f7,			\
			msm_mux_##f8,			\
			msm_mux_##f9,			\
			msm_mux_##f10,			\
			msm_mux_##f11,			\
			msm_mux_##f12,			\
			msm_mux_##f13			\
		},					        \
		.nfuncs = 14,				\
	}

#define SDC_QDSD_PINGROUP(pg_name, ctl, pull, drv)	\
	{					        \
		.name = #pg_name,			\
		.pins = pg_name##_pins,			\
		.npins = (unsigned int)ARRAY_SIZE(pg_name##_pins),	\
		.ctl_reg = ctl,				\
		.io_reg = 0,				\
		.intr_cfg_reg = 0,			\
		.intr_status_reg = 0,			\
		.intr_target_reg = 0,			\
		.mux_bit = -1,				\
		.pull_bit = pull,			\
		.drv_bit = drv,				\
		.oe_bit = -1,				\
		.in_bit = -1,				\
		.out_bit = -1,				\
		.intr_enable_bit = -1,			\
		.intr_status_bit = -1,			\
		.intr_target_bit = -1,			\
		.intr_raw_status_bit = -1,		\
		.intr_polarity_bit = -1,		\
		.intr_detection_bit = -1,		\
		.intr_detection_width = -1,		\
	}

#define UFS_RESET(pg_name, offset)				\
	{					        \
		.name = #pg_name,			\
		.pins = pg_name##_pins,			\
		.npins = (unsigned int)ARRAY_SIZE(pg_name##_pins),	\
		.ctl_reg = offset,			\
		.io_reg = offset + 0x4,			\
		.intr_cfg_reg = 0,			\
		.intr_status_reg = 0,			\
		.intr_target_reg = 0,			\
		.mux_bit = -1,				\
		.pull_bit = 3,				\
		.drv_bit = 0,				\
		.oe_bit = -1,				\
		.in_bit = -1,				\
		.out_bit = 0,				\
		.intr_enable_bit = -1,			\
		.intr_status_bit = -1,			\
		.intr_target_bit = -1,			\
		.intr_raw_status_bit = -1,		\
		.intr_polarity_bit = -1,		\
		.intr_detection_bit = -1,		\
		.intr_detection_width = -1,		\
	}

#define QUP_I3C(qup_mode, qup_offset)			\
	{						\
		.mode = qup_mode,			\
		.offset = REG_BASE + qup_offset,			\
	}

enum mdm9607_functions {
	msm_mux_gpio,
	msm_mux_adsp_ext,
	msm_mux_atest_bbrx0,
	msm_mux_atest_bbrx1,
	msm_mux_atest_char_start,
	msm_mux_atest_char_status0,
	msm_mux_atest_char_status1,
	msm_mux_atest_char_status2,
	msm_mux_atest_char_status3,
	msm_mux_atest_combodac_to_gpio_native,
	msm_mux_atest_gpsadc_dtest0_native,
	msm_mux_atest_gpsadc_dtest1_native,
	msm_mux_atest_tsens,
	msm_mux_bimc_dte0,
	msm_mux_bimc_dte1,
	msm_mux_blsp1_spi,
	msm_mux_blsp2_spi,
	msm_mux_blsp3_spi,
	msm_mux_blsp_i2c_scl1,
	msm_mux_blsp_i2c_scl2,
	msm_mux_blsp_i2c_scl3,
	msm_mux_blsp_i2c_scl4,
	msm_mux_blsp_i2c_scl5,
	msm_mux_blsp_i2c_scl6,
	msm_mux_blsp_i2c_sda1,
	msm_mux_blsp_i2c_sda2,
	msm_mux_blsp_i2c_sda3,
	msm_mux_blsp_i2c_sda4,
	msm_mux_blsp_i2c_sda5,
	msm_mux_blsp_i2c_sda6,
	msm_mux_blsp_spi1,
	msm_mux_blsp_spi2,
	msm_mux_blsp_spi3,
	msm_mux_blsp_spi4,
	msm_mux_blsp_spi5,
	msm_mux_blsp_spi6,
	msm_mux_blsp_spi_clk1,
	msm_mux_blsp_spi_clk2,
	msm_mux_blsp_spi_clk3,
	msm_mux_blsp_spi_clk4,
	msm_mux_blsp_spi_clk5,
	msm_mux_blsp_spi_clk6,
	msm_mux_blsp_spi_miso1,
	msm_mux_blsp_spi_miso2,
	msm_mux_blsp_spi_miso3,
	msm_mux_blsp_spi_miso4,
	msm_mux_blsp_spi_miso5,
	msm_mux_blsp_spi_miso6,
	msm_mux_blsp_spi_mosi1,
	msm_mux_blsp_spi_mosi2,
	msm_mux_blsp_spi_mosi3,
	msm_mux_blsp_spi_mosi4,
	msm_mux_blsp_spi_mosi5,
	msm_mux_blsp_spi_mosi6,
	msm_mux_blsp_uart1,
	msm_mux_blsp_uart2,
	msm_mux_blsp_uart3,
	msm_mux_blsp_uart4,
	msm_mux_blsp_uart5,
	msm_mux_blsp_uart6,
	msm_mux_blsp_uart_rx1,
	msm_mux_blsp_uart_rx2,
	msm_mux_blsp_uart_rx3,
	msm_mux_blsp_uart_rx4,
	msm_mux_blsp_uart_rx5,
	msm_mux_blsp_uart_rx6,
	msm_mux_blsp_uart_tx1,
	msm_mux_blsp_uart_tx2,
	msm_mux_blsp_uart_tx3,
	msm_mux_blsp_uart_tx4,
	msm_mux_blsp_uart_tx5,
	msm_mux_blsp_uart_tx6,
	msm_mux_blsp_uim_clk1,
	msm_mux_blsp_uim_clk2,
	msm_mux_blsp_uim_data1,
	msm_mux_blsp_uim_data2,
	msm_mux_coex_uart_rx,
	msm_mux_coex_uart_tx,
	msm_mux_cri_trng,
	msm_mux_cri_trng0,
	msm_mux_cri_trng1,
	msm_mux_dbg_out_clk,
	msm_mux_ebi0_wrcdc,
	msm_mux_ebi2_a,
	msm_mux_ebi2_a_d_8_b,
	msm_mux_ebi2_lcd,
	msm_mux_ebi2_lcd_cs_n_b,
	msm_mux_ebi2_lcd_te,
	msm_mux_ebi2_lcd_te_b,
	msm_mux_gcc_gp1_clk_a,
	msm_mux_gcc_gp1_clk_b,
	msm_mux_gcc_gp2_clk_a,
	msm_mux_gcc_gp2_clk_b,
	msm_mux_gcc_gp3_clk_a,
	msm_mux_gcc_gp3_clk_b,
	msm_mux_gcc_plltest_bypassnl,
	msm_mux_gcc_plltest_resetn,
	msm_mux_gcc_tlmm,
	msm_mux_gmac_mdio_clk,
	msm_mux_gmac_mdio_data,
	msm_mux_gsm0_tx,
	msm_mux_ldo_en,
	msm_mux_ldo_update,
	msm_mux_m_voc,
	msm_mux_modem_tsync_out,
	msm_mux_nav_ptp_pps_in_a,
	msm_mux_nav_ptp_pps_in_b,
	msm_mux_nav_tsync_out_a,
	msm_mux_nav_tsync_out_b,
	msm_mux_pa_indicator,
	msm_mux_pbs_out0,
	msm_mux_pbs_out1,
	msm_mux_pbs_out2,
	msm_mux_pri_mi2s_data0_a,
	msm_mux_pri_mi2s_data1_a,
	msm_mux_pri_mi2s_mclk_a,
	msm_mux_pri_mi2s_sck_a,
	msm_mux_pri_mi2s_ws_a,
	msm_mux_prng_rosc,
	msm_mux_ptp_pps_out_a,
	msm_mux_ptp_pps_out_b,
	msm_mux_pwr_crypto_enabled_a,
	msm_mux_pwr_crypto_enabled_b,
	msm_mux_pwr_modem_enabled_a,
	msm_mux_pwr_modem_enabled_b,
	msm_mux_pwr_nav_enabled_a,
	msm_mux_pwr_nav_enabled_b,
	msm_mux_qdss_cti_trig_in_a0,
	msm_mux_qdss_cti_trig_in_a1,
	msm_mux_qdss_cti_trig_in_b0,
	msm_mux_qdss_cti_trig_in_b1,
	msm_mux_qdss_cti_trig_out_a0,
	msm_mux_qdss_cti_trig_out_a1,
	msm_mux_qdss_cti_trig_out_b0,
	msm_mux_qdss_cti_trig_out_b1,
	msm_mux_qdss_traceclk_a,
	msm_mux_qdss_traceclk_b,
	msm_mux_qdss_tracectl_a,
	msm_mux_qdss_tracectl_b,
	msm_mux_qdss_tracedata_a,
	msm_mux_qdss_tracedata_b,
	msm_mux_sd_write_protect,
	msm_mux_sec_mi2s_data0,
	msm_mux_sec_mi2s_data1,
	msm_mux_sec_mi2s_sck,
	msm_mux_sec_mi2s_ws,
	msm_mux_ssbi1,
	msm_mux_ssbi2,
	msm_mux_uim1_clk,
	msm_mux_uim1_data,
	msm_mux_uim1_present,
	msm_mux_uim1_reset,
	msm_mux_uim2_clk,
	msm_mux_uim2_data,
	msm_mux_uim2_present,
	msm_mux_uim2_reset,
	msm_mux_uim_batt_alarm,
	msm_mux_NA,
};

static const char *const gpio_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3", "gpio4", "gpio5",
	"gpio6", "gpio7", "gpio8", "gpio9", "gpio10", "gpio11",
	"gpio12", "gpio13", "gpio14", "gpio15", "gpio16", "gpio17",
	"gpio18", "gpio19", "gpio20", "gpio21", "gpio22", "gpio23",
	"gpio24", "gpio25", "gpio26", "gpio27", "gpio28", "gpio29",
	"gpio30", "gpio31", "gpio32", "gpio33", "gpio34", "gpio35",
	"gpio36", "gpio37", "gpio38", "gpio39", "gpio40", "gpio41",
	"gpio42", "gpio43", "gpio44", "gpio45", "gpio46", "gpio47",
	"gpio48", "gpio49", "gpio50", "gpio51", "gpio52", "gpio53",
	"gpio54", "gpio55", "gpio56", "gpio57", "gpio58", "gpio59",
	"gpio60", "gpio61", "gpio62", "gpio63", "gpio64", "gpio65",
	"gpio66", "gpio67", "gpio68", "gpio69", "gpio70", "gpio71",
	"gpio72", "gpio73", "gpio74", "gpio75", "gpio76", "gpio77",
	"gpio78", "gpio79",
};
static const char *const adsp_ext_groups[] = {
	"gpio26",
};
static const char *const atest_bbrx0_groups[] = {
	"gpio40",
};
static const char *const atest_bbrx1_groups[] = {
	"gpio39",
};
static const char *const atest_char_start_groups[] = {
	"gpio64",
};
static const char *const atest_char_status0_groups[] = {
	"gpio63",
};
static const char *const atest_char_status1_groups[] = {
	"gpio62",
};
static const char *const atest_char_status2_groups[] = {
	"gpio61",
};
static const char *const atest_char_status3_groups[] = {
	"gpio60",
};
static const char *const atest_combodac_to_gpio_native_groups[] = {
	"gpio26", "gpio27", "gpio28", "gpio29", "gpio30", "gpio31",
	"gpio32", "gpio33", "gpio34", "gpio35", "gpio41", "gpio45",
	"gpio49", "gpio50", "gpio51", "gpio52", "gpio54", "gpio55",
	"gpio57", "gpio59",
};
static const char *const atest_gpsadc_dtest0_native_groups[] = {
	"gpio44",
};
static const char *const atest_gpsadc_dtest1_native_groups[] = {
	"gpio58",
};
static const char *const atest_tsens_groups[] = {
	"gpio73",
};
static const char *const bimc_dte0_groups[] = {
	"gpio2", "gpio15",
};
static const char *const bimc_dte1_groups[] = {
	"gpio1", "gpio24",
};
static const char *const blsp1_spi_groups[] = {
	"gpio20", "gpio21", "gpio78",
};
static const char *const blsp2_spi_groups[] = {
	"gpio11", "gpio13", "gpio77",
};
static const char *const blsp3_spi_groups[] = {
	"gpio12", "gpio26", "gpio76",
};
static const char *const blsp_i2c_scl1_groups[] = {
	"gpio15",
};
static const char *const blsp_i2c_scl2_groups[] = {
	"gpio7",
};
static const char *const blsp_i2c_scl3_groups[] = {
	"gpio3",
};
static const char *const blsp_i2c_scl4_groups[] = {
	"gpio19",
};
static const char *const blsp_i2c_scl5_groups[] = {
	"gpio11",
};
static const char *const blsp_i2c_scl6_groups[] = {
	"gpio23",
};
static const char *const blsp_i2c_sda1_groups[] = {
	"gpio14",
};
static const char *const blsp_i2c_sda2_groups[] = {
	"gpio6",
};
static const char *const blsp_i2c_sda3_groups[] = {
	"gpio2",
};
static const char *const blsp_i2c_sda4_groups[] = {
	"gpio18",
};
static const char *const blsp_i2c_sda5_groups[] = {
	"gpio10",
};
static const char *const blsp_i2c_sda6_groups[] = {
	"gpio22",
};
static const char *const blsp_spi1_groups[] = {
	"gpio14",
};
static const char *const blsp_spi2_groups[] = {
	"gpio6",
};
static const char *const blsp_spi3_groups[] = {
	"gpio2",
};
static const char *const blsp_spi4_groups[] = {
	"gpio18",
};
static const char *const blsp_spi5_groups[] = {
	"gpio10",
};
static const char *const blsp_spi6_groups[] = {
	"gpio22",
};
static const char *const blsp_spi_clk1_groups[] = {
	"gpio15",
};
static const char *const blsp_spi_clk2_groups[] = {
	"gpio7",
};
static const char *const blsp_spi_clk3_groups[] = {
	"gpio3",
};
static const char *const blsp_spi_clk4_groups[] = {
	"gpio19",
};
static const char *const blsp_spi_clk5_groups[] = {
	"gpio11",
};
static const char *const blsp_spi_clk6_groups[] = {
	"gpio23",
};
static const char *const blsp_spi_miso1_groups[] = {
	"gpio13",
};
static const char *const blsp_spi_miso2_groups[] = {
	"gpio5",
};
static const char *const blsp_spi_miso3_groups[] = {
	"gpio1",
};
static const char *const blsp_spi_miso4_groups[] = {
	"gpio17",
};
static const char *const blsp_spi_miso5_groups[] = {
	"gpio9",
};
static const char *const blsp_spi_miso6_groups[] = {
	"gpio21",
};
static const char *const blsp_spi_mosi1_groups[] = {
	"gpio12",
};
static const char *const blsp_spi_mosi2_groups[] = {
	"gpio4",
};
static const char *const blsp_spi_mosi3_groups[] = {
	"gpio0",
};
static const char *const blsp_spi_mosi4_groups[] = {
	"gpio16",
};
static const char *const blsp_spi_mosi5_groups[] = {
	"gpio8",
};
static const char *const blsp_spi_mosi6_groups[] = {
	"gpio20",
};
static const char *const blsp_uart1_groups[] = {
	"gpio14", "gpio15",
};
static const char *const blsp_uart2_groups[] = {
	"gpio6", "gpio7",
};
static const char *const blsp_uart3_groups[] = {
	"gpio2", "gpio3",
};
static const char *const blsp_uart4_groups[] = {
	"gpio18", "gpio19",
};
static const char *const blsp_uart5_groups[] = {
	"gpio10", "gpio11",
};
static const char *const blsp_uart6_groups[] = {
	"gpio22", "gpio23",
};
static const char *const blsp_uart_rx1_groups[] = {
	"gpio13",
};
static const char *const blsp_uart_rx2_groups[] = {
	"gpio5",
};
static const char *const blsp_uart_rx3_groups[] = {
	"gpio1",
};
static const char *const blsp_uart_rx4_groups[] = {
	"gpio17",
};
static const char *const blsp_uart_rx5_groups[] = {
	"gpio9",
};
static const char *const blsp_uart_rx6_groups[] = {
	"gpio21",
};
static const char *const blsp_uart_tx1_groups[] = {
	"gpio12",
};
static const char *const blsp_uart_tx2_groups[] = {
	"gpio4",
};
static const char *const blsp_uart_tx3_groups[] = {
	"gpio0",
};
static const char *const blsp_uart_tx4_groups[] = {
	"gpio16",
};
static const char *const blsp_uart_tx5_groups[] = {
	"gpio8",
};
static const char *const blsp_uart_tx6_groups[] = {
	"gpio20",
};
static const char *const blsp_uim_clk1_groups[] = {
	"gpio13",
};
static const char *const blsp_uim_clk2_groups[] = {
	"gpio5",
};
static const char *const blsp_uim_data1_groups[] = {
	"gpio12",
};
static const char *const blsp_uim_data2_groups[] = {
	"gpio4",
};
static const char *const coex_uart_rx_groups[] = {
	"gpio37",
};
static const char *const coex_uart_tx_groups[] = {
	"gpio36",
};
static const char *const cri_trng_groups[] = {
	"gpio42",
};
static const char *const cri_trng0_groups[] = {
	"gpio40",
};
static const char *const cri_trng1_groups[] = {
	"gpio57",
};
static const char *const dbg_out_clk_groups[] = {
	"gpio73",
};
static const char *const ebi0_wrcdc_groups[] = {
	"gpio70",
};
static const char *const ebi2_a_groups[] = {
	"gpio10",
};
static const char *const ebi2_a_d_8_b_groups[] = {
	"gpio22",
};
static const char *const ebi2_lcd_groups[] = {
	"gpio11", "gpio74", "gpio78",
};
static const char *const ebi2_lcd_cs_n_b_groups[] = {
	"gpio23",
};
static const char *const ebi2_lcd_te_groups[] = {
	"gpio8",
};
static const char *const ebi2_lcd_te_b_groups[] = {
	"gpio20",
};
static const char *const gcc_gp1_clk_a_groups[] = {
	"gpio27",
};
static const char *const gcc_gp1_clk_b_groups[] = {
	"gpio14",
};
static const char *const gcc_gp2_clk_a_groups[] = {
	"gpio28",
};
static const char *const gcc_gp2_clk_b_groups[] = {
	"gpio12",
};
static const char *const gcc_gp3_clk_a_groups[] = {
	"gpio29",
};
static const char *const gcc_gp3_clk_b_groups[] = {
	"gpio13",
};
static const char *const gcc_plltest_bypassnl_groups[] = {
	"gpio34",
};
static const char *const gcc_plltest_resetn_groups[] = {
	"gpio35",
};
static const char *const gcc_tlmm_groups[] = {
	"gpio72",
};
static const char *const gmac_mdio_clk_groups[] = {
	"gpio27",
};
static const char *const gmac_mdio_data_groups[] = {
	"gpio28",
};
static const char *const gsm0_tx_groups[] = {
	"gpio55",
};
static const char *const ldo_en_groups[] = {
	"gpio73",
};
static const char *const ldo_update_groups[] = {
	"gpio72",
};
static const char *const m_voc_groups[] = {
	"gpio8", "gpio78",
};
static const char *const modem_tsync_out_groups[] = {
	"gpio53",
};
static const char *const nav_ptp_pps_in_a_groups[] = {
	"gpio53",
};
static const char *const nav_ptp_pps_in_b_groups[] = {
	"gpio75",
};
static const char *const nav_tsync_out_a_groups[] = {
	"gpio53",
};
static const char *const nav_tsync_out_b_groups[] = {
	"gpio75",
};
static const char *const pa_indicator_groups[] = {
	"gpio51",
};
static const char *const pbs_out0_groups[] = {
	"gpio76",
};
static const char *const pbs_out1_groups[] = {
	"gpio77",
};
static const char *const pbs_out2_groups[] = {
	"gpio78",
};
static const char *const pri_mi2s_data0_a_groups[] = {
	"gpio21",
};
static const char *const pri_mi2s_data1_a_groups[] = {
	"gpio22",
};
static const char *const pri_mi2s_mclk_a_groups[] = {
	"gpio24",
};
static const char *const pri_mi2s_sck_a_groups[] = {
	"gpio23",
};
static const char *const pri_mi2s_ws_a_groups[] = {
	"gpio20",
};
static const char *const prng_rosc_groups[] = {
	"gpio30",
};
static const char *const ptp_pps_out_a_groups[] = {
	"gpio53",
};
static const char *const ptp_pps_out_b_groups[] = {
	"gpio75",
};
static const char *const pwr_crypto_enabled_a_groups[] = {
	"gpio25",
};
static const char *const pwr_crypto_enabled_b_groups[] = {
	"gpio79",
};
static const char *const pwr_modem_enabled_a_groups[] = {
	"gpio76",
};
static const char *const pwr_modem_enabled_b_groups[] = {
	"gpio77",
};
static const char *const pwr_nav_enabled_a_groups[] = {
	"gpio24",
};
static const char *const pwr_nav_enabled_b_groups[] = {
	"gpio78",
};
static const char *const qdss_cti_trig_in_a0_groups[] = {
	"gpio38",
};
static const char *const qdss_cti_trig_in_a1_groups[] = {
	"gpio3",
};
static const char *const qdss_cti_trig_in_b0_groups[] = {
	"gpio44",
};
static const char *const qdss_cti_trig_in_b1_groups[] = {
	"gpio56",
};
static const char *const qdss_cti_trig_out_a0_groups[] = {
	"gpio19",
};
static const char *const qdss_cti_trig_out_a1_groups[] = {
	"gpio18",
};
static const char *const qdss_cti_trig_out_b0_groups[] = {
	"gpio45",
};
static const char *const qdss_cti_trig_out_b1_groups[] = {
	"gpio57",
};
static const char *const qdss_traceclk_a_groups[] = {
	"gpio2",
};
static const char *const qdss_traceclk_b_groups[] = {
	"gpio50",
};
static const char *const qdss_tracectl_a_groups[] = {
	"gpio6",
};
static const char *const qdss_tracectl_b_groups[] = {
	"gpio49",
};
static const char *const qdss_tracedata_a_groups[] = {
	"gpio0", "gpio1", "gpio4", "gpio5", "gpio20", "gpio21",
	"gpio22", "gpio23", "gpio24", "gpio25", "gpio26", "gpio75",
	"gpio76", "gpio77", "gpio78", "gpio79",
};
static const char *const qdss_tracedata_b_groups[] = {
	"gpio10", "gpio39", "gpio40", "gpio41", "gpio42", "gpio43",
	"gpio46", "gpio47", "gpio48", "gpio51", "gpio52", "gpio53",
	"gpio54", "gpio55", "gpio58", "gpio59",
};
static const char *const sd_write_protect_groups[] = {
	"gpio25",
};
static const char *const sec_mi2s_data0_groups[] = {
	"gpio76",
};
static const char *const sec_mi2s_data1_groups[] = {
	"gpio77",
};
static const char *const sec_mi2s_sck_groups[] = {
	"gpio78",
};
static const char *const sec_mi2s_ws_groups[] = {
	"gpio79",
};
static const char *const ssbi1_groups[] = {
	"gpio58",
};
static const char *const ssbi2_groups[] = {
	"gpio59",
};
static const char *const uim1_clk_groups[] = {
	"gpio32",
};
static const char *const uim1_data_groups[] = {
	"gpio31",
};
static const char *const uim1_present_groups[] = {
	"gpio34",
};
static const char *const uim1_reset_groups[] = {
	"gpio33",
};
static const char *const uim2_clk_groups[] = {
	"gpio28",
};
static const char *const uim2_data_groups[] = {
	"gpio27",
};
static const char *const uim2_present_groups[] = {
	"gpio30",
};
static const char *const uim2_reset_groups[] = {
	"gpio29",
};
static const char *const uim_batt_alarm_groups[] = {
	"gpio35",
};

static const struct msm_function mdm9607_functions[] = {
	FUNCTION(gpio),
	FUNCTION(adsp_ext),
	FUNCTION(atest_bbrx0),
	FUNCTION(atest_bbrx1),
	FUNCTION(atest_char_start),
	FUNCTION(atest_char_status0),
	FUNCTION(atest_char_status1),
	FUNCTION(atest_char_status2),
	FUNCTION(atest_char_status3),
	FUNCTION(atest_combodac_to_gpio_native),
	FUNCTION(atest_gpsadc_dtest0_native),
	FUNCTION(atest_gpsadc_dtest1_native),
	FUNCTION(atest_tsens),
	FUNCTION(bimc_dte0),
	FUNCTION(bimc_dte1),
	FUNCTION(blsp1_spi),
	FUNCTION(blsp2_spi),
	FUNCTION(blsp3_spi),
	FUNCTION(blsp_i2c_scl1),
	FUNCTION(blsp_i2c_scl2),
	FUNCTION(blsp_i2c_scl3),
	FUNCTION(blsp_i2c_scl4),
	FUNCTION(blsp_i2c_scl5),
	FUNCTION(blsp_i2c_scl6),
	FUNCTION(blsp_i2c_sda1),
	FUNCTION(blsp_i2c_sda2),
	FUNCTION(blsp_i2c_sda3),
	FUNCTION(blsp_i2c_sda4),
	FUNCTION(blsp_i2c_sda5),
	FUNCTION(blsp_i2c_sda6),
	FUNCTION(blsp_spi1),
	FUNCTION(blsp_spi2),
	FUNCTION(blsp_spi3),
	FUNCTION(blsp_spi4),
	FUNCTION(blsp_spi5),
	FUNCTION(blsp_spi6),
	FUNCTION(blsp_spi_clk1),
	FUNCTION(blsp_spi_clk2),
	FUNCTION(blsp_spi_clk3),
	FUNCTION(blsp_spi_clk4),
	FUNCTION(blsp_spi_clk5),
	FUNCTION(blsp_spi_clk6),
	FUNCTION(blsp_spi_miso1),
	FUNCTION(blsp_spi_miso2),
	FUNCTION(blsp_spi_miso3),
	FUNCTION(blsp_spi_miso4),
	FUNCTION(blsp_spi_miso5),
	FUNCTION(blsp_spi_miso6),
	FUNCTION(blsp_spi_mosi1),
	FUNCTION(blsp_spi_mosi2),
	FUNCTION(blsp_spi_mosi3),
	FUNCTION(blsp_spi_mosi4),
	FUNCTION(blsp_spi_mosi5),
	FUNCTION(blsp_spi_mosi6),
	FUNCTION(blsp_uart1),
	FUNCTION(blsp_uart2),
	FUNCTION(blsp_uart3),
	FUNCTION(blsp_uart4),
	FUNCTION(blsp_uart5),
	FUNCTION(blsp_uart6),
	FUNCTION(blsp_uart_rx1),
	FUNCTION(blsp_uart_rx2),
	FUNCTION(blsp_uart_rx3),
	FUNCTION(blsp_uart_rx4),
	FUNCTION(blsp_uart_rx5),
	FUNCTION(blsp_uart_rx6),
	FUNCTION(blsp_uart_tx1),
	FUNCTION(blsp_uart_tx2),
	FUNCTION(blsp_uart_tx3),
	FUNCTION(blsp_uart_tx4),
	FUNCTION(blsp_uart_tx5),
	FUNCTION(blsp_uart_tx6),
	FUNCTION(blsp_uim_clk1),
	FUNCTION(blsp_uim_clk2),
	FUNCTION(blsp_uim_data1),
	FUNCTION(blsp_uim_data2),
	FUNCTION(coex_uart_rx),
	FUNCTION(coex_uart_tx),
	FUNCTION(cri_trng),
	FUNCTION(cri_trng0),
	FUNCTION(cri_trng1),
	FUNCTION(dbg_out_clk),
	FUNCTION(ebi0_wrcdc),
	FUNCTION(ebi2_a),
	FUNCTION(ebi2_a_d_8_b),
	FUNCTION(ebi2_lcd),
	FUNCTION(ebi2_lcd_cs_n_b),
	FUNCTION(ebi2_lcd_te),
	FUNCTION(ebi2_lcd_te_b),
	FUNCTION(gcc_gp1_clk_a),
	FUNCTION(gcc_gp1_clk_b),
	FUNCTION(gcc_gp2_clk_a),
	FUNCTION(gcc_gp2_clk_b),
	FUNCTION(gcc_gp3_clk_a),
	FUNCTION(gcc_gp3_clk_b),
	FUNCTION(gcc_plltest_bypassnl),
	FUNCTION(gcc_plltest_resetn),
	FUNCTION(gcc_tlmm),
	FUNCTION(gmac_mdio_clk),
	FUNCTION(gmac_mdio_data),
	FUNCTION(gsm0_tx),
	FUNCTION(ldo_en),
	FUNCTION(ldo_update),
	FUNCTION(m_voc),
	FUNCTION(modem_tsync_out),
	FUNCTION(nav_ptp_pps_in_a),
	FUNCTION(nav_ptp_pps_in_b),
	FUNCTION(nav_tsync_out_a),
	FUNCTION(nav_tsync_out_b),
	FUNCTION(pa_indicator),
	FUNCTION(pbs_out0),
	FUNCTION(pbs_out1),
	FUNCTION(pbs_out2),
	FUNCTION(pri_mi2s_data0_a),
	FUNCTION(pri_mi2s_data1_a),
	FUNCTION(pri_mi2s_mclk_a),
	FUNCTION(pri_mi2s_sck_a),
	FUNCTION(pri_mi2s_ws_a),
	FUNCTION(prng_rosc),
	FUNCTION(ptp_pps_out_a),
	FUNCTION(ptp_pps_out_b),
	FUNCTION(pwr_crypto_enabled_a),
	FUNCTION(pwr_crypto_enabled_b),
	FUNCTION(pwr_modem_enabled_a),
	FUNCTION(pwr_modem_enabled_b),
	FUNCTION(pwr_nav_enabled_a),
	FUNCTION(pwr_nav_enabled_b),
	FUNCTION(qdss_cti_trig_in_a0),
	FUNCTION(qdss_cti_trig_in_a1),
	FUNCTION(qdss_cti_trig_in_b0),
	FUNCTION(qdss_cti_trig_in_b1),
	FUNCTION(qdss_cti_trig_out_a0),
	FUNCTION(qdss_cti_trig_out_a1),
	FUNCTION(qdss_cti_trig_out_b0),
	FUNCTION(qdss_cti_trig_out_b1),
	FUNCTION(qdss_traceclk_a),
	FUNCTION(qdss_traceclk_b),
	FUNCTION(qdss_tracectl_a),
	FUNCTION(qdss_tracectl_b),
	FUNCTION(qdss_tracedata_a),
	FUNCTION(qdss_tracedata_b),
	FUNCTION(sd_write_protect),
	FUNCTION(sec_mi2s_data0),
	FUNCTION(sec_mi2s_data1),
	FUNCTION(sec_mi2s_sck),
	FUNCTION(sec_mi2s_ws),
	FUNCTION(ssbi1),
	FUNCTION(ssbi2),
	FUNCTION(uim1_clk),
	FUNCTION(uim1_data),
	FUNCTION(uim1_present),
	FUNCTION(uim1_reset),
	FUNCTION(uim2_clk),
	FUNCTION(uim2_data),
	FUNCTION(uim2_present),
	FUNCTION(uim2_reset),
	FUNCTION(uim_batt_alarm),
};

/* Every pin is maintained as a single group, and missing or non-existing pin
 * would be maintained as dummy group to synchronize pin group index with
 * pin descriptor registered with pinctrl core.
 * Clients would not be able to request these dummy pin groups.
 */
static const struct msm_pingroup mdm9607_groups[] = {
	[0] = PINGROUP(0, blsp_uart_tx3, blsp_spi_mosi3, NA, NA, NA, NA, NA,
		       qdss_tracedata_a, NA, NA, NA, NA, NA, 0, -1),
	[1] = PINGROUP(1, blsp_uart_rx3, blsp_spi_miso3, NA, NA, NA, NA, NA,
		       qdss_tracedata_a, bimc_dte1, NA, NA, NA, NA, 0, -1),
	[2] = PINGROUP(2, blsp_uart3, blsp_i2c_sda3, blsp_spi3, NA, NA, NA, NA,
		       NA, qdss_traceclk_a, bimc_dte0, NA, NA, NA, 0, -1),
	[3] = PINGROUP(3, blsp_uart3, blsp_i2c_scl3, blsp_spi_clk3, NA, NA, NA,
		       NA, NA, NA, qdss_cti_trig_in_a1, NA, NA, NA, 0, -1),
	[4] = PINGROUP(4, blsp_spi_mosi2, blsp_uart_tx2, blsp_uim_data2, NA, NA,
		       NA, NA, qdss_tracedata_a, NA, NA, NA, NA, NA, 0, -1),
	[5] = PINGROUP(5, blsp_spi_miso2, blsp_uart_rx2, blsp_uim_clk2, NA, NA,
		       NA, NA, qdss_tracedata_a, NA, NA, NA, NA, NA, 0x100008,
		       0),
	[6] = PINGROUP(6, blsp_spi2, blsp_uart2, blsp_i2c_sda2, NA, NA, NA, NA,
		       NA, NA, qdss_tracectl_a, NA, NA, NA, 0, -1),
	[7] = PINGROUP(7, blsp_spi_clk2, blsp_uart2, blsp_i2c_scl2, NA, NA, NA,
		       NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[8] = PINGROUP(8, blsp_spi_mosi5, blsp_uart_tx5, ebi2_lcd_te, m_voc, NA,
		       NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[9] = PINGROUP(9, blsp_spi_miso5, blsp_uart_rx5, NA, NA, NA, NA, NA, NA,
		       NA, NA, NA, NA, NA, 0, -1),
	[10] = PINGROUP(10, blsp_spi5, blsp_i2c_sda5, blsp_uart5, ebi2_a, NA,
			NA, qdss_tracedata_b, NA, NA, NA, NA, NA, NA, 0, -1),
	[11] = PINGROUP(11, blsp_spi_clk5, blsp_i2c_scl5, blsp_uart5, blsp2_spi,
			ebi2_lcd, NA, NA, NA, NA, NA, NA, NA, NA, 0x100008, 1),
	[12] = PINGROUP(12, blsp_spi_mosi1, blsp_uart_tx1, blsp_uim_data1,
			blsp3_spi, gcc_gp2_clk_b, NA, NA, NA, NA, NA, NA, NA,
			NA, 0x100008, 2),
	[13] = PINGROUP(13, blsp_spi_miso1, blsp_uart_rx1, blsp_uim_clk1,
			blsp2_spi, gcc_gp3_clk_b, NA, NA, NA, NA, NA, NA, NA,
			NA, 0x100008, 3),
	[14] = PINGROUP(14, blsp_spi1, blsp_uart1, blsp_i2c_sda1, gcc_gp1_clk_b,
			NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[15] = PINGROUP(15, blsp_spi_clk1, blsp_uart1, blsp_i2c_scl1, NA, NA,
			NA, NA, NA, NA, bimc_dte0, NA, NA, NA, 0, -1),
	[16] = PINGROUP(16, blsp_spi_mosi4, blsp_uart_tx4, NA, NA, NA, NA, NA,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[17] = PINGROUP(17, blsp_spi_miso4, blsp_uart_rx4, NA, NA, NA, NA, NA,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[18] = PINGROUP(18, blsp_spi4, blsp_uart4, blsp_i2c_sda4, NA, NA, NA,
			NA, NA, NA, qdss_cti_trig_out_a1, NA, NA, NA, 0, -1),
	[19] = PINGROUP(19, blsp_spi_clk4, blsp_uart4, blsp_i2c_scl4, NA, NA,
			NA, NA, NA, NA, NA, qdss_cti_trig_out_a0, NA, NA, 0,
			-1),
	[20] = PINGROUP(20, blsp_spi_mosi6, blsp_uart_tx6, pri_mi2s_ws_a,
			ebi2_lcd_te_b, blsp1_spi, NA, NA, NA, qdss_tracedata_a,
			NA, NA, NA, NA, 0x100008, 4),
	[21] = PINGROUP(21, blsp_spi_miso6, blsp_uart_rx6, pri_mi2s_data0_a,
			blsp1_spi, NA, NA, NA, NA, NA, NA, NA, qdss_tracedata_a,
			NA, 0x100008, 5),
	[22] = PINGROUP(22, blsp_spi6, blsp_uart6, pri_mi2s_data1_a,
			blsp_i2c_sda6, ebi2_a_d_8_b, NA, NA, NA, NA, NA, NA,
			qdss_tracedata_a, NA, 0, -1),
	[23] = PINGROUP(23, blsp_spi_clk6, blsp_uart6, pri_mi2s_sck_a,
			blsp_i2c_scl6, ebi2_lcd_cs_n_b, NA, NA, NA, NA,
			qdss_tracedata_a, NA, NA, NA, 0x100008, 6),
	[24] = PINGROUP(24, pri_mi2s_mclk_a, NA, pwr_nav_enabled_a, NA, NA, NA,
			NA, qdss_tracedata_a, bimc_dte1, NA, NA, NA, NA, 0, -1),
	[25] = PINGROUP(25, sd_write_protect, NA, pwr_crypto_enabled_a, NA, NA,
			NA, NA, qdss_tracedata_a, NA, NA, NA, NA, NA, 0, -1),
	[26] = PINGROUP(26, blsp3_spi, adsp_ext, NA, qdss_tracedata_a, NA,
			atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, NA,
			NA, 0x100008, 7),
	[27] = PINGROUP(27, uim2_data, gmac_mdio_clk, gcc_gp1_clk_a, NA, NA,
			atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, NA,
			NA, 0x100008, 8),
	[28] = PINGROUP(28, uim2_clk, gmac_mdio_data, gcc_gp2_clk_a, NA, NA,
			atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, NA,
			NA, 0x100008, 9),
	[29] = PINGROUP(29, uim2_reset, gcc_gp3_clk_a, NA, NA,
			atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, NA,
			NA, NA, 0x100008, 10),
	[30] = PINGROUP(30, uim2_present, prng_rosc, NA, NA,
			atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[31] = PINGROUP(31, uim1_data, NA, NA, atest_combodac_to_gpio_native,
			NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[32] = PINGROUP(32, uim1_clk, NA, NA, atest_combodac_to_gpio_native, NA,
			NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[33] = PINGROUP(33, uim1_reset, NA, NA, atest_combodac_to_gpio_native,
			NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[34] = PINGROUP(34, uim1_present, gcc_plltest_bypassnl, NA, NA,
			atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[35] = PINGROUP(35, uim_batt_alarm, gcc_plltest_resetn, NA,
			atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[36] = PINGROUP(36, coex_uart_tx, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[37] = PINGROUP(37, coex_uart_rx, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			NA, NA, NA, 0x100008, 11),
	[38] = PINGROUP(38, NA, NA, NA, qdss_cti_trig_in_a0, NA, NA, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[39] = PINGROUP(39, NA, NA, NA, qdss_tracedata_b, NA, atest_bbrx1, NA,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[40] = PINGROUP(40, NA, cri_trng0, NA, NA, NA, NA, qdss_tracedata_b, NA,
			atest_bbrx0, NA, NA, NA, NA, 0, -1),
	[41] = PINGROUP(41, NA, NA, NA, NA, NA, qdss_tracedata_b, NA,
			atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, 0,
			-1),
	[42] = PINGROUP(42, NA, cri_trng, NA, NA, qdss_tracedata_b, NA, NA, NA,
			NA, NA, NA, NA, NA, 0x100008, 12),
	[43] = PINGROUP(43, NA, NA, NA, NA, qdss_tracedata_b, NA, NA, NA, NA,
			NA, NA, NA, NA, 0x100008, 13),
	[44] = PINGROUP(44, NA, NA, qdss_cti_trig_in_b0, NA,
			atest_gpsadc_dtest0_native, NA, NA, NA, NA, NA, NA, NA,
			NA, 0x100008, 14),
	[45] = PINGROUP(45, NA, NA, qdss_cti_trig_out_b0, NA,
			atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[46] = PINGROUP(46, NA, NA, qdss_tracedata_b, NA, NA, NA, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[47] = PINGROUP(47, NA, NA, qdss_tracedata_b, NA, NA, NA, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[48] = PINGROUP(48, NA, NA, qdss_tracedata_b, NA, NA, NA, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[49] = PINGROUP(49, NA, NA, qdss_tracectl_b, NA,
			atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[50] = PINGROUP(50, NA, NA, qdss_traceclk_b, NA,
			atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[51] = PINGROUP(51, NA, pa_indicator, NA, qdss_tracedata_b, NA,
			atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, NA,
			NA, 0, -1),
	[52] = PINGROUP(52, NA, NA, NA, qdss_tracedata_b, NA,
			atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, NA,
			NA, 0, -1),
	[53] = PINGROUP(53, NA, modem_tsync_out, nav_tsync_out_a,
			nav_ptp_pps_in_a, ptp_pps_out_a, qdss_tracedata_b, NA,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[54] = PINGROUP(54, NA, qdss_tracedata_b, NA,
			atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[55] = PINGROUP(55, gsm0_tx, NA, qdss_tracedata_b, NA,
			atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[56] = PINGROUP(56, NA, NA, qdss_cti_trig_in_b1, NA, NA, NA, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[57] = PINGROUP(57, NA, cri_trng1, NA, qdss_cti_trig_out_b1, NA,
			atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, NA,
			NA, 0, -1),
	[58] = PINGROUP(58, NA, ssbi1, NA, qdss_tracedata_b, NA,
			atest_gpsadc_dtest1_native, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[59] = PINGROUP(59, NA, ssbi2, NA, qdss_tracedata_b, NA,
			atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, NA,
			NA, 0, -1),
	[60] = PINGROUP(60, atest_char_status3, NA, NA, NA, NA, NA, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[61] = PINGROUP(61, atest_char_status2, NA, NA, NA, NA, NA, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[62] = PINGROUP(62, atest_char_status1, NA, NA, NA, NA, NA, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[63] = PINGROUP(63, atest_char_status0, NA, NA, NA, NA, NA, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[64] = PINGROUP(64, atest_char_start, NA, NA, NA, NA, NA, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[65] = PINGROUP(65, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[66] = PINGROUP(66, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[67] = PINGROUP(67, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[68] = PINGROUP(68, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[69] = PINGROUP(69, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0x100008, 15),
	[70] = PINGROUP(70, NA, NA, ebi0_wrcdc, NA, NA, NA, NA, NA, NA, NA, NA,
			NA, NA, 0x100008, 16),
	[71] = PINGROUP(71, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0x100008, 17),
	[72] = PINGROUP(72, ldo_update, NA, gcc_tlmm, NA, NA, NA, NA, NA, NA,
			NA, NA, NA, NA, 0x100008, 18),
	[73] = PINGROUP(73, ldo_en, dbg_out_clk, NA, NA, NA, atest_tsens, NA,
			NA, NA, NA, NA, NA, NA, 0x100008, 19),
	[74] = PINGROUP(74, ebi2_lcd, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			NA, NA, 0x100008, 20),
	[75] = PINGROUP(75, nav_tsync_out_b, nav_ptp_pps_in_b, ptp_pps_out_b,
			NA, qdss_tracedata_a, NA, NA, NA, NA, NA, NA, NA, NA,
			0x100008, 21),
	[76] = PINGROUP(76, pbs_out0, sec_mi2s_data0, blsp3_spi,
			pwr_modem_enabled_a, NA, qdss_tracedata_a, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[77] = PINGROUP(77, pbs_out1, sec_mi2s_data1, blsp2_spi,
			pwr_modem_enabled_b, NA, qdss_tracedata_a, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[78] = PINGROUP(78, pbs_out2, sec_mi2s_sck, blsp1_spi, ebi2_lcd, m_voc,
			pwr_nav_enabled_b, NA, qdss_tracedata_a, NA, NA, NA, NA,
			NA, 0, -1),
	[79] = PINGROUP(79, sec_mi2s_ws, NA, pwr_crypto_enabled_b, NA,
			qdss_tracedata_a, NA, NA, NA, NA, NA, NA, NA, NA, 0,
			-1),
	[80] = SDC_QDSD_PINGROUP(sdc1_rclk, 0x10A000, 0, 0),
	[81] = SDC_QDSD_PINGROUP(sdc1_clk, 0x10A000, 13, 6),
	[82] = SDC_QDSD_PINGROUP(sdc1_cmd, 0x10A000, 11, 3),
	[83] = SDC_QDSD_PINGROUP(sdc1_data, 0x10A000, 9, 0),
	[84] = SDC_QDSD_PINGROUP(sdc2_clk, 0x109000, 14, 6),
	[85] = SDC_QDSD_PINGROUP(sdc2_cmd, 0x109000, 11, 3),
	[86] = SDC_QDSD_PINGROUP(sdc2_data, 0x109000, 9, 0),
	[87] = SDC_QDSD_PINGROUP(qdsd_clk, 0x19C000, 3, 0),
	[88] = SDC_QDSD_PINGROUP(qdsd_cmd, 0x19C000, 8, 5),
	[89] = SDC_QDSD_PINGROUP(qdsd_data0, 0x19C000, 13, 10),
	[90] = SDC_QDSD_PINGROUP(qdsd_data1, 0x19C000, 18, 15),
	[91] = SDC_QDSD_PINGROUP(qdsd_data2, 0x19C000, 23, 20),
	[92] = SDC_QDSD_PINGROUP(qdsd_data3, 0x19C000, 28, 25),
};

static struct pinctrl_qup mdm9607_qup_regs[] = {
};

static const struct msm_gpio_wakeirq_map mdm9607_mpm_map[] = {
	{ 1, 11 }, { 3, 7 }, { 5, 4 }, { 8, 30 }, { 9, 9 }, { 11, 5 }, { 12, 6 },
	{ 13, 10 }, { 16, 3 }, { 17, 8 }, { 20, 12 }, { 21, 13 }, { 22, 14 },
	{ 25, 26 }, { 26, 19 }, { 28, 17 }, { 29, 22 }, { 30, 24 }, { 34, 28 },
	{ 37, 25 }, { 38, 39 }, { 40, 31 }, { 42, 21 }, { 43, 20 }, { 44, 18 },
	{ 48, 32 }, { 52, 33 }, { 55, 29 }, { 57, 34 }, { 59, 37 }, { 62, 35 },
	{ 63, 40 }, { 66, 36 }, { 69, 23 }, { 71, 27 }, { 74, 16 }, { 75, 15 },
	{ 76, 41 }, { 79, 38 },
};

static const struct msm_pinctrl_soc_data mdm9607_pinctrl = {
	.pins = mdm9607_pins,
	.npins = ARRAY_SIZE(mdm9607_pins),
	.functions = mdm9607_functions,
	.nfunctions = ARRAY_SIZE(mdm9607_functions),
	.groups = mdm9607_groups,
	.ngroups = ARRAY_SIZE(mdm9607_groups),
	.ngpios = 80,
	.qup_regs = mdm9607_qup_regs,
	.nqup_regs = ARRAY_SIZE(mdm9607_qup_regs),
	.wakeirq_map = mdm9607_mpm_map,
	.nwakeirq_map = ARRAY_SIZE(mdm9607_mpm_map),
};

static int mdm9607_pinctrl_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &mdm9607_pinctrl);
}

static const struct of_device_id mdm9607_pinctrl_of_match[] = {
	{ .compatible = "qcom,mdm9607-tlmm", },
	{ }
};

static struct platform_driver mdm9607_pinctrl_driver = {
	.driver = {
		.name = "mdm9607-pinctrl",
		.of_match_table = mdm9607_pinctrl_of_match,
	},
	.probe = mdm9607_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init mdm9607_pinctrl_init(void)
{
	return platform_driver_register(&mdm9607_pinctrl_driver);
}
arch_initcall(mdm9607_pinctrl_init);

static void __exit mdm9607_pinctrl_exit(void)
{
	platform_driver_unregister(&mdm9607_pinctrl_driver);
}
module_exit(mdm9607_pinctrl_exit);

MODULE_DESCRIPTION("Qualcomm mdm9607 pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, mdm9607_pinctrl_of_match);

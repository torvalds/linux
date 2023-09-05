// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, Konrad Dybcio <konrad.dybcio@somainline.org>
 *
 * based on pinctrl-msm8916.c
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

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
	PINCTRL_PIN(80, "SDC1_CLK"),
	PINCTRL_PIN(81, "SDC1_CMD"),
	PINCTRL_PIN(82, "SDC1_DATA"),
	PINCTRL_PIN(83, "SDC2_CLK"),
	PINCTRL_PIN(84, "SDC2_CMD"),
	PINCTRL_PIN(85, "SDC2_DATA"),
	PINCTRL_PIN(86, "QDSD_CLK"),
	PINCTRL_PIN(87, "QDSD_CMD"),
	PINCTRL_PIN(88, "QDSD_DATA0"),
	PINCTRL_PIN(89, "QDSD_DATA1"),
	PINCTRL_PIN(90, "QDSD_DATA2"),
	PINCTRL_PIN(91, "QDSD_DATA3"),
};

#define DECLARE_MSM_GPIO_PINS(pin)	\
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

static const unsigned int sdc1_clk_pins[] = { 80 };
static const unsigned int sdc1_cmd_pins[] = { 81 };
static const unsigned int sdc1_data_pins[] = { 82 };
static const unsigned int sdc2_clk_pins[] = { 83 };
static const unsigned int sdc2_cmd_pins[] = { 84 };
static const unsigned int sdc2_data_pins[] = { 85 };
static const unsigned int qdsd_clk_pins[] = { 86 };
static const unsigned int qdsd_cmd_pins[] = { 87 };
static const unsigned int qdsd_data0_pins[] = { 88 };
static const unsigned int qdsd_data1_pins[] = { 89 };
static const unsigned int qdsd_data2_pins[] = { 90 };
static const unsigned int qdsd_data3_pins[] = { 91 };

#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9)	\
	{							\
		.grp = PINCTRL_PINGROUP("gpio" #id, 	\
			gpio##id##_pins, 		\
			ARRAY_SIZE(gpio##id##_pins)),	\
		.funcs = (int[]){				\
			msm_mux_gpio,				\
			msm_mux_##f1,				\
			msm_mux_##f2,				\
			msm_mux_##f3,				\
			msm_mux_##f4,				\
			msm_mux_##f5,				\
			msm_mux_##f6,				\
			msm_mux_##f7,				\
			msm_mux_##f8,				\
			msm_mux_##f9				\
		},					\
		.nfuncs = 10,				\
		.ctl_reg = 0x1000 * id,		\
		.io_reg = 0x4 + 0x1000 * id,			\
		.intr_cfg_reg = 0x8 + 0x1000 * id,		\
		.intr_status_reg = 0xc + 0x1000 * id,		\
		.intr_target_reg = 0x8 + 0x1000 * id,		\
		.mux_bit = 2,					\
		.pull_bit = 0,					\
		.drv_bit = 6,					\
		.oe_bit = 9,					\
		.in_bit = 0,					\
		.out_bit = 1,					\
		.intr_enable_bit = 0,				\
		.intr_status_bit = 0,				\
		.intr_target_bit = 5,				\
		.intr_target_kpss_val = 4,			\
		.intr_raw_status_bit = 4,			\
		.intr_polarity_bit = 1,				\
		.intr_detection_bit = 2,			\
		.intr_detection_width = 2,			\
	}

#define SDC_PINGROUP(pg_name, ctl, pull, drv)	\
	{					        \
		.grp = PINCTRL_PINGROUP(#pg_name, 	\
			pg_name##_pins, 		\
			ARRAY_SIZE(pg_name##_pins)),	\
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
		.intr_target_kpss_val = -1,		\
		.intr_raw_status_bit = -1,		\
		.intr_polarity_bit = -1,		\
		.intr_detection_bit = -1,		\
		.intr_detection_width = -1,		\
	}

enum mdm9607_functions {
	msm_mux_adsp_ext,
	msm_mux_atest_bbrx0,
	msm_mux_atest_bbrx1,
	msm_mux_atest_char,
	msm_mux_atest_char0,
	msm_mux_atest_char1,
	msm_mux_atest_char2,
	msm_mux_atest_char3,
	msm_mux_atest_combodac_to_gpio_native,
	msm_mux_atest_gpsadc_dtest0_native,
	msm_mux_atest_gpsadc_dtest1_native,
	msm_mux_atest_tsens,
	msm_mux_backlight_en_b,
	msm_mux_bimc_dte0,
	msm_mux_bimc_dte1,
	msm_mux_blsp1_spi,
	msm_mux_blsp2_spi,
	msm_mux_blsp3_spi,
	msm_mux_blsp_i2c1,
	msm_mux_blsp_i2c2,
	msm_mux_blsp_i2c3,
	msm_mux_blsp_i2c4,
	msm_mux_blsp_i2c5,
	msm_mux_blsp_i2c6,
	msm_mux_blsp_spi1,
	msm_mux_blsp_spi2,
	msm_mux_blsp_spi3,
	msm_mux_blsp_spi4,
	msm_mux_blsp_spi5,
	msm_mux_blsp_spi6,
	msm_mux_blsp_uart1,
	msm_mux_blsp_uart2,
	msm_mux_blsp_uart3,
	msm_mux_blsp_uart4,
	msm_mux_blsp_uart5,
	msm_mux_blsp_uart6,
	msm_mux_blsp_uim1,
	msm_mux_blsp_uim2,
	msm_mux_codec_int,
	msm_mux_codec_rst,
	msm_mux_coex_uart,
	msm_mux_cri_trng,
	msm_mux_cri_trng0,
	msm_mux_cri_trng1,
	msm_mux_dbg_out,
	msm_mux_ebi0_wrcdc,
	msm_mux_ebi2_a,
	msm_mux_ebi2_a_d_8_b,
	msm_mux_ebi2_lcd,
	msm_mux_ebi2_lcd_cs_n_b,
	msm_mux_ebi2_lcd_te_b,
	msm_mux_eth_irq,
	msm_mux_eth_rst,
	msm_mux_gcc_gp1_clk_a,
	msm_mux_gcc_gp1_clk_b,
	msm_mux_gcc_gp2_clk_a,
	msm_mux_gcc_gp2_clk_b,
	msm_mux_gcc_gp3_clk_a,
	msm_mux_gcc_gp3_clk_b,
	msm_mux_gcc_plltest,
	msm_mux_gcc_tlmm,
	msm_mux_gmac_mdio,
	msm_mux_gpio,
	msm_mux_gsm0_tx,
	msm_mux_lcd_rst,
	msm_mux_ldo_en,
	msm_mux_ldo_update,
	msm_mux_m_voc,
	msm_mux_modem_tsync,
	msm_mux_nav_ptp_pps_in_a,
	msm_mux_nav_ptp_pps_in_b,
	msm_mux_nav_tsync_out_a,
	msm_mux_nav_tsync_out_b,
	msm_mux_pa_indicator,
	msm_mux_pbs0,
	msm_mux_pbs1,
	msm_mux_pbs2,
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
	msm_mux_rcm_marker1,
	msm_mux_rcm_marker2,
	msm_mux_sd_write,
	msm_mux_sec_mi2s,
	msm_mux_sensor_en,
	msm_mux_sensor_int2,
	msm_mux_sensor_int3,
	msm_mux_sensor_rst,
	msm_mux_ssbi1,
	msm_mux_ssbi2,
	msm_mux_touch_rst,
	msm_mux_ts_int,
	msm_mux_uim1_clk,
	msm_mux_uim1_data,
	msm_mux_uim1_present,
	msm_mux_uim1_reset,
	msm_mux_uim2_clk,
	msm_mux_uim2_data,
	msm_mux_uim2_present,
	msm_mux_uim2_reset,
	msm_mux_uim_batt,
	msm_mux_wlan_en1,
	msm_mux__,
};

static const char * const gpio_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3", "gpio4", "gpio5", "gpio6", "gpio7",
	"gpio8", "gpio9", "gpio10", "gpio11", "gpio12", "gpio13", "gpio14",
	"gpio15", "gpio16", "gpio17", "gpio18", "gpio19", "gpio20", "gpio21",
	"gpio22", "gpio23", "gpio24", "gpio25", "gpio26", "gpio27", "gpio28",
	"gpio29", "gpio30", "gpio31", "gpio32", "gpio33", "gpio34", "gpio35",
	"gpio36", "gpio37", "gpio38", "gpio39", "gpio40", "gpio41", "gpio42",
	"gpio43", "gpio44", "gpio45", "gpio46", "gpio47", "gpio48", "gpio49",
	"gpio50", "gpio51", "gpio52", "gpio53", "gpio54", "gpio55", "gpio56",
	"gpio57", "gpio58", "gpio59", "gpio60", "gpio61", "gpio62", "gpio63",
	"gpio64", "gpio65", "gpio66", "gpio67", "gpio68", "gpio69", "gpio70",
	"gpio71", "gpio72", "gpio73", "gpio74", "gpio75", "gpio76", "gpio77",
	"gpio78", "gpio79",
};
static const char * const blsp_spi3_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3",
};
static const char * const blsp_uart3_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3",
};
static const char * const qdss_tracedata_a_groups[] = {
	"gpio0", "gpio1", "gpio4", "gpio5", "gpio20", "gpio21", "gpio22",
	"gpio23", "gpio24", "gpio25", "gpio26", "gpio75", "gpio76", "gpio77",
	"gpio78", "gpio79",
};
static const char * const bimc_dte1_groups[] = {
	"gpio1", "gpio24",
};
static const char * const blsp_i2c3_groups[] = {
	"gpio2", "gpio3",
};
static const char * const qdss_traceclk_a_groups[] = {
	"gpio2",
};
static const char * const bimc_dte0_groups[] = {
	"gpio2", "gpio15",
};
static const char * const qdss_cti_trig_in_a1_groups[] = {
	"gpio3",
};
static const char * const blsp_spi2_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7",
};
static const char * const blsp_uart2_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7",
};
static const char * const blsp_uim2_groups[] = {
	"gpio4", "gpio5",
};
static const char * const blsp_i2c2_groups[] = {
	"gpio6", "gpio7",
};
static const char * const qdss_tracectl_a_groups[] = {
	"gpio6",
};
static const char * const sensor_int2_groups[] = {
	"gpio8",
};
static const char * const blsp_spi5_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio11",
};
static const char * const blsp_uart5_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio11",
};
static const char * const ebi2_lcd_groups[] = {
	"gpio8", "gpio11", "gpio74", "gpio78",
};
static const char * const m_voc_groups[] = {
	"gpio8", "gpio78",
};
static const char * const sensor_int3_groups[] = {
	"gpio9",
};
static const char * const sensor_en_groups[] = {
	"gpio10",
};
static const char * const blsp_i2c5_groups[] = {
	"gpio10", "gpio11",
};
static const char * const ebi2_a_groups[] = {
	"gpio10",
};
static const char * const qdss_tracedata_b_groups[] = {
	"gpio10", "gpio39", "gpio40", "gpio41", "gpio42", "gpio43", "gpio46",
	"gpio47", "gpio48", "gpio51", "gpio52", "gpio53", "gpio54", "gpio55",
	"gpio58", "gpio59",
};
static const char * const sensor_rst_groups[] = {
	"gpio11",
};
static const char * const blsp2_spi_groups[] = {
	"gpio11", "gpio13", "gpio77",
};
static const char * const blsp_spi1_groups[] = {
	"gpio12", "gpio13", "gpio14", "gpio15",
};
static const char * const blsp_uart1_groups[] = {
	"gpio12", "gpio13", "gpio14", "gpio15",
};
static const char * const blsp_uim1_groups[] = {
	"gpio12", "gpio13",
};
static const char * const blsp3_spi_groups[] = {
	"gpio12", "gpio26", "gpio76",
};
static const char * const gcc_gp2_clk_b_groups[] = {
	"gpio12",
};
static const char * const gcc_gp3_clk_b_groups[] = {
	"gpio13",
};
static const char * const blsp_i2c1_groups[] = {
	"gpio14", "gpio15",
};
static const char * const gcc_gp1_clk_b_groups[] = {
	"gpio14",
};
static const char * const blsp_spi4_groups[] = {
	"gpio16", "gpio17", "gpio18", "gpio19",
};
static const char * const blsp_uart4_groups[] = {
	"gpio16", "gpio17", "gpio18", "gpio19",
};
static const char * const rcm_marker1_groups[] = {
	"gpio18",
};
static const char * const blsp_i2c4_groups[] = {
	"gpio18", "gpio19",
};
static const char * const qdss_cti_trig_out_a1_groups[] = {
	"gpio18",
};
static const char * const rcm_marker2_groups[] = {
	"gpio19",
};
static const char * const qdss_cti_trig_out_a0_groups[] = {
	"gpio19",
};
static const char * const blsp_spi6_groups[] = {
	"gpio20", "gpio21", "gpio22", "gpio23",
};
static const char * const blsp_uart6_groups[] = {
	"gpio20", "gpio21", "gpio22", "gpio23",
};
static const char * const pri_mi2s_ws_a_groups[] = {
	"gpio20",
};
static const char * const ebi2_lcd_te_b_groups[] = {
	"gpio20",
};
static const char * const blsp1_spi_groups[] = {
	"gpio20", "gpio21", "gpio78",
};
static const char * const backlight_en_b_groups[] = {
	"gpio21",
};
static const char * const pri_mi2s_data0_a_groups[] = {
	"gpio21",
};
static const char * const pri_mi2s_data1_a_groups[] = {
	"gpio22",
};
static const char * const blsp_i2c6_groups[] = {
	"gpio22", "gpio23",
};
static const char * const ebi2_a_d_8_b_groups[] = {
	"gpio22",
};
static const char * const pri_mi2s_sck_a_groups[] = {
	"gpio23",
};
static const char * const ebi2_lcd_cs_n_b_groups[] = {
	"gpio23",
};
static const char * const touch_rst_groups[] = {
	"gpio24",
};
static const char * const pri_mi2s_mclk_a_groups[] = {
	"gpio24",
};
static const char * const pwr_nav_enabled_a_groups[] = {
	"gpio24",
};
static const char * const ts_int_groups[] = {
	"gpio25",
};
static const char * const sd_write_groups[] = {
	"gpio25",
};
static const char * const pwr_crypto_enabled_a_groups[] = {
	"gpio25",
};
static const char * const codec_rst_groups[] = {
	"gpio26",
};
static const char * const adsp_ext_groups[] = {
	"gpio26",
};
static const char * const atest_combodac_to_gpio_native_groups[] = {
	"gpio26", "gpio27", "gpio28", "gpio29", "gpio30", "gpio31", "gpio32",
	"gpio33", "gpio34", "gpio35", "gpio41", "gpio45", "gpio49", "gpio50",
	"gpio51", "gpio52", "gpio54", "gpio55", "gpio57", "gpio59",
};
static const char * const uim2_data_groups[] = {
	"gpio27",
};
static const char * const gmac_mdio_groups[] = {
	"gpio27", "gpio28",
};
static const char * const gcc_gp1_clk_a_groups[] = {
	"gpio27",
};
static const char * const uim2_clk_groups[] = {
	"gpio28",
};
static const char * const gcc_gp2_clk_a_groups[] = {
	"gpio28",
};
static const char * const eth_irq_groups[] = {
	"gpio29",
};
static const char * const uim2_reset_groups[] = {
	"gpio29",
};
static const char * const gcc_gp3_clk_a_groups[] = {
	"gpio29",
};
static const char * const eth_rst_groups[] = {
	"gpio30",
};
static const char * const uim2_present_groups[] = {
	"gpio30",
};
static const char * const prng_rosc_groups[] = {
	"gpio30",
};
static const char * const uim1_data_groups[] = {
	"gpio31",
};
static const char * const uim1_clk_groups[] = {
	"gpio32",
};
static const char * const uim1_reset_groups[] = {
	"gpio33",
};
static const char * const uim1_present_groups[] = {
	"gpio34",
};
static const char * const gcc_plltest_groups[] = {
	"gpio34", "gpio35",
};
static const char * const uim_batt_groups[] = {
	"gpio35",
};
static const char * const coex_uart_groups[] = {
	"gpio36", "gpio37",
};
static const char * const codec_int_groups[] = {
	"gpio38",
};
static const char * const qdss_cti_trig_in_a0_groups[] = {
	"gpio38",
};
static const char * const atest_bbrx1_groups[] = {
	"gpio39",
};
static const char * const cri_trng0_groups[] = {
	"gpio40",
};
static const char * const atest_bbrx0_groups[] = {
	"gpio40",
};
static const char * const cri_trng_groups[] = {
	"gpio42",
};
static const char * const qdss_cti_trig_in_b0_groups[] = {
	"gpio44",
};
static const char * const atest_gpsadc_dtest0_native_groups[] = {
	"gpio44",
};
static const char * const qdss_cti_trig_out_b0_groups[] = {
	"gpio45",
};
static const char * const qdss_tracectl_b_groups[] = {
	"gpio49",
};
static const char * const qdss_traceclk_b_groups[] = {
	"gpio50",
};
static const char * const pa_indicator_groups[] = {
	"gpio51",
};
static const char * const modem_tsync_groups[] = {
	"gpio53",
};
static const char * const nav_tsync_out_a_groups[] = {
	"gpio53",
};
static const char * const nav_ptp_pps_in_a_groups[] = {
	"gpio53",
};
static const char * const ptp_pps_out_a_groups[] = {
	"gpio53",
};
static const char * const gsm0_tx_groups[] = {
	"gpio55",
};
static const char * const qdss_cti_trig_in_b1_groups[] = {
	"gpio56",
};
static const char * const cri_trng1_groups[] = {
	"gpio57",
};
static const char * const qdss_cti_trig_out_b1_groups[] = {
	"gpio57",
};
static const char * const ssbi1_groups[] = {
	"gpio58",
};
static const char * const atest_gpsadc_dtest1_native_groups[] = {
	"gpio58",
};
static const char * const ssbi2_groups[] = {
	"gpio59",
};
static const char * const atest_char3_groups[] = {
	"gpio60",
};
static const char * const atest_char2_groups[] = {
	"gpio61",
};
static const char * const atest_char1_groups[] = {
	"gpio62",
};
static const char * const atest_char0_groups[] = {
	"gpio63",
};
static const char * const atest_char_groups[] = {
	"gpio64",
};
static const char * const ebi0_wrcdc_groups[] = {
	"gpio70",
};
static const char * const ldo_update_groups[] = {
	"gpio72",
};
static const char * const gcc_tlmm_groups[] = {
	"gpio72",
};
static const char * const ldo_en_groups[] = {
	"gpio73",
};
static const char * const dbg_out_groups[] = {
	"gpio73",
};
static const char * const atest_tsens_groups[] = {
	"gpio73",
};
static const char * const lcd_rst_groups[] = {
	"gpio74",
};
static const char * const wlan_en1_groups[] = {
	"gpio75",
};
static const char * const nav_tsync_out_b_groups[] = {
	"gpio75",
};
static const char * const nav_ptp_pps_in_b_groups[] = {
	"gpio75",
};
static const char * const ptp_pps_out_b_groups[] = {
	"gpio75",
};
static const char * const pbs0_groups[] = {
	"gpio76",
};
static const char * const sec_mi2s_groups[] = {
	"gpio76", "gpio77", "gpio78", "gpio79",
};
static const char * const pwr_modem_enabled_a_groups[] = {
	"gpio76",
};
static const char * const pbs1_groups[] = {
	"gpio77",
};
static const char * const pwr_modem_enabled_b_groups[] = {
	"gpio77",
};
static const char * const pbs2_groups[] = {
	"gpio78",
};
static const char * const pwr_nav_enabled_b_groups[] = {
	"gpio78",
};
static const char * const pwr_crypto_enabled_b_groups[] = {
	"gpio79",
};

static const struct pinfunction mdm9607_functions[] = {
	MSM_PIN_FUNCTION(adsp_ext),
	MSM_PIN_FUNCTION(atest_bbrx0),
	MSM_PIN_FUNCTION(atest_bbrx1),
	MSM_PIN_FUNCTION(atest_char),
	MSM_PIN_FUNCTION(atest_char0),
	MSM_PIN_FUNCTION(atest_char1),
	MSM_PIN_FUNCTION(atest_char2),
	MSM_PIN_FUNCTION(atest_char3),
	MSM_PIN_FUNCTION(atest_combodac_to_gpio_native),
	MSM_PIN_FUNCTION(atest_gpsadc_dtest0_native),
	MSM_PIN_FUNCTION(atest_gpsadc_dtest1_native),
	MSM_PIN_FUNCTION(atest_tsens),
	MSM_PIN_FUNCTION(backlight_en_b),
	MSM_PIN_FUNCTION(bimc_dte0),
	MSM_PIN_FUNCTION(bimc_dte1),
	MSM_PIN_FUNCTION(blsp1_spi),
	MSM_PIN_FUNCTION(blsp2_spi),
	MSM_PIN_FUNCTION(blsp3_spi),
	MSM_PIN_FUNCTION(blsp_i2c1),
	MSM_PIN_FUNCTION(blsp_i2c2),
	MSM_PIN_FUNCTION(blsp_i2c3),
	MSM_PIN_FUNCTION(blsp_i2c4),
	MSM_PIN_FUNCTION(blsp_i2c5),
	MSM_PIN_FUNCTION(blsp_i2c6),
	MSM_PIN_FUNCTION(blsp_spi1),
	MSM_PIN_FUNCTION(blsp_spi2),
	MSM_PIN_FUNCTION(blsp_spi3),
	MSM_PIN_FUNCTION(blsp_spi4),
	MSM_PIN_FUNCTION(blsp_spi5),
	MSM_PIN_FUNCTION(blsp_spi6),
	MSM_PIN_FUNCTION(blsp_uart1),
	MSM_PIN_FUNCTION(blsp_uart2),
	MSM_PIN_FUNCTION(blsp_uart3),
	MSM_PIN_FUNCTION(blsp_uart4),
	MSM_PIN_FUNCTION(blsp_uart5),
	MSM_PIN_FUNCTION(blsp_uart6),
	MSM_PIN_FUNCTION(blsp_uim1),
	MSM_PIN_FUNCTION(blsp_uim2),
	MSM_PIN_FUNCTION(codec_int),
	MSM_PIN_FUNCTION(codec_rst),
	MSM_PIN_FUNCTION(coex_uart),
	MSM_PIN_FUNCTION(cri_trng),
	MSM_PIN_FUNCTION(cri_trng0),
	MSM_PIN_FUNCTION(cri_trng1),
	MSM_PIN_FUNCTION(dbg_out),
	MSM_PIN_FUNCTION(ebi0_wrcdc),
	MSM_PIN_FUNCTION(ebi2_a),
	MSM_PIN_FUNCTION(ebi2_a_d_8_b),
	MSM_PIN_FUNCTION(ebi2_lcd),
	MSM_PIN_FUNCTION(ebi2_lcd_cs_n_b),
	MSM_PIN_FUNCTION(ebi2_lcd_te_b),
	MSM_PIN_FUNCTION(eth_irq),
	MSM_PIN_FUNCTION(eth_rst),
	MSM_PIN_FUNCTION(gcc_gp1_clk_a),
	MSM_PIN_FUNCTION(gcc_gp1_clk_b),
	MSM_PIN_FUNCTION(gcc_gp2_clk_a),
	MSM_PIN_FUNCTION(gcc_gp2_clk_b),
	MSM_PIN_FUNCTION(gcc_gp3_clk_a),
	MSM_PIN_FUNCTION(gcc_gp3_clk_b),
	MSM_PIN_FUNCTION(gcc_plltest),
	MSM_PIN_FUNCTION(gcc_tlmm),
	MSM_PIN_FUNCTION(gmac_mdio),
	MSM_PIN_FUNCTION(gpio),
	MSM_PIN_FUNCTION(gsm0_tx),
	MSM_PIN_FUNCTION(lcd_rst),
	MSM_PIN_FUNCTION(ldo_en),
	MSM_PIN_FUNCTION(ldo_update),
	MSM_PIN_FUNCTION(m_voc),
	MSM_PIN_FUNCTION(modem_tsync),
	MSM_PIN_FUNCTION(nav_ptp_pps_in_a),
	MSM_PIN_FUNCTION(nav_ptp_pps_in_b),
	MSM_PIN_FUNCTION(nav_tsync_out_a),
	MSM_PIN_FUNCTION(nav_tsync_out_b),
	MSM_PIN_FUNCTION(pa_indicator),
	MSM_PIN_FUNCTION(pbs0),
	MSM_PIN_FUNCTION(pbs1),
	MSM_PIN_FUNCTION(pbs2),
	MSM_PIN_FUNCTION(pri_mi2s_data0_a),
	MSM_PIN_FUNCTION(pri_mi2s_data1_a),
	MSM_PIN_FUNCTION(pri_mi2s_mclk_a),
	MSM_PIN_FUNCTION(pri_mi2s_sck_a),
	MSM_PIN_FUNCTION(pri_mi2s_ws_a),
	MSM_PIN_FUNCTION(prng_rosc),
	MSM_PIN_FUNCTION(ptp_pps_out_a),
	MSM_PIN_FUNCTION(ptp_pps_out_b),
	MSM_PIN_FUNCTION(pwr_crypto_enabled_a),
	MSM_PIN_FUNCTION(pwr_crypto_enabled_b),
	MSM_PIN_FUNCTION(pwr_modem_enabled_a),
	MSM_PIN_FUNCTION(pwr_modem_enabled_b),
	MSM_PIN_FUNCTION(pwr_nav_enabled_a),
	MSM_PIN_FUNCTION(pwr_nav_enabled_b),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_a0),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_a1),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_b0),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_b1),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_a0),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_a1),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_b0),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_b1),
	MSM_PIN_FUNCTION(qdss_traceclk_a),
	MSM_PIN_FUNCTION(qdss_traceclk_b),
	MSM_PIN_FUNCTION(qdss_tracectl_a),
	MSM_PIN_FUNCTION(qdss_tracectl_b),
	MSM_PIN_FUNCTION(qdss_tracedata_a),
	MSM_PIN_FUNCTION(qdss_tracedata_b),
	MSM_PIN_FUNCTION(rcm_marker1),
	MSM_PIN_FUNCTION(rcm_marker2),
	MSM_PIN_FUNCTION(sd_write),
	MSM_PIN_FUNCTION(sec_mi2s),
	MSM_PIN_FUNCTION(sensor_en),
	MSM_PIN_FUNCTION(sensor_int2),
	MSM_PIN_FUNCTION(sensor_int3),
	MSM_PIN_FUNCTION(sensor_rst),
	MSM_PIN_FUNCTION(ssbi1),
	MSM_PIN_FUNCTION(ssbi2),
	MSM_PIN_FUNCTION(touch_rst),
	MSM_PIN_FUNCTION(ts_int),
	MSM_PIN_FUNCTION(uim1_clk),
	MSM_PIN_FUNCTION(uim1_data),
	MSM_PIN_FUNCTION(uim1_present),
	MSM_PIN_FUNCTION(uim1_reset),
	MSM_PIN_FUNCTION(uim2_clk),
	MSM_PIN_FUNCTION(uim2_data),
	MSM_PIN_FUNCTION(uim2_present),
	MSM_PIN_FUNCTION(uim2_reset),
	MSM_PIN_FUNCTION(uim_batt),
	MSM_PIN_FUNCTION(wlan_en1)
};

static const struct msm_pingroup mdm9607_groups[] = {
	PINGROUP(0, blsp_uart3, blsp_spi3, _, _, _, _, _, qdss_tracedata_a, _),
	PINGROUP(1, blsp_uart3, blsp_spi3, _, _, _, _, _, qdss_tracedata_a, bimc_dte1),
	PINGROUP(2, blsp_uart3, blsp_i2c3, blsp_spi3, _, _, _, _, _, qdss_traceclk_a),
	PINGROUP(3, blsp_uart3, blsp_i2c3, blsp_spi3, _, _, _, _, _, _),
	PINGROUP(4, blsp_spi2, blsp_uart2, blsp_uim2, _, _, _, _, qdss_tracedata_a, _),
	PINGROUP(5, blsp_spi2, blsp_uart2, blsp_uim2, _, _, _, _, qdss_tracedata_a, _),
	PINGROUP(6, blsp_spi2, blsp_uart2, blsp_i2c2, _, _, _, _, _, _),
	PINGROUP(7, blsp_spi2, blsp_uart2, blsp_i2c2, _, _, _, _, _, _),
	PINGROUP(8, blsp_spi5, blsp_uart5, ebi2_lcd, m_voc, _, _, _, _, _),
	PINGROUP(9, blsp_spi5, blsp_uart5, _, _, _, _, _, _, _),
	PINGROUP(10, blsp_spi5, blsp_i2c5, blsp_uart5, ebi2_a, _, _, qdss_tracedata_b, _, _),
	PINGROUP(11, blsp_spi5, blsp_i2c5, blsp_uart5, blsp2_spi, ebi2_lcd, _, _, _, _),
	PINGROUP(12, blsp_spi1, blsp_uart1, blsp_uim1, blsp3_spi, gcc_gp2_clk_b, _, _, _, _),
	PINGROUP(13, blsp_spi1, blsp_uart1, blsp_uim1, blsp2_spi, gcc_gp3_clk_b, _, _, _, _),
	PINGROUP(14, blsp_spi1, blsp_uart1, blsp_i2c1, gcc_gp1_clk_b, _, _, _, _, _),
	PINGROUP(15, blsp_spi1, blsp_uart1, blsp_i2c1, _, _, _, _, _, _),
	PINGROUP(16, blsp_spi4, blsp_uart4, _, _, _, _, _, _, _),
	PINGROUP(17, blsp_spi4, blsp_uart4, _, _, _, _, _, _, _),
	PINGROUP(18, blsp_spi4, blsp_uart4, blsp_i2c4, _, _, _, _, _, _),
	PINGROUP(19, blsp_spi4, blsp_uart4, blsp_i2c4, _, _, _, _, _, _),
	PINGROUP(20, blsp_spi6, blsp_uart6, pri_mi2s_ws_a, ebi2_lcd_te_b, blsp1_spi, _, _, _,
		 qdss_tracedata_a),
	PINGROUP(21, blsp_spi6, blsp_uart6, pri_mi2s_data0_a, blsp1_spi, _, _, _, _, _),
	PINGROUP(22, blsp_spi6, blsp_uart6, pri_mi2s_data1_a, blsp_i2c6, ebi2_a_d_8_b, _, _, _, _),
	PINGROUP(23, blsp_spi6, blsp_uart6, pri_mi2s_sck_a, blsp_i2c6, ebi2_lcd_cs_n_b, _, _, _, _),
	PINGROUP(24, pri_mi2s_mclk_a, _, pwr_nav_enabled_a, _, _, _, _, qdss_tracedata_a,
		 bimc_dte1),
	PINGROUP(25, sd_write, _, pwr_crypto_enabled_a, _, _, _, _, qdss_tracedata_a, _),
	PINGROUP(26, blsp3_spi, adsp_ext, _, qdss_tracedata_a, _, atest_combodac_to_gpio_native, _,
		 _, _),
	PINGROUP(27, uim2_data, gmac_mdio, gcc_gp1_clk_a, _, _, atest_combodac_to_gpio_native, _, _,
		 _),
	PINGROUP(28, uim2_clk, gmac_mdio, gcc_gp2_clk_a, _, _, atest_combodac_to_gpio_native, _, _,
		 _),
	PINGROUP(29, uim2_reset, gcc_gp3_clk_a, _, _, atest_combodac_to_gpio_native, _, _, _, _),
	PINGROUP(30, uim2_present, prng_rosc, _, _, atest_combodac_to_gpio_native, _, _, _, _),
	PINGROUP(31, uim1_data, _, _, atest_combodac_to_gpio_native, _, _, _, _, _),
	PINGROUP(32, uim1_clk, _, _, atest_combodac_to_gpio_native, _, _, _, _, _),
	PINGROUP(33, uim1_reset, _, _, atest_combodac_to_gpio_native, _, _, _, _, _),
	PINGROUP(34, uim1_present, gcc_plltest, _, _, atest_combodac_to_gpio_native, _, _, _, _),
	PINGROUP(35, uim_batt, gcc_plltest, _, atest_combodac_to_gpio_native, _, _, _, _, _),
	PINGROUP(36, coex_uart, _, _, _, _, _, _, _, _),
	PINGROUP(37, coex_uart, _, _, _, _, _, _, _, _),
	PINGROUP(38, _, _, _, qdss_cti_trig_in_a0, _, _, _, _, _),
	PINGROUP(39, _, _, _, qdss_tracedata_b, _, atest_bbrx1, _, _, _),
	PINGROUP(40, _, cri_trng0, _, _, _, _, qdss_tracedata_b, _, atest_bbrx0),
	PINGROUP(41, _, _, _, _, _, qdss_tracedata_b, _, atest_combodac_to_gpio_native, _),
	PINGROUP(42, _, cri_trng, _, _, qdss_tracedata_b, _, _, _, _),
	PINGROUP(43, _, _, _, _, qdss_tracedata_b, _, _, _, _),
	PINGROUP(44, _, _, qdss_cti_trig_in_b0, _, atest_gpsadc_dtest0_native, _, _, _, _),
	PINGROUP(45, _, _, qdss_cti_trig_out_b0, _, atest_combodac_to_gpio_native, _, _, _, _),
	PINGROUP(46, _, _, qdss_tracedata_b, _, _, _, _, _, _),
	PINGROUP(47, _, _, qdss_tracedata_b, _, _, _, _, _, _),
	PINGROUP(48, _, _, qdss_tracedata_b, _, _, _, _, _, _),
	PINGROUP(49, _, _, qdss_tracectl_b, _, atest_combodac_to_gpio_native, _, _, _, _),
	PINGROUP(50, _, _, qdss_traceclk_b, _, atest_combodac_to_gpio_native, _, _, _, _),
	PINGROUP(51, _, pa_indicator, _, qdss_tracedata_b, _, atest_combodac_to_gpio_native, _, _,
		 _),
	PINGROUP(52, _, _, _, qdss_tracedata_b, _, atest_combodac_to_gpio_native, _, _, _),
	PINGROUP(53, _, modem_tsync, nav_tsync_out_a, nav_ptp_pps_in_a, ptp_pps_out_a,
		 qdss_tracedata_b, _, _, _),
	PINGROUP(54, _, qdss_tracedata_b, _, atest_combodac_to_gpio_native, _, _, _, _, _),
	PINGROUP(55, gsm0_tx, _, qdss_tracedata_b, _, atest_combodac_to_gpio_native, _, _, _, _),
	PINGROUP(56, _, _, qdss_cti_trig_in_b1, _, _, _, _, _, _),
	PINGROUP(57, _, cri_trng1, _, qdss_cti_trig_out_b1, _, atest_combodac_to_gpio_native, _, _,
		 _),
	PINGROUP(58, _, ssbi1, _, qdss_tracedata_b, _, atest_gpsadc_dtest1_native, _, _, _),
	PINGROUP(59, _, ssbi2, _, qdss_tracedata_b, _, atest_combodac_to_gpio_native, _, _, _),
	PINGROUP(60, atest_char3, _, _, _, _, _, _, _, _),
	PINGROUP(61, atest_char2, _, _, _, _, _, _, _, _),
	PINGROUP(62, atest_char1, _, _, _, _, _, _, _, _),
	PINGROUP(63, atest_char0, _, _, _, _, _, _, _, _),
	PINGROUP(64, atest_char, _, _, _, _, _, _, _, _),
	PINGROUP(65, _, _, _, _, _, _, _, _, _),
	PINGROUP(66, _, _, _, _, _, _, _, _, _),
	PINGROUP(67, _, _, _, _, _, _, _, _, _),
	PINGROUP(68, _, _, _, _, _, _, _, _, _),
	PINGROUP(69, _, _, _, _, _, _, _, _, _),
	PINGROUP(70, _, _, ebi0_wrcdc, _, _, _, _, _, _),
	PINGROUP(71, _, _, _, _, _, _, _, _, _),
	PINGROUP(72, ldo_update, _, gcc_tlmm, _, _, _, _, _, _),
	PINGROUP(73, ldo_en, dbg_out, _, _, _, atest_tsens, _, _, _),
	PINGROUP(74, ebi2_lcd, _, _, _, _, _, _, _, _),
	PINGROUP(75, nav_tsync_out_b, nav_ptp_pps_in_b, ptp_pps_out_b, _, qdss_tracedata_a, _, _, _,
		 _),
	PINGROUP(76, pbs0, sec_mi2s, blsp3_spi, pwr_modem_enabled_a, _, qdss_tracedata_a, _, _, _),
	PINGROUP(77, pbs1, sec_mi2s, blsp2_spi, pwr_modem_enabled_b, _, qdss_tracedata_a, _, _, _),
	PINGROUP(78, pbs2, sec_mi2s, blsp1_spi, ebi2_lcd, m_voc, pwr_nav_enabled_b, _,
		 qdss_tracedata_a, _),
	PINGROUP(79, sec_mi2s, _, pwr_crypto_enabled_b, _, qdss_tracedata_a, _, _, _, _),
	SDC_PINGROUP(sdc1_clk, 0x10a000, 13, 6),
	SDC_PINGROUP(sdc1_cmd, 0x10a000, 11, 3),
	SDC_PINGROUP(sdc1_data, 0x10a000, 9, 0),
	SDC_PINGROUP(sdc2_clk, 0x109000, 14, 6),
	SDC_PINGROUP(sdc2_cmd, 0x109000, 11, 3),
	SDC_PINGROUP(sdc2_data, 0x109000, 9, 0),
	SDC_PINGROUP(qdsd_clk, 0x19c000, 3, 0),
	SDC_PINGROUP(qdsd_cmd, 0x19c000, 8, 5),
	SDC_PINGROUP(qdsd_data0, 0x19c000, 13, 10),
	SDC_PINGROUP(qdsd_data1, 0x19c000, 18, 15),
	SDC_PINGROUP(qdsd_data2, 0x19c000, 23, 20),
	SDC_PINGROUP(qdsd_data3, 0x19c000, 28, 25),
};

static const struct msm_pinctrl_soc_data mdm9607_pinctrl = {
	.pins = mdm9607_pins,
	.npins = ARRAY_SIZE(mdm9607_pins),
	.functions = mdm9607_functions,
	.nfunctions = ARRAY_SIZE(mdm9607_functions),
	.groups = mdm9607_groups,
	.ngroups = ARRAY_SIZE(mdm9607_groups),
	.ngpios = 80,
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

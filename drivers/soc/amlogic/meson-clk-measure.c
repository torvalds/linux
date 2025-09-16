// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2018 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/bitfield.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/regmap.h>
#include <linux/module.h>

static DEFINE_MUTEX(measure_lock);

#define MSR_DURATION		GENMASK(15, 0)
#define MSR_ENABLE		BIT(16)
#define MSR_CONT		BIT(17) /* continuous measurement */
#define MSR_INTR		BIT(18) /* interrupts */
#define MSR_RUN			BIT(19)
#define MSR_CLK_SRC		GENMASK(26, 20)
#define MSR_BUSY		BIT(31)

#define MSR_VAL_MASK		GENMASK(15, 0)

#define DIV_MIN			32
#define DIV_STEP		32
#define DIV_MAX			640

struct meson_msr_id {
	struct meson_msr *priv;
	unsigned int id;
	const char *name;
};

struct msr_reg_offset {
	unsigned int duty_val;
	unsigned int freq_ctrl;
	unsigned int duty_ctrl;
	unsigned int freq_val;
};

struct meson_msr_data {
	struct meson_msr_id *msr_table;
	unsigned int msr_count;
	const struct msr_reg_offset *reg;
};

struct meson_msr {
	struct regmap *regmap;
	struct meson_msr_data data;
};

#define CLK_MSR_ID(__id, __name) \
	[__id] = {.id = __id, .name = __name,}

static const struct meson_msr_id clk_msr_m8[] = {
	CLK_MSR_ID(0, "ring_osc_out_ee0"),
	CLK_MSR_ID(1, "ring_osc_out_ee1"),
	CLK_MSR_ID(2, "ring_osc_out_ee2"),
	CLK_MSR_ID(3, "a9_ring_osck"),
	CLK_MSR_ID(6, "vid_pll"),
	CLK_MSR_ID(7, "clk81"),
	CLK_MSR_ID(8, "encp"),
	CLK_MSR_ID(9, "encl"),
	CLK_MSR_ID(11, "eth_rmii"),
	CLK_MSR_ID(13, "amclk"),
	CLK_MSR_ID(14, "fec_clk_0"),
	CLK_MSR_ID(15, "fec_clk_1"),
	CLK_MSR_ID(16, "fec_clk_2"),
	CLK_MSR_ID(18, "a9_clk_div16"),
	CLK_MSR_ID(19, "hdmi_sys"),
	CLK_MSR_ID(20, "rtc_osc_clk_out"),
	CLK_MSR_ID(21, "i2s_clk_in_src0"),
	CLK_MSR_ID(22, "clk_rmii_from_pad"),
	CLK_MSR_ID(23, "hdmi_ch0_tmds"),
	CLK_MSR_ID(24, "lvds_fifo"),
	CLK_MSR_ID(26, "sc_clk_int"),
	CLK_MSR_ID(28, "sar_adc"),
	CLK_MSR_ID(30, "mpll_clk_test_out"),
	CLK_MSR_ID(31, "audac_clkpi"),
	CLK_MSR_ID(32, "vdac"),
	CLK_MSR_ID(33, "sdhc_rx"),
	CLK_MSR_ID(34, "sdhc_sd"),
	CLK_MSR_ID(35, "mali"),
	CLK_MSR_ID(36, "hdmi_tx_pixel"),
	CLK_MSR_ID(38, "vdin_meas"),
	CLK_MSR_ID(39, "pcm_sclk"),
	CLK_MSR_ID(40, "pcm_mclk"),
	CLK_MSR_ID(41, "eth_rx_tx"),
	CLK_MSR_ID(42, "pwm_d"),
	CLK_MSR_ID(43, "pwm_c"),
	CLK_MSR_ID(44, "pwm_b"),
	CLK_MSR_ID(45, "pwm_a"),
	CLK_MSR_ID(46, "pcm2_sclk"),
	CLK_MSR_ID(47, "ddr_dpll_pt"),
	CLK_MSR_ID(48, "pwm_f"),
	CLK_MSR_ID(49, "pwm_e"),
	CLK_MSR_ID(59, "hcodec"),
	CLK_MSR_ID(60, "usb_32k_alt"),
	CLK_MSR_ID(61, "gpio"),
	CLK_MSR_ID(62, "vid2_pll"),
	CLK_MSR_ID(63, "mipi_csi_cfg"),
};

static const struct meson_msr_id clk_msr_gx[] = {
	CLK_MSR_ID(0, "ring_osc_out_ee_0"),
	CLK_MSR_ID(1, "ring_osc_out_ee_1"),
	CLK_MSR_ID(2, "ring_osc_out_ee_2"),
	CLK_MSR_ID(3, "a53_ring_osc"),
	CLK_MSR_ID(4, "gp0_pll"),
	CLK_MSR_ID(6, "enci"),
	CLK_MSR_ID(7, "clk81"),
	CLK_MSR_ID(8, "encp"),
	CLK_MSR_ID(9, "encl"),
	CLK_MSR_ID(10, "vdac"),
	CLK_MSR_ID(11, "rgmii_tx"),
	CLK_MSR_ID(12, "pdm"),
	CLK_MSR_ID(13, "amclk"),
	CLK_MSR_ID(14, "fec_0"),
	CLK_MSR_ID(15, "fec_1"),
	CLK_MSR_ID(16, "fec_2"),
	CLK_MSR_ID(17, "sys_pll_div16"),
	CLK_MSR_ID(18, "sys_cpu_div16"),
	CLK_MSR_ID(19, "hdmitx_sys"),
	CLK_MSR_ID(20, "rtc_osc_out"),
	CLK_MSR_ID(21, "i2s_in_src0"),
	CLK_MSR_ID(22, "eth_phy_ref"),
	CLK_MSR_ID(23, "hdmi_todig"),
	CLK_MSR_ID(26, "sc_int"),
	CLK_MSR_ID(28, "sar_adc"),
	CLK_MSR_ID(31, "mpll_test_out"),
	CLK_MSR_ID(32, "vdec"),
	CLK_MSR_ID(35, "mali"),
	CLK_MSR_ID(36, "hdmi_tx_pixel"),
	CLK_MSR_ID(37, "i958"),
	CLK_MSR_ID(38, "vdin_meas"),
	CLK_MSR_ID(39, "pcm_sclk"),
	CLK_MSR_ID(40, "pcm_mclk"),
	CLK_MSR_ID(41, "eth_rx_or_rmii"),
	CLK_MSR_ID(42, "mp0_out"),
	CLK_MSR_ID(43, "fclk_div5"),
	CLK_MSR_ID(44, "pwm_b"),
	CLK_MSR_ID(45, "pwm_a"),
	CLK_MSR_ID(46, "vpu"),
	CLK_MSR_ID(47, "ddr_dpll_pt"),
	CLK_MSR_ID(48, "mp1_out"),
	CLK_MSR_ID(49, "mp2_out"),
	CLK_MSR_ID(50, "mp3_out"),
	CLK_MSR_ID(51, "nand_core"),
	CLK_MSR_ID(52, "sd_emmc_b"),
	CLK_MSR_ID(53, "sd_emmc_a"),
	CLK_MSR_ID(55, "vid_pll_div_out"),
	CLK_MSR_ID(56, "cci"),
	CLK_MSR_ID(57, "wave420l_c"),
	CLK_MSR_ID(58, "wave420l_b"),
	CLK_MSR_ID(59, "hcodec"),
	CLK_MSR_ID(60, "alt_32k"),
	CLK_MSR_ID(61, "gpio_msr"),
	CLK_MSR_ID(62, "hevc"),
	CLK_MSR_ID(66, "vid_lock"),
	CLK_MSR_ID(70, "pwm_f"),
	CLK_MSR_ID(71, "pwm_e"),
	CLK_MSR_ID(72, "pwm_d"),
	CLK_MSR_ID(73, "pwm_c"),
	CLK_MSR_ID(75, "aoclkx2_int"),
	CLK_MSR_ID(76, "aoclk_int"),
	CLK_MSR_ID(77, "rng_ring_osc_0"),
	CLK_MSR_ID(78, "rng_ring_osc_1"),
	CLK_MSR_ID(79, "rng_ring_osc_2"),
	CLK_MSR_ID(80, "rng_ring_osc_3"),
	CLK_MSR_ID(81, "vapb"),
	CLK_MSR_ID(82, "ge2d"),
};

static const struct meson_msr_id clk_msr_axg[] = {
	CLK_MSR_ID(0, "ring_osc_out_ee_0"),
	CLK_MSR_ID(1, "ring_osc_out_ee_1"),
	CLK_MSR_ID(2, "ring_osc_out_ee_2"),
	CLK_MSR_ID(3, "a53_ring_osc"),
	CLK_MSR_ID(4, "gp0_pll"),
	CLK_MSR_ID(5, "gp1_pll"),
	CLK_MSR_ID(7, "clk81"),
	CLK_MSR_ID(9, "encl"),
	CLK_MSR_ID(17, "sys_pll_div16"),
	CLK_MSR_ID(18, "sys_cpu_div16"),
	CLK_MSR_ID(20, "rtc_osc_out"),
	CLK_MSR_ID(23, "mmc_clk"),
	CLK_MSR_ID(28, "sar_adc"),
	CLK_MSR_ID(31, "mpll_test_out"),
	CLK_MSR_ID(40, "mod_eth_tx_clk"),
	CLK_MSR_ID(41, "mod_eth_rx_clk_rmii"),
	CLK_MSR_ID(42, "mp0_out"),
	CLK_MSR_ID(43, "fclk_div5"),
	CLK_MSR_ID(44, "pwm_b"),
	CLK_MSR_ID(45, "pwm_a"),
	CLK_MSR_ID(46, "vpu"),
	CLK_MSR_ID(47, "ddr_dpll_pt"),
	CLK_MSR_ID(48, "mp1_out"),
	CLK_MSR_ID(49, "mp2_out"),
	CLK_MSR_ID(50, "mp3_out"),
	CLK_MSR_ID(51, "sd_emmm_c"),
	CLK_MSR_ID(52, "sd_emmc_b"),
	CLK_MSR_ID(61, "gpio_msr"),
	CLK_MSR_ID(66, "audio_slv_lrclk_c"),
	CLK_MSR_ID(67, "audio_slv_lrclk_b"),
	CLK_MSR_ID(68, "audio_slv_lrclk_a"),
	CLK_MSR_ID(69, "audio_slv_sclk_c"),
	CLK_MSR_ID(70, "audio_slv_sclk_b"),
	CLK_MSR_ID(71, "audio_slv_sclk_a"),
	CLK_MSR_ID(72, "pwm_d"),
	CLK_MSR_ID(73, "pwm_c"),
	CLK_MSR_ID(74, "wifi_beacon"),
	CLK_MSR_ID(75, "tdmin_lb_lrcl"),
	CLK_MSR_ID(76, "tdmin_lb_sclk"),
	CLK_MSR_ID(77, "rng_ring_osc_0"),
	CLK_MSR_ID(78, "rng_ring_osc_1"),
	CLK_MSR_ID(79, "rng_ring_osc_2"),
	CLK_MSR_ID(80, "rng_ring_osc_3"),
	CLK_MSR_ID(81, "vapb"),
	CLK_MSR_ID(82, "ge2d"),
	CLK_MSR_ID(84, "audio_resample"),
	CLK_MSR_ID(85, "audio_pdm_sys"),
	CLK_MSR_ID(86, "audio_spdifout"),
	CLK_MSR_ID(87, "audio_spdifin"),
	CLK_MSR_ID(88, "audio_lrclk_f"),
	CLK_MSR_ID(89, "audio_lrclk_e"),
	CLK_MSR_ID(90, "audio_lrclk_d"),
	CLK_MSR_ID(91, "audio_lrclk_c"),
	CLK_MSR_ID(92, "audio_lrclk_b"),
	CLK_MSR_ID(93, "audio_lrclk_a"),
	CLK_MSR_ID(94, "audio_sclk_f"),
	CLK_MSR_ID(95, "audio_sclk_e"),
	CLK_MSR_ID(96, "audio_sclk_d"),
	CLK_MSR_ID(97, "audio_sclk_c"),
	CLK_MSR_ID(98, "audio_sclk_b"),
	CLK_MSR_ID(99, "audio_sclk_a"),
	CLK_MSR_ID(100, "audio_mclk_f"),
	CLK_MSR_ID(101, "audio_mclk_e"),
	CLK_MSR_ID(102, "audio_mclk_d"),
	CLK_MSR_ID(103, "audio_mclk_c"),
	CLK_MSR_ID(104, "audio_mclk_b"),
	CLK_MSR_ID(105, "audio_mclk_a"),
	CLK_MSR_ID(106, "pcie_refclk_n"),
	CLK_MSR_ID(107, "pcie_refclk_p"),
	CLK_MSR_ID(108, "audio_locker_out"),
	CLK_MSR_ID(109, "audio_locker_in"),
};

static const struct meson_msr_id clk_msr_g12a[] = {
	CLK_MSR_ID(0, "ring_osc_out_ee_0"),
	CLK_MSR_ID(1, "ring_osc_out_ee_1"),
	CLK_MSR_ID(2, "ring_osc_out_ee_2"),
	CLK_MSR_ID(3, "sys_cpu_ring_osc"),
	CLK_MSR_ID(4, "gp0_pll"),
	CLK_MSR_ID(6, "enci"),
	CLK_MSR_ID(7, "clk81"),
	CLK_MSR_ID(8, "encp"),
	CLK_MSR_ID(9, "encl"),
	CLK_MSR_ID(10, "vdac"),
	CLK_MSR_ID(11, "eth_tx"),
	CLK_MSR_ID(12, "hifi_pll"),
	CLK_MSR_ID(13, "mod_tcon"),
	CLK_MSR_ID(14, "fec_0"),
	CLK_MSR_ID(15, "fec_1"),
	CLK_MSR_ID(16, "fec_2"),
	CLK_MSR_ID(17, "sys_pll_div16"),
	CLK_MSR_ID(18, "sys_cpu_div16"),
	CLK_MSR_ID(19, "lcd_an_ph2"),
	CLK_MSR_ID(20, "rtc_osc_out"),
	CLK_MSR_ID(21, "lcd_an_ph3"),
	CLK_MSR_ID(22, "eth_phy_ref"),
	CLK_MSR_ID(23, "mpll_50m"),
	CLK_MSR_ID(24, "eth_125m"),
	CLK_MSR_ID(25, "eth_rmii"),
	CLK_MSR_ID(26, "sc_int"),
	CLK_MSR_ID(27, "in_mac"),
	CLK_MSR_ID(28, "sar_adc"),
	CLK_MSR_ID(29, "pcie_inp"),
	CLK_MSR_ID(30, "pcie_inn"),
	CLK_MSR_ID(31, "mpll_test_out"),
	CLK_MSR_ID(32, "vdec"),
	CLK_MSR_ID(33, "sys_cpu_ring_osc_1"),
	CLK_MSR_ID(34, "eth_mpll_50m"),
	CLK_MSR_ID(35, "mali"),
	CLK_MSR_ID(36, "hdmi_tx_pixel"),
	CLK_MSR_ID(37, "cdac"),
	CLK_MSR_ID(38, "vdin_meas"),
	CLK_MSR_ID(39, "bt656"),
	CLK_MSR_ID(41, "eth_rx_or_rmii"),
	CLK_MSR_ID(42, "mp0_out"),
	CLK_MSR_ID(43, "fclk_div5"),
	CLK_MSR_ID(44, "pwm_b"),
	CLK_MSR_ID(45, "pwm_a"),
	CLK_MSR_ID(46, "vpu"),
	CLK_MSR_ID(47, "ddr_dpll_pt"),
	CLK_MSR_ID(48, "mp1_out"),
	CLK_MSR_ID(49, "mp2_out"),
	CLK_MSR_ID(50, "mp3_out"),
	CLK_MSR_ID(51, "sd_emmc_c"),
	CLK_MSR_ID(52, "sd_emmc_b"),
	CLK_MSR_ID(53, "sd_emmc_a"),
	CLK_MSR_ID(54, "vpu_clkc"),
	CLK_MSR_ID(55, "vid_pll_div_out"),
	CLK_MSR_ID(56, "wave420l_a"),
	CLK_MSR_ID(57, "wave420l_c"),
	CLK_MSR_ID(58, "wave420l_b"),
	CLK_MSR_ID(59, "hcodec"),
	CLK_MSR_ID(61, "gpio_msr"),
	CLK_MSR_ID(62, "hevcb"),
	CLK_MSR_ID(63, "dsi_meas"),
	CLK_MSR_ID(64, "spicc_1"),
	CLK_MSR_ID(65, "spicc_0"),
	CLK_MSR_ID(66, "vid_lock"),
	CLK_MSR_ID(67, "dsi_phy"),
	CLK_MSR_ID(68, "hdcp22_esm"),
	CLK_MSR_ID(69, "hdcp22_skp"),
	CLK_MSR_ID(70, "pwm_f"),
	CLK_MSR_ID(71, "pwm_e"),
	CLK_MSR_ID(72, "pwm_d"),
	CLK_MSR_ID(73, "pwm_c"),
	CLK_MSR_ID(75, "hevcf"),
	CLK_MSR_ID(77, "rng_ring_osc_0"),
	CLK_MSR_ID(78, "rng_ring_osc_1"),
	CLK_MSR_ID(79, "rng_ring_osc_2"),
	CLK_MSR_ID(80, "rng_ring_osc_3"),
	CLK_MSR_ID(81, "vapb"),
	CLK_MSR_ID(82, "ge2d"),
	CLK_MSR_ID(83, "co_rx"),
	CLK_MSR_ID(84, "co_tx"),
	CLK_MSR_ID(89, "hdmi_todig"),
	CLK_MSR_ID(90, "hdmitx_sys"),
	CLK_MSR_ID(91, "sys_cpub_div16"),
	CLK_MSR_ID(92, "sys_pll_cpub_div16"),
	CLK_MSR_ID(94, "eth_phy_rx"),
	CLK_MSR_ID(95, "eth_phy_pll"),
	CLK_MSR_ID(96, "vpu_b"),
	CLK_MSR_ID(97, "cpu_b_tmp"),
	CLK_MSR_ID(98, "ts"),
	CLK_MSR_ID(99, "ring_osc_out_ee_3"),
	CLK_MSR_ID(100, "ring_osc_out_ee_4"),
	CLK_MSR_ID(101, "ring_osc_out_ee_5"),
	CLK_MSR_ID(102, "ring_osc_out_ee_6"),
	CLK_MSR_ID(103, "ring_osc_out_ee_7"),
	CLK_MSR_ID(104, "ring_osc_out_ee_8"),
	CLK_MSR_ID(105, "ring_osc_out_ee_9"),
	CLK_MSR_ID(106, "ephy_test"),
	CLK_MSR_ID(107, "au_dac_g128x"),
	CLK_MSR_ID(108, "audio_locker_out"),
	CLK_MSR_ID(109, "audio_locker_in"),
	CLK_MSR_ID(110, "audio_tdmout_c_sclk"),
	CLK_MSR_ID(111, "audio_tdmout_b_sclk"),
	CLK_MSR_ID(112, "audio_tdmout_a_sclk"),
	CLK_MSR_ID(113, "audio_tdmin_lb_sclk"),
	CLK_MSR_ID(114, "audio_tdmin_c_sclk"),
	CLK_MSR_ID(115, "audio_tdmin_b_sclk"),
	CLK_MSR_ID(116, "audio_tdmin_a_sclk"),
	CLK_MSR_ID(117, "audio_resample"),
	CLK_MSR_ID(118, "audio_pdm_sys"),
	CLK_MSR_ID(119, "audio_spdifout_b"),
	CLK_MSR_ID(120, "audio_spdifout"),
	CLK_MSR_ID(121, "audio_spdifin"),
	CLK_MSR_ID(122, "audio_pdm_dclk"),
};

static const struct meson_msr_id clk_msr_sm1[] = {
	CLK_MSR_ID(0, "ring_osc_out_ee_0"),
	CLK_MSR_ID(1, "ring_osc_out_ee_1"),
	CLK_MSR_ID(2, "ring_osc_out_ee_2"),
	CLK_MSR_ID(3, "ring_osc_out_ee_3"),
	CLK_MSR_ID(4, "gp0_pll"),
	CLK_MSR_ID(5, "gp1_pll"),
	CLK_MSR_ID(6, "enci"),
	CLK_MSR_ID(7, "clk81"),
	CLK_MSR_ID(8, "encp"),
	CLK_MSR_ID(9, "encl"),
	CLK_MSR_ID(10, "vdac"),
	CLK_MSR_ID(11, "eth_tx"),
	CLK_MSR_ID(12, "hifi_pll"),
	CLK_MSR_ID(13, "mod_tcon"),
	CLK_MSR_ID(14, "fec_0"),
	CLK_MSR_ID(15, "fec_1"),
	CLK_MSR_ID(16, "fec_2"),
	CLK_MSR_ID(17, "sys_pll_div16"),
	CLK_MSR_ID(18, "sys_cpu_div16"),
	CLK_MSR_ID(19, "lcd_an_ph2"),
	CLK_MSR_ID(20, "rtc_osc_out"),
	CLK_MSR_ID(21, "lcd_an_ph3"),
	CLK_MSR_ID(22, "eth_phy_ref"),
	CLK_MSR_ID(23, "mpll_50m"),
	CLK_MSR_ID(24, "eth_125m"),
	CLK_MSR_ID(25, "eth_rmii"),
	CLK_MSR_ID(26, "sc_int"),
	CLK_MSR_ID(27, "in_mac"),
	CLK_MSR_ID(28, "sar_adc"),
	CLK_MSR_ID(29, "pcie_inp"),
	CLK_MSR_ID(30, "pcie_inn"),
	CLK_MSR_ID(31, "mpll_test_out"),
	CLK_MSR_ID(32, "vdec"),
	CLK_MSR_ID(34, "eth_mpll_50m"),
	CLK_MSR_ID(35, "mali"),
	CLK_MSR_ID(36, "hdmi_tx_pixel"),
	CLK_MSR_ID(37, "cdac"),
	CLK_MSR_ID(38, "vdin_meas"),
	CLK_MSR_ID(39, "bt656"),
	CLK_MSR_ID(40, "arm_ring_osc_out_4"),
	CLK_MSR_ID(41, "eth_rx_or_rmii"),
	CLK_MSR_ID(42, "mp0_out"),
	CLK_MSR_ID(43, "fclk_div5"),
	CLK_MSR_ID(44, "pwm_b"),
	CLK_MSR_ID(45, "pwm_a"),
	CLK_MSR_ID(46, "vpu"),
	CLK_MSR_ID(47, "ddr_dpll_pt"),
	CLK_MSR_ID(48, "mp1_out"),
	CLK_MSR_ID(49, "mp2_out"),
	CLK_MSR_ID(50, "mp3_out"),
	CLK_MSR_ID(51, "sd_emmc_c"),
	CLK_MSR_ID(52, "sd_emmc_b"),
	CLK_MSR_ID(53, "sd_emmc_a"),
	CLK_MSR_ID(54, "vpu_clkc"),
	CLK_MSR_ID(55, "vid_pll_div_out"),
	CLK_MSR_ID(56, "wave420l_a"),
	CLK_MSR_ID(57, "wave420l_c"),
	CLK_MSR_ID(58, "wave420l_b"),
	CLK_MSR_ID(59, "hcodec"),
	CLK_MSR_ID(60, "arm_ring_osc_out_5"),
	CLK_MSR_ID(61, "gpio_msr"),
	CLK_MSR_ID(62, "hevcb"),
	CLK_MSR_ID(63, "dsi_meas"),
	CLK_MSR_ID(64, "spicc_1"),
	CLK_MSR_ID(65, "spicc_0"),
	CLK_MSR_ID(66, "vid_lock"),
	CLK_MSR_ID(67, "dsi_phy"),
	CLK_MSR_ID(68, "hdcp22_esm"),
	CLK_MSR_ID(69, "hdcp22_skp"),
	CLK_MSR_ID(70, "pwm_f"),
	CLK_MSR_ID(71, "pwm_e"),
	CLK_MSR_ID(72, "pwm_d"),
	CLK_MSR_ID(73, "pwm_c"),
	CLK_MSR_ID(74, "arm_ring_osc_out_6"),
	CLK_MSR_ID(75, "hevcf"),
	CLK_MSR_ID(76, "arm_ring_osc_out_7"),
	CLK_MSR_ID(77, "rng_ring_osc_0"),
	CLK_MSR_ID(78, "rng_ring_osc_1"),
	CLK_MSR_ID(79, "rng_ring_osc_2"),
	CLK_MSR_ID(80, "rng_ring_osc_3"),
	CLK_MSR_ID(81, "vapb"),
	CLK_MSR_ID(82, "ge2d"),
	CLK_MSR_ID(83, "co_rx"),
	CLK_MSR_ID(84, "co_tx"),
	CLK_MSR_ID(85, "arm_ring_osc_out_8"),
	CLK_MSR_ID(86, "arm_ring_osc_out_9"),
	CLK_MSR_ID(87, "mipi_dsi_phy"),
	CLK_MSR_ID(88, "cis2_adapt"),
	CLK_MSR_ID(89, "hdmi_todig"),
	CLK_MSR_ID(90, "hdmitx_sys"),
	CLK_MSR_ID(91, "nna_core"),
	CLK_MSR_ID(92, "nna_axi"),
	CLK_MSR_ID(93, "vad"),
	CLK_MSR_ID(94, "eth_phy_rx"),
	CLK_MSR_ID(95, "eth_phy_pll"),
	CLK_MSR_ID(96, "vpu_b"),
	CLK_MSR_ID(97, "cpu_b_tmp"),
	CLK_MSR_ID(98, "ts"),
	CLK_MSR_ID(99, "arm_ring_osc_out_10"),
	CLK_MSR_ID(100, "arm_ring_osc_out_11"),
	CLK_MSR_ID(101, "arm_ring_osc_out_12"),
	CLK_MSR_ID(102, "arm_ring_osc_out_13"),
	CLK_MSR_ID(103, "arm_ring_osc_out_14"),
	CLK_MSR_ID(104, "arm_ring_osc_out_15"),
	CLK_MSR_ID(105, "arm_ring_osc_out_16"),
	CLK_MSR_ID(106, "ephy_test"),
	CLK_MSR_ID(107, "au_dac_g128x"),
	CLK_MSR_ID(108, "audio_locker_out"),
	CLK_MSR_ID(109, "audio_locker_in"),
	CLK_MSR_ID(110, "audio_tdmout_c_sclk"),
	CLK_MSR_ID(111, "audio_tdmout_b_sclk"),
	CLK_MSR_ID(112, "audio_tdmout_a_sclk"),
	CLK_MSR_ID(113, "audio_tdmin_lb_sclk"),
	CLK_MSR_ID(114, "audio_tdmin_c_sclk"),
	CLK_MSR_ID(115, "audio_tdmin_b_sclk"),
	CLK_MSR_ID(116, "audio_tdmin_a_sclk"),
	CLK_MSR_ID(117, "audio_resample"),
	CLK_MSR_ID(118, "audio_pdm_sys"),
	CLK_MSR_ID(119, "audio_spdifout_b"),
	CLK_MSR_ID(120, "audio_spdifout"),
	CLK_MSR_ID(121, "audio_spdifin"),
	CLK_MSR_ID(122, "audio_pdm_dclk"),
	CLK_MSR_ID(123, "audio_resampled"),
	CLK_MSR_ID(124, "earcrx_pll"),
	CLK_MSR_ID(125, "earcrx_pll_test"),
	CLK_MSR_ID(126, "csi_phy0"),
	CLK_MSR_ID(127, "csi2_data"),
};

static const struct meson_msr_id clk_msr_c3[] = {
	CLK_MSR_ID(0,	"sys_clk"),
	CLK_MSR_ID(1,	"axi_clk"),
	CLK_MSR_ID(2,	"rtc_clk"),
	CLK_MSR_ID(3,	"p20_usb2_ckout"),
	CLK_MSR_ID(4,	"eth_mpll_test"),
	CLK_MSR_ID(5,	"sys_pll"),
	CLK_MSR_ID(6,	"cpu_clk_div16"),
	CLK_MSR_ID(7,	"ts_pll"),
	CLK_MSR_ID(8,	"fclk_div2"),
	CLK_MSR_ID(9,	"fclk_div2p5"),
	CLK_MSR_ID(10,	"fclk_div3"),
	CLK_MSR_ID(11,	"fclk_div4"),
	CLK_MSR_ID(12,	"fclk_div5"),
	CLK_MSR_ID(13,	"fclk_div7"),
	CLK_MSR_ID(15,	"fclk_50m"),
	CLK_MSR_ID(16,	"sys_oscin32k_i"),
	CLK_MSR_ID(17,	"mclk_pll"),
	CLK_MSR_ID(19,	"hifi_pll"),
	CLK_MSR_ID(20,	"gp0_pll"),
	CLK_MSR_ID(21,	"gp1_pll"),
	CLK_MSR_ID(22,	"eth_mppll_50m_ckout"),
	CLK_MSR_ID(23,	"sys_pll_div16"),
	CLK_MSR_ID(24,	"ddr_dpll_pt_clk"),
	CLK_MSR_ID(26,	"nna_core"),
	CLK_MSR_ID(27,	"rtc_sec_pulse_out"),
	CLK_MSR_ID(28,	"rtc_osc_clk_out"),
	CLK_MSR_ID(29,	"debug_in_clk"),
	CLK_MSR_ID(30,	"mod_eth_phy_ref_clk"),
	CLK_MSR_ID(31,	"mod_eth_tx_clk"),
	CLK_MSR_ID(32,	"eth_125m"),
	CLK_MSR_ID(33,	"eth_rmii"),
	CLK_MSR_ID(34,	"co_clkin_to_mac"),
	CLK_MSR_ID(36,	"co_rx_clk"),
	CLK_MSR_ID(37,	"co_tx_clk"),
	CLK_MSR_ID(38,	"eth_phy_rxclk"),
	CLK_MSR_ID(39,	"eth_phy_plltxclk"),
	CLK_MSR_ID(40,	"ephy_test_clk"),
	CLK_MSR_ID(66,	"vapb"),
	CLK_MSR_ID(67,	"ge2d"),
	CLK_MSR_ID(68,	"dewarpa"),
	CLK_MSR_ID(70,	"mipi_dsi_meas"),
	CLK_MSR_ID(71,	"dsi_phy"),
	CLK_MSR_ID(79,	"rama"),
	CLK_MSR_ID(94,	"vc9000e_core"),
	CLK_MSR_ID(95,	"vc9000e_sys"),
	CLK_MSR_ID(96,	"vc9000e_aclk"),
	CLK_MSR_ID(97,	"hcodec"),
	CLK_MSR_ID(106,	"deskew_pll_clk_div32_out"),
	CLK_MSR_ID(107,	"mipi_csi_phy_clk_out[0]"),
	CLK_MSR_ID(108,	"mipi_csi_phy_clk_out[1]"),
	CLK_MSR_ID(110,	"spifc"),
	CLK_MSR_ID(111,	"saradc"),
	CLK_MSR_ID(112,	"ts"),
	CLK_MSR_ID(113,	"sd_emmc_c"),
	CLK_MSR_ID(114,	"sd_emmc_b"),
	CLK_MSR_ID(115,	"sd_emmc_a"),
	CLK_MSR_ID(116,	"gpio_msr_clk"),
	CLK_MSR_ID(117,	"spicc_b"),
	CLK_MSR_ID(118,	"spicc_a"),
	CLK_MSR_ID(122,	"mod_audio_pdm_dclk_o"),
	CLK_MSR_ID(124,	"o_earcrx_dmac_clk"),
	CLK_MSR_ID(125,	"o_earcrx_cmdc_clk"),
	CLK_MSR_ID(126,	"o_earctx_dmac_clk"),
	CLK_MSR_ID(127,	"o_earctx_cmdc_clk"),
	CLK_MSR_ID(128,	"o_tohdmitx_bclk"),
	CLK_MSR_ID(129,	"o_tohdmitx_mclk"),
	CLK_MSR_ID(130,	"o_tohdmitx_spdif_clk"),
	CLK_MSR_ID(131,	"o_toacodec_bclk"),
	CLK_MSR_ID(132,	"o_toacodec_mclk"),
	CLK_MSR_ID(133,	"o_spdifout_b_mst_clk"),
	CLK_MSR_ID(134,	"o_spdifout_mst_clk"),
	CLK_MSR_ID(135,	"o_spdifin_mst_clk"),
	CLK_MSR_ID(136,	"o_audio_mclk"),
	CLK_MSR_ID(137,	"o_vad_clk"),
	CLK_MSR_ID(138,	"o_tdmout_d_sclk"),
	CLK_MSR_ID(139,	"o_tdmout_c_sclk"),
	CLK_MSR_ID(140,	"o_tdmout_b_sclk"),
	CLK_MSR_ID(141,	"o_tdmout_a_sclk"),
	CLK_MSR_ID(142,	"o_tdminb_1b_sclk"),
	CLK_MSR_ID(143,	"o_tdmin_1b_sclk"),
	CLK_MSR_ID(144,	"o_tdmin_d_sclk"),
	CLK_MSR_ID(145,	"o_tdmin_c_sclk"),
	CLK_MSR_ID(146,	"o_tdmin_b_sclk"),
	CLK_MSR_ID(147,	"o_tdmin_a_sclk"),
	CLK_MSR_ID(148,	"o_resampleb_clk"),
	CLK_MSR_ID(149,	"o_resamplea_clk"),
	CLK_MSR_ID(150,	"o_pdmb_sysclk"),
	CLK_MSR_ID(151,	"o_pdmb_dclk"),
	CLK_MSR_ID(152,	"o_pdm_sysclk"),
	CLK_MSR_ID(153,	"o_pdm_dclk"),
	CLK_MSR_ID(154,	"c_alockerb_out_clk"),
	CLK_MSR_ID(155,	"c_alockerb_in_clk"),
	CLK_MSR_ID(156,	"c_alocker_out_clk"),
	CLK_MSR_ID(157,	"c_alocker_in_clk"),
	CLK_MSR_ID(158,	"audio_mst_clk[34]"),
	CLK_MSR_ID(159,	"audio_mst_clk[35]"),
	CLK_MSR_ID(160,	"pwm_n"),
	CLK_MSR_ID(161,	"pwm_m"),
	CLK_MSR_ID(162,	"pwm_l"),
	CLK_MSR_ID(163,	"pwm_k"),
	CLK_MSR_ID(164,	"pwm_j"),
	CLK_MSR_ID(165,	"pwm_i"),
	CLK_MSR_ID(166,	"pwm_h"),
	CLK_MSR_ID(167,	"pwm_g"),
	CLK_MSR_ID(168,	"pwm_f"),
	CLK_MSR_ID(169,	"pwm_e"),
	CLK_MSR_ID(170,	"pwm_d"),
	CLK_MSR_ID(171,	"pwm_c"),
	CLK_MSR_ID(172,	"pwm_b"),
	CLK_MSR_ID(173,	"pwm_a"),
	CLK_MSR_ID(174,	"AU_DAC1_CLK_TO_GPIO"),
	CLK_MSR_ID(175,	"AU_ADC_CLK_TO_GPIO"),
	CLK_MSR_ID(176,	"rng_ring_osc_clk[0]"),
	CLK_MSR_ID(177,	"rng_ring_osc_clk[1]"),
	CLK_MSR_ID(178,	"rng_ring_osc_clk[2]"),
	CLK_MSR_ID(179,	"rng_ring_osc_clk[3]"),
	CLK_MSR_ID(180,	"sys_cpu_ring_osc_clk[0]"),
	CLK_MSR_ID(181,	"sys_cpu_ring_osc_clk[1]"),
	CLK_MSR_ID(182,	"sys_cpu_ring_osc_clk[2]"),
	CLK_MSR_ID(183,	"sys_cpu_ring_osc_clk[3]"),
	CLK_MSR_ID(184,	"sys_cpu_ring_osc_clk[4]"),
	CLK_MSR_ID(185,	"sys_cpu_ring_osc_clk[5]"),
	CLK_MSR_ID(186,	"sys_cpu_ring_osc_clk[6]"),
	CLK_MSR_ID(187,	"sys_cpu_ring_osc_clk[7]"),
	CLK_MSR_ID(188,	"sys_cpu_ring_osc_clk[8]"),
	CLK_MSR_ID(189,	"sys_cpu_ring_osc_clk[9]"),
	CLK_MSR_ID(190,	"sys_cpu_ring_osc_clk[10]"),
	CLK_MSR_ID(191,	"sys_cpu_ring_osc_clk[11]"),
	CLK_MSR_ID(192,	"am_ring_osc_clk_out[12](dmc)"),
	CLK_MSR_ID(193,	"am_ring_osc_clk_out[13](rama)"),
	CLK_MSR_ID(194,	"am_ring_osc_clk_out[14](nna)"),
	CLK_MSR_ID(195,	"am_ring_osc_clk_out[15](nna)"),
	CLK_MSR_ID(200,	"rng_ring_osc_clk_1[0]"),
	CLK_MSR_ID(201,	"rng_ring_osc_clk_1[1]"),
	CLK_MSR_ID(202,	"rng_ring_osc_clk_1[2]"),
	CLK_MSR_ID(203,	"rng_ring_osc_clk_1[3]"),

};

static const struct meson_msr_id clk_msr_s4[] = {
	CLK_MSR_ID(0, "sys_clk"),
	CLK_MSR_ID(1, "axi_clk"),
	CLK_MSR_ID(2, "rtc_clk"),
	CLK_MSR_ID(5, "mali"),
	CLK_MSR_ID(6, "cpu_clk_div16"),
	CLK_MSR_ID(7, "ceca_clk"),
	CLK_MSR_ID(8, "cecb_clk"),
	CLK_MSR_ID(10, "fclk_div5"),
	CLK_MSR_ID(11, "mpll0"),
	CLK_MSR_ID(12, "mpll1"),
	CLK_MSR_ID(13, "mpll2"),
	CLK_MSR_ID(14, "mpll3"),
	CLK_MSR_ID(15, "fclk_50m"),
	CLK_MSR_ID(16, "pcie_clk_inp"),
	CLK_MSR_ID(17, "pcie_clk_inn"),
	CLK_MSR_ID(18, "mpll_clk_test_out"),
	CLK_MSR_ID(19, "hifi_pll"),
	CLK_MSR_ID(20, "gp0_pll"),
	CLK_MSR_ID(21, "gp1_pll"),
	CLK_MSR_ID(22, "eth_mppll_50m_ckout"),
	CLK_MSR_ID(23, "sys_pll_div16"),
	CLK_MSR_ID(24, "ddr_dpll_pt_clk"),
	CLK_MSR_ID(30, "mod_eth_phy_ref_clk"),
	CLK_MSR_ID(31, "mod_eth_tx_clk"),
	CLK_MSR_ID(32, "eth_125m"),
	CLK_MSR_ID(33, "eth_rmii"),
	CLK_MSR_ID(34, "co_clkin_to_mac"),
	CLK_MSR_ID(35, "mod_eth_rx_clk_rmii"),
	CLK_MSR_ID(36, "co_rx_clk"),
	CLK_MSR_ID(37, "co_tx_clk"),
	CLK_MSR_ID(38, "eth_phy_rxclk"),
	CLK_MSR_ID(39, "eth_phy_plltxclk"),
	CLK_MSR_ID(40, "ephy_test_clk"),
	CLK_MSR_ID(50, "vid_pll_div_clk_out"),
	CLK_MSR_ID(51, "enci"),
	CLK_MSR_ID(52, "encp"),
	CLK_MSR_ID(53, "encl"),
	CLK_MSR_ID(54, "vdac"),
	CLK_MSR_ID(55, "cdac_clk_c"),
	CLK_MSR_ID(56, "mod_tcon_clko"),
	CLK_MSR_ID(57, "lcd_an_clk_ph2"),
	CLK_MSR_ID(58, "lcd_an_clk_ph3"),
	CLK_MSR_ID(59, "hdmitx_pixel"),
	CLK_MSR_ID(60, "vdin_meas"),
	CLK_MSR_ID(61, "vpu"),
	CLK_MSR_ID(62, "vpu_clkb"),
	CLK_MSR_ID(63, "vpu_clkb_tmp"),
	CLK_MSR_ID(64, "vpu_clkc"),
	CLK_MSR_ID(65, "vid_lock"),
	CLK_MSR_ID(66, "vapb"),
	CLK_MSR_ID(67, "ge2d"),
	CLK_MSR_ID(68, "cts_hdcp22_esmclk"),
	CLK_MSR_ID(69, "cts_hdcp22_skpclk"),
	CLK_MSR_ID(76, "hdmitx_tmds"),
	CLK_MSR_ID(77, "hdmitx_sys_clk"),
	CLK_MSR_ID(78, "hdmitx_fe_clk"),
	CLK_MSR_ID(79, "rama"),
	CLK_MSR_ID(93, "vdec"),
	CLK_MSR_ID(99, "hevcf"),
	CLK_MSR_ID(100, "demod_core"),
	CLK_MSR_ID(101, "adc_extclk_in"),
	CLK_MSR_ID(102, "cts_demod_core_t2_clk"),
	CLK_MSR_ID(103, "adc_dpll_intclk"),
	CLK_MSR_ID(104, "adc_dpll_clk_b3"),
	CLK_MSR_ID(105, "s2_adc_clk"),
	CLK_MSR_ID(106, "deskew_pll_clk_div32_out"),
	CLK_MSR_ID(110, "sc"),
	CLK_MSR_ID(111, "sar_adc"),
	CLK_MSR_ID(113, "sd_emmc_c"),
	CLK_MSR_ID(114, "sd_emmc_b"),
	CLK_MSR_ID(115, "sd_emmc_a"),
	CLK_MSR_ID(116, "gpio_msr_clk"),
	CLK_MSR_ID(118, "spicc0"),
	CLK_MSR_ID(121, "ts"),
	CLK_MSR_ID(130, "audio_vad_clk"),
	CLK_MSR_ID(131, "acodec_dac_clk_x128"),
	CLK_MSR_ID(132, "audio_locker_in_clk"),
	CLK_MSR_ID(133, "audio_locker_out_clk"),
	CLK_MSR_ID(134, "audio_tdmout_c_sclk"),
	CLK_MSR_ID(135, "audio_tdmout_b_sclk"),
	CLK_MSR_ID(136, "audio_tdmout_a_sclk"),
	CLK_MSR_ID(137, "audio_tdmin_lb_sclk"),
	CLK_MSR_ID(138, "audio_tdmin_c_sclk"),
	CLK_MSR_ID(139, "audio_tdmin_b_sclk"),
	CLK_MSR_ID(140, "audio_tdmin_a_sclk"),
	CLK_MSR_ID(141, "audio_resamplea_clk"),
	CLK_MSR_ID(142, "audio_pdm_sysclk"),
	CLK_MSR_ID(143, "audio_spdifout_b_mst_clk"),
	CLK_MSR_ID(144, "audio_spdifout_mst_clk"),
	CLK_MSR_ID(145, "audio_spdifin_mst_clk"),
	CLK_MSR_ID(146, "audio_pdm_dclk"),
	CLK_MSR_ID(147, "audio_resampleb_clk"),
	CLK_MSR_ID(160, "pwm_j"),
	CLK_MSR_ID(161, "pwm_i"),
	CLK_MSR_ID(162, "pwm_h"),
	CLK_MSR_ID(163, "pwm_g"),
	CLK_MSR_ID(164, "pwm_f"),
	CLK_MSR_ID(165, "pwm_e"),
	CLK_MSR_ID(166, "pwm_d"),
	CLK_MSR_ID(167, "pwm_c"),
	CLK_MSR_ID(168, "pwm_b"),
	CLK_MSR_ID(169, "pwm_a"),
	CLK_MSR_ID(176, "rng_ring_0"),
	CLK_MSR_ID(177, "rng_ring_1"),
	CLK_MSR_ID(178, "rng_ring_2"),
	CLK_MSR_ID(179, "rng_ring_3"),
	CLK_MSR_ID(180, "dmc_osc_ring(LVT16)"),
	CLK_MSR_ID(181, "gpu_osc_ring0(LVT16)"),
	CLK_MSR_ID(182, "gpu_osc_ring1(ULVT16)"),
	CLK_MSR_ID(183, "gpu_osc_ring2(SLVT16)"),
	CLK_MSR_ID(184, "vpu_osc_ring0(SVT24)"),
	CLK_MSR_ID(185, "vpu_osc_ring1(LVT20)"),
	CLK_MSR_ID(186, "vpu_osc_ring2(LVT16)"),
	CLK_MSR_ID(187, "dos_osc_ring0(SVT24)"),
	CLK_MSR_ID(188, "dos_osc_ring1(SVT16)"),
	CLK_MSR_ID(189, "dos_osc_ring2(LVT16)"),
	CLK_MSR_ID(190, "dos_osc_ring3(ULVT20)"),
	CLK_MSR_ID(192, "axi_sram_osc_ring(SVT16)"),
	CLK_MSR_ID(193, "demod_osc_ring0"),
	CLK_MSR_ID(194, "demod_osc_ring1"),
	CLK_MSR_ID(195, "sar_osc_ring"),
	CLK_MSR_ID(196, "sys_cpu_osc_ring0"),
	CLK_MSR_ID(197, "sys_cpu_osc_ring1"),
	CLK_MSR_ID(198, "sys_cpu_osc_ring2"),
	CLK_MSR_ID(199, "sys_cpu_osc_ring3"),
	CLK_MSR_ID(200, "sys_cpu_osc_ring4"),
	CLK_MSR_ID(201, "sys_cpu_osc_ring5"),
	CLK_MSR_ID(202, "sys_cpu_osc_ring6"),
	CLK_MSR_ID(203, "sys_cpu_osc_ring7"),
	CLK_MSR_ID(204, "sys_cpu_osc_ring8"),
	CLK_MSR_ID(205, "sys_cpu_osc_ring9"),
	CLK_MSR_ID(206, "sys_cpu_osc_ring10"),
	CLK_MSR_ID(207, "sys_cpu_osc_ring11"),
	CLK_MSR_ID(208, "sys_cpu_osc_ring12"),
	CLK_MSR_ID(209, "sys_cpu_osc_ring13"),
	CLK_MSR_ID(210, "sys_cpu_osc_ring14"),
	CLK_MSR_ID(211, "sys_cpu_osc_ring15"),
	CLK_MSR_ID(212, "sys_cpu_osc_ring16"),
	CLK_MSR_ID(213, "sys_cpu_osc_ring17"),
	CLK_MSR_ID(214, "sys_cpu_osc_ring18"),
	CLK_MSR_ID(215, "sys_cpu_osc_ring19"),
	CLK_MSR_ID(216, "sys_cpu_osc_ring20"),
	CLK_MSR_ID(217, "sys_cpu_osc_ring21"),
	CLK_MSR_ID(218, "sys_cpu_osc_ring22"),
	CLK_MSR_ID(219, "sys_cpu_osc_ring23"),
	CLK_MSR_ID(220, "sys_cpu_osc_ring24"),
	CLK_MSR_ID(221, "sys_cpu_osc_ring25"),
	CLK_MSR_ID(222, "sys_cpu_osc_ring26"),
	CLK_MSR_ID(223, "sys_cpu_osc_ring27"),

};

static int meson_measure_id(struct meson_msr_id *clk_msr_id,
			    unsigned int duration)
{
	struct meson_msr *priv = clk_msr_id->priv;
	const struct msr_reg_offset *reg = priv->data.reg;
	unsigned int val;
	int ret;

	ret = mutex_lock_interruptible(&measure_lock);
	if (ret)
		return ret;

	regmap_write(priv->regmap, reg->freq_ctrl, 0);

	/* Set measurement duration */
	regmap_update_bits(priv->regmap, reg->freq_ctrl, MSR_DURATION,
			   FIELD_PREP(MSR_DURATION, duration - 1));

	/* Set ID */
	regmap_update_bits(priv->regmap, reg->freq_ctrl, MSR_CLK_SRC,
			   FIELD_PREP(MSR_CLK_SRC, clk_msr_id->id));

	/* Enable & Start */
	regmap_update_bits(priv->regmap, reg->freq_ctrl,
			   MSR_RUN | MSR_ENABLE,
			   MSR_RUN | MSR_ENABLE);

	ret = regmap_read_poll_timeout(priv->regmap, reg->freq_ctrl,
				       val, !(val & MSR_BUSY), 10, 10000);
	if (ret) {
		mutex_unlock(&measure_lock);
		return ret;
	}

	/* Disable */
	regmap_update_bits(priv->regmap, reg->freq_ctrl, MSR_ENABLE, 0);

	/* Get the value in multiple of gate time counts */
	regmap_read(priv->regmap, reg->freq_val, &val);

	mutex_unlock(&measure_lock);

	if (val >= MSR_VAL_MASK)
		return -EINVAL;

	return DIV_ROUND_CLOSEST_ULL((val & MSR_VAL_MASK) * 1000000ULL,
				     duration);
}

static int meson_measure_best_id(struct meson_msr_id *clk_msr_id,
				    unsigned int *precision)
{
	unsigned int duration = DIV_MAX;
	int ret;

	/* Start from max duration and down to min duration */
	do {
		ret = meson_measure_id(clk_msr_id, duration);
		if (ret >= 0)
			*precision = (2 * 1000000) / duration;
		else
			duration -= DIV_STEP;
	} while (duration >= DIV_MIN && ret == -EINVAL);

	return ret;
}

static int clk_msr_show(struct seq_file *s, void *data)
{
	struct meson_msr_id *clk_msr_id = s->private;
	unsigned int precision = 0;
	int val;

	val = meson_measure_best_id(clk_msr_id, &precision);
	if (val < 0)
		return val;

	seq_printf(s, "%d\t+/-%dHz\n", val, precision);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(clk_msr);

static int clk_msr_summary_show(struct seq_file *s, void *data)
{
	struct meson_msr_id *msr_table = s->private;
	unsigned int msr_count = msr_table->priv->data.msr_count;
	unsigned int precision = 0;
	int val, i;

	seq_puts(s, "  clock                     rate    precision\n");
	seq_puts(s, "---------------------------------------------\n");

	for (i = 0 ; i < msr_count ; ++i) {
		if (!msr_table[i].name)
			continue;

		val = meson_measure_best_id(&msr_table[i], &precision);
		if (val < 0)
			return val;

		seq_printf(s, " %-20s %10d    +/-%dHz\n",
			   msr_table[i].name, val, precision);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(clk_msr_summary);

static struct regmap_config meson_clk_msr_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static int meson_msr_probe(struct platform_device *pdev)
{
	const struct meson_msr_data *match_data;
	struct meson_msr *priv;
	struct dentry *root, *clks;
	struct resource *res;
	void __iomem *base;
	int i;

	priv = devm_kzalloc(&pdev->dev, sizeof(struct meson_msr),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	match_data = device_get_match_data(&pdev->dev);
	if (!match_data) {
		dev_err(&pdev->dev, "failed to get match data\n");
		return -ENODEV;
	}

	priv->data.msr_table = devm_kcalloc(&pdev->dev,
					    match_data->msr_count,
					    sizeof(struct meson_msr_id),
					    GFP_KERNEL);
	if (!priv->data.msr_table)
		return -ENOMEM;

	memcpy(priv->data.msr_table, match_data->msr_table,
	       match_data->msr_count * sizeof(struct meson_msr_id));
	priv->data.msr_count = match_data->msr_count;

	base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	meson_clk_msr_regmap_config.max_register = resource_size(res) - 4;
	priv->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					     &meson_clk_msr_regmap_config);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	priv->data.reg = devm_kzalloc(&pdev->dev, sizeof(struct msr_reg_offset),
				      GFP_KERNEL);
	if (!priv->data.reg)
		return -ENOMEM;

	memcpy((void *)priv->data.reg, match_data->reg,
	       sizeof(struct msr_reg_offset));

	root = debugfs_create_dir("meson-clk-msr", NULL);
	clks = debugfs_create_dir("clks", root);

	debugfs_create_file("measure_summary", 0444, root,
			    priv->data.msr_table, &clk_msr_summary_fops);

	for (i = 0 ; i < priv->data.msr_count ; ++i) {
		if (!priv->data.msr_table[i].name)
			continue;

		priv->data.msr_table[i].priv = priv;

		debugfs_create_file(priv->data.msr_table[i].name, 0444, clks,
				    &priv->data.msr_table[i], &clk_msr_fops);
	}

	return 0;
}

static const struct msr_reg_offset msr_reg_offset = {
	.duty_val = 0x0,
	.freq_ctrl = 0x4,
	.duty_ctrl = 0x8,
	.freq_val = 0xc,
};

static const struct meson_msr_data clk_msr_gx_data = {
	.msr_table = (void *)clk_msr_gx,
	.msr_count = ARRAY_SIZE(clk_msr_gx),
	.reg = &msr_reg_offset,
};

static const struct meson_msr_data clk_msr_m8_data = {
	.msr_table = (void *)clk_msr_m8,
	.msr_count = ARRAY_SIZE(clk_msr_m8),
	.reg = &msr_reg_offset,
};

static const struct meson_msr_data clk_msr_axg_data = {
	.msr_table = (void *)clk_msr_axg,
	.msr_count = ARRAY_SIZE(clk_msr_axg),
	.reg = &msr_reg_offset,
};

static const struct meson_msr_data clk_msr_g12a_data = {
	.msr_table = (void *)clk_msr_g12a,
	.msr_count = ARRAY_SIZE(clk_msr_g12a),
	.reg = &msr_reg_offset,
};

static const struct meson_msr_data clk_msr_sm1_data = {
	.msr_table = (void *)clk_msr_sm1,
	.msr_count = ARRAY_SIZE(clk_msr_sm1),
	.reg = &msr_reg_offset,
};

static const struct msr_reg_offset msr_reg_offset_v2 = {
	.freq_ctrl = 0x0,
	.duty_ctrl = 0x4,
	.freq_val = 0x8,
	.duty_val = 0x18,
};

static const struct meson_msr_data clk_msr_c3_data = {
	.msr_table = (void *)clk_msr_c3,
	.msr_count = ARRAY_SIZE(clk_msr_c3),
	.reg = &msr_reg_offset_v2,
};

static const struct meson_msr_data clk_msr_s4_data = {
	.msr_table = (void *)clk_msr_s4,
	.msr_count = ARRAY_SIZE(clk_msr_s4),
	.reg = &msr_reg_offset_v2,
};

static const struct of_device_id meson_msr_match_table[] = {
	{
		.compatible = "amlogic,meson-gx-clk-measure",
		.data = &clk_msr_gx_data,
	},
	{
		.compatible = "amlogic,meson8-clk-measure",
		.data = &clk_msr_m8_data,
	},
	{
		.compatible = "amlogic,meson8b-clk-measure",
		.data = &clk_msr_m8_data,
	},
	{
		.compatible = "amlogic,meson-axg-clk-measure",
		.data = &clk_msr_axg_data,
	},
	{
		.compatible = "amlogic,meson-g12a-clk-measure",
		.data = &clk_msr_g12a_data,
	},
	{
		.compatible = "amlogic,meson-sm1-clk-measure",
		.data = &clk_msr_sm1_data,
	},
	{
		.compatible = "amlogic,c3-clk-measure",
		.data = &clk_msr_c3_data,
	},
	{
		.compatible = "amlogic,s4-clk-measure",
		.data = &clk_msr_s4_data,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, meson_msr_match_table);

static struct platform_driver meson_msr_driver = {
	.probe	= meson_msr_probe,
	.driver = {
		.name		= "meson_msr",
		.of_match_table	= meson_msr_match_table,
	},
};
module_platform_driver(meson_msr_driver);
MODULE_DESCRIPTION("Amlogic Meson SoC Clock Measure driver");
MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Broadcom brcmstb GPIO units (pinctrl only)
 *
 * Copyright (C) 2024-2025 Ivan T. Ivanov, Andrea della Porta
 * Copyright (C) 2021-3 Raspberry Pi Ltd.
 * Copyright (C) 2012 Chris Boot, Simon Arlott, Stephen Warren
 *
 * Based heavily on the BCM2835 GPIO & pinctrl driver, which was inspired by:
 * pinctrl-nomadik.c, please see original file for copyright information
 * pinctrl-tegra.c, please see original file for copyright information
 */

#include <linux/pinctrl/pinctrl.h>
#include <linux/of.h>
#include "pinctrl-brcmstb.h"

#define BRCMSTB_FSEL_COUNT	8
#define BRCMSTB_FSEL_MASK	0xf

#define BRCMSTB_PIN(i, f1, f2, f3, f4, f5, f6, f7, f8) \
	[i] = { \
		.funcs = (u8[]) { \
			func_##f1, \
			func_##f2, \
			func_##f3, \
			func_##f4, \
			func_##f5, \
			func_##f6, \
			func_##f7, \
			func_##f8, \
		}, \
		.n_funcs = BRCMSTB_FSEL_COUNT, \
		.func_mask = BRCMSTB_FSEL_MASK, \
	}

enum bcm2712_funcs {
	func_gpio,
	func_alt1,
	func_alt2,
	func_alt3,
	func_alt4,
	func_alt5,
	func_alt6,
	func_alt7,
	func_alt8,
	func_aon_cpu_standbyb,
	func_aon_fp_4sec_resetb,
	func_aon_gpclk,
	func_aon_pwm,
	func_arm_jtag,
	func_aud_fs_clk0,
	func_avs_pmu_bsc,
	func_bsc_m0,
	func_bsc_m1,
	func_bsc_m2,
	func_bsc_m3,
	func_clk_observe,
	func_ctl_hdmi_5v,
	func_enet0,
	func_enet0_mii,
	func_enet0_rgmii,
	func_ext_sc_clk,
	func_fl0,
	func_fl1,
	func_gpclk0,
	func_gpclk1,
	func_gpclk2,
	func_hdmi_tx0_auto_i2c,
	func_hdmi_tx0_bsc,
	func_hdmi_tx1_auto_i2c,
	func_hdmi_tx1_bsc,
	func_i2s_in,
	func_i2s_out,
	func_ir_in,
	func_mtsif,
	func_mtsif_alt,
	func_mtsif_alt1,
	func_pdm,
	func_pkt,
	func_pm_led_out,
	func_sc0,
	func_sd0,
	func_sd2,
	func_sd_card_a,
	func_sd_card_b,
	func_sd_card_c,
	func_sd_card_d,
	func_sd_card_e,
	func_sd_card_f,
	func_sd_card_g,
	func_spdif_out,
	func_spi_m,
	func_spi_s,
	func_sr_edm_sense,
	func_te0,
	func_te1,
	func_tsio,
	func_uart0,
	func_uart1,
	func_uart2,
	func_usb_pwr,
	func_usb_vbus,
	func_uui,
	func_vc_i2c0,
	func_vc_i2c3,
	func_vc_i2c4,
	func_vc_i2c5,
	func_vc_i2csl,
	func_vc_pcm,
	func_vc_pwm0,
	func_vc_pwm1,
	func_vc_spi0,
	func_vc_spi3,
	func_vc_spi4,
	func_vc_spi5,
	func_vc_uart0,
	func_vc_uart2,
	func_vc_uart3,
	func_vc_uart4,
	func__,
	func_count = func__
};

static const struct pin_regs bcm2712_c0_gpio_pin_regs[] = {
	GPIO_REGS(0, 0, 0, 7, 7),
	GPIO_REGS(1, 0, 1, 7, 8),
	GPIO_REGS(2, 0, 2, 7, 9),
	GPIO_REGS(3, 0, 3, 7, 10),
	GPIO_REGS(4, 0, 4, 7, 11),
	GPIO_REGS(5, 0, 5, 7, 12),
	GPIO_REGS(6, 0, 6, 7, 13),
	GPIO_REGS(7, 0, 7, 7, 14),
	GPIO_REGS(8, 1, 0, 8, 0),
	GPIO_REGS(9, 1, 1, 8, 1),
	GPIO_REGS(10, 1, 2, 8, 2),
	GPIO_REGS(11, 1, 3, 8, 3),
	GPIO_REGS(12, 1, 4, 8, 4),
	GPIO_REGS(13, 1, 5, 8, 5),
	GPIO_REGS(14, 1, 6, 8, 6),
	GPIO_REGS(15, 1, 7, 8, 7),
	GPIO_REGS(16, 2, 0, 8, 8),
	GPIO_REGS(17, 2, 1, 8, 9),
	GPIO_REGS(18, 2, 2, 8, 10),
	GPIO_REGS(19, 2, 3, 8, 11),
	GPIO_REGS(20, 2, 4, 8, 12),
	GPIO_REGS(21, 2, 5, 8, 13),
	GPIO_REGS(22, 2, 6, 8, 14),
	GPIO_REGS(23, 2, 7, 9, 0),
	GPIO_REGS(24, 3, 0, 9, 1),
	GPIO_REGS(25, 3, 1, 9, 2),
	GPIO_REGS(26, 3, 2, 9, 3),
	GPIO_REGS(27, 3, 3, 9, 4),
	GPIO_REGS(28, 3, 4, 9, 5),
	GPIO_REGS(29, 3, 5, 9, 6),
	GPIO_REGS(30, 3, 6, 9, 7),
	GPIO_REGS(31, 3, 7, 9, 8),
	GPIO_REGS(32, 4, 0, 9, 9),
	GPIO_REGS(33, 4, 1, 9, 10),
	GPIO_REGS(34, 4, 2, 9, 11),
	GPIO_REGS(35, 4, 3, 9, 12),
	GPIO_REGS(36, 4, 4, 9, 13),
	GPIO_REGS(37, 4, 5, 9, 14),
	GPIO_REGS(38, 4, 6, 10, 0),
	GPIO_REGS(39, 4, 7, 10, 1),
	GPIO_REGS(40, 5, 0, 10, 2),
	GPIO_REGS(41, 5, 1, 10, 3),
	GPIO_REGS(42, 5, 2, 10, 4),
	GPIO_REGS(43, 5, 3, 10, 5),
	GPIO_REGS(44, 5, 4, 10, 6),
	GPIO_REGS(45, 5, 5, 10, 7),
	GPIO_REGS(46, 5, 6, 10, 8),
	GPIO_REGS(47, 5, 7, 10, 9),
	GPIO_REGS(48, 6, 0, 10, 10),
	GPIO_REGS(49, 6, 1, 10, 11),
	GPIO_REGS(50, 6, 2, 10, 12),
	GPIO_REGS(51, 6, 3, 10, 13),
	GPIO_REGS(52, 6, 4, 10, 14),
	GPIO_REGS(53, 6, 5, 11, 0),
	EMMC_REGS(54, 11, 1), /* EMMC_CMD */
	EMMC_REGS(55, 11, 2), /* EMMC_DS */
	EMMC_REGS(56, 11, 3), /* EMMC_CLK */
	EMMC_REGS(57, 11, 4), /* EMMC_DAT0 */
	EMMC_REGS(58, 11, 5), /* EMMC_DAT1 */
	EMMC_REGS(59, 11, 6), /* EMMC_DAT2 */
	EMMC_REGS(60, 11, 7), /* EMMC_DAT3 */
	EMMC_REGS(61, 11, 8), /* EMMC_DAT4 */
	EMMC_REGS(62, 11, 9), /* EMMC_DAT5 */
	EMMC_REGS(63, 11, 10), /* EMMC_DAT6 */
	EMMC_REGS(64, 11, 11), /* EMMC_DAT7 */
};

static struct pin_regs bcm2712_c0_aon_gpio_pin_regs[] = {
	AON_GPIO_REGS(0, 3, 0, 6, 10),
	AON_GPIO_REGS(1, 3, 1, 6, 11),
	AON_GPIO_REGS(2, 3, 2, 6, 12),
	AON_GPIO_REGS(3, 3, 3, 6, 13),
	AON_GPIO_REGS(4, 3, 4, 6, 14),
	AON_GPIO_REGS(5, 3, 5, 7, 0),
	AON_GPIO_REGS(6, 3, 6, 7, 1),
	AON_GPIO_REGS(7, 3, 7, 7, 2),
	AON_GPIO_REGS(8, 4, 0, 7, 3),
	AON_GPIO_REGS(9, 4, 1, 7, 4),
	AON_GPIO_REGS(10, 4, 2, 7, 5),
	AON_GPIO_REGS(11, 4, 3, 7, 6),
	AON_GPIO_REGS(12, 4, 4, 7, 7),
	AON_GPIO_REGS(13, 4, 5, 7, 8),
	AON_GPIO_REGS(14, 4, 6, 7, 9),
	AON_GPIO_REGS(15, 4, 7, 7, 10),
	AON_GPIO_REGS(16, 5, 0, 7, 11),
	AON_SGPIO_REGS(0, 0, 0),
	AON_SGPIO_REGS(1, 0, 1),
	AON_SGPIO_REGS(2, 0, 2),
	AON_SGPIO_REGS(3, 0, 3),
	AON_SGPIO_REGS(4, 1, 0),
	AON_SGPIO_REGS(5, 2, 0),
};

static const struct pinctrl_pin_desc bcm2712_c0_gpio_pins[] = {
	GPIO_PIN(0),
	GPIO_PIN(1),
	GPIO_PIN(2),
	GPIO_PIN(3),
	GPIO_PIN(4),
	GPIO_PIN(5),
	GPIO_PIN(6),
	GPIO_PIN(7),
	GPIO_PIN(8),
	GPIO_PIN(9),
	GPIO_PIN(10),
	GPIO_PIN(11),
	GPIO_PIN(12),
	GPIO_PIN(13),
	GPIO_PIN(14),
	GPIO_PIN(15),
	GPIO_PIN(16),
	GPIO_PIN(17),
	GPIO_PIN(18),
	GPIO_PIN(19),
	GPIO_PIN(20),
	GPIO_PIN(21),
	GPIO_PIN(22),
	GPIO_PIN(23),
	GPIO_PIN(24),
	GPIO_PIN(25),
	GPIO_PIN(26),
	GPIO_PIN(27),
	GPIO_PIN(28),
	GPIO_PIN(29),
	GPIO_PIN(30),
	GPIO_PIN(31),
	GPIO_PIN(32),
	GPIO_PIN(33),
	GPIO_PIN(34),
	GPIO_PIN(35),
	GPIO_PIN(36),
	GPIO_PIN(37),
	GPIO_PIN(38),
	GPIO_PIN(39),
	GPIO_PIN(40),
	GPIO_PIN(41),
	GPIO_PIN(42),
	GPIO_PIN(43),
	GPIO_PIN(44),
	GPIO_PIN(45),
	GPIO_PIN(46),
	GPIO_PIN(47),
	GPIO_PIN(48),
	GPIO_PIN(49),
	GPIO_PIN(50),
	GPIO_PIN(51),
	GPIO_PIN(52),
	GPIO_PIN(53),
	PINCTRL_PIN(54, "emmc_cmd"),
	PINCTRL_PIN(55, "emmc_ds"),
	PINCTRL_PIN(56, "emmc_clk"),
	PINCTRL_PIN(57, "emmc_dat0"),
	PINCTRL_PIN(58, "emmc_dat1"),
	PINCTRL_PIN(59, "emmc_dat2"),
	PINCTRL_PIN(60, "emmc_dat3"),
	PINCTRL_PIN(61, "emmc_dat4"),
	PINCTRL_PIN(62, "emmc_dat5"),
	PINCTRL_PIN(63, "emmc_dat6"),
	PINCTRL_PIN(64, "emmc_dat7"),
};

static struct pinctrl_pin_desc bcm2712_c0_aon_gpio_pins[] = {
	AON_GPIO_PIN(0), AON_GPIO_PIN(1), AON_GPIO_PIN(2), AON_GPIO_PIN(3),
	AON_GPIO_PIN(4), AON_GPIO_PIN(5), AON_GPIO_PIN(6), AON_GPIO_PIN(7),
	AON_GPIO_PIN(8), AON_GPIO_PIN(9), AON_GPIO_PIN(10), AON_GPIO_PIN(11),
	AON_GPIO_PIN(12), AON_GPIO_PIN(13), AON_GPIO_PIN(14), AON_GPIO_PIN(15),
	AON_GPIO_PIN(16), AON_SGPIO_PIN(0), AON_SGPIO_PIN(1), AON_SGPIO_PIN(2),
	AON_SGPIO_PIN(3), AON_SGPIO_PIN(4), AON_SGPIO_PIN(5),
};

static const struct pin_regs bcm2712_d0_gpio_pin_regs[] = {
	GPIO_REGS(1, 0, 0, 4, 5),
	GPIO_REGS(2, 0, 1, 4, 6),
	GPIO_REGS(3, 0, 2, 4, 7),
	GPIO_REGS(4, 0, 3, 4, 8),
	GPIO_REGS(10, 0, 4, 4, 9),
	GPIO_REGS(11, 0, 5, 4, 10),
	GPIO_REGS(12, 0, 6, 4, 11),
	GPIO_REGS(13, 0, 7, 4, 12),
	GPIO_REGS(14, 1, 0, 4, 13),
	GPIO_REGS(15, 1, 1, 4, 14),
	GPIO_REGS(18, 1, 2, 5, 0),
	GPIO_REGS(19, 1, 3, 5, 1),
	GPIO_REGS(20, 1, 4, 5, 2),
	GPIO_REGS(21, 1, 5, 5, 3),
	GPIO_REGS(22, 1, 6, 5, 4),
	GPIO_REGS(23, 1, 7, 5, 5),
	GPIO_REGS(24, 2, 0, 5, 6),
	GPIO_REGS(25, 2, 1, 5, 7),
	GPIO_REGS(26, 2, 2, 5, 8),
	GPIO_REGS(27, 2, 3, 5, 9),
	GPIO_REGS(28, 2, 4, 5, 10),
	GPIO_REGS(29, 2, 5, 5, 11),
	GPIO_REGS(30, 2, 6, 5, 12),
	GPIO_REGS(31, 2, 7, 5, 13),
	GPIO_REGS(32, 3, 0, 5, 14),
	GPIO_REGS(33, 3, 1, 6, 0),
	GPIO_REGS(34, 3, 2, 6, 1),
	GPIO_REGS(35, 3, 3, 6, 2),
	EMMC_REGS(36, 6, 3), /* EMMC_CMD */
	EMMC_REGS(37, 6, 4), /* EMMC_DS */
	EMMC_REGS(38, 6, 5), /* EMMC_CLK */
	EMMC_REGS(39, 6, 6), /* EMMC_DAT0 */
	EMMC_REGS(40, 6, 7), /* EMMC_DAT1 */
	EMMC_REGS(41, 6, 8), /* EMMC_DAT2 */
	EMMC_REGS(42, 6, 9), /* EMMC_DAT3 */
	EMMC_REGS(43, 6, 10), /* EMMC_DAT4 */
	EMMC_REGS(44, 6, 11), /* EMMC_DAT5 */
	EMMC_REGS(45, 6, 12), /* EMMC_DAT6 */
	EMMC_REGS(46, 6, 13), /* EMMC_DAT7 */
};

static struct pin_regs bcm2712_d0_aon_gpio_pin_regs[] = {
	AON_GPIO_REGS(0, 3, 0, 5, 9),
	AON_GPIO_REGS(1, 3, 1, 5, 10),
	AON_GPIO_REGS(2, 3, 2, 5, 11),
	AON_GPIO_REGS(3, 3, 3, 5, 12),
	AON_GPIO_REGS(4, 3, 4, 5, 13),
	AON_GPIO_REGS(5, 3, 5, 5, 14),
	AON_GPIO_REGS(6, 3, 6, 6, 0),
	AON_GPIO_REGS(8, 3, 7, 6, 1),
	AON_GPIO_REGS(9, 4, 0, 6, 2),
	AON_GPIO_REGS(12, 4, 1, 6, 3),
	AON_GPIO_REGS(13, 4, 2, 6, 4),
	AON_GPIO_REGS(14, 4, 3, 6, 5),
	AON_SGPIO_REGS(0, 0, 0),
	AON_SGPIO_REGS(1, 0, 1),
	AON_SGPIO_REGS(2, 0, 2),
	AON_SGPIO_REGS(3, 0, 3),
	AON_SGPIO_REGS(4, 1, 0),
	AON_SGPIO_REGS(5, 2, 0),
};

static const struct pinctrl_pin_desc bcm2712_d0_gpio_pins[] = {
	GPIO_PIN(1),
	GPIO_PIN(2),
	GPIO_PIN(3),
	GPIO_PIN(4),
	GPIO_PIN(10),
	GPIO_PIN(11),
	GPIO_PIN(12),
	GPIO_PIN(13),
	GPIO_PIN(14),
	GPIO_PIN(15),
	GPIO_PIN(18),
	GPIO_PIN(19),
	GPIO_PIN(20),
	GPIO_PIN(21),
	GPIO_PIN(22),
	GPIO_PIN(23),
	GPIO_PIN(24),
	GPIO_PIN(25),
	GPIO_PIN(26),
	GPIO_PIN(27),
	GPIO_PIN(28),
	GPIO_PIN(29),
	GPIO_PIN(30),
	GPIO_PIN(31),
	GPIO_PIN(32),
	GPIO_PIN(33),
	GPIO_PIN(34),
	GPIO_PIN(35),
	PINCTRL_PIN(36, "emmc_cmd"),
	PINCTRL_PIN(37, "emmc_ds"),
	PINCTRL_PIN(38, "emmc_clk"),
	PINCTRL_PIN(39, "emmc_dat0"),
	PINCTRL_PIN(40, "emmc_dat1"),
	PINCTRL_PIN(41, "emmc_dat2"),
	PINCTRL_PIN(42, "emmc_dat3"),
	PINCTRL_PIN(43, "emmc_dat4"),
	PINCTRL_PIN(44, "emmc_dat5"),
	PINCTRL_PIN(45, "emmc_dat6"),
	PINCTRL_PIN(46, "emmc_dat7"),
};

static struct pinctrl_pin_desc bcm2712_d0_aon_gpio_pins[] = {
	AON_GPIO_PIN(0), AON_GPIO_PIN(1), AON_GPIO_PIN(2), AON_GPIO_PIN(3),
	AON_GPIO_PIN(4), AON_GPIO_PIN(5), AON_GPIO_PIN(6), AON_GPIO_PIN(8),
	AON_GPIO_PIN(9), AON_GPIO_PIN(12), AON_GPIO_PIN(13), AON_GPIO_PIN(14),
	AON_SGPIO_PIN(0), AON_SGPIO_PIN(1), AON_SGPIO_PIN(2),
	AON_SGPIO_PIN(3), AON_SGPIO_PIN(4), AON_SGPIO_PIN(5),
};

static const char * const bcm2712_func_names[] = {
	BRCMSTB_FUNC(gpio),
	BRCMSTB_FUNC(alt1),
	BRCMSTB_FUNC(alt2),
	BRCMSTB_FUNC(alt3),
	BRCMSTB_FUNC(alt4),
	BRCMSTB_FUNC(alt5),
	BRCMSTB_FUNC(alt6),
	BRCMSTB_FUNC(alt7),
	BRCMSTB_FUNC(alt8),
	BRCMSTB_FUNC(aon_cpu_standbyb),
	BRCMSTB_FUNC(aon_fp_4sec_resetb),
	BRCMSTB_FUNC(aon_gpclk),
	BRCMSTB_FUNC(aon_pwm),
	BRCMSTB_FUNC(arm_jtag),
	BRCMSTB_FUNC(aud_fs_clk0),
	BRCMSTB_FUNC(avs_pmu_bsc),
	BRCMSTB_FUNC(bsc_m0),
	BRCMSTB_FUNC(bsc_m1),
	BRCMSTB_FUNC(bsc_m2),
	BRCMSTB_FUNC(bsc_m3),
	BRCMSTB_FUNC(clk_observe),
	BRCMSTB_FUNC(ctl_hdmi_5v),
	BRCMSTB_FUNC(enet0),
	BRCMSTB_FUNC(enet0_mii),
	BRCMSTB_FUNC(enet0_rgmii),
	BRCMSTB_FUNC(ext_sc_clk),
	BRCMSTB_FUNC(fl0),
	BRCMSTB_FUNC(fl1),
	BRCMSTB_FUNC(gpclk0),
	BRCMSTB_FUNC(gpclk1),
	BRCMSTB_FUNC(gpclk2),
	BRCMSTB_FUNC(hdmi_tx0_auto_i2c),
	BRCMSTB_FUNC(hdmi_tx0_bsc),
	BRCMSTB_FUNC(hdmi_tx1_auto_i2c),
	BRCMSTB_FUNC(hdmi_tx1_bsc),
	BRCMSTB_FUNC(i2s_in),
	BRCMSTB_FUNC(i2s_out),
	BRCMSTB_FUNC(ir_in),
	BRCMSTB_FUNC(mtsif),
	BRCMSTB_FUNC(mtsif_alt),
	BRCMSTB_FUNC(mtsif_alt1),
	BRCMSTB_FUNC(pdm),
	BRCMSTB_FUNC(pkt),
	BRCMSTB_FUNC(pm_led_out),
	BRCMSTB_FUNC(sc0),
	BRCMSTB_FUNC(sd0),
	BRCMSTB_FUNC(sd2),
	BRCMSTB_FUNC(sd_card_a),
	BRCMSTB_FUNC(sd_card_b),
	BRCMSTB_FUNC(sd_card_c),
	BRCMSTB_FUNC(sd_card_d),
	BRCMSTB_FUNC(sd_card_e),
	BRCMSTB_FUNC(sd_card_f),
	BRCMSTB_FUNC(sd_card_g),
	BRCMSTB_FUNC(spdif_out),
	BRCMSTB_FUNC(spi_m),
	BRCMSTB_FUNC(spi_s),
	BRCMSTB_FUNC(sr_edm_sense),
	BRCMSTB_FUNC(te0),
	BRCMSTB_FUNC(te1),
	BRCMSTB_FUNC(tsio),
	BRCMSTB_FUNC(uart0),
	BRCMSTB_FUNC(uart1),
	BRCMSTB_FUNC(uart2),
	BRCMSTB_FUNC(usb_pwr),
	BRCMSTB_FUNC(usb_vbus),
	BRCMSTB_FUNC(uui),
	BRCMSTB_FUNC(vc_i2c0),
	BRCMSTB_FUNC(vc_i2c3),
	BRCMSTB_FUNC(vc_i2c4),
	BRCMSTB_FUNC(vc_i2c5),
	BRCMSTB_FUNC(vc_i2csl),
	BRCMSTB_FUNC(vc_pcm),
	BRCMSTB_FUNC(vc_pwm0),
	BRCMSTB_FUNC(vc_pwm1),
	BRCMSTB_FUNC(vc_spi0),
	BRCMSTB_FUNC(vc_spi3),
	BRCMSTB_FUNC(vc_spi4),
	BRCMSTB_FUNC(vc_spi5),
	BRCMSTB_FUNC(vc_uart0),
	BRCMSTB_FUNC(vc_uart2),
	BRCMSTB_FUNC(vc_uart3),
	BRCMSTB_FUNC(vc_uart4),
};

static const struct brcmstb_pin_funcs bcm2712_c0_aon_gpio_pin_funcs[] = {
	BRCMSTB_PIN(0, ir_in, vc_spi0, vc_uart3, vc_i2c3, te0, vc_i2c0, _, _),
	BRCMSTB_PIN(1, vc_pwm0, vc_spi0, vc_uart3, vc_i2c3, te1, aon_pwm, vc_i2c0, vc_pwm1),
	BRCMSTB_PIN(2, vc_pwm0, vc_spi0, vc_uart3, ctl_hdmi_5v, fl0, aon_pwm, ir_in, vc_pwm1),
	BRCMSTB_PIN(3, ir_in, vc_spi0, vc_uart3, aon_fp_4sec_resetb, fl1, sd_card_g, aon_gpclk, _),
	BRCMSTB_PIN(4, gpclk0, vc_spi0, vc_i2csl, aon_gpclk, pm_led_out, aon_pwm, sd_card_g, vc_pwm0),
	BRCMSTB_PIN(5, gpclk1, ir_in, vc_i2csl, clk_observe, aon_pwm, sd_card_g, vc_pwm0, _),
	BRCMSTB_PIN(6, uart1, vc_uart4, gpclk2, ctl_hdmi_5v, vc_uart0, vc_spi3, _, _),
	BRCMSTB_PIN(7, uart1, vc_uart4, gpclk0, aon_pwm, vc_uart0, vc_spi3, _, _),
	BRCMSTB_PIN(8, uart1, vc_uart4, vc_i2csl, ctl_hdmi_5v, vc_uart0, vc_spi3, _, _),
	BRCMSTB_PIN(9, uart1, vc_uart4, vc_i2csl, aon_pwm, vc_uart0, vc_spi3, _, _),
	BRCMSTB_PIN(10, tsio, ctl_hdmi_5v, sc0, spdif_out, vc_spi5, usb_pwr, aon_gpclk, sd_card_f),
	BRCMSTB_PIN(11, tsio, uart0, sc0, aud_fs_clk0, vc_spi5, usb_vbus, vc_uart2, sd_card_f),
	BRCMSTB_PIN(12, tsio, uart0, vc_uart0, tsio, vc_spi5, usb_pwr, vc_uart2, sd_card_f),
	BRCMSTB_PIN(13, bsc_m1, uart0, vc_uart0, uui, vc_spi5, arm_jtag, vc_uart2, vc_i2c3),
	BRCMSTB_PIN(14, bsc_m1, uart0, vc_uart0, uui, vc_spi5, arm_jtag, vc_uart2, vc_i2c3),
	BRCMSTB_PIN(15, ir_in, aon_fp_4sec_resetb, vc_uart0, pm_led_out, ctl_hdmi_5v, aon_pwm, aon_gpclk, _),
	BRCMSTB_PIN(16, aon_cpu_standbyb, gpclk0, pm_led_out, ctl_hdmi_5v, vc_pwm0, usb_pwr, aud_fs_clk0, _),
};

static const struct brcmstb_pin_funcs bcm2712_c0_gpio_pin_funcs[] = {
	BRCMSTB_PIN(0, bsc_m3, vc_i2c0, gpclk0, enet0, vc_pwm1, vc_spi0, ir_in, _),
	BRCMSTB_PIN(1, bsc_m3, vc_i2c0, gpclk1, enet0, vc_pwm1, sr_edm_sense, vc_spi0, vc_uart3),
	BRCMSTB_PIN(2, pdm, i2s_in, gpclk2, vc_spi4, pkt, vc_spi0, vc_uart3, _),
	BRCMSTB_PIN(3, pdm, i2s_in, vc_spi4, pkt, vc_spi0, vc_uart3, _, _),
	BRCMSTB_PIN(4, pdm, i2s_in, arm_jtag, vc_spi4, pkt, vc_spi0, vc_uart3, _),
	BRCMSTB_PIN(5, pdm, vc_i2c3, arm_jtag, sd_card_e, vc_spi4, pkt, vc_pcm, vc_i2c5),
	BRCMSTB_PIN(6, pdm, vc_i2c3, arm_jtag, sd_card_e, vc_spi4, pkt, vc_pcm, vc_i2c5),
	BRCMSTB_PIN(7, i2s_out, spdif_out, arm_jtag, sd_card_e, vc_i2c3, enet0_rgmii, vc_pcm, vc_spi4),
	BRCMSTB_PIN(8, i2s_out, aud_fs_clk0, arm_jtag, sd_card_e, vc_i2c3, enet0_mii, vc_pcm, vc_spi4),
	BRCMSTB_PIN(9, i2s_out, aud_fs_clk0, arm_jtag, sd_card_e, enet0_mii, sd_card_c, vc_spi4, _),
	BRCMSTB_PIN(10, bsc_m3, mtsif_alt1, i2s_in, i2s_out, vc_spi5, enet0_mii, sd_card_c, vc_spi4),
	BRCMSTB_PIN(11, bsc_m3, mtsif_alt1, i2s_in, i2s_out, vc_spi5, enet0_mii, sd_card_c, vc_spi4),
	BRCMSTB_PIN(12, spi_s, mtsif_alt1, i2s_in, i2s_out, vc_spi5, vc_i2csl, sd0, sd_card_d),
	BRCMSTB_PIN(13, spi_s, mtsif_alt1, i2s_out, usb_vbus, vc_spi5, vc_i2csl, sd0, sd_card_d),
	BRCMSTB_PIN(14, spi_s, vc_i2csl, enet0_rgmii, arm_jtag, vc_spi5, vc_pwm0, vc_i2c4, sd_card_d),
	BRCMSTB_PIN(15, spi_s, vc_i2csl, vc_spi3, arm_jtag, vc_pwm0, vc_i2c4, gpclk0, _),
	BRCMSTB_PIN(16, sd_card_b, i2s_out, vc_spi3, i2s_in, sd0, enet0_rgmii, gpclk1, _),
	BRCMSTB_PIN(17, sd_card_b, i2s_out, vc_spi3, i2s_in, ext_sc_clk, sd0, enet0_rgmii, gpclk2),
	BRCMSTB_PIN(18, sd_card_b, i2s_out, vc_spi3, i2s_in, sd0, enet0_rgmii, vc_pwm1, _),
	BRCMSTB_PIN(19, sd_card_b, usb_pwr, vc_spi3, pkt, spdif_out, sd0, ir_in, vc_pwm1),
	BRCMSTB_PIN(20, sd_card_b, uui, vc_uart0, arm_jtag, uart2, usb_pwr, vc_pcm, vc_uart4),
	BRCMSTB_PIN(21, usb_pwr, uui, vc_uart0, arm_jtag, uart2, sd_card_b, vc_pcm, vc_uart4),
	BRCMSTB_PIN(22, usb_pwr, enet0, vc_uart0, mtsif, uart2, usb_vbus, vc_pcm, vc_i2c5),
	BRCMSTB_PIN(23, usb_vbus, enet0, vc_uart0, mtsif, uart2, i2s_out, vc_pcm, vc_i2c5),
	BRCMSTB_PIN(24, mtsif, pkt, uart0, enet0_rgmii, enet0_rgmii, vc_i2c4, vc_uart3, _),
	BRCMSTB_PIN(25, mtsif, pkt, sc0, uart0, enet0_rgmii, enet0_rgmii, vc_i2c4, vc_uart3),
	BRCMSTB_PIN(26, mtsif, pkt, sc0, uart0, enet0_rgmii, vc_uart4, vc_spi5, _),
	BRCMSTB_PIN(27, mtsif, pkt, sc0, uart0, enet0_rgmii, vc_uart4, vc_spi5, _),
	BRCMSTB_PIN(28, mtsif, pkt, sc0, enet0_rgmii, vc_uart4, vc_spi5, _, _),
	BRCMSTB_PIN(29, mtsif, pkt, sc0, enet0_rgmii, vc_uart4, vc_spi5, _, _),
	BRCMSTB_PIN(30, mtsif, pkt, sc0, sd2, enet0_rgmii, gpclk0, vc_pwm0, _),
	BRCMSTB_PIN(31, mtsif, pkt, sc0, sd2, enet0_rgmii, vc_spi3, vc_pwm0, _),
	BRCMSTB_PIN(32, mtsif, pkt, sc0, sd2, enet0_rgmii, vc_spi3, vc_uart3, _),
	BRCMSTB_PIN(33, mtsif, pkt, sd2, enet0_rgmii, vc_spi3, vc_uart3, _, _),
	BRCMSTB_PIN(34, mtsif, pkt, ext_sc_clk, sd2, enet0_rgmii, vc_spi3, vc_i2c5, _),
	BRCMSTB_PIN(35, mtsif, pkt, sd2, enet0_rgmii, vc_spi3, vc_i2c5, _, _),
	BRCMSTB_PIN(36, sd0, mtsif, sc0, i2s_in, vc_uart3, vc_uart2, _, _),
	BRCMSTB_PIN(37, sd0, mtsif, sc0, vc_spi0, i2s_in, vc_uart3, vc_uart2, _),
	BRCMSTB_PIN(38, sd0, mtsif_alt, sc0, vc_spi0, i2s_in, vc_uart3, vc_uart2, _),
	BRCMSTB_PIN(39, sd0, mtsif_alt, sc0, vc_spi0, vc_uart3, vc_uart2, _, _),
	BRCMSTB_PIN(40, sd0, mtsif_alt, sc0, vc_spi0, bsc_m3, _, _, _),
	BRCMSTB_PIN(41, sd0, mtsif_alt, sc0, vc_spi0, bsc_m3, _, _, _),
	BRCMSTB_PIN(42, vc_spi0, mtsif_alt, vc_i2c0, sd_card_a, mtsif_alt1, arm_jtag, pdm, spi_m),
	BRCMSTB_PIN(43, vc_spi0, mtsif_alt, vc_i2c0, sd_card_a, mtsif_alt1, arm_jtag, pdm, spi_m),
	BRCMSTB_PIN(44, vc_spi0, mtsif_alt, enet0, sd_card_a, mtsif_alt1, arm_jtag, pdm, spi_m),
	BRCMSTB_PIN(45, vc_spi0, mtsif_alt, enet0, sd_card_a, mtsif_alt1, arm_jtag, pdm, spi_m),
	BRCMSTB_PIN(46, vc_spi0, mtsif_alt, sd_card_a, mtsif_alt1, arm_jtag, pdm, spi_m, _),
	BRCMSTB_PIN(47, enet0, mtsif_alt, i2s_out, mtsif_alt1, arm_jtag, _, _, _),
	BRCMSTB_PIN(48, sc0, usb_pwr, spdif_out, mtsif, _, _, _, _),
	BRCMSTB_PIN(49, sc0, usb_pwr, aud_fs_clk0, mtsif, _, _, _, _),
	BRCMSTB_PIN(50, sc0, usb_vbus, sc0, _, _, _, _, _),
	BRCMSTB_PIN(51, sc0, enet0, sc0, sr_edm_sense, _, _, _, _),
	BRCMSTB_PIN(52, sc0, enet0, vc_pwm1, _, _, _, _, _),
	BRCMSTB_PIN(53, sc0, enet0_rgmii, ext_sc_clk, _, _, _, _, _),
};

static const struct brcmstb_pin_funcs bcm2712_d0_aon_gpio_pin_funcs[] = {
	BRCMSTB_PIN(0, ir_in, vc_spi0, vc_uart0, vc_i2c3, uart0, vc_i2c0, _, _),
	BRCMSTB_PIN(1, vc_pwm0, vc_spi0, vc_uart0, vc_i2c3, uart0, aon_pwm, vc_i2c0, vc_pwm1),
	BRCMSTB_PIN(2, vc_pwm0, vc_spi0, vc_uart0, ctl_hdmi_5v, uart0, aon_pwm, ir_in, vc_pwm1),
	BRCMSTB_PIN(3, ir_in, vc_spi0, vc_uart0, uart0, sd_card_g, aon_gpclk, _, _),
	BRCMSTB_PIN(4, gpclk0, vc_spi0, pm_led_out, aon_pwm, sd_card_g, vc_pwm0, _, _),
	BRCMSTB_PIN(5, gpclk1, ir_in, aon_pwm, sd_card_g, vc_pwm0, _, _, _),
	BRCMSTB_PIN(6, uart1, vc_uart2, ctl_hdmi_5v, gpclk2, vc_spi3, _, _, _),
	BRCMSTB_PIN(7, _, _, _, _, _, _, _, _), /* non-existent on D0 silicon */
	BRCMSTB_PIN(8, uart1, vc_uart2, ctl_hdmi_5v, vc_spi0, vc_spi3, _, _, _),
	BRCMSTB_PIN(9, uart1, vc_uart2, vc_uart0, aon_pwm, vc_spi0, vc_uart2, vc_spi3, _),
	BRCMSTB_PIN(10, _, _, _, _, _, _, _, _), /* non-existent on D0 silicon */
	BRCMSTB_PIN(11, _, _, _, _, _, _, _, _), /* non-existent on D0 silicon */
	BRCMSTB_PIN(12, uart1, vc_uart2, vc_uart0, vc_spi0, usb_pwr, vc_uart2, vc_spi3, _),
	BRCMSTB_PIN(13, bsc_m1, vc_uart0, uui, vc_spi0, arm_jtag, vc_uart2, vc_i2c3, _),
	BRCMSTB_PIN(14, bsc_m1, aon_gpclk, vc_uart0, uui, vc_spi0, arm_jtag, vc_uart2, vc_i2c3),
};

static const struct brcmstb_pin_funcs bcm2712_d0_gpio_pin_funcs[] = {
	BRCMSTB_PIN(1, vc_i2c0, usb_pwr, gpclk0, sd_card_e, vc_spi3, sr_edm_sense, vc_spi0, vc_uart0),
	BRCMSTB_PIN(2, vc_i2c0, usb_pwr, gpclk1, sd_card_e, vc_spi3, clk_observe, vc_spi0, vc_uart0),
	BRCMSTB_PIN(3, vc_i2c3, usb_vbus, gpclk2, sd_card_e, vc_spi3, vc_spi0, vc_uart0, _),
	BRCMSTB_PIN(4, vc_i2c3, vc_pwm1, vc_spi3, sd_card_e, vc_spi3, vc_spi0, vc_uart0, _),
	BRCMSTB_PIN(10, bsc_m3, vc_pwm1, vc_spi3, sd_card_e, vc_spi3, gpclk0, _, _),
	BRCMSTB_PIN(11, bsc_m3, vc_spi3, clk_observe, sd_card_c, gpclk1, _, _, _),
	BRCMSTB_PIN(12, spi_s, vc_spi3, sd_card_c, sd_card_d, _, _, _, _),
	BRCMSTB_PIN(13, spi_s, vc_spi3, sd_card_c, sd_card_d, _, _, _, _),
	BRCMSTB_PIN(14, spi_s, uui, arm_jtag, vc_pwm0, vc_i2c0, sd_card_d, _, _),
	BRCMSTB_PIN(15, spi_s, uui, arm_jtag, vc_pwm0, vc_i2c0, gpclk0, _, _),
	BRCMSTB_PIN(18, sd_card_f, vc_pwm1, _, _, _, _, _, _),
	BRCMSTB_PIN(19, sd_card_f, usb_pwr, vc_pwm1, _, _, _, _, _),
	BRCMSTB_PIN(20, vc_i2c3, uui, vc_uart0, arm_jtag, vc_uart2, _, _, _),
	BRCMSTB_PIN(21, vc_i2c3, uui, vc_uart0, arm_jtag, vc_uart2, _, _, _),
	BRCMSTB_PIN(22, sd_card_f, vc_uart0, vc_i2c3, _, _, _, _, _),
	BRCMSTB_PIN(23, vc_uart0, vc_i2c3, _, _, _, _, _, _),
	BRCMSTB_PIN(24, sd_card_b, vc_spi0, arm_jtag, uart0, usb_pwr, vc_uart2, vc_uart0, _),
	BRCMSTB_PIN(25, sd_card_b, vc_spi0, arm_jtag, uart0, usb_pwr, vc_uart2, vc_uart0, _),
	BRCMSTB_PIN(26, sd_card_b, vc_spi0, arm_jtag, uart0, usb_vbus, vc_uart2, vc_spi0, _),
	BRCMSTB_PIN(27, sd_card_b, vc_spi0, arm_jtag, uart0, vc_uart2, vc_spi0, _, _),
	BRCMSTB_PIN(28, sd_card_b, vc_spi0, arm_jtag, vc_i2c0, vc_spi0, _, _, _),
	BRCMSTB_PIN(29, arm_jtag, vc_i2c0, vc_spi0, _, _, _, _, _),
	BRCMSTB_PIN(30, sd2, gpclk0, vc_pwm0, _, _, _, _, _),
	BRCMSTB_PIN(31, sd2, vc_spi3, vc_pwm0, _, _, _, _, _),
	BRCMSTB_PIN(32, sd2, vc_spi3, vc_uart3, _, _, _, _, _),
	BRCMSTB_PIN(33, sd2, vc_spi3, vc_uart3, _, _, _, _, _),
	BRCMSTB_PIN(34, sd2, vc_spi3, vc_i2c5, _, _, _, _, _),
	BRCMSTB_PIN(35, sd2, vc_spi3, vc_i2c5, _, _, _, _, _),
};

static const struct pinctrl_desc bcm2712_c0_pinctrl_desc = {
	.name = "pinctrl-bcm2712",
	.pins = bcm2712_c0_gpio_pins,
	.npins = ARRAY_SIZE(bcm2712_c0_gpio_pins),
};

static const struct pinctrl_desc bcm2712_c0_aon_pinctrl_desc = {
	.name = "aon-pinctrl-bcm2712",
	.pins = bcm2712_c0_aon_gpio_pins,
	.npins = ARRAY_SIZE(bcm2712_c0_aon_gpio_pins),
};

static const struct pinctrl_desc bcm2712_d0_pinctrl_desc = {
	.name = "pinctrl-bcm2712",
	.pins = bcm2712_d0_gpio_pins,
	.npins = ARRAY_SIZE(bcm2712_d0_gpio_pins),
};

static const struct pinctrl_desc bcm2712_d0_aon_pinctrl_desc = {
	.name = "aon-pinctrl-bcm2712",
	.pins = bcm2712_d0_aon_gpio_pins,
	.npins = ARRAY_SIZE(bcm2712_d0_aon_gpio_pins),
};

static const struct pinctrl_gpio_range bcm2712_c0_pinctrl_gpio_range = {
	.name = "pinctrl-bcm2712",
	.npins = ARRAY_SIZE(bcm2712_c0_gpio_pins),
};

static const struct pinctrl_gpio_range bcm2712_c0_aon_pinctrl_gpio_range = {
	.name = "aon-pinctrl-bcm2712",
	.npins = ARRAY_SIZE(bcm2712_c0_aon_gpio_pins),
};

static const struct pinctrl_gpio_range bcm2712_d0_pinctrl_gpio_range = {
	.name = "pinctrl-bcm2712",
	.npins = ARRAY_SIZE(bcm2712_d0_gpio_pins),
};

static const struct pinctrl_gpio_range bcm2712_d0_aon_pinctrl_gpio_range = {
	.name = "aon-pinctrl-bcm2712",
	.npins = ARRAY_SIZE(bcm2712_d0_aon_gpio_pins),
};

static const struct brcmstb_pdata bcm2712_c0_pdata = {
	.pctl_desc = &bcm2712_c0_pinctrl_desc,
	.gpio_range = &bcm2712_c0_pinctrl_gpio_range,
	.pin_regs = bcm2712_c0_gpio_pin_regs,
	.pin_funcs = bcm2712_c0_gpio_pin_funcs,
	.func_count = func_count,
	.func_gpio = func_gpio,
	.func_names = bcm2712_func_names,
};

static const struct brcmstb_pdata bcm2712_c0_aon_pdata = {
	.pctl_desc = &bcm2712_c0_aon_pinctrl_desc,
	.gpio_range = &bcm2712_c0_aon_pinctrl_gpio_range,
	.pin_regs = bcm2712_c0_aon_gpio_pin_regs,
	.pin_funcs = bcm2712_c0_aon_gpio_pin_funcs,
	.func_count = func_count,
	.func_gpio = func_gpio,
	.func_names = bcm2712_func_names,
};

static const struct brcmstb_pdata bcm2712_d0_pdata = {
	.pctl_desc = &bcm2712_d0_pinctrl_desc,
	.gpio_range = &bcm2712_d0_pinctrl_gpio_range,
	.pin_regs = bcm2712_d0_gpio_pin_regs,
	.pin_funcs = bcm2712_d0_gpio_pin_funcs,
	.func_count = func_count,
	.func_gpio = func_gpio,
	.func_names = bcm2712_func_names,
};

static const struct brcmstb_pdata bcm2712_d0_aon_pdata = {
	.pctl_desc = &bcm2712_d0_aon_pinctrl_desc,
	.gpio_range = &bcm2712_d0_aon_pinctrl_gpio_range,
	.pin_regs = bcm2712_d0_aon_gpio_pin_regs,
	.pin_funcs = bcm2712_d0_aon_gpio_pin_funcs,
	.func_count = func_count,
	.func_gpio = func_gpio,
	.func_names = bcm2712_func_names,
};

static int bcm2712_pinctrl_probe(struct platform_device *pdev)
{
	return brcmstb_pinctrl_probe(pdev);
}

static const struct of_device_id bcm2712_pinctrl_match[] = {
	{
		.compatible = "brcm,bcm2712c0-pinctrl",
		.data = &bcm2712_c0_pdata
	},
	{
		.compatible = "brcm,bcm2712c0-aon-pinctrl",
		.data = &bcm2712_c0_aon_pdata
	},

	{
		.compatible = "brcm,bcm2712d0-pinctrl",
		.data = &bcm2712_d0_pdata
	},
	{
		.compatible = "brcm,bcm2712d0-aon-pinctrl",
		.data = &bcm2712_d0_aon_pdata
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, bcm2712_pinctrl_match);

static struct platform_driver bcm2712_pinctrl_driver = {
	.probe = bcm2712_pinctrl_probe,
	.driver = {
		.name = "pinctrl-bcm2712",
		.of_match_table = bcm2712_pinctrl_match,
		.suppress_bind_attrs = true,
	},
};
module_platform_driver(bcm2712_pinctrl_driver);

MODULE_AUTHOR("Phil Elwell");
MODULE_AUTHOR("Jonathan Bell");
MODULE_AUTHOR("Ivan T. Ivanov");
MODULE_AUTHOR("Andrea della Porta");
MODULE_DESCRIPTION("Broadcom BCM2712 pinctrl driver");
MODULE_LICENSE("GPL");

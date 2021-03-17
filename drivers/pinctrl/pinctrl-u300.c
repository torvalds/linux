// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the U300 pin controller
 *
 * Based on the original U300 padmux functions
 * Copyright (C) 2009-2011 ST-Ericsson AB
 * Author: Martin Persson <martin.persson@stericsson.com>
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * The DB3350 design and control registers are oriented around pads rather than
 * pins, so we enumerate the pads we can mux rather than actual pins. The pads
 * are connected to different pins in different packaging types, so it would
 * be confusing.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include "pinctrl-coh901.h"

/*
 * Register definitions for the U300 Padmux control registers in the
 * system controller
 */

/* PAD MUX Control register 1 (LOW) 16bit (R/W) */
#define U300_SYSCON_PMC1LR					0x007C
#define U300_SYSCON_PMC1LR_MASK					0xFFFF
#define U300_SYSCON_PMC1LR_CDI_MASK				0xC000
#define U300_SYSCON_PMC1LR_CDI_CDI				0x0000
#define U300_SYSCON_PMC1LR_CDI_EMIF				0x4000
/* For BS335 */
#define U300_SYSCON_PMC1LR_CDI_CDI2				0x8000
#define U300_SYSCON_PMC1LR_CDI_WCDMA_APP_GPIO			0xC000
/* For BS365 */
#define U300_SYSCON_PMC1LR_CDI_GPIO				0x8000
#define U300_SYSCON_PMC1LR_CDI_WCDMA				0xC000
/* Common defs */
#define U300_SYSCON_PMC1LR_PDI_MASK				0x3000
#define U300_SYSCON_PMC1LR_PDI_PDI				0x0000
#define U300_SYSCON_PMC1LR_PDI_EGG				0x1000
#define U300_SYSCON_PMC1LR_PDI_WCDMA				0x3000
#define U300_SYSCON_PMC1LR_MMCSD_MASK				0x0C00
#define U300_SYSCON_PMC1LR_MMCSD_MMCSD				0x0000
#define U300_SYSCON_PMC1LR_MMCSD_MSPRO				0x0400
#define U300_SYSCON_PMC1LR_MMCSD_DSP				0x0800
#define U300_SYSCON_PMC1LR_MMCSD_WCDMA				0x0C00
#define U300_SYSCON_PMC1LR_ETM_MASK				0x0300
#define U300_SYSCON_PMC1LR_ETM_ACC				0x0000
#define U300_SYSCON_PMC1LR_ETM_APP				0x0100
#define U300_SYSCON_PMC1LR_EMIF_1_CS2_MASK			0x00C0
#define U300_SYSCON_PMC1LR_EMIF_1_CS2_STATIC			0x0000
#define U300_SYSCON_PMC1LR_EMIF_1_CS2_NFIF			0x0040
#define U300_SYSCON_PMC1LR_EMIF_1_CS2_SDRAM			0x0080
#define U300_SYSCON_PMC1LR_EMIF_1_CS2_STATIC_2GB		0x00C0
#define U300_SYSCON_PMC1LR_EMIF_1_CS1_MASK			0x0030
#define U300_SYSCON_PMC1LR_EMIF_1_CS1_STATIC			0x0000
#define U300_SYSCON_PMC1LR_EMIF_1_CS1_NFIF			0x0010
#define U300_SYSCON_PMC1LR_EMIF_1_CS1_SDRAM			0x0020
#define U300_SYSCON_PMC1LR_EMIF_1_CS1_SEMI			0x0030
#define U300_SYSCON_PMC1LR_EMIF_1_CS0_MASK			0x000C
#define U300_SYSCON_PMC1LR_EMIF_1_CS0_STATIC			0x0000
#define U300_SYSCON_PMC1LR_EMIF_1_CS0_NFIF			0x0004
#define U300_SYSCON_PMC1LR_EMIF_1_CS0_SDRAM			0x0008
#define U300_SYSCON_PMC1LR_EMIF_1_CS0_SEMI			0x000C
#define U300_SYSCON_PMC1LR_EMIF_1_MASK				0x0003
#define U300_SYSCON_PMC1LR_EMIF_1_STATIC			0x0000
#define U300_SYSCON_PMC1LR_EMIF_1_SDRAM0			0x0001
#define U300_SYSCON_PMC1LR_EMIF_1_SDRAM1			0x0002
#define U300_SYSCON_PMC1LR_EMIF_1				0x0003
/* PAD MUX Control register 2 (HIGH) 16bit (R/W) */
#define U300_SYSCON_PMC1HR					0x007E
#define U300_SYSCON_PMC1HR_MASK					0xFFFF
#define U300_SYSCON_PMC1HR_MISC_2_MASK				0xC000
#define U300_SYSCON_PMC1HR_MISC_2_APP_GPIO			0x0000
#define U300_SYSCON_PMC1HR_MISC_2_MSPRO				0x4000
#define U300_SYSCON_PMC1HR_MISC_2_DSP				0x8000
#define U300_SYSCON_PMC1HR_MISC_2_AAIF				0xC000
#define U300_SYSCON_PMC1HR_APP_GPIO_2_MASK			0x3000
#define U300_SYSCON_PMC1HR_APP_GPIO_2_APP_GPIO			0x0000
#define U300_SYSCON_PMC1HR_APP_GPIO_2_NFIF			0x1000
#define U300_SYSCON_PMC1HR_APP_GPIO_2_DSP			0x2000
#define U300_SYSCON_PMC1HR_APP_GPIO_2_AAIF			0x3000
#define U300_SYSCON_PMC1HR_APP_GPIO_1_MASK			0x0C00
#define U300_SYSCON_PMC1HR_APP_GPIO_1_APP_GPIO			0x0000
#define U300_SYSCON_PMC1HR_APP_GPIO_1_MMC			0x0400
#define U300_SYSCON_PMC1HR_APP_GPIO_1_DSP			0x0800
#define U300_SYSCON_PMC1HR_APP_GPIO_1_AAIF			0x0C00
#define U300_SYSCON_PMC1HR_APP_SPI_CS_2_MASK			0x0300
#define U300_SYSCON_PMC1HR_APP_SPI_CS_2_APP_GPIO		0x0000
#define U300_SYSCON_PMC1HR_APP_SPI_CS_2_SPI			0x0100
#define U300_SYSCON_PMC1HR_APP_SPI_CS_2_AAIF			0x0300
#define U300_SYSCON_PMC1HR_APP_SPI_CS_1_MASK			0x00C0
#define U300_SYSCON_PMC1HR_APP_SPI_CS_1_APP_GPIO		0x0000
#define U300_SYSCON_PMC1HR_APP_SPI_CS_1_SPI			0x0040
#define U300_SYSCON_PMC1HR_APP_SPI_CS_1_AAIF			0x00C0
#define U300_SYSCON_PMC1HR_APP_SPI_2_MASK			0x0030
#define U300_SYSCON_PMC1HR_APP_SPI_2_APP_GPIO			0x0000
#define U300_SYSCON_PMC1HR_APP_SPI_2_SPI			0x0010
#define U300_SYSCON_PMC1HR_APP_SPI_2_DSP			0x0020
#define U300_SYSCON_PMC1HR_APP_SPI_2_AAIF			0x0030
#define U300_SYSCON_PMC1HR_APP_UART0_2_MASK			0x000C
#define U300_SYSCON_PMC1HR_APP_UART0_2_APP_GPIO			0x0000
#define U300_SYSCON_PMC1HR_APP_UART0_2_UART0			0x0004
#define U300_SYSCON_PMC1HR_APP_UART0_2_NFIF_CS			0x0008
#define U300_SYSCON_PMC1HR_APP_UART0_2_AAIF			0x000C
#define U300_SYSCON_PMC1HR_APP_UART0_1_MASK			0x0003
#define U300_SYSCON_PMC1HR_APP_UART0_1_APP_GPIO			0x0000
#define U300_SYSCON_PMC1HR_APP_UART0_1_UART0			0x0001
#define U300_SYSCON_PMC1HR_APP_UART0_1_AAIF			0x0003
/* Padmux 2 control */
#define U300_SYSCON_PMC2R					0x100
#define U300_SYSCON_PMC2R_APP_MISC_0_MASK			0x00C0
#define U300_SYSCON_PMC2R_APP_MISC_0_APP_GPIO			0x0000
#define U300_SYSCON_PMC2R_APP_MISC_0_EMIF_SDRAM			0x0040
#define U300_SYSCON_PMC2R_APP_MISC_0_MMC			0x0080
#define U300_SYSCON_PMC2R_APP_MISC_0_CDI2			0x00C0
#define U300_SYSCON_PMC2R_APP_MISC_1_MASK			0x0300
#define U300_SYSCON_PMC2R_APP_MISC_1_APP_GPIO			0x0000
#define U300_SYSCON_PMC2R_APP_MISC_1_EMIF_SDRAM			0x0100
#define U300_SYSCON_PMC2R_APP_MISC_1_MMC			0x0200
#define U300_SYSCON_PMC2R_APP_MISC_1_CDI2			0x0300
#define U300_SYSCON_PMC2R_APP_MISC_2_MASK			0x0C00
#define U300_SYSCON_PMC2R_APP_MISC_2_APP_GPIO			0x0000
#define U300_SYSCON_PMC2R_APP_MISC_2_EMIF_SDRAM			0x0400
#define U300_SYSCON_PMC2R_APP_MISC_2_MMC			0x0800
#define U300_SYSCON_PMC2R_APP_MISC_2_CDI2			0x0C00
#define U300_SYSCON_PMC2R_APP_MISC_3_MASK			0x3000
#define U300_SYSCON_PMC2R_APP_MISC_3_APP_GPIO			0x0000
#define U300_SYSCON_PMC2R_APP_MISC_3_EMIF_SDRAM			0x1000
#define U300_SYSCON_PMC2R_APP_MISC_3_MMC			0x2000
#define U300_SYSCON_PMC2R_APP_MISC_3_CDI2			0x3000
#define U300_SYSCON_PMC2R_APP_MISC_4_MASK			0xC000
#define U300_SYSCON_PMC2R_APP_MISC_4_APP_GPIO			0x0000
#define U300_SYSCON_PMC2R_APP_MISC_4_EMIF_SDRAM			0x4000
#define U300_SYSCON_PMC2R_APP_MISC_4_MMC			0x8000
#define U300_SYSCON_PMC2R_APP_MISC_4_ACC_GPIO			0xC000
/* TODO: More SYSCON registers missing */
#define U300_SYSCON_PMC3R					0x10C
#define U300_SYSCON_PMC3R_APP_MISC_11_MASK			0xC000
#define U300_SYSCON_PMC3R_APP_MISC_11_SPI			0x4000
#define U300_SYSCON_PMC3R_APP_MISC_10_MASK			0x3000
#define U300_SYSCON_PMC3R_APP_MISC_10_SPI			0x1000
/* TODO: Missing other configs */
#define U300_SYSCON_PMC4R					0x168
#define U300_SYSCON_PMC4R_APP_MISC_12_MASK			0x0003
#define U300_SYSCON_PMC4R_APP_MISC_12_APP_GPIO			0x0000
#define U300_SYSCON_PMC4R_APP_MISC_13_MASK			0x000C
#define U300_SYSCON_PMC4R_APP_MISC_13_CDI			0x0000
#define U300_SYSCON_PMC4R_APP_MISC_13_SMIA			0x0004
#define U300_SYSCON_PMC4R_APP_MISC_13_SMIA2			0x0008
#define U300_SYSCON_PMC4R_APP_MISC_13_APP_GPIO			0x000C
#define U300_SYSCON_PMC4R_APP_MISC_14_MASK			0x0030
#define U300_SYSCON_PMC4R_APP_MISC_14_CDI			0x0000
#define U300_SYSCON_PMC4R_APP_MISC_14_SMIA			0x0010
#define U300_SYSCON_PMC4R_APP_MISC_14_CDI2			0x0020
#define U300_SYSCON_PMC4R_APP_MISC_14_APP_GPIO			0x0030
#define U300_SYSCON_PMC4R_APP_MISC_16_MASK			0x0300
#define U300_SYSCON_PMC4R_APP_MISC_16_APP_GPIO_13		0x0000
#define U300_SYSCON_PMC4R_APP_MISC_16_APP_UART1_CTS		0x0100
#define U300_SYSCON_PMC4R_APP_MISC_16_EMIF_1_STATIC_CS5_N	0x0200

#define DRIVER_NAME "pinctrl-u300"

/*
 * The DB3350 has 467 pads, I have enumerated the pads clockwise around the
 * edges of the silicon, finger by finger. LTCORNER upper left is pad 0.
 * Data taken from the PadRing chart, arranged like this:
 *
 *   0 ..... 104
 * 466        105
 *   .        .
 *   .        .
 * 358        224
 *  357 .... 225
 */
#define U300_NUM_PADS 467

/* Pad names for the pinmux subsystem */
static const struct pinctrl_pin_desc u300_pads[] = {
	/* Pads along the top edge of the chip */
	PINCTRL_PIN(0, "P PAD VDD 28"),
	PINCTRL_PIN(1, "P PAD GND 28"),
	PINCTRL_PIN(2, "PO SIM RST N"),
	PINCTRL_PIN(3, "VSSIO 25"),
	PINCTRL_PIN(4, "VSSA ADDA ESDSUB"),
	PINCTRL_PIN(5, "PWR VSSCOMMON"),
	PINCTRL_PIN(6, "PI ADC I1 POS"),
	PINCTRL_PIN(7, "PI ADC I1 NEG"),
	PINCTRL_PIN(8, "PWR VSSAD0"),
	PINCTRL_PIN(9, "PWR VCCAD0"),
	PINCTRL_PIN(10, "PI ADC Q1 NEG"),
	PINCTRL_PIN(11, "PI ADC Q1 POS"),
	PINCTRL_PIN(12, "PWR VDDAD"),
	PINCTRL_PIN(13, "PWR GNDAD"),
	PINCTRL_PIN(14, "PI ADC I2 POS"),
	PINCTRL_PIN(15, "PI ADC I2 NEG"),
	PINCTRL_PIN(16, "PWR VSSAD1"),
	PINCTRL_PIN(17, "PWR VCCAD1"),
	PINCTRL_PIN(18, "PI ADC Q2 NEG"),
	PINCTRL_PIN(19, "PI ADC Q2 POS"),
	PINCTRL_PIN(20, "VSSA ADDA ESDSUB"),
	PINCTRL_PIN(21, "PWR VCCGPAD"),
	PINCTRL_PIN(22, "PI TX POW"),
	PINCTRL_PIN(23, "PWR VSSGPAD"),
	PINCTRL_PIN(24, "PO DAC I POS"),
	PINCTRL_PIN(25, "PO DAC I NEG"),
	PINCTRL_PIN(26, "PO DAC Q POS"),
	PINCTRL_PIN(27, "PO DAC Q NEG"),
	PINCTRL_PIN(28, "PWR VSSDA"),
	PINCTRL_PIN(29, "PWR VCCDA"),
	PINCTRL_PIN(30, "VSSA ADDA ESDSUB"),
	PINCTRL_PIN(31, "P PAD VDDIO 11"),
	PINCTRL_PIN(32, "PI PLL 26 FILTVDD"),
	PINCTRL_PIN(33, "PI PLL 26 VCONT"),
	PINCTRL_PIN(34, "PWR AGNDPLL2V5 32 13"),
	PINCTRL_PIN(35, "PWR AVDDPLL2V5 32 13"),
	PINCTRL_PIN(36, "VDDA PLL ESD"),
	PINCTRL_PIN(37, "VSSA PLL ESD"),
	PINCTRL_PIN(38, "VSS PLL"),
	PINCTRL_PIN(39, "VDDC PLL"),
	PINCTRL_PIN(40, "PWR AGNDPLL2V5 26 60"),
	PINCTRL_PIN(41, "PWR AVDDPLL2V5 26 60"),
	PINCTRL_PIN(42, "PWR AVDDPLL2V5 26 208"),
	PINCTRL_PIN(43, "PWR AGNDPLL2V5 26 208"),
	PINCTRL_PIN(44, "PWR AVDDPLL2V5 13 208"),
	PINCTRL_PIN(45, "PWR AGNDPLL2V5 13 208"),
	PINCTRL_PIN(46, "P PAD VSSIO 11"),
	PINCTRL_PIN(47, "P PAD VSSIO 12"),
	PINCTRL_PIN(48, "PI POW RST N"),
	PINCTRL_PIN(49, "VDDC IO"),
	PINCTRL_PIN(50, "P PAD VDDIO 16"),
	PINCTRL_PIN(51, "PO RF WCDMA EN 4"),
	PINCTRL_PIN(52, "PO RF WCDMA EN 3"),
	PINCTRL_PIN(53, "PO RF WCDMA EN 2"),
	PINCTRL_PIN(54, "PO RF WCDMA EN 1"),
	PINCTRL_PIN(55, "PO RF WCDMA EN 0"),
	PINCTRL_PIN(56, "PO GSM PA ENABLE"),
	PINCTRL_PIN(57, "PO RF DATA STRB"),
	PINCTRL_PIN(58, "PO RF DATA2"),
	PINCTRL_PIN(59, "PIO RF DATA1"),
	PINCTRL_PIN(60, "PIO RF DATA0"),
	PINCTRL_PIN(61, "P PAD VDD 11"),
	PINCTRL_PIN(62, "P PAD GND 11"),
	PINCTRL_PIN(63, "P PAD VSSIO 16"),
	PINCTRL_PIN(64, "P PAD VDDIO 18"),
	PINCTRL_PIN(65, "PO RF CTRL STRB2"),
	PINCTRL_PIN(66, "PO RF CTRL STRB1"),
	PINCTRL_PIN(67, "PO RF CTRL STRB0"),
	PINCTRL_PIN(68, "PIO RF CTRL DATA"),
	PINCTRL_PIN(69, "PO RF CTRL CLK"),
	PINCTRL_PIN(70, "PO TX ADC STRB"),
	PINCTRL_PIN(71, "PO ANT SW 2"),
	PINCTRL_PIN(72, "PO ANT SW 3"),
	PINCTRL_PIN(73, "PO ANT SW 0"),
	PINCTRL_PIN(74, "PO ANT SW 1"),
	PINCTRL_PIN(75, "PO M CLKRQ"),
	PINCTRL_PIN(76, "PI M CLK"),
	PINCTRL_PIN(77, "PI RTC CLK"),
	PINCTRL_PIN(78, "P PAD VDD 8"),
	PINCTRL_PIN(79, "P PAD GND 8"),
	PINCTRL_PIN(80, "P PAD VSSIO 13"),
	PINCTRL_PIN(81, "P PAD VDDIO 13"),
	PINCTRL_PIN(82, "PO SYS 1 CLK"),
	PINCTRL_PIN(83, "PO SYS 2 CLK"),
	PINCTRL_PIN(84, "PO SYS 0 CLK"),
	PINCTRL_PIN(85, "PI SYS 0 CLKRQ"),
	PINCTRL_PIN(86, "PO PWR MNGT CTRL 1"),
	PINCTRL_PIN(87, "PO PWR MNGT CTRL 0"),
	PINCTRL_PIN(88, "PO RESOUT2 RST N"),
	PINCTRL_PIN(89, "PO RESOUT1 RST N"),
	PINCTRL_PIN(90, "PO RESOUT0 RST N"),
	PINCTRL_PIN(91, "PI SERVICE N"),
	PINCTRL_PIN(92, "P PAD VDD 29"),
	PINCTRL_PIN(93, "P PAD GND 29"),
	PINCTRL_PIN(94, "P PAD VSSIO 8"),
	PINCTRL_PIN(95, "P PAD VDDIO 8"),
	PINCTRL_PIN(96, "PI EXT IRQ1 N"),
	PINCTRL_PIN(97, "PI EXT IRQ0 N"),
	PINCTRL_PIN(98, "PIO DC ON"),
	PINCTRL_PIN(99, "PIO ACC APP I2C DATA"),
	PINCTRL_PIN(100, "PIO ACC APP I2C CLK"),
	PINCTRL_PIN(101, "P PAD VDD 12"),
	PINCTRL_PIN(102, "P PAD GND 12"),
	PINCTRL_PIN(103, "P PAD VSSIO 14"),
	PINCTRL_PIN(104, "P PAD VDDIO 14"),
	/* Pads along the right edge of the chip */
	PINCTRL_PIN(105, "PIO APP I2C1 DATA"),
	PINCTRL_PIN(106, "PIO APP I2C1 CLK"),
	PINCTRL_PIN(107, "PO KEY OUT0"),
	PINCTRL_PIN(108, "PO KEY OUT1"),
	PINCTRL_PIN(109, "PO KEY OUT2"),
	PINCTRL_PIN(110, "PO KEY OUT3"),
	PINCTRL_PIN(111, "PO KEY OUT4"),
	PINCTRL_PIN(112, "PI KEY IN0"),
	PINCTRL_PIN(113, "PI KEY IN1"),
	PINCTRL_PIN(114, "PI KEY IN2"),
	PINCTRL_PIN(115, "P PAD VDDIO 15"),
	PINCTRL_PIN(116, "P PAD VSSIO 15"),
	PINCTRL_PIN(117, "P PAD GND 13"),
	PINCTRL_PIN(118, "P PAD VDD 13"),
	PINCTRL_PIN(119, "PI KEY IN3"),
	PINCTRL_PIN(120, "PI KEY IN4"),
	PINCTRL_PIN(121, "PI KEY IN5"),
	PINCTRL_PIN(122, "PIO APP PCM I2S1 DATA B"),
	PINCTRL_PIN(123, "PIO APP PCM I2S1 DATA A"),
	PINCTRL_PIN(124, "PIO APP PCM I2S1 WS"),
	PINCTRL_PIN(125, "PIO APP PCM I2S1 CLK"),
	PINCTRL_PIN(126, "PIO APP PCM I2S0 DATA B"),
	PINCTRL_PIN(127, "PIO APP PCM I2S0 DATA A"),
	PINCTRL_PIN(128, "PIO APP PCM I2S0 WS"),
	PINCTRL_PIN(129, "PIO APP PCM I2S0 CLK"),
	PINCTRL_PIN(130, "P PAD VDD 17"),
	PINCTRL_PIN(131, "P PAD GND 17"),
	PINCTRL_PIN(132, "P PAD VSSIO 19"),
	PINCTRL_PIN(133, "P PAD VDDIO 19"),
	PINCTRL_PIN(134, "UART0 RTS"),
	PINCTRL_PIN(135, "UART0 CTS"),
	PINCTRL_PIN(136, "UART0 TX"),
	PINCTRL_PIN(137, "UART0 RX"),
	PINCTRL_PIN(138, "PIO ACC SPI DO"),
	PINCTRL_PIN(139, "PIO ACC SPI DI"),
	PINCTRL_PIN(140, "PIO ACC SPI CS0 N"),
	PINCTRL_PIN(141, "PIO ACC SPI CS1 N"),
	PINCTRL_PIN(142, "PIO ACC SPI CS2 N"),
	PINCTRL_PIN(143, "PIO ACC SPI CLK"),
	PINCTRL_PIN(144, "PO PDI EXT RST N"),
	PINCTRL_PIN(145, "P PAD VDDIO 22"),
	PINCTRL_PIN(146, "P PAD VSSIO 22"),
	PINCTRL_PIN(147, "P PAD GND 18"),
	PINCTRL_PIN(148, "P PAD VDD 18"),
	PINCTRL_PIN(149, "PIO PDI C0"),
	PINCTRL_PIN(150, "PIO PDI C1"),
	PINCTRL_PIN(151, "PIO PDI C2"),
	PINCTRL_PIN(152, "PIO PDI C3"),
	PINCTRL_PIN(153, "PIO PDI C4"),
	PINCTRL_PIN(154, "PIO PDI C5"),
	PINCTRL_PIN(155, "PIO PDI D0"),
	PINCTRL_PIN(156, "PIO PDI D1"),
	PINCTRL_PIN(157, "PIO PDI D2"),
	PINCTRL_PIN(158, "PIO PDI D3"),
	PINCTRL_PIN(159, "P PAD VDDIO 21"),
	PINCTRL_PIN(160, "P PAD VSSIO 21"),
	PINCTRL_PIN(161, "PIO PDI D4"),
	PINCTRL_PIN(162, "PIO PDI D5"),
	PINCTRL_PIN(163, "PIO PDI D6"),
	PINCTRL_PIN(164, "PIO PDI D7"),
	PINCTRL_PIN(165, "PIO MS INS"),
	PINCTRL_PIN(166, "MMC DATA DIR LS"),
	PINCTRL_PIN(167, "MMC DATA 3"),
	PINCTRL_PIN(168, "MMC DATA 2"),
	PINCTRL_PIN(169, "MMC DATA 1"),
	PINCTRL_PIN(170, "MMC DATA 0"),
	PINCTRL_PIN(171, "MMC CMD DIR LS"),
	PINCTRL_PIN(172, "P PAD VDD 27"),
	PINCTRL_PIN(173, "P PAD GND 27"),
	PINCTRL_PIN(174, "P PAD VSSIO 20"),
	PINCTRL_PIN(175, "P PAD VDDIO 20"),
	PINCTRL_PIN(176, "MMC CMD"),
	PINCTRL_PIN(177, "MMC CLK"),
	PINCTRL_PIN(178, "PIO APP GPIO 14"),
	PINCTRL_PIN(179, "PIO APP GPIO 13"),
	PINCTRL_PIN(180, "PIO APP GPIO 11"),
	PINCTRL_PIN(181, "PIO APP GPIO 25"),
	PINCTRL_PIN(182, "PIO APP GPIO 24"),
	PINCTRL_PIN(183, "PIO APP GPIO 23"),
	PINCTRL_PIN(184, "PIO APP GPIO 22"),
	PINCTRL_PIN(185, "PIO APP GPIO 21"),
	PINCTRL_PIN(186, "PIO APP GPIO 20"),
	PINCTRL_PIN(187, "P PAD VDD 19"),
	PINCTRL_PIN(188, "P PAD GND 19"),
	PINCTRL_PIN(189, "P PAD VSSIO 23"),
	PINCTRL_PIN(190, "P PAD VDDIO 23"),
	PINCTRL_PIN(191, "PIO APP GPIO 19"),
	PINCTRL_PIN(192, "PIO APP GPIO 18"),
	PINCTRL_PIN(193, "PIO APP GPIO 17"),
	PINCTRL_PIN(194, "PIO APP GPIO 16"),
	PINCTRL_PIN(195, "PI CI D1"),
	PINCTRL_PIN(196, "PI CI D0"),
	PINCTRL_PIN(197, "PI CI HSYNC"),
	PINCTRL_PIN(198, "PI CI VSYNC"),
	PINCTRL_PIN(199, "PI CI EXT CLK"),
	PINCTRL_PIN(200, "PO CI EXT RST N"),
	PINCTRL_PIN(201, "P PAD VSSIO 43"),
	PINCTRL_PIN(202, "P PAD VDDIO 43"),
	PINCTRL_PIN(203, "PI CI D6"),
	PINCTRL_PIN(204, "PI CI D7"),
	PINCTRL_PIN(205, "PI CI D2"),
	PINCTRL_PIN(206, "PI CI D3"),
	PINCTRL_PIN(207, "PI CI D4"),
	PINCTRL_PIN(208, "PI CI D5"),
	PINCTRL_PIN(209, "PI CI D8"),
	PINCTRL_PIN(210, "PI CI D9"),
	PINCTRL_PIN(211, "P PAD VDD 20"),
	PINCTRL_PIN(212, "P PAD GND 20"),
	PINCTRL_PIN(213, "P PAD VSSIO 24"),
	PINCTRL_PIN(214, "P PAD VDDIO 24"),
	PINCTRL_PIN(215, "P PAD VDDIO 26"),
	PINCTRL_PIN(216, "PO EMIF 1 A26"),
	PINCTRL_PIN(217, "PO EMIF 1 A25"),
	PINCTRL_PIN(218, "P PAD VSSIO 26"),
	PINCTRL_PIN(219, "PO EMIF 1 A24"),
	PINCTRL_PIN(220, "PO EMIF 1 A23"),
	/* Pads along the bottom edge of the chip */
	PINCTRL_PIN(221, "PO EMIF 1 A22"),
	PINCTRL_PIN(222, "PO EMIF 1 A21"),
	PINCTRL_PIN(223, "P PAD VDD 21"),
	PINCTRL_PIN(224, "P PAD GND 21"),
	PINCTRL_PIN(225, "P PAD VSSIO 27"),
	PINCTRL_PIN(226, "P PAD VDDIO 27"),
	PINCTRL_PIN(227, "PO EMIF 1 A20"),
	PINCTRL_PIN(228, "PO EMIF 1 A19"),
	PINCTRL_PIN(229, "PO EMIF 1 A18"),
	PINCTRL_PIN(230, "PO EMIF 1 A17"),
	PINCTRL_PIN(231, "P PAD VDDIO 28"),
	PINCTRL_PIN(232, "P PAD VSSIO 28"),
	PINCTRL_PIN(233, "PO EMIF 1 A16"),
	PINCTRL_PIN(234, "PIO EMIF 1 D15"),
	PINCTRL_PIN(235, "PO EMIF 1 A15"),
	PINCTRL_PIN(236, "PIO EMIF 1 D14"),
	PINCTRL_PIN(237, "P PAD VDD 22"),
	PINCTRL_PIN(238, "P PAD GND 22"),
	PINCTRL_PIN(239, "P PAD VSSIO 29"),
	PINCTRL_PIN(240, "P PAD VDDIO 29"),
	PINCTRL_PIN(241, "PO EMIF 1 A14"),
	PINCTRL_PIN(242, "PIO EMIF 1 D13"),
	PINCTRL_PIN(243, "PO EMIF 1 A13"),
	PINCTRL_PIN(244, "PIO EMIF 1 D12"),
	PINCTRL_PIN(245, "P PAD VSSIO 30"),
	PINCTRL_PIN(246, "P PAD VDDIO 30"),
	PINCTRL_PIN(247, "PO EMIF 1 A12"),
	PINCTRL_PIN(248, "PIO EMIF 1 D11"),
	PINCTRL_PIN(249, "PO EMIF 1 A11"),
	PINCTRL_PIN(250, "PIO EMIF 1 D10"),
	PINCTRL_PIN(251, "P PAD VSSIO 31"),
	PINCTRL_PIN(252, "P PAD VDDIO 31"),
	PINCTRL_PIN(253, "PO EMIF 1 A10"),
	PINCTRL_PIN(254, "PIO EMIF 1 D09"),
	PINCTRL_PIN(255, "PO EMIF 1 A09"),
	PINCTRL_PIN(256, "P PAD VDDIO 32"),
	PINCTRL_PIN(257, "P PAD VSSIO 32"),
	PINCTRL_PIN(258, "P PAD GND 24"),
	PINCTRL_PIN(259, "P PAD VDD 24"),
	PINCTRL_PIN(260, "PIO EMIF 1 D08"),
	PINCTRL_PIN(261, "PO EMIF 1 A08"),
	PINCTRL_PIN(262, "PIO EMIF 1 D07"),
	PINCTRL_PIN(263, "PO EMIF 1 A07"),
	PINCTRL_PIN(264, "P PAD VDDIO 33"),
	PINCTRL_PIN(265, "P PAD VSSIO 33"),
	PINCTRL_PIN(266, "PIO EMIF 1 D06"),
	PINCTRL_PIN(267, "PO EMIF 1 A06"),
	PINCTRL_PIN(268, "PIO EMIF 1 D05"),
	PINCTRL_PIN(269, "PO EMIF 1 A05"),
	PINCTRL_PIN(270, "P PAD VDDIO 34"),
	PINCTRL_PIN(271, "P PAD VSSIO 34"),
	PINCTRL_PIN(272, "PIO EMIF 1 D04"),
	PINCTRL_PIN(273, "PO EMIF 1 A04"),
	PINCTRL_PIN(274, "PIO EMIF 1 D03"),
	PINCTRL_PIN(275, "PO EMIF 1 A03"),
	PINCTRL_PIN(276, "P PAD VDDIO 35"),
	PINCTRL_PIN(277, "P PAD VSSIO 35"),
	PINCTRL_PIN(278, "P PAD GND 23"),
	PINCTRL_PIN(279, "P PAD VDD 23"),
	PINCTRL_PIN(280, "PIO EMIF 1 D02"),
	PINCTRL_PIN(281, "PO EMIF 1 A02"),
	PINCTRL_PIN(282, "PIO EMIF 1 D01"),
	PINCTRL_PIN(283, "PO EMIF 1 A01"),
	PINCTRL_PIN(284, "P PAD VDDIO 36"),
	PINCTRL_PIN(285, "P PAD VSSIO 36"),
	PINCTRL_PIN(286, "PIO EMIF 1 D00"),
	PINCTRL_PIN(287, "PO EMIF 1 BE1 N"),
	PINCTRL_PIN(288, "PO EMIF 1 BE0 N"),
	PINCTRL_PIN(289, "PO EMIF 1 ADV N"),
	PINCTRL_PIN(290, "P PAD VDDIO 37"),
	PINCTRL_PIN(291, "P PAD VSSIO 37"),
	PINCTRL_PIN(292, "PO EMIF 1 SD CKE0"),
	PINCTRL_PIN(293, "PO EMIF 1 OE N"),
	PINCTRL_PIN(294, "PO EMIF 1 WE N"),
	PINCTRL_PIN(295, "P PAD VDDIO 38"),
	PINCTRL_PIN(296, "P PAD VSSIO 38"),
	PINCTRL_PIN(297, "PO EMIF 1 CLK"),
	PINCTRL_PIN(298, "PIO EMIF 1 SD CLK"),
	PINCTRL_PIN(299, "P PAD VSSIO 45 (not bonded)"),
	PINCTRL_PIN(300, "P PAD VDDIO 42"),
	PINCTRL_PIN(301, "P PAD VSSIO 42"),
	PINCTRL_PIN(302, "P PAD GND 31"),
	PINCTRL_PIN(303, "P PAD VDD 31"),
	PINCTRL_PIN(304, "PI EMIF 1 RET CLK"),
	PINCTRL_PIN(305, "PI EMIF 1 WAIT N"),
	PINCTRL_PIN(306, "PI EMIF 1 NFIF READY"),
	PINCTRL_PIN(307, "PO EMIF 1 SD CKE1"),
	PINCTRL_PIN(308, "PO EMIF 1 CS3 N"),
	PINCTRL_PIN(309, "P PAD VDD 25"),
	PINCTRL_PIN(310, "P PAD GND 25"),
	PINCTRL_PIN(311, "P PAD VSSIO 39"),
	PINCTRL_PIN(312, "P PAD VDDIO 39"),
	PINCTRL_PIN(313, "PO EMIF 1 CS2 N"),
	PINCTRL_PIN(314, "PO EMIF 1 CS1 N"),
	PINCTRL_PIN(315, "PO EMIF 1 CS0 N"),
	PINCTRL_PIN(316, "PO ETM TRACE PKT0"),
	PINCTRL_PIN(317, "PO ETM TRACE PKT1"),
	PINCTRL_PIN(318, "PO ETM TRACE PKT2"),
	PINCTRL_PIN(319, "P PAD VDD 30"),
	PINCTRL_PIN(320, "P PAD GND 30"),
	PINCTRL_PIN(321, "P PAD VSSIO 44"),
	PINCTRL_PIN(322, "P PAD VDDIO 44"),
	PINCTRL_PIN(323, "PO ETM TRACE PKT3"),
	PINCTRL_PIN(324, "PO ETM TRACE PKT4"),
	PINCTRL_PIN(325, "PO ETM TRACE PKT5"),
	PINCTRL_PIN(326, "PO ETM TRACE PKT6"),
	PINCTRL_PIN(327, "PO ETM TRACE PKT7"),
	PINCTRL_PIN(328, "PO ETM PIPE STAT0"),
	PINCTRL_PIN(329, "P PAD VDD 26"),
	PINCTRL_PIN(330, "P PAD GND 26"),
	PINCTRL_PIN(331, "P PAD VSSIO 40"),
	PINCTRL_PIN(332, "P PAD VDDIO 40"),
	PINCTRL_PIN(333, "PO ETM PIPE STAT1"),
	PINCTRL_PIN(334, "PO ETM PIPE STAT2"),
	PINCTRL_PIN(335, "PO ETM TRACE CLK"),
	PINCTRL_PIN(336, "PO ETM TRACE SYNC"),
	PINCTRL_PIN(337, "PIO ACC GPIO 33"),
	PINCTRL_PIN(338, "PIO ACC GPIO 32"),
	PINCTRL_PIN(339, "PIO ACC GPIO 30"),
	PINCTRL_PIN(340, "PIO ACC GPIO 29"),
	PINCTRL_PIN(341, "P PAD VDDIO 17"),
	PINCTRL_PIN(342, "P PAD VSSIO 17"),
	PINCTRL_PIN(343, "P PAD GND 15"),
	PINCTRL_PIN(344, "P PAD VDD 15"),
	PINCTRL_PIN(345, "PIO ACC GPIO 28"),
	PINCTRL_PIN(346, "PIO ACC GPIO 27"),
	PINCTRL_PIN(347, "PIO ACC GPIO 16"),
	PINCTRL_PIN(348, "PI TAP TMS"),
	PINCTRL_PIN(349, "PI TAP TDI"),
	PINCTRL_PIN(350, "PO TAP TDO"),
	PINCTRL_PIN(351, "PI TAP RST N"),
	/* Pads along the left edge of the chip */
	PINCTRL_PIN(352, "PI EMU MODE 0"),
	PINCTRL_PIN(353, "PO TAP RET CLK"),
	PINCTRL_PIN(354, "PI TAP CLK"),
	PINCTRL_PIN(355, "PO EMIF 0 SD CS N"),
	PINCTRL_PIN(356, "PO EMIF 0 SD CAS N"),
	PINCTRL_PIN(357, "PO EMIF 0 SD WE N"),
	PINCTRL_PIN(358, "P PAD VDDIO 1"),
	PINCTRL_PIN(359, "P PAD VSSIO 1"),
	PINCTRL_PIN(360, "P PAD GND 1"),
	PINCTRL_PIN(361, "P PAD VDD 1"),
	PINCTRL_PIN(362, "PO EMIF 0 SD CKE"),
	PINCTRL_PIN(363, "PO EMIF 0 SD DQML"),
	PINCTRL_PIN(364, "PO EMIF 0 SD DQMU"),
	PINCTRL_PIN(365, "PO EMIF 0 SD RAS N"),
	PINCTRL_PIN(366, "PIO EMIF 0 D15"),
	PINCTRL_PIN(367, "PO EMIF 0 A15"),
	PINCTRL_PIN(368, "PIO EMIF 0 D14"),
	PINCTRL_PIN(369, "PO EMIF 0 A14"),
	PINCTRL_PIN(370, "PIO EMIF 0 D13"),
	PINCTRL_PIN(371, "PO EMIF 0 A13"),
	PINCTRL_PIN(372, "P PAD VDDIO 2"),
	PINCTRL_PIN(373, "P PAD VSSIO 2"),
	PINCTRL_PIN(374, "P PAD GND 2"),
	PINCTRL_PIN(375, "P PAD VDD 2"),
	PINCTRL_PIN(376, "PIO EMIF 0 D12"),
	PINCTRL_PIN(377, "PO EMIF 0 A12"),
	PINCTRL_PIN(378, "PIO EMIF 0 D11"),
	PINCTRL_PIN(379, "PO EMIF 0 A11"),
	PINCTRL_PIN(380, "PIO EMIF 0 D10"),
	PINCTRL_PIN(381, "PO EMIF 0 A10"),
	PINCTRL_PIN(382, "PIO EMIF 0 D09"),
	PINCTRL_PIN(383, "PO EMIF 0 A09"),
	PINCTRL_PIN(384, "PIO EMIF 0 D08"),
	PINCTRL_PIN(385, "PO EMIF 0 A08"),
	PINCTRL_PIN(386, "PIO EMIF 0 D07"),
	PINCTRL_PIN(387, "PO EMIF 0 A07"),
	PINCTRL_PIN(388, "P PAD VDDIO 3"),
	PINCTRL_PIN(389, "P PAD VSSIO 3"),
	PINCTRL_PIN(390, "P PAD GND 3"),
	PINCTRL_PIN(391, "P PAD VDD 3"),
	PINCTRL_PIN(392, "PO EFUSE RDOUT1"),
	PINCTRL_PIN(393, "PIO EMIF 0 D06"),
	PINCTRL_PIN(394, "PO EMIF 0 A06"),
	PINCTRL_PIN(395, "PIO EMIF 0 D05"),
	PINCTRL_PIN(396, "PO EMIF 0 A05"),
	PINCTRL_PIN(397, "PIO EMIF 0 D04"),
	PINCTRL_PIN(398, "PO EMIF 0 A04"),
	PINCTRL_PIN(399, "A PADS/A VDDCO1v82v5 GND 80U SF LIN VDDCO AF"),
	PINCTRL_PIN(400, "PWR VDDCO AF"),
	PINCTRL_PIN(401, "PWR EFUSE HV1"),
	PINCTRL_PIN(402, "P PAD VSSIO 4"),
	PINCTRL_PIN(403, "P PAD VDDIO 4"),
	PINCTRL_PIN(404, "P PAD GND 4"),
	PINCTRL_PIN(405, "P PAD VDD 4"),
	PINCTRL_PIN(406, "PIO EMIF 0 D03"),
	PINCTRL_PIN(407, "PO EMIF 0 A03"),
	PINCTRL_PIN(408, "PWR EFUSE HV2"),
	PINCTRL_PIN(409, "PWR EFUSE HV3"),
	PINCTRL_PIN(410, "PIO EMIF 0 D02"),
	PINCTRL_PIN(411, "PO EMIF 0 A02"),
	PINCTRL_PIN(412, "PIO EMIF 0 D01"),
	PINCTRL_PIN(413, "P PAD VDDIO 5"),
	PINCTRL_PIN(414, "P PAD VSSIO 5"),
	PINCTRL_PIN(415, "P PAD GND 5"),
	PINCTRL_PIN(416, "P PAD VDD 5"),
	PINCTRL_PIN(417, "PO EMIF 0 A01"),
	PINCTRL_PIN(418, "PIO EMIF 0 D00"),
	PINCTRL_PIN(419, "IF 0 SD CLK"),
	PINCTRL_PIN(420, "APP SPI CLK"),
	PINCTRL_PIN(421, "APP SPI DO"),
	PINCTRL_PIN(422, "APP SPI DI"),
	PINCTRL_PIN(423, "APP SPI CS0"),
	PINCTRL_PIN(424, "APP SPI CS1"),
	PINCTRL_PIN(425, "APP SPI CS2"),
	PINCTRL_PIN(426, "PIO APP GPIO 10"),
	PINCTRL_PIN(427, "P PAD VDDIO 41"),
	PINCTRL_PIN(428, "P PAD VSSIO 41"),
	PINCTRL_PIN(429, "P PAD GND 6"),
	PINCTRL_PIN(430, "P PAD VDD 6"),
	PINCTRL_PIN(431, "PIO ACC SDIO0 CMD"),
	PINCTRL_PIN(432, "PIO ACC SDIO0 CK"),
	PINCTRL_PIN(433, "PIO ACC SDIO0 D3"),
	PINCTRL_PIN(434, "PIO ACC SDIO0 D2"),
	PINCTRL_PIN(435, "PIO ACC SDIO0 D1"),
	PINCTRL_PIN(436, "PIO ACC SDIO0 D0"),
	PINCTRL_PIN(437, "PIO USB PU"),
	PINCTRL_PIN(438, "PIO USB SP"),
	PINCTRL_PIN(439, "PIO USB DAT VP"),
	PINCTRL_PIN(440, "PIO USB SE0 VM"),
	PINCTRL_PIN(441, "PIO USB OE"),
	PINCTRL_PIN(442, "PIO USB SUSP"),
	PINCTRL_PIN(443, "P PAD VSSIO 6"),
	PINCTRL_PIN(444, "P PAD VDDIO 6"),
	PINCTRL_PIN(445, "PIO USB PUEN"),
	PINCTRL_PIN(446, "PIO ACC UART0 RX"),
	PINCTRL_PIN(447, "PIO ACC UART0 TX"),
	PINCTRL_PIN(448, "PIO ACC UART0 CTS"),
	PINCTRL_PIN(449, "PIO ACC UART0 RTS"),
	PINCTRL_PIN(450, "PIO ACC UART3 RX"),
	PINCTRL_PIN(451, "PIO ACC UART3 TX"),
	PINCTRL_PIN(452, "PIO ACC UART3 CTS"),
	PINCTRL_PIN(453, "PIO ACC UART3 RTS"),
	PINCTRL_PIN(454, "PIO ACC IRDA TX"),
	PINCTRL_PIN(455, "P PAD VDDIO 7"),
	PINCTRL_PIN(456, "P PAD VSSIO 7"),
	PINCTRL_PIN(457, "P PAD GND 7"),
	PINCTRL_PIN(458, "P PAD VDD 7"),
	PINCTRL_PIN(459, "PIO ACC IRDA RX"),
	PINCTRL_PIN(460, "PIO ACC PCM I2S CLK"),
	PINCTRL_PIN(461, "PIO ACC PCM I2S WS"),
	PINCTRL_PIN(462, "PIO ACC PCM I2S DATA A"),
	PINCTRL_PIN(463, "PIO ACC PCM I2S DATA B"),
	PINCTRL_PIN(464, "PO SIM CLK"),
	PINCTRL_PIN(465, "PIO ACC IRDA SD"),
	PINCTRL_PIN(466, "PIO SIM DATA"),
};

/**
 * @dev: a pointer back to containing device
 * @virtbase: the offset to the controller in virtual memory
 */
struct u300_pmx {
	struct device *dev;
	struct pinctrl_dev *pctl;
	void __iomem *virtbase;
};

/**
 * u300_pmx_registers - the array of registers read/written for each pinmux
 * shunt setting
 */
static const u32 u300_pmx_registers[] = {
	U300_SYSCON_PMC1LR,
	U300_SYSCON_PMC1HR,
	U300_SYSCON_PMC2R,
	U300_SYSCON_PMC3R,
	U300_SYSCON_PMC4R,
};

/**
 * struct u300_pin_group - describes a U300 pin group
 * @name: the name of this specific pin group
 * @pins: an array of discrete physical pins used in this group, taken
 *	from the driver-local pin enumeration space
 * @num_pins: the number of pins in this group array, i.e. the number of
 *	elements in .pins so we can iterate over that array
 */
struct u300_pin_group {
	const char *name;
	const unsigned int *pins;
	const unsigned num_pins;
};

/**
 * struct pmx_onmask - mask bits to enable/disable padmux
 * @mask: mask bits to disable
 * @val: mask bits to enable
 *
 * onmask lazy dog:
 * onmask = {
 *   {"PMC1LR" mask, "PMC1LR" value},
 *   {"PMC1HR" mask, "PMC1HR" value},
 *   {"PMC2R"  mask, "PMC2R"  value},
 *   {"PMC3R"  mask, "PMC3R"  value},
 *   {"PMC4R"  mask, "PMC4R"  value}
 * }
 */
struct u300_pmx_mask {
	u16 mask;
	u16 bits;
};

/* The chip power pins are VDD, GND, VDDIO and VSSIO */
static const unsigned power_pins[] = { 0, 1, 3, 31, 46, 47, 49, 50, 61, 62, 63,
	64, 78, 79, 80, 81, 92, 93, 94, 95, 101, 102, 103, 104, 115, 116, 117,
	118, 130, 131, 132, 133, 145, 146, 147, 148, 159, 160, 172, 173, 174,
	175, 187, 188, 189, 190, 201, 202, 211, 212, 213, 214, 215, 218, 223,
	224, 225, 226, 231, 232, 237, 238, 239, 240, 245, 246, 251, 252, 256,
	257, 258, 259, 264, 265, 270, 271, 276, 277, 278, 279, 284, 285, 290,
	291, 295, 296, 299, 300, 301, 302, 303, 309, 310, 311, 312, 319, 320,
	321, 322, 329, 330, 331, 332, 341, 342, 343, 344, 358, 359, 360, 361,
	372, 373, 374, 375, 388, 389, 390, 391, 402, 403, 404, 405, 413, 414,
	415, 416, 427, 428, 429, 430, 443, 444, 455, 456, 457, 458 };
static const unsigned emif0_pins[] = { 355, 356, 357, 362, 363, 364, 365, 366,
	367, 368, 369, 370, 371, 376, 377, 378, 379, 380, 381, 382, 383, 384,
	385, 386, 387, 393, 394, 395, 396, 397, 398, 406, 407, 410, 411, 412,
	417, 418 };
static const unsigned emif1_pins[] = { 216, 217, 219, 220, 221, 222, 227, 228,
	229, 230, 233, 234, 235, 236, 241, 242, 243, 244, 247, 248, 249, 250,
	253, 254, 255, 260, 261, 262, 263, 266, 267, 268, 269, 272, 273, 274,
	275, 280, 281, 282, 283, 286, 287, 288, 289, 292, 293, 294, 297, 298,
	304, 305, 306, 307, 308, 313, 314, 315 };
static const unsigned uart0_pins[] = { 134, 135, 136, 137 };
static const unsigned mmc0_pins[] = { 166, 167, 168, 169, 170, 171, 176, 177 };
static const unsigned spi0_pins[] = { 420, 421, 422, 423, 424, 425 };

static const struct u300_pmx_mask emif0_mask[] = {
	{0, 0},
	{0, 0},
	{0, 0},
	{0, 0},
	{0, 0},
};

static const struct u300_pmx_mask emif1_mask[] = {
	/*
	 * This connects the SDRAM to CS2 and a NAND flash to
	 * CS0 on the EMIF.
	 */
	{
		U300_SYSCON_PMC1LR_EMIF_1_CS2_MASK |
		U300_SYSCON_PMC1LR_EMIF_1_CS1_MASK |
		U300_SYSCON_PMC1LR_EMIF_1_CS0_MASK |
		U300_SYSCON_PMC1LR_EMIF_1_MASK,
		U300_SYSCON_PMC1LR_EMIF_1_CS2_SDRAM |
		U300_SYSCON_PMC1LR_EMIF_1_CS1_STATIC |
		U300_SYSCON_PMC1LR_EMIF_1_CS0_NFIF |
		U300_SYSCON_PMC1LR_EMIF_1_SDRAM0
	},
	{0, 0},
	{0, 0},
	{0, 0},
	{0, 0},
};

static const struct u300_pmx_mask uart0_mask[] = {
	{0, 0},
	{
		U300_SYSCON_PMC1HR_APP_UART0_1_MASK |
		U300_SYSCON_PMC1HR_APP_UART0_2_MASK,
		U300_SYSCON_PMC1HR_APP_UART0_1_UART0 |
		U300_SYSCON_PMC1HR_APP_UART0_2_UART0
	},
	{0, 0},
	{0, 0},
	{0, 0},
};

static const struct u300_pmx_mask mmc0_mask[] = {
	{ U300_SYSCON_PMC1LR_MMCSD_MASK, U300_SYSCON_PMC1LR_MMCSD_MMCSD},
	{0, 0},
	{0, 0},
	{0, 0},
	{ U300_SYSCON_PMC4R_APP_MISC_12_MASK,
	  U300_SYSCON_PMC4R_APP_MISC_12_APP_GPIO }
};

static const struct u300_pmx_mask spi0_mask[] = {
	{0, 0},
	{
		U300_SYSCON_PMC1HR_APP_SPI_2_MASK |
		U300_SYSCON_PMC1HR_APP_SPI_CS_1_MASK |
		U300_SYSCON_PMC1HR_APP_SPI_CS_2_MASK,
		U300_SYSCON_PMC1HR_APP_SPI_2_SPI |
		U300_SYSCON_PMC1HR_APP_SPI_CS_1_SPI |
		U300_SYSCON_PMC1HR_APP_SPI_CS_2_SPI
	},
	{0, 0},
	{0, 0},
	{0, 0}
};

static const struct u300_pin_group u300_pin_groups[] = {
	{
		.name = "powergrp",
		.pins = power_pins,
		.num_pins = ARRAY_SIZE(power_pins),
	},
	{
		.name = "emif0grp",
		.pins = emif0_pins,
		.num_pins = ARRAY_SIZE(emif0_pins),
	},
	{
		.name = "emif1grp",
		.pins = emif1_pins,
		.num_pins = ARRAY_SIZE(emif1_pins),
	},
	{
		.name = "uart0grp",
		.pins = uart0_pins,
		.num_pins = ARRAY_SIZE(uart0_pins),
	},
	{
		.name = "mmc0grp",
		.pins = mmc0_pins,
		.num_pins = ARRAY_SIZE(mmc0_pins),
	},
	{
		.name = "spi0grp",
		.pins = spi0_pins,
		.num_pins = ARRAY_SIZE(spi0_pins),
	},
};

static int u300_get_groups_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(u300_pin_groups);
}

static const char *u300_get_group_name(struct pinctrl_dev *pctldev,
				       unsigned selector)
{
	return u300_pin_groups[selector].name;
}

static int u300_get_group_pins(struct pinctrl_dev *pctldev, unsigned selector,
			       const unsigned **pins,
			       unsigned *num_pins)
{
	*pins = u300_pin_groups[selector].pins;
	*num_pins = u300_pin_groups[selector].num_pins;
	return 0;
}

static void u300_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
		   unsigned offset)
{
	seq_printf(s, " " DRIVER_NAME);
}

static const struct pinctrl_ops u300_pctrl_ops = {
	.get_groups_count = u300_get_groups_count,
	.get_group_name = u300_get_group_name,
	.get_group_pins = u300_get_group_pins,
	.pin_dbg_show = u300_pin_dbg_show,
};

/*
 * Here we define the available functions and their corresponding pin groups
 */

/**
 * struct u300_pmx_func - describes U300 pinmux functions
 * @name: the name of this specific function
 * @groups: corresponding pin groups
 * @onmask: bits to set to enable this when doing pin muxing
 */
struct u300_pmx_func {
	const char *name;
	const char * const *groups;
	const unsigned num_groups;
	const struct u300_pmx_mask *mask;
};

static const char * const powergrps[] = { "powergrp" };
static const char * const emif0grps[] = { "emif0grp" };
static const char * const emif1grps[] = { "emif1grp" };
static const char * const uart0grps[] = { "uart0grp" };
static const char * const mmc0grps[] = { "mmc0grp" };
static const char * const spi0grps[] = { "spi0grp" };

static const struct u300_pmx_func u300_pmx_functions[] = {
	{
		.name = "power",
		.groups = powergrps,
		.num_groups = ARRAY_SIZE(powergrps),
		/* Mask is N/A */
	},
	{
		.name = "emif0",
		.groups = emif0grps,
		.num_groups = ARRAY_SIZE(emif0grps),
		.mask = emif0_mask,
	},
	{
		.name = "emif1",
		.groups = emif1grps,
		.num_groups = ARRAY_SIZE(emif1grps),
		.mask = emif1_mask,
	},
	{
		.name = "uart0",
		.groups = uart0grps,
		.num_groups = ARRAY_SIZE(uart0grps),
		.mask = uart0_mask,
	},
	{
		.name = "mmc0",
		.groups = mmc0grps,
		.num_groups = ARRAY_SIZE(mmc0grps),
		.mask = mmc0_mask,
	},
	{
		.name = "spi0",
		.groups = spi0grps,
		.num_groups = ARRAY_SIZE(spi0grps),
		.mask = spi0_mask,
	},
};

static void u300_pmx_endisable(struct u300_pmx *upmx, unsigned selector,
			       bool enable)
{
	u16 regval, val, mask;
	int i;
	const struct u300_pmx_mask *upmx_mask;

	upmx_mask = u300_pmx_functions[selector].mask;
	for (i = 0; i < ARRAY_SIZE(u300_pmx_registers); i++) {
		if (enable)
			val = upmx_mask->bits;
		else
			val = 0;

		mask = upmx_mask->mask;
		if (mask != 0) {
			regval = readw(upmx->virtbase + u300_pmx_registers[i]);
			regval &= ~mask;
			regval |= val;
			writew(regval, upmx->virtbase + u300_pmx_registers[i]);
		}
		upmx_mask++;
	}
}

static int u300_pmx_set_mux(struct pinctrl_dev *pctldev, unsigned selector,
			    unsigned group)
{
	struct u300_pmx *upmx;

	/* There is nothing to do with the power pins */
	if (selector == 0)
		return 0;

	upmx = pinctrl_dev_get_drvdata(pctldev);
	u300_pmx_endisable(upmx, selector, true);

	return 0;
}

static int u300_pmx_get_funcs_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(u300_pmx_functions);
}

static const char *u300_pmx_get_func_name(struct pinctrl_dev *pctldev,
					  unsigned selector)
{
	return u300_pmx_functions[selector].name;
}

static int u300_pmx_get_groups(struct pinctrl_dev *pctldev, unsigned selector,
			       const char * const **groups,
			       unsigned * const num_groups)
{
	*groups = u300_pmx_functions[selector].groups;
	*num_groups = u300_pmx_functions[selector].num_groups;
	return 0;
}

static const struct pinmux_ops u300_pmx_ops = {
	.get_functions_count = u300_pmx_get_funcs_count,
	.get_function_name = u300_pmx_get_func_name,
	.get_function_groups = u300_pmx_get_groups,
	.set_mux = u300_pmx_set_mux,
};

static int u300_pin_config_get(struct pinctrl_dev *pctldev, unsigned pin,
			       unsigned long *config)
{
	struct pinctrl_gpio_range *range =
		pinctrl_find_gpio_range_from_pin(pctldev, pin);

	/* We get config for those pins we CAN get it for and that's it */
	if (!range)
		return -ENOTSUPP;

	return u300_gpio_config_get(range->gc,
				    (pin - range->pin_base + range->base),
				    config);
}

static int u300_pin_config_set(struct pinctrl_dev *pctldev, unsigned pin,
			       unsigned long *configs, unsigned num_configs)
{
	struct pinctrl_gpio_range *range =
		pinctrl_find_gpio_range_from_pin(pctldev, pin);
	int ret, i;

	if (!range)
		return -EINVAL;

	for (i = 0; i < num_configs; i++) {
		/* Note: none of these configurations take any argument */
		ret = u300_gpio_config_set(range->gc,
			(pin - range->pin_base + range->base),
			pinconf_to_config_param(configs[i]));
		if (ret)
			return ret;
	} /* for each config */

	return 0;
}

static const struct pinconf_ops u300_pconf_ops = {
	.is_generic = true,
	.pin_config_get = u300_pin_config_get,
	.pin_config_set = u300_pin_config_set,
};

static struct pinctrl_desc u300_pmx_desc = {
	.name = DRIVER_NAME,
	.pins = u300_pads,
	.npins = ARRAY_SIZE(u300_pads),
	.pctlops = &u300_pctrl_ops,
	.pmxops = &u300_pmx_ops,
	.confops = &u300_pconf_ops,
	.owner = THIS_MODULE,
};

static int u300_pmx_probe(struct platform_device *pdev)
{
	struct u300_pmx *upmx;

	/* Create state holders etc for this driver */
	upmx = devm_kzalloc(&pdev->dev, sizeof(*upmx), GFP_KERNEL);
	if (!upmx)
		return -ENOMEM;

	upmx->dev = &pdev->dev;

	upmx->virtbase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(upmx->virtbase))
		return PTR_ERR(upmx->virtbase);

	upmx->pctl = devm_pinctrl_register(&pdev->dev, &u300_pmx_desc, upmx);
	if (IS_ERR(upmx->pctl)) {
		dev_err(&pdev->dev, "could not register U300 pinmux driver\n");
		return PTR_ERR(upmx->pctl);
	}

	platform_set_drvdata(pdev, upmx);

	dev_info(&pdev->dev, "initialized U300 pin control driver\n");

	return 0;
}

static const struct of_device_id u300_pinctrl_match[] = {
	{ .compatible = "stericsson,pinctrl-u300" },
	{},
};


static struct platform_driver u300_pmx_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = u300_pinctrl_match,
	},
	.probe = u300_pmx_probe,
};

static int __init u300_pmx_init(void)
{
	return platform_driver_register(&u300_pmx_driver);
}
arch_initcall(u300_pmx_init);

static void __exit u300_pmx_exit(void)
{
	platform_driver_unregister(&u300_pmx_driver);
}
module_exit(u300_pmx_exit);

MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("U300 pin control driver");
MODULE_LICENSE("GPL v2");

/*
 * Copyright (C) 2015 Masahiro Yamada <yamada.masahiro@socionext.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/platform_device.h>

#include "pinctrl-uniphier.h"

#define DRIVER_NAME "ph1-sld8-pinctrl"

static const struct pinctrl_pin_desc ph1_sld8_pins[] = {
	UNIPHIER_PINCTRL_PIN(0, "PCA00", 0,
			     15, UNIPHIER_PIN_DRV_4_8,
			     15, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(1, "PCA01", 0,
			     16, UNIPHIER_PIN_DRV_4_8,
			     16, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(2, "PCA02", 0,
			     17, UNIPHIER_PIN_DRV_4_8,
			     17, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(3, "PCA03", 0,
			     18, UNIPHIER_PIN_DRV_4_8,
			     18, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(4, "PCA04", 0,
			     19, UNIPHIER_PIN_DRV_4_8,
			     19, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(5, "PCA05", 0,
			     20, UNIPHIER_PIN_DRV_4_8,
			     20, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(6, "PCA06", 0,
			     21, UNIPHIER_PIN_DRV_4_8,
			     21, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(7, "PCA07", 0,
			     22, UNIPHIER_PIN_DRV_4_8,
			     22, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(8, "PCA08", 0,
			     23, UNIPHIER_PIN_DRV_4_8,
			     23, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(9, "PCA09", 0,
			     24, UNIPHIER_PIN_DRV_4_8,
			     24, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(10, "PCA10", 0,
			     25, UNIPHIER_PIN_DRV_4_8,
			     25, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(11, "PCA11", 0,
			     26, UNIPHIER_PIN_DRV_4_8,
			     26, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(12, "PCA12", 0,
			     27, UNIPHIER_PIN_DRV_4_8,
			     27, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(13, "PCA13", 0,
			     28, UNIPHIER_PIN_DRV_4_8,
			     28, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(14, "PCA14", 0,
			     29, UNIPHIER_PIN_DRV_4_8,
			     29, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(15, "XNFRE_GB", UNIPHIER_PIN_IECTRL_NONE,
			     30, UNIPHIER_PIN_DRV_4_8,
			     30, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(16, "XNFWE_GB", UNIPHIER_PIN_IECTRL_NONE,
			     31, UNIPHIER_PIN_DRV_4_8,
			     31, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(17, "NFALE_GB", UNIPHIER_PIN_IECTRL_NONE,
			     32, UNIPHIER_PIN_DRV_4_8,
			     32, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(18, "NFCLE_GB", UNIPHIER_PIN_IECTRL_NONE,
			     33, UNIPHIER_PIN_DRV_4_8,
			     33, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(19, "XNFWP_GB", UNIPHIER_PIN_IECTRL_NONE,
			     34, UNIPHIER_PIN_DRV_4_8,
			     34, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(20, "XNFCE0_GB", UNIPHIER_PIN_IECTRL_NONE,
			     35, UNIPHIER_PIN_DRV_4_8,
			     35, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(21, "NANDRYBY0_GB", UNIPHIER_PIN_IECTRL_NONE,
			     36, UNIPHIER_PIN_DRV_4_8,
			     36, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(22, "XNFCE1_GB", UNIPHIER_PIN_IECTRL_NONE,
			     0, UNIPHIER_PIN_DRV_8_12_16_20,
			     119, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(23, "NANDRYBY1_GB", UNIPHIER_PIN_IECTRL_NONE,
			     4, UNIPHIER_PIN_DRV_8_12_16_20,
			     120, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(24, "NFD0_GB", UNIPHIER_PIN_IECTRL_NONE,
			     8, UNIPHIER_PIN_DRV_8_12_16_20,
			     121, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(25, "NFD1_GB", UNIPHIER_PIN_IECTRL_NONE,
			     12, UNIPHIER_PIN_DRV_8_12_16_20,
			     122, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(26, "NFD2_GB", UNIPHIER_PIN_IECTRL_NONE,
			     16, UNIPHIER_PIN_DRV_8_12_16_20,
			     123, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(27, "NFD3_GB", UNIPHIER_PIN_IECTRL_NONE,
			     20, UNIPHIER_PIN_DRV_8_12_16_20,
			     124, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(28, "NFD4_GB", UNIPHIER_PIN_IECTRL_NONE,
			     24, UNIPHIER_PIN_DRV_8_12_16_20,
			     125, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(29, "NFD5_GB", UNIPHIER_PIN_IECTRL_NONE,
			     28, UNIPHIER_PIN_DRV_8_12_16_20,
			     126, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(30, "NFD6_GB", UNIPHIER_PIN_IECTRL_NONE,
			     32, UNIPHIER_PIN_DRV_8_12_16_20,
			     127, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(31, "NFD7_GB", UNIPHIER_PIN_IECTRL_NONE,
			     36, UNIPHIER_PIN_DRV_8_12_16_20,
			     128, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(32, "SDCLK", 8,
			     40, UNIPHIER_PIN_DRV_8_12_16_20,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(33, "SDCMD", 8,
			     44, UNIPHIER_PIN_DRV_8_12_16_20,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(34, "SDDAT0", 8,
			     48, UNIPHIER_PIN_DRV_8_12_16_20,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(35, "SDDAT1", 8,
			     52, UNIPHIER_PIN_DRV_8_12_16_20,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(36, "SDDAT2", 8,
			     56, UNIPHIER_PIN_DRV_8_12_16_20,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(37, "SDDAT3", 8,
			     60, UNIPHIER_PIN_DRV_8_12_16_20,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(38, "SDCD", 8,
			     -1, UNIPHIER_PIN_DRV_FIXED_4,
			     129, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(39, "SDWP", 8,
			     -1, UNIPHIER_PIN_DRV_FIXED_4,
			     130, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(40, "SDVOLC", 9,
			     -1, UNIPHIER_PIN_DRV_FIXED_4,
			     131, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(41, "USB0VBUS", 0,
			     37, UNIPHIER_PIN_DRV_4_8,
			     37, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(42, "USB0OD", 0,
			     38, UNIPHIER_PIN_DRV_4_8,
			     38, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(43, "USB1VBUS", 0,
			     39, UNIPHIER_PIN_DRV_4_8,
			     39, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(44, "USB1OD", 0,
			     40, UNIPHIER_PIN_DRV_4_8,
			     40, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(45, "PCRESET", 0,
			     41, UNIPHIER_PIN_DRV_4_8,
			     41, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(46, "PCREG", 0,
			     42, UNIPHIER_PIN_DRV_4_8,
			     42, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(47, "PCCE2", 0,
			     43, UNIPHIER_PIN_DRV_4_8,
			     43, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(48, "PCVS1", 0,
			     44, UNIPHIER_PIN_DRV_4_8,
			     44, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(49, "PCCD2", 0,
			     45, UNIPHIER_PIN_DRV_4_8,
			     45, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(50, "PCCD1", 0,
			     46, UNIPHIER_PIN_DRV_4_8,
			     46, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(51, "PCREADY", 0,
			     47, UNIPHIER_PIN_DRV_4_8,
			     47, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(52, "PCDOE", 0,
			     48, UNIPHIER_PIN_DRV_4_8,
			     48, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(53, "PCCE1", 0,
			     49, UNIPHIER_PIN_DRV_4_8,
			     49, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(54, "PCWE", 0,
			     50, UNIPHIER_PIN_DRV_4_8,
			     50, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(55, "PCOE", 0,
			     51, UNIPHIER_PIN_DRV_4_8,
			     51, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(56, "PCWAIT", 0,
			     52, UNIPHIER_PIN_DRV_4_8,
			     52, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(57, "PCIOWR", 0,
			     53, UNIPHIER_PIN_DRV_4_8,
			     53, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(58, "PCIORD", 0,
			     54, UNIPHIER_PIN_DRV_4_8,
			     54, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(59, "HS0DIN0", 0,
			     55, UNIPHIER_PIN_DRV_4_8,
			     55, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(60, "HS0DIN1", 0,
			     56, UNIPHIER_PIN_DRV_4_8,
			     56, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(61, "HS0DIN2", 0,
			     57, UNIPHIER_PIN_DRV_4_8,
			     57, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(62, "HS0DIN3", 0,
			     58, UNIPHIER_PIN_DRV_4_8,
			     58, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(63, "HS0DIN4", 0,
			     59, UNIPHIER_PIN_DRV_4_8,
			     59, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(64, "HS0DIN5", 0,
			     60, UNIPHIER_PIN_DRV_4_8,
			     60, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(65, "HS0DIN6", 0,
			     61, UNIPHIER_PIN_DRV_4_8,
			     61, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(66, "HS0DIN7", 0,
			     62, UNIPHIER_PIN_DRV_4_8,
			     62, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(67, "HS0BCLKIN", 0,
			     63, UNIPHIER_PIN_DRV_4_8,
			     63, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(68, "HS0VALIN", 0,
			     64, UNIPHIER_PIN_DRV_4_8,
			     64, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(69, "HS0SYNCIN", 0,
			     65, UNIPHIER_PIN_DRV_4_8,
			     65, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(70, "HSDOUT0", 0,
			     66, UNIPHIER_PIN_DRV_4_8,
			     66, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(71, "HSDOUT1", 0,
			     67, UNIPHIER_PIN_DRV_4_8,
			     67, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(72, "HSDOUT2", 0,
			     68, UNIPHIER_PIN_DRV_4_8,
			     68, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(73, "HSDOUT3", 0,
			     69, UNIPHIER_PIN_DRV_4_8,
			     69, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(74, "HSDOUT4", 0,
			     70, UNIPHIER_PIN_DRV_4_8,
			     70, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(75, "HSDOUT5", 0,
			     71, UNIPHIER_PIN_DRV_4_8,
			     71, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(76, "HSDOUT6", 0,
			     72, UNIPHIER_PIN_DRV_4_8,
			     72, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(77, "HSDOUT7", 0,
			     73, UNIPHIER_PIN_DRV_4_8,
			     73, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(78, "HSBCLKOUT", 0,
			     74, UNIPHIER_PIN_DRV_4_8,
			     74, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(79, "HSVALOUT", 0,
			     75, UNIPHIER_PIN_DRV_4_8,
			     75, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(80, "HSSYNCOUT", 0,
			     76, UNIPHIER_PIN_DRV_4_8,
			     76, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(81, "HS1DIN0", 0,
			     77, UNIPHIER_PIN_DRV_4_8,
			     77, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(82, "HS1DIN1", 0,
			     78, UNIPHIER_PIN_DRV_4_8,
			     78, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(83, "HS1DIN2", 0,
			     79, UNIPHIER_PIN_DRV_4_8,
			     79, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(84, "HS1DIN3", 0,
			     80, UNIPHIER_PIN_DRV_4_8,
			     80, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(85, "HS1DIN4", 0,
			     81, UNIPHIER_PIN_DRV_4_8,
			     81, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(86, "HS1DIN5", 0,
			     82, UNIPHIER_PIN_DRV_4_8,
			     82, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(87, "HS1DIN6", 0,
			     83, UNIPHIER_PIN_DRV_4_8,
			     83, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(88, "HS1DIN7", 0,
			     84, UNIPHIER_PIN_DRV_4_8,
			     84, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(89, "HS1BCLKIN", 0,
			     85, UNIPHIER_PIN_DRV_4_8,
			     85, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(90, "HS1VALIN", 0,
			     86, UNIPHIER_PIN_DRV_4_8,
			     86, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(91, "HS1SYNCIN", 0,
			     87, UNIPHIER_PIN_DRV_4_8,
			     87, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(92, "AGCI", 3,
			     -1, UNIPHIER_PIN_DRV_FIXED_4,
			     132, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(93, "AGCR", 4,
			     -1, UNIPHIER_PIN_DRV_FIXED_4,
			     133, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(94, "AGCBS", 5,
			     -1, UNIPHIER_PIN_DRV_FIXED_4,
			     134, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(95, "IECOUT", 0,
			     88, UNIPHIER_PIN_DRV_4_8,
			     88, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(96, "ASMCK", 0,
			     89, UNIPHIER_PIN_DRV_4_8,
			     89, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(97, "ABCKO", UNIPHIER_PIN_IECTRL_NONE,
			     90, UNIPHIER_PIN_DRV_4_8,
			     90, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(98, "ALRCKO", UNIPHIER_PIN_IECTRL_NONE,
			     91, UNIPHIER_PIN_DRV_4_8,
			     91, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(99, "ASDOUT0", UNIPHIER_PIN_IECTRL_NONE,
			     92, UNIPHIER_PIN_DRV_4_8,
			     92, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(100, "ASDOUT1", UNIPHIER_PIN_IECTRL_NONE,
			     93, UNIPHIER_PIN_DRV_4_8,
			     93, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(101, "ARCOUT", 0,
			     94, UNIPHIER_PIN_DRV_4_8,
			     94, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(102, "SDA0", 10,
			     -1, UNIPHIER_PIN_DRV_FIXED_4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(103, "SCL0", 10,
			     -1, UNIPHIER_PIN_DRV_FIXED_4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(104, "SDA1", 11,
			     -1, UNIPHIER_PIN_DRV_FIXED_4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(105, "SCL1", 11,
			     -1, UNIPHIER_PIN_DRV_FIXED_4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(106, "DMDSDA0", 12,
			     -1, UNIPHIER_PIN_DRV_FIXED_4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(107, "DMDSCL0", 12,
			     -1, UNIPHIER_PIN_DRV_FIXED_4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(108, "DMDSDA1", 13,
			     -1, UNIPHIER_PIN_DRV_FIXED_4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(109, "DMDSCL1", 13,
			     -1, UNIPHIER_PIN_DRV_FIXED_4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(110, "SBO0", UNIPHIER_PIN_IECTRL_NONE,
			     95, UNIPHIER_PIN_DRV_4_8,
			     95, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(111, "SBI0", UNIPHIER_PIN_IECTRL_NONE,
			     96, UNIPHIER_PIN_DRV_4_8,
			     96, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(112, "SBO1", 0,
			     97, UNIPHIER_PIN_DRV_4_8,
			     97, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(113, "SBI1", 0,
			     98, UNIPHIER_PIN_DRV_4_8,
			     98, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(114, "TXD1", 0,
			     99, UNIPHIER_PIN_DRV_4_8,
			     99, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(115, "RXD1", 0,
			     100, UNIPHIER_PIN_DRV_4_8,
			     100, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(116, "HIN", 1,
			     -1, UNIPHIER_PIN_DRV_FIXED_5,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(117, "VIN", 2,
			     -1, UNIPHIER_PIN_DRV_FIXED_5,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(118, "TCON0", 0,
			     101, UNIPHIER_PIN_DRV_4_8,
			     101, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(119, "TCON1", 0,
			     102, UNIPHIER_PIN_DRV_4_8,
			     102, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(120, "TCON2", 0,
			     103, UNIPHIER_PIN_DRV_4_8,
			     103, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(121, "TCON3", 0,
			     104, UNIPHIER_PIN_DRV_4_8,
			     104, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(122, "TCON4", 0,
			     105, UNIPHIER_PIN_DRV_4_8,
			     105, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(123, "TCON5", 0,
			     106, UNIPHIER_PIN_DRV_4_8,
			     106, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(124, "TCON6", 0,
			     107, UNIPHIER_PIN_DRV_4_8,
			     107, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(125, "TCON7", 0,
			     108, UNIPHIER_PIN_DRV_4_8,
			     108, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(126, "TCON8", 0,
			     109, UNIPHIER_PIN_DRV_4_8,
			     109, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(127, "PWMA", 0,
			     110, UNIPHIER_PIN_DRV_4_8,
			     110, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(128, "XIRQ0", 0,
			     111, UNIPHIER_PIN_DRV_4_8,
			     111, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(129, "XIRQ1", 0,
			     112, UNIPHIER_PIN_DRV_4_8,
			     112, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(130, "XIRQ2", 0,
			     113, UNIPHIER_PIN_DRV_4_8,
			     113, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(131, "XIRQ3", 0,
			     114, UNIPHIER_PIN_DRV_4_8,
			     114, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(132, "XIRQ4", 0,
			     115, UNIPHIER_PIN_DRV_4_8,
			     115, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(133, "XIRQ5", 0,
			     116, UNIPHIER_PIN_DRV_4_8,
			     116, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(134, "XIRQ6", 0,
			     117, UNIPHIER_PIN_DRV_4_8,
			     117, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(135, "XIRQ7", 0,
			     118, UNIPHIER_PIN_DRV_4_8,
			     118, UNIPHIER_PIN_PULL_DOWN),
};

static const unsigned emmc_pins[] = {21, 22, 23, 24, 25, 26, 27};
static const unsigned emmc_muxvals[] = {1, 1, 1, 1, 1, 1, 1};
static const unsigned emmc_dat8_pins[] = {28, 29, 30, 31};
static const unsigned emmc_dat8_muxvals[] = {1, 1, 1, 1};
static const unsigned i2c0_pins[] = {102, 103};
static const unsigned i2c0_muxvals[] = {0, 0};
static const unsigned i2c1_pins[] = {104, 105};
static const unsigned i2c1_muxvals[] = {0, 0};
static const unsigned i2c2_pins[] = {108, 109};
static const unsigned i2c2_muxvals[] = {2, 2};
static const unsigned i2c3_pins[] = {108, 109};
static const unsigned i2c3_muxvals[] = {3, 3};
static const unsigned nand_pins[] = {15, 16, 17, 18, 19, 20, 21, 24, 25, 26,
				     27, 28, 29, 30, 31};
static const unsigned nand_muxvals[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
					0, 0};
static const unsigned nand_cs1_pins[] = {22, 23};
static const unsigned nand_cs1_muxvals[] = {0, 0};
static const unsigned sd_pins[] = {32, 33, 34, 35, 36, 37, 38, 39, 40};
static const unsigned sd_muxvals[] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
static const unsigned uart0_pins[] = {70, 71};
static const unsigned uart0_muxvals[] = {3, 3};
static const unsigned uart1_pins[] = {114, 115};
static const unsigned uart1_muxvals[] = {0, 0};
static const unsigned uart2_pins[] = {112, 113};
static const unsigned uart2_muxvals[] = {1, 1};
static const unsigned uart3_pins[] = {110, 111};
static const unsigned uart3_muxvals[] = {1, 1};
static const unsigned usb0_pins[] = {41, 42};
static const unsigned usb0_muxvals[] = {0, 0};
static const unsigned usb1_pins[] = {43, 44};
static const unsigned usb1_muxvals[] = {0, 0};
static const unsigned usb2_pins[] = {114, 115};
static const unsigned usb2_muxvals[] = {1, 1};
static const unsigned port_range0_pins[] = {
	0, 1, 2, 3, 4, 5, 6, 7,				/* PORT0x */
	8, 9, 10, 11, 12, 13, 14, 15,			/* PORT1x */
	32, 33, 34, 35, 36, 37, 38, 39,			/* PORT2x */
	59, 60, 61, 62, 63, 64, 65, 66,			/* PORT3x */
	95, 96, 97, 98, 99, 100, 101, 57,		/* PORT4x */
	70, 71, 72, 73, 74, 75, 76, 77,			/* PORT5x */
	81, 83, 84, 85, 86, 89, 90, 91,			/* PORT6x */
	118, 119, 120, 121, 122, 53, 54, 55,		/* PORT7x */
	41, 42, 43, 44, 79, 80, 18, 19,			/* PORT8x */
	110, 111, 112, 113, 114, 115, 16, 17,		/* PORT9x */
	40, 67, 68, 69, 78, 92, 93, 94,			/* PORT10x */
	48, 49, 46, 45, 123, 124, 125, 126,		/* PORT11x */
	47, 127, 20, 56, 22,				/* PORT120-124 */
};
static const unsigned port_range0_muxvals[] = {
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT0x */
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT1x */
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT2x */
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT3x */
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT4x */
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT5x */
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT6x */
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT7x */
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT8x */
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT9x */
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT10x */
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT11x */
	15, 15, 15, 15, 15,				/* PORT120-124 */
};
static const unsigned port_range1_pins[] = {
	116, 117,					/* PORT130-131 */
};
static const unsigned port_range1_muxvals[] = {
	15, 15,						/* PORT130-131 */
};
static const unsigned port_range2_pins[] = {
	102, 103, 104, 105, 106, 107, 108, 109,		/* PORT14x */
};
static const unsigned port_range2_muxvals[] = {
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT14x */
};
static const unsigned port_range3_pins[] = {
	23,						/* PORT166 */
};
static const unsigned port_range3_muxvals[] = {
	15,						/* PORT166 */
};
static const unsigned xirq_range0_pins[] = {
	128, 129, 130, 131, 132, 133, 134, 135,		/* XIRQ0-7 */
	82, 87, 88, 50, 51,				/* XIRQ8-12 */
};
static const unsigned xirq_range0_muxvals[] = {
	0, 0, 0, 0, 0, 0, 0, 0,				/* XIRQ0-7 */
	14, 14, 14, 14, 14,				/* XIRQ8-12 */
};
static const unsigned xirq_range1_pins[] = {
	52, 58,						/* XIRQ14-15 */
};
static const unsigned xirq_range1_muxvals[] = {
	14, 14,						/* XIRQ14-15 */
};

static const struct uniphier_pinctrl_group ph1_sld8_groups[] = {
	UNIPHIER_PINCTRL_GROUP(emmc),
	UNIPHIER_PINCTRL_GROUP(emmc_dat8),
	UNIPHIER_PINCTRL_GROUP(i2c0),
	UNIPHIER_PINCTRL_GROUP(i2c1),
	UNIPHIER_PINCTRL_GROUP(i2c2),
	UNIPHIER_PINCTRL_GROUP(i2c3),
	UNIPHIER_PINCTRL_GROUP(nand),
	UNIPHIER_PINCTRL_GROUP(nand_cs1),
	UNIPHIER_PINCTRL_GROUP(sd),
	UNIPHIER_PINCTRL_GROUP(uart0),
	UNIPHIER_PINCTRL_GROUP(uart1),
	UNIPHIER_PINCTRL_GROUP(uart2),
	UNIPHIER_PINCTRL_GROUP(uart3),
	UNIPHIER_PINCTRL_GROUP(usb0),
	UNIPHIER_PINCTRL_GROUP(usb1),
	UNIPHIER_PINCTRL_GROUP(usb2),
	UNIPHIER_PINCTRL_GROUP_GPIO_RANGE_PORT(port_range0),
	UNIPHIER_PINCTRL_GROUP_GPIO_RANGE_PORT(port_range1),
	UNIPHIER_PINCTRL_GROUP_GPIO_RANGE_PORT(port_range2),
	UNIPHIER_PINCTRL_GROUP_GPIO_RANGE_PORT(port_range3),
	UNIPHIER_PINCTRL_GROUP_GPIO_RANGE_IRQ(xirq_range0),
	UNIPHIER_PINCTRL_GROUP_GPIO_RANGE_IRQ(xirq_range1),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port00, port_range0, 0),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port01, port_range0, 1),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port02, port_range0, 2),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port03, port_range0, 3),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port04, port_range0, 4),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port05, port_range0, 5),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port06, port_range0, 6),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port07, port_range0, 7),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port10, port_range0, 8),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port11, port_range0, 9),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port12, port_range0, 10),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port13, port_range0, 11),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port14, port_range0, 12),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port15, port_range0, 13),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port16, port_range0, 14),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port17, port_range0, 15),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port20, port_range0, 16),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port21, port_range0, 17),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port22, port_range0, 18),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port23, port_range0, 19),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port24, port_range0, 20),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port25, port_range0, 21),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port26, port_range0, 22),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port27, port_range0, 23),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port30, port_range0, 24),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port31, port_range0, 25),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port32, port_range0, 26),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port33, port_range0, 27),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port34, port_range0, 28),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port35, port_range0, 29),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port36, port_range0, 30),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port37, port_range0, 31),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port40, port_range0, 32),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port41, port_range0, 33),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port42, port_range0, 34),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port43, port_range0, 35),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port44, port_range0, 36),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port45, port_range0, 37),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port46, port_range0, 38),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port47, port_range0, 39),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port50, port_range0, 40),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port51, port_range0, 41),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port52, port_range0, 42),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port53, port_range0, 43),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port54, port_range0, 44),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port55, port_range0, 45),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port56, port_range0, 46),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port57, port_range0, 47),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port60, port_range0, 48),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port61, port_range0, 49),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port62, port_range0, 50),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port63, port_range0, 51),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port64, port_range0, 52),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port65, port_range0, 53),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port66, port_range0, 54),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port67, port_range0, 55),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port70, port_range0, 56),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port71, port_range0, 57),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port72, port_range0, 58),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port73, port_range0, 59),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port74, port_range0, 60),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port75, port_range0, 61),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port76, port_range0, 62),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port77, port_range0, 63),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port80, port_range0, 64),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port81, port_range0, 65),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port82, port_range0, 66),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port83, port_range0, 67),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port84, port_range0, 68),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port85, port_range0, 69),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port86, port_range0, 70),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port87, port_range0, 71),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port90, port_range0, 72),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port91, port_range0, 73),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port92, port_range0, 74),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port93, port_range0, 75),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port94, port_range0, 76),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port95, port_range0, 77),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port96, port_range0, 78),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port97, port_range0, 79),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port100, port_range0, 80),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port101, port_range0, 81),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port102, port_range0, 82),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port103, port_range0, 83),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port104, port_range0, 84),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port105, port_range0, 85),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port106, port_range0, 86),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port107, port_range0, 87),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port110, port_range0, 88),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port111, port_range0, 89),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port112, port_range0, 90),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port113, port_range0, 91),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port114, port_range0, 92),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port115, port_range0, 93),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port116, port_range0, 94),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port117, port_range0, 95),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port120, port_range0, 96),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port121, port_range0, 97),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port122, port_range0, 98),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port123, port_range0, 99),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port124, port_range0, 100),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port130, port_range1, 0),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port131, port_range1, 1),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port140, port_range2, 0),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port141, port_range2, 1),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port142, port_range2, 2),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port143, port_range2, 3),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port144, port_range2, 4),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port145, port_range2, 5),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port146, port_range2, 6),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port147, port_range2, 7),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port166, port_range3, 0),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq0, xirq_range0, 0),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq1, xirq_range0, 1),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq2, xirq_range0, 2),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq3, xirq_range0, 3),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq4, xirq_range0, 4),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq5, xirq_range0, 5),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq6, xirq_range0, 6),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq7, xirq_range0, 7),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq8, xirq_range0, 8),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq9, xirq_range0, 9),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq10, xirq_range0, 10),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq11, xirq_range0, 11),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq12, xirq_range0, 12),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq14, xirq_range1, 0),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq15, xirq_range1, 1),
};

static const char * const emmc_groups[] = {"emmc", "emmc_dat8"};
static const char * const i2c0_groups[] = {"i2c0"};
static const char * const i2c1_groups[] = {"i2c1"};
static const char * const i2c2_groups[] = {"i2c2"};
static const char * const i2c3_groups[] = {"i2c3"};
static const char * const nand_groups[] = {"nand", "nand_cs1"};
static const char * const sd_groups[] = {"sd"};
static const char * const uart0_groups[] = {"uart0"};
static const char * const uart1_groups[] = {"uart1"};
static const char * const uart2_groups[] = {"uart2"};
static const char * const uart3_groups[] = {"uart3"};
static const char * const usb0_groups[] = {"usb0"};
static const char * const usb1_groups[] = {"usb1"};
static const char * const usb2_groups[] = {"usb2"};
static const char * const port_groups[] = {
	"port00",  "port01",  "port02",  "port03",
	"port04",  "port05",  "port06",  "port07",
	"port10",  "port11",  "port12",  "port13",
	"port14",  "port15",  "port16",  "port17",
	"port20",  "port21",  "port22",  "port23",
	"port24",  "port25",  "port26",  "port27",
	"port30",  "port31",  "port32",  "port33",
	"port34",  "port35",  "port36",  "port37",
	"port40",  "port41",  "port42",  "port43",
	"port44",  "port45",  "port46",  "port47",
	"port50",  "port51",  "port52",  "port53",
	"port54",  "port55",  "port56",  "port57",
	"port60",  "port61",  "port62",  "port63",
	"port64",  "port65",  "port66",  "port67",
	"port70",  "port71",  "port72",  "port73",
	"port74",  "port75",  "port76",  "port77",
	"port80",  "port81",  "port82",  "port83",
	"port84",  "port85",  "port86",  "port87",
	"port90",  "port91",  "port92",  "port93",
	"port94",  "port95",  "port96",  "port97",
	"port100", "port101", "port102", "port103",
	"port104", "port105", "port106", "port107",
	"port110", "port111", "port112", "port113",
	"port114", "port115", "port116", "port117",
	"port120", "port121", "port122", "port123",
	"port124", "port125", "port126", "port127",
	"port130", "port131", "port132", "port133",
	"port134", "port135", "port136", "port137",
	"port140", "port141", "port142", "port143",
	"port144", "port145", "port146", "port147",
	/* port150-164 missing */
	/* none */ "port165",
};
static const char * const xirq_groups[] = {
	"xirq0",  "xirq1",  "xirq2",  "xirq3",
	"xirq4",  "xirq5",  "xirq6",  "xirq7",
	"xirq8",  "xirq9",  "xirq10", "xirq11",
	"xirq12", /* none*/ "xirq14", "xirq15",
};

static const struct uniphier_pinmux_function ph1_sld8_functions[] = {
	UNIPHIER_PINMUX_FUNCTION(emmc),
	UNIPHIER_PINMUX_FUNCTION(i2c0),
	UNIPHIER_PINMUX_FUNCTION(i2c1),
	UNIPHIER_PINMUX_FUNCTION(i2c2),
	UNIPHIER_PINMUX_FUNCTION(i2c3),
	UNIPHIER_PINMUX_FUNCTION(nand),
	UNIPHIER_PINMUX_FUNCTION(sd),
	UNIPHIER_PINMUX_FUNCTION(uart0),
	UNIPHIER_PINMUX_FUNCTION(uart1),
	UNIPHIER_PINMUX_FUNCTION(uart2),
	UNIPHIER_PINMUX_FUNCTION(uart3),
	UNIPHIER_PINMUX_FUNCTION(usb0),
	UNIPHIER_PINMUX_FUNCTION(usb1),
	UNIPHIER_PINMUX_FUNCTION(usb2),
	UNIPHIER_PINMUX_FUNCTION(port),
	UNIPHIER_PINMUX_FUNCTION(xirq),
};

static struct uniphier_pinctrl_socdata ph1_sld8_pindata = {
	.groups = ph1_sld8_groups,
	.groups_count = ARRAY_SIZE(ph1_sld8_groups),
	.functions = ph1_sld8_functions,
	.functions_count = ARRAY_SIZE(ph1_sld8_functions),
	.mux_bits = 8,
	.reg_stride = 4,
	.load_pinctrl = false,
};

static struct pinctrl_desc ph1_sld8_pinctrl_desc = {
	.name = DRIVER_NAME,
	.pins = ph1_sld8_pins,
	.npins = ARRAY_SIZE(ph1_sld8_pins),
	.owner = THIS_MODULE,
};

static int ph1_sld8_pinctrl_probe(struct platform_device *pdev)
{
	return uniphier_pinctrl_probe(pdev, &ph1_sld8_pinctrl_desc,
				      &ph1_sld8_pindata);
}

static const struct of_device_id ph1_sld8_pinctrl_match[] = {
	{ .compatible = "socionext,ph1-sld8-pinctrl" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ph1_sld8_pinctrl_match);

static struct platform_driver ph1_sld8_pinctrl_driver = {
	.probe = ph1_sld8_pinctrl_probe,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = ph1_sld8_pinctrl_match,
	},
};
module_platform_driver(ph1_sld8_pinctrl_driver);

MODULE_AUTHOR("Masahiro Yamada <yamada.masahiro@socionext.com>");
MODULE_DESCRIPTION("UniPhier PH1-sLD8 pinctrl driver");
MODULE_LICENSE("GPL");

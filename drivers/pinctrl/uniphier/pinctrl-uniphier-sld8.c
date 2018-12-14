// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (C) 2015-2017 Socionext Inc.
//   Author: Masahiro Yamada <yamada.masahiro@socionext.com>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mod_devicetable.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/platform_device.h>

#include "pinctrl-uniphier.h"

static const struct pinctrl_pin_desc uniphier_sld8_pins[] = {
	UNIPHIER_PINCTRL_PIN(0, "PCA00", 0,
			     15, UNIPHIER_PIN_DRV_1BIT,
			     15, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(1, "PCA01", 0,
			     16, UNIPHIER_PIN_DRV_1BIT,
			     16, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(2, "PCA02", 0,
			     17, UNIPHIER_PIN_DRV_1BIT,
			     17, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(3, "PCA03", 0,
			     18, UNIPHIER_PIN_DRV_1BIT,
			     18, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(4, "PCA04", 0,
			     19, UNIPHIER_PIN_DRV_1BIT,
			     19, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(5, "PCA05", 0,
			     20, UNIPHIER_PIN_DRV_1BIT,
			     20, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(6, "PCA06", 0,
			     21, UNIPHIER_PIN_DRV_1BIT,
			     21, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(7, "PCA07", 0,
			     22, UNIPHIER_PIN_DRV_1BIT,
			     22, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(8, "PCA08", 0,
			     23, UNIPHIER_PIN_DRV_1BIT,
			     23, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(9, "PCA09", 0,
			     24, UNIPHIER_PIN_DRV_1BIT,
			     24, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(10, "PCA10", 0,
			     25, UNIPHIER_PIN_DRV_1BIT,
			     25, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(11, "PCA11", 0,
			     26, UNIPHIER_PIN_DRV_1BIT,
			     26, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(12, "PCA12", 0,
			     27, UNIPHIER_PIN_DRV_1BIT,
			     27, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(13, "PCA13", 0,
			     28, UNIPHIER_PIN_DRV_1BIT,
			     28, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(14, "PCA14", 0,
			     29, UNIPHIER_PIN_DRV_1BIT,
			     29, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(15, "XNFRE_GB", UNIPHIER_PIN_IECTRL_NONE,
			     30, UNIPHIER_PIN_DRV_1BIT,
			     30, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(16, "XNFWE_GB", UNIPHIER_PIN_IECTRL_NONE,
			     31, UNIPHIER_PIN_DRV_1BIT,
			     31, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(17, "NFALE_GB", UNIPHIER_PIN_IECTRL_NONE,
			     32, UNIPHIER_PIN_DRV_1BIT,
			     32, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(18, "NFCLE_GB", UNIPHIER_PIN_IECTRL_NONE,
			     33, UNIPHIER_PIN_DRV_1BIT,
			     33, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(19, "XNFWP_GB", UNIPHIER_PIN_IECTRL_NONE,
			     34, UNIPHIER_PIN_DRV_1BIT,
			     34, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(20, "XNFCE0_GB", UNIPHIER_PIN_IECTRL_NONE,
			     35, UNIPHIER_PIN_DRV_1BIT,
			     35, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(21, "NANDRYBY0_GB", UNIPHIER_PIN_IECTRL_NONE,
			     36, UNIPHIER_PIN_DRV_1BIT,
			     36, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(22, "XNFCE1_GB", UNIPHIER_PIN_IECTRL_NONE,
			     0, UNIPHIER_PIN_DRV_2BIT,
			     119, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(23, "NANDRYBY1_GB", UNIPHIER_PIN_IECTRL_NONE,
			     1, UNIPHIER_PIN_DRV_2BIT,
			     120, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(24, "NFD0_GB", UNIPHIER_PIN_IECTRL_NONE,
			     2, UNIPHIER_PIN_DRV_2BIT,
			     121, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(25, "NFD1_GB", UNIPHIER_PIN_IECTRL_NONE,
			     3, UNIPHIER_PIN_DRV_2BIT,
			     122, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(26, "NFD2_GB", UNIPHIER_PIN_IECTRL_NONE,
			     4, UNIPHIER_PIN_DRV_2BIT,
			     123, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(27, "NFD3_GB", UNIPHIER_PIN_IECTRL_NONE,
			     5, UNIPHIER_PIN_DRV_2BIT,
			     124, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(28, "NFD4_GB", UNIPHIER_PIN_IECTRL_NONE,
			     6, UNIPHIER_PIN_DRV_2BIT,
			     125, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(29, "NFD5_GB", UNIPHIER_PIN_IECTRL_NONE,
			     7, UNIPHIER_PIN_DRV_2BIT,
			     126, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(30, "NFD6_GB", UNIPHIER_PIN_IECTRL_NONE,
			     8, UNIPHIER_PIN_DRV_2BIT,
			     127, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(31, "NFD7_GB", UNIPHIER_PIN_IECTRL_NONE,
			     9, UNIPHIER_PIN_DRV_2BIT,
			     128, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(32, "SDCLK", 8,
			     10, UNIPHIER_PIN_DRV_2BIT,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(33, "SDCMD", 8,
			     11, UNIPHIER_PIN_DRV_2BIT,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(34, "SDDAT0", 8,
			     12, UNIPHIER_PIN_DRV_2BIT,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(35, "SDDAT1", 8,
			     13, UNIPHIER_PIN_DRV_2BIT,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(36, "SDDAT2", 8,
			     14, UNIPHIER_PIN_DRV_2BIT,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(37, "SDDAT3", 8,
			     15, UNIPHIER_PIN_DRV_2BIT,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(38, "SDCD", 8,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     129, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(39, "SDWP", 8,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     130, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(40, "SDVOLC", 9,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     131, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(41, "USB0VBUS", 0,
			     37, UNIPHIER_PIN_DRV_1BIT,
			     37, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(42, "USB0OD", 0,
			     38, UNIPHIER_PIN_DRV_1BIT,
			     38, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(43, "USB1VBUS", 0,
			     39, UNIPHIER_PIN_DRV_1BIT,
			     39, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(44, "USB1OD", 0,
			     40, UNIPHIER_PIN_DRV_1BIT,
			     40, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(45, "PCRESET", 0,
			     41, UNIPHIER_PIN_DRV_1BIT,
			     41, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(46, "PCREG", 0,
			     42, UNIPHIER_PIN_DRV_1BIT,
			     42, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(47, "PCCE2", 0,
			     43, UNIPHIER_PIN_DRV_1BIT,
			     43, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(48, "PCVS1", 0,
			     44, UNIPHIER_PIN_DRV_1BIT,
			     44, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(49, "PCCD2", 0,
			     45, UNIPHIER_PIN_DRV_1BIT,
			     45, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(50, "PCCD1", 0,
			     46, UNIPHIER_PIN_DRV_1BIT,
			     46, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(51, "PCREADY", 0,
			     47, UNIPHIER_PIN_DRV_1BIT,
			     47, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(52, "PCDOE", 0,
			     48, UNIPHIER_PIN_DRV_1BIT,
			     48, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(53, "PCCE1", 0,
			     49, UNIPHIER_PIN_DRV_1BIT,
			     49, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(54, "PCWE", 0,
			     50, UNIPHIER_PIN_DRV_1BIT,
			     50, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(55, "PCOE", 0,
			     51, UNIPHIER_PIN_DRV_1BIT,
			     51, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(56, "PCWAIT", 0,
			     52, UNIPHIER_PIN_DRV_1BIT,
			     52, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(57, "PCIOWR", 0,
			     53, UNIPHIER_PIN_DRV_1BIT,
			     53, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(58, "PCIORD", 0,
			     54, UNIPHIER_PIN_DRV_1BIT,
			     54, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(59, "HS0DIN0", 0,
			     55, UNIPHIER_PIN_DRV_1BIT,
			     55, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(60, "HS0DIN1", 0,
			     56, UNIPHIER_PIN_DRV_1BIT,
			     56, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(61, "HS0DIN2", 0,
			     57, UNIPHIER_PIN_DRV_1BIT,
			     57, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(62, "HS0DIN3", 0,
			     58, UNIPHIER_PIN_DRV_1BIT,
			     58, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(63, "HS0DIN4", 0,
			     59, UNIPHIER_PIN_DRV_1BIT,
			     59, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(64, "HS0DIN5", 0,
			     60, UNIPHIER_PIN_DRV_1BIT,
			     60, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(65, "HS0DIN6", 0,
			     61, UNIPHIER_PIN_DRV_1BIT,
			     61, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(66, "HS0DIN7", 0,
			     62, UNIPHIER_PIN_DRV_1BIT,
			     62, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(67, "HS0BCLKIN", 0,
			     63, UNIPHIER_PIN_DRV_1BIT,
			     63, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(68, "HS0VALIN", 0,
			     64, UNIPHIER_PIN_DRV_1BIT,
			     64, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(69, "HS0SYNCIN", 0,
			     65, UNIPHIER_PIN_DRV_1BIT,
			     65, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(70, "HSDOUT0", 0,
			     66, UNIPHIER_PIN_DRV_1BIT,
			     66, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(71, "HSDOUT1", 0,
			     67, UNIPHIER_PIN_DRV_1BIT,
			     67, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(72, "HSDOUT2", 0,
			     68, UNIPHIER_PIN_DRV_1BIT,
			     68, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(73, "HSDOUT3", 0,
			     69, UNIPHIER_PIN_DRV_1BIT,
			     69, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(74, "HSDOUT4", 0,
			     70, UNIPHIER_PIN_DRV_1BIT,
			     70, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(75, "HSDOUT5", 0,
			     71, UNIPHIER_PIN_DRV_1BIT,
			     71, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(76, "HSDOUT6", 0,
			     72, UNIPHIER_PIN_DRV_1BIT,
			     72, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(77, "HSDOUT7", 0,
			     73, UNIPHIER_PIN_DRV_1BIT,
			     73, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(78, "HSBCLKOUT", 0,
			     74, UNIPHIER_PIN_DRV_1BIT,
			     74, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(79, "HSVALOUT", 0,
			     75, UNIPHIER_PIN_DRV_1BIT,
			     75, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(80, "HSSYNCOUT", 0,
			     76, UNIPHIER_PIN_DRV_1BIT,
			     76, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(81, "HS1DIN0", 0,
			     77, UNIPHIER_PIN_DRV_1BIT,
			     77, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(82, "HS1DIN1", 0,
			     78, UNIPHIER_PIN_DRV_1BIT,
			     78, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(83, "HS1DIN2", 0,
			     79, UNIPHIER_PIN_DRV_1BIT,
			     79, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(84, "HS1DIN3", 0,
			     80, UNIPHIER_PIN_DRV_1BIT,
			     80, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(85, "HS1DIN4", 0,
			     81, UNIPHIER_PIN_DRV_1BIT,
			     81, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(86, "HS1DIN5", 0,
			     82, UNIPHIER_PIN_DRV_1BIT,
			     82, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(87, "HS1DIN6", 0,
			     83, UNIPHIER_PIN_DRV_1BIT,
			     83, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(88, "HS1DIN7", 0,
			     84, UNIPHIER_PIN_DRV_1BIT,
			     84, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(89, "HS1BCLKIN", 0,
			     85, UNIPHIER_PIN_DRV_1BIT,
			     85, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(90, "HS1VALIN", 0,
			     86, UNIPHIER_PIN_DRV_1BIT,
			     86, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(91, "HS1SYNCIN", 0,
			     87, UNIPHIER_PIN_DRV_1BIT,
			     87, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(92, "AGCI", 3,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     132, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(93, "AGCR", 4,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     133, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(94, "AGCBS", 5,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     134, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(95, "IECOUT", 0,
			     88, UNIPHIER_PIN_DRV_1BIT,
			     88, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(96, "ASMCK", 0,
			     89, UNIPHIER_PIN_DRV_1BIT,
			     89, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(97, "ABCKO", UNIPHIER_PIN_IECTRL_NONE,
			     90, UNIPHIER_PIN_DRV_1BIT,
			     90, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(98, "ALRCKO", UNIPHIER_PIN_IECTRL_NONE,
			     91, UNIPHIER_PIN_DRV_1BIT,
			     91, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(99, "ASDOUT0", UNIPHIER_PIN_IECTRL_NONE,
			     92, UNIPHIER_PIN_DRV_1BIT,
			     92, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(100, "ASDOUT1", UNIPHIER_PIN_IECTRL_NONE,
			     93, UNIPHIER_PIN_DRV_1BIT,
			     93, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(101, "ARCOUT", 0,
			     94, UNIPHIER_PIN_DRV_1BIT,
			     94, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(102, "SDA0", 10,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(103, "SCL0", 10,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(104, "SDA1", 11,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(105, "SCL1", 11,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(106, "DMDSDA0", 12,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(107, "DMDSCL0", 12,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(108, "DMDSDA1", 13,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(109, "DMDSCL1", 13,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(110, "SBO0", UNIPHIER_PIN_IECTRL_NONE,
			     95, UNIPHIER_PIN_DRV_1BIT,
			     95, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(111, "SBI0", UNIPHIER_PIN_IECTRL_NONE,
			     96, UNIPHIER_PIN_DRV_1BIT,
			     96, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(112, "SBO1", 0,
			     97, UNIPHIER_PIN_DRV_1BIT,
			     97, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(113, "SBI1", 0,
			     98, UNIPHIER_PIN_DRV_1BIT,
			     98, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(114, "TXD1", 0,
			     99, UNIPHIER_PIN_DRV_1BIT,
			     99, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(115, "RXD1", 0,
			     100, UNIPHIER_PIN_DRV_1BIT,
			     100, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(116, "HIN", 1,
			     -1, UNIPHIER_PIN_DRV_FIXED5,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(117, "VIN", 2,
			     -1, UNIPHIER_PIN_DRV_FIXED5,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(118, "TCON0", 0,
			     101, UNIPHIER_PIN_DRV_1BIT,
			     101, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(119, "TCON1", 0,
			     102, UNIPHIER_PIN_DRV_1BIT,
			     102, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(120, "TCON2", 0,
			     103, UNIPHIER_PIN_DRV_1BIT,
			     103, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(121, "TCON3", 0,
			     104, UNIPHIER_PIN_DRV_1BIT,
			     104, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(122, "TCON4", 0,
			     105, UNIPHIER_PIN_DRV_1BIT,
			     105, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(123, "TCON5", 0,
			     106, UNIPHIER_PIN_DRV_1BIT,
			     106, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(124, "TCON6", 0,
			     107, UNIPHIER_PIN_DRV_1BIT,
			     107, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(125, "TCON7", 0,
			     108, UNIPHIER_PIN_DRV_1BIT,
			     108, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(126, "TCON8", 0,
			     109, UNIPHIER_PIN_DRV_1BIT,
			     109, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(127, "PWMA", 0,
			     110, UNIPHIER_PIN_DRV_1BIT,
			     110, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(128, "XIRQ0", 0,
			     111, UNIPHIER_PIN_DRV_1BIT,
			     111, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(129, "XIRQ1", 0,
			     112, UNIPHIER_PIN_DRV_1BIT,
			     112, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(130, "XIRQ2", 0,
			     113, UNIPHIER_PIN_DRV_1BIT,
			     113, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(131, "XIRQ3", 0,
			     114, UNIPHIER_PIN_DRV_1BIT,
			     114, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(132, "XIRQ4", 0,
			     115, UNIPHIER_PIN_DRV_1BIT,
			     115, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(133, "XIRQ5", 0,
			     116, UNIPHIER_PIN_DRV_1BIT,
			     116, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(134, "XIRQ6", 0,
			     117, UNIPHIER_PIN_DRV_1BIT,
			     117, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(135, "XIRQ7", 0,
			     118, UNIPHIER_PIN_DRV_1BIT,
			     118, UNIPHIER_PIN_PULL_DOWN),
	/* dedicated pins */
	UNIPHIER_PINCTRL_PIN(136, "ED0", -1,
			     0, UNIPHIER_PIN_DRV_1BIT,
			     0, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(137, "ED1", -1,
			     1, UNIPHIER_PIN_DRV_1BIT,
			     1, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(138, "ED2", -1,
			     2, UNIPHIER_PIN_DRV_1BIT,
			     2, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(139, "ED3", -1,
			     3, UNIPHIER_PIN_DRV_1BIT,
			     3, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(140, "ED4", -1,
			     4, UNIPHIER_PIN_DRV_1BIT,
			     4, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(141, "ED5", -1,
			     5, UNIPHIER_PIN_DRV_1BIT,
			     5, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(142, "ED6", -1,
			     6, UNIPHIER_PIN_DRV_1BIT,
			     6, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(143, "ED7", -1,
			     7, UNIPHIER_PIN_DRV_1BIT,
			     7, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(144, "XERWE0", -1,
			     8, UNIPHIER_PIN_DRV_1BIT,
			     8, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(145, "XERWE1", -1,
			     9, UNIPHIER_PIN_DRV_1BIT,
			     9, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(146, "ERXW", -1,
			     10, UNIPHIER_PIN_DRV_1BIT,
			     10, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(147, "ES0", -1,
			     11, UNIPHIER_PIN_DRV_1BIT,
			     11, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(148, "ES1", -1,
			     12, UNIPHIER_PIN_DRV_1BIT,
			     12, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(149, "ES2", -1,
			     13, UNIPHIER_PIN_DRV_1BIT,
			     13, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(150, "XECS1", -1,
			     14, UNIPHIER_PIN_DRV_1BIT,
			     14, UNIPHIER_PIN_PULL_DOWN),
};

static const unsigned emmc_pins[] = {21, 22, 23, 24, 25, 26, 27};
static const int emmc_muxvals[] = {1, 1, 1, 1, 1, 1, 1};
static const unsigned emmc_dat8_pins[] = {28, 29, 30, 31};
static const int emmc_dat8_muxvals[] = {1, 1, 1, 1};
static const unsigned ether_mii_pins[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 13, 14,
					  61, 63, 64, 65, 66, 67, 68};
static const int ether_mii_muxvals[] = {13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
					13, 13, 27, 27, 27, 27, 27, 27, 27};
static const unsigned ether_rmii_pins[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 13,
					   14};
static const int ether_rmii_muxvals[] = {13, 13, 13, 13, 13, 13, 13, 13, 13,
					 13, 13, 13};
static const unsigned i2c0_pins[] = {102, 103};
static const int i2c0_muxvals[] = {0, 0};
static const unsigned i2c1_pins[] = {104, 105};
static const int i2c1_muxvals[] = {0, 0};
static const unsigned i2c2_pins[] = {108, 109};
static const int i2c2_muxvals[] = {2, 2};
static const unsigned i2c3_pins[] = {108, 109};
static const int i2c3_muxvals[] = {3, 3};
static const unsigned nand_pins[] = {15, 16, 17, 18, 19, 20, 21, 24, 25, 26,
				     27, 28, 29, 30, 31};
static const int nand_muxvals[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const unsigned nand_cs1_pins[] = {22, 23};
static const int nand_cs1_muxvals[] = {0, 0};
static const unsigned sd_pins[] = {32, 33, 34, 35, 36, 37, 38, 39, 40};
static const int sd_muxvals[] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
static const unsigned spi0_pins[] = {118, 119, 120, 121};
static const int spi0_muxvals[] = {3, 3, 3, 3};
static const unsigned system_bus_pins[] = {136, 137, 138, 139, 140, 141, 142,
					   143, 144, 145, 146, 147, 148, 149};
static const int system_bus_muxvals[] = {-1, -1, -1, -1, -1, -1, -1, -1, -1,
					 -1, -1, -1, -1, -1};
static const unsigned system_bus_cs1_pins[] = {150};
static const int system_bus_cs1_muxvals[] = {-1};
static const unsigned system_bus_cs2_pins[] = {10};
static const int system_bus_cs2_muxvals[] = {1};
static const unsigned system_bus_cs3_pins[] = {11};
static const int system_bus_cs3_muxvals[] = {1};
static const unsigned system_bus_cs4_pins[] = {12};
static const int system_bus_cs4_muxvals[] = {1};
static const unsigned system_bus_cs5_pins[] = {13};
static const int system_bus_cs5_muxvals[] = {1};
static const unsigned uart0_pins[] = {70, 71};
static const int uart0_muxvals[] = {3, 3};
static const unsigned uart0_ctsrts_pins[] = {72, 73, 74};
static const int uart0_ctsrts_muxvals[] = {3, 3, 3};
static const unsigned uart1_pins[] = {114, 115};
static const int uart1_muxvals[] = {0, 0};
static const unsigned uart2_pins[] = {112, 113};
static const int uart2_muxvals[] = {1, 1};
static const unsigned uart3_pins[] = {110, 111};
static const int uart3_muxvals[] = {1, 1};
static const unsigned usb0_pins[] = {41, 42};
static const int usb0_muxvals[] = {0, 0};
static const unsigned usb1_pins[] = {43, 44};
static const int usb1_muxvals[] = {0, 0};
static const unsigned usb2_pins[] = {114, 115};
static const int usb2_muxvals[] = {1, 1};
static const unsigned int gpio_range0_pins[] = {
	0, 1, 2, 3, 4, 5, 6, 7,			/* PORT0x */
	8, 9, 10, 11, 12, 13, 14, 15,		/* PORT1x */
	32, 33, 34, 35, 36, 37, 38, 39,		/* PORT2x */
	59, 60, 61, 62, 63, 64, 65, 66,		/* PORT3x */
	95, 96, 97, 98, 99, 100, 101, 57,	/* PORT4x */
	70, 71, 72, 73, 74, 75, 76, 77,		/* PORT5x */
	81, 83, 84, 85, 86, 89, 90, 91,		/* PORT6x */
	118, 119, 120, 121, 122, 53, 54, 55,	/* PORT7x */
	41, 42, 43, 44, 79, 80, 18, 19,		/* PORT8x */
	110, 111, 112, 113, 114, 115, 16, 17,	/* PORT9x */
	40, 67, 68, 69, 78, 92, 93, 94,		/* PORT10x */
	48, 49, 46, 45, 123, 124, 125, 126,	/* PORT11x */
	47, 127, 20, 56, 22,			/* PORT120-124 */
};
static const unsigned int gpio_range1_pins[] = {
	116, 117,				/* PORT130-131 */
};
static const unsigned int gpio_range2_pins[] = {
	102, 103, 104, 105, 106, 107, 108, 109,	/* PORT14x */
	128, 129, 130, 131, 132, 133, 134, 135,	/* XIRQ0-7 */
	82, 87, 88, 50, 51, 23, 52, 58,	      /* XIRQ8-12, PORT165, XIRQ14-15 */
};

static const struct uniphier_pinctrl_group uniphier_sld8_groups[] = {
	UNIPHIER_PINCTRL_GROUP(emmc),
	UNIPHIER_PINCTRL_GROUP(emmc_dat8),
	UNIPHIER_PINCTRL_GROUP(ether_mii),
	UNIPHIER_PINCTRL_GROUP(ether_rmii),
	UNIPHIER_PINCTRL_GROUP(i2c0),
	UNIPHIER_PINCTRL_GROUP(i2c1),
	UNIPHIER_PINCTRL_GROUP(i2c2),
	UNIPHIER_PINCTRL_GROUP(i2c3),
	UNIPHIER_PINCTRL_GROUP(nand),
	UNIPHIER_PINCTRL_GROUP(nand_cs1),
	UNIPHIER_PINCTRL_GROUP(sd),
	UNIPHIER_PINCTRL_GROUP(spi0),
	UNIPHIER_PINCTRL_GROUP(system_bus),
	UNIPHIER_PINCTRL_GROUP(system_bus_cs1),
	UNIPHIER_PINCTRL_GROUP(system_bus_cs2),
	UNIPHIER_PINCTRL_GROUP(system_bus_cs3),
	UNIPHIER_PINCTRL_GROUP(system_bus_cs4),
	UNIPHIER_PINCTRL_GROUP(system_bus_cs5),
	UNIPHIER_PINCTRL_GROUP(uart0),
	UNIPHIER_PINCTRL_GROUP(uart0_ctsrts),
	UNIPHIER_PINCTRL_GROUP(uart1),
	UNIPHIER_PINCTRL_GROUP(uart2),
	UNIPHIER_PINCTRL_GROUP(uart3),
	UNIPHIER_PINCTRL_GROUP(usb0),
	UNIPHIER_PINCTRL_GROUP(usb1),
	UNIPHIER_PINCTRL_GROUP(usb2),
	UNIPHIER_PINCTRL_GROUP_GPIO(gpio_range0),
	UNIPHIER_PINCTRL_GROUP_GPIO(gpio_range1),
	UNIPHIER_PINCTRL_GROUP_GPIO(gpio_range2),
};

static const char * const emmc_groups[] = {"emmc", "emmc_dat8"};
static const char * const ether_mii_groups[] = {"ether_mii"};
static const char * const ether_rmii_groups[] = {"ether_rmii"};
static const char * const i2c0_groups[] = {"i2c0"};
static const char * const i2c1_groups[] = {"i2c1"};
static const char * const i2c2_groups[] = {"i2c2"};
static const char * const i2c3_groups[] = {"i2c3"};
static const char * const nand_groups[] = {"nand", "nand_cs1"};
static const char * const sd_groups[] = {"sd"};
static const char * const spi0_groups[] = {"spi0"};
static const char * const system_bus_groups[] = {"system_bus",
						 "system_bus_cs1",
						 "system_bus_cs2",
						 "system_bus_cs3",
						 "system_bus_cs4",
						 "system_bus_cs5"};
static const char * const uart0_groups[] = {"uart0", "uart0_ctsrts"};
static const char * const uart1_groups[] = {"uart1"};
static const char * const uart2_groups[] = {"uart2"};
static const char * const uart3_groups[] = {"uart3"};
static const char * const usb0_groups[] = {"usb0"};
static const char * const usb1_groups[] = {"usb1"};
static const char * const usb2_groups[] = {"usb2"};

static const struct uniphier_pinmux_function uniphier_sld8_functions[] = {
	UNIPHIER_PINMUX_FUNCTION(emmc),
	UNIPHIER_PINMUX_FUNCTION(ether_mii),
	UNIPHIER_PINMUX_FUNCTION(ether_rmii),
	UNIPHIER_PINMUX_FUNCTION(i2c0),
	UNIPHIER_PINMUX_FUNCTION(i2c1),
	UNIPHIER_PINMUX_FUNCTION(i2c2),
	UNIPHIER_PINMUX_FUNCTION(i2c3),
	UNIPHIER_PINMUX_FUNCTION(nand),
	UNIPHIER_PINMUX_FUNCTION(sd),
	UNIPHIER_PINMUX_FUNCTION(spi0),
	UNIPHIER_PINMUX_FUNCTION(system_bus),
	UNIPHIER_PINMUX_FUNCTION(uart0),
	UNIPHIER_PINMUX_FUNCTION(uart1),
	UNIPHIER_PINMUX_FUNCTION(uart2),
	UNIPHIER_PINMUX_FUNCTION(uart3),
	UNIPHIER_PINMUX_FUNCTION(usb0),
	UNIPHIER_PINMUX_FUNCTION(usb1),
	UNIPHIER_PINMUX_FUNCTION(usb2),
};

static int uniphier_sld8_get_gpio_muxval(unsigned int pin,
					 unsigned int gpio_offset)
{
	switch (gpio_offset) {
	case 120 ... 127:	/* XIRQ0-XIRQ7 */
		return 0;
	case 128 ... 132:	/* XIRQ8-12 */
	case 134 ... 135:	/* XIRQ14-15 */
		return 14;
	default:
		return 15;
	}
}

static const struct uniphier_pinctrl_socdata uniphier_sld8_pindata = {
	.pins = uniphier_sld8_pins,
	.npins = ARRAY_SIZE(uniphier_sld8_pins),
	.groups = uniphier_sld8_groups,
	.groups_count = ARRAY_SIZE(uniphier_sld8_groups),
	.functions = uniphier_sld8_functions,
	.functions_count = ARRAY_SIZE(uniphier_sld8_functions),
	.get_gpio_muxval = uniphier_sld8_get_gpio_muxval,
	.caps = 0,
};

static int uniphier_sld8_pinctrl_probe(struct platform_device *pdev)
{
	return uniphier_pinctrl_probe(pdev, &uniphier_sld8_pindata);
}

static const struct of_device_id uniphier_sld8_pinctrl_match[] = {
	{ .compatible = "socionext,uniphier-sld8-pinctrl" },
	{ /* sentinel */ }
};

static struct platform_driver uniphier_sld8_pinctrl_driver = {
	.probe = uniphier_sld8_pinctrl_probe,
	.driver = {
		.name = "uniphier-sld8-pinctrl",
		.of_match_table = uniphier_sld8_pinctrl_match,
		.pm = &uniphier_pinctrl_pm_ops,
	},
};
builtin_platform_driver(uniphier_sld8_pinctrl_driver);

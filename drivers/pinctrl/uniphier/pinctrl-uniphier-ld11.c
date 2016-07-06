/*
 * Copyright (C) 2016 Socionext Inc.
 *   Author: Masahiro Yamada <yamada.masahiro@socionext.com>
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

static const struct pinctrl_pin_desc uniphier_ld11_pins[] = {
	UNIPHIER_PINCTRL_PIN(0, "XECS1", 0,
			     0, UNIPHIER_PIN_DRV_1BIT,
			     0, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(1, "ERXW", UNIPHIER_PIN_IECTRL_NONE,
			     1, UNIPHIER_PIN_DRV_1BIT,
			     1, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(2, "XERWE1", UNIPHIER_PIN_IECTRL_NONE,
			     2, UNIPHIER_PIN_DRV_1BIT,
			     2, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(3, "XNFWP", 3,
			     3, UNIPHIER_PIN_DRV_1BIT,
			     3, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(4, "XNFCE0", 4,
			     4, UNIPHIER_PIN_DRV_1BIT,
			     4, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(5, "NFRYBY0", 5,
			     5, UNIPHIER_PIN_DRV_1BIT,
			     5, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(6, "XNFRE", UNIPHIER_PIN_IECTRL_NONE,
			     6, UNIPHIER_PIN_DRV_1BIT,
			     6, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(7, "XNFWE", UNIPHIER_PIN_IECTRL_NONE,
			     7, UNIPHIER_PIN_DRV_1BIT,
			     7, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(8, "NFALE", UNIPHIER_PIN_IECTRL_NONE,
			     8, UNIPHIER_PIN_DRV_1BIT,
			     8, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(9, "NFCLE", UNIPHIER_PIN_IECTRL_NONE,
			     9, UNIPHIER_PIN_DRV_1BIT,
			     9, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(10, "NFD0", 10,
			     10, UNIPHIER_PIN_DRV_1BIT,
			     10, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(11, "NFD1", 11,
			     11, UNIPHIER_PIN_DRV_1BIT,
			     11, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(12, "NFD2", 12,
			     12, UNIPHIER_PIN_DRV_1BIT,
			     12, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(13, "NFD3", 13,
			     13, UNIPHIER_PIN_DRV_1BIT,
			     13, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(14, "NFD4", 14,
			     14, UNIPHIER_PIN_DRV_1BIT,
			     14, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(15, "NFD5", 15,
			     15, UNIPHIER_PIN_DRV_1BIT,
			     15, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(16, "NFD6", 16,
			     16, UNIPHIER_PIN_DRV_1BIT,
			     16, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(17, "NFD7", 17,
			     17, UNIPHIER_PIN_DRV_1BIT,
			     17, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(18, "XERST", 18,
			     0, UNIPHIER_PIN_DRV_2BIT,
			     18, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(19, "MMCCLK", 19,
			     1, UNIPHIER_PIN_DRV_2BIT,
			     19, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(20, "MMCCMD", 20,
			     2, UNIPHIER_PIN_DRV_2BIT,
			     20, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(21, "MMCDS", 21,
			     3, UNIPHIER_PIN_DRV_2BIT,
			     21, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(22, "MMCDAT0", 22,
			     4, UNIPHIER_PIN_DRV_2BIT,
			     22, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(23, "MMCDAT1", 23,
			     5, UNIPHIER_PIN_DRV_2BIT,
			     23, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(24, "MMCDAT2", 24,
			     6, UNIPHIER_PIN_DRV_2BIT,
			     24, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(25, "MMCDAT3", 25,
			     7, UNIPHIER_PIN_DRV_2BIT,
			     25, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(26, "MMCDAT4", 26,
			     8, UNIPHIER_PIN_DRV_2BIT,
			     26, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(27, "MMCDAT5", 27,
			     9, UNIPHIER_PIN_DRV_2BIT,
			     27, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(28, "MMCDAT6", 28,
			     10, UNIPHIER_PIN_DRV_2BIT,
			     28, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(29, "MMCDAT7", 29,
			     11, UNIPHIER_PIN_DRV_2BIT,
			     29, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(46, "USB0VBUS", 46,
			     46, UNIPHIER_PIN_DRV_1BIT,
			     46, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(47, "USB0OD", UNIPHIER_PIN_IECTRL_NONE,
			     47, UNIPHIER_PIN_DRV_1BIT,
			     47, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(48, "USB1VBUS", 48,
			     48, UNIPHIER_PIN_DRV_1BIT,
			     48, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(49, "USB1OD", 49,
			     49, UNIPHIER_PIN_DRV_1BIT,
			     49, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(50, "USB2VBUS", 50,
			     50, UNIPHIER_PIN_DRV_1BIT,
			     50, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(51, "USB2OD", 51,
			     51, UNIPHIER_PIN_DRV_1BIT,
			     51, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(54, "TXD0", 54,
			     54, UNIPHIER_PIN_DRV_1BIT,
			     54, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(55, "RXD0", 55,
			     55, UNIPHIER_PIN_DRV_1BIT,
			     55, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(56, "SPISYNC0", 56,
			     56, UNIPHIER_PIN_DRV_1BIT,
			     56, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(57, "SPISCLK0", 57,
			     57, UNIPHIER_PIN_DRV_1BIT,
			     57, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(58, "SPITXD0", 58,
			     58, UNIPHIER_PIN_DRV_1BIT,
			     58, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(59, "SPIRXD0", 59,
			     59, UNIPHIER_PIN_DRV_1BIT,
			     59, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(60, "AGCI", 60,
			     60, UNIPHIER_PIN_DRV_1BIT,
			     60, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(61, "DMDSDA0", 61,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(62, "DMDSCL0", 62,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(63, "SDA0", 63,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(64, "SCL0", 64,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(65, "SDA1", 65,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(66, "SCL1", 66,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(67, "HIN", 67,
			     -1, UNIPHIER_PIN_DRV_FIXED5,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(68, "VIN", 68,
			     -1, UNIPHIER_PIN_DRV_FIXED5,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(69, "PCA00", 69,
			     69, UNIPHIER_PIN_DRV_1BIT,
			     69, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(70, "PCA01", 70,
			     70, UNIPHIER_PIN_DRV_1BIT,
			     70, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(71, "PCA02", 71,
			     71, UNIPHIER_PIN_DRV_1BIT,
			     71, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(72, "PCA03", 72,
			     72, UNIPHIER_PIN_DRV_1BIT,
			     72, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(73, "PCA04", 73,
			     73, UNIPHIER_PIN_DRV_1BIT,
			     73, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(74, "PCA05", 74,
			     74, UNIPHIER_PIN_DRV_1BIT,
			     74, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(75, "PCA06", 75,
			     75, UNIPHIER_PIN_DRV_1BIT,
			     75, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(76, "PCA07", 76,
			     76, UNIPHIER_PIN_DRV_1BIT,
			     76, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(77, "PCA08", 77,
			     77, UNIPHIER_PIN_DRV_1BIT,
			     77, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(78, "PCA09", 78,
			     78, UNIPHIER_PIN_DRV_1BIT,
			     78, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(79, "PCA10", 79,
			     79, UNIPHIER_PIN_DRV_1BIT,
			     79, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(80, "PCA11", 80,
			     80, UNIPHIER_PIN_DRV_1BIT,
			     80, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(81, "PCA12", 81,
			     81, UNIPHIER_PIN_DRV_1BIT,
			     81, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(82, "PCA13", 82,
			     82, UNIPHIER_PIN_DRV_1BIT,
			     82, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(83, "PCA14", 83,
			     83, UNIPHIER_PIN_DRV_1BIT,
			     83, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(84, "PC0READY", 84,
			     84, UNIPHIER_PIN_DRV_1BIT,
			     84, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(85, "PC0CD1", 85,
			     85, UNIPHIER_PIN_DRV_1BIT,
			     85, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(86, "PC0CD2", 86,
			     86, UNIPHIER_PIN_DRV_1BIT,
			     86, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(87, "PC0WAIT", 87,
			     87, UNIPHIER_PIN_DRV_1BIT,
			     87, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(88, "PC0RESET", 88,
			     88, UNIPHIER_PIN_DRV_1BIT,
			     88, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(89, "PC0CE1", 89,
			     89, UNIPHIER_PIN_DRV_1BIT,
			     89, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(90, "PC0WE", 90,
			     90, UNIPHIER_PIN_DRV_1BIT,
			     90, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(91, "PC0OE", 91,
			     91, UNIPHIER_PIN_DRV_1BIT,
			     91, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(92, "PC0IOWR", 92,
			     92, UNIPHIER_PIN_DRV_1BIT,
			     92, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(93, "PC0IORD", 93,
			     93, UNIPHIER_PIN_DRV_1BIT,
			     93, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(94, "PCD00", 94,
			     94, UNIPHIER_PIN_DRV_1BIT,
			     94, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(95, "PCD01", 95,
			     95, UNIPHIER_PIN_DRV_1BIT,
			     95, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(96, "PCD02", 96,
			     96, UNIPHIER_PIN_DRV_1BIT,
			     96, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(97, "PCD03", 97,
			     97, UNIPHIER_PIN_DRV_1BIT,
			     97, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(98, "PCD04", 98,
			     98, UNIPHIER_PIN_DRV_1BIT,
			     98, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(99, "PCD05", 99,
			     99, UNIPHIER_PIN_DRV_1BIT,
			     99, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(100, "PCD06", 100,
			     100, UNIPHIER_PIN_DRV_1BIT,
			     100, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(101, "PCD07", 101,
			     101, UNIPHIER_PIN_DRV_1BIT,
			     101, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(102, "HS0BCLKIN", 102,
			     102, UNIPHIER_PIN_DRV_1BIT,
			     102, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(103, "HS0SYNCIN", 103,
			     103, UNIPHIER_PIN_DRV_1BIT,
			     103, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(104, "HS0VALIN", 104,
			     104, UNIPHIER_PIN_DRV_1BIT,
			     104, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(105, "HS0DIN0", 105,
			     105, UNIPHIER_PIN_DRV_1BIT,
			     105, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(106, "HS0DIN1", 106,
			     106, UNIPHIER_PIN_DRV_1BIT,
			     106, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(107, "HS0DIN2", 107,
			     107, UNIPHIER_PIN_DRV_1BIT,
			     107, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(108, "HS0DIN3", 108,
			     108, UNIPHIER_PIN_DRV_1BIT,
			     108, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(109, "HS0DIN4", 109,
			     109, UNIPHIER_PIN_DRV_1BIT,
			     109, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(110, "HS0DIN5", 110,
			     110, UNIPHIER_PIN_DRV_1BIT,
			     110, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(111, "HS0DIN6", 111,
			     111, UNIPHIER_PIN_DRV_1BIT,
			     111, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(112, "HS0DIN7", 112,
			     112, UNIPHIER_PIN_DRV_1BIT,
			     112, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(113, "HS0BCLKOUT", 113,
			     113, UNIPHIER_PIN_DRV_1BIT,
			     113, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(114, "HS0SYNCOUT", 114,
			     114, UNIPHIER_PIN_DRV_1BIT,
			     114, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(115, "HS0VALOUT", 115,
			     115, UNIPHIER_PIN_DRV_1BIT,
			     115, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(116, "HS0DOUT0", 116,
			     116, UNIPHIER_PIN_DRV_1BIT,
			     116, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(117, "HS0DOUT1", 117,
			     117, UNIPHIER_PIN_DRV_1BIT,
			     117, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(118, "HS0DOUT2", 118,
			     118, UNIPHIER_PIN_DRV_1BIT,
			     118, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(119, "HS0DOUT3", 119,
			     119, UNIPHIER_PIN_DRV_1BIT,
			     119, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(120, "HS0DOUT4", 120,
			     120, UNIPHIER_PIN_DRV_1BIT,
			     120, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(121, "HS0DOUT5", 121,
			     121, UNIPHIER_PIN_DRV_1BIT,
			     121, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(122, "HS0DOUT6", 122,
			     122, UNIPHIER_PIN_DRV_1BIT,
			     122, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(123, "HS0DOUT7", 123,
			     123, UNIPHIER_PIN_DRV_1BIT,
			     123, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(124, "HS1BCLKIN", 124,
			     124, UNIPHIER_PIN_DRV_1BIT,
			     124, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(125, "HS1SYNCIN", 125,
			     125, UNIPHIER_PIN_DRV_1BIT,
			     125, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(126, "HS1VALIN", 126,
			     126, UNIPHIER_PIN_DRV_1BIT,
			     126, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(127, "HS1DIN0", 127,
			     127, UNIPHIER_PIN_DRV_1BIT,
			     127, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(128, "HS1DIN1", 128,
			     128, UNIPHIER_PIN_DRV_1BIT,
			     128, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(129, "HS1DIN2", 129,
			     129, UNIPHIER_PIN_DRV_1BIT,
			     129, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(130, "HS1DIN3", 130,
			     130, UNIPHIER_PIN_DRV_1BIT,
			     130, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(131, "HS1DIN4", 131,
			     131, UNIPHIER_PIN_DRV_1BIT,
			     131, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(132, "HS1DIN5", 132,
			     132, UNIPHIER_PIN_DRV_1BIT,
			     132, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(133, "HS1DIN6", 133,
			     133, UNIPHIER_PIN_DRV_1BIT,
			     133, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(134, "HS1DIN7", 134,
			     134, UNIPHIER_PIN_DRV_1BIT,
			     134, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(135, "AO1IEC", 135,
			     135, UNIPHIER_PIN_DRV_1BIT,
			     135, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(136, "AO1ARC", 136,
			     136, UNIPHIER_PIN_DRV_1BIT,
			     136, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(137, "AO1DACCK", 137,
			     137, UNIPHIER_PIN_DRV_1BIT,
			     137, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(138, "AO1BCK", 138,
			     138, UNIPHIER_PIN_DRV_1BIT,
			     138, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(139, "AO1LRCK", 139,
			     139, UNIPHIER_PIN_DRV_1BIT,
			     139, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(140, "AO1D0", 140,
			     140, UNIPHIER_PIN_DRV_1BIT,
			     140, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(141, "TCON0", 141,
			     141, UNIPHIER_PIN_DRV_1BIT,
			     141, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(142, "TCON1", 142,
			     142, UNIPHIER_PIN_DRV_1BIT,
			     142, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(143, "TCON2", 143,
			     143, UNIPHIER_PIN_DRV_1BIT,
			     143, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(144, "TCON3", 144,
			     144, UNIPHIER_PIN_DRV_1BIT,
			     144, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(145, "TCON4", 145,
			     145, UNIPHIER_PIN_DRV_1BIT,
			     145, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(146, "TCON5", 146,
			     146, UNIPHIER_PIN_DRV_1BIT,
			     146, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(147, "PWMA", 147,
			     147, UNIPHIER_PIN_DRV_1BIT,
			     147, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(148, "LR_GOUT", 148,
			     148, UNIPHIER_PIN_DRV_1BIT,
			     148, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(149, "XIRQ0", 149,
			     149, UNIPHIER_PIN_DRV_1BIT,
			     149, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(150, "XIRQ1", 150,
			     150, UNIPHIER_PIN_DRV_1BIT,
			     150, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(151, "XIRQ2", 151,
			     151, UNIPHIER_PIN_DRV_1BIT,
			     151, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(152, "XIRQ3", 152,
			     152, UNIPHIER_PIN_DRV_1BIT,
			     152, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(153, "XIRQ4", 153,
			     153, UNIPHIER_PIN_DRV_1BIT,
			     153, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(154, "XIRQ5", 154,
			     154, UNIPHIER_PIN_DRV_1BIT,
			     154, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(155, "XIRQ6", 155,
			     155, UNIPHIER_PIN_DRV_1BIT,
			     155, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(156, "XIRQ7", 156,
			     156, UNIPHIER_PIN_DRV_1BIT,
			     156, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(157, "XIRQ8", 157,
			     157, UNIPHIER_PIN_DRV_1BIT,
			     157, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(158, "AGCBS", 158,
			     158, UNIPHIER_PIN_DRV_1BIT,
			     158, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(159, "XIRQ21", 159,
			     159, UNIPHIER_PIN_DRV_1BIT,
			     159, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(160, "XIRQ22", 160,
			     160, UNIPHIER_PIN_DRV_1BIT,
			     160, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(161, "XIRQ23", 161,
			     161, UNIPHIER_PIN_DRV_1BIT,
			     161, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(162, "CH2CLK", 162,
			     162, UNIPHIER_PIN_DRV_1BIT,
			     162, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(163, "CH2PSYNC", 163,
			     163, UNIPHIER_PIN_DRV_1BIT,
			     163, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(164, "CH2VAL", 164,
			     164, UNIPHIER_PIN_DRV_1BIT,
			     164, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(165, "CH2DATA", 165,
			     165, UNIPHIER_PIN_DRV_1BIT,
			     165, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(166, "CK25O", 166,
			     166, UNIPHIER_PIN_DRV_1BIT,
			     166, UNIPHIER_PIN_PULL_DOWN),
};

static const unsigned emmc_pins[] = {18, 19, 20, 21, 22, 23, 24, 25};
static const int emmc_muxvals[] = {0, 0, 0, 0, 0, 0, 0, 0};
static const unsigned emmc_dat8_pins[] = {26, 27, 28, 29};
static const int emmc_dat8_muxvals[] = {0, 0, 0, 0};
static const unsigned ether_rmii_pins[] = {6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
					   16, 17};
static const int ether_rmii_muxvals[] = {4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4};
static const unsigned i2c0_pins[] = {63, 64};
static const int i2c0_muxvals[] = {0, 0};
static const unsigned i2c1_pins[] = {65, 66};
static const int i2c1_muxvals[] = {0, 0};
static const unsigned i2c3_pins[] = {67, 68};
static const int i2c3_muxvals[] = {1, 1};
static const unsigned i2c4_pins[] = {61, 62};
static const int i2c4_muxvals[] = {1, 1};
static const unsigned nand_pins[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
				     15, 16, 17};
static const int nand_muxvals[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const unsigned system_bus_pins[] = {1, 2, 6, 7, 8, 9, 10, 11, 12, 13,
					   14, 15, 16, 17};
static const int system_bus_muxvals[] = {0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
					 2};
static const unsigned system_bus_cs1_pins[] = {0};
static const int system_bus_cs1_muxvals[] = {0};
static const unsigned uart0_pins[] = {54, 55};
static const int uart0_muxvals[] = {0, 0};
static const unsigned uart1_pins[] = {58, 59};
static const int uart1_muxvals[] = {1, 1};
static const unsigned uart2_pins[] = {90, 91};
static const int uart2_muxvals[] = {1, 1};
static const unsigned uart3_pins[] = {94, 95};
static const int uart3_muxvals[] = {1, 1};
static const unsigned usb0_pins[] = {46, 47};
static const int usb0_muxvals[] = {0, 0};
static const unsigned usb1_pins[] = {48, 49};
static const int usb1_muxvals[] = {0, 0};
static const unsigned usb2_pins[] = {50, 51};
static const int usb2_muxvals[] = {0, 0};
static const unsigned port_range_pins[] = {
	159, 160, 161, 162, 163, 164, 165, 166,		/* PORT0x */
	0, 1, 2, 3, 4, 5, 6, 7,				/* PORT1x */
	8, 9, 10, 11, 12, 13, 14, 15,			/* PORT2x */
	16, 17, 18, -1, -1, -1, -1, -1,			/* PORT3x */
	-1, -1, -1, -1, -1, -1, -1, -1,			/* PORT4x */
	-1, -1, -1, 46, 47, 48, 49, 50,			/* PORT5x */
	51, -1, -1, 54, 55, 56, 57, 58,			/* PORT6x */
	59, 60, 69, 70, 71, 72, 73, 74,			/* PORT7x */
	75, 76, 77, 78, 79, 80, 81, 82,			/* PORT8x */
	83, 84, 85, 86, 87, 88, 89, 90,			/* PORT9x */
	91, 92, 93, 94, 95, 96, 97, 98,			/* PORT10x */
	-1, -1, -1, -1, -1, -1, -1, -1,			/* PORT11x */
	99, 100, 101, 102, 103, 104, 105, 106,		/* PORT12x */
	107, 108, 109, 110, 111, 112, 113, 114,		/* PORT13x */
	115, 116, 117, 118, 119, 120, 121, 122,		/* PORT14x */
	-1, -1, -1, -1, -1, -1, -1, -1,			/* PORT15x */
	-1, -1, -1, -1, -1, -1, -1, -1,			/* PORT16x */
	-1, -1, -1, -1, -1, -1, -1, -1,			/* PORT17x */
	61, 62, 63, 64, 65, 66, 67, 68,			/* PORT18x */
	-1, -1, -1, -1, -1, -1, -1, -1,			/* PORT19x */
	123, 124, 125, 126, 127, 128, 129, 130,		/* PORT20x */
	131, 132, 133, 134, 135, 136, 137, 138,		/* PORT21x */
	139, 140, 141, 142, -1, -1, -1, -1,		/* PORT22x */
	147, 148, 149, 150, 151, 152, 153, 154,		/* PORT23x */
	155, 156, 157, 143, 144, 145, 146, 158,		/* PORT24x */
};
static const int port_range_muxvals[] = {
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT0x */
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT1x */
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT2x */
	15, 15, 15, -1, -1, -1, -1, -1,			/* PORT3x */
	-1, -1, -1, -1, -1, -1, -1, -1,			/* PORT4x */
	-1, -1, -1, 15, 15, 15, 15, 15,			/* PORT5x */
	15, -1, -1, 15, 15, 15, 15, 15,			/* PORT6x */
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT7x */
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT8x */
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT9x */
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT10x */
	-1, -1, -1, -1, -1, -1, -1, -1,			/* PORT11x */
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT12x */
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT13x */
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT14x */
	-1, -1, -1, -1, -1, -1, -1, -1,			/* PORT15x */
	-1, -1, -1, -1, -1, -1, -1, -1,			/* PORT16x */
	-1, -1, -1, -1, -1, -1, -1, -1,			/* PORT17x */
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT18x */
	-1, -1, -1, -1, -1, -1, -1, -1,			/* PORT19x */
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT20x */
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT21x */
	15, 15, 15, 15, -1, -1, -1, -1,			/* PORT22x */
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT23x */
	15, 15, 15, 15, 15, 15, 15, 15,			/* PORT24x */
};
static const unsigned xirq_pins[] = {
	149, 150, 151, 152, 153, 154, 155, 156,		/* XIRQ0-7 */
	157, 143, 144, 145, 85, 146, 158, 84,		/* XIRQ8-15 */
	141, 142, 148, 50, 51, 159, 160, 161,		/* XIRQ16-23 */
};
static const int xirq_muxvals[] = {
	14, 14, 14, 14, 14, 14, 14, 14,			/* XIRQ0-7 */
	14, 14, 14, 14, 13, 14, 14, 13,			/* XIRQ8-15 */
	14, 14, 14, 14, 14, 14, 14, 14,			/* XIRQ16-23 */
};
static const unsigned xirq_alternatives_pins[] = {
	94, 95, 96, 97, 98, 99, 100, 101,		/* XIRQ0-7 */
	102, 103, 104, 105, 106, 107,			/* XIRQ8-11,13,14 */
	108, 109, 110, 111, 112, 113, 114, 115,		/* XIRQ16-23 */
	9, 10, 11, 12, 13, 14, 15, 16,			/* XIRQ4-11 */
	17, 0, 1, 2, 3, 4, 5, 6, 7, 8,			/* XIRQ13,14,16-23 */
	139, 140, 135, 147,				/* XIRQ17,18,21,22 */
};
static const int xirq_alternatives_muxvals[] = {
	14, 14, 14, 14, 14, 14, 14, 14,			/* XIRQ0-7 */
	14, 14, 14, 14, 14, 14,				/* XIRQ8-11,13,14 */
	14, 14, 14, 14, 14, 14, 14, 14,			/* XIRQ16-23 */
	14, 14, 14, 14, 14, 14, 14, 14,			/* XIRQ4-11 */
	14, 14, 14, 14, 14, 14, 14, 14, 14, 14,		/* XIRQ13,14,16-23 */
	14, 14, 14, 14,					/* XIRQ17,18,21,22 */
};

static const struct uniphier_pinctrl_group uniphier_ld11_groups[] = {
	UNIPHIER_PINCTRL_GROUP(emmc),
	UNIPHIER_PINCTRL_GROUP(emmc_dat8),
	UNIPHIER_PINCTRL_GROUP(ether_rmii),
	UNIPHIER_PINCTRL_GROUP(i2c0),
	UNIPHIER_PINCTRL_GROUP(i2c1),
	UNIPHIER_PINCTRL_GROUP(i2c3),
	UNIPHIER_PINCTRL_GROUP(i2c4),
	UNIPHIER_PINCTRL_GROUP(nand),
	UNIPHIER_PINCTRL_GROUP(system_bus),
	UNIPHIER_PINCTRL_GROUP(system_bus_cs1),
	UNIPHIER_PINCTRL_GROUP(uart0),
	UNIPHIER_PINCTRL_GROUP(uart1),
	UNIPHIER_PINCTRL_GROUP(uart2),
	UNIPHIER_PINCTRL_GROUP(uart3),
	UNIPHIER_PINCTRL_GROUP(usb0),
	UNIPHIER_PINCTRL_GROUP(usb1),
	UNIPHIER_PINCTRL_GROUP(usb2),
	UNIPHIER_PINCTRL_GROUP_GPIO_RANGE_PORT(port_range),
	UNIPHIER_PINCTRL_GROUP_GPIO_RANGE_IRQ(xirq),
	UNIPHIER_PINCTRL_GROUP_GPIO_RANGE_IRQ(xirq_alternatives),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port00, port_range, 0),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port01, port_range, 1),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port02, port_range, 2),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port03, port_range, 3),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port04, port_range, 4),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port05, port_range, 5),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port06, port_range, 6),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port07, port_range, 7),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port10, port_range, 8),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port11, port_range, 9),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port12, port_range, 10),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port13, port_range, 11),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port14, port_range, 12),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port15, port_range, 13),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port16, port_range, 14),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port17, port_range, 15),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port20, port_range, 16),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port21, port_range, 17),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port22, port_range, 18),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port23, port_range, 19),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port24, port_range, 20),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port25, port_range, 21),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port26, port_range, 22),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port27, port_range, 23),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port30, port_range, 24),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port31, port_range, 25),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port32, port_range, 26),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port53, port_range, 43),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port54, port_range, 44),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port55, port_range, 45),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port56, port_range, 46),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port57, port_range, 47),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port60, port_range, 48),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port63, port_range, 51),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port64, port_range, 52),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port65, port_range, 53),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port66, port_range, 54),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port67, port_range, 55),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port70, port_range, 56),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port71, port_range, 57),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port72, port_range, 58),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port73, port_range, 59),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port74, port_range, 60),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port75, port_range, 61),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port76, port_range, 62),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port77, port_range, 63),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port80, port_range, 64),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port81, port_range, 65),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port82, port_range, 66),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port83, port_range, 67),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port84, port_range, 68),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port85, port_range, 69),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port86, port_range, 70),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port87, port_range, 71),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port90, port_range, 72),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port91, port_range, 73),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port92, port_range, 74),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port93, port_range, 75),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port94, port_range, 76),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port95, port_range, 77),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port96, port_range, 78),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port97, port_range, 79),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port100, port_range, 80),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port101, port_range, 81),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port102, port_range, 82),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port103, port_range, 83),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port104, port_range, 84),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port105, port_range, 85),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port106, port_range, 86),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port107, port_range, 87),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port120, port_range, 96),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port121, port_range, 97),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port122, port_range, 98),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port123, port_range, 99),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port124, port_range, 100),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port125, port_range, 101),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port126, port_range, 102),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port127, port_range, 103),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port130, port_range, 104),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port131, port_range, 105),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port132, port_range, 106),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port133, port_range, 107),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port134, port_range, 108),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port135, port_range, 109),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port136, port_range, 110),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port137, port_range, 111),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port140, port_range, 112),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port141, port_range, 113),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port142, port_range, 114),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port143, port_range, 115),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port144, port_range, 116),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port145, port_range, 117),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port146, port_range, 118),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port147, port_range, 119),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port180, port_range, 144),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port181, port_range, 145),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port182, port_range, 146),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port183, port_range, 147),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port184, port_range, 148),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port185, port_range, 149),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port186, port_range, 150),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port187, port_range, 151),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port200, port_range, 160),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port201, port_range, 161),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port202, port_range, 162),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port203, port_range, 163),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port204, port_range, 164),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port205, port_range, 165),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port206, port_range, 166),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port207, port_range, 167),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port210, port_range, 168),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port211, port_range, 169),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port212, port_range, 170),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port213, port_range, 171),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port214, port_range, 172),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port215, port_range, 173),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port216, port_range, 174),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port217, port_range, 175),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port220, port_range, 176),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port221, port_range, 177),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port222, port_range, 178),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port223, port_range, 179),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port230, port_range, 184),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port231, port_range, 185),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port232, port_range, 186),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port233, port_range, 187),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port234, port_range, 188),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port235, port_range, 189),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port236, port_range, 190),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port237, port_range, 191),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port240, port_range, 192),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port241, port_range, 193),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port242, port_range, 194),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port243, port_range, 195),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port244, port_range, 196),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port245, port_range, 197),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port246, port_range, 198),
	UNIPHIER_PINCTRL_GROUP_SINGLE(port247, port_range, 199),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq0, xirq, 0),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq1, xirq, 1),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq2, xirq, 2),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq3, xirq, 3),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq4, xirq, 4),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq5, xirq, 5),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq6, xirq, 6),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq7, xirq, 7),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq8, xirq, 8),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq9, xirq, 9),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq10, xirq, 10),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq11, xirq, 11),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq12, xirq, 12),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq13, xirq, 13),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq14, xirq, 14),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq15, xirq, 15),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq16, xirq, 16),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq17, xirq, 17),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq18, xirq, 18),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq19, xirq, 19),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq20, xirq, 20),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq21, xirq, 21),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq22, xirq, 22),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq23, xirq, 23),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq0b, xirq_alternatives, 0),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq1b, xirq_alternatives, 1),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq2b, xirq_alternatives, 2),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq3b, xirq_alternatives, 3),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq4b, xirq_alternatives, 4),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq5b, xirq_alternatives, 5),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq6b, xirq_alternatives, 6),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq7b, xirq_alternatives, 7),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq8b, xirq_alternatives, 8),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq9b, xirq_alternatives, 9),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq10b, xirq_alternatives, 10),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq11b, xirq_alternatives, 11),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq13b, xirq_alternatives, 12),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq14b, xirq_alternatives, 13),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq16b, xirq_alternatives, 14),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq17b, xirq_alternatives, 15),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq18b, xirq_alternatives, 16),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq19b, xirq_alternatives, 17),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq20b, xirq_alternatives, 18),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq21b, xirq_alternatives, 19),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq22b, xirq_alternatives, 20),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq23b, xirq_alternatives, 21),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq4c, xirq_alternatives, 22),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq5c, xirq_alternatives, 23),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq6c, xirq_alternatives, 24),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq7c, xirq_alternatives, 25),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq8c, xirq_alternatives, 26),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq9c, xirq_alternatives, 27),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq10c, xirq_alternatives, 28),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq11c, xirq_alternatives, 29),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq13c, xirq_alternatives, 30),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq14c, xirq_alternatives, 31),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq16c, xirq_alternatives, 32),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq17c, xirq_alternatives, 33),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq18c, xirq_alternatives, 34),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq19c, xirq_alternatives, 35),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq20c, xirq_alternatives, 36),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq21c, xirq_alternatives, 37),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq22c, xirq_alternatives, 38),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq23c, xirq_alternatives, 39),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq17d, xirq_alternatives, 40),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq18d, xirq_alternatives, 41),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq21d, xirq_alternatives, 42),
	UNIPHIER_PINCTRL_GROUP_SINGLE(xirq22d, xirq_alternatives, 43),
};

static const char * const emmc_groups[] = {"emmc", "emmc_dat8"};
static const char * const ether_rmii_groups[] = {"ether_rmii"};
static const char * const i2c0_groups[] = {"i2c0"};
static const char * const i2c1_groups[] = {"i2c1"};
static const char * const i2c3_groups[] = {"i2c3"};
static const char * const i2c4_groups[] = {"i2c4"};
static const char * const nand_groups[] = {"nand"};
static const char * const system_bus_groups[] = {"system_bus",
						 "system_bus_cs1"};
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
	"port30",  "port31",  "port32",
	/* port33-52 missing */          "port53",
	"port54",  "port55",  "port56",  "port57",
	"port60", /* port61-62 missing*/ "port63",
	"port64",  "port65",  "port66",  "port67",
	"port70",  "port71",  "port72",  "port73",
	"port74",  "port75",  "port76",  "port77",
	"port80",  "port81",  "port82",  "port83",
	"port84",  "port85",  "port86",  "port87",
	"port90",  "port91",  "port92",  "port93",
	"port94",  "port95",  "port96",  "port97",
	"port100", "port101", "port102", "port103",
	"port104", "port105", "port106", "port107",
	/* port110-117 missing */
	"port120", "port121", "port122", "port123",
	"port124", "port125", "port126", "port127",
	"port130", "port131", "port132", "port133",
	"port134", "port135", "port136", "port137",
	"port140", "port141", "port142", "port143",
	"port144", "port145", "port146", "port147",
	/* port150-177 missing */
	"port180", "port181", "port182", "port183",
	"port184", "port185", "port186", "port187",
	/* port190-197 missing */
	"port200", "port201", "port202", "port203",
	"port204", "port205", "port206", "port207",
	"port210", "port211", "port212", "port213",
	"port214", "port215", "port216", "port217",
	"port220", "port221", "port222", "port223",
	/* port224-227 missing */
	"port230", "port231", "port232", "port233",
	"port234", "port235", "port236", "port237",
	"port240", "port241", "port242", "port243",
	"port244", "port245", "port246", "port247",
};
static const char * const xirq_groups[] = {
	"xirq0",  "xirq1",  "xirq2",  "xirq3",
	"xirq4",  "xirq5",  "xirq6",  "xirq7",
	"xirq8",  "xirq9",  "xirq10", "xirq11",
	"xirq12", "xirq13", "xirq14", "xirq15",
	"xirq16", "xirq17", "xirq18", "xirq19",
	"xirq20", "xirq21", "xirq22", "xirq23",
	"xirq0b",  "xirq1b",  "xirq2b",  "xirq3b",
	"xirq4b",  "xirq5b",  "xirq6b",  "xirq7b",
	"xirq8b",  "xirq9b",  "xirq10b", "xirq11b",
	/* none */ "xirq13b", "xirq14b", /* none */
	"xirq16b", "xirq17b", "xirq18b", "xirq19b",
	"xirq20b", "xirq21b", "xirq22b", "xirq23b",
	"xirq4c",  "xirq5c",  "xirq6c",  "xirq7c",
	"xirq8c",  "xirq9c",  "xirq10c", "xirq11c",
	/* none */ "xirq13c", "xirq14c", /* none */
	"xirq16c", "xirq17c", "xirq18c", "xirq19c",
	"xirq20c", "xirq21c", "xirq22c", "xirq23c",
	"xirq17d", "xirq18d", "xirq21d", "xirq22d",
};

static const struct uniphier_pinmux_function uniphier_ld11_functions[] = {
	UNIPHIER_PINMUX_FUNCTION(emmc),
	UNIPHIER_PINMUX_FUNCTION(ether_rmii),
	UNIPHIER_PINMUX_FUNCTION(i2c0),
	UNIPHIER_PINMUX_FUNCTION(i2c1),
	UNIPHIER_PINMUX_FUNCTION(i2c3),
	UNIPHIER_PINMUX_FUNCTION(i2c4),
	UNIPHIER_PINMUX_FUNCTION(nand),
	UNIPHIER_PINMUX_FUNCTION(system_bus),
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

static struct uniphier_pinctrl_socdata uniphier_ld11_pindata = {
	.pins = uniphier_ld11_pins,
	.npins = ARRAY_SIZE(uniphier_ld11_pins),
	.groups = uniphier_ld11_groups,
	.groups_count = ARRAY_SIZE(uniphier_ld11_groups),
	.functions = uniphier_ld11_functions,
	.functions_count = ARRAY_SIZE(uniphier_ld11_functions),
	.caps = UNIPHIER_PINCTRL_CAPS_PERPIN_IECTRL,
};

static int uniphier_ld11_pinctrl_probe(struct platform_device *pdev)
{
	return uniphier_pinctrl_probe(pdev, &uniphier_ld11_pindata);
}

static const struct of_device_id uniphier_ld11_pinctrl_match[] = {
	{ .compatible = "socionext,uniphier-ld11-pinctrl" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, uniphier_ld11_pinctrl_match);

static struct platform_driver uniphier_ld11_pinctrl_driver = {
	.probe = uniphier_ld11_pinctrl_probe,
	.driver = {
		.name = "uniphier-ld11-pinctrl",
		.of_match_table = uniphier_ld11_pinctrl_match,
	},
};
module_platform_driver(uniphier_ld11_pinctrl_driver);

MODULE_AUTHOR("Masahiro Yamada <yamada.masahiro@socionext.com>");
MODULE_DESCRIPTION("UniPhier PH1-LD11 pinctrl driver");
MODULE_LICENSE("GPL");

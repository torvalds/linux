// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (C) 2019 Socionext Inc.
//   Author: Masahiro Yamada <yamada.masahiro@socionext.com>

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/platform_device.h>

#include "pinctrl-uniphier.h"

static const struct pinctrl_pin_desc uniphier_nx1_pins[] = {
	UNIPHIER_PINCTRL_PIN(0, "LPST", UNIPHIER_PIN_IECTRL_EXIST,
			     0, UNIPHIER_PIN_DRV_3BIT,
			     0, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(1, "SDCLK", UNIPHIER_PIN_IECTRL_EXIST,
			     12, UNIPHIER_PIN_DRV_2BIT,
			     1, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(2, "SDCMD", UNIPHIER_PIN_IECTRL_EXIST,
			     13, UNIPHIER_PIN_DRV_2BIT,
			     2, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(3, "SDDAT0", UNIPHIER_PIN_IECTRL_EXIST,
			     14, UNIPHIER_PIN_DRV_2BIT,
			     3, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(4, "SDDAT1", UNIPHIER_PIN_IECTRL_EXIST,
			     15, UNIPHIER_PIN_DRV_2BIT,
			     4, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(5, "SDDAT2", UNIPHIER_PIN_IECTRL_EXIST,
			     16, UNIPHIER_PIN_DRV_2BIT,
			     5, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(6, "SDDAT3", UNIPHIER_PIN_IECTRL_EXIST,
			     17, UNIPHIER_PIN_DRV_2BIT,
			     6, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(7, "SDCD", UNIPHIER_PIN_IECTRL_EXIST,
			     1, UNIPHIER_PIN_DRV_3BIT,
			     7, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(8, "SDWP", UNIPHIER_PIN_IECTRL_EXIST,
			     2, UNIPHIER_PIN_DRV_3BIT,
			     8, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(9, "SDVOLC", UNIPHIER_PIN_IECTRL_EXIST,
			     3, UNIPHIER_PIN_DRV_3BIT,
			     9, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(10, "XERST", UNIPHIER_PIN_IECTRL_EXIST,
			     0, UNIPHIER_PIN_DRV_2BIT,
			     10, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(11, "MDC", UNIPHIER_PIN_IECTRL_EXIST,
			     18, UNIPHIER_PIN_DRV_2BIT,
			     11, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(12, "MDIO", UNIPHIER_PIN_IECTRL_EXIST,
			     19, UNIPHIER_PIN_DRV_2BIT,
			     12, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(13, "MDIO_INTL", UNIPHIER_PIN_IECTRL_EXIST,
			     20, UNIPHIER_PIN_DRV_2BIT,
			     13, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(14, "PHYRSTL", UNIPHIER_PIN_IECTRL_EXIST,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(15, "RGMII_RXCLK", UNIPHIER_PIN_IECTRL_EXIST,
			     22, UNIPHIER_PIN_DRV_2BIT,
			     15, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(16, "RGMII_RXD0", UNIPHIER_PIN_IECTRL_EXIST,
			     23, UNIPHIER_PIN_DRV_2BIT,
			     16, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(17, "RGMII_RXD1", UNIPHIER_PIN_IECTRL_EXIST,
			     24, UNIPHIER_PIN_DRV_2BIT,
			     17, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(18, "RGMII_RXD2", UNIPHIER_PIN_IECTRL_EXIST,
			     25, UNIPHIER_PIN_DRV_2BIT,
			     18, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(19, "RGMII_RXD3", UNIPHIER_PIN_IECTRL_EXIST,
			     26, UNIPHIER_PIN_DRV_2BIT,
			     19, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(20, "RGMII_RXCTL", UNIPHIER_PIN_IECTRL_EXIST,
			     27, UNIPHIER_PIN_DRV_2BIT,
			     20, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(21, "RGMII_TXCLK", UNIPHIER_PIN_IECTRL_EXIST,
			     28, UNIPHIER_PIN_DRV_2BIT,
			     21, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(22, "RGMII_TXD0", UNIPHIER_PIN_IECTRL_EXIST,
			     29, UNIPHIER_PIN_DRV_2BIT,
			     22, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(23, "RGMII_TXD1", UNIPHIER_PIN_IECTRL_EXIST,
			     30, UNIPHIER_PIN_DRV_2BIT,
			     23, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(24, "RGMII_TXD2", UNIPHIER_PIN_IECTRL_EXIST,
			     31, UNIPHIER_PIN_DRV_2BIT,
			     24, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(25, "RGMII_TXD3", UNIPHIER_PIN_IECTRL_EXIST,
			     32, UNIPHIER_PIN_DRV_2BIT,
			     25, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(26, "RGMII_TXCTL", UNIPHIER_PIN_IECTRL_EXIST,
			     33, UNIPHIER_PIN_DRV_2BIT,
			     26, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(27, "TXD0", UNIPHIER_PIN_IECTRL_EXIST,
			     4, UNIPHIER_PIN_DRV_3BIT,
			     27, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(28, "RXD0", UNIPHIER_PIN_IECTRL_EXIST,
			     5, UNIPHIER_PIN_DRV_3BIT,
			     28, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(29, "TXD1", UNIPHIER_PIN_IECTRL_EXIST,
			     6, UNIPHIER_PIN_DRV_3BIT,
			     29, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(30, "RXD1", UNIPHIER_PIN_IECTRL_EXIST,
			     7, UNIPHIER_PIN_DRV_3BIT,
			     30, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(31, "XRTS1", UNIPHIER_PIN_IECTRL_EXIST,
			     8, UNIPHIER_PIN_DRV_3BIT,
			     31, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(32, "XDTR1", UNIPHIER_PIN_IECTRL_EXIST,
			     9, UNIPHIER_PIN_DRV_3BIT,
			     32, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(33, "XCTS1", UNIPHIER_PIN_IECTRL_EXIST,
			     10, UNIPHIER_PIN_DRV_3BIT,
			     33, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(34, "XDSR1", UNIPHIER_PIN_IECTRL_EXIST,
			     11, UNIPHIER_PIN_DRV_3BIT,
			     34, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(35, "XDCD1", UNIPHIER_PIN_IECTRL_EXIST,
			     12, UNIPHIER_PIN_DRV_3BIT,
			     35, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(36, "TXD2", UNIPHIER_PIN_IECTRL_EXIST,
			     13, UNIPHIER_PIN_DRV_3BIT,
			     36, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(37, "RXD2", UNIPHIER_PIN_IECTRL_EXIST,
			     14, UNIPHIER_PIN_DRV_3BIT,
			     37, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(38, "XRTS2", UNIPHIER_PIN_IECTRL_EXIST,
			     15, UNIPHIER_PIN_DRV_3BIT,
			     38, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(39, "XCTS2", UNIPHIER_PIN_IECTRL_EXIST,
			     16, UNIPHIER_PIN_DRV_3BIT,
			     39, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(40, "TXD3", UNIPHIER_PIN_IECTRL_EXIST,
			     17, UNIPHIER_PIN_DRV_3BIT,
			     40, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(41, "RXD3", UNIPHIER_PIN_IECTRL_EXIST,
			     18, UNIPHIER_PIN_DRV_3BIT,
			     41, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(42, "SPISYNC0", UNIPHIER_PIN_IECTRL_EXIST,
			     19, UNIPHIER_PIN_DRV_3BIT,
			     42, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(43, "SPISCLK0", UNIPHIER_PIN_IECTRL_EXIST,
			     20, UNIPHIER_PIN_DRV_3BIT,
			     43, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(44, "SPITXD0", UNIPHIER_PIN_IECTRL_EXIST,
			     21, UNIPHIER_PIN_DRV_3BIT,
			     44, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(45, "SPIRXD0", UNIPHIER_PIN_IECTRL_EXIST,
			     22, UNIPHIER_PIN_DRV_3BIT,
			     45, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(46, "SPISYNC1", UNIPHIER_PIN_IECTRL_EXIST,
			     23, UNIPHIER_PIN_DRV_3BIT,
			     46, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(47, "SPISCLK1", UNIPHIER_PIN_IECTRL_EXIST,
			     24, UNIPHIER_PIN_DRV_3BIT,
			     47, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(48, "SPITXD1", UNIPHIER_PIN_IECTRL_EXIST,
			     25, UNIPHIER_PIN_DRV_3BIT,
			     48, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(49, "SPIRXD1", UNIPHIER_PIN_IECTRL_EXIST,
			     26, UNIPHIER_PIN_DRV_3BIT,
			     49, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(50, "SDA0", UNIPHIER_PIN_IECTRL_EXIST,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(51, "SCL0", UNIPHIER_PIN_IECTRL_EXIST,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(52, "SDA1", UNIPHIER_PIN_IECTRL_EXIST,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(53, "SCL1", UNIPHIER_PIN_IECTRL_EXIST,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(54, "SDA2", UNIPHIER_PIN_IECTRL_EXIST,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(55, "SCL2", UNIPHIER_PIN_IECTRL_EXIST,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(56, "SDA3", UNIPHIER_PIN_IECTRL_EXIST,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(57, "SCL3", UNIPHIER_PIN_IECTRL_EXIST,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(58, "XIRQ0", UNIPHIER_PIN_IECTRL_EXIST,
			     27, UNIPHIER_PIN_DRV_3BIT,
			     58, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(59, "XIRQ1", UNIPHIER_PIN_IECTRL_EXIST,
			     28, UNIPHIER_PIN_DRV_3BIT,
			     59, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(60, "XIRQ2", UNIPHIER_PIN_IECTRL_EXIST,
			     29, UNIPHIER_PIN_DRV_3BIT,
			     60, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(61, "XIRQ3", UNIPHIER_PIN_IECTRL_EXIST,
			     30, UNIPHIER_PIN_DRV_3BIT,
			     61, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(62, "XIRQ4", UNIPHIER_PIN_IECTRL_EXIST,
			     31, UNIPHIER_PIN_DRV_3BIT,
			     62, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(63, "XIRQ5", UNIPHIER_PIN_IECTRL_EXIST,
			     32, UNIPHIER_PIN_DRV_3BIT,
			     63, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(64, "PORT00", UNIPHIER_PIN_IECTRL_EXIST,
			     33, UNIPHIER_PIN_DRV_3BIT,
			     64, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(65, "PORT01", UNIPHIER_PIN_IECTRL_EXIST,
			     34, UNIPHIER_PIN_DRV_3BIT,
			     65, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(66, "PORT02", UNIPHIER_PIN_IECTRL_EXIST,
			     35, UNIPHIER_PIN_DRV_3BIT,
			     66, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(67, "PORT03", UNIPHIER_PIN_IECTRL_EXIST,
			     36, UNIPHIER_PIN_DRV_3BIT,
			     67, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(68, "PORT04", UNIPHIER_PIN_IECTRL_EXIST,
			     37, UNIPHIER_PIN_DRV_3BIT,
			     68, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(69, "PORT05", UNIPHIER_PIN_IECTRL_EXIST,
			     38, UNIPHIER_PIN_DRV_3BIT,
			     69, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(70, "PORT06", UNIPHIER_PIN_IECTRL_EXIST,
			     39, UNIPHIER_PIN_DRV_3BIT,
			     70, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(71, "PORT07", UNIPHIER_PIN_IECTRL_EXIST,
			     40, UNIPHIER_PIN_DRV_3BIT,
			     71, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(72, "PORT10", UNIPHIER_PIN_IECTRL_EXIST,
			     41, UNIPHIER_PIN_DRV_3BIT,
			     72, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(73, "PORT11", UNIPHIER_PIN_IECTRL_EXIST,
			     42, UNIPHIER_PIN_DRV_3BIT,
			     73, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(74, "PORT12", UNIPHIER_PIN_IECTRL_EXIST,
			     43, UNIPHIER_PIN_DRV_3BIT,
			     74, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(75, "PORT13", UNIPHIER_PIN_IECTRL_EXIST,
			     44, UNIPHIER_PIN_DRV_3BIT,
			     75, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(76, "PORT14", UNIPHIER_PIN_IECTRL_EXIST,
			     45, UNIPHIER_PIN_DRV_3BIT,
			     76, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(77, "PORT15", UNIPHIER_PIN_IECTRL_EXIST,
			     46, UNIPHIER_PIN_DRV_3BIT,
			     77, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(78, "USBAVBUS", UNIPHIER_PIN_IECTRL_EXIST,
			     47, UNIPHIER_PIN_DRV_3BIT,
			     78, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(79, "USBAOD", UNIPHIER_PIN_IECTRL_EXIST,
			     48, UNIPHIER_PIN_DRV_3BIT,
			     79, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(80, "USBBVBUS", UNIPHIER_PIN_IECTRL_EXIST,
			     49, UNIPHIER_PIN_DRV_3BIT,
			     80, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(81, "USBBOD", UNIPHIER_PIN_IECTRL_EXIST,
			     50, UNIPHIER_PIN_DRV_3BIT,
			     81, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(82, "HTDDCSDA0", UNIPHIER_PIN_IECTRL_EXIST,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(83, "HTDDCSCL0", UNIPHIER_PIN_IECTRL_EXIST,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(84, "HTHPDI0", UNIPHIER_PIN_IECTRL_EXIST,
			     -1, UNIPHIER_PIN_DRV_FIXED4,
			     -1, UNIPHIER_PIN_PULL_NONE),
	UNIPHIER_PINCTRL_PIN(85, "MMCCLK", UNIPHIER_PIN_IECTRL_EXIST,
			     1, UNIPHIER_PIN_DRV_2BIT,
			     85, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(86, "MMCCMD", UNIPHIER_PIN_IECTRL_EXIST,
			     2, UNIPHIER_PIN_DRV_2BIT,
			     86, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(87, "MMCDS", UNIPHIER_PIN_IECTRL_EXIST,
			     3, UNIPHIER_PIN_DRV_2BIT,
			     87, UNIPHIER_PIN_PULL_DOWN),
	UNIPHIER_PINCTRL_PIN(88, "MMCDAT0", UNIPHIER_PIN_IECTRL_EXIST,
			     4, UNIPHIER_PIN_DRV_2BIT,
			     88, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(89, "MMCDAT1", UNIPHIER_PIN_IECTRL_EXIST,
			     5, UNIPHIER_PIN_DRV_2BIT,
			     89, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(90, "MMCDAT2", UNIPHIER_PIN_IECTRL_EXIST,
			     6, UNIPHIER_PIN_DRV_2BIT,
			     90, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(91, "MMCDAT3", UNIPHIER_PIN_IECTRL_EXIST,
			     7, UNIPHIER_PIN_DRV_2BIT,
			     91, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(92, "MMCDAT4", UNIPHIER_PIN_IECTRL_EXIST,
			     8, UNIPHIER_PIN_DRV_2BIT,
			     92, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(93, "MMCDAT5", UNIPHIER_PIN_IECTRL_EXIST,
			     9, UNIPHIER_PIN_DRV_2BIT,
			     93, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(94, "MMCDAT6", UNIPHIER_PIN_IECTRL_EXIST,
			     10, UNIPHIER_PIN_DRV_2BIT,
			     94, UNIPHIER_PIN_PULL_UP),
	UNIPHIER_PINCTRL_PIN(95, "MMCDAT7", UNIPHIER_PIN_IECTRL_EXIST,
			     11, UNIPHIER_PIN_DRV_2BIT,
			     95, UNIPHIER_PIN_PULL_UP),
};

static const unsigned int emmc_pins[] = {85, 86, 87, 88, 89, 90, 91};
static const int emmc_muxvals[] = {-1, -1, -1, -1, -1, -1, -1};
static const unsigned int emmc_dat8_pins[] = {92, 93, 94, 95};
static const int emmc_dat8_muxvals[] = {-1, -1, -1, -1};
static const unsigned int ether_rgmii_pins[] = {11, 12, 13, 14, 15, 16, 17, 18,
						19, 20, 21, 22, 23, 24, 25, 26};
static const int ether_rgmii_muxvals[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
					  0, 0, 0};
static const unsigned int ether_rmii_pins[] = {11, 12, 13, 14, 15, 16, 17, 18,
					       20, 22, 23, 26};
static const int ether_rmii_muxvals[] = {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1};
static const unsigned int i2c0_pins[] = {50, 51};
static const int i2c0_muxvals[] = {0, 0};
static const unsigned int i2c1_pins[] = {52, 53};
static const int i2c1_muxvals[] = {0, 0};
static const unsigned int i2c2_pins[] = {54, 55};
static const int i2c2_muxvals[] = {0, 0};
static const unsigned int i2c3_pins[] = {56, 57};
static const int i2c3_muxvals[] = {0, 0};
static const unsigned int i2c4_pins[] = {72, 73};
static const int i2c4_muxvals[] = {1, 1};
static const unsigned int i2c5_pins[] = {74, 75};
static const int i2c5_muxvals[] = {1, 1};
static const unsigned int i2c6_pins[] = {82, 83};
static const int i2c6_muxvals[] = {1, 1};
static const unsigned int sd_pins[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
static const int sd_muxvals[] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
static const unsigned spi0_pins[] = {42, 43, 44, 45};
static const int spi0_muxvals[] = {0, 0, 0, 0};
static const unsigned spi1_pins[] = {46, 47, 48, 49};
static const int spi1_muxvals[] = {0, 0, 0, 0};
static const unsigned int uart0_pins[] = {27, 28};
static const int uart0_muxvals[] = {0, 0};
static const unsigned int uart1_pins[] = {29, 30};
static const int uart1_muxvals[] = {0, 0};
static const unsigned int uart1_ctsrts_pins[] = {31, 33};
static const int uart1_ctsrts_muxvals[] = {0, 0};
static const unsigned int uart1_modem_pins[] = {32, 34, 35};
static const int uart1_modem_muxvals[] = {0, 0, 0};
static const unsigned int uart2_pins[] = {36, 37};
static const int uart2_muxvals[] = {0, 0};
static const unsigned int uart2_ctsrts_pins[] = {38, 39};
static const int uart2_ctsrts_muxvals[] = {0, 0};
static const unsigned int uart3_pins[] = {40, 41};
static const int uart3_muxvals[] = {0, 0};
static const unsigned int usb0_pins[] = {78, 79};
static const int usb0_muxvals[] = {0, 0};
static const unsigned int usb1_pins[] = {80, 81};
static const int usb1_muxvals[] = {0, 0};
static const unsigned int gpio_range0_pins[] = {
	64, 65, 66, 67, 68, 69, 70, 71,			/* PORT0x */
	72, 73, 74, 75, 76, 77, 0, 1,			/* PORT1x */
	2, 3, 4, 5, 6, 7, 8, 9,				/* PORT2x */
	10, 78, 79, 80, 81,				/* PORT30-34 */
};
static const unsigned int gpio_range1_pins[] = {
	11, 12, 13,					/* PORT61-63 */
};
static const unsigned int gpio_range2_pins[] = {
	15, 16, 17,					/* PORT65-67 */
	18, 19, 20, 21, 22, 23, 24, 25,			/* PORT7x */
	26, 27, 28, 29, 30, 31, 32, 33,			/* PORT8x */
	34, 35, 36, 37, 38, 39, 40, 41,			/* PORT9x */
	42, 43, 44, 45, 46, 47, 48, 49,			/* PORT10x */
};
static const unsigned int gpio_range3_pins[] = {
	58, 59, 60, 61, 62, 63,				/* PORT12x */
};
static const unsigned int gpio_range4_pins[] = {
	58, 59, 60, 61, 62, 63,				/* XIRQ0-5 */
};

static const struct uniphier_pinctrl_group uniphier_nx1_groups[] = {
	UNIPHIER_PINCTRL_GROUP(emmc),
	UNIPHIER_PINCTRL_GROUP(emmc_dat8),
	UNIPHIER_PINCTRL_GROUP(ether_rgmii),
	UNIPHIER_PINCTRL_GROUP(ether_rmii),
	UNIPHIER_PINCTRL_GROUP(i2c0),
	UNIPHIER_PINCTRL_GROUP(i2c1),
	UNIPHIER_PINCTRL_GROUP(i2c2),
	UNIPHIER_PINCTRL_GROUP(i2c3),
	UNIPHIER_PINCTRL_GROUP(i2c4),
	UNIPHIER_PINCTRL_GROUP(i2c5),
	UNIPHIER_PINCTRL_GROUP(i2c6),
	UNIPHIER_PINCTRL_GROUP(sd),
	UNIPHIER_PINCTRL_GROUP(spi0),
	UNIPHIER_PINCTRL_GROUP(spi1),
	UNIPHIER_PINCTRL_GROUP(uart0),
	UNIPHIER_PINCTRL_GROUP(uart1),
	UNIPHIER_PINCTRL_GROUP(uart1_ctsrts),
	UNIPHIER_PINCTRL_GROUP(uart1_modem),
	UNIPHIER_PINCTRL_GROUP(uart2),
	UNIPHIER_PINCTRL_GROUP(uart2_ctsrts),
	UNIPHIER_PINCTRL_GROUP(uart3),
	UNIPHIER_PINCTRL_GROUP(usb0),
	UNIPHIER_PINCTRL_GROUP(usb1),
	UNIPHIER_PINCTRL_GROUP_GPIO(gpio_range0),
	UNIPHIER_PINCTRL_GROUP_GPIO(gpio_range1),
	UNIPHIER_PINCTRL_GROUP_GPIO(gpio_range2),
	UNIPHIER_PINCTRL_GROUP_GPIO(gpio_range3),
	UNIPHIER_PINCTRL_GROUP_GPIO(gpio_range4),
};

static const char * const emmc_groups[] = {"emmc", "emmc_dat8"};
static const char * const ether_rgmii_groups[] = {"ether_rgmii"};
static const char * const ether_rmii_groups[] = {"ether_rmii"};
static const char * const i2c0_groups[] = {"i2c0"};
static const char * const i2c1_groups[] = {"i2c1"};
static const char * const i2c2_groups[] = {"i2c2"};
static const char * const i2c3_groups[] = {"i2c3"};
static const char * const i2c4_groups[] = {"i2c4"};
static const char * const i2c5_groups[] = {"i2c5"};
static const char * const i2c6_groups[] = {"i2c6"};
static const char * const sd_groups[] = {"sd"};
static const char * const spi0_groups[] = {"spi0"};
static const char * const spi1_groups[] = {"spi1"};
static const char * const uart0_groups[] = {"uart0"};
static const char * const uart1_groups[] = {"uart1", "uart1_ctsrts",
					    "uart1_modem"};
static const char * const uart2_groups[] = {"uart2", "uart2_ctsrts"};
static const char * const uart3_groups[] = {"uart3"};
static const char * const usb0_groups[] = {"usb0"};
static const char * const usb1_groups[] = {"usb1"};

static const struct uniphier_pinmux_function uniphier_nx1_functions[] = {
	UNIPHIER_PINMUX_FUNCTION(emmc),
	UNIPHIER_PINMUX_FUNCTION(ether_rgmii),
	UNIPHIER_PINMUX_FUNCTION(ether_rmii),
	UNIPHIER_PINMUX_FUNCTION(i2c0),
	UNIPHIER_PINMUX_FUNCTION(i2c1),
	UNIPHIER_PINMUX_FUNCTION(i2c2),
	UNIPHIER_PINMUX_FUNCTION(i2c3),
	UNIPHIER_PINMUX_FUNCTION(i2c4),
	UNIPHIER_PINMUX_FUNCTION(i2c5),
	UNIPHIER_PINMUX_FUNCTION(i2c6),
	UNIPHIER_PINMUX_FUNCTION(sd),
	UNIPHIER_PINMUX_FUNCTION(spi0),
	UNIPHIER_PINMUX_FUNCTION(spi1),
	UNIPHIER_PINMUX_FUNCTION(uart0),
	UNIPHIER_PINMUX_FUNCTION(uart1),
	UNIPHIER_PINMUX_FUNCTION(uart2),
	UNIPHIER_PINMUX_FUNCTION(uart3),
	UNIPHIER_PINMUX_FUNCTION(usb0),
	UNIPHIER_PINMUX_FUNCTION(usb1),
};

static int uniphier_nx1_get_gpio_muxval(unsigned int pin,
					unsigned int gpio_offset)
{
	if (gpio_offset >= 120)	/* XIRQx */
		return 14;

	return 15;
}

static const struct uniphier_pinctrl_socdata uniphier_nx1_pindata = {
	.pins = uniphier_nx1_pins,
	.npins = ARRAY_SIZE(uniphier_nx1_pins),
	.groups = uniphier_nx1_groups,
	.groups_count = ARRAY_SIZE(uniphier_nx1_groups),
	.functions = uniphier_nx1_functions,
	.functions_count = ARRAY_SIZE(uniphier_nx1_functions),
	.get_gpio_muxval = uniphier_nx1_get_gpio_muxval,
	.caps = UNIPHIER_PINCTRL_CAPS_PERPIN_IECTRL,
};

static int uniphier_nx1_pinctrl_probe(struct platform_device *pdev)
{
	return uniphier_pinctrl_probe(pdev, &uniphier_nx1_pindata);
}

static const struct of_device_id uniphier_nx1_pinctrl_match[] = {
	{ .compatible = "socionext,uniphier-nx1-pinctrl" },
	{ /* sentinel */ }
};

static struct platform_driver uniphier_nx1_pinctrl_driver = {
	.probe = uniphier_nx1_pinctrl_probe,
	.driver = {
		.name = "uniphier-nx1-pinctrl",
		.of_match_table = uniphier_nx1_pinctrl_match,
		.pm = &uniphier_pinctrl_pm_ops,
	},
};
builtin_platform_driver(uniphier_nx1_pinctrl_driver);

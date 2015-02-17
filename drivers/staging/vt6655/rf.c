/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
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
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 * File: rf.c
 *
 * Purpose: rf function code
 *
 * Author: Jerry Chen
 *
 * Date: Feb. 19, 2004
 *
 * Functions:
 *      IFRFbWriteEmbedded      - Embedded write RF register via MAC
 *
 * Revision History:
 *	RobertYu 2005
 *	chester 2008
 *
 */

#include "mac.h"
#include "srom.h"
#include "rf.h"
#include "baseband.h"

#define BY_AL2230_REG_LEN     23 //24bit
#define CB_AL2230_INIT_SEQ    15
#define SWITCH_CHANNEL_DELAY_AL2230 200 //us
#define AL2230_PWR_IDX_LEN    64

#define BY_AL7230_REG_LEN     23 //24bit
#define CB_AL7230_INIT_SEQ    16
#define SWITCH_CHANNEL_DELAY_AL7230 200 //us
#define AL7230_PWR_IDX_LEN    64

static const unsigned long dwAL2230InitTable[CB_AL2230_INIT_SEQ] = {
	0x03F79000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, //
	0x03333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, //
	0x01A00200+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, //
	0x00FFF300+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, //
	0x0005A400+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, //
	0x0F4DC500+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, //
	0x0805B600+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, //
	0x0146C700+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, //
	0x00068800+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, //
	0x0403B900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, //
	0x00DBBA00+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, //
	0x00099B00+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, //
	0x0BDFFC00+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x00000D00+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x00580F00+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW
};

static const unsigned long dwAL2230ChannelTable0[CB_MAX_CHANNEL] = {
	0x03F79000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 1, Tf = 2412MHz
	0x03F79000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 2, Tf = 2417MHz
	0x03E79000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 3, Tf = 2422MHz
	0x03E79000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 4, Tf = 2427MHz
	0x03F7A000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 5, Tf = 2432MHz
	0x03F7A000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 6, Tf = 2437MHz
	0x03E7A000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 7, Tf = 2442MHz
	0x03E7A000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 8, Tf = 2447MHz
	0x03F7B000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 9, Tf = 2452MHz
	0x03F7B000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 10, Tf = 2457MHz
	0x03E7B000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 11, Tf = 2462MHz
	0x03E7B000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 12, Tf = 2467MHz
	0x03F7C000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 13, Tf = 2472MHz
	0x03E7C000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW  // channel = 14, Tf = 2412M
};

static const unsigned long dwAL2230ChannelTable1[CB_MAX_CHANNEL] = {
	0x03333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 1, Tf = 2412MHz
	0x0B333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 2, Tf = 2417MHz
	0x03333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 3, Tf = 2422MHz
	0x0B333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 4, Tf = 2427MHz
	0x03333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 5, Tf = 2432MHz
	0x0B333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 6, Tf = 2437MHz
	0x03333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 7, Tf = 2442MHz
	0x0B333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 8, Tf = 2447MHz
	0x03333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 9, Tf = 2452MHz
	0x0B333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 10, Tf = 2457MHz
	0x03333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 11, Tf = 2462MHz
	0x0B333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 12, Tf = 2467MHz
	0x03333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 13, Tf = 2472MHz
	0x06666100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW  // channel = 14, Tf = 2412M
};

static unsigned long dwAL2230PowerTable[AL2230_PWR_IDX_LEN] = {
	0x04040900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04041900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04042900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04043900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04044900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04045900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04046900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04047900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04048900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04049900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x0404A900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x0404B900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x0404C900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x0404D900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x0404E900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x0404F900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04050900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04051900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04052900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04053900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04054900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04055900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04056900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04057900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04058900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04059900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x0405A900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x0405B900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x0405C900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x0405D900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x0405E900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x0405F900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04060900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04061900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04062900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04063900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04064900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04065900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04066900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04067900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04068900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04069900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x0406A900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x0406B900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x0406C900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x0406D900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x0406E900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x0406F900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04070900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04071900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04072900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04073900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04074900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04075900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04076900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04077900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04078900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x04079900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x0407A900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x0407B900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x0407C900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x0407D900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x0407E900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
	0x0407F900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW
};

// 40MHz reference frequency
// Need to Pull PLLON(PE3) low when writing channel registers through 3-wire.
static const unsigned long dwAL7230InitTable[CB_AL7230_INIT_SEQ] = {
	0x00379000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // Channel1 // Need modify for 11a
	0x13333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // Channel1 // Need modify for 11a
	0x841FF200+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // Need modify for 11a: 451FE2
	0x3FDFA300+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // Need modify for 11a: 5FDFA3
	0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // 11b/g    // Need modify for 11a
	// RoberYu:20050113, Rev0.47 Regsiter Setting Guide
	0x802B5500+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // Need modify for 11a: 8D1B55
	0x56AF3600+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW,
	0xCE020700+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // Need modify for 11a: 860207
	0x6EBC0800+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW,
	0x221BB900+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW,
	0xE0000A00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // Need modify for 11a: E0600A
	0x08031B00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // init 0x080B1B00 => 0x080F1B00 for 3 wire control TxGain(D10)
	// RoberYu:20050113, Rev0.47 Regsiter Setting Guide
	0x000A3C00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // Need modify for 11a: 00143C
	0xFFFFFD00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW,
	0x00000E00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW,
	0x1ABA8F00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW  // Need modify for 11a: 12BACF
};

static const unsigned long dwAL7230InitTableAMode[CB_AL7230_INIT_SEQ] = {
	0x0FF52000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // Channel184 // Need modify for 11b/g
	0x00000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // Channel184 // Need modify for 11b/g
	0x451FE200+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // Need modify for 11b/g
	0x5FDFA300+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // Need modify for 11b/g
	0x67F78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // 11a    // Need modify for 11b/g
	0x853F5500+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // Need modify for 11b/g, RoberYu:20050113
	0x56AF3600+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW,
	0xCE020700+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // Need modify for 11b/g
	0x6EBC0800+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW,
	0x221BB900+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW,
	0xE0600A00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // Need modify for 11b/g
	0x08031B00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // init 0x080B1B00 => 0x080F1B00 for 3 wire control TxGain(D10)
	0x00147C00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // Need modify for 11b/g
	0xFFFFFD00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW,
	0x00000E00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW,
	0x12BACF00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW  // Need modify for 11b/g
};

static const unsigned long dwAL7230ChannelTable0[CB_MAX_CHANNEL] = {
	0x00379000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  1, Tf = 2412MHz
	0x00379000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  2, Tf = 2417MHz
	0x00379000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  3, Tf = 2422MHz
	0x00379000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  4, Tf = 2427MHz
	0x0037A000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  5, Tf = 2432MHz
	0x0037A000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  6, Tf = 2437MHz
	0x0037A000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  7, Tf = 2442MHz
	0x0037A000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  8, Tf = 2447MHz //RobertYu: 20050218, update for APNode 0.49
	0x0037B000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  9, Tf = 2452MHz //RobertYu: 20050218, update for APNode 0.49
	0x0037B000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 10, Tf = 2457MHz //RobertYu: 20050218, update for APNode 0.49
	0x0037B000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 11, Tf = 2462MHz //RobertYu: 20050218, update for APNode 0.49
	0x0037B000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 12, Tf = 2467MHz //RobertYu: 20050218, update for APNode 0.49
	0x0037C000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 13, Tf = 2472MHz //RobertYu: 20050218, update for APNode 0.49
	0x0037C000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 14, Tf = 2484MHz

	// 4.9G => Ch 183, 184, 185, 187, 188, 189, 192, 196  (Value:15 ~ 22)
	0x0FF52000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 183, Tf = 4915MHz (15)
	0x0FF52000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 184, Tf = 4920MHz (16)
	0x0FF52000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 185, Tf = 4925MHz (17)
	0x0FF52000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 187, Tf = 4935MHz (18)
	0x0FF52000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 188, Tf = 4940MHz (19)
	0x0FF52000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 189, Tf = 4945MHz (20)
	0x0FF53000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 192, Tf = 4960MHz (21)
	0x0FF53000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 196, Tf = 4980MHz (22)

	// 5G => Ch 7, 8, 9, 11, 12, 16, 34, 36, 38, 40, 42, 44, 46, 48, 52, 56, 60, 64,
	// 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165  (Value 23 ~ 56)

	0x0FF54000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =   7, Tf = 5035MHz (23)
	0x0FF54000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =   8, Tf = 5040MHz (24)
	0x0FF54000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =   9, Tf = 5045MHz (25)
	0x0FF54000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  11, Tf = 5055MHz (26)
	0x0FF54000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  12, Tf = 5060MHz (27)
	0x0FF55000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  16, Tf = 5080MHz (28)
	0x0FF56000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  34, Tf = 5170MHz (29)
	0x0FF56000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  36, Tf = 5180MHz (30)
	0x0FF57000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  38, Tf = 5190MHz (31) //RobertYu: 20050218, update for APNode 0.49
	0x0FF57000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  40, Tf = 5200MHz (32)
	0x0FF57000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  42, Tf = 5210MHz (33)
	0x0FF57000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  44, Tf = 5220MHz (34)
	0x0FF57000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  46, Tf = 5230MHz (35)
	0x0FF57000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  48, Tf = 5240MHz (36)
	0x0FF58000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  52, Tf = 5260MHz (37)
	0x0FF58000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  56, Tf = 5280MHz (38)
	0x0FF58000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  60, Tf = 5300MHz (39)
	0x0FF59000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  64, Tf = 5320MHz (40)

	0x0FF5C000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 100, Tf = 5500MHz (41)
	0x0FF5C000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 104, Tf = 5520MHz (42)
	0x0FF5C000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 108, Tf = 5540MHz (43)
	0x0FF5D000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 112, Tf = 5560MHz (44)
	0x0FF5D000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 116, Tf = 5580MHz (45)
	0x0FF5D000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 120, Tf = 5600MHz (46)
	0x0FF5E000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 124, Tf = 5620MHz (47)
	0x0FF5E000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 128, Tf = 5640MHz (48)
	0x0FF5E000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 132, Tf = 5660MHz (49)
	0x0FF5F000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 136, Tf = 5680MHz (50)
	0x0FF5F000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 140, Tf = 5700MHz (51)
	0x0FF60000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 149, Tf = 5745MHz (52)
	0x0FF60000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 153, Tf = 5765MHz (53)
	0x0FF60000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 157, Tf = 5785MHz (54)
	0x0FF61000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 161, Tf = 5805MHz (55)
	0x0FF61000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW  // channel = 165, Tf = 5825MHz (56)
};

static const unsigned long dwAL7230ChannelTable1[CB_MAX_CHANNEL] = {
	0x13333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  1, Tf = 2412MHz
	0x1B333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  2, Tf = 2417MHz
	0x03333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  3, Tf = 2422MHz
	0x0B333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  4, Tf = 2427MHz
	0x13333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  5, Tf = 2432MHz
	0x1B333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  6, Tf = 2437MHz
	0x03333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  7, Tf = 2442MHz
	0x0B333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  8, Tf = 2447MHz
	0x13333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  9, Tf = 2452MHz
	0x1B333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 10, Tf = 2457MHz
	0x03333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 11, Tf = 2462MHz
	0x0B333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 12, Tf = 2467MHz
	0x13333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 13, Tf = 2472MHz
	0x06666100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 14, Tf = 2484MHz

	// 4.9G => Ch 183, 184, 185, 187, 188, 189, 192, 196  (Value:15 ~ 22)
	0x1D555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 183, Tf = 4915MHz (15)
	0x00000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 184, Tf = 4920MHz (16)
	0x02AAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 185, Tf = 4925MHz (17)
	0x08000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 187, Tf = 4935MHz (18)
	0x0AAAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 188, Tf = 4940MHz (19)
	0x0D555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 189, Tf = 4945MHz (20)
	0x15555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 192, Tf = 4960MHz (21)
	0x00000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 196, Tf = 4980MHz (22)

	// 5G => Ch 7, 8, 9, 11, 12, 16, 34, 36, 38, 40, 42, 44, 46, 48, 52, 56, 60, 64,
	// 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165  (Value 23 ~ 56)
	0x1D555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =   7, Tf = 5035MHz (23)
	0x00000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =   8, Tf = 5040MHz (24)
	0x02AAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =   9, Tf = 5045MHz (25)
	0x08000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  11, Tf = 5055MHz (26)
	0x0AAAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  12, Tf = 5060MHz (27)
	0x15555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  16, Tf = 5080MHz (28)
	0x05555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  34, Tf = 5170MHz (29)
	0x0AAAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  36, Tf = 5180MHz (30)
	0x10000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  38, Tf = 5190MHz (31)
	0x15555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  40, Tf = 5200MHz (32)
	0x1AAAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  42, Tf = 5210MHz (33)
	0x00000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  44, Tf = 5220MHz (34)
	0x05555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  46, Tf = 5230MHz (35)
	0x0AAAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  48, Tf = 5240MHz (36)
	0x15555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  52, Tf = 5260MHz (37)
	0x00000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  56, Tf = 5280MHz (38)
	0x0AAAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  60, Tf = 5300MHz (39)
	0x15555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  64, Tf = 5320MHz (40)
	0x15555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 100, Tf = 5500MHz (41)
	0x00000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 104, Tf = 5520MHz (42)
	0x0AAAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 108, Tf = 5540MHz (43)
	0x15555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 112, Tf = 5560MHz (44)
	0x00000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 116, Tf = 5580MHz (45)
	0x0AAAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 120, Tf = 5600MHz (46)
	0x15555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 124, Tf = 5620MHz (47)
	0x00000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 128, Tf = 5640MHz (48)
	0x0AAAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 132, Tf = 5660MHz (49)
	0x15555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 136, Tf = 5680MHz (50)
	0x00000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 140, Tf = 5700MHz (51)
	0x18000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 149, Tf = 5745MHz (52)
	0x02AAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 153, Tf = 5765MHz (53)
	0x0D555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 157, Tf = 5785MHz (54)
	0x18000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 161, Tf = 5805MHz (55)
	0x02AAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW  // channel = 165, Tf = 5825MHz (56)
};

static const unsigned long dwAL7230ChannelTable2[CB_MAX_CHANNEL] = {
	0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  1, Tf = 2412MHz
	0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  2, Tf = 2417MHz
	0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  3, Tf = 2422MHz
	0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  4, Tf = 2427MHz
	0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  5, Tf = 2432MHz
	0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  6, Tf = 2437MHz
	0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  7, Tf = 2442MHz
	0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  8, Tf = 2447MHz
	0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  9, Tf = 2452MHz
	0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 10, Tf = 2457MHz
	0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 11, Tf = 2462MHz
	0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 12, Tf = 2467MHz
	0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 13, Tf = 2472MHz
	0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 14, Tf = 2484MHz

	// 4.9G => Ch 183, 184, 185, 187, 188, 189, 192, 196  (Value:15 ~ 22)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 183, Tf = 4915MHz (15)
	0x67D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 184, Tf = 4920MHz (16)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 185, Tf = 4925MHz (17)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 187, Tf = 4935MHz (18)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 188, Tf = 4940MHz (19)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 189, Tf = 4945MHz (20)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 192, Tf = 4960MHz (21)
	0x67D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 196, Tf = 4980MHz (22)

	// 5G => Ch 7, 8, 9, 11, 12, 16, 34, 36, 38, 40, 42, 44, 46, 48, 52, 56, 60, 64,
	// 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165  (Value 23 ~ 56)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =   7, Tf = 5035MHz (23)
	0x67D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =   8, Tf = 5040MHz (24)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =   9, Tf = 5045MHz (25)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  11, Tf = 5055MHz (26)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  12, Tf = 5060MHz (27)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  16, Tf = 5080MHz (28)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  34, Tf = 5170MHz (29)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  36, Tf = 5180MHz (30)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  38, Tf = 5190MHz (31)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  40, Tf = 5200MHz (32)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  42, Tf = 5210MHz (33)
	0x67D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  44, Tf = 5220MHz (34)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  46, Tf = 5230MHz (35)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  48, Tf = 5240MHz (36)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  52, Tf = 5260MHz (37)
	0x67D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  56, Tf = 5280MHz (38)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  60, Tf = 5300MHz (39)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel =  64, Tf = 5320MHz (40)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 100, Tf = 5500MHz (41)
	0x67D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 104, Tf = 5520MHz (42)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 108, Tf = 5540MHz (43)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 112, Tf = 5560MHz (44)
	0x67D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 116, Tf = 5580MHz (45)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 120, Tf = 5600MHz (46)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 124, Tf = 5620MHz (47)
	0x67D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 128, Tf = 5640MHz (48)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 132, Tf = 5660MHz (49)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 136, Tf = 5680MHz (50)
	0x67D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 140, Tf = 5700MHz (51)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 149, Tf = 5745MHz (52)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 153, Tf = 5765MHz (53)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 157, Tf = 5785MHz (54)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // channel = 161, Tf = 5805MHz (55)
	0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW  // channel = 165, Tf = 5825MHz (56)
};

/*
 * Description: AIROHA IFRF chip init function
 *
 * Parameters:
 *  In:
 *      dwIoBase    - I/O base address
 *  Out:
 *      none
 *
 * Return Value: true if succeeded; false if failed.
 *
 */
static bool s_bAL7230Init(struct vnt_private *priv)
{
	void __iomem *dwIoBase = priv->PortOffset;
	int     ii;
	bool bResult;

	bResult = true;

	/* 3-wire control for normal mode */
	VNSvOutPortB(dwIoBase + MAC_REG_SOFTPWRCTL, 0);

	MACvWordRegBitsOn(dwIoBase, MAC_REG_SOFTPWRCTL, (SOFTPWRCTL_SWPECTI  |
							 SOFTPWRCTL_TXPEINV));
	BBvPowerSaveModeOFF(priv); /* RobertYu:20050106, have DC value for Calibration */

	for (ii = 0; ii < CB_AL7230_INIT_SEQ; ii++)
		bResult &= IFRFbWriteEmbedded(priv, dwAL7230InitTable[ii]);

	/* PLL On */
	MACvWordRegBitsOn(dwIoBase, MAC_REG_SOFTPWRCTL, SOFTPWRCTL_SWPE3);

	/* Calibration */
	MACvTimer0MicroSDelay(dwIoBase, 150);//150us
	/* TXDCOC:active, RCK:disable */
	bResult &= IFRFbWriteEmbedded(priv, (0x9ABA8F00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW));
	MACvTimer0MicroSDelay(dwIoBase, 30);//30us
	/* TXDCOC:disable, RCK:active */
	bResult &= IFRFbWriteEmbedded(priv, (0x3ABA8F00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW));
	MACvTimer0MicroSDelay(dwIoBase, 30);//30us
	/* TXDCOC:disable, RCK:disable */
	bResult &= IFRFbWriteEmbedded(priv, dwAL7230InitTable[CB_AL7230_INIT_SEQ-1]);

	MACvWordRegBitsOn(dwIoBase, MAC_REG_SOFTPWRCTL, (SOFTPWRCTL_SWPE3    |
							 SOFTPWRCTL_SWPE2    |
							 SOFTPWRCTL_SWPECTI  |
							 SOFTPWRCTL_TXPEINV));

	BBvPowerSaveModeON(priv); /* RobertYu:20050106 */

	/* PE1: TX_ON, PE2: RX_ON, PE3: PLLON */
	/* 3-wire control for power saving mode */
	VNSvOutPortB(dwIoBase + MAC_REG_PSPWRSIG, (PSSIG_WPE3 | PSSIG_WPE2)); //1100 0000

	return bResult;
}

/* Need to Pull PLLON low when writing channel registers through
 * 3-wire interface */
static bool s_bAL7230SelectChannel(struct vnt_private *priv, unsigned char byChannel)
{
	void __iomem *dwIoBase = priv->PortOffset;
	bool bResult;

	bResult = true;

	/* PLLON Off */
	MACvWordRegBitsOff(dwIoBase, MAC_REG_SOFTPWRCTL, SOFTPWRCTL_SWPE3);

	bResult &= IFRFbWriteEmbedded(priv, dwAL7230ChannelTable0[byChannel - 1]);
	bResult &= IFRFbWriteEmbedded(priv, dwAL7230ChannelTable1[byChannel - 1]);
	bResult &= IFRFbWriteEmbedded(priv, dwAL7230ChannelTable2[byChannel - 1]);

	/* PLLOn On */
	MACvWordRegBitsOn(dwIoBase, MAC_REG_SOFTPWRCTL, SOFTPWRCTL_SWPE3);

	/* Set Channel[7] = 0 to tell H/W channel is changing now. */
	VNSvOutPortB(dwIoBase + MAC_REG_CHANNEL, (byChannel & 0x7F));
	MACvTimer0MicroSDelay(dwIoBase, SWITCH_CHANNEL_DELAY_AL7230);
	/* Set Channel[7] = 1 to tell H/W channel change is done. */
	VNSvOutPortB(dwIoBase + MAC_REG_CHANNEL, (byChannel | 0x80));

	return bResult;
}

/*
 * Description: Write to IF/RF, by embedded programming
 *
 * Parameters:
 *  In:
 *      dwIoBase    - I/O base address
 *      dwData      - data to write
 *  Out:
 *      none
 *
 * Return Value: true if succeeded; false if failed.
 *
 */
bool IFRFbWriteEmbedded(struct vnt_private *priv, unsigned long dwData)
{
	void __iomem *dwIoBase = priv->PortOffset;
	unsigned short ww;
	unsigned long dwValue;

	VNSvOutPortD(dwIoBase + MAC_REG_IFREGCTL, dwData);

	/* W_MAX_TIMEOUT is the timeout period */
	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		VNSvInPortD(dwIoBase + MAC_REG_IFREGCTL, &dwValue);
		if (dwValue & IFREGCTL_DONE)
			break;
	}

	if (ww == W_MAX_TIMEOUT)
		return false;

	return true;
}

/*
 * Description: AIROHA IFRF chip init function
 *
 * Parameters:
 *  In:
 *      dwIoBase    - I/O base address
 *  Out:
 *      none
 *
 * Return Value: true if succeeded; false if failed.
 *
 */
static bool RFbAL2230Init(struct vnt_private *priv)
{
	void __iomem *dwIoBase = priv->PortOffset;
	int     ii;
	bool bResult;

	bResult = true;

	/* 3-wire control for normal mode */
	VNSvOutPortB(dwIoBase + MAC_REG_SOFTPWRCTL, 0);

	MACvWordRegBitsOn(dwIoBase, MAC_REG_SOFTPWRCTL, (SOFTPWRCTL_SWPECTI  |
							 SOFTPWRCTL_TXPEINV));
	/* PLL  Off */
	MACvWordRegBitsOff(dwIoBase, MAC_REG_SOFTPWRCTL, SOFTPWRCTL_SWPE3);

	/* patch abnormal AL2230 frequency output */
	IFRFbWriteEmbedded(priv, (0x07168700+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW));

	for (ii = 0; ii < CB_AL2230_INIT_SEQ; ii++)
		bResult &= IFRFbWriteEmbedded(priv, dwAL2230InitTable[ii]);
	MACvTimer0MicroSDelay(dwIoBase, 30); //delay 30 us

	/* PLL On */
	MACvWordRegBitsOn(dwIoBase, MAC_REG_SOFTPWRCTL, SOFTPWRCTL_SWPE3);

	MACvTimer0MicroSDelay(dwIoBase, 150);//150us
	bResult &= IFRFbWriteEmbedded(priv, (0x00d80f00+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW));
	MACvTimer0MicroSDelay(dwIoBase, 30);//30us
	bResult &= IFRFbWriteEmbedded(priv, (0x00780f00+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW));
	MACvTimer0MicroSDelay(dwIoBase, 30);//30us
	bResult &= IFRFbWriteEmbedded(priv, dwAL2230InitTable[CB_AL2230_INIT_SEQ-1]);

	MACvWordRegBitsOn(dwIoBase, MAC_REG_SOFTPWRCTL, (SOFTPWRCTL_SWPE3    |
							 SOFTPWRCTL_SWPE2    |
							 SOFTPWRCTL_SWPECTI  |
							 SOFTPWRCTL_TXPEINV));

	/* 3-wire control for power saving mode */
	VNSvOutPortB(dwIoBase + MAC_REG_PSPWRSIG, (PSSIG_WPE3 | PSSIG_WPE2)); //1100 0000

	return bResult;
}

static bool RFbAL2230SelectChannel(struct vnt_private *priv, unsigned char byChannel)
{
	void __iomem *dwIoBase = priv->PortOffset;
	bool bResult;

	bResult = true;

	bResult &= IFRFbWriteEmbedded(priv, dwAL2230ChannelTable0[byChannel - 1]);
	bResult &= IFRFbWriteEmbedded(priv, dwAL2230ChannelTable1[byChannel - 1]);

	/* Set Channel[7] = 0 to tell H/W channel is changing now. */
	VNSvOutPortB(dwIoBase + MAC_REG_CHANNEL, (byChannel & 0x7F));
	MACvTimer0MicroSDelay(dwIoBase, SWITCH_CHANNEL_DELAY_AL2230);
	/* Set Channel[7] = 1 to tell H/W channel change is done. */
	VNSvOutPortB(dwIoBase + MAC_REG_CHANNEL, (byChannel | 0x80));

	return bResult;
}

/*
 * Description: RF init function
 *
 * Parameters:
 *  In:
 *      byBBType
 *      byRFType
 *  Out:
 *      none
 *
 * Return Value: true if succeeded; false if failed.
 *
 */
bool RFbInit(
	struct vnt_private *priv
)
{
	bool bResult = true;

	switch (priv->byRFType) {
	case RF_AIROHA:
	case RF_AL2230S:
		priv->byMaxPwrLevel = AL2230_PWR_IDX_LEN;
		bResult = RFbAL2230Init(priv);
		break;
	case RF_AIROHA7230:
		priv->byMaxPwrLevel = AL7230_PWR_IDX_LEN;
		bResult = s_bAL7230Init(priv);
		break;
	case RF_NOTHING:
		bResult = true;
		break;
	default:
		bResult = false;
		break;
	}
	return bResult;
}

/*
 * Description: Select channel
 *
 * Parameters:
 *  In:
 *      byRFType
 *      byChannel    - Channel number
 *  Out:
 *      none
 *
 * Return Value: true if succeeded; false if failed.
 *
 */
bool RFbSelectChannel(struct vnt_private *priv, unsigned char byRFType,
		      u16 byChannel)
{
	bool bResult = true;

	switch (byRFType) {
	case RF_AIROHA:
	case RF_AL2230S:
		bResult = RFbAL2230SelectChannel(priv, byChannel);
		break;
		//{{ RobertYu: 20050104
	case RF_AIROHA7230:
		bResult = s_bAL7230SelectChannel(priv, byChannel);
		break;
		//}} RobertYu
	case RF_NOTHING:
		bResult = true;
		break;
	default:
		bResult = false;
		break;
	}
	return bResult;
}

/*
 * Description: Write WakeProgSyn
 *
 * Parameters:
 *  In:
 *      dwIoBase    - I/O base address
 *      uChannel    - channel number
 *      bySleepCnt  - SleepProgSyn count
 *
 * Return Value: None.
 *
 */
bool RFvWriteWakeProgSyn(struct vnt_private *priv, unsigned char byRFType,
			 u16 uChannel)
{
	void __iomem *dwIoBase = priv->PortOffset;
	int   ii;
	unsigned char byInitCount = 0;
	unsigned char bySleepCount = 0;

	VNSvOutPortW(dwIoBase + MAC_REG_MISCFFNDEX, 0);
	switch (byRFType) {
	case RF_AIROHA:
	case RF_AL2230S:

		if (uChannel > CB_MAX_CHANNEL_24G)
			return false;

		 /* Init Reg + Channel Reg (2) */
		byInitCount = CB_AL2230_INIT_SEQ + 2;
		bySleepCount = 0;
		if (byInitCount > (MISCFIFO_SYNDATASIZE - bySleepCount))
			return false;

		for (ii = 0; ii < CB_AL2230_INIT_SEQ; ii++)
			MACvSetMISCFifo(dwIoBase, (unsigned short)(MISCFIFO_SYNDATA_IDX + ii), dwAL2230InitTable[ii]);

		MACvSetMISCFifo(dwIoBase, (unsigned short)(MISCFIFO_SYNDATA_IDX + ii), dwAL2230ChannelTable0[uChannel-1]);
		ii++;
		MACvSetMISCFifo(dwIoBase, (unsigned short)(MISCFIFO_SYNDATA_IDX + ii), dwAL2230ChannelTable1[uChannel-1]);
		break;

		/* Need to check, PLLON need to be low for channel setting */
	case RF_AIROHA7230:
		 /* Init Reg + Channel Reg (3) */
		byInitCount = CB_AL7230_INIT_SEQ + 3;
		bySleepCount = 0;
		if (byInitCount > (MISCFIFO_SYNDATASIZE - bySleepCount))
			return false;

		if (uChannel <= CB_MAX_CHANNEL_24G) {
			for (ii = 0; ii < CB_AL7230_INIT_SEQ; ii++)
				MACvSetMISCFifo(dwIoBase, (unsigned short)(MISCFIFO_SYNDATA_IDX + ii), dwAL7230InitTable[ii]);
		} else {
			for (ii = 0; ii < CB_AL7230_INIT_SEQ; ii++)
				MACvSetMISCFifo(dwIoBase, (unsigned short)(MISCFIFO_SYNDATA_IDX + ii), dwAL7230InitTableAMode[ii]);
		}

		MACvSetMISCFifo(dwIoBase, (unsigned short)(MISCFIFO_SYNDATA_IDX + ii), dwAL7230ChannelTable0[uChannel-1]);
		ii++;
		MACvSetMISCFifo(dwIoBase, (unsigned short)(MISCFIFO_SYNDATA_IDX + ii), dwAL7230ChannelTable1[uChannel-1]);
		ii++;
		MACvSetMISCFifo(dwIoBase, (unsigned short)(MISCFIFO_SYNDATA_IDX + ii), dwAL7230ChannelTable2[uChannel-1]);
		break;

	case RF_NOTHING:
		return true;

	default:
		return false;
	}

	MACvSetMISCFifo(dwIoBase, MISCFIFO_SYNINFO_IDX, (unsigned long)MAKEWORD(bySleepCount, byInitCount));

	return true;
}

/*
 * Description: Set Tx power
 *
 * Parameters:
 *  In:
 *      dwIoBase       - I/O base address
 *      dwRFPowerTable - RF Tx Power Setting
 *  Out:
 *      none
 *
 * Return Value: true if succeeded; false if failed.
 *
 */
bool RFbSetPower(
	struct vnt_private *priv,
	unsigned int uRATE,
	u16 uCH
)
{
	bool bResult = true;
	unsigned char byPwr = 0;
	unsigned char byDec = 0;

	if (priv->dwDiagRefCount != 0)
		return true;

	if ((uCH < 1) || (uCH > CB_MAX_CHANNEL))
		return false;

	switch (uRATE) {
	case RATE_1M:
	case RATE_2M:
	case RATE_5M:
	case RATE_11M:
		if (uCH > CB_MAX_CHANNEL_24G)
			return false;

		byPwr = priv->abyCCKPwrTbl[uCH];
		break;
	case RATE_6M:
	case RATE_9M:
	case RATE_18M:
		byPwr = priv->abyOFDMPwrTbl[uCH];
		if (priv->byRFType == RF_UW2452)
			byDec = byPwr + 14;
		else
			byDec = byPwr + 10;

		if (byDec >= priv->byMaxPwrLevel)
			byDec = priv->byMaxPwrLevel-1;

		byPwr = byDec;
		break;
	case RATE_24M:
	case RATE_36M:
	case RATE_48M:
	case RATE_54M:
		byPwr = priv->abyOFDMPwrTbl[uCH];
		break;
	}

	if (priv->byCurPwr == byPwr)
		return true;

	bResult = RFbRawSetPower(priv, byPwr, uRATE);
	if (bResult)
		priv->byCurPwr = byPwr;

	return bResult;
}

/*
 * Description: Set Tx power
 *
 * Parameters:
 *  In:
 *      dwIoBase       - I/O base address
 *      dwRFPowerTable - RF Tx Power Setting
 *  Out:
 *      none
 *
 * Return Value: true if succeeded; false if failed.
 *
 */

bool RFbRawSetPower(
	struct vnt_private *priv,
	unsigned char byPwr,
	unsigned int uRATE
)
{
	bool bResult = true;
	unsigned long dwMax7230Pwr = 0;

	if (byPwr >=  priv->byMaxPwrLevel)
		return false;

	switch (priv->byRFType) {
	case RF_AIROHA:
		bResult &= IFRFbWriteEmbedded(priv, dwAL2230PowerTable[byPwr]);
		if (uRATE <= RATE_11M)
			bResult &= IFRFbWriteEmbedded(priv, 0x0001B400+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW);
		else
			bResult &= IFRFbWriteEmbedded(priv, 0x0005A400+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW);

		break;

	case RF_AL2230S:
		bResult &= IFRFbWriteEmbedded(priv, dwAL2230PowerTable[byPwr]);
		if (uRATE <= RATE_11M) {
			bResult &= IFRFbWriteEmbedded(priv, 0x040C1400+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW);
			bResult &= IFRFbWriteEmbedded(priv, 0x00299B00+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW);
		} else {
			bResult &= IFRFbWriteEmbedded(priv, 0x0005A400+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW);
			bResult &= IFRFbWriteEmbedded(priv, 0x00099B00+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW);
		}

		break;

	case RF_AIROHA7230:
		/* 0x080F1B00 for 3 wire control TxGain(D10)
		 * and 0x31 as TX Gain value */
		dwMax7230Pwr = 0x080C0B00 | ((byPwr) << 12) |
			(BY_AL7230_REG_LEN << 3)  | IFREGCTL_REGW;

		bResult &= IFRFbWriteEmbedded(priv, dwMax7230Pwr);
		break;

	default:
		break;
	}
	return bResult;
}

/*+
 *
 * Routine Description:
 *     Translate RSSI to dBm
 *
 * Parameters:
 *  In:
 *      priv         - The adapter to be translated
 *      byCurrRSSI      - RSSI to be translated
 *  Out:
 *      pdwdbm          - Translated dbm number
 *
 * Return Value: none
 *
 -*/
void
RFvRSSITodBm(
	struct vnt_private *priv,
	unsigned char byCurrRSSI,
	long *pldBm
	)
{
	unsigned char byIdx = (((byCurrRSSI & 0xC0) >> 6) & 0x03);
	long b = (byCurrRSSI & 0x3F);
	long a = 0;
	unsigned char abyAIROHARF[4] = {0, 18, 0, 40};

	switch (priv->byRFType) {
	case RF_AIROHA:
	case RF_AL2230S:
	case RF_AIROHA7230:
		a = abyAIROHARF[byIdx];
		break;
	default:
		break;
	}

	*pldBm = -1 * (a + b * 2);
}

/* Post processing for the 11b/g and 11a.
 * for save time on changing Reg2,3,5,7,10,12,15 */
bool RFbAL7230SelectChannelPostProcess(struct vnt_private *priv,
				       u16 byOldChannel,
				       u16 byNewChannel)
{
	bool bResult;

	bResult = true;

	/* if change between 11 b/g and 11a need to update the following
	 * register
	 * Channel Index 1~14 */
	if ((byOldChannel <= CB_MAX_CHANNEL_24G) && (byNewChannel > CB_MAX_CHANNEL_24G)) {
		/* Change from 2.4G to 5G [Reg] */
		bResult &= IFRFbWriteEmbedded(priv, dwAL7230InitTableAMode[2]);
		bResult &= IFRFbWriteEmbedded(priv, dwAL7230InitTableAMode[3]);
		bResult &= IFRFbWriteEmbedded(priv, dwAL7230InitTableAMode[5]);
		bResult &= IFRFbWriteEmbedded(priv, dwAL7230InitTableAMode[7]);
		bResult &= IFRFbWriteEmbedded(priv, dwAL7230InitTableAMode[10]);
		bResult &= IFRFbWriteEmbedded(priv, dwAL7230InitTableAMode[12]);
		bResult &= IFRFbWriteEmbedded(priv, dwAL7230InitTableAMode[15]);
	} else if ((byOldChannel > CB_MAX_CHANNEL_24G) && (byNewChannel <= CB_MAX_CHANNEL_24G)) {
		/* Change from 5G to 2.4G [Reg] */
		bResult &= IFRFbWriteEmbedded(priv, dwAL7230InitTable[2]);
		bResult &= IFRFbWriteEmbedded(priv, dwAL7230InitTable[3]);
		bResult &= IFRFbWriteEmbedded(priv, dwAL7230InitTable[5]);
		bResult &= IFRFbWriteEmbedded(priv, dwAL7230InitTable[7]);
		bResult &= IFRFbWriteEmbedded(priv, dwAL7230InitTable[10]);
		bResult &= IFRFbWriteEmbedded(priv, dwAL7230InitTable[12]);
		bResult &= IFRFbWriteEmbedded(priv, dwAL7230InitTable[15]);
	}

	return bResult;
}

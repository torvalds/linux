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
 *
 */

#include "mac.h"
#include "rf.h"
#include "baseband.h"
#include "control.h"
#include "rndis.h"
#include "datarate.h"

static int          msglevel                =MSG_LEVEL_INFO;
//static int          msglevel                =MSG_LEVEL_DEBUG;
/*---------------------  Static Definitions -------------------------*/
#define BY_AL2230_REG_LEN     23 //24bit
#define CB_AL2230_INIT_SEQ    15
#define AL2230_PWR_IDX_LEN    64

#define BY_AL7230_REG_LEN     23 //24bit
#define CB_AL7230_INIT_SEQ    16
#define AL7230_PWR_IDX_LEN    64

//{{RobertYu:20051111
#define BY_VT3226_REG_LEN     23
#define CB_VT3226_INIT_SEQ    11
#define VT3226_PWR_IDX_LEN    64
//}}

//{{RobertYu:20060609
#define BY_VT3342_REG_LEN     23
#define CB_VT3342_INIT_SEQ    13
#define VT3342_PWR_IDX_LEN    64
//}}

/*---------------------  Static Classes  ----------------------------*/

/*---------------------  Static Variables  --------------------------*/




u8 abyAL2230InitTable[CB_AL2230_INIT_SEQ][3] = {
    {0x03, 0xF7, 0x90},
    {0x03, 0x33, 0x31},
    {0x01, 0xB8, 0x02},
    {0x00, 0xFF, 0xF3},
    {0x00, 0x05, 0xA4},
    {0x0F, 0x4D, 0xC5},   //RobertYu:20060814
    {0x08, 0x05, 0xB6},
    {0x01, 0x47, 0xC7},
    {0x00, 0x06, 0x88},
    {0x04, 0x03, 0xB9},
    {0x00, 0xDB, 0xBA},
    {0x00, 0x09, 0x9B},
    {0x0B, 0xDF, 0xFC},
    {0x00, 0x00, 0x0D},
    {0x00, 0x58, 0x0F}
    };

u8 abyAL2230ChannelTable0[CB_MAX_CHANNEL_24G][3] = {
    {0x03, 0xF7, 0x90}, // channel = 1, Tf = 2412MHz
    {0x03, 0xF7, 0x90}, // channel = 2, Tf = 2417MHz
    {0x03, 0xE7, 0x90}, // channel = 3, Tf = 2422MHz
    {0x03, 0xE7, 0x90}, // channel = 4, Tf = 2427MHz
    {0x03, 0xF7, 0xA0}, // channel = 5, Tf = 2432MHz
    {0x03, 0xF7, 0xA0}, // channel = 6, Tf = 2437MHz
    {0x03, 0xE7, 0xA0}, // channel = 7, Tf = 2442MHz
    {0x03, 0xE7, 0xA0}, // channel = 8, Tf = 2447MHz
    {0x03, 0xF7, 0xB0}, // channel = 9, Tf = 2452MHz
    {0x03, 0xF7, 0xB0}, // channel = 10, Tf = 2457MHz
    {0x03, 0xE7, 0xB0}, // channel = 11, Tf = 2462MHz
    {0x03, 0xE7, 0xB0}, // channel = 12, Tf = 2467MHz
    {0x03, 0xF7, 0xC0}, // channel = 13, Tf = 2472MHz
    {0x03, 0xE7, 0xC0}  // channel = 14, Tf = 2412M
    };

u8 abyAL2230ChannelTable1[CB_MAX_CHANNEL_24G][3] = {
    {0x03, 0x33, 0x31}, // channel = 1, Tf = 2412MHz
    {0x0B, 0x33, 0x31}, // channel = 2, Tf = 2417MHz
    {0x03, 0x33, 0x31}, // channel = 3, Tf = 2422MHz
    {0x0B, 0x33, 0x31}, // channel = 4, Tf = 2427MHz
    {0x03, 0x33, 0x31}, // channel = 5, Tf = 2432MHz
    {0x0B, 0x33, 0x31}, // channel = 6, Tf = 2437MHz
    {0x03, 0x33, 0x31}, // channel = 7, Tf = 2442MHz
    {0x0B, 0x33, 0x31}, // channel = 8, Tf = 2447MHz
    {0x03, 0x33, 0x31}, // channel = 9, Tf = 2452MHz
    {0x0B, 0x33, 0x31}, // channel = 10, Tf = 2457MHz
    {0x03, 0x33, 0x31}, // channel = 11, Tf = 2462MHz
    {0x0B, 0x33, 0x31}, // channel = 12, Tf = 2467MHz
    {0x03, 0x33, 0x31}, // channel = 13, Tf = 2472MHz
    {0x06, 0x66, 0x61}  // channel = 14, Tf = 2412M
    };

// 40MHz reference frequency
// Need to Pull PLLON(PE3) low when writing channel registers through 3-wire.
u8 abyAL7230InitTable[CB_AL7230_INIT_SEQ][3] = {
    {0x20, 0x37, 0x90}, // Channel1 // Need modify for 11a
    {0x13, 0x33, 0x31}, // Channel1 // Need modify for 11a
    {0x84, 0x1F, 0xF2}, // Need modify for 11a: 451FE2
    {0x3F, 0xDF, 0xA3}, // Need modify for 11a: 5FDFA3
    {0x7F, 0xD7, 0x84}, // 11b/g    // Need modify for 11a
    //0x802B4500+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // Need modify for 11a: 8D1B45
    // RoberYu:20050113, Rev0.47 Regsiter Setting Guide
    {0x80, 0x2B, 0x55}, // Need modify for 11a: 8D1B55
    {0x56, 0xAF, 0x36},
    {0xCE, 0x02, 0x07}, // Need modify for 11a: 860207
    {0x6E, 0xBC, 0x98},
    {0x22, 0x1B, 0xB9},
    {0xE0, 0x00, 0x0A}, // Need modify for 11a: E0600A
    {0x08, 0x03, 0x1B}, // init 0x080B1B00 => 0x080F1B00 for 3 wire control TxGain(D10)
    //0x00093C00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, // Need modify for 11a: 00143C
    // RoberYu:20050113, Rev0.47 Regsiter Setting Guide
    {0x00, 0x0A, 0x3C}, // Need modify for 11a: 00143C
    {0xFF, 0xFF, 0xFD},
    {0x00, 0x00, 0x0E},
    {0x1A, 0xBA, 0x8F} // Need modify for 11a: 12BACF
    };

u8 abyAL7230InitTableAMode[CB_AL7230_INIT_SEQ][3] = {
    {0x2F, 0xF5, 0x20}, // Channel184 // Need modify for 11b/g
    {0x00, 0x00, 0x01}, // Channel184 // Need modify for 11b/g
    {0x45, 0x1F, 0xE2}, // Need modify for 11b/g
    {0x5F, 0xDF, 0xA3}, // Need modify for 11b/g
    {0x6F, 0xD7, 0x84}, // 11a    // Need modify for 11b/g
    {0x85, 0x3F, 0x55}, // Need modify for 11b/g, RoberYu:20050113
    {0x56, 0xAF, 0x36},
    {0xCE, 0x02, 0x07}, // Need modify for 11b/g
    {0x6E, 0xBC, 0x98},
    {0x22, 0x1B, 0xB9},
    {0xE0, 0x60, 0x0A}, // Need modify for 11b/g
    {0x08, 0x03, 0x1B}, // init 0x080B1B00 => 0x080F1B00 for 3 wire control TxGain(D10)
    {0x00, 0x14, 0x7C}, // Need modify for 11b/g
    {0xFF, 0xFF, 0xFD},
    {0x00, 0x00, 0x0E},
    {0x12, 0xBA, 0xCF} // Need modify for 11b/g
    };

u8 abyAL7230ChannelTable0[CB_MAX_CHANNEL][3] = {
    {0x20, 0x37, 0x90}, // channel =  1, Tf = 2412MHz
    {0x20, 0x37, 0x90}, // channel =  2, Tf = 2417MHz
    {0x20, 0x37, 0x90}, // channel =  3, Tf = 2422MHz
    {0x20, 0x37, 0x90}, // channel =  4, Tf = 2427MHz
    {0x20, 0x37, 0xA0}, // channel =  5, Tf = 2432MHz
    {0x20, 0x37, 0xA0}, // channel =  6, Tf = 2437MHz
    {0x20, 0x37, 0xA0}, // channel =  7, Tf = 2442MHz
    {0x20, 0x37, 0xA0}, // channel =  8, Tf = 2447MHz //RobertYu: 20050218, update for APNode 0.49
    {0x20, 0x37, 0xB0}, // channel =  9, Tf = 2452MHz //RobertYu: 20050218, update for APNode 0.49
    {0x20, 0x37, 0xB0}, // channel = 10, Tf = 2457MHz //RobertYu: 20050218, update for APNode 0.49
    {0x20, 0x37, 0xB0}, // channel = 11, Tf = 2462MHz //RobertYu: 20050218, update for APNode 0.49
    {0x20, 0x37, 0xB0}, // channel = 12, Tf = 2467MHz //RobertYu: 20050218, update for APNode 0.49
    {0x20, 0x37, 0xC0}, // channel = 13, Tf = 2472MHz //RobertYu: 20050218, update for APNode 0.49
    {0x20, 0x37, 0xC0}, // channel = 14, Tf = 2484MHz

    // 4.9G => Ch 183, 184, 185, 187, 188, 189, 192, 196  (Value:15 ~ 22)
    {0x0F, 0xF5, 0x20}, // channel = 183, Tf = 4915MHz (15)
    {0x2F, 0xF5, 0x20}, // channel = 184, Tf = 4920MHz (16)
    {0x0F, 0xF5, 0x20}, // channel = 185, Tf = 4925MHz (17)
    {0x0F, 0xF5, 0x20}, // channel = 187, Tf = 4935MHz (18)
    {0x2F, 0xF5, 0x20}, // channel = 188, Tf = 4940MHz (19)
    {0x0F, 0xF5, 0x20}, // channel = 189, Tf = 4945MHz (20)
    {0x2F, 0xF5, 0x30}, // channel = 192, Tf = 4960MHz (21)
    {0x2F, 0xF5, 0x30}, // channel = 196, Tf = 4980MHz (22)

    // 5G => Ch 7, 8, 9, 11, 12, 16, 34, 36, 38, 40, 42, 44, 46, 48, 52, 56, 60, 64,
    // 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165  (Value 23 ~ 56)

    {0x0F, 0xF5, 0x40}, // channel =   7, Tf = 5035MHz (23)
    {0x2F, 0xF5, 0x40}, // channel =   8, Tf = 5040MHz (24)
    {0x0F, 0xF5, 0x40}, // channel =   9, Tf = 5045MHz (25)
    {0x0F, 0xF5, 0x40}, // channel =  11, Tf = 5055MHz (26)
    {0x2F, 0xF5, 0x40}, // channel =  12, Tf = 5060MHz (27)
    {0x2F, 0xF5, 0x50}, // channel =  16, Tf = 5080MHz (28)
    {0x2F, 0xF5, 0x60}, // channel =  34, Tf = 5170MHz (29)
    {0x2F, 0xF5, 0x60}, // channel =  36, Tf = 5180MHz (30)
    {0x2F, 0xF5, 0x70}, // channel =  38, Tf = 5190MHz (31) //RobertYu: 20050218, update for APNode 0.49
    {0x2F, 0xF5, 0x70}, // channel =  40, Tf = 5200MHz (32)
    {0x2F, 0xF5, 0x70}, // channel =  42, Tf = 5210MHz (33)
    {0x2F, 0xF5, 0x70}, // channel =  44, Tf = 5220MHz (34)
    {0x2F, 0xF5, 0x70}, // channel =  46, Tf = 5230MHz (35)
    {0x2F, 0xF5, 0x70}, // channel =  48, Tf = 5240MHz (36)
    {0x2F, 0xF5, 0x80}, // channel =  52, Tf = 5260MHz (37)
    {0x2F, 0xF5, 0x80}, // channel =  56, Tf = 5280MHz (38)
    {0x2F, 0xF5, 0x80}, // channel =  60, Tf = 5300MHz (39)
    {0x2F, 0xF5, 0x90}, // channel =  64, Tf = 5320MHz (40)

    {0x2F, 0xF5, 0xC0}, // channel = 100, Tf = 5500MHz (41)
    {0x2F, 0xF5, 0xC0}, // channel = 104, Tf = 5520MHz (42)
    {0x2F, 0xF5, 0xC0}, // channel = 108, Tf = 5540MHz (43)
    {0x2F, 0xF5, 0xD0}, // channel = 112, Tf = 5560MHz (44)
    {0x2F, 0xF5, 0xD0}, // channel = 116, Tf = 5580MHz (45)
    {0x2F, 0xF5, 0xD0}, // channel = 120, Tf = 5600MHz (46)
    {0x2F, 0xF5, 0xE0}, // channel = 124, Tf = 5620MHz (47)
    {0x2F, 0xF5, 0xE0}, // channel = 128, Tf = 5640MHz (48)
    {0x2F, 0xF5, 0xE0}, // channel = 132, Tf = 5660MHz (49)
    {0x2F, 0xF5, 0xF0}, // channel = 136, Tf = 5680MHz (50)
    {0x2F, 0xF5, 0xF0}, // channel = 140, Tf = 5700MHz (51)
    {0x2F, 0xF6, 0x00}, // channel = 149, Tf = 5745MHz (52)
    {0x2F, 0xF6, 0x00}, // channel = 153, Tf = 5765MHz (53)
    {0x2F, 0xF6, 0x00}, // channel = 157, Tf = 5785MHz (54)
    {0x2F, 0xF6, 0x10}, // channel = 161, Tf = 5805MHz (55)
    {0x2F, 0xF6, 0x10} // channel = 165, Tf = 5825MHz (56)
    };

u8 abyAL7230ChannelTable1[CB_MAX_CHANNEL][3] = {
    {0x13, 0x33, 0x31}, // channel =  1, Tf = 2412MHz
    {0x1B, 0x33, 0x31}, // channel =  2, Tf = 2417MHz
    {0x03, 0x33, 0x31}, // channel =  3, Tf = 2422MHz
    {0x0B, 0x33, 0x31}, // channel =  4, Tf = 2427MHz
    {0x13, 0x33, 0x31}, // channel =  5, Tf = 2432MHz
    {0x1B, 0x33, 0x31}, // channel =  6, Tf = 2437MHz
    {0x03, 0x33, 0x31}, // channel =  7, Tf = 2442MHz
    {0x0B, 0x33, 0x31}, // channel =  8, Tf = 2447MHz
    {0x13, 0x33, 0x31}, // channel =  9, Tf = 2452MHz
    {0x1B, 0x33, 0x31}, // channel = 10, Tf = 2457MHz
    {0x03, 0x33, 0x31}, // channel = 11, Tf = 2462MHz
    {0x0B, 0x33, 0x31}, // channel = 12, Tf = 2467MHz
    {0x13, 0x33, 0x31}, // channel = 13, Tf = 2472MHz
    {0x06, 0x66, 0x61}, // channel = 14, Tf = 2484MHz

    // 4.9G => Ch 183, 184, 185, 187, 188, 189, 192, 196  (Value:15 ~ 22)
    {0x1D, 0x55, 0x51}, // channel = 183, Tf = 4915MHz (15)
    {0x00, 0x00, 0x01}, // channel = 184, Tf = 4920MHz (16)
    {0x02, 0xAA, 0xA1}, // channel = 185, Tf = 4925MHz (17)
    {0x08, 0x00, 0x01}, // channel = 187, Tf = 4935MHz (18)
    {0x0A, 0xAA, 0xA1}, // channel = 188, Tf = 4940MHz (19)
    {0x0D, 0x55, 0x51}, // channel = 189, Tf = 4945MHz (20)
    {0x15, 0x55, 0x51}, // channel = 192, Tf = 4960MHz (21)
    {0x00, 0x00, 0x01}, // channel = 196, Tf = 4980MHz (22)

    // 5G => Ch 7, 8, 9, 11, 12, 16, 34, 36, 38, 40, 42, 44, 46, 48, 52, 56, 60, 64,
    // 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165  (Value 23 ~ 56)
    {0x1D, 0x55, 0x51}, // channel =   7, Tf = 5035MHz (23)
    {0x00, 0x00, 0x01}, // channel =   8, Tf = 5040MHz (24)
    {0x02, 0xAA, 0xA1}, // channel =   9, Tf = 5045MHz (25)
    {0x08, 0x00, 0x01}, // channel =  11, Tf = 5055MHz (26)
    {0x0A, 0xAA, 0xA1}, // channel =  12, Tf = 5060MHz (27)
    {0x15, 0x55, 0x51}, // channel =  16, Tf = 5080MHz (28)
    {0x05, 0x55, 0x51}, // channel =  34, Tf = 5170MHz (29)
    {0x0A, 0xAA, 0xA1}, // channel =  36, Tf = 5180MHz (30)
    {0x10, 0x00, 0x01}, // channel =  38, Tf = 5190MHz (31)
    {0x15, 0x55, 0x51}, // channel =  40, Tf = 5200MHz (32)
    {0x1A, 0xAA, 0xA1}, // channel =  42, Tf = 5210MHz (33)
    {0x00, 0x00, 0x01}, // channel =  44, Tf = 5220MHz (34)
    {0x05, 0x55, 0x51}, // channel =  46, Tf = 5230MHz (35)
    {0x0A, 0xAA, 0xA1}, // channel =  48, Tf = 5240MHz (36)
    {0x15, 0x55, 0x51}, // channel =  52, Tf = 5260MHz (37)
    {0x00, 0x00, 0x01}, // channel =  56, Tf = 5280MHz (38)
    {0x0A, 0xAA, 0xA1}, // channel =  60, Tf = 5300MHz (39)
    {0x15, 0x55, 0x51}, // channel =  64, Tf = 5320MHz (40)
    {0x15, 0x55, 0x51}, // channel = 100, Tf = 5500MHz (41)
    {0x00, 0x00, 0x01}, // channel = 104, Tf = 5520MHz (42)
    {0x0A, 0xAA, 0xA1}, // channel = 108, Tf = 5540MHz (43)
    {0x15, 0x55, 0x51}, // channel = 112, Tf = 5560MHz (44)
    {0x00, 0x00, 0x01}, // channel = 116, Tf = 5580MHz (45)
    {0x0A, 0xAA, 0xA1}, // channel = 120, Tf = 5600MHz (46)
    {0x15, 0x55, 0x51}, // channel = 124, Tf = 5620MHz (47)
    {0x00, 0x00, 0x01}, // channel = 128, Tf = 5640MHz (48)
    {0x0A, 0xAA, 0xA1}, // channel = 132, Tf = 5660MHz (49)
    {0x15, 0x55, 0x51}, // channel = 136, Tf = 5680MHz (50)
    {0x00, 0x00, 0x01}, // channel = 140, Tf = 5700MHz (51)
    {0x18, 0x00, 0x01}, // channel = 149, Tf = 5745MHz (52)
    {0x02, 0xAA, 0xA1}, // channel = 153, Tf = 5765MHz (53)
    {0x0D, 0x55, 0x51}, // channel = 157, Tf = 5785MHz (54)
    {0x18, 0x00, 0x01}, // channel = 161, Tf = 5805MHz (55)
    {0x02, 0xAA, 0xB1}  // channel = 165, Tf = 5825MHz (56)
    };

u8 abyAL7230ChannelTable2[CB_MAX_CHANNEL][3] = {
    {0x7F, 0xD7, 0x84}, // channel =  1, Tf = 2412MHz
    {0x7F, 0xD7, 0x84}, // channel =  2, Tf = 2417MHz
    {0x7F, 0xD7, 0x84}, // channel =  3, Tf = 2422MHz
    {0x7F, 0xD7, 0x84}, // channel =  4, Tf = 2427MHz
    {0x7F, 0xD7, 0x84}, // channel =  5, Tf = 2432MHz
    {0x7F, 0xD7, 0x84}, // channel =  6, Tf = 2437MHz
    {0x7F, 0xD7, 0x84}, // channel =  7, Tf = 2442MHz
    {0x7F, 0xD7, 0x84}, // channel =  8, Tf = 2447MHz
    {0x7F, 0xD7, 0x84}, // channel =  9, Tf = 2452MHz
    {0x7F, 0xD7, 0x84}, // channel = 10, Tf = 2457MHz
    {0x7F, 0xD7, 0x84}, // channel = 11, Tf = 2462MHz
    {0x7F, 0xD7, 0x84}, // channel = 12, Tf = 2467MHz
    {0x7F, 0xD7, 0x84}, // channel = 13, Tf = 2472MHz
    {0x7F, 0xD7, 0x84}, // channel = 14, Tf = 2484MHz

    // 4.9G => Ch 183, 184, 185, 187, 188, 189, 192, 196  (Value:15 ~ 22)
    {0x7F, 0xD7, 0x84}, // channel = 183, Tf = 4915MHz (15)
    {0x6F, 0xD7, 0x84}, // channel = 184, Tf = 4920MHz (16)
    {0x7F, 0xD7, 0x84}, // channel = 185, Tf = 4925MHz (17)
    {0x7F, 0xD7, 0x84}, // channel = 187, Tf = 4935MHz (18)
    {0x7F, 0xD7, 0x84}, // channel = 188, Tf = 4940MHz (19)
    {0x7F, 0xD7, 0x84}, // channel = 189, Tf = 4945MHz (20)
    {0x7F, 0xD7, 0x84}, // channel = 192, Tf = 4960MHz (21)
    {0x6F, 0xD7, 0x84}, // channel = 196, Tf = 4980MHz (22)

    // 5G => Ch 7, 8, 9, 11, 12, 16, 34, 36, 38, 40, 42, 44, 46, 48, 52, 56, 60, 64,
    // 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165  (Value 23 ~ 56)
    {0x7F, 0xD7, 0x84}, // channel =   7, Tf = 5035MHz (23)
    {0x6F, 0xD7, 0x84}, // channel =   8, Tf = 5040MHz (24)
    {0x7F, 0xD7, 0x84}, // channel =   9, Tf = 5045MHz (25)
    {0x7F, 0xD7, 0x84}, // channel =  11, Tf = 5055MHz (26)
    {0x7F, 0xD7, 0x84}, // channel =  12, Tf = 5060MHz (27)
    {0x7F, 0xD7, 0x84}, // channel =  16, Tf = 5080MHz (28)
    {0x7F, 0xD7, 0x84}, // channel =  34, Tf = 5170MHz (29)
    {0x7F, 0xD7, 0x84}, // channel =  36, Tf = 5180MHz (30)
    {0x7F, 0xD7, 0x84}, // channel =  38, Tf = 5190MHz (31)
    {0x7F, 0xD7, 0x84}, // channel =  40, Tf = 5200MHz (32)
    {0x7F, 0xD7, 0x84}, // channel =  42, Tf = 5210MHz (33)
    {0x6F, 0xD7, 0x84}, // channel =  44, Tf = 5220MHz (34)
    {0x7F, 0xD7, 0x84}, // channel =  46, Tf = 5230MHz (35)
    {0x7F, 0xD7, 0x84}, // channel =  48, Tf = 5240MHz (36)
    {0x7F, 0xD7, 0x84}, // channel =  52, Tf = 5260MHz (37)
    {0x6F, 0xD7, 0x84}, // channel =  56, Tf = 5280MHz (38)
    {0x7F, 0xD7, 0x84}, // channel =  60, Tf = 5300MHz (39)
    {0x7F, 0xD7, 0x84}, // channel =  64, Tf = 5320MHz (40)
    {0x7F, 0xD7, 0x84}, // channel = 100, Tf = 5500MHz (41)
    {0x6F, 0xD7, 0x84}, // channel = 104, Tf = 5520MHz (42)
    {0x7F, 0xD7, 0x84}, // channel = 108, Tf = 5540MHz (43)
    {0x7F, 0xD7, 0x84}, // channel = 112, Tf = 5560MHz (44)
    {0x6F, 0xD7, 0x84}, // channel = 116, Tf = 5580MHz (45)
    {0x7F, 0xD7, 0x84}, // channel = 120, Tf = 5600MHz (46)
    {0x7F, 0xD7, 0x84}, // channel = 124, Tf = 5620MHz (47)
    {0x6F, 0xD7, 0x84}, // channel = 128, Tf = 5640MHz (48)
    {0x7F, 0xD7, 0x84}, // channel = 132, Tf = 5660MHz (49)
    {0x7F, 0xD7, 0x84}, // channel = 136, Tf = 5680MHz (50)
    {0x6F, 0xD7, 0x84}, // channel = 140, Tf = 5700MHz (51)
    {0x7F, 0xD7, 0x84}, // channel = 149, Tf = 5745MHz (52)
    {0x7F, 0xD7, 0x84}, // channel = 153, Tf = 5765MHz (53)
    {0x7F, 0xD7, 0x84}, // channel = 157, Tf = 5785MHz (54)
    {0x7F, 0xD7, 0x84}, // channel = 161, Tf = 5805MHz (55)
    {0x7F, 0xD7, 0x84}  // channel = 165, Tf = 5825MHz (56)
    };

///{{RobertYu:20051111
u8 abyVT3226_InitTable[CB_VT3226_INIT_SEQ][3] = {
    {0x03, 0xFF, 0x80},
    {0x02, 0x82, 0xA1},
    {0x03, 0xC6, 0xA2},
    {0x01, 0x97, 0x93},
    {0x03, 0x66, 0x64},
    {0x00, 0x61, 0xA5},
    {0x01, 0x7B, 0xD6},
    {0x00, 0x80, 0x17},
    {0x03, 0xF8, 0x08},
    {0x00, 0x02, 0x39},   //RobertYu:20051116
    {0x02, 0x00, 0x2A}
    };

u8 abyVT3226D0_InitTable[CB_VT3226_INIT_SEQ][3] = {
    {0x03, 0xFF, 0x80},
    {0x03, 0x02, 0x21}, //RobertYu:20060327
    {0x03, 0xC6, 0xA2},
    {0x01, 0x97, 0x93},
    {0x03, 0x66, 0x64},
    {0x00, 0x71, 0xA5}, //RobertYu:20060103
    {0x01, 0x15, 0xC6}, //RobertYu:20060420
    {0x01, 0x2E, 0x07}, //RobertYu:20060420
    {0x00, 0x58, 0x08}, //RobertYu:20060111
    {0x00, 0x02, 0x79}, //RobertYu:20060420
    {0x02, 0x01, 0xAA}  //RobertYu:20060523
    };


u8 abyVT3226_ChannelTable0[CB_MAX_CHANNEL_24G][3] = {
    {0x01, 0x97, 0x83}, // channel = 1, Tf = 2412MHz
    {0x01, 0x97, 0x83}, // channel = 2, Tf = 2417MHz
    {0x01, 0x97, 0x93}, // channel = 3, Tf = 2422MHz
    {0x01, 0x97, 0x93}, // channel = 4, Tf = 2427MHz
    {0x01, 0x97, 0x93}, // channel = 5, Tf = 2432MHz
    {0x01, 0x97, 0x93}, // channel = 6, Tf = 2437MHz
    {0x01, 0x97, 0xA3}, // channel = 7, Tf = 2442MHz
    {0x01, 0x97, 0xA3}, // channel = 8, Tf = 2447MHz
    {0x01, 0x97, 0xA3}, // channel = 9, Tf = 2452MHz
    {0x01, 0x97, 0xA3}, // channel = 10, Tf = 2457MHz
    {0x01, 0x97, 0xB3}, // channel = 11, Tf = 2462MHz
    {0x01, 0x97, 0xB3}, // channel = 12, Tf = 2467MHz
    {0x01, 0x97, 0xB3}, // channel = 13, Tf = 2472MHz
    {0x03, 0x37, 0xC3}  // channel = 14, Tf = 2484MHz
    };

u8 abyVT3226_ChannelTable1[CB_MAX_CHANNEL_24G][3] = {
    {0x02, 0x66, 0x64}, // channel = 1, Tf = 2412MHz
    {0x03, 0x66, 0x64}, // channel = 2, Tf = 2417MHz
    {0x00, 0x66, 0x64}, // channel = 3, Tf = 2422MHz
    {0x01, 0x66, 0x64}, // channel = 4, Tf = 2427MHz
    {0x02, 0x66, 0x64}, // channel = 5, Tf = 2432MHz
    {0x03, 0x66, 0x64}, // channel = 6, Tf = 2437MHz
    {0x00, 0x66, 0x64}, // channel = 7, Tf = 2442MHz
    {0x01, 0x66, 0x64}, // channel = 8, Tf = 2447MHz
    {0x02, 0x66, 0x64}, // channel = 9, Tf = 2452MHz
    {0x03, 0x66, 0x64}, // channel = 10, Tf = 2457MHz
    {0x00, 0x66, 0x64}, // channel = 11, Tf = 2462MHz
    {0x01, 0x66, 0x64}, // channel = 12, Tf = 2467MHz
    {0x02, 0x66, 0x64}, // channel = 13, Tf = 2472MHz
    {0x00, 0xCC, 0xC4}  // channel = 14, Tf = 2484MHz
    };
///}}RobertYu


//{{RobertYu:20060502, TWIF 1.14, LO Current for 11b mode
u32 dwVT3226D0LoCurrentTable[CB_MAX_CHANNEL_24G] = {
    0x0135C600+(BY_VT3226_REG_LEN<<3)+IFREGCTL_REGW, // channel = 1, Tf = 2412MHz
    0x0135C600+(BY_VT3226_REG_LEN<<3)+IFREGCTL_REGW, // channel = 2, Tf = 2417MHz
    0x0235C600+(BY_VT3226_REG_LEN<<3)+IFREGCTL_REGW, // channel = 3, Tf = 2422MHz
    0x0235C600+(BY_VT3226_REG_LEN<<3)+IFREGCTL_REGW, // channel = 4, Tf = 2427MHz
    0x0235C600+(BY_VT3226_REG_LEN<<3)+IFREGCTL_REGW, // channel = 5, Tf = 2432MHz
    0x0335C600+(BY_VT3226_REG_LEN<<3)+IFREGCTL_REGW, // channel = 6, Tf = 2437MHz
    0x0335C600+(BY_VT3226_REG_LEN<<3)+IFREGCTL_REGW, // channel = 7, Tf = 2442MHz
    0x0335C600+(BY_VT3226_REG_LEN<<3)+IFREGCTL_REGW, // channel = 8, Tf = 2447MHz
    0x0335C600+(BY_VT3226_REG_LEN<<3)+IFREGCTL_REGW, // channel = 9, Tf = 2452MHz
    0x0335C600+(BY_VT3226_REG_LEN<<3)+IFREGCTL_REGW, // channel = 10, Tf = 2457MHz
    0x0335C600+(BY_VT3226_REG_LEN<<3)+IFREGCTL_REGW, // channel = 11, Tf = 2462MHz
    0x0335C600+(BY_VT3226_REG_LEN<<3)+IFREGCTL_REGW, // channel = 12, Tf = 2467MHz
    0x0335C600+(BY_VT3226_REG_LEN<<3)+IFREGCTL_REGW, // channel = 13, Tf = 2472MHz
    0x0135C600+(BY_VT3226_REG_LEN<<3)+IFREGCTL_REGW  // channel = 14, Tf = 2484MHz
};
//}}


//{{RobertYu:20060609
u8 abyVT3342A0_InitTable[CB_VT3342_INIT_SEQ][3] = { /* 11b/g mode */
    {0x03, 0xFF, 0x80}, //update for mode//
    {0x02, 0x08, 0x81},
    {0x00, 0xC6, 0x02},
    {0x03, 0xC5, 0x13}, // channel6
    {0x00, 0xEE, 0xE4}, // channel6
    {0x00, 0x71, 0xA5},
    {0x01, 0x75, 0x46},
    {0x01, 0x40, 0x27},
    {0x01, 0x54, 0x08},
    {0x00, 0x01, 0x69},
    {0x02, 0x00, 0xAA},
    {0x00, 0x08, 0xCB},
    {0x01, 0x70, 0x0C}
    };

 //11b/g mode: 0x03, 0xFF, 0x80,
 //11a mode:   0x03, 0xFF, 0xC0,

 // channel44, 5220MHz  0x00C402
 // channel56, 5280MHz  0x00C402 for disable Frac
 // other channels 0x00C602

u8 abyVT3342_ChannelTable0[CB_MAX_CHANNEL][3] = {
    {0x02, 0x05, 0x03}, // channel = 1, Tf = 2412MHz
    {0x01, 0x15, 0x03}, // channel = 2, Tf = 2417MHz
    {0x03, 0xC5, 0x03}, // channel = 3, Tf = 2422MHz
    {0x02, 0x65, 0x03}, // channel = 4, Tf = 2427MHz
    {0x01, 0x15, 0x13}, // channel = 5, Tf = 2432MHz
    {0x03, 0xC5, 0x13}, // channel = 6, Tf = 2437MHz
    {0x02, 0x05, 0x13}, // channel = 7, Tf = 2442MHz
    {0x01, 0x15, 0x13}, // channel = 8, Tf = 2447MHz
    {0x03, 0xC5, 0x13}, // channel = 9, Tf = 2452MHz
    {0x02, 0x65, 0x13}, // channel = 10, Tf = 2457MHz
    {0x01, 0x15, 0x23}, // channel = 11, Tf = 2462MHz
    {0x03, 0xC5, 0x23}, // channel = 12, Tf = 2467MHz
    {0x02, 0x05, 0x23}, // channel = 13, Tf = 2472MHz
    {0x00, 0xD5, 0x23}, // channel = 14, Tf = 2484MHz

    // 4.9G => Ch 183, 184, 185, 187, 188, 189, 192, 196  (Value:15 ~ 22)
    {0x01, 0x15, 0x13}, // channel = 183, Tf = 4915MHz (15), TBD
    {0x01, 0x15, 0x13}, // channel = 184, Tf = 4920MHz (16), TBD
    {0x01, 0x15, 0x13}, // channel = 185, Tf = 4925MHz (17), TBD
    {0x01, 0x15, 0x13}, // channel = 187, Tf = 4935MHz (18), TBD
    {0x01, 0x15, 0x13}, // channel = 188, Tf = 4940MHz (19), TBD
    {0x01, 0x15, 0x13}, // channel = 189, Tf = 4945MHz (20), TBD
    {0x01, 0x15, 0x13}, // channel = 192, Tf = 4960MHz (21), TBD
    {0x01, 0x15, 0x13}, // channel = 196, Tf = 4980MHz (22), TBD

    // 5G => Ch 7, 8, 9, 11, 12, 16, 34, 36, 38, 40, 42, 44, 46, 48, 52, 56, 60, 64,
    // 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165  (Value 23 ~ 56)
    {0x01, 0x15, 0x13}, // channel =   7, Tf = 5035MHz (23), TBD
    {0x01, 0x15, 0x13}, // channel =   8, Tf = 5040MHz (24), TBD
    {0x01, 0x15, 0x13}, // channel =   9, Tf = 5045MHz (25), TBD
    {0x01, 0x15, 0x13}, // channel =  11, Tf = 5055MHz (26), TBD
    {0x01, 0x15, 0x13}, // channel =  12, Tf = 5060MHz (27), TBD
    {0x01, 0x15, 0x13}, // channel =  16, Tf = 5080MHz (28), TBD
    {0x01, 0x15, 0x13}, // channel =  34, Tf = 5170MHz (29), TBD
    {0x01, 0x55, 0x63}, // channel =  36, Tf = 5180MHz (30)
    {0x01, 0x55, 0x63}, // channel =  38, Tf = 5190MHz (31), TBD
    {0x02, 0xA5, 0x63}, // channel =  40, Tf = 5200MHz (32)
    {0x02, 0xA5, 0x63}, // channel =  42, Tf = 5210MHz (33), TBD
    {0x00, 0x05, 0x73}, // channel =  44, Tf = 5220MHz (34)
    {0x00, 0x05, 0x73}, // channel =  46, Tf = 5230MHz (35), TBD
    {0x01, 0x55, 0x73}, // channel =  48, Tf = 5240MHz (36)
    {0x02, 0xA5, 0x73}, // channel =  52, Tf = 5260MHz (37)
    {0x00, 0x05, 0x83}, // channel =  56, Tf = 5280MHz (38)
    {0x01, 0x55, 0x83}, // channel =  60, Tf = 5300MHz (39)
    {0x02, 0xA5, 0x83}, // channel =  64, Tf = 5320MHz (40)

    {0x02, 0xA5, 0x83}, // channel = 100, Tf = 5500MHz (41), TBD
    {0x02, 0xA5, 0x83}, // channel = 104, Tf = 5520MHz (42), TBD
    {0x02, 0xA5, 0x83}, // channel = 108, Tf = 5540MHz (43), TBD
    {0x02, 0xA5, 0x83}, // channel = 112, Tf = 5560MHz (44), TBD
    {0x02, 0xA5, 0x83}, // channel = 116, Tf = 5580MHz (45), TBD
    {0x02, 0xA5, 0x83}, // channel = 120, Tf = 5600MHz (46), TBD
    {0x02, 0xA5, 0x83}, // channel = 124, Tf = 5620MHz (47), TBD
    {0x02, 0xA5, 0x83}, // channel = 128, Tf = 5640MHz (48), TBD
    {0x02, 0xA5, 0x83}, // channel = 132, Tf = 5660MHz (49), TBD
    {0x02, 0xA5, 0x83}, // channel = 136, Tf = 5680MHz (50), TBD
    {0x02, 0xA5, 0x83}, // channel = 140, Tf = 5700MHz (51), TBD

    {0x00, 0x05, 0xF3}, // channel = 149, Tf = 5745MHz (52)
    {0x01, 0x56, 0x03}, // channel = 153, Tf = 5765MHz (53)
    {0x02, 0xA6, 0x03}, // channel = 157, Tf = 5785MHz (54)
    {0x00, 0x06, 0x03}, // channel = 161, Tf = 5805MHz (55)
    {0x00, 0x06, 0x03}  // channel = 165, Tf = 5825MHz (56), TBD
    };

u8 abyVT3342_ChannelTable1[CB_MAX_CHANNEL][3] = {
    {0x01, 0x99, 0x94}, // channel = 1, Tf = 2412MHz
    {0x02, 0x44, 0x44}, // channel = 2, Tf = 2417MHz
    {0x02, 0xEE, 0xE4}, // channel = 3, Tf = 2422MHz
    {0x03, 0x99, 0x94}, // channel = 4, Tf = 2427MHz
    {0x00, 0x44, 0x44}, // channel = 5, Tf = 2432MHz
    {0x00, 0xEE, 0xE4}, // channel = 6, Tf = 2437MHz
    {0x01, 0x99, 0x94}, // channel = 7, Tf = 2442MHz
    {0x02, 0x44, 0x44}, // channel = 8, Tf = 2447MHz
    {0x02, 0xEE, 0xE4}, // channel = 9, Tf = 2452MHz
    {0x03, 0x99, 0x94}, // channel = 10, Tf = 2457MHz
    {0x00, 0x44, 0x44}, // channel = 11, Tf = 2462MHz
    {0x00, 0xEE, 0xE4}, // channel = 12, Tf = 2467MHz
    {0x01, 0x99, 0x94}, // channel = 13, Tf = 2472MHz
    {0x03, 0x33, 0x34}, // channel = 14, Tf = 2484MHz

    // 4.9G => Ch 183, 184, 185, 187, 188, 189, 192, 196  (Value:15 ~ 22)
    {0x00, 0x44, 0x44}, // channel = 183, Tf = 4915MHz (15), TBD
    {0x00, 0x44, 0x44}, // channel = 184, Tf = 4920MHz (16), TBD
    {0x00, 0x44, 0x44}, // channel = 185, Tf = 4925MHz (17), TBD
    {0x00, 0x44, 0x44}, // channel = 187, Tf = 4935MHz (18), TBD
    {0x00, 0x44, 0x44}, // channel = 188, Tf = 4940MHz (19), TBD
    {0x00, 0x44, 0x44}, // channel = 189, Tf = 4945MHz (20), TBD
    {0x00, 0x44, 0x44}, // channel = 192, Tf = 4960MHz (21), TBD
    {0x00, 0x44, 0x44}, // channel = 196, Tf = 4980MHz (22), TBD

    // 5G => Ch 7, 8, 9, 11, 12, 16, 34, 36, 38, 40, 42, 44, 46, 48, 52, 56, 60, 64,
    // 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165  (Value 23 ~ 56)
    {0x00, 0x44, 0x44}, // channel =   7, Tf = 5035MHz (23), TBD
    {0x00, 0x44, 0x44}, // channel =   8, Tf = 5040MHz (24), TBD
    {0x00, 0x44, 0x44}, // channel =   9, Tf = 5045MHz (25), TBD
    {0x00, 0x44, 0x44}, // channel =  11, Tf = 5055MHz (26), TBD
    {0x00, 0x44, 0x44}, // channel =  12, Tf = 5060MHz (27), TBD
    {0x00, 0x44, 0x44}, // channel =  16, Tf = 5080MHz (28), TBD
    {0x00, 0x44, 0x44}, // channel =  34, Tf = 5170MHz (29), TBD
    {0x01, 0x55, 0x54}, // channel =  36, Tf = 5180MHz (30)
    {0x01, 0x55, 0x54}, // channel =  38, Tf = 5190MHz (31), TBD
    {0x02, 0xAA, 0xA4}, // channel =  40, Tf = 5200MHz (32)
    {0x02, 0xAA, 0xA4}, // channel =  42, Tf = 5210MHz (33), TBD
    {0x00, 0x00, 0x04}, // channel =  44, Tf = 5220MHz (34)
    {0x00, 0x00, 0x04}, // channel =  46, Tf = 5230MHz (35), TBD
    {0x01, 0x55, 0x54}, // channel =  48, Tf = 5240MHz (36)
    {0x02, 0xAA, 0xA4}, // channel =  52, Tf = 5260MHz (37)
    {0x00, 0x00, 0x04}, // channel =  56, Tf = 5280MHz (38)
    {0x01, 0x55, 0x54}, // channel =  60, Tf = 5300MHz (39)
    {0x02, 0xAA, 0xA4}, // channel =  64, Tf = 5320MHz (40)
    {0x02, 0xAA, 0xA4}, // channel = 100, Tf = 5500MHz (41), TBD
    {0x02, 0xAA, 0xA4}, // channel = 104, Tf = 5520MHz (42), TBD
    {0x02, 0xAA, 0xA4}, // channel = 108, Tf = 5540MHz (43), TBD
    {0x02, 0xAA, 0xA4}, // channel = 112, Tf = 5560MHz (44), TBD
    {0x02, 0xAA, 0xA4}, // channel = 116, Tf = 5580MHz (45), TBD
    {0x02, 0xAA, 0xA4}, // channel = 120, Tf = 5600MHz (46), TBD
    {0x02, 0xAA, 0xA4}, // channel = 124, Tf = 5620MHz (47), TBD
    {0x02, 0xAA, 0xA4}, // channel = 128, Tf = 5640MHz (48), TBD
    {0x02, 0xAA, 0xA4}, // channel = 132, Tf = 5660MHz (49), TBD
    {0x02, 0xAA, 0xA4}, // channel = 136, Tf = 5680MHz (50), TBD
    {0x02, 0xAA, 0xA4}, // channel = 140, Tf = 5700MHz (51), TBD
    {0x03, 0x00, 0x04}, // channel = 149, Tf = 5745MHz (52)
    {0x00, 0x55, 0x54}, // channel = 153, Tf = 5765MHz (53)
    {0x01, 0xAA, 0xA4}, // channel = 157, Tf = 5785MHz (54)
    {0x03, 0x00, 0x04}, // channel = 161, Tf = 5805MHz (55)
    {0x03, 0x00, 0x04}  // channel = 165, Tf = 5825MHz (56), TBD
    };


/*+
 *
 * Power Table
 *
-*/

const u32 dwAL2230PowerTable[AL2230_PWR_IDX_LEN] = {
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

/*---------------------  Static Functions  --------------------------*/

/*---------------------  Export Variables  --------------------------*/

//{{ RobertYu:20050103, Channel 11a Number To Index
// 4.9G => Ch 183, 184, 185, 187, 188, 189, 192, 196  (Value:15 ~ 22)
// 5G => Ch 7, 8, 9, 11, 12, 16, 34, 36, 38, 40, 42, 44, 46, 48, 52, 56, 60, 64,
// 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165  (Value 23 ~ 56)

const u8 RFaby11aChannelIndex[200] = {
  // 1   2   3   4   5   6   7   8   9  10
    00, 00, 00, 00, 00, 00, 23, 24, 25, 00,  // 10
    26, 27, 00, 00, 00, 28, 00, 00, 00, 00,  // 20
    00, 00, 00, 00, 00, 00, 00, 00, 00, 00,  // 30
    00, 00, 00, 29, 00, 30, 00, 31, 00, 32,  // 40
    00, 33, 00, 34, 00, 35, 00, 36, 00, 00,  // 50
    00, 37, 00, 00, 00, 38, 00, 00, 00, 39,  // 60
    00, 00, 00, 40, 00, 00, 00, 00, 00, 00,  // 70
    00, 00, 00, 00, 00, 00, 00, 00, 00, 00,  // 80
    00, 00, 00, 00, 00, 00, 00, 00, 00, 00,  // 90
    00, 00, 00, 00, 00, 00, 00, 00, 00, 41,  //100

    00, 00, 00, 42, 00, 00, 00, 43, 00, 00,  //110
    00, 44, 00, 00, 00, 45, 00, 00, 00, 46,  //120
    00, 00, 00, 47, 00, 00, 00, 48, 00, 00,  //130
    00, 49, 00, 00, 00, 50, 00, 00, 00, 51,  //140
    00, 00, 00, 00, 00, 00, 00, 00, 52, 00,  //150
    00, 00, 53, 00, 00, 00, 54, 00, 00, 00,  //160
    55, 00, 00, 00, 56, 00, 00, 00, 00, 00,  //170
    00, 00, 00, 00, 00, 00, 00, 00, 00, 00,  //180
    00, 00, 15, 16, 17, 00, 18, 19, 20, 00,  //190
    00, 21, 00, 00, 00, 22, 00, 00, 00, 00   //200
};
//}} RobertYu

/*---------------------  Export Functions  --------------------------*/

/*
 * Description: Write to IF/RF, by embedded programming
 *
 * Parameters:
 *  In:
 *      dwData      - data to write
 *  Out:
 *      none
 *
 * Return Value: true if succeeded; false if failed.
 *
 */
int IFRFbWriteEmbedded(struct vnt_private *pDevice, u32 dwData)
{
	u8 pbyData[4];

	pbyData[0] = (u8)dwData;
	pbyData[1] = (u8)(dwData >> 8);
	pbyData[2] = (u8)(dwData >> 16);
	pbyData[3] = (u8)(dwData >> 24);

	CONTROLnsRequestOut(pDevice,
		MESSAGE_TYPE_WRITE_IFRF, 0, 0, 4, pbyData);


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
int RFbSetPower(struct vnt_private *pDevice, u32 uRATE, u32 uCH)
{
	int bResult = true;
	u8 byPwr = pDevice->byCCKPwr;

	if (pDevice->dwDiagRefCount)
		return true;

	if (uCH == 0)
		return -EINVAL;

    switch (uRATE) {
    case RATE_1M:
    case RATE_2M:
    case RATE_5M:
    case RATE_11M:
        byPwr = pDevice->abyCCKPwrTbl[uCH-1];
        break;
    case RATE_6M:
    case RATE_9M:
    case RATE_18M:
    case RATE_24M:
    case RATE_36M:
    case RATE_48M:
    case RATE_54M:
        if (uCH > CB_MAX_CHANNEL_24G) {
            byPwr = pDevice->abyOFDMAPwrTbl[uCH-15];
        } else {
            byPwr = pDevice->abyOFDMPwrTbl[uCH-1];
        }
        break;
    }

    bResult = RFbRawSetPower(pDevice, byPwr, uRATE);

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

int RFbRawSetPower(struct vnt_private *pDevice, u8 byPwr, u32 uRATE)
{
	int bResult = true;

    if (pDevice->byCurPwr == byPwr)
        return true;

    pDevice->byCurPwr = byPwr;

    switch (pDevice->byRFType) {

        case RF_AL2230 :
            if (pDevice->byCurPwr >= AL2230_PWR_IDX_LEN)
                return false;
            bResult &= IFRFbWriteEmbedded(pDevice, dwAL2230PowerTable[pDevice->byCurPwr]);
            if (uRATE <= RATE_11M)
                bResult &= IFRFbWriteEmbedded(pDevice, 0x0001B400+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW);
            else
                bResult &= IFRFbWriteEmbedded(pDevice, 0x0005A400+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW);
            break;

        case RF_AL2230S :
            if (pDevice->byCurPwr >= AL2230_PWR_IDX_LEN)
                return false;
            bResult &= IFRFbWriteEmbedded(pDevice, dwAL2230PowerTable[pDevice->byCurPwr]);
            if (uRATE <= RATE_11M) {
                bResult &= IFRFbWriteEmbedded(pDevice, 0x040C1400+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW);
                bResult &= IFRFbWriteEmbedded(pDevice, 0x00299B00+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW);
            }else {
                bResult &= IFRFbWriteEmbedded(pDevice, 0x0005A400+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW);
                bResult &= IFRFbWriteEmbedded(pDevice, 0x00099B00+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW);
            }
            break;


        case RF_AIROHA7230:
            {
                u32       dwMax7230Pwr;

                if (uRATE <= RATE_11M) { //RobertYu:20060426, for better 11b mask
                    bResult &= IFRFbWriteEmbedded(pDevice, 0x111BB900+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW);
                }
                else {
                    bResult &= IFRFbWriteEmbedded(pDevice, 0x221BB900+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW);
                }

                if (pDevice->byCurPwr > AL7230_PWR_IDX_LEN) return false;

                //  0x080F1B00 for 3 wire control TxGain(D10) and 0x31 as TX Gain value
                dwMax7230Pwr = 0x080C0B00 | ( (pDevice->byCurPwr) << 12 ) |
                                 (BY_AL7230_REG_LEN << 3 )  | IFREGCTL_REGW;

                bResult &= IFRFbWriteEmbedded(pDevice, dwMax7230Pwr);
                break;
            }
            break;

        case RF_VT3226: //RobertYu:20051111, VT3226C0 and before
        {
            u32       dwVT3226Pwr;

            if (pDevice->byCurPwr >= VT3226_PWR_IDX_LEN)
                return false;
            dwVT3226Pwr = ((0x3F-pDevice->byCurPwr) << 20 ) | ( 0x17 << 8 ) /* Reg7 */ |
                           (BY_VT3226_REG_LEN << 3 )  | IFREGCTL_REGW;
            bResult &= IFRFbWriteEmbedded(pDevice, dwVT3226Pwr);
            break;
        }

        case RF_VT3226D0: //RobertYu:20051228
        {
            u32       dwVT3226Pwr;

            if (pDevice->byCurPwr >= VT3226_PWR_IDX_LEN)
                return false;

            if (uRATE <= RATE_11M) {

                dwVT3226Pwr = ((0x3F-pDevice->byCurPwr) << 20 ) | ( 0xE07 << 8 ) /* Reg7 */ |   //RobertYu:20060420, TWIF 1.10
                               (BY_VT3226_REG_LEN << 3 )  | IFREGCTL_REGW;
                bResult &= IFRFbWriteEmbedded(pDevice, dwVT3226Pwr);

                bResult &= IFRFbWriteEmbedded(pDevice, 0x03C6A200+(BY_VT3226_REG_LEN<<3)+IFREGCTL_REGW);
		if (pDevice->vnt_mgmt.eScanState != WMAC_NO_SCANNING) {
			/* scanning, channel number is pDevice->uScanChannel */
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO
				"RFbRawSetPower> 11B mode uCurrChannel[%d]\n",
				pDevice->vnt_mgmt.uScanChannel);
			bResult &= IFRFbWriteEmbedded(pDevice,
				dwVT3226D0LoCurrentTable[pDevice->
					vnt_mgmt.uScanChannel - 1]);
		} else {
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO
				"RFbRawSetPower> 11B mode uCurrChannel[%d]\n",
				pDevice->vnt_mgmt.uCurrChannel);
			bResult &= IFRFbWriteEmbedded(pDevice,
				dwVT3226D0LoCurrentTable[pDevice->
					vnt_mgmt.uCurrChannel - 1]);
		}

                bResult &= IFRFbWriteEmbedded(pDevice, 0x015C0800+(BY_VT3226_REG_LEN<<3)+IFREGCTL_REGW); //RobertYu:20060420, ok now, new switching power (mini-pci can have bigger power consumption)
            } else {
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"@@@@ RFbRawSetPower> 11G mode\n");
                dwVT3226Pwr = ((0x3F-pDevice->byCurPwr) << 20 ) | ( 0x7 << 8 ) /* Reg7 */ |   //RobertYu:20060420, TWIF 1.10
                               (BY_VT3226_REG_LEN << 3 )  | IFREGCTL_REGW;
                bResult &= IFRFbWriteEmbedded(pDevice, dwVT3226Pwr);
                bResult &= IFRFbWriteEmbedded(pDevice, 0x00C6A200+(BY_VT3226_REG_LEN<<3)+IFREGCTL_REGW); //RobertYu:20060327
                bResult &= IFRFbWriteEmbedded(pDevice, 0x016BC600+(BY_VT3226_REG_LEN<<3)+IFREGCTL_REGW); //RobertYu:20060111
                bResult &= IFRFbWriteEmbedded(pDevice, 0x00900800+(BY_VT3226_REG_LEN<<3)+IFREGCTL_REGW); //RobertYu:20060111
            }
            break;
        }

        //{{RobertYu:20060609
        case RF_VT3342A0:
        {
            u32       dwVT3342Pwr;

            if (pDevice->byCurPwr >= VT3342_PWR_IDX_LEN)
                return false;

            dwVT3342Pwr =  ((0x3F-pDevice->byCurPwr) << 20 ) | ( 0x27 << 8 ) /* Reg7 */ |
                            (BY_VT3342_REG_LEN << 3 )  | IFREGCTL_REGW;
            bResult &= IFRFbWriteEmbedded(pDevice, dwVT3342Pwr);
            break;
        }

        default :
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
 *      pDevice         - The adapter to be translated
 *      byCurrRSSI      - RSSI to be translated
 *  Out:
 *      pdwdbm          - Translated dbm number
 *
 * Return Value: none
 *
-*/
void RFvRSSITodBm(struct vnt_private *pDevice, u8 byCurrRSSI, long *pldBm)
{
	u8 byIdx = (((byCurrRSSI & 0xC0) >> 6) & 0x03);
	signed long b = (byCurrRSSI & 0x3F);
	signed long a = 0;
	u8 abyAIROHARF[4] = {0, 18, 0, 40};

    switch (pDevice->byRFType) {
        case RF_AL2230:
        case RF_AL2230S:
        case RF_AIROHA7230:
        case RF_VT3226: //RobertYu:20051111
        case RF_VT3226D0:
        case RF_VT3342A0:   //RobertYu:20060609
            a = abyAIROHARF[byIdx];
            break;
        default:
            break;
    }

    *pldBm = -1 * (a + b * 2);
}



void RFbRFTableDownload(struct vnt_private *pDevice)
{
	u16 wLength1 = 0, wLength2 = 0, wLength3 = 0;
	u8 *pbyAddr1 = NULL, *pbyAddr2 = NULL, *pbyAddr3 = NULL;
	u16 wLength, wValue;
	u8 abyArray[256];

    switch ( pDevice->byRFType ) {
        case RF_AL2230:
        case RF_AL2230S:
            wLength1 = CB_AL2230_INIT_SEQ * 3;
            wLength2 = CB_MAX_CHANNEL_24G * 3;
            wLength3 = CB_MAX_CHANNEL_24G * 3;
            pbyAddr1 = &(abyAL2230InitTable[0][0]);
            pbyAddr2 = &(abyAL2230ChannelTable0[0][0]);
            pbyAddr3 = &(abyAL2230ChannelTable1[0][0]);
            break;
        case RF_AIROHA7230:
            wLength1 = CB_AL7230_INIT_SEQ * 3;
            wLength2 = CB_MAX_CHANNEL * 3;
            wLength3 = CB_MAX_CHANNEL * 3;
            pbyAddr1 = &(abyAL7230InitTable[0][0]);
            pbyAddr2 = &(abyAL7230ChannelTable0[0][0]);
            pbyAddr3 = &(abyAL7230ChannelTable1[0][0]);
            break;
        case RF_VT3226: //RobertYu:20051111
            wLength1 = CB_VT3226_INIT_SEQ * 3;
            wLength2 = CB_MAX_CHANNEL_24G * 3;
            wLength3 = CB_MAX_CHANNEL_24G * 3;
            pbyAddr1 = &(abyVT3226_InitTable[0][0]);
            pbyAddr2 = &(abyVT3226_ChannelTable0[0][0]);
            pbyAddr3 = &(abyVT3226_ChannelTable1[0][0]);
            break;
        case RF_VT3226D0: //RobertYu:20051114
            wLength1 = CB_VT3226_INIT_SEQ * 3;
            wLength2 = CB_MAX_CHANNEL_24G * 3;
            wLength3 = CB_MAX_CHANNEL_24G * 3;
            pbyAddr1 = &(abyVT3226D0_InitTable[0][0]);
            pbyAddr2 = &(abyVT3226_ChannelTable0[0][0]);
            pbyAddr3 = &(abyVT3226_ChannelTable1[0][0]);
            break;
        case RF_VT3342A0: //RobertYu:20060609
            wLength1 = CB_VT3342_INIT_SEQ * 3;
            wLength2 = CB_MAX_CHANNEL * 3;
            wLength3 = CB_MAX_CHANNEL * 3;
            pbyAddr1 = &(abyVT3342A0_InitTable[0][0]);
            pbyAddr2 = &(abyVT3342_ChannelTable0[0][0]);
            pbyAddr3 = &(abyVT3342_ChannelTable1[0][0]);
            break;

    }
    //Init Table

    memcpy(abyArray, pbyAddr1, wLength1);
    CONTROLnsRequestOut(pDevice,
                    MESSAGE_TYPE_WRITE,
                    0,
                    MESSAGE_REQUEST_RF_INIT,
                    wLength1,
                    abyArray
                    );
    //Channel Table 0
    wValue = 0;
    while ( wLength2 > 0 ) {

        if ( wLength2 >= 64 ) {
            wLength = 64;
        } else {
            wLength = wLength2;
        }
        memcpy(abyArray, pbyAddr2, wLength);
        CONTROLnsRequestOut(pDevice,
                        MESSAGE_TYPE_WRITE,
                        wValue,
                        MESSAGE_REQUEST_RF_CH0,
                        wLength,
                        abyArray);

        wLength2 -= wLength;
        wValue += wLength;
        pbyAddr2 += wLength;
    }
    //Channel table 1
    wValue = 0;
    while ( wLength3 > 0 ) {

        if ( wLength3 >= 64 ) {
            wLength = 64;
        } else {
            wLength = wLength3;
        }
        memcpy(abyArray, pbyAddr3, wLength);
        CONTROLnsRequestOut(pDevice,
                        MESSAGE_TYPE_WRITE,
                        wValue,
                        MESSAGE_REQUEST_RF_CH1,
                        wLength,
                        abyArray);

        wLength3 -= wLength;
        wValue += wLength;
        pbyAddr3 += wLength;
    }

    //7230 needs 2 InitTable and 3 Channel Table
    if ( pDevice->byRFType == RF_AIROHA7230 ) {
        wLength1 = CB_AL7230_INIT_SEQ * 3;
        wLength2 = CB_MAX_CHANNEL * 3;
        pbyAddr1 = &(abyAL7230InitTableAMode[0][0]);
        pbyAddr2 = &(abyAL7230ChannelTable2[0][0]);
        memcpy(abyArray, pbyAddr1, wLength1);
        //Init Table 2
        CONTROLnsRequestOut(pDevice,
                    MESSAGE_TYPE_WRITE,
                    0,
                    MESSAGE_REQUEST_RF_INIT2,
                    wLength1,
                    abyArray);

        //Channel Table 0
        wValue = 0;
        while ( wLength2 > 0 ) {

            if ( wLength2 >= 64 ) {
                wLength = 64;
            } else {
                wLength = wLength2;
            }
            memcpy(abyArray, pbyAddr2, wLength);
            CONTROLnsRequestOut(pDevice,
                            MESSAGE_TYPE_WRITE,
                            wValue,
                            MESSAGE_REQUEST_RF_CH2,
                            wLength,
                            abyArray);

            wLength2 -= wLength;
            wValue += wLength;
            pbyAddr2 += wLength;
        }
    }

}

int s_bVT3226D0_11bLoCurrentAdjust(struct vnt_private *pDevice, u8 byChannel,
	int b11bMode)
{
	int bResult = true;

	if (b11bMode)
		bResult &= IFRFbWriteEmbedded(pDevice,
			dwVT3226D0LoCurrentTable[byChannel-1]);
	else
		bResult &= IFRFbWriteEmbedded(pDevice, 0x016bc600 +
			(BY_VT3226_REG_LEN << 3) + IFREGCTL_REGW);

	return bResult;
}



/*

  Broadcom B43 wireless driver
  IEEE 802.11g LP-PHY and radio device data tables

  Copyright (c) 2009 Michael Buesch <mb@bu3sch.de>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
  Boston, MA 02110-1301, USA.

*/

#include "b43.h"
#include "tables_lpphy.h"
#include "phy_common.h"
#include "phy_lp.h"


/* Entry of the 2062 radio init table */
struct b2062_init_tab_entry {
	u16 offset;
	u16 value_a;
	u16 value_g;
	u8 flags;
};
#define B2062_FLAG_A	0x01 /* Flag: Init in A mode */
#define B2062_FLAG_G	0x02 /* Flag: Init in G mode */

static const struct b2062_init_tab_entry b2062_init_tab[] = {
	/* { .offset = B2062_N_COMM1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = 0x0001, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_COMM2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_COMM3, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2062_N_COMM4, .value_a = 0x0001, .value_g = 0x0000, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	/* { .offset = B2062_N_COMM5, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_COMM6, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_COMM7, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_COMM8, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_COMM9, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_COMM10, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_COMM11, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_COMM12, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_COMM13, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_COMM14, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_COMM15, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_PDN_CTL0, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2062_N_PDN_CTL1, .value_a = 0x0000, .value_g = 0x00CA, .flags = B2062_FLAG_G, },
	/* { .offset = B2062_N_PDN_CTL2, .value_a = 0x0018, .value_g = 0x0018, .flags = 0, }, */
	{ .offset = B2062_N_PDN_CTL3, .value_a = 0x0000, .value_g = 0x0000, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	{ .offset = B2062_N_PDN_CTL4, .value_a = 0x0015, .value_g = 0x002A, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	/* { .offset = B2062_N_GEN_CTL0, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_IQ_CALIB, .value_a = 0x0001, .value_g = 0x0001, .flags = 0, }, */
	{ .offset = B2062_N_LGENC, .value_a = 0x00DB, .value_g = 0x00FF, .flags = B2062_FLAG_A, },
	/* { .offset = B2062_N_LGENA_LPF, .value_a = 0x0001, .value_g = 0x0001, .flags = 0, }, */
	/* { .offset = B2062_N_LGENA_BIAS0, .value_a = 0x0041, .value_g = 0x0041, .flags = 0, }, */
	/* { .offset = B2062_N_LGNEA_BIAS1, .value_a = 0x0002, .value_g = 0x0002, .flags = 0, }, */
	/* { .offset = B2062_N_LGENA_CTL0, .value_a = 0x0032, .value_g = 0x0032, .flags = 0, }, */
	/* { .offset = B2062_N_LGENA_CTL1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_LGENA_CTL2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2062_N_LGENA_TUNE0, .value_a = 0x00DD, .value_g = 0x0000, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	/* { .offset = B2062_N_LGENA_TUNE1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2062_N_LGENA_TUNE2, .value_a = 0x00DD, .value_g = 0x0000, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	{ .offset = B2062_N_LGENA_TUNE3, .value_a = 0x0077, .value_g = 0x00B5, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	{ .offset = B2062_N_LGENA_CTL3, .value_a = 0x0000, .value_g = 0x00FF, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	/* { .offset = B2062_N_LGENA_CTL4, .value_a = 0x001F, .value_g = 0x001F, .flags = 0, }, */
	/* { .offset = B2062_N_LGENA_CTL5, .value_a = 0x0032, .value_g = 0x0032, .flags = 0, }, */
	/* { .offset = B2062_N_LGENA_CTL6, .value_a = 0x0032, .value_g = 0x0032, .flags = 0, }, */
	{ .offset = B2062_N_LGENA_CTL7, .value_a = 0x0033, .value_g = 0x0033, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	/* { .offset = B2062_N_RXA_CTL0, .value_a = 0x0009, .value_g = 0x0009, .flags = 0, }, */
	{ .offset = B2062_N_RXA_CTL1, .value_a = 0x0000, .value_g = 0x0000, .flags = B2062_FLAG_G, },
	/* { .offset = B2062_N_RXA_CTL2, .value_a = 0x0018, .value_g = 0x0018, .flags = 0, }, */
	/* { .offset = B2062_N_RXA_CTL3, .value_a = 0x0027, .value_g = 0x0027, .flags = 0, }, */
	/* { .offset = B2062_N_RXA_CTL4, .value_a = 0x0028, .value_g = 0x0028, .flags = 0, }, */
	/* { .offset = B2062_N_RXA_CTL5, .value_a = 0x0007, .value_g = 0x0007, .flags = 0, }, */
	/* { .offset = B2062_N_RXA_CTL6, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_RXA_CTL7, .value_a = 0x0008, .value_g = 0x0008, .flags = 0, }, */
	{ .offset = B2062_N_RXBB_CTL0, .value_a = 0x0082, .value_g = 0x0080, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	/* { .offset = B2062_N_RXBB_CTL1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_RXBB_CTL2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_RXBB_GAIN0, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2062_N_RXBB_GAIN1, .value_a = 0x0004, .value_g = 0x0004, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	{ .offset = B2062_N_RXBB_GAIN2, .value_a = 0x0000, .value_g = 0x0000, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	/* { .offset = B2062_N_RXBB_GAIN3, .value_a = 0x0011, .value_g = 0x0011, .flags = 0, }, */
	/* { .offset = B2062_N_RXBB_RSSI0, .value_a = 0x0043, .value_g = 0x0043, .flags = 0, }, */
	/* { .offset = B2062_N_RXBB_RSSI1, .value_a = 0x0033, .value_g = 0x0033, .flags = 0, }, */
	/* { .offset = B2062_N_RXBB_CALIB0, .value_a = 0x0010, .value_g = 0x0010, .flags = 0, }, */
	/* { .offset = B2062_N_RXBB_CALIB1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_RXBB_CALIB2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_RXBB_BIAS0, .value_a = 0x0006, .value_g = 0x0006, .flags = 0, }, */
	/* { .offset = B2062_N_RXBB_BIAS1, .value_a = 0x002A, .value_g = 0x002A, .flags = 0, }, */
	/* { .offset = B2062_N_RXBB_BIAS2, .value_a = 0x00AA, .value_g = 0x00AA, .flags = 0, }, */
	/* { .offset = B2062_N_RXBB_BIAS3, .value_a = 0x0021, .value_g = 0x0021, .flags = 0, }, */
	/* { .offset = B2062_N_RXBB_BIAS4, .value_a = 0x00AA, .value_g = 0x00AA, .flags = 0, }, */
	/* { .offset = B2062_N_RXBB_BIAS5, .value_a = 0x0022, .value_g = 0x0022, .flags = 0, }, */
	/* { .offset = B2062_N_RXBB_RSSI2, .value_a = 0x0001, .value_g = 0x0001, .flags = 0, }, */
	/* { .offset = B2062_N_RXBB_RSSI3, .value_a = 0x0055, .value_g = 0x0055, .flags = 0, }, */
	/* { .offset = B2062_N_RXBB_RSSI4, .value_a = 0x0001, .value_g = 0x0001, .flags = 0, }, */
	/* { .offset = B2062_N_RXBB_RSSI5, .value_a = 0x0055, .value_g = 0x0055, .flags = 0, }, */
	/* { .offset = B2062_N_TX_CTL0, .value_a = 0x0001, .value_g = 0x0001, .flags = 0, }, */
	/* { .offset = B2062_N_TX_CTL1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_TX_CTL2, .value_a = 0x0084, .value_g = 0x0084, .flags = 0, }, */
	/* { .offset = B2062_N_TX_CTL3, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2062_N_TX_CTL4, .value_a = 0x0003, .value_g = 0x0003, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	{ .offset = B2062_N_TX_CTL5, .value_a = 0x0002, .value_g = 0x0002, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	/* { .offset = B2062_N_TX_CTL6, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_TX_CTL7, .value_a = 0x0058, .value_g = 0x0058, .flags = 0, }, */
	/* { .offset = B2062_N_TX_CTL8, .value_a = 0x0082, .value_g = 0x0082, .flags = 0, }, */
	/* { .offset = B2062_N_TX_CTL9, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_TX_CTL_A, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_TX_GC2G, .value_a = 0x00FF, .value_g = 0x00FF, .flags = 0, }, */
	/* { .offset = B2062_N_TX_GC5G, .value_a = 0x00FF, .value_g = 0x00FF, .flags = 0, }, */
	{ .offset = B2062_N_TX_TUNE, .value_a = 0x0088, .value_g = 0x001B, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	/* { .offset = B2062_N_TX_PAD, .value_a = 0x0088, .value_g = 0x0088, .flags = 0, }, */
	/* { .offset = B2062_N_TX_PGA, .value_a = 0x0088, .value_g = 0x0088, .flags = 0, }, */
	/* { .offset = B2062_N_TX_PADAUX, .value_a = 0x0033, .value_g = 0x0033, .flags = 0, }, */
	/* { .offset = B2062_N_TX_PGAAUX, .value_a = 0x0033, .value_g = 0x0033, .flags = 0, }, */
	/* { .offset = B2062_N_TSSI_CTL0, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_TSSI_CTL1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_TSSI_CTL2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_IQ_CALIB_CTL0, .value_a = 0x0033, .value_g = 0x0033, .flags = 0, }, */
	/* { .offset = B2062_N_IQ_CALIB_CTL1, .value_a = 0x0055, .value_g = 0x0055, .flags = 0, }, */
	/* { .offset = B2062_N_IQ_CALIB_CTL2, .value_a = 0x0032, .value_g = 0x0032, .flags = 0, }, */
	/* { .offset = B2062_N_CALIB_TS, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_CALIB_CTL0, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_CALIB_CTL1, .value_a = 0x0015, .value_g = 0x0015, .flags = 0, }, */
	/* { .offset = B2062_N_CALIB_CTL2, .value_a = 0x000F, .value_g = 0x000F, .flags = 0, }, */
	/* { .offset = B2062_N_CALIB_CTL3, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_CALIB_CTL4, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_CALIB_DBG0, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_CALIB_DBG1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_CALIB_DBG2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_CALIB_DBG3, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_PSENSE_CTL0, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_PSENSE_CTL1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_PSENSE_CTL2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_TEST_BUF0, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_COMM1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_RADIO_ID_CODE, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_COMM2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_COMM3, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2062_S_COMM4, .value_a = 0x0001, .value_g = 0x0000, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	/* { .offset = B2062_S_COMM5, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_COMM6, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_COMM7, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_COMM8, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_COMM9, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_COMM10, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_COMM11, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_COMM12, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_COMM13, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_COMM14, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_COMM15, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2062_S_PDS_CTL0, .value_a = 0x00FF, .value_g = 0x00FF, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	/* { .offset = B2062_S_PDS_CTL1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_PDS_CTL2, .value_a = 0x008E, .value_g = 0x008E, .flags = 0, }, */
	/* { .offset = B2062_S_PDS_CTL3, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_BG_CTL0, .value_a = 0x0006, .value_g = 0x0006, .flags = 0, }, */
	/* { .offset = B2062_S_BG_CTL1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_BG_CTL2, .value_a = 0x0011, .value_g = 0x0011, .flags = 0, }, */
	{ .offset = B2062_S_LGENG_CTL0, .value_a = 0x00F8, .value_g = 0x00D8, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	{ .offset = B2062_S_LGENG_CTL1, .value_a = 0x003C, .value_g = 0x0024, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	/* { .offset = B2062_S_LGENG_CTL2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_LGENG_CTL3, .value_a = 0x0041, .value_g = 0x0041, .flags = 0, }, */
	/* { .offset = B2062_S_LGENG_CTL4, .value_a = 0x0002, .value_g = 0x0002, .flags = 0, }, */
	/* { .offset = B2062_S_LGENG_CTL5, .value_a = 0x0033, .value_g = 0x0033, .flags = 0, }, */
	/* { .offset = B2062_S_LGENG_CTL6, .value_a = 0x0022, .value_g = 0x0022, .flags = 0, }, */
	/* { .offset = B2062_S_LGENG_CTL7, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2062_S_LGENG_CTL8, .value_a = 0x0088, .value_g = 0x0080, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	/* { .offset = B2062_S_LGENG_CTL9, .value_a = 0x0088, .value_g = 0x0088, .flags = 0, }, */
	{ .offset = B2062_S_LGENG_CTL10, .value_a = 0x0088, .value_g = 0x0080, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	/* { .offset = B2062_S_LGENG_CTL11, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_REFPLL_CTL0, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_REFPLL_CTL1, .value_a = 0x0007, .value_g = 0x0007, .flags = 0, }, */
	/* { .offset = B2062_S_REFPLL_CTL2, .value_a = 0x00AF, .value_g = 0x00AF, .flags = 0, }, */
	/* { .offset = B2062_S_REFPLL_CTL3, .value_a = 0x0012, .value_g = 0x0012, .flags = 0, }, */
	/* { .offset = B2062_S_REFPLL_CTL4, .value_a = 0x000B, .value_g = 0x000B, .flags = 0, }, */
	/* { .offset = B2062_S_REFPLL_CTL5, .value_a = 0x005F, .value_g = 0x005F, .flags = 0, }, */
	/* { .offset = B2062_S_REFPLL_CTL6, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_REFPLL_CTL7, .value_a = 0x0040, .value_g = 0x0040, .flags = 0, }, */
	/* { .offset = B2062_S_REFPLL_CTL8, .value_a = 0x0052, .value_g = 0x0052, .flags = 0, }, */
	/* { .offset = B2062_S_REFPLL_CTL9, .value_a = 0x0026, .value_g = 0x0026, .flags = 0, }, */
	/* { .offset = B2062_S_REFPLL_CTL10, .value_a = 0x0003, .value_g = 0x0003, .flags = 0, }, */
	/* { .offset = B2062_S_REFPLL_CTL11, .value_a = 0x0036, .value_g = 0x0036, .flags = 0, }, */
	/* { .offset = B2062_S_REFPLL_CTL12, .value_a = 0x0057, .value_g = 0x0057, .flags = 0, }, */
	/* { .offset = B2062_S_REFPLL_CTL13, .value_a = 0x0011, .value_g = 0x0011, .flags = 0, }, */
	/* { .offset = B2062_S_REFPLL_CTL14, .value_a = 0x0075, .value_g = 0x0075, .flags = 0, }, */
	/* { .offset = B2062_S_REFPLL_CTL15, .value_a = 0x00B4, .value_g = 0x00B4, .flags = 0, }, */
	/* { .offset = B2062_S_REFPLL_CTL16, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2062_S_RFPLL_CTL0, .value_a = 0x0098, .value_g = 0x0098, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL1, .value_a = 0x0010, .value_g = 0x0010, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	/* { .offset = B2062_S_RFPLL_CTL2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_RFPLL_CTL3, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_RFPLL_CTL4, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2062_S_RFPLL_CTL5, .value_a = 0x0043, .value_g = 0x0043, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL6, .value_a = 0x0047, .value_g = 0x0047, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL7, .value_a = 0x000C, .value_g = 0x000C, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL8, .value_a = 0x0011, .value_g = 0x0011, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL9, .value_a = 0x0011, .value_g = 0x0011, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL10, .value_a = 0x000E, .value_g = 0x000E, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL11, .value_a = 0x0008, .value_g = 0x0008, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL12, .value_a = 0x0033, .value_g = 0x0033, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL13, .value_a = 0x000A, .value_g = 0x000A, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL14, .value_a = 0x0006, .value_g = 0x0006, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	/* { .offset = B2062_S_RFPLL_CTL15, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_RFPLL_CTL16, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_RFPLL_CTL17, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2062_S_RFPLL_CTL18, .value_a = 0x003E, .value_g = 0x003E, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL19, .value_a = 0x0013, .value_g = 0x0013, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	/* { .offset = B2062_S_RFPLL_CTL20, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2062_S_RFPLL_CTL21, .value_a = 0x0062, .value_g = 0x0062, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL22, .value_a = 0x0007, .value_g = 0x0007, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL23, .value_a = 0x0016, .value_g = 0x0016, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL24, .value_a = 0x005C, .value_g = 0x005C, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL25, .value_a = 0x0095, .value_g = 0x0095, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	/* { .offset = B2062_S_RFPLL_CTL26, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_RFPLL_CTL27, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_RFPLL_CTL28, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_RFPLL_CTL29, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2062_S_RFPLL_CTL30, .value_a = 0x00A0, .value_g = 0x00A0, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL31, .value_a = 0x0004, .value_g = 0x0004, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	/* { .offset = B2062_S_RFPLL_CTL32, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2062_S_RFPLL_CTL33, .value_a = 0x00CC, .value_g = 0x00CC, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL34, .value_a = 0x0007, .value_g = 0x0007, .flags = B2062_FLAG_A | B2062_FLAG_G, },
	/* { .offset = B2062_S_RXG_CNT0, .value_a = 0x0010, .value_g = 0x0010, .flags = 0, }, */
	/* { .offset = B2062_S_RXG_CNT1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_RXG_CNT2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_RXG_CNT3, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_RXG_CNT4, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_RXG_CNT5, .value_a = 0x0055, .value_g = 0x0055, .flags = 0, }, */
	/* { .offset = B2062_S_RXG_CNT6, .value_a = 0x0055, .value_g = 0x0055, .flags = 0, }, */
	/* { .offset = B2062_S_RXG_CNT7, .value_a = 0x0005, .value_g = 0x0005, .flags = 0, }, */
	{ .offset = B2062_S_RXG_CNT8, .value_a = 0x000F, .value_g = 0x000F, .flags = B2062_FLAG_A, },
	/* { .offset = B2062_S_RXG_CNT9, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_RXG_CNT10, .value_a = 0x0055, .value_g = 0x0055, .flags = 0, }, */
	/* { .offset = B2062_S_RXG_CNT11, .value_a = 0x0066, .value_g = 0x0066, .flags = 0, }, */
	/* { .offset = B2062_S_RXG_CNT12, .value_a = 0x0055, .value_g = 0x0055, .flags = 0, }, */
	/* { .offset = B2062_S_RXG_CNT13, .value_a = 0x0044, .value_g = 0x0044, .flags = 0, }, */
	/* { .offset = B2062_S_RXG_CNT14, .value_a = 0x00A0, .value_g = 0x00A0, .flags = 0, }, */
	/* { .offset = B2062_S_RXG_CNT15, .value_a = 0x0004, .value_g = 0x0004, .flags = 0, }, */
	/* { .offset = B2062_S_RXG_CNT16, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_RXG_CNT17, .value_a = 0x0055, .value_g = 0x0055, .flags = 0, }, */
};

void b2062_upload_init_table(struct b43_wldev *dev)
{
	const struct b2062_init_tab_entry *e;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(b2062_init_tab); i++) {
		e = &b2062_init_tab[i];
		if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
			if (!(e->flags & B2062_FLAG_G))
				continue;
			b43_radio_write(dev, e->offset, e->value_g);
		} else {
			if (!(e->flags & B2062_FLAG_A))
				continue;
			b43_radio_write(dev, e->offset, e->value_a);
		}
	}
}

u32 b43_lptab_read(struct b43_wldev *dev, u32 offset)
{
	u32 type, value;

	type = offset & B43_LPTAB_TYPEMASK;
	offset &= ~B43_LPTAB_TYPEMASK;
	B43_WARN_ON(offset > 0xFFFF);

	switch (type) {
	case B43_LPTAB_8BIT:
		b43_phy_write(dev, B43_LPPHY_TABLE_ADDR, offset);
		value = b43_phy_read(dev, B43_LPPHY_TABLEDATALO) & 0xFF;
		break;
	case B43_LPTAB_16BIT:
		b43_phy_write(dev, B43_LPPHY_TABLE_ADDR, offset);
		value = b43_phy_read(dev, B43_LPPHY_TABLEDATALO);
		break;
	case B43_LPTAB_32BIT:
		b43_phy_write(dev, B43_LPPHY_TABLE_ADDR, offset);
		value = b43_phy_read(dev, B43_LPPHY_TABLEDATAHI);
		value <<= 16;
		value |= b43_phy_read(dev, B43_LPPHY_TABLEDATALO);
		break;
	default:
		B43_WARN_ON(1);
		value = 0;
	}

	return value;
}

void b43_lptab_read_bulk(struct b43_wldev *dev, u32 offset,
			 unsigned int nr_elements, void *_data)
{
	u32 type, value;
	u8 *data = _data;
	unsigned int i;

	type = offset & B43_LPTAB_TYPEMASK;
	for (i = 0; i < nr_elements; i++) {
		value = b43_lptab_read(dev, offset);
		switch (type) {
		case B43_LPTAB_8BIT:
			*data = value;
			data++;
			break;
		case B43_LPTAB_16BIT:
			*((u16 *)data) = value;
			data += 2;
			break;
		case B43_LPTAB_32BIT:
			*((u32 *)data) = value;
			data += 4;
			break;
		default:
			B43_WARN_ON(1);
		}
		offset++;
	}
}

void b43_lptab_write(struct b43_wldev *dev, u32 offset, u32 value)
{
	u32 type;

	type = offset & B43_LPTAB_TYPEMASK;
	offset &= ~B43_LPTAB_TYPEMASK;
	B43_WARN_ON(offset > 0xFFFF);

	switch (type) {
	case B43_LPTAB_8BIT:
		B43_WARN_ON(value & ~0xFF);
		b43_phy_write(dev, B43_LPPHY_TABLE_ADDR, offset);
		b43_phy_write(dev, B43_LPPHY_TABLEDATALO, value);
		break;
	case B43_LPTAB_16BIT:
		B43_WARN_ON(value & ~0xFFFF);
		b43_phy_write(dev, B43_LPPHY_TABLE_ADDR, offset);
		b43_phy_write(dev, B43_LPPHY_TABLEDATALO, value);
		break;
	case B43_LPTAB_32BIT:
		b43_phy_write(dev, B43_LPPHY_TABLE_ADDR, offset);
		b43_phy_write(dev, B43_LPPHY_TABLEDATAHI, value >> 16);
		b43_phy_write(dev, B43_LPPHY_TABLEDATALO, value);
		break;
	default:
		B43_WARN_ON(1);
	}
}

void b43_lptab_write_bulk(struct b43_wldev *dev, u32 offset,
			  unsigned int nr_elements, const void *_data)
{
	u32 type, value;
	const u8 *data = _data;
	unsigned int i;

	type = offset & B43_LPTAB_TYPEMASK;
	for (i = 0; i < nr_elements; i++) {
		switch (type) {
		case B43_LPTAB_8BIT:
			value = *data;
			data++;
			break;
		case B43_LPTAB_16BIT:
			value = *((u16 *)data);
			data += 2;
			break;
		case B43_LPTAB_32BIT:
			value = *((u32 *)data);
			data += 4;
			break;
		default:
			B43_WARN_ON(1);
			value = 0;
		}
		b43_lptab_write(dev, offset, value);
		offset++;
	}
}

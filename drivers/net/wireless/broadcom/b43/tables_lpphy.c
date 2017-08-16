/*

  Broadcom B43 wireless driver
  IEEE 802.11a/g LP-PHY and radio device data tables

  Copyright (c) 2009 Michael Buesch <m@bues.ch>
  Copyright (c) 2009 Gábor Stefanik <netrolller.3d@gmail.com>

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


/* Entry of the 2062/2063 radio init table */
struct b206x_init_tab_entry {
	u16 offset;
	u16 value_a;
	u16 value_g;
	u8 flags;
};
#define B206X_FLAG_A	0x01 /* Flag: Init in A mode */
#define B206X_FLAG_G	0x02 /* Flag: Init in G mode */

static const struct b206x_init_tab_entry b2062_init_tab[] = {
	/* { .offset = B2062_N_COMM1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = 0x0001, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_COMM2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_COMM3, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2062_N_COMM4, .value_a = 0x0001, .value_g = 0x0000, .flags = B206X_FLAG_A | B206X_FLAG_G, },
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
	{ .offset = B2062_N_PDN_CTL1, .value_a = 0x0000, .value_g = 0x00CA, .flags = B206X_FLAG_G, },
	/* { .offset = B2062_N_PDN_CTL2, .value_a = 0x0018, .value_g = 0x0018, .flags = 0, }, */
	{ .offset = B2062_N_PDN_CTL3, .value_a = 0x0000, .value_g = 0x0000, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2062_N_PDN_CTL4, .value_a = 0x0015, .value_g = 0x002A, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2062_N_GEN_CTL0, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_IQ_CALIB, .value_a = 0x0001, .value_g = 0x0001, .flags = 0, }, */
	{ .offset = B2062_N_LGENC, .value_a = 0x00DB, .value_g = 0x00FF, .flags = B206X_FLAG_A, },
	/* { .offset = B2062_N_LGENA_LPF, .value_a = 0x0001, .value_g = 0x0001, .flags = 0, }, */
	/* { .offset = B2062_N_LGENA_BIAS0, .value_a = 0x0041, .value_g = 0x0041, .flags = 0, }, */
	/* { .offset = B2062_N_LGNEA_BIAS1, .value_a = 0x0002, .value_g = 0x0002, .flags = 0, }, */
	/* { .offset = B2062_N_LGENA_CTL0, .value_a = 0x0032, .value_g = 0x0032, .flags = 0, }, */
	/* { .offset = B2062_N_LGENA_CTL1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_LGENA_CTL2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2062_N_LGENA_TUNE0, .value_a = 0x00DD, .value_g = 0x0000, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2062_N_LGENA_TUNE1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2062_N_LGENA_TUNE2, .value_a = 0x00DD, .value_g = 0x0000, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2062_N_LGENA_TUNE3, .value_a = 0x0077, .value_g = 0x00B5, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2062_N_LGENA_CTL3, .value_a = 0x0000, .value_g = 0x00FF, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2062_N_LGENA_CTL4, .value_a = 0x001F, .value_g = 0x001F, .flags = 0, }, */
	/* { .offset = B2062_N_LGENA_CTL5, .value_a = 0x0032, .value_g = 0x0032, .flags = 0, }, */
	/* { .offset = B2062_N_LGENA_CTL6, .value_a = 0x0032, .value_g = 0x0032, .flags = 0, }, */
	{ .offset = B2062_N_LGENA_CTL7, .value_a = 0x0033, .value_g = 0x0033, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2062_N_RXA_CTL0, .value_a = 0x0009, .value_g = 0x0009, .flags = 0, }, */
	{ .offset = B2062_N_RXA_CTL1, .value_a = 0x0000, .value_g = 0x0000, .flags = B206X_FLAG_G, },
	/* { .offset = B2062_N_RXA_CTL2, .value_a = 0x0018, .value_g = 0x0018, .flags = 0, }, */
	/* { .offset = B2062_N_RXA_CTL3, .value_a = 0x0027, .value_g = 0x0027, .flags = 0, }, */
	/* { .offset = B2062_N_RXA_CTL4, .value_a = 0x0028, .value_g = 0x0028, .flags = 0, }, */
	/* { .offset = B2062_N_RXA_CTL5, .value_a = 0x0007, .value_g = 0x0007, .flags = 0, }, */
	/* { .offset = B2062_N_RXA_CTL6, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_RXA_CTL7, .value_a = 0x0008, .value_g = 0x0008, .flags = 0, }, */
	{ .offset = B2062_N_RXBB_CTL0, .value_a = 0x0082, .value_g = 0x0080, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2062_N_RXBB_CTL1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_RXBB_CTL2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_RXBB_GAIN0, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2062_N_RXBB_GAIN1, .value_a = 0x0004, .value_g = 0x0004, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2062_N_RXBB_GAIN2, .value_a = 0x0000, .value_g = 0x0000, .flags = B206X_FLAG_A | B206X_FLAG_G, },
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
	{ .offset = B2062_N_TX_CTL4, .value_a = 0x0003, .value_g = 0x0003, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2062_N_TX_CTL5, .value_a = 0x0002, .value_g = 0x0002, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2062_N_TX_CTL6, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_TX_CTL7, .value_a = 0x0058, .value_g = 0x0058, .flags = 0, }, */
	/* { .offset = B2062_N_TX_CTL8, .value_a = 0x0082, .value_g = 0x0082, .flags = 0, }, */
	/* { .offset = B2062_N_TX_CTL9, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_TX_CTL_A, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_N_TX_GC2G, .value_a = 0x00FF, .value_g = 0x00FF, .flags = 0, }, */
	/* { .offset = B2062_N_TX_GC5G, .value_a = 0x00FF, .value_g = 0x00FF, .flags = 0, }, */
	{ .offset = B2062_N_TX_TUNE, .value_a = 0x0088, .value_g = 0x001B, .flags = B206X_FLAG_A | B206X_FLAG_G, },
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
	{ .offset = B2062_S_COMM4, .value_a = 0x0001, .value_g = 0x0000, .flags = B206X_FLAG_A | B206X_FLAG_G, },
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
	{ .offset = B2062_S_PDS_CTL0, .value_a = 0x00FF, .value_g = 0x00FF, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2062_S_PDS_CTL1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_PDS_CTL2, .value_a = 0x008E, .value_g = 0x008E, .flags = 0, }, */
	/* { .offset = B2062_S_PDS_CTL3, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_BG_CTL0, .value_a = 0x0006, .value_g = 0x0006, .flags = 0, }, */
	/* { .offset = B2062_S_BG_CTL1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_BG_CTL2, .value_a = 0x0011, .value_g = 0x0011, .flags = 0, }, */
	{ .offset = B2062_S_LGENG_CTL0, .value_a = 0x00F8, .value_g = 0x00D8, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2062_S_LGENG_CTL1, .value_a = 0x003C, .value_g = 0x0024, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2062_S_LGENG_CTL2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_LGENG_CTL3, .value_a = 0x0041, .value_g = 0x0041, .flags = 0, }, */
	/* { .offset = B2062_S_LGENG_CTL4, .value_a = 0x0002, .value_g = 0x0002, .flags = 0, }, */
	/* { .offset = B2062_S_LGENG_CTL5, .value_a = 0x0033, .value_g = 0x0033, .flags = 0, }, */
	/* { .offset = B2062_S_LGENG_CTL6, .value_a = 0x0022, .value_g = 0x0022, .flags = 0, }, */
	/* { .offset = B2062_S_LGENG_CTL7, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2062_S_LGENG_CTL8, .value_a = 0x0088, .value_g = 0x0080, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2062_S_LGENG_CTL9, .value_a = 0x0088, .value_g = 0x0088, .flags = 0, }, */
	{ .offset = B2062_S_LGENG_CTL10, .value_a = 0x0088, .value_g = 0x0080, .flags = B206X_FLAG_A | B206X_FLAG_G, },
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
	{ .offset = B2062_S_RFPLL_CTL0, .value_a = 0x0098, .value_g = 0x0098, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL1, .value_a = 0x0010, .value_g = 0x0010, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2062_S_RFPLL_CTL2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_RFPLL_CTL3, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_RFPLL_CTL4, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2062_S_RFPLL_CTL5, .value_a = 0x0043, .value_g = 0x0043, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL6, .value_a = 0x0047, .value_g = 0x0047, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL7, .value_a = 0x000C, .value_g = 0x000C, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL8, .value_a = 0x0011, .value_g = 0x0011, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL9, .value_a = 0x0011, .value_g = 0x0011, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL10, .value_a = 0x000E, .value_g = 0x000E, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL11, .value_a = 0x0008, .value_g = 0x0008, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL12, .value_a = 0x0033, .value_g = 0x0033, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL13, .value_a = 0x000A, .value_g = 0x000A, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL14, .value_a = 0x0006, .value_g = 0x0006, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2062_S_RFPLL_CTL15, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_RFPLL_CTL16, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_RFPLL_CTL17, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2062_S_RFPLL_CTL18, .value_a = 0x003E, .value_g = 0x003E, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL19, .value_a = 0x0013, .value_g = 0x0013, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2062_S_RFPLL_CTL20, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2062_S_RFPLL_CTL21, .value_a = 0x0062, .value_g = 0x0062, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL22, .value_a = 0x0007, .value_g = 0x0007, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL23, .value_a = 0x0016, .value_g = 0x0016, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL24, .value_a = 0x005C, .value_g = 0x005C, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL25, .value_a = 0x0095, .value_g = 0x0095, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2062_S_RFPLL_CTL26, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_RFPLL_CTL27, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_RFPLL_CTL28, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_RFPLL_CTL29, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2062_S_RFPLL_CTL30, .value_a = 0x00A0, .value_g = 0x00A0, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL31, .value_a = 0x0004, .value_g = 0x0004, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2062_S_RFPLL_CTL32, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2062_S_RFPLL_CTL33, .value_a = 0x00CC, .value_g = 0x00CC, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2062_S_RFPLL_CTL34, .value_a = 0x0007, .value_g = 0x0007, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2062_S_RXG_CNT0, .value_a = 0x0010, .value_g = 0x0010, .flags = 0, }, */
	/* { .offset = B2062_S_RXG_CNT1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_RXG_CNT2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_RXG_CNT3, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_RXG_CNT4, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2062_S_RXG_CNT5, .value_a = 0x0055, .value_g = 0x0055, .flags = 0, }, */
	/* { .offset = B2062_S_RXG_CNT6, .value_a = 0x0055, .value_g = 0x0055, .flags = 0, }, */
	/* { .offset = B2062_S_RXG_CNT7, .value_a = 0x0005, .value_g = 0x0005, .flags = 0, }, */
	{ .offset = B2062_S_RXG_CNT8, .value_a = 0x000F, .value_g = 0x000F, .flags = B206X_FLAG_A, },
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

static const struct b206x_init_tab_entry b2063_init_tab[] = {
	{ .offset = B2063_COMM1, .value_a = 0x0000, .value_g = 0x0000, .flags = B206X_FLAG_G, },
	/* { .offset = B2063_COMM2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_COMM3, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_COMM4, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_COMM5, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_COMM6, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_COMM7, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_COMM8, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_COMM9, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2063_COMM10, .value_a = 0x0001, .value_g = 0x0000, .flags = B206X_FLAG_A, },
	/* { .offset = B2063_COMM11, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_COMM12, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_COMM13, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_COMM14, .value_a = 0x0006, .value_g = 0x0006, .flags = 0, }, */
	/* { .offset = B2063_COMM15, .value_a = 0x000f, .value_g = 0x000f, .flags = 0, }, */
	{ .offset = B2063_COMM16, .value_a = 0x0000, .value_g = 0x0000, .flags = B206X_FLAG_G, },
	{ .offset = B2063_COMM17, .value_a = 0x0000, .value_g = 0x0000, .flags = B206X_FLAG_G, },
	{ .offset = B2063_COMM18, .value_a = 0x0000, .value_g = 0x0000, .flags = B206X_FLAG_G, },
	{ .offset = B2063_COMM19, .value_a = 0x0000, .value_g = 0x0000, .flags = B206X_FLAG_G, },
	{ .offset = B2063_COMM20, .value_a = 0x0000, .value_g = 0x0000, .flags = B206X_FLAG_G, },
	{ .offset = B2063_COMM21, .value_a = 0x0000, .value_g = 0x0000, .flags = B206X_FLAG_G, },
	{ .offset = B2063_COMM22, .value_a = 0x0000, .value_g = 0x0000, .flags = B206X_FLAG_G, },
	{ .offset = B2063_COMM23, .value_a = 0x0000, .value_g = 0x0000, .flags = B206X_FLAG_G, },
	{ .offset = B2063_COMM24, .value_a = 0x0000, .value_g = 0x0000, .flags = B206X_FLAG_G, },
	/* { .offset = B2063_PWR_SWITCH_CTL, .value_a = 0x007f, .value_g = 0x007f, .flags = 0, }, */
	/* { .offset = B2063_PLL_SP1, .value_a = 0x003f, .value_g = 0x003f, .flags = 0, }, */
	/* { .offset = B2063_PLL_SP2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2063_LOGEN_SP1, .value_a = 0x00e8, .value_g = 0x00d4, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2063_LOGEN_SP2, .value_a = 0x00a7, .value_g = 0x0053, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2063_LOGEN_SP3, .value_a = 0x00ff, .value_g = 0x00ff, .flags = 0, }, */
	{ .offset = B2063_LOGEN_SP4, .value_a = 0x00f0, .value_g = 0x000f, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2063_LOGEN_SP5, .value_a = 0x0001, .value_g = 0x0001, .flags = 0, }, */
	{ .offset = B2063_G_RX_SP1, .value_a = 0x001f, .value_g = 0x005e, .flags = B206X_FLAG_G, },
	{ .offset = B2063_G_RX_SP2, .value_a = 0x007f, .value_g = 0x007e, .flags = B206X_FLAG_G, },
	{ .offset = B2063_G_RX_SP3, .value_a = 0x0030, .value_g = 0x00f0, .flags = B206X_FLAG_G, },
	/* { .offset = B2063_G_RX_SP4, .value_a = 0x0035, .value_g = 0x0035, .flags = 0, }, */
	/* { .offset = B2063_G_RX_SP5, .value_a = 0x003f, .value_g = 0x003f, .flags = 0, }, */
	/* { .offset = B2063_G_RX_SP6, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2063_G_RX_SP7, .value_a = 0x007f, .value_g = 0x007f, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2063_G_RX_SP8, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_G_RX_SP9, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2063_G_RX_SP10, .value_a = 0x000c, .value_g = 0x000c, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2063_G_RX_SP11, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2063_A_RX_SP1, .value_a = 0x003c, .value_g = 0x003f, .flags = B206X_FLAG_A, },
	{ .offset = B2063_A_RX_SP2, .value_a = 0x00fc, .value_g = 0x00fe, .flags = B206X_FLAG_A, },
	/* { .offset = B2063_A_RX_SP3, .value_a = 0x00ff, .value_g = 0x00ff, .flags = 0, }, */
	/* { .offset = B2063_A_RX_SP4, .value_a = 0x00ff, .value_g = 0x00ff, .flags = 0, }, */
	/* { .offset = B2063_A_RX_SP5, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_A_RX_SP6, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2063_A_RX_SP7, .value_a = 0x0008, .value_g = 0x0008, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2063_RX_BB_SP1, .value_a = 0x000f, .value_g = 0x000f, .flags = 0, }, */
	/* { .offset = B2063_RX_BB_SP2, .value_a = 0x0022, .value_g = 0x0022, .flags = 0, }, */
	/* { .offset = B2063_RX_BB_SP3, .value_a = 0x00a8, .value_g = 0x00a8, .flags = 0, }, */
	{ .offset = B2063_RX_BB_SP4, .value_a = 0x0060, .value_g = 0x0060, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2063_RX_BB_SP5, .value_a = 0x0011, .value_g = 0x0011, .flags = 0, }, */
	/* { .offset = B2063_RX_BB_SP6, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_RX_BB_SP7, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2063_RX_BB_SP8, .value_a = 0x0030, .value_g = 0x0030, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2063_TX_RF_SP1, .value_a = 0x0001, .value_g = 0x0001, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_SP2, .value_a = 0x0003, .value_g = 0x0003, .flags = 0, }, */
	{ .offset = B2063_TX_RF_SP3, .value_a = 0x000c, .value_g = 0x000b, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2063_TX_RF_SP4, .value_a = 0x0010, .value_g = 0x000f, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2063_TX_RF_SP5, .value_a = 0x000f, .value_g = 0x000f, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_SP6, .value_a = 0x0080, .value_g = 0x0080, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_SP7, .value_a = 0x0068, .value_g = 0x0068, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_SP8, .value_a = 0x0068, .value_g = 0x0068, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_SP9, .value_a = 0x0080, .value_g = 0x0080, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_SP10, .value_a = 0x00ff, .value_g = 0x00ff, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_SP11, .value_a = 0x0003, .value_g = 0x0003, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_SP12, .value_a = 0x0038, .value_g = 0x0038, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_SP13, .value_a = 0x00ff, .value_g = 0x00ff, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_SP14, .value_a = 0x0038, .value_g = 0x0038, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_SP15, .value_a = 0x00c0, .value_g = 0x00c0, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_SP16, .value_a = 0x00ff, .value_g = 0x00ff, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_SP17, .value_a = 0x00ff, .value_g = 0x00ff, .flags = 0, }, */
	{ .offset = B2063_PA_SP1, .value_a = 0x003d, .value_g = 0x00fd, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2063_PA_SP2, .value_a = 0x000c, .value_g = 0x000c, .flags = 0, }, */
	/* { .offset = B2063_PA_SP3, .value_a = 0x0096, .value_g = 0x0096, .flags = 0, }, */
	/* { .offset = B2063_PA_SP4, .value_a = 0x005a, .value_g = 0x005a, .flags = 0, }, */
	/* { .offset = B2063_PA_SP5, .value_a = 0x007f, .value_g = 0x007f, .flags = 0, }, */
	/* { .offset = B2063_PA_SP6, .value_a = 0x007f, .value_g = 0x007f, .flags = 0, }, */
	/* { .offset = B2063_PA_SP7, .value_a = 0x0033, .value_g = 0x0033, .flags = 0, }, */
	{ .offset = B2063_TX_BB_SP1, .value_a = 0x0002, .value_g = 0x0002, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2063_TX_BB_SP2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_TX_BB_SP3, .value_a = 0x0030, .value_g = 0x0030, .flags = 0, }, */
	/* { .offset = B2063_REG_SP1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2063_BANDGAP_CTL1, .value_a = 0x0056, .value_g = 0x0056, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2063_BANDGAP_CTL2, .value_a = 0x0006, .value_g = 0x0006, .flags = 0, }, */
	/* { .offset = B2063_LPO_CTL1, .value_a = 0x000e, .value_g = 0x000e, .flags = 0, }, */
	/* { .offset = B2063_RC_CALIB_CTL1, .value_a = 0x007e, .value_g = 0x007e, .flags = 0, }, */
	/* { .offset = B2063_RC_CALIB_CTL2, .value_a = 0x0015, .value_g = 0x0015, .flags = 0, }, */
	/* { .offset = B2063_RC_CALIB_CTL3, .value_a = 0x000f, .value_g = 0x000f, .flags = 0, }, */
	/* { .offset = B2063_RC_CALIB_CTL4, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_RC_CALIB_CTL5, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_RC_CALIB_CTL6, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_RC_CALIB_CTL7, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_RC_CALIB_CTL8, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_RC_CALIB_CTL9, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_RC_CALIB_CTL10, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_CALNRST, .value_a = 0x0004, .value_g = 0x0004, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_IN_PLL1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_IN_PLL2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_CP1, .value_a = 0x00cf, .value_g = 0x00cf, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_CP2, .value_a = 0x0059, .value_g = 0x0059, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_CP3, .value_a = 0x0007, .value_g = 0x0007, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_CP4, .value_a = 0x0042, .value_g = 0x0042, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_CTL1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_LF1, .value_a = 0x00db, .value_g = 0x00db, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_LF2, .value_a = 0x0094, .value_g = 0x0094, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_LF3, .value_a = 0x0028, .value_g = 0x0028, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_LF4, .value_a = 0x0063, .value_g = 0x0063, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_SG1, .value_a = 0x0007, .value_g = 0x0007, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_SG2, .value_a = 0x00d3, .value_g = 0x00d3, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_SG3, .value_a = 0x00b1, .value_g = 0x00b1, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_SG4, .value_a = 0x003b, .value_g = 0x003b, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_SG5, .value_a = 0x0006, .value_g = 0x0006, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_VCO1, .value_a = 0x0058, .value_g = 0x0058, .flags = 0, }, */
	{ .offset = B2063_PLL_JTAG_PLL_VCO2, .value_a = 0x00f7, .value_g = 0x00f7, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2063_PLL_JTAG_PLL_VCO_CALIB1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_VCO_CALIB2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_VCO_CALIB3, .value_a = 0x0002, .value_g = 0x0002, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_VCO_CALIB4, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_VCO_CALIB5, .value_a = 0x0009, .value_g = 0x0009, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_VCO_CALIB6, .value_a = 0x0005, .value_g = 0x0005, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_VCO_CALIB7, .value_a = 0x0016, .value_g = 0x0016, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_VCO_CALIB8, .value_a = 0x006b, .value_g = 0x006b, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_VCO_CALIB9, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_VCO_CALIB10, .value_a = 0x00b3, .value_g = 0x00b3, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_XTAL_12, .value_a = 0x0004, .value_g = 0x0004, .flags = 0, }, */
	/* { .offset = B2063_PLL_JTAG_PLL_XTAL3, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_LOGEN_ACL1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_LOGEN_ACL2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_LOGEN_ACL3, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_LOGEN_ACL4, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_LOGEN_ACL5, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_LO_CALIB_INPUTS, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_LO_CALIB_CTL1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_LO_CALIB_CTL2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_LO_CALIB_CTL3, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_LO_CALIB_WAITCNT, .value_a = 0x0002, .value_g = 0x0002, .flags = 0, }, */
	/* { .offset = B2063_LO_CALIB_OVR1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_LO_CALIB_OVR2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_LO_CALIB_OVAL1, .value_a = 0x0066, .value_g = 0x0066, .flags = 0, }, */
	/* { .offset = B2063_LO_CALIB_OVAL2, .value_a = 0x0066, .value_g = 0x0066, .flags = 0, }, */
	/* { .offset = B2063_LO_CALIB_OVAL3, .value_a = 0x0066, .value_g = 0x0066, .flags = 0, }, */
	/* { .offset = B2063_LO_CALIB_OVAL4, .value_a = 0x0066, .value_g = 0x0066, .flags = 0, }, */
	/* { .offset = B2063_LO_CALIB_OVAL5, .value_a = 0x0066, .value_g = 0x0066, .flags = 0, }, */
	/* { .offset = B2063_LO_CALIB_OVAL6, .value_a = 0x0066, .value_g = 0x0066, .flags = 0, }, */
	/* { .offset = B2063_LO_CALIB_OVAL7, .value_a = 0x0066, .value_g = 0x0066, .flags = 0, }, */
	/* { .offset = B2063_LO_CALIB_CALVLD1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_LO_CALIB_CALVLD2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_LO_CALIB_CVAL1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_LO_CALIB_CVAL2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_LO_CALIB_CVAL3, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_LO_CALIB_CVAL4, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_LO_CALIB_CVAL5, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_LO_CALIB_CVAL6, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_LO_CALIB_CVAL7, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_LOGEN_CALIB_EN, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_LOGEN_PEAKDET1, .value_a = 0x00ff, .value_g = 0x00ff, .flags = 0, }, */
	/* { .offset = B2063_LOGEN_RCCR1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_LOGEN_VCOBUF1, .value_a = 0x0060, .value_g = 0x0060, .flags = 0, }, */
	/* { .offset = B2063_LOGEN_MIXER1, .value_a = 0x0066, .value_g = 0x0066, .flags = 0, }, */
	/* { .offset = B2063_LOGEN_MIXER2, .value_a = 0x000c, .value_g = 0x000c, .flags = 0, }, */
	/* { .offset = B2063_LOGEN_BUF1, .value_a = 0x0066, .value_g = 0x0066, .flags = 0, }, */
	/* { .offset = B2063_LOGEN_BUF2, .value_a = 0x000c, .value_g = 0x000c, .flags = 0, }, */
	/* { .offset = B2063_LOGEN_DIV1, .value_a = 0x0001, .value_g = 0x0001, .flags = 0, }, */
	/* { .offset = B2063_LOGEN_DIV2, .value_a = 0x0066, .value_g = 0x0066, .flags = 0, }, */
	/* { .offset = B2063_LOGEN_DIV3, .value_a = 0x0066, .value_g = 0x0066, .flags = 0, }, */
	/* { .offset = B2063_LOGEN_CBUFRX1, .value_a = 0x0066, .value_g = 0x0066, .flags = 0, }, */
	/* { .offset = B2063_LOGEN_CBUFRX2, .value_a = 0x0066, .value_g = 0x0066, .flags = 0, }, */
	/* { .offset = B2063_LOGEN_CBUFTX1, .value_a = 0x0066, .value_g = 0x0066, .flags = 0, }, */
	/* { .offset = B2063_LOGEN_CBUFTX2, .value_a = 0x0066, .value_g = 0x0066, .flags = 0, }, */
	/* { .offset = B2063_LOGEN_IDAC1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_LOGEN_SPARE1, .value_a = 0x0001, .value_g = 0x0001, .flags = 0, }, */
	/* { .offset = B2063_LOGEN_SPARE2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_LOGEN_SPARE3, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_G_RX_1ST1, .value_a = 0x0033, .value_g = 0x0033, .flags = 0, }, */
	/* { .offset = B2063_G_RX_1ST2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_G_RX_1ST3, .value_a = 0x0005, .value_g = 0x0005, .flags = 0, }, */
	/* { .offset = B2063_G_RX_2ND1, .value_a = 0x0030, .value_g = 0x0030, .flags = 0, }, */
	/* { .offset = B2063_G_RX_2ND2, .value_a = 0x0055, .value_g = 0x0055, .flags = 0, }, */
	/* { .offset = B2063_G_RX_2ND3, .value_a = 0x0033, .value_g = 0x0033, .flags = 0, }, */
	/* { .offset = B2063_G_RX_2ND4, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_G_RX_2ND5, .value_a = 0x0033, .value_g = 0x0033, .flags = 0, }, */
	/* { .offset = B2063_G_RX_2ND6, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_G_RX_2ND7, .value_a = 0x0035, .value_g = 0x0035, .flags = 0, }, */
	/* { .offset = B2063_G_RX_2ND8, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_G_RX_PS1, .value_a = 0x0033, .value_g = 0x0033, .flags = 0, }, */
	/* { .offset = B2063_G_RX_PS2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_G_RX_PS3, .value_a = 0x0033, .value_g = 0x0033, .flags = 0, }, */
	/* { .offset = B2063_G_RX_PS4, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_G_RX_PS5, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_G_RX_MIX1, .value_a = 0x0044, .value_g = 0x0044, .flags = 0, }, */
	/* { .offset = B2063_G_RX_MIX2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2063_G_RX_MIX3, .value_a = 0x0071, .value_g = 0x0071, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2063_G_RX_MIX4, .value_a = 0x0071, .value_g = 0x0071, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2063_G_RX_MIX5, .value_a = 0x0003, .value_g = 0x0003, .flags = 0, }, */
	/* { .offset = B2063_G_RX_MIX6, .value_a = 0x0088, .value_g = 0x0088, .flags = 0, }, */
	/* { .offset = B2063_G_RX_MIX7, .value_a = 0x0044, .value_g = 0x0044, .flags = 0, }, */
	/* { .offset = B2063_G_RX_MIX8, .value_a = 0x0001, .value_g = 0x0001, .flags = 0, }, */
	/* { .offset = B2063_G_RX_PDET1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_G_RX_SPARES1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_G_RX_SPARES2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_G_RX_SPARES3, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_A_RX_1ST1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2063_A_RX_1ST2, .value_a = 0x00f0, .value_g = 0x0030, .flags = B206X_FLAG_A, },
	/* { .offset = B2063_A_RX_1ST3, .value_a = 0x0005, .value_g = 0x0005, .flags = 0, }, */
	/* { .offset = B2063_A_RX_1ST4, .value_a = 0x0033, .value_g = 0x0033, .flags = 0, }, */
	/* { .offset = B2063_A_RX_1ST5, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_A_RX_2ND1, .value_a = 0x0005, .value_g = 0x0005, .flags = 0, }, */
	/* { .offset = B2063_A_RX_2ND2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_A_RX_2ND3, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_A_RX_2ND4, .value_a = 0x0005, .value_g = 0x0005, .flags = 0, }, */
	/* { .offset = B2063_A_RX_2ND5, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_A_RX_2ND6, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_A_RX_2ND7, .value_a = 0x0005, .value_g = 0x0005, .flags = 0, }, */
	/* { .offset = B2063_A_RX_PS1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_A_RX_PS2, .value_a = 0x0033, .value_g = 0x0033, .flags = 0, }, */
	/* { .offset = B2063_A_RX_PS3, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_A_RX_PS4, .value_a = 0x0033, .value_g = 0x0033, .flags = 0, }, */
	/* { .offset = B2063_A_RX_PS5, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2063_A_RX_PS6, .value_a = 0x0077, .value_g = 0x0077, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2063_A_RX_MIX1, .value_a = 0x0088, .value_g = 0x0088, .flags = 0, }, */
	/* { .offset = B2063_A_RX_MIX2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_A_RX_MIX3, .value_a = 0x0044, .value_g = 0x0044, .flags = 0, }, */
	{ .offset = B2063_A_RX_MIX4, .value_a = 0x0003, .value_g = 0x0003, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2063_A_RX_MIX5, .value_a = 0x000f, .value_g = 0x000f, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	{ .offset = B2063_A_RX_MIX6, .value_a = 0x000f, .value_g = 0x000f, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2063_A_RX_MIX7, .value_a = 0x0044, .value_g = 0x0044, .flags = 0, }, */
	/* { .offset = B2063_A_RX_MIX8, .value_a = 0x0001, .value_g = 0x0001, .flags = 0, }, */
	/* { .offset = B2063_A_RX_PWRDET1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_A_RX_SPARE1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_A_RX_SPARE2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_A_RX_SPARE3, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2063_RX_TIA_CTL1, .value_a = 0x0077, .value_g = 0x0077, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2063_RX_TIA_CTL2, .value_a = 0x0058, .value_g = 0x0058, .flags = 0, }, */
	{ .offset = B2063_RX_TIA_CTL3, .value_a = 0x0077, .value_g = 0x0077, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2063_RX_TIA_CTL4, .value_a = 0x0058, .value_g = 0x0058, .flags = 0, }, */
	/* { .offset = B2063_RX_TIA_CTL5, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_RX_TIA_CTL6, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_RX_BB_CTL1, .value_a = 0x0074, .value_g = 0x0074, .flags = 0, }, */
	{ .offset = B2063_RX_BB_CTL2, .value_a = 0x0004, .value_g = 0x0004, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2063_RX_BB_CTL3, .value_a = 0x00a2, .value_g = 0x00a2, .flags = 0, }, */
	/* { .offset = B2063_RX_BB_CTL4, .value_a = 0x00aa, .value_g = 0x00aa, .flags = 0, }, */
	/* { .offset = B2063_RX_BB_CTL5, .value_a = 0x0024, .value_g = 0x0024, .flags = 0, }, */
	/* { .offset = B2063_RX_BB_CTL6, .value_a = 0x00a9, .value_g = 0x00a9, .flags = 0, }, */
	/* { .offset = B2063_RX_BB_CTL7, .value_a = 0x0028, .value_g = 0x0028, .flags = 0, }, */
	/* { .offset = B2063_RX_BB_CTL8, .value_a = 0x0010, .value_g = 0x0010, .flags = 0, }, */
	/* { .offset = B2063_RX_BB_CTL9, .value_a = 0x0055, .value_g = 0x0055, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_CTL1, .value_a = 0x0080, .value_g = 0x0080, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_IDAC_LO_RF_I, .value_a = 0x0088, .value_g = 0x0088, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_IDAC_LO_RF_Q, .value_a = 0x0088, .value_g = 0x0088, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_IDAC_LO_BB_I, .value_a = 0x0088, .value_g = 0x0088, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_IDAC_LO_BB_Q, .value_a = 0x0088, .value_g = 0x0088, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_CTL2, .value_a = 0x0080, .value_g = 0x0080, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_CTL3, .value_a = 0x0038, .value_g = 0x0038, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_CTL4, .value_a = 0x00b8, .value_g = 0x00b8, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_CTL5, .value_a = 0x0080, .value_g = 0x0080, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_CTL6, .value_a = 0x0038, .value_g = 0x0038, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_CTL7, .value_a = 0x0078, .value_g = 0x0078, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_CTL8, .value_a = 0x00c0, .value_g = 0x00c0, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_CTL9, .value_a = 0x0003, .value_g = 0x0003, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_CTL10, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_CTL14, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_TX_RF_CTL15, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2063_PA_CTL1, .value_a = 0x0000, .value_g = 0x0004, .flags = B206X_FLAG_A, },
	/* { .offset = B2063_PA_CTL2, .value_a = 0x000c, .value_g = 0x000c, .flags = 0, }, */
	/* { .offset = B2063_PA_CTL3, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_PA_CTL4, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_PA_CTL5, .value_a = 0x0096, .value_g = 0x0096, .flags = 0, }, */
	/* { .offset = B2063_PA_CTL6, .value_a = 0x0077, .value_g = 0x0077, .flags = 0, }, */
	/* { .offset = B2063_PA_CTL7, .value_a = 0x005a, .value_g = 0x005a, .flags = 0, }, */
	/* { .offset = B2063_PA_CTL8, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_PA_CTL9, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_PA_CTL10, .value_a = 0x0021, .value_g = 0x0021, .flags = 0, }, */
	/* { .offset = B2063_PA_CTL11, .value_a = 0x0070, .value_g = 0x0070, .flags = 0, }, */
	/* { .offset = B2063_PA_CTL12, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_PA_CTL13, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_TX_BB_CTL1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_TX_BB_CTL2, .value_a = 0x00b3, .value_g = 0x00b3, .flags = 0, }, */
	/* { .offset = B2063_TX_BB_CTL3, .value_a = 0x0055, .value_g = 0x0055, .flags = 0, }, */
	/* { .offset = B2063_TX_BB_CTL4, .value_a = 0x000b, .value_g = 0x000b, .flags = 0, }, */
	/* { .offset = B2063_GPIO_CTL1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	{ .offset = B2063_VREG_CTL1, .value_a = 0x0003, .value_g = 0x0003, .flags = B206X_FLAG_A | B206X_FLAG_G, },
	/* { .offset = B2063_AMUX_CTL1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_IQ_CALIB_GVAR, .value_a = 0x00b3, .value_g = 0x00b3, .flags = 0, }, */
	/* { .offset = B2063_IQ_CALIB_CTL1, .value_a = 0x0055, .value_g = 0x0055, .flags = 0, }, */
	/* { .offset = B2063_IQ_CALIB_CTL2, .value_a = 0x0030, .value_g = 0x0030, .flags = 0, }, */
	/* { .offset = B2063_TEMPSENSE_CTL1, .value_a = 0x0046, .value_g = 0x0046, .flags = 0, }, */
	/* { .offset = B2063_TEMPSENSE_CTL2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_TX_RX_LOOPBACK1, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_TX_RX_LOOPBACK2, .value_a = 0x0000, .value_g = 0x0000, .flags = 0, }, */
	/* { .offset = B2063_EXT_TSSI_CTL1, .value_a = 0x0021, .value_g = 0x0021, .flags = 0, }, */
	/* { .offset = B2063_EXT_TSSI_CTL2, .value_a = 0x0023, .value_g = 0x0023, .flags = 0, }, */
	/* { .offset = B2063_AFE_CTL , .value_a = 0x0002, .value_g = 0x0002, .flags = 0, }, */
};

void b2062_upload_init_table(struct b43_wldev *dev)
{
	const struct b206x_init_tab_entry *e;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(b2062_init_tab); i++) {
		e = &b2062_init_tab[i];
		if (b43_current_band(dev->wl) == NL80211_BAND_2GHZ) {
			if (!(e->flags & B206X_FLAG_G))
				continue;
			b43_radio_write(dev, e->offset, e->value_g);
		} else {
			if (!(e->flags & B206X_FLAG_A))
				continue;
			b43_radio_write(dev, e->offset, e->value_a);
		}
	}
}

void b2063_upload_init_table(struct b43_wldev *dev)
{
	const struct b206x_init_tab_entry *e;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(b2063_init_tab); i++) {
		e = &b2063_init_tab[i];
		if (b43_current_band(dev->wl) == NL80211_BAND_2GHZ) {
			if (!(e->flags & B206X_FLAG_G))
				continue;
			b43_radio_write(dev, e->offset, e->value_g);
		} else {
			if (!(e->flags & B206X_FLAG_A))
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
	u32 type;
	u8 *data = _data;
	unsigned int i;

	type = offset & B43_LPTAB_TYPEMASK;
	offset &= ~B43_LPTAB_TYPEMASK;
	B43_WARN_ON(offset > 0xFFFF);

	b43_phy_write(dev, B43_LPPHY_TABLE_ADDR, offset);

	for (i = 0; i < nr_elements; i++) {
		switch (type) {
		case B43_LPTAB_8BIT:
			*data = b43_phy_read(dev, B43_LPPHY_TABLEDATALO) & 0xFF;
			data++;
			break;
		case B43_LPTAB_16BIT:
			*((u16 *)data) = b43_phy_read(dev, B43_LPPHY_TABLEDATALO);
			data += 2;
			break;
		case B43_LPTAB_32BIT:
			*((u32 *)data) = b43_phy_read(dev, B43_LPPHY_TABLEDATAHI);
			*((u32 *)data) <<= 16;
			*((u32 *)data) |= b43_phy_read(dev, B43_LPPHY_TABLEDATALO);
			data += 4;
			break;
		default:
			B43_WARN_ON(1);
		}
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
	offset &= ~B43_LPTAB_TYPEMASK;
	B43_WARN_ON(offset > 0xFFFF);

	b43_phy_write(dev, B43_LPPHY_TABLE_ADDR, offset);

	for (i = 0; i < nr_elements; i++) {
		switch (type) {
		case B43_LPTAB_8BIT:
			value = *data;
			data++;
			B43_WARN_ON(value & ~0xFF);
			b43_phy_write(dev, B43_LPPHY_TABLEDATALO, value);
			break;
		case B43_LPTAB_16BIT:
			value = *((u16 *)data);
			data += 2;
			B43_WARN_ON(value & ~0xFFFF);
			b43_phy_write(dev, B43_LPPHY_TABLEDATALO, value);
			break;
		case B43_LPTAB_32BIT:
			value = *((u32 *)data);
			data += 4;
			b43_phy_write(dev, B43_LPPHY_TABLEDATAHI, value >> 16);
			b43_phy_write(dev, B43_LPPHY_TABLEDATALO, value);
			break;
		default:
			B43_WARN_ON(1);
		}
	}
}

static const u8 lpphy_min_sig_sq_table[] = {
	0xde, 0xdc, 0xda, 0xd8, 0xd6, 0xd4, 0xd2, 0xcf, 0xcd,
	0xca, 0xc7, 0xc4, 0xc1, 0xbe, 0xbe, 0xbe, 0xbe, 0xbe,
	0xbe, 0xbe, 0xbe, 0xbe, 0xbe, 0xbe, 0xbe, 0xbe, 0x00,
	0xbe, 0xbe, 0xbe, 0xbe, 0xbe, 0xbe, 0xbe, 0xbe, 0xbe,
	0xbe, 0xbe, 0xbe, 0xbe, 0xc1, 0xc4, 0xc7, 0xca, 0xcd,
	0xcf, 0xd2, 0xd4, 0xd6, 0xd8, 0xda, 0xdc, 0xde,
};

static const u16 lpphy_rev01_noise_scale_table[] = {
	0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4,
	0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa400, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4,
	0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0x00a4,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x4c00, 0x2d36,
	0x0000, 0x0000, 0x4c00, 0x2d36,
};

static const u16 lpphy_rev2plus_noise_scale_table[] = {
	0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4,
	0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4,
	0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x0000,
	0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4,
	0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4,
	0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4, 0x00a4,
	0x00a4,
};

static const u16 lpphy_crs_gain_nft_table[] = {
	0x0366, 0x036a, 0x036f, 0x0364, 0x0367, 0x036d, 0x0374, 0x037f, 0x036f,
	0x037b, 0x038a, 0x0378, 0x0367, 0x036d, 0x0375, 0x0381, 0x0374, 0x0381,
	0x0392, 0x03a9, 0x03c4, 0x03e1, 0x0001, 0x001f, 0x0040, 0x005e, 0x007f,
	0x009e, 0x00bd, 0x00dd, 0x00fd, 0x011d, 0x013d,
};

static const u16 lpphy_rev01_filter_control_table[] = {
	0xa0fc, 0x10fc, 0x10db, 0x20b7, 0xff93, 0x10bf, 0x109b, 0x2077, 0xff53,
	0x0127,
};

static const u32 lpphy_rev2plus_filter_control_table[] = {
	0x000141fc, 0x000021fc, 0x000021b7, 0x0000416f, 0x0001ff27, 0x0000217f,
	0x00002137, 0x000040ef, 0x0001fea7, 0x0000024f,
};

static const u32 lpphy_rev01_ps_control_table[] = {
	0x00010000, 0x000000a0, 0x00040000, 0x00000048, 0x08080101, 0x00000080,
	0x08080101, 0x00000040, 0x08080101, 0x000000c0, 0x08a81501, 0x000000c0,
	0x0fe8fd01, 0x000000c0, 0x08300105, 0x000000c0, 0x08080201, 0x000000c0,
	0x08280205, 0x000000c0, 0xe80802fe, 0x000000c7, 0x28080206, 0x000000c0,
	0x08080202, 0x000000c0, 0x0ba87602, 0x000000c0, 0x1068013d, 0x000000c0,
	0x10280105, 0x000000c0, 0x08880102, 0x000000c0, 0x08280106, 0x000000c0,
	0xe80801fd, 0x000000c7, 0xa8080115, 0x000000c0,
};

static const u32 lpphy_rev2plus_ps_control_table[] = {
	0x00e38e08, 0x00e08e38, 0x00000000, 0x00000000, 0x00000000, 0x00002080,
	0x00006180, 0x00003002, 0x00000040, 0x00002042, 0x00180047, 0x00080043,
	0x00000041, 0x000020c1, 0x00046006, 0x00042002, 0x00040000, 0x00002003,
	0x00180006, 0x00080002,
};

static const u8 lpphy_pll_fraction_table[] = {
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x80,
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
};

static const u16 lpphy_iqlo_cal_table[] = {
	0x0200, 0x0300, 0x0400, 0x0600, 0x0800, 0x0b00, 0x1000, 0x1001, 0x1002,
	0x1003, 0x1004, 0x1005, 0x1006, 0x1007, 0x1707, 0x2007, 0x2d07, 0x4007,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0200, 0x0300, 0x0400, 0x0600,
	0x0800, 0x0b00, 0x1000, 0x1001, 0x1002, 0x1003, 0x1004, 0x1005, 0x1006,
	0x1007, 0x1707, 0x2007, 0x2d07, 0x4007, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x4000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

static const u16 lpphy_rev0_ofdm_cck_gain_table[] = {
	0x0001, 0x0001, 0x0001, 0x0001, 0x1001, 0x2001, 0x3001, 0x4001, 0x5001,
	0x6001, 0x7001, 0x7011, 0x7021, 0x2035, 0x2045, 0x2055, 0x2065, 0x2075,
	0x006d, 0x007d, 0x014d, 0x015d, 0x115d, 0x035d, 0x135d, 0x055d, 0x155d,
	0x0d5d, 0x1d5d, 0x2d5d, 0x555d, 0x655d, 0x755d,
};

static const u16 lpphy_rev1_ofdm_cck_gain_table[] = {
	0x5000, 0x6000, 0x7000, 0x0001, 0x1001, 0x2001, 0x3001, 0x4001, 0x5001,
	0x6001, 0x7001, 0x7011, 0x7021, 0x2035, 0x2045, 0x2055, 0x2065, 0x2075,
	0x006d, 0x007d, 0x014d, 0x015d, 0x115d, 0x035d, 0x135d, 0x055d, 0x155d,
	0x0d5d, 0x1d5d, 0x2d5d, 0x555d, 0x655d, 0x755d,
};

static const u16 lpphy_gain_delta_table[] = {
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

static const u32 lpphy_tx_power_control_table[] = {
	0x00000050, 0x0000004f, 0x0000004e, 0x0000004d, 0x0000004c, 0x0000004b,
	0x0000004a, 0x00000049, 0x00000048, 0x00000047, 0x00000046, 0x00000045,
	0x00000044, 0x00000043, 0x00000042, 0x00000041, 0x00000040, 0x0000003f,
	0x0000003e, 0x0000003d, 0x0000003c, 0x0000003b, 0x0000003a, 0x00000039,
	0x00000038, 0x00000037, 0x00000036, 0x00000035, 0x00000034, 0x00000033,
	0x00000032, 0x00000031, 0x00000030, 0x0000002f, 0x0000002e, 0x0000002d,
	0x0000002c, 0x0000002b, 0x0000002a, 0x00000029, 0x00000028, 0x00000027,
	0x00000026, 0x00000025, 0x00000024, 0x00000023, 0x00000022, 0x00000021,
	0x00000020, 0x0000001f, 0x0000001e, 0x0000001d, 0x0000001c, 0x0000001b,
	0x0000001a, 0x00000019, 0x00000018, 0x00000017, 0x00000016, 0x00000015,
	0x00000014, 0x00000013, 0x00000012, 0x00000011, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x000075a0, 0x000075a0, 0x000075a1, 0x000075a1, 0x000075a2, 0x000075a2,
	0x000075a3, 0x000075a3, 0x000074b0, 0x000074b0, 0x000074b1, 0x000074b1,
	0x000074b2, 0x000074b2, 0x000074b3, 0x000074b3, 0x00006d20, 0x00006d20,
	0x00006d21, 0x00006d21, 0x00006d22, 0x00006d22, 0x00006d23, 0x00006d23,
	0x00004660, 0x00004660, 0x00004661, 0x00004661, 0x00004662, 0x00004662,
	0x00004663, 0x00004663, 0x00003e60, 0x00003e60, 0x00003e61, 0x00003e61,
	0x00003e62, 0x00003e62, 0x00003e63, 0x00003e63, 0x00003660, 0x00003660,
	0x00003661, 0x00003661, 0x00003662, 0x00003662, 0x00003663, 0x00003663,
	0x00002e60, 0x00002e60, 0x00002e61, 0x00002e61, 0x00002e62, 0x00002e62,
	0x00002e63, 0x00002e63, 0x00002660, 0x00002660, 0x00002661, 0x00002661,
	0x00002662, 0x00002662, 0x00002663, 0x00002663, 0x000025e0, 0x000025e0,
	0x000025e1, 0x000025e1, 0x000025e2, 0x000025e2, 0x000025e3, 0x000025e3,
	0x00001de0, 0x00001de0, 0x00001de1, 0x00001de1, 0x00001de2, 0x00001de2,
	0x00001de3, 0x00001de3, 0x00001d60, 0x00001d60, 0x00001d61, 0x00001d61,
	0x00001d62, 0x00001d62, 0x00001d63, 0x00001d63, 0x00001560, 0x00001560,
	0x00001561, 0x00001561, 0x00001562, 0x00001562, 0x00001563, 0x00001563,
	0x00000d60, 0x00000d60, 0x00000d61, 0x00000d61, 0x00000d62, 0x00000d62,
	0x00000d63, 0x00000d63, 0x00000ce0, 0x00000ce0, 0x00000ce1, 0x00000ce1,
	0x00000ce2, 0x00000ce2, 0x00000ce3, 0x00000ce3, 0x00000e10, 0x00000e10,
	0x00000e11, 0x00000e11, 0x00000e12, 0x00000e12, 0x00000e13, 0x00000e13,
	0x00000bf0, 0x00000bf0, 0x00000bf1, 0x00000bf1, 0x00000bf2, 0x00000bf2,
	0x00000bf3, 0x00000bf3, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
	0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
	0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
	0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
	0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
	0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
	0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
	0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
	0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
	0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
	0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
	0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
	0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
	0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
	0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
	0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
	0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
	0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
	0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
	0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
	0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x04200000, 0x04000000,
	0x04200000, 0x04000000, 0x04200000, 0x04000000, 0x000000ff, 0x000002fc,
	0x0000fa08, 0x00000305, 0x00000206, 0x00000304, 0x0000fb04, 0x0000fcff,
	0x000005fb, 0x0000fd01, 0x00000401, 0x00000006, 0x0000ff03, 0x000007fc,
	0x0000fc08, 0x00000203, 0x0000fffb, 0x00000600, 0x0000fa01, 0x0000fc03,
	0x0000fe06, 0x0000fe00, 0x00000102, 0x000007fd, 0x000004fb, 0x000006ff,
	0x000004fd, 0x0000fdfa, 0x000007fb, 0x0000fdfa, 0x0000fa06, 0x00000500,
	0x0000f902, 0x000007fa, 0x0000fafa, 0x00000500, 0x000007fa, 0x00000700,
	0x00000305, 0x000004ff, 0x00000801, 0x00000503, 0x000005f9, 0x00000404,
	0x0000fb08, 0x000005fd, 0x00000501, 0x00000405, 0x0000fb03, 0x000007fc,
	0x00000403, 0x00000303, 0x00000402, 0x0000faff, 0x0000fe05, 0x000005fd,
	0x0000fe01, 0x000007fa, 0x00000202, 0x00000504, 0x00000102, 0x000008fe,
	0x0000fa04, 0x0000fafc, 0x0000fe08, 0x000000f9, 0x000002fa, 0x000003fe,
	0x00000304, 0x000004f9, 0x00000100, 0x0000fd06, 0x000008fc, 0x00000701,
	0x00000504, 0x0000fdfe, 0x0000fdfc, 0x000003fe, 0x00000704, 0x000002fc,
	0x000004f9, 0x0000fdfd, 0x0000fa07, 0x00000205, 0x000003fd, 0x000005fb,
	0x000004f9, 0x00000804, 0x0000fc06, 0x0000fcf9, 0x00000100, 0x0000fe05,
	0x00000408, 0x0000fb02, 0x00000304, 0x000006fe, 0x000004fa, 0x00000305,
	0x000008fc, 0x00000102, 0x000001fd, 0x000004fc, 0x0000fe03, 0x00000701,
	0x000001fb, 0x000001f9, 0x00000206, 0x000006fd, 0x00000508, 0x00000700,
	0x00000304, 0x000005fe, 0x000005ff, 0x0000fa04, 0x00000303, 0x0000fefb,
	0x000007f9, 0x0000fefc, 0x000004fd, 0x000005fc, 0x0000fffd, 0x0000fc08,
	0x0000fbf9, 0x0000fd07, 0x000008fb, 0x0000fe02, 0x000006fb, 0x00000702,
};

static const u32 lpphy_gain_idx_table[] = {
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x10000001, 0x00000000, 0x20000082, 0x00000000, 0x40000104, 0x00000000,
	0x60004207, 0x00000001, 0x7000838a, 0x00000001, 0xd021050d, 0x00000001,
	0xe041c683, 0x00000001, 0x50828805, 0x00000000, 0x80e34288, 0x00000000,
	0xb144040b, 0x00000000, 0xe1a6058e, 0x00000000, 0x12064711, 0x00000001,
	0xb0a18612, 0x00000010, 0xe1024794, 0x00000010, 0x11630915, 0x00000011,
	0x31c3ca1b, 0x00000011, 0xc1848a9c, 0x00000018, 0xf1e50da0, 0x00000018,
	0x22468e21, 0x00000019, 0x4286d023, 0x00000019, 0xa347d0a4, 0x00000019,
	0xb36811a6, 0x00000019, 0xf3e89227, 0x00000019, 0x0408d329, 0x0000001a,
	0x244953aa, 0x0000001a, 0x346994ab, 0x0000001a, 0x54aa152c, 0x0000001a,
	0x64ca55ad, 0x0000001a, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x10000001, 0x00000000, 0x20000082, 0x00000000,
	0x40000104, 0x00000000, 0x60004207, 0x00000001, 0x7000838a, 0x00000001,
	0xd021050d, 0x00000001, 0xe041c683, 0x00000001, 0x50828805, 0x00000000,
	0x80e34288, 0x00000000, 0xb144040b, 0x00000000, 0xe1a6058e, 0x00000000,
	0x12064711, 0x00000001, 0xb0a18612, 0x00000010, 0xe1024794, 0x00000010,
	0x11630915, 0x00000011, 0x31c3ca1b, 0x00000011, 0xc1848a9c, 0x00000018,
	0xf1e50da0, 0x00000018, 0x22468e21, 0x00000019, 0x4286d023, 0x00000019,
	0xa347d0a4, 0x00000019, 0xb36811a6, 0x00000019, 0xf3e89227, 0x00000019,
	0x0408d329, 0x0000001a, 0x244953aa, 0x0000001a, 0x346994ab, 0x0000001a,
	0x54aa152c, 0x0000001a, 0x64ca55ad, 0x0000001a,
};

static const u16 lpphy_aux_gain_idx_table[] = {
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0001, 0x0002, 0x0004, 0x0016, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x0002, 0x0004, 0x0016,
};

static const u32 lpphy_gain_value_table[] = {
	0x00000008, 0x0000000e, 0x00000014, 0x0000001a, 0x000000fb, 0x00000004,
	0x00000008, 0x0000000d, 0x00000001, 0x00000004, 0x00000007, 0x0000000a,
	0x0000000d, 0x00000010, 0x00000012, 0x00000015, 0x00000000, 0x00000006,
	0x0000000c, 0x00000000, 0x00000000, 0x00000000, 0x00000012, 0x00000000,
	0x00000000, 0x00000000, 0x00000018, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x0000001e, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000003, 0x00000006, 0x00000009, 0x0000000c, 0x0000000f,
	0x00000012, 0x00000015, 0x00000018, 0x0000001b, 0x0000001e, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000009, 0x000000f1,
	0x00000000, 0x00000000,
};

static const u16 lpphy_gain_table[] = {
	0x0000, 0x0400, 0x0800, 0x0802, 0x0804, 0x0806, 0x0807, 0x0808, 0x080a,
	0x080b, 0x080c, 0x080e, 0x080f, 0x0810, 0x0812, 0x0813, 0x0814, 0x0816,
	0x0817, 0x081a, 0x081b, 0x081f, 0x0820, 0x0824, 0x0830, 0x0834, 0x0837,
	0x083b, 0x083f, 0x0840, 0x0844, 0x0857, 0x085b, 0x085f, 0x08d7, 0x08db,
	0x08df, 0x0957, 0x095b, 0x095f, 0x0b57, 0x0b5b, 0x0b5f, 0x0f5f, 0x135f,
	0x175f, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

static const u32 lpphy_a0_gain_idx_table[] = {
	0x001111e0, 0x00652051, 0x00606055, 0x005b005a, 0x00555060, 0x00511065,
	0x004c806b, 0x0047d072, 0x00444078, 0x00400080, 0x003ca087, 0x0039408f,
	0x0035e098, 0x0032e0a1, 0x003030aa, 0x002d80b4, 0x002ae0bf, 0x002880ca,
	0x002640d6, 0x002410e3, 0x002220f0, 0x002020ff, 0x001e510e, 0x001ca11e,
	0x001b012f, 0x00199140, 0x00182153, 0x0016c168, 0x0015817d, 0x00145193,
	0x001321ab, 0x001211c5, 0x001111e0, 0x001021fc, 0x000f321a, 0x000e523a,
	0x000d925c, 0x000cd27f, 0x000c12a5, 0x000b62cd, 0x000ac2f8, 0x000a2325,
	0x00099355, 0x00091387, 0x000883bd, 0x000813f5, 0x0007a432, 0x00073471,
	0x0006c4b5, 0x000664fc, 0x00061547, 0x0005b598, 0x000565ec, 0x00051646,
	0x0004d6a5, 0x0004870a, 0x00044775, 0x000407e6, 0x0003d85e, 0x000398dd,
	0x00036963, 0x000339f2, 0x00030a89, 0x0002db28,
};

static const u16 lpphy_a0_aux_gain_idx_table[] = {
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0002, 0x0014, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0002, 0x0014,
};

static const u32 lpphy_a0_gain_value_table[] = {
	0x00000008, 0x0000000e, 0x00000014, 0x0000001a, 0x000000fb, 0x00000004,
	0x00000008, 0x0000000d, 0x00000001, 0x00000004, 0x00000007, 0x0000000a,
	0x0000000d, 0x00000010, 0x00000012, 0x00000015, 0x00000000, 0x00000006,
	0x0000000c, 0x00000000, 0x00000000, 0x00000000, 0x00000012, 0x00000000,
	0x00000000, 0x00000000, 0x00000018, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x0000001e, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000003, 0x00000006, 0x00000009, 0x0000000c, 0x0000000f,
	0x00000012, 0x00000015, 0x00000018, 0x0000001b, 0x0000001e, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000000f, 0x000000f7,
	0x00000000, 0x00000000,
};

static const u16 lpphy_a0_gain_table[] = {
	0x0000, 0x0002, 0x0004, 0x0006, 0x0007, 0x0008, 0x000a, 0x000b, 0x000c,
	0x000e, 0x000f, 0x0010, 0x0012, 0x0013, 0x0014, 0x0016, 0x0017, 0x001a,
	0x001b, 0x001f, 0x0020, 0x0024, 0x0030, 0x0034, 0x0037, 0x003b, 0x003f,
	0x0040, 0x0044, 0x0057, 0x005b, 0x005f, 0x00d7, 0x00db, 0x00df, 0x0157,
	0x015b, 0x015f, 0x0357, 0x035b, 0x035f, 0x075f, 0x0b5f, 0x0f5f, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

static const u16 lpphy_sw_control_table[] = {
	0x0128, 0x0128, 0x0009, 0x0009, 0x0028, 0x0028, 0x0028, 0x0028, 0x0128,
	0x0128, 0x0009, 0x0009, 0x0028, 0x0028, 0x0028, 0x0028, 0x0009, 0x0009,
	0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0018, 0x0018, 0x0018,
	0x0018, 0x0018, 0x0018, 0x0018, 0x0018, 0x0128, 0x0128, 0x0009, 0x0009,
	0x0028, 0x0028, 0x0028, 0x0028, 0x0128, 0x0128, 0x0009, 0x0009, 0x0028,
	0x0028, 0x0028, 0x0028, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
	0x0009, 0x0009, 0x0018, 0x0018, 0x0018, 0x0018, 0x0018, 0x0018, 0x0018,
	0x0018,
};

static const u8 lpphy_hf_table[] = {
	0x4b, 0x36, 0x24, 0x18, 0x49, 0x34, 0x23, 0x17, 0x48,
	0x33, 0x23, 0x17, 0x48, 0x33, 0x23, 0x17,
};

static const u32 lpphy_papd_eps_table[] = {
	0x00000000, 0x00013ffc, 0x0001dff3, 0x0001bff0, 0x00023fe9, 0x00021fdf,
	0x00028fdf, 0x00033fd2, 0x00039fcb, 0x00043fc7, 0x0004efc2, 0x00055fb5,
	0x0005cfb0, 0x00063fa8, 0x00068fa3, 0x00071f98, 0x0007ef92, 0x00084f8b,
	0x0008df82, 0x00097f77, 0x0009df69, 0x000a3f62, 0x000adf57, 0x000b6f4c,
	0x000bff41, 0x000c9f39, 0x000cff30, 0x000dbf27, 0x000e4f1e, 0x000edf16,
	0x000f7f13, 0x00102f11, 0x00110f10, 0x0011df11, 0x0012ef15, 0x00143f1c,
	0x00158f27, 0x00172f35, 0x00193f47, 0x001baf5f, 0x001e6f7e, 0x0021cfa4,
	0x0025bfd2, 0x002a2008, 0x002fb047, 0x00360090, 0x003d40e0, 0x0045c135,
	0x004fb189, 0x005ae1d7, 0x0067221d, 0x0075025a, 0x007ff291, 0x007ff2bf,
	0x007ff2e3, 0x007ff2ff, 0x007ff315, 0x007ff329, 0x007ff33f, 0x007ff356,
	0x007ff36e, 0x007ff39c, 0x007ff441, 0x007ff506,
};

static const u32 lpphy_papd_mult_table[] = {
	0x001111e0, 0x00652051, 0x00606055, 0x005b005a, 0x00555060, 0x00511065,
	0x004c806b, 0x0047d072, 0x00444078, 0x00400080, 0x003ca087, 0x0039408f,
	0x0035e098, 0x0032e0a1, 0x003030aa, 0x002d80b4, 0x002ae0bf, 0x002880ca,
	0x002640d6, 0x002410e3, 0x002220f0, 0x002020ff, 0x001e510e, 0x001ca11e,
	0x001b012f, 0x00199140, 0x00182153, 0x0016c168, 0x0015817d, 0x00145193,
	0x001321ab, 0x001211c5, 0x001111e0, 0x001021fc, 0x000f321a, 0x000e523a,
	0x000d925c, 0x000cd27f, 0x000c12a5, 0x000b62cd, 0x000ac2f8, 0x000a2325,
	0x00099355, 0x00091387, 0x000883bd, 0x000813f5, 0x0007a432, 0x00073471,
	0x0006c4b5, 0x000664fc, 0x00061547, 0x0005b598, 0x000565ec, 0x00051646,
	0x0004d6a5, 0x0004870a, 0x00044775, 0x000407e6, 0x0003d85e, 0x000398dd,
	0x00036963, 0x000339f2, 0x00030a89, 0x0002db28,
};

static struct lpphy_tx_gain_table_entry lpphy_rev0_nopa_tx_gain_table[] = {
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 152, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 147, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 143, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 139, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 135, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 131, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 128, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 124, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 121, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 117, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 114, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 111, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 107, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 104, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 101, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 99, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 96, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 93, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 90, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 88, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 85, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 83, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 81, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 78, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 76, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 74, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 72, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 57, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 72, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 57, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 71, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 69, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 67, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 65, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 63, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 58, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 57, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 65, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 63, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 58, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 71, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 69, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 67, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 65, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 63, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 58, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 56, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 72, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 72, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 63, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 57, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 72, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 72, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 73, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 71, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 69, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 67, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 65, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 63, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 72, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 65, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 63, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 73, },
	{ .gm = 7, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 71, },
};

static struct lpphy_tx_gain_table_entry lpphy_rev0_2ghz_tx_gain_table[] = {
	{ .gm = 4, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 64, },
	{ .gm = 4, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 62, },
	{ .gm = 4, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 60, },
	{ .gm = 4, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 72, },
	{ .gm = 4, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 70, },
	{ .gm = 4, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 68, },
	{ .gm = 4, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 66, },
	{ .gm = 4, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 64, },
	{ .gm = 4, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 62, },
	{ .gm = 4, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 60, },
	{ .gm = 4, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 72, },
	{ .gm = 4, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 70, },
	{ .gm = 4, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 68, },
	{ .gm = 4, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 66, },
	{ .gm = 4, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 64, },
	{ .gm = 4, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 63, },
	{ .gm = 4, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 61, },
	{ .gm = 4, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 57, },
	{ .gm = 4, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 72, },
	{ .gm = 4, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 70, },
	{ .gm = 4, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 68, },
	{ .gm = 4, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 66, },
	{ .gm = 4, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 64, },
	{ .gm = 4, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 62, },
	{ .gm = 4, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 60, },
	{ .gm = 4, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 72, },
	{ .gm = 4, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 70, },
	{ .gm = 4, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 68, },
	{ .gm = 4, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 66, },
	{ .gm = 4, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 64, },
	{ .gm = 4, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 62, },
	{ .gm = 4, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 61, },
	{ .gm = 4, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 73, },
	{ .gm = 4, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 71, },
	{ .gm = 4, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 69, },
	{ .gm = 4, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 67, },
	{ .gm = 4, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 65, },
	{ .gm = 4, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 63, },
	{ .gm = 4, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 61, },
	{ .gm = 4, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 72, },
	{ .gm = 4, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 70, },
	{ .gm = 4, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 68, },
	{ .gm = 4, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 66, },
	{ .gm = 4, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 65, },
	{ .gm = 4, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 63, },
	{ .gm = 4, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 61, },
	{ .gm = 4, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 73, },
	{ .gm = 4, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 71, },
	{ .gm = 4, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 69, },
	{ .gm = 4, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 67, },
	{ .gm = 4, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 65, },
	{ .gm = 4, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 63, },
	{ .gm = 4, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 61, },
	{ .gm = 4, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 60, },
	{ .gm = 4, .pga = 10, .pad = 6, .dac = 0, .bb_mult = 72, },
	{ .gm = 4, .pga = 10, .pad = 6, .dac = 0, .bb_mult = 70, },
	{ .gm = 4, .pga = 10, .pad = 6, .dac = 0, .bb_mult = 68, },
	{ .gm = 4, .pga = 10, .pad = 6, .dac = 0, .bb_mult = 66, },
	{ .gm = 4, .pga = 10, .pad = 6, .dac = 0, .bb_mult = 64, },
	{ .gm = 4, .pga = 10, .pad = 6, .dac = 0, .bb_mult = 62, },
	{ .gm = 4, .pga = 10, .pad = 6, .dac = 0, .bb_mult = 60, },
	{ .gm = 4, .pga = 10, .pad = 6, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 10, .pad = 5, .dac = 0, .bb_mult = 72, },
	{ .gm = 4, .pga = 10, .pad = 5, .dac = 0, .bb_mult = 70, },
	{ .gm = 4, .pga = 10, .pad = 5, .dac = 0, .bb_mult = 68, },
	{ .gm = 4, .pga = 10, .pad = 5, .dac = 0, .bb_mult = 66, },
	{ .gm = 4, .pga = 10, .pad = 5, .dac = 0, .bb_mult = 64, },
	{ .gm = 4, .pga = 10, .pad = 5, .dac = 0, .bb_mult = 62, },
	{ .gm = 4, .pga = 10, .pad = 5, .dac = 0, .bb_mult = 60, },
	{ .gm = 4, .pga = 10, .pad = 5, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 9, .pad = 5, .dac = 0, .bb_mult = 70, },
	{ .gm = 4, .pga = 9, .pad = 5, .dac = 0, .bb_mult = 68, },
	{ .gm = 4, .pga = 9, .pad = 5, .dac = 0, .bb_mult = 66, },
	{ .gm = 4, .pga = 9, .pad = 5, .dac = 0, .bb_mult = 64, },
	{ .gm = 4, .pga = 9, .pad = 5, .dac = 0, .bb_mult = 63, },
	{ .gm = 4, .pga = 9, .pad = 5, .dac = 0, .bb_mult = 61, },
	{ .gm = 4, .pga = 9, .pad = 5, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 9, .pad = 4, .dac = 0, .bb_mult = 71, },
	{ .gm = 4, .pga = 9, .pad = 4, .dac = 0, .bb_mult = 69, },
	{ .gm = 4, .pga = 9, .pad = 4, .dac = 0, .bb_mult = 67, },
	{ .gm = 4, .pga = 9, .pad = 4, .dac = 0, .bb_mult = 65, },
	{ .gm = 4, .pga = 9, .pad = 4, .dac = 0, .bb_mult = 63, },
	{ .gm = 4, .pga = 9, .pad = 4, .dac = 0, .bb_mult = 62, },
	{ .gm = 4, .pga = 9, .pad = 4, .dac = 0, .bb_mult = 60, },
	{ .gm = 4, .pga = 9, .pad = 4, .dac = 0, .bb_mult = 58, },
	{ .gm = 4, .pga = 8, .pad = 4, .dac = 0, .bb_mult = 70, },
	{ .gm = 4, .pga = 8, .pad = 4, .dac = 0, .bb_mult = 68, },
	{ .gm = 4, .pga = 8, .pad = 4, .dac = 0, .bb_mult = 66, },
	{ .gm = 4, .pga = 8, .pad = 4, .dac = 0, .bb_mult = 65, },
	{ .gm = 4, .pga = 8, .pad = 4, .dac = 0, .bb_mult = 63, },
	{ .gm = 4, .pga = 8, .pad = 4, .dac = 0, .bb_mult = 61, },
	{ .gm = 4, .pga = 8, .pad = 4, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 7, .pad = 4, .dac = 0, .bb_mult = 68, },
	{ .gm = 4, .pga = 7, .pad = 4, .dac = 0, .bb_mult = 66, },
	{ .gm = 4, .pga = 7, .pad = 4, .dac = 0, .bb_mult = 64, },
	{ .gm = 4, .pga = 7, .pad = 4, .dac = 0, .bb_mult = 62, },
	{ .gm = 4, .pga = 7, .pad = 4, .dac = 0, .bb_mult = 61, },
	{ .gm = 4, .pga = 7, .pad = 4, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 7, .pad = 3, .dac = 0, .bb_mult = 67, },
	{ .gm = 4, .pga = 7, .pad = 3, .dac = 0, .bb_mult = 65, },
	{ .gm = 4, .pga = 7, .pad = 3, .dac = 0, .bb_mult = 63, },
	{ .gm = 4, .pga = 7, .pad = 3, .dac = 0, .bb_mult = 62, },
	{ .gm = 4, .pga = 7, .pad = 3, .dac = 0, .bb_mult = 60, },
	{ .gm = 4, .pga = 6, .pad = 3, .dac = 0, .bb_mult = 65, },
	{ .gm = 4, .pga = 6, .pad = 3, .dac = 0, .bb_mult = 63, },
	{ .gm = 4, .pga = 6, .pad = 3, .dac = 0, .bb_mult = 61, },
	{ .gm = 4, .pga = 6, .pad = 3, .dac = 0, .bb_mult = 60, },
	{ .gm = 4, .pga = 6, .pad = 3, .dac = 0, .bb_mult = 58, },
	{ .gm = 4, .pga = 5, .pad = 3, .dac = 0, .bb_mult = 68, },
	{ .gm = 4, .pga = 5, .pad = 3, .dac = 0, .bb_mult = 66, },
	{ .gm = 4, .pga = 5, .pad = 3, .dac = 0, .bb_mult = 64, },
	{ .gm = 4, .pga = 5, .pad = 3, .dac = 0, .bb_mult = 62, },
	{ .gm = 4, .pga = 5, .pad = 3, .dac = 0, .bb_mult = 60, },
	{ .gm = 4, .pga = 5, .pad = 3, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 5, .pad = 3, .dac = 0, .bb_mult = 57, },
	{ .gm = 4, .pga = 4, .pad = 2, .dac = 0, .bb_mult = 83, },
	{ .gm = 4, .pga = 4, .pad = 2, .dac = 0, .bb_mult = 81, },
	{ .gm = 4, .pga = 4, .pad = 2, .dac = 0, .bb_mult = 78, },
	{ .gm = 4, .pga = 4, .pad = 2, .dac = 0, .bb_mult = 76, },
	{ .gm = 4, .pga = 4, .pad = 2, .dac = 0, .bb_mult = 74, },
	{ .gm = 4, .pga = 4, .pad = 2, .dac = 0, .bb_mult = 72, },
};

static struct lpphy_tx_gain_table_entry lpphy_rev0_5ghz_tx_gain_table[] = {
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 99, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 96, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 93, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 90, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 88, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 85, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 83, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 81, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 78, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 76, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 74, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 72, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 57, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 55, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 72, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 58, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 56, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 55, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 71, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 69, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 67, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 65, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 63, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 58, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 56, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 72, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 57, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 73, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 71, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 69, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 67, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 65, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 63, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 58, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 71, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 69, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 67, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 65, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 63, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 58, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 57, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 56, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 65, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 63, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 58, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 63, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 57, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 57, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 57, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 57, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 57, },
	{ .gm = 7, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 69, },
	{ .gm = 7, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 67, },
	{ .gm = 7, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 65, },
	{ .gm = 7, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 63, },
	{ .gm = 7, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 60, },
};

static struct lpphy_tx_gain_table_entry lpphy_rev1_nopa_tx_gain_table[] = {
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 152, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 147, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 143, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 139, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 135, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 131, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 128, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 124, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 121, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 117, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 114, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 111, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 107, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 104, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 101, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 99, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 96, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 93, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 90, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 88, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 85, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 83, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 81, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 78, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 76, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 74, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 72, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 57, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 72, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 57, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 72, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 57, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 71, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 69, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 67, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 65, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 63, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 58, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 57, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 65, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 63, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 58, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 71, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 69, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 67, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 65, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 63, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 58, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 56, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 72, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 72, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 63, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 57, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 72, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 72, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 73, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 71, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 69, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 67, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 65, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 63, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 72, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 65, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 63, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 73, },
	{ .gm = 7, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 71, },
};

static struct lpphy_tx_gain_table_entry lpphy_rev1_2ghz_tx_gain_table[] = {
	{ .gm = 4, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 90, },
	{ .gm = 4, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 88, },
	{ .gm = 4, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 85, },
	{ .gm = 4, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 83, },
	{ .gm = 4, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 81, },
	{ .gm = 4, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 78, },
	{ .gm = 4, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 76, },
	{ .gm = 4, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 74, },
	{ .gm = 4, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 72, },
	{ .gm = 4, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 70, },
	{ .gm = 4, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 68, },
	{ .gm = 4, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 66, },
	{ .gm = 4, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 64, },
	{ .gm = 4, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 62, },
	{ .gm = 4, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 60, },
	{ .gm = 4, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 72, },
	{ .gm = 4, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 70, },
	{ .gm = 4, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 68, },
	{ .gm = 4, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 66, },
	{ .gm = 4, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 64, },
	{ .gm = 4, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 62, },
	{ .gm = 4, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 60, },
	{ .gm = 4, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 72, },
	{ .gm = 4, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 70, },
	{ .gm = 4, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 68, },
	{ .gm = 4, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 66, },
	{ .gm = 4, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 64, },
	{ .gm = 4, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 62, },
	{ .gm = 4, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 60, },
	{ .gm = 4, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 72, },
	{ .gm = 4, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 70, },
	{ .gm = 4, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 68, },
	{ .gm = 4, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 66, },
	{ .gm = 4, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 64, },
	{ .gm = 4, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 62, },
	{ .gm = 4, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 60, },
	{ .gm = 4, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 72, },
	{ .gm = 4, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 70, },
	{ .gm = 4, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 68, },
	{ .gm = 4, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 66, },
	{ .gm = 4, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 64, },
	{ .gm = 4, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 62, },
	{ .gm = 4, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 60, },
	{ .gm = 4, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 72, },
	{ .gm = 4, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 70, },
	{ .gm = 4, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 68, },
	{ .gm = 4, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 66, },
	{ .gm = 4, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 64, },
	{ .gm = 4, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 62, },
	{ .gm = 4, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 60, },
	{ .gm = 4, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 72, },
	{ .gm = 4, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 70, },
	{ .gm = 4, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 68, },
	{ .gm = 4, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 66, },
	{ .gm = 4, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 64, },
	{ .gm = 4, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 62, },
	{ .gm = 4, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 60, },
	{ .gm = 4, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 72, },
	{ .gm = 4, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 70, },
	{ .gm = 4, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 68, },
	{ .gm = 4, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 66, },
	{ .gm = 4, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 64, },
	{ .gm = 4, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 62, },
	{ .gm = 4, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 60, },
	{ .gm = 4, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 72, },
	{ .gm = 4, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 70, },
	{ .gm = 4, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 68, },
	{ .gm = 4, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 66, },
	{ .gm = 4, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 64, },
	{ .gm = 4, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 63, },
	{ .gm = 4, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 61, },
	{ .gm = 4, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 57, },
	{ .gm = 4, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 72, },
	{ .gm = 4, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 70, },
	{ .gm = 4, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 68, },
	{ .gm = 4, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 66, },
	{ .gm = 4, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 64, },
	{ .gm = 4, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 62, },
	{ .gm = 4, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 60, },
	{ .gm = 4, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 72, },
	{ .gm = 4, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 70, },
	{ .gm = 4, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 68, },
	{ .gm = 4, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 66, },
	{ .gm = 4, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 64, },
	{ .gm = 4, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 62, },
	{ .gm = 4, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 61, },
	{ .gm = 4, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 73, },
	{ .gm = 4, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 71, },
	{ .gm = 4, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 69, },
	{ .gm = 4, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 67, },
	{ .gm = 4, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 65, },
	{ .gm = 4, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 63, },
	{ .gm = 4, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 61, },
	{ .gm = 4, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 72, },
	{ .gm = 4, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 70, },
	{ .gm = 4, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 68, },
	{ .gm = 4, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 66, },
	{ .gm = 4, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 65, },
	{ .gm = 4, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 63, },
	{ .gm = 4, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 61, },
	{ .gm = 4, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 59, },
	{ .gm = 4, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 73, },
	{ .gm = 4, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 71, },
	{ .gm = 4, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 69, },
	{ .gm = 4, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 67, },
	{ .gm = 4, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 65, },
	{ .gm = 4, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 63, },
	{ .gm = 4, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 61, },
	{ .gm = 4, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 60, },
	{ .gm = 4, .pga = 10, .pad = 6, .dac = 0, .bb_mult = 72, },
	{ .gm = 4, .pga = 10, .pad = 6, .dac = 0, .bb_mult = 70, },
	{ .gm = 4, .pga = 10, .pad = 6, .dac = 0, .bb_mult = 68, },
	{ .gm = 4, .pga = 10, .pad = 6, .dac = 0, .bb_mult = 66, },
	{ .gm = 4, .pga = 10, .pad = 6, .dac = 0, .bb_mult = 64, },
	{ .gm = 4, .pga = 10, .pad = 6, .dac = 0, .bb_mult = 62, },
	{ .gm = 4, .pga = 10, .pad = 6, .dac = 0, .bb_mult = 60, },
};

static struct lpphy_tx_gain_table_entry lpphy_rev1_5ghz_tx_gain_table[] = {
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 99, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 96, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 93, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 90, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 88, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 85, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 83, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 81, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 78, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 76, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 74, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 72, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 57, },
	{ .gm = 7, .pga = 15, .pad = 15, .dac = 0, .bb_mult = 55, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 72, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 58, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 56, },
	{ .gm = 7, .pga = 15, .pad = 14, .dac = 0, .bb_mult = 55, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 71, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 69, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 67, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 65, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 63, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 58, },
	{ .gm = 7, .pga = 15, .pad = 13, .dac = 0, .bb_mult = 56, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 72, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 15, .pad = 12, .dac = 0, .bb_mult = 57, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 73, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 71, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 69, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 67, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 65, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 63, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 15, .pad = 11, .dac = 0, .bb_mult = 58, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 71, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 69, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 67, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 65, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 63, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 15, .pad = 10, .dac = 0, .bb_mult = 58, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 57, },
	{ .gm = 7, .pga = 15, .pad = 9, .dac = 0, .bb_mult = 56, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 65, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 63, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 14, .pad = 9, .dac = 0, .bb_mult = 58, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 63, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 13, .pad = 9, .dac = 0, .bb_mult = 57, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 60, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 13, .pad = 8, .dac = 0, .bb_mult = 57, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 12, .pad = 8, .dac = 0, .bb_mult = 57, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 12, .pad = 7, .dac = 0, .bb_mult = 57, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 70, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 68, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 66, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 61, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 59, },
	{ .gm = 7, .pga = 11, .pad = 7, .dac = 0, .bb_mult = 57, },
	{ .gm = 7, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 69, },
	{ .gm = 7, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 67, },
	{ .gm = 7, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 65, },
	{ .gm = 7, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 63, },
	{ .gm = 7, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 62, },
	{ .gm = 7, .pga = 11, .pad = 6, .dac = 0, .bb_mult = 60, },
};

static struct lpphy_tx_gain_table_entry lpphy_rev2_nopa_tx_gain_table[] = {
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 152, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 147, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 143, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 139, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 135, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 131, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 128, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 124, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 121, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 117, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 114, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 111, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 107, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 104, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 101, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 99, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 96, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 93, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 90, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 88, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 85, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 83, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 81, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 78, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 76, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 74, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 72, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 70, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 68, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 66, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 197, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 192, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 186, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 181, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 176, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 171, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 166, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 161, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 157, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 152, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 148, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 144, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 140, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 136, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 132, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 128, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 124, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 121, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 117, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 114, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 111, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 108, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 105, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 102, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 99, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 96, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 93, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 91, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 88, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 86, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 83, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 81, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 79, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 76, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 74, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 72, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 70, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 68, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 66, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 64, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 248, .pad = 64, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 248, .pad = 62, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 241, .pad = 62, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 241, .pad = 60, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 234, .pad = 60, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 234, .pad = 59, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 227, .pad = 59, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 227, .pad = 57, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 221, .pad = 57, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 221, .pad = 55, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 215, .pad = 55, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 215, .pad = 54, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 208, .pad = 54, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 208, .pad = 52, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 203, .pad = 52, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 203, .pad = 51, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 197, .pad = 51, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 197, .pad = 49, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 191, .pad = 49, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 191, .pad = 48, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 186, .pad = 48, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 186, .pad = 47, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 181, .pad = 47, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 181, .pad = 45, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 175, .pad = 45, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 175, .pad = 44, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 170, .pad = 44, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 170, .pad = 43, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 166, .pad = 43, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 166, .pad = 42, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 161, .pad = 42, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 161, .pad = 40, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 156, .pad = 40, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 156, .pad = 39, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 152, .pad = 39, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 152, .pad = 38, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 148, .pad = 38, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 148, .pad = 37, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 143, .pad = 37, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 143, .pad = 36, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 139, .pad = 36, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 139, .pad = 35, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 135, .pad = 35, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 135, .pad = 34, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 132, .pad = 34, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 132, .pad = 33, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 128, .pad = 33, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 128, .pad = 32, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 124, .pad = 32, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 124, .pad = 31, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 121, .pad = 31, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 121, .pad = 30, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 117, .pad = 30, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 117, .pad = 29, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 114, .pad = 29, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 114, .pad = 29, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 111, .pad = 29, .dac = 0, .bb_mult = 64, },
};

static struct lpphy_tx_gain_table_entry lpphy_rev2_2ghz_tx_gain_table[] = {
	{ .gm = 7, .pga = 99, .pad = 255, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 96, .pad = 255, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 93, .pad = 255, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 90, .pad = 255, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 88, .pad = 255, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 85, .pad = 255, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 83, .pad = 255, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 81, .pad = 255, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 78, .pad = 255, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 76, .pad = 255, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 74, .pad = 255, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 72, .pad = 255, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 70, .pad = 255, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 68, .pad = 255, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 66, .pad = 255, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 64, .pad = 255, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 64, .pad = 255, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 62, .pad = 255, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 62, .pad = 248, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 60, .pad = 248, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 60, .pad = 241, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 59, .pad = 241, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 59, .pad = 234, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 57, .pad = 234, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 57, .pad = 227, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 55, .pad = 227, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 55, .pad = 221, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 54, .pad = 221, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 54, .pad = 215, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 52, .pad = 215, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 52, .pad = 208, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 51, .pad = 208, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 51, .pad = 203, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 49, .pad = 203, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 49, .pad = 197, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 48, .pad = 197, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 48, .pad = 191, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 47, .pad = 191, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 47, .pad = 186, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 45, .pad = 186, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 45, .pad = 181, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 44, .pad = 181, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 44, .pad = 175, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 43, .pad = 175, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 43, .pad = 170, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 42, .pad = 170, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 42, .pad = 166, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 40, .pad = 166, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 40, .pad = 161, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 39, .pad = 161, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 39, .pad = 156, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 38, .pad = 156, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 38, .pad = 152, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 37, .pad = 152, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 37, .pad = 148, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 36, .pad = 148, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 36, .pad = 143, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 35, .pad = 143, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 35, .pad = 139, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 34, .pad = 139, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 34, .pad = 135, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 33, .pad = 135, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 33, .pad = 132, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 32, .pad = 132, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 32, .pad = 128, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 31, .pad = 128, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 31, .pad = 124, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 30, .pad = 124, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 30, .pad = 121, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 29, .pad = 121, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 29, .pad = 117, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 29, .pad = 117, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 29, .pad = 114, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 28, .pad = 114, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 28, .pad = 111, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 27, .pad = 111, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 27, .pad = 108, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 26, .pad = 108, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 26, .pad = 104, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 25, .pad = 104, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 25, .pad = 102, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 25, .pad = 102, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 25, .pad = 99, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 24, .pad = 99, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 24, .pad = 96, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 23, .pad = 96, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 23, .pad = 93, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 23, .pad = 93, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 23, .pad = 90, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 22, .pad = 90, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 22, .pad = 88, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 21, .pad = 88, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 21, .pad = 85, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 21, .pad = 85, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 21, .pad = 83, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 20, .pad = 83, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 20, .pad = 81, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 20, .pad = 81, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 20, .pad = 78, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 19, .pad = 78, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 19, .pad = 76, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 19, .pad = 76, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 19, .pad = 74, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 18, .pad = 74, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 18, .pad = 72, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 18, .pad = 72, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 18, .pad = 70, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 17, .pad = 70, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 17, .pad = 68, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 17, .pad = 68, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 17, .pad = 66, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 16, .pad = 66, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 16, .pad = 64, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 16, .pad = 64, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 16, .pad = 62, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 15, .pad = 62, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 15, .pad = 60, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 15, .pad = 60, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 15, .pad = 59, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 14, .pad = 59, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 14, .pad = 57, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 14, .pad = 57, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 14, .pad = 55, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 14, .pad = 55, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 14, .pad = 54, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 13, .pad = 54, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 13, .pad = 52, .dac = 0, .bb_mult = 64, },
	{ .gm = 7, .pga = 13, .pad = 52, .dac = 0, .bb_mult = 64, },
};

static struct lpphy_tx_gain_table_entry lpphy_rev2_5ghz_tx_gain_table[] = {
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 152, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 147, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 143, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 139, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 135, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 131, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 128, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 124, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 121, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 117, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 114, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 111, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 107, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 104, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 101, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 99, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 96, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 93, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 90, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 88, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 85, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 83, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 81, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 78, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 76, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 74, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 72, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 70, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 68, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 66, },
	{ .gm = 255, .pga = 255, .pad = 255, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 248, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 241, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 234, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 227, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 221, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 215, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 208, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 203, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 197, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 191, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 186, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 181, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 175, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 170, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 166, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 161, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 156, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 152, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 148, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 143, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 139, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 135, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 132, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 128, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 124, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 121, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 117, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 114, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 111, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 108, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 104, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 102, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 99, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 96, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 93, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 90, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 88, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 85, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 83, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 81, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 78, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 76, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 74, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 72, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 70, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 68, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 66, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 64, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 64, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 255, .pad = 62, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 248, .pad = 62, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 248, .pad = 60, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 241, .pad = 60, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 241, .pad = 59, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 234, .pad = 59, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 234, .pad = 57, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 227, .pad = 57, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 227, .pad = 55, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 221, .pad = 55, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 221, .pad = 54, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 215, .pad = 54, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 215, .pad = 52, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 208, .pad = 52, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 208, .pad = 51, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 203, .pad = 51, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 203, .pad = 49, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 197, .pad = 49, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 197, .pad = 48, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 191, .pad = 48, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 191, .pad = 47, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 186, .pad = 47, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 186, .pad = 45, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 181, .pad = 45, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 181, .pad = 44, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 175, .pad = 44, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 175, .pad = 43, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 170, .pad = 43, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 170, .pad = 42, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 166, .pad = 42, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 166, .pad = 40, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 161, .pad = 40, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 161, .pad = 39, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 156, .pad = 39, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 156, .pad = 38, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 152, .pad = 38, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 152, .pad = 37, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 148, .pad = 37, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 148, .pad = 36, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 143, .pad = 36, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 143, .pad = 35, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 139, .pad = 35, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 139, .pad = 34, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 135, .pad = 34, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 135, .pad = 33, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 132, .pad = 33, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 132, .pad = 32, .dac = 0, .bb_mult = 64, },
	{ .gm = 255, .pga = 128, .pad = 32, .dac = 0, .bb_mult = 64, },
};

void lpphy_rev0_1_table_init(struct b43_wldev *dev)
{
	B43_WARN_ON(dev->phy.rev >= 2);

	b43_lptab_write_bulk(dev, B43_LPTAB8(2, 0),
		ARRAY_SIZE(lpphy_min_sig_sq_table), lpphy_min_sig_sq_table);
	b43_lptab_write_bulk(dev, B43_LPTAB16(1, 0),
		ARRAY_SIZE(lpphy_rev01_noise_scale_table), lpphy_rev01_noise_scale_table);
	b43_lptab_write_bulk(dev, B43_LPTAB16(14, 0),
		ARRAY_SIZE(lpphy_crs_gain_nft_table), lpphy_crs_gain_nft_table);
	b43_lptab_write_bulk(dev, B43_LPTAB16(8, 0),
		ARRAY_SIZE(lpphy_rev01_filter_control_table), lpphy_rev01_filter_control_table);
	b43_lptab_write_bulk(dev, B43_LPTAB32(9, 0),
		ARRAY_SIZE(lpphy_rev01_ps_control_table), lpphy_rev01_ps_control_table);
	b43_lptab_write_bulk(dev, B43_LPTAB8(6, 0),
		ARRAY_SIZE(lpphy_pll_fraction_table), lpphy_pll_fraction_table);
	b43_lptab_write_bulk(dev, B43_LPTAB16(0, 0),
		ARRAY_SIZE(lpphy_iqlo_cal_table), lpphy_iqlo_cal_table);
	if (dev->phy.rev == 0) {
		b43_lptab_write_bulk(dev, B43_LPTAB16(13, 0),
			ARRAY_SIZE(lpphy_rev0_ofdm_cck_gain_table), lpphy_rev0_ofdm_cck_gain_table);
		b43_lptab_write_bulk(dev, B43_LPTAB16(12, 0),
			ARRAY_SIZE(lpphy_rev0_ofdm_cck_gain_table), lpphy_rev0_ofdm_cck_gain_table);
	} else {
		b43_lptab_write_bulk(dev, B43_LPTAB16(13, 0),
			ARRAY_SIZE(lpphy_rev1_ofdm_cck_gain_table), lpphy_rev1_ofdm_cck_gain_table);
		b43_lptab_write_bulk(dev, B43_LPTAB16(12, 0),
			ARRAY_SIZE(lpphy_rev1_ofdm_cck_gain_table), lpphy_rev1_ofdm_cck_gain_table);
}
	b43_lptab_write_bulk(dev, B43_LPTAB16(15, 0),
		ARRAY_SIZE(lpphy_gain_delta_table), lpphy_gain_delta_table);
	b43_lptab_write_bulk(dev, B43_LPTAB32(10, 0),
		ARRAY_SIZE(lpphy_tx_power_control_table), lpphy_tx_power_control_table);
}

void lpphy_rev2plus_table_init(struct b43_wldev *dev)
{
	int i;

	B43_WARN_ON(dev->phy.rev < 2);

	for (i = 0; i < 704; i++)
		b43_lptab_write(dev, B43_LPTAB32(7, i), 0);

	b43_lptab_write_bulk(dev, B43_LPTAB8(2, 0),
		ARRAY_SIZE(lpphy_min_sig_sq_table), lpphy_min_sig_sq_table);
	b43_lptab_write_bulk(dev, B43_LPTAB16(1, 0),
		ARRAY_SIZE(lpphy_rev2plus_noise_scale_table), lpphy_rev2plus_noise_scale_table);
	b43_lptab_write_bulk(dev, B43_LPTAB32(11, 0),
		ARRAY_SIZE(lpphy_rev2plus_filter_control_table), lpphy_rev2plus_filter_control_table);
	b43_lptab_write_bulk(dev, B43_LPTAB32(12, 0),
		ARRAY_SIZE(lpphy_rev2plus_ps_control_table), lpphy_rev2plus_ps_control_table);
	b43_lptab_write_bulk(dev, B43_LPTAB32(13, 0),
		ARRAY_SIZE(lpphy_gain_idx_table), lpphy_gain_idx_table);
	b43_lptab_write_bulk(dev, B43_LPTAB16(14, 0),
		ARRAY_SIZE(lpphy_aux_gain_idx_table), lpphy_aux_gain_idx_table);
	b43_lptab_write_bulk(dev, B43_LPTAB16(15, 0),
		ARRAY_SIZE(lpphy_sw_control_table), lpphy_sw_control_table);
	b43_lptab_write_bulk(dev, B43_LPTAB8(16, 0),
		ARRAY_SIZE(lpphy_hf_table), lpphy_hf_table);
	b43_lptab_write_bulk(dev, B43_LPTAB32(17, 0),
		ARRAY_SIZE(lpphy_gain_value_table), lpphy_gain_value_table);
	b43_lptab_write_bulk(dev, B43_LPTAB16(18, 0),
		ARRAY_SIZE(lpphy_gain_table), lpphy_gain_table);
	b43_lptab_write_bulk(dev, B43_LPTAB8(6, 0),
		ARRAY_SIZE(lpphy_pll_fraction_table), lpphy_pll_fraction_table);
	b43_lptab_write_bulk(dev, B43_LPTAB16(0, 0),
		ARRAY_SIZE(lpphy_iqlo_cal_table), lpphy_iqlo_cal_table);
	b43_lptab_write_bulk(dev, B43_LPTAB32(9, 0),
		ARRAY_SIZE(lpphy_papd_eps_table), lpphy_papd_eps_table);
	b43_lptab_write_bulk(dev, B43_LPTAB32(10, 0),
		ARRAY_SIZE(lpphy_papd_mult_table), lpphy_papd_mult_table);

	if ((dev->dev->chip_id == 0x4325) && (dev->dev->chip_rev == 0)) {
		b43_lptab_write_bulk(dev, B43_LPTAB32(13, 0),
			ARRAY_SIZE(lpphy_a0_gain_idx_table), lpphy_a0_gain_idx_table);
		b43_lptab_write_bulk(dev, B43_LPTAB16(14, 0),
			ARRAY_SIZE(lpphy_a0_aux_gain_idx_table), lpphy_a0_aux_gain_idx_table);
		b43_lptab_write_bulk(dev, B43_LPTAB32(17, 0),
			ARRAY_SIZE(lpphy_a0_gain_value_table), lpphy_a0_gain_value_table);
		b43_lptab_write_bulk(dev, B43_LPTAB16(18, 0),
			ARRAY_SIZE(lpphy_a0_gain_table), lpphy_a0_gain_table);
	}
}

static void lpphy_rev0_1_write_gain_table(struct b43_wldev *dev, int offset,
				struct lpphy_tx_gain_table_entry data)
{
	u32 tmp;

	B43_WARN_ON(dev->phy.rev >= 2);

	tmp  = data.pad << 11;
	tmp |= data.pga << 7;
	tmp |= data.gm  << 4;
	tmp |= data.dac;
	b43_lptab_write(dev, B43_LPTAB32(10, 0xC0 + offset), tmp);
	tmp  = data.bb_mult << 20;
	b43_lptab_write(dev, B43_LPTAB32(10, 0x140 + offset), tmp);
}

static void lpphy_rev2plus_write_gain_table(struct b43_wldev *dev, int offset,
				struct lpphy_tx_gain_table_entry data)
{
	u32 tmp;

	B43_WARN_ON(dev->phy.rev < 2);

	tmp  = data.pad << 16;
	tmp |= data.pga << 8;
	tmp |= data.gm;
	if (dev->phy.rev >= 3) {
		if (b43_current_band(dev->wl) == NL80211_BAND_5GHZ)
			tmp |= 0x10 << 24;
		else
			tmp |= 0x70 << 24;
	} else {
		if (b43_current_band(dev->wl) == NL80211_BAND_5GHZ)
			tmp |= 0x14 << 24;
		else
			tmp |= 0x7F << 24;
	}
	b43_lptab_write(dev, B43_LPTAB32(7, 0xC0 + offset), tmp);
	tmp  = data.bb_mult << 20;
	tmp |= data.dac << 28;
	b43_lptab_write(dev, B43_LPTAB32(7, 0x140 + offset), tmp);
}

void lpphy_write_gain_table(struct b43_wldev *dev, int offset,
			    struct lpphy_tx_gain_table_entry data)
{
	if (dev->phy.rev >= 2)
		lpphy_rev2plus_write_gain_table(dev, offset, data);
	else
		lpphy_rev0_1_write_gain_table(dev, offset, data);
}

void lpphy_write_gain_table_bulk(struct b43_wldev *dev, int offset, int count,
				 struct lpphy_tx_gain_table_entry *table)
{
	int i;

	for (i = offset; i < count; i++)
		lpphy_write_gain_table(dev, i, table[i]);
}

void lpphy_init_tx_gain_table(struct b43_wldev *dev)
{
	struct ssb_sprom *sprom = dev->dev->bus_sprom;

	switch (dev->phy.rev) {
	case 0:
		if ((sprom->boardflags_hi & B43_BFH_NOPA) ||
		    (sprom->boardflags_lo & B43_BFL_HGPA))
			lpphy_write_gain_table_bulk(dev, 0, 128,
					lpphy_rev0_nopa_tx_gain_table);
		else if (b43_current_band(dev->wl) == NL80211_BAND_2GHZ)
			lpphy_write_gain_table_bulk(dev, 0, 128,
					lpphy_rev0_2ghz_tx_gain_table);
		else
			lpphy_write_gain_table_bulk(dev, 0, 128,
					lpphy_rev0_5ghz_tx_gain_table);
		break;
	case 1:
		if ((sprom->boardflags_hi & B43_BFH_NOPA) ||
		    (sprom->boardflags_lo & B43_BFL_HGPA))
			lpphy_write_gain_table_bulk(dev, 0, 128,
					lpphy_rev1_nopa_tx_gain_table);
		else if (b43_current_band(dev->wl) == NL80211_BAND_2GHZ)
			lpphy_write_gain_table_bulk(dev, 0, 128,
					lpphy_rev1_2ghz_tx_gain_table);
		else
			lpphy_write_gain_table_bulk(dev, 0, 128,
					lpphy_rev1_5ghz_tx_gain_table);
		break;
	default:
		if (sprom->boardflags_hi & B43_BFH_NOPA)
			lpphy_write_gain_table_bulk(dev, 0, 128,
					lpphy_rev2_nopa_tx_gain_table);
		else if (b43_current_band(dev->wl) == NL80211_BAND_2GHZ)
			lpphy_write_gain_table_bulk(dev, 0, 128,
					lpphy_rev2_2ghz_tx_gain_table);
		else
			lpphy_write_gain_table_bulk(dev, 0, 128,
					lpphy_rev2_5ghz_tx_gain_table);
	}
}

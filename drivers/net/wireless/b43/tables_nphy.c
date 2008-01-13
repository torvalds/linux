/*

  Broadcom B43 wireless driver
  IEEE 802.11n PHY and radio device data tables

  Copyright (c) 2008 Michael Buesch <mb@bu3sch.de>

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
#include "tables_nphy.h"
#include "phy.h"
#include "nphy.h"


struct b2055_inittab_entry {
	/* Value to write if we use the 5GHz band. */
	u16 ghz5;
	/* Value to write if we use the 2.4GHz band. */
	u16 ghz2;
	/* Flags */
	u8 flags;
#define B2055_INITTAB_ENTRY_OK	0x01
#define B2055_INITTAB_UPLOAD	0x02
};
#define UPLOAD		.flags = B2055_INITTAB_ENTRY_OK | B2055_INITTAB_UPLOAD
#define NOUPLOAD	.flags = B2055_INITTAB_ENTRY_OK

static struct b2055_inittab_entry b2055_inittab [] = {
  [B2055_SP_PINPD]		= { .ghz5 = 0x0080, .ghz2 = 0x0080, NOUPLOAD, },
  [B2055_C1_SP_RSSI]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_C1_SP_PDMISC]		= { .ghz5 = 0x0027, .ghz2 = 0x0027, NOUPLOAD, },
  [B2055_C2_SP_RSSI]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_C2_SP_PDMISC]		= { .ghz5 = 0x0027, .ghz2 = 0x0027, NOUPLOAD, },
  [B2055_C1_SP_RXGC1]		= { .ghz5 = 0x007F, .ghz2 = 0x007F, UPLOAD, },
  [B2055_C1_SP_RXGC2]		= { .ghz5 = 0x0007, .ghz2 = 0x0007, UPLOAD, },
  [B2055_C2_SP_RXGC1]		= { .ghz5 = 0x007F, .ghz2 = 0x007F, UPLOAD, },
  [B2055_C2_SP_RXGC2]		= { .ghz5 = 0x0007, .ghz2 = 0x0007, UPLOAD, },
  [B2055_C1_SP_LPFBWSEL]	= { .ghz5 = 0x0015, .ghz2 = 0x0015, NOUPLOAD, },
  [B2055_C2_SP_LPFBWSEL]	= { .ghz5 = 0x0015, .ghz2 = 0x0015, NOUPLOAD, },
  [B2055_C1_SP_TXGC1]		= { .ghz5 = 0x004F, .ghz2 = 0x004F, UPLOAD, },
  [B2055_C1_SP_TXGC2]		= { .ghz5 = 0x0005, .ghz2 = 0x0005, UPLOAD, },
  [B2055_C2_SP_TXGC1]		= { .ghz5 = 0x004F, .ghz2 = 0x004F, UPLOAD, },
  [B2055_C2_SP_TXGC2]		= { .ghz5 = 0x0005, .ghz2 = 0x0005, UPLOAD, },
  [B2055_MASTER1]		= { .ghz5 = 0x00D0, .ghz2 = 0x00D0, NOUPLOAD, },
  [B2055_MASTER2]		= { .ghz5 = 0x0002, .ghz2 = 0x0002, NOUPLOAD, },
  [B2055_PD_LGEN]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_PD_PLLTS]		= { .ghz5 = 0x0040, .ghz2 = 0x0040, NOUPLOAD, },
  [B2055_C1_PD_LGBUF]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_C1_PD_TX]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_C1_PD_RXTX]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_C1_PD_RSSIMISC]	= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_C2_PD_LGBUF]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_C2_PD_TX]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_C2_PD_RXTX]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_C2_PD_RSSIMISC]	= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_PWRDET_LGEN]		= { .ghz5 = 0x00C0, .ghz2 = 0x00C0, NOUPLOAD, },
  [B2055_C1_PWRDET_LGBUF]	= { .ghz5 = 0x00FF, .ghz2 = 0x00FF, NOUPLOAD, },
  [B2055_C1_PWRDET_RXTX]	= { .ghz5 = 0x00C0, .ghz2 = 0x00C0, NOUPLOAD, },
  [B2055_C2_PWRDET_LGBUF]	= { .ghz5 = 0x00FF, .ghz2 = 0x00FF, NOUPLOAD, },
  [B2055_C2_PWRDET_RXTX]	= { .ghz5 = 0x00C0, .ghz2 = 0x00C0, NOUPLOAD, },
  [B2055_RRCCAL_CS]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_RRCCAL_NOPTSEL]	= { .ghz5 = 0x002C, .ghz2 = 0x002C, NOUPLOAD, },
  [B2055_CAL_MISC]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_CAL_COUT]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_CAL_COUT2]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_CAL_CVARCTL]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_CAL_RVARCTL]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_CAL_LPOCTL]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_CAL_TS]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_CAL_RCCALRTS]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_CAL_RCALRTS]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_PADDRV]		= { .ghz5 = 0x00A4, .ghz2 = 0x00A4, NOUPLOAD, },
  [B2055_XOCTL1]		= { .ghz5 = 0x0038, .ghz2 = 0x0038, NOUPLOAD, },
  [B2055_XOCTL2]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_XOREGUL]		= { .ghz5 = 0x0004, .ghz2 = 0x0004, UPLOAD, },
  [B2055_XOMISC]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_PLL_LFC1]		= { .ghz5 = 0x000A, .ghz2 = 0x000A, NOUPLOAD, },
  [B2055_PLL_CALVTH]		= { .ghz5 = 0x0087, .ghz2 = 0x0087, NOUPLOAD, },
  [B2055_PLL_LFC2]		= { .ghz5 = 0x0009, .ghz2 = 0x0009, NOUPLOAD, },
  [B2055_PLL_REF]		= { .ghz5 = 0x0070, .ghz2 = 0x0070, NOUPLOAD, },
  [B2055_PLL_LFR1]		= { .ghz5 = 0x0011, .ghz2 = 0x0011, NOUPLOAD, },
  [B2055_PLL_PFDCP]		= { .ghz5 = 0x0018, .ghz2 = 0x0018, UPLOAD, },
  [B2055_PLL_IDAC_CPOPAMP]	= { .ghz5 = 0x0006, .ghz2 = 0x0006, NOUPLOAD, },
  [B2055_PLL_CPREG]		= { .ghz5 = 0x0004, .ghz2 = 0x0004, UPLOAD, },
  [B2055_PLL_RCAL]		= { .ghz5 = 0x0006, .ghz2 = 0x0006, NOUPLOAD, },
  [B2055_RF_PLLMOD0]		= { .ghz5 = 0x009E, .ghz2 = 0x009E, NOUPLOAD, },
  [B2055_RF_PLLMOD1]		= { .ghz5 = 0x0009, .ghz2 = 0x0009, NOUPLOAD, },
  [B2055_RF_MMDIDAC1]		= { .ghz5 = 0x00C8, .ghz2 = 0x00C8, UPLOAD, },
  [B2055_RF_MMDIDAC0]		= { .ghz5 = 0x0088, .ghz2 = 0x0088, NOUPLOAD, },
  [B2055_RF_MMDSP]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_VCO_CAL1]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_VCO_CAL2]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_VCO_CAL3]		= { .ghz5 = 0x0001, .ghz2 = 0x0001, NOUPLOAD, },
  [B2055_VCO_CAL4]		= { .ghz5 = 0x0002, .ghz2 = 0x0002, NOUPLOAD, },
  [B2055_VCO_CAL5]		= { .ghz5 = 0x0096, .ghz2 = 0x0096, NOUPLOAD, },
  [B2055_VCO_CAL6]		= { .ghz5 = 0x003E, .ghz2 = 0x003E, NOUPLOAD, },
  [B2055_VCO_CAL7]		= { .ghz5 = 0x003E, .ghz2 = 0x003E, NOUPLOAD, },
  [B2055_VCO_CAL8]		= { .ghz5 = 0x0013, .ghz2 = 0x0013, NOUPLOAD, },
  [B2055_VCO_CAL9]		= { .ghz5 = 0x0002, .ghz2 = 0x0002, NOUPLOAD, },
  [B2055_VCO_CAL10]		= { .ghz5 = 0x0015, .ghz2 = 0x0015, NOUPLOAD, },
  [B2055_VCO_CAL11]		= { .ghz5 = 0x0007, .ghz2 = 0x0007, NOUPLOAD, },
  [B2055_VCO_CAL12]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_VCO_CAL13]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_VCO_CAL14]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_VCO_CAL15]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_VCO_CAL16]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_VCO_KVCO]		= { .ghz5 = 0x0008, .ghz2 = 0x0008, NOUPLOAD, },
  [B2055_VCO_CAPTAIL]		= { .ghz5 = 0x0008, .ghz2 = 0x0008, NOUPLOAD, },
  [B2055_VCO_IDACVCO]		= { .ghz5 = 0x0006, .ghz2 = 0x0006, NOUPLOAD, },
  [B2055_VCO_REG]		= { .ghz5 = 0x0084, .ghz2 = 0x0084, UPLOAD, },
  [B2055_PLL_RFVTH]		= { .ghz5 = 0x00C3, .ghz2 = 0x00C3, NOUPLOAD, },
  [B2055_LGBUF_CENBUF]		= { .ghz5 = 0x008F, .ghz2 = 0x008F, NOUPLOAD, },
  [B2055_LGEN_TUNE1]		= { .ghz5 = 0x00FF, .ghz2 = 0x00FF, NOUPLOAD, },
  [B2055_LGEN_TUNE2]		= { .ghz5 = 0x00FF, .ghz2 = 0x00FF, NOUPLOAD, },
  [B2055_LGEN_IDAC1]		= { .ghz5 = 0x0088, .ghz2 = 0x0088, NOUPLOAD, },
  [B2055_LGEN_IDAC2]		= { .ghz5 = 0x0088, .ghz2 = 0x0088, NOUPLOAD, },
  [B2055_LGEN_BIASC]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_LGEN_BIASIDAC]		= { .ghz5 = 0x00CC, .ghz2 = 0x00CC, NOUPLOAD, },
  [B2055_LGEN_RCAL]		= { .ghz5 = 0x0006, .ghz2 = 0x0006, NOUPLOAD, },
  [B2055_LGEN_DIV]		= { .ghz5 = 0x0080, .ghz2 = 0x0080, NOUPLOAD, },
  [B2055_LGEN_SPARE2]		= { .ghz5 = 0x0080, .ghz2 = 0x0080, NOUPLOAD, },
  [B2055_C1_LGBUF_ATUNE]	= { .ghz5 = 0x00F8, .ghz2 = 0x00F8, NOUPLOAD, },
  [B2055_C1_LGBUF_GTUNE]	= { .ghz5 = 0x0088, .ghz2 = 0x0088, NOUPLOAD, },
  [B2055_C1_LGBUF_DIV]		= { .ghz5 = 0x0088, .ghz2 = 0x0088, NOUPLOAD, },
  [B2055_C1_LGBUF_AIDAC]	= { .ghz5 = 0x0088, .ghz2 = 0x0008, UPLOAD, },
  [B2055_C1_LGBUF_GIDAC]	= { .ghz5 = 0x0088, .ghz2 = 0x0088, NOUPLOAD, },
  [B2055_C1_LGBUF_IDACFO]	= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_C1_LGBUF_SPARE]	= { .ghz5 = 0x0001, .ghz2 = 0x0001, UPLOAD, },
  [B2055_C1_RX_RFSPC1]		= { .ghz5 = 0x008A, .ghz2 = 0x008A, NOUPLOAD, },
  [B2055_C1_RX_RFR1]		= { .ghz5 = 0x0008, .ghz2 = 0x0008, NOUPLOAD, },
  [B2055_C1_RX_RFR2]		= { .ghz5 = 0x0083, .ghz2 = 0x0083, NOUPLOAD, },
  [B2055_C1_RX_RFRCAL]		= { .ghz5 = 0x0006, .ghz2 = 0x0006, NOUPLOAD, },
  [B2055_C1_RX_BB_BLCMP]	= { .ghz5 = 0x00A0, .ghz2 = 0x00A0, NOUPLOAD, },
  [B2055_C1_RX_BB_LPF]		= { .ghz5 = 0x000A, .ghz2 = 0x000A, NOUPLOAD, },
  [B2055_C1_RX_BB_MIDACHP]	= { .ghz5 = 0x0087, .ghz2 = 0x0087, UPLOAD, },
  [B2055_C1_RX_BB_VGA1IDAC]	= { .ghz5 = 0x002A, .ghz2 = 0x002A, NOUPLOAD, },
  [B2055_C1_RX_BB_VGA2IDAC]	= { .ghz5 = 0x002A, .ghz2 = 0x002A, NOUPLOAD, },
  [B2055_C1_RX_BB_VGA3IDAC]	= { .ghz5 = 0x002A, .ghz2 = 0x002A, NOUPLOAD, },
  [B2055_C1_RX_BB_BUFOCTL]	= { .ghz5 = 0x002A, .ghz2 = 0x002A, NOUPLOAD, },
  [B2055_C1_RX_BB_RCCALCTL]	= { .ghz5 = 0x0018, .ghz2 = 0x0018, NOUPLOAD, },
  [B2055_C1_RX_BB_RSSICTL1]	= { .ghz5 = 0x006A, .ghz2 = 0x006A, UPLOAD, },
  [B2055_C1_RX_BB_RSSICTL2]	= { .ghz5 = 0x00AB, .ghz2 = 0x00AB, UPLOAD, },
  [B2055_C1_RX_BB_RSSICTL3]	= { .ghz5 = 0x0013, .ghz2 = 0x0013, UPLOAD, },
  [B2055_C1_RX_BB_RSSICTL4]	= { .ghz5 = 0x00C1, .ghz2 = 0x00C1, UPLOAD, },
  [B2055_C1_RX_BB_RSSICTL5]	= { .ghz5 = 0x00AA, .ghz2 = 0x00AA, UPLOAD, },
  [B2055_C1_RX_BB_REG]		= { .ghz5 = 0x0087, .ghz2 = 0x0087, UPLOAD, },
  [B2055_C1_RX_BB_SPARE1]	= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_C1_RX_TXBBRCAL]	= { .ghz5 = 0x0006, .ghz2 = 0x0006, NOUPLOAD, },
  [B2055_C1_TX_RF_SPGA]		= { .ghz5 = 0x0007, .ghz2 = 0x0007, NOUPLOAD, },
  [B2055_C1_TX_RF_SPAD]		= { .ghz5 = 0x0007, .ghz2 = 0x0007, NOUPLOAD, },
  [B2055_C1_TX_RF_CNTPGA1]	= { .ghz5 = 0x0015, .ghz2 = 0x0015, NOUPLOAD, },
  [B2055_C1_TX_RF_CNTPAD1]	= { .ghz5 = 0x0055, .ghz2 = 0x0055, NOUPLOAD, },
  [B2055_C1_TX_RF_PGAIDAC]	= { .ghz5 = 0x0097, .ghz2 = 0x0097, UPLOAD, },
  [B2055_C1_TX_PGAPADTN]	= { .ghz5 = 0x0008, .ghz2 = 0x0008, NOUPLOAD, },
  [B2055_C1_TX_PADIDAC1]	= { .ghz5 = 0x0014, .ghz2 = 0x0014, UPLOAD, },
  [B2055_C1_TX_PADIDAC2]	= { .ghz5 = 0x0033, .ghz2 = 0x0033, NOUPLOAD, },
  [B2055_C1_TX_MXBGTRIM]	= { .ghz5 = 0x0088, .ghz2 = 0x0088, NOUPLOAD, },
  [B2055_C1_TX_RF_RCAL]		= { .ghz5 = 0x0006, .ghz2 = 0x0006, NOUPLOAD, },
  [B2055_C1_TX_RF_PADTSSI1]	= { .ghz5 = 0x0003, .ghz2 = 0x0003, UPLOAD, },
  [B2055_C1_TX_RF_PADTSSI2]	= { .ghz5 = 0x000A, .ghz2 = 0x000A, NOUPLOAD, },
  [B2055_C1_TX_RF_SPARE]	= { .ghz5 = 0x0003, .ghz2 = 0x0003, UPLOAD, },
  [B2055_C1_TX_RF_IQCAL1]	= { .ghz5 = 0x002A, .ghz2 = 0x002A, NOUPLOAD, },
  [B2055_C1_TX_RF_IQCAL2]	= { .ghz5 = 0x00A4, .ghz2 = 0x00A4, NOUPLOAD, },
  [B2055_C1_TXBB_RCCAL]		= { .ghz5 = 0x0018, .ghz2 = 0x0018, NOUPLOAD, },
  [B2055_C1_TXBB_LPF1]		= { .ghz5 = 0x0028, .ghz2 = 0x0028, NOUPLOAD, },
  [B2055_C1_TX_VOSCNCL]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_C1_TX_LPF_MXGMIDAC]	= { .ghz5 = 0x004A, .ghz2 = 0x004A, NOUPLOAD, },
  [B2055_C1_TX_BB_MXGM]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_C2_LGBUF_ATUNE]	= { .ghz5 = 0x00F8, .ghz2 = 0x00F8, NOUPLOAD, },
  [B2055_C2_LGBUF_GTUNE]	= { .ghz5 = 0x0088, .ghz2 = 0x0088, NOUPLOAD, },
  [B2055_C2_LGBUF_DIV]		= { .ghz5 = 0x0088, .ghz2 = 0x0088, NOUPLOAD, },
  [B2055_C2_LGBUF_AIDAC]	= { .ghz5 = 0x0088, .ghz2 = 0x0008, UPLOAD, },
  [B2055_C2_LGBUF_GIDAC]	= { .ghz5 = 0x0088, .ghz2 = 0x0088, NOUPLOAD, },
  [B2055_C2_LGBUF_IDACFO]	= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_C2_LGBUF_SPARE]	= { .ghz5 = 0x0001, .ghz2 = 0x0001, UPLOAD, },
  [B2055_C2_RX_RFSPC1]		= { .ghz5 = 0x008A, .ghz2 = 0x008A, NOUPLOAD, },
  [B2055_C2_RX_RFR1]		= { .ghz5 = 0x0008, .ghz2 = 0x0008, NOUPLOAD, },
  [B2055_C2_RX_RFR2]		= { .ghz5 = 0x0083, .ghz2 = 0x0083, NOUPLOAD, },
  [B2055_C2_RX_RFRCAL]		= { .ghz5 = 0x0006, .ghz2 = 0x0006, NOUPLOAD, },
  [B2055_C2_RX_BB_BLCMP]	= { .ghz5 = 0x00A0, .ghz2 = 0x00A0, NOUPLOAD, },
  [B2055_C2_RX_BB_LPF]		= { .ghz5 = 0x000A, .ghz2 = 0x000A, NOUPLOAD, },
  [B2055_C2_RX_BB_MIDACHP]	= { .ghz5 = 0x0087, .ghz2 = 0x0087, UPLOAD, },
  [B2055_C2_RX_BB_VGA1IDAC]	= { .ghz5 = 0x002A, .ghz2 = 0x002A, NOUPLOAD, },
  [B2055_C2_RX_BB_VGA2IDAC]	= { .ghz5 = 0x002A, .ghz2 = 0x002A, NOUPLOAD, },
  [B2055_C2_RX_BB_VGA3IDAC]	= { .ghz5 = 0x002A, .ghz2 = 0x002A, NOUPLOAD, },
  [B2055_C2_RX_BB_BUFOCTL]	= { .ghz5 = 0x002A, .ghz2 = 0x002A, NOUPLOAD, },
  [B2055_C2_RX_BB_RCCALCTL]	= { .ghz5 = 0x0018, .ghz2 = 0x0018, NOUPLOAD, },
  [B2055_C2_RX_BB_RSSICTL1]	= { .ghz5 = 0x006A, .ghz2 = 0x006A, UPLOAD, },
  [B2055_C2_RX_BB_RSSICTL2]	= { .ghz5 = 0x00AB, .ghz2 = 0x00AB, UPLOAD, },
  [B2055_C2_RX_BB_RSSICTL3]	= { .ghz5 = 0x0013, .ghz2 = 0x0013, UPLOAD, },
  [B2055_C2_RX_BB_RSSICTL4]	= { .ghz5 = 0x00C1, .ghz2 = 0x00C1, UPLOAD, },
  [B2055_C2_RX_BB_RSSICTL5]	= { .ghz5 = 0x00AA, .ghz2 = 0x00AA, UPLOAD, },
  [B2055_C2_RX_BB_REG]		= { .ghz5 = 0x0087, .ghz2 = 0x0087, UPLOAD, },
  [B2055_C2_RX_BB_SPARE1]	= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_C2_RX_TXBBRCAL]	= { .ghz5 = 0x0006, .ghz2 = 0x0006, NOUPLOAD, },
  [B2055_C2_TX_RF_SPGA]		= { .ghz5 = 0x0007, .ghz2 = 0x0007, NOUPLOAD, },
  [B2055_C2_TX_RF_SPAD]		= { .ghz5 = 0x0007, .ghz2 = 0x0007, NOUPLOAD, },
  [B2055_C2_TX_RF_CNTPGA1]	= { .ghz5 = 0x0015, .ghz2 = 0x0015, NOUPLOAD, },
  [B2055_C2_TX_RF_CNTPAD1]	= { .ghz5 = 0x0055, .ghz2 = 0x0055, NOUPLOAD, },
  [B2055_C2_TX_RF_PGAIDAC]	= { .ghz5 = 0x0097, .ghz2 = 0x0097, UPLOAD, },
  [B2055_C2_TX_PGAPADTN]	= { .ghz5 = 0x0008, .ghz2 = 0x0008, NOUPLOAD, },
  [B2055_C2_TX_PADIDAC1]	= { .ghz5 = 0x0014, .ghz2 = 0x0014, UPLOAD, },
  [B2055_C2_TX_PADIDAC2]	= { .ghz5 = 0x0033, .ghz2 = 0x0033, NOUPLOAD, },
  [B2055_C2_TX_MXBGTRIM]	= { .ghz5 = 0x0088, .ghz2 = 0x0088, NOUPLOAD, },
  [B2055_C2_TX_RF_RCAL]		= { .ghz5 = 0x0006, .ghz2 = 0x0006, NOUPLOAD, },
  [B2055_C2_TX_RF_PADTSSI1]	= { .ghz5 = 0x0003, .ghz2 = 0x0003, UPLOAD, },
  [B2055_C2_TX_RF_PADTSSI2]	= { .ghz5 = 0x000A, .ghz2 = 0x000A, NOUPLOAD, },
  [B2055_C2_TX_RF_SPARE]	= { .ghz5 = 0x0003, .ghz2 = 0x0003, UPLOAD, },
  [B2055_C2_TX_RF_IQCAL1]	= { .ghz5 = 0x002A, .ghz2 = 0x002A, NOUPLOAD, },
  [B2055_C2_TX_RF_IQCAL2]	= { .ghz5 = 0x00A4, .ghz2 = 0x00A4, NOUPLOAD, },
  [B2055_C2_TXBB_RCCAL]		= { .ghz5 = 0x0018, .ghz2 = 0x0018, NOUPLOAD, },
  [B2055_C2_TXBB_LPF1]		= { .ghz5 = 0x0028, .ghz2 = 0x0028, NOUPLOAD, },
  [B2055_C2_TX_VOSCNCL]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_C2_TX_LPF_MXGMIDAC]	= { .ghz5 = 0x004A, .ghz2 = 0x004A, NOUPLOAD, },
  [B2055_C2_TX_BB_MXGM]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_PRG_GCHP21]		= { .ghz5 = 0x0071, .ghz2 = 0x0071, NOUPLOAD, },
  [B2055_PRG_GCHP22]		= { .ghz5 = 0x0072, .ghz2 = 0x0072, NOUPLOAD, },
  [B2055_PRG_GCHP23]		= { .ghz5 = 0x0073, .ghz2 = 0x0073, NOUPLOAD, },
  [B2055_PRG_GCHP24]		= { .ghz5 = 0x0074, .ghz2 = 0x0074, NOUPLOAD, },
  [B2055_PRG_GCHP25]		= { .ghz5 = 0x0075, .ghz2 = 0x0075, NOUPLOAD, },
  [B2055_PRG_GCHP26]		= { .ghz5 = 0x0076, .ghz2 = 0x0076, NOUPLOAD, },
  [B2055_PRG_GCHP27]		= { .ghz5 = 0x0077, .ghz2 = 0x0077, NOUPLOAD, },
  [B2055_PRG_GCHP28]		= { .ghz5 = 0x0078, .ghz2 = 0x0078, NOUPLOAD, },
  [B2055_PRG_GCHP29]		= { .ghz5 = 0x0079, .ghz2 = 0x0079, NOUPLOAD, },
  [B2055_PRG_GCHP30]		= { .ghz5 = 0x007A, .ghz2 = 0x007A, NOUPLOAD, },
  [0xC7]			= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [0xC8]			= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [0xC9]			= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [0xCA]			= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [0xCB]			= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [0xCC]			= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_C1_LNA_GAINBST]	= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [0xCE]			= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [0xCF]			= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [0xD0]			= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [0xD1]			= { .ghz5 = 0x0018, .ghz2 = 0x0018, NOUPLOAD, },
  [B2055_C1_B0NB_RSSIVCM]	= { .ghz5 = 0x0088, .ghz2 = 0x0088, NOUPLOAD, },
  [0xD3]			= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [0xD4]			= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [0xD5]			= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_C1_GENSPARE2]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [0xD7]			= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [0xD8]			= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_C2_LNA_GAINBST]	= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [0xDA]			= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [0xDB]			= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [0xDC]			= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [0xDD]			= { .ghz5 = 0x0018, .ghz2 = 0x0018, NOUPLOAD, },
  [B2055_C2_B0NB_RSSIVCM]	= { .ghz5 = 0x0088, .ghz2 = 0x0088, NOUPLOAD, },
  [0xDF]			= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [0xE0]			= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [0xE1]			= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
  [B2055_C2_GENSPARE2]		= { .ghz5 = 0x0000, .ghz2 = 0x0000, NOUPLOAD, },
};


void b2055_upload_inittab(struct b43_wldev *dev,
			  bool ghz5, bool ignore_uploadflag)
{
	struct b2055_inittab_entry *e;
	unsigned int i;
	u16 value;

	for (i = 0; i < ARRAY_SIZE(b2055_inittab); i++) {
		e = &(b2055_inittab[i]);
		if (!(e->flags & B2055_INITTAB_ENTRY_OK))
			continue;
		if ((e->flags & B2055_INITTAB_UPLOAD) || ignore_uploadflag) {
			if (ghz5)
				value = e->ghz5;
			else
				value = e->ghz2;
			b43_radio_write16(dev, i, value);
		}
	}
}

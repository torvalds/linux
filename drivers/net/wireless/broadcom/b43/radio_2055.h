/* SPDX-License-Identifier: GPL-2.0 */
#ifndef B43_RADIO_2055_H_
#define B43_RADIO_2055_H_

#include <linux/types.h>

#include "tables_nphy.h"

#define B2055_GEN_SPARE			0x00 /* GEN spare */
#define B2055_SP_PINPD			0x02 /* SP PIN PD */
#define B2055_C1_SP_RSSI		0x03 /* SP RSSI Core 1 */
#define B2055_C1_SP_PDMISC		0x04 /* SP PD MISC Core 1 */
#define B2055_C2_SP_RSSI		0x05 /* SP RSSI Core 2 */
#define B2055_C2_SP_PDMISC		0x06 /* SP PD MISC Core 2 */
#define B2055_C1_SP_RXGC1		0x07 /* SP RX GC1 Core 1 */
#define B2055_C1_SP_RXGC2		0x08 /* SP RX GC2 Core 1 */
#define B2055_C2_SP_RXGC1		0x09 /* SP RX GC1 Core 2 */
#define B2055_C2_SP_RXGC2		0x0A /* SP RX GC2 Core 2 */
#define B2055_C1_SP_LPFBWSEL		0x0B /* SP LPF BW select Core 1 */
#define B2055_C2_SP_LPFBWSEL		0x0C /* SP LPF BW select Core 2 */
#define B2055_C1_SP_TXGC1		0x0D /* SP TX GC1 Core 1 */
#define B2055_C1_SP_TXGC2		0x0E /* SP TX GC2 Core 1 */
#define B2055_C2_SP_TXGC1		0x0F /* SP TX GC1 Core 2 */
#define B2055_C2_SP_TXGC2		0x10 /* SP TX GC2 Core 2 */
#define B2055_MASTER1			0x11 /* Master control 1 */
#define B2055_MASTER2			0x12 /* Master control 2 */
#define B2055_PD_LGEN			0x13 /* PD LGEN */
#define B2055_PD_PLLTS			0x14 /* PD PLL TS */
#define B2055_C1_PD_LGBUF		0x15 /* PD Core 1 LGBUF */
#define B2055_C1_PD_TX			0x16 /* PD Core 1 TX */
#define B2055_C1_PD_RXTX		0x17 /* PD Core 1 RXTX */
#define B2055_C1_PD_RSSIMISC		0x18 /* PD Core 1 RSSI MISC */
#define B2055_C2_PD_LGBUF		0x19 /* PD Core 2 LGBUF */
#define B2055_C2_PD_TX			0x1A /* PD Core 2 TX */
#define B2055_C2_PD_RXTX		0x1B /* PD Core 2 RXTX */
#define B2055_C2_PD_RSSIMISC		0x1C /* PD Core 2 RSSI MISC */
#define B2055_PWRDET_LGEN		0x1D /* PWRDET LGEN */
#define B2055_C1_PWRDET_LGBUF		0x1E /* PWRDET LGBUF Core 1 */
#define B2055_C1_PWRDET_RXTX		0x1F /* PWRDET RXTX Core 1 */
#define B2055_C2_PWRDET_LGBUF		0x20 /* PWRDET LGBUF Core 2 */
#define B2055_C2_PWRDET_RXTX		0x21 /* PWRDET RXTX Core 2 */
#define B2055_RRCCAL_CS			0x22 /* RRCCAL Control spare */
#define B2055_RRCCAL_NOPTSEL		0x23 /* RRCCAL N OPT SEL */
#define B2055_CAL_MISC			0x24 /* CAL MISC */
#define B2055_CAL_COUT			0x25 /* CAL Counter out */
#define B2055_CAL_COUT2			0x26 /* CAL Counter out 2 */
#define B2055_CAL_CVARCTL		0x27 /* CAL CVAR Control */
#define B2055_CAL_RVARCTL		0x28 /* CAL RVAR Control */
#define B2055_CAL_LPOCTL		0x29 /* CAL LPO Control */
#define B2055_CAL_TS			0x2A /* CAL TS */
#define B2055_CAL_RCCALRTS		0x2B /* CAL RCCAL READ TS */
#define B2055_CAL_RCALRTS		0x2C /* CAL RCAL READ TS */
#define B2055_PADDRV			0x2D /* PAD driver */
#define B2055_XOCTL1			0x2E /* XO Control 1 */
#define B2055_XOCTL2			0x2F /* XO Control 2 */
#define B2055_XOREGUL			0x30 /* XO Regulator */
#define B2055_XOMISC			0x31 /* XO misc */
#define B2055_PLL_LFC1			0x32 /* PLL LF C1 */
#define B2055_PLL_CALVTH		0x33 /* PLL CAL VTH */
#define B2055_PLL_LFC2			0x34 /* PLL LF C2 */
#define B2055_PLL_REF			0x35 /* PLL reference */
#define B2055_PLL_LFR1			0x36 /* PLL LF R1 */
#define B2055_PLL_PFDCP			0x37 /* PLL PFD CP */
#define B2055_PLL_IDAC_CPOPAMP		0x38 /* PLL IDAC CPOPAMP */
#define B2055_PLL_CPREG			0x39 /* PLL CP Regulator */
#define B2055_PLL_RCAL			0x3A /* PLL RCAL */
#define B2055_RF_PLLMOD0		0x3B /* RF PLL MOD0 */
#define B2055_RF_PLLMOD1		0x3C /* RF PLL MOD1 */
#define B2055_RF_MMDIDAC1		0x3D /* RF MMD IDAC 1 */
#define B2055_RF_MMDIDAC0		0x3E /* RF MMD IDAC 0 */
#define B2055_RF_MMDSP			0x3F /* RF MMD spare */
#define B2055_VCO_CAL1			0x40 /* VCO cal 1 */
#define B2055_VCO_CAL2			0x41 /* VCO cal 2 */
#define B2055_VCO_CAL3			0x42 /* VCO cal 3 */
#define B2055_VCO_CAL4			0x43 /* VCO cal 4 */
#define B2055_VCO_CAL5			0x44 /* VCO cal 5 */
#define B2055_VCO_CAL6			0x45 /* VCO cal 6 */
#define B2055_VCO_CAL7			0x46 /* VCO cal 7 */
#define B2055_VCO_CAL8			0x47 /* VCO cal 8 */
#define B2055_VCO_CAL9			0x48 /* VCO cal 9 */
#define B2055_VCO_CAL10			0x49 /* VCO cal 10 */
#define B2055_VCO_CAL11			0x4A /* VCO cal 11 */
#define B2055_VCO_CAL12			0x4B /* VCO cal 12 */
#define B2055_VCO_CAL13			0x4C /* VCO cal 13 */
#define B2055_VCO_CAL14			0x4D /* VCO cal 14 */
#define B2055_VCO_CAL15			0x4E /* VCO cal 15 */
#define B2055_VCO_CAL16			0x4F /* VCO cal 16 */
#define B2055_VCO_KVCO			0x50 /* VCO KVCO */
#define B2055_VCO_CAPTAIL		0x51 /* VCO CAP TAIL */
#define B2055_VCO_IDACVCO		0x52 /* VCO IDAC VCO */
#define B2055_VCO_REG			0x53 /* VCO Regulator */
#define B2055_PLL_RFVTH			0x54 /* PLL RF VTH */
#define B2055_LGBUF_CENBUF		0x55 /* LGBUF CEN BUF */
#define B2055_LGEN_TUNE1		0x56 /* LGEN tune 1 */
#define B2055_LGEN_TUNE2		0x57 /* LGEN tune 2 */
#define B2055_LGEN_IDAC1		0x58 /* LGEN IDAC 1 */
#define B2055_LGEN_IDAC2		0x59 /* LGEN IDAC 2 */
#define B2055_LGEN_BIASC		0x5A /* LGEN BIAS counter */
#define B2055_LGEN_BIASIDAC		0x5B /* LGEN BIAS IDAC */
#define B2055_LGEN_RCAL			0x5C /* LGEN RCAL */
#define B2055_LGEN_DIV			0x5D /* LGEN div */
#define B2055_LGEN_SPARE2		0x5E /* LGEN spare 2 */
#define B2055_C1_LGBUF_ATUNE		0x5F /* Core 1 LGBUF A tune */
#define B2055_C1_LGBUF_GTUNE		0x60 /* Core 1 LGBUF G tune */
#define B2055_C1_LGBUF_DIV		0x61 /* Core 1 LGBUF div */
#define B2055_C1_LGBUF_AIDAC		0x62 /* Core 1 LGBUF A IDAC */
#define B2055_C1_LGBUF_GIDAC		0x63 /* Core 1 LGBUF G IDAC */
#define B2055_C1_LGBUF_IDACFO		0x64 /* Core 1 LGBUF IDAC filter override */
#define B2055_C1_LGBUF_SPARE		0x65 /* Core 1 LGBUF spare */
#define B2055_C1_RX_RFSPC1		0x66 /* Core 1 RX RF SPC1 */
#define B2055_C1_RX_RFR1		0x67 /* Core 1 RX RF reg 1 */
#define B2055_C1_RX_RFR2		0x68 /* Core 1 RX RF reg 2 */
#define B2055_C1_RX_RFRCAL		0x69 /* Core 1 RX RF RCAL */
#define B2055_C1_RX_BB_BLCMP		0x6A /* Core 1 RX Baseband BUFI LPF CMP */
#define B2055_C1_RX_BB_LPF		0x6B /* Core 1 RX Baseband LPF */
#define B2055_C1_RX_BB_MIDACHP		0x6C /* Core 1 RX Baseband MIDAC High-pass */
#define B2055_C1_RX_BB_VGA1IDAC		0x6D /* Core 1 RX Baseband VGA1 IDAC */
#define B2055_C1_RX_BB_VGA2IDAC		0x6E /* Core 1 RX Baseband VGA2 IDAC */
#define B2055_C1_RX_BB_VGA3IDAC		0x6F /* Core 1 RX Baseband VGA3 IDAC */
#define B2055_C1_RX_BB_BUFOCTL		0x70 /* Core 1 RX Baseband BUFO Control */
#define B2055_C1_RX_BB_RCCALCTL		0x71 /* Core 1 RX Baseband RCCAL Control */
#define B2055_C1_RX_BB_RSSICTL1		0x72 /* Core 1 RX Baseband RSSI Control 1 */
#define B2055_C1_RX_BB_RSSICTL2		0x73 /* Core 1 RX Baseband RSSI Control 2 */
#define B2055_C1_RX_BB_RSSICTL3		0x74 /* Core 1 RX Baseband RSSI Control 3 */
#define B2055_C1_RX_BB_RSSICTL4		0x75 /* Core 1 RX Baseband RSSI Control 4 */
#define B2055_C1_RX_BB_RSSICTL5		0x76 /* Core 1 RX Baseband RSSI Control 5 */
#define B2055_C1_RX_BB_REG		0x77 /* Core 1 RX Baseband Regulator */
#define B2055_C1_RX_BB_SPARE1		0x78 /* Core 1 RX Baseband spare 1 */
#define B2055_C1_RX_TXBBRCAL		0x79 /* Core 1 RX TX BB RCAL */
#define B2055_C1_TX_RF_SPGA		0x7A /* Core 1 TX RF SGM PGA */
#define B2055_C1_TX_RF_SPAD		0x7B /* Core 1 TX RF SGM PAD */
#define B2055_C1_TX_RF_CNTPGA1		0x7C /* Core 1 TX RF counter PGA 1 */
#define B2055_C1_TX_RF_CNTPAD1		0x7D /* Core 1 TX RF counter PAD 1 */
#define B2055_C1_TX_RF_PGAIDAC		0x7E /* Core 1 TX RF PGA IDAC */
#define B2055_C1_TX_PGAPADTN		0x7F /* Core 1 TX PGA PAD TN */
#define B2055_C1_TX_PADIDAC1		0x80 /* Core 1 TX PAD IDAC 1 */
#define B2055_C1_TX_PADIDAC2		0x81 /* Core 1 TX PAD IDAC 2 */
#define B2055_C1_TX_MXBGTRIM		0x82 /* Core 1 TX MX B/G TRIM */
#define B2055_C1_TX_RF_RCAL		0x83 /* Core 1 TX RF RCAL */
#define B2055_C1_TX_RF_PADTSSI1		0x84 /* Core 1 TX RF PAD TSSI1 */
#define B2055_C1_TX_RF_PADTSSI2		0x85 /* Core 1 TX RF PAD TSSI2 */
#define B2055_C1_TX_RF_SPARE		0x86 /* Core 1 TX RF spare */
#define B2055_C1_TX_RF_IQCAL1		0x87 /* Core 1 TX RF I/Q CAL 1 */
#define B2055_C1_TX_RF_IQCAL2		0x88 /* Core 1 TX RF I/Q CAL 2 */
#define B2055_C1_TXBB_RCCAL		0x89 /* Core 1 TXBB RC CAL Control */
#define B2055_C1_TXBB_LPF1		0x8A /* Core 1 TXBB LPF 1 */
#define B2055_C1_TX_VOSCNCL		0x8B /* Core 1 TX VOS CNCL */
#define B2055_C1_TX_LPF_MXGMIDAC	0x8C /* Core 1 TX LPF MXGM IDAC */
#define B2055_C1_TX_BB_MXGM		0x8D /* Core 1 TX BB MXGM */
#define B2055_C2_LGBUF_ATUNE		0x8E /* Core 2 LGBUF A tune */
#define B2055_C2_LGBUF_GTUNE		0x8F /* Core 2 LGBUF G tune */
#define B2055_C2_LGBUF_DIV		0x90 /* Core 2 LGBUF div */
#define B2055_C2_LGBUF_AIDAC		0x91 /* Core 2 LGBUF A IDAC */
#define B2055_C2_LGBUF_GIDAC		0x92 /* Core 2 LGBUF G IDAC */
#define B2055_C2_LGBUF_IDACFO		0x93 /* Core 2 LGBUF IDAC filter override */
#define B2055_C2_LGBUF_SPARE		0x94 /* Core 2 LGBUF spare */
#define B2055_C2_RX_RFSPC1		0x95 /* Core 2 RX RF SPC1 */
#define B2055_C2_RX_RFR1		0x96 /* Core 2 RX RF reg 1 */
#define B2055_C2_RX_RFR2		0x97 /* Core 2 RX RF reg 2 */
#define B2055_C2_RX_RFRCAL		0x98 /* Core 2 RX RF RCAL */
#define B2055_C2_RX_BB_BLCMP		0x99 /* Core 2 RX Baseband BUFI LPF CMP */
#define B2055_C2_RX_BB_LPF		0x9A /* Core 2 RX Baseband LPF */
#define B2055_C2_RX_BB_MIDACHP		0x9B /* Core 2 RX Baseband MIDAC High-pass */
#define B2055_C2_RX_BB_VGA1IDAC		0x9C /* Core 2 RX Baseband VGA1 IDAC */
#define B2055_C2_RX_BB_VGA2IDAC		0x9D /* Core 2 RX Baseband VGA2 IDAC */
#define B2055_C2_RX_BB_VGA3IDAC		0x9E /* Core 2 RX Baseband VGA3 IDAC */
#define B2055_C2_RX_BB_BUFOCTL		0x9F /* Core 2 RX Baseband BUFO Control */
#define B2055_C2_RX_BB_RCCALCTL		0xA0 /* Core 2 RX Baseband RCCAL Control */
#define B2055_C2_RX_BB_RSSICTL1		0xA1 /* Core 2 RX Baseband RSSI Control 1 */
#define B2055_C2_RX_BB_RSSICTL2		0xA2 /* Core 2 RX Baseband RSSI Control 2 */
#define B2055_C2_RX_BB_RSSICTL3		0xA3 /* Core 2 RX Baseband RSSI Control 3 */
#define B2055_C2_RX_BB_RSSICTL4		0xA4 /* Core 2 RX Baseband RSSI Control 4 */
#define B2055_C2_RX_BB_RSSICTL5		0xA5 /* Core 2 RX Baseband RSSI Control 5 */
#define B2055_C2_RX_BB_REG		0xA6 /* Core 2 RX Baseband Regulator */
#define B2055_C2_RX_BB_SPARE1		0xA7 /* Core 2 RX Baseband spare 1 */
#define B2055_C2_RX_TXBBRCAL		0xA8 /* Core 2 RX TX BB RCAL */
#define B2055_C2_TX_RF_SPGA		0xA9 /* Core 2 TX RF SGM PGA */
#define B2055_C2_TX_RF_SPAD		0xAA /* Core 2 TX RF SGM PAD */
#define B2055_C2_TX_RF_CNTPGA1		0xAB /* Core 2 TX RF counter PGA 1 */
#define B2055_C2_TX_RF_CNTPAD1		0xAC /* Core 2 TX RF counter PAD 1 */
#define B2055_C2_TX_RF_PGAIDAC		0xAD /* Core 2 TX RF PGA IDAC */
#define B2055_C2_TX_PGAPADTN		0xAE /* Core 2 TX PGA PAD TN */
#define B2055_C2_TX_PADIDAC1		0xAF /* Core 2 TX PAD IDAC 1 */
#define B2055_C2_TX_PADIDAC2		0xB0 /* Core 2 TX PAD IDAC 2 */
#define B2055_C2_TX_MXBGTRIM		0xB1 /* Core 2 TX MX B/G TRIM */
#define B2055_C2_TX_RF_RCAL		0xB2 /* Core 2 TX RF RCAL */
#define B2055_C2_TX_RF_PADTSSI1		0xB3 /* Core 2 TX RF PAD TSSI1 */
#define B2055_C2_TX_RF_PADTSSI2		0xB4 /* Core 2 TX RF PAD TSSI2 */
#define B2055_C2_TX_RF_SPARE		0xB5 /* Core 2 TX RF spare */
#define B2055_C2_TX_RF_IQCAL1		0xB6 /* Core 2 TX RF I/Q CAL 1 */
#define B2055_C2_TX_RF_IQCAL2		0xB7 /* Core 2 TX RF I/Q CAL 2 */
#define B2055_C2_TXBB_RCCAL		0xB8 /* Core 2 TXBB RC CAL Control */
#define B2055_C2_TXBB_LPF1		0xB9 /* Core 2 TXBB LPF 1 */
#define B2055_C2_TX_VOSCNCL		0xBA /* Core 2 TX VOS CNCL */
#define B2055_C2_TX_LPF_MXGMIDAC	0xBB /* Core 2 TX LPF MXGM IDAC */
#define B2055_C2_TX_BB_MXGM		0xBC /* Core 2 TX BB MXGM */
#define B2055_PRG_GCHP21		0xBD /* PRG GC HPVGA23 21 */
#define B2055_PRG_GCHP22		0xBE /* PRG GC HPVGA23 22 */
#define B2055_PRG_GCHP23		0xBF /* PRG GC HPVGA23 23 */
#define B2055_PRG_GCHP24		0xC0 /* PRG GC HPVGA23 24 */
#define B2055_PRG_GCHP25		0xC1 /* PRG GC HPVGA23 25 */
#define B2055_PRG_GCHP26		0xC2 /* PRG GC HPVGA23 26 */
#define B2055_PRG_GCHP27		0xC3 /* PRG GC HPVGA23 27 */
#define B2055_PRG_GCHP28		0xC4 /* PRG GC HPVGA23 28 */
#define B2055_PRG_GCHP29		0xC5 /* PRG GC HPVGA23 29 */
#define B2055_PRG_GCHP30		0xC6 /* PRG GC HPVGA23 30 */
#define B2055_C1_LNA_GAINBST		0xCD /* Core 1 LNA GAINBST */
#define B2055_C1_B0NB_RSSIVCM		0xD2 /* Core 1 B0 narrow-band RSSI VCM */
#define B2055_C1_GENSPARE2		0xD6 /* Core 1 GEN spare 2 */
#define B2055_C2_LNA_GAINBST		0xD9 /* Core 2 LNA GAINBST */
#define B2055_C2_B0NB_RSSIVCM		0xDE /* Core 2 B0 narrow-band RSSI VCM */
#define B2055_C2_GENSPARE2		0xE2 /* Core 2 GEN spare 2 */

struct b43_nphy_channeltab_entry_rev2 {
	/* The channel number */
	u8 channel;
	/* The channel frequency in MHz */
	u16 freq;
	/* An unknown value */
	u16 unk2;
	/* Radio register values on channelswitch */
	u8 radio_pll_ref;
	u8 radio_rf_pllmod0;
	u8 radio_rf_pllmod1;
	u8 radio_vco_captail;
	u8 radio_vco_cal1;
	u8 radio_vco_cal2;
	u8 radio_pll_lfc1;
	u8 radio_pll_lfr1;
	u8 radio_pll_lfc2;
	u8 radio_lgbuf_cenbuf;
	u8 radio_lgen_tune1;
	u8 radio_lgen_tune2;
	u8 radio_c1_lgbuf_atune;
	u8 radio_c1_lgbuf_gtune;
	u8 radio_c1_rx_rfr1;
	u8 radio_c1_tx_pgapadtn;
	u8 radio_c1_tx_mxbgtrim;
	u8 radio_c2_lgbuf_atune;
	u8 radio_c2_lgbuf_gtune;
	u8 radio_c2_rx_rfr1;
	u8 radio_c2_tx_pgapadtn;
	u8 radio_c2_tx_mxbgtrim;
	/* PHY register values on channelswitch */
	struct b43_phy_n_sfo_cfg phy_regs;
};

/* Upload the default register value table.
 * If "ghz5" is true, we upload the 5Ghz table. Otherwise the 2.4Ghz
 * table is uploaded. If "ignore_uploadflag" is true, we upload any value
 * and ignore the "UPLOAD" flag. */
void b2055_upload_inittab(struct b43_wldev *dev,
			  bool ghz5, bool ignore_uploadflag);

/* Get the NPHY Channel Switch Table entry for a channel.
 * Returns NULL on failure to find an entry. */
const struct b43_nphy_channeltab_entry_rev2 *
b43_nphy_get_chantabent_rev2(struct b43_wldev *dev, u8 channel);

#endif /* B43_RADIO_2055_H_ */

/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright Fiona Klute <fiona.klute@gmx.de> */

#ifndef __RTW8703B_H__
#define __RTW8703B_H__

#include "rtw8723x.h"

extern const struct rtw_chip_info rtw8703b_hw_spec;

/* phy status parsing */
#define VGA_BITS GENMASK(4, 0)
#define LNA_L_BITS GENMASK(7, 5)
#define LNA_H_BIT BIT(7)
/* masks for assembling LNA index from high and low bits */
#define BIT_LNA_H_MASK BIT(3)
#define BIT_LNA_L_MASK GENMASK(2, 0)

struct phy_rx_agc_info {
#ifdef __LITTLE_ENDIAN
	u8 gain: 7;
	u8 trsw: 1;
#else
	u8 trsw: 1;
	u8 gain: 7;
#endif
} __packed;

/* This struct is called phy_status_rpt_8192cd in the vendor driver,
 * there might be potential to share it with drivers for other chips
 * of the same generation.
 */
struct phy_status_8703b {
	struct phy_rx_agc_info path_agc[2];
	u8 ch_corr[2];
	u8 cck_sig_qual_ofdm_pwdb_all;
	/* for CCK: bits 0:4: VGA index, bits 5:7: LNA index (low) */
	u8 cck_agc_rpt_ofdm_cfosho_a;
	/* for CCK: bit 7 is high bit of LNA index if long report type */
	u8 cck_rpt_b_ofdm_cfosho_b;
	u8 reserved_1;
	u8 noise_power_db_msb;
	s8 path_cfotail[2];
	u8 pcts_mask[2];
	s8 stream_rxevm[2];
	u8 path_rxsnr[2];
	u8 noise_power_db_lsb;
	u8 reserved_2[3];
	u8 stream_csi[2];
	u8 stream_target_csi[2];
	s8 sig_evm;
	u8 reserved_3;

#ifdef __LITTLE_ENDIAN
	u8 antsel_rx_keep_2: 1;
	u8 sgi_en: 1;
	u8 rxsc: 2;
	u8 idle_long: 1;
	u8 r_ant_train_en: 1;
	u8 ant_sel_b: 1;
	u8 ant_sel: 1;
#else /* __BIG_ENDIAN */
	u8 ant_sel: 1;
	u8 ant_sel_b: 1;
	u8 r_ant_train_en: 1;
	u8 idle_long: 1;
	u8 rxsc: 2;
	u8 sgi_en: 1;
	u8 antsel_rx_keep_2: 1;
#endif
} __packed;

/* Baseband registers */
#define REG_BB_PWR_SAV5_11N 0x0818
/* BIT(11) should be 1 for 8703B *and* 8723D, which means LNA uses 4
 * bit for CCK rates in report, not 3. Vendor driver logs a warning if
 * it's 0, but handles the case.
 *
 * Purpose of other parts of this register is unknown, 8723cs driver
 * code indicates some other chips use certain bits for antenna
 * diversity.
 */
#define REG_BB_AMP 0x0950
#define BIT_MASK_RX_LNA (BIT(11))

/* 0xaXX: 40MHz channel settings */
#define REG_CCK_TXSF2 0x0a24  /* CCK TX filter 2 */
#define REG_CCK_DBG 0x0a28  /* debug port */
#define REG_OFDM0_A_TX_AFE 0x0c84
#define REG_TXIQK_MATRIXB_LSB2_11N 0x0c9c
#define REG_OFDM0_TX_PSD_NOISE 0x0ce4  /* TX pseudo noise weighting */
#define REG_IQK_RDY 0x0e90  /* is != 0 when IQK is done */

/* RF registers */
#define RF_RCK1 0x1E

#define AGG_BURST_NUM 3
#define AGG_BURST_SIZE 0 /* 1K */
#define BIT_MASK_AGG_BURST_NUM (GENMASK(3, 2))
#define BIT_MASK_AGG_BURST_SIZE (GENMASK(5, 4))

#endif /* __RTW8703B_H__ */

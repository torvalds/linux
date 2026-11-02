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

/* Baseband registers */
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

/* RF registers */
#define RF_RCK1 0x1E

#endif /* __RTW8703B_H__ */

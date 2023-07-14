/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_FDI_REGS_H__
#define __INTEL_FDI_REGS_H__

#include "intel_display_reg_defs.h"

#define FDI_PLL_BIOS_0  _MMIO(0x46000)
#define  FDI_PLL_FB_CLOCK_MASK  0xff
#define FDI_PLL_BIOS_1  _MMIO(0x46004)
#define FDI_PLL_BIOS_2  _MMIO(0x46008)
#define DISPLAY_PORT_PLL_BIOS_0         _MMIO(0x4600c)
#define DISPLAY_PORT_PLL_BIOS_1         _MMIO(0x46010)
#define DISPLAY_PORT_PLL_BIOS_2         _MMIO(0x46014)

#define FDI_PLL_FREQ_CTL        _MMIO(0x46030)
#define  FDI_PLL_FREQ_CHANGE_REQUEST    (1 << 24)
#define  FDI_PLL_FREQ_LOCK_LIMIT_MASK   0xfff00
#define  FDI_PLL_FREQ_DISABLE_COUNT_LIMIT_MASK  0xff

#define _FDI_RXA_CHICKEN        0xc200c
#define _FDI_RXB_CHICKEN        0xc2010
#define  FDI_RX_PHASE_SYNC_POINTER_OVR	(1 << 1)
#define  FDI_RX_PHASE_SYNC_POINTER_EN	(1 << 0)
#define FDI_RX_CHICKEN(pipe)	_MMIO_PIPE(pipe, _FDI_RXA_CHICKEN, _FDI_RXB_CHICKEN)

/* CPU: FDI_TX */
#define _FDI_TXA_CTL            0x60100
#define _FDI_TXB_CTL            0x61100
#define FDI_TX_CTL(pipe)	_MMIO_PIPE(pipe, _FDI_TXA_CTL, _FDI_TXB_CTL)
#define  FDI_TX_DISABLE         (0 << 31)
#define  FDI_TX_ENABLE          (1 << 31)
#define  FDI_LINK_TRAIN_PATTERN_1       (0 << 28)
#define  FDI_LINK_TRAIN_PATTERN_2       (1 << 28)
#define  FDI_LINK_TRAIN_PATTERN_IDLE    (2 << 28)
#define  FDI_LINK_TRAIN_NONE            (3 << 28)
#define  FDI_LINK_TRAIN_VOLTAGE_0_4V    (0 << 25)
#define  FDI_LINK_TRAIN_VOLTAGE_0_6V    (1 << 25)
#define  FDI_LINK_TRAIN_VOLTAGE_0_8V    (2 << 25)
#define  FDI_LINK_TRAIN_VOLTAGE_1_2V    (3 << 25)
#define  FDI_LINK_TRAIN_PRE_EMPHASIS_NONE (0 << 22)
#define  FDI_LINK_TRAIN_PRE_EMPHASIS_1_5X (1 << 22)
#define  FDI_LINK_TRAIN_PRE_EMPHASIS_2X   (2 << 22)
#define  FDI_LINK_TRAIN_PRE_EMPHASIS_3X   (3 << 22)
/* ILK always use 400mV 0dB for voltage swing and pre-emphasis level.
   SNB has different settings. */
/* SNB A-stepping */
#define  FDI_LINK_TRAIN_400MV_0DB_SNB_A		(0x38 << 22)
#define  FDI_LINK_TRAIN_400MV_6DB_SNB_A		(0x02 << 22)
#define  FDI_LINK_TRAIN_600MV_3_5DB_SNB_A	(0x01 << 22)
#define  FDI_LINK_TRAIN_800MV_0DB_SNB_A		(0x0 << 22)
/* SNB B-stepping */
#define  FDI_LINK_TRAIN_400MV_0DB_SNB_B		(0x0 << 22)
#define  FDI_LINK_TRAIN_400MV_6DB_SNB_B		(0x3a << 22)
#define  FDI_LINK_TRAIN_600MV_3_5DB_SNB_B	(0x39 << 22)
#define  FDI_LINK_TRAIN_800MV_0DB_SNB_B		(0x38 << 22)
#define  FDI_LINK_TRAIN_VOL_EMP_MASK		(0x3f << 22)
#define  FDI_DP_PORT_WIDTH_SHIFT		19
#define  FDI_DP_PORT_WIDTH_MASK			(7 << FDI_DP_PORT_WIDTH_SHIFT)
#define  FDI_DP_PORT_WIDTH(width)           (((width) - 1) << FDI_DP_PORT_WIDTH_SHIFT)
#define  FDI_TX_ENHANCE_FRAME_ENABLE    (1 << 18)
/* Ironlake: hardwired to 1 */
#define  FDI_TX_PLL_ENABLE              (1 << 14)

/* Ivybridge has different bits for lolz */
#define  FDI_LINK_TRAIN_PATTERN_1_IVB       (0 << 8)
#define  FDI_LINK_TRAIN_PATTERN_2_IVB       (1 << 8)
#define  FDI_LINK_TRAIN_PATTERN_IDLE_IVB    (2 << 8)
#define  FDI_LINK_TRAIN_NONE_IVB            (3 << 8)

/* both Tx and Rx */
#define  FDI_COMPOSITE_SYNC		(1 << 11)
#define  FDI_LINK_TRAIN_AUTO		(1 << 10)
#define  FDI_SCRAMBLING_ENABLE          (0 << 7)
#define  FDI_SCRAMBLING_DISABLE         (1 << 7)

/* FDI_RX, FDI_X is hard-wired to Transcoder_X */
#define _FDI_RXA_CTL             0xf000c
#define _FDI_RXB_CTL             0xf100c
#define FDI_RX_CTL(pipe)	_MMIO_PIPE(pipe, _FDI_RXA_CTL, _FDI_RXB_CTL)
#define  FDI_RX_ENABLE          (1 << 31)
/* train, dp width same as FDI_TX */
#define  FDI_FS_ERRC_ENABLE		(1 << 27)
#define  FDI_FE_ERRC_ENABLE		(1 << 26)
#define  FDI_RX_POLARITY_REVERSED_LPT	(1 << 16)
#define  FDI_8BPC                       (0 << 16)
#define  FDI_10BPC                      (1 << 16)
#define  FDI_6BPC                       (2 << 16)
#define  FDI_12BPC                      (3 << 16)
#define  FDI_RX_LINK_REVERSAL_OVERRIDE  (1 << 15)
#define  FDI_DMI_LINK_REVERSE_MASK      (1 << 14)
#define  FDI_RX_PLL_ENABLE              (1 << 13)
#define  FDI_FS_ERR_CORRECT_ENABLE      (1 << 11)
#define  FDI_FE_ERR_CORRECT_ENABLE      (1 << 10)
#define  FDI_FS_ERR_REPORT_ENABLE       (1 << 9)
#define  FDI_FE_ERR_REPORT_ENABLE       (1 << 8)
#define  FDI_RX_ENHANCE_FRAME_ENABLE    (1 << 6)
#define  FDI_PCDCLK	                (1 << 4)
/* CPT */
#define  FDI_AUTO_TRAINING			(1 << 10)
#define  FDI_LINK_TRAIN_PATTERN_1_CPT		(0 << 8)
#define  FDI_LINK_TRAIN_PATTERN_2_CPT		(1 << 8)
#define  FDI_LINK_TRAIN_PATTERN_IDLE_CPT	(2 << 8)
#define  FDI_LINK_TRAIN_NORMAL_CPT		(3 << 8)
#define  FDI_LINK_TRAIN_PATTERN_MASK_CPT	(3 << 8)

#define _FDI_RXA_MISC			0xf0010
#define _FDI_RXB_MISC			0xf1010
#define  FDI_RX_PWRDN_LANE1_MASK	(3 << 26)
#define  FDI_RX_PWRDN_LANE1_VAL(x)	((x) << 26)
#define  FDI_RX_PWRDN_LANE0_MASK	(3 << 24)
#define  FDI_RX_PWRDN_LANE0_VAL(x)	((x) << 24)
#define  FDI_RX_TP1_TO_TP2_48		(2 << 20)
#define  FDI_RX_TP1_TO_TP2_64		(3 << 20)
#define  FDI_RX_FDI_DELAY_90		(0x90 << 0)
#define FDI_RX_MISC(pipe)	_MMIO_PIPE(pipe, _FDI_RXA_MISC, _FDI_RXB_MISC)

#define _FDI_RXA_TUSIZE1        0xf0030
#define _FDI_RXA_TUSIZE2        0xf0038
#define _FDI_RXB_TUSIZE1        0xf1030
#define _FDI_RXB_TUSIZE2        0xf1038
#define FDI_RX_TUSIZE1(pipe)	_MMIO_PIPE(pipe, _FDI_RXA_TUSIZE1, _FDI_RXB_TUSIZE1)
#define FDI_RX_TUSIZE2(pipe)	_MMIO_PIPE(pipe, _FDI_RXA_TUSIZE2, _FDI_RXB_TUSIZE2)

/* FDI_RX interrupt register format */
#define FDI_RX_INTER_LANE_ALIGN         (1 << 10)
#define FDI_RX_SYMBOL_LOCK              (1 << 9) /* train 2 */
#define FDI_RX_BIT_LOCK                 (1 << 8) /* train 1 */
#define FDI_RX_TRAIN_PATTERN_2_FAIL     (1 << 7)
#define FDI_RX_FS_CODE_ERR              (1 << 6)
#define FDI_RX_FE_CODE_ERR              (1 << 5)
#define FDI_RX_SYMBOL_ERR_RATE_ABOVE    (1 << 4)
#define FDI_RX_HDCP_LINK_FAIL           (1 << 3)
#define FDI_RX_PIXEL_FIFO_OVERFLOW      (1 << 2)
#define FDI_RX_CROSS_CLOCK_OVERFLOW     (1 << 1)
#define FDI_RX_SYMBOL_QUEUE_OVERFLOW    (1 << 0)

#define _FDI_RXA_IIR            0xf0014
#define _FDI_RXA_IMR            0xf0018
#define _FDI_RXB_IIR            0xf1014
#define _FDI_RXB_IMR            0xf1018
#define FDI_RX_IIR(pipe)	_MMIO_PIPE(pipe, _FDI_RXA_IIR, _FDI_RXB_IIR)
#define FDI_RX_IMR(pipe)	_MMIO_PIPE(pipe, _FDI_RXA_IMR, _FDI_RXB_IMR)

#define FDI_PLL_CTL_1           _MMIO(0xfe000)
#define FDI_PLL_CTL_2           _MMIO(0xfe004)

#endif /* __INTEL_FDI_REGS_H__ */

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This Synopsys software and associated documentation (hereinafter the
 * "Software") is an unsupported proprietary work of Synopsys, Inc. unless
 * otherwise expressly agreed to in writing between Synopsys and you. The
 * Software IS NOT an item of Licensed Software or a Licensed Product under
 * any End User Software License Agreement or Agreement for Licensed Products
 * with Synopsys or any supplement thereto. Synopsys is a registered trademark
 * of Synopsys, Inc. Other names included in the SOFTWARE may be the
 * trademarks of their respective owners.
 *
 * The contents of this file are dual-licensed; you may select either version
 * 2 of the GNU General Public License ("GPL") or the BSD-3-Clause license
 * ("BSD-3-Clause"). The GPL is included in the COPYING file accompanying the
 * SOFTWARE. The BSD License is copied below.
 *
 * BSD-3-Clause License:
 * Copyright (c) 2012-2016 Synopsys, Inc. and/or its affiliates.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer, without
 *    modification.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. The names of the above-listed copyright holders may not be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef NISTTRNG_HW_H
#define NISTTRNG_HW_H

/* HW related Parameters */
#define NIST_TRNG_RAND_BLK_SIZE_BITS   128
#define CHX_URUN_BLANK_AFTER_RESET  0x3

/* registers */
#define NIST_TRNG_REG_CTRL               0x00
#define NIST_TRNG_REG_MODE               0x01
#define NIST_TRNG_REG_SMODE              0x02
#define NIST_TRNG_REG_STAT               0x03
#define NIST_TRNG_REG_IE                 0x04
#define NIST_TRNG_REG_ISTAT              0x05
#define NIST_TRNG_REG_ALARM              0x06
#define NIST_TRNG_REG_COREKIT_REL        0x07
#define NIST_TRNG_REG_FEATURES           0x08
#define NIST_TRNG_REG_RAND0              0x09
#define NIST_TRNG_REG_RAND1              0x0A
#define NIST_TRNG_REG_RAND2              0x0B
#define NIST_TRNG_REG_RAND3              0x0C
#define NIST_TRNG_REG_NPA_DATA0          0x0D
#define NIST_TRNG_REG_NPA_DATA1          0x0E
#define NIST_TRNG_REG_NPA_DATA2          0x0F
#define NIST_TRNG_REG_NPA_DATA3          0x10
#define NIST_TRNG_REG_NPA_DATA4          0x11
#define NIST_TRNG_REG_NPA_DATA5          0x12
#define NIST_TRNG_REG_NPA_DATA6          0x13
#define NIST_TRNG_REG_NPA_DATA7          0x14
#define NIST_TRNG_REG_NPA_DATA8          0x15
#define NIST_TRNG_REG_NPA_DATA9          0x16
#define NIST_TRNG_REG_NPA_DATA10         0x17
#define NIST_TRNG_REG_NPA_DATA11         0x18
#define NIST_TRNG_REG_NPA_DATA12         0x19
#define NIST_TRNG_REG_NPA_DATA13         0x1A
#define NIST_TRNG_REG_NPA_DATA14         0x1B
#define NIST_TRNG_REG_NPA_DATA15         0x1C
#define NIST_TRNG_REG_SEED0              0x1D
#define NIST_TRNG_REG_SEED1              0x1E
#define NIST_TRNG_REG_SEED2              0x1F
#define NIST_TRNG_REG_SEED3              0x20
#define NIST_TRNG_REG_SEED4              0x21
#define NIST_TRNG_REG_SEED5              0x22
#define NIST_TRNG_REG_SEED6              0x23
#define NIST_TRNG_REG_SEED7              0x24
#define NIST_TRNG_REG_SEED8              0x25
#define NIST_TRNG_REG_SEED9              0x26
#define NIST_TRNG_REG_SEED10             0x27
#define NIST_TRNG_REG_SEED11             0x28
#define NIST_TRNG_REG_TIME_TO_SEED       0x34
#define NIST_TRNG_REG_IA_RDATA           0x38
#define NIST_TRNG_REG_IA_WDATA           0x39
#define NIST_TRNG_REG_IA_ADDR            0x3A
#define NIST_TRNG_REG_IA_CMD             0x3B
#define NIST_TRNG_REG_BUILD_CFG0         0x3C
#define NIST_TRNG_REG_BUILD_CFG1         0x3D

/* nist edu registers */
#define NIST_TRNG_EDU_RNC_CTRL        0x100
#define NIST_TRNG_EDU_FLUSH_CTRL      0x101
#define NIST_TRNG_EDU_RESEED_CNTR     0x102
#define NIST_TRNG_EDU_RBC_CTRL        0x104
#define NIST_TRNG_EDU_STAT            0x106
#define NIST_TRNG_EDU_IE              0x108
#define NIST_TRNG_EDU_ISTAT           0x109
#define NIST_TRNG_EDU_BUILD_CFG0      0x12C
#define NIST_TRNG_EDU_VCTRL           0x138
#define NIST_TRNG_EDU_VSTAT           0x139
#define NIST_TRNG_EDU_VIE             0x13A
#define NIST_TRNG_EDU_VISTAT          0x13B
#define NIST_TRNG_EDU_VRAND_0         0x13C
#define NIST_TRNG_EDU_VRAND_1         0x13D
#define NIST_TRNG_EDU_VRAND_2         0x13E
#define NIST_TRNG_EDU_VRAND_3         0x13F

/* edu vtrng registers */
#define NIST_TRNG_EDU_VTRNG_VCTRL0         0x180
#define NIST_TRNG_EDU_VTRNG_VSTAT0         0x181
#define NIST_TRNG_EDU_VTRNG_VIE0           0x182
#define NIST_TRNG_EDU_VTRNG_VISTAT0        0x183
#define NIST_TRNG_EDU_VTRNG_VRAND0_0       0x184
#define NIST_TRNG_EDU_VTRNG_VRAND0_1       0x185
#define NIST_TRNG_EDU_VTRNG_VRAND0_2       0x186
#define NIST_TRNG_EDU_VTRNG_VRAND0_3       0x187
#define NIST_TRNG_EDU_VTRNG_VCTRL1         0x188
#define NIST_TRNG_EDU_VTRNG_VSTAT1         0x189
#define NIST_TRNG_EDU_VTRNG_VIE1           0x18A
#define NIST_TRNG_EDU_VTRNG_VISTAT1        0x18B
#define NIST_TRNG_EDU_VTRNG_VRAND1_0       0x18C
#define NIST_TRNG_EDU_VTRNG_VRAND1_1       0x18D
#define NIST_TRNG_EDU_VTRNG_VRAND1_2       0x18E
#define NIST_TRNG_EDU_VTRNG_VRAND1_3       0x18F
#define NIST_TRNG_EDU_VTRNG_VCTRL2         0x190
#define NIST_TRNG_EDU_VTRNG_VSTAT2         0x191
#define NIST_TRNG_EDU_VTRNG_VIE2           0x192
#define NIST_TRNG_EDU_VTRNG_VISTAT2        0x193
#define NIST_TRNG_EDU_VTRNG_VRAND2_0       0x194
#define NIST_TRNG_EDU_VTRNG_VRAND2_1       0x195
#define NIST_TRNG_EDU_VTRNG_VRAND2_2       0x196
#define NIST_TRNG_EDU_VTRNG_VRAND2_3       0x197
#define NIST_TRNG_EDU_VTRNG_VCTRL3         0x198
#define NIST_TRNG_EDU_VTRNG_VSTAT3         0x199
#define NIST_TRNG_EDU_VTRNG_VIE3           0x19A
#define NIST_TRNG_EDU_VTRNG_VISTAT3        0x19B
#define NIST_TRNG_EDU_VTRNG_VRAND3_0       0x19C
#define NIST_TRNG_EDU_VTRNG_VRAND3_1       0x19D
#define NIST_TRNG_EDU_VTRNG_VRAND3_2       0x19E
#define NIST_TRNG_EDU_VTRNG_VRAND3_3       0x19F
#define NIST_TRNG_EDU_VTRNG_VCTRL4         0x1A0
#define NIST_TRNG_EDU_VTRNG_VSTAT4         0x1A1
#define NIST_TRNG_EDU_VTRNG_VIE4           0x1A2
#define NIST_TRNG_EDU_VTRNG_VISTAT4        0x1A3
#define NIST_TRNG_EDU_VTRNG_VRAND4_0       0x1A4
#define NIST_TRNG_EDU_VTRNG_VRAND4_1       0x1A5
#define NIST_TRNG_EDU_VTRNG_VRAND4_2       0x1A6
#define NIST_TRNG_EDU_VTRNG_VRAND4_3       0x1A7
#define NIST_TRNG_EDU_VTRNG_VCTRL5         0x1A8
#define NIST_TRNG_EDU_VTRNG_VSTAT5         0x1A9
#define NIST_TRNG_EDU_VTRNG_VIE5           0x1AA
#define NIST_TRNG_EDU_VTRNG_VISTAT5        0x1AB
#define NIST_TRNG_EDU_VTRNG_VRAND5_0       0x1AC
#define NIST_TRNG_EDU_VTRNG_VRAND5_1       0x1AD
#define NIST_TRNG_EDU_VTRNG_VRAND5_2       0x1AE
#define NIST_TRNG_EDU_VTRNG_VRAND5_3       0x1AF
#define NIST_TRNG_EDU_VTRNG_VCTRL6         0x1B0
#define NIST_TRNG_EDU_VTRNG_VSTAT6         0x1B1
#define NIST_TRNG_EDU_VTRNG_VIE6           0x1B2
#define NIST_TRNG_EDU_VTRNG_VISTAT6        0x1B3
#define NIST_TRNG_EDU_VTRNG_VRAND6_0       0x1B4
#define NIST_TRNG_EDU_VTRNG_VRAND6_1       0x1B5
#define NIST_TRNG_EDU_VTRNG_VRAND6_2       0x1B6
#define NIST_TRNG_EDU_VTRNG_VRAND6_3       0x1B7
#define NIST_TRNG_EDU_VTRNG_VCTRL7         0x1B8
#define NIST_TRNG_EDU_VTRNG_VSTAT7         0x1B9
#define NIST_TRNG_EDU_VTRNG_VIE7           0x1BA
#define NIST_TRNG_EDU_VTRNG_VISTAT7        0x1BB
#define NIST_TRNG_EDU_VTRNG_VRAND7_0       0x1BC
#define NIST_TRNG_EDU_VTRNG_VRAND7_1       0x1BD
#define NIST_TRNG_EDU_VTRNG_VRAND7_2       0x1BE
#define NIST_TRNG_EDU_VTRNG_VRAND7_3       0x1BF

#define NIST_TRNG_EDU_VTRNG_VCTRL_CMD_NOP        0x0
#define NIST_TRNG_EDU_VTRNG_VCTRL_CMD_GET_RANDOM 0x1
#define NIST_TRNG_EDU_VTRNG_VCTRL_CMD_INIT       0x2

#define NIST_TRNG_EDU_VTRNG_VCTRL_CMD_MASK       0x3Ul
#define NIST_TRNG_EDU_VTRNG_VCTRL_CMD_SET(y, x)  (((y) & ~(NIST_TRNG_EDU_VTRNG_VCTRL_CMD_MASK)) | ((x)))

/* CTRL */
#define NIST_TRNG_REG_CTRL_CMD_NOP             0
#define NIST_TRNG_REG_CTRL_CMD_GEN_NOISE       1
#define NIST_TRNG_REG_CTRL_CMD_GEN_NONCE       2
#define NIST_TRNG_REG_CTRL_CMD_CREATE_STATE    3
#define NIST_TRNG_REG_CTRL_CMD_RENEW_STATE     4
#define NIST_TRNG_REG_CTRL_CMD_REFRESH_ADDIN   5
#define NIST_TRNG_REG_CTRL_CMD_GEN_RANDOM      6
#define NIST_TRNG_REG_CTRL_CMD_ADVANCE_STATE   7
#define NIST_TRNG_REG_CTRL_CMD_KAT             8
#define NIST_TRNG_REG_CTRL_CMD_ZEROIZE        15

/* EDU CTRL */
#define NIST_TRNG_EDU_RNC_CTRL_CMD_RNC_DISABLE_TO_HOLD   0
#define NIST_TRNG_EDU_RNC_CTRL_CMD_RNC_ENABLE            1
#define NIST_TRNG_EDU_RNC_CTRL_CMD_RNC_DISABLE_TO_IDLE   2
#define NIST_TRNG_EDU_RNC_CTRL_CMD_RNC_FINISH_TO_IDLE    3

#define NIST_TRNG_EDU_RNC_CTRL_CMD_MASK                  0x3Ul
#define NIST_TRNG_EDU_RNC_CTRL_CMD_SET(y, x)             (((y) & ~(NIST_TRNG_EDU_RNC_CTRL_CMD_MASK)) | ((x)))

/* EDU_FLUSH_CTRL */
#define _NIST_TRNG_EDU_FLUSH_CTRL_CH2_RBC    3
#define _NIST_TRNG_EDU_FLUSH_CTRL_CH1_RBC    2
#define _NIST_TRNG_EDU_FLUSH_CTRL_CH0_RBC    1
#define _NIST_TRNG_EDU_FLUSH_CTRL_FIFO       0

#define NIST_TRNG_EDU_FLUSH_CTRL_CH2_RBC    BIT(_NIST_TRNG_EDU_FLUSH_CTRL_CH2_RBC)
#define NIST_TRNG_EDU_FLUSH_CTRL_CH1_RBC    BIT(_NIST_TRNG_EDU_FLUSH_CTRL_CH1_RBC)
#define NIST_TRNG_EDU_FLUSH_CTRL_CH0_RBC    BIT(_NIST_TRNG_EDU_FLUSH_CTRL_CH0_RBC)
#define NIST_TRNG_EDU_FLUSH_CTRL_FIFO       BIT(_NIST_TRNG_EDU_FLUSH_CTRL_FIFO)

/*EDU_RBC_CTRL*/
#define _NIST_TRNG_EDU_RBC_CTRL_CH2_URUN_BLANK    28
#define _NIST_TRNG_EDU_RBC_CTRL_CH1_URUN_BLANK    26
#define _NIST_TRNG_EDU_RBC_CTRL_CH0_URUN_BLANK    24
#define _NIST_TRNG_EDU_RBC_CTRL_CH2_RATE          16
#define _NIST_TRNG_EDU_RBC_CTRL_CH1_RATE          8
#define _NIST_TRNG_EDU_RBC_CTRL_CH0_RATE          0

#define _NIST_TRNG_EDU_RBC_CTRL_CH_RATE_MASK           0xFUL
#define _NIST_TRNG_EDU_RBC_CTRL_CH_URUN_BLANK_MASK     0x3UL

#define NISTTRNG_EDU_RBC_CTRL_SET_CH_RATE(z, y, x)       (((y) & ~(_NIST_TRNG_EDU_RBC_CTRL_CH_RATE_MASK << (x))) | ((z) << (x)))
#define NISTTRNG_EDU_RBC_CTRL_SET_CH_URUN_BLANK(z, y, x) (((y) & ~(_NIST_TRNG_EDU_RBC_CTRL_CH_URUN_BLANK_MASK << (x))) | ((z) << (x)))

#define NISTTRNG_EDU_RBC_CTRL_GET_CH_RATE(y, x)         ((_NIST_TRNG_EDU_RBC_CTRL_CH_RATE_MASK) & ((y) >> (x)))
#define NISTTRNG_EDU_RBC_CTRL_GET_CH_URUN_BLANK(y, x)   ((_NIST_TRNG_EDU_RBC_CTRL_CH_URUN_BLANK_MASK) & ((y) >> (x)))

#define NISTTRNG_EDU_RBC_CTRL_GET_CH_RATE_AFTER_RESET             0x0
#define NISTTRNG_EDU_RBC_CTRL_SET_CH_URUN_BLANK_AFTER_RESET       0x3

/* MODE */
#define _NIST_TRNG_REG_MODE_KAT_SEL       7
#define _NIST_TRNG_REG_MODE_KAT_VEC       5
#define _NIST_TRNG_REG_MODE_ADDIN_PRESENT 4
#define _NIST_TRNG_REG_MODE_PRED_RESIST   3
#define _NIST_TRNG_REG_MODE_SEC_ALG       0

#define NIST_TRNG_REG_MODE_ADDIN_PRESENT BIT(_NIST_TRNG_REG_MODE_ADDIN_PRESENT)
#define NIST_TRNG_REG_MODE_PRED_RESIST   BIT(_NIST_TRNG_REG_MODE_PRED_RESIST)
#define NIST_TRNG_REG_MODE_SEC_ALG       BIT(_NIST_TRNG_REG_MODE_SEC_ALG)

/* SMODE */
#define _NIST_TRNG_REG_SMODE_NOISE_COLLECT             31
#define _NIST_TRNG_REG_SMODE_INDIV_HT_DISABLE          16
#define _NIST_TRNG_REG_SMODE_MAX_REJECTS               2
#define _NIST_TRNG_REG_SMODE_MISSION_MODE              1
#define _NIST_TRNG_REG_SMODE_SECURE_EN                 _NIST_TRNG_REG_SMODE_MISSION_MODE
#define _NIST_TRNG_REG_SMODE_NONCE                     0

#define NIST_TRNG_REG_SMODE_MAX_REJECTS(x) ((x) << _NIST_TRNG_REG_SMODE_MAX_REJECTS)
#define NIST_TRNG_REG_SMODE_SECURE_EN(x)   ((x) << _NIST_TRNG_REG_SMODE_SECURE_EN)
#define NIST_TRNG_REG_SMODE_NONCE          BIT(_NIST_TRNG_REG_SMODE_NONCE)

/* STAT */
#define _NIST_TRNG_REG_STAT_BUSY         31
#define _NIST_TRNG_REG_STAT_STARTUP_TEST_IN_PROG    10
#define _NIST_TRNG_REG_STAT_STARTUP_TEST_STUCK      9
#define _NIST_TRNG_REG_STAT_DRBG_STATE    7
#define _NIST_TRNG_REG_STAT_SECURE        6
#define _NIST_TRNG_REG_STAT_NONCE_MODE    5
#define _NIST_TRNG_REG_STAT_SEC_ALG       4
#define _NIST_TRNG_REG_STAT_LAST_CMD      0

#define NIST_TRNG_REG_STAT_BUSY          BIT(_NIST_TRNG_REG_STAT_BUSY)
//#define NIST_TRNG_REG_STAT_DRBG_STATE    (1UL<<_NIST_TRNG_REG_STAT_DRBG_STATE)
//#define NIST_TRNG_REG_STAT_SECURE        (1UL << _NIST_TRNG_REG_STAT_SECURE)
//#define NIST_TRNG_REG_STAT_NONCE_MODE    (1UL << _NIST_TRNG_REG_STAT_NONCE_MODE)
//#define NIST_TRNG_REG_STAT_SEC_ALG       (1UL << _NIST_TRNG_REG_STAT_SEC_ALG)
//#define NIST_TRNG_REG_STAT_LAST_CMD(x)   (((x) >> _NIST_TRNG_REG_STAT_LAST_CMD)&0xF)

/*EDU_STAT*/

#define NIST_TRNG_EDU_STAT_FIFO_LEVEL(x)  (((x) >> 24) & 255)
#define NIST_TRNG_EDU_STAT_TTT_INDEX(x)   (((x) >> 16) & 255)
#define NIST_TRNG_EDU_STAT_RNC_BUSY(x)    (((x) >> 3) & 7)
#define NIST_TRNG_EDU_STAT_RNC_ENABLED(x) (((x) >> 2) & 1)
#define NIST_TRNG_EDU_STAT_FIFO_EMPTY(x)  (((x) >> 1) & 1)
#define NIST_TRNG_EDU_STAT_FIFO_FULL(x)   ((x) & 1)

/* IE */
#define _NIST_TRNG_REG_IE_GLBL         31
#define _NIST_TRNG_REG_IE_DONE          4
#define _NIST_TRNG_REG_IE_ALARMS        3
#define _NIST_TRNG_REG_IE_NOISE_RDY     2
#define _NIST_TRNG_REG_IE_KAT_COMPLETE  1
#define _NIST_TRNG_REG_IE_ZEROIZE       0

#define NIST_TRNG_REG_IE_GLBL         BIT(_NIST_TRNG_REG_IE_GLBL)
#define NIST_TRNG_REG_IE_DONE         BIT(_NIST_TRNG_REG_IE_DONE)
#define NIST_TRNG_REG_IE_ALARMS       BIT(_NIST_TRNG_REG_IE_ALARMS)
#define NIST_TRNG_REG_IE_NOISE_RDY    BIT(_NIST_TRNG_REG_IE_NOISE_RDY)
#define NIST_TRNG_REG_IE_KAT_COMPLETE BIT(_NIST_TRNG_REG_IE_KAT_COMPLETE)
#define NIST_TRNG_REG_IE_ZEROIZE      BIT(_NIST_TRNG_REG_IE_ZEROIZE)

/* ISTAT */
#define _NIST_TRNG_REG_ISTAT_DONE          4
#define _NIST_TRNG_REG_ISTAT_ALARMS        3
#define _NIST_TRNG_REG_ISTAT_NOISE_RDY     2
#define _NIST_TRNG_REG_ISTAT_KAT_COMPLETE  1
#define _NIST_TRNG_REG_ISTAT_ZEROIZE       0

#define NIST_TRNG_REG_ISTAT_DONE         BIT(_NIST_TRNG_REG_ISTAT_DONE)
#define NIST_TRNG_REG_ISTAT_ALARMS       BIT(_NIST_TRNG_REG_ISTAT_ALARMS)
#define NIST_TRNG_REG_ISTAT_NOISE_RDY    BIT(_NIST_TRNG_REG_ISTAT_NOISE_RDY)
#define NIST_TRNG_REG_ISTAT_KAT_COMPLETE BIT(_NIST_TRNG_REG_ISTAT_KAT_COMPLETE)
#define NIST_TRNG_REG_ISTAT_ZEROIZE      BIT(_NIST_TRNG_REG_ISTAT_ZEROIZE)

/*EDU_ISTAT*/

#define _NIST_TRNG_EDU_ISTAT_CH2_RBC_URUN        8
#define _NIST_TRNG_EDU_ISTAT_CH1_RBC_URUN        7
#define _NIST_TRNG_EDU_ISTAT_CH0_RBC_URUN        6
#define _NIST_TRNG_EDU_ISTAT_PRIVATE_VTRNG       5
#define _NIST_TRNG_EDU_ISTAT_WAIT_EXP_TIMEOUT    4
#define _NIST_TRNG_EDU_ISTAT_RNC_DRVN_OFFLINE    3
#define _NIST_TRNG_EDU_ISTAT_FIFO_URUN           2
#define _NIST_TRNG_EDU_ISTAT_ACCESS_VIOL         1
#define _NIST_TRNG_EDU_ISTAT_RESEED_REMINDER     0

#define NIST_TRNG_EDU_ISTAT_CH2_RBC_URUN       BIT(_NIST_TRNG_EDU_ISTAT_CH2_RBC_URUN)
#define NIST_TRNG_EDU_ISTAT_CH1_RBC_URUN       BIT(_NIST_TRNG_EDU_ISTAT_CH1_RBC_URUN)
#define NIST_TRNG_EDU_ISTAT_CH0_RBC_URUN       BIT(_NIST_TRNG_EDU_ISTAT_CH0_RBC_URUN)
#define NIST_TRNG_EDU_ISTAT_PRIVATE_VTRNG      BIT(_NIST_TRNG_EDU_ISTAT_PRIVATE_VTRNG)
#define NIST_TRNG_EDU_ISTAT_WAIT_EXP_TIMEOUT   BIT(_NIST_TRNG_EDU_ISTAT_WAIT_EXP_TIMEOUT)
#define NIST_TRNG_EDU_ISTAT_RNC_DRVN_OFFLINE   BIT(_NIST_TRNG_EDU_ISTAT_RNC_DRVN_OFFLINE)
#define NIST_TRNG_EDU_ISTAT_FIFO_URUN          BIT(_NIST_TRNG_EDU_ISTAT_FIFO_URUN)
#define NIST_TRNG_EDU_ISTAT_ACCESS_VIOL        BIT(_NIST_TRNG_EDU_ISTAT_ACCESS_VIOL)
#define NIST_TRNG_EDU_ISTAT_RESEED_REMINDER    BIT(_NIST_TRNG_EDU_ISTAT_RESEED_REMINDER)

/* ALARMS */
#define NIST_TRNG_REG_ALARM_ILLEGAL_CMD_SEQ                      BIT(4)
#define NIST_TRNG_REG_ALARM_FAILED_TEST_ID_OK                    0
#define NIST_TRNG_REG_ALARM_FAILED_TEST_ID_KAT_STAT              1
#define NIST_TRNG_REG_ALARM_FAILED_TEST_ID_KAT                   2
#define NIST_TRNG_REG_ALARM_FAILED_TEST_ID_MONOBIT               3
#define NIST_TRNG_REG_ALARM_FAILED_TEST_ID_RUN                   4
#define NIST_TRNG_REG_ALARM_FAILED_TEST_ID_LONGRUN               5
#define NIST_TRNG_REG_ALARM_FAILED_TEST_ID_AUTOCORRELATION       6
#define NIST_TRNG_REG_ALARM_FAILED_TEST_ID_POKER                 7
#define NIST_TRNG_REG_ALARM_FAILED_TEST_ID_REPETITION_COUNT      8
#define NIST_TRNG_REG_ALARM_FAILED_TEST_ID_ADAPATIVE_PROPORTION  9

/* COREKIT_REL */
#define NIST_TRNG_REG_EXT_ENUM(x)     (((x) >> 28) & 0xF)
#define NIST_TRNG_REG_EXT_VER(x)      (((x) >> 23) & 0xFF)
#define NIST_TRNG_REG_REL_NUM(x)      ((x) & 0xFFFF)

// This will be deleted ?? per comments in hw details. ie use CFG
/* FEATURES */
#define NIST_TRNG_REG_FEATURES_AES_256(x)           (((x) >> 9) & 1)
#define NIST_TRNG_REG_FEATURES_EXTRA_PS_PRESENT(x)  (((x) >> 8) & 1)
#define NIST_TRNG_REG_FEATURES_DIAG_LEVEL_NS(x)     (((x) >> 7) & 1)
#define NIST_TRNG_REG_FEATURES_DIAG_LEVEL_BASIC_TRNG(x) (((x) >> 4) & 7)
#define NIST_TRNG_REG_FEATURES_DIAG_LEVEL_ST_HLT(x) (((x) >> 1) & 7)
#define NIST_TRNG_REG_FEATURES_SECURE_RST_STATE(x)  ((x) & 1)

/* build_CFG0 */
#define NIST_TRNG_REG_CFG0_PERSONILIZATION_STR(x) (((x) >> 14) & 1)
#define NIST_TRNG_REG_CFG0_AES_MAX_KEY_SIZE(x)    (((x) >> 13) & 1)
#define NIST_TRNG_REG_CFG0_AES_DATAPATH(x)        (((x) >> 12) & 1)
#define NIST_TRNG_REG_CFG0_EDU_PRESENT(x)         (((x) >> 11) & 1)
#define NIST_TRNG_REG_CFG0_BACGROUND_NOISE(x)     (((x) >> 10) & 1)
#define NIST_TRNG_REG_CFG0_CDC_SYNCH_DEPTH(x)     (((x) >> 8) & 3)
#define NIST_TRNG_REG_CFG0_BG8(x)                 (((x) >> 7) & 1)
#define NIST_TRNG_REG_CFG0_CORE_TYPE(x)           ((x) & 3)

/* build_CFG1 */
#define NIST_TRNG_REG_CFG1_ENT_SRC_REP_MIN_ENTROPY(x) (((x) >> 24) & 255)
#define NIST_TRNG_REG_CFG1_ENT_SRC_REP_TEST(x)        (((x) >> 23) & 1)
#define NIST_TRNG_REG_CFG1_ENT_SRC_REP_SMPL_SIZE(x)   (((x) >> 20) & 7)
#define NIST_TRNG_REG_CFG1_RAW_HT_REP_TEST(x)         (((x) >> 19) & 1)
#define NIST_TRNG_REG_CFG1_RAW_HT_ADAP_TEST(x)        (((x) >> 16) & 7)
#define NIST_TRNG_REG_CFG1_POKER_TEST(x)              (((x) >> 15) & 1)
#define NIST_TRNG_REG_CFG1_RUN_TEST(x)                (((x) >> 14) & 1)
#define NIST_TRNG_REG_CFG1_MONO_BIT_TEST(x)           (((x) >> 13) & 1)
#define NIST_TRNG_REG_CFG1_AUTO_CORRELATION_TEST(x)   (((x) >> 12) & 1)
#define NIST_TRNG_REG_CFG1_STICKY_STARTUP(x)          (((x) >> 8) & 1)
#define NIST_TRNG_REG_CFG1_NUM_RAW_NOISE_BLKS(x)      ((x) & 255)

/* EDU_BUILD_CFG0 */
#define NIST_TRNG_REG_EDU_CFG0_RBC2_RATE_WIDTH(x)     (((x) >> 20) & 7)
#define NIST_TRNG_REG_EDU_CFG0_RBC1_RATE_WIDTH(x)     (((x) >> 16) & 7)
#define NIST_TRNG_REG_EDU_CFG0_RBC0_RATE_WIDTH(x)     (((x) >> 12) & 7)
#define NIST_TRNG_REG_EDU_CFG0_PUBLIC_VTRNG_CHANNELS(x) (((x) >> 8) & 15)
#define NIST_TRNG_REG_EDU_CFG0_ESM_CHANNEL(x)         (((x) >> 6) & 1)
#define NIST_TRNG_REG_EDU_CFG0_RBC_CHANNELS(x)        (((x) >> 4) & 3)
#define NIST_TRNG_REG_EDU_CFG0_FIFO_DEPTH(x)          (((x) >> 2) & 7)

/* EDU_VSTAT */
#define NIST_TRNG_REG_EDU_VSTAT_BUSY(x)               (((x) >> 31) & 1)
#define NIST_TRNG_REG_EDU_VSTAT_RNC_ENABLED(x)        (((x) >> 30) & 1)
#define NIST_TRNG_REG_EDU_VSTAT_SEED_ENUM(x)          (((x) >> 28) & 3)
#define NIST_TRNG_REG_EDU_VSTAT_RWUE(x)               (((x) >> 27) & 1)
#define NIST_TRNG_REG_EDU_VSTAT_RWNE(x)               (((x) >> 26) & 1)
#define NIST_TRNG_REG_EDU_VSTAT_SRWE(x)               (((x) >> 25) & 1)
#define NIST_TRNG_REG_EDU_VSTAT_ANY_RW1(x)            (((x) >> 24) & 1)
#define NIST_TRNG_REG_EDU_VSTAT_BCKGRND_NOISE(x)      (((x) >> 23) & 1)
#define NIST_TRNG_REG_EDU_VSTAT_RNC_FIFO_EMPTY(x)     (((x) >> 22) & 1)
#define NIST_TRNG_REG_EDU_VSTAT_SLICE_RWI3(x)         (((x) >> 15) & 1)
#define NIST_TRNG_REG_EDU_VSTAT_SLICE_RWI2(x)         (((x) >> 14) & 1)
#define NIST_TRNG_REG_EDU_VSTAT_SLICE_RWI1(x)         (((x) >> 13) & 1)
#define NIST_TRNG_REG_EDU_VSTAT_SLICE_RWI0(x)         (((x) >> 12) & 1)
#define NIST_TRNG_REG_EDU_VSTAT_SLICE_VLD3(x)         (((x) >> 11) & 1)
#define NIST_TRNG_REG_EDU_VSTAT_SLICE_VLD2(x)         (((x) >> 10) & 1)
#define NIST_TRNG_REG_EDU_VSTAT_SLICE_VLD1(x)         (((x) >> 9) & 1)
#define NIST_TRNG_REG_EDU_VSTAT_SLICE_VLD0(x)         (((x) >> 8) & 1)
#define NIST_TRNG_REG_EDU_VSTAT_CURRENT_CMD(x)        (((x) >> 4) & 15)
#define NIST_TRNG_REG_EDU_VSTAT_LAST_CMD(x)           ((x) & 15)

#define _NIST_TRNG_REG_SMODE_MAX_REJECTS_MASK    255UL
#define _NIST_TRNG_REG_SMODE_SECURE_EN_MASK        1UL
#define _NIST_TRNG_REG_SMODE_NONCE_MASK            1UL
#define _NIST_TRNG_REG_MODE_SEC_ALG_MASK           1UL
#define _NIST_TRNG_REG_MODE_ADDIN_PRESENT_MASK     1UL
#define _NIST_TRNG_REG_MODE_PRED_RESIST_MASK       1UL
#define _NIST_TRNG_REG_MODE_KAT_SEL_MASK           3UL
#define _NIST_TRNG_REG_MODE_KAT_VEC_MASK           3UL
#define _NIST_TRNG_REG_STAT_DRBG_STATE_MASK        3UL
#define _NIST_TRNG_REG_STAT_SECURE_MASK            1UL
#define _NIST_TRNG_REG_STAT_NONCE_MASK             1UL

#define NIST_TRNG_REG_SMODE_SET_MAX_REJECTS(y, x)  (((y) & ~(_NIST_TRNG_REG_SMODE_MAX_REJECTS_MASK << _NIST_TRNG_REG_SMODE_MAX_REJECTS)) | ((x) << _NIST_TRNG_REG_SMODE_MAX_REJECTS))
#define NIST_TRNG_REG_SMODE_SET_SECURE_EN(y, x)    (((y) & ~(_NIST_TRNG_REG_SMODE_SECURE_EN_MASK   << _NIST_TRNG_REG_SMODE_SECURE_EN))   | ((x) << _NIST_TRNG_REG_SMODE_SECURE_EN))
#define NIST_TRNG_REG_SMODE_SET_NONCE(y, x)        (((y) & ~(_NIST_TRNG_REG_SMODE_NONCE_MASK       << _NIST_TRNG_REG_SMODE_NONCE))       | ((x) << _NIST_TRNG_REG_SMODE_NONCE))
#define NIST_TRNG_REG_SMODE_GET_MAX_REJECTS(x)     (((x) >> _NIST_TRNG_REG_SMODE_MAX_REJECTS) & _NIST_TRNG_REG_SMODE_MAX_REJECTS_MASK)
#define NIST_TRNG_REG_SMODE_GET_SECURE_EN(x)       (((x) >> _NIST_TRNG_REG_SMODE_SECURE_EN)   & _NIST_TRNG_REG_SMODE_SECURE_EN_MASK)
#define NIST_TRNG_REG_SMODE_GET_NONCE(x)           (((x) >> _NIST_TRNG_REG_SMODE_NONCE)       & _NIST_TRNG_REG_SMODE_NONCE_MASK)

#define NIST_TRNG_REG_MODE_SET_SEC_ALG(y, x)       (((y) & ~(_NIST_TRNG_REG_MODE_SEC_ALG_MASK       << _NIST_TRNG_REG_MODE_SEC_ALG))        | ((x) << _NIST_TRNG_REG_MODE_SEC_ALG))
#define NIST_TRNG_REG_MODE_SET_PRED_RESIST(y, x)   (((y) & ~(_NIST_TRNG_REG_MODE_PRED_RESIST_MASK   << _NIST_TRNG_REG_MODE_PRED_RESIST))    | ((x) << _NIST_TRNG_REG_MODE_PRED_RESIST))
#define NIST_TRNG_REG_MODE_SET_ADDIN_PRESENT(y, x) (((y) & ~(_NIST_TRNG_REG_MODE_ADDIN_PRESENT_MASK << _NIST_TRNG_REG_MODE_ADDIN_PRESENT))  | ((x) << _NIST_TRNG_REG_MODE_ADDIN_PRESENT))
#define NIST_TRNG_REG_MODE_SET_KAT_SEL(y, x)       (((y) & ~(_NIST_TRNG_REG_MODE_KAT_SEL_MASK << _NIST_TRNG_REG_MODE_KAT_SEL)) | ((x) << _NIST_TRNG_REG_MODE_KAT_SEL))
#define NIST_TRNG_REG_MODE_SET_KAT_VEC(y, x)       (((y) & ~(_NIST_TRNG_REG_MODE_KAT_VEC_MASK << _NIST_TRNG_REG_MODE_KAT_VEC)) | ((x) << _NIST_TRNG_REG_MODE_KAT_VEC))
#define NIST_TRNG_REG_MODE_GET_SEC_ALG(x)          (((x) >> _NIST_TRNG_REG_MODE_SEC_ALG)       & _NIST_TRNG_REG_MODE_SEC_ALG_MASK)
#define NIST_TRNG_REG_MODE_GET_PRED_RESIST(x)      (((x) >> _NIST_TRNG_REG_MODE_PRED_RESIST)   & _NIST_TRNG_REG_MODE_PRED_RESIST_MASK)
#define NIST_TRNG_REG_MODE_GET_ADDIN_PRESENT(x)    (((x) >> _NIST_TRNG_REG_MODE_ADDIN_PRESENT) & _NIST_TRNG_REG_MODE_ADDIN_PRESENT_MASK)
#define NIST_TRNG_REG_STAT_GET_DRBG_STATE(x)       (((x) >> _NIST_TRNG_REG_STAT_DRBG_STATE) & _NIST_TRNG_REG_STAT_DRBG_STATE_MASK)
#define NIST_TRNG_REG_STAT_GET_SECURE(x)           (((x) >> _NIST_TRNG_REG_STAT_SECURE) & _NIST_TRNG_REG_STAT_SECURE_MASK)
#define NIST_TRNG_REG_STAT_GET_NONCE(x)            (((x) >> _NIST_TRNG_REG_STAT_NONCE_MODE) & _NIST_TRNG_REG_STAT_NONCE_MASK)

#endif

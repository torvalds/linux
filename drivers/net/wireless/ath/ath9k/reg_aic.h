/*
 * Copyright (c) 2015 Qualcomm Atheros Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef REG_AIC_H
#define REG_AIC_H

#define AR_SM_BASE                              0xa200
#define AR_SM1_BASE                             0xb200
#define AR_AGC_BASE                             0x9e00

#define AR_PHY_AIC_CTRL_0_B0                    (AR_SM_BASE + 0x4b0)
#define AR_PHY_AIC_CTRL_1_B0                    (AR_SM_BASE + 0x4b4)
#define AR_PHY_AIC_CTRL_2_B0                    (AR_SM_BASE + 0x4b8)
#define AR_PHY_AIC_CTRL_3_B0                    (AR_SM_BASE + 0x4bc)
#define AR_PHY_AIC_CTRL_4_B0                    (AR_SM_BASE + 0x4c0)

#define AR_PHY_AIC_STAT_0_B0                    (AR_SM_BASE + 0x4c4)
#define AR_PHY_AIC_STAT_1_B0                    (AR_SM_BASE + 0x4c8)
#define AR_PHY_AIC_STAT_2_B0                    (AR_SM_BASE + 0x4cc)

#define AR_PHY_AIC_CTRL_0_B1                    (AR_SM1_BASE + 0x4b0)
#define AR_PHY_AIC_CTRL_1_B1                    (AR_SM1_BASE + 0x4b4)
#define AR_PHY_AIC_CTRL_4_B1                    (AR_SM1_BASE + 0x4c0)

#define AR_PHY_AIC_STAT_0_B1                    (AR_SM1_BASE + 0x4c4)
#define AR_PHY_AIC_STAT_1_B1                    (AR_SM1_BASE + 0x4c8)
#define AR_PHY_AIC_STAT_2_B1                    (AR_SM1_BASE + 0x4cc)

#define AR_PHY_AIC_SRAM_ADDR_B0                 (AR_SM_BASE + 0x5f0)
#define AR_PHY_AIC_SRAM_DATA_B0                 (AR_SM_BASE + 0x5f4)

#define AR_PHY_AIC_SRAM_ADDR_B1                 (AR_SM1_BASE + 0x5f0)
#define AR_PHY_AIC_SRAM_DATA_B1                 (AR_SM1_BASE + 0x5f4)

#define AR_PHY_BT_COEX_4                        (AR_AGC_BASE + 0x60)
#define AR_PHY_BT_COEX_5                        (AR_AGC_BASE + 0x64)

/* AIC fields */
#define AR_PHY_AIC_MON_ENABLE                   0x80000000
#define AR_PHY_AIC_MON_ENABLE_S                 31
#define AR_PHY_AIC_CAL_MAX_HOP_COUNT            0x7F000000
#define AR_PHY_AIC_CAL_MAX_HOP_COUNT_S          24
#define AR_PHY_AIC_CAL_MIN_VALID_COUNT          0x00FE0000
#define AR_PHY_AIC_CAL_MIN_VALID_COUNT_S        17
#define AR_PHY_AIC_F_WLAN                       0x0001FC00
#define AR_PHY_AIC_F_WLAN_S                     10
#define AR_PHY_AIC_CAL_CH_VALID_RESET           0x00000200
#define AR_PHY_AIC_CAL_CH_VALID_RESET_S         9
#define AR_PHY_AIC_CAL_ENABLE                   0x00000100
#define AR_PHY_AIC_CAL_ENABLE_S                 8
#define AR_PHY_AIC_BTTX_PWR_THR                 0x000000FE
#define AR_PHY_AIC_BTTX_PWR_THR_S               1
#define AR_PHY_AIC_ENABLE                       0x00000001
#define AR_PHY_AIC_ENABLE_S                     0
#define AR_PHY_AIC_CAL_BT_REF_DELAY             0x00F00000
#define AR_PHY_AIC_CAL_BT_REF_DELAY_S           20
#define AR_PHY_AIC_BT_IDLE_CFG                  0x00080000
#define AR_PHY_AIC_BT_IDLE_CFG_S                19
#define AR_PHY_AIC_STDBY_COND                   0x00060000
#define AR_PHY_AIC_STDBY_COND_S                 17
#define AR_PHY_AIC_STDBY_ROT_ATT_DB             0x0001F800
#define AR_PHY_AIC_STDBY_ROT_ATT_DB_S           11
#define AR_PHY_AIC_STDBY_COM_ATT_DB             0x00000700
#define AR_PHY_AIC_STDBY_COM_ATT_DB_S           8
#define AR_PHY_AIC_RSSI_MAX                     0x000000F0
#define AR_PHY_AIC_RSSI_MAX_S                   4
#define AR_PHY_AIC_RSSI_MIN                     0x0000000F
#define AR_PHY_AIC_RSSI_MIN_S                   0
#define AR_PHY_AIC_RADIO_DELAY                  0x7F000000
#define AR_PHY_AIC_RADIO_DELAY_S                24
#define AR_PHY_AIC_CAL_STEP_SIZE_CORR           0x00F00000
#define AR_PHY_AIC_CAL_STEP_SIZE_CORR_S         20
#define AR_PHY_AIC_CAL_ROT_IDX_CORR             0x000F8000
#define AR_PHY_AIC_CAL_ROT_IDX_CORR_S           15
#define AR_PHY_AIC_CAL_CONV_CHECK_FACTOR        0x00006000
#define AR_PHY_AIC_CAL_CONV_CHECK_FACTOR_S      13
#define AR_PHY_AIC_ROT_IDX_COUNT_MAX            0x00001C00
#define AR_PHY_AIC_ROT_IDX_COUNT_MAX_S          10
#define AR_PHY_AIC_CAL_SYNTH_TOGGLE             0x00000200
#define AR_PHY_AIC_CAL_SYNTH_TOGGLE_S           9
#define AR_PHY_AIC_CAL_SYNTH_AFTER_BTRX         0x00000100
#define AR_PHY_AIC_CAL_SYNTH_AFTER_BTRX_S       8
#define AR_PHY_AIC_CAL_SYNTH_SETTLING           0x000000FF
#define AR_PHY_AIC_CAL_SYNTH_SETTLING_S         0
#define AR_PHY_AIC_MON_MAX_HOP_COUNT            0x07F00000
#define AR_PHY_AIC_MON_MAX_HOP_COUNT_S          20
#define AR_PHY_AIC_MON_MIN_STALE_COUNT          0x000FE000
#define AR_PHY_AIC_MON_MIN_STALE_COUNT_S        13
#define AR_PHY_AIC_MON_PWR_EST_LONG             0x00001000
#define AR_PHY_AIC_MON_PWR_EST_LONG_S           12
#define AR_PHY_AIC_MON_PD_TALLY_SCALING         0x00000C00
#define AR_PHY_AIC_MON_PD_TALLY_SCALING_S       10
#define AR_PHY_AIC_MON_PERF_THR                 0x000003E0
#define AR_PHY_AIC_MON_PERF_THR_S               5
#define AR_PHY_AIC_CAL_TARGET_MAG_SETTING       0x00000018
#define AR_PHY_AIC_CAL_TARGET_MAG_SETTING_S     3
#define AR_PHY_AIC_CAL_PERF_CHECK_FACTOR        0x00000006
#define AR_PHY_AIC_CAL_PERF_CHECK_FACTOR_S      1
#define AR_PHY_AIC_CAL_PWR_EST_LONG             0x00000001
#define AR_PHY_AIC_CAL_PWR_EST_LONG_S           0
#define AR_PHY_AIC_MON_DONE                     0x80000000
#define AR_PHY_AIC_MON_DONE_S                   31
#define AR_PHY_AIC_MON_ACTIVE                   0x40000000
#define AR_PHY_AIC_MON_ACTIVE_S                 30
#define AR_PHY_AIC_MEAS_COUNT                   0x3F000000
#define AR_PHY_AIC_MEAS_COUNT_S                 24
#define AR_PHY_AIC_CAL_ANT_ISO_EST              0x00FC0000
#define AR_PHY_AIC_CAL_ANT_ISO_EST_S            18
#define AR_PHY_AIC_CAL_HOP_COUNT                0x0003F800
#define AR_PHY_AIC_CAL_HOP_COUNT_S              11
#define AR_PHY_AIC_CAL_VALID_COUNT              0x000007F0
#define AR_PHY_AIC_CAL_VALID_COUNT_S            4
#define AR_PHY_AIC_CAL_BT_TOO_WEAK_ERR          0x00000008
#define AR_PHY_AIC_CAL_BT_TOO_WEAK_ERR_S        3
#define AR_PHY_AIC_CAL_BT_TOO_STRONG_ERR        0x00000004
#define AR_PHY_AIC_CAL_BT_TOO_STRONG_ERR_S      2
#define AR_PHY_AIC_CAL_DONE                     0x00000002
#define AR_PHY_AIC_CAL_DONE_S                   1
#define AR_PHY_AIC_CAL_ACTIVE                   0x00000001
#define AR_PHY_AIC_CAL_ACTIVE_S                 0

#define AR_PHY_AIC_MEAS_MAG_MIN                 0xFFC00000
#define AR_PHY_AIC_MEAS_MAG_MIN_S               22
#define AR_PHY_AIC_MON_STALE_COUNT              0x003F8000
#define AR_PHY_AIC_MON_STALE_COUNT_S            15
#define AR_PHY_AIC_MON_HOP_COUNT                0x00007F00
#define AR_PHY_AIC_MON_HOP_COUNT_S              8
#define AR_PHY_AIC_CAL_AIC_SM                   0x000000F8
#define AR_PHY_AIC_CAL_AIC_SM_S                 3
#define AR_PHY_AIC_SM                           0x00000007
#define AR_PHY_AIC_SM_S                         0
#define AR_PHY_AIC_SRAM_VALID                   0x00000001
#define AR_PHY_AIC_SRAM_VALID_S                 0
#define AR_PHY_AIC_SRAM_ROT_QUAD_ATT_DB         0x0000007E
#define AR_PHY_AIC_SRAM_ROT_QUAD_ATT_DB_S       1
#define AR_PHY_AIC_SRAM_VGA_QUAD_SIGN           0x00000080
#define AR_PHY_AIC_SRAM_VGA_QUAD_SIGN_S         7
#define AR_PHY_AIC_SRAM_ROT_DIR_ATT_DB          0x00003F00
#define AR_PHY_AIC_SRAM_ROT_DIR_ATT_DB_S        8
#define AR_PHY_AIC_SRAM_VGA_DIR_SIGN            0x00004000
#define AR_PHY_AIC_SRAM_VGA_DIR_SIGN_S          14
#define AR_PHY_AIC_SRAM_COM_ATT_6DB             0x00038000
#define AR_PHY_AIC_SRAM_COM_ATT_6DB_S           15
#define AR_PHY_AIC_CAL_ROT_ATT_DB_EST_ISO       0x0000E000
#define AR_PHY_AIC_CAL_ROT_ATT_DB_EST_ISO_S     13
#define AR_PHY_AIC_CAL_COM_ATT_DB_EST_ISO       0x00001E00
#define AR_PHY_AIC_CAL_COM_ATT_DB_EST_ISO_S     9
#define AR_PHY_AIC_CAL_ISO_EST_INIT_SETTING     0x000001F8
#define AR_PHY_AIC_CAL_ISO_EST_INIT_SETTING_S   3
#define AR_PHY_AIC_CAL_COM_ATT_DB_BACKOFF       0x00000006
#define AR_PHY_AIC_CAL_COM_ATT_DB_BACKOFF_S     1
#define AR_PHY_AIC_CAL_COM_ATT_DB_FIXED         0x00000001
#define AR_PHY_AIC_CAL_COM_ATT_DB_FIXED_S       0

#endif /* REG_AIC_H */

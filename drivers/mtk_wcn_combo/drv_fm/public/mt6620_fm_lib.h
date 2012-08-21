/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein
 * is confidential and proprietary to MediaTek Inc. and/or its licensors.
 * Without the prior written permission of MediaTek inc. and/or its licensors,
 * any reproduction, modification, use or disclosure of MediaTek Software,
 * and information contained herein, in whole or in part, shall be strictly prohibited.
 *
 * MediaTek Inc. (C) 2010. All rights reserved.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER ON
 * AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
 * NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
 * SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
 * SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
 * THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
 * THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
 * CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
 * SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
 * CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
 * AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
 * OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
 * MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek Software")
 * have been modified by MediaTek Inc. All revisions are subject to any receiver's
 * applicable license agreements with MediaTek Inc.
 */

#include <linux/types.h>

//#define MT6620_FPGA

#define FM_MAIN_PGSEL   (0x9F)
#define FM_MAIN_BASE            (0x0)
#define FM_MAIN_BITMAP0         (FM_MAIN_BASE + 0x80)
#define FM_MAIN_BITMAP1         (FM_MAIN_BASE + 0x81)
#define FM_MAIN_BITMAP2         (FM_MAIN_BASE + 0x82)
#define FM_MAIN_BITMAP3         (FM_MAIN_BASE + 0x83)
#define FM_MAIN_BITMAP4         (FM_MAIN_BASE + 0x84)
#define FM_MAIN_BITMAP5         (FM_MAIN_BASE + 0x85)
#define FM_MAIN_BITMAP6         (FM_MAIN_BASE + 0x86)
#define FM_MAIN_BITMAP7         (FM_MAIN_BASE + 0x87)
#define FM_MAIN_BITMAP8         (FM_MAIN_BASE + 0x88)
#define FM_MAIN_BITMAP9         (FM_MAIN_BASE + 0x89)
#define FM_MAIN_BITMAPA         (FM_MAIN_BASE + 0x8a)
#define FM_MAIN_BITMAPB         (FM_MAIN_BASE + 0x8b)
#define FM_MAIN_BITMAPC         (FM_MAIN_BASE + 0x8c)
#define FM_MAIN_BITMAPD         (FM_MAIN_BASE + 0x8d)
#define FM_MAIN_BITMAPE         (FM_MAIN_BASE + 0x8e)
#define FM_MAIN_BITMAPF         (FM_MAIN_BASE + 0x8f)

int mt6620_off_2_longANA(unsigned char *tx_buf, int tx_buf_size);
int mt6620_off_2_longANA_ack(unsigned char *tx_buf, int tx_buf_size);
int mt6620_rx_digital_init(unsigned char *tx_buf, int tx_buf_size);
int mt6620_rx_digital_init_ack(unsigned char *tx_buf, int tx_buf_size);
int mt6620_powerdown(unsigned char *tx_buf, int tx_buf_size);
int mt6620_powerdown_ack(unsigned char *tx_buf, int tx_buf_size);
int mt6620_rampdown(unsigned char *tx_buf, int tx_buf_size);
int mt6620_rampdown_tx(unsigned char *tx_buf, int tx_buf_size);
int mt6620_rampdown_ack(unsigned char *tx_buf, int tx_buf_size);
int mt6620_tune_1(unsigned char *tx_buf, int tx_buf_size, uint16_t freq);
int mt6620_tune_2(unsigned char *tx_buf, int tx_buf_size, uint16_t freq);
int mt6620_tune_3(unsigned char *tx_buf, int tx_buf_size, uint16_t freq);
int mt6620_fast_tune(unsigned char *tx_buf, int tx_buf_size, uint16_t freq);
int mt6620_tune_txscan(unsigned char *tx_buf, int tx_buf_size, uint16_t freq);
int mt6620_tune_tx(unsigned char *tx_buf, int tx_buf_size, uint16_t freq);
int mt6620_tune_ack(unsigned char *tx_buf, int tx_buf_size);
int mt6620_seek_1(unsigned char *tx_buf, int tx_buf_size, uint16_t seekdir, uint16_t space, uint16_t max_freq, uint16_t min_freq);
int mt6620_seek_2(unsigned char *tx_buf, int tx_buf_size, uint16_t seekdir, uint16_t space, uint16_t max_freq, uint16_t min_freq);
int mt6620_seek_ack(unsigned char *tx_buf, int tx_buf_size);
int mt6620_scan_1(unsigned char *tx_buf, int tx_buf_size, uint16_t scandir, uint16_t space, uint16_t max_freq, uint16_t min_freq);
int mt6620_scan_2(unsigned char *tx_buf, int tx_buf_size, uint16_t scandir, uint16_t space, uint16_t max_freq, uint16_t min_freq);
int mt6620_scan_ack(unsigned char *tx_buf, int tx_buf_size);
int mt6620_rds_rx_enable(unsigned char *tx_buf, int tx_buf_size);
int mt6620_rds_rx_enable_ack(unsigned char *tx_buf, int tx_buf_size);
int mt6620_rds_rx_disable(unsigned char *tx_buf, int tx_buf_size);
int mt6620_rds_rx_disable_ack(unsigned char *tx_buf, int tx_buf_size);
int mt6620_rds_tx(unsigned char *tx_buf, int tx_buf_size, uint16_t pi, uint16_t *ps, uint16_t *other_rds, uint8_t other_rds_cnt);
int mt6620_rds_tx_ack(unsigned char *tx_buf, int tx_buf_size);
int mt6620_get_reg(unsigned char *tx_buf, int tx_buf_size, uint8_t addr);
int mt6620_set_reg(unsigned char *tx_buf, int tx_buf_size, uint8_t addr, uint16_t value);
int mt6620_set_reg_ack(unsigned char *tx_buf, int tx_buf_size);
int mt6620_off_2_tx_shortANA(unsigned char *tx_buf, int tx_buf_size);
int mt6620_off_2_tx_shortANA_ack(unsigned char *tx_buf, int tx_buf_size);
int mt6620_dig_init(unsigned char *tx_buf, int tx_buf_size);
int mt6620_dig_init_ack(unsigned char *tx_buf, int tx_buf_size);
int mt6620_com(unsigned char *tx_buf, int tx_buf_size, int opcode, void* data);

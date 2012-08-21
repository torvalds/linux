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

#include <linux/kernel.h>
#include <linux/types.h>
#include "fm.h"
#include "mt6620_fm.h"
#include "mt6620_fm_lib.h"

//#define MT6620E1
//#define MT6620_FM_USE_SHORT_ANTENNA

int fm_bop_write(uint8_t addr, uint16_t value, unsigned char *buf, int size)
{
    if(size < (FM_WRITE_BASIC_OP_SIZE+2)){
        return (-1);
    }

    if(buf == NULL){
        return (-2);
    }

    buf[0] = FM_WRITE_BASIC_OP;
    buf[1] = FM_WRITE_BASIC_OP_SIZE;
    buf[2] = addr;
    buf[3] = (uint8_t)((value) & 0x00FF);
    buf[4] = (uint8_t)((value >> 8) & 0x00FF);

    FM_LOG_DBG(D_MAIN,"%02x %02x %02x %02x %02x \n", buf[0], buf[1], buf[2], buf[3], buf[4]);

    return (FM_WRITE_BASIC_OP_SIZE+2);
}

int fm_bop_udelay(uint32_t value, unsigned char *buf, int size)
{
    if(size < (FM_UDELAY_BASIC_OP_SIZE+2)){
        return (-1);
    }

    if(buf == NULL){
        return (-2);
    }

    buf[0] = FM_UDELAY_BASIC_OP;
    buf[1] = FM_UDELAY_BASIC_OP_SIZE;
    buf[2] = (uint8_t)((value) & 0x000000FF);
    buf[3] = (uint8_t)((value >> 8) & 0x000000FF);
    buf[4] = (uint8_t)((value >> 16) & 0x000000FF);
    buf[5] = (uint8_t)((value >> 24) & 0x000000FF);

    FM_LOG_DBG(D_MAIN,"%02x %02x %02x %02x %02x %02x \n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);

    return (FM_UDELAY_BASIC_OP_SIZE+2);
}

int fm_bop_rd_until(uint8_t addr, uint16_t mask, uint16_t value, unsigned char *buf, int size)
{
    if(size < (FM_RD_UNTIL_BASIC_OP_SIZE+2)){
        return (-1);
    }

    if(buf == NULL){
        return (-2);
    }

    buf[0] = FM_RD_UNTIL_BASIC_OP;
    buf[1] = FM_RD_UNTIL_BASIC_OP_SIZE;
    buf[2] = addr;
    buf[3] = (uint8_t)((mask) & 0x00FF);
    buf[4] = (uint8_t)((mask >> 8) & 0x00FF);
    buf[5] = (uint8_t)((value) & 0x00FF);
    buf[6] = (uint8_t)((value >> 8) & 0x00FF);

    FM_LOG_DBG(D_MAIN,"%02x %02x %02x %02x %02x %02x %02x \n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);

    return (FM_RD_UNTIL_BASIC_OP_SIZE+2);
}

int fm_bop_modify(uint8_t addr, uint16_t mask_and, uint16_t mask_or, unsigned char *buf, int size)
{
    if(size < (FM_MODIFY_BASIC_OP_SIZE+2)){
        return (-1);
    }

    if(buf == NULL){
        return (-2);
    }

    buf[0] = FM_MODIFY_BASIC_OP;
    buf[1] = FM_MODIFY_BASIC_OP_SIZE;
    buf[2] = addr;
    buf[3] = (uint8_t)((mask_and) & 0x00FF);
    buf[4] = (uint8_t)((mask_and >> 8) & 0x00FF);
    buf[5] = (uint8_t)((mask_or) & 0x00FF);
    buf[6] = (uint8_t)((mask_or >> 8) & 0x00FF);

    FM_LOG_DBG(D_MAIN,"%02x %02x %02x %02x %02x %02x %02x \n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);

    return (FM_MODIFY_BASIC_OP_SIZE+2);
}

int fm_bop_msleep(uint32_t value, unsigned char *buf, int size)
{
    if(size < (FM_MSLEEP_BASIC_OP_SIZE+2)){
        return (-1);
    }

    if(buf == NULL){
        return (-2);
    }

    buf[0] = FM_MSLEEP_BASIC_OP;
    buf[1] = FM_MSLEEP_BASIC_OP_SIZE;
    buf[2] = (uint8_t)((value) & 0x000000FF);
    buf[3] = (uint8_t)((value >> 8) & 0x000000FF);
    buf[4] = (uint8_t)((value >> 16) & 0x000000FF);
    buf[5] = (uint8_t)((value >> 24) & 0x000000FF);

    FM_LOG_DBG(D_MAIN,"%02x %02x %02x %02x %02x %02x \n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);

    return (FM_MSLEEP_BASIC_OP_SIZE+2);
}

//MT6620 IC
int mt6620_off_2_longANA(unsigned char *tx_buf, int tx_buf_size)
{
    int pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_ENABLE_OPCODE;
    pkt_size = 4;
    
    //A01 Turn on the bandgap and central biasing core
    pkt_size += fm_bop_write(0x01, 0x4A00, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1 4A00
    pkt_size += fm_bop_udelay(30, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 30
    pkt_size += fm_bop_write(0x01, 0x6A00, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1 6A00
    pkt_size += fm_bop_udelay(50, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 50
    //A02 Initialise the LDO's Output
    pkt_size += fm_bop_write(0x02, 0x299C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 2 299C
    //A03 Enable RX, ADC and ADPLL LDO
    pkt_size += fm_bop_write(0x01, 0x6B82, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1 6B82
    //A04 Update FMRF optimized register settings
    pkt_size += fm_bop_write(0x04, 0x0142, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 4 0142
    pkt_size += fm_bop_write(0x05, 0x00E7, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 5 00E7
    pkt_size += fm_bop_write(0x0A, 0x0060, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a 0060
    pkt_size += fm_bop_write(0x0C, 0xA88C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c A88C
    pkt_size += fm_bop_write(0x0D, 0x0888, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr d 0888
    pkt_size += fm_bop_write(0x10, 0x1E8D, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 10 1E8D
    pkt_size += fm_bop_write(0x27, 0x0005, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 27 0005
    pkt_size += fm_bop_write(0x0E, 0x0040, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr e 0040
    pkt_size += fm_bop_write(0x03, 0x50F0, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3 50f0
    pkt_size += fm_bop_write(0x3F, 0xAD06, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3f AD06
    pkt_size += fm_bop_write(0x3E, 0x3280, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3e 3280
    pkt_size += fm_bop_write(0x06, 0x0124, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 6 0124
    pkt_size += fm_bop_write(0x08, 0x15B8, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 8 15B8
    //A05 Enable RX related blocks
    pkt_size += fm_bop_write(0x28, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 28 0000
    pkt_size += fm_bop_write(0x00, 0x0166, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 0 0166
    pkt_size += fm_bop_write(0x3A, 0x0004, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3a 0004
    pkt_size += fm_bop_write(0x37, 0x2590, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 37 2590
    // FM ADPLL Power Up
    // () for 16.384M mode, otherwise 15.36M
    pkt_size += fm_bop_write(0x25, 0x040F, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 25 040f
    pkt_size += fm_bop_write(0x20, 0x2720, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 20 2720
    //XHC, 2011/03/18, [wr 22 9980->6680]
    pkt_size += fm_bop_write(0x22, 0x6680, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 22 9980
    pkt_size += fm_bop_write(0x25, 0x080F, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 25 080f
    pkt_size += fm_bop_write(0x1E, 0x0863, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1e 0863(0A63)
    pkt_size += fm_bop_udelay(5000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 5000
    pkt_size += fm_bop_write(0x1E, 0x0865, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1e 0865 (0A65)
    pkt_size += fm_bop_udelay(5000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 5000
    pkt_size += fm_bop_write(0x1E, 0x0871, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1e 0871 (0A71)
    pkt_size += fm_bop_udelay(100000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 100000
    pkt_size += fm_bop_write(0x2A, 0x1026, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 2a 1026
    // FM RC Calibration
    pkt_size += fm_bop_write(0x00, 0x01E6, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 0 01E6
    pkt_size += fm_bop_udelay(1, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 1
    pkt_size += fm_bop_write(0x1B, 0x0094, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1b 0094
    pkt_size += fm_bop_write(0x1B, 0x0095, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1b 0095
    pkt_size += fm_bop_udelay(200, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 200
    pkt_size += fm_bop_write(0x1B, 0x0094, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1b 0094
    pkt_size += fm_bop_write(0x00, 0x0166, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 0 0166
    // FM VCO Enable
    pkt_size += fm_bop_write(0x01, 0x6B8A, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1 6B8A
    pkt_size += fm_bop_udelay(1, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 1
    pkt_size += fm_bop_write(0x00, 0xC166, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 0 C166
    pkt_size += fm_bop_udelay(3000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 3000
    pkt_size += fm_bop_write(0x00, 0xF166, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 0 F166
    pkt_size += fm_bop_write(0x09, 0x2964, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9 2964
    // FM RFDIG settings
    pkt_size += fm_bop_write(0x2E, 0x0008, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 2e 8
    pkt_size += fm_bop_write(0x2B, 0x0064, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 2b 64
    pkt_size += fm_bop_write(0x2C, 0x0032, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 2c 32
    pkt_size += fm_bop_write(0x11, 0x17d4, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 11 17d4
    //Update dynamic subband switching setting, XHC 2011/05/17
    pkt_size += fm_bop_write(0x13, 0xFA00, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 13 FA00
    pkt_size += fm_bop_write(0x14, 0x0580, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 14 0580
    pkt_size += fm_bop_write(0x15, 0xFA80, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 15 FA80
    pkt_size += fm_bop_write(0x16, 0x0580, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 16 0580
    pkt_size += fm_bop_write(0x33, 0x0008, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 33 0008
    // FM DCOC Calibration
    pkt_size += fm_bop_write(0x64, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 64 1
    pkt_size += fm_bop_write(0x63, 0x0020, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 63 20
    pkt_size += fm_bop_write(0x9C, 0x0044, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9C 0044
    //pkt_size += fm_bop_write(0x6B, 0x0100, &tx_buf[pkt_size], tx_buf_size - pkt_size);//"Disable other interrupts except for STC_DONE(dependent on interrupt output source selection)"
    pkt_size += fm_bop_write(0x0F, 0x1A08, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr F 1A08
    pkt_size += fm_bop_write(0x63, 0x0021, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 63 21
    pkt_size += fm_bop_rd_until(0x69, 0x0001, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//Poll fm_intr_stc_done (69H D0) = 1
    pkt_size += fm_bop_write(0x69, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 69 1
    pkt_size += fm_bop_write(0x63, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 63 0
    pkt_size += fm_bop_rd_until(0x6F, 0x0001, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//Poll stc_done (6FH D0)= 0
    // Others
    pkt_size += fm_bop_write(0x00, 0xF167, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 0 F167
    pkt_size += fm_bop_udelay(50000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 50000
    
    tx_buf[2] = (uint8_t)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (uint8_t)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

int mt6620_off_2_longANA_ack(unsigned char *tx_buf, int tx_buf_size)
{
    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_EVENT_PKT_TYPE;
    tx_buf[1] = FM_ENABLE_OPCODE;
    tx_buf[2] = 0x00;
    tx_buf[3] = 0x00;

    return 4;
}

int mt6620_rx_digital_init(unsigned char *tx_buf, int tx_buf_size)
{
    int pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_ENABLE_OPCODE;
    pkt_size = 4;

    // fm_rgf_maincon
    //rd 62
    pkt_size += fm_bop_write(0x65, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 65 0
    pkt_size += fm_bop_write(0x64, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 64 1
    pkt_size += fm_bop_write(0x63, 0x0480, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 63 480
    pkt_size += fm_bop_write(0x6D, 0x1AB2, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 6d 1ab2
    pkt_size += fm_bop_write(0x6B, 0x2100, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 6b 2100
    pkt_size += fm_bop_write(0x68, 0xE100, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 68 E100
    pkt_size += fm_bop_udelay(10000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 10000
    pkt_size += fm_bop_write(0x68, 0xE000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 68 E000
    // fm_rgf_dac
    pkt_size += fm_bop_write(0x9C, 0xab48, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9c ab48
    pkt_size += fm_bop_write(0x9E, 0x000C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9e c
    pkt_size += fm_bop_write(0x71, 0x807f, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 71 807f
    pkt_size += fm_bop_write(0x72, 0x878f, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 72 878f
    //XHC, 2011/04/29 update 0x73 form 0x07C3 to 0x07C1 speed up I/Q calibration
    pkt_size += fm_bop_write(0x73, 0x07c1, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 73 7c3
    // fm_rgf_front
    pkt_size += fm_bop_write(0x9F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9f 0
    pkt_size += fm_bop_write(0xCB, 0x00B0, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr cb b0
    //XHC, 2011/05/06 FM RSSI config
    pkt_size += fm_bop_write(0xE0, ((0xA301 & 0xFC00) | (FMR_RSSI_TH_LONG & 0x03FF)), &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr e0 a301
    pkt_size += fm_bop_write(0xE1, ((0x00E9 & 0xFF00) | (FMR_CQI_TH & 0x00FF)), &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr e1 D0~D7, PAMD TH
    //XHC, 2011/04/15 update search MR threshold
    pkt_size += fm_bop_write(0xE3, FMR_MR_TH, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr e3 1B0
    pkt_size += fm_bop_write(0xE4, 0x008F, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr e4 8f
    pkt_size += fm_bop_write(0xCC, 0x0488, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr cc 488
    pkt_size += fm_bop_write(0xD6, 0x036A, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr d6 36a
    pkt_size += fm_bop_write(0xD7, 0x836a, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr d7 836a
    pkt_size += fm_bop_write(0xDD, 0x0080, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr dd 80
    pkt_size += fm_bop_write(0xB0, 0xcd00, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr b0 cd00
    //XHC, 2011/03/18 Update AFC gain[wr 96 41E2->4000][wr 97 049A->021F]
    //[wr 98 0B66->0D00][wr 99 0E1E->0E7F][wr D0 8233->8192][wr D1 20BC->2086]
    //[wr 90 03FF->0192][wr 91 01BE->0086][wr 92 03FF->0192][wr 93 0354->0086]
    //[wr 94 03FF->0192][wr 95 0354->0086][wr 52 17F3]
    pkt_size += fm_bop_write(0x96, 0x4000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 96     41E2 
    pkt_size += fm_bop_write(0x97, 0x021F, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 97     049A 
    pkt_size += fm_bop_write(0x98, 0x0D00, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr  98    0B66 
    pkt_size += fm_bop_write(0x99, 0x0E7F, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 99     0E1E 
    pkt_size += fm_bop_write(0xD0, 0x8192, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr D0    8233 
    pkt_size += fm_bop_write(0xD1, 0x2086, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr D1    20BC 
    pkt_size += fm_bop_write(0x90, 0x0192, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 90     03ff 
    pkt_size += fm_bop_write(0x91, 0x0086, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 91     01BE 
    pkt_size += fm_bop_write(0x92, 0x0192, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 92     03FF 
    pkt_size += fm_bop_write(0x93, 0x0086, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 93     0354 
    pkt_size += fm_bop_write(0x94, 0x0192, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 94     03FF 
    pkt_size += fm_bop_write(0x95, 0x0086, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 95     0354 
    pkt_size += fm_bop_write(0x52, 0x17F3, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 95     0354 
    // fm_rgf_fmx
    pkt_size += fm_bop_write(0x9F, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9f 1
    pkt_size += fm_bop_write(0xDE, 0x3388, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr de 3388
    pkt_size += fm_bop_write(0xC2, 0x0180, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c2 180
    pkt_size += fm_bop_write(0xC3, 0x0180, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c3 180
    pkt_size += fm_bop_write(0xDB, 0x0181, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr db 181
    pkt_size += fm_bop_write(0xDC, 0x0184, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr dc 184
    pkt_size += fm_bop_write(0xA2, 0x03C0, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a2 3c0
    pkt_size += fm_bop_write(0xA3, 0x03C0, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a3 3c0
    pkt_size += fm_bop_write(0xA4, 0x03C0, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a4 3c0
    pkt_size += fm_bop_write(0xA5, 0x03C0, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a5 3c0
    pkt_size += fm_bop_write(0xA6, 0x03C0, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a6 3c0
    pkt_size += fm_bop_write(0xA7, 0x03C0, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a7 3c0
    pkt_size += fm_bop_write(0xE3, 0x0280, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr e3 280
    pkt_size += fm_bop_write(0xE4, 0x0280, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr e4 280
    pkt_size += fm_bop_write(0xE5, 0x0280, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr e5 280
    pkt_size += fm_bop_write(0xE6, 0x026C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr e6 26c
    pkt_size += fm_bop_write(0xE7, 0x026C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr e7 26c
    pkt_size += fm_bop_write(0xEA, 0x026C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ea 26c
    pkt_size += fm_bop_udelay(1000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 1000
    pkt_size += fm_bop_write(0xB6, 0x03FC, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr b6 3fc
    pkt_size += fm_bop_write(0xB7, 0x02C1, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr b7 2c1
    pkt_size += fm_bop_write(0xA8, 0x0820, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 820
    pkt_size += fm_bop_write(0xAC, 0x065E, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ac 65e
    pkt_size += fm_bop_write(0xAD, 0x09AD, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ad 9ad
    pkt_size += fm_bop_write(0xAE, 0x0CF8, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ae cf8
    pkt_size += fm_bop_write(0xAF, 0x0302, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr af 302
    pkt_size += fm_bop_write(0xB0, 0x04A6, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr b0 4a6
    pkt_size += fm_bop_write(0xB1, 0x062C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr b1 62c
    pkt_size += fm_bop_write(0xEC, 0x014A, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ec 14a
    pkt_size += fm_bop_write(0xC8, 0x0232, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c8 232
    pkt_size += fm_bop_write(0xC7, 0x0184, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c7 0184
    pkt_size += fm_bop_write(0xD8, 0x00E8, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr d8 0e8
    pkt_size += fm_bop_write(0xFB, 0x051F, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr fb 51f
    pkt_size += fm_bop_udelay(1000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 1000
    //XHC,2011/03/18 [wr C9 01F0][wr CA 0250][wr D4 2657]
    pkt_size += fm_bop_write(0xC9, 0x01F0, &tx_buf[pkt_size], tx_buf_size - pkt_size);
    pkt_size += fm_bop_write(0xCA, 0x0250, &tx_buf[pkt_size], tx_buf_size - pkt_size);
    pkt_size += fm_bop_write(0xD4, 0x2657, &tx_buf[pkt_size], tx_buf_size - pkt_size);
    pkt_size += fm_bop_write(0x9F, 0x0002, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9f 2
    pkt_size += fm_bop_write(0xA8, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 0
    pkt_size += fm_bop_write(0xA8, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 0
    pkt_size += fm_bop_write(0xA8, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 0
    pkt_size += fm_bop_write(0xA8, 0xFF80, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 ff80
    pkt_size += fm_bop_write(0xA8, 0x0061, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 61
    pkt_size += fm_bop_write(0xA8, 0xFF22, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 ff22
    pkt_size += fm_bop_write(0xA8, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 0
    pkt_size += fm_bop_write(0xA8, 0x0100, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 100
    pkt_size += fm_bop_write(0xA8, 0x009A, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 9a
    pkt_size += fm_bop_write(0xA8, 0xFF80, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 ff80
    pkt_size += fm_bop_write(0xA8, 0x0140, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 140    
    pkt_size += fm_bop_write(0xA8, 0xFF42, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 ff42   
    pkt_size += fm_bop_write(0xA8, 0xFFE0, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 ffe0    
    pkt_size += fm_bop_write(0xA8, 0x0114, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 114    
    pkt_size += fm_bop_write(0xA8, 0xFF4E, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 ff4e   
    pkt_size += fm_bop_write(0xA8, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 0
    pkt_size += fm_bop_write(0xA8, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 0
    pkt_size += fm_bop_write(0xA8, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 0
    pkt_size += fm_bop_write(0xA8, 0x003E, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 3e
    pkt_size += fm_bop_write(0xA8, 0xFF6E, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 ff6e
    pkt_size += fm_bop_write(0xA8, 0x0087, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 87
    pkt_size += fm_bop_write(0xA8, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 0
    pkt_size += fm_bop_write(0xA8, 0xFEDC, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 fedc
    pkt_size += fm_bop_write(0xA8, 0x0015, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 15
    pkt_size += fm_bop_write(0xA8, 0x00CF, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 cf
    pkt_size += fm_bop_write(0xA8, 0xFF6B, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 ff6b
    pkt_size += fm_bop_write(0xA8, 0x0164, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 164
    pkt_size += fm_bop_write(0xA8, 0x002B, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 2b
    pkt_size += fm_bop_write(0xA8, 0xFF7E, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 ff7e
    pkt_size += fm_bop_write(0xA8, 0x0065, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 65
    pkt_size += fm_bop_udelay(10000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 10000
    pkt_size += fm_bop_write(0x9F, 0x0002, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9f 2
    pkt_size += fm_bop_write(0xA9, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a9 0
    pkt_size += fm_bop_write(0xA9, 0xFFEE, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a9 ffee
    pkt_size += fm_bop_write(0xA9, 0xFF81, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a9 ff81
    //XHC,2011/03/18 [wr A9 FFC3]
    pkt_size += fm_bop_write(0xA9, 0xFFC3, &tx_buf[pkt_size], tx_buf_size - pkt_size);
    pkt_size += fm_bop_write(0xA9, 0x0022, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a9 22
    pkt_size += fm_bop_write(0xA9, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a9 0
    pkt_size += fm_bop_write(0xA9, 0xFFCC, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a9 ffcc
    pkt_size += fm_bop_write(0xA9, 0x0023, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a9 23
    pkt_size += fm_bop_write(0xA9, 0xFFDA, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a9 ffda
    pkt_size += fm_bop_write(0xA9, 0xFFF7, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a9 fff7
    pkt_size += fm_bop_udelay(10, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 10
    pkt_size += fm_bop_write(0x9F, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9f 1
    pkt_size += fm_bop_write(0xD3, 0x250b, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr d3 250b
    pkt_size += fm_bop_write(0xBB, 0x2320, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr bb 2320
    pkt_size += fm_bop_write(0xD0, 0x02f8, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr d0 02f8
    pkt_size += fm_bop_write(0xEC, 0x019a, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ec 19a
    pkt_size += fm_bop_write(0xFE, 0x2140, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr fe 2140
    pkt_size += fm_bop_write(0x9F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9f 0
    // fm_rgf_rds    
    pkt_size += fm_bop_write(0x9F, 0x0003, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9f 3
    pkt_size += fm_bop_write(0xBD, 0x37EB, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr bd 37eb
    pkt_size += fm_bop_write(0xBC, 0x0808, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr bc 808
    pkt_size += fm_bop_write(0x9F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9f 0

    tx_buf[2] = (uint8_t)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (uint8_t)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

int mt6620_rx_digital_init_ack(unsigned char *tx_buf, int tx_buf_size)
{
    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_EVENT_PKT_TYPE;
    tx_buf[1] = FM_ENABLE_OPCODE;
    tx_buf[2] = 0x00;
    tx_buf[3] = 0x00;

    return 4;
}

// TBD for IC
int mt6620_powerdown(unsigned char *tx_buf, int tx_buf_size)
{
    int pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_ENABLE_OPCODE;
    pkt_size = 4;

//Digital Modem Power Down
    pkt_size += fm_bop_write(0x63, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 63 0
    pkt_size += fm_bop_modify(0x6E, 0xFFFC, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);// clear 0x6e[1:0], round1
    pkt_size += fm_bop_modify(0x6E, 0xFFFC, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);// clear 0x6e[1:0], round2
    pkt_size += fm_bop_modify(0x6E, 0xFFFC, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);// clear 0x6e[1:0], round3
    pkt_size += fm_bop_modify(0x6E, 0xFFFC, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);// clear 0x6e[1:0], round4
//ADPLL Power Off Sequence
    pkt_size += fm_bop_write(0x2A, 0x0022, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 2a 22
    pkt_size += fm_bop_write(0x1E, 0x0860, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1E 0860
    pkt_size += fm_bop_write(0x20, 0x0720, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 20 0720
    pkt_size += fm_bop_write(0x20, 0x2720, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 20 2720
//ANALOG/RF Power Off Sequence
    pkt_size += fm_bop_write(0x00, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 0 0
    pkt_size += fm_bop_write(0x01, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1 0
    pkt_size += fm_bop_write(0x04, 0x0141, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 4 0141
    pkt_size += fm_bop_write(0x09, 0x0964, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9 0964
    pkt_size += fm_bop_write(0x0C, 0x288C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c 288c
    pkt_size += fm_bop_write(0x26, 0x0004, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 26 0004
    pkt_size += fm_bop_write(0x3A, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3A 0000
    pkt_size += fm_bop_write(0x3B, 0x00C3, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3B 00C3
    pkt_size += fm_bop_write(0x3E, 0x3280, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3E 3280
    pkt_size += fm_bop_write(0x3F, 0x4E16, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3F 4E16
    pkt_size += fm_bop_write(0x41, 0x0004, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 41 0004
    //clear TX settings
    pkt_size += fm_bop_write(0x12, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 12 0000
    pkt_size += fm_bop_write(0x47, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 47 0000
    tx_buf[2] = (uint8_t)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (uint8_t)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

int mt6620_powerdown_ack(unsigned char *tx_buf, int tx_buf_size)
{
    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_EVENT_PKT_TYPE;
    tx_buf[1] = FM_ENABLE_OPCODE;
    tx_buf[2] = 0x00;
    tx_buf[3] = 0x00;

    return 4;
}

int mt6620_rampdown(unsigned char *tx_buf, int tx_buf_size)
{
    int pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_RAMPDOWN_OPCODE;
    pkt_size = 4;

    //XHC, 2011/04/06 ramp 125ms -> 34ms
    pkt_size += fm_bop_modify(0x9C, 0xFF87, 0x0068, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9c[3] = 1, ramp down
    pkt_size += fm_bop_write(0x9F, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9f 1
    pkt_size += fm_bop_write(0xC8, 0x0101, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c8 101
    pkt_size += fm_bop_write(0xDD, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr dd 0
    pkt_size += fm_bop_write(0xD8, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr d8 0
    pkt_size += fm_bop_write(0x9F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9f 0
    pkt_size += fm_bop_udelay(35000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 35000
    //disable interrupt before rampdown
    pkt_size += fm_bop_write(0x6B, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 6b 0000
    pkt_size += fm_bop_modify(0x63, 0xFFF0, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 63[3:0] = 0, ramp down
    pkt_size += fm_bop_rd_until(0x6f, 0x0001, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//Poll 6f[0] = b'0
    pkt_size += fm_bop_write(0x6B, 0x2100, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 6b 2100
    //enable interrupt after rampdown

    tx_buf[2] = (uint8_t)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (uint8_t)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

int mt6620_rampdown_tx(unsigned char *tx_buf, int tx_buf_size)
{
    int pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_RAMPDOWN_OPCODE;
    pkt_size = 4;

    pkt_size += fm_bop_write(0x3B, 0x0500, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3b 0500
    pkt_size += fm_bop_write(0x3A, 0x00FF, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3a ff
    pkt_size += fm_bop_write(0x65, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 65 1
    pkt_size += fm_bop_write(0x48, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 48 0
    
    tx_buf[2] = (uint8_t)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (uint8_t)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

int mt6620_rampdown_ack(unsigned char *tx_buf, int tx_buf_size)
{
    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_EVENT_PKT_TYPE;
    tx_buf[1] = FM_RAMPDOWN_OPCODE;
    tx_buf[2] = 0x00;
    tx_buf[3] = 0x00;

    return 4;
}

// freq: 760 ~ 1080, 100KHz unit, step 1
int mt6620_tune_1(unsigned char *tx_buf, int tx_buf_size, uint16_t freq)
{
    int pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    freq = (freq - 640) * 2;

    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_TUNE_OPCODE;
    pkt_size = 4;

    pkt_size += fm_bop_modify(0x0F, 0xFC00, freq, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set 0x0F[9:0] = 0x029e, => ((97.5 - 64) * 20)
    pkt_size += fm_bop_modify(0x63, 0xFFFF, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set 0x63[0] = 1

    tx_buf[2] = (uint8_t)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (uint8_t)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

// freq: 760 ~ 1080, 100KHz unit, step 2
int mt6620_tune_2(unsigned char *tx_buf, int tx_buf_size, uint16_t freq)
{
    int pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    freq = (freq - 640) * 2;

    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_TUNE_OPCODE;
    pkt_size = 4;

    pkt_size += fm_bop_modify(0x9C, 0xFFF7, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set 0x9C[3] = 0
  
    tx_buf[2] = (uint8_t)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (uint8_t)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

// freq: 760 ~ 1080, 100KHz unit, step 3
int mt6620_tune_3(unsigned char *tx_buf, int tx_buf_size, uint16_t freq)
{
    int pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    freq = (freq - 640) * 2;

    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_TUNE_OPCODE;
    pkt_size = 4;

    pkt_size += fm_bop_write(0x9F, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9F 1
    pkt_size += fm_bop_write(0xC8, 0x0232, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr C8 232
    pkt_size += fm_bop_write(0xDD, 0x8833, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr DD 8833
    pkt_size += fm_bop_write(0xD8, 0x00E8, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr D8 E8
    pkt_size += fm_bop_write(0x9F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9F 0
    
    tx_buf[2] = (uint8_t)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (uint8_t)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

int mt6620_fast_tune(unsigned char *tx_buf, int tx_buf_size, uint16_t freq)
{
    int pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    freq = (freq - 640) * 2;

    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_TUNE_OPCODE;
    pkt_size = 4;
    
    pkt_size += fm_bop_modify(0x63, 0xFFF0, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set 0x63[3:0] = 0
    pkt_size += fm_bop_modify(0x6F, 0xFFFE, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set 0x6F[0] = 0
    pkt_size += fm_bop_modify(0x0F, 0xFC00, freq, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set 0x0F[9:0] = 0x029e, => ((97.5 - 64) * 20)
    //disable interrupt before rampdown
    pkt_size += fm_bop_write(0x6B, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 6b 0000
    pkt_size += fm_bop_modify(0x63, 0xFFFE, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set 0x63[0] = 1
    pkt_size += fm_bop_rd_until(0x69, 0x0001, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//Poll 69[0] = b'1
    pkt_size += fm_bop_modify(0x69, 0xFFFE, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set 0x69[0] = 1
    pkt_size += fm_bop_write(0x6B, 0x2100, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 6b 2100
    //enable interrupt after rampdown
    
    tx_buf[2] = (uint8_t)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (uint8_t)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

// freq: 760 ~ 1080, 100KHz unit
int mt6620_tune_tx(unsigned char *tx_buf, int tx_buf_size, uint16_t freq)
{
    int pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    freq = (freq - 640) * 2;

    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_TUNE_OPCODE;
    pkt_size = 4;
    
    //XHC, 2011/04/20, ramp down before tune
    pkt_size += fm_bop_write(0x3B, 0x0500, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3b 0500
    pkt_size += fm_bop_write(0x3A, 0x00FF, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3a ff
    pkt_size += fm_bop_write(0x65, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 65 1
    pkt_size += fm_bop_write(0x48, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 48 0
    //XHC, 2011/04/14
    pkt_size += fm_bop_modify(0x41, 0xFFFE, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set 0x41[0] = 1
    //XHC, 2011/04/18
    pkt_size += fm_bop_modify(0x12, 0x7FFF, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set 0x12[15] = 0
    //XHC, 2011/04/22, clear RTC compensation info
    pkt_size += fm_bop_modify(0x47, 0x003F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set 0x47[15:6] = 0
    pkt_size += fm_bop_modify(0x0F, 0xFC00, freq, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set 0x0F[9:0] = freq
    pkt_size += fm_bop_write(0x26, 0x002C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 26 002C --> SCAL Related --> SCAL_EN and SCAL_GM_EN
    pkt_size += fm_bop_udelay(1000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 1000us
    pkt_size += fm_bop_write(0x26, 0x003C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 26 003C --> SCAL_BUF_EN
    pkt_size += fm_bop_udelay(1000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 1000us
    pkt_size += fm_bop_write(0x10, 0x1E8D, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 10 1e8d
    pkt_size += fm_bop_udelay(1000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 1000us
    pkt_size += fm_bop_write(0x10, 0x9E8D, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 10 9e8d
    pkt_size += fm_bop_udelay(10000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 10000us
    pkt_size += fm_bop_write(0x26, 0x0024, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 26 0024 --> Turn off SCAL gm and BUF
    pkt_size += fm_bop_write(0x65, 0x400F, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 65 400f
    pkt_size += fm_bop_write(0x48, 0x8000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 48 8000
    pkt_size += fm_bop_write(0x3B, 0x0420, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3b 0420
    pkt_size += fm_bop_write(0x3A, 0x01FF, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3a 1ff
    //XHC, 2011/04/14
    //pkt_size += fm_bop_udelay(10000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 10000us
    //pkt_size += fm_bop_modify(0x41, 0xFFFE, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set 0x41[0] = 0
    
    tx_buf[2] = (uint8_t)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (uint8_t)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

// freq: 760 ~ 1080, 100KHz unit
int mt6620_tune_txscan(unsigned char *tx_buf, int tx_buf_size, uint16_t freq)
{
    int pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    freq = (freq - 640) * 2;

    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_TUNE_OPCODE;
    pkt_size = 4;

	//rampdown
    //disable interrupt before rampdown
    pkt_size += fm_bop_write(0x6B, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 6b 0000
	pkt_size += fm_bop_modify(0x63, 0xFFF0, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 63[3:0] = 0, ramp down
    pkt_size += fm_bop_rd_until(0x6f, 0x0001, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//Poll 6f[0] = b'0
    pkt_size += fm_bop_modify(0x9C, 0xFFFF, 0x0008, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9c[3] = 1, ramp down
    pkt_size += fm_bop_write(0x6B, 0x2100, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 6b 2100
    //enable interrupt after rampdown

	//set desired channel
    pkt_size += fm_bop_modify(0x0F, 0xFC00, freq, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set 0x0F[9:0] = 0x029e, => ((97.5 - 64) * 20)

	//only for short antennal tune
#ifdef MT6620_FM_USE_SHORT_ANTENNA
	pkt_size += fm_bop_write(0x28, 0x3800, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 28 3800
	pkt_size += fm_bop_write(0x03, 0x90F0, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3 90f0
	pkt_size += fm_bop_write(0x2E, 0x0028, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 2e 28
	pkt_size += fm_bop_write(0x2F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 2f 0
	pkt_size += fm_bop_write(0x26, 0x003C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 26 3c
	pkt_size += fm_bop_write(0x2E, 0x002C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 2e 2c
	pkt_size += fm_bop_udelay(10000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wait 10ms
	pkt_size += fm_bop_write(0x26, 0x0024, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 26 24
	pkt_size += fm_bop_write(0x28, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 28 00
#endif

	//only for E1
#ifdef MT6620E1
    pkt_size += fm_bop_write(0x9F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9F 0
    pkt_size += fm_bop_write(0xAF, 0x2210, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr AF 2210
#endif

	//mask STC_DONE interrupt, 6a(D0) 0 
	//pkt_size += fm_bop_modify(0x6A, 0xFFFE,  0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);

	//enable hardware are controlled tuning sequence
    pkt_size += fm_bop_modify(0x63, 0xFFFF,  0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set rgf_tune (63H D0) =1

	//check STC_DONE interrupt status flag
	//pkt_size += fm_bop_rd_until(0x69, 0x0001, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//Poll fm_intr_stc_done (69H D0) = 1

	//write 1 clear the STC_DONE status flag
    //pkt_size += fm_bop_modify(0x69, 0xFFFF, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set stc_done (6FH D0) =1

	//unmask STC_DONE interrupt, 6a(D0) 1
	//pkt_size += fm_bop_modify(0x6A, 0xFFFF,  0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);

	//only for E1
#ifdef MT6620E1
	//pkt_size += fm_bop_write(0xAF, 0x7710, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr AF 7710
#endif

    tx_buf[2] = (uint8_t)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (uint8_t)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

int mt6620_tune_ack(unsigned char *tx_buf, int tx_buf_size)
{
    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_EVENT_PKT_TYPE;
    tx_buf[1] = FM_TUNE_OPCODE;
    tx_buf[2] = 0x00;
    tx_buf[3] = 0x00;

    return 4;
}

// TBD for IC
int mt6620_seek_1(unsigned char *tx_buf, int tx_buf_size, uint16_t seekdir, uint16_t space, uint16_t max_freq, uint16_t min_freq)
{
    int pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_SEEK_OPCODE;
    pkt_size = 4;

    //A1 Program seek direction, 0x66[10]: 0=seek up, 1=seek down 
    if(seekdir == MT6620_FM_SEEK_UP){
        pkt_size += fm_bop_modify(0x66, 0xFBFF, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//0x66[10] = 0, seek up
    }else{
        pkt_size += fm_bop_modify(0x66, 0xFFFF, 0x0400, &tx_buf[pkt_size], tx_buf_size - pkt_size);//0x66[10] = 1, seek down
    }
    //0x66[11] 0=no wrarp, 1=wrap
    pkt_size += fm_bop_modify(0x66, 0xFFFF, 0x0800, &tx_buf[pkt_size], tx_buf_size - pkt_size);//0x66[11] = 1, wrap
    //A2 Program scan channel spacing, 0x66[14:12] step 50KHz:001/100KHz:010/200KHz:100
    if(space == MT6620_FM_SPACE_50K){
        pkt_size += fm_bop_modify(0x66, 0x8FFF, 0x1000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//clear 0x66[14:12] then 0x66[14:12]=001
    }else if(space == MT6620_FM_SPACE_100K){
        pkt_size += fm_bop_modify(0x66, 0x8FFF, 0x2000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//clear 0x66[14:12] then 0x66[14:12]=010
    }else if(space == MT6620_FM_SPACE_200K){
        pkt_size += fm_bop_modify(0x66, 0x8FFF, 0x4000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//clear 0x66[14:12] then 0x66[14:12]=100
    }

    //0x66[9:0] freq upper bound
    max_freq = (max_freq - 640) * 2;
    pkt_size += fm_bop_modify(0x66, 0xFC00, max_freq, &tx_buf[pkt_size], tx_buf_size - pkt_size);

    //0x67[9:0] freq lower bound
    min_freq = (min_freq - 640) * 2;
    pkt_size += fm_bop_modify(0x67, 0xFC00, min_freq, &tx_buf[pkt_size], tx_buf_size - pkt_size);
    //A3 Enable hardware controlled seeking sequence
    pkt_size += fm_bop_modify(0x63, 0xFFFF, 0x0002, &tx_buf[pkt_size], tx_buf_size - pkt_size);//0x63[1] = 1

    tx_buf[2] = (uint8_t)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (uint8_t)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

int mt6620_seek_2(unsigned char *tx_buf, int tx_buf_size, uint16_t seekdir, uint16_t space, uint16_t max_freq, uint16_t min_freq)
{
    int pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_SEEK_OPCODE;
    pkt_size = 4;
    
    //A10 Set softmute to normal mode
    pkt_size += fm_bop_write(0x9F, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9F 1
    pkt_size += fm_bop_write(0xC8, 0x0232, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr C8 232
    pkt_size += fm_bop_write(0xDD, 0x8833, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr DD 8833
    pkt_size += fm_bop_write(0xD8, 0x00E8, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr D8 E8
    pkt_size += fm_bop_write(0x9F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9F 0

    tx_buf[2] = (uint8_t)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (uint8_t)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

int mt6620_seek_ack(unsigned char *tx_buf, int tx_buf_size)
{
    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_EVENT_PKT_TYPE;
    tx_buf[1] = FM_SEEK_OPCODE;
    tx_buf[2] = 0x00;
    tx_buf[3] = 0x00;

    return 4;
}

int mt6620_scan_1(unsigned char *tx_buf, int tx_buf_size, uint16_t scandir, uint16_t space, uint16_t max_freq, uint16_t min_freq)
{
    int pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_SCAN_OPCODE;
    pkt_size = 4;

    //A1 Program scan direction, 0x66[10]: 0=seek up, 1=seek down 
    if(scandir == MT6620_FM_SCAN_UP){
        pkt_size += fm_bop_modify(0x66, 0xFBFF, 0x0000, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//0x66[10] = 0, seek up
    }else{
        pkt_size += fm_bop_modify(0x66, 0xFFFF, 0x0400, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//0x66[10] = 1, seek down
    }
    //0x66[11] 0=no wrarp, 1=wrap
    pkt_size += fm_bop_modify(0x66, 0xFFFF, 0x0800, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//0x66[11] = 1, wrap
    //A2 Program scan channel spacing, 0x66[14:12] step 50KHz:001/100KHz:010/200KHz:100
    if(space == MT6620_FM_SPACE_50K){
        pkt_size += fm_bop_modify(0x66, 0x8FFF, 0x1000, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//clear 0x66[14:12] then 0x66[14:12]=001
    }else if(space == MT6620_FM_SPACE_100K){
        pkt_size += fm_bop_modify(0x66, 0x8FFF, 0x2000, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//clear 0x66[14:12] then 0x66[14:12]=010
    }else if(space == MT6620_FM_SPACE_200K){
        pkt_size += fm_bop_modify(0x66, 0x8FFF, 0x4000, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//clear 0x66[14:12] then 0x66[14:12]=100
    }

    //0x66[9:0] freq upper bound
    max_freq = (max_freq - 640) * 2;
    pkt_size += fm_bop_modify(0x66, 0xFC00, max_freq, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);

    //0x67[9:0] freq lower bound
    min_freq = (min_freq - 640) * 2;
    pkt_size += fm_bop_modify(0x67, 0xFC00, min_freq, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);

    //A3 Enable hardware controlled scanning sequence
    pkt_size += fm_bop_modify(0x63, 0xFFFF, 0x0004, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//0x63[2] = 1

    tx_buf[2] = (uint8_t)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (uint8_t)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

int mt6620_scan_2(unsigned char *tx_buf, int tx_buf_size, uint16_t scandir, uint16_t space, uint16_t max_freq, uint16_t min_freq)
{
    int pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_SCAN_OPCODE;
    pkt_size = 4;
    
    //A10 Set softmute to normal mode
    pkt_size += fm_bop_write(0x9F, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9F 1
    pkt_size += fm_bop_write(0xC8, 0x0232, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr C8 232
    pkt_size += fm_bop_write(0xDD, 0x8833, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr DD 8833
    pkt_size += fm_bop_write(0xD8, 0x00E8, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr D8 E8
    pkt_size += fm_bop_write(0x9F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9F 0

    tx_buf[2] = (uint8_t)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (uint8_t)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

int mt6620_com(unsigned char *tx_buf, int tx_buf_size, int opcode, void* data)
{
    int pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_ENABLE_OPCODE; //FM_ENABLE_OPCODE can be used by other user
    pkt_size = 4;

    switch(opcode){
        case FM_COM_CMD_TEST:
            //disable mute
            pkt_size += fm_bop_modify(0x9C, 0xFFF7, 0x0000, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//0x9c[3] =  0
            break;
        case FM_COM_CMD_SCAN:
            //A7 Set to read page 1
            pkt_size += fm_bop_modify(0x9F, 0xFFFC, 0x0001, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//0x9F D0~D1 =  01 
            //A8 Read back channel scan bit_map information
            //A9 disable mute
            pkt_size += fm_bop_modify(0x9C, 0xFFF7, 0x0000, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//0x9c[3] =  0
            pkt_size += fm_bop_udelay(35000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 35ms
            //A10 Set softmute to normal mode
            pkt_size += fm_bop_write(0x9F, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9F 1
            pkt_size += fm_bop_write(0xC8, 0x0232, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr C8 232
            pkt_size += fm_bop_write(0xDD, 0x8833, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr DD 8833
            pkt_size += fm_bop_write(0xD8, 0x00E8, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr D8 E8
            pkt_size += fm_bop_write(0x9F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9F 0
            break;
        case FM_COM_CMD_SEEK:
            //A7 Set to read page 1
            pkt_size += fm_bop_modify(0x9F, 0xFFFC, 0x0001, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//0x9F D0~D1 =  01
            //A8 Read back channel scan bit_map information
            //A9 disable mute
            pkt_size += fm_bop_modify(0x9C, 0xFFF7, 0x0000, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//0x9c[3] =  0
            pkt_size += fm_bop_udelay(35000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 35ms
            //A10 Set softmute to normal mode
            pkt_size += fm_bop_write(0x9F, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9F 1
            pkt_size += fm_bop_write(0xC8, 0x0232, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr C8 232
            pkt_size += fm_bop_write(0xDD, 0x8833, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr DD 8833
            pkt_size += fm_bop_write(0xD8, 0x00E8, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr D8 E8
            pkt_size += fm_bop_write(0x9F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9F 0
            break;
        default:
            return -1;
            break;
    }

    tx_buf[2] = (uint8_t)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (uint8_t)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

int mt6620_scan_ack(unsigned char *tx_buf, int tx_buf_size)
{
    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_EVENT_PKT_TYPE;
    tx_buf[1] = FM_SCAN_OPCODE;
    tx_buf[2] = 0x00;
    tx_buf[3] = 0x00;

    return 4;
}

int mt6620_rds_rx_enable(unsigned char *tx_buf, int tx_buf_size) //IC version
{
    int pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = RDS_RX_ENABLE_OPCODE;
    pkt_size = 4;

    pkt_size += fm_bop_write(0x9F, 0x0003, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 0x9f 3
    pkt_size += fm_bop_write(0xCB, 0xE016, &tx_buf[pkt_size], tx_buf_size - pkt_size);//Wr 0xcb e016
    pkt_size += fm_bop_write(0x9F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 0x9f 0
    pkt_size += fm_bop_write(0x63, 0x0491, &tx_buf[pkt_size], tx_buf_size - pkt_size);//Wr 0x63 491
    pkt_size += fm_bop_modify(0x6B, 0xFFFF, 0x2000, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//Wr 0x6b [13] = 1

    tx_buf[2] = (uint8_t)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (uint8_t)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

int mt6620_rds_rx_enable_ack(unsigned char *tx_buf, int tx_buf_size) //IC version
{
    int pkt_size = 0;

	pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_EVENT_PKT_TYPE;
    tx_buf[1] = RDS_RX_ENABLE_OPCODE;
    tx_buf[2] = 0x00;
    tx_buf[3] = 0x00;

    return 4;
}

int mt6620_rds_rx_disable(unsigned char *tx_buf, int tx_buf_size) //IC version
{
    int pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = RDS_RX_ENABLE_OPCODE;
    pkt_size = 4;

    pkt_size += fm_bop_modify(0x6B, 0xDFFF, 0x0000, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//Wr 0x6b [13] = 0
    pkt_size += fm_bop_write(0x63, 0x0481, &tx_buf[pkt_size], tx_buf_size - pkt_size);//Wr 0x63 481


    tx_buf[2] = (uint8_t)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (uint8_t)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

int mt6620_rds_rx_disable_ack(unsigned char *tx_buf, int tx_buf_size) //IC version
{
    int pkt_size = 0;

	pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_EVENT_PKT_TYPE;
    tx_buf[1] = RDS_RX_ENABLE_OPCODE;
    tx_buf[2] = 0x00;
    tx_buf[3] = 0x00;

    return 4;
}

// TBD for IC
int mt6620_rds_tx(unsigned char *tx_buf, int tx_buf_size, uint16_t pi, uint16_t *ps, uint16_t *other_rds, uint8_t other_rds_cnt)
{
    int pkt_size = 0;
    int i;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }

    if(other_rds_cnt > 29){
        return (-2);
    }

    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = RDS_TX_OPCODE;
    pkt_size = 4;

    //RDS Tx config
    pkt_size += fm_bop_modify(0x65, 0xFFFF, 0x0010, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//wr 65[4] = b'1, enable RDS Tx
    pkt_size += fm_bop_write(0x9F, 0x0003, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9F 3
    pkt_size += fm_bop_write(0xA2, 0x0005, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a2 5, repeat + PI_reg mode
    pkt_size += fm_bop_write(0xA1, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a1 0
    //pkt_size += fm_bop_write(0xA0, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a0 1
    pkt_size += fm_bop_write(0xA4, pi, &tx_buf[pkt_size], tx_buf_size - pkt_size);//write PI to PI_reg
    //program PS buf
    pkt_size += fm_bop_rd_until(0xAA, 0x0001, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);// rd until aa[0] = b'0, ptr in normal buf
    //program normal buf, workaround that PS buf can't work while normal buf is empty
    for(i = 0; i < 12; i++)
    {
        pkt_size += fm_bop_write(0xA8, ps[i], &tx_buf[pkt_size], tx_buf_size - pkt_size);//a8 = RDS Tx data
    }
    pkt_size += fm_bop_modify(0xA2, 0xFFFF, 0x0002, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//wr a2[1] = b'1, mem_addr mode
    for(i = 0; i < 12; i++)
    {
        pkt_size += fm_bop_write(0xA7, (0x0063+i), &tx_buf[pkt_size], tx_buf_size - pkt_size);//a7 = mem_addr
        pkt_size += fm_bop_write(0xA8, ps[i], &tx_buf[pkt_size], tx_buf_size - pkt_size);//a8 = RDS Tx data
    }
    pkt_size += fm_bop_modify(0xA2, 0xFFFF, 0x0010, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//wr a2[4] = b'1, switch to ps buf
    //program normal buf
    pkt_size += fm_bop_rd_until(0xAA, 0x0001, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);// rd until aa[0] = b'1, ptr in ps buf
    pkt_size += fm_bop_modify(0xA2, 0xFFFD, 0x0000, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//wr a2[1] = b'0, h/w addr mode
    for(i = 0; i < 12; i++)
    {
        pkt_size += fm_bop_write(0xA8, ps[i], &tx_buf[pkt_size], tx_buf_size - pkt_size);//a8 = RDS Tx data
    }
    for(i = 0; i < (other_rds_cnt * 3); i++)
    {
        pkt_size += fm_bop_write(0xA8, other_rds[i], &tx_buf[pkt_size], tx_buf_size - pkt_size);//a8 = RDS Tx data
    }
    pkt_size += fm_bop_modify(0xA2, 0xFFEF, 0x0000, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//wr a2[4] = b'0, switch to normal buf
    pkt_size += fm_bop_write(0x9F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9F 0

    tx_buf[2] = (uint8_t)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (uint8_t)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

int mt6620_rds_tx_ack(unsigned char *tx_buf, int tx_buf_size)
{
    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_EVENT_PKT_TYPE;
    tx_buf[1] = RDS_TX_OPCODE;
    tx_buf[2] = 0x00;
    tx_buf[3] = 0x00;

    return 4;
}

int mt6620_get_reg(unsigned char *tx_buf, int tx_buf_size, uint8_t addr)
{
    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FSPI_READ_OPCODE;
    tx_buf[2] = 0x01;
    tx_buf[3] = 0x00;
    tx_buf[4] = addr;

    return 5;
}

int mt6620_set_reg(unsigned char *tx_buf, int tx_buf_size, uint8_t addr, uint16_t value)
{
    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FSPI_WRITE_OPCODE;
    tx_buf[2] = 0x03;
    tx_buf[3] = 0x00;
    tx_buf[4] = addr;
    tx_buf[5] = (uint8_t)((value) & 0x00FF);
    tx_buf[6] = (uint8_t)((value >> 8) & 0x00FF);

    return 7;
}

int mt6620_set_reg_ack(unsigned char *tx_buf, int tx_buf_size)
{
    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_EVENT_PKT_TYPE;
    tx_buf[1] = FSPI_WRITE_OPCODE;
    tx_buf[2] = 0x00;
    tx_buf[3] = 0x00;

    return 4;
}

// TBD for IC
int mt6620_off_2_tx_shortANA(unsigned char *tx_buf, int tx_buf_size)
{
    int pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_ENABLE_OPCODE;
    pkt_size = 4;

    pkt_size += fm_bop_write(0x01, 0x4A00, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1 4A00  --> Turn on Central Bias + FC
    pkt_size += fm_bop_udelay(30, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 30 
    pkt_size += fm_bop_write(0x01, 0x6A00, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1 6A00  --> Turn off FC
    pkt_size += fm_bop_udelay(50, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 50
    pkt_size += fm_bop_write(0x02, 0x299C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 2 299C  --> Set the LDOs Output Voltages
    pkt_size += fm_bop_write(0x01, 0x6B82, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1 6B82  --> Turn on DCO, RX and ADDA LDO
    pkt_size += fm_bop_write(0x3B, 0x0500, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3B 0500 --> Turn on PA LDO --> LDO PA = 2.5V [0000 010x xxx0 000] {xxxx} - 0001 = 2.5V
    pkt_size += fm_bop_write(0x04, 0x0548, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 4 0548  --> Set the RX LDO to Low Power Mode + TR Switch Off
    pkt_size += fm_bop_write(0x37, 0x2000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 37 2000 --> Set the Short Antenna Bias
    pkt_size += fm_bop_write(0x42, 0xC002, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 42 C002 --> Set the Short Antenna Bias
    pkt_size += fm_bop_write(0x0A, 0x0060, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr A 0060  --> Update AFCDAC LPF Setting
    pkt_size += fm_bop_write(0x0E, 0x0040, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr E 0040  --> Update SX_VDC_CBANK
    pkt_size += fm_bop_write(0x0C, 0xA88C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c A88C
    pkt_size += fm_bop_write(0x10, 0x1E8D, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 10 1e8d
    pkt_size += fm_bop_write(0x27, 0x0005, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 27 0005 --> Update ACAL and Enable RMS_DET    
    pkt_size += fm_bop_write(0x11, 0x07D8, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 11 07D8 --> Set VCO_DIVRST_N = 0 
    pkt_size += fm_bop_write(0x41, 0x0003, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 41 0003 --> Set TX_FVCO_EN = 1 and FVCO_SEL=1
    pkt_size += fm_bop_write(0x08, 0x25B8, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 8 25b8  --> ADC = TX Mode (AU_ADC)
    pkt_size += fm_bop_write(0x09, 0x2964, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9 2964  --> ADC DWA ON
    pkt_size += fm_bop_write(0x3F, 0xAD86, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3F AD86 --> TX DAc RX_TX_SEL = TX MOde 
    pkt_size += fm_bop_write(0x3A, 0x01EF, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3A 01EF --> Turn on TX Chain [PA+D2S+HRM+AUPGA+TXDAC+LODIV]
    pkt_size += fm_bop_write(0x3E, 0x3181, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3E 3181 --> TX LPF EN + CSEL from RCCAL
    pkt_size += fm_bop_write(0x00, 0x0100, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 0  0100 --> Turn on SDADC
    pkt_size += fm_bop_write(0x37, 0x2000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 37 2000 --> Control Signal for DAC_CK output clock gate
    //FM ADPLL Power Up
    pkt_size += fm_bop_write(0x25, 0x040F, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 25 040f --> Turn off DIG_CK_EN
    pkt_size += fm_bop_write(0x20, 0x2720, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 20 2720 --> Turn on ADPLL  2320 CW32 
    //XHC,2011/03/18, [wr 22 9980-> 6680]
    pkt_size += fm_bop_write(0x22, 0x6680, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 22 9980 --> Update DLF Gain
    pkt_size += fm_bop_write(0x25, 0x080F, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 25 080f --> Update I_CODE_CCAL + ADC_CK_EN + 32KCLKPLL_EN 0803 CW37
    pkt_size += fm_bop_write(0x1E, 0x0863, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1e 0863 --> Turn on DCO + CAL_COARSE_EN
    pkt_size += fm_bop_udelay(5000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 5000
    pkt_size += fm_bop_write(0x1E, 0x0865, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1e 0865 --> Change the CAL_COARSE to CAL_FINE
    pkt_size += fm_bop_udelay(5000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 5000
    pkt_size += fm_bop_write(0x1E, 0x0871, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1e 0871 --> Off the CAL_COARSE and CAL_FINE + Turn on PLL
    pkt_size += fm_bop_udelay(100000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 100000
    pkt_size += fm_bop_write(0x2A, 0x1026, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 2a 1022 --> Enable TOP_CG
    //FM RC Calibration
    pkt_size += fm_bop_write(0x00, 0x0080, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 0 0080 --> RCCAL
    pkt_size += fm_bop_udelay(1, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 1000
    pkt_size += fm_bop_write(0x1B, 0x0094, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1b 0094 --> Update RCCAL Target Count
    pkt_size += fm_bop_write(0x1B, 0x0095, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1b 0095 --> Start RCCAL
    pkt_size += fm_bop_udelay(200, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 1000
    pkt_size += fm_bop_write(0x1B, 0x0094, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1b 0094 --> Off RCCAL
    pkt_size += fm_bop_write(0x00, 0x0100, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 0 0100 --> Turn off RCCAL Analog Block
    //FM VCO Enable
    pkt_size += fm_bop_write(0x01, 0x6B8A, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1 6B8A --> Turn on VCO LDO
    pkt_size += fm_bop_udelay(1, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 1000
    pkt_size += fm_bop_write(0x00, 0xC100, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 0 C100 --> Turn on VCO, AFCDAC
    pkt_size += fm_bop_write(0x0C, 0xB88C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c B88c --> Turn on Const gm
    pkt_size += fm_bop_udelay(3000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 3000
    pkt_size += fm_bop_write(0x3A, 0x01FF, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3A 01FF --> Enable TX Divider
    //Short Antenna Setting
    pkt_size += fm_bop_write(0x42, 0xF002, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 42 F002 --> MSB of HRM_Gain <5> ****>> For Max Pout 
    pkt_size += fm_bop_write(0x3C, 0xABE9, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3C ABE9 --> Max HRM Gain<4:0> - xxxxx [1010 10xx xxx0 1001]
    pkt_size += fm_bop_write(0x3D, 0x027E, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3D 027E --> Max PA Gain<4:0> - [0000 0010 0xxx xx10]
    pkt_size += fm_bop_write(0x33, 0x0008, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 33 0008 --> Use old VCO calibration routine to save calibration time
    pkt_size += fm_bop_write(0x28, 0xFFFF, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 28 FFFF
    pkt_size += fm_bop_write(0x2E, 0x0020, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 2E 0020 --> Turn on SCAL_HWTRIG_DIS --> VCO CAL and SCAL are done separately
    pkt_size += fm_bop_write(0x2F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 2F 0000 --> Disable Capbank manual enter mode [4A40 previously]
    pkt_size += fm_bop_write(0x44, 0x006E, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 44 6e
    pkt_size += fm_bop_write(0x46, 0xDC22, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 46 DC22
    pkt_size += fm_bop_write(0x49, 0x0080, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 49 80
    pkt_size += fm_bop_write(0x4A, 0x0004, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 4A 4
    pkt_size += fm_bop_write(0x4B, 0x0040, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 4B 40

    tx_buf[2] = (uint8_t)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (uint8_t)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

int mt6620_off_2_tx_shortANA_ack(unsigned char *tx_buf, int tx_buf_size)
{
    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_EVENT_PKT_TYPE;
    tx_buf[1] = FM_ENABLE_OPCODE;
    tx_buf[2] = 0x00;
    tx_buf[3] = 0x00;

    return 4;
}

// TBD for IC
int mt6620_dig_init(unsigned char *tx_buf, int tx_buf_size)
{
    int pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_ENABLE_OPCODE;
    pkt_size = 4;

//fm_rgf_maincon
    pkt_size += fm_bop_write(0x64, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 64 0
    pkt_size += fm_bop_write(0x65, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 65 1
    pkt_size += fm_bop_write(0x68, 0xE100, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 68 E100
    pkt_size += fm_bop_udelay(10000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 10000   
    pkt_size += fm_bop_write(0x68, 0xE000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 68 E000
//fm_rgf_dac
    pkt_size += fm_bop_write(0x9F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9F 0
    pkt_size += fm_bop_write(0x9E, 0x001C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9E 1C
    pkt_size += fm_bop_write(0x9C, 0xA540, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9c A540
//fm_rgf_front
    pkt_size += fm_bop_write(0x9F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9f 0 --> start rgf_front
    pkt_size += fm_bop_write(0xB8, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr b8 1
    pkt_size += fm_bop_write(0xAB, 0x39B6, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ab 39b6
    pkt_size += fm_bop_write(0xAC, 0x3C3E, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ac 3c3e
    pkt_size += fm_bop_write(0xAD, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ad 0 
    pkt_size += fm_bop_write(0xAE, 0x03C2, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ae 3c2
    pkt_size += fm_bop_write(0xAF, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr af 1
    pkt_size += fm_bop_write(0xB1, 0x623D, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr b1 623d 
    pkt_size += fm_bop_write(0xF4, 0x0020, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr f4 20
    pkt_size += fm_bop_write(0xF5, 0xBF16, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr f5 bf16
    pkt_size += fm_bop_write(0xB9, 0x0050, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr b9 0050
    pkt_size += fm_bop_write(0xBA, 0x00C3, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ba 00c3
    pkt_size += fm_bop_write(0xBB, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr bb 0
    pkt_size += fm_bop_write(0xBC, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr bc 0
    pkt_size += fm_bop_write(0xBD, 0x0056, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr bd 56 
    pkt_size += fm_bop_write(0xBE, 0x0089, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr be 89
    pkt_size += fm_bop_write(0xBF, 0x004C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr bf 4c
    pkt_size += fm_bop_write(0xC0, 0x0171, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c0 171
    pkt_size += fm_bop_write(0xC1, 0x002B, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c1 2b
    pkt_size += fm_bop_write(0xC2, 0x001F, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c2 1f
    pkt_size += fm_bop_write(0xC3, 0x0066, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c3 66
    pkt_size += fm_bop_write(0xC4, 0x00F6, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c4 f6
    pkt_size += fm_bop_write(0xC5, 0x0066, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c5 66
    pkt_size += fm_bop_write(0xC6, 0x001F, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c6 1f
    pkt_size += fm_bop_write(0xC7, 0x0007, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c7 7 
    pkt_size += fm_bop_write(0xFE, 0x0039, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr fe 39
    pkt_size += fm_bop_write(0xFF, 0x3907, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ff 3907
//fm_rgf_fmx
    pkt_size += fm_bop_write(0x9F, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9f 1 --> start rgf_outer
    pkt_size += fm_bop_write(0xC0, 0x076C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c0 76c
    pkt_size += fm_bop_write(0xB7, 0x0004, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr b7 4
    pkt_size += fm_bop_write(0xD8, 0x006A, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr d8 6a
    pkt_size += fm_bop_write(0xC8, 0x2828, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c8 2828
    pkt_size += fm_bop_write(0xCE, 0x0090, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ce 90
    pkt_size += fm_bop_write(0xFE, 0x000F, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr fe f
    pkt_size += fm_bop_write(0xA2, 0x0100, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a2 100
    pkt_size += fm_bop_write(0xA3, 0x0111, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a3 111
    pkt_size += fm_bop_write(0xA4, 0x0122, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a4 122
    pkt_size += fm_bop_write(0xA5, 0x0135, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a5 135
    pkt_size += fm_bop_write(0xA6, 0x0149, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a6 149
    pkt_size += fm_bop_write(0xA7, 0x015E, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a7 15e
    pkt_size += fm_bop_write(0xDB, 0x0174, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr db 174
    pkt_size += fm_bop_write(0xDC, 0x018D, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr dc 18d
    pkt_size += fm_bop_write(0xC9, 0x01A6, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c9 1a6
    pkt_size += fm_bop_write(0xCA, 0x01C1, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ca 1c1
    pkt_size += fm_bop_write(0xCB, 0x01DE, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr cb 1de
    pkt_size += fm_bop_write(0xCC, 0x01FD, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr cc 1fd
    pkt_size += fm_bop_write(0xD4, 0x2657, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr d4 2657
    pkt_size += fm_bop_write(0xA0, 0x85B2, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a0 85b2
    pkt_size += fm_bop_write(0x9F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9f 0

    tx_buf[2] = (uint8_t)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (uint8_t)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

int mt6620_dig_init_ack(unsigned char *tx_buf, int tx_buf_size)
{
    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_EVENT_PKT_TYPE;
    tx_buf[1] = FM_ENABLE_OPCODE;
    tx_buf[2] = 0x00;
    tx_buf[3] = 0x00;

    return 4;
}

// TBD for IC
int mt6620_ant_switch(unsigned char *tx_buf, int tx_buf_size, int dir)    // dir: 0=long2short / 1=short2long
{
    int pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_ENABLE_OPCODE;
    pkt_size = 4;

    if(dir == 0){
        // long2short
        pkt_size += fm_bop_write(0x04, 0x0145, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 4 0145
        pkt_size += fm_bop_write(0x05, 0x00FF, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 5 00FF
        pkt_size += fm_bop_write(0x26, 0x0024, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 26 0024
        pkt_size += fm_bop_write(0x2E, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 2E 0000
    }else{
        // short2long
        pkt_size += fm_bop_write(0x04, 0x0142, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 4 0142
        pkt_size += fm_bop_write(0x05, 0x00E7, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 5 00E7
        pkt_size += fm_bop_write(0x26, 0x0004, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 26 0004
        pkt_size += fm_bop_write(0x2E, 0x0008, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 2E 0008
    }

    tx_buf[2] = (uint8_t)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (uint8_t)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

int mt6620_ant_switch_ack(unsigned char *tx_buf, int tx_buf_size)
{
    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_EVENT_PKT_TYPE;
    tx_buf[1] = FM_ENABLE_OPCODE;
    tx_buf[2] = 0x00;
    tx_buf[3] = 0x00;

    return 4;
}


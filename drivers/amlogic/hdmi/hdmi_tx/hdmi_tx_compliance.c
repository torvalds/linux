/*
 * Amlogic Meson HDMI Transmitter Driver
 * hdmitx driver-----------HDMI_TX
 * Copyright (C) 2013 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/major.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <asm/delay.h>
#include <mach/am_regs.h>
#include <mach/clock.h>
#include <mach/power_gate.h>
#include <linux/clk.h>
#include <mach/clock.h>
#include <linux/amlogic/vout/vinfo.h>
#include <linux/amlogic/vout/enc_clk_config.h>

#include "hdmi_info_global.h"
#include "hdmi_tx_module.h"
#include "hdmi_tx_compliance.h"
#include "hdmi_tx_cec.h"
#include "hdmi_tx_hdcp.h"
#include "hw/hdmi_tx_reg.h"

// Note: 
// set P_HHI_VID_PLL_CNTL as 0x43e, get better clock performance
// while as 0x21ef, get better clock precision
// 24 * 62 = 1488
// 24 / 8 * 495 = 1485

static void hdmitx_get_clk_better_performance(hdmitx_dev_t* hdmitx_device)
{
    if((aml_read_reg32(P_HHI_VID_PLL_CNTL) & 0x3fff ) == 0x21ef) {
        aml_set_reg32_bits(P_HHI_VID_PLL_CNTL, 0x43e, 0, 15);
    }
}

static void hdmitx_reset_audio_n(hdmitx_dev_t* hdmitx_device)
{
    static int rewrite_flag = 0;
    unsigned int audio_N_para = 6144;
    switch(hdmitx_device->cur_VIC) {
    case HDMI_480p60:
    case HDMI_480p60_16x9:
    case HDMI_576p50:
    case HDMI_576p50_16x9:
    case HDMI_480i60:
    case HDMI_480i60_16x9:
    case HDMI_576i50:
    case HDMI_576i50_16x9:
        switch(hdmitx_device->cur_audio_param.sample_rate){
        case FS_44K1:
            audio_N_para = 6272 * 3;
            rewrite_flag = 1;
            break;
        case FS_48K:
            audio_N_para = 6144 * 3;
            rewrite_flag = 1;
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
    if(rewrite_flag) {
        hdmi_wr_reg(TX_SYS1_ACR_N_0, (audio_N_para&0xff)); // N[7:0]
        hdmi_wr_reg(TX_SYS1_ACR_N_1, (audio_N_para>>8)&0xff); // N[15:8]
        hdmi_wr_reg(TX_SYS1_ACR_N_2, hdmi_rd_reg(TX_SYS1_ACR_N_2) | ((audio_N_para>>16)&0xf)); // N[19:16]
    }
}

// a sony special tv edid hash value: "acc0df36f1e523a2e02cfd54514732513e3a4351"
// got the first 4 bytes
static unsigned int SONY_EDID_HASH = 0xacc0df36;
static int edid_hash_compare(unsigned char *dat, unsigned int SPECIAL)
{
    int ret = 0;

    if((dat[0] == ((SPECIAL >> 24)&0xff)) && (dat[1] == ((SPECIAL >> 16)&0xff)) && (dat[2] == ((SPECIAL >> 8)&0xff)) && (dat[3] == (SPECIAL & 0xff)))
        ret = 1;
    return ret;
}

void hdmitx_special_handler_audio(hdmitx_dev_t* hdmitx_device)
{
    if(edid_hash_compare(&hdmitx_device->EDID_hash[0], SONY_EDID_HASH)) {
        hdmitx_reset_audio_n(hdmitx_device);
    }
}

void hdmitx_special_handler_video(hdmitx_dev_t* hdmitx_device)
{
    if(strncmp(hdmitx_device->RXCap.ReceiverBrandName, HDMI_RX_VIEWSONIC, strlen(HDMI_RX_VIEWSONIC)) == 0) {
        if(strncmp(hdmitx_device->RXCap.ReceiverProductName, HDMI_RX_VIEWSONIC_MODEL, strlen(HDMI_RX_VIEWSONIC_MODEL)) == 0) {
            hdmitx_get_clk_better_performance(hdmitx_device);
        }
    }
}


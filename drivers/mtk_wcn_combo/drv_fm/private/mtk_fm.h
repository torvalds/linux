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

#ifndef __MTK_FM_H__
#define __MTK_FM_H__

#include <linux/types.h>

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

//#define HiSideTableSize 1
#define FM_TX_PWR_CTRL_FREQ_THR 890
#define FM_TX_PWR_CTRL_TMP_THR_UP 45
#define FM_TX_PWR_CTRL_TMP_THR_DOWN 0

#define FM_TX_TRACKING_TIME_MAX 10000 //TX VCO tracking time, default 100ms

enum fm_priv_state{
    UNINITED,
    INITED     
};

enum fm_adpll_state{
    FM_ADPLL_ON,
    FM_ADPLL_OFF
};

enum fm_adpll_clk{
    FM_ADPLL_16M,
    FM_ADPLL_15M
};

enum fm_mcu_desense{
    FM_MCU_DESENSE_ENABLE,
    FM_MCU_DESENSE_DISABLE
};

struct fm_priv_cb {
	//Basic functions.
	int (*hl_side)(uint16_t freq, int *hl);
	int (*adpll_freq_avoid)(uint16_t freq, int *freqavoid);
	int (*mcu_freq_avoid)(uint16_t freq, int *freqavoid);
    int (*tx_pwr_ctrl)(uint16_t freq, int *ctr);
    int (*rtc_drift_ctrl)(uint16_t freq, int *ctr);
    int (*tx_desense_wifi)(uint16_t freq, int *ctr);
};

struct fm_priv{
    int state;
    void *data;
    struct fm_priv_cb priv_tbl;
};

struct fm_op_cb {
	//Basic functions.
	int (*read)(uint8_t addr, uint16_t *val);
	int (*write)(uint8_t addr, uint16_t val);
	int (*setbits)(uint8_t addr, uint16_t bits, uint16_t mask);
    int (*rampdown)(void);
};

struct fm_op{
    int state;
    void *data;
    struct fm_op_cb op_tbl;
};

#define FMR_ASSERT(a) { \
			if ((a) == NULL) { \
				printk("%s,invalid pointer\n", __func__);\
				return -ERR_INVALID_BUF; \
			} \
		}

int fm_priv_register(struct fm_priv *pri, struct fm_op *op);
int fm_priv_unregister(struct fm_priv *pri, struct fm_op *op);

#endif //__MTK_FM_H__

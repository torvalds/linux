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
#ifndef __MT_MSDC_DEUBG__
#define __MT_MSDC_DEUBG__

//==========================
extern u32 sdio_pro_enable;
/* for a type command, e.g. CMD53, 2 blocks */
struct cmd_profile {
	u32 max_tc;    /* Max tick count */
	u32 min_tc;
	u32 tot_tc;    /* total tick count */
	u32 tot_bytes;
	u32 count;     /* the counts of the command */
};

/* dump when total_tc and total_bytes */
struct sdio_profile {
	u32 total_tc;         /* total tick count of CMD52 and CMD53 */
	u32 total_tx_bytes;   /* total bytes of CMD53 Tx */
	u32 total_rx_bytes;   /* total bytes of CMD53 Rx */

	/*CMD52*/
	struct cmd_profile cmd52_tx;
	struct cmd_profile cmd52_rx;

	/*CMD53 in byte unit */
	struct cmd_profile cmd53_tx_byte[512];
	struct cmd_profile cmd53_rx_byte[512];

	/*CMD53 in block unit */
	struct cmd_profile cmd53_tx_blk[100];
	struct cmd_profile cmd53_rx_blk[100];
};

//==========================
enum msdc_dbg {
	SD_TOOL_ZONE = 0,
	SD_TOOL_DMA_SIZE  = 1,
	SD_TOOL_PM_ENABLE = 2,
	SD_TOOL_SDIO_PROFILE = 3,
};

enum msdc_mode {
	MODE_PIO = 0,
	MODE_DMA = 1,
	MODE_SIZE_DEP = 2,
};

/* Debug message event */
#define DBG_EVT_NONE        (0)       /* No event */
#define DBG_EVT_DMA         (1 << 0)  /* DMA related event */
#define DBG_EVT_CMD         (1 << 1)  /* MSDC CMD related event */
#define DBG_EVT_RSP         (1 << 2)  /* MSDC CMD RSP related event */
#define DBG_EVT_INT         (1 << 3)  /* MSDC INT event */
#define DBG_EVT_CFG         (1 << 4)  /* MSDC CFG event */
#define DBG_EVT_FUC         (1 << 5)  /* Function event */
#define DBG_EVT_OPS         (1 << 6)  /* Read/Write operation event */
#define DBG_EVT_FIO         (1 << 7)  /* FIFO operation event */
#define DBG_EVT_WRN         (1 << 8)  /* Warning event */
#define DBG_EVT_PWR         (1 << 9)  /* Power event */
#define DBG_EVT_ALL         (0xffffffff)

#define DBG_EVT_MASK        (DBG_EVT_ALL)

extern unsigned int sd_debug_zone[4];
#define TAG "msdc"
#if 0 /* +++ chhung */
#define BUG_ON(x) \
do { \
	if (x) { \
		printk("[BUG] %s LINE:%d FILE:%s\n", #x, __LINE__, __FILE__); \
		while (1)						\
			;						\
	} \
} while (0)
#endif /* end of +++ */

#define N_MSG(evt, fmt, args...)
/*
do {    \
    if ((DBG_EVT_##evt) & sd_debug_zone[host->id]) { \
        printk(KERN_ERR TAG"%d -> "fmt" <- %s() : L<%d> PID<%s><0x%x>\n", \
            host->id,  ##args , __FUNCTION__, __LINE__, current->comm, current->pid);	\
    } \
} while(0)
*/

#define ERR_MSG(fmt, args...) \
do { \
	printk(KERN_ERR TAG"%d -> "fmt" <- %s() : L<%d> PID<%s><0x%x>\n", \
	       host->id,  ##args, __FUNCTION__, __LINE__, current->comm, current->pid); \
} while (0);

#if 1
//defined CONFIG_MTK_MMC_CD_POLL
#define INIT_MSG(fmt, args...)
#define IRQ_MSG(fmt, args...)
#else
#define INIT_MSG(fmt, args...) \
do { \
	printk(KERN_ERR TAG"%d -> "fmt" <- %s() : L<%d> PID<%s><0x%x>\n", \
	       host->id,  ##args, __FUNCTION__, __LINE__, current->comm, current->pid); \
} while (0);

/* PID in ISR in not corrent */
#define IRQ_MSG(fmt, args...) \
do { \
	printk(KERN_ERR TAG"%d -> "fmt" <- %s() : L<%d>\n",	\
	       host->id,  ##args, __FUNCTION__, __LINE__);	\
} while (0);
#endif

void msdc_debug_proc_init(void);

#if 0 /* --- chhung */
void msdc_init_gpt(void);
extern void GPT_GetCounter64(UINT32 *cntL32, UINT32 *cntH32);
#endif /* end of --- */
u32 msdc_time_calc(u32 old_L32, u32 old_H32, u32 new_L32, u32 new_H32);
void msdc_performance(u32 opcode, u32 sizes, u32 bRx, u32 ticks);

#endif

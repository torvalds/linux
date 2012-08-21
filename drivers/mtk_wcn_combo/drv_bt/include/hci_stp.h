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

/*
 *
 *  Bluetooth HCI UART driver
 *
 *  Copyright (C) 2000-2001  Qualcomm Incorporated
 *  Copyright (C) 2002-2003  Maxim Krasnyansky <maxk@qualcomm.com>
 *  Copyright (C) 2004-2005  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/
#ifndef _HCI_STP_H
#define _HCI_STP_H

#define HCI_STP_TX_TASKLET (0) /* do tx in a tasklet context */
#define HCI_STP_TX_THRD (1) /* do tx in an init thread context */

/* select tx context */
#define HCI_STP_TX (HCI_STP_TX_THRD)

#if (HCI_STP_TX == HCI_STP_TX_TASKLET)
#define HCI_STP_TX_TASKLET_RWLOCK (0) /* use rwlock_t */
#define HCI_STP_TX_TASKLET_SPINLOCK (1) /* use spinlock_t */

/* select txq protection method */
#define HCI_STP_TX_TASKLET_LOCK (HCI_STP_TX_TASKLET_SPINLOCK)
#endif

#define HCI_STP_DEV_INIT_OPEN_CTX (0) /* do init in hci_dev->open context, checked ok */
#define HCI_STP_DEV_INIT_THRD (1) /* do init in another thread context */

/* select init context */
#define HCI_STP_DEV_INIT (HCI_STP_DEV_INIT_THRD)

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/poll.h>

#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/signal.h>
#include <linux/ioctl.h>
#include <linux/skbuff.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <asm/uaccess.h>

/* debugging */
#include <linux/time.h>
#include <linux/delay.h>

/* constant of kernel version */
#include <linux/version.h>

/* kthread APIs */
#include <linux/kthread.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/* HCI-STP flag bits */
#define HCI_STP_PROTO_SET (0)
/* HCI-STP flag TX states bits */
#define HCI_STP_SENDING (1)
#define HCI_STP_TX_WAKEUP (2)

#if (HCI_STP_DEV_INIT == HCI_STP_DEV_INIT_THRD)
#define HCI_STP_INIT_TO_MSEC (5000) /* required time to finish init sequence */
#endif

/* maximum delay required */
#define BT_CMD_DELAY_MS_COMM (50)
#define BT_CMD_DELAY_MS_RESET (500)
#define BT_CMD_DELAY_SAFE_GUARD (20) /*(2)*/

/* HCI-STP safer hci_reset handling */
#define HCI_STP_SAFE_RESET (1)

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
struct hci_stp_init_cmd {
    unsigned char *hci_cmd;
    unsigned int cmdSz;
    unsigned char *hci_evt;
    unsigned int evtSz;
    char *str;
};

struct hci_stp {
    struct hci_dev *hdev;
    unsigned long flags;

    struct sk_buff_head txq; /* used to queue TX packets */
    unsigned long tx_state;

#if (HCI_STP_DEV_INIT == HCI_STP_DEV_INIT_THRD)
    struct work_struct init_work;
    struct completion *p_init_comp;
    wait_queue_head_t *p_init_evt_wq;
    spinlock_t init_lock; /* protect init variables: comp and wq */
    unsigned int init_cmd_idx;
    int init_evt_rx_flag; /* init result of last sent cmd */
#endif

#if HCI_STP_SAFE_RESET
    wait_queue_head_t reset_wq;
    atomic_t reset_count; /* !0: reset in progress */
#endif
    //void *priv; /* unused? */
    //struct sk_buff *tx_skb; /* unused? */
    //spinlock_t rx_lock; /* unused? */
};

struct btradio_conf_data {
  unsigned char addr[6];
  unsigned char voice[2];
  unsigned char codec[4];
  unsigned char radio[6];
  unsigned char sleep[7];
  unsigned char feature[2];
  unsigned char tx_pwr_offset[3];
};
/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/
#define hci_stp_init_entry(c) {.hci_cmd=c, .cmdSz=sizeof(c), .hci_evt=c##_evt, .evtSz=sizeof(c##_evt), .str=#c}

#endif /* end of _HCI_STP_H */


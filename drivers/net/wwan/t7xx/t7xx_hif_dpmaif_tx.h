/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021-2022, Intel Corporation.
 *
 * Authors:
 *  Haijun Liu <haijun.liu@mediatek.com>
 *  Eliot Lee <eliot.lee@intel.com>
 *  Ricardo Martinez <ricardo.martinez@linux.intel.com>
 *
 * Contributors:
 *  Amir Hanania <amir.hanania@intel.com>
 *  Chiranjeevi Rapolu <chiranjeevi.rapolu@intel.com>
 *  Moises Veleta <moises.veleta@intel.com>
 *  Sreehari Kancharla <sreehari.kancharla@intel.com>
 */

#ifndef __T7XX_HIF_DPMA_TX_H__
#define __T7XX_HIF_DPMA_TX_H__

#include <linux/bits.h>
#include <linux/skbuff.h>
#include <linux/types.h>

#include "t7xx_hif_dpmaif.h"

#define DPMAIF_TX_DEFAULT_QUEUE	0

struct dpmaif_drb {
	__le32 header;
	union {
		struct {
			__le32 data_addr_l;
			__le32 data_addr_h;
		} pd;
		struct {
			__le32 msg_hdr;
			__le32 reserved1;
		} msg;
	};
	__le32 reserved2;
};

/* Header fields */
#define DRB_HDR_DATA_LEN	GENMASK(31, 16)
#define DRB_HDR_RESERVED	GENMASK(15, 3)
#define DRB_HDR_CONT		BIT(2)
#define DRB_HDR_DTYP		GENMASK(1, 0)

#define DRB_MSG_DW2_RES		GENMASK(31, 30)
#define DRB_MSG_L4_CHK		BIT(29)
#define DRB_MSG_IP_CHK		BIT(28)
#define DRB_MSG_RESERVED	BIT(27)
#define DRB_MSG_NETWORK_TYPE	GENMASK(26, 24)
#define DRB_MSG_CHANNEL_ID	GENMASK(23, 16)
#define DRB_MSG_COUNT_L		GENMASK(15, 0)

struct dpmaif_drb_skb {
	struct sk_buff		*skb;
	dma_addr_t		bus_addr;
	unsigned int		data_len;
	u16			index:13;
	u16			is_msg:1;
	u16			is_frag:1;
	u16			is_last:1;
};

int t7xx_dpmaif_tx_send_skb(struct dpmaif_ctrl *dpmaif_ctrl, unsigned int txq_number,
			    struct sk_buff *skb);
void t7xx_dpmaif_tx_thread_rel(struct dpmaif_ctrl *dpmaif_ctrl);
int t7xx_dpmaif_tx_thread_init(struct dpmaif_ctrl *dpmaif_ctrl);
void t7xx_dpmaif_txq_free(struct dpmaif_tx_queue *txq);
void t7xx_dpmaif_irq_tx_done(struct dpmaif_ctrl *dpmaif_ctrl, unsigned int que_mask);
int t7xx_dpmaif_txq_init(struct dpmaif_tx_queue *txq);
void t7xx_dpmaif_tx_stop(struct dpmaif_ctrl *dpmaif_ctrl);
void t7xx_dpmaif_tx_clear(struct dpmaif_ctrl *dpmaif_ctrl);

#endif /* __T7XX_HIF_DPMA_TX_H__ */

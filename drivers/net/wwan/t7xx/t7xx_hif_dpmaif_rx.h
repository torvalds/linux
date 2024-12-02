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
 *  Moises Veleta <moises.veleta@intel.com>
 *  Sreehari Kancharla <sreehari.kancharla@intel.com>
 */

#ifndef __T7XX_HIF_DPMA_RX_H__
#define __T7XX_HIF_DPMA_RX_H__

#include <linux/bits.h>
#include <linux/types.h>

#include "t7xx_hif_dpmaif.h"

#define NETIF_MASK		GENMASK(4, 0)

#define PKT_TYPE_IP4		0
#define PKT_TYPE_IP6		1

/* Structure of DL PIT */
struct dpmaif_pit {
	__le32 header;
	union {
		struct {
			__le32 data_addr_l;
			__le32 data_addr_h;
			__le32 footer;
		} pd;
		struct {
			__le32 params_1;
			__le32 params_2;
			__le32 params_3;
		} msg;
	};
};

/* PIT header fields */
#define PD_PIT_DATA_LEN		GENMASK(31, 16)
#define PD_PIT_BUFFER_ID	GENMASK(15, 3)
#define PD_PIT_BUFFER_TYPE	BIT(2)
#define PD_PIT_CONT		BIT(1)
#define PD_PIT_PACKET_TYPE	BIT(0)
/* PIT footer fields */
#define PD_PIT_DLQ_DONE		GENMASK(31, 30)
#define PD_PIT_ULQ_DONE		GENMASK(29, 24)
#define PD_PIT_HEADER_OFFSET	GENMASK(23, 19)
#define PD_PIT_BI_F		GENMASK(18, 17)
#define PD_PIT_IG		BIT(16)
#define PD_PIT_RES		GENMASK(15, 11)
#define PD_PIT_H_BID		GENMASK(10, 8)
#define PD_PIT_PIT_SEQ		GENMASK(7, 0)

#define MSG_PIT_DP		BIT(31)
#define MSG_PIT_RES		GENMASK(30, 27)
#define MSG_PIT_NETWORK_TYPE	GENMASK(26, 24)
#define MSG_PIT_CHANNEL_ID	GENMASK(23, 16)
#define MSG_PIT_RES2		GENMASK(15, 12)
#define MSG_PIT_HPC_IDX		GENMASK(11, 8)
#define MSG_PIT_SRC_QID		GENMASK(7, 5)
#define MSG_PIT_ERROR_BIT	BIT(4)
#define MSG_PIT_CHECKSUM	GENMASK(3, 2)
#define MSG_PIT_CONT		BIT(1)
#define MSG_PIT_PACKET_TYPE	BIT(0)

#define MSG_PIT_HP_IDX		GENMASK(31, 27)
#define MSG_PIT_CMD		GENMASK(26, 24)
#define MSG_PIT_RES3		GENMASK(23, 21)
#define MSG_PIT_FLOW		GENMASK(20, 16)
#define MSG_PIT_COUNT		GENMASK(15, 0)

#define MSG_PIT_HASH		GENMASK(31, 24)
#define MSG_PIT_RES4		GENMASK(23, 18)
#define MSG_PIT_PRO		GENMASK(17, 16)
#define MSG_PIT_VBID		GENMASK(15, 3)
#define MSG_PIT_RES5		GENMASK(2, 0)

#define MSG_PIT_DLQ_DONE	GENMASK(31, 30)
#define MSG_PIT_ULQ_DONE	GENMASK(29, 24)
#define MSG_PIT_IP		BIT(23)
#define MSG_PIT_RES6		BIT(22)
#define MSG_PIT_MR		GENMASK(21, 20)
#define MSG_PIT_RES7		GENMASK(19, 17)
#define MSG_PIT_IG		BIT(16)
#define MSG_PIT_RES8		GENMASK(15, 11)
#define MSG_PIT_H_BID		GENMASK(10, 8)
#define MSG_PIT_PIT_SEQ		GENMASK(7, 0)

int t7xx_dpmaif_rxq_init(struct dpmaif_rx_queue *queue);
void t7xx_dpmaif_rx_clear(struct dpmaif_ctrl *dpmaif_ctrl);
int t7xx_dpmaif_bat_rel_wq_alloc(struct dpmaif_ctrl *dpmaif_ctrl);
int t7xx_dpmaif_rx_buf_alloc(struct dpmaif_ctrl *dpmaif_ctrl,
			     const struct dpmaif_bat_request *bat_req,
			     const unsigned int q_num, const unsigned int buf_cnt,
			     const bool initial);
int t7xx_dpmaif_rx_frag_alloc(struct dpmaif_ctrl *dpmaif_ctrl, struct dpmaif_bat_request *bat_req,
			      const unsigned int buf_cnt, const bool first_time);
void t7xx_dpmaif_rx_stop(struct dpmaif_ctrl *dpmaif_ctrl);
void t7xx_dpmaif_irq_rx_done(struct dpmaif_ctrl *dpmaif_ctrl, const unsigned int que_mask);
void t7xx_dpmaif_rxq_free(struct dpmaif_rx_queue *queue);
void t7xx_dpmaif_bat_wq_rel(struct dpmaif_ctrl *dpmaif_ctrl);
int t7xx_dpmaif_bat_alloc(const struct dpmaif_ctrl *dpmaif_ctrl, struct dpmaif_bat_request *bat_req,
			  const enum bat_type buf_type);
void t7xx_dpmaif_bat_free(const struct dpmaif_ctrl *dpmaif_ctrl,
			  struct dpmaif_bat_request *bat_req);

#endif /* __T7XX_HIF_DPMA_RX_H__ */

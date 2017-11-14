// SPDX-License-Identifier: GPL-2.0
/*
 * mtu3_qmu.h - Queue Management Unit driver header
 *
 * Copyright (C) 2016 MediaTek Inc.
 *
 * Author: Chunfeng Yun <chunfeng.yun@mediatek.com>
 */

#ifndef __MTK_QMU_H__
#define __MTK_QMU_H__

#define MAX_GPD_NUM		64
#define QMU_GPD_SIZE		(sizeof(struct qmu_gpd))
#define QMU_GPD_RING_SIZE	(MAX_GPD_NUM * QMU_GPD_SIZE)

#define GPD_BUF_SIZE		65532

void mtu3_qmu_stop(struct mtu3_ep *mep);
int mtu3_qmu_start(struct mtu3_ep *mep);
void mtu3_qmu_resume(struct mtu3_ep *mep);
void mtu3_qmu_flush(struct mtu3_ep *mep);

void mtu3_insert_gpd(struct mtu3_ep *mep, struct mtu3_request *mreq);
int mtu3_prepare_transfer(struct mtu3_ep *mep);

int mtu3_gpd_ring_alloc(struct mtu3_ep *mep);
void mtu3_gpd_ring_free(struct mtu3_ep *mep);

irqreturn_t mtu3_qmu_isr(struct mtu3 *mtu);
int mtu3_qmu_init(struct mtu3 *mtu);
void mtu3_qmu_exit(struct mtu3 *mtu);

#endif

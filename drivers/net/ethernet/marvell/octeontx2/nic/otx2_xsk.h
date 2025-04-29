/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell RVU PF/VF Netdev Devlink
 *
 * Copyright (C) 2024 Marvell.
 *
 */

#ifndef	OTX2_XSK_H
#define	OTX2_XSK_H

struct otx2_nic;
struct xsk_buff_pool;

int otx2_xsk_pool_setup(struct otx2_nic *pf, struct xsk_buff_pool *pool, u16 qid);
int otx2_xsk_pool_enable(struct otx2_nic *pf, struct xsk_buff_pool *pool, u16 qid);
int otx2_xsk_pool_disable(struct otx2_nic *pf, u16 qid);
int otx2_xsk_pool_alloc_buf(struct otx2_nic *pfvf, struct otx2_pool *pool,
			    dma_addr_t *dma, int idx);
int otx2_xsk_wakeup(struct net_device *dev, u32 queue_id, u32 flags);
void otx2_zc_napi_handler(struct otx2_nic *pfvf, struct xsk_buff_pool *pool,
			  int queue, int budget);
void otx2_attach_xsk_buff(struct otx2_nic *pfvf, struct otx2_snd_queue *sq, int qidx);

#endif /* OTX2_XSK_H */

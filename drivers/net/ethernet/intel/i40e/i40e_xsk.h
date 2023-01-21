/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2018 Intel Corporation. */

#ifndef _I40E_XSK_H_
#define _I40E_XSK_H_

struct i40e_vsi;
struct xsk_buff_pool;
struct zero_copy_allocator;

int i40e_queue_pair_disable(struct i40e_vsi *vsi, int queue_pair);
int i40e_queue_pair_enable(struct i40e_vsi *vsi, int queue_pair);
int i40e_xsk_pool_setup(struct i40e_vsi *vsi, struct xsk_buff_pool *pool,
			u16 qid);
bool i40e_alloc_rx_buffers_zc(struct i40e_ring *rx_ring, u16 cleaned_count);
int i40e_clean_rx_irq_zc(struct i40e_ring *rx_ring, int budget);

bool i40e_clean_xdp_tx_irq(struct i40e_vsi *vsi, struct i40e_ring *tx_ring);
int i40e_xsk_wakeup(struct net_device *dev, u32 queue_id, u32 flags);
int i40e_realloc_rx_bi_zc(struct i40e_vsi *vsi, bool zc);
void i40e_clear_rx_bi_zc(struct i40e_ring *rx_ring);

#endif /* _I40E_XSK_H_ */

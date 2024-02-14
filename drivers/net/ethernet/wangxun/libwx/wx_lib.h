/* SPDX-License-Identifier: GPL-2.0 */
/*
 * WangXun Gigabit PCI Express Linux driver
 * Copyright (c) 2019 - 2022 Beijing WangXun Technology Co., Ltd.
 */

#ifndef _WX_LIB_H_
#define _WX_LIB_H_

void wx_alloc_rx_buffers(struct wx_ring *rx_ring, u16 cleaned_count);
u16 wx_desc_unused(struct wx_ring *ring);
netdev_tx_t wx_xmit_frame(struct sk_buff *skb,
			  struct net_device *netdev);
void wx_napi_enable_all(struct wx *wx);
void wx_napi_disable_all(struct wx *wx);
void wx_reset_interrupt_capability(struct wx *wx);
void wx_clear_interrupt_scheme(struct wx *wx);
int wx_init_interrupt_scheme(struct wx *wx);
irqreturn_t wx_msix_clean_rings(int __always_unused irq, void *data);
void wx_free_irq(struct wx *wx);
int wx_setup_isb_resources(struct wx *wx);
void wx_free_isb_resources(struct wx *wx);
u32 wx_misc_isb(struct wx *wx, enum wx_isb_idx idx);
void wx_write_eitr(struct wx_q_vector *q_vector);
void wx_configure_vectors(struct wx *wx);
void wx_clean_all_rx_rings(struct wx *wx);
void wx_clean_all_tx_rings(struct wx *wx);
void wx_free_resources(struct wx *wx);
int wx_setup_resources(struct wx *wx);
void wx_get_stats64(struct net_device *netdev,
		    struct rtnl_link_stats64 *stats);
int wx_set_features(struct net_device *netdev, netdev_features_t features);
void wx_set_ring(struct wx *wx, u32 new_tx_count,
		 u32 new_rx_count, struct wx_ring *temp_ring);

#endif /* _NGBE_LIB_H_ */

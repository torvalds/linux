/* SPDX-License-Identifier: GPL-2.0-only */
/* Atlantic Network Driver
 *
 * Copyright (C) 2014-2019 aQuantia Corporation
 * Copyright (C) 2019-2020 Marvell International Ltd.
 */

/* File hw_atl_b0.h: Declaration of abstract interface for Atlantic hardware
 * specific functions.
 */

#ifndef HW_ATL_B0_H
#define HW_ATL_B0_H

#include "../aq_common.h"

extern const struct aq_hw_caps_s hw_atl_b0_caps_aqc100;
extern const struct aq_hw_caps_s hw_atl_b0_caps_aqc107;
extern const struct aq_hw_caps_s hw_atl_b0_caps_aqc108;
extern const struct aq_hw_caps_s hw_atl_b0_caps_aqc109;
extern const struct aq_hw_caps_s hw_atl_b0_caps_aqc111;
extern const struct aq_hw_caps_s hw_atl_b0_caps_aqc112;

#define hw_atl_b0_caps_aqc100s hw_atl_b0_caps_aqc100
#define hw_atl_b0_caps_aqc107s hw_atl_b0_caps_aqc107
#define hw_atl_b0_caps_aqc108s hw_atl_b0_caps_aqc108
#define hw_atl_b0_caps_aqc109s hw_atl_b0_caps_aqc109
#define hw_atl_b0_caps_aqc111s hw_atl_b0_caps_aqc111
#define hw_atl_b0_caps_aqc112s hw_atl_b0_caps_aqc112

extern const struct aq_hw_ops hw_atl_ops_b0;

#define hw_atl_ops_b1 hw_atl_ops_b0

int hw_atl_b0_hw_rss_hash_set(struct aq_hw_s *self,
			      struct aq_rss_parameters *rss_params);
int hw_atl_b0_hw_offload_set(struct aq_hw_s *self,
			     struct aq_nic_cfg_s *aq_nic_cfg);

int hw_atl_b0_hw_ring_tx_start(struct aq_hw_s *self, struct aq_ring_s *ring);
int hw_atl_b0_hw_ring_rx_start(struct aq_hw_s *self, struct aq_ring_s *ring);

int hw_atl_b0_hw_ring_rx_init(struct aq_hw_s *self, struct aq_ring_s *aq_ring,
			      struct aq_ring_param_s *aq_ring_param);
int hw_atl_b0_hw_ring_rx_fill(struct aq_hw_s *self, struct aq_ring_s *ring,
			      unsigned int sw_tail_old);
int hw_atl_b0_hw_ring_rx_receive(struct aq_hw_s *self, struct aq_ring_s *ring);

int hw_atl_b0_hw_ring_tx_init(struct aq_hw_s *self, struct aq_ring_s *aq_ring,
			      struct aq_ring_param_s *aq_ring_param);
int hw_atl_b0_hw_ring_tx_xmit(struct aq_hw_s *self, struct aq_ring_s *ring,
			      unsigned int frags);
int hw_atl_b0_hw_ring_tx_head_update(struct aq_hw_s *self,
				     struct aq_ring_s *ring);

int hw_atl_b0_hw_ring_tx_stop(struct aq_hw_s *self, struct aq_ring_s *ring);
int hw_atl_b0_hw_ring_rx_stop(struct aq_hw_s *self, struct aq_ring_s *ring);

void hw_atl_b0_hw_init_rx_rss_ctrl1(struct aq_hw_s *self);

int hw_atl_b0_hw_mac_addr_set(struct aq_hw_s *self, u8 *mac_addr);

int hw_atl_b0_hw_start(struct aq_hw_s *self);

int hw_atl_b0_hw_irq_enable(struct aq_hw_s *self, u64 mask);
int hw_atl_b0_hw_irq_disable(struct aq_hw_s *self, u64 mask);
int hw_atl_b0_hw_irq_read(struct aq_hw_s *self, u64 *mask);

int hw_atl_b0_hw_packet_filter_set(struct aq_hw_s *self,
				   unsigned int packet_filter);

#endif /* HW_ATL_B0_H */

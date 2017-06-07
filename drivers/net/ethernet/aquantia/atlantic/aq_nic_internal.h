/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

/* File aq_nic_internal.h: Definition of private object structure. */

#ifndef AQ_NIC_INTERNAL_H
#define AQ_NIC_INTERNAL_H

struct aq_nic_s {
	struct aq_obj_s header;
	struct aq_vec_s *aq_vec[AQ_CFG_VECS_MAX];
	struct aq_ring_s *aq_ring_tx[AQ_CFG_VECS_MAX * AQ_CFG_TCS_MAX];
	struct aq_hw_s *aq_hw;
	struct net_device *ndev;
	struct aq_pci_func_s *aq_pci_func;
	unsigned int aq_vecs;
	unsigned int packet_filter;
	unsigned int power_state;
	u8 port;
	struct aq_hw_ops aq_hw_ops;
	struct aq_hw_caps_s aq_hw_caps;
	struct aq_nic_cfg_s aq_nic_cfg;
	struct timer_list service_timer;
	struct timer_list polling_timer;
	struct aq_hw_link_status_s link_status;
	struct {
		u32 count;
		u8 ar[AQ_CFG_MULTICAST_ADDRESS_MAX][ETH_ALEN];
	} mc_list;
};

#define AQ_NIC_FLAGS_IS_NOT_READY (AQ_NIC_FLAG_STOPPING | \
			AQ_NIC_FLAG_RESETTING | AQ_NIC_FLAG_CLOSING | \
			AQ_NIC_FLAG_ERR_UNPLUG | AQ_NIC_FLAG_ERR_HW)

#define AQ_NIC_FLAGS_IS_NOT_TX_READY (AQ_NIC_FLAGS_IS_NOT_READY | \
					AQ_NIC_LINK_DOWN)

#endif /* AQ_NIC_INTERNAL_H */

/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

/* File aq_hw.h: Declaraion of abstract interface for NIC hardware specific
 * functions.
 */

#ifndef AQ_HW_H
#define AQ_HW_H

#include "aq_common.h"

/* NIC H/W capabilities */
struct aq_hw_caps_s {
	u64 hw_features;
	u64 link_speed_msk;
	unsigned int hw_priv_flags;
	u32 rxds;
	u32 txds;
	u32 txhwb_alignment;
	u32 irq_mask;
	u32 vecs;
	u32 mtu;
	u32 mac_regs_count;
	u8 ports;
	u8 msix_irqs;
	u8 tcs;
	u8 rxd_alignment;
	u8 rxd_size;
	u8 txd_alignment;
	u8 txd_size;
	u8 tx_rings;
	u8 rx_rings;
	bool flow_control;
	bool is_64_dma;
	u32 fw_ver_expected;
};

struct aq_hw_link_status_s {
	unsigned int mbps;
};

#define AQ_HW_IRQ_INVALID 0U
#define AQ_HW_IRQ_LEGACY  1U
#define AQ_HW_IRQ_MSI     2U
#define AQ_HW_IRQ_MSIX    3U

#define AQ_HW_POWER_STATE_D0   0U
#define AQ_HW_POWER_STATE_D3   3U

#define AQ_HW_FLAG_STARTED     0x00000004U
#define AQ_HW_FLAG_STOPPING    0x00000008U
#define AQ_HW_FLAG_RESETTING   0x00000010U
#define AQ_HW_FLAG_CLOSING     0x00000020U
#define AQ_HW_LINK_DOWN        0x04000000U
#define AQ_HW_FLAG_ERR_UNPLUG  0x40000000U
#define AQ_HW_FLAG_ERR_HW      0x80000000U

#define AQ_HW_FLAG_ERRORS      (AQ_HW_FLAG_ERR_HW | AQ_HW_FLAG_ERR_UNPLUG)

struct aq_hw_s {
	struct aq_obj_s header;
	struct aq_nic_cfg_s *aq_nic_cfg;
	struct aq_pci_func_s *aq_pci_func;
	void __iomem *mmio;
	unsigned int not_ff_addr;
	struct aq_hw_link_status_s aq_link_status;
};

struct aq_ring_s;
struct aq_ring_param_s;
struct aq_nic_cfg_s;
struct sk_buff;

struct aq_hw_ops {
	struct aq_hw_s *(*create)(struct aq_pci_func_s *aq_pci_func,
				  unsigned int port, struct aq_hw_ops *ops);

	void (*destroy)(struct aq_hw_s *self);

	int (*get_hw_caps)(struct aq_hw_s *self,
			   struct aq_hw_caps_s *aq_hw_caps);

	int (*hw_ring_tx_xmit)(struct aq_hw_s *self, struct aq_ring_s *aq_ring,
			       unsigned int frags);

	int (*hw_ring_rx_receive)(struct aq_hw_s *self,
				  struct aq_ring_s *aq_ring);

	int (*hw_ring_rx_fill)(struct aq_hw_s *self, struct aq_ring_s *aq_ring,
			       unsigned int sw_tail_old);

	int (*hw_ring_tx_head_update)(struct aq_hw_s *self,
				      struct aq_ring_s *aq_ring);

	int (*hw_get_mac_permanent)(struct aq_hw_s *self,
				    struct aq_hw_caps_s *aq_hw_caps,
				    u8 *mac);

	int (*hw_set_mac_address)(struct aq_hw_s *self, u8 *mac_addr);

	int (*hw_get_link_status)(struct aq_hw_s *self,
				  struct aq_hw_link_status_s *link_status);

	int (*hw_set_link_speed)(struct aq_hw_s *self, u32 speed);

	int (*hw_reset)(struct aq_hw_s *self);

	int (*hw_init)(struct aq_hw_s *self, struct aq_nic_cfg_s *aq_nic_cfg,
		       u8 *mac_addr);

	int (*hw_start)(struct aq_hw_s *self);

	int (*hw_stop)(struct aq_hw_s *self);

	int (*hw_ring_tx_init)(struct aq_hw_s *self, struct aq_ring_s *aq_ring,
			       struct aq_ring_param_s *aq_ring_param);

	int (*hw_ring_tx_start)(struct aq_hw_s *self,
				struct aq_ring_s *aq_ring);

	int (*hw_ring_tx_stop)(struct aq_hw_s *self,
			       struct aq_ring_s *aq_ring);

	int (*hw_ring_rx_init)(struct aq_hw_s *self,
			       struct aq_ring_s *aq_ring,
			       struct aq_ring_param_s *aq_ring_param);

	int (*hw_ring_rx_start)(struct aq_hw_s *self,
				struct aq_ring_s *aq_ring);

	int (*hw_ring_rx_stop)(struct aq_hw_s *self,
			       struct aq_ring_s *aq_ring);

	int (*hw_irq_enable)(struct aq_hw_s *self, u64 mask);

	int (*hw_irq_disable)(struct aq_hw_s *self, u64 mask);

	int (*hw_irq_read)(struct aq_hw_s *self, u64 *mask);

	int (*hw_packet_filter_set)(struct aq_hw_s *self,
				    unsigned int packet_filter);

	int (*hw_multicast_list_set)(struct aq_hw_s *self,
				     u8 ar_mac[AQ_CFG_MULTICAST_ADDRESS_MAX]
				     [ETH_ALEN],
				     u32 count);

	int (*hw_interrupt_moderation_set)(struct aq_hw_s *self,
					   bool itr_enabled);

	int (*hw_rss_set)(struct aq_hw_s *self,
			  struct aq_rss_parameters *rss_params);

	int (*hw_rss_hash_set)(struct aq_hw_s *self,
			       struct aq_rss_parameters *rss_params);

	int (*hw_get_regs)(struct aq_hw_s *self,
			   struct aq_hw_caps_s *aq_hw_caps, u32 *regs_buff);

	int (*hw_get_hw_stats)(struct aq_hw_s *self, u64 *data,
			       unsigned int *p_count);

	int (*hw_get_fw_version)(struct aq_hw_s *self, u32 *fw_version);

	int (*hw_deinit)(struct aq_hw_s *self);

	int (*hw_set_power)(struct aq_hw_s *self, unsigned int power_state);
};

#endif /* AQ_HW_H */

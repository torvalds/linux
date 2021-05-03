/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
// Copyright (c) 2018 Synopsys, Inc. and/or its affiliates.
// stmmac HW Interface Callbacks

#ifndef __STMMAC_HWIF_H__
#define __STMMAC_HWIF_H__

#include <linux/netdevice.h>
#include <linux/stmmac.h>

#define stmmac_do_void_callback(__priv, __module, __cname,  __arg0, __args...) \
({ \
	int __result = -EINVAL; \
	if ((__priv)->hw->__module && (__priv)->hw->__module->__cname) { \
		(__priv)->hw->__module->__cname((__arg0), ##__args); \
		__result = 0; \
	} \
	__result; \
})
#define stmmac_do_callback(__priv, __module, __cname,  __arg0, __args...) \
({ \
	int __result = -EINVAL; \
	if ((__priv)->hw->__module && (__priv)->hw->__module->__cname) \
		__result = (__priv)->hw->__module->__cname((__arg0), ##__args); \
	__result; \
})

struct stmmac_extra_stats;
struct stmmac_safety_stats;
struct dma_desc;
struct dma_extended_desc;
struct dma_edesc;

/* Descriptors helpers */
struct stmmac_desc_ops {
	/* DMA RX descriptor ring initialization */
	void (*init_rx_desc)(struct dma_desc *p, int disable_rx_ic, int mode,
			int end, int bfsize);
	/* DMA TX descriptor ring initialization */
	void (*init_tx_desc)(struct dma_desc *p, int mode, int end);
	/* Invoked by the xmit function to prepare the tx descriptor */
	void (*prepare_tx_desc)(struct dma_desc *p, int is_fs, int len,
			bool csum_flag, int mode, bool tx_own, bool ls,
			unsigned int tot_pkt_len);
	void (*prepare_tso_tx_desc)(struct dma_desc *p, int is_fs, int len1,
			int len2, bool tx_own, bool ls, unsigned int tcphdrlen,
			unsigned int tcppayloadlen);
	/* Set/get the owner of the descriptor */
	void (*set_tx_owner)(struct dma_desc *p);
	int (*get_tx_owner)(struct dma_desc *p);
	/* Clean the tx descriptor as soon as the tx irq is received */
	void (*release_tx_desc)(struct dma_desc *p, int mode);
	/* Clear interrupt on tx frame completion. When this bit is
	 * set an interrupt happens as soon as the frame is transmitted */
	void (*set_tx_ic)(struct dma_desc *p);
	/* Last tx segment reports the transmit status */
	int (*get_tx_ls)(struct dma_desc *p);
	/* Return the transmit status looking at the TDES1 */
	int (*tx_status)(void *data, struct stmmac_extra_stats *x,
			struct dma_desc *p, void __iomem *ioaddr);
	/* Get the buffer size from the descriptor */
	int (*get_tx_len)(struct dma_desc *p);
	/* Handle extra events on specific interrupts hw dependent */
	void (*set_rx_owner)(struct dma_desc *p, int disable_rx_ic);
	/* Get the receive frame size */
	int (*get_rx_frame_len)(struct dma_desc *p, int rx_coe_type);
	/* Return the reception status looking at the RDES1 */
	int (*rx_status)(void *data, struct stmmac_extra_stats *x,
			struct dma_desc *p);
	void (*rx_extended_status)(void *data, struct stmmac_extra_stats *x,
			struct dma_extended_desc *p);
	/* Set tx timestamp enable bit */
	void (*enable_tx_timestamp) (struct dma_desc *p);
	/* get tx timestamp status */
	int (*get_tx_timestamp_status) (struct dma_desc *p);
	/* get timestamp value */
	void (*get_timestamp)(void *desc, u32 ats, u64 *ts);
	/* get rx timestamp status */
	int (*get_rx_timestamp_status)(void *desc, void *next_desc, u32 ats);
	/* Display ring */
	void (*display_ring)(void *head, unsigned int size, bool rx,
			     dma_addr_t dma_rx_phy, unsigned int desc_size);
	/* set MSS via context descriptor */
	void (*set_mss)(struct dma_desc *p, unsigned int mss);
	/* get descriptor skbuff address */
	void (*get_addr)(struct dma_desc *p, unsigned int *addr);
	/* set descriptor skbuff address */
	void (*set_addr)(struct dma_desc *p, dma_addr_t addr);
	/* clear descriptor */
	void (*clear)(struct dma_desc *p);
	/* RSS */
	int (*get_rx_hash)(struct dma_desc *p, u32 *hash,
			   enum pkt_hash_types *type);
	void (*get_rx_header_len)(struct dma_desc *p, unsigned int *len);
	void (*set_sec_addr)(struct dma_desc *p, dma_addr_t addr, bool buf2_valid);
	void (*set_sarc)(struct dma_desc *p, u32 sarc_type);
	void (*set_vlan_tag)(struct dma_desc *p, u16 tag, u16 inner_tag,
			     u32 inner_type);
	void (*set_vlan)(struct dma_desc *p, u32 type);
	void (*set_tbs)(struct dma_edesc *p, u32 sec, u32 nsec);
};

#define stmmac_init_rx_desc(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, init_rx_desc, __args)
#define stmmac_init_tx_desc(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, init_tx_desc, __args)
#define stmmac_prepare_tx_desc(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, prepare_tx_desc, __args)
#define stmmac_prepare_tso_tx_desc(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, prepare_tso_tx_desc, __args)
#define stmmac_set_tx_owner(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, set_tx_owner, __args)
#define stmmac_get_tx_owner(__priv, __args...) \
	stmmac_do_callback(__priv, desc, get_tx_owner, __args)
#define stmmac_release_tx_desc(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, release_tx_desc, __args)
#define stmmac_set_tx_ic(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, set_tx_ic, __args)
#define stmmac_get_tx_ls(__priv, __args...) \
	stmmac_do_callback(__priv, desc, get_tx_ls, __args)
#define stmmac_tx_status(__priv, __args...) \
	stmmac_do_callback(__priv, desc, tx_status, __args)
#define stmmac_get_tx_len(__priv, __args...) \
	stmmac_do_callback(__priv, desc, get_tx_len, __args)
#define stmmac_set_rx_owner(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, set_rx_owner, __args)
#define stmmac_get_rx_frame_len(__priv, __args...) \
	stmmac_do_callback(__priv, desc, get_rx_frame_len, __args)
#define stmmac_rx_status(__priv, __args...) \
	stmmac_do_callback(__priv, desc, rx_status, __args)
#define stmmac_rx_extended_status(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, rx_extended_status, __args)
#define stmmac_enable_tx_timestamp(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, enable_tx_timestamp, __args)
#define stmmac_get_tx_timestamp_status(__priv, __args...) \
	stmmac_do_callback(__priv, desc, get_tx_timestamp_status, __args)
#define stmmac_get_timestamp(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, get_timestamp, __args)
#define stmmac_get_rx_timestamp_status(__priv, __args...) \
	stmmac_do_callback(__priv, desc, get_rx_timestamp_status, __args)
#define stmmac_display_ring(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, display_ring, __args)
#define stmmac_set_mss(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, set_mss, __args)
#define stmmac_get_desc_addr(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, get_addr, __args)
#define stmmac_set_desc_addr(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, set_addr, __args)
#define stmmac_clear_desc(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, clear, __args)
#define stmmac_get_rx_hash(__priv, __args...) \
	stmmac_do_callback(__priv, desc, get_rx_hash, __args)
#define stmmac_get_rx_header_len(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, get_rx_header_len, __args)
#define stmmac_set_desc_sec_addr(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, set_sec_addr, __args)
#define stmmac_set_desc_sarc(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, set_sarc, __args)
#define stmmac_set_desc_vlan_tag(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, set_vlan_tag, __args)
#define stmmac_set_desc_vlan(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, set_vlan, __args)
#define stmmac_set_desc_tbs(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, set_tbs, __args)

struct stmmac_dma_cfg;
struct dma_features;

/* Specific DMA helpers */
struct stmmac_dma_ops {
	/* DMA core initialization */
	int (*reset)(void __iomem *ioaddr);
	void (*init)(void __iomem *ioaddr, struct stmmac_dma_cfg *dma_cfg,
		     int atds);
	void (*init_chan)(void __iomem *ioaddr,
			  struct stmmac_dma_cfg *dma_cfg, u32 chan);
	void (*init_rx_chan)(void __iomem *ioaddr,
			     struct stmmac_dma_cfg *dma_cfg,
			     dma_addr_t phy, u32 chan);
	void (*init_tx_chan)(void __iomem *ioaddr,
			     struct stmmac_dma_cfg *dma_cfg,
			     dma_addr_t phy, u32 chan);
	/* Configure the AXI Bus Mode Register */
	void (*axi)(void __iomem *ioaddr, struct stmmac_axi *axi);
	/* Dump DMA registers */
	void (*dump_regs)(void __iomem *ioaddr, u32 *reg_space);
	void (*dma_rx_mode)(void __iomem *ioaddr, int mode, u32 channel,
			    int fifosz, u8 qmode);
	void (*dma_tx_mode)(void __iomem *ioaddr, int mode, u32 channel,
			    int fifosz, u8 qmode);
	/* To track extra statistic (if supported) */
	void (*dma_diagnostic_fr) (void *data, struct stmmac_extra_stats *x,
				   void __iomem *ioaddr);
	void (*enable_dma_transmission) (void __iomem *ioaddr);
	void (*enable_dma_irq)(void __iomem *ioaddr, u32 chan,
			       bool rx, bool tx);
	void (*disable_dma_irq)(void __iomem *ioaddr, u32 chan,
				bool rx, bool tx);
	void (*start_tx)(void __iomem *ioaddr, u32 chan);
	void (*stop_tx)(void __iomem *ioaddr, u32 chan);
	void (*start_rx)(void __iomem *ioaddr, u32 chan);
	void (*stop_rx)(void __iomem *ioaddr, u32 chan);
	int (*dma_interrupt) (void __iomem *ioaddr,
			      struct stmmac_extra_stats *x, u32 chan);
	/* If supported then get the optional core features */
	void (*get_hw_feature)(void __iomem *ioaddr,
			       struct dma_features *dma_cap);
	/* Program the HW RX Watchdog */
	void (*rx_watchdog)(void __iomem *ioaddr, u32 riwt, u32 number_chan);
	void (*set_tx_ring_len)(void __iomem *ioaddr, u32 len, u32 chan);
	void (*set_rx_ring_len)(void __iomem *ioaddr, u32 len, u32 chan);
	void (*set_rx_tail_ptr)(void __iomem *ioaddr, u32 tail_ptr, u32 chan);
	void (*set_tx_tail_ptr)(void __iomem *ioaddr, u32 tail_ptr, u32 chan);
	void (*enable_tso)(void __iomem *ioaddr, bool en, u32 chan);
	void (*qmode)(void __iomem *ioaddr, u32 channel, u8 qmode);
	void (*set_bfsize)(void __iomem *ioaddr, int bfsize, u32 chan);
	void (*enable_sph)(void __iomem *ioaddr, bool en, u32 chan);
	int (*enable_tbs)(void __iomem *ioaddr, bool en, u32 chan);
};

#define stmmac_reset(__priv, __args...) \
	stmmac_do_callback(__priv, dma, reset, __args)
#define stmmac_dma_init(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, init, __args)
#define stmmac_init_chan(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, init_chan, __args)
#define stmmac_init_rx_chan(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, init_rx_chan, __args)
#define stmmac_init_tx_chan(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, init_tx_chan, __args)
#define stmmac_axi(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, axi, __args)
#define stmmac_dump_dma_regs(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, dump_regs, __args)
#define stmmac_dma_rx_mode(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, dma_rx_mode, __args)
#define stmmac_dma_tx_mode(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, dma_tx_mode, __args)
#define stmmac_dma_diagnostic_fr(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, dma_diagnostic_fr, __args)
#define stmmac_enable_dma_transmission(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, enable_dma_transmission, __args)
#define stmmac_enable_dma_irq(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, enable_dma_irq, __args)
#define stmmac_disable_dma_irq(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, disable_dma_irq, __args)
#define stmmac_start_tx(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, start_tx, __args)
#define stmmac_stop_tx(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, stop_tx, __args)
#define stmmac_start_rx(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, start_rx, __args)
#define stmmac_stop_rx(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, stop_rx, __args)
#define stmmac_dma_interrupt_status(__priv, __args...) \
	stmmac_do_callback(__priv, dma, dma_interrupt, __args)
#define stmmac_get_hw_feature(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, get_hw_feature, __args)
#define stmmac_rx_watchdog(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, rx_watchdog, __args)
#define stmmac_set_tx_ring_len(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, set_tx_ring_len, __args)
#define stmmac_set_rx_ring_len(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, set_rx_ring_len, __args)
#define stmmac_set_rx_tail_ptr(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, set_rx_tail_ptr, __args)
#define stmmac_set_tx_tail_ptr(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, set_tx_tail_ptr, __args)
#define stmmac_enable_tso(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, enable_tso, __args)
#define stmmac_dma_qmode(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, qmode, __args)
#define stmmac_set_dma_bfsize(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, set_bfsize, __args)
#define stmmac_enable_sph(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, enable_sph, __args)
#define stmmac_enable_tbs(__priv, __args...) \
	stmmac_do_callback(__priv, dma, enable_tbs, __args)

struct mac_device_info;
struct net_device;
struct rgmii_adv;
struct stmmac_safety_stats;
struct stmmac_tc_entry;
struct stmmac_pps_cfg;
struct stmmac_rss;
struct stmmac_est;

/* Helpers to program the MAC core */
struct stmmac_ops {
	/* MAC core initialization */
	void (*core_init)(struct mac_device_info *hw, struct net_device *dev);
	/* Enable the MAC RX/TX */
	void (*set_mac)(void __iomem *ioaddr, bool enable);
	/* Enable and verify that the IPC module is supported */
	int (*rx_ipc)(struct mac_device_info *hw);
	/* Enable RX Queues */
	void (*rx_queue_enable)(struct mac_device_info *hw, u8 mode, u32 queue);
	/* RX Queues Priority */
	void (*rx_queue_prio)(struct mac_device_info *hw, u32 prio, u32 queue);
	/* TX Queues Priority */
	void (*tx_queue_prio)(struct mac_device_info *hw, u32 prio, u32 queue);
	/* RX Queues Routing */
	void (*rx_queue_routing)(struct mac_device_info *hw, u8 packet,
				 u32 queue);
	/* Program RX Algorithms */
	void (*prog_mtl_rx_algorithms)(struct mac_device_info *hw, u32 rx_alg);
	/* Program TX Algorithms */
	void (*prog_mtl_tx_algorithms)(struct mac_device_info *hw, u32 tx_alg);
	/* Set MTL TX queues weight */
	void (*set_mtl_tx_queue_weight)(struct mac_device_info *hw,
					u32 weight, u32 queue);
	/* RX MTL queue to RX dma mapping */
	void (*map_mtl_to_dma)(struct mac_device_info *hw, u32 queue, u32 chan);
	/* Configure AV Algorithm */
	void (*config_cbs)(struct mac_device_info *hw, u32 send_slope,
			   u32 idle_slope, u32 high_credit, u32 low_credit,
			   u32 queue);
	/* Dump MAC registers */
	void (*dump_regs)(struct mac_device_info *hw, u32 *reg_space);
	/* Handle extra events on specific interrupts hw dependent */
	int (*host_irq_status)(struct mac_device_info *hw,
			       struct stmmac_extra_stats *x);
	/* Handle MTL interrupts */
	int (*host_mtl_irq_status)(struct mac_device_info *hw, u32 chan);
	/* Multicast filter setting */
	void (*set_filter)(struct mac_device_info *hw, struct net_device *dev);
	/* Flow control setting */
	void (*flow_ctrl)(struct mac_device_info *hw, unsigned int duplex,
			  unsigned int fc, unsigned int pause_time, u32 tx_cnt);
	/* Set power management mode (e.g. magic frame) */
	void (*pmt)(struct mac_device_info *hw, unsigned long mode);
	/* Set/Get Unicast MAC addresses */
	void (*set_umac_addr)(struct mac_device_info *hw, unsigned char *addr,
			      unsigned int reg_n);
	void (*get_umac_addr)(struct mac_device_info *hw, unsigned char *addr,
			      unsigned int reg_n);
	void (*set_eee_mode)(struct mac_device_info *hw,
			     bool en_tx_lpi_clockgating);
	void (*reset_eee_mode)(struct mac_device_info *hw);
	void (*set_eee_timer)(struct mac_device_info *hw, int ls, int tw);
	void (*set_eee_pls)(struct mac_device_info *hw, int link);
	void (*debug)(void __iomem *ioaddr, struct stmmac_extra_stats *x,
		      u32 rx_queues, u32 tx_queues);
	/* PCS calls */
	void (*pcs_ctrl_ane)(void __iomem *ioaddr, bool ane, bool srgmi_ral,
			     bool loopback);
	void (*pcs_rane)(void __iomem *ioaddr, bool restart);
	void (*pcs_get_adv_lp)(void __iomem *ioaddr, struct rgmii_adv *adv);
	/* Safety Features */
	int (*safety_feat_config)(void __iomem *ioaddr, unsigned int asp);
	int (*safety_feat_irq_status)(struct net_device *ndev,
			void __iomem *ioaddr, unsigned int asp,
			struct stmmac_safety_stats *stats);
	int (*safety_feat_dump)(struct stmmac_safety_stats *stats,
			int index, unsigned long *count, const char **desc);
	/* Flexible RX Parser */
	int (*rxp_config)(void __iomem *ioaddr, struct stmmac_tc_entry *entries,
			  unsigned int count);
	/* Flexible PPS */
	int (*flex_pps_config)(void __iomem *ioaddr, int index,
			       struct stmmac_pps_cfg *cfg, bool enable,
			       u32 sub_second_inc, u32 systime_flags);
	/* Loopback for selftests */
	void (*set_mac_loopback)(void __iomem *ioaddr, bool enable);
	/* RSS */
	int (*rss_configure)(struct mac_device_info *hw,
			     struct stmmac_rss *cfg, u32 num_rxq);
	/* VLAN */
	void (*update_vlan_hash)(struct mac_device_info *hw, u32 hash,
				 __le16 perfect_match, bool is_double);
	void (*enable_vlan)(struct mac_device_info *hw, u32 type);
	int (*add_hw_vlan_rx_fltr)(struct net_device *dev,
				   struct mac_device_info *hw,
				   __be16 proto, u16 vid);
	int (*del_hw_vlan_rx_fltr)(struct net_device *dev,
				   struct mac_device_info *hw,
				   __be16 proto, u16 vid);
	void (*restore_hw_vlan_rx_fltr)(struct net_device *dev,
					struct mac_device_info *hw);
	/* TX Timestamp */
	int (*get_mac_tx_timestamp)(struct mac_device_info *hw, u64 *ts);
	/* Source Address Insertion / Replacement */
	void (*sarc_configure)(void __iomem *ioaddr, int val);
	/* Filtering */
	int (*config_l3_filter)(struct mac_device_info *hw, u32 filter_no,
				bool en, bool ipv6, bool sa, bool inv,
				u32 match);
	int (*config_l4_filter)(struct mac_device_info *hw, u32 filter_no,
				bool en, bool udp, bool sa, bool inv,
				u32 match);
	void (*set_arp_offload)(struct mac_device_info *hw, bool en, u32 addr);
	int (*est_configure)(void __iomem *ioaddr, struct stmmac_est *cfg,
			     unsigned int ptp_rate);
	void (*fpe_configure)(void __iomem *ioaddr, u32 num_txq, u32 num_rxq,
			      bool enable);
};

#define stmmac_core_init(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, core_init, __args)
#define stmmac_mac_set(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, set_mac, __args)
#define stmmac_rx_ipc(__priv, __args...) \
	stmmac_do_callback(__priv, mac, rx_ipc, __args)
#define stmmac_rx_queue_enable(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, rx_queue_enable, __args)
#define stmmac_rx_queue_prio(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, rx_queue_prio, __args)
#define stmmac_tx_queue_prio(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, tx_queue_prio, __args)
#define stmmac_rx_queue_routing(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, rx_queue_routing, __args)
#define stmmac_prog_mtl_rx_algorithms(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, prog_mtl_rx_algorithms, __args)
#define stmmac_prog_mtl_tx_algorithms(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, prog_mtl_tx_algorithms, __args)
#define stmmac_set_mtl_tx_queue_weight(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, set_mtl_tx_queue_weight, __args)
#define stmmac_map_mtl_to_dma(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, map_mtl_to_dma, __args)
#define stmmac_config_cbs(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, config_cbs, __args)
#define stmmac_dump_mac_regs(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, dump_regs, __args)
#define stmmac_host_irq_status(__priv, __args...) \
	stmmac_do_callback(__priv, mac, host_irq_status, __args)
#define stmmac_host_mtl_irq_status(__priv, __args...) \
	stmmac_do_callback(__priv, mac, host_mtl_irq_status, __args)
#define stmmac_set_filter(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, set_filter, __args)
#define stmmac_flow_ctrl(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, flow_ctrl, __args)
#define stmmac_pmt(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, pmt, __args)
#define stmmac_set_umac_addr(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, set_umac_addr, __args)
#define stmmac_get_umac_addr(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, get_umac_addr, __args)
#define stmmac_set_eee_mode(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, set_eee_mode, __args)
#define stmmac_reset_eee_mode(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, reset_eee_mode, __args)
#define stmmac_set_eee_timer(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, set_eee_timer, __args)
#define stmmac_set_eee_pls(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, set_eee_pls, __args)
#define stmmac_mac_debug(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, debug, __args)
#define stmmac_pcs_ctrl_ane(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, pcs_ctrl_ane, __args)
#define stmmac_pcs_rane(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, pcs_rane, __args)
#define stmmac_pcs_get_adv_lp(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, pcs_get_adv_lp, __args)
#define stmmac_safety_feat_config(__priv, __args...) \
	stmmac_do_callback(__priv, mac, safety_feat_config, __args)
#define stmmac_safety_feat_irq_status(__priv, __args...) \
	stmmac_do_callback(__priv, mac, safety_feat_irq_status, __args)
#define stmmac_safety_feat_dump(__priv, __args...) \
	stmmac_do_callback(__priv, mac, safety_feat_dump, __args)
#define stmmac_rxp_config(__priv, __args...) \
	stmmac_do_callback(__priv, mac, rxp_config, __args)
#define stmmac_flex_pps_config(__priv, __args...) \
	stmmac_do_callback(__priv, mac, flex_pps_config, __args)
#define stmmac_set_mac_loopback(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, set_mac_loopback, __args)
#define stmmac_rss_configure(__priv, __args...) \
	stmmac_do_callback(__priv, mac, rss_configure, __args)
#define stmmac_update_vlan_hash(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, update_vlan_hash, __args)
#define stmmac_enable_vlan(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, enable_vlan, __args)
#define stmmac_add_hw_vlan_rx_fltr(__priv, __args...) \
	stmmac_do_callback(__priv, mac, add_hw_vlan_rx_fltr, __args)
#define stmmac_del_hw_vlan_rx_fltr(__priv, __args...) \
	stmmac_do_callback(__priv, mac, del_hw_vlan_rx_fltr, __args)
#define stmmac_restore_hw_vlan_rx_fltr(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, restore_hw_vlan_rx_fltr, __args)
#define stmmac_get_mac_tx_timestamp(__priv, __args...) \
	stmmac_do_callback(__priv, mac, get_mac_tx_timestamp, __args)
#define stmmac_sarc_configure(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, sarc_configure, __args)
#define stmmac_config_l3_filter(__priv, __args...) \
	stmmac_do_callback(__priv, mac, config_l3_filter, __args)
#define stmmac_config_l4_filter(__priv, __args...) \
	stmmac_do_callback(__priv, mac, config_l4_filter, __args)
#define stmmac_set_arp_offload(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, set_arp_offload, __args)
#define stmmac_est_configure(__priv, __args...) \
	stmmac_do_callback(__priv, mac, est_configure, __args)
#define stmmac_fpe_configure(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, fpe_configure, __args)

/* PTP and HW Timer helpers */
struct stmmac_hwtimestamp {
	void (*config_hw_tstamping) (void __iomem *ioaddr, u32 data);
	void (*config_sub_second_increment)(void __iomem *ioaddr, u32 ptp_clock,
					   int gmac4, u32 *ssinc);
	int (*init_systime) (void __iomem *ioaddr, u32 sec, u32 nsec);
	int (*config_addend) (void __iomem *ioaddr, u32 addend);
	int (*adjust_systime) (void __iomem *ioaddr, u32 sec, u32 nsec,
			       int add_sub, int gmac4);
	void (*get_systime) (void __iomem *ioaddr, u64 *systime);
};

#define stmmac_config_hw_tstamping(__priv, __args...) \
	stmmac_do_void_callback(__priv, ptp, config_hw_tstamping, __args)
#define stmmac_config_sub_second_increment(__priv, __args...) \
	stmmac_do_void_callback(__priv, ptp, config_sub_second_increment, __args)
#define stmmac_init_systime(__priv, __args...) \
	stmmac_do_callback(__priv, ptp, init_systime, __args)
#define stmmac_config_addend(__priv, __args...) \
	stmmac_do_callback(__priv, ptp, config_addend, __args)
#define stmmac_adjust_systime(__priv, __args...) \
	stmmac_do_callback(__priv, ptp, adjust_systime, __args)
#define stmmac_get_systime(__priv, __args...) \
	stmmac_do_void_callback(__priv, ptp, get_systime, __args)

/* Helpers to manage the descriptors for chain and ring modes */
struct stmmac_mode_ops {
	void (*init) (void *des, dma_addr_t phy_addr, unsigned int size,
		      unsigned int extend_desc);
	unsigned int (*is_jumbo_frm) (int len, int ehn_desc);
	int (*jumbo_frm)(void *priv, struct sk_buff *skb, int csum);
	int (*set_16kib_bfsize)(int mtu);
	void (*init_desc3)(struct dma_desc *p);
	void (*refill_desc3) (void *priv, struct dma_desc *p);
	void (*clean_desc3) (void *priv, struct dma_desc *p);
};

#define stmmac_mode_init(__priv, __args...) \
	stmmac_do_void_callback(__priv, mode, init, __args)
#define stmmac_is_jumbo_frm(__priv, __args...) \
	stmmac_do_callback(__priv, mode, is_jumbo_frm, __args)
#define stmmac_jumbo_frm(__priv, __args...) \
	stmmac_do_callback(__priv, mode, jumbo_frm, __args)
#define stmmac_set_16kib_bfsize(__priv, __args...) \
	stmmac_do_callback(__priv, mode, set_16kib_bfsize, __args)
#define stmmac_init_desc3(__priv, __args...) \
	stmmac_do_void_callback(__priv, mode, init_desc3, __args)
#define stmmac_refill_desc3(__priv, __args...) \
	stmmac_do_void_callback(__priv, mode, refill_desc3, __args)
#define stmmac_clean_desc3(__priv, __args...) \
	stmmac_do_void_callback(__priv, mode, clean_desc3, __args)

struct stmmac_priv;
struct tc_cls_u32_offload;
struct tc_cbs_qopt_offload;
struct flow_cls_offload;
struct tc_taprio_qopt_offload;
struct tc_etf_qopt_offload;

struct stmmac_tc_ops {
	int (*init)(struct stmmac_priv *priv);
	int (*setup_cls_u32)(struct stmmac_priv *priv,
			     struct tc_cls_u32_offload *cls);
	int (*setup_cbs)(struct stmmac_priv *priv,
			 struct tc_cbs_qopt_offload *qopt);
	int (*setup_cls)(struct stmmac_priv *priv,
			 struct flow_cls_offload *cls);
	int (*setup_taprio)(struct stmmac_priv *priv,
			    struct tc_taprio_qopt_offload *qopt);
	int (*setup_etf)(struct stmmac_priv *priv,
			 struct tc_etf_qopt_offload *qopt);
};

#define stmmac_tc_init(__priv, __args...) \
	stmmac_do_callback(__priv, tc, init, __args)
#define stmmac_tc_setup_cls_u32(__priv, __args...) \
	stmmac_do_callback(__priv, tc, setup_cls_u32, __args)
#define stmmac_tc_setup_cbs(__priv, __args...) \
	stmmac_do_callback(__priv, tc, setup_cbs, __args)
#define stmmac_tc_setup_cls(__priv, __args...) \
	stmmac_do_callback(__priv, tc, setup_cls, __args)
#define stmmac_tc_setup_taprio(__priv, __args...) \
	stmmac_do_callback(__priv, tc, setup_taprio, __args)
#define stmmac_tc_setup_etf(__priv, __args...) \
	stmmac_do_callback(__priv, tc, setup_etf, __args)

struct stmmac_counters;

struct stmmac_mmc_ops {
	void (*ctrl)(void __iomem *ioaddr, unsigned int mode);
	void (*intr_all_mask)(void __iomem *ioaddr);
	void (*read)(void __iomem *ioaddr, struct stmmac_counters *mmc);
};

#define stmmac_mmc_ctrl(__priv, __args...) \
	stmmac_do_void_callback(__priv, mmc, ctrl, __args)
#define stmmac_mmc_intr_all_mask(__priv, __args...) \
	stmmac_do_void_callback(__priv, mmc, intr_all_mask, __args)
#define stmmac_mmc_read(__priv, __args...) \
	stmmac_do_void_callback(__priv, mmc, read, __args)

/* XPCS callbacks */
#define stmmac_xpcs_validate(__priv, __args...) \
	stmmac_do_callback(__priv, xpcs, validate, __args)
#define stmmac_xpcs_config(__priv, __args...) \
	stmmac_do_callback(__priv, xpcs, config, __args)
#define stmmac_xpcs_get_state(__priv, __args...) \
	stmmac_do_callback(__priv, xpcs, get_state, __args)
#define stmmac_xpcs_link_up(__priv, __args...) \
	stmmac_do_callback(__priv, xpcs, link_up, __args)
#define stmmac_xpcs_probe(__priv, __args...) \
	stmmac_do_callback(__priv, xpcs, probe, __args)

struct stmmac_regs_off {
	u32 ptp_off;
	u32 mmc_off;
};

extern const struct stmmac_ops dwmac100_ops;
extern const struct stmmac_dma_ops dwmac100_dma_ops;
extern const struct stmmac_ops dwmac1000_ops;
extern const struct stmmac_dma_ops dwmac1000_dma_ops;
extern const struct stmmac_ops dwmac4_ops;
extern const struct stmmac_dma_ops dwmac4_dma_ops;
extern const struct stmmac_ops dwmac410_ops;
extern const struct stmmac_dma_ops dwmac410_dma_ops;
extern const struct stmmac_ops dwmac510_ops;
extern const struct stmmac_tc_ops dwmac510_tc_ops;
extern const struct stmmac_ops dwxgmac210_ops;
extern const struct stmmac_ops dwxlgmac2_ops;
extern const struct stmmac_dma_ops dwxgmac210_dma_ops;
extern const struct stmmac_desc_ops dwxgmac210_desc_ops;
extern const struct stmmac_mmc_ops dwmac_mmc_ops;
extern const struct stmmac_mmc_ops dwxgmac_mmc_ops;

#define GMAC_VERSION		0x00000020	/* GMAC CORE Version */
#define GMAC4_VERSION		0x00000110	/* GMAC4+ CORE Version */

int stmmac_hwif_init(struct stmmac_priv *priv);

#endif /* __STMMAC_HWIF_H__ */

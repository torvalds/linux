/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2024 Hisilicon Limited. */

#ifndef __HBG_COMMON_H
#define __HBG_COMMON_H

#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include "hbg_reg.h"

#define HBG_STATUS_DISABLE		0x0
#define HBG_STATUS_ENABLE		0x1
#define HBG_RX_SKIP1			0x00
#define HBG_RX_SKIP2			0x01
#define HBG_VECTOR_NUM			4
#define HBG_PCU_CACHE_LINE_SIZE		32
#define HBG_TX_TIMEOUT_BUF_LEN		1024
#define HBG_RX_DESCR			0x01

#define HBG_PACKET_HEAD_SIZE	((HBG_RX_SKIP1 + HBG_RX_SKIP2 + \
				  HBG_RX_DESCR) * HBG_PCU_CACHE_LINE_SIZE)

enum hbg_dir {
	HBG_DIR_TX = 1 << 0,
	HBG_DIR_RX = 1 << 1,
	HBG_DIR_TX_RX = HBG_DIR_TX | HBG_DIR_RX,
};

enum hbg_tx_state {
	HBG_TX_STATE_COMPLETE = 0, /* clear state, must fix to 0 */
	HBG_TX_STATE_START,
};

enum hbg_nic_state {
	HBG_NIC_STATE_EVENT_HANDLING = 0,
	HBG_NIC_STATE_RESETTING,
	HBG_NIC_STATE_RESET_FAIL,
	HBG_NIC_STATE_NEED_RESET, /* trigger a reset in scheduled task */
	HBG_NIC_STATE_NP_LINK_FAIL,
};

enum hbg_reset_type {
	HBG_RESET_TYPE_NONE = 0,
	HBG_RESET_TYPE_FLR,
	HBG_RESET_TYPE_FUNCTION,
};

struct hbg_buffer {
	u32 state;
	dma_addr_t state_dma;

	struct sk_buff *skb;
	dma_addr_t skb_dma;
	u32 skb_len;

	enum hbg_dir dir;
	struct hbg_ring *ring;
	struct hbg_priv *priv;
};

struct hbg_ring {
	struct hbg_buffer *queue;
	dma_addr_t queue_dma;

	union {
		u32 head;
		u32 ntc;
	};
	union {
		u32 tail;
		u32 ntu;
	};
	u32 len;

	enum hbg_dir dir;
	struct hbg_priv *priv;
	struct napi_struct napi;
	char *tout_log_buf; /* tx timeout log buffer */
};

enum hbg_hw_event_type {
	HBG_HW_EVENT_NONE = 0,
	HBG_HW_EVENT_INIT, /* driver is loading */
	HBG_HW_EVENT_RESET,
	HBG_HW_EVENT_CORE_RESET,
};

struct hbg_dev_specs {
	u32 mac_id;
	struct sockaddr mac_addr;
	u32 phy_addr;
	u32 mdio_frequency;
	u32 rx_fifo_num;
	u32 tx_fifo_num;
	u32 vlan_layers;
	u32 max_mtu;
	u32 min_mtu;
	u32 uc_mac_num;

	u32 max_frame_len;
	u32 rx_buf_size;
};

struct hbg_irq_info {
	const char *name;
	u32 mask;
	bool re_enable;
	bool need_print;
	bool need_reset;
	u64 count;

	void (*irq_handle)(struct hbg_priv *priv, struct hbg_irq_info *info);
};

struct hbg_vector {
	char name[HBG_VECTOR_NUM][32];
	struct hbg_irq_info *info_array;
	u32 info_array_len;
};

struct hbg_mac {
	struct mii_bus *mdio_bus;
	struct phy_device *phydev;
	u8 phy_addr;

	u32 speed;
	u32 duplex;
	u32 autoneg;
	u32 link_status;
	u32 pause_autoneg;
};

struct hbg_mac_table_entry {
	u8 addr[ETH_ALEN];
};

struct hbg_mac_filter {
	struct hbg_mac_table_entry *mac_table;
	u32 table_max_len;
	bool enabled;
};

/* saved for restore after rest */
struct hbg_user_def {
	struct ethtool_pauseparam pause_param;
};

struct hbg_stats {
	u64 rx_desc_drop;
	u64 rx_desc_l2_err_cnt;
	u64 rx_desc_pkt_len_err_cnt;
	u64 rx_desc_l3l4_err_cnt;
	u64 rx_desc_l3_wrong_head_cnt;
	u64 rx_desc_l3_csum_err_cnt;
	u64 rx_desc_l3_len_err_cnt;
	u64 rx_desc_l3_zero_ttl_cnt;
	u64 rx_desc_l3_other_cnt;
	u64 rx_desc_l4_err_cnt;
	u64 rx_desc_l4_wrong_head_cnt;
	u64 rx_desc_l4_len_err_cnt;
	u64 rx_desc_l4_csum_err_cnt;
	u64 rx_desc_l4_zero_port_num_cnt;
	u64 rx_desc_l4_other_cnt;
	u64 rx_desc_frag_cnt;
	u64 rx_desc_ip_ver_err_cnt;
	u64 rx_desc_ipv4_pkt_cnt;
	u64 rx_desc_ipv6_pkt_cnt;
	u64 rx_desc_no_ip_pkt_cnt;
	u64 rx_desc_ip_pkt_cnt;
	u64 rx_desc_tcp_pkt_cnt;
	u64 rx_desc_udp_pkt_cnt;
	u64 rx_desc_vlan_pkt_cnt;
	u64 rx_desc_icmp_pkt_cnt;
	u64 rx_desc_arp_pkt_cnt;
	u64 rx_desc_rarp_pkt_cnt;
	u64 rx_desc_multicast_pkt_cnt;
	u64 rx_desc_broadcast_pkt_cnt;
	u64 rx_desc_ipsec_pkt_cnt;
	u64 rx_desc_ip_opt_pkt_cnt;
	u64 rx_desc_key_not_match_cnt;

	u64 rx_octets_total_ok_cnt;
	u64 rx_uc_pkt_cnt;
	u64 rx_mc_pkt_cnt;
	u64 rx_bc_pkt_cnt;
	u64 rx_vlan_pkt_cnt;
	u64 rx_octets_bad_cnt;
	u64 rx_octets_total_filt_cnt;
	u64 rx_filt_pkt_cnt;
	u64 rx_trans_pkt_cnt;
	u64 rx_framesize_64;
	u64 rx_framesize_65_127;
	u64 rx_framesize_128_255;
	u64 rx_framesize_256_511;
	u64 rx_framesize_512_1023;
	u64 rx_framesize_1024_1518;
	u64 rx_framesize_bt_1518;
	u64 rx_fcs_error_cnt;
	u64 rx_data_error_cnt;
	u64 rx_align_error_cnt;
	u64 rx_pause_macctl_frame_cnt;
	u64 rx_unknown_macctl_frame_cnt;
	/* crc ok, > max_frm_size, < 2max_frm_size */
	u64 rx_frame_long_err_cnt;
	/* crc fail, > max_frm_size, < 2max_frm_size */
	u64 rx_jabber_err_cnt;
	/* > 2max_frm_size */
	u64 rx_frame_very_long_err_cnt;
	/* < 64byte, >= short_runts_thr */
	u64 rx_frame_runt_err_cnt;
	/* < short_runts_thr */
	u64 rx_frame_short_err_cnt;
	/* PCU: dropped when the RX FIFO is full.*/
	u64 rx_overflow_cnt;
	/* GMAC: the count of overflows of the RX FIFO */
	u64 rx_overrun_cnt;
	/* PCU: the count of buffer alloc errors in RX */
	u64 rx_bufrq_err_cnt;
	/* PCU: the count of write descriptor errors in RX */
	u64 rx_we_err_cnt;
	/* GMAC: the count of pkts that contain PAD but length is not 64 */
	u64 rx_lengthfield_err_cnt;
	u64 rx_fail_comma_cnt;

	u64 rx_dma_err_cnt;
	u64 rx_fifo_less_empty_thrsld_cnt;

	u64 tx_octets_total_ok_cnt;
	u64 tx_uc_pkt_cnt;
	u64 tx_mc_pkt_cnt;
	u64 tx_bc_pkt_cnt;
	u64 tx_vlan_pkt_cnt;
	u64 tx_octets_bad_cnt;
	u64 tx_trans_pkt_cnt;
	u64 tx_pause_frame_cnt;
	u64 tx_framesize_64;
	u64 tx_framesize_65_127;
	u64 tx_framesize_128_255;
	u64 tx_framesize_256_511;
	u64 tx_framesize_512_1023;
	u64 tx_framesize_1024_1518;
	u64 tx_framesize_bt_1518;
	/* GMAC: the count of times that frames fail to be transmitted
	 *       due to internal errors.
	 */
	u64 tx_underrun_err_cnt;
	u64 tx_add_cs_fail_cnt;
	/* PCU: the count of buffer free errors in TX */
	u64 tx_bufrl_err_cnt;
	u64 tx_crc_err_cnt;
	u64 tx_drop_cnt;
	u64 tx_excessive_length_drop_cnt;

	u64 tx_timeout_cnt;
	u64 tx_dma_err_cnt;

	u64 np_link_fail_cnt;
};

struct hbg_priv {
	struct net_device *netdev;
	struct pci_dev *pdev;
	u8 __iomem *io_base;
	struct hbg_dev_specs dev_specs;
	unsigned long state;
	struct hbg_mac mac;
	struct hbg_vector vectors;
	struct hbg_ring tx_ring;
	struct hbg_ring rx_ring;
	struct hbg_mac_filter filter;
	enum hbg_reset_type reset_type;
	struct hbg_user_def user_def;
	struct hbg_stats stats;
	unsigned long last_update_stats_time;
	struct delayed_work service_task;
};

void hbg_err_reset_task_schedule(struct hbg_priv *priv);
void hbg_np_link_fail_task_schedule(struct hbg_priv *priv);

#endif

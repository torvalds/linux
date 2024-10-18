/* SPDX-License-Identifier: GPL-2.0-only */
/* Atlantic Network Driver
 *
 * Copyright (C) 2014-2019 aQuantia Corporation
 * Copyright (C) 2019-2020 Marvell International Ltd.
 */

/* File hw_atl_llh.h: Declarations of bitfield and register access functions for
 * Atlantic registers.
 */

#ifndef HW_ATL_LLH_H
#define HW_ATL_LLH_H

#include <linux/types.h>

struct aq_hw_s;

/* set temperature sense reset */
void hw_atl_ts_reset_set(struct aq_hw_s *aq_hw, u32 val);

/* set temperature sense power down */
void hw_atl_ts_power_down_set(struct aq_hw_s *aq_hw, u32 val);

/* get temperature sense power down */
u32 hw_atl_ts_power_down_get(struct aq_hw_s *aq_hw);

/* get temperature sense ready */
u32 hw_atl_ts_ready_get(struct aq_hw_s *aq_hw);

/* get temperature sense ready latch high */
u32 hw_atl_ts_ready_latch_high_get(struct aq_hw_s *aq_hw);

/* get temperature sense data */
u32 hw_atl_ts_data_get(struct aq_hw_s *aq_hw);

/* SMBUS0 bus busy */
u32 hw_atl_smb0_bus_busy_get(struct aq_hw_s *aq_hw);

/* SMBUS0 byte transfer complete */
u32 hw_atl_smb0_byte_transfer_complete_get(struct aq_hw_s *aq_hw);

/* SMBUS0 receive acknowledged */
u32 hw_atl_smb0_receive_acknowledged_get(struct aq_hw_s *aq_hw);

/* SMBUS0 set transmitted data (only leftmost byte of data valid) */
void hw_atl_smb0_tx_data_set(struct aq_hw_s *aq_hw, u32 data);

/* SMBUS0 provisioning2 command register */
void hw_atl_smb0_provisioning2_set(struct aq_hw_s *aq_hw, u32 data);

/* SMBUS0 repeated start detect */
u32 hw_atl_smb0_repeated_start_detect_get(struct aq_hw_s *aq_hw);

/* SMBUS0 received data register */
u32 hw_atl_smb0_rx_data_get(struct aq_hw_s *aq_hw);

/* global */

/* set global microprocessor semaphore */
void hw_atl_reg_glb_cpu_sem_set(struct aq_hw_s *aq_hw,	u32 glb_cpu_sem,
				u32 semaphore);

/* get global microprocessor semaphore */
u32 hw_atl_reg_glb_cpu_sem_get(struct aq_hw_s *aq_hw, u32 semaphore);

/* set global register reset disable */
void hw_atl_glb_glb_reg_res_dis_set(struct aq_hw_s *aq_hw, u32 glb_reg_res_dis);

/* set soft reset */
void hw_atl_glb_soft_res_set(struct aq_hw_s *aq_hw, u32 soft_res);

/* get soft reset */
u32 hw_atl_glb_soft_res_get(struct aq_hw_s *aq_hw);

/* stats */

u32 hw_atl_rpb_rx_dma_drop_pkt_cnt_get(struct aq_hw_s *aq_hw);

/* get rx dma good octet counter */
u64 hw_atl_stats_rx_dma_good_octet_counter_get(struct aq_hw_s *aq_hw);

/* get rx dma good packet counter */
u64 hw_atl_stats_rx_dma_good_pkt_counter_get(struct aq_hw_s *aq_hw);

/* get tx dma good octet counter */
u64 hw_atl_stats_tx_dma_good_octet_counter_get(struct aq_hw_s *aq_hw);

/* get tx dma good packet counter */
u64 hw_atl_stats_tx_dma_good_pkt_counter_get(struct aq_hw_s *aq_hw);

/* get msm rx errors counter register */
u32 hw_atl_reg_mac_msm_rx_errs_cnt_get(struct aq_hw_s *aq_hw);

/* get msm rx unicast frames counter register */
u32 hw_atl_reg_mac_msm_rx_ucst_frm_cnt_get(struct aq_hw_s *aq_hw);

/* get msm rx multicast frames counter register */
u32 hw_atl_reg_mac_msm_rx_mcst_frm_cnt_get(struct aq_hw_s *aq_hw);

/* get msm rx broadcast frames counter register */
u32 hw_atl_reg_mac_msm_rx_bcst_frm_cnt_get(struct aq_hw_s *aq_hw);

/* get msm rx broadcast octets counter register 1 */
u32 hw_atl_reg_mac_msm_rx_bcst_octets_counter1get(struct aq_hw_s *aq_hw);

/* get msm rx unicast octets counter register 0 */
u32 hw_atl_reg_mac_msm_rx_ucst_octets_counter0get(struct aq_hw_s *aq_hw);

/* get msm tx errors counter register */
u32 hw_atl_reg_mac_msm_tx_errs_cnt_get(struct aq_hw_s *aq_hw);

/* get msm tx unicast frames counter register */
u32 hw_atl_reg_mac_msm_tx_ucst_frm_cnt_get(struct aq_hw_s *aq_hw);

/* get msm tx multicast frames counter register */
u32 hw_atl_reg_mac_msm_tx_mcst_frm_cnt_get(struct aq_hw_s *aq_hw);

/* get msm tx broadcast frames counter register */
u32 hw_atl_reg_mac_msm_tx_bcst_frm_cnt_get(struct aq_hw_s *aq_hw);

/* get msm tx multicast octets counter register 1 */
u32 hw_atl_reg_mac_msm_tx_mcst_octets_counter1get(struct aq_hw_s *aq_hw);

/* get msm tx broadcast octets counter register 1 */
u32 hw_atl_reg_mac_msm_tx_bcst_octets_counter1get(struct aq_hw_s *aq_hw);

/* get msm tx unicast octets counter register 0 */
u32 hw_atl_reg_mac_msm_tx_ucst_octets_counter0get(struct aq_hw_s *aq_hw);

/* get global mif identification */
u32 hw_atl_reg_glb_mif_id_get(struct aq_hw_s *aq_hw);

/* interrupt */

/* set interrupt auto mask lsw */
void hw_atl_itr_irq_auto_masklsw_set(struct aq_hw_s *aq_hw,
				     u32 irq_auto_masklsw);

/* set interrupt mapping enable rx */
void hw_atl_itr_irq_map_en_rx_set(struct aq_hw_s *aq_hw, u32 irq_map_en_rx,
				  u32 rx);

/* set interrupt mapping enable tx */
void hw_atl_itr_irq_map_en_tx_set(struct aq_hw_s *aq_hw, u32 irq_map_en_tx,
				  u32 tx);

/* set interrupt mapping rx */
void hw_atl_itr_irq_map_rx_set(struct aq_hw_s *aq_hw, u32 irq_map_rx, u32 rx);

/* set interrupt mapping tx */
void hw_atl_itr_irq_map_tx_set(struct aq_hw_s *aq_hw, u32 irq_map_tx, u32 tx);

/* set interrupt mask clear lsw */
void hw_atl_itr_irq_msk_clearlsw_set(struct aq_hw_s *aq_hw,
				     u32 irq_msk_clearlsw);

/* set interrupt mask set lsw */
void hw_atl_itr_irq_msk_setlsw_set(struct aq_hw_s *aq_hw, u32 irq_msk_setlsw);

/* set interrupt register reset disable */
void hw_atl_itr_irq_reg_res_dis_set(struct aq_hw_s *aq_hw, u32 irq_reg_res_dis);

/* set interrupt status clear lsw */
void hw_atl_itr_irq_status_clearlsw_set(struct aq_hw_s *aq_hw,
					u32 irq_status_clearlsw);

/* get interrupt status lsw */
u32 hw_atl_itr_irq_statuslsw_get(struct aq_hw_s *aq_hw);

/* get reset interrupt */
u32 hw_atl_itr_res_irq_get(struct aq_hw_s *aq_hw);

/* set reset interrupt */
void hw_atl_itr_res_irq_set(struct aq_hw_s *aq_hw, u32 res_irq);

/* set RSC interrupt */
void hw_atl_itr_rsc_en_set(struct aq_hw_s *aq_hw, u32 enable);

/* set RSC delay */
void hw_atl_itr_rsc_delay_set(struct aq_hw_s *aq_hw, u32 delay);

/* rdm */

/* set cpu id */
void hw_atl_rdm_cpu_id_set(struct aq_hw_s *aq_hw, u32 cpuid, u32 dca);

/* set rx dca enable */
void hw_atl_rdm_rx_dca_en_set(struct aq_hw_s *aq_hw, u32 rx_dca_en);

/* set rx dca mode */
void hw_atl_rdm_rx_dca_mode_set(struct aq_hw_s *aq_hw, u32 rx_dca_mode);

/* set rx descriptor data buffer size */
void hw_atl_rdm_rx_desc_data_buff_size_set(struct aq_hw_s *aq_hw,
					   u32 rx_desc_data_buff_size,
				    u32 descriptor);

/* set rx descriptor dca enable */
void hw_atl_rdm_rx_desc_dca_en_set(struct aq_hw_s *aq_hw, u32 rx_desc_dca_en,
				   u32 dca);

/* set rx descriptor enable */
void hw_atl_rdm_rx_desc_en_set(struct aq_hw_s *aq_hw, u32 rx_desc_en,
			       u32 descriptor);

/* set rx descriptor header splitting */
void hw_atl_rdm_rx_desc_head_splitting_set(struct aq_hw_s *aq_hw,
					   u32 rx_desc_head_splitting,
				    u32 descriptor);

/* get rx descriptor head pointer */
u32 hw_atl_rdm_rx_desc_head_ptr_get(struct aq_hw_s *aq_hw, u32 descriptor);

/* set rx descriptor length */
void hw_atl_rdm_rx_desc_len_set(struct aq_hw_s *aq_hw, u32 rx_desc_len,
				u32 descriptor);

/* set rx descriptor write-back interrupt enable */
void hw_atl_rdm_rx_desc_wr_wb_irq_en_set(struct aq_hw_s *aq_hw,
					 u32 rx_desc_wr_wb_irq_en);

/* set rx header dca enable */
void hw_atl_rdm_rx_head_dca_en_set(struct aq_hw_s *aq_hw, u32 rx_head_dca_en,
				   u32 dca);

/* set rx payload dca enable */
void hw_atl_rdm_rx_pld_dca_en_set(struct aq_hw_s *aq_hw, u32 rx_pld_dca_en,
				  u32 dca);

/* set rx descriptor header buffer size */
void hw_atl_rdm_rx_desc_head_buff_size_set(struct aq_hw_s *aq_hw,
					   u32 rx_desc_head_buff_size,
					   u32 descriptor);

/* set rx descriptor reset */
void hw_atl_rdm_rx_desc_res_set(struct aq_hw_s *aq_hw, u32 rx_desc_res,
				u32 descriptor);

/* Set RDM Interrupt Moderation Enable */
void hw_atl_rdm_rdm_intr_moder_en_set(struct aq_hw_s *aq_hw,
				      u32 rdm_intr_moder_en);

/* reg */

/* set general interrupt mapping register */
void hw_atl_reg_gen_irq_map_set(struct aq_hw_s *aq_hw, u32 gen_intr_map,
				u32 regidx);

/* get general interrupt status register */
u32 hw_atl_reg_gen_irq_status_get(struct aq_hw_s *aq_hw);

/* set interrupt global control register */
void hw_atl_reg_irq_glb_ctl_set(struct aq_hw_s *aq_hw, u32 intr_glb_ctl);

/* set interrupt throttle register */
void hw_atl_reg_irq_thr_set(struct aq_hw_s *aq_hw, u32 intr_thr, u32 throttle);

/* set rx dma descriptor base address lsw */
void hw_atl_reg_rx_dma_desc_base_addresslswset(struct aq_hw_s *aq_hw,
					       u32 rx_dma_desc_base_addrlsw,
					u32 descriptor);

/* set rx dma descriptor base address msw */
void hw_atl_reg_rx_dma_desc_base_addressmswset(struct aq_hw_s *aq_hw,
					       u32 rx_dma_desc_base_addrmsw,
					u32 descriptor);

/* get rx dma descriptor status register */
u32 hw_atl_reg_rx_dma_desc_status_get(struct aq_hw_s *aq_hw, u32 descriptor);

/* set rx dma descriptor tail pointer register */
void hw_atl_reg_rx_dma_desc_tail_ptr_set(struct aq_hw_s *aq_hw,
					 u32 rx_dma_desc_tail_ptr,
				  u32 descriptor);

/* set rx filter multicast filter mask register */
void hw_atl_reg_rx_flr_mcst_flr_msk_set(struct aq_hw_s *aq_hw,
					u32 rx_flr_mcst_flr_msk);

/* set rx filter multicast filter register */
void hw_atl_reg_rx_flr_mcst_flr_set(struct aq_hw_s *aq_hw, u32 rx_flr_mcst_flr,
				    u32 filter);

/* set rx filter rss control register 1 */
void hw_atl_reg_rx_flr_rss_control1set(struct aq_hw_s *aq_hw,
				       u32 rx_flr_rss_control1);

/* Set RX Filter Control Register 2 */
void hw_atl_reg_rx_flr_control2_set(struct aq_hw_s *aq_hw, u32 rx_flr_control2);

/* Set RX Interrupt Moderation Control Register */
void hw_atl_reg_rx_intr_moder_ctrl_set(struct aq_hw_s *aq_hw,
				       u32 rx_intr_moderation_ctl,
				u32 queue);

/* set tx dma debug control */
void hw_atl_reg_tx_dma_debug_ctl_set(struct aq_hw_s *aq_hw,
				     u32 tx_dma_debug_ctl);

/* set tx dma descriptor base address lsw */
void hw_atl_reg_tx_dma_desc_base_addresslswset(struct aq_hw_s *aq_hw,
					       u32 tx_dma_desc_base_addrlsw,
					u32 descriptor);

/* set tx dma descriptor base address msw */
void hw_atl_reg_tx_dma_desc_base_addressmswset(struct aq_hw_s *aq_hw,
					       u32 tx_dma_desc_base_addrmsw,
					u32 descriptor);

/* set tx dma descriptor tail pointer register */
void hw_atl_reg_tx_dma_desc_tail_ptr_set(struct aq_hw_s *aq_hw,
					 u32 tx_dma_desc_tail_ptr,
					 u32 descriptor);

/* Set TX Interrupt Moderation Control Register */
void hw_atl_reg_tx_intr_moder_ctrl_set(struct aq_hw_s *aq_hw,
				       u32 tx_intr_moderation_ctl,
				       u32 queue);

/* set global microprocessor scratch pad */
void hw_atl_reg_glb_cpu_scratch_scp_set(struct aq_hw_s *aq_hw,
					u32 glb_cpu_scratch_scp,
					u32 scratch_scp);

/* rpb */

/* set dma system loopback */
void hw_atl_rpb_dma_sys_lbk_set(struct aq_hw_s *aq_hw, u32 dma_sys_lbk);

/* set dma network loopback */
void hw_atl_rpb_dma_net_lbk_set(struct aq_hw_s *aq_hw, u32 dma_net_lbk);

/* set rx traffic class mode */
void hw_atl_rpb_rpf_rx_traf_class_mode_set(struct aq_hw_s *aq_hw,
					   u32 rx_traf_class_mode);

/* get rx traffic class mode */
u32 hw_atl_rpb_rpf_rx_traf_class_mode_get(struct aq_hw_s *aq_hw);

/* set rx buffer enable */
void hw_atl_rpb_rx_buff_en_set(struct aq_hw_s *aq_hw, u32 rx_buff_en);

/* set rx buffer high threshold (per tc) */
void hw_atl_rpb_rx_buff_hi_threshold_per_tc_set(struct aq_hw_s *aq_hw,
						u32 rx_buff_hi_threshold_per_tc,
						u32 buffer);

/* set rx buffer low threshold (per tc) */
void hw_atl_rpb_rx_buff_lo_threshold_per_tc_set(struct aq_hw_s *aq_hw,
						u32 rx_buff_lo_threshold_per_tc,
					 u32 buffer);

/* set rx flow control mode */
void hw_atl_rpb_rx_flow_ctl_mode_set(struct aq_hw_s *aq_hw,
				     u32 rx_flow_ctl_mode);

/* set rx packet buffer size (per tc) */
void hw_atl_rpb_rx_pkt_buff_size_per_tc_set(struct aq_hw_s *aq_hw,
					    u32 rx_pkt_buff_size_per_tc,
					    u32 buffer);

/* toggle rdm rx dma descriptor cache init */
void hw_atl_rdm_rx_dma_desc_cache_init_tgl(struct aq_hw_s *aq_hw);

/* get rdm rx dma descriptor cache init done */
u32 hw_atl_rdm_rx_dma_desc_cache_init_done_get(struct aq_hw_s *aq_hw);

/* set rx xoff enable (per tc) */
void hw_atl_rpb_rx_xoff_en_per_tc_set(struct aq_hw_s *aq_hw,
				      u32 rx_xoff_en_per_tc,
				      u32 buffer);

/* rpf */

/* set l2 broadcast count threshold */
void hw_atl_rpfl2broadcast_count_threshold_set(struct aq_hw_s *aq_hw,
					       u32 l2broadcast_count_threshold);

/* set l2 broadcast enable */
void hw_atl_rpfl2broadcast_en_set(struct aq_hw_s *aq_hw, u32 l2broadcast_en);

/* set l2 broadcast filter action */
void hw_atl_rpfl2broadcast_flr_act_set(struct aq_hw_s *aq_hw,
				       u32 l2broadcast_flr_act);

/* set l2 multicast filter enable */
void hw_atl_rpfl2multicast_flr_en_set(struct aq_hw_s *aq_hw,
				      u32 l2multicast_flr_en,
				      u32 filter);

/* get l2 promiscuous mode enable */
u32 hw_atl_rpfl2promiscuous_mode_en_get(struct aq_hw_s *aq_hw);

/* set l2 promiscuous mode enable */
void hw_atl_rpfl2promiscuous_mode_en_set(struct aq_hw_s *aq_hw,
					 u32 l2promiscuous_mode_en);

/* set l2 unicast filter action */
void hw_atl_rpfl2unicast_flr_act_set(struct aq_hw_s *aq_hw,
				     u32 l2unicast_flr_act,
				     u32 filter);

/* set l2 unicast filter enable */
void hw_atl_rpfl2_uc_flr_en_set(struct aq_hw_s *aq_hw, u32 l2unicast_flr_en,
				u32 filter);

/* set l2 unicast destination address lsw */
void hw_atl_rpfl2unicast_dest_addresslsw_set(struct aq_hw_s *aq_hw,
					     u32 l2unicast_dest_addresslsw,
				      u32 filter);

/* set l2 unicast destination address msw */
void hw_atl_rpfl2unicast_dest_addressmsw_set(struct aq_hw_s *aq_hw,
					     u32 l2unicast_dest_addressmsw,
				      u32 filter);

/* Set L2 Accept all Multicast packets */
void hw_atl_rpfl2_accept_all_mc_packets_set(struct aq_hw_s *aq_hw,
					    u32 l2_accept_all_mc_packets);

/* set user-priority tc mapping */
void hw_atl_rpf_rpb_user_priority_tc_map_set(struct aq_hw_s *aq_hw,
					     u32 user_priority_tc_map, u32 tc);

/* set rss key address */
void hw_atl_rpf_rss_key_addr_set(struct aq_hw_s *aq_hw, u32 rss_key_addr);

/* set rss key write data */
void hw_atl_rpf_rss_key_wr_data_set(struct aq_hw_s *aq_hw, u32 rss_key_wr_data);

/* get rss key write enable */
u32 hw_atl_rpf_rss_key_wr_en_get(struct aq_hw_s *aq_hw);

/* set rss key write enable */
void hw_atl_rpf_rss_key_wr_en_set(struct aq_hw_s *aq_hw, u32 rss_key_wr_en);

/* set rss redirection table address */
void hw_atl_rpf_rss_redir_tbl_addr_set(struct aq_hw_s *aq_hw,
				       u32 rss_redir_tbl_addr);

/* set rss redirection table write data */
void hw_atl_rpf_rss_redir_tbl_wr_data_set(struct aq_hw_s *aq_hw,
					  u32 rss_redir_tbl_wr_data);

/* get rss redirection write enable */
u32 hw_atl_rpf_rss_redir_wr_en_get(struct aq_hw_s *aq_hw);

/* set rss redirection write enable */
void hw_atl_rpf_rss_redir_wr_en_set(struct aq_hw_s *aq_hw, u32 rss_redir_wr_en);

/* set tpo to rpf system loopback */
void hw_atl_rpf_tpo_to_rpf_sys_lbk_set(struct aq_hw_s *aq_hw,
				       u32 tpo_to_rpf_sys_lbk);

/* set vlan inner ethertype */
void hw_atl_rpf_vlan_inner_etht_set(struct aq_hw_s *aq_hw, u32 vlan_inner_etht);

/* set vlan outer ethertype */
void hw_atl_rpf_vlan_outer_etht_set(struct aq_hw_s *aq_hw, u32 vlan_outer_etht);

/* set vlan promiscuous mode enable */
void hw_atl_rpf_vlan_prom_mode_en_set(struct aq_hw_s *aq_hw,
				      u32 vlan_prom_mode_en);

/* Get VLAN promiscuous mode enable */
u32 hw_atl_rpf_vlan_prom_mode_en_get(struct aq_hw_s *aq_hw);

/* Set VLAN untagged action */
void hw_atl_rpf_vlan_untagged_act_set(struct aq_hw_s *aq_hw,
				      u32 vlan_untagged_act);

/* Set VLAN accept untagged packets */
void hw_atl_rpf_vlan_accept_untagged_packets_set(struct aq_hw_s *aq_hw,
						 u32 vlan_acc_untagged_packets);

/* Set VLAN filter enable */
void hw_atl_rpf_vlan_flr_en_set(struct aq_hw_s *aq_hw, u32 vlan_flr_en,
				u32 filter);

/* Set VLAN Filter Action */
void hw_atl_rpf_vlan_flr_act_set(struct aq_hw_s *aq_hw, u32 vlan_filter_act,
				 u32 filter);

/* Set VLAN ID Filter */
void hw_atl_rpf_vlan_id_flr_set(struct aq_hw_s *aq_hw, u32 vlan_id_flr,
				u32 filter);

/* Set VLAN RX queue assignment enable */
void hw_atl_rpf_vlan_rxq_en_flr_set(struct aq_hw_s *aq_hw, u32 vlan_rxq_en,
				    u32 filter);

/* Set VLAN RX queue */
void hw_atl_rpf_vlan_rxq_flr_set(struct aq_hw_s *aq_hw, u32 vlan_rxq,
				 u32 filter);

/* set ethertype filter enable */
void hw_atl_rpf_etht_flr_en_set(struct aq_hw_s *aq_hw, u32 etht_flr_en,
				u32 filter);

/* set  ethertype user-priority enable */
void hw_atl_rpf_etht_user_priority_en_set(struct aq_hw_s *aq_hw,
					  u32 etht_user_priority_en,
					  u32 filter);

/* set  ethertype rx queue enable */
void hw_atl_rpf_etht_rx_queue_en_set(struct aq_hw_s *aq_hw,
				     u32 etht_rx_queue_en,
				     u32 filter);

/* set ethertype rx queue */
void hw_atl_rpf_etht_rx_queue_set(struct aq_hw_s *aq_hw, u32 etht_rx_queue,
				  u32 filter);

/* set ethertype user-priority */
void hw_atl_rpf_etht_user_priority_set(struct aq_hw_s *aq_hw,
				       u32 etht_user_priority,
				       u32 filter);

/* set ethertype management queue */
void hw_atl_rpf_etht_mgt_queue_set(struct aq_hw_s *aq_hw, u32 etht_mgt_queue,
				   u32 filter);

/* set ethertype filter action */
void hw_atl_rpf_etht_flr_act_set(struct aq_hw_s *aq_hw, u32 etht_flr_act,
				 u32 filter);

/* set ethertype filter */
void hw_atl_rpf_etht_flr_set(struct aq_hw_s *aq_hw, u32 etht_flr, u32 filter);

/* set L4 source port */
void hw_atl_rpf_l4_spd_set(struct aq_hw_s *aq_hw, u32 val, u32 filter);

/* set L4 destination port */
void hw_atl_rpf_l4_dpd_set(struct aq_hw_s *aq_hw, u32 val, u32 filter);

/* rpo */

/* set ipv4 header checksum offload enable */
void hw_atl_rpo_ipv4header_crc_offload_en_set(struct aq_hw_s *aq_hw,
					      u32 ipv4header_crc_offload_en);

/* set rx descriptor vlan stripping */
void hw_atl_rpo_rx_desc_vlan_stripping_set(struct aq_hw_s *aq_hw,
					   u32 rx_desc_vlan_stripping,
					   u32 descriptor);

void hw_atl_rpo_outer_vlan_tag_mode_set(void *context,
					u32 outervlantagmode);

u32 hw_atl_rpo_outer_vlan_tag_mode_get(void *context);

/* set tcp/udp checksum offload enable */
void hw_atl_rpo_tcp_udp_crc_offload_en_set(struct aq_hw_s *aq_hw,
					   u32 tcp_udp_crc_offload_en);

/* Set LRO Patch Optimization Enable. */
void hw_atl_rpo_lro_patch_optimization_en_set(struct aq_hw_s *aq_hw,
					      u32 lro_patch_optimization_en);

/* Set Large Receive Offload Enable */
void hw_atl_rpo_lro_en_set(struct aq_hw_s *aq_hw, u32 lro_en);

/* Set LRO Q Sessions Limit */
void hw_atl_rpo_lro_qsessions_lim_set(struct aq_hw_s *aq_hw,
				      u32 lro_qsessions_lim);

/* Set LRO Total Descriptor Limit */
void hw_atl_rpo_lro_total_desc_lim_set(struct aq_hw_s *aq_hw,
				       u32 lro_total_desc_lim);

/* Set LRO Min Payload of First Packet */
void hw_atl_rpo_lro_min_pay_of_first_pkt_set(struct aq_hw_s *aq_hw,
					     u32 lro_min_pld_of_first_pkt);

/* Set LRO Packet Limit */
void hw_atl_rpo_lro_pkt_lim_set(struct aq_hw_s *aq_hw, u32 lro_packet_lim);

/* Set LRO Max Number of Descriptors */
void hw_atl_rpo_lro_max_num_of_descriptors_set(struct aq_hw_s *aq_hw,
					       u32 lro_max_desc_num, u32 lro);

/* Set LRO Time Base Divider */
void hw_atl_rpo_lro_time_base_divider_set(struct aq_hw_s *aq_hw,
					  u32 lro_time_base_divider);

/*Set LRO Inactive Interval */
void hw_atl_rpo_lro_inactive_interval_set(struct aq_hw_s *aq_hw,
					  u32 lro_inactive_interval);

/*Set LRO Max Coalescing Interval */
void hw_atl_rpo_lro_max_coalescing_interval_set(struct aq_hw_s *aq_hw,
						u32 lro_max_coal_interval);

/* rx */

/* set rx register reset disable */
void hw_atl_rx_rx_reg_res_dis_set(struct aq_hw_s *aq_hw, u32 rx_reg_res_dis);

/* tdm */

/* set cpu id */
void hw_atl_tdm_cpu_id_set(struct aq_hw_s *aq_hw, u32 cpuid, u32 dca);

/* set large send offload enable */
void hw_atl_tdm_large_send_offload_en_set(struct aq_hw_s *aq_hw,
					  u32 large_send_offload_en);

/* set tx descriptor enable */
void hw_atl_tdm_tx_desc_en_set(struct aq_hw_s *aq_hw, u32 tx_desc_en,
			       u32 descriptor);

/* set tx dca enable */
void hw_atl_tdm_tx_dca_en_set(struct aq_hw_s *aq_hw, u32 tx_dca_en);

/* set tx dca mode */
void hw_atl_tdm_tx_dca_mode_set(struct aq_hw_s *aq_hw, u32 tx_dca_mode);

/* set tx descriptor dca enable */
void hw_atl_tdm_tx_desc_dca_en_set(struct aq_hw_s *aq_hw, u32 tx_desc_dca_en,
				   u32 dca);

/* get tx descriptor head pointer */
u32 hw_atl_tdm_tx_desc_head_ptr_get(struct aq_hw_s *aq_hw, u32 descriptor);

/* set tx descriptor length */
void hw_atl_tdm_tx_desc_len_set(struct aq_hw_s *aq_hw, u32 tx_desc_len,
				u32 descriptor);

/* set tx descriptor write-back interrupt enable */
void hw_atl_tdm_tx_desc_wr_wb_irq_en_set(struct aq_hw_s *aq_hw,
					 u32 tx_desc_wr_wb_irq_en);

/* set tx descriptor write-back threshold */
void hw_atl_tdm_tx_desc_wr_wb_threshold_set(struct aq_hw_s *aq_hw,
					    u32 tx_desc_wr_wb_threshold,
				     u32 descriptor);

/* Set TDM Interrupt Moderation Enable */
void hw_atl_tdm_tdm_intr_moder_en_set(struct aq_hw_s *aq_hw,
				      u32 tdm_irq_moderation_en);
/* thm */

/* set lso tcp flag of first packet */
void hw_atl_thm_lso_tcp_flag_of_first_pkt_set(struct aq_hw_s *aq_hw,
					      u32 lso_tcp_flag_of_first_pkt);

/* set lso tcp flag of last packet */
void hw_atl_thm_lso_tcp_flag_of_last_pkt_set(struct aq_hw_s *aq_hw,
					     u32 lso_tcp_flag_of_last_pkt);

/* set lso tcp flag of middle packet */
void hw_atl_thm_lso_tcp_flag_of_middle_pkt_set(struct aq_hw_s *aq_hw,
					       u32 lso_tcp_flag_of_middle_pkt);

/* tpb */

/* set TX Traffic Class Mode */
void hw_atl_tpb_tps_tx_tc_mode_set(struct aq_hw_s *aq_hw,
				   u32 tx_traf_class_mode);

/* get TX Traffic Class Mode */
u32 hw_atl_tpb_tps_tx_tc_mode_get(struct aq_hw_s *aq_hw);

/* set tx buffer enable */
void hw_atl_tpb_tx_buff_en_set(struct aq_hw_s *aq_hw, u32 tx_buff_en);

/* set tx buffer high threshold (per tc) */
void hw_atl_tpb_tx_buff_hi_threshold_per_tc_set(struct aq_hw_s *aq_hw,
						u32 tx_buff_hi_threshold_per_tc,
					 u32 buffer);

/* set tx buffer low threshold (per tc) */
void hw_atl_tpb_tx_buff_lo_threshold_per_tc_set(struct aq_hw_s *aq_hw,
						u32 tx_buff_lo_threshold_per_tc,
					 u32 buffer);

/* set tx dma system loopback enable */
void hw_atl_tpb_tx_dma_sys_lbk_en_set(struct aq_hw_s *aq_hw, u32 tx_dma_sys_lbk_en);

/* set tx dma network loopback enable */
void hw_atl_tpb_tx_dma_net_lbk_en_set(struct aq_hw_s *aq_hw,
				      u32 tx_dma_net_lbk_en);

/* set tx clock gating enable */
void hw_atl_tpb_tx_tx_clk_gate_en_set(struct aq_hw_s *aq_hw,
				      u32 tx_clk_gate_en);

/* set tx packet buffer size (per tc) */
void hw_atl_tpb_tx_pkt_buff_size_per_tc_set(struct aq_hw_s *aq_hw,
					    u32 tx_pkt_buff_size_per_tc,
					    u32 buffer);

/* set tx path pad insert enable */
void hw_atl_tpb_tx_path_scp_ins_en_set(struct aq_hw_s *aq_hw, u32 tx_path_scp_ins_en);

/* tpo */

/* set ipv4 header checksum offload enable */
void hw_atl_tpo_ipv4header_crc_offload_en_set(struct aq_hw_s *aq_hw,
					      u32 ipv4header_crc_offload_en);

/* set tcp/udp checksum offload enable */
void hw_atl_tpo_tcp_udp_crc_offload_en_set(struct aq_hw_s *aq_hw,
					   u32 tcp_udp_crc_offload_en);

/* set tx pkt system loopback enable */
void hw_atl_tpo_tx_pkt_sys_lbk_en_set(struct aq_hw_s *aq_hw,
				      u32 tx_pkt_sys_lbk_en);

/* tps */

/* set tx packet scheduler data arbitration mode */
void hw_atl_tps_tx_pkt_shed_data_arb_mode_set(struct aq_hw_s *aq_hw,
					      u32 tx_pkt_shed_data_arb_mode);

/* set tx packet scheduler descriptor rate current time reset */
void hw_atl_tps_tx_pkt_shed_desc_rate_curr_time_res_set(struct aq_hw_s *aq_hw,
							u32 curr_time_res);

/* set tx packet scheduler descriptor rate limit */
void hw_atl_tps_tx_pkt_shed_desc_rate_lim_set(struct aq_hw_s *aq_hw,
					      u32 tx_pkt_shed_desc_rate_lim);

/* set tx packet scheduler descriptor tc arbitration mode */
void hw_atl_tps_tx_pkt_shed_desc_tc_arb_mode_set(struct aq_hw_s *aq_hw,
						 u32 arb_mode);

/* set tx packet scheduler descriptor tc max credit */
void hw_atl_tps_tx_pkt_shed_desc_tc_max_credit_set(struct aq_hw_s *aq_hw,
						   const u32 tc,
						   const u32 max_credit);

/* set tx packet scheduler descriptor tc weight */
void hw_atl_tps_tx_pkt_shed_desc_tc_weight_set(struct aq_hw_s *aq_hw,
					       const u32 tc,
					       const u32 weight);

/* set tx packet scheduler descriptor vm arbitration mode */
void hw_atl_tps_tx_pkt_shed_desc_vm_arb_mode_set(struct aq_hw_s *aq_hw,
						 u32 arb_mode);

/* set tx packet scheduler tc data max credit */
void hw_atl_tps_tx_pkt_shed_tc_data_max_credit_set(struct aq_hw_s *aq_hw,
						   const u32 tc,
						   const u32 max_credit);

/* set tx packet scheduler tc data weight */
void hw_atl_tps_tx_pkt_shed_tc_data_weight_set(struct aq_hw_s *aq_hw,
					       const u32 tc,
					       const u32 weight);

/* set tx descriptor rate mode */
void hw_atl_tps_tx_desc_rate_mode_set(struct aq_hw_s *aq_hw,
				      const u32 rate_mode);

/* set tx packet scheduler descriptor rate enable */
void hw_atl_tps_tx_desc_rate_en_set(struct aq_hw_s *aq_hw, const u32 desc,
				    const u32 enable);

/* set tx packet scheduler descriptor rate integral value */
void hw_atl_tps_tx_desc_rate_x_set(struct aq_hw_s *aq_hw, const u32 desc,
				   const u32 rate_int);

/* set tx packet scheduler descriptor rate fractional value */
void hw_atl_tps_tx_desc_rate_y_set(struct aq_hw_s *aq_hw, const u32 desc,
				   const u32 rate_frac);

/* tx */

/* set tx register reset disable */
void hw_atl_tx_tx_reg_res_dis_set(struct aq_hw_s *aq_hw, u32 tx_reg_res_dis);

/* msm */

/* get register access status */
u32 hw_atl_msm_reg_access_status_get(struct aq_hw_s *aq_hw);

/* set  register address for indirect address */
void hw_atl_msm_reg_addr_for_indirect_addr_set(struct aq_hw_s *aq_hw,
					       u32 reg_addr_for_indirect_addr);

/* set register read strobe */
void hw_atl_msm_reg_rd_strobe_set(struct aq_hw_s *aq_hw, u32 reg_rd_strobe);

/* get  register read data */
u32 hw_atl_msm_reg_rd_data_get(struct aq_hw_s *aq_hw);

/* set  register write data */
void hw_atl_msm_reg_wr_data_set(struct aq_hw_s *aq_hw, u32 reg_wr_data);

/* set register write strobe */
void hw_atl_msm_reg_wr_strobe_set(struct aq_hw_s *aq_hw, u32 reg_wr_strobe);

/* pci */

/* set pci register reset disable */
void hw_atl_pci_pci_reg_res_dis_set(struct aq_hw_s *aq_hw, u32 pci_reg_res_dis);

/* pcs */
void hw_atl_pcs_ptp_clock_read_enable(struct aq_hw_s *aq_hw,
				      u32 ptp_clock_read_enable);

u32 hw_atl_pcs_ptp_clock_get(struct aq_hw_s *aq_hw, u32 index);

/* set uP Force Interrupt */
void hw_atl_mcp_up_force_intr_set(struct aq_hw_s *aq_hw, u32 up_force_intr);

/* clear ipv4 filter destination address */
void hw_atl_rpfl3l4_ipv4_dest_addr_clear(struct aq_hw_s *aq_hw, u8 location);

/* clear ipv4 filter source address */
void hw_atl_rpfl3l4_ipv4_src_addr_clear(struct aq_hw_s *aq_hw, u8 location);

/* clear command for filter l3-l4 */
void hw_atl_rpfl3l4_cmd_clear(struct aq_hw_s *aq_hw, u8 location);

/* clear ipv6 filter destination address */
void hw_atl_rpfl3l4_ipv6_dest_addr_clear(struct aq_hw_s *aq_hw, u8 location);

/* clear ipv6 filter source address */
void hw_atl_rpfl3l4_ipv6_src_addr_clear(struct aq_hw_s *aq_hw, u8 location);

/* set ipv4 filter destination address */
void hw_atl_rpfl3l4_ipv4_dest_addr_set(struct aq_hw_s *aq_hw, u8 location,
				       u32 ipv4_dest);

/* set ipv4 filter source address */
void hw_atl_rpfl3l4_ipv4_src_addr_set(struct aq_hw_s *aq_hw, u8 location,
				      u32 ipv4_src);

/* set command for filter l3-l4 */
void hw_atl_rpfl3l4_cmd_set(struct aq_hw_s *aq_hw, u8 location, u32 cmd);

/* set ipv6 filter source address */
void hw_atl_rpfl3l4_ipv6_src_addr_set(struct aq_hw_s *aq_hw, u8 location,
				      u32 *ipv6_src);

/* set ipv6 filter destination address */
void hw_atl_rpfl3l4_ipv6_dest_addr_set(struct aq_hw_s *aq_hw, u8 location,
				       u32 *ipv6_dest);

/* set Global MDIO Interface 1 */
void hw_atl_glb_mdio_iface1_set(struct aq_hw_s *hw, u32 value);

/* get Global MDIO Interface 1 */
u32 hw_atl_glb_mdio_iface1_get(struct aq_hw_s *hw);

/* set Global MDIO Interface 2 */
void hw_atl_glb_mdio_iface2_set(struct aq_hw_s *hw, u32 value);

/* get Global MDIO Interface 2 */
u32 hw_atl_glb_mdio_iface2_get(struct aq_hw_s *hw);

/* set Global MDIO Interface 3 */
void hw_atl_glb_mdio_iface3_set(struct aq_hw_s *hw, u32 value);

/* get Global MDIO Interface 3 */
u32 hw_atl_glb_mdio_iface3_get(struct aq_hw_s *hw);

/* set Global MDIO Interface 4 */
void hw_atl_glb_mdio_iface4_set(struct aq_hw_s *hw, u32 value);

/* get Global MDIO Interface 4 */
u32 hw_atl_glb_mdio_iface4_get(struct aq_hw_s *hw);

/* set Global MDIO Interface 5 */
void hw_atl_glb_mdio_iface5_set(struct aq_hw_s *hw, u32 value);

/* get Global MDIO Interface 5 */
u32 hw_atl_glb_mdio_iface5_get(struct aq_hw_s *hw);

u32 hw_atl_mdio_busy_get(struct aq_hw_s *aq_hw);

/* get global microprocessor ram semaphore */
u32 hw_atl_sem_ram_get(struct aq_hw_s *self);

/* get global microprocessor mdio semaphore */
u32 hw_atl_sem_mdio_get(struct aq_hw_s *self);

u32 hw_atl_sem_reset1_get(struct aq_hw_s *self);
u32 hw_atl_sem_reset2_get(struct aq_hw_s *self);

/* get global microprocessor scratch pad register */
u32 hw_atl_scrpad_get(struct aq_hw_s *aq_hw, u32 scratch_scp);

/* get global microprocessor scratch pad 12 register */
u32 hw_atl_scrpad12_get(struct aq_hw_s *self);

/* get global microprocessor scratch pad 25 register */
u32 hw_atl_scrpad25_get(struct aq_hw_s *self);

#endif /* HW_ATL_LLH_H */

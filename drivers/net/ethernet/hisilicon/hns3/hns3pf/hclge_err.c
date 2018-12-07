// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2016-2017 Hisilicon Limited. */

#include "hclge_err.h"

static const struct hclge_hw_error hclge_imp_tcm_ecc_int[] = {
	{ .int_msk = BIT(0), .msg = "imp_itcm0_ecc_1bit_err" },
	{ .int_msk = BIT(1), .msg = "imp_itcm0_ecc_mbit_err" },
	{ .int_msk = BIT(2), .msg = "imp_itcm1_ecc_1bit_err" },
	{ .int_msk = BIT(3), .msg = "imp_itcm1_ecc_mbit_err" },
	{ .int_msk = BIT(4), .msg = "imp_itcm2_ecc_1bit_err" },
	{ .int_msk = BIT(5), .msg = "imp_itcm2_ecc_mbit_err" },
	{ .int_msk = BIT(6), .msg = "imp_itcm3_ecc_1bit_err" },
	{ .int_msk = BIT(7), .msg = "imp_itcm3_ecc_mbit_err" },
	{ .int_msk = BIT(8), .msg = "imp_dtcm0_mem0_ecc_1bit_err" },
	{ .int_msk = BIT(9), .msg = "imp_dtcm0_mem0_ecc_mbit_err" },
	{ .int_msk = BIT(10), .msg = "imp_dtcm0_mem1_ecc_1bit_err" },
	{ .int_msk = BIT(11), .msg = "imp_dtcm0_mem1_ecc_mbit_err" },
	{ .int_msk = BIT(12), .msg = "imp_dtcm1_mem0_ecc_1bit_err" },
	{ .int_msk = BIT(13), .msg = "imp_dtcm1_mem0_ecc_mbit_err" },
	{ .int_msk = BIT(14), .msg = "imp_dtcm1_mem1_ecc_1bit_err" },
	{ .int_msk = BIT(15), .msg = "imp_dtcm1_mem1_ecc_mbit_err" },
	{ /* sentinel */ }
};

static const struct hclge_hw_error hclge_imp_itcm4_ecc_int[] = {
	{ .int_msk = BIT(0), .msg = "imp_itcm4_ecc_1bit_err" },
	{ .int_msk = BIT(1), .msg = "imp_itcm4_ecc_mbit_err" },
	{ /* sentinel */ }
};

static const struct hclge_hw_error hclge_cmdq_nic_mem_ecc_int[] = {
	{ .int_msk = BIT(0), .msg = "cmdq_nic_rx_depth_ecc_1bit_err" },
	{ .int_msk = BIT(1), .msg = "cmdq_nic_rx_depth_ecc_mbit_err" },
	{ .int_msk = BIT(2), .msg = "cmdq_nic_tx_depth_ecc_1bit_err" },
	{ .int_msk = BIT(3), .msg = "cmdq_nic_tx_depth_ecc_mbit_err" },
	{ .int_msk = BIT(4), .msg = "cmdq_nic_rx_tail_ecc_1bit_err" },
	{ .int_msk = BIT(5), .msg = "cmdq_nic_rx_tail_ecc_mbit_err" },
	{ .int_msk = BIT(6), .msg = "cmdq_nic_tx_tail_ecc_1bit_err" },
	{ .int_msk = BIT(7), .msg = "cmdq_nic_tx_tail_ecc_mbit_err" },
	{ .int_msk = BIT(8), .msg = "cmdq_nic_rx_head_ecc_1bit_err" },
	{ .int_msk = BIT(9), .msg = "cmdq_nic_rx_head_ecc_mbit_err" },
	{ .int_msk = BIT(10), .msg = "cmdq_nic_tx_head_ecc_1bit_err" },
	{ .int_msk = BIT(11), .msg = "cmdq_nic_tx_head_ecc_mbit_err" },
	{ .int_msk = BIT(12), .msg = "cmdq_nic_rx_addr_ecc_1bit_err" },
	{ .int_msk = BIT(13), .msg = "cmdq_nic_rx_addr_ecc_mbit_err" },
	{ .int_msk = BIT(14), .msg = "cmdq_nic_tx_addr_ecc_1bit_err" },
	{ .int_msk = BIT(15), .msg = "cmdq_nic_tx_addr_ecc_mbit_err" },
	{ /* sentinel */ }
};

static const struct hclge_hw_error hclge_cmdq_rocee_mem_ecc_int[] = {
	{ .int_msk = BIT(0), .msg = "cmdq_rocee_rx_depth_ecc_1bit_err" },
	{ .int_msk = BIT(1), .msg = "cmdq_rocee_rx_depth_ecc_mbit_err" },
	{ .int_msk = BIT(2), .msg = "cmdq_rocee_tx_depth_ecc_1bit_err" },
	{ .int_msk = BIT(3), .msg = "cmdq_rocee_tx_depth_ecc_mbit_err" },
	{ .int_msk = BIT(4), .msg = "cmdq_rocee_rx_tail_ecc_1bit_err" },
	{ .int_msk = BIT(5), .msg = "cmdq_rocee_rx_tail_ecc_mbit_err" },
	{ .int_msk = BIT(6), .msg = "cmdq_rocee_tx_tail_ecc_1bit_err" },
	{ .int_msk = BIT(7), .msg = "cmdq_rocee_tx_tail_ecc_mbit_err" },
	{ .int_msk = BIT(8), .msg = "cmdq_rocee_rx_head_ecc_1bit_err" },
	{ .int_msk = BIT(9), .msg = "cmdq_rocee_rx_head_ecc_mbit_err" },
	{ .int_msk = BIT(10), .msg = "cmdq_rocee_tx_head_ecc_1bit_err" },
	{ .int_msk = BIT(11), .msg = "cmdq_rocee_tx_head_ecc_mbit_err" },
	{ .int_msk = BIT(12), .msg = "cmdq_rocee_rx_addr_ecc_1bit_err" },
	{ .int_msk = BIT(13), .msg = "cmdq_rocee_rx_addr_ecc_mbit_err" },
	{ .int_msk = BIT(14), .msg = "cmdq_rocee_tx_addr_ecc_1bit_err" },
	{ .int_msk = BIT(15), .msg = "cmdq_rocee_tx_addr_ecc_mbit_err" },
	{ /* sentinel */ }
};

static const struct hclge_hw_error hclge_tqp_int_ecc_int[] = {
	{ .int_msk = BIT(0), .msg = "tqp_int_cfg_even_ecc_1bit_err" },
	{ .int_msk = BIT(1), .msg = "tqp_int_cfg_odd_ecc_1bit_err" },
	{ .int_msk = BIT(2), .msg = "tqp_int_ctrl_even_ecc_1bit_err" },
	{ .int_msk = BIT(3), .msg = "tqp_int_ctrl_odd_ecc_1bit_err" },
	{ .int_msk = BIT(4), .msg = "tx_que_scan_int_ecc_1bit_err" },
	{ .int_msk = BIT(5), .msg = "rx_que_scan_int_ecc_1bit_err" },
	{ .int_msk = BIT(6), .msg = "tqp_int_cfg_even_ecc_mbit_err" },
	{ .int_msk = BIT(7), .msg = "tqp_int_cfg_odd_ecc_mbit_err" },
	{ .int_msk = BIT(8), .msg = "tqp_int_ctrl_even_ecc_mbit_err" },
	{ .int_msk = BIT(9), .msg = "tqp_int_ctrl_odd_ecc_mbit_err" },
	{ .int_msk = BIT(10), .msg = "tx_que_scan_int_ecc_mbit_err" },
	{ .int_msk = BIT(11), .msg = "rx_que_scan_int_ecc_mbit_err" },
	{ /* sentinel */ }
};

static const struct hclge_hw_error hclge_igu_com_err_int[] = {
	{ .int_msk = BIT(0), .msg = "igu_rx_buf0_ecc_mbit_err" },
	{ .int_msk = BIT(1), .msg = "igu_rx_buf0_ecc_1bit_err" },
	{ .int_msk = BIT(2), .msg = "igu_rx_buf1_ecc_mbit_err" },
	{ .int_msk = BIT(3), .msg = "igu_rx_buf1_ecc_1bit_err" },
	{ /* sentinel */ }
};

static const struct hclge_hw_error hclge_igu_egu_tnl_err_int[] = {
	{ .int_msk = BIT(0), .msg = "rx_buf_overflow" },
	{ .int_msk = BIT(1), .msg = "rx_stp_fifo_overflow" },
	{ .int_msk = BIT(2), .msg = "rx_stp_fifo_undeflow" },
	{ .int_msk = BIT(3), .msg = "tx_buf_overflow" },
	{ .int_msk = BIT(4), .msg = "tx_buf_underrun" },
	{ .int_msk = BIT(5), .msg = "rx_stp_buf_overflow" },
	{ /* sentinel */ }
};

static const struct hclge_hw_error hclge_ncsi_err_int[] = {
	{ .int_msk = BIT(0), .msg = "ncsi_tx_ecc_1bit_err" },
	{ .int_msk = BIT(1), .msg = "ncsi_tx_ecc_mbit_err" },
	{ /* sentinel */ }
};

static const struct hclge_hw_error hclge_ppp_mpf_int0[] = {
	{ .int_msk = BIT(0), .msg = "vf_vlan_ad_mem_ecc_1bit_err" },
	{ .int_msk = BIT(1), .msg = "umv_mcast_group_mem_ecc_1bit_err" },
	{ .int_msk = BIT(2), .msg = "umv_key_mem0_ecc_1bit_err" },
	{ .int_msk = BIT(3), .msg = "umv_key_mem1_ecc_1bit_err" },
	{ .int_msk = BIT(4), .msg = "umv_key_mem2_ecc_1bit_err" },
	{ .int_msk = BIT(5), .msg = "umv_key_mem3_ecc_1bit_err" },
	{ .int_msk = BIT(6), .msg = "umv_ad_mem_ecc_1bit_err" },
	{ .int_msk = BIT(7), .msg = "rss_tc_mode_mem_ecc_1bit_err" },
	{ .int_msk = BIT(8), .msg = "rss_idt_mem0_ecc_1bit_err" },
	{ .int_msk = BIT(9), .msg = "rss_idt_mem1_ecc_1bit_err" },
	{ .int_msk = BIT(10), .msg = "rss_idt_mem2_ecc_1bit_err" },
	{ .int_msk = BIT(11), .msg = "rss_idt_mem3_ecc_1bit_err" },
	{ .int_msk = BIT(12), .msg = "rss_idt_mem4_ecc_1bit_err" },
	{ .int_msk = BIT(13), .msg = "rss_idt_mem5_ecc_1bit_err" },
	{ .int_msk = BIT(14), .msg = "rss_idt_mem6_ecc_1bit_err" },
	{ .int_msk = BIT(15), .msg = "rss_idt_mem7_ecc_1bit_err" },
	{ .int_msk = BIT(16), .msg = "rss_idt_mem8_ecc_1bit_err" },
	{ .int_msk = BIT(17), .msg = "rss_idt_mem9_ecc_1bit_err" },
	{ .int_msk = BIT(18), .msg = "rss_idt_mem10_ecc_1bit_err" },
	{ .int_msk = BIT(19), .msg = "rss_idt_mem11_ecc_1bit_err" },
	{ .int_msk = BIT(20), .msg = "rss_idt_mem12_ecc_1bit_err" },
	{ .int_msk = BIT(21), .msg = "rss_idt_mem13_ecc_1bit_err" },
	{ .int_msk = BIT(22), .msg = "rss_idt_mem14_ecc_1bit_err" },
	{ .int_msk = BIT(23), .msg = "rss_idt_mem15_ecc_1bit_err" },
	{ .int_msk = BIT(24), .msg = "port_vlan_mem_ecc_1bit_err" },
	{ .int_msk = BIT(25), .msg = "mcast_linear_table_mem_ecc_1bit_err" },
	{ .int_msk = BIT(26), .msg = "mcast_result_mem_ecc_1bit_err" },
	{ .int_msk = BIT(27),
		.msg = "flow_director_ad_mem0_ecc_1bit_err" },
	{ .int_msk = BIT(28),
		.msg = "flow_director_ad_mem1_ecc_1bit_err" },
	{ .int_msk = BIT(29),
		.msg = "rx_vlan_tag_memory_ecc_1bit_err" },
	{ .int_msk = BIT(30),
		.msg = "Tx_UP_mapping_config_mem_ecc_1bit_err" },
	{ /* sentinel */ }
};

static const struct hclge_hw_error hclge_ppp_mpf_int1[] = {
	{ .int_msk = BIT(0), .msg = "vf_vlan_ad_mem_ecc_mbit_err" },
	{ .int_msk = BIT(1), .msg = "umv_mcast_group_mem_ecc_mbit_err" },
	{ .int_msk = BIT(2), .msg = "umv_key_mem0_ecc_mbit_err" },
	{ .int_msk = BIT(3), .msg = "umv_key_mem1_ecc_mbit_err" },
	{ .int_msk = BIT(4), .msg = "umv_key_mem2_ecc_mbit_err" },
	{ .int_msk = BIT(5), .msg = "umv_key_mem3_ecc_mbit_err" },
	{ .int_msk = BIT(6), .msg = "umv_ad_mem_ecc_mbit_erre" },
	{ .int_msk = BIT(7), .msg = "rss_tc_mode_mem_ecc_mbit_err" },
	{ .int_msk = BIT(8), .msg = "rss_idt_mem0_ecc_mbit_err" },
	{ .int_msk = BIT(9), .msg = "rss_idt_mem1_ecc_mbit_err" },
	{ .int_msk = BIT(10), .msg = "rss_idt_mem2_ecc_mbit_err" },
	{ .int_msk = BIT(11), .msg = "rss_idt_mem3_ecc_mbit_err" },
	{ .int_msk = BIT(12), .msg = "rss_idt_mem4_ecc_mbit_err" },
	{ .int_msk = BIT(13), .msg = "rss_idt_mem5_ecc_mbit_err" },
	{ .int_msk = BIT(14), .msg = "rss_idt_mem6_ecc_mbit_err" },
	{ .int_msk = BIT(15), .msg = "rss_idt_mem7_ecc_mbit_err" },
	{ .int_msk = BIT(16), .msg = "rss_idt_mem8_ecc_mbit_err" },
	{ .int_msk = BIT(17), .msg = "rss_idt_mem9_ecc_mbit_err" },
	{ .int_msk = BIT(18), .msg = "rss_idt_mem10_ecc_m1bit_err" },
	{ .int_msk = BIT(19), .msg = "rss_idt_mem11_ecc_mbit_err" },
	{ .int_msk = BIT(20), .msg = "rss_idt_mem12_ecc_mbit_err" },
	{ .int_msk = BIT(21), .msg = "rss_idt_mem13_ecc_mbit_err" },
	{ .int_msk = BIT(22), .msg = "rss_idt_mem14_ecc_mbit_err" },
	{ .int_msk = BIT(23), .msg = "rss_idt_mem15_ecc_mbit_err" },
	{ .int_msk = BIT(24), .msg = "port_vlan_mem_ecc_mbit_err" },
	{ .int_msk = BIT(25), .msg = "mcast_linear_table_mem_ecc_mbit_err" },
	{ .int_msk = BIT(26), .msg = "mcast_result_mem_ecc_mbit_err" },
	{ .int_msk = BIT(27),
		.msg = "flow_director_ad_mem0_ecc_mbit_err" },
	{ .int_msk = BIT(28),
		.msg = "flow_director_ad_mem1_ecc_mbit_err" },
	{ .int_msk = BIT(29),
		.msg = "rx_vlan_tag_memory_ecc_mbit_err" },
	{ .int_msk = BIT(30),
		.msg = "Tx_UP_mapping_config_mem_ecc_mbit_err" },
	{ /* sentinel */ }
};

static const struct hclge_hw_error hclge_ppp_pf_int[] = {
	{ .int_msk = BIT(0), .msg = "Tx_vlan_tag_err" },
	{ .int_msk = BIT(1), .msg = "rss_list_tc_unassigned_queue_err" },
	{ /* sentinel */ }
};

static const struct hclge_hw_error hclge_ppp_mpf_int2[] = {
	{ .int_msk = BIT(0), .msg = "hfs_fifo_mem_ecc_1bit_err" },
	{ .int_msk = BIT(1), .msg = "rslt_descr_fifo_mem_ecc_1bit_err" },
	{ .int_msk = BIT(2), .msg = "tx_vlan_tag_mem_ecc_1bit_err" },
	{ .int_msk = BIT(3), .msg = "FD_CN0_memory_ecc_1bit_err" },
	{ .int_msk = BIT(4), .msg = "FD_CN1_memory_ecc_1bit_err" },
	{ .int_msk = BIT(5), .msg = "GRO_AD_memory_ecc_1bit_err" },
	{ /* sentinel */ }
};

static const struct hclge_hw_error hclge_ppp_mpf_int3[] = {
	{ .int_msk = BIT(0), .msg = "hfs_fifo_mem_ecc_mbit_err" },
	{ .int_msk = BIT(1), .msg = "rslt_descr_fifo_mem_ecc_mbit_err" },
	{ .int_msk = BIT(2), .msg = "tx_vlan_tag_mem_ecc_mbit_err" },
	{ .int_msk = BIT(3), .msg = "FD_CN0_memory_ecc_mbit_err" },
	{ .int_msk = BIT(4), .msg = "FD_CN1_memory_ecc_mbit_err" },
	{ .int_msk = BIT(5), .msg = "GRO_AD_memory_ecc_mbit_err" },
	{ /* sentinel */ }
};

struct hclge_tm_sch_ecc_info {
	const char *name;
};

static const struct hclge_tm_sch_ecc_info hclge_tm_sch_ecc_err[7][15] = {
	{
		{ .name = "QSET_QUEUE_CTRL:PRI_LEN TAB" },
		{ .name = "QSET_QUEUE_CTRL:SPA_LEN TAB" },
		{ .name = "QSET_QUEUE_CTRL:SPB_LEN TAB" },
		{ .name = "QSET_QUEUE_CTRL:WRRA_LEN TAB" },
		{ .name = "QSET_QUEUE_CTRL:WRRB_LEN TAB" },
		{ .name = "QSET_QUEUE_CTRL:SPA_HPTR TAB" },
		{ .name = "QSET_QUEUE_CTRL:SPB_HPTR TAB" },
		{ .name = "QSET_QUEUE_CTRL:WRRA_HPTR TAB" },
		{ .name = "QSET_QUEUE_CTRL:WRRB_HPTR TAB" },
		{ .name = "QSET_QUEUE_CTRL:QS_LINKLIST TAB" },
		{ .name = "QSET_QUEUE_CTRL:SPA_TPTR TAB" },
		{ .name = "QSET_QUEUE_CTRL:SPB_TPTR TAB" },
		{ .name = "QSET_QUEUE_CTRL:WRRA_TPTR TAB" },
		{ .name = "QSET_QUEUE_CTRL:WRRB_TPTR TAB" },
		{ .name = "QSET_QUEUE_CTRL:QS_DEFICITCNT TAB" },
	},
	{
		{ .name = "ROCE_QUEUE_CTRL:QS_LEN TAB" },
		{ .name = "ROCE_QUEUE_CTRL:QS_TPTR TAB" },
		{ .name = "ROCE_QUEUE_CTRL:QS_HPTR TAB" },
		{ .name = "ROCE_QUEUE_CTRL:QLINKLIST TAB" },
		{ .name = "ROCE_QUEUE_CTRL:QCLEN TAB" },
	},
	{
		{ .name = "NIC_QUEUE_CTRL:QS_LEN TAB" },
		{ .name = "NIC_QUEUE_CTRL:QS_TPTR TAB" },
		{ .name = "NIC_QUEUE_CTRL:QS_HPTR TAB" },
		{ .name = "NIC_QUEUE_CTRL:QLINKLIST TAB" },
		{ .name = "NIC_QUEUE_CTRL:QCLEN TAB" },
	},
	{
		{ .name = "RAM_CFG_CTRL:CSHAP TAB" },
		{ .name = "RAM_CFG_CTRL:PSHAP TAB" },
	},
	{
		{ .name = "SHAPER_CTRL:PSHAP TAB" },
	},
	{
		{ .name = "MSCH_CTRL" },
	},
	{
		{ .name = "TOP_CTRL" },
	},
};

static const struct hclge_hw_error hclge_tm_sch_err_int[] = {
	{ .int_msk = BIT(0), .msg = "tm_sch_ecc_1bit_err" },
	{ .int_msk = BIT(1), .msg = "tm_sch_ecc_mbit_err" },
	{ .int_msk = BIT(2), .msg = "tm_sch_port_shap_sub_fifo_wr_full_err" },
	{ .int_msk = BIT(3), .msg = "tm_sch_port_shap_sub_fifo_rd_empty_err" },
	{ .int_msk = BIT(4), .msg = "tm_sch_pg_pshap_sub_fifo_wr_full_err" },
	{ .int_msk = BIT(5), .msg = "tm_sch_pg_pshap_sub_fifo_rd_empty_err" },
	{ .int_msk = BIT(6), .msg = "tm_sch_pg_cshap_sub_fifo_wr_full_err" },
	{ .int_msk = BIT(7), .msg = "tm_sch_pg_cshap_sub_fifo_rd_empty_err" },
	{ .int_msk = BIT(8), .msg = "tm_sch_pri_pshap_sub_fifo_wr_full_err" },
	{ .int_msk = BIT(9), .msg = "tm_sch_pri_pshap_sub_fifo_rd_empty_err" },
	{ .int_msk = BIT(10), .msg = "tm_sch_pri_cshap_sub_fifo_wr_full_err" },
	{ .int_msk = BIT(11), .msg = "tm_sch_pri_cshap_sub_fifo_rd_empty_err" },
	{ .int_msk = BIT(12),
	  .msg = "tm_sch_port_shap_offset_fifo_wr_full_err" },
	{ .int_msk = BIT(13),
	  .msg = "tm_sch_port_shap_offset_fifo_rd_empty_err" },
	{ .int_msk = BIT(14),
	  .msg = "tm_sch_pg_pshap_offset_fifo_wr_full_err" },
	{ .int_msk = BIT(15),
	  .msg = "tm_sch_pg_pshap_offset_fifo_rd_empty_err" },
	{ .int_msk = BIT(16),
	  .msg = "tm_sch_pg_cshap_offset_fifo_wr_full_err" },
	{ .int_msk = BIT(17),
	  .msg = "tm_sch_pg_cshap_offset_fifo_rd_empty_err" },
	{ .int_msk = BIT(18),
	  .msg = "tm_sch_pri_pshap_offset_fifo_wr_full_err" },
	{ .int_msk = BIT(19),
	  .msg = "tm_sch_pri_pshap_offset_fifo_rd_empty_err" },
	{ .int_msk = BIT(20),
	  .msg = "tm_sch_pri_cshap_offset_fifo_wr_full_err" },
	{ .int_msk = BIT(21),
	  .msg = "tm_sch_pri_cshap_offset_fifo_rd_empty_err" },
	{ .int_msk = BIT(22), .msg = "tm_sch_rq_fifo_wr_full_err" },
	{ .int_msk = BIT(23), .msg = "tm_sch_rq_fifo_rd_empty_err" },
	{ .int_msk = BIT(24), .msg = "tm_sch_nq_fifo_wr_full_err" },
	{ .int_msk = BIT(25), .msg = "tm_sch_nq_fifo_rd_empty_err" },
	{ .int_msk = BIT(26), .msg = "tm_sch_roce_up_fifo_wr_full_err" },
	{ .int_msk = BIT(27), .msg = "tm_sch_roce_up_fifo_rd_empty_err" },
	{ .int_msk = BIT(28), .msg = "tm_sch_rcb_byte_fifo_wr_full_err" },
	{ .int_msk = BIT(29), .msg = "tm_sch_rcb_byte_fifo_rd_empty_err" },
	{ .int_msk = BIT(30), .msg = "tm_sch_ssu_byte_fifo_wr_full_err" },
	{ .int_msk = BIT(31), .msg = "tm_sch_ssu_byte_fifo_rd_empty_err" },
	{ /* sentinel */ }
};

static const struct hclge_hw_error hclge_qcn_ecc_err_int[] = {
	{ .int_msk = BIT(0), .msg = "qcn_byte_mem_ecc_1bit_err" },
	{ .int_msk = BIT(1), .msg = "qcn_byte_mem_ecc_mbit_err" },
	{ .int_msk = BIT(2), .msg = "qcn_time_mem_ecc_1bit_err" },
	{ .int_msk = BIT(3), .msg = "qcn_time_mem_ecc_mbit_err" },
	{ .int_msk = BIT(4), .msg = "qcn_fb_mem_ecc_1bit_err" },
	{ .int_msk = BIT(5), .msg = "qcn_fb_mem_ecc_mbit_err" },
	{ .int_msk = BIT(6), .msg = "qcn_link_mem_ecc_1bit_err" },
	{ .int_msk = BIT(7), .msg = "qcn_link_mem_ecc_mbit_err" },
	{ .int_msk = BIT(8), .msg = "qcn_rate_mem_ecc_1bit_err" },
	{ .int_msk = BIT(9), .msg = "qcn_rate_mem_ecc_mbit_err" },
	{ .int_msk = BIT(10), .msg = "qcn_tmplt_mem_ecc_1bit_err" },
	{ .int_msk = BIT(11), .msg = "qcn_tmplt_mem_ecc_mbit_err" },
	{ .int_msk = BIT(12), .msg = "qcn_shap_cfg_mem_ecc_1bit_err" },
	{ .int_msk = BIT(13), .msg = "qcn_shap_cfg_mem_ecc_mbit_err" },
	{ .int_msk = BIT(14), .msg = "qcn_gp0_barrel_mem_ecc_1bit_err" },
	{ .int_msk = BIT(15), .msg = "qcn_gp0_barrel_mem_ecc_mbit_err" },
	{ .int_msk = BIT(16), .msg = "qcn_gp1_barrel_mem_ecc_1bit_err" },
	{ .int_msk = BIT(17), .msg = "qcn_gp1_barrel_mem_ecc_mbit_err" },
	{ .int_msk = BIT(18), .msg = "qcn_gp2_barrel_mem_ecc_1bit_err" },
	{ .int_msk = BIT(19), .msg = "qcn_gp2_barrel_mem_ecc_mbit_err" },
	{ .int_msk = BIT(20), .msg = "qcn_gp3_barral_mem_ecc_1bit_err" },
	{ .int_msk = BIT(21), .msg = "qcn_gp3_barral_mem_ecc_mbit_err" },
	{ /* sentinel */ }
};

/* hclge_cmd_query_error: read the error information
 * @hdev: pointer to struct hclge_dev
 * @desc: descriptor for describing the command
 * @cmd:  command opcode
 * @flag: flag for extended command structure
 * @w_num: offset for setting the read interrupt type.
 * @int_type: select which type of the interrupt for which the error
 * info will be read(RAS-CE/RAS-NFE/RAS-FE etc).
 *
 * This function query the error info from hw register/s using command
 */
static int hclge_cmd_query_error(struct hclge_dev *hdev,
				 struct hclge_desc *desc, u32 cmd,
				 u16 flag, u8 w_num,
				 enum hclge_err_int_type int_type)
{
	struct device *dev = &hdev->pdev->dev;
	int num = 1;
	int ret;

	hclge_cmd_setup_basic_desc(&desc[0], cmd, true);
	if (flag) {
		desc[0].flag |= cpu_to_le16(flag);
		hclge_cmd_setup_basic_desc(&desc[1], cmd, true);
		num = 2;
	}
	if (w_num)
		desc[0].data[w_num] = cpu_to_le32(int_type);

	ret = hclge_cmd_send(&hdev->hw, &desc[0], num);
	if (ret)
		dev_err(dev, "query error cmd failed (%d)\n", ret);

	return ret;
}

static int hclge_config_common_hw_err_int(struct hclge_dev *hdev, bool en)
{
	struct device *dev = &hdev->pdev->dev;
	struct hclge_desc desc[2];
	int ret;

	/* configure common error interrupts */
	hclge_cmd_setup_basic_desc(&desc[0], HCLGE_COMMON_ECC_INT_CFG, false);
	desc[0].flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);
	hclge_cmd_setup_basic_desc(&desc[1], HCLGE_COMMON_ECC_INT_CFG, false);

	if (en) {
		desc[0].data[0] = cpu_to_le32(HCLGE_IMP_TCM_ECC_ERR_INT_EN);
		desc[0].data[2] = cpu_to_le32(HCLGE_CMDQ_NIC_ECC_ERR_INT_EN |
					HCLGE_CMDQ_ROCEE_ECC_ERR_INT_EN);
		desc[0].data[3] = cpu_to_le32(HCLGE_IMP_RD_POISON_ERR_INT_EN);
		desc[0].data[4] = cpu_to_le32(HCLGE_TQP_ECC_ERR_INT_EN);
		desc[0].data[5] = cpu_to_le32(HCLGE_IMP_ITCM4_ECC_ERR_INT_EN);
	} else {
		desc[0].data[0] = 0;
		desc[0].data[2] = 0;
		desc[0].data[3] = 0;
		desc[0].data[4] = 0;
		desc[0].data[5] = 0;
	}
	desc[1].data[0] = cpu_to_le32(HCLGE_IMP_TCM_ECC_ERR_INT_EN_MASK);
	desc[1].data[2] = cpu_to_le32(HCLGE_CMDQ_NIC_ECC_ERR_INT_EN_MASK |
				HCLGE_CMDQ_ROCEE_ECC_ERR_INT_EN_MASK);
	desc[1].data[3] = cpu_to_le32(HCLGE_IMP_RD_POISON_ERR_INT_EN_MASK);
	desc[1].data[4] = cpu_to_le32(HCLGE_TQP_ECC_ERR_INT_EN_MASK);
	desc[1].data[5] = cpu_to_le32(HCLGE_IMP_ITCM4_ECC_ERR_INT_EN_MASK);

	ret = hclge_cmd_send(&hdev->hw, &desc[0], 2);
	if (ret)
		dev_err(dev,
			"fail(%d) to configure common err interrupts\n", ret);

	return ret;
}

static int hclge_config_ncsi_hw_err_int(struct hclge_dev *hdev, bool en)
{
	struct device *dev = &hdev->pdev->dev;
	struct hclge_desc desc;
	int ret;

	if (hdev->pdev->revision < 0x21)
		return 0;

	/* configure NCSI error interrupts */
	hclge_cmd_setup_basic_desc(&desc, HCLGE_NCSI_INT_EN, false);
	if (en)
		desc.data[0] = cpu_to_le32(HCLGE_NCSI_ERR_INT_EN);
	else
		desc.data[0] = 0;

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(dev,
			"fail(%d) to configure  NCSI error interrupts\n", ret);

	return ret;
}

static int hclge_config_igu_egu_hw_err_int(struct hclge_dev *hdev, bool en)
{
	struct device *dev = &hdev->pdev->dev;
	struct hclge_desc desc;
	int ret;

	/* configure IGU,EGU error interrupts */
	hclge_cmd_setup_basic_desc(&desc, HCLGE_IGU_COMMON_INT_EN, false);
	if (en)
		desc.data[0] = cpu_to_le32(HCLGE_IGU_ERR_INT_EN);
	else
		desc.data[0] = 0;
	desc.data[1] = cpu_to_le32(HCLGE_IGU_ERR_INT_EN_MASK);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(dev,
			"fail(%d) to configure IGU common interrupts\n", ret);
		return ret;
	}

	hclge_cmd_setup_basic_desc(&desc, HCLGE_IGU_EGU_TNL_INT_EN, false);
	if (en)
		desc.data[0] = cpu_to_le32(HCLGE_IGU_TNL_ERR_INT_EN);
	else
		desc.data[0] = 0;
	desc.data[1] = cpu_to_le32(HCLGE_IGU_TNL_ERR_INT_EN_MASK);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(dev,
			"fail(%d) to configure IGU-EGU TNL interrupts\n", ret);
		return ret;
	}

	ret = hclge_config_ncsi_hw_err_int(hdev, en);

	return ret;
}

static int hclge_config_ppp_error_interrupt(struct hclge_dev *hdev, u32 cmd,
					    bool en)
{
	struct device *dev = &hdev->pdev->dev;
	struct hclge_desc desc[2];
	int ret;

	/* configure PPP error interrupts */
	hclge_cmd_setup_basic_desc(&desc[0], cmd, false);
	desc[0].flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);
	hclge_cmd_setup_basic_desc(&desc[1], cmd, false);

	if (cmd == HCLGE_PPP_CMD0_INT_CMD) {
		if (en) {
			desc[0].data[0] =
				cpu_to_le32(HCLGE_PPP_MPF_ECC_ERR_INT0_EN);
			desc[0].data[1] =
				cpu_to_le32(HCLGE_PPP_MPF_ECC_ERR_INT1_EN);
		} else {
			desc[0].data[0] = 0;
			desc[0].data[1] = 0;
		}
		desc[1].data[0] =
			cpu_to_le32(HCLGE_PPP_MPF_ECC_ERR_INT0_EN_MASK);
		desc[1].data[1] =
			cpu_to_le32(HCLGE_PPP_MPF_ECC_ERR_INT1_EN_MASK);
	} else if (cmd == HCLGE_PPP_CMD1_INT_CMD) {
		if (en) {
			desc[0].data[0] =
				cpu_to_le32(HCLGE_PPP_MPF_ECC_ERR_INT2_EN);
			desc[0].data[1] =
				cpu_to_le32(HCLGE_PPP_MPF_ECC_ERR_INT3_EN);
		} else {
			desc[0].data[0] = 0;
			desc[0].data[1] = 0;
		}
		desc[1].data[0] =
				cpu_to_le32(HCLGE_PPP_MPF_ECC_ERR_INT2_EN_MASK);
		desc[1].data[1] =
				cpu_to_le32(HCLGE_PPP_MPF_ECC_ERR_INT3_EN_MASK);
	}

	ret = hclge_cmd_send(&hdev->hw, &desc[0], 2);
	if (ret)
		dev_err(dev, "fail(%d) to configure PPP error intr\n", ret);

	return ret;
}

static int hclge_config_ppp_hw_err_int(struct hclge_dev *hdev, bool en)
{
	int ret;

	ret = hclge_config_ppp_error_interrupt(hdev, HCLGE_PPP_CMD0_INT_CMD,
					       en);
	if (ret)
		return ret;

	ret = hclge_config_ppp_error_interrupt(hdev, HCLGE_PPP_CMD1_INT_CMD,
					       en);

	return ret;
}

static int hclge_config_tm_hw_err_int(struct hclge_dev *hdev, bool en)
{
	struct device *dev = &hdev->pdev->dev;
	struct hclge_desc desc;
	int ret;

	/* configure TM SCH hw errors */
	hclge_cmd_setup_basic_desc(&desc, HCLGE_TM_SCH_ECC_INT_EN, false);
	if (en)
		desc.data[0] = cpu_to_le32(HCLGE_TM_SCH_ECC_ERR_INT_EN);
	else
		desc.data[0] = 0;

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(dev, "fail(%d) to configure TM SCH errors\n", ret);
		return ret;
	}

	/* configure TM QCN hw errors */
	ret = hclge_cmd_query_error(hdev, &desc, HCLGE_TM_QCN_MEM_INT_CFG,
				    0, 0, 0);
	if (ret) {
		dev_err(dev, "fail(%d) to read TM QCN CFG status\n", ret);
		return ret;
	}

	hclge_cmd_reuse_desc(&desc, false);
	if (en)
		desc.data[1] = cpu_to_le32(HCLGE_TM_QCN_MEM_ERR_INT_EN);
	else
		desc.data[1] = 0;

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(dev,
			"fail(%d) to configure TM QCN mem errors\n", ret);

	return ret;
}

static const struct hclge_hw_blk hw_blk[] = {
	{
	  .msk = BIT(0), .name = "IGU_EGU",
	  .config_err_int = hclge_config_igu_egu_hw_err_int,
	},
	{
	  .msk = BIT(1), .name = "PPP",
	  .config_err_int = hclge_config_ppp_hw_err_int,
	},
	{
	  .msk = BIT(4), .name = "TM",
	  .config_err_int = hclge_config_tm_hw_err_int,
	},
	{
	  .msk = BIT(5), .name = "COMMON",
	  .config_err_int = hclge_config_common_hw_err_int,
	},
	{ /* sentinel */ }
};

int hclge_hw_error_set_state(struct hclge_dev *hdev, bool state)
{
	int ret = 0;
	int i = 0;

	while (hw_blk[i].name) {
		if (!hw_blk[i].config_err_int) {
			i++;
			continue;
		}
		ret = hw_blk[i].config_err_int(hdev, state);
		if (ret)
			return ret;
		i++;
	}

	return ret;
}

pci_ers_result_t hclge_process_ras_hw_error(struct hnae3_ae_dev *ae_dev)
{
	struct hclge_dev *hdev = ae_dev->priv;
	struct device *dev = &hdev->pdev->dev;
	u32 sts;

	sts = hclge_read_dev(&hdev->hw, HCLGE_RAS_PF_OTHER_INT_STS_REG);

	/* Handling Non-fatal RAS errors */
	if (sts & HCLGE_RAS_REG_NFE_MASK)
		dev_warn(dev, "HNS Non-Fatal RAS error identified\n");

	return PCI_ERS_RESULT_NEED_RESET;
}

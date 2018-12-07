// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2016-2017 Hisilicon Limited. */

#include "hclge_err.h"

static const struct hclge_hw_error hclge_imp_tcm_ecc_int[] = {
	{ .int_msk = BIT(1), .msg = "imp_itcm0_ecc_mbit_err" },
	{ .int_msk = BIT(3), .msg = "imp_itcm1_ecc_mbit_err" },
	{ .int_msk = BIT(5), .msg = "imp_itcm2_ecc_mbit_err" },
	{ .int_msk = BIT(7), .msg = "imp_itcm3_ecc_mbit_err" },
	{ .int_msk = BIT(9), .msg = "imp_dtcm0_mem0_ecc_mbit_err" },
	{ .int_msk = BIT(11), .msg = "imp_dtcm0_mem1_ecc_mbit_err" },
	{ .int_msk = BIT(13), .msg = "imp_dtcm1_mem0_ecc_mbit_err" },
	{ .int_msk = BIT(15), .msg = "imp_dtcm1_mem1_ecc_mbit_err" },
	{ .int_msk = BIT(17), .msg = "imp_itcm4_ecc_mbit_err" },
	{ /* sentinel */ }
};

static const struct hclge_hw_error hclge_cmdq_nic_mem_ecc_int[] = {
	{ .int_msk = BIT(1), .msg = "cmdq_nic_rx_depth_ecc_mbit_err" },
	{ .int_msk = BIT(3), .msg = "cmdq_nic_tx_depth_ecc_mbit_err" },
	{ .int_msk = BIT(5), .msg = "cmdq_nic_rx_tail_ecc_mbit_err" },
	{ .int_msk = BIT(7), .msg = "cmdq_nic_tx_tail_ecc_mbit_err" },
	{ .int_msk = BIT(9), .msg = "cmdq_nic_rx_head_ecc_mbit_err" },
	{ .int_msk = BIT(11), .msg = "cmdq_nic_tx_head_ecc_mbit_err" },
	{ .int_msk = BIT(13), .msg = "cmdq_nic_rx_addr_ecc_mbit_err" },
	{ .int_msk = BIT(15), .msg = "cmdq_nic_tx_addr_ecc_mbit_err" },
	{ .int_msk = BIT(17), .msg = "cmdq_rocee_rx_depth_ecc_mbit_err" },
	{ .int_msk = BIT(19), .msg = "cmdq_rocee_tx_depth_ecc_mbit_err" },
	{ .int_msk = BIT(21), .msg = "cmdq_rocee_rx_tail_ecc_mbit_err" },
	{ .int_msk = BIT(23), .msg = "cmdq_rocee_tx_tail_ecc_mbit_err" },
	{ .int_msk = BIT(25), .msg = "cmdq_rocee_rx_head_ecc_mbit_err" },
	{ .int_msk = BIT(27), .msg = "cmdq_rocee_tx_head_ecc_mbit_err" },
	{ .int_msk = BIT(29), .msg = "cmdq_rocee_rx_addr_ecc_mbit_err" },
	{ .int_msk = BIT(31), .msg = "cmdq_rocee_tx_addr_ecc_mbit_err" },
	{ /* sentinel */ }
};

static const struct hclge_hw_error hclge_tqp_int_ecc_int[] = {
	{ .int_msk = BIT(6), .msg = "tqp_int_cfg_even_ecc_mbit_err" },
	{ .int_msk = BIT(7), .msg = "tqp_int_cfg_odd_ecc_mbit_err" },
	{ .int_msk = BIT(8), .msg = "tqp_int_ctrl_even_ecc_mbit_err" },
	{ .int_msk = BIT(9), .msg = "tqp_int_ctrl_odd_ecc_mbit_err" },
	{ .int_msk = BIT(10), .msg = "tx_que_scan_int_ecc_mbit_err" },
	{ .int_msk = BIT(11), .msg = "rx_que_scan_int_ecc_mbit_err" },
	{ /* sentinel */ }
};

static const struct hclge_hw_error hclge_msix_sram_ecc_int[] = {
	{ .int_msk = BIT(1), .msg = "msix_nic_ecc_mbit_err" },
	{ .int_msk = BIT(3), .msg = "msix_rocee_ecc_mbit_err" },
	{ /* sentinel */ }
};

static const struct hclge_hw_error hclge_igu_int[] = {
	{ .int_msk = BIT(0), .msg = "igu_rx_buf0_ecc_mbit_err" },
	{ .int_msk = BIT(2), .msg = "igu_rx_buf1_ecc_mbit_err" },
	{ /* sentinel */ }
};

static const struct hclge_hw_error hclge_igu_egu_tnl_int[] = {
	{ .int_msk = BIT(0), .msg = "rx_buf_overflow" },
	{ .int_msk = BIT(1), .msg = "rx_stp_fifo_overflow" },
	{ .int_msk = BIT(2), .msg = "rx_stp_fifo_undeflow" },
	{ .int_msk = BIT(3), .msg = "tx_buf_overflow" },
	{ .int_msk = BIT(4), .msg = "tx_buf_underrun" },
	{ .int_msk = BIT(5), .msg = "rx_stp_buf_overflow" },
	{ /* sentinel */ }
};

static const struct hclge_hw_error hclge_ncsi_err_int[] = {
	{ .int_msk = BIT(1), .msg = "ncsi_tx_ecc_mbit_err" },
	{ /* sentinel */ }
};

static const struct hclge_hw_error hclge_ppp_mpf_abnormal_int_st1[] = {
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

static const struct hclge_hw_error hclge_ppp_mpf_abnormal_int_st3[] = {
	{ .int_msk = BIT(0), .msg = "hfs_fifo_mem_ecc_mbit_err" },
	{ .int_msk = BIT(1), .msg = "rslt_descr_fifo_mem_ecc_mbit_err" },
	{ .int_msk = BIT(2), .msg = "tx_vlan_tag_mem_ecc_mbit_err" },
	{ .int_msk = BIT(3), .msg = "FD_CN0_memory_ecc_mbit_err" },
	{ .int_msk = BIT(4), .msg = "FD_CN1_memory_ecc_mbit_err" },
	{ .int_msk = BIT(5), .msg = "GRO_AD_memory_ecc_mbit_err" },
	{ /* sentinel */ }
};

static const struct hclge_hw_error hclge_tm_sch_rint[] = {
	{ .int_msk = BIT(1), .msg = "tm_sch_ecc_mbit_err" },
	{ .int_msk = BIT(2), .msg = "tm_sch_port_shap_sub_fifo_wr_err" },
	{ .int_msk = BIT(3), .msg = "tm_sch_port_shap_sub_fifo_rd_err" },
	{ .int_msk = BIT(4), .msg = "tm_sch_pg_pshap_sub_fifo_wr_err" },
	{ .int_msk = BIT(5), .msg = "tm_sch_pg_pshap_sub_fifo_rd_err" },
	{ .int_msk = BIT(6), .msg = "tm_sch_pg_cshap_sub_fifo_wr_err" },
	{ .int_msk = BIT(7), .msg = "tm_sch_pg_cshap_sub_fifo_rd_err" },
	{ .int_msk = BIT(8), .msg = "tm_sch_pri_pshap_sub_fifo_wr_err" },
	{ .int_msk = BIT(9), .msg = "tm_sch_pri_pshap_sub_fifo_rd_err" },
	{ .int_msk = BIT(10), .msg = "tm_sch_pri_cshap_sub_fifo_wr_err" },
	{ .int_msk = BIT(11), .msg = "tm_sch_pri_cshap_sub_fifo_rd_err" },
	{ .int_msk = BIT(12),
	  .msg = "tm_sch_port_shap_offset_fifo_wr_err" },
	{ .int_msk = BIT(13),
	  .msg = "tm_sch_port_shap_offset_fifo_rd_err" },
	{ .int_msk = BIT(14),
	  .msg = "tm_sch_pg_pshap_offset_fifo_wr_err" },
	{ .int_msk = BIT(15),
	  .msg = "tm_sch_pg_pshap_offset_fifo_rd_err" },
	{ .int_msk = BIT(16),
	  .msg = "tm_sch_pg_cshap_offset_fifo_wr_err" },
	{ .int_msk = BIT(17),
	  .msg = "tm_sch_pg_cshap_offset_fifo_rd_err" },
	{ .int_msk = BIT(18),
	  .msg = "tm_sch_pri_pshap_offset_fifo_wr_err" },
	{ .int_msk = BIT(19),
	  .msg = "tm_sch_pri_pshap_offset_fifo_rd_err" },
	{ .int_msk = BIT(20),
	  .msg = "tm_sch_pri_cshap_offset_fifo_wr_err" },
	{ .int_msk = BIT(21),
	  .msg = "tm_sch_pri_cshap_offset_fifo_rd_err" },
	{ .int_msk = BIT(22), .msg = "tm_sch_rq_fifo_wr_err" },
	{ .int_msk = BIT(23), .msg = "tm_sch_rq_fifo_rd_err" },
	{ .int_msk = BIT(24), .msg = "tm_sch_nq_fifo_wr_err" },
	{ .int_msk = BIT(25), .msg = "tm_sch_nq_fifo_rd_err" },
	{ .int_msk = BIT(26), .msg = "tm_sch_roce_up_fifo_wr_err" },
	{ .int_msk = BIT(27), .msg = "tm_sch_roce_up_fifo_rd_err" },
	{ .int_msk = BIT(28), .msg = "tm_sch_rcb_byte_fifo_wr_err" },
	{ .int_msk = BIT(29), .msg = "tm_sch_rcb_byte_fifo_rd_err" },
	{ .int_msk = BIT(30), .msg = "tm_sch_ssu_byte_fifo_wr_err" },
	{ .int_msk = BIT(31), .msg = "tm_sch_ssu_byte_fifo_rd_err" },
	{ /* sentinel */ }
};

static const struct hclge_hw_error hclge_qcn_fifo_rint[] = {
	{ .int_msk = BIT(0), .msg = "qcn_shap_gp0_sch_fifo_rd_err" },
	{ .int_msk = BIT(1), .msg = "qcn_shap_gp0_sch_fifo_wr_err" },
	{ .int_msk = BIT(2), .msg = "qcn_shap_gp1_sch_fifo_rd_err" },
	{ .int_msk = BIT(3), .msg = "qcn_shap_gp1_sch_fifo_wr_err" },
	{ .int_msk = BIT(4), .msg = "qcn_shap_gp2_sch_fifo_rd_err" },
	{ .int_msk = BIT(5), .msg = "qcn_shap_gp2_sch_fifo_wr_err" },
	{ .int_msk = BIT(6), .msg = "qcn_shap_gp3_sch_fifo_rd_err" },
	{ .int_msk = BIT(7), .msg = "qcn_shap_gp3_sch_fifo_wr_err" },
	{ .int_msk = BIT(8), .msg = "qcn_shap_gp0_offset_fifo_rd_err" },
	{ .int_msk = BIT(9), .msg = "qcn_shap_gp0_offser_fifo_wr_err" },
	{ .int_msk = BIT(10), .msg = "qcn_shap_gp1_offset_fifo_rd_err" },
	{ .int_msk = BIT(11), .msg = "qcn_shap_gp1_offset_fifo_wr_err" },
	{ .int_msk = BIT(12), .msg = "qcn_shap_gp2_offset_fifo_rd_err" },
	{ .int_msk = BIT(13), .msg = "qcn_shap_gp2_offset_fifo_wr_err" },
	{ .int_msk = BIT(14), .msg = "qcn_shap_gp3_offset_fifo_rd_err" },
	{ .int_msk = BIT(15), .msg = "qcn_shap_gp3_offset_fifo_wr_err" },
	{ .int_msk = BIT(16), .msg = "qcn_byte_info_fifo_rd_err" },
	{ .int_msk = BIT(17), .msg = "qcn_byte_info_fifo_wr_err" },
	{ /* sentinel */ }
};

static const struct hclge_hw_error hclge_qcn_ecc_rint[] = {
	{ .int_msk = BIT(1), .msg = "qcn_byte_mem_ecc_mbit_err" },
	{ .int_msk = BIT(3), .msg = "qcn_time_mem_ecc_mbit_err" },
	{ .int_msk = BIT(5), .msg = "qcn_fb_mem_ecc_mbit_err" },
	{ .int_msk = BIT(7), .msg = "qcn_link_mem_ecc_mbit_err" },
	{ .int_msk = BIT(9), .msg = "qcn_rate_mem_ecc_mbit_err" },
	{ .int_msk = BIT(11), .msg = "qcn_tmplt_mem_ecc_mbit_err" },
	{ .int_msk = BIT(13), .msg = "qcn_shap_cfg_mem_ecc_mbit_err" },
	{ .int_msk = BIT(15), .msg = "qcn_gp0_barrel_mem_ecc_mbit_err" },
	{ .int_msk = BIT(17), .msg = "qcn_gp1_barrel_mem_ecc_mbit_err" },
	{ .int_msk = BIT(19), .msg = "qcn_gp2_barrel_mem_ecc_mbit_err" },
	{ .int_msk = BIT(21), .msg = "qcn_gp3_barral_mem_ecc_mbit_err" },
	{ /* sentinel */ }
};

static const struct hclge_hw_error hclge_mac_afifo_tnl_int[] = {
	{ .int_msk = BIT(0), .msg = "egu_cge_afifo_ecc_1bit_err" },
	{ .int_msk = BIT(1), .msg = "egu_cge_afifo_ecc_mbit_err" },
	{ .int_msk = BIT(2), .msg = "egu_lge_afifo_ecc_1bit_err" },
	{ .int_msk = BIT(3), .msg = "egu_lge_afifo_ecc_mbit_err" },
	{ .int_msk = BIT(4), .msg = "cge_igu_afifo_ecc_1bit_err" },
	{ .int_msk = BIT(5), .msg = "cge_igu_afifo_ecc_mbit_err" },
	{ .int_msk = BIT(6), .msg = "lge_igu_afifo_ecc_1bit_err" },
	{ .int_msk = BIT(7), .msg = "lge_igu_afifo_ecc_mbit_err" },
	{ /* sentinel */ }
};

static void hclge_log_error(struct device *dev, char *reg,
			    const struct hclge_hw_error *err,
			    u32 err_sts)
{
	while (err->msg) {
		if (err->int_msk & err_sts)
			dev_warn(dev, "%s %s found [error status=0x%x]\n",
				 reg, err->msg, err_sts);
		err++;
	}
}

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
		desc[0].data[4] = cpu_to_le32(HCLGE_TQP_ECC_ERR_INT_EN |
					      HCLGE_MSIX_SRAM_ECC_ERR_INT_EN);
		desc[0].data[5] = cpu_to_le32(HCLGE_IMP_ITCM4_ECC_ERR_INT_EN);
	}

	desc[1].data[0] = cpu_to_le32(HCLGE_IMP_TCM_ECC_ERR_INT_EN_MASK);
	desc[1].data[2] = cpu_to_le32(HCLGE_CMDQ_NIC_ECC_ERR_INT_EN_MASK |
				HCLGE_CMDQ_ROCEE_ECC_ERR_INT_EN_MASK);
	desc[1].data[3] = cpu_to_le32(HCLGE_IMP_RD_POISON_ERR_INT_EN_MASK);
	desc[1].data[4] = cpu_to_le32(HCLGE_TQP_ECC_ERR_INT_EN_MASK |
				      HCLGE_MSIX_SRAM_ECC_ERR_INT_EN_MASK);
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

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(dev,
			"fail(%d) to configure TM QCN mem errors\n", ret);

	return ret;
}

static int hclge_config_mac_err_int(struct hclge_dev *hdev, bool en)
{
	struct device *dev = &hdev->pdev->dev;
	struct hclge_desc desc;
	int ret;

	/* configure MAC common error interrupts */
	hclge_cmd_setup_basic_desc(&desc, HCLGE_MAC_COMMON_INT_EN, false);
	if (en)
		desc.data[0] = cpu_to_le32(HCLGE_MAC_COMMON_ERR_INT_EN);

	desc.data[1] = cpu_to_le32(HCLGE_MAC_COMMON_ERR_INT_EN_MASK);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(dev,
			"fail(%d) to configure MAC COMMON error intr\n", ret);

	return ret;
}

#define HCLGE_SET_DEFAULT_RESET_REQUEST(reset_type) \
	do { \
		if (ae_dev->ops->set_default_reset_request) \
			ae_dev->ops->set_default_reset_request(ae_dev, \
							       reset_type); \
	} while (0)

/* hclge_handle_mpf_ras_error: handle all main PF RAS errors
 * @hdev: pointer to struct hclge_dev
 * @desc: descriptor for describing the command
 * @num:  number of extended command structures
 *
 * This function handles all the main PF RAS errors in the
 * hw register/s using command.
 */
static int hclge_handle_mpf_ras_error(struct hclge_dev *hdev,
				      struct hclge_desc *desc,
				      int num)
{
	struct hnae3_ae_dev *ae_dev = hdev->ae_dev;
	struct device *dev = &hdev->pdev->dev;
	__le32 *desc_data;
	u32 status;
	int ret;

	/* query all main PF RAS errors */
	hclge_cmd_setup_basic_desc(&desc[0], HCLGE_QUERY_CLEAR_MPF_RAS_INT,
				   true);
	desc[0].flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);

	ret = hclge_cmd_send(&hdev->hw, &desc[0], num);
	if (ret) {
		dev_err(dev, "query all mpf ras int cmd failed (%d)\n", ret);
		return ret;
	}

	/* log HNS common errors */
	status = le32_to_cpu(desc[0].data[0]);
	if (status) {
		hclge_log_error(dev, "IMP_TCM_ECC_INT_STS",
				&hclge_imp_tcm_ecc_int[0], status);
		HCLGE_SET_DEFAULT_RESET_REQUEST(HNAE3_GLOBAL_RESET);
	}

	status = le32_to_cpu(desc[0].data[1]);
	if (status) {
		hclge_log_error(dev, "CMDQ_MEM_ECC_INT_STS",
				&hclge_cmdq_nic_mem_ecc_int[0], status);
		HCLGE_SET_DEFAULT_RESET_REQUEST(HNAE3_GLOBAL_RESET);
	}

	if ((le32_to_cpu(desc[0].data[2])) & BIT(0)) {
		dev_warn(dev, "imp_rd_data_poison_err found\n");
		HCLGE_SET_DEFAULT_RESET_REQUEST(HNAE3_GLOBAL_RESET);
	}

	status = le32_to_cpu(desc[0].data[3]);
	if (status) {
		hclge_log_error(dev, "TQP_INT_ECC_INT_STS",
				&hclge_tqp_int_ecc_int[0], status);
		HCLGE_SET_DEFAULT_RESET_REQUEST(HNAE3_CORE_RESET);
	}

	status = le32_to_cpu(desc[0].data[4]);
	if (status) {
		hclge_log_error(dev, "MSIX_ECC_INT_STS",
				&hclge_msix_sram_ecc_int[0], status);
		HCLGE_SET_DEFAULT_RESET_REQUEST(HNAE3_CORE_RESET);
	}

	/* log IGU(Ingress Unit) errors */
	desc_data = (__le32 *)&desc[3];
	status = le32_to_cpu(*desc_data) & HCLGE_IGU_INT_MASK;
	if (status)
		hclge_log_error(dev, "IGU_INT_STS",
				&hclge_igu_int[0], status);

	/* log PPP(Programmable Packet Process) errors */
	desc_data = (__le32 *)&desc[4];
	status = le32_to_cpu(*(desc_data + 1));
	if (status)
		hclge_log_error(dev, "PPP_MPF_ABNORMAL_INT_ST1",
				&hclge_ppp_mpf_abnormal_int_st1[0], status);

	status = le32_to_cpu(*(desc_data + 3)) & HCLGE_PPP_MPF_INT_ST3_MASK;
	if (status)
		hclge_log_error(dev, "PPP_MPF_ABNORMAL_INT_ST3",
				&hclge_ppp_mpf_abnormal_int_st3[0], status);

	/* log TM(Traffic Manager) errors */
	desc_data = (__le32 *)&desc[6];
	status = le32_to_cpu(*desc_data);
	if (status) {
		hclge_log_error(dev, "TM_SCH_RINT",
				&hclge_tm_sch_rint[0], status);
		HCLGE_SET_DEFAULT_RESET_REQUEST(HNAE3_CORE_RESET);
	}

	/* log QCN(Quantized Congestion Control) errors */
	desc_data = (__le32 *)&desc[7];
	status = le32_to_cpu(*desc_data) & HCLGE_QCN_FIFO_INT_MASK;
	if (status) {
		hclge_log_error(dev, "QCN_FIFO_RINT",
				&hclge_qcn_fifo_rint[0], status);
		HCLGE_SET_DEFAULT_RESET_REQUEST(HNAE3_CORE_RESET);
	}

	status = le32_to_cpu(*(desc_data + 1)) & HCLGE_QCN_ECC_INT_MASK;
	if (status) {
		hclge_log_error(dev, "QCN_ECC_RINT",
				&hclge_qcn_ecc_rint[0], status);
		HCLGE_SET_DEFAULT_RESET_REQUEST(HNAE3_CORE_RESET);
	}

	/* log NCSI errors */
	desc_data = (__le32 *)&desc[9];
	status = le32_to_cpu(*desc_data) & HCLGE_NCSI_ECC_INT_MASK;
	if (status) {
		hclge_log_error(dev, "NCSI_ECC_INT_RPT",
				&hclge_ncsi_err_int[0], status);
		HCLGE_SET_DEFAULT_RESET_REQUEST(HNAE3_CORE_RESET);
	}

	/* clear all main PF RAS errors */
	hclge_cmd_reuse_desc(&desc[0], false);
	desc[0].flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);

	ret = hclge_cmd_send(&hdev->hw, &desc[0], num);
	if (ret)
		dev_err(dev, "clear all mpf ras int cmd failed (%d)\n", ret);

	return ret;
}

/* hclge_handle_pf_ras_error: handle all PF RAS errors
 * @hdev: pointer to struct hclge_dev
 * @desc: descriptor for describing the command
 * @num:  number of extended command structures
 *
 * This function handles all the PF RAS errors in the
 * hw register/s using command.
 */
static int hclge_handle_pf_ras_error(struct hclge_dev *hdev,
				     struct hclge_desc *desc,
				     int num)
{
	struct device *dev = &hdev->pdev->dev;
	__le32 *desc_data;
	u32 status;
	int ret;

	/* query all PF RAS errors */
	hclge_cmd_setup_basic_desc(&desc[0], HCLGE_QUERY_CLEAR_PF_RAS_INT,
				   true);
	desc[0].flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);

	ret = hclge_cmd_send(&hdev->hw, &desc[0], num);
	if (ret) {
		dev_err(dev, "query all pf ras int cmd failed (%d)\n", ret);
		return ret;
	}

	/* log IGU(Ingress Unit) EGU(Egress Unit) TNL errors */
	desc_data = (__le32 *)&desc[1];
	status = le32_to_cpu(*desc_data) & HCLGE_IGU_EGU_TNL_INT_MASK;
	if (status)
		hclge_log_error(dev, "IGU_EGU_TNL_INT_STS",
				&hclge_igu_egu_tnl_int[0], status);

	/* clear all PF RAS errors */
	hclge_cmd_reuse_desc(&desc[0], false);
	desc[0].flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);

	ret = hclge_cmd_send(&hdev->hw, &desc[0], num);
	if (ret)
		dev_err(dev, "clear all pf ras int cmd failed (%d)\n", ret);

	return ret;
}

static int hclge_handle_all_ras_errors(struct hclge_dev *hdev)
{
	struct device *dev = &hdev->pdev->dev;
	u32 mpf_bd_num, pf_bd_num, bd_num;
	struct hclge_desc desc_bd;
	struct hclge_desc *desc;
	int ret;

	/* query the number of registers in the RAS int status */
	hclge_cmd_setup_basic_desc(&desc_bd, HCLGE_QUERY_RAS_INT_STS_BD_NUM,
				   true);
	ret = hclge_cmd_send(&hdev->hw, &desc_bd, 1);
	if (ret) {
		dev_err(dev, "fail(%d) to query ras int status bd num\n", ret);
		return ret;
	}
	mpf_bd_num = le32_to_cpu(desc_bd.data[0]);
	pf_bd_num = le32_to_cpu(desc_bd.data[1]);
	bd_num = max_t(u32, mpf_bd_num, pf_bd_num);

	desc = kcalloc(bd_num, sizeof(struct hclge_desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	/* handle all main PF RAS errors */
	ret = hclge_handle_mpf_ras_error(hdev, desc, mpf_bd_num);
	if (ret) {
		kfree(desc);
		return ret;
	}
	memset(desc, 0, bd_num * sizeof(struct hclge_desc));

	/* handle all PF RAS errors */
	ret = hclge_handle_pf_ras_error(hdev, desc, pf_bd_num);
	kfree(desc);

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
	{
	  .msk = BIT(8), .name = "MAC",
	  .config_err_int = hclge_config_mac_err_int,
	},
	{ /* sentinel */ }
};

int hclge_hw_error_set_state(struct hclge_dev *hdev, bool state)
{
	const struct hclge_hw_blk *module = hw_blk;
	int ret = 0;

	while (module->name) {
		if (module->config_err_int) {
			ret = module->config_err_int(hdev, state);
			if (ret)
				return ret;
		}
		module++;
	}

	return ret;
}

pci_ers_result_t hclge_handle_hw_ras_error(struct hnae3_ae_dev *ae_dev)
{
	struct hclge_dev *hdev = ae_dev->priv;
	struct device *dev = &hdev->pdev->dev;
	u32 status;

	status = hclge_read_dev(&hdev->hw, HCLGE_RAS_PF_OTHER_INT_STS_REG);

	/* Handling Non-fatal HNS RAS errors */
	if (status & HCLGE_RAS_REG_NFE_MASK) {
		dev_warn(dev,
			 "HNS Non-Fatal RAS error(status=0x%x) identified\n",
			 status);
		hclge_handle_all_ras_errors(hdev);
		return PCI_ERS_RESULT_NEED_RESET;
	}

	return PCI_ERS_RESULT_RECOVERED;
}

int hclge_handle_hw_msix_error(struct hclge_dev *hdev,
			       unsigned long *reset_requests)
{
	struct device *dev = &hdev->pdev->dev;
	u32 mpf_bd_num, pf_bd_num, bd_num;
	struct hclge_desc desc_bd;
	struct hclge_desc *desc;
	__le32 *desc_data;
	int ret = 0;
	u32 status;

	/* set default handling */
	set_bit(HNAE3_FUNC_RESET, reset_requests);

	/* query the number of bds for the MSIx int status */
	hclge_cmd_setup_basic_desc(&desc_bd, HCLGE_QUERY_MSIX_INT_STS_BD_NUM,
				   true);
	ret = hclge_cmd_send(&hdev->hw, &desc_bd, 1);
	if (ret) {
		dev_err(dev, "fail(%d) to query msix int status bd num\n",
			ret);
		/* reset everything for now */
		set_bit(HNAE3_GLOBAL_RESET, reset_requests);
		return ret;
	}

	mpf_bd_num = le32_to_cpu(desc_bd.data[0]);
	pf_bd_num = le32_to_cpu(desc_bd.data[1]);
	bd_num = max_t(u32, mpf_bd_num, pf_bd_num);

	desc = kcalloc(bd_num, sizeof(struct hclge_desc), GFP_KERNEL);
	if (!desc)
		goto out;

	/* query all main PF MSIx errors */
	hclge_cmd_setup_basic_desc(&desc[0], HCLGE_QUERY_CLEAR_ALL_MPF_MSIX_INT,
				   true);
	desc[0].flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);

	ret = hclge_cmd_send(&hdev->hw, &desc[0], mpf_bd_num);
	if (ret) {
		dev_err(dev, "query all mpf msix int cmd failed (%d)\n",
			ret);
		/* reset everything for now */
		set_bit(HNAE3_GLOBAL_RESET, reset_requests);
		goto msi_error;
	}

	/* log MAC errors */
	desc_data = (__le32 *)&desc[1];
	status = le32_to_cpu(*desc_data);
	if (status) {
		hclge_log_error(dev, "MAC_AFIFO_TNL_INT_R",
				&hclge_mac_afifo_tnl_int[0], status);
		set_bit(HNAE3_GLOBAL_RESET, reset_requests);
	}

	/* clear all main PF MSIx errors */
	hclge_cmd_reuse_desc(&desc[0], false);
	desc[0].flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);

	ret = hclge_cmd_send(&hdev->hw, &desc[0], mpf_bd_num);
	if (ret) {
		dev_err(dev, "clear all mpf msix int cmd failed (%d)\n",
			ret);
		/* reset everything for now */
		set_bit(HNAE3_GLOBAL_RESET, reset_requests);
		goto msi_error;
	}

	/* query all PF MSIx errors */
	memset(desc, 0, bd_num * sizeof(struct hclge_desc));
	hclge_cmd_setup_basic_desc(&desc[0], HCLGE_QUERY_CLEAR_ALL_PF_MSIX_INT,
				   true);
	desc[0].flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);

	ret = hclge_cmd_send(&hdev->hw, &desc[0], pf_bd_num);
	if (ret) {
		dev_err(dev, "query all pf msix int cmd failed (%d)\n",
			ret);
		/* reset everything for now */
		set_bit(HNAE3_GLOBAL_RESET, reset_requests);
		goto msi_error;
	}

	/* clear all PF MSIx errors */
	hclge_cmd_reuse_desc(&desc[0], false);
	desc[0].flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);

	ret = hclge_cmd_send(&hdev->hw, &desc[0], pf_bd_num);
	if (ret) {
		dev_err(dev, "clear all pf msix int cmd failed (%d)\n",
			ret);
		/* reset everything for now */
		set_bit(HNAE3_GLOBAL_RESET, reset_requests);
	}

msi_error:
	kfree(desc);
out:
	return ret;
}

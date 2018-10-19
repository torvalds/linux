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

static void hclge_log_error(struct device *dev,
			    const struct hclge_hw_error *err_list,
			    u32 err_sts)
{
	const struct hclge_hw_error *err;
	int i = 0;

	while (err_list[i].msg) {
		err = &err_list[i];
		if (!(err->int_msk & err_sts)) {
			i++;
			continue;
		}
		dev_warn(dev, "%s [error status=0x%x] found\n",
			 err->msg, err_sts);
		i++;
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

/* hclge_cmd_clear_error: clear the error status
 * @hdev: pointer to struct hclge_dev
 * @desc: descriptor for describing the command
 * @desc_src: prefilled descriptor from the previous command for reusing
 * @cmd:  command opcode
 * @flag: flag for extended command structure
 *
 * This function clear the error status in the hw register/s using command
 */
static int hclge_cmd_clear_error(struct hclge_dev *hdev,
				 struct hclge_desc *desc,
				 struct hclge_desc *desc_src,
				 u32 cmd, u16 flag)
{
	struct device *dev = &hdev->pdev->dev;
	int num = 1;
	int ret, i;

	if (cmd) {
		hclge_cmd_setup_basic_desc(&desc[0], cmd, false);
		if (flag) {
			desc[0].flag |= cpu_to_le16(flag);
			hclge_cmd_setup_basic_desc(&desc[1], cmd, false);
			num = 2;
		}
		if (desc_src) {
			for (i = 0; i < 6; i++) {
				desc[0].data[i] = desc_src[0].data[i];
				if (flag)
					desc[1].data[i] = desc_src[1].data[i];
			}
		}
	} else {
		hclge_cmd_reuse_desc(&desc[0], false);
		if (flag) {
			desc[0].flag |= cpu_to_le16(flag);
			hclge_cmd_reuse_desc(&desc[1], false);
			num = 2;
		}
	}
	ret = hclge_cmd_send(&hdev->hw, &desc[0], num);
	if (ret)
		dev_err(dev, "clear error cmd failed (%d)\n", ret);

	return ret;
}

static int hclge_enable_common_error(struct hclge_dev *hdev, bool en)
{
	struct device *dev = &hdev->pdev->dev;
	struct hclge_desc desc[2];
	int ret;

	hclge_cmd_setup_basic_desc(&desc[0], HCLGE_COMMON_ECC_INT_CFG, false);
	desc[0].flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);
	hclge_cmd_setup_basic_desc(&desc[1], HCLGE_COMMON_ECC_INT_CFG, false);

	if (en) {
		/* enable COMMON error interrupts */
		desc[0].data[0] = cpu_to_le32(HCLGE_IMP_TCM_ECC_ERR_INT_EN);
		desc[0].data[2] = cpu_to_le32(HCLGE_CMDQ_NIC_ECC_ERR_INT_EN |
					HCLGE_CMDQ_ROCEE_ECC_ERR_INT_EN);
		desc[0].data[3] = cpu_to_le32(HCLGE_IMP_RD_POISON_ERR_INT_EN);
		desc[0].data[4] = cpu_to_le32(HCLGE_TQP_ECC_ERR_INT_EN);
		desc[0].data[5] = cpu_to_le32(HCLGE_IMP_ITCM4_ECC_ERR_INT_EN);
	} else {
		/* disable COMMON error interrupts */
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
			"failed(%d) to enable/disable COMMON err interrupts\n",
			ret);

	return ret;
}

static int hclge_enable_ncsi_error(struct hclge_dev *hdev, bool en)
{
	struct device *dev = &hdev->pdev->dev;
	struct hclge_desc desc;
	int ret;

	if (hdev->pdev->revision < 0x21)
		return 0;

	/* enable/disable NCSI  error interrupts */
	hclge_cmd_setup_basic_desc(&desc, HCLGE_NCSI_INT_EN, false);
	if (en)
		desc.data[0] = cpu_to_le32(HCLGE_NCSI_ERR_INT_EN);
	else
		desc.data[0] = 0;

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(dev,
			"failed(%d) to enable/disable NCSI error interrupts\n",
			ret);

	return ret;
}

static int hclge_enable_igu_egu_error(struct hclge_dev *hdev, bool en)
{
	struct device *dev = &hdev->pdev->dev;
	struct hclge_desc desc;
	int ret;

	/* enable/disable error interrupts */
	hclge_cmd_setup_basic_desc(&desc, HCLGE_IGU_COMMON_INT_EN, false);
	if (en)
		desc.data[0] = cpu_to_le32(HCLGE_IGU_ERR_INT_EN);
	else
		desc.data[0] = 0;
	desc.data[1] = cpu_to_le32(HCLGE_IGU_ERR_INT_EN_MASK);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(dev,
			"failed(%d) to enable/disable IGU common interrupts\n",
			ret);
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
			"failed(%d) to enable/disable IGU-EGU TNL interrupts\n",
			ret);
		return ret;
	}

	ret = hclge_enable_ncsi_error(hdev, en);
	if (ret)
		dev_err(dev, "fail(%d) to en/disable err int\n", ret);

	return ret;
}

static void hclge_process_common_error(struct hclge_dev *hdev,
				       enum hclge_err_int_type type)
{
	struct device *dev = &hdev->pdev->dev;
	struct hclge_desc desc[2];
	u32 err_sts;
	int ret;

	/* read err sts */
	ret = hclge_cmd_query_error(hdev, &desc[0],
				    HCLGE_COMMON_ECC_INT_CFG,
				    HCLGE_CMD_FLAG_NEXT, 0, 0);
	if (ret) {
		dev_err(dev,
			"failed(=%d) to query COMMON error interrupt status\n",
			ret);
		return;
	}

	/* log err */
	err_sts = (le32_to_cpu(desc[0].data[0])) & HCLGE_IMP_TCM_ECC_INT_MASK;
	hclge_log_error(dev, &hclge_imp_tcm_ecc_int[0], err_sts);

	err_sts = (le32_to_cpu(desc[0].data[1])) & HCLGE_CMDQ_ECC_INT_MASK;
	hclge_log_error(dev, &hclge_cmdq_nic_mem_ecc_int[0], err_sts);

	err_sts = (le32_to_cpu(desc[0].data[1]) >> HCLGE_CMDQ_ROC_ECC_INT_SHIFT)
		   & HCLGE_CMDQ_ECC_INT_MASK;
	hclge_log_error(dev, &hclge_cmdq_rocee_mem_ecc_int[0], err_sts);

	if ((le32_to_cpu(desc[0].data[3])) & BIT(0))
		dev_warn(dev, "imp_rd_data_poison_err found\n");

	err_sts = (le32_to_cpu(desc[0].data[3]) >> HCLGE_TQP_ECC_INT_SHIFT) &
		   HCLGE_TQP_ECC_INT_MASK;
	hclge_log_error(dev, &hclge_tqp_int_ecc_int[0], err_sts);

	err_sts = (le32_to_cpu(desc[0].data[5])) &
		   HCLGE_IMP_ITCM4_ECC_INT_MASK;
	hclge_log_error(dev, &hclge_imp_itcm4_ecc_int[0], err_sts);

	/* clear error interrupts */
	desc[1].data[0] = cpu_to_le32(HCLGE_IMP_TCM_ECC_CLR_MASK);
	desc[1].data[1] = cpu_to_le32(HCLGE_CMDQ_NIC_ECC_CLR_MASK |
				HCLGE_CMDQ_ROCEE_ECC_CLR_MASK);
	desc[1].data[3] = cpu_to_le32(HCLGE_TQP_IMP_ERR_CLR_MASK);
	desc[1].data[5] = cpu_to_le32(HCLGE_IMP_ITCM4_ECC_CLR_MASK);

	ret = hclge_cmd_clear_error(hdev, &desc[0], NULL, 0,
				    HCLGE_CMD_FLAG_NEXT);
	if (ret)
		dev_err(dev,
			"failed(%d) to clear COMMON error interrupt status\n",
			ret);
}

static void hclge_process_ncsi_error(struct hclge_dev *hdev,
				     enum hclge_err_int_type type)
{
	struct device *dev = &hdev->pdev->dev;
	struct hclge_desc desc_rd;
	struct hclge_desc desc_wr;
	u32 err_sts;
	int ret;

	if (hdev->pdev->revision < 0x21)
		return;

	/* read NCSI error status */
	ret = hclge_cmd_query_error(hdev, &desc_rd, HCLGE_NCSI_INT_QUERY,
				    0, 1, HCLGE_NCSI_ERR_INT_TYPE);
	if (ret) {
		dev_err(dev,
			"failed(=%d) to query NCSI error interrupt status\n",
			ret);
		return;
	}

	/* log err */
	err_sts = le32_to_cpu(desc_rd.data[0]);
	hclge_log_error(dev, &hclge_ncsi_err_int[0], err_sts);

	/* clear err int */
	ret = hclge_cmd_clear_error(hdev, &desc_wr, &desc_rd,
				    HCLGE_NCSI_INT_CLR, 0);
	if (ret)
		dev_err(dev, "failed(=%d) to clear NCSI intrerrupt status\n",
			ret);
}

static void hclge_process_igu_egu_error(struct hclge_dev *hdev,
					enum hclge_err_int_type int_type)
{
	struct device *dev = &hdev->pdev->dev;
	struct hclge_desc desc_rd;
	struct hclge_desc desc_wr;
	u32 err_sts;
	int ret;

	/* read IGU common err sts */
	ret = hclge_cmd_query_error(hdev, &desc_rd,
				    HCLGE_IGU_COMMON_INT_QUERY,
				    0, 1, int_type);
	if (ret) {
		dev_err(dev, "failed(=%d) to query IGU common int status\n",
			ret);
		return;
	}

	/* log err */
	err_sts = le32_to_cpu(desc_rd.data[0]) &
				   HCLGE_IGU_COM_INT_MASK;
	hclge_log_error(dev, &hclge_igu_com_err_int[0], err_sts);

	/* clear err int */
	ret = hclge_cmd_clear_error(hdev, &desc_wr, &desc_rd,
				    HCLGE_IGU_COMMON_INT_CLR, 0);
	if (ret) {
		dev_err(dev, "failed(=%d) to clear IGU common int status\n",
			ret);
		return;
	}

	/* read IGU-EGU TNL err sts */
	ret = hclge_cmd_query_error(hdev, &desc_rd,
				    HCLGE_IGU_EGU_TNL_INT_QUERY,
				    0, 1, int_type);
	if (ret) {
		dev_err(dev, "failed(=%d) to query IGU-EGU TNL int status\n",
			ret);
		return;
	}

	/* log err */
	err_sts = le32_to_cpu(desc_rd.data[0]) &
				   HCLGE_IGU_EGU_TNL_INT_MASK;
	hclge_log_error(dev, &hclge_igu_egu_tnl_err_int[0], err_sts);

	/* clear err int */
	ret = hclge_cmd_clear_error(hdev, &desc_wr, &desc_rd,
				    HCLGE_IGU_EGU_TNL_INT_CLR, 0);
	if (ret) {
		dev_err(dev, "failed(=%d) to clear IGU-EGU TNL int status\n",
			ret);
		return;
	}

	hclge_process_ncsi_error(hdev, HCLGE_ERR_INT_RAS_NFE);
}

static const struct hclge_hw_blk hw_blk[] = {
	{ .msk = BIT(0), .name = "IGU_EGU",
	  .enable_error = hclge_enable_igu_egu_error,
	  .process_error = hclge_process_igu_egu_error, },
	{ .msk = BIT(5), .name = "COMMON",
	  .enable_error = hclge_enable_common_error,
	  .process_error = hclge_process_common_error, },
	{ /* sentinel */ }
};

int hclge_hw_error_set_state(struct hclge_dev *hdev, bool state)
{
	struct device *dev = &hdev->pdev->dev;
	int ret = 0;
	int i = 0;

	while (hw_blk[i].name) {
		if (!hw_blk[i].enable_error) {
			i++;
			continue;
		}
		ret = hw_blk[i].enable_error(hdev, state);
		if (ret) {
			dev_err(dev, "fail(%d) to en/disable err int\n", ret);
			return ret;
		}
		i++;
	}

	return ret;
}

pci_ers_result_t hclge_process_ras_hw_error(struct hnae3_ae_dev *ae_dev)
{
	struct hclge_dev *hdev = ae_dev->priv;
	struct device *dev = &hdev->pdev->dev;
	u32 sts, val;
	int i = 0;

	sts = hclge_read_dev(&hdev->hw, HCLGE_RAS_PF_OTHER_INT_STS_REG);

	/* Processing Non-fatal errors */
	if (sts & HCLGE_RAS_REG_NFE_MASK) {
		val = (sts >> HCLGE_RAS_REG_NFE_SHIFT) & 0xFF;
		i = 0;
		while (hw_blk[i].name) {
			if (!(hw_blk[i].msk & val)) {
				i++;
				continue;
			}
			dev_warn(dev, "%s ras non-fatal error identified\n",
				 hw_blk[i].name);
			if (hw_blk[i].process_error)
				hw_blk[i].process_error(hdev,
							 HCLGE_ERR_INT_RAS_NFE);
			i++;
		}
	}

	return PCI_ERS_RESULT_NEED_RESET;
}

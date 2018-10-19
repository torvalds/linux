// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2016-2017 Hisilicon Limited. */

#include "hclge_err.h"

static const struct hclge_hw_blk hw_blk[] = {
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

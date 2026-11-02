// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 Nebula Matrix Limited.
 */
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/bits.h>
#include <linux/io.h>
#include "nbl_hw_leonis.h"

/* Structure starts here, adding an op should not modify anything below */
static struct nbl_hw_mgt *nbl_hw_setup_hw_mgt(struct nbl_common_info *common)
{
	struct device *dev = common->dev;
	struct nbl_hw_mgt *hw_mgt;

	hw_mgt = devm_kzalloc(dev, sizeof(*hw_mgt), GFP_KERNEL);
	if (!hw_mgt)
		return ERR_PTR(-ENOMEM);

	hw_mgt->common = common;

	return hw_mgt;
}

static int nbl_pcim_request_selected_bars(struct pci_dev *pdev, u32 mask,
					  const char *name)
{
	int bar;
	int ret;

	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++) {
		if (!(mask & BIT(bar)))
			continue;
		ret = pcim_request_region(pdev, bar, name);
		if (ret)
			return ret;
	}
	return 0;
}

int nbl_hw_init_leonis(struct nbl_adapter *adapter)
{
	resource_size_t expect_sz = NBL_MEM_BAR_TOTAL_SIZE;
	struct nbl_common_info *common = &adapter->common;
	struct pci_dev *pdev = common->pdev;
	struct nbl_hw_mgt *hw_mgt = NULL;
	resource_size_t bar_len;
	resource_size_t hw_size;
	u32 bar_mask;
	int ret;

	hw_mgt = nbl_hw_setup_hw_mgt(common);
	if (IS_ERR(hw_mgt)) {
		ret = PTR_ERR(hw_mgt);
		goto setup_mgt_fail;
	}
	bar_mask = BIT(NBL_MEMORY_BAR) | BIT(NBL_MAILBOX_BAR);
	ret = nbl_pcim_request_selected_bars(pdev, bar_mask, NBL_DRIVER_NAME);
	if (ret) {
		dev_err(&pdev->dev,
			"Request memory bar failed, err = %d\n",
			ret);
		goto setup_mgt_fail;
	}

	bar_len = pci_resource_len(pdev, NBL_MEMORY_BAR);
	if (!(pci_resource_flags(pdev, NBL_MEMORY_BAR) & IORESOURCE_MEM)) {
		dev_err(&pdev->dev, "MEMORY BAR is not memory resource\n");
		ret = -EINVAL;
		goto setup_mgt_fail;
	}
	if (common->has_ctrl) {
		/*
		 * Hardware layout: MEMORY BAR total size is 64M.
		 * The tail NBL_RDMA_NOTIFY_LEN bytes of the 64M BAR are
		 * reserved exclusively for RDMA notify hardware.
		 * Ethernet driver must avoid mapping this reserved tail
		 * to prevent x86 PAT aliasing conflict between eth net
		 * mapping and RDMA driver WC mapping. Mapping starts
		 * at BAR offset 0.
		 *
		 * Skip trailing NBL_RDMA_NOTIFY_LEN bytes at BAR tail.
		 * Round size down to page boundary to avoid ioremap
		 * rounding up and accidentally including RDMA reserved
		 * region when PAGE_SIZE > 8KiB.
		 */
		if (bar_len < NBL_MEM_BAR_TOTAL_SIZE) {
			dev_err(&pdev->dev,
				"MEMORY BAR len %pa smaller than expected %pa\n",
				&bar_len, &expect_sz);
			ret = -EINVAL;
			goto setup_mgt_fail;
		}
		hw_size = PAGE_ALIGN_DOWN(NBL_MEM_BAR_TOTAL_SIZE -
					  NBL_RDMA_NOTIFY_LEN);
		hw_mgt->hw_addr =
			pcim_iomap(pdev, NBL_MEMORY_BAR,
				   hw_size);
	} else {
		if (bar_len < NBL_REG_NET_ONLY_LEN) {
			dev_err(&pdev->dev,
				"MEMORY BAR len %pa too small for net only reg space\n",
				&bar_len);
			ret = -EINVAL;
			goto setup_mgt_fail;
		}
		hw_size = NBL_REG_NET_ONLY_LEN;
		hw_mgt->hw_addr = pcim_iomap(pdev, NBL_MEMORY_BAR,
					     hw_size);
	}
	if (!hw_mgt->hw_addr) {
		dev_err(&pdev->dev, "MEMORY BAR pcim_iomap failed\n");
		ret = -EIO;
		goto setup_mgt_fail;
	}

	bar_len = pci_resource_len(pdev, NBL_MAILBOX_BAR);
	if (!(pci_resource_flags(pdev, NBL_MAILBOX_BAR) & IORESOURCE_MEM)) {
		dev_err(&pdev->dev, "MAILBOX BAR is not memory resource\n");
		ret = -EINVAL;
		goto setup_mgt_fail;
	}
	if (bar_len < NBL_BAR2_MAX_LEN) {
		dev_err(&pdev->dev, "MAILBOX BAR length %pa too small\n",
			&bar_len);
		ret = -EINVAL;
		goto setup_mgt_fail;
	}
	hw_mgt->mailbox_bar_hw_addr = pcim_iomap(pdev, NBL_MAILBOX_BAR,
						 bar_len);
	if (!hw_mgt->mailbox_bar_hw_addr) {
		dev_err(&pdev->dev, "MAILBOX BAR pcim_iomap failed\n");
		ret = -EIO;
		goto setup_mgt_fail;
	}

	hw_mgt->mailbox_bar_size = bar_len;

	adapter->core.hw_mgt = hw_mgt;

	return 0;

setup_mgt_fail:
	return ret;
}

void nbl_hw_remove_leonis(struct nbl_adapter *adapter)
{
	/* All BAR mappings & PCI regions are managed by pcim/devres,
	 * no manual iounmap / release required
	 */
}

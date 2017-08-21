/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/err.h>

#include "hinic_hw_if.h"
#include "hinic_hw_dev.h"

#define MAX_IRQS(max_qps, num_aeqs, num_ceqs)   \
		 (2 * (max_qps) + (num_aeqs) + (num_ceqs))

/**
 * init_msix - enable the msix and save the entries
 * @hwdev: the NIC HW device
 *
 * Return 0 - Success, negative - Failure
 **/
static int init_msix(struct hinic_hwdev *hwdev)
{
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	int nr_irqs, num_aeqs, num_ceqs;
	size_t msix_entries_size;
	int i, err;

	num_aeqs = HINIC_HWIF_NUM_AEQS(hwif);
	num_ceqs = HINIC_HWIF_NUM_CEQS(hwif);
	nr_irqs = MAX_IRQS(HINIC_MAX_QPS, num_aeqs, num_ceqs);
	if (nr_irqs > HINIC_HWIF_NUM_IRQS(hwif))
		nr_irqs = HINIC_HWIF_NUM_IRQS(hwif);

	msix_entries_size = nr_irqs * sizeof(*hwdev->msix_entries);
	hwdev->msix_entries = devm_kzalloc(&pdev->dev, msix_entries_size,
					   GFP_KERNEL);
	if (!hwdev->msix_entries)
		return -ENOMEM;

	for (i = 0; i < nr_irqs; i++)
		hwdev->msix_entries[i].entry = i;

	err = pci_enable_msix_exact(pdev, hwdev->msix_entries, nr_irqs);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable pci msix\n");
		return err;
	}

	return 0;
}

/**
 * disable_msix - disable the msix
 * @hwdev: the NIC HW device
 **/
static void disable_msix(struct hinic_hwdev *hwdev)
{
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;

	pci_disable_msix(pdev);
}

/**
 * init_pfhwdev - Initialize the extended components of PF
 * @pfhwdev: the HW device for PF
 *
 * Return 0 - success, negative - failure
 **/
static int init_pfhwdev(struct hinic_pfhwdev *pfhwdev)
{
	/* Initialize PF HW device extended components */
	return 0;
}

/**
 * free_pfhwdev - Free the extended components of PF
 * @pfhwdev: the HW device for PF
 **/
static void free_pfhwdev(struct hinic_pfhwdev *pfhwdev)
{
}

/**
 * hinic_init_hwdev - Initialize the NIC HW
 * @pdev: the NIC pci device
 *
 * Return initialized NIC HW device
 *
 * Initialize the NIC HW device and return a pointer to it
 **/
struct hinic_hwdev *hinic_init_hwdev(struct pci_dev *pdev)
{
	struct hinic_pfhwdev *pfhwdev;
	struct hinic_hwdev *hwdev;
	struct hinic_hwif *hwif;
	int err;

	hwif = devm_kzalloc(&pdev->dev, sizeof(*hwif), GFP_KERNEL);
	if (!hwif)
		return ERR_PTR(-ENOMEM);

	err = hinic_init_hwif(hwif, pdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to init HW interface\n");
		return ERR_PTR(err);
	}

	if (!HINIC_IS_PF(hwif) && !HINIC_IS_PPF(hwif)) {
		dev_err(&pdev->dev, "Unsupported PCI Function type\n");
		err = -EFAULT;
		goto err_func_type;
	}

	pfhwdev = devm_kzalloc(&pdev->dev, sizeof(*pfhwdev), GFP_KERNEL);
	if (!pfhwdev) {
		err = -ENOMEM;
		goto err_pfhwdev_alloc;
	}

	hwdev = &pfhwdev->hwdev;
	hwdev->hwif = hwif;

	err = init_msix(hwdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to init msix\n");
		goto err_init_msix;
	}

	err = init_pfhwdev(pfhwdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to init PF HW device\n");
		goto err_init_pfhwdev;
	}

	return hwdev;

err_init_pfhwdev:
	disable_msix(hwdev);

err_init_msix:
err_pfhwdev_alloc:
err_func_type:
	hinic_free_hwif(hwif);
	return ERR_PTR(err);
}

/**
 * hinic_free_hwdev - Free the NIC HW device
 * @hwdev: the NIC HW device
 **/
void hinic_free_hwdev(struct hinic_hwdev *hwdev)
{
	struct hinic_pfhwdev *pfhwdev = container_of(hwdev,
						     struct hinic_pfhwdev,
						     hwdev);

	free_pfhwdev(pfhwdev);

	disable_msix(hwdev);

	hinic_free_hwif(hwdev->hwif);
}

/**
 * hinic_hwdev_num_qps - return the number QPs available for use
 * @hwdev: the NIC HW device
 *
 * Return number QPs available for use
 **/
int hinic_hwdev_num_qps(struct hinic_hwdev *hwdev)
{
	int num_aeqs, num_ceqs, nr_irqs, num_qps;

	num_aeqs = HINIC_HWIF_NUM_AEQS(hwdev->hwif);
	num_ceqs = HINIC_HWIF_NUM_CEQS(hwdev->hwif);
	nr_irqs  = HINIC_HWIF_NUM_IRQS(hwdev->hwif);

	/* Each QP has its own (SQ + RQ) interrupt */
	num_qps = (nr_irqs - (num_aeqs + num_ceqs)) / 2;

	/* num_qps must be power of 2 */
	return BIT(fls(num_qps) - 1);
}

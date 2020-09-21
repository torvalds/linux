// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 */

/**
 * DOC: Nitro Enclaves (NE) PCI device driver.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nitro_enclaves.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/wait.h>

#include "ne_misc_dev.h"
#include "ne_pci_dev.h"

/**
 * NE_DEFAULT_TIMEOUT_MSECS - Default timeout to wait for a reply from
 *			      the NE PCI device.
 */
#define NE_DEFAULT_TIMEOUT_MSECS	(120000) /* 120 sec */

static const struct pci_device_id ne_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMAZON, PCI_DEVICE_ID_NE) },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, ne_pci_ids);

/**
 * ne_setup_msix() - Setup MSI-X vectors for the PCI device.
 * @pdev:	PCI device to setup the MSI-X for.
 *
 * Context: Process context.
 * Return:
 * * 0 on success.
 * * Negative return value on failure.
 */
static int ne_setup_msix(struct pci_dev *pdev)
{
	int nr_vecs = 0;
	int rc = -EINVAL;

	nr_vecs = pci_msix_vec_count(pdev);
	if (nr_vecs < 0) {
		rc = nr_vecs;

		dev_err(&pdev->dev, "Error in getting vec count [rc=%d]\n", rc);

		return rc;
	}

	rc = pci_alloc_irq_vectors(pdev, nr_vecs, nr_vecs, PCI_IRQ_MSIX);
	if (rc < 0) {
		dev_err(&pdev->dev, "Error in alloc MSI-X vecs [rc=%d]\n", rc);

		return rc;
	}

	return 0;
}

/**
 * ne_teardown_msix() - Teardown MSI-X vectors for the PCI device.
 * @pdev:	PCI device to teardown the MSI-X for.
 *
 * Context: Process context.
 */
static void ne_teardown_msix(struct pci_dev *pdev)
{
	pci_free_irq_vectors(pdev);
}

/**
 * ne_pci_dev_enable() - Select the PCI device version and enable it.
 * @pdev:	PCI device to select version for and then enable.
 *
 * Context: Process context.
 * Return:
 * * 0 on success.
 * * Negative return value on failure.
 */
static int ne_pci_dev_enable(struct pci_dev *pdev)
{
	u8 dev_enable_reply = 0;
	u16 dev_version_reply = 0;
	struct ne_pci_dev *ne_pci_dev = pci_get_drvdata(pdev);

	iowrite16(NE_VERSION_MAX, ne_pci_dev->iomem_base + NE_VERSION);

	dev_version_reply = ioread16(ne_pci_dev->iomem_base + NE_VERSION);
	if (dev_version_reply != NE_VERSION_MAX) {
		dev_err(&pdev->dev, "Error in pci dev version cmd\n");

		return -EIO;
	}

	iowrite8(NE_ENABLE_ON, ne_pci_dev->iomem_base + NE_ENABLE);

	dev_enable_reply = ioread8(ne_pci_dev->iomem_base + NE_ENABLE);
	if (dev_enable_reply != NE_ENABLE_ON) {
		dev_err(&pdev->dev, "Error in pci dev enable cmd\n");

		return -EIO;
	}

	return 0;
}

/**
 * ne_pci_dev_disable() - Disable the PCI device.
 * @pdev:	PCI device to disable.
 *
 * Context: Process context.
 */
static void ne_pci_dev_disable(struct pci_dev *pdev)
{
	u8 dev_disable_reply = 0;
	struct ne_pci_dev *ne_pci_dev = pci_get_drvdata(pdev);
	const unsigned int sleep_time = 10; /* 10 ms */
	unsigned int sleep_time_count = 0;

	iowrite8(NE_ENABLE_OFF, ne_pci_dev->iomem_base + NE_ENABLE);

	/*
	 * Check for NE_ENABLE_OFF in a loop, to handle cases when the device
	 * state is not immediately set to disabled and going through a
	 * transitory state of disabling.
	 */
	while (sleep_time_count < NE_DEFAULT_TIMEOUT_MSECS) {
		dev_disable_reply = ioread8(ne_pci_dev->iomem_base + NE_ENABLE);
		if (dev_disable_reply == NE_ENABLE_OFF)
			return;

		msleep_interruptible(sleep_time);
		sleep_time_count += sleep_time;
	}

	dev_disable_reply = ioread8(ne_pci_dev->iomem_base + NE_ENABLE);
	if (dev_disable_reply != NE_ENABLE_OFF)
		dev_err(&pdev->dev, "Error in pci dev disable cmd\n");
}

/**
 * ne_pci_probe() - Probe function for the NE PCI device.
 * @pdev:	PCI device to match with the NE PCI driver.
 * @id :	PCI device id table associated with the NE PCI driver.
 *
 * Context: Process context.
 * Return:
 * * 0 on success.
 * * Negative return value on failure.
 */
static int ne_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct ne_pci_dev *ne_pci_dev = NULL;
	int rc = -EINVAL;

	ne_pci_dev = kzalloc(sizeof(*ne_pci_dev), GFP_KERNEL);
	if (!ne_pci_dev)
		return -ENOMEM;

	rc = pci_enable_device(pdev);
	if (rc < 0) {
		dev_err(&pdev->dev, "Error in pci dev enable [rc=%d]\n", rc);

		goto free_ne_pci_dev;
	}

	rc = pci_request_regions_exclusive(pdev, "nitro_enclaves");
	if (rc < 0) {
		dev_err(&pdev->dev, "Error in pci request regions [rc=%d]\n", rc);

		goto disable_pci_dev;
	}

	ne_pci_dev->iomem_base = pci_iomap(pdev, PCI_BAR_NE, 0);
	if (!ne_pci_dev->iomem_base) {
		rc = -ENOMEM;

		dev_err(&pdev->dev, "Error in pci iomap [rc=%d]\n", rc);

		goto release_pci_regions;
	}

	pci_set_drvdata(pdev, ne_pci_dev);

	rc = ne_setup_msix(pdev);
	if (rc < 0) {
		dev_err(&pdev->dev, "Error in pci dev msix setup [rc=%d]\n", rc);

		goto iounmap_pci_bar;
	}

	ne_pci_dev_disable(pdev);

	rc = ne_pci_dev_enable(pdev);
	if (rc < 0) {
		dev_err(&pdev->dev, "Error in ne_pci_dev enable [rc=%d]\n", rc);

		goto teardown_msix;
	}

	atomic_set(&ne_pci_dev->cmd_reply_avail, 0);
	init_waitqueue_head(&ne_pci_dev->cmd_reply_wait_q);
	INIT_LIST_HEAD(&ne_pci_dev->enclaves_list);
	mutex_init(&ne_pci_dev->enclaves_list_mutex);
	mutex_init(&ne_pci_dev->pci_dev_mutex);
	ne_pci_dev->pdev = pdev;

	ne_devs.ne_pci_dev = ne_pci_dev;

	return 0;

teardown_msix:
	ne_teardown_msix(pdev);
iounmap_pci_bar:
	pci_set_drvdata(pdev, NULL);
	pci_iounmap(pdev, ne_pci_dev->iomem_base);
release_pci_regions:
	pci_release_regions(pdev);
disable_pci_dev:
	pci_disable_device(pdev);
free_ne_pci_dev:
	kfree(ne_pci_dev);

	return rc;
}

/**
 * ne_pci_remove() - Remove function for the NE PCI device.
 * @pdev:	PCI device associated with the NE PCI driver.
 *
 * Context: Process context.
 */
static void ne_pci_remove(struct pci_dev *pdev)
{
	struct ne_pci_dev *ne_pci_dev = pci_get_drvdata(pdev);

	ne_devs.ne_pci_dev = NULL;

	ne_pci_dev_disable(pdev);

	ne_teardown_msix(pdev);

	pci_set_drvdata(pdev, NULL);

	pci_iounmap(pdev, ne_pci_dev->iomem_base);

	pci_release_regions(pdev);

	pci_disable_device(pdev);

	kfree(ne_pci_dev);
}

/**
 * ne_pci_shutdown() - Shutdown function for the NE PCI device.
 * @pdev:	PCI device associated with the NE PCI driver.
 *
 * Context: Process context.
 */
static void ne_pci_shutdown(struct pci_dev *pdev)
{
	struct ne_pci_dev *ne_pci_dev = pci_get_drvdata(pdev);

	if (!ne_pci_dev)
		return;

	ne_devs.ne_pci_dev = NULL;

	ne_pci_dev_disable(pdev);

	ne_teardown_msix(pdev);

	pci_set_drvdata(pdev, NULL);

	pci_iounmap(pdev, ne_pci_dev->iomem_base);

	pci_release_regions(pdev);

	pci_disable_device(pdev);

	kfree(ne_pci_dev);
}

/*
 * TODO: Add suspend / resume functions for power management w/ CONFIG_PM, if
 * needed.
 */
/* NE PCI device driver. */
struct pci_driver ne_pci_driver = {
	.name		= "nitro_enclaves",
	.id_table	= ne_pci_ids,
	.probe		= ne_pci_probe,
	.remove		= ne_pci_remove,
	.shutdown	= ne_pci_shutdown,
};

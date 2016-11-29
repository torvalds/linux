/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2016 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more details.
 ***********************************************************************/
#include <linux/pci.h>
#include <net/vxlan.h>
#include "liquidio_common.h"
#include "octeon_droq.h"
#include "octeon_iq.h"
#include "response_manager.h"
#include "octeon_device.h"
#include "octeon_main.h"
#include "cn23xx_vf_device.h"

MODULE_AUTHOR("Cavium Networks, <support@cavium.com>");
MODULE_DESCRIPTION("Cavium LiquidIO Intelligent Server Adapter Virtual Function Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(LIQUIDIO_VERSION);

struct octeon_device_priv {
	/* Tasklet structures for this device. */
	struct tasklet_struct droq_tasklet;
	unsigned long napi_mask;
};

static int
liquidio_vf_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
static void liquidio_vf_remove(struct pci_dev *pdev);
static int octeon_device_init(struct octeon_device *oct);

static const struct pci_device_id liquidio_vf_pci_tbl[] = {
	{
		PCI_VENDOR_ID_CAVIUM, OCTEON_CN23XX_VF_VID,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0
	},
	{
		0, 0, 0, 0, 0, 0, 0
	}
};
MODULE_DEVICE_TABLE(pci, liquidio_vf_pci_tbl);

static struct pci_driver liquidio_vf_pci_driver = {
	.name		= "LiquidIO_VF",
	.id_table	= liquidio_vf_pci_tbl,
	.probe		= liquidio_vf_probe,
	.remove		= liquidio_vf_remove,
};

/**
 * \brief PCI probe handler
 * @param pdev PCI device structure
 * @param ent unused
 */
static int
liquidio_vf_probe(struct pci_dev *pdev,
		  const struct pci_device_id *ent __attribute__((unused)))
{
	struct octeon_device *oct_dev = NULL;

	oct_dev = octeon_allocate_device(pdev->device,
					 sizeof(struct octeon_device_priv));

	if (!oct_dev) {
		dev_err(&pdev->dev, "Unable to allocate device\n");
		return -ENOMEM;
	}

	dev_info(&pdev->dev, "Initializing device %x:%x.\n",
		 (u32)pdev->vendor, (u32)pdev->device);

	/* Assign octeon_device for this device to the private data area. */
	pci_set_drvdata(pdev, oct_dev);

	/* set linux specific device pointer */
	oct_dev->pci_dev = pdev;

	if (octeon_device_init(oct_dev)) {
		liquidio_vf_remove(pdev);
		return -ENOMEM;
	}

	dev_dbg(&oct_dev->pci_dev->dev, "Device is ready\n");

	return 0;
}

/**
 * \brief PCI FLR for each Octeon device.
 * @param oct octeon device
 */
static void octeon_pci_flr(struct octeon_device *oct)
{
	u16 status;

	pci_save_state(oct->pci_dev);

	pci_cfg_access_lock(oct->pci_dev);

	/* Quiesce the device completely */
	pci_write_config_word(oct->pci_dev, PCI_COMMAND,
			      PCI_COMMAND_INTX_DISABLE);

	/* Wait for Transaction Pending bit clean */
	msleep(100);
	pcie_capability_read_word(oct->pci_dev, PCI_EXP_DEVSTA, &status);
	if (status & PCI_EXP_DEVSTA_TRPND) {
		dev_info(&oct->pci_dev->dev, "Function reset incomplete after 100ms, sleeping for 5 seconds\n");
		ssleep(5);
		pcie_capability_read_word(oct->pci_dev, PCI_EXP_DEVSTA,
					  &status);
		if (status & PCI_EXP_DEVSTA_TRPND)
			dev_info(&oct->pci_dev->dev, "Function reset still incomplete after 5s, reset anyway\n");
	}
	pcie_capability_set_word(oct->pci_dev, PCI_EXP_DEVCTL,
				 PCI_EXP_DEVCTL_BCR_FLR);
	mdelay(100);

	pci_cfg_access_unlock(oct->pci_dev);

	pci_restore_state(oct->pci_dev);
}

/**
 *\brief Destroy resources associated with octeon device
 * @param pdev PCI device structure
 * @param ent unused
 */
static void octeon_destroy_resources(struct octeon_device *oct)
{
	int i;

	switch (atomic_read(&oct->status)) {
	case OCT_DEV_MBOX_SETUP_DONE:
		oct->fn_list.free_mbox(oct);

		/* fallthrough */
	case OCT_DEV_IN_RESET:
	case OCT_DEV_DROQ_INIT_DONE:
		mdelay(100);
		for (i = 0; i < MAX_OCTEON_OUTPUT_QUEUES(oct); i++) {
			if (!(oct->io_qmask.oq & BIT_ULL(i)))
				continue;
			octeon_delete_droq(oct, i);
		}

		/* fallthrough */
	case OCT_DEV_RESP_LIST_INIT_DONE:
		octeon_delete_response_list(oct);

		/* fallthrough */
	case OCT_DEV_INSTR_QUEUE_INIT_DONE:
		for (i = 0; i < MAX_OCTEON_INSTR_QUEUES(oct); i++) {
			if (!(oct->io_qmask.iq & BIT_ULL(i)))
				continue;
			octeon_delete_instr_queue(oct, i);
		}

		/* fallthrough */
	case OCT_DEV_SC_BUFF_POOL_INIT_DONE:
		octeon_free_sc_buffer_pool(oct);

		/* fallthrough */
	case OCT_DEV_DISPATCH_INIT_DONE:
		octeon_delete_dispatch_list(oct);
		cancel_delayed_work_sync(&oct->nic_poll_work.work);

		/* fallthrough */
	case OCT_DEV_PCI_MAP_DONE:
		octeon_unmap_pci_barx(oct, 0);
		octeon_unmap_pci_barx(oct, 1);

		/* fallthrough */
	case OCT_DEV_PCI_ENABLE_DONE:
		pci_clear_master(oct->pci_dev);
		/* Disable the device, releasing the PCI INT */
		pci_disable_device(oct->pci_dev);

		/* fallthrough */
	case OCT_DEV_BEGIN_STATE:
		/* Nothing to be done here either */
		break;
	}
}

/**
 * \brief Cleans up resources at unload time
 * @param pdev PCI device structure
 */
static void liquidio_vf_remove(struct pci_dev *pdev)
{
	struct octeon_device *oct_dev = pci_get_drvdata(pdev);

	dev_dbg(&oct_dev->pci_dev->dev, "Stopping device\n");

	/* Reset the octeon device and cleanup all memory allocated for
	 * the octeon device by driver.
	 */
	octeon_destroy_resources(oct_dev);

	dev_info(&oct_dev->pci_dev->dev, "Device removed\n");

	/* This octeon device has been removed. Update the global
	 * data structure to reflect this. Free the device structure.
	 */
	octeon_free_device_mem(oct_dev);
}

/**
 * \brief PCI initialization for each Octeon device.
 * @param oct octeon device
 */
static int octeon_pci_os_setup(struct octeon_device *oct)
{
#ifdef CONFIG_PCI_IOV
	/* setup PCI stuff first */
	if (!oct->pci_dev->physfn)
		octeon_pci_flr(oct);
#endif

	if (pci_enable_device(oct->pci_dev)) {
		dev_err(&oct->pci_dev->dev, "pci_enable_device failed\n");
		return 1;
	}

	if (dma_set_mask_and_coherent(&oct->pci_dev->dev, DMA_BIT_MASK(64))) {
		dev_err(&oct->pci_dev->dev, "Unexpected DMA device capability\n");
		pci_disable_device(oct->pci_dev);
		return 1;
	}

	/* Enable PCI DMA Master. */
	pci_set_master(oct->pci_dev);

	return 0;
}

/**
 * \brief Device initialization for each Octeon device that is probed
 * @param octeon_dev  octeon device
 */
static int octeon_device_init(struct octeon_device *oct)
{
	u32 rev_id;

	atomic_set(&oct->status, OCT_DEV_BEGIN_STATE);

	/* Enable access to the octeon device and make its DMA capability
	 * known to the OS.
	 */
	if (octeon_pci_os_setup(oct))
		return 1;
	atomic_set(&oct->status, OCT_DEV_PCI_ENABLE_DONE);

	oct->chip_id = OCTEON_CN23XX_VF_VID;
	pci_read_config_dword(oct->pci_dev, 8, &rev_id);
	oct->rev_id = rev_id & 0xff;

	if (cn23xx_setup_octeon_vf_device(oct))
		return 1;

	atomic_set(&oct->status, OCT_DEV_PCI_MAP_DONE);

	/* Initialize the dispatch mechanism used to push packets arriving on
	 * Octeon Output queues.
	 */
	if (octeon_init_dispatch_list(oct))
		return 1;

	atomic_set(&oct->status, OCT_DEV_DISPATCH_INIT_DONE);

	if (octeon_set_io_queues_off(oct)) {
		dev_err(&oct->pci_dev->dev, "setting io queues off failed\n");
		return 1;
	}

	if (oct->fn_list.setup_device_regs(oct)) {
		dev_err(&oct->pci_dev->dev, "device registers configuration failed\n");
		return 1;
	}

	/* Initialize soft command buffer pool */
	if (octeon_setup_sc_buffer_pool(oct)) {
		dev_err(&oct->pci_dev->dev, "sc buffer pool allocation failed\n");
		return 1;
	}
	atomic_set(&oct->status, OCT_DEV_SC_BUFF_POOL_INIT_DONE);

	/* Setup the data structures that manage this Octeon's Input queues. */
	if (octeon_setup_instr_queues(oct)) {
		dev_err(&oct->pci_dev->dev, "instruction queue initialization failed\n");
		return 1;
	}
	atomic_set(&oct->status, OCT_DEV_INSTR_QUEUE_INIT_DONE);

	/* Initialize lists to manage the requests of different types that
	 * arrive from user & kernel applications for this octeon device.
	 */
	if (octeon_setup_response_list(oct)) {
		dev_err(&oct->pci_dev->dev, "Response list allocation failed\n");
		return 1;
	}
	atomic_set(&oct->status, OCT_DEV_RESP_LIST_INIT_DONE);

	if (octeon_setup_output_queues(oct)) {
		dev_err(&oct->pci_dev->dev, "Output queue initialization failed\n");
		return 1;
	}
	atomic_set(&oct->status, OCT_DEV_DROQ_INIT_DONE);

	if (oct->fn_list.setup_mbox(oct)) {
		dev_err(&oct->pci_dev->dev, "Mailbox setup failed\n");
		return 1;
	}
	atomic_set(&oct->status, OCT_DEV_MBOX_SETUP_DONE);

	return 0;
}

static int __init liquidio_vf_init(void)
{
	octeon_init_device_list(0);
	return pci_register_driver(&liquidio_vf_pci_driver);
}

static void __exit liquidio_vf_exit(void)
{
	pci_unregister_driver(&liquidio_vf_pci_driver);

	pr_info("LiquidIO_VF network module is now unloaded\n");
}

module_init(liquidio_vf_init);
module_exit(liquidio_vf_exit);

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

static int lio_wait_for_oq_pkts(struct octeon_device *oct)
{
	struct octeon_device_priv *oct_priv =
	    (struct octeon_device_priv *)oct->priv;
	int retry = MAX_VF_IP_OP_PENDING_PKT_COUNT;
	int pkt_cnt = 0, pending_pkts;
	int i;

	do {
		pending_pkts = 0;

		for (i = 0; i < MAX_OCTEON_OUTPUT_QUEUES(oct); i++) {
			if (!(oct->io_qmask.oq & BIT_ULL(i)))
				continue;
			pkt_cnt += octeon_droq_check_hw_for_pkts(oct->droq[i]);
		}
		if (pkt_cnt > 0) {
			pending_pkts += pkt_cnt;
			tasklet_schedule(&oct_priv->droq_tasklet);
		}
		pkt_cnt = 0;
		schedule_timeout_uninterruptible(1);

	} while (retry-- && pending_pkts);

	return pkt_cnt;
}

/**
 * \brief wait for all pending requests to complete
 * @param oct Pointer to Octeon device
 *
 * Called during shutdown sequence
 */
static int wait_for_pending_requests(struct octeon_device *oct)
{
	int i, pcount = 0;

	for (i = 0; i < MAX_VF_IP_OP_PENDING_PKT_COUNT; i++) {
		pcount = atomic_read(
		    &oct->response_list[OCTEON_ORDERED_SC_LIST]
			 .pending_req_count);
		if (pcount)
			schedule_timeout_uninterruptible(HZ / 10);
		else
			break;
	}

	if (pcount)
		return 1;

	return 0;
}

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

static
int liquidio_schedule_msix_droq_pkt_handler(struct octeon_droq *droq, u64 ret)
{
	struct octeon_device *oct = droq->oct_dev;
	struct octeon_device_priv *oct_priv =
	    (struct octeon_device_priv *)oct->priv;

	if (droq->ops.poll_mode) {
		droq->ops.napi_fn(droq);
	} else {
		if (ret & MSIX_PO_INT) {
			dev_err(&oct->pci_dev->dev,
				"should not come here should not get rx when poll mode = 0 for vf\n");
			tasklet_schedule(&oct_priv->droq_tasklet);
			return 1;
		}
		/* this will be flushed periodically by check iq db */
		if (ret & MSIX_PI_INT)
			return 0;
	}
	return 0;
}

static irqreturn_t
liquidio_msix_intr_handler(int irq __attribute__((unused)), void *dev)
{
	struct octeon_ioq_vector *ioq_vector = (struct octeon_ioq_vector *)dev;
	struct octeon_device *oct = ioq_vector->oct_dev;
	struct octeon_droq *droq = oct->droq[ioq_vector->droq_index];
	u64 ret;

	ret = oct->fn_list.msix_interrupt_handler(ioq_vector);

	if ((ret & MSIX_PO_INT) || (ret & MSIX_PI_INT))
		liquidio_schedule_msix_droq_pkt_handler(droq, ret);

	return IRQ_HANDLED;
}

/**
 * \brief Setup interrupt for octeon device
 * @param oct octeon device
 *
 *  Enable interrupt in Octeon device as given in the PCI interrupt mask.
 */
static int octeon_setup_interrupt(struct octeon_device *oct)
{
	struct msix_entry *msix_entries;
	int num_alloc_ioq_vectors;
	int num_ioq_vectors;
	int irqret;
	int i;

	if (oct->msix_on) {
		oct->num_msix_irqs = oct->sriov_info.rings_per_vf;

		oct->msix_entries = kcalloc(
		    oct->num_msix_irqs, sizeof(struct msix_entry), GFP_KERNEL);
		if (!oct->msix_entries)
			return 1;

		msix_entries = (struct msix_entry *)oct->msix_entries;

		for (i = 0; i < oct->num_msix_irqs; i++)
			msix_entries[i].entry = i;
		num_alloc_ioq_vectors = pci_enable_msix_range(
						oct->pci_dev, msix_entries,
						oct->num_msix_irqs,
						oct->num_msix_irqs);
		if (num_alloc_ioq_vectors < 0) {
			dev_err(&oct->pci_dev->dev, "unable to Allocate MSI-X interrupts\n");
			kfree(oct->msix_entries);
			oct->msix_entries = NULL;
			return 1;
		}
		dev_dbg(&oct->pci_dev->dev, "OCTEON: Enough MSI-X interrupts are allocated...\n");

		num_ioq_vectors = oct->num_msix_irqs;

		for (i = 0; i < num_ioq_vectors; i++) {
			irqret = request_irq(msix_entries[i].vector,
					     liquidio_msix_intr_handler, 0,
					     "octeon", &oct->ioq_vector[i]);
			if (irqret) {
				dev_err(&oct->pci_dev->dev,
					"OCTEON: Request_irq failed for MSIX interrupt Error: %d\n",
					irqret);

				while (i) {
					i--;
					irq_set_affinity_hint(
					    msix_entries[i].vector, NULL);
					free_irq(msix_entries[i].vector,
						 &oct->ioq_vector[i]);
				}
				pci_disable_msix(oct->pci_dev);
				kfree(oct->msix_entries);
				oct->msix_entries = NULL;
				return 1;
			}
			oct->ioq_vector[i].vector = msix_entries[i].vector;
			/* assign the cpu mask for this msix interrupt vector */
			irq_set_affinity_hint(
			    msix_entries[i].vector,
			    (&oct->ioq_vector[i].affinity_mask));
		}
		dev_dbg(&oct->pci_dev->dev,
			"OCTEON[%d]: MSI-X enabled\n", oct->octeon_id);
	}
	return 0;
}

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
	oct_dev->msix_on = LIO_FLAG_MSIX_ENABLED;

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
	struct msix_entry *msix_entries;
	int i;

	switch (atomic_read(&oct->status)) {
	case OCT_DEV_RUNNING:
	case OCT_DEV_CORE_OK:
		/* No more instructions will be forwarded. */
		atomic_set(&oct->status, OCT_DEV_IN_RESET);

		dev_dbg(&oct->pci_dev->dev, "Device state is now %s\n",
			lio_get_state_string(&oct->status));

		schedule_timeout_uninterruptible(HZ / 10);

		/* fallthrough */
	case OCT_DEV_HOST_OK:
		/* fallthrough */
	case OCT_DEV_IO_QUEUES_DONE:
		if (wait_for_pending_requests(oct))
			dev_err(&oct->pci_dev->dev, "There were pending requests\n");

		if (lio_wait_for_instr_fetch(oct))
			dev_err(&oct->pci_dev->dev, "IQ had pending instructions\n");

		/* Disable the input and output queues now. No more packets will
		 * arrive from Octeon, but we should wait for all packet
		 * processing to finish.
		 */
		oct->fn_list.disable_io_queues(oct);

		if (lio_wait_for_oq_pkts(oct))
			dev_err(&oct->pci_dev->dev, "OQ had pending packets\n");

	case OCT_DEV_INTR_SET_DONE:
		/* Disable interrupts  */
		oct->fn_list.disable_interrupt(oct, OCTEON_ALL_INTR);

		if (oct->msix_on) {
			msix_entries = (struct msix_entry *)oct->msix_entries;
			for (i = 0; i < oct->num_msix_irqs; i++) {
				irq_set_affinity_hint(msix_entries[i].vector,
						      NULL);
				free_irq(msix_entries[i].vector,
					 &oct->ioq_vector[i]);
			}
			pci_disable_msix(oct->pci_dev);
			kfree(oct->msix_entries);
			oct->msix_entries = NULL;
		}
		/* Soft reset the octeon device before exiting */
		if (oct->pci_dev->reset_fn)
			octeon_pci_flr(oct);
		else
			cn23xx_vf_ask_pf_to_do_flr(oct);

		/* fallthrough */
	case OCT_DEV_MSIX_ALLOC_VECTOR_DONE:
		octeon_free_ioq_vector(oct);

		/* fallthrough */
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
	int j;

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

	if (octeon_allocate_ioq_vector(oct)) {
		dev_err(&oct->pci_dev->dev, "ioq vector allocation failed\n");
		return 1;
	}
	atomic_set(&oct->status, OCT_DEV_MSIX_ALLOC_VECTOR_DONE);

	dev_info(&oct->pci_dev->dev, "OCTEON_CN23XX VF Version: %s, %d ioqs\n",
		 LIQUIDIO_VERSION, oct->sriov_info.rings_per_vf);

	/* Setup the interrupt handler and record the INT SUM register address*/
	if (octeon_setup_interrupt(oct))
		return 1;

	if (cn23xx_octeon_pfvf_handshake(oct))
		return 1;

	/* Enable Octeon device interrupts */
	oct->fn_list.enable_interrupt(oct, OCTEON_ALL_INTR);

	atomic_set(&oct->status, OCT_DEV_INTR_SET_DONE);

	/* Enable the input and output queues for this Octeon device */
	if (oct->fn_list.enable_io_queues(oct)) {
		dev_err(&oct->pci_dev->dev, "enabling io queues failed\n");
		return 1;
	}

	atomic_set(&oct->status, OCT_DEV_IO_QUEUES_DONE);

	atomic_set(&oct->status, OCT_DEV_HOST_OK);

	/* Send Credit for Octeon Output queues. Credits are always sent after
	 * the output queue is enabled.
	 */
	for (j = 0; j < oct->num_oqs; j++)
		writel(oct->droq[j]->max_count, oct->droq[j]->pkts_credit_reg);

	/* Packets can start arriving on the output queues from this point. */

	atomic_set(&oct->status, OCT_DEV_CORE_OK);

	atomic_set(&oct->status, OCT_DEV_RUNNING);

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

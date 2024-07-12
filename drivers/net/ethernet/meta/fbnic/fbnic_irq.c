// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/pci.h>
#include <linux/types.h>

#include "fbnic.h"
#include "fbnic_netdev.h"
#include "fbnic_txrx.h"

static irqreturn_t fbnic_fw_msix_intr(int __always_unused irq, void *data)
{
	struct fbnic_dev *fbd = (struct fbnic_dev *)data;

	fbnic_mbx_poll(fbd);

	fbnic_wr32(fbd, FBNIC_INTR_MASK_CLEAR(0), 1u << FBNIC_FW_MSIX_ENTRY);

	return IRQ_HANDLED;
}

/**
 * fbnic_fw_enable_mbx - Configure and initialize Firmware Mailbox
 * @fbd: Pointer to device to initialize
 *
 * This function will initialize the firmware mailbox rings, enable the IRQ
 * and initialize the communication between the Firmware and the host. The
 * firmware is expected to respond to the initialization by sending an
 * interrupt essentially notifying the host that it has seen the
 * initialization and is now synced up.
 *
 * Return: non-zero on failure.
 **/
int fbnic_fw_enable_mbx(struct fbnic_dev *fbd)
{
	u32 vector = fbd->fw_msix_vector;
	int err;

	/* Request the IRQ for FW Mailbox vector. */
	err = request_threaded_irq(vector, NULL, &fbnic_fw_msix_intr,
				   IRQF_ONESHOT, dev_name(fbd->dev), fbd);
	if (err)
		return err;

	/* Initialize mailbox and attempt to poll it into ready state */
	fbnic_mbx_init(fbd);
	err = fbnic_mbx_poll_tx_ready(fbd);
	if (err) {
		dev_warn(fbd->dev, "FW mailbox did not enter ready state\n");
		free_irq(vector, fbd);
		return err;
	}

	/* Enable interrupts */
	fbnic_wr32(fbd, FBNIC_INTR_MASK_CLEAR(0), 1u << FBNIC_FW_MSIX_ENTRY);

	return 0;
}

/**
 * fbnic_fw_disable_mbx - Disable mailbox and place it in standby state
 * @fbd: Pointer to device to disable
 *
 * This function will disable the mailbox interrupt, free any messages still
 * in the mailbox and place it into a standby state. The firmware is
 * expected to see the update and assume that the host is in the reset state.
 **/
void fbnic_fw_disable_mbx(struct fbnic_dev *fbd)
{
	/* Disable interrupt and free vector */
	fbnic_wr32(fbd, FBNIC_INTR_MASK_SET(0), 1u << FBNIC_FW_MSIX_ENTRY);

	/* Free the vector */
	free_irq(fbd->fw_msix_vector, fbd);

	/* Make sure disabling logs message is sent, must be done here to
	 * avoid risk of completing without a running interrupt.
	 */
	fbnic_mbx_flush_tx(fbd);

	/* Reset the mailboxes to the initialized state */
	fbnic_mbx_clean(fbd);
}

static irqreturn_t fbnic_pcs_msix_intr(int __always_unused irq, void *data)
{
	struct fbnic_dev *fbd = data;
	struct fbnic_net *fbn;

	if (fbd->mac->pcs_get_link_event(fbd) == FBNIC_LINK_EVENT_NONE) {
		fbnic_wr32(fbd, FBNIC_INTR_MASK_CLEAR(0),
			   1u << FBNIC_PCS_MSIX_ENTRY);
		return IRQ_HANDLED;
	}

	fbn = netdev_priv(fbd->netdev);

	phylink_pcs_change(&fbn->phylink_pcs, false);

	return IRQ_HANDLED;
}

/**
 * fbnic_pcs_irq_enable - Configure the MAC to enable it to advertise link
 * @fbd: Pointer to device to initialize
 *
 * This function provides basic bringup for the MAC/PCS IRQ. For now the IRQ
 * will remain disabled until we start the MAC/PCS/PHY logic via phylink.
 *
 * Return: non-zero on failure.
 **/
int fbnic_pcs_irq_enable(struct fbnic_dev *fbd)
{
	u32 vector = fbd->pcs_msix_vector;
	int err;

	/* Request the IRQ for MAC link vector.
	 * Map MAC cause to it, and unmask it
	 */
	err = request_irq(vector, &fbnic_pcs_msix_intr, 0,
			  fbd->netdev->name, fbd);
	if (err)
		return err;

	fbnic_wr32(fbd, FBNIC_INTR_MSIX_CTRL(FBNIC_INTR_MSIX_CTRL_PCS_IDX),
		   FBNIC_PCS_MSIX_ENTRY | FBNIC_INTR_MSIX_CTRL_ENABLE);

	return 0;
}

/**
 * fbnic_pcs_irq_disable - Teardown the MAC IRQ to prepare for stopping
 * @fbd: Pointer to device that is stopping
 *
 * This function undoes the work done in fbnic_pcs_irq_enable and prepares
 * the device to no longer receive traffic on the host interface.
 **/
void fbnic_pcs_irq_disable(struct fbnic_dev *fbd)
{
	/* Disable interrupt */
	fbnic_wr32(fbd, FBNIC_INTR_MSIX_CTRL(FBNIC_INTR_MSIX_CTRL_PCS_IDX),
		   FBNIC_PCS_MSIX_ENTRY);
	fbnic_wr32(fbd, FBNIC_INTR_MASK_SET(0), 1u << FBNIC_PCS_MSIX_ENTRY);

	/* Free the vector */
	free_irq(fbd->pcs_msix_vector, fbd);
}

int fbnic_request_irq(struct fbnic_dev *fbd, int nr, irq_handler_t handler,
		      unsigned long flags, const char *name, void *data)
{
	struct pci_dev *pdev = to_pci_dev(fbd->dev);
	int irq = pci_irq_vector(pdev, nr);

	if (irq < 0)
		return irq;

	return request_irq(irq, handler, flags, name, data);
}

void fbnic_free_irq(struct fbnic_dev *fbd, int nr, void *data)
{
	struct pci_dev *pdev = to_pci_dev(fbd->dev);
	int irq = pci_irq_vector(pdev, nr);

	if (irq < 0)
		return;

	free_irq(irq, data);
}

void fbnic_free_irqs(struct fbnic_dev *fbd)
{
	struct pci_dev *pdev = to_pci_dev(fbd->dev);

	fbd->pcs_msix_vector = 0;
	fbd->fw_msix_vector = 0;

	fbd->num_irqs = 0;

	pci_free_irq_vectors(pdev);
}

int fbnic_alloc_irqs(struct fbnic_dev *fbd)
{
	unsigned int wanted_irqs = FBNIC_NON_NAPI_VECTORS;
	struct pci_dev *pdev = to_pci_dev(fbd->dev);
	int num_irqs;

	wanted_irqs += min_t(unsigned int, num_online_cpus(), FBNIC_MAX_RXQS);
	num_irqs = pci_alloc_irq_vectors(pdev, FBNIC_NON_NAPI_VECTORS + 1,
					 wanted_irqs, PCI_IRQ_MSIX);
	if (num_irqs < 0) {
		dev_err(fbd->dev, "Failed to allocate MSI-X entries\n");
		return num_irqs;
	}

	if (num_irqs < wanted_irqs)
		dev_warn(fbd->dev, "Allocated %d IRQs, expected %d\n",
			 num_irqs, wanted_irqs);

	fbd->num_irqs = num_irqs;

	fbd->pcs_msix_vector = pci_irq_vector(pdev, FBNIC_PCS_MSIX_ENTRY);
	fbd->fw_msix_vector = pci_irq_vector(pdev, FBNIC_FW_MSIX_ENTRY);

	return 0;
}

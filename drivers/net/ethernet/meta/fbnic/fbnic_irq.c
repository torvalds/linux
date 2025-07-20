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

static int __fbnic_fw_enable_mbx(struct fbnic_dev *fbd, int vector)
{
	int err;

	/* Initialize mailbox and attempt to poll it into ready state */
	fbnic_mbx_init(fbd);
	err = fbnic_mbx_poll_tx_ready(fbd);
	if (err) {
		dev_warn(fbd->dev, "FW mailbox did not enter ready state\n");
		return err;
	}

	/* Enable interrupt and unmask the vector */
	enable_irq(vector);
	fbnic_wr32(fbd, FBNIC_INTR_MASK_CLEAR(0), 1u << FBNIC_FW_MSIX_ENTRY);

	return 0;
}

/**
 * fbnic_fw_request_mbx - Configure and initialize Firmware Mailbox
 * @fbd: Pointer to device to initialize
 *
 * This function will allocate the IRQ and then reinitialize the mailbox
 * starting communication between the host and firmware.
 *
 * Return: non-zero on failure.
 **/
int fbnic_fw_request_mbx(struct fbnic_dev *fbd)
{
	struct pci_dev *pdev = to_pci_dev(fbd->dev);
	int vector, err;

	WARN_ON(fbd->fw_msix_vector);

	vector = pci_irq_vector(pdev, FBNIC_FW_MSIX_ENTRY);
	if (vector < 0)
		return vector;

	/* Request the IRQ for FW Mailbox vector. */
	err = request_threaded_irq(vector, NULL, &fbnic_fw_msix_intr,
				   IRQF_ONESHOT | IRQF_NO_AUTOEN,
				   dev_name(fbd->dev), fbd);
	if (err)
		return err;

	/* Initialize mailbox and attempt to poll it into ready state */
	err = __fbnic_fw_enable_mbx(fbd, vector);
	if (err)
		free_irq(vector, fbd);

	fbd->fw_msix_vector = vector;

	return err;
}

/**
 * fbnic_fw_disable_mbx - Temporarily place mailbox in standby state
 * @fbd: Pointer to device
 *
 * Shutdown the mailbox by notifying the firmware to stop sending us logs, mask
 * and synchronize the IRQ, and then clean up the rings.
 **/
static void fbnic_fw_disable_mbx(struct fbnic_dev *fbd)
{
	/* Disable interrupt and synchronize the IRQ */
	disable_irq(fbd->fw_msix_vector);

	/* Mask the vector */
	fbnic_wr32(fbd, FBNIC_INTR_MASK_SET(0), 1u << FBNIC_FW_MSIX_ENTRY);

	/* Make sure disabling logs message is sent, must be done here to
	 * avoid risk of completing without a running interrupt.
	 */
	fbnic_mbx_flush_tx(fbd);
	fbnic_mbx_clean(fbd);
}

/**
 * fbnic_fw_free_mbx - Disable mailbox and place it in standby state
 * @fbd: Pointer to device to disable
 *
 * This function will disable the mailbox interrupt, free any messages still
 * in the mailbox and place it into a disabled state. The firmware is
 * expected to see the update and assume that the host is in the reset state.
 **/
void fbnic_fw_free_mbx(struct fbnic_dev *fbd)
{
	/* Vector has already been freed */
	if (!fbd->fw_msix_vector)
		return;

	fbnic_fw_disable_mbx(fbd);

	/* Free the vector */
	free_irq(fbd->fw_msix_vector, fbd);
	fbd->fw_msix_vector = 0;
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
 * fbnic_pcs_request_irq - Configure the PCS to enable it to advertise link
 * @fbd: Pointer to device to initialize
 *
 * This function provides basic bringup for the MAC/PCS IRQ. For now the IRQ
 * will remain disabled until we start the MAC/PCS/PHY logic via phylink.
 *
 * Return: non-zero on failure.
 **/
int fbnic_pcs_request_irq(struct fbnic_dev *fbd)
{
	struct pci_dev *pdev = to_pci_dev(fbd->dev);
	int vector, err;

	WARN_ON(fbd->pcs_msix_vector);

	vector = pci_irq_vector(pdev, FBNIC_PCS_MSIX_ENTRY);
	if (vector < 0)
		return vector;

	/* Request the IRQ for PCS link vector.
	 * Map PCS cause to it, and unmask it
	 */
	err = request_irq(vector, &fbnic_pcs_msix_intr, 0,
			  fbd->netdev->name, fbd);
	if (err)
		return err;

	/* Map and enable interrupt, unmask vector after link is configured */
	fbnic_wr32(fbd, FBNIC_INTR_MSIX_CTRL(FBNIC_INTR_MSIX_CTRL_PCS_IDX),
		   FBNIC_PCS_MSIX_ENTRY | FBNIC_INTR_MSIX_CTRL_ENABLE);

	fbd->pcs_msix_vector = vector;

	return 0;
}

/**
 * fbnic_pcs_free_irq - Teardown the PCS IRQ to prepare for stopping
 * @fbd: Pointer to device that is stopping
 *
 * This function undoes the work done in fbnic_pcs_request_irq and prepares
 * the device to no longer receive traffic on the host interface.
 **/
void fbnic_pcs_free_irq(struct fbnic_dev *fbd)
{
	/* Vector has already been freed */
	if (!fbd->pcs_msix_vector)
		return;

	/* Disable interrupt */
	fbnic_wr32(fbd, FBNIC_INTR_MSIX_CTRL(FBNIC_INTR_MSIX_CTRL_PCS_IDX),
		   FBNIC_PCS_MSIX_ENTRY);
	fbnic_wrfl(fbd);

	/* Synchronize IRQ to prevent race that would unmask vector */
	synchronize_irq(fbd->pcs_msix_vector);

	/* Mask the vector */
	fbnic_wr32(fbd, FBNIC_INTR_MASK_SET(0), 1u << FBNIC_PCS_MSIX_ENTRY);

	/* Free the vector */
	free_irq(fbd->pcs_msix_vector, fbd);
	fbd->pcs_msix_vector = 0;
}

void fbnic_synchronize_irq(struct fbnic_dev *fbd, int nr)
{
	struct pci_dev *pdev = to_pci_dev(fbd->dev);
	int irq = pci_irq_vector(pdev, nr);

	if (irq < 0)
		return;

	synchronize_irq(irq);
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

void fbnic_napi_name_irqs(struct fbnic_dev *fbd)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(fbd->napi_irq); i++)
		snprintf(fbd->napi_irq[i].name,
			 sizeof(fbd->napi_irq[i].name),
			 "%s-TxRx-%u", fbd->netdev->name, i);
}

int fbnic_napi_request_irq(struct fbnic_dev *fbd,
			   struct fbnic_napi_vector *nv)
{
	struct fbnic_net *fbn = netdev_priv(fbd->netdev);
	int i = fbnic_napi_idx(nv);
	int err;

	if (!fbd->napi_irq[i].users) {
		err = fbnic_request_irq(fbd, nv->v_idx,
					fbnic_msix_clean_rings,	0,
					fbd->napi_irq[i].name,
					&fbn->napi[i]);
		if (err)
			return err;
	}

	fbd->napi_irq[i].users++;
	return 0;
}

void fbnic_napi_free_irq(struct fbnic_dev *fbd,
			 struct fbnic_napi_vector *nv)
{
	struct fbnic_net *fbn = netdev_priv(fbd->netdev);
	int i = fbnic_napi_idx(nv);

	if (--fbd->napi_irq[i].users)
		return;

	fbnic_free_irq(fbd, nv->v_idx, &fbn->napi[i]);
}

void fbnic_free_irqs(struct fbnic_dev *fbd)
{
	struct pci_dev *pdev = to_pci_dev(fbd->dev);

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

	return 0;
}

// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2024 Beijing WangXun Technology Co., Ltd. */

#include <linux/pci.h>

#include "../libwx/wx_type.h"
#include "../libwx/wx_lib.h"
#include "../libwx/wx_hw.h"
#include "txgbe_type.h"
#include "txgbe_irq.h"

/**
 * txgbe_irq_enable - Enable default interrupt generation settings
 * @wx: pointer to private structure
 * @queues: enable irqs for queues
 **/
void txgbe_irq_enable(struct wx *wx, bool queues)
{
	wr32(wx, WX_PX_MISC_IEN, TXGBE_PX_MISC_IEN_MASK);

	/* unmask interrupt */
	wx_intr_enable(wx, TXGBE_INTR_MISC);
	if (queues)
		wx_intr_enable(wx, TXGBE_INTR_QALL(wx));
}

/**
 * txgbe_intr - msi/legacy mode Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a network interface device structure
 **/
static irqreturn_t txgbe_intr(int __always_unused irq, void *data)
{
	struct wx_q_vector *q_vector;
	struct wx *wx  = data;
	struct pci_dev *pdev;
	u32 eicr;

	q_vector = wx->q_vector[0];
	pdev = wx->pdev;

	eicr = wx_misc_isb(wx, WX_ISB_VEC0);
	if (!eicr) {
		/* shared interrupt alert!
		 * the interrupt that we masked before the ICR read.
		 */
		if (netif_running(wx->netdev))
			txgbe_irq_enable(wx, true);
		return IRQ_NONE;        /* Not our interrupt */
	}
	wx->isb_mem[WX_ISB_VEC0] = 0;
	if (!(pdev->msi_enabled))
		wr32(wx, WX_PX_INTA, 1);

	wx->isb_mem[WX_ISB_MISC] = 0;
	/* would disable interrupts here but it is auto disabled */
	napi_schedule_irqoff(&q_vector->napi);

	/* re-enable link(maybe) and non-queue interrupts, no flush.
	 * txgbe_poll will re-enable the queue interrupts
	 */
	if (netif_running(wx->netdev))
		txgbe_irq_enable(wx, false);

	return IRQ_HANDLED;
}

/**
 * txgbe_request_msix_irqs - Initialize MSI-X interrupts
 * @wx: board private structure
 *
 * Allocate MSI-X vectors and request interrupts from the kernel.
 **/
static int txgbe_request_msix_irqs(struct wx *wx)
{
	struct net_device *netdev = wx->netdev;
	int vector, err;

	for (vector = 0; vector < wx->num_q_vectors; vector++) {
		struct wx_q_vector *q_vector = wx->q_vector[vector];
		struct msix_entry *entry = &wx->msix_q_entries[vector];

		if (q_vector->tx.ring && q_vector->rx.ring)
			snprintf(q_vector->name, sizeof(q_vector->name) - 1,
				 "%s-TxRx-%d", netdev->name, entry->entry);
		else
			/* skip this unused q_vector */
			continue;

		err = request_irq(entry->vector, wx_msix_clean_rings, 0,
				  q_vector->name, q_vector);
		if (err) {
			wx_err(wx, "request_irq failed for MSIX interrupt %s Error: %d\n",
			       q_vector->name, err);
			goto free_queue_irqs;
		}
	}

	return 0;

free_queue_irqs:
	while (vector) {
		vector--;
		free_irq(wx->msix_q_entries[vector].vector,
			 wx->q_vector[vector]);
	}
	wx_reset_interrupt_capability(wx);
	return err;
}

/**
 * txgbe_request_irq - initialize interrupts
 * @wx: board private structure
 *
 * Attempt to configure interrupts using the best available
 * capabilities of the hardware and kernel.
 **/
int txgbe_request_irq(struct wx *wx)
{
	struct net_device *netdev = wx->netdev;
	struct pci_dev *pdev = wx->pdev;
	int err;

	if (pdev->msix_enabled)
		err = txgbe_request_msix_irqs(wx);
	else if (pdev->msi_enabled)
		err = request_irq(wx->pdev->irq, &txgbe_intr, 0,
				  netdev->name, wx);
	else
		err = request_irq(wx->pdev->irq, &txgbe_intr, IRQF_SHARED,
				  netdev->name, wx);

	if (err)
		wx_err(wx, "request_irq failed, Error %d\n", err);

	return err;
}

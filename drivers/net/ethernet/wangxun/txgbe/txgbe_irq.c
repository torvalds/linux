// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2024 Beijing WangXun Technology Co., Ltd. */

#include <linux/irqdomain.h>
#include <linux/pci.h>

#include "../libwx/wx_type.h"
#include "../libwx/wx_lib.h"
#include "../libwx/wx_hw.h"
#include "txgbe_type.h"
#include "txgbe_phy.h"
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

static int txgbe_request_gpio_irq(struct txgbe *txgbe)
{
	txgbe->gpio_irq = irq_find_mapping(txgbe->misc.domain, TXGBE_IRQ_GPIO);
	return request_threaded_irq(txgbe->gpio_irq, NULL,
				    txgbe_gpio_irq_handler,
				    IRQF_ONESHOT, "txgbe-gpio-irq", txgbe);
}

static int txgbe_request_link_irq(struct txgbe *txgbe)
{
	txgbe->link_irq = irq_find_mapping(txgbe->misc.domain, TXGBE_IRQ_LINK);
	return request_threaded_irq(txgbe->link_irq, NULL,
				    txgbe_link_irq_handler,
				    IRQF_ONESHOT, "txgbe-link-irq", txgbe);
}

static const struct irq_chip txgbe_irq_chip = {
	.name = "txgbe-misc-irq",
};

static int txgbe_misc_irq_domain_map(struct irq_domain *d,
				     unsigned int irq,
				     irq_hw_number_t hwirq)
{
	struct txgbe *txgbe = d->host_data;

	irq_set_chip_data(irq, txgbe);
	irq_set_chip(irq, &txgbe->misc.chip);
	irq_set_nested_thread(irq, true);
	irq_set_noprobe(irq);

	return 0;
}

static const struct irq_domain_ops txgbe_misc_irq_domain_ops = {
	.map = txgbe_misc_irq_domain_map,
};

static irqreturn_t txgbe_misc_irq_handle(int irq, void *data)
{
	struct txgbe *txgbe = data;
	struct wx *wx = txgbe->wx;
	unsigned int nhandled = 0;
	unsigned int sub_irq;
	u32 eicr;

	eicr = wx_misc_isb(wx, WX_ISB_MISC);
	if (eicr & TXGBE_PX_MISC_GPIO) {
		sub_irq = irq_find_mapping(txgbe->misc.domain, TXGBE_IRQ_GPIO);
		handle_nested_irq(sub_irq);
		nhandled++;
	}
	if (eicr & (TXGBE_PX_MISC_ETH_LK | TXGBE_PX_MISC_ETH_LKDN |
		    TXGBE_PX_MISC_ETH_AN)) {
		sub_irq = irq_find_mapping(txgbe->misc.domain, TXGBE_IRQ_LINK);
		handle_nested_irq(sub_irq);
		nhandled++;
	}

	wx_intr_enable(wx, TXGBE_INTR_MISC);
	return (nhandled > 0 ? IRQ_HANDLED : IRQ_NONE);
}

static void txgbe_del_irq_domain(struct txgbe *txgbe)
{
	int hwirq, virq;

	for (hwirq = 0; hwirq < txgbe->misc.nirqs; hwirq++) {
		virq = irq_find_mapping(txgbe->misc.domain, hwirq);
		irq_dispose_mapping(virq);
	}

	irq_domain_remove(txgbe->misc.domain);
}

void txgbe_free_misc_irq(struct txgbe *txgbe)
{
	free_irq(txgbe->gpio_irq, txgbe);
	free_irq(txgbe->link_irq, txgbe);
	free_irq(txgbe->misc.irq, txgbe);
	txgbe_del_irq_domain(txgbe);
}

int txgbe_setup_misc_irq(struct txgbe *txgbe)
{
	struct wx *wx = txgbe->wx;
	int hwirq, err;

	txgbe->misc.nirqs = 2;
	txgbe->misc.domain = irq_domain_add_simple(NULL, txgbe->misc.nirqs, 0,
						   &txgbe_misc_irq_domain_ops, txgbe);
	if (!txgbe->misc.domain)
		return -ENOMEM;

	for (hwirq = 0; hwirq < txgbe->misc.nirqs; hwirq++)
		irq_create_mapping(txgbe->misc.domain, hwirq);

	txgbe->misc.chip = txgbe_irq_chip;
	if (wx->pdev->msix_enabled)
		txgbe->misc.irq = wx->msix_entry->vector;
	else
		txgbe->misc.irq = wx->pdev->irq;

	err = request_threaded_irq(txgbe->misc.irq, NULL,
				   txgbe_misc_irq_handle,
				   IRQF_ONESHOT,
				   wx->netdev->name, txgbe);
	if (err)
		goto del_misc_irq;

	err = txgbe_request_gpio_irq(txgbe);
	if (err)
		goto free_msic_irq;

	err = txgbe_request_link_irq(txgbe);
	if (err)
		goto free_gpio_irq;

	return 0;

free_gpio_irq:
	free_irq(txgbe->gpio_irq, txgbe);
free_msic_irq:
	free_irq(txgbe->misc.irq, txgbe);
del_misc_irq:
	txgbe_del_irq_domain(txgbe);

	return err;
}

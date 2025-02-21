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
 * txgbe_request_queue_irqs - Initialize MSI-X queue interrupts
 * @wx: board private structure
 *
 * Allocate MSI-X queue vectors and request interrupts from the kernel.
 **/
int txgbe_request_queue_irqs(struct wx *wx)
{
	struct net_device *netdev = wx->netdev;
	int vector, err;

	if (!wx->pdev->msix_enabled)
		return 0;

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
	struct wx_q_vector *q_vector;
	struct txgbe *txgbe = data;
	struct wx *wx = txgbe->wx;
	u32 eicr;

	if (wx->pdev->msix_enabled)
		return IRQ_WAKE_THREAD;

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
	if (!(wx->pdev->msi_enabled))
		wr32(wx, WX_PX_INTA, 1);

	/* would disable interrupts here but it is auto disabled */
	q_vector = wx->q_vector[0];
	napi_schedule_irqoff(&q_vector->napi);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t txgbe_misc_irq_thread_fn(int irq, void *data)
{
	struct txgbe *txgbe = data;
	struct wx *wx = txgbe->wx;
	unsigned int nhandled = 0;
	unsigned int sub_irq;
	u32 eicr;

	eicr = wx_misc_isb(wx, WX_ISB_MISC);
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
	if (txgbe->wx->mac.type == wx_mac_aml)
		return;

	free_irq(txgbe->link_irq, txgbe);
	free_irq(txgbe->misc.irq, txgbe);
	txgbe_del_irq_domain(txgbe);
}

int txgbe_setup_misc_irq(struct txgbe *txgbe)
{
	unsigned long flags = IRQF_ONESHOT;
	struct wx *wx = txgbe->wx;
	int hwirq, err;

	if (wx->mac.type == wx_mac_aml)
		goto skip_sp_irq;

	txgbe->misc.nirqs = 1;
	txgbe->misc.domain = irq_domain_add_simple(NULL, txgbe->misc.nirqs, 0,
						   &txgbe_misc_irq_domain_ops, txgbe);
	if (!txgbe->misc.domain)
		return -ENOMEM;

	for (hwirq = 0; hwirq < txgbe->misc.nirqs; hwirq++)
		irq_create_mapping(txgbe->misc.domain, hwirq);

	txgbe->misc.chip = txgbe_irq_chip;
	if (wx->pdev->msix_enabled) {
		txgbe->misc.irq = wx->msix_entry->vector;
	} else {
		txgbe->misc.irq = wx->pdev->irq;
		if (!wx->pdev->msi_enabled)
			flags |= IRQF_SHARED;
	}

	err = request_threaded_irq(txgbe->misc.irq, txgbe_misc_irq_handle,
				   txgbe_misc_irq_thread_fn,
				   flags,
				   wx->netdev->name, txgbe);
	if (err)
		goto del_misc_irq;

	err = txgbe_request_link_irq(txgbe);
	if (err)
		goto free_msic_irq;

skip_sp_irq:
	wx->misc_irq_domain = true;

	return 0;

free_msic_irq:
	free_irq(txgbe->misc.irq, txgbe);
del_misc_irq:
	txgbe_del_irq_domain(txgbe);

	return err;
}

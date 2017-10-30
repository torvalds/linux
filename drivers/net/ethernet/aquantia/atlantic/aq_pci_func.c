/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

/* File aq_pci_func.c: Definition of PCI functions. */

#include "aq_pci_func.h"
#include "aq_nic.h"
#include "aq_vec.h"
#include "aq_hw.h"
#include <linux/interrupt.h>

struct aq_pci_func_s {
	struct pci_dev *pdev;
	struct aq_nic_s *port[AQ_CFG_PCI_FUNC_PORTS];
	void __iomem *mmio;
	void *aq_vec[AQ_CFG_PCI_FUNC_MSIX_IRQS];
	resource_size_t mmio_pa;
	unsigned int msix_entry_mask;
	unsigned int ports;
	bool is_pci_enabled;
	bool is_regions;
	bool is_pci_using_dac;
	struct aq_hw_caps_s aq_hw_caps;
};

struct aq_pci_func_s *aq_pci_func_alloc(struct aq_hw_ops *aq_hw_ops,
					struct pci_dev *pdev,
					const struct net_device_ops *ndev_ops,
					const struct ethtool_ops *eth_ops)
{
	struct aq_pci_func_s *self = NULL;
	int err = 0;
	unsigned int port = 0U;

	if (!aq_hw_ops) {
		err = -EFAULT;
		goto err_exit;
	}
	self = kzalloc(sizeof(*self), GFP_KERNEL);
	if (!self) {
		err = -ENOMEM;
		goto err_exit;
	}

	pci_set_drvdata(pdev, self);
	self->pdev = pdev;

	err = aq_hw_ops->get_hw_caps(NULL, &self->aq_hw_caps);
	if (err < 0)
		goto err_exit;

	self->ports = self->aq_hw_caps.ports;

	for (port = 0; port < self->ports; ++port) {
		struct aq_nic_s *aq_nic = aq_nic_alloc_cold(ndev_ops, eth_ops,
							    &pdev->dev, self,
							    port, aq_hw_ops);

		if (!aq_nic) {
			err = -ENOMEM;
			goto err_exit;
		}
		self->port[port] = aq_nic;
	}

err_exit:
	if (err < 0) {
		if (self)
			aq_pci_func_free(self);
		self = NULL;
	}

	(void)err;
	return self;
}

int aq_pci_func_init(struct aq_pci_func_s *self)
{
	int err = 0;
	unsigned int bar = 0U;
	unsigned int port = 0U;
	unsigned int numvecs = 0U;

	err = pci_enable_device(self->pdev);
	if (err < 0)
		goto err_exit;

	self->is_pci_enabled = true;

	err = pci_set_dma_mask(self->pdev, DMA_BIT_MASK(64));
	if (!err) {
		err = pci_set_consistent_dma_mask(self->pdev, DMA_BIT_MASK(64));
		self->is_pci_using_dac = 1;
	}
	if (err) {
		err = pci_set_dma_mask(self->pdev, DMA_BIT_MASK(32));
		if (!err)
			err = pci_set_consistent_dma_mask(self->pdev,
							  DMA_BIT_MASK(32));
		self->is_pci_using_dac = 0;
	}
	if (err != 0) {
		err = -ENOSR;
		goto err_exit;
	}

	err = pci_request_regions(self->pdev, AQ_CFG_DRV_NAME "_mmio");
	if (err < 0)
		goto err_exit;

	self->is_regions = true;

	pci_set_master(self->pdev);

	for (bar = 0; bar < 4; ++bar) {
		if (IORESOURCE_MEM & pci_resource_flags(self->pdev, bar)) {
			resource_size_t reg_sz;

			self->mmio_pa = pci_resource_start(self->pdev, bar);
			if (self->mmio_pa == 0U) {
				err = -EIO;
				goto err_exit;
			}

			reg_sz = pci_resource_len(self->pdev, bar);
			if ((reg_sz <= 24 /*ATL_REGS_SIZE*/)) {
				err = -EIO;
				goto err_exit;
			}

			self->mmio = ioremap_nocache(self->mmio_pa, reg_sz);
			if (!self->mmio) {
				err = -EIO;
				goto err_exit;
			}
			break;
		}
	}

	numvecs = min((u8)AQ_CFG_VECS_DEF, self->aq_hw_caps.msix_irqs);
	numvecs = min(numvecs, num_online_cpus());

	/* enable interrupts */
#if !AQ_CFG_FORCE_LEGACY_INT
	err = pci_alloc_irq_vectors(self->pdev, numvecs, numvecs, PCI_IRQ_MSIX);

	if (err < 0) {
		err = pci_alloc_irq_vectors(self->pdev, 1, 1,
				PCI_IRQ_MSI | PCI_IRQ_LEGACY);
		if (err < 0)
			goto err_exit;
	}
#endif /* AQ_CFG_FORCE_LEGACY_INT */

	/* net device init */
	for (port = 0; port < self->ports; ++port) {
		if (!self->port[port])
			continue;

		err = aq_nic_cfg_start(self->port[port]);
		if (err < 0)
			goto err_exit;

		err = aq_nic_ndev_init(self->port[port]);
		if (err < 0)
			goto err_exit;

		err = aq_nic_ndev_register(self->port[port]);
		if (err < 0)
			goto err_exit;
	}

err_exit:
	if (err < 0)
		aq_pci_func_deinit(self);
	return err;
}

int aq_pci_func_alloc_irq(struct aq_pci_func_s *self, unsigned int i,
			  char *name, void *aq_vec, cpumask_t *affinity_mask)
{
	struct pci_dev *pdev = self->pdev;
	int err = 0;

	if (pdev->msix_enabled || pdev->msi_enabled)
		err = request_irq(pci_irq_vector(pdev, i), aq_vec_isr, 0,
				  name, aq_vec);
	else
		err = request_irq(pci_irq_vector(pdev, i), aq_vec_isr_legacy,
				  IRQF_SHARED, name, aq_vec);

	if (err >= 0) {
		self->msix_entry_mask |= (1 << i);
		self->aq_vec[i] = aq_vec;

		if (pdev->msix_enabled)
			irq_set_affinity_hint(pci_irq_vector(pdev, i),
					      affinity_mask);
	}

	return err;
}

void aq_pci_func_free_irqs(struct aq_pci_func_s *self)
{
	struct pci_dev *pdev = self->pdev;
	unsigned int i = 0U;

	for (i = 32U; i--;) {
		if (!((1U << i) & self->msix_entry_mask))
			continue;

		if (pdev->msix_enabled)
			irq_set_affinity_hint(pci_irq_vector(pdev, i), NULL);
		free_irq(pci_irq_vector(pdev, i), self->aq_vec[i]);
		self->msix_entry_mask &= ~(1U << i);
	}
}

void __iomem *aq_pci_func_get_mmio(struct aq_pci_func_s *self)
{
	return self->mmio;
}

unsigned int aq_pci_func_get_irq_type(struct aq_pci_func_s *self)
{
	if (self->pdev->msix_enabled)
		return AQ_HW_IRQ_MSIX;
	if (self->pdev->msi_enabled)
		return AQ_HW_IRQ_MSIX;
	return AQ_HW_IRQ_LEGACY;
}

void aq_pci_func_deinit(struct aq_pci_func_s *self)
{
	if (!self)
		goto err_exit;

	aq_pci_func_free_irqs(self);
	pci_free_irq_vectors(self->pdev);

	if (self->is_regions)
		pci_release_regions(self->pdev);

	if (self->is_pci_enabled)
		pci_disable_device(self->pdev);

err_exit:;
}

void aq_pci_func_free(struct aq_pci_func_s *self)
{
	unsigned int port = 0U;

	if (!self)
		goto err_exit;

	for (port = 0; port < self->ports; ++port) {
		if (!self->port[port])
			continue;

		aq_nic_ndev_free(self->port[port]);
	}

	if (self->mmio)
		iounmap(self->mmio);

	kfree(self);

err_exit:;
}

int aq_pci_func_change_pm_state(struct aq_pci_func_s *self,
				pm_message_t *pm_msg)
{
	int err = 0;
	unsigned int port = 0U;

	if (!self) {
		err = -EFAULT;
		goto err_exit;
	}
	for (port = 0; port < self->ports; ++port) {
		if (!self->port[port])
			continue;

		(void)aq_nic_change_pm_state(self->port[port], pm_msg);
	}

err_exit:
	return err;
}

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
	unsigned int irq_type;
	unsigned int ports;
	bool is_pci_enabled;
	bool is_regions;
	bool is_pci_using_dac;
	struct aq_hw_caps_s aq_hw_caps;
	struct msix_entry msix_entry[AQ_CFG_PCI_FUNC_MSIX_IRQS];
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
	unsigned int i = 0U;

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

	for (i = 0; i < self->aq_hw_caps.msix_irqs; i++)
		self->msix_entry[i].entry = i;

	/*enable interrupts */
#if AQ_CFG_FORCE_LEGACY_INT
	self->irq_type = AQ_HW_IRQ_LEGACY;
#else
	err = pci_enable_msix(self->pdev, self->msix_entry,
			      self->aq_hw_caps.msix_irqs);

	if (err >= 0) {
		self->irq_type = AQ_HW_IRQ_MSIX;
	} else {
		err = pci_enable_msi(self->pdev);

		if (err >= 0) {
			self->irq_type = AQ_HW_IRQ_MSI;
		} else {
			self->irq_type = AQ_HW_IRQ_LEGACY;
			err = 0;
		}
	}
#endif

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
	int err = 0;

	switch (self->irq_type) {
	case AQ_HW_IRQ_MSIX:
		err = request_irq(self->msix_entry[i].vector, aq_vec_isr, 0,
				  name, aq_vec);
		break;

	case AQ_HW_IRQ_MSI:
		err = request_irq(self->pdev->irq, aq_vec_isr, 0, name, aq_vec);
		break;

	case AQ_HW_IRQ_LEGACY:
		err = request_irq(self->pdev->irq, aq_vec_isr_legacy,
				  IRQF_SHARED, name, aq_vec);
		break;

	default:
		err = -EFAULT;
		break;
	}

	if (err >= 0) {
		self->msix_entry_mask |= (1 << i);
		self->aq_vec[i] = aq_vec;

		if (self->irq_type == AQ_HW_IRQ_MSIX)
			irq_set_affinity_hint(self->msix_entry[i].vector,
					      affinity_mask);
	}

	return err;
}

void aq_pci_func_free_irqs(struct aq_pci_func_s *self)
{
	unsigned int i = 0U;

	for (i = 32U; i--;) {
		if (!((1U << i) & self->msix_entry_mask))
			continue;

		switch (self->irq_type) {
		case AQ_HW_IRQ_MSIX:
			irq_set_affinity_hint(self->msix_entry[i].vector, NULL);
			free_irq(self->msix_entry[i].vector, self->aq_vec[i]);
			break;

		case AQ_HW_IRQ_MSI:
			free_irq(self->pdev->irq, self->aq_vec[i]);
			break;

		case AQ_HW_IRQ_LEGACY:
			free_irq(self->pdev->irq, self->aq_vec[i]);
			break;

		default:
			break;
		}

		self->msix_entry_mask &= ~(1U << i);
	}
}

void __iomem *aq_pci_func_get_mmio(struct aq_pci_func_s *self)
{
	return self->mmio;
}

unsigned int aq_pci_func_get_irq_type(struct aq_pci_func_s *self)
{
	return self->irq_type;
}

void aq_pci_func_deinit(struct aq_pci_func_s *self)
{
	if (!self)
		goto err_exit;

	aq_pci_func_free_irqs(self);

	switch (self->irq_type) {
	case AQ_HW_IRQ_MSI:
		pci_disable_msi(self->pdev);
		break;

	case AQ_HW_IRQ_MSIX:
		pci_disable_msix(self->pdev);
		break;

	case AQ_HW_IRQ_LEGACY:
		break;

	default:
		break;
	}

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

// SPDX-License-Identifier: GPL-2.0-only
/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2019 aQuantia Corporation. All rights reserved
 */

/* File aq_pci_func.c: Definition of PCI functions. */

#include <linux/interrupt.h>
#include <linux/module.h>

#include "aq_main.h"
#include "aq_nic.h"
#include "aq_vec.h"
#include "aq_hw.h"
#include "aq_pci_func.h"
#include "hw_atl/hw_atl_a0.h"
#include "hw_atl/hw_atl_b0.h"
#include "aq_filters.h"
#include "aq_drvinfo.h"
#include "aq_macsec.h"

static const struct pci_device_id aq_pci_tbl[] = {
	{ PCI_VDEVICE(AQUANTIA, AQ_DEVICE_ID_0001), },
	{ PCI_VDEVICE(AQUANTIA, AQ_DEVICE_ID_D100), },
	{ PCI_VDEVICE(AQUANTIA, AQ_DEVICE_ID_D107), },
	{ PCI_VDEVICE(AQUANTIA, AQ_DEVICE_ID_D108), },
	{ PCI_VDEVICE(AQUANTIA, AQ_DEVICE_ID_D109), },

	{ PCI_VDEVICE(AQUANTIA, AQ_DEVICE_ID_AQC100), },
	{ PCI_VDEVICE(AQUANTIA, AQ_DEVICE_ID_AQC107), },
	{ PCI_VDEVICE(AQUANTIA, AQ_DEVICE_ID_AQC108), },
	{ PCI_VDEVICE(AQUANTIA, AQ_DEVICE_ID_AQC109), },
	{ PCI_VDEVICE(AQUANTIA, AQ_DEVICE_ID_AQC111), },
	{ PCI_VDEVICE(AQUANTIA, AQ_DEVICE_ID_AQC112), },

	{ PCI_VDEVICE(AQUANTIA, AQ_DEVICE_ID_AQC100S), },
	{ PCI_VDEVICE(AQUANTIA, AQ_DEVICE_ID_AQC107S), },
	{ PCI_VDEVICE(AQUANTIA, AQ_DEVICE_ID_AQC108S), },
	{ PCI_VDEVICE(AQUANTIA, AQ_DEVICE_ID_AQC109S), },
	{ PCI_VDEVICE(AQUANTIA, AQ_DEVICE_ID_AQC111S), },
	{ PCI_VDEVICE(AQUANTIA, AQ_DEVICE_ID_AQC112S), },

	{}
};

static const struct aq_board_revision_s hw_atl_boards[] = {
	{ AQ_DEVICE_ID_0001,	AQ_HWREV_1,	&hw_atl_ops_a0, &hw_atl_a0_caps_aqc107, },
	{ AQ_DEVICE_ID_D100,	AQ_HWREV_1,	&hw_atl_ops_a0, &hw_atl_a0_caps_aqc100, },
	{ AQ_DEVICE_ID_D107,	AQ_HWREV_1,	&hw_atl_ops_a0, &hw_atl_a0_caps_aqc107, },
	{ AQ_DEVICE_ID_D108,	AQ_HWREV_1,	&hw_atl_ops_a0, &hw_atl_a0_caps_aqc108, },
	{ AQ_DEVICE_ID_D109,	AQ_HWREV_1,	&hw_atl_ops_a0, &hw_atl_a0_caps_aqc109, },

	{ AQ_DEVICE_ID_0001,	AQ_HWREV_2,	&hw_atl_ops_b0, &hw_atl_b0_caps_aqc107, },
	{ AQ_DEVICE_ID_D100,	AQ_HWREV_2,	&hw_atl_ops_b0, &hw_atl_b0_caps_aqc100, },
	{ AQ_DEVICE_ID_D107,	AQ_HWREV_2,	&hw_atl_ops_b0, &hw_atl_b0_caps_aqc107, },
	{ AQ_DEVICE_ID_D108,	AQ_HWREV_2,	&hw_atl_ops_b0, &hw_atl_b0_caps_aqc108, },
	{ AQ_DEVICE_ID_D109,	AQ_HWREV_2,	&hw_atl_ops_b0, &hw_atl_b0_caps_aqc109, },

	{ AQ_DEVICE_ID_AQC100,	AQ_HWREV_ANY,	&hw_atl_ops_b1, &hw_atl_b0_caps_aqc107, },
	{ AQ_DEVICE_ID_AQC107,	AQ_HWREV_ANY,	&hw_atl_ops_b1, &hw_atl_b0_caps_aqc107, },
	{ AQ_DEVICE_ID_AQC108,	AQ_HWREV_ANY,	&hw_atl_ops_b1, &hw_atl_b0_caps_aqc108, },
	{ AQ_DEVICE_ID_AQC109,	AQ_HWREV_ANY,	&hw_atl_ops_b1, &hw_atl_b0_caps_aqc109, },
	{ AQ_DEVICE_ID_AQC111,	AQ_HWREV_ANY,	&hw_atl_ops_b1, &hw_atl_b0_caps_aqc111, },
	{ AQ_DEVICE_ID_AQC112,	AQ_HWREV_ANY,	&hw_atl_ops_b1, &hw_atl_b0_caps_aqc112, },

	{ AQ_DEVICE_ID_AQC100S,	AQ_HWREV_ANY,	&hw_atl_ops_b1, &hw_atl_b0_caps_aqc100s, },
	{ AQ_DEVICE_ID_AQC107S,	AQ_HWREV_ANY,	&hw_atl_ops_b1, &hw_atl_b0_caps_aqc107s, },
	{ AQ_DEVICE_ID_AQC108S,	AQ_HWREV_ANY,	&hw_atl_ops_b1, &hw_atl_b0_caps_aqc108s, },
	{ AQ_DEVICE_ID_AQC109S,	AQ_HWREV_ANY,	&hw_atl_ops_b1, &hw_atl_b0_caps_aqc109s, },
	{ AQ_DEVICE_ID_AQC111S,	AQ_HWREV_ANY,	&hw_atl_ops_b1, &hw_atl_b0_caps_aqc111s, },
	{ AQ_DEVICE_ID_AQC112S,	AQ_HWREV_ANY,	&hw_atl_ops_b1, &hw_atl_b0_caps_aqc112s, },
};

MODULE_DEVICE_TABLE(pci, aq_pci_tbl);

static int aq_pci_probe_get_hw_by_id(struct pci_dev *pdev,
				     const struct aq_hw_ops **ops,
				     const struct aq_hw_caps_s **caps)
{
	int i;

	if (pdev->vendor != PCI_VENDOR_ID_AQUANTIA)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(hw_atl_boards); i++) {
		if (hw_atl_boards[i].devid == pdev->device &&
		    (hw_atl_boards[i].revision == AQ_HWREV_ANY ||
		     hw_atl_boards[i].revision == pdev->revision)) {
			*ops = hw_atl_boards[i].ops;
			*caps = hw_atl_boards[i].caps;
			break;
		}
	}

	if (i == ARRAY_SIZE(hw_atl_boards))
		return -EINVAL;

	return 0;
}

int aq_pci_func_init(struct pci_dev *pdev)
{
	int err;

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (!err) {
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));

	}
	if (err) {
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (!err)
			err = pci_set_consistent_dma_mask(pdev,
							  DMA_BIT_MASK(32));
	}
	if (err != 0) {
		err = -ENOSR;
		goto err_exit;
	}

	err = pci_request_regions(pdev, AQ_CFG_DRV_NAME "_mmio");
	if (err < 0)
		goto err_exit;

	pci_set_master(pdev);

	return 0;

err_exit:
	return err;
}

int aq_pci_func_alloc_irq(struct aq_nic_s *self, unsigned int i,
			  char *name, irq_handler_t irq_handler,
			  void *irq_arg, cpumask_t *affinity_mask)
{
	struct pci_dev *pdev = self->pdev;
	int err;

	if (pdev->msix_enabled || pdev->msi_enabled)
		err = request_irq(pci_irq_vector(pdev, i), irq_handler, 0,
				  name, irq_arg);
	else
		err = request_irq(pci_irq_vector(pdev, i), aq_vec_isr_legacy,
				  IRQF_SHARED, name, irq_arg);

	if (err >= 0) {
		self->msix_entry_mask |= (1 << i);

		if (pdev->msix_enabled && affinity_mask)
			irq_set_affinity_hint(pci_irq_vector(pdev, i),
					      affinity_mask);
	}

	return err;
}

void aq_pci_func_free_irqs(struct aq_nic_s *self)
{
	struct pci_dev *pdev = self->pdev;
	unsigned int i;
	void *irq_data;

	for (i = 32U; i--;) {
		if (!((1U << i) & self->msix_entry_mask))
			continue;
		if (self->aq_nic_cfg.link_irq_vec &&
		    i == self->aq_nic_cfg.link_irq_vec)
			irq_data = self;
		else if (i < AQ_CFG_VECS_MAX)
			irq_data = self->aq_vec[i];
		else
			continue;

		if (pdev->msix_enabled)
			irq_set_affinity_hint(pci_irq_vector(pdev, i), NULL);
		free_irq(pci_irq_vector(pdev, i), irq_data);
		self->msix_entry_mask &= ~(1U << i);
	}
}

unsigned int aq_pci_func_get_irq_type(struct aq_nic_s *self)
{
	if (self->pdev->msix_enabled)
		return AQ_HW_IRQ_MSIX;
	if (self->pdev->msi_enabled)
		return AQ_HW_IRQ_MSI;

	return AQ_HW_IRQ_LEGACY;
}

static void aq_pci_free_irq_vectors(struct aq_nic_s *self)
{
	pci_free_irq_vectors(self->pdev);
}

static int aq_pci_probe(struct pci_dev *pdev,
			const struct pci_device_id *pci_id)
{
	struct net_device *ndev;
	resource_size_t mmio_pa;
	struct aq_nic_s *self;
	u32 numvecs;
	u32 bar;
	int err;

	err = pci_enable_device(pdev);
	if (err)
		return err;

	err = aq_pci_func_init(pdev);
	if (err)
		goto err_pci_func;

	ndev = aq_ndev_alloc();
	if (!ndev) {
		err = -ENOMEM;
		goto err_ndev;
	}

	self = netdev_priv(ndev);
	self->pdev = pdev;
	SET_NETDEV_DEV(ndev, &pdev->dev);
	pci_set_drvdata(pdev, self);

	mutex_init(&self->fwreq_mutex);

	err = aq_pci_probe_get_hw_by_id(pdev, &self->aq_hw_ops,
					&aq_nic_get_cfg(self)->aq_hw_caps);
	if (err)
		goto err_ioremap;

	self->aq_hw = kzalloc(sizeof(*self->aq_hw), GFP_KERNEL);
	if (!self->aq_hw) {
		err = -ENOMEM;
		goto err_ioremap;
	}
	self->aq_hw->aq_nic_cfg = aq_nic_get_cfg(self);

	for (bar = 0; bar < 4; ++bar) {
		if (IORESOURCE_MEM & pci_resource_flags(pdev, bar)) {
			resource_size_t reg_sz;

			mmio_pa = pci_resource_start(pdev, bar);
			if (mmio_pa == 0U) {
				err = -EIO;
				goto err_free_aq_hw;
			}

			reg_sz = pci_resource_len(pdev, bar);
			if ((reg_sz <= 24 /*ATL_REGS_SIZE*/)) {
				err = -EIO;
				goto err_free_aq_hw;
			}

			self->aq_hw->mmio = ioremap(mmio_pa, reg_sz);
			if (!self->aq_hw->mmio) {
				err = -EIO;
				goto err_free_aq_hw;
			}
			break;
		}
	}

	if (bar == 4) {
		err = -EIO;
		goto err_free_aq_hw;
	}

	numvecs = min((u8)AQ_CFG_VECS_DEF,
		      aq_nic_get_cfg(self)->aq_hw_caps->msix_irqs);
	numvecs = min(numvecs, num_online_cpus());
	/* Request IRQ vector for PTP */
	numvecs += 1;

	numvecs += AQ_HW_SERVICE_IRQS;
	/*enable interrupts */
#if !AQ_CFG_FORCE_LEGACY_INT
	err = pci_alloc_irq_vectors(self->pdev, 1, numvecs,
				    PCI_IRQ_MSIX | PCI_IRQ_MSI |
				    PCI_IRQ_LEGACY);

	if (err < 0)
		goto err_hwinit;
	numvecs = err;
#endif
	self->irqvecs = numvecs;

	/* net device init */
	aq_nic_cfg_start(self);

	aq_nic_ndev_init(self);

	err = aq_nic_ndev_register(self);
	if (err < 0)
		goto err_register;

	aq_drvinfo_init(ndev);

	return 0;

err_register:
	aq_nic_free_vectors(self);
	aq_pci_free_irq_vectors(self);
err_hwinit:
	iounmap(self->aq_hw->mmio);
err_free_aq_hw:
	kfree(self->aq_hw);
err_ioremap:
	free_netdev(ndev);
err_ndev:
	pci_release_regions(pdev);
err_pci_func:
	pci_disable_device(pdev);

	return err;
}

static void aq_pci_remove(struct pci_dev *pdev)
{
	struct aq_nic_s *self = pci_get_drvdata(pdev);

	if (self->ndev) {
		aq_clear_rxnfc_all_rules(self);
		if (self->ndev->reg_state == NETREG_REGISTERED)
			unregister_netdev(self->ndev);

#if IS_ENABLED(CONFIG_MACSEC)
		aq_macsec_free(self);
#endif
		aq_nic_free_vectors(self);
		aq_pci_free_irq_vectors(self);
		iounmap(self->aq_hw->mmio);
		kfree(self->aq_hw);
		pci_release_regions(pdev);
		free_netdev(self->ndev);
	}

	pci_disable_device(pdev);
}

static void aq_pci_shutdown(struct pci_dev *pdev)
{
	struct aq_nic_s *self = pci_get_drvdata(pdev);

	aq_nic_shutdown(self);

	pci_disable_device(pdev);

	if (system_state == SYSTEM_POWER_OFF) {
		pci_wake_from_d3(pdev, false);
		pci_set_power_state(pdev, PCI_D3hot);
	}
}

static int aq_suspend_common(struct device *dev, bool deep)
{
	struct aq_nic_s *nic = pci_get_drvdata(to_pci_dev(dev));

	rtnl_lock();

	nic->power_state = AQ_HW_POWER_STATE_D3;
	netif_device_detach(nic->ndev);
	netif_tx_stop_all_queues(nic->ndev);

	if (netif_running(nic->ndev))
		aq_nic_stop(nic);

	if (deep) {
		aq_nic_deinit(nic, !nic->aq_hw->aq_nic_cfg->wol);
		aq_nic_set_power(nic);
	}

	rtnl_unlock();

	return 0;
}

static int atl_resume_common(struct device *dev, bool deep)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct aq_nic_s *nic;
	int ret = 0;

	nic = pci_get_drvdata(pdev);

	rtnl_lock();

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	if (deep) {
		ret = aq_nic_init(nic);
		if (ret)
			goto err_exit;
	}

	if (netif_running(nic->ndev)) {
		ret = aq_nic_start(nic);
		if (ret)
			goto err_exit;
	}

	netif_device_attach(nic->ndev);
	netif_tx_start_all_queues(nic->ndev);

err_exit:
	rtnl_unlock();

	return ret;
}

static int aq_pm_freeze(struct device *dev)
{
	return aq_suspend_common(dev, false);
}

static int aq_pm_suspend_poweroff(struct device *dev)
{
	return aq_suspend_common(dev, true);
}

static int aq_pm_thaw(struct device *dev)
{
	return atl_resume_common(dev, false);
}

static int aq_pm_resume_restore(struct device *dev)
{
	return atl_resume_common(dev, true);
}

static const struct dev_pm_ops aq_pm_ops = {
	.suspend = aq_pm_suspend_poweroff,
	.poweroff = aq_pm_suspend_poweroff,
	.freeze = aq_pm_freeze,
	.resume = aq_pm_resume_restore,
	.restore = aq_pm_resume_restore,
	.thaw = aq_pm_thaw,
};

static struct pci_driver aq_pci_ops = {
	.name = AQ_CFG_DRV_NAME,
	.id_table = aq_pci_tbl,
	.probe = aq_pci_probe,
	.remove = aq_pci_remove,
	.shutdown = aq_pci_shutdown,
#ifdef CONFIG_PM
	.driver.pm = &aq_pm_ops,
#endif
};

int aq_pci_func_register_driver(void)
{
	return pci_register_driver(&aq_pci_ops);
}

void aq_pci_func_unregister_driver(void)
{
	pci_unregister_driver(&aq_pci_ops);
}


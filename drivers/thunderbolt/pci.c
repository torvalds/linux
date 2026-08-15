// SPDX-License-Identifier: GPL-2.0-only
/*
 * Thunderbolt driver - PCI NHI driver
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 * Copyright (C) 2018, Intel Corporation
 */

#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/property.h>
#include <linux/string_helpers.h>
#include <linux/suspend.h>

#include "nhi.h"
#include "nhi_regs.h"
#include "tb.h"

/**
 * struct tb_nhi_pci - NHI device connected over PCIe
 * @nhi: NHI device
 * @msix_ida: Used to allocate MSI-X vectors for rings
 */
struct tb_nhi_pci {
	struct tb_nhi nhi;
	struct ida msix_ida;
};

static inline struct tb_nhi_pci *nhi_to_pci(struct tb_nhi *nhi)
{
	return container_of(nhi, struct tb_nhi_pci, nhi);
}

static void nhi_pci_check_quirks(struct tb_nhi_pci *nhi_pci)
{
	struct tb_nhi *nhi = &nhi_pci->nhi;
	struct pci_dev *pdev = to_pci_dev(nhi->dev);

	if (pdev->vendor == PCI_VENDOR_ID_INTEL) {
		/*
		 * Intel hardware supports auto clear of the interrupt
		 * status register right after interrupt is being
		 * issued.
		 */
		nhi->quirks |= QUIRK_AUTO_CLEAR_INT;

		switch (pdev->device) {
		case PCI_DEVICE_ID_INTEL_FALCON_RIDGE_2C_NHI:
		case PCI_DEVICE_ID_INTEL_FALCON_RIDGE_4C_NHI:
			/*
			 * Falcon Ridge controller needs the end-to-end
			 * flow control workaround to avoid losing Rx
			 * packets when RING_FLAG_E2E is set.
			 */
			nhi->quirks |= QUIRK_E2E;
			break;
		}
	}
}

static int nhi_pci_check_iommu_pdev(struct pci_dev *pdev, void *data)
{
	if (!pdev->external_facing ||
	    !device_iommu_capable(&pdev->dev, IOMMU_CAP_PRE_BOOT_PROTECTION))
		return 0;
	*(bool *)data = true;
	return 1; /* Stop walking */
}

static void nhi_pci_check_iommu(struct tb_nhi_pci *nhi_pci)
{
	struct tb_nhi *nhi = &nhi_pci->nhi;
	struct pci_dev *pdev = to_pci_dev(nhi->dev);
	struct pci_bus *bus = pdev->bus;
	bool port_ok = false;

	/*
	 * Ideally what we'd do here is grab every PCI device that
	 * represents a tunnelling adapter for this NHI and check their
	 * status directly, but unfortunately USB4 seems to make it
	 * obnoxiously difficult to reliably make any correlation.
	 *
	 * So for now we'll have to bodge it... Hoping that the system
	 * is at least sane enough that an adapter is in the same PCI
	 * segment as its NHI, if we can find *something* on that segment
	 * which meets the requirements for Kernel DMA Protection, we'll
	 * take that to imply that firmware is aware and has (hopefully)
	 * done the right thing in general. We need to know that the PCI
	 * layer has seen the ExternalFacingPort property which will then
	 * inform the IOMMU layer to enforce the complete "untrusted DMA"
	 * flow, but also that the IOMMU driver itself can be trusted not
	 * to have been subverted by a pre-boot DMA attack.
	 */
	while (bus->parent)
		bus = bus->parent;

	pci_walk_bus(bus, nhi_pci_check_iommu_pdev, &port_ok);

	nhi->iommu_dma_protection = port_ok;
	dev_dbg(nhi->dev, "IOMMU DMA protection is %s\n",
		str_enabled_disabled(port_ok));
}

static int nhi_pci_init_msi(struct tb_nhi *nhi)
{
	struct tb_nhi_pci *nhi_pci = nhi_to_pci(nhi);
	struct pci_dev *pdev = to_pci_dev(nhi->dev);
	struct device *dev = &pdev->dev;
	int res, irq, nvec;

	ida_init(&nhi_pci->msix_ida);

	/*
	 * The NHI has 16 MSI-X vectors or a single MSI. We first try to
	 * get all MSI-X vectors and if we succeed, each ring will have
	 * one MSI-X. If for some reason that does not work out, we
	 * fallback to a single MSI.
	 */
	nvec = pci_alloc_irq_vectors(pdev, MSIX_MIN_VECS, MSIX_MAX_VECS,
				     PCI_IRQ_MSIX);
	if (nvec < 0) {
		nvec = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
		if (nvec < 0)
			return nvec;

		INIT_WORK(&nhi->interrupt_work, nhi_interrupt_work);

		irq = pci_irq_vector(pdev, 0);
		if (irq < 0)
			return irq;

		res = devm_request_irq(&pdev->dev, irq, nhi_msi,
				       IRQF_NO_SUSPEND, "thunderbolt", nhi);
		if (res)
			return dev_err_probe(dev, res, "request_irq failed, aborting\n");
	}

	return 0;
}

static bool nhi_pci_imr_valid(struct pci_dev *pdev)
{
	u8 val;

	if (!device_property_read_u8(&pdev->dev, "IMR_VALID", &val))
		return !!val;

	return true;
}

static void nhi_pci_start_dma_port(struct tb_nhi *nhi)
{
	struct pci_dev *pdev = to_pci_dev(nhi->dev);
	struct pci_dev *root_port;

	/*
	 * During host router NVM upgrade we should not allow root port to
	 * go into D3cold because some root ports cannot trigger PME
	 * itself. To be on the safe side keep the root port in D0 during
	 * the whole upgrade process.
	 */
	root_port = pcie_find_root_port(pdev);
	if (root_port)
		pm_runtime_get_noresume(&root_port->dev);
}

static void nhi_pci_complete_dma_port(struct tb_nhi *nhi)
{
	struct pci_dev *pdev = to_pci_dev(nhi->dev);
	struct pci_dev *root_port;

	root_port = pcie_find_root_port(pdev);
	if (root_port)
		pm_runtime_put(&root_port->dev);
}

static int nhi_pci_ring_request_msix(struct tb_ring *ring, bool no_suspend)
{
	struct tb_nhi *nhi = ring->nhi;
	struct tb_nhi_pci *nhi_pci = nhi_to_pci(nhi);
	struct pci_dev *pdev = to_pci_dev(nhi->dev);
	unsigned long irqflags;
	int ret;

	if (!pdev->msix_enabled)
		return 0;

	ret = ida_alloc_max(&nhi_pci->msix_ida, MSIX_MAX_VECS - 1, GFP_KERNEL);
	if (ret < 0)
		return ret;

	ring->vector = ret;

	ret = pci_irq_vector(pdev, ring->vector);
	if (ret < 0)
		goto err_ida_remove;

	ring->irq = ret;

	irqflags = no_suspend ? IRQF_NO_SUSPEND : 0;
	ret = request_irq(ring->irq, ring_msix, irqflags, "thunderbolt", ring);
	if (ret)
		goto err_ida_remove;

	return 0;

err_ida_remove:
	ida_free(&nhi_pci->msix_ida, ring->vector);

	return ret;
}

static void nhi_pci_ring_release_msix(struct tb_ring *ring)
{
	struct tb_nhi_pci *nhi_pci = nhi_to_pci(ring->nhi);

	if (ring->irq <= 0)
		return;

	free_irq(ring->irq, ring);
	ida_free(&nhi_pci->msix_ida, ring->vector);
	ring->vector = 0;
	ring->irq = 0;
}

static void nhi_pci_shutdown(struct tb_nhi *nhi)
{
	struct tb_nhi_pci *nhi_pci = nhi_to_pci(nhi);
	struct pci_dev *pdev = to_pci_dev(nhi->dev);

	/*
	 * We have to release the irq before calling flush_work. Otherwise an
	 * already executing IRQ handler could call schedule_work again.
	 */
	if (!pdev->msix_enabled) {
		devm_free_irq(nhi->dev, pdev->irq, nhi);
		flush_work(&nhi->interrupt_work);
	}
	ida_destroy(&nhi_pci->msix_ida);
}

static bool nhi_pci_is_present(struct tb_nhi *nhi)
{
	return pci_device_is_present(to_pci_dev(nhi->dev));
}

static const struct tb_nhi_ops pci_nhi_default_ops = {
	.pre_nvm_auth = nhi_pci_start_dma_port,
	.post_nvm_auth = nhi_pci_complete_dma_port,
	.request_ring_irq = nhi_pci_ring_request_msix,
	.release_ring_irq = nhi_pci_ring_release_msix,
	.shutdown = nhi_pci_shutdown,
	.is_present = nhi_pci_is_present,
	.init_interrupts = nhi_pci_init_msi,
};

/* Ice Lake specific NHI operations */

#define ICL_LC_MAILBOX_TIMEOUT	500 /* ms */

static int check_for_device(struct device *dev, void *data)
{
	return tb_is_switch(dev);
}

static bool icl_nhi_is_device_connected(struct tb_nhi *nhi)
{
	struct tb *tb = dev_get_drvdata(nhi->dev);
	int ret;

	ret = device_for_each_child(&tb->root_switch->dev, NULL,
				    check_for_device);
	return ret > 0;
}

static int icl_nhi_force_power(struct tb_nhi *nhi, bool power)
{
	struct pci_dev *pdev = to_pci_dev(nhi->dev);
	u32 vs_cap;

	/*
	 * The Thunderbolt host controller is present always in Ice Lake
	 * but the firmware may not be loaded and running (depending
	 * whether there is device connected and so on). Each time the
	 * controller is used we need to "Force Power" it first and wait
	 * for the firmware to indicate it is up and running. This "Force
	 * Power" is really not about actually powering on/off the
	 * controller so it is accessible even if "Force Power" is off.
	 *
	 * The actual power management happens inside shared ACPI power
	 * resources using standard ACPI methods.
	 */
	pci_read_config_dword(pdev, VS_CAP_22, &vs_cap);
	if (power) {
		vs_cap &= ~VS_CAP_22_DMA_DELAY_MASK;
		vs_cap |= 0x22 << VS_CAP_22_DMA_DELAY_SHIFT;
		vs_cap |= VS_CAP_22_FORCE_POWER;
	} else {
		vs_cap &= ~VS_CAP_22_FORCE_POWER;
	}
	pci_write_config_dword(pdev, VS_CAP_22, vs_cap);

	if (power) {
		unsigned int retries = 350;
		u32 val;

		/* Wait until the firmware tells it is up and running */
		do {
			pci_read_config_dword(pdev, VS_CAP_9, &val);
			if (val & VS_CAP_9_FW_READY)
				return 0;
			usleep_range(3000, 3100);
		} while (--retries);

		return -ETIMEDOUT;
	}

	return 0;
}

static void icl_nhi_lc_mailbox_cmd(struct tb_nhi *nhi, enum icl_lc_mailbox_cmd cmd)
{
	struct pci_dev *pdev = to_pci_dev(nhi->dev);
	u32 data;

	data = (cmd << VS_CAP_19_CMD_SHIFT) & VS_CAP_19_CMD_MASK;
	pci_write_config_dword(pdev, VS_CAP_19, data | VS_CAP_19_VALID);
}

static int icl_nhi_lc_mailbox_cmd_complete(struct tb_nhi *nhi, int timeout)
{
	struct pci_dev *pdev = to_pci_dev(nhi->dev);
	unsigned long end;
	u32 data;

	if (!timeout)
		goto clear;

	end = jiffies + msecs_to_jiffies(timeout);
	do {
		pci_read_config_dword(pdev, VS_CAP_18, &data);
		if (data & VS_CAP_18_DONE)
			goto clear;
		usleep_range(1000, 1100);
	} while (time_before(jiffies, end));

	return -ETIMEDOUT;

clear:
	/* Clear the valid bit */
	pci_write_config_dword(pdev, VS_CAP_19, 0);
	return 0;
}

static void icl_nhi_set_ltr(struct tb_nhi *nhi)
{
	struct pci_dev *pdev = to_pci_dev(nhi->dev);
	u32 max_ltr, ltr;

	pci_read_config_dword(pdev, VS_CAP_16, &max_ltr);
	max_ltr &= 0xffff;
	/* Program the same value for both snoop and no-snoop */
	ltr = max_ltr << 16 | max_ltr;
	pci_write_config_dword(pdev, VS_CAP_15, ltr);
}

static int icl_nhi_suspend(struct tb_nhi *nhi)
{
	struct tb *tb = dev_get_drvdata(nhi->dev);
	int ret;

	if (icl_nhi_is_device_connected(nhi))
		return 0;

	if (tb_switch_is_icm(tb->root_switch)) {
		/*
		 * If there is no device connected we need to perform
		 * both: a handshake through LC mailbox and force power
		 * down before entering D3.
		 */
		icl_nhi_lc_mailbox_cmd(nhi, ICL_LC_PREPARE_FOR_RESET);
		ret = icl_nhi_lc_mailbox_cmd_complete(nhi, ICL_LC_MAILBOX_TIMEOUT);
		if (ret)
			return ret;
	}

	return icl_nhi_force_power(nhi, false);
}

static int icl_nhi_suspend_noirq(struct tb_nhi *nhi, bool wakeup)
{
	struct tb *tb = dev_get_drvdata(nhi->dev);
	enum icl_lc_mailbox_cmd cmd;

	if (!pm_suspend_via_firmware())
		return icl_nhi_suspend(nhi);

	if (!tb_switch_is_icm(tb->root_switch))
		return 0;

	cmd = wakeup ? ICL_LC_GO2SX : ICL_LC_GO2SX_NO_WAKE;
	icl_nhi_lc_mailbox_cmd(nhi, cmd);
	return icl_nhi_lc_mailbox_cmd_complete(nhi, ICL_LC_MAILBOX_TIMEOUT);
}

static int icl_nhi_resume(struct tb_nhi *nhi)
{
	int ret;

	ret = icl_nhi_force_power(nhi, true);
	if (ret)
		return ret;

	icl_nhi_set_ltr(nhi);
	return 0;
}

static void icl_nhi_shutdown(struct tb_nhi *nhi)
{
	nhi_pci_shutdown(nhi);

	icl_nhi_force_power(nhi, false);
}

static const struct tb_nhi_ops icl_nhi_ops = {
	.init = icl_nhi_resume,
	.suspend_noirq = icl_nhi_suspend_noirq,
	.resume_noirq = icl_nhi_resume,
	.runtime_suspend = icl_nhi_suspend,
	.runtime_resume = icl_nhi_resume,
	.shutdown = icl_nhi_shutdown,
	.pre_nvm_auth = nhi_pci_start_dma_port,
	.post_nvm_auth = nhi_pci_complete_dma_port,
	.request_ring_irq = nhi_pci_ring_request_msix,
	.release_ring_irq = nhi_pci_ring_release_msix,
	.is_present = nhi_pci_is_present,
	.init_interrupts = nhi_pci_init_msi,
};

static int nhi_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct tb_nhi_pci *nhi_pci;
	struct tb_nhi *nhi;
	int res;

	if (!nhi_pci_imr_valid(pdev))
		return dev_err_probe(dev, -ENODEV, "firmware image not valid, aborting\n");

	res = pcim_enable_device(pdev);
	if (res)
		return dev_err_probe(dev, res, "cannot enable PCI device, aborting\n");

	nhi_pci = devm_kzalloc(dev, sizeof(*nhi_pci), GFP_KERNEL);
	if (!nhi_pci)
		return -ENOMEM;

	nhi = &nhi_pci->nhi;
	nhi->dev = dev;
	nhi->ops = (const struct tb_nhi_ops *)id->driver_data ?: &pci_nhi_default_ops;

	nhi->iobase = pcim_iomap_region(pdev, 0, "thunderbolt");
	res = PTR_ERR_OR_ZERO(nhi->iobase);
	if (res)
		return dev_err_probe(dev, res, "cannot obtain PCI resources, aborting\n");

	nhi_pci_check_quirks(nhi_pci);
	nhi_pci_check_iommu(nhi_pci);

	pci_set_master(pdev);

	return nhi_probe(&nhi_pci->nhi);
}

static void nhi_pci_remove(struct pci_dev *pdev)
{
	struct tb *tb = pci_get_drvdata(pdev);
	struct tb_nhi *nhi = tb->nhi;

	pm_runtime_get_sync(&pdev->dev);
	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_forbid(&pdev->dev);

	tb_domain_remove(tb);
	wait_for_completion(&nhi->domain_released);
	nhi_shutdown(nhi);
}

static struct pci_device_id nhi_ids[] = {
	/*
	 * We have to specify class, the TB bridges use the same device and
	 * vendor (sub)id on gen 1 and gen 2 controllers.
	 */
	{
		.class = PCI_CLASS_SYSTEM_OTHER << 8, .class_mask = ~0,
		.vendor = PCI_VENDOR_ID_INTEL,
		.device = PCI_DEVICE_ID_INTEL_LIGHT_RIDGE,
		.subvendor = 0x2222, .subdevice = 0x1111,
	},
	{
		.class = PCI_CLASS_SYSTEM_OTHER << 8, .class_mask = ~0,
		.vendor = PCI_VENDOR_ID_INTEL,
		.device = PCI_DEVICE_ID_INTEL_CACTUS_RIDGE_4C,
		.subvendor = 0x2222, .subdevice = 0x1111,
	},
	{
		.class = PCI_CLASS_SYSTEM_OTHER << 8, .class_mask = ~0,
		.vendor = PCI_VENDOR_ID_INTEL,
		.device = PCI_DEVICE_ID_INTEL_FALCON_RIDGE_2C_NHI,
		.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID,
	},
	{
		.class = PCI_CLASS_SYSTEM_OTHER << 8, .class_mask = ~0,
		.vendor = PCI_VENDOR_ID_INTEL,
		.device = PCI_DEVICE_ID_INTEL_FALCON_RIDGE_4C_NHI,
		.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID,
	},

	/* Thunderbolt 3 */
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_2C_NHI) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_4C_NHI) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_USBONLY_NHI) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_LP_NHI) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_LP_USBONLY_NHI) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_2C_NHI) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_4C_NHI) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_USBONLY_NHI) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_TITAN_RIDGE_2C_NHI) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_TITAN_RIDGE_4C_NHI) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ICL_NHI0),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ICL_NHI1),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	/* Thunderbolt 4 */
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_TGL_NHI0),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_TGL_NHI1),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_TGL_H_NHI0),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_TGL_H_NHI1),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ADL_NHI0),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ADL_NHI1),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_RPL_NHI0),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_RPL_NHI1),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_MTL_M_NHI0),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_MTL_P_NHI0),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_MTL_P_NHI1),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_LNL_NHI0),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_LNL_NHI1),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_PTL_M_NHI0),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_PTL_M_NHI1),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_PTL_P_NHI0),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_PTL_P_NHI1),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_WCL_NHI0),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_BARLOW_RIDGE_HOST_80G_NHI) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_BARLOW_RIDGE_HOST_40G_NHI) },

	/* Any USB4 compliant host */
	{ PCI_DEVICE_CLASS(PCI_CLASS_SERIAL_USB_USB4, ~0) },

	{ 0,}
};

MODULE_DEVICE_TABLE(pci, nhi_ids);
MODULE_DESCRIPTION("Thunderbolt/USB4 core driver");
MODULE_LICENSE("GPL");

static struct pci_driver nhi_driver = {
	.name = "thunderbolt",
	.id_table = nhi_ids,
	.probe = nhi_pci_probe,
	.remove = nhi_pci_remove,
	.shutdown = nhi_pci_remove,
	.driver.pm = &nhi_pm_ops,
};

static int __init nhi_init(void)
{
	int ret;

	ret = tb_domain_init();
	if (ret)
		return ret;

	ret = pci_register_driver(&nhi_driver);
	if (ret)
		tb_domain_exit();

	return ret;
}

static void __exit nhi_unload(void)
{
	pci_unregister_driver(&nhi_driver);
	tb_domain_exit();
}

rootfs_initcall(nhi_init);
module_exit(nhi_unload);

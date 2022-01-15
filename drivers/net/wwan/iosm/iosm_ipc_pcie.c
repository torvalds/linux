// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-21 Intel Corporation.
 */

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/module.h>
#include <net/rtnetlink.h>

#include "iosm_ipc_imem.h"
#include "iosm_ipc_pcie.h"
#include "iosm_ipc_protocol.h"

MODULE_DESCRIPTION("IOSM Driver");
MODULE_LICENSE("GPL v2");

/* WWAN GUID */
static guid_t wwan_acpi_guid = GUID_INIT(0xbad01b75, 0x22a8, 0x4f48, 0x87, 0x92,
				       0xbd, 0xde, 0x94, 0x67, 0x74, 0x7d);

static void ipc_pcie_resources_release(struct iosm_pcie *ipc_pcie)
{
	/* Free the MSI resources. */
	ipc_release_irq(ipc_pcie);

	/* Free mapped doorbell scratchpad bus memory into CPU space. */
	iounmap(ipc_pcie->scratchpad);

	/* Free mapped IPC_REGS bus memory into CPU space. */
	iounmap(ipc_pcie->ipc_regs);

	/* Releases all PCI I/O and memory resources previously reserved by a
	 * successful call to pci_request_regions.  Call this function only
	 * after all use of the PCI regions has ceased.
	 */
	pci_release_regions(ipc_pcie->pci);
}

static void ipc_pcie_cleanup(struct iosm_pcie *ipc_pcie)
{
	/* Free the shared memory resources. */
	ipc_imem_cleanup(ipc_pcie->imem);

	ipc_pcie_resources_release(ipc_pcie);

	/* Signal to the system that the PCI device is not in use. */
	pci_disable_device(ipc_pcie->pci);
}

static void ipc_pcie_deinit(struct iosm_pcie *ipc_pcie)
{
	kfree(ipc_pcie->imem);
	kfree(ipc_pcie);
}

static void ipc_pcie_remove(struct pci_dev *pci)
{
	struct iosm_pcie *ipc_pcie = pci_get_drvdata(pci);

	ipc_pcie_cleanup(ipc_pcie);

	ipc_pcie_deinit(ipc_pcie);
}

static int ipc_pcie_resources_request(struct iosm_pcie *ipc_pcie)
{
	struct pci_dev *pci = ipc_pcie->pci;
	u32 cap = 0;
	u32 ret;

	/* Reserved PCI I/O and memory resources.
	 * Mark all PCI regions associated with PCI device pci as
	 * being reserved by owner IOSM_IPC.
	 */
	ret = pci_request_regions(pci, "IOSM_IPC");
	if (ret) {
		dev_err(ipc_pcie->dev, "failed pci request regions");
		goto pci_request_region_fail;
	}

	/* Reserve the doorbell IPC REGS memory resources.
	 * Remap the memory into CPU space. Arrange for the physical address
	 * (BAR) to be visible from this driver.
	 * pci_ioremap_bar() ensures that the memory is marked uncachable.
	 */
	ipc_pcie->ipc_regs = pci_ioremap_bar(pci, ipc_pcie->ipc_regs_bar_nr);

	if (!ipc_pcie->ipc_regs) {
		dev_err(ipc_pcie->dev, "IPC REGS ioremap error");
		ret = -EBUSY;
		goto ipc_regs_remap_fail;
	}

	/* Reserve the MMIO scratchpad memory resources.
	 * Remap the memory into CPU space. Arrange for the physical address
	 * (BAR) to be visible from this driver.
	 * pci_ioremap_bar() ensures that the memory is marked uncachable.
	 */
	ipc_pcie->scratchpad =
		pci_ioremap_bar(pci, ipc_pcie->scratchpad_bar_nr);

	if (!ipc_pcie->scratchpad) {
		dev_err(ipc_pcie->dev, "doorbell scratchpad ioremap error");
		ret = -EBUSY;
		goto scratch_remap_fail;
	}

	/* Install the irq handler triggered by CP. */
	ret = ipc_acquire_irq(ipc_pcie);
	if (ret) {
		dev_err(ipc_pcie->dev, "acquiring MSI irq failed!");
		goto irq_acquire_fail;
	}

	/* Enable bus-mastering for the IOSM IPC device. */
	pci_set_master(pci);

	/* Enable LTR if possible
	 * This is needed for L1.2!
	 */
	pcie_capability_read_dword(ipc_pcie->pci, PCI_EXP_DEVCAP2, &cap);
	if (cap & PCI_EXP_DEVCAP2_LTR)
		pcie_capability_set_word(ipc_pcie->pci, PCI_EXP_DEVCTL2,
					 PCI_EXP_DEVCTL2_LTR_EN);

	dev_dbg(ipc_pcie->dev, "link between AP and CP is fully on");

	return ret;

irq_acquire_fail:
	iounmap(ipc_pcie->scratchpad);
scratch_remap_fail:
	iounmap(ipc_pcie->ipc_regs);
ipc_regs_remap_fail:
	pci_release_regions(pci);
pci_request_region_fail:
	return ret;
}

bool ipc_pcie_check_aspm_enabled(struct iosm_pcie *ipc_pcie,
				 bool parent)
{
	struct pci_dev *pdev;
	u16 value = 0;
	u32 enabled;

	if (parent)
		pdev = ipc_pcie->pci->bus->self;
	else
		pdev = ipc_pcie->pci;

	pcie_capability_read_word(pdev, PCI_EXP_LNKCTL, &value);
	enabled = value & PCI_EXP_LNKCTL_ASPMC;
	dev_dbg(ipc_pcie->dev, "ASPM L1: 0x%04X 0x%03X", pdev->device, value);

	return (enabled == PCI_EXP_LNKCTL_ASPM_L1 ||
		enabled == PCI_EXP_LNKCTL_ASPMC);
}

bool ipc_pcie_check_data_link_active(struct iosm_pcie *ipc_pcie)
{
	struct pci_dev *parent;
	u16 link_status = 0;

	if (!ipc_pcie->pci->bus || !ipc_pcie->pci->bus->self) {
		dev_err(ipc_pcie->dev, "root port not found");
		return false;
	}

	parent = ipc_pcie->pci->bus->self;

	pcie_capability_read_word(parent, PCI_EXP_LNKSTA, &link_status);
	dev_dbg(ipc_pcie->dev, "Link status: 0x%04X", link_status);

	return link_status & PCI_EXP_LNKSTA_DLLLA;
}

static bool ipc_pcie_check_aspm_supported(struct iosm_pcie *ipc_pcie,
					  bool parent)
{
	struct pci_dev *pdev;
	u32 support;
	u32 cap = 0;

	if (parent)
		pdev = ipc_pcie->pci->bus->self;
	else
		pdev = ipc_pcie->pci;
	pcie_capability_read_dword(pdev, PCI_EXP_LNKCAP, &cap);
	support = u32_get_bits(cap, PCI_EXP_LNKCAP_ASPMS);
	if (support < PCI_EXP_LNKCTL_ASPM_L1) {
		dev_dbg(ipc_pcie->dev, "ASPM L1 not supported: 0x%04X",
			pdev->device);
		return false;
	}
	return true;
}

void ipc_pcie_config_aspm(struct iosm_pcie *ipc_pcie)
{
	bool parent_aspm_enabled, dev_aspm_enabled;

	/* check if both root port and child supports ASPM L1 */
	if (!ipc_pcie_check_aspm_supported(ipc_pcie, true) ||
	    !ipc_pcie_check_aspm_supported(ipc_pcie, false))
		return;

	parent_aspm_enabled = ipc_pcie_check_aspm_enabled(ipc_pcie, true);
	dev_aspm_enabled = ipc_pcie_check_aspm_enabled(ipc_pcie, false);

	dev_dbg(ipc_pcie->dev, "ASPM parent: %s device: %s",
		parent_aspm_enabled ? "Enabled" : "Disabled",
		dev_aspm_enabled ? "Enabled" : "Disabled");
}

/* Initializes PCIe endpoint configuration */
static void ipc_pcie_config_init(struct iosm_pcie *ipc_pcie)
{
	/* BAR0 is used for doorbell */
	ipc_pcie->ipc_regs_bar_nr = IPC_DOORBELL_BAR0;

	/* update HW configuration */
	ipc_pcie->scratchpad_bar_nr = IPC_SCRATCHPAD_BAR2;
	ipc_pcie->doorbell_reg_offset = IPC_DOORBELL_CH_OFFSET;
	ipc_pcie->doorbell_write = IPC_WRITE_PTR_REG_0;
	ipc_pcie->doorbell_capture = IPC_CAPTURE_PTR_REG_0;
}

/* This will read the BIOS WWAN RTD3 settings:
 * D0L1.2/D3L2/Disabled
 */
static enum ipc_pcie_sleep_state ipc_pcie_read_bios_cfg(struct device *dev)
{
	union acpi_object *object;
	acpi_handle handle_acpi;

	handle_acpi = ACPI_HANDLE(dev);
	if (!handle_acpi) {
		pr_debug("pci device is NOT ACPI supporting device\n");
		goto default_ret;
	}

	object = acpi_evaluate_dsm(handle_acpi, &wwan_acpi_guid, 0, 3, NULL);

	if (object && object->integer.value == 3)
		return IPC_PCIE_D3L2;

default_ret:
	return IPC_PCIE_D0L12;
}

static int ipc_pcie_probe(struct pci_dev *pci,
			  const struct pci_device_id *pci_id)
{
	struct iosm_pcie *ipc_pcie = kzalloc(sizeof(*ipc_pcie), GFP_KERNEL);

	pr_debug("Probing device 0x%X from the vendor 0x%X", pci_id->device,
		 pci_id->vendor);

	if (!ipc_pcie)
		goto ret_fail;

	/* Initialize ipc dbg component for the PCIe device */
	ipc_pcie->dev = &pci->dev;

	/* Set the driver specific data. */
	pci_set_drvdata(pci, ipc_pcie);

	/* Save the address of the PCI device configuration. */
	ipc_pcie->pci = pci;

	/* Update platform configuration */
	ipc_pcie_config_init(ipc_pcie);

	/* Initialize the device before it is used. Ask low-level code
	 * to enable I/O and memory. Wake up the device if it was suspended.
	 */
	if (pci_enable_device(pci)) {
		dev_err(ipc_pcie->dev, "failed to enable the AP PCIe device");
		/* If enable of PCIe device has failed then calling
		 * ipc_pcie_cleanup will panic the system. More over
		 * ipc_pcie_cleanup() is required to be called after
		 * ipc_imem_mount()
		 */
		goto pci_enable_fail;
	}

	ipc_pcie_config_aspm(ipc_pcie);
	dev_dbg(ipc_pcie->dev, "PCIe device enabled.");

	/* Read WWAN RTD3 BIOS Setting
	 */
	ipc_pcie->d3l2_support = ipc_pcie_read_bios_cfg(&pci->dev);

	ipc_pcie->suspend = 0;

	if (ipc_pcie_resources_request(ipc_pcie))
		goto resources_req_fail;

	/* Establish the link to the imem layer. */
	ipc_pcie->imem = ipc_imem_init(ipc_pcie, pci->device,
				       ipc_pcie->scratchpad, ipc_pcie->dev);
	if (!ipc_pcie->imem) {
		dev_err(ipc_pcie->dev, "failed to init imem");
		goto imem_init_fail;
	}

	return 0;

imem_init_fail:
	ipc_pcie_resources_release(ipc_pcie);
resources_req_fail:
	pci_disable_device(pci);
pci_enable_fail:
	kfree(ipc_pcie);
ret_fail:
	return -EIO;
}

static const struct pci_device_id iosm_ipc_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, INTEL_CP_DEVICE_7560_ID) },
	{}
};
MODULE_DEVICE_TABLE(pci, iosm_ipc_ids);

/* Enter sleep in s2idle case
 */
static int __maybe_unused ipc_pcie_suspend_s2idle(struct iosm_pcie *ipc_pcie)
{
	ipc_cp_irq_sleep_control(ipc_pcie, IPC_MEM_DEV_PM_FORCE_SLEEP);

	/* Complete all memory stores before setting bit */
	smp_mb__before_atomic();

	set_bit(0, &ipc_pcie->suspend);

	/* Complete all memory stores after setting bit */
	smp_mb__after_atomic();

	ipc_imem_pm_s2idle_sleep(ipc_pcie->imem, true);

	return 0;
}

/* Resume from sleep in s2idle case
 */
static int __maybe_unused ipc_pcie_resume_s2idle(struct iosm_pcie *ipc_pcie)
{
	ipc_cp_irq_sleep_control(ipc_pcie, IPC_MEM_DEV_PM_FORCE_ACTIVE);

	ipc_imem_pm_s2idle_sleep(ipc_pcie->imem, false);

	/* Complete all memory stores before clearing bit. */
	smp_mb__before_atomic();

	clear_bit(0, &ipc_pcie->suspend);

	/* Complete all memory stores after clearing bit. */
	smp_mb__after_atomic();
	return 0;
}

int __maybe_unused ipc_pcie_suspend(struct iosm_pcie *ipc_pcie)
{
	struct pci_dev *pdev;
	int ret;

	pdev = ipc_pcie->pci;

	/* Execute D3 one time. */
	if (pdev->current_state != PCI_D0) {
		dev_dbg(ipc_pcie->dev, "done for PM=%d", pdev->current_state);
		return 0;
	}

	/* The HAL shall ask the shared memory layer whether D3 is allowed. */
	ipc_imem_pm_suspend(ipc_pcie->imem);

	/* Save the PCI configuration space of a device before suspending. */
	ret = pci_save_state(pdev);

	if (ret) {
		dev_err(ipc_pcie->dev, "pci_save_state error=%d", ret);
		return ret;
	}

	/* Set the power state of a PCI device.
	 * Transition a device to a new power state, using the device's PCI PM
	 * registers.
	 */
	ret = pci_set_power_state(pdev, PCI_D3cold);

	if (ret) {
		dev_err(ipc_pcie->dev, "pci_set_power_state error=%d", ret);
		return ret;
	}

	dev_dbg(ipc_pcie->dev, "SUSPEND done");
	return ret;
}

int __maybe_unused ipc_pcie_resume(struct iosm_pcie *ipc_pcie)
{
	int ret;

	/* Set the power state of a PCI device.
	 * Transition a device to a new power state, using the device's PCI PM
	 * registers.
	 */
	ret = pci_set_power_state(ipc_pcie->pci, PCI_D0);

	if (ret) {
		dev_err(ipc_pcie->dev, "pci_set_power_state error=%d", ret);
		return ret;
	}

	pci_restore_state(ipc_pcie->pci);

	/* The HAL shall inform the shared memory layer that the device is
	 * active.
	 */
	ipc_imem_pm_resume(ipc_pcie->imem);

	dev_dbg(ipc_pcie->dev, "RESUME done");
	return ret;
}

static int __maybe_unused ipc_pcie_suspend_cb(struct device *dev)
{
	struct iosm_pcie *ipc_pcie;
	struct pci_dev *pdev;

	pdev = to_pci_dev(dev);

	ipc_pcie = pci_get_drvdata(pdev);

	switch (ipc_pcie->d3l2_support) {
	case IPC_PCIE_D0L12:
		ipc_pcie_suspend_s2idle(ipc_pcie);
		break;
	case IPC_PCIE_D3L2:
		ipc_pcie_suspend(ipc_pcie);
		break;
	}

	return 0;
}

static int __maybe_unused ipc_pcie_resume_cb(struct device *dev)
{
	struct iosm_pcie *ipc_pcie;
	struct pci_dev *pdev;

	pdev = to_pci_dev(dev);

	ipc_pcie = pci_get_drvdata(pdev);

	switch (ipc_pcie->d3l2_support) {
	case IPC_PCIE_D0L12:
		ipc_pcie_resume_s2idle(ipc_pcie);
		break;
	case IPC_PCIE_D3L2:
		ipc_pcie_resume(ipc_pcie);
		break;
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(iosm_ipc_pm, ipc_pcie_suspend_cb, ipc_pcie_resume_cb);

static struct pci_driver iosm_ipc_driver = {
	.name = KBUILD_MODNAME,
	.probe = ipc_pcie_probe,
	.remove = ipc_pcie_remove,
	.driver = {
		.pm = &iosm_ipc_pm,
	},
	.id_table = iosm_ipc_ids,
};
module_pci_driver(iosm_ipc_driver);

int ipc_pcie_addr_map(struct iosm_pcie *ipc_pcie, unsigned char *data,
		      size_t size, dma_addr_t *mapping, int direction)
{
	if (ipc_pcie->pci) {
		*mapping = dma_map_single(&ipc_pcie->pci->dev, data, size,
					  direction);
		if (dma_mapping_error(&ipc_pcie->pci->dev, *mapping)) {
			dev_err(ipc_pcie->dev, "dma mapping failed");
			return -EINVAL;
		}
	}
	return 0;
}

void ipc_pcie_addr_unmap(struct iosm_pcie *ipc_pcie, size_t size,
			 dma_addr_t mapping, int direction)
{
	if (!mapping)
		return;
	if (ipc_pcie->pci)
		dma_unmap_single(&ipc_pcie->pci->dev, mapping, size, direction);
}

struct sk_buff *ipc_pcie_alloc_local_skb(struct iosm_pcie *ipc_pcie,
					 gfp_t flags, size_t size)
{
	struct sk_buff *skb;

	if (!ipc_pcie || !size) {
		pr_err("invalid pcie object or size");
		return NULL;
	}

	skb = __netdev_alloc_skb(NULL, size, flags);
	if (!skb)
		return NULL;

	IPC_CB(skb)->op_type = (u8)UL_DEFAULT;
	IPC_CB(skb)->mapping = 0;

	return skb;
}

struct sk_buff *ipc_pcie_alloc_skb(struct iosm_pcie *ipc_pcie, size_t size,
				   gfp_t flags, dma_addr_t *mapping,
				   int direction, size_t headroom)
{
	struct sk_buff *skb = ipc_pcie_alloc_local_skb(ipc_pcie, flags,
						       size + headroom);
	if (!skb)
		return NULL;

	if (headroom)
		skb_reserve(skb, headroom);

	if (ipc_pcie_addr_map(ipc_pcie, skb->data, size, mapping, direction)) {
		dev_kfree_skb(skb);
		return NULL;
	}

	BUILD_BUG_ON(sizeof(*IPC_CB(skb)) > sizeof(skb->cb));

	/* Store the mapping address in skb scratch pad for later usage */
	IPC_CB(skb)->mapping = *mapping;
	IPC_CB(skb)->direction = direction;
	IPC_CB(skb)->len = size;

	return skb;
}

void ipc_pcie_kfree_skb(struct iosm_pcie *ipc_pcie, struct sk_buff *skb)
{
	if (!skb)
		return;

	ipc_pcie_addr_unmap(ipc_pcie, IPC_CB(skb)->len, IPC_CB(skb)->mapping,
			    IPC_CB(skb)->direction);
	IPC_CB(skb)->mapping = 0;
	dev_kfree_skb(skb);
}

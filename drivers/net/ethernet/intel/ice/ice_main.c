// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

/* Intel(R) Ethernet Connection E800 Series Linux Driver */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "ice.h"

#define DRV_VERSION	"ice-0.0.1-k"
#define DRV_SUMMARY	"Intel(R) Ethernet Connection E800 Series Linux Driver"
static const char ice_drv_ver[] = DRV_VERSION;
static const char ice_driver_string[] = DRV_SUMMARY;
static const char ice_copyright[] = "Copyright (c) 2018, Intel Corporation.";

MODULE_AUTHOR("Intel Corporation, <linux.nics@intel.com>");
MODULE_DESCRIPTION(DRV_SUMMARY);
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

static int debug = -1;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "netif message level (0=none,...,0x7FFF=all)");

/**
 * ice_probe - Device initialization routine
 * @pdev: PCI device information struct
 * @ent: entry in ice_pci_tbl
 *
 * Returns 0 on success, negative on failure
 */
static int ice_probe(struct pci_dev *pdev,
		     const struct pci_device_id __always_unused *ent)
{
	struct ice_pf *pf;
	struct ice_hw *hw;
	int err;

	/* this driver uses devres, see Documentation/driver-model/devres.txt */
	err = pcim_enable_device(pdev);
	if (err)
		return err;

	err = pcim_iomap_regions(pdev, BIT(ICE_BAR0), pci_name(pdev));
	if (err) {
		dev_err(&pdev->dev, "I/O map error %d\n", err);
		return err;
	}

	pf = devm_kzalloc(&pdev->dev, sizeof(*pf), GFP_KERNEL);
	if (!pf)
		return -ENOMEM;

	/* set up for high or low dma */
	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err)
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (err) {
		dev_err(&pdev->dev, "DMA configuration failed: 0x%x\n", err);
		return err;
	}

	pci_enable_pcie_error_reporting(pdev);
	pci_set_master(pdev);

	pf->pdev = pdev;
	pci_set_drvdata(pdev, pf);
	set_bit(__ICE_DOWN, pf->state);

	hw = &pf->hw;
	hw->hw_addr = pcim_iomap_table(pdev)[ICE_BAR0];
	hw->back = pf;
	hw->vendor_id = pdev->vendor;
	hw->device_id = pdev->device;
	pci_read_config_byte(pdev, PCI_REVISION_ID, &hw->revision_id);
	hw->subsystem_vendor_id = pdev->subsystem_vendor;
	hw->subsystem_device_id = pdev->subsystem_device;
	hw->bus.device = PCI_SLOT(pdev->devfn);
	hw->bus.func = PCI_FUNC(pdev->devfn);
	pf->msg_enable = netif_msg_init(debug, ICE_DFLT_NETIF_M);

	return 0;
}

/**
 * ice_remove - Device removal routine
 * @pdev: PCI device information struct
 */
static void ice_remove(struct pci_dev *pdev)
{
	struct ice_pf *pf = pci_get_drvdata(pdev);

	if (!pf)
		return;

	set_bit(__ICE_DOWN, pf->state);
	pci_disable_pcie_error_reporting(pdev);
}

/* ice_pci_tbl - PCI Device ID Table
 *
 * Wildcard entries (PCI_ANY_ID) should come last
 * Last entry must be all 0s
 *
 * { Vendor ID, Device ID, SubVendor ID, SubDevice ID,
 *   Class, Class Mask, private data (not used) }
 */
static const struct pci_device_id ice_pci_tbl[] = {
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_C810_BACKPLANE), 0 },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_C810_QSFP), 0 },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_C810_SFP), 0 },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_C810_10G_BASE_T), 0 },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_C810_SGMII), 0 },
	/* required last entry */
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, ice_pci_tbl);

static struct pci_driver ice_driver = {
	.name = KBUILD_MODNAME,
	.id_table = ice_pci_tbl,
	.probe = ice_probe,
	.remove = ice_remove,
};

/**
 * ice_module_init - Driver registration routine
 *
 * ice_module_init is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 */
static int __init ice_module_init(void)
{
	int status;

	pr_info("%s - version %s\n", ice_driver_string, ice_drv_ver);
	pr_info("%s\n", ice_copyright);

	status = pci_register_driver(&ice_driver);
	if (status)
		pr_err("failed to register pci driver, err %d\n", status);

	return status;
}
module_init(ice_module_init);

/**
 * ice_module_exit - Driver exit cleanup routine
 *
 * ice_module_exit is called just before the driver is removed
 * from memory.
 */
static void __exit ice_module_exit(void)
{
	pci_unregister_driver(&ice_driver);
	pr_info("module unloaded\n");
}
module_exit(ice_module_exit);

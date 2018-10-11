// SPDX-License-Identifier: GPL-2.0
/* Copyright (c)  2018 Intel Corporation */

#include <linux/module.h>
#include <linux/types.h>

#include "igc.h"
#include "igc_hw.h"

#define DRV_VERSION	"0.0.1-k"
#define DRV_SUMMARY	"Intel(R) 2.5G Ethernet Linux Driver"

MODULE_AUTHOR("Intel Corporation, <linux.nics@intel.com>");
MODULE_DESCRIPTION(DRV_SUMMARY);
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);

char igc_driver_name[] = "igc";
char igc_driver_version[] = DRV_VERSION;
static const char igc_driver_string[] = DRV_SUMMARY;
static const char igc_copyright[] =
	"Copyright(c) 2018 Intel Corporation.";

static const struct pci_device_id igc_pci_tbl[] = {
	{ PCI_VDEVICE(INTEL, IGC_DEV_ID_I225_LM) },
	{ PCI_VDEVICE(INTEL, IGC_DEV_ID_I225_V) },
	/* required last entry */
	{0, }
};

MODULE_DEVICE_TABLE(pci, igc_pci_tbl);

/* forward declaration */
static int igc_sw_init(struct igc_adapter *);

/* PCIe configuration access */
void igc_read_pci_cfg(struct igc_hw *hw, u32 reg, u16 *value)
{
	struct igc_adapter *adapter = hw->back;

	pci_read_config_word(adapter->pdev, reg, value);
}

void igc_write_pci_cfg(struct igc_hw *hw, u32 reg, u16 *value)
{
	struct igc_adapter *adapter = hw->back;

	pci_write_config_word(adapter->pdev, reg, *value);
}

s32 igc_read_pcie_cap_reg(struct igc_hw *hw, u32 reg, u16 *value)
{
	struct igc_adapter *adapter = hw->back;
	u16 cap_offset;

	cap_offset = pci_find_capability(adapter->pdev, PCI_CAP_ID_EXP);
	if (!cap_offset)
		return -IGC_ERR_CONFIG;

	pci_read_config_word(adapter->pdev, cap_offset + reg, value);

	return IGC_SUCCESS;
}

s32 igc_write_pcie_cap_reg(struct igc_hw *hw, u32 reg, u16 *value)
{
	struct igc_adapter *adapter = hw->back;
	u16 cap_offset;

	cap_offset = pci_find_capability(adapter->pdev, PCI_CAP_ID_EXP);
	if (!cap_offset)
		return -IGC_ERR_CONFIG;

	pci_write_config_word(adapter->pdev, cap_offset + reg, *value);

	return IGC_SUCCESS;
}

u32 igc_rd32(struct igc_hw *hw, u32 reg)
{
	u8 __iomem *hw_addr = READ_ONCE(hw->hw_addr);
	u32 value = 0;

	if (IGC_REMOVED(hw_addr))
		return ~value;

	value = readl(&hw_addr[reg]);

	/* reads should not return all F's */
	if (!(~value) && (!reg || !(~readl(hw_addr))))
		hw->hw_addr = NULL;

	return value;
}

/**
 * igc_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in igc_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * igc_probe initializes an adapter identified by a pci_dev structure.
 * The OS initialization, configuring the adapter private structure,
 * and a hardware reset occur.
 */
static int igc_probe(struct pci_dev *pdev,
		     const struct pci_device_id *ent)
{
	struct igc_adapter *adapter;
	int err, pci_using_dac;

	err = pci_enable_device_mem(pdev);
	if (err)
		return err;

	pci_using_dac = 0;
	err = dma_set_mask(&pdev->dev, DMA_BIT_MASK(64));
	if (!err) {
		err = dma_set_coherent_mask(&pdev->dev,
					    DMA_BIT_MASK(64));
		if (!err)
			pci_using_dac = 1;
	} else {
		err = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
		if (err) {
			err = dma_set_coherent_mask(&pdev->dev,
						    DMA_BIT_MASK(32));
			if (err) {
				IGC_ERR("Wrong DMA configuration, aborting\n");
				goto err_dma;
			}
		}
	}

	err = pci_request_selected_regions(pdev,
					   pci_select_bars(pdev,
							   IORESOURCE_MEM),
					   igc_driver_name);
	if (err)
		goto err_pci_reg;

	pci_set_master(pdev);
	err = pci_save_state(pdev);

	/* setup the private structure */
	err = igc_sw_init(adapter);
	if (err)
		goto err_sw_init;

	return 0;

err_sw_init:
err_pci_reg:
err_dma:
	pci_disable_device(pdev);
	return err;
}

/**
 * igc_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * igc_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  This could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 */
static void igc_remove(struct pci_dev *pdev)
{
	pci_release_selected_regions(pdev,
				     pci_select_bars(pdev, IORESOURCE_MEM));

	pci_disable_device(pdev);
}

static struct pci_driver igc_driver = {
	.name     = igc_driver_name,
	.id_table = igc_pci_tbl,
	.probe    = igc_probe,
	.remove   = igc_remove,
};

/**
 * igc_sw_init - Initialize general software structures (struct igc_adapter)
 * @adapter: board private structure to initialize
 *
 * igc_sw_init initializes the Adapter private data structure.
 * Fields are initialized based on PCI device information and
 * OS network device settings (MTU size).
 */
static int igc_sw_init(struct igc_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	struct igc_hw *hw = &adapter->hw;

	/* PCI config space info */

	hw->vendor_id = pdev->vendor;
	hw->device_id = pdev->device;
	hw->subsystem_vendor_id = pdev->subsystem_vendor;
	hw->subsystem_device_id = pdev->subsystem_device;

	pci_read_config_byte(pdev, PCI_REVISION_ID, &hw->revision_id);

	pci_read_config_word(pdev, PCI_COMMAND, &hw->bus.pci_cmd_word);

	return 0;
}

/**
 * igc_init_module - Driver Registration Routine
 *
 * igc_init_module is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 */
static int __init igc_init_module(void)
{
	int ret;

	pr_info("%s - version %s\n",
		igc_driver_string, igc_driver_version);

	pr_info("%s\n", igc_copyright);

	ret = pci_register_driver(&igc_driver);
	return ret;
}

module_init(igc_init_module);

/**
 * igc_exit_module - Driver Exit Cleanup Routine
 *
 * igc_exit_module is called just before the driver is removed
 * from memory.
 */
static void __exit igc_exit_module(void)
{
	pci_unregister_driver(&igc_driver);
}

module_exit(igc_exit_module);
/* igc_main.c */

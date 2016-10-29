/*
 * AMD 76x Memory Controller kernel module
 * (C) 2003 Linux Networx (http://lnxi.com)
 * This file may be distributed under the terms of the
 * GNU General Public License.
 *
 * Written by Thayne Harbaugh
 * Based on work by Dan Hollis <goemon at anime dot net> and others.
 *	http://www.anime.net/~goemon/linux-ecc/
 *
 * $Id: edac_amd76x.c,v 1.4.2.5 2005/10/05 00:43:44 dsp_llnl Exp $
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/edac.h>
#include "edac_module.h"

#define AMD76X_REVISION	" Ver: 2.0.2"
#define EDAC_MOD_STR	"amd76x_edac"

#define amd76x_printk(level, fmt, arg...) \
	edac_printk(level, "amd76x", fmt, ##arg)

#define amd76x_mc_printk(mci, level, fmt, arg...) \
	edac_mc_chipset_printk(mci, level, "amd76x", fmt, ##arg)

#define AMD76X_NR_CSROWS 8
#define AMD76X_NR_DIMMS  4

/* AMD 76x register addresses - device 0 function 0 - PCI bridge */

#define AMD76X_ECC_MODE_STATUS	0x48	/* Mode and status of ECC (32b)
					 *
					 * 31:16 reserved
					 * 15:14 SERR enabled: x1=ue 1x=ce
					 * 13    reserved
					 * 12    diag: disabled, enabled
					 * 11:10 mode: dis, EC, ECC, ECC+scrub
					 *  9:8  status: x1=ue 1x=ce
					 *  7:4  UE cs row
					 *  3:0  CE cs row
					 */

#define AMD76X_DRAM_MODE_STATUS	0x58	/* DRAM Mode and status (32b)
					 *
					 * 31:26 clock disable 5 - 0
					 * 25    SDRAM init
					 * 24    reserved
					 * 23    mode register service
					 * 22:21 suspend to RAM
					 * 20    burst refresh enable
					 * 19    refresh disable
					 * 18    reserved
					 * 17:16 cycles-per-refresh
					 * 15:8  reserved
					 *  7:0  x4 mode enable 7 - 0
					 */

#define AMD76X_MEM_BASE_ADDR	0xC0	/* Memory base address (8 x 32b)
					 *
					 * 31:23 chip-select base
					 * 22:16 reserved
					 * 15:7  chip-select mask
					 *  6:3  reserved
					 *  2:1  address mode
					 *  0    chip-select enable
					 */

struct amd76x_error_info {
	u32 ecc_mode_status;
};

enum amd76x_chips {
	AMD761 = 0,
	AMD762
};

struct amd76x_dev_info {
	const char *ctl_name;
};

static const struct amd76x_dev_info amd76x_devs[] = {
	[AMD761] = {
		.ctl_name = "AMD761"},
	[AMD762] = {
		.ctl_name = "AMD762"},
};

static struct edac_pci_ctl_info *amd76x_pci;

/**
 *	amd76x_get_error_info	-	fetch error information
 *	@mci: Memory controller
 *	@info: Info to fill in
 *
 *	Fetch and store the AMD76x ECC status. Clear pending status
 *	on the chip so that further errors will be reported
 */
static void amd76x_get_error_info(struct mem_ctl_info *mci,
				struct amd76x_error_info *info)
{
	struct pci_dev *pdev;

	pdev = to_pci_dev(mci->pdev);
	pci_read_config_dword(pdev, AMD76X_ECC_MODE_STATUS,
			&info->ecc_mode_status);

	if (info->ecc_mode_status & BIT(8))
		pci_write_bits32(pdev, AMD76X_ECC_MODE_STATUS,
				 (u32) BIT(8), (u32) BIT(8));

	if (info->ecc_mode_status & BIT(9))
		pci_write_bits32(pdev, AMD76X_ECC_MODE_STATUS,
				 (u32) BIT(9), (u32) BIT(9));
}

/**
 *	amd76x_process_error_info	-	Error check
 *	@mci: Memory controller
 *	@info: Previously fetched information from chip
 *	@handle_errors: 1 if we should do recovery
 *
 *	Process the chip state and decide if an error has occurred.
 *	A return of 1 indicates an error. Also if handle_errors is true
 *	then attempt to handle and clean up after the error
 */
static int amd76x_process_error_info(struct mem_ctl_info *mci,
				struct amd76x_error_info *info,
				int handle_errors)
{
	int error_found;
	u32 row;

	error_found = 0;

	/*
	 *      Check for an uncorrectable error
	 */
	if (info->ecc_mode_status & BIT(8)) {
		error_found = 1;

		if (handle_errors) {
			row = (info->ecc_mode_status >> 4) & 0xf;
			edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci, 1,
					     mci->csrows[row]->first_page, 0, 0,
					     row, 0, -1,
					     mci->ctl_name, "");
		}
	}

	/*
	 *      Check for a correctable error
	 */
	if (info->ecc_mode_status & BIT(9)) {
		error_found = 1;

		if (handle_errors) {
			row = info->ecc_mode_status & 0xf;
			edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci, 1,
					     mci->csrows[row]->first_page, 0, 0,
					     row, 0, -1,
					     mci->ctl_name, "");
		}
	}

	return error_found;
}

/**
 *	amd76x_check	-	Poll the controller
 *	@mci: Memory controller
 *
 *	Called by the poll handlers this function reads the status
 *	from the controller and checks for errors.
 */
static void amd76x_check(struct mem_ctl_info *mci)
{
	struct amd76x_error_info info;
	edac_dbg(3, "\n");
	amd76x_get_error_info(mci, &info);
	amd76x_process_error_info(mci, &info, 1);
}

static void amd76x_init_csrows(struct mem_ctl_info *mci, struct pci_dev *pdev,
			enum edac_type edac_mode)
{
	struct csrow_info *csrow;
	struct dimm_info *dimm;
	u32 mba, mba_base, mba_mask, dms;
	int index;

	for (index = 0; index < mci->nr_csrows; index++) {
		csrow = mci->csrows[index];
		dimm = csrow->channels[0]->dimm;

		/* find the DRAM Chip Select Base address and mask */
		pci_read_config_dword(pdev,
				AMD76X_MEM_BASE_ADDR + (index * 4), &mba);

		if (!(mba & BIT(0)))
			continue;

		mba_base = mba & 0xff800000UL;
		mba_mask = ((mba & 0xff80) << 16) | 0x7fffffUL;
		pci_read_config_dword(pdev, AMD76X_DRAM_MODE_STATUS, &dms);
		csrow->first_page = mba_base >> PAGE_SHIFT;
		dimm->nr_pages = (mba_mask + 1) >> PAGE_SHIFT;
		csrow->last_page = csrow->first_page + dimm->nr_pages - 1;
		csrow->page_mask = mba_mask >> PAGE_SHIFT;
		dimm->grain = dimm->nr_pages << PAGE_SHIFT;
		dimm->mtype = MEM_RDDR;
		dimm->dtype = ((dms >> index) & 0x1) ? DEV_X4 : DEV_UNKNOWN;
		dimm->edac_mode = edac_mode;
	}
}

/**
 *	amd76x_probe1	-	Perform set up for detected device
 *	@pdev; PCI device detected
 *	@dev_idx: Device type index
 *
 *	We have found an AMD76x and now need to set up the memory
 *	controller status reporting. We configure and set up the
 *	memory controller reporting and claim the device.
 */
static int amd76x_probe1(struct pci_dev *pdev, int dev_idx)
{
	static const enum edac_type ems_modes[] = {
		EDAC_NONE,
		EDAC_EC,
		EDAC_SECDED,
		EDAC_SECDED
	};
	struct mem_ctl_info *mci;
	struct edac_mc_layer layers[2];
	u32 ems;
	u32 ems_mode;
	struct amd76x_error_info discard;

	edac_dbg(0, "\n");
	pci_read_config_dword(pdev, AMD76X_ECC_MODE_STATUS, &ems);
	ems_mode = (ems >> 10) & 0x3;

	layers[0].type = EDAC_MC_LAYER_CHIP_SELECT;
	layers[0].size = AMD76X_NR_CSROWS;
	layers[0].is_virt_csrow = true;
	layers[1].type = EDAC_MC_LAYER_CHANNEL;
	layers[1].size = 1;
	layers[1].is_virt_csrow = false;
	mci = edac_mc_alloc(0, ARRAY_SIZE(layers), layers, 0);

	if (mci == NULL)
		return -ENOMEM;

	edac_dbg(0, "mci = %p\n", mci);
	mci->pdev = &pdev->dev;
	mci->mtype_cap = MEM_FLAG_RDDR;
	mci->edac_ctl_cap = EDAC_FLAG_NONE | EDAC_FLAG_EC | EDAC_FLAG_SECDED;
	mci->edac_cap = ems_mode ?
		(EDAC_FLAG_EC | EDAC_FLAG_SECDED) : EDAC_FLAG_NONE;
	mci->mod_name = EDAC_MOD_STR;
	mci->mod_ver = AMD76X_REVISION;
	mci->ctl_name = amd76x_devs[dev_idx].ctl_name;
	mci->dev_name = pci_name(pdev);
	mci->edac_check = amd76x_check;
	mci->ctl_page_to_phys = NULL;

	amd76x_init_csrows(mci, pdev, ems_modes[ems_mode]);
	amd76x_get_error_info(mci, &discard);	/* clear counters */

	/* Here we assume that we will never see multiple instances of this
	 * type of memory controller.  The ID is therefore hardcoded to 0.
	 */
	if (edac_mc_add_mc(mci)) {
		edac_dbg(3, "failed edac_mc_add_mc()\n");
		goto fail;
	}

	/* allocating generic PCI control info */
	amd76x_pci = edac_pci_create_generic_ctl(&pdev->dev, EDAC_MOD_STR);
	if (!amd76x_pci) {
		printk(KERN_WARNING
			"%s(): Unable to create PCI control\n",
			__func__);
		printk(KERN_WARNING
			"%s(): PCI error report via EDAC not setup\n",
			__func__);
	}

	/* get this far and it's successful */
	edac_dbg(3, "success\n");
	return 0;

fail:
	edac_mc_free(mci);
	return -ENODEV;
}

/* returns count (>= 0), or negative on error */
static int amd76x_init_one(struct pci_dev *pdev,
			   const struct pci_device_id *ent)
{
	edac_dbg(0, "\n");

	/* don't need to call pci_enable_device() */
	return amd76x_probe1(pdev, ent->driver_data);
}

/**
 *	amd76x_remove_one	-	driver shutdown
 *	@pdev: PCI device being handed back
 *
 *	Called when the driver is unloaded. Find the matching mci
 *	structure for the device then delete the mci and free the
 *	resources.
 */
static void amd76x_remove_one(struct pci_dev *pdev)
{
	struct mem_ctl_info *mci;

	edac_dbg(0, "\n");

	if (amd76x_pci)
		edac_pci_release_generic_ctl(amd76x_pci);

	if ((mci = edac_mc_del_mc(&pdev->dev)) == NULL)
		return;

	edac_mc_free(mci);
}

static const struct pci_device_id amd76x_pci_tbl[] = {
	{
	 PCI_VEND_DEV(AMD, FE_GATE_700C), PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	 AMD762},
	{
	 PCI_VEND_DEV(AMD, FE_GATE_700E), PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	 AMD761},
	{
	 0,
	 }			/* 0 terminated list. */
};

MODULE_DEVICE_TABLE(pci, amd76x_pci_tbl);

static struct pci_driver amd76x_driver = {
	.name = EDAC_MOD_STR,
	.probe = amd76x_init_one,
	.remove = amd76x_remove_one,
	.id_table = amd76x_pci_tbl,
};

static int __init amd76x_init(void)
{
       /* Ensure that the OPSTATE is set correctly for POLL or NMI */
       opstate_init();

	return pci_register_driver(&amd76x_driver);
}

static void __exit amd76x_exit(void)
{
	pci_unregister_driver(&amd76x_driver);
}

module_init(amd76x_init);
module_exit(amd76x_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Linux Networx (http://lnxi.com) Thayne Harbaugh");
MODULE_DESCRIPTION("MC support for AMD 76x memory controllers");

module_param(edac_op_state, int, 0444);
MODULE_PARM_DESC(edac_op_state, "EDAC Error Reporting state: 0=Poll,1=NMI");

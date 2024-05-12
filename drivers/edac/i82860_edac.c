/*
 * Intel 82860 Memory Controller kernel module
 * (C) 2005 Red Hat (http://www.redhat.com)
 * This file may be distributed under the terms of the
 * GNU General Public License.
 *
 * Written by Ben Woodard <woodard@redhat.com>
 * shamelessly copied from and based upon the edac_i82875 driver
 * by Thayne Harbaugh of Linux Networx. (http://lnxi.com)
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/edac.h>
#include "edac_module.h"

#define EDAC_MOD_STR	"i82860_edac"

#define i82860_printk(level, fmt, arg...) \
	edac_printk(level, "i82860", fmt, ##arg)

#define i82860_mc_printk(mci, level, fmt, arg...) \
	edac_mc_chipset_printk(mci, level, "i82860", fmt, ##arg)

#ifndef PCI_DEVICE_ID_INTEL_82860_0
#define PCI_DEVICE_ID_INTEL_82860_0	0x2531
#endif				/* PCI_DEVICE_ID_INTEL_82860_0 */

#define I82860_MCHCFG 0x50
#define I82860_GBA 0x60
#define I82860_GBA_MASK 0x7FF
#define I82860_GBA_SHIFT 24
#define I82860_ERRSTS 0xC8
#define I82860_EAP 0xE4
#define I82860_DERRCTL_STS 0xE2

enum i82860_chips {
	I82860 = 0,
};

struct i82860_dev_info {
	const char *ctl_name;
};

struct i82860_error_info {
	u16 errsts;
	u32 eap;
	u16 derrsyn;
	u16 errsts2;
};

static const struct i82860_dev_info i82860_devs[] = {
	[I82860] = {
		.ctl_name = "i82860"},
};

static struct pci_dev *mci_pdev;	/* init dev: in case that AGP code
					 * has already registered driver
					 */
static struct edac_pci_ctl_info *i82860_pci;

static void i82860_get_error_info(struct mem_ctl_info *mci,
				struct i82860_error_info *info)
{
	struct pci_dev *pdev;

	pdev = to_pci_dev(mci->pdev);

	/*
	 * This is a mess because there is no atomic way to read all the
	 * registers at once and the registers can transition from CE being
	 * overwritten by UE.
	 */
	pci_read_config_word(pdev, I82860_ERRSTS, &info->errsts);
	pci_read_config_dword(pdev, I82860_EAP, &info->eap);
	pci_read_config_word(pdev, I82860_DERRCTL_STS, &info->derrsyn);
	pci_read_config_word(pdev, I82860_ERRSTS, &info->errsts2);

	pci_write_bits16(pdev, I82860_ERRSTS, 0x0003, 0x0003);

	/*
	 * If the error is the same for both reads then the first set of reads
	 * is valid.  If there is a change then there is a CE no info and the
	 * second set of reads is valid and should be UE info.
	 */
	if (!(info->errsts2 & 0x0003))
		return;

	if ((info->errsts ^ info->errsts2) & 0x0003) {
		pci_read_config_dword(pdev, I82860_EAP, &info->eap);
		pci_read_config_word(pdev, I82860_DERRCTL_STS, &info->derrsyn);
	}
}

static int i82860_process_error_info(struct mem_ctl_info *mci,
				struct i82860_error_info *info,
				int handle_errors)
{
	struct dimm_info *dimm;
	int row;

	if (!(info->errsts2 & 0x0003))
		return 0;

	if (!handle_errors)
		return 1;

	if ((info->errsts ^ info->errsts2) & 0x0003) {
		edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci, 1, 0, 0, 0,
				     -1, -1, -1, "UE overwrote CE", "");
		info->errsts = info->errsts2;
	}

	info->eap >>= PAGE_SHIFT;
	row = edac_mc_find_csrow_by_page(mci, info->eap);
	dimm = mci->csrows[row]->channels[0]->dimm;

	if (info->errsts & 0x0002)
		edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci, 1,
				     info->eap, 0, 0,
				     dimm->location[0], dimm->location[1], -1,
				     "i82860 UE", "");
	else
		edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci, 1,
				     info->eap, 0, info->derrsyn,
				     dimm->location[0], dimm->location[1], -1,
				     "i82860 CE", "");

	return 1;
}

static void i82860_check(struct mem_ctl_info *mci)
{
	struct i82860_error_info info;

	i82860_get_error_info(mci, &info);
	i82860_process_error_info(mci, &info, 1);
}

static void i82860_init_csrows(struct mem_ctl_info *mci, struct pci_dev *pdev)
{
	unsigned long last_cumul_size;
	u16 mchcfg_ddim;	/* DRAM Data Integrity Mode 0=none, 2=edac */
	u16 value;
	u32 cumul_size;
	struct csrow_info *csrow;
	struct dimm_info *dimm;
	int index;

	pci_read_config_word(pdev, I82860_MCHCFG, &mchcfg_ddim);
	mchcfg_ddim = mchcfg_ddim & 0x180;
	last_cumul_size = 0;

	/* The group row boundary (GRA) reg values are boundary address
	 * for each DRAM row with a granularity of 16MB.  GRA regs are
	 * cumulative; therefore GRA15 will contain the total memory contained
	 * in all eight rows.
	 */
	for (index = 0; index < mci->nr_csrows; index++) {
		csrow = mci->csrows[index];
		dimm = csrow->channels[0]->dimm;

		pci_read_config_word(pdev, I82860_GBA + index * 2, &value);
		cumul_size = (value & I82860_GBA_MASK) <<
			(I82860_GBA_SHIFT - PAGE_SHIFT);
		edac_dbg(3, "(%d) cumul_size 0x%x\n", index, cumul_size);

		if (cumul_size == last_cumul_size)
			continue;	/* not populated */

		csrow->first_page = last_cumul_size;
		csrow->last_page = cumul_size - 1;
		dimm->nr_pages = cumul_size - last_cumul_size;
		last_cumul_size = cumul_size;
		dimm->grain = 1 << 12;	/* I82860_EAP has 4KiB reolution */
		dimm->mtype = MEM_RMBS;
		dimm->dtype = DEV_UNKNOWN;
		dimm->edac_mode = mchcfg_ddim ? EDAC_SECDED : EDAC_NONE;
	}
}

static int i82860_probe1(struct pci_dev *pdev, int dev_idx)
{
	struct mem_ctl_info *mci;
	struct edac_mc_layer layers[2];
	struct i82860_error_info discard;

	/*
	 * RDRAM has channels but these don't map onto the csrow abstraction.
	 * According with the datasheet, there are 2 Rambus channels, supporting
	 * up to 16 direct RDRAM devices.
	 * The device groups from the GRA registers seem to map reasonably
	 * well onto the notion of a chip select row.
	 * There are 16 GRA registers and since the name is associated with
	 * the channel and the GRA registers map to physical devices so we are
	 * going to make 1 channel for group.
	 */
	layers[0].type = EDAC_MC_LAYER_CHANNEL;
	layers[0].size = 2;
	layers[0].is_virt_csrow = true;
	layers[1].type = EDAC_MC_LAYER_SLOT;
	layers[1].size = 8;
	layers[1].is_virt_csrow = true;
	mci = edac_mc_alloc(0, ARRAY_SIZE(layers), layers, 0);
	if (!mci)
		return -ENOMEM;

	edac_dbg(3, "init mci\n");
	mci->pdev = &pdev->dev;
	mci->mtype_cap = MEM_FLAG_DDR;
	mci->edac_ctl_cap = EDAC_FLAG_NONE | EDAC_FLAG_SECDED;
	/* I"m not sure about this but I think that all RDRAM is SECDED */
	mci->edac_cap = EDAC_FLAG_SECDED;
	mci->mod_name = EDAC_MOD_STR;
	mci->ctl_name = i82860_devs[dev_idx].ctl_name;
	mci->dev_name = pci_name(pdev);
	mci->edac_check = i82860_check;
	mci->ctl_page_to_phys = NULL;
	i82860_init_csrows(mci, pdev);
	i82860_get_error_info(mci, &discard);	/* clear counters */

	/* Here we assume that we will never see multiple instances of this
	 * type of memory controller.  The ID is therefore hardcoded to 0.
	 */
	if (edac_mc_add_mc(mci)) {
		edac_dbg(3, "failed edac_mc_add_mc()\n");
		goto fail;
	}

	/* allocating generic PCI control info */
	i82860_pci = edac_pci_create_generic_ctl(&pdev->dev, EDAC_MOD_STR);
	if (!i82860_pci) {
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
static int i82860_init_one(struct pci_dev *pdev,
			   const struct pci_device_id *ent)
{
	int rc;

	edac_dbg(0, "\n");
	i82860_printk(KERN_INFO, "i82860 init one\n");

	if (pci_enable_device(pdev) < 0)
		return -EIO;

	rc = i82860_probe1(pdev, ent->driver_data);

	if (rc == 0)
		mci_pdev = pci_dev_get(pdev);

	return rc;
}

static void i82860_remove_one(struct pci_dev *pdev)
{
	struct mem_ctl_info *mci;

	edac_dbg(0, "\n");

	if (i82860_pci)
		edac_pci_release_generic_ctl(i82860_pci);

	if ((mci = edac_mc_del_mc(&pdev->dev)) == NULL)
		return;

	edac_mc_free(mci);
}

static const struct pci_device_id i82860_pci_tbl[] = {
	{
	 PCI_VEND_DEV(INTEL, 82860_0), PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	 I82860},
	{
	 0,
	 }			/* 0 terminated list. */
};

MODULE_DEVICE_TABLE(pci, i82860_pci_tbl);

static struct pci_driver i82860_driver = {
	.name = EDAC_MOD_STR,
	.probe = i82860_init_one,
	.remove = i82860_remove_one,
	.id_table = i82860_pci_tbl,
};

static int __init i82860_init(void)
{
	int pci_rc;

	edac_dbg(3, "\n");

       /* Ensure that the OPSTATE is set correctly for POLL or NMI */
       opstate_init();

	if ((pci_rc = pci_register_driver(&i82860_driver)) < 0)
		goto fail0;

	if (!mci_pdev) {
		mci_pdev = pci_get_device(PCI_VENDOR_ID_INTEL,
					PCI_DEVICE_ID_INTEL_82860_0, NULL);

		if (mci_pdev == NULL) {
			edac_dbg(0, "860 pci_get_device fail\n");
			pci_rc = -ENODEV;
			goto fail1;
		}

		pci_rc = i82860_init_one(mci_pdev, i82860_pci_tbl);

		if (pci_rc < 0) {
			edac_dbg(0, "860 init fail\n");
			pci_rc = -ENODEV;
			goto fail1;
		}
	}

	return 0;

fail1:
	pci_unregister_driver(&i82860_driver);

fail0:
	pci_dev_put(mci_pdev);
	return pci_rc;
}

static void __exit i82860_exit(void)
{
	edac_dbg(3, "\n");
	pci_unregister_driver(&i82860_driver);
	pci_dev_put(mci_pdev);
}

module_init(i82860_init);
module_exit(i82860_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Red Hat Inc. (http://www.redhat.com) Ben Woodard <woodard@redhat.com>");
MODULE_DESCRIPTION("ECC support for Intel 82860 memory hub controllers");

module_param(edac_op_state, int, 0444);
MODULE_PARM_DESC(edac_op_state, "EDAC Error Reporting state: 0=Poll,1=NMI");

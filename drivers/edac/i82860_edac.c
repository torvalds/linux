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


#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/slab.h>
#include "edac_mc.h"


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

static struct pci_dev *mci_pdev = NULL;	/* init dev: in case that AGP code
					   has already registered driver */

static int i82860_registered = 1;

static void i82860_get_error_info (struct mem_ctl_info *mci,
		struct i82860_error_info *info)
{
	/*
	 * This is a mess because there is no atomic way to read all the
	 * registers at once and the registers can transition from CE being
	 * overwritten by UE.
	 */
	pci_read_config_word(mci->pdev, I82860_ERRSTS, &info->errsts);
	pci_read_config_dword(mci->pdev, I82860_EAP, &info->eap);
	pci_read_config_word(mci->pdev, I82860_DERRCTL_STS, &info->derrsyn);
	pci_read_config_word(mci->pdev, I82860_ERRSTS, &info->errsts2);

	pci_write_bits16(mci->pdev, I82860_ERRSTS, 0x0003, 0x0003);

	/*
	 * If the error is the same for both reads then the first set of reads
	 * is valid.  If there is a change then there is a CE no info and the
	 * second set of reads is valid and should be UE info.
	 */
	if (!(info->errsts2 & 0x0003))
		return;
	if ((info->errsts ^ info->errsts2) & 0x0003) {
		pci_read_config_dword(mci->pdev, I82860_EAP, &info->eap);
		pci_read_config_word(mci->pdev, I82860_DERRCTL_STS,
		    &info->derrsyn);
	}
}

static int i82860_process_error_info (struct mem_ctl_info *mci,
		struct i82860_error_info *info, int handle_errors)
{
	int row;

	if (!(info->errsts2 & 0x0003))
		return 0;

	if (!handle_errors)
		return 1;

	if ((info->errsts ^ info->errsts2) & 0x0003) {
		edac_mc_handle_ce_no_info(mci, "UE overwrote CE");
		info->errsts = info->errsts2;
	}

	info->eap >>= PAGE_SHIFT;
	row = edac_mc_find_csrow_by_page(mci, info->eap);

	if (info->errsts & 0x0002)
		edac_mc_handle_ue(mci, info->eap, 0, row, "i82860 UE");
	else
		edac_mc_handle_ce(mci, info->eap, 0, info->derrsyn, row,
				       0, "i82860 UE");

	return 1;
}

static void i82860_check(struct mem_ctl_info *mci)
{
	struct i82860_error_info info;

	debugf1("MC%d: " __FILE__ ": %s()\n", mci->mc_idx, __func__);
	i82860_get_error_info(mci, &info);
	i82860_process_error_info(mci, &info, 1);
}

static int i82860_probe1(struct pci_dev *pdev, int dev_idx)
{
	int rc = -ENODEV;
	int index;
	struct mem_ctl_info *mci = NULL;
	unsigned long last_cumul_size;

	u16 mchcfg_ddim;	/* DRAM Data Integrity Mode 0=none,2=edac */

	/* RDRAM has channels but these don't map onto the abstractions that
	   edac uses.
	   The device groups from the GRA registers seem to map reasonably
	   well onto the notion of a chip select row.
	   There are 16 GRA registers and since the name is associated with
	   the channel and the GRA registers map to physical devices so we are
	   going to make 1 channel for group.
	 */
	mci = edac_mc_alloc(0, 16, 1);
	if (!mci)
		return -ENOMEM;

	debugf3("MC: " __FILE__ ": %s(): init mci\n", __func__);

	mci->pdev = pdev;
	mci->mtype_cap = MEM_FLAG_DDR;


	mci->edac_ctl_cap = EDAC_FLAG_NONE | EDAC_FLAG_SECDED;
	/* I"m not sure about this but I think that all RDRAM is SECDED */
	mci->edac_cap = EDAC_FLAG_SECDED;
	/* adjust FLAGS */

	mci->mod_name = BS_MOD_STR;
	mci->mod_ver = "$Revision: 1.1.2.6 $";
	mci->ctl_name = i82860_devs[dev_idx].ctl_name;
	mci->edac_check = i82860_check;
	mci->ctl_page_to_phys = NULL;

	pci_read_config_word(mci->pdev, I82860_MCHCFG, &mchcfg_ddim);
	mchcfg_ddim = mchcfg_ddim & 0x180;

	/*
	 * The group row boundary (GRA) reg values are boundary address
	 * for each DRAM row with a granularity of 16MB.  GRA regs are
	 * cumulative; therefore GRA15 will contain the total memory contained
	 * in all eight rows.
	 */
	for (last_cumul_size = index = 0; index < mci->nr_csrows; index++) {
		u16 value;
		u32 cumul_size;
		struct csrow_info *csrow = &mci->csrows[index];

		pci_read_config_word(mci->pdev, I82860_GBA + index * 2,
				     &value);

		cumul_size = (value & I82860_GBA_MASK) <<
		    (I82860_GBA_SHIFT - PAGE_SHIFT);
		debugf3("MC: " __FILE__ ": %s(): (%d) cumul_size 0x%x\n",
			__func__, index, cumul_size);
		if (cumul_size == last_cumul_size)
			continue;	/* not populated */

		csrow->first_page = last_cumul_size;
		csrow->last_page = cumul_size - 1;
		csrow->nr_pages = cumul_size - last_cumul_size;
		last_cumul_size = cumul_size;
		csrow->grain = 1 << 12;	/* I82860_EAP has 4KiB reolution */
		csrow->mtype = MEM_RMBS;
		csrow->dtype = DEV_UNKNOWN;
		csrow->edac_mode = mchcfg_ddim ? EDAC_SECDED : EDAC_NONE;
	}

	/* clear counters */
	pci_write_bits16(mci->pdev, I82860_ERRSTS, 0x0003, 0x0003);

	if (edac_mc_add_mc(mci)) {
		debugf3("MC: " __FILE__
			": %s(): failed edac_mc_add_mc()\n",
			__func__);
		edac_mc_free(mci);
	} else {
		/* get this far and it's successful */
		debugf3("MC: " __FILE__ ": %s(): success\n", __func__);
		rc = 0;
	}
	return rc;
}

/* returns count (>= 0), or negative on error */
static int __devinit i82860_init_one(struct pci_dev *pdev,
				     const struct pci_device_id *ent)
{
	int rc;

	debugf0("MC: " __FILE__ ": %s()\n", __func__);

	printk(KERN_INFO "i82860 init one\n");
	if(pci_enable_device(pdev) < 0)
		return -EIO;
	rc = i82860_probe1(pdev, ent->driver_data);
	if(rc == 0)
		mci_pdev = pci_dev_get(pdev);
	return rc;
}

static void __devexit i82860_remove_one(struct pci_dev *pdev)
{
	struct mem_ctl_info *mci;

	debugf0(__FILE__ ": %s()\n", __func__);

	mci = edac_mc_find_mci_by_pdev(pdev);
	if ((mci != NULL) && (edac_mc_del_mc(mci) == 0))
		edac_mc_free(mci);
}

static const struct pci_device_id i82860_pci_tbl[] __devinitdata = {
	{PCI_VEND_DEV(INTEL, 82860_0), PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	 I82860},
	{0,}			/* 0 terminated list. */
};

MODULE_DEVICE_TABLE(pci, i82860_pci_tbl);

static struct pci_driver i82860_driver = {
	.name = BS_MOD_STR,
	.probe = i82860_init_one,
	.remove = __devexit_p(i82860_remove_one),
	.id_table = i82860_pci_tbl,
};

static int __init i82860_init(void)
{
	int pci_rc;

	debugf3("MC: " __FILE__ ": %s()\n", __func__);
	if ((pci_rc = pci_register_driver(&i82860_driver)) < 0)
		return pci_rc;

	if (!mci_pdev) {
		i82860_registered = 0;
		mci_pdev = pci_get_device(PCI_VENDOR_ID_INTEL,
					  PCI_DEVICE_ID_INTEL_82860_0, NULL);
		if (mci_pdev == NULL) {
			debugf0("860 pci_get_device fail\n");
			return -ENODEV;
		}
		pci_rc = i82860_init_one(mci_pdev, i82860_pci_tbl);
		if (pci_rc < 0) {
			debugf0("860 init fail\n");
			pci_dev_put(mci_pdev);
			return -ENODEV;
		}
	}
	return 0;
}

static void __exit i82860_exit(void)
{
	debugf3("MC: " __FILE__ ": %s()\n", __func__);

	pci_unregister_driver(&i82860_driver);
	if (!i82860_registered) {
		i82860_remove_one(mci_pdev);
		pci_dev_put(mci_pdev);
	}
}

module_init(i82860_init);
module_exit(i82860_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR
    ("Red Hat Inc. (http://www.redhat.com.com) Ben Woodard <woodard@redhat.com>");
MODULE_DESCRIPTION("ECC support for Intel 82860 memory hub controllers");

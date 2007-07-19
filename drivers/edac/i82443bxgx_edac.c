/*
 * Intel 82443BX/GX (440BX/GX chipset) Memory Controller EDAC kernel
 * module (C) 2006 Tim Small
 *
 * This file may be distributed under the terms of the GNU General
 * Public License.
 *
 * Written by Tim Small <tim@buttersideup.com>, based on work by Linux
 * Networx, Thayne Harbaugh, Dan Hollis <goemon at anime dot net> and
 * others.
 *
 * 440GX fix by Jason Uhlenkott <juhlenko@akamai.com>.
 *
 * Written with reference to 82443BX Host Bridge Datasheet:
 * http://www.intel.com/design/chipsets/440/documentation.htm
 * references to this document given in [].
 *
 * This module doesn't support the 440LX, but it may be possible to
 * make it do so (the 440LX's register definitions are different, but
 * not completely so - I haven't studied them in enough detail to know
 * how easy this would be).
 */

#include <linux/module.h>
#include <linux/init.h>

#include <linux/pci.h>
#include <linux/pci_ids.h>

#include <linux/slab.h>

#include "edac_core.h"

#define I82443_REVISION	"0.1"

#define EDAC_MOD_STR    "i82443bxgx_edac"

/* The 82443BX supports SDRAM, or EDO (EDO for mobile only), "Memory
 * Size: 8 MB to 512 MB (1GB with Registered DIMMs) with eight memory
 * rows" "The 82443BX supports multiple-bit error detection and
 * single-bit error correction when ECC mode is enabled and
 * single/multi-bit error detection when correction is disabled.
 * During writes to the DRAM, the 82443BX generates ECC for the data
 * on a QWord basis. Partial QWord writes require a read-modify-write
 * cycle when ECC is enabled."
*/

/* "Additionally, the 82443BX ensures that the data is corrected in
 * main memory so that accumulation of errors is prevented. Another
 * error within the same QWord would result in a double-bit error
 * which is unrecoverable. This is known as hardware scrubbing since
 * it requires no software intervention to correct the data in memory."
 */

/* [Also see page 100 (section 4.3), "DRAM Interface"]
 * [Also see page 112 (section 4.6.1.4), ECC]
 */

#define I82443BXGX_NR_CSROWS 8
#define I82443BXGX_NR_CHANS  1
#define I82443BXGX_NR_DIMMS  4

/* 82443 PCI Device 0 */
#define I82443BXGX_NBXCFG 0x50	/* 32bit register starting at this PCI
				 * config space offset */
#define I82443BXGX_NBXCFG_OFFSET_NON_ECCROW 24	/* Array of bits, zero if
						 * row is non-ECC */
#define I82443BXGX_NBXCFG_OFFSET_DRAM_FREQ 12	/* 2 bits,00=100MHz,10=66 MHz */

#define I82443BXGX_NBXCFG_OFFSET_DRAM_INTEGRITY 7	/* 2 bits:       */
#define I82443BXGX_NBXCFG_INTEGRITY_NONE   0x0	/* 00 = Non-ECC */
#define I82443BXGX_NBXCFG_INTEGRITY_EC     0x1	/* 01 = EC (only) */
#define I82443BXGX_NBXCFG_INTEGRITY_ECC    0x2	/* 10 = ECC */
#define I82443BXGX_NBXCFG_INTEGRITY_SCRUB  0x3	/* 11 = ECC + HW Scrub */

#define I82443BXGX_NBXCFG_OFFSET_ECC_DIAG_ENABLE  6

/* 82443 PCI Device 0 */
#define I82443BXGX_EAP   0x80	/* 32bit register starting at this PCI
				 * config space offset, Error Address
				 * Pointer Register */
#define I82443BXGX_EAP_OFFSET_EAP  12	/* High 20 bits of error address */
#define I82443BXGX_EAP_OFFSET_MBE  BIT(1)	/* Err at EAP was multi-bit (W1TC) */
#define I82443BXGX_EAP_OFFSET_SBE  BIT(0)	/* Err at EAP was single-bit (W1TC) */

#define I82443BXGX_ERRCMD  0x90	/* 8bit register starting at this PCI
				 * config space offset. */
#define I82443BXGX_ERRCMD_OFFSET_SERR_ON_MBE BIT(1)	/* 1 = enable */
#define I82443BXGX_ERRCMD_OFFSET_SERR_ON_SBE BIT(0)	/* 1 = enable */

#define I82443BXGX_ERRSTS  0x91	/* 16bit register starting at this PCI
				 * config space offset. */
#define I82443BXGX_ERRSTS_OFFSET_MBFRE 5	/* 3 bits - first err row multibit */
#define I82443BXGX_ERRSTS_OFFSET_MEF   BIT(4)	/* 1 = MBE occurred */
#define I82443BXGX_ERRSTS_OFFSET_SBFRE 1	/* 3 bits - first err row singlebit */
#define I82443BXGX_ERRSTS_OFFSET_SEF   BIT(0)	/* 1 = SBE occurred */

#define I82443BXGX_DRAMC 0x57	/* 8bit register starting at this PCI
				 * config space offset. */
#define I82443BXGX_DRAMC_OFFSET_DT 3	/* 2 bits, DRAM Type */
#define I82443BXGX_DRAMC_DRAM_IS_EDO 0	/* 00 = EDO */
#define I82443BXGX_DRAMC_DRAM_IS_SDRAM 1	/* 01 = SDRAM */
#define I82443BXGX_DRAMC_DRAM_IS_RSDRAM 2	/* 10 = Registered SDRAM */

#define I82443BXGX_DRB 0x60	/* 8x 8bit registers starting at this PCI
				 * config space offset. */

/* FIXME - don't poll when ECC disabled? */

struct i82443bxgx_edacmc_error_info {
	u32 eap;
};

static void i82443bxgx_edacmc_get_error_info(struct mem_ctl_info *mci,
					     struct i82443bxgx_edacmc_error_info
					     *info)
{
	struct pci_dev *pdev;
	pdev = to_pci_dev(mci->dev);
	pci_read_config_dword(pdev, I82443BXGX_EAP, &info->eap);
	if (info->eap & I82443BXGX_EAP_OFFSET_SBE)
		/* Clear error to allow next error to be reported [p.61] */
		pci_write_bits32(pdev, I82443BXGX_EAP,
				 I82443BXGX_EAP_OFFSET_SBE,
				 I82443BXGX_EAP_OFFSET_SBE);

	if (info->eap & I82443BXGX_EAP_OFFSET_MBE)
		/* Clear error to allow next error to be reported [p.61] */
		pci_write_bits32(pdev, I82443BXGX_EAP,
				 I82443BXGX_EAP_OFFSET_MBE,
				 I82443BXGX_EAP_OFFSET_MBE);
}

static int i82443bxgx_edacmc_process_error_info(struct mem_ctl_info *mci,
						struct
						i82443bxgx_edacmc_error_info
						*info, int handle_errors)
{
	int error_found = 0;
	u32 eapaddr, page, pageoffset;

	/* bits 30:12 hold the 4kb block in which the error occurred
	 * [p.61] */
	eapaddr = (info->eap & 0xfffff000);
	page = eapaddr >> PAGE_SHIFT;
	pageoffset = eapaddr - (page << PAGE_SHIFT);

	if (info->eap & I82443BXGX_EAP_OFFSET_SBE) {
		error_found = 1;
		if (handle_errors)
			edac_mc_handle_ce(mci, page, pageoffset,
					  /* 440BX/GX don't make syndrome information available */
					  0, edac_mc_find_csrow_by_page(mci, page), 0,	/* channel */
					  mci->ctl_name);
	}

	if (info->eap & I82443BXGX_EAP_OFFSET_MBE) {
		error_found = 1;
		if (handle_errors)
			edac_mc_handle_ue(mci, page, pageoffset,
					  edac_mc_find_csrow_by_page(mci, page),
					  mci->ctl_name);
	}

	return error_found;
}

static void i82443bxgx_edacmc_check(struct mem_ctl_info *mci)
{
	struct i82443bxgx_edacmc_error_info info;

	debugf1("MC%d: " __FILE__ ": %s()\n", mci->mc_idx, __func__);
	i82443bxgx_edacmc_get_error_info(mci, &info);
	i82443bxgx_edacmc_process_error_info(mci, &info, 1);
}

static void i82443bxgx_init_csrows(struct mem_ctl_info *mci,
				   struct pci_dev *pdev,
				   enum edac_type edac_mode,
				   enum mem_type mtype)
{
	struct csrow_info *csrow;
	int index;
	u8 drbar, dramc;
	u32 row_base, row_high_limit, row_high_limit_last;

	pci_read_config_byte(pdev, I82443BXGX_DRAMC, &dramc);
	row_high_limit_last = 0;
	for (index = 0; index < mci->nr_csrows; index++) {
		csrow = &mci->csrows[index];
		pci_read_config_byte(pdev, I82443BXGX_DRB + index, &drbar);
		debugf1("MC%d: " __FILE__ ": %s() Row=%d DRB = %#0x\n",
			mci->mc_idx, __func__, index, drbar);
		row_high_limit = ((u32) drbar << 23);
		/* find the DRAM Chip Select Base address and mask */
		debugf1("MC%d: " __FILE__ ": %s() Row=%d, "
			"Boundry Address=%#0x, Last = %#0x \n",
			mci->mc_idx, __func__, index, row_high_limit,
			row_high_limit_last);

		/* 440GX goes to 2GB, represented with a DRB of 0. */
		if (row_high_limit_last && !row_high_limit)
			row_high_limit = 1UL << 31;

		/* This row is empty [p.49] */
		if (row_high_limit == row_high_limit_last)
			continue;
		row_base = row_high_limit_last;
		csrow->first_page = row_base >> PAGE_SHIFT;
		csrow->last_page = (row_high_limit >> PAGE_SHIFT) - 1;
		csrow->nr_pages = csrow->last_page - csrow->first_page + 1;
		/* EAP reports in 4kilobyte granularity [61] */
		csrow->grain = 1 << 12;
		csrow->mtype = mtype;
		/* I don't think 440BX can tell you device type? FIXME? */
		csrow->dtype = DEV_UNKNOWN;
		/* Mode is global to all rows on 440BX */
		csrow->edac_mode = edac_mode;
		row_high_limit_last = row_high_limit;
	}
}

static int i82443bxgx_edacmc_probe1(struct pci_dev *pdev, int dev_idx)
{
	struct mem_ctl_info *mci;
	u8 dramc;
	u32 nbxcfg, ecc_mode;
	enum mem_type mtype;
	enum edac_type edac_mode;

	debugf0("MC: " __FILE__ ": %s()\n", __func__);

	/* Something is really hosed if PCI config space reads from
	   the MC aren't working.  */
	if (pci_read_config_dword(pdev, I82443BXGX_NBXCFG, &nbxcfg))
		return -EIO;

	mci = edac_mc_alloc(0, I82443BXGX_NR_CSROWS, I82443BXGX_NR_CHANS);

	if (mci == NULL)
		return -ENOMEM;

	debugf0("MC: " __FILE__ ": %s(): mci = %p\n", __func__, mci);
	mci->dev = &pdev->dev;
	mci->mtype_cap = MEM_FLAG_EDO | MEM_FLAG_SDR | MEM_FLAG_RDR;
	mci->edac_ctl_cap = EDAC_FLAG_NONE | EDAC_FLAG_EC | EDAC_FLAG_SECDED;
	pci_read_config_byte(pdev, I82443BXGX_DRAMC, &dramc);
	switch ((dramc >> I82443BXGX_DRAMC_OFFSET_DT) & (BIT(0) | BIT(1))) {
	case I82443BXGX_DRAMC_DRAM_IS_EDO:
		mtype = MEM_EDO;
		break;
	case I82443BXGX_DRAMC_DRAM_IS_SDRAM:
		mtype = MEM_SDR;
		break;
	case I82443BXGX_DRAMC_DRAM_IS_RSDRAM:
		mtype = MEM_RDR;
		break;
	default:
		debugf0
		    ("Unknown/reserved DRAM type value in DRAMC register!\n");
		mtype = -MEM_UNKNOWN;
	}

	if ((mtype == MEM_SDR) || (mtype == MEM_RDR))
		mci->edac_cap = mci->edac_ctl_cap;
	else
		mci->edac_cap = EDAC_FLAG_NONE;

	mci->scrub_cap = SCRUB_FLAG_HW_SRC;
	pci_read_config_dword(pdev, I82443BXGX_NBXCFG, &nbxcfg);
	ecc_mode = ((nbxcfg >> I82443BXGX_NBXCFG_OFFSET_DRAM_INTEGRITY) &
		    (BIT(0) | BIT(1)));

	mci->scrub_mode = (ecc_mode == I82443BXGX_NBXCFG_INTEGRITY_SCRUB)
	    ? SCRUB_HW_SRC : SCRUB_NONE;

	switch (ecc_mode) {
	case I82443BXGX_NBXCFG_INTEGRITY_NONE:
		edac_mode = EDAC_NONE;
		break;
	case I82443BXGX_NBXCFG_INTEGRITY_EC:
		edac_mode = EDAC_EC;
		break;
	case I82443BXGX_NBXCFG_INTEGRITY_ECC:
	case I82443BXGX_NBXCFG_INTEGRITY_SCRUB:
		edac_mode = EDAC_SECDED;
		break;
	default:
		debugf0
		    ("%s(): Unknown/reserved ECC state in NBXCFG register!\n",
		     __func__);
		edac_mode = EDAC_UNKNOWN;
		break;
	}

	i82443bxgx_init_csrows(mci, pdev, edac_mode, mtype);

	/* Many BIOSes don't clear error flags on boot, so do this
	 * here, or we get "phantom" errors occuring at module-load
	 * time. */
	pci_write_bits32(pdev, I82443BXGX_EAP,
			 (I82443BXGX_EAP_OFFSET_SBE |
			  I82443BXGX_EAP_OFFSET_MBE),
			 (I82443BXGX_EAP_OFFSET_SBE |
			  I82443BXGX_EAP_OFFSET_MBE));

	mci->mod_name = EDAC_MOD_STR;
	mci->mod_ver = I82443_REVISION;
	mci->ctl_name = "I82443BXGX";
	mci->dev_name = pci_name(pdev);
	mci->edac_check = i82443bxgx_edacmc_check;
	mci->ctl_page_to_phys = NULL;

	if (edac_mc_add_mc(mci, 0)) {
		debugf3("%s(): failed edac_mc_add_mc()\n", __func__);
		goto fail;
	}

	debugf3("MC: " __FILE__ ": %s(): success\n", __func__);
	return 0;

      fail:
	edac_mc_free(mci);
	return -ENODEV;
}

EXPORT_SYMBOL_GPL(i82443bxgx_edacmc_probe1);

/* returns count (>= 0), or negative on error */
static int __devinit i82443bxgx_edacmc_init_one(struct pci_dev *pdev,
						const struct pci_device_id *ent)
{
	debugf0("MC: " __FILE__ ": %s()\n", __func__);

	/* don't need to call pci_device_enable() */
	return i82443bxgx_edacmc_probe1(pdev, ent->driver_data);
}

static void __devexit i82443bxgx_edacmc_remove_one(struct pci_dev *pdev)
{
	struct mem_ctl_info *mci;

	debugf0(__FILE__ ": %s()\n", __func__);

	if ((mci = edac_mc_del_mc(&pdev->dev)) == NULL)
		return;

	edac_mc_free(mci);
}

EXPORT_SYMBOL_GPL(i82443bxgx_edacmc_remove_one);

static const struct pci_device_id i82443bxgx_pci_tbl[] __devinitdata = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82443BX_0)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82443BX_2)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82443GX_0)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82443GX_2)},
	{0,}			/* 0 terminated list. */
};

MODULE_DEVICE_TABLE(pci, i82443bxgx_pci_tbl);

static struct pci_driver i82443bxgx_edacmc_driver = {
	.name = EDAC_MOD_STR,
	.probe = i82443bxgx_edacmc_init_one,
	.remove = __devexit_p(i82443bxgx_edacmc_remove_one),
	.id_table = i82443bxgx_pci_tbl,
};

static int __init i82443bxgx_edacmc_init(void)
{
	return pci_register_driver(&i82443bxgx_edacmc_driver);
}

static void __exit i82443bxgx_edacmc_exit(void)
{
	pci_unregister_driver(&i82443bxgx_edacmc_driver);
}

module_init(i82443bxgx_edacmc_init);
module_exit(i82443bxgx_edacmc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tim Small <tim@buttersideup.com> - WPAD");
MODULE_DESCRIPTION("EDAC MC support for Intel 82443BX/GX memory controllers");

/*
 * Radisys 82600 Embedded chipset Memory Controller kernel module
 * (C) 2005 EADS Astrium
 * This file may be distributed under the terms of the
 * GNU General Public License.
 *
 * Written by Tim Small <tim@buttersideup.com>, based on work by Thayne
 * Harbaugh, Dan Hollis <goemon at anime dot net> and others.
 *
 * $Id: edac_r82600.c,v 1.1.2.6 2005/10/05 00:43:44 dsp_llnl Exp $
 *
 * Written with reference to 82600 High Integration Dual PCI System
 * Controller Data Book:
 * www.radisys.com/files/support_downloads/007-01277-0002.82600DataBook.pdf
 * references to this document given in []
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/slab.h>
#include "edac_core.h"

#define R82600_REVISION	" Ver: 2.0.2 " __DATE__
#define EDAC_MOD_STR	"r82600_edac"

#define r82600_printk(level, fmt, arg...) \
	edac_printk(level, "r82600", fmt, ##arg)

#define r82600_mc_printk(mci, level, fmt, arg...) \
	edac_mc_chipset_printk(mci, level, "r82600", fmt, ##arg)

/* Radisys say "The 82600 integrates a main memory SDRAM controller that
 * supports up to four banks of memory. The four banks can support a mix of
 * sizes of 64 bit wide (72 bits with ECC) Synchronous DRAM (SDRAM) DIMMs,
 * each of which can be any size from 16MB to 512MB. Both registered (control
 * signals buffered) and unbuffered DIMM types are supported. Mixing of
 * registered and unbuffered DIMMs as well as mixing of ECC and non-ECC DIMMs
 * is not allowed. The 82600 SDRAM interface operates at the same frequency as
 * the CPU bus, 66MHz, 100MHz or 133MHz."
 */

#define R82600_NR_CSROWS 4
#define R82600_NR_CHANS  1
#define R82600_NR_DIMMS  4

#define R82600_BRIDGE_ID  0x8200

/* Radisys 82600 register addresses - device 0 function 0 - PCI bridge */
#define R82600_DRAMC	0x57	/* Various SDRAM related control bits
				 * all bits are R/W
				 *
				 * 7    SDRAM ISA Hole Enable
				 * 6    Flash Page Mode Enable
				 * 5    ECC Enable: 1=ECC 0=noECC
				 * 4    DRAM DIMM Type: 1=
				 * 3    BIOS Alias Disable
				 * 2    SDRAM BIOS Flash Write Enable
				 * 1:0  SDRAM Refresh Rate: 00=Disabled
				 *          01=7.8usec (256Mbit SDRAMs)
				 *          10=15.6us 11=125usec
				 */

#define R82600_SDRAMC	0x76	/* "SDRAM Control Register"
				 * More SDRAM related control bits
				 * all bits are R/W
				 *
				 * 15:8 Reserved.
				 *
				 * 7:5  Special SDRAM Mode Select
				 *
				 * 4    Force ECC
				 *
				 *        1=Drive ECC bits to 0 during
				 *          write cycles (i.e. ECC test mode)
				 *
				 *        0=Normal ECC functioning
				 *
				 * 3    Enhanced Paging Enable
				 *
				 * 2    CAS# Latency 0=3clks 1=2clks
				 *
				 * 1    RAS# to CAS# Delay 0=3 1=2
				 *
				 * 0    RAS# Precharge     0=3 1=2
				 */

#define R82600_EAP	0x80	/* ECC Error Address Pointer Register
				 *
				 * 31    Disable Hardware Scrubbing (RW)
				 *        0=Scrub on corrected read
				 *        1=Don't scrub on corrected read
				 *
				 * 30:12 Error Address Pointer (RO)
				 *        Upper 19 bits of error address
				 *
				 * 11:4  Syndrome Bits (RO)
				 *
				 * 3     BSERR# on multibit error (RW)
				 *        1=enable 0=disable
				 *
				 * 2     NMI on Single Bit Eror (RW)
				 *        1=NMI triggered by SBE n.b. other
				 *          prerequeists
				 *        0=NMI not triggered
				 *
				 * 1     MBE (R/WC)
				 *        read 1=MBE at EAP (see above)
				 *        read 0=no MBE, or SBE occurred first
				 *        write 1=Clear MBE status (must also
				 *          clear SBE)
				 *        write 0=NOP
				 *
				 * 1     SBE (R/WC)
				 *        read 1=SBE at EAP (see above)
				 *        read 0=no SBE, or MBE occurred first
				 *        write 1=Clear SBE status (must also
				 *          clear MBE)
				 *        write 0=NOP
				 */

#define R82600_DRBA	0x60	/* + 0x60..0x63 SDRAM Row Boundry Address
				 *  Registers
				 *
				 * 7:0  Address lines 30:24 - upper limit of
				 * each row [p57]
				 */

struct r82600_error_info {
	u32 eapr;
};

static unsigned int disable_hardware_scrub = 0;

static struct edac_pci_ctl_info *r82600_pci;

static void r82600_get_error_info(struct mem_ctl_info *mci,
				struct r82600_error_info *info)
{
	struct pci_dev *pdev;

	pdev = to_pci_dev(mci->dev);
	pci_read_config_dword(pdev, R82600_EAP, &info->eapr);

	if (info->eapr & BIT(0))
		/* Clear error to allow next error to be reported [p.62] */
		pci_write_bits32(pdev, R82600_EAP,
				 ((u32) BIT(0) & (u32) BIT(1)),
				 ((u32) BIT(0) & (u32) BIT(1)));

	if (info->eapr & BIT(1))
		/* Clear error to allow next error to be reported [p.62] */
		pci_write_bits32(pdev, R82600_EAP,
				 ((u32) BIT(0) & (u32) BIT(1)),
				 ((u32) BIT(0) & (u32) BIT(1)));
}

static int r82600_process_error_info(struct mem_ctl_info *mci,
				struct r82600_error_info *info,
				int handle_errors)
{
	int error_found;
	u32 eapaddr, page;
	u32 syndrome;

	error_found = 0;

	/* bits 30:12 store the upper 19 bits of the 32 bit error address */
	eapaddr = ((info->eapr >> 12) & 0x7FFF) << 13;
	/* Syndrome in bits 11:4 [p.62]       */
	syndrome = (info->eapr >> 4) & 0xFF;

	/* the R82600 reports at less than page *
	 * granularity (upper 19 bits only)     */
	page = eapaddr >> PAGE_SHIFT;

	if (info->eapr & BIT(0)) {	/* CE? */
		error_found = 1;

		if (handle_errors)
			edac_mc_handle_ce(mci, page, 0,	/* not avail */
					syndrome,
					edac_mc_find_csrow_by_page(mci, page),
					0, mci->ctl_name);
	}

	if (info->eapr & BIT(1)) {	/* UE? */
		error_found = 1;

		if (handle_errors)
			/* 82600 doesn't give enough info */
			edac_mc_handle_ue(mci, page, 0,
					edac_mc_find_csrow_by_page(mci, page),
					mci->ctl_name);
	}

	return error_found;
}

static void r82600_check(struct mem_ctl_info *mci)
{
	struct r82600_error_info info;

	debugf1("MC%d: %s()\n", mci->mc_idx, __func__);
	r82600_get_error_info(mci, &info);
	r82600_process_error_info(mci, &info, 1);
}

static inline int ecc_enabled(u8 dramcr)
{
	return dramcr & BIT(5);
}

static void r82600_init_csrows(struct mem_ctl_info *mci, struct pci_dev *pdev,
			u8 dramcr)
{
	struct csrow_info *csrow;
	int index;
	u8 drbar;		/* SDRAM Row Boundry Address Register */
	u32 row_high_limit, row_high_limit_last;
	u32 reg_sdram, ecc_on, row_base;

	ecc_on = ecc_enabled(dramcr);
	reg_sdram = dramcr & BIT(4);
	row_high_limit_last = 0;

	for (index = 0; index < mci->nr_csrows; index++) {
		csrow = &mci->csrows[index];

		/* find the DRAM Chip Select Base address and mask */
		pci_read_config_byte(pdev, R82600_DRBA + index, &drbar);

		debugf1("%s() Row=%d DRBA = %#0x\n", __func__, index, drbar);

		row_high_limit = ((u32) drbar << 24);
/*		row_high_limit = ((u32)drbar << 24) | 0xffffffUL; */

		debugf1("%s() Row=%d, Boundry Address=%#0x, Last = %#0x\n",
			__func__, index, row_high_limit, row_high_limit_last);

		/* Empty row [p.57] */
		if (row_high_limit == row_high_limit_last)
			continue;

		row_base = row_high_limit_last;

		csrow->first_page = row_base >> PAGE_SHIFT;
		csrow->last_page = (row_high_limit >> PAGE_SHIFT) - 1;
		csrow->nr_pages = csrow->last_page - csrow->first_page + 1;
		/* Error address is top 19 bits - so granularity is      *
		 * 14 bits                                               */
		csrow->grain = 1 << 14;
		csrow->mtype = reg_sdram ? MEM_RDDR : MEM_DDR;
		/* FIXME - check that this is unknowable with this chipset */
		csrow->dtype = DEV_UNKNOWN;

		/* Mode is global on 82600 */
		csrow->edac_mode = ecc_on ? EDAC_SECDED : EDAC_NONE;
		row_high_limit_last = row_high_limit;
	}
}

static int r82600_probe1(struct pci_dev *pdev, int dev_idx)
{
	struct mem_ctl_info *mci;
	u8 dramcr;
	u32 eapr;
	u32 scrub_disabled;
	u32 sdram_refresh_rate;
	struct r82600_error_info discard;

	debugf0("%s()\n", __func__);
	pci_read_config_byte(pdev, R82600_DRAMC, &dramcr);
	pci_read_config_dword(pdev, R82600_EAP, &eapr);
	scrub_disabled = eapr & BIT(31);
	sdram_refresh_rate = dramcr & (BIT(0) | BIT(1));
	debugf2("%s(): sdram refresh rate = %#0x\n", __func__,
		sdram_refresh_rate);
	debugf2("%s(): DRAMC register = %#0x\n", __func__, dramcr);
	mci = edac_mc_alloc(0, R82600_NR_CSROWS, R82600_NR_CHANS);

	if (mci == NULL)
		return -ENOMEM;

	debugf0("%s(): mci = %p\n", __func__, mci);
	mci->dev = &pdev->dev;
	mci->mtype_cap = MEM_FLAG_RDDR | MEM_FLAG_DDR;
	mci->edac_ctl_cap = EDAC_FLAG_NONE | EDAC_FLAG_EC | EDAC_FLAG_SECDED;
	/* FIXME try to work out if the chip leads have been used for COM2
	 * instead on this board? [MA6?] MAYBE:
	 */

	/* On the R82600, the pins for memory bits 72:65 - i.e. the   *
	 * EC bits are shared with the pins for COM2 (!), so if COM2  *
	 * is enabled, we assume COM2 is wired up, and thus no EDAC   *
	 * is possible.                                               */
	mci->edac_cap = EDAC_FLAG_NONE | EDAC_FLAG_EC | EDAC_FLAG_SECDED;

	if (ecc_enabled(dramcr)) {
		if (scrub_disabled)
			debugf3("%s(): mci = %p - Scrubbing disabled! EAP: "
				"%#0x\n", __func__, mci, eapr);
	} else
		mci->edac_cap = EDAC_FLAG_NONE;

	mci->mod_name = EDAC_MOD_STR;
	mci->mod_ver = R82600_REVISION;
	mci->ctl_name = "R82600";
	mci->dev_name = pci_name(pdev);
	mci->edac_check = r82600_check;
	mci->ctl_page_to_phys = NULL;
	r82600_init_csrows(mci, pdev, dramcr);
	r82600_get_error_info(mci, &discard);	/* clear counters */

	/* Here we assume that we will never see multiple instances of this
	 * type of memory controller.  The ID is therefore hardcoded to 0.
	 */
	if (edac_mc_add_mc(mci, 0)) {
		debugf3("%s(): failed edac_mc_add_mc()\n", __func__);
		goto fail;
	}

	/* get this far and it's successful */

	if (disable_hardware_scrub) {
		debugf3("%s(): Disabling Hardware Scrub (scrub on error)\n",
			__func__);
		pci_write_bits32(pdev, R82600_EAP, BIT(31), BIT(31));
	}

	/* allocating generic PCI control info */
	r82600_pci = edac_pci_create_generic_ctl(&pdev->dev, EDAC_MOD_STR);
	if (!r82600_pci) {
		printk(KERN_WARNING
			"%s(): Unable to create PCI control\n",
			__func__);
		printk(KERN_WARNING
			"%s(): PCI error report via EDAC not setup\n",
			__func__);
	}

	debugf3("%s(): success\n", __func__);
	return 0;

fail:
	edac_mc_free(mci);
	return -ENODEV;
}

/* returns count (>= 0), or negative on error */
static int __devinit r82600_init_one(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	debugf0("%s()\n", __func__);

	/* don't need to call pci_device_enable() */
	return r82600_probe1(pdev, ent->driver_data);
}

static void __devexit r82600_remove_one(struct pci_dev *pdev)
{
	struct mem_ctl_info *mci;

	debugf0("%s()\n", __func__);

	if (r82600_pci)
		edac_pci_release_generic_ctl(r82600_pci);

	if ((mci = edac_mc_del_mc(&pdev->dev)) == NULL)
		return;

	edac_mc_free(mci);
}

static const struct pci_device_id r82600_pci_tbl[] __devinitdata = {
	{
	 PCI_DEVICE(PCI_VENDOR_ID_RADISYS, R82600_BRIDGE_ID)
	 },
	{
	 0,
	 }			/* 0 terminated list. */
};

MODULE_DEVICE_TABLE(pci, r82600_pci_tbl);

static struct pci_driver r82600_driver = {
	.name = EDAC_MOD_STR,
	.probe = r82600_init_one,
	.remove = __devexit_p(r82600_remove_one),
	.id_table = r82600_pci_tbl,
};

static int __init r82600_init(void)
{
	return pci_register_driver(&r82600_driver);
}

static void __exit r82600_exit(void)
{
	pci_unregister_driver(&r82600_driver);
}

module_init(r82600_init);
module_exit(r82600_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tim Small <tim@buttersideup.com> - WPAD Ltd. "
		"on behalf of EADS Astrium");
MODULE_DESCRIPTION("MC support for Radisys 82600 memory controllers");

module_param(disable_hardware_scrub, bool, 0644);
MODULE_PARM_DESC(disable_hardware_scrub,
		 "If set, disable the chipset's automatic scrub for CEs");

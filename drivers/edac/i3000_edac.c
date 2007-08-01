/*
 * Intel 3000/3010 Memory Controller kernel module
 * Copyright (C) 2007 Akamai Technologies, Inc.
 * Shamelessly copied from:
 * 	Intel D82875P Memory Controller kernel module
 * 	(C) 2003 Linux Networx (http://lnxi.com)
 *
 * This file may be distributed under the terms of the
 * GNU General Public License.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/slab.h>
#include "edac_core.h"

#define I3000_REVISION		"1.1"

#define EDAC_MOD_STR		"i3000_edac"

#define I3000_RANKS		8
#define I3000_RANKS_PER_CHANNEL	4
#define I3000_CHANNELS		2

/* Intel 3000 register addresses - device 0 function 0 - DRAM Controller */

#define I3000_MCHBAR		0x44	/* MCH Memory Mapped Register BAR */
#define I3000_MCHBAR_MASK	0xffffc000
#define I3000_MMR_WINDOW_SIZE	16384

#define I3000_EDEAP		0x70	/* Extended DRAM Error Address Pointer (8b)
					 *
					 * 7:1   reserved
					 * 0     bit 32 of address
					 */
#define I3000_DEAP		0x58	/* DRAM Error Address Pointer (32b)
					 *
					 * 31:7  address
					 * 6:1   reserved
					 * 0     Error channel 0/1
					 */
#define I3000_DEAP_GRAIN	(1 << 7)
#define I3000_DEAP_PFN(edeap, deap)	((((edeap) & 1) << (32 - PAGE_SHIFT)) | \
					((deap) >> PAGE_SHIFT))
#define I3000_DEAP_OFFSET(deap)		((deap) & ~(I3000_DEAP_GRAIN-1) & ~PAGE_MASK)
#define I3000_DEAP_CHANNEL(deap)	((deap) & 1)

#define I3000_DERRSYN		0x5c	/* DRAM Error Syndrome (8b)
					 *
					 *  7:0  DRAM ECC Syndrome
					 */

#define I3000_ERRSTS		0xc8	/* Error Status Register (16b)
					 *
					 * 15:12 reserved
					 * 11    MCH Thermal Sensor Event for SMI/SCI/SERR
					 * 10    reserved
					 *  9    LOCK to non-DRAM Memory Flag (LCKF)
					 *  8    Received Refresh Timeout Flag (RRTOF)
					 *  7:2  reserved
					 *  1    Multiple-bit DRAM ECC Error Flag (DMERR)
					 *  0    Single-bit DRAM ECC Error Flag (DSERR)
					 */
#define I3000_ERRSTS_BITS	0x0b03	/* bits which indicate errors */
#define I3000_ERRSTS_UE		0x0002
#define I3000_ERRSTS_CE		0x0001

#define I3000_ERRCMD		0xca	/* Error Command (16b)
					 *
					 * 15:12 reserved
					 * 11    SERR on MCH Thermal Sensor Event (TSESERR)
					 * 10    reserved
					 *  9    SERR on LOCK to non-DRAM Memory (LCKERR)
					 *  8    SERR on DRAM Refresh Timeout (DRTOERR)
					 *  7:2  reserved
					 *  1    SERR Multiple-Bit DRAM ECC Error (DMERR)
					 *  0    SERR on Single-Bit ECC Error (DSERR)
					 */

/* Intel  MMIO register space - device 0 function 0 - MMR space */

#define I3000_DRB_SHIFT 25	/* 32MiB grain */

#define I3000_C0DRB		0x100	/* Channel 0 DRAM Rank Boundary (8b x 4)
					 *
					 * 7:0   Channel 0 DRAM Rank Boundary Address
					 */
#define I3000_C1DRB		0x180	/* Channel 1 DRAM Rank Boundary (8b x 4)
					 *
					 * 7:0   Channel 1 DRAM Rank Boundary Address
					 */

#define I3000_C0DRA		0x108	/* Channel 0 DRAM Rank Attribute (8b x 2)
					 *
					 * 7     reserved
					 * 6:4   DRAM odd Rank Attribute
					 * 3     reserved
					 * 2:0   DRAM even Rank Attribute
					 *
					 * Each attribute defines the page
					 * size of the corresponding rank:
					 *     000: unpopulated
					 *     001: reserved
					 *     010: 4 KB
					 *     011: 8 KB
					 *     100: 16 KB
					 *     Others: reserved
					 */
#define I3000_C1DRA		0x188	/* Channel 1 DRAM Rank Attribute (8b x 2) */
#define ODD_RANK_ATTRIB(dra) (((dra) & 0x70) >> 4)
#define EVEN_RANK_ATTRIB(dra) ((dra) & 0x07)

#define I3000_C0DRC0		0x120	/* DRAM Controller Mode 0 (32b)
					 *
					 * 31:30 reserved
					 * 29    Initialization Complete (IC)
					 * 28:11 reserved
					 * 10:8  Refresh Mode Select (RMS)
					 * 7     reserved
					 * 6:4   Mode Select (SMS)
					 * 3:2   reserved
					 * 1:0   DRAM Type (DT)
					 */

#define I3000_C0DRC1		0x124	/* DRAM Controller Mode 1 (32b)
					 *
					 * 31    Enhanced Addressing Enable (ENHADE)
					 * 30:0  reserved
					 */

enum i3000p_chips {
	I3000 = 0,
};

struct i3000_dev_info {
	const char *ctl_name;
};

struct i3000_error_info {
	u16 errsts;
	u8 derrsyn;
	u8 edeap;
	u32 deap;
	u16 errsts2;
};

static const struct i3000_dev_info i3000_devs[] = {
	[I3000] = {
		.ctl_name = "i3000"},
};

static struct pci_dev *mci_pdev;
static int i3000_registered = 1;
static struct edac_pci_ctl_info *i3000_pci;

static void i3000_get_error_info(struct mem_ctl_info *mci,
				 struct i3000_error_info *info)
{
	struct pci_dev *pdev;

	pdev = to_pci_dev(mci->dev);

	/*
	 * This is a mess because there is no atomic way to read all the
	 * registers at once and the registers can transition from CE being
	 * overwritten by UE.
	 */
	pci_read_config_word(pdev, I3000_ERRSTS, &info->errsts);
	if (!(info->errsts & I3000_ERRSTS_BITS))
		return;
	pci_read_config_byte(pdev, I3000_EDEAP, &info->edeap);
	pci_read_config_dword(pdev, I3000_DEAP, &info->deap);
	pci_read_config_byte(pdev, I3000_DERRSYN, &info->derrsyn);
	pci_read_config_word(pdev, I3000_ERRSTS, &info->errsts2);

	/*
	 * If the error is the same for both reads then the first set
	 * of reads is valid.  If there is a change then there is a CE
	 * with no info and the second set of reads is valid and
	 * should be UE info.
	 */
	if ((info->errsts ^ info->errsts2) & I3000_ERRSTS_BITS) {
		pci_read_config_byte(pdev, I3000_EDEAP, &info->edeap);
		pci_read_config_dword(pdev, I3000_DEAP, &info->deap);
		pci_read_config_byte(pdev, I3000_DERRSYN, &info->derrsyn);
	}

	/* Clear any error bits.
	 * (Yes, we really clear bits by writing 1 to them.)
	 */
	pci_write_bits16(pdev, I3000_ERRSTS, I3000_ERRSTS_BITS,
			 I3000_ERRSTS_BITS);
}

static int i3000_process_error_info(struct mem_ctl_info *mci,
				struct i3000_error_info *info,
				int handle_errors)
{
	int row, multi_chan;
	int pfn, offset, channel;

	multi_chan = mci->csrows[0].nr_channels - 1;

	if (!(info->errsts & I3000_ERRSTS_BITS))
		return 0;

	if (!handle_errors)
		return 1;

	if ((info->errsts ^ info->errsts2) & I3000_ERRSTS_BITS) {
		edac_mc_handle_ce_no_info(mci, "UE overwrote CE");
		info->errsts = info->errsts2;
	}

	pfn = I3000_DEAP_PFN(info->edeap, info->deap);
	offset = I3000_DEAP_OFFSET(info->deap);
	channel = I3000_DEAP_CHANNEL(info->deap);

	row = edac_mc_find_csrow_by_page(mci, pfn);

	if (info->errsts & I3000_ERRSTS_UE)
		edac_mc_handle_ue(mci, pfn, offset, row, "i3000 UE");
	else
		edac_mc_handle_ce(mci, pfn, offset, info->derrsyn, row,
				multi_chan ? channel : 0, "i3000 CE");

	return 1;
}

static void i3000_check(struct mem_ctl_info *mci)
{
	struct i3000_error_info info;

	debugf1("MC%d: %s()\n", mci->mc_idx, __func__);
	i3000_get_error_info(mci, &info);
	i3000_process_error_info(mci, &info, 1);
}

static int i3000_is_interleaved(const unsigned char *c0dra,
				const unsigned char *c1dra,
				const unsigned char *c0drb,
				const unsigned char *c1drb)
{
	int i;

	/* If the channels aren't populated identically then
	 * we're not interleaved.
	 */
	for (i = 0; i < I3000_RANKS_PER_CHANNEL / 2; i++)
		if (ODD_RANK_ATTRIB(c0dra[i]) != ODD_RANK_ATTRIB(c1dra[i]) ||
			EVEN_RANK_ATTRIB(c0dra[i]) !=
						EVEN_RANK_ATTRIB(c1dra[i]))
			return 0;

	/* If the rank boundaries for the two channels are different
	 * then we're not interleaved.
	 */
	for (i = 0; i < I3000_RANKS_PER_CHANNEL; i++)
		if (c0drb[i] != c1drb[i])
			return 0;

	return 1;
}

static int i3000_probe1(struct pci_dev *pdev, int dev_idx)
{
	int rc;
	int i;
	struct mem_ctl_info *mci = NULL;
	unsigned long last_cumul_size;
	int interleaved, nr_channels;
	unsigned char dra[I3000_RANKS / 2], drb[I3000_RANKS];
	unsigned char *c0dra = dra, *c1dra = &dra[I3000_RANKS_PER_CHANNEL / 2];
	unsigned char *c0drb = drb, *c1drb = &drb[I3000_RANKS_PER_CHANNEL];
	unsigned long mchbar;
	void __iomem *window;

	debugf0("MC: %s()\n", __func__);

	pci_read_config_dword(pdev, I3000_MCHBAR, (u32 *) & mchbar);
	mchbar &= I3000_MCHBAR_MASK;
	window = ioremap_nocache(mchbar, I3000_MMR_WINDOW_SIZE);
	if (!window) {
		printk(KERN_ERR "i3000: cannot map mmio space at 0x%lx\n",
			mchbar);
		return -ENODEV;
	}

	c0dra[0] = readb(window + I3000_C0DRA + 0);	/* ranks 0,1 */
	c0dra[1] = readb(window + I3000_C0DRA + 1);	/* ranks 2,3 */
	c1dra[0] = readb(window + I3000_C1DRA + 0);	/* ranks 0,1 */
	c1dra[1] = readb(window + I3000_C1DRA + 1);	/* ranks 2,3 */

	for (i = 0; i < I3000_RANKS_PER_CHANNEL; i++) {
		c0drb[i] = readb(window + I3000_C0DRB + i);
		c1drb[i] = readb(window + I3000_C1DRB + i);
	}

	iounmap(window);

	/* Figure out how many channels we have.
	 *
	 * If we have what the datasheet calls "asymmetric channels"
	 * (essentially the same as what was called "virtual single
	 * channel mode" in the i82875) then it's a single channel as
	 * far as EDAC is concerned.
	 */
	interleaved = i3000_is_interleaved(c0dra, c1dra, c0drb, c1drb);
	nr_channels = interleaved ? 2 : 1;
	mci = edac_mc_alloc(0, I3000_RANKS / nr_channels, nr_channels, 0);
	if (!mci)
		return -ENOMEM;

	debugf3("MC: %s(): init mci\n", __func__);

	mci->dev = &pdev->dev;
	mci->mtype_cap = MEM_FLAG_DDR2;

	mci->edac_ctl_cap = EDAC_FLAG_SECDED;
	mci->edac_cap = EDAC_FLAG_SECDED;

	mci->mod_name = EDAC_MOD_STR;
	mci->mod_ver = I3000_REVISION;
	mci->ctl_name = i3000_devs[dev_idx].ctl_name;
	mci->dev_name = pci_name(pdev);
	mci->edac_check = i3000_check;
	mci->ctl_page_to_phys = NULL;

	/*
	 * The dram rank boundary (DRB) reg values are boundary addresses
	 * for each DRAM rank with a granularity of 32MB.  DRB regs are
	 * cumulative; the last one will contain the total memory
	 * contained in all ranks.
	 *
	 * If we're in interleaved mode then we're only walking through
	 * the ranks of controller 0, so we double all the values we see.
	 */
	for (last_cumul_size = i = 0; i < mci->nr_csrows; i++) {
		u8 value;
		u32 cumul_size;
		struct csrow_info *csrow = &mci->csrows[i];

		value = drb[i];
		cumul_size = value << (I3000_DRB_SHIFT - PAGE_SHIFT);
		if (interleaved)
			cumul_size <<= 1;
		debugf3("MC: %s(): (%d) cumul_size 0x%x\n",
			__func__, i, cumul_size);
		if (cumul_size == last_cumul_size) {
			csrow->mtype = MEM_EMPTY;
			continue;
		}

		csrow->first_page = last_cumul_size;
		csrow->last_page = cumul_size - 1;
		csrow->nr_pages = cumul_size - last_cumul_size;
		last_cumul_size = cumul_size;
		csrow->grain = I3000_DEAP_GRAIN;
		csrow->mtype = MEM_DDR2;
		csrow->dtype = DEV_UNKNOWN;
		csrow->edac_mode = EDAC_UNKNOWN;
	}

	/* Clear any error bits.
	 * (Yes, we really clear bits by writing 1 to them.)
	 */
	pci_write_bits16(pdev, I3000_ERRSTS, I3000_ERRSTS_BITS,
			 I3000_ERRSTS_BITS);

	rc = -ENODEV;
	if (edac_mc_add_mc(mci)) {
		debugf3("MC: %s(): failed edac_mc_add_mc()\n", __func__);
		goto fail;
	}

	/* allocating generic PCI control info */
	i3000_pci = edac_pci_create_generic_ctl(&pdev->dev, EDAC_MOD_STR);
	if (!i3000_pci) {
		printk(KERN_WARNING
			"%s(): Unable to create PCI control\n",
			__func__);
		printk(KERN_WARNING
			"%s(): PCI error report via EDAC not setup\n",
			__func__);
	}

	/* get this far and it's successful */
	debugf3("MC: %s(): success\n", __func__);
	return 0;

      fail:
	if (mci)
		edac_mc_free(mci);

	return rc;
}

/* returns count (>= 0), or negative on error */
static int __devinit i3000_init_one(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	int rc;

	debugf0("MC: %s()\n", __func__);

	if (pci_enable_device(pdev) < 0)
		return -EIO;

	rc = i3000_probe1(pdev, ent->driver_data);
	if (mci_pdev == NULL)
		mci_pdev = pci_dev_get(pdev);

	return rc;
}

static void __devexit i3000_remove_one(struct pci_dev *pdev)
{
	struct mem_ctl_info *mci;

	debugf0("%s()\n", __func__);

	if (i3000_pci)
		edac_pci_release_generic_ctl(i3000_pci);

	if ((mci = edac_mc_del_mc(&pdev->dev)) == NULL)
		return;

	edac_mc_free(mci);
}

static const struct pci_device_id i3000_pci_tbl[] __devinitdata = {
	{
	 PCI_VEND_DEV(INTEL, 3000_HB), PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	 I3000},
	{
	 0,
	 }			/* 0 terminated list. */
};

MODULE_DEVICE_TABLE(pci, i3000_pci_tbl);

static struct pci_driver i3000_driver = {
	.name = EDAC_MOD_STR,
	.probe = i3000_init_one,
	.remove = __devexit_p(i3000_remove_one),
	.id_table = i3000_pci_tbl,
};

static int __init i3000_init(void)
{
	int pci_rc;

	debugf3("MC: %s()\n", __func__);
	pci_rc = pci_register_driver(&i3000_driver);
	if (pci_rc < 0)
		goto fail0;

	if (mci_pdev == NULL) {
		i3000_registered = 0;
		mci_pdev = pci_get_device(PCI_VENDOR_ID_INTEL,
					PCI_DEVICE_ID_INTEL_3000_HB, NULL);
		if (!mci_pdev) {
			debugf0("i3000 pci_get_device fail\n");
			pci_rc = -ENODEV;
			goto fail1;
		}

		pci_rc = i3000_init_one(mci_pdev, i3000_pci_tbl);
		if (pci_rc < 0) {
			debugf0("i3000 init fail\n");
			pci_rc = -ENODEV;
			goto fail1;
		}
	}

	return 0;

fail1:
	pci_unregister_driver(&i3000_driver);

fail0:
	if (mci_pdev)
		pci_dev_put(mci_pdev);

	return pci_rc;
}

static void __exit i3000_exit(void)
{
	debugf3("MC: %s()\n", __func__);

	pci_unregister_driver(&i3000_driver);
	if (!i3000_registered) {
		i3000_remove_one(mci_pdev);
		pci_dev_put(mci_pdev);
	}
}

module_init(i3000_init);
module_exit(i3000_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Akamai Technologies Arthur Ulfeldt/Jason Uhlenkott");
MODULE_DESCRIPTION("MC support for Intel 3000 memory hub controllers");

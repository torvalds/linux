/*
 * Intel X38 Memory Controller kernel module
 * Copyright (C) 2008 Cluster Computing, Inc.
 *
 * This file may be distributed under the terms of the
 * GNU General Public License.
 *
 * This file is based on i3200_edac.c
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/slab.h>
#include <linux/edac.h>
#include "edac_core.h"

#define X38_REVISION		"1.1"

#define EDAC_MOD_STR		"x38_edac"

#define PCI_DEVICE_ID_INTEL_X38_HB	0x29e0

#define X38_RANKS		8
#define X38_RANKS_PER_CHANNEL	4
#define X38_CHANNELS		2

/* Intel X38 register addresses - device 0 function 0 - DRAM Controller */

#define X38_MCHBAR_LOW	0x48	/* MCH Memory Mapped Register BAR */
#define X38_MCHBAR_HIGH	0x4c
#define X38_MCHBAR_MASK	0xfffffc000ULL	/* bits 35:14 */
#define X38_MMR_WINDOW_SIZE	16384

#define X38_TOM	0xa0	/* Top of Memory (16b)
				 *
				 * 15:10 reserved
				 *  9:0  total populated physical memory
				 */
#define X38_TOM_MASK	0x3ff	/* bits 9:0 */
#define X38_TOM_SHIFT 26	/* 64MiB grain */

#define X38_ERRSTS	0xc8	/* Error Status Register (16b)
				 *
				 * 15    reserved
				 * 14    Isochronous TBWRR Run Behind FIFO Full
				 *       (ITCV)
				 * 13    Isochronous TBWRR Run Behind FIFO Put
				 *       (ITSTV)
				 * 12    reserved
				 * 11    MCH Thermal Sensor Event
				 *       for SMI/SCI/SERR (GTSE)
				 * 10    reserved
				 *  9    LOCK to non-DRAM Memory Flag (LCKF)
				 *  8    reserved
				 *  7    DRAM Throttle Flag (DTF)
				 *  6:2  reserved
				 *  1    Multi-bit DRAM ECC Error Flag (DMERR)
				 *  0    Single-bit DRAM ECC Error Flag (DSERR)
				 */
#define X38_ERRSTS_UE		0x0002
#define X38_ERRSTS_CE		0x0001
#define X38_ERRSTS_BITS	(X38_ERRSTS_UE | X38_ERRSTS_CE)


/* Intel  MMIO register space - device 0 function 0 - MMR space */

#define X38_C0DRB	0x200	/* Channel 0 DRAM Rank Boundary (16b x 4)
				 *
				 * 15:10 reserved
				 *  9:0  Channel 0 DRAM Rank Boundary Address
				 */
#define X38_C1DRB	0x600	/* Channel 1 DRAM Rank Boundary (16b x 4) */
#define X38_DRB_MASK	0x3ff	/* bits 9:0 */
#define X38_DRB_SHIFT 26	/* 64MiB grain */

#define X38_C0ECCERRLOG 0x280	/* Channel 0 ECC Error Log (64b)
				 *
				 * 63:48 Error Column Address (ERRCOL)
				 * 47:32 Error Row Address (ERRROW)
				 * 31:29 Error Bank Address (ERRBANK)
				 * 28:27 Error Rank Address (ERRRANK)
				 * 26:24 reserved
				 * 23:16 Error Syndrome (ERRSYND)
				 * 15: 2 reserved
				 *    1  Multiple Bit Error Status (MERRSTS)
				 *    0  Correctable Error Status (CERRSTS)
				 */
#define X38_C1ECCERRLOG 0x680	/* Channel 1 ECC Error Log (64b) */
#define X38_ECCERRLOG_CE	0x1
#define X38_ECCERRLOG_UE	0x2
#define X38_ECCERRLOG_RANK_BITS	0x18000000
#define X38_ECCERRLOG_SYNDROME_BITS	0xff0000

#define X38_CAPID0 0xe0	/* see P.94 of spec for details */

static int x38_channel_num;

static int how_many_channel(struct pci_dev *pdev)
{
	unsigned char capid0_8b; /* 8th byte of CAPID0 */

	pci_read_config_byte(pdev, X38_CAPID0 + 8, &capid0_8b);
	if (capid0_8b & 0x20) {	/* check DCD: Dual Channel Disable */
		debugf0("In single channel mode.\n");
		x38_channel_num = 1;
	} else {
		debugf0("In dual channel mode.\n");
		x38_channel_num = 2;
	}

	return x38_channel_num;
}

static unsigned long eccerrlog_syndrome(u64 log)
{
	return (log & X38_ECCERRLOG_SYNDROME_BITS) >> 16;
}

static int eccerrlog_row(int channel, u64 log)
{
	return ((log & X38_ECCERRLOG_RANK_BITS) >> 27) |
		(channel * X38_RANKS_PER_CHANNEL);
}

enum x38_chips {
	X38 = 0,
};

struct x38_dev_info {
	const char *ctl_name;
};

struct x38_error_info {
	u16 errsts;
	u16 errsts2;
	u64 eccerrlog[X38_CHANNELS];
};

static const struct x38_dev_info x38_devs[] = {
	[X38] = {
		.ctl_name = "x38"},
};

static struct pci_dev *mci_pdev;
static int x38_registered = 1;


static void x38_clear_error_info(struct mem_ctl_info *mci)
{
	struct pci_dev *pdev;

	pdev = to_pci_dev(mci->dev);

	/*
	 * Clear any error bits.
	 * (Yes, we really clear bits by writing 1 to them.)
	 */
	pci_write_bits16(pdev, X38_ERRSTS, X38_ERRSTS_BITS,
			 X38_ERRSTS_BITS);
}

static u64 x38_readq(const void __iomem *addr)
{
	return readl(addr) | (((u64)readl(addr + 4)) << 32);
}

static void x38_get_and_clear_error_info(struct mem_ctl_info *mci,
				 struct x38_error_info *info)
{
	struct pci_dev *pdev;
	void __iomem *window = mci->pvt_info;

	pdev = to_pci_dev(mci->dev);

	/*
	 * This is a mess because there is no atomic way to read all the
	 * registers at once and the registers can transition from CE being
	 * overwritten by UE.
	 */
	pci_read_config_word(pdev, X38_ERRSTS, &info->errsts);
	if (!(info->errsts & X38_ERRSTS_BITS))
		return;

	info->eccerrlog[0] = x38_readq(window + X38_C0ECCERRLOG);
	if (x38_channel_num == 2)
		info->eccerrlog[1] = x38_readq(window + X38_C1ECCERRLOG);

	pci_read_config_word(pdev, X38_ERRSTS, &info->errsts2);

	/*
	 * If the error is the same for both reads then the first set
	 * of reads is valid.  If there is a change then there is a CE
	 * with no info and the second set of reads is valid and
	 * should be UE info.
	 */
	if ((info->errsts ^ info->errsts2) & X38_ERRSTS_BITS) {
		info->eccerrlog[0] = x38_readq(window + X38_C0ECCERRLOG);
		if (x38_channel_num == 2)
			info->eccerrlog[1] =
				x38_readq(window + X38_C1ECCERRLOG);
	}

	x38_clear_error_info(mci);
}

static void x38_process_error_info(struct mem_ctl_info *mci,
				struct x38_error_info *info)
{
	int channel;
	u64 log;

	if (!(info->errsts & X38_ERRSTS_BITS))
		return;

	if ((info->errsts ^ info->errsts2) & X38_ERRSTS_BITS) {
		edac_mc_handle_ce_no_info(mci, "UE overwrote CE");
		info->errsts = info->errsts2;
	}

	for (channel = 0; channel < x38_channel_num; channel++) {
		log = info->eccerrlog[channel];
		if (log & X38_ECCERRLOG_UE) {
			edac_mc_handle_ue(mci, 0, 0,
				eccerrlog_row(channel, log), "x38 UE");
		} else if (log & X38_ECCERRLOG_CE) {
			edac_mc_handle_ce(mci, 0, 0,
				eccerrlog_syndrome(log),
				eccerrlog_row(channel, log), 0, "x38 CE");
		}
	}
}

static void x38_check(struct mem_ctl_info *mci)
{
	struct x38_error_info info;

	debugf1("MC%d: %s()\n", mci->mc_idx, __func__);
	x38_get_and_clear_error_info(mci, &info);
	x38_process_error_info(mci, &info);
}


void __iomem *x38_map_mchbar(struct pci_dev *pdev)
{
	union {
		u64 mchbar;
		struct {
			u32 mchbar_low;
			u32 mchbar_high;
		};
	} u;
	void __iomem *window;

	pci_read_config_dword(pdev, X38_MCHBAR_LOW, &u.mchbar_low);
	pci_write_config_dword(pdev, X38_MCHBAR_LOW, u.mchbar_low | 0x1);
	pci_read_config_dword(pdev, X38_MCHBAR_HIGH, &u.mchbar_high);
	u.mchbar &= X38_MCHBAR_MASK;

	if (u.mchbar != (resource_size_t)u.mchbar) {
		printk(KERN_ERR
			"x38: mmio space beyond accessible range (0x%llx)\n",
			(unsigned long long)u.mchbar);
		return NULL;
	}

	window = ioremap_nocache(u.mchbar, X38_MMR_WINDOW_SIZE);
	if (!window)
		printk(KERN_ERR "x38: cannot map mmio space at 0x%llx\n",
			(unsigned long long)u.mchbar);

	return window;
}


static void x38_get_drbs(void __iomem *window,
			u16 drbs[X38_CHANNELS][X38_RANKS_PER_CHANNEL])
{
	int i;

	for (i = 0; i < X38_RANKS_PER_CHANNEL; i++) {
		drbs[0][i] = readw(window + X38_C0DRB + 2*i) & X38_DRB_MASK;
		drbs[1][i] = readw(window + X38_C1DRB + 2*i) & X38_DRB_MASK;
	}
}

static bool x38_is_stacked(struct pci_dev *pdev,
			u16 drbs[X38_CHANNELS][X38_RANKS_PER_CHANNEL])
{
	u16 tom;

	pci_read_config_word(pdev, X38_TOM, &tom);
	tom &= X38_TOM_MASK;

	return drbs[X38_CHANNELS - 1][X38_RANKS_PER_CHANNEL - 1] == tom;
}

static unsigned long drb_to_nr_pages(
			u16 drbs[X38_CHANNELS][X38_RANKS_PER_CHANNEL],
			bool stacked, int channel, int rank)
{
	int n;

	n = drbs[channel][rank];
	if (rank > 0)
		n -= drbs[channel][rank - 1];
	if (stacked && (channel == 1) && drbs[channel][rank] ==
				drbs[channel][X38_RANKS_PER_CHANNEL - 1]) {
		n -= drbs[0][X38_RANKS_PER_CHANNEL - 1];
	}

	n <<= (X38_DRB_SHIFT - PAGE_SHIFT);
	return n;
}

static int x38_probe1(struct pci_dev *pdev, int dev_idx)
{
	int rc;
	int i;
	struct mem_ctl_info *mci = NULL;
	unsigned long last_page;
	u16 drbs[X38_CHANNELS][X38_RANKS_PER_CHANNEL];
	bool stacked;
	void __iomem *window;

	debugf0("MC: %s()\n", __func__);

	window = x38_map_mchbar(pdev);
	if (!window)
		return -ENODEV;

	x38_get_drbs(window, drbs);

	how_many_channel(pdev);

	/* FIXME: unconventional pvt_info usage */
	mci = edac_mc_alloc(0, X38_RANKS, x38_channel_num, 0);
	if (!mci)
		return -ENOMEM;

	debugf3("MC: %s(): init mci\n", __func__);

	mci->dev = &pdev->dev;
	mci->mtype_cap = MEM_FLAG_DDR2;

	mci->edac_ctl_cap = EDAC_FLAG_SECDED;
	mci->edac_cap = EDAC_FLAG_SECDED;

	mci->mod_name = EDAC_MOD_STR;
	mci->mod_ver = X38_REVISION;
	mci->ctl_name = x38_devs[dev_idx].ctl_name;
	mci->dev_name = pci_name(pdev);
	mci->edac_check = x38_check;
	mci->ctl_page_to_phys = NULL;
	mci->pvt_info = window;

	stacked = x38_is_stacked(pdev, drbs);

	/*
	 * The dram rank boundary (DRB) reg values are boundary addresses
	 * for each DRAM rank with a granularity of 64MB.  DRB regs are
	 * cumulative; the last one will contain the total memory
	 * contained in all ranks.
	 */
	last_page = -1UL;
	for (i = 0; i < mci->nr_csrows; i++) {
		unsigned long nr_pages;
		struct csrow_info *csrow = &mci->csrows[i];

		nr_pages = drb_to_nr_pages(drbs, stacked,
			i / X38_RANKS_PER_CHANNEL,
			i % X38_RANKS_PER_CHANNEL);

		if (nr_pages == 0) {
			csrow->mtype = MEM_EMPTY;
			continue;
		}

		csrow->first_page = last_page + 1;
		last_page += nr_pages;
		csrow->last_page = last_page;
		csrow->nr_pages = nr_pages;

		csrow->grain = nr_pages << PAGE_SHIFT;
		csrow->mtype = MEM_DDR2;
		csrow->dtype = DEV_UNKNOWN;
		csrow->edac_mode = EDAC_UNKNOWN;
	}

	x38_clear_error_info(mci);

	rc = -ENODEV;
	if (edac_mc_add_mc(mci)) {
		debugf3("MC: %s(): failed edac_mc_add_mc()\n", __func__);
		goto fail;
	}

	/* get this far and it's successful */
	debugf3("MC: %s(): success\n", __func__);
	return 0;

fail:
	iounmap(window);
	if (mci)
		edac_mc_free(mci);

	return rc;
}

static int __devinit x38_init_one(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	int rc;

	debugf0("MC: %s()\n", __func__);

	if (pci_enable_device(pdev) < 0)
		return -EIO;

	rc = x38_probe1(pdev, ent->driver_data);
	if (!mci_pdev)
		mci_pdev = pci_dev_get(pdev);

	return rc;
}

static void __devexit x38_remove_one(struct pci_dev *pdev)
{
	struct mem_ctl_info *mci;

	debugf0("%s()\n", __func__);

	mci = edac_mc_del_mc(&pdev->dev);
	if (!mci)
		return;

	iounmap(mci->pvt_info);

	edac_mc_free(mci);
}

static const struct pci_device_id x38_pci_tbl[] __devinitdata = {
	{
	 PCI_VEND_DEV(INTEL, X38_HB), PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	 X38},
	{
	 0,
	 }			/* 0 terminated list. */
};

MODULE_DEVICE_TABLE(pci, x38_pci_tbl);

static struct pci_driver x38_driver = {
	.name = EDAC_MOD_STR,
	.probe = x38_init_one,
	.remove = __devexit_p(x38_remove_one),
	.id_table = x38_pci_tbl,
};

static int __init x38_init(void)
{
	int pci_rc;

	debugf3("MC: %s()\n", __func__);

	/* Ensure that the OPSTATE is set correctly for POLL or NMI */
	opstate_init();

	pci_rc = pci_register_driver(&x38_driver);
	if (pci_rc < 0)
		goto fail0;

	if (!mci_pdev) {
		x38_registered = 0;
		mci_pdev = pci_get_device(PCI_VENDOR_ID_INTEL,
					PCI_DEVICE_ID_INTEL_X38_HB, NULL);
		if (!mci_pdev) {
			debugf0("x38 pci_get_device fail\n");
			pci_rc = -ENODEV;
			goto fail1;
		}

		pci_rc = x38_init_one(mci_pdev, x38_pci_tbl);
		if (pci_rc < 0) {
			debugf0("x38 init fail\n");
			pci_rc = -ENODEV;
			goto fail1;
		}
	}

	return 0;

fail1:
	pci_unregister_driver(&x38_driver);

fail0:
	if (mci_pdev)
		pci_dev_put(mci_pdev);

	return pci_rc;
}

static void __exit x38_exit(void)
{
	debugf3("MC: %s()\n", __func__);

	pci_unregister_driver(&x38_driver);
	if (!x38_registered) {
		x38_remove_one(mci_pdev);
		pci_dev_put(mci_pdev);
	}
}

module_init(x38_init);
module_exit(x38_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cluster Computing, Inc. Hitoshi Mitake");
MODULE_DESCRIPTION("MC support for Intel X38 memory hub controllers");

module_param(edac_op_state, int, 0444);
MODULE_PARM_DESC(edac_op_state, "EDAC Error Reporting state: 0=Poll,1=NMI");

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel E3-1200
 * Copyright (C) 2014 Jason Baron <jbaron@akamai.com>
 *
 * Support for the E3-1200 processor family. Heavily based on previous
 * Intel EDAC drivers.
 *
 * Since the DRAM controller is on the cpu chip, we can use its PCI device
 * id to identify these processors.
 *
 * PCI DRAM controller device ids (Taken from The PCI ID Repository - http://pci-ids.ucw.cz/)
 *
 * 0108: Xeon E3-1200 Processor Family DRAM Controller
 * 010c: Xeon E3-1200/2nd Generation Core Processor Family DRAM Controller
 * 0150: Xeon E3-1200 v2/3rd Gen Core processor DRAM Controller
 * 0158: Xeon E3-1200 v2/Ivy Bridge DRAM Controller
 * 015c: Xeon E3-1200 v2/3rd Gen Core processor DRAM Controller
 * 0c04: Xeon E3-1200 v3/4th Gen Core Processor DRAM Controller
 * 0c08: Xeon E3-1200 v3 Processor DRAM Controller
 * 1918: Xeon E3-1200 v5 Skylake Host Bridge/DRAM Registers
 * 5918: Xeon E3-1200 Xeon E3-1200 v6/7th Gen Core Processor Host Bridge/DRAM Registers
 *
 * Based on Intel specification:
 * http://www.intel.com/content/dam/www/public/us/en/documents/datasheets/xeon-e3-1200v3-vol-2-datasheet.pdf
 * http://www.intel.com/content/www/us/en/processors/xeon/xeon-e3-1200-family-vol-2-datasheet.html
 * http://www.intel.com/content/www/us/en/processors/core/7th-gen-core-family-mobile-h-processor-lines-datasheet-vol-2.html
 *
 * According to the above datasheet (p.16):
 * "
 * 6. Software must not access B0/D0/F0 32-bit memory-mapped registers with
 * requests that cross a DW boundary.
 * "
 *
 * Thus, we make use of the explicit: lo_hi_readq(), which breaks the readq into
 * 2 readl() calls. This restriction may be lifted in subsequent chip releases,
 * but lo_hi_readq() ensures that we are safe across all e3-1200 processors.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/edac.h>

#include <linux/io-64-nonatomic-lo-hi.h>
#include "edac_module.h"

#define EDAC_MOD_STR "ie31200_edac"

#define ie31200_printk(level, fmt, arg...) \
	edac_printk(level, "ie31200", fmt, ##arg)

#define PCI_DEVICE_ID_INTEL_IE31200_HB_1 0x0108
#define PCI_DEVICE_ID_INTEL_IE31200_HB_2 0x010c
#define PCI_DEVICE_ID_INTEL_IE31200_HB_3 0x0150
#define PCI_DEVICE_ID_INTEL_IE31200_HB_4 0x0158
#define PCI_DEVICE_ID_INTEL_IE31200_HB_5 0x015c
#define PCI_DEVICE_ID_INTEL_IE31200_HB_6 0x0c04
#define PCI_DEVICE_ID_INTEL_IE31200_HB_7 0x0c08
#define PCI_DEVICE_ID_INTEL_IE31200_HB_8 0x1918
#define PCI_DEVICE_ID_INTEL_IE31200_HB_9 0x5918

#define IE31200_DIMMS			4
#define IE31200_RANKS			8
#define IE31200_RANKS_PER_CHANNEL	4
#define IE31200_DIMMS_PER_CHANNEL	2
#define IE31200_CHANNELS		2

/* Intel IE31200 register addresses - device 0 function 0 - DRAM Controller */
#define IE31200_MCHBAR_LOW		0x48
#define IE31200_MCHBAR_HIGH		0x4c
#define IE31200_MCHBAR_MASK		GENMASK_ULL(38, 15)
#define IE31200_MMR_WINDOW_SIZE		BIT(15)

/*
 * Error Status Register (16b)
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
#define IE31200_ERRSTS			0xc8
#define IE31200_ERRSTS_UE		BIT(1)
#define IE31200_ERRSTS_CE		BIT(0)
#define IE31200_ERRSTS_BITS		(IE31200_ERRSTS_UE | IE31200_ERRSTS_CE)

/*
 * Channel 0 ECC Error Log (64b)
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

#define IE31200_C0ECCERRLOG			0x40c8
#define IE31200_C1ECCERRLOG			0x44c8
#define IE31200_C0ECCERRLOG_SKL			0x4048
#define IE31200_C1ECCERRLOG_SKL			0x4448
#define IE31200_ECCERRLOG_CE			BIT(0)
#define IE31200_ECCERRLOG_UE			BIT(1)
#define IE31200_ECCERRLOG_RANK_BITS		GENMASK_ULL(28, 27)
#define IE31200_ECCERRLOG_RANK_SHIFT		27
#define IE31200_ECCERRLOG_SYNDROME_BITS		GENMASK_ULL(23, 16)
#define IE31200_ECCERRLOG_SYNDROME_SHIFT	16

#define IE31200_ECCERRLOG_SYNDROME(log)		   \
	((log & IE31200_ECCERRLOG_SYNDROME_BITS) >> \
	 IE31200_ECCERRLOG_SYNDROME_SHIFT)

#define IE31200_CAPID0			0xe4
#define IE31200_CAPID0_PDCD		BIT(4)
#define IE31200_CAPID0_DDPCD		BIT(6)
#define IE31200_CAPID0_ECC		BIT(1)

#define IE31200_MAD_DIMM_0_OFFSET		0x5004
#define IE31200_MAD_DIMM_0_OFFSET_SKL		0x500C
#define IE31200_MAD_DIMM_SIZE			GENMASK_ULL(7, 0)
#define IE31200_MAD_DIMM_A_RANK			BIT(17)
#define IE31200_MAD_DIMM_A_RANK_SHIFT		17
#define IE31200_MAD_DIMM_A_RANK_SKL		BIT(10)
#define IE31200_MAD_DIMM_A_RANK_SKL_SHIFT	10
#define IE31200_MAD_DIMM_A_WIDTH		BIT(19)
#define IE31200_MAD_DIMM_A_WIDTH_SHIFT		19
#define IE31200_MAD_DIMM_A_WIDTH_SKL		GENMASK_ULL(9, 8)
#define IE31200_MAD_DIMM_A_WIDTH_SKL_SHIFT	8

/* Skylake reports 1GB increments, everything else is 256MB */
#define IE31200_PAGES(n, skl)	\
	(n << (28 + (2 * skl) - PAGE_SHIFT))

static int nr_channels;

struct ie31200_priv {
	void __iomem *window;
	void __iomem *c0errlog;
	void __iomem *c1errlog;
};

enum ie31200_chips {
	IE31200 = 0,
};

struct ie31200_dev_info {
	const char *ctl_name;
};

struct ie31200_error_info {
	u16 errsts;
	u16 errsts2;
	u64 eccerrlog[IE31200_CHANNELS];
};

static const struct ie31200_dev_info ie31200_devs[] = {
	[IE31200] = {
		.ctl_name = "IE31200"
	},
};

struct dimm_data {
	u8 size; /* in multiples of 256MB, except Skylake is 1GB */
	u8 dual_rank : 1,
	   x16_width : 2; /* 0 means x8 width */
};

static int how_many_channels(struct pci_dev *pdev)
{
	int n_channels;
	unsigned char capid0_2b; /* 2nd byte of CAPID0 */

	pci_read_config_byte(pdev, IE31200_CAPID0 + 1, &capid0_2b);

	/* check PDCD: Dual Channel Disable */
	if (capid0_2b & IE31200_CAPID0_PDCD) {
		edac_dbg(0, "In single channel mode\n");
		n_channels = 1;
	} else {
		edac_dbg(0, "In dual channel mode\n");
		n_channels = 2;
	}

	/* check DDPCD - check if both channels are filled */
	if (capid0_2b & IE31200_CAPID0_DDPCD)
		edac_dbg(0, "2 DIMMS per channel disabled\n");
	else
		edac_dbg(0, "2 DIMMS per channel enabled\n");

	return n_channels;
}

static bool ecc_capable(struct pci_dev *pdev)
{
	unsigned char capid0_4b; /* 4th byte of CAPID0 */

	pci_read_config_byte(pdev, IE31200_CAPID0 + 3, &capid0_4b);
	if (capid0_4b & IE31200_CAPID0_ECC)
		return false;
	return true;
}

static int eccerrlog_row(u64 log)
{
	return ((log & IE31200_ECCERRLOG_RANK_BITS) >>
				IE31200_ECCERRLOG_RANK_SHIFT);
}

static void ie31200_clear_error_info(struct mem_ctl_info *mci)
{
	/*
	 * Clear any error bits.
	 * (Yes, we really clear bits by writing 1 to them.)
	 */
	pci_write_bits16(to_pci_dev(mci->pdev), IE31200_ERRSTS,
			 IE31200_ERRSTS_BITS, IE31200_ERRSTS_BITS);
}

static void ie31200_get_and_clear_error_info(struct mem_ctl_info *mci,
					     struct ie31200_error_info *info)
{
	struct pci_dev *pdev;
	struct ie31200_priv *priv = mci->pvt_info;

	pdev = to_pci_dev(mci->pdev);

	/*
	 * This is a mess because there is no atomic way to read all the
	 * registers at once and the registers can transition from CE being
	 * overwritten by UE.
	 */
	pci_read_config_word(pdev, IE31200_ERRSTS, &info->errsts);
	if (!(info->errsts & IE31200_ERRSTS_BITS))
		return;

	info->eccerrlog[0] = lo_hi_readq(priv->c0errlog);
	if (nr_channels == 2)
		info->eccerrlog[1] = lo_hi_readq(priv->c1errlog);

	pci_read_config_word(pdev, IE31200_ERRSTS, &info->errsts2);

	/*
	 * If the error is the same for both reads then the first set
	 * of reads is valid.  If there is a change then there is a CE
	 * with no info and the second set of reads is valid and
	 * should be UE info.
	 */
	if ((info->errsts ^ info->errsts2) & IE31200_ERRSTS_BITS) {
		info->eccerrlog[0] = lo_hi_readq(priv->c0errlog);
		if (nr_channels == 2)
			info->eccerrlog[1] =
				lo_hi_readq(priv->c1errlog);
	}

	ie31200_clear_error_info(mci);
}

static void ie31200_process_error_info(struct mem_ctl_info *mci,
				       struct ie31200_error_info *info)
{
	int channel;
	u64 log;

	if (!(info->errsts & IE31200_ERRSTS_BITS))
		return;

	if ((info->errsts ^ info->errsts2) & IE31200_ERRSTS_BITS) {
		edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci, 1, 0, 0, 0,
				     -1, -1, -1, "UE overwrote CE", "");
		info->errsts = info->errsts2;
	}

	for (channel = 0; channel < nr_channels; channel++) {
		log = info->eccerrlog[channel];
		if (log & IE31200_ECCERRLOG_UE) {
			edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci, 1,
					     0, 0, 0,
					     eccerrlog_row(log),
					     channel, -1,
					     "ie31200 UE", "");
		} else if (log & IE31200_ECCERRLOG_CE) {
			edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci, 1,
					     0, 0,
					     IE31200_ECCERRLOG_SYNDROME(log),
					     eccerrlog_row(log),
					     channel, -1,
					     "ie31200 CE", "");
		}
	}
}

static void ie31200_check(struct mem_ctl_info *mci)
{
	struct ie31200_error_info info;

	edac_dbg(1, "MC%d\n", mci->mc_idx);
	ie31200_get_and_clear_error_info(mci, &info);
	ie31200_process_error_info(mci, &info);
}

static void __iomem *ie31200_map_mchbar(struct pci_dev *pdev)
{
	union {
		u64 mchbar;
		struct {
			u32 mchbar_low;
			u32 mchbar_high;
		};
	} u;
	void __iomem *window;

	pci_read_config_dword(pdev, IE31200_MCHBAR_LOW, &u.mchbar_low);
	pci_read_config_dword(pdev, IE31200_MCHBAR_HIGH, &u.mchbar_high);
	u.mchbar &= IE31200_MCHBAR_MASK;

	if (u.mchbar != (resource_size_t)u.mchbar) {
		ie31200_printk(KERN_ERR, "mmio space beyond accessible range (0x%llx)\n",
			       (unsigned long long)u.mchbar);
		return NULL;
	}

	window = ioremap_nocache(u.mchbar, IE31200_MMR_WINDOW_SIZE);
	if (!window)
		ie31200_printk(KERN_ERR, "Cannot map mmio space at 0x%llx\n",
			       (unsigned long long)u.mchbar);

	return window;
}

static void __skl_populate_dimm_info(struct dimm_data *dd, u32 addr_decode,
				     int chan)
{
	dd->size = (addr_decode >> (chan << 4)) & IE31200_MAD_DIMM_SIZE;
	dd->dual_rank = (addr_decode & (IE31200_MAD_DIMM_A_RANK_SKL << (chan << 4))) ? 1 : 0;
	dd->x16_width = ((addr_decode & (IE31200_MAD_DIMM_A_WIDTH_SKL << (chan << 4))) >>
				(IE31200_MAD_DIMM_A_WIDTH_SKL_SHIFT + (chan << 4)));
}

static void __populate_dimm_info(struct dimm_data *dd, u32 addr_decode,
				 int chan)
{
	dd->size = (addr_decode >> (chan << 3)) & IE31200_MAD_DIMM_SIZE;
	dd->dual_rank = (addr_decode & (IE31200_MAD_DIMM_A_RANK << chan)) ? 1 : 0;
	dd->x16_width = (addr_decode & (IE31200_MAD_DIMM_A_WIDTH << chan)) ? 1 : 0;
}

static void populate_dimm_info(struct dimm_data *dd, u32 addr_decode, int chan,
			       bool skl)
{
	if (skl)
		__skl_populate_dimm_info(dd, addr_decode, chan);
	else
		__populate_dimm_info(dd, addr_decode, chan);
}


static int ie31200_probe1(struct pci_dev *pdev, int dev_idx)
{
	int i, j, ret;
	struct mem_ctl_info *mci = NULL;
	struct edac_mc_layer layers[2];
	struct dimm_data dimm_info[IE31200_CHANNELS][IE31200_DIMMS_PER_CHANNEL];
	void __iomem *window;
	struct ie31200_priv *priv;
	u32 addr_decode, mad_offset;

	/*
	 * Kaby Lake seems to work like Skylake. Please re-visit this logic
	 * when adding new CPU support.
	 */
	bool skl = (pdev->device >= PCI_DEVICE_ID_INTEL_IE31200_HB_8);

	edac_dbg(0, "MC:\n");

	if (!ecc_capable(pdev)) {
		ie31200_printk(KERN_INFO, "No ECC support\n");
		return -ENODEV;
	}

	nr_channels = how_many_channels(pdev);
	layers[0].type = EDAC_MC_LAYER_CHIP_SELECT;
	layers[0].size = IE31200_DIMMS;
	layers[0].is_virt_csrow = true;
	layers[1].type = EDAC_MC_LAYER_CHANNEL;
	layers[1].size = nr_channels;
	layers[1].is_virt_csrow = false;
	mci = edac_mc_alloc(0, ARRAY_SIZE(layers), layers,
			    sizeof(struct ie31200_priv));
	if (!mci)
		return -ENOMEM;

	window = ie31200_map_mchbar(pdev);
	if (!window) {
		ret = -ENODEV;
		goto fail_free;
	}

	edac_dbg(3, "MC: init mci\n");
	mci->pdev = &pdev->dev;
	if (skl)
		mci->mtype_cap = MEM_FLAG_DDR4;
	else
		mci->mtype_cap = MEM_FLAG_DDR3;
	mci->edac_ctl_cap = EDAC_FLAG_SECDED;
	mci->edac_cap = EDAC_FLAG_SECDED;
	mci->mod_name = EDAC_MOD_STR;
	mci->ctl_name = ie31200_devs[dev_idx].ctl_name;
	mci->dev_name = pci_name(pdev);
	mci->edac_check = ie31200_check;
	mci->ctl_page_to_phys = NULL;
	priv = mci->pvt_info;
	priv->window = window;
	if (skl) {
		priv->c0errlog = window + IE31200_C0ECCERRLOG_SKL;
		priv->c1errlog = window + IE31200_C1ECCERRLOG_SKL;
		mad_offset = IE31200_MAD_DIMM_0_OFFSET_SKL;
	} else {
		priv->c0errlog = window + IE31200_C0ECCERRLOG;
		priv->c1errlog = window + IE31200_C1ECCERRLOG;
		mad_offset = IE31200_MAD_DIMM_0_OFFSET;
	}

	/* populate DIMM info */
	for (i = 0; i < IE31200_CHANNELS; i++) {
		addr_decode = readl(window + mad_offset +
					(i * 4));
		edac_dbg(0, "addr_decode: 0x%x\n", addr_decode);
		for (j = 0; j < IE31200_DIMMS_PER_CHANNEL; j++) {
			populate_dimm_info(&dimm_info[i][j], addr_decode, j,
					   skl);
			edac_dbg(0, "size: 0x%x, rank: %d, width: %d\n",
				 dimm_info[i][j].size,
				 dimm_info[i][j].dual_rank,
				 dimm_info[i][j].x16_width);
		}
	}

	/*
	 * The dram rank boundary (DRB) reg values are boundary addresses
	 * for each DRAM rank with a granularity of 64MB.  DRB regs are
	 * cumulative; the last one will contain the total memory
	 * contained in all ranks.
	 */
	for (i = 0; i < IE31200_DIMMS_PER_CHANNEL; i++) {
		for (j = 0; j < IE31200_CHANNELS; j++) {
			struct dimm_info *dimm;
			unsigned long nr_pages;

			nr_pages = IE31200_PAGES(dimm_info[j][i].size, skl);
			if (nr_pages == 0)
				continue;

			if (dimm_info[j][i].dual_rank) {
				nr_pages = nr_pages / 2;
				dimm = EDAC_DIMM_PTR(mci->layers, mci->dimms,
						     mci->n_layers, (i * 2) + 1,
						     j, 0);
				dimm->nr_pages = nr_pages;
				edac_dbg(0, "set nr pages: 0x%lx\n", nr_pages);
				dimm->grain = 8; /* just a guess */
				if (skl)
					dimm->mtype = MEM_DDR4;
				else
					dimm->mtype = MEM_DDR3;
				dimm->dtype = DEV_UNKNOWN;
				dimm->edac_mode = EDAC_UNKNOWN;
			}
			dimm = EDAC_DIMM_PTR(mci->layers, mci->dimms,
					     mci->n_layers, i * 2, j, 0);
			dimm->nr_pages = nr_pages;
			edac_dbg(0, "set nr pages: 0x%lx\n", nr_pages);
			dimm->grain = 8; /* same guess */
			if (skl)
				dimm->mtype = MEM_DDR4;
			else
				dimm->mtype = MEM_DDR3;
			dimm->dtype = DEV_UNKNOWN;
			dimm->edac_mode = EDAC_UNKNOWN;
		}
	}

	ie31200_clear_error_info(mci);

	if (edac_mc_add_mc(mci)) {
		edac_dbg(3, "MC: failed edac_mc_add_mc()\n");
		ret = -ENODEV;
		goto fail_unmap;
	}

	/* get this far and it's successful */
	edac_dbg(3, "MC: success\n");
	return 0;

fail_unmap:
	iounmap(window);

fail_free:
	edac_mc_free(mci);

	return ret;
}

static int ie31200_init_one(struct pci_dev *pdev,
			    const struct pci_device_id *ent)
{
	edac_dbg(0, "MC:\n");

	if (pci_enable_device(pdev) < 0)
		return -EIO;

	return ie31200_probe1(pdev, ent->driver_data);
}

static void ie31200_remove_one(struct pci_dev *pdev)
{
	struct mem_ctl_info *mci;
	struct ie31200_priv *priv;

	edac_dbg(0, "\n");
	mci = edac_mc_del_mc(&pdev->dev);
	if (!mci)
		return;
	priv = mci->pvt_info;
	iounmap(priv->window);
	edac_mc_free(mci);
}

static const struct pci_device_id ie31200_pci_tbl[] = {
	{
		PCI_VEND_DEV(INTEL, IE31200_HB_1), PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		IE31200},
	{
		PCI_VEND_DEV(INTEL, IE31200_HB_2), PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		IE31200},
	{
		PCI_VEND_DEV(INTEL, IE31200_HB_3), PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		IE31200},
	{
		PCI_VEND_DEV(INTEL, IE31200_HB_4), PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		IE31200},
	{
		PCI_VEND_DEV(INTEL, IE31200_HB_5), PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		IE31200},
	{
		PCI_VEND_DEV(INTEL, IE31200_HB_6), PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		IE31200},
	{
		PCI_VEND_DEV(INTEL, IE31200_HB_7), PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		IE31200},
	{
		PCI_VEND_DEV(INTEL, IE31200_HB_8), PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		IE31200},
	{
		PCI_VEND_DEV(INTEL, IE31200_HB_9), PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		IE31200},
	{
		0,
	}            /* 0 terminated list. */
};
MODULE_DEVICE_TABLE(pci, ie31200_pci_tbl);

static struct pci_driver ie31200_driver = {
	.name = EDAC_MOD_STR,
	.probe = ie31200_init_one,
	.remove = ie31200_remove_one,
	.id_table = ie31200_pci_tbl,
};

static int __init ie31200_init(void)
{
	edac_dbg(3, "MC:\n");
	/* Ensure that the OPSTATE is set correctly for POLL or NMI */
	opstate_init();

	return pci_register_driver(&ie31200_driver);
}

static void __exit ie31200_exit(void)
{
	edac_dbg(3, "MC:\n");
	pci_unregister_driver(&ie31200_driver);
}

module_init(ie31200_init);
module_exit(ie31200_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jason Baron <jbaron@akamai.com>");
MODULE_DESCRIPTION("MC support for Intel Processor E31200 memory hub controllers");

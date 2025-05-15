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
 * PCI DRAM controller device ids (Taken from The PCI ID Repository - https://pci-ids.ucw.cz/)
 *
 * 0108: Xeon E3-1200 Processor Family DRAM Controller
 * 010c: Xeon E3-1200/2nd Generation Core Processor Family DRAM Controller
 * 0150: Xeon E3-1200 v2/3rd Gen Core processor DRAM Controller
 * 0158: Xeon E3-1200 v2/Ivy Bridge DRAM Controller
 * 015c: Xeon E3-1200 v2/3rd Gen Core processor DRAM Controller
 * 0c04: Xeon E3-1200 v3/4th Gen Core Processor DRAM Controller
 * 0c08: Xeon E3-1200 v3 Processor DRAM Controller
 * 1918: Xeon E3-1200 v5 Skylake Host Bridge/DRAM Registers
 * 590f: Xeon E3-1200 v6/7th Gen Core Processor Host Bridge/DRAM Registers
 * 5918: Xeon E3-1200 v6/7th Gen Core Processor Host Bridge/DRAM Registers
 * 190f: 6th Gen Core Dual-Core Processor Host Bridge/DRAM Registers
 * 191f: 6th Gen Core Quad-Core Processor Host Bridge/DRAM Registers
 * 3e..: 8th/9th Gen Core Processor Host Bridge/DRAM Registers
 *
 * Based on Intel specification:
 * https://www.intel.com/content/dam/www/public/us/en/documents/datasheets/xeon-e3-1200v3-vol-2-datasheet.pdf
 * http://www.intel.com/content/www/us/en/processors/xeon/xeon-e3-1200-family-vol-2-datasheet.html
 * https://www.intel.com/content/dam/www/public/us/en/documents/datasheets/desktop-6th-gen-core-family-datasheet-vol-2.pdf
 * https://www.intel.com/content/dam/www/public/us/en/documents/datasheets/xeon-e3-1200v6-vol-2-datasheet.pdf
 * https://www.intel.com/content/www/us/en/processors/core/7th-gen-core-family-mobile-h-processor-lines-datasheet-vol-2.html
 * https://www.intel.com/content/www/us/en/products/docs/processors/core/8th-gen-core-family-datasheet-vol-2.html
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
#include <asm/mce.h>
#include "edac_module.h"

#define EDAC_MOD_STR "ie31200_edac"

#define ie31200_printk(level, fmt, arg...) \
	edac_printk(level, "ie31200", fmt, ##arg)

#define PCI_DEVICE_ID_INTEL_IE31200_HB_1  0x0108
#define PCI_DEVICE_ID_INTEL_IE31200_HB_2  0x010c
#define PCI_DEVICE_ID_INTEL_IE31200_HB_3  0x0150
#define PCI_DEVICE_ID_INTEL_IE31200_HB_4  0x0158
#define PCI_DEVICE_ID_INTEL_IE31200_HB_5  0x015c
#define PCI_DEVICE_ID_INTEL_IE31200_HB_6  0x0c04
#define PCI_DEVICE_ID_INTEL_IE31200_HB_7  0x0c08
#define PCI_DEVICE_ID_INTEL_IE31200_HB_8  0x190F
#define PCI_DEVICE_ID_INTEL_IE31200_HB_9  0x1918
#define PCI_DEVICE_ID_INTEL_IE31200_HB_10 0x191F
#define PCI_DEVICE_ID_INTEL_IE31200_HB_11 0x590f
#define PCI_DEVICE_ID_INTEL_IE31200_HB_12 0x5918

/* Coffee Lake-S */
#define PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_MASK 0x3e00
#define PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_1    0x3e0f
#define PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_2    0x3e18
#define PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_3    0x3e1f
#define PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_4    0x3e30
#define PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_5    0x3e31
#define PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_6    0x3e32
#define PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_7    0x3e33
#define PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_8    0x3ec2
#define PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_9    0x3ec6
#define PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_10   0x3eca

/* Raptor Lake-S */
#define PCI_DEVICE_ID_INTEL_IE31200_RPL_S_1	0xa703
#define PCI_DEVICE_ID_INTEL_IE31200_RPL_S_2	0x4640
#define PCI_DEVICE_ID_INTEL_IE31200_RPL_S_3	0x4630

#define IE31200_RANKS_PER_CHANNEL	8
#define IE31200_DIMMS_PER_CHANNEL	2
#define IE31200_CHANNELS		2
#define IE31200_IMC_NUM			2

/* Intel IE31200 register addresses - device 0 function 0 - DRAM Controller */
#define IE31200_MCHBAR_LOW		0x48
#define IE31200_MCHBAR_HIGH		0x4c

/*
 * Error Status Register (16b)
 *
 *  1    Multi-bit DRAM ECC Error Flag (DMERR)
 *  0    Single-bit DRAM ECC Error Flag (DSERR)
 */
#define IE31200_ERRSTS			0xc8
#define IE31200_ERRSTS_UE		BIT(1)
#define IE31200_ERRSTS_CE		BIT(0)
#define IE31200_ERRSTS_BITS		(IE31200_ERRSTS_UE | IE31200_ERRSTS_CE)

#define IE31200_CAPID0			0xe4
#define IE31200_CAPID0_PDCD		BIT(4)
#define IE31200_CAPID0_DDPCD		BIT(6)
#define IE31200_CAPID0_ECC		BIT(1)

/* Non-constant mask variant of FIELD_GET() */
#define field_get(_mask, _reg)  (((_reg) & (_mask)) >> (ffs(_mask) - 1))

static int nr_channels;
static struct pci_dev *mci_pdev;
static int ie31200_registered = 1;

struct res_config {
	enum mem_type mtype;
	bool cmci;
	int imc_num;
	/* Host MMIO configuration register */
	u64 reg_mchbar_mask;
	u64 reg_mchbar_window_size;
	/* ECC error log register */
	u64 reg_eccerrlog_offset[IE31200_CHANNELS];
	u64 reg_eccerrlog_ce_mask;
	u64 reg_eccerrlog_ce_ovfl_mask;
	u64 reg_eccerrlog_ue_mask;
	u64 reg_eccerrlog_ue_ovfl_mask;
	u64 reg_eccerrlog_rank_mask;
	u64 reg_eccerrlog_syndrome_mask;
	/* MSR to clear ECC error log register */
	u32 msr_clear_eccerrlog_offset;
	/* DIMM characteristics register */
	u64 reg_mad_dimm_size_granularity;
	u64 reg_mad_dimm_offset[IE31200_CHANNELS];
	u32 reg_mad_dimm_size_mask[IE31200_DIMMS_PER_CHANNEL];
	u32 reg_mad_dimm_rank_mask[IE31200_DIMMS_PER_CHANNEL];
	u32 reg_mad_dimm_width_mask[IE31200_DIMMS_PER_CHANNEL];
};

struct ie31200_priv {
	void __iomem *window;
	void __iomem *c0errlog;
	void __iomem *c1errlog;
	struct res_config *cfg;
	struct mem_ctl_info *mci;
	struct pci_dev *pdev;
	struct device dev;
};

static struct ie31200_pvt {
	struct ie31200_priv *priv[IE31200_IMC_NUM];
} ie31200_pvt;

enum ie31200_chips {
	IE31200 = 0,
	IE31200_1 = 1,
};

struct ie31200_dev_info {
	const char *ctl_name;
};

struct ie31200_error_info {
	u16 errsts;
	u16 errsts2;
	u64 eccerrlog[IE31200_CHANNELS];
	u64 erraddr;
};

static const struct ie31200_dev_info ie31200_devs[] = {
	[IE31200] = {
		.ctl_name = "IE31200"
	},
	[IE31200_1] = {
		.ctl_name = "IE31200_1"
	},
};

struct dimm_data {
	u64 size; /* in bytes */
	u8  ranks;
	enum dev_type dtype;
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

#define mci_to_pci_dev(mci)	(((struct ie31200_priv *)(mci)->pvt_info)->pdev)

static void ie31200_clear_error_info(struct mem_ctl_info *mci)
{
	struct ie31200_priv *priv = mci->pvt_info;
	struct res_config *cfg = priv->cfg;

	/*
	 * The PCI ERRSTS register is deprecated. Write the MSR to clear
	 * the ECC error log registers in all memory controllers.
	 */
	if (cfg->msr_clear_eccerrlog_offset) {
		if (wrmsr_safe(cfg->msr_clear_eccerrlog_offset,
			       cfg->reg_eccerrlog_ce_mask |
			       cfg->reg_eccerrlog_ce_ovfl_mask |
			       cfg->reg_eccerrlog_ue_mask |
			       cfg->reg_eccerrlog_ue_ovfl_mask, 0) < 0)
			ie31200_printk(KERN_ERR, "Failed to wrmsr.\n");

		return;
	}

	/*
	 * Clear any error bits.
	 * (Yes, we really clear bits by writing 1 to them.)
	 */
	pci_write_bits16(mci_to_pci_dev(mci), IE31200_ERRSTS,
			 IE31200_ERRSTS_BITS, IE31200_ERRSTS_BITS);
}

static void ie31200_get_and_clear_error_info(struct mem_ctl_info *mci,
					     struct ie31200_error_info *info)
{
	struct pci_dev *pdev = mci_to_pci_dev(mci);
	struct ie31200_priv *priv = mci->pvt_info;

	/*
	 * The PCI ERRSTS register is deprecated, directly read the
	 * MMIO-mapped ECC error log registers.
	 */
	if (priv->cfg->msr_clear_eccerrlog_offset) {
		info->eccerrlog[0] = lo_hi_readq(priv->c0errlog);
		if (nr_channels == 2)
			info->eccerrlog[1] = lo_hi_readq(priv->c1errlog);

		ie31200_clear_error_info(mci);
		return;
	}

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
	struct ie31200_priv *priv = mci->pvt_info;
	struct res_config *cfg = priv->cfg;
	int channel;
	u64 log;

	if (!cfg->msr_clear_eccerrlog_offset) {
		if (!(info->errsts & IE31200_ERRSTS_BITS))
			return;

		if ((info->errsts ^ info->errsts2) & IE31200_ERRSTS_BITS) {
			edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci, 1, 0, 0, 0,
					     -1, -1, -1, "UE overwrote CE", "");
			info->errsts = info->errsts2;
		}
	}

	for (channel = 0; channel < nr_channels; channel++) {
		log = info->eccerrlog[channel];
		if (log & cfg->reg_eccerrlog_ue_mask) {
			edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci, 1,
					     info->erraddr >> PAGE_SHIFT, 0, 0,
					     field_get(cfg->reg_eccerrlog_rank_mask, log),
					     channel, -1,
					     "ie31200 UE", "");
		} else if (log & cfg->reg_eccerrlog_ce_mask) {
			edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci, 1,
					     info->erraddr >> PAGE_SHIFT, 0,
					     field_get(cfg->reg_eccerrlog_syndrome_mask, log),
					     field_get(cfg->reg_eccerrlog_rank_mask, log),
					     channel, -1,
					     "ie31200 CE", "");
		}
	}
}

static void __ie31200_check(struct mem_ctl_info *mci, struct mce *mce)
{
	struct ie31200_error_info info;

	info.erraddr = mce ? mce->addr : 0;
	ie31200_get_and_clear_error_info(mci, &info);
	ie31200_process_error_info(mci, &info);
}

static void ie31200_check(struct mem_ctl_info *mci)
{
	__ie31200_check(mci, NULL);
}

static void __iomem *ie31200_map_mchbar(struct pci_dev *pdev, struct res_config *cfg, int mc)
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
	u.mchbar &= cfg->reg_mchbar_mask;
	u.mchbar += cfg->reg_mchbar_window_size * mc;

	if (u.mchbar != (resource_size_t)u.mchbar) {
		ie31200_printk(KERN_ERR, "mmio space beyond accessible range (0x%llx)\n",
			       (unsigned long long)u.mchbar);
		return NULL;
	}

	window = ioremap(u.mchbar, cfg->reg_mchbar_window_size);
	if (!window)
		ie31200_printk(KERN_ERR, "Cannot map mmio space at 0x%llx\n",
			       (unsigned long long)u.mchbar);

	return window;
}

static void populate_dimm_info(struct dimm_data *dd, u32 addr_decode, int dimm,
			       struct res_config *cfg)
{
	dd->size = field_get(cfg->reg_mad_dimm_size_mask[dimm], addr_decode) * cfg->reg_mad_dimm_size_granularity;
	dd->ranks = field_get(cfg->reg_mad_dimm_rank_mask[dimm], addr_decode) + 1;
	dd->dtype = field_get(cfg->reg_mad_dimm_width_mask[dimm], addr_decode) + DEV_X8;
}

static void ie31200_get_dimm_config(struct mem_ctl_info *mci, void __iomem *window,
				    struct res_config *cfg, int mc)
{
	struct dimm_data dimm_info;
	struct dimm_info *dimm;
	unsigned long nr_pages;
	u32 addr_decode;
	int i, j, k;

	for (i = 0; i < IE31200_CHANNELS; i++) {
		addr_decode = readl(window + cfg->reg_mad_dimm_offset[i]);
		edac_dbg(0, "addr_decode: 0x%x\n", addr_decode);

		for (j = 0; j < IE31200_DIMMS_PER_CHANNEL; j++) {
			populate_dimm_info(&dimm_info, addr_decode, j, cfg);
			edac_dbg(0, "mc: %d, channel: %d, dimm: %d, size: %lld MiB, ranks: %d, DRAM chip type: %d\n",
				 mc, i, j, dimm_info.size >> 20,
				 dimm_info.ranks,
				 dimm_info.dtype);

			nr_pages = MiB_TO_PAGES(dimm_info.size >> 20);
			if (nr_pages == 0)
				continue;

			nr_pages = nr_pages / dimm_info.ranks;
			for (k = 0; k < dimm_info.ranks; k++) {
				dimm = edac_get_dimm(mci, (j * dimm_info.ranks) + k, i, 0);
				dimm->nr_pages = nr_pages;
				edac_dbg(0, "set nr pages: 0x%lx\n", nr_pages);
				dimm->grain = 8; /* just a guess */
				dimm->mtype = cfg->mtype;
				dimm->dtype = dimm_info.dtype;
				dimm->edac_mode = EDAC_UNKNOWN;
			}
		}
	}
}

static int ie31200_register_mci(struct pci_dev *pdev, struct res_config *cfg, int mc)
{
	struct edac_mc_layer layers[2];
	struct ie31200_priv *priv;
	struct mem_ctl_info *mci;
	void __iomem *window;
	int ret;

	nr_channels = how_many_channels(pdev);
	layers[0].type = EDAC_MC_LAYER_CHIP_SELECT;
	layers[0].size = IE31200_RANKS_PER_CHANNEL;
	layers[0].is_virt_csrow = true;
	layers[1].type = EDAC_MC_LAYER_CHANNEL;
	layers[1].size = nr_channels;
	layers[1].is_virt_csrow = false;
	mci = edac_mc_alloc(mc, ARRAY_SIZE(layers), layers,
			    sizeof(struct ie31200_priv));
	if (!mci)
		return -ENOMEM;

	window = ie31200_map_mchbar(pdev, cfg, mc);
	if (!window) {
		ret = -ENODEV;
		goto fail_free;
	}

	edac_dbg(3, "MC: init mci\n");
	mci->mtype_cap = BIT(cfg->mtype);
	mci->edac_ctl_cap = EDAC_FLAG_SECDED;
	mci->edac_cap = EDAC_FLAG_SECDED;
	mci->mod_name = EDAC_MOD_STR;
	mci->ctl_name = ie31200_devs[mc].ctl_name;
	mci->dev_name = pci_name(pdev);
	mci->edac_check = cfg->cmci ? NULL : ie31200_check;
	mci->ctl_page_to_phys = NULL;
	priv = mci->pvt_info;
	priv->window = window;
	priv->c0errlog = window + cfg->reg_eccerrlog_offset[0];
	priv->c1errlog = window + cfg->reg_eccerrlog_offset[1];
	priv->cfg = cfg;
	priv->mci = mci;
	priv->pdev = pdev;
	device_initialize(&priv->dev);
	/*
	 * The EDAC core uses mci->pdev (pointer to the structure device)
	 * as the memory controller ID. The SoCs attach one or more memory
	 * controllers to a single pci_dev (a single pci_dev->dev can
	 * correspond to multiple memory controllers).
	 *
	 * To make mci->pdev unique, assign pci_dev->dev to mci->pdev
	 * for the first memory controller and assign a unique priv->dev
	 * to mci->pdev for each additional memory controller.
	 */
	mci->pdev = mc ? &priv->dev : &pdev->dev;

	ie31200_get_dimm_config(mci, window, cfg, mc);
	ie31200_clear_error_info(mci);

	if (edac_mc_add_mc(mci)) {
		edac_dbg(3, "MC: failed edac_mc_add_mc()\n");
		ret = -ENODEV;
		goto fail_unmap;
	}

	ie31200_pvt.priv[mc] = priv;
	return 0;
fail_unmap:
	iounmap(window);
fail_free:
	edac_mc_free(mci);
	return ret;
}

static void mce_check(struct mce *mce)
{
	struct ie31200_priv *priv;
	int i;

	for (i = 0; i < IE31200_IMC_NUM; i++) {
		priv = ie31200_pvt.priv[i];
		if (!priv)
			continue;

		__ie31200_check(priv->mci, mce);
	}
}

static int mce_handler(struct notifier_block *nb, unsigned long val, void *data)
{
	struct mce *mce = (struct mce *)data;
	char *type;

	if (mce->kflags & MCE_HANDLED_CEC)
		return NOTIFY_DONE;

	/*
	 * Ignore unless this is a memory related error.
	 * Don't check MCI_STATUS_ADDRV since it's not set on some CPUs.
	 */
	if ((mce->status & 0xefff) >> 7 != 1)
		return NOTIFY_DONE;

	type = mce->mcgstatus & MCG_STATUS_MCIP ?  "Exception" : "Event";

	edac_dbg(0, "CPU %d: Machine Check %s: 0x%llx Bank %d: 0x%llx\n",
		 mce->extcpu, type, mce->mcgstatus,
		 mce->bank, mce->status);
	edac_dbg(0, "TSC 0x%llx\n", mce->tsc);
	edac_dbg(0, "ADDR 0x%llx\n", mce->addr);
	edac_dbg(0, "MISC 0x%llx\n", mce->misc);
	edac_dbg(0, "PROCESSOR %u:0x%x TIME %llu SOCKET %u APIC 0x%x\n",
		 mce->cpuvendor, mce->cpuid, mce->time,
		 mce->socketid, mce->apicid);

	mce_check(mce);
	mce->kflags |= MCE_HANDLED_EDAC;

	return NOTIFY_DONE;
}

static struct notifier_block ie31200_mce_dec = {
	.notifier_call	= mce_handler,
	.priority	= MCE_PRIO_EDAC,
};

static void ie31200_unregister_mcis(void)
{
	struct ie31200_priv *priv;
	struct mem_ctl_info *mci;
	int i;

	for (i = 0; i < IE31200_IMC_NUM; i++) {
		priv = ie31200_pvt.priv[i];
		if (!priv)
			continue;

		mci = priv->mci;
		edac_mc_del_mc(mci->pdev);
		iounmap(priv->window);
		edac_mc_free(mci);
	}
}

static int ie31200_probe1(struct pci_dev *pdev, struct res_config *cfg)
{
	int i, ret;

	edac_dbg(0, "MC:\n");

	if (!ecc_capable(pdev)) {
		ie31200_printk(KERN_INFO, "No ECC support\n");
		return -ENODEV;
	}

	for (i = 0; i < cfg->imc_num; i++) {
		ret = ie31200_register_mci(pdev, cfg, i);
		if (ret)
			goto fail_register;
	}

	if (cfg->cmci) {
		mce_register_decode_chain(&ie31200_mce_dec);
		edac_op_state = EDAC_OPSTATE_INT;
	} else {
		edac_op_state = EDAC_OPSTATE_POLL;
	}

	/* get this far and it's successful. */
	edac_dbg(3, "MC: success\n");
	return 0;

fail_register:
	ie31200_unregister_mcis();
	return ret;
}

static int ie31200_init_one(struct pci_dev *pdev,
			    const struct pci_device_id *ent)
{
	int rc;

	edac_dbg(0, "MC:\n");
	if (pci_enable_device(pdev) < 0)
		return -EIO;
	rc = ie31200_probe1(pdev, (struct res_config *)ent->driver_data);
	if (rc == 0 && !mci_pdev)
		mci_pdev = pci_dev_get(pdev);

	return rc;
}

static void ie31200_remove_one(struct pci_dev *pdev)
{
	struct ie31200_priv *priv = ie31200_pvt.priv[0];

	edac_dbg(0, "\n");
	pci_dev_put(mci_pdev);
	mci_pdev = NULL;
	if (priv->cfg->cmci)
		mce_unregister_decode_chain(&ie31200_mce_dec);
	ie31200_unregister_mcis();
}

static struct res_config snb_cfg = {
	.mtype				= MEM_DDR3,
	.imc_num			= 1,
	.reg_mchbar_mask		= GENMASK_ULL(38, 15),
	.reg_mchbar_window_size		= BIT_ULL(15),
	.reg_eccerrlog_offset[0]	= 0x40c8,
	.reg_eccerrlog_offset[1]	= 0x44c8,
	.reg_eccerrlog_ce_mask		= BIT_ULL(0),
	.reg_eccerrlog_ue_mask		= BIT_ULL(1),
	.reg_eccerrlog_rank_mask	= GENMASK_ULL(28, 27),
	.reg_eccerrlog_syndrome_mask	= GENMASK_ULL(23, 16),
	.reg_mad_dimm_size_granularity	= BIT_ULL(28),
	.reg_mad_dimm_offset[0]		= 0x5004,
	.reg_mad_dimm_offset[1]		= 0x5008,
	.reg_mad_dimm_size_mask[0]	= GENMASK(7, 0),
	.reg_mad_dimm_size_mask[1]	= GENMASK(15, 8),
	.reg_mad_dimm_rank_mask[0]	= BIT(17),
	.reg_mad_dimm_rank_mask[1]	= BIT(18),
	.reg_mad_dimm_width_mask[0]	= BIT(19),
	.reg_mad_dimm_width_mask[1]	= BIT(20),
};

static struct res_config skl_cfg = {
	.mtype				= MEM_DDR4,
	.imc_num			= 1,
	.reg_mchbar_mask		= GENMASK_ULL(38, 15),
	.reg_mchbar_window_size		= BIT_ULL(15),
	.reg_eccerrlog_offset[0]	= 0x4048,
	.reg_eccerrlog_offset[1]	= 0x4448,
	.reg_eccerrlog_ce_mask		= BIT_ULL(0),
	.reg_eccerrlog_ue_mask		= BIT_ULL(1),
	.reg_eccerrlog_rank_mask	= GENMASK_ULL(28, 27),
	.reg_eccerrlog_syndrome_mask	= GENMASK_ULL(23, 16),
	.reg_mad_dimm_size_granularity	= BIT_ULL(30),
	.reg_mad_dimm_offset[0]		= 0x500c,
	.reg_mad_dimm_offset[1]		= 0x5010,
	.reg_mad_dimm_size_mask[0]	= GENMASK(5, 0),
	.reg_mad_dimm_size_mask[1]	= GENMASK(21, 16),
	.reg_mad_dimm_rank_mask[0]	= BIT(10),
	.reg_mad_dimm_rank_mask[1]	= BIT(26),
	.reg_mad_dimm_width_mask[0]	= GENMASK(9, 8),
	.reg_mad_dimm_width_mask[1]	= GENMASK(25, 24),
};

struct res_config rpl_s_cfg = {
	.mtype				= MEM_DDR5,
	.cmci				= true,
	.imc_num			= 2,
	.reg_mchbar_mask		= GENMASK_ULL(41, 17),
	.reg_mchbar_window_size		= BIT_ULL(16),
	.reg_eccerrlog_offset[0]	= 0xe048,
	.reg_eccerrlog_offset[1]	= 0xe848,
	.reg_eccerrlog_ce_mask		= BIT_ULL(0),
	.reg_eccerrlog_ce_ovfl_mask	= BIT_ULL(1),
	.reg_eccerrlog_ue_mask		= BIT_ULL(2),
	.reg_eccerrlog_ue_ovfl_mask	= BIT_ULL(3),
	.reg_eccerrlog_rank_mask	= GENMASK_ULL(28, 27),
	.reg_eccerrlog_syndrome_mask	= GENMASK_ULL(23, 16),
	.msr_clear_eccerrlog_offset	= 0x791,
	.reg_mad_dimm_offset[0]		= 0xd80c,
	.reg_mad_dimm_offset[1]		= 0xd810,
	.reg_mad_dimm_size_granularity	= BIT_ULL(29),
	.reg_mad_dimm_size_mask[0]	= GENMASK(6, 0),
	.reg_mad_dimm_size_mask[1]	= GENMASK(22, 16),
	.reg_mad_dimm_rank_mask[0]	= GENMASK(10, 9),
	.reg_mad_dimm_rank_mask[1]	= GENMASK(27, 26),
	.reg_mad_dimm_width_mask[0]	= GENMASK(8, 7),
	.reg_mad_dimm_width_mask[1]	= GENMASK(25, 24),
};

static const struct pci_device_id ie31200_pci_tbl[] = {
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IE31200_HB_1), (kernel_ulong_t)&snb_cfg },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IE31200_HB_2), (kernel_ulong_t)&snb_cfg },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IE31200_HB_3), (kernel_ulong_t)&snb_cfg },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IE31200_HB_4), (kernel_ulong_t)&snb_cfg },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IE31200_HB_5), (kernel_ulong_t)&snb_cfg },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IE31200_HB_6), (kernel_ulong_t)&snb_cfg },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IE31200_HB_7), (kernel_ulong_t)&snb_cfg },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IE31200_HB_8), (kernel_ulong_t)&skl_cfg },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IE31200_HB_9), (kernel_ulong_t)&skl_cfg },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IE31200_HB_10), (kernel_ulong_t)&skl_cfg },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IE31200_HB_11), (kernel_ulong_t)&skl_cfg },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IE31200_HB_12), (kernel_ulong_t)&skl_cfg },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_1), (kernel_ulong_t)&skl_cfg },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_2), (kernel_ulong_t)&skl_cfg },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_3), (kernel_ulong_t)&skl_cfg },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_4), (kernel_ulong_t)&skl_cfg },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_5), (kernel_ulong_t)&skl_cfg },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_6), (kernel_ulong_t)&skl_cfg },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_7), (kernel_ulong_t)&skl_cfg },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_8), (kernel_ulong_t)&skl_cfg },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_9), (kernel_ulong_t)&skl_cfg },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_10), (kernel_ulong_t)&skl_cfg },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IE31200_RPL_S_1), (kernel_ulong_t)&rpl_s_cfg},
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IE31200_RPL_S_2), (kernel_ulong_t)&rpl_s_cfg},
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IE31200_RPL_S_3), (kernel_ulong_t)&rpl_s_cfg},
	{ 0, } /* 0 terminated list. */
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
	int pci_rc, i;

	edac_dbg(3, "MC:\n");

	pci_rc = pci_register_driver(&ie31200_driver);
	if (pci_rc < 0)
		return pci_rc;

	if (!mci_pdev) {
		ie31200_registered = 0;
		for (i = 0; ie31200_pci_tbl[i].vendor != 0; i++) {
			mci_pdev = pci_get_device(ie31200_pci_tbl[i].vendor,
						  ie31200_pci_tbl[i].device,
						  NULL);
			if (mci_pdev)
				break;
		}

		if (!mci_pdev) {
			edac_dbg(0, "ie31200 pci_get_device fail\n");
			pci_rc = -ENODEV;
			goto fail0;
		}

		pci_rc = ie31200_init_one(mci_pdev, &ie31200_pci_tbl[i]);
		if (pci_rc < 0) {
			edac_dbg(0, "ie31200 init fail\n");
			pci_rc = -ENODEV;
			goto fail1;
		}
	}

	return 0;
fail1:
	pci_dev_put(mci_pdev);
fail0:
	pci_unregister_driver(&ie31200_driver);

	return pci_rc;
}

static void __exit ie31200_exit(void)
{
	edac_dbg(3, "MC:\n");
	pci_unregister_driver(&ie31200_driver);
	if (!ie31200_registered)
		ie31200_remove_one(mci_pdev);
}

module_init(ie31200_init);
module_exit(ie31200_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jason Baron <jbaron@akamai.com>");
MODULE_DESCRIPTION("MC support for Intel Processor E31200 memory hub controllers");

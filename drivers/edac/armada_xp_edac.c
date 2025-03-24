// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017 Pengutronix, Jan Luebbe <kernel@pengutronix.de>
 */

#include <linux/kernel.h>
#include <linux/edac.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <asm/hardware/cache-l2x0.h>
#include <asm/hardware/cache-aurora-l2.h>

#include "edac_mc.h"
#include "edac_device.h"
#include "edac_module.h"

/************************ EDAC MC (DDR RAM) ********************************/

#define SDRAM_NUM_CS 4

#define SDRAM_CONFIG_REG        0x0
#define SDRAM_CONFIG_ECC_MASK         BIT(18)
#define SDRAM_CONFIG_REGISTERED_MASK  BIT(17)
#define SDRAM_CONFIG_BUS_WIDTH_MASK   BIT(15)

#define SDRAM_ADDR_CTRL_REG     0x10
#define SDRAM_ADDR_CTRL_SIZE_HIGH_OFFSET(cs) (20+cs)
#define SDRAM_ADDR_CTRL_SIZE_HIGH_MASK(cs)   (0x1 << SDRAM_ADDR_CTRL_SIZE_HIGH_OFFSET(cs))
#define SDRAM_ADDR_CTRL_ADDR_SEL_MASK(cs)    BIT(16+cs)
#define SDRAM_ADDR_CTRL_SIZE_LOW_OFFSET(cs)  (cs*4+2)
#define SDRAM_ADDR_CTRL_SIZE_LOW_MASK(cs)    (0x3 << SDRAM_ADDR_CTRL_SIZE_LOW_OFFSET(cs))
#define SDRAM_ADDR_CTRL_STRUCT_OFFSET(cs)    (cs*4)
#define SDRAM_ADDR_CTRL_STRUCT_MASK(cs)      (0x3 << SDRAM_ADDR_CTRL_STRUCT_OFFSET(cs))

#define SDRAM_ERR_DATA_H_REG    0x40
#define SDRAM_ERR_DATA_L_REG    0x44

#define SDRAM_ERR_RECV_ECC_REG  0x48
#define SDRAM_ERR_RECV_ECC_VALUE_MASK 0xff

#define SDRAM_ERR_CALC_ECC_REG  0x4c
#define SDRAM_ERR_CALC_ECC_ROW_OFFSET 8
#define SDRAM_ERR_CALC_ECC_ROW_MASK   (0xffff << SDRAM_ERR_CALC_ECC_ROW_OFFSET)
#define SDRAM_ERR_CALC_ECC_VALUE_MASK 0xff

#define SDRAM_ERR_ADDR_REG      0x50
#define SDRAM_ERR_ADDR_BANK_OFFSET    23
#define SDRAM_ERR_ADDR_BANK_MASK      (0x7 << SDRAM_ERR_ADDR_BANK_OFFSET)
#define SDRAM_ERR_ADDR_COL_OFFSET     8
#define SDRAM_ERR_ADDR_COL_MASK       (0x7fff << SDRAM_ERR_ADDR_COL_OFFSET)
#define SDRAM_ERR_ADDR_CS_OFFSET      1
#define SDRAM_ERR_ADDR_CS_MASK        (0x3 << SDRAM_ERR_ADDR_CS_OFFSET)
#define SDRAM_ERR_ADDR_TYPE_MASK      BIT(0)

#define SDRAM_ERR_CTRL_REG      0x54
#define SDRAM_ERR_CTRL_THR_OFFSET     16
#define SDRAM_ERR_CTRL_THR_MASK       (0xff << SDRAM_ERR_CTRL_THR_OFFSET)
#define SDRAM_ERR_CTRL_PROP_MASK      BIT(9)

#define SDRAM_ERR_SBE_COUNT_REG 0x58
#define SDRAM_ERR_DBE_COUNT_REG 0x5c

#define SDRAM_ERR_CAUSE_ERR_REG 0xd0
#define SDRAM_ERR_CAUSE_MSG_REG 0xd8
#define SDRAM_ERR_CAUSE_DBE_MASK      BIT(1)
#define SDRAM_ERR_CAUSE_SBE_MASK      BIT(0)

#define SDRAM_RANK_CTRL_REG 0x1e0
#define SDRAM_RANK_CTRL_EXIST_MASK(cs) BIT(cs)

struct axp_mc_drvdata {
	void __iomem *base;
	/* width in bytes */
	unsigned int width;
	/* bank interleaving */
	bool cs_addr_sel[SDRAM_NUM_CS];

	char msg[128];
};

/* derived from "DRAM Address Multiplexing" in the ARMADA XP Functional Spec */
static uint32_t axp_mc_calc_address(struct axp_mc_drvdata *drvdata,
				    uint8_t cs, uint8_t bank, uint16_t row,
				    uint16_t col)
{
	if (drvdata->width == 8) {
		/* 64 bit */
		if (drvdata->cs_addr_sel[cs])
			/* bank interleaved */
			return (((row & 0xfff8) << 16) |
				((bank & 0x7) << 16) |
				((row & 0x7) << 13) |
				((col & 0x3ff) << 3));
		else
			return (((row & 0xffff << 16) |
				 ((bank & 0x7) << 13) |
				 ((col & 0x3ff)) << 3));
	} else if (drvdata->width == 4) {
		/* 32 bit */
		if (drvdata->cs_addr_sel[cs])
			/* bank interleaved */
			return (((row & 0xfff0) << 15) |
				((bank & 0x7) << 16) |
				((row & 0xf) << 12) |
				((col & 0x3ff) << 2));
		else
			return (((row & 0xffff << 15) |
				 ((bank & 0x7) << 12) |
				 ((col & 0x3ff)) << 2));
	} else {
		/* 16 bit */
		if (drvdata->cs_addr_sel[cs])
			/* bank interleaved */
			return (((row & 0xffe0) << 14) |
				((bank & 0x7) << 16) |
				((row & 0x1f) << 11) |
				((col & 0x3ff) << 1));
		else
			return (((row & 0xffff << 14) |
				 ((bank & 0x7) << 11) |
				 ((col & 0x3ff)) << 1));
	}
}

static void axp_mc_check(struct mem_ctl_info *mci)
{
	struct axp_mc_drvdata *drvdata = mci->pvt_info;
	uint32_t data_h, data_l, recv_ecc, calc_ecc, addr;
	uint32_t cnt_sbe, cnt_dbe, cause_err, cause_msg;
	uint32_t row_val, col_val, bank_val, addr_val;
	uint8_t syndrome_val, cs_val;
	char *msg = drvdata->msg;

	data_h    = readl(drvdata->base + SDRAM_ERR_DATA_H_REG);
	data_l    = readl(drvdata->base + SDRAM_ERR_DATA_L_REG);
	recv_ecc  = readl(drvdata->base + SDRAM_ERR_RECV_ECC_REG);
	calc_ecc  = readl(drvdata->base + SDRAM_ERR_CALC_ECC_REG);
	addr      = readl(drvdata->base + SDRAM_ERR_ADDR_REG);
	cnt_sbe   = readl(drvdata->base + SDRAM_ERR_SBE_COUNT_REG);
	cnt_dbe   = readl(drvdata->base + SDRAM_ERR_DBE_COUNT_REG);
	cause_err = readl(drvdata->base + SDRAM_ERR_CAUSE_ERR_REG);
	cause_msg = readl(drvdata->base + SDRAM_ERR_CAUSE_MSG_REG);

	/* clear cause registers */
	writel(~(SDRAM_ERR_CAUSE_DBE_MASK | SDRAM_ERR_CAUSE_SBE_MASK),
	       drvdata->base + SDRAM_ERR_CAUSE_ERR_REG);
	writel(~(SDRAM_ERR_CAUSE_DBE_MASK | SDRAM_ERR_CAUSE_SBE_MASK),
	       drvdata->base + SDRAM_ERR_CAUSE_MSG_REG);

	/* clear error counter registers */
	if (cnt_sbe)
		writel(0, drvdata->base + SDRAM_ERR_SBE_COUNT_REG);
	if (cnt_dbe)
		writel(0, drvdata->base + SDRAM_ERR_DBE_COUNT_REG);

	if (!cnt_sbe && !cnt_dbe)
		return;

	if (!(addr & SDRAM_ERR_ADDR_TYPE_MASK)) {
		if (cnt_sbe)
			cnt_sbe--;
		else
			dev_warn(mci->pdev, "inconsistent SBE count detected\n");
	} else {
		if (cnt_dbe)
			cnt_dbe--;
		else
			dev_warn(mci->pdev, "inconsistent DBE count detected\n");
	}

	/* report earlier errors */
	if (cnt_sbe)
		edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci,
				     cnt_sbe, /* error count */
				     0, 0, 0, /* pfn, offset, syndrome */
				     -1, -1, -1, /* top, mid, low layer */
				     mci->ctl_name,
				     "details unavailable (multiple errors)");
	if (cnt_dbe)
		edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci,
				     cnt_dbe, /* error count */
				     0, 0, 0, /* pfn, offset, syndrome */
				     -1, -1, -1, /* top, mid, low layer */
				     mci->ctl_name,
				     "details unavailable (multiple errors)");

	/* report details for most recent error */
	cs_val   = (addr & SDRAM_ERR_ADDR_CS_MASK) >> SDRAM_ERR_ADDR_CS_OFFSET;
	bank_val = (addr & SDRAM_ERR_ADDR_BANK_MASK) >> SDRAM_ERR_ADDR_BANK_OFFSET;
	row_val  = (calc_ecc & SDRAM_ERR_CALC_ECC_ROW_MASK) >> SDRAM_ERR_CALC_ECC_ROW_OFFSET;
	col_val  = (addr & SDRAM_ERR_ADDR_COL_MASK) >> SDRAM_ERR_ADDR_COL_OFFSET;
	syndrome_val = (recv_ecc ^ calc_ecc) & 0xff;
	addr_val = axp_mc_calc_address(drvdata, cs_val, bank_val, row_val,
				       col_val);
	msg += sprintf(msg, "row=0x%04x ", row_val); /* 11 chars */
	msg += sprintf(msg, "bank=0x%x ", bank_val); /*  9 chars */
	msg += sprintf(msg, "col=0x%04x ", col_val); /* 11 chars */
	msg += sprintf(msg, "cs=%d", cs_val);	     /*  4 chars */

	if (!(addr & SDRAM_ERR_ADDR_TYPE_MASK)) {
		edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci,
				     1,	/* error count */
				     addr_val >> PAGE_SHIFT,
				     addr_val & ~PAGE_MASK,
				     syndrome_val,
				     cs_val, -1, -1, /* top, mid, low layer */
				     mci->ctl_name, drvdata->msg);
	} else {
		edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci,
				     1,	/* error count */
				     addr_val >> PAGE_SHIFT,
				     addr_val & ~PAGE_MASK,
				     syndrome_val,
				     cs_val, -1, -1, /* top, mid, low layer */
				     mci->ctl_name, drvdata->msg);
	}
}

static void axp_mc_read_config(struct mem_ctl_info *mci)
{
	struct axp_mc_drvdata *drvdata = mci->pvt_info;
	uint32_t config, addr_ctrl, rank_ctrl;
	unsigned int i, cs_struct, cs_size;
	struct dimm_info *dimm;

	config = readl(drvdata->base + SDRAM_CONFIG_REG);
	if (config & SDRAM_CONFIG_BUS_WIDTH_MASK)
		/* 64 bit */
		drvdata->width = 8;
	else
		/* 32 bit */
		drvdata->width = 4;

	addr_ctrl = readl(drvdata->base + SDRAM_ADDR_CTRL_REG);
	rank_ctrl = readl(drvdata->base + SDRAM_RANK_CTRL_REG);
	for (i = 0; i < SDRAM_NUM_CS; i++) {
		dimm = mci->dimms[i];

		if (!(rank_ctrl & SDRAM_RANK_CTRL_EXIST_MASK(i)))
			continue;

		drvdata->cs_addr_sel[i] =
			!!(addr_ctrl & SDRAM_ADDR_CTRL_ADDR_SEL_MASK(i));

		cs_struct = (addr_ctrl & SDRAM_ADDR_CTRL_STRUCT_MASK(i)) >> SDRAM_ADDR_CTRL_STRUCT_OFFSET(i);
		cs_size   = ((addr_ctrl & SDRAM_ADDR_CTRL_SIZE_HIGH_MASK(i)) >> (SDRAM_ADDR_CTRL_SIZE_HIGH_OFFSET(i) - 2) |
			    ((addr_ctrl & SDRAM_ADDR_CTRL_SIZE_LOW_MASK(i)) >> SDRAM_ADDR_CTRL_SIZE_LOW_OFFSET(i)));

		switch (cs_size) {
		case 0: /* 2GBit */
			dimm->nr_pages = 524288;
			break;
		case 1: /* 256MBit */
			dimm->nr_pages = 65536;
			break;
		case 2: /* 512MBit */
			dimm->nr_pages = 131072;
			break;
		case 3: /* 1GBit */
			dimm->nr_pages = 262144;
			break;
		case 4: /* 4GBit */
			dimm->nr_pages = 1048576;
			break;
		case 5: /* 8GBit */
			dimm->nr_pages = 2097152;
			break;
		}
		dimm->grain = 8;
		dimm->dtype = cs_struct ? DEV_X16 : DEV_X8;
		dimm->mtype = (config & SDRAM_CONFIG_REGISTERED_MASK) ?
			MEM_RDDR3 : MEM_DDR3;
		dimm->edac_mode = EDAC_SECDED;
	}
}

static const struct of_device_id axp_mc_of_match[] = {
	{.compatible = "marvell,armada-xp-sdram-controller",},
	{},
};
MODULE_DEVICE_TABLE(of, axp_mc_of_match);

static int axp_mc_probe(struct platform_device *pdev)
{
	struct axp_mc_drvdata *drvdata;
	struct edac_mc_layer layers[1];
	const struct of_device_id *id;
	struct mem_ctl_info *mci;
	void __iomem *base;
	uint32_t config;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base)) {
		dev_err(&pdev->dev, "Unable to map regs\n");
		return PTR_ERR(base);
	}

	config = readl(base + SDRAM_CONFIG_REG);
	if (!(config & SDRAM_CONFIG_ECC_MASK)) {
		dev_warn(&pdev->dev, "SDRAM ECC is not enabled\n");
		return -EINVAL;
	}

	layers[0].type = EDAC_MC_LAYER_CHIP_SELECT;
	layers[0].size = SDRAM_NUM_CS;
	layers[0].is_virt_csrow = true;

	mci = edac_mc_alloc(0, ARRAY_SIZE(layers), layers, sizeof(*drvdata));
	if (!mci)
		return -ENOMEM;

	drvdata = mci->pvt_info;
	drvdata->base = base;
	mci->pdev = &pdev->dev;
	platform_set_drvdata(pdev, mci);

	id = of_match_device(axp_mc_of_match, &pdev->dev);
	mci->edac_check = axp_mc_check;
	mci->mtype_cap = MEM_FLAG_DDR3;
	mci->edac_cap = EDAC_FLAG_SECDED;
	mci->mod_name = pdev->dev.driver->name;
	mci->ctl_name = id ? id->compatible : "unknown";
	mci->dev_name = dev_name(&pdev->dev);
	mci->scrub_mode = SCRUB_NONE;

	axp_mc_read_config(mci);

	/* These SoCs have a reduced width bus */
	if (of_machine_is_compatible("marvell,armada380") ||
	    of_machine_is_compatible("marvell,armadaxp-98dx3236"))
		drvdata->width /= 2;

	/* configure SBE threshold */
	/* it seems that SBEs are not captured otherwise */
	writel(1 << SDRAM_ERR_CTRL_THR_OFFSET, drvdata->base + SDRAM_ERR_CTRL_REG);

	/* clear cause registers */
	writel(~(SDRAM_ERR_CAUSE_DBE_MASK | SDRAM_ERR_CAUSE_SBE_MASK), drvdata->base + SDRAM_ERR_CAUSE_ERR_REG);
	writel(~(SDRAM_ERR_CAUSE_DBE_MASK | SDRAM_ERR_CAUSE_SBE_MASK), drvdata->base + SDRAM_ERR_CAUSE_MSG_REG);

	/* clear counter registers */
	writel(0, drvdata->base + SDRAM_ERR_SBE_COUNT_REG);
	writel(0, drvdata->base + SDRAM_ERR_DBE_COUNT_REG);

	if (edac_mc_add_mc(mci)) {
		edac_mc_free(mci);
		return -EINVAL;
	}
	edac_op_state = EDAC_OPSTATE_POLL;

	return 0;
}

static void axp_mc_remove(struct platform_device *pdev)
{
	struct mem_ctl_info *mci = platform_get_drvdata(pdev);

	edac_mc_del_mc(&pdev->dev);
	edac_mc_free(mci);
	platform_set_drvdata(pdev, NULL);
}

static struct platform_driver axp_mc_driver = {
	.probe = axp_mc_probe,
	.remove = axp_mc_remove,
	.driver = {
		.name = "armada_xp_mc_edac",
		.of_match_table = of_match_ptr(axp_mc_of_match),
	},
};

/************************ EDAC Device (L2 Cache) ***************************/

struct aurora_l2_drvdata {
	void __iomem *base;

	char msg[128];

	/* error injection via debugfs */
	uint32_t inject_addr;
	uint32_t inject_mask;
	uint8_t inject_ctl;

	struct dentry *debugfs;
};

#ifdef CONFIG_EDAC_DEBUG
static void aurora_l2_inject(struct aurora_l2_drvdata *drvdata)
{
	drvdata->inject_addr &= AURORA_ERR_INJECT_CTL_ADDR_MASK;
	drvdata->inject_ctl &= AURORA_ERR_INJECT_CTL_EN_MASK;
	writel(0, drvdata->base + AURORA_ERR_INJECT_CTL_REG);
	writel(drvdata->inject_mask, drvdata->base + AURORA_ERR_INJECT_MASK_REG);
	writel(drvdata->inject_addr | drvdata->inject_ctl, drvdata->base + AURORA_ERR_INJECT_CTL_REG);
}
#endif

static void aurora_l2_check(struct edac_device_ctl_info *dci)
{
	struct aurora_l2_drvdata *drvdata = dci->pvt_info;
	uint32_t cnt, src, txn, err, attr_cap, addr_cap, way_cap;
	unsigned int cnt_ce, cnt_ue;
	char *msg = drvdata->msg;
	size_t size = sizeof(drvdata->msg);
	size_t len = 0;

	cnt = readl(drvdata->base + AURORA_ERR_CNT_REG);
	attr_cap = readl(drvdata->base + AURORA_ERR_ATTR_CAP_REG);
	addr_cap = readl(drvdata->base + AURORA_ERR_ADDR_CAP_REG);
	way_cap = readl(drvdata->base + AURORA_ERR_WAY_CAP_REG);

	cnt_ce = (cnt & AURORA_ERR_CNT_CE_MASK) >> AURORA_ERR_CNT_CE_OFFSET;
	cnt_ue = (cnt & AURORA_ERR_CNT_UE_MASK) >> AURORA_ERR_CNT_UE_OFFSET;
	/* clear error counter registers */
	if (cnt_ce || cnt_ue)
		writel(AURORA_ERR_CNT_CLR, drvdata->base + AURORA_ERR_CNT_REG);

	if (!(attr_cap & AURORA_ERR_ATTR_CAP_VALID))
		goto clear_remaining;

	src = (attr_cap & AURORA_ERR_ATTR_SRC_MSK) >> AURORA_ERR_ATTR_SRC_OFF;
	if (src <= 3)
		len += scnprintf(msg+len, size-len, "src=CPU%d ", src);
	else
		len += scnprintf(msg+len, size-len, "src=IO ");

	txn =  (attr_cap & AURORA_ERR_ATTR_TXN_MSK) >> AURORA_ERR_ATTR_TXN_OFF;
	switch (txn) {
	case 0:
		len += scnprintf(msg+len, size-len, "txn=Data-Read ");
		break;
	case 1:
		len += scnprintf(msg+len, size-len, "txn=Isn-Read ");
		break;
	case 2:
		len += scnprintf(msg+len, size-len, "txn=Clean-Flush ");
		break;
	case 3:
		len += scnprintf(msg+len, size-len, "txn=Eviction ");
		break;
	case 4:
		len += scnprintf(msg+len, size-len,
				"txn=Read-Modify-Write ");
		break;
	}

	err = (attr_cap & AURORA_ERR_ATTR_ERR_MSK) >> AURORA_ERR_ATTR_ERR_OFF;
	switch (err) {
	case 0:
		len += scnprintf(msg+len, size-len, "err=CorrECC ");
		break;
	case 1:
		len += scnprintf(msg+len, size-len, "err=UnCorrECC ");
		break;
	case 2:
		len += scnprintf(msg+len, size-len, "err=TagParity ");
		break;
	}

	len += scnprintf(msg+len, size-len, "addr=0x%x ", addr_cap & AURORA_ERR_ADDR_CAP_ADDR_MASK);
	len += scnprintf(msg+len, size-len, "index=0x%x ", (way_cap & AURORA_ERR_WAY_IDX_MSK) >> AURORA_ERR_WAY_IDX_OFF);
	len += scnprintf(msg+len, size-len, "way=0x%x", (way_cap & AURORA_ERR_WAY_CAP_WAY_MASK) >> AURORA_ERR_WAY_CAP_WAY_OFFSET);

	/* clear error capture registers */
	writel(AURORA_ERR_ATTR_CAP_VALID, drvdata->base + AURORA_ERR_ATTR_CAP_REG);
	if (err) {
		/* UnCorrECC or TagParity */
		if (cnt_ue)
			cnt_ue--;
		edac_device_handle_ue(dci, 0, 0, drvdata->msg);
	} else {
		if (cnt_ce)
			cnt_ce--;
		edac_device_handle_ce(dci, 0, 0, drvdata->msg);
	}

clear_remaining:
	/* report remaining errors */
	while (cnt_ue--)
		edac_device_handle_ue(dci, 0, 0, "details unavailable (multiple errors)");
	while (cnt_ce--)
		edac_device_handle_ue(dci, 0, 0, "details unavailable (multiple errors)");
}

static void aurora_l2_poll(struct edac_device_ctl_info *dci)
{
#ifdef CONFIG_EDAC_DEBUG
	struct aurora_l2_drvdata *drvdata = dci->pvt_info;
#endif

	aurora_l2_check(dci);
#ifdef CONFIG_EDAC_DEBUG
	aurora_l2_inject(drvdata);
#endif
}

static const struct of_device_id aurora_l2_of_match[] = {
	{.compatible = "marvell,aurora-system-cache",},
	{},
};
MODULE_DEVICE_TABLE(of, aurora_l2_of_match);

static int aurora_l2_probe(struct platform_device *pdev)
{
	struct aurora_l2_drvdata *drvdata;
	struct edac_device_ctl_info *dci;
	const struct of_device_id *id;
	uint32_t l2x0_aux_ctrl;
	void __iomem *base;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base)) {
		dev_err(&pdev->dev, "Unable to map regs\n");
		return PTR_ERR(base);
	}

	l2x0_aux_ctrl = readl(base + L2X0_AUX_CTRL);
	if (!(l2x0_aux_ctrl & AURORA_ACR_PARITY_EN))
		dev_warn(&pdev->dev, "tag parity is not enabled\n");
	if (!(l2x0_aux_ctrl & AURORA_ACR_ECC_EN))
		dev_warn(&pdev->dev, "data ECC is not enabled\n");

	dci = edac_device_alloc_ctl_info(sizeof(*drvdata),
					 "cpu", 1, "L", 1, 2, 0);
	if (!dci)
		return -ENOMEM;

	drvdata = dci->pvt_info;
	drvdata->base = base;
	dci->dev = &pdev->dev;
	platform_set_drvdata(pdev, dci);

	id = of_match_device(aurora_l2_of_match, &pdev->dev);
	dci->edac_check = aurora_l2_poll;
	dci->mod_name = pdev->dev.driver->name;
	dci->ctl_name = id ? id->compatible : "unknown";
	dci->dev_name = dev_name(&pdev->dev);

	/* clear registers */
	writel(AURORA_ERR_CNT_CLR, drvdata->base + AURORA_ERR_CNT_REG);
	writel(AURORA_ERR_ATTR_CAP_VALID, drvdata->base + AURORA_ERR_ATTR_CAP_REG);

	if (edac_device_add_device(dci)) {
		edac_device_free_ctl_info(dci);
		return -EINVAL;
	}

#ifdef CONFIG_EDAC_DEBUG
	drvdata->debugfs = edac_debugfs_create_dir(dev_name(&pdev->dev));
	if (drvdata->debugfs) {
		edac_debugfs_create_x32("inject_addr", 0644,
					drvdata->debugfs,
					&drvdata->inject_addr);
		edac_debugfs_create_x32("inject_mask", 0644,
					drvdata->debugfs,
					&drvdata->inject_mask);
		edac_debugfs_create_x8("inject_ctl", 0644,
				       drvdata->debugfs, &drvdata->inject_ctl);
	}
#endif

	return 0;
}

static void aurora_l2_remove(struct platform_device *pdev)
{
	struct edac_device_ctl_info *dci = platform_get_drvdata(pdev);
#ifdef CONFIG_EDAC_DEBUG
	struct aurora_l2_drvdata *drvdata = dci->pvt_info;

	edac_debugfs_remove_recursive(drvdata->debugfs);
#endif
	edac_device_del_device(&pdev->dev);
	edac_device_free_ctl_info(dci);
	platform_set_drvdata(pdev, NULL);
}

static struct platform_driver aurora_l2_driver = {
	.probe = aurora_l2_probe,
	.remove = aurora_l2_remove,
	.driver = {
		.name = "aurora_l2_edac",
		.of_match_table = of_match_ptr(aurora_l2_of_match),
	},
};

/************************ Driver registration ******************************/

static struct platform_driver * const drivers[] = {
	&axp_mc_driver,
	&aurora_l2_driver,
};

static int __init armada_xp_edac_init(void)
{
	int res;

	if (ghes_get_devices())
		return -EBUSY;

	/* only polling is supported */
	edac_op_state = EDAC_OPSTATE_POLL;

	res = platform_register_drivers(drivers, ARRAY_SIZE(drivers));
	if (res)
		pr_warn("Armada XP EDAC drivers fail to register\n");

	return 0;
}
module_init(armada_xp_edac_init);

static void __exit armada_xp_edac_exit(void)
{
	platform_unregister_drivers(drivers, ARRAY_SIZE(drivers));
}
module_exit(armada_xp_edac_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Pengutronix");
MODULE_DESCRIPTION("EDAC Drivers for Marvell Armada XP SDRAM and L2 Cache Controller");

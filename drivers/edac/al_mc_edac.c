// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 */
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/edac.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include "edac_module.h"

/* Registers Offset */
#define AL_MC_ECC_CFG		0x70
#define AL_MC_ECC_CLEAR		0x7c
#define AL_MC_ECC_ERR_COUNT	0x80
#define AL_MC_ECC_CE_ADDR0	0x84
#define AL_MC_ECC_CE_ADDR1	0x88
#define AL_MC_ECC_UE_ADDR0	0xa4
#define AL_MC_ECC_UE_ADDR1	0xa8
#define AL_MC_ECC_CE_SYND0	0x8c
#define AL_MC_ECC_CE_SYND1	0x90
#define AL_MC_ECC_CE_SYND2	0x94
#define AL_MC_ECC_UE_SYND0	0xac
#define AL_MC_ECC_UE_SYND1	0xb0
#define AL_MC_ECC_UE_SYND2	0xb4

/* Registers Fields */
#define AL_MC_ECC_CFG_SCRUB_DISABLED	BIT(4)

#define AL_MC_ECC_CLEAR_UE_COUNT	BIT(3)
#define AL_MC_ECC_CLEAR_CE_COUNT	BIT(2)
#define AL_MC_ECC_CLEAR_UE_ERR		BIT(1)
#define AL_MC_ECC_CLEAR_CE_ERR		BIT(0)

#define AL_MC_ECC_ERR_COUNT_UE		GENMASK(31, 16)
#define AL_MC_ECC_ERR_COUNT_CE		GENMASK(15, 0)

#define AL_MC_ECC_CE_ADDR0_RANK		GENMASK(25, 24)
#define AL_MC_ECC_CE_ADDR0_ROW		GENMASK(17, 0)

#define AL_MC_ECC_CE_ADDR1_BG		GENMASK(25, 24)
#define AL_MC_ECC_CE_ADDR1_BANK		GENMASK(18, 16)
#define AL_MC_ECC_CE_ADDR1_COLUMN	GENMASK(11, 0)

#define AL_MC_ECC_UE_ADDR0_RANK		GENMASK(25, 24)
#define AL_MC_ECC_UE_ADDR0_ROW		GENMASK(17, 0)

#define AL_MC_ECC_UE_ADDR1_BG		GENMASK(25, 24)
#define AL_MC_ECC_UE_ADDR1_BANK		GENMASK(18, 16)
#define AL_MC_ECC_UE_ADDR1_COLUMN	GENMASK(11, 0)

#define DRV_NAME "al_mc_edac"
#define AL_MC_EDAC_MSG_MAX 256

struct al_mc_edac {
	void __iomem *mmio_base;
	spinlock_t lock;
	int irq_ce;
	int irq_ue;
};

static void prepare_msg(char *message, size_t buffer_size,
			enum hw_event_mc_err_type type,
			u8 rank, u32 row, u8 bg, u8 bank, u16 column,
			u32 syn0, u32 syn1, u32 syn2)
{
	snprintf(message, buffer_size,
		 "%s rank=0x%x row=0x%x bg=0x%x bank=0x%x col=0x%x syn0: 0x%x syn1: 0x%x syn2: 0x%x",
		 type == HW_EVENT_ERR_UNCORRECTED ? "UE" : "CE",
		 rank, row, bg, bank, column, syn0, syn1, syn2);
}

static int handle_ce(struct mem_ctl_info *mci)
{
	u32 eccerrcnt, ecccaddr0, ecccaddr1, ecccsyn0, ecccsyn1, ecccsyn2, row;
	struct al_mc_edac *al_mc = mci->pvt_info;
	char msg[AL_MC_EDAC_MSG_MAX];
	u16 ce_count, column;
	unsigned long flags;
	u8 rank, bg, bank;

	eccerrcnt = readl_relaxed(al_mc->mmio_base + AL_MC_ECC_ERR_COUNT);
	ce_count = FIELD_GET(AL_MC_ECC_ERR_COUNT_CE, eccerrcnt);
	if (!ce_count)
		return 0;

	ecccaddr0 = readl_relaxed(al_mc->mmio_base + AL_MC_ECC_CE_ADDR0);
	ecccaddr1 = readl_relaxed(al_mc->mmio_base + AL_MC_ECC_CE_ADDR1);
	ecccsyn0 = readl_relaxed(al_mc->mmio_base + AL_MC_ECC_CE_SYND0);
	ecccsyn1 = readl_relaxed(al_mc->mmio_base + AL_MC_ECC_CE_SYND1);
	ecccsyn2 = readl_relaxed(al_mc->mmio_base + AL_MC_ECC_CE_SYND2);

	writel_relaxed(AL_MC_ECC_CLEAR_CE_COUNT | AL_MC_ECC_CLEAR_CE_ERR,
		       al_mc->mmio_base + AL_MC_ECC_CLEAR);

	dev_dbg(mci->pdev, "eccuaddr0=0x%08x eccuaddr1=0x%08x\n",
		ecccaddr0, ecccaddr1);

	rank = FIELD_GET(AL_MC_ECC_CE_ADDR0_RANK, ecccaddr0);
	row = FIELD_GET(AL_MC_ECC_CE_ADDR0_ROW, ecccaddr0);

	bg = FIELD_GET(AL_MC_ECC_CE_ADDR1_BG, ecccaddr1);
	bank = FIELD_GET(AL_MC_ECC_CE_ADDR1_BANK, ecccaddr1);
	column = FIELD_GET(AL_MC_ECC_CE_ADDR1_COLUMN, ecccaddr1);

	prepare_msg(msg, sizeof(msg), HW_EVENT_ERR_CORRECTED,
		    rank, row, bg, bank, column,
		    ecccsyn0, ecccsyn1, ecccsyn2);

	spin_lock_irqsave(&al_mc->lock, flags);
	edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci,
			     ce_count, 0, 0, 0, 0, 0, -1, mci->ctl_name, msg);
	spin_unlock_irqrestore(&al_mc->lock, flags);

	return ce_count;
}

static int handle_ue(struct mem_ctl_info *mci)
{
	u32 eccerrcnt, eccuaddr0, eccuaddr1, eccusyn0, eccusyn1, eccusyn2, row;
	struct al_mc_edac *al_mc = mci->pvt_info;
	char msg[AL_MC_EDAC_MSG_MAX];
	u16 ue_count, column;
	unsigned long flags;
	u8 rank, bg, bank;

	eccerrcnt = readl_relaxed(al_mc->mmio_base + AL_MC_ECC_ERR_COUNT);
	ue_count = FIELD_GET(AL_MC_ECC_ERR_COUNT_UE, eccerrcnt);
	if (!ue_count)
		return 0;

	eccuaddr0 = readl_relaxed(al_mc->mmio_base + AL_MC_ECC_UE_ADDR0);
	eccuaddr1 = readl_relaxed(al_mc->mmio_base + AL_MC_ECC_UE_ADDR1);
	eccusyn0 = readl_relaxed(al_mc->mmio_base + AL_MC_ECC_UE_SYND0);
	eccusyn1 = readl_relaxed(al_mc->mmio_base + AL_MC_ECC_UE_SYND1);
	eccusyn2 = readl_relaxed(al_mc->mmio_base + AL_MC_ECC_UE_SYND2);

	writel_relaxed(AL_MC_ECC_CLEAR_UE_COUNT | AL_MC_ECC_CLEAR_UE_ERR,
		       al_mc->mmio_base + AL_MC_ECC_CLEAR);

	dev_dbg(mci->pdev, "eccuaddr0=0x%08x eccuaddr1=0x%08x\n",
		eccuaddr0, eccuaddr1);

	rank = FIELD_GET(AL_MC_ECC_UE_ADDR0_RANK, eccuaddr0);
	row = FIELD_GET(AL_MC_ECC_UE_ADDR0_ROW, eccuaddr0);

	bg = FIELD_GET(AL_MC_ECC_UE_ADDR1_BG, eccuaddr1);
	bank = FIELD_GET(AL_MC_ECC_UE_ADDR1_BANK, eccuaddr1);
	column = FIELD_GET(AL_MC_ECC_UE_ADDR1_COLUMN, eccuaddr1);

	prepare_msg(msg, sizeof(msg), HW_EVENT_ERR_UNCORRECTED,
		    rank, row, bg, bank, column,
		    eccusyn0, eccusyn1, eccusyn2);

	spin_lock_irqsave(&al_mc->lock, flags);
	edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci,
			     ue_count, 0, 0, 0, 0, 0, -1, mci->ctl_name, msg);
	spin_unlock_irqrestore(&al_mc->lock, flags);

	return ue_count;
}

static void al_mc_edac_check(struct mem_ctl_info *mci)
{
	struct al_mc_edac *al_mc = mci->pvt_info;

	if (al_mc->irq_ue <= 0)
		handle_ue(mci);

	if (al_mc->irq_ce <= 0)
		handle_ce(mci);
}

static irqreturn_t al_mc_edac_irq_handler_ue(int irq, void *info)
{
	struct platform_device *pdev = info;
	struct mem_ctl_info *mci = platform_get_drvdata(pdev);

	if (handle_ue(mci))
		return IRQ_HANDLED;
	return IRQ_NONE;
}

static irqreturn_t al_mc_edac_irq_handler_ce(int irq, void *info)
{
	struct platform_device *pdev = info;
	struct mem_ctl_info *mci = platform_get_drvdata(pdev);

	if (handle_ce(mci))
		return IRQ_HANDLED;
	return IRQ_NONE;
}

static enum scrub_type get_scrub_mode(void __iomem *mmio_base)
{
	u32 ecccfg0;

	ecccfg0 = readl(mmio_base + AL_MC_ECC_CFG);

	if (FIELD_GET(AL_MC_ECC_CFG_SCRUB_DISABLED, ecccfg0))
		return SCRUB_NONE;
	else
		return SCRUB_HW_SRC;
}

static void devm_al_mc_edac_free(void *data)
{
	edac_mc_free(data);
}

static void devm_al_mc_edac_del(void *data)
{
	edac_mc_del_mc(data);
}

static int al_mc_edac_probe(struct platform_device *pdev)
{
	struct edac_mc_layer layers[1];
	struct mem_ctl_info *mci;
	struct al_mc_edac *al_mc;
	void __iomem *mmio_base;
	struct dimm_info *dimm;
	int ret;

	mmio_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mmio_base)) {
		dev_err(&pdev->dev, "failed to ioremap memory (%ld)\n",
			PTR_ERR(mmio_base));
		return PTR_ERR(mmio_base);
	}

	layers[0].type = EDAC_MC_LAYER_CHIP_SELECT;
	layers[0].size = 1;
	layers[0].is_virt_csrow = false;
	mci = edac_mc_alloc(0, ARRAY_SIZE(layers), layers,
			    sizeof(struct al_mc_edac));
	if (!mci)
		return -ENOMEM;

	ret = devm_add_action_or_reset(&pdev->dev, devm_al_mc_edac_free, mci);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, mci);
	al_mc = mci->pvt_info;

	al_mc->mmio_base = mmio_base;

	al_mc->irq_ue = of_irq_get_byname(pdev->dev.of_node, "ue");
	if (al_mc->irq_ue <= 0)
		dev_dbg(&pdev->dev,
			"no IRQ defined for UE - falling back to polling\n");

	al_mc->irq_ce = of_irq_get_byname(pdev->dev.of_node, "ce");
	if (al_mc->irq_ce <= 0)
		dev_dbg(&pdev->dev,
			"no IRQ defined for CE - falling back to polling\n");

	/*
	 * In case both interrupts (ue/ce) are to be found, use interrupt mode.
	 * In case none of the interrupt are foud, use polling mode.
	 * In case only one interrupt is found, use interrupt mode for it but
	 * keep polling mode enable for the other.
	 */
	if (al_mc->irq_ue <= 0 || al_mc->irq_ce <= 0) {
		edac_op_state = EDAC_OPSTATE_POLL;
		mci->edac_check = al_mc_edac_check;
	} else {
		edac_op_state = EDAC_OPSTATE_INT;
	}

	spin_lock_init(&al_mc->lock);

	mci->mtype_cap = MEM_FLAG_DDR3 | MEM_FLAG_DDR4;
	mci->edac_ctl_cap = EDAC_FLAG_NONE | EDAC_FLAG_SECDED;
	mci->edac_cap = EDAC_FLAG_SECDED;
	mci->mod_name = DRV_NAME;
	mci->ctl_name = "al_mc";
	mci->pdev = &pdev->dev;
	mci->scrub_mode = get_scrub_mode(mmio_base);

	dimm = *mci->dimms;
	dimm->grain = 1;

	ret = edac_mc_add_mc(mci);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"fail to add memory controller device (%d)\n",
			ret);
		return ret;
	}

	ret = devm_add_action_or_reset(&pdev->dev, devm_al_mc_edac_del, &pdev->dev);
	if (ret)
		return ret;

	if (al_mc->irq_ue > 0) {
		ret = devm_request_irq(&pdev->dev,
				       al_mc->irq_ue,
				       al_mc_edac_irq_handler_ue,
				       IRQF_SHARED,
				       pdev->name,
				       pdev);
		if (ret != 0) {
			dev_err(&pdev->dev,
				"failed to request UE IRQ %d (%d)\n",
				al_mc->irq_ue, ret);
			return ret;
		}
	}

	if (al_mc->irq_ce > 0) {
		ret = devm_request_irq(&pdev->dev,
				       al_mc->irq_ce,
				       al_mc_edac_irq_handler_ce,
				       IRQF_SHARED,
				       pdev->name,
				       pdev);
		if (ret != 0) {
			dev_err(&pdev->dev,
				"failed to request CE IRQ %d (%d)\n",
				al_mc->irq_ce, ret);
			return ret;
		}
	}

	return 0;
}

static const struct of_device_id al_mc_edac_of_match[] = {
	{ .compatible = "amazon,al-mc-edac", },
	{},
};

MODULE_DEVICE_TABLE(of, al_mc_edac_of_match);

static struct platform_driver al_mc_edac_driver = {
	.probe = al_mc_edac_probe,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = al_mc_edac_of_match,
	},
};

module_platform_driver(al_mc_edac_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Talel Shenhar");
MODULE_DESCRIPTION("Amazon's Annapurna Lab's Memory Controller EDAC Driver");

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 */

#include <linux/edac.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rockchip/rockchip_sip.h>
#include <soc/rockchip/rockchip_sip.h>

#include "edac_module.h"

#define MAX_CS					(4)

#define MAX_CH					(1)

#define	RK_EDAC_MOD				"1"

/* ECCCADDR0 */
#define ECC_CORR_RANK_SHIFT			(24)
#define ECC_CORR_RANK_MASK			(0x3)
#define ECC_CORR_ROW_MASK			(0x3ffff)
/* ECCCADDR1 */
#define ECC_CORR_CID_SHIFT			(28)
#define ECC_CORR_CID_MASK			(0x3)
#define ECC_CORR_BG_SHIFT			(24)
#define ECC_CORR_BG_MASK			(0x3)
#define ECC_CORR_BANK_SHIFT			(16)
#define ECC_CORR_BANK_MASK			(0x7)
#define ECC_CORR_COL_MASK			(0xfff)
/* ECCUADDR0 */
#define ECC_UNCORR_RANK_SHIFT			(24)
#define ECC_UNCORR_RANK_MASK			(0x3)
#define ECC_UNCORR_ROW_MASK			(0x3ffff)
/* ECCUADDR1 */
#define ECC_UNCORR_CID_SHIFT			(28)
#define ECC_UNCORR_CID_MASK			(0x3)
#define ECC_UNCORR_BG_SHIFT			(24)
#define ECC_UNCORR_BG_MASK			(0x3)
#define ECC_UNCORR_BANK_SHIFT			(16)
#define ECC_UNCORR_BANK_MASK			(0x7)
#define ECC_UNCORR_COL_MASK			(0xfff)

/**
 * struct ddr_ecc_error_info - DDR ECC error log information
 * @err_cnt:	error count
 * @rank:	Rank number
 * @row:	Row number
 * @chip_id:	Chip id number
 * @bank_group:	Bank Group number
 * @bank:	Bank number
 * @col:	Column number
 * @bitpos:	Bit position
 */
struct ddr_ecc_error_info {
	u32 err_cnt;
	u32 rank;
	u32 row;
	u32 chip_id;
	u32 bank_group;
	u32 bank;
	u32 col;
	u32 bitpos;
};

/**
 * struct ddr_ecc_status - DDR ECC status information to report
 * @ceinfo:	Correctable error log information
 * @ueinfo:	Uncorrectable error log information
 */
struct ddr_ecc_status {
	struct ddr_ecc_error_info ceinfo;
	struct ddr_ecc_error_info ueinfo;
};

/**
 * struct rk_edac_priv - RK DDR memory controller private instance data
 * @name:	EDAC name
 * @stat:	DDR ECC status information
 * @ce_cnt:	Correctable Error count
 * @ue_cnt:	Uncorrectable Error count
 * @irq_ce:	Corrected interrupt number
 * @irq_ue:	Uncorrected interrupt number
 */
struct rk_edac_priv {
	char *name;
	struct ddr_ecc_status stat;
	u32 ce_cnt;
	u32 ue_cnt;
	int irq_ce;
	int irq_ue;
};

static struct ddr_ecc_status *ddr_edac_info;

static inline void opstate_init_int(void)
{
	switch (edac_op_state) {
	case EDAC_OPSTATE_POLL:
	case EDAC_OPSTATE_INT:
		break;
	default:
		edac_op_state = EDAC_OPSTATE_INT;
		break;
	}
}

static void rockchip_edac_handle_ce_error(struct mem_ctl_info *mci,
					  struct ddr_ecc_status *p)
{
	struct ddr_ecc_error_info *pinf;

	if (p->ceinfo.err_cnt) {
		pinf = &p->ceinfo;
		edac_mc_printk(mci, KERN_ERR,
			       "DDR ECC CE error: CS%d, Row 0x%x, Bg 0x%x, Bk 0x%x, Col 0x%x bit 0x%x\n",
			       pinf->rank, pinf->row, pinf->bank_group,
			       pinf->bank, pinf->col,
			       pinf->bitpos);
		edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci,
				     p->ceinfo.err_cnt, 0, 0, 0, 0, 0, -1,
				     mci->ctl_name, "");
	}
}

static void rockchip_edac_handle_ue_error(struct mem_ctl_info *mci,
					  struct ddr_ecc_status *p)
{
	struct ddr_ecc_error_info *pinf;

	if (p->ueinfo.err_cnt) {
		pinf = &p->ueinfo;
		edac_mc_printk(mci, KERN_ERR,
			       "DDR ECC UE error: CS%d, Row 0x%x, Bg 0x%x, Bk 0x%x, Col 0x%x\n",
			       pinf->rank, pinf->row,
			       pinf->bank_group, pinf->bank, pinf->col);
		edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci,
				     p->ueinfo.err_cnt, 0, 0, 0, 0, 0, -1,
				     mci->ctl_name, "");
	}
}

static int rockchip_edac_get_error_info(struct mem_ctl_info *mci)
{
	struct arm_smccc_res res;

	res = sip_smc_dram(SHARE_PAGE_TYPE_DDRECC, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_ECC);
	if ((res.a0) || (res.a1)) {
		edac_mc_printk(mci, KERN_ERR, "ROCKCHIP_SIP_CONFIG_DRAM_ECC not support: 0x%lx\n",
			       res.a0);
		return -ENXIO;
	}

	return 0;
}

static void rockchip_edac_check(struct mem_ctl_info *mci)
{
	struct rk_edac_priv *priv = mci->pvt_info;
	int ret;

	ret = rockchip_edac_get_error_info(mci);
	if (ret)
		return;

	priv->ce_cnt += ddr_edac_info->ceinfo.err_cnt;
	priv->ue_cnt += ddr_edac_info->ceinfo.err_cnt;
	rockchip_edac_handle_ce_error(mci, ddr_edac_info);
	rockchip_edac_handle_ue_error(mci, ddr_edac_info);
}

static irqreturn_t rockchip_edac_mc_ce_isr(int irq, void *dev_id)
{
	struct mem_ctl_info *mci = dev_id;
	struct rk_edac_priv *priv = mci->pvt_info;
	int ret;

	ret = rockchip_edac_get_error_info(mci);
	if (ret)
		return IRQ_NONE;

	priv->ce_cnt += ddr_edac_info->ceinfo.err_cnt;

	rockchip_edac_handle_ce_error(mci, ddr_edac_info);

	return IRQ_HANDLED;
}

static irqreturn_t rockchip_edac_mc_ue_isr(int irq, void *dev_id)
{
	struct mem_ctl_info *mci = dev_id;
	struct rk_edac_priv *priv = mci->pvt_info;
	int ret;

	ret = rockchip_edac_get_error_info(mci);
	if (ret)
		return IRQ_NONE;

	priv->ue_cnt += ddr_edac_info->ueinfo.err_cnt;

	rockchip_edac_handle_ue_error(mci, ddr_edac_info);

	return IRQ_HANDLED;
}

static int rockchip_edac_mc_init(struct mem_ctl_info *mci,
			   struct platform_device *pdev)
{
	struct rk_edac_priv *priv = mci->pvt_info;
	struct arm_smccc_res res;
	int ret;

	mci->pdev = &pdev->dev;
	dev_set_drvdata(mci->pdev, mci);
	mci->mtype_cap = MEM_FLAG_DDR3 | MEM_FLAG_DDR4;
	mci->edac_ctl_cap = EDAC_FLAG_SECDED;
	mci->scrub_cap = SCRUB_NONE;
	mci->scrub_mode = SCRUB_NONE;

	mci->edac_cap = EDAC_FLAG_SECDED;
	mci->ctl_name = priv->name;
	mci->dev_name = priv->name;
	mci->mod_name = RK_EDAC_MOD;

	if (edac_op_state == EDAC_OPSTATE_POLL)
		mci->edac_check = rockchip_edac_check;
	mci->ctl_page_to_phys = NULL;

	res = sip_smc_request_share_mem(1, SHARE_PAGE_TYPE_DDRECC);
	if (res.a0 != 0) {
		dev_err(&pdev->dev, "no ATF memory for init, ret 0x%lx\n", res.a0);
		return -ENOMEM;
	}
	ddr_edac_info = (struct ddr_ecc_status *)res.a1;
	memset(ddr_edac_info, 0, sizeof(struct ddr_ecc_status));

	ret = rockchip_edac_get_error_info(mci);
	if (ret)
		return ret;

	return 0;
}

static int rockchip_edac_probe(struct platform_device *pdev)
{
	struct mem_ctl_info *mci;
	struct edac_mc_layer layers[2];
	struct rk_edac_priv *priv;
	int ret;

	opstate_init_int();
	layers[0].type = EDAC_MC_LAYER_CHIP_SELECT;
	layers[0].size = MAX_CS;
	layers[0].is_virt_csrow = true;
	layers[1].type = EDAC_MC_LAYER_CHANNEL;
	layers[1].size = MAX_CH;
	layers[1].is_virt_csrow = false;

	mci = edac_mc_alloc(0, ARRAY_SIZE(layers), layers,
			    sizeof(struct rk_edac_priv));
	if (!mci) {
		edac_printk(KERN_ERR, EDAC_MC,
			    "Failed memory allocation for mc instance\n");
		return -ENOMEM;
	}

	priv = mci->pvt_info;
	priv->name = "rk_edac_ecc";
	ret = rockchip_edac_mc_init(mci, pdev);
	if (ret) {
		edac_printk(KERN_ERR, EDAC_MC,
			    "Failed to initialize instance\n");
		goto free_edac_mc;
	}

	ret = edac_mc_add_mc(mci);
	if (ret) {
		edac_printk(KERN_ERR, EDAC_MC,
			    "Failed edac_mc_add_mc()\n");
		goto free_edac_mc;
	}

	if (edac_op_state == EDAC_OPSTATE_INT) {
		/* register interrupts */
		priv->irq_ce = platform_get_irq_byname(pdev, "ce");
		ret = devm_request_irq(&pdev->dev, priv->irq_ce,
				       rockchip_edac_mc_ce_isr,
				       0,
				       "[EDAC] MC err", mci);
		if (ret < 0) {
			edac_printk(KERN_ERR, EDAC_MC,
				    "%s: Unable to request ce irq %d for RK EDAC\n",
				    __func__, priv->irq_ce);
			goto del_mc;
		}

		edac_printk(KERN_INFO, EDAC_MC,
			    "acquired ce irq %d for MC\n",
			    priv->irq_ce);

		priv->irq_ue = platform_get_irq_byname(pdev, "ue");
		ret = devm_request_irq(&pdev->dev, priv->irq_ue,
				       rockchip_edac_mc_ue_isr,
				       0,
				       "[EDAC] MC err", mci);
		if (ret < 0) {
			edac_printk(KERN_ERR, EDAC_MC,
				    "%s: Unable to request ue irq %d for RK EDAC\n",
				    __func__, priv->irq_ue);
			goto del_mc;
		}

		edac_printk(KERN_INFO, EDAC_MC,
			    "acquired ue irq %d for MC\n",
			    priv->irq_ue);
	}

	return 0;

del_mc:
	edac_mc_del_mc(&pdev->dev);
free_edac_mc:
	edac_mc_free(mci);

	return -ENODEV;
}

static int rockchip_edac_remove(struct platform_device *pdev)
{
	struct mem_ctl_info *mci = dev_get_drvdata(&pdev->dev);

	edac_mc_del_mc(&pdev->dev);
	edac_mc_free(mci);

	return 0;
}

static const struct of_device_id rk_ddr_mc_err_of_match[] = {
	{ .compatible = "rockchip,rk3568-edac", },
	{},
};
MODULE_DEVICE_TABLE(of, rk_ddr_mc_err_of_match);

static struct platform_driver rockchip_edac_driver = {
	.probe = rockchip_edac_probe,
	.remove = rockchip_edac_remove,
	.driver = {
		.name = "rk_edac",
		.of_match_table = rk_ddr_mc_err_of_match,
	},
};
module_platform_driver(rockchip_edac_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("He Zhihuan <huan.he@rock-chips.com>\n");
MODULE_DESCRIPTION("ROCKCHIP EDAC kernel module");

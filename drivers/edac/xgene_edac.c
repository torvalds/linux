// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * APM X-Gene SoC EDAC (error detection and correction)
 *
 * Copyright (c) 2015, Applied Micro Circuits Corporation
 * Author: Feng Kan <fkan@apm.com>
 *         Loc Ho <lho@apm.com>
 */

#include <linux/ctype.h>
#include <linux/edac.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/regmap.h>

#include "edac_module.h"

#define EDAC_MOD_STR			"xgene_edac"

/* Global error configuration status registers (CSR) */
#define PCPHPERRINTSTS			0x0000
#define PCPHPERRINTMSK			0x0004
#define  MCU_CTL_ERR_MASK		BIT(12)
#define  IOB_PA_ERR_MASK		BIT(11)
#define  IOB_BA_ERR_MASK		BIT(10)
#define  IOB_XGIC_ERR_MASK		BIT(9)
#define  IOB_RB_ERR_MASK		BIT(8)
#define  L3C_UNCORR_ERR_MASK		BIT(5)
#define  MCU_UNCORR_ERR_MASK		BIT(4)
#define  PMD3_MERR_MASK			BIT(3)
#define  PMD2_MERR_MASK			BIT(2)
#define  PMD1_MERR_MASK			BIT(1)
#define  PMD0_MERR_MASK			BIT(0)
#define PCPLPERRINTSTS			0x0008
#define PCPLPERRINTMSK			0x000C
#define  CSW_SWITCH_TRACE_ERR_MASK	BIT(2)
#define  L3C_CORR_ERR_MASK		BIT(1)
#define  MCU_CORR_ERR_MASK		BIT(0)
#define MEMERRINTSTS			0x0010
#define MEMERRINTMSK			0x0014

struct xgene_edac {
	struct device		*dev;
	struct regmap		*csw_map;
	struct regmap		*mcba_map;
	struct regmap		*mcbb_map;
	struct regmap		*efuse_map;
	struct regmap		*rb_map;
	void __iomem		*pcp_csr;
	spinlock_t		lock;
	struct dentry           *dfs;

	struct list_head	mcus;
	struct list_head	pmds;
	struct list_head	l3s;
	struct list_head	socs;

	struct mutex		mc_lock;
	int			mc_active_mask;
	int			mc_registered_mask;
};

static void xgene_edac_pcp_rd(struct xgene_edac *edac, u32 reg, u32 *val)
{
	*val = readl(edac->pcp_csr + reg);
}

static void xgene_edac_pcp_clrbits(struct xgene_edac *edac, u32 reg,
				   u32 bits_mask)
{
	u32 val;

	spin_lock(&edac->lock);
	val = readl(edac->pcp_csr + reg);
	val &= ~bits_mask;
	writel(val, edac->pcp_csr + reg);
	spin_unlock(&edac->lock);
}

static void xgene_edac_pcp_setbits(struct xgene_edac *edac, u32 reg,
				   u32 bits_mask)
{
	u32 val;

	spin_lock(&edac->lock);
	val = readl(edac->pcp_csr + reg);
	val |= bits_mask;
	writel(val, edac->pcp_csr + reg);
	spin_unlock(&edac->lock);
}

/* Memory controller error CSR */
#define MCU_MAX_RANK			8
#define MCU_RANK_STRIDE			0x40

#define MCUGECR				0x0110
#define  MCU_GECR_DEMANDUCINTREN_MASK	BIT(0)
#define  MCU_GECR_BACKUCINTREN_MASK	BIT(1)
#define  MCU_GECR_CINTREN_MASK		BIT(2)
#define  MUC_GECR_MCUADDRERREN_MASK	BIT(9)
#define MCUGESR				0x0114
#define  MCU_GESR_ADDRNOMATCH_ERR_MASK	BIT(7)
#define  MCU_GESR_ADDRMULTIMATCH_ERR_MASK	BIT(6)
#define  MCU_GESR_PHYP_ERR_MASK		BIT(3)
#define MCUESRR0			0x0314
#define  MCU_ESRR_MULTUCERR_MASK	BIT(3)
#define  MCU_ESRR_BACKUCERR_MASK	BIT(2)
#define  MCU_ESRR_DEMANDUCERR_MASK	BIT(1)
#define  MCU_ESRR_CERR_MASK		BIT(0)
#define MCUESRRA0			0x0318
#define MCUEBLRR0			0x031c
#define  MCU_EBLRR_ERRBANK_RD(src)	(((src) & 0x00000007) >> 0)
#define MCUERCRR0			0x0320
#define  MCU_ERCRR_ERRROW_RD(src)	(((src) & 0xFFFF0000) >> 16)
#define  MCU_ERCRR_ERRCOL_RD(src)	((src) & 0x00000FFF)
#define MCUSBECNT0			0x0324
#define MCU_SBECNT_COUNT(src)		((src) & 0xFFFF)

#define CSW_CSWCR			0x0000
#define  CSW_CSWCR_DUALMCB_MASK		BIT(0)

#define MCBADDRMR			0x0000
#define  MCBADDRMR_MCU_INTLV_MODE_MASK	BIT(3)
#define  MCBADDRMR_DUALMCU_MODE_MASK	BIT(2)
#define  MCBADDRMR_MCB_INTLV_MODE_MASK	BIT(1)
#define  MCBADDRMR_ADDRESS_MODE_MASK	BIT(0)

struct xgene_edac_mc_ctx {
	struct list_head	next;
	char			*name;
	struct mem_ctl_info	*mci;
	struct xgene_edac	*edac;
	void __iomem		*mcu_csr;
	u32			mcu_id;
};

static ssize_t xgene_edac_mc_err_inject_write(struct file *file,
					      const char __user *data,
					      size_t count, loff_t *ppos)
{
	struct mem_ctl_info *mci = file->private_data;
	struct xgene_edac_mc_ctx *ctx = mci->pvt_info;
	int i;

	for (i = 0; i < MCU_MAX_RANK; i++) {
		writel(MCU_ESRR_MULTUCERR_MASK | MCU_ESRR_BACKUCERR_MASK |
		       MCU_ESRR_DEMANDUCERR_MASK | MCU_ESRR_CERR_MASK,
		       ctx->mcu_csr + MCUESRRA0 + i * MCU_RANK_STRIDE);
	}
	return count;
}

static const struct file_operations xgene_edac_mc_debug_inject_fops = {
	.open = simple_open,
	.write = xgene_edac_mc_err_inject_write,
	.llseek = generic_file_llseek,
};

static void xgene_edac_mc_create_debugfs_node(struct mem_ctl_info *mci)
{
	if (!IS_ENABLED(CONFIG_EDAC_DEBUG))
		return;

	if (!mci->debugfs)
		return;

	edac_debugfs_create_file("inject_ctrl", S_IWUSR, mci->debugfs, mci,
				 &xgene_edac_mc_debug_inject_fops);
}

static void xgene_edac_mc_check(struct mem_ctl_info *mci)
{
	struct xgene_edac_mc_ctx *ctx = mci->pvt_info;
	unsigned int pcp_hp_stat;
	unsigned int pcp_lp_stat;
	u32 reg;
	u32 rank;
	u32 bank;
	u32 count;
	u32 col_row;

	xgene_edac_pcp_rd(ctx->edac, PCPHPERRINTSTS, &pcp_hp_stat);
	xgene_edac_pcp_rd(ctx->edac, PCPLPERRINTSTS, &pcp_lp_stat);
	if (!((MCU_UNCORR_ERR_MASK & pcp_hp_stat) ||
	      (MCU_CTL_ERR_MASK & pcp_hp_stat) ||
	      (MCU_CORR_ERR_MASK & pcp_lp_stat)))
		return;

	for (rank = 0; rank < MCU_MAX_RANK; rank++) {
		reg = readl(ctx->mcu_csr + MCUESRR0 + rank * MCU_RANK_STRIDE);

		/* Detect uncorrectable memory error */
		if (reg & (MCU_ESRR_DEMANDUCERR_MASK |
			   MCU_ESRR_BACKUCERR_MASK)) {
			/* Detected uncorrectable memory error */
			edac_mc_chipset_printk(mci, KERN_ERR, "X-Gene",
				"MCU uncorrectable error at rank %d\n", rank);

			edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci,
				1, 0, 0, 0, 0, 0, -1, mci->ctl_name, "");
		}

		/* Detect correctable memory error */
		if (reg & MCU_ESRR_CERR_MASK) {
			bank = readl(ctx->mcu_csr + MCUEBLRR0 +
				     rank * MCU_RANK_STRIDE);
			col_row = readl(ctx->mcu_csr + MCUERCRR0 +
					rank * MCU_RANK_STRIDE);
			count = readl(ctx->mcu_csr + MCUSBECNT0 +
				      rank * MCU_RANK_STRIDE);
			edac_mc_chipset_printk(mci, KERN_WARNING, "X-Gene",
				"MCU correctable error at rank %d bank %d column %d row %d count %d\n",
				rank, MCU_EBLRR_ERRBANK_RD(bank),
				MCU_ERCRR_ERRCOL_RD(col_row),
				MCU_ERCRR_ERRROW_RD(col_row),
				MCU_SBECNT_COUNT(count));

			edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci,
				1, 0, 0, 0, 0, 0, -1, mci->ctl_name, "");
		}

		/* Clear all error registers */
		writel(0x0, ctx->mcu_csr + MCUEBLRR0 + rank * MCU_RANK_STRIDE);
		writel(0x0, ctx->mcu_csr + MCUERCRR0 + rank * MCU_RANK_STRIDE);
		writel(0x0, ctx->mcu_csr + MCUSBECNT0 +
		       rank * MCU_RANK_STRIDE);
		writel(reg, ctx->mcu_csr + MCUESRR0 + rank * MCU_RANK_STRIDE);
	}

	/* Detect memory controller error */
	reg = readl(ctx->mcu_csr + MCUGESR);
	if (reg) {
		if (reg & MCU_GESR_ADDRNOMATCH_ERR_MASK)
			edac_mc_chipset_printk(mci, KERN_WARNING, "X-Gene",
				"MCU address miss-match error\n");
		if (reg & MCU_GESR_ADDRMULTIMATCH_ERR_MASK)
			edac_mc_chipset_printk(mci, KERN_WARNING, "X-Gene",
				"MCU address multi-match error\n");

		writel(reg, ctx->mcu_csr + MCUGESR);
	}
}

static void xgene_edac_mc_irq_ctl(struct mem_ctl_info *mci, bool enable)
{
	struct xgene_edac_mc_ctx *ctx = mci->pvt_info;
	unsigned int val;

	if (edac_op_state != EDAC_OPSTATE_INT)
		return;

	mutex_lock(&ctx->edac->mc_lock);

	/*
	 * As there is only single bit for enable error and interrupt mask,
	 * we must only enable top level interrupt after all MCUs are
	 * registered. Otherwise, if there is an error and the corresponding
	 * MCU has not registered, the interrupt will never get cleared. To
	 * determine all MCU have registered, we will keep track of active
	 * MCUs and registered MCUs.
	 */
	if (enable) {
		/* Set registered MCU bit */
		ctx->edac->mc_registered_mask |= 1 << ctx->mcu_id;

		/* Enable interrupt after all active MCU registered */
		if (ctx->edac->mc_registered_mask ==
		    ctx->edac->mc_active_mask) {
			/* Enable memory controller top level interrupt */
			xgene_edac_pcp_clrbits(ctx->edac, PCPHPERRINTMSK,
					       MCU_UNCORR_ERR_MASK |
					       MCU_CTL_ERR_MASK);
			xgene_edac_pcp_clrbits(ctx->edac, PCPLPERRINTMSK,
					       MCU_CORR_ERR_MASK);
		}

		/* Enable MCU interrupt and error reporting */
		val = readl(ctx->mcu_csr + MCUGECR);
		val |= MCU_GECR_DEMANDUCINTREN_MASK |
		       MCU_GECR_BACKUCINTREN_MASK |
		       MCU_GECR_CINTREN_MASK |
		       MUC_GECR_MCUADDRERREN_MASK;
		writel(val, ctx->mcu_csr + MCUGECR);
	} else {
		/* Disable MCU interrupt */
		val = readl(ctx->mcu_csr + MCUGECR);
		val &= ~(MCU_GECR_DEMANDUCINTREN_MASK |
			 MCU_GECR_BACKUCINTREN_MASK |
			 MCU_GECR_CINTREN_MASK |
			 MUC_GECR_MCUADDRERREN_MASK);
		writel(val, ctx->mcu_csr + MCUGECR);

		/* Disable memory controller top level interrupt */
		xgene_edac_pcp_setbits(ctx->edac, PCPHPERRINTMSK,
				       MCU_UNCORR_ERR_MASK | MCU_CTL_ERR_MASK);
		xgene_edac_pcp_setbits(ctx->edac, PCPLPERRINTMSK,
				       MCU_CORR_ERR_MASK);

		/* Clear registered MCU bit */
		ctx->edac->mc_registered_mask &= ~(1 << ctx->mcu_id);
	}

	mutex_unlock(&ctx->edac->mc_lock);
}

static int xgene_edac_mc_is_active(struct xgene_edac_mc_ctx *ctx, int mc_idx)
{
	unsigned int reg;
	u32 mcu_mask;

	if (regmap_read(ctx->edac->csw_map, CSW_CSWCR, &reg))
		return 0;

	if (reg & CSW_CSWCR_DUALMCB_MASK) {
		/*
		 * Dual MCB active - Determine if all 4 active or just MCU0
		 * and MCU2 active
		 */
		if (regmap_read(ctx->edac->mcbb_map, MCBADDRMR, &reg))
			return 0;
		mcu_mask = (reg & MCBADDRMR_DUALMCU_MODE_MASK) ? 0xF : 0x5;
	} else {
		/*
		 * Single MCB active - Determine if MCU0/MCU1 or just MCU0
		 * active
		 */
		if (regmap_read(ctx->edac->mcba_map, MCBADDRMR, &reg))
			return 0;
		mcu_mask = (reg & MCBADDRMR_DUALMCU_MODE_MASK) ? 0x3 : 0x1;
	}

	/* Save active MC mask if hasn't set already */
	if (!ctx->edac->mc_active_mask)
		ctx->edac->mc_active_mask = mcu_mask;

	return (mcu_mask & (1 << mc_idx)) ? 1 : 0;
}

static int xgene_edac_mc_add(struct xgene_edac *edac, struct device_node *np)
{
	struct mem_ctl_info *mci;
	struct edac_mc_layer layers[2];
	struct xgene_edac_mc_ctx tmp_ctx;
	struct xgene_edac_mc_ctx *ctx;
	struct resource res;
	int rc;

	memset(&tmp_ctx, 0, sizeof(tmp_ctx));
	tmp_ctx.edac = edac;

	if (!devres_open_group(edac->dev, xgene_edac_mc_add, GFP_KERNEL))
		return -ENOMEM;

	rc = of_address_to_resource(np, 0, &res);
	if (rc < 0) {
		dev_err(edac->dev, "no MCU resource address\n");
		goto err_group;
	}
	tmp_ctx.mcu_csr = devm_ioremap_resource(edac->dev, &res);
	if (IS_ERR(tmp_ctx.mcu_csr)) {
		dev_err(edac->dev, "unable to map MCU resource\n");
		rc = PTR_ERR(tmp_ctx.mcu_csr);
		goto err_group;
	}

	/* Ignore non-active MCU */
	if (of_property_read_u32(np, "memory-controller", &tmp_ctx.mcu_id)) {
		dev_err(edac->dev, "no memory-controller property\n");
		rc = -ENODEV;
		goto err_group;
	}
	if (!xgene_edac_mc_is_active(&tmp_ctx, tmp_ctx.mcu_id)) {
		rc = -ENODEV;
		goto err_group;
	}

	layers[0].type = EDAC_MC_LAYER_CHIP_SELECT;
	layers[0].size = 4;
	layers[0].is_virt_csrow = true;
	layers[1].type = EDAC_MC_LAYER_CHANNEL;
	layers[1].size = 2;
	layers[1].is_virt_csrow = false;
	mci = edac_mc_alloc(tmp_ctx.mcu_id, ARRAY_SIZE(layers), layers,
			    sizeof(*ctx));
	if (!mci) {
		rc = -ENOMEM;
		goto err_group;
	}

	ctx = mci->pvt_info;
	*ctx = tmp_ctx;		/* Copy over resource value */
	ctx->name = "xgene_edac_mc_err";
	ctx->mci = mci;
	mci->pdev = &mci->dev;
	mci->ctl_name = ctx->name;
	mci->dev_name = ctx->name;

	mci->mtype_cap = MEM_FLAG_RDDR | MEM_FLAG_RDDR2 | MEM_FLAG_RDDR3 |
			 MEM_FLAG_DDR | MEM_FLAG_DDR2 | MEM_FLAG_DDR3;
	mci->edac_ctl_cap = EDAC_FLAG_SECDED;
	mci->edac_cap = EDAC_FLAG_SECDED;
	mci->mod_name = EDAC_MOD_STR;
	mci->ctl_page_to_phys = NULL;
	mci->scrub_cap = SCRUB_FLAG_HW_SRC;
	mci->scrub_mode = SCRUB_HW_SRC;

	if (edac_op_state == EDAC_OPSTATE_POLL)
		mci->edac_check = xgene_edac_mc_check;

	if (edac_mc_add_mc(mci)) {
		dev_err(edac->dev, "edac_mc_add_mc failed\n");
		rc = -EINVAL;
		goto err_free;
	}

	xgene_edac_mc_create_debugfs_node(mci);

	list_add(&ctx->next, &edac->mcus);

	xgene_edac_mc_irq_ctl(mci, true);

	devres_remove_group(edac->dev, xgene_edac_mc_add);

	dev_info(edac->dev, "X-Gene EDAC MC registered\n");
	return 0;

err_free:
	edac_mc_free(mci);
err_group:
	devres_release_group(edac->dev, xgene_edac_mc_add);
	return rc;
}

static int xgene_edac_mc_remove(struct xgene_edac_mc_ctx *mcu)
{
	xgene_edac_mc_irq_ctl(mcu->mci, false);
	edac_mc_del_mc(&mcu->mci->dev);
	edac_mc_free(mcu->mci);
	return 0;
}

/* CPU L1/L2 error CSR */
#define MAX_CPU_PER_PMD				2
#define CPU_CSR_STRIDE				0x00100000
#define CPU_L2C_PAGE				0x000D0000
#define CPU_MEMERR_L2C_PAGE			0x000E0000
#define CPU_MEMERR_CPU_PAGE			0x000F0000

#define MEMERR_CPU_ICFECR_PAGE_OFFSET		0x0000
#define MEMERR_CPU_ICFESR_PAGE_OFFSET		0x0004
#define  MEMERR_CPU_ICFESR_ERRWAY_RD(src)	(((src) & 0xFF000000) >> 24)
#define  MEMERR_CPU_ICFESR_ERRINDEX_RD(src)	(((src) & 0x003F0000) >> 16)
#define  MEMERR_CPU_ICFESR_ERRINFO_RD(src)	(((src) & 0x0000FF00) >> 8)
#define  MEMERR_CPU_ICFESR_ERRTYPE_RD(src)	(((src) & 0x00000070) >> 4)
#define  MEMERR_CPU_ICFESR_MULTCERR_MASK	BIT(2)
#define  MEMERR_CPU_ICFESR_CERR_MASK		BIT(0)
#define MEMERR_CPU_LSUESR_PAGE_OFFSET		0x000c
#define  MEMERR_CPU_LSUESR_ERRWAY_RD(src)	(((src) & 0xFF000000) >> 24)
#define  MEMERR_CPU_LSUESR_ERRINDEX_RD(src)	(((src) & 0x003F0000) >> 16)
#define  MEMERR_CPU_LSUESR_ERRINFO_RD(src)	(((src) & 0x0000FF00) >> 8)
#define  MEMERR_CPU_LSUESR_ERRTYPE_RD(src)	(((src) & 0x00000070) >> 4)
#define  MEMERR_CPU_LSUESR_MULTCERR_MASK	BIT(2)
#define  MEMERR_CPU_LSUESR_CERR_MASK		BIT(0)
#define MEMERR_CPU_LSUECR_PAGE_OFFSET		0x0008
#define MEMERR_CPU_MMUECR_PAGE_OFFSET		0x0010
#define MEMERR_CPU_MMUESR_PAGE_OFFSET		0x0014
#define  MEMERR_CPU_MMUESR_ERRWAY_RD(src)	(((src) & 0xFF000000) >> 24)
#define  MEMERR_CPU_MMUESR_ERRINDEX_RD(src)	(((src) & 0x007F0000) >> 16)
#define  MEMERR_CPU_MMUESR_ERRINFO_RD(src)	(((src) & 0x0000FF00) >> 8)
#define  MEMERR_CPU_MMUESR_ERRREQSTR_LSU_MASK	BIT(7)
#define  MEMERR_CPU_MMUESR_ERRTYPE_RD(src)	(((src) & 0x00000070) >> 4)
#define  MEMERR_CPU_MMUESR_MULTCERR_MASK	BIT(2)
#define  MEMERR_CPU_MMUESR_CERR_MASK		BIT(0)
#define MEMERR_CPU_ICFESRA_PAGE_OFFSET		0x0804
#define MEMERR_CPU_LSUESRA_PAGE_OFFSET		0x080c
#define MEMERR_CPU_MMUESRA_PAGE_OFFSET		0x0814

#define MEMERR_L2C_L2ECR_PAGE_OFFSET		0x0000
#define MEMERR_L2C_L2ESR_PAGE_OFFSET		0x0004
#define  MEMERR_L2C_L2ESR_ERRSYN_RD(src)	(((src) & 0xFF000000) >> 24)
#define  MEMERR_L2C_L2ESR_ERRWAY_RD(src)	(((src) & 0x00FC0000) >> 18)
#define  MEMERR_L2C_L2ESR_ERRCPU_RD(src)	(((src) & 0x00020000) >> 17)
#define  MEMERR_L2C_L2ESR_ERRGROUP_RD(src)	(((src) & 0x0000E000) >> 13)
#define  MEMERR_L2C_L2ESR_ERRACTION_RD(src)	(((src) & 0x00001C00) >> 10)
#define  MEMERR_L2C_L2ESR_ERRTYPE_RD(src)	(((src) & 0x00000300) >> 8)
#define  MEMERR_L2C_L2ESR_MULTUCERR_MASK	BIT(3)
#define  MEMERR_L2C_L2ESR_MULTICERR_MASK	BIT(2)
#define  MEMERR_L2C_L2ESR_UCERR_MASK		BIT(1)
#define  MEMERR_L2C_L2ESR_ERR_MASK		BIT(0)
#define MEMERR_L2C_L2EALR_PAGE_OFFSET		0x0008
#define CPUX_L2C_L2RTOCR_PAGE_OFFSET		0x0010
#define MEMERR_L2C_L2EAHR_PAGE_OFFSET		0x000c
#define CPUX_L2C_L2RTOSR_PAGE_OFFSET		0x0014
#define  MEMERR_L2C_L2RTOSR_MULTERR_MASK	BIT(1)
#define  MEMERR_L2C_L2RTOSR_ERR_MASK		BIT(0)
#define CPUX_L2C_L2RTOALR_PAGE_OFFSET		0x0018
#define CPUX_L2C_L2RTOAHR_PAGE_OFFSET		0x001c
#define MEMERR_L2C_L2ESRA_PAGE_OFFSET		0x0804

/*
 * Processor Module Domain (PMD) context - Context for a pair of processsors.
 * Each PMD consists of 2 CPUs and a shared L2 cache. Each CPU consists of
 * its own L1 cache.
 */
struct xgene_edac_pmd_ctx {
	struct list_head	next;
	struct device		ddev;
	char			*name;
	struct xgene_edac	*edac;
	struct edac_device_ctl_info *edac_dev;
	void __iomem		*pmd_csr;
	u32			pmd;
	int			version;
};

static void xgene_edac_pmd_l1_check(struct edac_device_ctl_info *edac_dev,
				    int cpu_idx)
{
	struct xgene_edac_pmd_ctx *ctx = edac_dev->pvt_info;
	void __iomem *pg_f;
	u32 val;

	pg_f = ctx->pmd_csr + cpu_idx * CPU_CSR_STRIDE + CPU_MEMERR_CPU_PAGE;

	val = readl(pg_f + MEMERR_CPU_ICFESR_PAGE_OFFSET);
	if (!val)
		goto chk_lsu;
	dev_err(edac_dev->dev,
		"CPU%d L1 memory error ICF 0x%08X Way 0x%02X Index 0x%02X Info 0x%02X\n",
		ctx->pmd * MAX_CPU_PER_PMD + cpu_idx, val,
		MEMERR_CPU_ICFESR_ERRWAY_RD(val),
		MEMERR_CPU_ICFESR_ERRINDEX_RD(val),
		MEMERR_CPU_ICFESR_ERRINFO_RD(val));
	if (val & MEMERR_CPU_ICFESR_CERR_MASK)
		dev_err(edac_dev->dev, "One or more correctable error\n");
	if (val & MEMERR_CPU_ICFESR_MULTCERR_MASK)
		dev_err(edac_dev->dev, "Multiple correctable error\n");
	switch (MEMERR_CPU_ICFESR_ERRTYPE_RD(val)) {
	case 1:
		dev_err(edac_dev->dev, "L1 TLB multiple hit\n");
		break;
	case 2:
		dev_err(edac_dev->dev, "Way select multiple hit\n");
		break;
	case 3:
		dev_err(edac_dev->dev, "Physical tag parity error\n");
		break;
	case 4:
	case 5:
		dev_err(edac_dev->dev, "L1 data parity error\n");
		break;
	case 6:
		dev_err(edac_dev->dev, "L1 pre-decode parity error\n");
		break;
	}

	/* Clear any HW errors */
	writel(val, pg_f + MEMERR_CPU_ICFESR_PAGE_OFFSET);

	if (val & (MEMERR_CPU_ICFESR_CERR_MASK |
		   MEMERR_CPU_ICFESR_MULTCERR_MASK))
		edac_device_handle_ce(edac_dev, 0, 0, edac_dev->ctl_name);

chk_lsu:
	val = readl(pg_f + MEMERR_CPU_LSUESR_PAGE_OFFSET);
	if (!val)
		goto chk_mmu;
	dev_err(edac_dev->dev,
		"CPU%d memory error LSU 0x%08X Way 0x%02X Index 0x%02X Info 0x%02X\n",
		ctx->pmd * MAX_CPU_PER_PMD + cpu_idx, val,
		MEMERR_CPU_LSUESR_ERRWAY_RD(val),
		MEMERR_CPU_LSUESR_ERRINDEX_RD(val),
		MEMERR_CPU_LSUESR_ERRINFO_RD(val));
	if (val & MEMERR_CPU_LSUESR_CERR_MASK)
		dev_err(edac_dev->dev, "One or more correctable error\n");
	if (val & MEMERR_CPU_LSUESR_MULTCERR_MASK)
		dev_err(edac_dev->dev, "Multiple correctable error\n");
	switch (MEMERR_CPU_LSUESR_ERRTYPE_RD(val)) {
	case 0:
		dev_err(edac_dev->dev, "Load tag error\n");
		break;
	case 1:
		dev_err(edac_dev->dev, "Load data error\n");
		break;
	case 2:
		dev_err(edac_dev->dev, "WSL multihit error\n");
		break;
	case 3:
		dev_err(edac_dev->dev, "Store tag error\n");
		break;
	case 4:
		dev_err(edac_dev->dev,
			"DTB multihit from load pipeline error\n");
		break;
	case 5:
		dev_err(edac_dev->dev,
			"DTB multihit from store pipeline error\n");
		break;
	}

	/* Clear any HW errors */
	writel(val, pg_f + MEMERR_CPU_LSUESR_PAGE_OFFSET);

	if (val & (MEMERR_CPU_LSUESR_CERR_MASK |
		   MEMERR_CPU_LSUESR_MULTCERR_MASK))
		edac_device_handle_ce(edac_dev, 0, 0, edac_dev->ctl_name);

chk_mmu:
	val = readl(pg_f + MEMERR_CPU_MMUESR_PAGE_OFFSET);
	if (!val)
		return;
	dev_err(edac_dev->dev,
		"CPU%d memory error MMU 0x%08X Way 0x%02X Index 0x%02X Info 0x%02X %s\n",
		ctx->pmd * MAX_CPU_PER_PMD + cpu_idx, val,
		MEMERR_CPU_MMUESR_ERRWAY_RD(val),
		MEMERR_CPU_MMUESR_ERRINDEX_RD(val),
		MEMERR_CPU_MMUESR_ERRINFO_RD(val),
		val & MEMERR_CPU_MMUESR_ERRREQSTR_LSU_MASK ? "LSU" : "ICF");
	if (val & MEMERR_CPU_MMUESR_CERR_MASK)
		dev_err(edac_dev->dev, "One or more correctable error\n");
	if (val & MEMERR_CPU_MMUESR_MULTCERR_MASK)
		dev_err(edac_dev->dev, "Multiple correctable error\n");
	switch (MEMERR_CPU_MMUESR_ERRTYPE_RD(val)) {
	case 0:
		dev_err(edac_dev->dev, "Stage 1 UTB hit error\n");
		break;
	case 1:
		dev_err(edac_dev->dev, "Stage 1 UTB miss error\n");
		break;
	case 2:
		dev_err(edac_dev->dev, "Stage 1 UTB allocate error\n");
		break;
	case 3:
		dev_err(edac_dev->dev, "TMO operation single bank error\n");
		break;
	case 4:
		dev_err(edac_dev->dev, "Stage 2 UTB error\n");
		break;
	case 5:
		dev_err(edac_dev->dev, "Stage 2 UTB miss error\n");
		break;
	case 6:
		dev_err(edac_dev->dev, "Stage 2 UTB allocate error\n");
		break;
	case 7:
		dev_err(edac_dev->dev, "TMO operation multiple bank error\n");
		break;
	}

	/* Clear any HW errors */
	writel(val, pg_f + MEMERR_CPU_MMUESR_PAGE_OFFSET);

	edac_device_handle_ce(edac_dev, 0, 0, edac_dev->ctl_name);
}

static void xgene_edac_pmd_l2_check(struct edac_device_ctl_info *edac_dev)
{
	struct xgene_edac_pmd_ctx *ctx = edac_dev->pvt_info;
	void __iomem *pg_d;
	void __iomem *pg_e;
	u32 val_hi;
	u32 val_lo;
	u32 val;

	/* Check L2 */
	pg_e = ctx->pmd_csr + CPU_MEMERR_L2C_PAGE;
	val = readl(pg_e + MEMERR_L2C_L2ESR_PAGE_OFFSET);
	if (!val)
		goto chk_l2c;
	val_lo = readl(pg_e + MEMERR_L2C_L2EALR_PAGE_OFFSET);
	val_hi = readl(pg_e + MEMERR_L2C_L2EAHR_PAGE_OFFSET);
	dev_err(edac_dev->dev,
		"PMD%d memory error L2C L2ESR 0x%08X @ 0x%08X.%08X\n",
		ctx->pmd, val, val_hi, val_lo);
	dev_err(edac_dev->dev,
		"ErrSyndrome 0x%02X ErrWay 0x%02X ErrCpu %d ErrGroup 0x%02X ErrAction 0x%02X\n",
		MEMERR_L2C_L2ESR_ERRSYN_RD(val),
		MEMERR_L2C_L2ESR_ERRWAY_RD(val),
		MEMERR_L2C_L2ESR_ERRCPU_RD(val),
		MEMERR_L2C_L2ESR_ERRGROUP_RD(val),
		MEMERR_L2C_L2ESR_ERRACTION_RD(val));

	if (val & MEMERR_L2C_L2ESR_ERR_MASK)
		dev_err(edac_dev->dev, "One or more correctable error\n");
	if (val & MEMERR_L2C_L2ESR_MULTICERR_MASK)
		dev_err(edac_dev->dev, "Multiple correctable error\n");
	if (val & MEMERR_L2C_L2ESR_UCERR_MASK)
		dev_err(edac_dev->dev, "One or more uncorrectable error\n");
	if (val & MEMERR_L2C_L2ESR_MULTUCERR_MASK)
		dev_err(edac_dev->dev, "Multiple uncorrectable error\n");

	switch (MEMERR_L2C_L2ESR_ERRTYPE_RD(val)) {
	case 0:
		dev_err(edac_dev->dev, "Outbound SDB parity error\n");
		break;
	case 1:
		dev_err(edac_dev->dev, "Inbound SDB parity error\n");
		break;
	case 2:
		dev_err(edac_dev->dev, "Tag ECC error\n");
		break;
	case 3:
		dev_err(edac_dev->dev, "Data ECC error\n");
		break;
	}

	/* Clear any HW errors */
	writel(val, pg_e + MEMERR_L2C_L2ESR_PAGE_OFFSET);

	if (val & (MEMERR_L2C_L2ESR_ERR_MASK |
		   MEMERR_L2C_L2ESR_MULTICERR_MASK))
		edac_device_handle_ce(edac_dev, 0, 0, edac_dev->ctl_name);
	if (val & (MEMERR_L2C_L2ESR_UCERR_MASK |
		   MEMERR_L2C_L2ESR_MULTUCERR_MASK))
		edac_device_handle_ue(edac_dev, 0, 0, edac_dev->ctl_name);

chk_l2c:
	/* Check if any memory request timed out on L2 cache */
	pg_d = ctx->pmd_csr + CPU_L2C_PAGE;
	val = readl(pg_d + CPUX_L2C_L2RTOSR_PAGE_OFFSET);
	if (val) {
		val_lo = readl(pg_d + CPUX_L2C_L2RTOALR_PAGE_OFFSET);
		val_hi = readl(pg_d + CPUX_L2C_L2RTOAHR_PAGE_OFFSET);
		dev_err(edac_dev->dev,
			"PMD%d L2C error L2C RTOSR 0x%08X @ 0x%08X.%08X\n",
			ctx->pmd, val, val_hi, val_lo);
		writel(val, pg_d + CPUX_L2C_L2RTOSR_PAGE_OFFSET);
	}
}

static void xgene_edac_pmd_check(struct edac_device_ctl_info *edac_dev)
{
	struct xgene_edac_pmd_ctx *ctx = edac_dev->pvt_info;
	unsigned int pcp_hp_stat;
	int i;

	xgene_edac_pcp_rd(ctx->edac, PCPHPERRINTSTS, &pcp_hp_stat);
	if (!((PMD0_MERR_MASK << ctx->pmd) & pcp_hp_stat))
		return;

	/* Check CPU L1 error */
	for (i = 0; i < MAX_CPU_PER_PMD; i++)
		xgene_edac_pmd_l1_check(edac_dev, i);

	/* Check CPU L2 error */
	xgene_edac_pmd_l2_check(edac_dev);
}

static void xgene_edac_pmd_cpu_hw_cfg(struct edac_device_ctl_info *edac_dev,
				      int cpu)
{
	struct xgene_edac_pmd_ctx *ctx = edac_dev->pvt_info;
	void __iomem *pg_f = ctx->pmd_csr + cpu * CPU_CSR_STRIDE +
			     CPU_MEMERR_CPU_PAGE;

	/*
	 * Enable CPU memory error:
	 *  MEMERR_CPU_ICFESRA, MEMERR_CPU_LSUESRA, and MEMERR_CPU_MMUESRA
	 */
	writel(0x00000301, pg_f + MEMERR_CPU_ICFECR_PAGE_OFFSET);
	writel(0x00000301, pg_f + MEMERR_CPU_LSUECR_PAGE_OFFSET);
	writel(0x00000101, pg_f + MEMERR_CPU_MMUECR_PAGE_OFFSET);
}

static void xgene_edac_pmd_hw_cfg(struct edac_device_ctl_info *edac_dev)
{
	struct xgene_edac_pmd_ctx *ctx = edac_dev->pvt_info;
	void __iomem *pg_d = ctx->pmd_csr + CPU_L2C_PAGE;
	void __iomem *pg_e = ctx->pmd_csr + CPU_MEMERR_L2C_PAGE;

	/* Enable PMD memory error - MEMERR_L2C_L2ECR and L2C_L2RTOCR */
	writel(0x00000703, pg_e + MEMERR_L2C_L2ECR_PAGE_OFFSET);
	/* Configure L2C HW request time out feature if supported */
	if (ctx->version > 1)
		writel(0x00000119, pg_d + CPUX_L2C_L2RTOCR_PAGE_OFFSET);
}

static void xgene_edac_pmd_hw_ctl(struct edac_device_ctl_info *edac_dev,
				  bool enable)
{
	struct xgene_edac_pmd_ctx *ctx = edac_dev->pvt_info;
	int i;

	/* Enable PMD error interrupt */
	if (edac_dev->op_state == OP_RUNNING_INTERRUPT) {
		if (enable)
			xgene_edac_pcp_clrbits(ctx->edac, PCPHPERRINTMSK,
					       PMD0_MERR_MASK << ctx->pmd);
		else
			xgene_edac_pcp_setbits(ctx->edac, PCPHPERRINTMSK,
					       PMD0_MERR_MASK << ctx->pmd);
	}

	if (enable) {
		xgene_edac_pmd_hw_cfg(edac_dev);

		/* Two CPUs per a PMD */
		for (i = 0; i < MAX_CPU_PER_PMD; i++)
			xgene_edac_pmd_cpu_hw_cfg(edac_dev, i);
	}
}

static ssize_t xgene_edac_pmd_l1_inject_ctrl_write(struct file *file,
						   const char __user *data,
						   size_t count, loff_t *ppos)
{
	struct edac_device_ctl_info *edac_dev = file->private_data;
	struct xgene_edac_pmd_ctx *ctx = edac_dev->pvt_info;
	void __iomem *cpux_pg_f;
	int i;

	for (i = 0; i < MAX_CPU_PER_PMD; i++) {
		cpux_pg_f = ctx->pmd_csr + i * CPU_CSR_STRIDE +
			    CPU_MEMERR_CPU_PAGE;

		writel(MEMERR_CPU_ICFESR_MULTCERR_MASK |
		       MEMERR_CPU_ICFESR_CERR_MASK,
		       cpux_pg_f + MEMERR_CPU_ICFESRA_PAGE_OFFSET);
		writel(MEMERR_CPU_LSUESR_MULTCERR_MASK |
		       MEMERR_CPU_LSUESR_CERR_MASK,
		       cpux_pg_f + MEMERR_CPU_LSUESRA_PAGE_OFFSET);
		writel(MEMERR_CPU_MMUESR_MULTCERR_MASK |
		       MEMERR_CPU_MMUESR_CERR_MASK,
		       cpux_pg_f + MEMERR_CPU_MMUESRA_PAGE_OFFSET);
	}
	return count;
}

static ssize_t xgene_edac_pmd_l2_inject_ctrl_write(struct file *file,
						   const char __user *data,
						   size_t count, loff_t *ppos)
{
	struct edac_device_ctl_info *edac_dev = file->private_data;
	struct xgene_edac_pmd_ctx *ctx = edac_dev->pvt_info;
	void __iomem *pg_e = ctx->pmd_csr + CPU_MEMERR_L2C_PAGE;

	writel(MEMERR_L2C_L2ESR_MULTUCERR_MASK |
	       MEMERR_L2C_L2ESR_MULTICERR_MASK |
	       MEMERR_L2C_L2ESR_UCERR_MASK |
	       MEMERR_L2C_L2ESR_ERR_MASK,
	       pg_e + MEMERR_L2C_L2ESRA_PAGE_OFFSET);
	return count;
}

static const struct file_operations xgene_edac_pmd_debug_inject_fops[] = {
	{
	.open = simple_open,
	.write = xgene_edac_pmd_l1_inject_ctrl_write,
	.llseek = generic_file_llseek, },
	{
	.open = simple_open,
	.write = xgene_edac_pmd_l2_inject_ctrl_write,
	.llseek = generic_file_llseek, },
	{ }
};

static void
xgene_edac_pmd_create_debugfs_nodes(struct edac_device_ctl_info *edac_dev)
{
	struct xgene_edac_pmd_ctx *ctx = edac_dev->pvt_info;
	struct dentry *dbgfs_dir;
	char name[10];

	if (!IS_ENABLED(CONFIG_EDAC_DEBUG) || !ctx->edac->dfs)
		return;

	snprintf(name, sizeof(name), "PMD%d", ctx->pmd);
	dbgfs_dir = edac_debugfs_create_dir_at(name, ctx->edac->dfs);
	if (!dbgfs_dir)
		return;

	edac_debugfs_create_file("l1_inject_ctrl", S_IWUSR, dbgfs_dir, edac_dev,
				 &xgene_edac_pmd_debug_inject_fops[0]);
	edac_debugfs_create_file("l2_inject_ctrl", S_IWUSR, dbgfs_dir, edac_dev,
				 &xgene_edac_pmd_debug_inject_fops[1]);
}

static int xgene_edac_pmd_available(u32 efuse, int pmd)
{
	return (efuse & (1 << pmd)) ? 0 : 1;
}

static int xgene_edac_pmd_add(struct xgene_edac *edac, struct device_node *np,
			      int version)
{
	struct edac_device_ctl_info *edac_dev;
	struct xgene_edac_pmd_ctx *ctx;
	struct resource res;
	char edac_name[10];
	u32 pmd;
	int rc;
	u32 val;

	if (!devres_open_group(edac->dev, xgene_edac_pmd_add, GFP_KERNEL))
		return -ENOMEM;

	/* Determine if this PMD is disabled */
	if (of_property_read_u32(np, "pmd-controller", &pmd)) {
		dev_err(edac->dev, "no pmd-controller property\n");
		rc = -ENODEV;
		goto err_group;
	}
	rc = regmap_read(edac->efuse_map, 0, &val);
	if (rc)
		goto err_group;
	if (!xgene_edac_pmd_available(val, pmd)) {
		rc = -ENODEV;
		goto err_group;
	}

	snprintf(edac_name, sizeof(edac_name), "l2c%d", pmd);
	edac_dev = edac_device_alloc_ctl_info(sizeof(*ctx),
					      edac_name, 1, "l2c", 1, 2, NULL,
					      0, edac_device_alloc_index());
	if (!edac_dev) {
		rc = -ENOMEM;
		goto err_group;
	}

	ctx = edac_dev->pvt_info;
	ctx->name = "xgene_pmd_err";
	ctx->pmd = pmd;
	ctx->edac = edac;
	ctx->edac_dev = edac_dev;
	ctx->ddev = *edac->dev;
	ctx->version = version;
	edac_dev->dev = &ctx->ddev;
	edac_dev->ctl_name = ctx->name;
	edac_dev->dev_name = ctx->name;
	edac_dev->mod_name = EDAC_MOD_STR;

	rc = of_address_to_resource(np, 0, &res);
	if (rc < 0) {
		dev_err(edac->dev, "no PMD resource address\n");
		goto err_free;
	}
	ctx->pmd_csr = devm_ioremap_resource(edac->dev, &res);
	if (IS_ERR(ctx->pmd_csr)) {
		dev_err(edac->dev,
			"devm_ioremap_resource failed for PMD resource address\n");
		rc = PTR_ERR(ctx->pmd_csr);
		goto err_free;
	}

	if (edac_op_state == EDAC_OPSTATE_POLL)
		edac_dev->edac_check = xgene_edac_pmd_check;

	xgene_edac_pmd_create_debugfs_nodes(edac_dev);

	rc = edac_device_add_device(edac_dev);
	if (rc > 0) {
		dev_err(edac->dev, "edac_device_add_device failed\n");
		rc = -ENOMEM;
		goto err_free;
	}

	if (edac_op_state == EDAC_OPSTATE_INT)
		edac_dev->op_state = OP_RUNNING_INTERRUPT;

	list_add(&ctx->next, &edac->pmds);

	xgene_edac_pmd_hw_ctl(edac_dev, 1);

	devres_remove_group(edac->dev, xgene_edac_pmd_add);

	dev_info(edac->dev, "X-Gene EDAC PMD%d registered\n", ctx->pmd);
	return 0;

err_free:
	edac_device_free_ctl_info(edac_dev);
err_group:
	devres_release_group(edac->dev, xgene_edac_pmd_add);
	return rc;
}

static int xgene_edac_pmd_remove(struct xgene_edac_pmd_ctx *pmd)
{
	struct edac_device_ctl_info *edac_dev = pmd->edac_dev;

	xgene_edac_pmd_hw_ctl(edac_dev, 0);
	edac_device_del_device(edac_dev->dev);
	edac_device_free_ctl_info(edac_dev);
	return 0;
}

/* L3 Error device */
#define L3C_ESR				(0x0A * 4)
#define  L3C_ESR_DATATAG_MASK		BIT(9)
#define  L3C_ESR_MULTIHIT_MASK		BIT(8)
#define  L3C_ESR_UCEVICT_MASK		BIT(6)
#define  L3C_ESR_MULTIUCERR_MASK	BIT(5)
#define  L3C_ESR_MULTICERR_MASK		BIT(4)
#define  L3C_ESR_UCERR_MASK		BIT(3)
#define  L3C_ESR_CERR_MASK		BIT(2)
#define  L3C_ESR_UCERRINTR_MASK		BIT(1)
#define  L3C_ESR_CERRINTR_MASK		BIT(0)
#define L3C_ECR				(0x0B * 4)
#define  L3C_ECR_UCINTREN		BIT(3)
#define  L3C_ECR_CINTREN		BIT(2)
#define  L3C_UCERREN			BIT(1)
#define  L3C_CERREN			BIT(0)
#define L3C_ELR				(0x0C * 4)
#define  L3C_ELR_ERRSYN(src)		((src & 0xFF800000) >> 23)
#define  L3C_ELR_ERRWAY(src)		((src & 0x007E0000) >> 17)
#define  L3C_ELR_AGENTID(src)		((src & 0x0001E000) >> 13)
#define  L3C_ELR_ERRGRP(src)		((src & 0x00000F00) >> 8)
#define  L3C_ELR_OPTYPE(src)		((src & 0x000000F0) >> 4)
#define  L3C_ELR_PADDRHIGH(src)		(src & 0x0000000F)
#define L3C_AELR			(0x0D * 4)
#define L3C_BELR			(0x0E * 4)
#define  L3C_BELR_BANK(src)		(src & 0x0000000F)

struct xgene_edac_dev_ctx {
	struct list_head	next;
	struct device		ddev;
	char			*name;
	struct xgene_edac	*edac;
	struct edac_device_ctl_info *edac_dev;
	int			edac_idx;
	void __iomem		*dev_csr;
	int			version;
};

/*
 * Version 1 of the L3 controller has broken single bit correctable logic for
 * certain error syndromes. Log them as uncorrectable in that case.
 */
static bool xgene_edac_l3_promote_to_uc_err(u32 l3cesr, u32 l3celr)
{
	if (l3cesr & L3C_ESR_DATATAG_MASK) {
		switch (L3C_ELR_ERRSYN(l3celr)) {
		case 0x13C:
		case 0x0B4:
		case 0x007:
		case 0x00D:
		case 0x00E:
		case 0x019:
		case 0x01A:
		case 0x01C:
		case 0x04E:
		case 0x041:
			return true;
		}
	} else if (L3C_ELR_ERRWAY(l3celr) == 9)
		return true;

	return false;
}

static void xgene_edac_l3_check(struct edac_device_ctl_info *edac_dev)
{
	struct xgene_edac_dev_ctx *ctx = edac_dev->pvt_info;
	u32 l3cesr;
	u32 l3celr;
	u32 l3caelr;
	u32 l3cbelr;

	l3cesr = readl(ctx->dev_csr + L3C_ESR);
	if (!(l3cesr & (L3C_ESR_UCERR_MASK | L3C_ESR_CERR_MASK)))
		return;

	if (l3cesr & L3C_ESR_UCERR_MASK)
		dev_err(edac_dev->dev, "L3C uncorrectable error\n");
	if (l3cesr & L3C_ESR_CERR_MASK)
		dev_warn(edac_dev->dev, "L3C correctable error\n");

	l3celr = readl(ctx->dev_csr + L3C_ELR);
	l3caelr = readl(ctx->dev_csr + L3C_AELR);
	l3cbelr = readl(ctx->dev_csr + L3C_BELR);
	if (l3cesr & L3C_ESR_MULTIHIT_MASK)
		dev_err(edac_dev->dev, "L3C multiple hit error\n");
	if (l3cesr & L3C_ESR_UCEVICT_MASK)
		dev_err(edac_dev->dev,
			"L3C dropped eviction of line with error\n");
	if (l3cesr & L3C_ESR_MULTIUCERR_MASK)
		dev_err(edac_dev->dev, "L3C multiple uncorrectable error\n");
	if (l3cesr & L3C_ESR_DATATAG_MASK)
		dev_err(edac_dev->dev,
			"L3C data error syndrome 0x%X group 0x%X\n",
			L3C_ELR_ERRSYN(l3celr), L3C_ELR_ERRGRP(l3celr));
	else
		dev_err(edac_dev->dev,
			"L3C tag error syndrome 0x%X Way of Tag 0x%X Agent ID 0x%X Operation type 0x%X\n",
			L3C_ELR_ERRSYN(l3celr), L3C_ELR_ERRWAY(l3celr),
			L3C_ELR_AGENTID(l3celr), L3C_ELR_OPTYPE(l3celr));
	/*
	 * NOTE: Address [41:38] in L3C_ELR_PADDRHIGH(l3celr).
	 *       Address [37:6] in l3caelr. Lower 6 bits are zero.
	 */
	dev_err(edac_dev->dev, "L3C error address 0x%08X.%08X bank %d\n",
		L3C_ELR_PADDRHIGH(l3celr) << 6 | (l3caelr >> 26),
		(l3caelr & 0x3FFFFFFF) << 6, L3C_BELR_BANK(l3cbelr));
	dev_err(edac_dev->dev,
		"L3C error status register value 0x%X\n", l3cesr);

	/* Clear L3C error interrupt */
	writel(0, ctx->dev_csr + L3C_ESR);

	if (ctx->version <= 1 &&
	    xgene_edac_l3_promote_to_uc_err(l3cesr, l3celr)) {
		edac_device_handle_ue(edac_dev, 0, 0, edac_dev->ctl_name);
		return;
	}
	if (l3cesr & L3C_ESR_CERR_MASK)
		edac_device_handle_ce(edac_dev, 0, 0, edac_dev->ctl_name);
	if (l3cesr & L3C_ESR_UCERR_MASK)
		edac_device_handle_ue(edac_dev, 0, 0, edac_dev->ctl_name);
}

static void xgene_edac_l3_hw_init(struct edac_device_ctl_info *edac_dev,
				  bool enable)
{
	struct xgene_edac_dev_ctx *ctx = edac_dev->pvt_info;
	u32 val;

	val = readl(ctx->dev_csr + L3C_ECR);
	val |= L3C_UCERREN | L3C_CERREN;
	/* On disable, we just disable interrupt but keep error enabled */
	if (edac_dev->op_state == OP_RUNNING_INTERRUPT) {
		if (enable)
			val |= L3C_ECR_UCINTREN | L3C_ECR_CINTREN;
		else
			val &= ~(L3C_ECR_UCINTREN | L3C_ECR_CINTREN);
	}
	writel(val, ctx->dev_csr + L3C_ECR);

	if (edac_dev->op_state == OP_RUNNING_INTERRUPT) {
		/* Enable/disable L3 error top level interrupt */
		if (enable) {
			xgene_edac_pcp_clrbits(ctx->edac, PCPHPERRINTMSK,
					       L3C_UNCORR_ERR_MASK);
			xgene_edac_pcp_clrbits(ctx->edac, PCPLPERRINTMSK,
					       L3C_CORR_ERR_MASK);
		} else {
			xgene_edac_pcp_setbits(ctx->edac, PCPHPERRINTMSK,
					       L3C_UNCORR_ERR_MASK);
			xgene_edac_pcp_setbits(ctx->edac, PCPLPERRINTMSK,
					       L3C_CORR_ERR_MASK);
		}
	}
}

static ssize_t xgene_edac_l3_inject_ctrl_write(struct file *file,
					       const char __user *data,
					       size_t count, loff_t *ppos)
{
	struct edac_device_ctl_info *edac_dev = file->private_data;
	struct xgene_edac_dev_ctx *ctx = edac_dev->pvt_info;

	/* Generate all errors */
	writel(0xFFFFFFFF, ctx->dev_csr + L3C_ESR);
	return count;
}

static const struct file_operations xgene_edac_l3_debug_inject_fops = {
	.open = simple_open,
	.write = xgene_edac_l3_inject_ctrl_write,
	.llseek = generic_file_llseek
};

static void
xgene_edac_l3_create_debugfs_nodes(struct edac_device_ctl_info *edac_dev)
{
	struct xgene_edac_dev_ctx *ctx = edac_dev->pvt_info;
	struct dentry *dbgfs_dir;
	char name[10];

	if (!IS_ENABLED(CONFIG_EDAC_DEBUG) || !ctx->edac->dfs)
		return;

	snprintf(name, sizeof(name), "l3c%d", ctx->edac_idx);
	dbgfs_dir = edac_debugfs_create_dir_at(name, ctx->edac->dfs);
	if (!dbgfs_dir)
		return;

	debugfs_create_file("l3_inject_ctrl", S_IWUSR, dbgfs_dir, edac_dev,
			    &xgene_edac_l3_debug_inject_fops);
}

static int xgene_edac_l3_add(struct xgene_edac *edac, struct device_node *np,
			     int version)
{
	struct edac_device_ctl_info *edac_dev;
	struct xgene_edac_dev_ctx *ctx;
	struct resource res;
	void __iomem *dev_csr;
	int edac_idx;
	int rc = 0;

	if (!devres_open_group(edac->dev, xgene_edac_l3_add, GFP_KERNEL))
		return -ENOMEM;

	rc = of_address_to_resource(np, 0, &res);
	if (rc < 0) {
		dev_err(edac->dev, "no L3 resource address\n");
		goto err_release_group;
	}
	dev_csr = devm_ioremap_resource(edac->dev, &res);
	if (IS_ERR(dev_csr)) {
		dev_err(edac->dev,
			"devm_ioremap_resource failed for L3 resource address\n");
		rc = PTR_ERR(dev_csr);
		goto err_release_group;
	}

	edac_idx = edac_device_alloc_index();
	edac_dev = edac_device_alloc_ctl_info(sizeof(*ctx),
					      "l3c", 1, "l3c", 1, 0, NULL, 0,
					      edac_idx);
	if (!edac_dev) {
		rc = -ENOMEM;
		goto err_release_group;
	}

	ctx = edac_dev->pvt_info;
	ctx->dev_csr = dev_csr;
	ctx->name = "xgene_l3_err";
	ctx->edac_idx = edac_idx;
	ctx->edac = edac;
	ctx->edac_dev = edac_dev;
	ctx->ddev = *edac->dev;
	ctx->version = version;
	edac_dev->dev = &ctx->ddev;
	edac_dev->ctl_name = ctx->name;
	edac_dev->dev_name = ctx->name;
	edac_dev->mod_name = EDAC_MOD_STR;

	if (edac_op_state == EDAC_OPSTATE_POLL)
		edac_dev->edac_check = xgene_edac_l3_check;

	xgene_edac_l3_create_debugfs_nodes(edac_dev);

	rc = edac_device_add_device(edac_dev);
	if (rc > 0) {
		dev_err(edac->dev, "failed edac_device_add_device()\n");
		rc = -ENOMEM;
		goto err_ctl_free;
	}

	if (edac_op_state == EDAC_OPSTATE_INT)
		edac_dev->op_state = OP_RUNNING_INTERRUPT;

	list_add(&ctx->next, &edac->l3s);

	xgene_edac_l3_hw_init(edac_dev, 1);

	devres_remove_group(edac->dev, xgene_edac_l3_add);

	dev_info(edac->dev, "X-Gene EDAC L3 registered\n");
	return 0;

err_ctl_free:
	edac_device_free_ctl_info(edac_dev);
err_release_group:
	devres_release_group(edac->dev, xgene_edac_l3_add);
	return rc;
}

static int xgene_edac_l3_remove(struct xgene_edac_dev_ctx *l3)
{
	struct edac_device_ctl_info *edac_dev = l3->edac_dev;

	xgene_edac_l3_hw_init(edac_dev, 0);
	edac_device_del_device(l3->edac->dev);
	edac_device_free_ctl_info(edac_dev);
	return 0;
}

/* SoC error device */
#define IOBAXIS0TRANSERRINTSTS		0x0000
#define  IOBAXIS0_M_ILLEGAL_ACCESS_MASK	BIT(1)
#define  IOBAXIS0_ILLEGAL_ACCESS_MASK	BIT(0)
#define IOBAXIS0TRANSERRINTMSK		0x0004
#define IOBAXIS0TRANSERRREQINFOL	0x0008
#define IOBAXIS0TRANSERRREQINFOH	0x000c
#define  REQTYPE_RD(src)		(((src) & BIT(0)))
#define  ERRADDRH_RD(src)		(((src) & 0xffc00000) >> 22)
#define IOBAXIS1TRANSERRINTSTS		0x0010
#define IOBAXIS1TRANSERRINTMSK		0x0014
#define IOBAXIS1TRANSERRREQINFOL	0x0018
#define IOBAXIS1TRANSERRREQINFOH	0x001c
#define IOBPATRANSERRINTSTS		0x0020
#define  IOBPA_M_REQIDRAM_CORRUPT_MASK	BIT(7)
#define  IOBPA_REQIDRAM_CORRUPT_MASK	BIT(6)
#define  IOBPA_M_TRANS_CORRUPT_MASK	BIT(5)
#define  IOBPA_TRANS_CORRUPT_MASK	BIT(4)
#define  IOBPA_M_WDATA_CORRUPT_MASK	BIT(3)
#define  IOBPA_WDATA_CORRUPT_MASK	BIT(2)
#define  IOBPA_M_RDATA_CORRUPT_MASK	BIT(1)
#define  IOBPA_RDATA_CORRUPT_MASK	BIT(0)
#define IOBBATRANSERRINTSTS		0x0030
#define  M_ILLEGAL_ACCESS_MASK		BIT(15)
#define  ILLEGAL_ACCESS_MASK		BIT(14)
#define  M_WIDRAM_CORRUPT_MASK		BIT(13)
#define  WIDRAM_CORRUPT_MASK		BIT(12)
#define  M_RIDRAM_CORRUPT_MASK		BIT(11)
#define  RIDRAM_CORRUPT_MASK		BIT(10)
#define  M_TRANS_CORRUPT_MASK		BIT(9)
#define  TRANS_CORRUPT_MASK		BIT(8)
#define  M_WDATA_CORRUPT_MASK		BIT(7)
#define  WDATA_CORRUPT_MASK		BIT(6)
#define  M_RBM_POISONED_REQ_MASK	BIT(5)
#define  RBM_POISONED_REQ_MASK		BIT(4)
#define  M_XGIC_POISONED_REQ_MASK	BIT(3)
#define  XGIC_POISONED_REQ_MASK		BIT(2)
#define  M_WRERR_RESP_MASK		BIT(1)
#define  WRERR_RESP_MASK		BIT(0)
#define IOBBATRANSERRREQINFOL		0x0038
#define IOBBATRANSERRREQINFOH		0x003c
#define  REQTYPE_F2_RD(src)		((src) & BIT(0))
#define  ERRADDRH_F2_RD(src)		(((src) & 0xffc00000) >> 22)
#define IOBBATRANSERRCSWREQID		0x0040
#define XGICTRANSERRINTSTS		0x0050
#define  M_WR_ACCESS_ERR_MASK		BIT(3)
#define  WR_ACCESS_ERR_MASK		BIT(2)
#define  M_RD_ACCESS_ERR_MASK		BIT(1)
#define  RD_ACCESS_ERR_MASK		BIT(0)
#define XGICTRANSERRINTMSK		0x0054
#define XGICTRANSERRREQINFO		0x0058
#define  REQTYPE_MASK			BIT(26)
#define  ERRADDR_RD(src)		((src) & 0x03ffffff)
#define GLBL_ERR_STS			0x0800
#define  MDED_ERR_MASK			BIT(3)
#define  DED_ERR_MASK			BIT(2)
#define  MSEC_ERR_MASK			BIT(1)
#define  SEC_ERR_MASK			BIT(0)
#define GLBL_SEC_ERRL			0x0810
#define GLBL_SEC_ERRH			0x0818
#define GLBL_MSEC_ERRL			0x0820
#define GLBL_MSEC_ERRH			0x0828
#define GLBL_DED_ERRL			0x0830
#define GLBL_DED_ERRLMASK		0x0834
#define GLBL_DED_ERRH			0x0838
#define GLBL_DED_ERRHMASK		0x083c
#define GLBL_MDED_ERRL			0x0840
#define GLBL_MDED_ERRLMASK		0x0844
#define GLBL_MDED_ERRH			0x0848
#define GLBL_MDED_ERRHMASK		0x084c

/* IO Bus Registers */
#define RBCSR				0x0000
#define STICKYERR_MASK			BIT(0)
#define RBEIR				0x0008
#define AGENT_OFFLINE_ERR_MASK		BIT(30)
#define UNIMPL_RBPAGE_ERR_MASK		BIT(29)
#define WORD_ALIGNED_ERR_MASK		BIT(28)
#define PAGE_ACCESS_ERR_MASK		BIT(27)
#define WRITE_ACCESS_MASK		BIT(26)

static const char * const soc_mem_err_v1[] = {
	"10GbE0",
	"10GbE1",
	"Security",
	"SATA45",
	"SATA23/ETH23",
	"SATA01/ETH01",
	"USB1",
	"USB0",
	"QML",
	"QM0",
	"QM1 (XGbE01)",
	"PCIE4",
	"PCIE3",
	"PCIE2",
	"PCIE1",
	"PCIE0",
	"CTX Manager",
	"OCM",
	"1GbE",
	"CLE",
	"AHBC",
	"PktDMA",
	"GFC",
	"MSLIM",
	"10GbE2",
	"10GbE3",
	"QM2 (XGbE23)",
	"IOB",
	"unknown",
	"unknown",
	"unknown",
	"unknown",
};

static void xgene_edac_iob_gic_report(struct edac_device_ctl_info *edac_dev)
{
	struct xgene_edac_dev_ctx *ctx = edac_dev->pvt_info;
	u32 err_addr_lo;
	u32 err_addr_hi;
	u32 reg;
	u32 info;

	/* GIC transaction error interrupt */
	reg = readl(ctx->dev_csr + XGICTRANSERRINTSTS);
	if (!reg)
		goto chk_iob_err;
	dev_err(edac_dev->dev, "XGIC transaction error\n");
	if (reg & RD_ACCESS_ERR_MASK)
		dev_err(edac_dev->dev, "XGIC read size error\n");
	if (reg & M_RD_ACCESS_ERR_MASK)
		dev_err(edac_dev->dev, "Multiple XGIC read size error\n");
	if (reg & WR_ACCESS_ERR_MASK)
		dev_err(edac_dev->dev, "XGIC write size error\n");
	if (reg & M_WR_ACCESS_ERR_MASK)
		dev_err(edac_dev->dev, "Multiple XGIC write size error\n");
	info = readl(ctx->dev_csr + XGICTRANSERRREQINFO);
	dev_err(edac_dev->dev, "XGIC %s access @ 0x%08X (0x%08X)\n",
		info & REQTYPE_MASK ? "read" : "write", ERRADDR_RD(info),
		info);
	writel(reg, ctx->dev_csr + XGICTRANSERRINTSTS);

chk_iob_err:
	/* IOB memory error */
	reg = readl(ctx->dev_csr + GLBL_ERR_STS);
	if (!reg)
		return;
	if (reg & SEC_ERR_MASK) {
		err_addr_lo = readl(ctx->dev_csr + GLBL_SEC_ERRL);
		err_addr_hi = readl(ctx->dev_csr + GLBL_SEC_ERRH);
		dev_err(edac_dev->dev,
			"IOB single-bit correctable memory at 0x%08X.%08X error\n",
			err_addr_lo, err_addr_hi);
		writel(err_addr_lo, ctx->dev_csr + GLBL_SEC_ERRL);
		writel(err_addr_hi, ctx->dev_csr + GLBL_SEC_ERRH);
	}
	if (reg & MSEC_ERR_MASK) {
		err_addr_lo = readl(ctx->dev_csr + GLBL_MSEC_ERRL);
		err_addr_hi = readl(ctx->dev_csr + GLBL_MSEC_ERRH);
		dev_err(edac_dev->dev,
			"IOB multiple single-bit correctable memory at 0x%08X.%08X error\n",
			err_addr_lo, err_addr_hi);
		writel(err_addr_lo, ctx->dev_csr + GLBL_MSEC_ERRL);
		writel(err_addr_hi, ctx->dev_csr + GLBL_MSEC_ERRH);
	}
	if (reg & (SEC_ERR_MASK | MSEC_ERR_MASK))
		edac_device_handle_ce(edac_dev, 0, 0, edac_dev->ctl_name);

	if (reg & DED_ERR_MASK) {
		err_addr_lo = readl(ctx->dev_csr + GLBL_DED_ERRL);
		err_addr_hi = readl(ctx->dev_csr + GLBL_DED_ERRH);
		dev_err(edac_dev->dev,
			"IOB double-bit uncorrectable memory at 0x%08X.%08X error\n",
			err_addr_lo, err_addr_hi);
		writel(err_addr_lo, ctx->dev_csr + GLBL_DED_ERRL);
		writel(err_addr_hi, ctx->dev_csr + GLBL_DED_ERRH);
	}
	if (reg & MDED_ERR_MASK) {
		err_addr_lo = readl(ctx->dev_csr + GLBL_MDED_ERRL);
		err_addr_hi = readl(ctx->dev_csr + GLBL_MDED_ERRH);
		dev_err(edac_dev->dev,
			"Multiple IOB double-bit uncorrectable memory at 0x%08X.%08X error\n",
			err_addr_lo, err_addr_hi);
		writel(err_addr_lo, ctx->dev_csr + GLBL_MDED_ERRL);
		writel(err_addr_hi, ctx->dev_csr + GLBL_MDED_ERRH);
	}
	if (reg & (DED_ERR_MASK | MDED_ERR_MASK))
		edac_device_handle_ue(edac_dev, 0, 0, edac_dev->ctl_name);
}

static void xgene_edac_rb_report(struct edac_device_ctl_info *edac_dev)
{
	struct xgene_edac_dev_ctx *ctx = edac_dev->pvt_info;
	u32 err_addr_lo;
	u32 err_addr_hi;
	u32 reg;

	/* If the register bus resource isn't available, just skip it */
	if (!ctx->edac->rb_map)
		goto rb_skip;

	/*
	 * Check RB access errors
	 * 1. Out of range
	 * 2. Un-implemented page
	 * 3. Un-aligned access
	 * 4. Offline slave IP
	 */
	if (regmap_read(ctx->edac->rb_map, RBCSR, &reg))
		return;
	if (reg & STICKYERR_MASK) {
		bool write;

		dev_err(edac_dev->dev, "IOB bus access error(s)\n");
		if (regmap_read(ctx->edac->rb_map, RBEIR, &reg))
			return;
		write = reg & WRITE_ACCESS_MASK ? 1 : 0;
		if (reg & AGENT_OFFLINE_ERR_MASK)
			dev_err(edac_dev->dev,
				"IOB bus %s access to offline agent error\n",
				write ? "write" : "read");
		if (reg & UNIMPL_RBPAGE_ERR_MASK)
			dev_err(edac_dev->dev,
				"IOB bus %s access to unimplemented page error\n",
				write ? "write" : "read");
		if (reg & WORD_ALIGNED_ERR_MASK)
			dev_err(edac_dev->dev,
				"IOB bus %s word aligned access error\n",
				write ? "write" : "read");
		if (reg & PAGE_ACCESS_ERR_MASK)
			dev_err(edac_dev->dev,
				"IOB bus %s to page out of range access error\n",
				write ? "write" : "read");
		if (regmap_write(ctx->edac->rb_map, RBEIR, 0))
			return;
		if (regmap_write(ctx->edac->rb_map, RBCSR, 0))
			return;
	}
rb_skip:

	/* IOB Bridge agent transaction error interrupt */
	reg = readl(ctx->dev_csr + IOBBATRANSERRINTSTS);
	if (!reg)
		return;

	dev_err(edac_dev->dev, "IOB bridge agent (BA) transaction error\n");
	if (reg & WRERR_RESP_MASK)
		dev_err(edac_dev->dev, "IOB BA write response error\n");
	if (reg & M_WRERR_RESP_MASK)
		dev_err(edac_dev->dev,
			"Multiple IOB BA write response error\n");
	if (reg & XGIC_POISONED_REQ_MASK)
		dev_err(edac_dev->dev, "IOB BA XGIC poisoned write error\n");
	if (reg & M_XGIC_POISONED_REQ_MASK)
		dev_err(edac_dev->dev,
			"Multiple IOB BA XGIC poisoned write error\n");
	if (reg & RBM_POISONED_REQ_MASK)
		dev_err(edac_dev->dev, "IOB BA RBM poisoned write error\n");
	if (reg & M_RBM_POISONED_REQ_MASK)
		dev_err(edac_dev->dev,
			"Multiple IOB BA RBM poisoned write error\n");
	if (reg & WDATA_CORRUPT_MASK)
		dev_err(edac_dev->dev, "IOB BA write error\n");
	if (reg & M_WDATA_CORRUPT_MASK)
		dev_err(edac_dev->dev, "Multiple IOB BA write error\n");
	if (reg & TRANS_CORRUPT_MASK)
		dev_err(edac_dev->dev, "IOB BA transaction error\n");
	if (reg & M_TRANS_CORRUPT_MASK)
		dev_err(edac_dev->dev, "Multiple IOB BA transaction error\n");
	if (reg & RIDRAM_CORRUPT_MASK)
		dev_err(edac_dev->dev,
			"IOB BA RDIDRAM read transaction ID error\n");
	if (reg & M_RIDRAM_CORRUPT_MASK)
		dev_err(edac_dev->dev,
			"Multiple IOB BA RDIDRAM read transaction ID error\n");
	if (reg & WIDRAM_CORRUPT_MASK)
		dev_err(edac_dev->dev,
			"IOB BA RDIDRAM write transaction ID error\n");
	if (reg & M_WIDRAM_CORRUPT_MASK)
		dev_err(edac_dev->dev,
			"Multiple IOB BA RDIDRAM write transaction ID error\n");
	if (reg & ILLEGAL_ACCESS_MASK)
		dev_err(edac_dev->dev,
			"IOB BA XGIC/RB illegal access error\n");
	if (reg & M_ILLEGAL_ACCESS_MASK)
		dev_err(edac_dev->dev,
			"Multiple IOB BA XGIC/RB illegal access error\n");

	err_addr_lo = readl(ctx->dev_csr + IOBBATRANSERRREQINFOL);
	err_addr_hi = readl(ctx->dev_csr + IOBBATRANSERRREQINFOH);
	dev_err(edac_dev->dev, "IOB BA %s access at 0x%02X.%08X (0x%08X)\n",
		REQTYPE_F2_RD(err_addr_hi) ? "read" : "write",
		ERRADDRH_F2_RD(err_addr_hi), err_addr_lo, err_addr_hi);
	if (reg & WRERR_RESP_MASK)
		dev_err(edac_dev->dev, "IOB BA requestor ID 0x%08X\n",
			readl(ctx->dev_csr + IOBBATRANSERRCSWREQID));
	writel(reg, ctx->dev_csr + IOBBATRANSERRINTSTS);
}

static void xgene_edac_pa_report(struct edac_device_ctl_info *edac_dev)
{
	struct xgene_edac_dev_ctx *ctx = edac_dev->pvt_info;
	u32 err_addr_lo;
	u32 err_addr_hi;
	u32 reg;

	/* IOB Processing agent transaction error interrupt */
	reg = readl(ctx->dev_csr + IOBPATRANSERRINTSTS);
	if (!reg)
		goto chk_iob_axi0;
	dev_err(edac_dev->dev, "IOB processing agent (PA) transaction error\n");
	if (reg & IOBPA_RDATA_CORRUPT_MASK)
		dev_err(edac_dev->dev, "IOB PA read data RAM error\n");
	if (reg & IOBPA_M_RDATA_CORRUPT_MASK)
		dev_err(edac_dev->dev,
			"Multiple IOB PA read data RAM error\n");
	if (reg & IOBPA_WDATA_CORRUPT_MASK)
		dev_err(edac_dev->dev, "IOB PA write data RAM error\n");
	if (reg & IOBPA_M_WDATA_CORRUPT_MASK)
		dev_err(edac_dev->dev,
			"Multiple IOB PA write data RAM error\n");
	if (reg & IOBPA_TRANS_CORRUPT_MASK)
		dev_err(edac_dev->dev, "IOB PA transaction error\n");
	if (reg & IOBPA_M_TRANS_CORRUPT_MASK)
		dev_err(edac_dev->dev, "Multiple IOB PA transaction error\n");
	if (reg & IOBPA_REQIDRAM_CORRUPT_MASK)
		dev_err(edac_dev->dev, "IOB PA transaction ID RAM error\n");
	if (reg & IOBPA_M_REQIDRAM_CORRUPT_MASK)
		dev_err(edac_dev->dev,
			"Multiple IOB PA transaction ID RAM error\n");
	writel(reg, ctx->dev_csr + IOBPATRANSERRINTSTS);

chk_iob_axi0:
	/* IOB AXI0 Error */
	reg = readl(ctx->dev_csr + IOBAXIS0TRANSERRINTSTS);
	if (!reg)
		goto chk_iob_axi1;
	err_addr_lo = readl(ctx->dev_csr + IOBAXIS0TRANSERRREQINFOL);
	err_addr_hi = readl(ctx->dev_csr + IOBAXIS0TRANSERRREQINFOH);
	dev_err(edac_dev->dev,
		"%sAXI slave 0 illegal %s access @ 0x%02X.%08X (0x%08X)\n",
		reg & IOBAXIS0_M_ILLEGAL_ACCESS_MASK ? "Multiple " : "",
		REQTYPE_RD(err_addr_hi) ? "read" : "write",
		ERRADDRH_RD(err_addr_hi), err_addr_lo, err_addr_hi);
	writel(reg, ctx->dev_csr + IOBAXIS0TRANSERRINTSTS);

chk_iob_axi1:
	/* IOB AXI1 Error */
	reg = readl(ctx->dev_csr + IOBAXIS1TRANSERRINTSTS);
	if (!reg)
		return;
	err_addr_lo = readl(ctx->dev_csr + IOBAXIS1TRANSERRREQINFOL);
	err_addr_hi = readl(ctx->dev_csr + IOBAXIS1TRANSERRREQINFOH);
	dev_err(edac_dev->dev,
		"%sAXI slave 1 illegal %s access @ 0x%02X.%08X (0x%08X)\n",
		reg & IOBAXIS0_M_ILLEGAL_ACCESS_MASK ? "Multiple " : "",
		REQTYPE_RD(err_addr_hi) ? "read" : "write",
		ERRADDRH_RD(err_addr_hi), err_addr_lo, err_addr_hi);
	writel(reg, ctx->dev_csr + IOBAXIS1TRANSERRINTSTS);
}

static void xgene_edac_soc_check(struct edac_device_ctl_info *edac_dev)
{
	struct xgene_edac_dev_ctx *ctx = edac_dev->pvt_info;
	const char * const *soc_mem_err = NULL;
	u32 pcp_hp_stat;
	u32 pcp_lp_stat;
	u32 reg;
	int i;

	xgene_edac_pcp_rd(ctx->edac, PCPHPERRINTSTS, &pcp_hp_stat);
	xgene_edac_pcp_rd(ctx->edac, PCPLPERRINTSTS, &pcp_lp_stat);
	xgene_edac_pcp_rd(ctx->edac, MEMERRINTSTS, &reg);
	if (!((pcp_hp_stat & (IOB_PA_ERR_MASK | IOB_BA_ERR_MASK |
			      IOB_XGIC_ERR_MASK | IOB_RB_ERR_MASK)) ||
	      (pcp_lp_stat & CSW_SWITCH_TRACE_ERR_MASK) || reg))
		return;

	if (pcp_hp_stat & IOB_XGIC_ERR_MASK)
		xgene_edac_iob_gic_report(edac_dev);

	if (pcp_hp_stat & (IOB_RB_ERR_MASK | IOB_BA_ERR_MASK))
		xgene_edac_rb_report(edac_dev);

	if (pcp_hp_stat & IOB_PA_ERR_MASK)
		xgene_edac_pa_report(edac_dev);

	if (pcp_lp_stat & CSW_SWITCH_TRACE_ERR_MASK) {
		dev_info(edac_dev->dev,
			 "CSW switch trace correctable memory parity error\n");
		edac_device_handle_ce(edac_dev, 0, 0, edac_dev->ctl_name);
	}

	if (!reg)
		return;
	if (ctx->version == 1)
		soc_mem_err = soc_mem_err_v1;
	if (!soc_mem_err) {
		dev_err(edac_dev->dev, "SoC memory parity error 0x%08X\n",
			reg);
		edac_device_handle_ue(edac_dev, 0, 0, edac_dev->ctl_name);
		return;
	}
	for (i = 0; i < 31; i++) {
		if (reg & (1 << i)) {
			dev_err(edac_dev->dev, "%s memory parity error\n",
				soc_mem_err[i]);
			edac_device_handle_ue(edac_dev, 0, 0,
					      edac_dev->ctl_name);
		}
	}
}

static void xgene_edac_soc_hw_init(struct edac_device_ctl_info *edac_dev,
				   bool enable)
{
	struct xgene_edac_dev_ctx *ctx = edac_dev->pvt_info;

	/* Enable SoC IP error interrupt */
	if (edac_dev->op_state == OP_RUNNING_INTERRUPT) {
		if (enable) {
			xgene_edac_pcp_clrbits(ctx->edac, PCPHPERRINTMSK,
					       IOB_PA_ERR_MASK |
					       IOB_BA_ERR_MASK |
					       IOB_XGIC_ERR_MASK |
					       IOB_RB_ERR_MASK);
			xgene_edac_pcp_clrbits(ctx->edac, PCPLPERRINTMSK,
					       CSW_SWITCH_TRACE_ERR_MASK);
		} else {
			xgene_edac_pcp_setbits(ctx->edac, PCPHPERRINTMSK,
					       IOB_PA_ERR_MASK |
					       IOB_BA_ERR_MASK |
					       IOB_XGIC_ERR_MASK |
					       IOB_RB_ERR_MASK);
			xgene_edac_pcp_setbits(ctx->edac, PCPLPERRINTMSK,
					       CSW_SWITCH_TRACE_ERR_MASK);
		}

		writel(enable ? 0x0 : 0xFFFFFFFF,
		       ctx->dev_csr + IOBAXIS0TRANSERRINTMSK);
		writel(enable ? 0x0 : 0xFFFFFFFF,
		       ctx->dev_csr + IOBAXIS1TRANSERRINTMSK);
		writel(enable ? 0x0 : 0xFFFFFFFF,
		       ctx->dev_csr + XGICTRANSERRINTMSK);

		xgene_edac_pcp_setbits(ctx->edac, MEMERRINTMSK,
				       enable ? 0x0 : 0xFFFFFFFF);
	}
}

static int xgene_edac_soc_add(struct xgene_edac *edac, struct device_node *np,
			      int version)
{
	struct edac_device_ctl_info *edac_dev;
	struct xgene_edac_dev_ctx *ctx;
	void __iomem *dev_csr;
	struct resource res;
	int edac_idx;
	int rc;

	if (!devres_open_group(edac->dev, xgene_edac_soc_add, GFP_KERNEL))
		return -ENOMEM;

	rc = of_address_to_resource(np, 0, &res);
	if (rc < 0) {
		dev_err(edac->dev, "no SoC resource address\n");
		goto err_release_group;
	}
	dev_csr = devm_ioremap_resource(edac->dev, &res);
	if (IS_ERR(dev_csr)) {
		dev_err(edac->dev,
			"devm_ioremap_resource failed for soc resource address\n");
		rc = PTR_ERR(dev_csr);
		goto err_release_group;
	}

	edac_idx = edac_device_alloc_index();
	edac_dev = edac_device_alloc_ctl_info(sizeof(*ctx),
					      "SOC", 1, "SOC", 1, 2, NULL, 0,
					      edac_idx);
	if (!edac_dev) {
		rc = -ENOMEM;
		goto err_release_group;
	}

	ctx = edac_dev->pvt_info;
	ctx->dev_csr = dev_csr;
	ctx->name = "xgene_soc_err";
	ctx->edac_idx = edac_idx;
	ctx->edac = edac;
	ctx->edac_dev = edac_dev;
	ctx->ddev = *edac->dev;
	ctx->version = version;
	edac_dev->dev = &ctx->ddev;
	edac_dev->ctl_name = ctx->name;
	edac_dev->dev_name = ctx->name;
	edac_dev->mod_name = EDAC_MOD_STR;

	if (edac_op_state == EDAC_OPSTATE_POLL)
		edac_dev->edac_check = xgene_edac_soc_check;

	rc = edac_device_add_device(edac_dev);
	if (rc > 0) {
		dev_err(edac->dev, "failed edac_device_add_device()\n");
		rc = -ENOMEM;
		goto err_ctl_free;
	}

	if (edac_op_state == EDAC_OPSTATE_INT)
		edac_dev->op_state = OP_RUNNING_INTERRUPT;

	list_add(&ctx->next, &edac->socs);

	xgene_edac_soc_hw_init(edac_dev, 1);

	devres_remove_group(edac->dev, xgene_edac_soc_add);

	dev_info(edac->dev, "X-Gene EDAC SoC registered\n");

	return 0;

err_ctl_free:
	edac_device_free_ctl_info(edac_dev);
err_release_group:
	devres_release_group(edac->dev, xgene_edac_soc_add);
	return rc;
}

static int xgene_edac_soc_remove(struct xgene_edac_dev_ctx *soc)
{
	struct edac_device_ctl_info *edac_dev = soc->edac_dev;

	xgene_edac_soc_hw_init(edac_dev, 0);
	edac_device_del_device(soc->edac->dev);
	edac_device_free_ctl_info(edac_dev);
	return 0;
}

static irqreturn_t xgene_edac_isr(int irq, void *dev_id)
{
	struct xgene_edac *ctx = dev_id;
	struct xgene_edac_pmd_ctx *pmd;
	struct xgene_edac_dev_ctx *node;
	unsigned int pcp_hp_stat;
	unsigned int pcp_lp_stat;

	xgene_edac_pcp_rd(ctx, PCPHPERRINTSTS, &pcp_hp_stat);
	xgene_edac_pcp_rd(ctx, PCPLPERRINTSTS, &pcp_lp_stat);
	if ((MCU_UNCORR_ERR_MASK & pcp_hp_stat) ||
	    (MCU_CTL_ERR_MASK & pcp_hp_stat) ||
	    (MCU_CORR_ERR_MASK & pcp_lp_stat)) {
		struct xgene_edac_mc_ctx *mcu;

		list_for_each_entry(mcu, &ctx->mcus, next)
			xgene_edac_mc_check(mcu->mci);
	}

	list_for_each_entry(pmd, &ctx->pmds, next) {
		if ((PMD0_MERR_MASK << pmd->pmd) & pcp_hp_stat)
			xgene_edac_pmd_check(pmd->edac_dev);
	}

	list_for_each_entry(node, &ctx->l3s, next)
		xgene_edac_l3_check(node->edac_dev);

	list_for_each_entry(node, &ctx->socs, next)
		xgene_edac_soc_check(node->edac_dev);

	return IRQ_HANDLED;
}

static int xgene_edac_probe(struct platform_device *pdev)
{
	struct xgene_edac *edac;
	struct device_node *child;
	struct resource *res;
	int rc;

	edac = devm_kzalloc(&pdev->dev, sizeof(*edac), GFP_KERNEL);
	if (!edac)
		return -ENOMEM;

	edac->dev = &pdev->dev;
	platform_set_drvdata(pdev, edac);
	INIT_LIST_HEAD(&edac->mcus);
	INIT_LIST_HEAD(&edac->pmds);
	INIT_LIST_HEAD(&edac->l3s);
	INIT_LIST_HEAD(&edac->socs);
	spin_lock_init(&edac->lock);
	mutex_init(&edac->mc_lock);

	edac->csw_map = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
							"regmap-csw");
	if (IS_ERR(edac->csw_map)) {
		dev_err(edac->dev, "unable to get syscon regmap csw\n");
		rc = PTR_ERR(edac->csw_map);
		goto out_err;
	}

	edac->mcba_map = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
							 "regmap-mcba");
	if (IS_ERR(edac->mcba_map)) {
		dev_err(edac->dev, "unable to get syscon regmap mcba\n");
		rc = PTR_ERR(edac->mcba_map);
		goto out_err;
	}

	edac->mcbb_map = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
							 "regmap-mcbb");
	if (IS_ERR(edac->mcbb_map)) {
		dev_err(edac->dev, "unable to get syscon regmap mcbb\n");
		rc = PTR_ERR(edac->mcbb_map);
		goto out_err;
	}
	edac->efuse_map = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
							  "regmap-efuse");
	if (IS_ERR(edac->efuse_map)) {
		dev_err(edac->dev, "unable to get syscon regmap efuse\n");
		rc = PTR_ERR(edac->efuse_map);
		goto out_err;
	}

	/*
	 * NOTE: The register bus resource is optional for compatibility
	 * reason.
	 */
	edac->rb_map = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
						       "regmap-rb");
	if (IS_ERR(edac->rb_map)) {
		dev_warn(edac->dev, "missing syscon regmap rb\n");
		edac->rb_map = NULL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	edac->pcp_csr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(edac->pcp_csr)) {
		dev_err(&pdev->dev, "no PCP resource address\n");
		rc = PTR_ERR(edac->pcp_csr);
		goto out_err;
	}

	if (edac_op_state == EDAC_OPSTATE_INT) {
		int irq;
		int i;

		for (i = 0; i < 3; i++) {
			irq = platform_get_irq_optional(pdev, i);
			if (irq < 0) {
				dev_err(&pdev->dev, "No IRQ resource\n");
				rc = -EINVAL;
				goto out_err;
			}
			rc = devm_request_irq(&pdev->dev, irq,
					      xgene_edac_isr, IRQF_SHARED,
					      dev_name(&pdev->dev), edac);
			if (rc) {
				dev_err(&pdev->dev,
					"Could not request IRQ %d\n", irq);
				goto out_err;
			}
		}
	}

	edac->dfs = edac_debugfs_create_dir(pdev->dev.kobj.name);

	for_each_child_of_node(pdev->dev.of_node, child) {
		if (!of_device_is_available(child))
			continue;
		if (of_device_is_compatible(child, "apm,xgene-edac-mc"))
			xgene_edac_mc_add(edac, child);
		if (of_device_is_compatible(child, "apm,xgene-edac-pmd"))
			xgene_edac_pmd_add(edac, child, 1);
		if (of_device_is_compatible(child, "apm,xgene-edac-pmd-v2"))
			xgene_edac_pmd_add(edac, child, 2);
		if (of_device_is_compatible(child, "apm,xgene-edac-l3"))
			xgene_edac_l3_add(edac, child, 1);
		if (of_device_is_compatible(child, "apm,xgene-edac-l3-v2"))
			xgene_edac_l3_add(edac, child, 2);
		if (of_device_is_compatible(child, "apm,xgene-edac-soc"))
			xgene_edac_soc_add(edac, child, 0);
		if (of_device_is_compatible(child, "apm,xgene-edac-soc-v1"))
			xgene_edac_soc_add(edac, child, 1);
	}

	return 0;

out_err:
	return rc;
}

static int xgene_edac_remove(struct platform_device *pdev)
{
	struct xgene_edac *edac = dev_get_drvdata(&pdev->dev);
	struct xgene_edac_mc_ctx *mcu;
	struct xgene_edac_mc_ctx *temp_mcu;
	struct xgene_edac_pmd_ctx *pmd;
	struct xgene_edac_pmd_ctx *temp_pmd;
	struct xgene_edac_dev_ctx *node;
	struct xgene_edac_dev_ctx *temp_node;

	list_for_each_entry_safe(mcu, temp_mcu, &edac->mcus, next)
		xgene_edac_mc_remove(mcu);

	list_for_each_entry_safe(pmd, temp_pmd, &edac->pmds, next)
		xgene_edac_pmd_remove(pmd);

	list_for_each_entry_safe(node, temp_node, &edac->l3s, next)
		xgene_edac_l3_remove(node);

	list_for_each_entry_safe(node, temp_node, &edac->socs, next)
		xgene_edac_soc_remove(node);

	return 0;
}

static const struct of_device_id xgene_edac_of_match[] = {
	{ .compatible = "apm,xgene-edac" },
	{},
};
MODULE_DEVICE_TABLE(of, xgene_edac_of_match);

static struct platform_driver xgene_edac_driver = {
	.probe = xgene_edac_probe,
	.remove = xgene_edac_remove,
	.driver = {
		.name = "xgene-edac",
		.of_match_table = xgene_edac_of_match,
	},
};

static int __init xgene_edac_init(void)
{
	int rc;

	/* Make sure error reporting method is sane */
	switch (edac_op_state) {
	case EDAC_OPSTATE_POLL:
	case EDAC_OPSTATE_INT:
		break;
	default:
		edac_op_state = EDAC_OPSTATE_INT;
		break;
	}

	rc = platform_driver_register(&xgene_edac_driver);
	if (rc) {
		edac_printk(KERN_ERR, EDAC_MOD_STR,
			    "EDAC fails to register\n");
		goto reg_failed;
	}

	return 0;

reg_failed:
	return rc;
}
module_init(xgene_edac_init);

static void __exit xgene_edac_exit(void)
{
	platform_driver_unregister(&xgene_edac_driver);
}
module_exit(xgene_edac_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Feng Kan <fkan@apm.com>");
MODULE_DESCRIPTION("APM X-Gene EDAC driver");
module_param(edac_op_state, int, 0444);
MODULE_PARM_DESC(edac_op_state,
		 "EDAC error reporting state: 0=Poll, 2=Interrupt");

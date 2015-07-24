/*
 * APM X-Gene SoC EDAC (error detection and correction)
 *
 * Copyright (c) 2015, Applied Micro Circuits Corporation
 * Author: Feng Kan <fkan@apm.com>
 *         Loc Ho <lho@apm.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/ctype.h>
#include <linux/edac.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/regmap.h>

#include "edac_core.h"

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
	void __iomem		*pcp_csr;
	spinlock_t		lock;
	struct dentry		*dfs;

	struct list_head	mcus;
	struct list_head	pmds;

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
#ifdef CONFIG_EDAC_DEBUG
	if (!mci->debugfs)
		return;
	debugfs_create_file("inject_ctrl", S_IWUSR, mci->debugfs, mci,
			    &xgene_edac_mc_debug_inject_fops);
#endif
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
	mci->mod_ver = "0.1";
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
	if (val) {
		dev_err(edac_dev->dev,
			"CPU%d L1 memory error ICF 0x%08X Way 0x%02X Index 0x%02X Info 0x%02X\n",
			ctx->pmd * MAX_CPU_PER_PMD + cpu_idx, val,
			MEMERR_CPU_ICFESR_ERRWAY_RD(val),
			MEMERR_CPU_ICFESR_ERRINDEX_RD(val),
			MEMERR_CPU_ICFESR_ERRINFO_RD(val));
		if (val & MEMERR_CPU_ICFESR_CERR_MASK)
			dev_err(edac_dev->dev,
				"One or more correctable error\n");
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
			edac_device_handle_ce(edac_dev, 0, 0,
					      edac_dev->ctl_name);
	}

	val = readl(pg_f + MEMERR_CPU_LSUESR_PAGE_OFFSET);
	if (val) {
		dev_err(edac_dev->dev,
			"CPU%d memory error LSU 0x%08X Way 0x%02X Index 0x%02X Info 0x%02X\n",
			ctx->pmd * MAX_CPU_PER_PMD + cpu_idx, val,
			MEMERR_CPU_LSUESR_ERRWAY_RD(val),
			MEMERR_CPU_LSUESR_ERRINDEX_RD(val),
			MEMERR_CPU_LSUESR_ERRINFO_RD(val));
		if (val & MEMERR_CPU_LSUESR_CERR_MASK)
			dev_err(edac_dev->dev,
				"One or more correctable error\n");
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
			edac_device_handle_ce(edac_dev, 0, 0,
					      edac_dev->ctl_name);
	}

	val = readl(pg_f + MEMERR_CPU_MMUESR_PAGE_OFFSET);
	if (val) {
		dev_err(edac_dev->dev,
			"CPU%d memory error MMU 0x%08X Way 0x%02X Index 0x%02X Info 0x%02X %s\n",
			ctx->pmd * MAX_CPU_PER_PMD + cpu_idx, val,
			MEMERR_CPU_MMUESR_ERRWAY_RD(val),
			MEMERR_CPU_MMUESR_ERRINDEX_RD(val),
			MEMERR_CPU_MMUESR_ERRINFO_RD(val),
			val & MEMERR_CPU_MMUESR_ERRREQSTR_LSU_MASK ? "LSU" :
								     "ICF");
		if (val & MEMERR_CPU_MMUESR_CERR_MASK)
			dev_err(edac_dev->dev,
				"One or more correctable error\n");
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
			dev_err(edac_dev->dev,
				"TMO operation single bank error\n");
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
			dev_err(edac_dev->dev,
				"TMO operation multiple bank error\n");
			break;
		}

		/* Clear any HW errors */
		writel(val, pg_f + MEMERR_CPU_MMUESR_PAGE_OFFSET);

		edac_device_handle_ce(edac_dev, 0, 0, edac_dev->ctl_name);
	}
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
	if (val) {
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
			dev_err(edac_dev->dev,
				"One or more correctable error\n");
		if (val & MEMERR_L2C_L2ESR_MULTICERR_MASK)
			dev_err(edac_dev->dev, "Multiple correctable error\n");
		if (val & MEMERR_L2C_L2ESR_UCERR_MASK)
			dev_err(edac_dev->dev,
				"One or more uncorrectable error\n");
		if (val & MEMERR_L2C_L2ESR_MULTUCERR_MASK)
			dev_err(edac_dev->dev,
				"Multiple uncorrectable error\n");

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
			edac_device_handle_ce(edac_dev, 0, 0,
					      edac_dev->ctl_name);
		if (val & (MEMERR_L2C_L2ESR_UCERR_MASK |
			   MEMERR_L2C_L2ESR_MULTUCERR_MASK))
			edac_device_handle_ue(edac_dev, 0, 0,
					      edac_dev->ctl_name);
	}

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

static void xgene_edac_pmd_create_debugfs_nodes(
	struct edac_device_ctl_info *edac_dev)
{
	struct xgene_edac_pmd_ctx *ctx = edac_dev->pvt_info;
	struct dentry *edac_debugfs;
	char name[30];

	if (!IS_ENABLED(CONFIG_EDAC_DEBUG))
		return;

	/*
	 * Todo: Switch to common EDAC debug file system for edac device
	 *       when available.
	 */
	if (!ctx->edac->dfs) {
		ctx->edac->dfs = debugfs_create_dir(edac_dev->dev->kobj.name,
						    NULL);
		if (!ctx->edac->dfs)
			return;
	}
	sprintf(name, "PMD%d", ctx->pmd);
	edac_debugfs = debugfs_create_dir(name, ctx->edac->dfs);
	if (!edac_debugfs)
		return;

	debugfs_create_file("l1_inject_ctrl", S_IWUSR, edac_debugfs, edac_dev,
			    &xgene_edac_pmd_debug_inject_fops[0]);
	debugfs_create_file("l2_inject_ctrl", S_IWUSR, edac_debugfs, edac_dev,
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

	sprintf(edac_name, "l2c%d", pmd);
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

static irqreturn_t xgene_edac_isr(int irq, void *dev_id)
{
	struct xgene_edac *ctx = dev_id;
	struct xgene_edac_pmd_ctx *pmd;
	unsigned int pcp_hp_stat;
	unsigned int pcp_lp_stat;

	xgene_edac_pcp_rd(ctx, PCPHPERRINTSTS, &pcp_hp_stat);
	xgene_edac_pcp_rd(ctx, PCPLPERRINTSTS, &pcp_lp_stat);
	if ((MCU_UNCORR_ERR_MASK & pcp_hp_stat) ||
	    (MCU_CTL_ERR_MASK & pcp_hp_stat) ||
	    (MCU_CORR_ERR_MASK & pcp_lp_stat)) {
		struct xgene_edac_mc_ctx *mcu;

		list_for_each_entry(mcu, &ctx->mcus, next) {
			xgene_edac_mc_check(mcu->mci);
		}
	}

	list_for_each_entry(pmd, &ctx->pmds, next) {
		if ((PMD0_MERR_MASK << pmd->pmd) & pcp_hp_stat)
			xgene_edac_pmd_check(pmd->edac_dev);
	}

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
			irq = platform_get_irq(pdev, i);
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

	for_each_child_of_node(pdev->dev.of_node, child) {
		if (!of_device_is_available(child))
			continue;
		if (of_device_is_compatible(child, "apm,xgene-edac-mc"))
			xgene_edac_mc_add(edac, child);
		if (of_device_is_compatible(child, "apm,xgene-edac-pmd"))
			xgene_edac_pmd_add(edac, child, 1);
		if (of_device_is_compatible(child, "apm,xgene-edac-pmd-v2"))
			xgene_edac_pmd_add(edac, child, 2);
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

	list_for_each_entry_safe(mcu, temp_mcu, &edac->mcus, next) {
		xgene_edac_mc_remove(mcu);
	}

	list_for_each_entry_safe(pmd, temp_pmd, &edac->pmds, next) {
		xgene_edac_pmd_remove(pmd);
	}
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
		.owner = THIS_MODULE,
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

// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 *
 * Author: Stepan Moskovchenko <stepanm@codeaurora.org>
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/io-pgtable.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/iommu.h>
#include <linux/clk.h>
#include <linux/err.h>

#include <asm/cacheflush.h>
#include <linux/sizes.h>

#include "msm_iommu_hw-8xxx.h"
#include "msm_iommu.h"

#define MRC(reg, processor, op1, crn, crm, op2)				\
__asm__ __volatile__ (							\
"   mrc   "   #processor "," #op1 ", %0,"  #crn "," #crm "," #op2 "\n"  \
: "=r" (reg))

/* bitmap of the page sizes currently supported */
#define MSM_IOMMU_PGSIZES	(SZ_4K | SZ_64K | SZ_1M | SZ_16M)

static DEFINE_SPINLOCK(msm_iommu_lock);
static LIST_HEAD(qcom_iommu_devices);
static struct iommu_ops msm_iommu_ops;

struct msm_priv {
	struct list_head list_attached;
	struct iommu_domain domain;
	struct io_pgtable_cfg	cfg;
	struct io_pgtable_ops	*iop;
	struct device		*dev;
	spinlock_t		pgtlock; /* pagetable lock */
};

static struct msm_priv *to_msm_priv(struct iommu_domain *dom)
{
	return container_of(dom, struct msm_priv, domain);
}

static int __enable_clocks(struct msm_iommu_dev *iommu)
{
	int ret;

	ret = clk_enable(iommu->pclk);
	if (ret)
		goto fail;

	if (iommu->clk) {
		ret = clk_enable(iommu->clk);
		if (ret)
			clk_disable(iommu->pclk);
	}
fail:
	return ret;
}

static void __disable_clocks(struct msm_iommu_dev *iommu)
{
	if (iommu->clk)
		clk_disable(iommu->clk);
	clk_disable(iommu->pclk);
}

static void msm_iommu_reset(void __iomem *base, int ncb)
{
	int ctx;

	SET_RPUE(base, 0);
	SET_RPUEIE(base, 0);
	SET_ESRRESTORE(base, 0);
	SET_TBE(base, 0);
	SET_CR(base, 0);
	SET_SPDMBE(base, 0);
	SET_TESTBUSCR(base, 0);
	SET_TLBRSW(base, 0);
	SET_GLOBAL_TLBIALL(base, 0);
	SET_RPU_ACR(base, 0);
	SET_TLBLKCRWE(base, 1);

	for (ctx = 0; ctx < ncb; ctx++) {
		SET_BPRCOSH(base, ctx, 0);
		SET_BPRCISH(base, ctx, 0);
		SET_BPRCNSH(base, ctx, 0);
		SET_BPSHCFG(base, ctx, 0);
		SET_BPMTCFG(base, ctx, 0);
		SET_ACTLR(base, ctx, 0);
		SET_SCTLR(base, ctx, 0);
		SET_FSRRESTORE(base, ctx, 0);
		SET_TTBR0(base, ctx, 0);
		SET_TTBR1(base, ctx, 0);
		SET_TTBCR(base, ctx, 0);
		SET_BFBCR(base, ctx, 0);
		SET_PAR(base, ctx, 0);
		SET_FAR(base, ctx, 0);
		SET_CTX_TLBIALL(base, ctx, 0);
		SET_TLBFLPTER(base, ctx, 0);
		SET_TLBSLPTER(base, ctx, 0);
		SET_TLBLKCR(base, ctx, 0);
		SET_CONTEXTIDR(base, ctx, 0);
	}
}

static void __flush_iotlb(void *cookie)
{
	struct msm_priv *priv = cookie;
	struct msm_iommu_dev *iommu = NULL;
	struct msm_iommu_ctx_dev *master;
	int ret = 0;

	list_for_each_entry(iommu, &priv->list_attached, dom_node) {
		ret = __enable_clocks(iommu);
		if (ret)
			goto fail;

		list_for_each_entry(master, &iommu->ctx_list, list)
			SET_CTX_TLBIALL(iommu->base, master->num, 0);

		__disable_clocks(iommu);
	}
fail:
	return;
}

static void __flush_iotlb_range(unsigned long iova, size_t size,
				size_t granule, bool leaf, void *cookie)
{
	struct msm_priv *priv = cookie;
	struct msm_iommu_dev *iommu = NULL;
	struct msm_iommu_ctx_dev *master;
	int ret = 0;
	int temp_size;

	list_for_each_entry(iommu, &priv->list_attached, dom_node) {
		ret = __enable_clocks(iommu);
		if (ret)
			goto fail;

		list_for_each_entry(master, &iommu->ctx_list, list) {
			temp_size = size;
			do {
				iova &= TLBIVA_VA;
				iova |= GET_CONTEXTIDR_ASID(iommu->base,
							    master->num);
				SET_TLBIVA(iommu->base, master->num, iova);
				iova += granule;
			} while (temp_size -= granule);
		}

		__disable_clocks(iommu);
	}

fail:
	return;
}

static void __flush_iotlb_walk(unsigned long iova, size_t size,
			       size_t granule, void *cookie)
{
	__flush_iotlb_range(iova, size, granule, false, cookie);
}

static void __flush_iotlb_page(struct iommu_iotlb_gather *gather,
			       unsigned long iova, size_t granule, void *cookie)
{
	__flush_iotlb_range(iova, granule, granule, true, cookie);
}

static const struct iommu_flush_ops msm_iommu_flush_ops = {
	.tlb_flush_all = __flush_iotlb,
	.tlb_flush_walk = __flush_iotlb_walk,
	.tlb_add_page = __flush_iotlb_page,
};

static int msm_iommu_alloc_ctx(unsigned long *map, int start, int end)
{
	int idx;

	do {
		idx = find_next_zero_bit(map, end, start);
		if (idx == end)
			return -ENOSPC;
	} while (test_and_set_bit(idx, map));

	return idx;
}

static void msm_iommu_free_ctx(unsigned long *map, int idx)
{
	clear_bit(idx, map);
}

static void config_mids(struct msm_iommu_dev *iommu,
			struct msm_iommu_ctx_dev *master)
{
	int mid, ctx, i;

	for (i = 0; i < master->num_mids; i++) {
		mid = master->mids[i];
		ctx = master->num;

		SET_M2VCBR_N(iommu->base, mid, 0);
		SET_CBACR_N(iommu->base, ctx, 0);

		/* Set VMID = 0 */
		SET_VMID(iommu->base, mid, 0);

		/* Set the context number for that MID to this context */
		SET_CBNDX(iommu->base, mid, ctx);

		/* Set MID associated with this context bank to 0*/
		SET_CBVMID(iommu->base, ctx, 0);

		/* Set the ASID for TLB tagging for this context */
		SET_CONTEXTIDR_ASID(iommu->base, ctx, ctx);

		/* Set security bit override to be Non-secure */
		SET_NSCFG(iommu->base, mid, 3);
	}
}

static void __reset_context(void __iomem *base, int ctx)
{
	SET_BPRCOSH(base, ctx, 0);
	SET_BPRCISH(base, ctx, 0);
	SET_BPRCNSH(base, ctx, 0);
	SET_BPSHCFG(base, ctx, 0);
	SET_BPMTCFG(base, ctx, 0);
	SET_ACTLR(base, ctx, 0);
	SET_SCTLR(base, ctx, 0);
	SET_FSRRESTORE(base, ctx, 0);
	SET_TTBR0(base, ctx, 0);
	SET_TTBR1(base, ctx, 0);
	SET_TTBCR(base, ctx, 0);
	SET_BFBCR(base, ctx, 0);
	SET_PAR(base, ctx, 0);
	SET_FAR(base, ctx, 0);
	SET_CTX_TLBIALL(base, ctx, 0);
	SET_TLBFLPTER(base, ctx, 0);
	SET_TLBSLPTER(base, ctx, 0);
	SET_TLBLKCR(base, ctx, 0);
}

static void __program_context(void __iomem *base, int ctx,
			      struct msm_priv *priv)
{
	__reset_context(base, ctx);

	/* Turn on TEX Remap */
	SET_TRE(base, ctx, 1);
	SET_AFE(base, ctx, 1);

	/* Set up HTW mode */
	/* TLB miss configuration: perform HTW on miss */
	SET_TLBMCFG(base, ctx, 0x3);

	/* V2P configuration: HTW for access */
	SET_V2PCFG(base, ctx, 0x3);

	SET_TTBCR(base, ctx, priv->cfg.arm_v7s_cfg.tcr);
	SET_TTBR0(base, ctx, priv->cfg.arm_v7s_cfg.ttbr);
	SET_TTBR1(base, ctx, 0);

	/* Set prrr and nmrr */
	SET_PRRR(base, ctx, priv->cfg.arm_v7s_cfg.prrr);
	SET_NMRR(base, ctx, priv->cfg.arm_v7s_cfg.nmrr);

	/* Invalidate the TLB for this context */
	SET_CTX_TLBIALL(base, ctx, 0);

	/* Set interrupt number to "secure" interrupt */
	SET_IRPTNDX(base, ctx, 0);

	/* Enable context fault interrupt */
	SET_CFEIE(base, ctx, 1);

	/* Stall access on a context fault and let the handler deal with it */
	SET_CFCFG(base, ctx, 1);

	/* Redirect all cacheable requests to L2 slave port. */
	SET_RCISH(base, ctx, 1);
	SET_RCOSH(base, ctx, 1);
	SET_RCNSH(base, ctx, 1);

	/* Turn on BFB prefetch */
	SET_BFBDFE(base, ctx, 1);

	/* Enable the MMU */
	SET_M(base, ctx, 1);
}

static struct iommu_domain *msm_iommu_domain_alloc_paging(struct device *dev)
{
	struct msm_priv *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		goto fail_nomem;

	INIT_LIST_HEAD(&priv->list_attached);

	priv->domain.geometry.aperture_start = 0;
	priv->domain.geometry.aperture_end   = (1ULL << 32) - 1;
	priv->domain.geometry.force_aperture = true;

	return &priv->domain;

fail_nomem:
	kfree(priv);
	return NULL;
}

static void msm_iommu_domain_free(struct iommu_domain *domain)
{
	struct msm_priv *priv;
	unsigned long flags;

	spin_lock_irqsave(&msm_iommu_lock, flags);
	priv = to_msm_priv(domain);
	kfree(priv);
	spin_unlock_irqrestore(&msm_iommu_lock, flags);
}

static int msm_iommu_domain_config(struct msm_priv *priv)
{
	spin_lock_init(&priv->pgtlock);

	priv->cfg = (struct io_pgtable_cfg) {
		.pgsize_bitmap = msm_iommu_ops.pgsize_bitmap,
		.ias = 32,
		.oas = 32,
		.tlb = &msm_iommu_flush_ops,
		.iommu_dev = priv->dev,
	};

	priv->iop = alloc_io_pgtable_ops(ARM_V7S, &priv->cfg, priv);
	if (!priv->iop) {
		dev_err(priv->dev, "Failed to allocate pgtable\n");
		return -EINVAL;
	}

	msm_iommu_ops.pgsize_bitmap = priv->cfg.pgsize_bitmap;

	return 0;
}

/* Must be called under msm_iommu_lock */
static struct msm_iommu_dev *find_iommu_for_dev(struct device *dev)
{
	struct msm_iommu_dev *iommu, *ret = NULL;
	struct msm_iommu_ctx_dev *master;

	list_for_each_entry(iommu, &qcom_iommu_devices, dev_node) {
		master = list_first_entry(&iommu->ctx_list,
					  struct msm_iommu_ctx_dev,
					  list);
		if (master->of_node == dev->of_node) {
			ret = iommu;
			break;
		}
	}

	return ret;
}

static struct iommu_device *msm_iommu_probe_device(struct device *dev)
{
	struct msm_iommu_dev *iommu;
	unsigned long flags;

	spin_lock_irqsave(&msm_iommu_lock, flags);
	iommu = find_iommu_for_dev(dev);
	spin_unlock_irqrestore(&msm_iommu_lock, flags);

	if (!iommu)
		return ERR_PTR(-ENODEV);

	return &iommu->iommu;
}

static int msm_iommu_attach_dev(struct iommu_domain *domain, struct device *dev)
{
	int ret = 0;
	unsigned long flags;
	struct msm_iommu_dev *iommu;
	struct msm_priv *priv = to_msm_priv(domain);
	struct msm_iommu_ctx_dev *master;

	priv->dev = dev;
	msm_iommu_domain_config(priv);

	spin_lock_irqsave(&msm_iommu_lock, flags);
	list_for_each_entry(iommu, &qcom_iommu_devices, dev_node) {
		master = list_first_entry(&iommu->ctx_list,
					  struct msm_iommu_ctx_dev,
					  list);
		if (master->of_node == dev->of_node) {
			ret = __enable_clocks(iommu);
			if (ret)
				goto fail;

			list_for_each_entry(master, &iommu->ctx_list, list) {
				if (master->num) {
					dev_err(dev, "domain already attached");
					ret = -EEXIST;
					goto fail;
				}
				master->num =
					msm_iommu_alloc_ctx(iommu->context_map,
							    0, iommu->ncb);
				if (IS_ERR_VALUE(master->num)) {
					ret = -ENODEV;
					goto fail;
				}
				config_mids(iommu, master);
				__program_context(iommu->base, master->num,
						  priv);
			}
			__disable_clocks(iommu);
			list_add(&iommu->dom_node, &priv->list_attached);
		}
	}

fail:
	spin_unlock_irqrestore(&msm_iommu_lock, flags);

	return ret;
}

static int msm_iommu_identity_attach(struct iommu_domain *identity_domain,
				     struct device *dev)
{
	struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
	struct msm_priv *priv;
	unsigned long flags;
	struct msm_iommu_dev *iommu;
	struct msm_iommu_ctx_dev *master;
	int ret = 0;

	if (domain == identity_domain || !domain)
		return 0;

	priv = to_msm_priv(domain);
	free_io_pgtable_ops(priv->iop);

	spin_lock_irqsave(&msm_iommu_lock, flags);
	list_for_each_entry(iommu, &priv->list_attached, dom_node) {
		ret = __enable_clocks(iommu);
		if (ret)
			goto fail;

		list_for_each_entry(master, &iommu->ctx_list, list) {
			msm_iommu_free_ctx(iommu->context_map, master->num);
			__reset_context(iommu->base, master->num);
		}
		__disable_clocks(iommu);
	}
fail:
	spin_unlock_irqrestore(&msm_iommu_lock, flags);
	return ret;
}

static struct iommu_domain_ops msm_iommu_identity_ops = {
	.attach_dev = msm_iommu_identity_attach,
};

static struct iommu_domain msm_iommu_identity_domain = {
	.type = IOMMU_DOMAIN_IDENTITY,
	.ops = &msm_iommu_identity_ops,
};

static int msm_iommu_map(struct iommu_domain *domain, unsigned long iova,
			 phys_addr_t pa, size_t pgsize, size_t pgcount,
			 int prot, gfp_t gfp, size_t *mapped)
{
	struct msm_priv *priv = to_msm_priv(domain);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&priv->pgtlock, flags);
	ret = priv->iop->map_pages(priv->iop, iova, pa, pgsize, pgcount, prot,
				   GFP_ATOMIC, mapped);
	spin_unlock_irqrestore(&priv->pgtlock, flags);

	return ret;
}

static int msm_iommu_sync_map(struct iommu_domain *domain, unsigned long iova,
			      size_t size)
{
	struct msm_priv *priv = to_msm_priv(domain);

	__flush_iotlb_range(iova, size, SZ_4K, false, priv);
	return 0;
}

static size_t msm_iommu_unmap(struct iommu_domain *domain, unsigned long iova,
			      size_t pgsize, size_t pgcount,
			      struct iommu_iotlb_gather *gather)
{
	struct msm_priv *priv = to_msm_priv(domain);
	unsigned long flags;
	size_t ret;

	spin_lock_irqsave(&priv->pgtlock, flags);
	ret = priv->iop->unmap_pages(priv->iop, iova, pgsize, pgcount, gather);
	spin_unlock_irqrestore(&priv->pgtlock, flags);

	return ret;
}

static phys_addr_t msm_iommu_iova_to_phys(struct iommu_domain *domain,
					  dma_addr_t va)
{
	struct msm_priv *priv;
	struct msm_iommu_dev *iommu;
	struct msm_iommu_ctx_dev *master;
	unsigned int par;
	unsigned long flags;
	phys_addr_t ret = 0;

	spin_lock_irqsave(&msm_iommu_lock, flags);

	priv = to_msm_priv(domain);
	iommu = list_first_entry(&priv->list_attached,
				 struct msm_iommu_dev, dom_node);

	if (list_empty(&iommu->ctx_list))
		goto fail;

	master = list_first_entry(&iommu->ctx_list,
				  struct msm_iommu_ctx_dev, list);
	if (!master)
		goto fail;

	ret = __enable_clocks(iommu);
	if (ret)
		goto fail;

	/* Invalidate context TLB */
	SET_CTX_TLBIALL(iommu->base, master->num, 0);
	SET_V2PPR(iommu->base, master->num, va & V2Pxx_VA);

	par = GET_PAR(iommu->base, master->num);

	/* We are dealing with a supersection */
	if (GET_NOFAULT_SS(iommu->base, master->num))
		ret = (par & 0xFF000000) | (va & 0x00FFFFFF);
	else	/* Upper 20 bits from PAR, lower 12 from VA */
		ret = (par & 0xFFFFF000) | (va & 0x00000FFF);

	if (GET_FAULT(iommu->base, master->num))
		ret = 0;

	__disable_clocks(iommu);
fail:
	spin_unlock_irqrestore(&msm_iommu_lock, flags);
	return ret;
}

static void print_ctx_regs(void __iomem *base, int ctx)
{
	unsigned int fsr = GET_FSR(base, ctx);
	pr_err("FAR    = %08x    PAR    = %08x\n",
	       GET_FAR(base, ctx), GET_PAR(base, ctx));
	pr_err("FSR    = %08x [%s%s%s%s%s%s%s%s%s%s]\n", fsr,
			(fsr & 0x02) ? "TF " : "",
			(fsr & 0x04) ? "AFF " : "",
			(fsr & 0x08) ? "APF " : "",
			(fsr & 0x10) ? "TLBMF " : "",
			(fsr & 0x20) ? "HTWDEEF " : "",
			(fsr & 0x40) ? "HTWSEEF " : "",
			(fsr & 0x80) ? "MHF " : "",
			(fsr & 0x10000) ? "SL " : "",
			(fsr & 0x40000000) ? "SS " : "",
			(fsr & 0x80000000) ? "MULTI " : "");

	pr_err("FSYNR0 = %08x    FSYNR1 = %08x\n",
	       GET_FSYNR0(base, ctx), GET_FSYNR1(base, ctx));
	pr_err("TTBR0  = %08x    TTBR1  = %08x\n",
	       GET_TTBR0(base, ctx), GET_TTBR1(base, ctx));
	pr_err("SCTLR  = %08x    ACTLR  = %08x\n",
	       GET_SCTLR(base, ctx), GET_ACTLR(base, ctx));
}

static int insert_iommu_master(struct device *dev,
				struct msm_iommu_dev **iommu,
				const struct of_phandle_args *spec)
{
	struct msm_iommu_ctx_dev *master = dev_iommu_priv_get(dev);
	int sid;

	if (list_empty(&(*iommu)->ctx_list)) {
		master = kzalloc(sizeof(*master), GFP_ATOMIC);
		if (!master) {
			dev_err(dev, "Failed to allocate iommu_master\n");
			return -ENOMEM;
		}
		master->of_node = dev->of_node;
		list_add(&master->list, &(*iommu)->ctx_list);
		dev_iommu_priv_set(dev, master);
	}

	for (sid = 0; sid < master->num_mids; sid++)
		if (master->mids[sid] == spec->args[0]) {
			dev_warn(dev, "Stream ID 0x%x repeated; ignoring\n",
				 sid);
			return 0;
		}

	master->mids[master->num_mids++] = spec->args[0];
	return 0;
}

static int qcom_iommu_of_xlate(struct device *dev,
			       const struct of_phandle_args *spec)
{
	struct msm_iommu_dev *iommu = NULL, *iter;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&msm_iommu_lock, flags);
	list_for_each_entry(iter, &qcom_iommu_devices, dev_node) {
		if (iter->dev->of_node == spec->np) {
			iommu = iter;
			break;
		}
	}

	if (!iommu) {
		ret = -ENODEV;
		goto fail;
	}

	ret = insert_iommu_master(dev, &iommu, spec);
fail:
	spin_unlock_irqrestore(&msm_iommu_lock, flags);

	return ret;
}

irqreturn_t msm_iommu_fault_handler(int irq, void *dev_id)
{
	struct msm_iommu_dev *iommu = dev_id;
	unsigned int fsr;
	int i, ret;

	spin_lock(&msm_iommu_lock);

	if (!iommu) {
		pr_err("Invalid device ID in context interrupt handler\n");
		goto fail;
	}

	pr_err("Unexpected IOMMU page fault!\n");
	pr_err("base = %08x\n", (unsigned int)iommu->base);

	ret = __enable_clocks(iommu);
	if (ret)
		goto fail;

	for (i = 0; i < iommu->ncb; i++) {
		fsr = GET_FSR(iommu->base, i);
		if (fsr) {
			pr_err("Fault occurred in context %d.\n", i);
			pr_err("Interesting registers:\n");
			print_ctx_regs(iommu->base, i);
			SET_FSR(iommu->base, i, 0x4000000F);
		}
	}
	__disable_clocks(iommu);
fail:
	spin_unlock(&msm_iommu_lock);
	return 0;
}

static struct iommu_ops msm_iommu_ops = {
	.identity_domain = &msm_iommu_identity_domain,
	.domain_alloc_paging = msm_iommu_domain_alloc_paging,
	.probe_device = msm_iommu_probe_device,
	.device_group = generic_device_group,
	.pgsize_bitmap = MSM_IOMMU_PGSIZES,
	.of_xlate = qcom_iommu_of_xlate,
	.default_domain_ops = &(const struct iommu_domain_ops) {
		.attach_dev	= msm_iommu_attach_dev,
		.map_pages	= msm_iommu_map,
		.unmap_pages	= msm_iommu_unmap,
		/*
		 * Nothing is needed here, the barrier to guarantee
		 * completion of the tlb sync operation is implicitly
		 * taken care when the iommu client does a writel before
		 * kick starting the other master.
		 */
		.iotlb_sync	= NULL,
		.iotlb_sync_map	= msm_iommu_sync_map,
		.iova_to_phys	= msm_iommu_iova_to_phys,
		.free		= msm_iommu_domain_free,
	}
};

static int msm_iommu_probe(struct platform_device *pdev)
{
	struct resource *r;
	resource_size_t ioaddr;
	struct msm_iommu_dev *iommu;
	int ret, par, val;

	iommu = devm_kzalloc(&pdev->dev, sizeof(*iommu), GFP_KERNEL);
	if (!iommu)
		return -ENODEV;

	iommu->dev = &pdev->dev;
	INIT_LIST_HEAD(&iommu->ctx_list);

	iommu->pclk = devm_clk_get_prepared(iommu->dev, "smmu_pclk");
	if (IS_ERR(iommu->pclk))
		return dev_err_probe(iommu->dev, PTR_ERR(iommu->pclk),
				     "could not get smmu_pclk\n");

	iommu->clk = devm_clk_get_prepared(iommu->dev, "iommu_clk");
	if (IS_ERR(iommu->clk))
		return dev_err_probe(iommu->dev, PTR_ERR(iommu->clk),
				     "could not get iommu_clk\n");

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	iommu->base = devm_ioremap_resource(iommu->dev, r);
	if (IS_ERR(iommu->base)) {
		ret = dev_err_probe(iommu->dev, PTR_ERR(iommu->base), "could not get iommu base\n");
		return ret;
	}
	ioaddr = r->start;

	iommu->irq = platform_get_irq(pdev, 0);
	if (iommu->irq < 0)
		return -ENODEV;

	ret = of_property_read_u32(iommu->dev->of_node, "qcom,ncb", &val);
	if (ret) {
		dev_err(iommu->dev, "could not get ncb\n");
		return ret;
	}
	iommu->ncb = val;

	msm_iommu_reset(iommu->base, iommu->ncb);
	SET_M(iommu->base, 0, 1);
	SET_PAR(iommu->base, 0, 0);
	SET_V2PCFG(iommu->base, 0, 1);
	SET_V2PPR(iommu->base, 0, 0);
	par = GET_PAR(iommu->base, 0);
	SET_V2PCFG(iommu->base, 0, 0);
	SET_M(iommu->base, 0, 0);

	if (!par) {
		pr_err("Invalid PAR value detected\n");
		return -ENODEV;
	}

	ret = devm_request_threaded_irq(iommu->dev, iommu->irq, NULL,
					msm_iommu_fault_handler,
					IRQF_ONESHOT | IRQF_SHARED,
					"msm_iommu_secure_irpt_handler",
					iommu);
	if (ret) {
		pr_err("Request IRQ %d failed with ret=%d\n", iommu->irq, ret);
		return ret;
	}

	list_add(&iommu->dev_node, &qcom_iommu_devices);

	ret = iommu_device_sysfs_add(&iommu->iommu, iommu->dev, NULL,
				     "msm-smmu.%pa", &ioaddr);
	if (ret) {
		pr_err("Could not add msm-smmu at %pa to sysfs\n", &ioaddr);
		return ret;
	}

	ret = iommu_device_register(&iommu->iommu, &msm_iommu_ops, &pdev->dev);
	if (ret) {
		pr_err("Could not register msm-smmu at %pa\n", &ioaddr);
		return ret;
	}

	pr_info("device mapped at %p, irq %d with %d ctx banks\n",
		iommu->base, iommu->irq, iommu->ncb);

	return ret;
}

static const struct of_device_id msm_iommu_dt_match[] = {
	{ .compatible = "qcom,apq8064-iommu" },
	{}
};

static struct platform_driver msm_iommu_driver = {
	.driver = {
		.name	= "msm_iommu",
		.of_match_table = msm_iommu_dt_match,
	},
	.probe		= msm_iommu_probe,
};
builtin_platform_driver(msm_iommu_driver);

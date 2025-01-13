// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/acpi.h>
#include <linux/adreno-smmu-priv.h>
#include <linux/delay.h>
#include <linux/bitfield.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <linux/of_device.h>
#include <linux/qcom_scm.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include "arm-smmu.h"
#include "arm-smmu-qcom.h"

#define QCOM_DUMMY_VAL	-1

#define IMPL_DEF4_MICRO_MMU_CTRL	0
#define IMPL_DEF4_CLK_ON_STATUS		0x50
#define IMPL_DEF4_CLK_ON_CLIENT_STATUS	0x54
#define MICRO_MMU_CTRL_LOCAL_HALT_REQ	BIT(2)
#define MICRO_MMU_CTRL_IDLE		BIT(3)

#include "arm-smmu-debug.h"
#include <linux/debugfs.h>
#include <linux/uaccess.h>

/* Definitions for implementation-defined registers */
#define ACTLR_QCOM_OSH			BIT(28)
#define ACTLR_QCOM_ISH			BIT(29)
#define ACTLR_QCOM_NSH			BIT(30)

struct arm_smmu_impl_def_reg {
	u32 offset;
	u32 value;
};

struct qsmmuv2_archdata {
	spinlock_t			atos_lock;
	struct arm_smmu_impl_def_reg	*impl_def_attach_registers;
	unsigned int			num_impl_def_attach_registers;
	struct arm_smmu_device		smmu;
};

#define to_qsmmuv2_archdata(smmu)				\
	container_of(smmu, struct qsmmuv2_archdata, smmu)

static int qsmmuv2_wait_for_halt(struct arm_smmu_device *smmu)
{
	void __iomem *reg = arm_smmu_page(smmu, ARM_SMMU_IMPL_DEF4);
	struct device *dev = smmu->dev;
	u32 tmp;

	if (readl_poll_timeout_atomic(reg + IMPL_DEF4_MICRO_MMU_CTRL, tmp,
				(tmp & MICRO_MMU_CTRL_IDLE), 0, 30000)) {
		dev_err(dev, "Couldn't halt SMMU!\n");
		return -EBUSY;
	}

	return 0;
}

static int __qsmmuv2_halt(struct arm_smmu_device *smmu, bool wait)
{
	u32 val;

	val = arm_smmu_readl(smmu, ARM_SMMU_IMPL_DEF4,
				     IMPL_DEF4_MICRO_MMU_CTRL);
	val |= MICRO_MMU_CTRL_LOCAL_HALT_REQ;

	arm_smmu_writel(smmu, ARM_SMMU_IMPL_DEF4, IMPL_DEF4_MICRO_MMU_CTRL,
				val);

	return wait ? qsmmuv2_wait_for_halt(smmu) : 0;
}

static int qsmmuv2_halt(struct arm_smmu_device *smmu)
{
	return __qsmmuv2_halt(smmu, true);
}

static int qsmmuv2_halt_nowait(struct arm_smmu_device *smmu)
{
	return __qsmmuv2_halt(smmu, false);
}

static void qsmmuv2_resume(struct arm_smmu_device *smmu)
{
	u32 val;

	val = arm_smmu_readl(smmu, ARM_SMMU_IMPL_DEF4,
			     IMPL_DEF4_MICRO_MMU_CTRL);
	val &= ~MICRO_MMU_CTRL_LOCAL_HALT_REQ;

	arm_smmu_writel(smmu, ARM_SMMU_IMPL_DEF4, IMPL_DEF4_MICRO_MMU_CTRL,
				val);
}


static phys_addr_t __qsmmuv2_iova_to_phys_hard(
					struct arm_smmu_domain *smmu_domain,
					dma_addr_t iova)
{
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	struct arm_smmu_cfg *cfg = &smmu_domain->cfg;
	struct device *dev = smmu->dev;
	int idx = cfg->cbndx;
	void __iomem *reg;
	u32 tmp;
	u64 phys;
	unsigned long va;

	/* ATS1 registers can only be written atomically */
	va = iova & ~0xfffUL;
	if (cfg->fmt == ARM_SMMU_CTX_FMT_AARCH64)
		arm_smmu_cb_writeq(smmu, idx, ARM_SMMU_CB_ATS1PR, va);
	else
		arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_ATS1PR, va);

	reg = arm_smmu_page(smmu, ARM_SMMU_CB(smmu, idx));
	if (readl_poll_timeout_atomic(reg + ARM_SMMU_CB_ATSR, tmp,
				      !(tmp & ARM_SMMU_ATSR_ACTIVE), 5, 50)) {
		dev_err(dev, "iova to phys timed out on %pad.\n", &iova);
		phys = 0;
		return phys;
	}

	phys = arm_smmu_cb_readq(smmu, idx, ARM_SMMU_CB_PAR);
	if (phys & ARM_SMMU_CB_PAR_F) {
		dev_err(dev, "translation fault!\n");
		dev_err(dev, "PAR = 0x%llx\n", phys);
		phys = 0;
	} else {
		phys = (phys & (PHYS_MASK & ~0xfffULL)) | (iova & 0xfff);
	}

	return phys;
}

static phys_addr_t qsmmuv2_iova_to_phys_hard(
					struct arm_smmu_domain *smmu_domain,
					struct qcom_iommu_atos_txn *txn)
{
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	struct qsmmuv2_archdata *data = to_qsmmuv2_archdata(smmu);
	int idx = smmu_domain->cfg.cbndx;
	dma_addr_t iova = txn->addr;
	phys_addr_t phys = 0;
	unsigned long flags;
	u32 sctlr, sctlr_orig, fsr;

	spin_lock_irqsave(&data->atos_lock, flags);

	qsmmuv2_halt_nowait(smmu);

	/* disable stall mode momentarily */
	sctlr_orig = arm_smmu_cb_read(smmu, idx, ARM_SMMU_CB_SCTLR);
	sctlr = sctlr_orig & ~(ARM_SMMU_SCTLR_CFCFG);
	arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_SCTLR, sctlr);

	/* clear FSR to allow ATOS to log any faults */
	fsr = arm_smmu_cb_read(smmu, idx, ARM_SMMU_CB_FSR);
	if (fsr & ARM_SMMU_FSR_FAULT) {
		/* Clear pending interrupts */
		arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_FSR, fsr);
		/*
		 * Barrier required to ensure that the FSR is cleared
		 * before resuming SMMU operation
		 */
		wmb();

		/*
		 *  TBU halt takes care of resuming any stalled transcation.
		 *  Kept it here for completeness sake.
		 */
		if (fsr & ARM_SMMU_FSR_SS)
			arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_RESUME,
				ARM_SMMU_RESUME_TERMINATE);
	}

	qsmmuv2_wait_for_halt(smmu);

	phys = __qsmmuv2_iova_to_phys_hard(smmu_domain, iova);

	/* restore SCTLR */
	arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_SCTLR, sctlr_orig);

	qsmmuv2_resume(smmu);
	spin_unlock_irqrestore(&data->atos_lock, flags);

	return phys;
}

static void qsmmuv2_tlb_sync_timeout(struct arm_smmu_device *smmu)
{
	u32 clk_on, clk_on_client;

	dev_err_ratelimited(smmu->dev,
			"TLB sync timed out -- SMMU may be deadlocked\n");

	clk_on = arm_smmu_readl(smmu, ARM_SMMU_IMPL_DEF4,
				IMPL_DEF4_CLK_ON_STATUS);
	clk_on_client = arm_smmu_readl(smmu, ARM_SMMU_IMPL_DEF4,
				IMPL_DEF4_CLK_ON_CLIENT_STATUS);
	dev_err_ratelimited(smmu->dev,
				    "clk on 0x%x, clk on client 0x%x status\n",
						    clk_on, clk_on_client);

	BUG_ON(IS_ENABLED(CONFIG_IOMMU_TLBSYNC_DEBUG));
}

static int qsmmuv2_device_reset(struct arm_smmu_device *smmu)
{
	struct qsmmuv2_archdata *data = to_qsmmuv2_archdata(smmu);
	struct arm_smmu_impl_def_reg *regs = data->impl_def_attach_registers;
	u32 i;

	/* Program implementation defined registers */
	qsmmuv2_halt(smmu);
	for (i = 0; i < data->num_impl_def_attach_registers; ++i)
		arm_smmu_gr0_write(smmu, regs[i].offset, regs[i].value);
	qsmmuv2_resume(smmu);

	return 0;
}

static void qsmmuv2_init_cb(struct arm_smmu_domain *smmu_domain,
					struct device *dev)
{
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	int idx = smmu_domain->cfg.cbndx;
	const struct iommu_flush_ops *tlb;
	u32 val;

	tlb = smmu_domain->flush_ops;

	val = ACTLR_QCOM_ISH | ACTLR_QCOM_OSH | ACTLR_QCOM_NSH;

	arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_ACTLR, val);

	/*
	 * Flush the context bank after modifying ACTLR to ensure there
	 * are no cache entries with stale state
	 */
	tlb->tlb_flush_all(smmu_domain);
}

static int arm_smmu_parse_impl_def_registers(struct arm_smmu_device *smmu)
{
	struct device *dev = smmu->dev;
	struct qsmmuv2_archdata *data = to_qsmmuv2_archdata(smmu);
	int i, ntuples, ret;
	u32 *tuples;
	struct arm_smmu_impl_def_reg *regs, *regit;

	if (!of_find_property(dev->of_node, "attach-impl-defs", &ntuples))
		return 0;

	ntuples /= sizeof(u32);
	if (ntuples % 2) {
		dev_err(dev,
			"Invalid number of attach-impl-defs registers: %d\n",
			ntuples);
		return -EINVAL;
	}

	regs = devm_kzalloc(dev, sizeof(*data->impl_def_attach_registers) *
		ntuples, GFP_KERNEL);

	if (!regs)
		return -ENOMEM;

	tuples = kzalloc(sizeof(u32) * ntuples * 2, GFP_KERNEL);
	if (!tuples)
		return -ENOMEM;

	ret = of_property_read_u32_array(dev->of_node, "attach-impl-defs",
					tuples, ntuples);
	if (ret) {
		kfree(tuples);
		return ret;
	}

	for (i = 0, regit = regs; i < ntuples; i += 2, ++regit) {
		regit->offset = tuples[i];
		regit->value = tuples[i + 1];
	}

	kfree(tuples);

	data->impl_def_attach_registers = regs;
	data->num_impl_def_attach_registers = ntuples / 2;

	return 0;
}


static struct qcom_smmu *to_qcom_smmu(struct arm_smmu_device *smmu)
{
	return container_of(smmu, struct qcom_smmu, smmu);
}

static void qcom_smmu_tlb_sync(struct arm_smmu_device *smmu, int page,
				int sync, int status)
{
	unsigned int spin_cnt, delay;
	u32 reg;

	arm_smmu_writel(smmu, page, sync, QCOM_DUMMY_VAL);
	for (delay = 1; delay < TLB_LOOP_TIMEOUT; delay *= 2) {
		for (spin_cnt = TLB_SPIN_COUNT; spin_cnt > 0; spin_cnt--) {
			reg = arm_smmu_readl(smmu, page, status);
			if (!(reg & ARM_SMMU_sTLBGSTATUS_GSACTIVE))
				return;
			cpu_relax();
		}
		udelay(delay);
	}

	qcom_smmu_tlb_sync_debug(smmu);
}

static void qcom_adreno_smmu_write_sctlr(struct arm_smmu_device *smmu, int idx,
		u32 reg)
{
	struct qcom_smmu *qsmmu = to_qcom_smmu(smmu);

	/*
	 * On the GPU device we want to process subsequent transactions after a
	 * fault to keep the GPU from hanging
	 */
	reg |= ARM_SMMU_SCTLR_HUPCF;

	if (qsmmu->stall_enabled & BIT(idx))
		reg |= ARM_SMMU_SCTLR_CFCFG;

	arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_SCTLR, reg);
}

static void qcom_adreno_smmu_get_fault_info(const void *cookie,
		struct adreno_smmu_fault_info *info)
{
	struct arm_smmu_domain *smmu_domain = (void *)cookie;
	struct arm_smmu_cfg *cfg = &smmu_domain->cfg;
	struct arm_smmu_device *smmu = smmu_domain->smmu;

	info->fsr = arm_smmu_cb_read(smmu, cfg->cbndx, ARM_SMMU_CB_FSR);
	info->fsynr0 = arm_smmu_cb_read(smmu, cfg->cbndx, ARM_SMMU_CB_FSYNR0);
	info->fsynr1 = arm_smmu_cb_read(smmu, cfg->cbndx, ARM_SMMU_CB_FSYNR1);
	info->far = arm_smmu_cb_readq(smmu, cfg->cbndx, ARM_SMMU_CB_FAR);
	info->cbfrsynra = arm_smmu_gr1_read(smmu, ARM_SMMU_GR1_CBFRSYNRA(cfg->cbndx));
	info->ttbr0 = arm_smmu_cb_readq(smmu, cfg->cbndx, ARM_SMMU_CB_TTBR0);
	info->contextidr = arm_smmu_cb_read(smmu, cfg->cbndx, ARM_SMMU_CB_CONTEXTIDR);
}

static void qcom_adreno_smmu_set_stall(const void *cookie, bool enabled)
{
	struct arm_smmu_domain *smmu_domain = (void *)cookie;
	struct arm_smmu_cfg *cfg = &smmu_domain->cfg;
	struct qcom_smmu *qsmmu = to_qcom_smmu(smmu_domain->smmu);

	if (enabled)
		qsmmu->stall_enabled |= BIT(cfg->cbndx);
	else
		qsmmu->stall_enabled &= ~BIT(cfg->cbndx);
}

static void qcom_adreno_smmu_resume_translation(const void *cookie, bool terminate)
{
	struct arm_smmu_domain *smmu_domain = (void *)cookie;
	struct arm_smmu_cfg *cfg = &smmu_domain->cfg;
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	u32 reg = 0;

	if (terminate)
		reg |= ARM_SMMU_RESUME_TERMINATE;

	arm_smmu_cb_write(smmu, cfg->cbndx, ARM_SMMU_CB_RESUME, reg);
}

#define QCOM_ADRENO_SMMU_GPU_SID 0
#define QCOM_ADRENO_SMMU_GPU_LPAC_SID 1

static bool qcom_adreno_smmu_is_gpu_device(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	int i;

	/*
	 * The GPU will always use SID 0 so that is a handy way to uniquely
	 * identify it and configure it for per-instance pagetables
	 */
	for (i = 0; i < fwspec->num_ids; i++) {
		u16 sid = FIELD_GET(ARM_SMMU_SMR_ID, fwspec->ids[i]);

		if (sid == QCOM_ADRENO_SMMU_GPU_SID ||
				sid == QCOM_ADRENO_SMMU_GPU_LPAC_SID)
			return true;
	}

	return false;
}

static const struct io_pgtable_cfg *qcom_adreno_smmu_get_ttbr1_cfg(
		const void *cookie)
{
	struct arm_smmu_domain *smmu_domain = (void *)cookie;
	struct io_pgtable *pgtable =
		io_pgtable_ops_to_pgtable(smmu_domain->pgtbl_ops);
	return &pgtable->cfg;
}

/*
 * Local implementation to configure TTBR0 with the specified pagetable config.
 * The GPU driver will call this to enable TTBR0 when per-instance pagetables
 * are active
 */

static int qcom_adreno_smmu_set_ttbr0_cfg(const void *cookie,
		const struct io_pgtable_cfg *pgtbl_cfg)
{
	struct arm_smmu_domain *smmu_domain = (void *)cookie;
	struct io_pgtable *pgtable = io_pgtable_ops_to_pgtable(smmu_domain->pgtbl_ops);
	struct arm_smmu_cfg *cfg = &smmu_domain->cfg;
	struct arm_smmu_cb *cb = &smmu_domain->smmu->cbs[cfg->cbndx];

	/* The domain must have split pagetables already enabled */
	if (cb->tcr[0] & ARM_SMMU_TCR_EPD1)
		return -EINVAL;

	/* If the pagetable config is NULL, disable TTBR0 */
	if (!pgtbl_cfg) {
		/* Do nothing if it is already disabled */
		if ((cb->tcr[0] & ARM_SMMU_TCR_EPD0))
			return -EINVAL;

		/* Set TCR to the original configuration */
		cb->tcr[0] = arm_smmu_lpae_tcr(&pgtable->cfg);
		cb->ttbr[0] = FIELD_PREP(ARM_SMMU_TTBRn_ASID, cb->cfg->asid);
	} else {
		u32 tcr = cb->tcr[0];

		/* Don't call this again if TTBR0 is already enabled */
		if (!(cb->tcr[0] & ARM_SMMU_TCR_EPD0))
			return -EINVAL;

		tcr |= arm_smmu_lpae_tcr(pgtbl_cfg);
		tcr &= ~(ARM_SMMU_TCR_EPD0 | ARM_SMMU_TCR_EPD1);

		cb->tcr[0] = tcr;
		cb->ttbr[0] = pgtbl_cfg->arm_lpae_s1_cfg.ttbr;
		cb->ttbr[0] |= FIELD_PREP(ARM_SMMU_TTBRn_ASID, cb->cfg->asid);
	}

	arm_smmu_write_context_bank(smmu_domain->smmu, cb->cfg->cbndx);

	return 0;
}

static int qcom_adreno_smmu_alloc_context_bank(struct arm_smmu_domain *smmu_domain,
					       struct arm_smmu_device *smmu,
					       struct device *dev, int start)
{
	int count;

	/*
	 * Assign context bank 0 and 1 to the GPU device so the GPU hardware can
	 * switch pagetables
	 */
	if (qcom_adreno_smmu_is_gpu_device(dev)) {
		start = 0;
		count = 2;
	} else {
		start = 2;
		count = smmu->num_context_banks;
	}

	return __arm_smmu_alloc_bitmap(smmu->context_map, start, count);
}

static bool qcom_adreno_can_do_ttbr1(struct arm_smmu_device *smmu)
{
	const struct device_node *np = smmu->dev->of_node;

	if (of_device_is_compatible(np, "qcom,msm8996-smmu-v2"))
		return false;

	return true;
}

static const struct of_device_id __maybe_unused qcom_smmu_impl_of_match[];
static int qcom_adreno_smmu_init_context(struct arm_smmu_domain *smmu_domain,
		struct io_pgtable_cfg *pgtbl_cfg, struct device *dev)
{
	struct adreno_smmu_priv *priv;
	const struct device_node *np = smmu_domain->smmu->dev->of_node;
	struct qcom_io_pgtable_info *input_info =
		container_of(pgtbl_cfg, struct qcom_io_pgtable_info, cfg);

	smmu_domain->cfg.flush_walk_prefer_tlbiasid = true;

	/* Only enable split pagetables for the GPU device (SID 0) */
	if (!qcom_adreno_smmu_is_gpu_device(dev))
		return 0;

	/*
	 * All targets that use the qcom,adreno-smmu compatible string *should*
	 * be AARCH64 stage 1 but double check because the arm-smmu code assumes
	 * that is the case when the TTBR1 quirk is enabled
	 */
	if (qcom_adreno_can_do_ttbr1(smmu_domain->smmu) &&
	    (smmu_domain->stage == ARM_SMMU_DOMAIN_S1) &&
	    (smmu_domain->cfg.fmt == ARM_SMMU_CTX_FMT_AARCH64))
		pgtbl_cfg->quirks |= IO_PGTABLE_QUIRK_ARM_TTBR1;

	/*
	 * Initialize private interface with GPU:
	 */

	priv = dev_get_drvdata(dev);
	priv->cookie = smmu_domain;
	priv->get_ttbr1_cfg = qcom_adreno_smmu_get_ttbr1_cfg;
	priv->set_ttbr0_cfg = qcom_adreno_smmu_set_ttbr0_cfg;
	priv->get_fault_info = qcom_adreno_smmu_get_fault_info;
	priv->pgtbl_info = *input_info;

	/*
	 * These functions are only compatible with the data structures used by the
	 * QCOM SMMU implementation hooks, and are thus not appropriate to set for other
	 * implementations (e.g. QSMMUV500).
	 *
	 * Providing these functions as part of the GPU interface also makes little sense
	 * as context banks are set to stall by default anyway.
	 */
	if (of_match_node(qcom_smmu_impl_of_match, np)) {
		priv->set_stall = qcom_adreno_smmu_set_stall;
		priv->resume_translation = qcom_adreno_smmu_resume_translation;
	}

	return 0;
}

static const struct of_device_id qcom_smmu_client_of_match[] __maybe_unused = {
	{ .compatible = "qcom,adreno" },
	{ .compatible = "qcom,adreno-gmu" },
	{ .compatible = "qcom,mdp4" },
	{ .compatible = "qcom,mdss" },
	{ .compatible = "qcom,sc7180-mdss" },
	{ .compatible = "qcom,sc7180-mss-pil" },
	{ .compatible = "qcom,sc7280-mdss" },
	{ .compatible = "qcom,sc7280-mss-pil" },
	{ .compatible = "qcom,sc8180x-mdss" },
	{ .compatible = "qcom,sm8250-mdss" },
	{ .compatible = "qcom,sdm845-mdss" },
	{ .compatible = "qcom,sdm845-mss-pil" },
	{ }
};

static int qcom_smmu_init_context(struct arm_smmu_domain *smmu_domain,
		struct io_pgtable_cfg *pgtbl_cfg, struct device *dev)
{
	smmu_domain->cfg.flush_walk_prefer_tlbiasid = true;

	return 0;
}

static int qcom_smmu_cfg_probe(struct arm_smmu_device *smmu)
{
	struct qcom_smmu *qsmmu = to_qcom_smmu(smmu);
	unsigned int last_s2cr;
	u32 reg;
	u32 smr;
	int i;

	/*
	 * MSM8998 LPASS SMMU reports 13 context banks, but accessing
	 * the last context bank crashes the system.
	 */
	if (of_device_is_compatible(smmu->dev->of_node, "qcom,msm8998-smmu-v2") && smmu->num_context_banks == 13)
		smmu->num_context_banks = 12;

	/*
	 * Some platforms support more than the Arm SMMU architected maximum of
	 * 128 stream matching groups. For unknown reasons, the additional
	 * groups don't exhibit the same behavior as the architected registers,
	 * so limit the groups to 128 until the behavior is fixed for the other
	 * groups.
	 */
	if (smmu->num_mapping_groups > 128) {
		dev_notice(smmu->dev, "\tLimiting the stream matching groups to 128\n");
		smmu->num_mapping_groups = 128;
	}

	last_s2cr = ARM_SMMU_GR0_S2CR(smmu->num_mapping_groups - 1);

	/*
	 * With some firmware versions writes to S2CR of type FAULT are
	 * ignored, and writing BYPASS will end up written as FAULT in the
	 * register. Perform a write to S2CR to detect if this is the case and
	 * if so reserve a context bank to emulate bypass streams.
	 */
	reg = FIELD_PREP(ARM_SMMU_S2CR_TYPE, S2CR_TYPE_BYPASS) |
	      FIELD_PREP(ARM_SMMU_S2CR_CBNDX, 0xff) |
	      FIELD_PREP(ARM_SMMU_S2CR_PRIVCFG, S2CR_PRIVCFG_DEFAULT);
	arm_smmu_gr0_write(smmu, last_s2cr, reg);
	reg = arm_smmu_gr0_read(smmu, last_s2cr);
	if (FIELD_GET(ARM_SMMU_S2CR_TYPE, reg) != S2CR_TYPE_BYPASS) {
		qsmmu->bypass_quirk = true;
		qsmmu->bypass_cbndx = smmu->num_context_banks - 1;

		set_bit(qsmmu->bypass_cbndx, smmu->context_map);

		arm_smmu_cb_write(smmu, qsmmu->bypass_cbndx, ARM_SMMU_CB_SCTLR, 0);

		reg = FIELD_PREP(ARM_SMMU_CBAR_TYPE, CBAR_TYPE_S1_TRANS_S2_BYPASS);
		arm_smmu_gr1_write(smmu, ARM_SMMU_GR1_CBAR(qsmmu->bypass_cbndx), reg);
	}

	for (i = 0; i < smmu->num_mapping_groups; i++) {
		smr = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_SMR(i));

		if (FIELD_GET(ARM_SMMU_SMR_VALID, smr)) {
			/* Ignore valid bit for SMR mask extraction. */
			smr &= ~ARM_SMMU_SMR_VALID;
			smmu->smrs[i].id = FIELD_GET(ARM_SMMU_SMR_ID, smr);
			smmu->smrs[i].mask = FIELD_GET(ARM_SMMU_SMR_MASK, smr);
			smmu->smrs[i].valid = true;
			smmu->smrs[i].used = true;

			smmu->s2crs[i].type = S2CR_TYPE_BYPASS;
			smmu->s2crs[i].privcfg = S2CR_PRIVCFG_DEFAULT;
			smmu->s2crs[i].cbndx = 0xff;
		}
	}

	return 0;
}

static void qcom_smmu_write_s2cr(struct arm_smmu_device *smmu, int idx)
{
	struct arm_smmu_s2cr *s2cr = smmu->s2crs + idx;
	struct qcom_smmu *qsmmu = to_qcom_smmu(smmu);
	u32 cbndx = s2cr->cbndx;
	u32 type = s2cr->type;
	u32 reg;

	if (qsmmu->bypass_quirk) {
		if (type == S2CR_TYPE_BYPASS) {
			/*
			 * Firmware with quirky S2CR handling will substitute
			 * BYPASS writes with FAULT, so point the stream to the
			 * reserved context bank and ask for translation on the
			 * stream
			 */
			type = S2CR_TYPE_TRANS;
			cbndx = qsmmu->bypass_cbndx;
		} else if (type == S2CR_TYPE_FAULT) {
			/*
			 * Firmware with quirky S2CR handling will ignore FAULT
			 * writes, so trick it to write FAULT by asking for a
			 * BYPASS.
			 */
			type = S2CR_TYPE_BYPASS;
			cbndx = 0xff;
		}
	}

	reg = FIELD_PREP(ARM_SMMU_S2CR_TYPE, type) |
	      FIELD_PREP(ARM_SMMU_S2CR_CBNDX, cbndx) |
	      FIELD_PREP(ARM_SMMU_S2CR_PRIVCFG, s2cr->privcfg);
	arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_S2CR(idx), reg);
}

static int qcom_smmu_def_domain_type(struct device *dev)
{
	const struct of_device_id *match =
		of_match_device(qcom_smmu_client_of_match, dev);

	return match ? IOMMU_DOMAIN_IDENTITY : 0;
}

static int qcom_sdm845_smmu500_reset(struct arm_smmu_device *smmu)
{
	int ret;

	/*
	 * To address performance degradation in non-real time clients,
	 * such as USB and UFS, turn off wait-for-safe on sdm845 based boards,
	 * such as MTP and db845, whose firmwares implement secure monitor
	 * call handlers to turn on/off the wait-for-safe logic.
	 */
	ret = qcom_scm_qsmmu500_wait_safe_toggle(0);
	if (ret)
		dev_warn(smmu->dev, "Failed to turn off SAFE logic\n");

	return ret;
}

static int qcom_smmu500_reset(struct arm_smmu_device *smmu)
{
	const struct device_node *np = smmu->dev->of_node;

	arm_mmu500_reset(smmu);

	if (of_device_is_compatible(np, "qcom,sdm845-smmu-500"))
		return qcom_sdm845_smmu500_reset(smmu);

	return 0;
}

static const struct arm_smmu_impl qcom_smmu_impl = {
	.init_context = qcom_smmu_init_context,
	.cfg_probe = qcom_smmu_cfg_probe,
	.def_domain_type = qcom_smmu_def_domain_type,
	.reset = qcom_smmu500_reset,
	.write_s2cr = qcom_smmu_write_s2cr,
	.tlb_sync = qcom_smmu_tlb_sync,
};

#define TBUID_SHIFT			10

#define DEBUG_SID_HALT_REG		0x0
#define DEBUG_SID_HALT_REQ		BIT(16)
#define DEBUG_SID_HALT_SID		GENMASK(9, 0)

#define DEBUG_VA_ADDR_REG		0x8

#define DEBUG_TXN_TRIGG_REG		0x18
#define DEBUG_TXN_AXPROT		GENMASK(8, 6)
#define DEBUG_TXN_AXCACHE		GENMASK(5, 2)
#define DEBUG_TXN_WRITE			BIT(1)
#define DEBUG_TXN_AXPROT_PRIV		0x1
#define DEBUG_TXN_AXPROT_UNPRIV		0x0
#define DEBUG_TXN_AXPROT_NSEC		0x2
#define DEBUG_TXN_AXPROT_SEC		0x0
#define DEBUG_TXN_AXPROT_INST		0x4
#define DEBUG_TXN_AXPROT_DATA		0x0
#define DEBUG_TXN_READ			(0x0 << 1)
#define DEBUG_TXN_TRIGGER		BIT(0)

#define DEBUG_SR_HALT_ACK_REG		0x20
#define DEBUG_SR_HALT_ACK_VAL		(0x1 << 1)
#define DEBUG_SR_ECATS_RUNNING_VAL	(0x1 << 0)

#define DEBUG_PAR_REG			0x28
#define DEBUG_PAR_PA			GENMASK_ULL(47, 12)
#define DEBUG_PAR_FAULT_VAL		BIT(0)

#define DEBUG_AXUSER_REG		0x30
#define DEBUG_AXUSER_CDMID		GENMASK_ULL(43, 36)
#define DEBUG_AXUSER_CDMID_VAL          255

#define TBU_DBG_TIMEOUT_US		100
#define TBU_MICRO_IDLE_DELAY_US		5


/* QTB constants */
#define QTB_DBG_TIMEOUT_US		100

#define QTB_SWID_LOW			0x0

#define QTB_OVR_DBG_FENCEREQ		0x410
#define QTB_OVR_DBG_FENCEREQ_HALT	BIT(0)

#define QTB_OVR_DBG_FENCEACK		0x418
#define QTB_OVR_DBG_FENCEACK_ACK	BIT(0)

#define QTB_OVR_ECATS_INFLD0			0x430
#define QTB_OVR_ECATS_INFLD0_PCIE_NO_SNOOP	BIT(21)
#define QTB_OVR_ECATS_INFLD0_SEC_SID		BIT(20)
#define QTB_OVR_ECATS_INFLD0_QAD		GENMASK(19, 16)
#define QTB_OVR_ECATS_INFLD0_SID		GENMASK(9, 0)

#define QTB_OVR_ECATS_INFLD1		0x438
#define QTB_OVR_ECATS_INFLD1_PNU	BIT(13)
#define QTB_OVR_ECATS_INFLD1_IND	BIT(12)
#define QTB_OVR_ECATS_INFLD1_DIRTY	BIT(11)
#define QTB_OVR_ECATS_INFLD1_TR_TYPE	GENMASK(10, 8)
#define QTB_OVR_ECATS_INFLD1_TR_TYPE_SHARED 4
#define QTB_OVR_ECATS_INFLD1_ALLOC	GENMASK(7, 4)
#define QTB_OVR_ECATS_INFLD1_NON_SEC	BIT(3)
#define QTB_OVR_ECATS_INFLD1_OPC	GENMASK(2, 0)
#define QTB_OVR_ECATS_INFLD1_OPC_WRI	1

#define QTB_OVR_ECATS_INFLD2	0x440

#define QTB_OVR_ECATS_TRIGGER		0x448
#define QTB_OVR_ECATS_TRIGGER_START	BIT(0)

#define QTB_OVR_ECATS_STATUS		0x450
#define QTB_OVR_ECATS_STATUS_DONE	BIT(0)

#define QTB_OVR_ECATS_OUTFLD0			0x458
#define QTB_OVR_ECATS_OUTFLD0_PA		GENMASK_ULL(63, 12)
#define QTB_OVR_ECATS_OUTFLD0_FAULT_TYPE	GENMASK(5, 4)
#define QTB_OVR_ECATS_OUTFLD0_FAULT		BIT(0)

#define QTB_NS_DBG_PORT_N_OT_SNAPSHOT(port_num)	(0xc10 + (0x10 * port_num))

#define TCU_TESTBUS_SEL_ALL		0x7
#define TBU_TESTBUS_SEL_ALL		0x7f

struct actlr_setting {
	struct arm_smmu_smr smr;
	u32 actlr;
};

struct qsmmuv500_archdata {
	struct arm_smmu_device		smmu;
	struct list_head		tbus;
	struct actlr_setting		*actlrs;
	u32				actlr_tbl_size;
	struct work_struct		outstanding_tnx_work;
	spinlock_t			atos_lock;
	void __iomem			*tcu_base;
};
#define to_qsmmuv500_archdata(smmu)				\
	container_of(smmu, struct qsmmuv500_archdata, smmu)

struct qsmmuv500_group_iommudata {
	bool has_actlr;
	u32 actlr;
};
#define to_qsmmuv500_group_iommudata(group)				\
	((struct qsmmuv500_group_iommudata *)				\
		(iommu_group_get_iommudata(group)))

struct qsmmuv500_tbu_device {
	struct list_head		list;
	struct device			*dev;
	struct arm_smmu_device		*smmu;
	void __iomem			*base;

	const struct qsmmuv500_tbu_impl	*impl;
	struct arm_smmu_power_resources *pwr;
	u32				sid_start;
	u32				num_sids;
	u32				iova_width;

	/* Protects halt count */
	spinlock_t			halt_lock;
	u32				halt_count;
	unsigned int			*irqs;
};

struct qsmmuv500_tbu_impl {
	int (*halt_req)(struct qsmmuv500_tbu_device *tbu);
	int (*halt_poll)(struct qsmmuv500_tbu_device *tbu);
	void (*resume)(struct qsmmuv500_tbu_device *tbu);
	phys_addr_t (*trigger_atos)(struct qsmmuv500_tbu_device *tbu, dma_addr_t iova, u32 sid,
				    unsigned long trans_flags);
	void (*write_sync)(struct qsmmuv500_tbu_device *tbu);
	void (*log_outstanding_transactions)(struct qsmmuv500_tbu_device *tbu);
};

struct arm_tbu_device {
	struct qsmmuv500_tbu_device tbu;
	bool has_micro_idle;
};

#define to_arm_tbu(tbu)			container_of(tbu, struct arm_tbu_device, tbu)

struct qtb500_device {
	struct qsmmuv500_tbu_device tbu;
	bool no_halt;
	u32 num_ports;
	void __iomem			*debugchain_base;
	void __iomem			*transactiontracker_base;
};

#define to_qtb500(tbu)		container_of(tbu, struct qtb500_device, tbu)

static int arm_tbu_halt_req(struct qsmmuv500_tbu_device *tbu)
{
	void __iomem *tbu_base = tbu->base;
	u32 halt;

	halt = readl_relaxed(tbu_base + DEBUG_SID_HALT_REG);
	halt |= DEBUG_SID_HALT_REQ;
	writel_relaxed(halt, tbu_base + DEBUG_SID_HALT_REG);

	return 0;
}

static int arm_tbu_halt_poll(struct qsmmuv500_tbu_device *tbu)
{
	void __iomem *tbu_base = tbu->base;
	u32 halt, status;

	if (readl_poll_timeout_atomic(tbu_base + DEBUG_SR_HALT_ACK_REG, status,
					(status & DEBUG_SR_HALT_ACK_VAL),
					0, TBU_DBG_TIMEOUT_US)) {
		dev_err(tbu->dev, "Couldn't halt TBU!\n");

		halt = readl_relaxed(tbu_base + DEBUG_SID_HALT_REG);
		halt &= ~DEBUG_SID_HALT_REQ;
		writel_relaxed(halt, tbu_base + DEBUG_SID_HALT_REG);

		return -ETIMEDOUT;
	}

	return 0;
}

static void arm_tbu_resume(struct qsmmuv500_tbu_device *tbu)
{
	void __iomem *base = tbu->base;
	u32 val;

	val = readl_relaxed(base + DEBUG_SID_HALT_REG);
	val &= ~DEBUG_SID_HALT_REQ;
	writel_relaxed(val, base + DEBUG_SID_HALT_REG);
}

static phys_addr_t arm_tbu_trigger_atos(struct qsmmuv500_tbu_device *tbu, dma_addr_t iova, u32 sid,
					unsigned long trans_flags)
{
	void __iomem *tbu_base = tbu->base;
	phys_addr_t phys = 0;
	u64 val;
	ktime_t timeout;
	bool ecats_timedout = false;

	/* Set address and stream-id */
	val = readq_relaxed(tbu_base + DEBUG_SID_HALT_REG);
	val &= ~DEBUG_SID_HALT_SID;
	val |= FIELD_PREP(DEBUG_SID_HALT_SID, sid);
	writeq_relaxed(val, tbu_base + DEBUG_SID_HALT_REG);
	writeq_relaxed(iova, tbu_base + DEBUG_VA_ADDR_REG);
	val = FIELD_PREP(DEBUG_AXUSER_CDMID, DEBUG_AXUSER_CDMID_VAL);
	writeq_relaxed(val, tbu_base + DEBUG_AXUSER_REG);

	/* Write-back Read and Write-Allocate */
	val = FIELD_PREP(DEBUG_TXN_AXCACHE, 0xF);

	/* Non-secure Access */
	val |= FIELD_PREP(DEBUG_TXN_AXPROT, DEBUG_TXN_AXPROT_NSEC);

	/* Write or Read Access */
	if (trans_flags & IOMMU_TRANS_WRITE)
		val |= DEBUG_TXN_WRITE;

	/* Priviledged or Unpriviledged Access */
	if (trans_flags & IOMMU_TRANS_PRIV)
		val |= FIELD_PREP(DEBUG_TXN_AXPROT, DEBUG_TXN_AXPROT_PRIV);

	/* Data or Instruction Access */
	if (trans_flags & IOMMU_TRANS_INST)
		val |= FIELD_PREP(DEBUG_TXN_AXPROT, DEBUG_TXN_AXPROT_INST);

	val |= DEBUG_TXN_TRIGGER;
	writeq_relaxed(val, tbu_base + DEBUG_TXN_TRIGG_REG);

	timeout = ktime_add_us(ktime_get(), TBU_DBG_TIMEOUT_US);
	for (;;) {
		val = readl_relaxed(tbu_base + DEBUG_SR_HALT_ACK_REG);
		if (!(val & DEBUG_SR_ECATS_RUNNING_VAL))
			break;
		val = readl_relaxed(tbu_base + DEBUG_PAR_REG);
		if (val & DEBUG_PAR_FAULT_VAL)
			break;
		if (ktime_compare(ktime_get(), timeout) > 0) {
			ecats_timedout = true;
			break;
		}
	}

	val = readq_relaxed(tbu_base + DEBUG_PAR_REG);
	if (val & DEBUG_PAR_FAULT_VAL)
		dev_err(tbu->dev, "ECATS generated a fault interrupt! PAR = %llx, SID=0x%x\n",
			val, sid);
	else if (ecats_timedout)
		dev_err_ratelimited(tbu->dev, "ECATS translation timed out!\n");
	else
		phys = FIELD_GET(DEBUG_PAR_PA, val);

	/* Reset hardware */
	writeq_relaxed(0, tbu_base + DEBUG_TXN_TRIGG_REG);
	writeq_relaxed(0, tbu_base + DEBUG_VA_ADDR_REG);
	val = readl_relaxed(tbu_base + DEBUG_SID_HALT_REG);
	val &= ~DEBUG_SID_HALT_SID;
	writel_relaxed(val, tbu_base + DEBUG_SID_HALT_REG);

	return phys;
}

static void arm_tbu_write_sync(struct qsmmuv500_tbu_device *tbu)
{
	readl_relaxed(tbu->base + DEBUG_SR_HALT_ACK_REG);
}

static void arm_tbu_log_outstanding_transactions(struct qsmmuv500_tbu_device *tbu)
{
	void __iomem *base = tbu->base;
	u64 outstanding_tnxs;
	u64 tcr_cntl_val, res;

	tcr_cntl_val = readq_relaxed(base + TNX_TCR_CNTL);

	/* Write 1 into MATCH_MASK_UPD of TNX_TCR_CNTL */
	writeq_relaxed(tcr_cntl_val | TNX_TCR_CNTL_MATCH_MASK_UPD,
		       base + TNX_TCR_CNTL);

	/*
	 * Simultaneously write 0 into MATCH_MASK_UPD, 0 into
	 * ALWAYS_CAPTURE, 0 into MATCH_MASK_VALID, and 1 into
	 * TBU_OT_CAPTURE_EN of TNX_TCR_CNTL
	 */
	tcr_cntl_val &= ~(TNX_TCR_CNTL_MATCH_MASK_UPD |
			  TNX_TCR_CNTL_ALWAYS_CAPTURE |
			  TNX_TCR_CNTL_MATCH_MASK_VALID);
	writeq_relaxed(tcr_cntl_val | TNX_TCR_CNTL_TBU_OT_CAPTURE_EN,
		       base + TNX_TCR_CNTL);

	/* Poll for CAPTURE1_VALID to become 1 on TNX_TCR_CNTL_2 */
	if (readq_poll_timeout_atomic(base + TNX_TCR_CNTL_2, res,
				      res & TNX_TCR_CNTL_2_CAP1_VALID,
				      0, TBU_DBG_TIMEOUT_US)) {
		dev_err_ratelimited(tbu->dev,
				    "Timeout on TNX snapshot poll\n");
		goto poll_timeout;
	}

	/* Read Register CAPTURE1_SNAPSHOT_1 */
	outstanding_tnxs = readq_relaxed(base + CAPTURE1_SNAPSHOT_1);
	dev_err_ratelimited(tbu->dev,
			    "Outstanding Transaction Bitmap: 0x%llx\n",
			    outstanding_tnxs);

poll_timeout:
	/* Write TBU_OT_CAPTURE_EN to 0 of TNX_TCR_CNTL */
	writeq_relaxed(tcr_cntl_val & ~TNX_TCR_CNTL_TBU_OT_CAPTURE_EN,
		       tbu->base + TNX_TCR_CNTL);
}

static const struct qsmmuv500_tbu_impl arm_tbu_impl = {
	.halt_req = arm_tbu_halt_req,
	.halt_poll = arm_tbu_halt_poll,
	.resume = arm_tbu_resume,
	.trigger_atos = arm_tbu_trigger_atos,
	.write_sync = arm_tbu_write_sync,
	.log_outstanding_transactions = arm_tbu_log_outstanding_transactions,
};

/*
 * Prior to accessing registers in the TBU local register space,
 * TBU must be woken from micro idle.
 */
static int __arm_tbu_micro_idle_cfg(struct arm_smmu_device *smmu,
					    u32 val, u32 mask)
{
	void __iomem *reg;
	u32 tmp, new;
	unsigned long flags;
	int ret;

	/* Protect APPS_SMMU_TBU_REG_ACCESS register. */
	spin_lock_irqsave(&smmu->global_sync_lock, flags);
	new = arm_smmu_readl(smmu, ARM_SMMU_IMPL_DEF5,
			APPS_SMMU_TBU_REG_ACCESS_REQ_NS);
	new &= ~mask;
	new |= val;
	arm_smmu_writel(smmu, ARM_SMMU_IMPL_DEF5,
			APPS_SMMU_TBU_REG_ACCESS_REQ_NS,
			new);

	reg = arm_smmu_page(smmu, ARM_SMMU_IMPL_DEF5);
	reg += APPS_SMMU_TBU_REG_ACCESS_ACK_NS;
	ret = readl_poll_timeout_atomic(reg, tmp, ((tmp & mask) == val), 0, 200);
	if (ret)
		dev_WARN(smmu->dev, "Timed out configuring micro idle! %x instead of %x\n", tmp,
			 new);
	/*
	 * While the micro-idle guard sequence registers may have been configured
	 * properly, it is possible that the intended effect has not been realized
	 * by the power management hardware due to delays in the system.
	 *
	 * Spin for a short amount of time to allow for the desired configuration to
	 * take effect before proceeding.
	 */
	udelay(TBU_MICRO_IDLE_DELAY_US);
	spin_unlock_irqrestore(&smmu->global_sync_lock, flags);
	return ret;
}

int arm_tbu_micro_idle_wake(struct arm_smmu_power_resources *pwr)
{
	struct qsmmuv500_tbu_device *tbu = dev_get_drvdata(pwr->dev);
	struct arm_tbu_device *arm_tbu = to_arm_tbu(tbu);
	u32 val;

	if (!arm_tbu->has_micro_idle)
		return 0;

	val = tbu->sid_start >> 10;
	val = 1 << val;
	return __arm_tbu_micro_idle_cfg(tbu->smmu, val, val);
}

void arm_tbu_micro_idle_allow(struct arm_smmu_power_resources *pwr)
{
	struct qsmmuv500_tbu_device *tbu = dev_get_drvdata(pwr->dev);
	struct arm_tbu_device *arm_tbu = to_arm_tbu(tbu);
	u32 val;

	if (!arm_tbu->has_micro_idle)
		return;

	val = tbu->sid_start >> 10;
	val = 1 << val;
	__arm_tbu_micro_idle_cfg(tbu->smmu, 0, val);
}

static struct qsmmuv500_tbu_device *arm_tbu_impl_init(struct qsmmuv500_tbu_device *tbu)
{
	struct arm_tbu_device *arm_tbu;
	struct device *dev = tbu->dev;

	arm_tbu = devm_krealloc(dev, tbu, sizeof(*arm_tbu), GFP_KERNEL);
	if (!arm_tbu)
		return ERR_PTR(-ENOMEM);

	arm_tbu->tbu.impl = &arm_tbu_impl;
	arm_tbu->has_micro_idle = of_property_read_bool(dev->of_node, "qcom,micro-idle");

	if (arm_tbu->has_micro_idle) {
		arm_tbu->tbu.pwr->resume = arm_tbu_micro_idle_wake;
		arm_tbu->tbu.pwr->suspend = arm_tbu_micro_idle_allow;
	}

	return &arm_tbu->tbu;
}

static int qtb500_tbu_halt_req(struct qsmmuv500_tbu_device *tbu)
{
	void __iomem *qtb_base = tbu->base;
	struct qtb500_device *qtb = to_qtb500(tbu);
	u64 val;

	if (qtb->no_halt)
		return 0;

	val = readq_relaxed(qtb_base + QTB_OVR_DBG_FENCEREQ);
	val |= QTB_OVR_DBG_FENCEREQ_HALT;
	writeq_relaxed(val, qtb_base  + QTB_OVR_DBG_FENCEREQ);

	return 0;
}

static int qtb500_tbu_halt_poll(struct qsmmuv500_tbu_device *tbu)
{
	void __iomem *qtb_base = tbu->base;
	struct qtb500_device *qtb = to_qtb500(tbu);
	u64 val, status;

	if (qtb->no_halt)
		return 0;

	if (readq_poll_timeout_atomic(qtb_base + QTB_OVR_DBG_FENCEACK, status,
				      (status &  QTB_OVR_DBG_FENCEACK_ACK), 0,
				      QTB_DBG_TIMEOUT_US)) {
		dev_err(tbu->dev, "Couldn't halt QTB\n");

		val = readq_relaxed(qtb_base + QTB_OVR_DBG_FENCEREQ);
		val &= ~QTB_OVR_DBG_FENCEREQ_HALT;
		writeq_relaxed(val, qtb_base + QTB_OVR_DBG_FENCEREQ);

		return -ETIMEDOUT;
	}

	return 0;
}

static void qtb500_tbu_resume(struct qsmmuv500_tbu_device *tbu)
{
	void __iomem *qtb_base = tbu->base;
	struct qtb500_device *qtb = to_qtb500(tbu);
	u64 val;

	if (qtb->no_halt)
		return;

	val = readq_relaxed(qtb_base + QTB_OVR_DBG_FENCEREQ);
	val &= ~QTB_OVR_DBG_FENCEREQ_HALT;
	writeq_relaxed(val, qtb_base  + QTB_OVR_DBG_FENCEREQ);
}

static phys_addr_t qtb500_trigger_atos(struct qsmmuv500_tbu_device *tbu, dma_addr_t iova,
				       u32 sid, unsigned long trans_flags)
{
	void __iomem *qtb_base = tbu->base;
	u64 infld0, infld1, infld2, val;
	phys_addr_t phys = 0;
	ktime_t timeout;
	bool ecats_timedout = false;

	/*
	 * Recommended to set:
	 *
	 * QTB_OVR_ECATS_INFLD0.QAD == 0 (AP Access Domain)
	 * QTB_OVR_EACTS_INFLD0.PCIE_NO_SNOOP == 0 (IO-Coherency enabled)
	 */
	infld0 = FIELD_PREP(QTB_OVR_ECATS_INFLD0_SID, sid);
	if (trans_flags & IOMMU_TRANS_SEC)
		infld0 |= QTB_OVR_ECATS_INFLD0_SEC_SID;

	infld1 = 0;
	if (trans_flags & IOMMU_TRANS_PRIV)
		infld1 |= QTB_OVR_ECATS_INFLD1_PNU;
	if (trans_flags & IOMMU_TRANS_INST)
		infld1 |= QTB_OVR_ECATS_INFLD1_IND;
	/*
	 * Recommended to set:
	 *
	 * QTB_OVR_ECATS_INFLD1.DIRTY == 0,
	 * QTB_OVR_ECATS_INFLD1.TR_TYPE == 4 (Cacheable and Shareable memory)
	 * QTB_OVR_ECATS_INFLD1.ALLOC == 0 (No allocation in TLB/caches)
	 */
	infld1 |= FIELD_PREP(QTB_OVR_ECATS_INFLD1_TR_TYPE, QTB_OVR_ECATS_INFLD1_TR_TYPE_SHARED);
	if (!(trans_flags & IOMMU_TRANS_SEC))
		infld1 |= QTB_OVR_ECATS_INFLD1_NON_SEC;
	if (trans_flags & IOMMU_TRANS_WRITE)
		infld1 |= FIELD_PREP(QTB_OVR_ECATS_INFLD1_OPC, QTB_OVR_ECATS_INFLD1_OPC_WRI);

	infld2 = iova;

	writeq_relaxed(infld0, qtb_base + QTB_OVR_ECATS_INFLD0);
	writeq_relaxed(infld1, qtb_base + QTB_OVR_ECATS_INFLD1);
	writeq_relaxed(infld2, qtb_base + QTB_OVR_ECATS_INFLD2);
	writeq_relaxed(QTB_OVR_ECATS_TRIGGER_START, qtb_base + QTB_OVR_ECATS_TRIGGER);

	timeout = ktime_add_us(ktime_get(), QTB_DBG_TIMEOUT_US);
	for (;;) {
		val = readq_relaxed(qtb_base + QTB_OVR_ECATS_STATUS);
		if (val & QTB_OVR_ECATS_STATUS_DONE)
			break;
		val = readq_relaxed(qtb_base + QTB_OVR_ECATS_OUTFLD0);
		if (val & QTB_OVR_ECATS_OUTFLD0_FAULT)
			break;
		if (ktime_compare(ktime_get(), timeout) > 0) {
			ecats_timedout = true;
			break;
		}
	}

	val = readq_relaxed(qtb_base + QTB_OVR_ECATS_OUTFLD0);
	if (val & QTB_OVR_ECATS_OUTFLD0_FAULT)
		dev_err(tbu->dev, "ECATS generated a fault interrupt! OUTFLD0 = 0x%llx SID = 0x%x\n",
			val, sid);
	else if (ecats_timedout)
		dev_err_ratelimited(tbu->dev, "ECATS translation timed out!\n");
	else
		phys = FIELD_GET(QTB_OVR_ECATS_OUTFLD0_PA, val);

	/* Reset hardware for next transaction. */
	writeq_relaxed(0, qtb_base + QTB_OVR_ECATS_TRIGGER);

	return phys;
}

static void qtb500_tbu_write_sync(struct qsmmuv500_tbu_device *tbu)
{
	readl_relaxed(tbu->base + QTB_SWID_LOW);
}

static void qtb500_log_outstanding_transactions(struct qsmmuv500_tbu_device *tbu)
{
	void __iomem *qtb_base = tbu->base;
	struct qtb500_device *qtb = to_qtb500(tbu);
	u64 outstanding_tnx;
	int i;

	for (i = 0; i < qtb->num_ports; i++) {
		outstanding_tnx = readq_relaxed(qtb_base + QTB_NS_DBG_PORT_N_OT_SNAPSHOT(i));
		dev_err(tbu->dev, "port %d outstanding transactions bitmap: 0x%llx\n", i,
			outstanding_tnx);
	}
}

static const struct qsmmuv500_tbu_impl qtb500_impl = {
	.halt_req = qtb500_tbu_halt_req,
	.halt_poll = qtb500_tbu_halt_poll,
	.resume = qtb500_tbu_resume,
	.trigger_atos = qtb500_trigger_atos,
	.write_sync = qtb500_tbu_write_sync,
	.log_outstanding_transactions = qtb500_log_outstanding_transactions,
};

static struct qsmmuv500_tbu_device *qtb500_impl_init(struct qsmmuv500_tbu_device *tbu)
{
	struct resource *ttres;
	struct qtb500_device *qtb;
	struct device *dev = tbu->dev;
	struct platform_device *pdev = to_platform_device(dev);
#ifdef CONFIG_ARM_SMMU_TESTBUS
	struct resource *res;
#endif
	int ret;

	qtb = devm_krealloc(dev, tbu, sizeof(*qtb), GFP_KERNEL);
	if (!qtb)
		return ERR_PTR(-ENOMEM);

	qtb->tbu.impl = &qtb500_impl;

	ret = of_property_read_u32(dev->of_node, "qcom,num-qtb-ports", &qtb->num_ports);
	if (ret)
		return ERR_PTR(ret);

	qtb->no_halt = of_property_read_bool(dev->of_node, "qcom,no-qtb-atos-halt");

#ifdef CONFIG_ARM_SMMU_TESTBUS
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "debugchain-base");
	if (!res) {
		dev_info(dev, "Unable to get the debugchain-base\n");
		return ERR_PTR(-EINVAL);
		goto end;
	}

	qtb->debugchain_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(qtb->debugchain_base)) {
		dev_info(dev, "devm_ioremap failure, overlapping regs\n");

		/*
		 * use ioremap for qtb's sharing same debug chain register space
		 * for eg : sf and hf qtb's on mmnoc.
		 */
		qtb->debugchain_base = ioremap(res->start, resource_size(res));
		if (IS_ERR(qtb->debugchain_base)) {
			dev_err(dev, "unable to ioremap the debugchain-base\n");
			return ERR_PTR(-EINVAL);
		}
	}

end:
#endif
	ttres = platform_get_resource_byname(pdev, IORESOURCE_MEM, "transactiontracker-base");
	if (ttres) {
		qtb->transactiontracker_base = devm_ioremap_resource(dev, ttres);
		if (IS_ERR(qtb->transactiontracker_base))
			dev_info(dev, "devm_ioremap failure for transaction tracker\n");
	} else {
		qtb->transactiontracker_base = NULL;
		dev_info(dev, "Unable to get the transactiontracker-base\n");
	}

	return &qtb->tbu;
}

static struct qsmmuv500_tbu_device *qsmmuv500_find_tbu(struct arm_smmu_device *smmu, u32 sid)
{
	struct qsmmuv500_tbu_device *tbu = NULL;
	struct qsmmuv500_archdata *data = to_qsmmuv500_archdata(smmu);

	if (!list_empty(&data->tbus)) {
		list_for_each_entry(tbu, &data->tbus, list) {
			if (tbu->sid_start <= sid &&
			sid < tbu->sid_start + tbu->num_sids)
				return tbu;
		}
	}

	return NULL;
}

static int qsmmuv500_tbu_halt(struct qsmmuv500_tbu_device *tbu,
			      struct arm_smmu_domain *smmu_domain)
{
	unsigned long flags;
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	int ret = 0, idx = smmu_domain->cfg.cbndx;
	u32 fsr;

	if (of_property_read_bool(tbu->dev->of_node, "qcom,opt-out-tbu-halting")) {
		dev_notice(tbu->dev, "TBU opted-out for halting!\n");
		return -EBUSY;
	}

	spin_lock_irqsave(&tbu->halt_lock, flags);
	if (tbu->halt_count) {
		tbu->halt_count++;
		goto out;
	}

	ret = tbu->impl->halt_req(tbu);
	if (ret)
		goto out;

	fsr = arm_smmu_cb_read(smmu, idx, ARM_SMMU_CB_FSR);
	if ((fsr & ARM_SMMU_FSR_FAULT) && (fsr & ARM_SMMU_FSR_SS)) {
		u32 sctlr_orig, sctlr;
		/*
		 * We are in a fault; Our request to halt the bus will not
		 * complete until transactions in front of us (such as the fault
		 * itself) have completed. Disable iommu faults and terminate
		 * any existing transactions.
		 */
		sctlr_orig = arm_smmu_cb_read(smmu, idx, ARM_SMMU_CB_SCTLR);
		sctlr = sctlr_orig & ~(ARM_SMMU_SCTLR_CFCFG | ARM_SMMU_SCTLR_CFIE);
		arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_SCTLR, sctlr);

		arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_FSR, fsr);
		/*
		 * Barrier required to ensure that the FSR is cleared
		 * before resuming SMMU operation
		 */
		wmb();
		arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_RESUME,
				  ARM_SMMU_RESUME_TERMINATE);

		arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_SCTLR, sctlr_orig);
	}

	ret = tbu->impl->halt_poll(tbu);
	if (ret)
		goto out;

	tbu->halt_count = 1;
out:
	spin_unlock_irqrestore(&tbu->halt_lock, flags);
	return ret;
}

static void qsmmuv500_tbu_resume(struct qsmmuv500_tbu_device *tbu)
{
	unsigned long flags;

	spin_lock_irqsave(&tbu->halt_lock, flags);
	if (WARN(!tbu->halt_count, "%s bad tbu->halt_count", dev_name(tbu->dev))) {
		goto out;

	} else if (tbu->halt_count > 1) {
		tbu->halt_count--;
		goto out;
	}

	tbu->impl->resume(tbu);

	tbu->halt_count = 0;
out:
	spin_unlock_irqrestore(&tbu->halt_lock, flags);
}

/*
 * Provides mutually exclusive access to the registers used by the
 * outstanding transaction snapshot feature and the transaction
 * snapshot capture feature.
 */
static DEFINE_MUTEX(capture_reg_lock);
static DEFINE_SPINLOCK(testbus_lock);

__maybe_unused static struct dentry *get_iommu_debug_dir(void)
{
	struct dentry *iommu_debug_dir;
	int ret;

	iommu_debug_dir = debugfs_lookup("iommu-debug", NULL);
	if (IS_ERR_OR_NULL(iommu_debug_dir)) {
		iommu_debug_dir = debugfs_create_dir("iommu-debug", NULL);
		if (IS_ERR_OR_NULL(iommu_debug_dir)) {
			ret = PTR_ERR(iommu_debug_dir);
			pr_err_ratelimited("Unable to create iommu-debug directory, ret=%d\n",
					ret);
			return NULL;
		}
	}
	return iommu_debug_dir;
}

#ifdef CONFIG_ARM_SMMU_TESTBUS_DEBUGFS
static struct dentry *debugfs_testbus_dir;

static ssize_t arm_smmu_debug_debugchain_read(struct file *file,
		char __user *ubuf, size_t count, loff_t *offset)

{
	char *buf;
	ssize_t retval;
	size_t buflen;
	int buf_len;
	struct qsmmuv500_tbu_device *tbu = file->private_data;
	struct qtb500_device *qtb = to_qtb500(tbu);
	void __iomem *debugchain_base = qtb->debugchain_base;
	long chain_length = 0;
	u64 val;

	if (*offset)
		return 0;

	arm_smmu_power_on(tbu->pwr);
	chain_length = arm_smmu_debug_qtb_debugchain_load(debugchain_base);
	buf_len = chain_length * sizeof(u64);
	buf = kzalloc(buf_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	arm_smmu_debug_qtb_debugchain_dump(debugchain_base);
	do {
		val = arm_smmu_debug_qtb_debugchain_dump(debugchain_base);
		scnprintf(buf + strlen(buf), buf_len - strlen(buf), "0x%0x\n", val);
	} while (chain_length--);
	arm_smmu_power_off(tbu->smmu, tbu->pwr);

	buflen = min(count, strlen(buf));
	if (copy_to_user(ubuf, buf, buflen)) {
		pr_err_ratelimited("Couldn't copy_to_user\n");
		retval = -EFAULT;
	} else {
		*offset = 1;
		retval = buflen;
	}
	return retval;
}

static ssize_t arm_smmu_debug_testbus_read(struct file *file,
		char __user *ubuf, size_t count, loff_t *offset,
		enum testbus_sel tbu, enum testbus_ops ops)

{
	char buf[100];
	ssize_t retval;
	size_t buflen;
	int buf_len = sizeof(buf);

	if (*offset)
		return 0;

	memset(buf, 0, buf_len);

	if (tbu == SEL_TBU) {
		struct qsmmuv500_tbu_device *tbu = file->private_data;
		void __iomem *tbu_base = tbu->base;
		long val;

		arm_smmu_power_on(tbu->pwr);
		if (ops == TESTBUS_SELECT)
			val = arm_smmu_debug_tbu_testbus_select(tbu_base,
							READ, 0);
		else
			val = arm_smmu_debug_tbu_testbus_output(tbu_base);
		arm_smmu_power_off(tbu->smmu, tbu->pwr);

		scnprintf(buf, buf_len, "0x%0x\n", val);
	} else {

		struct arm_smmu_device *smmu = file->private_data;
		struct qsmmuv500_archdata *data = to_qsmmuv500_archdata(smmu);
		phys_addr_t phys_addr = smmu->phys_addr;
		void __iomem *tcu_base = data->tcu_base;

		arm_smmu_power_on(smmu->pwr);

		if (ops == TESTBUS_SELECT) {
			scnprintf(buf, buf_len, "TCU clk testbus sel: 0x%0x\n",
				  arm_smmu_debug_tcu_testbus_select(phys_addr,
					tcu_base, CLK_TESTBUS, READ, 0));
			scnprintf(buf + strlen(buf), buf_len - strlen(buf),
				  "TCU testbus sel : 0x%0x\n",
				  arm_smmu_debug_tcu_testbus_select(phys_addr,
					 tcu_base, PTW_AND_CACHE_TESTBUS,
					 READ, 0));
		} else {
			scnprintf(buf, buf_len, "0x%0x\n",
				  arm_smmu_debug_tcu_testbus_output(phys_addr));
		}

		arm_smmu_power_off(smmu, smmu->pwr);
	}
	buflen = min(count, strlen(buf));
	if (copy_to_user(ubuf, buf, buflen)) {
		pr_err_ratelimited("Couldn't copy_to_user\n");
		retval = -EFAULT;
	} else {
		*offset = 1;
		retval = buflen;
	}

	return retval;
}

static ssize_t arm_smmu_debug_tcu_testbus_sel_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *offset)
{
	struct arm_smmu_device *smmu = file->private_data;
	struct qsmmuv500_archdata *data = to_qsmmuv500_archdata(smmu);
	void __iomem *tcu_base = data->tcu_base;
	phys_addr_t phys_addr = smmu->phys_addr;
	char *comma;
	char buf[100];
	u64 sel, val;

	if (count >= 100) {
		pr_err_ratelimited("Value too large\n");
		return -EINVAL;
	}

	memset(buf, 0, 100);

	if (copy_from_user(buf, ubuf, count)) {
		pr_err_ratelimited("Couldn't copy from user\n");
		return -EFAULT;
	}

	comma = strnchr(buf, count, ',');
	if (!comma)
		goto invalid_format;

	/* split up the words */
	*comma = '\0';

	if (kstrtou64(buf, 0, &sel))
		goto invalid_format;

	if (sel != 1 && sel != 2)
		goto invalid_format;

	if (kstrtou64(comma + 1, 0, &val))
		goto invalid_format;

	arm_smmu_power_on(smmu->pwr);

	if (sel == 1)
		arm_smmu_debug_tcu_testbus_select(phys_addr,
				tcu_base, CLK_TESTBUS, WRITE, val);
	else if (sel == 2)
		arm_smmu_debug_tcu_testbus_select(phys_addr,
				tcu_base, PTW_AND_CACHE_TESTBUS, WRITE, val);

	arm_smmu_power_off(smmu, smmu->pwr);

	return count;

invalid_format:
	pr_err_ratelimited("Invalid format. Expected: <1, testbus select> for tcu CLK testbus (or) <2, testbus select> for tcu PTW/CACHE testbuses\n");
	return -EINVAL;
}

static ssize_t arm_smmu_debug_tcu_testbus_sel_read(struct file *file,
		char __user *ubuf, size_t count, loff_t *offset)
{
	return arm_smmu_debug_testbus_read(file, ubuf,
			count, offset, SEL_TCU, TESTBUS_SELECT);
}

static const struct file_operations arm_smmu_debug_tcu_testbus_sel_fops = {
	.open	= simple_open,
	.write	= arm_smmu_debug_tcu_testbus_sel_write,
	.read	= arm_smmu_debug_tcu_testbus_sel_read,
};

static ssize_t arm_smmu_debug_tcu_testbus_read(struct file *file,
		char __user *ubuf, size_t count, loff_t *offset)
{
	return arm_smmu_debug_testbus_read(file, ubuf,
			count, offset, SEL_TCU, TESTBUS_OUTPUT);
}

static const struct file_operations arm_smmu_debug_tcu_testbus_fops = {
	.open	= simple_open,
	.read	= arm_smmu_debug_tcu_testbus_read,
};

static int qsmmuv500_tcu_testbus_init(struct arm_smmu_device *smmu)
{
	struct dentry *testbus_dir, *iommu_debug_dir;

	iommu_debug_dir = get_iommu_debug_dir();
	if (!iommu_debug_dir)
		return 0;

	if (!debugfs_testbus_dir) {
		debugfs_testbus_dir = debugfs_create_dir("testbus",
						       iommu_debug_dir);
		if (IS_ERR(debugfs_testbus_dir)) {
			pr_err_ratelimited("Couldn't create iommu/testbus debugfs directory\n");
			return -ENODEV;
		}
	}

	testbus_dir = debugfs_create_dir(dev_name(smmu->dev),
				debugfs_testbus_dir);

	if (IS_ERR(testbus_dir)) {
		pr_err_ratelimited("Couldn't create iommu/testbus/%s debugfs directory\n",
		       dev_name(smmu->dev));
		goto err;
	}

	if (IS_ERR(debugfs_create_file("tcu_testbus_sel", 0400,
					testbus_dir, smmu,
					&arm_smmu_debug_tcu_testbus_sel_fops))) {
		pr_err_ratelimited("Couldn't create iommu/testbus/%s/tcu_testbus_sel debugfs file\n",
		       dev_name(smmu->dev));
		goto err_rmdir;
	}

	if (IS_ERR(debugfs_create_file("tcu_testbus_output", 0400,
					testbus_dir, smmu,
					&arm_smmu_debug_tcu_testbus_fops))) {
		pr_err_ratelimited("Couldn't create iommu/testbus/%s/tcu_testbus_output debugfs file\n",
				   dev_name(smmu->dev));
		goto err_rmdir;
	}

	return 0;
err_rmdir:
	debugfs_remove_recursive(testbus_dir);
err:
	return 0;
}

static ssize_t arm_smmu_debug_tbu_testbus_sel_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *offset)
{
	struct qsmmuv500_tbu_device *tbu = file->private_data;
	void __iomem *tbu_base = tbu->base;
	u64 val;

	if (kstrtoull_from_user(ubuf, count, 0, &val)) {
		pr_err_ratelimited("Invalid format for tbu testbus select\n");
		return -EINVAL;
	}

	if (of_device_is_compatible(tbu->dev->of_node, "qcom,qtb500"))
		return 0;

	arm_smmu_power_on(tbu->pwr);
	arm_smmu_debug_tbu_testbus_select(tbu_base, WRITE, val);
	arm_smmu_power_off(tbu->smmu, tbu->pwr);

	return count;
}

static ssize_t arm_smmu_debug_tbu_testbus_sel_read(struct file *file,
		char __user *ubuf, size_t count, loff_t *offset)
{
	return arm_smmu_debug_testbus_read(file, ubuf,
			count, offset, SEL_TBU, TESTBUS_SELECT);
}

static const struct file_operations arm_smmu_debug_tbu_testbus_sel_fops = {
	.open	= simple_open,
	.write	= arm_smmu_debug_tbu_testbus_sel_write,
	.read	= arm_smmu_debug_tbu_testbus_sel_read,
};

static ssize_t arm_smmu_debug_tbu_testbus_read(struct file *file,
		char __user *ubuf, size_t count, loff_t *offset)
{
	struct qsmmuv500_tbu_device *tbu = file->private_data;

	if (of_device_is_compatible(tbu->dev->of_node, "qcom,qtb500"))
		return arm_smmu_debug_debugchain_read(file, ubuf,
				count, offset);
	else
		return arm_smmu_debug_testbus_read(file, ubuf,
			count, offset, SEL_TBU, TESTBUS_OUTPUT);
}

static const struct file_operations arm_smmu_debug_tbu_testbus_fops = {
	.open	= simple_open,
	.read	= arm_smmu_debug_tbu_testbus_read,
};

static int qsmmuv500_tbu_testbus_init(struct qsmmuv500_tbu_device *tbu)
{
	struct dentry *testbus_dir, *iommu_debug_dir;

	iommu_debug_dir = get_iommu_debug_dir();
	if (!iommu_debug_dir)
		return 0;

	if (!debugfs_testbus_dir) {
		debugfs_testbus_dir = debugfs_create_dir("testbus",
						       iommu_debug_dir);
		if (IS_ERR(debugfs_testbus_dir)) {
			pr_err_ratelimited("Couldn't create iommu/testbus debugfs directory\n");
			return -ENODEV;
		}
	}

	testbus_dir = debugfs_create_dir(dev_name(tbu->dev),
				debugfs_testbus_dir);

	if (IS_ERR(testbus_dir)) {
		pr_err_ratelimited("Couldn't create iommu/testbus/%s debugfs directory\n",
		       dev_name(tbu->dev));
		goto err;
	}

	if (IS_ERR(debugfs_create_file("tbu_testbus_sel", 0400,
					testbus_dir, tbu,
					&arm_smmu_debug_tbu_testbus_sel_fops)))	{
		pr_err_ratelimited("Couldn't create iommu/testbus/%s/tbu_testbus_sel debugfs file\n",
		       dev_name(tbu->dev));
		goto err_rmdir;
	}

	if (IS_ERR(debugfs_create_file("tbu_testbus_output", 0400,
					testbus_dir, tbu,
					&arm_smmu_debug_tbu_testbus_fops))) {
		pr_err_ratelimited("Couldn't create iommu/testbus/%s/tbu_testbus_output debugfs file\n",
		       dev_name(tbu->dev));
		goto err_rmdir;
	}

	return 0;
err_rmdir:
	debugfs_remove_recursive(testbus_dir);
err:
	return 0;
}
#else
static int qsmmuv500_tcu_testbus_init(struct arm_smmu_device *smmu)
{
	return 0;
}

static int qsmmuv500_tbu_testbus_init(struct qsmmuv500_tbu_device *tbu)
{
	return 0;
}
#endif

static void arm_smmu_testbus_dump(struct arm_smmu_device *smmu, u16 sid)
{
	if (smmu->model == QCOM_SMMUV500 &&
	    IS_ENABLED(CONFIG_ARM_SMMU_TESTBUS_DUMP)) {
		struct qsmmuv500_archdata *data = to_qsmmuv500_archdata(smmu);
		struct qsmmuv500_tbu_device *tbu;

		tbu = qsmmuv500_find_tbu(smmu, sid);
		spin_lock(&testbus_lock);
		if (tbu) {
			if (of_device_is_compatible(tbu->dev->of_node, "qcom,qtb500")) {
				struct qtb500_device *qtb = to_qtb500(tbu);

				qtb500_tbu_halt_req(tbu);
				if (!qtb500_tbu_halt_poll(tbu)) {
					arm_smmu_debug_dump_debugchain(tbu->dev,
								qtb->debugchain_base);
					qtb500_tbu_resume(tbu);
				}
				arm_smmu_debug_dump_qtb_regs(tbu->dev, tbu->base);
			} else {
				arm_smmu_debug_dump_tbu_testbus(tbu->dev,
							tbu->base,
							TBU_TESTBUS_SEL_ALL);
			}
		} else {
			arm_smmu_debug_dump_tcu_testbus(smmu->dev,
							smmu->phys_addr,
							data->tcu_base,
							TCU_TESTBUS_SEL_ALL);
		}
		spin_unlock(&testbus_lock);
	}
}

static void qsmmuv500_log_outstanding_transactions(struct work_struct *work)
{
	struct qsmmuv500_tbu_device *tbu = NULL;
	struct qsmmuv500_archdata *data = container_of(work,
						struct qsmmuv500_archdata,
						outstanding_tnx_work);
	struct arm_smmu_device *smmu = &data->smmu;

	if (!mutex_trylock(&capture_reg_lock)) {
		dev_warn_ratelimited(smmu->dev,
			"Tnx snapshot regs in use, not dumping OT tnxs.\n");
		goto bug;
	}

	if (arm_smmu_power_on(smmu->pwr)) {
		dev_err_ratelimited(smmu->dev,
				    "%s: Failed to power on SMMU.\n",
				    __func__);
		goto unlock;
	}

	if (!list_empty(&data->tbus)) {
		list_for_each_entry(tbu, &data->tbus, list) {
			if (arm_smmu_power_on(tbu->pwr)) {
				dev_err_ratelimited(tbu->dev,
				"%s: Failed to power on TBU.\n", __func__);
				continue;
			}

			tbu->impl->log_outstanding_transactions(tbu);

			arm_smmu_power_off(smmu, tbu->pwr);
		}
	}

	arm_smmu_power_off(smmu, smmu->pwr);
unlock:
	mutex_unlock(&capture_reg_lock);
bug:
	BUG_ON(IS_ENABLED(CONFIG_IOMMU_TLBSYNC_DEBUG));
}

static struct qsmmuv500_tbu_device *qsmmuv500_tbu_impl_init(struct qsmmuv500_tbu_device *tbu)
{
	if (of_device_is_compatible(tbu->dev->of_node, "qcom,qtb500"))
		return qtb500_impl_init(tbu);

	return arm_tbu_impl_init(tbu);
}

static int qsmmuv500_tbu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct qsmmuv500_tbu_device *tbu;
	struct resource *res;
	const __be32 *cell;
	int ret, len;

	tbu = devm_kzalloc(dev, sizeof(*tbu), GFP_KERNEL);
	if (!tbu)
		return -ENOMEM;

	tbu->dev = dev;

	/*
	 * ARM TBUs need to have power resources initialized before its
	 * implementation defined initialization occurs to setup the
	 * suspend and resure power callbacks.
	 */
	tbu->pwr = arm_smmu_init_power_resources(dev);
	if (IS_ERR(tbu->pwr))
		return PTR_ERR(tbu->pwr);

	tbu = qsmmuv500_tbu_impl_init(tbu);
	if (IS_ERR(tbu))
		return PTR_ERR(tbu);

	INIT_LIST_HEAD(&tbu->list);

	spin_lock_init(&tbu->halt_lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	tbu->base = devm_ioremap(dev, res->start, resource_size(res));
	if (!tbu->base)
		return -ENOMEM;

	cell = of_get_property(dev->of_node, "qcom,stream-id-range", &len);
	if (!cell || len < 8)
		return -EINVAL;

	tbu->sid_start = of_read_number(cell, 1);
	tbu->num_sids = of_read_number(cell + 1, 1);

	ret = of_property_read_u32(dev->of_node, "qcom,iova-width", &tbu->iova_width);
	if (ret < 0)
		return ret;

	dev_set_drvdata(dev, tbu);
	return 0;
}

static const struct of_device_id qsmmuv500_tbu_of_match[] = {
	{.compatible = "qcom,qsmmuv500-tbu"},
	{}
};

struct platform_driver qsmmuv500_tbu_driver = {
	.driver	= {
		.name		= "qsmmuv500-tbu",
		.of_match_table	= of_match_ptr(qsmmuv500_tbu_of_match),
	},
	.probe	= qsmmuv500_tbu_probe,
};

static ssize_t arm_smmu_debug_capturebus_snapshot_read(struct file *file,
		char __user *ubuf, size_t count, loff_t *offset)
{
	struct qsmmuv500_tbu_device *tbu = file->private_data;
	struct arm_smmu_device *smmu = tbu->smmu;
	void __iomem *tbu_base = tbu->base;
	u64 snapshot[NO_OF_CAPTURE_POINTS][REGS_PER_CAPTURE_POINT];
	u64 gfxttlogs[TTQTB_Capture_Points][2*TTQTB_Regs_Per_Capture_Points];
	u64 ttlogs[TTQTB_Capture_Points][4*TTQTB_Regs_Per_Capture_Points];
	u64 ttlogs_time[2*TTQTB_Capture_Points];
	struct qtb500_device *qtb = to_qtb500(tbu);
	void __iomem *transactiontracker_base = qtb->transactiontracker_base;
	char buf[8192];
	ssize_t retval;
	size_t buflen;
	int buf_len = sizeof(buf);
	int qtb_type;
	int i, j, x, y;

	if (*offset)
		return 0;

	memset(buf, 0, buf_len);

	if (arm_smmu_power_on(smmu->pwr))
		return -EINVAL;

	if (arm_smmu_power_on(tbu->pwr)) {
		arm_smmu_power_off(smmu, smmu->pwr);
		return -EINVAL;
	}

	if (!mutex_trylock(&capture_reg_lock)) {
		dev_warn_ratelimited(smmu->dev,
			"capture bus regs in use, not dumping it.\n");
		return -EBUSY;
	}

	if (of_device_is_compatible(tbu->dev->of_node, "qcom,qtb500")) {
		if (of_device_is_compatible(smmu->dev->of_node, "qcom,adreno-smmu"))
			qtb_type = 1;
		else
			qtb_type = 2;

		arm_smmu_debug_qtb_transtrac_collect(transactiontracker_base, gfxttlogs, ttlogs,
				ttlogs_time, qtb_type);

		arm_smmu_debug_qtb_transtrac_reset(transactiontracker_base);

		mutex_unlock(&capture_reg_lock);
		arm_smmu_power_off(tbu->smmu, tbu->pwr);
		arm_smmu_power_off(smmu, smmu->pwr);

		for (i = 0, x = 0; i < TTQTB_Capture_Points &&
				x < 2*TTQTB_Capture_Points; i++, x += 2) {
			scnprintf(buf + strlen(buf), buf_len - strlen(buf),
				"Latency_%d : 0x%lx\n",
				i, ttlogs_time[x]);
			scnprintf(buf + strlen(buf), buf_len - strlen(buf),
				"Timestamp_%d : 0x%lx\n",
				i, ttlogs_time[x+1]);
			if (qtb_type == 1) {
				for (j = 0, y = 0; j < TTQTB_Regs_Per_Capture_Points &&
						y < 2*TTQTB_Regs_Per_Capture_Points; j++, y += 2) {
					scnprintf(buf + strlen(buf), buf_len - strlen(buf),
						"LogIn_%d_%d : 0x%lx\n",
						i, j, gfxttlogs[i][y]);
					scnprintf(buf + strlen(buf), buf_len - strlen(buf),
						"LogOut_%d_%d : 0x%lx\n",
						i, j, gfxttlogs[i][y+1]);
				}
			} else if (qtb_type == 2) {
				for (j = 0, y = 0; j < TTQTB_Regs_Per_Capture_Points &&
						y < 4*TTQTB_Regs_Per_Capture_Points; j++, y += 4) {
					scnprintf(buf + strlen(buf), buf_len - strlen(buf),
						"LogIn_%d_%d_Low : 0x%lx\n",
						i, j, ttlogs[i][y]);
					scnprintf(buf + strlen(buf), buf_len - strlen(buf),
						"LogIn_%d_%d_High : 0x%lx\n",
						i, j, ttlogs[i][y+1]);
					scnprintf(buf + strlen(buf), buf_len - strlen(buf),
						"LogOut_%d_%d_Low : 0x%lx\n",
						i, j, ttlogs[i][y+2]);
					scnprintf(buf + strlen(buf), buf_len - strlen(buf),
						"LogOut_%d_%d_High : 0x%lx\n",
						i, j, ttlogs[i][y+3]);
				}

			}

		}

		buflen = min(count, strlen(buf));
		if (copy_to_user(ubuf, buf, buflen)) {
			dev_err_ratelimited(smmu->dev, "Couldn't copy_to_user\n");
			retval = -EFAULT;
		} else {
			*offset = 1;
			retval = buflen;
		}

		return retval;
	}

	else {
		arm_smmu_debug_get_capture_snapshot(tbu_base, snapshot);

		mutex_unlock(&capture_reg_lock);
		arm_smmu_power_off(tbu->smmu, tbu->pwr);
		arm_smmu_power_off(smmu, smmu->pwr);

		for (i = 0; i < NO_OF_CAPTURE_POINTS ; ++i) {
			for (j = 0; j < REGS_PER_CAPTURE_POINT; ++j) {
				scnprintf(buf + strlen(buf), buf_len - strlen(buf),
					 "Capture_%d_Snapshot_%d : 0x%0llx\n",
					  i+1, j+1, snapshot[i][j]);
			}
		}

		buflen = min(count, strlen(buf));
		if (copy_to_user(ubuf, buf, buflen)) {
			dev_err_ratelimited(smmu->dev, "Couldn't copy_to_user\n");
			retval = -EFAULT;
		} else {
			*offset = 1;
			retval = buflen;
		}

		return retval;
	}
}

static const struct file_operations arm_smmu_debug_capturebus_snapshot_fops = {
	.open	= simple_open,
	.read	= arm_smmu_debug_capturebus_snapshot_read,
};

static ssize_t arm_smmu_debug_capturebus_config_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *offset)
{
	struct qsmmuv500_tbu_device *tbu = file->private_data;
	struct arm_smmu_device *smmu = tbu->smmu;
	void __iomem *tbu_base = tbu->base;
	struct qtb500_device *qtb = to_qtb500(tbu);
	void __iomem *transactiontracker_base = qtb->transactiontracker_base;
	char *comma1, *comma2;
	char buf[100];
	u64 sel, mask, match, val;

	if (of_device_is_compatible(tbu->dev->of_node, "qcom,qtb500")) {

		if (count >= sizeof(buf)) {
			dev_err_ratelimited(smmu->dev, "Input too large\n");
			goto invalid_format;
		}

		memset(buf, 0, sizeof(buf));

		if (copy_from_user(buf, ubuf, count)) {
			dev_err_ratelimited(smmu->dev, "Couldn't copy from user\n");
			return -EFAULT;
		}

		if (kstrtou64(buf, 0, &sel))
			goto invalid_tt_format;

		if (sel == 0 || sel == 1)
			goto transtracker_configure;

		else
			goto invalid_tt_format;

transtracker_configure:
		if (arm_smmu_power_on(smmu->pwr))
			return -EINVAL;

		if (arm_smmu_power_on(tbu->pwr)) {
			arm_smmu_power_off(smmu, smmu->pwr);
			return -EINVAL;
		}

		if (!mutex_trylock(&capture_reg_lock)) {
			dev_warn_ratelimited(smmu->dev,
				"Transaction Tracker regs in use, not configuring it.\n");
			return -EBUSY;
		}

		arm_smmu_debug_qtb_transtracker_set_config(transactiontracker_base, sel);

		mutex_unlock(&capture_reg_lock);
		arm_smmu_power_off(tbu->smmu, tbu->pwr);
		arm_smmu_power_off(smmu, smmu->pwr);

		return count;

invalid_tt_format:
		dev_err_ratelimited(smmu->dev, "Invalid format\n");
		dev_err_ratelimited(smmu->dev, "This is QTB equipped device\n");
		dev_err_ratelimited(smmu->dev,
				"Expected:<0/1> 0 for Default configuration, 1 for custom configuration\n");
		return -EINVAL;
	}

	else {

		if (count >= sizeof(buf)) {
			dev_err_ratelimited(smmu->dev, "Input too large\n");
			goto invalid_format;
		}

		memset(buf, 0, sizeof(buf));

		if (copy_from_user(buf, ubuf, count)) {
			dev_err_ratelimited(smmu->dev, "Couldn't copy from user\n");
			return -EFAULT;
		}

		comma1 = strnchr(buf, count, ',');
		if (!comma1)
			goto invalid_format;

		*comma1  = '\0';

		if (kstrtou64(buf, 0, &sel))
			goto invalid_format;

		if (sel > 4) {
			goto invalid_format;
		} else if (sel == 4) {
			if (kstrtou64(comma1 + 1, 0, &val))
				goto invalid_format;
			goto program_capturebus;
		}

		comma2 = strnchr(comma1 + 1, count, ',');
		if (!comma2)
			goto invalid_format;

		/* split up the words */
		*comma2 = '\0';

		if (kstrtou64(comma1 + 1, 0, &mask))
			goto invalid_format;

		if (kstrtou64(comma2 + 1, 0, &match))
			goto invalid_format;

program_capturebus:
		if (arm_smmu_power_on(smmu->pwr))
			return -EINVAL;

		if (arm_smmu_power_on(tbu->pwr)) {
			arm_smmu_power_off(smmu, smmu->pwr);
			return -EINVAL;
		}

		if (!mutex_trylock(&capture_reg_lock)) {
			dev_warn_ratelimited(smmu->dev,
				"capture bus regs in use, not configuring it.\n");
			return -EBUSY;
		}

		if (sel == 4)
			arm_smmu_debug_set_tnx_tcr_cntl(tbu_base, val);
		else
			arm_smmu_debug_set_mask_and_match(tbu_base, sel, mask, match);

		mutex_unlock(&capture_reg_lock);
		arm_smmu_power_off(tbu->smmu, tbu->pwr);
		arm_smmu_power_off(smmu, smmu->pwr);

		return count;

invalid_format:
		dev_err_ratelimited(smmu->dev, "Invalid format\n");
		dev_err_ratelimited(smmu->dev,
				    "Expected:<1/2/3,Mask,Match> <4,TNX_TCR_CNTL>\n");
		return -EINVAL;
	}
}

static ssize_t arm_smmu_debug_capturebus_config_read(struct file *file,
		char __user *ubuf, size_t count, loff_t *offset)
{
	struct qsmmuv500_tbu_device *tbu = file->private_data;
	struct arm_smmu_device *smmu = tbu->smmu;
	void __iomem *tbu_base = tbu->base;
	struct qtb500_device *qtb = to_qtb500(tbu);
	void __iomem *transactiontracker_base = qtb->transactiontracker_base;
	u64 val;
	u64 config;
	u64 mask[NO_OF_MASK_AND_MATCH], match[NO_OF_MASK_AND_MATCH];
	char buf[400];
	ssize_t retval;
	size_t buflen;
	int buf_len = sizeof(buf);
	int i;

	if (*offset)
		return 0;

	memset(buf, 0, buf_len);

	if (of_device_is_compatible(tbu->dev->of_node, "qcom,qtb500")) {

		if (arm_smmu_power_on(smmu->pwr))
			return -EINVAL;

		if (arm_smmu_power_on(tbu->pwr)) {
			arm_smmu_power_off(smmu, smmu->pwr);
			return -EINVAL;
		}

		if (!mutex_trylock(&capture_reg_lock)) {
			dev_warn_ratelimited(smmu->dev,
				"capture bus regs in use, not configuring it.\n");
			return -EBUSY;
		}

		config = arm_smmu_debug_qtb_transtracker_get_config(transactiontracker_base);

		mutex_unlock(&capture_reg_lock);
		arm_smmu_power_off(tbu->smmu, tbu->pwr);
		arm_smmu_power_off(smmu, smmu->pwr);

		if (config == (TTQTB_GlbEn | TTQTB_IgnoreCtiTrigIn0 | TTQTB_LogAsstEn))
			scnprintf(buf + strlen(buf), buf_len - strlen(buf),
					"Custom configuration selected, MainCtl filter val: 0x%0lx\n",
					config);

		else if (config == (TTQTB_GlbEn | TTQTB_IgnoreCtiTrigIn0 | TTQTB_LogAll))
			scnprintf(buf + strlen(buf), buf_len - strlen(buf),
					"Default configuration selected, MainCtl filter val : 0x%0lx\n",
					config);

		buflen = min(count, strlen(buf));

		if (copy_to_user(ubuf, buf, buflen)) {
			dev_err_ratelimited(smmu->dev, "Couldn't copy_to_user\n");
			retval = -EFAULT;
		} else {
			*offset = 1;
			retval = buflen;
		}

		return retval;
	}

	else {

		if (arm_smmu_power_on(smmu->pwr))
			return -EINVAL;

		if (arm_smmu_power_on(tbu->pwr)) {
			arm_smmu_power_off(smmu, smmu->pwr);
			return -EINVAL;
		}

		if (!mutex_trylock(&capture_reg_lock)) {
			dev_warn_ratelimited(smmu->dev,
					"capture bus regs in use, not configuring it.\n");
			return -EBUSY;
		}
		arm_smmu_debug_get_mask_and_match(tbu_base,
						mask, match);
		val = arm_smmu_debug_get_tnx_tcr_cntl(tbu_base);

		mutex_unlock(&capture_reg_lock);
		arm_smmu_power_off(tbu->smmu, tbu->pwr);
		arm_smmu_power_off(smmu, smmu->pwr);

		for (i = 0; i < NO_OF_MASK_AND_MATCH; ++i) {
			scnprintf(buf + strlen(buf), buf_len - strlen(buf),
					"Mask_%d : 0x%0llx\t", i+1, mask[i]);
			scnprintf(buf + strlen(buf), buf_len - strlen(buf),
					"Match_%d : 0x%0llx\n", i+1, match[i]);
		}
		scnprintf(buf + strlen(buf), buf_len - strlen(buf), "0x%0lx\n", val);

		buflen = min(count, strlen(buf));
		if (copy_to_user(ubuf, buf, buflen)) {
			dev_err_ratelimited(smmu->dev, "Couldn't copy_to_user\n");
			retval = -EFAULT;
		} else {
			*offset = 1;
			retval = buflen;
		}

		return retval;
	}
}

static ssize_t arm_smmu_debug_capturebus_filter_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *offset)
{
	struct qsmmuv500_tbu_device *tbu = file->private_data;
	struct arm_smmu_device *smmu = tbu->smmu;
	struct qtb500_device *qtb = to_qtb500(tbu);
	void __iomem *transactiontracker_base = qtb->transactiontracker_base;
	char *comma1;
	char buf[200];
	u64 sel, filter;
	int qtb_type = 0;

	if (count >= sizeof(buf)) {
		dev_err_ratelimited(smmu->dev, "Input too large\n");
		goto invalid_filter_format;
	}

	memset(buf, 0, sizeof(buf));

	if (copy_from_user(buf, ubuf, count)) {
		dev_err_ratelimited(smmu->dev, "Couldn't copy from user\n");
		return -EFAULT;
	}

	comma1 = strnchr(buf, count, ',');
	if (!comma1)
		goto invalid_filter_format;

	*comma1  = '\0';

	if (kstrtou64(buf, 0, &sel))
		goto invalid_filter_format;

	if (sel == 1 || sel == 2 || sel == 3) {
		if (kstrtou64(comma1 + 1, 0, &filter))
			goto invalid_filter_format;
		goto set_capturebus_filter;
	}

	else
		goto invalid_filter_format;

set_capturebus_filter:
		if (arm_smmu_power_on(smmu->pwr))
			return -EINVAL;

		if (arm_smmu_power_on(tbu->pwr)) {
			arm_smmu_power_off(smmu, smmu->pwr);
			return -EINVAL;
		}

		if (!mutex_trylock(&capture_reg_lock)) {
			dev_warn_ratelimited(smmu->dev,
				"Transaction Tracker regs in use, not configuring it.\n");
			return -EBUSY;
		}

		if (of_device_is_compatible(smmu->dev->of_node, "qcom,adreno-smmu"))
			qtb_type = 1;
		else
			qtb_type = 2;

		arm_smmu_debug_qtb_transtracker_setfilter(transactiontracker_base,
				sel, filter, qtb_type);

		mutex_unlock(&capture_reg_lock);
		arm_smmu_power_off(tbu->smmu, tbu->pwr);
		arm_smmu_power_off(smmu, smmu->pwr);

		return count;

invalid_filter_format:
		dev_err_ratelimited(smmu->dev, "Invalid format\n");
		dev_err_ratelimited(smmu->dev, "This is QTB equipped device\n");
		dev_err_ratelimited(smmu->dev,
				    "Expected:<1/2/3,TrType/AddressMin/AddressMax>\n");
		return -EINVAL;

}

static ssize_t arm_smmu_debug_capturebus_filter_read(struct file *file,
		char __user *ubuf, size_t count, loff_t *offset)
{
	struct qsmmuv500_tbu_device *tbu = file->private_data;
	struct arm_smmu_device *smmu = tbu->smmu;
	struct qtb500_device *qtb = to_qtb500(tbu);
	void __iomem *transactiontracker_base = qtb->transactiontracker_base;
	u64 filter[3];
	char buf[400];
	ssize_t retval;
	size_t buflen;
	int buf_len = sizeof(buf);
	int i = 0, qtb_type = 0;

	if (*offset)
		return 0;

	memset(buf, 0, buf_len);

	if (arm_smmu_power_on(smmu->pwr))
		return -EINVAL;

	if (arm_smmu_power_on(tbu->pwr)) {
		arm_smmu_power_off(smmu, smmu->pwr);
		return -EINVAL;
	}

	if (!mutex_trylock(&capture_reg_lock)) {
		dev_warn_ratelimited(smmu->dev,
			"Transaction Tracker regs in use, not configuring it.\n");
		return -EBUSY;
	}

	if (of_device_is_compatible(smmu->dev->of_node, "qcom,adreno-smmu"))
		qtb_type = 1;
	else
		qtb_type = 2;

	arm_smmu_debug_qtb_transtracker_getfilter(transactiontracker_base, filter, qtb_type);

	mutex_unlock(&capture_reg_lock);
	arm_smmu_power_off(tbu->smmu, tbu->pwr);
	arm_smmu_power_off(smmu, smmu->pwr);

	scnprintf(buf + strlen(buf), buf_len - strlen(buf),
		"Filter_TrType : 0x%lx\n", filter[i]);
	scnprintf(buf + strlen(buf), buf_len - strlen(buf),
		"Filter_AddressMin : 0x%lx\n", filter[i+1]);
	scnprintf(buf + strlen(buf), buf_len - strlen(buf),
		"Filter_AddressMax : 0x%lx\n", filter[i+2]);


	buflen = min(count, strlen(buf));
	if (copy_to_user(ubuf, buf, buflen)) {
		dev_err_ratelimited(smmu->dev, "Couldn't copy_to_user\n");
		retval = -EFAULT;
	} else {
		*offset = 1;
		retval = buflen;
	}

	return retval;
}

static const struct file_operations arm_smmu_debug_capturebus_config_fops = {
	.open	= simple_open,
	.write	= arm_smmu_debug_capturebus_config_write,
	.read	= arm_smmu_debug_capturebus_config_read,
};

static const struct file_operations arm_smmu_debug_capturebus_filter_fops = {
	.open	= simple_open,
	.write	= arm_smmu_debug_capturebus_filter_write,
	.read	= arm_smmu_debug_capturebus_filter_read,
};

#ifdef CONFIG_ARM_SMMU_CAPTUREBUS_DEBUGFS
static struct dentry *debugfs_capturebus_dir;

static int qsmmuv500_capturebus_init(struct qsmmuv500_tbu_device *tbu)
{
	struct dentry *capturebus_dir, *iommu_debug_dir;

	iommu_debug_dir = get_iommu_debug_dir();
	if (!iommu_debug_dir)
		return 0;

	if (!debugfs_capturebus_dir) {
		debugfs_capturebus_dir = debugfs_create_dir(
					 "capturebus", iommu_debug_dir);
		if (IS_ERR(debugfs_capturebus_dir)) {
			dev_err_ratelimited(tbu->dev, "Couldn't create iommu/capturebus debugfs directory\n");
			return PTR_ERR(debugfs_capturebus_dir);
		}
	}

	capturebus_dir = debugfs_create_dir(dev_name(tbu->dev),
				debugfs_capturebus_dir);
	if (IS_ERR(capturebus_dir)) {
		dev_err_ratelimited(tbu->dev, "Couldn't create iommu/capturebus/%s debugfs directory\n",
				dev_name(tbu->dev));
		goto err;
	}

	if (IS_ERR(debugfs_create_file("config", 0400, capturebus_dir, tbu,
			&arm_smmu_debug_capturebus_config_fops))) {
		dev_err_ratelimited(tbu->dev, "Couldn't create iommu/capturebus/%s/config debugfs file\n",
				dev_name(tbu->dev));
		goto err_rmdir;
	}

	if (of_device_is_compatible(tbu->dev->of_node, "qcom,qtb500")) {
		if (IS_ERR(debugfs_create_file("filter", 0400, capturebus_dir, tbu,
				&arm_smmu_debug_capturebus_filter_fops))) {
			dev_err_ratelimited(tbu->dev, "Couldn't create iommu/capturebus/%s/filter debugfs file\n",
					dev_name(tbu->dev));
			goto err_rmdir;
		}
	}

	if (IS_ERR(debugfs_create_file("snapshot", 0400, capturebus_dir, tbu,
			&arm_smmu_debug_capturebus_snapshot_fops))) {
		dev_err_ratelimited(tbu->dev, "Couldn't create iommu/capturebus/%s/snapshot debugfs file\n",
				dev_name(tbu->dev));
		goto err_rmdir;
	}
	return 0;
err_rmdir:
	debugfs_remove_recursive(capturebus_dir);
err:
	return -ENODEV;
}
#else
static int qsmmuv500_capturebus_init(struct qsmmuv500_tbu_device *tbu)
{
	return 0;
}
#endif

static irqreturn_t arm_smmu_debug_capture_bus_match(int irq, void *dev)
{
	struct qsmmuv500_tbu_device *tbu = dev;
	struct arm_smmu_device *smmu = tbu->smmu;
	void __iomem *tbu_base = tbu->base;
	u64 mask[NO_OF_MASK_AND_MATCH], match[NO_OF_MASK_AND_MATCH];
	u64 snapshot[NO_OF_CAPTURE_POINTS][REGS_PER_CAPTURE_POINT];
	int i, j;
	u64 val;

	if (arm_smmu_power_on(smmu->pwr))
		return IRQ_NONE;

	if (arm_smmu_power_on(tbu->pwr)) {
		arm_smmu_power_off(smmu, smmu->pwr);
		return IRQ_NONE;
	}

	if (!mutex_trylock(&capture_reg_lock)) {
		dev_warn_ratelimited(smmu->dev,
			"capture bus regs in use, not dumping it.\n");
		return IRQ_NONE;
	}

	val = arm_smmu_debug_get_tnx_tcr_cntl(tbu_base);
	arm_smmu_debug_get_mask_and_match(tbu_base, mask, match);
	arm_smmu_debug_get_capture_snapshot(tbu_base, snapshot);
	arm_smmu_debug_clear_intr_and_validbits(tbu_base);

	mutex_unlock(&capture_reg_lock);
	arm_smmu_power_off(tbu->smmu, tbu->pwr);
	arm_smmu_power_off(smmu, smmu->pwr);

	dev_info(tbu->dev, "TNX_TCR_CNTL : 0x%0llx\n", val);

	for (i = 0; i < NO_OF_MASK_AND_MATCH; ++i) {
		dev_info(tbu->dev,
				"Mask_%d : 0x%0llx\n", i+1, mask[i]);
		dev_info(tbu->dev,
				"Match_%d : 0x%0llx\n", i+1, match[i]);
	}

	for (i = 0; i < NO_OF_CAPTURE_POINTS ; ++i) {
		for (j = 0; j < REGS_PER_CAPTURE_POINT; ++j) {
			dev_info(tbu->dev,
					"Capture_%d_Snapshot_%d : 0x%0llx\n",
					i+1, j+1, snapshot[i][j]);
		}
	}

	return IRQ_HANDLED;
}

static void qsmmuv500_tlb_sync_timeout(struct arm_smmu_device *smmu)
{
	u32 sync_inv_ack, tbu_pwr_status, sync_inv_progress;
	u32 tbu_inv_pending = 0, tbu_sync_pending = 0;
	u32 tbu_inv_acked = 0, tbu_sync_acked = 0;
	u32 tcu_inv_pending = 0, tcu_sync_pending = 0;
	unsigned long tbu_ids = 0;
	struct qsmmuv500_archdata *data = to_qsmmuv500_archdata(smmu);
	int ret;

	static DEFINE_RATELIMIT_STATE(_rs,
				      DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);

	dev_err_ratelimited(smmu->dev,
			    "TLB sync timed out -- SMMU may be deadlocked\n");

	sync_inv_ack = arm_smmu_readl(smmu,
				      ARM_SMMU_IMPL_DEF5,
				      ARM_SMMU_STATS_SYNC_INV_TBU_ACK);

	if (sync_inv_ack) {
		tbu_inv_pending = FIELD_GET(TBU_INV_REQ, sync_inv_ack);
		tbu_inv_acked = FIELD_GET(TBU_INV_ACK, sync_inv_ack);
		tbu_sync_pending = FIELD_GET(TBU_SYNC_REQ, sync_inv_ack);
		tbu_sync_acked = FIELD_GET(TBU_SYNC_ACK, sync_inv_ack);
	}

	ret = qcom_scm_io_readl((unsigned long)(smmu->phys_addr +
				ARM_SMMU_TBU_PWR_STATUS), &tbu_pwr_status);
	if (ret) {
		dev_err_ratelimited(smmu->dev,
				    "SCM read of TBU power status fails: %d\n",
				    ret);
		goto out;
	}

	ret = qcom_scm_io_readl((unsigned long)(smmu->phys_addr +
				ARM_SMMU_MMU2QSS_AND_SAFE_WAIT_CNTR),
				&sync_inv_progress);
	if (ret) {
		dev_err_ratelimited(smmu->dev,
				    "SCM read of TBU sync/inv prog fails: %d\n",
				    ret);
		goto out;
	}

	if (tbu_pwr_status) {
		if (tbu_sync_pending)
			tbu_ids = tbu_pwr_status & ~tbu_sync_acked;
		else if (tbu_inv_pending)
			tbu_ids = tbu_pwr_status & ~tbu_inv_acked;
	}

	tcu_inv_pending = FIELD_GET(TCU_INV_IN_PRGSS, sync_inv_progress);
	tcu_sync_pending = FIELD_GET(TCU_SYNC_IN_PRGSS, sync_inv_progress);

	if (__ratelimit(&_rs)) {
		unsigned long tbu_id;

		dev_err(smmu->dev,
			"TBU ACK 0x%x TBU PWR 0x%x TCU sync_inv 0x%x\n",
			sync_inv_ack, tbu_pwr_status, sync_inv_progress);
		dev_err(smmu->dev,
			"TCU invalidation %s, TCU sync %s\n",
			tcu_inv_pending?"pending":"completed",
			tcu_sync_pending?"pending":"completed");

		for_each_set_bit(tbu_id, &tbu_ids, sizeof(tbu_ids) *
				 BITS_PER_BYTE) {

			struct qsmmuv500_tbu_device *tbu;

			tbu = qsmmuv500_find_tbu(smmu,
						 (u16)(tbu_id << TBUID_SHIFT));
			if (tbu) {
				dev_err(smmu->dev,
					"TBU %s ack pending for TBU %s, %s\n",
					tbu_sync_pending?"sync" : "inv",
					dev_name(tbu->dev),
					tbu_sync_pending ?
					"check pending transactions on TBU"
					: "check for TBU power status");
				arm_smmu_testbus_dump(smmu,
						(u16)(tbu_id << TBUID_SHIFT));
			}
		}

		/*dump TCU testbus*/
		arm_smmu_testbus_dump(smmu, U16_MAX);

	}

	if (tcu_sync_pending) {
		schedule_work(&data->outstanding_tnx_work);
		return;
	}
out:
	if (ret) {
		if (sync_inv_ack) {
			/* TBU PWR status is not available so just dump raw
			 * fields
			 */
			dev_err(smmu->dev,
				"TBU %s ack pending, got ack for TBUs %d, %s\n",
				tbu_sync_pending ? "sync" : "inv",
				tbu_sync_pending ? tbu_sync_acked:tbu_inv_acked,
				tbu_sync_pending ?
				"check pending transactions on TBU"
				: "check for TBU power status");

		}

		dev_err(smmu->dev, "TBU SYNC_INV_ACK reg 0x%x\n", sync_inv_ack);
	}

	BUG_ON(IS_ENABLED(CONFIG_IOMMU_TLBSYNC_DEBUG));
}

static void qsmmuv500_device_remove(struct arm_smmu_device *smmu)
{
	struct qsmmuv500_archdata *data = to_qsmmuv500_archdata(smmu);

	cancel_work_sync(&data->outstanding_tnx_work);
}

/*
 * Checks whether smr2 is a subset of smr
 */
static bool smr_is_subset(struct arm_smmu_smr *smr2, struct arm_smmu_smr *smr)
{
	return (smr->mask & smr2->mask) == smr2->mask &&
	    !((smr->id ^ smr2->id) & ~smr->mask);
}

/*
 * Zero means failure.
 */
static phys_addr_t qsmmuv500_iova_to_phys(struct arm_smmu_domain *smmu_domain, dma_addr_t iova,
					   u32 sid, unsigned long trans_flags)
{
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	struct arm_smmu_cfg *cfg = &smmu_domain->cfg;
	struct qsmmuv500_archdata *data = to_qsmmuv500_archdata(smmu);
	struct qsmmuv500_tbu_device *tbu;
	phys_addr_t phys = 0;
	int idx = cfg->cbndx;
	int needs_redo = 0;
	u32 sctlr_orig, sctlr, fsr;
	unsigned long spinlock_flags;

	tbu = qsmmuv500_find_tbu(smmu, sid);
	if (!tbu)
		return 0;

	if (arm_smmu_power_on(tbu->pwr))
		return 0;

	if (iova >= (1ULL << tbu->iova_width)) {
		dev_err_ratelimited(tbu->dev, "ECATS: address too large: %pad\n", &iova);
		return 0;
	}

	if (qsmmuv500_tbu_halt(tbu, smmu_domain))
		goto out_power_off;

	/*
	 * ECATS can trigger the fault interrupt, so disable it temporarily
	 * and check for an interrupt manually.
	 */
	sctlr_orig = arm_smmu_cb_read(smmu, idx, ARM_SMMU_CB_SCTLR);
	sctlr = sctlr_orig & ~(ARM_SMMU_SCTLR_CFCFG | ARM_SMMU_SCTLR_CFIE);
	arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_SCTLR, sctlr);

	fsr = arm_smmu_cb_read(smmu, idx, ARM_SMMU_CB_FSR);
	if (fsr & ARM_SMMU_FSR_FAULT) {
		/* Clear pending interrupts */
		arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_FSR, fsr);
		/*
		 * Barrier required to ensure that the FSR is cleared
		 * before resuming SMMU operation.
		 */
		wmb();

		/*
		 * TBU halt takes care of resuming any stalled transcation.
		 * Kept it here for completeness sake.
		 */
		if (fsr & ARM_SMMU_FSR_SS)
			arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_RESUME,
					  ARM_SMMU_RESUME_TERMINATE);
	}

	/* Only one concurrent atos operation */
	spin_lock_irqsave(&data->atos_lock, spinlock_flags);

	/*
	 * After a failed translation, the next successful translation will
	 * incorrectly be reported as a failure.
	 */
	do {
		phys = tbu->impl->trigger_atos(tbu, iova, sid, trans_flags);

		fsr = arm_smmu_cb_read(smmu, idx, ARM_SMMU_CB_FSR);
		if (fsr & ARM_SMMU_FSR_FAULT) {
			/* Clear pending interrupts */
			arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_FSR, fsr);
			/*
			 * Barrier required to ensure that the FSR is cleared
			 * before resuming SMMU operation.
			 */
			wmb();

			if (fsr & ARM_SMMU_FSR_SS)
				arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_RESUME,
						  ARM_SMMU_RESUME_TERMINATE);
		}
	} while (!phys && needs_redo++ < 2);

	arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_SCTLR, sctlr_orig);
	spin_unlock_irqrestore(&data->atos_lock, spinlock_flags);
	qsmmuv500_tbu_resume(tbu);

out_power_off:
	/* Read to complete prior write transcations */
	tbu->impl->write_sync(tbu);

	/* Wait for read to complete before off */
	rmb();

	arm_smmu_power_off(tbu->smmu, tbu->pwr);

	return phys;
}

static phys_addr_t qsmmuv500_iova_to_phys_hard(
					struct arm_smmu_domain *smmu_domain,
					struct qcom_iommu_atos_txn *txn)
{
	return qsmmuv500_iova_to_phys(smmu_domain, txn->addr, txn->id,
				txn->flags);
}

static void qsmmuv500_release_group_iommudata(void *data)
{
	kfree(data);
}

/* If a device has a valid actlr, it must match */
static int qsmmuv500_device_group(struct device *dev,
				struct iommu_group *group)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct arm_smmu_master_cfg *cfg = dev_iommu_priv_get(dev);
	struct arm_smmu_device *smmu;
	struct qsmmuv500_archdata *data;
	struct qsmmuv500_group_iommudata *iommudata;
	u32 actlr, i, j, idx;
	struct arm_smmu_smr *smr, *smr2;

	if (!fwspec || !cfg)
		return -EINVAL;

	smmu = cfg->smmu;
	data = to_qsmmuv500_archdata(smmu);

	iommudata = to_qsmmuv500_group_iommudata(group);
	if (!iommudata) {
		iommudata = kzalloc(sizeof(*iommudata), GFP_KERNEL);
		if (!iommudata)
			return -ENOMEM;

		iommu_group_set_iommudata(group, iommudata,
				qsmmuv500_release_group_iommudata);
	}

	for (i = 0; i < data->actlr_tbl_size; i++) {
		smr = &data->actlrs[i].smr;
		actlr = data->actlrs[i].actlr;

		for_each_cfg_sme(cfg, fwspec, j, idx) {
			smr2 = &smmu->smrs[idx];
			if (!smr_is_subset(smr2, smr))
				continue;

			dev_dbg(dev, "Matched actlr sid=%x mask=%x actlr=%x\n",
				smr->id, smr->mask, actlr);

			if (!iommudata->has_actlr) {
				iommudata->actlr = actlr;
				iommudata->has_actlr = true;
			} else if (iommudata->actlr != actlr) {
				dev_err(dev, "Invalid actlr setting\n");
				return -EINVAL;
			}
		}
	}

	return 0;
}

static void qsmmuv500_init_cb(struct arm_smmu_domain *smmu_domain,
				struct device *dev)
{
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	struct qsmmuv500_group_iommudata *iommudata =
		to_qsmmuv500_group_iommudata(dev->iommu_group);
	int idx = smmu_domain->cfg.cbndx;

	if (!iommudata->has_actlr)
		return;

	arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_ACTLR, iommudata->actlr);

	/*
	 * Flush the context bank after modifying ACTLR to ensure there
	 * are no cache entries with stale state
	 */
	iommu_flush_iotlb_all(&smmu_domain->domain);
}

static int qsmmuv500_tbu_register(struct device *dev, void *cookie)
{
	struct resource *res;
	struct qsmmuv500_tbu_device *tbu;
	struct qsmmuv500_archdata *data = cookie;
	struct platform_device *pdev = to_platform_device(dev);
	int i, err, num_irqs = 0;

	if (!dev->driver) {
		dev_err(dev, "TBU failed probe, QSMMUV500 cannot continue!\n");
		return -EINVAL;
	}

	tbu = dev_get_drvdata(dev);

	INIT_LIST_HEAD(&tbu->list);
	tbu->smmu = &data->smmu;
	list_add(&tbu->list, &data->tbus);

	while ((res = platform_get_resource(pdev, IORESOURCE_IRQ, num_irqs)))
		num_irqs++;

	tbu->irqs = devm_kzalloc(dev, sizeof(*tbu->irqs) * num_irqs,
				  GFP_KERNEL);
	if (!tbu->irqs)
		return -ENOMEM;

	for (i = 0; i < num_irqs; ++i) {
		int irq = platform_get_irq(pdev, i);

		if (irq < 0) {
			dev_err(dev, "failed to get irq index %d\n", i);
			return -ENODEV;
		}
		tbu->irqs[i] = irq;

		err = devm_request_threaded_irq(tbu->dev, tbu->irqs[i],
					NULL, arm_smmu_debug_capture_bus_match,
					IRQF_ONESHOT | IRQF_SHARED,
					"capture bus", tbu);
		if (err) {
			dev_err(dev, "failed to request capture bus irq%d (%u)\n",
				i, tbu->irqs[i]);
			return err;
		}
	}

	/*
	 * Create testbus debugfs only if debugchain base
	 *  property is set in devicetree in case of qtb500.
	 */

	if (!of_device_is_compatible(tbu->dev->of_node, "qcom,qtb500") ||
			to_qtb500(tbu)->debugchain_base)
		qsmmuv500_tbu_testbus_init(tbu);

	/*
	 * Create transactiontracker debugfs only if
	 * transactiontracker base is set in devicetree in case of qtb500.
	 */

	if (!of_device_is_compatible(tbu->dev->of_node, "qcom,qtb500") ||
			to_qtb500(tbu)->transactiontracker_base)
		qsmmuv500_capturebus_init(tbu);

	return 0;
}

static int qsmmuv500_read_actlr_tbl(struct qsmmuv500_archdata *data)
{
	int len, i;
	struct device *dev = data->smmu.dev;
	struct actlr_setting *actlrs;
	const __be32 *cell;

	cell = of_get_property(dev->of_node, "qcom,actlr", NULL);
	if (!cell)
		return 0;

	len = of_property_count_elems_of_size(dev->of_node, "qcom,actlr",
						sizeof(u32) * 3);
	if (len < 0)
		return 0;

	actlrs = devm_kzalloc(dev, sizeof(*actlrs) * len, GFP_KERNEL);
	if (!actlrs)
		return -ENOMEM;

	for (i = 0; i < len; i++) {
		actlrs[i].smr.id = of_read_number(cell++, 1);
		actlrs[i].smr.mask = of_read_number(cell++, 1);
		actlrs[i].actlr = of_read_number(cell++, 1);
	}

	data->actlrs = actlrs;
	data->actlr_tbl_size = len;
	return 0;
}

static int qsmmuv500_cfg_probe(struct arm_smmu_device *smmu)
{
	u32 val;

	val = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_sACR);
	val &= ~ARM_MMU500_ACR_CACHE_LOCK;
	arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_sACR, val);
	val = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_sACR);
	/*
	 * Modifiying the nonsecure copy of the sACR register is only
	 * allowed if permission is given in the secure sACR register.
	 * Attempt to detect if we were able to update the value.
	 */
	WARN_ON(val & ARM_MMU500_ACR_CACHE_LOCK);

	return 0;
}

/*
 * Case 1)
 * Client wants to use the standard upstream dma ops from
 * drivers/iommu/dma-iommu.c
 *
 * This requires domain->type == IOMMU_DOMAIN_DMA.
 *
 * Case 2)
 * Client doesn't want to use the default dma domain, and wants to
 * allocate their own via iommu_domain_alloc()
 *
 * There are insufficient context banks to "waste" one on a default
 * dma domain that isn't going to be used. Therefore, give it
 * IOMMU_DOMAIN_IDENTITY. Note that IOMMU_DOMAIN_IDENTITY is treated as
 * IOMMU_DOMAIN_BLOCKED by our hypervisor, which doesn't allow setting
 * S2CR.TYPE = 0x1.
 *
 * Case 3)
 * Client wants to use our fastmap dma ops
 *
 * Per case 1) we cannot use IOMMU_DOMAIN_DMA, since those imply using
 * the standard upstream dma ops. So use IOMMU_DOMAIN_UNMANAGED instead.
 *
 * Case 4)
 * Client wants to use S1 bypass
 *
 * Same as Case 3, except use the platform dma ops.
 *
 * This function can be used for qsmmuv500 and qsmmuv2.
 */
static int qcom_def_domain_type(struct device *dev)
{
	const char *str;
	struct device_node *np;

	/* Default to iommu_def_domain_type */
	np = of_parse_phandle(dev->of_node, "qcom,iommu-group", 0);
	if (!np)
		np = dev->of_node;

	if (of_property_read_string(np, "qcom,iommu-dma", &str))
		str = "default";

	if (!strcmp(str, "fastmap") &&
	     IS_ENABLED(CONFIG_IOMMU_IO_PGTABLE_FAST))
		return IOMMU_DOMAIN_UNMANAGED;
	if (!strcmp(str, "bypass"))
		return IOMMU_DOMAIN_UNMANAGED;
	if (!strcmp(str, "atomic"))
		return IOMMU_DOMAIN_DMA;
	if (!strcmp(str, "disabled"))
		return IOMMU_DOMAIN_IDENTITY;

	return IOMMU_DOMAIN_DMA;
}

static const struct arm_smmu_impl qsmmuv500_impl = {
	.cfg_probe = qsmmuv500_cfg_probe,
	.init_context_bank = qsmmuv500_init_cb,
	.iova_to_phys_hard = qsmmuv500_iova_to_phys_hard,
	.tlb_sync_timeout = qsmmuv500_tlb_sync_timeout,
	.device_remove = qsmmuv500_device_remove,
	.device_group = qsmmuv500_device_group,
	.def_domain_type = qcom_def_domain_type,
};

static const struct arm_smmu_impl qsmmuv500_adreno_impl = {
	.init_context = qcom_adreno_smmu_init_context,
	.alloc_context_bank = qcom_adreno_smmu_alloc_context_bank,
	.cfg_probe = qsmmuv500_cfg_probe,
	.init_context_bank = qsmmuv500_init_cb,
	.iova_to_phys_hard = qsmmuv500_iova_to_phys_hard,
	.tlb_sync_timeout = qsmmuv500_tlb_sync_timeout,
	.device_remove = qsmmuv500_device_remove,
	.device_group = qsmmuv500_device_group,
	.def_domain_type = qcom_def_domain_type,
};

static const struct arm_smmu_impl qsmmuv2_impl = {
	.init_context_bank = qsmmuv2_init_cb,
	.iova_to_phys_hard = qsmmuv2_iova_to_phys_hard,
	.tlb_sync_timeout = qsmmuv2_tlb_sync_timeout,
	.reset = qsmmuv2_device_reset,
	.def_domain_type = qcom_def_domain_type,
};

static const struct arm_smmu_impl qsmmuv2_adreno_impl = {
	.init_context = qcom_adreno_smmu_init_context,
	.alloc_context_bank = qcom_adreno_smmu_alloc_context_bank,
	.init_context_bank = qsmmuv2_init_cb,
	.iova_to_phys_hard = qsmmuv2_iova_to_phys_hard,
	.tlb_sync_timeout = qsmmuv2_tlb_sync_timeout,
	.reset = qsmmuv2_device_reset,
	.def_domain_type = qcom_def_domain_type,
};

/* We only have access to arm-architected registers */
static const struct arm_smmu_impl qsmmuv500_virt_impl = {
	.cfg_probe = qsmmuv500_cfg_probe,
	.init_context_bank = qsmmuv500_init_cb,
	.device_group = qsmmuv500_device_group,
	.def_domain_type = qcom_def_domain_type,
};

struct arm_smmu_device *qsmmuv500_create(struct arm_smmu_device *smmu,
		const struct arm_smmu_impl *impl)
{
	struct device *dev = smmu->dev;
	struct qsmmuv500_archdata *data;
	int ret;
#if IS_ENABLED(CONFIG_ARM_SMMU_TESTBUS)
	struct platform_device *pdev;
#endif

	/*
	 * devm_krealloc() invokes devm_kmalloc(), so we pass __GFP_ZERO
	 * to ensure that fields after smmu are initialized, even if we don't
	 * initialize them (e.g. ACTLR related fields).
	 */
	data = devm_krealloc(dev, smmu, sizeof(*data), GFP_KERNEL | __GFP_ZERO);
	if (!data)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&data->tbus);
	spin_lock_init(&data->atos_lock);
	INIT_WORK(&data->outstanding_tnx_work,
		  qsmmuv500_log_outstanding_transactions);
	data->smmu.impl = impl;

#if IS_ENABLED(CONFIG_ARM_SMMU_TESTBUS)
	pdev = to_platform_device(dev);
	data->tcu_base = devm_platform_ioremap_resource_byname(pdev, "tcu-base");
	if (IS_ERR(data->tcu_base)) {
		dev_err(dev, "Unable to get the tcu-base\n");
		return ERR_PTR(-EINVAL);
	}
#endif

	qsmmuv500_tcu_testbus_init(&data->smmu);

	ret = qsmmuv500_read_actlr_tbl(data);
	if (ret)
		return ERR_PTR(ret);

	ret = of_platform_populate(dev->of_node, NULL, NULL, dev);
	if (ret)
		return ERR_PTR(ret);

	/* Attempt to register child devices */
	ret = device_for_each_child(dev, data, qsmmuv500_tbu_register);
	if (ret)
		return ERR_PTR(-EPROBE_DEFER);

	return &data->smmu;
}

static struct arm_smmu_device *qsmmuv500_virt_create(struct arm_smmu_device *smmu,
		const struct arm_smmu_impl *impl)
{
	struct device *dev = smmu->dev;
	struct qsmmuv500_archdata *data;
	int ret;

	data = devm_krealloc(dev, smmu, sizeof(*data), GFP_KERNEL | __GFP_ZERO);
	if (!data)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&data->tbus);
	spin_lock_init(&data->atos_lock);
	INIT_WORK(&data->outstanding_tnx_work,
		  qsmmuv500_log_outstanding_transactions);
	data->smmu.impl = impl;

	ret = qsmmuv500_read_actlr_tbl(data);
	if (ret)
		return ERR_PTR(ret);

	return &data->smmu;
}

struct arm_smmu_device *qsmmuv500_impl_init(struct arm_smmu_device *smmu)
{
	if (of_device_is_compatible(smmu->dev->of_node, "qcom,adreno-smmu"))
		return qsmmuv500_create(smmu, &qsmmuv500_adreno_impl);

	if (of_device_is_compatible(smmu->dev->of_node, "qcom,virt-smmu"))
		return qsmmuv500_virt_create(smmu, &qsmmuv500_virt_impl);

	return qsmmuv500_create(smmu, &qsmmuv500_impl);
}

static const struct arm_smmu_impl qcom_adreno_smmu_impl = {
	.init_context = qcom_adreno_smmu_init_context,
	.def_domain_type = qcom_smmu_def_domain_type,
	.reset = qcom_smmu500_reset,
	.alloc_context_bank = qcom_adreno_smmu_alloc_context_bank,
	.write_sctlr = qcom_adreno_smmu_write_sctlr,
	.tlb_sync = qcom_smmu_tlb_sync,
};

static struct arm_smmu_device *qcom_smmu_create(struct arm_smmu_device *smmu,
		const struct arm_smmu_impl *impl)
{
	struct qcom_smmu *qsmmu;

	/* Check to make sure qcom_scm has finished probing */
	if (!qcom_scm_is_available())
		return ERR_PTR(-EPROBE_DEFER);

	qsmmu = devm_krealloc(smmu->dev, smmu, sizeof(*qsmmu), GFP_KERNEL);
	if (!qsmmu)
		return ERR_PTR(-ENOMEM);

	qsmmu->smmu.impl = impl;
	qsmmu->cfg = qcom_smmu_impl_data(smmu);

	return &qsmmu->smmu;
}

static const struct of_device_id __maybe_unused qcom_smmu_impl_of_match[] = {
	{ .compatible = "qcom,msm8998-smmu-v2" },
	{ .compatible = "qcom,qcm2290-smmu-500" },
	{ .compatible = "qcom,sc7180-smmu-500" },
	{ .compatible = "qcom,sc7280-smmu-500" },
	{ .compatible = "qcom,sc8180x-smmu-500" },
	{ .compatible = "qcom,sc8280xp-smmu-500" },
	{ .compatible = "qcom,sdm630-smmu-v2" },
	{ .compatible = "qcom,sdm845-smmu-500" },
	{ .compatible = "qcom,sm6125-smmu-500" },
	{ .compatible = "qcom,sm6350-smmu-500" },
	{ .compatible = "qcom,sm6375-smmu-500" },
	{ .compatible = "qcom,sm8150-smmu-500" },
	{ .compatible = "qcom,sm8250-smmu-500" },
	{ .compatible = "qcom,sm8350-smmu-500" },
	{ .compatible = "qcom,sm8450-smmu-500" },
	{ }
};

#ifdef CONFIG_ACPI
static struct acpi_platform_list qcom_acpi_platlist[] = {
	{ "LENOVO", "CB-01   ", 0x8180, ACPI_SIG_IORT, equal, "QCOM SMMU" },
	{ "QCOM  ", "QCOMEDK2", 0x8180, ACPI_SIG_IORT, equal, "QCOM SMMU" },
	{ }
};
#endif

struct arm_smmu_device *qcom_smmu_impl_init(struct arm_smmu_device *smmu)
{
	const struct device_node *np = smmu->dev->of_node;

#ifdef CONFIG_ACPI
	if (np == NULL) {
		/* Match platform for ACPI boot */
		if (acpi_match_platform_list(qcom_acpi_platlist) >= 0)
			return qcom_smmu_create(smmu, &qcom_smmu_impl);
	}
#endif

	/*
	 * Do not change this order of implementation, i.e., first adreno
	 * smmu impl and then apss smmu since we can have both implementing
	 * arm,mmu-500 in which case we will miss setting adreno smmu specific
	 * features if the order is changed.
	 */
	if (of_device_is_compatible(np, "qcom,adreno-smmu"))
		return qcom_smmu_create(smmu, &qcom_adreno_smmu_impl);

	if (of_match_node(qcom_smmu_impl_of_match, np))
		return qcom_smmu_create(smmu, &qcom_smmu_impl);

	return smmu;
}

struct arm_smmu_device *qsmmuv2_impl_init(struct arm_smmu_device *smmu)
{
	struct device *dev = smmu->dev;
	struct qsmmuv2_archdata *data;
	struct platform_device *pdev;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);

	pdev = to_platform_device(dev);

	spin_lock_init(&data->atos_lock);
	data->smmu = *smmu;
	if (of_device_is_compatible(smmu->dev->of_node, "qcom,adreno-smmu"))
		data->smmu.impl = &qsmmuv2_adreno_impl;
	else
		data->smmu.impl = &qsmmuv2_impl;

	ret = arm_smmu_parse_impl_def_registers(&data->smmu);
	if (ret)
		return ERR_PTR(ret);

	return &data->smmu;
}

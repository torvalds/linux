// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 */

#include <linux/acpi.h>
#include <linux/adreno-smmu-priv.h>
#include <linux/delay.h>
#include <linux/of_device.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "arm-smmu.h"
#include "arm-smmu-qcom.h"

#define QCOM_DUMMY_VAL	-1

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

		if (sid == QCOM_ADRENO_SMMU_GPU_SID)
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
	 * Assign context bank 0 to the GPU device so the GPU hardware can
	 * switch pagetables
	 */
	if (qcom_adreno_smmu_is_gpu_device(dev)) {
		start = 0;
		count = 1;
	} else {
		start = 1;
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

static int qcom_adreno_smmu_init_context(struct arm_smmu_domain *smmu_domain,
		struct io_pgtable_cfg *pgtbl_cfg, struct device *dev)
{
	struct adreno_smmu_priv *priv;

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
	priv->set_stall = qcom_adreno_smmu_set_stall;
	priv->resume_translation = qcom_adreno_smmu_resume_translation;

	return 0;
}

static const struct of_device_id qcom_smmu_client_of_match[] __maybe_unused = {
	{ .compatible = "qcom,adreno" },
	{ .compatible = "qcom,adreno-gmu" },
	{ .compatible = "qcom,mdp4" },
	{ .compatible = "qcom,mdss" },
	{ .compatible = "qcom,qcm2290-mdss" },
	{ .compatible = "qcom,sc7180-mdss" },
	{ .compatible = "qcom,sc7180-mss-pil" },
	{ .compatible = "qcom,sc7280-mdss" },
	{ .compatible = "qcom,sc7280-mss-pil" },
	{ .compatible = "qcom,sc8180x-mdss" },
	{ .compatible = "qcom,sc8280xp-mdss" },
	{ .compatible = "qcom,sdm670-mdss" },
	{ .compatible = "qcom,sdm845-mdss" },
	{ .compatible = "qcom,sdm845-mss-pil" },
	{ .compatible = "qcom,sm6350-mdss" },
	{ .compatible = "qcom,sm6375-mdss" },
	{ .compatible = "qcom,sm8150-mdss" },
	{ .compatible = "qcom,sm8250-mdss" },
	{ .compatible = "qcom,x1e80100-mdss" },
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

	arm_mmu500_reset(smmu);

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

static const struct arm_smmu_impl qcom_smmu_v2_impl = {
	.init_context = qcom_smmu_init_context,
	.cfg_probe = qcom_smmu_cfg_probe,
	.def_domain_type = qcom_smmu_def_domain_type,
	.write_s2cr = qcom_smmu_write_s2cr,
	.tlb_sync = qcom_smmu_tlb_sync,
};

static const struct arm_smmu_impl qcom_smmu_500_impl = {
	.init_context = qcom_smmu_init_context,
	.cfg_probe = qcom_smmu_cfg_probe,
	.def_domain_type = qcom_smmu_def_domain_type,
	.reset = arm_mmu500_reset,
	.write_s2cr = qcom_smmu_write_s2cr,
	.tlb_sync = qcom_smmu_tlb_sync,
#ifdef CONFIG_ARM_SMMU_QCOM_DEBUG
	.context_fault = qcom_smmu_context_fault,
	.context_fault_needs_threaded_irq = true,
#endif
};

static const struct arm_smmu_impl sdm845_smmu_500_impl = {
	.init_context = qcom_smmu_init_context,
	.cfg_probe = qcom_smmu_cfg_probe,
	.def_domain_type = qcom_smmu_def_domain_type,
	.reset = qcom_sdm845_smmu500_reset,
	.write_s2cr = qcom_smmu_write_s2cr,
	.tlb_sync = qcom_smmu_tlb_sync,
#ifdef CONFIG_ARM_SMMU_QCOM_DEBUG
	.context_fault = qcom_smmu_context_fault,
	.context_fault_needs_threaded_irq = true,
#endif
};

static const struct arm_smmu_impl qcom_adreno_smmu_v2_impl = {
	.init_context = qcom_adreno_smmu_init_context,
	.def_domain_type = qcom_smmu_def_domain_type,
	.alloc_context_bank = qcom_adreno_smmu_alloc_context_bank,
	.write_sctlr = qcom_adreno_smmu_write_sctlr,
	.tlb_sync = qcom_smmu_tlb_sync,
};

static const struct arm_smmu_impl qcom_adreno_smmu_500_impl = {
	.init_context = qcom_adreno_smmu_init_context,
	.def_domain_type = qcom_smmu_def_domain_type,
	.reset = arm_mmu500_reset,
	.alloc_context_bank = qcom_adreno_smmu_alloc_context_bank,
	.write_sctlr = qcom_adreno_smmu_write_sctlr,
	.tlb_sync = qcom_smmu_tlb_sync,
};

static struct arm_smmu_device *qcom_smmu_create(struct arm_smmu_device *smmu,
		const struct qcom_smmu_match_data *data)
{
	const struct device_node *np = smmu->dev->of_node;
	const struct arm_smmu_impl *impl;
	struct qcom_smmu *qsmmu;

	if (!data)
		return ERR_PTR(-EINVAL);

	if (np && of_device_is_compatible(np, "qcom,adreno-smmu"))
		impl = data->adreno_impl;
	else
		impl = data->impl;

	if (!impl)
		return smmu;

	/* Check to make sure qcom_scm has finished probing */
	if (!qcom_scm_is_available())
		return ERR_PTR(dev_err_probe(smmu->dev, -EPROBE_DEFER,
			"qcom_scm not ready\n"));

	qsmmu = devm_krealloc(smmu->dev, smmu, sizeof(*qsmmu), GFP_KERNEL);
	if (!qsmmu)
		return ERR_PTR(-ENOMEM);

	qsmmu->smmu.impl = impl;
	qsmmu->cfg = data->cfg;

	return &qsmmu->smmu;
}

/* Implementation Defined Register Space 0 register offsets */
static const u32 qcom_smmu_impl0_reg_offset[] = {
	[QCOM_SMMU_TBU_PWR_STATUS]		= 0x2204,
	[QCOM_SMMU_STATS_SYNC_INV_TBU_ACK]	= 0x25dc,
	[QCOM_SMMU_MMU2QSS_AND_SAFE_WAIT_CNTR]	= 0x2670,
};

static const struct qcom_smmu_config qcom_smmu_impl0_cfg = {
	.reg_offset = qcom_smmu_impl0_reg_offset,
};

/*
 * It is not yet possible to use MDP SMMU with the bypass quirk on the msm8996,
 * there are not enough context banks.
 */
static const struct qcom_smmu_match_data msm8996_smmu_data = {
	.impl = NULL,
	.adreno_impl = &qcom_adreno_smmu_v2_impl,
};

static const struct qcom_smmu_match_data qcom_smmu_v2_data = {
	.impl = &qcom_smmu_v2_impl,
	.adreno_impl = &qcom_adreno_smmu_v2_impl,
};

static const struct qcom_smmu_match_data sdm845_smmu_500_data = {
	.impl = &sdm845_smmu_500_impl,
	/*
	 * No need for adreno impl here. On sdm845 the Adreno SMMU is handled
	 * by the separate sdm845-smmu-v2 device.
	 */
	/* Also no debug configuration. */
};

static const struct qcom_smmu_match_data qcom_smmu_500_impl0_data = {
	.impl = &qcom_smmu_500_impl,
	.adreno_impl = &qcom_adreno_smmu_500_impl,
	.cfg = &qcom_smmu_impl0_cfg,
};

/*
 * Do not add any more qcom,SOC-smmu-500 entries to this list, unless they need
 * special handling and can not be covered by the qcom,smmu-500 entry.
 */
static const struct of_device_id __maybe_unused qcom_smmu_impl_of_match[] = {
	{ .compatible = "qcom,msm8996-smmu-v2", .data = &msm8996_smmu_data },
	{ .compatible = "qcom,msm8998-smmu-v2", .data = &qcom_smmu_v2_data },
	{ .compatible = "qcom,qcm2290-smmu-500", .data = &qcom_smmu_500_impl0_data },
	{ .compatible = "qcom,qdu1000-smmu-500", .data = &qcom_smmu_500_impl0_data  },
	{ .compatible = "qcom,sc7180-smmu-500", .data = &qcom_smmu_500_impl0_data },
	{ .compatible = "qcom,sc7180-smmu-v2", .data = &qcom_smmu_v2_data },
	{ .compatible = "qcom,sc7280-smmu-500", .data = &qcom_smmu_500_impl0_data },
	{ .compatible = "qcom,sc8180x-smmu-500", .data = &qcom_smmu_500_impl0_data },
	{ .compatible = "qcom,sc8280xp-smmu-500", .data = &qcom_smmu_500_impl0_data },
	{ .compatible = "qcom,sdm630-smmu-v2", .data = &qcom_smmu_v2_data },
	{ .compatible = "qcom,sdm845-smmu-v2", .data = &qcom_smmu_v2_data },
	{ .compatible = "qcom,sdm845-smmu-500", .data = &sdm845_smmu_500_data },
	{ .compatible = "qcom,sm6115-smmu-500", .data = &qcom_smmu_500_impl0_data},
	{ .compatible = "qcom,sm6125-smmu-500", .data = &qcom_smmu_500_impl0_data },
	{ .compatible = "qcom,sm6350-smmu-v2", .data = &qcom_smmu_v2_data },
	{ .compatible = "qcom,sm6350-smmu-500", .data = &qcom_smmu_500_impl0_data },
	{ .compatible = "qcom,sm6375-smmu-v2", .data = &qcom_smmu_v2_data },
	{ .compatible = "qcom,sm6375-smmu-500", .data = &qcom_smmu_500_impl0_data },
	{ .compatible = "qcom,sm7150-smmu-v2", .data = &qcom_smmu_v2_data },
	{ .compatible = "qcom,sm8150-smmu-500", .data = &qcom_smmu_500_impl0_data },
	{ .compatible = "qcom,sm8250-smmu-500", .data = &qcom_smmu_500_impl0_data },
	{ .compatible = "qcom,sm8350-smmu-500", .data = &qcom_smmu_500_impl0_data },
	{ .compatible = "qcom,sm8450-smmu-500", .data = &qcom_smmu_500_impl0_data },
	{ .compatible = "qcom,smmu-500", .data = &qcom_smmu_500_impl0_data },
	{ }
};

#ifdef CONFIG_ACPI
static struct acpi_platform_list qcom_acpi_platlist[] = {
	{ "LENOVO", "CB-01   ", 0x8180, ACPI_SIG_IORT, equal, "QCOM SMMU" },
	{ "QCOM  ", "QCOMEDK2", 0x8180, ACPI_SIG_IORT, equal, "QCOM SMMU" },
	{ }
};
#endif

static int qcom_smmu_tbu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	if (IS_ENABLED(CONFIG_ARM_SMMU_QCOM_DEBUG)) {
		ret = qcom_tbu_probe(pdev);
		if (ret)
			return ret;
	}

	if (dev->pm_domain) {
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);
	}

	return 0;
}

static const struct of_device_id qcom_smmu_tbu_of_match[] = {
	{ .compatible = "qcom,sc7280-tbu" },
	{ .compatible = "qcom,sdm845-tbu" },
	{ }
};

static struct platform_driver qcom_smmu_tbu_driver = {
	.driver = {
		.name           = "qcom_tbu",
		.of_match_table = qcom_smmu_tbu_of_match,
	},
	.probe = qcom_smmu_tbu_probe,
};

struct arm_smmu_device *qcom_smmu_impl_init(struct arm_smmu_device *smmu)
{
	const struct device_node *np = smmu->dev->of_node;
	const struct of_device_id *match;
	static u8 tbu_registered;

	if (!tbu_registered++)
		platform_driver_register(&qcom_smmu_tbu_driver);

#ifdef CONFIG_ACPI
	if (np == NULL) {
		/* Match platform for ACPI boot */
		if (acpi_match_platform_list(qcom_acpi_platlist) >= 0)
			return qcom_smmu_create(smmu, &qcom_smmu_500_impl0_data);
	}
#endif

	match = of_match_node(qcom_smmu_impl_of_match, np);
	if (match)
		return qcom_smmu_create(smmu, match->data);

	/*
	 * If you hit this WARN_ON() you are missing an entry in the
	 * qcom_smmu_impl_of_match[] table, and GPU per-process page-
	 * tables will be broken.
	 */
	WARN(of_device_is_compatible(np, "qcom,adreno-smmu"),
	     "Missing qcom_smmu_impl_of_match entry for: %s",
	     dev_name(smmu->dev));

	return smmu;
}

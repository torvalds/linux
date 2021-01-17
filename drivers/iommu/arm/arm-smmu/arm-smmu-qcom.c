// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 */

#include <linux/adreno-smmu-priv.h>
#include <linux/bitfield.h>
#include <linux/of_device.h>
#include <linux/qcom_scm.h>

#include "arm-smmu.h"

struct qcom_smmu {
	struct arm_smmu_device smmu;
	bool bypass_quirk;
	u8 bypass_cbndx;
};

static int qcom_sdm845_smmu500_cfg_probe(struct arm_smmu_device *smmu)
{
	u32 s2cr;
	u32 smr;
	int i;

	for (i = 0; i < smmu->num_mapping_groups; i++) {
		smr = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_SMR(i));
		s2cr = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_S2CR(i));

		smmu->smrs[i].mask = FIELD_GET(ARM_SMMU_SMR_MASK, smr);
		smmu->smrs[i].id = FIELD_GET(ARM_SMMU_SMR_ID, smr);
		if (smmu->features & ARM_SMMU_FEAT_EXIDS)
			smmu->smrs[i].valid = FIELD_GET(
						ARM_SMMU_S2CR_EXIDVALID,
						s2cr);
		else
			smmu->smrs[i].valid = FIELD_GET(
						ARM_SMMU_SMR_VALID,
						smr);

		smmu->s2crs[i].group = NULL;
		smmu->s2crs[i].count = 0;
		smmu->s2crs[i].type = FIELD_GET(ARM_SMMU_S2CR_TYPE, s2cr);
		smmu->s2crs[i].privcfg = FIELD_GET(ARM_SMMU_S2CR_PRIVCFG, s2cr);
		smmu->s2crs[i].cbndx = FIELD_GET(ARM_SMMU_S2CR_CBNDX, s2cr);

		if (!smmu->smrs[i].valid)
			continue;

		smmu->s2crs[i].pinned = true;
		bitmap_set(smmu->context_map, smmu->s2crs[i].cbndx, 1);
	}

	return 0;
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

static int qcom_adreno_smmu_init_context(struct arm_smmu_domain *smmu_domain,
		struct io_pgtable_cfg *pgtbl_cfg, struct device *dev)
{
	struct adreno_smmu_priv *priv;

	/* Only enable split pagetables for the GPU device (SID 0) */
	if (!qcom_adreno_smmu_is_gpu_device(dev))
		return 0;

	/*
	 * All targets that use the qcom,adreno-smmu compatible string *should*
	 * be AARCH64 stage 1 but double check because the arm-smmu code assumes
	 * that is the case when the TTBR1 quirk is enabled
	 */
	if ((smmu_domain->stage == ARM_SMMU_DOMAIN_S1) &&
	    (smmu_domain->cfg.fmt == ARM_SMMU_CTX_FMT_AARCH64))
		pgtbl_cfg->quirks |= IO_PGTABLE_QUIRK_ARM_TTBR1;

	/*
	 * Initialize private interface with GPU:
	 */

	priv = dev_get_drvdata(dev);
	priv->cookie = smmu_domain;
	priv->get_ttbr1_cfg = qcom_adreno_smmu_get_ttbr1_cfg;
	priv->set_ttbr0_cfg = qcom_adreno_smmu_set_ttbr0_cfg;

	return 0;
}

static struct qcom_smmu *to_qcom_smmu(struct arm_smmu_device *smmu)
{
	return container_of(smmu, struct qcom_smmu, smmu);
}

static const struct of_device_id qcom_smmu_client_of_match[] __maybe_unused = {
	{ .compatible = "qcom,adreno" },
	{ .compatible = "qcom,mdp4" },
	{ .compatible = "qcom,mdss" },
	{ .compatible = "qcom,sc7180-mdss" },
	{ .compatible = "qcom,sc7180-mss-pil" },
	{ .compatible = "qcom,sdm845-mdss" },
	{ .compatible = "qcom,sdm845-mss-pil" },
	{ }
};

static int qcom_smmu_cfg_probe(struct arm_smmu_device *smmu)
{
	unsigned int last_s2cr = ARM_SMMU_GR0_S2CR(smmu->num_mapping_groups - 1);
	struct qcom_smmu *qsmmu = to_qcom_smmu(smmu);
	u32 reg;
	u32 smr;
	int i;

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
	.cfg_probe = qcom_smmu_cfg_probe,
	.def_domain_type = qcom_smmu_def_domain_type,
	.cfg_probe = qcom_sdm845_smmu500_cfg_probe,
	.reset = qcom_smmu500_reset,
	.write_s2cr = qcom_smmu_write_s2cr,
};

static const struct arm_smmu_impl qcom_adreno_smmu_impl = {
	.init_context = qcom_adreno_smmu_init_context,
	.def_domain_type = qcom_smmu_def_domain_type,
	.reset = qcom_smmu500_reset,
	.alloc_context_bank = qcom_adreno_smmu_alloc_context_bank,
};

static struct arm_smmu_device *qcom_smmu_create(struct arm_smmu_device *smmu,
		const struct arm_smmu_impl *impl)
{
	struct qcom_smmu *qsmmu;

	/* Check to make sure qcom_scm has finished probing */
	if (!qcom_scm_is_available())
		return ERR_PTR(-EPROBE_DEFER);

	qsmmu = devm_kzalloc(smmu->dev, sizeof(*qsmmu), GFP_KERNEL);
	if (!qsmmu)
		return ERR_PTR(-ENOMEM);

	qsmmu->smmu = *smmu;

	qsmmu->smmu.impl = impl;
	devm_kfree(smmu->dev, smmu);

	return &qsmmu->smmu;
}

struct arm_smmu_device *qcom_smmu_impl_init(struct arm_smmu_device *smmu)
{
	return qcom_smmu_create(smmu, &qcom_smmu_impl);
}

struct arm_smmu_device *qcom_adreno_smmu_impl_init(struct arm_smmu_device *smmu)
{
	return qcom_smmu_create(smmu, &qcom_adreno_smmu_impl);
}

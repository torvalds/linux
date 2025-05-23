// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 Rivos Inc.
 *
 * Authors:
 *     Clément Léger <cleger@rivosinc.com>
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/kvm_host.h>
#include <asm/cpufeature.h>
#include <asm/sbi.h>
#include <asm/kvm_vcpu_sbi.h>
#include <asm/kvm_vcpu_sbi_fwft.h>

#define MIS_DELEG (BIT_ULL(EXC_LOAD_MISALIGNED) | BIT_ULL(EXC_STORE_MISALIGNED))

struct kvm_sbi_fwft_feature {
	/**
	 * @id: Feature ID
	 */
	enum sbi_fwft_feature_t id;

	/**
	 * @supported: Check if the feature is supported on the vcpu
	 *
	 * This callback is optional, if not provided the feature is assumed to
	 * be supported
	 */
	bool (*supported)(struct kvm_vcpu *vcpu);

	/**
	 * @set: Set the feature value
	 *
	 * Return SBI_SUCCESS on success or an SBI error (SBI_ERR_*)
	 *
	 * This callback is mandatory
	 */
	long (*set)(struct kvm_vcpu *vcpu, struct kvm_sbi_fwft_config *conf, unsigned long value);

	/**
	 * @get: Get the feature current value
	 *
	 * Return SBI_SUCCESS on success or an SBI error (SBI_ERR_*)
	 *
	 * This callback is mandatory
	 */
	long (*get)(struct kvm_vcpu *vcpu, struct kvm_sbi_fwft_config *conf, unsigned long *value);
};

static const enum sbi_fwft_feature_t kvm_fwft_defined_features[] = {
	SBI_FWFT_MISALIGNED_EXC_DELEG,
	SBI_FWFT_LANDING_PAD,
	SBI_FWFT_SHADOW_STACK,
	SBI_FWFT_DOUBLE_TRAP,
	SBI_FWFT_PTE_AD_HW_UPDATING,
	SBI_FWFT_POINTER_MASKING_PMLEN,
};

static bool kvm_fwft_is_defined_feature(enum sbi_fwft_feature_t feature)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(kvm_fwft_defined_features); i++) {
		if (kvm_fwft_defined_features[i] == feature)
			return true;
	}

	return false;
}

static bool kvm_sbi_fwft_misaligned_delegation_supported(struct kvm_vcpu *vcpu)
{
	return misaligned_traps_can_delegate();
}

static long kvm_sbi_fwft_set_misaligned_delegation(struct kvm_vcpu *vcpu,
					struct kvm_sbi_fwft_config *conf,
					unsigned long value)
{
	struct kvm_vcpu_config *cfg = &vcpu->arch.cfg;

	if (value == 1) {
		cfg->hedeleg |= MIS_DELEG;
		csr_set(CSR_HEDELEG, MIS_DELEG);
	} else if (value == 0) {
		cfg->hedeleg &= ~MIS_DELEG;
		csr_clear(CSR_HEDELEG, MIS_DELEG);
	} else {
		return SBI_ERR_INVALID_PARAM;
	}

	return SBI_SUCCESS;
}

static long kvm_sbi_fwft_get_misaligned_delegation(struct kvm_vcpu *vcpu,
					struct kvm_sbi_fwft_config *conf,
					unsigned long *value)
{
	*value = (csr_read(CSR_HEDELEG) & MIS_DELEG) == MIS_DELEG;

	return SBI_SUCCESS;
}

static const struct kvm_sbi_fwft_feature features[] = {
	{
		.id = SBI_FWFT_MISALIGNED_EXC_DELEG,
		.supported = kvm_sbi_fwft_misaligned_delegation_supported,
		.set = kvm_sbi_fwft_set_misaligned_delegation,
		.get = kvm_sbi_fwft_get_misaligned_delegation,
	},
};

static struct kvm_sbi_fwft_config *
kvm_sbi_fwft_get_config(struct kvm_vcpu *vcpu, enum sbi_fwft_feature_t feature)
{
	int i;
	struct kvm_sbi_fwft *fwft = vcpu_to_fwft(vcpu);

	for (i = 0; i < ARRAY_SIZE(features); i++) {
		if (fwft->configs[i].feature->id == feature)
			return &fwft->configs[i];
	}

	return NULL;
}

static int kvm_fwft_get_feature(struct kvm_vcpu *vcpu, u32 feature,
				struct kvm_sbi_fwft_config **conf)
{
	struct kvm_sbi_fwft_config *tconf;

	tconf = kvm_sbi_fwft_get_config(vcpu, feature);
	if (!tconf) {
		if (kvm_fwft_is_defined_feature(feature))
			return SBI_ERR_NOT_SUPPORTED;

		return SBI_ERR_DENIED;
	}

	if (!tconf->supported)
		return SBI_ERR_NOT_SUPPORTED;

	*conf = tconf;

	return SBI_SUCCESS;
}

static int kvm_sbi_fwft_set(struct kvm_vcpu *vcpu, u32 feature,
			    unsigned long value, unsigned long flags)
{
	int ret;
	struct kvm_sbi_fwft_config *conf;

	ret = kvm_fwft_get_feature(vcpu, feature, &conf);
	if (ret)
		return ret;

	if ((flags & ~SBI_FWFT_SET_FLAG_LOCK) != 0)
		return SBI_ERR_INVALID_PARAM;

	if (conf->flags & SBI_FWFT_SET_FLAG_LOCK)
		return SBI_ERR_DENIED_LOCKED;

	conf->flags = flags;

	return conf->feature->set(vcpu, conf, value);
}

static int kvm_sbi_fwft_get(struct kvm_vcpu *vcpu, unsigned long feature,
			    unsigned long *value)
{
	int ret;
	struct kvm_sbi_fwft_config *conf;

	ret = kvm_fwft_get_feature(vcpu, feature, &conf);
	if (ret)
		return ret;

	return conf->feature->get(vcpu, conf, value);
}

static int kvm_sbi_ext_fwft_handler(struct kvm_vcpu *vcpu, struct kvm_run *run,
				    struct kvm_vcpu_sbi_return *retdata)
{
	int ret;
	struct kvm_cpu_context *cp = &vcpu->arch.guest_context;
	unsigned long funcid = cp->a6;

	switch (funcid) {
	case SBI_EXT_FWFT_SET:
		ret = kvm_sbi_fwft_set(vcpu, cp->a0, cp->a1, cp->a2);
		break;
	case SBI_EXT_FWFT_GET:
		ret = kvm_sbi_fwft_get(vcpu, cp->a0, &retdata->out_val);
		break;
	default:
		ret = SBI_ERR_NOT_SUPPORTED;
		break;
	}

	retdata->err_val = ret;

	return 0;
}

static int kvm_sbi_ext_fwft_init(struct kvm_vcpu *vcpu)
{
	struct kvm_sbi_fwft *fwft = vcpu_to_fwft(vcpu);
	const struct kvm_sbi_fwft_feature *feature;
	struct kvm_sbi_fwft_config *conf;
	int i;

	fwft->configs = kcalloc(ARRAY_SIZE(features), sizeof(struct kvm_sbi_fwft_config),
				GFP_KERNEL);
	if (!fwft->configs)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(features); i++) {
		feature = &features[i];
		conf = &fwft->configs[i];
		if (feature->supported)
			conf->supported = feature->supported(vcpu);
		else
			conf->supported = true;

		conf->feature = feature;
	}

	return 0;
}

static void kvm_sbi_ext_fwft_deinit(struct kvm_vcpu *vcpu)
{
	struct kvm_sbi_fwft *fwft = vcpu_to_fwft(vcpu);

	kfree(fwft->configs);
}

static void kvm_sbi_ext_fwft_reset(struct kvm_vcpu *vcpu)
{
	int i;
	struct kvm_sbi_fwft *fwft = vcpu_to_fwft(vcpu);

	for (i = 0; i < ARRAY_SIZE(features); i++)
		fwft->configs[i].flags = 0;
}

const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_fwft = {
	.extid_start = SBI_EXT_FWFT,
	.extid_end = SBI_EXT_FWFT,
	.handler = kvm_sbi_ext_fwft_handler,
	.init = kvm_sbi_ext_fwft_init,
	.deinit = kvm_sbi_ext_fwft_deinit,
	.reset = kvm_sbi_ext_fwft_reset,
};

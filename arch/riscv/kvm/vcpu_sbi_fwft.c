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
	 * @first_reg_num: ONE_REG index of the first ONE_REG register
	 */
	unsigned long first_reg_num;

	/**
	 * @supported: Check if the feature is supported on the vcpu
	 *
	 * This callback is optional, if not provided the feature is assumed to
	 * be supported
	 */
	bool (*supported)(struct kvm_vcpu *vcpu);

	/**
	 * @reset: Reset the feature value irrespective whether feature is supported or not
	 *
	 * This callback is mandatory
	 */
	void (*reset)(struct kvm_vcpu *vcpu);

	/**
	 * @set: Set the feature value
	 *
	 * Return SBI_SUCCESS on success or an SBI error (SBI_ERR_*)
	 *
	 * This callback is mandatory
	 */
	long (*set)(struct kvm_vcpu *vcpu, struct kvm_sbi_fwft_config *conf,
		    bool one_reg_access, unsigned long value);

	/**
	 * @get: Get the feature current value
	 *
	 * Return SBI_SUCCESS on success or an SBI error (SBI_ERR_*)
	 *
	 * This callback is mandatory
	 */
	long (*get)(struct kvm_vcpu *vcpu, struct kvm_sbi_fwft_config *conf,
		    bool one_reg_access, unsigned long *value);
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

static void kvm_sbi_fwft_reset_misaligned_delegation(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_config *cfg = &vcpu->arch.cfg;

	cfg->hedeleg &= ~MIS_DELEG;
}

static long kvm_sbi_fwft_set_misaligned_delegation(struct kvm_vcpu *vcpu,
					struct kvm_sbi_fwft_config *conf,
					bool one_reg_access, unsigned long value)
{
	struct kvm_vcpu_config *cfg = &vcpu->arch.cfg;

	if (value == 1) {
		cfg->hedeleg |= MIS_DELEG;
		if (!one_reg_access)
			csr_set(CSR_HEDELEG, MIS_DELEG);
	} else if (value == 0) {
		cfg->hedeleg &= ~MIS_DELEG;
		if (!one_reg_access)
			csr_clear(CSR_HEDELEG, MIS_DELEG);
	} else {
		return SBI_ERR_INVALID_PARAM;
	}

	return SBI_SUCCESS;
}

static long kvm_sbi_fwft_get_misaligned_delegation(struct kvm_vcpu *vcpu,
					struct kvm_sbi_fwft_config *conf,
					bool one_reg_access, unsigned long *value)
{
	struct kvm_vcpu_config *cfg = &vcpu->arch.cfg;

	*value = (cfg->hedeleg & MIS_DELEG) == MIS_DELEG;
	return SBI_SUCCESS;
}

#ifndef CONFIG_32BIT

static bool try_to_set_pmm(unsigned long value)
{
	csr_set(CSR_HENVCFG, value);
	return (csr_read_clear(CSR_HENVCFG, ENVCFG_PMM) & ENVCFG_PMM) == value;
}

static bool kvm_sbi_fwft_pointer_masking_pmlen_supported(struct kvm_vcpu *vcpu)
{
	struct kvm_sbi_fwft *fwft = vcpu_to_fwft(vcpu);

	if (!riscv_isa_extension_available(vcpu->arch.isa, SMNPM))
		return false;

	fwft->have_vs_pmlen_7 = try_to_set_pmm(ENVCFG_PMM_PMLEN_7);
	fwft->have_vs_pmlen_16 = try_to_set_pmm(ENVCFG_PMM_PMLEN_16);

	return fwft->have_vs_pmlen_7 || fwft->have_vs_pmlen_16;
}

static void kvm_sbi_fwft_reset_pointer_masking_pmlen(struct kvm_vcpu *vcpu)
{
	vcpu->arch.cfg.henvcfg &= ~ENVCFG_PMM;
}

static long kvm_sbi_fwft_set_pointer_masking_pmlen(struct kvm_vcpu *vcpu,
						   struct kvm_sbi_fwft_config *conf,
						   bool one_reg_access, unsigned long value)
{
	struct kvm_sbi_fwft *fwft = vcpu_to_fwft(vcpu);
	unsigned long pmm;

	switch (value) {
	case 0:
		pmm = ENVCFG_PMM_PMLEN_0;
		break;
	case 7:
		if (!fwft->have_vs_pmlen_7)
			return SBI_ERR_INVALID_PARAM;
		pmm = ENVCFG_PMM_PMLEN_7;
		break;
	case 16:
		if (!fwft->have_vs_pmlen_16)
			return SBI_ERR_INVALID_PARAM;
		pmm = ENVCFG_PMM_PMLEN_16;
		break;
	default:
		return SBI_ERR_INVALID_PARAM;
	}

	vcpu->arch.cfg.henvcfg &= ~ENVCFG_PMM;
	vcpu->arch.cfg.henvcfg |= pmm;

	/*
	 * Instead of waiting for vcpu_load/put() to update HENVCFG CSR,
	 * update here so that VCPU see's pointer masking mode change
	 * immediately.
	 */
	if (!one_reg_access)
		csr_write(CSR_HENVCFG, vcpu->arch.cfg.henvcfg);

	return SBI_SUCCESS;
}

static long kvm_sbi_fwft_get_pointer_masking_pmlen(struct kvm_vcpu *vcpu,
						   struct kvm_sbi_fwft_config *conf,
						   bool one_reg_access, unsigned long *value)
{
	switch (vcpu->arch.cfg.henvcfg & ENVCFG_PMM) {
	case ENVCFG_PMM_PMLEN_0:
		*value = 0;
		break;
	case ENVCFG_PMM_PMLEN_7:
		*value = 7;
		break;
	case ENVCFG_PMM_PMLEN_16:
		*value = 16;
		break;
	default:
		return SBI_ERR_FAILURE;
	}

	return SBI_SUCCESS;
}

#endif

static const struct kvm_sbi_fwft_feature features[] = {
	{
		.id = SBI_FWFT_MISALIGNED_EXC_DELEG,
		.first_reg_num = offsetof(struct kvm_riscv_sbi_fwft, misaligned_deleg.enable) /
				 sizeof(unsigned long),
		.supported = kvm_sbi_fwft_misaligned_delegation_supported,
		.reset = kvm_sbi_fwft_reset_misaligned_delegation,
		.set = kvm_sbi_fwft_set_misaligned_delegation,
		.get = kvm_sbi_fwft_get_misaligned_delegation,
	},
#ifndef CONFIG_32BIT
	{
		.id = SBI_FWFT_POINTER_MASKING_PMLEN,
		.first_reg_num = offsetof(struct kvm_riscv_sbi_fwft, pointer_masking.enable) /
				 sizeof(unsigned long),
		.supported = kvm_sbi_fwft_pointer_masking_pmlen_supported,
		.reset = kvm_sbi_fwft_reset_pointer_masking_pmlen,
		.set = kvm_sbi_fwft_set_pointer_masking_pmlen,
		.get = kvm_sbi_fwft_get_pointer_masking_pmlen,
	},
#endif
};

static const struct kvm_sbi_fwft_feature *kvm_sbi_fwft_regnum_to_feature(unsigned long reg_num)
{
	const struct kvm_sbi_fwft_feature *feature;
	int i;

	for (i = 0; i < ARRAY_SIZE(features); i++) {
		feature = &features[i];
		if (feature->first_reg_num <= reg_num && reg_num < (feature->first_reg_num + 3))
			return feature;
	}

	return NULL;
}

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

	if (!tconf->supported || !tconf->enabled)
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

	return conf->feature->set(vcpu, conf, false, value);
}

static int kvm_sbi_fwft_get(struct kvm_vcpu *vcpu, unsigned long feature,
			    unsigned long *value)
{
	int ret;
	struct kvm_sbi_fwft_config *conf;

	ret = kvm_fwft_get_feature(vcpu, feature, &conf);
	if (ret)
		return ret;

	return conf->feature->get(vcpu, conf, false, value);
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

		conf->enabled = conf->supported;
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
	struct kvm_sbi_fwft *fwft = vcpu_to_fwft(vcpu);
	const struct kvm_sbi_fwft_feature *feature;
	int i;

	for (i = 0; i < ARRAY_SIZE(features); i++) {
		fwft->configs[i].flags = 0;
		feature = &features[i];
		if (feature->reset)
			feature->reset(vcpu);
	}
}

static unsigned long kvm_sbi_ext_fwft_get_reg_count(struct kvm_vcpu *vcpu)
{
	unsigned long max_reg_count = sizeof(struct kvm_riscv_sbi_fwft) / sizeof(unsigned long);
	const struct kvm_sbi_fwft_feature *feature;
	struct kvm_sbi_fwft_config *conf;
	unsigned long reg, ret = 0;

	for (reg = 0; reg < max_reg_count; reg++) {
		feature = kvm_sbi_fwft_regnum_to_feature(reg);
		if (!feature)
			continue;

		conf = kvm_sbi_fwft_get_config(vcpu, feature->id);
		if (!conf || !conf->supported)
			continue;

		ret++;
	}

	return ret;
}

static int kvm_sbi_ext_fwft_get_reg_id(struct kvm_vcpu *vcpu, int index, u64 *reg_id)
{
	int reg, max_reg_count = sizeof(struct kvm_riscv_sbi_fwft) / sizeof(unsigned long);
	const struct kvm_sbi_fwft_feature *feature;
	struct kvm_sbi_fwft_config *conf;
	int idx = 0;

	for (reg = 0; reg < max_reg_count; reg++) {
		feature = kvm_sbi_fwft_regnum_to_feature(reg);
		if (!feature)
			continue;

		conf = kvm_sbi_fwft_get_config(vcpu, feature->id);
		if (!conf || !conf->supported)
			continue;

		if (index == idx) {
			*reg_id = KVM_REG_RISCV |
				  (IS_ENABLED(CONFIG_32BIT) ?
				   KVM_REG_SIZE_U32 : KVM_REG_SIZE_U64) |
				  KVM_REG_RISCV_SBI_STATE |
				  KVM_REG_RISCV_SBI_FWFT | reg;
			return 0;
		}

		idx++;
	}

	return -ENOENT;
}

static int kvm_sbi_ext_fwft_get_reg(struct kvm_vcpu *vcpu, unsigned long reg_num,
				    unsigned long reg_size, void *reg_val)
{
	const struct kvm_sbi_fwft_feature *feature;
	struct kvm_sbi_fwft_config *conf;
	unsigned long *value;
	int ret = 0;

	if (reg_size != sizeof(unsigned long))
		return -EINVAL;
	value = reg_val;

	feature = kvm_sbi_fwft_regnum_to_feature(reg_num);
	if (!feature)
		return -ENOENT;

	conf = kvm_sbi_fwft_get_config(vcpu, feature->id);
	if (!conf || !conf->supported)
		return -ENOENT;

	switch (reg_num - feature->first_reg_num) {
	case 0:
		*value = conf->enabled;
		break;
	case 1:
		*value = conf->flags;
		break;
	case 2:
		ret = conf->feature->get(vcpu, conf, true, value);
		break;
	default:
		return -ENOENT;
	}

	return sbi_err_map_linux_errno(ret);
}

static int kvm_sbi_ext_fwft_set_reg(struct kvm_vcpu *vcpu, unsigned long reg_num,
				    unsigned long reg_size, const void *reg_val)
{
	const struct kvm_sbi_fwft_feature *feature;
	struct kvm_sbi_fwft_config *conf;
	unsigned long value;
	int ret = 0;

	if (reg_size != sizeof(unsigned long))
		return -EINVAL;
	value = *(const unsigned long *)reg_val;

	feature = kvm_sbi_fwft_regnum_to_feature(reg_num);
	if (!feature)
		return -ENOENT;

	conf = kvm_sbi_fwft_get_config(vcpu, feature->id);
	if (!conf || !conf->supported)
		return -ENOENT;

	switch (reg_num - feature->first_reg_num) {
	case 0:
		switch (value) {
		case 0:
			conf->enabled = false;
			break;
		case 1:
			conf->enabled = true;
			break;
		default:
			return -EINVAL;
		}
		break;
	case 1:
		conf->flags = value & SBI_FWFT_SET_FLAG_LOCK;
		break;
	case 2:
		ret = conf->feature->set(vcpu, conf, true, value);
		break;
	default:
		return -ENOENT;
	}

	return sbi_err_map_linux_errno(ret);
}

const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_fwft = {
	.extid_start = SBI_EXT_FWFT,
	.extid_end = SBI_EXT_FWFT,
	.handler = kvm_sbi_ext_fwft_handler,
	.init = kvm_sbi_ext_fwft_init,
	.deinit = kvm_sbi_ext_fwft_deinit,
	.reset = kvm_sbi_ext_fwft_reset,
	.state_reg_subtype = KVM_REG_RISCV_SBI_FWFT,
	.get_state_reg_count = kvm_sbi_ext_fwft_get_reg_count,
	.get_state_reg_id = kvm_sbi_ext_fwft_get_reg_id,
	.get_state_reg = kvm_sbi_ext_fwft_get_reg,
	.set_state_reg = kvm_sbi_ext_fwft_set_reg,
};

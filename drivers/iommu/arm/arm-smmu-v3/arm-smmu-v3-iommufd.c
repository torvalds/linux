// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES
 */

#include <uapi/linux/iommufd.h>

#include "arm-smmu-v3.h"

void *arm_smmu_hw_info(struct device *dev, u32 *length, u32 *type)
{
	struct arm_smmu_master *master = dev_iommu_priv_get(dev);
	struct iommu_hw_info_arm_smmuv3 *info;
	u32 __iomem *base_idr;
	unsigned int i;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	base_idr = master->smmu->base + ARM_SMMU_IDR0;
	for (i = 0; i <= 5; i++)
		info->idr[i] = readl_relaxed(base_idr + i);
	info->iidr = readl_relaxed(master->smmu->base + ARM_SMMU_IIDR);
	info->aidr = readl_relaxed(master->smmu->base + ARM_SMMU_AIDR);

	*length = sizeof(*info);
	*type = IOMMU_HW_INFO_TYPE_ARM_SMMUV3;

	return info;
}

static void arm_smmu_make_nested_cd_table_ste(
	struct arm_smmu_ste *target, struct arm_smmu_master *master,
	struct arm_smmu_nested_domain *nested_domain, bool ats_enabled)
{
	arm_smmu_make_s2_domain_ste(
		target, master, nested_domain->vsmmu->s2_parent, ats_enabled);

	target->data[0] = cpu_to_le64(STRTAB_STE_0_V |
				      FIELD_PREP(STRTAB_STE_0_CFG,
						 STRTAB_STE_0_CFG_NESTED));
	target->data[0] |= nested_domain->ste[0] &
			   ~cpu_to_le64(STRTAB_STE_0_CFG);
	target->data[1] |= nested_domain->ste[1];
}

/*
 * Create a physical STE from the virtual STE that userspace provided when it
 * created the nested domain. Using the vSTE userspace can request:
 * - Non-valid STE
 * - Abort STE
 * - Bypass STE (install the S2, no CD table)
 * - CD table STE (install the S2 and the userspace CD table)
 */
static void arm_smmu_make_nested_domain_ste(
	struct arm_smmu_ste *target, struct arm_smmu_master *master,
	struct arm_smmu_nested_domain *nested_domain, bool ats_enabled)
{
	unsigned int cfg =
		FIELD_GET(STRTAB_STE_0_CFG, le64_to_cpu(nested_domain->ste[0]));

	/*
	 * Userspace can request a non-valid STE through the nesting interface.
	 * We relay that into an abort physical STE with the intention that
	 * C_BAD_STE for this SID can be generated to userspace.
	 */
	if (!(nested_domain->ste[0] & cpu_to_le64(STRTAB_STE_0_V)))
		cfg = STRTAB_STE_0_CFG_ABORT;

	switch (cfg) {
	case STRTAB_STE_0_CFG_S1_TRANS:
		arm_smmu_make_nested_cd_table_ste(target, master, nested_domain,
						  ats_enabled);
		break;
	case STRTAB_STE_0_CFG_BYPASS:
		arm_smmu_make_s2_domain_ste(target, master,
					    nested_domain->vsmmu->s2_parent,
					    ats_enabled);
		break;
	case STRTAB_STE_0_CFG_ABORT:
	default:
		arm_smmu_make_abort_ste(target);
		break;
	}
}

static int arm_smmu_attach_dev_nested(struct iommu_domain *domain,
				      struct device *dev)
{
	struct arm_smmu_nested_domain *nested_domain =
		to_smmu_nested_domain(domain);
	struct arm_smmu_master *master = dev_iommu_priv_get(dev);
	struct arm_smmu_attach_state state = {
		.master = master,
		.old_domain = iommu_get_domain_for_dev(dev),
		.ssid = IOMMU_NO_PASID,
	};
	struct arm_smmu_ste ste;
	int ret;

	if (nested_domain->vsmmu->smmu != master->smmu)
		return -EINVAL;
	if (arm_smmu_ssids_in_use(&master->cd_table))
		return -EBUSY;

	mutex_lock(&arm_smmu_asid_lock);
	/*
	 * The VM has to control the actual ATS state at the PCI device because
	 * we forward the invalidations directly from the VM. If the VM doesn't
	 * think ATS is on it will not generate ATC flushes and the ATC will
	 * become incoherent. Since we can't access the actual virtual PCI ATS
	 * config bit here base this off the EATS value in the STE. If the EATS
	 * is set then the VM must generate ATC flushes.
	 */
	state.disable_ats = !nested_domain->enable_ats;
	ret = arm_smmu_attach_prepare(&state, domain);
	if (ret) {
		mutex_unlock(&arm_smmu_asid_lock);
		return ret;
	}

	arm_smmu_make_nested_domain_ste(&ste, master, nested_domain,
					state.ats_enabled);
	arm_smmu_install_ste_for_dev(master, &ste);
	arm_smmu_attach_commit(&state);
	mutex_unlock(&arm_smmu_asid_lock);
	return 0;
}

static void arm_smmu_domain_nested_free(struct iommu_domain *domain)
{
	kfree(to_smmu_nested_domain(domain));
}

static const struct iommu_domain_ops arm_smmu_nested_ops = {
	.attach_dev = arm_smmu_attach_dev_nested,
	.free = arm_smmu_domain_nested_free,
};

static int arm_smmu_validate_vste(struct iommu_hwpt_arm_smmuv3 *arg,
				  bool *enable_ats)
{
	unsigned int eats;
	unsigned int cfg;

	if (!(arg->ste[0] & cpu_to_le64(STRTAB_STE_0_V))) {
		memset(arg->ste, 0, sizeof(arg->ste));
		return 0;
	}

	/* EIO is reserved for invalid STE data. */
	if ((arg->ste[0] & ~STRTAB_STE_0_NESTING_ALLOWED) ||
	    (arg->ste[1] & ~STRTAB_STE_1_NESTING_ALLOWED))
		return -EIO;

	cfg = FIELD_GET(STRTAB_STE_0_CFG, le64_to_cpu(arg->ste[0]));
	if (cfg != STRTAB_STE_0_CFG_ABORT && cfg != STRTAB_STE_0_CFG_BYPASS &&
	    cfg != STRTAB_STE_0_CFG_S1_TRANS)
		return -EIO;

	/*
	 * Only Full ATS or ATS UR is supported
	 * The EATS field will be set by arm_smmu_make_nested_domain_ste()
	 */
	eats = FIELD_GET(STRTAB_STE_1_EATS, le64_to_cpu(arg->ste[1]));
	arg->ste[1] &= ~cpu_to_le64(STRTAB_STE_1_EATS);
	if (eats != STRTAB_STE_1_EATS_ABT && eats != STRTAB_STE_1_EATS_TRANS)
		return -EIO;

	if (cfg == STRTAB_STE_0_CFG_S1_TRANS)
		*enable_ats = (eats == STRTAB_STE_1_EATS_TRANS);
	return 0;
}

static struct iommu_domain *
arm_vsmmu_alloc_domain_nested(struct iommufd_viommu *viommu, u32 flags,
			      const struct iommu_user_data *user_data)
{
	struct arm_vsmmu *vsmmu = container_of(viommu, struct arm_vsmmu, core);
	const u32 SUPPORTED_FLAGS = IOMMU_HWPT_FAULT_ID_VALID;
	struct arm_smmu_nested_domain *nested_domain;
	struct iommu_hwpt_arm_smmuv3 arg;
	bool enable_ats = false;
	int ret;

	/*
	 * Faults delivered to the nested domain are faults that originated by
	 * the S1 in the domain. The core code will match all PASIDs when
	 * delivering the fault due to user_pasid_table
	 */
	if (flags & ~SUPPORTED_FLAGS)
		return ERR_PTR(-EOPNOTSUPP);

	ret = iommu_copy_struct_from_user(&arg, user_data,
					  IOMMU_HWPT_DATA_ARM_SMMUV3, ste);
	if (ret)
		return ERR_PTR(ret);

	ret = arm_smmu_validate_vste(&arg, &enable_ats);
	if (ret)
		return ERR_PTR(ret);

	nested_domain = kzalloc(sizeof(*nested_domain), GFP_KERNEL_ACCOUNT);
	if (!nested_domain)
		return ERR_PTR(-ENOMEM);

	nested_domain->domain.type = IOMMU_DOMAIN_NESTED;
	nested_domain->domain.ops = &arm_smmu_nested_ops;
	nested_domain->enable_ats = enable_ats;
	nested_domain->vsmmu = vsmmu;
	nested_domain->ste[0] = arg.ste[0];
	nested_domain->ste[1] = arg.ste[1] & ~cpu_to_le64(STRTAB_STE_1_EATS);

	return &nested_domain->domain;
}

static const struct iommufd_viommu_ops arm_vsmmu_ops = {
	.alloc_domain_nested = arm_vsmmu_alloc_domain_nested,
};

struct iommufd_viommu *arm_vsmmu_alloc(struct device *dev,
				       struct iommu_domain *parent,
				       struct iommufd_ctx *ictx,
				       unsigned int viommu_type)
{
	struct arm_smmu_device *smmu =
		iommu_get_iommu_dev(dev, struct arm_smmu_device, iommu);
	struct arm_smmu_master *master = dev_iommu_priv_get(dev);
	struct arm_smmu_domain *s2_parent = to_smmu_domain(parent);
	struct arm_vsmmu *vsmmu;

	if (viommu_type != IOMMU_VIOMMU_TYPE_ARM_SMMUV3)
		return ERR_PTR(-EOPNOTSUPP);

	if (!(smmu->features & ARM_SMMU_FEAT_NESTING))
		return ERR_PTR(-EOPNOTSUPP);

	if (s2_parent->smmu != master->smmu)
		return ERR_PTR(-EINVAL);

	/*
	 * Must support some way to prevent the VM from bypassing the cache
	 * because VFIO currently does not do any cache maintenance. canwbs
	 * indicates the device is fully coherent and no cache maintenance is
	 * ever required, even for PCI No-Snoop. S2FWB means the S1 can't make
	 * things non-coherent using the memattr, but No-Snoop behavior is not
	 * effected.
	 */
	if (!arm_smmu_master_canwbs(master) &&
	    !(smmu->features & ARM_SMMU_FEAT_S2FWB))
		return ERR_PTR(-EOPNOTSUPP);

	vsmmu = iommufd_viommu_alloc(ictx, struct arm_vsmmu, core,
				     &arm_vsmmu_ops);
	if (IS_ERR(vsmmu))
		return ERR_CAST(vsmmu);

	vsmmu->smmu = smmu;
	vsmmu->s2_parent = s2_parent;
	/* FIXME Move VMID allocation from the S2 domain allocation to here */
	vsmmu->vmid = s2_parent->s2_cfg.vmid;

	return &vsmmu->core;
}

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
	/* Merge events for DoS mitigations on eventq */
	target->data[1] |= cpu_to_le64(STRTAB_STE_1_MEV);
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

int arm_smmu_attach_prepare_vmaster(struct arm_smmu_attach_state *state,
				    struct arm_smmu_nested_domain *nested_domain)
{
	struct arm_smmu_vmaster *vmaster;
	unsigned long vsid;
	int ret;

	iommu_group_mutex_assert(state->master->dev);

	ret = iommufd_viommu_get_vdev_id(&nested_domain->vsmmu->core,
					 state->master->dev, &vsid);
	if (ret)
		return ret;

	vmaster = kzalloc(sizeof(*vmaster), GFP_KERNEL);
	if (!vmaster)
		return -ENOMEM;
	vmaster->vsmmu = nested_domain->vsmmu;
	vmaster->vsid = vsid;
	state->vmaster = vmaster;

	return 0;
}

void arm_smmu_attach_commit_vmaster(struct arm_smmu_attach_state *state)
{
	struct arm_smmu_master *master = state->master;

	mutex_lock(&master->smmu->streams_mutex);
	kfree(master->vmaster);
	master->vmaster = state->vmaster;
	mutex_unlock(&master->smmu->streams_mutex);
}

void arm_smmu_master_clear_vmaster(struct arm_smmu_master *master)
{
	struct arm_smmu_attach_state state = { .master = master };

	arm_smmu_attach_commit_vmaster(&state);
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
	struct arm_smmu_nested_domain *nested_domain;
	struct iommu_hwpt_arm_smmuv3 arg;
	bool enable_ats = false;
	int ret;

	if (flags)
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

static int arm_vsmmu_vsid_to_sid(struct arm_vsmmu *vsmmu, u32 vsid, u32 *sid)
{
	struct arm_smmu_master *master;
	struct device *dev;
	int ret = 0;

	xa_lock(&vsmmu->core.vdevs);
	dev = iommufd_viommu_find_dev(&vsmmu->core, (unsigned long)vsid);
	if (!dev) {
		ret = -EIO;
		goto unlock;
	}
	master = dev_iommu_priv_get(dev);

	/* At this moment, iommufd only supports PCI device that has one SID */
	if (sid)
		*sid = master->streams[0].id;
unlock:
	xa_unlock(&vsmmu->core.vdevs);
	return ret;
}

/* This is basically iommu_viommu_arm_smmuv3_invalidate in u64 for conversion */
struct arm_vsmmu_invalidation_cmd {
	union {
		u64 cmd[2];
		struct iommu_viommu_arm_smmuv3_invalidate ucmd;
	};
};

/*
 * Convert, in place, the raw invalidation command into an internal format that
 * can be passed to arm_smmu_cmdq_issue_cmdlist(). Internally commands are
 * stored in CPU endian.
 *
 * Enforce the VMID or SID on the command.
 */
static int arm_vsmmu_convert_user_cmd(struct arm_vsmmu *vsmmu,
				      struct arm_vsmmu_invalidation_cmd *cmd)
{
	/* Commands are le64 stored in u64 */
	cmd->cmd[0] = le64_to_cpu(cmd->ucmd.cmd[0]);
	cmd->cmd[1] = le64_to_cpu(cmd->ucmd.cmd[1]);

	switch (cmd->cmd[0] & CMDQ_0_OP) {
	case CMDQ_OP_TLBI_NSNH_ALL:
		/* Convert to NH_ALL */
		cmd->cmd[0] = CMDQ_OP_TLBI_NH_ALL |
			      FIELD_PREP(CMDQ_TLBI_0_VMID, vsmmu->vmid);
		cmd->cmd[1] = 0;
		break;
	case CMDQ_OP_TLBI_NH_VA:
	case CMDQ_OP_TLBI_NH_VAA:
	case CMDQ_OP_TLBI_NH_ALL:
	case CMDQ_OP_TLBI_NH_ASID:
		cmd->cmd[0] &= ~CMDQ_TLBI_0_VMID;
		cmd->cmd[0] |= FIELD_PREP(CMDQ_TLBI_0_VMID, vsmmu->vmid);
		break;
	case CMDQ_OP_ATC_INV:
	case CMDQ_OP_CFGI_CD:
	case CMDQ_OP_CFGI_CD_ALL: {
		u32 sid, vsid = FIELD_GET(CMDQ_CFGI_0_SID, cmd->cmd[0]);

		if (arm_vsmmu_vsid_to_sid(vsmmu, vsid, &sid))
			return -EIO;
		cmd->cmd[0] &= ~CMDQ_CFGI_0_SID;
		cmd->cmd[0] |= FIELD_PREP(CMDQ_CFGI_0_SID, sid);
		break;
	}
	default:
		return -EIO;
	}
	return 0;
}

static int arm_vsmmu_cache_invalidate(struct iommufd_viommu *viommu,
				      struct iommu_user_data_array *array)
{
	struct arm_vsmmu *vsmmu = container_of(viommu, struct arm_vsmmu, core);
	struct arm_smmu_device *smmu = vsmmu->smmu;
	struct arm_vsmmu_invalidation_cmd *last;
	struct arm_vsmmu_invalidation_cmd *cmds;
	struct arm_vsmmu_invalidation_cmd *cur;
	struct arm_vsmmu_invalidation_cmd *end;
	int ret;

	cmds = kcalloc(array->entry_num, sizeof(*cmds), GFP_KERNEL);
	if (!cmds)
		return -ENOMEM;
	cur = cmds;
	end = cmds + array->entry_num;

	static_assert(sizeof(*cmds) == 2 * sizeof(u64));
	ret = iommu_copy_struct_from_full_user_array(
		cmds, sizeof(*cmds), array,
		IOMMU_VIOMMU_INVALIDATE_DATA_ARM_SMMUV3);
	if (ret)
		goto out;

	last = cmds;
	while (cur != end) {
		ret = arm_vsmmu_convert_user_cmd(vsmmu, cur);
		if (ret)
			goto out;

		/* FIXME work in blocks of CMDQ_BATCH_ENTRIES and copy each block? */
		cur++;
		if (cur != end && (cur - last) != CMDQ_BATCH_ENTRIES - 1)
			continue;

		/* FIXME always uses the main cmdq rather than trying to group by type */
		ret = arm_smmu_cmdq_issue_cmdlist(smmu, &smmu->cmdq, last->cmd,
						  cur - last, true);
		if (ret) {
			cur--;
			goto out;
		}
		last = cur;
	}
out:
	array->entry_num = cur - cmds;
	kfree(cmds);
	return ret;
}

static const struct iommufd_viommu_ops arm_vsmmu_ops = {
	.alloc_domain_nested = arm_vsmmu_alloc_domain_nested,
	.cache_invalidate = arm_vsmmu_cache_invalidate,
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
	 * FORCE_SYNC is not set with FEAT_NESTING. Some study of the exact HW
	 * defect is needed to determine if arm_vsmmu_cache_invalidate() needs
	 * any change to remove this.
	 */
	if (WARN_ON(smmu->options & ARM_SMMU_OPT_CMDQ_FORCE_SYNC))
		return ERR_PTR(-EOPNOTSUPP);

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

int arm_vmaster_report_event(struct arm_smmu_vmaster *vmaster, u64 *evt)
{
	struct iommu_vevent_arm_smmuv3 vevt;
	int i;

	lockdep_assert_held(&vmaster->vsmmu->smmu->streams_mutex);

	vevt.evt[0] = cpu_to_le64((evt[0] & ~EVTQ_0_SID) |
				  FIELD_PREP(EVTQ_0_SID, vmaster->vsid));
	for (i = 1; i < EVTQ_ENT_DWORDS; i++)
		vevt.evt[i] = cpu_to_le64(evt[i]);

	return iommufd_viommu_report_event(&vmaster->vsmmu->core,
					   IOMMU_VEVENTQ_TYPE_ARM_SMMUV3, &vevt,
					   sizeof(vevt));
}

MODULE_IMPORT_NS("IOMMUFD");

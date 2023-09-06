// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/arm-smccc.h>
#include <linux/gunyah_rsc_mgr.h>
#include <linux/module.h>
#include <linux/qcom_scm.h>
#include <linux/types.h>
#include <linux/uuid.h>
#include <linux/mm.h>

#define QCOM_SCM_RM_MANAGED_VMID	0x3A
#define QCOM_SCM_MAX_MANAGED_VMID	0x3F

static void qcom_scm_gh_pin_pages(phys_addr_t phys_addr, size_t size)
{
	struct page *page = pfn_to_page(__phys_to_pfn(phys_addr));

	while (size) {
		get_page(page++);
		size -= PAGE_SIZE;
	}
}

static int qcom_scm_gh_rm_pre_mem_share(void *rm, struct gh_rm_mem_parcel *mem_parcel)
{
	struct qcom_scm_vmperm *new_perms;
	u64 src, src_cpy;
	int ret = 0, i, n, rb_ret;
	u16 vmid;

	new_perms = kcalloc(mem_parcel->n_acl_entries, sizeof(*new_perms), GFP_KERNEL);
	if (!new_perms)
		return -ENOMEM;

	for (n = 0; n < mem_parcel->n_acl_entries; n++) {
		vmid = le16_to_cpu(mem_parcel->acl_entries[n].vmid);
		if (vmid <= QCOM_SCM_MAX_MANAGED_VMID)
			new_perms[n].vmid = vmid;
		else
			new_perms[n].vmid = QCOM_SCM_RM_MANAGED_VMID;
		if (mem_parcel->acl_entries[n].perms & GH_RM_ACL_X)
			new_perms[n].perm |= QCOM_SCM_PERM_EXEC;
		if (mem_parcel->acl_entries[n].perms & GH_RM_ACL_W)
			new_perms[n].perm |= QCOM_SCM_PERM_WRITE;
		if (mem_parcel->acl_entries[n].perms & GH_RM_ACL_R)
			new_perms[n].perm |= QCOM_SCM_PERM_READ;
	}

	src = BIT_ULL(QCOM_SCM_VMID_HLOS);

	for (i = 0; i < mem_parcel->n_mem_entries; i++) {
		src_cpy = src;
		ret = qcom_scm_assign_mem(le64_to_cpu(mem_parcel->mem_entries[i].phys_addr),
						le64_to_cpu(mem_parcel->mem_entries[i].size),
						&src_cpy, new_perms, mem_parcel->n_acl_entries);
		if (ret)
			break;
	}

	if (!ret)
		goto out;

	src = 0;
	for (n = 0; n < mem_parcel->n_acl_entries; n++) {
		vmid = le16_to_cpu(mem_parcel->acl_entries[n].vmid);
		if (vmid <= QCOM_SCM_MAX_MANAGED_VMID)
			src |= BIT_ULL(vmid);
		else
			src |= BIT_ULL(QCOM_SCM_RM_MANAGED_VMID);
	}

	new_perms[0].vmid = QCOM_SCM_VMID_HLOS;

	for (i--; i >= 0; i--) {
		src_cpy = src;
		rb_ret = qcom_scm_assign_mem(
				le64_to_cpu(mem_parcel->mem_entries[i].phys_addr),
				le64_to_cpu(mem_parcel->mem_entries[i].size),
				&src_cpy, new_perms, 1);
		WARN_ON_ONCE(rb_ret);
		if (rb_ret)
			/*
			 * We have failed to assign pages back to the host.
			 * Keep the pages pinned forever as they shouldn't be
			 * accessed by host.
			 */
			qcom_scm_gh_pin_pages(
				le64_to_cpu(mem_parcel->mem_entries[i].phys_addr),
				le64_to_cpu(mem_parcel->mem_entries[i].size));
	}

out:
	kfree(new_perms);
	return ret;
}

static int qcom_scm_gh_rm_post_mem_reclaim(void *rm, struct gh_rm_mem_parcel *mem_parcel)
{
	struct qcom_scm_vmperm new_perms;
	u64 src = 0, src_cpy;
	int ret = 0, i, n;
	u16 vmid;

	new_perms.vmid = QCOM_SCM_VMID_HLOS;
	new_perms.perm = QCOM_SCM_PERM_EXEC | QCOM_SCM_PERM_WRITE | QCOM_SCM_PERM_READ;

	for (n = 0; n < mem_parcel->n_acl_entries; n++) {
		vmid = le16_to_cpu(mem_parcel->acl_entries[n].vmid);
		if (vmid <= QCOM_SCM_MAX_MANAGED_VMID)
			src |= (1ull << vmid);
		else
			src |= (1ull << QCOM_SCM_RM_MANAGED_VMID);
	}

	for (i = 0; i < mem_parcel->n_mem_entries; i++) {
		src_cpy = src;
		ret = qcom_scm_assign_mem(le64_to_cpu(mem_parcel->mem_entries[i].phys_addr),
						le64_to_cpu(mem_parcel->mem_entries[i].size),
						&src_cpy, &new_perms, 1);
		WARN_ON_ONCE(ret);
		if (ret)
			/*
			 * We have failed to assign pages back to the host.
			 * Keep the pages pinned forever as they shouldn't be
			 * accessed by host.
			 */
			qcom_scm_gh_pin_pages(
				le64_to_cpu(mem_parcel->mem_entries[i].phys_addr),
				le64_to_cpu(mem_parcel->mem_entries[i].size));
	}

	return ret;
}

static struct gh_rm_platform_ops qcom_scm_gh_rm_platform_ops = {
	.pre_mem_share = qcom_scm_gh_rm_pre_mem_share,
	.post_mem_reclaim = qcom_scm_gh_rm_post_mem_reclaim,
};

/* {19bd54bd-0b37-571b-946f-609b54539de6} */
static const uuid_t QCOM_EXT_UUID =
	UUID_INIT(0x19bd54bd, 0x0b37, 0x571b, 0x94, 0x6f, 0x60, 0x9b, 0x54, 0x53, 0x9d, 0xe6);

#define GH_QCOM_EXT_CALL_UUID_ID	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_32, \
							   ARM_SMCCC_OWNER_VENDOR_HYP, 0x3f01)

static bool gh_has_qcom_extensions(void)
{
	struct arm_smccc_res res;
	uuid_t uuid;
	u32 *up;

	arm_smccc_1_1_smc(GH_QCOM_EXT_CALL_UUID_ID, &res);

	up = (u32 *)&uuid.b[0];
	up[0] = lower_32_bits(res.a0);
	up[1] = lower_32_bits(res.a1);
	up[2] = lower_32_bits(res.a2);
	up[3] = lower_32_bits(res.a3);

	return uuid_equal(&uuid, &QCOM_EXT_UUID);
}

static int __init qcom_gh_platform_hooks_register(void)
{
	if (!gh_has_qcom_extensions())
		return -ENODEV;

	return gh_rm_register_platform_ops(&qcom_scm_gh_rm_platform_ops);
}

static void __exit qcom_gh_platform_hooks_unregister(void)
{
	gh_rm_unregister_platform_ops(&qcom_scm_gh_rm_platform_ops);
}

module_init(qcom_gh_platform_hooks_register);
module_exit(qcom_gh_platform_hooks_unregister);
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Platform Hooks for Gunyah");
MODULE_LICENSE("GPL");

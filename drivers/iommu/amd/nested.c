// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */

#define dev_fmt(fmt)	"AMD-Vi: " fmt

#include <linux/iommu.h>
#include <uapi/linux/iommufd.h>

#include "amd_iommu.h"

static const struct iommu_domain_ops nested_domain_ops;

static inline struct nested_domain *to_ndomain(struct iommu_domain *dom)
{
	return container_of(dom, struct nested_domain, domain);
}

/*
 * Validate guest DTE to make sure that configuration for host (v1)
 * and guest (v2) page tables are valid when allocating nested domain.
 */
static int validate_gdte_nested(struct iommu_hwpt_amd_guest *gdte)
{
	u32 gpt_level = FIELD_GET(DTE_GPT_LEVEL_MASK, gdte->dte[2]);

	/* Must be zero: Mode, Host-TPR */
	if (FIELD_GET(DTE_MODE_MASK, gdte->dte[0]) != 0 ||
	    FIELD_GET(DTE_HOST_TRP, gdte->dte[0]) != 0)
		return -EINVAL;

	/* GCR3 TRP must be non-zero if V, GV is set */
	if (FIELD_GET(DTE_FLAG_V, gdte->dte[0]) == 1 &&
	    FIELD_GET(DTE_FLAG_GV, gdte->dte[0]) == 1 &&
	    FIELD_GET(DTE_GCR3_14_12, gdte->dte[0]) == 0 &&
	    FIELD_GET(DTE_GCR3_30_15, gdte->dte[1]) == 0 &&
	    FIELD_GET(DTE_GCR3_51_31, gdte->dte[1]) == 0)
		return -EINVAL;

	/* Valid Guest Paging Mode values are 0 and 1 */
	if (gpt_level != GUEST_PGTABLE_4_LEVEL &&
	    gpt_level != GUEST_PGTABLE_5_LEVEL)
		return -EINVAL;

	/* GLX = 3 is reserved */
	if (FIELD_GET(DTE_GLX, gdte->dte[0]) == 3)
		return -EINVAL;

	/*
	 * We need to check host capability before setting
	 * the Guest Paging Mode
	 */
	if (gpt_level == GUEST_PGTABLE_5_LEVEL &&
	    amd_iommu_gpt_level < PAGE_MODE_5_LEVEL)
		return -EOPNOTSUPP;

	return 0;
}

/*
 * This function is assigned to struct iommufd_viommu_ops.alloc_domain_nested()
 * during the call to struct iommu_ops.viommu_init().
 */
struct iommu_domain *
amd_iommu_alloc_domain_nested(struct iommufd_viommu *viommu, u32 flags,
			      const struct iommu_user_data *user_data)
{
	int ret;
	struct nested_domain *ndom;
	struct amd_iommu_viommu *aviommu = container_of(viommu, struct amd_iommu_viommu, core);

	if (user_data->type != IOMMU_HWPT_DATA_AMD_GUEST)
		return ERR_PTR(-EOPNOTSUPP);

	ndom = kzalloc(sizeof(*ndom), GFP_KERNEL);
	if (!ndom)
		return ERR_PTR(-ENOMEM);

	ret = iommu_copy_struct_from_user(&ndom->gdte, user_data,
					  IOMMU_HWPT_DATA_AMD_GUEST,
					  dte);
	if (ret)
		goto out_err;

	ret = validate_gdte_nested(&ndom->gdte);
	if (ret)
		goto out_err;

	ndom->gdom_id = FIELD_GET(DTE_DOMID_MASK, ndom->gdte.dte[1]);
	ndom->domain.ops = &nested_domain_ops;
	ndom->domain.type = IOMMU_DOMAIN_NESTED;
	ndom->viommu = aviommu;

	return &ndom->domain;
out_err:
	kfree(ndom);
	return ERR_PTR(ret);
}

static void nested_domain_free(struct iommu_domain *dom)
{
	struct nested_domain *ndom = to_ndomain(dom);

	kfree(ndom);
}

static const struct iommu_domain_ops nested_domain_ops = {
	.free = nested_domain_free,
};

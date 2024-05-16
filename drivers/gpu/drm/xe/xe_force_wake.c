// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_force_wake.h"

#include <drm/drm_util.h>

#include "regs/xe_gt_regs.h"
#include "regs/xe_reg_defs.h"
#include "xe_gt.h"
#include "xe_mmio.h"

#define XE_FORCE_WAKE_ACK_TIMEOUT_MS	50

static struct xe_gt *
fw_to_gt(struct xe_force_wake *fw)
{
	return fw->gt;
}

static struct xe_device *
fw_to_xe(struct xe_force_wake *fw)
{
	return gt_to_xe(fw_to_gt(fw));
}

static void domain_init(struct xe_force_wake_domain *domain,
			enum xe_force_wake_domain_id id,
			struct xe_reg reg, struct xe_reg ack, u32 val, u32 mask)
{
	domain->id = id;
	domain->reg_ctl = reg;
	domain->reg_ack = ack;
	domain->val = val;
	domain->mask = mask;
}

void xe_force_wake_init_gt(struct xe_gt *gt, struct xe_force_wake *fw)
{
	struct xe_device *xe = gt_to_xe(gt);

	fw->gt = gt;
	spin_lock_init(&fw->lock);

	/* Assuming gen11+ so assert this assumption is correct */
	xe_gt_assert(gt, GRAPHICS_VER(gt_to_xe(gt)) >= 11);

	if (xe->info.graphics_verx100 >= 1270) {
		domain_init(&fw->domains[XE_FW_DOMAIN_ID_GT],
			    XE_FW_DOMAIN_ID_GT,
			    FORCEWAKE_GT,
			    FORCEWAKE_ACK_GT_MTL,
			    BIT(0), BIT(16));
	} else {
		domain_init(&fw->domains[XE_FW_DOMAIN_ID_GT],
			    XE_FW_DOMAIN_ID_GT,
			    FORCEWAKE_GT,
			    FORCEWAKE_ACK_GT,
			    BIT(0), BIT(16));
	}
}

void xe_force_wake_init_engines(struct xe_gt *gt, struct xe_force_wake *fw)
{
	int i, j;

	/* Assuming gen11+ so assert this assumption is correct */
	xe_gt_assert(gt, GRAPHICS_VER(gt_to_xe(gt)) >= 11);

	if (!xe_gt_is_media_type(gt))
		domain_init(&fw->domains[XE_FW_DOMAIN_ID_RENDER],
			    XE_FW_DOMAIN_ID_RENDER,
			    FORCEWAKE_RENDER,
			    FORCEWAKE_ACK_RENDER,
			    BIT(0), BIT(16));

	for (i = XE_HW_ENGINE_VCS0, j = 0; i <= XE_HW_ENGINE_VCS7; ++i, ++j) {
		if (!(gt->info.engine_mask & BIT(i)))
			continue;

		domain_init(&fw->domains[XE_FW_DOMAIN_ID_MEDIA_VDBOX0 + j],
			    XE_FW_DOMAIN_ID_MEDIA_VDBOX0 + j,
			    FORCEWAKE_MEDIA_VDBOX(j),
			    FORCEWAKE_ACK_MEDIA_VDBOX(j),
			    BIT(0), BIT(16));
	}

	for (i = XE_HW_ENGINE_VECS0, j = 0; i <= XE_HW_ENGINE_VECS3; ++i, ++j) {
		if (!(gt->info.engine_mask & BIT(i)))
			continue;

		domain_init(&fw->domains[XE_FW_DOMAIN_ID_MEDIA_VEBOX0 + j],
			    XE_FW_DOMAIN_ID_MEDIA_VEBOX0 + j,
			    FORCEWAKE_MEDIA_VEBOX(j),
			    FORCEWAKE_ACK_MEDIA_VEBOX(j),
			    BIT(0), BIT(16));
	}

	if (gt->info.engine_mask & BIT(XE_HW_ENGINE_GSCCS0))
		domain_init(&fw->domains[XE_FW_DOMAIN_ID_GSC],
			    XE_FW_DOMAIN_ID_GSC,
			    FORCEWAKE_GSC,
			    FORCEWAKE_ACK_GSC,
			    BIT(0), BIT(16));
}

static void domain_wake(struct xe_gt *gt, struct xe_force_wake_domain *domain)
{
	xe_mmio_write32(gt, domain->reg_ctl, domain->mask | domain->val);
}

static int domain_wake_wait(struct xe_gt *gt,
			    struct xe_force_wake_domain *domain)
{
	return xe_mmio_wait32(gt, domain->reg_ack, domain->val, domain->val,
			      XE_FORCE_WAKE_ACK_TIMEOUT_MS * USEC_PER_MSEC,
			      NULL, true);
}

static void domain_sleep(struct xe_gt *gt, struct xe_force_wake_domain *domain)
{
	xe_mmio_write32(gt, domain->reg_ctl, domain->mask);
}

static int domain_sleep_wait(struct xe_gt *gt,
			     struct xe_force_wake_domain *domain)
{
	return xe_mmio_wait32(gt, domain->reg_ack, domain->val, 0,
			      XE_FORCE_WAKE_ACK_TIMEOUT_MS * USEC_PER_MSEC,
			      NULL, true);
}

#define for_each_fw_domain_masked(domain__, mask__, fw__, tmp__) \
	for (tmp__ = (mask__); tmp__; tmp__ &= ~BIT(ffs(tmp__) - 1)) \
		for_each_if((domain__ = ((fw__)->domains + \
					 (ffs(tmp__) - 1))) && \
					 domain__->reg_ctl.addr)

int xe_force_wake_get(struct xe_force_wake *fw,
		      enum xe_force_wake_domains domains)
{
	struct xe_device *xe = fw_to_xe(fw);
	struct xe_gt *gt = fw_to_gt(fw);
	struct xe_force_wake_domain *domain;
	enum xe_force_wake_domains tmp, woken = 0;
	unsigned long flags;
	int ret, ret2 = 0;

	spin_lock_irqsave(&fw->lock, flags);
	for_each_fw_domain_masked(domain, domains, fw, tmp) {
		if (!domain->ref++) {
			woken |= BIT(domain->id);
			domain_wake(gt, domain);
		}
	}
	for_each_fw_domain_masked(domain, woken, fw, tmp) {
		ret = domain_wake_wait(gt, domain);
		ret2 |= ret;
		if (ret)
			drm_notice(&xe->drm, "Force wake domain (%d) failed to ack wake, ret=%d\n",
				   domain->id, ret);
	}
	fw->awake_domains |= woken;
	spin_unlock_irqrestore(&fw->lock, flags);

	return ret2;
}

int xe_force_wake_put(struct xe_force_wake *fw,
		      enum xe_force_wake_domains domains)
{
	struct xe_device *xe = fw_to_xe(fw);
	struct xe_gt *gt = fw_to_gt(fw);
	struct xe_force_wake_domain *domain;
	enum xe_force_wake_domains tmp, sleep = 0;
	unsigned long flags;
	int ret, ret2 = 0;

	spin_lock_irqsave(&fw->lock, flags);
	for_each_fw_domain_masked(domain, domains, fw, tmp) {
		if (!--domain->ref) {
			sleep |= BIT(domain->id);
			domain_sleep(gt, domain);
		}
	}
	for_each_fw_domain_masked(domain, sleep, fw, tmp) {
		ret = domain_sleep_wait(gt, domain);
		ret2 |= ret;
		if (ret)
			drm_notice(&xe->drm, "Force wake domain (%d) failed to ack sleep, ret=%d\n",
				   domain->id, ret);
	}
	fw->awake_domains &= ~sleep;
	spin_unlock_irqrestore(&fw->lock, flags);

	return ret2;
}

// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_force_wake.h"

#include <drm/drm_util.h>

#include "regs/xe_gt_regs.h"
#include "regs/xe_reg_defs.h"
#include "xe_gt.h"
#include "xe_gt_printk.h"
#include "xe_mmio.h"
#include "xe_sriov.h"

#define XE_FORCE_WAKE_ACK_TIMEOUT_MS	50

static const char *str_wake_sleep(bool wake)
{
	return wake ? "wake" : "sleep";
}

static void domain_init(struct xe_force_wake_domain *domain,
			enum xe_force_wake_domain_id id,
			struct xe_reg reg, struct xe_reg ack)
{
	domain->id = id;
	domain->reg_ctl = reg;
	domain->reg_ack = ack;
	domain->val = FORCEWAKE_MT(FORCEWAKE_KERNEL);
	domain->mask = FORCEWAKE_MT_MASK(FORCEWAKE_KERNEL);
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
			    FORCEWAKE_ACK_GT_MTL);
	} else {
		domain_init(&fw->domains[XE_FW_DOMAIN_ID_GT],
			    XE_FW_DOMAIN_ID_GT,
			    FORCEWAKE_GT,
			    FORCEWAKE_ACK_GT);
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
			    FORCEWAKE_ACK_RENDER);

	for (i = XE_HW_ENGINE_VCS0, j = 0; i <= XE_HW_ENGINE_VCS7; ++i, ++j) {
		if (!(gt->info.engine_mask & BIT(i)))
			continue;

		domain_init(&fw->domains[XE_FW_DOMAIN_ID_MEDIA_VDBOX0 + j],
			    XE_FW_DOMAIN_ID_MEDIA_VDBOX0 + j,
			    FORCEWAKE_MEDIA_VDBOX(j),
			    FORCEWAKE_ACK_MEDIA_VDBOX(j));
	}

	for (i = XE_HW_ENGINE_VECS0, j = 0; i <= XE_HW_ENGINE_VECS3; ++i, ++j) {
		if (!(gt->info.engine_mask & BIT(i)))
			continue;

		domain_init(&fw->domains[XE_FW_DOMAIN_ID_MEDIA_VEBOX0 + j],
			    XE_FW_DOMAIN_ID_MEDIA_VEBOX0 + j,
			    FORCEWAKE_MEDIA_VEBOX(j),
			    FORCEWAKE_ACK_MEDIA_VEBOX(j));
	}

	if (gt->info.engine_mask & BIT(XE_HW_ENGINE_GSCCS0))
		domain_init(&fw->domains[XE_FW_DOMAIN_ID_GSC],
			    XE_FW_DOMAIN_ID_GSC,
			    FORCEWAKE_GSC,
			    FORCEWAKE_ACK_GSC);
}

static void __domain_ctl(struct xe_gt *gt, struct xe_force_wake_domain *domain, bool wake)
{
	if (IS_SRIOV_VF(gt_to_xe(gt)))
		return;

	xe_mmio_write32(gt, domain->reg_ctl, domain->mask | (wake ? domain->val : 0));
}

static int __domain_wait(struct xe_gt *gt, struct xe_force_wake_domain *domain, bool wake)
{
	u32 value;
	int ret;

	if (IS_SRIOV_VF(gt_to_xe(gt)))
		return 0;

	ret = xe_mmio_wait32(gt, domain->reg_ack, domain->val, wake ? domain->val : 0,
			     XE_FORCE_WAKE_ACK_TIMEOUT_MS * USEC_PER_MSEC,
			     &value, true);
	if (ret)
		xe_gt_notice(gt, "Force wake domain %d failed to ack %s (%pe) reg[%#x] = %#x\n",
			     domain->id, str_wake_sleep(wake), ERR_PTR(ret),
			     domain->reg_ack.addr, value);

	return ret;
}

static void domain_wake(struct xe_gt *gt, struct xe_force_wake_domain *domain)
{
	__domain_ctl(gt, domain, true);
}

static int domain_wake_wait(struct xe_gt *gt,
			    struct xe_force_wake_domain *domain)
{
	return __domain_wait(gt, domain, true);
}

static void domain_sleep(struct xe_gt *gt, struct xe_force_wake_domain *domain)
{
	__domain_ctl(gt, domain, false);
}

static int domain_sleep_wait(struct xe_gt *gt,
			     struct xe_force_wake_domain *domain)
{
	return __domain_wait(gt, domain, false);
}

#define for_each_fw_domain_masked(domain__, mask__, fw__, tmp__) \
	for (tmp__ = (mask__); tmp__; tmp__ &= ~BIT(ffs(tmp__) - 1)) \
		for_each_if((domain__ = ((fw__)->domains + \
					 (ffs(tmp__) - 1))) && \
					 domain__->reg_ctl.addr)

int xe_force_wake_get(struct xe_force_wake *fw,
		      enum xe_force_wake_domains domains)
{
	struct xe_gt *gt = fw->gt;
	struct xe_force_wake_domain *domain;
	enum xe_force_wake_domains tmp, woken = 0;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&fw->lock, flags);
	for_each_fw_domain_masked(domain, domains, fw, tmp) {
		if (!domain->ref++) {
			woken |= BIT(domain->id);
			domain_wake(gt, domain);
		}
	}
	for_each_fw_domain_masked(domain, woken, fw, tmp) {
		ret |= domain_wake_wait(gt, domain);
	}
	fw->awake_domains |= woken;
	spin_unlock_irqrestore(&fw->lock, flags);

	return ret;
}

int xe_force_wake_put(struct xe_force_wake *fw,
		      enum xe_force_wake_domains domains)
{
	struct xe_gt *gt = fw->gt;
	struct xe_force_wake_domain *domain;
	enum xe_force_wake_domains tmp, sleep = 0;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&fw->lock, flags);
	for_each_fw_domain_masked(domain, domains, fw, tmp) {
		if (!--domain->ref) {
			sleep |= BIT(domain->id);
			domain_sleep(gt, domain);
		}
	}
	for_each_fw_domain_masked(domain, sleep, fw, tmp) {
		ret |= domain_sleep_wait(gt, domain);
	}
	fw->awake_domains &= ~sleep;
	spin_unlock_irqrestore(&fw->lock, flags);

	return ret;
}

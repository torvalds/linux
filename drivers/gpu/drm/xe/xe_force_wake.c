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

static void mark_domain_initialized(struct xe_force_wake *fw,
				    enum xe_force_wake_domain_id id)
{
	fw->initialized_domains |= BIT(id);
}

static void init_domain(struct xe_force_wake *fw,
			enum xe_force_wake_domain_id id,
			struct xe_reg reg, struct xe_reg ack)
{
	struct xe_force_wake_domain *domain = &fw->domains[id];

	domain->id = id;
	domain->reg_ctl = reg;
	domain->reg_ack = ack;
	domain->val = FORCEWAKE_MT(FORCEWAKE_KERNEL);
	domain->mask = FORCEWAKE_MT_MASK(FORCEWAKE_KERNEL);

	mark_domain_initialized(fw, id);
}

void xe_force_wake_init_gt(struct xe_gt *gt, struct xe_force_wake *fw)
{
	struct xe_device *xe = gt_to_xe(gt);

	fw->gt = gt;
	spin_lock_init(&fw->lock);

	/* Assuming gen11+ so assert this assumption is correct */
	xe_gt_assert(gt, GRAPHICS_VER(gt_to_xe(gt)) >= 11);

	if (xe->info.graphics_verx100 >= 1270) {
		init_domain(fw, XE_FW_DOMAIN_ID_GT,
			    FORCEWAKE_GT,
			    FORCEWAKE_ACK_GT_MTL);
	} else {
		init_domain(fw, XE_FW_DOMAIN_ID_GT,
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
		init_domain(fw, XE_FW_DOMAIN_ID_RENDER,
			    FORCEWAKE_RENDER,
			    FORCEWAKE_ACK_RENDER);

	for (i = XE_HW_ENGINE_VCS0, j = 0; i <= XE_HW_ENGINE_VCS7; ++i, ++j) {
		if (!(gt->info.engine_mask & BIT(i)))
			continue;

		init_domain(fw, XE_FW_DOMAIN_ID_MEDIA_VDBOX0 + j,
			    FORCEWAKE_MEDIA_VDBOX(j),
			    FORCEWAKE_ACK_MEDIA_VDBOX(j));
	}

	for (i = XE_HW_ENGINE_VECS0, j = 0; i <= XE_HW_ENGINE_VECS3; ++i, ++j) {
		if (!(gt->info.engine_mask & BIT(i)))
			continue;

		init_domain(fw, XE_FW_DOMAIN_ID_MEDIA_VEBOX0 + j,
			    FORCEWAKE_MEDIA_VEBOX(j),
			    FORCEWAKE_ACK_MEDIA_VEBOX(j));
	}

	if (gt->info.engine_mask & BIT(XE_HW_ENGINE_GSCCS0))
		init_domain(fw, XE_FW_DOMAIN_ID_GSC,
			    FORCEWAKE_GSC,
			    FORCEWAKE_ACK_GSC);
}

static void __domain_ctl(struct xe_gt *gt, struct xe_force_wake_domain *domain, bool wake)
{
	if (IS_SRIOV_VF(gt_to_xe(gt)))
		return;

	xe_mmio_write32(&gt->mmio, domain->reg_ctl, domain->mask | (wake ? domain->val : 0));
}

static int __domain_wait(struct xe_gt *gt, struct xe_force_wake_domain *domain, bool wake)
{
	u32 value;
	int ret;

	if (IS_SRIOV_VF(gt_to_xe(gt)))
		return 0;

	ret = xe_mmio_wait32(&gt->mmio, domain->reg_ack, domain->val, wake ? domain->val : 0,
			     XE_FORCE_WAKE_ACK_TIMEOUT_MS * USEC_PER_MSEC,
			     &value, true);
	if (ret)
		xe_gt_err(gt, "Force wake domain %d failed to ack %s (%pe) reg[%#x] = %#x\n",
			  domain->id, str_wake_sleep(wake), ERR_PTR(ret),
			  domain->reg_ack.addr, value);
	if (value == ~0) {
		xe_gt_err(gt,
			  "Force wake domain %d: %s. MMIO unreliable (forcewake register returns 0xFFFFFFFF)!\n",
			  domain->id, str_wake_sleep(wake));
		ret = -EIO;
	}

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

/**
 * xe_force_wake_get() : Increase the domain refcount
 * @fw: struct xe_force_wake
 * @domains: forcewake domains to get refcount on
 *
 * This function wakes up @domains if they are asleep and takes references.
 * If requested domain is XE_FORCEWAKE_ALL then only applicable/initialized
 * domains will be considered for refcount and it is a caller responsibility
 * to check returned ref if it includes any specific domain by using
 * xe_force_wake_ref_has_domain() function. Caller must call
 * xe_force_wake_put() function to decrease incremented refcounts.
 *
 * Return: opaque reference to woken domains or zero if none of requested
 * domains were awake.
 */
unsigned int __must_check xe_force_wake_get(struct xe_force_wake *fw,
					    enum xe_force_wake_domains domains)
{
	struct xe_gt *gt = fw->gt;
	struct xe_force_wake_domain *domain;
	unsigned int ref_incr = 0, awake_rqst = 0, awake_failed = 0;
	unsigned int tmp, ref_rqst;
	unsigned long flags;

	xe_gt_assert(gt, is_power_of_2(domains));
	xe_gt_assert(gt, domains <= XE_FORCEWAKE_ALL);
	xe_gt_assert(gt, domains == XE_FORCEWAKE_ALL || fw->initialized_domains & domains);

	ref_rqst = (domains == XE_FORCEWAKE_ALL) ? fw->initialized_domains : domains;
	spin_lock_irqsave(&fw->lock, flags);
	for_each_fw_domain_masked(domain, ref_rqst, fw, tmp) {
		if (!domain->ref++) {
			awake_rqst |= BIT(domain->id);
			domain_wake(gt, domain);
		}
		ref_incr |= BIT(domain->id);
	}
	for_each_fw_domain_masked(domain, awake_rqst, fw, tmp) {
		if (domain_wake_wait(gt, domain) == 0) {
			fw->awake_domains |= BIT(domain->id);
		} else {
			awake_failed |= BIT(domain->id);
			--domain->ref;
		}
	}
	ref_incr &= ~awake_failed;
	spin_unlock_irqrestore(&fw->lock, flags);

	xe_gt_WARN(gt, awake_failed, "Forcewake domain%s %#x failed to acknowledge awake request\n",
		   str_plural(hweight_long(awake_failed)), awake_failed);

	if (domains == XE_FORCEWAKE_ALL && ref_incr == fw->initialized_domains)
		ref_incr |= XE_FORCEWAKE_ALL;

	return ref_incr;
}

/**
 * xe_force_wake_put - Decrement the refcount and put domain to sleep if refcount becomes 0
 * @fw: Pointer to the force wake structure
 * @fw_ref: return of xe_force_wake_get()
 *
 * This function reduces the reference counts for domains in fw_ref. If
 * refcount for any of the specified domain reaches 0, it puts the domain to sleep
 * and waits for acknowledgment for domain to sleep within 50 milisec timeout.
 * Warns in case of timeout of ack from domain.
 */
void xe_force_wake_put(struct xe_force_wake *fw, unsigned int fw_ref)
{
	struct xe_gt *gt = fw->gt;
	struct xe_force_wake_domain *domain;
	unsigned int tmp, sleep = 0;
	unsigned long flags;
	int ack_fail = 0;

	/*
	 * Avoid unnecessary lock and unlock when the function is called
	 * in error path of individual domains.
	 */
	if (!fw_ref)
		return;

	if (xe_force_wake_ref_has_domain(fw_ref, XE_FORCEWAKE_ALL))
		fw_ref = fw->initialized_domains;

	spin_lock_irqsave(&fw->lock, flags);
	for_each_fw_domain_masked(domain, fw_ref, fw, tmp) {
		xe_gt_assert(gt, domain->ref);

		if (!--domain->ref) {
			sleep |= BIT(domain->id);
			domain_sleep(gt, domain);
		}
	}
	for_each_fw_domain_masked(domain, sleep, fw, tmp) {
		if (domain_sleep_wait(gt, domain) == 0)
			fw->awake_domains &= ~BIT(domain->id);
		else
			ack_fail |= BIT(domain->id);
	}
	spin_unlock_irqrestore(&fw->lock, flags);

	xe_gt_WARN(gt, ack_fail, "Forcewake domain%s %#x failed to acknowledge sleep request\n",
		   str_plural(hweight_long(ack_fail)), ack_fail);
}

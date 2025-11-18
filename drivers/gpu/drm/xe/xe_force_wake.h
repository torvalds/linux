/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_FORCE_WAKE_H_
#define _XE_FORCE_WAKE_H_

#include "xe_assert.h"
#include "xe_force_wake_types.h"

struct xe_gt;

void xe_force_wake_init_gt(struct xe_gt *gt,
			   struct xe_force_wake *fw);
void xe_force_wake_init_engines(struct xe_gt *gt,
				struct xe_force_wake *fw);
unsigned int __must_check xe_force_wake_get(struct xe_force_wake *fw,
					    enum xe_force_wake_domains domains);
void xe_force_wake_put(struct xe_force_wake *fw, unsigned int fw_ref);

static inline int
xe_force_wake_ref(struct xe_force_wake *fw,
		  enum xe_force_wake_domains domain)
{
	xe_gt_assert(fw->gt, domain != XE_FORCEWAKE_ALL);
	return fw->domains[ffs(domain) - 1].ref;
}

/**
 * xe_force_wake_assert_held - asserts domain is awake
 * @fw : xe_force_wake structure
 * @domain: xe_force_wake_domains apart from XE_FORCEWAKE_ALL
 *
 * xe_force_wake_assert_held() is designed to confirm a particular
 * forcewake domain's wakefulness; it doesn't verify the wakefulness of
 * multiple domains. Make sure the caller doesn't input multiple
 * domains(XE_FORCEWAKE_ALL) as a parameter.
 */
static inline void
xe_force_wake_assert_held(struct xe_force_wake *fw,
			  enum xe_force_wake_domains domain)
{
	xe_gt_assert(fw->gt, domain != XE_FORCEWAKE_ALL);
	xe_gt_assert(fw->gt, fw->awake_domains & domain);
}

/**
 * xe_force_wake_ref_has_domain - verifies if the domains are in fw_ref
 * @fw_ref : the force_wake reference
 * @domain : forcewake domain to verify
 *
 * This function confirms whether the @fw_ref includes a reference to the
 * specified @domain.
 *
 * Return: true if domain is refcounted.
 */
static inline bool
xe_force_wake_ref_has_domain(unsigned int fw_ref, enum xe_force_wake_domains domain)
{
	return fw_ref & domain;
}

struct xe_force_wake_ref {
	struct xe_force_wake *fw;
	unsigned int domains;
};

static struct xe_force_wake_ref
xe_force_wake_constructor(struct xe_force_wake *fw, unsigned int domains)
{
	struct xe_force_wake_ref fw_ref = { .fw = fw };

	fw_ref.domains = xe_force_wake_get(fw, domains);

	return fw_ref;
}

DEFINE_CLASS(xe_force_wake, struct xe_force_wake_ref,
	     xe_force_wake_put(_T.fw, _T.domains),
	     xe_force_wake_constructor(fw, domains),
	     struct xe_force_wake *fw, unsigned int domains);

/*
 * Scoped helper for the forcewake class, using the same trick as scoped_guard()
 * to bind the lifetime to the next statement/block.
 */
#define __xe_with_force_wake(ref, fw, domains, done) \
	for (CLASS(xe_force_wake, ref)(fw, domains), *(done) = NULL; \
	     !(done); (done) = (void *)1)

#define xe_with_force_wake(ref, fw, domains) \
	__xe_with_force_wake(ref, fw, domains, __UNIQUE_ID(done))

/*
 * Used when xe_force_wake_constructor() has already been called by another
 * function and the current function is responsible for releasing the forcewake
 * reference in all possible cases and error paths.
 */
DEFINE_CLASS(xe_force_wake_release_only, struct xe_force_wake_ref,
	     if (_T.fw) xe_force_wake_put(_T.fw, _T.domains), fw_ref,
	     struct xe_force_wake_ref fw_ref);

#endif

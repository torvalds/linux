/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_FORCE_WAKE_TYPES_H_
#define _XE_FORCE_WAKE_TYPES_H_

#include <linux/mutex.h>
#include <linux/types.h>

#include "regs/xe_reg_defs.h"

enum xe_force_wake_domain_id {
	XE_FW_DOMAIN_ID_GT = 0,
	XE_FW_DOMAIN_ID_RENDER,
	XE_FW_DOMAIN_ID_MEDIA,
	XE_FW_DOMAIN_ID_MEDIA_VDBOX0,
	XE_FW_DOMAIN_ID_MEDIA_VDBOX1,
	XE_FW_DOMAIN_ID_MEDIA_VDBOX2,
	XE_FW_DOMAIN_ID_MEDIA_VDBOX3,
	XE_FW_DOMAIN_ID_MEDIA_VDBOX4,
	XE_FW_DOMAIN_ID_MEDIA_VDBOX5,
	XE_FW_DOMAIN_ID_MEDIA_VDBOX6,
	XE_FW_DOMAIN_ID_MEDIA_VDBOX7,
	XE_FW_DOMAIN_ID_MEDIA_VEBOX0,
	XE_FW_DOMAIN_ID_MEDIA_VEBOX1,
	XE_FW_DOMAIN_ID_MEDIA_VEBOX2,
	XE_FW_DOMAIN_ID_MEDIA_VEBOX3,
	XE_FW_DOMAIN_ID_GSC,
	XE_FW_DOMAIN_ID_COUNT
};

enum xe_force_wake_domains {
	XE_FW_GT		= BIT(XE_FW_DOMAIN_ID_GT),
	XE_FW_RENDER		= BIT(XE_FW_DOMAIN_ID_RENDER),
	XE_FW_MEDIA		= BIT(XE_FW_DOMAIN_ID_MEDIA),
	XE_FW_MEDIA_VDBOX0	= BIT(XE_FW_DOMAIN_ID_MEDIA_VDBOX0),
	XE_FW_MEDIA_VDBOX1	= BIT(XE_FW_DOMAIN_ID_MEDIA_VDBOX1),
	XE_FW_MEDIA_VDBOX2	= BIT(XE_FW_DOMAIN_ID_MEDIA_VDBOX2),
	XE_FW_MEDIA_VDBOX3	= BIT(XE_FW_DOMAIN_ID_MEDIA_VDBOX3),
	XE_FW_MEDIA_VDBOX4	= BIT(XE_FW_DOMAIN_ID_MEDIA_VDBOX4),
	XE_FW_MEDIA_VDBOX5	= BIT(XE_FW_DOMAIN_ID_MEDIA_VDBOX5),
	XE_FW_MEDIA_VDBOX6	= BIT(XE_FW_DOMAIN_ID_MEDIA_VDBOX6),
	XE_FW_MEDIA_VDBOX7	= BIT(XE_FW_DOMAIN_ID_MEDIA_VDBOX7),
	XE_FW_MEDIA_VEBOX0	= BIT(XE_FW_DOMAIN_ID_MEDIA_VEBOX0),
	XE_FW_MEDIA_VEBOX1	= BIT(XE_FW_DOMAIN_ID_MEDIA_VEBOX1),
	XE_FW_MEDIA_VEBOX2	= BIT(XE_FW_DOMAIN_ID_MEDIA_VEBOX2),
	XE_FW_MEDIA_VEBOX3	= BIT(XE_FW_DOMAIN_ID_MEDIA_VEBOX3),
	XE_FW_GSC		= BIT(XE_FW_DOMAIN_ID_GSC),
	XE_FORCEWAKE_ALL	= BIT(XE_FW_DOMAIN_ID_COUNT)
};

/**
 * struct xe_force_wake_domain - Xe force wake power domain
 *
 * Represents an individual device-internal power domain.  The driver must
 * ensure the power domain is awake before accessing registers or other
 * hardware functionality that is part of the power domain.  Since different
 * driver threads may access hardware units simultaneously, a reference count
 * is used to ensure that the domain remains awake as long as any software
 * is using the part of the hardware covered by the power domain.
 *
 * Hardware provides a register interface to allow the driver to request
 * wake/sleep of power domains, although in most cases the actual action of
 * powering the hardware up/down is handled by firmware (and may be subject to
 * requirements and constraints outside of the driver's visibility) so the
 * driver needs to wait for an acknowledgment that a wake request has been
 * acted upon before accessing the parts of the hardware that reside within the
 * power domain.
 */
struct xe_force_wake_domain {
	/** @id: domain force wake id */
	enum xe_force_wake_domain_id id;
	/** @reg_ctl: domain wake control register address */
	struct xe_reg reg_ctl;
	/** @reg_ack: domain ack register address */
	struct xe_reg reg_ack;
	/** @val: domain wake write value */
	u32 val;
	/** @mask: domain mask */
	u32 mask;
	/** @ref: domain reference */
	u32 ref;
};

/**
 * struct xe_force_wake - Xe force wake collection
 *
 * Represents a collection of related power domains (struct
 * xe_force_wake_domain) associated with a subunit of the device.
 *
 * Currently only used for GT power domains (where the term "forcewake" is used
 * in the hardware documentation), although the interface could be extended to
 * power wells in other parts of the hardware in the future.
 */
struct xe_force_wake {
	/** @gt: back pointers to GT */
	struct xe_gt *gt;
	/** @lock: protects everything force wake struct */
	spinlock_t lock;
	/** @awake_domains: mask of all domains awake */
	unsigned int awake_domains;
	/** @initialized_domains: mask of all initialized domains */
	unsigned int initialized_domains;
	/** @domains: force wake domains */
	struct xe_force_wake_domain domains[XE_FW_DOMAIN_ID_COUNT];
};

#endif

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
	XE_FORCEWAKE_ALL	= BIT(XE_FW_DOMAIN_ID_COUNT) - 1
};

/**
 * struct xe_force_wake_domain - XE force wake domains
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
 * struct xe_force_wake - XE force wake
 */
struct xe_force_wake {
	/** @gt: back pointers to GT */
	struct xe_gt *gt;
	/** @lock: protects everything force wake struct */
	spinlock_t lock;
	/** @awake_domains: mask of all domains awake */
	enum xe_force_wake_domains awake_domains;
	/** @domains: force wake domains */
	struct xe_force_wake_domain domains[XE_FW_DOMAIN_ID_COUNT];
};

#endif

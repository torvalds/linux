/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_SESSION_H__
#define __INTEL_PXP_SESSION_H__

#include <linux/types.h>

struct intel_pxp;

int intel_pxp_create_arb_session(struct intel_pxp *pxp);

#endif /* __INTEL_PXP_SESSION_H__ */

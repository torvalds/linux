/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __GH_RM_BOOSTER_H
#define __GH_RM_BOOSTER_H

#include <linux/err.h>
#include <linux/types.h>

#include <linux/gunyah/hcall_common.h>
#include <linux/gunyah/gh_common.h>
#include <asm/gunyah/hcall.h>

static inline int gh_hcall_change_rm_affinity(gh_capid_t cap_id, int cpu)
{
	struct gh_hcall_resp _resp = {0};

	return _gh_hcall(0x603d, (struct gh_hcall_args){ cap_id, cpu, -1 },
			 &_resp);
}

#endif


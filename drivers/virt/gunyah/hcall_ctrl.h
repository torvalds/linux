/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */
#ifndef __GH_HCALL_CTRL_H
#define __GH_HCALL_CTRL_H

#include <linux/err.h>
#include <linux/types.h>

#include <linux/gunyah/hcall_common.h>
#include <linux/gunyah/gh_common.h>
#include <asm/gunyah/hcall.h>

struct gh_hcall_hyp_identify_resp {
	u64 api_info;
	u64 flags[3];
};

static inline int gh_hcall_hyp_identify(struct gh_hcall_hyp_identify_resp *resp)
{
	struct gh_hcall_resp _resp = {0};

	_gh_hcall(0x6000,
		(struct gh_hcall_args){ 0 },
		&_resp);

	if (resp) {
		resp->api_info = _resp.resp0;
		resp->flags[0] = _resp.resp1;
		resp->flags[1] = _resp.resp2;
		resp->flags[2] = _resp.resp3;
	}

	return 0;
}

static inline int gh_hcall_trace_update_class_flags(
		uint64_t set_flags, uint64_t clear_flags,
		uint64_t *new_flags)
{
	int ret;
	struct gh_hcall_resp _resp = {0};

	ret = _gh_hcall(0x603f,
			(struct gh_hcall_args){ set_flags, clear_flags, 0 },
			&_resp);

	if (!ret && new_flags)
		*new_flags = _resp.resp1;

	return ret;
}

#endif

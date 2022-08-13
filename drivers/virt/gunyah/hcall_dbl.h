/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */
#ifndef __GH_HCALL_DBL_H
#define __GH_HCALL_DBL_H

#include <linux/err.h>
#include <linux/types.h>

#include <linux/gunyah/hcall_common.h>
#include <linux/gunyah/gh_common.h>
#include <asm/gunyah/hcall.h>

static inline int gh_hcall_dbl_bind(gh_capid_t dbl_capid, gh_capid_t vic_capid,
				    gh_virq_handle_t virq_info)
{
	struct gh_hcall_resp _resp = {0};

	return _gh_hcall(0x6010,
			 (struct gh_hcall_args){ dbl_capid, vic_capid,
						 virq_info },
			 &_resp);
}

static inline int gh_hcall_dbl_unbind(gh_capid_t dbl_capid)
{
	struct gh_hcall_resp _resp = {0};

	return _gh_hcall(0x6011, (struct gh_hcall_args){ dbl_capid }, &_resp);
}

struct gh_hcall_dbl_send_resp {
	u64 old_flags;
};

static inline int gh_hcall_dbl_send(gh_capid_t dbl_capid,
				    gh_dbl_flags_t new_flags,
				    struct gh_hcall_dbl_send_resp *resp)
{
	int ret;
	struct gh_hcall_resp _resp = {0};

	ret = _gh_hcall(0x6012,
		(struct gh_hcall_args){ dbl_capid, new_flags },
		&_resp);

	if (!ret && resp)
		resp->old_flags = _resp.resp1;

	return ret;
}

struct gh_hcall_dbl_recv_resp {
	u64 old_flags;
};

static inline int gh_hcall_dbl_recv(gh_capid_t dbl_capid,
				    gh_dbl_flags_t clear_flags,
				    struct gh_hcall_dbl_recv_resp *resp)
{
	int ret;
	struct gh_hcall_resp _resp = {0};

	ret = _gh_hcall(0x6013,
		(struct gh_hcall_args){ dbl_capid, clear_flags },
		&_resp);

	if (!ret && resp)
		resp->old_flags = _resp.resp1;

	return ret;
}

static inline int gh_hcall_dbl_reset(gh_capid_t dbl_capid)
{
	struct gh_hcall_resp _resp = {0};

	return _gh_hcall(0x6014, (struct gh_hcall_args){ dbl_capid }, &_resp);
}

static inline int gh_hcall_dbl_mask(gh_capid_t dbl_capid,
				    gh_dbl_flags_t enable_mask,
				    gh_dbl_flags_t ack_mask)
{
	struct gh_hcall_resp _resp = {0};

	return _gh_hcall(0x6015,
			 (struct gh_hcall_args){ dbl_capid, enable_mask,
						 ack_mask },
			 &_resp);
}

#endif

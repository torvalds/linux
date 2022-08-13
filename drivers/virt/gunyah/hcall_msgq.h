/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */
#ifndef __GH_HCALL_MSGQ_H
#define __GH_HCALL_MSGQ_H

#include <linux/err.h>
#include <linux/types.h>

#include <linux/gunyah/hcall_common.h>
#include <linux/gunyah/gh_common.h>
#include <asm/gunyah/hcall.h>

static inline int gh_hcall_msgq_bind_send(gh_capid_t msgq_capid,
					  gh_capid_t vic_capid,
					  gh_virq_handle_t virq_info)
{
	struct gh_hcall_resp _resp = {0};

	return _gh_hcall(0x6017,
			 (struct gh_hcall_args){ msgq_capid, vic_capid,
						 virq_info },
			 &_resp);
}

static inline int gh_hcall_msgq_bind_recv(gh_capid_t msgq_capid,
					  gh_capid_t vic_capid,
					  gh_virq_handle_t virq_info)
{
	struct gh_hcall_resp _resp = {0};

	return _gh_hcall(0x6018,
			 (struct gh_hcall_args){ msgq_capid, vic_capid,
						 virq_info },
			 &_resp);
}

static inline int gh_hcall_msgq_unbind_send(gh_capid_t msgq_capid)
{
	struct gh_hcall_resp _resp = {0};

	return _gh_hcall(0x6019, (struct gh_hcall_args){ msgq_capid }, &_resp);
}

static inline int gh_hcall_msgq_unbind_recv(gh_capid_t msgq_capid)
{
	struct gh_hcall_resp _resp = {0};

	return _gh_hcall(0x601A, (struct gh_hcall_args){ msgq_capid }, &_resp);
}

struct gh_hcall_msgq_send_resp {
	bool not_full;
};

static inline int gh_hcall_msgq_send(gh_capid_t msgq_capid, size_t size,
				     void *data, u64 send_flags,
				     struct gh_hcall_msgq_send_resp *resp)
{
	int ret;
	struct gh_hcall_resp _resp = {0};

	ret = _gh_hcall(0x601B,
		(struct gh_hcall_args){ msgq_capid, size, (unsigned long)data,
					send_flags },
		&_resp);

	if (!ret && resp)
		resp->not_full = _resp.resp1;

	return ret;
}

struct gh_hcall_msgq_recv_resp {
	size_t recv_size;
	bool not_empty;
};

static inline int gh_hcall_msgq_recv(gh_capid_t msgq_capid, void *buffer,
				     size_t max_size,
				     struct gh_hcall_msgq_recv_resp *resp)
{
	int ret;
	struct gh_hcall_resp _resp = {0};

	ret = _gh_hcall(0x601C,
		(struct gh_hcall_args){ msgq_capid, (unsigned long)buffer,
					max_size },
		&_resp);

	if (!ret && resp) {
		resp->recv_size = _resp.resp1;
		resp->not_empty = _resp.resp2;
	}

	return ret;
}

static inline int gh_hcall_msgq_flush(gh_capid_t msgq_capid)
{
	struct gh_hcall_resp _resp = {0};

	return _gh_hcall(0x601D, (struct gh_hcall_args){ msgq_capid }, &_resp);
}

static inline int gh_hcall_msgq_configure_send(gh_capid_t msgq_capid,
					       long not_full_threshold,
					       long not_full_delay)
{
	struct gh_hcall_resp _resp = {0};

	return _gh_hcall(0x601F,
			 (struct gh_hcall_args){ msgq_capid, not_full_threshold,
						 not_full_delay, -1 },
			 &_resp);
}

static inline int gh_hcall_msgq_configure_recv(gh_capid_t msgq_capid,
					       long not_empty_threshold,
					       long not_empty_delay)
{
	struct gh_hcall_resp _resp = {0};

	return _gh_hcall(0x6020,
		(struct gh_hcall_args){ msgq_capid, not_empty_threshold,
					not_empty_delay, -1 },
		&_resp);
}

#endif

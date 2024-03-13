/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __GH_RSC_MGR_PRIV_H
#define __GH_RSC_MGR_PRIV_H

#include <linux/gunyah.h>
#include <linux/gunyah_rsc_mgr.h>
#include <linux/types.h>

struct gh_rm;
int gh_rm_call(void *rsc_mgr, u32 message_id, const void *req_buf, size_t req_buf_size,
		void **resp_buf, size_t *resp_buf_size);

int gh_rm_platform_pre_mem_share(struct gh_rm *rm, struct gh_rm_mem_parcel *mem_parcel);
int gh_rm_platform_post_mem_reclaim(struct gh_rm *rm, struct gh_rm_mem_parcel *mem_parcel);

#endif

/*
 * Copyright (C) 2010 Google, Inc.
 * Author: Dima Zavin <dima@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __MEDIA_VIDEO_TEGRA_AVP_H
#define __MEDIA_VIDEO_TEGRA_AVP_H

#include <linux/platform_device.h>
#include <linux/types.h>

#include "trpc.h"

enum {
	AVP_DBG_TRACE_XPC	= 1U << 0,
	AVP_DBG_TRACE_XPC_IRQ	= 1U << 1,
	AVP_DBG_TRACE_XPC_MSG	= 1U << 2,
	AVP_DBG_TRACE_XPC_CONN	= 1U << 3,
	AVP_DBG_TRACE_SVC	= 1U << 4,
	AVP_DBG_TRACE_TRPC_MSG	= 1U << 5,
	AVP_DBG_TRACE_TRPC_CONN	= 1U << 6,
	AVP_DBG_TRACE_LIB	= 1U << 7,
};

extern u32 avp_debug_mask;
#define DBG(flag, args...) \
	do { if (unlikely(avp_debug_mask & (flag))) pr_info(args); } while (0)

struct avp_svc_info;

struct avp_svc_info *avp_svc_init(struct platform_device *pdev,
				  struct trpc_node *rpc_node);
void avp_svc_destroy(struct avp_svc_info *avp_svc);
int avp_svc_start(struct avp_svc_info *svc);
void avp_svc_stop(struct avp_svc_info *svc);

#endif

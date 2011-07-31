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

struct avp_svc_info;

struct avp_svc_info *avp_svc_init(struct platform_device *pdev,
				  struct trpc_node *rpc_node);
void avp_svc_destroy(struct avp_svc_info *avp_svc);
int avp_svc_start(struct avp_svc_info *svc);
void avp_svc_stop(struct avp_svc_info *svc);

#endif

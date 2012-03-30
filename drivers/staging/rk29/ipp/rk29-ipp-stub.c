/* drivers/staging/rk29/ipp/rk29-ipp-stub.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
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

/*This is a dummy file */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <plat/ipp.h>

static int ipp_blit_sync_default(const struct rk29_ipp_req *req)
{
	return 0;
}

int (*ipp_blit_sync)(const struct rk29_ipp_req *req) = ipp_blit_sync_default;
EXPORT_SYMBOL(ipp_blit_sync);

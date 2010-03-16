/******************************************************************************
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2010 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

#include "iwl-dev.h"
#include "iwl-core.h"

#define IWL_UCODE_GET(item)						\
static u32 iwlagn_ucode_get_##item(const struct iwl_ucode_header *ucode,\
				    u32 api_ver)			\
{									\
	if (api_ver <= 2)						\
		return le32_to_cpu(ucode->u.v1.item);			\
	return le32_to_cpu(ucode->u.v2.item);				\
}

static u32 iwlagn_ucode_get_header_size(u32 api_ver)
{
	if (api_ver <= 2)
		return UCODE_HEADER_SIZE(1);
	return UCODE_HEADER_SIZE(2);
}

static u32 iwlagn_ucode_get_build(const struct iwl_ucode_header *ucode,
				   u32 api_ver)
{
	if (api_ver <= 2)
		return 0;
	return le32_to_cpu(ucode->u.v2.build);
}

static u8 *iwlagn_ucode_get_data(const struct iwl_ucode_header *ucode,
				  u32 api_ver)
{
	if (api_ver <= 2)
		return (u8 *) ucode->u.v1.data;
	return (u8 *) ucode->u.v2.data;
}

IWL_UCODE_GET(inst_size);
IWL_UCODE_GET(data_size);
IWL_UCODE_GET(init_size);
IWL_UCODE_GET(init_data_size);
IWL_UCODE_GET(boot_size);

struct iwl_ucode_ops iwlagn_ucode = {
	.get_header_size = iwlagn_ucode_get_header_size,
	.get_build = iwlagn_ucode_get_build,
	.get_inst_size = iwlagn_ucode_get_inst_size,
	.get_data_size = iwlagn_ucode_get_data_size,
	.get_init_size = iwlagn_ucode_get_init_size,
	.get_init_data_size = iwlagn_ucode_get_init_data_size,
	.get_boot_size = iwlagn_ucode_get_boot_size,
	.get_data = iwlagn_ucode_get_data,
};

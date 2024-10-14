// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD Address Translation Library
 *
 * prm.c : Plumbing code for ACPI Platform Runtime Mechanism (PRM)
 *
 * Information on AMD PRM modules and handlers including the GUIDs and buffer
 * structures used here are defined in the AMD ACPI Porting Guide in the
 * chapter "Platform Runtime Mechanism Table (PRMT)"
 *
 * Copyright (c) 2024, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: John Allen <john.allen@amd.com>
 */

#include "internal.h"

#include <linux/prmt.h>

/*
 * PRM parameter buffer - normalized to system physical address, as described
 * in the "PRM Parameter Buffer" section of the AMD ACPI Porting Guide.
 */
struct norm_to_sys_param_buf {
	u64 norm_addr;
	u8 socket;
	u64 bank_id;
	void *out_buf;
} __packed;

static const guid_t norm_to_sys_guid = GUID_INIT(0xE7180659, 0xA65D, 0x451D,
						 0x92, 0xCD, 0x2B, 0x56, 0xF1,
						 0x2B, 0xEB, 0xA6);

unsigned long prm_umc_norm_to_sys_addr(u8 socket_id, u64 bank_id, unsigned long addr)
{
	struct norm_to_sys_param_buf p_buf;
	unsigned long ret_addr;
	int ret;

	p_buf.norm_addr = addr;
	p_buf.socket    = socket_id;
	p_buf.bank_id   = bank_id;
	p_buf.out_buf   = &ret_addr;

	ret = acpi_call_prm_handler(norm_to_sys_guid, &p_buf);
	if (!ret)
		return ret_addr;

	if (ret == -ENODEV)
		pr_debug("PRM module/handler not available\n");
	else
		pr_notice_once("PRM address translation failed\n");

	return ret;
}

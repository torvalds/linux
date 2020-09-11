/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2019 - 2020 Intel Corporation
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
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2019 - 2020 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#include "img.h"

u8 iwl_fw_lookup_cmd_ver(const struct iwl_fw *fw, u8 grp, u8 cmd)
{
	const struct iwl_fw_cmd_version *entry;
	unsigned int i;

	if (!fw->ucode_capa.cmd_versions ||
	    !fw->ucode_capa.n_cmd_versions)
		return IWL_FW_CMD_VER_UNKNOWN;

	entry = fw->ucode_capa.cmd_versions;
	for (i = 0; i < fw->ucode_capa.n_cmd_versions; i++, entry++) {
		if (entry->group == grp && entry->cmd == cmd)
			return entry->cmd_ver;
	}

	return IWL_FW_CMD_VER_UNKNOWN;
}
EXPORT_SYMBOL_GPL(iwl_fw_lookup_cmd_ver);

u8 iwl_fw_lookup_notif_ver(const struct iwl_fw *fw, u8 grp, u8 cmd, u8 def)
{
	const struct iwl_fw_cmd_version *entry;
	unsigned int i;

	if (!fw->ucode_capa.cmd_versions ||
	    !fw->ucode_capa.n_cmd_versions)
		return def;

	entry = fw->ucode_capa.cmd_versions;
	for (i = 0; i < fw->ucode_capa.n_cmd_versions; i++, entry++) {
		if (entry->group == grp && entry->cmd == cmd) {
			if (entry->notif_ver == IWL_FW_CMD_VER_UNKNOWN)
				return def;
			return entry->notif_ver;
		}
	}

	return def;
}
EXPORT_SYMBOL_GPL(iwl_fw_lookup_notif_ver);

#define FW_SYSASSERT_CPU_MASK 0xf0000000
static const struct {
	const char *name;
	u8 num;
} advanced_lookup[] = {
	{ "NMI_INTERRUPT_WDG", 0x34 },
	{ "SYSASSERT", 0x35 },
	{ "UCODE_VERSION_MISMATCH", 0x37 },
	{ "BAD_COMMAND", 0x38 },
	{ "BAD_COMMAND", 0x39 },
	{ "NMI_INTERRUPT_DATA_ACTION_PT", 0x3C },
	{ "FATAL_ERROR", 0x3D },
	{ "NMI_TRM_HW_ERR", 0x46 },
	{ "NMI_INTERRUPT_TRM", 0x4C },
	{ "NMI_INTERRUPT_BREAK_POINT", 0x54 },
	{ "NMI_INTERRUPT_WDG_RXF_FULL", 0x5C },
	{ "NMI_INTERRUPT_WDG_NO_RBD_RXF_FULL", 0x64 },
	{ "NMI_INTERRUPT_HOST", 0x66 },
	{ "NMI_INTERRUPT_LMAC_FATAL", 0x70 },
	{ "NMI_INTERRUPT_UMAC_FATAL", 0x71 },
	{ "NMI_INTERRUPT_OTHER_LMAC_FATAL", 0x73 },
	{ "NMI_INTERRUPT_ACTION_PT", 0x7C },
	{ "NMI_INTERRUPT_UNKNOWN", 0x84 },
	{ "NMI_INTERRUPT_INST_ACTION_PT", 0x86 },
	{ "ADVANCED_SYSASSERT", 0 },
};

const char *iwl_fw_lookup_assert_desc(u32 num)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(advanced_lookup) - 1; i++)
		if (advanced_lookup[i].num == (num & ~FW_SYSASSERT_CPU_MASK))
			return advanced_lookup[i].name;

	/* No entry matches 'num', so it is the last: ADVANCED_SYSASSERT */
	return advanced_lookup[i].name;
}
EXPORT_SYMBOL_GPL(iwl_fw_lookup_assert_desc);

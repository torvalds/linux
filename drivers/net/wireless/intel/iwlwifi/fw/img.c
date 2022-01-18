// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright(c) 2019 - 2020 Intel Corporation
 */

#include "img.h"

u8 iwl_fw_lookup_cmd_ver(const struct iwl_fw *fw, u8 grp, u8 cmd, u8 def)
{
	const struct iwl_fw_cmd_version *entry;
	unsigned int i;

	if (!fw->ucode_capa.cmd_versions ||
	    !fw->ucode_capa.n_cmd_versions)
		return def;

	entry = fw->ucode_capa.cmd_versions;
	for (i = 0; i < fw->ucode_capa.n_cmd_versions; i++, entry++) {
		if (entry->group == grp && entry->cmd == cmd) {
			if (entry->cmd_ver == IWL_FW_CMD_VER_UNKNOWN)
				return def;
			return entry->cmd_ver;
		}
	}

	return def;
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

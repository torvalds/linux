/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */
#ifndef __iwl_mld_hcmd_h__
#define __iwl_mld_hcmd_h__

static inline int iwl_mld_send_cmd(struct iwl_mld *mld, struct iwl_host_cmd *cmd)
{
	/* No commands, including the d3 related commands, should be sent
	 * after entering d3
	 */
#ifdef CONFIG_PM_SLEEP
	if (WARN_ON(mld->fw_status.in_d3))
		return -EIO;
#endif

	if (!(cmd->flags & CMD_ASYNC))
		lockdep_assert_wiphy(mld->wiphy);

	/* Devices that need to shutdown immediately on rfkill are not
	 * supported, so we can send all the cmds in rfkill
	 */
	cmd->flags |= CMD_SEND_IN_RFKILL;

	return iwl_trans_send_cmd(mld->trans, cmd);
}

static inline int
__iwl_mld_send_cmd_with_flags_pdu(struct iwl_mld *mld, u32 id,
				  u32 flags, const void *data, u16 len)
{
	struct iwl_host_cmd cmd = {
		.id = id,
		.len = { data ? len : 0, },
		.data = { data, },
		.flags = flags,
	};

	return iwl_mld_send_cmd(mld, &cmd);
}

#define _iwl_mld_send_cmd_with_flags_pdu(mld, id, flags, data, len,	\
					 ignored...)			\
	__iwl_mld_send_cmd_with_flags_pdu(mld, id, flags, data, len)
#define iwl_mld_send_cmd_with_flags_pdu(mld, id, flags, data, len...)	\
	_iwl_mld_send_cmd_with_flags_pdu(mld, id, flags, data, ##len,	\
					 sizeof(*(data)))

#define iwl_mld_send_cmd_pdu(mld, id, ...)				\
	iwl_mld_send_cmd_with_flags_pdu(mld, id, 0, __VA_ARGS__)

#define iwl_mld_send_cmd_empty(mld, id)					\
	iwl_mld_send_cmd_with_flags_pdu(mld, id, 0, NULL, 0)

#endif /* __iwl_mld_hcmd_h__ */

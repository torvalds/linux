// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * Copyright 2021 NXP
 *
 */
#include <linux/kernel.h>
#include <linux/fsl/mc.h>

#include "fsl-mc-private.h"

static int fsl_mc_get_open_cmd_id(const char *type)
{
	static const struct {
		int cmd_id;
		const char *type;
	} dev_ids[] = {
		{ DPRTC_CMDID_OPEN, "dprtc" },
		{ DPRC_CMDID_OPEN, "dprc" },
		{ DPNI_CMDID_OPEN, "dpni" },
		{ DPIO_CMDID_OPEN, "dpio" },
		{ DPSW_CMDID_OPEN, "dpsw" },
		{ DPBP_CMDID_OPEN, "dpbp" },
		{ DPCON_CMDID_OPEN, "dpcon" },
		{ DPMCP_CMDID_OPEN, "dpmcp" },
		{ DPMAC_CMDID_OPEN, "dpmac" },
		{ DPSECI_CMDID_OPEN, "dpseci" },
		{ DPDMUX_CMDID_OPEN, "dpdmux" },
		{ DPDCEI_CMDID_OPEN, "dpdcei" },
		{ DPAIOP_CMDID_OPEN, "dpaiop" },
		{ DPCI_CMDID_OPEN, "dpci" },
		{ DPDMAI_CMDID_OPEN, "dpdmai" },
		{ DPDBG_CMDID_OPEN, "dpdbg" },
		{ 0, NULL }
	};
	int i;

	for (i = 0; dev_ids[i].type; i++)
		if (!strcmp(dev_ids[i].type, type))
			return dev_ids[i].cmd_id;

	return -1;
}

int fsl_mc_obj_open(struct fsl_mc_io *mc_io,
		    u32 cmd_flags,
		    int obj_id,
		    char *obj_type,
		    u16 *token)
{
	struct fsl_mc_command cmd = { 0 };
	struct fsl_mc_obj_cmd_open *cmd_params;
	int err = 0;
	int cmd_id = fsl_mc_get_open_cmd_id(obj_type);

	if (cmd_id == -1)
		return -ENODEV;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(cmd_id, cmd_flags, 0);
	cmd_params = (struct fsl_mc_obj_cmd_open *)cmd.params;
	cmd_params->obj_id = cpu_to_le32(obj_id);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	*token = mc_cmd_hdr_read_token(&cmd);

	return err;
}
EXPORT_SYMBOL_GPL(fsl_mc_obj_open);

int fsl_mc_obj_close(struct fsl_mc_io *mc_io,
		     u32 cmd_flags,
		     u16 token)
{
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(OBJ_CMDID_CLOSE, cmd_flags,
					  token);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}
EXPORT_SYMBOL_GPL(fsl_mc_obj_close);

int fsl_mc_obj_reset(struct fsl_mc_io *mc_io,
		     u32 cmd_flags,
		     u16 token)
{
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(OBJ_CMDID_RESET, cmd_flags,
					  token);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}
EXPORT_SYMBOL_GPL(fsl_mc_obj_reset);

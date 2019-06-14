// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
 * Copyright 2016-2018 NXP
 */

#include <linux/fsl/mc.h>

#include "dprtc.h"
#include "dprtc-cmd.h"

/**
 * dprtc_open() - Open a control session for the specified object.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @dprtc_id:	DPRTC unique ID
 * @token:	Returned token; use in subsequent API calls
 *
 * This function can be used to open a control session for an
 * already created object; an object may have been declared in
 * the DPL or by calling the dprtc_create function.
 * This function returns a unique authentication token,
 * associated with the specific object ID and the specific MC
 * portal; this token must be used in all subsequent commands for
 * this specific object
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprtc_open(struct fsl_mc_io *mc_io,
	       u32 cmd_flags,
	       int dprtc_id,
	       u16 *token)
{
	struct dprtc_cmd_open *cmd_params;
	struct fsl_mc_command cmd = { 0 };
	int err;

	cmd.header = mc_encode_cmd_header(DPRTC_CMDID_OPEN,
					  cmd_flags,
					  0);
	cmd_params = (struct dprtc_cmd_open *)cmd.params;
	cmd_params->dprtc_id = cpu_to_le32(dprtc_id);

	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	*token = mc_cmd_hdr_read_token(&cmd);

	return 0;
}

/**
 * dprtc_close() - Close the control session of the object
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRTC object
 *
 * After this function is called, no further operations are
 * allowed on the object without opening a new control session.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprtc_close(struct fsl_mc_io *mc_io,
		u32 cmd_flags,
		u16 token)
{
	struct fsl_mc_command cmd = { 0 };

	cmd.header = mc_encode_cmd_header(DPRTC_CMDID_CLOSE, cmd_flags,
					  token);

	return mc_send_command(mc_io, &cmd);
}

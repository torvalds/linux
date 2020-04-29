// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
 * Copyright 2016 NXP
 *
 */
#include <linux/kernel.h>
#include <linux/fsl/mc.h>

#include "dpio.h"
#include "dpio-cmd.h"

/*
 * Data Path I/O Portal API
 * Contains initialization APIs and runtime control APIs for DPIO
 */

/**
 * dpio_open() - Open a control session for the specified object
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @dpio_id:	DPIO unique ID
 * @token:	Returned token; use in subsequent API calls
 *
 * This function can be used to open a control session for an
 * already created object; an object may have been declared in
 * the DPL or by calling the dpio_create() function.
 * This function returns a unique authentication token,
 * associated with the specific object ID and the specific MC
 * portal; this token must be used in all subsequent commands for
 * this specific object.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpio_open(struct fsl_mc_io *mc_io,
	      u32 cmd_flags,
	      int dpio_id,
	      u16 *token)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpio_cmd_open *dpio_cmd;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPIO_CMDID_OPEN,
					  cmd_flags,
					  0);
	dpio_cmd = (struct dpio_cmd_open *)cmd.params;
	dpio_cmd->dpio_id = cpu_to_le32(dpio_id);

	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	*token = mc_cmd_hdr_read_token(&cmd);

	return 0;
}

/**
 * dpio_close() - Close the control session of the object
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPIO object
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpio_close(struct fsl_mc_io *mc_io,
	       u32 cmd_flags,
	       u16 token)
{
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPIO_CMDID_CLOSE,
					  cmd_flags,
					  token);

	return mc_send_command(mc_io, &cmd);
}

/**
 * dpio_enable() - Enable the DPIO, allow I/O portal operations.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPIO object
 *
 * Return:	'0' on Success; Error code otherwise
 */
int dpio_enable(struct fsl_mc_io *mc_io,
		u32 cmd_flags,
		u16 token)
{
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPIO_CMDID_ENABLE,
					  cmd_flags,
					  token);

	return mc_send_command(mc_io, &cmd);
}

/**
 * dpio_disable() - Disable the DPIO, stop any I/O portal operation.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPIO object
 *
 * Return:	'0' on Success; Error code otherwise
 */
int dpio_disable(struct fsl_mc_io *mc_io,
		 u32 cmd_flags,
		 u16 token)
{
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPIO_CMDID_DISABLE,
					  cmd_flags,
					  token);

	return mc_send_command(mc_io, &cmd);
}

/**
 * dpio_get_attributes() - Retrieve DPIO attributes
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPIO object
 * @attr:	Returned object's attributes
 *
 * Return:	'0' on Success; Error code otherwise
 */
int dpio_get_attributes(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			struct dpio_attr *attr)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpio_rsp_get_attr *dpio_rsp;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPIO_CMDID_GET_ATTR,
					  cmd_flags,
					  token);

	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	dpio_rsp = (struct dpio_rsp_get_attr *)cmd.params;
	attr->id = le32_to_cpu(dpio_rsp->id);
	attr->qbman_portal_id = le16_to_cpu(dpio_rsp->qbman_portal_id);
	attr->num_priorities = dpio_rsp->num_priorities;
	attr->channel_mode = dpio_rsp->channel_mode & DPIO_CHANNEL_MODE_MASK;
	attr->qbman_portal_ce_offset =
		le64_to_cpu(dpio_rsp->qbman_portal_ce_addr);
	attr->qbman_portal_ci_offset =
		le64_to_cpu(dpio_rsp->qbman_portal_ci_addr);
	attr->qbman_version = le32_to_cpu(dpio_rsp->qbman_version);

	return 0;
}

int dpio_set_stashing_destination(struct fsl_mc_io *mc_io,
				  u32 cmd_flags,
				  u16 token,
				  u8 sdest)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpio_stashing_dest *dpio_cmd;

	cmd.header = mc_encode_cmd_header(DPIO_CMDID_SET_STASHING_DEST,
					  cmd_flags, token);
	dpio_cmd = (struct dpio_stashing_dest *)cmd.params;
	dpio_cmd->sdest = sdest;

	return mc_send_command(mc_io, &cmd);
}

/**
 * dpio_get_api_version - Get Data Path I/O API version
 * @mc_io:	Pointer to MC portal's DPIO object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @major_ver:	Major version of DPIO API
 * @minor_ver:	Minor version of DPIO API
 *
 * Return:	'0' on Success; Error code otherwise
 */
int dpio_get_api_version(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 *major_ver,
			 u16 *minor_ver)
{
	struct fsl_mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPIO_CMDID_GET_API_VERSION,
					  cmd_flags, 0);

	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	mc_cmd_read_api_version(&cmd, major_ver, minor_ver);

	return 0;
}

/**
 * dpio_reset() - Reset the DPIO, returns the object to initial state.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPIO object
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpio_reset(struct fsl_mc_io *mc_io,
	       u32 cmd_flags,
	       u16 token)
{
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPIO_CMDID_RESET,
					  cmd_flags,
					  token);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

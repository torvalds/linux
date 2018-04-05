// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
 *
 */
#include <linux/kernel.h>
#include <linux/fsl/mc.h>
#include <linux/fsl/mc.h>

#include "fsl-mc-private.h"

/**
 * dpcon_open() - Open a control session for the specified object
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @dpcon_id:	DPCON unique ID
 * @token:	Returned token; use in subsequent API calls
 *
 * This function can be used to open a control session for an
 * already created object; an object may have been declared in
 * the DPL or by calling the dpcon_create() function.
 * This function returns a unique authentication token,
 * associated with the specific object ID and the specific MC
 * portal; this token must be used in all subsequent commands for
 * this specific object.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpcon_open(struct fsl_mc_io *mc_io,
	       u32 cmd_flags,
	       int dpcon_id,
	       u16 *token)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpcon_cmd_open *dpcon_cmd;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPCON_CMDID_OPEN,
					  cmd_flags,
					  0);
	dpcon_cmd = (struct dpcon_cmd_open *)cmd.params;
	dpcon_cmd->dpcon_id = cpu_to_le32(dpcon_id);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	*token = mc_cmd_hdr_read_token(&cmd);

	return 0;
}
EXPORT_SYMBOL_GPL(dpcon_open);

/**
 * dpcon_close() - Close the control session of the object
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPCON object
 *
 * After this function is called, no further operations are
 * allowed on the object without opening a new control session.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpcon_close(struct fsl_mc_io *mc_io,
		u32 cmd_flags,
		u16 token)
{
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPCON_CMDID_CLOSE,
					  cmd_flags,
					  token);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}
EXPORT_SYMBOL_GPL(dpcon_close);

/**
 * dpcon_enable() - Enable the DPCON
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPCON object
 *
 * Return:	'0' on Success; Error code otherwise
 */
int dpcon_enable(struct fsl_mc_io *mc_io,
		 u32 cmd_flags,
		 u16 token)
{
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPCON_CMDID_ENABLE,
					  cmd_flags,
					  token);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}
EXPORT_SYMBOL_GPL(dpcon_enable);

/**
 * dpcon_disable() - Disable the DPCON
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPCON object
 *
 * Return:	'0' on Success; Error code otherwise
 */
int dpcon_disable(struct fsl_mc_io *mc_io,
		  u32 cmd_flags,
		  u16 token)
{
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPCON_CMDID_DISABLE,
					  cmd_flags,
					  token);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}
EXPORT_SYMBOL_GPL(dpcon_disable);

/**
 * dpcon_reset() - Reset the DPCON, returns the object to initial state.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPCON object
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpcon_reset(struct fsl_mc_io *mc_io,
		u32 cmd_flags,
		u16 token)
{
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPCON_CMDID_RESET,
					  cmd_flags, token);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}
EXPORT_SYMBOL_GPL(dpcon_reset);

/**
 * dpcon_get_attributes() - Retrieve DPCON attributes.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPCON object
 * @attr:	Object's attributes
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpcon_get_attributes(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 token,
			 struct dpcon_attr *attr)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpcon_rsp_get_attr *dpcon_rsp;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPCON_CMDID_GET_ATTR,
					  cmd_flags,
					  token);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	dpcon_rsp = (struct dpcon_rsp_get_attr *)cmd.params;
	attr->id = le32_to_cpu(dpcon_rsp->id);
	attr->qbman_ch_id = le16_to_cpu(dpcon_rsp->qbman_ch_id);
	attr->num_priorities = dpcon_rsp->num_priorities;

	return 0;
}
EXPORT_SYMBOL_GPL(dpcon_get_attributes);

/**
 * dpcon_set_notification() - Set DPCON notification destination
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPCON object
 * @cfg:	Notification parameters
 *
 * Return:	'0' on Success; Error code otherwise
 */
int dpcon_set_notification(struct fsl_mc_io *mc_io,
			   u32 cmd_flags,
			   u16 token,
			   struct dpcon_notification_cfg *cfg)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpcon_cmd_set_notification *dpcon_cmd;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPCON_CMDID_SET_NOTIFICATION,
					  cmd_flags,
					  token);
	dpcon_cmd = (struct dpcon_cmd_set_notification *)cmd.params;
	dpcon_cmd->dpio_id = cpu_to_le32(cfg->dpio_id);
	dpcon_cmd->priority = cfg->priority;
	dpcon_cmd->user_ctx = cpu_to_le64(cfg->user_ctx);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}
EXPORT_SYMBOL_GPL(dpcon_set_notification);

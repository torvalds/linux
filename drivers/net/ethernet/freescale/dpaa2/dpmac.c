// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2013-2016 Freescale Semiconductor Inc.
 * Copyright 2019 NXP
 */
#include <linux/fsl/mc.h>
#include "dpmac.h"
#include "dpmac-cmd.h"

/**
 * dpmac_open() - Open a control session for the specified object.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @dpmac_id:	DPMAC unique ID
 * @token:	Returned token; use in subsequent API calls
 *
 * This function can be used to open a control session for an
 * already created object; an object may have been declared in
 * the DPL or by calling the dpmac_create function.
 * This function returns a unique authentication token,
 * associated with the specific object ID and the specific MC
 * portal; this token must be used in all subsequent commands for
 * this specific object
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpmac_open(struct fsl_mc_io *mc_io,
	       u32 cmd_flags,
	       int dpmac_id,
	       u16 *token)
{
	struct dpmac_cmd_open *cmd_params;
	struct fsl_mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPMAC_CMDID_OPEN,
					  cmd_flags,
					  0);
	cmd_params = (struct dpmac_cmd_open *)cmd.params;
	cmd_params->dpmac_id = cpu_to_le32(dpmac_id);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	*token = mc_cmd_hdr_read_token(&cmd);

	return err;
}

/**
 * dpmac_close() - Close the control session of the object
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPMAC object
 *
 * After this function is called, no further operations are
 * allowed on the object without opening a new control session.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpmac_close(struct fsl_mc_io *mc_io,
		u32 cmd_flags,
		u16 token)
{
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPMAC_CMDID_CLOSE, cmd_flags,
					  token);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpmac_get_attributes - Retrieve DPMAC attributes.
 *
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPMAC object
 * @attr:	Returned object's attributes
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpmac_get_attributes(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 token,
			 struct dpmac_attr *attr)
{
	struct dpmac_rsp_get_attributes *rsp_params;
	struct fsl_mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPMAC_CMDID_GET_ATTR,
					  cmd_flags,
					  token);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpmac_rsp_get_attributes *)cmd.params;
	attr->eth_if = rsp_params->eth_if;
	attr->link_type = rsp_params->link_type;
	attr->id = le16_to_cpu(rsp_params->id);
	attr->max_rate = le32_to_cpu(rsp_params->max_rate);

	return 0;
}

/**
 * dpmac_set_link_state() - Set the Ethernet link status
 * @mc_io:      Pointer to opaque I/O object
 * @cmd_flags:  Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:      Token of DPMAC object
 * @link_state: Link state configuration
 *
 * Return:      '0' on Success; Error code otherwise.
 */
int dpmac_set_link_state(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 token,
			 struct dpmac_link_state *link_state)
{
	struct dpmac_cmd_set_link_state *cmd_params;
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPMAC_CMDID_SET_LINK_STATE,
					  cmd_flags,
					  token);
	cmd_params = (struct dpmac_cmd_set_link_state *)cmd.params;
	cmd_params->options = cpu_to_le64(link_state->options);
	cmd_params->rate = cpu_to_le32(link_state->rate);
	dpmac_set_field(cmd_params->state, STATE, link_state->up);
	dpmac_set_field(cmd_params->state, STATE_VALID,
			link_state->state_valid);
	cmd_params->supported = cpu_to_le64(link_state->supported);
	cmd_params->advertising = cpu_to_le64(link_state->advertising);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpmac_get_counter() - Read a specific DPMAC counter
 * @mc_io:	Pointer to opaque I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPMAC object
 * @id:		The requested counter ID
 * @value:	Returned counter value
 *
 * Return:	The requested counter; '0' otherwise.
 */
int dpmac_get_counter(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token,
		      enum dpmac_counter_id id, u64 *value)
{
	struct dpmac_cmd_get_counter *dpmac_cmd;
	struct dpmac_rsp_get_counter *dpmac_rsp;
	struct fsl_mc_command cmd = { 0 };
	int err = 0;

	cmd.header = mc_encode_cmd_header(DPMAC_CMDID_GET_COUNTER,
					  cmd_flags,
					  token);
	dpmac_cmd = (struct dpmac_cmd_get_counter *)cmd.params;
	dpmac_cmd->id = id;

	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	dpmac_rsp = (struct dpmac_rsp_get_counter *)cmd.params;
	*value = le64_to_cpu(dpmac_rsp->counter);

	return 0;
}

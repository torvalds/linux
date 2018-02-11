/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
 * Copyright 2016 NXP
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * * Neither the name of the above-listed copyright holders nor the
 * names of any contributors may be used to endorse or promote products
 * derived from this software without specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <linux/kernel.h>
#include "../../include/mc.h"

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
	struct mc_command cmd = { 0 };
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
	struct mc_command cmd = { 0 };

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
	struct mc_command cmd = { 0 };

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
	struct mc_command cmd = { 0 };

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
	struct mc_command cmd = { 0 };
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
	struct mc_command cmd = { 0 };
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

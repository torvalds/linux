/* Copyright 2013-2016 Freescale Semiconductor Inc.
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
#include "../include/mc-sys.h"
#include "../include/mc-cmd.h"
#include "../include/dpmng.h"
#include "dpmng-cmd.h"

/**
 * mc_get_version() - Retrieves the Management Complex firmware
 *			version information
 * @mc_io:		Pointer to opaque I/O object
 * @cmd_flags:		Command flags; one or more of 'MC_CMD_FLAG_'
 * @mc_ver_info:	Returned version information structure
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int mc_get_version(struct fsl_mc_io *mc_io,
		   u32 cmd_flags,
		   struct mc_version *mc_ver_info)
{
	struct mc_command cmd = { 0 };
	struct dpmng_rsp_get_version *rsp_params;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPMNG_CMDID_GET_VERSION,
					  cmd_flags,
					  0);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpmng_rsp_get_version *)cmd.params;
	mc_ver_info->revision = le32_to_cpu(rsp_params->revision);
	mc_ver_info->major = le32_to_cpu(rsp_params->version_major);
	mc_ver_info->minor = le32_to_cpu(rsp_params->version_minor);

	return 0;
}
EXPORT_SYMBOL(mc_get_version);

/**
 * dpmng_get_container_id() - Get container ID associated with a given portal.
 * @mc_io:		Pointer to MC portal's I/O object
 * @cmd_flags:		Command flags; one or more of 'MC_CMD_FLAG_'
 * @container_id:	Requested container ID
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpmng_get_container_id(struct fsl_mc_io *mc_io,
			   u32 cmd_flags,
			   int *container_id)
{
	struct mc_command cmd = { 0 };
	struct dpmng_rsp_get_container_id *rsp_params;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPMNG_CMDID_GET_CONT_ID,
					  cmd_flags,
					  0);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpmng_rsp_get_container_id *)cmd.params;
	*container_id = le32_to_cpu(rsp_params->container_id);

	return 0;
}


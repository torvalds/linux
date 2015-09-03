/* Copyright 2013-2014 Freescale Semiconductor Inc.
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
#include "../include/dprc.h"
#include "dprc-cmd.h"

int dprc_open(struct fsl_mc_io *mc_io, int container_id, uint16_t *token)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_OPEN, MC_CMD_PRI_LOW,
					  0);
	cmd.params[0] |= mc_enc(0, 32, container_id);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	*token = MC_CMD_HDR_READ_TOKEN(cmd.header);

	return 0;
}
EXPORT_SYMBOL(dprc_open);

int dprc_close(struct fsl_mc_io *mc_io, uint16_t token)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_CLOSE, MC_CMD_PRI_HIGH,
					  token);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}
EXPORT_SYMBOL(dprc_close);

int dprc_create_container(struct fsl_mc_io *mc_io,
			  uint16_t token,
			  struct dprc_cfg *cfg,
			  int *child_container_id,
			  uint64_t *child_portal_paddr)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.params[0] |= mc_enc(32, 16, cfg->icid);
	cmd.params[0] |= mc_enc(0, 32, cfg->options);
	cmd.params[1] |= mc_enc(32, 32, cfg->portal_id);

	cmd.header = mc_encode_cmd_header(DPRC_CMDID_CREATE_CONT,
					  MC_CMD_PRI_LOW, token);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	*child_container_id = mc_dec(cmd.params[1], 0, 32);
	*child_portal_paddr = mc_dec(cmd.params[2], 0, 64);

	return 0;
}

int dprc_destroy_container(struct fsl_mc_io *mc_io,
			   uint16_t token,
			   int child_container_id)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_DESTROY_CONT,
					  MC_CMD_PRI_LOW, token);
	cmd.params[0] |= mc_enc(0, 32, child_container_id);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

int dprc_reset_container(struct fsl_mc_io *mc_io,
			 uint16_t token,
			 int child_container_id)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_RESET_CONT,
					  MC_CMD_PRI_LOW, token);
	cmd.params[0] |= mc_enc(0, 32, child_container_id);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

int dprc_get_irq(struct fsl_mc_io *mc_io,
		 uint16_t token,
		 uint8_t irq_index,
		 int *type,
		 uint64_t *irq_paddr,
		 uint32_t *irq_val,
		 int *user_irq_id)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_IRQ,
					  MC_CMD_PRI_LOW,
					  token);
	cmd.params[0] |= mc_enc(32, 8, irq_index);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	*irq_val = mc_dec(cmd.params[0], 0, 32);
	*irq_paddr = mc_dec(cmd.params[1], 0, 64);
	*user_irq_id = mc_dec(cmd.params[2], 0, 32);
	*type = mc_dec(cmd.params[2], 32, 32);

	return 0;
}

int dprc_set_irq(struct fsl_mc_io *mc_io,
		 uint16_t token,
		 uint8_t irq_index,
		 uint64_t irq_paddr,
		 uint32_t irq_val,
		 int user_irq_id)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_SET_IRQ,
					  MC_CMD_PRI_LOW,
					  token);
	cmd.params[0] |= mc_enc(32, 8, irq_index);
	cmd.params[0] |= mc_enc(0, 32, irq_val);
	cmd.params[1] |= mc_enc(0, 64, irq_paddr);
	cmd.params[2] |= mc_enc(0, 32, user_irq_id);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

int dprc_get_irq_enable(struct fsl_mc_io *mc_io,
			uint16_t token,
			uint8_t irq_index,
			uint8_t *en)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_IRQ_ENABLE,
					  MC_CMD_PRI_LOW, token);
	cmd.params[0] |= mc_enc(32, 8, irq_index);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	*en = mc_dec(cmd.params[0], 0, 8);

	return 0;
}

int dprc_set_irq_enable(struct fsl_mc_io *mc_io,
			uint16_t token,
			uint8_t irq_index,
			uint8_t en)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_SET_IRQ_ENABLE,
					  MC_CMD_PRI_LOW, token);
	cmd.params[0] |= mc_enc(0, 8, en);
	cmd.params[0] |= mc_enc(32, 8, irq_index);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

int dprc_get_irq_mask(struct fsl_mc_io *mc_io,
		      uint16_t token,
		      uint8_t irq_index,
		      uint32_t *mask)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_IRQ_MASK,
					  MC_CMD_PRI_LOW, token);
	cmd.params[0] |= mc_enc(32, 8, irq_index);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	*mask = mc_dec(cmd.params[0], 0, 32);

	return 0;
}

int dprc_set_irq_mask(struct fsl_mc_io *mc_io,
		      uint16_t token,
		      uint8_t irq_index,
		      uint32_t mask)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_SET_IRQ_MASK,
					  MC_CMD_PRI_LOW, token);
	cmd.params[0] |= mc_enc(0, 32, mask);
	cmd.params[0] |= mc_enc(32, 8, irq_index);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

int dprc_get_irq_status(struct fsl_mc_io *mc_io,
			uint16_t token,
			uint8_t irq_index,
			uint32_t *status)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_IRQ_STATUS,
					  MC_CMD_PRI_LOW, token);
	cmd.params[0] |= mc_enc(32, 8, irq_index);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	*status = mc_dec(cmd.params[0], 0, 32);

	return 0;
}

int dprc_clear_irq_status(struct fsl_mc_io *mc_io,
			  uint16_t token,
			  uint8_t irq_index,
			  uint32_t status)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_CLEAR_IRQ_STATUS,
					  MC_CMD_PRI_LOW, token);
	cmd.params[0] |= mc_enc(0, 32, status);
	cmd.params[0] |= mc_enc(32, 8, irq_index);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

int dprc_get_attributes(struct fsl_mc_io *mc_io,
			uint16_t token,
			struct dprc_attributes *attr)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_ATTR,
					  MC_CMD_PRI_LOW,
					  token);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	attr->container_id = mc_dec(cmd.params[0], 0, 32);
	attr->icid = mc_dec(cmd.params[0], 32, 16);
	attr->options = mc_dec(cmd.params[1], 0, 32);
	attr->portal_id = mc_dec(cmd.params[1], 32, 32);
	attr->version.major = mc_dec(cmd.params[2], 0, 16);
	attr->version.minor = mc_dec(cmd.params[2], 16, 16);

	return 0;
}

int dprc_set_res_quota(struct fsl_mc_io *mc_io,
		       uint16_t token,
		       int child_container_id,
		       char *type,
		       uint16_t quota)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_SET_RES_QUOTA,
					  MC_CMD_PRI_LOW, token);
	cmd.params[0] |= mc_enc(0, 32, child_container_id);
	cmd.params[0] |= mc_enc(32, 16, quota);
	cmd.params[1] |= mc_enc(0, 8, type[0]);
	cmd.params[1] |= mc_enc(8, 8, type[1]);
	cmd.params[1] |= mc_enc(16, 8, type[2]);
	cmd.params[1] |= mc_enc(24, 8, type[3]);
	cmd.params[1] |= mc_enc(32, 8, type[4]);
	cmd.params[1] |= mc_enc(40, 8, type[5]);
	cmd.params[1] |= mc_enc(48, 8, type[6]);
	cmd.params[1] |= mc_enc(56, 8, type[7]);
	cmd.params[2] |= mc_enc(0, 8, type[8]);
	cmd.params[2] |= mc_enc(8, 8, type[9]);
	cmd.params[2] |= mc_enc(16, 8, type[10]);
	cmd.params[2] |= mc_enc(24, 8, type[11]);
	cmd.params[2] |= mc_enc(32, 8, type[12]);
	cmd.params[2] |= mc_enc(40, 8, type[13]);
	cmd.params[2] |= mc_enc(48, 8, type[14]);
	cmd.params[2] |= mc_enc(56, 8, '\0');

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

int dprc_get_res_quota(struct fsl_mc_io *mc_io,
		       uint16_t token,
		       int child_container_id,
		       char *type,
		       uint16_t *quota)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_RES_QUOTA,
					  MC_CMD_PRI_LOW, token);
	cmd.params[0] |= mc_enc(0, 32, child_container_id);
	cmd.params[1] |= mc_enc(0, 8, type[0]);
	cmd.params[1] |= mc_enc(8, 8, type[1]);
	cmd.params[1] |= mc_enc(16, 8, type[2]);
	cmd.params[1] |= mc_enc(24, 8, type[3]);
	cmd.params[1] |= mc_enc(32, 8, type[4]);
	cmd.params[1] |= mc_enc(40, 8, type[5]);
	cmd.params[1] |= mc_enc(48, 8, type[6]);
	cmd.params[1] |= mc_enc(56, 8, type[7]);
	cmd.params[2] |= mc_enc(0, 8, type[8]);
	cmd.params[2] |= mc_enc(8, 8, type[9]);
	cmd.params[2] |= mc_enc(16, 8, type[10]);
	cmd.params[2] |= mc_enc(24, 8, type[11]);
	cmd.params[2] |= mc_enc(32, 8, type[12]);
	cmd.params[2] |= mc_enc(40, 8, type[13]);
	cmd.params[2] |= mc_enc(48, 8, type[14]);
	cmd.params[2] |= mc_enc(56, 8, '\0');

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	*quota = mc_dec(cmd.params[0], 32, 16);

	return 0;
}

int dprc_assign(struct fsl_mc_io *mc_io,
		uint16_t token,
		int container_id,
		struct dprc_res_req *res_req)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_ASSIGN,
					  MC_CMD_PRI_LOW, token);
	cmd.params[0] |= mc_enc(0, 32, container_id);
	cmd.params[0] |= mc_enc(32, 32, res_req->options);
	cmd.params[1] |= mc_enc(0, 32, res_req->num);
	cmd.params[1] |= mc_enc(32, 32, res_req->id_base_align);
	cmd.params[2] |= mc_enc(0, 8, res_req->type[0]);
	cmd.params[2] |= mc_enc(8, 8, res_req->type[1]);
	cmd.params[2] |= mc_enc(16, 8, res_req->type[2]);
	cmd.params[2] |= mc_enc(24, 8, res_req->type[3]);
	cmd.params[2] |= mc_enc(32, 8, res_req->type[4]);
	cmd.params[2] |= mc_enc(40, 8, res_req->type[5]);
	cmd.params[2] |= mc_enc(48, 8, res_req->type[6]);
	cmd.params[2] |= mc_enc(56, 8, res_req->type[7]);
	cmd.params[3] |= mc_enc(0, 8, res_req->type[8]);
	cmd.params[3] |= mc_enc(8, 8, res_req->type[9]);
	cmd.params[3] |= mc_enc(16, 8, res_req->type[10]);
	cmd.params[3] |= mc_enc(24, 8, res_req->type[11]);
	cmd.params[3] |= mc_enc(32, 8, res_req->type[12]);
	cmd.params[3] |= mc_enc(40, 8, res_req->type[13]);
	cmd.params[3] |= mc_enc(48, 8, res_req->type[14]);
	cmd.params[3] |= mc_enc(56, 8, res_req->type[15]);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

int dprc_unassign(struct fsl_mc_io *mc_io,
		  uint16_t token,
		  int child_container_id,
		  struct dprc_res_req *res_req)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_UNASSIGN,
					  MC_CMD_PRI_LOW,
					  token);
	cmd.params[0] |= mc_enc(0, 32, child_container_id);
	cmd.params[0] |= mc_enc(32, 32, res_req->options);
	cmd.params[1] |= mc_enc(0, 32, res_req->num);
	cmd.params[1] |= mc_enc(32, 32, res_req->id_base_align);
	cmd.params[2] |= mc_enc(0, 8, res_req->type[0]);
	cmd.params[2] |= mc_enc(8, 8, res_req->type[1]);
	cmd.params[2] |= mc_enc(16, 8, res_req->type[2]);
	cmd.params[2] |= mc_enc(24, 8, res_req->type[3]);
	cmd.params[2] |= mc_enc(32, 8, res_req->type[4]);
	cmd.params[2] |= mc_enc(40, 8, res_req->type[5]);
	cmd.params[2] |= mc_enc(48, 8, res_req->type[6]);
	cmd.params[2] |= mc_enc(56, 8, res_req->type[7]);
	cmd.params[3] |= mc_enc(0, 8, res_req->type[8]);
	cmd.params[3] |= mc_enc(8, 8, res_req->type[9]);
	cmd.params[3] |= mc_enc(16, 8, res_req->type[10]);
	cmd.params[3] |= mc_enc(24, 8, res_req->type[11]);
	cmd.params[3] |= mc_enc(32, 8, res_req->type[12]);
	cmd.params[3] |= mc_enc(40, 8, res_req->type[13]);
	cmd.params[3] |= mc_enc(48, 8, res_req->type[14]);
	cmd.params[3] |= mc_enc(56, 8, res_req->type[15]);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

int dprc_get_pool_count(struct fsl_mc_io *mc_io,
			uint16_t token,
			int *pool_count)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_POOL_COUNT,
					  MC_CMD_PRI_LOW, token);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	*pool_count = mc_dec(cmd.params[0], 0, 32);

	return 0;
}

int dprc_get_pool(struct fsl_mc_io *mc_io,
		  uint16_t token,
		  int pool_index,
		  char *type)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_POOL,
					  MC_CMD_PRI_LOW,
					  token);
	cmd.params[0] |= mc_enc(0, 32, pool_index);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	type[0] = mc_dec(cmd.params[1], 0, 8);
	type[1] = mc_dec(cmd.params[1], 8, 8);
	type[2] = mc_dec(cmd.params[1], 16, 8);
	type[3] = mc_dec(cmd.params[1], 24, 8);
	type[4] = mc_dec(cmd.params[1], 32, 8);
	type[5] = mc_dec(cmd.params[1], 40, 8);
	type[6] = mc_dec(cmd.params[1], 48, 8);
	type[7] = mc_dec(cmd.params[1], 56, 8);
	type[8] = mc_dec(cmd.params[2], 0, 8);
	type[9] = mc_dec(cmd.params[2], 8, 8);
	type[10] = mc_dec(cmd.params[2], 16, 8);
	type[11] = mc_dec(cmd.params[2], 24, 8);
	type[12] = mc_dec(cmd.params[2], 32, 8);
	type[13] = mc_dec(cmd.params[2], 40, 8);
	type[14] = mc_dec(cmd.params[2], 48, 8);
	type[15] = '\0';

	return 0;
}

int dprc_get_obj_count(struct fsl_mc_io *mc_io, uint16_t token, int *obj_count)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_OBJ_COUNT,
					  MC_CMD_PRI_LOW, token);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	*obj_count = mc_dec(cmd.params[0], 32, 32);

	return 0;
}
EXPORT_SYMBOL(dprc_get_obj_count);

int dprc_get_obj(struct fsl_mc_io *mc_io,
		 uint16_t token,
		 int obj_index,
		 struct dprc_obj_desc *obj_desc)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_OBJ,
					  MC_CMD_PRI_LOW,
					  token);
	cmd.params[0] |= mc_enc(0, 32, obj_index);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	obj_desc->id = mc_dec(cmd.params[0], 32, 32);
	obj_desc->vendor = mc_dec(cmd.params[1], 0, 16);
	obj_desc->irq_count = mc_dec(cmd.params[1], 16, 8);
	obj_desc->region_count = mc_dec(cmd.params[1], 24, 8);
	obj_desc->state = mc_dec(cmd.params[1], 32, 32);
	obj_desc->ver_major = mc_dec(cmd.params[2], 0, 16);
	obj_desc->ver_minor = mc_dec(cmd.params[2], 16, 16);
	obj_desc->type[0] = mc_dec(cmd.params[3], 0, 8);
	obj_desc->type[1] = mc_dec(cmd.params[3], 8, 8);
	obj_desc->type[2] = mc_dec(cmd.params[3], 16, 8);
	obj_desc->type[3] = mc_dec(cmd.params[3], 24, 8);
	obj_desc->type[4] = mc_dec(cmd.params[3], 32, 8);
	obj_desc->type[5] = mc_dec(cmd.params[3], 40, 8);
	obj_desc->type[6] = mc_dec(cmd.params[3], 48, 8);
	obj_desc->type[7] = mc_dec(cmd.params[3], 56, 8);
	obj_desc->type[8] = mc_dec(cmd.params[4], 0, 8);
	obj_desc->type[9] = mc_dec(cmd.params[4], 8, 8);
	obj_desc->type[10] = mc_dec(cmd.params[4], 16, 8);
	obj_desc->type[11] = mc_dec(cmd.params[4], 24, 8);
	obj_desc->type[12] = mc_dec(cmd.params[4], 32, 8);
	obj_desc->type[13] = mc_dec(cmd.params[4], 40, 8);
	obj_desc->type[14] = mc_dec(cmd.params[4], 48, 8);
	obj_desc->type[15] = '\0';

	return 0;
}
EXPORT_SYMBOL(dprc_get_obj);

int dprc_get_res_count(struct fsl_mc_io *mc_io,
		       uint16_t token,
		       char *type,
		       int *res_count)
{
	struct mc_command cmd = { 0 };
	int err;

	*res_count = 0;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_RES_COUNT,
					  MC_CMD_PRI_LOW, token);
	cmd.params[1] |= mc_enc(0, 8, type[0]);
	cmd.params[1] |= mc_enc(8, 8, type[1]);
	cmd.params[1] |= mc_enc(16, 8, type[2]);
	cmd.params[1] |= mc_enc(24, 8, type[3]);
	cmd.params[1] |= mc_enc(32, 8, type[4]);
	cmd.params[1] |= mc_enc(40, 8, type[5]);
	cmd.params[1] |= mc_enc(48, 8, type[6]);
	cmd.params[1] |= mc_enc(56, 8, type[7]);
	cmd.params[2] |= mc_enc(0, 8, type[8]);
	cmd.params[2] |= mc_enc(8, 8, type[9]);
	cmd.params[2] |= mc_enc(16, 8, type[10]);
	cmd.params[2] |= mc_enc(24, 8, type[11]);
	cmd.params[2] |= mc_enc(32, 8, type[12]);
	cmd.params[2] |= mc_enc(40, 8, type[13]);
	cmd.params[2] |= mc_enc(48, 8, type[14]);
	cmd.params[2] |= mc_enc(56, 8, '\0');

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	*res_count = mc_dec(cmd.params[0], 0, 32);

	return 0;
}
EXPORT_SYMBOL(dprc_get_res_count);

int dprc_get_res_ids(struct fsl_mc_io *mc_io,
		     uint16_t token,
		     char *type,
		     struct dprc_res_ids_range_desc *range_desc)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_RES_IDS,
					  MC_CMD_PRI_LOW, token);
	cmd.params[0] |= mc_enc(42, 7, range_desc->iter_status);
	cmd.params[1] |= mc_enc(0, 32, range_desc->base_id);
	cmd.params[1] |= mc_enc(32, 32, range_desc->last_id);
	cmd.params[2] |= mc_enc(0, 8, type[0]);
	cmd.params[2] |= mc_enc(8, 8, type[1]);
	cmd.params[2] |= mc_enc(16, 8, type[2]);
	cmd.params[2] |= mc_enc(24, 8, type[3]);
	cmd.params[2] |= mc_enc(32, 8, type[4]);
	cmd.params[2] |= mc_enc(40, 8, type[5]);
	cmd.params[2] |= mc_enc(48, 8, type[6]);
	cmd.params[2] |= mc_enc(56, 8, type[7]);
	cmd.params[3] |= mc_enc(0, 8, type[8]);
	cmd.params[3] |= mc_enc(8, 8, type[9]);
	cmd.params[3] |= mc_enc(16, 8, type[10]);
	cmd.params[3] |= mc_enc(24, 8, type[11]);
	cmd.params[3] |= mc_enc(32, 8, type[12]);
	cmd.params[3] |= mc_enc(40, 8, type[13]);
	cmd.params[3] |= mc_enc(48, 8, type[14]);
	cmd.params[3] |= mc_enc(56, 8, '\0');

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	range_desc->iter_status = mc_dec(cmd.params[0], 42, 7);
	range_desc->base_id = mc_dec(cmd.params[1], 0, 32);
	range_desc->last_id = mc_dec(cmd.params[1], 32, 32);

	return 0;
}
EXPORT_SYMBOL(dprc_get_res_ids);

int dprc_get_portal_paddr(struct fsl_mc_io *mc_io,
			  uint16_t token,
			  int portal_id,
			  uint64_t *portal_addr)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_PORTAL_PADDR,
					  MC_CMD_PRI_LOW, token);
	cmd.params[0] |= mc_enc(0, 32, portal_id);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	*portal_addr = mc_dec(cmd.params[1], 0, 64);

	return 0;
}
EXPORT_SYMBOL(dprc_get_portal_paddr);

int dprc_get_obj_region(struct fsl_mc_io *mc_io,
			uint16_t token,
			char *obj_type,
			int obj_id,
			uint8_t region_index,
			struct dprc_region_desc *region_desc)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_OBJ_REG,
					  MC_CMD_PRI_LOW, token);
	cmd.params[0] |= mc_enc(0, 32, obj_id);
	cmd.params[0] |= mc_enc(48, 8, region_index);
	cmd.params[3] |= mc_enc(0, 8, obj_type[0]);
	cmd.params[3] |= mc_enc(8, 8, obj_type[1]);
	cmd.params[3] |= mc_enc(16, 8, obj_type[2]);
	cmd.params[3] |= mc_enc(24, 8, obj_type[3]);
	cmd.params[3] |= mc_enc(32, 8, obj_type[4]);
	cmd.params[3] |= mc_enc(40, 8, obj_type[5]);
	cmd.params[3] |= mc_enc(48, 8, obj_type[6]);
	cmd.params[3] |= mc_enc(56, 8, obj_type[7]);
	cmd.params[4] |= mc_enc(0, 8, obj_type[8]);
	cmd.params[4] |= mc_enc(8, 8, obj_type[9]);
	cmd.params[4] |= mc_enc(16, 8, obj_type[10]);
	cmd.params[4] |= mc_enc(24, 8, obj_type[11]);
	cmd.params[4] |= mc_enc(32, 8, obj_type[12]);
	cmd.params[4] |= mc_enc(40, 8, obj_type[13]);
	cmd.params[4] |= mc_enc(48, 8, obj_type[14]);
	cmd.params[4] |= mc_enc(56, 8, '\0');

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	region_desc->base_paddr = mc_dec(cmd.params[1], 0, 64);
	region_desc->size = mc_dec(cmd.params[2], 0, 32);

	return 0;
}
EXPORT_SYMBOL(dprc_get_obj_region);

int dprc_connect(struct fsl_mc_io *mc_io,
		 uint16_t token,
		 const struct dprc_endpoint *endpoint1,
		 const struct dprc_endpoint *endpoint2)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_CONNECT,
					  MC_CMD_PRI_LOW,
					  token);
	cmd.params[0] |= mc_enc(0, 32, endpoint1->id);
	cmd.params[0] |= mc_enc(32, 32, endpoint1->interface_id);
	cmd.params[1] |= mc_enc(0, 32, endpoint2->id);
	cmd.params[1] |= mc_enc(32, 32, endpoint2->interface_id);
	cmd.params[2] |= mc_enc(0, 8, endpoint1->type[0]);
	cmd.params[2] |= mc_enc(8, 8, endpoint1->type[1]);
	cmd.params[2] |= mc_enc(16, 8, endpoint1->type[2]);
	cmd.params[2] |= mc_enc(24, 8, endpoint1->type[3]);
	cmd.params[2] |= mc_enc(32, 8, endpoint1->type[4]);
	cmd.params[2] |= mc_enc(40, 8, endpoint1->type[5]);
	cmd.params[2] |= mc_enc(48, 8, endpoint1->type[6]);
	cmd.params[2] |= mc_enc(56, 8, endpoint1->type[7]);
	cmd.params[3] |= mc_enc(0, 8, endpoint1->type[8]);
	cmd.params[3] |= mc_enc(8, 8, endpoint1->type[9]);
	cmd.params[3] |= mc_enc(16, 8, endpoint1->type[10]);
	cmd.params[3] |= mc_enc(24, 8, endpoint1->type[11]);
	cmd.params[3] |= mc_enc(32, 8, endpoint1->type[12]);
	cmd.params[3] |= mc_enc(40, 8, endpoint1->type[13]);
	cmd.params[3] |= mc_enc(48, 8, endpoint1->type[14]);
	cmd.params[3] |= mc_enc(56, 8, endpoint1->type[15]);
	cmd.params[5] |= mc_enc(0, 8, endpoint2->type[0]);
	cmd.params[5] |= mc_enc(8, 8, endpoint2->type[1]);
	cmd.params[5] |= mc_enc(16, 8, endpoint2->type[2]);
	cmd.params[5] |= mc_enc(24, 8, endpoint2->type[3]);
	cmd.params[5] |= mc_enc(32, 8, endpoint2->type[4]);
	cmd.params[5] |= mc_enc(40, 8, endpoint2->type[5]);
	cmd.params[5] |= mc_enc(48, 8, endpoint2->type[6]);
	cmd.params[5] |= mc_enc(56, 8, endpoint2->type[7]);
	cmd.params[6] |= mc_enc(0, 8, endpoint2->type[8]);
	cmd.params[6] |= mc_enc(8, 8, endpoint2->type[9]);
	cmd.params[6] |= mc_enc(16, 8, endpoint2->type[10]);
	cmd.params[6] |= mc_enc(24, 8, endpoint2->type[11]);
	cmd.params[6] |= mc_enc(32, 8, endpoint2->type[12]);
	cmd.params[6] |= mc_enc(40, 8, endpoint2->type[13]);
	cmd.params[6] |= mc_enc(48, 8, endpoint2->type[14]);
	cmd.params[6] |= mc_enc(56, 8, endpoint2->type[15]);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

int dprc_disconnect(struct fsl_mc_io *mc_io,
		    uint16_t token,
		    const struct dprc_endpoint *endpoint)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_DISCONNECT,
					  MC_CMD_PRI_LOW,
					  token);
	cmd.params[0] |= mc_enc(0, 32, endpoint->id);
	cmd.params[0] |= mc_enc(32, 32, endpoint->interface_id);
	cmd.params[1] |= mc_enc(0, 8, endpoint->type[0]);
	cmd.params[1] |= mc_enc(8, 8, endpoint->type[1]);
	cmd.params[1] |= mc_enc(16, 8, endpoint->type[2]);
	cmd.params[1] |= mc_enc(24, 8, endpoint->type[3]);
	cmd.params[1] |= mc_enc(32, 8, endpoint->type[4]);
	cmd.params[1] |= mc_enc(40, 8, endpoint->type[5]);
	cmd.params[1] |= mc_enc(48, 8, endpoint->type[6]);
	cmd.params[1] |= mc_enc(56, 8, endpoint->type[7]);
	cmd.params[2] |= mc_enc(0, 8, endpoint->type[8]);
	cmd.params[2] |= mc_enc(8, 8, endpoint->type[9]);
	cmd.params[2] |= mc_enc(16, 8, endpoint->type[10]);
	cmd.params[2] |= mc_enc(24, 8, endpoint->type[11]);
	cmd.params[2] |= mc_enc(32, 8, endpoint->type[12]);
	cmd.params[2] |= mc_enc(40, 8, endpoint->type[13]);
	cmd.params[2] |= mc_enc(48, 8, endpoint->type[14]);
	cmd.params[2] |= mc_enc(56, 8, endpoint->type[15]);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

int dprc_get_connection(struct fsl_mc_io *mc_io,
			uint16_t token,
					const struct dprc_endpoint *endpoint1,
					struct dprc_endpoint *endpoint2,
					int *state)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_CONNECTION,
					  MC_CMD_PRI_LOW,
					  token);
	cmd.params[0] |= mc_enc(0, 32, endpoint1->id);
	cmd.params[0] |= mc_enc(32, 32, endpoint1->interface_id);
	cmd.params[1] |= mc_enc(0, 8, endpoint1->type[0]);
	cmd.params[1] |= mc_enc(8, 8, endpoint1->type[1]);
	cmd.params[1] |= mc_enc(16, 8, endpoint1->type[2]);
	cmd.params[1] |= mc_enc(24, 8, endpoint1->type[3]);
	cmd.params[1] |= mc_enc(32, 8, endpoint1->type[4]);
	cmd.params[1] |= mc_enc(40, 8, endpoint1->type[5]);
	cmd.params[1] |= mc_enc(48, 8, endpoint1->type[6]);
	cmd.params[1] |= mc_enc(56, 8, endpoint1->type[7]);
	cmd.params[2] |= mc_enc(0, 8, endpoint1->type[8]);
	cmd.params[2] |= mc_enc(8, 8, endpoint1->type[9]);
	cmd.params[2] |= mc_enc(16, 8, endpoint1->type[10]);
	cmd.params[2] |= mc_enc(24, 8, endpoint1->type[11]);
	cmd.params[2] |= mc_enc(32, 8, endpoint1->type[12]);
	cmd.params[2] |= mc_enc(40, 8, endpoint1->type[13]);
	cmd.params[2] |= mc_enc(48, 8, endpoint1->type[14]);
	cmd.params[2] |= mc_enc(56, 8, endpoint1->type[15]);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	endpoint2->id = mc_dec(cmd.params[3], 0, 32);
	endpoint2->interface_id = mc_dec(cmd.params[3], 32, 32);
	endpoint2->type[0] = mc_dec(cmd.params[4], 0, 8);
	endpoint2->type[1] = mc_dec(cmd.params[4], 8, 8);
	endpoint2->type[2] = mc_dec(cmd.params[4], 16, 8);
	endpoint2->type[3] = mc_dec(cmd.params[4], 24, 8);
	endpoint2->type[4] = mc_dec(cmd.params[4], 32, 8);
	endpoint2->type[5] = mc_dec(cmd.params[4], 40, 8);
	endpoint2->type[6] = mc_dec(cmd.params[4], 48, 8);
	endpoint2->type[7] = mc_dec(cmd.params[4], 56, 8);
	endpoint2->type[8] = mc_dec(cmd.params[5], 0, 8);
	endpoint2->type[9] = mc_dec(cmd.params[5], 8, 8);
	endpoint2->type[10] = mc_dec(cmd.params[5], 16, 8);
	endpoint2->type[11] = mc_dec(cmd.params[5], 24, 8);
	endpoint2->type[12] = mc_dec(cmd.params[5], 32, 8);
	endpoint2->type[13] = mc_dec(cmd.params[5], 40, 8);
	endpoint2->type[14] = mc_dec(cmd.params[5], 48, 8);
	endpoint2->type[15] = mc_dec(cmd.params[5], 56, 8);
	*state = mc_dec(cmd.params[6], 0, 32);

	return 0;
}

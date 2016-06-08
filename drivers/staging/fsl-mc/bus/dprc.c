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

/**
 * dprc_open() - Open DPRC object for use
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @container_id: Container ID to open
 * @token:	Returned token of DPRC object
 *
 * Return:	'0' on Success; Error code otherwise.
 *
 * @warning	Required before any operation on the object.
 */
int dprc_open(struct fsl_mc_io *mc_io,
	      u32 cmd_flags,
	      int container_id,
	      u16 *token)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_OPEN, cmd_flags,
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

/**
 * dprc_close() - Close the control session of the object
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 *
 * After this function is called, no further operations are
 * allowed on the object without opening a new control session.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_close(struct fsl_mc_io *mc_io,
	       u32 cmd_flags,
	       u16 token)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_CLOSE, cmd_flags,
					  token);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}
EXPORT_SYMBOL(dprc_close);

/**
 * dprc_create_container() - Create child container
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @cfg:	Child container configuration
 * @child_container_id:	Returned child container ID
 * @child_portal_offset: Returned child portal offset from MC portal base
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_create_container(struct fsl_mc_io *mc_io,
			  u32 cmd_flags,
			  u16 token,
			  struct dprc_cfg *cfg,
			  int *child_container_id,
			  u64 *child_portal_offset)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.params[0] |= mc_enc(32, 16, cfg->icid);
	cmd.params[0] |= mc_enc(0, 32, cfg->options);
	cmd.params[1] |= mc_enc(32, 32, cfg->portal_id);
	cmd.params[2] |= mc_enc(0, 8, cfg->label[0]);
	cmd.params[2] |= mc_enc(8, 8, cfg->label[1]);
	cmd.params[2] |= mc_enc(16, 8, cfg->label[2]);
	cmd.params[2] |= mc_enc(24, 8, cfg->label[3]);
	cmd.params[2] |= mc_enc(32, 8, cfg->label[4]);
	cmd.params[2] |= mc_enc(40, 8, cfg->label[5]);
	cmd.params[2] |= mc_enc(48, 8, cfg->label[6]);
	cmd.params[2] |= mc_enc(56, 8, cfg->label[7]);
	cmd.params[3] |= mc_enc(0, 8, cfg->label[8]);
	cmd.params[3] |= mc_enc(8, 8, cfg->label[9]);
	cmd.params[3] |= mc_enc(16, 8, cfg->label[10]);
	cmd.params[3] |= mc_enc(24, 8, cfg->label[11]);
	cmd.params[3] |= mc_enc(32, 8, cfg->label[12]);
	cmd.params[3] |= mc_enc(40, 8, cfg->label[13]);
	cmd.params[3] |= mc_enc(48, 8, cfg->label[14]);
	cmd.params[3] |= mc_enc(56, 8, cfg->label[15]);

	cmd.header = mc_encode_cmd_header(DPRC_CMDID_CREATE_CONT,
					  cmd_flags, token);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	*child_container_id = mc_dec(cmd.params[1], 0, 32);
	*child_portal_offset = mc_dec(cmd.params[2], 0, 64);

	return 0;
}

/**
 * dprc_destroy_container() - Destroy child container.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @child_container_id:	ID of the container to destroy
 *
 * This function terminates the child container, so following this call the
 * child container ID becomes invalid.
 *
 * Notes:
 * - All resources and objects of the destroyed container are returned to the
 * parent container or destroyed if were created be the destroyed container.
 * - This function destroy all the child containers of the specified
 *   container prior to destroying the container itself.
 *
 * warning: Only the parent container is allowed to destroy a child policy
 *		Container 0 can't be destroyed
 *
 * Return:	'0' on Success; Error code otherwise.
 *
 */
int dprc_destroy_container(struct fsl_mc_io *mc_io,
			   u32 cmd_flags,
			   u16 token,
			   int child_container_id)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_DESTROY_CONT,
					  cmd_flags, token);
	cmd.params[0] |= mc_enc(0, 32, child_container_id);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dprc_reset_container - Reset child container.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @child_container_id:	ID of the container to reset
 *
 * In case a software context crashes or becomes non-responsive, the parent
 * may wish to reset its resources container before the software context is
 * restarted.
 *
 * This routine informs all objects assigned to the child container that the
 * container is being reset, so they may perform any cleanup operations that are
 * needed. All objects handles that were owned by the child container shall be
 * closed.
 *
 * Note that such request may be submitted even if the child software context
 * has not crashed, but the resulting object cleanup operations will not be
 * aware of that.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_reset_container(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 token,
			 int child_container_id)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_RESET_CONT,
					  cmd_flags, token);
	cmd.params[0] |= mc_enc(0, 32, child_container_id);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dprc_get_irq() - Get IRQ information from the DPRC.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @irq_index:	The interrupt index to configure
 * @type:	Interrupt type: 0 represents message interrupt
 *		type (both irq_addr and irq_val are valid)
 * @irq_cfg:	IRQ attributes
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_irq(struct fsl_mc_io *mc_io,
		 u32 cmd_flags,
		 u16 token,
		 u8 irq_index,
		 int *type,
		 struct dprc_irq_cfg *irq_cfg)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_IRQ,
					  cmd_flags,
					  token);
	cmd.params[0] |= mc_enc(32, 8, irq_index);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	irq_cfg->val = mc_dec(cmd.params[0], 0, 32);
	irq_cfg->paddr = mc_dec(cmd.params[1], 0, 64);
	irq_cfg->irq_num = mc_dec(cmd.params[2], 0, 32);
	*type = mc_dec(cmd.params[2], 32, 32);

	return 0;
}

/**
 * dprc_set_irq() - Set IRQ information for the DPRC to trigger an interrupt.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @irq_index:	Identifies the interrupt index to configure
 * @irq_cfg:	IRQ configuration
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_set_irq(struct fsl_mc_io *mc_io,
		 u32 cmd_flags,
		 u16 token,
		 u8 irq_index,
		 struct dprc_irq_cfg *irq_cfg)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_SET_IRQ,
					  cmd_flags,
					  token);
	cmd.params[0] |= mc_enc(32, 8, irq_index);
	cmd.params[0] |= mc_enc(0, 32, irq_cfg->val);
	cmd.params[1] |= mc_enc(0, 64, irq_cfg->paddr);
	cmd.params[2] |= mc_enc(0, 32, irq_cfg->irq_num);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dprc_get_irq_enable() - Get overall interrupt state.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @irq_index:  The interrupt index to configure
 * @en:		Returned interrupt state - enable = 1, disable = 0
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_irq_enable(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			u8 irq_index,
			u8 *en)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_IRQ_ENABLE,
					  cmd_flags, token);
	cmd.params[0] |= mc_enc(32, 8, irq_index);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	*en = mc_dec(cmd.params[0], 0, 8);

	return 0;
}

/**
 * dprc_set_irq_enable() - Set overall interrupt state.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @irq_index:	The interrupt index to configure
 * @en:		Interrupt state - enable = 1, disable = 0
 *
 * Allows GPP software to control when interrupts are generated.
 * Each interrupt can have up to 32 causes.  The enable/disable control's the
 * overall interrupt state. if the interrupt is disabled no causes will cause
 * an interrupt.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_set_irq_enable(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			u8 irq_index,
			u8 en)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_SET_IRQ_ENABLE,
					  cmd_flags, token);
	cmd.params[0] |= mc_enc(0, 8, en);
	cmd.params[0] |= mc_enc(32, 8, irq_index);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dprc_get_irq_mask() - Get interrupt mask.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @irq_index:	The interrupt index to configure
 * @mask:	Returned event mask to trigger interrupt
 *
 * Every interrupt can have up to 32 causes and the interrupt model supports
 * masking/unmasking each cause independently
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_irq_mask(struct fsl_mc_io *mc_io,
		      u32 cmd_flags,
		      u16 token,
		      u8 irq_index,
		      u32 *mask)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_IRQ_MASK,
					  cmd_flags, token);
	cmd.params[0] |= mc_enc(32, 8, irq_index);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	*mask = mc_dec(cmd.params[0], 0, 32);

	return 0;
}

/**
 * dprc_set_irq_mask() - Set interrupt mask.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @irq_index:	The interrupt index to configure
 * @mask:	event mask to trigger interrupt;
 *			each bit:
 *				0 = ignore event
 *				1 = consider event for asserting irq
 *
 * Every interrupt can have up to 32 causes and the interrupt model supports
 * masking/unmasking each cause independently
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_set_irq_mask(struct fsl_mc_io *mc_io,
		      u32 cmd_flags,
		      u16 token,
		      u8 irq_index,
		      u32 mask)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_SET_IRQ_MASK,
					  cmd_flags, token);
	cmd.params[0] |= mc_enc(0, 32, mask);
	cmd.params[0] |= mc_enc(32, 8, irq_index);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dprc_get_irq_status() - Get the current status of any pending interrupts.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @irq_index:	The interrupt index to configure
 * @status:	Returned interrupts status - one bit per cause:
 *			0 = no interrupt pending
 *			1 = interrupt pending
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_irq_status(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			u8 irq_index,
			u32 *status)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_IRQ_STATUS,
					  cmd_flags, token);
	cmd.params[0] |= mc_enc(0, 32, *status);
	cmd.params[0] |= mc_enc(32, 8, irq_index);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	*status = mc_dec(cmd.params[0], 0, 32);

	return 0;
}

/**
 * dprc_clear_irq_status() - Clear a pending interrupt's status
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @irq_index:	The interrupt index to configure
 * @status:	bits to clear (W1C) - one bit per cause:
 *					0 = don't change
 *					1 = clear status bit
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_clear_irq_status(struct fsl_mc_io *mc_io,
			  u32 cmd_flags,
			  u16 token,
			  u8 irq_index,
			  u32 status)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_CLEAR_IRQ_STATUS,
					  cmd_flags, token);
	cmd.params[0] |= mc_enc(0, 32, status);
	cmd.params[0] |= mc_enc(32, 8, irq_index);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dprc_get_attributes() - Obtains container attributes
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @attributes	Returned container attributes
 *
 * Return:     '0' on Success; Error code otherwise.
 */
int dprc_get_attributes(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			struct dprc_attributes *attr)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_ATTR,
					  cmd_flags,
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

/**
 * dprc_set_res_quota() - Set allocation policy for a specific resource/object
 *		type in a child container
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @child_container_id:	ID of the child container
 * @type:	Resource/object type
 * @quota:	Sets the maximum number of resources of	the selected type
 *		that the child container is allowed to allocate from its parent;
 *		when quota is set to -1, the policy is the same as container's
 *		general policy.
 *
 * Allocation policy determines whether or not a container may allocate
 * resources from its parent. Each container has a 'global' allocation policy
 * that is set when the container is created.
 *
 * This function sets allocation policy for a specific resource type.
 * The default policy for all resource types matches the container's 'global'
 * allocation policy.
 *
 * Return:	'0' on Success; Error code otherwise.
 *
 * @warning	Only the parent container is allowed to change a child policy.
 */
int dprc_set_res_quota(struct fsl_mc_io *mc_io,
		       u32 cmd_flags,
		       u16 token,
		       int child_container_id,
		       char *type,
		       u16 quota)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_SET_RES_QUOTA,
					  cmd_flags, token);
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

/**
 * dprc_get_res_quota() - Gets the allocation policy of a specific
 *		resource/object type in a child container
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @child_container_id;	ID of the child container
 * @type:	resource/object type
 * @quota:	Returnes the maximum number of resources of the selected type
 *		that the child container is allowed to allocate from the parent;
 *		when quota is set to -1, the policy is the same as container's
 *		general policy.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_res_quota(struct fsl_mc_io *mc_io,
		       u32 cmd_flags,
		       u16 token,
		       int child_container_id,
		       char *type,
		       u16 *quota)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_RES_QUOTA,
					  cmd_flags, token);
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

/**
 * dprc_assign() - Assigns objects or resource to a child container.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @container_id: ID of the child container
 * @res_req:	Describes the type and amount of resources to
 *			assign to the given container
 *
 * Assignment is usually done by a parent (this DPRC) to one of its child
 * containers.
 *
 * According to the DPRC allocation policy, the assigned resources may be taken
 * (allocated) from the container's ancestors, if not enough resources are
 * available in the container itself.
 *
 * The type of assignment depends on the dprc_res_req options, as follows:
 * - DPRC_RES_REQ_OPT_EXPLICIT: indicates that assigned resources should have
 *   the explicit base ID specified at the id_base_align field of res_req.
 * - DPRC_RES_REQ_OPT_ALIGNED: indicates that the assigned resources should be
 *   aligned to the value given at id_base_align field of res_req.
 * - DPRC_RES_REQ_OPT_PLUGGED: Relevant only for object assignment,
 *   and indicates that the object must be set to the plugged state.
 *
 * A container may use this function with its own ID in order to change a
 * object state to plugged or unplugged.
 *
 * If IRQ information has been set in the child DPRC, it will signal an
 * interrupt following every change in its object assignment.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_assign(struct fsl_mc_io *mc_io,
		u32 cmd_flags,
		u16 token,
		int container_id,
		struct dprc_res_req *res_req)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_ASSIGN,
					  cmd_flags, token);
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

/**
 * dprc_unassign() - Un-assigns objects or resources from a child container
 *		and moves them into this (parent) DPRC.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @child_container_id:	ID of the child container
 * @res_req:	Describes the type and amount of resources to un-assign from
 *		the child container
 *
 * Un-assignment of objects can succeed only if the object is not in the
 * plugged or opened state.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_unassign(struct fsl_mc_io *mc_io,
		  u32 cmd_flags,
		  u16 token,
		  int child_container_id,
		  struct dprc_res_req *res_req)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_UNASSIGN,
					  cmd_flags,
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

/**
 * dprc_get_pool_count() - Get the number of dprc's pools
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @pool_count:	Returned number of resource pools in the dprc
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_pool_count(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			int *pool_count)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_POOL_COUNT,
					  cmd_flags, token);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	*pool_count = mc_dec(cmd.params[0], 0, 32);

	return 0;
}

/**
 * dprc_get_pool() - Get the type (string) of a certain dprc's pool
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @pool_index;	Index of the pool to be queried (< pool_count)
 * @type:	The type of the pool
 *
 * The pool types retrieved one by one by incrementing
 * pool_index up to (not including) the value of pool_count returned
 * from dprc_get_pool_count(). dprc_get_pool_count() must
 * be called prior to dprc_get_pool().
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_pool(struct fsl_mc_io *mc_io,
		  u32 cmd_flags,
		  u16 token,
		  int pool_index,
		  char *type)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_POOL,
					  cmd_flags,
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

/**
 * dprc_get_obj_count() - Obtains the number of objects in the DPRC
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @obj_count:	Number of objects assigned to the DPRC
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_obj_count(struct fsl_mc_io *mc_io,
		       u32 cmd_flags,
		       u16 token,
		       int *obj_count)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_OBJ_COUNT,
					  cmd_flags, token);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	*obj_count = mc_dec(cmd.params[0], 32, 32);

	return 0;
}
EXPORT_SYMBOL(dprc_get_obj_count);

/**
 * dprc_get_obj() - Get general information on an object
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @obj_index:	Index of the object to be queried (< obj_count)
 * @obj_desc:	Returns the requested object descriptor
 *
 * The object descriptors are retrieved one by one by incrementing
 * obj_index up to (not including) the value of obj_count returned
 * from dprc_get_obj_count(). dprc_get_obj_count() must
 * be called prior to dprc_get_obj().
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_obj(struct fsl_mc_io *mc_io,
		 u32 cmd_flags,
		 u16 token,
		 int obj_index,
		 struct dprc_obj_desc *obj_desc)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_OBJ,
					  cmd_flags,
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
	obj_desc->flags = mc_dec(cmd.params[2], 32, 16);
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
	obj_desc->label[0] = mc_dec(cmd.params[5], 0, 8);
	obj_desc->label[1] = mc_dec(cmd.params[5], 8, 8);
	obj_desc->label[2] = mc_dec(cmd.params[5], 16, 8);
	obj_desc->label[3] = mc_dec(cmd.params[5], 24, 8);
	obj_desc->label[4] = mc_dec(cmd.params[5], 32, 8);
	obj_desc->label[5] = mc_dec(cmd.params[5], 40, 8);
	obj_desc->label[6] = mc_dec(cmd.params[5], 48, 8);
	obj_desc->label[7] = mc_dec(cmd.params[5], 56, 8);
	obj_desc->label[8] = mc_dec(cmd.params[6], 0, 8);
	obj_desc->label[9] = mc_dec(cmd.params[6], 8, 8);
	obj_desc->label[10] = mc_dec(cmd.params[6], 16, 8);
	obj_desc->label[11] = mc_dec(cmd.params[6], 24, 8);
	obj_desc->label[12] = mc_dec(cmd.params[6], 32, 8);
	obj_desc->label[13] = mc_dec(cmd.params[6], 40, 8);
	obj_desc->label[14] = mc_dec(cmd.params[6], 48, 8);
	obj_desc->label[15] = '\0';
	return 0;
}
EXPORT_SYMBOL(dprc_get_obj);

/**
 * dprc_get_obj_desc() - Get object descriptor.
 *
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @obj_type:	The type of the object to get its descriptor.
 * @obj_id:	The id of the object to get its descriptor
 * @obj_desc:	The returned descriptor to fill and return to the user
 *
 * Return:	'0' on Success; Error code otherwise.
 *
 */
int dprc_get_obj_desc(struct fsl_mc_io *mc_io,
		      u32 cmd_flags,
		      u16 token,
		      char *obj_type,
		      int obj_id,
		      struct dprc_obj_desc *obj_desc)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_OBJ_DESC,
					  cmd_flags,
					  token);
	cmd.params[0] |= mc_enc(0, 32, obj_id);
	cmd.params[1] |= mc_enc(0, 8, obj_type[0]);
	cmd.params[1] |= mc_enc(8, 8, obj_type[1]);
	cmd.params[1] |= mc_enc(16, 8, obj_type[2]);
	cmd.params[1] |= mc_enc(24, 8, obj_type[3]);
	cmd.params[1] |= mc_enc(32, 8, obj_type[4]);
	cmd.params[1] |= mc_enc(40, 8, obj_type[5]);
	cmd.params[1] |= mc_enc(48, 8, obj_type[6]);
	cmd.params[1] |= mc_enc(56, 8, obj_type[7]);
	cmd.params[2] |= mc_enc(0, 8, obj_type[8]);
	cmd.params[2] |= mc_enc(8, 8, obj_type[9]);
	cmd.params[2] |= mc_enc(16, 8, obj_type[10]);
	cmd.params[2] |= mc_enc(24, 8, obj_type[11]);
	cmd.params[2] |= mc_enc(32, 8, obj_type[12]);
	cmd.params[2] |= mc_enc(40, 8, obj_type[13]);
	cmd.params[2] |= mc_enc(48, 8, obj_type[14]);
	cmd.params[2] |= mc_enc(56, 8, obj_type[15]);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	obj_desc->id = (int)mc_dec(cmd.params[0], 32, 32);
	obj_desc->vendor = (u16)mc_dec(cmd.params[1], 0, 16);
	obj_desc->vendor = (u8)mc_dec(cmd.params[1], 16, 8);
	obj_desc->region_count = (u8)mc_dec(cmd.params[1], 24, 8);
	obj_desc->state = (u32)mc_dec(cmd.params[1], 32, 32);
	obj_desc->ver_major = (u16)mc_dec(cmd.params[2], 0, 16);
	obj_desc->ver_minor = (u16)mc_dec(cmd.params[2], 16, 16);
	obj_desc->flags = mc_dec(cmd.params[2], 32, 16);
	obj_desc->type[0] = (char)mc_dec(cmd.params[3], 0, 8);
	obj_desc->type[1] = (char)mc_dec(cmd.params[3], 8, 8);
	obj_desc->type[2] = (char)mc_dec(cmd.params[3], 16, 8);
	obj_desc->type[3] = (char)mc_dec(cmd.params[3], 24, 8);
	obj_desc->type[4] = (char)mc_dec(cmd.params[3], 32, 8);
	obj_desc->type[5] = (char)mc_dec(cmd.params[3], 40, 8);
	obj_desc->type[6] = (char)mc_dec(cmd.params[3], 48, 8);
	obj_desc->type[7] = (char)mc_dec(cmd.params[3], 56, 8);
	obj_desc->type[8] = (char)mc_dec(cmd.params[4], 0, 8);
	obj_desc->type[9] = (char)mc_dec(cmd.params[4], 8, 8);
	obj_desc->type[10] = (char)mc_dec(cmd.params[4], 16, 8);
	obj_desc->type[11] = (char)mc_dec(cmd.params[4], 24, 8);
	obj_desc->type[12] = (char)mc_dec(cmd.params[4], 32, 8);
	obj_desc->type[13] = (char)mc_dec(cmd.params[4], 40, 8);
	obj_desc->type[14] = (char)mc_dec(cmd.params[4], 48, 8);
	obj_desc->type[15] = (char)mc_dec(cmd.params[4], 56, 8);
	obj_desc->label[0] = (char)mc_dec(cmd.params[5], 0, 8);
	obj_desc->label[1] = (char)mc_dec(cmd.params[5], 8, 8);
	obj_desc->label[2] = (char)mc_dec(cmd.params[5], 16, 8);
	obj_desc->label[3] = (char)mc_dec(cmd.params[5], 24, 8);
	obj_desc->label[4] = (char)mc_dec(cmd.params[5], 32, 8);
	obj_desc->label[5] = (char)mc_dec(cmd.params[5], 40, 8);
	obj_desc->label[6] = (char)mc_dec(cmd.params[5], 48, 8);
	obj_desc->label[7] = (char)mc_dec(cmd.params[5], 56, 8);
	obj_desc->label[8] = (char)mc_dec(cmd.params[6], 0, 8);
	obj_desc->label[9] = (char)mc_dec(cmd.params[6], 8, 8);
	obj_desc->label[10] = (char)mc_dec(cmd.params[6], 16, 8);
	obj_desc->label[11] = (char)mc_dec(cmd.params[6], 24, 8);
	obj_desc->label[12] = (char)mc_dec(cmd.params[6], 32, 8);
	obj_desc->label[13] = (char)mc_dec(cmd.params[6], 40, 8);
	obj_desc->label[14] = (char)mc_dec(cmd.params[6], 48, 8);
	obj_desc->label[15] = (char)mc_dec(cmd.params[6], 56, 8);

	return 0;
}
EXPORT_SYMBOL(dprc_get_obj_desc);

/**
 * dprc_set_obj_irq() - Set IRQ information for object to trigger an interrupt.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @obj_type:	Type of the object to set its IRQ
 * @obj_id:	ID of the object to set its IRQ
 * @irq_index:	The interrupt index to configure
 * @irq_cfg:	IRQ configuration
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_set_obj_irq(struct fsl_mc_io *mc_io,
		     u32 cmd_flags,
		     u16 token,
		     char *obj_type,
		     int obj_id,
		     u8 irq_index,
		     struct dprc_irq_cfg *irq_cfg)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_SET_OBJ_IRQ,
					  cmd_flags,
					  token);
	cmd.params[0] |= mc_enc(32, 8, irq_index);
	cmd.params[0] |= mc_enc(0, 32, irq_cfg->val);
	cmd.params[1] |= mc_enc(0, 64, irq_cfg->paddr);
	cmd.params[2] |= mc_enc(0, 32, irq_cfg->irq_num);
	cmd.params[2] |= mc_enc(32, 32, obj_id);
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
	cmd.params[4] |= mc_enc(56, 8, obj_type[15]);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}
EXPORT_SYMBOL(dprc_set_obj_irq);

/**
 * dprc_get_obj_irq() - Get IRQ information from object.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @obj_type:	Type od the object to get its IRQ
 * @obj_id:	ID of the object to get its IRQ
 * @irq_index:	The interrupt index to configure
 * @type:	Interrupt type: 0 represents message interrupt
 *		type (both irq_addr and irq_val are valid)
 * @irq_cfg:	The returned IRQ attributes
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_obj_irq(struct fsl_mc_io *mc_io,
		     u32 cmd_flags,
		     u16 token,
		     char *obj_type,
		     int obj_id,
		     u8 irq_index,
		     int *type,
		     struct dprc_irq_cfg *irq_cfg)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_OBJ_IRQ,
					  cmd_flags,
					  token);
	cmd.params[0] |= mc_enc(0, 32, obj_id);
	cmd.params[0] |= mc_enc(32, 8, irq_index);
	cmd.params[1] |= mc_enc(0, 8, obj_type[0]);
	cmd.params[1] |= mc_enc(8, 8, obj_type[1]);
	cmd.params[1] |= mc_enc(16, 8, obj_type[2]);
	cmd.params[1] |= mc_enc(24, 8, obj_type[3]);
	cmd.params[1] |= mc_enc(32, 8, obj_type[4]);
	cmd.params[1] |= mc_enc(40, 8, obj_type[5]);
	cmd.params[1] |= mc_enc(48, 8, obj_type[6]);
	cmd.params[1] |= mc_enc(56, 8, obj_type[7]);
	cmd.params[2] |= mc_enc(0, 8, obj_type[8]);
	cmd.params[2] |= mc_enc(8, 8, obj_type[9]);
	cmd.params[2] |= mc_enc(16, 8, obj_type[10]);
	cmd.params[2] |= mc_enc(24, 8, obj_type[11]);
	cmd.params[2] |= mc_enc(32, 8, obj_type[12]);
	cmd.params[2] |= mc_enc(40, 8, obj_type[13]);
	cmd.params[2] |= mc_enc(48, 8, obj_type[14]);
	cmd.params[2] |= mc_enc(56, 8, obj_type[15]);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	irq_cfg->val = (u32)mc_dec(cmd.params[0], 0, 32);
	irq_cfg->paddr = (u64)mc_dec(cmd.params[1], 0, 64);
	irq_cfg->irq_num = (int)mc_dec(cmd.params[2], 0, 32);
	*type = (int)mc_dec(cmd.params[2], 32, 32);

	return 0;
}
EXPORT_SYMBOL(dprc_get_obj_irq);

/**
 * dprc_get_res_count() - Obtains the number of free resources that are assigned
 *		to this container, by pool type
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @type:	pool type
 * @res_count:	Returned number of free resources of the given
 *			resource type that are assigned to this DPRC
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_res_count(struct fsl_mc_io *mc_io,
		       u32 cmd_flags,
		       u16 token,
		       char *type,
		       int *res_count)
{
	struct mc_command cmd = { 0 };
	int err;

	*res_count = 0;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_RES_COUNT,
					  cmd_flags, token);
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

/**
 * dprc_get_res_ids() - Obtains IDs of free resources in the container
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @type:	pool type
 * @range_desc:	range descriptor
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_res_ids(struct fsl_mc_io *mc_io,
		     u32 cmd_flags,
		     u16 token,
		     char *type,
		     struct dprc_res_ids_range_desc *range_desc)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_RES_IDS,
					  cmd_flags, token);
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

/**
 * dprc_get_obj_region() - Get region information for a specified object.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @obj_type;	Object type as returned in dprc_get_obj()
 * @obj_id:	Unique object instance as returned in dprc_get_obj()
 * @region_index: The specific region to query
 * @region_desc:  Returns the requested region descriptor
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_obj_region(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			char *obj_type,
			int obj_id,
			u8 region_index,
			struct dprc_region_desc *region_desc)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_OBJ_REG,
					  cmd_flags, token);
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
	region_desc->base_offset = mc_dec(cmd.params[1], 0, 64);
	region_desc->size = mc_dec(cmd.params[2], 0, 32);

	return 0;
}
EXPORT_SYMBOL(dprc_get_obj_region);

/**
 * dprc_set_obj_label() - Set object label.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @obj_type:	Object's type
 * @obj_id:	Object's ID
 * @label:	The required label. The maximum length is 16 chars.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_set_obj_label(struct fsl_mc_io *mc_io,
		       u32 cmd_flags,
		       u16  token,
		       char *obj_type,
		       int  obj_id,
		       char *label)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_SET_OBJ_LABEL,
					  cmd_flags,
					  token);

	cmd.params[0] |= mc_enc(0, 32, obj_id);
	cmd.params[1] |= mc_enc(0, 8, label[0]);
	cmd.params[1] |= mc_enc(8, 8, label[1]);
	cmd.params[1] |= mc_enc(16, 8, label[2]);
	cmd.params[1] |= mc_enc(24, 8, label[3]);
	cmd.params[1] |= mc_enc(32, 8, label[4]);
	cmd.params[1] |= mc_enc(40, 8, label[5]);
	cmd.params[1] |= mc_enc(48, 8, label[6]);
	cmd.params[1] |= mc_enc(56, 8, label[7]);
	cmd.params[2] |= mc_enc(0, 8, label[8]);
	cmd.params[2] |= mc_enc(8, 8, label[9]);
	cmd.params[2] |= mc_enc(16, 8, label[10]);
	cmd.params[2] |= mc_enc(24, 8, label[11]);
	cmd.params[2] |= mc_enc(32, 8, label[12]);
	cmd.params[2] |= mc_enc(40, 8, label[13]);
	cmd.params[2] |= mc_enc(48, 8, label[14]);
	cmd.params[2] |= mc_enc(56, 8, label[15]);
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
	cmd.params[4] |= mc_enc(56, 8, obj_type[15]);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}
EXPORT_SYMBOL(dprc_set_obj_label);

/**
 * dprc_connect() - Connect two endpoints to create a network link between them
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @endpoint1:	Endpoint 1 configuration parameters
 * @endpoint2:	Endpoint 2 configuration parameters
 * @cfg: Connection configuration. The connection configuration is ignored for
 *	 connections made to DPMAC objects, where rate is retrieved from the
 *	 MAC configuration.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_connect(struct fsl_mc_io *mc_io,
		 u32 cmd_flags,
		 u16 token,
		 const struct dprc_endpoint *endpoint1,
		 const struct dprc_endpoint *endpoint2,
		 const struct dprc_connection_cfg *cfg)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_CONNECT,
					  cmd_flags,
					  token);
	cmd.params[0] |= mc_enc(0, 32, endpoint1->id);
	cmd.params[0] |= mc_enc(32, 32, endpoint1->if_id);
	cmd.params[1] |= mc_enc(0, 32, endpoint2->id);
	cmd.params[1] |= mc_enc(32, 32, endpoint2->if_id);
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
	cmd.params[4] |= mc_enc(0, 32, cfg->max_rate);
	cmd.params[4] |= mc_enc(32, 32, cfg->committed_rate);
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

/**
 * dprc_disconnect() - Disconnect one endpoint to remove its network connection
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRC object
 * @endpoint:	Endpoint configuration parameters
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_disconnect(struct fsl_mc_io *mc_io,
		    u32 cmd_flags,
		    u16 token,
		    const struct dprc_endpoint *endpoint)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_DISCONNECT,
					  cmd_flags,
					  token);
	cmd.params[0] |= mc_enc(0, 32, endpoint->id);
	cmd.params[0] |= mc_enc(32, 32, endpoint->if_id);
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

/**
* dprc_get_connection() - Get connected endpoint and link status if connection
*			exists.
* @mc_io:	Pointer to MC portal's I/O object
* @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
* @token:	Token of DPRC object
* @endpoint1:	Endpoint 1 configuration parameters
* @endpoint2:	Returned endpoint 2 configuration parameters
* @state:	Returned link state:
*		1 - link is up;
*		0 - link is down;
*		-1 - no connection (endpoint2 information is irrelevant)
*
* Return:     '0' on Success; -ENAVAIL if connection does not exist.
*/
int dprc_get_connection(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			const struct dprc_endpoint *endpoint1,
			struct dprc_endpoint *endpoint2,
			int *state)
{
	struct mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPRC_CMDID_GET_CONNECTION,
					  cmd_flags,
					  token);
	cmd.params[0] |= mc_enc(0, 32, endpoint1->id);
	cmd.params[0] |= mc_enc(32, 32, endpoint1->if_id);
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
	endpoint2->if_id = mc_dec(cmd.params[3], 32, 32);
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

/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
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
#include "../include/mc-sys.h"
#include "../include/mc-cmd.h"
#include "../include/dpbp.h"

#include "dpbp-cmd.h"

/**
 * dpbp_open() - Open a control session for the specified object.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @dpbp_id:	DPBP unique ID
 * @token:	Returned token; use in subsequent API calls
 *
 * This function can be used to open a control session for an
 * already created object; an object may have been declared in
 * the DPL or by calling the dpbp_create function.
 * This function returns a unique authentication token,
 * associated with the specific object ID and the specific MC
 * portal; this token must be used in all subsequent commands for
 * this specific object
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpbp_open(struct fsl_mc_io *mc_io,
	      u32 cmd_flags,
	      int dpbp_id,
	      u16 *token)
{
	struct mc_command cmd = { 0 };
	struct dpbp_cmd_open *cmd_params;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPBP_CMDID_OPEN,
					  cmd_flags, 0);
	cmd_params = (struct dpbp_cmd_open *)cmd.params;
	cmd_params->dpbp_id = cpu_to_le32(dpbp_id);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	*token = mc_cmd_hdr_read_token(&cmd);

	return err;
}
EXPORT_SYMBOL(dpbp_open);

/**
 * dpbp_close() - Close the control session of the object
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPBP object
 *
 * After this function is called, no further operations are
 * allowed on the object without opening a new control session.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpbp_close(struct fsl_mc_io *mc_io,
	       u32 cmd_flags,
	       u16 token)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPBP_CMDID_CLOSE, cmd_flags,
					  token);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}
EXPORT_SYMBOL(dpbp_close);

/**
 * dpbp_create() - Create the DPBP object.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @cfg:	Configuration structure
 * @token:	Returned token; use in subsequent API calls
 *
 * Create the DPBP object, allocate required resources and
 * perform required initialization.
 *
 * The object can be created either by declaring it in the
 * DPL file, or by calling this function.
 * This function returns a unique authentication token,
 * associated with the specific object ID and the specific MC
 * portal; this token must be used in all subsequent calls to
 * this specific object. For objects that are created using the
 * DPL file, call dpbp_open function to get an authentication
 * token first.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpbp_create(struct fsl_mc_io *mc_io,
		u32 cmd_flags,
		const struct dpbp_cfg *cfg,
		u16 *token)
{
	struct mc_command cmd = { 0 };
	int err;

	(void)(cfg); /* unused */

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPBP_CMDID_CREATE,
					  cmd_flags, 0);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	*token = mc_cmd_hdr_read_token(&cmd);

	return 0;
}

/**
 * dpbp_destroy() - Destroy the DPBP object and release all its resources.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPBP object
 *
 * Return:	'0' on Success; error code otherwise.
 */
int dpbp_destroy(struct fsl_mc_io *mc_io,
		 u32 cmd_flags,
		 u16 token)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPBP_CMDID_DESTROY,
					  cmd_flags, token);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpbp_enable() - Enable the DPBP.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPBP object
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpbp_enable(struct fsl_mc_io *mc_io,
		u32 cmd_flags,
		u16 token)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPBP_CMDID_ENABLE, cmd_flags,
					  token);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}
EXPORT_SYMBOL(dpbp_enable);

/**
 * dpbp_disable() - Disable the DPBP.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPBP object
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpbp_disable(struct fsl_mc_io *mc_io,
		 u32 cmd_flags,
		 u16 token)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPBP_CMDID_DISABLE,
					  cmd_flags, token);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}
EXPORT_SYMBOL(dpbp_disable);

/**
 * dpbp_is_enabled() - Check if the DPBP is enabled.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPBP object
 * @en:		Returns '1' if object is enabled; '0' otherwise
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpbp_is_enabled(struct fsl_mc_io *mc_io,
		    u32 cmd_flags,
		    u16 token,
		    int *en)
{
	struct mc_command cmd = { 0 };
	struct dpbp_rsp_is_enabled *rsp_params;
	int err;
	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPBP_CMDID_IS_ENABLED, cmd_flags,
					  token);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpbp_rsp_is_enabled *)cmd.params;
	*en = rsp_params->enabled & DPBP_ENABLE;

	return 0;
}

/**
 * dpbp_reset() - Reset the DPBP, returns the object to initial state.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPBP object
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpbp_reset(struct fsl_mc_io *mc_io,
	       u32 cmd_flags,
	       u16 token)
{
	struct mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPBP_CMDID_RESET,
					  cmd_flags, token);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpbp_set_irq() - Set IRQ information for the DPBP to trigger an interrupt.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPBP object
 * @irq_index:	Identifies the interrupt index to configure
 * @irq_cfg:	IRQ configuration
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpbp_set_irq(struct fsl_mc_io *mc_io,
		 u32 cmd_flags,
		 u16 token,
		 u8 irq_index,
		 struct dpbp_irq_cfg *irq_cfg)
{
	struct mc_command cmd = { 0 };
	struct dpbp_cmd_set_irq *cmd_params;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPBP_CMDID_SET_IRQ,
					  cmd_flags, token);
	cmd_params = (struct dpbp_cmd_set_irq *)cmd.params;
	cmd_params->irq_index = irq_index;
	cmd_params->irq_val = cpu_to_le32(irq_cfg->val);
	cmd_params->irq_addr = cpu_to_le64(irq_cfg->addr);
	cmd_params->irq_num = cpu_to_le32(irq_cfg->irq_num);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpbp_get_irq() - Get IRQ information from the DPBP.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPBP object
 * @irq_index:	The interrupt index to configure
 * @type:	Interrupt type: 0 represents message interrupt
 *		type (both irq_addr and irq_val are valid)
 * @irq_cfg:	IRQ attributes
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpbp_get_irq(struct fsl_mc_io *mc_io,
		 u32 cmd_flags,
		 u16 token,
		 u8 irq_index,
		 int *type,
		 struct dpbp_irq_cfg *irq_cfg)
{
	struct mc_command cmd = { 0 };
	struct dpbp_cmd_get_irq *cmd_params;
	struct dpbp_rsp_get_irq *rsp_params;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPBP_CMDID_GET_IRQ,
					  cmd_flags, token);
	cmd_params = (struct dpbp_cmd_get_irq *)cmd.params;
	cmd_params->irq_index = irq_index;

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpbp_rsp_get_irq *)cmd.params;
	irq_cfg->val = le32_to_cpu(rsp_params->irq_val);
	irq_cfg->addr = le64_to_cpu(rsp_params->irq_addr);
	irq_cfg->irq_num = le32_to_cpu(rsp_params->irq_num);
	*type = le32_to_cpu(rsp_params->type);

	return 0;
}

/**
 * dpbp_set_irq_enable() - Set overall interrupt state.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPBP object
 * @irq_index:	The interrupt index to configure
 * @en:	Interrupt state - enable = 1, disable = 0
 *
 * Allows GPP software to control when interrupts are generated.
 * Each interrupt can have up to 32 causes.  The enable/disable control's the
 * overall interrupt state. if the interrupt is disabled no causes will cause
 * an interrupt.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpbp_set_irq_enable(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			u8 irq_index,
			u8 en)
{
	struct mc_command cmd = { 0 };
	struct dpbp_cmd_set_irq_enable *cmd_params;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPBP_CMDID_SET_IRQ_ENABLE,
					  cmd_flags, token);
	cmd_params = (struct dpbp_cmd_set_irq_enable *)cmd.params;
	cmd_params->enable = en & DPBP_ENABLE;
	cmd_params->irq_index = irq_index;

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpbp_get_irq_enable() - Get overall interrupt state
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPBP object
 * @irq_index:	The interrupt index to configure
 * @en:		Returned interrupt state - enable = 1, disable = 0
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpbp_get_irq_enable(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			u8 irq_index,
			u8 *en)
{
	struct mc_command cmd = { 0 };
	struct dpbp_cmd_get_irq_enable *cmd_params;
	struct dpbp_rsp_get_irq_enable *rsp_params;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPBP_CMDID_GET_IRQ_ENABLE,
					  cmd_flags, token);
	cmd_params = (struct dpbp_cmd_get_irq_enable *)cmd.params;
	cmd_params->irq_index = irq_index;

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpbp_rsp_get_irq_enable *)cmd.params;
	*en = rsp_params->enabled & DPBP_ENABLE;
	return 0;
}

/**
 * dpbp_set_irq_mask() - Set interrupt mask.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPBP object
 * @irq_index:	The interrupt index to configure
 * @mask:	Event mask to trigger interrupt;
 *			each bit:
 *				0 = ignore event
 *				1 = consider event for asserting IRQ
 *
 * Every interrupt can have up to 32 causes and the interrupt model supports
 * masking/unmasking each cause independently
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpbp_set_irq_mask(struct fsl_mc_io *mc_io,
		      u32 cmd_flags,
		      u16 token,
		      u8 irq_index,
		      u32 mask)
{
	struct mc_command cmd = { 0 };
	struct dpbp_cmd_set_irq_mask *cmd_params;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPBP_CMDID_SET_IRQ_MASK,
					  cmd_flags, token);
	cmd_params = (struct dpbp_cmd_set_irq_mask *)cmd.params;
	cmd_params->mask = cpu_to_le32(mask);
	cmd_params->irq_index = irq_index;

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpbp_get_irq_mask() - Get interrupt mask.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPBP object
 * @irq_index:	The interrupt index to configure
 * @mask:	Returned event mask to trigger interrupt
 *
 * Every interrupt can have up to 32 causes and the interrupt model supports
 * masking/unmasking each cause independently
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpbp_get_irq_mask(struct fsl_mc_io *mc_io,
		      u32 cmd_flags,
		      u16 token,
		      u8 irq_index,
		      u32 *mask)
{
	struct mc_command cmd = { 0 };
	struct dpbp_cmd_get_irq_mask *cmd_params;
	struct dpbp_rsp_get_irq_mask *rsp_params;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPBP_CMDID_GET_IRQ_MASK,
					  cmd_flags, token);
	cmd_params = (struct dpbp_cmd_get_irq_mask *)cmd.params;
	cmd_params->irq_index = irq_index;

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpbp_rsp_get_irq_mask *)cmd.params;
	*mask = le32_to_cpu(rsp_params->mask);

	return 0;
}

/**
 * dpbp_get_irq_status() - Get the current status of any pending interrupts.
 *
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPBP object
 * @irq_index:	The interrupt index to configure
 * @status:	Returned interrupts status - one bit per cause:
 *			0 = no interrupt pending
 *			1 = interrupt pending
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpbp_get_irq_status(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			u8 irq_index,
			u32 *status)
{
	struct mc_command cmd = { 0 };
	struct dpbp_cmd_get_irq_status *cmd_params;
	struct dpbp_rsp_get_irq_status *rsp_params;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPBP_CMDID_GET_IRQ_STATUS,
					  cmd_flags, token);
	cmd_params = (struct dpbp_cmd_get_irq_status *)cmd.params;
	cmd_params->status = cpu_to_le32(*status);
	cmd_params->irq_index = irq_index;

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpbp_rsp_get_irq_status *)cmd.params;
	*status = le32_to_cpu(rsp_params->status);

	return 0;
}

/**
 * dpbp_clear_irq_status() - Clear a pending interrupt's status
 *
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPBP object
 * @irq_index:	The interrupt index to configure
 * @status:	Bits to clear (W1C) - one bit per cause:
 *					0 = don't change
 *					1 = clear status bit
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpbp_clear_irq_status(struct fsl_mc_io *mc_io,
			  u32 cmd_flags,
			  u16 token,
			  u8 irq_index,
			  u32 status)
{
	struct mc_command cmd = { 0 };
	struct dpbp_cmd_clear_irq_status *cmd_params;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPBP_CMDID_CLEAR_IRQ_STATUS,
					  cmd_flags, token);
	cmd_params = (struct dpbp_cmd_clear_irq_status *)cmd.params;
	cmd_params->status = cpu_to_le32(status);
	cmd_params->irq_index = irq_index;

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpbp_get_attributes - Retrieve DPBP attributes.
 *
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPBP object
 * @attr:	Returned object's attributes
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpbp_get_attributes(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			struct dpbp_attr *attr)
{
	struct mc_command cmd = { 0 };
	struct dpbp_rsp_get_attributes *rsp_params;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPBP_CMDID_GET_ATTR,
					  cmd_flags, token);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpbp_rsp_get_attributes *)cmd.params;
	attr->bpid = le16_to_cpu(rsp_params->bpid);
	attr->id = le32_to_cpu(rsp_params->id);
	attr->version.major = le16_to_cpu(rsp_params->version_major);
	attr->version.minor = le16_to_cpu(rsp_params->version_minor);

	return 0;
}
EXPORT_SYMBOL(dpbp_get_attributes);

/**
 * dpbp_set_notifications() - Set notifications towards software
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPBP object
 * @cfg:	notifications configuration
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpbp_set_notifications(struct fsl_mc_io *mc_io,
			   u32 cmd_flags,
			   u16 token,
			   struct dpbp_notification_cfg	*cfg)
{
	struct mc_command cmd = { 0 };
	struct dpbp_cmd_set_notifications *cmd_params;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPBP_CMDID_SET_NOTIFICATIONS,
					  cmd_flags, token);
	cmd_params = (struct dpbp_cmd_set_notifications *)cmd.params;
	cmd_params->depletion_entry = cpu_to_le32(cfg->depletion_entry);
	cmd_params->depletion_exit = cpu_to_le32(cfg->depletion_exit);
	cmd_params->surplus_entry = cpu_to_le32(cfg->surplus_entry);
	cmd_params->surplus_exit = cpu_to_le32(cfg->surplus_exit);
	cmd_params->options = cpu_to_le16(cfg->options);
	cmd_params->message_ctx = cpu_to_le64(cfg->message_ctx);
	cmd_params->message_iova = cpu_to_le64(cfg->message_iova);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpbp_get_notifications() - Get the notifications configuration
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPBP object
 * @cfg:	notifications configuration
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpbp_get_notifications(struct fsl_mc_io *mc_io,
			   u32 cmd_flags,
			   u16 token,
			   struct dpbp_notification_cfg	*cfg)
{
	struct mc_command cmd = { 0 };
	struct dpbp_rsp_get_notifications *rsp_params;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPBP_CMDID_GET_NOTIFICATIONS,
					  cmd_flags,
					  token);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpbp_rsp_get_notifications *)cmd.params;
	cfg->depletion_entry = le32_to_cpu(rsp_params->depletion_entry);
	cfg->depletion_exit = le32_to_cpu(rsp_params->depletion_exit);
	cfg->surplus_entry = le32_to_cpu(rsp_params->surplus_entry);
	cfg->surplus_exit = le32_to_cpu(rsp_params->surplus_exit);
	cfg->options = le16_to_cpu(rsp_params->options);
	cfg->message_ctx = le64_to_cpu(rsp_params->message_ctx);
	cfg->message_iova = le64_to_cpu(rsp_params->message_iova);

	return 0;
}

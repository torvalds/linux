// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2013-2016 Freescale Semiconductor Inc.
 * Copyright 2016 NXP
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fsl/mc.h>
#include "dpni.h"
#include "dpni-cmd.h"

/**
 * dpni_prepare_key_cfg() - function prepare extract parameters
 * @cfg: defining a full Key Generation profile (rule)
 * @key_cfg_buf: Zeroed 256 bytes of memory before mapping it to DMA
 *
 * This function has to be called before the following functions:
 *	- dpni_set_rx_tc_dist()
 *	- dpni_set_qos_table()
 */
int dpni_prepare_key_cfg(const struct dpkg_profile_cfg *cfg, u8 *key_cfg_buf)
{
	int i, j;
	struct dpni_ext_set_rx_tc_dist *dpni_ext;
	struct dpni_dist_extract *extr;

	if (cfg->num_extracts > DPKG_MAX_NUM_OF_EXTRACTS)
		return -EINVAL;

	dpni_ext = (struct dpni_ext_set_rx_tc_dist *)key_cfg_buf;
	dpni_ext->num_extracts = cfg->num_extracts;

	for (i = 0; i < cfg->num_extracts; i++) {
		extr = &dpni_ext->extracts[i];

		switch (cfg->extracts[i].type) {
		case DPKG_EXTRACT_FROM_HDR:
			extr->prot = cfg->extracts[i].extract.from_hdr.prot;
			dpni_set_field(extr->efh_type, EFH_TYPE,
				       cfg->extracts[i].extract.from_hdr.type);
			extr->size = cfg->extracts[i].extract.from_hdr.size;
			extr->offset = cfg->extracts[i].extract.from_hdr.offset;
			extr->field = cpu_to_le32(
				cfg->extracts[i].extract.from_hdr.field);
			extr->hdr_index =
				cfg->extracts[i].extract.from_hdr.hdr_index;
			break;
		case DPKG_EXTRACT_FROM_DATA:
			extr->size = cfg->extracts[i].extract.from_data.size;
			extr->offset =
				cfg->extracts[i].extract.from_data.offset;
			break;
		case DPKG_EXTRACT_FROM_PARSE:
			extr->size = cfg->extracts[i].extract.from_parse.size;
			extr->offset =
				cfg->extracts[i].extract.from_parse.offset;
			break;
		default:
			return -EINVAL;
		}

		extr->num_of_byte_masks = cfg->extracts[i].num_of_byte_masks;
		dpni_set_field(extr->extract_type, EXTRACT_TYPE,
			       cfg->extracts[i].type);

		for (j = 0; j < DPKG_NUM_OF_MASKS; j++) {
			extr->masks[j].mask = cfg->extracts[i].masks[j].mask;
			extr->masks[j].offset =
				cfg->extracts[i].masks[j].offset;
		}
	}

	return 0;
}

/**
 * dpni_open() - Open a control session for the specified object
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @dpni_id:	DPNI unique ID
 * @token:	Returned token; use in subsequent API calls
 *
 * This function can be used to open a control session for an
 * already created object; an object may have been declared in
 * the DPL or by calling the dpni_create() function.
 * This function returns a unique authentication token,
 * associated with the specific object ID and the specific MC
 * portal; this token must be used in all subsequent commands for
 * this specific object.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_open(struct fsl_mc_io *mc_io,
	      u32 cmd_flags,
	      int dpni_id,
	      u16 *token)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_open *cmd_params;

	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_OPEN,
					  cmd_flags,
					  0);
	cmd_params = (struct dpni_cmd_open *)cmd.params;
	cmd_params->dpni_id = cpu_to_le32(dpni_id);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	*token = mc_cmd_hdr_read_token(&cmd);

	return 0;
}

/**
 * dpni_close() - Close the control session of the object
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 *
 * After this function is called, no further operations are
 * allowed on the object without opening a new control session.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_close(struct fsl_mc_io *mc_io,
	       u32 cmd_flags,
	       u16 token)
{
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_CLOSE,
					  cmd_flags,
					  token);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpni_set_pools() - Set buffer pools configuration
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @cfg:	Buffer pools configuration
 *
 * mandatory for DPNI operation
 * warning:Allowed only when DPNI is disabled
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_set_pools(struct fsl_mc_io *mc_io,
		   u32 cmd_flags,
		   u16 token,
		   const struct dpni_pools_cfg *cfg)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_set_pools *cmd_params;
	int i;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_SET_POOLS,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_set_pools *)cmd.params;
	cmd_params->num_dpbp = cfg->num_dpbp;
	for (i = 0; i < DPNI_MAX_DPBP; i++) {
		cmd_params->dpbp_id[i] = cpu_to_le32(cfg->pools[i].dpbp_id);
		cmd_params->buffer_size[i] =
			cpu_to_le16(cfg->pools[i].buffer_size);
		cmd_params->backup_pool_mask |=
			DPNI_BACKUP_POOL(cfg->pools[i].backup_pool, i);
	}

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpni_enable() - Enable the DPNI, allow sending and receiving frames.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:		Token of DPNI object
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_enable(struct fsl_mc_io *mc_io,
		u32 cmd_flags,
		u16 token)
{
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_ENABLE,
					  cmd_flags,
					  token);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpni_disable() - Disable the DPNI, stop sending and receiving frames.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_disable(struct fsl_mc_io *mc_io,
		 u32 cmd_flags,
		 u16 token)
{
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_DISABLE,
					  cmd_flags,
					  token);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpni_is_enabled() - Check if the DPNI is enabled.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @en:		Returns '1' if object is enabled; '0' otherwise
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_is_enabled(struct fsl_mc_io *mc_io,
		    u32 cmd_flags,
		    u16 token,
		    int *en)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_rsp_is_enabled *rsp_params;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_IS_ENABLED,
					  cmd_flags,
					  token);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpni_rsp_is_enabled *)cmd.params;
	*en = dpni_get_field(rsp_params->enabled, ENABLE);

	return 0;
}

/**
 * dpni_reset() - Reset the DPNI, returns the object to initial state.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_reset(struct fsl_mc_io *mc_io,
	       u32 cmd_flags,
	       u16 token)
{
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_RESET,
					  cmd_flags,
					  token);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpni_set_irq_enable() - Set overall interrupt state.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @irq_index:	The interrupt index to configure
 * @en:		Interrupt state: - enable = 1, disable = 0
 *
 * Allows GPP software to control when interrupts are generated.
 * Each interrupt can have up to 32 causes.  The enable/disable control's the
 * overall interrupt state. if the interrupt is disabled no causes will cause
 * an interrupt.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_set_irq_enable(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			u8 irq_index,
			u8 en)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_set_irq_enable *cmd_params;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_SET_IRQ_ENABLE,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_set_irq_enable *)cmd.params;
	dpni_set_field(cmd_params->enable, ENABLE, en);
	cmd_params->irq_index = irq_index;

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpni_get_irq_enable() - Get overall interrupt state
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @irq_index:	The interrupt index to configure
 * @en:		Returned interrupt state - enable = 1, disable = 0
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_get_irq_enable(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			u8 irq_index,
			u8 *en)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_get_irq_enable *cmd_params;
	struct dpni_rsp_get_irq_enable *rsp_params;

	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_GET_IRQ_ENABLE,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_get_irq_enable *)cmd.params;
	cmd_params->irq_index = irq_index;

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpni_rsp_get_irq_enable *)cmd.params;
	*en = dpni_get_field(rsp_params->enabled, ENABLE);

	return 0;
}

/**
 * dpni_set_irq_mask() - Set interrupt mask.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @irq_index:	The interrupt index to configure
 * @mask:	event mask to trigger interrupt;
 *			each bit:
 *				0 = ignore event
 *				1 = consider event for asserting IRQ
 *
 * Every interrupt can have up to 32 causes and the interrupt model supports
 * masking/unmasking each cause independently
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_set_irq_mask(struct fsl_mc_io *mc_io,
		      u32 cmd_flags,
		      u16 token,
		      u8 irq_index,
		      u32 mask)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_set_irq_mask *cmd_params;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_SET_IRQ_MASK,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_set_irq_mask *)cmd.params;
	cmd_params->mask = cpu_to_le32(mask);
	cmd_params->irq_index = irq_index;

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpni_get_irq_mask() - Get interrupt mask.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @irq_index:	The interrupt index to configure
 * @mask:	Returned event mask to trigger interrupt
 *
 * Every interrupt can have up to 32 causes and the interrupt model supports
 * masking/unmasking each cause independently
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_get_irq_mask(struct fsl_mc_io *mc_io,
		      u32 cmd_flags,
		      u16 token,
		      u8 irq_index,
		      u32 *mask)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_get_irq_mask *cmd_params;
	struct dpni_rsp_get_irq_mask *rsp_params;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_GET_IRQ_MASK,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_get_irq_mask *)cmd.params;
	cmd_params->irq_index = irq_index;

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpni_rsp_get_irq_mask *)cmd.params;
	*mask = le32_to_cpu(rsp_params->mask);

	return 0;
}

/**
 * dpni_get_irq_status() - Get the current status of any pending interrupts.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @irq_index:	The interrupt index to configure
 * @status:	Returned interrupts status - one bit per cause:
 *			0 = no interrupt pending
 *			1 = interrupt pending
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_get_irq_status(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			u8 irq_index,
			u32 *status)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_get_irq_status *cmd_params;
	struct dpni_rsp_get_irq_status *rsp_params;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_GET_IRQ_STATUS,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_get_irq_status *)cmd.params;
	cmd_params->status = cpu_to_le32(*status);
	cmd_params->irq_index = irq_index;

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpni_rsp_get_irq_status *)cmd.params;
	*status = le32_to_cpu(rsp_params->status);

	return 0;
}

/**
 * dpni_clear_irq_status() - Clear a pending interrupt's status
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @irq_index:	The interrupt index to configure
 * @status:	bits to clear (W1C) - one bit per cause:
 *			0 = don't change
 *			1 = clear status bit
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_clear_irq_status(struct fsl_mc_io *mc_io,
			  u32 cmd_flags,
			  u16 token,
			  u8 irq_index,
			  u32 status)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_clear_irq_status *cmd_params;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_CLEAR_IRQ_STATUS,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_clear_irq_status *)cmd.params;
	cmd_params->irq_index = irq_index;
	cmd_params->status = cpu_to_le32(status);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpni_get_attributes() - Retrieve DPNI attributes.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @attr:	Object's attributes
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_get_attributes(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			struct dpni_attr *attr)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_rsp_get_attr *rsp_params;

	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_GET_ATTR,
					  cmd_flags,
					  token);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpni_rsp_get_attr *)cmd.params;
	attr->options = le32_to_cpu(rsp_params->options);
	attr->num_queues = rsp_params->num_queues;
	attr->num_tcs = rsp_params->num_tcs;
	attr->mac_filter_entries = rsp_params->mac_filter_entries;
	attr->vlan_filter_entries = rsp_params->vlan_filter_entries;
	attr->qos_entries = rsp_params->qos_entries;
	attr->fs_entries = le16_to_cpu(rsp_params->fs_entries);
	attr->qos_key_size = rsp_params->qos_key_size;
	attr->fs_key_size = rsp_params->fs_key_size;
	attr->wriop_version = le16_to_cpu(rsp_params->wriop_version);

	return 0;
}

/**
 * dpni_set_errors_behavior() - Set errors behavior
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @cfg:	Errors configuration
 *
 * this function may be called numerous times with different
 * error masks
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_set_errors_behavior(struct fsl_mc_io *mc_io,
			     u32 cmd_flags,
			     u16 token,
			     struct dpni_error_cfg *cfg)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_set_errors_behavior *cmd_params;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_SET_ERRORS_BEHAVIOR,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_set_errors_behavior *)cmd.params;
	cmd_params->errors = cpu_to_le32(cfg->errors);
	dpni_set_field(cmd_params->flags, ERROR_ACTION, cfg->error_action);
	dpni_set_field(cmd_params->flags, FRAME_ANN, cfg->set_frame_annotation);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpni_get_buffer_layout() - Retrieve buffer layout attributes.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @qtype:	Type of queue to retrieve configuration for
 * @layout:	Returns buffer layout attributes
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_get_buffer_layout(struct fsl_mc_io *mc_io,
			   u32 cmd_flags,
			   u16 token,
			   enum dpni_queue_type qtype,
			   struct dpni_buffer_layout *layout)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_get_buffer_layout *cmd_params;
	struct dpni_rsp_get_buffer_layout *rsp_params;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_GET_BUFFER_LAYOUT,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_get_buffer_layout *)cmd.params;
	cmd_params->qtype = qtype;

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpni_rsp_get_buffer_layout *)cmd.params;
	layout->pass_timestamp = dpni_get_field(rsp_params->flags, PASS_TS);
	layout->pass_parser_result = dpni_get_field(rsp_params->flags, PASS_PR);
	layout->pass_frame_status = dpni_get_field(rsp_params->flags, PASS_FS);
	layout->private_data_size = le16_to_cpu(rsp_params->private_data_size);
	layout->data_align = le16_to_cpu(rsp_params->data_align);
	layout->data_head_room = le16_to_cpu(rsp_params->head_room);
	layout->data_tail_room = le16_to_cpu(rsp_params->tail_room);

	return 0;
}

/**
 * dpni_set_buffer_layout() - Set buffer layout configuration.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @qtype:	Type of queue this configuration applies to
 * @layout:	Buffer layout configuration
 *
 * Return:	'0' on Success; Error code otherwise.
 *
 * @warning	Allowed only when DPNI is disabled
 */
int dpni_set_buffer_layout(struct fsl_mc_io *mc_io,
			   u32 cmd_flags,
			   u16 token,
			   enum dpni_queue_type qtype,
			   const struct dpni_buffer_layout *layout)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_set_buffer_layout *cmd_params;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_SET_BUFFER_LAYOUT,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_set_buffer_layout *)cmd.params;
	cmd_params->qtype = qtype;
	cmd_params->options = cpu_to_le16(layout->options);
	dpni_set_field(cmd_params->flags, PASS_TS, layout->pass_timestamp);
	dpni_set_field(cmd_params->flags, PASS_PR, layout->pass_parser_result);
	dpni_set_field(cmd_params->flags, PASS_FS, layout->pass_frame_status);
	cmd_params->private_data_size = cpu_to_le16(layout->private_data_size);
	cmd_params->data_align = cpu_to_le16(layout->data_align);
	cmd_params->head_room = cpu_to_le16(layout->data_head_room);
	cmd_params->tail_room = cpu_to_le16(layout->data_tail_room);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpni_set_offload() - Set DPNI offload configuration.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @type:	Type of DPNI offload
 * @config:	Offload configuration.
 *		For checksum offloads, non-zero value enables the offload
 *
 * Return:     '0' on Success; Error code otherwise.
 *
 * @warning    Allowed only when DPNI is disabled
 */

int dpni_set_offload(struct fsl_mc_io *mc_io,
		     u32 cmd_flags,
		     u16 token,
		     enum dpni_offload type,
		     u32 config)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_set_offload *cmd_params;

	cmd.header = mc_encode_cmd_header(DPNI_CMDID_SET_OFFLOAD,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_set_offload *)cmd.params;
	cmd_params->dpni_offload = type;
	cmd_params->config = cpu_to_le32(config);

	return mc_send_command(mc_io, &cmd);
}

int dpni_get_offload(struct fsl_mc_io *mc_io,
		     u32 cmd_flags,
		     u16 token,
		     enum dpni_offload type,
		     u32 *config)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_get_offload *cmd_params;
	struct dpni_rsp_get_offload *rsp_params;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_GET_OFFLOAD,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_get_offload *)cmd.params;
	cmd_params->dpni_offload = type;

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpni_rsp_get_offload *)cmd.params;
	*config = le32_to_cpu(rsp_params->config);

	return 0;
}

/**
 * dpni_get_qdid() - Get the Queuing Destination ID (QDID) that should be used
 *			for enqueue operations
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @qtype:	Type of queue to receive QDID for
 * @qdid:	Returned virtual QDID value that should be used as an argument
 *			in all enqueue operations
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_get_qdid(struct fsl_mc_io *mc_io,
		  u32 cmd_flags,
		  u16 token,
		  enum dpni_queue_type qtype,
		  u16 *qdid)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_get_qdid *cmd_params;
	struct dpni_rsp_get_qdid *rsp_params;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_GET_QDID,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_get_qdid *)cmd.params;
	cmd_params->qtype = qtype;

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpni_rsp_get_qdid *)cmd.params;
	*qdid = le16_to_cpu(rsp_params->qdid);

	return 0;
}

/**
 * dpni_get_tx_data_offset() - Get the Tx data offset (from start of buffer)
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @data_offset: Tx data offset (from start of buffer)
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_get_tx_data_offset(struct fsl_mc_io *mc_io,
			    u32 cmd_flags,
			    u16 token,
			    u16 *data_offset)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_rsp_get_tx_data_offset *rsp_params;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_GET_TX_DATA_OFFSET,
					  cmd_flags,
					  token);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpni_rsp_get_tx_data_offset *)cmd.params;
	*data_offset = le16_to_cpu(rsp_params->data_offset);

	return 0;
}

/**
 * dpni_set_link_cfg() - set the link configuration.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @cfg:	Link configuration
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_set_link_cfg(struct fsl_mc_io *mc_io,
		      u32 cmd_flags,
		      u16 token,
		      const struct dpni_link_cfg *cfg)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_link_cfg *cmd_params;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_SET_LINK_CFG,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_link_cfg *)cmd.params;
	cmd_params->rate = cpu_to_le32(cfg->rate);
	cmd_params->options = cpu_to_le64(cfg->options);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpni_get_link_cfg() - return the link configuration
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @cfg:	Link configuration from dpni object
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_get_link_cfg(struct fsl_mc_io *mc_io,
		      u32 cmd_flags,
		      u16 token,
		      struct dpni_link_cfg *cfg)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_link_cfg *rsp_params;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_GET_LINK_CFG,
					  cmd_flags,
					  token);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpni_cmd_link_cfg *)cmd.params;
	cfg->rate = le32_to_cpu(rsp_params->rate);
	cfg->options = le64_to_cpu(rsp_params->options);

	return err;
}

/**
 * dpni_get_link_state() - Return the link state (either up or down)
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @state:	Returned link state;
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_get_link_state(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			struct dpni_link_state *state)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_rsp_get_link_state *rsp_params;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_GET_LINK_STATE,
					  cmd_flags,
					  token);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpni_rsp_get_link_state *)cmd.params;
	state->up = dpni_get_field(rsp_params->flags, LINK_STATE);
	state->rate = le32_to_cpu(rsp_params->rate);
	state->options = le64_to_cpu(rsp_params->options);

	return 0;
}

/**
 * dpni_set_max_frame_length() - Set the maximum received frame length.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @max_frame_length:	Maximum received frame length (in
 *				bytes); frame is discarded if its
 *				length exceeds this value
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_set_max_frame_length(struct fsl_mc_io *mc_io,
			      u32 cmd_flags,
			      u16 token,
			      u16 max_frame_length)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_set_max_frame_length *cmd_params;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_SET_MAX_FRAME_LENGTH,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_set_max_frame_length *)cmd.params;
	cmd_params->max_frame_length = cpu_to_le16(max_frame_length);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpni_get_max_frame_length() - Get the maximum received frame length.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @max_frame_length:	Maximum received frame length (in
 *				bytes); frame is discarded if its
 *				length exceeds this value
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_get_max_frame_length(struct fsl_mc_io *mc_io,
			      u32 cmd_flags,
			      u16 token,
			      u16 *max_frame_length)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_rsp_get_max_frame_length *rsp_params;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_GET_MAX_FRAME_LENGTH,
					  cmd_flags,
					  token);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpni_rsp_get_max_frame_length *)cmd.params;
	*max_frame_length = le16_to_cpu(rsp_params->max_frame_length);

	return 0;
}

/**
 * dpni_set_multicast_promisc() - Enable/disable multicast promiscuous mode
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @en:		Set to '1' to enable; '0' to disable
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_set_multicast_promisc(struct fsl_mc_io *mc_io,
			       u32 cmd_flags,
			       u16 token,
			       int en)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_set_multicast_promisc *cmd_params;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_SET_MCAST_PROMISC,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_set_multicast_promisc *)cmd.params;
	dpni_set_field(cmd_params->enable, ENABLE, en);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpni_get_multicast_promisc() - Get multicast promiscuous mode
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @en:		Returns '1' if enabled; '0' otherwise
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_get_multicast_promisc(struct fsl_mc_io *mc_io,
			       u32 cmd_flags,
			       u16 token,
			       int *en)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_rsp_get_multicast_promisc *rsp_params;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_GET_MCAST_PROMISC,
					  cmd_flags,
					  token);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpni_rsp_get_multicast_promisc *)cmd.params;
	*en = dpni_get_field(rsp_params->enabled, ENABLE);

	return 0;
}

/**
 * dpni_set_unicast_promisc() - Enable/disable unicast promiscuous mode
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @en:		Set to '1' to enable; '0' to disable
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_set_unicast_promisc(struct fsl_mc_io *mc_io,
			     u32 cmd_flags,
			     u16 token,
			     int en)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_set_unicast_promisc *cmd_params;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_SET_UNICAST_PROMISC,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_set_unicast_promisc *)cmd.params;
	dpni_set_field(cmd_params->enable, ENABLE, en);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpni_get_unicast_promisc() - Get unicast promiscuous mode
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @en:		Returns '1' if enabled; '0' otherwise
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_get_unicast_promisc(struct fsl_mc_io *mc_io,
			     u32 cmd_flags,
			     u16 token,
			     int *en)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_rsp_get_unicast_promisc *rsp_params;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_GET_UNICAST_PROMISC,
					  cmd_flags,
					  token);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpni_rsp_get_unicast_promisc *)cmd.params;
	*en = dpni_get_field(rsp_params->enabled, ENABLE);

	return 0;
}

/**
 * dpni_set_primary_mac_addr() - Set the primary MAC address
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @mac_addr:	MAC address to set as primary address
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_set_primary_mac_addr(struct fsl_mc_io *mc_io,
			      u32 cmd_flags,
			      u16 token,
			      const u8 mac_addr[6])
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_set_primary_mac_addr *cmd_params;
	int i;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_SET_PRIM_MAC,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_set_primary_mac_addr *)cmd.params;
	for (i = 0; i < 6; i++)
		cmd_params->mac_addr[i] = mac_addr[5 - i];

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpni_get_primary_mac_addr() - Get the primary MAC address
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @mac_addr:	Returned MAC address
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_get_primary_mac_addr(struct fsl_mc_io *mc_io,
			      u32 cmd_flags,
			      u16 token,
			      u8 mac_addr[6])
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_rsp_get_primary_mac_addr *rsp_params;
	int i, err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_GET_PRIM_MAC,
					  cmd_flags,
					  token);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpni_rsp_get_primary_mac_addr *)cmd.params;
	for (i = 0; i < 6; i++)
		mac_addr[5 - i] = rsp_params->mac_addr[i];

	return 0;
}

/**
 * dpni_get_port_mac_addr() - Retrieve MAC address associated to the physical
 *			port the DPNI is attached to
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @mac_addr:	MAC address of the physical port, if any, otherwise 0
 *
 * The primary MAC address is not cleared by this operation.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_get_port_mac_addr(struct fsl_mc_io *mc_io,
			   u32 cmd_flags,
			   u16 token,
			   u8 mac_addr[6])
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_rsp_get_port_mac_addr *rsp_params;
	int i, err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_GET_PORT_MAC_ADDR,
					  cmd_flags,
					  token);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpni_rsp_get_port_mac_addr *)cmd.params;
	for (i = 0; i < 6; i++)
		mac_addr[5 - i] = rsp_params->mac_addr[i];

	return 0;
}

/**
 * dpni_add_mac_addr() - Add MAC address filter
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @mac_addr:	MAC address to add
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_add_mac_addr(struct fsl_mc_io *mc_io,
		      u32 cmd_flags,
		      u16 token,
		      const u8 mac_addr[6])
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_add_mac_addr *cmd_params;
	int i;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_ADD_MAC_ADDR,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_add_mac_addr *)cmd.params;
	for (i = 0; i < 6; i++)
		cmd_params->mac_addr[i] = mac_addr[5 - i];

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpni_remove_mac_addr() - Remove MAC address filter
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @mac_addr:	MAC address to remove
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_remove_mac_addr(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 token,
			 const u8 mac_addr[6])
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_remove_mac_addr *cmd_params;
	int i;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_REMOVE_MAC_ADDR,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_remove_mac_addr *)cmd.params;
	for (i = 0; i < 6; i++)
		cmd_params->mac_addr[i] = mac_addr[5 - i];

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpni_clear_mac_filters() - Clear all unicast and/or multicast MAC filters
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @unicast:	Set to '1' to clear unicast addresses
 * @multicast:	Set to '1' to clear multicast addresses
 *
 * The primary MAC address is not cleared by this operation.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_clear_mac_filters(struct fsl_mc_io *mc_io,
			   u32 cmd_flags,
			   u16 token,
			   int unicast,
			   int multicast)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_clear_mac_filters *cmd_params;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_CLR_MAC_FILTERS,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_clear_mac_filters *)cmd.params;
	dpni_set_field(cmd_params->flags, UNICAST_FILTERS, unicast);
	dpni_set_field(cmd_params->flags, MULTICAST_FILTERS, multicast);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpni_set_rx_tc_dist() - Set Rx traffic class distribution configuration
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @tc_id:	Traffic class selection (0-7)
 * @cfg:	Traffic class distribution configuration
 *
 * warning: if 'dist_mode != DPNI_DIST_MODE_NONE', call dpni_prepare_key_cfg()
 *			first to prepare the key_cfg_iova parameter
 *
 * Return:	'0' on Success; error code otherwise.
 */
int dpni_set_rx_tc_dist(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			u8 tc_id,
			const struct dpni_rx_tc_dist_cfg *cfg)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_set_rx_tc_dist *cmd_params;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_SET_RX_TC_DIST,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_set_rx_tc_dist *)cmd.params;
	cmd_params->dist_size = cpu_to_le16(cfg->dist_size);
	cmd_params->tc_id = tc_id;
	dpni_set_field(cmd_params->flags, DIST_MODE, cfg->dist_mode);
	dpni_set_field(cmd_params->flags, MISS_ACTION, cfg->fs_cfg.miss_action);
	cmd_params->default_flow_id = cpu_to_le16(cfg->fs_cfg.default_flow_id);
	cmd_params->key_cfg_iova = cpu_to_le64(cfg->key_cfg_iova);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpni_set_queue() - Set queue parameters
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @qtype:	Type of queue - all queue types are supported, although
 *		the command is ignored for Tx
 * @tc:		Traffic class, in range 0 to NUM_TCS - 1
 * @index:	Selects the specific queue out of the set allocated for the
 *		same TC. Value must be in range 0 to NUM_QUEUES - 1
 * @options:	A combination of DPNI_QUEUE_OPT_ values that control what
 *		configuration options are set on the queue
 * @queue:	Queue structure
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_set_queue(struct fsl_mc_io *mc_io,
		   u32 cmd_flags,
		   u16 token,
		   enum dpni_queue_type qtype,
		   u8 tc,
		   u8 index,
		   u8 options,
		   const struct dpni_queue *queue)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_set_queue *cmd_params;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_SET_QUEUE,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_set_queue *)cmd.params;
	cmd_params->qtype = qtype;
	cmd_params->tc = tc;
	cmd_params->index = index;
	cmd_params->options = options;
	cmd_params->dest_id = cpu_to_le32(queue->destination.id);
	cmd_params->dest_prio = queue->destination.priority;
	dpni_set_field(cmd_params->flags, DEST_TYPE, queue->destination.type);
	dpni_set_field(cmd_params->flags, STASH_CTRL, queue->flc.stash_control);
	dpni_set_field(cmd_params->flags, HOLD_ACTIVE,
		       queue->destination.hold_active);
	cmd_params->flc = cpu_to_le64(queue->flc.value);
	cmd_params->user_context = cpu_to_le64(queue->user_context);

	/* send command to mc */
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpni_get_queue() - Get queue parameters
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @qtype:	Type of queue - all queue types are supported
 * @tc:		Traffic class, in range 0 to NUM_TCS - 1
 * @index:	Selects the specific queue out of the set allocated for the
 *		same TC. Value must be in range 0 to NUM_QUEUES - 1
 * @queue:	Queue configuration structure
 * @qid:	Queue identification
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_get_queue(struct fsl_mc_io *mc_io,
		   u32 cmd_flags,
		   u16 token,
		   enum dpni_queue_type qtype,
		   u8 tc,
		   u8 index,
		   struct dpni_queue *queue,
		   struct dpni_queue_id *qid)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_get_queue *cmd_params;
	struct dpni_rsp_get_queue *rsp_params;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_GET_QUEUE,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_get_queue *)cmd.params;
	cmd_params->qtype = qtype;
	cmd_params->tc = tc;
	cmd_params->index = index;

	/* send command to mc */
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpni_rsp_get_queue *)cmd.params;
	queue->destination.id = le32_to_cpu(rsp_params->dest_id);
	queue->destination.priority = rsp_params->dest_prio;
	queue->destination.type = dpni_get_field(rsp_params->flags,
						 DEST_TYPE);
	queue->flc.stash_control = dpni_get_field(rsp_params->flags,
						  STASH_CTRL);
	queue->destination.hold_active = dpni_get_field(rsp_params->flags,
							HOLD_ACTIVE);
	queue->flc.value = le64_to_cpu(rsp_params->flc);
	queue->user_context = le64_to_cpu(rsp_params->user_context);
	qid->fqid = le32_to_cpu(rsp_params->fqid);
	qid->qdbin = le16_to_cpu(rsp_params->qdbin);

	return 0;
}

/**
 * dpni_get_statistics() - Get DPNI statistics
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @page:	Selects the statistics page to retrieve, see
 *		DPNI_GET_STATISTICS output. Pages are numbered 0 to 6.
 * @stat:	Structure containing the statistics
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_get_statistics(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			u8 page,
			union dpni_statistics *stat)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_get_statistics *cmd_params;
	struct dpni_rsp_get_statistics *rsp_params;
	int i, err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_GET_STATISTICS,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_get_statistics *)cmd.params;
	cmd_params->page_number = page;

	/* send command to mc */
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpni_rsp_get_statistics *)cmd.params;
	for (i = 0; i < DPNI_STATISTICS_CNT; i++)
		stat->raw.counter[i] = le64_to_cpu(rsp_params->counter[i]);

	return 0;
}

/**
 * dpni_set_taildrop() - Set taildrop per queue or TC
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @cg_point:	Congestion point
 * @q_type:	Queue type on which the taildrop is configured.
 *		Only Rx queues are supported for now
 * @tc:		Traffic class to apply this taildrop to
 * @q_index:	Index of the queue if the DPNI supports multiple queues for
 *		traffic distribution. Ignored if CONGESTION_POINT is not 0.
 * @taildrop:	Taildrop structure
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_set_taildrop(struct fsl_mc_io *mc_io,
		      u32 cmd_flags,
		      u16 token,
		      enum dpni_congestion_point cg_point,
		      enum dpni_queue_type qtype,
		      u8 tc,
		      u8 index,
		      struct dpni_taildrop *taildrop)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_set_taildrop *cmd_params;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_SET_TAILDROP,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_set_taildrop *)cmd.params;
	cmd_params->congestion_point = cg_point;
	cmd_params->qtype = qtype;
	cmd_params->tc = tc;
	cmd_params->index = index;
	dpni_set_field(cmd_params->enable, ENABLE, taildrop->enable);
	cmd_params->units = taildrop->units;
	cmd_params->threshold = cpu_to_le32(taildrop->threshold);

	/* send command to mc */
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpni_get_taildrop() - Get taildrop information
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @cg_point:	Congestion point
 * @q_type:	Queue type on which the taildrop is configured.
 *		Only Rx queues are supported for now
 * @tc:		Traffic class to apply this taildrop to
 * @q_index:	Index of the queue if the DPNI supports multiple queues for
 *		traffic distribution. Ignored if CONGESTION_POINT is not 0.
 * @taildrop:	Taildrop structure
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_get_taildrop(struct fsl_mc_io *mc_io,
		      u32 cmd_flags,
		      u16 token,
		      enum dpni_congestion_point cg_point,
		      enum dpni_queue_type qtype,
		      u8 tc,
		      u8 index,
		      struct dpni_taildrop *taildrop)
{
	struct fsl_mc_command cmd = { 0 };
	struct dpni_cmd_get_taildrop *cmd_params;
	struct dpni_rsp_get_taildrop *rsp_params;
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_GET_TAILDROP,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_get_taildrop *)cmd.params;
	cmd_params->congestion_point = cg_point;
	cmd_params->qtype = qtype;
	cmd_params->tc = tc;
	cmd_params->index = index;

	/* send command to mc */
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpni_rsp_get_taildrop *)cmd.params;
	taildrop->enable = dpni_get_field(rsp_params->enable, ENABLE);
	taildrop->units = rsp_params->units;
	taildrop->threshold = le32_to_cpu(rsp_params->threshold);

	return 0;
}

/**
 * dpni_get_api_version() - Get Data Path Network Interface API version
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @major_ver:	Major version of data path network interface API
 * @minor_ver:	Minor version of data path network interface API
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_get_api_version(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 *major_ver,
			 u16 *minor_ver)
{
	struct dpni_rsp_get_api_version *rsp_params;
	struct fsl_mc_command cmd = { 0 };
	int err;

	cmd.header = mc_encode_cmd_header(DPNI_CMDID_GET_API_VERSION,
					  cmd_flags, 0);

	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	rsp_params = (struct dpni_rsp_get_api_version *)cmd.params;
	*major_ver = le16_to_cpu(rsp_params->major);
	*minor_ver = le16_to_cpu(rsp_params->minor);

	return 0;
}

/**
 * dpni_set_rx_fs_dist() - Set Rx flow steering distribution
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @cfg: Distribution configuration
 *
 * If the FS is already enabled with a previous call the classification
 * key will be changed but all the table rules are kept. If the
 * existing rules do not match the key the results will not be
 * predictable. It is the user responsibility to keep key integrity.
 * If cfg.enable is set to 1 the command will create a flow steering table
 * and will classify packets according to this table. The packets that
 * miss all the table rules will be classified according to settings
 * made in dpni_set_rx_hash_dist()
 * If cfg.enable is set to 0 the command will clear flow steering table.
 * The packets will be classified according to settings made in
 * dpni_set_rx_hash_dist()
 */
int dpni_set_rx_fs_dist(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			const struct dpni_rx_dist_cfg *cfg)
{
	struct dpni_cmd_set_rx_fs_dist *cmd_params;
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_SET_RX_FS_DIST,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_set_rx_fs_dist *)cmd.params;
	cmd_params->dist_size = cpu_to_le16(cfg->dist_size);
	dpni_set_field(cmd_params->enable, RX_FS_DIST_ENABLE, cfg->enable);
	cmd_params->tc = cfg->tc;
	cmd_params->miss_flow_id = cpu_to_le16(cfg->fs_miss_flow_id);
	cmd_params->key_cfg_iova = cpu_to_le64(cfg->key_cfg_iova);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpni_set_rx_hash_dist() - Set Rx hash distribution
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @cfg: Distribution configuration
 * If cfg.enable is set to 1 the packets will be classified using a hash
 * function based on the key received in cfg.key_cfg_iova parameter.
 * If cfg.enable is set to 0 the packets will be sent to the default queue
 */
int dpni_set_rx_hash_dist(struct fsl_mc_io *mc_io,
			  u32 cmd_flags,
			  u16 token,
			  const struct dpni_rx_dist_cfg *cfg)
{
	struct dpni_cmd_set_rx_hash_dist *cmd_params;
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_SET_RX_HASH_DIST,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_set_rx_hash_dist *)cmd.params;
	cmd_params->dist_size = cpu_to_le16(cfg->dist_size);
	dpni_set_field(cmd_params->enable, RX_HASH_DIST_ENABLE, cfg->enable);
	cmd_params->tc = cfg->tc;
	cmd_params->key_cfg_iova = cpu_to_le64(cfg->key_cfg_iova);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpni_add_fs_entry() - Add Flow Steering entry for a specific traffic class
 *			(to select a flow ID)
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @tc_id:	Traffic class selection (0-7)
 * @index:	Location in the FS table where to insert the entry.
 *		Only relevant if MASKING is enabled for FS
 *		classification on this DPNI, it is ignored for exact match.
 * @cfg:	Flow steering rule to add
 * @action:	Action to be taken as result of a classification hit
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_add_fs_entry(struct fsl_mc_io *mc_io,
		      u32 cmd_flags,
		      u16 token,
		      u8 tc_id,
		      u16 index,
		      const struct dpni_rule_cfg *cfg,
		      const struct dpni_fs_action_cfg *action)
{
	struct dpni_cmd_add_fs_entry *cmd_params;
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_ADD_FS_ENT,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_add_fs_entry *)cmd.params;
	cmd_params->tc_id = tc_id;
	cmd_params->key_size = cfg->key_size;
	cmd_params->index = cpu_to_le16(index);
	cmd_params->key_iova = cpu_to_le64(cfg->key_iova);
	cmd_params->mask_iova = cpu_to_le64(cfg->mask_iova);
	cmd_params->options = cpu_to_le16(action->options);
	cmd_params->flow_id = cpu_to_le16(action->flow_id);
	cmd_params->flc = cpu_to_le64(action->flc);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

/**
 * dpni_remove_fs_entry() - Remove Flow Steering entry from a specific
 *			    traffic class
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPNI object
 * @tc_id:	Traffic class selection (0-7)
 * @cfg:	Flow steering rule to remove
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpni_remove_fs_entry(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 token,
			 u8 tc_id,
			 const struct dpni_rule_cfg *cfg)
{
	struct dpni_cmd_remove_fs_entry *cmd_params;
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPNI_CMDID_REMOVE_FS_ENT,
					  cmd_flags,
					  token);
	cmd_params = (struct dpni_cmd_remove_fs_entry *)cmd.params;
	cmd_params->tc_id = tc_id;
	cmd_params->key_size = cfg->key_size;
	cmd_params->key_iova = cpu_to_le64(cfg->key_iova);
	cmd_params->mask_iova = cpu_to_le64(cfg->mask_iova);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}

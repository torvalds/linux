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

/**
 * dprtc_create() - Create the DPRTC object.
 * @mc_io:	Pointer to MC portal's I/O object
 * @dprc_token:	Parent container token; '0' for default container
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @cfg:	Configuration structure
 * @obj_id:	Returned object id
 *
 * Create the DPRTC object, allocate required resources and
 * perform required initialization.
 *
 * The function accepts an authentication token of a parent
 * container that this object should be assigned to. The token
 * can be '0' so the object will be assigned to the default container.
 * The newly created object can be opened with the returned
 * object id and using the container's associated tokens and MC portals.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprtc_create(struct fsl_mc_io *mc_io,
		 u16 dprc_token,
		 u32 cmd_flags,
		 const struct dprtc_cfg *cfg,
		 u32 *obj_id)
{
	struct fsl_mc_command cmd = { 0 };
	int err;

	cmd.header = mc_encode_cmd_header(DPRTC_CMDID_CREATE,
					  cmd_flags,
					  dprc_token);

	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	*obj_id = mc_cmd_read_object_id(&cmd);

	return 0;
}

/**
 * dprtc_destroy() - Destroy the DPRTC object and release all its resources.
 * @mc_io:	Pointer to MC portal's I/O object
 * @dprc_token: Parent container token; '0' for default container
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @object_id:	The object id; it must be a valid id within the container that
 * created this object;
 *
 * The function accepts the authentication token of the parent container that
 * created the object (not the one that currently owns the object). The object
 * is searched within parent using the provided 'object_id'.
 * All tokens to the object must be closed before calling destroy.
 *
 * Return:	'0' on Success; error code otherwise.
 */
int dprtc_destroy(struct fsl_mc_io *mc_io,
		  u16 dprc_token,
		  u32 cmd_flags,
		  u32 object_id)
{
	struct dprtc_cmd_destroy *cmd_params;
	struct fsl_mc_command cmd = { 0 };

	cmd.header = mc_encode_cmd_header(DPRTC_CMDID_DESTROY,
					  cmd_flags,
					  dprc_token);
	cmd_params = (struct dprtc_cmd_destroy *)cmd.params;
	cmd_params->object_id = cpu_to_le32(object_id);

	return mc_send_command(mc_io, &cmd);
}

/**
 * dprtc_enable() - Enable the DPRTC.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRTC object
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprtc_enable(struct fsl_mc_io *mc_io,
		 u32 cmd_flags,
		 u16 token)
{
	struct fsl_mc_command cmd = { 0 };

	cmd.header = mc_encode_cmd_header(DPRTC_CMDID_ENABLE, cmd_flags,
					  token);

	return mc_send_command(mc_io, &cmd);
}

/**
 * dprtc_disable() - Disable the DPRTC.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRTC object
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprtc_disable(struct fsl_mc_io *mc_io,
		  u32 cmd_flags,
		  u16 token)
{
	struct fsl_mc_command cmd = { 0 };

	cmd.header = mc_encode_cmd_header(DPRTC_CMDID_DISABLE,
					  cmd_flags,
					  token);

	return mc_send_command(mc_io, &cmd);
}

/**
 * dprtc_is_enabled() - Check if the DPRTC is enabled.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRTC object
 * @en:		Returns '1' if object is enabled; '0' otherwise
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprtc_is_enabled(struct fsl_mc_io *mc_io,
		     u32 cmd_flags,
		     u16 token,
		     int *en)
{
	struct dprtc_rsp_is_enabled *rsp_params;
	struct fsl_mc_command cmd = { 0 };
	int err;

	cmd.header = mc_encode_cmd_header(DPRTC_CMDID_IS_ENABLED, cmd_flags,
					  token);

	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	rsp_params = (struct dprtc_rsp_is_enabled *)cmd.params;
	*en = dprtc_get_field(rsp_params->en, ENABLE);

	return 0;
}

/**
 * dprtc_reset() - Reset the DPRTC, returns the object to initial state.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRTC object
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprtc_reset(struct fsl_mc_io *mc_io,
		u32 cmd_flags,
		u16 token)
{
	struct fsl_mc_command cmd = { 0 };

	cmd.header = mc_encode_cmd_header(DPRTC_CMDID_RESET,
					  cmd_flags,
					  token);

	return mc_send_command(mc_io, &cmd);
}

/**
 * dprtc_set_irq_enable() - Set overall interrupt state.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRTC object
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
int dprtc_set_irq_enable(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 token,
			 u8 irq_index,
			 u8 en)
{
	struct dprtc_cmd_set_irq_enable *cmd_params;
	struct fsl_mc_command cmd = { 0 };

	cmd.header = mc_encode_cmd_header(DPRTC_CMDID_SET_IRQ_ENABLE,
					  cmd_flags,
					  token);
	cmd_params = (struct dprtc_cmd_set_irq_enable *)cmd.params;
	cmd_params->irq_index = irq_index;
	cmd_params->en = en;

	return mc_send_command(mc_io, &cmd);
}

/**
 * dprtc_get_irq_enable() - Get overall interrupt state
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRTC object
 * @irq_index:	The interrupt index to configure
 * @en:		Returned interrupt state - enable = 1, disable = 0
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprtc_get_irq_enable(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 token,
			 u8 irq_index,
			 u8 *en)
{
	struct dprtc_rsp_get_irq_enable *rsp_params;
	struct dprtc_cmd_get_irq *cmd_params;
	struct fsl_mc_command cmd = { 0 };
	int err;

	cmd.header = mc_encode_cmd_header(DPRTC_CMDID_GET_IRQ_ENABLE,
					  cmd_flags,
					  token);
	cmd_params = (struct dprtc_cmd_get_irq *)cmd.params;
	cmd_params->irq_index = irq_index;

	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	rsp_params = (struct dprtc_rsp_get_irq_enable *)cmd.params;
	*en = rsp_params->en;

	return 0;
}

/**
 * dprtc_set_irq_mask() - Set interrupt mask.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRTC object
 * @irq_index:	The interrupt index to configure
 * @mask:	Event mask to trigger interrupt;
 *		each bit:
 *			0 = ignore event
 *			1 = consider event for asserting IRQ
 *
 * Every interrupt can have up to 32 causes and the interrupt model supports
 * masking/unmasking each cause independently
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprtc_set_irq_mask(struct fsl_mc_io *mc_io,
		       u32 cmd_flags,
		       u16 token,
		       u8 irq_index,
		       u32 mask)
{
	struct dprtc_cmd_set_irq_mask *cmd_params;
	struct fsl_mc_command cmd = { 0 };

	cmd.header = mc_encode_cmd_header(DPRTC_CMDID_SET_IRQ_MASK,
					  cmd_flags,
					  token);
	cmd_params = (struct dprtc_cmd_set_irq_mask *)cmd.params;
	cmd_params->mask = cpu_to_le32(mask);
	cmd_params->irq_index = irq_index;

	return mc_send_command(mc_io, &cmd);
}

/**
 * dprtc_get_irq_mask() - Get interrupt mask.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRTC object
 * @irq_index:	The interrupt index to configure
 * @mask:	Returned event mask to trigger interrupt
 *
 * Every interrupt can have up to 32 causes and the interrupt model supports
 * masking/unmasking each cause independently
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprtc_get_irq_mask(struct fsl_mc_io *mc_io,
		       u32 cmd_flags,
		       u16 token,
		       u8 irq_index,
		       u32 *mask)
{
	struct dprtc_rsp_get_irq_mask *rsp_params;
	struct dprtc_cmd_get_irq *cmd_params;
	struct fsl_mc_command cmd = { 0 };
	int err;

	cmd.header = mc_encode_cmd_header(DPRTC_CMDID_GET_IRQ_MASK,
					  cmd_flags,
					  token);
	cmd_params = (struct dprtc_cmd_get_irq *)cmd.params;
	cmd_params->irq_index = irq_index;

	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	rsp_params = (struct dprtc_rsp_get_irq_mask *)cmd.params;
	*mask = le32_to_cpu(rsp_params->mask);

	return 0;
}

/**
 * dprtc_get_irq_status() - Get the current status of any pending interrupts.
 *
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRTC object
 * @irq_index:	The interrupt index to configure
 * @status:	Returned interrupts status - one bit per cause:
 *			0 = no interrupt pending
 *			1 = interrupt pending
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprtc_get_irq_status(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 token,
			 u8 irq_index,
			 u32 *status)
{
	struct dprtc_cmd_get_irq_status *cmd_params;
	struct dprtc_rsp_get_irq_status *rsp_params;
	struct fsl_mc_command cmd = { 0 };
	int err;

	cmd.header = mc_encode_cmd_header(DPRTC_CMDID_GET_IRQ_STATUS,
					  cmd_flags,
					  token);
	cmd_params = (struct dprtc_cmd_get_irq_status *)cmd.params;
	cmd_params->status = cpu_to_le32(*status);
	cmd_params->irq_index = irq_index;

	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	rsp_params = (struct dprtc_rsp_get_irq_status *)cmd.params;
	*status = le32_to_cpu(rsp_params->status);

	return 0;
}

/**
 * dprtc_clear_irq_status() - Clear a pending interrupt's status
 *
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRTC object
 * @irq_index:	The interrupt index to configure
 * @status:	Bits to clear (W1C) - one bit per cause:
 *			0 = don't change
 *			1 = clear status bit
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprtc_clear_irq_status(struct fsl_mc_io *mc_io,
			   u32 cmd_flags,
			   u16 token,
			   u8 irq_index,
			   u32 status)
{
	struct dprtc_cmd_clear_irq_status *cmd_params;
	struct fsl_mc_command cmd = { 0 };

	cmd.header = mc_encode_cmd_header(DPRTC_CMDID_CLEAR_IRQ_STATUS,
					  cmd_flags,
					  token);
	cmd_params = (struct dprtc_cmd_clear_irq_status *)cmd.params;
	cmd_params->irq_index = irq_index;
	cmd_params->status = cpu_to_le32(status);

	return mc_send_command(mc_io, &cmd);
}

/**
 * dprtc_get_attributes - Retrieve DPRTC attributes.
 *
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRTC object
 * @attr:	Returned object's attributes
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprtc_get_attributes(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 token,
			 struct dprtc_attr *attr)
{
	struct dprtc_rsp_get_attributes *rsp_params;
	struct fsl_mc_command cmd = { 0 };
	int err;

	cmd.header = mc_encode_cmd_header(DPRTC_CMDID_GET_ATTR,
					  cmd_flags,
					  token);

	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	rsp_params = (struct dprtc_rsp_get_attributes *)cmd.params;
	attr->id = le32_to_cpu(rsp_params->id);

	return 0;
}

/**
 * dprtc_set_clock_offset() - Sets the clock's offset
 * (usually relative to another clock).
 *
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRTC object
 * @offset:	New clock offset (in nanoseconds).
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprtc_set_clock_offset(struct fsl_mc_io *mc_io,
			   u32 cmd_flags,
			   u16 token,
			   int64_t offset)
{
	struct dprtc_cmd_set_clock_offset *cmd_params;
	struct fsl_mc_command cmd = { 0 };

	cmd.header = mc_encode_cmd_header(DPRTC_CMDID_SET_CLOCK_OFFSET,
					  cmd_flags,
					  token);
	cmd_params = (struct dprtc_cmd_set_clock_offset *)cmd.params;
	cmd_params->offset = cpu_to_le64(offset);

	return mc_send_command(mc_io, &cmd);
}

/**
 * dprtc_set_freq_compensation() - Sets a new frequency compensation value.
 *
 * @mc_io:		Pointer to MC portal's I/O object
 * @cmd_flags:		Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:		Token of DPRTC object
 * @freq_compensation:	The new frequency compensation value to set.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprtc_set_freq_compensation(struct fsl_mc_io *mc_io,
				u32 cmd_flags,
				u16 token,
				u32 freq_compensation)
{
	struct dprtc_get_freq_compensation *cmd_params;
	struct fsl_mc_command cmd = { 0 };

	cmd.header = mc_encode_cmd_header(DPRTC_CMDID_SET_FREQ_COMPENSATION,
					  cmd_flags,
					  token);
	cmd_params = (struct dprtc_get_freq_compensation *)cmd.params;
	cmd_params->freq_compensation = cpu_to_le32(freq_compensation);

	return mc_send_command(mc_io, &cmd);
}

/**
 * dprtc_get_freq_compensation() - Retrieves the frequency compensation value
 *
 * @mc_io:		Pointer to MC portal's I/O object
 * @cmd_flags:		Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:		Token of DPRTC object
 * @freq_compensation:	Frequency compensation value
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprtc_get_freq_compensation(struct fsl_mc_io *mc_io,
				u32 cmd_flags,
				u16 token,
				u32 *freq_compensation)
{
	struct dprtc_get_freq_compensation *rsp_params;
	struct fsl_mc_command cmd = { 0 };
	int err;

	cmd.header = mc_encode_cmd_header(DPRTC_CMDID_GET_FREQ_COMPENSATION,
					  cmd_flags,
					  token);

	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	rsp_params = (struct dprtc_get_freq_compensation *)cmd.params;
	*freq_compensation = le32_to_cpu(rsp_params->freq_compensation);

	return 0;
}

/**
 * dprtc_get_time() - Returns the current RTC time.
 *
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRTC object
 * @time:	Current RTC time.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprtc_get_time(struct fsl_mc_io *mc_io,
		   u32 cmd_flags,
		   u16 token,
		   uint64_t *time)
{
	struct dprtc_time *rsp_params;
	struct fsl_mc_command cmd = { 0 };
	int err;

	cmd.header = mc_encode_cmd_header(DPRTC_CMDID_GET_TIME,
					  cmd_flags,
					  token);

	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	rsp_params = (struct dprtc_time *)cmd.params;
	*time = le64_to_cpu(rsp_params->time);

	return 0;
}

/**
 * dprtc_set_time() - Updates current RTC time.
 *
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRTC object
 * @time:	New RTC time.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprtc_set_time(struct fsl_mc_io *mc_io,
		   u32 cmd_flags,
		   u16 token,
		   uint64_t time)
{
	struct dprtc_time *cmd_params;
	struct fsl_mc_command cmd = { 0 };

	cmd.header = mc_encode_cmd_header(DPRTC_CMDID_SET_TIME,
					  cmd_flags,
					  token);
	cmd_params = (struct dprtc_time *)cmd.params;
	cmd_params->time = cpu_to_le64(time);

	return mc_send_command(mc_io, &cmd);
}

/**
 * dprtc_set_alarm() - Defines and sets alarm.
 *
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPRTC object
 * @time:	In nanoseconds, the time when the alarm
 *			should go off - must be a multiple of
 *			1 microsecond
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprtc_set_alarm(struct fsl_mc_io *mc_io,
		    u32 cmd_flags,
		    u16 token, uint64_t time)
{
	struct dprtc_time *cmd_params;
	struct fsl_mc_command cmd = { 0 };

	cmd.header = mc_encode_cmd_header(DPRTC_CMDID_SET_ALARM,
					  cmd_flags,
					  token);
	cmd_params = (struct dprtc_time *)cmd.params;
	cmd_params->time = cpu_to_le64(time);

	return mc_send_command(mc_io, &cmd);
}

/**
 * dprtc_get_api_version() - Get Data Path Real Time Counter API version
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @major_ver:	Major version of data path real time counter API
 * @minor_ver:	Minor version of data path real time counter API
 *
 * Return:  '0' on Success; Error code otherwise.
 */
int dprtc_get_api_version(struct fsl_mc_io *mc_io,
			  u32 cmd_flags,
			  u16 *major_ver,
			  u16 *minor_ver)
{
	struct dprtc_rsp_get_api_version *rsp_params;
	struct fsl_mc_command cmd = { 0 };
	int err;

	cmd.header = mc_encode_cmd_header(DPRTC_CMDID_GET_API_VERSION,
					  cmd_flags,
					  0);

	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	rsp_params = (struct dprtc_rsp_get_api_version *)cmd.params;
	*major_ver = le16_to_cpu(rsp_params->major);
	*minor_ver = le16_to_cpu(rsp_params->minor);

	return 0;
}

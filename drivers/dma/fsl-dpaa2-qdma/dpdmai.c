// SPDX-License-Identifier: GPL-2.0
// Copyright 2019 NXP

#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/fsl/mc.h>
#include "dpdmai.h"

#define DEST_TYPE_MASK 0xF

struct dpdmai_rsp_get_attributes {
	__le32 id;
	u8 num_of_priorities;
	u8 num_of_queues;
	u8 pad0[2];
	__le16 major;
	__le16 minor;
};

struct dpdmai_cmd_queue {
	__le32 dest_id;
	u8 dest_priority;
	union {
		u8 queue;
		u8 pri;
	};
	u8 dest_type;
	u8 queue_idx;
	__le64 user_ctx;
	union {
		__le32 options;
		__le32 fqid;
	};
} __packed;

struct dpdmai_rsp_get_tx_queue {
	__le64 pad;
	__le32 fqid;
};

struct dpdmai_cmd_open {
	__le32 dpdmai_id;
} __packed;

struct dpdmai_cmd_destroy {
	__le32 dpdmai_id;
} __packed;

/**
 * dpdmai_open() - Open a control session for the specified object
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @dpdmai_id:	DPDMAI unique ID
 * @token:	Returned token; use in subsequent API calls
 *
 * This function can be used to open a control session for an
 * already created object; an object may have been declared in
 * the DPL or by calling the dpdmai_create() function.
 * This function returns a unique authentication token,
 * associated with the specific object ID and the specific MC
 * portal; this token must be used in all subsequent commands for
 * this specific object.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpdmai_open(struct fsl_mc_io *mc_io, u32 cmd_flags,
		int dpdmai_id, u16 *token)
{
	struct dpdmai_cmd_open *cmd_params;
	struct fsl_mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPDMAI_CMDID_OPEN,
					  cmd_flags, 0);

	cmd_params = (struct dpdmai_cmd_open *)&cmd.params;
	cmd_params->dpdmai_id = cpu_to_le32(dpdmai_id);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	*token = mc_cmd_hdr_read_token(&cmd);

	return 0;
}
EXPORT_SYMBOL_GPL(dpdmai_open);

/**
 * dpdmai_close() - Close the control session of the object
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPDMAI object
 *
 * After this function is called, no further operations are
 * allowed on the object without opening a new control session.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpdmai_close(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token)
{
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPDMAI_CMDID_CLOSE,
					  cmd_flags, token);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}
EXPORT_SYMBOL_GPL(dpdmai_close);

/**
 * dpdmai_destroy() - Destroy the DPDMAI object and release all its resources.
 * @mc_io:      Pointer to MC portal's I/O object
 * @cmd_flags:  Command flags; one or more of 'MC_CMD_FLAG_'
 * @dpdmai_id:	The object id; it must be a valid id within the container that created this object;
 * @token:      Token of DPDMAI object
 *
 * Return:      '0' on Success; error code otherwise.
 */
int dpdmai_destroy(struct fsl_mc_io *mc_io, u32 cmd_flags, u32 dpdmai_id, u16 token)
{
	struct dpdmai_cmd_destroy *cmd_params;
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPDMAI_CMDID_DESTROY,
					  cmd_flags, token);

	cmd_params = (struct dpdmai_cmd_destroy *)&cmd.params;
	cmd_params->dpdmai_id = cpu_to_le32(dpdmai_id);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}
EXPORT_SYMBOL_GPL(dpdmai_destroy);

/**
 * dpdmai_enable() - Enable the DPDMAI, allow sending and receiving frames.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPDMAI object
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpdmai_enable(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token)
{
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPDMAI_CMDID_ENABLE,
					  cmd_flags, token);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}
EXPORT_SYMBOL_GPL(dpdmai_enable);

/**
 * dpdmai_disable() - Disable the DPDMAI, stop sending and receiving frames.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPDMAI object
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpdmai_disable(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token)
{
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPDMAI_CMDID_DISABLE,
					  cmd_flags, token);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}
EXPORT_SYMBOL_GPL(dpdmai_disable);

/**
 * dpdmai_reset() - Reset the DPDMAI, returns the object to initial state.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPDMAI object
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpdmai_reset(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token)
{
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPDMAI_CMDID_RESET,
					  cmd_flags, token);

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}
EXPORT_SYMBOL_GPL(dpdmai_reset);

/**
 * dpdmai_get_attributes() - Retrieve DPDMAI attributes.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPDMAI object
 * @attr:	Returned object's attributes
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpdmai_get_attributes(struct fsl_mc_io *mc_io, u32 cmd_flags,
			  u16 token, struct dpdmai_attr *attr)
{
	struct dpdmai_rsp_get_attributes *rsp_params;
	struct fsl_mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPDMAI_CMDID_GET_ATTR,
					  cmd_flags, token);

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	rsp_params = (struct dpdmai_rsp_get_attributes *)cmd.params;
	attr->id = le32_to_cpu(rsp_params->id);
	attr->version.major = le16_to_cpu(rsp_params->major);
	attr->version.minor = le16_to_cpu(rsp_params->minor);
	attr->num_of_priorities = rsp_params->num_of_priorities;
	attr->num_of_queues = rsp_params->num_of_queues;

	return 0;
}
EXPORT_SYMBOL_GPL(dpdmai_get_attributes);

/**
 * dpdmai_set_rx_queue() - Set Rx queue configuration
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPDMAI object
 * @queue_idx:	DMA queue index
 * @priority:	Select the queue relative to number of
 *		priorities configured at DPDMAI creation
 * @cfg:	Rx queue configuration
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpdmai_set_rx_queue(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token, u8 queue_idx,
			u8 priority, const struct dpdmai_rx_queue_cfg *cfg)
{
	struct dpdmai_cmd_queue *cmd_params;
	struct fsl_mc_command cmd = { 0 };

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPDMAI_CMDID_SET_RX_QUEUE,
					  cmd_flags, token);

	cmd_params = (struct dpdmai_cmd_queue *)cmd.params;
	cmd_params->dest_id = cpu_to_le32(cfg->dest_cfg.dest_id);
	cmd_params->dest_priority = cfg->dest_cfg.priority;
	cmd_params->pri = priority;
	cmd_params->dest_type = cfg->dest_cfg.dest_type;
	cmd_params->user_ctx = cpu_to_le64(cfg->user_ctx);
	cmd_params->options = cpu_to_le32(cfg->options);
	cmd_params->queue_idx = queue_idx;

	/* send command to mc*/
	return mc_send_command(mc_io, &cmd);
}
EXPORT_SYMBOL_GPL(dpdmai_set_rx_queue);

/**
 * dpdmai_get_rx_queue() - Retrieve Rx queue attributes.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPDMAI object
 * @queue_idx:	DMA Queue index
 * @priority:	Select the queue relative to number of
 *				priorities configured at DPDMAI creation
 * @attr:	Returned Rx queue attributes
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpdmai_get_rx_queue(struct fsl_mc_io *mc_io, u32 cmd_flags, u16 token, u8 queue_idx,
			u8 priority, struct dpdmai_rx_queue_attr *attr)
{
	struct dpdmai_cmd_queue *cmd_params;
	struct fsl_mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPDMAI_CMDID_GET_RX_QUEUE,
					  cmd_flags, token);

	cmd_params = (struct dpdmai_cmd_queue *)cmd.params;
	cmd_params->queue = priority;
	cmd_params->queue_idx = queue_idx;

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */
	attr->dest_cfg.dest_id = le32_to_cpu(cmd_params->dest_id);
	attr->dest_cfg.priority = cmd_params->dest_priority;
	attr->dest_cfg.dest_type = FIELD_GET(DEST_TYPE_MASK, cmd_params->dest_type);
	attr->user_ctx = le64_to_cpu(cmd_params->user_ctx);
	attr->fqid = le32_to_cpu(cmd_params->fqid);

	return 0;
}
EXPORT_SYMBOL_GPL(dpdmai_get_rx_queue);

/**
 * dpdmai_get_tx_queue() - Retrieve Tx queue attributes.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cmd_flags:	Command flags; one or more of 'MC_CMD_FLAG_'
 * @token:	Token of DPDMAI object
 * @queue_idx:	DMA queue index
 * @priority:	Select the queue relative to number of
 *			priorities configured at DPDMAI creation
 * @attr:	Returned DMA Tx queue attributes
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpdmai_get_tx_queue(struct fsl_mc_io *mc_io, u32 cmd_flags,
			u16 token, u8 queue_idx, u8 priority, struct dpdmai_tx_queue_attr *attr)
{
	struct dpdmai_rsp_get_tx_queue *rsp_params;
	struct dpdmai_cmd_queue *cmd_params;
	struct fsl_mc_command cmd = { 0 };
	int err;

	/* prepare command */
	cmd.header = mc_encode_cmd_header(DPDMAI_CMDID_GET_TX_QUEUE,
					  cmd_flags, token);

	cmd_params = (struct dpdmai_cmd_queue *)cmd.params;
	cmd_params->queue = priority;
	cmd_params->queue_idx = queue_idx;

	/* send command to mc*/
	err = mc_send_command(mc_io, &cmd);
	if (err)
		return err;

	/* retrieve response parameters */

	rsp_params = (struct dpdmai_rsp_get_tx_queue *)cmd.params;
	attr->fqid = le32_to_cpu(rsp_params->fqid);

	return 0;
}
EXPORT_SYMBOL_GPL(dpdmai_get_tx_queue);

MODULE_DESCRIPTION("NXP DPAA2 QDMA driver");
MODULE_LICENSE("GPL v2");

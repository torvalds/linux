/*
 * Abilis Systems Single DVB-T Receiver
 * Copyright (C) 2008 Pierrick Hascoet <pierrick.hascoet@abilis.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include "as102_drv.h"
#include "as10x_cmd.h"

/**
 * as10x_cmd_add_PID_filter - send add filter command to AS10x
 * @adap:      pointer to AS10x bus adapter
 * @filter:    TSFilter filter for DVB-T
 *
 * Return 0 on success or negative value in case of error.
 */
int as10x_cmd_add_PID_filter(struct as10x_bus_adapter_t *adap,
			     struct as10x_ts_filter *filter)
{
	int error;
	struct as10x_cmd_t *pcmd, *prsp;

	pcmd = adap->cmd;
	prsp = adap->rsp;

	/* prepare command */
	as10x_cmd_build(pcmd, (++adap->cmd_xid),
			sizeof(pcmd->body.add_pid_filter.req));

	/* fill command */
	pcmd->body.add_pid_filter.req.proc_id =
		cpu_to_le16(CONTROL_PROC_SETFILTER);
	pcmd->body.add_pid_filter.req.pid = cpu_to_le16(filter->pid);
	pcmd->body.add_pid_filter.req.stream_type = filter->type;

	if (filter->idx < 16)
		pcmd->body.add_pid_filter.req.idx = filter->idx;
	else
		pcmd->body.add_pid_filter.req.idx = 0xFF;

	/* send command */
	if (adap->ops->xfer_cmd) {
		error = adap->ops->xfer_cmd(adap, (uint8_t *) pcmd,
				sizeof(pcmd->body.add_pid_filter.req)
				+ HEADER_SIZE, (uint8_t *) prsp,
				sizeof(prsp->body.add_pid_filter.rsp)
				+ HEADER_SIZE);
	} else {
		error = AS10X_CMD_ERROR;
	}

	if (error < 0)
		goto out;

	/* parse response */
	error = as10x_rsp_parse(prsp, CONTROL_PROC_SETFILTER_RSP);

	if (error == 0) {
		/* Response OK -> get response data */
		filter->idx = prsp->body.add_pid_filter.rsp.filter_id;
	}

out:
	return error;
}

/**
 * as10x_cmd_del_PID_filter - Send delete filter command to AS10x
 * @adap:         pointer to AS10x bus adapte
 * @pid_value:    PID to delete
 *
 * Return 0 on success or negative value in case of error.
 */
int as10x_cmd_del_PID_filter(struct as10x_bus_adapter_t *adap,
			     uint16_t pid_value)
{
	int error;
	struct as10x_cmd_t *pcmd, *prsp;

	pcmd = adap->cmd;
	prsp = adap->rsp;

	/* prepare command */
	as10x_cmd_build(pcmd, (++adap->cmd_xid),
			sizeof(pcmd->body.del_pid_filter.req));

	/* fill command */
	pcmd->body.del_pid_filter.req.proc_id =
		cpu_to_le16(CONTROL_PROC_REMOVEFILTER);
	pcmd->body.del_pid_filter.req.pid = cpu_to_le16(pid_value);

	/* send command */
	if (adap->ops->xfer_cmd) {
		error = adap->ops->xfer_cmd(adap, (uint8_t *) pcmd,
				sizeof(pcmd->body.del_pid_filter.req)
				+ HEADER_SIZE, (uint8_t *) prsp,
				sizeof(prsp->body.del_pid_filter.rsp)
				+ HEADER_SIZE);
	} else {
		error = AS10X_CMD_ERROR;
	}

	if (error < 0)
		goto out;

	/* parse response */
	error = as10x_rsp_parse(prsp, CONTROL_PROC_REMOVEFILTER_RSP);

out:
	return error;
}

/**
 * as10x_cmd_start_streaming - Send start streaming command to AS10x
 * @adap:   pointer to AS10x bus adapter
 *
 * Return 0 on success or negative value in case of error.
 */
int as10x_cmd_start_streaming(struct as10x_bus_adapter_t *adap)
{
	int error;
	struct as10x_cmd_t *pcmd, *prsp;

	pcmd = adap->cmd;
	prsp = adap->rsp;

	/* prepare command */
	as10x_cmd_build(pcmd, (++adap->cmd_xid),
			sizeof(pcmd->body.start_streaming.req));

	/* fill command */
	pcmd->body.start_streaming.req.proc_id =
		cpu_to_le16(CONTROL_PROC_START_STREAMING);

	/* send command */
	if (adap->ops->xfer_cmd) {
		error = adap->ops->xfer_cmd(adap, (uint8_t *) pcmd,
				sizeof(pcmd->body.start_streaming.req)
				+ HEADER_SIZE, (uint8_t *) prsp,
				sizeof(prsp->body.start_streaming.rsp)
				+ HEADER_SIZE);
	} else {
		error = AS10X_CMD_ERROR;
	}

	if (error < 0)
		goto out;

	/* parse response */
	error = as10x_rsp_parse(prsp, CONTROL_PROC_START_STREAMING_RSP);

out:
	return error;
}

/**
 * as10x_cmd_stop_streaming - Send stop streaming command to AS10x
 * @adap:   pointer to AS10x bus adapter
 *
 * Return 0 on success or negative value in case of error.
 */
int as10x_cmd_stop_streaming(struct as10x_bus_adapter_t *adap)
{
	int8_t error;
	struct as10x_cmd_t *pcmd, *prsp;

	pcmd = adap->cmd;
	prsp = adap->rsp;

	/* prepare command */
	as10x_cmd_build(pcmd, (++adap->cmd_xid),
			sizeof(pcmd->body.stop_streaming.req));

	/* fill command */
	pcmd->body.stop_streaming.req.proc_id =
		cpu_to_le16(CONTROL_PROC_STOP_STREAMING);

	/* send command */
	if (adap->ops->xfer_cmd) {
		error = adap->ops->xfer_cmd(adap, (uint8_t *) pcmd,
				sizeof(pcmd->body.stop_streaming.req)
				+ HEADER_SIZE, (uint8_t *) prsp,
				sizeof(prsp->body.stop_streaming.rsp)
				+ HEADER_SIZE);
	} else {
		error = AS10X_CMD_ERROR;
	}

	if (error < 0)
		goto out;

	/* parse response */
	error = as10x_rsp_parse(prsp, CONTROL_PROC_STOP_STREAMING_RSP);

out:
	return error;
}

// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#include "dc_fused_io.h"

#include "dm_helpers.h"
#include "gpio.h"

static bool op_i2c_convert(
		union dmub_rb_cmd *cmd,
		const struct mod_hdcp_atomic_op_i2c *op,
		enum dmub_cmd_fused_request_type type,
		uint32_t ddc_line,
		bool over_aux
)
{
	struct dmub_cmd_fused_request *req = &cmd->fused_io.request;
	struct dmub_cmd_fused_request_location_i2c *loc = &req->u.i2c;

	if (!op || op->size > sizeof(req->buffer))
		return false;

	req->type = type;
	loc->is_aux = false;
	loc->ddc_line = ddc_line;
	loc->over_aux = over_aux;
	loc->address = op->address;
	loc->offset = op->offset;
	loc->length = op->size;
	memcpy(req->buffer, op->data, op->size);

	return true;
}

static bool op_aux_convert(
		union dmub_rb_cmd *cmd,
		const struct mod_hdcp_atomic_op_aux *op,
		enum dmub_cmd_fused_request_type type,
		uint32_t ddc_line
)
{
	struct dmub_cmd_fused_request *req = &cmd->fused_io.request;
	struct dmub_cmd_fused_request_location_aux *loc = &req->u.aux;

	if (!op || op->size > sizeof(req->buffer))
		return false;

	req->type = type;
	loc->is_aux = true;
	loc->ddc_line = ddc_line;
	loc->address = op->address;
	loc->length = op->size;
	memcpy(req->buffer, op->data, op->size);

	return true;
}

static bool atomic_write_poll_read(
		struct dc_link *link,
		union dmub_rb_cmd commands[3],
		uint32_t poll_timeout_us,
		uint8_t poll_mask_msb
)
{
	const uint8_t count = 3;
	const uint32_t timeout_per_request_us = 10000;
	const uint32_t timeout_per_aux_transaction_us = 10000;
	uint64_t timeout_us = 0;

	commands[1].fused_io.request.poll_mask_msb = poll_mask_msb;
	commands[1].fused_io.request.timeout_us = poll_timeout_us;

	for (uint8_t i = 0; i < count; i++) {
		struct dmub_rb_cmd_fused_io *io = &commands[i].fused_io;

		io->header.type = DMUB_CMD__FUSED_IO;
		io->header.sub_type = DMUB_CMD__FUSED_IO_EXECUTE;
		io->header.multi_cmd_pending = i != count - 1;
		io->header.payload_bytes = sizeof(commands[i].fused_io) - sizeof(io->header);

		timeout_us += timeout_per_request_us + io->request.timeout_us;
		if (!io->request.timeout_us && io->request.u.aux.is_aux)
			timeout_us += timeout_per_aux_transaction_us * (io->request.u.aux.length / 16);
	}

	if (!dm_helpers_execute_fused_io(link->ctx, link, commands, count, timeout_us))
		return false;

	return commands[0].fused_io.request.status == FUSED_REQUEST_STATUS_SUCCESS;
}

bool dm_atomic_write_poll_read_i2c(
		struct dc_link *link,
		const struct mod_hdcp_atomic_op_i2c *write,
		const struct mod_hdcp_atomic_op_i2c *poll,
		struct mod_hdcp_atomic_op_i2c *read,
		uint32_t poll_timeout_us,
		uint8_t poll_mask_msb
)
{
	if (!link)
		return false;

	const bool over_aux = false;
	const uint32_t ddc_line = link->ddc->ddc_pin->pin_data->en;

	union dmub_rb_cmd commands[3] = { 0 };
	const bool converted = op_i2c_convert(&commands[0], write, FUSED_REQUEST_WRITE, ddc_line, over_aux)
			&& op_i2c_convert(&commands[1], poll, FUSED_REQUEST_POLL, ddc_line, over_aux)
			&& op_i2c_convert(&commands[2], read, FUSED_REQUEST_READ, ddc_line, over_aux);

	if (!converted)
		return false;

	const bool result = atomic_write_poll_read(link, commands, poll_timeout_us, poll_mask_msb);

	memcpy(read->data, commands[0].fused_io.request.buffer, read->size);
	return result;
}

bool dm_atomic_write_poll_read_aux(
		struct dc_link *link,
		const struct mod_hdcp_atomic_op_aux *write,
		const struct mod_hdcp_atomic_op_aux *poll,
		struct mod_hdcp_atomic_op_aux *read,
		uint32_t poll_timeout_us,
		uint8_t poll_mask_msb
)
{
	if (!link)
		return false;

	const uint32_t ddc_line = link->ddc->ddc_pin->pin_data->en;
	union dmub_rb_cmd commands[3] = { 0 };
	const bool converted = op_aux_convert(&commands[0], write, FUSED_REQUEST_WRITE, ddc_line)
			&& op_aux_convert(&commands[1], poll, FUSED_REQUEST_POLL, ddc_line)
			&& op_aux_convert(&commands[2], read, FUSED_REQUEST_READ, ddc_line);

	if (!converted)
		return false;

	const bool result = atomic_write_poll_read(link, commands, poll_timeout_us, poll_mask_msb);

	memcpy(read->data, commands[0].fused_io.request.buffer, read->size);
	return result;
}


/*
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
/*
 * dc_helper.c
 *
 *  Created on: Aug 30, 2016
 *      Author: agrodzov
 */

#include <linux/delay.h>

#include "dm_services.h"
#include <stdarg.h>

#include "dc.h"
#include "dc_dmub_srv.h"

static inline void submit_dmub_read_modify_write(
	struct dc_reg_helper_state *offload,
	const struct dc_context *ctx)
{
	struct dmub_rb_cmd_read_modify_write *cmd_buf = &offload->cmd_data.read_modify_write;
	bool gather = false;

	offload->should_burst_write =
			(offload->same_addr_count == (DMUB_READ_MODIFY_WRITE_SEQ__MAX - 1));
	cmd_buf->header.payload_bytes =
			sizeof(struct dmub_cmd_read_modify_write_sequence) * offload->reg_seq_count;

	gather = ctx->dmub_srv->reg_helper_offload.gather_in_progress;
	ctx->dmub_srv->reg_helper_offload.gather_in_progress = false;

	dc_dmub_srv_cmd_queue(ctx->dmub_srv, &cmd_buf->header);

	ctx->dmub_srv->reg_helper_offload.gather_in_progress = gather;

	memset(cmd_buf, 0, sizeof(*cmd_buf));

	offload->reg_seq_count = 0;
	offload->same_addr_count = 0;
}

static inline void submit_dmub_burst_write(
	struct dc_reg_helper_state *offload,
	const struct dc_context *ctx)
{
	struct dmub_rb_cmd_burst_write *cmd_buf = &offload->cmd_data.burst_write;
	bool gather = false;

	cmd_buf->header.payload_bytes =
			sizeof(uint32_t) * offload->reg_seq_count;

	gather = ctx->dmub_srv->reg_helper_offload.gather_in_progress;
	ctx->dmub_srv->reg_helper_offload.gather_in_progress = false;

	dc_dmub_srv_cmd_queue(ctx->dmub_srv, &cmd_buf->header);

	ctx->dmub_srv->reg_helper_offload.gather_in_progress = gather;

	memset(cmd_buf, 0, sizeof(*cmd_buf));

	offload->reg_seq_count = 0;
}

static inline void submit_dmub_reg_wait(
		struct dc_reg_helper_state *offload,
		const struct dc_context *ctx)
{
	struct dmub_rb_cmd_reg_wait *cmd_buf = &offload->cmd_data.reg_wait;
	bool gather = false;

	gather = ctx->dmub_srv->reg_helper_offload.gather_in_progress;
	ctx->dmub_srv->reg_helper_offload.gather_in_progress = false;

	dc_dmub_srv_cmd_queue(ctx->dmub_srv, &cmd_buf->header);

	memset(cmd_buf, 0, sizeof(*cmd_buf));
	offload->reg_seq_count = 0;

	ctx->dmub_srv->reg_helper_offload.gather_in_progress = gather;
}

struct dc_reg_value_masks {
	uint32_t value;
	uint32_t mask;
};

struct dc_reg_sequence {
	uint32_t addr;
	struct dc_reg_value_masks value_masks;
};

static inline void set_reg_field_value_masks(
	struct dc_reg_value_masks *field_value_mask,
	uint32_t value,
	uint32_t mask,
	uint8_t shift)
{
	ASSERT(mask != 0);

	field_value_mask->value = (field_value_mask->value & ~mask) | (mask & (value << shift));
	field_value_mask->mask = field_value_mask->mask | mask;
}

static void set_reg_field_values(struct dc_reg_value_masks *field_value_mask,
		uint32_t addr, int n,
		uint8_t shift1, uint32_t mask1, uint32_t field_value1,
		va_list ap)
{
	uint32_t shift, mask, field_value;
	int i = 1;

	/* gather all bits value/mask getting updated in this register */
	set_reg_field_value_masks(field_value_mask,
			field_value1, mask1, shift1);

	while (i < n) {
		shift = va_arg(ap, uint32_t);
		mask = va_arg(ap, uint32_t);
		field_value = va_arg(ap, uint32_t);

		set_reg_field_value_masks(field_value_mask,
				field_value, mask, shift);
		i++;
	}
}

static void dmub_flush_buffer_execute(
		struct dc_reg_helper_state *offload,
		const struct dc_context *ctx)
{
	submit_dmub_read_modify_write(offload, ctx);
	dc_dmub_srv_cmd_execute(ctx->dmub_srv);
}

static void dmub_flush_burst_write_buffer_execute(
		struct dc_reg_helper_state *offload,
		const struct dc_context *ctx)
{
	submit_dmub_burst_write(offload, ctx);
	dc_dmub_srv_cmd_execute(ctx->dmub_srv);
}

static bool dmub_reg_value_burst_set_pack(const struct dc_context *ctx, uint32_t addr,
		uint32_t reg_val)
{
	struct dc_reg_helper_state *offload = &ctx->dmub_srv->reg_helper_offload;
	struct dmub_rb_cmd_burst_write *cmd_buf = &offload->cmd_data.burst_write;

	/* flush command if buffer is full */
	if (offload->reg_seq_count == DMUB_BURST_WRITE_VALUES__MAX)
		dmub_flush_burst_write_buffer_execute(offload, ctx);

	if (offload->cmd_data.cmd_common.header.type == DMUB_CMD__REG_SEQ_BURST_WRITE &&
			addr != cmd_buf->addr) {
		dmub_flush_burst_write_buffer_execute(offload, ctx);
		return false;
	}

	cmd_buf->header.type = DMUB_CMD__REG_SEQ_BURST_WRITE;
	cmd_buf->header.sub_type = 0;
	cmd_buf->addr = addr;
	cmd_buf->write_values[offload->reg_seq_count] = reg_val;
	offload->reg_seq_count++;

	return true;
}

static uint32_t dmub_reg_value_pack(const struct dc_context *ctx, uint32_t addr,
		struct dc_reg_value_masks *field_value_mask)
{
	struct dc_reg_helper_state *offload = &ctx->dmub_srv->reg_helper_offload;
	struct dmub_rb_cmd_read_modify_write *cmd_buf = &offload->cmd_data.read_modify_write;
	struct dmub_cmd_read_modify_write_sequence *seq;

	/* flush command if buffer is full */
	if (offload->cmd_data.cmd_common.header.type != DMUB_CMD__REG_SEQ_BURST_WRITE &&
			offload->reg_seq_count == DMUB_READ_MODIFY_WRITE_SEQ__MAX)
		dmub_flush_buffer_execute(offload, ctx);

	if (offload->should_burst_write) {
		if (dmub_reg_value_burst_set_pack(ctx, addr, field_value_mask->value))
			return field_value_mask->value;
		else
			offload->should_burst_write = false;
	}

	/* pack commands */
	cmd_buf->header.type = DMUB_CMD__REG_SEQ_READ_MODIFY_WRITE;
	cmd_buf->header.sub_type = 0;
	seq = &cmd_buf->seq[offload->reg_seq_count];

	if (offload->reg_seq_count) {
		if (cmd_buf->seq[offload->reg_seq_count - 1].addr == addr)
			offload->same_addr_count++;
		else
			offload->same_addr_count = 0;
	}

	seq->addr = addr;
	seq->modify_mask = field_value_mask->mask;
	seq->modify_value = field_value_mask->value;
	offload->reg_seq_count++;

	return field_value_mask->value;
}

static void dmub_reg_wait_done_pack(const struct dc_context *ctx, uint32_t addr,
		uint32_t mask, uint32_t shift, uint32_t condition_value, uint32_t time_out_us)
{
	struct dc_reg_helper_state *offload = &ctx->dmub_srv->reg_helper_offload;
	struct dmub_rb_cmd_reg_wait *cmd_buf = &offload->cmd_data.reg_wait;

	cmd_buf->header.type = DMUB_CMD__REG_REG_WAIT;
	cmd_buf->header.sub_type = 0;
	cmd_buf->reg_wait.addr = addr;
	cmd_buf->reg_wait.condition_field_value = mask & (condition_value << shift);
	cmd_buf->reg_wait.mask = mask;
	cmd_buf->reg_wait.time_out_us = time_out_us;
}

uint32_t generic_reg_update_ex(const struct dc_context *ctx,
		uint32_t addr, int n,
		uint8_t shift1, uint32_t mask1, uint32_t field_value1,
		...)
{
	struct dc_reg_value_masks field_value_mask = {0};
	uint32_t reg_val;
	va_list ap;

	va_start(ap, field_value1);

	set_reg_field_values(&field_value_mask, addr, n, shift1, mask1,
			field_value1, ap);

	va_end(ap);

	if (ctx->dmub_srv &&
	    ctx->dmub_srv->reg_helper_offload.gather_in_progress)
		return dmub_reg_value_pack(ctx, addr, &field_value_mask);
		/* todo: return void so we can decouple code running in driver from register states */

	/* mmio write directly */
	reg_val = dm_read_reg(ctx, addr);
	reg_val = (reg_val & ~field_value_mask.mask) | field_value_mask.value;
	dm_write_reg(ctx, addr, reg_val);
	return reg_val;
}

uint32_t generic_reg_set_ex(const struct dc_context *ctx,
		uint32_t addr, uint32_t reg_val, int n,
		uint8_t shift1, uint32_t mask1, uint32_t field_value1,
		...)
{
	struct dc_reg_value_masks field_value_mask = {0};
	va_list ap;

	va_start(ap, field_value1);

	set_reg_field_values(&field_value_mask, addr, n, shift1, mask1,
			field_value1, ap);

	va_end(ap);


	/* mmio write directly */
	reg_val = (reg_val & ~field_value_mask.mask) | field_value_mask.value;

	if (ctx->dmub_srv &&
	    ctx->dmub_srv->reg_helper_offload.gather_in_progress) {
		return dmub_reg_value_burst_set_pack(ctx, addr, reg_val);
		/* todo: return void so we can decouple code running in driver from register states */
	}

	dm_write_reg(ctx, addr, reg_val);
	return reg_val;
}

uint32_t dm_read_reg_func(
	const struct dc_context *ctx,
	uint32_t address,
	const char *func_name)
{
	uint32_t value;
#ifdef DM_CHECK_ADDR_0
	if (address == 0) {
		DC_ERR("invalid register read; address = 0\n");
		return 0;
	}
#endif

	if (ctx->dmub_srv &&
	    ctx->dmub_srv->reg_helper_offload.gather_in_progress &&
	    !ctx->dmub_srv->reg_helper_offload.should_burst_write) {
		ASSERT(false);
		return 0;
	}

	value = cgs_read_register(ctx->cgs_device, address);
	trace_amdgpu_dc_rreg(&ctx->perf_trace->read_count, address, value);

	return value;
}

uint32_t generic_reg_get(const struct dc_context *ctx, uint32_t addr,
		uint8_t shift, uint32_t mask, uint32_t *field_value)
{
	uint32_t reg_val = dm_read_reg(ctx, addr);
	*field_value = get_reg_field_value_ex(reg_val, mask, shift);
	return reg_val;
}

uint32_t generic_reg_get2(const struct dc_context *ctx, uint32_t addr,
		uint8_t shift1, uint32_t mask1, uint32_t *field_value1,
		uint8_t shift2, uint32_t mask2, uint32_t *field_value2)
{
	uint32_t reg_val = dm_read_reg(ctx, addr);
	*field_value1 = get_reg_field_value_ex(reg_val, mask1, shift1);
	*field_value2 = get_reg_field_value_ex(reg_val, mask2, shift2);
	return reg_val;
}

uint32_t generic_reg_get3(const struct dc_context *ctx, uint32_t addr,
		uint8_t shift1, uint32_t mask1, uint32_t *field_value1,
		uint8_t shift2, uint32_t mask2, uint32_t *field_value2,
		uint8_t shift3, uint32_t mask3, uint32_t *field_value3)
{
	uint32_t reg_val = dm_read_reg(ctx, addr);
	*field_value1 = get_reg_field_value_ex(reg_val, mask1, shift1);
	*field_value2 = get_reg_field_value_ex(reg_val, mask2, shift2);
	*field_value3 = get_reg_field_value_ex(reg_val, mask3, shift3);
	return reg_val;
}

uint32_t generic_reg_get4(const struct dc_context *ctx, uint32_t addr,
		uint8_t shift1, uint32_t mask1, uint32_t *field_value1,
		uint8_t shift2, uint32_t mask2, uint32_t *field_value2,
		uint8_t shift3, uint32_t mask3, uint32_t *field_value3,
		uint8_t shift4, uint32_t mask4, uint32_t *field_value4)
{
	uint32_t reg_val = dm_read_reg(ctx, addr);
	*field_value1 = get_reg_field_value_ex(reg_val, mask1, shift1);
	*field_value2 = get_reg_field_value_ex(reg_val, mask2, shift2);
	*field_value3 = get_reg_field_value_ex(reg_val, mask3, shift3);
	*field_value4 = get_reg_field_value_ex(reg_val, mask4, shift4);
	return reg_val;
}

uint32_t generic_reg_get5(const struct dc_context *ctx, uint32_t addr,
		uint8_t shift1, uint32_t mask1, uint32_t *field_value1,
		uint8_t shift2, uint32_t mask2, uint32_t *field_value2,
		uint8_t shift3, uint32_t mask3, uint32_t *field_value3,
		uint8_t shift4, uint32_t mask4, uint32_t *field_value4,
		uint8_t shift5, uint32_t mask5, uint32_t *field_value5)
{
	uint32_t reg_val = dm_read_reg(ctx, addr);
	*field_value1 = get_reg_field_value_ex(reg_val, mask1, shift1);
	*field_value2 = get_reg_field_value_ex(reg_val, mask2, shift2);
	*field_value3 = get_reg_field_value_ex(reg_val, mask3, shift3);
	*field_value4 = get_reg_field_value_ex(reg_val, mask4, shift4);
	*field_value5 = get_reg_field_value_ex(reg_val, mask5, shift5);
	return reg_val;
}

uint32_t generic_reg_get6(const struct dc_context *ctx, uint32_t addr,
		uint8_t shift1, uint32_t mask1, uint32_t *field_value1,
		uint8_t shift2, uint32_t mask2, uint32_t *field_value2,
		uint8_t shift3, uint32_t mask3, uint32_t *field_value3,
		uint8_t shift4, uint32_t mask4, uint32_t *field_value4,
		uint8_t shift5, uint32_t mask5, uint32_t *field_value5,
		uint8_t shift6, uint32_t mask6, uint32_t *field_value6)
{
	uint32_t reg_val = dm_read_reg(ctx, addr);
	*field_value1 = get_reg_field_value_ex(reg_val, mask1, shift1);
	*field_value2 = get_reg_field_value_ex(reg_val, mask2, shift2);
	*field_value3 = get_reg_field_value_ex(reg_val, mask3, shift3);
	*field_value4 = get_reg_field_value_ex(reg_val, mask4, shift4);
	*field_value5 = get_reg_field_value_ex(reg_val, mask5, shift5);
	*field_value6 = get_reg_field_value_ex(reg_val, mask6, shift6);
	return reg_val;
}

uint32_t generic_reg_get7(const struct dc_context *ctx, uint32_t addr,
		uint8_t shift1, uint32_t mask1, uint32_t *field_value1,
		uint8_t shift2, uint32_t mask2, uint32_t *field_value2,
		uint8_t shift3, uint32_t mask3, uint32_t *field_value3,
		uint8_t shift4, uint32_t mask4, uint32_t *field_value4,
		uint8_t shift5, uint32_t mask5, uint32_t *field_value5,
		uint8_t shift6, uint32_t mask6, uint32_t *field_value6,
		uint8_t shift7, uint32_t mask7, uint32_t *field_value7)
{
	uint32_t reg_val = dm_read_reg(ctx, addr);
	*field_value1 = get_reg_field_value_ex(reg_val, mask1, shift1);
	*field_value2 = get_reg_field_value_ex(reg_val, mask2, shift2);
	*field_value3 = get_reg_field_value_ex(reg_val, mask3, shift3);
	*field_value4 = get_reg_field_value_ex(reg_val, mask4, shift4);
	*field_value5 = get_reg_field_value_ex(reg_val, mask5, shift5);
	*field_value6 = get_reg_field_value_ex(reg_val, mask6, shift6);
	*field_value7 = get_reg_field_value_ex(reg_val, mask7, shift7);
	return reg_val;
}

uint32_t generic_reg_get8(const struct dc_context *ctx, uint32_t addr,
		uint8_t shift1, uint32_t mask1, uint32_t *field_value1,
		uint8_t shift2, uint32_t mask2, uint32_t *field_value2,
		uint8_t shift3, uint32_t mask3, uint32_t *field_value3,
		uint8_t shift4, uint32_t mask4, uint32_t *field_value4,
		uint8_t shift5, uint32_t mask5, uint32_t *field_value5,
		uint8_t shift6, uint32_t mask6, uint32_t *field_value6,
		uint8_t shift7, uint32_t mask7, uint32_t *field_value7,
		uint8_t shift8, uint32_t mask8, uint32_t *field_value8)
{
	uint32_t reg_val = dm_read_reg(ctx, addr);
	*field_value1 = get_reg_field_value_ex(reg_val, mask1, shift1);
	*field_value2 = get_reg_field_value_ex(reg_val, mask2, shift2);
	*field_value3 = get_reg_field_value_ex(reg_val, mask3, shift3);
	*field_value4 = get_reg_field_value_ex(reg_val, mask4, shift4);
	*field_value5 = get_reg_field_value_ex(reg_val, mask5, shift5);
	*field_value6 = get_reg_field_value_ex(reg_val, mask6, shift6);
	*field_value7 = get_reg_field_value_ex(reg_val, mask7, shift7);
	*field_value8 = get_reg_field_value_ex(reg_val, mask8, shift8);
	return reg_val;
}
/* note:  va version of this is pretty bad idea, since there is a output parameter pass by pointer
 * compiler won't be able to check for size match and is prone to stack corruption type of bugs

uint32_t generic_reg_get(const struct dc_context *ctx,
		uint32_t addr, int n, ...)
{
	uint32_t shift, mask;
	uint32_t *field_value;
	uint32_t reg_val;
	int i = 0;

	reg_val = dm_read_reg(ctx, addr);

	va_list ap;
	va_start(ap, n);

	while (i < n) {
		shift = va_arg(ap, uint32_t);
		mask = va_arg(ap, uint32_t);
		field_value = va_arg(ap, uint32_t *);

		*field_value = get_reg_field_value_ex(reg_val, mask, shift);
		i++;
	}

	va_end(ap);

	return reg_val;
}
*/

void generic_reg_wait(const struct dc_context *ctx,
	uint32_t addr, uint32_t shift, uint32_t mask, uint32_t condition_value,
	unsigned int delay_between_poll_us, unsigned int time_out_num_tries,
	const char *func_name, int line)
{
	uint32_t field_value;
	uint32_t reg_val;
	int i;

	if (ctx->dmub_srv &&
	    ctx->dmub_srv->reg_helper_offload.gather_in_progress) {
		dmub_reg_wait_done_pack(ctx, addr, mask, shift, condition_value,
				delay_between_poll_us * time_out_num_tries);
		return;
	}

	/*
	 * Something is terribly wrong if time out is > 3000ms.
	 * 3000ms is the maximum time needed for SMU to pass values back.
	 * This value comes from experiments.
	 *
	 */
	ASSERT(delay_between_poll_us * time_out_num_tries <= 3000000);

	for (i = 0; i <= time_out_num_tries; i++) {
		if (i) {
			if (delay_between_poll_us >= 1000)
				msleep(delay_between_poll_us/1000);
			else if (delay_between_poll_us > 0)
				udelay(delay_between_poll_us);
		}

		reg_val = dm_read_reg(ctx, addr);

		field_value = get_reg_field_value_ex(reg_val, mask, shift);

		if (field_value == condition_value) {
			if (i * delay_between_poll_us > 1000 &&
					!IS_FPGA_MAXIMUS_DC(ctx->dce_environment))
				DC_LOG_DC("REG_WAIT taking a while: %dms in %s line:%d\n",
						delay_between_poll_us * i / 1000,
						func_name, line);
			return;
		}
	}

	DC_LOG_WARNING("REG_WAIT timeout %dus * %d tries - %s line:%d\n",
			delay_between_poll_us, time_out_num_tries,
			func_name, line);

	if (!IS_FPGA_MAXIMUS_DC(ctx->dce_environment))
		BREAK_TO_DEBUGGER();
}

void generic_write_indirect_reg(const struct dc_context *ctx,
		uint32_t addr_index, uint32_t addr_data,
		uint32_t index, uint32_t data)
{
	dm_write_reg(ctx, addr_index, index);
	dm_write_reg(ctx, addr_data, data);
}

uint32_t generic_read_indirect_reg(const struct dc_context *ctx,
		uint32_t addr_index, uint32_t addr_data,
		uint32_t index)
{
	uint32_t value = 0;

	// when reg read, there should not be any offload.
	if (ctx->dmub_srv &&
	    ctx->dmub_srv->reg_helper_offload.gather_in_progress) {
		ASSERT(false);
	}

	dm_write_reg(ctx, addr_index, index);
	value = dm_read_reg(ctx, addr_data);

	return value;
}

uint32_t generic_indirect_reg_get(const struct dc_context *ctx,
		uint32_t addr_index, uint32_t addr_data,
		uint32_t index, int n,
		uint8_t shift1, uint32_t mask1, uint32_t *field_value1,
		...)
{
	uint32_t shift, mask, *field_value;
	uint32_t value = 0;
	int i = 1;

	va_list ap;

	va_start(ap, field_value1);

	value = generic_read_indirect_reg(ctx, addr_index, addr_data, index);
	*field_value1 = get_reg_field_value_ex(value, mask1, shift1);

	while (i < n) {
		shift = va_arg(ap, uint32_t);
		mask = va_arg(ap, uint32_t);
		field_value = va_arg(ap, uint32_t *);

		*field_value = get_reg_field_value_ex(value, mask, shift);
		i++;
	}

	va_end(ap);

	return value;
}

uint32_t generic_indirect_reg_update_ex(const struct dc_context *ctx,
		uint32_t addr_index, uint32_t addr_data,
		uint32_t index, uint32_t reg_val, int n,
		uint8_t shift1, uint32_t mask1, uint32_t field_value1,
		...)
{
	uint32_t shift, mask, field_value;
	int i = 1;

	va_list ap;

	va_start(ap, field_value1);

	reg_val = set_reg_field_value_ex(reg_val, field_value1, mask1, shift1);

	while (i < n) {
		shift = va_arg(ap, uint32_t);
		mask = va_arg(ap, uint32_t);
		field_value = va_arg(ap, uint32_t);

		reg_val = set_reg_field_value_ex(reg_val, field_value, mask, shift);
		i++;
	}

	generic_write_indirect_reg(ctx, addr_index, addr_data, index, reg_val);
	va_end(ap);

	return reg_val;
}

void reg_sequence_start_gather(const struct dc_context *ctx)
{
	/* if reg sequence is supported and enabled, set flag to
	 * indicate we want to have REG_SET, REG_UPDATE macro build
	 * reg sequence command buffer rather than MMIO directly.
	 */

	if (ctx->dmub_srv && ctx->dc->debug.dmub_offload_enabled) {
		struct dc_reg_helper_state *offload =
			&ctx->dmub_srv->reg_helper_offload;

		/* caller sequence mismatch.  need to debug caller.  offload will not work!!! */
		ASSERT(!offload->gather_in_progress);

		offload->gather_in_progress = true;
	}
}

void reg_sequence_start_execute(const struct dc_context *ctx)
{
	struct dc_reg_helper_state *offload;

	if (!ctx->dmub_srv)
		return;

	offload = &ctx->dmub_srv->reg_helper_offload;

	if (offload && offload->gather_in_progress) {
		offload->gather_in_progress = false;
		offload->should_burst_write = false;
		switch (offload->cmd_data.cmd_common.header.type) {
		case DMUB_CMD__REG_SEQ_READ_MODIFY_WRITE:
			submit_dmub_read_modify_write(offload, ctx);
			break;
		case DMUB_CMD__REG_REG_WAIT:
			submit_dmub_reg_wait(offload, ctx);
			break;
		case DMUB_CMD__REG_SEQ_BURST_WRITE:
			submit_dmub_burst_write(offload, ctx);
			break;
		default:
			return;
		}

		dc_dmub_srv_cmd_execute(ctx->dmub_srv);
	}
}

void reg_sequence_wait_done(const struct dc_context *ctx)
{
	/* callback to DM to poll for last submission done*/
	struct dc_reg_helper_state *offload;

	if (!ctx->dmub_srv)
		return;

	offload = &ctx->dmub_srv->reg_helper_offload;

	if (offload &&
	    ctx->dc->debug.dmub_offload_enabled &&
	    !ctx->dc->debug.dmcub_emulation) {
		dc_dmub_srv_wait_idle(ctx->dmub_srv);
	}
}

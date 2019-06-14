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

	/* something is terribly wrong if time out is > 200ms. (5Hz) */
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

	dm_write_reg(ctx, addr_index, index);
	value = dm_read_reg(ctx, addr_data);

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

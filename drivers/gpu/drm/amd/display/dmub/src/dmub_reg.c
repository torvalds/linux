/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#include "dmub_reg.h"
#include "../inc/dmub_srv.h"

struct dmub_reg_value_masks {
	uint32_t value;
	uint32_t mask;
};

static inline void
set_reg_field_value_masks(struct dmub_reg_value_masks *field_value_mask,
			  uint32_t value, uint32_t mask, uint8_t shift)
{
	field_value_mask->value =
		(field_value_mask->value & ~mask) | (mask & (value << shift));
	field_value_mask->mask = field_value_mask->mask | mask;
}

static void set_reg_field_values(struct dmub_reg_value_masks *field_value_mask,
				 uint32_t addr, int n, uint8_t shift1,
				 uint32_t mask1, uint32_t field_value1,
				 va_list ap)
{
	uint32_t shift, mask, field_value;
	int i = 1;

	/* gather all bits value/mask getting updated in this register */
	set_reg_field_value_masks(field_value_mask, field_value1, mask1,
				  shift1);

	while (i < n) {
		shift = va_arg(ap, uint32_t);
		mask = va_arg(ap, uint32_t);
		field_value = va_arg(ap, uint32_t);

		set_reg_field_value_masks(field_value_mask, field_value, mask,
					  shift);
		i++;
	}
}

static inline uint32_t get_reg_field_value_ex(uint32_t reg_value, uint32_t mask,
					      uint8_t shift)
{
	return (mask & reg_value) >> shift;
}

void dmub_reg_update(struct dmub_srv *srv, uint32_t addr, int n, uint8_t shift1,
		     uint32_t mask1, uint32_t field_value1, ...)
{
	struct dmub_reg_value_masks field_value_mask = { 0 };
	uint32_t reg_val;
	va_list ap;

	va_start(ap, field_value1);
	set_reg_field_values(&field_value_mask, addr, n, shift1, mask1,
			     field_value1, ap);
	va_end(ap);

	reg_val = srv->funcs.reg_read(srv->user_ctx, addr);
	reg_val = (reg_val & ~field_value_mask.mask) | field_value_mask.value;
	srv->funcs.reg_write(srv->user_ctx, addr, reg_val);
}

void dmub_reg_set(struct dmub_srv *srv, uint32_t addr, uint32_t reg_val, int n,
		  uint8_t shift1, uint32_t mask1, uint32_t field_value1, ...)
{
	struct dmub_reg_value_masks field_value_mask = { 0 };
	va_list ap;

	va_start(ap, field_value1);
	set_reg_field_values(&field_value_mask, addr, n, shift1, mask1,
			     field_value1, ap);
	va_end(ap);

	reg_val = (reg_val & ~field_value_mask.mask) | field_value_mask.value;
	srv->funcs.reg_write(srv->user_ctx, addr, reg_val);
}

void dmub_reg_get(struct dmub_srv *srv, uint32_t addr, uint8_t shift,
		  uint32_t mask, uint32_t *field_value)
{
	uint32_t reg_val = srv->funcs.reg_read(srv->user_ctx, addr);
	*field_value = get_reg_field_value_ex(reg_val, mask, shift);
}

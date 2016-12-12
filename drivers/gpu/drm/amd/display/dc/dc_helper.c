/*
 * dc_helper.c
 *
 *  Created on: Aug 30, 2016
 *      Author: agrodzov
 */
#include "dm_services.h"
#include <stdarg.h>

uint32_t generic_reg_update_ex(const struct dc_context *ctx,
		uint32_t addr, uint32_t reg_val, int n,
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

	dm_write_reg(ctx, addr, reg_val);
	va_end(ap);

	return reg_val;
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

uint32_t generic_reg_wait(const struct dc_context *ctx,
	uint32_t addr, uint32_t shift, uint32_t mask, uint32_t condition_value,
	unsigned int delay_between_poll_us, unsigned int time_out_num_tries,
	const char *func_name)
{
	uint32_t field_value;
	uint32_t reg_val;
	int i;

	for (i = 0; i <= time_out_num_tries; i++) {
		if (i) {
			if (0 < delay_between_poll_us && delay_between_poll_us < 1000)
				udelay(delay_between_poll_us);

			if (delay_between_poll_us > 1000)
				msleep(delay_between_poll_us/1000);
		}

		reg_val = dm_read_reg(ctx, addr);

		field_value = get_reg_field_value_ex(reg_val, mask, shift);

		if (field_value == condition_value)
			return reg_val;
	}

	DC_ERR("REG_WAIT timeout %dus * %d tries - %s\n",
			delay_between_poll_us, time_out_num_tries, func_name);
	return reg_val;
}

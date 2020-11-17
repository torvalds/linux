/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

/*@************************************************************
 * include files
 ************************************************************/

#include "mp_precomp.h"
#include "phydm_precomp.h"

const u32 db_invert_table[12][8] = {
	{10, 13, 16, 20, 25, 32, 40, 50}, /* @U(32,3) */
	{64, 80, 101, 128, 160, 201, 256, 318}, /* @U(32,3) */
	{401, 505, 635, 800, 1007, 1268, 1596, 2010}, /* @U(32,3) */
	{316, 398, 501, 631, 794, 1000, 1259, 1585}, /* @U(32,0) */
	{1995, 2512, 3162, 3981, 5012, 6310, 7943, 10000}, /* @U(32,0) */
	{12589, 15849, 19953, 25119, 31623, 39811, 50119, 63098}, /* @U(32,0) */
	{79433, 100000, 125893, 158489, 199526, 251189, 316228,
	 398107}, /* @U(32,0) */
	{501187, 630957, 794328, 1000000, 1258925, 1584893, 1995262,
	 2511886}, /* @U(32,0) */
	{3162278, 3981072, 5011872, 6309573, 7943282, 1000000, 12589254,
	 15848932}, /* @U(32,0) */
	{19952623, 25118864, 31622777, 39810717, 50118723, 63095734,
	 79432823, 100000000}, /* @U(32,0) */
	{125892541, 158489319, 199526232, 251188643, 316227766, 398107171,
	 501187234, 630957345}, /* @U(32,0) */
	{794328235, 1000000000, 1258925412, 1584893192, 1995262315,
	 2511886432U, 3162277660U, 3981071706U} }; /* @U(32,0) */

/*Y = 10*log(X)*/
s32 odm_pwdb_conversion(s32 X, u32 total_bit, u32 decimal_bit)
{
	s32 Y, integer = 0, decimal = 0;
	u32 i;

	if (X == 0)
		X = 1; /* @log2(x), x can't be 0 */

	for (i = (total_bit - 1); i > 0; i--) {
		if (X & BIT(i)) {
			integer = i;
			if (i > 0) {
				/*decimal is 0.5dB*3=1.5dB~=2dB */
				decimal = (X & BIT(i - 1)) ? 2 : 0;
			}
			break;
		}
	}

	Y = 3 * (integer - decimal_bit) + decimal; /* @10*log(x)=3*log2(x), */

	return Y;
}

s32 odm_sign_conversion(s32 value, u32 total_bit)
{
	if (value & BIT(total_bit - 1))
		value -= BIT(total_bit);

	return value;
}

/*threshold must form low to high*/
u16 phydm_find_intrvl(void *dm_void, u16 val, u16 *threshold, u16 th_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u16 i = 0;
	u16 ret_val = 0;
	u16 max_th = threshold[th_len - 1];

	for (i = 0; i < th_len; i++) {
		if (val < threshold[i]) {
			ret_val = i;
			break;
		} else if (val >= max_th) {
			ret_val = th_len;
			break;
		}
	}

	return ret_val;
}

void phydm_seq_sorting(void *dm_void, u32 *value, u32 *rank_idx, u32 *idx_out,
		       u8 seq_length)
{
	u8 i = 0, j = 0;
	u32 tmp_a, tmp_b;
	u32 tmp_idx_a, tmp_idx_b;

	for (i = 0; i < seq_length; i++)
		rank_idx[i] = i;

	for (i = 0; i < (seq_length - 1); i++) {
		for (j = 0; j < (seq_length - 1 - i); j++) {
			tmp_a = value[j];
			tmp_b = value[j + 1];

			tmp_idx_a = rank_idx[j];
			tmp_idx_b = rank_idx[j + 1];

			if (tmp_a < tmp_b) {
				value[j] = tmp_b;
				value[j + 1] = tmp_a;

				rank_idx[j] = tmp_idx_b;
				rank_idx[j + 1] = tmp_idx_a;
			}
		}
	}

	for (i = 0; i < seq_length; i++)
		idx_out[rank_idx[i]] = i + 1;
}

u32 odm_convert_to_db(u64 value)
{
	u8 i;
	u8 j;
	u32 dB;

	if (value >= db_invert_table[11][7]) {
		pr_debug("[%s] ====>\n", __func__);
		return 96; /* @maximum 96 dB */
	}

	for (i = 0; i < 12; i++) {
		if (i <= 2 && (value << FRAC_BITS) <= db_invert_table[i][7])
			break;
		else if (i > 2 && value <= db_invert_table[i][7])
			break;
	}

	for (j = 0; j < 8; j++) {
		if (i <= 2 && (value << FRAC_BITS) <= db_invert_table[i][j])
			break;
		else if (i > 2 && i < 12 && value <= db_invert_table[i][j])
			break;
	}

	if (j == 0 && i == 0)
		goto end;

	if (j == 0) {
		if (i != 3) {
			if (db_invert_table[i][0] - value >
			    value - db_invert_table[i - 1][7]) {
				i = i - 1;
				j = 7;
			}
		} else {
			if (db_invert_table[3][0] - value >
			    value - db_invert_table[2][7]) {
				i = 2;
				j = 7;
			}
		}
	} else {
		if (db_invert_table[i][j] - value >
		    value - db_invert_table[i][j - 1]) {
			i = i;
			j = j - 1;
		}
	}
end:
	dB = (i << 3) + j + 1;

	return dB;
}

u64 phydm_db_2_linear(u32 value)
{
	u8 i;
	u8 j;
	u64 linear;

	/* @1dB~96dB */
	if (value > 96)
		value = 96;
	value = value & 0xFF;

	i = (u8)((value - 1) >> 3);
	j = (u8)(value - 1) - (i << 3);

	linear = db_invert_table[i][j];

	if (i > 2)
		linear = linear << FRAC_BITS;

	return linear;
}

u16 phydm_show_fraction_num(u32 frac_val, u8 bit_num)
{
	u8 i = 0;
	u16 val = 0;
	u16 base = 5000;

	for (i = bit_num; i > 0; i--) {
		if (frac_val & BIT(i - 1))
			val += (base >> (bit_num - i));
	}
	return val;
}

u64 phydm_gen_bitmask(u8 mask_num)
{
	u8 i = 0;
	u64 bitmask = 0;

	if (mask_num > 64)
		return 1;

	for (i = 0; i < mask_num; i++)
		bitmask = (bitmask << 1) | BIT(0);

	return bitmask;
}

s32 phydm_cnvrt_2_sign(u32 val, u8 bit_num)
{
	if (val & BIT(bit_num - 1)) /*Sign BIT*/
		val -= (1 << bit_num); /*@2's*/

	return val;
}


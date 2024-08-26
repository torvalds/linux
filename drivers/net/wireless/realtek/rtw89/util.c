// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2024  Realtek Corporation
 */

#include "util.h"

#define FRAC_ROWS 3
#define FRAC_ROW_MAX (FRAC_ROWS - 1)
#define NORM_ROW_MIN FRAC_ROWS

static const u32 db_invert_table[12][8] = {
	/* rows 0~2 in unit of U(32,3) */
	{10, 13, 16, 20, 25, 32, 40, 50},
	{64, 80, 101, 128, 160, 201, 256, 318},
	{401, 505, 635, 800, 1007, 1268, 1596, 2010},
	/* rows 3~11 in unit of U(32,0) */
	{316, 398, 501, 631, 794, 1000, 1259, 1585},
	{1995, 2512, 3162, 3981, 5012, 6310, 7943, 10000},
	{12589, 15849, 19953, 25119, 31623, 39811, 50119, 63098},
	{79433, 100000, 125893, 158489, 199526, 251189, 316228, 398107},
	{501187, 630957, 794328, 1000000, 1258925, 1584893, 1995262, 2511886},
	{3162278, 3981072, 5011872, 6309573, 7943282, 1000000, 12589254,
	 15848932},
	{19952623, 25118864, 31622777, 39810717, 50118723, 63095734, 79432823,
	 100000000},
	{125892541, 158489319, 199526232, 251188643, 316227766, 398107171,
	 501187234, 630957345},
	{794328235, 1000000000, 1258925412, 1584893192, 1995262315, 2511886432U,
	 3162277660U, 3981071706U},
};

u32 rtw89_linear_2_db(u64 val)
{
	u8 i, j;
	u32 dB;

	for (i = 0; i < 12; i++) {
		for (j = 0; j < 8; j++) {
			if (i <= FRAC_ROW_MAX &&
			    (val << RTW89_LINEAR_FRAC_BITS) <= db_invert_table[i][j])
				goto cnt;
			else if (i > FRAC_ROW_MAX && val <= db_invert_table[i][j])
				goto cnt;
		}
	}

	return 96; /* maximum 96 dB */

cnt:
	/* special cases */
	if (j == 0 && i == 0)
		goto end;

	if (i == NORM_ROW_MIN && j == 0) {
		if (db_invert_table[NORM_ROW_MIN][0] - val >
		    val - (db_invert_table[FRAC_ROW_MAX][7] >> RTW89_LINEAR_FRAC_BITS)) {
			i = FRAC_ROW_MAX;
			j = 7;
		}
		goto end;
	}

	if (i <= FRAC_ROW_MAX)
		val <<= RTW89_LINEAR_FRAC_BITS;

	/* compare difference to get precise dB */
	if (j == 0) {
		if (db_invert_table[i][j] - val >
		    val - db_invert_table[i - 1][7]) {
			i--;
			j = 7;
		}
	} else {
		if (db_invert_table[i][j] - val >
		    val - db_invert_table[i][j - 1]) {
			j--;
		}
	}
end:
	dB = (i << 3) + j + 1;

	return dB;
}
EXPORT_SYMBOL(rtw89_linear_2_db);

u64 rtw89_db_2_linear(u32 db)
{
	u64 linear;
	u8 i, j;

	if (db > 96)
		db = 96;
	else if (db < 1)
		return 1;

	i = (db - 1) >> 3;
	j = (db - 1) & 0x7;

	linear = db_invert_table[i][j];

	if (i >= NORM_ROW_MIN)
		linear = linear << RTW89_LINEAR_FRAC_BITS;

	return linear;
}
EXPORT_SYMBOL(rtw89_db_2_linear);

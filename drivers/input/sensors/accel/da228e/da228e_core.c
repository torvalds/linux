// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Kay Guo <kay.guo@rock-chips.com>
 */
#include <linux/kernel.h>
#include "da228e_core.h"

#define DA228E_OFFSET_THRESHOLD	20
#define PEAK_LVL		800
#define STICK_LSB		2000
#define AIX_HISTORY_SIZE	20

#define TEMP_CALIBRATE_STATIC_THRESHOLD	60
#define TEMP_CALIBRATE_STATIC_COUNT	12

static int z_offset;

static int squareRoot(int val)
{
	int ret = 0, x;
	int shift;

	if (val < 0)
		return 0;

	for (shift = 0; shift < 32; shift += 2) {
		x = 0x40000000l >> shift;
		if ((x+ret) <= val) {
			val -= x + ret;
			ret = (ret >> 1) | x;
		} else
			ret = ret >> 1;
	}

	return ret;
}

static int da228e_temp_calibrate_detect_static(short x, short y, short z)
{
	static int count_static;
	static short temp_x[TEMP_CALIBRATE_STATIC_COUNT];
	static short temp_y[TEMP_CALIBRATE_STATIC_COUNT];
	static short temp_z[TEMP_CALIBRATE_STATIC_COUNT];
	static short max_x, max_y, max_z;
	static short min_x, min_y, min_z;
	static char is_first = 1;
	int i, delta_sum = 0;

	count_static++;

	if (is_first) {
		temp_x[0] = x;
		temp_y[0] = y;
		temp_z[0] = z;
		for (i = 1; i < TEMP_CALIBRATE_STATIC_COUNT; i++) {
			temp_x[i] = temp_x[0];
			temp_y[i] = temp_y[0];
			temp_z[i] = temp_z[0];
		}
		is_first = 0;
	} else {
		max_x = min_x = temp_x[1];
		max_y = min_y = temp_y[1];
		max_z = min_z = temp_z[1];

		for (i = 0; i < TEMP_CALIBRATE_STATIC_COUNT; i++) {
			if (i == TEMP_CALIBRATE_STATIC_COUNT - 1) {
				temp_x[i] = x;
				temp_y[i] = y;
				temp_z[i] = z;
			} else {
				temp_x[i] = temp_x[i+1];
				temp_y[i] = temp_y[i+1];
				temp_z[i] = temp_z[i+1];
			}
			max_x = (max_x > temp_x[i]) ? max_x:temp_x[i];
			max_y = (max_y > temp_y[i]) ? max_y:temp_y[i];
			max_z = (max_z > temp_z[i]) ? max_z:temp_z[i];

			min_x = (min_x < temp_x[i]) ? min_x:temp_x[i];
			min_y = (min_y < temp_y[i]) ? min_y:temp_y[i];
			min_z = (min_z < temp_z[i]) ? min_z:temp_z[i];
		}
	}

	if (count_static > TEMP_CALIBRATE_STATIC_COUNT) {
		count_static = TEMP_CALIBRATE_STATIC_COUNT;
		delta_sum = abs(max_x - min_x) + abs(max_y - min_y) +
			    abs(max_z - min_z);

		if (delta_sum < TEMP_CALIBRATE_STATIC_THRESHOLD)
			return 1;
	}

	return 0;
}

int da228e_temp_calibrate(int *x, int *y, int *z)
{
	int tem_z = 0;
	int cus = MIR3DA_OFFSET_MAX-MIR3DA_OFFSET_CUS;
	int is_static = 0;
	short lz_offset;

	*z = *z + z_offset;
	lz_offset  =  (*z) % 10;

	if ((abs(*x) < MIR3DA_OFFSET_MAX) &&
	    (abs(*y) < MIR3DA_OFFSET_MAX)) {
		is_static = da228e_temp_calibrate_detect_static(*x, *y, *z-z_offset);
		tem_z = squareRoot(MIR3DA_OFFSET_SEN*MIR3DA_OFFSET_SEN -
				  (*x)*(*x) - (*y)*(*y)) + lz_offset;
		if (z_offset == 0) {
			if (is_static == 1)
				z_offset = (*z >= 0) ? (tem_z-*z) : (-tem_z-*z);
			*z = ((*z >= 0) ? (1) : (-1))*tem_z;
		} else if (is_static) {
			if (abs(abs(*z) - MIR3DA_OFFSET_SEN) > MIR3DA_OFFSET_CUS) {
				*z = ((*z >= 0) ? (1) : (-1))*tem_z;
				z_offset = 0;
			}
		}
		*x = (*x)*MIR3DA_OFFSET_CUS/MIR3DA_OFFSET_MAX;
		*y = (*y)*MIR3DA_OFFSET_CUS/MIR3DA_OFFSET_MAX;

	} else if ((abs((abs(*x) - MIR3DA_OFFSET_SEN)) < MIR3DA_OFFSET_MAX) &&
		   (abs(*y) < MIR3DA_OFFSET_MAX) && (z_offset)) {
		if (abs(*x) > MIR3DA_OFFSET_SEN) {
			*x = (*x > 0) ?
			     (*x - (abs(*x) - MIR3DA_OFFSET_SEN)*cus/MIR3DA_OFFSET_MAX) :
			     (*x + (abs(*x) - MIR3DA_OFFSET_SEN)*cus/MIR3DA_OFFSET_MAX);
		} else {
			*x = (*x > 0) ?
			     (*x + (MIR3DA_OFFSET_SEN - abs(*x))*cus/MIR3DA_OFFSET_MAX) :
			     (*x - (MIR3DA_OFFSET_SEN-abs(*x))*cus/MIR3DA_OFFSET_MAX);
		}
		*y = (*y) * MIR3DA_OFFSET_CUS/MIR3DA_OFFSET_MAX;
	} else if ((abs((abs(*y) - MIR3DA_OFFSET_SEN)) < MIR3DA_OFFSET_MAX) &&
		   (abs(*x) < MIR3DA_OFFSET_MAX) && (z_offset)) {
		if (abs(*y) > MIR3DA_OFFSET_SEN) {
			*y = (*y > 0) ?
			     (*y - (abs(*y) - MIR3DA_OFFSET_SEN)*cus/MIR3DA_OFFSET_MAX) :
			     (*y + (abs(*y)-MIR3DA_OFFSET_SEN)*cus/MIR3DA_OFFSET_MAX);
		} else {
			*y = (*y > 0) ?
			     (*y + (MIR3DA_OFFSET_SEN - abs(*y))*cus/MIR3DA_OFFSET_MAX) :
			     (*y - (MIR3DA_OFFSET_SEN - abs(*y))*cus/MIR3DA_OFFSET_MAX);
		}
		*x = (*x) * MIR3DA_OFFSET_CUS/MIR3DA_OFFSET_MAX;
	} else if (z_offset == 0) {
		if ((abs(*x) < MIR3DA_OFFSET_MAX) && (abs((*y > 0) ?
		    (MIR3DA_OFFSET_SEN - *y) : (MIR3DA_OFFSET_SEN + *y)) < MIR3DA_OFFSET_MAX)) {
			*z = ((*z >= 0) ?
			     (1) : (-1))*abs(*x)*MIR3DA_OFFSET_CUS/MIR3DA_OFFSET_MAX;
		} else if ((abs(*y) < MIR3DA_OFFSET_MAX) &&
			   (abs((*x > 0) ? (MIR3DA_OFFSET_SEN - *x) : (MIR3DA_OFFSET_SEN + *x))
			    < MIR3DA_OFFSET_MAX)) {
			*z = ((*z >= 0) ? (1) : (-1)) *
			     abs(*y) * MIR3DA_OFFSET_CUS/MIR3DA_OFFSET_MAX;
		} else {
			tem_z = squareRoot(MIR3DA_OFFSET_SEN*MIR3DA_OFFSET_SEN -
					  (*x)*(*x) - (*y)*(*y)) + lz_offset;
			*z = ((*z >= 0) ? (1) : (-1))*tem_z;
		}
	}

	if (z_offset)
		return 0;
	else
		return -1;
}



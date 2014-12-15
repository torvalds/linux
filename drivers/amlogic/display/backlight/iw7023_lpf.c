/*
* iW7023 Driver for LCD Panel Backlight
*
* Copyright (C) 2012 AMLOGIC, INC.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
*/

#include <linux/types.h>
#include <linux/spinlock.h>


static DEFINE_SPINLOCK(bri_lock);

/* step responding time (the period of time fully adapting to a new step)
 * ranges 0~256
 *   0: freeze
 *   1: 4 seconds
 *   2: 2 seconds
 *   4: 1 second
 *   8: 1/2 seconds
 *  16: 1/4 seconds
 *  32: 1/8 seconds
 *  64: 1/16 seconds
 * 128: 1/32 seconds
 * 256: immediately
 */
static unsigned int lpf_speed;



/* 12-bit value
 * limit to iWatt7023
 */
static unsigned int limit_max, limit_min;

/* software dimming
 * ranges 0~256
 * 0: darkest
 * 256: brightest (default)
 * affect all local dimming segments
 * decided by software functions, for example, enviromental brightness, and so on
 */
static unsigned int sw_dimming;


/*
 * 12-bit value
 * default is straight curve
 * finetune with gamma curve for better performance
 */
static unsigned int sw_gamma[256] = {
	0,    16,   32,   48,   64,   80,   96,   112,
	128,  144,  160,  176,  192,  208,  224,  240,
	256,  272,  288,  304,  320,  336,  352,  368,
	384,  400,  416,  432,  448,  464,  480,  496,
	512,  528,  544,  560,  576,  592,  608,  624,
	640,  656,  672,  688,  704,  720,  736,  752,
	768,  784,  800,  816,  832,  848,  864,  880,
	896,  912,  928,  944,  960,  976,  992,  1008,
	1024, 1040, 1056, 1072, 1088, 1104, 1120, 1136,
	1152, 1168, 1184, 1200, 1216, 1232, 1248, 1264,
	1280, 1296, 1312, 1328, 1344, 1360, 1376, 1392,
	1408, 1424, 1440, 1456, 1472, 1488, 1504, 1520,
	1536, 1552, 1568, 1584, 1600, 1616, 1632, 1648,
	1664, 1680, 1696, 1712, 1728, 1744, 1760, 1776,
	1792, 1808, 1824, 1840, 1856, 1872, 1888, 1904,
	1920, 1936, 1952, 1968, 1984, 2000, 2016, 2032,
	2048, 2064, 2080, 2096, 2112, 2128, 2144, 2160,
	2176, 2192, 2208, 2224, 2240, 2256, 2272, 2288,
	2304, 2320, 2336, 2352, 2368, 2384, 2400, 2416,
	2432, 2448, 2464, 2480, 2496, 2512, 2528, 2544,
	2560, 2576, 2592, 2608, 2624, 2640, 2656, 2672,
	2688, 2704, 2720, 2736, 2752, 2768, 2784, 2800,
	2816, 2832, 2848, 2864, 2880, 2896, 2912, 2928,
	2944, 2960, 2976, 2992, 3008, 3024, 3040, 3056,
	3072, 3088, 3104, 3120, 3136, 3152, 3168, 3184,
	3200, 3216, 3232, 3248, 3264, 3280, 3296, 3312,
	3328, 3344, 3360, 3376, 3392, 3408, 3424, 3440,
	3456, 3472, 3488, 3504, 3520, 3536, 3552, 3568,
	3584, 3600, 3616, 3632, 3648, 3663, 3679, 3695,
	3711, 3727, 3743, 3759, 3775, 3791, 3808, 3824,
	3840, 3856, 3873, 3889, 3906, 3922, 3939, 3956,
	3973, 3990, 4007, 4025, 4042, 4060, 4077, 4095,
};

/* lookup table
 * 12-bit value
 * mapping from a 8-bit value of histgram
 * map_lookup[  0] mapping from 16
 * map_lookup[219] mapping from 235
 */
static int map_lookup[220];

/* average luma of local dimming sements fetching from driver
 * 8-bit value but limited to 16~235 afterwards
 */
static unsigned int luma_hist[16];

/* 12-bit value
 * target value by mapping histgram in lookup table
 */
static unsigned int bri_target[16];

/* 20-bit value, 12-bit integer and 8-bit fraction
 * current value after lpf (low pass filter)
 */
static unsigned int bri_current[16];

/* 12-bit value
 * final value to iWatt7023
 */
static unsigned int bri_final[16];



void set_lpf_speed(unsigned int speed)
{
	if (speed < 0)
		lpf_speed = 0;
	else if (speed > 256)
		lpf_speed = 256;
	else
		lpf_speed = speed;
}

void set_user_limit(unsigned int min, unsigned int max)
{
	limit_min = min;
	limit_max = max;
}

void set_user_dimrate(unsigned int dimming)
{
	unsigned int percentage;

	percentage = dimming;
	if (dimming < 0)
		percentage = 0;
	if (dimming > 100)
		percentage = 100;

	sw_dimming = (256 * percentage + 50) / 100;
}


void print_map_lookup(void)
{
	int i, j;

	printk("map lookup:\n");
	for (i = 0; i < 16; i++) {
		for (j = 0; j < 16; j++) {
			if (16 * i + j < 220)
				printk("%8d ", map_lookup[16 * i + j]);
		}
		printk("\n");
	}
}

void print_luma_hist(void)
{
	int i, j;

	printk("luma histgram:\n");
	for (i = 0; i < 2; i++) {
		for (j = 0; j < 8; j++) {
			printk("%8d ", luma_hist[8 * i + j]);
		}
		printk("\n");
	}
}

void print_bri_target(void)
{
	int i, j;

	printk("target:\n");
	for (i = 0; i < 2; i++) {
		for (j = 0; j < 8; j++) {
			printk("%8d ", bri_target[8 * i + j]);
		}
		printk("\n");
	}
}

void print_bri_current(void)
{
	int i, j;

	printk("current:\n");
	for (i = 0; i < 2; i++) {
		for (j = 0; j < 8; j++) {
			printk("%8d ", bri_current[8 * i + j]);
		}
		printk("\n");
	}
}

void print_bri_final(void)
{
	int i, j;

	printk("final:\n");
	for (i = 0; i < 2; i++) {
		for (j = 0; j < 8; j++) {
			printk("%8d ", bri_final[8 * i + j]);
		}
		printk("\n");
	}
}

/* stretch 0~219 (16~235) to 0~255
 * finetune with gamma curve for better performance
 */
void calculate_map_lookup(void)
{
	int i = 0, idx = 0;

	for (i = 0; i < 220; i++)
	{
		idx = (i * 255 + 110) / 219;
		map_lookup[i] = sw_gamma[idx];
	}
}


unsigned int limitation(unsigned int data, unsigned int max, unsigned int min)
{
	if (data < min)
		return min;
	if (data > max)
		return max;
	return data;
}


void calculate_target(void)
{
	int i = 0;
	unsigned int hist;

	spin_lock(&bri_lock);
	for (i = 0; i < 16; i++) {
		hist = limitation(luma_hist[i], 235, 16);
		bri_target[i] = map_lookup[hist - 16];
	}
	spin_unlock(&bri_lock);
}


void calculate_current(void)
{
	int i = 0;
	unsigned int x, y, z;

	for (i = 0; i < 16; i++) {
		x = bri_current[i];
		y = lpf_speed * bri_target[i];
		z = (lpf_speed * x + 128) >> 8;
		bri_current[i] = x + y - z;
	}
}

void calculate_final(void)
{
	int i = 0;
	unsigned int data = 0;

	for (i = 0; i < 16; i++) {
		/* take its integer only */
		data = (bri_current[i] + 128) >> 8;

		/* limit  handling */
		if (data > limit_max)
			data = limit_max;
		if (data < limit_min)
			data = limit_min;

		/* sw_dimming handling */
		data = (data * sw_dimming + 128) >> 8;
		bri_final[i] = data;
	}
}

void set_luma_hist(unsigned short luma[16])
{
	int i;
	spin_lock(&bri_lock);
	for (i = 0; i < 16; i++) {
		luma_hist[i] = luma[i];
	}
	spin_unlock(&bri_lock);
}

unsigned short get_luma_hist(int win)
{
	unsigned short luma = 0;

	spin_lock(&bri_lock);
	if (win >=0 && win < 16)
		luma = luma_hist[win];
	spin_unlock(&bri_lock);

	return luma;
}

/* @winindex of windows
 *
 */
unsigned short get_bri_final(int win)
{
	unsigned short val = 0;
	if (win >=0 && win < 16)
		val = bri_final[win];
	return val;
}


void lpf_init(void)
{
	int i;

	spin_lock_init(&bri_lock);

	calculate_map_lookup();

	for (i = 0; i < 16; i++) {
		bri_current[i] = 0xfff00;
	}

	calculate_final();
}

void lpf_work(void)
{
	calculate_target();
	calculate_current();
	calculate_final();
}



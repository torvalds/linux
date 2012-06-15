/*
 * SDRC register values for Nokia boards
 *
 * Copyright (C) 2008, 2010-2011 Nokia Corporation
 *
 * Lauri Leukkunen <lauri.leukkunen@nokia.com>
 *
 * Original code by Juha Yrjola <juha.yrjola@solidboot.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>

#include "common.h"
#include <plat/clock.h>
#include <plat/sdrc.h>

#include "sdram-nokia.h"

/* In picoseconds, except for tREF (ns), tXP, tCKE, tWTR (clks) */
struct sdram_timings {
	u32 casl;
	u32 tDAL;
	u32 tDPL;
	u32 tRRD;
	u32 tRCD;
	u32 tRP;
	u32 tRAS;
	u32 tRC;
	u32 tRFC;
	u32 tXSR;

	u32 tREF; /* in ns */

	u32 tXP;
	u32 tCKE;
	u32 tWTR;
};

static const struct sdram_timings nokia_97dot6mhz_timings[] = {
	{
		.casl = 3,
		.tDAL = 30725,
		.tDPL = 15362,
		.tRRD = 10241,
		.tRCD = 20483,
		.tRP = 15362,
		.tRAS = 40967,
		.tRC = 56330,
		.tRFC = 138266,
		.tXSR = 204839,

		.tREF = 7798,

		.tXP = 2,
		.tCKE = 4,
		.tWTR = 2,
	},
};

static const struct sdram_timings nokia_166mhz_timings[] = {
	{
		.casl = 3,
		.tDAL = 33000,
		.tDPL = 15000,
		.tRRD = 12000,
		.tRCD = 22500,
		.tRP = 18000,
		.tRAS = 42000,
		.tRC = 66000,
		.tRFC = 138000,
		.tXSR = 200000,

		.tREF = 7800,

		.tXP = 2,
		.tCKE = 2,
		.tWTR = 2
	},
};

static const struct sdram_timings nokia_195dot2mhz_timings[] = {
	{
		.casl = 3,
		.tDAL = 30725,
		.tDPL = 15362,
		.tRRD = 10241,
		.tRCD = 20483,
		.tRP = 15362,
		.tRAS = 40967,
		.tRC = 56330,
		.tRFC = 138266,
		.tXSR = 204839,

		.tREF = 7752,

		.tXP = 2,
		.tCKE = 4,
		.tWTR = 2,
	},
};

static const struct sdram_timings nokia_200mhz_timings[] = {
	{
		.casl = 3,
		.tDAL = 30000,
		.tDPL = 15000,
		.tRRD = 10000,
		.tRCD = 20000,
		.tRP = 15000,
		.tRAS = 40000,
		.tRC = 55000,
		.tRFC = 140000,
		.tXSR = 200000,

		.tREF = 7800,

		.tXP = 2,
		.tCKE = 4,
		.tWTR = 2
	},
};

static const struct {
	long rate;
	struct sdram_timings const *data;
} nokia_timings[] = {
	{ 83000000, nokia_166mhz_timings },
	{ 97600000, nokia_97dot6mhz_timings },
	{ 100000000, nokia_200mhz_timings },
	{ 166000000, nokia_166mhz_timings },
	{ 195200000, nokia_195dot2mhz_timings },
	{ 200000000, nokia_200mhz_timings },
};
static struct omap_sdrc_params nokia_sdrc_params[ARRAY_SIZE(nokia_timings) + 1];

static unsigned long sdrc_get_fclk_period(long rate)
{
	/* In picoseconds */
	return 1000000000 / rate;
}

static unsigned int sdrc_ps_to_ticks(unsigned int time_ps, long rate)
{
	unsigned long tick_ps;

	/* Calculate in picosecs to yield more exact results */
	tick_ps = sdrc_get_fclk_period(rate);

	return (time_ps + tick_ps - 1) / tick_ps;
}
#undef DEBUG
#ifdef DEBUG
static int set_sdrc_timing_regval(u32 *regval, int st_bit, int end_bit,
				int ticks, long rate, const char *name)
#else
static int set_sdrc_timing_regval(u32 *regval, int st_bit, int end_bit,
			       int ticks)
#endif
{
	int mask, nr_bits;

	nr_bits = end_bit - st_bit + 1;
	if (ticks >= 1 << nr_bits)
		return -1;
	mask = (1 << nr_bits) - 1;
	*regval &= ~(mask << st_bit);
	*regval |= ticks << st_bit;
#ifdef DEBUG
	printk(KERN_INFO "SDRC %s: %i ticks %i ns\n", name, ticks,
			(unsigned int)sdrc_get_fclk_period(rate) * ticks /
			1000);
#endif

	return 0;
}

#ifdef DEBUG
#define SDRC_SET_ONE(reg, st, end, field, rate) \
	if (set_sdrc_timing_regval((reg), (st), (end), \
			memory_timings->field, (rate), #field) < 0) \
		err = -1;
#else
#define SDRC_SET_ONE(reg, st, end, field, rate) \
	if (set_sdrc_timing_regval((reg), (st), (end), \
			memory_timings->field) < 0) \
		err = -1;
#endif

#ifdef DEBUG
static int set_sdrc_timing_regval_ps(u32 *regval, int st_bit, int end_bit,
				int time, long rate, const char *name)
#else
static int set_sdrc_timing_regval_ps(u32 *regval, int st_bit, int end_bit,
				int time, long rate)
#endif
{
	int ticks, ret;
	ret = 0;

	if (time == 0)
		ticks = 0;
	else
		ticks = sdrc_ps_to_ticks(time, rate);

#ifdef DEBUG
	ret = set_sdrc_timing_regval(regval, st_bit, end_bit, ticks,
				     rate, name);
#else
	ret = set_sdrc_timing_regval(regval, st_bit, end_bit, ticks);
#endif

	return ret;
}

#ifdef DEBUG
#define SDRC_SET_ONE_PS(reg, st, end, field, rate) \
	if (set_sdrc_timing_regval_ps((reg), (st), (end), \
			memory_timings->field, \
			(rate), #field) < 0) \
		err = -1;

#else
#define SDRC_SET_ONE_PS(reg, st, end, field, rate) \
	if (set_sdrc_timing_regval_ps((reg), (st), (end), \
			memory_timings->field, (rate)) < 0) \
		err = -1;
#endif

static int sdrc_timings(int id, long rate,
			const struct sdram_timings *memory_timings)
{
	u32 ticks_per_ms;
	u32 rfr, l;
	u32 actim_ctrla = 0, actim_ctrlb = 0;
	u32 rfr_ctrl;
	int err = 0;
	long l3_rate = rate / 1000;

	SDRC_SET_ONE_PS(&actim_ctrla,  0,  4, tDAL, l3_rate);
	SDRC_SET_ONE_PS(&actim_ctrla,  6,  8, tDPL, l3_rate);
	SDRC_SET_ONE_PS(&actim_ctrla,  9, 11, tRRD, l3_rate);
	SDRC_SET_ONE_PS(&actim_ctrla, 12, 14, tRCD, l3_rate);
	SDRC_SET_ONE_PS(&actim_ctrla, 15, 17, tRP, l3_rate);
	SDRC_SET_ONE_PS(&actim_ctrla, 18, 21, tRAS, l3_rate);
	SDRC_SET_ONE_PS(&actim_ctrla, 22, 26, tRC, l3_rate);
	SDRC_SET_ONE_PS(&actim_ctrla, 27, 31, tRFC, l3_rate);

	SDRC_SET_ONE_PS(&actim_ctrlb,  0,  7, tXSR, l3_rate);

	SDRC_SET_ONE(&actim_ctrlb,  8, 10, tXP, l3_rate);
	SDRC_SET_ONE(&actim_ctrlb, 12, 14, tCKE, l3_rate);
	SDRC_SET_ONE(&actim_ctrlb, 16, 17, tWTR, l3_rate);

	ticks_per_ms = l3_rate;
	rfr = memory_timings[0].tREF * ticks_per_ms / 1000000;
	if (rfr > 65535 + 50)
		rfr = 65535;
	else
		rfr -= 50;

#ifdef DEBUG
	printk(KERN_INFO "SDRC tREF: %i ticks\n", rfr);
#endif

	l = rfr << 8;
	rfr_ctrl = l | 0x1; /* autorefresh, reload counter with 1xARCV */

	nokia_sdrc_params[id].rate = rate;
	nokia_sdrc_params[id].actim_ctrla = actim_ctrla;
	nokia_sdrc_params[id].actim_ctrlb = actim_ctrlb;
	nokia_sdrc_params[id].rfr_ctrl = rfr_ctrl;
	nokia_sdrc_params[id].mr = 0x32;

	nokia_sdrc_params[id + 1].rate = 0;

	return err;
}

struct omap_sdrc_params *nokia_get_sdram_timings(void)
{
	int err = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(nokia_timings); i++) {
		err |= sdrc_timings(i, nokia_timings[i].rate,
				       nokia_timings[i].data);
		if (err)
			pr_err("%s: error with rate %ld: %d\n", __func__,
			       nokia_timings[i].rate, err);
	}

	return err ? NULL : nokia_sdrc_params;
}


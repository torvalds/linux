// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Cobalt CPLD functions
 *
 *  Copyright 2012-2015 Cisco Systems, Inc. and/or its affiliates.
 *  All rights reserved.
 */

#include <linux/delay.h>

#include "cobalt-cpld.h"

#define ADRS(offset) (COBALT_BUS_CPLD_BASE + offset)

static u16 cpld_read(struct cobalt *cobalt, u32 offset)
{
	return cobalt_bus_read32(cobalt->bar1, ADRS(offset));
}

static void cpld_write(struct cobalt *cobalt, u32 offset, u16 val)
{
	return cobalt_bus_write32(cobalt->bar1, ADRS(offset), val);
}

static void cpld_info_ver3(struct cobalt *cobalt)
{
	u32 rd;
	u32 tmp;

	cobalt_info("CPLD System control register (read/write)\n");
	cobalt_info("\t\tSystem control:  0x%04x (0x0f00)\n",
		    cpld_read(cobalt, 0));
	cobalt_info("CPLD Clock control register (read/write)\n");
	cobalt_info("\t\tClock control:   0x%04x (0x0000)\n",
		    cpld_read(cobalt, 0x04));
	cobalt_info("CPLD HSMA Clk Osc register (read/write) - Must set wr trigger to load default values\n");
	cobalt_info("\t\tRegister #7:\t0x%04x (0x0022)\n",
		    cpld_read(cobalt, 0x08));
	cobalt_info("\t\tRegister #8:\t0x%04x (0x0047)\n",
		    cpld_read(cobalt, 0x0c));
	cobalt_info("\t\tRegister #9:\t0x%04x (0x00fa)\n",
		    cpld_read(cobalt, 0x10));
	cobalt_info("\t\tRegister #10:\t0x%04x (0x0061)\n",
		    cpld_read(cobalt, 0x14));
	cobalt_info("\t\tRegister #11:\t0x%04x (0x001e)\n",
		    cpld_read(cobalt, 0x18));
	cobalt_info("\t\tRegister #12:\t0x%04x (0x0045)\n",
		    cpld_read(cobalt, 0x1c));
	cobalt_info("\t\tRegister #135:\t0x%04x\n",
		    cpld_read(cobalt, 0x20));
	cobalt_info("\t\tRegister #137:\t0x%04x\n",
		    cpld_read(cobalt, 0x24));
	cobalt_info("CPLD System status register (read only)\n");
	cobalt_info("\t\tSystem status:  0x%04x\n",
		    cpld_read(cobalt, 0x28));
	cobalt_info("CPLD MAXII info register (read only)\n");
	cobalt_info("\t\tBoard serial number:     0x%04x\n",
		    cpld_read(cobalt, 0x2c));
	cobalt_info("\t\tMAXII program revision:  0x%04x\n",
		    cpld_read(cobalt, 0x30));
	cobalt_info("CPLD temp and voltage ADT7411 registers (read only)\n");
	cobalt_info("\t\tBoard temperature:  %u Celsius\n",
		    cpld_read(cobalt, 0x34) / 4);
	cobalt_info("\t\tFPGA temperature:   %u Celsius\n",
		    cpld_read(cobalt, 0x38) / 4);
	rd = cpld_read(cobalt, 0x3c);
	tmp = (rd * 33 * 1000) / (483 * 10);
	cobalt_info("\t\tVDD 3V3:      %u,%03uV\n", tmp / 1000, tmp % 1000);
	rd = cpld_read(cobalt, 0x40);
	tmp = (rd * 74 * 2197) / (27 * 1000);
	cobalt_info("\t\tADC ch3 5V:   %u,%03uV\n", tmp / 1000, tmp % 1000);
	rd = cpld_read(cobalt, 0x44);
	tmp = (rd * 74 * 2197) / (47 * 1000);
	cobalt_info("\t\tADC ch4 3V:   %u,%03uV\n", tmp / 1000, tmp % 1000);
	rd = cpld_read(cobalt, 0x48);
	tmp = (rd * 57 * 2197) / (47 * 1000);
	cobalt_info("\t\tADC ch5 2V5:  %u,%03uV\n", tmp / 1000, tmp % 1000);
	rd = cpld_read(cobalt, 0x4c);
	tmp = (rd * 2197) / 1000;
	cobalt_info("\t\tADC ch6 1V8:  %u,%03uV\n", tmp / 1000, tmp % 1000);
	rd = cpld_read(cobalt, 0x50);
	tmp = (rd * 2197) / 1000;
	cobalt_info("\t\tADC ch7 1V5:  %u,%03uV\n", tmp / 1000, tmp % 1000);
	rd = cpld_read(cobalt, 0x54);
	tmp = (rd * 2197) / 1000;
	cobalt_info("\t\tADC ch8 0V9:  %u,%03uV\n", tmp / 1000, tmp % 1000);
}

void cobalt_cpld_status(struct cobalt *cobalt)
{
	u32 rev = cpld_read(cobalt, 0x30);

	switch (rev) {
	case 3:
	case 4:
	case 5:
		cpld_info_ver3(cobalt);
		break;
	default:
		cobalt_info("CPLD revision %u is not supported!\n", rev);
		break;
	}
}

#define DCO_MIN 4850000000ULL
#define DCO_MAX 5670000000ULL

#define SI570_CLOCK_CTRL   0x04
#define S01755_REG_CLOCK_CTRL_BITMAP_CLKHSMA_WR_TRIGGER 0x200
#define S01755_REG_CLOCK_CTRL_BITMAP_CLKHSMA_RST_TRIGGER 0x100
#define S01755_REG_CLOCK_CTRL_BITMAP_CLKHSMA_FPGA_CTRL 0x80
#define S01755_REG_CLOCK_CTRL_BITMAP_CLKHSMA_EN 0x40

#define SI570_REG7   0x08
#define SI570_REG8   0x0c
#define SI570_REG9   0x10
#define SI570_REG10  0x14
#define SI570_REG11  0x18
#define SI570_REG12  0x1c
#define SI570_REG135 0x20
#define SI570_REG137 0x24

struct multiplier {
	unsigned mult, hsdiv, n1;
};

/* List all possible multipliers (= hsdiv * n1). There are lots of duplicates,
   which are all removed in this list to keep the list as short as possible.
   The values for hsdiv and n1 are the actual values, not the register values.
 */
static const struct multiplier multipliers[] = {
	{    4,  4,   1 }, {    5,  5,   1 }, {    6,  6,   1 },
	{    7,  7,   1 }, {    8,  4,   2 }, {    9,  9,   1 },
	{   10,  5,   2 }, {   11, 11,   1 }, {   12,  6,   2 },
	{   14,  7,   2 }, {   16,  4,   4 }, {   18,  9,   2 },
	{   20,  5,   4 }, {   22, 11,   2 }, {   24,  4,   6 },
	{   28,  7,   4 }, {   30,  5,   6 }, {   32,  4,   8 },
	{   36,  6,   6 }, {   40,  4,  10 }, {   42,  7,   6 },
	{   44, 11,   4 }, {   48,  4,  12 }, {   50,  5,  10 },
	{   54,  9,   6 }, {   56,  4,  14 }, {   60,  5,  12 },
	{   64,  4,  16 }, {   66, 11,   6 }, {   70,  5,  14 },
	{   72,  4,  18 }, {   80,  4,  20 }, {   84,  6,  14 },
	{   88, 11,   8 }, {   90,  5,  18 }, {   96,  4,  24 },
	{   98,  7,  14 }, {  100,  5,  20 }, {  104,  4,  26 },
	{  108,  6,  18 }, {  110, 11,  10 }, {  112,  4,  28 },
	{  120,  4,  30 }, {  126,  7,  18 }, {  128,  4,  32 },
	{  130,  5,  26 }, {  132, 11,  12 }, {  136,  4,  34 },
	{  140,  5,  28 }, {  144,  4,  36 }, {  150,  5,  30 },
	{  152,  4,  38 }, {  154, 11,  14 }, {  156,  6,  26 },
	{  160,  4,  40 }, {  162,  9,  18 }, {  168,  4,  42 },
	{  170,  5,  34 }, {  176, 11,  16 }, {  180,  5,  36 },
	{  182,  7,  26 }, {  184,  4,  46 }, {  190,  5,  38 },
	{  192,  4,  48 }, {  196,  7,  28 }, {  198, 11,  18 },
	{  198,  9,  22 }, {  200,  4,  50 }, {  204,  6,  34 },
	{  208,  4,  52 }, {  210,  5,  42 }, {  216,  4,  54 },
	{  220, 11,  20 }, {  224,  4,  56 }, {  228,  6,  38 },
	{  230,  5,  46 }, {  232,  4,  58 }, {  234,  9,  26 },
	{  238,  7,  34 }, {  240,  4,  60 }, {  242, 11,  22 },
	{  248,  4,  62 }, {  250,  5,  50 }, {  252,  6,  42 },
	{  256,  4,  64 }, {  260,  5,  52 }, {  264, 11,  24 },
	{  266,  7,  38 }, {  270,  5,  54 }, {  272,  4,  68 },
	{  276,  6,  46 }, {  280,  4,  70 }, {  286, 11,  26 },
	{  288,  4,  72 }, {  290,  5,  58 }, {  294,  7,  42 },
	{  296,  4,  74 }, {  300,  5,  60 }, {  304,  4,  76 },
	{  306,  9,  34 }, {  308, 11,  28 }, {  310,  5,  62 },
	{  312,  4,  78 }, {  320,  4,  80 }, {  322,  7,  46 },
	{  324,  6,  54 }, {  328,  4,  82 }, {  330, 11,  30 },
	{  336,  4,  84 }, {  340,  5,  68 }, {  342,  9,  38 },
	{  344,  4,  86 }, {  348,  6,  58 }, {  350,  5,  70 },
	{  352, 11,  32 }, {  360,  4,  90 }, {  364,  7,  52 },
	{  368,  4,  92 }, {  370,  5,  74 }, {  372,  6,  62 },
	{  374, 11,  34 }, {  376,  4,  94 }, {  378,  7,  54 },
	{  380,  5,  76 }, {  384,  4,  96 }, {  390,  5,  78 },
	{  392,  4,  98 }, {  396, 11,  36 }, {  400,  4, 100 },
	{  406,  7,  58 }, {  408,  4, 102 }, {  410,  5,  82 },
	{  414,  9,  46 }, {  416,  4, 104 }, {  418, 11,  38 },
	{  420,  5,  84 }, {  424,  4, 106 }, {  430,  5,  86 },
	{  432,  4, 108 }, {  434,  7,  62 }, {  440, 11,  40 },
	{  444,  6,  74 }, {  448,  4, 112 }, {  450,  5,  90 },
	{  456,  4, 114 }, {  460,  5,  92 }, {  462, 11,  42 },
	{  464,  4, 116 }, {  468,  6,  78 }, {  470,  5,  94 },
	{  472,  4, 118 }, {  476,  7,  68 }, {  480,  4, 120 },
	{  484, 11,  44 }, {  486,  9,  54 }, {  488,  4, 122 },
	{  490,  5,  98 }, {  492,  6,  82 }, {  496,  4, 124 },
	{  500,  5, 100 }, {  504,  4, 126 }, {  506, 11,  46 },
	{  510,  5, 102 }, {  512,  4, 128 }, {  516,  6,  86 },
	{  518,  7,  74 }, {  520,  5, 104 }, {  522,  9,  58 },
	{  528, 11,  48 }, {  530,  5, 106 }, {  532,  7,  76 },
	{  540,  5, 108 }, {  546,  7,  78 }, {  550, 11,  50 },
	{  552,  6,  92 }, {  558,  9,  62 }, {  560,  5, 112 },
	{  564,  6,  94 }, {  570,  5, 114 }, {  572, 11,  52 },
	{  574,  7,  82 }, {  576,  6,  96 }, {  580,  5, 116 },
	{  588,  6,  98 }, {  590,  5, 118 }, {  594, 11,  54 },
	{  600,  5, 120 }, {  602,  7,  86 }, {  610,  5, 122 },
	{  612,  6, 102 }, {  616, 11,  56 }, {  620,  5, 124 },
	{  624,  6, 104 }, {  630,  5, 126 }, {  636,  6, 106 },
	{  638, 11,  58 }, {  640,  5, 128 }, {  644,  7,  92 },
	{  648,  6, 108 }, {  658,  7,  94 }, {  660, 11,  60 },
	{  666,  9,  74 }, {  672,  6, 112 }, {  682, 11,  62 },
	{  684,  6, 114 }, {  686,  7,  98 }, {  696,  6, 116 },
	{  700,  7, 100 }, {  702,  9,  78 }, {  704, 11,  64 },
	{  708,  6, 118 }, {  714,  7, 102 }, {  720,  6, 120 },
	{  726, 11,  66 }, {  728,  7, 104 }, {  732,  6, 122 },
	{  738,  9,  82 }, {  742,  7, 106 }, {  744,  6, 124 },
	{  748, 11,  68 }, {  756,  6, 126 }, {  768,  6, 128 },
	{  770, 11,  70 }, {  774,  9,  86 }, {  784,  7, 112 },
	{  792, 11,  72 }, {  798,  7, 114 }, {  810,  9,  90 },
	{  812,  7, 116 }, {  814, 11,  74 }, {  826,  7, 118 },
	{  828,  9,  92 }, {  836, 11,  76 }, {  840,  7, 120 },
	{  846,  9,  94 }, {  854,  7, 122 }, {  858, 11,  78 },
	{  864,  9,  96 }, {  868,  7, 124 }, {  880, 11,  80 },
	{  882,  7, 126 }, {  896,  7, 128 }, {  900,  9, 100 },
	{  902, 11,  82 }, {  918,  9, 102 }, {  924, 11,  84 },
	{  936,  9, 104 }, {  946, 11,  86 }, {  954,  9, 106 },
	{  968, 11,  88 }, {  972,  9, 108 }, {  990, 11,  90 },
	{ 1008,  9, 112 }, { 1012, 11,  92 }, { 1026,  9, 114 },
	{ 1034, 11,  94 }, { 1044,  9, 116 }, { 1056, 11,  96 },
	{ 1062,  9, 118 }, { 1078, 11,  98 }, { 1080,  9, 120 },
	{ 1098,  9, 122 }, { 1100, 11, 100 }, { 1116,  9, 124 },
	{ 1122, 11, 102 }, { 1134,  9, 126 }, { 1144, 11, 104 },
	{ 1152,  9, 128 }, { 1166, 11, 106 }, { 1188, 11, 108 },
	{ 1210, 11, 110 }, { 1232, 11, 112 }, { 1254, 11, 114 },
	{ 1276, 11, 116 }, { 1298, 11, 118 }, { 1320, 11, 120 },
	{ 1342, 11, 122 }, { 1364, 11, 124 }, { 1386, 11, 126 },
	{ 1408, 11, 128 },
};

bool cobalt_cpld_set_freq(struct cobalt *cobalt, unsigned f_out)
{
	const unsigned f_xtal = 39170000;	/* xtal for si598 */
	u64 dco;
	u64 rfreq;
	unsigned delta = 0xffffffff;
	unsigned i_best = 0;
	unsigned i;
	u8 n1, hsdiv;
	u8 regs[6];
	int found = 0;
	int retries = 3;

	for (i = 0; i < ARRAY_SIZE(multipliers); i++) {
		unsigned mult = multipliers[i].mult;
		u32 d;

		dco = (u64)f_out * mult;
		if (dco < DCO_MIN || dco > DCO_MAX)
			continue;
		div_u64_rem((dco << 28) + f_xtal / 2, f_xtal, &d);
		if (d < delta) {
			found = 1;
			i_best = i;
			delta = d;
		}
	}
	if (!found)
		return false;
	dco = (u64)f_out * multipliers[i_best].mult;
	n1 = multipliers[i_best].n1 - 1;
	hsdiv = multipliers[i_best].hsdiv - 4;
	rfreq = div_u64(dco << 28, f_xtal);

	cpld_read(cobalt, SI570_CLOCK_CTRL);

	regs[0] = (hsdiv << 5) | (n1 >> 2);
	regs[1] = ((n1 & 0x3) << 6) | (rfreq >> 32);
	regs[2] = (rfreq >> 24) & 0xff;
	regs[3] = (rfreq >> 16) & 0xff;
	regs[4] = (rfreq >> 8) & 0xff;
	regs[5] = rfreq & 0xff;

	/* The sequence of clock_ctrl flags to set is very weird. It looks
	   like I have to reset it, then set the new frequency and reset it
	   again. It shouldn't be necessary to do a reset, but if I don't,
	   then a strange frequency is set (156.412034 MHz, or register values
	   0x01, 0xc7, 0xfc, 0x7f, 0x53, 0x62).
	 */

	cobalt_dbg(1, "%u: %6ph\n", f_out, regs);

	while (retries--) {
		u8 read_regs[6];

		cpld_write(cobalt, SI570_CLOCK_CTRL,
			S01755_REG_CLOCK_CTRL_BITMAP_CLKHSMA_EN |
			S01755_REG_CLOCK_CTRL_BITMAP_CLKHSMA_FPGA_CTRL);
		usleep_range(10000, 15000);
		cpld_write(cobalt, SI570_REG7, regs[0]);
		cpld_write(cobalt, SI570_REG8, regs[1]);
		cpld_write(cobalt, SI570_REG9, regs[2]);
		cpld_write(cobalt, SI570_REG10, regs[3]);
		cpld_write(cobalt, SI570_REG11, regs[4]);
		cpld_write(cobalt, SI570_REG12, regs[5]);
		cpld_write(cobalt, SI570_CLOCK_CTRL,
			S01755_REG_CLOCK_CTRL_BITMAP_CLKHSMA_EN |
			S01755_REG_CLOCK_CTRL_BITMAP_CLKHSMA_WR_TRIGGER);
		usleep_range(10000, 15000);
		cpld_write(cobalt, SI570_CLOCK_CTRL,
			S01755_REG_CLOCK_CTRL_BITMAP_CLKHSMA_EN |
			S01755_REG_CLOCK_CTRL_BITMAP_CLKHSMA_FPGA_CTRL);
		usleep_range(10000, 15000);
		read_regs[0] = cpld_read(cobalt, SI570_REG7);
		read_regs[1] = cpld_read(cobalt, SI570_REG8);
		read_regs[2] = cpld_read(cobalt, SI570_REG9);
		read_regs[3] = cpld_read(cobalt, SI570_REG10);
		read_regs[4] = cpld_read(cobalt, SI570_REG11);
		read_regs[5] = cpld_read(cobalt, SI570_REG12);
		cpld_write(cobalt, SI570_CLOCK_CTRL,
			S01755_REG_CLOCK_CTRL_BITMAP_CLKHSMA_EN |
			S01755_REG_CLOCK_CTRL_BITMAP_CLKHSMA_FPGA_CTRL |
			S01755_REG_CLOCK_CTRL_BITMAP_CLKHSMA_RST_TRIGGER);
		usleep_range(10000, 15000);
		cpld_write(cobalt, SI570_CLOCK_CTRL,
			S01755_REG_CLOCK_CTRL_BITMAP_CLKHSMA_EN);
		usleep_range(10000, 15000);

		if (!memcmp(read_regs, regs, sizeof(read_regs)))
			break;
		cobalt_dbg(1, "retry: %6ph\n", read_regs);
	}
	if (2 - retries)
		cobalt_info("Needed %d retries\n", 2 - retries);

	return true;
}

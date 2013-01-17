/* linux/arch/arm/mach-exynos/cpufreq-4x12.c
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4X12 - CPU frequency scaling support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/cpufreq.h>

#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/regs-pmu.h>
#include <mach/cpufreq.h>
#include <mach/asv.h>

#include <plat/clock.h>
#include <plat/cpu.h>

#undef PRINT_DIV_VAL

#define ENABLE_CLKOUT

static int max_support_idx;
static int min_support_idx = (CPUFREQ_LEVEL_END - 1);
static struct clk *cpu_clk;
static struct clk *moutcore;
static struct clk *mout_mpll;
static struct clk *mout_apll;
static bool need_dynamic_ema = false;

struct cpufreq_clkdiv {
	unsigned int	index;
	unsigned int	clkdiv;
	unsigned int	clkdiv1;
};

static unsigned int exynos4x12_volt_table[CPUFREQ_LEVEL_END];

#if defined(CONFIG_BOARD_ODROID_X2) || defined(CONFIG_BOARD_ODROID_Q2) || defined(CONFIG_BOARD_ODROID_U2)
	static struct cpufreq_frequency_table exynos4x12_freq_table[] = {
		{L0, 2000*1000}, 
		{L1, 1920*1000}, 
		{L2, 1800*1000}, 
		{L3, 1704*1000}, 
		{L4, 1600*1000},
		{L5, 1500*1000}, 
		{L6, 1400*1000}, 
		{L7, 1300*1000}, 
		{L8, 1200*1000}, 
		{L9, 1100*1000},
		{L10, 1000*1000}, 
		{L11, 900*1000}, 
		{L12, 800*1000}, 
		{L13, 700*1000}, 
		{L14, 600*1000},
		{L15, 500*1000}, 
		{L16, 400*1000}, 
		{L17, 300*1000}, 
		{L18, 200*1000},
		{0, CPUFREQ_TABLE_END},
	}; 
#elif defined(CONFIG_BOARD_ODROID_X) || defined(CONFIG_BOARD_ODROID_Q) || defined(CONFIG_BOARD_ODROID_U)	
	static struct cpufreq_frequency_table exynos4x12_freq_table[] = {
		{L0, 1800*1000}, 
		{L1, 1704*1000}, 
		{L2, 1600*1000},
                {L3, 1500*1000}, 
		{L4, 1400*1000}, 
		{L5, 1300*1000}, 
		{L6, 1200*1000}, 
		{L7, 1100*1000},
                {L8, 1000*1000}, 
		{L9, 900*1000}, 
		{L10, 800*1000}, 
		{L11, 700*1000}, 
		{L12, 600*1000},
                {L13, 500*1000}, 
		{L14, 400*1000}, 
		{L15, 300*1000}, 
		{L16, 200*1000},
                {0, CPUFREQ_TABLE_END},
        }; 
#else
static struct cpufreq_frequency_table exynos4x12_freq_table[] = {
	{L0, 1800*1000},
	{L1, 1704*1000},
	{L2, 1600*1000},
	{L3, 1500*1000},
	{L4, 1400*1000},
	{L5, 1300*1000},
	{L6, 1200*1000},
	{L7, 1100*1000},
	{L8, 1000*1000},
	{L9, 900*1000},
	{L10, 800*1000},
	{L11, 700*1000},
	{L12, 600*1000},
	{L13, 500*1000},
	{L14, 400*1000},
	{L15, 300*1000},
	{L16, 200*1000},
	{0, CPUFREQ_TABLE_END},
};

#endif

static struct cpufreq_clkdiv exynos4x12_clkdiv_table[CPUFREQ_LEVEL_END];

static unsigned int clkdiv_cpu0_4212[CPUFREQ_LEVEL_END][8] = {
	/*
	 * Clock divider value for following
	 * { DIVCORE, DIVCOREM0, DIVCOREM1, DIVPERIPH,
	 *		DIVATB, DIVPCLK_DBG, DIVAPLL, DIVCORE2 }
	 */
	/* ARM L0: 1600Mhz */
	{ 0, 3, 7, 0, 6, 1, 2, 0 },

	/* ARM L1: 1500Mhz */
	{ 0, 3, 7, 0, 6, 1, 2, 0 },

	/* ARM L2: 1400Mhz */
	{ 0, 3, 7, 0, 6, 1, 2, 0 },

	/* ARM L3: 1300Mhz */
	{ 0, 3, 7, 0, 5, 1, 2, 0 },

	/* ARM L4: 1200Mhz */
	{ 0, 3, 7, 0, 5, 1, 2, 0 },

	/* ARM L5: 1100MHz */
	{ 0, 3, 6, 0, 4, 1, 2, 0 },

	/* ARM L6: 1000MHz */
	{ 0, 2, 5, 0, 4, 1, 1, 0 },

	/* ARM L7: 900MHz */
	{ 0, 2, 5, 0, 3, 1, 1, 0 },

	/* ARM L8: 800MHz */
	{ 0, 2, 5, 0, 3, 1, 1, 0 },

	/* ARM L9: 700MHz */
	{ 0, 2, 4, 0, 3, 1, 1, 0 },

	/* ARM L10: 600MHz */
	{ 0, 2, 4, 0, 3, 1, 1, 0 },

	/* ARM L11: 500MHz */
	{ 0, 2, 4, 0, 3, 1, 1, 0 },

	/* ARM L12: 400MHz */
	{ 0, 2, 4, 0, 3, 1, 1, 0 },

	/* ARM L13: 300MHz */
	{ 0, 2, 4, 0, 2, 1, 1, 0 },

	/* ARM L14: 200MHz */
	{ 0, 1, 3, 0, 1, 1, 1, 0 },
};

static unsigned int clkdiv_cpu0_4412[CPUFREQ_LEVEL_END][8] = {
	/*
	 * Clock divider value for following
	 * { DIVCORE, DIVCOREM0, DIVCOREM1, DIVPERIPH,
	 *		DIVATB, DIVPCLK_DBG, DIVAPLL, DIVCORE2 }
	 */
	#if defined(CONFIG_BOARD_ODROID_X2) || defined(CONFIG_BOARD_ODROID_Q2) || defined(CONFIG_BOARD_ODROID_U2)
	/* 2000 Mhz Support */
	{ 0, 3, 7, 0, 6, 1, 2, 0},
	
	/* 1920 Mhz Support */
	{ 0, 3, 7, 0, 6, 1, 2, 0},
	#endif
	/* 1800 Mhz Support */
	{ 0, 3, 7, 0, 6, 1, 2, 0},

	/* 1704 Mhz Support */
	{ 0, 3, 7, 0, 6, 1, 2, 0},

	/* ARM L0: 1600Mhz */
	{ 0, 3, 7, 0, 6, 1, 2, 0 },
	 
	/* ARM L1: 1500Mhz */
	{ 0, 3, 7, 0, 6, 1, 2, 0 },

	/* ARM L2: 1400Mhz */
	{ 0, 3, 7, 0, 6, 1, 2, 0 },

	/* ARM L3: 1300Mhz */
	{ 0, 3, 7, 0, 5, 1, 2, 0 },

	/* ARM L4: 1200Mhz */
	{ 0, 3, 7, 0, 5, 1, 2, 0 },

	/* ARM L5: 1100MHz */
	{ 0, 3, 6, 0, 4, 1, 2, 0 },

	/* ARM L6: 1000MHz */
	{ 0, 2, 5, 0, 4, 1, 1, 0 },

	/* ARM L7: 900MHz */
	{ 0, 2, 5, 0, 3, 1, 1, 0 },

	/* ARM L8: 800MHz */
	{ 0, 2, 5, 0, 3, 1, 1, 0 },

	/* ARM L9: 700MHz */
	{ 0, 2, 4, 0, 3, 1, 1, 0 },

	/* ARM L10: 600MHz */
	{ 0, 2, 4, 0, 3, 1, 1, 0 },

	/* ARM L11: 500MHz */
	{ 0, 2, 4, 0, 3, 1, 1, 0 },

	/* ARM L11: 400MHz */
	{ 0, 2, 4, 0, 3, 1, 1, 0 },

	/* ARM L12: 300MHz */
	{ 0, 2, 4, 0, 2, 1, 1, 0 },

	/* ARM L13: 200MHz */
	{ 0, 1, 3, 0, 1, 1, 1, 0 },
};

static unsigned int clkdiv_cpu1_4212[CPUFREQ_LEVEL_END][2] = {
	/* Clock divider value for following
	 * { DIVCOPY, DIVHPM }
	 */
	/* ARM L0: 1600MHz */
	{ 6, 0 },

	/* ARM L1: 1500MHz */
	{ 6, 0 },

	/* ARM L2: 1400MHz */
	{ 6, 0 },

	/* ARM L3: 1300MHz */
	{ 5, 0 },

	/* ARM L4: 1200MHz */
	{ 5, 0 },

	/* ARM L5: 1100MHz */
	{ 4, 0 },

	/* ARM L6: 1000MHz */
	{ 4, 0 },

	/* ARM L7: 900MHz */
	{ 3, 0 },

	/* ARM L8: 800MHz */
	{ 3, 0 },

	/* ARM L9: 700MHz */
	{ 3, 0 },

	/* ARM L10: 600MHz */
	{ 3, 0 },

	/* ARM L11: 500MHz */
	{ 3, 0 },

	/* ARM L12: 400MHz */
	{ 3, 0 },

	/* ARM L13: 300MHz */
	{ 3, 0 },

	/* ARM L14: 200MHz */
	{ 3, 0 },
};

static unsigned int clkdiv_cpu1_4412[CPUFREQ_LEVEL_END][3] = {
	/* Clock divider value for following
	 * { DIVCOPY, DIVHPM, DIVCORES }
	 */
#if defined(CONFIG_BOARD_ODROID_X2) || defined(CONFIG_BOARD_ODROID_Q2) || defined(CONFIG_BOARD_ODROID_U2)
	/* 2000Mhz */
	{ 7, 0, 7 },
	
	/* 1920Mhz */
	{ 7, 0, 7 },
#endif
	/* 1800Mhz */
	{ 7, 0, 7 },

	/* 1704Mhz */
	{ 7, 0, 7 },

	/* ARM L0: 1600MHz */
	{ 6, 0, 7 },
	
	/* ARM L0: 1500MHz */
	{ 6, 0, 7 },

	/* ARM L1: 1400MHz */
	{ 6, 0, 6 },

	/* ARM L2: 1300MHz */
	{ 5, 0, 6 },

	/* ARM L3: 1200MHz */
	{ 5, 0, 5 },

	/* ARM L4: 1100MHz */
	{ 4, 0, 5 },

	/* ARM L5: 1000MHz */
	{ 4, 0, 4 },

	/* ARM L6: 900MHz */
	{ 3, 0, 4 },

	/* ARM L7: 800MHz */
	{ 3, 0, 3 },

	/* ARM L8: 700MHz */
	{ 3, 0, 3 },

	/* ARM L9: 600MHz */
	{ 3, 0, 2 },

	/* ARM L10: 500MHz */
	{ 3, 0, 2 },

	/* ARM L11: 400MHz */
	{ 3, 0, 1 },

	/* ARM L12: 300MHz */
	{ 3, 0, 1 },

	/* ARM L13: 200MHz */
	{ 3, 0, 0 },
};

static unsigned int exynos4x12_apll_pms_table[CPUFREQ_LEVEL_END] = {

#if defined(CONFIG_BOARD_ODROID_X2) || defined(CONFIG_BOARD_ODROID_Q2) || defined(CONFIG_BOARD_ODROID_U2)
	/* 2000MHz */
	((250<<16)|(3<<8)|(0x0)),
	
	/* 1920Mhz */
	((240<<16)|(3<<8)|(0x0)),
#endif
	/* APLL FOUT L2: 1800MHz */
	((300<<16)|(4<<8)|(0x0)),

	/* APLL FOUT L3: 1704MHz */
	((213<<16)|(3<<8)|(0x0)),

	/* APLL FOUT L4: 1600MHz */
	((200<<16)|(3<<8)|(0x0)),
	        
	/* APLL FOUT L5: 1500MHz */
	((250<<16)|(4<<8)|(0x0)),

	/* APLL FOUT L6: 1400MHz */
	((175<<16)|(3<<8)|(0x0)),

	/* APLL FOUT L7: 1300MHz */
	((325<<16)|(6<<8)|(0x0)),

	/* APLL FOUT L8: 1200MHz */
	((200<<16)|(4<<8)|(0x0)),

	/* APLL FOUT L9: 1100MHz */
	((275<<16)|(6<<8)|(0x0)),

	/* APLL FOUT L10: 1000MHz */
	((125<<16)|(3<<8)|(0x0)),

	/* APLL FOUT L11: 900MHz */
	((150<<16)|(4<<8)|(0x0)),

	/* APLL FOUT L12: 800MHz */
	((100<<16)|(3<<8)|(0x0)),

	/* APLL FOUT L13: 700MHz */
	((175<<16)|(3<<8)|(0x1)),

	/* APLL FOUT L14: 600MHz */
	((200<<16)|(4<<8)|(0x1)),

	/* APLL FOUT L15: 500MHz */
	((125<<16)|(3<<8)|(0x1)),

	/* APLL FOUT L16 400MHz */
	((100<<16)|(3<<8)|(0x1)),

	/* APLL FOUT L17: 300MHz */
	((200<<16)|(4<<8)|(0x2)),

	/* APLL FOUT L18: 200MHz */
	((100<<16)|(3<<8)|(0x2)),

};

/*
 * ASV group voltage table
 */

#define NO_ABB_LIMIT	L14

static const unsigned int asv_voltage_4212[CPUFREQ_LEVEL_END][12] = {
	/*   ASV0,    ASV1,    ASV2,    ASV3,	 ASV4,	  ASV5,	   ASV6,    ASV7,    ASV8,    ASV9,   ASV10,   ASV11 */
	{	0, 1300000, 1300000, 1275000, 1300000, 1287500,	1275000, 1250000, 1237500, 1225000, 1225000, 1212500 }, /* L0 */
	{	0, 1300000, 1300000, 1275000, 1300000, 1287500,	1275000, 1250000, 1237500, 1225000, 1225000, 1212500 }, /* L1 */
	{ 1300000, 1287500, 1250000, 1225000, 1237500, 1237500,	1225000, 1200000, 1187500, 1175000, 1175000, 1162500 }, /* L2 */
	{ 1237500, 1225000, 1200000, 1175000, 1187500, 1187500,	1162500, 1150000, 1137500, 1125000, 1125000, 1112500 }, /* L3 */
	{ 1187500, 1175000, 1150000, 1137500, 1150000, 1137500,	1125000, 1100000, 1087500, 1075000, 1075000, 1062500 }, /* L4 */
	{ 1137500, 1125000, 1112500, 1087500, 1112500, 1112500,	1075000, 1062500, 1050000, 1025000, 1025000, 1012500 }, /* L5 */
	{ 1100000, 1087500, 1075000, 1050000, 1075000, 1062500,	1037500, 1025000, 1012500, 1000000,  987500,  975000 }, /* L6 */
	{ 1050000, 1037500, 1025000, 1000000, 1025000, 1025000,	 987500,  975000,  962500,  950000,  937500,  925000 }, /* L7 */
	{ 1012500, 1000000,  987500,  962500,  987500,	975000,	 962500,  937500,  925000,  912500,  912500,  900000 }, /* L8 */
	{  962500,  950000,  937500,  912500,  937500,	937500,	 925000,  900000,  900000,  900000,  900000,  900000 }, /* L9 */
	{  925000,  912500,  912500,  900000,  912500,	900000,	 900000,  900000,  900000,  900000,  900000,  900000 }, /* L10 */
	{  912500,  900000,  900000,  900000,  900000,	900000,	 900000,  900000,  900000,  900000,  900000,  900000 }, /* L11 */
	{  912500,  900000,  900000,  900000,  900000,	900000,	 900000,  900000,  900000,  900000,  900000,  900000 }, /* L12 */
	{  912500,  900000,  900000,  900000,  900000,	900000,	 900000,  900000,  900000,  900000,  900000,  900000 }, /* L13 */
	{  912500,  900000,  900000,  900000,  900000,	900000,	 900000,  900000,  900000,  900000,  900000,  900000 }, /* L14 */
};

static const unsigned int asv_voltage_s[CPUFREQ_LEVEL_END] = {
	1300000, 1300000, 1300000, 1250000, 1200000, 1175000, 1100000,
	1050000, 1025000, 1000000, 1000000, 1000000, 950000, 950000
};

#if defined(CONFIG_BOARD_ODROID_X2) || defined(CONFIG_BOARD_ODROID_Q2) || defined(CONFIG_BOARD_ODROID_U2)
static const unsigned int asv_voltage_step_12_5[CPUFREQ_LEVEL_END][12] = {
    /*   ASV0,    ASV1,    ASV2,    ASV3,    ASV4,    ASV5,    ASV6,    ASV7,    ASV8,    ASV9,   ASV10,   ASV11 */
    { 1425000, 1425000, 1425000, 1425000, 1425000, 1425000, 1425000, 1425000, 1425000, 1425000, 1425000, 1425000 }, /* L0 */  
    { 1400000, 1400000, 1400000, 1400000, 1400000, 1400000, 1400000, 1400000, 1400000, 1400000, 1400000, 1400000 }, /* L1 */  
    { 1400000, 1400000, 1400000, 1400000, 1400000, 1400000, 1400000, 1400000, 1400000, 1387500, 1375000, 1362500 }, /* L2 */  
    { 1350000, 1312500, 1300000, 1287500, 1300000, 1287500, 1275000, 1250000, 1250000, 1237500, 1225000, 1212500 }, /* L3 */  
    { 1350000, 1312500, 1300000, 1287500, 1300000, 1287500, 1275000, 1250000, 1250000, 1237500, 1225000, 1212500 }, /* L4 */  
    { 1300000, 1275000, 1237500, 1237500, 1250000, 1250000, 1237500, 1212500, 1200000, 1200000, 1187500, 1175000 }, /* L5 */  
    { 1225000, 1212500, 1200000, 1187500, 1200000, 1187500, 1175000, 1150000, 1137500, 1125000, 1125000, 1112500 }, /* L6 */  
    { 1175000, 1162500, 1150000, 1137500, 1150000, 1137500, 1125000, 1100000, 1100000, 1075000, 1075000, 1062500 }, /* L7 */  
    { 1125000, 1112500, 1100000, 1087500, 1100000, 1087500, 1075000, 1050000, 1037500, 1025000, 1025000, 1012500 }, /* L8 */  
    { 1075000, 1062500, 1050000, 1050000, 1050000, 1037500, 1025000, 1012500, 1000000,  987500,  987500,  975000 }, /* L9 */  
    { 1037500, 1025000, 1000000, 1000000, 1000000,  987500,  975000,  962500,  962500,  962500,  962500,  950000 }, /* L10 */ 
    { 1012500, 1000000,  975000,  975000,  975000,  975000,  962500,  962500,  950000,  950000,  950000,  937500 }, /* L11 */ 
    { 1000000,  987500,  962500,  962500,  962500,  962500,  950000,  950000,  937500,  937500,  937500,  925000 }, /* L12 */ 
    {  987500,  975000,  950000,  937500,  950000,  937500,  937500,  937500,  912500,  912500,  912500,  900000 }, /* L13 */
    {  975000,  962500,  950000,  925000,  950000,  925000,  925000,  925000,  900000,  900000,  900500,  887500 }, /* L14 */
    {  925000,  912500,  900000,  900000,  900000,  900000,  900000,  900000,  887500,  875000,  875000,  862500 }, /* L15 */
    {  925000,  912500,  900000,  900000,  900000,  900000,  900000,  900000,  887500,  875000,  875000,  862500 }, /* L16 */
    {  925000,  912500,  900000,  900000,  900000,  900000,  900000,  900000,  887500,  875000,  875000,  862500 }, /* L17 */
};
#elif defined(CONFIG_BOARD_ODROID_X) || defined(CONFIG_BOARD_ODROID_Q) || defined(CONFIG_BOARD_ODROID_U)
static const unsigned int asv_voltage_step_12_5[CPUFREQ_LEVEL_END][12] = {
    /*   ASV0,    ASV1,    ASV2,    ASV3,    ASV4,    ASV5,    ASV6,    ASV7,    ASV8,    ASV9,   ASV10,   ASV11 */
    { 1450000, 1450000, 1450000, 1450000, 1450000, 1450000, 1450000, 1450000, 1450000, 1387500, 1375000, 1362500 }, /* L2 */
    { 1400000, 1312500, 1300000, 1287500, 1300000, 1287500, 1275000, 1250000, 1250000, 1237500, 1225000, 1212500 }, /* L3 */
    { 1400000, 1312500, 1300000, 1287500, 1300000, 1287500, 1275000, 1250000, 1250000, 1237500, 1225000, 1212500 }, /* L4 */
    { 1350000, 1275000, 1237500, 1237500, 1250000, 1250000, 1237500, 1212500, 1200000, 1200000, 1187500, 1175000 }, /* L5 */
    { 1325000, 1212500, 1200000, 1187500, 1200000, 1187500, 1175000, 1150000, 1137500, 1125000, 1125000, 1112500 }, /* L6 */
    { 1275000, 1162500, 1150000, 1137500, 1150000, 1137500, 1125000, 1100000, 1100000, 1075000, 1075000, 1062500 }, /* L7 */
    { 1225000, 1112500, 1100000, 1087500, 1100000, 1087500, 1075000, 1050000, 1037500, 1025000, 1025000, 1012500 }, /* L8 */
    { 1175000, 1062500, 1050000, 1050000, 1050000, 1037500, 1025000, 1012500, 1000000,  987500,  987500,  975000 }, /* L9 */
    { 1137500, 1025000, 1000000, 1000000, 1000000,  987500,  975000,  962500,  962500,  962500,  962500,  950000 }, /* L10 */
    { 1112500, 1000000,  975000,  975000,  975000,  975000,  962500,  962500,  950000,  950000,  950000,  937500 }, /* L11 */
    { 1100000,  987500,  962500,  962500,  962500,  962500,  950000,  950000,  937500,  937500,  937500,  925000 }, /* L12 */
    {  987500,  975000,  950000,  937500,  950000,  937500,  937500,  937500,  912500,  912500,  912500,  900000 }, /* L13 */
    {  975000,  962500,  950000,  925000,  950000,  925000,  925000,  925000,  900000,  900000,  900500,  887500 }, /* L14 */
    {  925000,  912500,  900000,  900000,  900000,  900000,  900000,  900000,  887500,  875000,  875000,  862500 }, /* L15 */
    {  925000,  912500,  900000,  900000,  900000,  900000,  900000,  900000,  887500,  875000,  875000,  862500 }, /* L16 */
    {  925000,  912500,  900000,  900000,  900000,  900000,  900000,  900000,  887500,  875000,  875000,  862500 }, /* L17 */
};
#else
* 20120210 DVFS table version */
static const unsigned int asv_voltage_step_12_5[CPUFREQ_LEVEL_END][12] = {
	/*   ASV0,    ASV1,    ASV2,    ASV3,	 ASV4,	  ASV5,	   ASV6,    ASV7,    ASV8,    ASV9,   ASV10,   ASV11 */
	{ 1400000, 1400000, 1400000, 1400000, 1400000, 1400000,	1400000, 1400000, 1400000, 1400000, 1400000, 1400000 }, /* L0 1800MHz */
	{ 1400000, 1400000, 1400000, 1400000, 1400000, 1400000,	1400000, 1400000, 1400000, 1387500, 1375000, 1362500 }, /* L1 1700MHz */
	{ 1400000, 1400000, 1400000, 1400000, 1387500, 1387500,	1375000, 1362500, 1350000, 1337500, 1325000, 1312500 }, /* L2 1600MHz */
	{ 1387500, 1375000, 1362500, 1350000, 1337500, 1325000,	1312500, 1300000, 1287500, 1275000, 1262500, 1250000 }, /* L3 1500MHz */
	{ 1325000, 1312500, 1300000, 1287500, 1300000, 1287500,	1275000, 1250000, 1250000, 1237500, 1225000, 1212500 }, /* L4 1400MHz */
	{ 1300000, 1275000, 1237500, 1237500, 1250000, 1250000,	1237500, 1212500, 1200000, 1200000, 1187500, 1175000 }, /* L5 1300MHz */
	{ 1225000, 1212500, 1200000, 1187500, 1200000, 1187500,	1175000, 1150000, 1137500, 1125000, 1125000, 1112500 }, /* L6 1200MHz */
	{ 1175000, 1162500, 1150000, 1137500, 1150000, 1137500,	1125000, 1100000, 1100000, 1075000, 1075000, 1062500 }, /* L7 1100MHz */
	{ 1125000, 1112500, 1100000, 1087500, 1100000, 1087500,	1075000, 1050000, 1037500, 1025000, 1025000, 1012500 }, /* L8 1000MHz */
	{ 1075000, 1062500, 1050000, 1050000, 1050000, 1037500,	1025000, 1012500, 1000000,  987500,  987500,  975000 }, /* L9  900MHz */
	{ 1037500, 1025000, 1000000, 1000000, 1000000,  987500,	 975000,  962500,  962500,  962500,  962500,  950000 }, /* L10 800MHz */
	{ 1012500, 1000000,  975000,  975000,  975000,  975000,	 962500,  962500,  950000,  950000,  950000,  937500 }, /* L11 700MHz */
	{ 1000000,  987500,  962500,  962500,  962500,  962500,	 950000,  950000,  937500,  937500,  937500,  925000 }, /* L12 600MHz */
	{  987500,  975000,  950000,  937500,  950000,  937500,	 937500,  937500,  912500,  912500,  912500,  900000 }, /* L13 500MHz */
	{  975000,  962500,  950000,  925000,  950000,  925000,	 925000,  925000,  900000,  900000,  900000,  887500 }, /* L14 400MHz */
	{  950000,  937500,  925000,  900000,  925000,  900000,	 900000,  900000,  900000,  887500,  875000,  862500 }, /* L15 300MHz */
	{  925000,  912500,  900000,  900000,  900000,  900000,	 900000,  900000,  887500,  875000,  875000,  862500 }, /* L16 200MHz */
};
#endif
/* 20120927 DVFS table for pega prime */
static const unsigned int asv_voltage_step_12_5_rev2[CPUFREQ_LEVEL_END][13] = {
	/*   ASV0,    ASV1,    ASV2,    ASV3,	 ASV4,	  ASV5,	   ASV6,    ASV7,    ASV8,    ASV9,   ASV10,   ASV11    ASV12 */
	{ 1400000, 1400000, 1400000, 1400000, 1400000, 1400000,	1400000, 1400000, 1400000, 1400000, 1400000, 1400000, 1400000}, /* L0 1800MHz */
	{ 1400000, 1400000, 1400000, 1400000, 1400000, 1400000,	1400000, 1400000, 1400000, 1387500, 1375000, 1362500, 1350000}, /* L1 1700MHz */
	{ 1312500, 1312500, 1312500, 1312500, 1300000, 1287500, 1275000, 1262500, 1250000, 1237500, 1212500, 1200000, 1187500 }, /* L2 1600MHz */
	{ 1275000, 1262500, 1262500, 1262500, 1250000, 1237500,	1225000, 1212500, 1200000, 1187500, 1162500, 1150000, 1137500 }, /* L3 1500MHz */
	{ 1237500, 1225000, 1225000, 1225000, 1212500, 1200000, 1187500, 1175000, 1162500, 1150000, 1125000, 1112500, 1100000 }, /* L4 1400MHz */
	{ 1187500, 1175000, 1175000, 1175000, 1162500, 1150000, 1137500, 1125000, 1112500, 1100000, 1075000, 1062500, 1050000 }, /* L5 1300MHz */
	{ 1150000, 1137500, 1137500, 1137500, 1125000, 1112500, 1100000, 1087500, 1075000, 1062500, 1037500, 1025000, 1012500 }, /* L6 1200MHz */
	{ 1112500, 1100000, 1100000, 1100000, 1087500, 1075000, 1062500, 1050000, 1037500, 1025000, 1000000,  987500,  975000 }, /* L7 1100MHz */
	{ 1087500, 1075000, 1075000, 1075000, 1062500, 1050000, 1037500, 1025000, 1012500, 1000000,  975000,  962500,  950000 }, /* L8 1000MHz */
	{ 1062500, 1050000, 1050000, 1050000, 1037500, 1025000, 1012500, 1000000,  987500,  975000,  950000,  937500,  925000 }, /* L9  900MHz */
	{ 1025000, 1012500, 1012500, 1012500, 1000000,  987500,  975000,  962500,  950000,  937500,  912500,  900000,  887500 }, /* L10 800MHz */
	{ 1000000,  987500,  987500,  987500,  975000,  962500,  950000,  937500,  925000,  912500,  887500,  887500,  887500 }, /* L11 700MHz */
	{  975000,  962500,  962500,  962500,  950000,  937500,  925000,  912500,  900000,  887500,  875000,  875000,  875000 }, /* L12 600MHz */
	{  962500,  950000,  950000,  950000,  937500,  925000,  912500,  900000,  887500,  887500,  875000,  875000,  875000 }, /* L13 500MHz */
	{  950000,  937500,  937500,  937500,  925000,  912500,  900000,  887500,  887500,  887500,  875000,  875000,  875000 }, /* L14 400MHz */
	{  937500,  925000,  925000,  925000,  912500,  900000,  887500,  887500,  887500,  887500,  875000,  875000,  875000 }, /* L15 300MHz */
	{  925000,  912500,  912500,  912500,  900000,  887500,  887500,  887500,  887500,  887500,  875000,  875000,  875000 }, /* L16 200MHz */
};

static void set_clkdiv(unsigned int div_index)
{
	unsigned int tmp;
	unsigned int stat_cpu1;

	/* Change Divider - CPU0 */

	tmp = exynos4x12_clkdiv_table[div_index].clkdiv;

	__raw_writel(tmp, EXYNOS4_CLKDIV_CPU);

	do {
		tmp = __raw_readl(EXYNOS4_CLKDIV_STATCPU);
	} while (tmp & 0x11111111);

#ifdef PRINT_DIV_VAL
	tmp = __raw_readl(EXYNOS4_CLKDIV_CPU);
	pr_info("DIV_CPU0[0x%x]\n", tmp);

#endif

	/* Change Divider - CPU1 */
	tmp = exynos4x12_clkdiv_table[div_index].clkdiv1;

	__raw_writel(tmp, EXYNOS4_CLKDIV_CPU1);
	if (soc_is_exynos4212())
		stat_cpu1 = 0x11;
	else
		stat_cpu1 = 0x111;

	do {
		tmp = __raw_readl(EXYNOS4_CLKDIV_STATCPU1);
	} while (tmp & stat_cpu1);
#ifdef PRINT_DIV_VAL
	tmp = __raw_readl(EXYNOS4_CLKDIV_CPU1);
	pr_info("DIV_CPU1[0x%x]\n", tmp);
#endif
}

static void set_apll(unsigned int new_index,
			     unsigned int old_index)
{
	unsigned int tmp, pdiv;

	/* 1. MUX_CORE_SEL = MPLL,
	 * ARMCLK uses MPLL for lock time */
	if (clk_set_parent(moutcore, mout_mpll))
		printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
				mout_mpll->name, moutcore->name);

	do {
		tmp = (__raw_readl(EXYNOS4_CLKMUX_STATCPU)
			>> EXYNOS4_CLKSRC_CPU_MUXCORE_SHIFT);
		tmp &= 0x7;
	} while (tmp != 0x2);

	/* 2. Set APLL Lock time */
	pdiv = ((exynos4x12_apll_pms_table[new_index] >> 8) & 0x3f);

	__raw_writel((pdiv * 250), EXYNOS4_APLL_LOCK);

	/* 3. Change PLL PMS values */
	tmp = __raw_readl(EXYNOS4_APLL_CON0);
	tmp &= ~((0x3ff << 16) | (0x3f << 8) | (0x7 << 0));
	tmp |= exynos4x12_apll_pms_table[new_index];
	__raw_writel(tmp, EXYNOS4_APLL_CON0);

	/* 4. wait_lock_time */
	do {
		tmp = __raw_readl(EXYNOS4_APLL_CON0);
	} while (!(tmp & (0x1 << EXYNOS4_APLLCON0_LOCKED_SHIFT)));

	/* 5. MUX_CORE_SEL = APLL */
	if (clk_set_parent(moutcore, mout_apll))
		printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
				mout_apll->name, moutcore->name);

	do {
		tmp = __raw_readl(EXYNOS4_CLKMUX_STATCPU);
		tmp &= EXYNOS4_CLKMUX_STATCPU_MUXCORE_MASK;
	} while (tmp != (0x1 << EXYNOS4_CLKSRC_CPU_MUXCORE_SHIFT));

}

bool exynos4x12_pms_change(unsigned int old_index, unsigned int new_index)
{
	unsigned int old_pm = (exynos4x12_apll_pms_table[old_index] >> 8);
	unsigned int new_pm = (exynos4x12_apll_pms_table[new_index] >> 8);

	return (old_pm == new_pm) ? 0 : 1;
}

static void exynos4x12_set_frequency(unsigned int old_index,
				  unsigned int new_index)
{
	unsigned int tmp;

	if (old_index > new_index) {
		if (exynos4x12_volt_table[new_index] >= 950000 &&
				need_dynamic_ema)
				__raw_writel(0x101, EXYNOS4_EMA_CONF);

		if ((samsung_rev() >= EXYNOS4412_REV_2_0)
			&& (exynos_result_of_asv > 2)
			&& (old_index > L8) && (new_index <= L8)) {
			exynos4x12_set_abb_member(ABB_ARM, ABB_MODE_130V);
		}

		if (!exynos4x12_pms_change(old_index, new_index)) {
			/* 1. Change the system clock divider values */
			set_clkdiv(new_index);
			/* 2. Change just s value in apll m,p,s value */
			tmp = __raw_readl(EXYNOS4_APLL_CON0);
			tmp &= ~(0x7 << 0);
			tmp |= (exynos4x12_apll_pms_table[new_index] & 0x7);
			__raw_writel(tmp, EXYNOS4_APLL_CON0);

		} else {
			/* Clock Configuration Procedure */
			/* 1. Change the system clock divider values */
			set_clkdiv(new_index);
			/* 2. Change the apll m,p,s value */
			set_apll(new_index, old_index);
		}
	} else if (old_index < new_index) {
		if (!exynos4x12_pms_change(old_index, new_index)) {
			/* 1. Change just s value in apll m,p,s value */
			tmp = __raw_readl(EXYNOS4_APLL_CON0);
			tmp &= ~(0x7 << 0);
			tmp |= (exynos4x12_apll_pms_table[new_index] & 0x7);
			__raw_writel(tmp, EXYNOS4_APLL_CON0);
			/* 2. Change the system clock divider values */
			set_clkdiv(new_index);
		} else {
			/* Clock Configuration Procedure */
			/* 1. Change the apll m,p,s value */
			set_apll(new_index, old_index);
			/* 2. Change the system clock divider values */
			set_clkdiv(new_index);
		}
		if ((samsung_rev() >= EXYNOS4412_REV_2_0)
			&& (exynos_result_of_asv > 2)
			&& (old_index <= L8) && (new_index > L8)) {
			exynos4x12_set_abb_member(ABB_ARM, ABB_MODE_100V);
		}
		if (exynos4x12_volt_table[new_index] < 950000 &&
				need_dynamic_ema)
			__raw_writel(0x404, EXYNOS4_EMA_CONF);
	}

	/* ABB value is changed in below case */
	if (soc_is_exynos4412() && (exynos_result_of_asv > 3)) {
		if (new_index == L13)
			exynos4x12_set_abb_member(ABB_ARM, ABB_MODE_100V);
		else
			exynos4x12_set_abb_member(ABB_ARM, ABB_MODE_130V);
	}
}

/* Get maximum cpufreq index of chip */
static void __init set_volt_table(void)
{
	unsigned int i, tmp;

	max_support_idx = L0;

	pr_info("DVFS : VDD_ARM Voltage table set with %d Group\n", exynos_result_of_asv);

	if (exynos_result_of_asv == 0xff) {
		for (i = 0 ; i < CPUFREQ_LEVEL_END ; i++)
			exynos4x12_volt_table[i] = asv_voltage_s[i];
	} else {
		if (soc_is_exynos4212()) {
			for (i = 0 ; i < CPUFREQ_LEVEL_END ; i++)
				exynos4x12_volt_table[i] =
					asv_voltage_4212[i][exynos_result_of_asv];
		} else if (soc_is_exynos4412()) {
			if (samsung_rev() >= EXYNOS4412_REV_2_0) {
				for (i = 0 ; i < CPUFREQ_LEVEL_END ; i++)
					exynos4x12_volt_table[i] =
						asv_voltage_step_12_5_rev2[i][exynos_result_of_asv];
			} else {
				for (i = 0 ; i < CPUFREQ_LEVEL_END ; i++)
					exynos4x12_volt_table[i] =
						asv_voltage_step_12_5[i][exynos_result_of_asv];
			}
		} else {
			pr_err("%s: Can't find SoC type \n", __func__);
		}
	}

	if (soc_is_exynos4412() && (samsung_rev() >= EXYNOS4412_REV_2_0)) {
		tmp = (is_special_flag() >> ARM_LOCK_FLAG) & 0x3;

		if (tmp) {
			pr_info("%s : special flag[%d]\n", __func__, tmp);
			switch (tmp) {
			case 1:
				/* 500MHz fixed volt */
				i = L11;
				break;
			case 2:
				/* 700MHz fixed volt */
				i = L9;
				break;
			case 3:
				/* 800MHz fixed volt */
				i = L8;
				break;
			default:
				break;
			}

			pr_info("ARM voltage locking at L%d\n", i);

			for (tmp = (i + 1) ; tmp < CPUFREQ_LEVEL_END ; tmp++) {
				exynos4x12_volt_table[tmp] =
					exynos4x12_volt_table[i];
				pr_info("CPUFREQ: L%d : %d\n", tmp, exynos4x12_volt_table[tmp]);
			}
		}

		if (exynos_dynamic_ema) {
			need_dynamic_ema = true;
			pr_info("%s: Dynamic EMA is enabled\n", __func__);
		}
	}
}

int exynos4x12_cpufreq_init(struct exynos_dvfs_info *info)
{
	int i;
	unsigned int tmp;
	unsigned long rate;

	set_volt_table();

	cpu_clk = clk_get(NULL, "armclk");
	if (IS_ERR(cpu_clk))
		return PTR_ERR(cpu_clk);

	moutcore = clk_get(NULL, "moutcore");
	if (IS_ERR(moutcore))
		goto err_moutcore;

	mout_mpll = clk_get(NULL, "mout_mpll");
	if (IS_ERR(mout_mpll))
		goto err_mout_mpll;

	rate = clk_get_rate(mout_mpll) / 1000;

	mout_apll = clk_get(NULL, "mout_apll");
	if (IS_ERR(mout_apll))
		goto err_mout_apll;

	for (i = L0; i <  CPUFREQ_LEVEL_END; i++) {

		exynos4x12_clkdiv_table[i].index = i;

		tmp = __raw_readl(EXYNOS4_CLKDIV_CPU);

		tmp &= ~(EXYNOS4_CLKDIV_CPU0_CORE_MASK |
			EXYNOS4_CLKDIV_CPU0_COREM0_MASK |
			EXYNOS4_CLKDIV_CPU0_COREM1_MASK |
			EXYNOS4_CLKDIV_CPU0_PERIPH_MASK |
			EXYNOS4_CLKDIV_CPU0_ATB_MASK |
			EXYNOS4_CLKDIV_CPU0_PCLKDBG_MASK |
			EXYNOS4_CLKDIV_CPU0_APLL_MASK |
			EXYNOS4_CLKDIV_CPU0_CORE2_MASK);

		if (soc_is_exynos4212()) {
			tmp |= ((clkdiv_cpu0_4212[i][0] << EXYNOS4_CLKDIV_CPU0_CORE_SHIFT) |
				(clkdiv_cpu0_4212[i][1] << EXYNOS4_CLKDIV_CPU0_COREM0_SHIFT) |
				(clkdiv_cpu0_4212[i][2] << EXYNOS4_CLKDIV_CPU0_COREM1_SHIFT) |
				(clkdiv_cpu0_4212[i][3] << EXYNOS4_CLKDIV_CPU0_PERIPH_SHIFT) |
				(clkdiv_cpu0_4212[i][4] << EXYNOS4_CLKDIV_CPU0_ATB_SHIFT) |
				(clkdiv_cpu0_4212[i][5] << EXYNOS4_CLKDIV_CPU0_PCLKDBG_SHIFT) |
				(clkdiv_cpu0_4212[i][6] << EXYNOS4_CLKDIV_CPU0_APLL_SHIFT) |
				(clkdiv_cpu0_4212[i][7] << EXYNOS4_CLKDIV_CPU0_CORE2_SHIFT));
		} else {
			tmp |= ((clkdiv_cpu0_4412[i][0] << EXYNOS4_CLKDIV_CPU0_CORE_SHIFT) |
				(clkdiv_cpu0_4412[i][1] << EXYNOS4_CLKDIV_CPU0_COREM0_SHIFT) |
				(clkdiv_cpu0_4412[i][2] << EXYNOS4_CLKDIV_CPU0_COREM1_SHIFT) |
				(clkdiv_cpu0_4412[i][3] << EXYNOS4_CLKDIV_CPU0_PERIPH_SHIFT) |
				(clkdiv_cpu0_4412[i][4] << EXYNOS4_CLKDIV_CPU0_ATB_SHIFT) |
				(clkdiv_cpu0_4412[i][5] << EXYNOS4_CLKDIV_CPU0_PCLKDBG_SHIFT) |
				(clkdiv_cpu0_4412[i][6] << EXYNOS4_CLKDIV_CPU0_APLL_SHIFT) |
				(clkdiv_cpu0_4412[i][7] << EXYNOS4_CLKDIV_CPU0_CORE2_SHIFT));
		}

		exynos4x12_clkdiv_table[i].clkdiv = tmp;

		tmp = __raw_readl(EXYNOS4_CLKDIV_CPU1);

		if (soc_is_exynos4212()) {
			tmp &= ~(EXYNOS4_CLKDIV_CPU1_COPY_MASK |
				EXYNOS4_CLKDIV_CPU1_HPM_MASK);
			tmp |= ((clkdiv_cpu1_4212[i][0] << EXYNOS4_CLKDIV_CPU1_COPY_SHIFT) |
				(clkdiv_cpu1_4212[i][1] << EXYNOS4_CLKDIV_CPU1_HPM_SHIFT));
		} else {
			tmp &= ~(EXYNOS4_CLKDIV_CPU1_COPY_MASK |
				EXYNOS4_CLKDIV_CPU1_HPM_MASK |
				EXYNOS4_CLKDIV_CPU1_CORES_MASK);
			tmp |= ((clkdiv_cpu1_4412[i][0] << EXYNOS4_CLKDIV_CPU1_COPY_SHIFT) |
				(clkdiv_cpu1_4412[i][1] << EXYNOS4_CLKDIV_CPU1_HPM_SHIFT) |
				(clkdiv_cpu1_4412[i][2] << EXYNOS4_CLKDIV_CPU1_CORES_SHIFT));
		}
		exynos4x12_clkdiv_table[i].clkdiv1 = tmp;
	}

	info->mpll_freq_khz = rate;
	info->pm_lock_idx = L5;
	info->pll_safe_idx = L7;
	info->max_support_idx = max_support_idx;
	info->min_support_idx = min_support_idx;
	info->cpu_clk = cpu_clk;
	info->volt_table = exynos4x12_volt_table;
	info->freq_table = exynos4x12_freq_table;
	info->set_freq = exynos4x12_set_frequency;
	info->need_apll_change = exynos4x12_pms_change;

#ifdef ENABLE_CLKOUT
	tmp = __raw_readl(EXYNOS4_CLKOUT_CMU_CPU);
	tmp &= ~0xffff;
	tmp |= 0x1904;
	__raw_writel(tmp, EXYNOS4_CLKOUT_CMU_CPU);

	tmp = __raw_readl(S5P_PMU_DEBUG);
	tmp &= ~0xf00;
	tmp |= 0x900;
	__raw_writel(tmp, S5P_PMU_DEBUG);

#endif

	return 0;

err_mout_apll:
	if (!IS_ERR(mout_mpll))
		clk_put(mout_mpll);
err_mout_mpll:
	if (!IS_ERR(moutcore))
		clk_put(moutcore);
err_moutcore:
	if (!IS_ERR(cpu_clk))
		clk_put(cpu_clk);

	pr_debug("%s: failed initialization\n", __func__);
	return -EINVAL;
}
EXPORT_SYMBOL(exynos4x12_cpufreq_init);

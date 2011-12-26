/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        AllWinner Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2007-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : cpu-freq-table.c
* By      : kevin.z
* Version : v1.0
* Date    : 2011-6-19 9:34
* Descript: sun4i cpu frequency table
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/

#include <linux/types.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include "cpu-freq.h"

struct cpufreq_frequency_table sun4i_freq_tbl[] = {

    { .frequency = 30000,   .index = SUN4I_CLK_DIV(1, 1, 1, 2), },
    { .frequency = 48000,   .index = SUN4I_CLK_DIV(1, 1, 1, 2), },
    { .frequency = 60000,   .index = SUN4I_CLK_DIV(1, 1, 1, 2), },
    { .frequency = 72000,   .index = SUN4I_CLK_DIV(1, 1, 1, 2), },
    { .frequency = 84000,   .index = SUN4I_CLK_DIV(1, 1, 1, 2), },
    { .frequency = 96000,   .index = SUN4I_CLK_DIV(1, 1, 1, 2), },
    { .frequency = 108000,  .index = SUN4I_CLK_DIV(1, 1, 1, 2), },
    { .frequency = 120000,  .index = SUN4I_CLK_DIV(1, 1, 1, 2), },
    { .frequency = 132000,  .index = SUN4I_CLK_DIV(1, 1, 1, 2), },
    { .frequency = 144000,  .index = SUN4I_CLK_DIV(1, 1, 1, 2), },
    { .frequency = 156000,  .index = SUN4I_CLK_DIV(1, 1, 1, 2), },
    { .frequency = 168000,  .index = SUN4I_CLK_DIV(1, 1, 1, 2), },
    { .frequency = 180000,  .index = SUN4I_CLK_DIV(1, 1, 1, 2), },
    { .frequency = 192000,  .index = SUN4I_CLK_DIV(1, 1, 1, 2), },
    { .frequency = 204000,  .index = SUN4I_CLK_DIV(1, 1, 2, 2), },
    { .frequency = 216000,  .index = SUN4I_CLK_DIV(1, 1, 2, 2), },
    { .frequency = 240000,  .index = SUN4I_CLK_DIV(1, 1, 2, 2), },
    { .frequency = 264000,  .index = SUN4I_CLK_DIV(1, 1, 2, 2), },
    { .frequency = 288000,  .index = SUN4I_CLK_DIV(1, 1, 2, 2), },
    { .frequency = 300000,  .index = SUN4I_CLK_DIV(1, 1, 2, 2), },
    { .frequency = 336000,  .index = SUN4I_CLK_DIV(1, 1, 2, 2), },
    { .frequency = 360000,  .index = SUN4I_CLK_DIV(1, 1, 2, 2), },
    { .frequency = 384000,  .index = SUN4I_CLK_DIV(1, 1, 2, 2), },
    { .frequency = 408000,  .index = SUN4I_CLK_DIV(1, 1, 2, 2), },
    { .frequency = 432000,  .index = SUN4I_CLK_DIV(1, 2, 2, 2), },
    { .frequency = 480000,  .index = SUN4I_CLK_DIV(1, 2, 2, 2), },
    { .frequency = 528000,  .index = SUN4I_CLK_DIV(1, 2, 2, 2), },
    { .frequency = 576000,  .index = SUN4I_CLK_DIV(1, 2, 2, 2), },
    { .frequency = 600000,  .index = SUN4I_CLK_DIV(1, 2, 2, 2), },
    { .frequency = 648000,  .index = SUN4I_CLK_DIV(1, 2, 2, 2), },
    { .frequency = 672000,  .index = SUN4I_CLK_DIV(1, 2, 2, 2), },
    { .frequency = 696000,  .index = SUN4I_CLK_DIV(1, 2, 2, 2), },
    { .frequency = 720000,  .index = SUN4I_CLK_DIV(1, 2, 2, 2), },
    { .frequency = 744000,  .index = SUN4I_CLK_DIV(1, 2, 2, 2), },
    { .frequency = 768000,  .index = SUN4I_CLK_DIV(1, 2, 2, 2), },
    { .frequency = 816000,  .index = SUN4I_CLK_DIV(1, 2, 2, 2), },
    { .frequency = 864000,  .index = SUN4I_CLK_DIV(1, 3, 2, 2), },
    { .frequency = 912000,  .index = SUN4I_CLK_DIV(1, 3, 2, 2), },
    { .frequency = 960000,  .index = SUN4I_CLK_DIV(1, 3, 2, 2), },
    { .frequency = 1008000, .index = SUN4I_CLK_DIV(1, 3, 2, 2), },
    #if(1)
    { .frequency = 1056000, .index = SUN4I_CLK_DIV(1, 3, 2, 2), },
    { .frequency = 1104000, .index = SUN4I_CLK_DIV(1, 3, 2, 2), },
    { .frequency = 1152000, .index = SUN4I_CLK_DIV(1, 3, 2, 2), },
    { .frequency = 1200000, .index = SUN4I_CLK_DIV(1, 3, 2, 2), },
    { .frequency = 1248000, .index = SUN4I_CLK_DIV(1, 4, 2, 2), },
    { .frequency = 1296000, .index = SUN4I_CLK_DIV(1, 4, 2, 2), },
    { .frequency = 1344000, .index = SUN4I_CLK_DIV(1, 4, 2, 2), },
    { .frequency = 1392000, .index = SUN4I_CLK_DIV(1, 4, 2, 2), },
    { .frequency = 1440000, .index = SUN4I_CLK_DIV(1, 4, 2, 2), },
    { .frequency = 1488000, .index = SUN4I_CLK_DIV(1, 4, 2, 2), },
    #endif

    /* table end */
    { .frequency = CPUFREQ_TABLE_END,  .index = 0,              },
};


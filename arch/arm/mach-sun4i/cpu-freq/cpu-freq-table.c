/*
 * arch/arm/mach-sun4i/cpu-freq/cpu-freq-table.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Kevin Zhang <kevin@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */


#include <linux/types.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include "cpu-freq.h"

static struct cpufreq_frequency_table sun4i_freq_tbl[] = {

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

/* div, pll (Hz) table */
static struct cpufreq_div_order sun4i_div_order_tbl[] = {
    { .div = SUN4I_CLK_DIV(1, 1, 1, 2), .pll = 204000000,  },
    { .div = SUN4I_CLK_DIV(1, 1, 2, 2), .pll = 408000000,  },
    { .div = SUN4I_CLK_DIV(1, 2, 2, 2), .pll = 816000000,  },
    { .div = SUN4I_CLK_DIV(1, 3, 2, 2), .pll = 1200000000, },
    { .div = SUN4I_CLK_DIV(1, 4, 2, 2), .pll = 1248000000, },
};

#ifdef CONFIG_CPU_FREQ_DVFS
static struct cpufreq_dvfs sun4i_dvfs_table[] = {
    {.freq = 1056000000, .volt = 1500}, /* core vdd is 1.50v if cpu frequency is (1008Mhz, xxxxMhz] */
    {.freq = 1008000000, .volt = 1400}, /* core vdd is 1.40v if cpu frequency is (960Mhz, 1008Mhz]  */
    {.freq = 960000000,  .volt = 1400}, /* core vdd is 1.40v if cpu frequency is (912Mhz, 960Mhz]   */
    {.freq = 912000000,  .volt = 1350}, /* core vdd is 1.35v if cpu frequency is (864Mhz, 912Mhz]   */
    {.freq = 864000000,  .volt = 1300}, /* core vdd is 1.30v if cpu frequency is (624Mhz, 864Mhz]   */
    {.freq = 624000000,  .volt = 1250}, /* core vdd is 1.25v if cpu frequency is (432Mhz, 624Mhz]   */
    {.freq = 432000000,  .volt = 1250}, /* core vdd is 1.25v if cpu frequency is (0, 432Mhz]        */
    {.freq = 0,          .volt = 1000}, /* end of cpu dvfs table                                    */
};
#endif

struct cpufreq_frequency_table * sunxi_cpufreq_table(void) {
    /* TODO: improve to handle A13 and others */
    return sun4i_freq_tbl;
}

struct cpufreq_div_order * sunxi_div_order_table(int *length) {
    /* TODO: improve to handle A13 and others */
    *length = ARRAY_SIZE(sun4i_div_order_tbl);
    return sun4i_div_order_tbl;
}

#ifdef CONFIG_CPU_FREQ_DVFS
struct cpufreq_dvfs * sunxi_dvfs_table(void) {
    /* TODO: improve to handle A13 and others */
    return sun4i_dvfs_table;
}
#endif

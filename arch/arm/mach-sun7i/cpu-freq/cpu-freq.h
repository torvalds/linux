/*
 *  arch/arm/mach-sun7i/cpu-freq/cpu-freq.h
 *
 * Copyright (c) 2012 Allwinner.
 * kevin.z.m (kevin@allwinnertech.com)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __sunxi_CPU_FREQ_H__
#define __sunxi_CPU_FREQ_H__

#include <linux/types.h>
#include <linux/cpufreq.h>

#undef CPUFREQ_DBG
#undef CPUFREQ_ERR
#if (0)
    #define CPUFREQ_DBG(format,args...)   printk("[cpu_freq] DBG:"format,##args)
#else
    #define CPUFREQ_DBG(format,args...)   do{}while(0)
#endif

#define CPUFREQ_INF(format,args...)   pr_info("[cpu_freq] INF:"format,##args)
#define CPUFREQ_ERR(format,args...)   pr_err("[cpu_freq] ERR:"format,##args)


#define SUNXI_CPUFREQ_MAX       (1008000000)    /* config the maximum frequency of sunxi core */
#define SUNXI_CPUFREQ_MIN       (60000000)      /* config the minimum frequency of sunxi core */
#define SUNXI_FREQTRANS_LATENCY (2000000)       /* config the transition latency, based on ns */

struct sunxi_clk_div_t {
    __u32   cpu_div:4;      /* division of cpu clock, divide core_pll */
    __u32   axi_div:4;      /* division of axi clock, divide cpu clock*/
    __u32   ahb_div:4;      /* division of ahb clock, divide axi clock*/
    __u32   apb_div:4;      /* division of apb clock, divide ahb clock*/
    __u32   reserved:16;
};


struct sunxi_cpu_freq_t {
    __u32                   pll;    /* core pll frequency value */
    struct sunxi_clk_div_t  div;    /* division configuration   */
};


#define SUNXI_CLK_DIV(cpu_div, axi_div, ahb_div, apb_div)       \
                ((cpu_div<<0)|(axi_div<<4)|(ahb_div<<8)|(apb_div<<12))

extern struct cpufreq_frequency_table sunxi_freq_tbl[];

#endif  /* #ifndef __sunxi_CPU_FREQ_H__ */



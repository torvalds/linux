/*
 * arch/arm/mach-sun4i/cpu-freq/cpu-freq.h
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

#ifndef __SUN4I_CPU_FREQ_H__
#define __SUN4I_CPU_FREQ_H__

#include <linux/types.h>
#include <linux/cpufreq.h>

#undef CPUFREQ_DBG
#undef CPUFREQ_ERR
#if (0)
    #define CPUFREQ_DBG(format,args...)   printk("[cpu_freq] DBG:"format,##args)
    #define CPUFREQ_ERR(format,args...)   printk("[cpu_freq] ERR:"format,##args)
    #define CPUFREQ_INF(format,args...)   printk("[cpu_freq] INF:"format,##args)
#else
    #define CPUFREQ_DBG(format,args...)   do{}while(0)
    #define CPUFREQ_ERR(format,args...)   do{}while(0)
    #define CPUFREQ_INF(format,args...)   do{}while(0)
#endif


/* Absolute minimum and maximum */
#define SUN4I_CPUFREQ_MAX       (1008000000)    /* config the maximum frequency of sun4i core */
#define SUN4I_CPUFREQ_MIN       (60000000)      /* config the minimum frequency of sun4i core */
/* Defaults limits for the scaling governor */
#define SUN4I_SCALING_MIN	(CONFIG_SUNXI_SCALING_MIN * 1000000)
#define SUN4I_SCALING_MAX	(1008000000)
#define SUN4I_FREQTRANS_LATENCY (2000000)       /* config the transition latency, based on ns */

struct sun4i_clk_div_t {
    __u32   cpu_div:4;      /* division of cpu clock, divide core_pll */
    __u32   axi_div:4;      /* division of axi clock, divide cpu clock*/
    __u32   ahb_div:4;      /* division of ahb clock, divide axi clock*/
    __u32   apb_div:4;      /* division of apb clock, divide ahb clock*/
    __u32   reserved:16;
};

struct sun4i_cpu_freq_t {
    __u32                   pll;    /* core pll frequency value */
    union {
        struct sun4i_clk_div_t s;    /* division configuration   */
        __u32 i;
    } div;
};

#define SUN4I_CLK_DIV(cpu_div, axi_div, ahb_div, apb_div)       \
                ((cpu_div<<0)|(axi_div<<4)|(ahb_div<<8)|(apb_div<<12))

#ifdef CONFIG_CPU_FREQ_DVFS
struct cpufreq_dvfs {
    unsigned int    freq;   /* cpu frequency    */
    unsigned int    volt;   /* voltage for the frequency    */
};
#endif

struct cpufreq_div_order {
    __u32 div;
    __u32 pll;
};

/* Table fetchers */
struct cpufreq_frequency_table *sunxi_cpufreq_table(void);
struct cpufreq_div_order *sunxi_div_order_table(int *length);
#ifdef CONFIG_CPU_FREQ_DVFS
struct cpufreq_dvfs *sunxi_dvfs_table(void);
#endif

#endif  /* #ifndef __SUN4I_CPU_FREQ_H__ */



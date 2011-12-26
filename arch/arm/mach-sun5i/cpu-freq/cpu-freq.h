/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        AllWinner Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : cpu-freq.h
* By      : kevin.z
* Version : v1.0
* Date    : 2011-6-19 13:16
* Descript: cpu frequency for sun4i;
* Update  : date                auther      ver     notes
*********************************************************************************************************
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


#define SUN4I_CPUFREQ_MAX       (1008000000)    /* config the maximum frequency of sun4i core */
#define SUN4I_CPUFREQ_MIN       (60000000)      /* config the minimum frequency of sun4i core */
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
    struct sun4i_clk_div_t  div;    /* division configuration   */
};


#define SUN4I_CLK_DIV(cpu_div, axi_div, ahb_div, apb_div)       \
                ((cpu_div<<0)|(axi_div<<4)|(ahb_div<<8)|(apb_div<<12))



extern struct cpufreq_frequency_table sun4i_freq_tbl[];

#endif  /* #ifndef __SUN4I_CPU_FREQ_H__ */



#ifndef _PM_H
#define _PM_H

/*
 * Copyright (c) 2011-2015 yanggq.young@allwinnertech.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

//#include "pm_types.h" 
#include "pm_config.h"
#include "pm_errcode.h"
#include "pm_debug.h"
#include "mem_cpu.h"
#include "mem_serial.h"
#include "mem_printk.h"
#include "mach/platform.h"
#include "mem_divlibc.h"
#include "mem_int.h"
#include "mem_tmr.h"
#include <mach/ccmu.h>
#include "mem_timing.h"
#include <linux/power/aw_pm.h>

#define AXP_IICBUS      (0)
#define TWI_CHECK_TIMEOUT       (0xf2ff)

#define PM_STANDBY_PRINT_STANDBY        (1U << 0)
#define PM_STANDBY_PRINT_RESUME         (1U << 1)
#define PM_STANDBY_PRINT_CACHE_TLB_MISS (1U << 2)
#define PM_STANDBY_PRINT_REG            (1U << 3)
#define PM_STANDBY_PRINT_CHECK_CRC            (1U << 4)

#ifdef CONFIG_ARCH_SUN4I
#define INT_REG_LENGTH	((0x90+0x4)>>2)
#define GPIO_REG_LENGTH	((0x218+0x4)>>2)
#define SRAM_REG_LENGTH	((0x94+0x4)>>2)
#elif defined CONFIG_ARCH_SUN5I
#define INT_REG_LENGTH	((0x94+0x4)>>2)
#define GPIO_REG_LENGTH	((0x218+0x4)>>2)
#define SRAM_REG_LENGTH	((0x94+0x4)>>2)
#elif defined CONFIG_ARCH_SUN6I
#define GPIO_REG_LENGTH	((0x278+0x4)>>2)
#define SRAM_REG_LENGTH	((0x94+0x4)>>2)
#define CCU_REG_LENGTH	((0x308+0x4)>>2)
#elif defined CONFIG_ARCH_SUN7I
#define GPIO_REG_LENGTH	((0x218+0x4)>>2)
#define SRAM_REG_LENGTH	((0x94+0x4)>>2)
#define CCU_REG_LENGTH	((0x1f4+0x4)>>2)
#define TMR_REG_LENGTH	((0x170+0x4)>>2)
#define TWI0_REG_LENGTH ((0x20+0x4)>>2)
#endif



struct clk_div_t {
    __u32   cpu_div:4;      /* division of cpu clock, divide core_pll */
    __u32   axi_div:4;      /* division of axi clock, divide cpu clock*/
    __u32   ahb_div:4;      /* division of ahb clock, divide axi clock*/
    __u32   apb_div:4;      /* division of apb clock, divide ahb clock*/
    __u32   reserved:16;
};
struct pll_factor_t {
    __u8    FactorN;
    __u8    FactorK;
    __u8    FactorM;
    __u8    FactorP;
    __u32   Pll;
};

struct mmu_state {
	/* CR0 */
	__u32 cssr;	/* Cache Size Selection */
	/* CR1 */
	__u32 cr;		/* Control */
	__u32 cacr;	/* Coprocessor Access Control */
	/* CR2 */
	__u32  ttb_0r;	/* Translation Table Base 0 */
	__u32  ttb_1r;	/* Translation Table Base 1 */
	__u32  ttbcr;	/* Translation Talbe Base Control */
	
	/* CR3 */
	__u32 dacr;	/* Domain Access Control */

	/*cr10*/
	__u32 prrr;	/* Primary Region Remap Register */
	__u32 nrrr;	/* Normal Memory Remap Register */
};

typedef struct _boot_dram_para_t
{
    unsigned int           dram_baseaddr;
    unsigned int           dram_clk;
    unsigned int           dram_type;
    unsigned int           dram_rank_num;
    unsigned int           dram_chip_density;
    unsigned int           dram_io_width;
    unsigned int		   dram_bus_width;
    unsigned int           dram_cas;
    unsigned int           dram_zq;
    unsigned int           dram_odt_en;
    unsigned int 		   dram_size;
    unsigned int           dram_tpr0;
    unsigned int           dram_tpr1;
    unsigned int           dram_tpr2;
    unsigned int           dram_tpr3;
    unsigned int           dram_tpr4;
    unsigned int           dram_tpr5;
    unsigned int 		   dram_emr1;
    unsigned int           dram_emr2;
    unsigned int           dram_emr3;
}standy_dram_para_t;

/**
*@brief struct of super mem
*/
struct aw_mem_para{
	void **resume_pointer;
	__u32 mem_flag;
	//__s32 suspend_vdd;
	__s32 suspend_dcdc2;
	__s32 suspend_dcdc3;
	__u32 suspend_freq;
	__u32 axp_enable;
	__u32 axp_event;
	__u32 sys_event;
	__u32 cpus_gpio_wakeup;
	__u32 debug_mask;
	__u32 suspend_delay_ms;
	__u32 saved_runtime_context_svc[RUNTIME_CONTEXT_SIZE];
	struct clk_div_t clk_div;
	struct pll_factor_t pll_factor;
	struct mmu_state saved_mmu_state;
//	struct saved_context saved_cpu_context;
    standy_dram_para_t      dram_para;
};

/**
*@brief struct of standby
*/
struct aw_standby_para{
	unsigned int event_enable;     /**<event type for system wakeup    */
	unsigned int event;          /**<event type for system wakeup    */
	unsigned int axp_src;        /**<axp event type for system wakeup    */
	unsigned int axp_enable;     /**<axp event type for system wakeup    */
	signed int   time_off;       /**<time to power off from now, based on second */
};


/**
*@brief struct of power management info
*/
struct aw_pm_info{
    struct aw_standby_para  standby_para;   /* standby parameter            */
    struct aw_pmu_arg       pmu_arg;        /**<args used by main function  */
    standy_dram_para_t      dram_para;
};


typedef  int (*super_standby_func)(void);
typedef  int (*normal_standby_func)(struct aw_pm_info *arg);

/*mem_mmu_pc_asm.S*/
extern unsigned int save_sp_nommu(void);
extern unsigned int save_sp(void);
extern void clear_reg_context(void);
extern void restore_sp(unsigned int sp);

//cache
extern void invalidate_dcache(void);
extern void flush_icache(void);
extern void flush_dcache(void);
extern void disable_cache(void);
extern void disable_dcache(void);
extern void disable_l2cache(void);
extern void enable_cache(void);
extern void enable_icache(void);

extern void disable_program_flow_prediction(void);
extern void invalidate_branch_predictor(void);
extern void enable_program_flow_prediction(void);

extern void mem_flush_tlb(void);
extern void mem_preload_tlb(void);
extern void mem_preload_tlb_nommu(void);

void disable_mmu(void);
void enable_mmu(void);

extern int jump_to_resume(void* pointer, __u32 *addr);
extern int jump_to_resume0(void* pointer);
void jump_to_suspend(__u32 ttbr1, super_standby_func p);
extern int jump_to_resume0_nommu(void* pointer);

/*mmu_pc.c*/
extern void save_mmu_state(struct mmu_state *saved_mmu_state);
extern void restore_mmu_state(struct mmu_state *saved_mmu_state);
void set_ttbr0(void);
extern void invalidate_dcache(void);

#endif /*_PM_H*/


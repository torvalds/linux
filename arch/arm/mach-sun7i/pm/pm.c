/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        AllWinner Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : pm.c
* By      : kevin.z
* Version : v1.0
* Date    : 2011-5-27 14:08
* Descript: power manager for allwinners chips platform.
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/

#include <linux/module.h>
#include <linux/suspend.h>
#include <linux/cpufreq.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/major.h>
#include <linux/device.h>
#include <asm/uaccess.h>
#include <asm/delay.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/tlbflush.h>
#include <linux/power/aw_pm.h>
#include <asm/mach/map.h>
#include <asm/cacheflush.h>
#include "pm.h"

#include <plat/sys_config.h>
#include <mach/system.h>

//#define CROSS_MAPPING_STANDBY

#define AW_PM_DBG   1
#undef PM_DBG
#if(AW_PM_DBG)
    #define PM_DBG(format,args...)   printk("[pm]"format,##args)
#else
    #define PM_DBG(format,args...)   do{}while(0)
#endif

#ifdef RETURN_FROM_RESUME0_WITH_NOMMU
#define PRE_DISABLE_MMU    //actually, mean ,prepare condition to disable mmu
#endif

#ifdef RETURN_FROM_RESUME0_WITH_MMU
#undef PRE_DISABLE_MMU
#endif

#ifdef WATCH_DOG_RESET
#define PRE_DISABLE_MMU    //actually, mean ,prepare condition to disable mmu
#endif

//#define VERIFY_RESTORE_STATUS

/* define major number for power manager */
#define AW_PMU_MAJOR    267

static int debug_mask = PM_STANDBY_PRINT_STANDBY | PM_STANDBY_PRINT_RESUME;

static int standby_axp_enable = 1;
static int standby_timeout = 0;

static int suspend_freq = SUSPEND_FREQ;

extern char *standby_bin_start;
extern char *standby_bin_end;

static void show_reg(unsigned long addr, int nbytes, const char *name);

#ifdef CONFIG_CPU_FREQ_USR_EVNT_NOTIFY
extern void cpufreq_user_event_notify(void);
#endif

static struct aw_pm_info standby_info = {
    .standby_para = {
        .event_enable  = SUSPEND_WAKEUP_SRC_EXINT,
        .axp_src = AXP_MEM_WAKEUP,
    },
    .pmu_arg = {
        .twi_port = 0,
        .dev_addr = 10,
    },
};

static volatile __u32   dogMode;

#ifdef GET_CYCLE_CNT
static int start = 0;
static int resume0_period = 0;
static int resume1_period = 0;

static int pm_start = 0;
static int invalidate_data_time = 0;
static int invalidate_instruct_time = 0;
static int before_restore_processor = 0;
static int after_restore_process = 0;
//static int restore_runtime_peroid = 0;

//late_resume timing
static int late_resume_start = 0;
static int backup_area_start = 0;
static int backup_area1_start = 0;
static int backup_area2_start = 0;
static int clk_restore_start = 0;
static int gpio_restore_start = 0;
static int twi_restore_start = 0;
static int int_restore_start = 0;
static int tmr_restore_start = 0;
static int sram_restore_start = 0;
static int late_resume_end = 0;
#endif

EXPORT_SYMBOL(pm_disable_watchdog);
EXPORT_SYMBOL(pm_enable_watchdog);

/*
*********************************************************************************************************
*                           aw_pm_valid
*
*Description: determine if given system sleep state is supported by the platform;
*
*Arguments  : state     suspend state;
*
*Return     : if the state is valid, return 1, else return 0;
*
*Notes      : this is a call-back function, registered into PM core;
*
*********************************************************************************************************
*/
static int aw_pm_valid(suspend_state_t state)
{
    if(!((state > PM_SUSPEND_ON) && (state < PM_SUSPEND_MAX))){
        PM_DBG("state (%d) invalid!\n", state);
        return 0;
    }

#ifdef GET_CYCLE_CNT
        // init counters:
        init_perfcounters (1, 0);
#endif

    return 1;

}


/*
*********************************************************************************************************
*                           aw_pm_begin
*
*Description: Initialise a transition to given system sleep state;
*
*Arguments  : state     suspend state;
*
*Return     : return 0 for process successed;
*
*Notes      : this is a call-back function, registered into PM core, and this function
*             will be called before devices suspened;
*********************************************************************************************************
*/
unsigned int backup_max_freq = 0;
unsigned int backup_min_freq = 0;

int aw_pm_begin(suspend_state_t state)
{
    struct cpufreq_policy *policy;

    PM_DBG("%d state begin:%d\n", state,debug_mask);

    //set freq max
#ifdef CONFIG_CPU_FREQ_USR_EVNT_NOTIFY
    //cpufreq_user_event_notify();
#endif
    
    backup_max_freq = 0;
    backup_min_freq = 0;
    policy = cpufreq_cpu_get(0);
    if (!policy)
    {
        PM_DBG("line:%d cpufreq_cpu_get failed!\n", __LINE__);
        goto out;
    }

    backup_max_freq = policy->max;
    backup_min_freq = policy->min;
    policy->user_policy.max= suspend_freq;
    policy->user_policy.min = suspend_freq;
    cpufreq_cpu_put(policy);
    cpufreq_update_policy(0);

    /*must init perfcounter, because delay_us and delay_ms is depandant perf counter*/
#ifndef GET_CYCLE_CNT
    backup_perfcounter();
    init_perfcounters (1, 0);
#endif

    if(unlikely(debug_mask&PM_STANDBY_PRINT_REG)){
        printk("before dev suspend , line:%d\n", __LINE__);
        show_reg(SW_VA_CCM_IO_BASE, (CCU_REG_LENGTH)*4, "ccu");
        show_reg(SW_VA_PORTC_IO_BASE, GPIO_REG_LENGTH*4, "gpio");
        show_reg(SW_VA_TIMERC_IO_BASE, TMR_REG_LENGTH*4, "timer");
        show_reg(SW_VA_TWI0_IO_BASE, TWI0_REG_LENGTH*4, "twi0");
        show_reg(SW_VA_SRAM_IO_BASE, SRAM_REG_LENGTH*4, "sram");
    }
    return 0;

out:
    return -1;
}


/*
*********************************************************************************************************
*                           aw_pm_prepare
*
*Description: Prepare the platform for entering the system sleep state.
*
*Arguments  : none;
*
*Return     : return 0 for process successed, and negative code for error;
*
*Notes      : this is a call-back function, registered into PM core, this function
*             will be called after devices suspended, and before device late suspend
*             call-back functions;
*********************************************************************************************************
*/
int aw_pm_prepare(void)
{
    PM_DBG("prepare\n");

    return 0;
}


/*
*********************************************************************************************************
*                           aw_pm_prepare_late
*
*Description: Finish preparing the platform for entering the system sleep state.
*
*Arguments  : none;
*
*Return     : return 0 for process successed, and negative code for error;
*
*Notes      : this is a call-back function, registered into PM core.
*             prepare_late is called before disabling nonboot CPUs and after
*              device drivers' late suspend callbacks have been executed;
*********************************************************************************************************
*/
int aw_pm_prepare_late(void)
{
    PM_DBG("prepare_late\n");

    return 0;
}


/*
*********************************************************************************************************
*                           aw_pm_enter
*
*Description: Enter the system sleep state;
*
*Arguments  : state     system sleep state;
*
*Return     : return 0 is process successed;
*
*Notes      : this function is the core function for platform sleep.
*********************************************************************************************************
*/
static int aw_pm_enter(suspend_state_t state)
{
//  asm volatile ("stmfd sp!, {r1-r12, lr}" );
    normal_standby_func standby;
    
    PM_DBG("enter state %d\n", state);

    if(unlikely(debug_mask&PM_STANDBY_PRINT_REG)){
        printk("after cpu suspend , line:%d\n", __LINE__);
        show_reg(SW_VA_CCM_IO_BASE, (CCU_REG_LENGTH)*4, "ccu");
        show_reg(SW_VA_PORTC_IO_BASE, GPIO_REG_LENGTH*4, "gpio");
        show_reg(SW_VA_TIMERC_IO_BASE, TMR_REG_LENGTH*4, "timer");
        show_reg(SW_VA_TWI0_IO_BASE, TWI0_REG_LENGTH*4, "twi0");
        show_reg(SW_VA_SRAM_IO_BASE, SRAM_REG_LENGTH*4, "sram");
    }

    standby_info.standby_para.axp_enable = standby_axp_enable;
    
        standby = (int (*)(struct aw_pm_info *arg))SRAM_FUNC_START;
        //move standby code to sram
        memcpy((void *)SRAM_FUNC_START, (void *)&standby_bin_start, (int)&standby_bin_end - (int)&standby_bin_start);
        /* config system wakeup evetn type */
        if(PM_SUSPEND_MEM == state || PM_SUSPEND_STANDBY == state){
            standby_info.standby_para.axp_src = AXP_MEM_WAKEUP;
        }else if(PM_SUSPEND_BOOTFAST == state){
            standby_info.standby_para.axp_src = AXP_BOOTFAST_WAKEUP;
        }
        standby_info.standby_para.event_enable = (SUSPEND_WAKEUP_SRC_EXINT | SUSPEND_WAKEUP_SRC_ALARM);

        if (standby_timeout != 0)
        {
            standby_info.standby_para.event_enable = (SUSPEND_WAKEUP_SRC_EXINT | SUSPEND_WAKEUP_SRC_ALARM | SUSPEND_WAKEUP_SRC_TIMEOFF);
            standby_info.standby_para.time_off = standby_timeout;
        }
        /* goto sram and run */
        standby(&standby_info);

    dogMode = pm_enable_watchdog();

    if(unlikely(debug_mask&PM_STANDBY_PRINT_REG)){
        printk("after cpu suspend , line:%d\n", __LINE__);
        show_reg(SW_VA_CCM_IO_BASE, (CCU_REG_LENGTH)*4, "ccu");
        show_reg(SW_VA_PORTC_IO_BASE, GPIO_REG_LENGTH*4, "gpio");
        show_reg(SW_VA_TIMERC_IO_BASE, TMR_REG_LENGTH*4, "timer");
        show_reg(SW_VA_TWI0_IO_BASE, TWI0_REG_LENGTH*4, "twi0");
        show_reg(SW_VA_SRAM_IO_BASE, SRAM_REG_LENGTH*4, "sram");
    }
//  asm volatile ("ldmfd sp!, {r1-r12, lr}" );
    return 0;
}


/*
*********************************************************************************************************
*                           aw_pm_wake
*
*Description: platform wakeup;
*
*Arguments  : none;
*
*Return     : none;
*
*Notes      : This function called when the system has just left a sleep state, right after
*             the nonboot CPUs have been enabled and before device drivers' early resume
*             callbacks are executed. This function is opposited to the aw_pm_prepare_late;
*********************************************************************************************************
*/
static void aw_pm_wake(void)
{
    PM_DBG("platform wakeup, wakesource is:0x%x\n", standby_info.standby_para.event);
}

/*
*********************************************************************************************************
*                           aw_pm_finish
*
*Description: Finish wake-up of the platform;
*
*Arguments  : none
*
*Return     : none
*
*Notes      : This function is called right prior to calling device drivers' regular suspend
*              callbacks. This function is opposited to the aw_pm_prepare function.
*********************************************************************************************************
*/
void aw_pm_finish(void)
{
    PM_DBG("platform wakeup finish\n");
}


/*
*********************************************************************************************************
*                           aw_pm_end
*
*Description: Notify the platform that system is in work mode now.
*
*Arguments  : none
*
*Return     : none
*
*Notes      : This function is called by the PM core right after resuming devices, to indicate to
*             the platform that the system has returned to the working state or
*             the transition to the sleep state has been aborted. This function is opposited to
*             aw_pm_begin function.
*********************************************************************************************************
*/
void aw_pm_end(void)
{
    struct cpufreq_policy *policy;

#ifndef GET_CYCLE_CNT
    #ifndef IO_MEASURE
            restore_perfcounter();
    #endif
#endif
    if (backup_max_freq != 0 && backup_min_freq != 0)
    {
        policy = cpufreq_cpu_get(0);
        if (!policy)
        {
            printk("cpufreq_cpu_get err! check it! aw_pm_end:%d\n", __LINE__);
            return;
        }
        
        policy->user_policy.max = backup_max_freq;
        policy->user_policy.min = backup_min_freq;
        cpufreq_cpu_put(policy);
        cpufreq_update_policy(0);
    }
    pm_disable_watchdog(dogMode);
    
    if(unlikely(debug_mask&PM_STANDBY_PRINT_REG)){
        printk("after dev suspend, line:%d\n", __LINE__);
        show_reg(SW_VA_CCM_IO_BASE, (CCU_REG_LENGTH)*4, "ccu");
        show_reg(SW_VA_PORTC_IO_BASE, GPIO_REG_LENGTH*4, "gpio");
        show_reg(SW_VA_TIMERC_IO_BASE, TMR_REG_LENGTH*4, "timer");
        show_reg(SW_VA_TWI0_IO_BASE, TWI0_REG_LENGTH*4, "twi0");
        show_reg(SW_VA_SRAM_IO_BASE, SRAM_REG_LENGTH*4, "sram");
    }

    PM_DBG("aw_pm_end!\n");
}


/*
*********************************************************************************************************
*                           aw_pm_recover
*
*Description: Recover platform from a suspend failure;
*
*Arguments  : none
*
*Return     : none
*
*Notes      : This function alled by the PM core if the suspending of devices fails.
*             This callback is optional and should only be implemented by platforms
*             which require special recovery actions in that situation.
*********************************************************************************************************
*/
void aw_pm_recover(void)
{
    PM_DBG("aw_pm_recover\n");
}


/*
    define platform_suspend_ops which is registered into PM core.
*/
static struct platform_suspend_ops aw_pm_ops = {
    .valid = aw_pm_valid,
    .begin = aw_pm_begin,
    .prepare = aw_pm_prepare,
    .prepare_late = aw_pm_prepare_late,
    .enter = aw_pm_enter,
    .wake = aw_pm_wake,
    .finish = aw_pm_finish,
    .end = aw_pm_end,
    .recover = aw_pm_recover,
};

static int dram_para_script_fetch(char *sub, u32 *val)
{
	if (script_parser_fetch("dram_para", sub, val, sizeof(int))) {
		pr_err("dram para %s fetch err\n", sub);
		return -1;
	}
	pr_debug("dram config [dram_para] [%s] : %d\n", sub, *val);
	return 0;
}

static int fetch_and_save_dram_para(standy_dram_para_t *pstandby_dram_para)
{
	int ret;

	ret = dram_para_script_fetch( "dram_baseaddr", &pstandby_dram_para->dram_baseaddr);
	if (ret)
		return -1;

	ret = dram_para_script_fetch( "dram_clk", &pstandby_dram_para->dram_clk);
	if (ret)
		return -1;

	ret = dram_para_script_fetch( "dram_type", &pstandby_dram_para->dram_type);
	if (ret)
		return -1;

	ret = dram_para_script_fetch( "dram_rank_num", &pstandby_dram_para->dram_rank_num);
	if (ret)
		return -1;

	ret = dram_para_script_fetch( "dram_chip_density", &pstandby_dram_para->dram_chip_density);
	if (ret)
		return -1;

	ret = dram_para_script_fetch( "dram_io_width", &pstandby_dram_para->dram_io_width);
	if (ret)
		return -1;

	ret = dram_para_script_fetch( "dram_bus_width", &pstandby_dram_para->dram_bus_width);
	if (ret)
		return -1;

	ret = dram_para_script_fetch( "dram_cas", &pstandby_dram_para->dram_cas);
	if (ret)
		return -1;

	ret = dram_para_script_fetch( "dram_zq", &pstandby_dram_para->dram_zq);
	if (ret)
		return -1;

	ret = dram_para_script_fetch( "dram_odt_en", &pstandby_dram_para->dram_odt_en);
	if (ret)
		return -1;

	ret = dram_para_script_fetch( "dram_size", &pstandby_dram_para->dram_size);
	if (ret)
		return -1;

	ret = dram_para_script_fetch( "dram_tpr0", &pstandby_dram_para->dram_tpr0);
	if (ret)
		return -1;

	ret = dram_para_script_fetch( "dram_tpr1", &pstandby_dram_para->dram_tpr1);
	if (ret)
		return -1;

	ret = dram_para_script_fetch( "dram_tpr2", &pstandby_dram_para->dram_tpr2);
	if (ret)
		return -1;

	ret = dram_para_script_fetch( "dram_tpr3", &pstandby_dram_para->dram_tpr3);
	if (ret)
		return -1;

	ret = dram_para_script_fetch( "dram_tpr4", &pstandby_dram_para->dram_tpr4);
	if (ret)
		return -1;
    
	ret = dram_para_script_fetch( "dram_tpr5", &pstandby_dram_para->dram_tpr5);
	if (ret)
		return -1;
    
	ret = dram_para_script_fetch( "dram_emr1", &pstandby_dram_para->dram_emr1);
	if (ret)
		return -1;
    
	ret = dram_para_script_fetch( "dram_emr2", &pstandby_dram_para->dram_emr2);
	if (ret)
		return -1;

	ret = dram_para_script_fetch( "dram_emr3", &pstandby_dram_para->dram_emr3);
	if (ret)
		return -1;

	return 0;
}

/*
*********************************************************************************************************
*                           aw_pm_init
*
*Description: initial pm sub-system for platform;
*
*Arguments  : none;
*
*Return     : result;
*
*Notes      :
*
*********************************************************************************************************
*/
static int __init aw_pm_init(void)
{
	if (fetch_and_save_dram_para(&standby_info.dram_para) != 0) {
		memset(&standby_info.dram_para, 0,
		       sizeof(standby_info.dram_para));
		pr_err("%s: fetch_and_save_dram_para err.\n", __func__);
	}

    suspend_set_ops(&aw_pm_ops);

    return 0;
}

/*
*********************************************************************************************************
*                           aw_pm_exit
*
*Description: exit pm sub-system on platform;
*
*Arguments  : none
*
*Return     : none
*
*Notes      :
*
*********************************************************************************************************
*/
static void __exit aw_pm_exit(void)
{
    PM_DBG("aw_pm_exit!\n");
    suspend_set_ops(NULL);
}


/*
 * dump a block of reg from around the given address
 */
static void show_reg(unsigned long addr, int nbytes, const char *name)
{
#if(AW_PM_DBG)
    int i, j;
    int nlines;
    u32 *p;

    printk("\n========%s: %#lx(%d)========\n", name, addr, nbytes);

    /*
     * round address down to a 32 bit boundary
     * and always dump a multiple of 32 bytes
     */
    p = (u32 *)(addr & ~(sizeof(u32) - 1));
    nbytes += (addr & (sizeof(u32) - 1));
    nlines = (nbytes + 31) / 32;

    for (i = 0; i < nlines; i++) {
        /*
         * just display low 16 bits of address to keep
         * each line of the dump < 80 characters
         */
        printk("%04lx ", (unsigned long)p & 0xffff);
        for (j = 0; j < 8; j++) {
            printk(" %08x", *p);
            ++p;
        }
        printk("\n");
    }
#endif
}




module_param_named(standby_axp_enable, standby_axp_enable, int, S_IRUGO | S_IWUSR | S_IWGRP);
module_param_named(standby_timeout, standby_timeout, int, S_IRUGO | S_IWUSR | S_IWGRP);
module_param_named(debug_mask, debug_mask, int, S_IRUGO | S_IWUSR | S_IWGRP);
module_param_named(suspend_freq, suspend_freq, int, S_IRUGO | S_IWUSR | S_IWGRP);
module_init(aw_pm_init);
module_exit(aw_pm_exit);

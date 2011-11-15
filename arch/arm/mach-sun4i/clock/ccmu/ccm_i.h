/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        AllWinner Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : ccm_i.h
* By      : kevin.z
* Version : v1.0
* Date    : 2011-5-13 18:50
* Descript:
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/
#ifndef __AW_CCMU_I_H__
#define __AW_CCMU_I_H__

#include <linux/kernel.h>
#include <mach/ccmu_regs.h>
#include <asm/io.h>

extern __ccmu_reg_list_t   *aw_ccu_reg;


#undef CCU_DBG
#undef CCU_ERR
#if (1)
    #define CCU_DBG(format,args...)   printk("[ccmu] "format,##args)
    #define CCU_ERR(format,args...)   printk("[ccmu] "format,##args)
#else
    #define CCU_DBG(...)
    #define CCU_ERR(...)
#endif


struct core_pll_factor_t {
    __u8    FactorN;
    __u8    FactorK;
    __u8    FactorM;
    __u8    FactorP;
};

extern int ccm_clk_get_pll_para(struct core_pll_factor_t *factor, __u64 rate);

#endif /* #ifndef __AW_CCMU_I_H__ */


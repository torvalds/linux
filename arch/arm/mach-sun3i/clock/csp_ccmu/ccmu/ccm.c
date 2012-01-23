/*
*******************************************************************************
*           				eBase
*                 the Abstract of Hardware
*
*
*              (c) Copyright 2006-2010, ALL WINNER TECH.
*           								All Rights Reserved
*
* File     :  D:\winners\eBase\eBSP\CSP\sun_20\HW_CCMU\ccm.c
* Date     :  2010/11/22 18:51
* By       :  Sam.Wu
* Version  :  V1.00
* Description :
* Update   :  date      author      version     notes
*******************************************************************************
*/
#include "ccm_i.h"


u32 g_virMemBaseCcm = 0;


#define _CCM_PHY_BASE   0X01C20000
#define _CCM_MEM_SZ     0X1000

__s32 CSP_CCM_init(void)
{

    g_virMemBaseCcm = (u32)ioremap(_CCM_PHY_BASE, _CCM_MEM_SZ);
    if(!g_virMemBaseCcm){
        return AW_CCMU_FAIL;
    }

    return AW_CCMU_OK;
}

__s32 CSP_CCM_exit( void )
{
    return AW_CCMU_OK;
}

u32 CSP_CCM_get_sys_clk_total_num( void )
{
    return (u32)CSP_CCM_SYS_CLK_TOTAL_NUM;
}

u32 CSP_CCM_get_mod_clk_total_num( void )
{
    return (u32)(CSP_CCM_MOD_CLK_TOTAL_NUM);
}


/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        newbie Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : super_clock.c
* By      : kevin.z
* Version : v1.0
* Date    : 2011-5-31 13:40
* Descript: ccmu process for platform mem;
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/

#include "super_i.h"


__u32   cpu_ms_loopcnt;
//==============================================================================
// CLOCK SET FOR SYSTEM STANDBY
//==============================================================================

/*
*********************************************************************************************************
*                                     mem_clk_plldisable
*
* Description: disable all pll except dram pll.
*
* Arguments  : none
*
* Returns    : 0;
*********************************************************************************************************
*/
__s32 mem_clk_plldisable(void)
{
    __ccmu_reg_list_t *CmuReg = (__ccmu_reg_list_t *)SW_VA_CCM_IO_BASE; 
    CmuReg->Pll1Ctl.PLLEn = 0;
    CmuReg->Pll2Ctl.PLLEn = 0;
    CmuReg->Pll3Ctl.PLLEn = 0;
    CmuReg->Pll4Ctl.PLLEn = 0;
  //  CmuReg->Pll5Ctl.PLLEn = 0;
    CmuReg->Pll6Ctl.PLLEn = 0;
    CmuReg->Pll7Ctl.PLLEn = 0;

    return 0;
}


/*
*********************************************************************************************************
*                                     mem_clk_pllenable
*
* Description: enable all pll except dram pll.
*
* Arguments  : none
*
* Returns    : 0;
*********************************************************************************************************
*/
__s32 mem_clk_pllenable(void)
{
    __ccmu_reg_list_t *CmuReg = (__ccmu_reg_list_t *)SW_VA_CCM_IO_BASE; 
    CmuReg->Pll1Ctl.PLLEn = 1;
    CmuReg->Pll2Ctl.PLLEn = 1;
    CmuReg->Pll3Ctl.PLLEn = 1;
    CmuReg->Pll4Ctl.PLLEn = 1;
    //CmuReg->Pll5Ctl.PLLEn = 1;
    CmuReg->Pll6Ctl.PLLEn = 1;
    CmuReg->Pll7Ctl.PLLEn = 1;

    return 0;
}


/*
*********************************************************************************************************
*                                     mem_clk_setdiv
*
* Description: switch core clock to 32k low osc.
*
* Arguments  : none
*
* Returns    : 0;
*********************************************************************************************************
*/
__s32 mem_clk_setdiv(struct clk_div_t *clk_div)
{
    __ccmu_reg_list_t *CmuReg = (__ccmu_reg_list_t *)SW_VA_CCM_IO_BASE; 

    if(!clk_div){
        return -1;
    }
    
    CmuReg->SysClkDiv.AXIClkDiv = clk_div->axi_div;
    CmuReg->SysClkDiv.AHBClkDiv = clk_div->ahb_div;
    CmuReg->SysClkDiv.APB0ClkDiv = clk_div->apb_div;
    
    return 0;
}

/*
*********************************************************************************************************
*                                     mem_clk_getdiv
*
* Description: switch core clock to 32k low osc.
*
* Arguments  : none
*
* Returns    : 0;
*********************************************************************************************************
*/
__s32 mem_clk_getdiv(struct clk_div_t  *clk_div)
{
    __ccmu_reg_list_t *CmuReg = (__ccmu_reg_list_t *)SW_VA_CCM_IO_BASE; 

    if(!clk_div){
        return -1;
    }
    
    clk_div->axi_div = CmuReg->SysClkDiv.AXIClkDiv;
    clk_div->ahb_div = CmuReg->SysClkDiv.AHBClkDiv;
    clk_div->apb_div = CmuReg->SysClkDiv.APB0ClkDiv;
    
    return 0;
}


/*
*********************************************************************************************************
*                                     mem_clk_set_pll_factor
*
* Description: set pll factor, target cpu freq is ?M hz
*
* Arguments  : none
*
* Returns    : 0;
*********************************************************************************************************
*/

__s32 mem_clk_set_pll_factor(struct pll_factor_t *pll_factor)
{
    __ccmu_reg_list_t *CmuReg = (__ccmu_reg_list_t *)SW_VA_CCM_IO_BASE; 

    CmuReg->Pll1Ctl.FactorK = pll_factor->FactorK;
    CmuReg->Pll1Ctl.FactorM = pll_factor->FactorM;
    CmuReg->Pll1Ctl.PLLDivP = pll_factor->FactorP;
    CmuReg->Pll1Ctl.FactorN = pll_factor->FactorN;
    
    //busy_waiting();
    
    return 0;
}

/*
*********************************************************************************************************
*                                     mem_clk_get_pll_factor
*
* Description: get pll factor
*
* Arguments  : none
*
* Returns    : 0;
*********************************************************************************************************
*/

__s32 mem_clk_get_pll_factor(struct pll_factor_t *pll_factor)
{
    __ccmu_reg_list_t *CmuReg = (__ccmu_reg_list_t *)SW_VA_CCM_IO_BASE; 
    
    pll_factor->FactorN = CmuReg->Pll1Ctl.FactorN;
    pll_factor->FactorK = CmuReg->Pll1Ctl.FactorK;
    pll_factor->FactorM = CmuReg->Pll1Ctl.FactorM;
    pll_factor->FactorP = CmuReg->Pll1Ctl.PLLDivP;
    
    //busy_waiting();
    
    return 0;
}


/*
*********************************************************************************************************
*                                     mem_clk_dramgating
*
* Description: gating dram clock.
*
* Arguments  : onoff    dram clock gating on or off;
*
* Returns    : 0;
*********************************************************************************************************
*/
void mem_clk_dramgating(int onoff)
{
    __ccmu_reg_list_t *CmuReg = (__ccmu_reg_list_t *)SW_VA_CCM_IO_BASE; 

    if(onoff){
        CmuReg->Pll5Ctl.OutputEn = 1;
    }
    else {
        CmuReg->Pll5Ctl.OutputEn = 0;
    }
}

/*
*********************************************************************************************************
*                                     mem_clk_dramgating
*
* Description: gating dram clock.
*
* Arguments  : onoff    dram clock gating on or off;
*
* Returns    : 0;
*********************************************************************************************************
*/
void mem_clk_dramgating_nommu(int onoff)
{
    __ccmu_reg_list_t *CmuReg = (__ccmu_reg_list_t *)SW_PA_CCM_IO_BASE; 

    if(onoff){
        CmuReg->Pll5Ctl.OutputEn = 1;
    }
    else {
        CmuReg->Pll5Ctl.OutputEn = 0;
    }
}



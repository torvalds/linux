/*
*******************************************************************************
*           				eBase
*                 the Abstract of Hardware
*
*
*              (c) Copyright 2006-2010, ALL WINNER TECH.
*           								All Rights Reserved
*
* File     :  d:\winners\eBase\eBSP\CSP\sun_20\HW_CCMU\ccmu_i.h
* Date     :  2010/11/12 16:18
* By       :  Sam.Wu
* Version  :  V1.00
* Description :  include dependent extern declarations and define constants
* Update   :  date      author      version     notes
*******************************************************************************
*/
#ifndef _CSP_CCMU_I_H_
#define _CSP_CCMU_I_H_

#include <linux/kernel.h>
#include <asm/io.h>

#include "../csp_ccm_para.h"
#include "../csp_ccm_ops.h"

extern u32 g_virMemBaseCcm;
//#define CCM_MOD_VIR_BASE g_virMemBaseCcm;
#define CCM_MOD_VIR_BASE g_virMemBaseCcm

#define AW_CCMU_OK    0
#define AW_CCMU_FAIL  -1

#ifndef CCM_CONSTANS
#define CCM_CONSTANS


#define CLK_STATUS_ON   1
#define CLK_STATUS_OFF  0


#endif //#ifndef CCM_CONSTANS

/*********************************************************************
* TypeName	 :    		CSP_CCM_PLL_t
* Description: when set
* Members :
    @ s32  byPassIsAble: only CSP_CCM_CLK_PLL_CORE/CSP_CCM_CLK_PLL_VEDIO_0/CSP_CCM_CLK_PLL_VEDIO_0 has this field
* Note   : 1)if pllId == CSP_CCM_CLK_PLL_AUDIO, set freqHigh=0/1 to select output frequency 22.5792/24.576 MHz,
           2)if pllId == CSP_CCM_CLK_PLL_CORE || pllId == CSP_CCM_CLK_PLL_VE ,
                a) bypass enabled , factor must be 1/2/4/8 and ouPutFreq = 24M/factor.
                b) bypass disabled, outPutFreq = 6MHz*(factor + 1) + 24MHz
           3)if pllId == CSP_CCM_CLK_PLL_SDRAM, ouPutFreq = 12MHz * (factor + 3) + 24MHz
           4)if pllId == CSP_CCM_CLK_PLL_VEDIO_0/1, ouPutFreq = 12MHz * (factor + 3) + 24MHz
*********************************************************************/
typedef struct _CcmPllPara{
    CSP_CCM_sysClkNo_t pllId;//should be CSP_CCM_CLK_PLL_xxx, see type CSP_CCM_clkNo_t
    //s32          clkIsOn;//PLL should not be off, or CPU will oFF!!!
    u8 biasCurrent;       //set to 0 is OK in most cases
    union{
        u8   freqHigh;//if it is Audio PLL, set 0/1 to select output frequency 22.5792/24.576 MHz
        struct{
            u8      factor;
            u8      enableBypass;//PLL core/VE/SDRAM needed
        }outPutPara;
    }para;
}CcmPllPara_t;


extern u32 _get_HOSC_rate(void);
extern u32 _get_LOSC_rate(void);



#include <stdarg.h>
#define CCM_inf(...)  do{\
    if (NULL != printk){\
    printk(__VA_ARGS__);}\
}while(0)

#define CCM_msg(...)  do{\
    if (NULL != printk){\
    printk("L%d(%s):", __LINE__, __FILE__);\
    printk(__VA_ARGS__);}\
}while(0)

#define  CCM_CHECK_NULL_PARA(hdl, retVal)  do{\
    if(NULL == hdl){CCM_msg("NULL Para!!\n"); return retVal;}\
}while(0)


extern char* g_sysClkName[];

#endif //#ifndef _CSP_CCMU_I_H_






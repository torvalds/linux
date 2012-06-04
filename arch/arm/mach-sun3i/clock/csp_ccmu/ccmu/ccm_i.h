/*
 * arch/arm/mach-sun3i/clock/csp_ccmu/ccmu/ccm_i.h
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






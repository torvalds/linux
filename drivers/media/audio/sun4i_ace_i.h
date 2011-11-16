/*
 * drivers\media\audio\sun4i_ace_i.h
 * (C) Copyright 2007-2011
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * huangxin <huangxin@allwinnertech.com>
 *
 * some simple description for this code
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */
#ifndef _SUN4I_ACE_I_H_
#define	_SUN4I_ACE_I_H_

#include "sun4i_drv_ace.h"

#define writeReg(nReg, nVal)    \
            do{*(volatile __u32 *)(nReg) = (nVal);}while(0)
#define readReg(nReg)   \
            *(volatile __u32 *)(nReg)
#define sys_get_wvalue(n)   (*((volatile __u32 *)(n)))          /* word input */
#define sys_put_wvalue(n,c) (*((volatile __u32 *)(n))  = (c))   /* word output */

#define ACE_REGS_pBASE    	(0x01c1a000)

extern void *       ace_hsram;

/*ACE register offset*/
#define ACE_MODE_SELECTOR_OFF   	      	0x00
#define ACE_ACE_RESET_OFF   	          	0x04

/*AE register offset*/
#define AE_INT_EN_REG_OFF             (0x100+0x08)
#define AE_STATUS_REG_OFF             (0x100+0x24)

/*ACE register address*/
#define ACE_MODE_SELECTOR     	(ace_hsram + ACE_MODE_SELECTOR_OFF)
#define ACE_ACE_RESET         	(ace_hsram + ACE_ACE_RESET_OFF)

/*AE register address*/
#define AE_INT_EN_REG        	(ace_hsram + AE_INT_EN_REG_OFF)
#define AE_STATUS_REG	        (ace_hsram + AE_STATUS_REG_OFF)

#define ACE_ENGINE_MAX_FREQ      150000000            //150M

//ace reset mask
#define ACE_RESET_MASK                      (1<<0)
//ace reset mode
#define ACE_RESET_DISABLE                   (0)
#define ACE_RESET_ENABLE                    (1)

//ace ce enable mask
#define ACE_CE_ENABLE_MASK                  (1<<4)
//ace ae enable mask
#define ACE_AE_ENABLE_MASK                  (1<<0)

typedef struct __ACE_MODULE_USE_STAT{
    __ace_module_type_e ceUseCnt;
    __ace_module_type_e aeUseCnt;
}__ace_module_use_stat_t;

//ace module enable mode
#define ACE_MODULE_DISABLE                      (0)
#define ACE_MODULE_ENABLE                       (1)

#define CSP_CCM_SYS_CLK_SDRAM_PLL  1

#define CLK_OFF 0
#define CLK_ON  1

#define ACE_FAIL    -1
#define ACE_OK       0

#define CLK_CMD_SCLKCHG_REQ  2
#define CLK_CMD_SCLKCHG_DONE 3

#define CSP_CCM_MOD_CLK_ACE  "ace"
#define CSP_CCM_MOD_CLK_AHB_ACE "ahb_ace"
#define CSP_CCM_MOD_CLK_SDRAM_ACE "sdram_ace"

#define esCLK_OpenMclk(a)  clk_get(NULL, (a))
#define esCLK_CloseMclk      clk_put
#define esCLK_GetMclkSrc     clk_get_parent

#define esCLK_GetSrcFreq    clk_get_rate
#define esCLK_SetMclkDiv    clk_set_rate
#define esCLK_MclkRegCb(a, b)   NULL
#define esCLK_MclkUnregCb(a, b) NULL
#define esCLK_MclkReset(a, b)   clk_reset(a, b)

#endif	/* _ACE_I_H_ */

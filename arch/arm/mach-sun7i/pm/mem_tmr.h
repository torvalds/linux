/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        AllWinner Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2011-2015, gq.yang China
*                                             All Rights Reserved
*
* File    : mem_tmr.h
* By      : gq.yang
* Version : v1.0
* Date    : 2012-11-31 15:23
* Descript:
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/
#ifndef __MEM_TMR_H__
#define __MEM_TMR_H__

#include "pm_types.h" 

typedef struct __MEM_TMR_REG
{
	// offset:0x00
	volatile __u32   IntCtl;
	volatile __u32   IntSta;
	volatile __u32   reserved0[2];
	// offset:0x10
	volatile __u32   Tmr0Ctl;
	volatile __u32   Tmr0IntVal;
	volatile __u32   Tmr0CntVal;
	volatile __u32   reserved1;
	// offset:0x20
	volatile __u32   Tmr1Ctl;
	volatile __u32   Tmr1IntVal;
	volatile __u32   Tmr1CntVal;
	volatile __u32   reserved2;
	// offset:0x30
	volatile __u32   Tmr2Ctl;
	volatile __u32   Tmr2IntVal;
	volatile __u32   Tmr2CntVal;
	volatile __u32   reserved3;
	// offset:0x40
	volatile __u32   Tmr3Ctl;
	volatile __u32   Tmr3IntVal;
	volatile __u32   reserved4[2];
	// offset:0x50
	volatile __u32   Tmr4Ctl;
	volatile __u32   Tmr4IntVal;
	volatile __u32   Tmr4CntVal;
	volatile __u32   reserved5;
	// offset:0x60
	volatile __u32   Tmr5Ctl;
	volatile __u32   Tmr5IntVal;
	volatile __u32   Tmr5CntVal;
	volatile __u32   reserved6[5];
	// offset:0x80
	volatile __u32   AvsCtl;
	volatile __u32   Avs0Cnt;
	volatile __u32   Avs1Cnt;
	volatile __u32   AvsDiv;

	// offset:0x90
	volatile __u32	 DogCtl;
	volatile __u32	 DogMode;
	volatile __u32	 reserved7[2];

} __mem_tmr_reg_t;

struct tmr_state{
	__mem_tmr_reg_t  *TmrReg;
	__u32 TmrIntCtl, Tmr0Ctl, Tmr0IntVal, Tmr0CntVal, Tmr1Ctl, Tmr1IntVal, Tmr1CntVal;
};
__s32 mem_tmr_save(struct tmr_state *ptmr_state);
__s32 mem_tmr_restore(struct tmr_state *ptmr_state);
__u32 pm_enable_watchdog(void);
void pm_disable_watchdog(__u32 dogMode);

#endif  //__MEM_TMR_H__

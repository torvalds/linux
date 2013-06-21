/*
*********************************************************************************************************
*                                                    eMOD
*                                   the Easy Portable/Player Operation System
*                                            power manager sub-system
*
*                                     (c) Copyright 2008-2009, kevin.z China
*                                              All Rights Reserved
*
* File   : super_tmr.c
* Version: V1.0
* By     : kevin.z
* Date   : 2009-7-22 18:31
*********************************************************************************************************
*/
#include "super_i.h"

/*
*********************************************************************************************************
*                           mem_tmr_disable_watchdog
*
*Description: disable watch-dog.
*
*Arguments  : none.
*
*Return     : none;
*
*Notes      :
*
*********************************************************************************************************
*/
void mem_tmr_disable_watchdog(void)
{
    __mem_tmr_reg_t  *TmrReg = (__mem_tmr_reg_t *)SW_VA_TIMERC_IO_BASE;
	/* set timer register base */
	//TmrReg = (__mem_tmr_reg_t *)SW_VA_TIMERC_IO_BASE;
	/* disable watch-dog reset */
	TmrReg->DogMode &= ~(1<<1);
	/* disable watch-dog */
	TmrReg->DogMode &= ~(1<<0);
	
	return;
}

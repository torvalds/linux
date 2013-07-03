#include <plat/hardware.h>
#include <plat/platform.h>
#include "mem_tmr.h"

/*
*********************************************************************************************************
*                                     TIMER save
*
* Description: save timer for mem.
*
* Arguments  : none
*
* Returns    : EPDK_TRUE/EPDK_FALSE;
*********************************************************************************************************
*/
__s32 mem_tmr_save(struct tmr_state *ptmr_state)
{
	__mem_tmr_reg_t  *TmrReg;
	/* set timer register base */
	ptmr_state->TmrReg = TmrReg = (__mem_tmr_reg_t *)IO_ADDRESS(SW_PA_TIMERC_IO_BASE);
	
	/* backup timer registers */
	ptmr_state->TmrIntCtl   = TmrReg->IntCtl;
	ptmr_state->Tmr0Ctl     = TmrReg->Tmr0Ctl;
	ptmr_state->Tmr0IntVal  = TmrReg->Tmr0IntVal;
	ptmr_state->Tmr0CntVal  = TmrReg->Tmr0CntVal;
	ptmr_state->Tmr1Ctl     = TmrReg->Tmr1Ctl;
	ptmr_state->Tmr1IntVal  = TmrReg->Tmr1IntVal;
	ptmr_state->Tmr1CntVal  = TmrReg->Tmr1CntVal;
	
	return 0;
}


/*
*********************************************************************************************************
*                                     TIMER restore
*
* Description: restore timer for mem.
*
* Arguments  : none
*
* Returns    : EPDK_TRUE/EPDK_FALSE;
*********************************************************************************************************
*/
__s32 mem_tmr_restore(struct tmr_state *ptmr_state)
{
	__mem_tmr_reg_t  *TmrReg;

	/* set timer register base */
	TmrReg = ptmr_state->TmrReg;
	/* restore timer0 parameters */
	TmrReg->Tmr0IntVal  = ptmr_state->Tmr0IntVal;
	TmrReg->Tmr0CntVal  = ptmr_state->Tmr0CntVal;
	TmrReg->Tmr0Ctl     = ptmr_state->Tmr0Ctl;
	TmrReg->Tmr1IntVal  = ptmr_state->Tmr1IntVal;
	TmrReg->Tmr1CntVal  = ptmr_state->Tmr1CntVal;
	TmrReg->Tmr1Ctl     = ptmr_state->Tmr1Ctl;
	TmrReg->IntCtl      = ptmr_state->TmrIntCtl;
	
	return 0;
}

/*
*********************************************************************************************************
*                                     enable watchdog
*
* Description: enable watchdog.
*
* Arguments  : none
*
* Returns    : none;
*********************************************************************************************************
*/
#define MEM_WATCHDOG_ENABLE_MASK 1
#define PM_WATCHDOG_ENABLE 1

#ifdef PM_WATCHDOG_ENABLE
__u32 pm_enable_watchdog(void)
{
	__mem_tmr_reg_t  *TmrReg;
    volatile __u32   dogMode;
	/* set timer register base */
	TmrReg = (__mem_tmr_reg_t *)IO_ADDRESS(SW_PA_TIMERC_IO_BASE);
    dogMode = TmrReg->DogMode;
    
    /* set watch-dog reset, timeout is 10 seconds */
    TmrReg->DogMode = (8<<3) | (1<<1);
    /* enable watch-dog */
    TmrReg->DogMode |= MEM_WATCHDOG_ENABLE_MASK;
    TmrReg->DogCtl = 1;  /*restart watchdog*/
    return dogMode;
}

void pm_feed_watchdog(void)
{
	__mem_tmr_reg_t  *TmrReg;
	/* set timer register base */
	TmrReg = (__mem_tmr_reg_t *)IO_ADDRESS(SW_PA_TIMERC_IO_BASE);
    TmrReg->DogCtl = 1;  /*restart watchdog*/
}

/*
*********************************************************************************************************
*                                     enable watchdog
*
* Description: enable watchdog.
*
* Arguments  : none
*
* Returns    : none;
*********************************************************************************************************
*/
void pm_disable_watchdog(__u32 dogMode)
{
	__mem_tmr_reg_t  *TmrReg;
	/* set timer register base */
	TmrReg = (__mem_tmr_reg_t *)IO_ADDRESS(SW_PA_TIMERC_IO_BASE);
    if (dogMode)
    {
        TmrReg->DogMode = dogMode;
        if (dogMode & MEM_WATCHDOG_ENABLE_MASK)
        {
            TmrReg->DogCtl = 1;  /*restart watchdog*/
        }
    }
    else
    {
        /* disable watch-dog reset */
        TmrReg->DogMode &= ~(1<<1);
        /* disable watch-dog */
        TmrReg->DogMode &= ~(1<<0);
    }
}
#else
__u32 pm_enable_watchdog(void)
{
    return 0;
}

void pm_feed_watchdog(void)
{
}

/*
*********************************************************************************************************
*                                     enable watchdog
*
* Description: enable watchdog.
*
* Arguments  : none
*
* Returns    : none;
*********************************************************************************************************
*/
void pm_disable_watchdog(__u32 dogMode)
{
}
#endif

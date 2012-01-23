/*
*********************************************************************************************************
*											        eBase
*						                the Abstract of Hardware
*
*
*						        (c) Copyright 2006-2010, holigun China
*											All	Rights Reserved
*
* File    	: 	ebase_critical.h
* Date	:	2010-09-25
* By      	: 	holigun
* Version 	: 	V1.00
*********************************************************************************************************
*/
#ifndef	__EBASE_CRITICAL_H__
#define	__EBASE_CRITICAL_H__

#include "ebase_sw_platform.h"
#if 0
static void ebase_init_critical( EBSP_CPSR_REG * p_cpsr )
{
	*p_cpsr = 0;
}
#endif
#if	(EBASE_SW_PLATFORM == EBASE_SW_PLATFORM_MELIS)
static EBSP_CPSR_REG ebase_enter_critical( void )
{
	u32 temp;
	u32 old;

#ifdef ARM_GCC_COMPLIER
  __asm__ __volatile__("mrs %0, cpsr\n"
                  "orr %1, %0, #0x80\n"
                  "msr cpsr_c, %1"
                  : "=r" (old), "=r" (temp)
                  :
                  : "memory");
#else
	__asm{
		MRS		temp , CPSR
	};
	old = temp;
	__asm{
		ORR		temp , temp , #0x80
		MSR		CPSR_c , temp
	};
#endif

	return old;
}

static void ebase_exit_critical(EBSP_CPSR_REG  sr)
{
	u32 temp = sr;

	//--FIXME--这种做法不好，应该直接读取，然后把I位清理掉
#ifdef ARM_GCC_COMPLIER
  __asm__ __volatile__("mrs %0, cpsr\n"
                  : "=r" (temp)
                  :
                  : "memory");
#else
	__asm{
		MSR		CPSR_c , temp
	};
#endif

}

#elif(EBASE_SW_PLATFORM == EBASE_SW_PLATFORM_WDK)
extern unsigned int ebsp_enter_critical_for_ce6(void);
extern ebsp_exit_critical_for_ce6(unsigned int  sr);

static EBSP_CPSR_REG ebase_enter_critical( void )
{
	unsigned int old;

	old = ebsp_enter_critical_for_ce6();

	return old;
}


static void ebase_exit_critical(unsigned int  sr)
{
	ebsp_exit_critical_for_ce6(sr);

}
#elif(EBASE_SW_PLATFORM == EBASE_SW_PLATFORM_LDK)

#else

#endif



#ifdef	ENTER_CRITICAL
#undef	ENTER_CRITICAL
#endif

#ifdef	EXIT_CRITICAL
#undef	EXIT_CRITICAL
#endif

#define	INIT_CRITICAL(cpu_sr)	{cpu_sr = 0;}
#define ENTER_CRITICAL(cpu_sr) 	{cpu_sr = ebase_enter_critical();}    /* enter critical area                     */
#define EXIT_CRITICAL(cpu_sr)  	{ebase_exit_critical(cpu_sr);}    /* exit critical area                      */

#endif	//__EBASE_CRITICAL_H__

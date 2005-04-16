/******************************************************************************
 *
 *	(C)Copyright 1998,1999 SysKonnect,
 *	a business unit of Schneider & Koch & Co. Datensysteme GmbH.
 *
 *	See the file "skfddi.c" for further information.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

/*
 * Timer Driver for FBI board (timer chip 82C54)
 */

/*
 * Modifications:
 *
 *	28-Jun-1994 sw	Edit v1.6.
 *			MCA: Added support for the SK-NET FDDI-FM2 adapter. The
 *			 following functions have been added(+) or modified(*):
 *			 hwt_start(*), hwt_stop(*), hwt_restart(*), hwt_read(*)
 */

#include "h/types.h"
#include "h/fddi.h"
#include "h/smc.h"

#ifndef	lint
static const char ID_sccs[] = "@(#)hwt.c	1.13 97/04/23 (C) SK " ;
#endif

/*
 * Prototypes of local functions.
 */
/* 28-Jun-1994 sw - Note: hwt_restart() is also used in module 'drvfbi.c'. */
/*static void hwt_restart() ; */

/************************
 *
 *	hwt_start
 *
 *	Start hardware timer (clock ticks are 16us).
 *
 *	void hwt_start(
 *		struct s_smc *smc,
 *		u_long time) ;
 * In
 *	smc - A pointer to the SMT Context structure.
 *
 *	time - The time in units of 16us to load the timer with.
 * Out
 *	Nothing.
 *
 ************************/
#define	HWT_MAX	(65000)

void hwt_start(struct s_smc *smc, u_long time)
{
	u_short	cnt ;

	if (time > HWT_MAX)
		time = HWT_MAX ;

	smc->hw.t_start = time ;
	smc->hw.t_stop = 0L ;

	cnt = (u_short)time ;
	/*
	 * if time < 16 us
	 *	time = 16 us
	 */
	if (!cnt)
		cnt++ ;
#ifndef	PCI
	/*
	 * 6.25MHz -> CLK0 : T0 (cnt0 = 16us)	-> OUT0
	 *    OUT0 -> CLK1 : T1 (cnt1)	OUT1	-> ISRA(IS_TIMINT)
	 */
	OUT_82c54_TIMER(3,1<<6 | 3<<4 | 0<<1) ;	/* counter 1, mode 0 */
	OUT_82c54_TIMER(1,cnt & 0xff) ;		/* LSB */
	OUT_82c54_TIMER(1,(cnt>>8) & 0xff) ;	/* MSB */
	/*
	 * start timer by switching counter 0 to mode 3
	 *	T0 resolution 16 us (CLK0=0.16us)
	 */
	OUT_82c54_TIMER(3,0<<6 | 3<<4 | 3<<1) ;	/* counter 0, mode 3 */
	OUT_82c54_TIMER(0,100) ;		/* LSB */
	OUT_82c54_TIMER(0,0) ;			/* MSB */
#else	/* PCI */
	outpd(ADDR(B2_TI_INI), (u_long) cnt * 200) ;	/* Load timer value. */
	outpw(ADDR(B2_TI_CRTL), TIM_START) ;		/* Start timer. */
#endif	/* PCI */
	smc->hw.timer_activ = TRUE ;
}

/************************
 *
 *	hwt_stop
 *
 *	Stop hardware timer.
 *
 *	void hwt_stop(
 *		struct s_smc *smc) ;
 * In
 *	smc - A pointer to the SMT Context structure.
 * Out
 *	Nothing.
 *
 ************************/
void hwt_stop(struct s_smc *smc)
{
#ifndef PCI
	/* stop counter 0 by switching to mode 0 */
	OUT_82c54_TIMER(3,0<<6 | 3<<4 | 0<<1) ;	/* counter 0, mode 0 */
	OUT_82c54_TIMER(0,0) ;			/* LSB */
	OUT_82c54_TIMER(0,0) ;			/* MSB */
#else	/* PCI */
	outpw(ADDR(B2_TI_CRTL), TIM_STOP) ;
	outpw(ADDR(B2_TI_CRTL), TIM_CL_IRQ) ;
#endif	/* PCI */

	smc->hw.timer_activ = FALSE ;
}

/************************
 *
 *	hwt_init
 *
 *	Initialize hardware timer.
 *
 *	void hwt_init(
 *		struct s_smc *smc) ;
 * In
 *	smc - A pointer to the SMT Context structure.
 * Out
 *	Nothing.
 *
 ************************/
void hwt_init(struct s_smc *smc)
{
	smc->hw.t_start = 0 ;
	smc->hw.t_stop	= 0 ;
	smc->hw.timer_activ = FALSE ;

	hwt_restart(smc) ;
}

/************************
 *
 *	hwt_restart
 *
 *	Clear timer interrupt.
 *
 *	void hwt_restart(
 *		struct s_smc *smc) ;
 * In
 *	smc - A pointer to the SMT Context structure.
 * Out
 *	Nothing.
 *
 ************************/
void hwt_restart(struct s_smc *smc)
{
	hwt_stop(smc) ;
#ifndef	PCI
	OUT_82c54_TIMER(3,1<<6 | 3<<4 | 0<<1) ;	/* counter 1, mode 0 */
	OUT_82c54_TIMER(1,1 ) ;			/* LSB */
	OUT_82c54_TIMER(1,0 ) ;			/* MSB */
#endif
}

/************************
 *
 *	hwt_read
 *
 *	Stop hardware timer and read time elapsed since last start.
 *
 *	u_long hwt_read(smc) ;
 * In
 *	smc - A pointer to the SMT Context structure.
 * Out
 *	The elapsed time since last start in units of 16us.
 *
 ************************/
u_long hwt_read(struct s_smc *smc)
{
	u_short	tr ;
#ifndef	PCI
	u_short	is ;
#else
	u_long	is ;
#endif

	if (smc->hw.timer_activ) {
		hwt_stop(smc) ;
#ifndef	PCI
		OUT_82c54_TIMER(3,1<<6) ;	/* latch command */
		tr = IN_82c54_TIMER(1) & 0xff ;
		tr += (IN_82c54_TIMER(1) & 0xff)<<8 ;
#else	/* PCI */
		tr = (u_short)((inpd(ADDR(B2_TI_VAL))/200) & 0xffff) ;
#endif	/* PCI */
		is = GET_ISR() ;
		/* Check if timer expired (or wraparound). */
		if ((tr > smc->hw.t_start) || (is & IS_TIMINT)) {
			hwt_restart(smc) ;
			smc->hw.t_stop = smc->hw.t_start ;
		}
		else
			smc->hw.t_stop = smc->hw.t_start - tr ;
	}
	return (smc->hw.t_stop) ;
}

#ifdef	PCI
/************************
 *
 *	hwt_quick_read
 *
 *	Stop hardware timer and read timer value and start the timer again.
 *
 *	u_long hwt_read(smc) ;
 * In
 *	smc - A pointer to the SMT Context structure.
 * Out
 *	current timer value in units of 80ns.
 *
 ************************/
u_long hwt_quick_read(struct s_smc *smc)
{
	u_long interval ;
	u_long time ;

	interval = inpd(ADDR(B2_TI_INI)) ;
	outpw(ADDR(B2_TI_CRTL), TIM_STOP) ;
	time = inpd(ADDR(B2_TI_VAL)) ;
	outpd(ADDR(B2_TI_INI),time) ;
	outpw(ADDR(B2_TI_CRTL), TIM_START) ;
	outpd(ADDR(B2_TI_INI),interval) ;

	return(time) ;
}

/************************
 *
 *	hwt_wait_time(smc,start,duration)
 *
 *	This function returnes after the amount of time is elapsed
 *	since the start time.
 * 
 * para	start		start time
 *	duration	time to wait
 *
 * NOTE: The fuction will return immediately, if the timer is not 
 *	 started
 ************************/
void hwt_wait_time(struct s_smc *smc, u_long start, long int duration)
{
	long	diff ;
	long	interval ;
	int	wrapped ;

	/*
	 * check if timer is running
	 */
	if (smc->hw.timer_activ == FALSE ||
		hwt_quick_read(smc) == hwt_quick_read(smc)) {
		return ;
	}

	interval = inpd(ADDR(B2_TI_INI)) ;
	if (interval > duration) {
		do {
			diff = (long)(start - hwt_quick_read(smc)) ;
			if (diff < 0) {
				diff += interval ;
			}
		} while (diff <= duration) ;
	}
	else {
		diff = interval ;
		wrapped = 0 ;
		do {
			if (!wrapped) {
				if (hwt_quick_read(smc) >= start) {
					diff += interval ;
					wrapped = 1 ;
				}
			}
			else {
				if (hwt_quick_read(smc) < start) {
					wrapped = 0 ;
				}
			}
		} while (diff <= duration) ;
	}
}
#endif


// SPDX-License-Identifier: GPL-2.0-or-later
/******************************************************************************
 *
 *	(C)Copyright 1998,1999 SysKonnect,
 *	a business unit of Schneider & Koch & Co. Datensysteme GmbH.
 *
 *	See the file "skfddi.c" for further information.
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

	outpd(ADDR(B2_TI_INI), (u_long) cnt * 200) ;	/* Load timer value. */
	outpw(ADDR(B2_TI_CRTL), TIM_START) ;		/* Start timer. */

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
	outpw(ADDR(B2_TI_CRTL), TIM_STOP) ;
	outpw(ADDR(B2_TI_CRTL), TIM_CL_IRQ) ;

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
	u_long	is ;

	if (smc->hw.timer_activ) {
		hwt_stop(smc) ;
		tr = (u_short)((inpd(ADDR(B2_TI_VAL))/200) & 0xffff) ;

		is = GET_ISR() ;
		/* Check if timer expired (or wraparound). */
		if ((tr > smc->hw.t_start) || (is & IS_TIMINT)) {
			hwt_restart(smc) ;
			smc->hw.t_stop = smc->hw.t_start ;
		}
		else
			smc->hw.t_stop = smc->hw.t_start - tr ;
	}
	return smc->hw.t_stop;
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

	return time;
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
 * NOTE: The function will return immediately, if the timer is not
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


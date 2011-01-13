/*****************************************************************************
* Copyright 2004 - 2008 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

/****************************************************************************/
/**
*  @file    tmrHw.h
*
*  @brief   API definitions for low level Timer driver
*
*/
/****************************************************************************/
#ifndef _TMRHW_H
#define _TMRHW_H

#include <csp/stdint.h>

typedef uint32_t tmrHw_ID_t;	/* Timer ID */
typedef uint32_t tmrHw_COUNT_t;	/* Timer count */
typedef uint32_t tmrHw_INTERVAL_t;	/* Timer interval */
typedef uint32_t tmrHw_RATE_t;	/* Timer event (count/interrupt) rate */

typedef enum {
	tmrHw_INTERRUPT_STATUS_SET,	/* Interrupted  */
	tmrHw_INTERRUPT_STATUS_UNSET	/* No Interrupt */
} tmrHw_INTERRUPT_STATUS_e;

typedef enum {
	tmrHw_CAPABILITY_CLOCK,	/* Clock speed in HHz */
	tmrHw_CAPABILITY_RESOLUTION	/* Timer resolution in bits */
} tmrHw_CAPABILITY_e;

/****************************************************************************/
/**
*  @brief   Get timer capability
*
*  This function returns various capabilities/attributes of a timer
*
*  @return  Numeric capability
*
*/
/****************************************************************************/
uint32_t tmrHw_getTimerCapability(tmrHw_ID_t timerId,	/*  [ IN ] Timer Id */
				  tmrHw_CAPABILITY_e capability	/*  [ IN ] Timer capability */
);

/****************************************************************************/
/**
*  @brief   Configures a periodic timer in terms of timer interrupt rate
*
*  This function initializes a periodic timer to generate specific number of
*  timer interrupt per second
*
*  @return   On success: Effective timer frequency
*            On failure: 0
*
*/
/****************************************************************************/
tmrHw_RATE_t tmrHw_setPeriodicTimerRate(tmrHw_ID_t timerId,	/*  [ IN ] Timer Id */
					tmrHw_RATE_t rate	/*  [ IN ] Number of timer interrupt per second */
);

/****************************************************************************/
/**
*  @brief   Configures a periodic timer to generate timer interrupt after
*           certain time interval
*
*  This function initializes a periodic timer to generate timer interrupt
*  after every time interval in milisecond
*
*  @return   On success: Effective interval set in mili-second
*            On failure: 0
*
*/
/****************************************************************************/
tmrHw_INTERVAL_t tmrHw_setPeriodicTimerInterval(tmrHw_ID_t timerId,	/*  [ IN ] Timer Id */
						tmrHw_INTERVAL_t msec	/*  [ IN ] Interval in mili-second */
);

/****************************************************************************/
/**
*  @brief   Configures a periodic timer to generate timer interrupt just once
*           after certain time interval
*
*  This function initializes a periodic timer to generate a single ticks after
*  certain time interval in milisecond
*
*  @return   On success: Effective interval set in mili-second
*            On failure: 0
*
*/
/****************************************************************************/
tmrHw_INTERVAL_t tmrHw_setOneshotTimerInterval(tmrHw_ID_t timerId,	/*  [ IN ] Timer Id */
					       tmrHw_INTERVAL_t msec	/*  [ IN ] Interval in mili-second */
);

/****************************************************************************/
/**
*  @brief   Configures a timer to run as a free running timer
*
*  This function initializes a timer to run as a free running timer
*
*  @return   Timer resolution (count / sec)
*
*/
/****************************************************************************/
tmrHw_RATE_t tmrHw_setFreeRunningTimer(tmrHw_ID_t timerId,	/*  [ IN ] Timer Id */
				       uint32_t divider	/*  [ IN ] Dividing the clock frequency */
) __attribute__ ((section(".aramtext")));

/****************************************************************************/
/**
*  @brief   Starts a timer
*
*  This function starts a preconfigured timer
*
*  @return  -1     - On Failure
*            0     - On Success
*/
/****************************************************************************/
int tmrHw_startTimer(tmrHw_ID_t timerId	/*  [ IN ] Timer id */
) __attribute__ ((section(".aramtext")));

/****************************************************************************/
/**
*  @brief   Stops a timer
*
*  This function stops a running timer
*
*  @return  -1     - On Failure
*            0     - On Success
*/
/****************************************************************************/
int tmrHw_stopTimer(tmrHw_ID_t timerId	/*  [ IN ] Timer id */
);

/****************************************************************************/
/**
*  @brief   Gets current timer count
*
*  This function returns the current timer value
*
*  @return  Current downcounting timer value
*
*/
/****************************************************************************/
tmrHw_COUNT_t tmrHw_GetCurrentCount(tmrHw_ID_t timerId	/*  [ IN ] Timer id */
) __attribute__ ((section(".aramtext")));

/****************************************************************************/
/**
*  @brief   Gets timer count rate
*
*  This function returns the number of counts per second
*
*  @return  Count rate
*
*/
/****************************************************************************/
tmrHw_RATE_t tmrHw_getCountRate(tmrHw_ID_t timerId	/*  [ IN ] Timer id */
) __attribute__ ((section(".aramtext")));

/****************************************************************************/
/**
*  @brief   Enables timer interrupt
*
*  This function enables the timer interrupt
*
*  @return   N/A
*
*/
/****************************************************************************/
void tmrHw_enableInterrupt(tmrHw_ID_t timerId	/*  [ IN ] Timer id */
);

/****************************************************************************/
/**
*  @brief   Disables timer interrupt
*
*  This function disable the timer interrupt
*
*  @return   N/A
*/
/****************************************************************************/
void tmrHw_disableInterrupt(tmrHw_ID_t timerId	/*  [ IN ] Timer id */
);

/****************************************************************************/
/**
*  @brief   Clears the interrupt
*
*  This function clears the timer interrupt
*
*  @return   N/A
*
*  @note
*     Must be called under the context of ISR
*/
/****************************************************************************/
void tmrHw_clearInterrupt(tmrHw_ID_t timerId	/*  [ IN ] Timer id */
);

/****************************************************************************/
/**
*  @brief   Gets the interrupt status
*
*  This function returns timer interrupt status
*
*  @return   Interrupt status
*/
/****************************************************************************/
tmrHw_INTERRUPT_STATUS_e tmrHw_getInterruptStatus(tmrHw_ID_t timerId	/*  [ IN ] Timer id */
);

/****************************************************************************/
/**
*  @brief   Indentifies a timer causing interrupt
*
*  This functions returns a timer causing interrupt
*
*  @return  0xFFFFFFFF   : No timer causing an interrupt
*           ! 0xFFFFFFFF : timer causing an interrupt
*  @note
*     tmrHw_clearIntrrupt() must be called with a valid timer id after calling this function
*/
/****************************************************************************/
tmrHw_ID_t tmrHw_getInterruptSource(void);

/****************************************************************************/
/**
*  @brief   Displays specific timer registers
*
*
*  @return  void
*
*/
/****************************************************************************/
void tmrHw_printDebugInfo(tmrHw_ID_t timerId,	/*  [ IN ] Timer id */
			  int (*fpPrint) (const char *, ...)	/*  [ IN ] Print callback function */
);

/****************************************************************************/
/**
*  @brief   Use a timer to perform a busy wait delay for a number of usecs.
*
*  @return   N/A
*/
/****************************************************************************/
void tmrHw_udelay(tmrHw_ID_t timerId,	/*  [ IN ] Timer id */
		  unsigned long usecs	/*  [ IN ] usec to delay */
) __attribute__ ((section(".aramtext")));

#endif /* _TMRHW_H */

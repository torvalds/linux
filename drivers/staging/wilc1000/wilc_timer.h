#ifndef __WILC_TIMER_H__
#define __WILC_TIMER_H__

/*!
 *  @file	wilc_timer.h
 *  @brief	Timer (One Shot and Periodic) OS wrapper functionality
 *  @author	syounan
 *  @sa		wilc_oswrapper.h top level OS wrapper file
 *  @date	16 Aug 2010
 *  @version	1.0
 */

#include "wilc_platform.h"
#include "wilc_errorsupport.h"

typedef void (*tpfWILC_TimerFunction)(void *);

/*!
 *  @struct             tstrWILC_TimerAttrs
 *  @brief		Timer API options
 *  @author		syounan
 *  @date		16 Aug 2010
 *  @version		1.0
 */
typedef struct {
	/* a dummy member to avoid compiler errors*/
	u8 dummy;
} tstrWILC_TimerAttrs;

/*!
 *  @brief	Creates a new timer
 *  @details	Timers are a useful utility to execute some callback function
 *              in the future.
 *              A timer object has 3 states : IDLE, PENDING and EXECUTING
 *              IDLE : initial timer state after creation, no execution for the
 *              callback function is planned
 *              PENDING : a request to execute the callback function is made
 *              using WILC_TimerStart.
 *              EXECUTING : the timer has expired and its callback is now
 *              executing, when execution is done the timer returns to PENDING
 *              if the feature CONFIG_WILC_TIMER_PERIODIC is enabled and
 *              the flag tstrWILC_TimerAttrs.bPeriodicTimer is set. otherwise the
 *              timer will return to IDLE
 *  @param[out]	pHandle handle to the newly created timer object
 *  @param[in]	pfEntry pointer to the callback function to be called when the
 *              timer expires
 *              the underlaying OS may put many restrictions on what can be
 *              called inside a timer's callback, as a general rule no blocking
 *              operations (IO or semaphore Acquision) should be perfomred
 *              It is recommended that the callback will be as short as possible
 *              and only flags other threads to do the actual work
 *              also it should be noted that the underlaying OS maynot give any
 *              guarentees on which contect this callback will execute in
 *  @param[in]	pstrAttrs Optional attributes, NULL for default
 *  @return	Error code indicating sucess/failure
 *  @sa		WILC_TimerAttrs
 *  @author	syounan
 *  @date	16 Aug 2010
 *  @version	1.0
 */
WILC_ErrNo WILC_TimerCreate(WILC_TimerHandle *pHandle,
			    tpfWILC_TimerFunction pfCallback, tstrWILC_TimerAttrs *pstrAttrs);


/*!
 *  @brief	Destroys a given timer
 *  @details	This will destroy a given timer freeing any resources used by it
 *              if the timer was PENDING Then must be cancelled as well(i.e.
 *              goes to	IDLE, same effect as calling WILC_TimerCancel first)
 *              if the timer was EXECUTING then the callback will be allowed to
 *              finish first then all resources are freed
 *  @param[in]	pHandle handle to the timer object
 *  @param[in]	pstrAttrs Optional attributes, NULL for default
 *  @return	Error code indicating sucess/failure
 *  @sa		WILC_TimerAttrs
 *  @author	syounan
 *  @date	16 Aug 2010
 *  @version	1.0
 */
WILC_ErrNo WILC_TimerDestroy(WILC_TimerHandle *pHandle,
			     tstrWILC_TimerAttrs *pstrAttrs);

/*!
 *  @brief	Starts a given timer
 *  @details	This function will move the timer to the PENDING state until the
 *              given time expires (in msec) then the callback function will be
 *              executed (timer in EXECUTING state) after execution is dene the
 *              timer either goes to IDLE (if bPeriodicTimer==false) or
 *              PENDING with same timeout value (if bPeriodicTimer==true)
 *  @param[in]	pHandle handle to the timer object
 *  @param[in]	u32Timeout timeout value in msec after witch the callback
 *              function will be executed. Timeout value of 0 is not allowed for
 *              periodic timers
 *  @param[in]	pstrAttrs Optional attributes, NULL for default,
 *              set bPeriodicTimer to run this timer as a periodic timer
 *  @return	Error code indicating sucess/failure
 *  @sa		WILC_TimerAttrs
 *  @author	syounan
 *  @date	16 Aug 2010
 *  @version	1.0
 */
WILC_ErrNo WILC_TimerStart(WILC_TimerHandle *pHandle, u32 u32Timeout, void *pvArg,
			   tstrWILC_TimerAttrs *pstrAttrs);


/*!
 *  @brief	Stops a given timer
 *  @details	This function will move the timer to the IDLE state cancelling
 *              any sheduled callback execution.
 *              if this function is called on a timer already in the IDLE state
 *              it will have no effect.
 *              if this function is called on a timer in EXECUTING state
 *              (callback has already started) it will wait until executing is
 *              done then move the timer to the IDLE state (which is trivial
 *              work if the timer is non periodic)
 *  @param[in]	pHandle handle to the timer object
 *  @param[in]	pstrAttrs Optional attributes, NULL for default,
 *  @return	Error code indicating sucess/failure
 *  @sa		WILC_TimerAttrs
 *  @author	syounan
 *  @date	16 Aug 2010
 *  @version	1.0
 */
WILC_ErrNo WILC_TimerStop(WILC_TimerHandle *pHandle,
			  tstrWILC_TimerAttrs *pstrAttrs);



#endif

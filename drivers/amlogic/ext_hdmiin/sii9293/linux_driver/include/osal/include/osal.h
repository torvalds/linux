/*
 * SiIxxxx <Firmware or Driver>
 *
 * Copyright (C) 2011 Silicon Image Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed .as is. WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
 * PURPOSE.  See the GNU General Public License for more details.
*/
/**
 * @file osal.h
 *
 * Complete public API of the OSAL layer
 *
 * Don't use source control directives!
 * Don't use source control directives!
 * Don't use source control directives!
 *
 *****************************************************************************/

#ifndef _OSAL_H
#define _OSAL_H

/* TODO: check necessity of inclusion for KAL layer implementation */

#if !defined(__KERNEL__)
#include <os_compiler.h>
#include <os_linux.h>
#include "os_data.h"
#include <os_socket.h>
#include <os_file.h>
#include <os_string.h>
#if !defined(DO_NOT_USE_DMLS)
#include <os_dmls.h>
#endif

#else
typedef bool bool_t;

#include "osal/include/os_types.h"
#include "osal/include/os_data.h"
#endif



/************************************************************************//**
*
* @brief Initialize the OSAL (public API)
*
* @return status code
* @retval SII_OS_STATUS_SUCCESS no error
* @retval SII_OS_STATUS_ERR_FAILED any kind of error
*
* @note Cannot be used from ISR.
*
******************************************************************************/
SiiOsStatus_t SiiOsInit(uint32_t maxChannels);


/************************************************************************//**
*
* @brief Cleanup the OSAL (public API)
*
* @return status code
* @retval SII_OS_STATUS_SUCCESS no error
* @retval SII_OS_STATUS_ERR_FAILED any kind of error
*
* @note Cannot be used from ISR.
*
******************************************************************************/
SiiOsStatus_t SiiOsTerm(void);



/*----------------------------------------------------------------------------------------*/
/* Timer APIs */


/************************************************************************//**
*
* @brief Create an OSAL timer (public API)
*
* @param[in]     pName           Name of the timer (16 characters at most)
* @param[in]     pTimerFunction  Function to run when the timer fires.
* @param[in]     pTimerArg       Parameter passed to timer function
* @param[in]     timerStartFlag  true: adds timer to system queue immediately.@n
*                                false: timer must be explicitly added to
*                                       system queue using
*                                       @ref SiiOsTimerSchedule().
* @param[in]     timeMsec        Timeout interval in milliseconds for firing
*                                either one-shot or periodic timer.
*                                The time value is relative, i.e., it is the
*                                delta from the current time.
*                                This parameter is valid only if timerStartFlag = true.
* @param[in]     periodicFlag    true: periodic timer,@n
*                                false: one-shot timer
* @param[out]    pRetTimerId     The created timer identifier
*
* @return status code
* @retval SII_OS_STATUS_SUCCESS no error
* @retval SII_OS_STATUS_ERR_INVALID_PARAM invalid parameter
* @retval SII_OS_STATUS_ERR_NOT_AVAIL insufficient memory
* @retval SII_OS_STATUS_ERR_FAILED any other kind of error
*
* @note Cannot be used from ISR.
* @note The name of the timer is mandatory.
*
******************************************************************************/
SiiOsStatus_t SiiOsTimerCreate
(
    const char *pName,
    void (*pTimerFunction)(void *pArg),
    void *pTimerArg,
    SiiOsTimer_t *pRetTimerId
);


/************************************************************************//**
*
* @brief Delete an OSAL timer (public API)
*
* @param[in]    timerId        the timer identifier
*
* @return status code
* @retval SII_OS_STATUS_SUCCESS no error
* @retval SII_OS_STATUS_ERR_INVALID_PARAM invalid parameter
* @retval SII_OS_STATUS_ERR_FAILED any other kind of error
*
* @note Cannot be used from ISR.
* @note Can be called from a non-OSAL thread
* @note Firing (or expiry) of a timer simply removes it from the system queue;
*       the timer still needs to be explicitly deleted. However, timer deletion
*       automatically removes it from system queue (if the timer hasn't fired, yet).
*
******************************************************************************/
SiiOsStatus_t SiiOsTimerDelete(SiiOsTimer_t *pTimerId);

SiiOsStatus_t  SiiOsTimerStart(SiiOsTimer_t timerId, uint32_t time_msec);
SiiOsStatus_t  SiiOsTimerStop(SiiOsTimer_t timerId);


/*----------------------------------------------------------------------------------------*/
/* Memory APIs */


/************************************************************************//**
*
* @brief Allocate a contiguous block of memory (public API)
*
* @param[in]   pName  name of the block (currently unused)
* @param[in]   size   size in bytes
* @param[in]   flags  SII_OS_MEMORY_SHARED: memory will be shared across a
*                     user-kernel interface.@n
*                     SII_OS_MEMORY_CONTIGUOUS: memory needs to be physically
*                     contiguous.@n
*                     Irrespective of this flag, the memory will always be
*                     virtually contiguous.
*
* @return pointer to allocated block
* @retval NULL out of memory or other failure
*
* @note Cannot be used from ISR.
* @note The block may currently not exceed 4 GiB.
*
******************************************************************************/
void * SiiOsAlloc
(
    const char *pName,
    size_t size,
    uint32_t flags
);


/************************************************************************//**
*
* @brief Allocate and clear a contiguous block of memory (public API)
*
* @param[in]   pName  name of the block (currently unused)
* @param[in]   size   size in bytes
* @param[in]   flags  SII_OS_MEMORY_SHARED: memory will be shared across a
*                     user-kernel interface.@n
*                     SII_OS_MEMORY_CONTIGUOUS: memory needs to be physically
*                     contiguous.@n
*                     Irrespective of this flag, the memory will always be
*                     virtually contiguous.
*
* @return pointer to allocated block
* @retval NULL out of memory or other failure
*
* @note Identical to @ref SiiOsAlloc(), but additionally initializes the
*       allocated memory to zero
* @note Cannot be used from ISR.
* @note The block may currently not exceed 4 GiB.
*
******************************************************************************/
void * SiiOsCalloc
(
    const char *pName,
    size_t size,
    uint32_t flags
);


/************************************************************************//**
*
* @brief Free a previously allocated block of memory (public API)
*
* @param[in]   pAddr  address of the block
*
* @note Cannot be used from ISR.
*
******************************************************************************/
void SiiOsFree(void *pAddr);



#endif /* _OSAL_H */

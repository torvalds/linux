#ifndef CSR_FRAMEWORK_EXT_H__
#define CSR_FRAMEWORK_EXT_H__
/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include "csr_result.h"
#include "csr_framework_ext_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Result codes */
#define CSR_FE_RESULT_NO_MORE_EVENTS    ((CsrResult) 0x0001)
#define CSR_FE_RESULT_INVALID_POINTER   ((CsrResult) 0x0002)
#define CSR_FE_RESULT_INVALID_HANDLE    ((CsrResult) 0x0003)
#define CSR_FE_RESULT_NO_MORE_MUTEXES   ((CsrResult) 0x0004)
#define CSR_FE_RESULT_TIMEOUT           ((CsrResult) 0x0005)
#define CSR_FE_RESULT_NO_MORE_THREADS   ((CsrResult) 0x0006)

/* Thread priorities */
#define CSR_THREAD_PRIORITY_HIGHEST     ((u16) 0)
#define CSR_THREAD_PRIORITY_HIGH        ((u16) 1)
#define CSR_THREAD_PRIORITY_NORMAL      ((u16) 2)
#define CSR_THREAD_PRIORITY_LOW         ((u16) 3)
#define CSR_THREAD_PRIORITY_LOWEST      ((u16) 4)

#define CSR_EVENT_WAIT_INFINITE         ((u16) 0xFFFF)

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrEventCreate
 *
 *  DESCRIPTION
 *      Creates an event and returns a handle to the created event.
 *
 *  RETURNS
 *      Possible values:
 *          CSR_RESULT_SUCCESS          in case of success
 *          CSR_FE_RESULT_NO_MORE_EVENTS   in case of out of event resources
 *          CSR_FE_RESULT_INVALID_POINTER  in case the eventHandle pointer is invalid
 *
 *----------------------------------------------------------------------------*/
CsrResult CsrEventCreate(CsrEventHandle *eventHandle);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrEventWait
 *
 *  DESCRIPTION
 *      Wait fore one or more of the event bits to be set.
 *
 *  RETURNS
 *      Possible values:
 *          CSR_RESULT_SUCCESS              in case of success
 *          CSR_FE_RESULT_TIMEOUT              in case of timeout
 *          CSR_FE_RESULT_INVALID_HANDLE       in case the eventHandle is invalid
 *          CSR_FE_RESULT_INVALID_POINTER      in case the eventBits pointer is invalid
 *
 *----------------------------------------------------------------------------*/
CsrResult CsrEventWait(CsrEventHandle *eventHandle, u16 timeoutInMs, u32 *eventBits);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrEventSet
 *
 *  DESCRIPTION
 *      Set an event.
 *
 *  RETURNS
 *      Possible values:
 *          CSR_RESULT_SUCCESS              in case of success
 *          CSR_FE_RESULT_INVALID_HANDLE       in case the eventHandle is invalid
 *
 *----------------------------------------------------------------------------*/
CsrResult CsrEventSet(CsrEventHandle *eventHandle, u32 eventBits);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrEventDestroy
 *
 *  DESCRIPTION
 *      Destroy the event associated.
 *
 *  RETURNS
 *      void
 *
 *----------------------------------------------------------------------------*/
void CsrEventDestroy(CsrEventHandle *eventHandle);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrMutexCreate
 *
 *  DESCRIPTION
 *      Create a mutex and return a handle to the created mutex.
 *
 *  RETURNS
 *      Possible values:
 *          CSR_RESULT_SUCCESS           in case of success
 *          CSR_FE_RESULT_NO_MORE_MUTEXES   in case of out of mutex resources
 *          CSR_FE_RESULT_INVALID_POINTER   in case the mutexHandle pointer is invalid
 *
 *----------------------------------------------------------------------------*/
CsrResult CsrMutexCreate(CsrMutexHandle *mutexHandle);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrMutexDestroy
 *
 *  DESCRIPTION
 *      Destroy the previously created mutex.
 *
 *  RETURNS
 *      void
 *
 *----------------------------------------------------------------------------*/
void CsrMutexDestroy(CsrMutexHandle *mutexHandle);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrGlobalMutexUnlock
 *
 *  DESCRIPTION
 *      Unlock the global mutex.
 *
 *----------------------------------------------------------------------------*/
void CsrGlobalMutexUnlock(void);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrThreadSleep
 *
 *  DESCRIPTION
 *      Sleep for a given period.
 *
 *  RETURNS
 *      void
 *
 *----------------------------------------------------------------------------*/
void CsrThreadSleep(u16 sleepTimeInMs);

#ifdef __cplusplus
}
#endif

#endif

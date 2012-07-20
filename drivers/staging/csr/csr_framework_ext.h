#ifndef CSR_FRAMEWORK_EXT_H__
#define CSR_FRAMEWORK_EXT_H__
/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include "csr_types.h"
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
 *      CsrMutexLock
 *
 *  DESCRIPTION
 *      Lock the mutex refered to by the provided handle.
 *
 *  RETURNS
 *      Possible values:
 *          CSR_RESULT_SUCCESS           in case of success
 *          CSR_FE_RESULT_INVALID_HANDLE    in case the mutexHandle is invalid
 *
 *----------------------------------------------------------------------------*/
CsrResult CsrMutexLock(CsrMutexHandle *mutexHandle);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrMutexUnlock
 *
 *  DESCRIPTION
 *      Unlock the mutex refered to by the provided handle.
 *
 *  RETURNS
 *      Possible values:
 *          CSR_RESULT_SUCCESS           in case of success
 *          CSR_FE_RESULT_INVALID_HANDLE    in case the mutexHandle is invalid
 *
 *----------------------------------------------------------------------------*/
CsrResult CsrMutexUnlock(CsrMutexHandle *mutexHandle);

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
 *      CsrGlobalMutexLock
 *
 *  DESCRIPTION
 *      Lock the global mutex. The global mutex is a single pre-initialised
 *      shared mutex, spinlock or similar that does not need to be created prior
 *      to use. The limitation is that there is only one single lock shared
 *      between all code. Consequently, it must only be used very briefly to
 *      either protect simple one-time initialisation or to protect the creation
 *      of a dedicated mutex by calling CsrMutexCreate.
 *
 *----------------------------------------------------------------------------*/
void CsrGlobalMutexLock(void);

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
 *      CsrThreadCreate
 *
 *  DESCRIPTION
 *      Create thread function and return a handle to the created thread.
 *
 *  RETURNS
 *      Possible values:
 *          CSR_RESULT_SUCCESS           in case of success
 *          CSR_FE_RESULT_NO_MORE_THREADS   in case of out of thread resources
 *          CSR_FE_RESULT_INVALID_POINTER   in case one of the supplied pointers is invalid
 *
 *----------------------------------------------------------------------------*/
CsrResult CsrThreadCreate(void (*threadFunction)(void *pointer), void *pointer,
    u32 stackSize, u16 priority,
    const char *threadName, CsrThreadHandle *threadHandle);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrThreadGetHandle
 *
 *  DESCRIPTION
 *      Return thread handle of calling thread.
 *
 *  RETURNS
 *      Possible values:
 *          CSR_RESULT_SUCCESS             in case of success
 *          CSR_FE_RESULT_INVALID_POINTER  in case the threadHandle pointer is invalid
 *
 *----------------------------------------------------------------------------*/
CsrResult CsrThreadGetHandle(CsrThreadHandle *threadHandle);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrThreadEqual
 *
 *  DESCRIPTION
 *      Compare thread handles
 *
 *  RETURNS
 *      Possible values:
 *          CSR_RESULT_SUCCESS             in case thread handles are identical
 *          CSR_FE_RESULT_INVALID_POINTER  in case either threadHandle pointer is invalid
 *          CSR_RESULT_FAILURE             otherwise
 *
 *----------------------------------------------------------------------------*/
CsrResult CsrThreadEqual(CsrThreadHandle *threadHandle1, CsrThreadHandle *threadHandle2);

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

#ifndef CSR_PMEM_DEBUG_ENABLE
/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrMemAlloc
 *
 *  DESCRIPTION
 *      Allocate dynamic memory of a given size.
 *
 *  RETURNS
 *      Pointer to allocated memory, or NULL in case of failure.
 *      Allocated memory is not initialised.
 *
 *----------------------------------------------------------------------------*/
#ifdef CSR_MEM_DEBUG
void *CsrMemAllocDebug(size_t size,
    const char *file, u32 line);
#define CsrMemAlloc(sz) CsrMemAllocDebug((sz), __FILE__, __LINE__)
#else
void *CsrMemAlloc(size_t size);
#endif

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrMemCalloc
 *
 *  DESCRIPTION
 *      Allocate dynamic memory of a given size calculated as the
 *      numberOfElements times the elementSize.
 *
 *  RETURNS
 *      Pointer to allocated memory, or NULL in case of failure.
 *      Allocated memory is zero initialised.
 *
 *----------------------------------------------------------------------------*/
#ifdef CSR_MEM_DEBUG
void *CsrMemCallocDebug(size_t numberOfElements, size_t elementSize,
    const char *file, u32 line);
#define CsrMemCalloc(cnt, sz) CsrMemAllocDebug((cnt), (sz), __FILE__, __LINE__)
#else
void *CsrMemCalloc(size_t numberOfElements, size_t elementSize);
#endif

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrMemFree
 *
 *  DESCRIPTION
 *      Free dynamic allocated memory.
 *
 *  RETURNS
 *      void
 *
 *----------------------------------------------------------------------------*/
void CsrMemFree(void *pointer);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrMemAllocDma
 *
 *  DESCRIPTION
 *      Allocate dynamic memory suitable for DMA transfers.
 *
 *  RETURNS
 *      Pointer to allocated memory, or NULL in case of failure.
 *      Allocated memory is not initialised.
 *
 *----------------------------------------------------------------------------*/
#ifdef CSR_MEM_DEBUG
void *CsrMemAllocDmaDebug(size_t size,
    const char *file, u32 line);
#define CsrMemAllocDma(sz) CsrMemAllocDmaDebug((sz), __FILE__, __LINE__)
#else
void *CsrMemAllocDma(size_t size);
#endif


/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrMemFreeDma
 *
 *  DESCRIPTION
 *      Free dynamic memory allocated by CsrMemAllocDma.
 *
 *  RETURNS
 *      void
 *
 *----------------------------------------------------------------------------*/
void CsrMemFreeDma(void *pointer);
#else

#include "csr_pmem.h"

#define CsrMemAlloc(size) CsrPmemDebugAlloc(size, CSR_PMEM_DEBUG_TYPE_MEM_ALLOC, __FILE__, __LINE__)

#define CsrMemCalloc(numberOfElements, elementSize) CsrPmemDebugAlloc((numberOfElements * elementSize), CSR_PMEM_DEBUG_TYPE_MEM_CALLOC, __FILE__, __LINE__)

#define CsrMemFree(ptr) CsrPmemDebugFree(ptr,CSR_PMEM_DEBUG_TYPE_MEM_ALLOC,  __FILE__, __LINE__)

#define CsrMemAllocDma(size) CsrPmemDebugAlloc(size, CSR_PMEM_DEBUG_TYPE_MEM_ALLOC_DMA, __FILE__, __LINE__)

#define CsrMemFreeDma(ptr) CsrPmemDebugFree(ptr, CSR_PMEM_DEBUG_TYPE_MEM_ALLOC_DMA, __FILE__, __LINE__)

#endif


#ifdef __cplusplus
}
#endif

#endif

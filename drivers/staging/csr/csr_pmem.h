#ifndef CSR_PMEM_H__
#define CSR_PMEM_H__
/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include <linux/types.h>
#include "csr_util.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CSR_PMEM_DEBUG_ENABLE
/*****************************************************************************

    NAME
        CsrPmemAlloc

    DESCRIPTION
        This function will allocate a contiguous block of memory of at least
        the specified size in bytes and return a pointer to the allocated
        memory. This function is not allowed to return NULL. A size of 0 is a
        valid request, and a unique and valid (not NULL) pointer must be
        returned in this case.

    PARAMETERS
        size - Size of memory requested. Note that a size of 0 is valid.

    RETURNS
        Pointer to allocated memory.

*****************************************************************************/
#ifdef CSR_PMEM_DEBUG
void *CsrPmemAllocDebug(size_t size,
    const char *file, u32 line);
#define CsrPmemAlloc(sz) CsrPmemAllocDebug((sz), __FILE__, __LINE__)
#else
void *CsrPmemAlloc(size_t size);
#endif


/*****************************************************************************

    NAME
        CsrPmemFree

    DESCRIPTION
        This function will deallocate a previously allocated block of memory.

    PARAMETERS
        ptr - Pointer to allocated memory.

*****************************************************************************/
void CsrPmemFree(void *ptr);
#endif

/*****************************************************************************

    NAME
        CsrPmemZalloc

    DESCRIPTION
        This function is equivalent to CsrPmemAlloc, but the allocated memory
        is initialised to zero.

    PARAMETERS
        size - Size of memory requested. Note that a size of 0 is valid.

    RETURNS
        Pointer to allocated memory.

*****************************************************************************/
#define CsrPmemZalloc(s) (CsrMemSet(CsrPmemAlloc(s), 0x00, (s)))


/*****************************************************************************

    NAME
        pnew and zpnew

    DESCRIPTIOM
        Type-safe wrappers for CsrPmemAlloc and CsrPmemZalloc, for allocating
        single instances of a specified and named type.

    PARAMETERS
        t - type to allocate.

*****************************************************************************/
#define pnew(t) ((t *) (CsrPmemAlloc(sizeof(t))))
#define zpnew(t) ((t *) (CsrPmemZalloc(sizeof(t))))


/*----------------------------------------------------------------------------*
 * Csr Pmem Debug code. Allows custom callbacks on CsrPmemAlloc and CsrPmemFree
 *----------------------------------------------------------------------------*/
#ifdef CSR_PMEM_DEBUG_ENABLE

typedef u8 CsrPmemDebugAllocType;
#define CSR_PMEM_DEBUG_TYPE_PMEM_ALLOC    1
#define CSR_PMEM_DEBUG_TYPE_MEM_ALLOC     2
#define CSR_PMEM_DEBUG_TYPE_MEM_CALLOC    3
#define CSR_PMEM_DEBUG_TYPE_MEM_ALLOC_DMA 4

typedef void (CsrPmemDebugOnAlloc)(void *ptr, void *userptr, size_t size, CsrPmemDebugAllocType type, const char* file, u32 line);
typedef void (CsrPmemDebugOnFree)(void *ptr, void *userptr, CsrPmemDebugAllocType type, const char* file, u32 line);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrPmemInstallHooks
 *
 *  DESCRIPTION
 *      Install debug hooks for memory allocation
 *      Use NULL values to uninstall the hooks
 *      headSize = The number of extra bytes to allocate in the head of the Allocated buffer
 *      footSize = The number of extra bytes to allocate in the end of the Allocated buffer
 *
 *  RETURNS
 *      void
 *
 *----------------------------------------------------------------------------*/
void CsrPmemDebugInstallHooks(u8 headSize, u8 endSize, CsrPmemDebugOnAlloc *onAllocCallback, CsrPmemDebugOnFree *onFreeCallback);

void *CsrPmemDebugAlloc(size_t size, CsrPmemDebugAllocType type, const char* file, u32 line);
#define CsrPmemAlloc(size) CsrPmemDebugAlloc(size, CSR_PMEM_DEBUG_TYPE_PMEM_ALLOC, __FILE__, __LINE__)

void CsrPmemDebugFree(void *ptr, CsrPmemDebugAllocType type, const char* file, u32 line);
#define CsrPmemFree(ptr) CsrPmemDebugFree(ptr, CSR_PMEM_DEBUG_TYPE_PMEM_ALLOC, __FILE__, __LINE__)

#endif


#ifdef __cplusplus
}
#endif

#endif

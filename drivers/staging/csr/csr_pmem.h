#ifndef CSR_PMEM_H__
#define CSR_PMEM_H__
/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include <linux/types.h>
#include "csr_macro.h"

#ifdef __cplusplus
extern "C" {
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

void CsrPmemDebugFree(void *ptr, CsrPmemDebugAllocType type, const char* file, u32 line);

#endif


#ifdef __cplusplus
}
#endif

#endif

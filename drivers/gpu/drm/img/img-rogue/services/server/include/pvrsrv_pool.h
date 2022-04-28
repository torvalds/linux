/**************************************************************************/ /*!
@File
@Title          Services pool implementation
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Provides a generic pool implementation.
                The pool allows to dynamically retrieve and return entries from
                it using functions pair PVRSRVPoolGet/PVRSRVPoolPut. The entries
                are created in lazy manner which means not until first usage.
                The pool API allows to pass and allocation/free functions
                pair that will allocate entry's private data and return it
                to the caller on every entry 'Get'.
                The pool will keep up to ui32MaxEntries entries allocated.
                Every entry that exceeds this number and is 'Put' back to the
                pool will be freed on the spot instead being returned to the
                pool.
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /***************************************************************************/

#if !defined(PVRSRVPOOL_H)
#define PVRSRVPOOL_H

/**************************************************************************/ /*!
 @Description  Callback function called during creation of the new element. This
               function allocates an object that will be stored in the pool.
               The object can be retrieved from the pool by calling
               PVRSRVPoolGet.
 @Input        pvPrivData      Private data passed to the alloc function.
 @Output       pvOut           Allocated object.
 @Return       PVRSRV_ERROR    PVRSRV_OK on success and an error otherwise
*/ /***************************************************************************/
typedef PVRSRV_ERROR (PVRSRV_POOL_ALLOC_FUNC)(void *pvPrivData, void **pvOut);

/**************************************************************************/ /*!
 @Description  Callback function called to free the object allocated by
               the counterpart alloc function.
 @Input        pvPrivData      Private data passed to the free function.
 @Output       pvFreeData      Object allocated by PVRSRV_POOL_ALLOC_FUNC.
*/ /***************************************************************************/
typedef void (PVRSRV_POOL_FREE_FUNC)(void *pvPrivData, void *pvFreeData);

typedef IMG_HANDLE PVRSRV_POOL_TOKEN;

typedef struct _PVRSRV_POOL_ PVRSRV_POOL;

/**************************************************************************/ /*!
 @Function     PVRSRVPoolCreate
 @Description  Creates new buffer pool.
 @Input        pfnAlloc        Allocation function pointer. Function is used
                               to allocate new pool entries' data.
 @Input        pfnFree         Free function pointer. Function is used to
                               free memory allocated by pfnAlloc function.
 @Input        ui32MaxEntries  Total maximum number of entries in the pool.
 @Input        pszName         Name of the pool. String has to be NULL
                               terminated.
 @Input        pvPrivData      Private data that will be passed to pfnAlloc and
                               pfnFree functions.
 @Output       ppsPool         New buffer pool object.
 @Return       PVRSRV_ERROR    PVRSRV_OK on success and an error otherwise
*/ /***************************************************************************/
PVRSRV_ERROR PVRSRVPoolCreate(PVRSRV_POOL_ALLOC_FUNC *pfnAlloc,
					PVRSRV_POOL_FREE_FUNC *pfnFree,
					IMG_UINT32 ui32MaxEntries,
					const IMG_CHAR *pszName,
					void *pvPrivData,
					PVRSRV_POOL **ppsPool);

/**************************************************************************/ /*!
 @Function     PVRSRVPoolDestroy
 @Description  Destroys pool created by PVRSRVPoolCreate.
 @Input        psPool          Buffer pool object meant to be destroyed.
*/ /***************************************************************************/
void PVRSRVPoolDestroy(PVRSRV_POOL *psPool);

/**************************************************************************/ /*!
 @Function     PVRSRVPoolGet
 @Description  Retrieves an entry from a pool. If no free elements are
               available new entry will be allocated.
 @Input        psPool          Pointer to the pool.
 @Output       hToken          Pointer to the entry handle.
 @Output       ppvDataOut      Pointer to data stored in the entry (the data
                               allocated by the pfnAlloc function).
 @Return       PVRSRV_ERROR    PVRSRV_OK on success and an error otherwise
*/ /***************************************************************************/
PVRSRV_ERROR PVRSRVPoolGet(PVRSRV_POOL *psPool,
						PVRSRV_POOL_TOKEN *hToken,
						void **ppvDataOut);

/**************************************************************************/ /*!
 @Function     PVRSRVPoolPut
 @Description  Returns entry to the pool. If number of entries is greater
               than ui32MaxEntries set during pool creation the entry will
               be freed instead.
 @Input        psPool          Pointer to the pool.
 @Input        hToken          Entry handle.
 @Return       PVRSRV_ERROR    PVRSRV_OK on success and an error otherwise
*/ /***************************************************************************/
PVRSRV_ERROR PVRSRVPoolPut(PVRSRV_POOL *psPool,
						PVRSRV_POOL_TOKEN hToken);

#endif /* PVRSRVPOOL_H */

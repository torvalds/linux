/*************************************************************************/ /*!
@File
@Title          Functions and types for creating Debug Info implementations.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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
*/ /**************************************************************************/

#ifndef OSDI_IMPL_H
#define OSDI_IMPL_H

#include <stdarg.h>

#include "di_common.h"
#include "pvrsrv_error.h"

/*! Implementation callbacks. Those operations are performed on native
 * implementation handles. */
typedef struct OSDI_IMPL_ENTRY_CB
{
    /*! @Function pfnVPrintf
     *
     * @Description
     * Implementation of the 'vprintf' operation.
     *
     * @Input pvNativeHandle native implementation handle
     * @Input pszFmt NUL-terminated format string
     * @Input va_list variable length argument list
     */
    void (*pfnVPrintf)(void *pvNativeHandle, const IMG_CHAR *pszFmt, va_list pArgs);

    /*! @Function pfnPuts
     *
     * @Description
     * Implementation of the 'puts' operation.
     *
     * @Input pvNativeHandle native implementation handle
     * @Input pszStr NUL-terminated string
     */
    void (*pfnPuts)(void *pvNativeHandle, const IMG_CHAR *pszStr);

    /*! @Function pfnHasOverflowed
     *
     * @Description
     * Checks if the native implementation's buffer has overflowed.
     *
     * @Input pvNativeHandle native implementation handle
     */
    IMG_BOOL (*pfnHasOverflowed)(void *pvNativeHandle);
} OSDI_IMPL_ENTRY_CB;

/*! Debug Info entry specialisation. */
typedef struct OSDI_IMPL_ENTRY
{
    /*! Pointer to the private data. The data originates from DICreateEntry()
     *  function. */
    void *pvPrivData;
    /*! Pointer to the implementation native handle. */
    void *pvNative;
    /*! Implementation entry callbacks. */
    OSDI_IMPL_ENTRY_CB *psCb;
} OSDI_IMPL_ENTRY;

/*! Debug Info implementation callbacks. */
typedef struct OSDI_IMPL_CB
{
    /*! Initialise implementation callback.
     */
    PVRSRV_ERROR (*pfnInit)(void);

    /*! De-initialise implementation callback.
     */
    void (*pfnDeInit)(void);

    /*! @Function pfnCreateEntry
     *
     * @Description
     * Creates entry of eType type with pszName in the pvNativeGroup parent
     * group. The entry is an abstract term which depends on the implementation,
     * e.g.: a file in DebugFS.
     *
     * @Input pszName: name of the entry
     * @Input eType: type of the entry
     * @Input psIterCb: iterator implementation for the entry
     * @Input pvPrivData: data that will be passed to the iterator callbacks
     *                    in OSDI_IMPL_ENTRY - it can be retrieved by calling
     *                    DIGetPrivData() function
     * @Input pvNativeGroup: implementation specific handle to the parent group
     *
     * @Output pvNativeEntry: implementation specific handle to the entry
     *
     * return PVRSRV_ERROR error code
     */
    PVRSRV_ERROR (*pfnCreateEntry)(const IMG_CHAR *pszName,
                                   DI_ENTRY_TYPE eType,
                                   const DI_ITERATOR_CB *psIterCb,
                                   void *pvPrivData,
                                   void *pvNativeGroup,
                                   void **pvNativeEntry);

    /*! @Function pfnDestroyEntry
     *
     * @Description
     * Destroys native entry.
     *
     * @Input psNativeEntry: handle to the entry
     */
    void (*pfnDestroyEntry)(void *psNativeEntry);

    /*! @Function pfnCreateGroup
     *
     * @Description
     * Creates group with pszName in the psNativeParentGroup parent group.
     * The group is an abstract term which depends on the implementation,
     * e.g.: a directory in DebugFS.
     *
     * @Input pszName: name of the entry
     * @Input psNativeParentGroup: implementation specific handle to the parent
     *                             group
     *
     * @Output psNativeGroup: implementation specific handle to the group
     *
     * return PVRSRV_ERROR error code
     */
    PVRSRV_ERROR (*pfnCreateGroup)(const IMG_CHAR *pszName,
                                   void *psNativeParentGroup,
                                   void **psNativeGroup);

    /*! @Function pfnDestroyGroup
     *
     * @Description
     * Destroys native group.
     *
     * @Input psNativeGroup: handle to the group
     */
    void (*pfnDestroyGroup)(void *psNativeGroup);
} OSDI_IMPL_CB;

/*! @Function DIRegisterImplementation
 *
 * @Description
 * Registers Debug Info implementations with the framework. The framework takes
 * the ownership of the implementation and will clean up the resources when
 * it's de-initialised.
 *
 * @Input pszName: name of the implementation
 * @Input psImplCb: implementation callbacks
 *
 * @Return PVRSRV_ERROR error code
 */
PVRSRV_ERROR DIRegisterImplementation(const IMG_CHAR *pszName,
                                      const OSDI_IMPL_CB *psImplCb);

#endif /* OSDI_IMPL_H */

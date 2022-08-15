/*************************************************************************/ /*!
@File
@Title          Functions for creating Debug Info groups and entries.
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

#ifndef DI_SERVER_H
#define DI_SERVER_H

#if defined(__linux__)
 #include <linux/version.h>

 #if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
  #include <linux/stdarg.h>
 #else
  #include <stdarg.h>
 #endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0) */
#else
 #include <stdarg.h>
#endif /* __linux__ */

#include "di_common.h"
#include "pvrsrv_error.h"
#include "img_defs.h"

/*! @Function DIInit
 *
 * @Description
 * Initialises Debug Info framework. This function will create common resources
 * for the framework.
 *
 * Note: This function must be called before first call to
 *       DIRegisterImplementation() all of the implementations.
 */
PVRSRV_ERROR DIInit(void);

/*! @Function DIDeInit
 *
 * @Description
 * De-initialises Debug Info framework. This function will call pfnDeInit()
 * on each implementation and clean up common resources.
 *
 * In case some of the entries and groups have not been cleaned up this function
 * will also perform recursive sweep and remove all entries and group for
 * all implementations.
 */
void DIDeInit(void);

/*! @Function DICreateEntry
 *
 * @Description
 * Creates debug info entry. Depending on different implementations the entry
 * might be for example a DebugFS file or something totally different.
 *
 * The entry will belong to a parent group if provided or to the root group
 * if not.
 *
 * @Input pszName: name of the new entry
 * @Input psDiGroup: parent group, if NULL entry will belong to the root group
 * @Input psIterCb: implementation of the iterator for the entry
 * @Input psPriv: private data that will be passed to the iterator operations
 * @Input eType: type of the entry
 *
 * @Output ppsEntry: handle to the newly created entry
 *
 * @Return   PVRSRV_ERROR error code
 */
PVRSRV_ERROR DICreateEntry(const IMG_CHAR *pszName,
                           DI_GROUP *psGroup,
                           const DI_ITERATOR_CB *psIterCb,
                           void *psPriv,
                           DI_ENTRY_TYPE eType,
                           DI_ENTRY **ppsEntry);

/*! @Function DIDestroyEntry
 *
 * @Description
 * Destroys debug info entry.
 *
 * @Input psEntry: handle to the entry
 */
void DIDestroyEntry(DI_ENTRY *psEntry);

/*! @Function DICreateGroup
 *
 * @Description
 * Creates debug info group. Depending on different implementations the group
 * might be for example a DebugFS directory or something totally different.
 *
 * The group will belong to a parent group if provided or to the root group
 * if not.
 *
 * @Input pszName: name of the new entry
 * @Input psParent: parent group, if NULL entry will belong to the root group
 *
 * @Output ppsGroup: handle to the newly created entry
 *
 * @Return   PVRSRV_ERROR error code
 */
PVRSRV_ERROR DICreateGroup(const IMG_CHAR *pszName,
                           DI_GROUP *psParent,
                           DI_GROUP **ppsGroup);

/*! @Function DIDestroyGroup
 *
 * @Description
 * Destroys debug info group.
 *
 * @Input psGroup: handle to the group
 */
void DIDestroyGroup(DI_GROUP *psGroup);

/*! @Function DIGetPrivData
 *
 * @Description
 * Retrieves private data from psEntry. The data is either passed during
 * entry creation via psPriv parameter of DICreateEntry() function
 * or by explicitly setting it with DIGetPrivData() function.
 *
 * @Input psEntry pointer to OSDI_IMPL_ENTRY object
 *
 * @Returns pointer to the private data (can be NULL if private data
 *          has not been specified)
 */
void *DIGetPrivData(const OSDI_IMPL_ENTRY *psEntry);

/*! @Function DIWrite
 *
 * @Description
 * Writes the binary data of the DI entry to the output sync, whatever that may
 * be for the DI implementation.
 *
 * @Input psEntry pointer to OSDI_IMPL_ENTRY object
 * @Input pvData data
 * @Input uiSize pvData length
 */
void DIWrite(const OSDI_IMPL_ENTRY *psEntry, const void *pvData,
             IMG_UINT32 uiSize);

/*! @Function DIPrintf
 *
 * @Description
 * Prints formatted string to the DI entry.
 *
 * @Input psEntry pointer to OSDI_IMPL_ENTRY object
 * @Input pszFmt NUL-terminated format string
 */
void DIPrintf(const OSDI_IMPL_ENTRY *psEntry, const IMG_CHAR *pszFmt, ...)
	__printf(2, 3);

/*! @Function DIVPrintf
 *
 * @Description
 * Prints formatted string to the DI entry. Equivalent to DIPrintf but takes
 * va_list instead of a variable number of arguments.
 *
 * @Input psEntry pointer to OSDI_IMPL_ENTRY object
 * @Input pszFmt NUL-terminated format string
 * @Input pArgs vs_list object
 */
void DIVPrintf(const OSDI_IMPL_ENTRY *psEntry, const IMG_CHAR *pszFmt,
               va_list pArgs);

/*! @Function DIPrintf
 *
 * @Description
 * Prints a string to the DI entry.
 *
 * @Input psEntry pointer to OSDI_IMPL_ENTRY object
 * @Input pszFmt NUL-terminated string
 */
void DIPuts(const OSDI_IMPL_ENTRY *psEntry, const IMG_CHAR *pszStr);

/*! @Function DIHasOverflowed
 *
 * @Description
 * Checks if the DI buffer has overflowed.
 *
 * @Return IMG_TRUE if buffer overflowed
 */
IMG_BOOL DIHasOverflowed(const OSDI_IMPL_ENTRY *psEntry);

#endif /* DI_SERVER_H */

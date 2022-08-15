/*************************************************************************/ /*!
@File
@Title          Common types for Debug Info framework.
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

#ifndef DI_COMMON_H
#define DI_COMMON_H

#include "img_types.h"

/* Token that signals that a header should be printed. */
#define DI_START_TOKEN ((void *) 1)

/* This is a public handle to an entry. */
#ifndef DI_GROUP_DEFINED
#define DI_GROUP_DEFINED
typedef struct DI_GROUP DI_GROUP;
#endif
#ifndef DI_ENTRY_DEFINED
#define DI_ENTRY_DEFINED
typedef struct DI_ENTRY DI_ENTRY;
#endif
typedef struct OSDI_IMPL_ENTRY OSDI_IMPL_ENTRY;

/*! Debug Info entries types. */
typedef enum DI_ENTRY_TYPE
{
    DI_ENTRY_TYPE_GENERIC,          /*!< generic entry type, implements
                                         start/stop/next/show iterator
                                         interface */
    DI_ENTRY_TYPE_RANDOM_ACCESS,    /*!< random access entry, implements
                                         seek/read iterator interface */
} DI_ENTRY_TYPE;

/*! @Function DI_PFN_START
 *
 * @Description
 * Start operation returns first entry and passes it to Show operation.
 *
 * @Input psEntry pointer to the implementation entry
 * @InOut pui64Pos current data position in the entry
 *
 * @Return pointer to data that will be passed to the other iterator
 *         functions in pvData argument
 */
typedef void *(*DI_PFN_START)(OSDI_IMPL_ENTRY *psEntry, IMG_UINT64 *pui64Pos);

/*! @Function DI_PFN_STOP
 *
 * @Description
 * Stop operations is called after iterator reaches end of data.
 *
 * If pvData was allocated in pfnStart it should be freed here.
 *
 * @Input psEntry pointer to the implementation entry
 * @Input pvData pointer to data returned from pfnStart/pfnNext
 */
typedef void (*DI_PFN_STOP)(OSDI_IMPL_ENTRY *psEntry, void *pvData);

/*! @Function DI_PFN_NEXT
 *
 * @Description
 * Next returns next data entry and passes it to Show operation.
 *
 * @Input psEntry pointer to the implementation entry
 * @Input pvData pointer to data returned from pfnStart/pfnNext
 * @InOut pui64Pos current data position in the entry
 */
typedef void *(*DI_PFN_NEXT)(OSDI_IMPL_ENTRY *psEntry, void *pvData,
                             IMG_UINT64 *pui64Pos);

/*! @Function DI_PFN_SHOW
 *
 * @Description
 * Outputs the data element.
 *
 * @Input psEntry pointer to the implementation entry
 * @Input pvData pointer to data returned from pfnStart/pfnNext
 */
typedef int (*DI_PFN_SHOW)(OSDI_IMPL_ENTRY *psEntry, void *pvData);

/*! @Function DI_PFN_SEEK
 *
 * @Description
 * Changes position of the entry data pointer
 *
 * @Input uiOffset new entry offset (absolute)
 * @Input pvData private data provided during entry creation
 */
typedef IMG_INT64 (*DI_PFN_SEEK)(IMG_UINT64 ui64Offset, void *pvData);

/*! @Function DI_PFN_READ
 *
 * @Description
 * Retrieves data from the entry from position previously set by Seek.
 *
 * @Input pszBuffer output buffer
 * @Input ui64Count length of the output buffer
 * @InOut pui64Pos pointer to the current position in the entry
 * @Input pvData private data provided during entry creation
 */
typedef IMG_INT64 (*DI_PFN_READ)(IMG_CHAR *pszBuffer, IMG_UINT64 ui64Count,
                                 IMG_UINT64 *pui64Pos, void *pvData);

/*! @Function DI_PFN_WRITE
 *
 * @Description
 * Handle writes operation to the entry.
 *
 * @Input pszBuffer NUL-terminated buffer containing written data
 * @Input ui64Count length of the data in pszBuffer (length of the buffer)
 * @InOut pui64Pos pointer to the current position in the entry
 * @Input pvData private data provided during entry creation
 */
typedef IMG_INT64 (*DI_PFN_WRITE)(const IMG_CHAR *pszBuffer,
                                  IMG_UINT64 ui64Count, IMG_UINT64 *pui64Pos,
                                  void *pvData);

/*! Debug info entry iterator.
 *
 * This covers all entry types: GENERIC and RANDOM_ACCESS.
 *
 * The GENERIC entry type
 *
 * The GENERIC type should implement either a full set of following callbacks:
 * pfnStart, pfnStop, pfnNext and pfnShow, or pfnShow only. If only pfnShow
 * callback is given the framework will use default handlers in place of the
 * other ones.
 *
 * e.g. for generic entry:
 *
 *   struct sIter = {
 *     .pfnStart = StartCb, .pfnStop = StopCb, pfnNext = NextCb,
 *     .pfnShow = ShowCb
 *   };
 *
 * The use case for implementing pfnShow only is if the data for the given
 * entry is short and can be printed in one go because the pfnShow callback
 * will be called only once.
 *
 * e.g. for one-shot print generic entry:
 *
 *   struct sIter = {
 *     .pfnShow = SingleShowCb
 *   };
 *
 * The DICreateEntry() function will return error if DI_ENTRY_TYPE_GENERIC
 * type is used and invalid combination of callbacks is given.
 *
 * The RANDOM_ACCESS entry
 *
 * The RANDOM_ACCESS type should implement either both pfnSeek and pfnRead
 * or pfnRead only callbacks.
 *
 * e.g. of seekable and readable random access entry:
 *
 *   struct sIter = {
 *     .pfnSeek = SeekCb, .pfnRead = ReadCb
 *   };
 *
 * The DICreateEntry() function will return error if DI_ENTRY_TYPE_RANDOM_ACCESS
 * type is used and invalid combination of callbacks is given.
 *
 * Writing to file (optional)
 *
 * The iterator allows also to pass a pfnWrite callback that allows implementing
 * write operation on the entry. The write operation is entry type agnostic
 * which means that it can be defined for both GENERIC and RANDOM_ACCESS
 * entries.
 *
 * e.g. for writable one-shot print generic entry
 *
 *   struct sIter = {
 *     .pfnShow = SingleShowCb, .pfnWrite = WriteCb
 *   };
 */
typedef struct DI_ITERATOR_CB
{
    /* Generic entry interface. */

    DI_PFN_START pfnStart; /*!< Starts iteration and returns first element
                                of entry's data. */
    DI_PFN_STOP pfnStop;   /*!< Stops iteration. */
    DI_PFN_NEXT pfnNext;   /*!< Returns next element of entry's data. */
    DI_PFN_SHOW pfnShow;   /*!< Shows current data element of an entry. */

    /* Optional random access entry interface. */

    DI_PFN_SEEK pfnSeek;   /*!< Sets data pointer in an entry. */
    DI_PFN_READ pfnRead;   /*!< Reads data from an entry. */

    /* Optional writing to entry interface. Null terminated. */

    DI_PFN_WRITE pfnWrite; /*!< Performs write operation on an entry. */
    IMG_UINT32   ui32WriteLenMax; /*!< Maximum char length of entry
                                       accepted for write. Includes \0 */
} DI_ITERATOR_CB;

#endif /* DI_COMMON_H */

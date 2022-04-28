/*************************************************************************/ /*!
@File
@Title          Device Memory Management allocation flags for internal Services
                use only
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    This file defines flags used on memory allocations and mappings
                These flags are relevant throughout the memory management
                software stack and are specified by users of services and
                understood by all levels of the memory management in the server
                and in special cases in the client.
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

#ifndef PVRSRV_MEMALLOCFLAGS_INTERNAL_H
#define PVRSRV_MEMALLOCFLAGS_INTERNAL_H

/*!
   CPU domain. Request uncached memory. This means that any writes to memory
   allocated with this flag are written straight to memory and thus are
   coherent for any device in the system.
*/
#define PVRSRV_MEMALLOCFLAG_CPU_UNCACHED (1ULL<<11)

/*!
  @Description    Macro checking whether the PVRSRV_MEMALLOCFLAG_CPU_UNCACHED mode is set.
  @Input  uiFlags Allocation flags.
  @Return         True if the mode is set, false otherwise
 */
#define PVRSRV_CHECK_CPU_UNCACHED(uiFlags) (PVRSRV_CPU_CACHE_MODE(uiFlags) == PVRSRV_MEMALLOCFLAG_CPU_UNCACHED)

/*!
 * Memory will be uncached on CPU and GPU
 */
#define PVRSRV_MEMALLOCFLAG_UNCACHED (PVRSRV_MEMALLOCFLAG_GPU_UNCACHED | PVRSRV_MEMALLOCFLAG_CPU_UNCACHED)

/*!
  @Description    Macro checking whether the PVRSRV_MEMALLOCFLAG_UNCACHED mode is set.
  @Input  uiFlags Allocation flags.
  @Return         True if the mode is set, false otherwise
 */
#define PVRSRV_CHECK_UNCACHED(uiFlags) (PVRSRV_CACHE_MODE(uiFlags) == PVRSRV_MEMALLOCFLAG_UNCACHED)

#endif /* PVRSRV_MEMALLOCFLAGS_INTERNAL_H */

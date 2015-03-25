/*************************************************************************/ /*!
@File
@Title          Common bridge header for debugmisc
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures that are used by both
                the client and sever side of the bridge for debugmisc
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

#ifndef COMMON_DEBUGMISC_BRIDGE_H
#define COMMON_DEBUGMISC_BRIDGE_H

#include "devicemem_typedefs.h"
#include "rgx_bridge.h"
#include "pvrsrv_memallocflags.h"


#include "pvr_bridge_io.h"

#define PVRSRV_BRIDGE_DEBUGMISC_CMD_FIRST			(PVRSRV_BRIDGE_DEBUGMISC_START)
#define PVRSRV_BRIDGE_DEBUGMISC_DEBUGMISCSLCSETBYPASSSTATE			PVRSRV_IOWR(PVRSRV_BRIDGE_DEBUGMISC_CMD_FIRST+0)
#define PVRSRV_BRIDGE_DEBUGMISC_RGXDEBUGMISCSETFWLOG			PVRSRV_IOWR(PVRSRV_BRIDGE_DEBUGMISC_CMD_FIRST+1)
#define PVRSRV_BRIDGE_DEBUGMISC_RGXDEBUGMISCDUMPFREELISTPAGELIST			PVRSRV_IOWR(PVRSRV_BRIDGE_DEBUGMISC_CMD_FIRST+2)
#define PVRSRV_BRIDGE_DEBUGMISC_PHYSMEMIMPORTSECBUF			PVRSRV_IOWR(PVRSRV_BRIDGE_DEBUGMISC_CMD_FIRST+3)
#define PVRSRV_BRIDGE_DEBUGMISC_POWMONTESTIOCTL			PVRSRV_IOWR(PVRSRV_BRIDGE_DEBUGMISC_CMD_FIRST+4)
#define PVRSRV_BRIDGE_DEBUGMISC_CMD_LAST			(PVRSRV_BRIDGE_DEBUGMISC_CMD_FIRST+4)


/*******************************************
            DebugMiscSLCSetBypassState          
 *******************************************/

/* Bridge in structure for DebugMiscSLCSetBypassState */
typedef struct PVRSRV_BRIDGE_IN_DEBUGMISCSLCSETBYPASSSTATE_TAG
{
	IMG_HANDLE hDevNode;
	IMG_UINT32 ui32Flags;
	IMG_BOOL bIsBypassed;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_DEBUGMISCSLCSETBYPASSSTATE;


/* Bridge out structure for DebugMiscSLCSetBypassState */
typedef struct PVRSRV_BRIDGE_OUT_DEBUGMISCSLCSETBYPASSSTATE_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_DEBUGMISCSLCSETBYPASSSTATE;

/*******************************************
            RGXDebugMiscSetFWLog          
 *******************************************/

/* Bridge in structure for RGXDebugMiscSetFWLog */
typedef struct PVRSRV_BRIDGE_IN_RGXDEBUGMISCSETFWLOG_TAG
{
	IMG_HANDLE hDevNode;
	IMG_UINT32 ui32RGXFWLogType;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXDEBUGMISCSETFWLOG;


/* Bridge out structure for RGXDebugMiscSetFWLog */
typedef struct PVRSRV_BRIDGE_OUT_RGXDEBUGMISCSETFWLOG_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXDEBUGMISCSETFWLOG;

/*******************************************
            RGXDebugMiscDumpFreelistPageList          
 *******************************************/

/* Bridge in structure for RGXDebugMiscDumpFreelistPageList */
typedef struct PVRSRV_BRIDGE_IN_RGXDEBUGMISCDUMPFREELISTPAGELIST_TAG
{
	IMG_HANDLE hDevNode;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXDEBUGMISCDUMPFREELISTPAGELIST;


/* Bridge out structure for RGXDebugMiscDumpFreelistPageList */
typedef struct PVRSRV_BRIDGE_OUT_RGXDEBUGMISCDUMPFREELISTPAGELIST_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXDEBUGMISCDUMPFREELISTPAGELIST;

/*******************************************
            PhysmemImportSecBuf          
 *******************************************/

/* Bridge in structure for PhysmemImportSecBuf */
typedef struct PVRSRV_BRIDGE_IN_PHYSMEMIMPORTSECBUF_TAG
{
	IMG_HANDLE hDevNode;
	IMG_DEVMEM_SIZE_T uiSize;
	IMG_UINT32 ui32Log2PageSize;
	PVRSRV_MEMALLOCFLAGS_T uiFlags;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_PHYSMEMIMPORTSECBUF;


/* Bridge out structure for PhysmemImportSecBuf */
typedef struct PVRSRV_BRIDGE_OUT_PHYSMEMIMPORTSECBUF_TAG
{
	IMG_HANDLE hPMRPtr;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_PHYSMEMIMPORTSECBUF;

/*******************************************
            PowMonTestIoctl          
 *******************************************/

/* Bridge in structure for PowMonTestIoctl */
typedef struct PVRSRV_BRIDGE_IN_POWMONTESTIOCTL_TAG
{
	IMG_UINT32 ui32Cmd;
	IMG_UINT32 ui32In1;
	IMG_UINT32 ui32In2;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_POWMONTESTIOCTL;


/* Bridge out structure for PowMonTestIoctl */
typedef struct PVRSRV_BRIDGE_OUT_POWMONTESTIOCTL_TAG
{
	IMG_UINT32 ui32Out1;
	IMG_UINT32 ui32Out2;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_POWMONTESTIOCTL;

#endif /* COMMON_DEBUGMISC_BRIDGE_H */

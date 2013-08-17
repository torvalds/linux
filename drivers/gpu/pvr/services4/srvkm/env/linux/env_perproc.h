/*************************************************************************/ /*!
@Title          OS specific per process data interface
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Linux per process data
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
#ifndef __ENV_PERPROC_H__
#define __ENV_PERPROC_H__

#include <linux/list.h>
#include <linux/proc_fs.h>

#include "services.h"
#include "handle.h"

#define ION_CLIENT_NAME_SIZE	50
typedef struct _PVRSRV_ENV_PER_PROCESS_DATA_
{
	IMG_HANDLE hBlockAlloc;
	struct proc_dir_entry *psProcDir;
#if defined(SUPPORT_DRI_DRM) && defined(PVR_SECURE_DRM_AUTH_EXPORT)
	struct list_head sDRMAuthListHead;
#endif
#if defined (SUPPORT_ION)
 	struct ion_client *psIONClient;
	IMG_CHAR azIonClientName[ION_CLIENT_NAME_SIZE];
#endif
} PVRSRV_ENV_PER_PROCESS_DATA;

IMG_VOID RemovePerProcessProcDir(PVRSRV_ENV_PER_PROCESS_DATA *psEnvPerProc);

PVRSRV_ERROR LinuxMMapPerProcessConnect(PVRSRV_ENV_PER_PROCESS_DATA *psEnvPerProc);

IMG_VOID LinuxMMapPerProcessDisconnect(PVRSRV_ENV_PER_PROCESS_DATA *psEnvPerProc);
 
PVRSRV_ERROR LinuxMMapPerProcessHandleOptions(PVRSRV_HANDLE_BASE *psHandleBase);

IMG_HANDLE LinuxTerminatingProcessPrivateData(IMG_VOID);

#endif /* __ENV_PERPROC_H__ */

/******************************************************************************
 End of file (env_perproc.h)
******************************************************************************/

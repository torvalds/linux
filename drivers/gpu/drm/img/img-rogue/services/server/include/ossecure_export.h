/*************************************************************************/ /*!
@File
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

#include "img_types.h"
#include "pvrsrv_error.h"
#include "connection_server.h"

/*************************************************************************/ /*!
@Function       OSSecureExport
@Description    Assigns an OS-specific 'token' to allow a resource
                to be securely referenced by another process.
                A process wishing to reference the exported resource
                should call OSSecureImport(), passing its OS-specific
                reference to the same resource.
                For the export/import to be deemed 'secure', the
                implementation should ensure that the OS-specific
                reference can only be meaningfully used by a process
                which is permitted to do so.
@Input          pszName        name of the "class" of new secure file
@Input          pfnReleaseFunc  pointer to the function to be called
                               while closing secure file
@Input          pvData         pointer to the actual resource that
                               is being exported
@Output         phSecure       the returned secure token
@Return         PVRSRV_OK      on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR OSSecureExport(const IMG_CHAR *pszName,
                            PVRSRV_ERROR (*pfnReleaseFunc)(void *),
                            void *pvData,
                            IMG_SECURE_TYPE *phSecure);

/*************************************************************************/ /*!
@Function       OSSecureImport
@Description    Imports an OS-specific 'token' that allows a resource
                allocated by another process to be securely referenced by
                the current process.
@Input          hSecure             the secure token for the resource to
                                    be imported
@Output         ppvData             pointer to the actual resource that
                                    is being referenced
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR OSSecureImport(IMG_SECURE_TYPE hSecure, void **ppvData);

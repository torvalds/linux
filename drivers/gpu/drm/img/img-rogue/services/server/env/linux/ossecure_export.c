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

#include <linux/version.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/anon_inodes.h>
#include <linux/dcache.h>
#include <linux/mount.h>
#include <linux/sched.h>
#include <linux/cred.h>

#include "img_types.h"
#include "img_defs.h"
#include "ossecure_export.h"
#include "private_data.h"
#include "pvr_debug.h"

#include "kernel_compatibility.h"

typedef struct
{
	PVRSRV_ERROR (*pfnReleaseFunc)(void *);
	void *pvData;
} OSSecureFileData;

static IMG_INT _OSSecureFileReleaseFunc(struct inode *psInode,
                                        struct file *psFile)
{
	OSSecureFileData *psSecureFileData = (OSSecureFileData *)psFile->private_data;
	psSecureFileData->pfnReleaseFunc(psSecureFileData->pvData);

	OSFreeMem(psSecureFileData);
	PVR_UNREFERENCED_PARAMETER(psInode);

	return 0;
}

static struct file_operations secure_file_fops = {
	.release	= _OSSecureFileReleaseFunc,
};

PVRSRV_ERROR OSSecureExport(const IMG_CHAR *pszName,
                            PVRSRV_ERROR (*pfnReleaseFunc)(void *),
                            void *pvData,
                            IMG_SECURE_TYPE *phSecure)
{
	struct file *secure_file;
	int secure_fd;
	PVRSRV_ERROR eError;
	OSSecureFileData *psSecureFileData;

	PVR_ASSERT(pfnReleaseFunc != NULL || pvData != NULL);

	psSecureFileData = OSAllocMem(sizeof(*psSecureFileData));
	if (psSecureFileData == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e0;
	}

	psSecureFileData->pvData = pvData;
	psSecureFileData->pfnReleaseFunc = pfnReleaseFunc;

	/* Allocate a fd number */
	secure_fd = get_unused_fd();
	if (secure_fd < 0)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e0;
	}

	/* Create a file with provided name, fops and flags,
	 * also store the private data in the file */
	secure_file = anon_inode_getfile(pszName, &secure_file_fops, psSecureFileData, 0);
	if (IS_ERR(secure_file))
	{
		put_unused_fd(secure_fd);
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e0;
	}

	/* Bind our struct file with it's fd number */
	fd_install(secure_fd, secure_file);

	*phSecure = secure_fd;
	return PVRSRV_OK;

e0:
	OSFreeMem(psSecureFileData);
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

PVRSRV_ERROR OSSecureImport(IMG_SECURE_TYPE hSecure, void **ppvData)
{
	struct file *secure_file;
	PVRSRV_ERROR eError;
	OSSecureFileData *psSecureFileData;

	secure_file = fget(hSecure);
	if (!secure_file)
	{
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto err_out;
	}

	psSecureFileData = (OSSecureFileData *)secure_file->private_data;
	*ppvData = psSecureFileData->pvData;

	fput(secure_file);
	return PVRSRV_OK;

err_out:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

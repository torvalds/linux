/*************************************************************************/ /*!
@File
@Title          Debug driver main file
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
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/kdev_t.h>
#include <linux/pci.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/version.h>

#if defined(LDM_PLATFORM) && !defined(SUPPORT_DRM)
#include <linux/platform_device.h>
#endif

#if defined(LDM_PCI) && !defined(SUPPORT_DRM)
#include <linux/pci.h>
#endif

#include <asm/uaccess.h>

#if defined(SUPPORT_DRM)
#include <drm/drmP.h>
#endif

#include "img_types.h"
#include "linuxsrv.h"
#include "dbgdriv_ioctl.h"
#include "dbgdrvif_srv5.h"
#include "dbgdriv.h"
#include "hostfunc.h"
#include "pvr_debug.h"
#include "pvrmodule.h"
#include "pvr_uaccess.h"

#if defined(SUPPORT_DRM)

#include "pvr_drm_shared.h"
#include "pvr_drm.h"

#else /* defined(SUPPORT_DRM) */

#define DRVNAME "dbgdrv"
MODULE_SUPPORTED_DEVICE(DRVNAME);

static struct class *psDbgDrvClass;

static int AssignedMajorNumber = 0;

long dbgdrv_ioctl(struct file *, unsigned int, unsigned long);
long dbgdrv_ioctl_compat(struct file *, unsigned int, unsigned long);

static int dbgdrv_open(struct inode unref__ * pInode, struct file unref__ * pFile)
{
	return 0;
}

static int dbgdrv_release(struct inode unref__ * pInode, struct file unref__ * pFile)
{
	return 0;
}

static int dbgdrv_mmap(struct file* pFile, struct vm_area_struct* ps_vma)
{
	return 0;
}

static struct file_operations dbgdrv_fops =
{
	.owner          = THIS_MODULE,
	.unlocked_ioctl = dbgdrv_ioctl,
	.compat_ioctl   = dbgdrv_ioctl_compat,
	.open           = dbgdrv_open,
	.release        = dbgdrv_release,
	.mmap           = dbgdrv_mmap,
};

#endif  /* defined(SUPPORT_DRM) */

/* Outward temp buffer used by IOCTL handler allocated once and grows as needed.
 * This optimisation means the debug driver performs less vmallocs/vfrees
 * reducing the chance of kernel vmalloc space exhaustion.
 * Singular out buffer for PDump UM reads is not multi-thread safe and so
 * it now needs a mutex to protect it from multiple simultaneous reads in 
 * the future.
 */
static IMG_CHAR*  g_outTmpBuf = IMG_NULL;
static IMG_UINT32 g_outTmpBufSize = 64*PAGE_SIZE;
static void*      g_pvOutTmpBufMutex = IMG_NULL;

IMG_VOID DBGDrvGetServiceTable(IMG_VOID **fn_table);

IMG_VOID DBGDrvGetServiceTable(IMG_VOID **fn_table)
{
	extern DBGKM_SERVICE_TABLE g_sDBGKMServices;

	*fn_table = &g_sDBGKMServices;
}

#if defined(SUPPORT_DRM)
void dbgdrv_cleanup(void)
#else
void cleanup_module(void)
#endif
{
	if (g_outTmpBuf)
	{
		vfree(g_outTmpBuf);
		g_outTmpBuf = IMG_NULL;
	}

#if !defined(SUPPORT_DRM)
	device_destroy(psDbgDrvClass, MKDEV(AssignedMajorNumber, 0));
	class_destroy(psDbgDrvClass);
	unregister_chrdev(AssignedMajorNumber, DRVNAME);
#endif /* !defined(SUPPORT_DRM) */
#if defined(SUPPORT_DBGDRV_EVENT_OBJECTS)
	HostDestroyEventObjects();
#endif
	HostDestroyMutex(g_pvOutTmpBufMutex);
	HostDestroyMutex(g_pvAPIMutex);
	return;
}

#if defined(SUPPORT_DRM)
IMG_INT dbgdrv_init(void)
#else
int init_module(void)
#endif
{
#if !defined(SUPPORT_DRM)
	struct device *psDev;
#endif

#if !defined(SUPPORT_DRM)
	int err = -EBUSY;
#endif

	/* Init API mutex */
	if ((g_pvAPIMutex=HostCreateMutex()) == IMG_NULL)
	{
		return -ENOMEM;
	}

	/* Init TmpBuf mutex */
	if ((g_pvOutTmpBufMutex=HostCreateMutex()) == IMG_NULL)
	{
		return -ENOMEM;
	}

#if defined(SUPPORT_DBGDRV_EVENT_OBJECTS)
	/*
	 * The current implementation of HostCreateEventObjects on Linux
	 * can never fail, so there is no need to check for error.
	 */
	(void) HostCreateEventObjects();
#endif

#if !defined(SUPPORT_DRM)
	AssignedMajorNumber =
		register_chrdev(AssignedMajorNumber, DRVNAME, &dbgdrv_fops);

	if (AssignedMajorNumber <= 0)
	{
		PVR_DPF((PVR_DBG_ERROR," unable to get major\n"));
		goto ErrDestroyEventObjects;
	}

	/*
	 * This code (using GPL symbols) facilitates automatic device
	 * node creation on platforms with udev (or similar).
	 */
	psDbgDrvClass = class_create(THIS_MODULE, DRVNAME);
	if (IS_ERR(psDbgDrvClass))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: unable to create class (%ld)",
				 __func__, PTR_ERR(psDbgDrvClass)));
		goto ErrUnregisterCharDev;
	}

	psDev = device_create(psDbgDrvClass, NULL, MKDEV(AssignedMajorNumber, 0), NULL, DRVNAME);
	if (IS_ERR(psDev))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: unable to create device (%ld)",
								__func__, PTR_ERR(psDev)));
		goto ErrDestroyClass;
	}
#endif /* !defined(SUPPORT_DRM) */

	return 0;

#if !defined(SUPPORT_DRM)
ErrDestroyEventObjects:
#if defined(SUPPORT_DBGDRV_EVENT_OBJECTS)
	HostDestroyEventObjects();
#endif
ErrUnregisterCharDev:
	unregister_chrdev(AssignedMajorNumber, DRVNAME);
ErrDestroyClass:
	class_destroy(psDbgDrvClass);
	return err;
#endif /* !defined(SUPPORT_DRM) */
}

static IMG_INT dbgdrv_ioctl_work(IMG_VOID *arg, IMG_BOOL bCompat)
{
	IOCTL_PACKAGE *pIP = (IOCTL_PACKAGE *) arg;
	char *buffer, *in, *out;
	unsigned int cmd;
	IMG_VOID *pBufferIn, *pBufferOut;

	if ((pIP->ui32InBufferSize > (PAGE_SIZE >> 1) ) || (pIP->ui32OutBufferSize > (PAGE_SIZE >> 1)))
	{
		PVR_DPF((PVR_DBG_ERROR,"Sizes of the buffers are too large, cannot do ioctl\n"));
		return -1;
	}

	buffer = (char *) HostPageablePageAlloc(1);
	if (!buffer)
	{
		PVR_DPF((PVR_DBG_ERROR,"Failed to allocate buffer, cannot do ioctl\n"));
		return -EFAULT;
	}

	in = buffer;
	out = buffer + (PAGE_SIZE >>1);

	pBufferIn = WIDEPTR_GET_PTR(pIP->pInBuffer, bCompat);
	pBufferOut = WIDEPTR_GET_PTR(pIP->pOutBuffer, bCompat);

	if (pvr_copy_from_user(in, pBufferIn, pIP->ui32InBufferSize) != 0)
	{
		goto init_failed;
	}

	/* Extra -1 because ioctls start at DEBUG_SERVICE_IOCTL_BASE + 1 */
	cmd = MAKEIOCTLINDEX(pIP->ui32Cmd) - DEBUG_SERVICE_IOCTL_BASE - 1;

	if (pIP->ui32Cmd == DEBUG_SERVICE_READ)
	{
		IMG_UINT32 *pui32BytesCopied = (IMG_UINT32 *)out;
		DBG_OUT_READ *psReadOutParams = (DBG_OUT_READ *)out;
		DBG_IN_READ *psReadInParams = (DBG_IN_READ *)in;
		IMG_VOID *pvOutBuffer;
		PDBG_STREAM psStream;

		psStream = SID2PStream(psReadInParams->hStream);
		if (!psStream)
		{
			goto init_failed;
		}

		/* Serialise IOCTL Read op access to the singular output buffer */
		HostAquireMutex(g_pvOutTmpBufMutex);

		if ((g_outTmpBuf == IMG_NULL) || (psReadInParams->ui32OutBufferSize > g_outTmpBufSize))
		{
			if (psReadInParams->ui32OutBufferSize > g_outTmpBufSize)
			{
				g_outTmpBufSize = psReadInParams->ui32OutBufferSize;
			}
			g_outTmpBuf = vmalloc(g_outTmpBufSize);
			if (!g_outTmpBuf)
			{
				HostReleaseMutex(g_pvOutTmpBufMutex);
				goto init_failed;
			}
		}

		/* Ensure only one thread is allowed into the DBGDriv core at a time */
		HostAquireMutex(g_pvAPIMutex);

		psReadOutParams->ui32DataRead = DBGDrivRead(psStream,
										   psReadInParams->ui32BufID,
										   psReadInParams->ui32OutBufferSize,
										   g_outTmpBuf);
		psReadOutParams->ui32SplitMarker = DBGDrivGetMarker(psStream);

		HostReleaseMutex(g_pvAPIMutex);

		pvOutBuffer = WIDEPTR_GET_PTR(psReadInParams->pui8OutBuffer, bCompat);

		if (pvr_copy_to_user(pvOutBuffer,
						g_outTmpBuf,
						*pui32BytesCopied) != 0)
		{
			HostReleaseMutex(g_pvOutTmpBufMutex);
			goto init_failed;
		}

		HostReleaseMutex(g_pvOutTmpBufMutex);

	}
	else
	{
		(g_DBGDrivProc[cmd])(in, out, bCompat);
	}

	if (copy_to_user(pBufferOut, out, pIP->ui32OutBufferSize) != 0)
	{
		goto init_failed;
	}

	HostPageablePageFree((IMG_VOID *)buffer);
	return 0;

init_failed:
	HostPageablePageFree((IMG_VOID *)buffer);
	return -EFAULT;
}

#if defined(SUPPORT_DRM)
int dbgdrv_ioctl(struct drm_device *dev, IMG_VOID *arg, struct drm_file *pFile)
#else
long dbgdrv_ioctl(struct file *file, unsigned int ioctlCmd, unsigned long arg)
#endif
{
	return dbgdrv_ioctl_work((IMG_VOID *) arg, IMG_FALSE);
}

#if defined(SUPPORT_DRM)
int dbgdrv_ioctl_compat(struct drm_device *dev, IMG_VOID *arg, struct drm_file *pFile)
#else
long dbgdrv_ioctl_compat(struct file *file, unsigned int ioctlCmd, unsigned long arg)
#endif
{
	return dbgdrv_ioctl_work((IMG_VOID *) arg, IMG_TRUE);
}



EXPORT_SYMBOL(DBGDrvGetServiceTable);

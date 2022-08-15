/*************************************************************************/ /*!
@File
@Title          Services firmware load and access routines for Linux
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Device specific functions
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

#include <linux/firmware.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/err.h>

#include "device.h"
#include "module_common.h"
#include "fwload.h"
#include "pvr_debug.h"
#include "srvkm.h"

#if defined(RGX_FW_SIGNED)

#include <linux/verification.h>
#include <linux/module.h>
#include <crypto/public_key.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
#include <linux/module_signature.h>
#else
#define PKEY_ID_PKCS7 2
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0) */

#include "signfw.h"
#endif /* RGX_FW_SIGNED */

struct OS_FW_IMAGE_t
{
	const struct firmware *psFW;
	size_t                 uSignatureSize;
};

#if defined(RGX_FW_SIGNED)

static int OSCheckSignature(const struct FirmwareSignatureHeader *psHeader, size_t uSize)
{
	if (be32_to_cpu(psHeader->ui32SignatureLen) >= uSize - sizeof(*psHeader))
	{
		return -EBADMSG;
	}

	if (psHeader->ui8IDType != PKEY_ID_PKCS7)
	{
		return -ENOPKG;
	}

	if (psHeader->ui8Algo != 0 || psHeader->ui8HashAlgo != 0 ||
	    psHeader->ui8SignerLen != 0 || psHeader->ui8KeyIDLen != 0 ||
	    psHeader->__ui8Padding[0] != 0 || psHeader->__ui8Padding[1] != 0 ||
	    psHeader->__ui8Padding[2] != 0)
	{
		return -EBADMSG;
	}

	return 0;
}

bool OSVerifyFirmware(OS_FW_IMAGE *psFWImage)
{
	const struct firmware *psFW        = psFWImage->psFW;
	const u8              *pui8FWData  = psFW->data;
	size_t                uFWSize      = psFW->size;
	uint32_t              ui32MagicLen = sizeof(MODULE_SIG_STRING) - 1;
	struct FirmwareSignatureHeader sHeader;
	int                            err;

	if (uFWSize <= ui32MagicLen)
	{
		return false;
	}

	/*
	 * Linux Kernel's sign-file utility is primarily intended for signing
	 * modules, and so appends the MODULE_SIG_STRING magic at the end of
	 * the signature. Only proceed with verification if this magic is found.
	 */
	if (memcmp(pui8FWData + uFWSize - ui32MagicLen, MODULE_SIG_STRING, ui32MagicLen) != 0)
	{
		return false;
	}

	uFWSize -= ui32MagicLen;
	if (uFWSize <= sizeof(sHeader))
	{
		return false;
	}

	/*
	 * After the magic, a header is placed which informs about the digest /
	 * crypto algorithm etc. Copy that header and ensure that it has valid
	 * contents (We only support RSA Crypto, SHA Hash, X509 certificate and
	 * PKCS#7 signature).
	 */
	memcpy(&sHeader, pui8FWData + (uFWSize - sizeof(sHeader)), sizeof(sHeader));
	if (OSCheckSignature(&sHeader, uFWSize) != 0)
	{
		return false;
	}

	/*
	 * As all information is now extracted, we can go ahead and ask PKCS
	 * module to verify the sign.
	 */
	uFWSize -= be32_to_cpu(sHeader.ui32SignatureLen) + sizeof(sHeader);
	err = verify_pkcs7_signature(pui8FWData, uFWSize, pui8FWData + uFWSize,
				     be32_to_cpu(sHeader.ui32SignatureLen), NULL,
				     VERIFYING_UNSPECIFIED_SIGNATURE, NULL, NULL);
	if (err == 0)
	{
		psFWImage->uSignatureSize = psFW->size - uFWSize;
		PVR_DPF((PVR_DBG_MESSAGE, "%s: Firmware Successfully Verified",
						__func__));
		return true;
	}

	PVR_DPF((PVR_DBG_WARNING, "%s: Firmware Verification Failed (%d)",
					__func__, err));
	return false;
}

#else /* defined(RGX_FW_SIGNED) */

inline bool OSVerifyFirmware(OS_FW_IMAGE *psFWImage)
{
	return true;
}

#endif /* defined(RGX_FW_SIGNED) */

PVRSRV_ERROR
OSLoadFirmware(PVRSRV_DEVICE_NODE *psDeviceNode, const IMG_CHAR *pszBVNCString,
               bool (*pfnVerifyFirmware)(OS_FW_IMAGE*), OS_FW_IMAGE **ppsFWImage)
{
	const struct firmware *psFW = NULL;
	OS_FW_IMAGE *psFWImage;
	IMG_INT32    res;
	PVRSRV_ERROR eError;

	res = request_firmware(&psFW, pszBVNCString, psDeviceNode->psDevConfig->pvOSDevice);
	if (res != 0)
	{
		release_firmware(psFW);
		if (res == -ENOENT)
		{
			PVR_DPF((PVR_DBG_WARNING, "%s: request_firmware('%s') not found (%d)",
							__func__, pszBVNCString, res));
			eError = PVRSRV_ERROR_NOT_FOUND;
		}
		else
		{
			PVR_DPF((PVR_DBG_WARNING, "%s: request_firmware('%s') not ready (%d)",
							__func__, pszBVNCString, res));
			eError = PVRSRV_ERROR_NOT_READY;
		}
		goto err_exit;
	}

	psFWImage = OSAllocZMem(sizeof(*psFWImage));
	if (psFWImage == NULL)
	{
		PVR_DPF((PVR_DBG_WARNING, "%s: OSAllocZMem('%s') failed.",
						__func__, pszBVNCString));

		release_firmware(psFW);
		eError =  PVRSRV_ERROR_OUT_OF_MEMORY;
		goto err_exit;
	}

	psFWImage->psFW = psFW;
	if (pfnVerifyFirmware != NULL && !pfnVerifyFirmware(psFWImage))
	{
		release_firmware(psFW);
		OSFreeMem(psFWImage);
		eError = PVRSRV_ERROR_NOT_AUTHENTICATED;
		goto err_exit;
	}

	*ppsFWImage = psFWImage;
	return PVRSRV_OK;

err_exit:
	*ppsFWImage = NULL;
	return eError;
}

void
OSUnloadFirmware(OS_FW_IMAGE *psFWImage)
{
	const struct firmware *psFW = psFWImage->psFW;

	release_firmware(psFW);
	OSFreeMem(psFWImage);
}

size_t
OSFirmwareSize(OS_FW_IMAGE *psFWImage)
{
	const struct firmware *psFW = psFWImage->psFW;
	return psFW->size - psFWImage->uSignatureSize;
}

const void *
OSFirmwareData(OS_FW_IMAGE *psFWImage)
{
	const struct firmware *psFW = psFWImage->psFW;

	return psFW->data;
}

/******************************************************************************
 End of file (fwload.c)
******************************************************************************/

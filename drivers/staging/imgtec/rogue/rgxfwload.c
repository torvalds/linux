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
#include "rgxfwload.h"
#include "pvr_debug.h"
#include "srvkm.h"

struct RGXFW
{
	const struct firmware sFW;
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,13,0)) && defined(RGX_FW_SIGNED)

/* The Linux kernel does not support the RSA PSS padding mode. It only
 * supports the legacy PKCS#1 padding mode.
 */
#if defined(RGX_FW_PKCS1_PSS_PADDING)
#error Linux does not support verification of RSA PSS padded signatures
#endif

#include <crypto/public_key.h>
#include <crypto/hash_info.h>
#include <crypto/hash.h>

#include <keys/asymmetric-type.h>
#include <keys/system_keyring.h>

#include "signfw.h"

static bool VerifyFirmware(const struct firmware *psFW)
{
	struct FirmwareSignatureHeader *psHeader;
	struct public_key_signature *psPKS;
	unsigned char *szKeyID, *pcKeyID;
	size_t uDigestSize, uDescSize;
	void *pvSignature, *pvSigner;
	struct crypto_shash *psTFM;
	struct shash_desc *psDesc;
	uint32_t ui32SignatureLen;
	bool bVerified = false;
	key_ref_t hKey;
	uint8_t i;
	int res;

	if (psFW->size < FW_SIGN_BACKWARDS_OFFSET)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Firmware is too small (%zu bytes)",
								__func__, psFW->size));
		goto err_release_firmware;
	}

	psHeader = (struct FirmwareSignatureHeader *)
					(psFW->data + (psFW->size - FW_SIGN_BACKWARDS_OFFSET));

	/* All derived from u8 so can't be exploited to flow out of this page */
	pvSigner    = (u8 *)psHeader + sizeof(struct FirmwareSignatureHeader);
	pcKeyID     = (unsigned char *)((u8 *)pvSigner + psHeader->ui8SignerLen);
	pvSignature = (u8 *)pcKeyID + psHeader->ui8KeyIDLen;

	/* We cannot update KERNEL_RO in-place, so we must copy the len */
	ui32SignatureLen = ntohl(psHeader->ui32SignatureLen);

	if (psHeader->ui8Algo >= PKEY_ALGO__LAST)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Public key algorithm %u is not supported",
								__func__, psHeader->ui8Algo));
		goto err_release_firmware;
	}

	if (psHeader->ui8HashAlgo >= PKEY_HASH__LAST)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Hash algorithm %u is not supported",
								__func__, psHeader->ui8HashAlgo));
		goto err_release_firmware;
	}

	if (psHeader->ui8IDType != PKEY_ID_X509)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Only asymmetric X.509 PKI certificates "
								"are supported", __func__));
		goto err_release_firmware;
	}

	/* Generate a hash of the fw data (including the padding) */

	psTFM = crypto_alloc_shash(hash_algo_name[psHeader->ui8HashAlgo], 0, 0);
	if (IS_ERR(psTFM))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: crypto_alloc_shash() failed (%ld)",
								__func__, PTR_ERR(psTFM)));
		goto err_release_firmware;
	}

	uDescSize = crypto_shash_descsize(psTFM) + sizeof(*psDesc);
	uDigestSize = crypto_shash_digestsize(psTFM);

	psPKS = kzalloc(sizeof(*psPKS) + uDescSize + uDigestSize, GFP_KERNEL);
	if (!psPKS)
		goto err_free_crypto_shash;

	psDesc = (struct shash_desc *)((u8 *)psPKS + sizeof(*psPKS));
	psDesc->tfm = psTFM;
	psDesc->flags = CRYPTO_TFM_REQ_MAY_SLEEP;

	psPKS->pkey_algo = psHeader->ui8Algo;
	psPKS->pkey_hash_algo = psHeader->ui8HashAlgo;

	psPKS->digest = (u8 *)psPKS + sizeof(*psPKS) + uDescSize;
	psPKS->digest_size = uDigestSize;

	res = crypto_shash_init(psDesc);
	if (res < 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: crypto_shash_init() failed (%d)",
								__func__, res));
		goto err_free_pks;
	}

	res = crypto_shash_finup(psDesc, psFW->data, psFW->size - FW_SIGN_BACKWARDS_OFFSET,
							 psPKS->digest);
	if (res < 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: crypto_shash_finup() failed (%d)",
								__func__, res));
		goto err_free_pks;
	}

	/* Populate the MPI with the signature payload */

	psPKS->nr_mpi = 1;
	psPKS->rsa.s = mpi_read_raw_data(pvSignature, ui32SignatureLen);
	if (!psPKS->rsa.s)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: mpi_read_raw_data() failed", __func__));
		goto err_free_pks;
	}

	/* Look up the key we'll use to verify this signature */

	szKeyID = kmalloc(psHeader->ui8SignerLen + 2 +
					  psHeader->ui8KeyIDLen * 2 + 1, GFP_KERNEL);
	if (!szKeyID)
		goto err_free_mpi;

	memcpy(szKeyID, pvSigner, psHeader->ui8SignerLen);

	szKeyID[psHeader->ui8SignerLen + 0] = ':';
	szKeyID[psHeader->ui8SignerLen + 1] = ' ';

	for (i = 0; i < psHeader->ui8KeyIDLen; i++)
		sprintf(&szKeyID[psHeader->ui8SignerLen + 2 + i * 2],
				"%02x", pcKeyID[i]);

	szKeyID[psHeader->ui8SignerLen + 2 + psHeader->ui8KeyIDLen * 2] = 0;

	hKey = keyring_search(make_key_ref(system_trusted_keyring, 1),
						  &key_type_asymmetric, szKeyID);
	if (IS_ERR(hKey))
	{
		PVR_DPF((PVR_DBG_ERROR, "Request for unknown key '%s' (%ld)",
								szKeyID, PTR_ERR(hKey)));
		goto err_free_keyid_string;
	}

	res = verify_signature(key_ref_to_ptr(hKey), psPKS);
	if (res)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Firmware digital signature verification "
								"failed (%d)", __func__, res));
		goto err_put_key;
	}

	PVR_LOG(("Digital signature for '%s' verified successfully.",
			 RGX_FW_FILENAME));
	bVerified = true;
err_put_key:
	key_put(key_ref_to_ptr(hKey));
err_free_keyid_string:
	kfree(szKeyID);
err_free_mpi:
	mpi_free(psPKS->rsa.s);
err_free_pks:
	kfree(psPKS);
err_free_crypto_shash:
	crypto_free_shash(psTFM);
err_release_firmware:
	return bVerified;
}

#else /* defined(RGX_FW_SIGNED) */

static inline bool VerifyFirmware(const struct firmware *psFW)
{
	return true;
}

#endif /* defined(RGX_FW_SIGNED) */

IMG_INTERNAL struct RGXFW *
RGXLoadFirmware(SHARED_DEV_CONNECTION psDeviceNode, const IMG_CHAR *pszBVNCString, const IMG_CHAR *pszBVpNCString)
{
	const struct firmware *psFW;
	int res;

	if(pszBVNCString != NULL)
	{
		res = request_firmware(&psFW, pszBVNCString, psDeviceNode->psDevConfig->pvOSDevice);
		if (res != 0)
		{
			if(pszBVpNCString != NULL)
			{
				PVR_DPF((PVR_DBG_WARNING, "%s: request_firmware('%s') failed (%d), trying '%s'",
										__func__, pszBVNCString, res, pszBVpNCString));
				res = request_firmware(&psFW, pszBVpNCString, psDeviceNode->psDevConfig->pvOSDevice);
			}
			if (res != 0)
			{
				PVR_DPF((PVR_DBG_WARNING, "%s: request_firmware('%s') failed (%d), trying '%s'",
										__func__, pszBVpNCString, res, RGX_FW_FILENAME));
				res = request_firmware(&psFW, RGX_FW_FILENAME, psDeviceNode->psDevConfig->pvOSDevice);
			}
		}
	}
	else
	{
		res = request_firmware(&psFW, RGX_FW_FILENAME, psDeviceNode->psDevConfig->pvOSDevice);
	}
	if (res != 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: request_firmware('%s') failed (%d)",
								__func__, RGX_FW_FILENAME, res));
		return NULL;
	}

	if (!VerifyFirmware(psFW))
	{
		release_firmware(psFW);
		return NULL;
	}

	return (struct RGXFW *)psFW;
}

IMG_INTERNAL void
RGXUnloadFirmware(struct RGXFW *psRGXFW)
{
	const struct firmware *psFW = &psRGXFW->sFW;

	release_firmware(psFW);
}

IMG_INTERNAL size_t
RGXFirmwareSize(struct RGXFW *psRGXFW)
{
#if	defined(PVRSRV_GPUVIRT_GUESTDRV)
	PVR_UNREFERENCED_PARAMETER(psRGXFW);
	return 0;
#else
	const struct firmware *psFW = &psRGXFW->sFW;

	return psFW->size;
#endif
}

IMG_INTERNAL const void *
RGXFirmwareData(struct RGXFW *psRGXFW)
{
	const struct firmware *psFW = &psRGXFW->sFW;

	return psFW->data;
}

/******************************************************************************
 End of file (rgxfwload.c)
******************************************************************************/

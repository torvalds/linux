/*************************************************************************/ /*!
@File
@Title          RGX firmware signature checks
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX firmware interface structures used by srvinit and server
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

#if !defined (__RGX_FWIF_SIG_H__)
#define __RGX_FWIF_SIG_H__

#include "rgxdefs_km.h"

/************************************************************************
* RGX FW signature checks
************************************************************************/

#if defined(PDUMP) && defined(SUPPORT_KERNEL_SRVINIT) && defined(__KERNEL__)

#define SIG_REG_TA_MAX_COUNT	(12)
static RGXFW_REGISTER_LIST asTASigRegList[SIG_REG_TA_MAX_COUNT];
static IMG_UINT32 gui32TASigRegCount = 0;

#define SIG_REG_3D_MAX_COUNT	(6)
static RGXFW_REGISTER_LIST as3DSigRegList[SIG_REG_3D_MAX_COUNT];
static IMG_UINT32	gui323DSigRegCount = 0;

#else

/* List of TA signature and checksum register addresses */
static const RGXFW_REGISTER_LIST asTASigRegList[] =
{	/* Register */						/* Indirect_Reg */			/* Start, End */
#if defined(RGX_FEATURE_SCALABLE_VDM_GPP)
	{RGX_CR_USC_UVB_CHECKSUM,			RGX_CR_BLACKPEARL_INDIRECT,	0, RGX_NUM_PHANTOMS-1},
#else
	{RGX_CR_USC_UVS0_CHECKSUM,			0,							0, 0},
	{RGX_CR_USC_UVS1_CHECKSUM,			0,							0, 0},
	{RGX_CR_USC_UVS2_CHECKSUM,			0,							0, 0},
	{RGX_CR_USC_UVS3_CHECKSUM,			0,							0, 0},
	{RGX_CR_USC_UVS4_CHECKSUM,			0,							0, 0},
	{RGX_CR_USC_UVS5_CHECKSUM,			0,							0, 0},
#endif
#if defined(RGX_FEATURE_SCALABLE_TE_ARCH)
#if defined(RGX_FEATURE_SCALABLE_VDM_GPP)
	{RGX_CR_PPP_CLIP_CHECKSUM,			RGX_CR_BLACKPEARL_INDIRECT,	0, RGX_NUM_PHANTOMS-1},
#else
	{RGX_CR_PPP,						0,							0, 0},
#endif
	{RGX_CR_TE_CHECKSUM,				0,							0, 0},
#else
	{RGX_CR_PPP_SIGNATURE,				0,							0, 0},
	{RGX_CR_TE_SIGNATURE,				0,							0, 0},
#endif
	{RGX_CR_VCE_CHECKSUM,				0,							0, 0},
#if !defined(RGX_FEATURE_PDS_PER_DUST)
	{RGX_CR_PDS_DOUTM_STM_SIGNATURE,	0,							0, 0},
#endif
};


/* List of 3D signature and checksum register addresses */
static const RGXFW_REGISTER_LIST as3DSigRegList[] =
{	/* Register */						/* Indirect_Reg */			/* Start, End */
#if !defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE)
	{RGX_CR_ISP_PDS_CHECKSUM,			0,							0, 0},
	{RGX_CR_ISP_TPF_CHECKSUM,			0,							0, 0},
	{RGX_CR_TFPU_PLANE0_CHECKSUM,		0,							0, 0},
	{RGX_CR_TFPU_PLANE1_CHECKSUM,		0,							0, 0},
	{RGX_CR_PBE_CHECKSUM,				0,							0, 0},
	{RGX_CR_IFPU_ISP_CHECKSUM,			0,							0, 0},
#else
	{RGX_CR_ISP_PDS_CHECKSUM,			RGX_CR_BLACKPEARL_INDIRECT,	0, RGX_NUM_PHANTOMS-1},
	{RGX_CR_ISP_TPF_CHECKSUM,			RGX_CR_BLACKPEARL_INDIRECT,	0, RGX_NUM_PHANTOMS-1},
	{RGX_CR_TFPU_PLANE0_CHECKSUM,		RGX_CR_BLACKPEARL_INDIRECT,	0, RGX_NUM_PHANTOMS-1},
	{RGX_CR_TFPU_PLANE1_CHECKSUM,		RGX_CR_BLACKPEARL_INDIRECT,	0, RGX_NUM_PHANTOMS-1},
	{RGX_CR_PBE_CHECKSUM,				RGX_CR_PBE_INDIRECT,		0, RGX_FEATURE_NUM_CLUSTERS-1},
	{RGX_CR_IFPU_ISP_CHECKSUM,			RGX_CR_BLACKPEARL_INDIRECT,	0, RGX_NUM_PHANTOMS-1},
#endif
};
#endif

#if defined (RGX_FEATURE_RAY_TRACING) || defined(__KERNEL__)
/* List of SHG signature and checksum register addresses */
static const RGXFW_REGISTER_LIST asRTUSigRegList[] =
{	/* Register */						/* Indirect_Reg */			/* Start, End */
	{DPX_CR_RS_PDS_RR_CHECKSUM,				0,							0, 0},
	{RGX_CR_FBA_FC0_CHECKSUM,				0,							0, 0},
	{RGX_CR_FBA_FC1_CHECKSUM,				0,							0, 0},
	{RGX_CR_FBA_FC2_CHECKSUM,				0,							0, 0},
	{RGX_CR_FBA_FC3_CHECKSUM,				0,							0, 0},
	{DPX_CR_RQ_USC_DEBUG,					0,							0, 0},
};

/* List of SHG signature and checksum register addresses */
static const RGXFW_REGISTER_LIST asSHGSigRegList[] =
{	/* Register */						/* Indirect_Reg */			/* Start, End */
	{RGX_CR_SHF_SHG_CHECKSUM,			0,							0, 0},
	{RGX_CR_SHF_VERTEX_BIF_CHECKSUM,	0,							0, 0},
	{RGX_CR_SHF_VARY_BIF_CHECKSUM,		0,							0, 0},
	{RGX_CR_RPM_BIF_CHECKSUM,			0,							0, 0},
	{RGX_CR_SHG_BIF_CHECKSUM,			0,							0, 0},
	{RGX_CR_SHG_FE_BE_CHECKSUM,			0,							0, 0},
};
#endif /* RGX_FEATURE_RAY_TRACING */

#endif /*  __RGX_FWIF_SIG_H__ */

/******************************************************************************
 End of file (rgx_fwif_sig.h)
******************************************************************************/



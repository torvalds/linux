
/**************************************************************************************************************
*
* FILE   :	DHF.C
*
* DATE	:	$Date: 2004/07/19 08:16:14 $   $Revision: 1.2 $
* Original	:	2004/05/28 14:05:34    Revision: 1.36      Tag: hcf7_t20040602_01
* Original	:	2004/05/11 06:22:57    Revision: 1.32      Tag: hcf7_t7_20040513_01
* Original	:	2004/04/15 09:24:42    Revision: 1.28      Tag: hcf7_t7_20040415_01
* Original	:	2004/04/08 15:18:16    Revision: 1.27      Tag: t7_20040413_01
* Original	:	2004/04/01 15:32:55    Revision: 1.25      Tag: t7_20040401_01
* Original	:	2004/03/10 15:39:28    Revision: 1.21      Tag: t20040310_01
* Original	:	2004/03/04 11:03:37    Revision: 1.19      Tag: t20040304_01
* Original	:	2004/03/02 09:27:11    Revision: 1.17      Tag: t20040302_03
* Original	:	2004/02/24 13:00:28    Revision: 1.15      Tag: t20040224_01
* Original	:	2004/02/19 10:57:28    Revision: 1.14      Tag: t20040219_01
* Original	:	2003/11/27 09:00:09    Revision: 1.3      Tag: t20021216_01
*
* AUTHOR :	John Meertens
*			Nico Valster
*
* SPECIFICATION: ........
*
* DESC   :	generic functions to handle the download of NIC firmware
*			Local Support Routines for above procedures
*
*			Customizable via HCFCFG.H, which is included by HCF.H
*
*
*	DHF is (intended to be) platform-independent.
*	DHF is a module that provides a number of routines to download firmware
*	images (the names primary, station, access point, secondary and tertiary
*	are used or have been used) to volatile or nonvolatile memory
*	in WaveLAN/IEEE NICs. To achieve this DHF makes use of the WaveLAN/IEEE
*	WCI as implemented by the HCF-module.
*
*	Download to non-volatile memory is used to update a WaveLAN/IEEE NIC to new
*	firmware. Normally this will be an upgrade to newer firmware, although
*	downgrading to older firmware is possible too.
*
* Note: relative to Asserts, the following can be observed:
*	Since the IFB is not known inside the routine, the macro HCFASSERT is replaced with MMDASSERT.
*	Also the line number reported in the assert is raised by FILE_NAME_OFFSET (10000) to discriminate the
*	DHF Asserts from HCF and MMD asserts.
*
***************************************************************************************************************
*
*
* SOFTWARE LICENSE
*
* This software is provided subject to the following terms and conditions,
* which you should read carefully before using the software.  Using this
* software indicates your acceptance of these terms and conditions.  If you do
* not agree with these terms and conditions, do not use the software.
*
* COPYRIGHT (C) 1999 - 2000 by Lucent Technologies.	All Rights Reserved
* COPYRIGHT (C) 2001 - 2004	by Agere Systems Inc.	All Rights Reserved
* All rights reserved.
*
* Redistribution and use in source or binary forms, with or without
* modifications, are permitted provided that the following conditions are met:
*
* . Redistributions of source code must retain the above copyright notice, this
*    list of conditions and the following Disclaimer as comments in the code as
*    well as in the documentation and/or other materials provided with the
*    distribution.
*
* . Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following Disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* . Neither the name of Agere Systems Inc. nor the names of the contributors
*    may be used to endorse or promote products derived from this software
*    without specific prior written permission.
*
* Disclaimer
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
* INCLUDING, BUT NOT LIMITED TO, INFRINGEMENT AND THE IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  ANY
* USE, MODIFICATION OR DISTRIBUTION OF THIS SOFTWARE IS SOLELY AT THE USERS OWN
* RISK. IN NO EVENT SHALL AGERE SYSTEMS INC. OR CONTRIBUTORS BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, INCLUDING, BUT NOT LIMITED TO, CONTRACT, STRICT
* LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
* OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
* DAMAGE.
*
*
**************************************************************************************************************/

#include "hcf.h"
#include "hcfdef.h"
#include "dhf.h"
#include "mmd.h"

/* to distinguish MMD from HCF asserts by means of line number */
#undef	FILE_NAME_OFFSET
#define FILE_NAME_OFFSET MMD_FILE_NAME_OFFSET
/*-----------------------------------------------------------------------------
 *
 * Defines, data structures, and global variables
 *
 *---------------------------------------------------------------------------*/

/*                    12345678901234 */
char signature[14] = "FUPU7D37dhfwci";

/*-----------------------------------------------------------------------------
 *
 * LTV-records retrieved from the NIC to:
 *	- determine compatibility between NIC and image
 *	- ((setup the buffer size dynamically for non-volatile download (see note below) ))
 *	- supply plugging information contained in the PDA (H-I only)
 *
 *---------------------------------------------------------------------------*/

/* for USB/H1 we needed a smaller value than the CFG_DL_BUF_STRCT reported 8192
	for the time being it seems simpler to always use 2000 for USB/H1 as well as all other cases rather than
	using the "fixed anyway" CFG_DL_BUF_STRCT. */
#define DL_SIZE 2000

/* CFG_IDENTITY_STRCT   	pri_identity	= { LOF(CFG_IDENTITY_STRCT), CFG_PRI_IDENTITY }; */
CFG_SUP_RANGE_STRCT 	mfi_sup        	= { LOF(CFG_SUP_RANGE_STRCT), CFG_NIC_MFI_SUP_RANGE };
CFG_SUP_RANGE_STRCT 	cfi_sup        	= { LOF(CFG_SUP_RANGE_STRCT), CFG_NIC_CFI_SUP_RANGE };
/* Note: could be used rather than the above explained and defined DL_SIZE if need arises
 * CFG_DL_BUF_STRCT    	dl_buf         	= { LOF(CFG_DL_BUF_STRCT), CFG_DL_BUF };
*/

/*-----------------------------------------------------------------------------
 * Array ltv_info stores NIC information (in the form of LTV-records)
 * needed for download. A NULL record indicates the end of the array.
 *---------------------------------------------------------------------------*/

/* The LTV_INFO_STRUCT is needed to save the sizes of the structs, because after a GET_INFO()
 * the len field is changed to the real len of the RID by the called routine.
 * This is only relevant if the DHF used without reloading the driver/utility.
 */

LTV_INFO_STRUCT ltv_info[] = {
	{ (LTVP)&mfi_sup,			LOF(CFG_SUP_RANGE_STRCT) } ,
	{ (LTVP)&cfi_sup,			LOF(CFG_SUP_RANGE_STRCT) } ,
	{ (LTVP) NULL, 				0 }
};


/***********************************************************************************************************/
/***************************************  PROTOTYPES  ******************************************************/
/***********************************************************************************************************/
static int				check_comp_fw(memimage *fw);


/************************************************************************************************************
*.SUBMODULE		int check_comp_fw( memimage *fw )
*.PURPOSE		Checks compatibility of CFI and MFI, NIC as supplier, station/AP firmware image as supplier.
*
*.ARGUMENTS
*   fw         	F/W image to be downloaded
*
*.RETURNS
*   HFC_SUCCESS      	- firmware OK
*   DHF_ERR_INCOMP_FW
*
*.DESCRIPTION
*   This function uses compatibility and identity information that has been
*   retrieved from the card which is currently inserted to check whether the
*   station firmware image to be downloaded is compatible.
*.ENDDOC				END DOCUMENTATION
*************************************************************************************************************/
int
check_comp_fw(memimage *fw)
{
CFG_RANGE20_STRCT  		*p;
int   					rc = HCF_SUCCESS;
CFG_RANGE_SPEC_STRCT *i;

	switch (fw->identity->typ) {
	case CFG_FW_IDENTITY:				/* Station F/W */
	case COMP_ID_FW_AP_FAKE:			/* ;?is this useful (used to be:  CFG_AP_IDENTITY) */
		break;
	default:
		MMDASSERT(DO_ASSERT, fw->identity->typ) 	/* unknown/unsupported firmware_type: */
		rc = DHF_ERR_INCOMP_FW;
		return rc; /* ;? how useful is this anyway,
					*  till that is sorted out might as well violate my own single exit principle
					*/
	}
	p = fw->compat;
	i = NULL;
	while (p->len && i == NULL) {					/* check the MFI ranges */
		if (p->typ  == CFG_MFI_ACT_RANGES_STA) {
			i = mmd_check_comp((void *)p, &mfi_sup);
		}
		p++;
	}
	MMDASSERT(i, 0)	/* MFI: NIC Supplier not compatible with F/W image Actor */
	if (i) {
		p = fw->compat;
		i = NULL;
		while (p->len && i == NULL) {			/* check the CFI ranges */
			if (p->typ  == CFG_CFI_ACT_RANGES_STA) {
				 i = mmd_check_comp((void *)p, &cfi_sup);
			}
			p++;
		}
		MMDASSERT(i, 0)	/* CFI: NIC Supplier not compatible with F/W image Actor */
	}
	if (i == NULL) {
		rc = DHF_ERR_INCOMP_FW;
	}
	return rc;
} /* check_comp_fw */





/*-----------------------------------------------------------------------------
 *
 * Exported functions
 *
 *---------------------------------------------------------------------------*/



/*************************************************************************************************************
*
*.MODULE 		int dhf_download_binary( void *ifbp, memimage *fw )
*.PURPOSE		Downloads a complete (primary, station, or access point) firmware image to the NIC.
*
*.ARGUMENTS
*	ifbp		address of the Interface Block
*   fw         	F/W image to be downloaded
*
*.RETURNS
*   HCF_SUCCESS         	- download completed successfully.
*   DHF_ERR_INCOMP_FW		- firmware not compatible
*
*.DESCRIPTION
*   Initialize global variables
*   Connect to the DHF
*   Check the compatibility of the image (For primary firmware images it is checked first
* 	whether download is necessary).
*   If everything's download the firmware.
*   Disconnect from the DHF.
*
*
*.DIAGRAM
*
*.NOTICE:
	MMDASSERT is unacceptable because some drivers call dhf_download_binary before hcf_connect

* The old comment was:
*.ENDDOC				END DOCUMENTATION
*************************************************************************************************************/
int
dhf_download_binary(memimage *fw)
{
int 			rc = HCF_SUCCESS;
CFG_PROG_STRCT 	*p;
int				i;

	/* validate the image */
	for (i = 0; i < sizeof(signature) && fw->signature[i] == signature[i]; i++)
		; /* NOP */
	if (i != sizeof(signature) 		||
		 fw->signature[i] != 0x01   	||
		 /* test for Little/Big Endian Binary flag */
		 fw->signature[i+1] != (/* HCF_BIG_ENDIAN ? 'B' : */ 'L'))
		rc = DHF_ERR_INCOMP_FW;
	else {					/* Little Endian Binary format */
		fw->codep    = (CFG_PROG_STRCT FAR*)((char *)fw->codep + (hcf_32)fw);
		fw->identity = (CFG_IDENTITY_STRCT FAR*)((char *)fw->identity + (hcf_32)fw);
		fw->compat   = (CFG_RANGE20_STRCT FAR*)((char *)fw->compat + (hcf_32)fw);
		for (i = 0; fw->p[i]; i++)
			fw->p[i] = ((char *)fw->p[i] + (hcf_32)fw);
		p = fw->codep;
		while (p->len) {
			p->host_addr = (char *)p->host_addr + (hcf_32)fw;
			p++;
		}
	}
	return rc;
}   /* dhf_download_binary */


/*************************************************************************************************************
*
*.MODULE 		int dhf_download_fw( void *ifbp, memimage *fw )
*.PURPOSE		Downloads a complete (primary or tertiary) firmware image to the NIC.
*
*.ARGUMENTS
*	ifbp		address of the Interface Block
*   fw     		F/W image to be downloaded
*
*.RETURNS
*	HCF_SUCCESS        	- download completed successfully.
*	HCF_ERR_NO_NIC     	- no NIC present
*	DHF_ERR_INCOMP_FW 	- firmware not compatible
*
*.DESCRIPTION
* - check the signature of the image
* - get the compatibility information from the components on the NIC
*	  - Primary Firmware Identity
*	  -	Modem - Firmware I/F
*	  -	Controller - Firmware I/F
*!! - if necessary ( i.e. H-I) get the PDA contents from the NIC
* - check the compatibility of the MFI and CFI of the NIC with the F/W image
*	Note: the Primary F/W compatibility is only relevant for the "running" HCF and is already verified in
*	hcf_connect
*!! -	if necessary ( i.e. H-I)
*!!	  -	verify the sumcheck of the PDA
*!!	  -	plug the image (based on the PDA and the default plug records)
* - loop over all the download LTVs in the image which consists of a sequence of
*	  - CFG_PROG_VOLATILE/CFG_PROG_NON_VOLATILE
*	  - 1 or more sequences of CFG_PROG_ADDR, CFG_PROG_DATA,....,CFG_PROG_DATA
*	  -	CFG_PROG_STOP
*
*.DIAGRAM
*
*.NOTICE
* The old comment was:
*	// Download primary firmware if necessary and allowed. This is done silently (without telling
*	// the user) and only if the firmware in the download image is newer than the firmware in the
*	// card.  In Major version 4 of the primary firmware functions of Hermes and Shark were
*	// combined. Prior to that two separate versions existed. We only have to download primary
*	// firmware if major version of primary firmware in the NIC < 4.
*	//		download = pri_identity.version_major < 4;
*	//		if ( download ) {
*	//			rc = check_comp_primary( fw );
*	//		}
* It is my understanding that Pri Variant 1 must be updated by Pri Variant 2. The test on
* major version < 4 should amount to the same result but be "principally" less correct
* In deliberation with the Architecture team, it was decided that this upgrade for old H-I
* NICs, is an aspect which belongs on the WSU level not on the DHF level
*
*.ENDDOC				END DOCUMENTATION
*************************************************************************************************************/
int
dhf_download_fw(void *ifbp, memimage *fw)
{
int 				rc = HCF_SUCCESS;
LTV_INFO_STRUCT_PTR pp = ltv_info;
CFG_PROG_STRCT 		*p = fw->codep;
LTVP 				ltvp;
int					i;

	MMDASSERT(fw != NULL, 0)
	/* validate the image */
	for (i = 0; i < sizeof(signature) && fw->signature[i] == signature[i]; i++)
		; /* NOP */
	if (i != sizeof(signature) 		||
		 fw->signature[i] != 0x01		||
		 /* check for binary image */
		 (fw->signature[i+1] != 'C' && fw->signature[i+1] != (/*HCF_BIG_ENDIAN ? 'B' : */ 'L')))
		 rc = DHF_ERR_INCOMP_FW;

/*	Retrieve all information needed for download from the NIC */
	while ((rc == HCF_SUCCESS) && ((ltvp = pp->ltvp) != NULL)) {
		ltvp->len = pp++->len;	/* Set len to original len. This len is changed to real len by GET_INFO() */
		rc = GET_INFO(ltvp);
		MMDASSERT(rc == HCF_SUCCESS, rc)
		MMDASSERT(rc == HCF_SUCCESS, ltvp->typ)
		MMDASSERT(rc == HCF_SUCCESS, ltvp->len)
	}
	if (rc == HCF_SUCCESS)
		rc = check_comp_fw(fw);
	if (rc == HCF_SUCCESS) {
		while (rc == HCF_SUCCESS && p->len) {
			rc = PUT_INFO(p);
			p++;
		}
	}
	MMDASSERT(rc == HCF_SUCCESS, rc)
	return rc;
}   /* dhf_download_fw */



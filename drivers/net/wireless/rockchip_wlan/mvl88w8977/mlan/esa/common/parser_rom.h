/** @file parser_rom.h
 *
 *  @brief This file contains the data structrue for iepointer and declare the parse function
 *
 * Copyright (C) 2014-2017, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

/******************************************************
Change log:
    03/07/2014: Initial version
******************************************************/
#ifndef PARSER_ROM_H__
#define PARSER_ROM_H__
#include "IEEE_types.h"

typedef enum {
	VendSpecIE_Other = 0,
	VendSpecIE_WMM_Info,
	VendSpecIE_WMM_Param,
	VendSpecIE_WPA,
	VendSpecIE_WPS,
	VendSpecIE_TSPEC,
	VendSpecIE_SsIdL,
	VendSpecIE_WFD,

	VendSpecIE_HT_Cap,
	VendSpecIE_HT_Info,

} VendorSpecificIEType_e;

typedef struct {
	/* IMPORTANT: please read before you modify this struct:
	   Some of the members of this struct are used in ROM code.
	   Therefore, please do not change any existing field, including
	   its name and type. If you want to add a new element into
	   this struct add it at the end.
	 */
	IEEEtypes_SsIdElement_t *pSsid;
	IEEEtypes_TimElement_t *pTim;
	IEEEtypes_WPAElement_t *pWpa;
	IEEEtypes_WMM_InfoElement_t *pWmmInfo;
	IEEEtypes_WMM_ParamElement_t *pWmmParam;
	IEEEtypes_DsParamElement_t *pDsParam;
	IEEEtypes_SuppRatesElement_t *pSupportedRates;
	IEEEtypes_ExtSuppRatesElement_t *pExtSupportedRates;
	IEEEtypes_ERPInfoElement_t *pErpInfo;
	IEEEtypes_IbssParamElement_t *pIbssParam;
	IEEEtypes_CountryInfoElement_t *pCountry;

	IEEEtypes_MobilityDomainElement_t *pMdie;

	IEEEtypes_RSNElement_t *pRsn;

	IEEEtypes_HT_Capability_t *pHtCap;
	IEEEtypes_HT_Information_t *pHtInfo;
	IEEEtypes_20N40_BSS_Coexist_t *p2040Coexist;
	IEEEtypes_OBSS_ScanParam_t *pHtScanParam;
	IEEEtypes_ExtCapability_t *pExtCap;

	IEEEtypes_WPSElement_t *pWps;
	IEEEtypes_WAPIElement_t *pWapi;

} IEPointers_t;

typedef struct {
	/* IMPORTANT: please read before you modify this struct:
	   Some of the members of this struct are used in ROM code.
	   Therefore, please do not change any existing field, including
	   its name and type. If you want to add a new element into
	   this struct add it at the end.
	 */
	IEEEtypes_SsIdElement_t *pSsid;
	IEEEtypes_TimElement_t *pTim;
	IEEEtypes_DsParamElement_t *pDsParam;

	IEEEtypes_CountryInfoElement_t *pCountry;

	UINT8 numSsIdLs;
	IEEEtypes_SsIdLElement_t *pSsIdL;	/* Only the first SSIDL found,
						 **   need iterator to get next since
						 **   multiple may be in beacon
						 */
} ScanIePointers_t;

typedef struct {
	/* IMPORTANT: please read before you modify this struct:
	   Some of the members of this struct are used in ROM code.
	   Therefore, please do not change any existing field, including
	   its name and type. If you want to add a new element into
	   this struct add it at the end.
	 */
	IEEEtypes_SsIdElement_t *pSsid;
	IEEEtypes_DsParamElement_t *pDsParam;

	IEEEtypes_CountryInfoElement_t *pCountry;
	IEEEtypes_ApChanRptElement_t *pApChanRpt;
	IEEEtypes_PowerConstraintElement_t *pPwrCon;

	IEEEtypes_SuppRatesElement_t *pSupportedRates;
	IEEEtypes_ExtSuppRatesElement_t *pExtSupportedRates;

	IEEEtypes_WPAElement_t *pWpa;
	IEEEtypes_WMM_InfoElement_t *pWmmInfo;
	IEEEtypes_WMM_ParamElement_t *pWmmParam;

	IEEEtypes_MobilityDomainElement_t *pMdie;

	IEEEtypes_RSNElement_t *pRsn;

	IEEEtypes_HT_Information_t *pHtInfo;
	IEEEtypes_HT_Capability_t *pHtCap;
	IEEEtypes_20N40_BSS_Coexist_t *p2040Coexist;
	IEEEtypes_OBSS_ScanParam_t *pHtScanParam;
	IEEEtypes_ExtCapability_t *pExtCap;

} AssocIePointers_t;
extern BOOLEAN ROM_parser_getIEPtr(void *priv, uint8 *pIe,
				   IEPointers_t *pIePointers);
extern BOOLEAN ROM_parser_getAssocIEPtr(void *priv, uint8 *pIe,
					AssocIePointers_t *pIePointers);

#endif // _PARSER_ROM_H_

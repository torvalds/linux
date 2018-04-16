/** @file parser.h
 *
 *  @brief This file contains definitions of 802.11 Management Frames
 *               and Information Element Parsing
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
#ifndef _PARSER_H_
#define _PARSER_H_

#include "IEEE_types.h"
#include "parser_rom.h"

typedef struct {
	IEEEtypes_HT_Capability_t *pHtCap;
	IEEEtypes_HT_Information_t *pHtInfo;
	IEEEtypes_20N40_BSS_Coexist_t *p2040Coexist;
	IEEEtypes_OBSS_ScanParam_t *pHtScanParam;
	IEEEtypes_ExtCapability_t *pHtExtCap;
} dot11nIEPointers_t;

#ifdef DOT11AC
typedef struct {
	IEEEtypes_VHT_Capability_t *pVhtCap;
	IEEEtypes_VHT_Operation_t *pVhtOper;
} dot11acIEPointers_t;
#endif

#ifdef TDLS
typedef struct {
	IEEEtypes_MobilityDomainElement_t *pMdie;
	IEEEtypes_FastBssTransElement_t *pFtie;
	IEEEtypes_RSNElement_t *pRsn;
	IEEEtypes_TimeoutIntervalElement_t *pTie[2];
	IEEEtypes_RICDataElement_t *pFirstRdie;

} Dot11rIePointers_t;
#endif
extern VendorSpecificIEType_e IsWMMElement(void *priv, UINT8 *pBuffer);
extern VendorSpecificIEType_e IsWPAElement(void *priv, UINT8 *pBuffer);

extern int ieBufValidate(UINT8 *pIe, int bufLen);

extern int GetIEPointers(void *priv, UINT8 *pIe,
			 int bufLen, IEPointers_t *pIePointers);

extern BOOLEAN parser_getAssocIEs(void *priv, UINT8 *pIe,
				  int bufLen, AssocIePointers_t *pIePointers);

extern
IEEEtypes_InfoElementHdr_t *parser_getSpecificIE(IEEEtypes_ElementId_e elemId,
						 UINT8 *pIe, int bufLen);

extern UINT8 parser_countNumInfoElements(UINT8 *pIe, int bufLen);

#endif // _PARSER_H_

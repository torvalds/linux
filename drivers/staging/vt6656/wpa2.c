/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 * File: wpa2.c
 *
 * Purpose: Handles the Basic Service Set & Node Database functions
 *
 * Functions:
 *
 * Revision History:
 *
 * Author: Yiching Chen
 *
 * Date: Oct. 4, 2004
 *
 */

#include "device.h"
#include "wpa2.h"

/*---------------------  Static Definitions -------------------------*/
static int          msglevel                =MSG_LEVEL_INFO;
//static int          msglevel                =MSG_LEVEL_DEBUG;
/*---------------------  Static Classes  ----------------------------*/

/*---------------------  Static Variables  --------------------------*/

const u8 abyOUIGK[4]      = { 0x00, 0x0F, 0xAC, 0x00 };
const u8 abyOUIWEP40[4]   = { 0x00, 0x0F, 0xAC, 0x01 };
const u8 abyOUIWEP104[4]  = { 0x00, 0x0F, 0xAC, 0x05 };
const u8 abyOUITKIP[4]    = { 0x00, 0x0F, 0xAC, 0x02 };
const u8 abyOUICCMP[4]    = { 0x00, 0x0F, 0xAC, 0x04 };

const u8 abyOUI8021X[4]   = { 0x00, 0x0F, 0xAC, 0x01 };
const u8 abyOUIPSK[4]     = { 0x00, 0x0F, 0xAC, 0x02 };


/*---------------------  Static Functions  --------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

/*+
 *
 * Description:
 *    Clear RSN information in BSSList.
 *
 * Parameters:
 *  In:
 *      pBSSNode - BSS list.
 *  Out:
 *      none
 *
 * Return Value: none.
 *
-*/
void
WPA2_ClearRSN (
     PKnownBSS        pBSSNode
    )
{
    int ii;

    pBSSNode->bWPA2Valid = false;

    pBSSNode->byCSSGK = WLAN_11i_CSS_CCMP;
    for (ii=0; ii < 4; ii ++)
        pBSSNode->abyCSSPK[ii] = WLAN_11i_CSS_CCMP;
    pBSSNode->wCSSPKCount = 1;
    for (ii=0; ii < 4; ii ++)
        pBSSNode->abyAKMSSAuthType[ii] = WLAN_11i_AKMSS_802_1X;
    pBSSNode->wAKMSSAuthCount = 1;
    pBSSNode->sRSNCapObj.bRSNCapExist = false;
    pBSSNode->sRSNCapObj.wRSNCap = 0;
}

/*+
 *
 * Description:
 *    Parse RSN IE.
 *
 * Parameters:
 *  In:
 *      pBSSNode - BSS list.
 *      pRSN - Pointer to the RSN IE.
 *  Out:
 *      none
 *
 * Return Value: none.
 *
-*/
void
WPA2vParseRSN (
     PKnownBSS        pBSSNode,
     PWLAN_IE_RSN     pRSN
    )
{
    int                 i, j;
    WORD                m = 0, n = 0;
    u8 *               pbyOUI;
    bool                bUseGK = false;

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"WPA2_ParseRSN: [%d]\n", pRSN->len);

    WPA2_ClearRSN(pBSSNode);

    if (pRSN->len == 2) { // ver(2)
        if ((pRSN->byElementID == WLAN_EID_RSN) && (pRSN->wVersion == 1)) {
            pBSSNode->bWPA2Valid = true;
        }
        return;
    }

    if (pRSN->len < 6) { // ver(2) + GK(4)
        // invalid CSS, P802.11i/D10.0, p31
        return;
    }

    // information element header makes sense
    if ((pRSN->byElementID == WLAN_EID_RSN) &&
        (pRSN->wVersion == 1)) {

        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"Legal 802.11i RSN\n");

        pbyOUI = &(pRSN->abyRSN[0]);
        if ( !memcmp(pbyOUI, abyOUIWEP40, 4))
            pBSSNode->byCSSGK = WLAN_11i_CSS_WEP40;
        else if ( !memcmp(pbyOUI, abyOUITKIP, 4))
            pBSSNode->byCSSGK = WLAN_11i_CSS_TKIP;
        else if ( !memcmp(pbyOUI, abyOUICCMP, 4))
            pBSSNode->byCSSGK = WLAN_11i_CSS_CCMP;
        else if ( !memcmp(pbyOUI, abyOUIWEP104, 4))
            pBSSNode->byCSSGK = WLAN_11i_CSS_WEP104;
        else if ( !memcmp(pbyOUI, abyOUIGK, 4)) {
            // invalid CSS, P802.11i/D10.0, p32
            return;
        } else
            // any vendor checks here
            pBSSNode->byCSSGK = WLAN_11i_CSS_UNKNOWN;

        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"802.11i CSS: %X\n", pBSSNode->byCSSGK);

        if (pRSN->len == 6) {
            pBSSNode->bWPA2Valid = true;
            return;
        }

        if (pRSN->len >= 8) { // ver(2) + GK(4) + PK count(2)
            pBSSNode->wCSSPKCount = *((PWORD) &(pRSN->abyRSN[4]));
            j = 0;
            pbyOUI = &(pRSN->abyRSN[6]);

            for (i = 0; (i < pBSSNode->wCSSPKCount) && (j < sizeof(pBSSNode->abyCSSPK)/sizeof(u8)); i++) {

                if (pRSN->len >= 8+i*4+4) { // ver(2)+GK(4)+PKCnt(2)+PKS(4*i)
                    if ( !memcmp(pbyOUI, abyOUIGK, 4)) {
                        pBSSNode->abyCSSPK[j++] = WLAN_11i_CSS_USE_GROUP;
                        bUseGK = true;
                    } else if ( !memcmp(pbyOUI, abyOUIWEP40, 4)) {
                        // Invalid CSS, continue parsing
                    } else if ( !memcmp(pbyOUI, abyOUITKIP, 4)) {
                        if (pBSSNode->byCSSGK != WLAN_11i_CSS_CCMP)
                            pBSSNode->abyCSSPK[j++] = WLAN_11i_CSS_TKIP;
                        else
                            ; // Invalid CSS, continue parsing
                    } else if ( !memcmp(pbyOUI, abyOUICCMP, 4)) {
                        pBSSNode->abyCSSPK[j++] = WLAN_11i_CSS_CCMP;
                    } else if ( !memcmp(pbyOUI, abyOUIWEP104, 4)) {
                        // Invalid CSS, continue parsing
                    } else {
                        // any vendor checks here
                        pBSSNode->abyCSSPK[j++] = WLAN_11i_CSS_UNKNOWN;
                    }
                    pbyOUI += 4;
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"abyCSSPK[%d]: %X\n", j-1, pBSSNode->abyCSSPK[j-1]);
                } else
                    break;
            } //for

            if (bUseGK == true) {
                if (j != 1) {
                    // invalid CSS, This should be only PK CSS.
                    return;
                }
                if (pBSSNode->byCSSGK == WLAN_11i_CSS_CCMP) {
                    // invalid CSS, If CCMP is enable , PK can't be CSSGK.
                    return;
                }
            }
            if ((pBSSNode->wCSSPKCount != 0) && (j == 0)) {
                // invalid CSS, No valid PK.
                return;
            }
            pBSSNode->wCSSPKCount = (WORD)j;
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"wCSSPKCount: %d\n", pBSSNode->wCSSPKCount);
        }

        m = *((PWORD) &(pRSN->abyRSN[4]));

        if (pRSN->len >= 10+m*4) { // ver(2) + GK(4) + PK count(2) + PKS(4*m) + AKMSS count(2)
            pBSSNode->wAKMSSAuthCount = *((PWORD) &(pRSN->abyRSN[6+4*m]));
            j = 0;
            pbyOUI = &(pRSN->abyRSN[8+4*m]);
            for (i = 0; (i < pBSSNode->wAKMSSAuthCount) && (j < sizeof(pBSSNode->abyAKMSSAuthType)/sizeof(u8)); i++) {
                if (pRSN->len >= 10+(m+i)*4+4) { // ver(2)+GK(4)+PKCnt(2)+PKS(4*m)+AKMSS(2)+AKS(4*i)
                    if ( !memcmp(pbyOUI, abyOUI8021X, 4))
                        pBSSNode->abyAKMSSAuthType[j++] = WLAN_11i_AKMSS_802_1X;
                    else if ( !memcmp(pbyOUI, abyOUIPSK, 4))
                        pBSSNode->abyAKMSSAuthType[j++] = WLAN_11i_AKMSS_PSK;
                    else
                        // any vendor checks here
                        pBSSNode->abyAKMSSAuthType[j++] = WLAN_11i_AKMSS_UNKNOWN;
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"abyAKMSSAuthType[%d]: %X\n", j-1, pBSSNode->abyAKMSSAuthType[j-1]);
                } else
                    break;
            }
            pBSSNode->wAKMSSAuthCount = (WORD)j;
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"wAKMSSAuthCount: %d\n", pBSSNode->wAKMSSAuthCount);

            n = *((PWORD) &(pRSN->abyRSN[6+4*m]));
            if (pRSN->len >= 12+4*m+4*n) { // ver(2)+GK(4)+PKCnt(2)+PKS(4*m)+AKMSSCnt(2)+AKMSS(4*n)+Cap(2)
                pBSSNode->sRSNCapObj.bRSNCapExist = true;
                pBSSNode->sRSNCapObj.wRSNCap = *((PWORD) &(pRSN->abyRSN[8+4*m+4*n]));
            }
        }
        //ignore PMKID lists bcs only (Re)Assocrequest has this field
        pBSSNode->bWPA2Valid = true;
    }
}


/*+
 *
 * Description:
 *    Set WPA IEs
 *
 * Parameters:
 *  In:
 *      pMgmtHandle - Pointer to management object
 *  Out:
 *      pRSNIEs     - Pointer to the RSN IE to set.
 *
 * Return Value: length of IEs.
 *
-*/
unsigned int WPA2uSetIEs(void *pMgmtHandle, PWLAN_IE_RSN pRSNIEs)
{
	struct vnt_manager *pMgmt = (struct vnt_manager *)pMgmtHandle;
	u8 *pbyBuffer = NULL;
	int ii = 0;
	u16 *pwPMKID = NULL;

	if (pRSNIEs == NULL)
		return 0;

    if (((pMgmt->eAuthenMode == WMAC_AUTH_WPA2) ||
         (pMgmt->eAuthenMode == WMAC_AUTH_WPA2PSK)) &&
        (pMgmt->pCurrBSS != NULL)) {
        /* WPA2 IE */
        pbyBuffer = (u8 *) pRSNIEs;
        pRSNIEs->byElementID = WLAN_EID_RSN;
        pRSNIEs->len = 6; //Version(2)+GK(4)
        pRSNIEs->wVersion = 1;
        //Group Key Cipher Suite
        pRSNIEs->abyRSN[0] = 0x00;
        pRSNIEs->abyRSN[1] = 0x0F;
        pRSNIEs->abyRSN[2] = 0xAC;
        if (pMgmt->byCSSGK == KEY_CTL_WEP) {
            pRSNIEs->abyRSN[3] = pMgmt->pCurrBSS->byCSSGK;
        } else if (pMgmt->byCSSGK == KEY_CTL_TKIP) {
            pRSNIEs->abyRSN[3] = WLAN_11i_CSS_TKIP;
        } else if (pMgmt->byCSSGK == KEY_CTL_CCMP) {
            pRSNIEs->abyRSN[3] = WLAN_11i_CSS_CCMP;
        } else {
            pRSNIEs->abyRSN[3] = WLAN_11i_CSS_UNKNOWN;
        }

        // Pairwise Key Cipher Suite
        pRSNIEs->abyRSN[4] = 1;
        pRSNIEs->abyRSN[5] = 0;
        pRSNIEs->abyRSN[6] = 0x00;
        pRSNIEs->abyRSN[7] = 0x0F;
        pRSNIEs->abyRSN[8] = 0xAC;
        if (pMgmt->byCSSPK == KEY_CTL_TKIP) {
            pRSNIEs->abyRSN[9] = WLAN_11i_CSS_TKIP;
        } else if (pMgmt->byCSSPK == KEY_CTL_CCMP) {
            pRSNIEs->abyRSN[9] = WLAN_11i_CSS_CCMP;
        } else if (pMgmt->byCSSPK == KEY_CTL_NONE) {
            pRSNIEs->abyRSN[9] = WLAN_11i_CSS_USE_GROUP;
        } else {
            pRSNIEs->abyRSN[9] = WLAN_11i_CSS_UNKNOWN;
        }
        pRSNIEs->len += 6;

        // Auth Key Management Suite
        pRSNIEs->abyRSN[10] = 1;
        pRSNIEs->abyRSN[11] = 0;
        pRSNIEs->abyRSN[12] = 0x00;
        pRSNIEs->abyRSN[13] = 0x0F;
        pRSNIEs->abyRSN[14] = 0xAC;
        if (pMgmt->eAuthenMode == WMAC_AUTH_WPA2PSK) {
            pRSNIEs->abyRSN[15] = WLAN_11i_AKMSS_PSK;
        } else if (pMgmt->eAuthenMode == WMAC_AUTH_WPA2) {
            pRSNIEs->abyRSN[15] = WLAN_11i_AKMSS_802_1X;
        } else {
            pRSNIEs->abyRSN[15] = WLAN_11i_AKMSS_UNKNOWN;
        }
        pRSNIEs->len +=6;

        // RSN Capabilites
        if (pMgmt->pCurrBSS->sRSNCapObj.bRSNCapExist == true) {
            memcpy(&pRSNIEs->abyRSN[16], &pMgmt->pCurrBSS->sRSNCapObj.wRSNCap, 2);
        } else {
            pRSNIEs->abyRSN[16] = 0;
            pRSNIEs->abyRSN[17] = 0;
        }
        pRSNIEs->len +=2;

	if ((pMgmt->gsPMKIDCache.BSSIDInfoCount > 0) &&
	    (pMgmt->bRoaming == true) &&
            (pMgmt->eAuthenMode == WMAC_AUTH_WPA2)) {
		/* RSN PMKID, pointer to PMKID count */
		pwPMKID = (PWORD)(&pRSNIEs->abyRSN[18]);
		*pwPMKID = 0;                     /* Initialize PMKID count */
		pbyBuffer = &pRSNIEs->abyRSN[20];    /* Point to PMKID list */
		for (ii = 0; ii < pMgmt->gsPMKIDCache.BSSIDInfoCount; ii++) {
			if (!memcmp(&pMgmt->
				    gsPMKIDCache.BSSIDInfo[ii].abyBSSID[0],
				    pMgmt->abyCurrBSSID,
				    ETH_ALEN)) {
				(*pwPMKID)++;
				memcpy(pbyBuffer,
			pMgmt->gsPMKIDCache.BSSIDInfo[ii].abyPMKID,
				       16);
				pbyBuffer += 16;
			}
		}
            if (*pwPMKID != 0) {
                pRSNIEs->len += (2 + (*pwPMKID)*16);
            } else {
                pbyBuffer = &pRSNIEs->abyRSN[18];
            }
        }
        return(pRSNIEs->len + WLAN_IEHDR_LEN);
    }
    return(0);
}

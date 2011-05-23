/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************

    Module Name:
	cmm_cfg.c

    Abstract:
    Ralink WiFi Driver configuration related subroutines

    Revision History:
    Who          When          What
    ---------    ----------    ----------------------------------------------
*/

#include "../rt_config.h"

char *GetPhyMode(int Mode)
{
	switch (Mode) {
	case MODE_CCK:
		return "CCK";

	case MODE_OFDM:
		return "OFDM";
	case MODE_HTMIX:
		return "HTMIX";

	case MODE_HTGREENFIELD:
		return "GREEN";
	default:
		return "N/A";
	}
}

char *GetBW(int BW)
{
	switch (BW) {
	case BW_10:
		return "10M";

	case BW_20:
		return "20M";
	case BW_40:
		return "40M";
	default:
		return "N/A";
	}
}

/*
    ==========================================================================
    Description:
        Set Country Region to pAd->CommonCfg.CountryRegion.
        This command will not work, if the field of CountryRegion in eeprom is programmed.

    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
int RT_CfgSetCountryRegion(struct rt_rtmp_adapter *pAd, char *arg, int band)
{
	long region, regionMax;
	u8 *pCountryRegion;

	region = simple_strtol(arg, 0, 10);

	if (band == BAND_24G) {
		pCountryRegion = &pAd->CommonCfg.CountryRegion;
		regionMax = REGION_MAXIMUM_BG_BAND;
	} else {
		pCountryRegion = &pAd->CommonCfg.CountryRegionForABand;
		regionMax = REGION_MAXIMUM_A_BAND;
	}

	/* TODO: Is it neccesay for following check??? */
	/* Country can be set only when EEPROM not programmed */
	if (*pCountryRegion & 0x80) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("CfgSetCountryRegion():CountryRegion in eeprom was programmed\n"));
		return FALSE;
	}

	if ((region >= 0) && (region <= REGION_MAXIMUM_BG_BAND)) {
		*pCountryRegion = (u8)region;
	} else if ((region == REGION_31_BG_BAND) && (band == BAND_24G)) {
		*pCountryRegion = (u8)region;
	} else {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("CfgSetCountryRegion():region(%ld) out of range!\n",
			  region));
		return FALSE;
	}

	return TRUE;

}

/*
    ==========================================================================
    Description:
        Set Wireless Mode
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
int RT_CfgSetWirelessMode(struct rt_rtmp_adapter *pAd, char *arg)
{
	int MaxPhyMode = PHY_11G;
	long WirelessMode;

	MaxPhyMode = PHY_11N_5G;

	WirelessMode = simple_strtol(arg, 0, 10);
	if (WirelessMode <= MaxPhyMode) {
		pAd->CommonCfg.PhyMode = WirelessMode;
		return TRUE;
	}

	return FALSE;

}

int RT_CfgSetShortSlot(struct rt_rtmp_adapter *pAd, char *arg)
{
	long ShortSlot;

	ShortSlot = simple_strtol(arg, 0, 10);

	if (ShortSlot == 1)
		pAd->CommonCfg.bUseShortSlotTime = TRUE;
	else if (ShortSlot == 0)
		pAd->CommonCfg.bUseShortSlotTime = FALSE;
	else
		return FALSE;	/*Invalid argument */

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set WEP KEY base on KeyIdx
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
int RT_CfgSetWepKey(struct rt_rtmp_adapter *pAd,
		    char *keyString,
		    struct rt_cipher_key *pSharedKey, int keyIdx)
{
	int KeyLen;
	int i;
	u8 CipherAlg = CIPHER_NONE;
	BOOLEAN bKeyIsHex = FALSE;

	/* TODO: Shall we do memset for the original key info?? */
	memset(pSharedKey, 0, sizeof(struct rt_cipher_key));
	KeyLen = strlen(keyString);
	switch (KeyLen) {
	case 5:		/*wep 40 Ascii type */
	case 13:		/*wep 104 Ascii type */
		bKeyIsHex = FALSE;
		pSharedKey->KeyLen = KeyLen;
		NdisMoveMemory(pSharedKey->Key, keyString, KeyLen);
		break;

	case 10:		/*wep 40 Hex type */
	case 26:		/*wep 104 Hex type */
		for (i = 0; i < KeyLen; i++) {
			if (!isxdigit(*(keyString + i)))
				return FALSE;	/*Not Hex value; */
		}
		bKeyIsHex = TRUE;
		pSharedKey->KeyLen = KeyLen / 2;
		AtoH(keyString, pSharedKey->Key, pSharedKey->KeyLen);
		break;

	default:		/*Invalid argument */
		DBGPRINT(RT_DEBUG_TRACE,
			 ("RT_CfgSetWepKey(keyIdx=%d):Invalid argument (arg=%s)\n",
			  keyIdx, keyString));
		return FALSE;
	}

	pSharedKey->CipherAlg = ((KeyLen % 5) ? CIPHER_WEP128 : CIPHER_WEP64);
	DBGPRINT(RT_DEBUG_TRACE,
		 ("RT_CfgSetWepKey:(KeyIdx=%d,type=%s, Alg=%s)\n", keyIdx,
		  (bKeyIsHex == FALSE ? "Ascii" : "Hex"),
		  CipherName[CipherAlg]));

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Set WPA PSK key

    Arguments:
        pAdapter	Pointer to our adapter
        keyString	WPA pre-shared key string
        pHashStr	String used for password hash function
        hashStrLen	Length of the hash string
        pPMKBuf		Output buffer of WPAPSK key

    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
int RT_CfgSetWPAPSKKey(struct rt_rtmp_adapter *pAd,
		       char *keyString,
		       u8 * pHashStr,
		       int hashStrLen, u8 *pPMKBuf)
{
	int keyLen;
	u8 keyMaterial[40];

	keyLen = strlen(keyString);
	if ((keyLen < 8) || (keyLen > 64)) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("WPAPSK Key length(%d) error, required 8 ~ 64 characters!(keyStr=%s)\n",
			  keyLen, keyString));
		return FALSE;
	}

	memset(pPMKBuf, 0, 32);
	if (keyLen == 64) {
		AtoH(keyString, pPMKBuf, 32);
	} else {
		PasswordHash(keyString, pHashStr, hashStrLen, keyMaterial);
		NdisMoveMemory(pPMKBuf, keyMaterial, 32);
	}

	return TRUE;
}

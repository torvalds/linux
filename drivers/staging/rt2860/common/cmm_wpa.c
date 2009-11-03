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
	wpa.c

	Abstract:

	Revision History:
	Who			When			What
	--------	----------		----------------------------------------------
	Jan	Lee		03-07-22		Initial
	Paul Lin	03-11-28		Modify for supplicant
*/
#include "../rt_config.h"
// WPA OUI
UCHAR		OUI_WPA_NONE_AKM[4]		= {0x00, 0x50, 0xF2, 0x00};
UCHAR       OUI_WPA_VERSION[4]      = {0x00, 0x50, 0xF2, 0x01};
UCHAR       OUI_WPA_WEP40[4]      = {0x00, 0x50, 0xF2, 0x01};
UCHAR       OUI_WPA_TKIP[4]     = {0x00, 0x50, 0xF2, 0x02};
UCHAR       OUI_WPA_CCMP[4]     = {0x00, 0x50, 0xF2, 0x04};
UCHAR       OUI_WPA_WEP104[4]      = {0x00, 0x50, 0xF2, 0x05};
UCHAR       OUI_WPA_8021X_AKM[4]	= {0x00, 0x50, 0xF2, 0x01};
UCHAR       OUI_WPA_PSK_AKM[4]      = {0x00, 0x50, 0xF2, 0x02};
// WPA2 OUI
UCHAR       OUI_WPA2_WEP40[4]   = {0x00, 0x0F, 0xAC, 0x01};
UCHAR       OUI_WPA2_TKIP[4]        = {0x00, 0x0F, 0xAC, 0x02};
UCHAR       OUI_WPA2_CCMP[4]        = {0x00, 0x0F, 0xAC, 0x04};
UCHAR       OUI_WPA2_8021X_AKM[4]   = {0x00, 0x0F, 0xAC, 0x01};
UCHAR       OUI_WPA2_PSK_AKM[4]   	= {0x00, 0x0F, 0xAC, 0x02};
UCHAR       OUI_WPA2_WEP104[4]   = {0x00, 0x0F, 0xAC, 0x05};
// MSA OUI
UCHAR   	OUI_MSA_8021X_AKM[4]    = {0x00, 0x0F, 0xAC, 0x05};		// Not yet final - IEEE 802.11s-D1.06
UCHAR   	OUI_MSA_PSK_AKM[4]   	= {0x00, 0x0F, 0xAC, 0x06};		// Not yet final - IEEE 802.11s-D1.06

/*
	========================================================================

	Routine Description:
		The pseudo-random function(PRF) that hashes various inputs to
		derive a pseudo-random value. To add liveness to the pseudo-random
		value, a nonce should be one of the inputs.

		It is used to generate PTK, GTK or some specific random value.

	Arguments:
		UCHAR	*key,		-	the key material for HMAC_SHA1 use
		INT		key_len		-	the length of key
		UCHAR	*prefix		-	a prefix label
		INT		prefix_len	-	the length of the label
		UCHAR	*data		-	a specific data with variable length
		INT		data_len	-	the length of a specific data
		INT		len			-	the output lenght

	Return Value:
		UCHAR	*output		-	the calculated result

	Note:
		802.11i-2004	Annex H.3

	========================================================================
*/
VOID	PRF(
	IN	UCHAR	*key,
	IN	INT		key_len,
	IN	UCHAR	*prefix,
	IN	INT		prefix_len,
	IN	UCHAR	*data,
	IN	INT		data_len,
	OUT	UCHAR	*output,
	IN	INT		len)
{
	INT		i;
    UCHAR   *input;
	INT		currentindex = 0;
	INT		total_len;

	// Allocate memory for input
	os_alloc_mem(NULL, (PUCHAR *)&input, 1024);

    if (input == NULL)
    {
        DBGPRINT(RT_DEBUG_ERROR, ("!!!PRF: no memory!!!\n"));
        return;
    }

	// Generate concatenation input
	NdisMoveMemory(input, prefix, prefix_len);

	// Concatenate a single octet containing 0
	input[prefix_len] =	0;

	// Concatenate specific data
	NdisMoveMemory(&input[prefix_len + 1], data, data_len);
	total_len =	prefix_len + 1 + data_len;

	// Concatenate a single octet containing 0
	// This octet shall be update later
	input[total_len] = 0;
	total_len++;

	// Iterate to calculate the result by hmac-sha-1
	// Then concatenate to last result
	for	(i = 0;	i <	(len + 19) / 20; i++)
	{
		HMAC_SHA1(input, total_len,	key, key_len, &output[currentindex]);
		currentindex +=	20;

		// update the last octet
		input[total_len - 1]++;
	}
    os_free_mem(NULL, input);
}

/*
	========================================================================

	Routine Description:
		It utilizes PRF-384 or PRF-512 to derive session-specific keys from a PMK.
		It shall be called by 4-way handshake processing.

	Arguments:
		pAd 	-	pointer to our pAdapter context
		PMK		-	pointer to PMK
		ANonce	-	pointer to ANonce
		AA		-	pointer to Authenticator Address
		SNonce	-	pointer to SNonce
		SA		-	pointer to Supplicant Address
		len		-	indicate the length of PTK (octet)

	Return Value:
		Output		pointer to the PTK

	Note:
		Refer to IEEE 802.11i-2004 8.5.1.2

	========================================================================
*/
VOID WpaCountPTK(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR	*PMK,
	IN	UCHAR	*ANonce,
	IN	UCHAR	*AA,
	IN	UCHAR	*SNonce,
	IN	UCHAR	*SA,
	OUT	UCHAR	*output,
	IN	UINT	len)
{
	UCHAR	concatenation[76];
	UINT	CurrPos = 0;
	UCHAR	temp[32];
	UCHAR	Prefix[] = {'P', 'a', 'i', 'r', 'w', 'i', 's', 'e', ' ', 'k', 'e', 'y', ' ',
						'e', 'x', 'p', 'a', 'n', 's', 'i', 'o', 'n'};

	// initiate the concatenation input
	NdisZeroMemory(temp, sizeof(temp));
	NdisZeroMemory(concatenation, 76);

	// Get smaller address
	if (RTMPCompareMemory(SA, AA, 6) == 1)
		NdisMoveMemory(concatenation, AA, 6);
	else
		NdisMoveMemory(concatenation, SA, 6);
	CurrPos += 6;

	// Get larger address
	if (RTMPCompareMemory(SA, AA, 6) == 1)
		NdisMoveMemory(&concatenation[CurrPos], SA, 6);
	else
		NdisMoveMemory(&concatenation[CurrPos], AA, 6);

	// store the larger mac address for backward compatible of
	// ralink proprietary STA-key issue
	NdisMoveMemory(temp, &concatenation[CurrPos], MAC_ADDR_LEN);
	CurrPos += 6;

	// Get smaller Nonce
	if (RTMPCompareMemory(ANonce, SNonce, 32) == 0)
		NdisMoveMemory(&concatenation[CurrPos], temp, 32);	// patch for ralink proprietary STA-key issue
	else if (RTMPCompareMemory(ANonce, SNonce, 32) == 1)
		NdisMoveMemory(&concatenation[CurrPos], SNonce, 32);
	else
		NdisMoveMemory(&concatenation[CurrPos], ANonce, 32);
	CurrPos += 32;

	// Get larger Nonce
	if (RTMPCompareMemory(ANonce, SNonce, 32) == 0)
		NdisMoveMemory(&concatenation[CurrPos], temp, 32);	// patch for ralink proprietary STA-key issue
	else if (RTMPCompareMemory(ANonce, SNonce, 32) == 1)
		NdisMoveMemory(&concatenation[CurrPos], ANonce, 32);
	else
		NdisMoveMemory(&concatenation[CurrPos], SNonce, 32);
	CurrPos += 32;

	hex_dump("concatenation=", concatenation, 76);

	// Use PRF to generate PTK
	PRF(PMK, LEN_MASTER_KEY, Prefix, 22, concatenation, 76, output, len);

}

/*
	========================================================================

	Routine Description:
		Generate random number by software.

	Arguments:
		pAd		-	pointer to our pAdapter context
		macAddr	-	pointer to local MAC address

	Return Value:

	Note:
		802.1ii-2004  Annex H.5

	========================================================================
*/
VOID	GenRandom(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR			*macAddr,
	OUT	UCHAR			*random)
{
	INT		i, curr;
	UCHAR	local[80], KeyCounter[32];
	UCHAR	result[80];
	ULONG	CurrentTime;
	UCHAR	prefix[] = {'I', 'n', 'i', 't', ' ', 'C', 'o', 'u', 'n', 't', 'e', 'r'};

	// Zero the related information
	NdisZeroMemory(result, 80);
	NdisZeroMemory(local, 80);
	NdisZeroMemory(KeyCounter, 32);

	for	(i = 0;	i <	32;	i++)
	{
		// copy the local MAC address
		COPY_MAC_ADDR(local, macAddr);
		curr =	MAC_ADDR_LEN;

		// concatenate the current time
		NdisGetSystemUpTime(&CurrentTime);
		NdisMoveMemory(&local[curr],  &CurrentTime,	sizeof(CurrentTime));
		curr +=	sizeof(CurrentTime);

		// concatenate the last result
		NdisMoveMemory(&local[curr],  result, 32);
		curr +=	32;

		// concatenate a variable
		NdisMoveMemory(&local[curr],  &i,  2);
		curr +=	2;

		// calculate the result
		PRF(KeyCounter, 32, prefix,12, local, curr, result, 32);
	}

	NdisMoveMemory(random, result,	32);
}

/*
	========================================================================

	Routine Description:
		Build cipher suite in RSN-IE.
		It only shall be called by RTMPMakeRSNIE.

	Arguments:
		pAd			-	pointer to our pAdapter context
    	ElementID	-	indicate the WPA1 or WPA2
    	WepStatus	-	indicate the encryption type
		bMixCipher	-	a boolean to indicate the pairwise cipher and group
						cipher are the same or not

	Return Value:

	Note:

	========================================================================
*/
static VOID RTMPInsertRsnIeCipher(
	IN  PRTMP_ADAPTER   pAd,
	IN	UCHAR			ElementID,
	IN	UINT			WepStatus,
	IN	BOOLEAN			bMixCipher,
	IN	UCHAR			FlexibleCipher,
	OUT	PUCHAR			pRsnIe,
	OUT	UCHAR			*rsn_len)
{
	UCHAR	PairwiseCnt;

	*rsn_len = 0;

	// decide WPA2 or WPA1
	if (ElementID == Wpa2Ie)
	{
		RSNIE2	*pRsnie_cipher = (RSNIE2*)pRsnIe;

		// Assign the verson as 1
		pRsnie_cipher->version = 1;

        switch (WepStatus)
        {
        	// TKIP mode
            case Ndis802_11Encryption2Enabled:
                NdisMoveMemory(pRsnie_cipher->mcast, OUI_WPA2_TKIP, 4);
                pRsnie_cipher->ucount = 1;
                NdisMoveMemory(pRsnie_cipher->ucast[0].oui, OUI_WPA2_TKIP, 4);
                *rsn_len = sizeof(RSNIE2);
                break;

			// AES mode
            case Ndis802_11Encryption3Enabled:
				if (bMixCipher)
					NdisMoveMemory(pRsnie_cipher->mcast, OUI_WPA2_TKIP, 4);
				else
					NdisMoveMemory(pRsnie_cipher->mcast, OUI_WPA2_CCMP, 4);
                pRsnie_cipher->ucount = 1;
                NdisMoveMemory(pRsnie_cipher->ucast[0].oui, OUI_WPA2_CCMP, 4);
                *rsn_len = sizeof(RSNIE2);
                break;

			// TKIP-AES mix mode
            case Ndis802_11Encryption4Enabled:
                NdisMoveMemory(pRsnie_cipher->mcast, OUI_WPA2_TKIP, 4);

				PairwiseCnt = 1;
				// Insert WPA2 TKIP as the first pairwise cipher
				if (MIX_CIPHER_WPA2_TKIP_ON(FlexibleCipher))
				{
                	NdisMoveMemory(pRsnie_cipher->ucast[0].oui, OUI_WPA2_TKIP, 4);
					// Insert WPA2 AES as the secondary pairwise cipher
					if (MIX_CIPHER_WPA2_AES_ON(FlexibleCipher))
					{
                		NdisMoveMemory(pRsnie_cipher->ucast[0].oui + 4, OUI_WPA2_CCMP, 4);
						PairwiseCnt = 2;
					}
				}
				else
				{
					// Insert WPA2 AES as the first pairwise cipher
					NdisMoveMemory(pRsnie_cipher->ucast[0].oui, OUI_WPA2_CCMP, 4);
				}

                pRsnie_cipher->ucount = PairwiseCnt;
                *rsn_len = sizeof(RSNIE2) + (4 * (PairwiseCnt - 1));
                break;
        }

		if ((pAd->OpMode == OPMODE_STA) &&
			(pAd->StaCfg.GroupCipher != Ndis802_11Encryption2Enabled) &&
			(pAd->StaCfg.GroupCipher != Ndis802_11Encryption3Enabled))
		{
			UINT GroupCipher = pAd->StaCfg.GroupCipher;
			switch(GroupCipher)
			{
				case Ndis802_11GroupWEP40Enabled:
					NdisMoveMemory(pRsnie_cipher->mcast, OUI_WPA2_WEP40, 4);
					break;
				case Ndis802_11GroupWEP104Enabled:
					NdisMoveMemory(pRsnie_cipher->mcast, OUI_WPA2_WEP104, 4);
					break;
			}
		}

		// swap for big-endian platform
		pRsnie_cipher->version = cpu2le16(pRsnie_cipher->version);
	    pRsnie_cipher->ucount = cpu2le16(pRsnie_cipher->ucount);
	}
	else
	{
		RSNIE	*pRsnie_cipher = (RSNIE*)pRsnIe;

		// Assign OUI and version
		NdisMoveMemory(pRsnie_cipher->oui, OUI_WPA_VERSION, 4);
        pRsnie_cipher->version = 1;

		switch (WepStatus)
		{
			// TKIP mode
            case Ndis802_11Encryption2Enabled:
                NdisMoveMemory(pRsnie_cipher->mcast, OUI_WPA_TKIP, 4);
                pRsnie_cipher->ucount = 1;
                NdisMoveMemory(pRsnie_cipher->ucast[0].oui, OUI_WPA_TKIP, 4);
                *rsn_len = sizeof(RSNIE);
                break;

			// AES mode
            case Ndis802_11Encryption3Enabled:
				if (bMixCipher)
					NdisMoveMemory(pRsnie_cipher->mcast, OUI_WPA_TKIP, 4);
				else
					NdisMoveMemory(pRsnie_cipher->mcast, OUI_WPA_CCMP, 4);
                pRsnie_cipher->ucount = 1;
                NdisMoveMemory(pRsnie_cipher->ucast[0].oui, OUI_WPA_CCMP, 4);
                *rsn_len = sizeof(RSNIE);
                break;

			// TKIP-AES mix mode
            case Ndis802_11Encryption4Enabled:
                NdisMoveMemory(pRsnie_cipher->mcast, OUI_WPA_TKIP, 4);

				PairwiseCnt = 1;
				// Insert WPA TKIP as the first pairwise cipher
				if (MIX_CIPHER_WPA_TKIP_ON(FlexibleCipher))
				{
                	NdisMoveMemory(pRsnie_cipher->ucast[0].oui, OUI_WPA_TKIP, 4);
					// Insert WPA AES as the secondary pairwise cipher
					if (MIX_CIPHER_WPA_AES_ON(FlexibleCipher))
					{
                		NdisMoveMemory(pRsnie_cipher->ucast[0].oui + 4, OUI_WPA_CCMP, 4);
						PairwiseCnt = 2;
					}
				}
				else
				{
					// Insert WPA AES as the first pairwise cipher
					NdisMoveMemory(pRsnie_cipher->ucast[0].oui, OUI_WPA_CCMP, 4);
				}

                pRsnie_cipher->ucount = PairwiseCnt;
                *rsn_len = sizeof(RSNIE) + (4 * (PairwiseCnt - 1));
                break;
        }

		if ((pAd->OpMode == OPMODE_STA) &&
			(pAd->StaCfg.GroupCipher != Ndis802_11Encryption2Enabled) &&
			(pAd->StaCfg.GroupCipher != Ndis802_11Encryption3Enabled))
		{
			UINT GroupCipher = pAd->StaCfg.GroupCipher;
			switch(GroupCipher)
			{
				case Ndis802_11GroupWEP40Enabled:
					NdisMoveMemory(pRsnie_cipher->mcast, OUI_WPA_WEP40, 4);
					break;
				case Ndis802_11GroupWEP104Enabled:
					NdisMoveMemory(pRsnie_cipher->mcast, OUI_WPA_WEP104, 4);
					break;
			}
		}

		// swap for big-endian platform
		pRsnie_cipher->version = cpu2le16(pRsnie_cipher->version);
	    pRsnie_cipher->ucount = cpu2le16(pRsnie_cipher->ucount);
	}
}

/*
	========================================================================

	Routine Description:
		Build AKM suite in RSN-IE.
		It only shall be called by RTMPMakeRSNIE.

	Arguments:
		pAd			-	pointer to our pAdapter context
    	ElementID	-	indicate the WPA1 or WPA2
    	AuthMode	-	indicate the authentication mode
		apidx		-	indicate the interface index

	Return Value:

	Note:

	========================================================================
*/
static VOID RTMPInsertRsnIeAKM(
	IN  PRTMP_ADAPTER   pAd,
	IN	UCHAR			ElementID,
	IN	UINT			AuthMode,
	IN	UCHAR			apidx,
	OUT	PUCHAR			pRsnIe,
	OUT	UCHAR			*rsn_len)
{
	RSNIE_AUTH		*pRsnie_auth;

	pRsnie_auth = (RSNIE_AUTH*)(pRsnIe + (*rsn_len));

	// decide WPA2 or WPA1
	if (ElementID == Wpa2Ie)
	{
		switch (AuthMode)
        {
            case Ndis802_11AuthModeWPA2:
            case Ndis802_11AuthModeWPA1WPA2:
                pRsnie_auth->acount = 1;
                	NdisMoveMemory(pRsnie_auth->auth[0].oui, OUI_WPA2_8021X_AKM, 4);
                break;

            case Ndis802_11AuthModeWPA2PSK:
            case Ndis802_11AuthModeWPA1PSKWPA2PSK:
                pRsnie_auth->acount = 1;
                	NdisMoveMemory(pRsnie_auth->auth[0].oui, OUI_WPA2_PSK_AKM, 4);
                break;
        }
	}
	else
	{
		switch (AuthMode)
        {
            case Ndis802_11AuthModeWPA:
            case Ndis802_11AuthModeWPA1WPA2:
                pRsnie_auth->acount = 1;
                NdisMoveMemory(pRsnie_auth->auth[0].oui, OUI_WPA_8021X_AKM, 4);
                break;

            case Ndis802_11AuthModeWPAPSK:
            case Ndis802_11AuthModeWPA1PSKWPA2PSK:
                pRsnie_auth->acount = 1;
                NdisMoveMemory(pRsnie_auth->auth[0].oui, OUI_WPA_PSK_AKM, 4);
                break;

			case Ndis802_11AuthModeWPANone:
                pRsnie_auth->acount = 1;
                NdisMoveMemory(pRsnie_auth->auth[0].oui, OUI_WPA_NONE_AKM, 4);
                break;
        }
	}

	pRsnie_auth->acount = cpu2le16(pRsnie_auth->acount);

	(*rsn_len) += sizeof(RSNIE_AUTH);	// update current RSNIE length

}

/*
	========================================================================

	Routine Description:
		Build capability in RSN-IE.
		It only shall be called by RTMPMakeRSNIE.

	Arguments:
		pAd			-	pointer to our pAdapter context
    	ElementID	-	indicate the WPA1 or WPA2
		apidx		-	indicate the interface index

	Return Value:

	Note:

	========================================================================
*/
static VOID RTMPInsertRsnIeCap(
	IN  PRTMP_ADAPTER   pAd,
	IN	UCHAR			ElementID,
	IN	UCHAR			apidx,
	OUT	PUCHAR			pRsnIe,
	OUT	UCHAR			*rsn_len)
{
	RSN_CAPABILITIES    *pRSN_Cap;

	// it could be ignored in WPA1 mode
	if (ElementID == WpaIe)
		return;

	pRSN_Cap = (RSN_CAPABILITIES*)(pRsnIe + (*rsn_len));


	pRSN_Cap->word = cpu2le16(pRSN_Cap->word);

	(*rsn_len) += sizeof(RSN_CAPABILITIES);	// update current RSNIE length

}


/*
	========================================================================

	Routine Description:
		Build RSN IE context. It is not included element-ID and length.

	Arguments:
		pAd			-	pointer to our pAdapter context
    	AuthMode	-	indicate the authentication mode
    	WepStatus	-	indicate the encryption type
		apidx		-	indicate the interface index

	Return Value:

	Note:

	========================================================================
*/
VOID RTMPMakeRSNIE(
    IN  PRTMP_ADAPTER   pAd,
    IN  UINT            AuthMode,
    IN  UINT            WepStatus,
	IN	UCHAR			apidx)
{
	PUCHAR		pRsnIe = NULL;			// primary RSNIE
	UCHAR 		*rsnielen_cur_p = 0;	// the length of the primary RSNIE
	UCHAR		*rsnielen_ex_cur_p = 0;	// the length of the secondary RSNIE
	UCHAR		PrimaryRsnie;
	BOOLEAN		bMixCipher = FALSE;	// indicate the pairwise and group cipher are different
	UCHAR		p_offset;
	WPA_MIX_PAIR_CIPHER		FlexibleCipher = WPA_TKIPAES_WPA2_TKIPAES;	// it provide the more flexible cipher combination in WPA-WPA2 and TKIPAES mode

	rsnielen_cur_p = NULL;
	rsnielen_ex_cur_p = NULL;

	{
		{
			if (pAd->StaCfg.WpaSupplicantUP != WPA_SUPPLICANT_DISABLE)
			{
				if (AuthMode < Ndis802_11AuthModeWPA)
					return;
			}
			else
			{
				// Support WPAPSK or WPA2PSK in STA-Infra mode
				// Support WPANone in STA-Adhoc mode
				if ((AuthMode != Ndis802_11AuthModeWPAPSK) &&
					(AuthMode != Ndis802_11AuthModeWPA2PSK) &&
					(AuthMode != Ndis802_11AuthModeWPANone)
					)
					return;
			}

			DBGPRINT(RT_DEBUG_TRACE,("==> RTMPMakeRSNIE(STA)\n"));

			// Zero RSNIE context
			pAd->StaCfg.RSNIE_Len = 0;
			NdisZeroMemory(pAd->StaCfg.RSN_IE, MAX_LEN_OF_RSNIE);

			// Pointer to RSNIE
			rsnielen_cur_p = &pAd->StaCfg.RSNIE_Len;
			pRsnIe = pAd->StaCfg.RSN_IE;

			bMixCipher = pAd->StaCfg.bMixCipher;
		}
	}

	// indicate primary RSNIE as WPA or WPA2
	if ((AuthMode == Ndis802_11AuthModeWPA) ||
		(AuthMode == Ndis802_11AuthModeWPAPSK) ||
		(AuthMode == Ndis802_11AuthModeWPANone) ||
		(AuthMode == Ndis802_11AuthModeWPA1WPA2) ||
		(AuthMode == Ndis802_11AuthModeWPA1PSKWPA2PSK))
		PrimaryRsnie = WpaIe;
	else
		PrimaryRsnie = Wpa2Ie;

	{
		// Build the primary RSNIE
		// 1. insert cipher suite
		RTMPInsertRsnIeCipher(pAd, PrimaryRsnie, WepStatus, bMixCipher, FlexibleCipher, pRsnIe, &p_offset);

		// 2. insert AKM
		RTMPInsertRsnIeAKM(pAd, PrimaryRsnie, AuthMode, apidx, pRsnIe, &p_offset);

		// 3. insert capability
		RTMPInsertRsnIeCap(pAd, PrimaryRsnie, apidx, pRsnIe, &p_offset);
	}

	// 4. update the RSNIE length
	*rsnielen_cur_p = p_offset;

	hex_dump("The primary RSNIE", pRsnIe, (*rsnielen_cur_p));


}

/*
    ==========================================================================
    Description:
		Check whether the received frame is EAP frame.

	Arguments:
		pAd				-	pointer to our pAdapter context
		pEntry			-	pointer to active entry
		pData			-	the received frame
		DataByteCount 	-	the received frame's length
		FromWhichBSSID	-	indicate the interface index

    Return:
         TRUE 			-	This frame is EAP frame
         FALSE 			-	otherwise
    ==========================================================================
*/
BOOLEAN RTMPCheckWPAframe(
    IN PRTMP_ADAPTER    pAd,
    IN PMAC_TABLE_ENTRY	pEntry,
    IN PUCHAR           pData,
    IN ULONG            DataByteCount,
	IN UCHAR			FromWhichBSSID)
{
	ULONG	Body_len;
	BOOLEAN Cancelled;


    if(DataByteCount < (LENGTH_802_1_H + LENGTH_EAPOL_H))
        return FALSE;


	// Skip LLC header
    if (NdisEqualMemory(SNAP_802_1H, pData, 6) ||
        // Cisco 1200 AP may send packet with SNAP_BRIDGE_TUNNEL
        NdisEqualMemory(SNAP_BRIDGE_TUNNEL, pData, 6))
    {
        pData += 6;
    }
	// Skip 2-bytes EAPoL type
    if (NdisEqualMemory(EAPOL, pData, 2))
    {
        pData += 2;
    }
    else
        return FALSE;

    switch (*(pData+1))
    {
        case EAPPacket:
			Body_len = (*(pData+2)<<8) | (*(pData+3));
            DBGPRINT(RT_DEBUG_TRACE, ("Receive EAP-Packet frame, TYPE = 0, Length = %ld\n", Body_len));
            break;
        case EAPOLStart:
            DBGPRINT(RT_DEBUG_TRACE, ("Receive EAPOL-Start frame, TYPE = 1 \n"));
			if (pEntry->EnqueueEapolStartTimerRunning != EAPOL_START_DISABLE)
            {
            	DBGPRINT(RT_DEBUG_TRACE, ("Cancel the EnqueueEapolStartTimerRunning \n"));
                RTMPCancelTimer(&pEntry->EnqueueStartForPSKTimer, &Cancelled);
                pEntry->EnqueueEapolStartTimerRunning = EAPOL_START_DISABLE;
            }
            break;
        case EAPOLLogoff:
            DBGPRINT(RT_DEBUG_TRACE, ("Receive EAPOLLogoff frame, TYPE = 2 \n"));
            break;
        case EAPOLKey:
			Body_len = (*(pData+2)<<8) | (*(pData+3));
            DBGPRINT(RT_DEBUG_TRACE, ("Receive EAPOL-Key frame, TYPE = 3, Length = %ld\n", Body_len));
            break;
        case EAPOLASFAlert:
            DBGPRINT(RT_DEBUG_TRACE, ("Receive EAPOLASFAlert frame, TYPE = 4 \n"));
            break;
        default:
            return FALSE;

    }
    return TRUE;
}

/*
	========================================================================

	Routine Description:
		Misc function to decrypt AES body

	Arguments:

	Return Value:

	Note:
		This function references to	RFC	3394 for aes key unwrap algorithm.

	========================================================================
*/
VOID	AES_GTK_KEY_UNWRAP(
	IN	UCHAR	*key,
	OUT	UCHAR	*plaintext,
	IN	UCHAR    c_len,
	IN	UCHAR	*ciphertext)

{
	UCHAR       A[8], BIN[16], BOUT[16];
	UCHAR       xor;
	INT         i, j;
	aes_context aesctx;
	UCHAR       *R;
	INT         num_blocks = c_len/8;	// unit:64bits


	os_alloc_mem(NULL, (PUCHAR *)&R, 512);

	if (R == NULL)
    {
        DBGPRINT(RT_DEBUG_ERROR, ("!!!AES_GTK_KEY_UNWRAP: no memory!!!\n"));
        return;
    } /* End of if */

	// Initialize
	NdisMoveMemory(A, ciphertext, 8);
	//Input plaintext
	for(i = 0; i < (c_len-8); i++)
	{
		R[ i] = ciphertext[i + 8];
	}

	rtmp_aes_set_key(&aesctx, key, 128);

	for(j = 5; j >= 0; j--)
	{
		for(i = (num_blocks-1); i > 0; i--)
		{
			xor = (num_blocks -1 )* j + i;
			NdisMoveMemory(BIN, A, 8);
			BIN[7] = A[7] ^ xor;
			NdisMoveMemory(&BIN[8], &R[(i-1)*8], 8);
			rtmp_aes_decrypt(&aesctx, BIN, BOUT);
			NdisMoveMemory(A, &BOUT[0], 8);
			NdisMoveMemory(&R[(i-1)*8], &BOUT[8], 8);
		}
	}

	// OUTPUT
	for(i = 0; i < c_len; i++)
	{
		plaintext[i] = R[i];
	}


	os_free_mem(NULL, R);
}

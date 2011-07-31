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
	rtmp_wep.c

	Abstract:

	Revision History:
	Who			When			What
	--------	----------		----------------------------------------------
	Paul Wu		10-28-02		Initial
*/

#include	"../rt_config.h"

u32 FCSTAB_32[256] = {
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba,
	0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
	0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
	0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
	0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
	0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
	0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,
	0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
	0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
	0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
	0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940,
	0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
	0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116,
	0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
	0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
	0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
	0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a,
	0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
	0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818,
	0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
	0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
	0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
	0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c,
	0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
	0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
	0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
	0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
	0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
	0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086,
	0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
	0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4,
	0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
	0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
	0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
	0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
	0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
	0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe,
	0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
	0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
	0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
	0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252,
	0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
	0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60,
	0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
	0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
	0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
	0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04,
	0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
	0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a,
	0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
	0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
	0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
	0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e,
	0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
	0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,
	0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
	0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
	0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
	0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0,
	0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
	0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6,
	0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
	0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
	0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

/*
u8   WEPKEY[] = {
		//IV
		0x00, 0x11, 0x22,
		//WEP KEY
		0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC
	};
 */

/*
	========================================================================

	Routine	Description:
		Init WEP function.

	Arguments:
      pAd		Pointer to our adapter
		pKey        Pointer to the WEP KEY
		KeyId		   WEP Key ID
		KeyLen      the length of WEP KEY
		pDest       Pointer to the destination which Encryption data will store in.

	Return Value:
		None

	IRQL = DISPATCH_LEVEL

	Note:

	========================================================================
*/
void RTMPInitWepEngine(struct rt_rtmp_adapter *pAd,
		       u8 *pKey,
		       u8 KeyId, u8 KeyLen, IN u8 *pDest)
{
	u32 i;
	u8 WEPKEY[] = {
		/*IV */
		0x00, 0x11, 0x22,
		/*WEP KEY */
		0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
		    0xAA, 0xBB, 0xCC
	};

	pAd->PrivateInfo.FCSCRC32 = PPPINITFCS32;	/*Init crc32. */

	{
		NdisMoveMemory(WEPKEY + 3, pKey, KeyLen);

		for (i = 0; i < 3; i++)
			WEPKEY[i] = RandomByte(pAd);	/*Call mlme RandomByte() function. */
		ARCFOUR_INIT(&pAd->PrivateInfo.WEPCONTEXT, WEPKEY, KeyLen + 3);	/*INIT SBOX, KEYLEN+3(IV) */

		NdisMoveMemory(pDest, WEPKEY, 3);	/*Append Init Vector */
	}
	*(pDest + 3) = (KeyId << 6);	/*Append KEYID */

}

/*
	========================================================================

	Routine	Description:
		Encrypt transimitted data

	Arguments:
      pAd		Pointer to our adapter
      pSrc        Pointer to the transimitted source data that will be encrypt
      pDest       Pointer to the destination where entryption data will be store in.
      Len			Indicate the length of the source data

	Return Value:
      None

	IRQL = DISPATCH_LEVEL

	Note:

	========================================================================
*/
void RTMPEncryptData(struct rt_rtmp_adapter *pAd,
		     u8 *pSrc, u8 *pDest, u32 Len)
{
	pAd->PrivateInfo.FCSCRC32 =
	    RTMP_CALC_FCS32(pAd->PrivateInfo.FCSCRC32, pSrc, Len);
	ARCFOUR_ENCRYPT(&pAd->PrivateInfo.WEPCONTEXT, pDest, pSrc, Len);
}

/*
	========================================================================

	Routine	Description:
		Decrypt received WEP data

	Arguments:
		pAdapter		Pointer to our adapter
		pSrc        Pointer to the received data
		Len         the length of the received data

	Return Value:
		TRUE        Decrypt WEP data success
		FALSE       Decrypt WEP data failed

	Note:

	========================================================================
*/
BOOLEAN RTMPSoftDecryptWEP(struct rt_rtmp_adapter *pAd,
			   u8 *pData,
			   unsigned long DataByteCnt, struct rt_cipher_key *pGroupKey)
{
	u32 trailfcs;
	u32 crc32;
	u8 KeyIdx;
	u8 WEPKEY[] = {
		/*IV */
		0x00, 0x11, 0x22,
		/*WEP KEY */
		0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
		    0xAA, 0xBB, 0xCC
	};
	u8 *pPayload = (u8 *) pData + LENGTH_802_11;
	unsigned long payload_len = DataByteCnt - LENGTH_802_11;

	NdisMoveMemory(WEPKEY, pPayload, 3);	/*Get WEP IV */

	KeyIdx = (*(pPayload + 3) & 0xc0) >> 6;
	if (pGroupKey[KeyIdx].KeyLen == 0)
		return (FALSE);

	NdisMoveMemory(WEPKEY + 3, pGroupKey[KeyIdx].Key,
		       pGroupKey[KeyIdx].KeyLen);
	ARCFOUR_INIT(&pAd->PrivateInfo.WEPCONTEXT, WEPKEY,
		     pGroupKey[KeyIdx].KeyLen + 3);
	ARCFOUR_DECRYPT(&pAd->PrivateInfo.WEPCONTEXT, pPayload, pPayload + 4,
			payload_len - 4);
	NdisMoveMemory(&trailfcs, pPayload + payload_len - 8, 4);
	crc32 = RTMP_CALC_FCS32(PPPINITFCS32, pPayload, payload_len - 8);	/*Skip last 4 bytes(FCS). */
	crc32 ^= 0xffffffff;	/* complement */

	if (crc32 != cpu2le32(trailfcs)) {
		DBGPRINT(RT_DEBUG_TRACE, ("WEP Data CRC Error!\n"));	/*CRC error. */
		return (FALSE);
	}
	return (TRUE);
}

/*
	========================================================================

	Routine	Description:
		The Stream Cipher Encryption Algorithm "struct rt_arcfour" initialize

	Arguments:
	   Ctx         Pointer to struct rt_arcfour CONTEXT (SBOX)
		pKey        Pointer to the WEP KEY
		KeyLen      Indicate the length fo the WEP KEY

	Return Value:
	   None

	IRQL = DISPATCH_LEVEL

	Note:

	========================================================================
*/
void ARCFOUR_INIT(struct rt_arcfourcontext *Ctx, u8 *pKey, u32 KeyLen)
{
	u8 t, u;
	u32 keyindex;
	u32 stateindex;
	u8 *state;
	u32 counter;

	state = Ctx->STATE;
	Ctx->X = 0;
	Ctx->Y = 0;
	for (counter = 0; counter < 256; counter++)
		state[counter] = (u8)counter;
	keyindex = 0;
	stateindex = 0;
	for (counter = 0; counter < 256; counter++) {
		t = state[counter];
		stateindex = (stateindex + pKey[keyindex] + t) & 0xff;
		u = state[stateindex];
		state[stateindex] = t;
		state[counter] = u;
		if (++keyindex >= KeyLen)
			keyindex = 0;
	}
}

/*
	========================================================================

	Routine	Description:
		Get bytes from struct rt_arcfour CONTEXT (S-BOX)

	Arguments:
	   Ctx         Pointer to struct rt_arcfour CONTEXT (SBOX)

	Return Value:
	   u8  - the value of the struct rt_arcfour CONTEXT (S-BOX)

	Note:

	========================================================================
*/
u8 ARCFOUR_BYTE(struct rt_arcfourcontext *Ctx)
{
	u32 x;
	u32 y;
	u8 sx, sy;
	u8 *state;

	state = Ctx->STATE;
	x = (Ctx->X + 1) & 0xff;
	sx = state[x];
	y = (sx + Ctx->Y) & 0xff;
	sy = state[y];
	Ctx->X = x;
	Ctx->Y = y;
	state[y] = sx;
	state[x] = sy;

	return (state[(sx + sy) & 0xff]);

}

/*
	========================================================================

	Routine	Description:
		The Stream Cipher Decryption Algorithm

	Arguments:
		Ctx         Pointer to struct rt_arcfour CONTEXT (SBOX)
		pDest			Pointer to the Destination
		pSrc        Pointer to the Source data
		Len         Indicate the length of the Source data

	Return Value:
		None

	Note:

	========================================================================
*/
void ARCFOUR_DECRYPT(struct rt_arcfourcontext *Ctx,
		     u8 *pDest, u8 *pSrc, u32 Len)
{
	u32 i;

	for (i = 0; i < Len; i++)
		pDest[i] = pSrc[i] ^ ARCFOUR_BYTE(Ctx);
}

/*
	========================================================================

	Routine	Description:
		The Stream Cipher Encryption Algorithm

	Arguments:
		Ctx         Pointer to struct rt_arcfour CONTEXT (SBOX)
		pDest			Pointer to the Destination
		pSrc        Pointer to the Source data
		Len         Indicate the length of the Source dta

	Return Value:
		None

	IRQL = DISPATCH_LEVEL

	Note:

	========================================================================
*/
void ARCFOUR_ENCRYPT(struct rt_arcfourcontext *Ctx,
		     u8 *pDest, u8 *pSrc, u32 Len)
{
	u32 i;

	for (i = 0; i < Len; i++)
		pDest[i] = pSrc[i] ^ ARCFOUR_BYTE(Ctx);
}

/*
	========================================================================

	Routine	Description:
		The Stream Cipher Encryption Algorithm which conform to the special requirement to encrypt  GTK.

	Arguments:
		Ctx         Pointer to struct rt_arcfour CONTEXT (SBOX)
		pDest			Pointer to the Destination
		pSrc        Pointer to the Source data
		Len         Indicate the length of the Source dta

	========================================================================
*/

void WPAARCFOUR_ENCRYPT(struct rt_arcfourcontext *Ctx,
			u8 *pDest, u8 *pSrc, u32 Len)
{
	u32 i;
	/*discard first 256 bytes */
	for (i = 0; i < 256; i++)
		ARCFOUR_BYTE(Ctx);

	for (i = 0; i < Len; i++)
		pDest[i] = pSrc[i] ^ ARCFOUR_BYTE(Ctx);
}

/*
	========================================================================

	Routine	Description:
		Calculate a new FCS given the current FCS and the new data.

	Arguments:
		Fcs	      the original FCS value
		Cp          pointer to the data which will be calculate the FCS
		Len         the length of the data

	Return Value:
		u32 - FCS 32 bits

	IRQL = DISPATCH_LEVEL

	Note:

	========================================================================
*/
u32 RTMP_CALC_FCS32(u32 Fcs, u8 *Cp, int Len)
{
	while (Len--)
		Fcs = (((Fcs) >> 8) ^ FCSTAB_32[((Fcs) ^ (*Cp++)) & 0xff]);

	return (Fcs);
}

/*
	========================================================================

	Routine	Description:
		Get last FCS and encrypt it to the destination

	Arguments:
		pDest			Pointer to the Destination

	Return Value:
		None

	Note:

	========================================================================
*/
void RTMPSetICV(struct rt_rtmp_adapter *pAd, u8 *pDest)
{
	pAd->PrivateInfo.FCSCRC32 ^= 0xffffffff;	/* complement */
	pAd->PrivateInfo.FCSCRC32 = cpu2le32(pAd->PrivateInfo.FCSCRC32);

	ARCFOUR_ENCRYPT(&pAd->PrivateInfo.WEPCONTEXT, pDest,
			(u8 *)& pAd->PrivateInfo.FCSCRC32, 4);
}

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
	rtmp_tkip.c

	Abstract:

	Revision History:
	Who			When			What
	--------	----------		----------------------------------------------
	Paul Wu		02-25-02		Initial
*/

#include "../rt_config.h"

// Rotation functions on 32 bit values
#define ROL32( A, n ) \
	( ((A) << (n)) | ( ((A)>>(32-(n))) & ( (1UL << (n)) - 1 ) ) )
#define ROR32( A, n ) ROL32( (A), 32-(n) )

UINT Tkip_Sbox_Lower[256] =
{
	0xA5,0x84,0x99,0x8D,0x0D,0xBD,0xB1,0x54,
	0x50,0x03,0xA9,0x7D,0x19,0x62,0xE6,0x9A,
	0x45,0x9D,0x40,0x87,0x15,0xEB,0xC9,0x0B,
	0xEC,0x67,0xFD,0xEA,0xBF,0xF7,0x96,0x5B,
	0xC2,0x1C,0xAE,0x6A,0x5A,0x41,0x02,0x4F,
	0x5C,0xF4,0x34,0x08,0x93,0x73,0x53,0x3F,
	0x0C,0x52,0x65,0x5E,0x28,0xA1,0x0F,0xB5,
	0x09,0x36,0x9B,0x3D,0x26,0x69,0xCD,0x9F,
	0x1B,0x9E,0x74,0x2E,0x2D,0xB2,0xEE,0xFB,
	0xF6,0x4D,0x61,0xCE,0x7B,0x3E,0x71,0x97,
	0xF5,0x68,0x00,0x2C,0x60,0x1F,0xC8,0xED,
	0xBE,0x46,0xD9,0x4B,0xDE,0xD4,0xE8,0x4A,
	0x6B,0x2A,0xE5,0x16,0xC5,0xD7,0x55,0x94,
	0xCF,0x10,0x06,0x81,0xF0,0x44,0xBA,0xE3,
	0xF3,0xFE,0xC0,0x8A,0xAD,0xBC,0x48,0x04,
	0xDF,0xC1,0x75,0x63,0x30,0x1A,0x0E,0x6D,
	0x4C,0x14,0x35,0x2F,0xE1,0xA2,0xCC,0x39,
	0x57,0xF2,0x82,0x47,0xAC,0xE7,0x2B,0x95,
	0xA0,0x98,0xD1,0x7F,0x66,0x7E,0xAB,0x83,
	0xCA,0x29,0xD3,0x3C,0x79,0xE2,0x1D,0x76,
	0x3B,0x56,0x4E,0x1E,0xDB,0x0A,0x6C,0xE4,
	0x5D,0x6E,0xEF,0xA6,0xA8,0xA4,0x37,0x8B,
	0x32,0x43,0x59,0xB7,0x8C,0x64,0xD2,0xE0,
	0xB4,0xFA,0x07,0x25,0xAF,0x8E,0xE9,0x18,
	0xD5,0x88,0x6F,0x72,0x24,0xF1,0xC7,0x51,
	0x23,0x7C,0x9C,0x21,0xDD,0xDC,0x86,0x85,
	0x90,0x42,0xC4,0xAA,0xD8,0x05,0x01,0x12,
	0xA3,0x5F,0xF9,0xD0,0x91,0x58,0x27,0xB9,
	0x38,0x13,0xB3,0x33,0xBB,0x70,0x89,0xA7,
	0xB6,0x22,0x92,0x20,0x49,0xFF,0x78,0x7A,
	0x8F,0xF8,0x80,0x17,0xDA,0x31,0xC6,0xB8,
	0xC3,0xB0,0x77,0x11,0xCB,0xFC,0xD6,0x3A
};

UINT Tkip_Sbox_Upper[256] =
{
	0xC6,0xF8,0xEE,0xF6,0xFF,0xD6,0xDE,0x91,
	0x60,0x02,0xCE,0x56,0xE7,0xB5,0x4D,0xEC,
	0x8F,0x1F,0x89,0xFA,0xEF,0xB2,0x8E,0xFB,
	0x41,0xB3,0x5F,0x45,0x23,0x53,0xE4,0x9B,
	0x75,0xE1,0x3D,0x4C,0x6C,0x7E,0xF5,0x83,
	0x68,0x51,0xD1,0xF9,0xE2,0xAB,0x62,0x2A,
	0x08,0x95,0x46,0x9D,0x30,0x37,0x0A,0x2F,
	0x0E,0x24,0x1B,0xDF,0xCD,0x4E,0x7F,0xEA,
	0x12,0x1D,0x58,0x34,0x36,0xDC,0xB4,0x5B,
	0xA4,0x76,0xB7,0x7D,0x52,0xDD,0x5E,0x13,
	0xA6,0xB9,0x00,0xC1,0x40,0xE3,0x79,0xB6,
	0xD4,0x8D,0x67,0x72,0x94,0x98,0xB0,0x85,
	0xBB,0xC5,0x4F,0xED,0x86,0x9A,0x66,0x11,
	0x8A,0xE9,0x04,0xFE,0xA0,0x78,0x25,0x4B,
	0xA2,0x5D,0x80,0x05,0x3F,0x21,0x70,0xF1,
	0x63,0x77,0xAF,0x42,0x20,0xE5,0xFD,0xBF,
	0x81,0x18,0x26,0xC3,0xBE,0x35,0x88,0x2E,
	0x93,0x55,0xFC,0x7A,0xC8,0xBA,0x32,0xE6,
	0xC0,0x19,0x9E,0xA3,0x44,0x54,0x3B,0x0B,
	0x8C,0xC7,0x6B,0x28,0xA7,0xBC,0x16,0xAD,
	0xDB,0x64,0x74,0x14,0x92,0x0C,0x48,0xB8,
	0x9F,0xBD,0x43,0xC4,0x39,0x31,0xD3,0xF2,
	0xD5,0x8B,0x6E,0xDA,0x01,0xB1,0x9C,0x49,
	0xD8,0xAC,0xF3,0xCF,0xCA,0xF4,0x47,0x10,
	0x6F,0xF0,0x4A,0x5C,0x38,0x57,0x73,0x97,
	0xCB,0xA1,0xE8,0x3E,0x96,0x61,0x0D,0x0F,
	0xE0,0x7C,0x71,0xCC,0x90,0x06,0xF7,0x1C,
	0xC2,0x6A,0xAE,0x69,0x17,0x99,0x3A,0x27,
	0xD9,0xEB,0x2B,0x22,0xD2,0xA9,0x07,0x33,
	0x2D,0x3C,0x15,0xC9,0x87,0xAA,0x50,0xA5,
	0x03,0x59,0x09,0x1A,0x65,0xD7,0x84,0xD0,
	0x82,0x29,0x5A,0x1E,0x7B,0xA8,0x6D,0x2C
};

/*****************************/
/******** SBOX Table *********/
/*****************************/

UCHAR SboxTable[256] =
{
	0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5,
	0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
	0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
	0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
	0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc,
	0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
	0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
	0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
	0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
	0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
	0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b,
	0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
	0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85,
	0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
	0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
	0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
	0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17,
	0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
	0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
	0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
	0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
	0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
	0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9,
	0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
	0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6,
	0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
	0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
	0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
	0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94,
	0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
	0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68,
	0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

VOID xor_32(
	IN  PUCHAR              a,
	IN  PUCHAR              b,
	OUT PUCHAR              out);

VOID xor_128(
	IN  PUCHAR              a,
	IN  PUCHAR              b,
	OUT PUCHAR              out);

VOID next_key(
	IN  PUCHAR              key,
	IN  INT                 round);

VOID byte_sub(
	IN  PUCHAR              in,
	OUT PUCHAR              out);

VOID shift_row(
	IN  PUCHAR              in,
	OUT PUCHAR              out);

VOID mix_column(
	IN  PUCHAR              in,
	OUT PUCHAR              out);

UCHAR RTMPCkipSbox(
	IN  UCHAR               a);
//
// Expanded IV for TKIP function.
//
typedef	struct	PACKED _IV_CONTROL_
{
	union PACKED
	{
		struct PACKED
		{
			UCHAR		rc0;
			UCHAR		rc1;
			UCHAR		rc2;

			union PACKED
			{
				struct PACKED
				{
					UCHAR	Rsvd:5;
					UCHAR	ExtIV:1;
					UCHAR	KeyID:2;
				}	field;
				UCHAR		Byte;
			}	CONTROL;
		}	field;

		ULONG	word;
	}	IV16;

	ULONG	IV32;
}	TKIP_IV, *PTKIP_IV;


/*
	========================================================================

	Routine	Description:
		Convert from UCHAR[] to ULONG in a portable way

	Arguments:
      pMICKey		pointer to MIC Key

	Return Value:
		None

	Note:

	========================================================================
*/
ULONG	RTMPTkipGetUInt32(
	IN	PUCHAR	pMICKey)
{
	ULONG	res = 0;
	INT		i;

	for (i = 0; i < 4; i++)
	{
		res |= (*pMICKey++) << (8 * i);
	}

	return res;
}

/*
	========================================================================

	Routine	Description:
		Convert from ULONG to UCHAR[] in a portable way

	Arguments:
      pDst			pointer to destination for convert ULONG to UCHAR[]
      val			the value for convert

	Return Value:
		None

	IRQL = DISPATCH_LEVEL

	Note:

	========================================================================
*/
VOID	RTMPTkipPutUInt32(
	IN OUT	PUCHAR		pDst,
	IN		ULONG		val)
{
	INT i;

	for(i = 0; i < 4; i++)
	{
		*pDst++ = (UCHAR) (val & 0xff);
		val >>= 8;
	}
}

/*
	========================================================================

	Routine	Description:
		Set the MIC Key.

	Arguments:
      pAd		Pointer to our adapter
      pMICKey		pointer to MIC Key

	Return Value:
		None

	IRQL = DISPATCH_LEVEL

	Note:

	========================================================================
*/
VOID RTMPTkipSetMICKey(
	IN	PTKIP_KEY_INFO	pTkip,
	IN	PUCHAR			pMICKey)
{
	// Set the key
	pTkip->K0 = RTMPTkipGetUInt32(pMICKey);
	pTkip->K1 = RTMPTkipGetUInt32(pMICKey + 4);
	// and reset the message
	pTkip->L = pTkip->K0;
	pTkip->R = pTkip->K1;
	pTkip->nBytesInM = 0;
	pTkip->M = 0;
}

/*
	========================================================================

	Routine	Description:
		Calculate the MIC Value.

	Arguments:
      pAd		Pointer to our adapter
      uChar			Append this uChar

	Return Value:
		None

	IRQL = DISPATCH_LEVEL

	Note:

	========================================================================
*/
VOID	RTMPTkipAppendByte(
	IN	PTKIP_KEY_INFO	pTkip,
	IN	UCHAR 			uChar)
{
	// Append the byte to our word-sized buffer
	pTkip->M |= (uChar << (8* pTkip->nBytesInM));
	pTkip->nBytesInM++;
	// Process the word if it is full.
	if( pTkip->nBytesInM >= 4 )
	{
		pTkip->L ^= pTkip->M;
		pTkip->R ^= ROL32( pTkip->L, 17 );
		pTkip->L += pTkip->R;
		pTkip->R ^= ((pTkip->L & 0xff00ff00) >> 8) | ((pTkip->L & 0x00ff00ff) << 8);
		pTkip->L += pTkip->R;
		pTkip->R ^= ROL32( pTkip->L, 3 );
		pTkip->L += pTkip->R;
		pTkip->R ^= ROR32( pTkip->L, 2 );
		pTkip->L += pTkip->R;
		// Clear the buffer
		pTkip->M = 0;
		pTkip->nBytesInM = 0;
	}
}

/*
	========================================================================

	Routine	Description:
		Calculate the MIC Value.

	Arguments:
      pAd		Pointer to our adapter
      pSrc			Pointer to source data for Calculate MIC Value
      Len			Indicate the length of the source data

	Return Value:
		None

	IRQL = DISPATCH_LEVEL

	Note:

	========================================================================
*/
VOID	RTMPTkipAppend(
	IN	PTKIP_KEY_INFO	pTkip,
	IN	PUCHAR			pSrc,
	IN	UINT			nBytes)
{
	// This is simple
	while(nBytes > 0)
	{
		RTMPTkipAppendByte(pTkip, *pSrc++);
		nBytes--;
	}
}

/*
	========================================================================

	Routine	Description:
		Get the MIC Value.

	Arguments:
      pAd		Pointer to our adapter

	Return Value:
		None

	IRQL = DISPATCH_LEVEL

	Note:
		the MIC Value is store in pAd->PrivateInfo.MIC
	========================================================================
*/
VOID	RTMPTkipGetMIC(
	IN	PTKIP_KEY_INFO	pTkip)
{
	// Append the minimum padding
	RTMPTkipAppendByte(pTkip, 0x5a );
	RTMPTkipAppendByte(pTkip, 0 );
	RTMPTkipAppendByte(pTkip, 0 );
	RTMPTkipAppendByte(pTkip, 0 );
	RTMPTkipAppendByte(pTkip, 0 );
	// and then zeroes until the length is a multiple of 4
	while( pTkip->nBytesInM != 0 )
	{
		RTMPTkipAppendByte(pTkip, 0 );
	}
	// The appendByte function has already computed the result.
	RTMPTkipPutUInt32(pTkip->MIC, pTkip->L);
	RTMPTkipPutUInt32(pTkip->MIC + 4, pTkip->R);
}

/*
	========================================================================

	Routine	Description:
		Init Tkip function.

	Arguments:
      pAd		Pointer to our adapter
		pTKey       Pointer to the Temporal Key (TK), TK shall be 128bits.
		KeyId		TK Key ID
		pTA			Pointer to transmitter address
		pMICKey		pointer to MIC Key

	Return Value:
		None

	IRQL = DISPATCH_LEVEL

	Note:

	========================================================================
*/
VOID	RTMPInitTkipEngine(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			pKey,
	IN	UCHAR			KeyId,
	IN	PUCHAR			pTA,
	IN	PUCHAR			pMICKey,
	IN	PUCHAR			pTSC,
	OUT	PULONG			pIV16,
	OUT	PULONG			pIV32)
{
	TKIP_IV	tkipIv;

	// Prepare 8 bytes TKIP encapsulation for MPDU
	NdisZeroMemory(&tkipIv, sizeof(TKIP_IV));
	tkipIv.IV16.field.rc0 = *(pTSC + 1);
	tkipIv.IV16.field.rc1 = (tkipIv.IV16.field.rc0 | 0x20) & 0x7f;
	tkipIv.IV16.field.rc2 = *pTSC;
	tkipIv.IV16.field.CONTROL.field.ExtIV = 1;  // 0: non-extended IV, 1: an extended IV
	tkipIv.IV16.field.CONTROL.field.KeyID = KeyId;
//	tkipIv.IV32 = *(PULONG)(pTSC + 2);
	NdisMoveMemory(&tkipIv.IV32, (pTSC + 2), 4);   // Copy IV

	*pIV16 = tkipIv.IV16.word;
	*pIV32 = tkipIv.IV32;
}

/*
	========================================================================

	Routine	Description:
		Init MIC Value calculation function which include set MIC key &
		calculate first 16 bytes (DA + SA + priority +  0)

	Arguments:
      pAd		Pointer to our adapter
		pTKey       Pointer to the Temporal Key (TK), TK shall be 128bits.
		pDA			Pointer to DA address
		pSA			Pointer to SA address
		pMICKey		pointer to MIC Key

	Return Value:
		None

	Note:

	========================================================================
*/
VOID	RTMPInitMICEngine(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			pKey,
	IN	PUCHAR			pDA,
	IN	PUCHAR			pSA,
	IN  UCHAR           UserPriority,
	IN	PUCHAR			pMICKey)
{
	ULONG Priority = UserPriority;

	// Init MIC value calculation
	RTMPTkipSetMICKey(&pAd->PrivateInfo.Tx, pMICKey);
	// DA
	RTMPTkipAppend(&pAd->PrivateInfo.Tx, pDA, MAC_ADDR_LEN);
	// SA
	RTMPTkipAppend(&pAd->PrivateInfo.Tx, pSA, MAC_ADDR_LEN);
	// Priority + 3 bytes of 0
	RTMPTkipAppend(&pAd->PrivateInfo.Tx, (PUCHAR)&Priority, 4);
}

/*
	========================================================================

	Routine	Description:
		Compare MIC value of received MSDU

	Arguments:
		pAd	Pointer to our adapter
		pSrc        Pointer to the received Plain text data
		pDA			Pointer to DA address
		pSA			Pointer to SA address
		pMICKey		pointer to MIC Key
		Len         the length of the received plain text data exclude MIC value

	Return Value:
		TRUE        MIC value matched
		FALSE       MIC value mismatched

	IRQL = DISPATCH_LEVEL

	Note:

	========================================================================
*/
BOOLEAN	RTMPTkipCompareMICValue(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			pSrc,
	IN	PUCHAR			pDA,
	IN	PUCHAR			pSA,
	IN	PUCHAR			pMICKey,
	IN	UCHAR			UserPriority,
	IN	UINT			Len)
{
	UCHAR	OldMic[8];
	ULONG	Priority = UserPriority;

	// Init MIC value calculation
	RTMPTkipSetMICKey(&pAd->PrivateInfo.Rx, pMICKey);
	// DA
	RTMPTkipAppend(&pAd->PrivateInfo.Rx, pDA, MAC_ADDR_LEN);
	// SA
	RTMPTkipAppend(&pAd->PrivateInfo.Rx, pSA, MAC_ADDR_LEN);
	// Priority + 3 bytes of 0
	RTMPTkipAppend(&pAd->PrivateInfo.Rx, (PUCHAR)&Priority, 4);

	// Calculate MIC value from plain text data
	RTMPTkipAppend(&pAd->PrivateInfo.Rx, pSrc, Len);

	// Get MIC valude from received frame
	NdisMoveMemory(OldMic, pSrc + Len, 8);

	// Get MIC value from decrypted plain data
	RTMPTkipGetMIC(&pAd->PrivateInfo.Rx);

	// Move MIC value from MSDU, this steps should move to data path.
	// Since the MIC value might cross MPDUs.
	if(!NdisEqualMemory(pAd->PrivateInfo.Rx.MIC, OldMic, 8))
	{
		DBGPRINT_RAW(RT_DEBUG_ERROR, ("RTMPTkipCompareMICValue(): TKIP MIC Error !\n"));  //MIC error.


		return (FALSE);
	}
	return (TRUE);
}

/*
	========================================================================

	Routine	Description:
		Compare MIC value of received MSDU

	Arguments:
		pAd	Pointer to our adapter
		pLLC		LLC header
		pSrc        Pointer to the received Plain text data
		pDA			Pointer to DA address
		pSA			Pointer to SA address
		pMICKey		pointer to MIC Key
		Len         the length of the received plain text data exclude MIC value

	Return Value:
		TRUE        MIC value matched
		FALSE       MIC value mismatched

	IRQL = DISPATCH_LEVEL

	Note:

	========================================================================
*/
BOOLEAN	RTMPTkipCompareMICValueWithLLC(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			pLLC,
	IN	PUCHAR			pSrc,
	IN	PUCHAR			pDA,
	IN	PUCHAR			pSA,
	IN	PUCHAR			pMICKey,
	IN	UINT			Len)
{
	UCHAR	OldMic[8];
	ULONG	Priority = 0;

	// Init MIC value calculation
	RTMPTkipSetMICKey(&pAd->PrivateInfo.Rx, pMICKey);
	// DA
	RTMPTkipAppend(&pAd->PrivateInfo.Rx, pDA, MAC_ADDR_LEN);
	// SA
	RTMPTkipAppend(&pAd->PrivateInfo.Rx, pSA, MAC_ADDR_LEN);
	// Priority + 3 bytes of 0
	RTMPTkipAppend(&pAd->PrivateInfo.Rx, (PUCHAR)&Priority, 4);

	// Start with LLC header
	RTMPTkipAppend(&pAd->PrivateInfo.Rx, pLLC, 8);

	// Calculate MIC value from plain text data
	RTMPTkipAppend(&pAd->PrivateInfo.Rx, pSrc, Len);

	// Get MIC valude from received frame
	NdisMoveMemory(OldMic, pSrc + Len, 8);

	// Get MIC value from decrypted plain data
	RTMPTkipGetMIC(&pAd->PrivateInfo.Rx);

	// Move MIC value from MSDU, this steps should move to data path.
	// Since the MIC value might cross MPDUs.
	if(!NdisEqualMemory(pAd->PrivateInfo.Rx.MIC, OldMic, 8))
	{
		DBGPRINT_RAW(RT_DEBUG_ERROR, ("RTMPTkipCompareMICValueWithLLC(): TKIP MIC Error !\n"));  //MIC error.


		return (FALSE);
	}
	return (TRUE);
}
/*
	========================================================================

	Routine	Description:
		Copy frame from waiting queue into relative ring buffer and set
	appropriate ASIC register to kick hardware transmit function

	Arguments:
		pAd		Pointer	to our adapter
		PNDIS_PACKET	Pointer to Ndis Packet for MIC calculation
		pEncap			Pointer to LLC encap data
		LenEncap		Total encap length, might be 0 which indicates no encap

	Return Value:
		None

	IRQL = DISPATCH_LEVEL

	Note:

	========================================================================
*/
VOID	RTMPCalculateMICValue(
	IN	PRTMP_ADAPTER	pAd,
	IN	PNDIS_PACKET	pPacket,
	IN	PUCHAR			pEncap,
	IN	PCIPHER_KEY		pKey,
	IN	UCHAR			apidx)
{
	PACKET_INFO		PacketInfo;
	PUCHAR			pSrcBufVA;
	UINT			SrcBufLen;
	PUCHAR			pSrc;
    UCHAR           UserPriority;
	UCHAR			vlan_offset = 0;

	RTMP_QueryPacketInfo(pPacket, &PacketInfo, &pSrcBufVA, &SrcBufLen);

	UserPriority = RTMP_GET_PACKET_UP(pPacket);
	pSrc = pSrcBufVA;

	// determine if this is a vlan packet
	if (((*(pSrc + 12) << 8) + *(pSrc + 13)) == 0x8100)
		vlan_offset = 4;

#ifdef CONFIG_STA_SUPPORT
#endif // CONFIG_STA_SUPPORT //
	{
		RTMPInitMICEngine(
			pAd,
			pKey->Key,
			pSrc,
			pSrc + 6,
			UserPriority,
			pKey->TxMic);
	}


	if (pEncap != NULL)
	{
		// LLC encapsulation
		RTMPTkipAppend(&pAd->PrivateInfo.Tx, pEncap, 6);
		// Protocol Type
		RTMPTkipAppend(&pAd->PrivateInfo.Tx, pSrc + 12 + vlan_offset, 2);
	}
	SrcBufLen -= (14 + vlan_offset);
	pSrc += (14 + vlan_offset);
	do
	{
		if (SrcBufLen > 0)
		{
			RTMPTkipAppend(&pAd->PrivateInfo.Tx, pSrc, SrcBufLen);
		}

		break;	// No need handle next packet

	}	while (TRUE);		// End of copying payload

	// Compute the final MIC Value
	RTMPTkipGetMIC(&pAd->PrivateInfo.Tx);
}


/************************************************************/
/* tkip_sbox()																*/
/* Returns a 16 bit value from a 64K entry table. The Table */
/* is synthesized from two 256 entry byte wide tables.		*/
/************************************************************/

UINT tkip_sbox(UINT index)
{
	UINT index_low;
	UINT index_high;
	UINT left, right;

	index_low = (index % 256);
	index_high = ((index >> 8) % 256);

	left = Tkip_Sbox_Lower[index_low] + (Tkip_Sbox_Upper[index_low] * 256);
	right = Tkip_Sbox_Upper[index_high] + (Tkip_Sbox_Lower[index_high] * 256);

	return (left ^ right);
}

UINT rotr1(UINT a)
{
	unsigned int b;

	if ((a & 0x01) == 0x01)
	{
		b = (a >> 1) | 0x8000;
	}
	else
	{
		b = (a >> 1) & 0x7fff;
	}
	b = b % 65536;
	return b;
}

VOID RTMPTkipMixKey(
	UCHAR *key,
	UCHAR *ta,
	ULONG pnl, /* Least significant 16 bits of PN */
	ULONG pnh, /* Most significant 32 bits of PN */
	UCHAR *rc4key,
	UINT *p1k)
{

	UINT tsc0;
	UINT tsc1;
	UINT tsc2;

	UINT ppk0;
	UINT ppk1;
	UINT ppk2;
	UINT ppk3;
	UINT ppk4;
	UINT ppk5;

	INT i;
	INT j;

	tsc0 = (unsigned int)((pnh >> 16) % 65536); /* msb */
	tsc1 = (unsigned int)(pnh % 65536);
	tsc2 = (unsigned int)(pnl % 65536); /* lsb */

	/* Phase 1, step 1 */
	p1k[0] = tsc1;
	p1k[1] = tsc0;
	p1k[2] = (UINT)(ta[0] + (ta[1]*256));
	p1k[3] = (UINT)(ta[2] + (ta[3]*256));
	p1k[4] = (UINT)(ta[4] + (ta[5]*256));

	/* Phase 1, step 2 */
	for (i=0; i<8; i++)
	{
		j = 2*(i & 1);
		p1k[0] = (p1k[0] + tkip_sbox( (p1k[4] ^ ((256*key[1+j]) + key[j])) % 65536 )) % 65536;
		p1k[1] = (p1k[1] + tkip_sbox( (p1k[0] ^ ((256*key[5+j]) + key[4+j])) % 65536 )) % 65536;
		p1k[2] = (p1k[2] + tkip_sbox( (p1k[1] ^ ((256*key[9+j]) + key[8+j])) % 65536 )) % 65536;
		p1k[3] = (p1k[3] + tkip_sbox( (p1k[2] ^ ((256*key[13+j]) + key[12+j])) % 65536 )) % 65536;
		p1k[4] = (p1k[4] + tkip_sbox( (p1k[3] ^ (((256*key[1+j]) + key[j]))) % 65536 )) % 65536;
		p1k[4] = (p1k[4] + i) % 65536;
	}

	/* Phase 2, Step 1 */
	ppk0 = p1k[0];
	ppk1 = p1k[1];
	ppk2 = p1k[2];
	ppk3 = p1k[3];
	ppk4 = p1k[4];
	ppk5 = (p1k[4] + tsc2) % 65536;

	/* Phase2, Step 2 */
	ppk0 = ppk0 + tkip_sbox( (ppk5 ^ ((256*key[1]) + key[0])) % 65536);
	ppk1 = ppk1 + tkip_sbox( (ppk0 ^ ((256*key[3]) + key[2])) % 65536);
	ppk2 = ppk2 + tkip_sbox( (ppk1 ^ ((256*key[5]) + key[4])) % 65536);
	ppk3 = ppk3 + tkip_sbox( (ppk2 ^ ((256*key[7]) + key[6])) % 65536);
	ppk4 = ppk4 + tkip_sbox( (ppk3 ^ ((256*key[9]) + key[8])) % 65536);
	ppk5 = ppk5 + tkip_sbox( (ppk4 ^ ((256*key[11]) + key[10])) % 65536);

	ppk0 = ppk0 + rotr1(ppk5 ^ ((256*key[13]) + key[12]));
	ppk1 = ppk1 + rotr1(ppk0 ^ ((256*key[15]) + key[14]));
	ppk2 = ppk2 + rotr1(ppk1);
	ppk3 = ppk3 + rotr1(ppk2);
	ppk4 = ppk4 + rotr1(ppk3);
	ppk5 = ppk5 + rotr1(ppk4);

	/* Phase 2, Step 3 */
    /* Phase 2, Step 3 */

	tsc0 = (unsigned int)((pnh >> 16) % 65536); /* msb */
	tsc1 = (unsigned int)(pnh % 65536);
	tsc2 = (unsigned int)(pnl % 65536); /* lsb */

	rc4key[0] = (tsc2 >> 8) % 256;
	rc4key[1] = (((tsc2 >> 8) % 256) | 0x20) & 0x7f;
	rc4key[2] = tsc2 % 256;
	rc4key[3] = ((ppk5 ^ ((256*key[1]) + key[0])) >> 1) % 256;

	rc4key[4] = ppk0 % 256;
	rc4key[5] = (ppk0 >> 8) % 256;

	rc4key[6] = ppk1 % 256;
	rc4key[7] = (ppk1 >> 8) % 256;

	rc4key[8] = ppk2 % 256;
	rc4key[9] = (ppk2 >> 8) % 256;

	rc4key[10] = ppk3 % 256;
	rc4key[11] = (ppk3 >> 8) % 256;

	rc4key[12] = ppk4 % 256;
	rc4key[13] = (ppk4 >> 8) % 256;

	rc4key[14] = ppk5 % 256;
	rc4key[15] = (ppk5 >> 8) % 256;
}


/************************************************/
/* construct_mic_header1()                      */
/* Builds the first MIC header block from       */
/* header fields.                               */
/************************************************/

void construct_mic_header1(
	unsigned char *mic_header1,
	int header_length,
	unsigned char *mpdu)
{
	mic_header1[0] = (unsigned char)((header_length - 2) / 256);
	mic_header1[1] = (unsigned char)((header_length - 2) % 256);
	mic_header1[2] = mpdu[0] & 0xcf;    /* Mute CF poll & CF ack bits */
	mic_header1[3] = mpdu[1] & 0xc7;    /* Mute retry, more data and pwr mgt bits */
	mic_header1[4] = mpdu[4];       /* A1 */
	mic_header1[5] = mpdu[5];
	mic_header1[6] = mpdu[6];
	mic_header1[7] = mpdu[7];
	mic_header1[8] = mpdu[8];
	mic_header1[9] = mpdu[9];
	mic_header1[10] = mpdu[10];     /* A2 */
	mic_header1[11] = mpdu[11];
	mic_header1[12] = mpdu[12];
	mic_header1[13] = mpdu[13];
	mic_header1[14] = mpdu[14];
	mic_header1[15] = mpdu[15];
}

/************************************************/
/* construct_mic_header2()                      */
/* Builds the last MIC header block from        */
/* header fields.                               */
/************************************************/

void construct_mic_header2(
	unsigned char *mic_header2,
	unsigned char *mpdu,
	int a4_exists,
	int qc_exists)
{
	int i;

	for (i = 0; i<16; i++) mic_header2[i]=0x00;

	mic_header2[0] = mpdu[16];    /* A3 */
	mic_header2[1] = mpdu[17];
	mic_header2[2] = mpdu[18];
	mic_header2[3] = mpdu[19];
	mic_header2[4] = mpdu[20];
	mic_header2[5] = mpdu[21];

	// In Sequence Control field, mute sequence numer bits (12-bit)
	mic_header2[6] = mpdu[22] & 0x0f;   /* SC */
	mic_header2[7] = 0x00; /* mpdu[23]; */

	if ((!qc_exists) & a4_exists)
	{
		for (i=0;i<6;i++) mic_header2[8+i] = mpdu[24+i];   /* A4 */

	}

	if (qc_exists && (!a4_exists))
	{
		mic_header2[8] = mpdu[24] & 0x0f; /* mute bits 15 - 4 */
		mic_header2[9] = mpdu[25] & 0x00;
	}

	if (qc_exists && a4_exists)
	{
		for (i=0;i<6;i++) mic_header2[8+i] = mpdu[24+i];   /* A4 */

		mic_header2[14] = mpdu[30] & 0x0f;
		mic_header2[15] = mpdu[31] & 0x00;
	}
}


/************************************************/
/* construct_mic_iv()                           */
/* Builds the MIC IV from header fields and PN  */
/************************************************/

void construct_mic_iv(
	unsigned char *mic_iv,
	int qc_exists,
	int a4_exists,
	unsigned char *mpdu,
	unsigned int payload_length,
	unsigned char *pn_vector)
{
	int i;

	mic_iv[0] = 0x59;
	if (qc_exists && a4_exists)
		mic_iv[1] = mpdu[30] & 0x0f;    /* QoS_TC           */
	if (qc_exists && !a4_exists)
		mic_iv[1] = mpdu[24] & 0x0f;   /* mute bits 7-4    */
	if (!qc_exists)
		mic_iv[1] = 0x00;
	for (i = 2; i < 8; i++)
		mic_iv[i] = mpdu[i + 8];                    /* mic_iv[2:7] = A2[0:5] = mpdu[10:15] */
#ifdef CONSISTENT_PN_ORDER
		for (i = 8; i < 14; i++)
			mic_iv[i] = pn_vector[i - 8];           /* mic_iv[8:13] = PN[0:5] */
#else
		for (i = 8; i < 14; i++)
			mic_iv[i] = pn_vector[13 - i];          /* mic_iv[8:13] = PN[5:0] */
#endif
	i = (payload_length / 256);
	i = (payload_length % 256);
	mic_iv[14] = (unsigned char) (payload_length / 256);
	mic_iv[15] = (unsigned char) (payload_length % 256);

}



/************************************/
/* bitwise_xor()                    */
/* A 128 bit, bitwise exclusive or  */
/************************************/

void bitwise_xor(unsigned char *ina, unsigned char *inb, unsigned char *out)
{
	int i;
	for (i=0; i<16; i++)
	{
		out[i] = ina[i] ^ inb[i];
	}
}


void aes128k128d(unsigned char *key, unsigned char *data, unsigned char *ciphertext)
{
	int round;
	int i;
	unsigned char intermediatea[16];
	unsigned char intermediateb[16];
	unsigned char round_key[16];

	for(i=0; i<16; i++) round_key[i] = key[i];

	for (round = 0; round < 11; round++)
	{
		if (round == 0)
		{
			xor_128(round_key, data, ciphertext);
			next_key(round_key, round);
		}
		else if (round == 10)
		{
			byte_sub(ciphertext, intermediatea);
			shift_row(intermediatea, intermediateb);
			xor_128(intermediateb, round_key, ciphertext);
		}
		else    /* 1 - 9 */
		{
			byte_sub(ciphertext, intermediatea);
			shift_row(intermediatea, intermediateb);
			mix_column(&intermediateb[0], &intermediatea[0]);
			mix_column(&intermediateb[4], &intermediatea[4]);
			mix_column(&intermediateb[8], &intermediatea[8]);
			mix_column(&intermediateb[12], &intermediatea[12]);
			xor_128(intermediatea, round_key, ciphertext);
			next_key(round_key, round);
		}
	}

}

void construct_ctr_preload(
	unsigned char *ctr_preload,
	int a4_exists,
	int qc_exists,
	unsigned char *mpdu,
	unsigned char *pn_vector,
	int c)
{

	int i = 0;
	for (i=0; i<16; i++) ctr_preload[i] = 0x00;
	i = 0;

	ctr_preload[0] = 0x01;                                  /* flag */
	if (qc_exists && a4_exists) ctr_preload[1] = mpdu[30] & 0x0f;   /* QoC_Control  */
	if (qc_exists && !a4_exists) ctr_preload[1] = mpdu[24] & 0x0f;

	for (i = 2; i < 8; i++)
		ctr_preload[i] = mpdu[i + 8];                       /* ctr_preload[2:7] = A2[0:5] = mpdu[10:15] */
#ifdef CONSISTENT_PN_ORDER
	  for (i = 8; i < 14; i++)
			ctr_preload[i] =    pn_vector[i - 8];           /* ctr_preload[8:13] = PN[0:5] */
#else
	  for (i = 8; i < 14; i++)
			ctr_preload[i] =    pn_vector[13 - i];          /* ctr_preload[8:13] = PN[5:0] */
#endif
	ctr_preload[14] =  (unsigned char) (c / 256); // Ctr
	ctr_preload[15] =  (unsigned char) (c % 256);

}


//
// TRUE: Success!
// FALSE: Decrypt Error!
//
BOOLEAN RTMPSoftDecryptTKIP(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR	pData,
	IN ULONG	DataByteCnt,
	IN UCHAR    UserPriority,
	IN PCIPHER_KEY	pWpaKey)
{
	UCHAR			KeyID;
	UINT			HeaderLen;
    UCHAR			fc0;
	UCHAR			fc1;
	USHORT			fc;
	UINT			frame_type;
	UINT			frame_subtype;
    UINT			from_ds;
    UINT			to_ds;
	INT				a4_exists;
	INT				qc_exists;
	USHORT			duration;
	USHORT			seq_control;
	USHORT			qos_control;
	UCHAR			TA[MAC_ADDR_LEN];
	UCHAR			DA[MAC_ADDR_LEN];
	UCHAR			SA[MAC_ADDR_LEN];
	UCHAR			RC4Key[16];
	UINT			p1k[5]; //for mix_key;
	ULONG			pnl;/* Least significant 16 bits of PN */
	ULONG			pnh;/* Most significant 32 bits of PN */
	UINT			num_blocks;
	UINT			payload_remainder;
	ARCFOURCONTEXT 	ArcFourContext;
	UINT			crc32 = 0;
	UINT			trailfcs = 0;
	UCHAR			MIC[8];
	UCHAR			TrailMIC[8];

	fc0 = *pData;
	fc1 = *(pData + 1);

	fc = *((PUSHORT)pData);

	frame_type = ((fc0 >> 2) & 0x03);
	frame_subtype = ((fc0 >> 4) & 0x0f);

    from_ds = (fc1 & 0x2) >> 1;
    to_ds = (fc1 & 0x1);

    a4_exists = (from_ds & to_ds);
    qc_exists = ((frame_subtype == 0x08) ||    /* Assumed QoS subtypes */
                  (frame_subtype == 0x09) ||   /* Likely to change.    */
                  (frame_subtype == 0x0a) ||
                  (frame_subtype == 0x0b)
                 );

	HeaderLen = 24;
	if (a4_exists)
		HeaderLen += 6;

	KeyID = *((PUCHAR)(pData+ HeaderLen + 3));
	KeyID = KeyID >> 6;

	if (pWpaKey[KeyID].KeyLen == 0)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("RTMPSoftDecryptTKIP failed!(KeyID[%d] Length can not be 0)\n", KeyID));
		return FALSE;
	}

	duration = *((PUSHORT)(pData+2));

	seq_control = *((PUSHORT)(pData+22));

	if (qc_exists)
	{
		if (a4_exists)
		{
			qos_control = *((PUSHORT)(pData+30));
		}
		else
		{
			qos_control = *((PUSHORT)(pData+24));
		}
	}

	if (to_ds == 0 && from_ds == 1)
	{
		NdisMoveMemory(DA, pData+4, MAC_ADDR_LEN);
		NdisMoveMemory(SA, pData+16, MAC_ADDR_LEN);
		NdisMoveMemory(TA, pData+10, MAC_ADDR_LEN);  //BSSID
	}
	else if (to_ds == 0 && from_ds == 0 )
	{
		NdisMoveMemory(TA, pData+10, MAC_ADDR_LEN);
		NdisMoveMemory(DA, pData+4, MAC_ADDR_LEN);
		NdisMoveMemory(SA, pData+10, MAC_ADDR_LEN);
	}
	else if (to_ds == 1 && from_ds == 0)
	{
		NdisMoveMemory(SA, pData+10, MAC_ADDR_LEN);
		NdisMoveMemory(TA, pData+10, MAC_ADDR_LEN);
		NdisMoveMemory(DA, pData+16, MAC_ADDR_LEN);
	}
	else if (to_ds == 1 && from_ds == 1)
	{
		NdisMoveMemory(TA, pData+10, MAC_ADDR_LEN);
		NdisMoveMemory(DA, pData+16, MAC_ADDR_LEN);
		NdisMoveMemory(SA, pData+22, MAC_ADDR_LEN);
	}

	num_blocks = (DataByteCnt - 16) / 16;
	payload_remainder = (DataByteCnt - 16) % 16;

	pnl = (*(pData + HeaderLen)) * 256 + *(pData + HeaderLen + 2);
	pnh = *((PULONG)(pData + HeaderLen + 4));
	pnh = cpu2le32(pnh);
	RTMPTkipMixKey(pWpaKey[KeyID].Key, TA, pnl, pnh, RC4Key, p1k);

	ARCFOUR_INIT(&ArcFourContext, RC4Key, 16);

	ARCFOUR_DECRYPT(&ArcFourContext, pData + HeaderLen, pData + HeaderLen + 8, DataByteCnt - HeaderLen - 8);
	NdisMoveMemory(&trailfcs, pData + DataByteCnt - 8 - 4, 4);
	crc32 = RTMP_CALC_FCS32(PPPINITFCS32, pData + HeaderLen, DataByteCnt - HeaderLen - 8 - 4);  //Skip IV+EIV 8 bytes & Skip last 4 bytes(FCS).
	crc32 ^= 0xffffffff;             /* complement */

    if(crc32 != cpu2le32(trailfcs))
	{
		DBGPRINT(RT_DEBUG_TRACE, ("RTMPSoftDecryptTKIP, WEP Data ICV Error !\n"));	 //ICV error.

		return (FALSE);
	}

	NdisMoveMemory(TrailMIC, pData + DataByteCnt - 8 - 8 - 4, 8);
	RTMPInitMICEngine(pAd, pWpaKey[KeyID].Key, DA, SA, UserPriority, pWpaKey[KeyID].RxMic);
	RTMPTkipAppend(&pAd->PrivateInfo.Tx, pData + HeaderLen, DataByteCnt - HeaderLen - 8 - 12);
	RTMPTkipGetMIC(&pAd->PrivateInfo.Tx);
	NdisMoveMemory(MIC, pAd->PrivateInfo.Tx.MIC, 8);

	if (!NdisEqualMemory(MIC, TrailMIC, 8))
	{
		DBGPRINT(RT_DEBUG_ERROR, ("RTMPSoftDecryptTKIP, WEP Data MIC Error !\n"));	 //MIC error.
		//RTMPReportMicError(pAd, &pWpaKey[KeyID]);	// marked by AlbertY @ 20060630
		return (FALSE);
	}

	//DBGPRINT(RT_DEBUG_TRACE, "RTMPSoftDecryptTKIP Decript done!!\n");
	return TRUE;
}




BOOLEAN RTMPSoftDecryptAES(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR	pData,
	IN ULONG	DataByteCnt,
	IN PCIPHER_KEY	pWpaKey)
{
	UCHAR			KeyID;
	UINT			HeaderLen;
	UCHAR			PN[6];
	UINT			payload_len;
	UINT			num_blocks;
	UINT			payload_remainder;
	USHORT			fc;
	UCHAR			fc0;
	UCHAR			fc1;
	UINT			frame_type;
	UINT			frame_subtype;
	UINT			from_ds;
	UINT			to_ds;
	INT				a4_exists;
	INT				qc_exists;
	UCHAR			aes_out[16];
	int 			payload_index;
	UINT 			i;
	UCHAR 			ctr_preload[16];
	UCHAR 			chain_buffer[16];
	UCHAR 			padded_buffer[16];
	UCHAR 			mic_iv[16];
	UCHAR 			mic_header1[16];
	UCHAR 			mic_header2[16];
	UCHAR			MIC[8];
	UCHAR			TrailMIC[8];

	fc0 = *pData;
	fc1 = *(pData + 1);

	fc = *((PUSHORT)pData);

	frame_type = ((fc0 >> 2) & 0x03);
	frame_subtype = ((fc0 >> 4) & 0x0f);

	from_ds = (fc1 & 0x2) >> 1;
	to_ds = (fc1 & 0x1);

	a4_exists = (from_ds & to_ds);
	qc_exists = ((frame_subtype == 0x08) ||    /* Assumed QoS subtypes */
				  (frame_subtype == 0x09) ||   /* Likely to change.    */
				  (frame_subtype == 0x0a) ||
				  (frame_subtype == 0x0b)
				 );

	HeaderLen = 24;
	if (a4_exists)
		HeaderLen += 6;

	KeyID = *((PUCHAR)(pData+ HeaderLen + 3));
	KeyID = KeyID >> 6;

	if (pWpaKey[KeyID].KeyLen == 0)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("RTMPSoftDecryptAES failed!(KeyID[%d] Length can not be 0)\n", KeyID));
		return FALSE;
	}

	PN[0] = *(pData+ HeaderLen);
	PN[1] = *(pData+ HeaderLen + 1);
	PN[2] = *(pData+ HeaderLen + 4);
	PN[3] = *(pData+ HeaderLen + 5);
	PN[4] = *(pData+ HeaderLen + 6);
	PN[5] = *(pData+ HeaderLen + 7);

	payload_len = DataByteCnt - HeaderLen - 8 - 8;	// 8 bytes for CCMP header , 8 bytes for MIC
	payload_remainder = (payload_len) % 16;
	num_blocks = (payload_len) / 16;



	// Find start of payload
	payload_index = HeaderLen + 8; //IV+EIV

	for (i=0; i< num_blocks; i++)
	{
		construct_ctr_preload(ctr_preload,
								a4_exists,
								qc_exists,
								pData,
								PN,
								i+1 );

		aes128k128d(pWpaKey[KeyID].Key, ctr_preload, aes_out);

		bitwise_xor(aes_out, pData + payload_index, chain_buffer);
		NdisMoveMemory(pData + payload_index - 8, chain_buffer, 16);
		payload_index += 16;
	}

	//
	// If there is a short final block, then pad it
	// encrypt it and copy the unpadded part back
	//
	if (payload_remainder > 0)
	{
		construct_ctr_preload(ctr_preload,
								a4_exists,
								qc_exists,
								pData,
								PN,
								num_blocks + 1);

		NdisZeroMemory(padded_buffer, 16);
		NdisMoveMemory(padded_buffer, pData + payload_index, payload_remainder);

		aes128k128d(pWpaKey[KeyID].Key, ctr_preload, aes_out);

		bitwise_xor(aes_out, padded_buffer, chain_buffer);
		NdisMoveMemory(pData + payload_index - 8, chain_buffer, payload_remainder);
		payload_index += payload_remainder;
	}

	//
	// Descrypt the MIC
	//
	construct_ctr_preload(ctr_preload,
							a4_exists,
							qc_exists,
							pData,
							PN,
							0);
	NdisZeroMemory(padded_buffer, 16);
	NdisMoveMemory(padded_buffer, pData + payload_index, 8);

	aes128k128d(pWpaKey[KeyID].Key, ctr_preload, aes_out);

	bitwise_xor(aes_out, padded_buffer, chain_buffer);

	NdisMoveMemory(TrailMIC, chain_buffer, 8);

	//
	// Calculate MIC
	//

	//Force the protected frame bit on
	*(pData + 1) = *(pData + 1) | 0x40;

	// Find start of payload
	// Because the CCMP header has been removed
	payload_index = HeaderLen;

	construct_mic_iv(
					mic_iv,
					qc_exists,
					a4_exists,
					pData,
					payload_len,
					PN);

	construct_mic_header1(
						mic_header1,
						HeaderLen,
						pData);

	construct_mic_header2(
						mic_header2,
						pData,
						a4_exists,
						qc_exists);

	aes128k128d(pWpaKey[KeyID].Key, mic_iv, aes_out);
	bitwise_xor(aes_out, mic_header1, chain_buffer);
	aes128k128d(pWpaKey[KeyID].Key, chain_buffer, aes_out);
	bitwise_xor(aes_out, mic_header2, chain_buffer);
	aes128k128d(pWpaKey[KeyID].Key, chain_buffer, aes_out);

	// iterate through each 16 byte payload block
	for (i = 0; i < num_blocks; i++)
	{
		bitwise_xor(aes_out, pData + payload_index, chain_buffer);
		payload_index += 16;
		aes128k128d(pWpaKey[KeyID].Key, chain_buffer, aes_out);
	}

	// Add on the final payload block if it needs padding
	if (payload_remainder > 0)
	{
		NdisZeroMemory(padded_buffer, 16);
		NdisMoveMemory(padded_buffer, pData + payload_index, payload_remainder);

		bitwise_xor(aes_out, padded_buffer, chain_buffer);
		aes128k128d(pWpaKey[KeyID].Key, chain_buffer, aes_out);
	}

	// aes_out contains padded mic, discard most significant
	// 8 bytes to generate 64 bit MIC
	for (i = 0 ; i < 8; i++) MIC[i] = aes_out[i];

	if (!NdisEqualMemory(MIC, TrailMIC, 8))
	{
		DBGPRINT(RT_DEBUG_ERROR, ("RTMPSoftDecryptAES, MIC Error !\n"));	 //MIC error.
		return FALSE;
	}

	return TRUE;
}

/****************************************/
/* aes128k128d()                        */
/* Performs a 128 bit AES encrypt with  */
/* 128 bit data.                        */
/****************************************/
VOID xor_128(
	IN  PUCHAR  a,
	IN  PUCHAR  b,
	OUT PUCHAR  out)
{
	INT i;

	for (i=0;i<16; i++)
	{
		out[i] = a[i] ^ b[i];
	}
}

VOID next_key(
	IN  PUCHAR  key,
	IN  INT     round)
{
	UCHAR       rcon;
	UCHAR       sbox_key[4];
	UCHAR       rcon_table[12] =
	{
		0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
		0x1b, 0x36, 0x36, 0x36
	};

	sbox_key[0] = RTMPCkipSbox(key[13]);
	sbox_key[1] = RTMPCkipSbox(key[14]);
	sbox_key[2] = RTMPCkipSbox(key[15]);
	sbox_key[3] = RTMPCkipSbox(key[12]);

	rcon = rcon_table[round];

	xor_32(&key[0], sbox_key, &key[0]);
	key[0] = key[0] ^ rcon;

	xor_32(&key[4], &key[0], &key[4]);
	xor_32(&key[8], &key[4], &key[8]);
	xor_32(&key[12], &key[8], &key[12]);
}

VOID xor_32(
	IN  PUCHAR  a,
	IN  PUCHAR  b,
	OUT PUCHAR  out)
{
	INT i;

	for (i=0;i<4; i++)
	{
		out[i] = a[i] ^ b[i];
	}
}

VOID byte_sub(
	IN  PUCHAR  in,
	OUT PUCHAR  out)
{
	INT i;

	for (i=0; i< 16; i++)
	{
		out[i] = RTMPCkipSbox(in[i]);
	}
}

UCHAR RTMPCkipSbox(
	IN  UCHAR   a)
{
	return SboxTable[(int)a];
}

VOID shift_row(
	IN  PUCHAR  in,
	OUT PUCHAR  out)
{
	out[0] =  in[0];
	out[1] =  in[5];
	out[2] =  in[10];
	out[3] =  in[15];
	out[4] =  in[4];
	out[5] =  in[9];
	out[6] =  in[14];
	out[7] =  in[3];
	out[8] =  in[8];
	out[9] =  in[13];
	out[10] = in[2];
	out[11] = in[7];
	out[12] = in[12];
	out[13] = in[1];
	out[14] = in[6];
	out[15] = in[11];
}

VOID mix_column(
	IN  PUCHAR  in,
	OUT PUCHAR  out)
{
	INT         i;
	UCHAR       add1b[4];
	UCHAR       add1bf7[4];
	UCHAR       rotl[4];
	UCHAR       swap_halfs[4];
	UCHAR       andf7[4];
	UCHAR       rotr[4];
	UCHAR       temp[4];
	UCHAR       tempb[4];

	for (i=0 ; i<4; i++)
	{
		if ((in[i] & 0x80)== 0x80)
			add1b[i] = 0x1b;
		else
			add1b[i] = 0x00;
	}

	swap_halfs[0] = in[2];    /* Swap halfs */
	swap_halfs[1] = in[3];
	swap_halfs[2] = in[0];
	swap_halfs[3] = in[1];

	rotl[0] = in[3];        /* Rotate left 8 bits */
	rotl[1] = in[0];
	rotl[2] = in[1];
	rotl[3] = in[2];

	andf7[0] = in[0] & 0x7f;
	andf7[1] = in[1] & 0x7f;
	andf7[2] = in[2] & 0x7f;
	andf7[3] = in[3] & 0x7f;

	for (i = 3; i>0; i--)    /* logical shift left 1 bit */
	{
		andf7[i] = andf7[i] << 1;
		if ((andf7[i-1] & 0x80) == 0x80)
		{
			andf7[i] = (andf7[i] | 0x01);
		}
	}
	andf7[0] = andf7[0] << 1;
	andf7[0] = andf7[0] & 0xfe;

	xor_32(add1b, andf7, add1bf7);

	xor_32(in, add1bf7, rotr);

	temp[0] = rotr[0];         /* Rotate right 8 bits */
	rotr[0] = rotr[1];
	rotr[1] = rotr[2];
	rotr[2] = rotr[3];
	rotr[3] = temp[0];

	xor_32(add1bf7, rotr, temp);
	xor_32(swap_halfs, rotl,tempb);
	xor_32(temp, tempb, out);
}


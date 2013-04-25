/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
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
 *************************************************************************/


#include	"rt_config.h"



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

UCHAR RTMPCkipSbox(
	IN  UCHAR   a)
{
	return SboxTable[(int)a];
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

	/* In Sequence Control field, mute sequence numer bits (12-bit) */
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

/****************************************/
/* aes128k128d()                        */
/* Performs a 128 bit AES encrypt with  */
/* 128 bit data.                        */
/****************************************/
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
	ctr_preload[14] =  (unsigned char) (c / 256); /* Ctr */
	ctr_preload[15] =  (unsigned char) (c % 256);

}

BOOLEAN RTMPSoftDecryptAES(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR	pData,
	IN ULONG	DataByteCnt, 
	IN PCIPHER_KEY	pWpaKey)
{
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

#ifdef RT_BIG_ENDIAN
	RTMPFrameEndianChange(pAd, (PUCHAR)pData, DIR_READ, FALSE);
#endif

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

	if (qc_exists)
		HeaderLen += 2;

	if (pWpaKey->KeyLen == 0)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("RTMPSoftDecryptAES failed!(the Length can not be 0)\n"));
		return FALSE;
	}

	PN[0] = *(pData+ HeaderLen);
	PN[1] = *(pData+ HeaderLen + 1);
	PN[2] = *(pData+ HeaderLen + 4);
	PN[3] = *(pData+ HeaderLen + 5);
	PN[4] = *(pData+ HeaderLen + 6);
	PN[5] = *(pData+ HeaderLen + 7);

	payload_len = DataByteCnt - HeaderLen - 8 - 8;	/* 8 bytes for CCMP header , 8 bytes for MIC*/
	payload_remainder = (payload_len) % 16;
	num_blocks = (payload_len) / 16; 
	
	

	/* Find start of payload*/
	payload_index = HeaderLen + 8; /*IV+EIV*/

	for (i=0; i< num_blocks; i++)	
	{
		construct_ctr_preload(ctr_preload,
								a4_exists,
								qc_exists,
								pData,
								PN,
								i+1 );

		aes128k128d(pWpaKey->Key, ctr_preload, aes_out);

		bitwise_xor(aes_out, pData + payload_index, chain_buffer);
		NdisMoveMemory(pData + payload_index - 8, chain_buffer, 16);
		payload_index += 16;
	}

	
	/* If there is a short final block, then pad it*/
	/* encrypt it and copy the unpadded part back */
	
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

		aes128k128d(pWpaKey->Key, ctr_preload, aes_out);

		bitwise_xor(aes_out, padded_buffer, chain_buffer);
		NdisMoveMemory(pData + payload_index - 8, chain_buffer, payload_remainder);
		payload_index += payload_remainder;
	}

	
	/* Descrypt the MIC*/
	/* */
	construct_ctr_preload(ctr_preload,
							a4_exists,
							qc_exists,
							pData,
							PN,
							0);
	NdisZeroMemory(padded_buffer, 16);
	NdisMoveMemory(padded_buffer, pData + payload_index, 8); 
	
	aes128k128d(pWpaKey->Key, ctr_preload, aes_out);

	bitwise_xor(aes_out, padded_buffer, chain_buffer);	

	NdisMoveMemory(TrailMIC, chain_buffer, 8);
	
	
	
	/* Calculate MIC*/
	

	/*Force the protected frame bit on*/
	*(pData + 1) = *(pData + 1) | 0x40;

	/* Find start of payload*/
	/* Because the CCMP header has been removed*/
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

	aes128k128d(pWpaKey->Key, mic_iv, aes_out);
	bitwise_xor(aes_out, mic_header1, chain_buffer);
	aes128k128d(pWpaKey->Key, chain_buffer, aes_out);
	bitwise_xor(aes_out, mic_header2, chain_buffer);
	aes128k128d(pWpaKey->Key, chain_buffer, aes_out);

	/* iterate through each 16 byte payload block*/
	for (i = 0; i < num_blocks; i++)     
	{
		bitwise_xor(aes_out, pData + payload_index, chain_buffer);
		payload_index += 16;
		aes128k128d(pWpaKey->Key, chain_buffer, aes_out);
	}

	/* Add on the final payload block if it needs padding*/
	if (payload_remainder > 0)
	{
		NdisZeroMemory(padded_buffer, 16);
		NdisMoveMemory(padded_buffer, pData + payload_index, payload_remainder);

		bitwise_xor(aes_out, padded_buffer, chain_buffer);
		aes128k128d(pWpaKey->Key, chain_buffer, aes_out);		
	}

	/* aes_out contains padded mic, discard most significant*/
	/* 8 bytes to generate 64 bit MIC*/
	for (i = 0 ; i < 8; i++) MIC[i] = aes_out[i];

	if (!NdisEqualMemory(MIC, TrailMIC, 8))
	{
		DBGPRINT(RT_DEBUG_ERROR, ("RTMPSoftDecryptAES, MIC Error !\n"));	 /*MIC error.	*/
		return FALSE;
	}

#ifdef RT_BIG_ENDIAN
	RTMPFrameEndianChange(pAd, (PUCHAR)pData, DIR_READ, FALSE);
#endif

	return TRUE;
}


/*
	========================================================================
	
	Routine Description:
		Construct AAD of CCMP.

	Arguments:
		
	Return Value:

	Note:
		It's described in IEEE Std 802.11-2007.
		The AAD is constructed from the MPDU header.
		
	========================================================================
*/
VOID RTMPConstructCCMPAAD(
	IN 	PUCHAR 			pHdr,
	IN	BOOLEAN			isDataFrame,
	IN	UINT8 			a4_exists,
	IN	UINT8			qc_exists,
	OUT	UCHAR			*aad_hdr,
	OUT	UINT			*aad_len)
{
	UINT len = 0;

	/* 	Frame control -
		Subtype bits (bits 4 5 6) in a Data MPDU masked to 0
		Retry bit (bit 11) masked to 0
		PwrMgt bit (bit 12) masked to 0
		MoreData bit (bit 13) masked to 0
		Protected Frame bit (bit 14) always set to 1 */
	if (isDataFrame)
		aad_hdr[0] = (*pHdr) & 0x8f;
	else
		aad_hdr[0] = (*pHdr);
	aad_hdr[1] = (*(pHdr + 1)) & 0xc7;
	aad_hdr[1] = aad_hdr[1] | 0x40;
	len = 2;
	
	/* Append Addr 1, 2 & 3 */
	NdisMoveMemory(&aad_hdr[len], pHdr + 4, 3 * MAC_ADDR_LEN);
	len += (3 * MAC_ADDR_LEN);

	/*  SC - 
		MPDU Sequence Control field, with the Sequence Number 
		subfield (bits 4-15 of the Sequence Control field) 
		masked to 0. The Fragment Number subfield is not modified. */	 
	aad_hdr[len] = (*(pHdr + 22)) & 0x0f;   
	aad_hdr[len + 1] = 0x00;
	len += 2;
	
			
	/* Append the Addr4 field if present. */ 
	if (a4_exists)
	{
		NdisMoveMemory(&aad_hdr[len], pHdr + 24, MAC_ADDR_LEN);
		len += MAC_ADDR_LEN;
	}
	
	/*  QC - 
		QoS Control field, if present, a 2-octet field that includes 
		the MSDU priority. The QC TID field is used in the 
		construction of the AAD and the remaining QC fields are 
		set to 0 for the AAD calculation (bits 4 to 15 are set to 0). */
	if (qc_exists & a4_exists)
	{
		aad_hdr[len] = (*(pHdr + 30)) & 0x0f;   /* Qos_TC*/
		aad_hdr[len + 1] = 0x00;
		len += 2;
	}
	else if (qc_exists & !a4_exists)
	{
		aad_hdr[len] = (*(pHdr + 24)) & 0x0f;   /* Qos_TC*/
		aad_hdr[len + 1] = 0x00;
		len += 2;
	}	

	*aad_len = len;	
}

/*
	========================================================================
	
	Routine Description:
		Construct NONCE header of CCMP.

	Arguments:
		
	Return Value:

	Note:
				
	========================================================================
*/
VOID RTMPConstructCCMPNonce(
	IN 	PUCHAR 			pHdr,
	IN	UINT8 			a4_exists,
	IN	UINT8			qc_exists,
	IN	BOOLEAN			isMgmtFrame,
	IN	UCHAR			*pn,		
	OUT	UCHAR			*nonce_hdr,
	OUT UINT			*nonce_hdr_len)
{
	UINT	n_offset = 0;
	INT		i;

	/* 	Decide the Priority Octet 
		The Priority sub-field of the Nonce Flags field shall 
		be set to the fixed value 0 when there is no QC field 
		present in the MPDU header. When the QC field is present, 
		bits 0 to 3 of the Priority field shall be set to the 
		value of the QC TID (bits 0 to 3 of the QC field).*/
	if (qc_exists && a4_exists) 
		nonce_hdr[0] = (*(pHdr + 30)) & 0x0f;
	if (qc_exists && !a4_exists) 
		nonce_hdr[0] = (*(pHdr + 24)) & 0x0f;

	n_offset += 1;

	/* Fill in MPDU Address A2 field */	
	NdisMoveMemory(&nonce_hdr[n_offset], pHdr + 10, MAC_ADDR_LEN);
	n_offset += MAC_ADDR_LEN;

	/* 	Fill in the PN. The PN field occupies octets 7¡V12. 
		The octets of PN shall be ordered so that PN0 is at octet index 12
		and PN5 is at octet index 7. */
 	for (i = 0; i < 6; i++)
		nonce_hdr[n_offset + i] = pn[5 - i];
	n_offset += LEN_PN;

	*nonce_hdr_len = n_offset;
	
}

/*
	========================================================================
	
	Routine Description:
		Construct CCMP header.

	Arguments:
		
	Return Value:

	Note:
		It's a 8-octets header.
				
	========================================================================
*/
VOID RTMPConstructCCMPHdr(
	IN	UINT8 			key_idx,
	IN	UCHAR			*pn,		
	OUT	UCHAR			*ccmp_hdr)
{
	NdisZeroMemory(ccmp_hdr, LEN_CCMP_HDR);

	ccmp_hdr[0] = pn[0];
	ccmp_hdr[1] = pn[1];
	ccmp_hdr[3] = (key_idx <<6) | 0x20;	
	ccmp_hdr[4] = pn[2];
	ccmp_hdr[5] = pn[3];
	ccmp_hdr[6] = pn[4];
	ccmp_hdr[7] = pn[5];
}

/*
	========================================================================
	
	Routine Description:

	Arguments:
		
	Return Value:

	Note:
					
	========================================================================
*/
BOOLEAN RTMPSoftEncryptCCMP(
	IN 	PRTMP_ADAPTER 	pAd,
	IN 	PUCHAR			pHdr,
	IN	PUCHAR			pIV,
	IN 		PUCHAR			pKey,
	INOUT 	PUCHAR			pData,
	IN 	UINT32			DataLen)
{
	UINT8			frame_type, frame_subtype;
	UINT8			from_ds, to_ds;
	UINT8 			a4_exists, qc_exists;
	UINT8			aad_hdr[30];
	UINT			aad_len = 0;
	UINT8			nonce_hdr[13];	
	UINT32			nonce_hdr_len = 0;
	UINT32			out_len = DataLen + 8;
		
#ifdef RT_BIG_ENDIAN
	RTMPFrameEndianChange(pAd, (PUCHAR)pHdr, DIR_READ, FALSE);
#endif

	/* Initial variable */
	NdisZeroMemory(aad_hdr, 30);
	NdisZeroMemory(nonce_hdr, 13);

	/* Indicate type and subtype of Frame Control field */
	frame_type = (((*pHdr) >> 2) & 0x03);
	frame_subtype = (((*pHdr) >> 4) & 0x0f);	

	/* Indicate the fromDS and ToDS */
	from_ds = ((*(pHdr + 1)) & 0x2) >> 1;
	to_ds = ((*(pHdr + 1)) & 0x1);
			
	/* decide if the Address 4 exist or QoS exist */
	a4_exists = (from_ds & to_ds);
	qc_exists = ((frame_subtype == SUBTYPE_QDATA) || 
				 (frame_subtype == SUBTYPE_QDATA_CFACK) ||
				 (frame_subtype == SUBTYPE_QDATA_CFPOLL) ||
				 (frame_subtype == SUBTYPE_QDATA_CFACK_CFPOLL));

	/* Construct AAD header */
	RTMPConstructCCMPAAD(pHdr, 
						 (frame_type == BTYPE_DATA), 
						 a4_exists,
						 qc_exists,
						 aad_hdr, 
						 &aad_len);

	/* Construct NONCE header */
	RTMPConstructCCMPNonce(pHdr, 
						   a4_exists,
						   qc_exists,
						   (frame_type == BTYPE_MGMT), 
						   pIV, 
						   nonce_hdr,
						   &nonce_hdr_len);

	/* CCM originator processing -
	   Use the temporal key, AAD, nonce, and MPDU data to 
	   form the cipher text and MIC. */
	if (AES_CCM_Encrypt(pData, DataLen, 
					pKey, 16, 
					nonce_hdr, nonce_hdr_len, 
					aad_hdr, aad_len, LEN_CCMP_MIC, 
					pData, &out_len))
		return FALSE;
		
#ifdef RT_BIG_ENDIAN
	RTMPFrameEndianChange(pAd, (PUCHAR)pHdr, DIR_READ, FALSE);
#endif
	
	return TRUE;
}

/*
	========================================================================
	
	Routine Description:
		Decrypt data with CCMP.

	Arguments:
		
	Return Value:

	Note:
		
	========================================================================
*/
BOOLEAN RTMPSoftDecryptCCMP(
	IN 		PRTMP_ADAPTER 	pAd,
	IN 		PUCHAR			pHdr,
	IN 		PCIPHER_KEY		pKey,
	INOUT 	PUCHAR			pData,
	INOUT 	UINT16			*DataLen)
{
	UINT8			frame_type, frame_subtype;
	UINT8			from_ds, to_ds;
	UINT8 			a4_exists, qc_exists;
	UINT8			aad_hdr[30];
	UINT			aad_len = 0;
	UINT8			pn[LEN_PN];	
	PUCHAR			cipherData_ptr;
	UINT32			cipherData_len;
	UINT8			nonce_hdr[13];	
	UINT32			nonce_hdr_len = 0;	
	UINT32			out_len = *DataLen;

#ifdef RT_BIG_ENDIAN
	RTMPFrameEndianChange(pAd, (PUCHAR)pHdr, DIR_READ, FALSE);
#endif

	/* Check the key is valid */
	if (pKey->KeyLen == 0)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s : The key is not available !\n", __FUNCTION__));
		return FALSE;
	}

	/* Initial variable */
	NdisZeroMemory(aad_hdr, 30);
	NdisZeroMemory(nonce_hdr, 13);

	/* Indicate type and subtype of Frame Control field */
	frame_type = (((*pHdr) >> 2) & 0x03);
	frame_subtype = (((*pHdr) >> 4) & 0x0f);	

	/* Indicate the fromDS and ToDS */
	from_ds = ((*(pHdr + 1)) & 0x2) >> 1;
	to_ds = ((*(pHdr + 1)) & 0x1);

	/* decide if the Address 4 exist or QoS exist */
	a4_exists = (from_ds & to_ds);
	qc_exists = ((frame_subtype == SUBTYPE_QDATA) || 
				 (frame_subtype == SUBTYPE_QDATA_CFACK) ||
				 (frame_subtype == SUBTYPE_QDATA_CFPOLL) ||
				 (frame_subtype == SUBTYPE_QDATA_CFACK_CFPOLL));	
			
	/* Extract PN and from CCMP header */
	pn[0] =	pData[0];
	pn[1] = pData[1];
	pn[2] = pData[4];
	pn[3] = pData[5];
	pn[4] = pData[6];
	pn[5] = pData[7];

	/* skip ccmp header */
	cipherData_ptr = pData + LEN_CCMP_HDR;
	cipherData_len = *DataLen - LEN_CCMP_HDR;
		
	/* Construct AAD header */
	RTMPConstructCCMPAAD(pHdr, 
						 (frame_type == BTYPE_DATA), 
						 a4_exists,
						 qc_exists,
						 aad_hdr, 
						 &aad_len);

	/* Construct NONCE header */
	RTMPConstructCCMPNonce(pHdr, 
						   a4_exists,
						   qc_exists,
						   (frame_type == BTYPE_MGMT), 
						   pn, 
						   nonce_hdr,
						   &nonce_hdr_len);
	
	/* CCM recipient processing -
	   uses the temporal key, AAD, nonce, MIC, 
	   and MPDU cipher text data */
	if (AES_CCM_Decrypt(cipherData_ptr, cipherData_len,
					pKey->Key, 16, 
					nonce_hdr, nonce_hdr_len, 
					aad_hdr, aad_len, LEN_CCMP_MIC, 
					pData, &out_len))
		return FALSE;

	*DataLen = out_len;

#ifdef RT_BIG_ENDIAN
	RTMPFrameEndianChange(pAd, (PUCHAR)pHdr, DIR_READ, FALSE);
#endif

	return TRUE;
}

/*
	========================================================================
	
	Routine Description:
		CCMP test vector

	Arguments:
		
	Return Value:

	Note:		
					
	========================================================================
*/
VOID CCMP_test_vector(
	IN 	PRTMP_ADAPTER 	pAd,
	IN	INT 			input)
{
	UINT8 Key_ID = 0;
	/*UINT8 A1[6] =  {0x0f, 0xd2, 0xe1, 0x28, 0xa5, 0x7c};*/
	/*UINT8 A2[6] =  {0x50, 0x30, 0xf1, 0x84, 0x44, 0x08};*/
	/*UINT8 A3[6] =  {0xab, 0xae, 0xa5, 0xb8, 0xfc, 0xba};*/
	UINT8 TK[16] = {0xc9, 0x7c, 0x1f, 0x67, 0xce, 0x37, 0x11, 0x85, 
				  	0x51, 0x4a, 0x8a, 0x19, 0xf2, 0xbd, 0xd5, 0x2f};
	UINT8 PN[6] =  {0x0C, 0xE7, 0x76, 0x97, 0x03, 0xB5};					
	UINT8 HDR[24]= {0x08, 0x48, 0xc3, 0x2c, 0x0f, 0xd2, 0xe1, 0x28, 
					0xa5, 0x7c, 0x50, 0x30, 0xf1, 0x84, 0x44, 0x08, 
					0xab, 0xae, 0xa5, 0xb8, 0xfc, 0xba, 0x80, 0x33};
	UINT8 AAD[22] = {0x08, 0x40, 0x0f, 0xd2, 0xe1, 0x28, 0xa5, 0x7c, 
				     0x50, 0x30, 0xf1, 0x84, 0x44, 0x08, 0xab, 0xae, 
				     0xa5, 0xb8, 0xfc, 0xba, 0x00, 0x00};
	UINT8 CCMP_HDR[8] = {0x0c, 0xe7, 0x00, 0x20, 0x76, 0x97, 0x03, 0xb5};
	UINT8 CCM_NONCE[13] = {0x00, 0x50, 0x30, 0xf1, 0x84, 0x44, 0x08, 0xb5, 
						   0x03, 0x97, 0x76, 0xe7, 0x0c};
	UINT8 P_TEXT_DATA[20] = {0xf8, 0xba, 0x1a, 0x55, 0xd0, 0x2f, 0x85, 0xae, 
						     0x96, 0x7b, 0xb6, 0x2f, 0xb6, 0xcd, 0xa8, 0xeb, 
						     0x7e, 0x78, 0xa0, 0x50};
	UINT8 C_TEXT_DATA[28] = {0xf3, 0xd0, 0xa2, 0xfe, 0x9a, 0x3d, 0xbf, 0x23,
							 0x42, 0xa6, 0x43, 0xe4, 0x32, 0x46, 0xe8, 0x0c, 
							 0x3c, 0x04, 0xd0, 0x19, 0x78, 0x45, 0xce, 0x0b,
							 0x16, 0xf9, 0x76, 0x23};		
	UINT8	res_buf[100];
	UINT	res_len = 0;

	printk("== CCMP test vector == \n");

	/* Check AAD */
	NdisZeroMemory(res_buf, 100);
	res_len = 0;
	RTMPConstructCCMPAAD(HDR, TRUE, 0, 0, res_buf, &res_len);
	if (res_len == 22 && NdisEqualMemory(res_buf, AAD, res_len))
		printk("Construct AAD is OK!!!\n");
	else
	{
		printk("\n!!!Construct AAD is FAILURE!!!\n\n");
		hex_dump("Calculate AAD", res_buf, res_len);
	}
	/* Check NONCE */
	NdisZeroMemory(res_buf, 100);
	res_len = 0;
	RTMPConstructCCMPNonce(HDR, 0, 0, FALSE, PN, res_buf, &res_len);
	if (res_len == 13 && NdisEqualMemory(res_buf, CCM_NONCE, res_len))
		printk("Construct NONCE is OK!!!\n");
	else
	{
		printk("\n!!!Construct NONCE is FAILURE!!!\n\n");
		hex_dump("Calculate NONCE", res_buf, res_len);
	}
	/* Check CCMP-Header */
	NdisZeroMemory(res_buf, 100);
	res_len = 0;
	RTMPConstructCCMPHdr(Key_ID, PN, res_buf);
	if (NdisEqualMemory(res_buf, CCMP_HDR, 8))
		printk("Construct CCMP_HDR is OK!!!\n");
	else
	{
		printk("\n!!!Construct CCMP_HDR is FAILURE!!!\n\n");
		hex_dump("Calculate CCMP_HDR", res_buf, 8);
	}

	/* Encrypt action */
	NdisZeroMemory(res_buf, 100);	
	NdisMoveMemory(res_buf, P_TEXT_DATA, sizeof(P_TEXT_DATA));
	res_len = sizeof(C_TEXT_DATA);
	if (AES_CCM_Encrypt(res_buf, sizeof(P_TEXT_DATA), 
					TK, sizeof(TK), 
					CCM_NONCE, sizeof(CCM_NONCE), 
					AAD, sizeof(AAD), 8, 
					res_buf, &res_len) == 0)
	{
		if (res_len == sizeof(C_TEXT_DATA) && 
				NdisEqualMemory(res_buf, C_TEXT_DATA, res_len))
			printk("CCM_Encrypt is OK!!!\n");
		else
		{
			printk("\n!!!CCM_Encrypt is FAILURE!!!\n\n");
			hex_dump("CCM_Encrypt", res_buf, res_len);
		}
	}
	
	/* Decrypt action */
	NdisZeroMemory(res_buf, 100);
	NdisMoveMemory(res_buf, C_TEXT_DATA, sizeof(C_TEXT_DATA));
	res_len = sizeof(P_TEXT_DATA);
	if (AES_CCM_Decrypt(res_buf, sizeof(C_TEXT_DATA), TK, 16, 
					CCM_NONCE, sizeof(CCM_NONCE), 
					AAD, sizeof(AAD), 8, 
					res_buf, &res_len) == 0)
	{
		if (res_len == sizeof(P_TEXT_DATA) && 
				NdisEqualMemory(res_buf, P_TEXT_DATA, res_len))
			printk("CCM_Decrypt is OK!!!\n");
		else
		{
			printk("\n!!!CCM_Decrypt is FAILURE!!!\n\n");
			hex_dump("CCM_Decrypt", res_buf, res_len);
		}
	}	
	

	printk("== CCMP test vector == \n");

	}



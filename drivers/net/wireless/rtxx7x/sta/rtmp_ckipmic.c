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


#include "rt_config.h"
#include "rtmp_ckipmic.h"

#define MIC_ACCUM(v)            pContext->accum += (ULONGLONG)v * RTMPMicGetCoefficient(pContext)
#define GB(p,i,s)               ( ((ULONG) *((UCHAR*)(p)+i) ) << (s) )
#define GETBIG32(p)             GB(p,0,24)|GB(p,1,16)|GB(p,2,8)|GB(p,3,0)

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

/*===========================================================================*/
/*=================== CKIP KEY PERMUTATION ==================================*/
/*===========================================================================*/

/* 2-byte by 2-byte subset of the full AES table */
static const USHORT Sbox[256] =
{
    0xC6A5,0xF884,0xEE99,0xF68D,0xFF0D,0xD6BD,0xDEB1,0x9154,
    0x6050,0x0203,0xCEA9,0x567D,0xE719,0xB562,0x4DE6,0xEC9A,
    0x8F45,0x1F9D,0x8940,0xFA87,0xEF15,0xB2EB,0x8EC9,0xFB0B,
    0x41EC,0xB367,0x5FFD,0x45EA,0x23BF,0x53F7,0xE496,0x9B5B,
    0x75C2,0xE11C,0x3DAE,0x4C6A,0x6C5A,0x7E41,0xF502,0x834F,
    0x685C,0x51F4,0xD134,0xF908,0xE293,0xAB73,0x6253,0x2A3F,
    0x080C,0x9552,0x4665,0x9D5E,0x3028,0x37A1,0x0A0F,0x2FB5,
    0x0E09,0x2436,0x1B9B,0xDF3D,0xCD26,0x4E69,0x7FCD,0xEA9F,
    0x121B,0x1D9E,0x5874,0x342E,0x362D,0xDCB2,0xB4EE,0x5BFB,
    0xA4F6,0x764D,0xB761,0x7DCE,0x527B,0xDD3E,0x5E71,0x1397,
    0xA6F5,0xB968,0x0000,0xC12C,0x4060,0xE31F,0x79C8,0xB6ED,
    0xD4BE,0x8D46,0x67D9,0x724B,0x94DE,0x98D4,0xB0E8,0x854A,
    0xBB6B,0xC52A,0x4FE5,0xED16,0x86C5,0x9AD7,0x6655,0x1194,
    0x8ACF,0xE910,0x0406,0xFE81,0xA0F0,0x7844,0x25BA,0x4BE3,
    0xA2F3,0x5DFE,0x80C0,0x058A,0x3FAD,0x21BC,0x7048,0xF104,
    0x63DF,0x77C1,0xAF75,0x4263,0x2030,0xE51A,0xFD0E,0xBF6D,
    0x814C,0x1814,0x2635,0xC32F,0xBEE1,0x35A2,0x88CC,0x2E39,
    0x9357,0x55F2,0xFC82,0x7A47,0xC8AC,0xBAE7,0x322B,0xE695,
    0xC0A0,0x1998,0x9ED1,0xA37F,0x4466,0x547E,0x3BAB,0x0B83,
    0x8CCA,0xC729,0x6BD3,0x283C,0xA779,0xBCE2,0x161D,0xAD76,
    0xDB3B,0x6456,0x744E,0x141E,0x92DB,0x0C0A,0x486C,0xB8E4,
    0x9F5D,0xBD6E,0x43EF,0xC4A6,0x39A8,0x31A4,0xD337,0xF28B,
    0xD532,0x8B43,0x6E59,0xDAB7,0x018C,0xB164,0x9CD2,0x49E0,
    0xD8B4,0xACFA,0xF307,0xCF25,0xCAAF,0xF48E,0x47E9,0x1018,
    0x6FD5,0xF088,0x4A6F,0x5C72,0x3824,0x57F1,0x73C7,0x9751,
    0xCB23,0xA17C,0xE89C,0x3E21,0x96DD,0x61DC,0x0D86,0x0F85,
    0xE090,0x7C42,0x71C4,0xCCAA,0x90D8,0x0605,0xF701,0x1C12,
    0xC2A3,0x6A5F,0xAEF9,0x69D0,0x1791,0x9958,0x3A27,0x27B9,
    0xD938,0xEB13,0x2BB3,0x2233,0xD2BB,0xA970,0x0789,0x33A7,
    0x2DB6,0x3C22,0x1592,0xC920,0x8749,0xAAFF,0x5078,0xA57A,
    0x038F,0x59F8,0x0980,0x1A17,0x65DA,0xD731,0x84C6,0xD0B8,
    0x82C3,0x29B0,0x5A77,0x1E11,0x7BCB,0xA8FC,0x6DD6,0x2C3A
    };

#define Lo8(v16)     ((v16)       & 0xFF)
#define Hi8(v16)    (((v16) >> 8) & 0xFF)
#define u16Swap(i)  ( (((i) >> 8) & 0xFF) | (((i) << 8) & 0xFF00) )
#define _S_(i)      (Sbox[Lo8(i)] ^ u16Swap(Sbox[Hi8(i)]))

#define rotLeft_1(x) ((((x) << 1) | ((x) >> 15)) & 0xFFFF)
VOID CKIP_key_permute
    (
     OUT UCHAR  *PK,           /* output permuted key */
     IN UCHAR *CK,           /* input CKIP key */
     IN UCHAR  toDsFromDs,    /* input toDs/FromDs bits */
     IN UCHAR *piv           /* input pointer to IV */
     )
{
    int i;
    USHORT H[2], tmp;          /* H=32-bits of per-packet hash value */
    USHORT L[8], R[8];         /* L=u16 array of CK, R=u16 array of PK */

    /* build L from input key */
    memset(L, 0, sizeof(L));
    for (i=0; i<16; i++) {
        L[i>>1] |= ( ((USHORT)(CK[i])) << ( i & 1 ? 8 : 0) );
    }

    H[0] = (((USHORT)piv[0]) << 8) + piv[1];
    H[1] = ( ((USHORT)toDsFromDs) << 8) | piv[2];

    for (i=0; i<8; i++) {
        H[0] ^= L[i];           /* 16-bits of key material */
        tmp   = _S_(H[0]);      /* 16x16 permutation */
        H[0]  = tmp ^ H[1];     /* set up for next round */
        H[1]  = tmp;
        R[i]  = H[0];           /* store into key array  */
    }
    
    /* sweep in the other direction */
    tmp=L[0];
    for (i=7; i>0; i--) {
        R[i] = tmp = rotLeft_1(tmp) + R[i];
    }
    
    /* IV of the permuted key is unchanged */
    PK[0] = piv[0];
    PK[1] = piv[1];
    PK[2] = piv[2];

    /* key portion of the permuted key is changed */
    for (i=3; i<16; i++) {
        PK[i] = (UCHAR) (R[i>>1] >> (i & 1 ? 8 : 0));
    }
}    

/* prepare for calculation of a new mic */
VOID RTMPCkipMicInit(
    IN  PMIC_CONTEXT        pContext,
    IN  PUCHAR              CK)
{
    /* prepare for new mic calculation */
    NdisMoveMemory(pContext->CK, CK, sizeof(pContext->CK));
    pContext->accum = 0;
    pContext->position = 0;
}

/* add some bytes to the mic calculation */
VOID RTMPMicUpdate(
    IN  PMIC_CONTEXT        pContext,
    IN  PUCHAR              pOctets,
    IN  INT                 len)
{
    INT     byte_position;
    ULONG   val;

    byte_position = (pContext->position & 3);
    while (len > 0) {
        /* build a 32-bit word for MIC multiply accumulate */
        do {
            if (len == 0) return;
            pContext->part[byte_position++] = *pOctets++;
            pContext->position++;
            len--;
        } while (byte_position < 4);
        /* have a full 32-bit word to process */
        val = GETBIG32(&pContext->part[0]);
        MIC_ACCUM(val);
        byte_position = 0;
    }
}

ULONG RTMPMicGetCoefficient(
    IN  PMIC_CONTEXT         pContext)
{
    UCHAR   aes_counter[16];
    INT     coeff_position;
    UCHAR   *p;

    coeff_position = (pContext->position - 1) >> 2;
    if ( (coeff_position & 3) == 0) {
        /* fetching the first coefficient -- get new 16-byte aes counter output */
        u32 counter = (coeff_position >> 2);
            
        /* new counter value */
        memset(&aes_counter[0], 0, sizeof(aes_counter));
        aes_counter[15] = (UINT8)(counter >> 0);
        aes_counter[14] = (UINT8)(counter >> 8);
        aes_counter[13] = (UINT8)(counter >> 16);
        aes_counter[12] = (UINT8)(counter >> 24);

        RTMPAesEncrypt(&pContext->CK[0], &aes_counter[0], pContext->coefficient);
    }
    p = &(pContext->coefficient[ (coeff_position & 3) << 2 ]);
    return GETBIG32(p);
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

UCHAR RTMPCkipSbox(
    IN  UCHAR   a)
{
    return SboxTable[(int)a];
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

VOID RTMPAesEncrypt(
    IN  PUCHAR  key,
    IN  PUCHAR  data,
    IN  PUCHAR  ciphertext)
{
    INT             round;
    INT             i;
    UCHAR           intermediatea[16];
    UCHAR           intermediateb[16];
    UCHAR           round_key[16];

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

/* calculate the mic */
VOID RTMPMicFinal(
    IN  PMIC_CONTEXT    pContext,
    OUT UCHAR           digest[4])
{
    INT             byte_position;
    ULONG           val;
    ULONGLONG       sum, utmp;
    LONGLONG        stmp;

    /* deal with partial 32-bit word left over from last update */
    if ( (byte_position = (pContext->position & 3)) != 0) {
        /* have a partial word in part to deal with -- zero unused bytes */
        do {
            pContext->part[byte_position++] = 0;
            pContext->position++;
        } while (byte_position < 4);
        val = GETBIG32(&pContext->part[0]);
        MIC_ACCUM(val);
    }

    /* reduce the accumulated u64 to a 32-bit MIC */
    sum = pContext->accum;
    stmp = (sum  & 0xffffffffL) - ((sum >> 32)  * 15);
    utmp = (stmp & 0xffffffffL) - ((stmp >> 32) * 15);
    sum = utmp & 0xffffffffL;
    if (utmp > 0x10000000fL)
        sum -= 15;

    val = (ULONG)sum;
    digest[0] = (UCHAR)((val>>24) & 0xFF);
    digest[1] = (UCHAR) ((val>>16) & 0xFF);
    digest[2] = (UCHAR) ((val>>8) & 0xFF);
    digest[3] = (UCHAR)((val>>0) & 0xFF);
}

VOID RTMPCkipInsertCMIC(
    IN  PRTMP_ADAPTER   pAd,
    OUT PUCHAR          pMIC,
    IN  PUCHAR          p80211hdr,
    IN  PNDIS_PACKET    pPacket,
    IN  PCIPHER_KEY     pKey,
    IN  PUCHAR          mic_snap)
{
	PACKET_INFO		PacketInfo;
	PUCHAR			pSrcBufVA;
	ULONG			SrcBufLen;
    PUCHAR          pDA, pSA, pProto;
    UCHAR           bigethlen[2];
	UCHAR			ckip_ck[16];
    MIC_CONTEXT     mic_ctx;
    USHORT          payloadlen;
	UCHAR			i;

	if (pKey == NULL)
	{
		DBGPRINT_ERR(("RTMPCkipInsertCMIC, Before to form the CKIP key (CK), pKey can't be NULL\n"));
		return;
	}

    switch (*(p80211hdr+1) & 3)
    {
        case 0: /* FromDs=0, ToDs=0 */
            pDA = p80211hdr+4;
            pSA = p80211hdr+10;
            break;
        case 1: /* FromDs=0, ToDs=1 */
            pDA = p80211hdr+16;
            pSA = p80211hdr+10;
            break;
        case 2: /* FromDs=1, ToDs=0 */
            pDA = p80211hdr+4;
            pSA = p80211hdr+16;
            break;
        case 3: /* FromDs=1, ToDs=1 */
            pDA = p80211hdr+16;
            pSA = p80211hdr+24;
            break;
    }

	RTMP_QueryPacketInfo(pPacket, &PacketInfo, &pSrcBufVA, &SrcBufLen);

    if (SrcBufLen < LENGTH_802_3)
        return;

    pProto = pSrcBufVA + 12;
    payloadlen = PacketInfo.TotalPacketLength - LENGTH_802_3 + 18; /* CKIP_LLC(8)+CMIC(4)+TxSEQ(4)+PROTO(2)=18 */
    
    bigethlen[0] = (unsigned char)(payloadlen >> 8);
    bigethlen[1] = (unsigned char)payloadlen;

	/* */
	/* Encryption Key expansion to form the CKIP Key (CKIP_CK). */
	/* */
	if (pKey->KeyLen < 16)
	{
		for(i = 0; i < (16 / pKey->KeyLen); i++)
		{
			NdisMoveMemory(ckip_ck + i * pKey->KeyLen, 
							pKey->Key, 
							pKey->KeyLen);			
		}
		NdisMoveMemory(ckip_ck + i * pKey->KeyLen,
						pKey->Key,
						16 - (i * pKey->KeyLen));
	}
	else
	{
		NdisMoveMemory(ckip_ck, pKey->Key, pKey->KeyLen);
	}	
    RTMPCkipMicInit(&mic_ctx, ckip_ck);
    RTMPMicUpdate(&mic_ctx, pDA, MAC_ADDR_LEN);            /* MIC <-- DA */
    RTMPMicUpdate(&mic_ctx, pSA, MAC_ADDR_LEN);            /* MIC <-- SA */
    RTMPMicUpdate(&mic_ctx, bigethlen, 2);                 /* MIC <-- payload length starting from CKIP SNAP */
    RTMPMicUpdate(&mic_ctx, mic_snap, 8);                  /* MIC <-- snap header */
    RTMPMicUpdate(&mic_ctx, pAd->StaCfg.TxSEQ, 4);   /* MIC <-- TxSEQ */
    RTMPMicUpdate(&mic_ctx, pProto, 2);                    /* MIC <-- Protocol */

    pSrcBufVA += LENGTH_802_3;
    SrcBufLen -= LENGTH_802_3;

    /* Mic <-- original payload. loop until all payload processed */
    do
    {
        if (SrcBufLen > 0)
            RTMPMicUpdate(&mic_ctx, pSrcBufVA, SrcBufLen); 

		NdisGetNextBuffer(PacketInfo.pFirstBuffer, &PacketInfo.pFirstBuffer);
        if (PacketInfo.pFirstBuffer)
        {
            NDIS_QUERY_BUFFER(PacketInfo.pFirstBuffer, &pSrcBufVA, &SrcBufLen);
        }
        else
            break;
    } while (TRUE);
    
    RTMPMicFinal(&mic_ctx, pMIC);                          /* update MIC */
}


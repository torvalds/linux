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
    md5.c

	Abstract:

	Revision History:
	Who			When			What
	--------	----------		----------------------------------------------
	Name		Date			Modification logs
	jan			10-28-03		Initial
	Rita    	11-23-04		Modify MD5 and SHA-1
	Rita		10-14-05		Modify SHA-1 in big-endian platform
 */
#include "../rt_config.h"

/**
 * md5_mac:
 * @key: pointer to	the	key	used for MAC generation
 * @key_len: length	of the key in bytes
 * @data: pointer to the data area for which the MAC is	generated
 * @data_len: length of	the	data in	bytes
 * @mac: pointer to	the	buffer holding space for the MAC; the buffer should
 * have	space for 128-bit (16 bytes) MD5 hash value
 *
 * md5_mac() determines	the	message	authentication code	by using secure	hash
 * MD5(key | data |	key).
 */
void md5_mac(u8 *key, size_t key_len, u8 *data, size_t data_len, u8 *mac)
{
	MD5_CTX	context;

	MD5Init(&context);
	MD5Update(&context,	key, key_len);
	MD5Update(&context,	data, data_len);
	MD5Update(&context,	key, key_len);
	MD5Final(mac, &context);
}

/**
 * hmac_md5:
 * @key: pointer to	the	key	used for MAC generation
 * @key_len: length	of the key in bytes
 * @data: pointer to the data area for which the MAC is	generated
 * @data_len: length of	the	data in	bytes
 * @mac: pointer to	the	buffer holding space for the MAC; the buffer should
 * have	space for 128-bit (16 bytes) MD5 hash value
 *
 * hmac_md5() determines the message authentication	code using HMAC-MD5.
 * This	implementation is based	on the sample code presented in	RFC	2104.
 */
void hmac_md5(u8 *key, size_t key_len, u8 *data, size_t data_len, u8 *mac)
{
	MD5_CTX	context;
    u8 k_ipad[65]; /* inner padding - key XORd with ipad */
    u8 k_opad[65]; /* outer padding - key XORd with opad */
    u8 tk[16];
	int	i;

	//assert(key != NULL && data != NULL && mac != NULL);

	/* if key is longer	than 64	bytes reset	it to key =	MD5(key) */
	if (key_len	> 64) {
		MD5_CTX	ttcontext;

		MD5Init(&ttcontext);
		MD5Update(&ttcontext, key, key_len);
		MD5Final(tk, &ttcontext);
		//key=(PUCHAR)ttcontext.buf;
		key	= tk;
		key_len	= 16;
	}

	/* the HMAC_MD5	transform looks	like:
	 *
	 * MD5(K XOR opad, MD5(K XOR ipad, text))
	 *
	 * where K is an n byte	key
	 * ipad	is the byte	0x36 repeated 64 times
	 * opad	is the byte	0x5c repeated 64 times
	 * and text	is the data	being protected	*/

	/* start out by	storing	key	in pads	*/
	NdisZeroMemory(k_ipad, sizeof(k_ipad));
	NdisZeroMemory(k_opad,	sizeof(k_opad));
	//assert(key_len < sizeof(k_ipad));
	NdisMoveMemory(k_ipad, key,	key_len);
	NdisMoveMemory(k_opad, key,	key_len);

	/* XOR key with	ipad and opad values */
	for	(i = 0;	i <	64;	i++) {
		k_ipad[i] ^= 0x36;
		k_opad[i] ^= 0x5c;
	}

	/* perform inner MD5 */
	MD5Init(&context);					 /*	init context for 1st pass */
	MD5Update(&context,	k_ipad,	64);	 /*	start with inner pad */
	MD5Update(&context,	data, data_len); /*	then text of datagram */
	MD5Final(mac, &context);			 /*	finish up 1st pass */

	/* perform outer MD5 */
	MD5Init(&context);					 /*	init context for 2nd pass */
	MD5Update(&context,	k_opad,	64);	 /*	start with outer pad */
	MD5Update(&context,	mac, 16);		 /*	then results of	1st	hash */
	MD5Final(mac, &context);			 /*	finish up 2nd pass */
}

#define byteReverse(buf, len)   /* Nothing */

/* ==========================  MD5 implementation =========================== */
// four base functions for MD5
#define MD5_F1(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define MD5_F2(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define MD5_F3(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_F4(x, y, z) ((y) ^ ((x) | (~z)))
#define CYCLIC_LEFT_SHIFT(w, s) (((w) << (s)) | ((w) >> (32-(s))))

#define	MD5Step(f, w, x, y,	z, data, t, s)	\
	( w	+= f(x,	y, z) +	data + t,  w = (CYCLIC_LEFT_SHIFT(w, s)) & 0xffffffff, w +=	x )


/*
 *  Function Description:
 *      Initiate MD5 Context satisfied in RFC 1321
 *
 *  Arguments:
 *      pCtx        Pointer	to MD5 context
 *
 *  Return Value:
 *      None
 */
VOID MD5Init(MD5_CTX *pCtx)
{
    pCtx->Buf[0]=0x67452301;
    pCtx->Buf[1]=0xefcdab89;
    pCtx->Buf[2]=0x98badcfe;
    pCtx->Buf[3]=0x10325476;

    pCtx->LenInBitCount[0]=0;
    pCtx->LenInBitCount[1]=0;
}


/*
 *  Function Description:
 *      Update MD5 Context, allow of an arrary of octets as the next portion
 *      of the message
 *
 *  Arguments:
 *      pCtx		Pointer	to MD5 context
 * 	    pData       Pointer to input data
 *      LenInBytes  The length of input data (unit: byte)
 *
 *  Return Value:
 *      None
 *
 *  Note:
 *      Called after MD5Init or MD5Update(itself)
 */
VOID MD5Update(MD5_CTX *pCtx, UCHAR *pData, UINT32 LenInBytes)
{

    UINT32 TfTimes;
    UINT32 temp;
	unsigned int i;

    temp = pCtx->LenInBitCount[0];

    pCtx->LenInBitCount[0] = (UINT32) (pCtx->LenInBitCount[0] + (LenInBytes << 3));

    if (pCtx->LenInBitCount[0] < temp)
        pCtx->LenInBitCount[1]++;   //carry in

    pCtx->LenInBitCount[1] += LenInBytes >> 29;

    // mod 64 bytes
    temp = (temp >> 3) & 0x3f;

    // process lacks of 64-byte data
    if (temp)
    {
        UCHAR *pAds = (UCHAR *) pCtx->Input + temp;

        if ((temp+LenInBytes) < 64)
        {
            NdisMoveMemory(pAds, (UCHAR *)pData, LenInBytes);
            return;
        }

        NdisMoveMemory(pAds, (UCHAR *)pData, 64-temp);
        byteReverse(pCtx->Input, 16);
        MD5Transform(pCtx->Buf, (UINT32 *)pCtx->Input);

        pData += 64-temp;
        LenInBytes -= 64-temp;
    } // end of if (temp)


    TfTimes = (LenInBytes >> 6);

    for (i=TfTimes; i>0; i--)
    {
        NdisMoveMemory(pCtx->Input, (UCHAR *)pData, 64);
        byteReverse(pCtx->Input, 16);
        MD5Transform(pCtx->Buf, (UINT32 *)pCtx->Input);
        pData += 64;
        LenInBytes -= 64;
    } // end of for

    // buffering lacks of 64-byte data
    if(LenInBytes)
        NdisMoveMemory(pCtx->Input, (UCHAR *)pData, LenInBytes);

}


/*
 *  Function Description:
 *      Append padding bits and length of original message in the tail
 *      The message digest has to be completed in the end
 *
 *  Arguments:
 *      Digest		Output of Digest-Message for MD5
 *  	pCtx        Pointer	to MD5 context
 *
 *  Return Value:
 *      None
 *
 *  Note:
 *      Called after MD5Update
 */
VOID MD5Final(UCHAR Digest[16], MD5_CTX *pCtx)
{
    UCHAR Remainder;
    UCHAR PadLenInBytes;
    UCHAR *pAppend=0;
    unsigned int i;

    Remainder = (UCHAR)((pCtx->LenInBitCount[0] >> 3) & 0x3f);

    PadLenInBytes = (Remainder < 56) ? (56-Remainder) : (120-Remainder);

    pAppend = (UCHAR *)pCtx->Input + Remainder;

    // padding bits without crossing block(64-byte based) boundary
    if (Remainder < 56)
    {
        *pAppend = 0x80;
        PadLenInBytes --;

        NdisZeroMemory((UCHAR *)pCtx->Input + Remainder+1, PadLenInBytes);

		// add data-length field, from low to high
       	for (i=0; i<4; i++)
        {
        	pCtx->Input[56+i] = (UCHAR)((pCtx->LenInBitCount[0] >> (i << 3)) & 0xff);
        	pCtx->Input[60+i] = (UCHAR)((pCtx->LenInBitCount[1] >> (i << 3)) & 0xff);
      	}

        byteReverse(pCtx->Input, 16);
        MD5Transform(pCtx->Buf, (UINT32 *)pCtx->Input);
    } // end of if

    // padding bits with crossing block(64-byte based) boundary
    else
    {
        // the first block ===
        *pAppend = 0x80;
        PadLenInBytes --;

        NdisZeroMemory((UCHAR *)pCtx->Input + Remainder+1, (64-Remainder-1));
        PadLenInBytes -= (64 - Remainder - 1);

        byteReverse(pCtx->Input, 16);
        MD5Transform(pCtx->Buf, (UINT32 *)pCtx->Input);


        // the second block ===
        NdisZeroMemory((UCHAR *)pCtx->Input, PadLenInBytes);

        // add data-length field
        for (i=0; i<4; i++)
        {
        	pCtx->Input[56+i] = (UCHAR)((pCtx->LenInBitCount[0] >> (i << 3)) & 0xff);
        	pCtx->Input[60+i] = (UCHAR)((pCtx->LenInBitCount[1] >> (i << 3)) & 0xff);
      	}

        byteReverse(pCtx->Input, 16);
        MD5Transform(pCtx->Buf, (UINT32 *)pCtx->Input);
    } // end of else


    NdisMoveMemory((UCHAR *)Digest, (UINT32 *)pCtx->Buf, 16); // output
    byteReverse((UCHAR *)Digest, 4);
    NdisZeroMemory(pCtx, sizeof(pCtx)); // memory free
}


/*
 *  Function Description:
 *      The central algorithm of MD5, consists of four rounds and sixteen
 *  	steps per round
 *
 *  Arguments:
 *      Buf     Buffers of four states (output: 16 bytes)
 * 	    Mes     Input data (input: 64 bytes)
 *
 *  Return Value:
 *      None
 *
 *  Note:
 *      Called by MD5Update or MD5Final
 */
VOID MD5Transform(UINT32 Buf[4], UINT32 Mes[16])
{
    UINT32 Reg[4], Temp;
	unsigned int i;

    static UCHAR LShiftVal[16] =
    {
        7, 12, 17, 22,
		5, 9 , 14, 20,
		4, 11, 16, 23,
 		6, 10, 15, 21,
 	};


	// [equal to 4294967296*abs(sin(index))]
    static UINT32 MD5Table[64] =
	{
		0xd76aa478,	0xe8c7b756,	0x242070db,	0xc1bdceee,
		0xf57c0faf,	0x4787c62a,	0xa8304613, 0xfd469501,
		0x698098d8,	0x8b44f7af,	0xffff5bb1,	0x895cd7be,
    	0x6b901122,	0xfd987193,	0xa679438e,	0x49b40821,

    	0xf61e2562,	0xc040b340,	0x265e5a51,	0xe9b6c7aa,
    	0xd62f105d,	0x02441453,	0xd8a1e681,	0xe7d3fbc8,
    	0x21e1cde6,	0xc33707d6,	0xf4d50d87,	0x455a14ed,
    	0xa9e3e905,	0xfcefa3f8,	0x676f02d9,	0x8d2a4c8a,

    	0xfffa3942,	0x8771f681,	0x6d9d6122,	0xfde5380c,
    	0xa4beea44,	0x4bdecfa9,	0xf6bb4b60,	0xbebfbc70,
    	0x289b7ec6,	0xeaa127fa,	0xd4ef3085,	0x04881d05,
    	0xd9d4d039,	0xe6db99e5,	0x1fa27cf8,	0xc4ac5665,

    	0xf4292244,	0x432aff97,	0xab9423a7,	0xfc93a039,
   		0x655b59c3,	0x8f0ccc92,	0xffeff47d,	0x85845dd1,
    	0x6fa87e4f,	0xfe2ce6e0,	0xa3014314,	0x4e0811a1,
    	0xf7537e82,	0xbd3af235,	0x2ad7d2bb,	0xeb86d391
	};


    for (i=0; i<4; i++)
        Reg[i]=Buf[i];


    // 64 steps in MD5 algorithm
    for (i=0; i<16; i++)
    {
        MD5Step(MD5_F1, Reg[0], Reg[1], Reg[2], Reg[3], Mes[i],
                MD5Table[i], LShiftVal[i & 0x3]);

        // one-word right shift
        Temp   = Reg[3];
        Reg[3] = Reg[2];
        Reg[2] = Reg[1];
        Reg[1] = Reg[0];
        Reg[0] = Temp;
    }
    for (i=16; i<32; i++)
    {
        MD5Step(MD5_F2, Reg[0], Reg[1], Reg[2], Reg[3], Mes[(5*(i & 0xf)+1) & 0xf],
                MD5Table[i], LShiftVal[(0x1 << 2)+(i & 0x3)]);

        // one-word right shift
        Temp   = Reg[3];
        Reg[3] = Reg[2];
        Reg[2] = Reg[1];
        Reg[1] = Reg[0];
        Reg[0] = Temp;
    }
    for (i=32; i<48; i++)
    {
        MD5Step(MD5_F3, Reg[0], Reg[1], Reg[2], Reg[3], Mes[(3*(i & 0xf)+5) & 0xf],
                MD5Table[i], LShiftVal[(0x1 << 3)+(i & 0x3)]);

        // one-word right shift
        Temp   = Reg[3];
        Reg[3] = Reg[2];
        Reg[2] = Reg[1];
        Reg[1] = Reg[0];
        Reg[0] = Temp;
    }
    for (i=48; i<64; i++)
    {
        MD5Step(MD5_F4, Reg[0], Reg[1], Reg[2], Reg[3], Mes[(7*(i & 0xf)) & 0xf],
                MD5Table[i], LShiftVal[(0x3 << 2)+(i & 0x3)]);

        // one-word right shift
        Temp   = Reg[3];
        Reg[3] = Reg[2];
        Reg[2] = Reg[1];
        Reg[1] = Reg[0];
        Reg[0] = Temp;
    }


    // (temporary)output
    for (i=0; i<4; i++)
        Buf[i] += Reg[i];

}



/* =========================  SHA-1 implementation ========================== */
// four base functions for SHA-1
#define SHA1_F1(b, c, d)    (((b) & (c)) | ((~b) & (d)))
#define SHA1_F2(b, c, d)    ((b) ^ (c) ^ (d))
#define SHA1_F3(b, c, d)    (((b) & (c)) | ((b) & (d)) | ((c) & (d)))


#define SHA1Step(f, a, b, c, d, e, w, k)    \
    ( e	+= ( f(b, c, d) + w + k + CYCLIC_LEFT_SHIFT(a, 5)) & 0xffffffff, \
      b = CYCLIC_LEFT_SHIFT(b, 30) )

//Initiate SHA-1 Context satisfied in RFC 3174
VOID SHAInit(SHA_CTX *pCtx)
{
    pCtx->Buf[0]=0x67452301;
    pCtx->Buf[1]=0xefcdab89;
    pCtx->Buf[2]=0x98badcfe;
    pCtx->Buf[3]=0x10325476;
    pCtx->Buf[4]=0xc3d2e1f0;

    pCtx->LenInBitCount[0]=0;
    pCtx->LenInBitCount[1]=0;
}

/*
 *  Function Description:
 *      Update SHA-1 Context, allow of an arrary of octets as the next
 *      portion of the message
 *
 *  Arguments:
 *      pCtx		Pointer	to SHA-1 context
 * 	    pData       Pointer to input data
 *      LenInBytes  The length of input data (unit: byte)
 *
 *  Return Value:
 *      error       indicate more than pow(2,64) bits of data
 *
 *  Note:
 *      Called after SHAInit or SHAUpdate(itself)
 */
UCHAR SHAUpdate(SHA_CTX *pCtx, UCHAR *pData, UINT32 LenInBytes)
{
    UINT32 TfTimes;
    UINT32 temp1,temp2;
	unsigned int i;
	UCHAR err=1;

    temp1 = pCtx->LenInBitCount[0];
    temp2 = pCtx->LenInBitCount[1];

    pCtx->LenInBitCount[0] = (UINT32) (pCtx->LenInBitCount[0] + (LenInBytes << 3));
    if (pCtx->LenInBitCount[0] < temp1)
        pCtx->LenInBitCount[1]++;   //carry in


    pCtx->LenInBitCount[1] = (UINT32) (pCtx->LenInBitCount[1] +(LenInBytes >> 29));
    if (pCtx->LenInBitCount[1] < temp2)
        return (err);   //check total length of original data


    // mod 64 bytes
    temp1 = (temp1 >> 3) & 0x3f;

    // process lacks of 64-byte data
    if (temp1)
    {
        UCHAR *pAds = (UCHAR *) pCtx->Input + temp1;

        if ((temp1+LenInBytes) < 64)
        {
            NdisMoveMemory(pAds, (UCHAR *)pData, LenInBytes);
            return (0);
        }

        NdisMoveMemory(pAds, (UCHAR *)pData, 64-temp1);
        byteReverse((UCHAR *)pCtx->Input, 16);

        NdisZeroMemory((UCHAR *)pCtx->Input + 64, 16);
        SHATransform(pCtx->Buf, (UINT32 *)pCtx->Input);

        pData += 64-temp1;
        LenInBytes -= 64-temp1;
    } // end of if (temp1)


    TfTimes = (LenInBytes >> 6);

    for (i=TfTimes; i>0; i--)
    {
        NdisMoveMemory(pCtx->Input, (UCHAR *)pData, 64);
        byteReverse((UCHAR *)pCtx->Input, 16);

        NdisZeroMemory((UCHAR *)pCtx->Input + 64, 16);
        SHATransform(pCtx->Buf, (UINT32 *)pCtx->Input);
        pData += 64;
        LenInBytes -= 64;
    } // end of for

    // buffering lacks of 64-byte data
    if(LenInBytes)
        NdisMoveMemory(pCtx->Input, (UCHAR *)pData, LenInBytes);

	return (0);

}

// Append padding bits and length of original message in the tail
// The message digest has to be completed in the end
VOID SHAFinal(SHA_CTX *pCtx, UCHAR Digest[20])
{
    UCHAR Remainder;
    UCHAR PadLenInBytes;
    UCHAR *pAppend=0;
    unsigned int i;

    Remainder = (UCHAR)((pCtx->LenInBitCount[0] >> 3) & 0x3f);

    pAppend = (UCHAR *)pCtx->Input + Remainder;

    PadLenInBytes = (Remainder < 56) ? (56-Remainder) : (120-Remainder);

    // padding bits without crossing block(64-byte based) boundary
    if (Remainder < 56)
    {
        *pAppend = 0x80;
        PadLenInBytes --;

        NdisZeroMemory((UCHAR *)pCtx->Input + Remainder+1, PadLenInBytes);

		// add data-length field, from high to low
        for (i=0; i<4; i++)
        {
        	pCtx->Input[56+i] = (UCHAR)((pCtx->LenInBitCount[1] >> ((3-i) << 3)) & 0xff);
        	pCtx->Input[60+i] = (UCHAR)((pCtx->LenInBitCount[0] >> ((3-i) << 3)) & 0xff);
      	}

        byteReverse((UCHAR *)pCtx->Input, 16);
        NdisZeroMemory((UCHAR *)pCtx->Input + 64, 14);
        SHATransform(pCtx->Buf, (UINT32 *)pCtx->Input);
    } // end of if

    // padding bits with crossing block(64-byte based) boundary
    else
    {
        // the first block ===
        *pAppend = 0x80;
        PadLenInBytes --;

        NdisZeroMemory((UCHAR *)pCtx->Input + Remainder+1, (64-Remainder-1));
        PadLenInBytes -= (64 - Remainder - 1);

        byteReverse((UCHAR *)pCtx->Input, 16);
        NdisZeroMemory((UCHAR *)pCtx->Input + 64, 16);
        SHATransform(pCtx->Buf, (UINT32 *)pCtx->Input);


        // the second block ===
        NdisZeroMemory((UCHAR *)pCtx->Input, PadLenInBytes);

		// add data-length field
		for (i=0; i<4; i++)
        {
        	pCtx->Input[56+i] = (UCHAR)((pCtx->LenInBitCount[1] >> ((3-i) << 3)) & 0xff);
        	pCtx->Input[60+i] = (UCHAR)((pCtx->LenInBitCount[0] >> ((3-i) << 3)) & 0xff);
      	}

        byteReverse((UCHAR *)pCtx->Input, 16);
        NdisZeroMemory((UCHAR *)pCtx->Input + 64, 16);
        SHATransform(pCtx->Buf, (UINT32 *)pCtx->Input);
    } // end of else


    //Output, bytereverse
    for (i=0; i<20; i++)
    {
        Digest [i] = (UCHAR)(pCtx->Buf[i>>2] >> 8*(3-(i & 0x3)));
    }

    NdisZeroMemory(pCtx, sizeof(pCtx)); // memory free
}


// The central algorithm of SHA-1, consists of four rounds and
// twenty steps per round
VOID SHATransform(UINT32 Buf[5], UINT32 Mes[20])
{
    UINT32 Reg[5],Temp;
	unsigned int i;
    UINT32 W[80];

    static UINT32 SHA1Table[4] = { 0x5a827999, 0x6ed9eba1,
                                  0x8f1bbcdc, 0xca62c1d6 };

    Reg[0]=Buf[0];
	Reg[1]=Buf[1];
	Reg[2]=Buf[2];
	Reg[3]=Buf[3];
	Reg[4]=Buf[4];

    //the first octet of a word is stored in the 0th element, bytereverse
	for(i = 0; i < 16; i++)
    {
    	W[i]  = (Mes[i] >> 24) & 0xff;
        W[i] |= (Mes[i] >> 8 ) & 0xff00;
        W[i] |= (Mes[i] << 8 ) & 0xff0000;
        W[i] |= (Mes[i] << 24) & 0xff000000;
    }


    for	(i = 0; i < 64; i++)
	    W[16+i] = CYCLIC_LEFT_SHIFT(W[i] ^ W[2+i] ^ W[8+i] ^ W[13+i], 1);


    // 80 steps in SHA-1 algorithm
    for (i=0; i<80; i++)
    {
        if (i<20)
            SHA1Step(SHA1_F1, Reg[0], Reg[1], Reg[2], Reg[3], Reg[4],
                     W[i], SHA1Table[0]);

        else if (i>=20 && i<40)
            SHA1Step(SHA1_F2, Reg[0], Reg[1], Reg[2], Reg[3], Reg[4],
                     W[i], SHA1Table[1]);

		else if (i>=40 && i<60)
            SHA1Step(SHA1_F3, Reg[0], Reg[1], Reg[2], Reg[3], Reg[4],
                      W[i], SHA1Table[2]);

        else
            SHA1Step(SHA1_F2, Reg[0], Reg[1], Reg[2], Reg[3], Reg[4],
                     W[i], SHA1Table[3]);


       // one-word right shift
		Temp   = Reg[4];
        Reg[4] = Reg[3];
        Reg[3] = Reg[2];
        Reg[2] = Reg[1];
        Reg[1] = Reg[0];
        Reg[0] = Temp;

    } // end of for-loop


    // (temporary)output
    for (i=0; i<5; i++)
        Buf[i] += Reg[i];

}


/* =========================  AES En/Decryption ========================== */

/* forward S-box */
static uint32 FSb[256] =
{
	0x63, 0x7C,	0x77, 0x7B,	0xF2, 0x6B,	0x6F, 0xC5,
	0x30, 0x01,	0x67, 0x2B,	0xFE, 0xD7,	0xAB, 0x76,
	0xCA, 0x82,	0xC9, 0x7D,	0xFA, 0x59,	0x47, 0xF0,
	0xAD, 0xD4,	0xA2, 0xAF,	0x9C, 0xA4,	0x72, 0xC0,
	0xB7, 0xFD,	0x93, 0x26,	0x36, 0x3F,	0xF7, 0xCC,
	0x34, 0xA5,	0xE5, 0xF1,	0x71, 0xD8,	0x31, 0x15,
	0x04, 0xC7,	0x23, 0xC3,	0x18, 0x96,	0x05, 0x9A,
	0x07, 0x12,	0x80, 0xE2,	0xEB, 0x27,	0xB2, 0x75,
	0x09, 0x83,	0x2C, 0x1A,	0x1B, 0x6E,	0x5A, 0xA0,
	0x52, 0x3B,	0xD6, 0xB3,	0x29, 0xE3,	0x2F, 0x84,
	0x53, 0xD1,	0x00, 0xED,	0x20, 0xFC,	0xB1, 0x5B,
	0x6A, 0xCB,	0xBE, 0x39,	0x4A, 0x4C,	0x58, 0xCF,
	0xD0, 0xEF,	0xAA, 0xFB,	0x43, 0x4D,	0x33, 0x85,
	0x45, 0xF9,	0x02, 0x7F,	0x50, 0x3C,	0x9F, 0xA8,
	0x51, 0xA3,	0x40, 0x8F,	0x92, 0x9D,	0x38, 0xF5,
	0xBC, 0xB6,	0xDA, 0x21,	0x10, 0xFF,	0xF3, 0xD2,
	0xCD, 0x0C,	0x13, 0xEC,	0x5F, 0x97,	0x44, 0x17,
	0xC4, 0xA7,	0x7E, 0x3D,	0x64, 0x5D,	0x19, 0x73,
	0x60, 0x81,	0x4F, 0xDC,	0x22, 0x2A,	0x90, 0x88,
	0x46, 0xEE,	0xB8, 0x14,	0xDE, 0x5E,	0x0B, 0xDB,
	0xE0, 0x32,	0x3A, 0x0A,	0x49, 0x06,	0x24, 0x5C,
	0xC2, 0xD3,	0xAC, 0x62,	0x91, 0x95,	0xE4, 0x79,
	0xE7, 0xC8,	0x37, 0x6D,	0x8D, 0xD5,	0x4E, 0xA9,
	0x6C, 0x56,	0xF4, 0xEA,	0x65, 0x7A,	0xAE, 0x08,
	0xBA, 0x78,	0x25, 0x2E,	0x1C, 0xA6,	0xB4, 0xC6,
	0xE8, 0xDD,	0x74, 0x1F,	0x4B, 0xBD,	0x8B, 0x8A,
	0x70, 0x3E,	0xB5, 0x66,	0x48, 0x03,	0xF6, 0x0E,
	0x61, 0x35,	0x57, 0xB9,	0x86, 0xC1,	0x1D, 0x9E,
	0xE1, 0xF8,	0x98, 0x11,	0x69, 0xD9,	0x8E, 0x94,
	0x9B, 0x1E,	0x87, 0xE9,	0xCE, 0x55,	0x28, 0xDF,
	0x8C, 0xA1,	0x89, 0x0D,	0xBF, 0xE6,	0x42, 0x68,
	0x41, 0x99,	0x2D, 0x0F,	0xB0, 0x54,	0xBB, 0x16
};

/* forward table */
#define	FT \
\
	V(C6,63,63,A5),	V(F8,7C,7C,84),	V(EE,77,77,99),	V(F6,7B,7B,8D),	\
	V(FF,F2,F2,0D),	V(D6,6B,6B,BD),	V(DE,6F,6F,B1),	V(91,C5,C5,54),	\
	V(60,30,30,50),	V(02,01,01,03),	V(CE,67,67,A9),	V(56,2B,2B,7D),	\
	V(E7,FE,FE,19),	V(B5,D7,D7,62),	V(4D,AB,AB,E6),	V(EC,76,76,9A),	\
	V(8F,CA,CA,45),	V(1F,82,82,9D),	V(89,C9,C9,40),	V(FA,7D,7D,87),	\
	V(EF,FA,FA,15),	V(B2,59,59,EB),	V(8E,47,47,C9),	V(FB,F0,F0,0B),	\
	V(41,AD,AD,EC),	V(B3,D4,D4,67),	V(5F,A2,A2,FD),	V(45,AF,AF,EA),	\
	V(23,9C,9C,BF),	V(53,A4,A4,F7),	V(E4,72,72,96),	V(9B,C0,C0,5B),	\
	V(75,B7,B7,C2),	V(E1,FD,FD,1C),	V(3D,93,93,AE),	V(4C,26,26,6A),	\
	V(6C,36,36,5A),	V(7E,3F,3F,41),	V(F5,F7,F7,02),	V(83,CC,CC,4F),	\
	V(68,34,34,5C),	V(51,A5,A5,F4),	V(D1,E5,E5,34),	V(F9,F1,F1,08),	\
	V(E2,71,71,93),	V(AB,D8,D8,73),	V(62,31,31,53),	V(2A,15,15,3F),	\
	V(08,04,04,0C),	V(95,C7,C7,52),	V(46,23,23,65),	V(9D,C3,C3,5E),	\
	V(30,18,18,28),	V(37,96,96,A1),	V(0A,05,05,0F),	V(2F,9A,9A,B5),	\
	V(0E,07,07,09),	V(24,12,12,36),	V(1B,80,80,9B),	V(DF,E2,E2,3D),	\
	V(CD,EB,EB,26),	V(4E,27,27,69),	V(7F,B2,B2,CD),	V(EA,75,75,9F),	\
	V(12,09,09,1B),	V(1D,83,83,9E),	V(58,2C,2C,74),	V(34,1A,1A,2E),	\
	V(36,1B,1B,2D),	V(DC,6E,6E,B2),	V(B4,5A,5A,EE),	V(5B,A0,A0,FB),	\
	V(A4,52,52,F6),	V(76,3B,3B,4D),	V(B7,D6,D6,61),	V(7D,B3,B3,CE),	\
	V(52,29,29,7B),	V(DD,E3,E3,3E),	V(5E,2F,2F,71),	V(13,84,84,97),	\
	V(A6,53,53,F5),	V(B9,D1,D1,68),	V(00,00,00,00),	V(C1,ED,ED,2C),	\
	V(40,20,20,60),	V(E3,FC,FC,1F),	V(79,B1,B1,C8),	V(B6,5B,5B,ED),	\
	V(D4,6A,6A,BE),	V(8D,CB,CB,46),	V(67,BE,BE,D9),	V(72,39,39,4B),	\
	V(94,4A,4A,DE),	V(98,4C,4C,D4),	V(B0,58,58,E8),	V(85,CF,CF,4A),	\
	V(BB,D0,D0,6B),	V(C5,EF,EF,2A),	V(4F,AA,AA,E5),	V(ED,FB,FB,16),	\
	V(86,43,43,C5),	V(9A,4D,4D,D7),	V(66,33,33,55),	V(11,85,85,94),	\
	V(8A,45,45,CF),	V(E9,F9,F9,10),	V(04,02,02,06),	V(FE,7F,7F,81),	\
	V(A0,50,50,F0),	V(78,3C,3C,44),	V(25,9F,9F,BA),	V(4B,A8,A8,E3),	\
	V(A2,51,51,F3),	V(5D,A3,A3,FE),	V(80,40,40,C0),	V(05,8F,8F,8A),	\
	V(3F,92,92,AD),	V(21,9D,9D,BC),	V(70,38,38,48),	V(F1,F5,F5,04),	\
	V(63,BC,BC,DF),	V(77,B6,B6,C1),	V(AF,DA,DA,75),	V(42,21,21,63),	\
	V(20,10,10,30),	V(E5,FF,FF,1A),	V(FD,F3,F3,0E),	V(BF,D2,D2,6D),	\
	V(81,CD,CD,4C),	V(18,0C,0C,14),	V(26,13,13,35),	V(C3,EC,EC,2F),	\
	V(BE,5F,5F,E1),	V(35,97,97,A2),	V(88,44,44,CC),	V(2E,17,17,39),	\
	V(93,C4,C4,57),	V(55,A7,A7,F2),	V(FC,7E,7E,82),	V(7A,3D,3D,47),	\
	V(C8,64,64,AC),	V(BA,5D,5D,E7),	V(32,19,19,2B),	V(E6,73,73,95),	\
	V(C0,60,60,A0),	V(19,81,81,98),	V(9E,4F,4F,D1),	V(A3,DC,DC,7F),	\
	V(44,22,22,66),	V(54,2A,2A,7E),	V(3B,90,90,AB),	V(0B,88,88,83),	\
	V(8C,46,46,CA),	V(C7,EE,EE,29),	V(6B,B8,B8,D3),	V(28,14,14,3C),	\
	V(A7,DE,DE,79),	V(BC,5E,5E,E2),	V(16,0B,0B,1D),	V(AD,DB,DB,76),	\
	V(DB,E0,E0,3B),	V(64,32,32,56),	V(74,3A,3A,4E),	V(14,0A,0A,1E),	\
	V(92,49,49,DB),	V(0C,06,06,0A),	V(48,24,24,6C),	V(B8,5C,5C,E4),	\
	V(9F,C2,C2,5D),	V(BD,D3,D3,6E),	V(43,AC,AC,EF),	V(C4,62,62,A6),	\
	V(39,91,91,A8),	V(31,95,95,A4),	V(D3,E4,E4,37),	V(F2,79,79,8B),	\
	V(D5,E7,E7,32),	V(8B,C8,C8,43),	V(6E,37,37,59),	V(DA,6D,6D,B7),	\
	V(01,8D,8D,8C),	V(B1,D5,D5,64),	V(9C,4E,4E,D2),	V(49,A9,A9,E0),	\
	V(D8,6C,6C,B4),	V(AC,56,56,FA),	V(F3,F4,F4,07),	V(CF,EA,EA,25),	\
	V(CA,65,65,AF),	V(F4,7A,7A,8E),	V(47,AE,AE,E9),	V(10,08,08,18),	\
	V(6F,BA,BA,D5),	V(F0,78,78,88),	V(4A,25,25,6F),	V(5C,2E,2E,72),	\
	V(38,1C,1C,24),	V(57,A6,A6,F1),	V(73,B4,B4,C7),	V(97,C6,C6,51),	\
	V(CB,E8,E8,23),	V(A1,DD,DD,7C),	V(E8,74,74,9C),	V(3E,1F,1F,21),	\
	V(96,4B,4B,DD),	V(61,BD,BD,DC),	V(0D,8B,8B,86),	V(0F,8A,8A,85),	\
	V(E0,70,70,90),	V(7C,3E,3E,42),	V(71,B5,B5,C4),	V(CC,66,66,AA),	\
	V(90,48,48,D8),	V(06,03,03,05),	V(F7,F6,F6,01),	V(1C,0E,0E,12),	\
	V(C2,61,61,A3),	V(6A,35,35,5F),	V(AE,57,57,F9),	V(69,B9,B9,D0),	\
	V(17,86,86,91),	V(99,C1,C1,58),	V(3A,1D,1D,27),	V(27,9E,9E,B9),	\
	V(D9,E1,E1,38),	V(EB,F8,F8,13),	V(2B,98,98,B3),	V(22,11,11,33),	\
	V(D2,69,69,BB),	V(A9,D9,D9,70),	V(07,8E,8E,89),	V(33,94,94,A7),	\
	V(2D,9B,9B,B6),	V(3C,1E,1E,22),	V(15,87,87,92),	V(C9,E9,E9,20),	\
	V(87,CE,CE,49),	V(AA,55,55,FF),	V(50,28,28,78),	V(A5,DF,DF,7A),	\
	V(03,8C,8C,8F),	V(59,A1,A1,F8),	V(09,89,89,80),	V(1A,0D,0D,17),	\
	V(65,BF,BF,DA),	V(D7,E6,E6,31),	V(84,42,42,C6),	V(D0,68,68,B8),	\
	V(82,41,41,C3),	V(29,99,99,B0),	V(5A,2D,2D,77),	V(1E,0F,0F,11),	\
	V(7B,B0,B0,CB),	V(A8,54,54,FC),	V(6D,BB,BB,D6),	V(2C,16,16,3A)

#define	V(a,b,c,d) 0x##a##b##c##d
static uint32 FT0[256] = { FT };
#undef V

#define	V(a,b,c,d) 0x##d##a##b##c
static uint32 FT1[256] = { FT };
#undef V

#define	V(a,b,c,d) 0x##c##d##a##b
static uint32 FT2[256] = { FT };
#undef V

#define	V(a,b,c,d) 0x##b##c##d##a
static uint32 FT3[256] = { FT };
#undef V

#undef FT

/* reverse S-box */

static uint32 RSb[256] =
{
	0x52, 0x09,	0x6A, 0xD5,	0x30, 0x36,	0xA5, 0x38,
	0xBF, 0x40,	0xA3, 0x9E,	0x81, 0xF3,	0xD7, 0xFB,
	0x7C, 0xE3,	0x39, 0x82,	0x9B, 0x2F,	0xFF, 0x87,
	0x34, 0x8E,	0x43, 0x44,	0xC4, 0xDE,	0xE9, 0xCB,
	0x54, 0x7B,	0x94, 0x32,	0xA6, 0xC2,	0x23, 0x3D,
	0xEE, 0x4C,	0x95, 0x0B,	0x42, 0xFA,	0xC3, 0x4E,
	0x08, 0x2E,	0xA1, 0x66,	0x28, 0xD9,	0x24, 0xB2,
	0x76, 0x5B,	0xA2, 0x49,	0x6D, 0x8B,	0xD1, 0x25,
	0x72, 0xF8,	0xF6, 0x64,	0x86, 0x68,	0x98, 0x16,
	0xD4, 0xA4,	0x5C, 0xCC,	0x5D, 0x65,	0xB6, 0x92,
	0x6C, 0x70,	0x48, 0x50,	0xFD, 0xED,	0xB9, 0xDA,
	0x5E, 0x15,	0x46, 0x57,	0xA7, 0x8D,	0x9D, 0x84,
	0x90, 0xD8,	0xAB, 0x00,	0x8C, 0xBC,	0xD3, 0x0A,
	0xF7, 0xE4,	0x58, 0x05,	0xB8, 0xB3,	0x45, 0x06,
	0xD0, 0x2C,	0x1E, 0x8F,	0xCA, 0x3F,	0x0F, 0x02,
	0xC1, 0xAF,	0xBD, 0x03,	0x01, 0x13,	0x8A, 0x6B,
	0x3A, 0x91,	0x11, 0x41,	0x4F, 0x67,	0xDC, 0xEA,
	0x97, 0xF2,	0xCF, 0xCE,	0xF0, 0xB4,	0xE6, 0x73,
	0x96, 0xAC,	0x74, 0x22,	0xE7, 0xAD,	0x35, 0x85,
	0xE2, 0xF9,	0x37, 0xE8,	0x1C, 0x75,	0xDF, 0x6E,
	0x47, 0xF1,	0x1A, 0x71,	0x1D, 0x29,	0xC5, 0x89,
	0x6F, 0xB7,	0x62, 0x0E,	0xAA, 0x18,	0xBE, 0x1B,
	0xFC, 0x56,	0x3E, 0x4B,	0xC6, 0xD2,	0x79, 0x20,
	0x9A, 0xDB,	0xC0, 0xFE,	0x78, 0xCD,	0x5A, 0xF4,
	0x1F, 0xDD,	0xA8, 0x33,	0x88, 0x07,	0xC7, 0x31,
	0xB1, 0x12,	0x10, 0x59,	0x27, 0x80,	0xEC, 0x5F,
	0x60, 0x51,	0x7F, 0xA9,	0x19, 0xB5,	0x4A, 0x0D,
	0x2D, 0xE5,	0x7A, 0x9F,	0x93, 0xC9,	0x9C, 0xEF,
	0xA0, 0xE0,	0x3B, 0x4D,	0xAE, 0x2A,	0xF5, 0xB0,
	0xC8, 0xEB,	0xBB, 0x3C,	0x83, 0x53,	0x99, 0x61,
	0x17, 0x2B,	0x04, 0x7E,	0xBA, 0x77,	0xD6, 0x26,
	0xE1, 0x69,	0x14, 0x63,	0x55, 0x21,	0x0C, 0x7D
};

/* reverse table */

#define	RT \
\
	V(51,F4,A7,50),	V(7E,41,65,53),	V(1A,17,A4,C3),	V(3A,27,5E,96),	\
	V(3B,AB,6B,CB),	V(1F,9D,45,F1),	V(AC,FA,58,AB),	V(4B,E3,03,93),	\
	V(20,30,FA,55),	V(AD,76,6D,F6),	V(88,CC,76,91),	V(F5,02,4C,25),	\
	V(4F,E5,D7,FC),	V(C5,2A,CB,D7),	V(26,35,44,80),	V(B5,62,A3,8F),	\
	V(DE,B1,5A,49),	V(25,BA,1B,67),	V(45,EA,0E,98),	V(5D,FE,C0,E1),	\
	V(C3,2F,75,02),	V(81,4C,F0,12),	V(8D,46,97,A3),	V(6B,D3,F9,C6),	\
	V(03,8F,5F,E7),	V(15,92,9C,95),	V(BF,6D,7A,EB),	V(95,52,59,DA),	\
	V(D4,BE,83,2D),	V(58,74,21,D3),	V(49,E0,69,29),	V(8E,C9,C8,44),	\
	V(75,C2,89,6A),	V(F4,8E,79,78),	V(99,58,3E,6B),	V(27,B9,71,DD),	\
	V(BE,E1,4F,B6),	V(F0,88,AD,17),	V(C9,20,AC,66),	V(7D,CE,3A,B4),	\
	V(63,DF,4A,18),	V(E5,1A,31,82),	V(97,51,33,60),	V(62,53,7F,45),	\
	V(B1,64,77,E0),	V(BB,6B,AE,84),	V(FE,81,A0,1C),	V(F9,08,2B,94),	\
	V(70,48,68,58),	V(8F,45,FD,19),	V(94,DE,6C,87),	V(52,7B,F8,B7),	\
	V(AB,73,D3,23),	V(72,4B,02,E2),	V(E3,1F,8F,57),	V(66,55,AB,2A),	\
	V(B2,EB,28,07),	V(2F,B5,C2,03),	V(86,C5,7B,9A),	V(D3,37,08,A5),	\
	V(30,28,87,F2),	V(23,BF,A5,B2),	V(02,03,6A,BA),	V(ED,16,82,5C),	\
	V(8A,CF,1C,2B),	V(A7,79,B4,92),	V(F3,07,F2,F0),	V(4E,69,E2,A1),	\
	V(65,DA,F4,CD),	V(06,05,BE,D5),	V(D1,34,62,1F),	V(C4,A6,FE,8A),	\
	V(34,2E,53,9D),	V(A2,F3,55,A0),	V(05,8A,E1,32),	V(A4,F6,EB,75),	\
	V(0B,83,EC,39),	V(40,60,EF,AA),	V(5E,71,9F,06),	V(BD,6E,10,51),	\
	V(3E,21,8A,F9),	V(96,DD,06,3D),	V(DD,3E,05,AE),	V(4D,E6,BD,46),	\
	V(91,54,8D,B5),	V(71,C4,5D,05),	V(04,06,D4,6F),	V(60,50,15,FF),	\
	V(19,98,FB,24),	V(D6,BD,E9,97),	V(89,40,43,CC),	V(67,D9,9E,77),	\
	V(B0,E8,42,BD),	V(07,89,8B,88),	V(E7,19,5B,38),	V(79,C8,EE,DB),	\
	V(A1,7C,0A,47),	V(7C,42,0F,E9),	V(F8,84,1E,C9),	V(00,00,00,00),	\
	V(09,80,86,83),	V(32,2B,ED,48),	V(1E,11,70,AC),	V(6C,5A,72,4E),	\
	V(FD,0E,FF,FB),	V(0F,85,38,56),	V(3D,AE,D5,1E),	V(36,2D,39,27),	\
	V(0A,0F,D9,64),	V(68,5C,A6,21),	V(9B,5B,54,D1),	V(24,36,2E,3A),	\
	V(0C,0A,67,B1),	V(93,57,E7,0F),	V(B4,EE,96,D2),	V(1B,9B,91,9E),	\
	V(80,C0,C5,4F),	V(61,DC,20,A2),	V(5A,77,4B,69),	V(1C,12,1A,16),	\
	V(E2,93,BA,0A),	V(C0,A0,2A,E5),	V(3C,22,E0,43),	V(12,1B,17,1D),	\
	V(0E,09,0D,0B),	V(F2,8B,C7,AD),	V(2D,B6,A8,B9),	V(14,1E,A9,C8),	\
	V(57,F1,19,85),	V(AF,75,07,4C),	V(EE,99,DD,BB),	V(A3,7F,60,FD),	\
	V(F7,01,26,9F),	V(5C,72,F5,BC),	V(44,66,3B,C5),	V(5B,FB,7E,34),	\
	V(8B,43,29,76),	V(CB,23,C6,DC),	V(B6,ED,FC,68),	V(B8,E4,F1,63),	\
	V(D7,31,DC,CA),	V(42,63,85,10),	V(13,97,22,40),	V(84,C6,11,20),	\
	V(85,4A,24,7D),	V(D2,BB,3D,F8),	V(AE,F9,32,11),	V(C7,29,A1,6D),	\
	V(1D,9E,2F,4B),	V(DC,B2,30,F3),	V(0D,86,52,EC),	V(77,C1,E3,D0),	\
	V(2B,B3,16,6C),	V(A9,70,B9,99),	V(11,94,48,FA),	V(47,E9,64,22),	\
	V(A8,FC,8C,C4),	V(A0,F0,3F,1A),	V(56,7D,2C,D8),	V(22,33,90,EF),	\
	V(87,49,4E,C7),	V(D9,38,D1,C1),	V(8C,CA,A2,FE),	V(98,D4,0B,36),	\
	V(A6,F5,81,CF),	V(A5,7A,DE,28),	V(DA,B7,8E,26),	V(3F,AD,BF,A4),	\
	V(2C,3A,9D,E4),	V(50,78,92,0D),	V(6A,5F,CC,9B),	V(54,7E,46,62),	\
	V(F6,8D,13,C2),	V(90,D8,B8,E8),	V(2E,39,F7,5E),	V(82,C3,AF,F5),	\
	V(9F,5D,80,BE),	V(69,D0,93,7C),	V(6F,D5,2D,A9),	V(CF,25,12,B3),	\
	V(C8,AC,99,3B),	V(10,18,7D,A7),	V(E8,9C,63,6E),	V(DB,3B,BB,7B),	\
	V(CD,26,78,09),	V(6E,59,18,F4),	V(EC,9A,B7,01),	V(83,4F,9A,A8),	\
	V(E6,95,6E,65),	V(AA,FF,E6,7E),	V(21,BC,CF,08),	V(EF,15,E8,E6),	\
	V(BA,E7,9B,D9),	V(4A,6F,36,CE),	V(EA,9F,09,D4),	V(29,B0,7C,D6),	\
	V(31,A4,B2,AF),	V(2A,3F,23,31),	V(C6,A5,94,30),	V(35,A2,66,C0),	\
	V(74,4E,BC,37),	V(FC,82,CA,A6),	V(E0,90,D0,B0),	V(33,A7,D8,15),	\
	V(F1,04,98,4A),	V(41,EC,DA,F7),	V(7F,CD,50,0E),	V(17,91,F6,2F),	\
	V(76,4D,D6,8D),	V(43,EF,B0,4D),	V(CC,AA,4D,54),	V(E4,96,04,DF),	\
	V(9E,D1,B5,E3),	V(4C,6A,88,1B),	V(C1,2C,1F,B8),	V(46,65,51,7F),	\
	V(9D,5E,EA,04),	V(01,8C,35,5D),	V(FA,87,74,73),	V(FB,0B,41,2E),	\
	V(B3,67,1D,5A),	V(92,DB,D2,52),	V(E9,10,56,33),	V(6D,D6,47,13),	\
	V(9A,D7,61,8C),	V(37,A1,0C,7A),	V(59,F8,14,8E),	V(EB,13,3C,89),	\
	V(CE,A9,27,EE),	V(B7,61,C9,35),	V(E1,1C,E5,ED),	V(7A,47,B1,3C),	\
	V(9C,D2,DF,59),	V(55,F2,73,3F),	V(18,14,CE,79),	V(73,C7,37,BF),	\
	V(53,F7,CD,EA),	V(5F,FD,AA,5B),	V(DF,3D,6F,14),	V(78,44,DB,86),	\
	V(CA,AF,F3,81),	V(B9,68,C4,3E),	V(38,24,34,2C),	V(C2,A3,40,5F),	\
	V(16,1D,C3,72),	V(BC,E2,25,0C),	V(28,3C,49,8B),	V(FF,0D,95,41),	\
	V(39,A8,01,71),	V(08,0C,B3,DE),	V(D8,B4,E4,9C),	V(64,56,C1,90),	\
	V(7B,CB,84,61),	V(D5,32,B6,70),	V(48,6C,5C,74),	V(D0,B8,57,42)

#define	V(a,b,c,d) 0x##a##b##c##d
static uint32 RT0[256] = { RT };
#undef V

#define	V(a,b,c,d) 0x##d##a##b##c
static uint32 RT1[256] = { RT };
#undef V

#define	V(a,b,c,d) 0x##c##d##a##b
static uint32 RT2[256] = { RT };
#undef V

#define	V(a,b,c,d) 0x##b##c##d##a
static uint32 RT3[256] = { RT };
#undef V

#undef RT

/* round constants */

static uint32 RCON[10] =
{
	0x01000000,	0x02000000,	0x04000000,	0x08000000,
	0x10000000,	0x20000000,	0x40000000,	0x80000000,
	0x1B000000,	0x36000000
};

/* key schedule	tables */

static int KT_init = 1;

static uint32 KT0[256];
static uint32 KT1[256];
static uint32 KT2[256];
static uint32 KT3[256];

/* platform-independant	32-bit integer manipulation	macros */

#define	GET_UINT32(n,b,i)						\
{												\
	(n)	= (	(uint32) (b)[(i)	] << 24	)		\
		| (	(uint32) (b)[(i) + 1] << 16	)		\
		| (	(uint32) (b)[(i) + 2] <<  8	)		\
		| (	(uint32) (b)[(i) + 3]		);		\
}

#define	PUT_UINT32(n,b,i)						\
{												\
	(b)[(i)	   ] = (uint8) ( (n) >>	24 );		\
	(b)[(i)	+ 1] = (uint8) ( (n) >>	16 );		\
	(b)[(i)	+ 2] = (uint8) ( (n) >>	 8 );		\
	(b)[(i)	+ 3] = (uint8) ( (n)	   );		\
}

/* AES key scheduling routine */

int	rtmp_aes_set_key( aes_context *ctx, uint8 *key, int nbits )
{
	int	i;
	uint32 *RK,	*SK;

	switch(	nbits )
	{
		case 128: ctx->nr =	10;	break;
		case 192: ctx->nr =	12;	break;
		case 256: ctx->nr =	14;	break;
		default	: return( 1	);
	}

	RK = ctx->erk;

	for( i = 0;	i <	(nbits >> 5); i++ )
	{
		GET_UINT32(	RK[i], key,	i *	4 );
	}

	/* setup encryption	round keys */

	switch(	nbits )
	{
	case 128:

		for( i = 0;	i <	10;	i++, RK	+= 4 )
		{
			RK[4]  = RK[0] ^ RCON[i] ^
						( FSb[ (uint8) ( RK[3] >> 16 ) ] <<	24 ) ^
						( FSb[ (uint8) ( RK[3] >>  8 ) ] <<	16 ) ^
						( FSb[ (uint8) ( RK[3]		 ) ] <<	 8 ) ^
						( FSb[ (uint8) ( RK[3] >> 24 ) ]	   );

			RK[5]  = RK[1] ^ RK[4];
			RK[6]  = RK[2] ^ RK[5];
			RK[7]  = RK[3] ^ RK[6];
		}
		break;

	case 192:

		for( i = 0;	i <	8; i++,	RK += 6	)
		{
			RK[6]  = RK[0] ^ RCON[i] ^
						( FSb[ (uint8) ( RK[5] >> 16 ) ] <<	24 ) ^
						( FSb[ (uint8) ( RK[5] >>  8 ) ] <<	16 ) ^
						( FSb[ (uint8) ( RK[5]		 ) ] <<	 8 ) ^
						( FSb[ (uint8) ( RK[5] >> 24 ) ]	   );

			RK[7]  = RK[1] ^ RK[6];
			RK[8]  = RK[2] ^ RK[7];
			RK[9]  = RK[3] ^ RK[8];
			RK[10] = RK[4] ^ RK[9];
			RK[11] = RK[5] ^ RK[10];
		}
		break;

	case 256:

		for( i = 0;	i <	7; i++,	RK += 8	)
		{
			RK[8]  = RK[0] ^ RCON[i] ^
						( FSb[ (uint8) ( RK[7] >> 16 ) ] <<	24 ) ^
						( FSb[ (uint8) ( RK[7] >>  8 ) ] <<	16 ) ^
						( FSb[ (uint8) ( RK[7]		 ) ] <<	 8 ) ^
						( FSb[ (uint8) ( RK[7] >> 24 ) ]	   );

			RK[9]  = RK[1] ^ RK[8];
			RK[10] = RK[2] ^ RK[9];
			RK[11] = RK[3] ^ RK[10];

			RK[12] = RK[4] ^
						( FSb[ (uint8) ( RK[11]	>> 24 )	] << 24	) ^
						( FSb[ (uint8) ( RK[11]	>> 16 )	] << 16	) ^
						( FSb[ (uint8) ( RK[11]	>>	8 )	] <<  8	) ^
						( FSb[ (uint8) ( RK[11]		  )	]		);

			RK[13] = RK[5] ^ RK[12];
			RK[14] = RK[6] ^ RK[13];
			RK[15] = RK[7] ^ RK[14];
		}
		break;
	}

	/* setup decryption	round keys */

	if(	KT_init	)
	{
		for( i = 0;	i <	256; i++ )
		{
			KT0[i] = RT0[ FSb[i] ];
			KT1[i] = RT1[ FSb[i] ];
			KT2[i] = RT2[ FSb[i] ];
			KT3[i] = RT3[ FSb[i] ];
		}

		KT_init	= 0;
	}

	SK = ctx->drk;

	*SK++ =	*RK++;
	*SK++ =	*RK++;
	*SK++ =	*RK++;
	*SK++ =	*RK++;

	for( i = 1;	i <	ctx->nr; i++ )
	{
		RK -= 8;

		*SK++ =	KT0[ (uint8) ( *RK >> 24 ) ] ^
				KT1[ (uint8) ( *RK >> 16 ) ] ^
				KT2[ (uint8) ( *RK >>  8 ) ] ^
				KT3[ (uint8) ( *RK		 ) ]; RK++;

		*SK++ =	KT0[ (uint8) ( *RK >> 24 ) ] ^
				KT1[ (uint8) ( *RK >> 16 ) ] ^
				KT2[ (uint8) ( *RK >>  8 ) ] ^
				KT3[ (uint8) ( *RK		 ) ]; RK++;

		*SK++ =	KT0[ (uint8) ( *RK >> 24 ) ] ^
				KT1[ (uint8) ( *RK >> 16 ) ] ^
				KT2[ (uint8) ( *RK >>  8 ) ] ^
				KT3[ (uint8) ( *RK		 ) ]; RK++;

		*SK++ =	KT0[ (uint8) ( *RK >> 24 ) ] ^
				KT1[ (uint8) ( *RK >> 16 ) ] ^
				KT2[ (uint8) ( *RK >>  8 ) ] ^
				KT3[ (uint8) ( *RK		 ) ]; RK++;
	}

	RK -= 8;

	*SK++ =	*RK++;
	*SK++ =	*RK++;
	*SK++ =	*RK++;
	*SK++ =	*RK++;

	return(	0 );
}

/* AES 128-bit block encryption	routine	*/

void rtmp_aes_encrypt(aes_context *ctx, uint8 input[16],	uint8 output[16] )
{
	uint32 *RK,	X0,	X1,	X2,	X3,	Y0,	Y1,	Y2,	Y3;

	RK = ctx->erk;
	GET_UINT32(	X0,	input,	0 ); X0	^= RK[0];
	GET_UINT32(	X1,	input,	4 ); X1	^= RK[1];
	GET_UINT32(	X2,	input,	8 ); X2	^= RK[2];
	GET_UINT32(	X3,	input, 12 ); X3	^= RK[3];

#define	AES_FROUND(X0,X1,X2,X3,Y0,Y1,Y2,Y3)		\
{												\
	RK += 4;									\
												\
	X0 = RK[0] ^ FT0[ (uint8) (	Y0 >> 24 ) ] ^	\
				 FT1[ (uint8) (	Y1 >> 16 ) ] ^	\
				 FT2[ (uint8) (	Y2 >>  8 ) ] ^	\
				 FT3[ (uint8) (	Y3		 ) ];	\
												\
	X1 = RK[1] ^ FT0[ (uint8) (	Y1 >> 24 ) ] ^	\
				 FT1[ (uint8) (	Y2 >> 16 ) ] ^	\
				 FT2[ (uint8) (	Y3 >>  8 ) ] ^	\
				 FT3[ (uint8) (	Y0		 ) ];	\
												\
	X2 = RK[2] ^ FT0[ (uint8) (	Y2 >> 24 ) ] ^	\
				 FT1[ (uint8) (	Y3 >> 16 ) ] ^	\
				 FT2[ (uint8) (	Y0 >>  8 ) ] ^	\
				 FT3[ (uint8) (	Y1		 ) ];	\
												\
	X3 = RK[3] ^ FT0[ (uint8) (	Y3 >> 24 ) ] ^	\
				 FT1[ (uint8) (	Y0 >> 16 ) ] ^	\
				 FT2[ (uint8) (	Y1 >>  8 ) ] ^	\
				 FT3[ (uint8) (	Y2		 ) ];	\
}

	AES_FROUND(	Y0,	Y1,	Y2,	Y3,	X0,	X1,	X2,	X3 );		/* round 1 */
	AES_FROUND(	X0,	X1,	X2,	X3,	Y0,	Y1,	Y2,	Y3 );		/* round 2 */
	AES_FROUND(	Y0,	Y1,	Y2,	Y3,	X0,	X1,	X2,	X3 );		/* round 3 */
	AES_FROUND(	X0,	X1,	X2,	X3,	Y0,	Y1,	Y2,	Y3 );		/* round 4 */
	AES_FROUND(	Y0,	Y1,	Y2,	Y3,	X0,	X1,	X2,	X3 );		/* round 5 */
	AES_FROUND(	X0,	X1,	X2,	X3,	Y0,	Y1,	Y2,	Y3 );		/* round 6 */
	AES_FROUND(	Y0,	Y1,	Y2,	Y3,	X0,	X1,	X2,	X3 );		/* round 7 */
	AES_FROUND(	X0,	X1,	X2,	X3,	Y0,	Y1,	Y2,	Y3 );		/* round 8 */
	AES_FROUND(	Y0,	Y1,	Y2,	Y3,	X0,	X1,	X2,	X3 );		/* round 9 */

	if(	ctx->nr	> 10 )
	{
		AES_FROUND(	X0,	X1,	X2,	X3,	Y0,	Y1,	Y2,	Y3 );	/* round 10	*/
		AES_FROUND(	Y0,	Y1,	Y2,	Y3,	X0,	X1,	X2,	X3 );	/* round 11	*/
	}

	if(	ctx->nr	> 12 )
	{
		AES_FROUND(	X0,	X1,	X2,	X3,	Y0,	Y1,	Y2,	Y3 );	/* round 12	*/
		AES_FROUND(	Y0,	Y1,	Y2,	Y3,	X0,	X1,	X2,	X3 );	/* round 13	*/
	}

	/* last	round */

	RK += 4;

	X0 = RK[0] ^ ( FSb[	(uint8)	( Y0 >>	24 ) ] << 24 ) ^
				 ( FSb[	(uint8)	( Y1 >>	16 ) ] << 16 ) ^
				 ( FSb[	(uint8)	( Y2 >>	 8 ) ] <<  8 ) ^
				 ( FSb[	(uint8)	( Y3	   ) ]		 );

	X1 = RK[1] ^ ( FSb[	(uint8)	( Y1 >>	24 ) ] << 24 ) ^
				 ( FSb[	(uint8)	( Y2 >>	16 ) ] << 16 ) ^
				 ( FSb[	(uint8)	( Y3 >>	 8 ) ] <<  8 ) ^
				 ( FSb[	(uint8)	( Y0	   ) ]		 );

	X2 = RK[2] ^ ( FSb[	(uint8)	( Y2 >>	24 ) ] << 24 ) ^
				 ( FSb[	(uint8)	( Y3 >>	16 ) ] << 16 ) ^
				 ( FSb[	(uint8)	( Y0 >>	 8 ) ] <<  8 ) ^
				 ( FSb[	(uint8)	( Y1	   ) ]		 );

	X3 = RK[3] ^ ( FSb[	(uint8)	( Y3 >>	24 ) ] << 24 ) ^
				 ( FSb[	(uint8)	( Y0 >>	16 ) ] << 16 ) ^
				 ( FSb[	(uint8)	( Y1 >>	 8 ) ] <<  8 ) ^
				 ( FSb[	(uint8)	( Y2	   ) ]		 );

	PUT_UINT32(	X0,	output,	 0 );
	PUT_UINT32(	X1,	output,	 4 );
	PUT_UINT32(	X2,	output,	 8 );
	PUT_UINT32(	X3,	output,	12 );
}

/* AES 128-bit block decryption	routine	*/

void rtmp_aes_decrypt( aes_context *ctx,	uint8 input[16], uint8 output[16] )
{
	uint32 *RK,	X0,	X1,	X2,	X3,	Y0,	Y1,	Y2,	Y3;

	RK = ctx->drk;

	GET_UINT32(	X0,	input,	0 ); X0	^= RK[0];
	GET_UINT32(	X1,	input,	4 ); X1	^= RK[1];
	GET_UINT32(	X2,	input,	8 ); X2	^= RK[2];
	GET_UINT32(	X3,	input, 12 ); X3	^= RK[3];

#define	AES_RROUND(X0,X1,X2,X3,Y0,Y1,Y2,Y3)		\
{												\
	RK += 4;									\
												\
	X0 = RK[0] ^ RT0[ (uint8) (	Y0 >> 24 ) ] ^	\
				 RT1[ (uint8) (	Y3 >> 16 ) ] ^	\
				 RT2[ (uint8) (	Y2 >>  8 ) ] ^	\
				 RT3[ (uint8) (	Y1		 ) ];	\
												\
	X1 = RK[1] ^ RT0[ (uint8) (	Y1 >> 24 ) ] ^	\
				 RT1[ (uint8) (	Y0 >> 16 ) ] ^	\
				 RT2[ (uint8) (	Y3 >>  8 ) ] ^	\
				 RT3[ (uint8) (	Y2		 ) ];	\
												\
	X2 = RK[2] ^ RT0[ (uint8) (	Y2 >> 24 ) ] ^	\
				 RT1[ (uint8) (	Y1 >> 16 ) ] ^	\
				 RT2[ (uint8) (	Y0 >>  8 ) ] ^	\
				 RT3[ (uint8) (	Y3		 ) ];	\
												\
	X3 = RK[3] ^ RT0[ (uint8) (	Y3 >> 24 ) ] ^	\
				 RT1[ (uint8) (	Y2 >> 16 ) ] ^	\
				 RT2[ (uint8) (	Y1 >>  8 ) ] ^	\
				 RT3[ (uint8) (	Y0		 ) ];	\
}

	AES_RROUND(	Y0,	Y1,	Y2,	Y3,	X0,	X1,	X2,	X3 );		/* round 1 */
	AES_RROUND(	X0,	X1,	X2,	X3,	Y0,	Y1,	Y2,	Y3 );		/* round 2 */
	AES_RROUND(	Y0,	Y1,	Y2,	Y3,	X0,	X1,	X2,	X3 );		/* round 3 */
	AES_RROUND(	X0,	X1,	X2,	X3,	Y0,	Y1,	Y2,	Y3 );		/* round 4 */
	AES_RROUND(	Y0,	Y1,	Y2,	Y3,	X0,	X1,	X2,	X3 );		/* round 5 */
	AES_RROUND(	X0,	X1,	X2,	X3,	Y0,	Y1,	Y2,	Y3 );		/* round 6 */
	AES_RROUND(	Y0,	Y1,	Y2,	Y3,	X0,	X1,	X2,	X3 );		/* round 7 */
	AES_RROUND(	X0,	X1,	X2,	X3,	Y0,	Y1,	Y2,	Y3 );		/* round 8 */
	AES_RROUND(	Y0,	Y1,	Y2,	Y3,	X0,	X1,	X2,	X3 );		/* round 9 */

	if(	ctx->nr	> 10 )
	{
		AES_RROUND(	X0,	X1,	X2,	X3,	Y0,	Y1,	Y2,	Y3 );	/* round 10	*/
		AES_RROUND(	Y0,	Y1,	Y2,	Y3,	X0,	X1,	X2,	X3 );	/* round 11	*/
	}

	if(	ctx->nr	> 12 )
	{
		AES_RROUND(	X0,	X1,	X2,	X3,	Y0,	Y1,	Y2,	Y3 );	/* round 12	*/
		AES_RROUND(	Y0,	Y1,	Y2,	Y3,	X0,	X1,	X2,	X3 );	/* round 13	*/
	}

	/* last	round */

	RK += 4;

	X0 = RK[0] ^ ( RSb[	(uint8)	( Y0 >>	24 ) ] << 24 ) ^
				 ( RSb[	(uint8)	( Y3 >>	16 ) ] << 16 ) ^
				 ( RSb[	(uint8)	( Y2 >>	 8 ) ] <<  8 ) ^
				 ( RSb[	(uint8)	( Y1	   ) ]		 );

	X1 = RK[1] ^ ( RSb[	(uint8)	( Y1 >>	24 ) ] << 24 ) ^
				 ( RSb[	(uint8)	( Y0 >>	16 ) ] << 16 ) ^
				 ( RSb[	(uint8)	( Y3 >>	 8 ) ] <<  8 ) ^
				 ( RSb[	(uint8)	( Y2	   ) ]		 );

	X2 = RK[2] ^ ( RSb[	(uint8)	( Y2 >>	24 ) ] << 24 ) ^
				 ( RSb[	(uint8)	( Y1 >>	16 ) ] << 16 ) ^
				 ( RSb[	(uint8)	( Y0 >>	 8 ) ] <<  8 ) ^
				 ( RSb[	(uint8)	( Y3	   ) ]		 );

	X3 = RK[3] ^ ( RSb[	(uint8)	( Y3 >>	24 ) ] << 24 ) ^
				 ( RSb[	(uint8)	( Y2 >>	16 ) ] << 16 ) ^
				 ( RSb[	(uint8)	( Y1 >>	 8 ) ] <<  8 ) ^
				 ( RSb[	(uint8)	( Y0	   ) ]		 );

	PUT_UINT32(	X0,	output,	 0 );
	PUT_UINT32(	X1,	output,	 4 );
	PUT_UINT32(	X2,	output,	 8 );
	PUT_UINT32(	X3,	output,	12 );
}

/*
	========================================================================

	Routine Description:
		SHA1 function

	Arguments:

	Return Value:

	Note:

	========================================================================
*/
VOID	HMAC_SHA1(
	IN	UCHAR	*text,
	IN	UINT	text_len,
	IN	UCHAR	*key,
	IN	UINT	key_len,
	IN	UCHAR	*digest)
{
	SHA_CTX	context;
	UCHAR	k_ipad[65]; /* inner padding - key XORd with ipad	*/
	UCHAR	k_opad[65]; /* outer padding - key XORd with opad	*/
	INT		i;

	// if key is longer	than 64	bytes reset	it to key=SHA1(key)
	if (key_len	> 64)
	{
		SHA_CTX		 tctx;
		SHAInit(&tctx);
		SHAUpdate(&tctx, key, key_len);
		SHAFinal(&tctx,	key);
		key_len	= 20;
	}
	NdisZeroMemory(k_ipad, sizeof(k_ipad));
	NdisZeroMemory(k_opad, sizeof(k_opad));
	NdisMoveMemory(k_ipad, key,	key_len);
	NdisMoveMemory(k_opad, key,	key_len);

	// XOR key with	ipad and opad values
	for	(i = 0;	i <	64;	i++)
	{
		k_ipad[i] ^= 0x36;
		k_opad[i] ^= 0x5c;
	}

	// perform inner SHA1
	SHAInit(&context); 						/* init context for 1st pass */
	SHAUpdate(&context,	k_ipad,	64);		/*	start with inner pad */
	SHAUpdate(&context,	text, text_len);	/*	then text of datagram */
	SHAFinal(&context, digest);				/* finish up 1st pass */

	//perform outer	SHA1
	SHAInit(&context);					/* init context for 2nd pass */
	SHAUpdate(&context,	k_opad,	64);	/*	start with outer pad */
	SHAUpdate(&context,	digest,	20);	/*	then results of	1st	hash */
	SHAFinal(&context, digest);			/* finish up 2nd pass */

}

/*
* F(P, S, c, i) = U1 xor U2 xor ... Uc
* U1 = PRF(P, S || Int(i))
* U2 = PRF(P, U1)
* Uc = PRF(P, Uc-1)
*/

void F(char *password, unsigned char *ssid, int ssidlength, int iterations, int count, unsigned char *output)
{
    unsigned char digest[36], digest1[SHA_DIGEST_LEN];
    int i, j;

    /* U1 = PRF(P, S || int(i)) */
    memcpy(digest, ssid, ssidlength);
    digest[ssidlength] = (unsigned char)((count>>24) & 0xff);
    digest[ssidlength+1] = (unsigned char)((count>>16) & 0xff);
    digest[ssidlength+2] = (unsigned char)((count>>8) & 0xff);
    digest[ssidlength+3] = (unsigned char)(count & 0xff);
    HMAC_SHA1(digest, ssidlength+4, (unsigned char*) password, (int) strlen(password), digest1); // for WPA update

    /* output = U1 */
    memcpy(output, digest1, SHA_DIGEST_LEN);

    for (i = 1; i < iterations; i++)
    {
        /* Un = PRF(P, Un-1) */
        HMAC_SHA1(digest1, SHA_DIGEST_LEN, (unsigned char*) password, (int) strlen(password), digest); // for WPA update
        memcpy(digest1, digest, SHA_DIGEST_LEN);

        /* output = output xor Un */
        for (j = 0; j < SHA_DIGEST_LEN; j++)
        {
            output[j] ^= digest[j];
        }
    }
}
/*
* password - ascii string up to 63 characters in length
* ssid - octet string up to 32 octets
* ssidlength - length of ssid in octets
* output must be 40 octets in length and outputs 256 bits of key
*/
int PasswordHash(char *password, unsigned char *ssid, int ssidlength, unsigned char *output)
{
    if ((strlen(password) > 63) || (ssidlength > 32))
        return 0;

    F(password, ssid, ssidlength, 4096, 1, output);
    F(password, ssid, ssidlength, 4096, 2, &output[SHA_DIGEST_LEN]);
    return 1;
}



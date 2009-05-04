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
	md5.h

	Abstract:

	Revision History:
	Who			When			What
	--------	----------		----------------------------------------------
	Name		Date			Modification logs
	jan			10-28-03		Initial
	Rita    	11-23-04		Modify MD5 and SHA-1
*/

#ifndef	uint8
#define	uint8  unsigned	char
#endif

#ifndef	uint32
#define	uint32 unsigned	long int
#endif


#ifndef	__MD5_H__
#define	__MD5_H__

#define MD5_MAC_LEN 16

typedef struct _MD5_CTX {
    UINT32   Buf[4];             // buffers of four states
	UCHAR   Input[64];          // input message
	UINT32   LenInBitCount[2];   // length counter for input message, 0 up to 64 bits
}   MD5_CTX;

VOID MD5Init(MD5_CTX *pCtx);
VOID MD5Update(MD5_CTX *pCtx, UCHAR *pData, UINT32 LenInBytes);
VOID MD5Final(UCHAR Digest[16], MD5_CTX *pCtx);
VOID MD5Transform(UINT32 Buf[4], UINT32 Mes[16]);

void md5_mac(u8 *key, size_t key_len, u8 *data, size_t data_len, u8 *mac);
void hmac_md5(u8 *key, size_t key_len, u8 *data, size_t data_len, u8 *mac);

//
// SHA context
//
typedef	struct _SHA_CTX
{
	UINT32   Buf[5];             // buffers of five states
	UCHAR   Input[80];          // input message
	UINT32   LenInBitCount[2];   // length counter for input message, 0 up to 64 bits

}	SHA_CTX;

VOID SHAInit(SHA_CTX *pCtx);
UCHAR SHAUpdate(SHA_CTX *pCtx, UCHAR *pData, UINT32 LenInBytes);
VOID SHAFinal(SHA_CTX *pCtx, UCHAR Digest[20]);
VOID SHATransform(UINT32 Buf[5], UINT32 Mes[20]);

#define SHA_DIGEST_LEN 20
#endif // __MD5_H__

/******************************************************************************/
#ifndef	_AES_H
#define	_AES_H

typedef	struct
{
	uint32 erk[64];		/* encryption round	keys */
	uint32 drk[64];		/* decryption round	keys */
	int	nr;				/* number of rounds	*/
}
aes_context;

int	 rtmp_aes_set_key( aes_context *ctx,	uint8 *key,	int	nbits );
void rtmp_aes_encrypt( aes_context *ctx,	uint8 input[16], uint8 output[16] );
void rtmp_aes_decrypt( aes_context *ctx,	uint8 input[16], uint8 output[16] );

void F(char *password, unsigned char *ssid, int ssidlength, int iterations, int count, unsigned char *output);
int PasswordHash(char *password, unsigned char *ssid, int ssidlength, unsigned char *output);

#endif /* aes.h	*/


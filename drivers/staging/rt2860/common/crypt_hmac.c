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
 *************************************************************************/

#include "../crypt_hmac.h"

#ifdef HMAC_SHA1_SUPPORT
/*
========================================================================
Routine Description:
    HMAC using SHA1 hash function

Arguments:
    key             Secret key
    key_len         The length of the key in bytes
    message         Message context
    message_len     The length of message in bytes
    macLen          Request the length of message authentication code

Return Value:
    mac             Message authentication code

Note:
    None
========================================================================
*/
void HMAC_SHA1(IN const u8 Key[],
	       u32 KeyLen,
	       IN const u8 Message[],
	       u32 MessageLen, u8 MAC[], u32 MACLen)
{
	struct rt_sha1_ctx sha_ctx1;
	struct rt_sha1_ctx sha_ctx2;
	u8 K0[SHA1_BLOCK_SIZE];
	u8 Digest[SHA1_DIGEST_SIZE];
	u32 index;

	NdisZeroMemory(&sha_ctx1, sizeof(struct rt_sha1_ctx));
	NdisZeroMemory(&sha_ctx2, sizeof(struct rt_sha1_ctx));
	/*
	 * If the length of K = B(Block size): K0 = K.
	 * If the length of K > B: hash K to obtain an L byte string,
	 * then append (B-L) zeros to create a B-byte string K0 (i.e., K0 = H(K) || 00...00).
	 * If the length of K < B: append zeros to the end of K to create a B-byte string K0
	 */
	NdisZeroMemory(K0, SHA1_BLOCK_SIZE);
	if (KeyLen <= SHA1_BLOCK_SIZE)
		NdisMoveMemory(K0, Key, KeyLen);
	else
		RT_SHA1(Key, KeyLen, K0);
	/* End of if */

	/* Exclusive-Or K0 with ipad */
	/* ipad: Inner pad; the byte x¡¦36¡¦ repeated B times. */
	for (index = 0; index < SHA1_BLOCK_SIZE; index++)
		K0[index] ^= 0x36;
	/* End of for */

	RT_SHA1_Init(&sha_ctx1);
	/* H(K0^ipad) */
	SHA1_Append(&sha_ctx1, K0, sizeof(K0));
	/* H((K0^ipad)||text) */
	SHA1_Append(&sha_ctx1, Message, MessageLen);
	SHA1_End(&sha_ctx1, Digest);

	/* Exclusive-Or K0 with opad and remove ipad */
	/* opad: Outer pad; the byte x¡¦5c¡¦ repeated B times. */
	for (index = 0; index < SHA1_BLOCK_SIZE; index++)
		K0[index] ^= 0x36 ^ 0x5c;
	/* End of for */

	RT_SHA1_Init(&sha_ctx2);
	/* H(K0^opad) */
	SHA1_Append(&sha_ctx2, K0, sizeof(K0));
	/* H( (K0^opad) || H((K0^ipad)||text) ) */
	SHA1_Append(&sha_ctx2, Digest, SHA1_DIGEST_SIZE);
	SHA1_End(&sha_ctx2, Digest);

	if (MACLen > SHA1_DIGEST_SIZE)
		NdisMoveMemory(MAC, Digest, SHA1_DIGEST_SIZE);
	else
		NdisMoveMemory(MAC, Digest, MACLen);
}				/* End of HMAC_SHA1 */
#endif /* HMAC_SHA1_SUPPORT */

#ifdef HMAC_MD5_SUPPORT
/*
========================================================================
Routine Description:
    HMAC using MD5 hash function

Arguments:
    key             Secret key
    key_len         The length of the key in bytes
    message         Message context
    message_len     The length of message in bytes
    macLen          Request the length of message authentication code

Return Value:
    mac             Message authentication code

Note:
    None
========================================================================
*/
void HMAC_MD5(IN const u8 Key[],
	      u32 KeyLen,
	      IN const u8 Message[],
	      u32 MessageLen, u8 MAC[], u32 MACLen)
{
	struct rt_md5_ctx_struc md5_ctx1;
	struct rt_md5_ctx_struc md5_ctx2;
	u8 K0[MD5_BLOCK_SIZE];
	u8 Digest[MD5_DIGEST_SIZE];
	u32 index;

	NdisZeroMemory(&md5_ctx1, sizeof(struct rt_md5_ctx_struc));
	NdisZeroMemory(&md5_ctx2, sizeof(struct rt_md5_ctx_struc));
	/*
	 * If the length of K = B(Block size): K0 = K.
	 * If the length of K > B: hash K to obtain an L byte string,
	 * then append (B-L) zeros to create a B-byte string K0 (i.e., K0 = H(K) || 00...00).
	 * If the length of K < B: append zeros to the end of K to create a B-byte string K0
	 */
	NdisZeroMemory(K0, MD5_BLOCK_SIZE);
	if (KeyLen <= MD5_BLOCK_SIZE) {
		NdisMoveMemory(K0, Key, KeyLen);
	} else {
		RT_MD5(Key, KeyLen, K0);
	}

	/* Exclusive-Or K0 with ipad */
	/* ipad: Inner pad; the byte x¡¦36¡¦ repeated B times. */
	for (index = 0; index < MD5_BLOCK_SIZE; index++)
		K0[index] ^= 0x36;
	/* End of for */

	MD5_Init(&md5_ctx1);
	/* H(K0^ipad) */
	MD5_Append(&md5_ctx1, K0, sizeof(K0));
	/* H((K0^ipad)||text) */
	MD5_Append(&md5_ctx1, Message, MessageLen);
	MD5_End(&md5_ctx1, Digest);

	/* Exclusive-Or K0 with opad and remove ipad */
	/* opad: Outer pad; the byte x¡¦5c¡¦ repeated B times. */
	for (index = 0; index < MD5_BLOCK_SIZE; index++)
		K0[index] ^= 0x36 ^ 0x5c;
	/* End of for */

	MD5_Init(&md5_ctx2);
	/* H(K0^opad) */
	MD5_Append(&md5_ctx2, K0, sizeof(K0));
	/* H( (K0^opad) || H((K0^ipad)||text) ) */
	MD5_Append(&md5_ctx2, Digest, MD5_DIGEST_SIZE);
	MD5_End(&md5_ctx2, Digest);

	if (MACLen > MD5_DIGEST_SIZE)
		NdisMoveMemory(MAC, Digest, MD5_DIGEST_SIZE);
	else
		NdisMoveMemory(MAC, Digest, MACLen);
}				/* End of HMAC_SHA256 */
#endif /* HMAC_MD5_SUPPORT */

/* End of crypt_hmac.c */

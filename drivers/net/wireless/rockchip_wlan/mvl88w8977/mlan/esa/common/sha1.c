/** @file sha1.c
 *
 *  @brief This file defines the sha1 functions
 *
 * Copyright (C) 2014-2017, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

/******************************************************
Change log:
    03/07/2014: Initial version
******************************************************/
#include "wltypes.h"
#include "sha1.h"
#include "hostsa_ext_def.h"
#include "authenticator.h"

/*
 *  Define the SHA1 circular left shift macro
 */

#define Mrvl_SHA1CircularShift(bits,word) \
                (((word) << (bits)) | ((word) >> (32-(bits))))

/* Local Function Prototyptes */
static void Mrvl_SHA1PadMessage(Mrvl_SHA1_CTX *);
static void Mrvl_SHA1ProcessMessageBlock(Mrvl_SHA1_CTX *);
/*
 *  SHA1Init
 *
 *  Description:
 *      This function will initialize the SHA1_CTX in preparation
 *      for computing a new SHA1 message digest.
 *
 *  Parameters:
 *      context: [in/out]
 *          The context to reset.
 *
 *  Returns:
 *      sha Error Code.
 *
 */
/*int SHA1Init(SHA1_CTX *context) */
int
Mrvl_SHA1Init(Mrvl_SHA1_CTX *context)
{
	if (!context) {
		return shaNull;
	}

	context->Length_Low = 0;
	context->Length_High = 0;
	context->Message_Block_Index = 0;

	context->Intermediate_Hash[0] = 0x67452301;
	context->Intermediate_Hash[1] = 0xEFCDAB89;
	context->Intermediate_Hash[2] = 0x98BADCFE;
	context->Intermediate_Hash[3] = 0x10325476;
	context->Intermediate_Hash[4] = 0xC3D2E1F0;

	context->Computed = 0;
	context->Corrupted = 0;

	return shaSuccess;
}

/*
 *  SHA1Final
 *
 *  Description:
 *      This function will return the 160-bit message digest into the
 *      Message_Digest array  provided by the caller.
 *      NOTE: The first octet of hash is stored in the 0th element,
 *            the last octet of hash in the 19th element.
 *
 *  Parameters:
 *      context: [in/out]
 *          The context to use to calculate the SHA-1 hash.
 *      Message_Digest: [out]
 *          Where the digest is returned.
 *
 *  Returns:
 *      sha Error Code.
 *
 */
int
Mrvl_SHA1Final(void *priv, Mrvl_SHA1_CTX *context,
	       UINT8 Message_Digest[A_SHA_DIGEST_LEN])
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	int i;

	if (!context || !Message_Digest) {
		return shaNull;
	}

	if (context->Corrupted) {
		return context->Corrupted;
	}

	if (!context->Computed) {
		Mrvl_SHA1PadMessage(context);
		for (i = 0; i < 64; ++i) {
			/* message may be sensitive, clear it out */
			context->Message_Block[i] = 0;
		}
		context->Length_Low = 0;	/* and clear length */
		context->Length_High = 0;
		context->Computed = 1;

	}

	for (i = 0; i < A_SHA_DIGEST_LEN; ++i) {
		Message_Digest[i] = context->Intermediate_Hash[i >> 2]
			>> 8 * (3 - (i & 0x03));
	}

	memset(util_fns, context, 0x00, sizeof(Mrvl_SHA1_CTX));

	return shaSuccess;
}

/*
 *  SHA1Update
 *
 *  Description:
 *      This function accepts an array of octets as the next portion
 *      of the message.
 *
 *  Parameters:
 *      context: [in/out]
 *          The SHA context to update
 *      message_array: [in]
 *          An array of characters representing the next portion of
 *          the message.
 *      length: [in]
 *          The length of the message in message_array
 *
 *  Returns:
 *      sha Error Code.
 *
 */
int
Mrvl_SHA1Update(Mrvl_SHA1_CTX *context,
		const UINT8 *message_array, unsigned length)
{
	if (!length) {
		return shaSuccess;
	}

	if (!context || !message_array) {
		return shaNull;
	}

	if (context->Computed) {
		context->Corrupted = shaStateError;

		return shaStateError;
	}

	if (context->Corrupted) {
		return context->Corrupted;
	}
	while (length-- && !context->Corrupted) {
		context->Message_Block[context->Message_Block_Index++] =
			(*message_array & 0xFF);

		context->Length_Low += 8;
		if (context->Length_Low == 0) {
			context->Length_High++;
			if (context->Length_High == 0) {
				/* Message is too long */
				context->Corrupted = 1;
			}
		}

		if (context->Message_Block_Index == 64) {
			Mrvl_SHA1ProcessMessageBlock(context);
		}

		message_array++;
	}

	return shaSuccess;
}

/*
 *  SHA1ProcessMessageBlock
 *
 *  Description:
 *      This function will process the next 512 bits of the message
 *      stored in the Message_Block array.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:



Eastlake & Jones             Informational                     [Page 14]

RFC 3174           US Secure Hash Algorithm 1 (SHA1)      September 2001


 *      Many of the variable names in this code, especially the
 *      single character names, were used because those were the
 *      names used in the publication.
 *
 *
 */

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

/* blk0() and blk() perform the initial expand. */
/* I got the idea of expanding during the round function from SSLeay */
#define blk(i) (W[i&15] = rol(W[(i+13)&15]^W[(i+8)&15]^W[(i+2)&15]^W[i&15],1))
#define blk0(i) ((i & 0x30)? blk(i) : W[i])

/*
NOte- Some of the variables are made static in this file because
this function runs in the idle task. The idle task dosent have
enough stack space to accomodate these variables. In the future
if this function is run in a different task with large stack space
or if the stack space of the idle task is increased then we can
remove the static defination from these variables.
*/
void
Mrvl_SHA1ProcessMessageBlock(Mrvl_SHA1_CTX *context)
{
	static const UINT32 K[] = {	/* Constants defined in SHA-1   */
		0x5A827999,
		0x6ED9EBA1,
		0x8F1BBCDC,
		0xCA62C1D6
	};
	int t;			/* Loop counter                */
	UINT32 temp;		/* Temporary word value        */
	UINT32 *W;
	UINT32 A, B, C, D, E;	/* Word buffers                */

	/* WLAN buffers are aligned, so the context starts at a UINT32 boundary */
	W = context->Scratch;

	for (t = 0; t < 16; t++) {
		W[t] = context->Message_Block[t * 4] << 24;
		W[t] |= context->Message_Block[t * 4 + 1] << 16;
		W[t] |= context->Message_Block[t * 4 + 2] << 8;
		W[t] |= context->Message_Block[t * 4 + 3];
	}

	A = context->Intermediate_Hash[0];
	B = context->Intermediate_Hash[1];
	C = context->Intermediate_Hash[2];
	D = context->Intermediate_Hash[3];
	E = context->Intermediate_Hash[4];

	for (t = 0; t < 20; t++) {
		temp = Mrvl_SHA1CircularShift(5, A) +
			((B & C) | ((~B) & D)) + E + blk0(t) + K[0];
		E = D;
		D = C;
		C = Mrvl_SHA1CircularShift(30, B);

		B = A;
		A = temp;
	}

	for (t = 20; t < 40; t++) {
		temp = Mrvl_SHA1CircularShift(5,
					      A) + (B ^ C ^ D) + E + blk(t) +
			K[1];
		E = D;
		D = C;
		C = Mrvl_SHA1CircularShift(30, B);
		B = A;
		A = temp;
	}

	for (t = 40; t < 60; t++) {
		temp = Mrvl_SHA1CircularShift(5, A) +
			((B & C) | (B & D) | (C & D)) + E + blk(t) + K[2];
		E = D;
		D = C;
		C = Mrvl_SHA1CircularShift(30, B);
		B = A;
		A = temp;
	}

	for (t = 60; t < 80; t++) {
		temp = Mrvl_SHA1CircularShift(5,
					      A) + (B ^ C ^ D) + E + blk(t) +
			K[3];
		E = D;
		D = C;
		C = Mrvl_SHA1CircularShift(30, B);
		B = A;
		A = temp;
	}

	context->Intermediate_Hash[0] += A;
	context->Intermediate_Hash[1] += B;
	context->Intermediate_Hash[2] += C;
	context->Intermediate_Hash[3] += D;
	context->Intermediate_Hash[4] += E;

	context->Message_Block_Index = 0;
}

/*
 *  SHA1PadMessage
 *



Eastlake & Jones             Informational                     [Page 16]

RFC 3174           US Secure Hash Algorithm 1 (SHA1)      September 2001


 *  Description:
 *      According to the standard, the message must be padded to an even
 *      512 bits.  The first padding bit must be a '1'.  The last 64
 *      bits represent the length of the original message.  All bits in
 *      between should be 0.  This function will pad the message
 *      according to those rules by filling the Message_Block array
 *      accordingly.  It will also call the ProcessMessageBlock function
 *      provided appropriately.  When it returns, it can be assumed that
 *      the message digest has been computed.
 *
 *  Parameters:
 *      context: [in/out]
 *          The context to pad
 *      ProcessMessageBlock: [in]
 *          The appropriate SHA*ProcessMessageBlock function
 *  Returns:
 *      Nothing.
 *
 */

void
Mrvl_SHA1PadMessage(Mrvl_SHA1_CTX *context)
{
	/*
	 *  Check to see if the current message block is too small to hold
	 *  the initial padding bits and length.  If so, we will pad the
	 *  block, process it, and then continue padding into a second
	 *  block.
	 */
	if (context->Message_Block_Index > 55) {
		context->Message_Block[context->Message_Block_Index++] = 0x80;
		while (context->Message_Block_Index < 64) {
			context->Message_Block[context->Message_Block_Index++] =
				0;
		}

		Mrvl_SHA1ProcessMessageBlock(context);

		while (context->Message_Block_Index < 56) {
			context->Message_Block[context->Message_Block_Index++] =
				0;
		}
	} else {
		context->Message_Block[context->Message_Block_Index++] = 0x80;
		while (context->Message_Block_Index < 56) {

			context->Message_Block[context->Message_Block_Index++] =
				0;
		}
	}

	/*
	 *  Store the message length as the last 8 octets
	 */
	context->Message_Block[56] = context->Length_High >> 24;
	context->Message_Block[57] = context->Length_High >> 16;
	context->Message_Block[58] = context->Length_High >> 8;
	context->Message_Block[59] = context->Length_High;
	context->Message_Block[60] = context->Length_Low >> 24;
	context->Message_Block[61] = context->Length_Low >> 16;
	context->Message_Block[62] = context->Length_Low >> 8;
	context->Message_Block[63] = context->Length_Low;

	Mrvl_SHA1ProcessMessageBlock(context);
}

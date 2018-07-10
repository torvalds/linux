/*
 * Copyright (C) 2017 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */


/* The part below is a section of matrixssl-3-7-2b-open/crypto/cryptolib.h
 * Changes are flagged with //bbox
 */

/******************************************************************************/
/* 32-bit Rotates */
/******************************************************************************/
#if defined(_MSC_VER)
/******************************************************************************/

/* instrinsic rotate */
#include <stdlib.h>
#pragma intrinsic(_lrotr,_lrotl)
#define ROR(x,n) _lrotr(x,n)
#define ROL(x,n) _lrotl(x,n)

/******************************************************************************/
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__)) && \
		!defined(INTEL_CC) && !defined(PS_NO_ASM)

static ALWAYS_INLINE unsigned ROL(unsigned word, int i)
{
 if (__builtin_constant_p(i)) { //box
   // Rotates by constant use fewer registers,
   // and on many Intel CPUs rotates by %cl take 2 cycles, not 1.
   asm ("roll %2,%0"
	  :"=r" (word)
	  :"0" (word),"i" (i));
   return word;
 } //box
 asm ("roll %%cl,%0"
	  :"=r" (word)
	  :"0" (word),"c" (i));
 return word;
}

static ALWAYS_INLINE unsigned ROR(unsigned word, int i)
{
 if (__builtin_constant_p(i)) { //box
   asm ("rorl %2,%0"
	  :"=r" (word)
	  :"0" (word),"i" (i));
   return word;
 } //box
 asm ("rorl %%cl,%0"
	  :"=r" (word)
	  :"0" (word),"c" (i));
 return word;
}

/******************************************************************************/
#else

/* rotates the hard way */
#define ROL(x, y) \
	( (((unsigned long)(x)<<(unsigned long)((y)&31)) | \
	(((unsigned long)(x)&0xFFFFFFFFUL)>>(unsigned long)(32-((y)&31)))) & \
	0xFFFFFFFFUL)
#define ROR(x, y) \
	( ((((unsigned long)(x)&0xFFFFFFFFUL)>>(unsigned long)((y)&31)) | \
	((unsigned long)(x)<<(unsigned long)(32-((y)&31)))) & 0xFFFFFFFFUL)

#endif /* 32-bit Rotates */
/******************************************************************************/

#ifdef HAVE_NATIVE_INT64
#ifdef _MSC_VER
	#define CONST64(n) n ## ui64
#else
	#define CONST64(n) n ## ULL
#endif
#endif

/******************************************************************************/
/*
	Endian helper macros
 */
#if defined (ENDIAN_NEUTRAL)
#define STORE32L(x, y) { \
(y)[3] = (unsigned char)(((x)>>24)&255); \
(y)[2] = (unsigned char)(((x)>>16)&255);  \
(y)[1] = (unsigned char)(((x)>>8)&255); \
(y)[0] = (unsigned char)((x)&255); \
}

#define LOAD32L(x, y) { \
x = ((unsigned long)((y)[3] & 255)<<24) | \
((unsigned long)((y)[2] & 255)<<16) | \
((unsigned long)((y)[1] & 255)<<8)  | \
((unsigned long)((y)[0] & 255)); \
}

#define STORE64L(x, y) { \
(y)[7] = (unsigned char)(((x)>>56)&255); \
(y)[6] = (unsigned char)(((x)>>48)&255); \
(y)[5] = (unsigned char)(((x)>>40)&255); \
(y)[4] = (unsigned char)(((x)>>32)&255); \
(y)[3] = (unsigned char)(((x)>>24)&255); \
(y)[2] = (unsigned char)(((x)>>16)&255); \
(y)[1] = (unsigned char)(((x)>>8)&255); \
(y)[0] = (unsigned char)((x)&255); \
}

#define LOAD64L(x, y) { \
x = (((uint64)((y)[7] & 255))<<56)|(((uint64)((y)[6] & 255))<<48)| \
(((uint64)((y)[5] & 255))<<40)|(((uint64)((y)[4] & 255))<<32)| \
(((uint64)((y)[3] & 255))<<24)|(((uint64)((y)[2] & 255))<<16)| \
(((uint64)((y)[1] & 255))<<8)|(((uint64)((y)[0] & 255))); \
}

#define STORE32H(x, y) { \
(y)[0] = (unsigned char)(((x)>>24)&255); \
(y)[1] = (unsigned char)(((x)>>16)&255); \
(y)[2] = (unsigned char)(((x)>>8)&255); \
(y)[3] = (unsigned char)((x)&255); \
}

#define LOAD32H(x, y) { \
x = ((unsigned long)((y)[0] & 255)<<24) | \
((unsigned long)((y)[1] & 255)<<16) | \
((unsigned long)((y)[2] & 255)<<8)  | \
((unsigned long)((y)[3] & 255)); \
}

#define STORE64H(x, y) { \
(y)[0] = (unsigned char)(((x)>>56)&255); \
(y)[1] = (unsigned char)(((x)>>48)&255); \
(y)[2] = (unsigned char)(((x)>>40)&255); \
(y)[3] = (unsigned char)(((x)>>32)&255); \
(y)[4] = (unsigned char)(((x)>>24)&255); \
(y)[5] = (unsigned char)(((x)>>16)&255); \
(y)[6] = (unsigned char)(((x)>>8)&255); \
(y)[7] = (unsigned char)((x)&255); \
}

#define LOAD64H(x, y) { \
x = (((uint64)((y)[0] & 255))<<56)|(((uint64)((y)[1] & 255))<<48) | \
(((uint64)((y)[2] & 255))<<40)|(((uint64)((y)[3] & 255))<<32) | \
(((uint64)((y)[4] & 255))<<24)|(((uint64)((y)[5] & 255))<<16) | \
(((uint64)((y)[6] & 255))<<8)|(((uint64)((y)[7] & 255))); \
}

#endif /* ENDIAN_NEUTRAL */

#ifdef ENDIAN_LITTLE
#define STORE32H(x, y) { \
(y)[0] = (unsigned char)(((x)>>24)&255); \
(y)[1] = (unsigned char)(((x)>>16)&255); \
(y)[2] = (unsigned char)(((x)>>8)&255); \
(y)[3] = (unsigned char)((x)&255); \
}

#define LOAD32H(x, y) { \
x = ((unsigned long)((y)[0] & 255)<<24) | \
((unsigned long)((y)[1] & 255)<<16) | \
((unsigned long)((y)[2] & 255)<<8)  | \
((unsigned long)((y)[3] & 255)); \
}

#define STORE64H(x, y) { \
(y)[0] = (unsigned char)(((x)>>56)&255); \
(y)[1] = (unsigned char)(((x)>>48)&255); \
(y)[2] = (unsigned char)(((x)>>40)&255); \
(y)[3] = (unsigned char)(((x)>>32)&255); \
(y)[4] = (unsigned char)(((x)>>24)&255); \
(y)[5] = (unsigned char)(((x)>>16)&255); \
(y)[6] = (unsigned char)(((x)>>8)&255); \
(y)[7] = (unsigned char)((x)&255); \
}

#define LOAD64H(x, y) { \
x = (((uint64)((y)[0] & 255))<<56)|(((uint64)((y)[1] & 255))<<48) | \
(((uint64)((y)[2] & 255))<<40)|(((uint64)((y)[3] & 255))<<32) | \
(((uint64)((y)[4] & 255))<<24)|(((uint64)((y)[5] & 255))<<16) | \
(((uint64)((y)[6] & 255))<<8)|(((uint64)((y)[7] & 255))); }

#ifdef ENDIAN_32BITWORD
#define STORE32L(x, y) { \
unsigned long __t = (x); memcpy(y, &__t, 4); \
}

#define LOAD32L(x, y)  memcpy(&(x), y, 4);

#define STORE64L(x, y) { \
(y)[7] = (unsigned char)(((x)>>56)&255); \
(y)[6] = (unsigned char)(((x)>>48)&255); \
(y)[5] = (unsigned char)(((x)>>40)&255); \
(y)[4] = (unsigned char)(((x)>>32)&255); \
(y)[3] = (unsigned char)(((x)>>24)&255); \
(y)[2] = (unsigned char)(((x)>>16)&255); \
(y)[1] = (unsigned char)(((x)>>8)&255); \
(y)[0] = (unsigned char)((x)&255); \
}

#define LOAD64L(x, y) { \
x = (((uint64)((y)[7] & 255))<<56)|(((uint64)((y)[6] & 255))<<48)| \
(((uint64)((y)[5] & 255))<<40)|(((uint64)((y)[4] & 255))<<32)| \
(((uint64)((y)[3] & 255))<<24)|(((uint64)((y)[2] & 255))<<16)| \
(((uint64)((y)[1] & 255))<<8)|(((uint64)((y)[0] & 255))); \
}

#else /* 64-bit words then  */
#define STORE32L(x, y) \
{ unsigned long __t = (x); memcpy(y, &__t, 4); }

#define LOAD32L(x, y) \
{ memcpy(&(x), y, 4); x &= 0xFFFFFFFF; }

#define STORE64L(x, y) \
{ uint64 __t = (x); memcpy(y, &__t, 8); }

#define LOAD64L(x, y) \
{ memcpy(&(x), y, 8); }

#endif /* ENDIAN_64BITWORD */
#endif /* ENDIAN_LITTLE */

#ifdef ENDIAN_BIG
#define STORE32L(x, y) { \
(y)[3] = (unsigned char)(((x)>>24)&255); \
(y)[2] = (unsigned char)(((x)>>16)&255); \
(y)[1] = (unsigned char)(((x)>>8)&255); \
(y)[0] = (unsigned char)((x)&255); \
}

#define LOAD32L(x, y) { \
x = ((unsigned long)((y)[3] & 255)<<24) | \
((unsigned long)((y)[2] & 255)<<16) | \
((unsigned long)((y)[1] & 255)<<8)  | \
((unsigned long)((y)[0] & 255)); \
}

#define STORE64L(x, y) { \
(y)[7] = (unsigned char)(((x)>>56)&255); \
(y)[6] = (unsigned char)(((x)>>48)&255); \
(y)[5] = (unsigned char)(((x)>>40)&255); \
(y)[4] = (unsigned char)(((x)>>32)&255); \
(y)[3] = (unsigned char)(((x)>>24)&255); \
(y)[2] = (unsigned char)(((x)>>16)&255); \
(y)[1] = (unsigned char)(((x)>>8)&255); \
(y)[0] = (unsigned char)((x)&255); \
}

#define LOAD64L(x, y) { \
x = (((uint64)((y)[7] & 255))<<56)|(((uint64)((y)[6] & 255))<<48) | \
(((uint64)((y)[5] & 255))<<40)|(((uint64)((y)[4] & 255))<<32) | \
(((uint64)((y)[3] & 255))<<24)|(((uint64)((y)[2] & 255))<<16) | \
(((uint64)((y)[1] & 255))<<8)|(((uint64)((y)[0] & 255))); \
}

#ifdef ENDIAN_32BITWORD
#define STORE32H(x, y) \
{ unsigned int __t = (x); memcpy(y, &__t, 4); }

#define LOAD32H(x, y) memcpy(&(x), y, 4);

#define STORE64H(x, y) { \
(y)[0] = (unsigned char)(((x)>>56)&255); \
(y)[1] = (unsigned char)(((x)>>48)&255); \
(y)[2] = (unsigned char)(((x)>>40)&255); \
(y)[3] = (unsigned char)(((x)>>32)&255); \
(y)[4] = (unsigned char)(((x)>>24)&255); \
(y)[5] = (unsigned char)(((x)>>16)&255); \
(y)[6] = (unsigned char)(((x)>>8)&255); \
(y)[7] = (unsigned char)((x)&255); \
}

#define LOAD64H(x, y) { \
x = (((uint64)((y)[0] & 255))<<56)|(((uint64)((y)[1] & 255))<<48)| \
(((uint64)((y)[2] & 255))<<40)|(((uint64)((y)[3] & 255))<<32)| \
(((uint64)((y)[4] & 255))<<24)|(((uint64)((y)[5] & 255))<<16)| \
(((uint64)((y)[6] & 255))<<8)| (((uint64)((y)[7] & 255))); \
}

#else /* 64-bit words then  */

#define STORE32H(x, y) \
{ unsigned long __t = (x); memcpy(y, &__t, 4); }

#define LOAD32H(x, y) \
{ memcpy(&(x), y, 4); x &= 0xFFFFFFFF; }

#define STORE64H(x, y) \
{ uint64 __t = (x); memcpy(y, &__t, 8); }

#define LOAD64H(x, y) \
{ memcpy(&(x), y, 8); }

#endif /* ENDIAN_64BITWORD */
#endif /* ENDIAN_BIG */

#ifdef HAVE_NATIVE_INT64
#define ROL64c(x, y) \
( (((x)<<((uint64)(y)&63)) | \
(((x)&CONST64(0xFFFFFFFFFFFFFFFF))>>((uint64)64-((y)&63)))) & CONST64(0xFFFFFFFFFFFFFFFF))

#define ROR64c(x, y) \
( ((((x)&CONST64(0xFFFFFFFFFFFFFFFF))>>((uint64)(y)&CONST64(63))) | \
((x)<<((uint64)(64-((y)&CONST64(63)))))) & CONST64(0xFFFFFFFFFFFFFFFF))
#endif /* HAVE_NATIVE_INT64 */
/******************************************************************************/



/* The part below is taken almost verbatim from matrixssl-3-7-2b-open/crypto/symmetric/.
 * Changes are flagged with //bbox
 */

/**
 *	@file    symmetric.h
 *	@version 33ef80f (HEAD, tag: MATRIXSSL-3-7-2-OPEN, tag: MATRIXSSL-3-7-2-COMM, origin/master, origin/HEAD, master)
 *
 *	Header for internal symmetric key cryptography support.
 */
/*
 *	Copyright (c) 2013-2015 INSIDE Secure Corporation
 *	Copyright (c) PeerSec Networks, 2002-2011
 *	All Rights Reserved
 *
 *	The latest version of this code is available at http://www.matrixssl.org
 *
 *	This software is open source; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This General Public License does NOT permit incorporating this software
 *	into proprietary programs.  If you are unable to comply with the GPL, a
 *	commercial license for this software may be purchased from INSIDE at
 *	http://www.insidesecure.com/eng/Company/Locations
 *
 *	This program is distributed in WITHOUT ANY WARRANTY; without even the
 *	implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *	See the GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *	http://www.gnu.org/copyleft/gpl.html
 */
/******************************************************************************/

#ifndef _h_PS_SYMMETRIC
#define _h_PS_SYMMETRIC

/******************************************************************************/
#ifdef USE_AES
/******************************************************************************/


#ifndef USE_AES_CBC_EXTERNAL
typedef struct {
	uint32 eK[64], dK[64];
	int32 Nr;
} psAesKey_t;

typedef struct {
	int32			blocklen;
	unsigned char	IV[16];
	psAesKey_t		key;
#if defined(USE_AES_GCM) || defined(USE_AES_CCM)
	unsigned char	EncCtr[16];
	unsigned char	CtrBlock[16];
#endif
#ifdef USE_AES_GCM
	unsigned char	gInit[16];
	uint32			TagTemp[4];
	unsigned char	Hash_SubKey[16];
	uint32			ProcessedBitCount[4];
	uint32			InputBufferCount;
	uint32			OutputBufferCount;
	union
	{
		unsigned char Buffer[128];
		uint32 BufferAlignment;
	} Input;
#endif /* USE_AES_GCM */
#ifdef USE_AES_CCM
	uint32_t ccmTagTemp[16 / sizeof(uint32_t)]; /* 32 */
	union
	{
		/* Used for formatting IV. */
		uint8_t Temporary[16];
		/* Used for processing Mac. */
		uint8_t Y0[16];
	} u; /* 48 */
#endif /* USE_AES_CCM */
} psAesCipher_t;
#endif /* USE_AES_CBC_EXTERNAL */

#endif /* USE_AES */

#ifdef USE_IDEA
#define SSL_IDEA_KEY_LEN	16
#define SSL_IDEA_IV_LEN		8
#define SSL_IDEA_BLOCK_LEN	8

typedef struct {
	uint16	key_schedule[52];
} psIdeaKey_t;

typedef struct {
	psIdeaKey_t		key;
	uint32			IV[2];
	short			for_encryption;
	short			inverted;
} idea_CBC;
#endif
/******************************************************************************/

/******************************************************************************/
#ifdef USE_SEED
/******************************************************************************/
#define SSL_SEED_KEY_LEN	16
#define SSL_SEED_IV_LEN		16


typedef struct {
	uint32 K[32], dK[32];
} psSeedKey_t;

typedef struct {
	int32			blocklen;
	unsigned char	IV[16];
	psSeedKey_t		key;
} seed_CBC;

#endif /* USE_SEED */
/******************************************************************************/

/******************************************************************************/
#if defined(USE_3DES) || defined(USE_DES)
/******************************************************************************/
#define DES3_KEY_LEN	24
#define DES3_IV_LEN		8
#define DES_KEY_LEN		8

typedef struct {
	uint32 ek[3][32], dk[3][32];
} psDes3Key_t;

/*
	A block cipher CBC structure
 */
typedef struct {
	int32				blocklen;
	unsigned char		IV[8];
	psDes3Key_t			key;
} des3_CBC;

#endif /* USE_3DES || USE_DES */
/******************************************************************************/

/******************************************************************************/
#ifdef USE_ARC4
typedef struct {
	unsigned char	state[256];
	uint32	byteCount;
	unsigned char	x;
	unsigned char	y;
} psRc4Key_t;
#endif /* USE_ARC4 */
/******************************************************************************/
#ifdef USE_RC2
typedef struct {
	unsigned xkey[64];
} psRc2Key_t;

typedef struct {
	int32				blocklen;
	unsigned char		IV[8];
	psRc2Key_t			key;
} rc2_CBC;
#endif /* USE_RC2 */
/******************************************************************************/
/*	Universal types and defines */
/******************************************************************************/
#define MAXBLOCKSIZE	24

typedef union {
#ifdef USE_RC2
	rc2_CBC		rc2;
#endif
#ifdef USE_ARC4
	psRc4Key_t	arc4;
#endif
#ifdef USE_3DES
	des3_CBC	des3;
#endif
#ifdef USE_AES
	psAesCipher_t	aes;
#endif
#ifdef USE_SEED
	seed_CBC	seed;
#endif
#ifdef USE_IDEA
	idea_CBC	idea;
#endif
} psCipherContext_t;

#define byte(x, n) (((x) >> (8 * (n))) & 255)

#endif /* _h_PS_SYMMETRIC */
/******************************************************************************/

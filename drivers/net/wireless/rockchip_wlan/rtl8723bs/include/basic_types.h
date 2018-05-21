/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#ifndef __BASIC_TYPES_H__
#define __BASIC_TYPES_H__


#define SUCCESS	0
#define FAIL	(-1)

#ifndef TRUE
	#define _TRUE	1
#else
	#define _TRUE	TRUE
#endif

#ifndef FALSE
	#define _FALSE	0
#else
	#define _FALSE	FALSE
#endif

#ifdef PLATFORM_WINDOWS

	typedef signed char s8;
	typedef unsigned char u8;

	typedef signed short s16;
	typedef unsigned short u16;

	typedef signed long s32;
	typedef unsigned long u32;

	typedef unsigned int	uint;
	typedef	signed int		sint;


	typedef signed long long s64;
	typedef unsigned long long u64;

	#ifdef NDIS50_MINIPORT

		#define NDIS_MAJOR_VERSION       5
		#define NDIS_MINOR_VERSION       0

	#endif

	#ifdef NDIS51_MINIPORT

		#define NDIS_MAJOR_VERSION       5
		#define NDIS_MINOR_VERSION       1

	#endif

	typedef NDIS_PROC proc_t;

	typedef LONG atomic_t;

#endif


#ifdef PLATFORM_LINUX
	#include <linux/version.h>
	#include <linux/types.h>
	#include <linux/module.h>
	#include <linux/kernel.h>
	#include <linux/init.h>
	#include <linux/utsname.h>
	#define IN
	#define OUT
	#define VOID void
	#define NDIS_OID uint
	#define NDIS_STATUS uint

	typedef	signed int sint;

	#ifndef	PVOID
		typedef void *PVOID;
		/* #define PVOID	(void *) */
	#endif

	#define UCHAR u8
	#define USHORT u16
	#define UINT u32
	#define ULONG u32

	#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19))
		typedef _Bool bool;
	#endif

	typedef void (*proc_t)(void *);

	typedef	__kernel_size_t	SIZE_T;
	typedef	__kernel_ssize_t	SSIZE_T;
	#define FIELD_OFFSET(s, field)	((SSIZE_T)&((s *)(0))->field)

#define u1Byte		u8
#define pu1Byte		u8*

#define u2Byte		u16
#define pu2Byte		u16*

#define u4Byte		u32
#define pu4Byte		u32*

#define u8Byte		u64
#define pu8Byte		u64*

#define s1Byte		s8
#define ps1Byte		s8*

#define s2Byte		s16
#define ps2Byte		s16*

#define s4Byte		s32
#define ps4Byte		s32*

#define s8Byte		s64
#define ps8Byte		s64*

#define UCHAR u8
#define USHORT u16
#define UINT u32
#define ULONG u32
#define PULONG u32*

#endif


#ifdef PLATFORM_FREEBSD

	typedef signed char s8;
	typedef unsigned char u8;

	typedef signed short s16;
	typedef unsigned short u16;

	typedef signed int s32;
	typedef unsigned int u32;

	typedef unsigned int	uint;
	typedef	signed int		sint;
	typedef long atomic_t;

	typedef signed long long s64;
	typedef unsigned long long u64;
	#define IN
	#define OUT
	#define VOID void
	#define NDIS_OID uint
	#define NDIS_STATUS uint

	#ifndef	PVOID
		typedef void *PVOID;
		/* #define PVOID	(void *) */
	#endif
	typedef u32 dma_addr_t;
	#define UCHAR u8
	#define USHORT u16
	#define UINT u32
	#define ULONG u32

	typedef void (*proc_t)(void *);

	typedef unsigned int __kernel_size_t;
	typedef int __kernel_ssize_t;

	typedef	__kernel_size_t	SIZE_T;
	typedef	__kernel_ssize_t	SSIZE_T;
	#define FIELD_OFFSET(s, field)	((SSIZE_T)&((s *)(0))->field)

#endif

#define MEM_ALIGNMENT_OFFSET	(sizeof (SIZE_T))
#define MEM_ALIGNMENT_PADDING	(sizeof(SIZE_T) - 1)

#define SIZE_PTR SIZE_T
#define SSIZE_PTR SSIZE_T

/*
* Continuous bits starting from least significant bit
* Example:
* BIT_LEN_MASK_32(0) => 0x00000000
* BIT_LEN_MASK_32(1) => 0x00000001
* BIT_LEN_MASK_32(2) => 0x00000003
* BIT_LEN_MASK_32(32) => 0xFFFFFFFF
*/
#define BIT_LEN_MASK_32(__BitLen) ((u32)(0xFFFFFFFF >> (32 - (__BitLen))))
#define BIT_LEN_MASK_16(__BitLen) ((u16)(0xFFFF >> (16 - (__BitLen))))
#define BIT_LEN_MASK_8(__BitLen) ((u8)(0xFF >> (8 - (__BitLen))))

/*
* Continuous bits starting from least significant bit
* Example:
* BIT_OFFSET_LEN_MASK_32(0, 2) => 0x00000003
* BIT_OFFSET_LEN_MASK_32(16, 2) => 0x00030000
*/
#define BIT_OFFSET_LEN_MASK_32(__BitOffset, __BitLen) ((u32)(BIT_LEN_MASK_32(__BitLen) << (__BitOffset)))
#define BIT_OFFSET_LEN_MASK_16(__BitOffset, __BitLen) ((u16)(BIT_LEN_MASK_16(__BitLen) << (__BitOffset)))
#define BIT_OFFSET_LEN_MASK_8(__BitOffset, __BitLen) ((u8)(BIT_LEN_MASK_8(__BitLen) << (__BitOffset)))

/*
* Convert LE data to host byte order
*/
#define EF1Byte (u8)
#define EF2Byte le16_to_cpu
#define EF4Byte le32_to_cpu

/*
* Read LE data from memory to host byte order
*/
#define ReadLE4Byte(_ptr)	le32_to_cpu(*((u32 *)(_ptr)))
#define ReadLE2Byte(_ptr)	le16_to_cpu(*((u16 *)(_ptr)))
#define ReadLE1Byte(_ptr)	(*((u8 *)(_ptr)))

/*
* Read BE data from memory to host byte order
*/
#define ReadBEE4Byte(_ptr)	be32_to_cpu(*((u32 *)(_ptr)))
#define ReadBE2Byte(_ptr)	be16_to_cpu(*((u16 *)(_ptr)))
#define ReadBE1Byte(_ptr)	(*((u8 *)(_ptr)))

/*
* Write host byte order data to memory in LE order
*/
#define WriteLE4Byte(_ptr, _val)	((*((u32 *)(_ptr))) = cpu_to_le32(_val))
#define WriteLE2Byte(_ptr, _val)	((*((u16 *)(_ptr))) = cpu_to_le16(_val))
#define WriteLE1Byte(_ptr, _val)	((*((u8 *)(_ptr))) = ((u8)(_val)))

/*
* Write host byte order data to memory in BE order
*/
#define WriteBE4Byte(_ptr, _val)	((*((u32 *)(_ptr))) = cpu_to_be32(_val))
#define WriteBE2Byte(_ptr, _val)	((*((u16 *)(_ptr))) = cpu_to_be16(_val))
#define WriteBE1Byte(_ptr, _val)	((*((u8 *)(_ptr))) = ((u8)(_val)))

/*
* Return 4-byte value in host byte ordering from 4-byte pointer in litten-endian system.
*/
#define LE_P4BYTE_TO_HOST_4BYTE(__pStart) (le32_to_cpu(*((u32 *)(__pStart))))
#define LE_P2BYTE_TO_HOST_2BYTE(__pStart) (le16_to_cpu(*((u16 *)(__pStart))))
#define LE_P1BYTE_TO_HOST_1BYTE(__pStart) ((*((u8 *)(__pStart))))

/*
* Return 4-byte value in host byte ordering from 4-byte pointer in big-endian system.
*/
#define BE_P4BYTE_TO_HOST_4BYTE(__pStart) (be32_to_cpu(*((u32 *)(__pStart))))
#define BE_P2BYTE_TO_HOST_2BYTE(__pStart) (be16_to_cpu(*((u16 *)(__pStart))))
#define BE_P1BYTE_TO_HOST_1BYTE(__pStart) ((*((u8 *)(__pStart))))

/*
* Translate subfield (continuous bits in little-endian) of 4-byte value in LE byte to
* 4-byte value in host byte ordering.
*/
#define LE_BITS_TO_4BYTE(__pStart, __BitOffset, __BitLen) \
	((LE_P4BYTE_TO_HOST_4BYTE(__pStart) >> (__BitOffset)) & BIT_LEN_MASK_32(__BitLen))

#define LE_BITS_TO_2BYTE(__pStart, __BitOffset, __BitLen) \
	((LE_P2BYTE_TO_HOST_2BYTE(__pStart) >> (__BitOffset)) & BIT_LEN_MASK_16(__BitLen))

#define LE_BITS_TO_1BYTE(__pStart, __BitOffset, __BitLen) \
	((LE_P1BYTE_TO_HOST_1BYTE(__pStart) >> (__BitOffset)) & BIT_LEN_MASK_8(__BitLen))

/*
* Translate subfield (continuous bits in big-endian) of 4-byte value in BE byte to
* 4-byte value in host byte ordering.
*/
#define BE_BITS_TO_4BYTE(__pStart, __BitOffset, __BitLen) \
	((BE_P4BYTE_TO_HOST_4BYTE(__pStart) >> (__BitOffset)) & BIT_LEN_MASK_32(__BitLen))

#define BE_BITS_TO_2BYTE(__pStart, __BitOffset, __BitLen) \
	((BE_P2BYTE_TO_HOST_2BYTE(__pStart) >> (__BitOffset)) & BIT_LEN_MASK_16(__BitLen))

#define BE_BITS_TO_1BYTE(__pStart, __BitOffset, __BitLen) \
	((BE_P1BYTE_TO_HOST_1BYTE(__pStart) >> (__BitOffset)) & BIT_LEN_MASK_8(__BitLen))

/*
* Mask subfield (continuous bits in little-endian) of 4-byte value in LE byte oredering
* and return the result in 4-byte value in host byte ordering.
*/
#define LE_BITS_CLEARED_TO_4BYTE(__pStart, __BitOffset, __BitLen) \
	(LE_P4BYTE_TO_HOST_4BYTE(__pStart) & (~BIT_OFFSET_LEN_MASK_32(__BitOffset, __BitLen)))

#define LE_BITS_CLEARED_TO_2BYTE(__pStart, __BitOffset, __BitLen) \
	(LE_P2BYTE_TO_HOST_2BYTE(__pStart) & (~BIT_OFFSET_LEN_MASK_16(__BitOffset, __BitLen)))

#define LE_BITS_CLEARED_TO_1BYTE(__pStart, __BitOffset, __BitLen) \
	(LE_P1BYTE_TO_HOST_1BYTE(__pStart) & ((u8)(~BIT_OFFSET_LEN_MASK_8(__BitOffset, __BitLen))))

/*
* Mask subfield (continuous bits in big-endian) of 4-byte value in BE byte oredering
* and return the result in 4-byte value in host byte ordering.
*/
#define BE_BITS_CLEARED_TO_4BYTE(__pStart, __BitOffset, __BitLen) \
	(BE_P4BYTE_TO_HOST_4BYTE(__pStart) & (~BIT_OFFSET_LEN_MASK_32(__BitOffset, __BitLen)))

#define BE_BITS_CLEARED_TO_2BYTE(__pStart, __BitOffset, __BitLen) \
	(BE_P2BYTE_TO_HOST_2BYTE(__pStart) & (~BIT_OFFSET_LEN_MASK_16(__BitOffset, __BitLen)))

#define BE_BITS_CLEARED_TO_1BYTE(__pStart, __BitOffset, __BitLen) \
	(BE_P1BYTE_TO_HOST_1BYTE(__pStart) & (~BIT_OFFSET_LEN_MASK_8(__BitOffset, __BitLen)))

/*
* Set subfield of little-endian 4-byte value to specified value.
*/
#define SET_BITS_TO_LE_4BYTE(__pStart, __BitOffset, __BitLen, __Value) \
	do { \
		if (__BitOffset == 0 && __BitLen == 32) \
			WriteLE4Byte(__pStart, __Value); \
		else { \
			WriteLE4Byte(__pStart, \
				LE_BITS_CLEARED_TO_4BYTE(__pStart, __BitOffset, __BitLen) \
				| \
				((((u32)__Value) & BIT_LEN_MASK_32(__BitLen)) << (__BitOffset)) \
			); \
		} \
	} while (0)

#define SET_BITS_TO_LE_2BYTE(__pStart, __BitOffset, __BitLen, __Value) \
	do { \
		if (__BitOffset == 0 && __BitLen == 16) \
			WriteLE2Byte(__pStart, __Value); \
		else { \
			WriteLE2Byte(__pStart, \
				LE_BITS_CLEARED_TO_2BYTE(__pStart, __BitOffset, __BitLen) \
				| \
				((((u16)__Value) & BIT_LEN_MASK_16(__BitLen)) << (__BitOffset)) \
			); \
		} \
	} while (0)

#define SET_BITS_TO_LE_1BYTE(__pStart, __BitOffset, __BitLen, __Value) \
	do { \
		if (__BitOffset == 0 && __BitLen == 8) \
			WriteLE1Byte(__pStart, __Value); \
		else { \
			WriteLE1Byte(__pStart, \
				LE_BITS_CLEARED_TO_1BYTE(__pStart, __BitOffset, __BitLen) \
				| \
				((((u8)__Value) & BIT_LEN_MASK_8(__BitLen)) << (__BitOffset)) \
			); \
		} \
	} while (0)

/*
* Set subfield of big-endian 4-byte value to specified value.
*/
#define SET_BITS_TO_BE_4BYTE(__pStart, __BitOffset, __BitLen, __Value) \
	do { \
		if (__BitOffset == 0 && __BitLen == 32) \
			WriteBE4Byte(__pStart, __Value); \
		else { \
			WriteBE4Byte(__pStart, \
				BE_BITS_CLEARED_TO_4BYTE(__pStart, __BitOffset, __BitLen) \
				| \
				((((u32)__Value) & BIT_LEN_MASK_32(__BitLen)) << (__BitOffset)) \
			); \
		} \
	} while (0)

#define SET_BITS_TO_BE_2BYTE(__pStart, __BitOffset, __BitLen, __Value) \
	do { \
		if (__BitOffset == 0 && __BitLen == 16) \
			WriteBE2Byte(__pStart, __Value); \
		else { \
			WriteBE2Byte(__pStart, \
				BE_BITS_CLEARED_TO_2BYTE(__pStart, __BitOffset, __BitLen) \
				| \
				((((u16)__Value) & BIT_LEN_MASK_16(__BitLen)) << (__BitOffset)) \
			); \
		} \
	} while (0)

#define SET_BITS_TO_BE_1BYTE(__pStart, __BitOffset, __BitLen, __Value) \
	do { \
		if (__BitOffset == 0 && __BitLen == 8) \
			WriteBE1Byte(__pStart, __Value); \
		else { \
			WriteBE1Byte(__pStart, \
				BE_BITS_CLEARED_TO_1BYTE(__pStart, __BitOffset, __BitLen) \
				| \
				((((u8)__Value) & BIT_LEN_MASK_8(__BitLen)) << (__BitOffset)) \
			); \
		} \
	} while (0)

/* Get the N-bytes aligment offset from the current length */
#define N_BYTE_ALIGMENT(__Value, __Aligment) ((__Aligment == 1) ? (__Value) : (((__Value + __Aligment - 1) / __Aligment) * __Aligment))

typedef unsigned char	BOOLEAN, *PBOOLEAN, boolean;

#define TEST_FLAG(__Flag, __testFlag)		(((__Flag) & (__testFlag)) != 0)
#define SET_FLAG(__Flag, __setFlag)			((__Flag) |= __setFlag)
#define CLEAR_FLAG(__Flag, __clearFlag)		((__Flag) &= ~(__clearFlag))
#define CLEAR_FLAGS(__Flag)					((__Flag) = 0)
#define TEST_FLAGS(__Flag, __testFlags)		(((__Flag) & (__testFlags)) == (__testFlags))

#endif /* __BASIC_TYPES_H__ */

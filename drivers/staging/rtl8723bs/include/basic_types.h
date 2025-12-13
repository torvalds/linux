/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __BASIC_TYPES_H__
#define __BASIC_TYPES_H__


#define SUCCESS	0
#define FAIL	(-1)

#include <linux/types.h>

#define FIELD_OFFSET(s, field)	((__kernel_ssize_t)&((s *)(0))->field)

#define SIZE_PTR __kernel_size_t
#define SSIZE_PTR __kernel_ssize_t

/* port from fw by thomas */
/*  TODO: Belows are Sync from SD7-Driver. It is necessary to check correctness */

/*
 * Call endian free function when
 *	1. Read/write packet content.
 *	2. Before write integer to IO.
 *	3. After read integer from IO.
 */

/*  */
/*  Byte Swapping routine. */
/*  */
#define EF1Byte	(u8)
#define EF2Byte		le16_to_cpu
#define EF4Byte	le32_to_cpu

/* Convert little data endian to host ordering */
#define EF1BYTE(_val)		\
	((u8)(_val))
#define EF2BYTE(_val)		\
	(le16_to_cpu(_val))
#define EF4BYTE(_val)		\
	(le32_to_cpu(_val))

/* Read data from memory */
#define READEF1BYTE(_ptr)	\
	EF1BYTE(*((u8 *)(_ptr)))
/* Read le16 data from memory and convert to host ordering */
#define READEF2BYTE(_ptr)	\
	EF2BYTE(*(_ptr))
#define READEF4BYTE(_ptr)	\
	EF4BYTE(*(_ptr))

/* Write data to memory */
#define WRITEEF1BYTE(_ptr, _val)			\
	do {						\
		(*((u8 *)(_ptr))) = EF1BYTE(_val);	\
	} while (0)
/* Write le data to memory in host ordering */
#define WRITEEF2BYTE(_ptr, _val)			\
	do {						\
		(*((u16 *)(_ptr))) = EF2BYTE(_val);	\
	} while (0)

#define WRITEEF4BYTE(_ptr, _val)			\
	do {						\
		(*((u32 *)(_ptr))) = EF2BYTE(_val);	\
	} while (0)

/*
 * Create a bit mask
 * Examples:
 * BIT_LEN_MASK_32(0) => 0x00000000
 * BIT_LEN_MASK_32(1) => 0x00000001
 * BIT_LEN_MASK_32(2) => 0x00000003
 * BIT_LEN_MASK_32(32) => 0xFFFFFFFF
 */
#define BIT_LEN_MASK_32(__bitlen)	 \
	(0xFFFFFFFF >> (32 - (__bitlen)))
#define BIT_LEN_MASK_16(__bitlen)	 \
	(0xFFFF >> (16 - (__bitlen)))
#define BIT_LEN_MASK_8(__bitlen) \
	(0xFF >> (8 - (__bitlen)))

/*
 * Create an offset bit mask
 * Examples:
 * BIT_OFFSET_LEN_MASK_32(0, 2) => 0x00000003
 * BIT_OFFSET_LEN_MASK_32(16, 2) => 0x00030000
 */
#define BIT_OFFSET_LEN_MASK_32(__bitoffset, __bitlen) \
	(BIT_LEN_MASK_32(__bitlen) << (__bitoffset))
#define BIT_OFFSET_LEN_MASK_16(__bitoffset, __bitlen) \
	(BIT_LEN_MASK_16(__bitlen) << (__bitoffset))
#define BIT_OFFSET_LEN_MASK_8(__bitoffset, __bitlen) \
	(BIT_LEN_MASK_8(__bitlen) << (__bitoffset))

/*
 * Description:
 * Return 4-byte value in host byte ordering from
 * 4-byte pointer in little-endian system.
 */
#define LE_P4BYTE_TO_HOST_4BYTE(__pstart) \
	(EF4BYTE(*((__le32 *)(__pstart))))
#define LE_P2BYTE_TO_HOST_2BYTE(__pstart) \
	(EF2BYTE(*((__le16 *)(__pstart))))
#define LE_P1BYTE_TO_HOST_1BYTE(__pstart) \
	(EF1BYTE(*((u8 *)(__pstart))))

/*
 * Description:
 * Translate subfield (continuous bits in little-endian) of 4-byte value in
 * little byte to 4-byte value in host byte ordering.
 */
#define LE_BITS_TO_4BYTE(__pstart, __bitoffset, __bitlen) \
	(\
		(LE_P4BYTE_TO_HOST_4BYTE(__pstart) >> (__bitoffset))  & \
		BIT_LEN_MASK_32(__bitlen) \
	)
#define LE_BITS_TO_2BYTE(__pstart, __bitoffset, __bitlen) \
	(\
		(LE_P2BYTE_TO_HOST_2BYTE(__pstart) >> (__bitoffset)) & \
		BIT_LEN_MASK_16(__bitlen) \
	)
#define LE_BITS_TO_1BYTE(__pstart, __bitoffset, __bitlen) \
	(\
		(LE_P1BYTE_TO_HOST_1BYTE(__pstart) >> (__bitoffset)) & \
		BIT_LEN_MASK_8(__bitlen) \
	)

/*
 * Description:
 * Mask subfield (continuous bits in little-endian) of 4-byte value in little
 * byte ordering and return the result in 4-byte value in host byte ordering.
 */
#define LE_BITS_CLEARED_TO_4BYTE(__pstart, __bitoffset, __bitlen) \
	(\
		LE_P4BYTE_TO_HOST_4BYTE(__pstart)  & \
		(~BIT_OFFSET_LEN_MASK_32(__bitoffset, __bitlen)) \
	)
#define LE_BITS_CLEARED_TO_2BYTE(__pstart, __bitoffset, __bitlen) \
	(\
		LE_P2BYTE_TO_HOST_2BYTE(__pstart) & \
		(~BIT_OFFSET_LEN_MASK_16(__bitoffset, __bitlen)) \
	)
#define LE_BITS_CLEARED_TO_1BYTE(__pstart, __bitoffset, __bitlen) \
	(\
		LE_P1BYTE_TO_HOST_1BYTE(__pstart) & \
		(~BIT_OFFSET_LEN_MASK_8(__bitoffset, __bitlen)) \
	)

/*
 * Description:
 * Set subfield of little-endian 4-byte value to specified value.
 */
#define SET_BITS_TO_LE_4BYTE(__pstart, __bitoffset, __bitlen, __val) \
		*((u32 *)(__pstart)) =				\
		(						\
		LE_BITS_CLEARED_TO_4BYTE(__pstart, __bitoffset, __bitlen) | \
		((((u32)__val) & BIT_LEN_MASK_32(__bitlen)) << (__bitoffset)) \
		)

#define SET_BITS_TO_LE_2BYTE(__pstart, __bitoffset, __bitlen, __val) \
		*((u16 *)(__pstart)) =				\
		(					\
		LE_BITS_CLEARED_TO_2BYTE(__pstart, __bitoffset, __bitlen) | \
		((((u16)__val) & BIT_LEN_MASK_16(__bitlen)) << (__bitoffset)) \
		)

#define SET_BITS_TO_LE_1BYTE(__pstart, __bitoffset, __bitlen, __val) \
		*((u8 *)(__pstart)) = EF1BYTE			\
		(					\
		LE_BITS_CLEARED_TO_1BYTE(__pstart, __bitoffset, __bitlen) | \
		((((u8)__val) & BIT_LEN_MASK_8(__bitlen)) << (__bitoffset)) \
		)

#define LE_BITS_CLEARED_TO_1BYTE_8BIT(__pStart, __BitOffset, __BitLen) \
	(\
		LE_P1BYTE_TO_HOST_1BYTE(__pStart) \
	)

#define SET_BITS_TO_LE_1BYTE_8BIT(__pStart, __BitOffset, __BitLen, __Value) \
{ \
	*((u8 *)(__pStart)) = \
		EF1Byte(\
			LE_BITS_CLEARED_TO_1BYTE_8BIT(__pStart, __BitOffset, __BitLen) \
			| \
			((u8)__Value) \
		); \
}

/*  Get the N-bytes alignent offset from the current length */
#define N_BYTE_ALIGMENT(__Value, __Aligment) ((__Aligment == 1) ? (__Value) : (((__Value + __Aligment - 1) / __Aligment) * __Aligment))

#define TEST_FLAG(__Flag, __testFlag)		(((__Flag) & (__testFlag)) != 0)
#define SET_FLAG(__Flag, __setFlag)			((__Flag) |= __setFlag)
#define CLEAR_FLAG(__Flag, __clearFlag)		((__Flag) &= ~(__clearFlag))
#define CLEAR_FLAGS(__Flag)					((__Flag) = 0)
#define TEST_FLAGS(__Flag, __testFlags)		(((__Flag) & (__testFlags)) == (__testFlags))

#endif /* __BASIC_TYPES_H__ */

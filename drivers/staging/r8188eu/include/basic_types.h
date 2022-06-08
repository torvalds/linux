/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __BASIC_TYPES_H__
#define __BASIC_TYPES_H__

#include <linux/types.h>
#define NDIS_OID uint

typedef void (*proc_t)(void *);

#define FIELD_OFFSET(s, field)	((ssize_t)&((s *)(0))->field)

/* port from fw */
/*  TODO: Macros Below are Sync from SD7-Driver. It is necessary
 * to check correctness */

/*
 *	Call endian free function when
 *		1. Read/write packet content.
 *		2. Before write integer to IO.
 *		3. After read integer from IO.
*/

/* Convert little data endian to host ordering */
#define EF1BYTE(_val)		\
	((u8)(_val))

/* Create a bit mask  */
#define BIT_LEN_MASK_8(__bitlen) \
	(0xFF >> (8 - (__bitlen)))

/*Description:
 * Return 4-byte value in host byte ordering from
 * 4-byte pointer in little-endian system.
 */
#define LE_P1BYTE_TO_HOST_1BYTE(__pstart) \
	(EF1BYTE(*((u8 *)(__pstart))))

/*Description:
Translate subfield (continuous bits in little-endian) of 4-byte
value to host byte ordering.*/
#define LE_BITS_TO_1BYTE(__pstart, __bitoffset, __bitlen) \
	( \
		(LE_P1BYTE_TO_HOST_1BYTE(__pstart) >> (__bitoffset)) & \
		BIT_LEN_MASK_8(__bitlen) \
	)

#define	N_BYTE_ALIGMENT(__value, __aligment) ((__aligment == 1) ? \
	(__value) : (((__value + __aligment - 1) / __aligment) * __aligment))

#endif /* __BASIC_TYPES_H__ */

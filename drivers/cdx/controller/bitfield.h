/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2013 Solarflare Communications Inc.
 * Copyright (C) 2022-2023, Advanced Micro Devices, Inc.
 */

#ifndef CDX_BITFIELD_H
#define CDX_BITFIELD_H

#include <linux/bitfield.h>

/* Lowest bit numbers and widths */
#define CDX_DWORD_LBN 0
#define CDX_DWORD_WIDTH 32

/* Specified attribute (e.g. LBN) of the specified field */
#define CDX_VAL(field, attribute) field ## _ ## attribute
/* Low bit number of the specified field */
#define CDX_LOW_BIT(field) CDX_VAL(field, LBN)
/* Bit width of the specified field */
#define CDX_WIDTH(field) CDX_VAL(field, WIDTH)
/* High bit number of the specified field */
#define CDX_HIGH_BIT(field) (CDX_LOW_BIT(field) + CDX_WIDTH(field) - 1)

/* A doubleword (i.e. 4 byte) datatype - little-endian in HW */
struct cdx_dword {
	__le32 cdx_u32;
};

/* Value expanders for printk */
#define CDX_DWORD_VAL(dword)				\
	((unsigned int)le32_to_cpu((dword).cdx_u32))

/*
 * Extract bit field portion [low,high) from the 32-bit little-endian
 * element which contains bits [min,max)
 */
#define CDX_DWORD_FIELD(dword, field)					\
	(FIELD_GET(GENMASK(CDX_HIGH_BIT(field), CDX_LOW_BIT(field)),	\
		   le32_to_cpu((dword).cdx_u32)))

/*
 * Creates the portion of the named bit field that lies within the
 * range [min,max).
 */
#define CDX_INSERT_FIELD(field, value)				\
	(FIELD_PREP(GENMASK(CDX_HIGH_BIT(field),		\
			    CDX_LOW_BIT(field)), value))

/*
 * Creates the portion of the named bit fields that lie within the
 * range [min,max).
 */
#define CDX_INSERT_FIELDS(field1, value1,		\
			  field2, value2,		\
			  field3, value3,		\
			  field4, value4,		\
			  field5, value5,		\
			  field6, value6,		\
			  field7, value7)		\
	(CDX_INSERT_FIELD(field1, (value1)) |		\
	 CDX_INSERT_FIELD(field2, (value2)) |		\
	 CDX_INSERT_FIELD(field3, (value3)) |		\
	 CDX_INSERT_FIELD(field4, (value4)) |		\
	 CDX_INSERT_FIELD(field5, (value5)) |		\
	 CDX_INSERT_FIELD(field6, (value6)) |		\
	 CDX_INSERT_FIELD(field7, (value7)))

#define CDX_POPULATE_DWORD(dword, ...)					\
	(dword).cdx_u32 = cpu_to_le32(CDX_INSERT_FIELDS(__VA_ARGS__))

/* Populate a dword field with various numbers of arguments */
#define CDX_POPULATE_DWORD_7 CDX_POPULATE_DWORD
#define CDX_POPULATE_DWORD_6(dword, ...) \
	CDX_POPULATE_DWORD_7(dword, CDX_DWORD, 0, __VA_ARGS__)
#define CDX_POPULATE_DWORD_5(dword, ...) \
	CDX_POPULATE_DWORD_6(dword, CDX_DWORD, 0, __VA_ARGS__)
#define CDX_POPULATE_DWORD_4(dword, ...) \
	CDX_POPULATE_DWORD_5(dword, CDX_DWORD, 0, __VA_ARGS__)
#define CDX_POPULATE_DWORD_3(dword, ...) \
	CDX_POPULATE_DWORD_4(dword, CDX_DWORD, 0, __VA_ARGS__)
#define CDX_POPULATE_DWORD_2(dword, ...) \
	CDX_POPULATE_DWORD_3(dword, CDX_DWORD, 0, __VA_ARGS__)
#define CDX_POPULATE_DWORD_1(dword, ...) \
	CDX_POPULATE_DWORD_2(dword, CDX_DWORD, 0, __VA_ARGS__)
#define CDX_SET_DWORD(dword) \
	CDX_POPULATE_DWORD_1(dword, CDX_DWORD, 0xffffffff)

#endif /* CDX_BITFIELD_H */

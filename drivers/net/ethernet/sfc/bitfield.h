/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2013 Solarflare Communications Inc.
 */

#ifndef EFX_BITFIELD_H
#define EFX_BITFIELD_H

/*
 * Efx bitfield access
 *
 * Efx NICs make extensive use of bitfields up to 128 bits
 * wide.  Since there is no native 128-bit datatype on most systems,
 * and since 64-bit datatypes are inefficient on 32-bit systems and
 * vice versa, we wrap accesses in a way that uses the most efficient
 * datatype.
 *
 * The NICs are PCI devices and therefore little-endian.  Since most
 * of the quantities that we deal with are DMAed to/from host memory,
 * we define our datatypes (efx_oword_t, efx_qword_t and
 * efx_dword_t) to be little-endian.
 */

/* Lowest bit numbers and widths */
#define EFX_DUMMY_FIELD_LBN 0
#define EFX_DUMMY_FIELD_WIDTH 0
#define EFX_WORD_0_LBN 0
#define EFX_WORD_0_WIDTH 16
#define EFX_WORD_1_LBN 16
#define EFX_WORD_1_WIDTH 16
#define EFX_DWORD_0_LBN 0
#define EFX_DWORD_0_WIDTH 32
#define EFX_DWORD_1_LBN 32
#define EFX_DWORD_1_WIDTH 32
#define EFX_DWORD_2_LBN 64
#define EFX_DWORD_2_WIDTH 32
#define EFX_DWORD_3_LBN 96
#define EFX_DWORD_3_WIDTH 32
#define EFX_QWORD_0_LBN 0
#define EFX_QWORD_0_WIDTH 64

/* Specified attribute (e.g. LBN) of the specified field */
#define EFX_VAL(field, attribute) field ## _ ## attribute
/* Low bit number of the specified field */
#define EFX_LOW_BIT(field) EFX_VAL(field, LBN)
/* Bit width of the specified field */
#define EFX_WIDTH(field) EFX_VAL(field, WIDTH)
/* High bit number of the specified field */
#define EFX_HIGH_BIT(field) (EFX_LOW_BIT(field) + EFX_WIDTH(field) - 1)
/* Mask equal in width to the specified field.
 *
 * For example, a field with width 5 would have a mask of 0x1f.
 *
 * The maximum width mask that can be generated is 64 bits.
 */
#define EFX_MASK64(width)			\
	((width) == 64 ? ~((u64) 0) :		\
	 (((((u64) 1) << (width))) - 1))

/* Mask equal in width to the specified field.
 *
 * For example, a field with width 5 would have a mask of 0x1f.
 *
 * The maximum width mask that can be generated is 32 bits.  Use
 * EFX_MASK64 for higher width fields.
 */
#define EFX_MASK32(width)			\
	((width) == 32 ? ~((u32) 0) :		\
	 (((((u32) 1) << (width))) - 1))

/* A doubleword (i.e. 4 byte) datatype - little-endian in HW */
typedef union efx_dword {
	__le32 u32[1];
} efx_dword_t;

/* A quadword (i.e. 8 byte) datatype - little-endian in HW */
typedef union efx_qword {
	__le64 u64[1];
	__le32 u32[2];
	efx_dword_t dword[2];
} efx_qword_t;

/* An octword (eight-word, i.e. 16 byte) datatype - little-endian in HW */
typedef union efx_oword {
	__le64 u64[2];
	efx_qword_t qword[2];
	__le32 u32[4];
	efx_dword_t dword[4];
} efx_oword_t;

/* Format string and value expanders for printk */
#define EFX_DWORD_FMT "%08x"
#define EFX_QWORD_FMT "%08x:%08x"
#define EFX_OWORD_FMT "%08x:%08x:%08x:%08x"
#define EFX_DWORD_VAL(dword)				\
	((unsigned int) le32_to_cpu((dword).u32[0]))
#define EFX_QWORD_VAL(qword)				\
	((unsigned int) le32_to_cpu((qword).u32[1])),	\
	((unsigned int) le32_to_cpu((qword).u32[0]))
#define EFX_OWORD_VAL(oword)				\
	((unsigned int) le32_to_cpu((oword).u32[3])),	\
	((unsigned int) le32_to_cpu((oword).u32[2])),	\
	((unsigned int) le32_to_cpu((oword).u32[1])),	\
	((unsigned int) le32_to_cpu((oword).u32[0]))

/*
 * Extract bit field portion [low,high) from the native-endian element
 * which contains bits [min,max).
 *
 * For example, suppose "element" represents the high 32 bits of a
 * 64-bit value, and we wish to extract the bits belonging to the bit
 * field occupying bits 28-45 of this 64-bit value.
 *
 * Then EFX_EXTRACT ( element, 32, 63, 28, 45 ) would give
 *
 *   ( element ) << 4
 *
 * The result will contain the relevant bits filled in in the range
 * [0,high-low), with garbage in bits [high-low+1,...).
 */
#define EFX_EXTRACT_NATIVE(native_element, min, max, low, high)		\
	((low) > (max) || (high) < (min) ? 0 :				\
	 (low) > (min) ?						\
	 (native_element) >> ((low) - (min)) :				\
	 (native_element) << ((min) - (low)))

/*
 * Extract bit field portion [low,high) from the 64-bit little-endian
 * element which contains bits [min,max)
 */
#define EFX_EXTRACT64(element, min, max, low, high)			\
	EFX_EXTRACT_NATIVE(le64_to_cpu(element), min, max, low, high)

/*
 * Extract bit field portion [low,high) from the 32-bit little-endian
 * element which contains bits [min,max)
 */
#define EFX_EXTRACT32(element, min, max, low, high)			\
	EFX_EXTRACT_NATIVE(le32_to_cpu(element), min, max, low, high)

#define EFX_EXTRACT_OWORD64(oword, low, high)				\
	((EFX_EXTRACT64((oword).u64[0], 0, 63, low, high) |		\
	  EFX_EXTRACT64((oword).u64[1], 64, 127, low, high)) &		\
	 EFX_MASK64((high) + 1 - (low)))

#define EFX_EXTRACT_QWORD64(qword, low, high)				\
	(EFX_EXTRACT64((qword).u64[0], 0, 63, low, high) &		\
	 EFX_MASK64((high) + 1 - (low)))

#define EFX_EXTRACT_OWORD32(oword, low, high)				\
	((EFX_EXTRACT32((oword).u32[0], 0, 31, low, high) |		\
	  EFX_EXTRACT32((oword).u32[1], 32, 63, low, high) |		\
	  EFX_EXTRACT32((oword).u32[2], 64, 95, low, high) |		\
	  EFX_EXTRACT32((oword).u32[3], 96, 127, low, high)) &		\
	 EFX_MASK32((high) + 1 - (low)))

#define EFX_EXTRACT_QWORD32(qword, low, high)				\
	((EFX_EXTRACT32((qword).u32[0], 0, 31, low, high) |		\
	  EFX_EXTRACT32((qword).u32[1], 32, 63, low, high)) &		\
	 EFX_MASK32((high) + 1 - (low)))

#define EFX_EXTRACT_DWORD(dword, low, high)			\
	(EFX_EXTRACT32((dword).u32[0], 0, 31, low, high) &	\
	 EFX_MASK32((high) + 1 - (low)))

#define EFX_OWORD_FIELD64(oword, field)				\
	EFX_EXTRACT_OWORD64(oword, EFX_LOW_BIT(field),		\
			    EFX_HIGH_BIT(field))

#define EFX_QWORD_FIELD64(qword, field)				\
	EFX_EXTRACT_QWORD64(qword, EFX_LOW_BIT(field),		\
			    EFX_HIGH_BIT(field))

#define EFX_OWORD_FIELD32(oword, field)				\
	EFX_EXTRACT_OWORD32(oword, EFX_LOW_BIT(field),		\
			    EFX_HIGH_BIT(field))

#define EFX_QWORD_FIELD32(qword, field)				\
	EFX_EXTRACT_QWORD32(qword, EFX_LOW_BIT(field),		\
			    EFX_HIGH_BIT(field))

#define EFX_DWORD_FIELD(dword, field)				\
	EFX_EXTRACT_DWORD(dword, EFX_LOW_BIT(field),		\
			  EFX_HIGH_BIT(field))

#define EFX_OWORD_IS_ZERO64(oword)					\
	(((oword).u64[0] | (oword).u64[1]) == (__force __le64) 0)

#define EFX_QWORD_IS_ZERO64(qword)					\
	(((qword).u64[0]) == (__force __le64) 0)

#define EFX_OWORD_IS_ZERO32(oword)					     \
	(((oword).u32[0] | (oword).u32[1] | (oword).u32[2] | (oword).u32[3]) \
	 == (__force __le32) 0)

#define EFX_QWORD_IS_ZERO32(qword)					\
	(((qword).u32[0] | (qword).u32[1]) == (__force __le32) 0)

#define EFX_DWORD_IS_ZERO(dword)					\
	(((dword).u32[0]) == (__force __le32) 0)

#define EFX_OWORD_IS_ALL_ONES64(oword)					\
	(((oword).u64[0] & (oword).u64[1]) == ~((__force __le64) 0))

#define EFX_QWORD_IS_ALL_ONES64(qword)					\
	((qword).u64[0] == ~((__force __le64) 0))

#define EFX_OWORD_IS_ALL_ONES32(oword)					\
	(((oword).u32[0] & (oword).u32[1] & (oword).u32[2] & (oword).u32[3]) \
	 == ~((__force __le32) 0))

#define EFX_QWORD_IS_ALL_ONES32(qword)					\
	(((qword).u32[0] & (qword).u32[1]) == ~((__force __le32) 0))

#define EFX_DWORD_IS_ALL_ONES(dword)					\
	((dword).u32[0] == ~((__force __le32) 0))

#if BITS_PER_LONG == 64
#define EFX_OWORD_FIELD		EFX_OWORD_FIELD64
#define EFX_QWORD_FIELD		EFX_QWORD_FIELD64
#define EFX_OWORD_IS_ZERO	EFX_OWORD_IS_ZERO64
#define EFX_QWORD_IS_ZERO	EFX_QWORD_IS_ZERO64
#define EFX_OWORD_IS_ALL_ONES	EFX_OWORD_IS_ALL_ONES64
#define EFX_QWORD_IS_ALL_ONES	EFX_QWORD_IS_ALL_ONES64
#else
#define EFX_OWORD_FIELD		EFX_OWORD_FIELD32
#define EFX_QWORD_FIELD		EFX_QWORD_FIELD32
#define EFX_OWORD_IS_ZERO	EFX_OWORD_IS_ZERO32
#define EFX_QWORD_IS_ZERO	EFX_QWORD_IS_ZERO32
#define EFX_OWORD_IS_ALL_ONES	EFX_OWORD_IS_ALL_ONES32
#define EFX_QWORD_IS_ALL_ONES	EFX_QWORD_IS_ALL_ONES32
#endif

/*
 * Construct bit field portion
 *
 * Creates the portion of the bit field [low,high) that lies within
 * the range [min,max).
 */
#define EFX_INSERT_NATIVE64(min, max, low, high, value)		\
	(((low > max) || (high < min)) ? 0 :			\
	 ((low > min) ?						\
	  (((u64) (value)) << (low - min)) :		\
	  (((u64) (value)) >> (min - low))))

#define EFX_INSERT_NATIVE32(min, max, low, high, value)		\
	(((low > max) || (high < min)) ? 0 :			\
	 ((low > min) ?						\
	  (((u32) (value)) << (low - min)) :		\
	  (((u32) (value)) >> (min - low))))

#define EFX_INSERT_NATIVE(min, max, low, high, value)		\
	((((max - min) >= 32) || ((high - low) >= 32)) ?	\
	 EFX_INSERT_NATIVE64(min, max, low, high, value) :	\
	 EFX_INSERT_NATIVE32(min, max, low, high, value))

/*
 * Construct bit field portion
 *
 * Creates the portion of the named bit field that lies within the
 * range [min,max).
 */
#define EFX_INSERT_FIELD_NATIVE(min, max, field, value)		\
	EFX_INSERT_NATIVE(min, max, EFX_LOW_BIT(field),		\
			  EFX_HIGH_BIT(field), value)

/*
 * Construct bit field
 *
 * Creates the portion of the named bit fields that lie within the
 * range [min,max).
 */
#define EFX_INSERT_FIELDS_NATIVE(min, max,				\
				 field1, value1,			\
				 field2, value2,			\
				 field3, value3,			\
				 field4, value4,			\
				 field5, value5,			\
				 field6, value6,			\
				 field7, value7,			\
				 field8, value8,			\
				 field9, value9,			\
				 field10, value10,			\
				 field11, value11,			\
				 field12, value12,			\
				 field13, value13)			\
	(EFX_INSERT_FIELD_NATIVE((min), (max), field1, (value1)) |	\
	 EFX_INSERT_FIELD_NATIVE((min), (max), field2, (value2)) |	\
	 EFX_INSERT_FIELD_NATIVE((min), (max), field3, (value3)) |	\
	 EFX_INSERT_FIELD_NATIVE((min), (max), field4, (value4)) |	\
	 EFX_INSERT_FIELD_NATIVE((min), (max), field5, (value5)) |	\
	 EFX_INSERT_FIELD_NATIVE((min), (max), field6, (value6)) |	\
	 EFX_INSERT_FIELD_NATIVE((min), (max), field7, (value7)) |	\
	 EFX_INSERT_FIELD_NATIVE((min), (max), field8, (value8)) |	\
	 EFX_INSERT_FIELD_NATIVE((min), (max), field9, (value9)) |	\
	 EFX_INSERT_FIELD_NATIVE((min), (max), field10, (value10)) |	\
	 EFX_INSERT_FIELD_NATIVE((min), (max), field11, (value11)) |	\
	 EFX_INSERT_FIELD_NATIVE((min), (max), field12, (value12)) |	\
	 EFX_INSERT_FIELD_NATIVE((min), (max), field13, (value13)))

#define EFX_INSERT_FIELDS64(...)				\
	cpu_to_le64(EFX_INSERT_FIELDS_NATIVE(__VA_ARGS__))

#define EFX_INSERT_FIELDS32(...)				\
	cpu_to_le32(EFX_INSERT_FIELDS_NATIVE(__VA_ARGS__))

#define EFX_POPULATE_OWORD64(oword, ...) do {				\
	(oword).u64[0] = EFX_INSERT_FIELDS64(0, 63, __VA_ARGS__);	\
	(oword).u64[1] = EFX_INSERT_FIELDS64(64, 127, __VA_ARGS__);	\
	} while (0)

#define EFX_POPULATE_QWORD64(qword, ...) do {				\
	(qword).u64[0] = EFX_INSERT_FIELDS64(0, 63, __VA_ARGS__);	\
	} while (0)

#define EFX_POPULATE_OWORD32(oword, ...) do {				\
	(oword).u32[0] = EFX_INSERT_FIELDS32(0, 31, __VA_ARGS__);	\
	(oword).u32[1] = EFX_INSERT_FIELDS32(32, 63, __VA_ARGS__);	\
	(oword).u32[2] = EFX_INSERT_FIELDS32(64, 95, __VA_ARGS__);	\
	(oword).u32[3] = EFX_INSERT_FIELDS32(96, 127, __VA_ARGS__);	\
	} while (0)

#define EFX_POPULATE_QWORD32(qword, ...) do {				\
	(qword).u32[0] = EFX_INSERT_FIELDS32(0, 31, __VA_ARGS__);	\
	(qword).u32[1] = EFX_INSERT_FIELDS32(32, 63, __VA_ARGS__);	\
	} while (0)

#define EFX_POPULATE_DWORD(dword, ...) do {				\
	(dword).u32[0] = EFX_INSERT_FIELDS32(0, 31, __VA_ARGS__);	\
	} while (0)

#if BITS_PER_LONG == 64
#define EFX_POPULATE_OWORD EFX_POPULATE_OWORD64
#define EFX_POPULATE_QWORD EFX_POPULATE_QWORD64
#else
#define EFX_POPULATE_OWORD EFX_POPULATE_OWORD32
#define EFX_POPULATE_QWORD EFX_POPULATE_QWORD32
#endif

/* Populate an octword field with various numbers of arguments */
#define EFX_POPULATE_OWORD_13 EFX_POPULATE_OWORD
#define EFX_POPULATE_OWORD_12(oword, ...) \
	EFX_POPULATE_OWORD_13(oword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_OWORD_11(oword, ...) \
	EFX_POPULATE_OWORD_12(oword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_OWORD_10(oword, ...) \
	EFX_POPULATE_OWORD_11(oword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_OWORD_9(oword, ...) \
	EFX_POPULATE_OWORD_10(oword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_OWORD_8(oword, ...) \
	EFX_POPULATE_OWORD_9(oword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_OWORD_7(oword, ...) \
	EFX_POPULATE_OWORD_8(oword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_OWORD_6(oword, ...) \
	EFX_POPULATE_OWORD_7(oword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_OWORD_5(oword, ...) \
	EFX_POPULATE_OWORD_6(oword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_OWORD_4(oword, ...) \
	EFX_POPULATE_OWORD_5(oword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_OWORD_3(oword, ...) \
	EFX_POPULATE_OWORD_4(oword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_OWORD_2(oword, ...) \
	EFX_POPULATE_OWORD_3(oword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_OWORD_1(oword, ...) \
	EFX_POPULATE_OWORD_2(oword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_ZERO_OWORD(oword) \
	EFX_POPULATE_OWORD_1(oword, EFX_DUMMY_FIELD, 0)
#define EFX_SET_OWORD(oword) \
	EFX_POPULATE_OWORD_4(oword, \
			     EFX_DWORD_0, 0xffffffff, \
			     EFX_DWORD_1, 0xffffffff, \
			     EFX_DWORD_2, 0xffffffff, \
			     EFX_DWORD_3, 0xffffffff)

/* Populate a quadword field with various numbers of arguments */
#define EFX_POPULATE_QWORD_13 EFX_POPULATE_QWORD
#define EFX_POPULATE_QWORD_12(qword, ...) \
	EFX_POPULATE_QWORD_13(qword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_QWORD_11(qword, ...) \
	EFX_POPULATE_QWORD_12(qword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_QWORD_10(qword, ...) \
	EFX_POPULATE_QWORD_11(qword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_QWORD_9(qword, ...) \
	EFX_POPULATE_QWORD_10(qword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_QWORD_8(qword, ...) \
	EFX_POPULATE_QWORD_9(qword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_QWORD_7(qword, ...) \
	EFX_POPULATE_QWORD_8(qword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_QWORD_6(qword, ...) \
	EFX_POPULATE_QWORD_7(qword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_QWORD_5(qword, ...) \
	EFX_POPULATE_QWORD_6(qword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_QWORD_4(qword, ...) \
	EFX_POPULATE_QWORD_5(qword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_QWORD_3(qword, ...) \
	EFX_POPULATE_QWORD_4(qword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_QWORD_2(qword, ...) \
	EFX_POPULATE_QWORD_3(qword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_QWORD_1(qword, ...) \
	EFX_POPULATE_QWORD_2(qword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_ZERO_QWORD(qword) \
	EFX_POPULATE_QWORD_1(qword, EFX_DUMMY_FIELD, 0)
#define EFX_SET_QWORD(qword) \
	EFX_POPULATE_QWORD_2(qword, \
			     EFX_DWORD_0, 0xffffffff, \
			     EFX_DWORD_1, 0xffffffff)

/* Populate a dword field with various numbers of arguments */
#define EFX_POPULATE_DWORD_13 EFX_POPULATE_DWORD
#define EFX_POPULATE_DWORD_12(dword, ...) \
	EFX_POPULATE_DWORD_13(dword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_DWORD_11(dword, ...) \
	EFX_POPULATE_DWORD_12(dword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_DWORD_10(dword, ...) \
	EFX_POPULATE_DWORD_11(dword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_DWORD_9(dword, ...) \
	EFX_POPULATE_DWORD_10(dword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_DWORD_8(dword, ...) \
	EFX_POPULATE_DWORD_9(dword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_DWORD_7(dword, ...) \
	EFX_POPULATE_DWORD_8(dword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_DWORD_6(dword, ...) \
	EFX_POPULATE_DWORD_7(dword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_DWORD_5(dword, ...) \
	EFX_POPULATE_DWORD_6(dword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_DWORD_4(dword, ...) \
	EFX_POPULATE_DWORD_5(dword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_DWORD_3(dword, ...) \
	EFX_POPULATE_DWORD_4(dword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_DWORD_2(dword, ...) \
	EFX_POPULATE_DWORD_3(dword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_POPULATE_DWORD_1(dword, ...) \
	EFX_POPULATE_DWORD_2(dword, EFX_DUMMY_FIELD, 0, __VA_ARGS__)
#define EFX_ZERO_DWORD(dword) \
	EFX_POPULATE_DWORD_1(dword, EFX_DUMMY_FIELD, 0)
#define EFX_SET_DWORD(dword) \
	EFX_POPULATE_DWORD_1(dword, EFX_DWORD_0, 0xffffffff)

/*
 * Modify a named field within an already-populated structure.  Used
 * for read-modify-write operations.
 *
 */
#define EFX_INVERT_OWORD(oword) do {		\
	(oword).u64[0] = ~((oword).u64[0]);	\
	(oword).u64[1] = ~((oword).u64[1]);	\
	} while (0)

#define EFX_AND_OWORD(oword, from, mask)			\
	do {							\
		(oword).u64[0] = (from).u64[0] & (mask).u64[0];	\
		(oword).u64[1] = (from).u64[1] & (mask).u64[1];	\
	} while (0)

#define EFX_AND_QWORD(qword, from, mask)			\
		(qword).u64[0] = (from).u64[0] & (mask).u64[0]

#define EFX_OR_OWORD(oword, from, mask)				\
	do {							\
		(oword).u64[0] = (from).u64[0] | (mask).u64[0];	\
		(oword).u64[1] = (from).u64[1] | (mask).u64[1];	\
	} while (0)

#define EFX_INSERT64(min, max, low, high, value)			\
	cpu_to_le64(EFX_INSERT_NATIVE(min, max, low, high, value))

#define EFX_INSERT32(min, max, low, high, value)			\
	cpu_to_le32(EFX_INSERT_NATIVE(min, max, low, high, value))

#define EFX_INPLACE_MASK64(min, max, low, high)				\
	EFX_INSERT64(min, max, low, high, EFX_MASK64((high) + 1 - (low)))

#define EFX_INPLACE_MASK32(min, max, low, high)				\
	EFX_INSERT32(min, max, low, high, EFX_MASK32((high) + 1 - (low)))

#define EFX_SET_OWORD64(oword, low, high, value) do {			\
	(oword).u64[0] = (((oword).u64[0]				\
			   & ~EFX_INPLACE_MASK64(0,  63, low, high))	\
			  | EFX_INSERT64(0,  63, low, high, value));	\
	(oword).u64[1] = (((oword).u64[1]				\
			   & ~EFX_INPLACE_MASK64(64, 127, low, high))	\
			  | EFX_INSERT64(64, 127, low, high, value));	\
	} while (0)

#define EFX_SET_QWORD64(qword, low, high, value) do {			\
	(qword).u64[0] = (((qword).u64[0]				\
			   & ~EFX_INPLACE_MASK64(0, 63, low, high))	\
			  | EFX_INSERT64(0, 63, low, high, value));	\
	} while (0)

#define EFX_SET_OWORD32(oword, low, high, value) do {			\
	(oword).u32[0] = (((oword).u32[0]				\
			   & ~EFX_INPLACE_MASK32(0, 31, low, high))	\
			  | EFX_INSERT32(0, 31, low, high, value));	\
	(oword).u32[1] = (((oword).u32[1]				\
			   & ~EFX_INPLACE_MASK32(32, 63, low, high))	\
			  | EFX_INSERT32(32, 63, low, high, value));	\
	(oword).u32[2] = (((oword).u32[2]				\
			   & ~EFX_INPLACE_MASK32(64, 95, low, high))	\
			  | EFX_INSERT32(64, 95, low, high, value));	\
	(oword).u32[3] = (((oword).u32[3]				\
			   & ~EFX_INPLACE_MASK32(96, 127, low, high))	\
			  | EFX_INSERT32(96, 127, low, high, value));	\
	} while (0)

#define EFX_SET_QWORD32(qword, low, high, value) do {			\
	(qword).u32[0] = (((qword).u32[0]				\
			   & ~EFX_INPLACE_MASK32(0, 31, low, high))	\
			  | EFX_INSERT32(0, 31, low, high, value));	\
	(qword).u32[1] = (((qword).u32[1]				\
			   & ~EFX_INPLACE_MASK32(32, 63, low, high))	\
			  | EFX_INSERT32(32, 63, low, high, value));	\
	} while (0)

#define EFX_SET_DWORD32(dword, low, high, value) do {			\
	(dword).u32[0] = (((dword).u32[0]				\
			   & ~EFX_INPLACE_MASK32(0, 31, low, high))	\
			  | EFX_INSERT32(0, 31, low, high, value));	\
	} while (0)

#define EFX_SET_OWORD_FIELD64(oword, field, value)			\
	EFX_SET_OWORD64(oword, EFX_LOW_BIT(field),			\
			 EFX_HIGH_BIT(field), value)

#define EFX_SET_QWORD_FIELD64(qword, field, value)			\
	EFX_SET_QWORD64(qword, EFX_LOW_BIT(field),			\
			 EFX_HIGH_BIT(field), value)

#define EFX_SET_OWORD_FIELD32(oword, field, value)			\
	EFX_SET_OWORD32(oword, EFX_LOW_BIT(field),			\
			 EFX_HIGH_BIT(field), value)

#define EFX_SET_QWORD_FIELD32(qword, field, value)			\
	EFX_SET_QWORD32(qword, EFX_LOW_BIT(field),			\
			 EFX_HIGH_BIT(field), value)

#define EFX_SET_DWORD_FIELD(dword, field, value)			\
	EFX_SET_DWORD32(dword, EFX_LOW_BIT(field),			\
			 EFX_HIGH_BIT(field), value)



#if BITS_PER_LONG == 64
#define EFX_SET_OWORD_FIELD EFX_SET_OWORD_FIELD64
#define EFX_SET_QWORD_FIELD EFX_SET_QWORD_FIELD64
#else
#define EFX_SET_OWORD_FIELD EFX_SET_OWORD_FIELD32
#define EFX_SET_QWORD_FIELD EFX_SET_QWORD_FIELD32
#endif

/* Used to avoid compiler warnings about shift range exceeding width
 * of the data types when dma_addr_t is only 32 bits wide.
 */
#define DMA_ADDR_T_WIDTH	(8 * sizeof(dma_addr_t))
#define EFX_DMA_TYPE_WIDTH(width) \
	(((width) < DMA_ADDR_T_WIDTH) ? (width) : DMA_ADDR_T_WIDTH)


/* Static initialiser */
#define EFX_OWORD32(a, b, c, d)				\
	{ .u32 = { cpu_to_le32(a), cpu_to_le32(b),	\
		   cpu_to_le32(c), cpu_to_le32(d) } }

#endif /* EFX_BITFIELD_H */

/*
 * GlobalTypes.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Global HW definitions
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _GLOBALTYPES_H
#define _GLOBALTYPES_H

/*
 * Definition: RET_CODE_BASE
 *
 * DESCRIPTION:  Base value for return code offsets
 */
#define RET_CODE_BASE	0

/*
 * Definition: *BIT_OFFSET
 *
 * DESCRIPTION:  offset in bytes from start of 32-bit word.
 */
#define LOWER16BIT_OFFSET	  0
#define UPPER16BIT_OFFSET	  2

#define LOWER8BIT_OFFSET	   0
#define LOWER_MIDDLE8BIT_OFFSET    1
#define UPPER_MIDDLE8BIT_OFFSET    2
#define UPPER8BIT_OFFSET	   3

#define LOWER8BIT_OF16_OFFSET      0
#define UPPER8BIT_OF16_OFFSET      1

/*
 * Definition: *BIT_SHIFT
 *
 * DESCRIPTION:  offset in bits from start of 32-bit word.
 */
#define LOWER16BIT_SHIFT	  0
#define UPPER16BIT_SHIFT	  16

#define LOWER8BIT_SHIFT	   0
#define LOWER_MIDDLE8BIT_SHIFT    8
#define UPPER_MIDDLE8BIT_SHIFT    16
#define UPPER8BIT_SHIFT	   24

#define LOWER8BIT_OF16_SHIFT      0
#define UPPER8BIT_OF16_SHIFT      8

/*
 * Definition: LOWER16BIT_MASK
 *
 * DESCRIPTION: 16 bit mask used for inclusion of lower 16 bits i.e. mask out
 *		the upper 16 bits
 */
#define LOWER16BIT_MASK	0x0000FFFF

/*
 * Definition: LOWER8BIT_MASK
 *
 * DESCRIPTION: 8 bit masks used for inclusion of 8 bits i.e. mask out
 *		the upper 16 bits
 */
#define LOWER8BIT_MASK	   0x000000FF

/*
 * Definition: RETURN32BITS_FROM16LOWER_AND16UPPER(lower16_bits, upper16_bits)
 *
 * DESCRIPTION: Returns a 32 bit value given a 16 bit lower value and a 16
 *		bit upper value
 */
#define RETURN32BITS_FROM16LOWER_AND16UPPER(lower16_bits, upper16_bits)\
    (((((u32)lower16_bits)  & LOWER16BIT_MASK)) | \
     (((((u32)upper16_bits) & LOWER16BIT_MASK) << UPPER16BIT_SHIFT)))

/*
 * Definition: RETURN16BITS_FROM8LOWER_AND8UPPER(lower16_bits, upper16_bits)
 *
 * DESCRIPTION:  Returns a 16 bit value given a 8 bit lower value and a 8
 *	       bit upper value
 */
#define RETURN16BITS_FROM8LOWER_AND8UPPER(lower8_bits, upper8_bits)\
    (((((u32)lower8_bits)  & LOWER8BIT_MASK)) | \
     (((((u32)upper8_bits) & LOWER8BIT_MASK) << UPPER8BIT_OF16_SHIFT)))

/*
 * Definition: RETURN32BITS_FROM48BIT_VALUES(lower8_bits, lower_middle8_bits,
 *					lower_upper8_bits, upper8_bits)
 *
 * DESCRIPTION:  Returns a 32 bit value given four 8 bit values
 */
#define RETURN32BITS_FROM48BIT_VALUES(lower8_bits, lower_middle8_bits,\
	lower_upper8_bits, upper8_bits)\
	(((((u32)lower8_bits) & LOWER8BIT_MASK)) | \
	(((((u32)lower_middle8_bits) & LOWER8BIT_MASK) <<\
		LOWER_MIDDLE8BIT_SHIFT)) | \
	(((((u32)lower_upper8_bits) & LOWER8BIT_MASK) <<\
		UPPER_MIDDLE8BIT_SHIFT)) | \
	(((((u32)upper8_bits) & LOWER8BIT_MASK) <<\
		UPPER8BIT_SHIFT)))

/*
 * Definition: READ_LOWER16BITS_OF32(value32bits)
 *
 * DESCRIPTION:  Returns a 16 lower bits of 32bit value
 */
#define READ_LOWER16BITS_OF32(value32bits)\
    ((u16)((u32)(value32bits) & LOWER16BIT_MASK))

/*
 * Definition: READ_UPPER16BITS_OF32(value32bits)
 *
 * DESCRIPTION:  Returns a 16 lower bits of 32bit value
 */
#define READ_UPPER16BITS_OF32(value32bits)\
	(((u16)((u32)(value32bits) >> UPPER16BIT_SHIFT)) &\
	LOWER16BIT_MASK)

/*
 * Definition: READ_LOWER8BITS_OF32(value32bits)
 *
 * DESCRIPTION:  Returns a 8 lower bits of 32bit value
 */
#define READ_LOWER8BITS_OF32(value32bits)\
    ((u8)((u32)(value32bits) & LOWER8BIT_MASK))

/*
 * Definition: READ_LOWER_MIDDLE8BITS_OF32(value32bits)
 *
 * DESCRIPTION:  Returns a 8 lower middle bits of 32bit value
 */
#define READ_LOWER_MIDDLE8BITS_OF32(value32bits)\
	(((u8)((u32)(value32bits) >> LOWER_MIDDLE8BIT_SHIFT)) &\
	LOWER8BIT_MASK)

/*
 * Definition: READ_LOWER_MIDDLE8BITS_OF32(value32bits)
 *
 * DESCRIPTION:  Returns a 8 lower middle bits of 32bit value
 */
#define READ_UPPER_MIDDLE8BITS_OF32(value32bits)\
	(((u8)((u32)(value32bits) >> LOWER_MIDDLE8BIT_SHIFT)) &\
	LOWER8BIT_MASK)

/*
 * Definition: READ_UPPER8BITS_OF32(value32bits)
 *
 * DESCRIPTION:  Returns a 8 upper bits of 32bit value
 */
#define READ_UPPER8BITS_OF32(value32bits)\
    (((u8)((u32)(value32bits) >> UPPER8BIT_SHIFT)) & LOWER8BIT_MASK)

/*
 * Definition: READ_LOWER8BITS_OF16(value16bits)
 *
 * DESCRIPTION:  Returns a 8 lower bits of 16bit value
 */
#define READ_LOWER8BITS_OF16(value16bits)\
    ((u8)((u16)(value16bits) & LOWER8BIT_MASK))

/*
 * Definition: READ_UPPER8BITS_OF16(value32bits)
 *
 * DESCRIPTION:  Returns a 8 upper bits of 16bit value
 */
#define READ_UPPER8BITS_OF16(value16bits)\
    (((u8)((u32)(value16bits) >> UPPER8BIT_SHIFT)) & LOWER8BIT_MASK)

/* UWORD16:  16 bit tpyes */

/* reg_uword8, reg_word8: 8 bit register types */
typedef volatile unsigned char reg_uword8;
typedef volatile signed char reg_word8;

/* reg_uword16, reg_word16: 16 bit register types */
#ifndef OMAPBRIDGE_TYPES
typedef volatile unsigned short reg_uword16;
#endif
typedef volatile short reg_word16;

/* reg_uword32, REG_WORD32: 32 bit register types */
typedef volatile unsigned long reg_uword32;

/* FLOAT
 *
 * Type to be used for floating point calculation. Note that floating point
 * calculation is very CPU expensive, and you should only  use if you
 * absolutely need this. */

/* boolean_t:  Boolean Type True, False */
/* return_code_t:  Return codes to be returned by all library functions */
enum return_code_label {
	RET_OK = 0,
	RET_FAIL = -1,
	RET_BAD_NULL_PARAM = -2,
	RET_PARAM_OUT_OF_RANGE = -3,
	RET_INVALID_ID = -4,
	RET_EMPTY = -5,
	RET_FULL = -6,
	RET_TIMEOUT = -7,
	RET_INVALID_OPERATION = -8,

	/* Add new error codes at end of above list */

	RET_NUM_RET_CODES	/* this should ALWAYS be LAST entry */
};

/* MACRO: RD_MEM8, WR_MEM8
 *
 * DESCRIPTION:  32 bit memory access macros
 */
#define RD_MEM8(addr)	((u8)(*((u8 *)(addr))))
#define WR_MEM8(addr, data)	(*((u8 *)(addr)) = (u8)(data))

/* MACRO: RD_MEM8_VOLATILE, WR_MEM8_VOLATILE
 *
 * DESCRIPTION:  8 bit register access macros
 */
#define RD_MEM8_VOLATILE(addr)	((u8)(*((reg_uword8 *)(addr))))
#define WR_MEM8_VOLATILE(addr, data) (*((reg_uword8 *)(addr)) = (u8)(data))

/*
 * MACRO: RD_MEM16, WR_MEM16
 *
 * DESCRIPTION:  16 bit memory access macros
 */
#define RD_MEM16(addr)	((u16)(*((u16 *)(addr))))
#define WR_MEM16(addr, data)	(*((u16 *)(addr)) = (u16)(data))

/*
 * MACRO: RD_MEM16_VOLATILE, WR_MEM16_VOLATILE
 *
 * DESCRIPTION:  16 bit register access macros
 */
#define RD_MEM16_VOLATILE(addr)	((u16)(*((reg_uword16 *)(addr))))
#define WR_MEM16_VOLATILE(addr, data)	(*((reg_uword16 *)(addr)) =\
					(u16)(data))

/*
 * MACRO: RD_MEM32, WR_MEM32
 *
 * DESCRIPTION:  32 bit memory access macros
 */
#define RD_MEM32(addr)	((u32)(*((u32 *)(addr))))
#define WR_MEM32(addr, data)	(*((u32 *)(addr)) = (u32)(data))

/*
 * MACRO: RD_MEM32_VOLATILE, WR_MEM32_VOLATILE
 *
 * DESCRIPTION:  32 bit register access macros
 */
#define RD_MEM32_VOLATILE(addr)	((u32)(*((reg_uword32 *)(addr))))
#define WR_MEM32_VOLATILE(addr, data)	(*((reg_uword32 *)(addr)) =\
					(u32)(data))

/* Not sure if this all belongs here */

#define CHECK_RETURN_VALUE(actual_value, expected_value,\
	return_code_if_mismatch, spy_code_if_mis_match)
#define CHECK_RETURN_VALUE_RET(actual_value, expected_value,\
	return_code_if_mismatch)
#define CHECK_RETURN_VALUE_RES(actual_value, expected_value,\
	spy_code_if_mis_match)
#define CHECK_RETURN_VALUE_RET_VOID(actual_value, expected_value,\
	spy_code_if_mis_match)

#define CHECK_INPUT_PARAM(actual_value, invalid_value,\
	return_code_if_mismatch, spy_code_if_mis_match)
#define CHECK_INPUT_PARAM_NO_SPY(actual_value, invalid_value,\
	return_code_if_mismatch)
#define CHECK_INPUT_RANGE(actual_value, min_valid_value, max_valid_value,\
	return_code_if_mismatch, spy_code_if_mis_match)
#define CHECK_INPUT_RANGE_NO_SPY(actual_value, min_valid_value,\
	max_valid_value, return_code_if_mismatch)
#define CHECK_INPUT_RANGE_MIN0(actual_value, max_valid_value,\
	return_code_if_mismatch, spy_code_if_mis_match)
#define CHECK_INPUT_RANGE_NO_SPY_MIN0(actual_value, max_valid_value,\
	return_code_if_mismatch)

#endif /* _GLOBALTYPES_H */

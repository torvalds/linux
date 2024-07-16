/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2008 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * or visit http://www.gnu.org/licenses/.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium Networks for more information
 ***********************license end**************************************/

/*
 * Interface to the hardware Fetch and Add Unit.
 */

#ifndef __CVMX_FAU_H__
#define __CVMX_FAU_H__

/*
 * Octeon Fetch and Add Unit (FAU)
 */

#define CVMX_FAU_LOAD_IO_ADDRESS    cvmx_build_io_address(0x1e, 0)
#define CVMX_FAU_BITS_SCRADDR	    63, 56
#define CVMX_FAU_BITS_LEN	    55, 48
#define CVMX_FAU_BITS_INEVAL	    35, 14
#define CVMX_FAU_BITS_TAGWAIT	    13, 13
#define CVMX_FAU_BITS_NOADD	    13, 13
#define CVMX_FAU_BITS_SIZE	    12, 11
#define CVMX_FAU_BITS_REGISTER	    10, 0

typedef enum {
	CVMX_FAU_OP_SIZE_8 = 0,
	CVMX_FAU_OP_SIZE_16 = 1,
	CVMX_FAU_OP_SIZE_32 = 2,
	CVMX_FAU_OP_SIZE_64 = 3
} cvmx_fau_op_size_t;

/**
 * Tagwait return definition. If a timeout occurs, the error
 * bit will be set. Otherwise the value of the register before
 * the update will be returned.
 */
typedef struct {
	uint64_t error:1;
	int64_t value:63;
} cvmx_fau_tagwait64_t;

/**
 * Tagwait return definition. If a timeout occurs, the error
 * bit will be set. Otherwise the value of the register before
 * the update will be returned.
 */
typedef struct {
	uint64_t error:1;
	int32_t value:31;
} cvmx_fau_tagwait32_t;

/**
 * Tagwait return definition. If a timeout occurs, the error
 * bit will be set. Otherwise the value of the register before
 * the update will be returned.
 */
typedef struct {
	uint64_t error:1;
	int16_t value:15;
} cvmx_fau_tagwait16_t;

/**
 * Tagwait return definition. If a timeout occurs, the error
 * bit will be set. Otherwise the value of the register before
 * the update will be returned.
 */
typedef struct {
	uint64_t error:1;
	int8_t value:7;
} cvmx_fau_tagwait8_t;

/**
 * Asynchronous tagwait return definition. If a timeout occurs,
 * the error bit will be set. Otherwise the value of the
 * register before the update will be returned.
 */
typedef union {
	uint64_t u64;
	struct {
		uint64_t invalid:1;
		uint64_t data:63;	/* unpredictable if invalid is set */
	} s;
} cvmx_fau_async_tagwait_result_t;

#ifdef __BIG_ENDIAN_BITFIELD
#define SWIZZLE_8  0
#define SWIZZLE_16 0
#define SWIZZLE_32 0
#else
#define SWIZZLE_8  0x7
#define SWIZZLE_16 0x6
#define SWIZZLE_32 0x4
#endif

/**
 * Builds a store I/O address for writing to the FAU
 *
 * @noadd:  0 = Store value is atomically added to the current value
 *		 1 = Store value is atomically written over the current value
 * @reg:    FAU atomic register to access. 0 <= reg < 2048.
 *		 - Step by 2 for 16 bit access.
 *		 - Step by 4 for 32 bit access.
 *		 - Step by 8 for 64 bit access.
 * Returns Address to store for atomic update
 */
static inline uint64_t __cvmx_fau_store_address(uint64_t noadd, uint64_t reg)
{
	return CVMX_ADD_IO_SEG(CVMX_FAU_LOAD_IO_ADDRESS) |
	       cvmx_build_bits(CVMX_FAU_BITS_NOADD, noadd) |
	       cvmx_build_bits(CVMX_FAU_BITS_REGISTER, reg);
}

/**
 * Builds a I/O address for accessing the FAU
 *
 * @tagwait: Should the atomic add wait for the current tag switch
 *		  operation to complete.
 *		  - 0 = Don't wait
 *		  - 1 = Wait for tag switch to complete
 * @reg:     FAU atomic register to access. 0 <= reg < 2048.
 *		  - Step by 2 for 16 bit access.
 *		  - Step by 4 for 32 bit access.
 *		  - Step by 8 for 64 bit access.
 * @value:   Signed value to add.
 *		  Note: When performing 32 and 64 bit access, only the low
 *		  22 bits are available.
 * Returns Address to read from for atomic update
 */
static inline uint64_t __cvmx_fau_atomic_address(uint64_t tagwait, uint64_t reg,
						 int64_t value)
{
	return CVMX_ADD_IO_SEG(CVMX_FAU_LOAD_IO_ADDRESS) |
	       cvmx_build_bits(CVMX_FAU_BITS_INEVAL, value) |
	       cvmx_build_bits(CVMX_FAU_BITS_TAGWAIT, tagwait) |
	       cvmx_build_bits(CVMX_FAU_BITS_REGISTER, reg);
}

/**
 * Perform an atomic 64 bit add
 *
 * @reg:     FAU atomic register to access. 0 <= reg < 2048.
 *		  - Step by 8 for 64 bit access.
 * @value:   Signed value to add.
 *		  Note: Only the low 22 bits are available.
 * Returns Value of the register before the update
 */
static inline int64_t cvmx_fau_fetch_and_add64(cvmx_fau_reg_64_t reg,
					       int64_t value)
{
	return cvmx_read64_int64(__cvmx_fau_atomic_address(0, reg, value));
}

/**
 * Perform an atomic 32 bit add
 *
 * @reg:     FAU atomic register to access. 0 <= reg < 2048.
 *		  - Step by 4 for 32 bit access.
 * @value:   Signed value to add.
 *		  Note: Only the low 22 bits are available.
 * Returns Value of the register before the update
 */
static inline int32_t cvmx_fau_fetch_and_add32(cvmx_fau_reg_32_t reg,
					       int32_t value)
{
	reg ^= SWIZZLE_32;
	return cvmx_read64_int32(__cvmx_fau_atomic_address(0, reg, value));
}

/**
 * Perform an atomic 16 bit add
 *
 * @reg:     FAU atomic register to access. 0 <= reg < 2048.
 *		  - Step by 2 for 16 bit access.
 * @value:   Signed value to add.
 * Returns Value of the register before the update
 */
static inline int16_t cvmx_fau_fetch_and_add16(cvmx_fau_reg_16_t reg,
					       int16_t value)
{
	reg ^= SWIZZLE_16;
	return cvmx_read64_int16(__cvmx_fau_atomic_address(0, reg, value));
}

/**
 * Perform an atomic 8 bit add
 *
 * @reg:     FAU atomic register to access. 0 <= reg < 2048.
 * @value:   Signed value to add.
 * Returns Value of the register before the update
 */
static inline int8_t cvmx_fau_fetch_and_add8(cvmx_fau_reg_8_t reg, int8_t value)
{
	reg ^= SWIZZLE_8;
	return cvmx_read64_int8(__cvmx_fau_atomic_address(0, reg, value));
}

/**
 * Perform an atomic 64 bit add after the current tag switch
 * completes
 *
 * @reg:    FAU atomic register to access. 0 <= reg < 2048.
 *		 - Step by 8 for 64 bit access.
 * @value:  Signed value to add.
 *		 Note: Only the low 22 bits are available.
 * Returns If a timeout occurs, the error bit will be set. Otherwise
 *	   the value of the register before the update will be
 *	   returned
 */
static inline cvmx_fau_tagwait64_t
cvmx_fau_tagwait_fetch_and_add64(cvmx_fau_reg_64_t reg, int64_t value)
{
	union {
		uint64_t i64;
		cvmx_fau_tagwait64_t t;
	} result;
	result.i64 =
	    cvmx_read64_int64(__cvmx_fau_atomic_address(1, reg, value));
	return result.t;
}

/**
 * Perform an atomic 32 bit add after the current tag switch
 * completes
 *
 * @reg:    FAU atomic register to access. 0 <= reg < 2048.
 *		 - Step by 4 for 32 bit access.
 * @value:  Signed value to add.
 *		 Note: Only the low 22 bits are available.
 * Returns If a timeout occurs, the error bit will be set. Otherwise
 *	   the value of the register before the update will be
 *	   returned
 */
static inline cvmx_fau_tagwait32_t
cvmx_fau_tagwait_fetch_and_add32(cvmx_fau_reg_32_t reg, int32_t value)
{
	union {
		uint64_t i32;
		cvmx_fau_tagwait32_t t;
	} result;
	reg ^= SWIZZLE_32;
	result.i32 =
	    cvmx_read64_int32(__cvmx_fau_atomic_address(1, reg, value));
	return result.t;
}

/**
 * Perform an atomic 16 bit add after the current tag switch
 * completes
 *
 * @reg:    FAU atomic register to access. 0 <= reg < 2048.
 *		 - Step by 2 for 16 bit access.
 * @value:  Signed value to add.
 * Returns If a timeout occurs, the error bit will be set. Otherwise
 *	   the value of the register before the update will be
 *	   returned
 */
static inline cvmx_fau_tagwait16_t
cvmx_fau_tagwait_fetch_and_add16(cvmx_fau_reg_16_t reg, int16_t value)
{
	union {
		uint64_t i16;
		cvmx_fau_tagwait16_t t;
	} result;
	reg ^= SWIZZLE_16;
	result.i16 =
	    cvmx_read64_int16(__cvmx_fau_atomic_address(1, reg, value));
	return result.t;
}

/**
 * Perform an atomic 8 bit add after the current tag switch
 * completes
 *
 * @reg:    FAU atomic register to access. 0 <= reg < 2048.
 * @value:  Signed value to add.
 * Returns If a timeout occurs, the error bit will be set. Otherwise
 *	   the value of the register before the update will be
 *	   returned
 */
static inline cvmx_fau_tagwait8_t
cvmx_fau_tagwait_fetch_and_add8(cvmx_fau_reg_8_t reg, int8_t value)
{
	union {
		uint64_t i8;
		cvmx_fau_tagwait8_t t;
	} result;
	reg ^= SWIZZLE_8;
	result.i8 = cvmx_read64_int8(__cvmx_fau_atomic_address(1, reg, value));
	return result.t;
}

/**
 * Builds I/O data for async operations
 *
 * @scraddr: Scratch pad byte address to write to.  Must be 8 byte aligned
 * @value:   Signed value to add.
 *		  Note: When performing 32 and 64 bit access, only the low
 *		  22 bits are available.
 * @tagwait: Should the atomic add wait for the current tag switch
 *		  operation to complete.
 *		  - 0 = Don't wait
 *		  - 1 = Wait for tag switch to complete
 * @size:    The size of the operation:
 *		  - CVMX_FAU_OP_SIZE_8	(0) = 8 bits
 *		  - CVMX_FAU_OP_SIZE_16 (1) = 16 bits
 *		  - CVMX_FAU_OP_SIZE_32 (2) = 32 bits
 *		  - CVMX_FAU_OP_SIZE_64 (3) = 64 bits
 * @reg:     FAU atomic register to access. 0 <= reg < 2048.
 *		  - Step by 2 for 16 bit access.
 *		  - Step by 4 for 32 bit access.
 *		  - Step by 8 for 64 bit access.
 * Returns Data to write using cvmx_send_single
 */
static inline uint64_t __cvmx_fau_iobdma_data(uint64_t scraddr, int64_t value,
					      uint64_t tagwait,
					      cvmx_fau_op_size_t size,
					      uint64_t reg)
{
	return CVMX_FAU_LOAD_IO_ADDRESS |
	       cvmx_build_bits(CVMX_FAU_BITS_SCRADDR, scraddr >> 3) |
	       cvmx_build_bits(CVMX_FAU_BITS_LEN, 1) |
	       cvmx_build_bits(CVMX_FAU_BITS_INEVAL, value) |
	       cvmx_build_bits(CVMX_FAU_BITS_TAGWAIT, tagwait) |
	       cvmx_build_bits(CVMX_FAU_BITS_SIZE, size) |
	       cvmx_build_bits(CVMX_FAU_BITS_REGISTER, reg);
}

/**
 * Perform an async atomic 64 bit add. The old value is
 * placed in the scratch memory at byte address scraddr.
 *
 * @scraddr: Scratch memory byte address to put response in.
 *		  Must be 8 byte aligned.
 * @reg:     FAU atomic register to access. 0 <= reg < 2048.
 *		  - Step by 8 for 64 bit access.
 * @value:   Signed value to add.
 *		  Note: Only the low 22 bits are available.
 * Returns Placed in the scratch pad register
 */
static inline void cvmx_fau_async_fetch_and_add64(uint64_t scraddr,
						  cvmx_fau_reg_64_t reg,
						  int64_t value)
{
	cvmx_send_single(__cvmx_fau_iobdma_data
			 (scraddr, value, 0, CVMX_FAU_OP_SIZE_64, reg));
}

/**
 * Perform an async atomic 32 bit add. The old value is
 * placed in the scratch memory at byte address scraddr.
 *
 * @scraddr: Scratch memory byte address to put response in.
 *		  Must be 8 byte aligned.
 * @reg:     FAU atomic register to access. 0 <= reg < 2048.
 *		  - Step by 4 for 32 bit access.
 * @value:   Signed value to add.
 *		  Note: Only the low 22 bits are available.
 * Returns Placed in the scratch pad register
 */
static inline void cvmx_fau_async_fetch_and_add32(uint64_t scraddr,
						  cvmx_fau_reg_32_t reg,
						  int32_t value)
{
	cvmx_send_single(__cvmx_fau_iobdma_data
			 (scraddr, value, 0, CVMX_FAU_OP_SIZE_32, reg));
}

/**
 * Perform an async atomic 16 bit add. The old value is
 * placed in the scratch memory at byte address scraddr.
 *
 * @scraddr: Scratch memory byte address to put response in.
 *		  Must be 8 byte aligned.
 * @reg:     FAU atomic register to access. 0 <= reg < 2048.
 *		  - Step by 2 for 16 bit access.
 * @value:   Signed value to add.
 * Returns Placed in the scratch pad register
 */
static inline void cvmx_fau_async_fetch_and_add16(uint64_t scraddr,
						  cvmx_fau_reg_16_t reg,
						  int16_t value)
{
	cvmx_send_single(__cvmx_fau_iobdma_data
			 (scraddr, value, 0, CVMX_FAU_OP_SIZE_16, reg));
}

/**
 * Perform an async atomic 8 bit add. The old value is
 * placed in the scratch memory at byte address scraddr.
 *
 * @scraddr: Scratch memory byte address to put response in.
 *		  Must be 8 byte aligned.
 * @reg:     FAU atomic register to access. 0 <= reg < 2048.
 * @value:   Signed value to add.
 * Returns Placed in the scratch pad register
 */
static inline void cvmx_fau_async_fetch_and_add8(uint64_t scraddr,
						 cvmx_fau_reg_8_t reg,
						 int8_t value)
{
	cvmx_send_single(__cvmx_fau_iobdma_data
			 (scraddr, value, 0, CVMX_FAU_OP_SIZE_8, reg));
}

/**
 * Perform an async atomic 64 bit add after the current tag
 * switch completes.
 *
 * @scraddr: Scratch memory byte address to put response in.  Must be
 *	     8 byte aligned.  If a timeout occurs, the error bit (63)
 *	     will be set. Otherwise the value of the register before
 *	     the update will be returned
 *
 * @reg:     FAU atomic register to access. 0 <= reg < 2048.
 *		  - Step by 8 for 64 bit access.
 * @value:   Signed value to add.
 *		  Note: Only the low 22 bits are available.
 * Returns Placed in the scratch pad register
 */
static inline void cvmx_fau_async_tagwait_fetch_and_add64(uint64_t scraddr,
							  cvmx_fau_reg_64_t reg,
							  int64_t value)
{
	cvmx_send_single(__cvmx_fau_iobdma_data
			 (scraddr, value, 1, CVMX_FAU_OP_SIZE_64, reg));
}

/**
 * Perform an async atomic 32 bit add after the current tag
 * switch completes.
 *
 * @scraddr: Scratch memory byte address to put response in.  Must be
 *	     8 byte aligned.  If a timeout occurs, the error bit (63)
 *	     will be set. Otherwise the value of the register before
 *	     the update will be returned
 *
 * @reg:     FAU atomic register to access. 0 <= reg < 2048.
 *		  - Step by 4 for 32 bit access.
 * @value:   Signed value to add.
 *		  Note: Only the low 22 bits are available.
 * Returns Placed in the scratch pad register
 */
static inline void cvmx_fau_async_tagwait_fetch_and_add32(uint64_t scraddr,
							  cvmx_fau_reg_32_t reg,
							  int32_t value)
{
	cvmx_send_single(__cvmx_fau_iobdma_data
			 (scraddr, value, 1, CVMX_FAU_OP_SIZE_32, reg));
}

/**
 * Perform an async atomic 16 bit add after the current tag
 * switch completes.
 *
 * @scraddr: Scratch memory byte address to put response in.  Must be
 *	     8 byte aligned.  If a timeout occurs, the error bit (63)
 *	     will be set. Otherwise the value of the register before
 *	     the update will be returned
 *
 * @reg:     FAU atomic register to access. 0 <= reg < 2048.
 *		  - Step by 2 for 16 bit access.
 * @value:   Signed value to add.
 *
 * Returns Placed in the scratch pad register
 */
static inline void cvmx_fau_async_tagwait_fetch_and_add16(uint64_t scraddr,
							  cvmx_fau_reg_16_t reg,
							  int16_t value)
{
	cvmx_send_single(__cvmx_fau_iobdma_data
			 (scraddr, value, 1, CVMX_FAU_OP_SIZE_16, reg));
}

/**
 * Perform an async atomic 8 bit add after the current tag
 * switch completes.
 *
 * @scraddr: Scratch memory byte address to put response in.  Must be
 *	     8 byte aligned.  If a timeout occurs, the error bit (63)
 *	     will be set. Otherwise the value of the register before
 *	     the update will be returned
 *
 * @reg:     FAU atomic register to access. 0 <= reg < 2048.
 * @value:   Signed value to add.
 *
 * Returns Placed in the scratch pad register
 */
static inline void cvmx_fau_async_tagwait_fetch_and_add8(uint64_t scraddr,
							 cvmx_fau_reg_8_t reg,
							 int8_t value)
{
	cvmx_send_single(__cvmx_fau_iobdma_data
			 (scraddr, value, 1, CVMX_FAU_OP_SIZE_8, reg));
}

/**
 * Perform an atomic 64 bit add
 *
 * @reg:     FAU atomic register to access. 0 <= reg < 2048.
 *		  - Step by 8 for 64 bit access.
 * @value:   Signed value to add.
 */
static inline void cvmx_fau_atomic_add64(cvmx_fau_reg_64_t reg, int64_t value)
{
	cvmx_write64_int64(__cvmx_fau_store_address(0, reg), value);
}

/**
 * Perform an atomic 32 bit add
 *
 * @reg:     FAU atomic register to access. 0 <= reg < 2048.
 *		  - Step by 4 for 32 bit access.
 * @value:   Signed value to add.
 */
static inline void cvmx_fau_atomic_add32(cvmx_fau_reg_32_t reg, int32_t value)
{
	reg ^= SWIZZLE_32;
	cvmx_write64_int32(__cvmx_fau_store_address(0, reg), value);
}

/**
 * Perform an atomic 16 bit add
 *
 * @reg:     FAU atomic register to access. 0 <= reg < 2048.
 *		  - Step by 2 for 16 bit access.
 * @value:   Signed value to add.
 */
static inline void cvmx_fau_atomic_add16(cvmx_fau_reg_16_t reg, int16_t value)
{
	reg ^= SWIZZLE_16;
	cvmx_write64_int16(__cvmx_fau_store_address(0, reg), value);
}

/**
 * Perform an atomic 8 bit add
 *
 * @reg:     FAU atomic register to access. 0 <= reg < 2048.
 * @value:   Signed value to add.
 */
static inline void cvmx_fau_atomic_add8(cvmx_fau_reg_8_t reg, int8_t value)
{
	reg ^= SWIZZLE_8;
	cvmx_write64_int8(__cvmx_fau_store_address(0, reg), value);
}

/**
 * Perform an atomic 64 bit write
 *
 * @reg:     FAU atomic register to access. 0 <= reg < 2048.
 *		  - Step by 8 for 64 bit access.
 * @value:   Signed value to write.
 */
static inline void cvmx_fau_atomic_write64(cvmx_fau_reg_64_t reg, int64_t value)
{
	cvmx_write64_int64(__cvmx_fau_store_address(1, reg), value);
}

/**
 * Perform an atomic 32 bit write
 *
 * @reg:     FAU atomic register to access. 0 <= reg < 2048.
 *		  - Step by 4 for 32 bit access.
 * @value:   Signed value to write.
 */
static inline void cvmx_fau_atomic_write32(cvmx_fau_reg_32_t reg, int32_t value)
{
	reg ^= SWIZZLE_32;
	cvmx_write64_int32(__cvmx_fau_store_address(1, reg), value);
}

/**
 * Perform an atomic 16 bit write
 *
 * @reg:     FAU atomic register to access. 0 <= reg < 2048.
 *		  - Step by 2 for 16 bit access.
 * @value:   Signed value to write.
 */
static inline void cvmx_fau_atomic_write16(cvmx_fau_reg_16_t reg, int16_t value)
{
	reg ^= SWIZZLE_16;
	cvmx_write64_int16(__cvmx_fau_store_address(1, reg), value);
}

/**
 * Perform an atomic 8 bit write
 *
 * @reg:     FAU atomic register to access. 0 <= reg < 2048.
 * @value:   Signed value to write.
 */
static inline void cvmx_fau_atomic_write8(cvmx_fau_reg_8_t reg, int8_t value)
{
	reg ^= SWIZZLE_8;
	cvmx_write64_int8(__cvmx_fau_store_address(1, reg), value);
}

#endif /* __CVMX_FAU_H__ */

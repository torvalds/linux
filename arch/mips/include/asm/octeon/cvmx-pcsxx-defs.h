/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2012 Cavium Networks
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

#ifndef __CVMX_PCSXX_DEFS_H__
#define __CVMX_PCSXX_DEFS_H__

static inline uint64_t CVMX_PCSXX_10GBX_STATUS_REG(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000828ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000828ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000828ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0000828ull) + (block_id) * 0x1000000ull;
}

static inline uint64_t CVMX_PCSXX_BIST_STATUS_REG(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000870ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000870ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000870ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0000870ull) + (block_id) * 0x1000000ull;
}

static inline uint64_t CVMX_PCSXX_BIT_LOCK_STATUS_REG(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000850ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000850ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000850ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0000850ull) + (block_id) * 0x1000000ull;
}

static inline uint64_t CVMX_PCSXX_CONTROL1_REG(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000800ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000800ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000800ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0000800ull) + (block_id) * 0x1000000ull;
}

static inline uint64_t CVMX_PCSXX_CONTROL2_REG(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000818ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000818ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000818ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0000818ull) + (block_id) * 0x1000000ull;
}

static inline uint64_t CVMX_PCSXX_INT_EN_REG(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000860ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000860ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000860ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0000860ull) + (block_id) * 0x1000000ull;
}

static inline uint64_t CVMX_PCSXX_INT_REG(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000858ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000858ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000858ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0000858ull) + (block_id) * 0x1000000ull;
}

static inline uint64_t CVMX_PCSXX_LOG_ANL_REG(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000868ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000868ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000868ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0000868ull) + (block_id) * 0x1000000ull;
}

static inline uint64_t CVMX_PCSXX_MISC_CTL_REG(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000848ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000848ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000848ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0000848ull) + (block_id) * 0x1000000ull;
}

static inline uint64_t CVMX_PCSXX_RX_SYNC_STATES_REG(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000838ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000838ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000838ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0000838ull) + (block_id) * 0x1000000ull;
}

static inline uint64_t CVMX_PCSXX_SPD_ABIL_REG(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000810ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000810ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000810ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0000810ull) + (block_id) * 0x1000000ull;
}

static inline uint64_t CVMX_PCSXX_STATUS1_REG(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000808ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000808ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000808ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0000808ull) + (block_id) * 0x1000000ull;
}

static inline uint64_t CVMX_PCSXX_STATUS2_REG(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000820ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000820ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000820ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0000820ull) + (block_id) * 0x1000000ull;
}

static inline uint64_t CVMX_PCSXX_TX_RX_POLARITY_REG(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000840ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000840ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000840ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0000840ull) + (block_id) * 0x1000000ull;
}

static inline uint64_t CVMX_PCSXX_TX_RX_STATES_REG(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000830ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000830ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0000830ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0000830ull) + (block_id) * 0x1000000ull;
}

union cvmx_pcsxx_10gbx_status_reg {
	uint64_t u64;
	struct cvmx_pcsxx_10gbx_status_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_13_63:51;
		uint64_t alignd:1;
		uint64_t pattst:1;
		uint64_t reserved_4_10:7;
		uint64_t l3sync:1;
		uint64_t l2sync:1;
		uint64_t l1sync:1;
		uint64_t l0sync:1;
#else
		uint64_t l0sync:1;
		uint64_t l1sync:1;
		uint64_t l2sync:1;
		uint64_t l3sync:1;
		uint64_t reserved_4_10:7;
		uint64_t pattst:1;
		uint64_t alignd:1;
		uint64_t reserved_13_63:51;
#endif
	} s;
	struct cvmx_pcsxx_10gbx_status_reg_s cn52xx;
	struct cvmx_pcsxx_10gbx_status_reg_s cn52xxp1;
	struct cvmx_pcsxx_10gbx_status_reg_s cn56xx;
	struct cvmx_pcsxx_10gbx_status_reg_s cn56xxp1;
	struct cvmx_pcsxx_10gbx_status_reg_s cn61xx;
	struct cvmx_pcsxx_10gbx_status_reg_s cn63xx;
	struct cvmx_pcsxx_10gbx_status_reg_s cn63xxp1;
	struct cvmx_pcsxx_10gbx_status_reg_s cn66xx;
	struct cvmx_pcsxx_10gbx_status_reg_s cn68xx;
	struct cvmx_pcsxx_10gbx_status_reg_s cn68xxp1;
};

union cvmx_pcsxx_bist_status_reg {
	uint64_t u64;
	struct cvmx_pcsxx_bist_status_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t bist_status:1;
#else
		uint64_t bist_status:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_pcsxx_bist_status_reg_s cn52xx;
	struct cvmx_pcsxx_bist_status_reg_s cn52xxp1;
	struct cvmx_pcsxx_bist_status_reg_s cn56xx;
	struct cvmx_pcsxx_bist_status_reg_s cn56xxp1;
	struct cvmx_pcsxx_bist_status_reg_s cn61xx;
	struct cvmx_pcsxx_bist_status_reg_s cn63xx;
	struct cvmx_pcsxx_bist_status_reg_s cn63xxp1;
	struct cvmx_pcsxx_bist_status_reg_s cn66xx;
	struct cvmx_pcsxx_bist_status_reg_s cn68xx;
	struct cvmx_pcsxx_bist_status_reg_s cn68xxp1;
};

union cvmx_pcsxx_bit_lock_status_reg {
	uint64_t u64;
	struct cvmx_pcsxx_bit_lock_status_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t bitlck3:1;
		uint64_t bitlck2:1;
		uint64_t bitlck1:1;
		uint64_t bitlck0:1;
#else
		uint64_t bitlck0:1;
		uint64_t bitlck1:1;
		uint64_t bitlck2:1;
		uint64_t bitlck3:1;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_pcsxx_bit_lock_status_reg_s cn52xx;
	struct cvmx_pcsxx_bit_lock_status_reg_s cn52xxp1;
	struct cvmx_pcsxx_bit_lock_status_reg_s cn56xx;
	struct cvmx_pcsxx_bit_lock_status_reg_s cn56xxp1;
	struct cvmx_pcsxx_bit_lock_status_reg_s cn61xx;
	struct cvmx_pcsxx_bit_lock_status_reg_s cn63xx;
	struct cvmx_pcsxx_bit_lock_status_reg_s cn63xxp1;
	struct cvmx_pcsxx_bit_lock_status_reg_s cn66xx;
	struct cvmx_pcsxx_bit_lock_status_reg_s cn68xx;
	struct cvmx_pcsxx_bit_lock_status_reg_s cn68xxp1;
};

union cvmx_pcsxx_control1_reg {
	uint64_t u64;
	struct cvmx_pcsxx_control1_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t reset:1;
		uint64_t loopbck1:1;
		uint64_t spdsel1:1;
		uint64_t reserved_12_12:1;
		uint64_t lo_pwr:1;
		uint64_t reserved_7_10:4;
		uint64_t spdsel0:1;
		uint64_t spd:4;
		uint64_t reserved_0_1:2;
#else
		uint64_t reserved_0_1:2;
		uint64_t spd:4;
		uint64_t spdsel0:1;
		uint64_t reserved_7_10:4;
		uint64_t lo_pwr:1;
		uint64_t reserved_12_12:1;
		uint64_t spdsel1:1;
		uint64_t loopbck1:1;
		uint64_t reset:1;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_pcsxx_control1_reg_s cn52xx;
	struct cvmx_pcsxx_control1_reg_s cn52xxp1;
	struct cvmx_pcsxx_control1_reg_s cn56xx;
	struct cvmx_pcsxx_control1_reg_s cn56xxp1;
	struct cvmx_pcsxx_control1_reg_s cn61xx;
	struct cvmx_pcsxx_control1_reg_s cn63xx;
	struct cvmx_pcsxx_control1_reg_s cn63xxp1;
	struct cvmx_pcsxx_control1_reg_s cn66xx;
	struct cvmx_pcsxx_control1_reg_s cn68xx;
	struct cvmx_pcsxx_control1_reg_s cn68xxp1;
};

union cvmx_pcsxx_control2_reg {
	uint64_t u64;
	struct cvmx_pcsxx_control2_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t type:2;
#else
		uint64_t type:2;
		uint64_t reserved_2_63:62;
#endif
	} s;
	struct cvmx_pcsxx_control2_reg_s cn52xx;
	struct cvmx_pcsxx_control2_reg_s cn52xxp1;
	struct cvmx_pcsxx_control2_reg_s cn56xx;
	struct cvmx_pcsxx_control2_reg_s cn56xxp1;
	struct cvmx_pcsxx_control2_reg_s cn61xx;
	struct cvmx_pcsxx_control2_reg_s cn63xx;
	struct cvmx_pcsxx_control2_reg_s cn63xxp1;
	struct cvmx_pcsxx_control2_reg_s cn66xx;
	struct cvmx_pcsxx_control2_reg_s cn68xx;
	struct cvmx_pcsxx_control2_reg_s cn68xxp1;
};

union cvmx_pcsxx_int_en_reg {
	uint64_t u64;
	struct cvmx_pcsxx_int_en_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t dbg_sync_en:1;
		uint64_t algnlos_en:1;
		uint64_t synlos_en:1;
		uint64_t bitlckls_en:1;
		uint64_t rxsynbad_en:1;
		uint64_t rxbad_en:1;
		uint64_t txflt_en:1;
#else
		uint64_t txflt_en:1;
		uint64_t rxbad_en:1;
		uint64_t rxsynbad_en:1;
		uint64_t bitlckls_en:1;
		uint64_t synlos_en:1;
		uint64_t algnlos_en:1;
		uint64_t dbg_sync_en:1;
		uint64_t reserved_7_63:57;
#endif
	} s;
	struct cvmx_pcsxx_int_en_reg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t algnlos_en:1;
		uint64_t synlos_en:1;
		uint64_t bitlckls_en:1;
		uint64_t rxsynbad_en:1;
		uint64_t rxbad_en:1;
		uint64_t txflt_en:1;
#else
		uint64_t txflt_en:1;
		uint64_t rxbad_en:1;
		uint64_t rxsynbad_en:1;
		uint64_t bitlckls_en:1;
		uint64_t synlos_en:1;
		uint64_t algnlos_en:1;
		uint64_t reserved_6_63:58;
#endif
	} cn52xx;
	struct cvmx_pcsxx_int_en_reg_cn52xx cn52xxp1;
	struct cvmx_pcsxx_int_en_reg_cn52xx cn56xx;
	struct cvmx_pcsxx_int_en_reg_cn52xx cn56xxp1;
	struct cvmx_pcsxx_int_en_reg_s cn61xx;
	struct cvmx_pcsxx_int_en_reg_s cn63xx;
	struct cvmx_pcsxx_int_en_reg_s cn63xxp1;
	struct cvmx_pcsxx_int_en_reg_s cn66xx;
	struct cvmx_pcsxx_int_en_reg_s cn68xx;
	struct cvmx_pcsxx_int_en_reg_s cn68xxp1;
};

union cvmx_pcsxx_int_reg {
	uint64_t u64;
	struct cvmx_pcsxx_int_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t dbg_sync:1;
		uint64_t algnlos:1;
		uint64_t synlos:1;
		uint64_t bitlckls:1;
		uint64_t rxsynbad:1;
		uint64_t rxbad:1;
		uint64_t txflt:1;
#else
		uint64_t txflt:1;
		uint64_t rxbad:1;
		uint64_t rxsynbad:1;
		uint64_t bitlckls:1;
		uint64_t synlos:1;
		uint64_t algnlos:1;
		uint64_t dbg_sync:1;
		uint64_t reserved_7_63:57;
#endif
	} s;
	struct cvmx_pcsxx_int_reg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t algnlos:1;
		uint64_t synlos:1;
		uint64_t bitlckls:1;
		uint64_t rxsynbad:1;
		uint64_t rxbad:1;
		uint64_t txflt:1;
#else
		uint64_t txflt:1;
		uint64_t rxbad:1;
		uint64_t rxsynbad:1;
		uint64_t bitlckls:1;
		uint64_t synlos:1;
		uint64_t algnlos:1;
		uint64_t reserved_6_63:58;
#endif
	} cn52xx;
	struct cvmx_pcsxx_int_reg_cn52xx cn52xxp1;
	struct cvmx_pcsxx_int_reg_cn52xx cn56xx;
	struct cvmx_pcsxx_int_reg_cn52xx cn56xxp1;
	struct cvmx_pcsxx_int_reg_s cn61xx;
	struct cvmx_pcsxx_int_reg_s cn63xx;
	struct cvmx_pcsxx_int_reg_s cn63xxp1;
	struct cvmx_pcsxx_int_reg_s cn66xx;
	struct cvmx_pcsxx_int_reg_s cn68xx;
	struct cvmx_pcsxx_int_reg_s cn68xxp1;
};

union cvmx_pcsxx_log_anl_reg {
	uint64_t u64;
	struct cvmx_pcsxx_log_anl_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t enc_mode:1;
		uint64_t drop_ln:2;
		uint64_t lafifovfl:1;
		uint64_t la_en:1;
		uint64_t pkt_sz:2;
#else
		uint64_t pkt_sz:2;
		uint64_t la_en:1;
		uint64_t lafifovfl:1;
		uint64_t drop_ln:2;
		uint64_t enc_mode:1;
		uint64_t reserved_7_63:57;
#endif
	} s;
	struct cvmx_pcsxx_log_anl_reg_s cn52xx;
	struct cvmx_pcsxx_log_anl_reg_s cn52xxp1;
	struct cvmx_pcsxx_log_anl_reg_s cn56xx;
	struct cvmx_pcsxx_log_anl_reg_s cn56xxp1;
	struct cvmx_pcsxx_log_anl_reg_s cn61xx;
	struct cvmx_pcsxx_log_anl_reg_s cn63xx;
	struct cvmx_pcsxx_log_anl_reg_s cn63xxp1;
	struct cvmx_pcsxx_log_anl_reg_s cn66xx;
	struct cvmx_pcsxx_log_anl_reg_s cn68xx;
	struct cvmx_pcsxx_log_anl_reg_s cn68xxp1;
};

union cvmx_pcsxx_misc_ctl_reg {
	uint64_t u64;
	struct cvmx_pcsxx_misc_ctl_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t tx_swap:1;
		uint64_t rx_swap:1;
		uint64_t xaui:1;
		uint64_t gmxeno:1;
#else
		uint64_t gmxeno:1;
		uint64_t xaui:1;
		uint64_t rx_swap:1;
		uint64_t tx_swap:1;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_pcsxx_misc_ctl_reg_s cn52xx;
	struct cvmx_pcsxx_misc_ctl_reg_s cn52xxp1;
	struct cvmx_pcsxx_misc_ctl_reg_s cn56xx;
	struct cvmx_pcsxx_misc_ctl_reg_s cn56xxp1;
	struct cvmx_pcsxx_misc_ctl_reg_s cn61xx;
	struct cvmx_pcsxx_misc_ctl_reg_s cn63xx;
	struct cvmx_pcsxx_misc_ctl_reg_s cn63xxp1;
	struct cvmx_pcsxx_misc_ctl_reg_s cn66xx;
	struct cvmx_pcsxx_misc_ctl_reg_s cn68xx;
	struct cvmx_pcsxx_misc_ctl_reg_s cn68xxp1;
};

union cvmx_pcsxx_rx_sync_states_reg {
	uint64_t u64;
	struct cvmx_pcsxx_rx_sync_states_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t sync3st:4;
		uint64_t sync2st:4;
		uint64_t sync1st:4;
		uint64_t sync0st:4;
#else
		uint64_t sync0st:4;
		uint64_t sync1st:4;
		uint64_t sync2st:4;
		uint64_t sync3st:4;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_pcsxx_rx_sync_states_reg_s cn52xx;
	struct cvmx_pcsxx_rx_sync_states_reg_s cn52xxp1;
	struct cvmx_pcsxx_rx_sync_states_reg_s cn56xx;
	struct cvmx_pcsxx_rx_sync_states_reg_s cn56xxp1;
	struct cvmx_pcsxx_rx_sync_states_reg_s cn61xx;
	struct cvmx_pcsxx_rx_sync_states_reg_s cn63xx;
	struct cvmx_pcsxx_rx_sync_states_reg_s cn63xxp1;
	struct cvmx_pcsxx_rx_sync_states_reg_s cn66xx;
	struct cvmx_pcsxx_rx_sync_states_reg_s cn68xx;
	struct cvmx_pcsxx_rx_sync_states_reg_s cn68xxp1;
};

union cvmx_pcsxx_spd_abil_reg {
	uint64_t u64;
	struct cvmx_pcsxx_spd_abil_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t tenpasst:1;
		uint64_t tengb:1;
#else
		uint64_t tengb:1;
		uint64_t tenpasst:1;
		uint64_t reserved_2_63:62;
#endif
	} s;
	struct cvmx_pcsxx_spd_abil_reg_s cn52xx;
	struct cvmx_pcsxx_spd_abil_reg_s cn52xxp1;
	struct cvmx_pcsxx_spd_abil_reg_s cn56xx;
	struct cvmx_pcsxx_spd_abil_reg_s cn56xxp1;
	struct cvmx_pcsxx_spd_abil_reg_s cn61xx;
	struct cvmx_pcsxx_spd_abil_reg_s cn63xx;
	struct cvmx_pcsxx_spd_abil_reg_s cn63xxp1;
	struct cvmx_pcsxx_spd_abil_reg_s cn66xx;
	struct cvmx_pcsxx_spd_abil_reg_s cn68xx;
	struct cvmx_pcsxx_spd_abil_reg_s cn68xxp1;
};

union cvmx_pcsxx_status1_reg {
	uint64_t u64;
	struct cvmx_pcsxx_status1_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t flt:1;
		uint64_t reserved_3_6:4;
		uint64_t rcv_lnk:1;
		uint64_t lpable:1;
		uint64_t reserved_0_0:1;
#else
		uint64_t reserved_0_0:1;
		uint64_t lpable:1;
		uint64_t rcv_lnk:1;
		uint64_t reserved_3_6:4;
		uint64_t flt:1;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_pcsxx_status1_reg_s cn52xx;
	struct cvmx_pcsxx_status1_reg_s cn52xxp1;
	struct cvmx_pcsxx_status1_reg_s cn56xx;
	struct cvmx_pcsxx_status1_reg_s cn56xxp1;
	struct cvmx_pcsxx_status1_reg_s cn61xx;
	struct cvmx_pcsxx_status1_reg_s cn63xx;
	struct cvmx_pcsxx_status1_reg_s cn63xxp1;
	struct cvmx_pcsxx_status1_reg_s cn66xx;
	struct cvmx_pcsxx_status1_reg_s cn68xx;
	struct cvmx_pcsxx_status1_reg_s cn68xxp1;
};

union cvmx_pcsxx_status2_reg {
	uint64_t u64;
	struct cvmx_pcsxx_status2_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t dev:2;
		uint64_t reserved_12_13:2;
		uint64_t xmtflt:1;
		uint64_t rcvflt:1;
		uint64_t reserved_3_9:7;
		uint64_t tengb_w:1;
		uint64_t tengb_x:1;
		uint64_t tengb_r:1;
#else
		uint64_t tengb_r:1;
		uint64_t tengb_x:1;
		uint64_t tengb_w:1;
		uint64_t reserved_3_9:7;
		uint64_t rcvflt:1;
		uint64_t xmtflt:1;
		uint64_t reserved_12_13:2;
		uint64_t dev:2;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_pcsxx_status2_reg_s cn52xx;
	struct cvmx_pcsxx_status2_reg_s cn52xxp1;
	struct cvmx_pcsxx_status2_reg_s cn56xx;
	struct cvmx_pcsxx_status2_reg_s cn56xxp1;
	struct cvmx_pcsxx_status2_reg_s cn61xx;
	struct cvmx_pcsxx_status2_reg_s cn63xx;
	struct cvmx_pcsxx_status2_reg_s cn63xxp1;
	struct cvmx_pcsxx_status2_reg_s cn66xx;
	struct cvmx_pcsxx_status2_reg_s cn68xx;
	struct cvmx_pcsxx_status2_reg_s cn68xxp1;
};

union cvmx_pcsxx_tx_rx_polarity_reg {
	uint64_t u64;
	struct cvmx_pcsxx_tx_rx_polarity_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t xor_rxplrt:4;
		uint64_t xor_txplrt:4;
		uint64_t rxplrt:1;
		uint64_t txplrt:1;
#else
		uint64_t txplrt:1;
		uint64_t rxplrt:1;
		uint64_t xor_txplrt:4;
		uint64_t xor_rxplrt:4;
		uint64_t reserved_10_63:54;
#endif
	} s;
	struct cvmx_pcsxx_tx_rx_polarity_reg_s cn52xx;
	struct cvmx_pcsxx_tx_rx_polarity_reg_cn52xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t rxplrt:1;
		uint64_t txplrt:1;
#else
		uint64_t txplrt:1;
		uint64_t rxplrt:1;
		uint64_t reserved_2_63:62;
#endif
	} cn52xxp1;
	struct cvmx_pcsxx_tx_rx_polarity_reg_s cn56xx;
	struct cvmx_pcsxx_tx_rx_polarity_reg_cn52xxp1 cn56xxp1;
	struct cvmx_pcsxx_tx_rx_polarity_reg_s cn61xx;
	struct cvmx_pcsxx_tx_rx_polarity_reg_s cn63xx;
	struct cvmx_pcsxx_tx_rx_polarity_reg_s cn63xxp1;
	struct cvmx_pcsxx_tx_rx_polarity_reg_s cn66xx;
	struct cvmx_pcsxx_tx_rx_polarity_reg_s cn68xx;
	struct cvmx_pcsxx_tx_rx_polarity_reg_s cn68xxp1;
};

union cvmx_pcsxx_tx_rx_states_reg {
	uint64_t u64;
	struct cvmx_pcsxx_tx_rx_states_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_14_63:50;
		uint64_t term_err:1;
		uint64_t syn3bad:1;
		uint64_t syn2bad:1;
		uint64_t syn1bad:1;
		uint64_t syn0bad:1;
		uint64_t rxbad:1;
		uint64_t algn_st:3;
		uint64_t rx_st:2;
		uint64_t tx_st:3;
#else
		uint64_t tx_st:3;
		uint64_t rx_st:2;
		uint64_t algn_st:3;
		uint64_t rxbad:1;
		uint64_t syn0bad:1;
		uint64_t syn1bad:1;
		uint64_t syn2bad:1;
		uint64_t syn3bad:1;
		uint64_t term_err:1;
		uint64_t reserved_14_63:50;
#endif
	} s;
	struct cvmx_pcsxx_tx_rx_states_reg_s cn52xx;
	struct cvmx_pcsxx_tx_rx_states_reg_cn52xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_13_63:51;
		uint64_t syn3bad:1;
		uint64_t syn2bad:1;
		uint64_t syn1bad:1;
		uint64_t syn0bad:1;
		uint64_t rxbad:1;
		uint64_t algn_st:3;
		uint64_t rx_st:2;
		uint64_t tx_st:3;
#else
		uint64_t tx_st:3;
		uint64_t rx_st:2;
		uint64_t algn_st:3;
		uint64_t rxbad:1;
		uint64_t syn0bad:1;
		uint64_t syn1bad:1;
		uint64_t syn2bad:1;
		uint64_t syn3bad:1;
		uint64_t reserved_13_63:51;
#endif
	} cn52xxp1;
	struct cvmx_pcsxx_tx_rx_states_reg_s cn56xx;
	struct cvmx_pcsxx_tx_rx_states_reg_cn52xxp1 cn56xxp1;
	struct cvmx_pcsxx_tx_rx_states_reg_s cn61xx;
	struct cvmx_pcsxx_tx_rx_states_reg_s cn63xx;
	struct cvmx_pcsxx_tx_rx_states_reg_s cn63xxp1;
	struct cvmx_pcsxx_tx_rx_states_reg_s cn66xx;
	struct cvmx_pcsxx_tx_rx_states_reg_s cn68xx;
	struct cvmx_pcsxx_tx_rx_states_reg_s cn68xxp1;
};

#endif

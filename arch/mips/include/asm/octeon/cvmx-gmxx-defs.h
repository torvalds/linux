/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (C) 2003-2018 Cavium, Inc.
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

#ifndef __CVMX_GMXX_DEFS_H__
#define __CVMX_GMXX_DEFS_H__

static inline uint64_t CVMX_GMXX_HG2_CONTROL(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000550ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000550ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_INF_MODE(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080007F8ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800080007F8ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_PRTX_CFG(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000010ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000010ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000010ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_ADR_CAM0(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000180ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000180ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000180ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_ADR_CAM1(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000188ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000188ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000188ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_ADR_CAM2(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000190ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000190ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000190ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_ADR_CAM3(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000198ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000198ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000198ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_ADR_CAM4(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080001A0ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080001A0ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x00011800080001A0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_ADR_CAM5(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080001A8ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080001A8ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x00011800080001A8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_ADR_CAM_EN(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000108ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000108ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000108ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_ADR_CTL(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000100ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000100ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000100ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_FRM_CTL(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000018ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000018ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000018ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

#define CVMX_GMXX_RXX_FRM_MAX(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180008000030ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048)
#define CVMX_GMXX_RXX_FRM_MIN(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180008000028ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048)

static inline uint64_t CVMX_GMXX_RXX_INT_EN(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000008ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000008ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000008ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_INT_REG(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000000ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000000ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000000ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_JABBER(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000038ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000038ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000038ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

#define CVMX_GMXX_RXX_RX_INBND(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180008000060ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048)

static inline uint64_t CVMX_GMXX_RX_PRTS(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000410ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000410ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_RX_XAUI_CTL(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000530ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000530ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_SMACX(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000230ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000230ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000230ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TXX_BURST(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000228ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000228ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000228ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

#define CVMX_GMXX_TXX_CLK(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180008000208ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048)
static inline uint64_t CVMX_GMXX_TXX_CTL(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000270ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000270ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000270ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TXX_PAUSE_PKT_INTERVAL(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000248ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000248ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000248ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TXX_PAUSE_PKT_TIME(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000238ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000238ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000238ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TXX_SLOT(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000220ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000220ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000220ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TXX_THRESH(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000210ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000210ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000210ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TX_INT_EN(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000508ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000508ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_TX_INT_REG(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000500ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000500ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_TX_OVR_BP(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080004C8ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800080004C8ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_TX_PRTS(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000480ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000480ull) + (block_id) * 0x8000000ull;
}

#define CVMX_GMXX_TX_SPI_CTL(block_id) (CVMX_ADD_IO_SEG(0x00011800080004C0ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_GMXX_TX_SPI_MAX(block_id) (CVMX_ADD_IO_SEG(0x00011800080004B0ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_GMXX_TX_SPI_THRESH(block_id) (CVMX_ADD_IO_SEG(0x00011800080004B8ull) + ((block_id) & 1) * 0x8000000ull)
static inline uint64_t CVMX_GMXX_TX_XAUI_CTL(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000528ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000528ull) + (block_id) * 0x8000000ull;
}

void __cvmx_interrupt_gmxx_enable(int interface);

union cvmx_gmxx_hg2_control {
	uint64_t u64;
	struct cvmx_gmxx_hg2_control_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_19_63:45;
		uint64_t hg2tx_en:1;
		uint64_t hg2rx_en:1;
		uint64_t phys_en:1;
		uint64_t logl_en:16;
#else
		uint64_t logl_en:16;
		uint64_t phys_en:1;
		uint64_t hg2rx_en:1;
		uint64_t hg2tx_en:1;
		uint64_t reserved_19_63:45;
#endif
	} s;
};

union cvmx_gmxx_inf_mode {
	uint64_t u64;
	struct cvmx_gmxx_inf_mode_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t rate:4;
		uint64_t reserved_12_15:4;
		uint64_t speed:4;
		uint64_t reserved_7_7:1;
		uint64_t mode:3;
		uint64_t reserved_3_3:1;
		uint64_t p0mii:1;
		uint64_t en:1;
		uint64_t type:1;
#else
		uint64_t type:1;
		uint64_t en:1;
		uint64_t p0mii:1;
		uint64_t reserved_3_3:1;
		uint64_t mode:3;
		uint64_t reserved_7_7:1;
		uint64_t speed:4;
		uint64_t reserved_12_15:4;
		uint64_t rate:4;
		uint64_t reserved_20_63:44;
#endif
	} s;
	struct cvmx_gmxx_inf_mode_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t p0mii:1;
		uint64_t en:1;
		uint64_t type:1;
#else
		uint64_t type:1;
		uint64_t en:1;
		uint64_t p0mii:1;
		uint64_t reserved_3_63:61;
#endif
	} cn30xx;
	struct cvmx_gmxx_inf_mode_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t en:1;
		uint64_t type:1;
#else
		uint64_t type:1;
		uint64_t en:1;
		uint64_t reserved_2_63:62;
#endif
	} cn31xx;
	struct cvmx_gmxx_inf_mode_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t speed:2;
		uint64_t reserved_6_7:2;
		uint64_t mode:2;
		uint64_t reserved_2_3:2;
		uint64_t en:1;
		uint64_t type:1;
#else
		uint64_t type:1;
		uint64_t en:1;
		uint64_t reserved_2_3:2;
		uint64_t mode:2;
		uint64_t reserved_6_7:2;
		uint64_t speed:2;
		uint64_t reserved_10_63:54;
#endif
	} cn52xx;
	struct cvmx_gmxx_inf_mode_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t speed:4;
		uint64_t reserved_5_7:3;
		uint64_t mode:1;
		uint64_t reserved_2_3:2;
		uint64_t en:1;
		uint64_t type:1;
#else
		uint64_t type:1;
		uint64_t en:1;
		uint64_t reserved_2_3:2;
		uint64_t mode:1;
		uint64_t reserved_5_7:3;
		uint64_t speed:4;
		uint64_t reserved_12_63:52;
#endif
	} cn61xx;
	struct cvmx_gmxx_inf_mode_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t rate:4;
		uint64_t reserved_12_15:4;
		uint64_t speed:4;
		uint64_t reserved_5_7:3;
		uint64_t mode:1;
		uint64_t reserved_2_3:2;
		uint64_t en:1;
		uint64_t type:1;
#else
		uint64_t type:1;
		uint64_t en:1;
		uint64_t reserved_2_3:2;
		uint64_t mode:1;
		uint64_t reserved_5_7:3;
		uint64_t speed:4;
		uint64_t reserved_12_15:4;
		uint64_t rate:4;
		uint64_t reserved_20_63:44;
#endif
	} cn66xx;
	struct cvmx_gmxx_inf_mode_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t speed:4;
		uint64_t reserved_7_7:1;
		uint64_t mode:3;
		uint64_t reserved_2_3:2;
		uint64_t en:1;
		uint64_t type:1;
#else
		uint64_t type:1;
		uint64_t en:1;
		uint64_t reserved_2_3:2;
		uint64_t mode:3;
		uint64_t reserved_7_7:1;
		uint64_t speed:4;
		uint64_t reserved_12_63:52;
#endif
	} cn68xx;
};

union cvmx_gmxx_prtx_cfg {
	uint64_t u64;
	struct cvmx_gmxx_prtx_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_22_63:42;
		uint64_t pknd:6;
		uint64_t reserved_14_15:2;
		uint64_t tx_idle:1;
		uint64_t rx_idle:1;
		uint64_t reserved_9_11:3;
		uint64_t speed_msb:1;
		uint64_t reserved_4_7:4;
		uint64_t slottime:1;
		uint64_t duplex:1;
		uint64_t speed:1;
		uint64_t en:1;
#else
		uint64_t en:1;
		uint64_t speed:1;
		uint64_t duplex:1;
		uint64_t slottime:1;
		uint64_t reserved_4_7:4;
		uint64_t speed_msb:1;
		uint64_t reserved_9_11:3;
		uint64_t rx_idle:1;
		uint64_t tx_idle:1;
		uint64_t reserved_14_15:2;
		uint64_t pknd:6;
		uint64_t reserved_22_63:42;
#endif
	} s;
	struct cvmx_gmxx_prtx_cfg_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t slottime:1;
		uint64_t duplex:1;
		uint64_t speed:1;
		uint64_t en:1;
#else
		uint64_t en:1;
		uint64_t speed:1;
		uint64_t duplex:1;
		uint64_t slottime:1;
		uint64_t reserved_4_63:60;
#endif
	} cn30xx;
	struct cvmx_gmxx_prtx_cfg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_14_63:50;
		uint64_t tx_idle:1;
		uint64_t rx_idle:1;
		uint64_t reserved_9_11:3;
		uint64_t speed_msb:1;
		uint64_t reserved_4_7:4;
		uint64_t slottime:1;
		uint64_t duplex:1;
		uint64_t speed:1;
		uint64_t en:1;
#else
		uint64_t en:1;
		uint64_t speed:1;
		uint64_t duplex:1;
		uint64_t slottime:1;
		uint64_t reserved_4_7:4;
		uint64_t speed_msb:1;
		uint64_t reserved_9_11:3;
		uint64_t rx_idle:1;
		uint64_t tx_idle:1;
		uint64_t reserved_14_63:50;
#endif
	} cn52xx;
};

union cvmx_gmxx_rxx_adr_ctl {
	uint64_t u64;
	struct cvmx_gmxx_rxx_adr_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t cam_mode:1;
		uint64_t mcst:2;
		uint64_t bcst:1;
#else
		uint64_t bcst:1;
		uint64_t mcst:2;
		uint64_t cam_mode:1;
		uint64_t reserved_4_63:60;
#endif
	} s;
};

union cvmx_gmxx_rxx_frm_ctl {
	uint64_t u64;
	struct cvmx_gmxx_rxx_frm_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_13_63:51;
		uint64_t ptp_mode:1;
		uint64_t reserved_11_11:1;
		uint64_t null_dis:1;
		uint64_t pre_align:1;
		uint64_t pad_len:1;
		uint64_t vlan_len:1;
		uint64_t pre_free:1;
		uint64_t ctl_smac:1;
		uint64_t ctl_mcst:1;
		uint64_t ctl_bck:1;
		uint64_t ctl_drp:1;
		uint64_t pre_strp:1;
		uint64_t pre_chk:1;
#else
		uint64_t pre_chk:1;
		uint64_t pre_strp:1;
		uint64_t ctl_drp:1;
		uint64_t ctl_bck:1;
		uint64_t ctl_mcst:1;
		uint64_t ctl_smac:1;
		uint64_t pre_free:1;
		uint64_t vlan_len:1;
		uint64_t pad_len:1;
		uint64_t pre_align:1;
		uint64_t null_dis:1;
		uint64_t reserved_11_11:1;
		uint64_t ptp_mode:1;
		uint64_t reserved_13_63:51;
#endif
	} s;
	struct cvmx_gmxx_rxx_frm_ctl_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t pad_len:1;
		uint64_t vlan_len:1;
		uint64_t pre_free:1;
		uint64_t ctl_smac:1;
		uint64_t ctl_mcst:1;
		uint64_t ctl_bck:1;
		uint64_t ctl_drp:1;
		uint64_t pre_strp:1;
		uint64_t pre_chk:1;
#else
		uint64_t pre_chk:1;
		uint64_t pre_strp:1;
		uint64_t ctl_drp:1;
		uint64_t ctl_bck:1;
		uint64_t ctl_mcst:1;
		uint64_t ctl_smac:1;
		uint64_t pre_free:1;
		uint64_t vlan_len:1;
		uint64_t pad_len:1;
		uint64_t reserved_9_63:55;
#endif
	} cn30xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t vlan_len:1;
		uint64_t pre_free:1;
		uint64_t ctl_smac:1;
		uint64_t ctl_mcst:1;
		uint64_t ctl_bck:1;
		uint64_t ctl_drp:1;
		uint64_t pre_strp:1;
		uint64_t pre_chk:1;
#else
		uint64_t pre_chk:1;
		uint64_t pre_strp:1;
		uint64_t ctl_drp:1;
		uint64_t ctl_bck:1;
		uint64_t ctl_mcst:1;
		uint64_t ctl_smac:1;
		uint64_t pre_free:1;
		uint64_t vlan_len:1;
		uint64_t reserved_8_63:56;
#endif
	} cn31xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_11_63:53;
		uint64_t null_dis:1;
		uint64_t pre_align:1;
		uint64_t reserved_7_8:2;
		uint64_t pre_free:1;
		uint64_t ctl_smac:1;
		uint64_t ctl_mcst:1;
		uint64_t ctl_bck:1;
		uint64_t ctl_drp:1;
		uint64_t pre_strp:1;
		uint64_t pre_chk:1;
#else
		uint64_t pre_chk:1;
		uint64_t pre_strp:1;
		uint64_t ctl_drp:1;
		uint64_t ctl_bck:1;
		uint64_t ctl_mcst:1;
		uint64_t ctl_smac:1;
		uint64_t pre_free:1;
		uint64_t reserved_7_8:2;
		uint64_t pre_align:1;
		uint64_t null_dis:1;
		uint64_t reserved_11_63:53;
#endif
	} cn50xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn56xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t pre_align:1;
		uint64_t reserved_7_8:2;
		uint64_t pre_free:1;
		uint64_t ctl_smac:1;
		uint64_t ctl_mcst:1;
		uint64_t ctl_bck:1;
		uint64_t ctl_drp:1;
		uint64_t pre_strp:1;
		uint64_t pre_chk:1;
#else
		uint64_t pre_chk:1;
		uint64_t pre_strp:1;
		uint64_t ctl_drp:1;
		uint64_t ctl_bck:1;
		uint64_t ctl_mcst:1;
		uint64_t ctl_smac:1;
		uint64_t pre_free:1;
		uint64_t reserved_7_8:2;
		uint64_t pre_align:1;
		uint64_t reserved_10_63:54;
#endif
	} cn56xxp1;
	struct cvmx_gmxx_rxx_frm_ctl_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_11_63:53;
		uint64_t null_dis:1;
		uint64_t pre_align:1;
		uint64_t pad_len:1;
		uint64_t vlan_len:1;
		uint64_t pre_free:1;
		uint64_t ctl_smac:1;
		uint64_t ctl_mcst:1;
		uint64_t ctl_bck:1;
		uint64_t ctl_drp:1;
		uint64_t pre_strp:1;
		uint64_t pre_chk:1;
#else
		uint64_t pre_chk:1;
		uint64_t pre_strp:1;
		uint64_t ctl_drp:1;
		uint64_t ctl_bck:1;
		uint64_t ctl_mcst:1;
		uint64_t ctl_smac:1;
		uint64_t pre_free:1;
		uint64_t vlan_len:1;
		uint64_t pad_len:1;
		uint64_t pre_align:1;
		uint64_t null_dis:1;
		uint64_t reserved_11_63:53;
#endif
	} cn58xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_13_63:51;
		uint64_t ptp_mode:1;
		uint64_t reserved_11_11:1;
		uint64_t null_dis:1;
		uint64_t pre_align:1;
		uint64_t reserved_7_8:2;
		uint64_t pre_free:1;
		uint64_t ctl_smac:1;
		uint64_t ctl_mcst:1;
		uint64_t ctl_bck:1;
		uint64_t ctl_drp:1;
		uint64_t pre_strp:1;
		uint64_t pre_chk:1;
#else
		uint64_t pre_chk:1;
		uint64_t pre_strp:1;
		uint64_t ctl_drp:1;
		uint64_t ctl_bck:1;
		uint64_t ctl_mcst:1;
		uint64_t ctl_smac:1;
		uint64_t pre_free:1;
		uint64_t reserved_7_8:2;
		uint64_t pre_align:1;
		uint64_t null_dis:1;
		uint64_t reserved_11_11:1;
		uint64_t ptp_mode:1;
		uint64_t reserved_13_63:51;
#endif
	} cn61xx;
};

union cvmx_gmxx_rxx_frm_max {
	uint64_t u64;
	struct cvmx_gmxx_rxx_frm_max_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t len:16;
#else
		uint64_t len:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
};

union cvmx_gmxx_rxx_frm_min {
	uint64_t u64;
	struct cvmx_gmxx_rxx_frm_min_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t len:16;
#else
		uint64_t len:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
};

union cvmx_gmxx_rxx_int_en {
	uint64_t u64;
	struct cvmx_gmxx_rxx_int_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_29_63:35;
		uint64_t hg2cc:1;
		uint64_t hg2fld:1;
		uint64_t undat:1;
		uint64_t uneop:1;
		uint64_t unsop:1;
		uint64_t bad_term:1;
		uint64_t bad_seq:1;
		uint64_t rem_fault:1;
		uint64_t loc_fault:1;
		uint64_t pause_drp:1;
		uint64_t phy_dupx:1;
		uint64_t phy_spd:1;
		uint64_t phy_link:1;
		uint64_t ifgerr:1;
		uint64_t coldet:1;
		uint64_t falerr:1;
		uint64_t rsverr:1;
		uint64_t pcterr:1;
		uint64_t ovrerr:1;
		uint64_t niberr:1;
		uint64_t skperr:1;
		uint64_t rcverr:1;
		uint64_t lenerr:1;
		uint64_t alnerr:1;
		uint64_t fcserr:1;
		uint64_t jabber:1;
		uint64_t maxerr:1;
		uint64_t carext:1;
		uint64_t minerr:1;
#else
		uint64_t minerr:1;
		uint64_t carext:1;
		uint64_t maxerr:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t alnerr:1;
		uint64_t lenerr:1;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t niberr:1;
		uint64_t ovrerr:1;
		uint64_t pcterr:1;
		uint64_t rsverr:1;
		uint64_t falerr:1;
		uint64_t coldet:1;
		uint64_t ifgerr:1;
		uint64_t phy_link:1;
		uint64_t phy_spd:1;
		uint64_t phy_dupx:1;
		uint64_t pause_drp:1;
		uint64_t loc_fault:1;
		uint64_t rem_fault:1;
		uint64_t bad_seq:1;
		uint64_t bad_term:1;
		uint64_t unsop:1;
		uint64_t uneop:1;
		uint64_t undat:1;
		uint64_t hg2fld:1;
		uint64_t hg2cc:1;
		uint64_t reserved_29_63:35;
#endif
	} s;
	struct cvmx_gmxx_rxx_int_en_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_19_63:45;
		uint64_t phy_dupx:1;
		uint64_t phy_spd:1;
		uint64_t phy_link:1;
		uint64_t ifgerr:1;
		uint64_t coldet:1;
		uint64_t falerr:1;
		uint64_t rsverr:1;
		uint64_t pcterr:1;
		uint64_t ovrerr:1;
		uint64_t niberr:1;
		uint64_t skperr:1;
		uint64_t rcverr:1;
		uint64_t lenerr:1;
		uint64_t alnerr:1;
		uint64_t fcserr:1;
		uint64_t jabber:1;
		uint64_t maxerr:1;
		uint64_t carext:1;
		uint64_t minerr:1;
#else
		uint64_t minerr:1;
		uint64_t carext:1;
		uint64_t maxerr:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t alnerr:1;
		uint64_t lenerr:1;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t niberr:1;
		uint64_t ovrerr:1;
		uint64_t pcterr:1;
		uint64_t rsverr:1;
		uint64_t falerr:1;
		uint64_t coldet:1;
		uint64_t ifgerr:1;
		uint64_t phy_link:1;
		uint64_t phy_spd:1;
		uint64_t phy_dupx:1;
		uint64_t reserved_19_63:45;
#endif
	} cn30xx;
	struct cvmx_gmxx_rxx_int_en_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t pause_drp:1;
		uint64_t phy_dupx:1;
		uint64_t phy_spd:1;
		uint64_t phy_link:1;
		uint64_t ifgerr:1;
		uint64_t coldet:1;
		uint64_t falerr:1;
		uint64_t rsverr:1;
		uint64_t pcterr:1;
		uint64_t ovrerr:1;
		uint64_t niberr:1;
		uint64_t skperr:1;
		uint64_t rcverr:1;
		uint64_t reserved_6_6:1;
		uint64_t alnerr:1;
		uint64_t fcserr:1;
		uint64_t jabber:1;
		uint64_t reserved_2_2:1;
		uint64_t carext:1;
		uint64_t reserved_0_0:1;
#else
		uint64_t reserved_0_0:1;
		uint64_t carext:1;
		uint64_t reserved_2_2:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t alnerr:1;
		uint64_t reserved_6_6:1;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t niberr:1;
		uint64_t ovrerr:1;
		uint64_t pcterr:1;
		uint64_t rsverr:1;
		uint64_t falerr:1;
		uint64_t coldet:1;
		uint64_t ifgerr:1;
		uint64_t phy_link:1;
		uint64_t phy_spd:1;
		uint64_t phy_dupx:1;
		uint64_t pause_drp:1;
		uint64_t reserved_20_63:44;
#endif
	} cn50xx;
	struct cvmx_gmxx_rxx_int_en_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_29_63:35;
		uint64_t hg2cc:1;
		uint64_t hg2fld:1;
		uint64_t undat:1;
		uint64_t uneop:1;
		uint64_t unsop:1;
		uint64_t bad_term:1;
		uint64_t bad_seq:1;
		uint64_t rem_fault:1;
		uint64_t loc_fault:1;
		uint64_t pause_drp:1;
		uint64_t reserved_16_18:3;
		uint64_t ifgerr:1;
		uint64_t coldet:1;
		uint64_t falerr:1;
		uint64_t rsverr:1;
		uint64_t pcterr:1;
		uint64_t ovrerr:1;
		uint64_t reserved_9_9:1;
		uint64_t skperr:1;
		uint64_t rcverr:1;
		uint64_t reserved_5_6:2;
		uint64_t fcserr:1;
		uint64_t jabber:1;
		uint64_t reserved_2_2:1;
		uint64_t carext:1;
		uint64_t reserved_0_0:1;
#else
		uint64_t reserved_0_0:1;
		uint64_t carext:1;
		uint64_t reserved_2_2:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t reserved_5_6:2;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t reserved_9_9:1;
		uint64_t ovrerr:1;
		uint64_t pcterr:1;
		uint64_t rsverr:1;
		uint64_t falerr:1;
		uint64_t coldet:1;
		uint64_t ifgerr:1;
		uint64_t reserved_16_18:3;
		uint64_t pause_drp:1;
		uint64_t loc_fault:1;
		uint64_t rem_fault:1;
		uint64_t bad_seq:1;
		uint64_t bad_term:1;
		uint64_t unsop:1;
		uint64_t uneop:1;
		uint64_t undat:1;
		uint64_t hg2fld:1;
		uint64_t hg2cc:1;
		uint64_t reserved_29_63:35;
#endif
	} cn52xx;
	struct cvmx_gmxx_rxx_int_en_cn56xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_27_63:37;
		uint64_t undat:1;
		uint64_t uneop:1;
		uint64_t unsop:1;
		uint64_t bad_term:1;
		uint64_t bad_seq:1;
		uint64_t rem_fault:1;
		uint64_t loc_fault:1;
		uint64_t pause_drp:1;
		uint64_t reserved_16_18:3;
		uint64_t ifgerr:1;
		uint64_t coldet:1;
		uint64_t falerr:1;
		uint64_t rsverr:1;
		uint64_t pcterr:1;
		uint64_t ovrerr:1;
		uint64_t reserved_9_9:1;
		uint64_t skperr:1;
		uint64_t rcverr:1;
		uint64_t reserved_5_6:2;
		uint64_t fcserr:1;
		uint64_t jabber:1;
		uint64_t reserved_2_2:1;
		uint64_t carext:1;
		uint64_t reserved_0_0:1;
#else
		uint64_t reserved_0_0:1;
		uint64_t carext:1;
		uint64_t reserved_2_2:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t reserved_5_6:2;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t reserved_9_9:1;
		uint64_t ovrerr:1;
		uint64_t pcterr:1;
		uint64_t rsverr:1;
		uint64_t falerr:1;
		uint64_t coldet:1;
		uint64_t ifgerr:1;
		uint64_t reserved_16_18:3;
		uint64_t pause_drp:1;
		uint64_t loc_fault:1;
		uint64_t rem_fault:1;
		uint64_t bad_seq:1;
		uint64_t bad_term:1;
		uint64_t unsop:1;
		uint64_t uneop:1;
		uint64_t undat:1;
		uint64_t reserved_27_63:37;
#endif
	} cn56xxp1;
	struct cvmx_gmxx_rxx_int_en_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t pause_drp:1;
		uint64_t phy_dupx:1;
		uint64_t phy_spd:1;
		uint64_t phy_link:1;
		uint64_t ifgerr:1;
		uint64_t coldet:1;
		uint64_t falerr:1;
		uint64_t rsverr:1;
		uint64_t pcterr:1;
		uint64_t ovrerr:1;
		uint64_t niberr:1;
		uint64_t skperr:1;
		uint64_t rcverr:1;
		uint64_t lenerr:1;
		uint64_t alnerr:1;
		uint64_t fcserr:1;
		uint64_t jabber:1;
		uint64_t maxerr:1;
		uint64_t carext:1;
		uint64_t minerr:1;
#else
		uint64_t minerr:1;
		uint64_t carext:1;
		uint64_t maxerr:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t alnerr:1;
		uint64_t lenerr:1;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t niberr:1;
		uint64_t ovrerr:1;
		uint64_t pcterr:1;
		uint64_t rsverr:1;
		uint64_t falerr:1;
		uint64_t coldet:1;
		uint64_t ifgerr:1;
		uint64_t phy_link:1;
		uint64_t phy_spd:1;
		uint64_t phy_dupx:1;
		uint64_t pause_drp:1;
		uint64_t reserved_20_63:44;
#endif
	} cn58xx;
	struct cvmx_gmxx_rxx_int_en_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_29_63:35;
		uint64_t hg2cc:1;
		uint64_t hg2fld:1;
		uint64_t undat:1;
		uint64_t uneop:1;
		uint64_t unsop:1;
		uint64_t bad_term:1;
		uint64_t bad_seq:1;
		uint64_t rem_fault:1;
		uint64_t loc_fault:1;
		uint64_t pause_drp:1;
		uint64_t reserved_16_18:3;
		uint64_t ifgerr:1;
		uint64_t coldet:1;
		uint64_t falerr:1;
		uint64_t rsverr:1;
		uint64_t pcterr:1;
		uint64_t ovrerr:1;
		uint64_t reserved_9_9:1;
		uint64_t skperr:1;
		uint64_t rcverr:1;
		uint64_t reserved_5_6:2;
		uint64_t fcserr:1;
		uint64_t jabber:1;
		uint64_t reserved_2_2:1;
		uint64_t carext:1;
		uint64_t minerr:1;
#else
		uint64_t minerr:1;
		uint64_t carext:1;
		uint64_t reserved_2_2:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t reserved_5_6:2;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t reserved_9_9:1;
		uint64_t ovrerr:1;
		uint64_t pcterr:1;
		uint64_t rsverr:1;
		uint64_t falerr:1;
		uint64_t coldet:1;
		uint64_t ifgerr:1;
		uint64_t reserved_16_18:3;
		uint64_t pause_drp:1;
		uint64_t loc_fault:1;
		uint64_t rem_fault:1;
		uint64_t bad_seq:1;
		uint64_t bad_term:1;
		uint64_t unsop:1;
		uint64_t uneop:1;
		uint64_t undat:1;
		uint64_t hg2fld:1;
		uint64_t hg2cc:1;
		uint64_t reserved_29_63:35;
#endif
	} cn61xx;
};

union cvmx_gmxx_rxx_int_reg {
	uint64_t u64;
	struct cvmx_gmxx_rxx_int_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_29_63:35;
		uint64_t hg2cc:1;
		uint64_t hg2fld:1;
		uint64_t undat:1;
		uint64_t uneop:1;
		uint64_t unsop:1;
		uint64_t bad_term:1;
		uint64_t bad_seq:1;
		uint64_t rem_fault:1;
		uint64_t loc_fault:1;
		uint64_t pause_drp:1;
		uint64_t phy_dupx:1;
		uint64_t phy_spd:1;
		uint64_t phy_link:1;
		uint64_t ifgerr:1;
		uint64_t coldet:1;
		uint64_t falerr:1;
		uint64_t rsverr:1;
		uint64_t pcterr:1;
		uint64_t ovrerr:1;
		uint64_t niberr:1;
		uint64_t skperr:1;
		uint64_t rcverr:1;
		uint64_t lenerr:1;
		uint64_t alnerr:1;
		uint64_t fcserr:1;
		uint64_t jabber:1;
		uint64_t maxerr:1;
		uint64_t carext:1;
		uint64_t minerr:1;
#else
		uint64_t minerr:1;
		uint64_t carext:1;
		uint64_t maxerr:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t alnerr:1;
		uint64_t lenerr:1;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t niberr:1;
		uint64_t ovrerr:1;
		uint64_t pcterr:1;
		uint64_t rsverr:1;
		uint64_t falerr:1;
		uint64_t coldet:1;
		uint64_t ifgerr:1;
		uint64_t phy_link:1;
		uint64_t phy_spd:1;
		uint64_t phy_dupx:1;
		uint64_t pause_drp:1;
		uint64_t loc_fault:1;
		uint64_t rem_fault:1;
		uint64_t bad_seq:1;
		uint64_t bad_term:1;
		uint64_t unsop:1;
		uint64_t uneop:1;
		uint64_t undat:1;
		uint64_t hg2fld:1;
		uint64_t hg2cc:1;
		uint64_t reserved_29_63:35;
#endif
	} s;
	struct cvmx_gmxx_rxx_int_reg_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_19_63:45;
		uint64_t phy_dupx:1;
		uint64_t phy_spd:1;
		uint64_t phy_link:1;
		uint64_t ifgerr:1;
		uint64_t coldet:1;
		uint64_t falerr:1;
		uint64_t rsverr:1;
		uint64_t pcterr:1;
		uint64_t ovrerr:1;
		uint64_t niberr:1;
		uint64_t skperr:1;
		uint64_t rcverr:1;
		uint64_t lenerr:1;
		uint64_t alnerr:1;
		uint64_t fcserr:1;
		uint64_t jabber:1;
		uint64_t maxerr:1;
		uint64_t carext:1;
		uint64_t minerr:1;
#else
		uint64_t minerr:1;
		uint64_t carext:1;
		uint64_t maxerr:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t alnerr:1;
		uint64_t lenerr:1;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t niberr:1;
		uint64_t ovrerr:1;
		uint64_t pcterr:1;
		uint64_t rsverr:1;
		uint64_t falerr:1;
		uint64_t coldet:1;
		uint64_t ifgerr:1;
		uint64_t phy_link:1;
		uint64_t phy_spd:1;
		uint64_t phy_dupx:1;
		uint64_t reserved_19_63:45;
#endif
	} cn30xx;
	struct cvmx_gmxx_rxx_int_reg_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t pause_drp:1;
		uint64_t phy_dupx:1;
		uint64_t phy_spd:1;
		uint64_t phy_link:1;
		uint64_t ifgerr:1;
		uint64_t coldet:1;
		uint64_t falerr:1;
		uint64_t rsverr:1;
		uint64_t pcterr:1;
		uint64_t ovrerr:1;
		uint64_t niberr:1;
		uint64_t skperr:1;
		uint64_t rcverr:1;
		uint64_t reserved_6_6:1;
		uint64_t alnerr:1;
		uint64_t fcserr:1;
		uint64_t jabber:1;
		uint64_t reserved_2_2:1;
		uint64_t carext:1;
		uint64_t reserved_0_0:1;
#else
		uint64_t reserved_0_0:1;
		uint64_t carext:1;
		uint64_t reserved_2_2:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t alnerr:1;
		uint64_t reserved_6_6:1;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t niberr:1;
		uint64_t ovrerr:1;
		uint64_t pcterr:1;
		uint64_t rsverr:1;
		uint64_t falerr:1;
		uint64_t coldet:1;
		uint64_t ifgerr:1;
		uint64_t phy_link:1;
		uint64_t phy_spd:1;
		uint64_t phy_dupx:1;
		uint64_t pause_drp:1;
		uint64_t reserved_20_63:44;
#endif
	} cn50xx;
	struct cvmx_gmxx_rxx_int_reg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_29_63:35;
		uint64_t hg2cc:1;
		uint64_t hg2fld:1;
		uint64_t undat:1;
		uint64_t uneop:1;
		uint64_t unsop:1;
		uint64_t bad_term:1;
		uint64_t bad_seq:1;
		uint64_t rem_fault:1;
		uint64_t loc_fault:1;
		uint64_t pause_drp:1;
		uint64_t reserved_16_18:3;
		uint64_t ifgerr:1;
		uint64_t coldet:1;
		uint64_t falerr:1;
		uint64_t rsverr:1;
		uint64_t pcterr:1;
		uint64_t ovrerr:1;
		uint64_t reserved_9_9:1;
		uint64_t skperr:1;
		uint64_t rcverr:1;
		uint64_t reserved_5_6:2;
		uint64_t fcserr:1;
		uint64_t jabber:1;
		uint64_t reserved_2_2:1;
		uint64_t carext:1;
		uint64_t reserved_0_0:1;
#else
		uint64_t reserved_0_0:1;
		uint64_t carext:1;
		uint64_t reserved_2_2:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t reserved_5_6:2;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t reserved_9_9:1;
		uint64_t ovrerr:1;
		uint64_t pcterr:1;
		uint64_t rsverr:1;
		uint64_t falerr:1;
		uint64_t coldet:1;
		uint64_t ifgerr:1;
		uint64_t reserved_16_18:3;
		uint64_t pause_drp:1;
		uint64_t loc_fault:1;
		uint64_t rem_fault:1;
		uint64_t bad_seq:1;
		uint64_t bad_term:1;
		uint64_t unsop:1;
		uint64_t uneop:1;
		uint64_t undat:1;
		uint64_t hg2fld:1;
		uint64_t hg2cc:1;
		uint64_t reserved_29_63:35;
#endif
	} cn52xx;
	struct cvmx_gmxx_rxx_int_reg_cn56xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_27_63:37;
		uint64_t undat:1;
		uint64_t uneop:1;
		uint64_t unsop:1;
		uint64_t bad_term:1;
		uint64_t bad_seq:1;
		uint64_t rem_fault:1;
		uint64_t loc_fault:1;
		uint64_t pause_drp:1;
		uint64_t reserved_16_18:3;
		uint64_t ifgerr:1;
		uint64_t coldet:1;
		uint64_t falerr:1;
		uint64_t rsverr:1;
		uint64_t pcterr:1;
		uint64_t ovrerr:1;
		uint64_t reserved_9_9:1;
		uint64_t skperr:1;
		uint64_t rcverr:1;
		uint64_t reserved_5_6:2;
		uint64_t fcserr:1;
		uint64_t jabber:1;
		uint64_t reserved_2_2:1;
		uint64_t carext:1;
		uint64_t reserved_0_0:1;
#else
		uint64_t reserved_0_0:1;
		uint64_t carext:1;
		uint64_t reserved_2_2:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t reserved_5_6:2;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t reserved_9_9:1;
		uint64_t ovrerr:1;
		uint64_t pcterr:1;
		uint64_t rsverr:1;
		uint64_t falerr:1;
		uint64_t coldet:1;
		uint64_t ifgerr:1;
		uint64_t reserved_16_18:3;
		uint64_t pause_drp:1;
		uint64_t loc_fault:1;
		uint64_t rem_fault:1;
		uint64_t bad_seq:1;
		uint64_t bad_term:1;
		uint64_t unsop:1;
		uint64_t uneop:1;
		uint64_t undat:1;
		uint64_t reserved_27_63:37;
#endif
	} cn56xxp1;
	struct cvmx_gmxx_rxx_int_reg_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t pause_drp:1;
		uint64_t phy_dupx:1;
		uint64_t phy_spd:1;
		uint64_t phy_link:1;
		uint64_t ifgerr:1;
		uint64_t coldet:1;
		uint64_t falerr:1;
		uint64_t rsverr:1;
		uint64_t pcterr:1;
		uint64_t ovrerr:1;
		uint64_t niberr:1;
		uint64_t skperr:1;
		uint64_t rcverr:1;
		uint64_t lenerr:1;
		uint64_t alnerr:1;
		uint64_t fcserr:1;
		uint64_t jabber:1;
		uint64_t maxerr:1;
		uint64_t carext:1;
		uint64_t minerr:1;
#else
		uint64_t minerr:1;
		uint64_t carext:1;
		uint64_t maxerr:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t alnerr:1;
		uint64_t lenerr:1;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t niberr:1;
		uint64_t ovrerr:1;
		uint64_t pcterr:1;
		uint64_t rsverr:1;
		uint64_t falerr:1;
		uint64_t coldet:1;
		uint64_t ifgerr:1;
		uint64_t phy_link:1;
		uint64_t phy_spd:1;
		uint64_t phy_dupx:1;
		uint64_t pause_drp:1;
		uint64_t reserved_20_63:44;
#endif
	} cn58xx;
	struct cvmx_gmxx_rxx_int_reg_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_29_63:35;
		uint64_t hg2cc:1;
		uint64_t hg2fld:1;
		uint64_t undat:1;
		uint64_t uneop:1;
		uint64_t unsop:1;
		uint64_t bad_term:1;
		uint64_t bad_seq:1;
		uint64_t rem_fault:1;
		uint64_t loc_fault:1;
		uint64_t pause_drp:1;
		uint64_t reserved_16_18:3;
		uint64_t ifgerr:1;
		uint64_t coldet:1;
		uint64_t falerr:1;
		uint64_t rsverr:1;
		uint64_t pcterr:1;
		uint64_t ovrerr:1;
		uint64_t reserved_9_9:1;
		uint64_t skperr:1;
		uint64_t rcverr:1;
		uint64_t reserved_5_6:2;
		uint64_t fcserr:1;
		uint64_t jabber:1;
		uint64_t reserved_2_2:1;
		uint64_t carext:1;
		uint64_t minerr:1;
#else
		uint64_t minerr:1;
		uint64_t carext:1;
		uint64_t reserved_2_2:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t reserved_5_6:2;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t reserved_9_9:1;
		uint64_t ovrerr:1;
		uint64_t pcterr:1;
		uint64_t rsverr:1;
		uint64_t falerr:1;
		uint64_t coldet:1;
		uint64_t ifgerr:1;
		uint64_t reserved_16_18:3;
		uint64_t pause_drp:1;
		uint64_t loc_fault:1;
		uint64_t rem_fault:1;
		uint64_t bad_seq:1;
		uint64_t bad_term:1;
		uint64_t unsop:1;
		uint64_t uneop:1;
		uint64_t undat:1;
		uint64_t hg2fld:1;
		uint64_t hg2cc:1;
		uint64_t reserved_29_63:35;
#endif
	} cn61xx;
};

union cvmx_gmxx_rxx_jabber {
	uint64_t u64;
	struct cvmx_gmxx_rxx_jabber_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t cnt:16;
#else
		uint64_t cnt:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
};

union cvmx_gmxx_rxx_rx_inbnd {
	uint64_t u64;
	struct cvmx_gmxx_rxx_rx_inbnd_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t duplex:1;
		uint64_t speed:2;
		uint64_t status:1;
#else
		uint64_t status:1;
		uint64_t speed:2;
		uint64_t duplex:1;
		uint64_t reserved_4_63:60;
#endif
	} s;
};

union cvmx_gmxx_rx_prts {
	uint64_t u64;
	struct cvmx_gmxx_rx_prts_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t prts:3;
#else
		uint64_t prts:3;
		uint64_t reserved_3_63:61;
#endif
	} s;
};

union cvmx_gmxx_rx_xaui_ctl {
	uint64_t u64;
	struct cvmx_gmxx_rx_xaui_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t status:2;
#else
		uint64_t status:2;
		uint64_t reserved_2_63:62;
#endif
	} s;
};

union cvmx_gmxx_txx_thresh {
	uint64_t u64;
	struct cvmx_gmxx_txx_thresh_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t cnt:10;
#else
		uint64_t cnt:10;
		uint64_t reserved_10_63:54;
#endif
	} s;
	struct cvmx_gmxx_txx_thresh_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t cnt:7;
#else
		uint64_t cnt:7;
		uint64_t reserved_7_63:57;
#endif
	} cn30xx;
	struct cvmx_gmxx_txx_thresh_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t cnt:9;
#else
		uint64_t cnt:9;
		uint64_t reserved_9_63:55;
#endif
	} cn38xx;
};

union cvmx_gmxx_tx_int_en {
	uint64_t u64;
	struct cvmx_gmxx_tx_int_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_25_63:39;
		uint64_t xchange:1;
		uint64_t ptp_lost:4;
		uint64_t late_col:4;
		uint64_t xsdef:4;
		uint64_t xscol:4;
		uint64_t reserved_6_7:2;
		uint64_t undflw:4;
		uint64_t reserved_1_1:1;
		uint64_t pko_nxa:1;
#else
		uint64_t pko_nxa:1;
		uint64_t reserved_1_1:1;
		uint64_t undflw:4;
		uint64_t reserved_6_7:2;
		uint64_t xscol:4;
		uint64_t xsdef:4;
		uint64_t late_col:4;
		uint64_t ptp_lost:4;
		uint64_t xchange:1;
		uint64_t reserved_25_63:39;
#endif
	} s;
	struct cvmx_gmxx_tx_int_en_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_19_63:45;
		uint64_t late_col:3;
		uint64_t reserved_15_15:1;
		uint64_t xsdef:3;
		uint64_t reserved_11_11:1;
		uint64_t xscol:3;
		uint64_t reserved_5_7:3;
		uint64_t undflw:3;
		uint64_t reserved_1_1:1;
		uint64_t pko_nxa:1;
#else
		uint64_t pko_nxa:1;
		uint64_t reserved_1_1:1;
		uint64_t undflw:3;
		uint64_t reserved_5_7:3;
		uint64_t xscol:3;
		uint64_t reserved_11_11:1;
		uint64_t xsdef:3;
		uint64_t reserved_15_15:1;
		uint64_t late_col:3;
		uint64_t reserved_19_63:45;
#endif
	} cn30xx;
	struct cvmx_gmxx_tx_int_en_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t xsdef:3;
		uint64_t reserved_11_11:1;
		uint64_t xscol:3;
		uint64_t reserved_5_7:3;
		uint64_t undflw:3;
		uint64_t reserved_1_1:1;
		uint64_t pko_nxa:1;
#else
		uint64_t pko_nxa:1;
		uint64_t reserved_1_1:1;
		uint64_t undflw:3;
		uint64_t reserved_5_7:3;
		uint64_t xscol:3;
		uint64_t reserved_11_11:1;
		uint64_t xsdef:3;
		uint64_t reserved_15_63:49;
#endif
	} cn31xx;
	struct cvmx_gmxx_tx_int_en_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t late_col:4;
		uint64_t xsdef:4;
		uint64_t xscol:4;
		uint64_t reserved_6_7:2;
		uint64_t undflw:4;
		uint64_t ncb_nxa:1;
		uint64_t pko_nxa:1;
#else
		uint64_t pko_nxa:1;
		uint64_t ncb_nxa:1;
		uint64_t undflw:4;
		uint64_t reserved_6_7:2;
		uint64_t xscol:4;
		uint64_t xsdef:4;
		uint64_t late_col:4;
		uint64_t reserved_20_63:44;
#endif
	} cn38xx;
	struct cvmx_gmxx_tx_int_en_cn38xxp2 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t xsdef:4;
		uint64_t xscol:4;
		uint64_t reserved_6_7:2;
		uint64_t undflw:4;
		uint64_t ncb_nxa:1;
		uint64_t pko_nxa:1;
#else
		uint64_t pko_nxa:1;
		uint64_t ncb_nxa:1;
		uint64_t undflw:4;
		uint64_t reserved_6_7:2;
		uint64_t xscol:4;
		uint64_t xsdef:4;
		uint64_t reserved_16_63:48;
#endif
	} cn38xxp2;
	struct cvmx_gmxx_tx_int_en_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t late_col:4;
		uint64_t xsdef:4;
		uint64_t xscol:4;
		uint64_t reserved_6_7:2;
		uint64_t undflw:4;
		uint64_t reserved_1_1:1;
		uint64_t pko_nxa:1;
#else
		uint64_t pko_nxa:1;
		uint64_t reserved_1_1:1;
		uint64_t undflw:4;
		uint64_t reserved_6_7:2;
		uint64_t xscol:4;
		uint64_t xsdef:4;
		uint64_t late_col:4;
		uint64_t reserved_20_63:44;
#endif
	} cn52xx;
	struct cvmx_gmxx_tx_int_en_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_24_63:40;
		uint64_t ptp_lost:4;
		uint64_t late_col:4;
		uint64_t xsdef:4;
		uint64_t xscol:4;
		uint64_t reserved_6_7:2;
		uint64_t undflw:4;
		uint64_t reserved_1_1:1;
		uint64_t pko_nxa:1;
#else
		uint64_t pko_nxa:1;
		uint64_t reserved_1_1:1;
		uint64_t undflw:4;
		uint64_t reserved_6_7:2;
		uint64_t xscol:4;
		uint64_t xsdef:4;
		uint64_t late_col:4;
		uint64_t ptp_lost:4;
		uint64_t reserved_24_63:40;
#endif
	} cn63xx;
	struct cvmx_gmxx_tx_int_en_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_25_63:39;
		uint64_t xchange:1;
		uint64_t ptp_lost:4;
		uint64_t late_col:4;
		uint64_t xsdef:4;
		uint64_t xscol:4;
		uint64_t reserved_6_7:2;
		uint64_t undflw:4;
		uint64_t pko_nxp:1;
		uint64_t pko_nxa:1;
#else
		uint64_t pko_nxa:1;
		uint64_t pko_nxp:1;
		uint64_t undflw:4;
		uint64_t reserved_6_7:2;
		uint64_t xscol:4;
		uint64_t xsdef:4;
		uint64_t late_col:4;
		uint64_t ptp_lost:4;
		uint64_t xchange:1;
		uint64_t reserved_25_63:39;
#endif
	} cn68xx;
	struct cvmx_gmxx_tx_int_en_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_25_63:39;
		uint64_t xchange:1;
		uint64_t reserved_22_23:2;
		uint64_t ptp_lost:2;
		uint64_t reserved_18_19:2;
		uint64_t late_col:2;
		uint64_t reserved_14_15:2;
		uint64_t xsdef:2;
		uint64_t reserved_10_11:2;
		uint64_t xscol:2;
		uint64_t reserved_4_7:4;
		uint64_t undflw:2;
		uint64_t reserved_1_1:1;
		uint64_t pko_nxa:1;
#else
		uint64_t pko_nxa:1;
		uint64_t reserved_1_1:1;
		uint64_t undflw:2;
		uint64_t reserved_4_7:4;
		uint64_t xscol:2;
		uint64_t reserved_10_11:2;
		uint64_t xsdef:2;
		uint64_t reserved_14_15:2;
		uint64_t late_col:2;
		uint64_t reserved_18_19:2;
		uint64_t ptp_lost:2;
		uint64_t reserved_22_23:2;
		uint64_t xchange:1;
		uint64_t reserved_25_63:39;
#endif
	} cnf71xx;
};

union cvmx_gmxx_tx_int_reg {
	uint64_t u64;
	struct cvmx_gmxx_tx_int_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_25_63:39;
		uint64_t xchange:1;
		uint64_t ptp_lost:4;
		uint64_t late_col:4;
		uint64_t xsdef:4;
		uint64_t xscol:4;
		uint64_t reserved_6_7:2;
		uint64_t undflw:4;
		uint64_t reserved_1_1:1;
		uint64_t pko_nxa:1;
#else
		uint64_t pko_nxa:1;
		uint64_t reserved_1_1:1;
		uint64_t undflw:4;
		uint64_t reserved_6_7:2;
		uint64_t xscol:4;
		uint64_t xsdef:4;
		uint64_t late_col:4;
		uint64_t ptp_lost:4;
		uint64_t xchange:1;
		uint64_t reserved_25_63:39;
#endif
	} s;
	struct cvmx_gmxx_tx_int_reg_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_19_63:45;
		uint64_t late_col:3;
		uint64_t reserved_15_15:1;
		uint64_t xsdef:3;
		uint64_t reserved_11_11:1;
		uint64_t xscol:3;
		uint64_t reserved_5_7:3;
		uint64_t undflw:3;
		uint64_t reserved_1_1:1;
		uint64_t pko_nxa:1;
#else
		uint64_t pko_nxa:1;
		uint64_t reserved_1_1:1;
		uint64_t undflw:3;
		uint64_t reserved_5_7:3;
		uint64_t xscol:3;
		uint64_t reserved_11_11:1;
		uint64_t xsdef:3;
		uint64_t reserved_15_15:1;
		uint64_t late_col:3;
		uint64_t reserved_19_63:45;
#endif
	} cn30xx;
	struct cvmx_gmxx_tx_int_reg_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t xsdef:3;
		uint64_t reserved_11_11:1;
		uint64_t xscol:3;
		uint64_t reserved_5_7:3;
		uint64_t undflw:3;
		uint64_t reserved_1_1:1;
		uint64_t pko_nxa:1;
#else
		uint64_t pko_nxa:1;
		uint64_t reserved_1_1:1;
		uint64_t undflw:3;
		uint64_t reserved_5_7:3;
		uint64_t xscol:3;
		uint64_t reserved_11_11:1;
		uint64_t xsdef:3;
		uint64_t reserved_15_63:49;
#endif
	} cn31xx;
	struct cvmx_gmxx_tx_int_reg_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t late_col:4;
		uint64_t xsdef:4;
		uint64_t xscol:4;
		uint64_t reserved_6_7:2;
		uint64_t undflw:4;
		uint64_t ncb_nxa:1;
		uint64_t pko_nxa:1;
#else
		uint64_t pko_nxa:1;
		uint64_t ncb_nxa:1;
		uint64_t undflw:4;
		uint64_t reserved_6_7:2;
		uint64_t xscol:4;
		uint64_t xsdef:4;
		uint64_t late_col:4;
		uint64_t reserved_20_63:44;
#endif
	} cn38xx;
	struct cvmx_gmxx_tx_int_reg_cn38xxp2 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t xsdef:4;
		uint64_t xscol:4;
		uint64_t reserved_6_7:2;
		uint64_t undflw:4;
		uint64_t ncb_nxa:1;
		uint64_t pko_nxa:1;
#else
		uint64_t pko_nxa:1;
		uint64_t ncb_nxa:1;
		uint64_t undflw:4;
		uint64_t reserved_6_7:2;
		uint64_t xscol:4;
		uint64_t xsdef:4;
		uint64_t reserved_16_63:48;
#endif
	} cn38xxp2;
	struct cvmx_gmxx_tx_int_reg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t late_col:4;
		uint64_t xsdef:4;
		uint64_t xscol:4;
		uint64_t reserved_6_7:2;
		uint64_t undflw:4;
		uint64_t reserved_1_1:1;
		uint64_t pko_nxa:1;
#else
		uint64_t pko_nxa:1;
		uint64_t reserved_1_1:1;
		uint64_t undflw:4;
		uint64_t reserved_6_7:2;
		uint64_t xscol:4;
		uint64_t xsdef:4;
		uint64_t late_col:4;
		uint64_t reserved_20_63:44;
#endif
	} cn52xx;
	struct cvmx_gmxx_tx_int_reg_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_24_63:40;
		uint64_t ptp_lost:4;
		uint64_t late_col:4;
		uint64_t xsdef:4;
		uint64_t xscol:4;
		uint64_t reserved_6_7:2;
		uint64_t undflw:4;
		uint64_t reserved_1_1:1;
		uint64_t pko_nxa:1;
#else
		uint64_t pko_nxa:1;
		uint64_t reserved_1_1:1;
		uint64_t undflw:4;
		uint64_t reserved_6_7:2;
		uint64_t xscol:4;
		uint64_t xsdef:4;
		uint64_t late_col:4;
		uint64_t ptp_lost:4;
		uint64_t reserved_24_63:40;
#endif
	} cn63xx;
	struct cvmx_gmxx_tx_int_reg_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_25_63:39;
		uint64_t xchange:1;
		uint64_t ptp_lost:4;
		uint64_t late_col:4;
		uint64_t xsdef:4;
		uint64_t xscol:4;
		uint64_t reserved_6_7:2;
		uint64_t undflw:4;
		uint64_t pko_nxp:1;
		uint64_t pko_nxa:1;
#else
		uint64_t pko_nxa:1;
		uint64_t pko_nxp:1;
		uint64_t undflw:4;
		uint64_t reserved_6_7:2;
		uint64_t xscol:4;
		uint64_t xsdef:4;
		uint64_t late_col:4;
		uint64_t ptp_lost:4;
		uint64_t xchange:1;
		uint64_t reserved_25_63:39;
#endif
	} cn68xx;
	struct cvmx_gmxx_tx_int_reg_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_25_63:39;
		uint64_t xchange:1;
		uint64_t reserved_22_23:2;
		uint64_t ptp_lost:2;
		uint64_t reserved_18_19:2;
		uint64_t late_col:2;
		uint64_t reserved_14_15:2;
		uint64_t xsdef:2;
		uint64_t reserved_10_11:2;
		uint64_t xscol:2;
		uint64_t reserved_4_7:4;
		uint64_t undflw:2;
		uint64_t reserved_1_1:1;
		uint64_t pko_nxa:1;
#else
		uint64_t pko_nxa:1;
		uint64_t reserved_1_1:1;
		uint64_t undflw:2;
		uint64_t reserved_4_7:4;
		uint64_t xscol:2;
		uint64_t reserved_10_11:2;
		uint64_t xsdef:2;
		uint64_t reserved_14_15:2;
		uint64_t late_col:2;
		uint64_t reserved_18_19:2;
		uint64_t ptp_lost:2;
		uint64_t reserved_22_23:2;
		uint64_t xchange:1;
		uint64_t reserved_25_63:39;
#endif
	} cnf71xx;
};

union cvmx_gmxx_tx_ovr_bp {
	uint64_t u64;
	struct cvmx_gmxx_tx_ovr_bp_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t tx_prt_bp:16;
		uint64_t reserved_12_31:20;
		uint64_t en:4;
		uint64_t bp:4;
		uint64_t ign_full:4;
#else
		uint64_t ign_full:4;
		uint64_t bp:4;
		uint64_t en:4;
		uint64_t reserved_12_31:20;
		uint64_t tx_prt_bp:16;
		uint64_t reserved_48_63:16;
#endif
	} s;
	struct cvmx_gmxx_tx_ovr_bp_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_11_63:53;
		uint64_t en:3;
		uint64_t reserved_7_7:1;
		uint64_t bp:3;
		uint64_t reserved_3_3:1;
		uint64_t ign_full:3;
#else
		uint64_t ign_full:3;
		uint64_t reserved_3_3:1;
		uint64_t bp:3;
		uint64_t reserved_7_7:1;
		uint64_t en:3;
		uint64_t reserved_11_63:53;
#endif
	} cn30xx;
	struct cvmx_gmxx_tx_ovr_bp_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t en:4;
		uint64_t bp:4;
		uint64_t ign_full:4;
#else
		uint64_t ign_full:4;
		uint64_t bp:4;
		uint64_t en:4;
		uint64_t reserved_12_63:52;
#endif
	} cn38xx;
	struct cvmx_gmxx_tx_ovr_bp_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t tx_prt_bp:16;
		uint64_t reserved_10_31:22;
		uint64_t en:2;
		uint64_t reserved_6_7:2;
		uint64_t bp:2;
		uint64_t reserved_2_3:2;
		uint64_t ign_full:2;
#else
		uint64_t ign_full:2;
		uint64_t reserved_2_3:2;
		uint64_t bp:2;
		uint64_t reserved_6_7:2;
		uint64_t en:2;
		uint64_t reserved_10_31:22;
		uint64_t tx_prt_bp:16;
		uint64_t reserved_48_63:16;
#endif
	} cnf71xx;
};

union cvmx_gmxx_tx_prts {
	uint64_t u64;
	struct cvmx_gmxx_tx_prts_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_5_63:59;
		uint64_t prts:5;
#else
		uint64_t prts:5;
		uint64_t reserved_5_63:59;
#endif
	} s;
};

union cvmx_gmxx_tx_spi_ctl {
	uint64_t u64;
	struct cvmx_gmxx_tx_spi_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t tpa_clr:1;
		uint64_t cont_pkt:1;
#else
		uint64_t cont_pkt:1;
		uint64_t tpa_clr:1;
		uint64_t reserved_2_63:62;
#endif
	} s;
};

union cvmx_gmxx_tx_spi_max {
	uint64_t u64;
	struct cvmx_gmxx_tx_spi_max_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_23_63:41;
		uint64_t slice:7;
		uint64_t max2:8;
		uint64_t max1:8;
#else
		uint64_t max1:8;
		uint64_t max2:8;
		uint64_t slice:7;
		uint64_t reserved_23_63:41;
#endif
	} s;
	struct cvmx_gmxx_tx_spi_max_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t max2:8;
		uint64_t max1:8;
#else
		uint64_t max1:8;
		uint64_t max2:8;
		uint64_t reserved_16_63:48;
#endif
	} cn38xx;
};

union cvmx_gmxx_tx_spi_thresh {
	uint64_t u64;
	struct cvmx_gmxx_tx_spi_thresh_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t thresh:6;
#else
		uint64_t thresh:6;
		uint64_t reserved_6_63:58;
#endif
	} s;
};

union cvmx_gmxx_tx_xaui_ctl {
	uint64_t u64;
	struct cvmx_gmxx_tx_xaui_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_11_63:53;
		uint64_t hg_pause_hgi:2;
		uint64_t hg_en:1;
		uint64_t reserved_7_7:1;
		uint64_t ls_byp:1;
		uint64_t ls:2;
		uint64_t reserved_2_3:2;
		uint64_t uni_en:1;
		uint64_t dic_en:1;
#else
		uint64_t dic_en:1;
		uint64_t uni_en:1;
		uint64_t reserved_2_3:2;
		uint64_t ls:2;
		uint64_t ls_byp:1;
		uint64_t reserved_7_7:1;
		uint64_t hg_en:1;
		uint64_t hg_pause_hgi:2;
		uint64_t reserved_11_63:53;
#endif
	} s;
};

#endif

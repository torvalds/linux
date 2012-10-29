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

#ifndef __CVMX_GMXX_DEFS_H__
#define __CVMX_GMXX_DEFS_H__

static inline uint64_t CVMX_GMXX_BAD_REG(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000518ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000518ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000518ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000518ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_BIST(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000400ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000400ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000400ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000400ull) + (block_id) * 0x8000000ull;
}

#define CVMX_GMXX_BPID_MAPX(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180008000680ull) + (((offset) & 15) + ((block_id) & 7) * 0x200000ull) * 8)
#define CVMX_GMXX_BPID_MSK(block_id) (CVMX_ADD_IO_SEG(0x0001180008000700ull) + ((block_id) & 7) * 0x1000000ull)
static inline uint64_t CVMX_GMXX_CLK_EN(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080007F0ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080007F0ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080007F0ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800080007F0ull) + (block_id) * 0x8000000ull;
}

#define CVMX_GMXX_EBP_DIS(block_id) (CVMX_ADD_IO_SEG(0x0001180008000608ull) + ((block_id) & 7) * 0x1000000ull)
#define CVMX_GMXX_EBP_MSK(block_id) (CVMX_ADD_IO_SEG(0x0001180008000600ull) + ((block_id) & 7) * 0x1000000ull)
static inline uint64_t CVMX_GMXX_HG2_CONTROL(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000550ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000550ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000550ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000550ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_INF_MODE(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080007F8ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080007F8ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080007F8ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800080007F8ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_NXA_ADR(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000510ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000510ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000510ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000510ull) + (block_id) * 0x8000000ull;
}

#define CVMX_GMXX_PIPE_STATUS(block_id) (CVMX_ADD_IO_SEG(0x0001180008000760ull) + ((block_id) & 7) * 0x1000000ull)
static inline uint64_t CVMX_GMXX_PRTX_CBFC_CTL(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000580ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000580ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000580ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000580ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_PRTX_CFG(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000010ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000010ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000010ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000010ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000010ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000010ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000010ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

#define CVMX_GMXX_RXAUI_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180008000740ull) + ((block_id) & 7) * 0x1000000ull)
static inline uint64_t CVMX_GMXX_RXX_ADR_CAM0(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000180ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000180ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000180ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
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
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000188ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000188ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000188ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
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
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000190ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000190ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000190ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
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
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000198ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000198ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000198ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
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
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080001A0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080001A0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080001A0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
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
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080001A8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080001A8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080001A8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080001A8ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080001A8ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x00011800080001A8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_ADR_CAM_ALL_EN(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000110ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000110ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000110ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000110ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_ADR_CAM_EN(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000108ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000108ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000108ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000108ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
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
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000100ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000100ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000100ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000100ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000100ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000100ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000100ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_DECISION(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000040ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000040ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000040ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000040ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000040ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000040ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000040ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_FRM_CHK(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000020ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000020ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000020ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000020ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000020ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000020ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000020ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_FRM_CTL(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000018ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000018ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000018ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000018ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000018ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000018ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000018ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

#define CVMX_GMXX_RXX_FRM_MAX(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180008000030ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048)
#define CVMX_GMXX_RXX_FRM_MIN(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180008000028ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048)
static inline uint64_t CVMX_GMXX_RXX_IFG(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000058ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000058ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000058ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000058ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000058ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000058ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000058ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_INT_EN(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000008ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000008ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000008ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000008ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
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
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000000ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000000ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000000ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000000ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
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
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000038ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000038ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000038ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000038ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000038ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000038ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000038ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_PAUSE_DROP_TIME(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000068ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000068ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000068ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000068ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000068ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000068ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000068ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

#define CVMX_GMXX_RXX_RX_INBND(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180008000060ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048)
static inline uint64_t CVMX_GMXX_RXX_STATS_CTL(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000050ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000050ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000050ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000050ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000050ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000050ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000050ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_STATS_OCTS(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000088ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000088ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000088ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000088ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000088ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000088ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000088ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_STATS_OCTS_CTL(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000098ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000098ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000098ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000098ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000098ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000098ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000098ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_STATS_OCTS_DMAC(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000A8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000A8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000A8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000A8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000A8ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000A8ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x00011800080000A8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_STATS_OCTS_DRP(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000B8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000B8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000B8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000B8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000B8ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000B8ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x00011800080000B8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_STATS_PKTS(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000080ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000080ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000080ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000080ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000080ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000080ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000080ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_STATS_PKTS_BAD(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000C0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000C0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000C0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000C0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000C0ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000C0ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x00011800080000C0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_STATS_PKTS_CTL(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000090ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000090ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000090ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000090ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000090ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000090ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000090ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_STATS_PKTS_DMAC(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000A0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000A0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000A0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000A0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000A0ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000A0ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x00011800080000A0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_STATS_PKTS_DRP(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000B0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000B0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000B0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000B0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000B0ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080000B0ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x00011800080000B0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RXX_UDD_SKP(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000048ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000048ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000048ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000048ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000048ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000048ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000048ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_RX_BP_DROPX(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000420ull) + ((offset) + (block_id) * 0x1000000ull) * 8;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000420ull) + ((offset) + (block_id) * 0x1000000ull) * 8;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000420ull) + ((offset) + (block_id) * 0x1000000ull) * 8;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000420ull) + ((offset) + (block_id) * 0x1000000ull) * 8;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000420ull) + ((offset) + (block_id) * 0x0ull) * 8;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000420ull) + ((offset) + (block_id) * 0x200000ull) * 8;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000420ull) + ((offset) + (block_id) * 0x1000000ull) * 8;
}

static inline uint64_t CVMX_GMXX_RX_BP_OFFX(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000460ull) + ((offset) + (block_id) * 0x1000000ull) * 8;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000460ull) + ((offset) + (block_id) * 0x1000000ull) * 8;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000460ull) + ((offset) + (block_id) * 0x1000000ull) * 8;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000460ull) + ((offset) + (block_id) * 0x1000000ull) * 8;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000460ull) + ((offset) + (block_id) * 0x0ull) * 8;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000460ull) + ((offset) + (block_id) * 0x200000ull) * 8;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000460ull) + ((offset) + (block_id) * 0x1000000ull) * 8;
}

static inline uint64_t CVMX_GMXX_RX_BP_ONX(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000440ull) + ((offset) + (block_id) * 0x1000000ull) * 8;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000440ull) + ((offset) + (block_id) * 0x1000000ull) * 8;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000440ull) + ((offset) + (block_id) * 0x1000000ull) * 8;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000440ull) + ((offset) + (block_id) * 0x1000000ull) * 8;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000440ull) + ((offset) + (block_id) * 0x0ull) * 8;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000440ull) + ((offset) + (block_id) * 0x200000ull) * 8;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000440ull) + ((offset) + (block_id) * 0x1000000ull) * 8;
}

static inline uint64_t CVMX_GMXX_RX_HG2_STATUS(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000548ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000548ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000548ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000548ull) + (block_id) * 0x8000000ull;
}

#define CVMX_GMXX_RX_PASS_EN(block_id) (CVMX_ADD_IO_SEG(0x00011800080005F8ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_GMXX_RX_PASS_MAPX(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180008000600ull) + (((offset) & 15) + ((block_id) & 1) * 0x1000000ull) * 8)
static inline uint64_t CVMX_GMXX_RX_PRTS(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000410ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000410ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000410ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000410ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_RX_PRT_INFO(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080004E8ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080004E8ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080004E8ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800080004E8ull) + (block_id) * 0x8000000ull;
}

#define CVMX_GMXX_RX_TX_STATUS(block_id) (CVMX_ADD_IO_SEG(0x00011800080007E8ull))
static inline uint64_t CVMX_GMXX_RX_XAUI_BAD_COL(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000538ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000538ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000538ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000538ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_RX_XAUI_CTL(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000530ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000530ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000530ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000530ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_SMACX(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000230ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000230ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000230ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000230ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000230ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000230ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000230ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_SOFT_BIST(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080007E8ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080007E8ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080007E8ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800080007E8ull) + (block_id) * 0x1000000ull;
}

static inline uint64_t CVMX_GMXX_STAT_BP(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000520ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000520ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000520ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000520ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_TB_REG(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080007E0ull) + (block_id) * 0x8000000ull;
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080007E0ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080007E0ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800080007E0ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_TXX_APPEND(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000218ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000218ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000218ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000218ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000218ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000218ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000218ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TXX_BURST(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000228ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000228ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000228ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000228ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000228ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000228ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000228ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TXX_CBFC_XOFF(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080005A0ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080005A0ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080005A0ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800080005A0ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_TXX_CBFC_XON(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080005C0ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080005C0ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080005C0ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800080005C0ull) + (block_id) * 0x8000000ull;
}

#define CVMX_GMXX_TXX_CLK(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180008000208ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048)
static inline uint64_t CVMX_GMXX_TXX_CTL(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000270ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000270ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000270ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000270ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000270ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000270ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000270ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TXX_MIN_PKT(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000240ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000240ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000240ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000240ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000240ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000240ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000240ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TXX_PAUSE_PKT_INTERVAL(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000248ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000248ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000248ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000248ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
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
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000238ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000238ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000238ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000238ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000238ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000238ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000238ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TXX_PAUSE_TOGO(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000258ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000258ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000258ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000258ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000258ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000258ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000258ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TXX_PAUSE_ZERO(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000260ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000260ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000260ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000260ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000260ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000260ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000260ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

#define CVMX_GMXX_TXX_PIPE(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180008000310ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048)
static inline uint64_t CVMX_GMXX_TXX_SGMII_CTL(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000300ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000300ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000300ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000300ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000300ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TXX_SLOT(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000220ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000220ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000220ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000220ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000220ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000220ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000220ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TXX_SOFT_PAUSE(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000250ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000250ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000250ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000250ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000250ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000250ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000250ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TXX_STAT0(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000280ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000280ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000280ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000280ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000280ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000280ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000280ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TXX_STAT1(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000288ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000288ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000288ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000288ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000288ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000288ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000288ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TXX_STAT2(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000290ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000290ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000290ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000290ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000290ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000290ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000290ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TXX_STAT3(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000298ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000298ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000298ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000298ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000298ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000298ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000298ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TXX_STAT4(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002A0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002A0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002A0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002A0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002A0ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002A0ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x00011800080002A0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TXX_STAT5(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002A8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002A8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002A8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002A8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002A8ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002A8ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x00011800080002A8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TXX_STAT6(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002B0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002B0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002B0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002B0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002B0ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002B0ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x00011800080002B0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TXX_STAT7(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002B8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002B8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002B8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002B8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002B8ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002B8ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x00011800080002B8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TXX_STAT8(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002C0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002C0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002C0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002C0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002C0ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002C0ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x00011800080002C0ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TXX_STAT9(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002C8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002C8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002C8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002C8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002C8ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080002C8ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x00011800080002C8ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TXX_STATS_CTL(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000268ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000268ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000268ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000268ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000268ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000268ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000268ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TXX_THRESH(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000210ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000210ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000210ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000210ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000210ull) + ((offset) + (block_id) * 0x0ull) * 2048;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000210ull) + ((offset) + (block_id) * 0x2000ull) * 2048;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000210ull) + ((offset) + (block_id) * 0x10000ull) * 2048;
}

static inline uint64_t CVMX_GMXX_TX_BP(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080004D0ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080004D0ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080004D0ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800080004D0ull) + (block_id) * 0x8000000ull;
}

#define CVMX_GMXX_TX_CLK_MSKX(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180008000780ull) + (((offset) & 1) + ((block_id) & 0) * 0x0ull) * 8)
static inline uint64_t CVMX_GMXX_TX_COL_ATTEMPT(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000498ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000498ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000498ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000498ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_TX_CORRUPT(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080004D8ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080004D8ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080004D8ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800080004D8ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_TX_HG2_REG1(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000558ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000558ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000558ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000558ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_TX_HG2_REG2(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000560ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000560ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000560ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000560ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_TX_IFG(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000488ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000488ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000488ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000488ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_TX_INT_EN(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000508ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000508ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000508ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000508ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_TX_INT_REG(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000500ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000500ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000500ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000500ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_TX_JAM(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000490ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000490ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000490ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000490ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_TX_LFSR(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080004F8ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080004F8ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080004F8ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800080004F8ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_TX_OVR_BP(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080004C8ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080004C8ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080004C8ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800080004C8ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_TX_PAUSE_PKT_DMAC(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080004A0ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080004A0ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080004A0ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800080004A0ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_TX_PAUSE_PKT_TYPE(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080004A8ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080004A8ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800080004A8ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800080004A8ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_TX_PRTS(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000480ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000480ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000480ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000480ull) + (block_id) * 0x8000000ull;
}

#define CVMX_GMXX_TX_SPI_CTL(block_id) (CVMX_ADD_IO_SEG(0x00011800080004C0ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_GMXX_TX_SPI_DRAIN(block_id) (CVMX_ADD_IO_SEG(0x00011800080004E0ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_GMXX_TX_SPI_MAX(block_id) (CVMX_ADD_IO_SEG(0x00011800080004B0ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_GMXX_TX_SPI_ROUNDX(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180008000680ull) + (((offset) & 31) + ((block_id) & 1) * 0x1000000ull) * 8)
#define CVMX_GMXX_TX_SPI_THRESH(block_id) (CVMX_ADD_IO_SEG(0x00011800080004B8ull) + ((block_id) & 1) * 0x8000000ull)
static inline uint64_t CVMX_GMXX_TX_XAUI_CTL(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000528ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000528ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000528ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000528ull) + (block_id) * 0x8000000ull;
}

static inline uint64_t CVMX_GMXX_XAUI_EXT_LOOPBACK(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000540ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000540ull) + (block_id) * 0x8000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180008000540ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180008000540ull) + (block_id) * 0x8000000ull;
}

union cvmx_gmxx_bad_reg {
	uint64_t u64;
	struct cvmx_gmxx_bad_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_31_63:33;
		uint64_t inb_nxa:4;
		uint64_t statovr:1;
		uint64_t loststat:4;
		uint64_t reserved_18_21:4;
		uint64_t out_ovr:16;
		uint64_t ncb_ovr:1;
		uint64_t out_col:1;
#else
		uint64_t out_col:1;
		uint64_t ncb_ovr:1;
		uint64_t out_ovr:16;
		uint64_t reserved_18_21:4;
		uint64_t loststat:4;
		uint64_t statovr:1;
		uint64_t inb_nxa:4;
		uint64_t reserved_31_63:33;
#endif
	} s;
	struct cvmx_gmxx_bad_reg_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_31_63:33;
		uint64_t inb_nxa:4;
		uint64_t statovr:1;
		uint64_t reserved_25_25:1;
		uint64_t loststat:3;
		uint64_t reserved_5_21:17;
		uint64_t out_ovr:3;
		uint64_t reserved_0_1:2;
#else
		uint64_t reserved_0_1:2;
		uint64_t out_ovr:3;
		uint64_t reserved_5_21:17;
		uint64_t loststat:3;
		uint64_t reserved_25_25:1;
		uint64_t statovr:1;
		uint64_t inb_nxa:4;
		uint64_t reserved_31_63:33;
#endif
	} cn30xx;
	struct cvmx_gmxx_bad_reg_cn30xx cn31xx;
	struct cvmx_gmxx_bad_reg_s cn38xx;
	struct cvmx_gmxx_bad_reg_s cn38xxp2;
	struct cvmx_gmxx_bad_reg_cn30xx cn50xx;
	struct cvmx_gmxx_bad_reg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_31_63:33;
		uint64_t inb_nxa:4;
		uint64_t statovr:1;
		uint64_t loststat:4;
		uint64_t reserved_6_21:16;
		uint64_t out_ovr:4;
		uint64_t reserved_0_1:2;
#else
		uint64_t reserved_0_1:2;
		uint64_t out_ovr:4;
		uint64_t reserved_6_21:16;
		uint64_t loststat:4;
		uint64_t statovr:1;
		uint64_t inb_nxa:4;
		uint64_t reserved_31_63:33;
#endif
	} cn52xx;
	struct cvmx_gmxx_bad_reg_cn52xx cn52xxp1;
	struct cvmx_gmxx_bad_reg_cn52xx cn56xx;
	struct cvmx_gmxx_bad_reg_cn52xx cn56xxp1;
	struct cvmx_gmxx_bad_reg_s cn58xx;
	struct cvmx_gmxx_bad_reg_s cn58xxp1;
	struct cvmx_gmxx_bad_reg_cn52xx cn61xx;
	struct cvmx_gmxx_bad_reg_cn52xx cn63xx;
	struct cvmx_gmxx_bad_reg_cn52xx cn63xxp1;
	struct cvmx_gmxx_bad_reg_cn52xx cn66xx;
	struct cvmx_gmxx_bad_reg_cn52xx cn68xx;
	struct cvmx_gmxx_bad_reg_cn52xx cn68xxp1;
	struct cvmx_gmxx_bad_reg_cn52xx cnf71xx;
};

union cvmx_gmxx_bist {
	uint64_t u64;
	struct cvmx_gmxx_bist_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_25_63:39;
		uint64_t status:25;
#else
		uint64_t status:25;
		uint64_t reserved_25_63:39;
#endif
	} s;
	struct cvmx_gmxx_bist_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t status:10;
#else
		uint64_t status:10;
		uint64_t reserved_10_63:54;
#endif
	} cn30xx;
	struct cvmx_gmxx_bist_cn30xx cn31xx;
	struct cvmx_gmxx_bist_cn30xx cn38xx;
	struct cvmx_gmxx_bist_cn30xx cn38xxp2;
	struct cvmx_gmxx_bist_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t status:12;
#else
		uint64_t status:12;
		uint64_t reserved_12_63:52;
#endif
	} cn50xx;
	struct cvmx_gmxx_bist_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t status:16;
#else
		uint64_t status:16;
		uint64_t reserved_16_63:48;
#endif
	} cn52xx;
	struct cvmx_gmxx_bist_cn52xx cn52xxp1;
	struct cvmx_gmxx_bist_cn52xx cn56xx;
	struct cvmx_gmxx_bist_cn52xx cn56xxp1;
	struct cvmx_gmxx_bist_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_17_63:47;
		uint64_t status:17;
#else
		uint64_t status:17;
		uint64_t reserved_17_63:47;
#endif
	} cn58xx;
	struct cvmx_gmxx_bist_cn58xx cn58xxp1;
	struct cvmx_gmxx_bist_s cn61xx;
	struct cvmx_gmxx_bist_s cn63xx;
	struct cvmx_gmxx_bist_s cn63xxp1;
	struct cvmx_gmxx_bist_s cn66xx;
	struct cvmx_gmxx_bist_s cn68xx;
	struct cvmx_gmxx_bist_s cn68xxp1;
	struct cvmx_gmxx_bist_s cnf71xx;
};

union cvmx_gmxx_bpid_mapx {
	uint64_t u64;
	struct cvmx_gmxx_bpid_mapx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_17_63:47;
		uint64_t status:1;
		uint64_t reserved_9_15:7;
		uint64_t val:1;
		uint64_t reserved_6_7:2;
		uint64_t bpid:6;
#else
		uint64_t bpid:6;
		uint64_t reserved_6_7:2;
		uint64_t val:1;
		uint64_t reserved_9_15:7;
		uint64_t status:1;
		uint64_t reserved_17_63:47;
#endif
	} s;
	struct cvmx_gmxx_bpid_mapx_s cn68xx;
	struct cvmx_gmxx_bpid_mapx_s cn68xxp1;
};

union cvmx_gmxx_bpid_msk {
	uint64_t u64;
	struct cvmx_gmxx_bpid_msk_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t msk_or:16;
		uint64_t reserved_16_31:16;
		uint64_t msk_and:16;
#else
		uint64_t msk_and:16;
		uint64_t reserved_16_31:16;
		uint64_t msk_or:16;
		uint64_t reserved_48_63:16;
#endif
	} s;
	struct cvmx_gmxx_bpid_msk_s cn68xx;
	struct cvmx_gmxx_bpid_msk_s cn68xxp1;
};

union cvmx_gmxx_clk_en {
	uint64_t u64;
	struct cvmx_gmxx_clk_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t clk_en:1;
#else
		uint64_t clk_en:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_gmxx_clk_en_s cn52xx;
	struct cvmx_gmxx_clk_en_s cn52xxp1;
	struct cvmx_gmxx_clk_en_s cn56xx;
	struct cvmx_gmxx_clk_en_s cn56xxp1;
	struct cvmx_gmxx_clk_en_s cn61xx;
	struct cvmx_gmxx_clk_en_s cn63xx;
	struct cvmx_gmxx_clk_en_s cn63xxp1;
	struct cvmx_gmxx_clk_en_s cn66xx;
	struct cvmx_gmxx_clk_en_s cn68xx;
	struct cvmx_gmxx_clk_en_s cn68xxp1;
	struct cvmx_gmxx_clk_en_s cnf71xx;
};

union cvmx_gmxx_ebp_dis {
	uint64_t u64;
	struct cvmx_gmxx_ebp_dis_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t dis:16;
#else
		uint64_t dis:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_gmxx_ebp_dis_s cn68xx;
	struct cvmx_gmxx_ebp_dis_s cn68xxp1;
};

union cvmx_gmxx_ebp_msk {
	uint64_t u64;
	struct cvmx_gmxx_ebp_msk_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t msk:16;
#else
		uint64_t msk:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_gmxx_ebp_msk_s cn68xx;
	struct cvmx_gmxx_ebp_msk_s cn68xxp1;
};

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
	struct cvmx_gmxx_hg2_control_s cn52xx;
	struct cvmx_gmxx_hg2_control_s cn52xxp1;
	struct cvmx_gmxx_hg2_control_s cn56xx;
	struct cvmx_gmxx_hg2_control_s cn61xx;
	struct cvmx_gmxx_hg2_control_s cn63xx;
	struct cvmx_gmxx_hg2_control_s cn63xxp1;
	struct cvmx_gmxx_hg2_control_s cn66xx;
	struct cvmx_gmxx_hg2_control_s cn68xx;
	struct cvmx_gmxx_hg2_control_s cn68xxp1;
	struct cvmx_gmxx_hg2_control_s cnf71xx;
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
	struct cvmx_gmxx_inf_mode_cn31xx cn38xx;
	struct cvmx_gmxx_inf_mode_cn31xx cn38xxp2;
	struct cvmx_gmxx_inf_mode_cn30xx cn50xx;
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
	struct cvmx_gmxx_inf_mode_cn52xx cn52xxp1;
	struct cvmx_gmxx_inf_mode_cn52xx cn56xx;
	struct cvmx_gmxx_inf_mode_cn52xx cn56xxp1;
	struct cvmx_gmxx_inf_mode_cn31xx cn58xx;
	struct cvmx_gmxx_inf_mode_cn31xx cn58xxp1;
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
	struct cvmx_gmxx_inf_mode_cn61xx cn63xx;
	struct cvmx_gmxx_inf_mode_cn61xx cn63xxp1;
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
	struct cvmx_gmxx_inf_mode_cn68xx cn68xxp1;
	struct cvmx_gmxx_inf_mode_cn61xx cnf71xx;
};

union cvmx_gmxx_nxa_adr {
	uint64_t u64;
	struct cvmx_gmxx_nxa_adr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_23_63:41;
		uint64_t pipe:7;
		uint64_t reserved_6_15:10;
		uint64_t prt:6;
#else
		uint64_t prt:6;
		uint64_t reserved_6_15:10;
		uint64_t pipe:7;
		uint64_t reserved_23_63:41;
#endif
	} s;
	struct cvmx_gmxx_nxa_adr_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t prt:6;
#else
		uint64_t prt:6;
		uint64_t reserved_6_63:58;
#endif
	} cn30xx;
	struct cvmx_gmxx_nxa_adr_cn30xx cn31xx;
	struct cvmx_gmxx_nxa_adr_cn30xx cn38xx;
	struct cvmx_gmxx_nxa_adr_cn30xx cn38xxp2;
	struct cvmx_gmxx_nxa_adr_cn30xx cn50xx;
	struct cvmx_gmxx_nxa_adr_cn30xx cn52xx;
	struct cvmx_gmxx_nxa_adr_cn30xx cn52xxp1;
	struct cvmx_gmxx_nxa_adr_cn30xx cn56xx;
	struct cvmx_gmxx_nxa_adr_cn30xx cn56xxp1;
	struct cvmx_gmxx_nxa_adr_cn30xx cn58xx;
	struct cvmx_gmxx_nxa_adr_cn30xx cn58xxp1;
	struct cvmx_gmxx_nxa_adr_cn30xx cn61xx;
	struct cvmx_gmxx_nxa_adr_cn30xx cn63xx;
	struct cvmx_gmxx_nxa_adr_cn30xx cn63xxp1;
	struct cvmx_gmxx_nxa_adr_cn30xx cn66xx;
	struct cvmx_gmxx_nxa_adr_s cn68xx;
	struct cvmx_gmxx_nxa_adr_s cn68xxp1;
	struct cvmx_gmxx_nxa_adr_cn30xx cnf71xx;
};

union cvmx_gmxx_pipe_status {
	uint64_t u64;
	struct cvmx_gmxx_pipe_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t ovr:4;
		uint64_t reserved_12_15:4;
		uint64_t bp:4;
		uint64_t reserved_4_7:4;
		uint64_t stop:4;
#else
		uint64_t stop:4;
		uint64_t reserved_4_7:4;
		uint64_t bp:4;
		uint64_t reserved_12_15:4;
		uint64_t ovr:4;
		uint64_t reserved_20_63:44;
#endif
	} s;
	struct cvmx_gmxx_pipe_status_s cn68xx;
	struct cvmx_gmxx_pipe_status_s cn68xxp1;
};

union cvmx_gmxx_prtx_cbfc_ctl {
	uint64_t u64;
	struct cvmx_gmxx_prtx_cbfc_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t phys_en:16;
		uint64_t logl_en:16;
		uint64_t phys_bp:16;
		uint64_t reserved_4_15:12;
		uint64_t bck_en:1;
		uint64_t drp_en:1;
		uint64_t tx_en:1;
		uint64_t rx_en:1;
#else
		uint64_t rx_en:1;
		uint64_t tx_en:1;
		uint64_t drp_en:1;
		uint64_t bck_en:1;
		uint64_t reserved_4_15:12;
		uint64_t phys_bp:16;
		uint64_t logl_en:16;
		uint64_t phys_en:16;
#endif
	} s;
	struct cvmx_gmxx_prtx_cbfc_ctl_s cn52xx;
	struct cvmx_gmxx_prtx_cbfc_ctl_s cn56xx;
	struct cvmx_gmxx_prtx_cbfc_ctl_s cn61xx;
	struct cvmx_gmxx_prtx_cbfc_ctl_s cn63xx;
	struct cvmx_gmxx_prtx_cbfc_ctl_s cn63xxp1;
	struct cvmx_gmxx_prtx_cbfc_ctl_s cn66xx;
	struct cvmx_gmxx_prtx_cbfc_ctl_s cn68xx;
	struct cvmx_gmxx_prtx_cbfc_ctl_s cn68xxp1;
	struct cvmx_gmxx_prtx_cbfc_ctl_s cnf71xx;
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
	struct cvmx_gmxx_prtx_cfg_cn30xx cn31xx;
	struct cvmx_gmxx_prtx_cfg_cn30xx cn38xx;
	struct cvmx_gmxx_prtx_cfg_cn30xx cn38xxp2;
	struct cvmx_gmxx_prtx_cfg_cn30xx cn50xx;
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
	struct cvmx_gmxx_prtx_cfg_cn52xx cn52xxp1;
	struct cvmx_gmxx_prtx_cfg_cn52xx cn56xx;
	struct cvmx_gmxx_prtx_cfg_cn52xx cn56xxp1;
	struct cvmx_gmxx_prtx_cfg_cn30xx cn58xx;
	struct cvmx_gmxx_prtx_cfg_cn30xx cn58xxp1;
	struct cvmx_gmxx_prtx_cfg_cn52xx cn61xx;
	struct cvmx_gmxx_prtx_cfg_cn52xx cn63xx;
	struct cvmx_gmxx_prtx_cfg_cn52xx cn63xxp1;
	struct cvmx_gmxx_prtx_cfg_cn52xx cn66xx;
	struct cvmx_gmxx_prtx_cfg_s cn68xx;
	struct cvmx_gmxx_prtx_cfg_s cn68xxp1;
	struct cvmx_gmxx_prtx_cfg_cn52xx cnf71xx;
};

union cvmx_gmxx_rxx_adr_cam0 {
	uint64_t u64;
	struct cvmx_gmxx_rxx_adr_cam0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t adr:64;
#else
		uint64_t adr:64;
#endif
	} s;
	struct cvmx_gmxx_rxx_adr_cam0_s cn30xx;
	struct cvmx_gmxx_rxx_adr_cam0_s cn31xx;
	struct cvmx_gmxx_rxx_adr_cam0_s cn38xx;
	struct cvmx_gmxx_rxx_adr_cam0_s cn38xxp2;
	struct cvmx_gmxx_rxx_adr_cam0_s cn50xx;
	struct cvmx_gmxx_rxx_adr_cam0_s cn52xx;
	struct cvmx_gmxx_rxx_adr_cam0_s cn52xxp1;
	struct cvmx_gmxx_rxx_adr_cam0_s cn56xx;
	struct cvmx_gmxx_rxx_adr_cam0_s cn56xxp1;
	struct cvmx_gmxx_rxx_adr_cam0_s cn58xx;
	struct cvmx_gmxx_rxx_adr_cam0_s cn58xxp1;
	struct cvmx_gmxx_rxx_adr_cam0_s cn61xx;
	struct cvmx_gmxx_rxx_adr_cam0_s cn63xx;
	struct cvmx_gmxx_rxx_adr_cam0_s cn63xxp1;
	struct cvmx_gmxx_rxx_adr_cam0_s cn66xx;
	struct cvmx_gmxx_rxx_adr_cam0_s cn68xx;
	struct cvmx_gmxx_rxx_adr_cam0_s cn68xxp1;
	struct cvmx_gmxx_rxx_adr_cam0_s cnf71xx;
};

union cvmx_gmxx_rxx_adr_cam1 {
	uint64_t u64;
	struct cvmx_gmxx_rxx_adr_cam1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t adr:64;
#else
		uint64_t adr:64;
#endif
	} s;
	struct cvmx_gmxx_rxx_adr_cam1_s cn30xx;
	struct cvmx_gmxx_rxx_adr_cam1_s cn31xx;
	struct cvmx_gmxx_rxx_adr_cam1_s cn38xx;
	struct cvmx_gmxx_rxx_adr_cam1_s cn38xxp2;
	struct cvmx_gmxx_rxx_adr_cam1_s cn50xx;
	struct cvmx_gmxx_rxx_adr_cam1_s cn52xx;
	struct cvmx_gmxx_rxx_adr_cam1_s cn52xxp1;
	struct cvmx_gmxx_rxx_adr_cam1_s cn56xx;
	struct cvmx_gmxx_rxx_adr_cam1_s cn56xxp1;
	struct cvmx_gmxx_rxx_adr_cam1_s cn58xx;
	struct cvmx_gmxx_rxx_adr_cam1_s cn58xxp1;
	struct cvmx_gmxx_rxx_adr_cam1_s cn61xx;
	struct cvmx_gmxx_rxx_adr_cam1_s cn63xx;
	struct cvmx_gmxx_rxx_adr_cam1_s cn63xxp1;
	struct cvmx_gmxx_rxx_adr_cam1_s cn66xx;
	struct cvmx_gmxx_rxx_adr_cam1_s cn68xx;
	struct cvmx_gmxx_rxx_adr_cam1_s cn68xxp1;
	struct cvmx_gmxx_rxx_adr_cam1_s cnf71xx;
};

union cvmx_gmxx_rxx_adr_cam2 {
	uint64_t u64;
	struct cvmx_gmxx_rxx_adr_cam2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t adr:64;
#else
		uint64_t adr:64;
#endif
	} s;
	struct cvmx_gmxx_rxx_adr_cam2_s cn30xx;
	struct cvmx_gmxx_rxx_adr_cam2_s cn31xx;
	struct cvmx_gmxx_rxx_adr_cam2_s cn38xx;
	struct cvmx_gmxx_rxx_adr_cam2_s cn38xxp2;
	struct cvmx_gmxx_rxx_adr_cam2_s cn50xx;
	struct cvmx_gmxx_rxx_adr_cam2_s cn52xx;
	struct cvmx_gmxx_rxx_adr_cam2_s cn52xxp1;
	struct cvmx_gmxx_rxx_adr_cam2_s cn56xx;
	struct cvmx_gmxx_rxx_adr_cam2_s cn56xxp1;
	struct cvmx_gmxx_rxx_adr_cam2_s cn58xx;
	struct cvmx_gmxx_rxx_adr_cam2_s cn58xxp1;
	struct cvmx_gmxx_rxx_adr_cam2_s cn61xx;
	struct cvmx_gmxx_rxx_adr_cam2_s cn63xx;
	struct cvmx_gmxx_rxx_adr_cam2_s cn63xxp1;
	struct cvmx_gmxx_rxx_adr_cam2_s cn66xx;
	struct cvmx_gmxx_rxx_adr_cam2_s cn68xx;
	struct cvmx_gmxx_rxx_adr_cam2_s cn68xxp1;
	struct cvmx_gmxx_rxx_adr_cam2_s cnf71xx;
};

union cvmx_gmxx_rxx_adr_cam3 {
	uint64_t u64;
	struct cvmx_gmxx_rxx_adr_cam3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t adr:64;
#else
		uint64_t adr:64;
#endif
	} s;
	struct cvmx_gmxx_rxx_adr_cam3_s cn30xx;
	struct cvmx_gmxx_rxx_adr_cam3_s cn31xx;
	struct cvmx_gmxx_rxx_adr_cam3_s cn38xx;
	struct cvmx_gmxx_rxx_adr_cam3_s cn38xxp2;
	struct cvmx_gmxx_rxx_adr_cam3_s cn50xx;
	struct cvmx_gmxx_rxx_adr_cam3_s cn52xx;
	struct cvmx_gmxx_rxx_adr_cam3_s cn52xxp1;
	struct cvmx_gmxx_rxx_adr_cam3_s cn56xx;
	struct cvmx_gmxx_rxx_adr_cam3_s cn56xxp1;
	struct cvmx_gmxx_rxx_adr_cam3_s cn58xx;
	struct cvmx_gmxx_rxx_adr_cam3_s cn58xxp1;
	struct cvmx_gmxx_rxx_adr_cam3_s cn61xx;
	struct cvmx_gmxx_rxx_adr_cam3_s cn63xx;
	struct cvmx_gmxx_rxx_adr_cam3_s cn63xxp1;
	struct cvmx_gmxx_rxx_adr_cam3_s cn66xx;
	struct cvmx_gmxx_rxx_adr_cam3_s cn68xx;
	struct cvmx_gmxx_rxx_adr_cam3_s cn68xxp1;
	struct cvmx_gmxx_rxx_adr_cam3_s cnf71xx;
};

union cvmx_gmxx_rxx_adr_cam4 {
	uint64_t u64;
	struct cvmx_gmxx_rxx_adr_cam4_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t adr:64;
#else
		uint64_t adr:64;
#endif
	} s;
	struct cvmx_gmxx_rxx_adr_cam4_s cn30xx;
	struct cvmx_gmxx_rxx_adr_cam4_s cn31xx;
	struct cvmx_gmxx_rxx_adr_cam4_s cn38xx;
	struct cvmx_gmxx_rxx_adr_cam4_s cn38xxp2;
	struct cvmx_gmxx_rxx_adr_cam4_s cn50xx;
	struct cvmx_gmxx_rxx_adr_cam4_s cn52xx;
	struct cvmx_gmxx_rxx_adr_cam4_s cn52xxp1;
	struct cvmx_gmxx_rxx_adr_cam4_s cn56xx;
	struct cvmx_gmxx_rxx_adr_cam4_s cn56xxp1;
	struct cvmx_gmxx_rxx_adr_cam4_s cn58xx;
	struct cvmx_gmxx_rxx_adr_cam4_s cn58xxp1;
	struct cvmx_gmxx_rxx_adr_cam4_s cn61xx;
	struct cvmx_gmxx_rxx_adr_cam4_s cn63xx;
	struct cvmx_gmxx_rxx_adr_cam4_s cn63xxp1;
	struct cvmx_gmxx_rxx_adr_cam4_s cn66xx;
	struct cvmx_gmxx_rxx_adr_cam4_s cn68xx;
	struct cvmx_gmxx_rxx_adr_cam4_s cn68xxp1;
	struct cvmx_gmxx_rxx_adr_cam4_s cnf71xx;
};

union cvmx_gmxx_rxx_adr_cam5 {
	uint64_t u64;
	struct cvmx_gmxx_rxx_adr_cam5_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t adr:64;
#else
		uint64_t adr:64;
#endif
	} s;
	struct cvmx_gmxx_rxx_adr_cam5_s cn30xx;
	struct cvmx_gmxx_rxx_adr_cam5_s cn31xx;
	struct cvmx_gmxx_rxx_adr_cam5_s cn38xx;
	struct cvmx_gmxx_rxx_adr_cam5_s cn38xxp2;
	struct cvmx_gmxx_rxx_adr_cam5_s cn50xx;
	struct cvmx_gmxx_rxx_adr_cam5_s cn52xx;
	struct cvmx_gmxx_rxx_adr_cam5_s cn52xxp1;
	struct cvmx_gmxx_rxx_adr_cam5_s cn56xx;
	struct cvmx_gmxx_rxx_adr_cam5_s cn56xxp1;
	struct cvmx_gmxx_rxx_adr_cam5_s cn58xx;
	struct cvmx_gmxx_rxx_adr_cam5_s cn58xxp1;
	struct cvmx_gmxx_rxx_adr_cam5_s cn61xx;
	struct cvmx_gmxx_rxx_adr_cam5_s cn63xx;
	struct cvmx_gmxx_rxx_adr_cam5_s cn63xxp1;
	struct cvmx_gmxx_rxx_adr_cam5_s cn66xx;
	struct cvmx_gmxx_rxx_adr_cam5_s cn68xx;
	struct cvmx_gmxx_rxx_adr_cam5_s cn68xxp1;
	struct cvmx_gmxx_rxx_adr_cam5_s cnf71xx;
};

union cvmx_gmxx_rxx_adr_cam_all_en {
	uint64_t u64;
	struct cvmx_gmxx_rxx_adr_cam_all_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t en:32;
#else
		uint64_t en:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_gmxx_rxx_adr_cam_all_en_s cn61xx;
	struct cvmx_gmxx_rxx_adr_cam_all_en_s cn66xx;
	struct cvmx_gmxx_rxx_adr_cam_all_en_s cn68xx;
	struct cvmx_gmxx_rxx_adr_cam_all_en_s cnf71xx;
};

union cvmx_gmxx_rxx_adr_cam_en {
	uint64_t u64;
	struct cvmx_gmxx_rxx_adr_cam_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t en:8;
#else
		uint64_t en:8;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_gmxx_rxx_adr_cam_en_s cn30xx;
	struct cvmx_gmxx_rxx_adr_cam_en_s cn31xx;
	struct cvmx_gmxx_rxx_adr_cam_en_s cn38xx;
	struct cvmx_gmxx_rxx_adr_cam_en_s cn38xxp2;
	struct cvmx_gmxx_rxx_adr_cam_en_s cn50xx;
	struct cvmx_gmxx_rxx_adr_cam_en_s cn52xx;
	struct cvmx_gmxx_rxx_adr_cam_en_s cn52xxp1;
	struct cvmx_gmxx_rxx_adr_cam_en_s cn56xx;
	struct cvmx_gmxx_rxx_adr_cam_en_s cn56xxp1;
	struct cvmx_gmxx_rxx_adr_cam_en_s cn58xx;
	struct cvmx_gmxx_rxx_adr_cam_en_s cn58xxp1;
	struct cvmx_gmxx_rxx_adr_cam_en_s cn61xx;
	struct cvmx_gmxx_rxx_adr_cam_en_s cn63xx;
	struct cvmx_gmxx_rxx_adr_cam_en_s cn63xxp1;
	struct cvmx_gmxx_rxx_adr_cam_en_s cn66xx;
	struct cvmx_gmxx_rxx_adr_cam_en_s cn68xx;
	struct cvmx_gmxx_rxx_adr_cam_en_s cn68xxp1;
	struct cvmx_gmxx_rxx_adr_cam_en_s cnf71xx;
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
	struct cvmx_gmxx_rxx_adr_ctl_s cn30xx;
	struct cvmx_gmxx_rxx_adr_ctl_s cn31xx;
	struct cvmx_gmxx_rxx_adr_ctl_s cn38xx;
	struct cvmx_gmxx_rxx_adr_ctl_s cn38xxp2;
	struct cvmx_gmxx_rxx_adr_ctl_s cn50xx;
	struct cvmx_gmxx_rxx_adr_ctl_s cn52xx;
	struct cvmx_gmxx_rxx_adr_ctl_s cn52xxp1;
	struct cvmx_gmxx_rxx_adr_ctl_s cn56xx;
	struct cvmx_gmxx_rxx_adr_ctl_s cn56xxp1;
	struct cvmx_gmxx_rxx_adr_ctl_s cn58xx;
	struct cvmx_gmxx_rxx_adr_ctl_s cn58xxp1;
	struct cvmx_gmxx_rxx_adr_ctl_s cn61xx;
	struct cvmx_gmxx_rxx_adr_ctl_s cn63xx;
	struct cvmx_gmxx_rxx_adr_ctl_s cn63xxp1;
	struct cvmx_gmxx_rxx_adr_ctl_s cn66xx;
	struct cvmx_gmxx_rxx_adr_ctl_s cn68xx;
	struct cvmx_gmxx_rxx_adr_ctl_s cn68xxp1;
	struct cvmx_gmxx_rxx_adr_ctl_s cnf71xx;
};

union cvmx_gmxx_rxx_decision {
	uint64_t u64;
	struct cvmx_gmxx_rxx_decision_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_5_63:59;
		uint64_t cnt:5;
#else
		uint64_t cnt:5;
		uint64_t reserved_5_63:59;
#endif
	} s;
	struct cvmx_gmxx_rxx_decision_s cn30xx;
	struct cvmx_gmxx_rxx_decision_s cn31xx;
	struct cvmx_gmxx_rxx_decision_s cn38xx;
	struct cvmx_gmxx_rxx_decision_s cn38xxp2;
	struct cvmx_gmxx_rxx_decision_s cn50xx;
	struct cvmx_gmxx_rxx_decision_s cn52xx;
	struct cvmx_gmxx_rxx_decision_s cn52xxp1;
	struct cvmx_gmxx_rxx_decision_s cn56xx;
	struct cvmx_gmxx_rxx_decision_s cn56xxp1;
	struct cvmx_gmxx_rxx_decision_s cn58xx;
	struct cvmx_gmxx_rxx_decision_s cn58xxp1;
	struct cvmx_gmxx_rxx_decision_s cn61xx;
	struct cvmx_gmxx_rxx_decision_s cn63xx;
	struct cvmx_gmxx_rxx_decision_s cn63xxp1;
	struct cvmx_gmxx_rxx_decision_s cn66xx;
	struct cvmx_gmxx_rxx_decision_s cn68xx;
	struct cvmx_gmxx_rxx_decision_s cn68xxp1;
	struct cvmx_gmxx_rxx_decision_s cnf71xx;
};

union cvmx_gmxx_rxx_frm_chk {
	uint64_t u64;
	struct cvmx_gmxx_rxx_frm_chk_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
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
		uint64_t reserved_10_63:54;
#endif
	} s;
	struct cvmx_gmxx_rxx_frm_chk_s cn30xx;
	struct cvmx_gmxx_rxx_frm_chk_s cn31xx;
	struct cvmx_gmxx_rxx_frm_chk_s cn38xx;
	struct cvmx_gmxx_rxx_frm_chk_s cn38xxp2;
	struct cvmx_gmxx_rxx_frm_chk_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
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
		uint64_t reserved_10_63:54;
#endif
	} cn50xx;
	struct cvmx_gmxx_rxx_frm_chk_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
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
		uint64_t reserved_9_63:55;
#endif
	} cn52xx;
	struct cvmx_gmxx_rxx_frm_chk_cn52xx cn52xxp1;
	struct cvmx_gmxx_rxx_frm_chk_cn52xx cn56xx;
	struct cvmx_gmxx_rxx_frm_chk_cn52xx cn56xxp1;
	struct cvmx_gmxx_rxx_frm_chk_s cn58xx;
	struct cvmx_gmxx_rxx_frm_chk_s cn58xxp1;
	struct cvmx_gmxx_rxx_frm_chk_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
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
		uint64_t reserved_9_63:55;
#endif
	} cn61xx;
	struct cvmx_gmxx_rxx_frm_chk_cn61xx cn63xx;
	struct cvmx_gmxx_rxx_frm_chk_cn61xx cn63xxp1;
	struct cvmx_gmxx_rxx_frm_chk_cn61xx cn66xx;
	struct cvmx_gmxx_rxx_frm_chk_cn61xx cn68xx;
	struct cvmx_gmxx_rxx_frm_chk_cn61xx cn68xxp1;
	struct cvmx_gmxx_rxx_frm_chk_cn61xx cnf71xx;
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
	struct cvmx_gmxx_rxx_frm_ctl_cn30xx cn38xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn31xx cn38xxp2;
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
	struct cvmx_gmxx_rxx_frm_ctl_cn50xx cn52xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn50xx cn52xxp1;
	struct cvmx_gmxx_rxx_frm_ctl_cn50xx cn56xx;
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
	struct cvmx_gmxx_rxx_frm_ctl_cn30xx cn58xxp1;
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
	struct cvmx_gmxx_rxx_frm_ctl_cn61xx cn63xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn61xx cn63xxp1;
	struct cvmx_gmxx_rxx_frm_ctl_cn61xx cn66xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn61xx cn68xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn61xx cn68xxp1;
	struct cvmx_gmxx_rxx_frm_ctl_cn61xx cnf71xx;
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
	struct cvmx_gmxx_rxx_frm_max_s cn30xx;
	struct cvmx_gmxx_rxx_frm_max_s cn31xx;
	struct cvmx_gmxx_rxx_frm_max_s cn38xx;
	struct cvmx_gmxx_rxx_frm_max_s cn38xxp2;
	struct cvmx_gmxx_rxx_frm_max_s cn58xx;
	struct cvmx_gmxx_rxx_frm_max_s cn58xxp1;
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
	struct cvmx_gmxx_rxx_frm_min_s cn30xx;
	struct cvmx_gmxx_rxx_frm_min_s cn31xx;
	struct cvmx_gmxx_rxx_frm_min_s cn38xx;
	struct cvmx_gmxx_rxx_frm_min_s cn38xxp2;
	struct cvmx_gmxx_rxx_frm_min_s cn58xx;
	struct cvmx_gmxx_rxx_frm_min_s cn58xxp1;
};

union cvmx_gmxx_rxx_ifg {
	uint64_t u64;
	struct cvmx_gmxx_rxx_ifg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t ifg:4;
#else
		uint64_t ifg:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_gmxx_rxx_ifg_s cn30xx;
	struct cvmx_gmxx_rxx_ifg_s cn31xx;
	struct cvmx_gmxx_rxx_ifg_s cn38xx;
	struct cvmx_gmxx_rxx_ifg_s cn38xxp2;
	struct cvmx_gmxx_rxx_ifg_s cn50xx;
	struct cvmx_gmxx_rxx_ifg_s cn52xx;
	struct cvmx_gmxx_rxx_ifg_s cn52xxp1;
	struct cvmx_gmxx_rxx_ifg_s cn56xx;
	struct cvmx_gmxx_rxx_ifg_s cn56xxp1;
	struct cvmx_gmxx_rxx_ifg_s cn58xx;
	struct cvmx_gmxx_rxx_ifg_s cn58xxp1;
	struct cvmx_gmxx_rxx_ifg_s cn61xx;
	struct cvmx_gmxx_rxx_ifg_s cn63xx;
	struct cvmx_gmxx_rxx_ifg_s cn63xxp1;
	struct cvmx_gmxx_rxx_ifg_s cn66xx;
	struct cvmx_gmxx_rxx_ifg_s cn68xx;
	struct cvmx_gmxx_rxx_ifg_s cn68xxp1;
	struct cvmx_gmxx_rxx_ifg_s cnf71xx;
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
	struct cvmx_gmxx_rxx_int_en_cn30xx cn31xx;
	struct cvmx_gmxx_rxx_int_en_cn30xx cn38xx;
	struct cvmx_gmxx_rxx_int_en_cn30xx cn38xxp2;
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
	struct cvmx_gmxx_rxx_int_en_cn52xx cn52xxp1;
	struct cvmx_gmxx_rxx_int_en_cn52xx cn56xx;
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
	struct cvmx_gmxx_rxx_int_en_cn58xx cn58xxp1;
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
	struct cvmx_gmxx_rxx_int_en_cn61xx cn63xx;
	struct cvmx_gmxx_rxx_int_en_cn61xx cn63xxp1;
	struct cvmx_gmxx_rxx_int_en_cn61xx cn66xx;
	struct cvmx_gmxx_rxx_int_en_cn61xx cn68xx;
	struct cvmx_gmxx_rxx_int_en_cn61xx cn68xxp1;
	struct cvmx_gmxx_rxx_int_en_cn61xx cnf71xx;
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
	struct cvmx_gmxx_rxx_int_reg_cn30xx cn31xx;
	struct cvmx_gmxx_rxx_int_reg_cn30xx cn38xx;
	struct cvmx_gmxx_rxx_int_reg_cn30xx cn38xxp2;
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
	struct cvmx_gmxx_rxx_int_reg_cn52xx cn52xxp1;
	struct cvmx_gmxx_rxx_int_reg_cn52xx cn56xx;
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
	struct cvmx_gmxx_rxx_int_reg_cn58xx cn58xxp1;
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
	struct cvmx_gmxx_rxx_int_reg_cn61xx cn63xx;
	struct cvmx_gmxx_rxx_int_reg_cn61xx cn63xxp1;
	struct cvmx_gmxx_rxx_int_reg_cn61xx cn66xx;
	struct cvmx_gmxx_rxx_int_reg_cn61xx cn68xx;
	struct cvmx_gmxx_rxx_int_reg_cn61xx cn68xxp1;
	struct cvmx_gmxx_rxx_int_reg_cn61xx cnf71xx;
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
	struct cvmx_gmxx_rxx_jabber_s cn30xx;
	struct cvmx_gmxx_rxx_jabber_s cn31xx;
	struct cvmx_gmxx_rxx_jabber_s cn38xx;
	struct cvmx_gmxx_rxx_jabber_s cn38xxp2;
	struct cvmx_gmxx_rxx_jabber_s cn50xx;
	struct cvmx_gmxx_rxx_jabber_s cn52xx;
	struct cvmx_gmxx_rxx_jabber_s cn52xxp1;
	struct cvmx_gmxx_rxx_jabber_s cn56xx;
	struct cvmx_gmxx_rxx_jabber_s cn56xxp1;
	struct cvmx_gmxx_rxx_jabber_s cn58xx;
	struct cvmx_gmxx_rxx_jabber_s cn58xxp1;
	struct cvmx_gmxx_rxx_jabber_s cn61xx;
	struct cvmx_gmxx_rxx_jabber_s cn63xx;
	struct cvmx_gmxx_rxx_jabber_s cn63xxp1;
	struct cvmx_gmxx_rxx_jabber_s cn66xx;
	struct cvmx_gmxx_rxx_jabber_s cn68xx;
	struct cvmx_gmxx_rxx_jabber_s cn68xxp1;
	struct cvmx_gmxx_rxx_jabber_s cnf71xx;
};

union cvmx_gmxx_rxx_pause_drop_time {
	uint64_t u64;
	struct cvmx_gmxx_rxx_pause_drop_time_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t status:16;
#else
		uint64_t status:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn50xx;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn52xx;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn52xxp1;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn56xx;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn56xxp1;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn58xx;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn58xxp1;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn61xx;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn63xx;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn63xxp1;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn66xx;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn68xx;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn68xxp1;
	struct cvmx_gmxx_rxx_pause_drop_time_s cnf71xx;
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
	struct cvmx_gmxx_rxx_rx_inbnd_s cn30xx;
	struct cvmx_gmxx_rxx_rx_inbnd_s cn31xx;
	struct cvmx_gmxx_rxx_rx_inbnd_s cn38xx;
	struct cvmx_gmxx_rxx_rx_inbnd_s cn38xxp2;
	struct cvmx_gmxx_rxx_rx_inbnd_s cn50xx;
	struct cvmx_gmxx_rxx_rx_inbnd_s cn58xx;
	struct cvmx_gmxx_rxx_rx_inbnd_s cn58xxp1;
};

union cvmx_gmxx_rxx_stats_ctl {
	uint64_t u64;
	struct cvmx_gmxx_rxx_stats_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t rd_clr:1;
#else
		uint64_t rd_clr:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_gmxx_rxx_stats_ctl_s cn30xx;
	struct cvmx_gmxx_rxx_stats_ctl_s cn31xx;
	struct cvmx_gmxx_rxx_stats_ctl_s cn38xx;
	struct cvmx_gmxx_rxx_stats_ctl_s cn38xxp2;
	struct cvmx_gmxx_rxx_stats_ctl_s cn50xx;
	struct cvmx_gmxx_rxx_stats_ctl_s cn52xx;
	struct cvmx_gmxx_rxx_stats_ctl_s cn52xxp1;
	struct cvmx_gmxx_rxx_stats_ctl_s cn56xx;
	struct cvmx_gmxx_rxx_stats_ctl_s cn56xxp1;
	struct cvmx_gmxx_rxx_stats_ctl_s cn58xx;
	struct cvmx_gmxx_rxx_stats_ctl_s cn58xxp1;
	struct cvmx_gmxx_rxx_stats_ctl_s cn61xx;
	struct cvmx_gmxx_rxx_stats_ctl_s cn63xx;
	struct cvmx_gmxx_rxx_stats_ctl_s cn63xxp1;
	struct cvmx_gmxx_rxx_stats_ctl_s cn66xx;
	struct cvmx_gmxx_rxx_stats_ctl_s cn68xx;
	struct cvmx_gmxx_rxx_stats_ctl_s cn68xxp1;
	struct cvmx_gmxx_rxx_stats_ctl_s cnf71xx;
};

union cvmx_gmxx_rxx_stats_octs {
	uint64_t u64;
	struct cvmx_gmxx_rxx_stats_octs_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t cnt:48;
#else
		uint64_t cnt:48;
		uint64_t reserved_48_63:16;
#endif
	} s;
	struct cvmx_gmxx_rxx_stats_octs_s cn30xx;
	struct cvmx_gmxx_rxx_stats_octs_s cn31xx;
	struct cvmx_gmxx_rxx_stats_octs_s cn38xx;
	struct cvmx_gmxx_rxx_stats_octs_s cn38xxp2;
	struct cvmx_gmxx_rxx_stats_octs_s cn50xx;
	struct cvmx_gmxx_rxx_stats_octs_s cn52xx;
	struct cvmx_gmxx_rxx_stats_octs_s cn52xxp1;
	struct cvmx_gmxx_rxx_stats_octs_s cn56xx;
	struct cvmx_gmxx_rxx_stats_octs_s cn56xxp1;
	struct cvmx_gmxx_rxx_stats_octs_s cn58xx;
	struct cvmx_gmxx_rxx_stats_octs_s cn58xxp1;
	struct cvmx_gmxx_rxx_stats_octs_s cn61xx;
	struct cvmx_gmxx_rxx_stats_octs_s cn63xx;
	struct cvmx_gmxx_rxx_stats_octs_s cn63xxp1;
	struct cvmx_gmxx_rxx_stats_octs_s cn66xx;
	struct cvmx_gmxx_rxx_stats_octs_s cn68xx;
	struct cvmx_gmxx_rxx_stats_octs_s cn68xxp1;
	struct cvmx_gmxx_rxx_stats_octs_s cnf71xx;
};

union cvmx_gmxx_rxx_stats_octs_ctl {
	uint64_t u64;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t cnt:48;
#else
		uint64_t cnt:48;
		uint64_t reserved_48_63:16;
#endif
	} s;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn30xx;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn31xx;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn38xx;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn38xxp2;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn50xx;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn52xx;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn52xxp1;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn56xx;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn56xxp1;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn58xx;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn58xxp1;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn61xx;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn63xx;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn63xxp1;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn66xx;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn68xx;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn68xxp1;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cnf71xx;
};

union cvmx_gmxx_rxx_stats_octs_dmac {
	uint64_t u64;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t cnt:48;
#else
		uint64_t cnt:48;
		uint64_t reserved_48_63:16;
#endif
	} s;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn30xx;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn31xx;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn38xx;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn38xxp2;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn50xx;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn52xx;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn52xxp1;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn56xx;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn56xxp1;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn58xx;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn58xxp1;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn61xx;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn63xx;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn63xxp1;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn66xx;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn68xx;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn68xxp1;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cnf71xx;
};

union cvmx_gmxx_rxx_stats_octs_drp {
	uint64_t u64;
	struct cvmx_gmxx_rxx_stats_octs_drp_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t cnt:48;
#else
		uint64_t cnt:48;
		uint64_t reserved_48_63:16;
#endif
	} s;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn30xx;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn31xx;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn38xx;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn38xxp2;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn50xx;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn52xx;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn52xxp1;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn56xx;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn56xxp1;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn58xx;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn58xxp1;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn61xx;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn63xx;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn63xxp1;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn66xx;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn68xx;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn68xxp1;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cnf71xx;
};

union cvmx_gmxx_rxx_stats_pkts {
	uint64_t u64;
	struct cvmx_gmxx_rxx_stats_pkts_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t cnt:32;
#else
		uint64_t cnt:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_gmxx_rxx_stats_pkts_s cn30xx;
	struct cvmx_gmxx_rxx_stats_pkts_s cn31xx;
	struct cvmx_gmxx_rxx_stats_pkts_s cn38xx;
	struct cvmx_gmxx_rxx_stats_pkts_s cn38xxp2;
	struct cvmx_gmxx_rxx_stats_pkts_s cn50xx;
	struct cvmx_gmxx_rxx_stats_pkts_s cn52xx;
	struct cvmx_gmxx_rxx_stats_pkts_s cn52xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_s cn56xx;
	struct cvmx_gmxx_rxx_stats_pkts_s cn56xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_s cn58xx;
	struct cvmx_gmxx_rxx_stats_pkts_s cn58xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_s cn61xx;
	struct cvmx_gmxx_rxx_stats_pkts_s cn63xx;
	struct cvmx_gmxx_rxx_stats_pkts_s cn63xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_s cn66xx;
	struct cvmx_gmxx_rxx_stats_pkts_s cn68xx;
	struct cvmx_gmxx_rxx_stats_pkts_s cn68xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_s cnf71xx;
};

union cvmx_gmxx_rxx_stats_pkts_bad {
	uint64_t u64;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t cnt:32;
#else
		uint64_t cnt:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn30xx;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn31xx;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn38xx;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn38xxp2;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn50xx;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn52xx;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn52xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn56xx;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn56xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn58xx;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn58xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn61xx;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn63xx;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn63xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn66xx;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn68xx;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn68xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cnf71xx;
};

union cvmx_gmxx_rxx_stats_pkts_ctl {
	uint64_t u64;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t cnt:32;
#else
		uint64_t cnt:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn30xx;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn31xx;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn38xx;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn38xxp2;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn50xx;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn52xx;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn52xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn56xx;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn56xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn58xx;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn58xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn61xx;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn63xx;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn63xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn66xx;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn68xx;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn68xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cnf71xx;
};

union cvmx_gmxx_rxx_stats_pkts_dmac {
	uint64_t u64;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t cnt:32;
#else
		uint64_t cnt:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn30xx;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn31xx;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn38xx;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn38xxp2;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn50xx;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn52xx;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn52xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn56xx;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn56xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn58xx;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn58xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn61xx;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn63xx;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn63xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn66xx;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn68xx;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn68xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cnf71xx;
};

union cvmx_gmxx_rxx_stats_pkts_drp {
	uint64_t u64;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t cnt:32;
#else
		uint64_t cnt:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn30xx;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn31xx;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn38xx;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn38xxp2;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn50xx;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn52xx;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn52xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn56xx;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn56xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn58xx;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn58xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn61xx;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn63xx;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn63xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn66xx;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn68xx;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn68xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cnf71xx;
};

union cvmx_gmxx_rxx_udd_skp {
	uint64_t u64;
	struct cvmx_gmxx_rxx_udd_skp_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t fcssel:1;
		uint64_t reserved_7_7:1;
		uint64_t len:7;
#else
		uint64_t len:7;
		uint64_t reserved_7_7:1;
		uint64_t fcssel:1;
		uint64_t reserved_9_63:55;
#endif
	} s;
	struct cvmx_gmxx_rxx_udd_skp_s cn30xx;
	struct cvmx_gmxx_rxx_udd_skp_s cn31xx;
	struct cvmx_gmxx_rxx_udd_skp_s cn38xx;
	struct cvmx_gmxx_rxx_udd_skp_s cn38xxp2;
	struct cvmx_gmxx_rxx_udd_skp_s cn50xx;
	struct cvmx_gmxx_rxx_udd_skp_s cn52xx;
	struct cvmx_gmxx_rxx_udd_skp_s cn52xxp1;
	struct cvmx_gmxx_rxx_udd_skp_s cn56xx;
	struct cvmx_gmxx_rxx_udd_skp_s cn56xxp1;
	struct cvmx_gmxx_rxx_udd_skp_s cn58xx;
	struct cvmx_gmxx_rxx_udd_skp_s cn58xxp1;
	struct cvmx_gmxx_rxx_udd_skp_s cn61xx;
	struct cvmx_gmxx_rxx_udd_skp_s cn63xx;
	struct cvmx_gmxx_rxx_udd_skp_s cn63xxp1;
	struct cvmx_gmxx_rxx_udd_skp_s cn66xx;
	struct cvmx_gmxx_rxx_udd_skp_s cn68xx;
	struct cvmx_gmxx_rxx_udd_skp_s cn68xxp1;
	struct cvmx_gmxx_rxx_udd_skp_s cnf71xx;
};

union cvmx_gmxx_rx_bp_dropx {
	uint64_t u64;
	struct cvmx_gmxx_rx_bp_dropx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t mark:6;
#else
		uint64_t mark:6;
		uint64_t reserved_6_63:58;
#endif
	} s;
	struct cvmx_gmxx_rx_bp_dropx_s cn30xx;
	struct cvmx_gmxx_rx_bp_dropx_s cn31xx;
	struct cvmx_gmxx_rx_bp_dropx_s cn38xx;
	struct cvmx_gmxx_rx_bp_dropx_s cn38xxp2;
	struct cvmx_gmxx_rx_bp_dropx_s cn50xx;
	struct cvmx_gmxx_rx_bp_dropx_s cn52xx;
	struct cvmx_gmxx_rx_bp_dropx_s cn52xxp1;
	struct cvmx_gmxx_rx_bp_dropx_s cn56xx;
	struct cvmx_gmxx_rx_bp_dropx_s cn56xxp1;
	struct cvmx_gmxx_rx_bp_dropx_s cn58xx;
	struct cvmx_gmxx_rx_bp_dropx_s cn58xxp1;
	struct cvmx_gmxx_rx_bp_dropx_s cn61xx;
	struct cvmx_gmxx_rx_bp_dropx_s cn63xx;
	struct cvmx_gmxx_rx_bp_dropx_s cn63xxp1;
	struct cvmx_gmxx_rx_bp_dropx_s cn66xx;
	struct cvmx_gmxx_rx_bp_dropx_s cn68xx;
	struct cvmx_gmxx_rx_bp_dropx_s cn68xxp1;
	struct cvmx_gmxx_rx_bp_dropx_s cnf71xx;
};

union cvmx_gmxx_rx_bp_offx {
	uint64_t u64;
	struct cvmx_gmxx_rx_bp_offx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t mark:6;
#else
		uint64_t mark:6;
		uint64_t reserved_6_63:58;
#endif
	} s;
	struct cvmx_gmxx_rx_bp_offx_s cn30xx;
	struct cvmx_gmxx_rx_bp_offx_s cn31xx;
	struct cvmx_gmxx_rx_bp_offx_s cn38xx;
	struct cvmx_gmxx_rx_bp_offx_s cn38xxp2;
	struct cvmx_gmxx_rx_bp_offx_s cn50xx;
	struct cvmx_gmxx_rx_bp_offx_s cn52xx;
	struct cvmx_gmxx_rx_bp_offx_s cn52xxp1;
	struct cvmx_gmxx_rx_bp_offx_s cn56xx;
	struct cvmx_gmxx_rx_bp_offx_s cn56xxp1;
	struct cvmx_gmxx_rx_bp_offx_s cn58xx;
	struct cvmx_gmxx_rx_bp_offx_s cn58xxp1;
	struct cvmx_gmxx_rx_bp_offx_s cn61xx;
	struct cvmx_gmxx_rx_bp_offx_s cn63xx;
	struct cvmx_gmxx_rx_bp_offx_s cn63xxp1;
	struct cvmx_gmxx_rx_bp_offx_s cn66xx;
	struct cvmx_gmxx_rx_bp_offx_s cn68xx;
	struct cvmx_gmxx_rx_bp_offx_s cn68xxp1;
	struct cvmx_gmxx_rx_bp_offx_s cnf71xx;
};

union cvmx_gmxx_rx_bp_onx {
	uint64_t u64;
	struct cvmx_gmxx_rx_bp_onx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_11_63:53;
		uint64_t mark:11;
#else
		uint64_t mark:11;
		uint64_t reserved_11_63:53;
#endif
	} s;
	struct cvmx_gmxx_rx_bp_onx_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t mark:9;
#else
		uint64_t mark:9;
		uint64_t reserved_9_63:55;
#endif
	} cn30xx;
	struct cvmx_gmxx_rx_bp_onx_cn30xx cn31xx;
	struct cvmx_gmxx_rx_bp_onx_cn30xx cn38xx;
	struct cvmx_gmxx_rx_bp_onx_cn30xx cn38xxp2;
	struct cvmx_gmxx_rx_bp_onx_cn30xx cn50xx;
	struct cvmx_gmxx_rx_bp_onx_cn30xx cn52xx;
	struct cvmx_gmxx_rx_bp_onx_cn30xx cn52xxp1;
	struct cvmx_gmxx_rx_bp_onx_cn30xx cn56xx;
	struct cvmx_gmxx_rx_bp_onx_cn30xx cn56xxp1;
	struct cvmx_gmxx_rx_bp_onx_cn30xx cn58xx;
	struct cvmx_gmxx_rx_bp_onx_cn30xx cn58xxp1;
	struct cvmx_gmxx_rx_bp_onx_cn30xx cn61xx;
	struct cvmx_gmxx_rx_bp_onx_cn30xx cn63xx;
	struct cvmx_gmxx_rx_bp_onx_cn30xx cn63xxp1;
	struct cvmx_gmxx_rx_bp_onx_cn30xx cn66xx;
	struct cvmx_gmxx_rx_bp_onx_s cn68xx;
	struct cvmx_gmxx_rx_bp_onx_s cn68xxp1;
	struct cvmx_gmxx_rx_bp_onx_cn30xx cnf71xx;
};

union cvmx_gmxx_rx_hg2_status {
	uint64_t u64;
	struct cvmx_gmxx_rx_hg2_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t phtim2go:16;
		uint64_t xof:16;
		uint64_t lgtim2go:16;
#else
		uint64_t lgtim2go:16;
		uint64_t xof:16;
		uint64_t phtim2go:16;
		uint64_t reserved_48_63:16;
#endif
	} s;
	struct cvmx_gmxx_rx_hg2_status_s cn52xx;
	struct cvmx_gmxx_rx_hg2_status_s cn52xxp1;
	struct cvmx_gmxx_rx_hg2_status_s cn56xx;
	struct cvmx_gmxx_rx_hg2_status_s cn61xx;
	struct cvmx_gmxx_rx_hg2_status_s cn63xx;
	struct cvmx_gmxx_rx_hg2_status_s cn63xxp1;
	struct cvmx_gmxx_rx_hg2_status_s cn66xx;
	struct cvmx_gmxx_rx_hg2_status_s cn68xx;
	struct cvmx_gmxx_rx_hg2_status_s cn68xxp1;
	struct cvmx_gmxx_rx_hg2_status_s cnf71xx;
};

union cvmx_gmxx_rx_pass_en {
	uint64_t u64;
	struct cvmx_gmxx_rx_pass_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t en:16;
#else
		uint64_t en:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_gmxx_rx_pass_en_s cn38xx;
	struct cvmx_gmxx_rx_pass_en_s cn38xxp2;
	struct cvmx_gmxx_rx_pass_en_s cn58xx;
	struct cvmx_gmxx_rx_pass_en_s cn58xxp1;
};

union cvmx_gmxx_rx_pass_mapx {
	uint64_t u64;
	struct cvmx_gmxx_rx_pass_mapx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t dprt:4;
#else
		uint64_t dprt:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_gmxx_rx_pass_mapx_s cn38xx;
	struct cvmx_gmxx_rx_pass_mapx_s cn38xxp2;
	struct cvmx_gmxx_rx_pass_mapx_s cn58xx;
	struct cvmx_gmxx_rx_pass_mapx_s cn58xxp1;
};

union cvmx_gmxx_rx_prt_info {
	uint64_t u64;
	struct cvmx_gmxx_rx_prt_info_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t drop:16;
		uint64_t commit:16;
#else
		uint64_t commit:16;
		uint64_t drop:16;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_gmxx_rx_prt_info_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_19_63:45;
		uint64_t drop:3;
		uint64_t reserved_3_15:13;
		uint64_t commit:3;
#else
		uint64_t commit:3;
		uint64_t reserved_3_15:13;
		uint64_t drop:3;
		uint64_t reserved_19_63:45;
#endif
	} cn30xx;
	struct cvmx_gmxx_rx_prt_info_cn30xx cn31xx;
	struct cvmx_gmxx_rx_prt_info_s cn38xx;
	struct cvmx_gmxx_rx_prt_info_cn30xx cn50xx;
	struct cvmx_gmxx_rx_prt_info_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t drop:4;
		uint64_t reserved_4_15:12;
		uint64_t commit:4;
#else
		uint64_t commit:4;
		uint64_t reserved_4_15:12;
		uint64_t drop:4;
		uint64_t reserved_20_63:44;
#endif
	} cn52xx;
	struct cvmx_gmxx_rx_prt_info_cn52xx cn52xxp1;
	struct cvmx_gmxx_rx_prt_info_cn52xx cn56xx;
	struct cvmx_gmxx_rx_prt_info_cn52xx cn56xxp1;
	struct cvmx_gmxx_rx_prt_info_s cn58xx;
	struct cvmx_gmxx_rx_prt_info_s cn58xxp1;
	struct cvmx_gmxx_rx_prt_info_cn52xx cn61xx;
	struct cvmx_gmxx_rx_prt_info_cn52xx cn63xx;
	struct cvmx_gmxx_rx_prt_info_cn52xx cn63xxp1;
	struct cvmx_gmxx_rx_prt_info_cn52xx cn66xx;
	struct cvmx_gmxx_rx_prt_info_cn52xx cn68xx;
	struct cvmx_gmxx_rx_prt_info_cn52xx cn68xxp1;
	struct cvmx_gmxx_rx_prt_info_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_18_63:46;
		uint64_t drop:2;
		uint64_t reserved_2_15:14;
		uint64_t commit:2;
#else
		uint64_t commit:2;
		uint64_t reserved_2_15:14;
		uint64_t drop:2;
		uint64_t reserved_18_63:46;
#endif
	} cnf71xx;
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
	struct cvmx_gmxx_rx_prts_s cn30xx;
	struct cvmx_gmxx_rx_prts_s cn31xx;
	struct cvmx_gmxx_rx_prts_s cn38xx;
	struct cvmx_gmxx_rx_prts_s cn38xxp2;
	struct cvmx_gmxx_rx_prts_s cn50xx;
	struct cvmx_gmxx_rx_prts_s cn52xx;
	struct cvmx_gmxx_rx_prts_s cn52xxp1;
	struct cvmx_gmxx_rx_prts_s cn56xx;
	struct cvmx_gmxx_rx_prts_s cn56xxp1;
	struct cvmx_gmxx_rx_prts_s cn58xx;
	struct cvmx_gmxx_rx_prts_s cn58xxp1;
	struct cvmx_gmxx_rx_prts_s cn61xx;
	struct cvmx_gmxx_rx_prts_s cn63xx;
	struct cvmx_gmxx_rx_prts_s cn63xxp1;
	struct cvmx_gmxx_rx_prts_s cn66xx;
	struct cvmx_gmxx_rx_prts_s cn68xx;
	struct cvmx_gmxx_rx_prts_s cn68xxp1;
	struct cvmx_gmxx_rx_prts_s cnf71xx;
};

union cvmx_gmxx_rx_tx_status {
	uint64_t u64;
	struct cvmx_gmxx_rx_tx_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t tx:3;
		uint64_t reserved_3_3:1;
		uint64_t rx:3;
#else
		uint64_t rx:3;
		uint64_t reserved_3_3:1;
		uint64_t tx:3;
		uint64_t reserved_7_63:57;
#endif
	} s;
	struct cvmx_gmxx_rx_tx_status_s cn30xx;
	struct cvmx_gmxx_rx_tx_status_s cn31xx;
	struct cvmx_gmxx_rx_tx_status_s cn50xx;
};

union cvmx_gmxx_rx_xaui_bad_col {
	uint64_t u64;
	struct cvmx_gmxx_rx_xaui_bad_col_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_40_63:24;
		uint64_t val:1;
		uint64_t state:3;
		uint64_t lane_rxc:4;
		uint64_t lane_rxd:32;
#else
		uint64_t lane_rxd:32;
		uint64_t lane_rxc:4;
		uint64_t state:3;
		uint64_t val:1;
		uint64_t reserved_40_63:24;
#endif
	} s;
	struct cvmx_gmxx_rx_xaui_bad_col_s cn52xx;
	struct cvmx_gmxx_rx_xaui_bad_col_s cn52xxp1;
	struct cvmx_gmxx_rx_xaui_bad_col_s cn56xx;
	struct cvmx_gmxx_rx_xaui_bad_col_s cn56xxp1;
	struct cvmx_gmxx_rx_xaui_bad_col_s cn61xx;
	struct cvmx_gmxx_rx_xaui_bad_col_s cn63xx;
	struct cvmx_gmxx_rx_xaui_bad_col_s cn63xxp1;
	struct cvmx_gmxx_rx_xaui_bad_col_s cn66xx;
	struct cvmx_gmxx_rx_xaui_bad_col_s cn68xx;
	struct cvmx_gmxx_rx_xaui_bad_col_s cn68xxp1;
	struct cvmx_gmxx_rx_xaui_bad_col_s cnf71xx;
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
	struct cvmx_gmxx_rx_xaui_ctl_s cn52xx;
	struct cvmx_gmxx_rx_xaui_ctl_s cn52xxp1;
	struct cvmx_gmxx_rx_xaui_ctl_s cn56xx;
	struct cvmx_gmxx_rx_xaui_ctl_s cn56xxp1;
	struct cvmx_gmxx_rx_xaui_ctl_s cn61xx;
	struct cvmx_gmxx_rx_xaui_ctl_s cn63xx;
	struct cvmx_gmxx_rx_xaui_ctl_s cn63xxp1;
	struct cvmx_gmxx_rx_xaui_ctl_s cn66xx;
	struct cvmx_gmxx_rx_xaui_ctl_s cn68xx;
	struct cvmx_gmxx_rx_xaui_ctl_s cn68xxp1;
	struct cvmx_gmxx_rx_xaui_ctl_s cnf71xx;
};

union cvmx_gmxx_rxaui_ctl {
	uint64_t u64;
	struct cvmx_gmxx_rxaui_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t disparity:1;
#else
		uint64_t disparity:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_gmxx_rxaui_ctl_s cn68xx;
	struct cvmx_gmxx_rxaui_ctl_s cn68xxp1;
};

union cvmx_gmxx_smacx {
	uint64_t u64;
	struct cvmx_gmxx_smacx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t smac:48;
#else
		uint64_t smac:48;
		uint64_t reserved_48_63:16;
#endif
	} s;
	struct cvmx_gmxx_smacx_s cn30xx;
	struct cvmx_gmxx_smacx_s cn31xx;
	struct cvmx_gmxx_smacx_s cn38xx;
	struct cvmx_gmxx_smacx_s cn38xxp2;
	struct cvmx_gmxx_smacx_s cn50xx;
	struct cvmx_gmxx_smacx_s cn52xx;
	struct cvmx_gmxx_smacx_s cn52xxp1;
	struct cvmx_gmxx_smacx_s cn56xx;
	struct cvmx_gmxx_smacx_s cn56xxp1;
	struct cvmx_gmxx_smacx_s cn58xx;
	struct cvmx_gmxx_smacx_s cn58xxp1;
	struct cvmx_gmxx_smacx_s cn61xx;
	struct cvmx_gmxx_smacx_s cn63xx;
	struct cvmx_gmxx_smacx_s cn63xxp1;
	struct cvmx_gmxx_smacx_s cn66xx;
	struct cvmx_gmxx_smacx_s cn68xx;
	struct cvmx_gmxx_smacx_s cn68xxp1;
	struct cvmx_gmxx_smacx_s cnf71xx;
};

union cvmx_gmxx_soft_bist {
	uint64_t u64;
	struct cvmx_gmxx_soft_bist_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t start_bist:1;
		uint64_t clear_bist:1;
#else
		uint64_t clear_bist:1;
		uint64_t start_bist:1;
		uint64_t reserved_2_63:62;
#endif
	} s;
	struct cvmx_gmxx_soft_bist_s cn63xx;
	struct cvmx_gmxx_soft_bist_s cn63xxp1;
	struct cvmx_gmxx_soft_bist_s cn66xx;
	struct cvmx_gmxx_soft_bist_s cn68xx;
	struct cvmx_gmxx_soft_bist_s cn68xxp1;
};

union cvmx_gmxx_stat_bp {
	uint64_t u64;
	struct cvmx_gmxx_stat_bp_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_17_63:47;
		uint64_t bp:1;
		uint64_t cnt:16;
#else
		uint64_t cnt:16;
		uint64_t bp:1;
		uint64_t reserved_17_63:47;
#endif
	} s;
	struct cvmx_gmxx_stat_bp_s cn30xx;
	struct cvmx_gmxx_stat_bp_s cn31xx;
	struct cvmx_gmxx_stat_bp_s cn38xx;
	struct cvmx_gmxx_stat_bp_s cn38xxp2;
	struct cvmx_gmxx_stat_bp_s cn50xx;
	struct cvmx_gmxx_stat_bp_s cn52xx;
	struct cvmx_gmxx_stat_bp_s cn52xxp1;
	struct cvmx_gmxx_stat_bp_s cn56xx;
	struct cvmx_gmxx_stat_bp_s cn56xxp1;
	struct cvmx_gmxx_stat_bp_s cn58xx;
	struct cvmx_gmxx_stat_bp_s cn58xxp1;
	struct cvmx_gmxx_stat_bp_s cn61xx;
	struct cvmx_gmxx_stat_bp_s cn63xx;
	struct cvmx_gmxx_stat_bp_s cn63xxp1;
	struct cvmx_gmxx_stat_bp_s cn66xx;
	struct cvmx_gmxx_stat_bp_s cn68xx;
	struct cvmx_gmxx_stat_bp_s cn68xxp1;
	struct cvmx_gmxx_stat_bp_s cnf71xx;
};

union cvmx_gmxx_tb_reg {
	uint64_t u64;
	struct cvmx_gmxx_tb_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t wr_magic:1;
#else
		uint64_t wr_magic:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_gmxx_tb_reg_s cn61xx;
	struct cvmx_gmxx_tb_reg_s cn66xx;
	struct cvmx_gmxx_tb_reg_s cn68xx;
	struct cvmx_gmxx_tb_reg_s cnf71xx;
};

union cvmx_gmxx_txx_append {
	uint64_t u64;
	struct cvmx_gmxx_txx_append_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t force_fcs:1;
		uint64_t fcs:1;
		uint64_t pad:1;
		uint64_t preamble:1;
#else
		uint64_t preamble:1;
		uint64_t pad:1;
		uint64_t fcs:1;
		uint64_t force_fcs:1;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_gmxx_txx_append_s cn30xx;
	struct cvmx_gmxx_txx_append_s cn31xx;
	struct cvmx_gmxx_txx_append_s cn38xx;
	struct cvmx_gmxx_txx_append_s cn38xxp2;
	struct cvmx_gmxx_txx_append_s cn50xx;
	struct cvmx_gmxx_txx_append_s cn52xx;
	struct cvmx_gmxx_txx_append_s cn52xxp1;
	struct cvmx_gmxx_txx_append_s cn56xx;
	struct cvmx_gmxx_txx_append_s cn56xxp1;
	struct cvmx_gmxx_txx_append_s cn58xx;
	struct cvmx_gmxx_txx_append_s cn58xxp1;
	struct cvmx_gmxx_txx_append_s cn61xx;
	struct cvmx_gmxx_txx_append_s cn63xx;
	struct cvmx_gmxx_txx_append_s cn63xxp1;
	struct cvmx_gmxx_txx_append_s cn66xx;
	struct cvmx_gmxx_txx_append_s cn68xx;
	struct cvmx_gmxx_txx_append_s cn68xxp1;
	struct cvmx_gmxx_txx_append_s cnf71xx;
};

union cvmx_gmxx_txx_burst {
	uint64_t u64;
	struct cvmx_gmxx_txx_burst_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t burst:16;
#else
		uint64_t burst:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_gmxx_txx_burst_s cn30xx;
	struct cvmx_gmxx_txx_burst_s cn31xx;
	struct cvmx_gmxx_txx_burst_s cn38xx;
	struct cvmx_gmxx_txx_burst_s cn38xxp2;
	struct cvmx_gmxx_txx_burst_s cn50xx;
	struct cvmx_gmxx_txx_burst_s cn52xx;
	struct cvmx_gmxx_txx_burst_s cn52xxp1;
	struct cvmx_gmxx_txx_burst_s cn56xx;
	struct cvmx_gmxx_txx_burst_s cn56xxp1;
	struct cvmx_gmxx_txx_burst_s cn58xx;
	struct cvmx_gmxx_txx_burst_s cn58xxp1;
	struct cvmx_gmxx_txx_burst_s cn61xx;
	struct cvmx_gmxx_txx_burst_s cn63xx;
	struct cvmx_gmxx_txx_burst_s cn63xxp1;
	struct cvmx_gmxx_txx_burst_s cn66xx;
	struct cvmx_gmxx_txx_burst_s cn68xx;
	struct cvmx_gmxx_txx_burst_s cn68xxp1;
	struct cvmx_gmxx_txx_burst_s cnf71xx;
};

union cvmx_gmxx_txx_cbfc_xoff {
	uint64_t u64;
	struct cvmx_gmxx_txx_cbfc_xoff_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t xoff:16;
#else
		uint64_t xoff:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_gmxx_txx_cbfc_xoff_s cn52xx;
	struct cvmx_gmxx_txx_cbfc_xoff_s cn56xx;
	struct cvmx_gmxx_txx_cbfc_xoff_s cn61xx;
	struct cvmx_gmxx_txx_cbfc_xoff_s cn63xx;
	struct cvmx_gmxx_txx_cbfc_xoff_s cn63xxp1;
	struct cvmx_gmxx_txx_cbfc_xoff_s cn66xx;
	struct cvmx_gmxx_txx_cbfc_xoff_s cn68xx;
	struct cvmx_gmxx_txx_cbfc_xoff_s cn68xxp1;
	struct cvmx_gmxx_txx_cbfc_xoff_s cnf71xx;
};

union cvmx_gmxx_txx_cbfc_xon {
	uint64_t u64;
	struct cvmx_gmxx_txx_cbfc_xon_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t xon:16;
#else
		uint64_t xon:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_gmxx_txx_cbfc_xon_s cn52xx;
	struct cvmx_gmxx_txx_cbfc_xon_s cn56xx;
	struct cvmx_gmxx_txx_cbfc_xon_s cn61xx;
	struct cvmx_gmxx_txx_cbfc_xon_s cn63xx;
	struct cvmx_gmxx_txx_cbfc_xon_s cn63xxp1;
	struct cvmx_gmxx_txx_cbfc_xon_s cn66xx;
	struct cvmx_gmxx_txx_cbfc_xon_s cn68xx;
	struct cvmx_gmxx_txx_cbfc_xon_s cn68xxp1;
	struct cvmx_gmxx_txx_cbfc_xon_s cnf71xx;
};

union cvmx_gmxx_txx_clk {
	uint64_t u64;
	struct cvmx_gmxx_txx_clk_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t clk_cnt:6;
#else
		uint64_t clk_cnt:6;
		uint64_t reserved_6_63:58;
#endif
	} s;
	struct cvmx_gmxx_txx_clk_s cn30xx;
	struct cvmx_gmxx_txx_clk_s cn31xx;
	struct cvmx_gmxx_txx_clk_s cn38xx;
	struct cvmx_gmxx_txx_clk_s cn38xxp2;
	struct cvmx_gmxx_txx_clk_s cn50xx;
	struct cvmx_gmxx_txx_clk_s cn58xx;
	struct cvmx_gmxx_txx_clk_s cn58xxp1;
};

union cvmx_gmxx_txx_ctl {
	uint64_t u64;
	struct cvmx_gmxx_txx_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t xsdef_en:1;
		uint64_t xscol_en:1;
#else
		uint64_t xscol_en:1;
		uint64_t xsdef_en:1;
		uint64_t reserved_2_63:62;
#endif
	} s;
	struct cvmx_gmxx_txx_ctl_s cn30xx;
	struct cvmx_gmxx_txx_ctl_s cn31xx;
	struct cvmx_gmxx_txx_ctl_s cn38xx;
	struct cvmx_gmxx_txx_ctl_s cn38xxp2;
	struct cvmx_gmxx_txx_ctl_s cn50xx;
	struct cvmx_gmxx_txx_ctl_s cn52xx;
	struct cvmx_gmxx_txx_ctl_s cn52xxp1;
	struct cvmx_gmxx_txx_ctl_s cn56xx;
	struct cvmx_gmxx_txx_ctl_s cn56xxp1;
	struct cvmx_gmxx_txx_ctl_s cn58xx;
	struct cvmx_gmxx_txx_ctl_s cn58xxp1;
	struct cvmx_gmxx_txx_ctl_s cn61xx;
	struct cvmx_gmxx_txx_ctl_s cn63xx;
	struct cvmx_gmxx_txx_ctl_s cn63xxp1;
	struct cvmx_gmxx_txx_ctl_s cn66xx;
	struct cvmx_gmxx_txx_ctl_s cn68xx;
	struct cvmx_gmxx_txx_ctl_s cn68xxp1;
	struct cvmx_gmxx_txx_ctl_s cnf71xx;
};

union cvmx_gmxx_txx_min_pkt {
	uint64_t u64;
	struct cvmx_gmxx_txx_min_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t min_size:8;
#else
		uint64_t min_size:8;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_gmxx_txx_min_pkt_s cn30xx;
	struct cvmx_gmxx_txx_min_pkt_s cn31xx;
	struct cvmx_gmxx_txx_min_pkt_s cn38xx;
	struct cvmx_gmxx_txx_min_pkt_s cn38xxp2;
	struct cvmx_gmxx_txx_min_pkt_s cn50xx;
	struct cvmx_gmxx_txx_min_pkt_s cn52xx;
	struct cvmx_gmxx_txx_min_pkt_s cn52xxp1;
	struct cvmx_gmxx_txx_min_pkt_s cn56xx;
	struct cvmx_gmxx_txx_min_pkt_s cn56xxp1;
	struct cvmx_gmxx_txx_min_pkt_s cn58xx;
	struct cvmx_gmxx_txx_min_pkt_s cn58xxp1;
	struct cvmx_gmxx_txx_min_pkt_s cn61xx;
	struct cvmx_gmxx_txx_min_pkt_s cn63xx;
	struct cvmx_gmxx_txx_min_pkt_s cn63xxp1;
	struct cvmx_gmxx_txx_min_pkt_s cn66xx;
	struct cvmx_gmxx_txx_min_pkt_s cn68xx;
	struct cvmx_gmxx_txx_min_pkt_s cn68xxp1;
	struct cvmx_gmxx_txx_min_pkt_s cnf71xx;
};

union cvmx_gmxx_txx_pause_pkt_interval {
	uint64_t u64;
	struct cvmx_gmxx_txx_pause_pkt_interval_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t interval:16;
#else
		uint64_t interval:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn30xx;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn31xx;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn38xx;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn38xxp2;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn50xx;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn52xx;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn52xxp1;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn56xx;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn56xxp1;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn58xx;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn58xxp1;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn61xx;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn63xx;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn63xxp1;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn66xx;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn68xx;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn68xxp1;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cnf71xx;
};

union cvmx_gmxx_txx_pause_pkt_time {
	uint64_t u64;
	struct cvmx_gmxx_txx_pause_pkt_time_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t time:16;
#else
		uint64_t time:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn30xx;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn31xx;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn38xx;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn38xxp2;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn50xx;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn52xx;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn52xxp1;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn56xx;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn56xxp1;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn58xx;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn58xxp1;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn61xx;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn63xx;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn63xxp1;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn66xx;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn68xx;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn68xxp1;
	struct cvmx_gmxx_txx_pause_pkt_time_s cnf71xx;
};

union cvmx_gmxx_txx_pause_togo {
	uint64_t u64;
	struct cvmx_gmxx_txx_pause_togo_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t msg_time:16;
		uint64_t time:16;
#else
		uint64_t time:16;
		uint64_t msg_time:16;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_gmxx_txx_pause_togo_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t time:16;
#else
		uint64_t time:16;
		uint64_t reserved_16_63:48;
#endif
	} cn30xx;
	struct cvmx_gmxx_txx_pause_togo_cn30xx cn31xx;
	struct cvmx_gmxx_txx_pause_togo_cn30xx cn38xx;
	struct cvmx_gmxx_txx_pause_togo_cn30xx cn38xxp2;
	struct cvmx_gmxx_txx_pause_togo_cn30xx cn50xx;
	struct cvmx_gmxx_txx_pause_togo_s cn52xx;
	struct cvmx_gmxx_txx_pause_togo_s cn52xxp1;
	struct cvmx_gmxx_txx_pause_togo_s cn56xx;
	struct cvmx_gmxx_txx_pause_togo_cn30xx cn56xxp1;
	struct cvmx_gmxx_txx_pause_togo_cn30xx cn58xx;
	struct cvmx_gmxx_txx_pause_togo_cn30xx cn58xxp1;
	struct cvmx_gmxx_txx_pause_togo_s cn61xx;
	struct cvmx_gmxx_txx_pause_togo_s cn63xx;
	struct cvmx_gmxx_txx_pause_togo_s cn63xxp1;
	struct cvmx_gmxx_txx_pause_togo_s cn66xx;
	struct cvmx_gmxx_txx_pause_togo_s cn68xx;
	struct cvmx_gmxx_txx_pause_togo_s cn68xxp1;
	struct cvmx_gmxx_txx_pause_togo_s cnf71xx;
};

union cvmx_gmxx_txx_pause_zero {
	uint64_t u64;
	struct cvmx_gmxx_txx_pause_zero_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t send:1;
#else
		uint64_t send:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_gmxx_txx_pause_zero_s cn30xx;
	struct cvmx_gmxx_txx_pause_zero_s cn31xx;
	struct cvmx_gmxx_txx_pause_zero_s cn38xx;
	struct cvmx_gmxx_txx_pause_zero_s cn38xxp2;
	struct cvmx_gmxx_txx_pause_zero_s cn50xx;
	struct cvmx_gmxx_txx_pause_zero_s cn52xx;
	struct cvmx_gmxx_txx_pause_zero_s cn52xxp1;
	struct cvmx_gmxx_txx_pause_zero_s cn56xx;
	struct cvmx_gmxx_txx_pause_zero_s cn56xxp1;
	struct cvmx_gmxx_txx_pause_zero_s cn58xx;
	struct cvmx_gmxx_txx_pause_zero_s cn58xxp1;
	struct cvmx_gmxx_txx_pause_zero_s cn61xx;
	struct cvmx_gmxx_txx_pause_zero_s cn63xx;
	struct cvmx_gmxx_txx_pause_zero_s cn63xxp1;
	struct cvmx_gmxx_txx_pause_zero_s cn66xx;
	struct cvmx_gmxx_txx_pause_zero_s cn68xx;
	struct cvmx_gmxx_txx_pause_zero_s cn68xxp1;
	struct cvmx_gmxx_txx_pause_zero_s cnf71xx;
};

union cvmx_gmxx_txx_pipe {
	uint64_t u64;
	struct cvmx_gmxx_txx_pipe_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_33_63:31;
		uint64_t ign_bp:1;
		uint64_t reserved_21_31:11;
		uint64_t nump:5;
		uint64_t reserved_7_15:9;
		uint64_t base:7;
#else
		uint64_t base:7;
		uint64_t reserved_7_15:9;
		uint64_t nump:5;
		uint64_t reserved_21_31:11;
		uint64_t ign_bp:1;
		uint64_t reserved_33_63:31;
#endif
	} s;
	struct cvmx_gmxx_txx_pipe_s cn68xx;
	struct cvmx_gmxx_txx_pipe_s cn68xxp1;
};

union cvmx_gmxx_txx_sgmii_ctl {
	uint64_t u64;
	struct cvmx_gmxx_txx_sgmii_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t align:1;
#else
		uint64_t align:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_gmxx_txx_sgmii_ctl_s cn52xx;
	struct cvmx_gmxx_txx_sgmii_ctl_s cn52xxp1;
	struct cvmx_gmxx_txx_sgmii_ctl_s cn56xx;
	struct cvmx_gmxx_txx_sgmii_ctl_s cn56xxp1;
	struct cvmx_gmxx_txx_sgmii_ctl_s cn61xx;
	struct cvmx_gmxx_txx_sgmii_ctl_s cn63xx;
	struct cvmx_gmxx_txx_sgmii_ctl_s cn63xxp1;
	struct cvmx_gmxx_txx_sgmii_ctl_s cn66xx;
	struct cvmx_gmxx_txx_sgmii_ctl_s cn68xx;
	struct cvmx_gmxx_txx_sgmii_ctl_s cn68xxp1;
	struct cvmx_gmxx_txx_sgmii_ctl_s cnf71xx;
};

union cvmx_gmxx_txx_slot {
	uint64_t u64;
	struct cvmx_gmxx_txx_slot_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t slot:10;
#else
		uint64_t slot:10;
		uint64_t reserved_10_63:54;
#endif
	} s;
	struct cvmx_gmxx_txx_slot_s cn30xx;
	struct cvmx_gmxx_txx_slot_s cn31xx;
	struct cvmx_gmxx_txx_slot_s cn38xx;
	struct cvmx_gmxx_txx_slot_s cn38xxp2;
	struct cvmx_gmxx_txx_slot_s cn50xx;
	struct cvmx_gmxx_txx_slot_s cn52xx;
	struct cvmx_gmxx_txx_slot_s cn52xxp1;
	struct cvmx_gmxx_txx_slot_s cn56xx;
	struct cvmx_gmxx_txx_slot_s cn56xxp1;
	struct cvmx_gmxx_txx_slot_s cn58xx;
	struct cvmx_gmxx_txx_slot_s cn58xxp1;
	struct cvmx_gmxx_txx_slot_s cn61xx;
	struct cvmx_gmxx_txx_slot_s cn63xx;
	struct cvmx_gmxx_txx_slot_s cn63xxp1;
	struct cvmx_gmxx_txx_slot_s cn66xx;
	struct cvmx_gmxx_txx_slot_s cn68xx;
	struct cvmx_gmxx_txx_slot_s cn68xxp1;
	struct cvmx_gmxx_txx_slot_s cnf71xx;
};

union cvmx_gmxx_txx_soft_pause {
	uint64_t u64;
	struct cvmx_gmxx_txx_soft_pause_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t time:16;
#else
		uint64_t time:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_gmxx_txx_soft_pause_s cn30xx;
	struct cvmx_gmxx_txx_soft_pause_s cn31xx;
	struct cvmx_gmxx_txx_soft_pause_s cn38xx;
	struct cvmx_gmxx_txx_soft_pause_s cn38xxp2;
	struct cvmx_gmxx_txx_soft_pause_s cn50xx;
	struct cvmx_gmxx_txx_soft_pause_s cn52xx;
	struct cvmx_gmxx_txx_soft_pause_s cn52xxp1;
	struct cvmx_gmxx_txx_soft_pause_s cn56xx;
	struct cvmx_gmxx_txx_soft_pause_s cn56xxp1;
	struct cvmx_gmxx_txx_soft_pause_s cn58xx;
	struct cvmx_gmxx_txx_soft_pause_s cn58xxp1;
	struct cvmx_gmxx_txx_soft_pause_s cn61xx;
	struct cvmx_gmxx_txx_soft_pause_s cn63xx;
	struct cvmx_gmxx_txx_soft_pause_s cn63xxp1;
	struct cvmx_gmxx_txx_soft_pause_s cn66xx;
	struct cvmx_gmxx_txx_soft_pause_s cn68xx;
	struct cvmx_gmxx_txx_soft_pause_s cn68xxp1;
	struct cvmx_gmxx_txx_soft_pause_s cnf71xx;
};

union cvmx_gmxx_txx_stat0 {
	uint64_t u64;
	struct cvmx_gmxx_txx_stat0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t xsdef:32;
		uint64_t xscol:32;
#else
		uint64_t xscol:32;
		uint64_t xsdef:32;
#endif
	} s;
	struct cvmx_gmxx_txx_stat0_s cn30xx;
	struct cvmx_gmxx_txx_stat0_s cn31xx;
	struct cvmx_gmxx_txx_stat0_s cn38xx;
	struct cvmx_gmxx_txx_stat0_s cn38xxp2;
	struct cvmx_gmxx_txx_stat0_s cn50xx;
	struct cvmx_gmxx_txx_stat0_s cn52xx;
	struct cvmx_gmxx_txx_stat0_s cn52xxp1;
	struct cvmx_gmxx_txx_stat0_s cn56xx;
	struct cvmx_gmxx_txx_stat0_s cn56xxp1;
	struct cvmx_gmxx_txx_stat0_s cn58xx;
	struct cvmx_gmxx_txx_stat0_s cn58xxp1;
	struct cvmx_gmxx_txx_stat0_s cn61xx;
	struct cvmx_gmxx_txx_stat0_s cn63xx;
	struct cvmx_gmxx_txx_stat0_s cn63xxp1;
	struct cvmx_gmxx_txx_stat0_s cn66xx;
	struct cvmx_gmxx_txx_stat0_s cn68xx;
	struct cvmx_gmxx_txx_stat0_s cn68xxp1;
	struct cvmx_gmxx_txx_stat0_s cnf71xx;
};

union cvmx_gmxx_txx_stat1 {
	uint64_t u64;
	struct cvmx_gmxx_txx_stat1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t scol:32;
		uint64_t mcol:32;
#else
		uint64_t mcol:32;
		uint64_t scol:32;
#endif
	} s;
	struct cvmx_gmxx_txx_stat1_s cn30xx;
	struct cvmx_gmxx_txx_stat1_s cn31xx;
	struct cvmx_gmxx_txx_stat1_s cn38xx;
	struct cvmx_gmxx_txx_stat1_s cn38xxp2;
	struct cvmx_gmxx_txx_stat1_s cn50xx;
	struct cvmx_gmxx_txx_stat1_s cn52xx;
	struct cvmx_gmxx_txx_stat1_s cn52xxp1;
	struct cvmx_gmxx_txx_stat1_s cn56xx;
	struct cvmx_gmxx_txx_stat1_s cn56xxp1;
	struct cvmx_gmxx_txx_stat1_s cn58xx;
	struct cvmx_gmxx_txx_stat1_s cn58xxp1;
	struct cvmx_gmxx_txx_stat1_s cn61xx;
	struct cvmx_gmxx_txx_stat1_s cn63xx;
	struct cvmx_gmxx_txx_stat1_s cn63xxp1;
	struct cvmx_gmxx_txx_stat1_s cn66xx;
	struct cvmx_gmxx_txx_stat1_s cn68xx;
	struct cvmx_gmxx_txx_stat1_s cn68xxp1;
	struct cvmx_gmxx_txx_stat1_s cnf71xx;
};

union cvmx_gmxx_txx_stat2 {
	uint64_t u64;
	struct cvmx_gmxx_txx_stat2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t octs:48;
#else
		uint64_t octs:48;
		uint64_t reserved_48_63:16;
#endif
	} s;
	struct cvmx_gmxx_txx_stat2_s cn30xx;
	struct cvmx_gmxx_txx_stat2_s cn31xx;
	struct cvmx_gmxx_txx_stat2_s cn38xx;
	struct cvmx_gmxx_txx_stat2_s cn38xxp2;
	struct cvmx_gmxx_txx_stat2_s cn50xx;
	struct cvmx_gmxx_txx_stat2_s cn52xx;
	struct cvmx_gmxx_txx_stat2_s cn52xxp1;
	struct cvmx_gmxx_txx_stat2_s cn56xx;
	struct cvmx_gmxx_txx_stat2_s cn56xxp1;
	struct cvmx_gmxx_txx_stat2_s cn58xx;
	struct cvmx_gmxx_txx_stat2_s cn58xxp1;
	struct cvmx_gmxx_txx_stat2_s cn61xx;
	struct cvmx_gmxx_txx_stat2_s cn63xx;
	struct cvmx_gmxx_txx_stat2_s cn63xxp1;
	struct cvmx_gmxx_txx_stat2_s cn66xx;
	struct cvmx_gmxx_txx_stat2_s cn68xx;
	struct cvmx_gmxx_txx_stat2_s cn68xxp1;
	struct cvmx_gmxx_txx_stat2_s cnf71xx;
};

union cvmx_gmxx_txx_stat3 {
	uint64_t u64;
	struct cvmx_gmxx_txx_stat3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t pkts:32;
#else
		uint64_t pkts:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_gmxx_txx_stat3_s cn30xx;
	struct cvmx_gmxx_txx_stat3_s cn31xx;
	struct cvmx_gmxx_txx_stat3_s cn38xx;
	struct cvmx_gmxx_txx_stat3_s cn38xxp2;
	struct cvmx_gmxx_txx_stat3_s cn50xx;
	struct cvmx_gmxx_txx_stat3_s cn52xx;
	struct cvmx_gmxx_txx_stat3_s cn52xxp1;
	struct cvmx_gmxx_txx_stat3_s cn56xx;
	struct cvmx_gmxx_txx_stat3_s cn56xxp1;
	struct cvmx_gmxx_txx_stat3_s cn58xx;
	struct cvmx_gmxx_txx_stat3_s cn58xxp1;
	struct cvmx_gmxx_txx_stat3_s cn61xx;
	struct cvmx_gmxx_txx_stat3_s cn63xx;
	struct cvmx_gmxx_txx_stat3_s cn63xxp1;
	struct cvmx_gmxx_txx_stat3_s cn66xx;
	struct cvmx_gmxx_txx_stat3_s cn68xx;
	struct cvmx_gmxx_txx_stat3_s cn68xxp1;
	struct cvmx_gmxx_txx_stat3_s cnf71xx;
};

union cvmx_gmxx_txx_stat4 {
	uint64_t u64;
	struct cvmx_gmxx_txx_stat4_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t hist1:32;
		uint64_t hist0:32;
#else
		uint64_t hist0:32;
		uint64_t hist1:32;
#endif
	} s;
	struct cvmx_gmxx_txx_stat4_s cn30xx;
	struct cvmx_gmxx_txx_stat4_s cn31xx;
	struct cvmx_gmxx_txx_stat4_s cn38xx;
	struct cvmx_gmxx_txx_stat4_s cn38xxp2;
	struct cvmx_gmxx_txx_stat4_s cn50xx;
	struct cvmx_gmxx_txx_stat4_s cn52xx;
	struct cvmx_gmxx_txx_stat4_s cn52xxp1;
	struct cvmx_gmxx_txx_stat4_s cn56xx;
	struct cvmx_gmxx_txx_stat4_s cn56xxp1;
	struct cvmx_gmxx_txx_stat4_s cn58xx;
	struct cvmx_gmxx_txx_stat4_s cn58xxp1;
	struct cvmx_gmxx_txx_stat4_s cn61xx;
	struct cvmx_gmxx_txx_stat4_s cn63xx;
	struct cvmx_gmxx_txx_stat4_s cn63xxp1;
	struct cvmx_gmxx_txx_stat4_s cn66xx;
	struct cvmx_gmxx_txx_stat4_s cn68xx;
	struct cvmx_gmxx_txx_stat4_s cn68xxp1;
	struct cvmx_gmxx_txx_stat4_s cnf71xx;
};

union cvmx_gmxx_txx_stat5 {
	uint64_t u64;
	struct cvmx_gmxx_txx_stat5_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t hist3:32;
		uint64_t hist2:32;
#else
		uint64_t hist2:32;
		uint64_t hist3:32;
#endif
	} s;
	struct cvmx_gmxx_txx_stat5_s cn30xx;
	struct cvmx_gmxx_txx_stat5_s cn31xx;
	struct cvmx_gmxx_txx_stat5_s cn38xx;
	struct cvmx_gmxx_txx_stat5_s cn38xxp2;
	struct cvmx_gmxx_txx_stat5_s cn50xx;
	struct cvmx_gmxx_txx_stat5_s cn52xx;
	struct cvmx_gmxx_txx_stat5_s cn52xxp1;
	struct cvmx_gmxx_txx_stat5_s cn56xx;
	struct cvmx_gmxx_txx_stat5_s cn56xxp1;
	struct cvmx_gmxx_txx_stat5_s cn58xx;
	struct cvmx_gmxx_txx_stat5_s cn58xxp1;
	struct cvmx_gmxx_txx_stat5_s cn61xx;
	struct cvmx_gmxx_txx_stat5_s cn63xx;
	struct cvmx_gmxx_txx_stat5_s cn63xxp1;
	struct cvmx_gmxx_txx_stat5_s cn66xx;
	struct cvmx_gmxx_txx_stat5_s cn68xx;
	struct cvmx_gmxx_txx_stat5_s cn68xxp1;
	struct cvmx_gmxx_txx_stat5_s cnf71xx;
};

union cvmx_gmxx_txx_stat6 {
	uint64_t u64;
	struct cvmx_gmxx_txx_stat6_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t hist5:32;
		uint64_t hist4:32;
#else
		uint64_t hist4:32;
		uint64_t hist5:32;
#endif
	} s;
	struct cvmx_gmxx_txx_stat6_s cn30xx;
	struct cvmx_gmxx_txx_stat6_s cn31xx;
	struct cvmx_gmxx_txx_stat6_s cn38xx;
	struct cvmx_gmxx_txx_stat6_s cn38xxp2;
	struct cvmx_gmxx_txx_stat6_s cn50xx;
	struct cvmx_gmxx_txx_stat6_s cn52xx;
	struct cvmx_gmxx_txx_stat6_s cn52xxp1;
	struct cvmx_gmxx_txx_stat6_s cn56xx;
	struct cvmx_gmxx_txx_stat6_s cn56xxp1;
	struct cvmx_gmxx_txx_stat6_s cn58xx;
	struct cvmx_gmxx_txx_stat6_s cn58xxp1;
	struct cvmx_gmxx_txx_stat6_s cn61xx;
	struct cvmx_gmxx_txx_stat6_s cn63xx;
	struct cvmx_gmxx_txx_stat6_s cn63xxp1;
	struct cvmx_gmxx_txx_stat6_s cn66xx;
	struct cvmx_gmxx_txx_stat6_s cn68xx;
	struct cvmx_gmxx_txx_stat6_s cn68xxp1;
	struct cvmx_gmxx_txx_stat6_s cnf71xx;
};

union cvmx_gmxx_txx_stat7 {
	uint64_t u64;
	struct cvmx_gmxx_txx_stat7_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t hist7:32;
		uint64_t hist6:32;
#else
		uint64_t hist6:32;
		uint64_t hist7:32;
#endif
	} s;
	struct cvmx_gmxx_txx_stat7_s cn30xx;
	struct cvmx_gmxx_txx_stat7_s cn31xx;
	struct cvmx_gmxx_txx_stat7_s cn38xx;
	struct cvmx_gmxx_txx_stat7_s cn38xxp2;
	struct cvmx_gmxx_txx_stat7_s cn50xx;
	struct cvmx_gmxx_txx_stat7_s cn52xx;
	struct cvmx_gmxx_txx_stat7_s cn52xxp1;
	struct cvmx_gmxx_txx_stat7_s cn56xx;
	struct cvmx_gmxx_txx_stat7_s cn56xxp1;
	struct cvmx_gmxx_txx_stat7_s cn58xx;
	struct cvmx_gmxx_txx_stat7_s cn58xxp1;
	struct cvmx_gmxx_txx_stat7_s cn61xx;
	struct cvmx_gmxx_txx_stat7_s cn63xx;
	struct cvmx_gmxx_txx_stat7_s cn63xxp1;
	struct cvmx_gmxx_txx_stat7_s cn66xx;
	struct cvmx_gmxx_txx_stat7_s cn68xx;
	struct cvmx_gmxx_txx_stat7_s cn68xxp1;
	struct cvmx_gmxx_txx_stat7_s cnf71xx;
};

union cvmx_gmxx_txx_stat8 {
	uint64_t u64;
	struct cvmx_gmxx_txx_stat8_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t mcst:32;
		uint64_t bcst:32;
#else
		uint64_t bcst:32;
		uint64_t mcst:32;
#endif
	} s;
	struct cvmx_gmxx_txx_stat8_s cn30xx;
	struct cvmx_gmxx_txx_stat8_s cn31xx;
	struct cvmx_gmxx_txx_stat8_s cn38xx;
	struct cvmx_gmxx_txx_stat8_s cn38xxp2;
	struct cvmx_gmxx_txx_stat8_s cn50xx;
	struct cvmx_gmxx_txx_stat8_s cn52xx;
	struct cvmx_gmxx_txx_stat8_s cn52xxp1;
	struct cvmx_gmxx_txx_stat8_s cn56xx;
	struct cvmx_gmxx_txx_stat8_s cn56xxp1;
	struct cvmx_gmxx_txx_stat8_s cn58xx;
	struct cvmx_gmxx_txx_stat8_s cn58xxp1;
	struct cvmx_gmxx_txx_stat8_s cn61xx;
	struct cvmx_gmxx_txx_stat8_s cn63xx;
	struct cvmx_gmxx_txx_stat8_s cn63xxp1;
	struct cvmx_gmxx_txx_stat8_s cn66xx;
	struct cvmx_gmxx_txx_stat8_s cn68xx;
	struct cvmx_gmxx_txx_stat8_s cn68xxp1;
	struct cvmx_gmxx_txx_stat8_s cnf71xx;
};

union cvmx_gmxx_txx_stat9 {
	uint64_t u64;
	struct cvmx_gmxx_txx_stat9_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t undflw:32;
		uint64_t ctl:32;
#else
		uint64_t ctl:32;
		uint64_t undflw:32;
#endif
	} s;
	struct cvmx_gmxx_txx_stat9_s cn30xx;
	struct cvmx_gmxx_txx_stat9_s cn31xx;
	struct cvmx_gmxx_txx_stat9_s cn38xx;
	struct cvmx_gmxx_txx_stat9_s cn38xxp2;
	struct cvmx_gmxx_txx_stat9_s cn50xx;
	struct cvmx_gmxx_txx_stat9_s cn52xx;
	struct cvmx_gmxx_txx_stat9_s cn52xxp1;
	struct cvmx_gmxx_txx_stat9_s cn56xx;
	struct cvmx_gmxx_txx_stat9_s cn56xxp1;
	struct cvmx_gmxx_txx_stat9_s cn58xx;
	struct cvmx_gmxx_txx_stat9_s cn58xxp1;
	struct cvmx_gmxx_txx_stat9_s cn61xx;
	struct cvmx_gmxx_txx_stat9_s cn63xx;
	struct cvmx_gmxx_txx_stat9_s cn63xxp1;
	struct cvmx_gmxx_txx_stat9_s cn66xx;
	struct cvmx_gmxx_txx_stat9_s cn68xx;
	struct cvmx_gmxx_txx_stat9_s cn68xxp1;
	struct cvmx_gmxx_txx_stat9_s cnf71xx;
};

union cvmx_gmxx_txx_stats_ctl {
	uint64_t u64;
	struct cvmx_gmxx_txx_stats_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t rd_clr:1;
#else
		uint64_t rd_clr:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_gmxx_txx_stats_ctl_s cn30xx;
	struct cvmx_gmxx_txx_stats_ctl_s cn31xx;
	struct cvmx_gmxx_txx_stats_ctl_s cn38xx;
	struct cvmx_gmxx_txx_stats_ctl_s cn38xxp2;
	struct cvmx_gmxx_txx_stats_ctl_s cn50xx;
	struct cvmx_gmxx_txx_stats_ctl_s cn52xx;
	struct cvmx_gmxx_txx_stats_ctl_s cn52xxp1;
	struct cvmx_gmxx_txx_stats_ctl_s cn56xx;
	struct cvmx_gmxx_txx_stats_ctl_s cn56xxp1;
	struct cvmx_gmxx_txx_stats_ctl_s cn58xx;
	struct cvmx_gmxx_txx_stats_ctl_s cn58xxp1;
	struct cvmx_gmxx_txx_stats_ctl_s cn61xx;
	struct cvmx_gmxx_txx_stats_ctl_s cn63xx;
	struct cvmx_gmxx_txx_stats_ctl_s cn63xxp1;
	struct cvmx_gmxx_txx_stats_ctl_s cn66xx;
	struct cvmx_gmxx_txx_stats_ctl_s cn68xx;
	struct cvmx_gmxx_txx_stats_ctl_s cn68xxp1;
	struct cvmx_gmxx_txx_stats_ctl_s cnf71xx;
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
	struct cvmx_gmxx_txx_thresh_cn30xx cn31xx;
	struct cvmx_gmxx_txx_thresh_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t cnt:9;
#else
		uint64_t cnt:9;
		uint64_t reserved_9_63:55;
#endif
	} cn38xx;
	struct cvmx_gmxx_txx_thresh_cn38xx cn38xxp2;
	struct cvmx_gmxx_txx_thresh_cn30xx cn50xx;
	struct cvmx_gmxx_txx_thresh_cn38xx cn52xx;
	struct cvmx_gmxx_txx_thresh_cn38xx cn52xxp1;
	struct cvmx_gmxx_txx_thresh_cn38xx cn56xx;
	struct cvmx_gmxx_txx_thresh_cn38xx cn56xxp1;
	struct cvmx_gmxx_txx_thresh_cn38xx cn58xx;
	struct cvmx_gmxx_txx_thresh_cn38xx cn58xxp1;
	struct cvmx_gmxx_txx_thresh_cn38xx cn61xx;
	struct cvmx_gmxx_txx_thresh_cn38xx cn63xx;
	struct cvmx_gmxx_txx_thresh_cn38xx cn63xxp1;
	struct cvmx_gmxx_txx_thresh_cn38xx cn66xx;
	struct cvmx_gmxx_txx_thresh_s cn68xx;
	struct cvmx_gmxx_txx_thresh_s cn68xxp1;
	struct cvmx_gmxx_txx_thresh_cn38xx cnf71xx;
};

union cvmx_gmxx_tx_bp {
	uint64_t u64;
	struct cvmx_gmxx_tx_bp_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t bp:4;
#else
		uint64_t bp:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_gmxx_tx_bp_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t bp:3;
#else
		uint64_t bp:3;
		uint64_t reserved_3_63:61;
#endif
	} cn30xx;
	struct cvmx_gmxx_tx_bp_cn30xx cn31xx;
	struct cvmx_gmxx_tx_bp_s cn38xx;
	struct cvmx_gmxx_tx_bp_s cn38xxp2;
	struct cvmx_gmxx_tx_bp_cn30xx cn50xx;
	struct cvmx_gmxx_tx_bp_s cn52xx;
	struct cvmx_gmxx_tx_bp_s cn52xxp1;
	struct cvmx_gmxx_tx_bp_s cn56xx;
	struct cvmx_gmxx_tx_bp_s cn56xxp1;
	struct cvmx_gmxx_tx_bp_s cn58xx;
	struct cvmx_gmxx_tx_bp_s cn58xxp1;
	struct cvmx_gmxx_tx_bp_s cn61xx;
	struct cvmx_gmxx_tx_bp_s cn63xx;
	struct cvmx_gmxx_tx_bp_s cn63xxp1;
	struct cvmx_gmxx_tx_bp_s cn66xx;
	struct cvmx_gmxx_tx_bp_s cn68xx;
	struct cvmx_gmxx_tx_bp_s cn68xxp1;
	struct cvmx_gmxx_tx_bp_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t bp:2;
#else
		uint64_t bp:2;
		uint64_t reserved_2_63:62;
#endif
	} cnf71xx;
};

union cvmx_gmxx_tx_clk_mskx {
	uint64_t u64;
	struct cvmx_gmxx_tx_clk_mskx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t msk:1;
#else
		uint64_t msk:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_gmxx_tx_clk_mskx_s cn30xx;
	struct cvmx_gmxx_tx_clk_mskx_s cn50xx;
};

union cvmx_gmxx_tx_col_attempt {
	uint64_t u64;
	struct cvmx_gmxx_tx_col_attempt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_5_63:59;
		uint64_t limit:5;
#else
		uint64_t limit:5;
		uint64_t reserved_5_63:59;
#endif
	} s;
	struct cvmx_gmxx_tx_col_attempt_s cn30xx;
	struct cvmx_gmxx_tx_col_attempt_s cn31xx;
	struct cvmx_gmxx_tx_col_attempt_s cn38xx;
	struct cvmx_gmxx_tx_col_attempt_s cn38xxp2;
	struct cvmx_gmxx_tx_col_attempt_s cn50xx;
	struct cvmx_gmxx_tx_col_attempt_s cn52xx;
	struct cvmx_gmxx_tx_col_attempt_s cn52xxp1;
	struct cvmx_gmxx_tx_col_attempt_s cn56xx;
	struct cvmx_gmxx_tx_col_attempt_s cn56xxp1;
	struct cvmx_gmxx_tx_col_attempt_s cn58xx;
	struct cvmx_gmxx_tx_col_attempt_s cn58xxp1;
	struct cvmx_gmxx_tx_col_attempt_s cn61xx;
	struct cvmx_gmxx_tx_col_attempt_s cn63xx;
	struct cvmx_gmxx_tx_col_attempt_s cn63xxp1;
	struct cvmx_gmxx_tx_col_attempt_s cn66xx;
	struct cvmx_gmxx_tx_col_attempt_s cn68xx;
	struct cvmx_gmxx_tx_col_attempt_s cn68xxp1;
	struct cvmx_gmxx_tx_col_attempt_s cnf71xx;
};

union cvmx_gmxx_tx_corrupt {
	uint64_t u64;
	struct cvmx_gmxx_tx_corrupt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t corrupt:4;
#else
		uint64_t corrupt:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_gmxx_tx_corrupt_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t corrupt:3;
#else
		uint64_t corrupt:3;
		uint64_t reserved_3_63:61;
#endif
	} cn30xx;
	struct cvmx_gmxx_tx_corrupt_cn30xx cn31xx;
	struct cvmx_gmxx_tx_corrupt_s cn38xx;
	struct cvmx_gmxx_tx_corrupt_s cn38xxp2;
	struct cvmx_gmxx_tx_corrupt_cn30xx cn50xx;
	struct cvmx_gmxx_tx_corrupt_s cn52xx;
	struct cvmx_gmxx_tx_corrupt_s cn52xxp1;
	struct cvmx_gmxx_tx_corrupt_s cn56xx;
	struct cvmx_gmxx_tx_corrupt_s cn56xxp1;
	struct cvmx_gmxx_tx_corrupt_s cn58xx;
	struct cvmx_gmxx_tx_corrupt_s cn58xxp1;
	struct cvmx_gmxx_tx_corrupt_s cn61xx;
	struct cvmx_gmxx_tx_corrupt_s cn63xx;
	struct cvmx_gmxx_tx_corrupt_s cn63xxp1;
	struct cvmx_gmxx_tx_corrupt_s cn66xx;
	struct cvmx_gmxx_tx_corrupt_s cn68xx;
	struct cvmx_gmxx_tx_corrupt_s cn68xxp1;
	struct cvmx_gmxx_tx_corrupt_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t corrupt:2;
#else
		uint64_t corrupt:2;
		uint64_t reserved_2_63:62;
#endif
	} cnf71xx;
};

union cvmx_gmxx_tx_hg2_reg1 {
	uint64_t u64;
	struct cvmx_gmxx_tx_hg2_reg1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t tx_xof:16;
#else
		uint64_t tx_xof:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_gmxx_tx_hg2_reg1_s cn52xx;
	struct cvmx_gmxx_tx_hg2_reg1_s cn52xxp1;
	struct cvmx_gmxx_tx_hg2_reg1_s cn56xx;
	struct cvmx_gmxx_tx_hg2_reg1_s cn61xx;
	struct cvmx_gmxx_tx_hg2_reg1_s cn63xx;
	struct cvmx_gmxx_tx_hg2_reg1_s cn63xxp1;
	struct cvmx_gmxx_tx_hg2_reg1_s cn66xx;
	struct cvmx_gmxx_tx_hg2_reg1_s cn68xx;
	struct cvmx_gmxx_tx_hg2_reg1_s cn68xxp1;
	struct cvmx_gmxx_tx_hg2_reg1_s cnf71xx;
};

union cvmx_gmxx_tx_hg2_reg2 {
	uint64_t u64;
	struct cvmx_gmxx_tx_hg2_reg2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t tx_xon:16;
#else
		uint64_t tx_xon:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_gmxx_tx_hg2_reg2_s cn52xx;
	struct cvmx_gmxx_tx_hg2_reg2_s cn52xxp1;
	struct cvmx_gmxx_tx_hg2_reg2_s cn56xx;
	struct cvmx_gmxx_tx_hg2_reg2_s cn61xx;
	struct cvmx_gmxx_tx_hg2_reg2_s cn63xx;
	struct cvmx_gmxx_tx_hg2_reg2_s cn63xxp1;
	struct cvmx_gmxx_tx_hg2_reg2_s cn66xx;
	struct cvmx_gmxx_tx_hg2_reg2_s cn68xx;
	struct cvmx_gmxx_tx_hg2_reg2_s cn68xxp1;
	struct cvmx_gmxx_tx_hg2_reg2_s cnf71xx;
};

union cvmx_gmxx_tx_ifg {
	uint64_t u64;
	struct cvmx_gmxx_tx_ifg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t ifg2:4;
		uint64_t ifg1:4;
#else
		uint64_t ifg1:4;
		uint64_t ifg2:4;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_gmxx_tx_ifg_s cn30xx;
	struct cvmx_gmxx_tx_ifg_s cn31xx;
	struct cvmx_gmxx_tx_ifg_s cn38xx;
	struct cvmx_gmxx_tx_ifg_s cn38xxp2;
	struct cvmx_gmxx_tx_ifg_s cn50xx;
	struct cvmx_gmxx_tx_ifg_s cn52xx;
	struct cvmx_gmxx_tx_ifg_s cn52xxp1;
	struct cvmx_gmxx_tx_ifg_s cn56xx;
	struct cvmx_gmxx_tx_ifg_s cn56xxp1;
	struct cvmx_gmxx_tx_ifg_s cn58xx;
	struct cvmx_gmxx_tx_ifg_s cn58xxp1;
	struct cvmx_gmxx_tx_ifg_s cn61xx;
	struct cvmx_gmxx_tx_ifg_s cn63xx;
	struct cvmx_gmxx_tx_ifg_s cn63xxp1;
	struct cvmx_gmxx_tx_ifg_s cn66xx;
	struct cvmx_gmxx_tx_ifg_s cn68xx;
	struct cvmx_gmxx_tx_ifg_s cn68xxp1;
	struct cvmx_gmxx_tx_ifg_s cnf71xx;
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
	struct cvmx_gmxx_tx_int_en_cn30xx cn50xx;
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
	struct cvmx_gmxx_tx_int_en_cn52xx cn52xxp1;
	struct cvmx_gmxx_tx_int_en_cn52xx cn56xx;
	struct cvmx_gmxx_tx_int_en_cn52xx cn56xxp1;
	struct cvmx_gmxx_tx_int_en_cn38xx cn58xx;
	struct cvmx_gmxx_tx_int_en_cn38xx cn58xxp1;
	struct cvmx_gmxx_tx_int_en_s cn61xx;
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
	struct cvmx_gmxx_tx_int_en_cn63xx cn63xxp1;
	struct cvmx_gmxx_tx_int_en_s cn66xx;
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
	struct cvmx_gmxx_tx_int_en_cn68xx cn68xxp1;
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
	struct cvmx_gmxx_tx_int_reg_cn30xx cn50xx;
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
	struct cvmx_gmxx_tx_int_reg_cn52xx cn52xxp1;
	struct cvmx_gmxx_tx_int_reg_cn52xx cn56xx;
	struct cvmx_gmxx_tx_int_reg_cn52xx cn56xxp1;
	struct cvmx_gmxx_tx_int_reg_cn38xx cn58xx;
	struct cvmx_gmxx_tx_int_reg_cn38xx cn58xxp1;
	struct cvmx_gmxx_tx_int_reg_s cn61xx;
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
	struct cvmx_gmxx_tx_int_reg_cn63xx cn63xxp1;
	struct cvmx_gmxx_tx_int_reg_s cn66xx;
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
	struct cvmx_gmxx_tx_int_reg_cn68xx cn68xxp1;
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

union cvmx_gmxx_tx_jam {
	uint64_t u64;
	struct cvmx_gmxx_tx_jam_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t jam:8;
#else
		uint64_t jam:8;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_gmxx_tx_jam_s cn30xx;
	struct cvmx_gmxx_tx_jam_s cn31xx;
	struct cvmx_gmxx_tx_jam_s cn38xx;
	struct cvmx_gmxx_tx_jam_s cn38xxp2;
	struct cvmx_gmxx_tx_jam_s cn50xx;
	struct cvmx_gmxx_tx_jam_s cn52xx;
	struct cvmx_gmxx_tx_jam_s cn52xxp1;
	struct cvmx_gmxx_tx_jam_s cn56xx;
	struct cvmx_gmxx_tx_jam_s cn56xxp1;
	struct cvmx_gmxx_tx_jam_s cn58xx;
	struct cvmx_gmxx_tx_jam_s cn58xxp1;
	struct cvmx_gmxx_tx_jam_s cn61xx;
	struct cvmx_gmxx_tx_jam_s cn63xx;
	struct cvmx_gmxx_tx_jam_s cn63xxp1;
	struct cvmx_gmxx_tx_jam_s cn66xx;
	struct cvmx_gmxx_tx_jam_s cn68xx;
	struct cvmx_gmxx_tx_jam_s cn68xxp1;
	struct cvmx_gmxx_tx_jam_s cnf71xx;
};

union cvmx_gmxx_tx_lfsr {
	uint64_t u64;
	struct cvmx_gmxx_tx_lfsr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t lfsr:16;
#else
		uint64_t lfsr:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_gmxx_tx_lfsr_s cn30xx;
	struct cvmx_gmxx_tx_lfsr_s cn31xx;
	struct cvmx_gmxx_tx_lfsr_s cn38xx;
	struct cvmx_gmxx_tx_lfsr_s cn38xxp2;
	struct cvmx_gmxx_tx_lfsr_s cn50xx;
	struct cvmx_gmxx_tx_lfsr_s cn52xx;
	struct cvmx_gmxx_tx_lfsr_s cn52xxp1;
	struct cvmx_gmxx_tx_lfsr_s cn56xx;
	struct cvmx_gmxx_tx_lfsr_s cn56xxp1;
	struct cvmx_gmxx_tx_lfsr_s cn58xx;
	struct cvmx_gmxx_tx_lfsr_s cn58xxp1;
	struct cvmx_gmxx_tx_lfsr_s cn61xx;
	struct cvmx_gmxx_tx_lfsr_s cn63xx;
	struct cvmx_gmxx_tx_lfsr_s cn63xxp1;
	struct cvmx_gmxx_tx_lfsr_s cn66xx;
	struct cvmx_gmxx_tx_lfsr_s cn68xx;
	struct cvmx_gmxx_tx_lfsr_s cn68xxp1;
	struct cvmx_gmxx_tx_lfsr_s cnf71xx;
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
	struct cvmx_gmxx_tx_ovr_bp_cn30xx cn31xx;
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
	struct cvmx_gmxx_tx_ovr_bp_cn38xx cn38xxp2;
	struct cvmx_gmxx_tx_ovr_bp_cn30xx cn50xx;
	struct cvmx_gmxx_tx_ovr_bp_s cn52xx;
	struct cvmx_gmxx_tx_ovr_bp_s cn52xxp1;
	struct cvmx_gmxx_tx_ovr_bp_s cn56xx;
	struct cvmx_gmxx_tx_ovr_bp_s cn56xxp1;
	struct cvmx_gmxx_tx_ovr_bp_cn38xx cn58xx;
	struct cvmx_gmxx_tx_ovr_bp_cn38xx cn58xxp1;
	struct cvmx_gmxx_tx_ovr_bp_s cn61xx;
	struct cvmx_gmxx_tx_ovr_bp_s cn63xx;
	struct cvmx_gmxx_tx_ovr_bp_s cn63xxp1;
	struct cvmx_gmxx_tx_ovr_bp_s cn66xx;
	struct cvmx_gmxx_tx_ovr_bp_s cn68xx;
	struct cvmx_gmxx_tx_ovr_bp_s cn68xxp1;
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

union cvmx_gmxx_tx_pause_pkt_dmac {
	uint64_t u64;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t dmac:48;
#else
		uint64_t dmac:48;
		uint64_t reserved_48_63:16;
#endif
	} s;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s cn30xx;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s cn31xx;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s cn38xx;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s cn38xxp2;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s cn50xx;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s cn52xx;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s cn52xxp1;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s cn56xx;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s cn56xxp1;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s cn58xx;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s cn58xxp1;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s cn61xx;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s cn63xx;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s cn63xxp1;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s cn66xx;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s cn68xx;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s cn68xxp1;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s cnf71xx;
};

union cvmx_gmxx_tx_pause_pkt_type {
	uint64_t u64;
	struct cvmx_gmxx_tx_pause_pkt_type_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t type:16;
#else
		uint64_t type:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_gmxx_tx_pause_pkt_type_s cn30xx;
	struct cvmx_gmxx_tx_pause_pkt_type_s cn31xx;
	struct cvmx_gmxx_tx_pause_pkt_type_s cn38xx;
	struct cvmx_gmxx_tx_pause_pkt_type_s cn38xxp2;
	struct cvmx_gmxx_tx_pause_pkt_type_s cn50xx;
	struct cvmx_gmxx_tx_pause_pkt_type_s cn52xx;
	struct cvmx_gmxx_tx_pause_pkt_type_s cn52xxp1;
	struct cvmx_gmxx_tx_pause_pkt_type_s cn56xx;
	struct cvmx_gmxx_tx_pause_pkt_type_s cn56xxp1;
	struct cvmx_gmxx_tx_pause_pkt_type_s cn58xx;
	struct cvmx_gmxx_tx_pause_pkt_type_s cn58xxp1;
	struct cvmx_gmxx_tx_pause_pkt_type_s cn61xx;
	struct cvmx_gmxx_tx_pause_pkt_type_s cn63xx;
	struct cvmx_gmxx_tx_pause_pkt_type_s cn63xxp1;
	struct cvmx_gmxx_tx_pause_pkt_type_s cn66xx;
	struct cvmx_gmxx_tx_pause_pkt_type_s cn68xx;
	struct cvmx_gmxx_tx_pause_pkt_type_s cn68xxp1;
	struct cvmx_gmxx_tx_pause_pkt_type_s cnf71xx;
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
	struct cvmx_gmxx_tx_prts_s cn30xx;
	struct cvmx_gmxx_tx_prts_s cn31xx;
	struct cvmx_gmxx_tx_prts_s cn38xx;
	struct cvmx_gmxx_tx_prts_s cn38xxp2;
	struct cvmx_gmxx_tx_prts_s cn50xx;
	struct cvmx_gmxx_tx_prts_s cn52xx;
	struct cvmx_gmxx_tx_prts_s cn52xxp1;
	struct cvmx_gmxx_tx_prts_s cn56xx;
	struct cvmx_gmxx_tx_prts_s cn56xxp1;
	struct cvmx_gmxx_tx_prts_s cn58xx;
	struct cvmx_gmxx_tx_prts_s cn58xxp1;
	struct cvmx_gmxx_tx_prts_s cn61xx;
	struct cvmx_gmxx_tx_prts_s cn63xx;
	struct cvmx_gmxx_tx_prts_s cn63xxp1;
	struct cvmx_gmxx_tx_prts_s cn66xx;
	struct cvmx_gmxx_tx_prts_s cn68xx;
	struct cvmx_gmxx_tx_prts_s cn68xxp1;
	struct cvmx_gmxx_tx_prts_s cnf71xx;
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
	struct cvmx_gmxx_tx_spi_ctl_s cn38xx;
	struct cvmx_gmxx_tx_spi_ctl_s cn38xxp2;
	struct cvmx_gmxx_tx_spi_ctl_s cn58xx;
	struct cvmx_gmxx_tx_spi_ctl_s cn58xxp1;
};

union cvmx_gmxx_tx_spi_drain {
	uint64_t u64;
	struct cvmx_gmxx_tx_spi_drain_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t drain:16;
#else
		uint64_t drain:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_gmxx_tx_spi_drain_s cn38xx;
	struct cvmx_gmxx_tx_spi_drain_s cn58xx;
	struct cvmx_gmxx_tx_spi_drain_s cn58xxp1;
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
	struct cvmx_gmxx_tx_spi_max_cn38xx cn38xxp2;
	struct cvmx_gmxx_tx_spi_max_s cn58xx;
	struct cvmx_gmxx_tx_spi_max_s cn58xxp1;
};

union cvmx_gmxx_tx_spi_roundx {
	uint64_t u64;
	struct cvmx_gmxx_tx_spi_roundx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t round:16;
#else
		uint64_t round:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_gmxx_tx_spi_roundx_s cn58xx;
	struct cvmx_gmxx_tx_spi_roundx_s cn58xxp1;
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
	struct cvmx_gmxx_tx_spi_thresh_s cn38xx;
	struct cvmx_gmxx_tx_spi_thresh_s cn38xxp2;
	struct cvmx_gmxx_tx_spi_thresh_s cn58xx;
	struct cvmx_gmxx_tx_spi_thresh_s cn58xxp1;
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
	struct cvmx_gmxx_tx_xaui_ctl_s cn52xx;
	struct cvmx_gmxx_tx_xaui_ctl_s cn52xxp1;
	struct cvmx_gmxx_tx_xaui_ctl_s cn56xx;
	struct cvmx_gmxx_tx_xaui_ctl_s cn56xxp1;
	struct cvmx_gmxx_tx_xaui_ctl_s cn61xx;
	struct cvmx_gmxx_tx_xaui_ctl_s cn63xx;
	struct cvmx_gmxx_tx_xaui_ctl_s cn63xxp1;
	struct cvmx_gmxx_tx_xaui_ctl_s cn66xx;
	struct cvmx_gmxx_tx_xaui_ctl_s cn68xx;
	struct cvmx_gmxx_tx_xaui_ctl_s cn68xxp1;
	struct cvmx_gmxx_tx_xaui_ctl_s cnf71xx;
};

union cvmx_gmxx_xaui_ext_loopback {
	uint64_t u64;
	struct cvmx_gmxx_xaui_ext_loopback_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_5_63:59;
		uint64_t en:1;
		uint64_t thresh:4;
#else
		uint64_t thresh:4;
		uint64_t en:1;
		uint64_t reserved_5_63:59;
#endif
	} s;
	struct cvmx_gmxx_xaui_ext_loopback_s cn52xx;
	struct cvmx_gmxx_xaui_ext_loopback_s cn52xxp1;
	struct cvmx_gmxx_xaui_ext_loopback_s cn56xx;
	struct cvmx_gmxx_xaui_ext_loopback_s cn56xxp1;
	struct cvmx_gmxx_xaui_ext_loopback_s cn61xx;
	struct cvmx_gmxx_xaui_ext_loopback_s cn63xx;
	struct cvmx_gmxx_xaui_ext_loopback_s cn63xxp1;
	struct cvmx_gmxx_xaui_ext_loopback_s cn66xx;
	struct cvmx_gmxx_xaui_ext_loopback_s cn68xx;
	struct cvmx_gmxx_xaui_ext_loopback_s cn68xxp1;
	struct cvmx_gmxx_xaui_ext_loopback_s cnf71xx;
};

#endif

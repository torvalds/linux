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

#ifndef __CVMX_PCSX_DEFS_H__
#define __CVMX_PCSX_DEFS_H__

static inline uint64_t CVMX_PCSX_ANX_ADV_REG(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001010ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001010ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001010ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001010ull) + ((offset) + (block_id) * 0x4000ull) * 1024;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0001010ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
}

static inline uint64_t CVMX_PCSX_ANX_EXT_ST_REG(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001028ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001028ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001028ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001028ull) + ((offset) + (block_id) * 0x4000ull) * 1024;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0001028ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
}

static inline uint64_t CVMX_PCSX_ANX_LP_ABIL_REG(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001018ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001018ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001018ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001018ull) + ((offset) + (block_id) * 0x4000ull) * 1024;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0001018ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
}

static inline uint64_t CVMX_PCSX_ANX_RESULTS_REG(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001020ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001020ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001020ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001020ull) + ((offset) + (block_id) * 0x4000ull) * 1024;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0001020ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
}

static inline uint64_t CVMX_PCSX_INTX_EN_REG(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001088ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001088ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001088ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001088ull) + ((offset) + (block_id) * 0x4000ull) * 1024;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0001088ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
}

static inline uint64_t CVMX_PCSX_INTX_REG(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001080ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001080ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001080ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001080ull) + ((offset) + (block_id) * 0x4000ull) * 1024;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0001080ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
}

static inline uint64_t CVMX_PCSX_LINKX_TIMER_COUNT_REG(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001040ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001040ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001040ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001040ull) + ((offset) + (block_id) * 0x4000ull) * 1024;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0001040ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
}

static inline uint64_t CVMX_PCSX_LOG_ANLX_REG(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001090ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001090ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001090ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001090ull) + ((offset) + (block_id) * 0x4000ull) * 1024;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0001090ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
}

static inline uint64_t CVMX_PCSX_MISCX_CTL_REG(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001078ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001078ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001078ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001078ull) + ((offset) + (block_id) * 0x4000ull) * 1024;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0001078ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
}

static inline uint64_t CVMX_PCSX_MRX_CONTROL_REG(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001000ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001000ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001000ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001000ull) + ((offset) + (block_id) * 0x4000ull) * 1024;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0001000ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
}

static inline uint64_t CVMX_PCSX_MRX_STATUS_REG(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001008ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001008ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001008ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001008ull) + ((offset) + (block_id) * 0x4000ull) * 1024;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0001008ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
}

static inline uint64_t CVMX_PCSX_RXX_STATES_REG(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001058ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001058ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001058ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001058ull) + ((offset) + (block_id) * 0x4000ull) * 1024;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0001058ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
}

static inline uint64_t CVMX_PCSX_RXX_SYNC_REG(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001050ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001050ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001050ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001050ull) + ((offset) + (block_id) * 0x4000ull) * 1024;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0001050ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
}

static inline uint64_t CVMX_PCSX_SGMX_AN_ADV_REG(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001068ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001068ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001068ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001068ull) + ((offset) + (block_id) * 0x4000ull) * 1024;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0001068ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
}

static inline uint64_t CVMX_PCSX_SGMX_LP_ADV_REG(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001070ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001070ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001070ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001070ull) + ((offset) + (block_id) * 0x4000ull) * 1024;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0001070ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
}

static inline uint64_t CVMX_PCSX_TXX_STATES_REG(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001060ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001060ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001060ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001060ull) + ((offset) + (block_id) * 0x4000ull) * 1024;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0001060ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
}

static inline uint64_t CVMX_PCSX_TX_RXX_POLARITY_REG(unsigned long offset, unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001048ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001048ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001048ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800B0001048ull) + ((offset) + (block_id) * 0x4000ull) * 1024;
	}
	return CVMX_ADD_IO_SEG(0x00011800B0001048ull) + ((offset) + (block_id) * 0x20000ull) * 1024;
}

void __cvmx_interrupt_pcsx_intx_en_reg_enable(int index, int block);

union cvmx_pcsx_anx_adv_reg {
	uint64_t u64;
	struct cvmx_pcsx_anx_adv_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t np:1;
		uint64_t reserved_14_14:1;
		uint64_t rem_flt:2;
		uint64_t reserved_9_11:3;
		uint64_t pause:2;
		uint64_t hfd:1;
		uint64_t fd:1;
		uint64_t reserved_0_4:5;
#else
		uint64_t reserved_0_4:5;
		uint64_t fd:1;
		uint64_t hfd:1;
		uint64_t pause:2;
		uint64_t reserved_9_11:3;
		uint64_t rem_flt:2;
		uint64_t reserved_14_14:1;
		uint64_t np:1;
		uint64_t reserved_16_63:48;
#endif
	} s;
};

union cvmx_pcsx_anx_ext_st_reg {
	uint64_t u64;
	struct cvmx_pcsx_anx_ext_st_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t thou_xfd:1;
		uint64_t thou_xhd:1;
		uint64_t thou_tfd:1;
		uint64_t thou_thd:1;
		uint64_t reserved_0_11:12;
#else
		uint64_t reserved_0_11:12;
		uint64_t thou_thd:1;
		uint64_t thou_tfd:1;
		uint64_t thou_xhd:1;
		uint64_t thou_xfd:1;
		uint64_t reserved_16_63:48;
#endif
	} s;
};

union cvmx_pcsx_anx_lp_abil_reg {
	uint64_t u64;
	struct cvmx_pcsx_anx_lp_abil_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t np:1;
		uint64_t ack:1;
		uint64_t rem_flt:2;
		uint64_t reserved_9_11:3;
		uint64_t pause:2;
		uint64_t hfd:1;
		uint64_t fd:1;
		uint64_t reserved_0_4:5;
#else
		uint64_t reserved_0_4:5;
		uint64_t fd:1;
		uint64_t hfd:1;
		uint64_t pause:2;
		uint64_t reserved_9_11:3;
		uint64_t rem_flt:2;
		uint64_t ack:1;
		uint64_t np:1;
		uint64_t reserved_16_63:48;
#endif
	} s;
};

union cvmx_pcsx_anx_results_reg {
	uint64_t u64;
	struct cvmx_pcsx_anx_results_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t pause:2;
		uint64_t spd:2;
		uint64_t an_cpt:1;
		uint64_t dup:1;
		uint64_t link_ok:1;
#else
		uint64_t link_ok:1;
		uint64_t dup:1;
		uint64_t an_cpt:1;
		uint64_t spd:2;
		uint64_t pause:2;
		uint64_t reserved_7_63:57;
#endif
	} s;
};

union cvmx_pcsx_intx_en_reg {
	uint64_t u64;
	struct cvmx_pcsx_intx_en_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_13_63:51;
		uint64_t dbg_sync_en:1;
		uint64_t dup:1;
		uint64_t sync_bad_en:1;
		uint64_t an_bad_en:1;
		uint64_t rxlock_en:1;
		uint64_t rxbad_en:1;
		uint64_t rxerr_en:1;
		uint64_t txbad_en:1;
		uint64_t txfifo_en:1;
		uint64_t txfifu_en:1;
		uint64_t an_err_en:1;
		uint64_t xmit_en:1;
		uint64_t lnkspd_en:1;
#else
		uint64_t lnkspd_en:1;
		uint64_t xmit_en:1;
		uint64_t an_err_en:1;
		uint64_t txfifu_en:1;
		uint64_t txfifo_en:1;
		uint64_t txbad_en:1;
		uint64_t rxerr_en:1;
		uint64_t rxbad_en:1;
		uint64_t rxlock_en:1;
		uint64_t an_bad_en:1;
		uint64_t sync_bad_en:1;
		uint64_t dup:1;
		uint64_t dbg_sync_en:1;
		uint64_t reserved_13_63:51;
#endif
	} s;
	struct cvmx_pcsx_intx_en_reg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t dup:1;
		uint64_t sync_bad_en:1;
		uint64_t an_bad_en:1;
		uint64_t rxlock_en:1;
		uint64_t rxbad_en:1;
		uint64_t rxerr_en:1;
		uint64_t txbad_en:1;
		uint64_t txfifo_en:1;
		uint64_t txfifu_en:1;
		uint64_t an_err_en:1;
		uint64_t xmit_en:1;
		uint64_t lnkspd_en:1;
#else
		uint64_t lnkspd_en:1;
		uint64_t xmit_en:1;
		uint64_t an_err_en:1;
		uint64_t txfifu_en:1;
		uint64_t txfifo_en:1;
		uint64_t txbad_en:1;
		uint64_t rxerr_en:1;
		uint64_t rxbad_en:1;
		uint64_t rxlock_en:1;
		uint64_t an_bad_en:1;
		uint64_t sync_bad_en:1;
		uint64_t dup:1;
		uint64_t reserved_12_63:52;
#endif
	} cn52xx;
};

union cvmx_pcsx_intx_reg {
	uint64_t u64;
	struct cvmx_pcsx_intx_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_13_63:51;
		uint64_t dbg_sync:1;
		uint64_t dup:1;
		uint64_t sync_bad:1;
		uint64_t an_bad:1;
		uint64_t rxlock:1;
		uint64_t rxbad:1;
		uint64_t rxerr:1;
		uint64_t txbad:1;
		uint64_t txfifo:1;
		uint64_t txfifu:1;
		uint64_t an_err:1;
		uint64_t xmit:1;
		uint64_t lnkspd:1;
#else
		uint64_t lnkspd:1;
		uint64_t xmit:1;
		uint64_t an_err:1;
		uint64_t txfifu:1;
		uint64_t txfifo:1;
		uint64_t txbad:1;
		uint64_t rxerr:1;
		uint64_t rxbad:1;
		uint64_t rxlock:1;
		uint64_t an_bad:1;
		uint64_t sync_bad:1;
		uint64_t dup:1;
		uint64_t dbg_sync:1;
		uint64_t reserved_13_63:51;
#endif
	} s;
	struct cvmx_pcsx_intx_reg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t dup:1;
		uint64_t sync_bad:1;
		uint64_t an_bad:1;
		uint64_t rxlock:1;
		uint64_t rxbad:1;
		uint64_t rxerr:1;
		uint64_t txbad:1;
		uint64_t txfifo:1;
		uint64_t txfifu:1;
		uint64_t an_err:1;
		uint64_t xmit:1;
		uint64_t lnkspd:1;
#else
		uint64_t lnkspd:1;
		uint64_t xmit:1;
		uint64_t an_err:1;
		uint64_t txfifu:1;
		uint64_t txfifo:1;
		uint64_t txbad:1;
		uint64_t rxerr:1;
		uint64_t rxbad:1;
		uint64_t rxlock:1;
		uint64_t an_bad:1;
		uint64_t sync_bad:1;
		uint64_t dup:1;
		uint64_t reserved_12_63:52;
#endif
	} cn52xx;
};

union cvmx_pcsx_linkx_timer_count_reg {
	uint64_t u64;
	struct cvmx_pcsx_linkx_timer_count_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t count:16;
#else
		uint64_t count:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
};

union cvmx_pcsx_log_anlx_reg {
	uint64_t u64;
	struct cvmx_pcsx_log_anlx_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t lafifovfl:1;
		uint64_t la_en:1;
		uint64_t pkt_sz:2;
#else
		uint64_t pkt_sz:2;
		uint64_t la_en:1;
		uint64_t lafifovfl:1;
		uint64_t reserved_4_63:60;
#endif
	} s;
};

union cvmx_pcsx_miscx_ctl_reg {
	uint64_t u64;
	struct cvmx_pcsx_miscx_ctl_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_13_63:51;
		uint64_t sgmii:1;
		uint64_t gmxeno:1;
		uint64_t loopbck2:1;
		uint64_t mac_phy:1;
		uint64_t mode:1;
		uint64_t an_ovrd:1;
		uint64_t samp_pt:7;
#else
		uint64_t samp_pt:7;
		uint64_t an_ovrd:1;
		uint64_t mode:1;
		uint64_t mac_phy:1;
		uint64_t loopbck2:1;
		uint64_t gmxeno:1;
		uint64_t sgmii:1;
		uint64_t reserved_13_63:51;
#endif
	} s;
};

union cvmx_pcsx_mrx_control_reg {
	uint64_t u64;
	struct cvmx_pcsx_mrx_control_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t reset:1;
		uint64_t loopbck1:1;
		uint64_t spdlsb:1;
		uint64_t an_en:1;
		uint64_t pwr_dn:1;
		uint64_t reserved_10_10:1;
		uint64_t rst_an:1;
		uint64_t dup:1;
		uint64_t coltst:1;
		uint64_t spdmsb:1;
		uint64_t uni:1;
		uint64_t reserved_0_4:5;
#else
		uint64_t reserved_0_4:5;
		uint64_t uni:1;
		uint64_t spdmsb:1;
		uint64_t coltst:1;
		uint64_t dup:1;
		uint64_t rst_an:1;
		uint64_t reserved_10_10:1;
		uint64_t pwr_dn:1;
		uint64_t an_en:1;
		uint64_t spdlsb:1;
		uint64_t loopbck1:1;
		uint64_t reset:1;
		uint64_t reserved_16_63:48;
#endif
	} s;
};

union cvmx_pcsx_mrx_status_reg {
	uint64_t u64;
	struct cvmx_pcsx_mrx_status_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t hun_t4:1;
		uint64_t hun_xfd:1;
		uint64_t hun_xhd:1;
		uint64_t ten_fd:1;
		uint64_t ten_hd:1;
		uint64_t hun_t2fd:1;
		uint64_t hun_t2hd:1;
		uint64_t ext_st:1;
		uint64_t reserved_7_7:1;
		uint64_t prb_sup:1;
		uint64_t an_cpt:1;
		uint64_t rm_flt:1;
		uint64_t an_abil:1;
		uint64_t lnk_st:1;
		uint64_t reserved_1_1:1;
		uint64_t extnd:1;
#else
		uint64_t extnd:1;
		uint64_t reserved_1_1:1;
		uint64_t lnk_st:1;
		uint64_t an_abil:1;
		uint64_t rm_flt:1;
		uint64_t an_cpt:1;
		uint64_t prb_sup:1;
		uint64_t reserved_7_7:1;
		uint64_t ext_st:1;
		uint64_t hun_t2hd:1;
		uint64_t hun_t2fd:1;
		uint64_t ten_hd:1;
		uint64_t ten_fd:1;
		uint64_t hun_xhd:1;
		uint64_t hun_xfd:1;
		uint64_t hun_t4:1;
		uint64_t reserved_16_63:48;
#endif
	} s;
};

union cvmx_pcsx_rxx_states_reg {
	uint64_t u64;
	struct cvmx_pcsx_rxx_states_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t rx_bad:1;
		uint64_t rx_st:5;
		uint64_t sync_bad:1;
		uint64_t sync:4;
		uint64_t an_bad:1;
		uint64_t an_st:4;
#else
		uint64_t an_st:4;
		uint64_t an_bad:1;
		uint64_t sync:4;
		uint64_t sync_bad:1;
		uint64_t rx_st:5;
		uint64_t rx_bad:1;
		uint64_t reserved_16_63:48;
#endif
	} s;
};

union cvmx_pcsx_rxx_sync_reg {
	uint64_t u64;
	struct cvmx_pcsx_rxx_sync_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t sync:1;
		uint64_t bit_lock:1;
#else
		uint64_t bit_lock:1;
		uint64_t sync:1;
		uint64_t reserved_2_63:62;
#endif
	} s;
};

union cvmx_pcsx_sgmx_an_adv_reg {
	uint64_t u64;
	struct cvmx_pcsx_sgmx_an_adv_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t link:1;
		uint64_t ack:1;
		uint64_t reserved_13_13:1;
		uint64_t dup:1;
		uint64_t speed:2;
		uint64_t reserved_1_9:9;
		uint64_t one:1;
#else
		uint64_t one:1;
		uint64_t reserved_1_9:9;
		uint64_t speed:2;
		uint64_t dup:1;
		uint64_t reserved_13_13:1;
		uint64_t ack:1;
		uint64_t link:1;
		uint64_t reserved_16_63:48;
#endif
	} s;
};

union cvmx_pcsx_sgmx_lp_adv_reg {
	uint64_t u64;
	struct cvmx_pcsx_sgmx_lp_adv_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t link:1;
		uint64_t reserved_13_14:2;
		uint64_t dup:1;
		uint64_t speed:2;
		uint64_t reserved_1_9:9;
		uint64_t one:1;
#else
		uint64_t one:1;
		uint64_t reserved_1_9:9;
		uint64_t speed:2;
		uint64_t dup:1;
		uint64_t reserved_13_14:2;
		uint64_t link:1;
		uint64_t reserved_16_63:48;
#endif
	} s;
};

union cvmx_pcsx_txx_states_reg {
	uint64_t u64;
	struct cvmx_pcsx_txx_states_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t xmit:2;
		uint64_t tx_bad:1;
		uint64_t ord_st:4;
#else
		uint64_t ord_st:4;
		uint64_t tx_bad:1;
		uint64_t xmit:2;
		uint64_t reserved_7_63:57;
#endif
	} s;
};

union cvmx_pcsx_tx_rxx_polarity_reg {
	uint64_t u64;
	struct cvmx_pcsx_tx_rxx_polarity_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t rxovrd:1;
		uint64_t autorxpl:1;
		uint64_t rxplrt:1;
		uint64_t txplrt:1;
#else
		uint64_t txplrt:1;
		uint64_t rxplrt:1;
		uint64_t autorxpl:1;
		uint64_t rxovrd:1;
		uint64_t reserved_4_63:60;
#endif
	} s;
};

#endif

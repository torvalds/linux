/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2017 Cavium, Inc.
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

#ifndef __CVMX_SLI_DEFS_H__
#define __CVMX_SLI_DEFS_H__

#include <uapi/asm/bitfield.h>

#define CVMX_SLI_PCIE_MSI_RCV CVMX_SLI_PCIE_MSI_RCV_FUNC()
static inline uint64_t CVMX_SLI_PCIE_MSI_RCV_FUNC(void)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN70XX & OCTEON_FAMILY_MASK:
		return 0x0000000000003CB0ull;
	case OCTEON_CNF75XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN73XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN78XX & OCTEON_FAMILY_MASK:
		if (OCTEON_IS_MODEL(OCTEON_CN78XX_PASS1_X))
			return 0x0000000000003CB0ull;
		/* Else, fall through */
	default:
		return 0x0000000000023CB0ull;
	}
}


union cvmx_sli_ctl_portx {
	uint64_t u64;
	struct cvmx_sli_ctl_portx_s {
		__BITFIELD_FIELD(uint64_t reserved_22_63:42,
		__BITFIELD_FIELD(uint64_t intd:1,
		__BITFIELD_FIELD(uint64_t intc:1,
		__BITFIELD_FIELD(uint64_t intb:1,
		__BITFIELD_FIELD(uint64_t inta:1,
		__BITFIELD_FIELD(uint64_t dis_port:1,
		__BITFIELD_FIELD(uint64_t waitl_com:1,
		__BITFIELD_FIELD(uint64_t intd_map:2,
		__BITFIELD_FIELD(uint64_t intc_map:2,
		__BITFIELD_FIELD(uint64_t intb_map:2,
		__BITFIELD_FIELD(uint64_t inta_map:2,
		__BITFIELD_FIELD(uint64_t ctlp_ro:1,
		__BITFIELD_FIELD(uint64_t reserved_6_6:1,
		__BITFIELD_FIELD(uint64_t ptlp_ro:1,
		__BITFIELD_FIELD(uint64_t reserved_1_4:4,
		__BITFIELD_FIELD(uint64_t wait_com:1,
		;))))))))))))))))
	} s;
};

union cvmx_sli_mem_access_ctl {
	uint64_t u64;
	struct cvmx_sli_mem_access_ctl_s {
		__BITFIELD_FIELD(uint64_t reserved_14_63:50,
		__BITFIELD_FIELD(uint64_t max_word:4,
		__BITFIELD_FIELD(uint64_t timer:10,
		;)))
	} s;
};

union cvmx_sli_s2m_portx_ctl {
	uint64_t u64;
	struct cvmx_sli_s2m_portx_ctl_s {
		__BITFIELD_FIELD(uint64_t reserved_5_63:59,
		__BITFIELD_FIELD(uint64_t wind_d:1,
		__BITFIELD_FIELD(uint64_t bar0_d:1,
		__BITFIELD_FIELD(uint64_t mrrs:3,
		;))))
	} s;
};

union cvmx_sli_mem_access_subidx {
	uint64_t u64;
	struct cvmx_sli_mem_access_subidx_s {
		__BITFIELD_FIELD(uint64_t reserved_43_63:21,
		__BITFIELD_FIELD(uint64_t zero:1,
		__BITFIELD_FIELD(uint64_t port:3,
		__BITFIELD_FIELD(uint64_t nmerge:1,
		__BITFIELD_FIELD(uint64_t esr:2,
		__BITFIELD_FIELD(uint64_t esw:2,
		__BITFIELD_FIELD(uint64_t wtype:2,
		__BITFIELD_FIELD(uint64_t rtype:2,
		__BITFIELD_FIELD(uint64_t ba:30,
		;)))))))))
	} s;
	struct cvmx_sli_mem_access_subidx_cn68xx {
		__BITFIELD_FIELD(uint64_t reserved_43_63:21,
		__BITFIELD_FIELD(uint64_t zero:1,
		__BITFIELD_FIELD(uint64_t port:3,
		__BITFIELD_FIELD(uint64_t nmerge:1,
		__BITFIELD_FIELD(uint64_t esr:2,
		__BITFIELD_FIELD(uint64_t esw:2,
		__BITFIELD_FIELD(uint64_t wtype:2,
		__BITFIELD_FIELD(uint64_t rtype:2,
		__BITFIELD_FIELD(uint64_t ba:28,
		__BITFIELD_FIELD(uint64_t reserved_0_1:2,
		;))))))))))
	} cn68xx;
};

#endif

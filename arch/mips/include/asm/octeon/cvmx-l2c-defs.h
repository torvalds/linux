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

#ifndef __CVMX_L2C_DEFS_H__
#define __CVMX_L2C_DEFS_H__

#include <uapi/asm/bitfield.h>

#define CVMX_L2C_DBG (CVMX_ADD_IO_SEG(0x0001180080000030ull))
#define CVMX_L2C_CFG (CVMX_ADD_IO_SEG(0x0001180080000000ull))
#define CVMX_L2C_CTL (CVMX_ADD_IO_SEG(0x0001180080800000ull))
#define CVMX_L2C_ERR_TDTX(block_id)					       \
	(CVMX_ADD_IO_SEG(0x0001180080A007E0ull) + ((block_id) & 3) * 0x40000ull)
#define CVMX_L2C_ERR_TTGX(block_id)					       \
	(CVMX_ADD_IO_SEG(0x0001180080A007E8ull) + ((block_id) & 3) * 0x40000ull)
#define CVMX_L2C_LCKBASE (CVMX_ADD_IO_SEG(0x0001180080000058ull))
#define CVMX_L2C_LCKOFF (CVMX_ADD_IO_SEG(0x0001180080000060ull))
#define CVMX_L2C_PFCTL (CVMX_ADD_IO_SEG(0x0001180080000090ull))
#define CVMX_L2C_PFCX(offset) (CVMX_ADD_IO_SEG(0x0001180080000098ull) +	       \
		((offset) & 3) * 8)
#define CVMX_L2C_PFC0 CVMX_L2C_PFCX(0)
#define CVMX_L2C_PFC1 CVMX_L2C_PFCX(1)
#define CVMX_L2C_PFC2 CVMX_L2C_PFCX(2)
#define CVMX_L2C_PFC3 CVMX_L2C_PFCX(3)
#define CVMX_L2C_SPAR0 (CVMX_ADD_IO_SEG(0x0001180080000068ull))
#define CVMX_L2C_SPAR1 (CVMX_ADD_IO_SEG(0x0001180080000070ull))
#define CVMX_L2C_SPAR2 (CVMX_ADD_IO_SEG(0x0001180080000078ull))
#define CVMX_L2C_SPAR3 (CVMX_ADD_IO_SEG(0x0001180080000080ull))
#define CVMX_L2C_SPAR4 (CVMX_ADD_IO_SEG(0x0001180080000088ull))
#define CVMX_L2C_TADX_PFCX(offset, block_id)				       \
		(CVMX_ADD_IO_SEG(0x0001180080A00400ull) + (((offset) & 3) +    \
		((block_id) & 7) * 0x8000ull) * 8)
#define CVMX_L2C_TADX_PFC0(block_id) (CVMX_ADD_IO_SEG(0x0001180080A00400ull) + \
		((block_id) & 3) * 0x40000ull)
#define CVMX_L2C_TADX_PFC1(block_id) (CVMX_ADD_IO_SEG(0x0001180080A00408ull) + \
		((block_id) & 3) * 0x40000ull)
#define CVMX_L2C_TADX_PFC2(block_id) (CVMX_ADD_IO_SEG(0x0001180080A00410ull) + \
		((block_id) & 3) * 0x40000ull)
#define CVMX_L2C_TADX_PFC3(block_id) (CVMX_ADD_IO_SEG(0x0001180080A00418ull) + \
		((block_id) & 3) * 0x40000ull)
#define CVMX_L2C_TADX_PRF(offset) (CVMX_ADD_IO_SEG(0x0001180080A00008ull)    + \
		((offset) & 7) * 0x40000ull)
#define CVMX_L2C_TADX_TAG(block_id) (CVMX_ADD_IO_SEG(0x0001180080A00010ull)  + \
		((block_id) & 3) * 0x40000ull)
#define CVMX_L2C_WPAR_IOBX(offset) (CVMX_ADD_IO_SEG(0x0001180080840200ull)   + \
		((offset) & 1) * 8)
#define CVMX_L2C_WPAR_PPX(offset) (CVMX_ADD_IO_SEG(0x0001180080840000ull)    + \
		((offset) & 31) * 8)


union cvmx_l2c_err_tdtx {
	uint64_t u64;
	struct cvmx_l2c_err_tdtx_s {
		__BITFIELD_FIELD(uint64_t dbe:1,
		__BITFIELD_FIELD(uint64_t sbe:1,
		__BITFIELD_FIELD(uint64_t vdbe:1,
		__BITFIELD_FIELD(uint64_t vsbe:1,
		__BITFIELD_FIELD(uint64_t syn:10,
		__BITFIELD_FIELD(uint64_t reserved_22_49:28,
		__BITFIELD_FIELD(uint64_t wayidx:18,
		__BITFIELD_FIELD(uint64_t reserved_2_3:2,
		__BITFIELD_FIELD(uint64_t type:2,
		;)))))))))
	} s;
};

union cvmx_l2c_err_ttgx {
	uint64_t u64;
	struct cvmx_l2c_err_ttgx_s {
		__BITFIELD_FIELD(uint64_t dbe:1,
		__BITFIELD_FIELD(uint64_t sbe:1,
		__BITFIELD_FIELD(uint64_t noway:1,
		__BITFIELD_FIELD(uint64_t reserved_56_60:5,
		__BITFIELD_FIELD(uint64_t syn:6,
		__BITFIELD_FIELD(uint64_t reserved_22_49:28,
		__BITFIELD_FIELD(uint64_t wayidx:15,
		__BITFIELD_FIELD(uint64_t reserved_2_6:5,
		__BITFIELD_FIELD(uint64_t type:2,
		;)))))))))
	} s;
};

union cvmx_l2c_cfg {
	uint64_t u64;
	struct cvmx_l2c_cfg_s {
		__BITFIELD_FIELD(uint64_t reserved_20_63:44,
		__BITFIELD_FIELD(uint64_t bstrun:1,
		__BITFIELD_FIELD(uint64_t lbist:1,
		__BITFIELD_FIELD(uint64_t xor_bank:1,
		__BITFIELD_FIELD(uint64_t dpres1:1,
		__BITFIELD_FIELD(uint64_t dpres0:1,
		__BITFIELD_FIELD(uint64_t dfill_dis:1,
		__BITFIELD_FIELD(uint64_t fpexp:4,
		__BITFIELD_FIELD(uint64_t fpempty:1,
		__BITFIELD_FIELD(uint64_t fpen:1,
		__BITFIELD_FIELD(uint64_t idxalias:1,
		__BITFIELD_FIELD(uint64_t mwf_crd:4,
		__BITFIELD_FIELD(uint64_t rsp_arb_mode:1,
		__BITFIELD_FIELD(uint64_t rfb_arb_mode:1,
		__BITFIELD_FIELD(uint64_t lrf_arb_mode:1,
		;)))))))))))))))
	} s;
};

union cvmx_l2c_ctl {
	uint64_t u64;
	struct cvmx_l2c_ctl_s {
		__BITFIELD_FIELD(uint64_t reserved_30_63:34,
		__BITFIELD_FIELD(uint64_t sepcmt:1,
		__BITFIELD_FIELD(uint64_t rdf_fast:1,
		__BITFIELD_FIELD(uint64_t disstgl2i:1,
		__BITFIELD_FIELD(uint64_t l2dfsbe:1,
		__BITFIELD_FIELD(uint64_t l2dfdbe:1,
		__BITFIELD_FIELD(uint64_t discclk:1,
		__BITFIELD_FIELD(uint64_t maxvab:4,
		__BITFIELD_FIELD(uint64_t maxlfb:4,
		__BITFIELD_FIELD(uint64_t rsp_arb_mode:1,
		__BITFIELD_FIELD(uint64_t xmc_arb_mode:1,
		__BITFIELD_FIELD(uint64_t ef_ena:1,
		__BITFIELD_FIELD(uint64_t ef_cnt:7,
		__BITFIELD_FIELD(uint64_t vab_thresh:4,
		__BITFIELD_FIELD(uint64_t disecc:1,
		__BITFIELD_FIELD(uint64_t disidxalias:1,
		;))))))))))))))))
	} s;
};

union cvmx_l2c_dbg {
	uint64_t u64;
	struct cvmx_l2c_dbg_s {
		__BITFIELD_FIELD(uint64_t reserved_15_63:49,
		__BITFIELD_FIELD(uint64_t lfb_enum:4,
		__BITFIELD_FIELD(uint64_t lfb_dmp:1,
		__BITFIELD_FIELD(uint64_t ppnum:4,
		__BITFIELD_FIELD(uint64_t set:3,
		__BITFIELD_FIELD(uint64_t finv:1,
		__BITFIELD_FIELD(uint64_t l2d:1,
		__BITFIELD_FIELD(uint64_t l2t:1,
		;))))))))
	} s;
};

union cvmx_l2c_pfctl {
	uint64_t u64;
	struct cvmx_l2c_pfctl_s {
		__BITFIELD_FIELD(uint64_t reserved_36_63:28,
		__BITFIELD_FIELD(uint64_t cnt3rdclr:1,
		__BITFIELD_FIELD(uint64_t cnt2rdclr:1,
		__BITFIELD_FIELD(uint64_t cnt1rdclr:1,
		__BITFIELD_FIELD(uint64_t cnt0rdclr:1,
		__BITFIELD_FIELD(uint64_t cnt3ena:1,
		__BITFIELD_FIELD(uint64_t cnt3clr:1,
		__BITFIELD_FIELD(uint64_t cnt3sel:6,
		__BITFIELD_FIELD(uint64_t cnt2ena:1,
		__BITFIELD_FIELD(uint64_t cnt2clr:1,
		__BITFIELD_FIELD(uint64_t cnt2sel:6,
		__BITFIELD_FIELD(uint64_t cnt1ena:1,
		__BITFIELD_FIELD(uint64_t cnt1clr:1,
		__BITFIELD_FIELD(uint64_t cnt1sel:6,
		__BITFIELD_FIELD(uint64_t cnt0ena:1,
		__BITFIELD_FIELD(uint64_t cnt0clr:1,
		__BITFIELD_FIELD(uint64_t cnt0sel:6,
		;)))))))))))))))))
	} s;
};

union cvmx_l2c_tadx_prf {
	uint64_t u64;
	struct cvmx_l2c_tadx_prf_s {
		__BITFIELD_FIELD(uint64_t reserved_32_63:32,
		__BITFIELD_FIELD(uint64_t cnt3sel:8,
		__BITFIELD_FIELD(uint64_t cnt2sel:8,
		__BITFIELD_FIELD(uint64_t cnt1sel:8,
		__BITFIELD_FIELD(uint64_t cnt0sel:8,
		;)))))
	} s;
};

union cvmx_l2c_tadx_tag {
	uint64_t u64;
	struct cvmx_l2c_tadx_tag_s {
		__BITFIELD_FIELD(uint64_t reserved_46_63:18,
		__BITFIELD_FIELD(uint64_t ecc:6,
		__BITFIELD_FIELD(uint64_t reserved_36_39:4,
		__BITFIELD_FIELD(uint64_t tag:19,
		__BITFIELD_FIELD(uint64_t reserved_4_16:13,
		__BITFIELD_FIELD(uint64_t use:1,
		__BITFIELD_FIELD(uint64_t valid:1,
		__BITFIELD_FIELD(uint64_t dirty:1,
		__BITFIELD_FIELD(uint64_t lock:1,
		;)))))))))
	} s;
};

union cvmx_l2c_lckbase {
	uint64_t u64;
	struct cvmx_l2c_lckbase_s {
		__BITFIELD_FIELD(uint64_t reserved_31_63:33,
		__BITFIELD_FIELD(uint64_t lck_base:27,
		__BITFIELD_FIELD(uint64_t reserved_1_3:3,
		__BITFIELD_FIELD(uint64_t lck_ena:1,
		;))))
	} s;
};

union cvmx_l2c_lckoff {
	uint64_t u64;
	struct cvmx_l2c_lckoff_s {
		__BITFIELD_FIELD(uint64_t reserved_10_63:54,
		__BITFIELD_FIELD(uint64_t lck_offset:10,
		;))
	} s;
};

#endif

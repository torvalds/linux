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

#ifndef __CVMX_RNM_DEFS_H__
#define __CVMX_RNM_DEFS_H__

#define CVMX_RNM_BIST_STATUS (CVMX_ADD_IO_SEG(0x0001180040000008ull))
#define CVMX_RNM_CTL_STATUS (CVMX_ADD_IO_SEG(0x0001180040000000ull))
#define CVMX_RNM_EER_DBG (CVMX_ADD_IO_SEG(0x0001180040000018ull))
#define CVMX_RNM_EER_KEY (CVMX_ADD_IO_SEG(0x0001180040000010ull))
#define CVMX_RNM_SERIAL_NUM (CVMX_ADD_IO_SEG(0x0001180040000020ull))

union cvmx_rnm_bist_status {
	uint64_t u64;
	struct cvmx_rnm_bist_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t rrc:1;
		uint64_t mem:1;
#else
		uint64_t mem:1;
		uint64_t rrc:1;
		uint64_t reserved_2_63:62;
#endif
	} s;
	struct cvmx_rnm_bist_status_s cn30xx;
	struct cvmx_rnm_bist_status_s cn31xx;
	struct cvmx_rnm_bist_status_s cn38xx;
	struct cvmx_rnm_bist_status_s cn38xxp2;
	struct cvmx_rnm_bist_status_s cn50xx;
	struct cvmx_rnm_bist_status_s cn52xx;
	struct cvmx_rnm_bist_status_s cn52xxp1;
	struct cvmx_rnm_bist_status_s cn56xx;
	struct cvmx_rnm_bist_status_s cn56xxp1;
	struct cvmx_rnm_bist_status_s cn58xx;
	struct cvmx_rnm_bist_status_s cn58xxp1;
	struct cvmx_rnm_bist_status_s cn61xx;
	struct cvmx_rnm_bist_status_s cn63xx;
	struct cvmx_rnm_bist_status_s cn63xxp1;
	struct cvmx_rnm_bist_status_s cn66xx;
	struct cvmx_rnm_bist_status_s cn68xx;
	struct cvmx_rnm_bist_status_s cn68xxp1;
	struct cvmx_rnm_bist_status_s cnf71xx;
};

union cvmx_rnm_ctl_status {
	uint64_t u64;
	struct cvmx_rnm_ctl_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t dis_mak:1;
		uint64_t eer_lck:1;
		uint64_t eer_val:1;
		uint64_t ent_sel:4;
		uint64_t exp_ent:1;
		uint64_t rng_rst:1;
		uint64_t rnm_rst:1;
		uint64_t rng_en:1;
		uint64_t ent_en:1;
#else
		uint64_t ent_en:1;
		uint64_t rng_en:1;
		uint64_t rnm_rst:1;
		uint64_t rng_rst:1;
		uint64_t exp_ent:1;
		uint64_t ent_sel:4;
		uint64_t eer_val:1;
		uint64_t eer_lck:1;
		uint64_t dis_mak:1;
		uint64_t reserved_12_63:52;
#endif
	} s;
	struct cvmx_rnm_ctl_status_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t rng_rst:1;
		uint64_t rnm_rst:1;
		uint64_t rng_en:1;
		uint64_t ent_en:1;
#else
		uint64_t ent_en:1;
		uint64_t rng_en:1;
		uint64_t rnm_rst:1;
		uint64_t rng_rst:1;
		uint64_t reserved_4_63:60;
#endif
	} cn30xx;
	struct cvmx_rnm_ctl_status_cn30xx cn31xx;
	struct cvmx_rnm_ctl_status_cn30xx cn38xx;
	struct cvmx_rnm_ctl_status_cn30xx cn38xxp2;
	struct cvmx_rnm_ctl_status_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t ent_sel:4;
		uint64_t exp_ent:1;
		uint64_t rng_rst:1;
		uint64_t rnm_rst:1;
		uint64_t rng_en:1;
		uint64_t ent_en:1;
#else
		uint64_t ent_en:1;
		uint64_t rng_en:1;
		uint64_t rnm_rst:1;
		uint64_t rng_rst:1;
		uint64_t exp_ent:1;
		uint64_t ent_sel:4;
		uint64_t reserved_9_63:55;
#endif
	} cn50xx;
	struct cvmx_rnm_ctl_status_cn50xx cn52xx;
	struct cvmx_rnm_ctl_status_cn50xx cn52xxp1;
	struct cvmx_rnm_ctl_status_cn50xx cn56xx;
	struct cvmx_rnm_ctl_status_cn50xx cn56xxp1;
	struct cvmx_rnm_ctl_status_cn50xx cn58xx;
	struct cvmx_rnm_ctl_status_cn50xx cn58xxp1;
	struct cvmx_rnm_ctl_status_s cn61xx;
	struct cvmx_rnm_ctl_status_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_11_63:53;
		uint64_t eer_lck:1;
		uint64_t eer_val:1;
		uint64_t ent_sel:4;
		uint64_t exp_ent:1;
		uint64_t rng_rst:1;
		uint64_t rnm_rst:1;
		uint64_t rng_en:1;
		uint64_t ent_en:1;
#else
		uint64_t ent_en:1;
		uint64_t rng_en:1;
		uint64_t rnm_rst:1;
		uint64_t rng_rst:1;
		uint64_t exp_ent:1;
		uint64_t ent_sel:4;
		uint64_t eer_val:1;
		uint64_t eer_lck:1;
		uint64_t reserved_11_63:53;
#endif
	} cn63xx;
	struct cvmx_rnm_ctl_status_cn63xx cn63xxp1;
	struct cvmx_rnm_ctl_status_s cn66xx;
	struct cvmx_rnm_ctl_status_cn63xx cn68xx;
	struct cvmx_rnm_ctl_status_cn63xx cn68xxp1;
	struct cvmx_rnm_ctl_status_s cnf71xx;
};

union cvmx_rnm_eer_dbg {
	uint64_t u64;
	struct cvmx_rnm_eer_dbg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t dat:64;
#else
		uint64_t dat:64;
#endif
	} s;
	struct cvmx_rnm_eer_dbg_s cn61xx;
	struct cvmx_rnm_eer_dbg_s cn63xx;
	struct cvmx_rnm_eer_dbg_s cn63xxp1;
	struct cvmx_rnm_eer_dbg_s cn66xx;
	struct cvmx_rnm_eer_dbg_s cn68xx;
	struct cvmx_rnm_eer_dbg_s cn68xxp1;
	struct cvmx_rnm_eer_dbg_s cnf71xx;
};

union cvmx_rnm_eer_key {
	uint64_t u64;
	struct cvmx_rnm_eer_key_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t key:64;
#else
		uint64_t key:64;
#endif
	} s;
	struct cvmx_rnm_eer_key_s cn61xx;
	struct cvmx_rnm_eer_key_s cn63xx;
	struct cvmx_rnm_eer_key_s cn63xxp1;
	struct cvmx_rnm_eer_key_s cn66xx;
	struct cvmx_rnm_eer_key_s cn68xx;
	struct cvmx_rnm_eer_key_s cn68xxp1;
	struct cvmx_rnm_eer_key_s cnf71xx;
};

union cvmx_rnm_serial_num {
	uint64_t u64;
	struct cvmx_rnm_serial_num_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t dat:64;
#else
		uint64_t dat:64;
#endif
	} s;
	struct cvmx_rnm_serial_num_s cn61xx;
	struct cvmx_rnm_serial_num_s cn63xx;
	struct cvmx_rnm_serial_num_s cn66xx;
	struct cvmx_rnm_serial_num_s cn68xx;
	struct cvmx_rnm_serial_num_s cn68xxp1;
	struct cvmx_rnm_serial_num_s cnf71xx;
};

#endif

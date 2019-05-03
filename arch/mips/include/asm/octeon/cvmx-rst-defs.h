/***********************license start***************
 * Author: Cavium Inc.
 *
 * Contact: support@cavium.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2014 Cavium Inc.
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
 * Contact Cavium Inc. for more information
 ***********************license end**************************************/

#ifndef __CVMX_RST_DEFS_H__
#define __CVMX_RST_DEFS_H__

#define CVMX_RST_BOOT (CVMX_ADD_IO_SEG(0x0001180006001600ull))
#define CVMX_RST_CFG (CVMX_ADD_IO_SEG(0x0001180006001610ull))
#define CVMX_RST_CKILL (CVMX_ADD_IO_SEG(0x0001180006001638ull))
#define CVMX_RST_CTLX(offset) (CVMX_ADD_IO_SEG(0x0001180006001640ull) + ((offset) & 3) * 8)
#define CVMX_RST_DELAY (CVMX_ADD_IO_SEG(0x0001180006001608ull))
#define CVMX_RST_ECO (CVMX_ADD_IO_SEG(0x00011800060017B8ull))
#define CVMX_RST_INT (CVMX_ADD_IO_SEG(0x0001180006001628ull))
#define CVMX_RST_OCX (CVMX_ADD_IO_SEG(0x0001180006001618ull))
#define CVMX_RST_POWER_DBG (CVMX_ADD_IO_SEG(0x0001180006001708ull))
#define CVMX_RST_PP_POWER (CVMX_ADD_IO_SEG(0x0001180006001700ull))
#define CVMX_RST_SOFT_PRSTX(offset) (CVMX_ADD_IO_SEG(0x00011800060016C0ull) + ((offset) & 3) * 8)
#define CVMX_RST_SOFT_RST (CVMX_ADD_IO_SEG(0x0001180006001680ull))

union cvmx_rst_boot {
	uint64_t u64;
	struct cvmx_rst_boot_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t chipkill:1;
		uint64_t jtcsrdis:1;
		uint64_t ejtagdis:1;
		uint64_t romen:1;
		uint64_t ckill_ppdis:1;
		uint64_t jt_tstmode:1;
		uint64_t vrm_err:1;
		uint64_t reserved_37_56:20;
		uint64_t c_mul:7;
		uint64_t pnr_mul:6;
		uint64_t reserved_21_23:3;
		uint64_t lboot_oci:3;
		uint64_t lboot_ext:6;
		uint64_t lboot:10;
		uint64_t rboot:1;
		uint64_t rboot_pin:1;
#else
		uint64_t rboot_pin:1;
		uint64_t rboot:1;
		uint64_t lboot:10;
		uint64_t lboot_ext:6;
		uint64_t lboot_oci:3;
		uint64_t reserved_21_23:3;
		uint64_t pnr_mul:6;
		uint64_t c_mul:7;
		uint64_t reserved_37_56:20;
		uint64_t vrm_err:1;
		uint64_t jt_tstmode:1;
		uint64_t ckill_ppdis:1;
		uint64_t romen:1;
		uint64_t ejtagdis:1;
		uint64_t jtcsrdis:1;
		uint64_t chipkill:1;
#endif
	} s;
};

union cvmx_rst_cfg {
	uint64_t u64;
	struct cvmx_rst_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bist_delay:58;
		uint64_t reserved_3_5:3;
		uint64_t cntl_clr_bist:1;
		uint64_t warm_clr_bist:1;
		uint64_t soft_clr_bist:1;
#else
		uint64_t soft_clr_bist:1;
		uint64_t warm_clr_bist:1;
		uint64_t cntl_clr_bist:1;
		uint64_t reserved_3_5:3;
		uint64_t bist_delay:58;
#endif
	} s;
};

union cvmx_rst_ckill {
	uint64_t u64;
	struct cvmx_rst_ckill_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_47_63:17;
		uint64_t timer:47;
#else
		uint64_t timer:47;
		uint64_t reserved_47_63:17;
#endif
	} s;
};

union cvmx_rst_ctlx {
	uint64_t u64;
	struct cvmx_rst_ctlx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t prst_link:1;
		uint64_t rst_done:1;
		uint64_t rst_link:1;
		uint64_t host_mode:1;
		uint64_t reserved_4_5:2;
		uint64_t rst_drv:1;
		uint64_t rst_rcv:1;
		uint64_t rst_chip:1;
		uint64_t rst_val:1;
#else
		uint64_t rst_val:1;
		uint64_t rst_chip:1;
		uint64_t rst_rcv:1;
		uint64_t rst_drv:1;
		uint64_t reserved_4_5:2;
		uint64_t host_mode:1;
		uint64_t rst_link:1;
		uint64_t rst_done:1;
		uint64_t prst_link:1;
		uint64_t reserved_10_63:54;
#endif
	} s;
};

union cvmx_rst_delay {
	uint64_t u64;
	struct cvmx_rst_delay_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t warm_rst_dly:16;
		uint64_t soft_rst_dly:16;
#else
		uint64_t soft_rst_dly:16;
		uint64_t warm_rst_dly:16;
		uint64_t reserved_32_63:32;
#endif
	} s;
};

union cvmx_rst_eco {
	uint64_t u64;
	struct cvmx_rst_eco_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t eco_rw:32;
#else
		uint64_t eco_rw:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
};

union cvmx_rst_int {
	uint64_t u64;
	struct cvmx_rst_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t perst:4;
		uint64_t reserved_4_7:4;
		uint64_t rst_link:4;
#else
		uint64_t rst_link:4;
		uint64_t reserved_4_7:4;
		uint64_t perst:4;
		uint64_t reserved_12_63:52;
#endif
	} s;
	struct cvmx_rst_int_cn70xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_11_63:53;
		uint64_t perst:3;
		uint64_t reserved_3_7:5;
		uint64_t rst_link:3;
#else
		uint64_t rst_link:3;
		uint64_t reserved_3_7:5;
		uint64_t perst:3;
		uint64_t reserved_11_63:53;
#endif
	} cn70xx;
};

union cvmx_rst_ocx {
	uint64_t u64;
	struct cvmx_rst_ocx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t rst_link:3;
#else
		uint64_t rst_link:3;
		uint64_t reserved_3_63:61;
#endif
	} s;
};

union cvmx_rst_power_dbg {
	uint64_t u64;
	struct cvmx_rst_power_dbg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t str:3;
#else
		uint64_t str:3;
		uint64_t reserved_3_63:61;
#endif
	} s;
};

union cvmx_rst_pp_power {
	uint64_t u64;
	struct cvmx_rst_pp_power_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t gate:48;
#else
		uint64_t gate:48;
		uint64_t reserved_48_63:16;
#endif
	} s;
	struct cvmx_rst_pp_power_cn70xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t gate:4;
#else
		uint64_t gate:4;
		uint64_t reserved_4_63:60;
#endif
	} cn70xx;
};

union cvmx_rst_soft_prstx {
	uint64_t u64;
	struct cvmx_rst_soft_prstx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t soft_prst:1;
#else
		uint64_t soft_prst:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
};

union cvmx_rst_soft_rst {
	uint64_t u64;
	struct cvmx_rst_soft_rst_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t soft_rst:1;
#else
		uint64_t soft_rst:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
};

#endif

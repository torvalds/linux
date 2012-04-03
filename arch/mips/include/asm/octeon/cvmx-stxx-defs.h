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

#ifndef __CVMX_STXX_DEFS_H__
#define __CVMX_STXX_DEFS_H__

#define CVMX_STXX_ARB_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180090000608ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_STXX_BCKPRS_CNT(block_id) (CVMX_ADD_IO_SEG(0x0001180090000688ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_STXX_COM_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180090000600ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_STXX_DIP_CNT(block_id) (CVMX_ADD_IO_SEG(0x0001180090000690ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_STXX_IGN_CAL(block_id) (CVMX_ADD_IO_SEG(0x0001180090000610ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_STXX_INT_MSK(block_id) (CVMX_ADD_IO_SEG(0x00011800900006A0ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_STXX_INT_REG(block_id) (CVMX_ADD_IO_SEG(0x0001180090000698ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_STXX_INT_SYNC(block_id) (CVMX_ADD_IO_SEG(0x00011800900006A8ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_STXX_MIN_BST(block_id) (CVMX_ADD_IO_SEG(0x0001180090000618ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_STXX_SPI4_CALX(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180090000400ull) + (((offset) & 31) + ((block_id) & 1) * 0x1000000ull) * 8)
#define CVMX_STXX_SPI4_DAT(block_id) (CVMX_ADD_IO_SEG(0x0001180090000628ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_STXX_SPI4_STAT(block_id) (CVMX_ADD_IO_SEG(0x0001180090000630ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_STXX_STAT_BYTES_HI(block_id) (CVMX_ADD_IO_SEG(0x0001180090000648ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_STXX_STAT_BYTES_LO(block_id) (CVMX_ADD_IO_SEG(0x0001180090000680ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_STXX_STAT_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180090000638ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_STXX_STAT_PKT_XMT(block_id) (CVMX_ADD_IO_SEG(0x0001180090000640ull) + ((block_id) & 1) * 0x8000000ull)

union cvmx_stxx_arb_ctl {
	uint64_t u64;
	struct cvmx_stxx_arb_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t mintrn:1;
		uint64_t reserved_4_4:1;
		uint64_t igntpa:1;
		uint64_t reserved_0_2:3;
#else
		uint64_t reserved_0_2:3;
		uint64_t igntpa:1;
		uint64_t reserved_4_4:1;
		uint64_t mintrn:1;
		uint64_t reserved_6_63:58;
#endif
	} s;
	struct cvmx_stxx_arb_ctl_s cn38xx;
	struct cvmx_stxx_arb_ctl_s cn38xxp2;
	struct cvmx_stxx_arb_ctl_s cn58xx;
	struct cvmx_stxx_arb_ctl_s cn58xxp1;
};

union cvmx_stxx_bckprs_cnt {
	uint64_t u64;
	struct cvmx_stxx_bckprs_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t cnt:32;
#else
		uint64_t cnt:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_stxx_bckprs_cnt_s cn38xx;
	struct cvmx_stxx_bckprs_cnt_s cn38xxp2;
	struct cvmx_stxx_bckprs_cnt_s cn58xx;
	struct cvmx_stxx_bckprs_cnt_s cn58xxp1;
};

union cvmx_stxx_com_ctl {
	uint64_t u64;
	struct cvmx_stxx_com_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t st_en:1;
		uint64_t reserved_1_2:2;
		uint64_t inf_en:1;
#else
		uint64_t inf_en:1;
		uint64_t reserved_1_2:2;
		uint64_t st_en:1;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_stxx_com_ctl_s cn38xx;
	struct cvmx_stxx_com_ctl_s cn38xxp2;
	struct cvmx_stxx_com_ctl_s cn58xx;
	struct cvmx_stxx_com_ctl_s cn58xxp1;
};

union cvmx_stxx_dip_cnt {
	uint64_t u64;
	struct cvmx_stxx_dip_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t frmmax:4;
		uint64_t dipmax:4;
#else
		uint64_t dipmax:4;
		uint64_t frmmax:4;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_stxx_dip_cnt_s cn38xx;
	struct cvmx_stxx_dip_cnt_s cn38xxp2;
	struct cvmx_stxx_dip_cnt_s cn58xx;
	struct cvmx_stxx_dip_cnt_s cn58xxp1;
};

union cvmx_stxx_ign_cal {
	uint64_t u64;
	struct cvmx_stxx_ign_cal_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t igntpa:16;
#else
		uint64_t igntpa:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_stxx_ign_cal_s cn38xx;
	struct cvmx_stxx_ign_cal_s cn38xxp2;
	struct cvmx_stxx_ign_cal_s cn58xx;
	struct cvmx_stxx_ign_cal_s cn58xxp1;
};

union cvmx_stxx_int_msk {
	uint64_t u64;
	struct cvmx_stxx_int_msk_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t frmerr:1;
		uint64_t unxfrm:1;
		uint64_t nosync:1;
		uint64_t diperr:1;
		uint64_t datovr:1;
		uint64_t ovrbst:1;
		uint64_t calpar1:1;
		uint64_t calpar0:1;
#else
		uint64_t calpar0:1;
		uint64_t calpar1:1;
		uint64_t ovrbst:1;
		uint64_t datovr:1;
		uint64_t diperr:1;
		uint64_t nosync:1;
		uint64_t unxfrm:1;
		uint64_t frmerr:1;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_stxx_int_msk_s cn38xx;
	struct cvmx_stxx_int_msk_s cn38xxp2;
	struct cvmx_stxx_int_msk_s cn58xx;
	struct cvmx_stxx_int_msk_s cn58xxp1;
};

union cvmx_stxx_int_reg {
	uint64_t u64;
	struct cvmx_stxx_int_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t syncerr:1;
		uint64_t frmerr:1;
		uint64_t unxfrm:1;
		uint64_t nosync:1;
		uint64_t diperr:1;
		uint64_t datovr:1;
		uint64_t ovrbst:1;
		uint64_t calpar1:1;
		uint64_t calpar0:1;
#else
		uint64_t calpar0:1;
		uint64_t calpar1:1;
		uint64_t ovrbst:1;
		uint64_t datovr:1;
		uint64_t diperr:1;
		uint64_t nosync:1;
		uint64_t unxfrm:1;
		uint64_t frmerr:1;
		uint64_t syncerr:1;
		uint64_t reserved_9_63:55;
#endif
	} s;
	struct cvmx_stxx_int_reg_s cn38xx;
	struct cvmx_stxx_int_reg_s cn38xxp2;
	struct cvmx_stxx_int_reg_s cn58xx;
	struct cvmx_stxx_int_reg_s cn58xxp1;
};

union cvmx_stxx_int_sync {
	uint64_t u64;
	struct cvmx_stxx_int_sync_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t frmerr:1;
		uint64_t unxfrm:1;
		uint64_t nosync:1;
		uint64_t diperr:1;
		uint64_t datovr:1;
		uint64_t ovrbst:1;
		uint64_t calpar1:1;
		uint64_t calpar0:1;
#else
		uint64_t calpar0:1;
		uint64_t calpar1:1;
		uint64_t ovrbst:1;
		uint64_t datovr:1;
		uint64_t diperr:1;
		uint64_t nosync:1;
		uint64_t unxfrm:1;
		uint64_t frmerr:1;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_stxx_int_sync_s cn38xx;
	struct cvmx_stxx_int_sync_s cn38xxp2;
	struct cvmx_stxx_int_sync_s cn58xx;
	struct cvmx_stxx_int_sync_s cn58xxp1;
};

union cvmx_stxx_min_bst {
	uint64_t u64;
	struct cvmx_stxx_min_bst_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t minb:9;
#else
		uint64_t minb:9;
		uint64_t reserved_9_63:55;
#endif
	} s;
	struct cvmx_stxx_min_bst_s cn38xx;
	struct cvmx_stxx_min_bst_s cn38xxp2;
	struct cvmx_stxx_min_bst_s cn58xx;
	struct cvmx_stxx_min_bst_s cn58xxp1;
};

union cvmx_stxx_spi4_calx {
	uint64_t u64;
	struct cvmx_stxx_spi4_calx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_17_63:47;
		uint64_t oddpar:1;
		uint64_t prt3:4;
		uint64_t prt2:4;
		uint64_t prt1:4;
		uint64_t prt0:4;
#else
		uint64_t prt0:4;
		uint64_t prt1:4;
		uint64_t prt2:4;
		uint64_t prt3:4;
		uint64_t oddpar:1;
		uint64_t reserved_17_63:47;
#endif
	} s;
	struct cvmx_stxx_spi4_calx_s cn38xx;
	struct cvmx_stxx_spi4_calx_s cn38xxp2;
	struct cvmx_stxx_spi4_calx_s cn58xx;
	struct cvmx_stxx_spi4_calx_s cn58xxp1;
};

union cvmx_stxx_spi4_dat {
	uint64_t u64;
	struct cvmx_stxx_spi4_dat_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t alpha:16;
		uint64_t max_t:16;
#else
		uint64_t max_t:16;
		uint64_t alpha:16;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_stxx_spi4_dat_s cn38xx;
	struct cvmx_stxx_spi4_dat_s cn38xxp2;
	struct cvmx_stxx_spi4_dat_s cn58xx;
	struct cvmx_stxx_spi4_dat_s cn58xxp1;
};

union cvmx_stxx_spi4_stat {
	uint64_t u64;
	struct cvmx_stxx_spi4_stat_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t m:8;
		uint64_t reserved_7_7:1;
		uint64_t len:7;
#else
		uint64_t len:7;
		uint64_t reserved_7_7:1;
		uint64_t m:8;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_stxx_spi4_stat_s cn38xx;
	struct cvmx_stxx_spi4_stat_s cn38xxp2;
	struct cvmx_stxx_spi4_stat_s cn58xx;
	struct cvmx_stxx_spi4_stat_s cn58xxp1;
};

union cvmx_stxx_stat_bytes_hi {
	uint64_t u64;
	struct cvmx_stxx_stat_bytes_hi_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t cnt:32;
#else
		uint64_t cnt:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_stxx_stat_bytes_hi_s cn38xx;
	struct cvmx_stxx_stat_bytes_hi_s cn38xxp2;
	struct cvmx_stxx_stat_bytes_hi_s cn58xx;
	struct cvmx_stxx_stat_bytes_hi_s cn58xxp1;
};

union cvmx_stxx_stat_bytes_lo {
	uint64_t u64;
	struct cvmx_stxx_stat_bytes_lo_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t cnt:32;
#else
		uint64_t cnt:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_stxx_stat_bytes_lo_s cn38xx;
	struct cvmx_stxx_stat_bytes_lo_s cn38xxp2;
	struct cvmx_stxx_stat_bytes_lo_s cn58xx;
	struct cvmx_stxx_stat_bytes_lo_s cn58xxp1;
};

union cvmx_stxx_stat_ctl {
	uint64_t u64;
	struct cvmx_stxx_stat_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_5_63:59;
		uint64_t clr:1;
		uint64_t bckprs:4;
#else
		uint64_t bckprs:4;
		uint64_t clr:1;
		uint64_t reserved_5_63:59;
#endif
	} s;
	struct cvmx_stxx_stat_ctl_s cn38xx;
	struct cvmx_stxx_stat_ctl_s cn38xxp2;
	struct cvmx_stxx_stat_ctl_s cn58xx;
	struct cvmx_stxx_stat_ctl_s cn58xxp1;
};

union cvmx_stxx_stat_pkt_xmt {
	uint64_t u64;
	struct cvmx_stxx_stat_pkt_xmt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t cnt:32;
#else
		uint64_t cnt:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_stxx_stat_pkt_xmt_s cn38xx;
	struct cvmx_stxx_stat_pkt_xmt_s cn38xxp2;
	struct cvmx_stxx_stat_pkt_xmt_s cn58xx;
	struct cvmx_stxx_stat_pkt_xmt_s cn58xxp1;
};

#endif

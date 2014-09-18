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

#ifndef __CVMX_SPXX_DEFS_H__
#define __CVMX_SPXX_DEFS_H__

#define CVMX_SPXX_BCKPRS_CNT(block_id) (CVMX_ADD_IO_SEG(0x0001180090000340ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_SPXX_BIST_STAT(block_id) (CVMX_ADD_IO_SEG(0x00011800900007F8ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_SPXX_CLK_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180090000348ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_SPXX_CLK_STAT(block_id) (CVMX_ADD_IO_SEG(0x0001180090000350ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_SPXX_DBG_DESKEW_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180090000368ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_SPXX_DBG_DESKEW_STATE(block_id) (CVMX_ADD_IO_SEG(0x0001180090000370ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_SPXX_DRV_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180090000358ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_SPXX_ERR_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180090000320ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_SPXX_INT_DAT(block_id) (CVMX_ADD_IO_SEG(0x0001180090000318ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_SPXX_INT_MSK(block_id) (CVMX_ADD_IO_SEG(0x0001180090000308ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_SPXX_INT_REG(block_id) (CVMX_ADD_IO_SEG(0x0001180090000300ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_SPXX_INT_SYNC(block_id) (CVMX_ADD_IO_SEG(0x0001180090000310ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_SPXX_TPA_ACC(block_id) (CVMX_ADD_IO_SEG(0x0001180090000338ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_SPXX_TPA_MAX(block_id) (CVMX_ADD_IO_SEG(0x0001180090000330ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_SPXX_TPA_SEL(block_id) (CVMX_ADD_IO_SEG(0x0001180090000328ull) + ((block_id) & 1) * 0x8000000ull)
#define CVMX_SPXX_TRN4_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180090000360ull) + ((block_id) & 1) * 0x8000000ull)

union cvmx_spxx_bckprs_cnt {
	uint64_t u64;
	struct cvmx_spxx_bckprs_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t cnt:32;
#else
		uint64_t cnt:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_spxx_bckprs_cnt_s cn38xx;
	struct cvmx_spxx_bckprs_cnt_s cn38xxp2;
	struct cvmx_spxx_bckprs_cnt_s cn58xx;
	struct cvmx_spxx_bckprs_cnt_s cn58xxp1;
};

union cvmx_spxx_bist_stat {
	uint64_t u64;
	struct cvmx_spxx_bist_stat_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t stat2:1;
		uint64_t stat1:1;
		uint64_t stat0:1;
#else
		uint64_t stat0:1;
		uint64_t stat1:1;
		uint64_t stat2:1;
		uint64_t reserved_3_63:61;
#endif
	} s;
	struct cvmx_spxx_bist_stat_s cn38xx;
	struct cvmx_spxx_bist_stat_s cn38xxp2;
	struct cvmx_spxx_bist_stat_s cn58xx;
	struct cvmx_spxx_bist_stat_s cn58xxp1;
};

union cvmx_spxx_clk_ctl {
	uint64_t u64;
	struct cvmx_spxx_clk_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_17_63:47;
		uint64_t seetrn:1;
		uint64_t reserved_12_15:4;
		uint64_t clkdly:5;
		uint64_t runbist:1;
		uint64_t statdrv:1;
		uint64_t statrcv:1;
		uint64_t sndtrn:1;
		uint64_t drptrn:1;
		uint64_t rcvtrn:1;
		uint64_t srxdlck:1;
#else
		uint64_t srxdlck:1;
		uint64_t rcvtrn:1;
		uint64_t drptrn:1;
		uint64_t sndtrn:1;
		uint64_t statrcv:1;
		uint64_t statdrv:1;
		uint64_t runbist:1;
		uint64_t clkdly:5;
		uint64_t reserved_12_15:4;
		uint64_t seetrn:1;
		uint64_t reserved_17_63:47;
#endif
	} s;
	struct cvmx_spxx_clk_ctl_s cn38xx;
	struct cvmx_spxx_clk_ctl_s cn38xxp2;
	struct cvmx_spxx_clk_ctl_s cn58xx;
	struct cvmx_spxx_clk_ctl_s cn58xxp1;
};

union cvmx_spxx_clk_stat {
	uint64_t u64;
	struct cvmx_spxx_clk_stat_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_11_63:53;
		uint64_t stxcal:1;
		uint64_t reserved_9_9:1;
		uint64_t srxtrn:1;
		uint64_t s4clk1:1;
		uint64_t s4clk0:1;
		uint64_t d4clk1:1;
		uint64_t d4clk0:1;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t d4clk0:1;
		uint64_t d4clk1:1;
		uint64_t s4clk0:1;
		uint64_t s4clk1:1;
		uint64_t srxtrn:1;
		uint64_t reserved_9_9:1;
		uint64_t stxcal:1;
		uint64_t reserved_11_63:53;
#endif
	} s;
	struct cvmx_spxx_clk_stat_s cn38xx;
	struct cvmx_spxx_clk_stat_s cn38xxp2;
	struct cvmx_spxx_clk_stat_s cn58xx;
	struct cvmx_spxx_clk_stat_s cn58xxp1;
};

union cvmx_spxx_dbg_deskew_ctl {
	uint64_t u64;
	struct cvmx_spxx_dbg_deskew_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_30_63:34;
		uint64_t fallnop:1;
		uint64_t fall8:1;
		uint64_t reserved_26_27:2;
		uint64_t sstep_go:1;
		uint64_t sstep:1;
		uint64_t reserved_22_23:2;
		uint64_t clrdly:1;
		uint64_t dec:1;
		uint64_t inc:1;
		uint64_t mux:1;
		uint64_t offset:5;
		uint64_t bitsel:5;
		uint64_t offdly:6;
		uint64_t dllfrc:1;
		uint64_t dlldis:1;
#else
		uint64_t dlldis:1;
		uint64_t dllfrc:1;
		uint64_t offdly:6;
		uint64_t bitsel:5;
		uint64_t offset:5;
		uint64_t mux:1;
		uint64_t inc:1;
		uint64_t dec:1;
		uint64_t clrdly:1;
		uint64_t reserved_22_23:2;
		uint64_t sstep:1;
		uint64_t sstep_go:1;
		uint64_t reserved_26_27:2;
		uint64_t fall8:1;
		uint64_t fallnop:1;
		uint64_t reserved_30_63:34;
#endif
	} s;
	struct cvmx_spxx_dbg_deskew_ctl_s cn38xx;
	struct cvmx_spxx_dbg_deskew_ctl_s cn38xxp2;
	struct cvmx_spxx_dbg_deskew_ctl_s cn58xx;
	struct cvmx_spxx_dbg_deskew_ctl_s cn58xxp1;
};

union cvmx_spxx_dbg_deskew_state {
	uint64_t u64;
	struct cvmx_spxx_dbg_deskew_state_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t testres:1;
		uint64_t unxterm:1;
		uint64_t muxsel:2;
		uint64_t offset:5;
#else
		uint64_t offset:5;
		uint64_t muxsel:2;
		uint64_t unxterm:1;
		uint64_t testres:1;
		uint64_t reserved_9_63:55;
#endif
	} s;
	struct cvmx_spxx_dbg_deskew_state_s cn38xx;
	struct cvmx_spxx_dbg_deskew_state_s cn38xxp2;
	struct cvmx_spxx_dbg_deskew_state_s cn58xx;
	struct cvmx_spxx_dbg_deskew_state_s cn58xxp1;
};

union cvmx_spxx_drv_ctl {
	uint64_t u64;
	struct cvmx_spxx_drv_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_0_63:64;
#else
		uint64_t reserved_0_63:64;
#endif
	} s;
	struct cvmx_spxx_drv_ctl_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t stx4ncmp:4;
		uint64_t stx4pcmp:4;
		uint64_t srx4cmp:8;
#else
		uint64_t srx4cmp:8;
		uint64_t stx4pcmp:4;
		uint64_t stx4ncmp:4;
		uint64_t reserved_16_63:48;
#endif
	} cn38xx;
	struct cvmx_spxx_drv_ctl_cn38xx cn38xxp2;
	struct cvmx_spxx_drv_ctl_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_24_63:40;
		uint64_t stx4ncmp:4;
		uint64_t stx4pcmp:4;
		uint64_t reserved_10_15:6;
		uint64_t srx4cmp:10;
#else
		uint64_t srx4cmp:10;
		uint64_t reserved_10_15:6;
		uint64_t stx4pcmp:4;
		uint64_t stx4ncmp:4;
		uint64_t reserved_24_63:40;
#endif
	} cn58xx;
	struct cvmx_spxx_drv_ctl_cn58xx cn58xxp1;
};

union cvmx_spxx_err_ctl {
	uint64_t u64;
	struct cvmx_spxx_err_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t prtnxa:1;
		uint64_t dipcls:1;
		uint64_t dippay:1;
		uint64_t reserved_4_5:2;
		uint64_t errcnt:4;
#else
		uint64_t errcnt:4;
		uint64_t reserved_4_5:2;
		uint64_t dippay:1;
		uint64_t dipcls:1;
		uint64_t prtnxa:1;
		uint64_t reserved_9_63:55;
#endif
	} s;
	struct cvmx_spxx_err_ctl_s cn38xx;
	struct cvmx_spxx_err_ctl_s cn38xxp2;
	struct cvmx_spxx_err_ctl_s cn58xx;
	struct cvmx_spxx_err_ctl_s cn58xxp1;
};

union cvmx_spxx_int_dat {
	uint64_t u64;
	struct cvmx_spxx_int_dat_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t mul:1;
		uint64_t reserved_14_30:17;
		uint64_t calbnk:2;
		uint64_t rsvop:4;
		uint64_t prt:8;
#else
		uint64_t prt:8;
		uint64_t rsvop:4;
		uint64_t calbnk:2;
		uint64_t reserved_14_30:17;
		uint64_t mul:1;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_spxx_int_dat_s cn38xx;
	struct cvmx_spxx_int_dat_s cn38xxp2;
	struct cvmx_spxx_int_dat_s cn58xx;
	struct cvmx_spxx_int_dat_s cn58xxp1;
};

union cvmx_spxx_int_msk {
	uint64_t u64;
	struct cvmx_spxx_int_msk_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t calerr:1;
		uint64_t syncerr:1;
		uint64_t diperr:1;
		uint64_t tpaovr:1;
		uint64_t rsverr:1;
		uint64_t drwnng:1;
		uint64_t clserr:1;
		uint64_t spiovr:1;
		uint64_t reserved_2_3:2;
		uint64_t abnorm:1;
		uint64_t prtnxa:1;
#else
		uint64_t prtnxa:1;
		uint64_t abnorm:1;
		uint64_t reserved_2_3:2;
		uint64_t spiovr:1;
		uint64_t clserr:1;
		uint64_t drwnng:1;
		uint64_t rsverr:1;
		uint64_t tpaovr:1;
		uint64_t diperr:1;
		uint64_t syncerr:1;
		uint64_t calerr:1;
		uint64_t reserved_12_63:52;
#endif
	} s;
	struct cvmx_spxx_int_msk_s cn38xx;
	struct cvmx_spxx_int_msk_s cn38xxp2;
	struct cvmx_spxx_int_msk_s cn58xx;
	struct cvmx_spxx_int_msk_s cn58xxp1;
};

union cvmx_spxx_int_reg {
	uint64_t u64;
	struct cvmx_spxx_int_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t spf:1;
		uint64_t reserved_12_30:19;
		uint64_t calerr:1;
		uint64_t syncerr:1;
		uint64_t diperr:1;
		uint64_t tpaovr:1;
		uint64_t rsverr:1;
		uint64_t drwnng:1;
		uint64_t clserr:1;
		uint64_t spiovr:1;
		uint64_t reserved_2_3:2;
		uint64_t abnorm:1;
		uint64_t prtnxa:1;
#else
		uint64_t prtnxa:1;
		uint64_t abnorm:1;
		uint64_t reserved_2_3:2;
		uint64_t spiovr:1;
		uint64_t clserr:1;
		uint64_t drwnng:1;
		uint64_t rsverr:1;
		uint64_t tpaovr:1;
		uint64_t diperr:1;
		uint64_t syncerr:1;
		uint64_t calerr:1;
		uint64_t reserved_12_30:19;
		uint64_t spf:1;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_spxx_int_reg_s cn38xx;
	struct cvmx_spxx_int_reg_s cn38xxp2;
	struct cvmx_spxx_int_reg_s cn58xx;
	struct cvmx_spxx_int_reg_s cn58xxp1;
};

union cvmx_spxx_int_sync {
	uint64_t u64;
	struct cvmx_spxx_int_sync_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t calerr:1;
		uint64_t syncerr:1;
		uint64_t diperr:1;
		uint64_t tpaovr:1;
		uint64_t rsverr:1;
		uint64_t drwnng:1;
		uint64_t clserr:1;
		uint64_t spiovr:1;
		uint64_t reserved_2_3:2;
		uint64_t abnorm:1;
		uint64_t prtnxa:1;
#else
		uint64_t prtnxa:1;
		uint64_t abnorm:1;
		uint64_t reserved_2_3:2;
		uint64_t spiovr:1;
		uint64_t clserr:1;
		uint64_t drwnng:1;
		uint64_t rsverr:1;
		uint64_t tpaovr:1;
		uint64_t diperr:1;
		uint64_t syncerr:1;
		uint64_t calerr:1;
		uint64_t reserved_12_63:52;
#endif
	} s;
	struct cvmx_spxx_int_sync_s cn38xx;
	struct cvmx_spxx_int_sync_s cn38xxp2;
	struct cvmx_spxx_int_sync_s cn58xx;
	struct cvmx_spxx_int_sync_s cn58xxp1;
};

union cvmx_spxx_tpa_acc {
	uint64_t u64;
	struct cvmx_spxx_tpa_acc_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t cnt:32;
#else
		uint64_t cnt:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_spxx_tpa_acc_s cn38xx;
	struct cvmx_spxx_tpa_acc_s cn38xxp2;
	struct cvmx_spxx_tpa_acc_s cn58xx;
	struct cvmx_spxx_tpa_acc_s cn58xxp1;
};

union cvmx_spxx_tpa_max {
	uint64_t u64;
	struct cvmx_spxx_tpa_max_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t max:32;
#else
		uint64_t max:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_spxx_tpa_max_s cn38xx;
	struct cvmx_spxx_tpa_max_s cn38xxp2;
	struct cvmx_spxx_tpa_max_s cn58xx;
	struct cvmx_spxx_tpa_max_s cn58xxp1;
};

union cvmx_spxx_tpa_sel {
	uint64_t u64;
	struct cvmx_spxx_tpa_sel_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t prtsel:4;
#else
		uint64_t prtsel:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_spxx_tpa_sel_s cn38xx;
	struct cvmx_spxx_tpa_sel_s cn38xxp2;
	struct cvmx_spxx_tpa_sel_s cn58xx;
	struct cvmx_spxx_tpa_sel_s cn58xxp1;
};

union cvmx_spxx_trn4_ctl {
	uint64_t u64;
	struct cvmx_spxx_trn4_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_13_63:51;
		uint64_t trntest:1;
		uint64_t jitter:3;
		uint64_t clr_boot:1;
		uint64_t set_boot:1;
		uint64_t maxdist:5;
		uint64_t macro_en:1;
		uint64_t mux_en:1;
#else
		uint64_t mux_en:1;
		uint64_t macro_en:1;
		uint64_t maxdist:5;
		uint64_t set_boot:1;
		uint64_t clr_boot:1;
		uint64_t jitter:3;
		uint64_t trntest:1;
		uint64_t reserved_13_63:51;
#endif
	} s;
	struct cvmx_spxx_trn4_ctl_s cn38xx;
	struct cvmx_spxx_trn4_ctl_s cn38xxp2;
	struct cvmx_spxx_trn4_ctl_s cn58xx;
	struct cvmx_spxx_trn4_ctl_s cn58xxp1;
};

#endif

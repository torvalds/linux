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

#ifndef __CVMX_DPI_DEFS_H__
#define __CVMX_DPI_DEFS_H__

#define CVMX_DPI_BIST_STATUS (CVMX_ADD_IO_SEG(0x0001DF0000000000ull))
#define CVMX_DPI_CTL (CVMX_ADD_IO_SEG(0x0001DF0000000040ull))
#define CVMX_DPI_DMAX_COUNTS(offset) (CVMX_ADD_IO_SEG(0x0001DF0000000300ull) + ((offset) & 7) * 8)
#define CVMX_DPI_DMAX_DBELL(offset) (CVMX_ADD_IO_SEG(0x0001DF0000000200ull) + ((offset) & 7) * 8)
#define CVMX_DPI_DMAX_ERR_RSP_STATUS(offset) (CVMX_ADD_IO_SEG(0x0001DF0000000A80ull) + ((offset) & 7) * 8)
#define CVMX_DPI_DMAX_IBUFF_SADDR(offset) (CVMX_ADD_IO_SEG(0x0001DF0000000280ull) + ((offset) & 7) * 8)
#define CVMX_DPI_DMAX_IFLIGHT(offset) (CVMX_ADD_IO_SEG(0x0001DF0000000A00ull) + ((offset) & 7) * 8)
#define CVMX_DPI_DMAX_NADDR(offset) (CVMX_ADD_IO_SEG(0x0001DF0000000380ull) + ((offset) & 7) * 8)
#define CVMX_DPI_DMAX_REQBNK0(offset) (CVMX_ADD_IO_SEG(0x0001DF0000000400ull) + ((offset) & 7) * 8)
#define CVMX_DPI_DMAX_REQBNK1(offset) (CVMX_ADD_IO_SEG(0x0001DF0000000480ull) + ((offset) & 7) * 8)
#define CVMX_DPI_DMA_CONTROL (CVMX_ADD_IO_SEG(0x0001DF0000000048ull))
#define CVMX_DPI_DMA_ENGX_EN(offset) (CVMX_ADD_IO_SEG(0x0001DF0000000080ull) + ((offset) & 7) * 8)
#define CVMX_DPI_DMA_PPX_CNT(offset) (CVMX_ADD_IO_SEG(0x0001DF0000000B00ull) + ((offset) & 31) * 8)
#define CVMX_DPI_ENGX_BUF(offset) (CVMX_ADD_IO_SEG(0x0001DF0000000880ull) + ((offset) & 7) * 8)
#define CVMX_DPI_INFO_REG (CVMX_ADD_IO_SEG(0x0001DF0000000980ull))
#define CVMX_DPI_INT_EN (CVMX_ADD_IO_SEG(0x0001DF0000000010ull))
#define CVMX_DPI_INT_REG (CVMX_ADD_IO_SEG(0x0001DF0000000008ull))
#define CVMX_DPI_NCBX_CFG(block_id) (CVMX_ADD_IO_SEG(0x0001DF0000000800ull))
#define CVMX_DPI_PINT_INFO (CVMX_ADD_IO_SEG(0x0001DF0000000830ull))
#define CVMX_DPI_PKT_ERR_RSP (CVMX_ADD_IO_SEG(0x0001DF0000000078ull))
#define CVMX_DPI_REQ_ERR_RSP (CVMX_ADD_IO_SEG(0x0001DF0000000058ull))
#define CVMX_DPI_REQ_ERR_RSP_EN (CVMX_ADD_IO_SEG(0x0001DF0000000068ull))
#define CVMX_DPI_REQ_ERR_RST (CVMX_ADD_IO_SEG(0x0001DF0000000060ull))
#define CVMX_DPI_REQ_ERR_RST_EN (CVMX_ADD_IO_SEG(0x0001DF0000000070ull))
#define CVMX_DPI_REQ_ERR_SKIP_COMP (CVMX_ADD_IO_SEG(0x0001DF0000000838ull))
#define CVMX_DPI_REQ_GBL_EN (CVMX_ADD_IO_SEG(0x0001DF0000000050ull))
#define CVMX_DPI_SLI_PRTX_CFG(offset) (CVMX_ADD_IO_SEG(0x0001DF0000000900ull) + ((offset) & 3) * 8)
static inline uint64_t CVMX_DPI_SLI_PRTX_ERR(unsigned long offset)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001DF0000000920ull) + (offset) * 8;
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:

		if (OCTEON_IS_MODEL(OCTEON_CN68XX_PASS1))
			return CVMX_ADD_IO_SEG(0x0001DF0000000928ull) + (offset) * 8;

		if (OCTEON_IS_MODEL(OCTEON_CN68XX_PASS2))
			return CVMX_ADD_IO_SEG(0x0001DF0000000920ull) + (offset) * 8;
		return CVMX_ADD_IO_SEG(0x0001DF0000000920ull) + (offset) * 8;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001DF0000000928ull) + (offset) * 8;
	}
	return CVMX_ADD_IO_SEG(0x0001DF0000000920ull) + (offset) * 8;
}

#define CVMX_DPI_SLI_PRTX_ERR_INFO(offset) (CVMX_ADD_IO_SEG(0x0001DF0000000940ull) + ((offset) & 3) * 8)

union cvmx_dpi_bist_status {
	uint64_t u64;
	struct cvmx_dpi_bist_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_47_63:17;
		uint64_t bist:47;
#else
		uint64_t bist:47;
		uint64_t reserved_47_63:17;
#endif
	} s;
	struct cvmx_dpi_bist_status_s cn61xx;
	struct cvmx_dpi_bist_status_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_45_63:19;
		uint64_t bist:45;
#else
		uint64_t bist:45;
		uint64_t reserved_45_63:19;
#endif
	} cn63xx;
	struct cvmx_dpi_bist_status_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_37_63:27;
		uint64_t bist:37;
#else
		uint64_t bist:37;
		uint64_t reserved_37_63:27;
#endif
	} cn63xxp1;
	struct cvmx_dpi_bist_status_s cn66xx;
	struct cvmx_dpi_bist_status_cn63xx cn68xx;
	struct cvmx_dpi_bist_status_cn63xx cn68xxp1;
	struct cvmx_dpi_bist_status_s cnf71xx;
};

union cvmx_dpi_ctl {
	uint64_t u64;
	struct cvmx_dpi_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t clk:1;
		uint64_t en:1;
#else
		uint64_t en:1;
		uint64_t clk:1;
		uint64_t reserved_2_63:62;
#endif
	} s;
	struct cvmx_dpi_ctl_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t en:1;
#else
		uint64_t en:1;
		uint64_t reserved_1_63:63;
#endif
	} cn61xx;
	struct cvmx_dpi_ctl_s cn63xx;
	struct cvmx_dpi_ctl_s cn63xxp1;
	struct cvmx_dpi_ctl_s cn66xx;
	struct cvmx_dpi_ctl_s cn68xx;
	struct cvmx_dpi_ctl_s cn68xxp1;
	struct cvmx_dpi_ctl_cn61xx cnf71xx;
};

union cvmx_dpi_dmax_counts {
	uint64_t u64;
	struct cvmx_dpi_dmax_counts_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_39_63:25;
		uint64_t fcnt:7;
		uint64_t dbell:32;
#else
		uint64_t dbell:32;
		uint64_t fcnt:7;
		uint64_t reserved_39_63:25;
#endif
	} s;
	struct cvmx_dpi_dmax_counts_s cn61xx;
	struct cvmx_dpi_dmax_counts_s cn63xx;
	struct cvmx_dpi_dmax_counts_s cn63xxp1;
	struct cvmx_dpi_dmax_counts_s cn66xx;
	struct cvmx_dpi_dmax_counts_s cn68xx;
	struct cvmx_dpi_dmax_counts_s cn68xxp1;
	struct cvmx_dpi_dmax_counts_s cnf71xx;
};

union cvmx_dpi_dmax_dbell {
	uint64_t u64;
	struct cvmx_dpi_dmax_dbell_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t dbell:16;
#else
		uint64_t dbell:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_dpi_dmax_dbell_s cn61xx;
	struct cvmx_dpi_dmax_dbell_s cn63xx;
	struct cvmx_dpi_dmax_dbell_s cn63xxp1;
	struct cvmx_dpi_dmax_dbell_s cn66xx;
	struct cvmx_dpi_dmax_dbell_s cn68xx;
	struct cvmx_dpi_dmax_dbell_s cn68xxp1;
	struct cvmx_dpi_dmax_dbell_s cnf71xx;
};

union cvmx_dpi_dmax_err_rsp_status {
	uint64_t u64;
	struct cvmx_dpi_dmax_err_rsp_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t status:6;
#else
		uint64_t status:6;
		uint64_t reserved_6_63:58;
#endif
	} s;
	struct cvmx_dpi_dmax_err_rsp_status_s cn61xx;
	struct cvmx_dpi_dmax_err_rsp_status_s cn66xx;
	struct cvmx_dpi_dmax_err_rsp_status_s cn68xx;
	struct cvmx_dpi_dmax_err_rsp_status_s cn68xxp1;
	struct cvmx_dpi_dmax_err_rsp_status_s cnf71xx;
};

union cvmx_dpi_dmax_ibuff_saddr {
	uint64_t u64;
	struct cvmx_dpi_dmax_ibuff_saddr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_62_63:2;
		uint64_t csize:14;
		uint64_t reserved_41_47:7;
		uint64_t idle:1;
		uint64_t saddr:33;
		uint64_t reserved_0_6:7;
#else
		uint64_t reserved_0_6:7;
		uint64_t saddr:33;
		uint64_t idle:1;
		uint64_t reserved_41_47:7;
		uint64_t csize:14;
		uint64_t reserved_62_63:2;
#endif
	} s;
	struct cvmx_dpi_dmax_ibuff_saddr_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_62_63:2;
		uint64_t csize:14;
		uint64_t reserved_41_47:7;
		uint64_t idle:1;
		uint64_t reserved_36_39:4;
		uint64_t saddr:29;
		uint64_t reserved_0_6:7;
#else
		uint64_t reserved_0_6:7;
		uint64_t saddr:29;
		uint64_t reserved_36_39:4;
		uint64_t idle:1;
		uint64_t reserved_41_47:7;
		uint64_t csize:14;
		uint64_t reserved_62_63:2;
#endif
	} cn61xx;
	struct cvmx_dpi_dmax_ibuff_saddr_cn61xx cn63xx;
	struct cvmx_dpi_dmax_ibuff_saddr_cn61xx cn63xxp1;
	struct cvmx_dpi_dmax_ibuff_saddr_cn61xx cn66xx;
	struct cvmx_dpi_dmax_ibuff_saddr_s cn68xx;
	struct cvmx_dpi_dmax_ibuff_saddr_s cn68xxp1;
	struct cvmx_dpi_dmax_ibuff_saddr_cn61xx cnf71xx;
};

union cvmx_dpi_dmax_iflight {
	uint64_t u64;
	struct cvmx_dpi_dmax_iflight_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t cnt:3;
#else
		uint64_t cnt:3;
		uint64_t reserved_3_63:61;
#endif
	} s;
	struct cvmx_dpi_dmax_iflight_s cn61xx;
	struct cvmx_dpi_dmax_iflight_s cn66xx;
	struct cvmx_dpi_dmax_iflight_s cn68xx;
	struct cvmx_dpi_dmax_iflight_s cn68xxp1;
	struct cvmx_dpi_dmax_iflight_s cnf71xx;
};

union cvmx_dpi_dmax_naddr {
	uint64_t u64;
	struct cvmx_dpi_dmax_naddr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_40_63:24;
		uint64_t addr:40;
#else
		uint64_t addr:40;
		uint64_t reserved_40_63:24;
#endif
	} s;
	struct cvmx_dpi_dmax_naddr_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_36_63:28;
		uint64_t addr:36;
#else
		uint64_t addr:36;
		uint64_t reserved_36_63:28;
#endif
	} cn61xx;
	struct cvmx_dpi_dmax_naddr_cn61xx cn63xx;
	struct cvmx_dpi_dmax_naddr_cn61xx cn63xxp1;
	struct cvmx_dpi_dmax_naddr_cn61xx cn66xx;
	struct cvmx_dpi_dmax_naddr_s cn68xx;
	struct cvmx_dpi_dmax_naddr_s cn68xxp1;
	struct cvmx_dpi_dmax_naddr_cn61xx cnf71xx;
};

union cvmx_dpi_dmax_reqbnk0 {
	uint64_t u64;
	struct cvmx_dpi_dmax_reqbnk0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t state:64;
#else
		uint64_t state:64;
#endif
	} s;
	struct cvmx_dpi_dmax_reqbnk0_s cn61xx;
	struct cvmx_dpi_dmax_reqbnk0_s cn63xx;
	struct cvmx_dpi_dmax_reqbnk0_s cn63xxp1;
	struct cvmx_dpi_dmax_reqbnk0_s cn66xx;
	struct cvmx_dpi_dmax_reqbnk0_s cn68xx;
	struct cvmx_dpi_dmax_reqbnk0_s cn68xxp1;
	struct cvmx_dpi_dmax_reqbnk0_s cnf71xx;
};

union cvmx_dpi_dmax_reqbnk1 {
	uint64_t u64;
	struct cvmx_dpi_dmax_reqbnk1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t state:64;
#else
		uint64_t state:64;
#endif
	} s;
	struct cvmx_dpi_dmax_reqbnk1_s cn61xx;
	struct cvmx_dpi_dmax_reqbnk1_s cn63xx;
	struct cvmx_dpi_dmax_reqbnk1_s cn63xxp1;
	struct cvmx_dpi_dmax_reqbnk1_s cn66xx;
	struct cvmx_dpi_dmax_reqbnk1_s cn68xx;
	struct cvmx_dpi_dmax_reqbnk1_s cn68xxp1;
	struct cvmx_dpi_dmax_reqbnk1_s cnf71xx;
};

union cvmx_dpi_dma_control {
	uint64_t u64;
	struct cvmx_dpi_dma_control_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_62_63:2;
		uint64_t dici_mode:1;
		uint64_t pkt_en1:1;
		uint64_t ffp_dis:1;
		uint64_t commit_mode:1;
		uint64_t pkt_hp:1;
		uint64_t pkt_en:1;
		uint64_t reserved_54_55:2;
		uint64_t dma_enb:6;
		uint64_t reserved_34_47:14;
		uint64_t b0_lend:1;
		uint64_t dwb_denb:1;
		uint64_t dwb_ichk:9;
		uint64_t fpa_que:3;
		uint64_t o_add1:1;
		uint64_t o_ro:1;
		uint64_t o_ns:1;
		uint64_t o_es:2;
		uint64_t o_mode:1;
		uint64_t reserved_0_13:14;
#else
		uint64_t reserved_0_13:14;
		uint64_t o_mode:1;
		uint64_t o_es:2;
		uint64_t o_ns:1;
		uint64_t o_ro:1;
		uint64_t o_add1:1;
		uint64_t fpa_que:3;
		uint64_t dwb_ichk:9;
		uint64_t dwb_denb:1;
		uint64_t b0_lend:1;
		uint64_t reserved_34_47:14;
		uint64_t dma_enb:6;
		uint64_t reserved_54_55:2;
		uint64_t pkt_en:1;
		uint64_t pkt_hp:1;
		uint64_t commit_mode:1;
		uint64_t ffp_dis:1;
		uint64_t pkt_en1:1;
		uint64_t dici_mode:1;
		uint64_t reserved_62_63:2;
#endif
	} s;
	struct cvmx_dpi_dma_control_s cn61xx;
	struct cvmx_dpi_dma_control_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_61_63:3;
		uint64_t pkt_en1:1;
		uint64_t ffp_dis:1;
		uint64_t commit_mode:1;
		uint64_t pkt_hp:1;
		uint64_t pkt_en:1;
		uint64_t reserved_54_55:2;
		uint64_t dma_enb:6;
		uint64_t reserved_34_47:14;
		uint64_t b0_lend:1;
		uint64_t dwb_denb:1;
		uint64_t dwb_ichk:9;
		uint64_t fpa_que:3;
		uint64_t o_add1:1;
		uint64_t o_ro:1;
		uint64_t o_ns:1;
		uint64_t o_es:2;
		uint64_t o_mode:1;
		uint64_t reserved_0_13:14;
#else
		uint64_t reserved_0_13:14;
		uint64_t o_mode:1;
		uint64_t o_es:2;
		uint64_t o_ns:1;
		uint64_t o_ro:1;
		uint64_t o_add1:1;
		uint64_t fpa_que:3;
		uint64_t dwb_ichk:9;
		uint64_t dwb_denb:1;
		uint64_t b0_lend:1;
		uint64_t reserved_34_47:14;
		uint64_t dma_enb:6;
		uint64_t reserved_54_55:2;
		uint64_t pkt_en:1;
		uint64_t pkt_hp:1;
		uint64_t commit_mode:1;
		uint64_t ffp_dis:1;
		uint64_t pkt_en1:1;
		uint64_t reserved_61_63:3;
#endif
	} cn63xx;
	struct cvmx_dpi_dma_control_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_59_63:5;
		uint64_t commit_mode:1;
		uint64_t pkt_hp:1;
		uint64_t pkt_en:1;
		uint64_t reserved_54_55:2;
		uint64_t dma_enb:6;
		uint64_t reserved_34_47:14;
		uint64_t b0_lend:1;
		uint64_t dwb_denb:1;
		uint64_t dwb_ichk:9;
		uint64_t fpa_que:3;
		uint64_t o_add1:1;
		uint64_t o_ro:1;
		uint64_t o_ns:1;
		uint64_t o_es:2;
		uint64_t o_mode:1;
		uint64_t reserved_0_13:14;
#else
		uint64_t reserved_0_13:14;
		uint64_t o_mode:1;
		uint64_t o_es:2;
		uint64_t o_ns:1;
		uint64_t o_ro:1;
		uint64_t o_add1:1;
		uint64_t fpa_que:3;
		uint64_t dwb_ichk:9;
		uint64_t dwb_denb:1;
		uint64_t b0_lend:1;
		uint64_t reserved_34_47:14;
		uint64_t dma_enb:6;
		uint64_t reserved_54_55:2;
		uint64_t pkt_en:1;
		uint64_t pkt_hp:1;
		uint64_t commit_mode:1;
		uint64_t reserved_59_63:5;
#endif
	} cn63xxp1;
	struct cvmx_dpi_dma_control_cn63xx cn66xx;
	struct cvmx_dpi_dma_control_s cn68xx;
	struct cvmx_dpi_dma_control_cn63xx cn68xxp1;
	struct cvmx_dpi_dma_control_s cnf71xx;
};

union cvmx_dpi_dma_engx_en {
	uint64_t u64;
	struct cvmx_dpi_dma_engx_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t qen:8;
#else
		uint64_t qen:8;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_dpi_dma_engx_en_s cn61xx;
	struct cvmx_dpi_dma_engx_en_s cn63xx;
	struct cvmx_dpi_dma_engx_en_s cn63xxp1;
	struct cvmx_dpi_dma_engx_en_s cn66xx;
	struct cvmx_dpi_dma_engx_en_s cn68xx;
	struct cvmx_dpi_dma_engx_en_s cn68xxp1;
	struct cvmx_dpi_dma_engx_en_s cnf71xx;
};

union cvmx_dpi_dma_ppx_cnt {
	uint64_t u64;
	struct cvmx_dpi_dma_ppx_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t cnt:16;
#else
		uint64_t cnt:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_dpi_dma_ppx_cnt_s cn61xx;
	struct cvmx_dpi_dma_ppx_cnt_s cn68xx;
	struct cvmx_dpi_dma_ppx_cnt_s cnf71xx;
};

union cvmx_dpi_engx_buf {
	uint64_t u64;
	struct cvmx_dpi_engx_buf_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_37_63:27;
		uint64_t compblks:5;
		uint64_t reserved_9_31:23;
		uint64_t base:5;
		uint64_t blks:4;
#else
		uint64_t blks:4;
		uint64_t base:5;
		uint64_t reserved_9_31:23;
		uint64_t compblks:5;
		uint64_t reserved_37_63:27;
#endif
	} s;
	struct cvmx_dpi_engx_buf_s cn61xx;
	struct cvmx_dpi_engx_buf_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t base:4;
		uint64_t blks:4;
#else
		uint64_t blks:4;
		uint64_t base:4;
		uint64_t reserved_8_63:56;
#endif
	} cn63xx;
	struct cvmx_dpi_engx_buf_cn63xx cn63xxp1;
	struct cvmx_dpi_engx_buf_s cn66xx;
	struct cvmx_dpi_engx_buf_s cn68xx;
	struct cvmx_dpi_engx_buf_s cn68xxp1;
	struct cvmx_dpi_engx_buf_s cnf71xx;
};

union cvmx_dpi_info_reg {
	uint64_t u64;
	struct cvmx_dpi_info_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t ffp:4;
		uint64_t reserved_2_3:2;
		uint64_t ncb:1;
		uint64_t rsl:1;
#else
		uint64_t rsl:1;
		uint64_t ncb:1;
		uint64_t reserved_2_3:2;
		uint64_t ffp:4;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_dpi_info_reg_s cn61xx;
	struct cvmx_dpi_info_reg_s cn63xx;
	struct cvmx_dpi_info_reg_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t ncb:1;
		uint64_t rsl:1;
#else
		uint64_t rsl:1;
		uint64_t ncb:1;
		uint64_t reserved_2_63:62;
#endif
	} cn63xxp1;
	struct cvmx_dpi_info_reg_s cn66xx;
	struct cvmx_dpi_info_reg_s cn68xx;
	struct cvmx_dpi_info_reg_s cn68xxp1;
	struct cvmx_dpi_info_reg_s cnf71xx;
};

union cvmx_dpi_int_en {
	uint64_t u64;
	struct cvmx_dpi_int_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_28_63:36;
		uint64_t sprt3_rst:1;
		uint64_t sprt2_rst:1;
		uint64_t sprt1_rst:1;
		uint64_t sprt0_rst:1;
		uint64_t reserved_23_23:1;
		uint64_t req_badfil:1;
		uint64_t req_inull:1;
		uint64_t req_anull:1;
		uint64_t req_undflw:1;
		uint64_t req_ovrflw:1;
		uint64_t req_badlen:1;
		uint64_t req_badadr:1;
		uint64_t dmadbo:8;
		uint64_t reserved_2_7:6;
		uint64_t nfovr:1;
		uint64_t nderr:1;
#else
		uint64_t nderr:1;
		uint64_t nfovr:1;
		uint64_t reserved_2_7:6;
		uint64_t dmadbo:8;
		uint64_t req_badadr:1;
		uint64_t req_badlen:1;
		uint64_t req_ovrflw:1;
		uint64_t req_undflw:1;
		uint64_t req_anull:1;
		uint64_t req_inull:1;
		uint64_t req_badfil:1;
		uint64_t reserved_23_23:1;
		uint64_t sprt0_rst:1;
		uint64_t sprt1_rst:1;
		uint64_t sprt2_rst:1;
		uint64_t sprt3_rst:1;
		uint64_t reserved_28_63:36;
#endif
	} s;
	struct cvmx_dpi_int_en_s cn61xx;
	struct cvmx_dpi_int_en_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_26_63:38;
		uint64_t sprt1_rst:1;
		uint64_t sprt0_rst:1;
		uint64_t reserved_23_23:1;
		uint64_t req_badfil:1;
		uint64_t req_inull:1;
		uint64_t req_anull:1;
		uint64_t req_undflw:1;
		uint64_t req_ovrflw:1;
		uint64_t req_badlen:1;
		uint64_t req_badadr:1;
		uint64_t dmadbo:8;
		uint64_t reserved_2_7:6;
		uint64_t nfovr:1;
		uint64_t nderr:1;
#else
		uint64_t nderr:1;
		uint64_t nfovr:1;
		uint64_t reserved_2_7:6;
		uint64_t dmadbo:8;
		uint64_t req_badadr:1;
		uint64_t req_badlen:1;
		uint64_t req_ovrflw:1;
		uint64_t req_undflw:1;
		uint64_t req_anull:1;
		uint64_t req_inull:1;
		uint64_t req_badfil:1;
		uint64_t reserved_23_23:1;
		uint64_t sprt0_rst:1;
		uint64_t sprt1_rst:1;
		uint64_t reserved_26_63:38;
#endif
	} cn63xx;
	struct cvmx_dpi_int_en_cn63xx cn63xxp1;
	struct cvmx_dpi_int_en_s cn66xx;
	struct cvmx_dpi_int_en_cn63xx cn68xx;
	struct cvmx_dpi_int_en_cn63xx cn68xxp1;
	struct cvmx_dpi_int_en_s cnf71xx;
};

union cvmx_dpi_int_reg {
	uint64_t u64;
	struct cvmx_dpi_int_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_28_63:36;
		uint64_t sprt3_rst:1;
		uint64_t sprt2_rst:1;
		uint64_t sprt1_rst:1;
		uint64_t sprt0_rst:1;
		uint64_t reserved_23_23:1;
		uint64_t req_badfil:1;
		uint64_t req_inull:1;
		uint64_t req_anull:1;
		uint64_t req_undflw:1;
		uint64_t req_ovrflw:1;
		uint64_t req_badlen:1;
		uint64_t req_badadr:1;
		uint64_t dmadbo:8;
		uint64_t reserved_2_7:6;
		uint64_t nfovr:1;
		uint64_t nderr:1;
#else
		uint64_t nderr:1;
		uint64_t nfovr:1;
		uint64_t reserved_2_7:6;
		uint64_t dmadbo:8;
		uint64_t req_badadr:1;
		uint64_t req_badlen:1;
		uint64_t req_ovrflw:1;
		uint64_t req_undflw:1;
		uint64_t req_anull:1;
		uint64_t req_inull:1;
		uint64_t req_badfil:1;
		uint64_t reserved_23_23:1;
		uint64_t sprt0_rst:1;
		uint64_t sprt1_rst:1;
		uint64_t sprt2_rst:1;
		uint64_t sprt3_rst:1;
		uint64_t reserved_28_63:36;
#endif
	} s;
	struct cvmx_dpi_int_reg_s cn61xx;
	struct cvmx_dpi_int_reg_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_26_63:38;
		uint64_t sprt1_rst:1;
		uint64_t sprt0_rst:1;
		uint64_t reserved_23_23:1;
		uint64_t req_badfil:1;
		uint64_t req_inull:1;
		uint64_t req_anull:1;
		uint64_t req_undflw:1;
		uint64_t req_ovrflw:1;
		uint64_t req_badlen:1;
		uint64_t req_badadr:1;
		uint64_t dmadbo:8;
		uint64_t reserved_2_7:6;
		uint64_t nfovr:1;
		uint64_t nderr:1;
#else
		uint64_t nderr:1;
		uint64_t nfovr:1;
		uint64_t reserved_2_7:6;
		uint64_t dmadbo:8;
		uint64_t req_badadr:1;
		uint64_t req_badlen:1;
		uint64_t req_ovrflw:1;
		uint64_t req_undflw:1;
		uint64_t req_anull:1;
		uint64_t req_inull:1;
		uint64_t req_badfil:1;
		uint64_t reserved_23_23:1;
		uint64_t sprt0_rst:1;
		uint64_t sprt1_rst:1;
		uint64_t reserved_26_63:38;
#endif
	} cn63xx;
	struct cvmx_dpi_int_reg_cn63xx cn63xxp1;
	struct cvmx_dpi_int_reg_s cn66xx;
	struct cvmx_dpi_int_reg_cn63xx cn68xx;
	struct cvmx_dpi_int_reg_cn63xx cn68xxp1;
	struct cvmx_dpi_int_reg_s cnf71xx;
};

union cvmx_dpi_ncbx_cfg {
	uint64_t u64;
	struct cvmx_dpi_ncbx_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t molr:6;
#else
		uint64_t molr:6;
		uint64_t reserved_6_63:58;
#endif
	} s;
	struct cvmx_dpi_ncbx_cfg_s cn61xx;
	struct cvmx_dpi_ncbx_cfg_s cn66xx;
	struct cvmx_dpi_ncbx_cfg_s cn68xx;
	struct cvmx_dpi_ncbx_cfg_s cnf71xx;
};

union cvmx_dpi_pint_info {
	uint64_t u64;
	struct cvmx_dpi_pint_info_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_14_63:50;
		uint64_t iinfo:6;
		uint64_t reserved_6_7:2;
		uint64_t sinfo:6;
#else
		uint64_t sinfo:6;
		uint64_t reserved_6_7:2;
		uint64_t iinfo:6;
		uint64_t reserved_14_63:50;
#endif
	} s;
	struct cvmx_dpi_pint_info_s cn61xx;
	struct cvmx_dpi_pint_info_s cn63xx;
	struct cvmx_dpi_pint_info_s cn63xxp1;
	struct cvmx_dpi_pint_info_s cn66xx;
	struct cvmx_dpi_pint_info_s cn68xx;
	struct cvmx_dpi_pint_info_s cn68xxp1;
	struct cvmx_dpi_pint_info_s cnf71xx;
};

union cvmx_dpi_pkt_err_rsp {
	uint64_t u64;
	struct cvmx_dpi_pkt_err_rsp_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t pkterr:1;
#else
		uint64_t pkterr:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_dpi_pkt_err_rsp_s cn61xx;
	struct cvmx_dpi_pkt_err_rsp_s cn63xx;
	struct cvmx_dpi_pkt_err_rsp_s cn63xxp1;
	struct cvmx_dpi_pkt_err_rsp_s cn66xx;
	struct cvmx_dpi_pkt_err_rsp_s cn68xx;
	struct cvmx_dpi_pkt_err_rsp_s cn68xxp1;
	struct cvmx_dpi_pkt_err_rsp_s cnf71xx;
};

union cvmx_dpi_req_err_rsp {
	uint64_t u64;
	struct cvmx_dpi_req_err_rsp_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t qerr:8;
#else
		uint64_t qerr:8;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_dpi_req_err_rsp_s cn61xx;
	struct cvmx_dpi_req_err_rsp_s cn63xx;
	struct cvmx_dpi_req_err_rsp_s cn63xxp1;
	struct cvmx_dpi_req_err_rsp_s cn66xx;
	struct cvmx_dpi_req_err_rsp_s cn68xx;
	struct cvmx_dpi_req_err_rsp_s cn68xxp1;
	struct cvmx_dpi_req_err_rsp_s cnf71xx;
};

union cvmx_dpi_req_err_rsp_en {
	uint64_t u64;
	struct cvmx_dpi_req_err_rsp_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t en:8;
#else
		uint64_t en:8;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_dpi_req_err_rsp_en_s cn61xx;
	struct cvmx_dpi_req_err_rsp_en_s cn63xx;
	struct cvmx_dpi_req_err_rsp_en_s cn63xxp1;
	struct cvmx_dpi_req_err_rsp_en_s cn66xx;
	struct cvmx_dpi_req_err_rsp_en_s cn68xx;
	struct cvmx_dpi_req_err_rsp_en_s cn68xxp1;
	struct cvmx_dpi_req_err_rsp_en_s cnf71xx;
};

union cvmx_dpi_req_err_rst {
	uint64_t u64;
	struct cvmx_dpi_req_err_rst_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t qerr:8;
#else
		uint64_t qerr:8;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_dpi_req_err_rst_s cn61xx;
	struct cvmx_dpi_req_err_rst_s cn63xx;
	struct cvmx_dpi_req_err_rst_s cn63xxp1;
	struct cvmx_dpi_req_err_rst_s cn66xx;
	struct cvmx_dpi_req_err_rst_s cn68xx;
	struct cvmx_dpi_req_err_rst_s cn68xxp1;
	struct cvmx_dpi_req_err_rst_s cnf71xx;
};

union cvmx_dpi_req_err_rst_en {
	uint64_t u64;
	struct cvmx_dpi_req_err_rst_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t en:8;
#else
		uint64_t en:8;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_dpi_req_err_rst_en_s cn61xx;
	struct cvmx_dpi_req_err_rst_en_s cn63xx;
	struct cvmx_dpi_req_err_rst_en_s cn63xxp1;
	struct cvmx_dpi_req_err_rst_en_s cn66xx;
	struct cvmx_dpi_req_err_rst_en_s cn68xx;
	struct cvmx_dpi_req_err_rst_en_s cn68xxp1;
	struct cvmx_dpi_req_err_rst_en_s cnf71xx;
};

union cvmx_dpi_req_err_skip_comp {
	uint64_t u64;
	struct cvmx_dpi_req_err_skip_comp_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_24_63:40;
		uint64_t en_rst:8;
		uint64_t reserved_8_15:8;
		uint64_t en_rsp:8;
#else
		uint64_t en_rsp:8;
		uint64_t reserved_8_15:8;
		uint64_t en_rst:8;
		uint64_t reserved_24_63:40;
#endif
	} s;
	struct cvmx_dpi_req_err_skip_comp_s cn61xx;
	struct cvmx_dpi_req_err_skip_comp_s cn66xx;
	struct cvmx_dpi_req_err_skip_comp_s cn68xx;
	struct cvmx_dpi_req_err_skip_comp_s cn68xxp1;
	struct cvmx_dpi_req_err_skip_comp_s cnf71xx;
};

union cvmx_dpi_req_gbl_en {
	uint64_t u64;
	struct cvmx_dpi_req_gbl_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t qen:8;
#else
		uint64_t qen:8;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_dpi_req_gbl_en_s cn61xx;
	struct cvmx_dpi_req_gbl_en_s cn63xx;
	struct cvmx_dpi_req_gbl_en_s cn63xxp1;
	struct cvmx_dpi_req_gbl_en_s cn66xx;
	struct cvmx_dpi_req_gbl_en_s cn68xx;
	struct cvmx_dpi_req_gbl_en_s cn68xxp1;
	struct cvmx_dpi_req_gbl_en_s cnf71xx;
};

union cvmx_dpi_sli_prtx_cfg {
	uint64_t u64;
	struct cvmx_dpi_sli_prtx_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_25_63:39;
		uint64_t halt:1;
		uint64_t qlm_cfg:4;
		uint64_t reserved_17_19:3;
		uint64_t rd_mode:1;
		uint64_t reserved_14_15:2;
		uint64_t molr:6;
		uint64_t mps_lim:1;
		uint64_t reserved_5_6:2;
		uint64_t mps:1;
		uint64_t mrrs_lim:1;
		uint64_t reserved_2_2:1;
		uint64_t mrrs:2;
#else
		uint64_t mrrs:2;
		uint64_t reserved_2_2:1;
		uint64_t mrrs_lim:1;
		uint64_t mps:1;
		uint64_t reserved_5_6:2;
		uint64_t mps_lim:1;
		uint64_t molr:6;
		uint64_t reserved_14_15:2;
		uint64_t rd_mode:1;
		uint64_t reserved_17_19:3;
		uint64_t qlm_cfg:4;
		uint64_t halt:1;
		uint64_t reserved_25_63:39;
#endif
	} s;
	struct cvmx_dpi_sli_prtx_cfg_s cn61xx;
	struct cvmx_dpi_sli_prtx_cfg_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_25_63:39;
		uint64_t halt:1;
		uint64_t reserved_21_23:3;
		uint64_t qlm_cfg:1;
		uint64_t reserved_17_19:3;
		uint64_t rd_mode:1;
		uint64_t reserved_14_15:2;
		uint64_t molr:6;
		uint64_t mps_lim:1;
		uint64_t reserved_5_6:2;
		uint64_t mps:1;
		uint64_t mrrs_lim:1;
		uint64_t reserved_2_2:1;
		uint64_t mrrs:2;
#else
		uint64_t mrrs:2;
		uint64_t reserved_2_2:1;
		uint64_t mrrs_lim:1;
		uint64_t mps:1;
		uint64_t reserved_5_6:2;
		uint64_t mps_lim:1;
		uint64_t molr:6;
		uint64_t reserved_14_15:2;
		uint64_t rd_mode:1;
		uint64_t reserved_17_19:3;
		uint64_t qlm_cfg:1;
		uint64_t reserved_21_23:3;
		uint64_t halt:1;
		uint64_t reserved_25_63:39;
#endif
	} cn63xx;
	struct cvmx_dpi_sli_prtx_cfg_cn63xx cn63xxp1;
	struct cvmx_dpi_sli_prtx_cfg_s cn66xx;
	struct cvmx_dpi_sli_prtx_cfg_cn63xx cn68xx;
	struct cvmx_dpi_sli_prtx_cfg_cn63xx cn68xxp1;
	struct cvmx_dpi_sli_prtx_cfg_s cnf71xx;
};

union cvmx_dpi_sli_prtx_err {
	uint64_t u64;
	struct cvmx_dpi_sli_prtx_err_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t addr:61;
		uint64_t reserved_0_2:3;
#else
		uint64_t reserved_0_2:3;
		uint64_t addr:61;
#endif
	} s;
	struct cvmx_dpi_sli_prtx_err_s cn61xx;
	struct cvmx_dpi_sli_prtx_err_s cn63xx;
	struct cvmx_dpi_sli_prtx_err_s cn63xxp1;
	struct cvmx_dpi_sli_prtx_err_s cn66xx;
	struct cvmx_dpi_sli_prtx_err_s cn68xx;
	struct cvmx_dpi_sli_prtx_err_s cn68xxp1;
	struct cvmx_dpi_sli_prtx_err_s cnf71xx;
};

union cvmx_dpi_sli_prtx_err_info {
	uint64_t u64;
	struct cvmx_dpi_sli_prtx_err_info_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t lock:1;
		uint64_t reserved_5_7:3;
		uint64_t type:1;
		uint64_t reserved_3_3:1;
		uint64_t reqq:3;
#else
		uint64_t reqq:3;
		uint64_t reserved_3_3:1;
		uint64_t type:1;
		uint64_t reserved_5_7:3;
		uint64_t lock:1;
		uint64_t reserved_9_63:55;
#endif
	} s;
	struct cvmx_dpi_sli_prtx_err_info_s cn61xx;
	struct cvmx_dpi_sli_prtx_err_info_s cn63xx;
	struct cvmx_dpi_sli_prtx_err_info_s cn63xxp1;
	struct cvmx_dpi_sli_prtx_err_info_s cn66xx;
	struct cvmx_dpi_sli_prtx_err_info_s cn68xx;
	struct cvmx_dpi_sli_prtx_err_info_s cn68xxp1;
	struct cvmx_dpi_sli_prtx_err_info_s cnf71xx;
};

#endif

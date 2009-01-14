/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2008 Cavium Networks
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

#ifndef __CVMX_MIO_DEFS_H__
#define __CVMX_MIO_DEFS_H__

#define CVMX_MIO_BOOT_BIST_STAT \
	 CVMX_ADD_IO_SEG(0x00011800000000F8ull)
#define CVMX_MIO_BOOT_COMP \
	 CVMX_ADD_IO_SEG(0x00011800000000B8ull)
#define CVMX_MIO_BOOT_DMA_CFGX(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000100ull + (((offset) & 3) * 8))
#define CVMX_MIO_BOOT_DMA_INTX(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000138ull + (((offset) & 3) * 8))
#define CVMX_MIO_BOOT_DMA_INT_ENX(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000150ull + (((offset) & 3) * 8))
#define CVMX_MIO_BOOT_DMA_TIMX(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000120ull + (((offset) & 3) * 8))
#define CVMX_MIO_BOOT_ERR \
	 CVMX_ADD_IO_SEG(0x00011800000000A0ull)
#define CVMX_MIO_BOOT_INT \
	 CVMX_ADD_IO_SEG(0x00011800000000A8ull)
#define CVMX_MIO_BOOT_LOC_ADR \
	 CVMX_ADD_IO_SEG(0x0001180000000090ull)
#define CVMX_MIO_BOOT_LOC_CFGX(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000080ull + (((offset) & 1) * 8))
#define CVMX_MIO_BOOT_LOC_DAT \
	 CVMX_ADD_IO_SEG(0x0001180000000098ull)
#define CVMX_MIO_BOOT_PIN_DEFS \
	 CVMX_ADD_IO_SEG(0x00011800000000C0ull)
#define CVMX_MIO_BOOT_REG_CFGX(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000000ull + (((offset) & 7) * 8))
#define CVMX_MIO_BOOT_REG_TIMX(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000040ull + (((offset) & 7) * 8))
#define CVMX_MIO_BOOT_THR \
	 CVMX_ADD_IO_SEG(0x00011800000000B0ull)
#define CVMX_MIO_FUS_BNK_DATX(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000001520ull + (((offset) & 3) * 8))
#define CVMX_MIO_FUS_DAT0 \
	 CVMX_ADD_IO_SEG(0x0001180000001400ull)
#define CVMX_MIO_FUS_DAT1 \
	 CVMX_ADD_IO_SEG(0x0001180000001408ull)
#define CVMX_MIO_FUS_DAT2 \
	 CVMX_ADD_IO_SEG(0x0001180000001410ull)
#define CVMX_MIO_FUS_DAT3 \
	 CVMX_ADD_IO_SEG(0x0001180000001418ull)
#define CVMX_MIO_FUS_EMA \
	 CVMX_ADD_IO_SEG(0x0001180000001550ull)
#define CVMX_MIO_FUS_PDF \
	 CVMX_ADD_IO_SEG(0x0001180000001420ull)
#define CVMX_MIO_FUS_PLL \
	 CVMX_ADD_IO_SEG(0x0001180000001580ull)
#define CVMX_MIO_FUS_PROG \
	 CVMX_ADD_IO_SEG(0x0001180000001510ull)
#define CVMX_MIO_FUS_PROG_TIMES \
	 CVMX_ADD_IO_SEG(0x0001180000001518ull)
#define CVMX_MIO_FUS_RCMD \
	 CVMX_ADD_IO_SEG(0x0001180000001500ull)
#define CVMX_MIO_FUS_SPR_REPAIR_RES \
	 CVMX_ADD_IO_SEG(0x0001180000001548ull)
#define CVMX_MIO_FUS_SPR_REPAIR_SUM \
	 CVMX_ADD_IO_SEG(0x0001180000001540ull)
#define CVMX_MIO_FUS_UNLOCK \
	 CVMX_ADD_IO_SEG(0x0001180000001578ull)
#define CVMX_MIO_FUS_WADR \
	 CVMX_ADD_IO_SEG(0x0001180000001508ull)
#define CVMX_MIO_NDF_DMA_CFG \
	 CVMX_ADD_IO_SEG(0x0001180000000168ull)
#define CVMX_MIO_NDF_DMA_INT \
	 CVMX_ADD_IO_SEG(0x0001180000000170ull)
#define CVMX_MIO_NDF_DMA_INT_EN \
	 CVMX_ADD_IO_SEG(0x0001180000000178ull)
#define CVMX_MIO_PLL_CTL \
	 CVMX_ADD_IO_SEG(0x0001180000001448ull)
#define CVMX_MIO_PLL_SETTING \
	 CVMX_ADD_IO_SEG(0x0001180000001440ull)
#define CVMX_MIO_TWSX_INT(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000001010ull + (((offset) & 1) * 512))
#define CVMX_MIO_TWSX_SW_TWSI(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000001000ull + (((offset) & 1) * 512))
#define CVMX_MIO_TWSX_SW_TWSI_EXT(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000001018ull + (((offset) & 1) * 512))
#define CVMX_MIO_TWSX_TWSI_SW(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000001008ull + (((offset) & 1) * 512))
#define CVMX_MIO_UART2_DLH \
	 CVMX_ADD_IO_SEG(0x0001180000000488ull)
#define CVMX_MIO_UART2_DLL \
	 CVMX_ADD_IO_SEG(0x0001180000000480ull)
#define CVMX_MIO_UART2_FAR \
	 CVMX_ADD_IO_SEG(0x0001180000000520ull)
#define CVMX_MIO_UART2_FCR \
	 CVMX_ADD_IO_SEG(0x0001180000000450ull)
#define CVMX_MIO_UART2_HTX \
	 CVMX_ADD_IO_SEG(0x0001180000000708ull)
#define CVMX_MIO_UART2_IER \
	 CVMX_ADD_IO_SEG(0x0001180000000408ull)
#define CVMX_MIO_UART2_IIR \
	 CVMX_ADD_IO_SEG(0x0001180000000410ull)
#define CVMX_MIO_UART2_LCR \
	 CVMX_ADD_IO_SEG(0x0001180000000418ull)
#define CVMX_MIO_UART2_LSR \
	 CVMX_ADD_IO_SEG(0x0001180000000428ull)
#define CVMX_MIO_UART2_MCR \
	 CVMX_ADD_IO_SEG(0x0001180000000420ull)
#define CVMX_MIO_UART2_MSR \
	 CVMX_ADD_IO_SEG(0x0001180000000430ull)
#define CVMX_MIO_UART2_RBR \
	 CVMX_ADD_IO_SEG(0x0001180000000400ull)
#define CVMX_MIO_UART2_RFL \
	 CVMX_ADD_IO_SEG(0x0001180000000608ull)
#define CVMX_MIO_UART2_RFW \
	 CVMX_ADD_IO_SEG(0x0001180000000530ull)
#define CVMX_MIO_UART2_SBCR \
	 CVMX_ADD_IO_SEG(0x0001180000000620ull)
#define CVMX_MIO_UART2_SCR \
	 CVMX_ADD_IO_SEG(0x0001180000000438ull)
#define CVMX_MIO_UART2_SFE \
	 CVMX_ADD_IO_SEG(0x0001180000000630ull)
#define CVMX_MIO_UART2_SRR \
	 CVMX_ADD_IO_SEG(0x0001180000000610ull)
#define CVMX_MIO_UART2_SRT \
	 CVMX_ADD_IO_SEG(0x0001180000000638ull)
#define CVMX_MIO_UART2_SRTS \
	 CVMX_ADD_IO_SEG(0x0001180000000618ull)
#define CVMX_MIO_UART2_STT \
	 CVMX_ADD_IO_SEG(0x0001180000000700ull)
#define CVMX_MIO_UART2_TFL \
	 CVMX_ADD_IO_SEG(0x0001180000000600ull)
#define CVMX_MIO_UART2_TFR \
	 CVMX_ADD_IO_SEG(0x0001180000000528ull)
#define CVMX_MIO_UART2_THR \
	 CVMX_ADD_IO_SEG(0x0001180000000440ull)
#define CVMX_MIO_UART2_USR \
	 CVMX_ADD_IO_SEG(0x0001180000000538ull)
#define CVMX_MIO_UARTX_DLH(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000888ull + (((offset) & 1) * 1024))
#define CVMX_MIO_UARTX_DLL(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000880ull + (((offset) & 1) * 1024))
#define CVMX_MIO_UARTX_FAR(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000920ull + (((offset) & 1) * 1024))
#define CVMX_MIO_UARTX_FCR(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000850ull + (((offset) & 1) * 1024))
#define CVMX_MIO_UARTX_HTX(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000B08ull + (((offset) & 1) * 1024))
#define CVMX_MIO_UARTX_IER(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000808ull + (((offset) & 1) * 1024))
#define CVMX_MIO_UARTX_IIR(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000810ull + (((offset) & 1) * 1024))
#define CVMX_MIO_UARTX_LCR(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000818ull + (((offset) & 1) * 1024))
#define CVMX_MIO_UARTX_LSR(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000828ull + (((offset) & 1) * 1024))
#define CVMX_MIO_UARTX_MCR(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000820ull + (((offset) & 1) * 1024))
#define CVMX_MIO_UARTX_MSR(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000830ull + (((offset) & 1) * 1024))
#define CVMX_MIO_UARTX_RBR(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000800ull + (((offset) & 1) * 1024))
#define CVMX_MIO_UARTX_RFL(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000A08ull + (((offset) & 1) * 1024))
#define CVMX_MIO_UARTX_RFW(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000930ull + (((offset) & 1) * 1024))
#define CVMX_MIO_UARTX_SBCR(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000A20ull + (((offset) & 1) * 1024))
#define CVMX_MIO_UARTX_SCR(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000838ull + (((offset) & 1) * 1024))
#define CVMX_MIO_UARTX_SFE(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000A30ull + (((offset) & 1) * 1024))
#define CVMX_MIO_UARTX_SRR(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000A10ull + (((offset) & 1) * 1024))
#define CVMX_MIO_UARTX_SRT(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000A38ull + (((offset) & 1) * 1024))
#define CVMX_MIO_UARTX_SRTS(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000A18ull + (((offset) & 1) * 1024))
#define CVMX_MIO_UARTX_STT(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000B00ull + (((offset) & 1) * 1024))
#define CVMX_MIO_UARTX_TFL(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000A00ull + (((offset) & 1) * 1024))
#define CVMX_MIO_UARTX_TFR(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000928ull + (((offset) & 1) * 1024))
#define CVMX_MIO_UARTX_THR(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000840ull + (((offset) & 1) * 1024))
#define CVMX_MIO_UARTX_USR(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000000938ull + (((offset) & 1) * 1024))

union cvmx_mio_boot_bist_stat {
	uint64_t u64;
	struct cvmx_mio_boot_bist_stat_s {
		uint64_t reserved_2_63:62;
		uint64_t loc:1;
		uint64_t ncbi:1;
	} s;
	struct cvmx_mio_boot_bist_stat_cn30xx {
		uint64_t reserved_4_63:60;
		uint64_t ncbo_1:1;
		uint64_t ncbo_0:1;
		uint64_t loc:1;
		uint64_t ncbi:1;
	} cn30xx;
	struct cvmx_mio_boot_bist_stat_cn30xx cn31xx;
	struct cvmx_mio_boot_bist_stat_cn38xx {
		uint64_t reserved_3_63:61;
		uint64_t ncbo_0:1;
		uint64_t loc:1;
		uint64_t ncbi:1;
	} cn38xx;
	struct cvmx_mio_boot_bist_stat_cn38xx cn38xxp2;
	struct cvmx_mio_boot_bist_stat_cn50xx {
		uint64_t reserved_6_63:58;
		uint64_t pcm_1:1;
		uint64_t pcm_0:1;
		uint64_t ncbo_1:1;
		uint64_t ncbo_0:1;
		uint64_t loc:1;
		uint64_t ncbi:1;
	} cn50xx;
	struct cvmx_mio_boot_bist_stat_cn52xx {
		uint64_t reserved_6_63:58;
		uint64_t ndf:2;
		uint64_t ncbo_0:1;
		uint64_t dma:1;
		uint64_t loc:1;
		uint64_t ncbi:1;
	} cn52xx;
	struct cvmx_mio_boot_bist_stat_cn52xxp1 {
		uint64_t reserved_4_63:60;
		uint64_t ncbo_0:1;
		uint64_t dma:1;
		uint64_t loc:1;
		uint64_t ncbi:1;
	} cn52xxp1;
	struct cvmx_mio_boot_bist_stat_cn52xxp1 cn56xx;
	struct cvmx_mio_boot_bist_stat_cn52xxp1 cn56xxp1;
	struct cvmx_mio_boot_bist_stat_cn38xx cn58xx;
	struct cvmx_mio_boot_bist_stat_cn38xx cn58xxp1;
};

union cvmx_mio_boot_comp {
	uint64_t u64;
	struct cvmx_mio_boot_comp_s {
		uint64_t reserved_10_63:54;
		uint64_t pctl:5;
		uint64_t nctl:5;
	} s;
	struct cvmx_mio_boot_comp_s cn50xx;
	struct cvmx_mio_boot_comp_s cn52xx;
	struct cvmx_mio_boot_comp_s cn52xxp1;
	struct cvmx_mio_boot_comp_s cn56xx;
	struct cvmx_mio_boot_comp_s cn56xxp1;
};

union cvmx_mio_boot_dma_cfgx {
	uint64_t u64;
	struct cvmx_mio_boot_dma_cfgx_s {
		uint64_t en:1;
		uint64_t rw:1;
		uint64_t clr:1;
		uint64_t reserved_60_60:1;
		uint64_t swap32:1;
		uint64_t swap16:1;
		uint64_t swap8:1;
		uint64_t endian:1;
		uint64_t size:20;
		uint64_t adr:36;
	} s;
	struct cvmx_mio_boot_dma_cfgx_s cn52xx;
	struct cvmx_mio_boot_dma_cfgx_s cn52xxp1;
	struct cvmx_mio_boot_dma_cfgx_s cn56xx;
	struct cvmx_mio_boot_dma_cfgx_s cn56xxp1;
};

union cvmx_mio_boot_dma_intx {
	uint64_t u64;
	struct cvmx_mio_boot_dma_intx_s {
		uint64_t reserved_2_63:62;
		uint64_t dmarq:1;
		uint64_t done:1;
	} s;
	struct cvmx_mio_boot_dma_intx_s cn52xx;
	struct cvmx_mio_boot_dma_intx_s cn52xxp1;
	struct cvmx_mio_boot_dma_intx_s cn56xx;
	struct cvmx_mio_boot_dma_intx_s cn56xxp1;
};

union cvmx_mio_boot_dma_int_enx {
	uint64_t u64;
	struct cvmx_mio_boot_dma_int_enx_s {
		uint64_t reserved_2_63:62;
		uint64_t dmarq:1;
		uint64_t done:1;
	} s;
	struct cvmx_mio_boot_dma_int_enx_s cn52xx;
	struct cvmx_mio_boot_dma_int_enx_s cn52xxp1;
	struct cvmx_mio_boot_dma_int_enx_s cn56xx;
	struct cvmx_mio_boot_dma_int_enx_s cn56xxp1;
};

union cvmx_mio_boot_dma_timx {
	uint64_t u64;
	struct cvmx_mio_boot_dma_timx_s {
		uint64_t dmack_pi:1;
		uint64_t dmarq_pi:1;
		uint64_t tim_mult:2;
		uint64_t rd_dly:3;
		uint64_t ddr:1;
		uint64_t width:1;
		uint64_t reserved_48_54:7;
		uint64_t pause:6;
		uint64_t dmack_h:6;
		uint64_t we_n:6;
		uint64_t we_a:6;
		uint64_t oe_n:6;
		uint64_t oe_a:6;
		uint64_t dmack_s:6;
		uint64_t dmarq:6;
	} s;
	struct cvmx_mio_boot_dma_timx_s cn52xx;
	struct cvmx_mio_boot_dma_timx_s cn52xxp1;
	struct cvmx_mio_boot_dma_timx_s cn56xx;
	struct cvmx_mio_boot_dma_timx_s cn56xxp1;
};

union cvmx_mio_boot_err {
	uint64_t u64;
	struct cvmx_mio_boot_err_s {
		uint64_t reserved_2_63:62;
		uint64_t wait_err:1;
		uint64_t adr_err:1;
	} s;
	struct cvmx_mio_boot_err_s cn30xx;
	struct cvmx_mio_boot_err_s cn31xx;
	struct cvmx_mio_boot_err_s cn38xx;
	struct cvmx_mio_boot_err_s cn38xxp2;
	struct cvmx_mio_boot_err_s cn50xx;
	struct cvmx_mio_boot_err_s cn52xx;
	struct cvmx_mio_boot_err_s cn52xxp1;
	struct cvmx_mio_boot_err_s cn56xx;
	struct cvmx_mio_boot_err_s cn56xxp1;
	struct cvmx_mio_boot_err_s cn58xx;
	struct cvmx_mio_boot_err_s cn58xxp1;
};

union cvmx_mio_boot_int {
	uint64_t u64;
	struct cvmx_mio_boot_int_s {
		uint64_t reserved_2_63:62;
		uint64_t wait_int:1;
		uint64_t adr_int:1;
	} s;
	struct cvmx_mio_boot_int_s cn30xx;
	struct cvmx_mio_boot_int_s cn31xx;
	struct cvmx_mio_boot_int_s cn38xx;
	struct cvmx_mio_boot_int_s cn38xxp2;
	struct cvmx_mio_boot_int_s cn50xx;
	struct cvmx_mio_boot_int_s cn52xx;
	struct cvmx_mio_boot_int_s cn52xxp1;
	struct cvmx_mio_boot_int_s cn56xx;
	struct cvmx_mio_boot_int_s cn56xxp1;
	struct cvmx_mio_boot_int_s cn58xx;
	struct cvmx_mio_boot_int_s cn58xxp1;
};

union cvmx_mio_boot_loc_adr {
	uint64_t u64;
	struct cvmx_mio_boot_loc_adr_s {
		uint64_t reserved_8_63:56;
		uint64_t adr:5;
		uint64_t reserved_0_2:3;
	} s;
	struct cvmx_mio_boot_loc_adr_s cn30xx;
	struct cvmx_mio_boot_loc_adr_s cn31xx;
	struct cvmx_mio_boot_loc_adr_s cn38xx;
	struct cvmx_mio_boot_loc_adr_s cn38xxp2;
	struct cvmx_mio_boot_loc_adr_s cn50xx;
	struct cvmx_mio_boot_loc_adr_s cn52xx;
	struct cvmx_mio_boot_loc_adr_s cn52xxp1;
	struct cvmx_mio_boot_loc_adr_s cn56xx;
	struct cvmx_mio_boot_loc_adr_s cn56xxp1;
	struct cvmx_mio_boot_loc_adr_s cn58xx;
	struct cvmx_mio_boot_loc_adr_s cn58xxp1;
};

union cvmx_mio_boot_loc_cfgx {
	uint64_t u64;
	struct cvmx_mio_boot_loc_cfgx_s {
		uint64_t reserved_32_63:32;
		uint64_t en:1;
		uint64_t reserved_28_30:3;
		uint64_t base:25;
		uint64_t reserved_0_2:3;
	} s;
	struct cvmx_mio_boot_loc_cfgx_s cn30xx;
	struct cvmx_mio_boot_loc_cfgx_s cn31xx;
	struct cvmx_mio_boot_loc_cfgx_s cn38xx;
	struct cvmx_mio_boot_loc_cfgx_s cn38xxp2;
	struct cvmx_mio_boot_loc_cfgx_s cn50xx;
	struct cvmx_mio_boot_loc_cfgx_s cn52xx;
	struct cvmx_mio_boot_loc_cfgx_s cn52xxp1;
	struct cvmx_mio_boot_loc_cfgx_s cn56xx;
	struct cvmx_mio_boot_loc_cfgx_s cn56xxp1;
	struct cvmx_mio_boot_loc_cfgx_s cn58xx;
	struct cvmx_mio_boot_loc_cfgx_s cn58xxp1;
};

union cvmx_mio_boot_loc_dat {
	uint64_t u64;
	struct cvmx_mio_boot_loc_dat_s {
		uint64_t data:64;
	} s;
	struct cvmx_mio_boot_loc_dat_s cn30xx;
	struct cvmx_mio_boot_loc_dat_s cn31xx;
	struct cvmx_mio_boot_loc_dat_s cn38xx;
	struct cvmx_mio_boot_loc_dat_s cn38xxp2;
	struct cvmx_mio_boot_loc_dat_s cn50xx;
	struct cvmx_mio_boot_loc_dat_s cn52xx;
	struct cvmx_mio_boot_loc_dat_s cn52xxp1;
	struct cvmx_mio_boot_loc_dat_s cn56xx;
	struct cvmx_mio_boot_loc_dat_s cn56xxp1;
	struct cvmx_mio_boot_loc_dat_s cn58xx;
	struct cvmx_mio_boot_loc_dat_s cn58xxp1;
};

union cvmx_mio_boot_pin_defs {
	uint64_t u64;
	struct cvmx_mio_boot_pin_defs_s {
		uint64_t reserved_16_63:48;
		uint64_t ale:1;
		uint64_t width:1;
		uint64_t dmack_p2:1;
		uint64_t dmack_p1:1;
		uint64_t dmack_p0:1;
		uint64_t term:2;
		uint64_t nand:1;
		uint64_t reserved_0_7:8;
	} s;
	struct cvmx_mio_boot_pin_defs_cn52xx {
		uint64_t reserved_16_63:48;
		uint64_t ale:1;
		uint64_t width:1;
		uint64_t reserved_13_13:1;
		uint64_t dmack_p1:1;
		uint64_t dmack_p0:1;
		uint64_t term:2;
		uint64_t nand:1;
		uint64_t reserved_0_7:8;
	} cn52xx;
	struct cvmx_mio_boot_pin_defs_cn56xx {
		uint64_t reserved_16_63:48;
		uint64_t ale:1;
		uint64_t width:1;
		uint64_t dmack_p2:1;
		uint64_t dmack_p1:1;
		uint64_t dmack_p0:1;
		uint64_t term:2;
		uint64_t reserved_0_8:9;
	} cn56xx;
};

union cvmx_mio_boot_reg_cfgx {
	uint64_t u64;
	struct cvmx_mio_boot_reg_cfgx_s {
		uint64_t reserved_44_63:20;
		uint64_t dmack:2;
		uint64_t tim_mult:2;
		uint64_t rd_dly:3;
		uint64_t sam:1;
		uint64_t we_ext:2;
		uint64_t oe_ext:2;
		uint64_t en:1;
		uint64_t orbit:1;
		uint64_t ale:1;
		uint64_t width:1;
		uint64_t size:12;
		uint64_t base:16;
	} s;
	struct cvmx_mio_boot_reg_cfgx_cn30xx {
		uint64_t reserved_37_63:27;
		uint64_t sam:1;
		uint64_t we_ext:2;
		uint64_t oe_ext:2;
		uint64_t en:1;
		uint64_t orbit:1;
		uint64_t ale:1;
		uint64_t width:1;
		uint64_t size:12;
		uint64_t base:16;
	} cn30xx;
	struct cvmx_mio_boot_reg_cfgx_cn30xx cn31xx;
	struct cvmx_mio_boot_reg_cfgx_cn38xx {
		uint64_t reserved_32_63:32;
		uint64_t en:1;
		uint64_t orbit:1;
		uint64_t reserved_28_29:2;
		uint64_t size:12;
		uint64_t base:16;
	} cn38xx;
	struct cvmx_mio_boot_reg_cfgx_cn38xx cn38xxp2;
	struct cvmx_mio_boot_reg_cfgx_cn50xx {
		uint64_t reserved_42_63:22;
		uint64_t tim_mult:2;
		uint64_t rd_dly:3;
		uint64_t sam:1;
		uint64_t we_ext:2;
		uint64_t oe_ext:2;
		uint64_t en:1;
		uint64_t orbit:1;
		uint64_t ale:1;
		uint64_t width:1;
		uint64_t size:12;
		uint64_t base:16;
	} cn50xx;
	struct cvmx_mio_boot_reg_cfgx_s cn52xx;
	struct cvmx_mio_boot_reg_cfgx_s cn52xxp1;
	struct cvmx_mio_boot_reg_cfgx_s cn56xx;
	struct cvmx_mio_boot_reg_cfgx_s cn56xxp1;
	struct cvmx_mio_boot_reg_cfgx_cn30xx cn58xx;
	struct cvmx_mio_boot_reg_cfgx_cn30xx cn58xxp1;
};

union cvmx_mio_boot_reg_timx {
	uint64_t u64;
	struct cvmx_mio_boot_reg_timx_s {
		uint64_t pagem:1;
		uint64_t waitm:1;
		uint64_t pages:2;
		uint64_t ale:6;
		uint64_t page:6;
		uint64_t wait:6;
		uint64_t pause:6;
		uint64_t wr_hld:6;
		uint64_t rd_hld:6;
		uint64_t we:6;
		uint64_t oe:6;
		uint64_t ce:6;
		uint64_t adr:6;
	} s;
	struct cvmx_mio_boot_reg_timx_s cn30xx;
	struct cvmx_mio_boot_reg_timx_s cn31xx;
	struct cvmx_mio_boot_reg_timx_cn38xx {
		uint64_t pagem:1;
		uint64_t waitm:1;
		uint64_t pages:2;
		uint64_t reserved_54_59:6;
		uint64_t page:6;
		uint64_t wait:6;
		uint64_t pause:6;
		uint64_t wr_hld:6;
		uint64_t rd_hld:6;
		uint64_t we:6;
		uint64_t oe:6;
		uint64_t ce:6;
		uint64_t adr:6;
	} cn38xx;
	struct cvmx_mio_boot_reg_timx_cn38xx cn38xxp2;
	struct cvmx_mio_boot_reg_timx_s cn50xx;
	struct cvmx_mio_boot_reg_timx_s cn52xx;
	struct cvmx_mio_boot_reg_timx_s cn52xxp1;
	struct cvmx_mio_boot_reg_timx_s cn56xx;
	struct cvmx_mio_boot_reg_timx_s cn56xxp1;
	struct cvmx_mio_boot_reg_timx_s cn58xx;
	struct cvmx_mio_boot_reg_timx_s cn58xxp1;
};

union cvmx_mio_boot_thr {
	uint64_t u64;
	struct cvmx_mio_boot_thr_s {
		uint64_t reserved_22_63:42;
		uint64_t dma_thr:6;
		uint64_t reserved_14_15:2;
		uint64_t fif_cnt:6;
		uint64_t reserved_6_7:2;
		uint64_t fif_thr:6;
	} s;
	struct cvmx_mio_boot_thr_cn30xx {
		uint64_t reserved_14_63:50;
		uint64_t fif_cnt:6;
		uint64_t reserved_6_7:2;
		uint64_t fif_thr:6;
	} cn30xx;
	struct cvmx_mio_boot_thr_cn30xx cn31xx;
	struct cvmx_mio_boot_thr_cn30xx cn38xx;
	struct cvmx_mio_boot_thr_cn30xx cn38xxp2;
	struct cvmx_mio_boot_thr_cn30xx cn50xx;
	struct cvmx_mio_boot_thr_s cn52xx;
	struct cvmx_mio_boot_thr_s cn52xxp1;
	struct cvmx_mio_boot_thr_s cn56xx;
	struct cvmx_mio_boot_thr_s cn56xxp1;
	struct cvmx_mio_boot_thr_cn30xx cn58xx;
	struct cvmx_mio_boot_thr_cn30xx cn58xxp1;
};

union cvmx_mio_fus_bnk_datx {
	uint64_t u64;
	struct cvmx_mio_fus_bnk_datx_s {
		uint64_t dat:64;
	} s;
	struct cvmx_mio_fus_bnk_datx_s cn50xx;
	struct cvmx_mio_fus_bnk_datx_s cn52xx;
	struct cvmx_mio_fus_bnk_datx_s cn52xxp1;
	struct cvmx_mio_fus_bnk_datx_s cn56xx;
	struct cvmx_mio_fus_bnk_datx_s cn56xxp1;
	struct cvmx_mio_fus_bnk_datx_s cn58xx;
	struct cvmx_mio_fus_bnk_datx_s cn58xxp1;
};

union cvmx_mio_fus_dat0 {
	uint64_t u64;
	struct cvmx_mio_fus_dat0_s {
		uint64_t reserved_32_63:32;
		uint64_t man_info:32;
	} s;
	struct cvmx_mio_fus_dat0_s cn30xx;
	struct cvmx_mio_fus_dat0_s cn31xx;
	struct cvmx_mio_fus_dat0_s cn38xx;
	struct cvmx_mio_fus_dat0_s cn38xxp2;
	struct cvmx_mio_fus_dat0_s cn50xx;
	struct cvmx_mio_fus_dat0_s cn52xx;
	struct cvmx_mio_fus_dat0_s cn52xxp1;
	struct cvmx_mio_fus_dat0_s cn56xx;
	struct cvmx_mio_fus_dat0_s cn56xxp1;
	struct cvmx_mio_fus_dat0_s cn58xx;
	struct cvmx_mio_fus_dat0_s cn58xxp1;
};

union cvmx_mio_fus_dat1 {
	uint64_t u64;
	struct cvmx_mio_fus_dat1_s {
		uint64_t reserved_32_63:32;
		uint64_t man_info:32;
	} s;
	struct cvmx_mio_fus_dat1_s cn30xx;
	struct cvmx_mio_fus_dat1_s cn31xx;
	struct cvmx_mio_fus_dat1_s cn38xx;
	struct cvmx_mio_fus_dat1_s cn38xxp2;
	struct cvmx_mio_fus_dat1_s cn50xx;
	struct cvmx_mio_fus_dat1_s cn52xx;
	struct cvmx_mio_fus_dat1_s cn52xxp1;
	struct cvmx_mio_fus_dat1_s cn56xx;
	struct cvmx_mio_fus_dat1_s cn56xxp1;
	struct cvmx_mio_fus_dat1_s cn58xx;
	struct cvmx_mio_fus_dat1_s cn58xxp1;
};

union cvmx_mio_fus_dat2 {
	uint64_t u64;
	struct cvmx_mio_fus_dat2_s {
		uint64_t reserved_34_63:30;
		uint64_t fus318:1;
		uint64_t raid_en:1;
		uint64_t reserved_30_31:2;
		uint64_t nokasu:1;
		uint64_t nodfa_cp2:1;
		uint64_t nomul:1;
		uint64_t nocrypto:1;
		uint64_t rst_sht:1;
		uint64_t bist_dis:1;
		uint64_t chip_id:8;
		uint64_t reserved_0_15:16;
	} s;
	struct cvmx_mio_fus_dat2_cn30xx {
		uint64_t reserved_29_63:35;
		uint64_t nodfa_cp2:1;
		uint64_t nomul:1;
		uint64_t nocrypto:1;
		uint64_t rst_sht:1;
		uint64_t bist_dis:1;
		uint64_t chip_id:8;
		uint64_t pll_off:4;
		uint64_t reserved_1_11:11;
		uint64_t pp_dis:1;
	} cn30xx;
	struct cvmx_mio_fus_dat2_cn31xx {
		uint64_t reserved_29_63:35;
		uint64_t nodfa_cp2:1;
		uint64_t nomul:1;
		uint64_t nocrypto:1;
		uint64_t rst_sht:1;
		uint64_t bist_dis:1;
		uint64_t chip_id:8;
		uint64_t pll_off:4;
		uint64_t reserved_2_11:10;
		uint64_t pp_dis:2;
	} cn31xx;
	struct cvmx_mio_fus_dat2_cn38xx {
		uint64_t reserved_29_63:35;
		uint64_t nodfa_cp2:1;
		uint64_t nomul:1;
		uint64_t nocrypto:1;
		uint64_t rst_sht:1;
		uint64_t bist_dis:1;
		uint64_t chip_id:8;
		uint64_t pp_dis:16;
	} cn38xx;
	struct cvmx_mio_fus_dat2_cn38xx cn38xxp2;
	struct cvmx_mio_fus_dat2_cn50xx {
		uint64_t reserved_34_63:30;
		uint64_t fus318:1;
		uint64_t raid_en:1;
		uint64_t reserved_30_31:2;
		uint64_t nokasu:1;
		uint64_t nodfa_cp2:1;
		uint64_t nomul:1;
		uint64_t nocrypto:1;
		uint64_t rst_sht:1;
		uint64_t bist_dis:1;
		uint64_t chip_id:8;
		uint64_t reserved_2_15:14;
		uint64_t pp_dis:2;
	} cn50xx;
	struct cvmx_mio_fus_dat2_cn52xx {
		uint64_t reserved_34_63:30;
		uint64_t fus318:1;
		uint64_t raid_en:1;
		uint64_t reserved_30_31:2;
		uint64_t nokasu:1;
		uint64_t nodfa_cp2:1;
		uint64_t nomul:1;
		uint64_t nocrypto:1;
		uint64_t rst_sht:1;
		uint64_t bist_dis:1;
		uint64_t chip_id:8;
		uint64_t reserved_4_15:12;
		uint64_t pp_dis:4;
	} cn52xx;
	struct cvmx_mio_fus_dat2_cn52xx cn52xxp1;
	struct cvmx_mio_fus_dat2_cn56xx {
		uint64_t reserved_34_63:30;
		uint64_t fus318:1;
		uint64_t raid_en:1;
		uint64_t reserved_30_31:2;
		uint64_t nokasu:1;
		uint64_t nodfa_cp2:1;
		uint64_t nomul:1;
		uint64_t nocrypto:1;
		uint64_t rst_sht:1;
		uint64_t bist_dis:1;
		uint64_t chip_id:8;
		uint64_t reserved_12_15:4;
		uint64_t pp_dis:12;
	} cn56xx;
	struct cvmx_mio_fus_dat2_cn56xx cn56xxp1;
	struct cvmx_mio_fus_dat2_cn58xx {
		uint64_t reserved_30_63:34;
		uint64_t nokasu:1;
		uint64_t nodfa_cp2:1;
		uint64_t nomul:1;
		uint64_t nocrypto:1;
		uint64_t rst_sht:1;
		uint64_t bist_dis:1;
		uint64_t chip_id:8;
		uint64_t pp_dis:16;
	} cn58xx;
	struct cvmx_mio_fus_dat2_cn58xx cn58xxp1;
};

union cvmx_mio_fus_dat3 {
	uint64_t u64;
	struct cvmx_mio_fus_dat3_s {
		uint64_t reserved_32_63:32;
		uint64_t pll_div4:1;
		uint64_t zip_crip:2;
		uint64_t bar2_en:1;
		uint64_t efus_lck:1;
		uint64_t efus_ign:1;
		uint64_t nozip:1;
		uint64_t nodfa_dte:1;
		uint64_t icache:24;
	} s;
	struct cvmx_mio_fus_dat3_cn30xx {
		uint64_t reserved_32_63:32;
		uint64_t pll_div4:1;
		uint64_t reserved_29_30:2;
		uint64_t bar2_en:1;
		uint64_t efus_lck:1;
		uint64_t efus_ign:1;
		uint64_t nozip:1;
		uint64_t nodfa_dte:1;
		uint64_t icache:24;
	} cn30xx;
	struct cvmx_mio_fus_dat3_s cn31xx;
	struct cvmx_mio_fus_dat3_cn38xx {
		uint64_t reserved_31_63:33;
		uint64_t zip_crip:2;
		uint64_t bar2_en:1;
		uint64_t efus_lck:1;
		uint64_t efus_ign:1;
		uint64_t nozip:1;
		uint64_t nodfa_dte:1;
		uint64_t icache:24;
	} cn38xx;
	struct cvmx_mio_fus_dat3_cn38xxp2 {
		uint64_t reserved_29_63:35;
		uint64_t bar2_en:1;
		uint64_t efus_lck:1;
		uint64_t efus_ign:1;
		uint64_t nozip:1;
		uint64_t nodfa_dte:1;
		uint64_t icache:24;
	} cn38xxp2;
	struct cvmx_mio_fus_dat3_cn38xx cn50xx;
	struct cvmx_mio_fus_dat3_cn38xx cn52xx;
	struct cvmx_mio_fus_dat3_cn38xx cn52xxp1;
	struct cvmx_mio_fus_dat3_cn38xx cn56xx;
	struct cvmx_mio_fus_dat3_cn38xx cn56xxp1;
	struct cvmx_mio_fus_dat3_cn38xx cn58xx;
	struct cvmx_mio_fus_dat3_cn38xx cn58xxp1;
};

union cvmx_mio_fus_ema {
	uint64_t u64;
	struct cvmx_mio_fus_ema_s {
		uint64_t reserved_7_63:57;
		uint64_t eff_ema:3;
		uint64_t reserved_3_3:1;
		uint64_t ema:3;
	} s;
	struct cvmx_mio_fus_ema_s cn50xx;
	struct cvmx_mio_fus_ema_s cn52xx;
	struct cvmx_mio_fus_ema_s cn52xxp1;
	struct cvmx_mio_fus_ema_s cn56xx;
	struct cvmx_mio_fus_ema_s cn56xxp1;
	struct cvmx_mio_fus_ema_cn58xx {
		uint64_t reserved_2_63:62;
		uint64_t ema:2;
	} cn58xx;
	struct cvmx_mio_fus_ema_cn58xx cn58xxp1;
};

union cvmx_mio_fus_pdf {
	uint64_t u64;
	struct cvmx_mio_fus_pdf_s {
		uint64_t pdf:64;
	} s;
	struct cvmx_mio_fus_pdf_s cn50xx;
	struct cvmx_mio_fus_pdf_s cn52xx;
	struct cvmx_mio_fus_pdf_s cn52xxp1;
	struct cvmx_mio_fus_pdf_s cn56xx;
	struct cvmx_mio_fus_pdf_s cn56xxp1;
	struct cvmx_mio_fus_pdf_s cn58xx;
};

union cvmx_mio_fus_pll {
	uint64_t u64;
	struct cvmx_mio_fus_pll_s {
		uint64_t reserved_2_63:62;
		uint64_t rfslip:1;
		uint64_t fbslip:1;
	} s;
	struct cvmx_mio_fus_pll_s cn50xx;
	struct cvmx_mio_fus_pll_s cn52xx;
	struct cvmx_mio_fus_pll_s cn52xxp1;
	struct cvmx_mio_fus_pll_s cn56xx;
	struct cvmx_mio_fus_pll_s cn56xxp1;
	struct cvmx_mio_fus_pll_s cn58xx;
	struct cvmx_mio_fus_pll_s cn58xxp1;
};

union cvmx_mio_fus_prog {
	uint64_t u64;
	struct cvmx_mio_fus_prog_s {
		uint64_t reserved_1_63:63;
		uint64_t prog:1;
	} s;
	struct cvmx_mio_fus_prog_s cn30xx;
	struct cvmx_mio_fus_prog_s cn31xx;
	struct cvmx_mio_fus_prog_s cn38xx;
	struct cvmx_mio_fus_prog_s cn38xxp2;
	struct cvmx_mio_fus_prog_s cn50xx;
	struct cvmx_mio_fus_prog_s cn52xx;
	struct cvmx_mio_fus_prog_s cn52xxp1;
	struct cvmx_mio_fus_prog_s cn56xx;
	struct cvmx_mio_fus_prog_s cn56xxp1;
	struct cvmx_mio_fus_prog_s cn58xx;
	struct cvmx_mio_fus_prog_s cn58xxp1;
};

union cvmx_mio_fus_prog_times {
	uint64_t u64;
	struct cvmx_mio_fus_prog_times_s {
		uint64_t reserved_33_63:31;
		uint64_t prog_pin:1;
		uint64_t out:8;
		uint64_t sclk_lo:4;
		uint64_t sclk_hi:12;
		uint64_t setup:8;
	} s;
	struct cvmx_mio_fus_prog_times_s cn50xx;
	struct cvmx_mio_fus_prog_times_s cn52xx;
	struct cvmx_mio_fus_prog_times_s cn52xxp1;
	struct cvmx_mio_fus_prog_times_s cn56xx;
	struct cvmx_mio_fus_prog_times_s cn56xxp1;
	struct cvmx_mio_fus_prog_times_s cn58xx;
	struct cvmx_mio_fus_prog_times_s cn58xxp1;
};

union cvmx_mio_fus_rcmd {
	uint64_t u64;
	struct cvmx_mio_fus_rcmd_s {
		uint64_t reserved_24_63:40;
		uint64_t dat:8;
		uint64_t reserved_13_15:3;
		uint64_t pend:1;
		uint64_t reserved_9_11:3;
		uint64_t efuse:1;
		uint64_t addr:8;
	} s;
	struct cvmx_mio_fus_rcmd_cn30xx {
		uint64_t reserved_24_63:40;
		uint64_t dat:8;
		uint64_t reserved_13_15:3;
		uint64_t pend:1;
		uint64_t reserved_9_11:3;
		uint64_t efuse:1;
		uint64_t reserved_7_7:1;
		uint64_t addr:7;
	} cn30xx;
	struct cvmx_mio_fus_rcmd_cn30xx cn31xx;
	struct cvmx_mio_fus_rcmd_cn30xx cn38xx;
	struct cvmx_mio_fus_rcmd_cn30xx cn38xxp2;
	struct cvmx_mio_fus_rcmd_cn30xx cn50xx;
	struct cvmx_mio_fus_rcmd_s cn52xx;
	struct cvmx_mio_fus_rcmd_s cn52xxp1;
	struct cvmx_mio_fus_rcmd_s cn56xx;
	struct cvmx_mio_fus_rcmd_s cn56xxp1;
	struct cvmx_mio_fus_rcmd_cn30xx cn58xx;
	struct cvmx_mio_fus_rcmd_cn30xx cn58xxp1;
};

union cvmx_mio_fus_spr_repair_res {
	uint64_t u64;
	struct cvmx_mio_fus_spr_repair_res_s {
		uint64_t reserved_42_63:22;
		uint64_t repair2:14;
		uint64_t repair1:14;
		uint64_t repair0:14;
	} s;
	struct cvmx_mio_fus_spr_repair_res_s cn30xx;
	struct cvmx_mio_fus_spr_repair_res_s cn31xx;
	struct cvmx_mio_fus_spr_repair_res_s cn38xx;
	struct cvmx_mio_fus_spr_repair_res_s cn50xx;
	struct cvmx_mio_fus_spr_repair_res_s cn52xx;
	struct cvmx_mio_fus_spr_repair_res_s cn52xxp1;
	struct cvmx_mio_fus_spr_repair_res_s cn56xx;
	struct cvmx_mio_fus_spr_repair_res_s cn56xxp1;
	struct cvmx_mio_fus_spr_repair_res_s cn58xx;
	struct cvmx_mio_fus_spr_repair_res_s cn58xxp1;
};

union cvmx_mio_fus_spr_repair_sum {
	uint64_t u64;
	struct cvmx_mio_fus_spr_repair_sum_s {
		uint64_t reserved_1_63:63;
		uint64_t too_many:1;
	} s;
	struct cvmx_mio_fus_spr_repair_sum_s cn30xx;
	struct cvmx_mio_fus_spr_repair_sum_s cn31xx;
	struct cvmx_mio_fus_spr_repair_sum_s cn38xx;
	struct cvmx_mio_fus_spr_repair_sum_s cn50xx;
	struct cvmx_mio_fus_spr_repair_sum_s cn52xx;
	struct cvmx_mio_fus_spr_repair_sum_s cn52xxp1;
	struct cvmx_mio_fus_spr_repair_sum_s cn56xx;
	struct cvmx_mio_fus_spr_repair_sum_s cn56xxp1;
	struct cvmx_mio_fus_spr_repair_sum_s cn58xx;
	struct cvmx_mio_fus_spr_repair_sum_s cn58xxp1;
};

union cvmx_mio_fus_unlock {
	uint64_t u64;
	struct cvmx_mio_fus_unlock_s {
		uint64_t reserved_24_63:40;
		uint64_t key:24;
	} s;
	struct cvmx_mio_fus_unlock_s cn30xx;
	struct cvmx_mio_fus_unlock_s cn31xx;
};

union cvmx_mio_fus_wadr {
	uint64_t u64;
	struct cvmx_mio_fus_wadr_s {
		uint64_t reserved_10_63:54;
		uint64_t addr:10;
	} s;
	struct cvmx_mio_fus_wadr_s cn30xx;
	struct cvmx_mio_fus_wadr_s cn31xx;
	struct cvmx_mio_fus_wadr_s cn38xx;
	struct cvmx_mio_fus_wadr_s cn38xxp2;
	struct cvmx_mio_fus_wadr_cn50xx {
		uint64_t reserved_2_63:62;
		uint64_t addr:2;
	} cn50xx;
	struct cvmx_mio_fus_wadr_cn52xx {
		uint64_t reserved_3_63:61;
		uint64_t addr:3;
	} cn52xx;
	struct cvmx_mio_fus_wadr_cn52xx cn52xxp1;
	struct cvmx_mio_fus_wadr_cn52xx cn56xx;
	struct cvmx_mio_fus_wadr_cn52xx cn56xxp1;
	struct cvmx_mio_fus_wadr_cn50xx cn58xx;
	struct cvmx_mio_fus_wadr_cn50xx cn58xxp1;
};

union cvmx_mio_ndf_dma_cfg {
	uint64_t u64;
	struct cvmx_mio_ndf_dma_cfg_s {
		uint64_t en:1;
		uint64_t rw:1;
		uint64_t clr:1;
		uint64_t reserved_60_60:1;
		uint64_t swap32:1;
		uint64_t swap16:1;
		uint64_t swap8:1;
		uint64_t endian:1;
		uint64_t size:20;
		uint64_t adr:36;
	} s;
	struct cvmx_mio_ndf_dma_cfg_s cn52xx;
};

union cvmx_mio_ndf_dma_int {
	uint64_t u64;
	struct cvmx_mio_ndf_dma_int_s {
		uint64_t reserved_1_63:63;
		uint64_t done:1;
	} s;
	struct cvmx_mio_ndf_dma_int_s cn52xx;
};

union cvmx_mio_ndf_dma_int_en {
	uint64_t u64;
	struct cvmx_mio_ndf_dma_int_en_s {
		uint64_t reserved_1_63:63;
		uint64_t done:1;
	} s;
	struct cvmx_mio_ndf_dma_int_en_s cn52xx;
};

union cvmx_mio_pll_ctl {
	uint64_t u64;
	struct cvmx_mio_pll_ctl_s {
		uint64_t reserved_5_63:59;
		uint64_t bw_ctl:5;
	} s;
	struct cvmx_mio_pll_ctl_s cn30xx;
	struct cvmx_mio_pll_ctl_s cn31xx;
};

union cvmx_mio_pll_setting {
	uint64_t u64;
	struct cvmx_mio_pll_setting_s {
		uint64_t reserved_17_63:47;
		uint64_t setting:17;
	} s;
	struct cvmx_mio_pll_setting_s cn30xx;
	struct cvmx_mio_pll_setting_s cn31xx;
};

union cvmx_mio_twsx_int {
	uint64_t u64;
	struct cvmx_mio_twsx_int_s {
		uint64_t reserved_12_63:52;
		uint64_t scl:1;
		uint64_t sda:1;
		uint64_t scl_ovr:1;
		uint64_t sda_ovr:1;
		uint64_t reserved_7_7:1;
		uint64_t core_en:1;
		uint64_t ts_en:1;
		uint64_t st_en:1;
		uint64_t reserved_3_3:1;
		uint64_t core_int:1;
		uint64_t ts_int:1;
		uint64_t st_int:1;
	} s;
	struct cvmx_mio_twsx_int_s cn30xx;
	struct cvmx_mio_twsx_int_s cn31xx;
	struct cvmx_mio_twsx_int_s cn38xx;
	struct cvmx_mio_twsx_int_cn38xxp2 {
		uint64_t reserved_7_63:57;
		uint64_t core_en:1;
		uint64_t ts_en:1;
		uint64_t st_en:1;
		uint64_t reserved_3_3:1;
		uint64_t core_int:1;
		uint64_t ts_int:1;
		uint64_t st_int:1;
	} cn38xxp2;
	struct cvmx_mio_twsx_int_s cn50xx;
	struct cvmx_mio_twsx_int_s cn52xx;
	struct cvmx_mio_twsx_int_s cn52xxp1;
	struct cvmx_mio_twsx_int_s cn56xx;
	struct cvmx_mio_twsx_int_s cn56xxp1;
	struct cvmx_mio_twsx_int_s cn58xx;
	struct cvmx_mio_twsx_int_s cn58xxp1;
};

union cvmx_mio_twsx_sw_twsi {
	uint64_t u64;
	struct cvmx_mio_twsx_sw_twsi_s {
		uint64_t v:1;
		uint64_t slonly:1;
		uint64_t eia:1;
		uint64_t op:4;
		uint64_t r:1;
		uint64_t sovr:1;
		uint64_t size:3;
		uint64_t scr:2;
		uint64_t a:10;
		uint64_t ia:5;
		uint64_t eop_ia:3;
		uint64_t d:32;
	} s;
	struct cvmx_mio_twsx_sw_twsi_s cn30xx;
	struct cvmx_mio_twsx_sw_twsi_s cn31xx;
	struct cvmx_mio_twsx_sw_twsi_s cn38xx;
	struct cvmx_mio_twsx_sw_twsi_s cn38xxp2;
	struct cvmx_mio_twsx_sw_twsi_s cn50xx;
	struct cvmx_mio_twsx_sw_twsi_s cn52xx;
	struct cvmx_mio_twsx_sw_twsi_s cn52xxp1;
	struct cvmx_mio_twsx_sw_twsi_s cn56xx;
	struct cvmx_mio_twsx_sw_twsi_s cn56xxp1;
	struct cvmx_mio_twsx_sw_twsi_s cn58xx;
	struct cvmx_mio_twsx_sw_twsi_s cn58xxp1;
};

union cvmx_mio_twsx_sw_twsi_ext {
	uint64_t u64;
	struct cvmx_mio_twsx_sw_twsi_ext_s {
		uint64_t reserved_40_63:24;
		uint64_t ia:8;
		uint64_t d:32;
	} s;
	struct cvmx_mio_twsx_sw_twsi_ext_s cn30xx;
	struct cvmx_mio_twsx_sw_twsi_ext_s cn31xx;
	struct cvmx_mio_twsx_sw_twsi_ext_s cn38xx;
	struct cvmx_mio_twsx_sw_twsi_ext_s cn38xxp2;
	struct cvmx_mio_twsx_sw_twsi_ext_s cn50xx;
	struct cvmx_mio_twsx_sw_twsi_ext_s cn52xx;
	struct cvmx_mio_twsx_sw_twsi_ext_s cn52xxp1;
	struct cvmx_mio_twsx_sw_twsi_ext_s cn56xx;
	struct cvmx_mio_twsx_sw_twsi_ext_s cn56xxp1;
	struct cvmx_mio_twsx_sw_twsi_ext_s cn58xx;
	struct cvmx_mio_twsx_sw_twsi_ext_s cn58xxp1;
};

union cvmx_mio_twsx_twsi_sw {
	uint64_t u64;
	struct cvmx_mio_twsx_twsi_sw_s {
		uint64_t v:2;
		uint64_t reserved_32_61:30;
		uint64_t d:32;
	} s;
	struct cvmx_mio_twsx_twsi_sw_s cn30xx;
	struct cvmx_mio_twsx_twsi_sw_s cn31xx;
	struct cvmx_mio_twsx_twsi_sw_s cn38xx;
	struct cvmx_mio_twsx_twsi_sw_s cn38xxp2;
	struct cvmx_mio_twsx_twsi_sw_s cn50xx;
	struct cvmx_mio_twsx_twsi_sw_s cn52xx;
	struct cvmx_mio_twsx_twsi_sw_s cn52xxp1;
	struct cvmx_mio_twsx_twsi_sw_s cn56xx;
	struct cvmx_mio_twsx_twsi_sw_s cn56xxp1;
	struct cvmx_mio_twsx_twsi_sw_s cn58xx;
	struct cvmx_mio_twsx_twsi_sw_s cn58xxp1;
};

union cvmx_mio_uartx_dlh {
	uint64_t u64;
	struct cvmx_mio_uartx_dlh_s {
		uint64_t reserved_8_63:56;
		uint64_t dlh:8;
	} s;
	struct cvmx_mio_uartx_dlh_s cn30xx;
	struct cvmx_mio_uartx_dlh_s cn31xx;
	struct cvmx_mio_uartx_dlh_s cn38xx;
	struct cvmx_mio_uartx_dlh_s cn38xxp2;
	struct cvmx_mio_uartx_dlh_s cn50xx;
	struct cvmx_mio_uartx_dlh_s cn52xx;
	struct cvmx_mio_uartx_dlh_s cn52xxp1;
	struct cvmx_mio_uartx_dlh_s cn56xx;
	struct cvmx_mio_uartx_dlh_s cn56xxp1;
	struct cvmx_mio_uartx_dlh_s cn58xx;
	struct cvmx_mio_uartx_dlh_s cn58xxp1;
};

union cvmx_mio_uartx_dll {
	uint64_t u64;
	struct cvmx_mio_uartx_dll_s {
		uint64_t reserved_8_63:56;
		uint64_t dll:8;
	} s;
	struct cvmx_mio_uartx_dll_s cn30xx;
	struct cvmx_mio_uartx_dll_s cn31xx;
	struct cvmx_mio_uartx_dll_s cn38xx;
	struct cvmx_mio_uartx_dll_s cn38xxp2;
	struct cvmx_mio_uartx_dll_s cn50xx;
	struct cvmx_mio_uartx_dll_s cn52xx;
	struct cvmx_mio_uartx_dll_s cn52xxp1;
	struct cvmx_mio_uartx_dll_s cn56xx;
	struct cvmx_mio_uartx_dll_s cn56xxp1;
	struct cvmx_mio_uartx_dll_s cn58xx;
	struct cvmx_mio_uartx_dll_s cn58xxp1;
};

union cvmx_mio_uartx_far {
	uint64_t u64;
	struct cvmx_mio_uartx_far_s {
		uint64_t reserved_1_63:63;
		uint64_t far:1;
	} s;
	struct cvmx_mio_uartx_far_s cn30xx;
	struct cvmx_mio_uartx_far_s cn31xx;
	struct cvmx_mio_uartx_far_s cn38xx;
	struct cvmx_mio_uartx_far_s cn38xxp2;
	struct cvmx_mio_uartx_far_s cn50xx;
	struct cvmx_mio_uartx_far_s cn52xx;
	struct cvmx_mio_uartx_far_s cn52xxp1;
	struct cvmx_mio_uartx_far_s cn56xx;
	struct cvmx_mio_uartx_far_s cn56xxp1;
	struct cvmx_mio_uartx_far_s cn58xx;
	struct cvmx_mio_uartx_far_s cn58xxp1;
};

union cvmx_mio_uartx_fcr {
	uint64_t u64;
	struct cvmx_mio_uartx_fcr_s {
		uint64_t reserved_8_63:56;
		uint64_t rxtrig:2;
		uint64_t txtrig:2;
		uint64_t reserved_3_3:1;
		uint64_t txfr:1;
		uint64_t rxfr:1;
		uint64_t en:1;
	} s;
	struct cvmx_mio_uartx_fcr_s cn30xx;
	struct cvmx_mio_uartx_fcr_s cn31xx;
	struct cvmx_mio_uartx_fcr_s cn38xx;
	struct cvmx_mio_uartx_fcr_s cn38xxp2;
	struct cvmx_mio_uartx_fcr_s cn50xx;
	struct cvmx_mio_uartx_fcr_s cn52xx;
	struct cvmx_mio_uartx_fcr_s cn52xxp1;
	struct cvmx_mio_uartx_fcr_s cn56xx;
	struct cvmx_mio_uartx_fcr_s cn56xxp1;
	struct cvmx_mio_uartx_fcr_s cn58xx;
	struct cvmx_mio_uartx_fcr_s cn58xxp1;
};

union cvmx_mio_uartx_htx {
	uint64_t u64;
	struct cvmx_mio_uartx_htx_s {
		uint64_t reserved_1_63:63;
		uint64_t htx:1;
	} s;
	struct cvmx_mio_uartx_htx_s cn30xx;
	struct cvmx_mio_uartx_htx_s cn31xx;
	struct cvmx_mio_uartx_htx_s cn38xx;
	struct cvmx_mio_uartx_htx_s cn38xxp2;
	struct cvmx_mio_uartx_htx_s cn50xx;
	struct cvmx_mio_uartx_htx_s cn52xx;
	struct cvmx_mio_uartx_htx_s cn52xxp1;
	struct cvmx_mio_uartx_htx_s cn56xx;
	struct cvmx_mio_uartx_htx_s cn56xxp1;
	struct cvmx_mio_uartx_htx_s cn58xx;
	struct cvmx_mio_uartx_htx_s cn58xxp1;
};

union cvmx_mio_uartx_ier {
	uint64_t u64;
	struct cvmx_mio_uartx_ier_s {
		uint64_t reserved_8_63:56;
		uint64_t ptime:1;
		uint64_t reserved_4_6:3;
		uint64_t edssi:1;
		uint64_t elsi:1;
		uint64_t etbei:1;
		uint64_t erbfi:1;
	} s;
	struct cvmx_mio_uartx_ier_s cn30xx;
	struct cvmx_mio_uartx_ier_s cn31xx;
	struct cvmx_mio_uartx_ier_s cn38xx;
	struct cvmx_mio_uartx_ier_s cn38xxp2;
	struct cvmx_mio_uartx_ier_s cn50xx;
	struct cvmx_mio_uartx_ier_s cn52xx;
	struct cvmx_mio_uartx_ier_s cn52xxp1;
	struct cvmx_mio_uartx_ier_s cn56xx;
	struct cvmx_mio_uartx_ier_s cn56xxp1;
	struct cvmx_mio_uartx_ier_s cn58xx;
	struct cvmx_mio_uartx_ier_s cn58xxp1;
};

union cvmx_mio_uartx_iir {
	uint64_t u64;
	struct cvmx_mio_uartx_iir_s {
		uint64_t reserved_8_63:56;
		uint64_t fen:2;
		uint64_t reserved_4_5:2;
		uint64_t iid:4;
	} s;
	struct cvmx_mio_uartx_iir_s cn30xx;
	struct cvmx_mio_uartx_iir_s cn31xx;
	struct cvmx_mio_uartx_iir_s cn38xx;
	struct cvmx_mio_uartx_iir_s cn38xxp2;
	struct cvmx_mio_uartx_iir_s cn50xx;
	struct cvmx_mio_uartx_iir_s cn52xx;
	struct cvmx_mio_uartx_iir_s cn52xxp1;
	struct cvmx_mio_uartx_iir_s cn56xx;
	struct cvmx_mio_uartx_iir_s cn56xxp1;
	struct cvmx_mio_uartx_iir_s cn58xx;
	struct cvmx_mio_uartx_iir_s cn58xxp1;
};

union cvmx_mio_uartx_lcr {
	uint64_t u64;
	struct cvmx_mio_uartx_lcr_s {
		uint64_t reserved_8_63:56;
		uint64_t dlab:1;
		uint64_t brk:1;
		uint64_t reserved_5_5:1;
		uint64_t eps:1;
		uint64_t pen:1;
		uint64_t stop:1;
		uint64_t cls:2;
	} s;
	struct cvmx_mio_uartx_lcr_s cn30xx;
	struct cvmx_mio_uartx_lcr_s cn31xx;
	struct cvmx_mio_uartx_lcr_s cn38xx;
	struct cvmx_mio_uartx_lcr_s cn38xxp2;
	struct cvmx_mio_uartx_lcr_s cn50xx;
	struct cvmx_mio_uartx_lcr_s cn52xx;
	struct cvmx_mio_uartx_lcr_s cn52xxp1;
	struct cvmx_mio_uartx_lcr_s cn56xx;
	struct cvmx_mio_uartx_lcr_s cn56xxp1;
	struct cvmx_mio_uartx_lcr_s cn58xx;
	struct cvmx_mio_uartx_lcr_s cn58xxp1;
};

union cvmx_mio_uartx_lsr {
	uint64_t u64;
	struct cvmx_mio_uartx_lsr_s {
		uint64_t reserved_8_63:56;
		uint64_t ferr:1;
		uint64_t temt:1;
		uint64_t thre:1;
		uint64_t bi:1;
		uint64_t fe:1;
		uint64_t pe:1;
		uint64_t oe:1;
		uint64_t dr:1;
	} s;
	struct cvmx_mio_uartx_lsr_s cn30xx;
	struct cvmx_mio_uartx_lsr_s cn31xx;
	struct cvmx_mio_uartx_lsr_s cn38xx;
	struct cvmx_mio_uartx_lsr_s cn38xxp2;
	struct cvmx_mio_uartx_lsr_s cn50xx;
	struct cvmx_mio_uartx_lsr_s cn52xx;
	struct cvmx_mio_uartx_lsr_s cn52xxp1;
	struct cvmx_mio_uartx_lsr_s cn56xx;
	struct cvmx_mio_uartx_lsr_s cn56xxp1;
	struct cvmx_mio_uartx_lsr_s cn58xx;
	struct cvmx_mio_uartx_lsr_s cn58xxp1;
};

union cvmx_mio_uartx_mcr {
	uint64_t u64;
	struct cvmx_mio_uartx_mcr_s {
		uint64_t reserved_6_63:58;
		uint64_t afce:1;
		uint64_t loop:1;
		uint64_t out2:1;
		uint64_t out1:1;
		uint64_t rts:1;
		uint64_t dtr:1;
	} s;
	struct cvmx_mio_uartx_mcr_s cn30xx;
	struct cvmx_mio_uartx_mcr_s cn31xx;
	struct cvmx_mio_uartx_mcr_s cn38xx;
	struct cvmx_mio_uartx_mcr_s cn38xxp2;
	struct cvmx_mio_uartx_mcr_s cn50xx;
	struct cvmx_mio_uartx_mcr_s cn52xx;
	struct cvmx_mio_uartx_mcr_s cn52xxp1;
	struct cvmx_mio_uartx_mcr_s cn56xx;
	struct cvmx_mio_uartx_mcr_s cn56xxp1;
	struct cvmx_mio_uartx_mcr_s cn58xx;
	struct cvmx_mio_uartx_mcr_s cn58xxp1;
};

union cvmx_mio_uartx_msr {
	uint64_t u64;
	struct cvmx_mio_uartx_msr_s {
		uint64_t reserved_8_63:56;
		uint64_t dcd:1;
		uint64_t ri:1;
		uint64_t dsr:1;
		uint64_t cts:1;
		uint64_t ddcd:1;
		uint64_t teri:1;
		uint64_t ddsr:1;
		uint64_t dcts:1;
	} s;
	struct cvmx_mio_uartx_msr_s cn30xx;
	struct cvmx_mio_uartx_msr_s cn31xx;
	struct cvmx_mio_uartx_msr_s cn38xx;
	struct cvmx_mio_uartx_msr_s cn38xxp2;
	struct cvmx_mio_uartx_msr_s cn50xx;
	struct cvmx_mio_uartx_msr_s cn52xx;
	struct cvmx_mio_uartx_msr_s cn52xxp1;
	struct cvmx_mio_uartx_msr_s cn56xx;
	struct cvmx_mio_uartx_msr_s cn56xxp1;
	struct cvmx_mio_uartx_msr_s cn58xx;
	struct cvmx_mio_uartx_msr_s cn58xxp1;
};

union cvmx_mio_uartx_rbr {
	uint64_t u64;
	struct cvmx_mio_uartx_rbr_s {
		uint64_t reserved_8_63:56;
		uint64_t rbr:8;
	} s;
	struct cvmx_mio_uartx_rbr_s cn30xx;
	struct cvmx_mio_uartx_rbr_s cn31xx;
	struct cvmx_mio_uartx_rbr_s cn38xx;
	struct cvmx_mio_uartx_rbr_s cn38xxp2;
	struct cvmx_mio_uartx_rbr_s cn50xx;
	struct cvmx_mio_uartx_rbr_s cn52xx;
	struct cvmx_mio_uartx_rbr_s cn52xxp1;
	struct cvmx_mio_uartx_rbr_s cn56xx;
	struct cvmx_mio_uartx_rbr_s cn56xxp1;
	struct cvmx_mio_uartx_rbr_s cn58xx;
	struct cvmx_mio_uartx_rbr_s cn58xxp1;
};

union cvmx_mio_uartx_rfl {
	uint64_t u64;
	struct cvmx_mio_uartx_rfl_s {
		uint64_t reserved_7_63:57;
		uint64_t rfl:7;
	} s;
	struct cvmx_mio_uartx_rfl_s cn30xx;
	struct cvmx_mio_uartx_rfl_s cn31xx;
	struct cvmx_mio_uartx_rfl_s cn38xx;
	struct cvmx_mio_uartx_rfl_s cn38xxp2;
	struct cvmx_mio_uartx_rfl_s cn50xx;
	struct cvmx_mio_uartx_rfl_s cn52xx;
	struct cvmx_mio_uartx_rfl_s cn52xxp1;
	struct cvmx_mio_uartx_rfl_s cn56xx;
	struct cvmx_mio_uartx_rfl_s cn56xxp1;
	struct cvmx_mio_uartx_rfl_s cn58xx;
	struct cvmx_mio_uartx_rfl_s cn58xxp1;
};

union cvmx_mio_uartx_rfw {
	uint64_t u64;
	struct cvmx_mio_uartx_rfw_s {
		uint64_t reserved_10_63:54;
		uint64_t rffe:1;
		uint64_t rfpe:1;
		uint64_t rfwd:8;
	} s;
	struct cvmx_mio_uartx_rfw_s cn30xx;
	struct cvmx_mio_uartx_rfw_s cn31xx;
	struct cvmx_mio_uartx_rfw_s cn38xx;
	struct cvmx_mio_uartx_rfw_s cn38xxp2;
	struct cvmx_mio_uartx_rfw_s cn50xx;
	struct cvmx_mio_uartx_rfw_s cn52xx;
	struct cvmx_mio_uartx_rfw_s cn52xxp1;
	struct cvmx_mio_uartx_rfw_s cn56xx;
	struct cvmx_mio_uartx_rfw_s cn56xxp1;
	struct cvmx_mio_uartx_rfw_s cn58xx;
	struct cvmx_mio_uartx_rfw_s cn58xxp1;
};

union cvmx_mio_uartx_sbcr {
	uint64_t u64;
	struct cvmx_mio_uartx_sbcr_s {
		uint64_t reserved_1_63:63;
		uint64_t sbcr:1;
	} s;
	struct cvmx_mio_uartx_sbcr_s cn30xx;
	struct cvmx_mio_uartx_sbcr_s cn31xx;
	struct cvmx_mio_uartx_sbcr_s cn38xx;
	struct cvmx_mio_uartx_sbcr_s cn38xxp2;
	struct cvmx_mio_uartx_sbcr_s cn50xx;
	struct cvmx_mio_uartx_sbcr_s cn52xx;
	struct cvmx_mio_uartx_sbcr_s cn52xxp1;
	struct cvmx_mio_uartx_sbcr_s cn56xx;
	struct cvmx_mio_uartx_sbcr_s cn56xxp1;
	struct cvmx_mio_uartx_sbcr_s cn58xx;
	struct cvmx_mio_uartx_sbcr_s cn58xxp1;
};

union cvmx_mio_uartx_scr {
	uint64_t u64;
	struct cvmx_mio_uartx_scr_s {
		uint64_t reserved_8_63:56;
		uint64_t scr:8;
	} s;
	struct cvmx_mio_uartx_scr_s cn30xx;
	struct cvmx_mio_uartx_scr_s cn31xx;
	struct cvmx_mio_uartx_scr_s cn38xx;
	struct cvmx_mio_uartx_scr_s cn38xxp2;
	struct cvmx_mio_uartx_scr_s cn50xx;
	struct cvmx_mio_uartx_scr_s cn52xx;
	struct cvmx_mio_uartx_scr_s cn52xxp1;
	struct cvmx_mio_uartx_scr_s cn56xx;
	struct cvmx_mio_uartx_scr_s cn56xxp1;
	struct cvmx_mio_uartx_scr_s cn58xx;
	struct cvmx_mio_uartx_scr_s cn58xxp1;
};

union cvmx_mio_uartx_sfe {
	uint64_t u64;
	struct cvmx_mio_uartx_sfe_s {
		uint64_t reserved_1_63:63;
		uint64_t sfe:1;
	} s;
	struct cvmx_mio_uartx_sfe_s cn30xx;
	struct cvmx_mio_uartx_sfe_s cn31xx;
	struct cvmx_mio_uartx_sfe_s cn38xx;
	struct cvmx_mio_uartx_sfe_s cn38xxp2;
	struct cvmx_mio_uartx_sfe_s cn50xx;
	struct cvmx_mio_uartx_sfe_s cn52xx;
	struct cvmx_mio_uartx_sfe_s cn52xxp1;
	struct cvmx_mio_uartx_sfe_s cn56xx;
	struct cvmx_mio_uartx_sfe_s cn56xxp1;
	struct cvmx_mio_uartx_sfe_s cn58xx;
	struct cvmx_mio_uartx_sfe_s cn58xxp1;
};

union cvmx_mio_uartx_srr {
	uint64_t u64;
	struct cvmx_mio_uartx_srr_s {
		uint64_t reserved_3_63:61;
		uint64_t stfr:1;
		uint64_t srfr:1;
		uint64_t usr:1;
	} s;
	struct cvmx_mio_uartx_srr_s cn30xx;
	struct cvmx_mio_uartx_srr_s cn31xx;
	struct cvmx_mio_uartx_srr_s cn38xx;
	struct cvmx_mio_uartx_srr_s cn38xxp2;
	struct cvmx_mio_uartx_srr_s cn50xx;
	struct cvmx_mio_uartx_srr_s cn52xx;
	struct cvmx_mio_uartx_srr_s cn52xxp1;
	struct cvmx_mio_uartx_srr_s cn56xx;
	struct cvmx_mio_uartx_srr_s cn56xxp1;
	struct cvmx_mio_uartx_srr_s cn58xx;
	struct cvmx_mio_uartx_srr_s cn58xxp1;
};

union cvmx_mio_uartx_srt {
	uint64_t u64;
	struct cvmx_mio_uartx_srt_s {
		uint64_t reserved_2_63:62;
		uint64_t srt:2;
	} s;
	struct cvmx_mio_uartx_srt_s cn30xx;
	struct cvmx_mio_uartx_srt_s cn31xx;
	struct cvmx_mio_uartx_srt_s cn38xx;
	struct cvmx_mio_uartx_srt_s cn38xxp2;
	struct cvmx_mio_uartx_srt_s cn50xx;
	struct cvmx_mio_uartx_srt_s cn52xx;
	struct cvmx_mio_uartx_srt_s cn52xxp1;
	struct cvmx_mio_uartx_srt_s cn56xx;
	struct cvmx_mio_uartx_srt_s cn56xxp1;
	struct cvmx_mio_uartx_srt_s cn58xx;
	struct cvmx_mio_uartx_srt_s cn58xxp1;
};

union cvmx_mio_uartx_srts {
	uint64_t u64;
	struct cvmx_mio_uartx_srts_s {
		uint64_t reserved_1_63:63;
		uint64_t srts:1;
	} s;
	struct cvmx_mio_uartx_srts_s cn30xx;
	struct cvmx_mio_uartx_srts_s cn31xx;
	struct cvmx_mio_uartx_srts_s cn38xx;
	struct cvmx_mio_uartx_srts_s cn38xxp2;
	struct cvmx_mio_uartx_srts_s cn50xx;
	struct cvmx_mio_uartx_srts_s cn52xx;
	struct cvmx_mio_uartx_srts_s cn52xxp1;
	struct cvmx_mio_uartx_srts_s cn56xx;
	struct cvmx_mio_uartx_srts_s cn56xxp1;
	struct cvmx_mio_uartx_srts_s cn58xx;
	struct cvmx_mio_uartx_srts_s cn58xxp1;
};

union cvmx_mio_uartx_stt {
	uint64_t u64;
	struct cvmx_mio_uartx_stt_s {
		uint64_t reserved_2_63:62;
		uint64_t stt:2;
	} s;
	struct cvmx_mio_uartx_stt_s cn30xx;
	struct cvmx_mio_uartx_stt_s cn31xx;
	struct cvmx_mio_uartx_stt_s cn38xx;
	struct cvmx_mio_uartx_stt_s cn38xxp2;
	struct cvmx_mio_uartx_stt_s cn50xx;
	struct cvmx_mio_uartx_stt_s cn52xx;
	struct cvmx_mio_uartx_stt_s cn52xxp1;
	struct cvmx_mio_uartx_stt_s cn56xx;
	struct cvmx_mio_uartx_stt_s cn56xxp1;
	struct cvmx_mio_uartx_stt_s cn58xx;
	struct cvmx_mio_uartx_stt_s cn58xxp1;
};

union cvmx_mio_uartx_tfl {
	uint64_t u64;
	struct cvmx_mio_uartx_tfl_s {
		uint64_t reserved_7_63:57;
		uint64_t tfl:7;
	} s;
	struct cvmx_mio_uartx_tfl_s cn30xx;
	struct cvmx_mio_uartx_tfl_s cn31xx;
	struct cvmx_mio_uartx_tfl_s cn38xx;
	struct cvmx_mio_uartx_tfl_s cn38xxp2;
	struct cvmx_mio_uartx_tfl_s cn50xx;
	struct cvmx_mio_uartx_tfl_s cn52xx;
	struct cvmx_mio_uartx_tfl_s cn52xxp1;
	struct cvmx_mio_uartx_tfl_s cn56xx;
	struct cvmx_mio_uartx_tfl_s cn56xxp1;
	struct cvmx_mio_uartx_tfl_s cn58xx;
	struct cvmx_mio_uartx_tfl_s cn58xxp1;
};

union cvmx_mio_uartx_tfr {
	uint64_t u64;
	struct cvmx_mio_uartx_tfr_s {
		uint64_t reserved_8_63:56;
		uint64_t tfr:8;
	} s;
	struct cvmx_mio_uartx_tfr_s cn30xx;
	struct cvmx_mio_uartx_tfr_s cn31xx;
	struct cvmx_mio_uartx_tfr_s cn38xx;
	struct cvmx_mio_uartx_tfr_s cn38xxp2;
	struct cvmx_mio_uartx_tfr_s cn50xx;
	struct cvmx_mio_uartx_tfr_s cn52xx;
	struct cvmx_mio_uartx_tfr_s cn52xxp1;
	struct cvmx_mio_uartx_tfr_s cn56xx;
	struct cvmx_mio_uartx_tfr_s cn56xxp1;
	struct cvmx_mio_uartx_tfr_s cn58xx;
	struct cvmx_mio_uartx_tfr_s cn58xxp1;
};

union cvmx_mio_uartx_thr {
	uint64_t u64;
	struct cvmx_mio_uartx_thr_s {
		uint64_t reserved_8_63:56;
		uint64_t thr:8;
	} s;
	struct cvmx_mio_uartx_thr_s cn30xx;
	struct cvmx_mio_uartx_thr_s cn31xx;
	struct cvmx_mio_uartx_thr_s cn38xx;
	struct cvmx_mio_uartx_thr_s cn38xxp2;
	struct cvmx_mio_uartx_thr_s cn50xx;
	struct cvmx_mio_uartx_thr_s cn52xx;
	struct cvmx_mio_uartx_thr_s cn52xxp1;
	struct cvmx_mio_uartx_thr_s cn56xx;
	struct cvmx_mio_uartx_thr_s cn56xxp1;
	struct cvmx_mio_uartx_thr_s cn58xx;
	struct cvmx_mio_uartx_thr_s cn58xxp1;
};

union cvmx_mio_uartx_usr {
	uint64_t u64;
	struct cvmx_mio_uartx_usr_s {
		uint64_t reserved_5_63:59;
		uint64_t rff:1;
		uint64_t rfne:1;
		uint64_t tfe:1;
		uint64_t tfnf:1;
		uint64_t busy:1;
	} s;
	struct cvmx_mio_uartx_usr_s cn30xx;
	struct cvmx_mio_uartx_usr_s cn31xx;
	struct cvmx_mio_uartx_usr_s cn38xx;
	struct cvmx_mio_uartx_usr_s cn38xxp2;
	struct cvmx_mio_uartx_usr_s cn50xx;
	struct cvmx_mio_uartx_usr_s cn52xx;
	struct cvmx_mio_uartx_usr_s cn52xxp1;
	struct cvmx_mio_uartx_usr_s cn56xx;
	struct cvmx_mio_uartx_usr_s cn56xxp1;
	struct cvmx_mio_uartx_usr_s cn58xx;
	struct cvmx_mio_uartx_usr_s cn58xxp1;
};

union cvmx_mio_uart2_dlh {
	uint64_t u64;
	struct cvmx_mio_uart2_dlh_s {
		uint64_t reserved_8_63:56;
		uint64_t dlh:8;
	} s;
	struct cvmx_mio_uart2_dlh_s cn52xx;
	struct cvmx_mio_uart2_dlh_s cn52xxp1;
};

union cvmx_mio_uart2_dll {
	uint64_t u64;
	struct cvmx_mio_uart2_dll_s {
		uint64_t reserved_8_63:56;
		uint64_t dll:8;
	} s;
	struct cvmx_mio_uart2_dll_s cn52xx;
	struct cvmx_mio_uart2_dll_s cn52xxp1;
};

union cvmx_mio_uart2_far {
	uint64_t u64;
	struct cvmx_mio_uart2_far_s {
		uint64_t reserved_1_63:63;
		uint64_t far:1;
	} s;
	struct cvmx_mio_uart2_far_s cn52xx;
	struct cvmx_mio_uart2_far_s cn52xxp1;
};

union cvmx_mio_uart2_fcr {
	uint64_t u64;
	struct cvmx_mio_uart2_fcr_s {
		uint64_t reserved_8_63:56;
		uint64_t rxtrig:2;
		uint64_t txtrig:2;
		uint64_t reserved_3_3:1;
		uint64_t txfr:1;
		uint64_t rxfr:1;
		uint64_t en:1;
	} s;
	struct cvmx_mio_uart2_fcr_s cn52xx;
	struct cvmx_mio_uart2_fcr_s cn52xxp1;
};

union cvmx_mio_uart2_htx {
	uint64_t u64;
	struct cvmx_mio_uart2_htx_s {
		uint64_t reserved_1_63:63;
		uint64_t htx:1;
	} s;
	struct cvmx_mio_uart2_htx_s cn52xx;
	struct cvmx_mio_uart2_htx_s cn52xxp1;
};

union cvmx_mio_uart2_ier {
	uint64_t u64;
	struct cvmx_mio_uart2_ier_s {
		uint64_t reserved_8_63:56;
		uint64_t ptime:1;
		uint64_t reserved_4_6:3;
		uint64_t edssi:1;
		uint64_t elsi:1;
		uint64_t etbei:1;
		uint64_t erbfi:1;
	} s;
	struct cvmx_mio_uart2_ier_s cn52xx;
	struct cvmx_mio_uart2_ier_s cn52xxp1;
};

union cvmx_mio_uart2_iir {
	uint64_t u64;
	struct cvmx_mio_uart2_iir_s {
		uint64_t reserved_8_63:56;
		uint64_t fen:2;
		uint64_t reserved_4_5:2;
		uint64_t iid:4;
	} s;
	struct cvmx_mio_uart2_iir_s cn52xx;
	struct cvmx_mio_uart2_iir_s cn52xxp1;
};

union cvmx_mio_uart2_lcr {
	uint64_t u64;
	struct cvmx_mio_uart2_lcr_s {
		uint64_t reserved_8_63:56;
		uint64_t dlab:1;
		uint64_t brk:1;
		uint64_t reserved_5_5:1;
		uint64_t eps:1;
		uint64_t pen:1;
		uint64_t stop:1;
		uint64_t cls:2;
	} s;
	struct cvmx_mio_uart2_lcr_s cn52xx;
	struct cvmx_mio_uart2_lcr_s cn52xxp1;
};

union cvmx_mio_uart2_lsr {
	uint64_t u64;
	struct cvmx_mio_uart2_lsr_s {
		uint64_t reserved_8_63:56;
		uint64_t ferr:1;
		uint64_t temt:1;
		uint64_t thre:1;
		uint64_t bi:1;
		uint64_t fe:1;
		uint64_t pe:1;
		uint64_t oe:1;
		uint64_t dr:1;
	} s;
	struct cvmx_mio_uart2_lsr_s cn52xx;
	struct cvmx_mio_uart2_lsr_s cn52xxp1;
};

union cvmx_mio_uart2_mcr {
	uint64_t u64;
	struct cvmx_mio_uart2_mcr_s {
		uint64_t reserved_6_63:58;
		uint64_t afce:1;
		uint64_t loop:1;
		uint64_t out2:1;
		uint64_t out1:1;
		uint64_t rts:1;
		uint64_t dtr:1;
	} s;
	struct cvmx_mio_uart2_mcr_s cn52xx;
	struct cvmx_mio_uart2_mcr_s cn52xxp1;
};

union cvmx_mio_uart2_msr {
	uint64_t u64;
	struct cvmx_mio_uart2_msr_s {
		uint64_t reserved_8_63:56;
		uint64_t dcd:1;
		uint64_t ri:1;
		uint64_t dsr:1;
		uint64_t cts:1;
		uint64_t ddcd:1;
		uint64_t teri:1;
		uint64_t ddsr:1;
		uint64_t dcts:1;
	} s;
	struct cvmx_mio_uart2_msr_s cn52xx;
	struct cvmx_mio_uart2_msr_s cn52xxp1;
};

union cvmx_mio_uart2_rbr {
	uint64_t u64;
	struct cvmx_mio_uart2_rbr_s {
		uint64_t reserved_8_63:56;
		uint64_t rbr:8;
	} s;
	struct cvmx_mio_uart2_rbr_s cn52xx;
	struct cvmx_mio_uart2_rbr_s cn52xxp1;
};

union cvmx_mio_uart2_rfl {
	uint64_t u64;
	struct cvmx_mio_uart2_rfl_s {
		uint64_t reserved_7_63:57;
		uint64_t rfl:7;
	} s;
	struct cvmx_mio_uart2_rfl_s cn52xx;
	struct cvmx_mio_uart2_rfl_s cn52xxp1;
};

union cvmx_mio_uart2_rfw {
	uint64_t u64;
	struct cvmx_mio_uart2_rfw_s {
		uint64_t reserved_10_63:54;
		uint64_t rffe:1;
		uint64_t rfpe:1;
		uint64_t rfwd:8;
	} s;
	struct cvmx_mio_uart2_rfw_s cn52xx;
	struct cvmx_mio_uart2_rfw_s cn52xxp1;
};

union cvmx_mio_uart2_sbcr {
	uint64_t u64;
	struct cvmx_mio_uart2_sbcr_s {
		uint64_t reserved_1_63:63;
		uint64_t sbcr:1;
	} s;
	struct cvmx_mio_uart2_sbcr_s cn52xx;
	struct cvmx_mio_uart2_sbcr_s cn52xxp1;
};

union cvmx_mio_uart2_scr {
	uint64_t u64;
	struct cvmx_mio_uart2_scr_s {
		uint64_t reserved_8_63:56;
		uint64_t scr:8;
	} s;
	struct cvmx_mio_uart2_scr_s cn52xx;
	struct cvmx_mio_uart2_scr_s cn52xxp1;
};

union cvmx_mio_uart2_sfe {
	uint64_t u64;
	struct cvmx_mio_uart2_sfe_s {
		uint64_t reserved_1_63:63;
		uint64_t sfe:1;
	} s;
	struct cvmx_mio_uart2_sfe_s cn52xx;
	struct cvmx_mio_uart2_sfe_s cn52xxp1;
};

union cvmx_mio_uart2_srr {
	uint64_t u64;
	struct cvmx_mio_uart2_srr_s {
		uint64_t reserved_3_63:61;
		uint64_t stfr:1;
		uint64_t srfr:1;
		uint64_t usr:1;
	} s;
	struct cvmx_mio_uart2_srr_s cn52xx;
	struct cvmx_mio_uart2_srr_s cn52xxp1;
};

union cvmx_mio_uart2_srt {
	uint64_t u64;
	struct cvmx_mio_uart2_srt_s {
		uint64_t reserved_2_63:62;
		uint64_t srt:2;
	} s;
	struct cvmx_mio_uart2_srt_s cn52xx;
	struct cvmx_mio_uart2_srt_s cn52xxp1;
};

union cvmx_mio_uart2_srts {
	uint64_t u64;
	struct cvmx_mio_uart2_srts_s {
		uint64_t reserved_1_63:63;
		uint64_t srts:1;
	} s;
	struct cvmx_mio_uart2_srts_s cn52xx;
	struct cvmx_mio_uart2_srts_s cn52xxp1;
};

union cvmx_mio_uart2_stt {
	uint64_t u64;
	struct cvmx_mio_uart2_stt_s {
		uint64_t reserved_2_63:62;
		uint64_t stt:2;
	} s;
	struct cvmx_mio_uart2_stt_s cn52xx;
	struct cvmx_mio_uart2_stt_s cn52xxp1;
};

union cvmx_mio_uart2_tfl {
	uint64_t u64;
	struct cvmx_mio_uart2_tfl_s {
		uint64_t reserved_7_63:57;
		uint64_t tfl:7;
	} s;
	struct cvmx_mio_uart2_tfl_s cn52xx;
	struct cvmx_mio_uart2_tfl_s cn52xxp1;
};

union cvmx_mio_uart2_tfr {
	uint64_t u64;
	struct cvmx_mio_uart2_tfr_s {
		uint64_t reserved_8_63:56;
		uint64_t tfr:8;
	} s;
	struct cvmx_mio_uart2_tfr_s cn52xx;
	struct cvmx_mio_uart2_tfr_s cn52xxp1;
};

union cvmx_mio_uart2_thr {
	uint64_t u64;
	struct cvmx_mio_uart2_thr_s {
		uint64_t reserved_8_63:56;
		uint64_t thr:8;
	} s;
	struct cvmx_mio_uart2_thr_s cn52xx;
	struct cvmx_mio_uart2_thr_s cn52xxp1;
};

union cvmx_mio_uart2_usr {
	uint64_t u64;
	struct cvmx_mio_uart2_usr_s {
		uint64_t reserved_5_63:59;
		uint64_t rff:1;
		uint64_t rfne:1;
		uint64_t tfe:1;
		uint64_t tfnf:1;
		uint64_t busy:1;
	} s;
	struct cvmx_mio_uart2_usr_s cn52xx;
	struct cvmx_mio_uart2_usr_s cn52xxp1;
};

#endif

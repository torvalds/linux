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

#ifndef __CVMX_MIO_DEFS_H__
#define __CVMX_MIO_DEFS_H__

#define CVMX_MIO_BOOT_BIST_STAT (CVMX_ADD_IO_SEG(0x00011800000000F8ull))
#define CVMX_MIO_BOOT_COMP (CVMX_ADD_IO_SEG(0x00011800000000B8ull))
#define CVMX_MIO_BOOT_DMA_CFGX(offset) (CVMX_ADD_IO_SEG(0x0001180000000100ull) + ((offset) & 3) * 8)
#define CVMX_MIO_BOOT_DMA_INTX(offset) (CVMX_ADD_IO_SEG(0x0001180000000138ull) + ((offset) & 3) * 8)
#define CVMX_MIO_BOOT_DMA_INT_ENX(offset) (CVMX_ADD_IO_SEG(0x0001180000000150ull) + ((offset) & 3) * 8)
#define CVMX_MIO_BOOT_DMA_TIMX(offset) (CVMX_ADD_IO_SEG(0x0001180000000120ull) + ((offset) & 3) * 8)
#define CVMX_MIO_BOOT_ERR (CVMX_ADD_IO_SEG(0x00011800000000A0ull))
#define CVMX_MIO_BOOT_INT (CVMX_ADD_IO_SEG(0x00011800000000A8ull))
#define CVMX_MIO_BOOT_LOC_ADR (CVMX_ADD_IO_SEG(0x0001180000000090ull))
#define CVMX_MIO_BOOT_LOC_CFGX(offset) (CVMX_ADD_IO_SEG(0x0001180000000080ull) + ((offset) & 1) * 8)
#define CVMX_MIO_BOOT_LOC_DAT (CVMX_ADD_IO_SEG(0x0001180000000098ull))
#define CVMX_MIO_BOOT_PIN_DEFS (CVMX_ADD_IO_SEG(0x00011800000000C0ull))
#define CVMX_MIO_BOOT_REG_CFGX(offset) (CVMX_ADD_IO_SEG(0x0001180000000000ull) + ((offset) & 7) * 8)
#define CVMX_MIO_BOOT_REG_TIMX(offset) (CVMX_ADD_IO_SEG(0x0001180000000040ull) + ((offset) & 7) * 8)
#define CVMX_MIO_BOOT_THR (CVMX_ADD_IO_SEG(0x00011800000000B0ull))
#define CVMX_MIO_EMM_BUF_DAT (CVMX_ADD_IO_SEG(0x00011800000020E8ull))
#define CVMX_MIO_EMM_BUF_IDX (CVMX_ADD_IO_SEG(0x00011800000020E0ull))
#define CVMX_MIO_EMM_CFG (CVMX_ADD_IO_SEG(0x0001180000002000ull))
#define CVMX_MIO_EMM_CMD (CVMX_ADD_IO_SEG(0x0001180000002058ull))
#define CVMX_MIO_EMM_DMA (CVMX_ADD_IO_SEG(0x0001180000002050ull))
#define CVMX_MIO_EMM_INT (CVMX_ADD_IO_SEG(0x0001180000002078ull))
#define CVMX_MIO_EMM_INT_EN (CVMX_ADD_IO_SEG(0x0001180000002080ull))
#define CVMX_MIO_EMM_MODEX(offset) (CVMX_ADD_IO_SEG(0x0001180000002008ull) + ((offset) & 3) * 8)
#define CVMX_MIO_EMM_RCA (CVMX_ADD_IO_SEG(0x00011800000020A0ull))
#define CVMX_MIO_EMM_RSP_HI (CVMX_ADD_IO_SEG(0x0001180000002070ull))
#define CVMX_MIO_EMM_RSP_LO (CVMX_ADD_IO_SEG(0x0001180000002068ull))
#define CVMX_MIO_EMM_RSP_STS (CVMX_ADD_IO_SEG(0x0001180000002060ull))
#define CVMX_MIO_EMM_SAMPLE (CVMX_ADD_IO_SEG(0x0001180000002090ull))
#define CVMX_MIO_EMM_STS_MASK (CVMX_ADD_IO_SEG(0x0001180000002098ull))
#define CVMX_MIO_EMM_SWITCH (CVMX_ADD_IO_SEG(0x0001180000002048ull))
#define CVMX_MIO_EMM_WDOG (CVMX_ADD_IO_SEG(0x0001180000002088ull))
#define CVMX_MIO_FUS_BNK_DATX(offset) (CVMX_ADD_IO_SEG(0x0001180000001520ull) + ((offset) & 3) * 8)
#define CVMX_MIO_FUS_DAT0 (CVMX_ADD_IO_SEG(0x0001180000001400ull))
#define CVMX_MIO_FUS_DAT1 (CVMX_ADD_IO_SEG(0x0001180000001408ull))
#define CVMX_MIO_FUS_DAT2 (CVMX_ADD_IO_SEG(0x0001180000001410ull))
#define CVMX_MIO_FUS_DAT3 (CVMX_ADD_IO_SEG(0x0001180000001418ull))
#define CVMX_MIO_FUS_EMA (CVMX_ADD_IO_SEG(0x0001180000001550ull))
#define CVMX_MIO_FUS_PDF (CVMX_ADD_IO_SEG(0x0001180000001420ull))
#define CVMX_MIO_FUS_PLL (CVMX_ADD_IO_SEG(0x0001180000001580ull))
#define CVMX_MIO_FUS_PROG (CVMX_ADD_IO_SEG(0x0001180000001510ull))
#define CVMX_MIO_FUS_PROG_TIMES (CVMX_ADD_IO_SEG(0x0001180000001518ull))
#define CVMX_MIO_FUS_RCMD (CVMX_ADD_IO_SEG(0x0001180000001500ull))
#define CVMX_MIO_FUS_READ_TIMES (CVMX_ADD_IO_SEG(0x0001180000001570ull))
#define CVMX_MIO_FUS_REPAIR_RES0 (CVMX_ADD_IO_SEG(0x0001180000001558ull))
#define CVMX_MIO_FUS_REPAIR_RES1 (CVMX_ADD_IO_SEG(0x0001180000001560ull))
#define CVMX_MIO_FUS_REPAIR_RES2 (CVMX_ADD_IO_SEG(0x0001180000001568ull))
#define CVMX_MIO_FUS_SPR_REPAIR_RES (CVMX_ADD_IO_SEG(0x0001180000001548ull))
#define CVMX_MIO_FUS_SPR_REPAIR_SUM (CVMX_ADD_IO_SEG(0x0001180000001540ull))
#define CVMX_MIO_FUS_TGG (CVMX_ADD_IO_SEG(0x0001180000001428ull))
#define CVMX_MIO_FUS_UNLOCK (CVMX_ADD_IO_SEG(0x0001180000001578ull))
#define CVMX_MIO_FUS_WADR (CVMX_ADD_IO_SEG(0x0001180000001508ull))
#define CVMX_MIO_GPIO_COMP (CVMX_ADD_IO_SEG(0x00011800000000C8ull))
#define CVMX_MIO_NDF_DMA_CFG (CVMX_ADD_IO_SEG(0x0001180000000168ull))
#define CVMX_MIO_NDF_DMA_INT (CVMX_ADD_IO_SEG(0x0001180000000170ull))
#define CVMX_MIO_NDF_DMA_INT_EN (CVMX_ADD_IO_SEG(0x0001180000000178ull))
#define CVMX_MIO_PLL_CTL (CVMX_ADD_IO_SEG(0x0001180000001448ull))
#define CVMX_MIO_PLL_SETTING (CVMX_ADD_IO_SEG(0x0001180000001440ull))
#define CVMX_MIO_PTP_CKOUT_HI_INCR (CVMX_ADD_IO_SEG(0x0001070000000F40ull))
#define CVMX_MIO_PTP_CKOUT_LO_INCR (CVMX_ADD_IO_SEG(0x0001070000000F48ull))
#define CVMX_MIO_PTP_CKOUT_THRESH_HI (CVMX_ADD_IO_SEG(0x0001070000000F38ull))
#define CVMX_MIO_PTP_CKOUT_THRESH_LO (CVMX_ADD_IO_SEG(0x0001070000000F30ull))
#define CVMX_MIO_PTP_CLOCK_CFG (CVMX_ADD_IO_SEG(0x0001070000000F00ull))
#define CVMX_MIO_PTP_CLOCK_COMP (CVMX_ADD_IO_SEG(0x0001070000000F18ull))
#define CVMX_MIO_PTP_CLOCK_HI (CVMX_ADD_IO_SEG(0x0001070000000F10ull))
#define CVMX_MIO_PTP_CLOCK_LO (CVMX_ADD_IO_SEG(0x0001070000000F08ull))
#define CVMX_MIO_PTP_EVT_CNT (CVMX_ADD_IO_SEG(0x0001070000000F28ull))
#define CVMX_MIO_PTP_PHY_1PPS_IN (CVMX_ADD_IO_SEG(0x0001070000000F70ull))
#define CVMX_MIO_PTP_PPS_HI_INCR (CVMX_ADD_IO_SEG(0x0001070000000F60ull))
#define CVMX_MIO_PTP_PPS_LO_INCR (CVMX_ADD_IO_SEG(0x0001070000000F68ull))
#define CVMX_MIO_PTP_PPS_THRESH_HI (CVMX_ADD_IO_SEG(0x0001070000000F58ull))
#define CVMX_MIO_PTP_PPS_THRESH_LO (CVMX_ADD_IO_SEG(0x0001070000000F50ull))
#define CVMX_MIO_PTP_TIMESTAMP (CVMX_ADD_IO_SEG(0x0001070000000F20ull))
#define CVMX_MIO_QLMX_CFG(offset) (CVMX_ADD_IO_SEG(0x0001180000001590ull) + ((offset) & 7) * 8)
#define CVMX_MIO_RST_BOOT (CVMX_ADD_IO_SEG(0x0001180000001600ull))
#define CVMX_MIO_RST_CFG (CVMX_ADD_IO_SEG(0x0001180000001610ull))
#define CVMX_MIO_RST_CKILL (CVMX_ADD_IO_SEG(0x0001180000001638ull))
#define CVMX_MIO_RST_CNTLX(offset) (CVMX_ADD_IO_SEG(0x0001180000001648ull) + ((offset) & 3) * 8)
#define CVMX_MIO_RST_CTLX(offset) (CVMX_ADD_IO_SEG(0x0001180000001618ull) + ((offset) & 1) * 8)
#define CVMX_MIO_RST_DELAY (CVMX_ADD_IO_SEG(0x0001180000001608ull))
#define CVMX_MIO_RST_INT (CVMX_ADD_IO_SEG(0x0001180000001628ull))
#define CVMX_MIO_RST_INT_EN (CVMX_ADD_IO_SEG(0x0001180000001630ull))
#define CVMX_MIO_TWSX_INT(offset) (CVMX_ADD_IO_SEG(0x0001180000001010ull) + ((offset) & 1) * 512)
#define CVMX_MIO_TWSX_SW_TWSI(offset) (CVMX_ADD_IO_SEG(0x0001180000001000ull) + ((offset) & 1) * 512)
#define CVMX_MIO_TWSX_SW_TWSI_EXT(offset) (CVMX_ADD_IO_SEG(0x0001180000001018ull) + ((offset) & 1) * 512)
#define CVMX_MIO_TWSX_TWSI_SW(offset) (CVMX_ADD_IO_SEG(0x0001180000001008ull) + ((offset) & 1) * 512)
#define CVMX_MIO_UART2_DLH (CVMX_ADD_IO_SEG(0x0001180000000488ull))
#define CVMX_MIO_UART2_DLL (CVMX_ADD_IO_SEG(0x0001180000000480ull))
#define CVMX_MIO_UART2_FAR (CVMX_ADD_IO_SEG(0x0001180000000520ull))
#define CVMX_MIO_UART2_FCR (CVMX_ADD_IO_SEG(0x0001180000000450ull))
#define CVMX_MIO_UART2_HTX (CVMX_ADD_IO_SEG(0x0001180000000708ull))
#define CVMX_MIO_UART2_IER (CVMX_ADD_IO_SEG(0x0001180000000408ull))
#define CVMX_MIO_UART2_IIR (CVMX_ADD_IO_SEG(0x0001180000000410ull))
#define CVMX_MIO_UART2_LCR (CVMX_ADD_IO_SEG(0x0001180000000418ull))
#define CVMX_MIO_UART2_LSR (CVMX_ADD_IO_SEG(0x0001180000000428ull))
#define CVMX_MIO_UART2_MCR (CVMX_ADD_IO_SEG(0x0001180000000420ull))
#define CVMX_MIO_UART2_MSR (CVMX_ADD_IO_SEG(0x0001180000000430ull))
#define CVMX_MIO_UART2_RBR (CVMX_ADD_IO_SEG(0x0001180000000400ull))
#define CVMX_MIO_UART2_RFL (CVMX_ADD_IO_SEG(0x0001180000000608ull))
#define CVMX_MIO_UART2_RFW (CVMX_ADD_IO_SEG(0x0001180000000530ull))
#define CVMX_MIO_UART2_SBCR (CVMX_ADD_IO_SEG(0x0001180000000620ull))
#define CVMX_MIO_UART2_SCR (CVMX_ADD_IO_SEG(0x0001180000000438ull))
#define CVMX_MIO_UART2_SFE (CVMX_ADD_IO_SEG(0x0001180000000630ull))
#define CVMX_MIO_UART2_SRR (CVMX_ADD_IO_SEG(0x0001180000000610ull))
#define CVMX_MIO_UART2_SRT (CVMX_ADD_IO_SEG(0x0001180000000638ull))
#define CVMX_MIO_UART2_SRTS (CVMX_ADD_IO_SEG(0x0001180000000618ull))
#define CVMX_MIO_UART2_STT (CVMX_ADD_IO_SEG(0x0001180000000700ull))
#define CVMX_MIO_UART2_TFL (CVMX_ADD_IO_SEG(0x0001180000000600ull))
#define CVMX_MIO_UART2_TFR (CVMX_ADD_IO_SEG(0x0001180000000528ull))
#define CVMX_MIO_UART2_THR (CVMX_ADD_IO_SEG(0x0001180000000440ull))
#define CVMX_MIO_UART2_USR (CVMX_ADD_IO_SEG(0x0001180000000538ull))
#define CVMX_MIO_UARTX_DLH(offset) (CVMX_ADD_IO_SEG(0x0001180000000888ull) + ((offset) & 1) * 1024)
#define CVMX_MIO_UARTX_DLL(offset) (CVMX_ADD_IO_SEG(0x0001180000000880ull) + ((offset) & 1) * 1024)
#define CVMX_MIO_UARTX_FAR(offset) (CVMX_ADD_IO_SEG(0x0001180000000920ull) + ((offset) & 1) * 1024)
#define CVMX_MIO_UARTX_FCR(offset) (CVMX_ADD_IO_SEG(0x0001180000000850ull) + ((offset) & 1) * 1024)
#define CVMX_MIO_UARTX_HTX(offset) (CVMX_ADD_IO_SEG(0x0001180000000B08ull) + ((offset) & 1) * 1024)
#define CVMX_MIO_UARTX_IER(offset) (CVMX_ADD_IO_SEG(0x0001180000000808ull) + ((offset) & 1) * 1024)
#define CVMX_MIO_UARTX_IIR(offset) (CVMX_ADD_IO_SEG(0x0001180000000810ull) + ((offset) & 1) * 1024)
#define CVMX_MIO_UARTX_LCR(offset) (CVMX_ADD_IO_SEG(0x0001180000000818ull) + ((offset) & 1) * 1024)
#define CVMX_MIO_UARTX_LSR(offset) (CVMX_ADD_IO_SEG(0x0001180000000828ull) + ((offset) & 1) * 1024)
#define CVMX_MIO_UARTX_MCR(offset) (CVMX_ADD_IO_SEG(0x0001180000000820ull) + ((offset) & 1) * 1024)
#define CVMX_MIO_UARTX_MSR(offset) (CVMX_ADD_IO_SEG(0x0001180000000830ull) + ((offset) & 1) * 1024)
#define CVMX_MIO_UARTX_RBR(offset) (CVMX_ADD_IO_SEG(0x0001180000000800ull) + ((offset) & 1) * 1024)
#define CVMX_MIO_UARTX_RFL(offset) (CVMX_ADD_IO_SEG(0x0001180000000A08ull) + ((offset) & 1) * 1024)
#define CVMX_MIO_UARTX_RFW(offset) (CVMX_ADD_IO_SEG(0x0001180000000930ull) + ((offset) & 1) * 1024)
#define CVMX_MIO_UARTX_SBCR(offset) (CVMX_ADD_IO_SEG(0x0001180000000A20ull) + ((offset) & 1) * 1024)
#define CVMX_MIO_UARTX_SCR(offset) (CVMX_ADD_IO_SEG(0x0001180000000838ull) + ((offset) & 1) * 1024)
#define CVMX_MIO_UARTX_SFE(offset) (CVMX_ADD_IO_SEG(0x0001180000000A30ull) + ((offset) & 1) * 1024)
#define CVMX_MIO_UARTX_SRR(offset) (CVMX_ADD_IO_SEG(0x0001180000000A10ull) + ((offset) & 1) * 1024)
#define CVMX_MIO_UARTX_SRT(offset) (CVMX_ADD_IO_SEG(0x0001180000000A38ull) + ((offset) & 1) * 1024)
#define CVMX_MIO_UARTX_SRTS(offset) (CVMX_ADD_IO_SEG(0x0001180000000A18ull) + ((offset) & 1) * 1024)
#define CVMX_MIO_UARTX_STT(offset) (CVMX_ADD_IO_SEG(0x0001180000000B00ull) + ((offset) & 1) * 1024)
#define CVMX_MIO_UARTX_TFL(offset) (CVMX_ADD_IO_SEG(0x0001180000000A00ull) + ((offset) & 1) * 1024)
#define CVMX_MIO_UARTX_TFR(offset) (CVMX_ADD_IO_SEG(0x0001180000000928ull) + ((offset) & 1) * 1024)
#define CVMX_MIO_UARTX_THR(offset) (CVMX_ADD_IO_SEG(0x0001180000000840ull) + ((offset) & 1) * 1024)
#define CVMX_MIO_UARTX_USR(offset) (CVMX_ADD_IO_SEG(0x0001180000000938ull) + ((offset) & 1) * 1024)

union cvmx_mio_boot_bist_stat {
	uint64_t u64;
	struct cvmx_mio_boot_bist_stat_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_0_63:64;
#else
		uint64_t reserved_0_63:64;
#endif
	} s;
	struct cvmx_mio_boot_bist_stat_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t ncbo_1:1;
		uint64_t ncbo_0:1;
		uint64_t loc:1;
		uint64_t ncbi:1;
#else
		uint64_t ncbi:1;
		uint64_t loc:1;
		uint64_t ncbo_0:1;
		uint64_t ncbo_1:1;
		uint64_t reserved_4_63:60;
#endif
	} cn30xx;
	struct cvmx_mio_boot_bist_stat_cn30xx cn31xx;
	struct cvmx_mio_boot_bist_stat_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t ncbo_0:1;
		uint64_t loc:1;
		uint64_t ncbi:1;
#else
		uint64_t ncbi:1;
		uint64_t loc:1;
		uint64_t ncbo_0:1;
		uint64_t reserved_3_63:61;
#endif
	} cn38xx;
	struct cvmx_mio_boot_bist_stat_cn38xx cn38xxp2;
	struct cvmx_mio_boot_bist_stat_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t pcm_1:1;
		uint64_t pcm_0:1;
		uint64_t ncbo_1:1;
		uint64_t ncbo_0:1;
		uint64_t loc:1;
		uint64_t ncbi:1;
#else
		uint64_t ncbi:1;
		uint64_t loc:1;
		uint64_t ncbo_0:1;
		uint64_t ncbo_1:1;
		uint64_t pcm_0:1;
		uint64_t pcm_1:1;
		uint64_t reserved_6_63:58;
#endif
	} cn50xx;
	struct cvmx_mio_boot_bist_stat_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t ndf:2;
		uint64_t ncbo_0:1;
		uint64_t dma:1;
		uint64_t loc:1;
		uint64_t ncbi:1;
#else
		uint64_t ncbi:1;
		uint64_t loc:1;
		uint64_t dma:1;
		uint64_t ncbo_0:1;
		uint64_t ndf:2;
		uint64_t reserved_6_63:58;
#endif
	} cn52xx;
	struct cvmx_mio_boot_bist_stat_cn52xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t ncbo_0:1;
		uint64_t dma:1;
		uint64_t loc:1;
		uint64_t ncbi:1;
#else
		uint64_t ncbi:1;
		uint64_t loc:1;
		uint64_t dma:1;
		uint64_t ncbo_0:1;
		uint64_t reserved_4_63:60;
#endif
	} cn52xxp1;
	struct cvmx_mio_boot_bist_stat_cn52xxp1 cn56xx;
	struct cvmx_mio_boot_bist_stat_cn52xxp1 cn56xxp1;
	struct cvmx_mio_boot_bist_stat_cn38xx cn58xx;
	struct cvmx_mio_boot_bist_stat_cn38xx cn58xxp1;
	struct cvmx_mio_boot_bist_stat_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t stat:12;
#else
		uint64_t stat:12;
		uint64_t reserved_12_63:52;
#endif
	} cn61xx;
	struct cvmx_mio_boot_bist_stat_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t stat:9;
#else
		uint64_t stat:9;
		uint64_t reserved_9_63:55;
#endif
	} cn63xx;
	struct cvmx_mio_boot_bist_stat_cn63xx cn63xxp1;
	struct cvmx_mio_boot_bist_stat_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t stat:10;
#else
		uint64_t stat:10;
		uint64_t reserved_10_63:54;
#endif
	} cn66xx;
	struct cvmx_mio_boot_bist_stat_cn66xx cn68xx;
	struct cvmx_mio_boot_bist_stat_cn66xx cn68xxp1;
	struct cvmx_mio_boot_bist_stat_cn61xx cnf71xx;
};

union cvmx_mio_boot_comp {
	uint64_t u64;
	struct cvmx_mio_boot_comp_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_0_63:64;
#else
		uint64_t reserved_0_63:64;
#endif
	} s;
	struct cvmx_mio_boot_comp_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t pctl:5;
		uint64_t nctl:5;
#else
		uint64_t nctl:5;
		uint64_t pctl:5;
		uint64_t reserved_10_63:54;
#endif
	} cn50xx;
	struct cvmx_mio_boot_comp_cn50xx cn52xx;
	struct cvmx_mio_boot_comp_cn50xx cn52xxp1;
	struct cvmx_mio_boot_comp_cn50xx cn56xx;
	struct cvmx_mio_boot_comp_cn50xx cn56xxp1;
	struct cvmx_mio_boot_comp_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t pctl:6;
		uint64_t nctl:6;
#else
		uint64_t nctl:6;
		uint64_t pctl:6;
		uint64_t reserved_12_63:52;
#endif
	} cn61xx;
	struct cvmx_mio_boot_comp_cn61xx cn63xx;
	struct cvmx_mio_boot_comp_cn61xx cn63xxp1;
	struct cvmx_mio_boot_comp_cn61xx cn66xx;
	struct cvmx_mio_boot_comp_cn61xx cn68xx;
	struct cvmx_mio_boot_comp_cn61xx cn68xxp1;
	struct cvmx_mio_boot_comp_cn61xx cnf71xx;
};

union cvmx_mio_boot_dma_cfgx {
	uint64_t u64;
	struct cvmx_mio_boot_dma_cfgx_s {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t adr:36;
		uint64_t size:20;
		uint64_t endian:1;
		uint64_t swap8:1;
		uint64_t swap16:1;
		uint64_t swap32:1;
		uint64_t reserved_60_60:1;
		uint64_t clr:1;
		uint64_t rw:1;
		uint64_t en:1;
#endif
	} s;
	struct cvmx_mio_boot_dma_cfgx_s cn52xx;
	struct cvmx_mio_boot_dma_cfgx_s cn52xxp1;
	struct cvmx_mio_boot_dma_cfgx_s cn56xx;
	struct cvmx_mio_boot_dma_cfgx_s cn56xxp1;
	struct cvmx_mio_boot_dma_cfgx_s cn61xx;
	struct cvmx_mio_boot_dma_cfgx_s cn63xx;
	struct cvmx_mio_boot_dma_cfgx_s cn63xxp1;
	struct cvmx_mio_boot_dma_cfgx_s cn66xx;
	struct cvmx_mio_boot_dma_cfgx_s cn68xx;
	struct cvmx_mio_boot_dma_cfgx_s cn68xxp1;
	struct cvmx_mio_boot_dma_cfgx_s cnf71xx;
};

union cvmx_mio_boot_dma_intx {
	uint64_t u64;
	struct cvmx_mio_boot_dma_intx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t dmarq:1;
		uint64_t done:1;
#else
		uint64_t done:1;
		uint64_t dmarq:1;
		uint64_t reserved_2_63:62;
#endif
	} s;
	struct cvmx_mio_boot_dma_intx_s cn52xx;
	struct cvmx_mio_boot_dma_intx_s cn52xxp1;
	struct cvmx_mio_boot_dma_intx_s cn56xx;
	struct cvmx_mio_boot_dma_intx_s cn56xxp1;
	struct cvmx_mio_boot_dma_intx_s cn61xx;
	struct cvmx_mio_boot_dma_intx_s cn63xx;
	struct cvmx_mio_boot_dma_intx_s cn63xxp1;
	struct cvmx_mio_boot_dma_intx_s cn66xx;
	struct cvmx_mio_boot_dma_intx_s cn68xx;
	struct cvmx_mio_boot_dma_intx_s cn68xxp1;
	struct cvmx_mio_boot_dma_intx_s cnf71xx;
};

union cvmx_mio_boot_dma_int_enx {
	uint64_t u64;
	struct cvmx_mio_boot_dma_int_enx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t dmarq:1;
		uint64_t done:1;
#else
		uint64_t done:1;
		uint64_t dmarq:1;
		uint64_t reserved_2_63:62;
#endif
	} s;
	struct cvmx_mio_boot_dma_int_enx_s cn52xx;
	struct cvmx_mio_boot_dma_int_enx_s cn52xxp1;
	struct cvmx_mio_boot_dma_int_enx_s cn56xx;
	struct cvmx_mio_boot_dma_int_enx_s cn56xxp1;
	struct cvmx_mio_boot_dma_int_enx_s cn61xx;
	struct cvmx_mio_boot_dma_int_enx_s cn63xx;
	struct cvmx_mio_boot_dma_int_enx_s cn63xxp1;
	struct cvmx_mio_boot_dma_int_enx_s cn66xx;
	struct cvmx_mio_boot_dma_int_enx_s cn68xx;
	struct cvmx_mio_boot_dma_int_enx_s cn68xxp1;
	struct cvmx_mio_boot_dma_int_enx_s cnf71xx;
};

union cvmx_mio_boot_dma_timx {
	uint64_t u64;
	struct cvmx_mio_boot_dma_timx_s {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t dmarq:6;
		uint64_t dmack_s:6;
		uint64_t oe_a:6;
		uint64_t oe_n:6;
		uint64_t we_a:6;
		uint64_t we_n:6;
		uint64_t dmack_h:6;
		uint64_t pause:6;
		uint64_t reserved_48_54:7;
		uint64_t width:1;
		uint64_t ddr:1;
		uint64_t rd_dly:3;
		uint64_t tim_mult:2;
		uint64_t dmarq_pi:1;
		uint64_t dmack_pi:1;
#endif
	} s;
	struct cvmx_mio_boot_dma_timx_s cn52xx;
	struct cvmx_mio_boot_dma_timx_s cn52xxp1;
	struct cvmx_mio_boot_dma_timx_s cn56xx;
	struct cvmx_mio_boot_dma_timx_s cn56xxp1;
	struct cvmx_mio_boot_dma_timx_s cn61xx;
	struct cvmx_mio_boot_dma_timx_s cn63xx;
	struct cvmx_mio_boot_dma_timx_s cn63xxp1;
	struct cvmx_mio_boot_dma_timx_s cn66xx;
	struct cvmx_mio_boot_dma_timx_s cn68xx;
	struct cvmx_mio_boot_dma_timx_s cn68xxp1;
	struct cvmx_mio_boot_dma_timx_s cnf71xx;
};

union cvmx_mio_boot_err {
	uint64_t u64;
	struct cvmx_mio_boot_err_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t wait_err:1;
		uint64_t adr_err:1;
#else
		uint64_t adr_err:1;
		uint64_t wait_err:1;
		uint64_t reserved_2_63:62;
#endif
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
	struct cvmx_mio_boot_err_s cn61xx;
	struct cvmx_mio_boot_err_s cn63xx;
	struct cvmx_mio_boot_err_s cn63xxp1;
	struct cvmx_mio_boot_err_s cn66xx;
	struct cvmx_mio_boot_err_s cn68xx;
	struct cvmx_mio_boot_err_s cn68xxp1;
	struct cvmx_mio_boot_err_s cnf71xx;
};

union cvmx_mio_boot_int {
	uint64_t u64;
	struct cvmx_mio_boot_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t wait_int:1;
		uint64_t adr_int:1;
#else
		uint64_t adr_int:1;
		uint64_t wait_int:1;
		uint64_t reserved_2_63:62;
#endif
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
	struct cvmx_mio_boot_int_s cn61xx;
	struct cvmx_mio_boot_int_s cn63xx;
	struct cvmx_mio_boot_int_s cn63xxp1;
	struct cvmx_mio_boot_int_s cn66xx;
	struct cvmx_mio_boot_int_s cn68xx;
	struct cvmx_mio_boot_int_s cn68xxp1;
	struct cvmx_mio_boot_int_s cnf71xx;
};

union cvmx_mio_boot_loc_adr {
	uint64_t u64;
	struct cvmx_mio_boot_loc_adr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t adr:5;
		uint64_t reserved_0_2:3;
#else
		uint64_t reserved_0_2:3;
		uint64_t adr:5;
		uint64_t reserved_8_63:56;
#endif
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
	struct cvmx_mio_boot_loc_adr_s cn61xx;
	struct cvmx_mio_boot_loc_adr_s cn63xx;
	struct cvmx_mio_boot_loc_adr_s cn63xxp1;
	struct cvmx_mio_boot_loc_adr_s cn66xx;
	struct cvmx_mio_boot_loc_adr_s cn68xx;
	struct cvmx_mio_boot_loc_adr_s cn68xxp1;
	struct cvmx_mio_boot_loc_adr_s cnf71xx;
};

union cvmx_mio_boot_loc_cfgx {
	uint64_t u64;
	struct cvmx_mio_boot_loc_cfgx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t en:1;
		uint64_t reserved_28_30:3;
		uint64_t base:25;
		uint64_t reserved_0_2:3;
#else
		uint64_t reserved_0_2:3;
		uint64_t base:25;
		uint64_t reserved_28_30:3;
		uint64_t en:1;
		uint64_t reserved_32_63:32;
#endif
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
	struct cvmx_mio_boot_loc_cfgx_s cn61xx;
	struct cvmx_mio_boot_loc_cfgx_s cn63xx;
	struct cvmx_mio_boot_loc_cfgx_s cn63xxp1;
	struct cvmx_mio_boot_loc_cfgx_s cn66xx;
	struct cvmx_mio_boot_loc_cfgx_s cn68xx;
	struct cvmx_mio_boot_loc_cfgx_s cn68xxp1;
	struct cvmx_mio_boot_loc_cfgx_s cnf71xx;
};

union cvmx_mio_boot_loc_dat {
	uint64_t u64;
	struct cvmx_mio_boot_loc_dat_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t data:64;
#else
		uint64_t data:64;
#endif
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
	struct cvmx_mio_boot_loc_dat_s cn61xx;
	struct cvmx_mio_boot_loc_dat_s cn63xx;
	struct cvmx_mio_boot_loc_dat_s cn63xxp1;
	struct cvmx_mio_boot_loc_dat_s cn66xx;
	struct cvmx_mio_boot_loc_dat_s cn68xx;
	struct cvmx_mio_boot_loc_dat_s cn68xxp1;
	struct cvmx_mio_boot_loc_dat_s cnf71xx;
};

union cvmx_mio_boot_pin_defs {
	uint64_t u64;
	struct cvmx_mio_boot_pin_defs_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t user1:16;
		uint64_t ale:1;
		uint64_t width:1;
		uint64_t dmack_p2:1;
		uint64_t dmack_p1:1;
		uint64_t dmack_p0:1;
		uint64_t term:2;
		uint64_t nand:1;
		uint64_t user0:8;
#else
		uint64_t user0:8;
		uint64_t nand:1;
		uint64_t term:2;
		uint64_t dmack_p0:1;
		uint64_t dmack_p1:1;
		uint64_t dmack_p2:1;
		uint64_t width:1;
		uint64_t ale:1;
		uint64_t user1:16;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_mio_boot_pin_defs_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t ale:1;
		uint64_t width:1;
		uint64_t reserved_13_13:1;
		uint64_t dmack_p1:1;
		uint64_t dmack_p0:1;
		uint64_t term:2;
		uint64_t nand:1;
		uint64_t reserved_0_7:8;
#else
		uint64_t reserved_0_7:8;
		uint64_t nand:1;
		uint64_t term:2;
		uint64_t dmack_p0:1;
		uint64_t dmack_p1:1;
		uint64_t reserved_13_13:1;
		uint64_t width:1;
		uint64_t ale:1;
		uint64_t reserved_16_63:48;
#endif
	} cn52xx;
	struct cvmx_mio_boot_pin_defs_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t ale:1;
		uint64_t width:1;
		uint64_t dmack_p2:1;
		uint64_t dmack_p1:1;
		uint64_t dmack_p0:1;
		uint64_t term:2;
		uint64_t reserved_0_8:9;
#else
		uint64_t reserved_0_8:9;
		uint64_t term:2;
		uint64_t dmack_p0:1;
		uint64_t dmack_p1:1;
		uint64_t dmack_p2:1;
		uint64_t width:1;
		uint64_t ale:1;
		uint64_t reserved_16_63:48;
#endif
	} cn56xx;
	struct cvmx_mio_boot_pin_defs_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t user1:16;
		uint64_t ale:1;
		uint64_t width:1;
		uint64_t reserved_13_13:1;
		uint64_t dmack_p1:1;
		uint64_t dmack_p0:1;
		uint64_t term:2;
		uint64_t nand:1;
		uint64_t user0:8;
#else
		uint64_t user0:8;
		uint64_t nand:1;
		uint64_t term:2;
		uint64_t dmack_p0:1;
		uint64_t dmack_p1:1;
		uint64_t reserved_13_13:1;
		uint64_t width:1;
		uint64_t ale:1;
		uint64_t user1:16;
		uint64_t reserved_32_63:32;
#endif
	} cn61xx;
	struct cvmx_mio_boot_pin_defs_cn52xx cn63xx;
	struct cvmx_mio_boot_pin_defs_cn52xx cn63xxp1;
	struct cvmx_mio_boot_pin_defs_cn52xx cn66xx;
	struct cvmx_mio_boot_pin_defs_cn52xx cn68xx;
	struct cvmx_mio_boot_pin_defs_cn52xx cn68xxp1;
	struct cvmx_mio_boot_pin_defs_cn61xx cnf71xx;
};

union cvmx_mio_boot_reg_cfgx {
	uint64_t u64;
	struct cvmx_mio_boot_reg_cfgx_s {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t base:16;
		uint64_t size:12;
		uint64_t width:1;
		uint64_t ale:1;
		uint64_t orbit:1;
		uint64_t en:1;
		uint64_t oe_ext:2;
		uint64_t we_ext:2;
		uint64_t sam:1;
		uint64_t rd_dly:3;
		uint64_t tim_mult:2;
		uint64_t dmack:2;
		uint64_t reserved_44_63:20;
#endif
	} s;
	struct cvmx_mio_boot_reg_cfgx_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t base:16;
		uint64_t size:12;
		uint64_t width:1;
		uint64_t ale:1;
		uint64_t orbit:1;
		uint64_t en:1;
		uint64_t oe_ext:2;
		uint64_t we_ext:2;
		uint64_t sam:1;
		uint64_t reserved_37_63:27;
#endif
	} cn30xx;
	struct cvmx_mio_boot_reg_cfgx_cn30xx cn31xx;
	struct cvmx_mio_boot_reg_cfgx_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t en:1;
		uint64_t orbit:1;
		uint64_t reserved_28_29:2;
		uint64_t size:12;
		uint64_t base:16;
#else
		uint64_t base:16;
		uint64_t size:12;
		uint64_t reserved_28_29:2;
		uint64_t orbit:1;
		uint64_t en:1;
		uint64_t reserved_32_63:32;
#endif
	} cn38xx;
	struct cvmx_mio_boot_reg_cfgx_cn38xx cn38xxp2;
	struct cvmx_mio_boot_reg_cfgx_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t base:16;
		uint64_t size:12;
		uint64_t width:1;
		uint64_t ale:1;
		uint64_t orbit:1;
		uint64_t en:1;
		uint64_t oe_ext:2;
		uint64_t we_ext:2;
		uint64_t sam:1;
		uint64_t rd_dly:3;
		uint64_t tim_mult:2;
		uint64_t reserved_42_63:22;
#endif
	} cn50xx;
	struct cvmx_mio_boot_reg_cfgx_s cn52xx;
	struct cvmx_mio_boot_reg_cfgx_s cn52xxp1;
	struct cvmx_mio_boot_reg_cfgx_s cn56xx;
	struct cvmx_mio_boot_reg_cfgx_s cn56xxp1;
	struct cvmx_mio_boot_reg_cfgx_cn30xx cn58xx;
	struct cvmx_mio_boot_reg_cfgx_cn30xx cn58xxp1;
	struct cvmx_mio_boot_reg_cfgx_s cn61xx;
	struct cvmx_mio_boot_reg_cfgx_s cn63xx;
	struct cvmx_mio_boot_reg_cfgx_s cn63xxp1;
	struct cvmx_mio_boot_reg_cfgx_s cn66xx;
	struct cvmx_mio_boot_reg_cfgx_s cn68xx;
	struct cvmx_mio_boot_reg_cfgx_s cn68xxp1;
	struct cvmx_mio_boot_reg_cfgx_s cnf71xx;
};

union cvmx_mio_boot_reg_timx {
	uint64_t u64;
	struct cvmx_mio_boot_reg_timx_s {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t adr:6;
		uint64_t ce:6;
		uint64_t oe:6;
		uint64_t we:6;
		uint64_t rd_hld:6;
		uint64_t wr_hld:6;
		uint64_t pause:6;
		uint64_t wait:6;
		uint64_t page:6;
		uint64_t ale:6;
		uint64_t pages:2;
		uint64_t waitm:1;
		uint64_t pagem:1;
#endif
	} s;
	struct cvmx_mio_boot_reg_timx_s cn30xx;
	struct cvmx_mio_boot_reg_timx_s cn31xx;
	struct cvmx_mio_boot_reg_timx_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t adr:6;
		uint64_t ce:6;
		uint64_t oe:6;
		uint64_t we:6;
		uint64_t rd_hld:6;
		uint64_t wr_hld:6;
		uint64_t pause:6;
		uint64_t wait:6;
		uint64_t page:6;
		uint64_t reserved_54_59:6;
		uint64_t pages:2;
		uint64_t waitm:1;
		uint64_t pagem:1;
#endif
	} cn38xx;
	struct cvmx_mio_boot_reg_timx_cn38xx cn38xxp2;
	struct cvmx_mio_boot_reg_timx_s cn50xx;
	struct cvmx_mio_boot_reg_timx_s cn52xx;
	struct cvmx_mio_boot_reg_timx_s cn52xxp1;
	struct cvmx_mio_boot_reg_timx_s cn56xx;
	struct cvmx_mio_boot_reg_timx_s cn56xxp1;
	struct cvmx_mio_boot_reg_timx_s cn58xx;
	struct cvmx_mio_boot_reg_timx_s cn58xxp1;
	struct cvmx_mio_boot_reg_timx_s cn61xx;
	struct cvmx_mio_boot_reg_timx_s cn63xx;
	struct cvmx_mio_boot_reg_timx_s cn63xxp1;
	struct cvmx_mio_boot_reg_timx_s cn66xx;
	struct cvmx_mio_boot_reg_timx_s cn68xx;
	struct cvmx_mio_boot_reg_timx_s cn68xxp1;
	struct cvmx_mio_boot_reg_timx_s cnf71xx;
};

union cvmx_mio_boot_thr {
	uint64_t u64;
	struct cvmx_mio_boot_thr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_22_63:42;
		uint64_t dma_thr:6;
		uint64_t reserved_14_15:2;
		uint64_t fif_cnt:6;
		uint64_t reserved_6_7:2;
		uint64_t fif_thr:6;
#else
		uint64_t fif_thr:6;
		uint64_t reserved_6_7:2;
		uint64_t fif_cnt:6;
		uint64_t reserved_14_15:2;
		uint64_t dma_thr:6;
		uint64_t reserved_22_63:42;
#endif
	} s;
	struct cvmx_mio_boot_thr_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_14_63:50;
		uint64_t fif_cnt:6;
		uint64_t reserved_6_7:2;
		uint64_t fif_thr:6;
#else
		uint64_t fif_thr:6;
		uint64_t reserved_6_7:2;
		uint64_t fif_cnt:6;
		uint64_t reserved_14_63:50;
#endif
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
	struct cvmx_mio_boot_thr_s cn61xx;
	struct cvmx_mio_boot_thr_s cn63xx;
	struct cvmx_mio_boot_thr_s cn63xxp1;
	struct cvmx_mio_boot_thr_s cn66xx;
	struct cvmx_mio_boot_thr_s cn68xx;
	struct cvmx_mio_boot_thr_s cn68xxp1;
	struct cvmx_mio_boot_thr_s cnf71xx;
};

union cvmx_mio_emm_buf_dat {
	uint64_t u64;
	struct cvmx_mio_emm_buf_dat_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t dat:64;
#else
		uint64_t dat:64;
#endif
	} s;
	struct cvmx_mio_emm_buf_dat_s cn61xx;
	struct cvmx_mio_emm_buf_dat_s cnf71xx;
};

union cvmx_mio_emm_buf_idx {
	uint64_t u64;
	struct cvmx_mio_emm_buf_idx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_17_63:47;
		uint64_t inc:1;
		uint64_t reserved_7_15:9;
		uint64_t buf_num:1;
		uint64_t offset:6;
#else
		uint64_t offset:6;
		uint64_t buf_num:1;
		uint64_t reserved_7_15:9;
		uint64_t inc:1;
		uint64_t reserved_17_63:47;
#endif
	} s;
	struct cvmx_mio_emm_buf_idx_s cn61xx;
	struct cvmx_mio_emm_buf_idx_s cnf71xx;
};

union cvmx_mio_emm_cfg {
	uint64_t u64;
	struct cvmx_mio_emm_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_17_63:47;
		uint64_t boot_fail:1;
		uint64_t reserved_4_15:12;
		uint64_t bus_ena:4;
#else
		uint64_t bus_ena:4;
		uint64_t reserved_4_15:12;
		uint64_t boot_fail:1;
		uint64_t reserved_17_63:47;
#endif
	} s;
	struct cvmx_mio_emm_cfg_s cn61xx;
	struct cvmx_mio_emm_cfg_s cnf71xx;
};

union cvmx_mio_emm_cmd {
	uint64_t u64;
	struct cvmx_mio_emm_cmd_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_62_63:2;
		uint64_t bus_id:2;
		uint64_t cmd_val:1;
		uint64_t reserved_56_58:3;
		uint64_t dbuf:1;
		uint64_t offset:6;
		uint64_t reserved_43_48:6;
		uint64_t ctype_xor:2;
		uint64_t rtype_xor:3;
		uint64_t cmd_idx:6;
		uint64_t arg:32;
#else
		uint64_t arg:32;
		uint64_t cmd_idx:6;
		uint64_t rtype_xor:3;
		uint64_t ctype_xor:2;
		uint64_t reserved_43_48:6;
		uint64_t offset:6;
		uint64_t dbuf:1;
		uint64_t reserved_56_58:3;
		uint64_t cmd_val:1;
		uint64_t bus_id:2;
		uint64_t reserved_62_63:2;
#endif
	} s;
	struct cvmx_mio_emm_cmd_s cn61xx;
	struct cvmx_mio_emm_cmd_s cnf71xx;
};

union cvmx_mio_emm_dma {
	uint64_t u64;
	struct cvmx_mio_emm_dma_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_62_63:2;
		uint64_t bus_id:2;
		uint64_t dma_val:1;
		uint64_t sector:1;
		uint64_t dat_null:1;
		uint64_t thres:6;
		uint64_t rel_wr:1;
		uint64_t rw:1;
		uint64_t multi:1;
		uint64_t block_cnt:16;
		uint64_t card_addr:32;
#else
		uint64_t card_addr:32;
		uint64_t block_cnt:16;
		uint64_t multi:1;
		uint64_t rw:1;
		uint64_t rel_wr:1;
		uint64_t thres:6;
		uint64_t dat_null:1;
		uint64_t sector:1;
		uint64_t dma_val:1;
		uint64_t bus_id:2;
		uint64_t reserved_62_63:2;
#endif
	} s;
	struct cvmx_mio_emm_dma_s cn61xx;
	struct cvmx_mio_emm_dma_s cnf71xx;
};

union cvmx_mio_emm_int {
	uint64_t u64;
	struct cvmx_mio_emm_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t switch_err:1;
		uint64_t switch_done:1;
		uint64_t dma_err:1;
		uint64_t cmd_err:1;
		uint64_t dma_done:1;
		uint64_t cmd_done:1;
		uint64_t buf_done:1;
#else
		uint64_t buf_done:1;
		uint64_t cmd_done:1;
		uint64_t dma_done:1;
		uint64_t cmd_err:1;
		uint64_t dma_err:1;
		uint64_t switch_done:1;
		uint64_t switch_err:1;
		uint64_t reserved_7_63:57;
#endif
	} s;
	struct cvmx_mio_emm_int_s cn61xx;
	struct cvmx_mio_emm_int_s cnf71xx;
};

union cvmx_mio_emm_int_en {
	uint64_t u64;
	struct cvmx_mio_emm_int_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t switch_err:1;
		uint64_t switch_done:1;
		uint64_t dma_err:1;
		uint64_t cmd_err:1;
		uint64_t dma_done:1;
		uint64_t cmd_done:1;
		uint64_t buf_done:1;
#else
		uint64_t buf_done:1;
		uint64_t cmd_done:1;
		uint64_t dma_done:1;
		uint64_t cmd_err:1;
		uint64_t dma_err:1;
		uint64_t switch_done:1;
		uint64_t switch_err:1;
		uint64_t reserved_7_63:57;
#endif
	} s;
	struct cvmx_mio_emm_int_en_s cn61xx;
	struct cvmx_mio_emm_int_en_s cnf71xx;
};

union cvmx_mio_emm_modex {
	uint64_t u64;
	struct cvmx_mio_emm_modex_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t hs_timing:1;
		uint64_t reserved_43_47:5;
		uint64_t bus_width:3;
		uint64_t reserved_36_39:4;
		uint64_t power_class:4;
		uint64_t clk_hi:16;
		uint64_t clk_lo:16;
#else
		uint64_t clk_lo:16;
		uint64_t clk_hi:16;
		uint64_t power_class:4;
		uint64_t reserved_36_39:4;
		uint64_t bus_width:3;
		uint64_t reserved_43_47:5;
		uint64_t hs_timing:1;
		uint64_t reserved_49_63:15;
#endif
	} s;
	struct cvmx_mio_emm_modex_s cn61xx;
	struct cvmx_mio_emm_modex_s cnf71xx;
};

union cvmx_mio_emm_rca {
	uint64_t u64;
	struct cvmx_mio_emm_rca_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t card_rca:16;
#else
		uint64_t card_rca:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_mio_emm_rca_s cn61xx;
	struct cvmx_mio_emm_rca_s cnf71xx;
};

union cvmx_mio_emm_rsp_hi {
	uint64_t u64;
	struct cvmx_mio_emm_rsp_hi_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t dat:64;
#else
		uint64_t dat:64;
#endif
	} s;
	struct cvmx_mio_emm_rsp_hi_s cn61xx;
	struct cvmx_mio_emm_rsp_hi_s cnf71xx;
};

union cvmx_mio_emm_rsp_lo {
	uint64_t u64;
	struct cvmx_mio_emm_rsp_lo_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t dat:64;
#else
		uint64_t dat:64;
#endif
	} s;
	struct cvmx_mio_emm_rsp_lo_s cn61xx;
	struct cvmx_mio_emm_rsp_lo_s cnf71xx;
};

union cvmx_mio_emm_rsp_sts {
	uint64_t u64;
	struct cvmx_mio_emm_rsp_sts_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_62_63:2;
		uint64_t bus_id:2;
		uint64_t cmd_val:1;
		uint64_t switch_val:1;
		uint64_t dma_val:1;
		uint64_t dma_pend:1;
		uint64_t reserved_29_55:27;
		uint64_t dbuf_err:1;
		uint64_t reserved_24_27:4;
		uint64_t dbuf:1;
		uint64_t blk_timeout:1;
		uint64_t blk_crc_err:1;
		uint64_t rsp_busybit:1;
		uint64_t stp_timeout:1;
		uint64_t stp_crc_err:1;
		uint64_t stp_bad_sts:1;
		uint64_t stp_val:1;
		uint64_t rsp_timeout:1;
		uint64_t rsp_crc_err:1;
		uint64_t rsp_bad_sts:1;
		uint64_t rsp_val:1;
		uint64_t rsp_type:3;
		uint64_t cmd_type:2;
		uint64_t cmd_idx:6;
		uint64_t cmd_done:1;
#else
		uint64_t cmd_done:1;
		uint64_t cmd_idx:6;
		uint64_t cmd_type:2;
		uint64_t rsp_type:3;
		uint64_t rsp_val:1;
		uint64_t rsp_bad_sts:1;
		uint64_t rsp_crc_err:1;
		uint64_t rsp_timeout:1;
		uint64_t stp_val:1;
		uint64_t stp_bad_sts:1;
		uint64_t stp_crc_err:1;
		uint64_t stp_timeout:1;
		uint64_t rsp_busybit:1;
		uint64_t blk_crc_err:1;
		uint64_t blk_timeout:1;
		uint64_t dbuf:1;
		uint64_t reserved_24_27:4;
		uint64_t dbuf_err:1;
		uint64_t reserved_29_55:27;
		uint64_t dma_pend:1;
		uint64_t dma_val:1;
		uint64_t switch_val:1;
		uint64_t cmd_val:1;
		uint64_t bus_id:2;
		uint64_t reserved_62_63:2;
#endif
	} s;
	struct cvmx_mio_emm_rsp_sts_s cn61xx;
	struct cvmx_mio_emm_rsp_sts_s cnf71xx;
};

union cvmx_mio_emm_sample {
	uint64_t u64;
	struct cvmx_mio_emm_sample_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_26_63:38;
		uint64_t cmd_cnt:10;
		uint64_t reserved_10_15:6;
		uint64_t dat_cnt:10;
#else
		uint64_t dat_cnt:10;
		uint64_t reserved_10_15:6;
		uint64_t cmd_cnt:10;
		uint64_t reserved_26_63:38;
#endif
	} s;
	struct cvmx_mio_emm_sample_s cn61xx;
	struct cvmx_mio_emm_sample_s cnf71xx;
};

union cvmx_mio_emm_sts_mask {
	uint64_t u64;
	struct cvmx_mio_emm_sts_mask_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t sts_msk:32;
#else
		uint64_t sts_msk:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_mio_emm_sts_mask_s cn61xx;
	struct cvmx_mio_emm_sts_mask_s cnf71xx;
};

union cvmx_mio_emm_switch {
	uint64_t u64;
	struct cvmx_mio_emm_switch_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_62_63:2;
		uint64_t bus_id:2;
		uint64_t switch_exe:1;
		uint64_t switch_err0:1;
		uint64_t switch_err1:1;
		uint64_t switch_err2:1;
		uint64_t reserved_49_55:7;
		uint64_t hs_timing:1;
		uint64_t reserved_43_47:5;
		uint64_t bus_width:3;
		uint64_t reserved_36_39:4;
		uint64_t power_class:4;
		uint64_t clk_hi:16;
		uint64_t clk_lo:16;
#else
		uint64_t clk_lo:16;
		uint64_t clk_hi:16;
		uint64_t power_class:4;
		uint64_t reserved_36_39:4;
		uint64_t bus_width:3;
		uint64_t reserved_43_47:5;
		uint64_t hs_timing:1;
		uint64_t reserved_49_55:7;
		uint64_t switch_err2:1;
		uint64_t switch_err1:1;
		uint64_t switch_err0:1;
		uint64_t switch_exe:1;
		uint64_t bus_id:2;
		uint64_t reserved_62_63:2;
#endif
	} s;
	struct cvmx_mio_emm_switch_s cn61xx;
	struct cvmx_mio_emm_switch_s cnf71xx;
};

union cvmx_mio_emm_wdog {
	uint64_t u64;
	struct cvmx_mio_emm_wdog_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_26_63:38;
		uint64_t clk_cnt:26;
#else
		uint64_t clk_cnt:26;
		uint64_t reserved_26_63:38;
#endif
	} s;
	struct cvmx_mio_emm_wdog_s cn61xx;
	struct cvmx_mio_emm_wdog_s cnf71xx;
};

union cvmx_mio_fus_bnk_datx {
	uint64_t u64;
	struct cvmx_mio_fus_bnk_datx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t dat:64;
#else
		uint64_t dat:64;
#endif
	} s;
	struct cvmx_mio_fus_bnk_datx_s cn50xx;
	struct cvmx_mio_fus_bnk_datx_s cn52xx;
	struct cvmx_mio_fus_bnk_datx_s cn52xxp1;
	struct cvmx_mio_fus_bnk_datx_s cn56xx;
	struct cvmx_mio_fus_bnk_datx_s cn56xxp1;
	struct cvmx_mio_fus_bnk_datx_s cn58xx;
	struct cvmx_mio_fus_bnk_datx_s cn58xxp1;
	struct cvmx_mio_fus_bnk_datx_s cn61xx;
	struct cvmx_mio_fus_bnk_datx_s cn63xx;
	struct cvmx_mio_fus_bnk_datx_s cn63xxp1;
	struct cvmx_mio_fus_bnk_datx_s cn66xx;
	struct cvmx_mio_fus_bnk_datx_s cn68xx;
	struct cvmx_mio_fus_bnk_datx_s cn68xxp1;
	struct cvmx_mio_fus_bnk_datx_s cnf71xx;
};

union cvmx_mio_fus_dat0 {
	uint64_t u64;
	struct cvmx_mio_fus_dat0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t man_info:32;
#else
		uint64_t man_info:32;
		uint64_t reserved_32_63:32;
#endif
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
	struct cvmx_mio_fus_dat0_s cn61xx;
	struct cvmx_mio_fus_dat0_s cn63xx;
	struct cvmx_mio_fus_dat0_s cn63xxp1;
	struct cvmx_mio_fus_dat0_s cn66xx;
	struct cvmx_mio_fus_dat0_s cn68xx;
	struct cvmx_mio_fus_dat0_s cn68xxp1;
	struct cvmx_mio_fus_dat0_s cnf71xx;
};

union cvmx_mio_fus_dat1 {
	uint64_t u64;
	struct cvmx_mio_fus_dat1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t man_info:32;
#else
		uint64_t man_info:32;
		uint64_t reserved_32_63:32;
#endif
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
	struct cvmx_mio_fus_dat1_s cn61xx;
	struct cvmx_mio_fus_dat1_s cn63xx;
	struct cvmx_mio_fus_dat1_s cn63xxp1;
	struct cvmx_mio_fus_dat1_s cn66xx;
	struct cvmx_mio_fus_dat1_s cn68xx;
	struct cvmx_mio_fus_dat1_s cn68xxp1;
	struct cvmx_mio_fus_dat1_s cnf71xx;
};

union cvmx_mio_fus_dat2 {
	uint64_t u64;
	struct cvmx_mio_fus_dat2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_59_63:5;
		uint64_t run_platform:3;
		uint64_t gbl_pwr_throttle:8;
		uint64_t fus118:1;
		uint64_t rom_info:10;
		uint64_t power_limit:2;
		uint64_t dorm_crypto:1;
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
#else
		uint64_t reserved_0_15:16;
		uint64_t chip_id:8;
		uint64_t bist_dis:1;
		uint64_t rst_sht:1;
		uint64_t nocrypto:1;
		uint64_t nomul:1;
		uint64_t nodfa_cp2:1;
		uint64_t nokasu:1;
		uint64_t reserved_30_31:2;
		uint64_t raid_en:1;
		uint64_t fus318:1;
		uint64_t dorm_crypto:1;
		uint64_t power_limit:2;
		uint64_t rom_info:10;
		uint64_t fus118:1;
		uint64_t gbl_pwr_throttle:8;
		uint64_t run_platform:3;
		uint64_t reserved_59_63:5;
#endif
	} s;
	struct cvmx_mio_fus_dat2_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t pp_dis:1;
		uint64_t reserved_1_11:11;
		uint64_t pll_off:4;
		uint64_t chip_id:8;
		uint64_t bist_dis:1;
		uint64_t rst_sht:1;
		uint64_t nocrypto:1;
		uint64_t nomul:1;
		uint64_t nodfa_cp2:1;
		uint64_t reserved_29_63:35;
#endif
	} cn30xx;
	struct cvmx_mio_fus_dat2_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t pp_dis:2;
		uint64_t reserved_2_11:10;
		uint64_t pll_off:4;
		uint64_t chip_id:8;
		uint64_t bist_dis:1;
		uint64_t rst_sht:1;
		uint64_t nocrypto:1;
		uint64_t nomul:1;
		uint64_t nodfa_cp2:1;
		uint64_t reserved_29_63:35;
#endif
	} cn31xx;
	struct cvmx_mio_fus_dat2_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_29_63:35;
		uint64_t nodfa_cp2:1;
		uint64_t nomul:1;
		uint64_t nocrypto:1;
		uint64_t rst_sht:1;
		uint64_t bist_dis:1;
		uint64_t chip_id:8;
		uint64_t pp_dis:16;
#else
		uint64_t pp_dis:16;
		uint64_t chip_id:8;
		uint64_t bist_dis:1;
		uint64_t rst_sht:1;
		uint64_t nocrypto:1;
		uint64_t nomul:1;
		uint64_t nodfa_cp2:1;
		uint64_t reserved_29_63:35;
#endif
	} cn38xx;
	struct cvmx_mio_fus_dat2_cn38xx cn38xxp2;
	struct cvmx_mio_fus_dat2_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t pp_dis:2;
		uint64_t reserved_2_15:14;
		uint64_t chip_id:8;
		uint64_t bist_dis:1;
		uint64_t rst_sht:1;
		uint64_t nocrypto:1;
		uint64_t nomul:1;
		uint64_t nodfa_cp2:1;
		uint64_t nokasu:1;
		uint64_t reserved_30_31:2;
		uint64_t raid_en:1;
		uint64_t fus318:1;
		uint64_t reserved_34_63:30;
#endif
	} cn50xx;
	struct cvmx_mio_fus_dat2_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t pp_dis:4;
		uint64_t reserved_4_15:12;
		uint64_t chip_id:8;
		uint64_t bist_dis:1;
		uint64_t rst_sht:1;
		uint64_t nocrypto:1;
		uint64_t nomul:1;
		uint64_t nodfa_cp2:1;
		uint64_t nokasu:1;
		uint64_t reserved_30_31:2;
		uint64_t raid_en:1;
		uint64_t fus318:1;
		uint64_t reserved_34_63:30;
#endif
	} cn52xx;
	struct cvmx_mio_fus_dat2_cn52xx cn52xxp1;
	struct cvmx_mio_fus_dat2_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t pp_dis:12;
		uint64_t reserved_12_15:4;
		uint64_t chip_id:8;
		uint64_t bist_dis:1;
		uint64_t rst_sht:1;
		uint64_t nocrypto:1;
		uint64_t nomul:1;
		uint64_t nodfa_cp2:1;
		uint64_t nokasu:1;
		uint64_t reserved_30_31:2;
		uint64_t raid_en:1;
		uint64_t fus318:1;
		uint64_t reserved_34_63:30;
#endif
	} cn56xx;
	struct cvmx_mio_fus_dat2_cn56xx cn56xxp1;
	struct cvmx_mio_fus_dat2_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_30_63:34;
		uint64_t nokasu:1;
		uint64_t nodfa_cp2:1;
		uint64_t nomul:1;
		uint64_t nocrypto:1;
		uint64_t rst_sht:1;
		uint64_t bist_dis:1;
		uint64_t chip_id:8;
		uint64_t pp_dis:16;
#else
		uint64_t pp_dis:16;
		uint64_t chip_id:8;
		uint64_t bist_dis:1;
		uint64_t rst_sht:1;
		uint64_t nocrypto:1;
		uint64_t nomul:1;
		uint64_t nodfa_cp2:1;
		uint64_t nokasu:1;
		uint64_t reserved_30_63:34;
#endif
	} cn58xx;
	struct cvmx_mio_fus_dat2_cn58xx cn58xxp1;
	struct cvmx_mio_fus_dat2_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t fus118:1;
		uint64_t rom_info:10;
		uint64_t power_limit:2;
		uint64_t dorm_crypto:1;
		uint64_t fus318:1;
		uint64_t raid_en:1;
		uint64_t reserved_29_31:3;
		uint64_t nodfa_cp2:1;
		uint64_t nomul:1;
		uint64_t nocrypto:1;
		uint64_t reserved_24_25:2;
		uint64_t chip_id:8;
		uint64_t reserved_4_15:12;
		uint64_t pp_dis:4;
#else
		uint64_t pp_dis:4;
		uint64_t reserved_4_15:12;
		uint64_t chip_id:8;
		uint64_t reserved_24_25:2;
		uint64_t nocrypto:1;
		uint64_t nomul:1;
		uint64_t nodfa_cp2:1;
		uint64_t reserved_29_31:3;
		uint64_t raid_en:1;
		uint64_t fus318:1;
		uint64_t dorm_crypto:1;
		uint64_t power_limit:2;
		uint64_t rom_info:10;
		uint64_t fus118:1;
		uint64_t reserved_48_63:16;
#endif
	} cn61xx;
	struct cvmx_mio_fus_dat2_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_35_63:29;
		uint64_t dorm_crypto:1;
		uint64_t fus318:1;
		uint64_t raid_en:1;
		uint64_t reserved_29_31:3;
		uint64_t nodfa_cp2:1;
		uint64_t nomul:1;
		uint64_t nocrypto:1;
		uint64_t reserved_24_25:2;
		uint64_t chip_id:8;
		uint64_t reserved_6_15:10;
		uint64_t pp_dis:6;
#else
		uint64_t pp_dis:6;
		uint64_t reserved_6_15:10;
		uint64_t chip_id:8;
		uint64_t reserved_24_25:2;
		uint64_t nocrypto:1;
		uint64_t nomul:1;
		uint64_t nodfa_cp2:1;
		uint64_t reserved_29_31:3;
		uint64_t raid_en:1;
		uint64_t fus318:1;
		uint64_t dorm_crypto:1;
		uint64_t reserved_35_63:29;
#endif
	} cn63xx;
	struct cvmx_mio_fus_dat2_cn63xx cn63xxp1;
	struct cvmx_mio_fus_dat2_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t fus118:1;
		uint64_t rom_info:10;
		uint64_t power_limit:2;
		uint64_t dorm_crypto:1;
		uint64_t fus318:1;
		uint64_t raid_en:1;
		uint64_t reserved_29_31:3;
		uint64_t nodfa_cp2:1;
		uint64_t nomul:1;
		uint64_t nocrypto:1;
		uint64_t reserved_24_25:2;
		uint64_t chip_id:8;
		uint64_t reserved_10_15:6;
		uint64_t pp_dis:10;
#else
		uint64_t pp_dis:10;
		uint64_t reserved_10_15:6;
		uint64_t chip_id:8;
		uint64_t reserved_24_25:2;
		uint64_t nocrypto:1;
		uint64_t nomul:1;
		uint64_t nodfa_cp2:1;
		uint64_t reserved_29_31:3;
		uint64_t raid_en:1;
		uint64_t fus318:1;
		uint64_t dorm_crypto:1;
		uint64_t power_limit:2;
		uint64_t rom_info:10;
		uint64_t fus118:1;
		uint64_t reserved_48_63:16;
#endif
	} cn66xx;
	struct cvmx_mio_fus_dat2_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_37_63:27;
		uint64_t power_limit:2;
		uint64_t dorm_crypto:1;
		uint64_t fus318:1;
		uint64_t raid_en:1;
		uint64_t reserved_29_31:3;
		uint64_t nodfa_cp2:1;
		uint64_t nomul:1;
		uint64_t nocrypto:1;
		uint64_t reserved_24_25:2;
		uint64_t chip_id:8;
		uint64_t reserved_0_15:16;
#else
		uint64_t reserved_0_15:16;
		uint64_t chip_id:8;
		uint64_t reserved_24_25:2;
		uint64_t nocrypto:1;
		uint64_t nomul:1;
		uint64_t nodfa_cp2:1;
		uint64_t reserved_29_31:3;
		uint64_t raid_en:1;
		uint64_t fus318:1;
		uint64_t dorm_crypto:1;
		uint64_t power_limit:2;
		uint64_t reserved_37_63:27;
#endif
	} cn68xx;
	struct cvmx_mio_fus_dat2_cn68xx cn68xxp1;
	struct cvmx_mio_fus_dat2_cn70xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t fus118:1;
		uint64_t rom_info:10;
		uint64_t power_limit:2;
		uint64_t dorm_crypto:1;
		uint64_t fus318:1;
		uint64_t raid_en:1;
		uint64_t reserved_31_29:3;
		uint64_t nodfa_cp2:1;
		uint64_t nomul:1;
		uint64_t nocrypto:1;
		uint64_t reserved_25_24:2;
		uint64_t chip_id:8;
		uint64_t reserved_15_0:16;
#else
		uint64_t reserved_15_0:16;
		uint64_t chip_id:8;
		uint64_t reserved_25_24:2;
		uint64_t nocrypto:1;
		uint64_t nomul:1;
		uint64_t nodfa_cp2:1;
		uint64_t reserved_31_29:3;
		uint64_t raid_en:1;
		uint64_t fus318:1;
		uint64_t dorm_crypto:1;
		uint64_t power_limit:2;
		uint64_t rom_info:10;
		uint64_t fus118:1;
		uint64_t reserved_48_63:16;
#endif
	} cn70xx;
	struct cvmx_mio_fus_dat2_cn70xx cn70xxp1;
	struct cvmx_mio_fus_dat2_cn73xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_59_63:5;
		uint64_t run_platform:3;
		uint64_t gbl_pwr_throttle:8;
		uint64_t fus118:1;
		uint64_t rom_info:10;
		uint64_t power_limit:2;
		uint64_t dorm_crypto:1;
		uint64_t fus318:1;
		uint64_t raid_en:1;
		uint64_t reserved_31_29:3;
		uint64_t nodfa_cp2:1;
		uint64_t nomul:1;
		uint64_t nocrypto:1;
		uint64_t reserved_25_24:2;
		uint64_t chip_id:8;
		uint64_t reserved_15_0:16;
#else
		uint64_t reserved_15_0:16;
		uint64_t chip_id:8;
		uint64_t reserved_25_24:2;
		uint64_t nocrypto:1;
		uint64_t nomul:1;
		uint64_t nodfa_cp2:1;
		uint64_t reserved_31_29:3;
		uint64_t raid_en:1;
		uint64_t fus318:1;
		uint64_t dorm_crypto:1;
		uint64_t power_limit:2;
		uint64_t rom_info:10;
		uint64_t fus118:1;
		uint64_t gbl_pwr_throttle:8;
		uint64_t run_platform:3;
		uint64_t reserved_59_63:5;
#endif
	} cn73xx;
	struct cvmx_mio_fus_dat2_cn78xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_59_63:5;
		uint64_t run_platform:3;
		uint64_t reserved_48_55:8;
		uint64_t fus118:1;
		uint64_t rom_info:10;
		uint64_t power_limit:2;
		uint64_t dorm_crypto:1;
		uint64_t fus318:1;
		uint64_t raid_en:1;
		uint64_t reserved_31_29:3;
		uint64_t nodfa_cp2:1;
		uint64_t nomul:1;
		uint64_t nocrypto:1;
		uint64_t reserved_25_24:2;
		uint64_t chip_id:8;
		uint64_t reserved_0_15:16;
#else
		uint64_t reserved_0_15:16;
		uint64_t chip_id:8;
		uint64_t reserved_25_24:2;
		uint64_t nocrypto:1;
		uint64_t nomul:1;
		uint64_t nodfa_cp2:1;
		uint64_t reserved_31_29:3;
		uint64_t raid_en:1;
		uint64_t fus318:1;
		uint64_t dorm_crypto:1;
		uint64_t power_limit:2;
		uint64_t rom_info:10;
		uint64_t fus118:1;
		uint64_t reserved_48_55:8;
		uint64_t run_platform:3;
		uint64_t reserved_59_63:5;
#endif
	} cn78xx;
	struct cvmx_mio_fus_dat2_cn78xxp2 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_59_63:5;
		uint64_t run_platform:3;
		uint64_t gbl_pwr_throttle:8;
		uint64_t fus118:1;
		uint64_t rom_info:10;
		uint64_t power_limit:2;
		uint64_t dorm_crypto:1;
		uint64_t fus318:1;
		uint64_t raid_en:1;
		uint64_t reserved_31_29:3;
		uint64_t nodfa_cp2:1;
		uint64_t nomul:1;
		uint64_t nocrypto:1;
		uint64_t reserved_25_24:2;
		uint64_t chip_id:8;
		uint64_t reserved_0_15:16;
#else
		uint64_t reserved_0_15:16;
		uint64_t chip_id:8;
		uint64_t reserved_25_24:2;
		uint64_t nocrypto:1;
		uint64_t nomul:1;
		uint64_t nodfa_cp2:1;
		uint64_t reserved_31_29:3;
		uint64_t raid_en:1;
		uint64_t fus318:1;
		uint64_t dorm_crypto:1;
		uint64_t power_limit:2;
		uint64_t rom_info:10;
		uint64_t fus118:1;
		uint64_t gbl_pwr_throttle:8;
		uint64_t run_platform:3;
		uint64_t reserved_59_63:5;
#endif
	} cn78xxp2;
	struct cvmx_mio_fus_dat2_cn61xx cnf71xx;
	struct cvmx_mio_fus_dat2_cn73xx cnf75xx;
};

union cvmx_mio_fus_dat3 {
	uint64_t u64;
	struct cvmx_mio_fus_dat3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t ema0:6;
		uint64_t pll_ctl:10;
		uint64_t dfa_info_dte:3;
		uint64_t dfa_info_clm:4;
		uint64_t pll_alt_matrix:1;
		uint64_t reserved_38_39:2;
		uint64_t efus_lck_rsv:1;
		uint64_t efus_lck_man:1;
		uint64_t pll_half_dis:1;
		uint64_t l2c_crip:3;
		uint64_t reserved_28_31:4;
		uint64_t efus_lck:1;
		uint64_t efus_ign:1;
		uint64_t nozip:1;
		uint64_t nodfa_dte:1;
		uint64_t reserved_0_23:24;
#else
		uint64_t reserved_0_23:24;
		uint64_t nodfa_dte:1;
		uint64_t nozip:1;
		uint64_t efus_ign:1;
		uint64_t efus_lck:1;
		uint64_t reserved_28_31:4;
		uint64_t l2c_crip:3;
		uint64_t pll_half_dis:1;
		uint64_t efus_lck_man:1;
		uint64_t efus_lck_rsv:1;
		uint64_t reserved_38_39:2;
		uint64_t pll_alt_matrix:1;
		uint64_t dfa_info_clm:4;
		uint64_t dfa_info_dte:3;
		uint64_t pll_ctl:10;
		uint64_t ema0:6;
#endif
	} s;
	struct cvmx_mio_fus_dat3_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t pll_div4:1;
		uint64_t reserved_29_30:2;
		uint64_t bar2_en:1;
		uint64_t efus_lck:1;
		uint64_t efus_ign:1;
		uint64_t nozip:1;
		uint64_t nodfa_dte:1;
		uint64_t icache:24;
#else
		uint64_t icache:24;
		uint64_t nodfa_dte:1;
		uint64_t nozip:1;
		uint64_t efus_ign:1;
		uint64_t efus_lck:1;
		uint64_t bar2_en:1;
		uint64_t reserved_29_30:2;
		uint64_t pll_div4:1;
		uint64_t reserved_32_63:32;
#endif
	} cn30xx;
	struct cvmx_mio_fus_dat3_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t pll_div4:1;
		uint64_t zip_crip:2;
		uint64_t bar2_en:1;
		uint64_t efus_lck:1;
		uint64_t efus_ign:1;
		uint64_t nozip:1;
		uint64_t nodfa_dte:1;
		uint64_t icache:24;
#else
		uint64_t icache:24;
		uint64_t nodfa_dte:1;
		uint64_t nozip:1;
		uint64_t efus_ign:1;
		uint64_t efus_lck:1;
		uint64_t bar2_en:1;
		uint64_t zip_crip:2;
		uint64_t pll_div4:1;
		uint64_t reserved_32_63:32;
#endif
	} cn31xx;
	struct cvmx_mio_fus_dat3_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_31_63:33;
		uint64_t zip_crip:2;
		uint64_t bar2_en:1;
		uint64_t efus_lck:1;
		uint64_t efus_ign:1;
		uint64_t nozip:1;
		uint64_t nodfa_dte:1;
		uint64_t icache:24;
#else
		uint64_t icache:24;
		uint64_t nodfa_dte:1;
		uint64_t nozip:1;
		uint64_t efus_ign:1;
		uint64_t efus_lck:1;
		uint64_t bar2_en:1;
		uint64_t zip_crip:2;
		uint64_t reserved_31_63:33;
#endif
	} cn38xx;
	struct cvmx_mio_fus_dat3_cn38xxp2 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_29_63:35;
		uint64_t bar2_en:1;
		uint64_t efus_lck:1;
		uint64_t efus_ign:1;
		uint64_t nozip:1;
		uint64_t nodfa_dte:1;
		uint64_t icache:24;
#else
		uint64_t icache:24;
		uint64_t nodfa_dte:1;
		uint64_t nozip:1;
		uint64_t efus_ign:1;
		uint64_t efus_lck:1;
		uint64_t bar2_en:1;
		uint64_t reserved_29_63:35;
#endif
	} cn38xxp2;
	struct cvmx_mio_fus_dat3_cn38xx cn50xx;
	struct cvmx_mio_fus_dat3_cn38xx cn52xx;
	struct cvmx_mio_fus_dat3_cn38xx cn52xxp1;
	struct cvmx_mio_fus_dat3_cn38xx cn56xx;
	struct cvmx_mio_fus_dat3_cn38xx cn56xxp1;
	struct cvmx_mio_fus_dat3_cn38xx cn58xx;
	struct cvmx_mio_fus_dat3_cn38xx cn58xxp1;
	struct cvmx_mio_fus_dat3_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_58_63:6;
		uint64_t pll_ctl:10;
		uint64_t dfa_info_dte:3;
		uint64_t dfa_info_clm:4;
		uint64_t reserved_40_40:1;
		uint64_t ema:2;
		uint64_t efus_lck_rsv:1;
		uint64_t efus_lck_man:1;
		uint64_t pll_half_dis:1;
		uint64_t l2c_crip:3;
		uint64_t reserved_31_31:1;
		uint64_t zip_info:2;
		uint64_t bar2_en:1;
		uint64_t efus_lck:1;
		uint64_t efus_ign:1;
		uint64_t nozip:1;
		uint64_t nodfa_dte:1;
		uint64_t reserved_0_23:24;
#else
		uint64_t reserved_0_23:24;
		uint64_t nodfa_dte:1;
		uint64_t nozip:1;
		uint64_t efus_ign:1;
		uint64_t efus_lck:1;
		uint64_t bar2_en:1;
		uint64_t zip_info:2;
		uint64_t reserved_31_31:1;
		uint64_t l2c_crip:3;
		uint64_t pll_half_dis:1;
		uint64_t efus_lck_man:1;
		uint64_t efus_lck_rsv:1;
		uint64_t ema:2;
		uint64_t reserved_40_40:1;
		uint64_t dfa_info_clm:4;
		uint64_t dfa_info_dte:3;
		uint64_t pll_ctl:10;
		uint64_t reserved_58_63:6;
#endif
	} cn61xx;
	struct cvmx_mio_fus_dat3_cn61xx cn63xx;
	struct cvmx_mio_fus_dat3_cn61xx cn63xxp1;
	struct cvmx_mio_fus_dat3_cn61xx cn66xx;
	struct cvmx_mio_fus_dat3_cn61xx cn68xx;
	struct cvmx_mio_fus_dat3_cn61xx cn68xxp1;
	struct cvmx_mio_fus_dat3_cn70xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t ema0:6;
		uint64_t pll_ctl:10;
		uint64_t dfa_info_dte:3;
		uint64_t dfa_info_clm:4;
		uint64_t pll_alt_matrix:1;
		uint64_t pll_bwadj_denom:2;
		uint64_t efus_lck_rsv:1;
		uint64_t efus_lck_man:1;
		uint64_t pll_half_dis:1;
		uint64_t l2c_crip:3;
		uint64_t use_int_refclk:1;
		uint64_t zip_info:2;
		uint64_t bar2_sz_conf:1;
		uint64_t efus_lck:1;
		uint64_t efus_ign:1;
		uint64_t nozip:1;
		uint64_t nodfa_dte:1;
		uint64_t ema1:6;
		uint64_t reserved_0_17:18;
#else
		uint64_t reserved_0_17:18;
		uint64_t ema1:6;
		uint64_t nodfa_dte:1;
		uint64_t nozip:1;
		uint64_t efus_ign:1;
		uint64_t efus_lck:1;
		uint64_t bar2_sz_conf:1;
		uint64_t zip_info:2;
		uint64_t use_int_refclk:1;
		uint64_t l2c_crip:3;
		uint64_t pll_half_dis:1;
		uint64_t efus_lck_man:1;
		uint64_t efus_lck_rsv:1;
		uint64_t pll_bwadj_denom:2;
		uint64_t pll_alt_matrix:1;
		uint64_t dfa_info_clm:4;
		uint64_t dfa_info_dte:3;
		uint64_t pll_ctl:10;
		uint64_t ema0:6;
#endif
	} cn70xx;
	struct cvmx_mio_fus_dat3_cn70xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t ema0:6;
		uint64_t pll_ctl:10;
		uint64_t dfa_info_dte:3;
		uint64_t dfa_info_clm:4;
		uint64_t reserved_38_40:3;
		uint64_t efus_lck_rsv:1;
		uint64_t efus_lck_man:1;
		uint64_t pll_half_dis:1;
		uint64_t l2c_crip:3;
		uint64_t reserved_31_31:1;
		uint64_t zip_info:2;
		uint64_t bar2_sz_conf:1;
		uint64_t efus_lck:1;
		uint64_t efus_ign:1;
		uint64_t nozip:1;
		uint64_t nodfa_dte:1;
		uint64_t ema1:6;
		uint64_t reserved_0_17:18;
#else
		uint64_t reserved_0_17:18;
		uint64_t ema1:6;
		uint64_t nodfa_dte:1;
		uint64_t nozip:1;
		uint64_t efus_ign:1;
		uint64_t efus_lck:1;
		uint64_t bar2_sz_conf:1;
		uint64_t zip_info:2;
		uint64_t reserved_31_31:1;
		uint64_t l2c_crip:3;
		uint64_t pll_half_dis:1;
		uint64_t efus_lck_man:1;
		uint64_t efus_lck_rsv:1;
		uint64_t reserved_38_40:3;
		uint64_t dfa_info_clm:4;
		uint64_t dfa_info_dte:3;
		uint64_t pll_ctl:10;
		uint64_t ema0:6;
#endif
	} cn70xxp1;
	struct cvmx_mio_fus_dat3_cn73xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t ema0:6;
		uint64_t pll_ctl:10;
		uint64_t dfa_info_dte:3;
		uint64_t dfa_info_clm:4;
		uint64_t pll_alt_matrix:1;
		uint64_t pll_bwadj_denom:2;
		uint64_t efus_lck_rsv:1;
		uint64_t efus_lck_man:1;
		uint64_t pll_half_dis:1;
		uint64_t l2c_crip:3;
		uint64_t use_int_refclk:1;
		uint64_t zip_info:2;
		uint64_t bar2_sz_conf:1;
		uint64_t efus_lck:1;
		uint64_t efus_ign:1;
		uint64_t nozip:1;
		uint64_t nodfa_dte:1;
		uint64_t ema1:6;
		uint64_t nohna_dte:1;
		uint64_t hna_info_dte:3;
		uint64_t hna_info_clm:4;
		uint64_t reserved_9_9:1;
		uint64_t core_pll_mul:5;
		uint64_t pnr_pll_mul:4;
#else
		uint64_t pnr_pll_mul:4;
		uint64_t core_pll_mul:5;
		uint64_t reserved_9_9:1;
		uint64_t hna_info_clm:4;
		uint64_t hna_info_dte:3;
		uint64_t nohna_dte:1;
		uint64_t ema1:6;
		uint64_t nodfa_dte:1;
		uint64_t nozip:1;
		uint64_t efus_ign:1;
		uint64_t efus_lck:1;
		uint64_t bar2_sz_conf:1;
		uint64_t zip_info:2;
		uint64_t use_int_refclk:1;
		uint64_t l2c_crip:3;
		uint64_t pll_half_dis:1;
		uint64_t efus_lck_man:1;
		uint64_t efus_lck_rsv:1;
		uint64_t pll_bwadj_denom:2;
		uint64_t pll_alt_matrix:1;
		uint64_t dfa_info_clm:4;
		uint64_t dfa_info_dte:3;
		uint64_t pll_ctl:10;
		uint64_t ema0:6;
#endif
	} cn73xx;
	struct cvmx_mio_fus_dat3_cn78xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t ema0:6;
		uint64_t pll_ctl:10;
		uint64_t dfa_info_dte:3;
		uint64_t dfa_info_clm:4;
		uint64_t reserved_38_40:3;
		uint64_t efus_lck_rsv:1;
		uint64_t efus_lck_man:1;
		uint64_t pll_half_dis:1;
		uint64_t l2c_crip:3;
		uint64_t reserved_31_31:1;
		uint64_t zip_info:2;
		uint64_t bar2_sz_conf:1;
		uint64_t efus_lck:1;
		uint64_t efus_ign:1;
		uint64_t nozip:1;
		uint64_t nodfa_dte:1;
		uint64_t ema1:6;
		uint64_t nohna_dte:1;
		uint64_t hna_info_dte:3;
		uint64_t hna_info_clm:4;
		uint64_t reserved_0_9:10;
#else
		uint64_t reserved_0_9:10;
		uint64_t hna_info_clm:4;
		uint64_t hna_info_dte:3;
		uint64_t nohna_dte:1;
		uint64_t ema1:6;
		uint64_t nodfa_dte:1;
		uint64_t nozip:1;
		uint64_t efus_ign:1;
		uint64_t efus_lck:1;
		uint64_t bar2_sz_conf:1;
		uint64_t zip_info:2;
		uint64_t reserved_31_31:1;
		uint64_t l2c_crip:3;
		uint64_t pll_half_dis:1;
		uint64_t efus_lck_man:1;
		uint64_t efus_lck_rsv:1;
		uint64_t reserved_38_40:3;
		uint64_t dfa_info_clm:4;
		uint64_t dfa_info_dte:3;
		uint64_t pll_ctl:10;
		uint64_t ema0:6;
#endif
	} cn78xx;
	struct cvmx_mio_fus_dat3_cn73xx cn78xxp2;
	struct cvmx_mio_fus_dat3_cn61xx cnf71xx;
	struct cvmx_mio_fus_dat3_cnf75xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t ema0:6;
		uint64_t pll_ctl:10;
		uint64_t dfa_info_dte:3;
		uint64_t dfa_info_clm:4;
		uint64_t pll_alt_matrix:1;
		uint64_t pll_bwadj_denom:2;
		uint64_t efus_lck_rsv:1;
		uint64_t efus_lck_man:1;
		uint64_t pll_half_dis:1;
		uint64_t l2c_crip:3;
		uint64_t use_int_refclk:1;
		uint64_t zip_info:2;
		uint64_t bar2_sz_conf:1;
		uint64_t efus_lck:1;
		uint64_t efus_ign:1;
		uint64_t nozip:1;
		uint64_t nodfa_dte:1;
		uint64_t ema1:6;
		uint64_t reserved_9_17:9;
		uint64_t core_pll_mul:5;
		uint64_t pnr_pll_mul:4;
#else
		uint64_t pnr_pll_mul:4;
		uint64_t core_pll_mul:5;
		uint64_t reserved_9_17:9;
		uint64_t ema1:6;
		uint64_t nodfa_dte:1;
		uint64_t nozip:1;
		uint64_t efus_ign:1;
		uint64_t efus_lck:1;
		uint64_t bar2_sz_conf:1;
		uint64_t zip_info:2;
		uint64_t use_int_refclk:1;
		uint64_t l2c_crip:3;
		uint64_t pll_half_dis:1;
		uint64_t efus_lck_man:1;
		uint64_t efus_lck_rsv:1;
		uint64_t pll_bwadj_denom:2;
		uint64_t pll_alt_matrix:1;
		uint64_t dfa_info_clm:4;
		uint64_t dfa_info_dte:3;
		uint64_t pll_ctl:10;
		uint64_t ema0:6;
#endif
	} cnf75xx;
};

union cvmx_mio_fus_ema {
	uint64_t u64;
	struct cvmx_mio_fus_ema_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t eff_ema:3;
		uint64_t reserved_3_3:1;
		uint64_t ema:3;
#else
		uint64_t ema:3;
		uint64_t reserved_3_3:1;
		uint64_t eff_ema:3;
		uint64_t reserved_7_63:57;
#endif
	} s;
	struct cvmx_mio_fus_ema_s cn50xx;
	struct cvmx_mio_fus_ema_s cn52xx;
	struct cvmx_mio_fus_ema_s cn52xxp1;
	struct cvmx_mio_fus_ema_s cn56xx;
	struct cvmx_mio_fus_ema_s cn56xxp1;
	struct cvmx_mio_fus_ema_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t ema:2;
#else
		uint64_t ema:2;
		uint64_t reserved_2_63:62;
#endif
	} cn58xx;
	struct cvmx_mio_fus_ema_cn58xx cn58xxp1;
	struct cvmx_mio_fus_ema_s cn61xx;
	struct cvmx_mio_fus_ema_s cn63xx;
	struct cvmx_mio_fus_ema_s cn63xxp1;
	struct cvmx_mio_fus_ema_s cn66xx;
	struct cvmx_mio_fus_ema_s cn68xx;
	struct cvmx_mio_fus_ema_s cn68xxp1;
	struct cvmx_mio_fus_ema_s cnf71xx;
};

union cvmx_mio_fus_pdf {
	uint64_t u64;
	struct cvmx_mio_fus_pdf_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t pdf:64;
#else
		uint64_t pdf:64;
#endif
	} s;
	struct cvmx_mio_fus_pdf_s cn50xx;
	struct cvmx_mio_fus_pdf_s cn52xx;
	struct cvmx_mio_fus_pdf_s cn52xxp1;
	struct cvmx_mio_fus_pdf_s cn56xx;
	struct cvmx_mio_fus_pdf_s cn56xxp1;
	struct cvmx_mio_fus_pdf_s cn58xx;
	struct cvmx_mio_fus_pdf_s cn61xx;
	struct cvmx_mio_fus_pdf_s cn63xx;
	struct cvmx_mio_fus_pdf_s cn63xxp1;
	struct cvmx_mio_fus_pdf_s cn66xx;
	struct cvmx_mio_fus_pdf_s cn68xx;
	struct cvmx_mio_fus_pdf_s cn68xxp1;
	struct cvmx_mio_fus_pdf_s cnf71xx;
};

union cvmx_mio_fus_pll {
	uint64_t u64;
	struct cvmx_mio_fus_pll_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t rclk_align_r:8;
		uint64_t rclk_align_l:8;
		uint64_t reserved_8_31:24;
		uint64_t c_cout_rst:1;
		uint64_t c_cout_sel:2;
		uint64_t pnr_cout_rst:1;
		uint64_t pnr_cout_sel:2;
		uint64_t rfslip:1;
		uint64_t fbslip:1;
#else
		uint64_t fbslip:1;
		uint64_t rfslip:1;
		uint64_t pnr_cout_sel:2;
		uint64_t pnr_cout_rst:1;
		uint64_t c_cout_sel:2;
		uint64_t c_cout_rst:1;
		uint64_t reserved_8_31:24;
		uint64_t rclk_align_l:8;
		uint64_t rclk_align_r:8;
		uint64_t reserved_48_63:16;
#endif
	} s;
	struct cvmx_mio_fus_pll_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t rfslip:1;
		uint64_t fbslip:1;
#else
		uint64_t fbslip:1;
		uint64_t rfslip:1;
		uint64_t reserved_2_63:62;
#endif
	} cn50xx;
	struct cvmx_mio_fus_pll_cn50xx cn52xx;
	struct cvmx_mio_fus_pll_cn50xx cn52xxp1;
	struct cvmx_mio_fus_pll_cn50xx cn56xx;
	struct cvmx_mio_fus_pll_cn50xx cn56xxp1;
	struct cvmx_mio_fus_pll_cn50xx cn58xx;
	struct cvmx_mio_fus_pll_cn50xx cn58xxp1;
	struct cvmx_mio_fus_pll_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t c_cout_rst:1;
		uint64_t c_cout_sel:2;
		uint64_t pnr_cout_rst:1;
		uint64_t pnr_cout_sel:2;
		uint64_t rfslip:1;
		uint64_t fbslip:1;
#else
		uint64_t fbslip:1;
		uint64_t rfslip:1;
		uint64_t pnr_cout_sel:2;
		uint64_t pnr_cout_rst:1;
		uint64_t c_cout_sel:2;
		uint64_t c_cout_rst:1;
		uint64_t reserved_8_63:56;
#endif
	} cn61xx;
	struct cvmx_mio_fus_pll_cn61xx cn63xx;
	struct cvmx_mio_fus_pll_cn61xx cn63xxp1;
	struct cvmx_mio_fus_pll_cn61xx cn66xx;
	struct cvmx_mio_fus_pll_s cn68xx;
	struct cvmx_mio_fus_pll_s cn68xxp1;
	struct cvmx_mio_fus_pll_cn61xx cnf71xx;
};

union cvmx_mio_fus_prog {
	uint64_t u64;
	struct cvmx_mio_fus_prog_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t soft:1;
		uint64_t prog:1;
#else
		uint64_t prog:1;
		uint64_t soft:1;
		uint64_t reserved_2_63:62;
#endif
	} s;
	struct cvmx_mio_fus_prog_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t prog:1;
#else
		uint64_t prog:1;
		uint64_t reserved_1_63:63;
#endif
	} cn30xx;
	struct cvmx_mio_fus_prog_cn30xx cn31xx;
	struct cvmx_mio_fus_prog_cn30xx cn38xx;
	struct cvmx_mio_fus_prog_cn30xx cn38xxp2;
	struct cvmx_mio_fus_prog_cn30xx cn50xx;
	struct cvmx_mio_fus_prog_cn30xx cn52xx;
	struct cvmx_mio_fus_prog_cn30xx cn52xxp1;
	struct cvmx_mio_fus_prog_cn30xx cn56xx;
	struct cvmx_mio_fus_prog_cn30xx cn56xxp1;
	struct cvmx_mio_fus_prog_cn30xx cn58xx;
	struct cvmx_mio_fus_prog_cn30xx cn58xxp1;
	struct cvmx_mio_fus_prog_s cn61xx;
	struct cvmx_mio_fus_prog_s cn63xx;
	struct cvmx_mio_fus_prog_s cn63xxp1;
	struct cvmx_mio_fus_prog_s cn66xx;
	struct cvmx_mio_fus_prog_s cn68xx;
	struct cvmx_mio_fus_prog_s cn68xxp1;
	struct cvmx_mio_fus_prog_s cnf71xx;
};

union cvmx_mio_fus_prog_times {
	uint64_t u64;
	struct cvmx_mio_fus_prog_times_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_35_63:29;
		uint64_t vgate_pin:1;
		uint64_t fsrc_pin:1;
		uint64_t prog_pin:1;
		uint64_t reserved_6_31:26;
		uint64_t setup:6;
#else
		uint64_t setup:6;
		uint64_t reserved_6_31:26;
		uint64_t prog_pin:1;
		uint64_t fsrc_pin:1;
		uint64_t vgate_pin:1;
		uint64_t reserved_35_63:29;
#endif
	} s;
	struct cvmx_mio_fus_prog_times_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_33_63:31;
		uint64_t prog_pin:1;
		uint64_t out:8;
		uint64_t sclk_lo:4;
		uint64_t sclk_hi:12;
		uint64_t setup:8;
#else
		uint64_t setup:8;
		uint64_t sclk_hi:12;
		uint64_t sclk_lo:4;
		uint64_t out:8;
		uint64_t prog_pin:1;
		uint64_t reserved_33_63:31;
#endif
	} cn50xx;
	struct cvmx_mio_fus_prog_times_cn50xx cn52xx;
	struct cvmx_mio_fus_prog_times_cn50xx cn52xxp1;
	struct cvmx_mio_fus_prog_times_cn50xx cn56xx;
	struct cvmx_mio_fus_prog_times_cn50xx cn56xxp1;
	struct cvmx_mio_fus_prog_times_cn50xx cn58xx;
	struct cvmx_mio_fus_prog_times_cn50xx cn58xxp1;
	struct cvmx_mio_fus_prog_times_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_35_63:29;
		uint64_t vgate_pin:1;
		uint64_t fsrc_pin:1;
		uint64_t prog_pin:1;
		uint64_t out:7;
		uint64_t sclk_lo:4;
		uint64_t sclk_hi:15;
		uint64_t setup:6;
#else
		uint64_t setup:6;
		uint64_t sclk_hi:15;
		uint64_t sclk_lo:4;
		uint64_t out:7;
		uint64_t prog_pin:1;
		uint64_t fsrc_pin:1;
		uint64_t vgate_pin:1;
		uint64_t reserved_35_63:29;
#endif
	} cn61xx;
	struct cvmx_mio_fus_prog_times_cn61xx cn63xx;
	struct cvmx_mio_fus_prog_times_cn61xx cn63xxp1;
	struct cvmx_mio_fus_prog_times_cn61xx cn66xx;
	struct cvmx_mio_fus_prog_times_cn61xx cn68xx;
	struct cvmx_mio_fus_prog_times_cn61xx cn68xxp1;
	struct cvmx_mio_fus_prog_times_cn61xx cnf71xx;
};

union cvmx_mio_fus_rcmd {
	uint64_t u64;
	struct cvmx_mio_fus_rcmd_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_24_63:40;
		uint64_t dat:8;
		uint64_t reserved_13_15:3;
		uint64_t pend:1;
		uint64_t reserved_9_11:3;
		uint64_t efuse:1;
		uint64_t addr:8;
#else
		uint64_t addr:8;
		uint64_t efuse:1;
		uint64_t reserved_9_11:3;
		uint64_t pend:1;
		uint64_t reserved_13_15:3;
		uint64_t dat:8;
		uint64_t reserved_24_63:40;
#endif
	} s;
	struct cvmx_mio_fus_rcmd_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_24_63:40;
		uint64_t dat:8;
		uint64_t reserved_13_15:3;
		uint64_t pend:1;
		uint64_t reserved_9_11:3;
		uint64_t efuse:1;
		uint64_t reserved_7_7:1;
		uint64_t addr:7;
#else
		uint64_t addr:7;
		uint64_t reserved_7_7:1;
		uint64_t efuse:1;
		uint64_t reserved_9_11:3;
		uint64_t pend:1;
		uint64_t reserved_13_15:3;
		uint64_t dat:8;
		uint64_t reserved_24_63:40;
#endif
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
	struct cvmx_mio_fus_rcmd_s cn61xx;
	struct cvmx_mio_fus_rcmd_s cn63xx;
	struct cvmx_mio_fus_rcmd_s cn63xxp1;
	struct cvmx_mio_fus_rcmd_s cn66xx;
	struct cvmx_mio_fus_rcmd_s cn68xx;
	struct cvmx_mio_fus_rcmd_s cn68xxp1;
	struct cvmx_mio_fus_rcmd_s cnf71xx;
};

union cvmx_mio_fus_read_times {
	uint64_t u64;
	struct cvmx_mio_fus_read_times_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_26_63:38;
		uint64_t sch:4;
		uint64_t fsh:4;
		uint64_t prh:4;
		uint64_t sdh:4;
		uint64_t setup:10;
#else
		uint64_t setup:10;
		uint64_t sdh:4;
		uint64_t prh:4;
		uint64_t fsh:4;
		uint64_t sch:4;
		uint64_t reserved_26_63:38;
#endif
	} s;
	struct cvmx_mio_fus_read_times_s cn61xx;
	struct cvmx_mio_fus_read_times_s cn63xx;
	struct cvmx_mio_fus_read_times_s cn63xxp1;
	struct cvmx_mio_fus_read_times_s cn66xx;
	struct cvmx_mio_fus_read_times_s cn68xx;
	struct cvmx_mio_fus_read_times_s cn68xxp1;
	struct cvmx_mio_fus_read_times_s cnf71xx;
};

union cvmx_mio_fus_repair_res0 {
	uint64_t u64;
	struct cvmx_mio_fus_repair_res0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_55_63:9;
		uint64_t too_many:1;
		uint64_t repair2:18;
		uint64_t repair1:18;
		uint64_t repair0:18;
#else
		uint64_t repair0:18;
		uint64_t repair1:18;
		uint64_t repair2:18;
		uint64_t too_many:1;
		uint64_t reserved_55_63:9;
#endif
	} s;
	struct cvmx_mio_fus_repair_res0_s cn61xx;
	struct cvmx_mio_fus_repair_res0_s cn63xx;
	struct cvmx_mio_fus_repair_res0_s cn63xxp1;
	struct cvmx_mio_fus_repair_res0_s cn66xx;
	struct cvmx_mio_fus_repair_res0_s cn68xx;
	struct cvmx_mio_fus_repair_res0_s cn68xxp1;
	struct cvmx_mio_fus_repair_res0_s cnf71xx;
};

union cvmx_mio_fus_repair_res1 {
	uint64_t u64;
	struct cvmx_mio_fus_repair_res1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t repair5:18;
		uint64_t repair4:18;
		uint64_t repair3:18;
#else
		uint64_t repair3:18;
		uint64_t repair4:18;
		uint64_t repair5:18;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_mio_fus_repair_res1_s cn61xx;
	struct cvmx_mio_fus_repair_res1_s cn63xx;
	struct cvmx_mio_fus_repair_res1_s cn63xxp1;
	struct cvmx_mio_fus_repair_res1_s cn66xx;
	struct cvmx_mio_fus_repair_res1_s cn68xx;
	struct cvmx_mio_fus_repair_res1_s cn68xxp1;
	struct cvmx_mio_fus_repair_res1_s cnf71xx;
};

union cvmx_mio_fus_repair_res2 {
	uint64_t u64;
	struct cvmx_mio_fus_repair_res2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_18_63:46;
		uint64_t repair6:18;
#else
		uint64_t repair6:18;
		uint64_t reserved_18_63:46;
#endif
	} s;
	struct cvmx_mio_fus_repair_res2_s cn61xx;
	struct cvmx_mio_fus_repair_res2_s cn63xx;
	struct cvmx_mio_fus_repair_res2_s cn63xxp1;
	struct cvmx_mio_fus_repair_res2_s cn66xx;
	struct cvmx_mio_fus_repair_res2_s cn68xx;
	struct cvmx_mio_fus_repair_res2_s cn68xxp1;
	struct cvmx_mio_fus_repair_res2_s cnf71xx;
};

union cvmx_mio_fus_spr_repair_res {
	uint64_t u64;
	struct cvmx_mio_fus_spr_repair_res_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_42_63:22;
		uint64_t repair2:14;
		uint64_t repair1:14;
		uint64_t repair0:14;
#else
		uint64_t repair0:14;
		uint64_t repair1:14;
		uint64_t repair2:14;
		uint64_t reserved_42_63:22;
#endif
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
	struct cvmx_mio_fus_spr_repair_res_s cn61xx;
	struct cvmx_mio_fus_spr_repair_res_s cn63xx;
	struct cvmx_mio_fus_spr_repair_res_s cn63xxp1;
	struct cvmx_mio_fus_spr_repair_res_s cn66xx;
	struct cvmx_mio_fus_spr_repair_res_s cn68xx;
	struct cvmx_mio_fus_spr_repair_res_s cn68xxp1;
	struct cvmx_mio_fus_spr_repair_res_s cnf71xx;
};

union cvmx_mio_fus_spr_repair_sum {
	uint64_t u64;
	struct cvmx_mio_fus_spr_repair_sum_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t too_many:1;
#else
		uint64_t too_many:1;
		uint64_t reserved_1_63:63;
#endif
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
	struct cvmx_mio_fus_spr_repair_sum_s cn61xx;
	struct cvmx_mio_fus_spr_repair_sum_s cn63xx;
	struct cvmx_mio_fus_spr_repair_sum_s cn63xxp1;
	struct cvmx_mio_fus_spr_repair_sum_s cn66xx;
	struct cvmx_mio_fus_spr_repair_sum_s cn68xx;
	struct cvmx_mio_fus_spr_repair_sum_s cn68xxp1;
	struct cvmx_mio_fus_spr_repair_sum_s cnf71xx;
};

union cvmx_mio_fus_tgg {
	uint64_t u64;
	struct cvmx_mio_fus_tgg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t val:1;
		uint64_t dat:63;
#else
		uint64_t dat:63;
		uint64_t val:1;
#endif
	} s;
	struct cvmx_mio_fus_tgg_s cn61xx;
	struct cvmx_mio_fus_tgg_s cn66xx;
	struct cvmx_mio_fus_tgg_s cnf71xx;
};

union cvmx_mio_fus_unlock {
	uint64_t u64;
	struct cvmx_mio_fus_unlock_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_24_63:40;
		uint64_t key:24;
#else
		uint64_t key:24;
		uint64_t reserved_24_63:40;
#endif
	} s;
	struct cvmx_mio_fus_unlock_s cn30xx;
	struct cvmx_mio_fus_unlock_s cn31xx;
};

union cvmx_mio_fus_wadr {
	uint64_t u64;
	struct cvmx_mio_fus_wadr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t addr:10;
#else
		uint64_t addr:10;
		uint64_t reserved_10_63:54;
#endif
	} s;
	struct cvmx_mio_fus_wadr_s cn30xx;
	struct cvmx_mio_fus_wadr_s cn31xx;
	struct cvmx_mio_fus_wadr_s cn38xx;
	struct cvmx_mio_fus_wadr_s cn38xxp2;
	struct cvmx_mio_fus_wadr_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t addr:2;
#else
		uint64_t addr:2;
		uint64_t reserved_2_63:62;
#endif
	} cn50xx;
	struct cvmx_mio_fus_wadr_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t addr:3;
#else
		uint64_t addr:3;
		uint64_t reserved_3_63:61;
#endif
	} cn52xx;
	struct cvmx_mio_fus_wadr_cn52xx cn52xxp1;
	struct cvmx_mio_fus_wadr_cn52xx cn56xx;
	struct cvmx_mio_fus_wadr_cn52xx cn56xxp1;
	struct cvmx_mio_fus_wadr_cn50xx cn58xx;
	struct cvmx_mio_fus_wadr_cn50xx cn58xxp1;
	struct cvmx_mio_fus_wadr_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t addr:4;
#else
		uint64_t addr:4;
		uint64_t reserved_4_63:60;
#endif
	} cn61xx;
	struct cvmx_mio_fus_wadr_cn61xx cn63xx;
	struct cvmx_mio_fus_wadr_cn61xx cn63xxp1;
	struct cvmx_mio_fus_wadr_cn61xx cn66xx;
	struct cvmx_mio_fus_wadr_cn61xx cn68xx;
	struct cvmx_mio_fus_wadr_cn61xx cn68xxp1;
	struct cvmx_mio_fus_wadr_cn61xx cnf71xx;
};

union cvmx_mio_gpio_comp {
	uint64_t u64;
	struct cvmx_mio_gpio_comp_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t pctl:6;
		uint64_t nctl:6;
#else
		uint64_t nctl:6;
		uint64_t pctl:6;
		uint64_t reserved_12_63:52;
#endif
	} s;
	struct cvmx_mio_gpio_comp_s cn61xx;
	struct cvmx_mio_gpio_comp_s cn63xx;
	struct cvmx_mio_gpio_comp_s cn63xxp1;
	struct cvmx_mio_gpio_comp_s cn66xx;
	struct cvmx_mio_gpio_comp_s cn68xx;
	struct cvmx_mio_gpio_comp_s cn68xxp1;
	struct cvmx_mio_gpio_comp_s cnf71xx;
};

union cvmx_mio_ndf_dma_cfg {
	uint64_t u64;
	struct cvmx_mio_ndf_dma_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t adr:36;
		uint64_t size:20;
		uint64_t endian:1;
		uint64_t swap8:1;
		uint64_t swap16:1;
		uint64_t swap32:1;
		uint64_t reserved_60_60:1;
		uint64_t clr:1;
		uint64_t rw:1;
		uint64_t en:1;
#endif
	} s;
	struct cvmx_mio_ndf_dma_cfg_s cn52xx;
	struct cvmx_mio_ndf_dma_cfg_s cn61xx;
	struct cvmx_mio_ndf_dma_cfg_s cn63xx;
	struct cvmx_mio_ndf_dma_cfg_s cn63xxp1;
	struct cvmx_mio_ndf_dma_cfg_s cn66xx;
	struct cvmx_mio_ndf_dma_cfg_s cn68xx;
	struct cvmx_mio_ndf_dma_cfg_s cn68xxp1;
	struct cvmx_mio_ndf_dma_cfg_s cnf71xx;
};

union cvmx_mio_ndf_dma_int {
	uint64_t u64;
	struct cvmx_mio_ndf_dma_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t done:1;
#else
		uint64_t done:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_mio_ndf_dma_int_s cn52xx;
	struct cvmx_mio_ndf_dma_int_s cn61xx;
	struct cvmx_mio_ndf_dma_int_s cn63xx;
	struct cvmx_mio_ndf_dma_int_s cn63xxp1;
	struct cvmx_mio_ndf_dma_int_s cn66xx;
	struct cvmx_mio_ndf_dma_int_s cn68xx;
	struct cvmx_mio_ndf_dma_int_s cn68xxp1;
	struct cvmx_mio_ndf_dma_int_s cnf71xx;
};

union cvmx_mio_ndf_dma_int_en {
	uint64_t u64;
	struct cvmx_mio_ndf_dma_int_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t done:1;
#else
		uint64_t done:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_mio_ndf_dma_int_en_s cn52xx;
	struct cvmx_mio_ndf_dma_int_en_s cn61xx;
	struct cvmx_mio_ndf_dma_int_en_s cn63xx;
	struct cvmx_mio_ndf_dma_int_en_s cn63xxp1;
	struct cvmx_mio_ndf_dma_int_en_s cn66xx;
	struct cvmx_mio_ndf_dma_int_en_s cn68xx;
	struct cvmx_mio_ndf_dma_int_en_s cn68xxp1;
	struct cvmx_mio_ndf_dma_int_en_s cnf71xx;
};

union cvmx_mio_pll_ctl {
	uint64_t u64;
	struct cvmx_mio_pll_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_5_63:59;
		uint64_t bw_ctl:5;
#else
		uint64_t bw_ctl:5;
		uint64_t reserved_5_63:59;
#endif
	} s;
	struct cvmx_mio_pll_ctl_s cn30xx;
	struct cvmx_mio_pll_ctl_s cn31xx;
};

union cvmx_mio_pll_setting {
	uint64_t u64;
	struct cvmx_mio_pll_setting_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_17_63:47;
		uint64_t setting:17;
#else
		uint64_t setting:17;
		uint64_t reserved_17_63:47;
#endif
	} s;
	struct cvmx_mio_pll_setting_s cn30xx;
	struct cvmx_mio_pll_setting_s cn31xx;
};

union cvmx_mio_ptp_ckout_hi_incr {
	uint64_t u64;
	struct cvmx_mio_ptp_ckout_hi_incr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t nanosec:32;
		uint64_t frnanosec:32;
#else
		uint64_t frnanosec:32;
		uint64_t nanosec:32;
#endif
	} s;
	struct cvmx_mio_ptp_ckout_hi_incr_s cn61xx;
	struct cvmx_mio_ptp_ckout_hi_incr_s cn66xx;
	struct cvmx_mio_ptp_ckout_hi_incr_s cn68xx;
	struct cvmx_mio_ptp_ckout_hi_incr_s cnf71xx;
};

union cvmx_mio_ptp_ckout_lo_incr {
	uint64_t u64;
	struct cvmx_mio_ptp_ckout_lo_incr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t nanosec:32;
		uint64_t frnanosec:32;
#else
		uint64_t frnanosec:32;
		uint64_t nanosec:32;
#endif
	} s;
	struct cvmx_mio_ptp_ckout_lo_incr_s cn61xx;
	struct cvmx_mio_ptp_ckout_lo_incr_s cn66xx;
	struct cvmx_mio_ptp_ckout_lo_incr_s cn68xx;
	struct cvmx_mio_ptp_ckout_lo_incr_s cnf71xx;
};

union cvmx_mio_ptp_ckout_thresh_hi {
	uint64_t u64;
	struct cvmx_mio_ptp_ckout_thresh_hi_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t nanosec:64;
#else
		uint64_t nanosec:64;
#endif
	} s;
	struct cvmx_mio_ptp_ckout_thresh_hi_s cn61xx;
	struct cvmx_mio_ptp_ckout_thresh_hi_s cn66xx;
	struct cvmx_mio_ptp_ckout_thresh_hi_s cn68xx;
	struct cvmx_mio_ptp_ckout_thresh_hi_s cnf71xx;
};

union cvmx_mio_ptp_ckout_thresh_lo {
	uint64_t u64;
	struct cvmx_mio_ptp_ckout_thresh_lo_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t frnanosec:32;
#else
		uint64_t frnanosec:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_mio_ptp_ckout_thresh_lo_s cn61xx;
	struct cvmx_mio_ptp_ckout_thresh_lo_s cn66xx;
	struct cvmx_mio_ptp_ckout_thresh_lo_s cn68xx;
	struct cvmx_mio_ptp_ckout_thresh_lo_s cnf71xx;
};

union cvmx_mio_ptp_clock_cfg {
	uint64_t u64;
	struct cvmx_mio_ptp_clock_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_42_63:22;
		uint64_t pps:1;
		uint64_t ckout:1;
		uint64_t ext_clk_edge:2;
		uint64_t ckout_out4:1;
		uint64_t pps_out:5;
		uint64_t pps_inv:1;
		uint64_t pps_en:1;
		uint64_t ckout_out:4;
		uint64_t ckout_inv:1;
		uint64_t ckout_en:1;
		uint64_t evcnt_in:6;
		uint64_t evcnt_edge:1;
		uint64_t evcnt_en:1;
		uint64_t tstmp_in:6;
		uint64_t tstmp_edge:1;
		uint64_t tstmp_en:1;
		uint64_t ext_clk_in:6;
		uint64_t ext_clk_en:1;
		uint64_t ptp_en:1;
#else
		uint64_t ptp_en:1;
		uint64_t ext_clk_en:1;
		uint64_t ext_clk_in:6;
		uint64_t tstmp_en:1;
		uint64_t tstmp_edge:1;
		uint64_t tstmp_in:6;
		uint64_t evcnt_en:1;
		uint64_t evcnt_edge:1;
		uint64_t evcnt_in:6;
		uint64_t ckout_en:1;
		uint64_t ckout_inv:1;
		uint64_t ckout_out:4;
		uint64_t pps_en:1;
		uint64_t pps_inv:1;
		uint64_t pps_out:5;
		uint64_t ckout_out4:1;
		uint64_t ext_clk_edge:2;
		uint64_t ckout:1;
		uint64_t pps:1;
		uint64_t reserved_42_63:22;
#endif
	} s;
	struct cvmx_mio_ptp_clock_cfg_s cn61xx;
	struct cvmx_mio_ptp_clock_cfg_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_24_63:40;
		uint64_t evcnt_in:6;
		uint64_t evcnt_edge:1;
		uint64_t evcnt_en:1;
		uint64_t tstmp_in:6;
		uint64_t tstmp_edge:1;
		uint64_t tstmp_en:1;
		uint64_t ext_clk_in:6;
		uint64_t ext_clk_en:1;
		uint64_t ptp_en:1;
#else
		uint64_t ptp_en:1;
		uint64_t ext_clk_en:1;
		uint64_t ext_clk_in:6;
		uint64_t tstmp_en:1;
		uint64_t tstmp_edge:1;
		uint64_t tstmp_in:6;
		uint64_t evcnt_en:1;
		uint64_t evcnt_edge:1;
		uint64_t evcnt_in:6;
		uint64_t reserved_24_63:40;
#endif
	} cn63xx;
	struct cvmx_mio_ptp_clock_cfg_cn63xx cn63xxp1;
	struct cvmx_mio_ptp_clock_cfg_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_40_63:24;
		uint64_t ext_clk_edge:2;
		uint64_t ckout_out4:1;
		uint64_t pps_out:5;
		uint64_t pps_inv:1;
		uint64_t pps_en:1;
		uint64_t ckout_out:4;
		uint64_t ckout_inv:1;
		uint64_t ckout_en:1;
		uint64_t evcnt_in:6;
		uint64_t evcnt_edge:1;
		uint64_t evcnt_en:1;
		uint64_t tstmp_in:6;
		uint64_t tstmp_edge:1;
		uint64_t tstmp_en:1;
		uint64_t ext_clk_in:6;
		uint64_t ext_clk_en:1;
		uint64_t ptp_en:1;
#else
		uint64_t ptp_en:1;
		uint64_t ext_clk_en:1;
		uint64_t ext_clk_in:6;
		uint64_t tstmp_en:1;
		uint64_t tstmp_edge:1;
		uint64_t tstmp_in:6;
		uint64_t evcnt_en:1;
		uint64_t evcnt_edge:1;
		uint64_t evcnt_in:6;
		uint64_t ckout_en:1;
		uint64_t ckout_inv:1;
		uint64_t ckout_out:4;
		uint64_t pps_en:1;
		uint64_t pps_inv:1;
		uint64_t pps_out:5;
		uint64_t ckout_out4:1;
		uint64_t ext_clk_edge:2;
		uint64_t reserved_40_63:24;
#endif
	} cn66xx;
	struct cvmx_mio_ptp_clock_cfg_s cn68xx;
	struct cvmx_mio_ptp_clock_cfg_cn63xx cn68xxp1;
	struct cvmx_mio_ptp_clock_cfg_s cnf71xx;
};

union cvmx_mio_ptp_clock_comp {
	uint64_t u64;
	struct cvmx_mio_ptp_clock_comp_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t nanosec:32;
		uint64_t frnanosec:32;
#else
		uint64_t frnanosec:32;
		uint64_t nanosec:32;
#endif
	} s;
	struct cvmx_mio_ptp_clock_comp_s cn61xx;
	struct cvmx_mio_ptp_clock_comp_s cn63xx;
	struct cvmx_mio_ptp_clock_comp_s cn63xxp1;
	struct cvmx_mio_ptp_clock_comp_s cn66xx;
	struct cvmx_mio_ptp_clock_comp_s cn68xx;
	struct cvmx_mio_ptp_clock_comp_s cn68xxp1;
	struct cvmx_mio_ptp_clock_comp_s cnf71xx;
};

union cvmx_mio_ptp_clock_hi {
	uint64_t u64;
	struct cvmx_mio_ptp_clock_hi_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t nanosec:64;
#else
		uint64_t nanosec:64;
#endif
	} s;
	struct cvmx_mio_ptp_clock_hi_s cn61xx;
	struct cvmx_mio_ptp_clock_hi_s cn63xx;
	struct cvmx_mio_ptp_clock_hi_s cn63xxp1;
	struct cvmx_mio_ptp_clock_hi_s cn66xx;
	struct cvmx_mio_ptp_clock_hi_s cn68xx;
	struct cvmx_mio_ptp_clock_hi_s cn68xxp1;
	struct cvmx_mio_ptp_clock_hi_s cnf71xx;
};

union cvmx_mio_ptp_clock_lo {
	uint64_t u64;
	struct cvmx_mio_ptp_clock_lo_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t frnanosec:32;
#else
		uint64_t frnanosec:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_mio_ptp_clock_lo_s cn61xx;
	struct cvmx_mio_ptp_clock_lo_s cn63xx;
	struct cvmx_mio_ptp_clock_lo_s cn63xxp1;
	struct cvmx_mio_ptp_clock_lo_s cn66xx;
	struct cvmx_mio_ptp_clock_lo_s cn68xx;
	struct cvmx_mio_ptp_clock_lo_s cn68xxp1;
	struct cvmx_mio_ptp_clock_lo_s cnf71xx;
};

union cvmx_mio_ptp_evt_cnt {
	uint64_t u64;
	struct cvmx_mio_ptp_evt_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t cntr:64;
#else
		uint64_t cntr:64;
#endif
	} s;
	struct cvmx_mio_ptp_evt_cnt_s cn61xx;
	struct cvmx_mio_ptp_evt_cnt_s cn63xx;
	struct cvmx_mio_ptp_evt_cnt_s cn63xxp1;
	struct cvmx_mio_ptp_evt_cnt_s cn66xx;
	struct cvmx_mio_ptp_evt_cnt_s cn68xx;
	struct cvmx_mio_ptp_evt_cnt_s cn68xxp1;
	struct cvmx_mio_ptp_evt_cnt_s cnf71xx;
};

union cvmx_mio_ptp_phy_1pps_in {
	uint64_t u64;
	struct cvmx_mio_ptp_phy_1pps_in_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_5_63:59;
		uint64_t sel:5;
#else
		uint64_t sel:5;
		uint64_t reserved_5_63:59;
#endif
	} s;
	struct cvmx_mio_ptp_phy_1pps_in_s cnf71xx;
};

union cvmx_mio_ptp_pps_hi_incr {
	uint64_t u64;
	struct cvmx_mio_ptp_pps_hi_incr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t nanosec:32;
		uint64_t frnanosec:32;
#else
		uint64_t frnanosec:32;
		uint64_t nanosec:32;
#endif
	} s;
	struct cvmx_mio_ptp_pps_hi_incr_s cn61xx;
	struct cvmx_mio_ptp_pps_hi_incr_s cn66xx;
	struct cvmx_mio_ptp_pps_hi_incr_s cn68xx;
	struct cvmx_mio_ptp_pps_hi_incr_s cnf71xx;
};

union cvmx_mio_ptp_pps_lo_incr {
	uint64_t u64;
	struct cvmx_mio_ptp_pps_lo_incr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t nanosec:32;
		uint64_t frnanosec:32;
#else
		uint64_t frnanosec:32;
		uint64_t nanosec:32;
#endif
	} s;
	struct cvmx_mio_ptp_pps_lo_incr_s cn61xx;
	struct cvmx_mio_ptp_pps_lo_incr_s cn66xx;
	struct cvmx_mio_ptp_pps_lo_incr_s cn68xx;
	struct cvmx_mio_ptp_pps_lo_incr_s cnf71xx;
};

union cvmx_mio_ptp_pps_thresh_hi {
	uint64_t u64;
	struct cvmx_mio_ptp_pps_thresh_hi_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t nanosec:64;
#else
		uint64_t nanosec:64;
#endif
	} s;
	struct cvmx_mio_ptp_pps_thresh_hi_s cn61xx;
	struct cvmx_mio_ptp_pps_thresh_hi_s cn66xx;
	struct cvmx_mio_ptp_pps_thresh_hi_s cn68xx;
	struct cvmx_mio_ptp_pps_thresh_hi_s cnf71xx;
};

union cvmx_mio_ptp_pps_thresh_lo {
	uint64_t u64;
	struct cvmx_mio_ptp_pps_thresh_lo_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t frnanosec:32;
#else
		uint64_t frnanosec:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_mio_ptp_pps_thresh_lo_s cn61xx;
	struct cvmx_mio_ptp_pps_thresh_lo_s cn66xx;
	struct cvmx_mio_ptp_pps_thresh_lo_s cn68xx;
	struct cvmx_mio_ptp_pps_thresh_lo_s cnf71xx;
};

union cvmx_mio_ptp_timestamp {
	uint64_t u64;
	struct cvmx_mio_ptp_timestamp_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t nanosec:64;
#else
		uint64_t nanosec:64;
#endif
	} s;
	struct cvmx_mio_ptp_timestamp_s cn61xx;
	struct cvmx_mio_ptp_timestamp_s cn63xx;
	struct cvmx_mio_ptp_timestamp_s cn63xxp1;
	struct cvmx_mio_ptp_timestamp_s cn66xx;
	struct cvmx_mio_ptp_timestamp_s cn68xx;
	struct cvmx_mio_ptp_timestamp_s cn68xxp1;
	struct cvmx_mio_ptp_timestamp_s cnf71xx;
};

union cvmx_mio_qlmx_cfg {
	uint64_t u64;
	struct cvmx_mio_qlmx_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t prtmode:1;
		uint64_t reserved_12_13:2;
		uint64_t qlm_spd:4;
		uint64_t reserved_4_7:4;
		uint64_t qlm_cfg:4;
#else
		uint64_t qlm_cfg:4;
		uint64_t reserved_4_7:4;
		uint64_t qlm_spd:4;
		uint64_t reserved_12_13:2;
		uint64_t prtmode:1;
		uint64_t reserved_15_63:49;
#endif
	} s;
	struct cvmx_mio_qlmx_cfg_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t prtmode:1;
		uint64_t reserved_12_13:2;
		uint64_t qlm_spd:4;
		uint64_t reserved_2_7:6;
		uint64_t qlm_cfg:2;
#else
		uint64_t qlm_cfg:2;
		uint64_t reserved_2_7:6;
		uint64_t qlm_spd:4;
		uint64_t reserved_12_13:2;
		uint64_t prtmode:1;
		uint64_t reserved_15_63:49;
#endif
	} cn61xx;
	struct cvmx_mio_qlmx_cfg_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t qlm_spd:4;
		uint64_t reserved_4_7:4;
		uint64_t qlm_cfg:4;
#else
		uint64_t qlm_cfg:4;
		uint64_t reserved_4_7:4;
		uint64_t qlm_spd:4;
		uint64_t reserved_12_63:52;
#endif
	} cn66xx;
	struct cvmx_mio_qlmx_cfg_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t qlm_spd:4;
		uint64_t reserved_3_7:5;
		uint64_t qlm_cfg:3;
#else
		uint64_t qlm_cfg:3;
		uint64_t reserved_3_7:5;
		uint64_t qlm_spd:4;
		uint64_t reserved_12_63:52;
#endif
	} cn68xx;
	struct cvmx_mio_qlmx_cfg_cn68xx cn68xxp1;
	struct cvmx_mio_qlmx_cfg_cn61xx cnf71xx;
};

union cvmx_mio_rst_boot {
	uint64_t u64;
	struct cvmx_mio_rst_boot_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t chipkill:1;
		uint64_t jtcsrdis:1;
		uint64_t ejtagdis:1;
		uint64_t romen:1;
		uint64_t ckill_ppdis:1;
		uint64_t jt_tstmode:1;
		uint64_t reserved_50_57:8;
		uint64_t lboot_ext:2;
		uint64_t reserved_44_47:4;
		uint64_t qlm4_spd:4;
		uint64_t qlm3_spd:4;
		uint64_t c_mul:6;
		uint64_t pnr_mul:6;
		uint64_t qlm2_spd:4;
		uint64_t qlm1_spd:4;
		uint64_t qlm0_spd:4;
		uint64_t lboot:10;
		uint64_t rboot:1;
		uint64_t rboot_pin:1;
#else
		uint64_t rboot_pin:1;
		uint64_t rboot:1;
		uint64_t lboot:10;
		uint64_t qlm0_spd:4;
		uint64_t qlm1_spd:4;
		uint64_t qlm2_spd:4;
		uint64_t pnr_mul:6;
		uint64_t c_mul:6;
		uint64_t qlm3_spd:4;
		uint64_t qlm4_spd:4;
		uint64_t reserved_44_47:4;
		uint64_t lboot_ext:2;
		uint64_t reserved_50_57:8;
		uint64_t jt_tstmode:1;
		uint64_t ckill_ppdis:1;
		uint64_t romen:1;
		uint64_t ejtagdis:1;
		uint64_t jtcsrdis:1;
		uint64_t chipkill:1;
#endif
	} s;
	struct cvmx_mio_rst_boot_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t chipkill:1;
		uint64_t jtcsrdis:1;
		uint64_t ejtagdis:1;
		uint64_t romen:1;
		uint64_t ckill_ppdis:1;
		uint64_t jt_tstmode:1;
		uint64_t reserved_50_57:8;
		uint64_t lboot_ext:2;
		uint64_t reserved_36_47:12;
		uint64_t c_mul:6;
		uint64_t pnr_mul:6;
		uint64_t qlm2_spd:4;
		uint64_t qlm1_spd:4;
		uint64_t qlm0_spd:4;
		uint64_t lboot:10;
		uint64_t rboot:1;
		uint64_t rboot_pin:1;
#else
		uint64_t rboot_pin:1;
		uint64_t rboot:1;
		uint64_t lboot:10;
		uint64_t qlm0_spd:4;
		uint64_t qlm1_spd:4;
		uint64_t qlm2_spd:4;
		uint64_t pnr_mul:6;
		uint64_t c_mul:6;
		uint64_t reserved_36_47:12;
		uint64_t lboot_ext:2;
		uint64_t reserved_50_57:8;
		uint64_t jt_tstmode:1;
		uint64_t ckill_ppdis:1;
		uint64_t romen:1;
		uint64_t ejtagdis:1;
		uint64_t jtcsrdis:1;
		uint64_t chipkill:1;
#endif
	} cn61xx;
	struct cvmx_mio_rst_boot_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_36_63:28;
		uint64_t c_mul:6;
		uint64_t pnr_mul:6;
		uint64_t qlm2_spd:4;
		uint64_t qlm1_spd:4;
		uint64_t qlm0_spd:4;
		uint64_t lboot:10;
		uint64_t rboot:1;
		uint64_t rboot_pin:1;
#else
		uint64_t rboot_pin:1;
		uint64_t rboot:1;
		uint64_t lboot:10;
		uint64_t qlm0_spd:4;
		uint64_t qlm1_spd:4;
		uint64_t qlm2_spd:4;
		uint64_t pnr_mul:6;
		uint64_t c_mul:6;
		uint64_t reserved_36_63:28;
#endif
	} cn63xx;
	struct cvmx_mio_rst_boot_cn63xx cn63xxp1;
	struct cvmx_mio_rst_boot_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t chipkill:1;
		uint64_t jtcsrdis:1;
		uint64_t ejtagdis:1;
		uint64_t romen:1;
		uint64_t ckill_ppdis:1;
		uint64_t reserved_50_58:9;
		uint64_t lboot_ext:2;
		uint64_t reserved_36_47:12;
		uint64_t c_mul:6;
		uint64_t pnr_mul:6;
		uint64_t qlm2_spd:4;
		uint64_t qlm1_spd:4;
		uint64_t qlm0_spd:4;
		uint64_t lboot:10;
		uint64_t rboot:1;
		uint64_t rboot_pin:1;
#else
		uint64_t rboot_pin:1;
		uint64_t rboot:1;
		uint64_t lboot:10;
		uint64_t qlm0_spd:4;
		uint64_t qlm1_spd:4;
		uint64_t qlm2_spd:4;
		uint64_t pnr_mul:6;
		uint64_t c_mul:6;
		uint64_t reserved_36_47:12;
		uint64_t lboot_ext:2;
		uint64_t reserved_50_58:9;
		uint64_t ckill_ppdis:1;
		uint64_t romen:1;
		uint64_t ejtagdis:1;
		uint64_t jtcsrdis:1;
		uint64_t chipkill:1;
#endif
	} cn66xx;
	struct cvmx_mio_rst_boot_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_59_63:5;
		uint64_t jt_tstmode:1;
		uint64_t reserved_44_57:14;
		uint64_t qlm4_spd:4;
		uint64_t qlm3_spd:4;
		uint64_t c_mul:6;
		uint64_t pnr_mul:6;
		uint64_t qlm2_spd:4;
		uint64_t qlm1_spd:4;
		uint64_t qlm0_spd:4;
		uint64_t lboot:10;
		uint64_t rboot:1;
		uint64_t rboot_pin:1;
#else
		uint64_t rboot_pin:1;
		uint64_t rboot:1;
		uint64_t lboot:10;
		uint64_t qlm0_spd:4;
		uint64_t qlm1_spd:4;
		uint64_t qlm2_spd:4;
		uint64_t pnr_mul:6;
		uint64_t c_mul:6;
		uint64_t qlm3_spd:4;
		uint64_t qlm4_spd:4;
		uint64_t reserved_44_57:14;
		uint64_t jt_tstmode:1;
		uint64_t reserved_59_63:5;
#endif
	} cn68xx;
	struct cvmx_mio_rst_boot_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_44_63:20;
		uint64_t qlm4_spd:4;
		uint64_t qlm3_spd:4;
		uint64_t c_mul:6;
		uint64_t pnr_mul:6;
		uint64_t qlm2_spd:4;
		uint64_t qlm1_spd:4;
		uint64_t qlm0_spd:4;
		uint64_t lboot:10;
		uint64_t rboot:1;
		uint64_t rboot_pin:1;
#else
		uint64_t rboot_pin:1;
		uint64_t rboot:1;
		uint64_t lboot:10;
		uint64_t qlm0_spd:4;
		uint64_t qlm1_spd:4;
		uint64_t qlm2_spd:4;
		uint64_t pnr_mul:6;
		uint64_t c_mul:6;
		uint64_t qlm3_spd:4;
		uint64_t qlm4_spd:4;
		uint64_t reserved_44_63:20;
#endif
	} cn68xxp1;
	struct cvmx_mio_rst_boot_cn61xx cnf71xx;
};

union cvmx_mio_rst_cfg {
	uint64_t u64;
	struct cvmx_mio_rst_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t cntl_clr_bist:1;
		uint64_t warm_clr_bist:1;
		uint64_t soft_clr_bist:1;
#else
		uint64_t soft_clr_bist:1;
		uint64_t warm_clr_bist:1;
		uint64_t cntl_clr_bist:1;
		uint64_t reserved_3_63:61;
#endif
	} s;
	struct cvmx_mio_rst_cfg_cn61xx {
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
	} cn61xx;
	struct cvmx_mio_rst_cfg_cn61xx cn63xx;
	struct cvmx_mio_rst_cfg_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bist_delay:58;
		uint64_t reserved_2_5:4;
		uint64_t warm_clr_bist:1;
		uint64_t soft_clr_bist:1;
#else
		uint64_t soft_clr_bist:1;
		uint64_t warm_clr_bist:1;
		uint64_t reserved_2_5:4;
		uint64_t bist_delay:58;
#endif
	} cn63xxp1;
	struct cvmx_mio_rst_cfg_cn61xx cn66xx;
	struct cvmx_mio_rst_cfg_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bist_delay:56;
		uint64_t reserved_3_7:5;
		uint64_t cntl_clr_bist:1;
		uint64_t warm_clr_bist:1;
		uint64_t soft_clr_bist:1;
#else
		uint64_t soft_clr_bist:1;
		uint64_t warm_clr_bist:1;
		uint64_t cntl_clr_bist:1;
		uint64_t reserved_3_7:5;
		uint64_t bist_delay:56;
#endif
	} cn68xx;
	struct cvmx_mio_rst_cfg_cn68xx cn68xxp1;
	struct cvmx_mio_rst_cfg_cn61xx cnf71xx;
};

union cvmx_mio_rst_ckill {
	uint64_t u64;
	struct cvmx_mio_rst_ckill_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_47_63:17;
		uint64_t timer:47;
#else
		uint64_t timer:47;
		uint64_t reserved_47_63:17;
#endif
	} s;
	struct cvmx_mio_rst_ckill_s cn61xx;
	struct cvmx_mio_rst_ckill_s cn66xx;
	struct cvmx_mio_rst_ckill_s cnf71xx;
};

union cvmx_mio_rst_cntlx {
	uint64_t u64;
	struct cvmx_mio_rst_cntlx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_13_63:51;
		uint64_t in_rev_ln:1;
		uint64_t rev_lanes:1;
		uint64_t gen1_only:1;
		uint64_t prst_link:1;
		uint64_t rst_done:1;
		uint64_t rst_link:1;
		uint64_t host_mode:1;
		uint64_t prtmode:2;
		uint64_t rst_drv:1;
		uint64_t rst_rcv:1;
		uint64_t rst_chip:1;
		uint64_t rst_val:1;
#else
		uint64_t rst_val:1;
		uint64_t rst_chip:1;
		uint64_t rst_rcv:1;
		uint64_t rst_drv:1;
		uint64_t prtmode:2;
		uint64_t host_mode:1;
		uint64_t rst_link:1;
		uint64_t rst_done:1;
		uint64_t prst_link:1;
		uint64_t gen1_only:1;
		uint64_t rev_lanes:1;
		uint64_t in_rev_ln:1;
		uint64_t reserved_13_63:51;
#endif
	} s;
	struct cvmx_mio_rst_cntlx_s cn61xx;
	struct cvmx_mio_rst_cntlx_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t prst_link:1;
		uint64_t rst_done:1;
		uint64_t rst_link:1;
		uint64_t host_mode:1;
		uint64_t prtmode:2;
		uint64_t rst_drv:1;
		uint64_t rst_rcv:1;
		uint64_t rst_chip:1;
		uint64_t rst_val:1;
#else
		uint64_t rst_val:1;
		uint64_t rst_chip:1;
		uint64_t rst_rcv:1;
		uint64_t rst_drv:1;
		uint64_t prtmode:2;
		uint64_t host_mode:1;
		uint64_t rst_link:1;
		uint64_t rst_done:1;
		uint64_t prst_link:1;
		uint64_t reserved_10_63:54;
#endif
	} cn66xx;
	struct cvmx_mio_rst_cntlx_cn66xx cn68xx;
	struct cvmx_mio_rst_cntlx_s cnf71xx;
};

union cvmx_mio_rst_ctlx {
	uint64_t u64;
	struct cvmx_mio_rst_ctlx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_13_63:51;
		uint64_t in_rev_ln:1;
		uint64_t rev_lanes:1;
		uint64_t gen1_only:1;
		uint64_t prst_link:1;
		uint64_t rst_done:1;
		uint64_t rst_link:1;
		uint64_t host_mode:1;
		uint64_t prtmode:2;
		uint64_t rst_drv:1;
		uint64_t rst_rcv:1;
		uint64_t rst_chip:1;
		uint64_t rst_val:1;
#else
		uint64_t rst_val:1;
		uint64_t rst_chip:1;
		uint64_t rst_rcv:1;
		uint64_t rst_drv:1;
		uint64_t prtmode:2;
		uint64_t host_mode:1;
		uint64_t rst_link:1;
		uint64_t rst_done:1;
		uint64_t prst_link:1;
		uint64_t gen1_only:1;
		uint64_t rev_lanes:1;
		uint64_t in_rev_ln:1;
		uint64_t reserved_13_63:51;
#endif
	} s;
	struct cvmx_mio_rst_ctlx_s cn61xx;
	struct cvmx_mio_rst_ctlx_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t prst_link:1;
		uint64_t rst_done:1;
		uint64_t rst_link:1;
		uint64_t host_mode:1;
		uint64_t prtmode:2;
		uint64_t rst_drv:1;
		uint64_t rst_rcv:1;
		uint64_t rst_chip:1;
		uint64_t rst_val:1;
#else
		uint64_t rst_val:1;
		uint64_t rst_chip:1;
		uint64_t rst_rcv:1;
		uint64_t rst_drv:1;
		uint64_t prtmode:2;
		uint64_t host_mode:1;
		uint64_t rst_link:1;
		uint64_t rst_done:1;
		uint64_t prst_link:1;
		uint64_t reserved_10_63:54;
#endif
	} cn63xx;
	struct cvmx_mio_rst_ctlx_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t rst_done:1;
		uint64_t rst_link:1;
		uint64_t host_mode:1;
		uint64_t prtmode:2;
		uint64_t rst_drv:1;
		uint64_t rst_rcv:1;
		uint64_t rst_chip:1;
		uint64_t rst_val:1;
#else
		uint64_t rst_val:1;
		uint64_t rst_chip:1;
		uint64_t rst_rcv:1;
		uint64_t rst_drv:1;
		uint64_t prtmode:2;
		uint64_t host_mode:1;
		uint64_t rst_link:1;
		uint64_t rst_done:1;
		uint64_t reserved_9_63:55;
#endif
	} cn63xxp1;
	struct cvmx_mio_rst_ctlx_cn63xx cn66xx;
	struct cvmx_mio_rst_ctlx_cn63xx cn68xx;
	struct cvmx_mio_rst_ctlx_cn63xx cn68xxp1;
	struct cvmx_mio_rst_ctlx_s cnf71xx;
};

union cvmx_mio_rst_delay {
	uint64_t u64;
	struct cvmx_mio_rst_delay_s {
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
	struct cvmx_mio_rst_delay_s cn61xx;
	struct cvmx_mio_rst_delay_s cn63xx;
	struct cvmx_mio_rst_delay_s cn63xxp1;
	struct cvmx_mio_rst_delay_s cn66xx;
	struct cvmx_mio_rst_delay_s cn68xx;
	struct cvmx_mio_rst_delay_s cn68xxp1;
	struct cvmx_mio_rst_delay_s cnf71xx;
};

union cvmx_mio_rst_int {
	uint64_t u64;
	struct cvmx_mio_rst_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t perst1:1;
		uint64_t perst0:1;
		uint64_t reserved_4_7:4;
		uint64_t rst_link3:1;
		uint64_t rst_link2:1;
		uint64_t rst_link1:1;
		uint64_t rst_link0:1;
#else
		uint64_t rst_link0:1;
		uint64_t rst_link1:1;
		uint64_t rst_link2:1;
		uint64_t rst_link3:1;
		uint64_t reserved_4_7:4;
		uint64_t perst0:1;
		uint64_t perst1:1;
		uint64_t reserved_10_63:54;
#endif
	} s;
	struct cvmx_mio_rst_int_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t perst1:1;
		uint64_t perst0:1;
		uint64_t reserved_2_7:6;
		uint64_t rst_link1:1;
		uint64_t rst_link0:1;
#else
		uint64_t rst_link0:1;
		uint64_t rst_link1:1;
		uint64_t reserved_2_7:6;
		uint64_t perst0:1;
		uint64_t perst1:1;
		uint64_t reserved_10_63:54;
#endif
	} cn61xx;
	struct cvmx_mio_rst_int_cn61xx cn63xx;
	struct cvmx_mio_rst_int_cn61xx cn63xxp1;
	struct cvmx_mio_rst_int_s cn66xx;
	struct cvmx_mio_rst_int_cn61xx cn68xx;
	struct cvmx_mio_rst_int_cn61xx cn68xxp1;
	struct cvmx_mio_rst_int_cn61xx cnf71xx;
};

union cvmx_mio_rst_int_en {
	uint64_t u64;
	struct cvmx_mio_rst_int_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t perst1:1;
		uint64_t perst0:1;
		uint64_t reserved_4_7:4;
		uint64_t rst_link3:1;
		uint64_t rst_link2:1;
		uint64_t rst_link1:1;
		uint64_t rst_link0:1;
#else
		uint64_t rst_link0:1;
		uint64_t rst_link1:1;
		uint64_t rst_link2:1;
		uint64_t rst_link3:1;
		uint64_t reserved_4_7:4;
		uint64_t perst0:1;
		uint64_t perst1:1;
		uint64_t reserved_10_63:54;
#endif
	} s;
	struct cvmx_mio_rst_int_en_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t perst1:1;
		uint64_t perst0:1;
		uint64_t reserved_2_7:6;
		uint64_t rst_link1:1;
		uint64_t rst_link0:1;
#else
		uint64_t rst_link0:1;
		uint64_t rst_link1:1;
		uint64_t reserved_2_7:6;
		uint64_t perst0:1;
		uint64_t perst1:1;
		uint64_t reserved_10_63:54;
#endif
	} cn61xx;
	struct cvmx_mio_rst_int_en_cn61xx cn63xx;
	struct cvmx_mio_rst_int_en_cn61xx cn63xxp1;
	struct cvmx_mio_rst_int_en_s cn66xx;
	struct cvmx_mio_rst_int_en_cn61xx cn68xx;
	struct cvmx_mio_rst_int_en_cn61xx cn68xxp1;
	struct cvmx_mio_rst_int_en_cn61xx cnf71xx;
};

union cvmx_mio_twsx_int {
	uint64_t u64;
	struct cvmx_mio_twsx_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t st_int:1;
		uint64_t ts_int:1;
		uint64_t core_int:1;
		uint64_t reserved_3_3:1;
		uint64_t st_en:1;
		uint64_t ts_en:1;
		uint64_t core_en:1;
		uint64_t reserved_7_7:1;
		uint64_t sda_ovr:1;
		uint64_t scl_ovr:1;
		uint64_t sda:1;
		uint64_t scl:1;
		uint64_t reserved_12_63:52;
#endif
	} s;
	struct cvmx_mio_twsx_int_s cn30xx;
	struct cvmx_mio_twsx_int_s cn31xx;
	struct cvmx_mio_twsx_int_s cn38xx;
	struct cvmx_mio_twsx_int_cn38xxp2 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t core_en:1;
		uint64_t ts_en:1;
		uint64_t st_en:1;
		uint64_t reserved_3_3:1;
		uint64_t core_int:1;
		uint64_t ts_int:1;
		uint64_t st_int:1;
#else
		uint64_t st_int:1;
		uint64_t ts_int:1;
		uint64_t core_int:1;
		uint64_t reserved_3_3:1;
		uint64_t st_en:1;
		uint64_t ts_en:1;
		uint64_t core_en:1;
		uint64_t reserved_7_63:57;
#endif
	} cn38xxp2;
	struct cvmx_mio_twsx_int_s cn50xx;
	struct cvmx_mio_twsx_int_s cn52xx;
	struct cvmx_mio_twsx_int_s cn52xxp1;
	struct cvmx_mio_twsx_int_s cn56xx;
	struct cvmx_mio_twsx_int_s cn56xxp1;
	struct cvmx_mio_twsx_int_s cn58xx;
	struct cvmx_mio_twsx_int_s cn58xxp1;
	struct cvmx_mio_twsx_int_s cn61xx;
	struct cvmx_mio_twsx_int_s cn63xx;
	struct cvmx_mio_twsx_int_s cn63xxp1;
	struct cvmx_mio_twsx_int_s cn66xx;
	struct cvmx_mio_twsx_int_s cn68xx;
	struct cvmx_mio_twsx_int_s cn68xxp1;
	struct cvmx_mio_twsx_int_s cnf71xx;
};

union cvmx_mio_twsx_sw_twsi {
	uint64_t u64;
	struct cvmx_mio_twsx_sw_twsi_s {
#ifdef __BIG_ENDIAN_BITFIELD
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
#else
		uint64_t d:32;
		uint64_t eop_ia:3;
		uint64_t ia:5;
		uint64_t a:10;
		uint64_t scr:2;
		uint64_t size:3;
		uint64_t sovr:1;
		uint64_t r:1;
		uint64_t op:4;
		uint64_t eia:1;
		uint64_t slonly:1;
		uint64_t v:1;
#endif
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
	struct cvmx_mio_twsx_sw_twsi_s cn61xx;
	struct cvmx_mio_twsx_sw_twsi_s cn63xx;
	struct cvmx_mio_twsx_sw_twsi_s cn63xxp1;
	struct cvmx_mio_twsx_sw_twsi_s cn66xx;
	struct cvmx_mio_twsx_sw_twsi_s cn68xx;
	struct cvmx_mio_twsx_sw_twsi_s cn68xxp1;
	struct cvmx_mio_twsx_sw_twsi_s cnf71xx;
};

union cvmx_mio_twsx_sw_twsi_ext {
	uint64_t u64;
	struct cvmx_mio_twsx_sw_twsi_ext_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_40_63:24;
		uint64_t ia:8;
		uint64_t d:32;
#else
		uint64_t d:32;
		uint64_t ia:8;
		uint64_t reserved_40_63:24;
#endif
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
	struct cvmx_mio_twsx_sw_twsi_ext_s cn61xx;
	struct cvmx_mio_twsx_sw_twsi_ext_s cn63xx;
	struct cvmx_mio_twsx_sw_twsi_ext_s cn63xxp1;
	struct cvmx_mio_twsx_sw_twsi_ext_s cn66xx;
	struct cvmx_mio_twsx_sw_twsi_ext_s cn68xx;
	struct cvmx_mio_twsx_sw_twsi_ext_s cn68xxp1;
	struct cvmx_mio_twsx_sw_twsi_ext_s cnf71xx;
};

union cvmx_mio_twsx_twsi_sw {
	uint64_t u64;
	struct cvmx_mio_twsx_twsi_sw_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t v:2;
		uint64_t reserved_32_61:30;
		uint64_t d:32;
#else
		uint64_t d:32;
		uint64_t reserved_32_61:30;
		uint64_t v:2;
#endif
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
	struct cvmx_mio_twsx_twsi_sw_s cn61xx;
	struct cvmx_mio_twsx_twsi_sw_s cn63xx;
	struct cvmx_mio_twsx_twsi_sw_s cn63xxp1;
	struct cvmx_mio_twsx_twsi_sw_s cn66xx;
	struct cvmx_mio_twsx_twsi_sw_s cn68xx;
	struct cvmx_mio_twsx_twsi_sw_s cn68xxp1;
	struct cvmx_mio_twsx_twsi_sw_s cnf71xx;
};

union cvmx_mio_uartx_dlh {
	uint64_t u64;
	struct cvmx_mio_uartx_dlh_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t dlh:8;
#else
		uint64_t dlh:8;
		uint64_t reserved_8_63:56;
#endif
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
	struct cvmx_mio_uartx_dlh_s cn61xx;
	struct cvmx_mio_uartx_dlh_s cn63xx;
	struct cvmx_mio_uartx_dlh_s cn63xxp1;
	struct cvmx_mio_uartx_dlh_s cn66xx;
	struct cvmx_mio_uartx_dlh_s cn68xx;
	struct cvmx_mio_uartx_dlh_s cn68xxp1;
	struct cvmx_mio_uartx_dlh_s cnf71xx;
};

union cvmx_mio_uartx_dll {
	uint64_t u64;
	struct cvmx_mio_uartx_dll_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t dll:8;
#else
		uint64_t dll:8;
		uint64_t reserved_8_63:56;
#endif
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
	struct cvmx_mio_uartx_dll_s cn61xx;
	struct cvmx_mio_uartx_dll_s cn63xx;
	struct cvmx_mio_uartx_dll_s cn63xxp1;
	struct cvmx_mio_uartx_dll_s cn66xx;
	struct cvmx_mio_uartx_dll_s cn68xx;
	struct cvmx_mio_uartx_dll_s cn68xxp1;
	struct cvmx_mio_uartx_dll_s cnf71xx;
};

union cvmx_mio_uartx_far {
	uint64_t u64;
	struct cvmx_mio_uartx_far_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t far:1;
#else
		uint64_t far:1;
		uint64_t reserved_1_63:63;
#endif
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
	struct cvmx_mio_uartx_far_s cn61xx;
	struct cvmx_mio_uartx_far_s cn63xx;
	struct cvmx_mio_uartx_far_s cn63xxp1;
	struct cvmx_mio_uartx_far_s cn66xx;
	struct cvmx_mio_uartx_far_s cn68xx;
	struct cvmx_mio_uartx_far_s cn68xxp1;
	struct cvmx_mio_uartx_far_s cnf71xx;
};

union cvmx_mio_uartx_fcr {
	uint64_t u64;
	struct cvmx_mio_uartx_fcr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t rxtrig:2;
		uint64_t txtrig:2;
		uint64_t reserved_3_3:1;
		uint64_t txfr:1;
		uint64_t rxfr:1;
		uint64_t en:1;
#else
		uint64_t en:1;
		uint64_t rxfr:1;
		uint64_t txfr:1;
		uint64_t reserved_3_3:1;
		uint64_t txtrig:2;
		uint64_t rxtrig:2;
		uint64_t reserved_8_63:56;
#endif
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
	struct cvmx_mio_uartx_fcr_s cn61xx;
	struct cvmx_mio_uartx_fcr_s cn63xx;
	struct cvmx_mio_uartx_fcr_s cn63xxp1;
	struct cvmx_mio_uartx_fcr_s cn66xx;
	struct cvmx_mio_uartx_fcr_s cn68xx;
	struct cvmx_mio_uartx_fcr_s cn68xxp1;
	struct cvmx_mio_uartx_fcr_s cnf71xx;
};

union cvmx_mio_uartx_htx {
	uint64_t u64;
	struct cvmx_mio_uartx_htx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t htx:1;
#else
		uint64_t htx:1;
		uint64_t reserved_1_63:63;
#endif
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
	struct cvmx_mio_uartx_htx_s cn61xx;
	struct cvmx_mio_uartx_htx_s cn63xx;
	struct cvmx_mio_uartx_htx_s cn63xxp1;
	struct cvmx_mio_uartx_htx_s cn66xx;
	struct cvmx_mio_uartx_htx_s cn68xx;
	struct cvmx_mio_uartx_htx_s cn68xxp1;
	struct cvmx_mio_uartx_htx_s cnf71xx;
};

union cvmx_mio_uartx_ier {
	uint64_t u64;
	struct cvmx_mio_uartx_ier_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t ptime:1;
		uint64_t reserved_4_6:3;
		uint64_t edssi:1;
		uint64_t elsi:1;
		uint64_t etbei:1;
		uint64_t erbfi:1;
#else
		uint64_t erbfi:1;
		uint64_t etbei:1;
		uint64_t elsi:1;
		uint64_t edssi:1;
		uint64_t reserved_4_6:3;
		uint64_t ptime:1;
		uint64_t reserved_8_63:56;
#endif
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
	struct cvmx_mio_uartx_ier_s cn61xx;
	struct cvmx_mio_uartx_ier_s cn63xx;
	struct cvmx_mio_uartx_ier_s cn63xxp1;
	struct cvmx_mio_uartx_ier_s cn66xx;
	struct cvmx_mio_uartx_ier_s cn68xx;
	struct cvmx_mio_uartx_ier_s cn68xxp1;
	struct cvmx_mio_uartx_ier_s cnf71xx;
};

union cvmx_mio_uartx_iir {
	uint64_t u64;
	struct cvmx_mio_uartx_iir_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t fen:2;
		uint64_t reserved_4_5:2;
		uint64_t iid:4;
#else
		uint64_t iid:4;
		uint64_t reserved_4_5:2;
		uint64_t fen:2;
		uint64_t reserved_8_63:56;
#endif
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
	struct cvmx_mio_uartx_iir_s cn61xx;
	struct cvmx_mio_uartx_iir_s cn63xx;
	struct cvmx_mio_uartx_iir_s cn63xxp1;
	struct cvmx_mio_uartx_iir_s cn66xx;
	struct cvmx_mio_uartx_iir_s cn68xx;
	struct cvmx_mio_uartx_iir_s cn68xxp1;
	struct cvmx_mio_uartx_iir_s cnf71xx;
};

union cvmx_mio_uartx_lcr {
	uint64_t u64;
	struct cvmx_mio_uartx_lcr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t dlab:1;
		uint64_t brk:1;
		uint64_t reserved_5_5:1;
		uint64_t eps:1;
		uint64_t pen:1;
		uint64_t stop:1;
		uint64_t cls:2;
#else
		uint64_t cls:2;
		uint64_t stop:1;
		uint64_t pen:1;
		uint64_t eps:1;
		uint64_t reserved_5_5:1;
		uint64_t brk:1;
		uint64_t dlab:1;
		uint64_t reserved_8_63:56;
#endif
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
	struct cvmx_mio_uartx_lcr_s cn61xx;
	struct cvmx_mio_uartx_lcr_s cn63xx;
	struct cvmx_mio_uartx_lcr_s cn63xxp1;
	struct cvmx_mio_uartx_lcr_s cn66xx;
	struct cvmx_mio_uartx_lcr_s cn68xx;
	struct cvmx_mio_uartx_lcr_s cn68xxp1;
	struct cvmx_mio_uartx_lcr_s cnf71xx;
};

union cvmx_mio_uartx_lsr {
	uint64_t u64;
	struct cvmx_mio_uartx_lsr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t ferr:1;
		uint64_t temt:1;
		uint64_t thre:1;
		uint64_t bi:1;
		uint64_t fe:1;
		uint64_t pe:1;
		uint64_t oe:1;
		uint64_t dr:1;
#else
		uint64_t dr:1;
		uint64_t oe:1;
		uint64_t pe:1;
		uint64_t fe:1;
		uint64_t bi:1;
		uint64_t thre:1;
		uint64_t temt:1;
		uint64_t ferr:1;
		uint64_t reserved_8_63:56;
#endif
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
	struct cvmx_mio_uartx_lsr_s cn61xx;
	struct cvmx_mio_uartx_lsr_s cn63xx;
	struct cvmx_mio_uartx_lsr_s cn63xxp1;
	struct cvmx_mio_uartx_lsr_s cn66xx;
	struct cvmx_mio_uartx_lsr_s cn68xx;
	struct cvmx_mio_uartx_lsr_s cn68xxp1;
	struct cvmx_mio_uartx_lsr_s cnf71xx;
};

union cvmx_mio_uartx_mcr {
	uint64_t u64;
	struct cvmx_mio_uartx_mcr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t afce:1;
		uint64_t loop:1;
		uint64_t out2:1;
		uint64_t out1:1;
		uint64_t rts:1;
		uint64_t dtr:1;
#else
		uint64_t dtr:1;
		uint64_t rts:1;
		uint64_t out1:1;
		uint64_t out2:1;
		uint64_t loop:1;
		uint64_t afce:1;
		uint64_t reserved_6_63:58;
#endif
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
	struct cvmx_mio_uartx_mcr_s cn61xx;
	struct cvmx_mio_uartx_mcr_s cn63xx;
	struct cvmx_mio_uartx_mcr_s cn63xxp1;
	struct cvmx_mio_uartx_mcr_s cn66xx;
	struct cvmx_mio_uartx_mcr_s cn68xx;
	struct cvmx_mio_uartx_mcr_s cn68xxp1;
	struct cvmx_mio_uartx_mcr_s cnf71xx;
};

union cvmx_mio_uartx_msr {
	uint64_t u64;
	struct cvmx_mio_uartx_msr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t dcd:1;
		uint64_t ri:1;
		uint64_t dsr:1;
		uint64_t cts:1;
		uint64_t ddcd:1;
		uint64_t teri:1;
		uint64_t ddsr:1;
		uint64_t dcts:1;
#else
		uint64_t dcts:1;
		uint64_t ddsr:1;
		uint64_t teri:1;
		uint64_t ddcd:1;
		uint64_t cts:1;
		uint64_t dsr:1;
		uint64_t ri:1;
		uint64_t dcd:1;
		uint64_t reserved_8_63:56;
#endif
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
	struct cvmx_mio_uartx_msr_s cn61xx;
	struct cvmx_mio_uartx_msr_s cn63xx;
	struct cvmx_mio_uartx_msr_s cn63xxp1;
	struct cvmx_mio_uartx_msr_s cn66xx;
	struct cvmx_mio_uartx_msr_s cn68xx;
	struct cvmx_mio_uartx_msr_s cn68xxp1;
	struct cvmx_mio_uartx_msr_s cnf71xx;
};

union cvmx_mio_uartx_rbr {
	uint64_t u64;
	struct cvmx_mio_uartx_rbr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t rbr:8;
#else
		uint64_t rbr:8;
		uint64_t reserved_8_63:56;
#endif
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
	struct cvmx_mio_uartx_rbr_s cn61xx;
	struct cvmx_mio_uartx_rbr_s cn63xx;
	struct cvmx_mio_uartx_rbr_s cn63xxp1;
	struct cvmx_mio_uartx_rbr_s cn66xx;
	struct cvmx_mio_uartx_rbr_s cn68xx;
	struct cvmx_mio_uartx_rbr_s cn68xxp1;
	struct cvmx_mio_uartx_rbr_s cnf71xx;
};

union cvmx_mio_uartx_rfl {
	uint64_t u64;
	struct cvmx_mio_uartx_rfl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t rfl:7;
#else
		uint64_t rfl:7;
		uint64_t reserved_7_63:57;
#endif
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
	struct cvmx_mio_uartx_rfl_s cn61xx;
	struct cvmx_mio_uartx_rfl_s cn63xx;
	struct cvmx_mio_uartx_rfl_s cn63xxp1;
	struct cvmx_mio_uartx_rfl_s cn66xx;
	struct cvmx_mio_uartx_rfl_s cn68xx;
	struct cvmx_mio_uartx_rfl_s cn68xxp1;
	struct cvmx_mio_uartx_rfl_s cnf71xx;
};

union cvmx_mio_uartx_rfw {
	uint64_t u64;
	struct cvmx_mio_uartx_rfw_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t rffe:1;
		uint64_t rfpe:1;
		uint64_t rfwd:8;
#else
		uint64_t rfwd:8;
		uint64_t rfpe:1;
		uint64_t rffe:1;
		uint64_t reserved_10_63:54;
#endif
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
	struct cvmx_mio_uartx_rfw_s cn61xx;
	struct cvmx_mio_uartx_rfw_s cn63xx;
	struct cvmx_mio_uartx_rfw_s cn63xxp1;
	struct cvmx_mio_uartx_rfw_s cn66xx;
	struct cvmx_mio_uartx_rfw_s cn68xx;
	struct cvmx_mio_uartx_rfw_s cn68xxp1;
	struct cvmx_mio_uartx_rfw_s cnf71xx;
};

union cvmx_mio_uartx_sbcr {
	uint64_t u64;
	struct cvmx_mio_uartx_sbcr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t sbcr:1;
#else
		uint64_t sbcr:1;
		uint64_t reserved_1_63:63;
#endif
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
	struct cvmx_mio_uartx_sbcr_s cn61xx;
	struct cvmx_mio_uartx_sbcr_s cn63xx;
	struct cvmx_mio_uartx_sbcr_s cn63xxp1;
	struct cvmx_mio_uartx_sbcr_s cn66xx;
	struct cvmx_mio_uartx_sbcr_s cn68xx;
	struct cvmx_mio_uartx_sbcr_s cn68xxp1;
	struct cvmx_mio_uartx_sbcr_s cnf71xx;
};

union cvmx_mio_uartx_scr {
	uint64_t u64;
	struct cvmx_mio_uartx_scr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t scr:8;
#else
		uint64_t scr:8;
		uint64_t reserved_8_63:56;
#endif
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
	struct cvmx_mio_uartx_scr_s cn61xx;
	struct cvmx_mio_uartx_scr_s cn63xx;
	struct cvmx_mio_uartx_scr_s cn63xxp1;
	struct cvmx_mio_uartx_scr_s cn66xx;
	struct cvmx_mio_uartx_scr_s cn68xx;
	struct cvmx_mio_uartx_scr_s cn68xxp1;
	struct cvmx_mio_uartx_scr_s cnf71xx;
};

union cvmx_mio_uartx_sfe {
	uint64_t u64;
	struct cvmx_mio_uartx_sfe_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t sfe:1;
#else
		uint64_t sfe:1;
		uint64_t reserved_1_63:63;
#endif
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
	struct cvmx_mio_uartx_sfe_s cn61xx;
	struct cvmx_mio_uartx_sfe_s cn63xx;
	struct cvmx_mio_uartx_sfe_s cn63xxp1;
	struct cvmx_mio_uartx_sfe_s cn66xx;
	struct cvmx_mio_uartx_sfe_s cn68xx;
	struct cvmx_mio_uartx_sfe_s cn68xxp1;
	struct cvmx_mio_uartx_sfe_s cnf71xx;
};

union cvmx_mio_uartx_srr {
	uint64_t u64;
	struct cvmx_mio_uartx_srr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t stfr:1;
		uint64_t srfr:1;
		uint64_t usr:1;
#else
		uint64_t usr:1;
		uint64_t srfr:1;
		uint64_t stfr:1;
		uint64_t reserved_3_63:61;
#endif
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
	struct cvmx_mio_uartx_srr_s cn61xx;
	struct cvmx_mio_uartx_srr_s cn63xx;
	struct cvmx_mio_uartx_srr_s cn63xxp1;
	struct cvmx_mio_uartx_srr_s cn66xx;
	struct cvmx_mio_uartx_srr_s cn68xx;
	struct cvmx_mio_uartx_srr_s cn68xxp1;
	struct cvmx_mio_uartx_srr_s cnf71xx;
};

union cvmx_mio_uartx_srt {
	uint64_t u64;
	struct cvmx_mio_uartx_srt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t srt:2;
#else
		uint64_t srt:2;
		uint64_t reserved_2_63:62;
#endif
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
	struct cvmx_mio_uartx_srt_s cn61xx;
	struct cvmx_mio_uartx_srt_s cn63xx;
	struct cvmx_mio_uartx_srt_s cn63xxp1;
	struct cvmx_mio_uartx_srt_s cn66xx;
	struct cvmx_mio_uartx_srt_s cn68xx;
	struct cvmx_mio_uartx_srt_s cn68xxp1;
	struct cvmx_mio_uartx_srt_s cnf71xx;
};

union cvmx_mio_uartx_srts {
	uint64_t u64;
	struct cvmx_mio_uartx_srts_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t srts:1;
#else
		uint64_t srts:1;
		uint64_t reserved_1_63:63;
#endif
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
	struct cvmx_mio_uartx_srts_s cn61xx;
	struct cvmx_mio_uartx_srts_s cn63xx;
	struct cvmx_mio_uartx_srts_s cn63xxp1;
	struct cvmx_mio_uartx_srts_s cn66xx;
	struct cvmx_mio_uartx_srts_s cn68xx;
	struct cvmx_mio_uartx_srts_s cn68xxp1;
	struct cvmx_mio_uartx_srts_s cnf71xx;
};

union cvmx_mio_uartx_stt {
	uint64_t u64;
	struct cvmx_mio_uartx_stt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t stt:2;
#else
		uint64_t stt:2;
		uint64_t reserved_2_63:62;
#endif
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
	struct cvmx_mio_uartx_stt_s cn61xx;
	struct cvmx_mio_uartx_stt_s cn63xx;
	struct cvmx_mio_uartx_stt_s cn63xxp1;
	struct cvmx_mio_uartx_stt_s cn66xx;
	struct cvmx_mio_uartx_stt_s cn68xx;
	struct cvmx_mio_uartx_stt_s cn68xxp1;
	struct cvmx_mio_uartx_stt_s cnf71xx;
};

union cvmx_mio_uartx_tfl {
	uint64_t u64;
	struct cvmx_mio_uartx_tfl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t tfl:7;
#else
		uint64_t tfl:7;
		uint64_t reserved_7_63:57;
#endif
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
	struct cvmx_mio_uartx_tfl_s cn61xx;
	struct cvmx_mio_uartx_tfl_s cn63xx;
	struct cvmx_mio_uartx_tfl_s cn63xxp1;
	struct cvmx_mio_uartx_tfl_s cn66xx;
	struct cvmx_mio_uartx_tfl_s cn68xx;
	struct cvmx_mio_uartx_tfl_s cn68xxp1;
	struct cvmx_mio_uartx_tfl_s cnf71xx;
};

union cvmx_mio_uartx_tfr {
	uint64_t u64;
	struct cvmx_mio_uartx_tfr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t tfr:8;
#else
		uint64_t tfr:8;
		uint64_t reserved_8_63:56;
#endif
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
	struct cvmx_mio_uartx_tfr_s cn61xx;
	struct cvmx_mio_uartx_tfr_s cn63xx;
	struct cvmx_mio_uartx_tfr_s cn63xxp1;
	struct cvmx_mio_uartx_tfr_s cn66xx;
	struct cvmx_mio_uartx_tfr_s cn68xx;
	struct cvmx_mio_uartx_tfr_s cn68xxp1;
	struct cvmx_mio_uartx_tfr_s cnf71xx;
};

union cvmx_mio_uartx_thr {
	uint64_t u64;
	struct cvmx_mio_uartx_thr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t thr:8;
#else
		uint64_t thr:8;
		uint64_t reserved_8_63:56;
#endif
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
	struct cvmx_mio_uartx_thr_s cn61xx;
	struct cvmx_mio_uartx_thr_s cn63xx;
	struct cvmx_mio_uartx_thr_s cn63xxp1;
	struct cvmx_mio_uartx_thr_s cn66xx;
	struct cvmx_mio_uartx_thr_s cn68xx;
	struct cvmx_mio_uartx_thr_s cn68xxp1;
	struct cvmx_mio_uartx_thr_s cnf71xx;
};

union cvmx_mio_uartx_usr {
	uint64_t u64;
	struct cvmx_mio_uartx_usr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_5_63:59;
		uint64_t rff:1;
		uint64_t rfne:1;
		uint64_t tfe:1;
		uint64_t tfnf:1;
		uint64_t busy:1;
#else
		uint64_t busy:1;
		uint64_t tfnf:1;
		uint64_t tfe:1;
		uint64_t rfne:1;
		uint64_t rff:1;
		uint64_t reserved_5_63:59;
#endif
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
	struct cvmx_mio_uartx_usr_s cn61xx;
	struct cvmx_mio_uartx_usr_s cn63xx;
	struct cvmx_mio_uartx_usr_s cn63xxp1;
	struct cvmx_mio_uartx_usr_s cn66xx;
	struct cvmx_mio_uartx_usr_s cn68xx;
	struct cvmx_mio_uartx_usr_s cn68xxp1;
	struct cvmx_mio_uartx_usr_s cnf71xx;
};

union cvmx_mio_uart2_dlh {
	uint64_t u64;
	struct cvmx_mio_uart2_dlh_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t dlh:8;
#else
		uint64_t dlh:8;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_mio_uart2_dlh_s cn52xx;
	struct cvmx_mio_uart2_dlh_s cn52xxp1;
};

union cvmx_mio_uart2_dll {
	uint64_t u64;
	struct cvmx_mio_uart2_dll_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t dll:8;
#else
		uint64_t dll:8;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_mio_uart2_dll_s cn52xx;
	struct cvmx_mio_uart2_dll_s cn52xxp1;
};

union cvmx_mio_uart2_far {
	uint64_t u64;
	struct cvmx_mio_uart2_far_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t far:1;
#else
		uint64_t far:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_mio_uart2_far_s cn52xx;
	struct cvmx_mio_uart2_far_s cn52xxp1;
};

union cvmx_mio_uart2_fcr {
	uint64_t u64;
	struct cvmx_mio_uart2_fcr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t rxtrig:2;
		uint64_t txtrig:2;
		uint64_t reserved_3_3:1;
		uint64_t txfr:1;
		uint64_t rxfr:1;
		uint64_t en:1;
#else
		uint64_t en:1;
		uint64_t rxfr:1;
		uint64_t txfr:1;
		uint64_t reserved_3_3:1;
		uint64_t txtrig:2;
		uint64_t rxtrig:2;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_mio_uart2_fcr_s cn52xx;
	struct cvmx_mio_uart2_fcr_s cn52xxp1;
};

union cvmx_mio_uart2_htx {
	uint64_t u64;
	struct cvmx_mio_uart2_htx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t htx:1;
#else
		uint64_t htx:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_mio_uart2_htx_s cn52xx;
	struct cvmx_mio_uart2_htx_s cn52xxp1;
};

union cvmx_mio_uart2_ier {
	uint64_t u64;
	struct cvmx_mio_uart2_ier_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t ptime:1;
		uint64_t reserved_4_6:3;
		uint64_t edssi:1;
		uint64_t elsi:1;
		uint64_t etbei:1;
		uint64_t erbfi:1;
#else
		uint64_t erbfi:1;
		uint64_t etbei:1;
		uint64_t elsi:1;
		uint64_t edssi:1;
		uint64_t reserved_4_6:3;
		uint64_t ptime:1;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_mio_uart2_ier_s cn52xx;
	struct cvmx_mio_uart2_ier_s cn52xxp1;
};

union cvmx_mio_uart2_iir {
	uint64_t u64;
	struct cvmx_mio_uart2_iir_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t fen:2;
		uint64_t reserved_4_5:2;
		uint64_t iid:4;
#else
		uint64_t iid:4;
		uint64_t reserved_4_5:2;
		uint64_t fen:2;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_mio_uart2_iir_s cn52xx;
	struct cvmx_mio_uart2_iir_s cn52xxp1;
};

union cvmx_mio_uart2_lcr {
	uint64_t u64;
	struct cvmx_mio_uart2_lcr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t dlab:1;
		uint64_t brk:1;
		uint64_t reserved_5_5:1;
		uint64_t eps:1;
		uint64_t pen:1;
		uint64_t stop:1;
		uint64_t cls:2;
#else
		uint64_t cls:2;
		uint64_t stop:1;
		uint64_t pen:1;
		uint64_t eps:1;
		uint64_t reserved_5_5:1;
		uint64_t brk:1;
		uint64_t dlab:1;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_mio_uart2_lcr_s cn52xx;
	struct cvmx_mio_uart2_lcr_s cn52xxp1;
};

union cvmx_mio_uart2_lsr {
	uint64_t u64;
	struct cvmx_mio_uart2_lsr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t ferr:1;
		uint64_t temt:1;
		uint64_t thre:1;
		uint64_t bi:1;
		uint64_t fe:1;
		uint64_t pe:1;
		uint64_t oe:1;
		uint64_t dr:1;
#else
		uint64_t dr:1;
		uint64_t oe:1;
		uint64_t pe:1;
		uint64_t fe:1;
		uint64_t bi:1;
		uint64_t thre:1;
		uint64_t temt:1;
		uint64_t ferr:1;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_mio_uart2_lsr_s cn52xx;
	struct cvmx_mio_uart2_lsr_s cn52xxp1;
};

union cvmx_mio_uart2_mcr {
	uint64_t u64;
	struct cvmx_mio_uart2_mcr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t afce:1;
		uint64_t loop:1;
		uint64_t out2:1;
		uint64_t out1:1;
		uint64_t rts:1;
		uint64_t dtr:1;
#else
		uint64_t dtr:1;
		uint64_t rts:1;
		uint64_t out1:1;
		uint64_t out2:1;
		uint64_t loop:1;
		uint64_t afce:1;
		uint64_t reserved_6_63:58;
#endif
	} s;
	struct cvmx_mio_uart2_mcr_s cn52xx;
	struct cvmx_mio_uart2_mcr_s cn52xxp1;
};

union cvmx_mio_uart2_msr {
	uint64_t u64;
	struct cvmx_mio_uart2_msr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t dcd:1;
		uint64_t ri:1;
		uint64_t dsr:1;
		uint64_t cts:1;
		uint64_t ddcd:1;
		uint64_t teri:1;
		uint64_t ddsr:1;
		uint64_t dcts:1;
#else
		uint64_t dcts:1;
		uint64_t ddsr:1;
		uint64_t teri:1;
		uint64_t ddcd:1;
		uint64_t cts:1;
		uint64_t dsr:1;
		uint64_t ri:1;
		uint64_t dcd:1;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_mio_uart2_msr_s cn52xx;
	struct cvmx_mio_uart2_msr_s cn52xxp1;
};

union cvmx_mio_uart2_rbr {
	uint64_t u64;
	struct cvmx_mio_uart2_rbr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t rbr:8;
#else
		uint64_t rbr:8;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_mio_uart2_rbr_s cn52xx;
	struct cvmx_mio_uart2_rbr_s cn52xxp1;
};

union cvmx_mio_uart2_rfl {
	uint64_t u64;
	struct cvmx_mio_uart2_rfl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t rfl:7;
#else
		uint64_t rfl:7;
		uint64_t reserved_7_63:57;
#endif
	} s;
	struct cvmx_mio_uart2_rfl_s cn52xx;
	struct cvmx_mio_uart2_rfl_s cn52xxp1;
};

union cvmx_mio_uart2_rfw {
	uint64_t u64;
	struct cvmx_mio_uart2_rfw_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t rffe:1;
		uint64_t rfpe:1;
		uint64_t rfwd:8;
#else
		uint64_t rfwd:8;
		uint64_t rfpe:1;
		uint64_t rffe:1;
		uint64_t reserved_10_63:54;
#endif
	} s;
	struct cvmx_mio_uart2_rfw_s cn52xx;
	struct cvmx_mio_uart2_rfw_s cn52xxp1;
};

union cvmx_mio_uart2_sbcr {
	uint64_t u64;
	struct cvmx_mio_uart2_sbcr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t sbcr:1;
#else
		uint64_t sbcr:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_mio_uart2_sbcr_s cn52xx;
	struct cvmx_mio_uart2_sbcr_s cn52xxp1;
};

union cvmx_mio_uart2_scr {
	uint64_t u64;
	struct cvmx_mio_uart2_scr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t scr:8;
#else
		uint64_t scr:8;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_mio_uart2_scr_s cn52xx;
	struct cvmx_mio_uart2_scr_s cn52xxp1;
};

union cvmx_mio_uart2_sfe {
	uint64_t u64;
	struct cvmx_mio_uart2_sfe_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t sfe:1;
#else
		uint64_t sfe:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_mio_uart2_sfe_s cn52xx;
	struct cvmx_mio_uart2_sfe_s cn52xxp1;
};

union cvmx_mio_uart2_srr {
	uint64_t u64;
	struct cvmx_mio_uart2_srr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t stfr:1;
		uint64_t srfr:1;
		uint64_t usr:1;
#else
		uint64_t usr:1;
		uint64_t srfr:1;
		uint64_t stfr:1;
		uint64_t reserved_3_63:61;
#endif
	} s;
	struct cvmx_mio_uart2_srr_s cn52xx;
	struct cvmx_mio_uart2_srr_s cn52xxp1;
};

union cvmx_mio_uart2_srt {
	uint64_t u64;
	struct cvmx_mio_uart2_srt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t srt:2;
#else
		uint64_t srt:2;
		uint64_t reserved_2_63:62;
#endif
	} s;
	struct cvmx_mio_uart2_srt_s cn52xx;
	struct cvmx_mio_uart2_srt_s cn52xxp1;
};

union cvmx_mio_uart2_srts {
	uint64_t u64;
	struct cvmx_mio_uart2_srts_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t srts:1;
#else
		uint64_t srts:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_mio_uart2_srts_s cn52xx;
	struct cvmx_mio_uart2_srts_s cn52xxp1;
};

union cvmx_mio_uart2_stt {
	uint64_t u64;
	struct cvmx_mio_uart2_stt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t stt:2;
#else
		uint64_t stt:2;
		uint64_t reserved_2_63:62;
#endif
	} s;
	struct cvmx_mio_uart2_stt_s cn52xx;
	struct cvmx_mio_uart2_stt_s cn52xxp1;
};

union cvmx_mio_uart2_tfl {
	uint64_t u64;
	struct cvmx_mio_uart2_tfl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t tfl:7;
#else
		uint64_t tfl:7;
		uint64_t reserved_7_63:57;
#endif
	} s;
	struct cvmx_mio_uart2_tfl_s cn52xx;
	struct cvmx_mio_uart2_tfl_s cn52xxp1;
};

union cvmx_mio_uart2_tfr {
	uint64_t u64;
	struct cvmx_mio_uart2_tfr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t tfr:8;
#else
		uint64_t tfr:8;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_mio_uart2_tfr_s cn52xx;
	struct cvmx_mio_uart2_tfr_s cn52xxp1;
};

union cvmx_mio_uart2_thr {
	uint64_t u64;
	struct cvmx_mio_uart2_thr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t thr:8;
#else
		uint64_t thr:8;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_mio_uart2_thr_s cn52xx;
	struct cvmx_mio_uart2_thr_s cn52xxp1;
};

union cvmx_mio_uart2_usr {
	uint64_t u64;
	struct cvmx_mio_uart2_usr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_5_63:59;
		uint64_t rff:1;
		uint64_t rfne:1;
		uint64_t tfe:1;
		uint64_t tfnf:1;
		uint64_t busy:1;
#else
		uint64_t busy:1;
		uint64_t tfnf:1;
		uint64_t tfe:1;
		uint64_t rfne:1;
		uint64_t rff:1;
		uint64_t reserved_5_63:59;
#endif
	} s;
	struct cvmx_mio_uart2_usr_s cn52xx;
	struct cvmx_mio_uart2_usr_s cn52xxp1;
};

#endif

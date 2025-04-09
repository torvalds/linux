/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx Zynq MPSoC Firmware layer
 *
 *  Copyright (C) 2014-2018 Xilinx, Inc.
 *
 */

#ifndef _XLNX_ZYNQMP_CLK_H
#define _XLNX_ZYNQMP_CLK_H

#define IOPLL			0
#define RPLL			1
#define APLL			2
#define DPLL			3
#define VPLL			4
#define IOPLL_TO_FPD		5
#define RPLL_TO_FPD		6
#define APLL_TO_LPD		7
#define DPLL_TO_LPD		8
#define VPLL_TO_LPD		9
#define ACPU			10
#define ACPU_HALF		11
#define DBF_FPD			12
#define DBF_LPD			13
#define DBG_TRACE		14
#define DBG_TSTMP		15
#define DP_VIDEO_REF		16
#define DP_AUDIO_REF		17
#define DP_STC_REF		18
#define GDMA_REF		19
#define DPDMA_REF		20
#define DDR_REF			21
#define SATA_REF		22
#define PCIE_REF		23
#define GPU_REF			24
#define GPU_PP0_REF		25
#define GPU_PP1_REF		26
#define TOPSW_MAIN		27
#define TOPSW_LSBUS		28
#define GTGREF0_REF		29
#define LPD_SWITCH		30
#define LPD_LSBUS		31
#define USB0_BUS_REF		32
#define USB1_BUS_REF		33
#define USB3_DUAL_REF		34
#define USB0			35
#define USB1			36
#define CPU_R5			37
#define CPU_R5_CORE		38
#define CSU_SPB			39
#define CSU_PLL			40
#define PCAP			41
#define IOU_SWITCH		42
#define GEM_TSU_REF		43
#define GEM_TSU			44
#define GEM0_TX			45
#define GEM1_TX			46
#define GEM2_TX			47
#define GEM3_TX			48
#define GEM0_RX			49
#define GEM1_RX			50
#define GEM2_RX			51
#define GEM3_RX			52
#define QSPI_REF		53
#define SDIO0_REF		54
#define SDIO1_REF		55
#define UART0_REF		56
#define UART1_REF		57
#define SPI0_REF		58
#define SPI1_REF		59
#define NAND_REF		60
#define I2C0_REF		61
#define I2C1_REF		62
#define CAN0_REF		63
#define CAN1_REF		64
#define CAN0			65
#define CAN1			66
#define DLL_REF			67
#define ADMA_REF		68
#define TIMESTAMP_REF		69
#define AMS_REF			70
#define PL0_REF			71
#define PL1_REF			72
#define PL2_REF			73
#define PL3_REF			74
#define WDT			75
#define IOPLL_INT		76
#define IOPLL_PRE_SRC		77
#define IOPLL_HALF		78
#define IOPLL_INT_MUX		79
#define IOPLL_POST_SRC		80
#define RPLL_INT		81
#define RPLL_PRE_SRC		82
#define RPLL_HALF		83
#define RPLL_INT_MUX		84
#define RPLL_POST_SRC		85
#define APLL_INT		86
#define APLL_PRE_SRC		87
#define APLL_HALF		88
#define APLL_INT_MUX		89
#define APLL_POST_SRC		90
#define DPLL_INT		91
#define DPLL_PRE_SRC		92
#define DPLL_HALF		93
#define DPLL_INT_MUX		94
#define DPLL_POST_SRC		95
#define VPLL_INT		96
#define VPLL_PRE_SRC		97
#define VPLL_HALF		98
#define VPLL_INT_MUX		99
#define VPLL_POST_SRC		100
#define CAN0_MIO		101
#define CAN1_MIO		102
#define ACPU_FULL		103
#define GEM0_REF		104
#define GEM1_REF		105
#define GEM2_REF		106
#define GEM3_REF		107
#define GEM0_REF_UNG		108
#define GEM1_REF_UNG		109
#define GEM2_REF_UNG		110
#define GEM3_REF_UNG		111
#define LPD_WDT			112

#endif /* _XLNX_ZYNQMP_CLK_H */

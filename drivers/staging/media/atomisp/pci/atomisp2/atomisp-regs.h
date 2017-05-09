/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2012 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#ifndef ATOMISP_REGS_H
#define ATOMISP_REGS_H

/* common register definitions */
#define PUNIT_PORT		0x04
#define CCK_PORT		0x14

#define PCICMDSTS		0x01
#define INTR			0x0f
#define MSI_CAPID		0x24
#define MSI_ADDRESS		0x25
#define MSI_DATA		0x26
#define INTR_CTL		0x27

#define PCI_MSI_CAPID		0x90
#define PCI_MSI_ADDR		0x94
#define PCI_MSI_DATA		0x98
#define PCI_INTERRUPT_CTRL	0x9C
#define PCI_I_CONTROL		0xfc

/* MRFLD specific register definitions */
#define MRFLD_CSI_AFE		0x39
#define MRFLD_CSI_CONTROL	0x3a
#define MRFLD_CSI_RCOMP		0x3d

#define MRFLD_PCI_PMCS		0x84
#define MRFLD_PCI_CSI_ACCESS_CTRL_VIOL	0xd4
#define MRFLD_PCI_CSI_AFE_HS_CONTROL	0xdc
#define MRFLD_PCI_CSI_AFE_RCOMP_CONTROL	0xe0
#define MRFLD_PCI_CSI_CONTROL		0xe8
#define MRFLD_PCI_CSI_AFE_TRIM_CONTROL	0xe4
#define MRFLD_PCI_CSI_DEADLINE_CONTROL	0xec
#define MRFLD_PCI_CSI_RCOMP_CONTROL	0xf4

/* Select Arasan (legacy)/Intel input system */
#define MRFLD_PCI_CSI_CONTROL_PARPATHEN	BIT(24)
/* Enable CSI interface (ANN B0/K0) */
#define MRFLD_PCI_CSI_CONTROL_CSI_READY	BIT(25)

/*
 * Enables the combining of adjacent 32-byte read requests to the same
 * cache line. When cleared, each 32-byte read request is sent as a
 * separate request on the IB interface.
 */
#define MRFLD_PCI_I_CONTROL_ENABLE_READ_COMBINING	0x1

/*
 * Register: MRFLD_PCI_CSI_RCOMP_CONTROL
 * If cleared, the high speed clock going to the digital logic is gated when
 * RCOMP update is happening. The clock is gated for a minimum of 100 nsec.
 * If this bit is set, then the high speed clock is not gated during the
 * update cycle.
 */
#define MRFLD_PCI_CSI_HS_OVR_CLK_GATE_ON_UPDATE		0x800000

/*
 * Enables the combining of adjacent 32-byte write requests to the same
 * cache line. When cleared, each 32-byte write request is sent as a
 * separate request on the IB interface.
 */
#define MRFLD_PCI_I_CONTROL_ENABLE_WRITE_COMBINING	0x2

#define MRFLD_PCI_I_CONTROL_SRSE_RESET_MASK		0xc

#define MRFLD_PCI_CSI1_HSRXCLKTRIM		0x2
#define MRFLD_PCI_CSI1_HSRXCLKTRIM_SHIFT	16
#define MRFLD_PCI_CSI2_HSRXCLKTRIM		0x3
#define MRFLD_PCI_CSI2_HSRXCLKTRIM_SHIFT	24
#define MRFLD_PCI_CSI3_HSRXCLKTRIM		0x2
#define MRFLD_PCI_CSI3_HSRXCLKTRIM_SHIFT	28
#define MRFLD_PCI_CSI_HSRXCLKTRIM_MASK		0xf

/*
 * This register is IUINT MMIO register, it is used to select the CSI
 * receiver backend.
 * 1: SH CSI backend
 * 0: Arasan CSI backend
 */
#define MRFLD_CSI_RECEIVER_SELECTION_REG       0x8081c

#define MRFLD_INTR_CLEAR_REG		       0x50c
#define MRFLD_INTR_STATUS_REG		       0x508
#define MRFLD_INTR_ENABLE_REG		       0x510

#define MRFLD_MAX_ZOOM_FACTOR	1024

/* MRFLD ISP POWER related */
#define MRFLD_ISPSSPM0         0x39
#define MRFLD_ISPSSPM0_ISPSSC_OFFSET   0
#define MRFLD_ISPSSPM0_ISPSSS_OFFSET   24
#define MRFLD_ISPSSPM0_ISPSSC_MASK     0x3
#define MRFLD_ISPSSPM0_IUNIT_POWER_ON  0
#define MRFLD_ISPSSPM0_IUNIT_POWER_OFF 0x3
#define MRFLD_ISPSSDVFS			0x13F
#define MRFLD_BIT0			0x0001
#define MRFLD_BIT1			0x0002

/* MRFLD CSI lane configuration related */
#define MRFLD_PORT_CONFIG_NUM  8
#define MRFLD_PORT_NUM         3
#define MRFLD_PORT1_ENABLE_SHIFT       0
#define MRFLD_PORT2_ENABLE_SHIFT       1
#define MRFLD_PORT3_ENABLE_SHIFT       2
#define MRFLD_PORT1_LANES_SHIFT        3
#define MRFLD_PORT2_LANES_SHIFT        7
#define MRFLD_PORT3_LANES_SHIFT        8
#define MRFLD_PORT_CONFIG_MASK 0x000f03ff
#define MRFLD_PORT_CONFIGCODE_SHIFT    16
#define MRFLD_ALL_CSI_PORTS_OFF_MASK   0x7

#define CHV_PORT3_LANES_SHIFT		9
#define CHV_PORT_CONFIG_MASK		0x1f07ff

#define ISPSSPM1				0x3a
#define ISP_FREQ_STAT_MASK			(0x1f << ISP_FREQ_STAT_OFFSET)
#define ISP_REQ_FREQ_MASK			0x1f
#define ISP_FREQ_VALID_MASK			(0x1 << ISP_FREQ_VALID_OFFSET)
#define ISP_FREQ_STAT_OFFSET			0x18
#define ISP_REQ_GUAR_FREQ_OFFSET		0x8
#define ISP_REQ_FREQ_OFFSET			0x0
#define ISP_FREQ_VALID_OFFSET			0x7
#define ISP_FREQ_RULE_ANY			0x0

#define ISP_FREQ_457MHZ				0x1C9
#define ISP_FREQ_400MHZ				0x190
#define ISP_FREQ_356MHZ				0x164
#define ISP_FREQ_320MHZ				0x140
#define ISP_FREQ_266MHZ				0x10a
#define ISP_FREQ_200MHZ				0xc8
#define ISP_FREQ_100MHZ				0x64

#define HPLL_FREQ_800MHZ			0x320
#define HPLL_FREQ_1600MHZ			0x640
#define HPLL_FREQ_2000MHZ			0x7D0

#define CCK_FUSE_REG_0			0x08
#define CCK_FUSE_HPLL_FREQ_MASK		0x03

#if defined(ISP2401)
#define ISP_FREQ_MAX	ISP_FREQ_320MHZ
#else
#define ISP_FREQ_MAX	ISP_FREQ_400MHZ
#endif

/* ISP2401 CSI2+ receiver delay settings */
#define CSI2_PORT_A_BASE					0xC0000
#define CSI2_PORT_B_BASE					0xC2000
#define CSI2_PORT_C_BASE					0xC4000

#define CSI2_LANE_CL_BASE					0x418
#define CSI2_LANE_D0_BASE					0x420
#define CSI2_LANE_D1_BASE					0x428
#define CSI2_LANE_D2_BASE					0x430
#define CSI2_LANE_D3_BASE					0x438

#define CSI2_REG_RX_CSI_DLY_CNT_TERMEN				0
#define CSI2_REG_RX_CSI_DLY_CNT_SETTLE				0x4

#define CSI2_PORT_A_RX_CSI_DLY_CNT_TERMEN_CLANE			0xC0418
#define CSI2_PORT_A_RX_CSI_DLY_CNT_SETTLE_CLANE			0xC041C
#define CSI2_PORT_A_RX_CSI_DLY_CNT_TERMEN_DLANE0		0xC0420
#define CSI2_PORT_A_RX_CSI_DLY_CNT_SETTLE_DLANE0		0xC0424
#define CSI2_PORT_A_RX_CSI_DLY_CNT_TERMEN_DLANE1		0xC0428
#define CSI2_PORT_A_RX_CSI_DLY_CNT_SETTLE_DLANE1		0xC042C
#define CSI2_PORT_A_RX_CSI_DLY_CNT_TERMEN_DLANE2		0xC0430
#define CSI2_PORT_A_RX_CSI_DLY_CNT_SETTLE_DLANE2		0xC0434
#define CSI2_PORT_A_RX_CSI_DLY_CNT_TERMEN_DLANE3		0xC0438
#define CSI2_PORT_A_RX_CSI_DLY_CNT_SETTLE_DLANE3		0xC043C

#define CSI2_PORT_B_RX_CSI_DLY_CNT_TERMEN_CLANE			0xC2418
#define CSI2_PORT_B_RX_CSI_DLY_CNT_SETTLE_CLANE			0xC241C
#define CSI2_PORT_B_RX_CSI_DLY_CNT_TERMEN_DLANE0		0xC2420
#define CSI2_PORT_B_RX_CSI_DLY_CNT_SETTLE_DLANE0		0xC2424
#define CSI2_PORT_B_RX_CSI_DLY_CNT_TERMEN_DLANE1		0xC2428
#define CSI2_PORT_B_RX_CSI_DLY_CNT_SETTLE_DLANE1		0xC242C

#define CSI2_PORT_C_RX_CSI_DLY_CNT_TERMEN_CLANE			0xC4418
#define CSI2_PORT_C_RX_CSI_DLY_CNT_SETTLE_CLANE			0xC441C
#define CSI2_PORT_C_RX_CSI_DLY_CNT_TERMEN_DLANE0		0xC4420
#define CSI2_PORT_C_RX_CSI_DLY_CNT_SETTLE_DLANE0		0xC4424
#define CSI2_PORT_C_RX_CSI_DLY_CNT_TERMEN_DLANE1		0xC4428
#define CSI2_PORT_C_RX_CSI_DLY_CNT_SETTLE_DLANE1		0xC442C

#define DMA_BURST_SIZE_REG					0xCD408

#define ISP_DFS_TRY_TIMES	2

#endif /* ATOMISP_REGS_H */

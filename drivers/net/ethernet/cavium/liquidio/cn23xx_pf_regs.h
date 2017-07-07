/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2016 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more details.
 ***********************************************************************/
/*! \file cn23xx_regs.h
 * \brief Host Driver: Register Address and Register Mask values for
 * Octeon CN23XX devices.
 */

#ifndef __CN23XX_PF_REGS_H__
#define __CN23XX_PF_REGS_H__

#define     CN23XX_CONFIG_VENDOR_ID	0x00
#define     CN23XX_CONFIG_DEVICE_ID	0x02

#define     CN23XX_CONFIG_XPANSION_BAR             0x38

#define     CN23XX_CONFIG_MSIX_CAP		   0x50
#define     CN23XX_CONFIG_MSIX_LMSI		   0x54
#define     CN23XX_CONFIG_MSIX_UMSI		   0x58
#define     CN23XX_CONFIG_MSIX_MSIMD		   0x5C
#define     CN23XX_CONFIG_MSIX_MSIMM		   0x60
#define     CN23XX_CONFIG_MSIX_MSIMP		   0x64

#define     CN23XX_CONFIG_PCIE_CAP                 0x70
#define     CN23XX_CONFIG_PCIE_DEVCAP              0x74
#define     CN23XX_CONFIG_PCIE_DEVCTL              0x78
#define     CN23XX_CONFIG_PCIE_LINKCAP             0x7C
#define     CN23XX_CONFIG_PCIE_LINKCTL             0x80
#define     CN23XX_CONFIG_PCIE_SLOTCAP             0x84
#define     CN23XX_CONFIG_PCIE_SLOTCTL             0x88
#define     CN23XX_CONFIG_PCIE_DEVCTL2             0x98
#define     CN23XX_CONFIG_PCIE_LINKCTL2            0xA0
#define     CN23XX_CONFIG_PCIE_UNCORRECT_ERR_MASK  0x108
#define     CN23XX_CONFIG_PCIE_CORRECT_ERR_STATUS  0x110
#define     CN23XX_CONFIG_PCIE_DEVCTL_MASK         0x00040000

#define     CN23XX_PCIE_SRIOV_FDL		   0x188
#define     CN23XX_PCIE_SRIOV_FDL_BIT_POS	   0x10
#define     CN23XX_PCIE_SRIOV_FDL_MASK		   0xFF

#define     CN23XX_CONFIG_PCIE_FLTMSK              0x720

#define     CN23XX_CONFIG_SRIOV_VFDEVID            0x190

#define     CN23XX_CONFIG_SRIOV_BAR_START	   0x19C
#define     CN23XX_CONFIG_SRIOV_BARX(i)		\
		(CN23XX_CONFIG_SRIOV_BAR_START + ((i) * 4))
#define     CN23XX_CONFIG_SRIOV_BAR_PF		   0x08
#define     CN23XX_CONFIG_SRIOV_BAR_64BIT	   0x04
#define     CN23XX_CONFIG_SRIOV_BAR_IO		   0x01

/* ##############  BAR0 Registers ################ */

#define    CN23XX_SLI_CTL_PORT_START               0x286E0
#define    CN23XX_PORT_OFFSET                      0x10

#define    CN23XX_SLI_CTL_PORT(p)                  \
		(CN23XX_SLI_CTL_PORT_START + ((p) * CN23XX_PORT_OFFSET))

/* 2 scatch registers (64-bit)  */
#define    CN23XX_SLI_WINDOW_CTL                   0x282E0
#define    CN23XX_SLI_SCRATCH1                     0x283C0
#define    CN23XX_SLI_SCRATCH2                     0x283D0
#define    CN23XX_SLI_WINDOW_CTL_DEFAULT           0x200000ULL

/* 1 registers (64-bit)  - SLI_CTL_STATUS */
#define    CN23XX_SLI_CTL_STATUS                   0x28570

/* SLI Packet Input Jabber Register (64 bit register)
 * <31:0> for Byte count for limiting sizes of packet sizes
 * that are allowed for sli packet inbound packets.
 * the default value is 0xFA00(=64000).
 */
#define    CN23XX_SLI_PKT_IN_JABBER                0x29170
/* The input jabber is used to determine the TSO max size.
 * Due to H/W limitation, this need to be reduced to 60000
 * in order to to H/W TSO and avoid the WQE malfarmation
 * PKO_BUG_24989_WQE_LEN
 */
#define    CN23XX_DEFAULT_INPUT_JABBER             0xEA60 /*60000*/

#define    CN23XX_WIN_WR_ADDR_LO                   0x20000
#define    CN23XX_WIN_WR_ADDR_HI                   0x20004
#define    CN23XX_WIN_WR_ADDR64                    CN23XX_WIN_WR_ADDR_LO

#define    CN23XX_WIN_RD_ADDR_LO                   0x20010
#define    CN23XX_WIN_RD_ADDR_HI                   0x20014
#define    CN23XX_WIN_RD_ADDR64                    CN23XX_WIN_RD_ADDR_LO

#define    CN23XX_WIN_WR_DATA_LO                   0x20020
#define    CN23XX_WIN_WR_DATA_HI                   0x20024
#define    CN23XX_WIN_WR_DATA64                    CN23XX_WIN_WR_DATA_LO

#define    CN23XX_WIN_RD_DATA_LO                   0x20040
#define    CN23XX_WIN_RD_DATA_HI                   0x20044
#define    CN23XX_WIN_RD_DATA64                    CN23XX_WIN_RD_DATA_LO

#define    CN23XX_WIN_WR_MASK_LO                   0x20030
#define    CN23XX_WIN_WR_MASK_HI                   0x20034
#define    CN23XX_WIN_WR_MASK_REG                  CN23XX_WIN_WR_MASK_LO
#define    CN23XX_SLI_MAC_CREDIT_CNT               0x23D70

/* 4 registers (64-bit) for mapping IOQs to MACs(PEMs)-
 * SLI_PKT_MAC(0..3)_PF(0..1)_RINFO
 */
#define    CN23XX_SLI_PKT_MAC_RINFO_START64       0x29030

/*1 register (64-bit) to determine whether IOQs are in reset. */
#define    CN23XX_SLI_PKT_IOQ_RING_RST            0x291E0

/* Each Input Queue register is at a 16-byte Offset in BAR0 */
#define    CN23XX_IQ_OFFSET                       0x20000

#define    CN23XX_MAC_RINFO_OFFSET                0x20
#define    CN23XX_PF_RINFO_OFFSET                 0x10

#define CN23XX_SLI_PKT_MAC_RINFO64(mac, pf)		\
		(CN23XX_SLI_PKT_MAC_RINFO_START64 +     \
		 ((mac) * CN23XX_MAC_RINFO_OFFSET) +	\
		 ((pf) * CN23XX_PF_RINFO_OFFSET))

/** mask for total rings, setting TRS to base */
#define    CN23XX_PKT_MAC_CTL_RINFO_TRS               BIT_ULL(16)
/** mask for starting ring number: setting SRN <6:0> = 0x7F */
#define    CN23XX_PKT_MAC_CTL_RINFO_SRN               (0x7F)

/* Starting bit of the TRS field in CN23XX_SLI_PKT_MAC_RINFO64 register */
#define    CN23XX_PKT_MAC_CTL_RINFO_TRS_BIT_POS     16
/* Starting bit of SRN field in CN23XX_SLI_PKT_MAC_RINFO64 register */
#define    CN23XX_PKT_MAC_CTL_RINFO_SRN_BIT_POS     0
/* Starting bit of RPVF field in CN23XX_SLI_PKT_MAC_RINFO64 register */
#define    CN23XX_PKT_MAC_CTL_RINFO_RPVF_BIT_POS     32
/* Starting bit of NVFS field in CN23XX_SLI_PKT_MAC_RINFO64 register */
#define    CN23XX_PKT_MAC_CTL_RINFO_NVFS_BIT_POS     48

/*###################### REQUEST QUEUE #########################*/

/* 64 registers for Input Queue Instr Count - SLI_PKT_IN_DONE0_CNTS */
#define    CN23XX_SLI_IQ_INSTR_COUNT_START64     0x10040

/* 64 registers for Input Queues Start Addr - SLI_PKT0_INSTR_BADDR */
#define    CN23XX_SLI_IQ_BASE_ADDR_START64       0x10010

/* 64 registers for Input Doorbell - SLI_PKT0_INSTR_BAOFF_DBELL */
#define    CN23XX_SLI_IQ_DOORBELL_START          0x10020

/* 64 registers for Input Queue size - SLI_PKT0_INSTR_FIFO_RSIZE */
#define    CN23XX_SLI_IQ_SIZE_START              0x10030

/* 64 registers (64-bit) - ES, RO, NS, Arbitration for Input Queue Data &
 * gather list fetches. SLI_PKT(0..63)_INPUT_CONTROL.
 */
#define    CN23XX_SLI_IQ_PKT_CONTROL_START64    0x10000

/*------- Request Queue Macros ---------*/
#define    CN23XX_SLI_IQ_PKT_CONTROL64(iq)          \
		(CN23XX_SLI_IQ_PKT_CONTROL_START64 + ((iq) * CN23XX_IQ_OFFSET))

#define    CN23XX_SLI_IQ_BASE_ADDR64(iq)          \
		(CN23XX_SLI_IQ_BASE_ADDR_START64 + ((iq) * CN23XX_IQ_OFFSET))

#define    CN23XX_SLI_IQ_SIZE(iq)                 \
		(CN23XX_SLI_IQ_SIZE_START + ((iq) * CN23XX_IQ_OFFSET))

#define    CN23XX_SLI_IQ_DOORBELL(iq)             \
		(CN23XX_SLI_IQ_DOORBELL_START + ((iq) * CN23XX_IQ_OFFSET))

#define    CN23XX_SLI_IQ_INSTR_COUNT64(iq)          \
		(CN23XX_SLI_IQ_INSTR_COUNT_START64 + ((iq) * CN23XX_IQ_OFFSET))

/*------------------ Masks ----------------*/
#define    CN23XX_PKT_INPUT_CTL_VF_NUM                  BIT_ULL(32)
#define    CN23XX_PKT_INPUT_CTL_MAC_NUM                 BIT(29)
/* Number of instructions to be read in one MAC read request.
 * setting to Max value(4)
 */
#define    CN23XX_PKT_INPUT_CTL_RDSIZE                  (3 << 25)
#define    CN23XX_PKT_INPUT_CTL_IS_64B                  BIT(24)
#define    CN23XX_PKT_INPUT_CTL_RST                     BIT(23)
#define    CN23XX_PKT_INPUT_CTL_QUIET                   BIT(28)
#define    CN23XX_PKT_INPUT_CTL_RING_ENB                BIT(22)
#define    CN23XX_PKT_INPUT_CTL_DATA_NS                 BIT(8)
#define    CN23XX_PKT_INPUT_CTL_DATA_ES_64B_SWAP        BIT(6)
#define    CN23XX_PKT_INPUT_CTL_DATA_RO                 BIT(5)
#define    CN23XX_PKT_INPUT_CTL_USE_CSR                 BIT(4)
#define    CN23XX_PKT_INPUT_CTL_GATHER_NS               BIT(3)
#define    CN23XX_PKT_INPUT_CTL_GATHER_ES_64B_SWAP      (2)
#define    CN23XX_PKT_INPUT_CTL_GATHER_RO               (1)

/** Rings per Virtual Function **/
#define    CN23XX_PKT_INPUT_CTL_RPVF_MASK               (0x3F)
#define    CN23XX_PKT_INPUT_CTL_RPVF_POS                (48)
/** These bits[47:44] select the Physical function number within the MAC */
#define    CN23XX_PKT_INPUT_CTL_PF_NUM_MASK             (0x7)
#define    CN23XX_PKT_INPUT_CTL_PF_NUM_POS              (45)
/** These bits[43:32] select the function number within the PF */
#define    CN23XX_PKT_INPUT_CTL_VF_NUM_MASK             (0x1FFF)
#define    CN23XX_PKT_INPUT_CTL_VF_NUM_POS              (32)
#define    CN23XX_PKT_INPUT_CTL_MAC_NUM_MASK            (0x3)
#define    CN23XX_PKT_INPUT_CTL_MAC_NUM_POS             (29)
#define    CN23XX_PKT_IN_DONE_WMARK_MASK                (0xFFFFULL)
#define    CN23XX_PKT_IN_DONE_WMARK_BIT_POS             (32)
#define    CN23XX_PKT_IN_DONE_CNT_MASK                  (0x00000000FFFFFFFFULL)

#ifdef __LITTLE_ENDIAN_BITFIELD
#define    CN23XX_PKT_INPUT_CTL_MASK				\
		(CN23XX_PKT_INPUT_CTL_RDSIZE		|	\
		 CN23XX_PKT_INPUT_CTL_DATA_ES_64B_SWAP	|	\
		 CN23XX_PKT_INPUT_CTL_USE_CSR)
#else
#define    CN23XX_PKT_INPUT_CTL_MASK				\
		(CN23XX_PKT_INPUT_CTL_RDSIZE		|	\
		 CN23XX_PKT_INPUT_CTL_DATA_ES_64B_SWAP	|	\
		 CN23XX_PKT_INPUT_CTL_USE_CSR		|	\
		 CN23XX_PKT_INPUT_CTL_GATHER_ES_64B_SWAP)
#endif

/** Masks for SLI_PKT_IN_DONE(0..63)_CNTS Register */
#define    CN23XX_IN_DONE_CNTS_PI_INT               BIT_ULL(62)
#define    CN23XX_IN_DONE_CNTS_CINT_ENB             BIT_ULL(48)

/*############################ OUTPUT QUEUE #########################*/

/* 64 registers for Output queue control - SLI_PKT(0..63)_OUTPUT_CONTROL */
#define    CN23XX_SLI_OQ_PKT_CONTROL_START       0x10050

/* 64 registers for Output queue buffer and info size - SLI_PKT0_OUT_SIZE */
#define    CN23XX_SLI_OQ0_BUFF_INFO_SIZE         0x10060

/* 64 registers for Output Queue Start Addr - SLI_PKT0_SLIST_BADDR */
#define    CN23XX_SLI_OQ_BASE_ADDR_START64       0x10070

/* 64 registers for Output Queue Packet Credits - SLI_PKT0_SLIST_BAOFF_DBELL */
#define    CN23XX_SLI_OQ_PKT_CREDITS_START       0x10080

/* 64 registers for Output Queue size - SLI_PKT0_SLIST_FIFO_RSIZE */
#define    CN23XX_SLI_OQ_SIZE_START              0x10090

/* 64 registers for Output Queue Packet Count - SLI_PKT0_CNTS */
#define    CN23XX_SLI_OQ_PKT_SENT_START          0x100B0

/* 64 registers for Output Queue INT Levels - SLI_PKT0_INT_LEVELS */
#define    CN23XX_SLI_OQ_PKT_INT_LEVELS_START64   0x100A0

/* Each Output Queue register is at a 16-byte Offset in BAR0 */
#define    CN23XX_OQ_OFFSET                      0x20000

/* 1 (64-bit register) for Output Queue backpressure across all rings. */
#define    CN23XX_SLI_OQ_WMARK                   0x29180

/* Global pkt control register */
#define    CN23XX_SLI_GBL_CONTROL                0x29210

/* Backpressure enable register for PF0  */
#define    CN23XX_SLI_OUT_BP_EN_W1S              0x29260

/* Backpressure enable register for PF1  */
#define    CN23XX_SLI_OUT_BP_EN2_W1S             0x29270

/* Backpressure disable register for PF0  */
#define    CN23XX_SLI_OUT_BP_EN_W1C              0x29280

/* Backpressure disable register for PF1  */
#define    CN23XX_SLI_OUT_BP_EN2_W1C             0x29290

/*------- Output Queue Macros ---------*/

#define    CN23XX_SLI_OQ_PKT_CONTROL(oq)          \
		(CN23XX_SLI_OQ_PKT_CONTROL_START + ((oq) * CN23XX_OQ_OFFSET))

#define    CN23XX_SLI_OQ_BASE_ADDR64(oq)          \
		(CN23XX_SLI_OQ_BASE_ADDR_START64 + ((oq) * CN23XX_OQ_OFFSET))

#define    CN23XX_SLI_OQ_SIZE(oq)                 \
		(CN23XX_SLI_OQ_SIZE_START + ((oq) * CN23XX_OQ_OFFSET))

#define    CN23XX_SLI_OQ_BUFF_INFO_SIZE(oq)                 \
		(CN23XX_SLI_OQ0_BUFF_INFO_SIZE + ((oq) * CN23XX_OQ_OFFSET))

#define    CN23XX_SLI_OQ_PKTS_SENT(oq)            \
		(CN23XX_SLI_OQ_PKT_SENT_START + ((oq) * CN23XX_OQ_OFFSET))

#define    CN23XX_SLI_OQ_PKTS_CREDIT(oq)          \
		(CN23XX_SLI_OQ_PKT_CREDITS_START + ((oq) * CN23XX_OQ_OFFSET))

#define    CN23XX_SLI_OQ_PKT_INT_LEVELS(oq)		\
		(CN23XX_SLI_OQ_PKT_INT_LEVELS_START64 +	\
		 ((oq) * CN23XX_OQ_OFFSET))

/*Macro's for accessing CNT and TIME separately from INT_LEVELS*/
#define    CN23XX_SLI_OQ_PKT_INT_LEVELS_CNT(oq)		\
		(CN23XX_SLI_OQ_PKT_INT_LEVELS_START64 + \
		 ((oq) * CN23XX_OQ_OFFSET))

#define    CN23XX_SLI_OQ_PKT_INT_LEVELS_TIME(oq)	\
		(CN23XX_SLI_OQ_PKT_INT_LEVELS_START64 +	\
		 ((oq) * CN23XX_OQ_OFFSET) + 4)

/*------------------ Masks ----------------*/
#define    CN23XX_PKT_OUTPUT_CTL_TENB                  BIT(13)
#define    CN23XX_PKT_OUTPUT_CTL_CENB                  BIT(12)
#define    CN23XX_PKT_OUTPUT_CTL_IPTR                  BIT(11)
#define    CN23XX_PKT_OUTPUT_CTL_ES                    BIT(9)
#define    CN23XX_PKT_OUTPUT_CTL_NSR                   BIT(8)
#define    CN23XX_PKT_OUTPUT_CTL_ROR                   BIT(7)
#define    CN23XX_PKT_OUTPUT_CTL_DPTR                  BIT(6)
#define    CN23XX_PKT_OUTPUT_CTL_BMODE                 BIT(5)
#define    CN23XX_PKT_OUTPUT_CTL_ES_P                  BIT(3)
#define    CN23XX_PKT_OUTPUT_CTL_NSR_P                 BIT(2)
#define    CN23XX_PKT_OUTPUT_CTL_ROR_P                 BIT(1)
#define    CN23XX_PKT_OUTPUT_CTL_RING_ENB              BIT(0)

/*######################### Mailbox Reg Macros ########################*/
#define    CN23XX_SLI_PKT_MBOX_INT_START             0x10210
#define    CN23XX_SLI_PKT_PF_VF_MBOX_SIG_START       0x10200
#define    CN23XX_SLI_MAC_PF_MBOX_INT_START          0x27380

#define    CN23XX_SLI_MBOX_OFFSET		     0x20000
#define    CN23XX_SLI_MBOX_SIG_IDX_OFFSET	     0x8

#define    CN23XX_SLI_PKT_MBOX_INT(q)          \
		(CN23XX_SLI_PKT_MBOX_INT_START + ((q) * CN23XX_SLI_MBOX_OFFSET))

#define    CN23XX_SLI_PKT_PF_VF_MBOX_SIG(q, idx)		\
		(CN23XX_SLI_PKT_PF_VF_MBOX_SIG_START +		\
		 ((q) * CN23XX_SLI_MBOX_OFFSET +		\
		  (idx) * CN23XX_SLI_MBOX_SIG_IDX_OFFSET))

#define    CN23XX_SLI_MAC_PF_MBOX_INT(mac, pf)		\
		(CN23XX_SLI_MAC_PF_MBOX_INT_START +	\
		 ((mac) * CN23XX_MAC_INT_OFFSET +	\
		  (pf) * CN23XX_PF_INT_OFFSET))

/*######################### DMA Counters #########################*/

/* 2 registers (64-bit) - DMA Count - 1 for each DMA counter 0/1. */
#define    CN23XX_DMA_CNT_START                   0x28400

/* 2 registers (64-bit) - DMA Timer 0/1, contains DMA timer values */
/* SLI_DMA_0_TIM */
#define    CN23XX_DMA_TIM_START                   0x28420

/* 2 registers (64-bit) - DMA count & Time Interrupt threshold -
 * SLI_DMA_0_INT_LEVEL
 */
#define    CN23XX_DMA_INT_LEVEL_START             0x283E0

/* Each DMA register is at a 16-byte Offset in BAR0 */
#define    CN23XX_DMA_OFFSET                      0x10

/*---------- DMA Counter Macros ---------*/
#define    CN23XX_DMA_CNT(dq)                      \
		(CN23XX_DMA_CNT_START + ((dq) * CN23XX_DMA_OFFSET))

#define    CN23XX_DMA_INT_LEVEL(dq)                \
		(CN23XX_DMA_INT_LEVEL_START + ((dq) * CN23XX_DMA_OFFSET))

#define    CN23XX_DMA_PKT_INT_LEVEL(dq)            \
		(CN23XX_DMA_INT_LEVEL_START + ((dq) * CN23XX_DMA_OFFSET))

#define    CN23XX_DMA_TIME_INT_LEVEL(dq)           \
		(CN23XX_DMA_INT_LEVEL_START + 4 + ((dq) * CN23XX_DMA_OFFSET))

#define    CN23XX_DMA_TIM(dq)                     \
		(CN23XX_DMA_TIM_START + ((dq) * CN23XX_DMA_OFFSET))

/*######################## MSIX TABLE #########################*/

#define	CN23XX_MSIX_TABLE_ADDR_START		0x0
#define	CN23XX_MSIX_TABLE_DATA_START		0x8

#define	CN23XX_MSIX_TABLE_SIZE			0x10
#define	CN23XX_MSIX_TABLE_ENTRIES		0x41

#define CN23XX_MSIX_ENTRY_VECTOR_CTL	BIT_ULL(32)

#define	CN23XX_MSIX_TABLE_ADDR(idx)		\
	(CN23XX_MSIX_TABLE_ADDR_START + ((idx) * CN23XX_MSIX_TABLE_SIZE))

#define	CN23XX_MSIX_TABLE_DATA(idx)		\
	(CN23XX_MSIX_TABLE_DATA_START + ((idx) * CN23XX_MSIX_TABLE_SIZE))

/*######################## INTERRUPTS #########################*/
#define CN23XX_MAC_INT_OFFSET   0x20
#define CN23XX_PF_INT_OFFSET    0x10

/* 1 register (64-bit) for Interrupt Summary */
#define    CN23XX_SLI_INT_SUM64            0x27000

/* 4 registers (64-bit) for Interrupt Enable for each Port */
#define    CN23XX_SLI_INT_ENB64            0x27080

#define    CN23XX_SLI_MAC_PF_INT_SUM64(mac, pf)			\
		(CN23XX_SLI_INT_SUM64 +				\
		 ((mac) * CN23XX_MAC_INT_OFFSET) +		\
		 ((pf) * CN23XX_PF_INT_OFFSET))

#define    CN23XX_SLI_MAC_PF_INT_ENB64(mac, pf)		\
		(CN23XX_SLI_INT_ENB64 +			\
		 ((mac) * CN23XX_MAC_INT_OFFSET) +	\
		 ((pf) * CN23XX_PF_INT_OFFSET))

/* 1 register (64-bit) to indicate which Output Queue reached pkt threshold */
#define    CN23XX_SLI_PKT_CNT_INT                0x29130

/* 1 register (64-bit) to indicate which Output Queue reached time threshold */
#define    CN23XX_SLI_PKT_TIME_INT               0x29140

/*------------------ Interrupt Masks ----------------*/

#define    CN23XX_INTR_PO_INT			BIT_ULL(63)
#define    CN23XX_INTR_PI_INT			BIT_ULL(62)
#define    CN23XX_INTR_MBOX_INT			BIT_ULL(61)
#define    CN23XX_INTR_RESEND			BIT_ULL(60)

#define    CN23XX_INTR_CINT_ENB                 BIT_ULL(48)
#define    CN23XX_INTR_MBOX_ENB                 BIT(0)

#define    CN23XX_INTR_RML_TIMEOUT_ERR           (1)

#define    CN23XX_INTR_MIO_INT                   BIT(1)

#define    CN23XX_INTR_RESERVED1                 (3 << 2)

#define    CN23XX_INTR_PKT_COUNT                 BIT(4)
#define    CN23XX_INTR_PKT_TIME                  BIT(5)

#define    CN23XX_INTR_RESERVED2                 (3 << 6)

#define    CN23XX_INTR_M0UPB0_ERR                BIT(8)
#define    CN23XX_INTR_M0UPWI_ERR                BIT(9)
#define    CN23XX_INTR_M0UNB0_ERR                BIT(10)
#define    CN23XX_INTR_M0UNWI_ERR                BIT(11)

#define    CN23XX_INTR_RESERVED3                 (0xFFFFFULL << 12)

#define    CN23XX_INTR_DMA0_FORCE                BIT_ULL(32)
#define    CN23XX_INTR_DMA1_FORCE                BIT_ULL(33)

#define    CN23XX_INTR_DMA0_COUNT                BIT_ULL(34)
#define    CN23XX_INTR_DMA1_COUNT                BIT_ULL(35)

#define    CN23XX_INTR_DMA0_TIME                 BIT_ULL(36)
#define    CN23XX_INTR_DMA1_TIME                 BIT_ULL(37)

#define    CN23XX_INTR_RESERVED4                 (0x7FFFFULL << 38)

#define    CN23XX_INTR_VF_MBOX                   BIT_ULL(57)
#define    CN23XX_INTR_DMAVF_ERR                 BIT_ULL(58)
#define    CN23XX_INTR_DMAPF_ERR                 BIT_ULL(59)

#define    CN23XX_INTR_PKTVF_ERR                 BIT_ULL(60)
#define    CN23XX_INTR_PKTPF_ERR                 BIT_ULL(61)
#define    CN23XX_INTR_PPVF_ERR                  BIT_ULL(62)
#define    CN23XX_INTR_PPPF_ERR                  BIT_ULL(63)

#define    CN23XX_INTR_DMA0_DATA                 (CN23XX_INTR_DMA0_TIME)
#define    CN23XX_INTR_DMA1_DATA                 (CN23XX_INTR_DMA1_TIME)

#define    CN23XX_INTR_DMA_DATA                  \
		(CN23XX_INTR_DMA0_DATA | CN23XX_INTR_DMA1_DATA)

/* By fault only TIME based */
#define    CN23XX_INTR_PKT_DATA                  (CN23XX_INTR_PKT_TIME)
/* For both COUNT and TIME based */
/* #define    CN23XX_INTR_PKT_DATA                  \
 * (CN23XX_INTR_PKT_COUNT | CN23XX_INTR_PKT_TIME)
 */

/* Sum of interrupts for all PCI-Express Data Interrupts */
#define    CN23XX_INTR_PCIE_DATA                 \
		(CN23XX_INTR_DMA_DATA | CN23XX_INTR_PKT_DAT)

/* Sum of interrupts for error events */
#define    CN23XX_INTR_ERR			\
		(CN23XX_INTR_M0UPB0_ERR	|	\
		 CN23XX_INTR_M0UPWI_ERR	|	\
		 CN23XX_INTR_M0UNB0_ERR	|	\
		 CN23XX_INTR_M0UNWI_ERR	|	\
		 CN23XX_INTR_DMAVF_ERR	|	\
		 CN23XX_INTR_DMAPF_ERR	|	\
		 CN23XX_INTR_PKTPF_ERR	|	\
		 CN23XX_INTR_PPPF_ERR	|	\
		 CN23XX_INTR_PPVF_ERR)

/* Programmed Mask for Interrupt Sum */
#define    CN23XX_INTR_MASK			\
		(CN23XX_INTR_DMA_DATA	|	\
		 CN23XX_INTR_DMA0_FORCE	|	\
		 CN23XX_INTR_DMA1_FORCE	|	\
		 CN23XX_INTR_MIO_INT	|	\
		 CN23XX_INTR_ERR)

/* 4 Registers (64 - bit) */
#define    CN23XX_SLI_S2M_PORT_CTL_START         0x23D80
#define    CN23XX_SLI_S2M_PORTX_CTL(port)	\
		(CN23XX_SLI_S2M_PORT_CTL_START + ((port) * 0x10))

#define    CN23XX_SLI_MAC_NUMBER                 0x20050

/** PEM(0..3)_BAR1_INDEX(0..15)address is defined as
 *  addr = (0x00011800C0000100  |port <<24 |idx <<3 )
 *  Here, port is PEM(0..3) & idx is INDEX(0..15)
 */
#define    CN23XX_PEM_BAR1_INDEX_START             0x00011800C0000100ULL
#define    CN23XX_PEM_OFFSET                       24
#define    CN23XX_BAR1_INDEX_OFFSET                3

#define    CN23XX_PEM_BAR1_INDEX_REG(port, idx)		\
		(CN23XX_PEM_BAR1_INDEX_START + ((port) << CN23XX_PEM_OFFSET) + \
		 ((idx) << CN23XX_BAR1_INDEX_OFFSET))

/*############################ DPI #########################*/

/* 1 register (64-bit) - provides DMA Enable */
#define    CN23XX_DPI_CTL                 0x0001df0000000040ULL

/* 1 register (64-bit) - Controls the DMA IO Operation */
#define    CN23XX_DPI_DMA_CONTROL         0x0001df0000000048ULL

/* 1 register (64-bit) - Provides DMA Instr'n Queue Enable  */
#define    CN23XX_DPI_REQ_GBL_ENB         0x0001df0000000050ULL

/* 1 register (64-bit) - DPI_REQ_ERR_RSP
 * Indicates which Instr'n Queue received error response from the IO sub-system
 */
#define    CN23XX_DPI_REQ_ERR_RSP         0x0001df0000000058ULL

/* 1 register (64-bit) - DPI_REQ_ERR_RST
 * Indicates which Instr'n Queue dropped an Instr'n
 */
#define    CN23XX_DPI_REQ_ERR_RST         0x0001df0000000060ULL

/* 6 register (64-bit) - DPI_DMA_ENG(0..5)_EN
 * Provides DMA Engine Queue Enable
 */
#define    CN23XX_DPI_DMA_ENG0_ENB        0x0001df0000000080ULL
#define    CN23XX_DPI_DMA_ENG_ENB(eng) (CN23XX_DPI_DMA_ENG0_ENB + ((eng) * 8))

/* 8 register (64-bit) - DPI_DMA(0..7)_REQQ_CTL
 * Provides control bits for transaction on 8 Queues
 */
#define    CN23XX_DPI_DMA_REQQ0_CTL       0x0001df0000000180ULL
#define    CN23XX_DPI_DMA_REQQ_CTL(q_no)	\
		(CN23XX_DPI_DMA_REQQ0_CTL + ((q_no) * 8))

/* 6 register (64-bit) - DPI_ENG(0..5)_BUF
 * Provides DMA Engine FIFO (Queue) Size
 */
#define    CN23XX_DPI_DMA_ENG0_BUF        0x0001df0000000880ULL
#define    CN23XX_DPI_DMA_ENG_BUF(eng)   \
		(CN23XX_DPI_DMA_ENG0_BUF + ((eng) * 8))

/* 4 Registers (64-bit) */
#define    CN23XX_DPI_SLI_PRT_CFG_START   0x0001df0000000900ULL
#define    CN23XX_DPI_SLI_PRTX_CFG(port)        \
		(CN23XX_DPI_SLI_PRT_CFG_START + ((port) * 0x8))

/* Masks for DPI_DMA_CONTROL Register */
#define    CN23XX_DPI_DMA_COMMIT_MODE     BIT_ULL(58)
#define    CN23XX_DPI_DMA_PKT_EN          BIT_ULL(56)
#define    CN23XX_DPI_DMA_ENB             (0x0FULL << 48)
/* Set the DMA Control, to update packet count not byte count sent by DMA,
 * when we use Interrupt Coalescing (CA mode)
 */
#define    CN23XX_DPI_DMA_O_ADD1          BIT(19)
/*selecting 64-bit Byte Swap Mode */
#define    CN23XX_DPI_DMA_O_ES            BIT(15)
#define    CN23XX_DPI_DMA_O_MODE          BIT(14)

#define    CN23XX_DPI_DMA_CTL_MASK			\
		(CN23XX_DPI_DMA_COMMIT_MODE	|	\
		 CN23XX_DPI_DMA_PKT_EN		|	\
		 CN23XX_DPI_DMA_O_ES		|	\
		 CN23XX_DPI_DMA_O_MODE)

/*############################ RST #########################*/

#define    CN23XX_RST_BOOT            0x0001180006001600ULL
#define    CN23XX_RST_SOFT_RST        0x0001180006001680ULL

#define    CN23XX_LMC0_RESET_CTL               0x0001180088000180ULL
#define    CN23XX_LMC0_RESET_CTL_DDR3RST_MASK  0x0000000000000001ULL

#endif

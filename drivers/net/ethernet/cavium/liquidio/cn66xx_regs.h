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
/*! \file cn66xx_regs.h
 *  \brief Host Driver: Register Address and Register Mask values for
 *  Octeon CN66XX devices.
 */

#ifndef __CN66XX_REGS_H__
#define __CN66XX_REGS_H__

#define     CN6XXX_XPANSION_BAR             0x30

#define     CN6XXX_MSI_CAP                  0x50
#define     CN6XXX_MSI_ADDR_LO              0x54
#define     CN6XXX_MSI_ADDR_HI              0x58
#define     CN6XXX_MSI_DATA                 0x5C

#define     CN6XXX_PCIE_CAP                 0x70
#define     CN6XXX_PCIE_DEVCAP              0x74
#define     CN6XXX_PCIE_DEVCTL              0x78
#define     CN6XXX_PCIE_LINKCAP             0x7C
#define     CN6XXX_PCIE_LINKCTL             0x80
#define     CN6XXX_PCIE_SLOTCAP             0x84
#define     CN6XXX_PCIE_SLOTCTL             0x88

#define     CN6XXX_PCIE_ENH_CAP             0x100
#define     CN6XXX_PCIE_UNCORR_ERR_STATUS   0x104
#define     CN6XXX_PCIE_UNCORR_ERR_MASK     0x108
#define     CN6XXX_PCIE_UNCORR_ERR          0x10C
#define     CN6XXX_PCIE_CORR_ERR_STATUS     0x110
#define     CN6XXX_PCIE_CORR_ERR_MASK       0x114
#define     CN6XXX_PCIE_ADV_ERR_CAP         0x118

#define     CN6XXX_PCIE_ACK_REPLAY_TIMER    0x700
#define     CN6XXX_PCIE_OTHER_MSG           0x704
#define     CN6XXX_PCIE_PORT_FORCE_LINK     0x708
#define     CN6XXX_PCIE_ACK_FREQ            0x70C
#define     CN6XXX_PCIE_PORT_LINK_CTL       0x710
#define     CN6XXX_PCIE_LANE_SKEW           0x714
#define     CN6XXX_PCIE_SYM_NUM             0x718
#define     CN6XXX_PCIE_FLTMSK              0x720

/* ##############  BAR0 Registers ################  */

#define    CN6XXX_SLI_CTL_PORT0                    0x0050
#define    CN6XXX_SLI_CTL_PORT1                    0x0060

#define    CN6XXX_SLI_WINDOW_CTL                   0x02E0
#define    CN6XXX_SLI_DBG_DATA                     0x0310
#define    CN6XXX_SLI_SCRATCH1                     0x03C0
#define    CN6XXX_SLI_SCRATCH2                     0x03D0
#define    CN6XXX_SLI_CTL_STATUS                   0x0570

#define    CN6XXX_WIN_WR_ADDR_LO                   0x0000
#define    CN6XXX_WIN_WR_ADDR_HI                   0x0004
#define    CN6XXX_WIN_WR_ADDR64                    CN6XXX_WIN_WR_ADDR_LO

#define    CN6XXX_WIN_RD_ADDR_LO                   0x0010
#define    CN6XXX_WIN_RD_ADDR_HI                   0x0014
#define    CN6XXX_WIN_RD_ADDR64                    CN6XXX_WIN_RD_ADDR_LO

#define    CN6XXX_WIN_WR_DATA_LO                   0x0020
#define    CN6XXX_WIN_WR_DATA_HI                   0x0024
#define    CN6XXX_WIN_WR_DATA64                    CN6XXX_WIN_WR_DATA_LO

#define    CN6XXX_WIN_RD_DATA_LO                   0x0040
#define    CN6XXX_WIN_RD_DATA_HI                   0x0044
#define    CN6XXX_WIN_RD_DATA64                    CN6XXX_WIN_RD_DATA_LO

#define    CN6XXX_WIN_WR_MASK_LO                   0x0030
#define    CN6XXX_WIN_WR_MASK_HI                   0x0034
#define    CN6XXX_WIN_WR_MASK_REG                  CN6XXX_WIN_WR_MASK_LO

/* 1 register (32-bit) to enable Input queues */
#define    CN6XXX_SLI_PKT_INSTR_ENB               0x1000

/* 1 register (32-bit) to enable Output queues */
#define    CN6XXX_SLI_PKT_OUT_ENB                 0x1010

/* 1 register (32-bit) to determine whether Output queues are in reset. */
#define    CN6XXX_SLI_PORT_IN_RST_OQ              0x11F0

/* 1 register (32-bit) to determine whether Input queues are in reset. */
#define    CN6XXX_SLI_PORT_IN_RST_IQ              0x11F4

/*###################### REQUEST QUEUE #########################*/

/* 1 register (32-bit) - instr. size of each input queue. */
#define    CN6XXX_SLI_PKT_INSTR_SIZE             0x1020

/* 32 registers for Input Queue Instr Count - SLI_PKT_IN_DONE0_CNTS */
#define    CN6XXX_SLI_IQ_INSTR_COUNT_START       0x2000

/* 32 registers for Input Queue Start Addr - SLI_PKT0_INSTR_BADDR */
#define    CN6XXX_SLI_IQ_BASE_ADDR_START64       0x2800

/* 32 registers for Input Doorbell - SLI_PKT0_INSTR_BAOFF_DBELL */
#define    CN6XXX_SLI_IQ_DOORBELL_START          0x2C00

/* 32 registers for Input Queue size - SLI_PKT0_INSTR_FIFO_RSIZE */
#define    CN6XXX_SLI_IQ_SIZE_START              0x3000

/* 32 registers for Instruction Header Options - SLI_PKT0_INSTR_HEADER */
#define    CN6XXX_SLI_IQ_PKT_INSTR_HDR_START64   0x3400

/* 1 register (64-bit) - Back Pressure for each input queue - SLI_PKT0_IN_BP */
#define    CN66XX_SLI_INPUT_BP_START64           0x3800

/* Each Input Queue register is at a 16-byte Offset in BAR0 */
#define    CN6XXX_IQ_OFFSET                      0x10

/* 1 register (32-bit) - ES, RO, NS, Arbitration for Input Queue Data &
 * gather list fetches. SLI_PKT_INPUT_CONTROL.
 */
#define    CN6XXX_SLI_PKT_INPUT_CONTROL          0x1170

/* 1 register (64-bit) - Number of instructions to read at one time
 * - 2 bits for each input ring. SLI_PKT_INSTR_RD_SIZE.
 */
#define    CN6XXX_SLI_PKT_INSTR_RD_SIZE          0x11A0

/* 1 register (64-bit) - Assign Input ring to MAC port
 * - 2 bits for each input ring. SLI_PKT_IN_PCIE_PORT.
 */
#define    CN6XXX_SLI_IN_PCIE_PORT               0x11B0

/*------- Request Queue Macros ---------*/
#define    CN6XXX_SLI_IQ_BASE_ADDR64(iq)          \
	(CN6XXX_SLI_IQ_BASE_ADDR_START64 + ((iq) * CN6XXX_IQ_OFFSET))

#define    CN6XXX_SLI_IQ_SIZE(iq)                 \
	(CN6XXX_SLI_IQ_SIZE_START + ((iq) * CN6XXX_IQ_OFFSET))

#define    CN6XXX_SLI_IQ_PKT_INSTR_HDR64(iq)      \
	(CN6XXX_SLI_IQ_PKT_INSTR_HDR_START64 + ((iq) * CN6XXX_IQ_OFFSET))

#define    CN6XXX_SLI_IQ_DOORBELL(iq)             \
	(CN6XXX_SLI_IQ_DOORBELL_START + ((iq) * CN6XXX_IQ_OFFSET))

#define    CN6XXX_SLI_IQ_INSTR_COUNT(iq)          \
	(CN6XXX_SLI_IQ_INSTR_COUNT_START + ((iq) * CN6XXX_IQ_OFFSET))

#define    CN66XX_SLI_IQ_BP64(iq)                 \
	(CN66XX_SLI_INPUT_BP_START64 + ((iq) * CN6XXX_IQ_OFFSET))

/*------------------ Masks ----------------*/
#define    CN6XXX_INPUT_CTL_ROUND_ROBIN_ARB         BIT(22)
#define    CN6XXX_INPUT_CTL_DATA_NS                 BIT(8)
#define    CN6XXX_INPUT_CTL_DATA_ES_64B_SWAP        BIT(6)
#define    CN6XXX_INPUT_CTL_DATA_RO                 BIT(5)
#define    CN6XXX_INPUT_CTL_USE_CSR                 BIT(4)
#define    CN6XXX_INPUT_CTL_GATHER_NS               BIT(3)
#define    CN6XXX_INPUT_CTL_GATHER_ES_64B_SWAP      BIT(2)
#define    CN6XXX_INPUT_CTL_GATHER_RO               BIT(1)

#ifdef __BIG_ENDIAN_BITFIELD
#define    CN6XXX_INPUT_CTL_MASK                    \
	(CN6XXX_INPUT_CTL_DATA_ES_64B_SWAP      \
	  | CN6XXX_INPUT_CTL_USE_CSR              \
	  | CN6XXX_INPUT_CTL_GATHER_ES_64B_SWAP)
#else
#define    CN6XXX_INPUT_CTL_MASK                    \
	(CN6XXX_INPUT_CTL_DATA_ES_64B_SWAP     \
	  | CN6XXX_INPUT_CTL_USE_CSR)
#endif

/*############################ OUTPUT QUEUE #########################*/

/* 32 registers for Output queue buffer and info size - SLI_PKT0_OUT_SIZE */
#define    CN6XXX_SLI_OQ0_BUFF_INFO_SIZE         0x0C00

/* 32 registers for Output Queue Start Addr - SLI_PKT0_SLIST_BADDR */
#define    CN6XXX_SLI_OQ_BASE_ADDR_START64       0x1400

/* 32 registers for Output Queue Packet Credits - SLI_PKT0_SLIST_BAOFF_DBELL */
#define    CN6XXX_SLI_OQ_PKT_CREDITS_START       0x1800

/* 32 registers for Output Queue size - SLI_PKT0_SLIST_FIFO_RSIZE */
#define    CN6XXX_SLI_OQ_SIZE_START              0x1C00

/* 32 registers for Output Queue Packet Count - SLI_PKT0_CNTS */
#define    CN6XXX_SLI_OQ_PKT_SENT_START          0x2400

/* Each Output Queue register is at a 16-byte Offset in BAR0 */
#define    CN6XXX_OQ_OFFSET                      0x10

/* 1 register (32-bit) - 1 bit for each output queue
 * - Relaxed Ordering setting for reading Output Queues descriptors
 * - SLI_PKT_SLIST_ROR
 */
#define    CN6XXX_SLI_PKT_SLIST_ROR              0x1030

/* 1 register (32-bit) - 1 bit for each output queue
 * - No Snoop mode for reading Output Queues descriptors
 * - SLI_PKT_SLIST_NS
 */
#define    CN6XXX_SLI_PKT_SLIST_NS               0x1040

/* 1 register (64-bit) - 2 bits for each output queue
 * - Endian-Swap mode for reading Output Queue descriptors
 * - SLI_PKT_SLIST_ES
 */
#define    CN6XXX_SLI_PKT_SLIST_ES64             0x1050

/* 1 register (32-bit) - 1 bit for each output queue
 * - InfoPtr mode for Output Queues.
 * - SLI_PKT_IPTR
 */
#define    CN6XXX_SLI_PKT_IPTR                   0x1070

/* 1 register (32-bit) - 1 bit for each output queue
 * - DPTR format selector for Output queues.
 * - SLI_PKT_DPADDR
 */
#define    CN6XXX_SLI_PKT_DPADDR                 0x1080

/* 1 register (32-bit) - 1 bit for each output queue
 * - Relaxed Ordering setting for reading Output Queues data
 * - SLI_PKT_DATA_OUT_ROR
 */
#define    CN6XXX_SLI_PKT_DATA_OUT_ROR           0x1090

/* 1 register (32-bit) - 1 bit for each output queue
 * - No Snoop mode for reading Output Queues data
 * - SLI_PKT_DATA_OUT_NS
 */
#define    CN6XXX_SLI_PKT_DATA_OUT_NS            0x10A0

/* 1 register (64-bit)  - 2 bits for each output queue
 * - Endian-Swap mode for reading Output Queue data
 * - SLI_PKT_DATA_OUT_ES
 */
#define    CN6XXX_SLI_PKT_DATA_OUT_ES64          0x10B0

/* 1 register (32-bit) - 1 bit for each output queue
 * - Controls whether SLI_PKTn_CNTS is incremented for bytes or for packets.
 * - SLI_PKT_OUT_BMODE
 */
#define    CN6XXX_SLI_PKT_OUT_BMODE              0x10D0

/* 1 register (64-bit) - 2 bits for each output queue
 * - Assign PCIE port for Output queues
 * - SLI_PKT_PCIE_PORT.
 */
#define    CN6XXX_SLI_PKT_PCIE_PORT64            0x10E0

/* 1 (64-bit) register for Output Queue Packet Count Interrupt Threshold
 * & Time Threshold. The same setting applies to all 32 queues.
 * The register is defined as a 64-bit registers, but we use the
 * 32-bit offsets to define distinct addresses.
 */
#define    CN6XXX_SLI_OQ_INT_LEVEL_PKTS          0x1120
#define    CN6XXX_SLI_OQ_INT_LEVEL_TIME          0x1124

/* 1 (64-bit register) for Output Queue backpressure across all rings. */
#define    CN6XXX_SLI_OQ_WMARK                   0x1180

/* 1 register to control output queue global backpressure & ring enable. */
#define    CN6XXX_SLI_PKT_CTL                    0x1220

/*------- Output Queue Macros ---------*/
#define    CN6XXX_SLI_OQ_BASE_ADDR64(oq)          \
	(CN6XXX_SLI_OQ_BASE_ADDR_START64 + ((oq) * CN6XXX_OQ_OFFSET))

#define    CN6XXX_SLI_OQ_SIZE(oq)                 \
	(CN6XXX_SLI_OQ_SIZE_START + ((oq) * CN6XXX_OQ_OFFSET))

#define    CN6XXX_SLI_OQ_BUFF_INFO_SIZE(oq)                 \
	(CN6XXX_SLI_OQ0_BUFF_INFO_SIZE + ((oq) * CN6XXX_OQ_OFFSET))

#define    CN6XXX_SLI_OQ_PKTS_SENT(oq)            \
	(CN6XXX_SLI_OQ_PKT_SENT_START + ((oq) * CN6XXX_OQ_OFFSET))

#define    CN6XXX_SLI_OQ_PKTS_CREDIT(oq)          \
	(CN6XXX_SLI_OQ_PKT_CREDITS_START + ((oq) * CN6XXX_OQ_OFFSET))

/*######################### DMA Counters #########################*/

/* 2 registers (64-bit) - DMA Count - 1 for each DMA counter 0/1. */
#define    CN6XXX_DMA_CNT_START                   0x0400

/* 2 registers (64-bit) - DMA Timer 0/1, contains DMA timer values
 * SLI_DMA_0_TIM
 */
#define    CN6XXX_DMA_TIM_START                   0x0420

/* 2 registers (64-bit) - DMA count & Time Interrupt threshold -
 * SLI_DMA_0_INT_LEVEL
 */
#define    CN6XXX_DMA_INT_LEVEL_START             0x03E0

/* Each DMA register is at a 16-byte Offset in BAR0 */
#define    CN6XXX_DMA_OFFSET                      0x10

/*---------- DMA Counter Macros ---------*/
#define    CN6XXX_DMA_CNT(dq)                      \
	(CN6XXX_DMA_CNT_START + ((dq) * CN6XXX_DMA_OFFSET))

#define    CN6XXX_DMA_INT_LEVEL(dq)                \
	(CN6XXX_DMA_INT_LEVEL_START + ((dq) * CN6XXX_DMA_OFFSET))

#define    CN6XXX_DMA_PKT_INT_LEVEL(dq)            \
	(CN6XXX_DMA_INT_LEVEL_START + ((dq) * CN6XXX_DMA_OFFSET))

#define    CN6XXX_DMA_TIME_INT_LEVEL(dq)           \
	(CN6XXX_DMA_INT_LEVEL_START + 4 + ((dq) * CN6XXX_DMA_OFFSET))

#define    CN6XXX_DMA_TIM(dq)                      \
	(CN6XXX_DMA_TIM_START + ((dq) * CN6XXX_DMA_OFFSET))

/*######################## INTERRUPTS #########################*/

/* 1 register (64-bit) for Interrupt Summary */
#define    CN6XXX_SLI_INT_SUM64                  0x0330

/* 1 register (64-bit) for Interrupt Enable */
#define    CN6XXX_SLI_INT_ENB64_PORT0            0x0340
#define    CN6XXX_SLI_INT_ENB64_PORT1            0x0350

/* 1 register (32-bit) to enable Output Queue Packet/Byte Count Interrupt */
#define    CN6XXX_SLI_PKT_CNT_INT_ENB            0x1150

/* 1 register (32-bit) to enable Output Queue Packet Timer Interrupt */
#define    CN6XXX_SLI_PKT_TIME_INT_ENB           0x1160

/* 1 register (32-bit) to indicate which Output Queue reached pkt threshold */
#define    CN6XXX_SLI_PKT_CNT_INT                0x1130

/* 1 register (32-bit) to indicate which Output Queue reached time threshold */
#define    CN6XXX_SLI_PKT_TIME_INT               0x1140

/*------------------ Interrupt Masks ----------------*/

#define    CN6XXX_INTR_RML_TIMEOUT_ERR           BIT(1)
#define    CN6XXX_INTR_BAR0_RW_TIMEOUT_ERR       BIT(2)
#define    CN6XXX_INTR_IO2BIG_ERR                BIT(3)
#define    CN6XXX_INTR_PKT_COUNT                 BIT(4)
#define    CN6XXX_INTR_PKT_TIME                  BIT(5)
#define    CN6XXX_INTR_M0UPB0_ERR                BIT(8)
#define    CN6XXX_INTR_M0UPWI_ERR                BIT(9)
#define    CN6XXX_INTR_M0UNB0_ERR                BIT(10)
#define    CN6XXX_INTR_M0UNWI_ERR                BIT(11)
#define    CN6XXX_INTR_M1UPB0_ERR                BIT(12)
#define    CN6XXX_INTR_M1UPWI_ERR                BIT(13)
#define    CN6XXX_INTR_M1UNB0_ERR                BIT(14)
#define    CN6XXX_INTR_M1UNWI_ERR                BIT(15)
#define    CN6XXX_INTR_MIO_INT0                  BIT(16)
#define    CN6XXX_INTR_MIO_INT1                  BIT(17)
#define    CN6XXX_INTR_MAC_INT0                  BIT(18)
#define    CN6XXX_INTR_MAC_INT1                  BIT(19)

#define    CN6XXX_INTR_DMA0_FORCE                BIT_ULL(32)
#define    CN6XXX_INTR_DMA1_FORCE                BIT_ULL(33)
#define    CN6XXX_INTR_DMA0_COUNT                BIT_ULL(34)
#define    CN6XXX_INTR_DMA1_COUNT                BIT_ULL(35)
#define    CN6XXX_INTR_DMA0_TIME                 BIT_ULL(36)
#define    CN6XXX_INTR_DMA1_TIME                 BIT_ULL(37)
#define    CN6XXX_INTR_INSTR_DB_OF_ERR           BIT_ULL(48)
#define    CN6XXX_INTR_SLIST_DB_OF_ERR           BIT_ULL(49)
#define    CN6XXX_INTR_POUT_ERR                  BIT_ULL(50)
#define    CN6XXX_INTR_PIN_BP_ERR                BIT_ULL(51)
#define    CN6XXX_INTR_PGL_ERR                   BIT_ULL(52)
#define    CN6XXX_INTR_PDI_ERR                   BIT_ULL(53)
#define    CN6XXX_INTR_POP_ERR                   BIT_ULL(54)
#define    CN6XXX_INTR_PINS_ERR                  BIT_ULL(55)
#define    CN6XXX_INTR_SPRT0_ERR                 BIT_ULL(56)
#define    CN6XXX_INTR_SPRT1_ERR                 BIT_ULL(57)
#define    CN6XXX_INTR_ILL_PAD_ERR               BIT_ULL(60)

#define    CN6XXX_INTR_DMA0_DATA                 (CN6XXX_INTR_DMA0_TIME)

#define    CN6XXX_INTR_DMA1_DATA                 (CN6XXX_INTR_DMA1_TIME)

#define    CN6XXX_INTR_DMA_DATA                  \
	(CN6XXX_INTR_DMA0_DATA | CN6XXX_INTR_DMA1_DATA)

#define    CN6XXX_INTR_PKT_DATA                  (CN6XXX_INTR_PKT_TIME | \
						  CN6XXX_INTR_PKT_COUNT)

/* Sum of interrupts for all PCI-Express Data Interrupts */
#define    CN6XXX_INTR_PCIE_DATA                 \
	(CN6XXX_INTR_DMA_DATA | CN6XXX_INTR_PKT_DATA)

#define    CN6XXX_INTR_MIO                       \
	(CN6XXX_INTR_MIO_INT0 | CN6XXX_INTR_MIO_INT1)

#define    CN6XXX_INTR_MAC                       \
	(CN6XXX_INTR_MAC_INT0 | CN6XXX_INTR_MAC_INT1)

/* Sum of interrupts for error events */
#define    CN6XXX_INTR_ERR                       \
	(CN6XXX_INTR_BAR0_RW_TIMEOUT_ERR    \
	   | CN6XXX_INTR_IO2BIG_ERR             \
	   | CN6XXX_INTR_M0UPB0_ERR             \
	   | CN6XXX_INTR_M0UPWI_ERR             \
	   | CN6XXX_INTR_M0UNB0_ERR             \
	   | CN6XXX_INTR_M0UNWI_ERR             \
	   | CN6XXX_INTR_M1UPB0_ERR             \
	   | CN6XXX_INTR_M1UPWI_ERR             \
	   | CN6XXX_INTR_M1UPB0_ERR             \
	   | CN6XXX_INTR_M1UNWI_ERR             \
	   | CN6XXX_INTR_INSTR_DB_OF_ERR        \
	   | CN6XXX_INTR_SLIST_DB_OF_ERR        \
	   | CN6XXX_INTR_POUT_ERR               \
	   | CN6XXX_INTR_PIN_BP_ERR             \
	   | CN6XXX_INTR_PGL_ERR                \
	   | CN6XXX_INTR_PDI_ERR                \
	   | CN6XXX_INTR_POP_ERR                \
	   | CN6XXX_INTR_PINS_ERR               \
	   | CN6XXX_INTR_SPRT0_ERR              \
	   | CN6XXX_INTR_SPRT1_ERR              \
	   | CN6XXX_INTR_ILL_PAD_ERR)

/* Programmed Mask for Interrupt Sum */
#define    CN6XXX_INTR_MASK                      \
	(CN6XXX_INTR_PCIE_DATA              \
	   | CN6XXX_INTR_DMA0_FORCE             \
	   | CN6XXX_INTR_DMA1_FORCE             \
	   | CN6XXX_INTR_MIO                    \
	   | CN6XXX_INTR_MAC                    \
	   | CN6XXX_INTR_ERR)

#define    CN6XXX_SLI_S2M_PORT0_CTL              0x3D80
#define    CN6XXX_SLI_S2M_PORT1_CTL              0x3D90
#define    CN6XXX_SLI_S2M_PORTX_CTL(port)        \
	(CN6XXX_SLI_S2M_PORT0_CTL + ((port) * 0x10))

#define    CN6XXX_SLI_INT_ENB64(port)            \
	(CN6XXX_SLI_INT_ENB64_PORT0 + ((port) * 0x10))

#define    CN6XXX_SLI_MAC_NUMBER                 0x3E00

/* CN6XXX BAR1 Index registers. */
#define    CN6XXX_PEM_BAR1_INDEX000                0x00011800C00000A8ULL
#define    CN6XXX_PEM_OFFSET                       0x0000000001000000ULL

#define    CN6XXX_BAR1_INDEX_START                 CN6XXX_PEM_BAR1_INDEX000
#define    CN6XXX_PCI_BAR1_OFFSET                  0x8

#define    CN6XXX_BAR1_REG(idx, port) \
		(CN6XXX_BAR1_INDEX_START + ((port) * CN6XXX_PEM_OFFSET) + \
		(CN6XXX_PCI_BAR1_OFFSET * (idx)))

/*############################ DPI #########################*/

#define    CN6XXX_DPI_CTL                 0x0001df0000000040ULL

#define    CN6XXX_DPI_DMA_CONTROL         0x0001df0000000048ULL

#define    CN6XXX_DPI_REQ_GBL_ENB         0x0001df0000000050ULL

#define    CN6XXX_DPI_REQ_ERR_RSP         0x0001df0000000058ULL

#define    CN6XXX_DPI_REQ_ERR_RST         0x0001df0000000060ULL

#define    CN6XXX_DPI_DMA_ENG0_ENB        0x0001df0000000080ULL

#define    CN6XXX_DPI_DMA_ENG_ENB(q_no)   \
	(CN6XXX_DPI_DMA_ENG0_ENB + ((q_no) * 8))

#define    CN6XXX_DPI_DMA_ENG0_BUF        0x0001df0000000880ULL

#define    CN6XXX_DPI_DMA_ENG_BUF(q_no)   \
	(CN6XXX_DPI_DMA_ENG0_BUF + ((q_no) * 8))

#define    CN6XXX_DPI_SLI_PRT0_CFG        0x0001df0000000900ULL
#define    CN6XXX_DPI_SLI_PRT1_CFG        0x0001df0000000908ULL
#define    CN6XXX_DPI_SLI_PRTX_CFG(port)        \
	(CN6XXX_DPI_SLI_PRT0_CFG + ((port) * 0x10))

#define    CN6XXX_DPI_DMA_COMMIT_MODE     BIT_ULL(58)
#define    CN6XXX_DPI_DMA_PKT_HP          BIT_ULL(57)
#define    CN6XXX_DPI_DMA_PKT_EN          BIT_ULL(56)
#define    CN6XXX_DPI_DMA_O_ES            BIT_ULL(15)
#define    CN6XXX_DPI_DMA_O_MODE          BIT_ULL(14)

#define    CN6XXX_DPI_DMA_CTL_MASK             \
	(CN6XXX_DPI_DMA_COMMIT_MODE    |    \
	 CN6XXX_DPI_DMA_PKT_HP         |    \
	 CN6XXX_DPI_DMA_PKT_EN         |    \
	 CN6XXX_DPI_DMA_O_ES           |    \
	 CN6XXX_DPI_DMA_O_MODE)

/*############################ CIU #########################*/

#define    CN6XXX_CIU_SOFT_BIST           0x0001070000000738ULL
#define    CN6XXX_CIU_SOFT_RST            0x0001070000000740ULL

/*############################ MIO #########################*/
#define    CN6XXX_MIO_PTP_CLOCK_CFG       0x0001070000000f00ULL
#define    CN6XXX_MIO_PTP_CLOCK_LO        0x0001070000000f08ULL
#define    CN6XXX_MIO_PTP_CLOCK_HI        0x0001070000000f10ULL
#define    CN6XXX_MIO_PTP_CLOCK_COMP      0x0001070000000f18ULL
#define    CN6XXX_MIO_PTP_TIMESTAMP       0x0001070000000f20ULL
#define    CN6XXX_MIO_PTP_EVT_CNT         0x0001070000000f28ULL
#define    CN6XXX_MIO_PTP_CKOUT_THRESH_LO 0x0001070000000f30ULL
#define    CN6XXX_MIO_PTP_CKOUT_THRESH_HI 0x0001070000000f38ULL
#define    CN6XXX_MIO_PTP_CKOUT_HI_INCR   0x0001070000000f40ULL
#define    CN6XXX_MIO_PTP_CKOUT_LO_INCR   0x0001070000000f48ULL
#define    CN6XXX_MIO_PTP_PPS_THRESH_LO   0x0001070000000f50ULL
#define    CN6XXX_MIO_PTP_PPS_THRESH_HI   0x0001070000000f58ULL
#define    CN6XXX_MIO_PTP_PPS_HI_INCR     0x0001070000000f60ULL
#define    CN6XXX_MIO_PTP_PPS_LO_INCR     0x0001070000000f68ULL

#define    CN6XXX_MIO_QLM4_CFG            0x00011800000015B0ULL
#define    CN6XXX_MIO_RST_BOOT            0x0001180000001600ULL

#define    CN6XXX_MIO_QLM_CFG_MASK        0x7

/*############################ LMC #########################*/

#define    CN6XXX_LMC0_RESET_CTL               0x0001180088000180ULL
#define    CN6XXX_LMC0_RESET_CTL_DDR3RST_MASK  0x0000000000000001ULL

#endif

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PKUnity Multi-Media Card and Security Digital Card (MMC/SD) Registers
 */
/*
 * Clock Control Reg SDC_CCR
 */
#define SDC_CCR		(PKUNITY_SDC_BASE + 0x0000)
/*
 * Software Reset Reg SDC_SRR
 */
#define SDC_SRR		(PKUNITY_SDC_BASE + 0x0004)
/*
 * Argument Reg SDC_ARGUMENT
 */
#define SDC_ARGUMENT	(PKUNITY_SDC_BASE + 0x0008)
/*
 * Command Reg SDC_COMMAND
 */
#define SDC_COMMAND	(PKUNITY_SDC_BASE + 0x000C)
/*
 * Block Size Reg SDC_BLOCKSIZE
 */
#define SDC_BLOCKSIZE	(PKUNITY_SDC_BASE + 0x0010)
/*
 * Block Cound Reg SDC_BLOCKCOUNT
 */
#define SDC_BLOCKCOUNT	(PKUNITY_SDC_BASE + 0x0014)
/*
 * Transfer Mode Reg SDC_TMR
 */
#define SDC_TMR		(PKUNITY_SDC_BASE + 0x0018)
/*
 * Response Reg. 0 SDC_RES0
 */
#define SDC_RES0	(PKUNITY_SDC_BASE + 0x001C)
/*
 * Response Reg. 1 SDC_RES1
 */
#define SDC_RES1	(PKUNITY_SDC_BASE + 0x0020)
/*
 * Response Reg. 2 SDC_RES2
 */
#define SDC_RES2	(PKUNITY_SDC_BASE + 0x0024)
/*
 * Response Reg. 3 SDC_RES3
 */
#define SDC_RES3	(PKUNITY_SDC_BASE + 0x0028)
/*
 * Read Timeout Control Reg SDC_RTCR
 */
#define SDC_RTCR	(PKUNITY_SDC_BASE + 0x002C)
/*
 * Interrupt Status Reg SDC_ISR
 */
#define SDC_ISR		(PKUNITY_SDC_BASE + 0x0030)
/*
 * Interrupt Status Mask Reg SDC_ISMR
 */
#define SDC_ISMR	(PKUNITY_SDC_BASE + 0x0034)
/*
 * RX FIFO SDC_RXFIFO
 */
#define SDC_RXFIFO	(PKUNITY_SDC_BASE + 0x0038)
/*
 * TX FIFO SDC_TXFIFO
 */
#define SDC_TXFIFO	(PKUNITY_SDC_BASE + 0x003C)

/*
 * SD Clock Enable SDC_CCR_CLKEN
 */
#define SDC_CCR_CLKEN			FIELD(1, 1, 2)
/*
 * [15:8] SDC_CCR_PDIV(v)
 */
#define SDC_CCR_PDIV(v)			FIELD((v), 8, 8)

/*
 * Software reset enable SDC_SRR_ENABLE
 */
#define SDC_SRR_ENABLE			FIELD(0, 1, 0)
/*
 * Software reset disable SDC_SRR_DISABLE
 */
#define SDC_SRR_DISABLE			FIELD(1, 1, 0)

/*
 * Response type SDC_COMMAND_RESTYPE_MASK
 */
#define SDC_COMMAND_RESTYPE_MASK	FMASK(2, 0)
/*
 * No response SDC_COMMAND_RESTYPE_NONE
 */
#define SDC_COMMAND_RESTYPE_NONE	FIELD(0, 2, 0)
/*
 * 136-bit long response SDC_COMMAND_RESTYPE_LONG
 */
#define SDC_COMMAND_RESTYPE_LONG	FIELD(1, 2, 0)
/*
 * 48-bit short response SDC_COMMAND_RESTYPE_SHORT
 */
#define SDC_COMMAND_RESTYPE_SHORT	FIELD(2, 2, 0)
/*
 * 48-bit short and test if busy response SDC_COMMAND_RESTYPE_SHORTBUSY
 */
#define SDC_COMMAND_RESTYPE_SHORTBUSY	FIELD(3, 2, 0)
/*
 * data ready SDC_COMMAND_DATAREADY
 */
#define SDC_COMMAND_DATAREADY		FIELD(1, 1, 2)
#define SDC_COMMAND_CMDEN		FIELD(1, 1, 3)
/*
 * [10:5] SDC_COMMAND_CMDINDEX(v)
 */
#define SDC_COMMAND_CMDINDEX(v)		FIELD((v), 6, 5)

/*
 * [10:0] SDC_BLOCKSIZE_BSMASK(v)
 */
#define SDC_BLOCKSIZE_BSMASK(v)		FIELD((v), 11, 0)
/*
 * [11:0] SDC_BLOCKCOUNT_BCMASK(v)
 */
#define SDC_BLOCKCOUNT_BCMASK(v)	FIELD((v), 12, 0)

/*
 * Data Width 1bit SDC_TMR_WTH_1BIT
 */
#define SDC_TMR_WTH_1BIT		FIELD(0, 1, 0)
/*
 * Data Width 4bit SDC_TMR_WTH_4BIT
 */
#define SDC_TMR_WTH_4BIT		FIELD(1, 1, 0)
/*
 * Read SDC_TMR_DIR_READ
 */
#define SDC_TMR_DIR_READ		FIELD(0, 1, 1)
/*
 * Write SDC_TMR_DIR_WRITE
 */
#define SDC_TMR_DIR_WRITE		FIELD(1, 1, 1)

#define SDC_IR_MASK			FMASK(13, 0)
#define SDC_IR_RESTIMEOUT		FIELD(1, 1, 0)
#define SDC_IR_WRITECRC			FIELD(1, 1, 1)
#define SDC_IR_READCRC			FIELD(1, 1, 2)
#define SDC_IR_TXFIFOREAD		FIELD(1, 1, 3)
#define SDC_IR_RXFIFOWRITE		FIELD(1, 1, 4)
#define SDC_IR_READTIMEOUT		FIELD(1, 1, 5)
#define SDC_IR_DATACOMPLETE		FIELD(1, 1, 6)
#define SDC_IR_CMDCOMPLETE		FIELD(1, 1, 7)
#define SDC_IR_RXFIFOFULL		FIELD(1, 1, 8)
#define SDC_IR_RXFIFOEMPTY		FIELD(1, 1, 9)
#define SDC_IR_TXFIFOFULL		FIELD(1, 1, 10)
#define SDC_IR_TXFIFOEMPTY		FIELD(1, 1, 11)
#define SDC_IR_ENDCMDWITHRES		FIELD(1, 1, 12)

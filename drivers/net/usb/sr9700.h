/*
 *  Copyright (c) 2009 jokeliu@163.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * Author : jokeliujl <jokeliu@163.com>
 * Date : 2010-10-01
 */

/* sr9700 spec. register table on android platform */
/* Registers */
#define	NCR			0x00
#define	NSR			0x01
#define	TCR			0x02
#define	TSR1		0x03
#define	TSR2		0x04
#define	RCR			0x05
#define	RSR			0x06
#define	ROCR		0x07
#define	BPTR		0x08
#define	FCTR		0x09
#define	FCR			0x0A
#define	EPCR		0x0B
#define	EPAR		0x0C
#define	EPDR		0x0D	// 0x0D ~ 0x0E
#define	WCR			0x0F
#define	PAR			0x10
#define	MAR			0x16
#define	PRR			0x1F
#define	TWPAL		0x20
#define	TWPAH		0x21
#define	TRPAL		0x22
#define	TRPAH		0x23
#define	RWPAL		0x24
#define	RWPAH		0x25
#define	RRPAL		0x26
#define	RRPAH		0x27
#define	VID			0x28
#define	PID			0x2A
#define	CHIPR		0x2C
#define	USBDA		0xF0
#define	RXC			0xF1
#define	TXC_USBS	0xF2
#define	USBC		0xF4

/* Bit definition for registers */
// Network Control Reg
#define	NCR_RST			(1 << 0)
#define	NCR_LBK			(3 << 1)
#define	NCR_FDX			(1 << 3)
#define	NCR_WAKEEN		(1 << 6)
// Network Status Reg
#define	NSR_RXRDY	(1 << 0)
#define	NSR_RXOV	(1 << 1)
#define	NSR_TX1END	(1 << 2)
#define	NSR_TX2END	(1 << 3)
#define	NSR_TXFULL	(1 << 4)
#define	NSR_WAKEST	(1 << 5)
#define	NSR_LINKST	(1 << 6)
#define	NSR_SPEED	(1 << 7)
// Tx Control Reg
#define	TCR_CRC_DIS		(1 << 1)
#define	TCR_PAD_DIS		(1 << 2)
#define	TCR_LC_CARE		(1 << 3)
#define	TCR_CRS_CARE	(1 << 4)
#define	TCR_EXCECM		(1 << 5)
#define	TCR_LF_EN		(1 << 6)
// Tx Status Reg for Packet 1
#define	TSR1_EC		(1 << 2)
#define	TSR1_COL	(1 << 3)
#define	TSR1_LC		(1 << 4)
#define	TSR1_NC		(1 << 5)
#define	TSR1_LOC		(1 << 6)
#define	TSR1_TLF	(1 << 7)
// Tx Status Reg for Packet 2
#define	TSR2_EC		(1 << 2)
#define	TSR2_COL	(1 << 3)
#define	TSR2_LC		(1 << 4)
#define	TSR2_NC		(1 << 5)
#define	TSR2_LOC		(1 << 6)
#define	TSR2_TLF	(1 << 7)
// Rx Control Reg
#define	RCR_RXEN		(1 << 0)
#define	RCR_PRMSC		(1 << 1)
#define	RCR_RUNT		(1 << 2)
#define	RCR_ALL			(1 << 3)
#define	RCR_DIS_CRC		(1 << 4)
#define	RCR_DIS_LONG	(1 << 5)
// Rx Status Reg
#define	RSR_AE		(1 << 2)
#define	RSR_MF		(1 << 6)
#define	RSR_RF		(1 << 7)
// Recv Overflow Counter Reg
#define	ROCR_ROC		(0x7F << 0)
#define	ROCR_RXFU		(1 << 7)
// Back Pressure Threshold Reg
#define	BPTR_JPT	(0x0F << 0)
#define	BPTR_BPHW	(0x0F << 4)
// Flow Control Threshold Reg
#define	FCTR_LWOT		(0x0F << 0)
#define	FCTR_HWOT		(0x0F << 4)
// rx/tx Flow Control Reg
#define	FCR_FLCE	(1 << 0)
#define	FCR_BKPA	(1 << 4)
#define	FCR_TXPEN	(1 << 5)
#define	FCR_TXPF	(1 << 6)
#define	FCR_TXP0	(1 << 7)
// EEPROM & PHY Control Reg
#define	EPCR_ERRE		(1 << 0)
#define	EPCR_ERPRW		(1 << 1)
#define	EPCR_ERPRR		(1 << 2)
#define	EPCR_EPOS		(1 << 3)
#define	EPCR_WEP		(1 << 4)
// EEPROM & PHY Address Reg
#define	EPAR_EROA		(0x3F << 0)
#define	EPAR_PHY_ADR	(0x03 << 6)
// Wakeup Control Reg
#define	WCR_MAGICST		(1 << 0)
#define	WCR_LINKST		(1 << 2)
#define	WCR_MAGICEN		(1 << 3)
#define	WCR_LINKEN		(1 << 5)
// Phy Reset Reg
#define	PRR_PHY_RST		(1 << 0)
// USB Device Address Reg
#define	USBDA_USBFA	(0x7F << 0)
// TX packet Counter & USB Status Reg
#define	TXC_USBS_TXC0	(1 << 0)
#define	TXC_USBS_TXC1	(1 << 1)
#define	TXC_USBS_TXC2	(1 << 2)
#define	TXC_USBS_EP1RDY	(1 << 5)
#define	TXC_USBS_SUSFLAG	(1 << 6)
#define	TXC_USBS_RXFAULT	(1 << 7)
// USB Control Reg
#define	USBC_EP3NAK	(1 << 4)
#define	USBC_EP3ACK	(1 << 5)

/* Variables */
#define	QF_RD_REGS		0x00
#define	QF_WR_REGS		0x01
#define	QF_WR_REG		0x03
#define	QF_REQ_RD_REG	(USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE)
#define	QF_REQ_WR_REG	(USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE)

#define	QF_SHARE_TIMEOUT	1000
#define	QF_EEPROM_LEN		256
#define	QF_MCAST_SIZE		8
#define	QF_MCAST_MAX		64
#define	QF_TX_OVERHEAD		2	// 2bytes header
#define	QF_RX_OVERHEAD		7	// 3bytes header + 4crc tail

/*----------------------------------------------------------------------------------------------*/

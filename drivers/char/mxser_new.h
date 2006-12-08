#ifndef _MXSER_H
#define _MXSER_H

/*
 *	Semi-public control interfaces
 */

/*
 *	MOXA ioctls
 */

#define MOXA			0x400
#define MOXA_GETDATACOUNT	(MOXA + 23)
#define	MOXA_GET_CONF		(MOXA + 35)
#define MOXA_DIAGNOSE		(MOXA + 50)
#define MOXA_CHKPORTENABLE	(MOXA + 60)
#define MOXA_HighSpeedOn	(MOXA + 61)
#define MOXA_GET_MAJOR		(MOXA + 63)
#define MOXA_GET_CUMAJOR	(MOXA + 64)
#define MOXA_GETMSTATUS		(MOXA + 65)
#define MOXA_SET_OP_MODE	(MOXA + 66)
#define MOXA_GET_OP_MODE	(MOXA + 67)

#define RS232_MODE		0
#define RS485_2WIRE_MODE	1
#define RS422_MODE		2
#define RS485_4WIRE_MODE	3
#define OP_MODE_MASK		3
// above add by Victor Yu. 01-05-2004

#define TTY_THRESHOLD_THROTTLE  128

#define LO_WATER	 	(TTY_FLIPBUF_SIZE)
#define HI_WATER		(TTY_FLIPBUF_SIZE*2*3/4)

// added by James. 03-11-2004.
#define MOXA_SDS_GETICOUNTER  	(MOXA + 68)
#define MOXA_SDS_RSTICOUNTER  	(MOXA + 69)
// (above) added by James.

#define MOXA_ASPP_OQUEUE  	(MOXA + 70)
#define MOXA_ASPP_SETBAUD 	(MOXA + 71)
#define MOXA_ASPP_GETBAUD 	(MOXA + 72)
#define MOXA_ASPP_MON     	(MOXA + 73)
#define MOXA_ASPP_LSTATUS 	(MOXA + 74)
#define MOXA_ASPP_MON_EXT 	(MOXA + 75)
#define MOXA_SET_BAUD_METHOD	(MOXA + 76)


/* --------------------------------------------------- */

#define NPPI_NOTIFY_PARITY	0x01
#define NPPI_NOTIFY_FRAMING	0x02
#define NPPI_NOTIFY_HW_OVERRUN	0x04
#define NPPI_NOTIFY_SW_OVERRUN	0x08
#define NPPI_NOTIFY_BREAK	0x10

#define NPPI_NOTIFY_CTSHOLD         0x01	// Tx hold by CTS low
#define NPPI_NOTIFY_DSRHOLD         0x02	// Tx hold by DSR low
#define NPPI_NOTIFY_XOFFHOLD        0x08	// Tx hold by Xoff received
#define NPPI_NOTIFY_XOFFXENT        0x10	// Xoff Sent

//CheckIsMoxaMust return value
#define MOXA_OTHER_UART			0x00
#define MOXA_MUST_MU150_HWID		0x01
#define MOXA_MUST_MU860_HWID		0x02

// follow just for Moxa Must chip define.
//
// when LCR register (offset 0x03) write following value,
// the Must chip will enter enchance mode. And write value
// on EFR (offset 0x02) bit 6,7 to change bank.
#define MOXA_MUST_ENTER_ENCHANCE	0xBF

// when enhance mode enable, access on general bank register
#define MOXA_MUST_GDL_REGISTER		0x07
#define MOXA_MUST_GDL_MASK		0x7F
#define MOXA_MUST_GDL_HAS_BAD_DATA	0x80

#define MOXA_MUST_LSR_RERR		0x80	// error in receive FIFO
// enchance register bank select and enchance mode setting register
// when LCR register equal to 0xBF
#define MOXA_MUST_EFR_REGISTER		0x02
// enchance mode enable
#define MOXA_MUST_EFR_EFRB_ENABLE	0x10
// enchance reister bank set 0, 1, 2
#define MOXA_MUST_EFR_BANK0		0x00
#define MOXA_MUST_EFR_BANK1		0x40
#define MOXA_MUST_EFR_BANK2		0x80
#define MOXA_MUST_EFR_BANK3		0xC0
#define MOXA_MUST_EFR_BANK_MASK		0xC0

// set XON1 value register, when LCR=0xBF and change to bank0
#define MOXA_MUST_XON1_REGISTER		0x04

// set XON2 value register, when LCR=0xBF and change to bank0
#define MOXA_MUST_XON2_REGISTER		0x05

// set XOFF1 value register, when LCR=0xBF and change to bank0
#define MOXA_MUST_XOFF1_REGISTER	0x06

// set XOFF2 value register, when LCR=0xBF and change to bank0
#define MOXA_MUST_XOFF2_REGISTER	0x07

#define MOXA_MUST_RBRTL_REGISTER	0x04
#define MOXA_MUST_RBRTH_REGISTER	0x05
#define MOXA_MUST_RBRTI_REGISTER	0x06
#define MOXA_MUST_THRTL_REGISTER	0x07
#define MOXA_MUST_ENUM_REGISTER		0x04
#define MOXA_MUST_HWID_REGISTER		0x05
#define MOXA_MUST_ECR_REGISTER		0x06
#define MOXA_MUST_CSR_REGISTER		0x07

// good data mode enable
#define MOXA_MUST_FCR_GDA_MODE_ENABLE	0x20
// only good data put into RxFIFO
#define MOXA_MUST_FCR_GDA_ONLY_ENABLE	0x10

// enable CTS interrupt
#define MOXA_MUST_IER_ECTSI		0x80
// enable RTS interrupt
#define MOXA_MUST_IER_ERTSI		0x40
// enable Xon/Xoff interrupt
#define MOXA_MUST_IER_XINT		0x20
// enable GDA interrupt
#define MOXA_MUST_IER_EGDAI		0x10

#define MOXA_MUST_RECV_ISR		(UART_IER_RDI | MOXA_MUST_IER_EGDAI)

// GDA interrupt pending
#define MOXA_MUST_IIR_GDA		0x1C
#define MOXA_MUST_IIR_RDA		0x04
#define MOXA_MUST_IIR_RTO		0x0C
#define MOXA_MUST_IIR_LSR		0x06

// recieved Xon/Xoff or specical interrupt pending
#define MOXA_MUST_IIR_XSC		0x10

// RTS/CTS change state interrupt pending
#define MOXA_MUST_IIR_RTSCTS		0x20
#define MOXA_MUST_IIR_MASK		0x3E

#define MOXA_MUST_MCR_XON_FLAG		0x40
#define MOXA_MUST_MCR_XON_ANY		0x80
#define MOXA_MUST_MCR_TX_XON		0x08


// software flow control on chip mask value
#define MOXA_MUST_EFR_SF_MASK		0x0F
// send Xon1/Xoff1
#define MOXA_MUST_EFR_SF_TX1		0x08
// send Xon2/Xoff2
#define MOXA_MUST_EFR_SF_TX2		0x04
// send Xon1,Xon2/Xoff1,Xoff2
#define MOXA_MUST_EFR_SF_TX12		0x0C
// don't send Xon/Xoff
#define MOXA_MUST_EFR_SF_TX_NO		0x00
// Tx software flow control mask
#define MOXA_MUST_EFR_SF_TX_MASK	0x0C
// don't receive Xon/Xoff
#define MOXA_MUST_EFR_SF_RX_NO		0x00
// receive Xon1/Xoff1
#define MOXA_MUST_EFR_SF_RX1		0x02
// receive Xon2/Xoff2
#define MOXA_MUST_EFR_SF_RX2		0x01
// receive Xon1,Xon2/Xoff1,Xoff2
#define MOXA_MUST_EFR_SF_RX12		0x03
// Rx software flow control mask
#define MOXA_MUST_EFR_SF_RX_MASK	0x03

//#define MOXA_MUST_MIN_XOFFLIMIT               66
//#define MOXA_MUST_MIN_XONLIMIT                20
//#define ID1_RX_TRIG                   120


#define CHECK_MOXA_MUST_XOFFLIMIT(info) { 	\
	if ( (info)->IsMoxaMustChipFlag && 	\
	 (info)->HandFlow.XoffLimit < MOXA_MUST_MIN_XOFFLIMIT ) {	\
		(info)->HandFlow.XoffLimit = MOXA_MUST_MIN_XOFFLIMIT;	\
		(info)->HandFlow.XonLimit = MOXA_MUST_MIN_XONLIMIT;	\
	}	\
}

#define ENABLE_MOXA_MUST_ENCHANCE_MODE(baseio) { \
	u8	__oldlcr, __efr;	\
	__oldlcr = inb((baseio)+UART_LCR);	\
	outb(MOXA_MUST_ENTER_ENCHANCE, (baseio)+UART_LCR);	\
	__efr = inb((baseio)+MOXA_MUST_EFR_REGISTER);	\
	__efr |= MOXA_MUST_EFR_EFRB_ENABLE;	\
	outb(__efr, (baseio)+MOXA_MUST_EFR_REGISTER);	\
	outb(__oldlcr, (baseio)+UART_LCR);	\
}

#define DISABLE_MOXA_MUST_ENCHANCE_MODE(baseio) {	\
	u8	__oldlcr, __efr;	\
	__oldlcr = inb((baseio)+UART_LCR);	\
	outb(MOXA_MUST_ENTER_ENCHANCE, (baseio)+UART_LCR);	\
	__efr = inb((baseio)+MOXA_MUST_EFR_REGISTER);	\
	__efr &= ~MOXA_MUST_EFR_EFRB_ENABLE;	\
	outb(__efr, (baseio)+MOXA_MUST_EFR_REGISTER);	\
	outb(__oldlcr, (baseio)+UART_LCR);	\
}

#define SET_MOXA_MUST_XON1_VALUE(baseio, Value) {	\
	u8	__oldlcr, __efr;	\
	__oldlcr = inb((baseio)+UART_LCR);	\
	outb(MOXA_MUST_ENTER_ENCHANCE, (baseio)+UART_LCR);	\
	__efr = inb((baseio)+MOXA_MUST_EFR_REGISTER);	\
	__efr &= ~MOXA_MUST_EFR_BANK_MASK;	\
	__efr |= MOXA_MUST_EFR_BANK0;	\
	outb(__efr, (baseio)+MOXA_MUST_EFR_REGISTER);	\
	outb((u8)(Value), (baseio)+MOXA_MUST_XON1_REGISTER);	\
	outb(__oldlcr, (baseio)+UART_LCR);	\
}

#define SET_MOXA_MUST_XON2_VALUE(baseio, Value) {	\
	u8	__oldlcr, __efr;	\
	__oldlcr = inb((baseio)+UART_LCR);	\
	outb(MOXA_MUST_ENTER_ENCHANCE, (baseio)+UART_LCR);	\
	__efr = inb((baseio)+MOXA_MUST_EFR_REGISTER);	\
	__efr &= ~MOXA_MUST_EFR_BANK_MASK;	\
	__efr |= MOXA_MUST_EFR_BANK0;	\
	outb(__efr, (baseio)+MOXA_MUST_EFR_REGISTER);	\
	outb((u8)(Value), (baseio)+MOXA_MUST_XON2_REGISTER);	\
	outb(__oldlcr, (baseio)+UART_LCR);	\
}

#define SET_MOXA_MUST_XOFF1_VALUE(baseio, Value) {	\
	u8	__oldlcr, __efr;	\
	__oldlcr = inb((baseio)+UART_LCR);	\
	outb(MOXA_MUST_ENTER_ENCHANCE, (baseio)+UART_LCR);	\
	__efr = inb((baseio)+MOXA_MUST_EFR_REGISTER);	\
	__efr &= ~MOXA_MUST_EFR_BANK_MASK;	\
	__efr |= MOXA_MUST_EFR_BANK0;	\
	outb(__efr, (baseio)+MOXA_MUST_EFR_REGISTER);	\
	outb((u8)(Value), (baseio)+MOXA_MUST_XOFF1_REGISTER);	\
	outb(__oldlcr, (baseio)+UART_LCR);	\
}

#define SET_MOXA_MUST_XOFF2_VALUE(baseio, Value) {	\
	u8	__oldlcr, __efr;	\
	__oldlcr = inb((baseio)+UART_LCR);	\
	outb(MOXA_MUST_ENTER_ENCHANCE, (baseio)+UART_LCR);	\
	__efr = inb((baseio)+MOXA_MUST_EFR_REGISTER);	\
	__efr &= ~MOXA_MUST_EFR_BANK_MASK;	\
	__efr |= MOXA_MUST_EFR_BANK0;	\
	outb(__efr, (baseio)+MOXA_MUST_EFR_REGISTER);	\
	outb((u8)(Value), (baseio)+MOXA_MUST_XOFF2_REGISTER);	\
	outb(__oldlcr, (baseio)+UART_LCR);	\
}

#define SET_MOXA_MUST_RBRTL_VALUE(baseio, Value) {	\
	u8	__oldlcr, __efr;	\
	__oldlcr = inb((baseio)+UART_LCR);	\
	outb(MOXA_MUST_ENTER_ENCHANCE, (baseio)+UART_LCR);	\
	__efr = inb((baseio)+MOXA_MUST_EFR_REGISTER);	\
	__efr &= ~MOXA_MUST_EFR_BANK_MASK;	\
	__efr |= MOXA_MUST_EFR_BANK1;	\
	outb(__efr, (baseio)+MOXA_MUST_EFR_REGISTER);	\
	outb((u8)(Value), (baseio)+MOXA_MUST_RBRTL_REGISTER);	\
	outb(__oldlcr, (baseio)+UART_LCR);	\
}

#define SET_MOXA_MUST_RBRTH_VALUE(baseio, Value) {	\
	u8	__oldlcr, __efr;	\
	__oldlcr = inb((baseio)+UART_LCR);	\
	outb(MOXA_MUST_ENTER_ENCHANCE, (baseio)+UART_LCR);	\
	__efr = inb((baseio)+MOXA_MUST_EFR_REGISTER);	\
	__efr &= ~MOXA_MUST_EFR_BANK_MASK;	\
	__efr |= MOXA_MUST_EFR_BANK1;	\
	outb(__efr, (baseio)+MOXA_MUST_EFR_REGISTER);	\
	outb((u8)(Value), (baseio)+MOXA_MUST_RBRTH_REGISTER);	\
	outb(__oldlcr, (baseio)+UART_LCR);	\
}

#define SET_MOXA_MUST_RBRTI_VALUE(baseio, Value) {	\
	u8	__oldlcr, __efr;	\
	__oldlcr = inb((baseio)+UART_LCR);	\
	outb(MOXA_MUST_ENTER_ENCHANCE, (baseio)+UART_LCR);	\
	__efr = inb((baseio)+MOXA_MUST_EFR_REGISTER);	\
	__efr &= ~MOXA_MUST_EFR_BANK_MASK;	\
	__efr |= MOXA_MUST_EFR_BANK1;	\
	outb(__efr, (baseio)+MOXA_MUST_EFR_REGISTER);	\
	outb((u8)(Value), (baseio)+MOXA_MUST_RBRTI_REGISTER);	\
	outb(__oldlcr, (baseio)+UART_LCR);	\
}

#define SET_MOXA_MUST_THRTL_VALUE(baseio, Value) {	\
	u8	__oldlcr, __efr;	\
	__oldlcr = inb((baseio)+UART_LCR);	\
	outb(MOXA_MUST_ENTER_ENCHANCE, (baseio)+UART_LCR);	\
	__efr = inb((baseio)+MOXA_MUST_EFR_REGISTER);	\
	__efr &= ~MOXA_MUST_EFR_BANK_MASK;	\
	__efr |= MOXA_MUST_EFR_BANK1;	\
	outb(__efr, (baseio)+MOXA_MUST_EFR_REGISTER);	\
	outb((u8)(Value), (baseio)+MOXA_MUST_THRTL_REGISTER);	\
	outb(__oldlcr, (baseio)+UART_LCR);	\
}

//#define MOXA_MUST_RBRL_VALUE  4
#define SET_MOXA_MUST_FIFO_VALUE(info) {	\
	u8	__oldlcr, __efr;	\
	__oldlcr = inb((info)->base+UART_LCR);	\
	outb(MOXA_MUST_ENTER_ENCHANCE, (info)->base+UART_LCR);	\
	__efr = inb((info)->base+MOXA_MUST_EFR_REGISTER);	\
	__efr &= ~MOXA_MUST_EFR_BANK_MASK;	\
	__efr |= MOXA_MUST_EFR_BANK1;	\
	outb(__efr, (info)->base+MOXA_MUST_EFR_REGISTER);	\
	outb((u8)((info)->rx_high_water), (info)->base+MOXA_MUST_RBRTH_REGISTER);	\
	outb((u8)((info)->rx_trigger), (info)->base+MOXA_MUST_RBRTI_REGISTER);	\
	outb((u8)((info)->rx_low_water), (info)->base+MOXA_MUST_RBRTL_REGISTER);	\
	outb(__oldlcr, (info)->base+UART_LCR);	\
}



#define SET_MOXA_MUST_ENUM_VALUE(baseio, Value) {	\
	u8	__oldlcr, __efr;	\
	__oldlcr = inb((baseio)+UART_LCR);	\
	outb(MOXA_MUST_ENTER_ENCHANCE, (baseio)+UART_LCR);	\
	__efr = inb((baseio)+MOXA_MUST_EFR_REGISTER);	\
	__efr &= ~MOXA_MUST_EFR_BANK_MASK;	\
	__efr |= MOXA_MUST_EFR_BANK2;	\
	outb(__efr, (baseio)+MOXA_MUST_EFR_REGISTER);	\
	outb((u8)(Value), (baseio)+MOXA_MUST_ENUM_REGISTER);	\
	outb(__oldlcr, (baseio)+UART_LCR);	\
}

#define GET_MOXA_MUST_HARDWARE_ID(baseio, pId) {	\
	u8	__oldlcr, __efr;	\
	__oldlcr = inb((baseio)+UART_LCR);	\
	outb(MOXA_MUST_ENTER_ENCHANCE, (baseio)+UART_LCR);	\
	__efr = inb((baseio)+MOXA_MUST_EFR_REGISTER);	\
	__efr &= ~MOXA_MUST_EFR_BANK_MASK;	\
	__efr |= MOXA_MUST_EFR_BANK2;	\
	outb(__efr, (baseio)+MOXA_MUST_EFR_REGISTER);	\
	*pId = inb((baseio)+MOXA_MUST_HWID_REGISTER);	\
	outb(__oldlcr, (baseio)+UART_LCR);	\
}

#define SET_MOXA_MUST_NO_SOFTWARE_FLOW_CONTROL(baseio) {	\
	u8	__oldlcr, __efr;	\
	__oldlcr = inb((baseio)+UART_LCR);	\
	outb(MOXA_MUST_ENTER_ENCHANCE, (baseio)+UART_LCR);	\
	__efr = inb((baseio)+MOXA_MUST_EFR_REGISTER);	\
	__efr &= ~MOXA_MUST_EFR_SF_MASK;	\
	outb(__efr, (baseio)+MOXA_MUST_EFR_REGISTER);	\
	outb(__oldlcr, (baseio)+UART_LCR);	\
}

#define SET_MOXA_MUST_JUST_TX_SOFTWARE_FLOW_CONTROL(baseio) {	\
	u8	__oldlcr, __efr;	\
	__oldlcr = inb((baseio)+UART_LCR);	\
	outb(MOXA_MUST_ENTER_ENCHANCE, (baseio)+UART_LCR);	\
	__efr = inb((baseio)+MOXA_MUST_EFR_REGISTER);	\
	__efr &= ~MOXA_MUST_EFR_SF_MASK;	\
	__efr |= MOXA_MUST_EFR_SF_TX1;	\
	outb(__efr, (baseio)+MOXA_MUST_EFR_REGISTER);	\
	outb(__oldlcr, (baseio)+UART_LCR);	\
}

#define ENABLE_MOXA_MUST_TX_SOFTWARE_FLOW_CONTROL(baseio) {	\
	u8	__oldlcr, __efr;	\
	__oldlcr = inb((baseio)+UART_LCR);	\
	outb(MOXA_MUST_ENTER_ENCHANCE, (baseio)+UART_LCR);	\
	__efr = inb((baseio)+MOXA_MUST_EFR_REGISTER);	\
	__efr &= ~MOXA_MUST_EFR_SF_TX_MASK;	\
	__efr |= MOXA_MUST_EFR_SF_TX1;	\
	outb(__efr, (baseio)+MOXA_MUST_EFR_REGISTER);	\
	outb(__oldlcr, (baseio)+UART_LCR);	\
}

#define DISABLE_MOXA_MUST_TX_SOFTWARE_FLOW_CONTROL(baseio) {	\
	u8	__oldlcr, __efr;	\
	__oldlcr = inb((baseio)+UART_LCR);	\
	outb(MOXA_MUST_ENTER_ENCHANCE, (baseio)+UART_LCR);	\
	__efr = inb((baseio)+MOXA_MUST_EFR_REGISTER);	\
	__efr &= ~MOXA_MUST_EFR_SF_TX_MASK;	\
	outb(__efr, (baseio)+MOXA_MUST_EFR_REGISTER);	\
	outb(__oldlcr, (baseio)+UART_LCR);	\
}

#define SET_MOXA_MUST_JUST_RX_SOFTWARE_FLOW_CONTROL(baseio) {	\
	u8	__oldlcr, __efr;	\
	__oldlcr = inb((baseio)+UART_LCR);	\
	outb(MOXA_MUST_ENTER_ENCHANCE, (baseio)+UART_LCR);	\
	__efr = inb((baseio)+MOXA_MUST_EFR_REGISTER);	\
	__efr &= ~MOXA_MUST_EFR_SF_MASK;	\
	__efr |= MOXA_MUST_EFR_SF_RX1;	\
	outb(__efr, (baseio)+MOXA_MUST_EFR_REGISTER);	\
	outb(__oldlcr, (baseio)+UART_LCR);	\
}

#define ENABLE_MOXA_MUST_RX_SOFTWARE_FLOW_CONTROL(baseio) {	\
	u8	__oldlcr, __efr;	\
	__oldlcr = inb((baseio)+UART_LCR);	\
	outb(MOXA_MUST_ENTER_ENCHANCE, (baseio)+UART_LCR);	\
	__efr = inb((baseio)+MOXA_MUST_EFR_REGISTER);	\
	__efr &= ~MOXA_MUST_EFR_SF_RX_MASK;	\
	__efr |= MOXA_MUST_EFR_SF_RX1;	\
	outb(__efr, (baseio)+MOXA_MUST_EFR_REGISTER);	\
	outb(__oldlcr, (baseio)+UART_LCR);	\
}

#define DISABLE_MOXA_MUST_RX_SOFTWARE_FLOW_CONTROL(baseio) {	\
	u8	__oldlcr, __efr;	\
	__oldlcr = inb((baseio)+UART_LCR);	\
	outb(MOXA_MUST_ENTER_ENCHANCE, (baseio)+UART_LCR);	\
	__efr = inb((baseio)+MOXA_MUST_EFR_REGISTER);	\
	__efr &= ~MOXA_MUST_EFR_SF_RX_MASK;	\
	outb(__efr, (baseio)+MOXA_MUST_EFR_REGISTER);	\
	outb(__oldlcr, (baseio)+UART_LCR);	\
}

#define ENABLE_MOXA_MUST_TX_RX_SOFTWARE_FLOW_CONTROL(baseio) {	\
	u8	__oldlcr, __efr;	\
	__oldlcr = inb((baseio)+UART_LCR);	\
	outb(MOXA_MUST_ENTER_ENCHANCE, (baseio)+UART_LCR);	\
	__efr = inb((baseio)+MOXA_MUST_EFR_REGISTER);	\
	__efr &= ~MOXA_MUST_EFR_SF_MASK;	\
	__efr |= (MOXA_MUST_EFR_SF_RX1|MOXA_MUST_EFR_SF_TX1);	\
	outb(__efr, (baseio)+MOXA_MUST_EFR_REGISTER);	\
	outb(__oldlcr, (baseio)+UART_LCR);	\
}

#define ENABLE_MOXA_MUST_XON_ANY_FLOW_CONTROL(baseio) {	\
	u8	__oldmcr;	\
	__oldmcr = inb((baseio)+UART_MCR);	\
	__oldmcr |= MOXA_MUST_MCR_XON_ANY;	\
	outb(__oldmcr, (baseio)+UART_MCR);	\
}

#define DISABLE_MOXA_MUST_XON_ANY_FLOW_CONTROL(baseio) {	\
	u8	__oldmcr;	\
	__oldmcr = inb((baseio)+UART_MCR);	\
	__oldmcr &= ~MOXA_MUST_MCR_XON_ANY;	\
	outb(__oldmcr, (baseio)+UART_MCR);	\
}

#define READ_MOXA_MUST_GDL(baseio)	inb((baseio)+MOXA_MUST_GDL_REGISTER)


#ifndef INIT_WORK
#define INIT_WORK(_work, _func, _data){	\
	_data->tqueue.routine = _func;\
	_data->tqueue.data = _data;\
	}
#endif

#endif

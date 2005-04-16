/*
 * Definitions for Belkin USB Serial Adapter Driver
 *
 *  Copyright (C) 2000
 *      William Greathouse (wgreathouse@smva.com)
 *
 *  This program is largely derived from work by the linux-usb group
 *  and associated source files.  Please see the usb/serial files for
 *  individual credits and copyrights.
 *  
 * 	This program is free software; you can redistribute it and/or modify
 * 	it under the terms of the GNU General Public License as published by
 * 	the Free Software Foundation; either version 2 of the License, or
 * 	(at your option) any later version.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this driver
 *
 * 12-Mar-2001 gkh
 *	Added GoHubs GO-COM232 device id.
 *
 * 06-Nov-2000 gkh
 *	Added old Belkin and Peracom device ids, which this driver supports
 *
 * 12-Oct-2000 William Greathouse
 *    First cut at supporting Belkin USB Serial Adapter F5U103
 *    I did not have a copy of the original work to support this
 *    adapter, so pardon any stupid mistakes.  All of the information
 *    I am using to write this driver was acquired by using a modified
 *    UsbSnoop on Windows2000.
 *    
 */

#ifndef __LINUX_USB_SERIAL_BSA_H
#define __LINUX_USB_SERIAL_BSA_H

#define BELKIN_DOCKSTATION_VID	0x050d	/* Vendor Id */
#define BELKIN_DOCKSTATION_PID	0x1203	/* Product Id */

#define BELKIN_SA_VID	0x050d	/* Vendor Id */
#define BELKIN_SA_PID	0x0103	/* Product Id */

#define BELKIN_OLD_VID	0x056c	/* Belkin's "old" vendor id */
#define BELKIN_OLD_PID	0x8007	/* Belkin's "old" single port serial converter's id */

#define PERACOM_VID	0x0565	/* Peracom's vendor id */
#define PERACOM_PID	0x0001	/* Peracom's single port serial converter's id */

#define GOHUBS_VID	0x0921	/* GoHubs vendor id */
#define GOHUBS_PID	0x1000	/* GoHubs single port serial converter's id (identical to the Peracom device) */
#define HANDYLINK_PID	0x1200	/* HandyLink USB's id (identical to the Peracom device) */

/* Vendor Request Interface */
#define BELKIN_SA_SET_BAUDRATE_REQUEST	0  /* Set baud rate */
#define BELKIN_SA_SET_STOP_BITS_REQUEST	1  /* Set stop bits (1,2) */
#define BELKIN_SA_SET_DATA_BITS_REQUEST	2  /* Set data bits (5,6,7,8) */
#define BELKIN_SA_SET_PARITY_REQUEST	3  /* Set parity (None, Even, Odd) */

#define BELKIN_SA_SET_DTR_REQUEST	10 /* Set DTR state */
#define BELKIN_SA_SET_RTS_REQUEST	11 /* Set RTS state */
#define BELKIN_SA_SET_BREAK_REQUEST	12 /* Set BREAK state */

#define BELKIN_SA_SET_FLOW_CTRL_REQUEST	16 /* Set flow control mode */


#ifdef WHEN_I_LEARN_THIS
#define BELKIN_SA_SET_MAGIC_REQUEST	17 /* I don't know, possibly flush */
					   /* (always in Wininit sequence before flow control) */
#define BELKIN_SA_RESET 		xx /* Reset the port */
#define BELKIN_SA_GET_MODEM_STATUS	xx /* Force return of modem status register */
#endif

#define BELKIN_SA_SET_REQUEST_TYPE	0x40

#define BELKIN_SA_BAUD(b)		(230400/b)

#define BELKIN_SA_STOP_BITS(b)		(b-1)

#define BELKIN_SA_DATA_BITS(b)		(b-5)

#define BELKIN_SA_PARITY_NONE		0
#define BELKIN_SA_PARITY_EVEN		1
#define BELKIN_SA_PARITY_ODD		2
#define BELKIN_SA_PARITY_MARK		3
#define BELKIN_SA_PARITY_SPACE		4

#define BELKIN_SA_FLOW_NONE		0x0000	/* No flow control */
#define BELKIN_SA_FLOW_OCTS		0x0001	/* use CTS input to throttle output */
#define BELKIN_SA_FLOW_ODSR		0x0002	/* use DSR input to throttle output */
#define BELKIN_SA_FLOW_IDSR		0x0004	/* use DSR input to enable receive */
#define BELKIN_SA_FLOW_IDTR		0x0008	/* use DTR output for input flow control */
#define BELKIN_SA_FLOW_IRTS		0x0010	/* use RTS output for input flow control */
#define BELKIN_SA_FLOW_ORTS		0x0020	/* use RTS to indicate data available to send */
#define BELKIN_SA_FLOW_ERRSUB		0x0040	/* ???? guess ???? substitute inline errors */
#define BELKIN_SA_FLOW_OXON		0x0080	/* use XON/XOFF for output flow control */
#define BELKIN_SA_FLOW_IXON		0x0100	/* use XON/XOFF for input flow control */

/*
 * It seems that the interrupt pipe is closely modelled after the
 * 16550 register layout.  This is probably because the adapter can 
 * be used in a "DOS" environment to simulate a standard hardware port.
 */
#define BELKIN_SA_LSR_INDEX		2		/* Line Status Register */
#define BELKIN_SA_LSR_RDR		0x01	/* receive data ready */
#define BELKIN_SA_LSR_OE		0x02	/* overrun error */
#define BELKIN_SA_LSR_PE		0x04	/* parity error */
#define BELKIN_SA_LSR_FE		0x08	/* framing error */
#define BELKIN_SA_LSR_BI		0x10	/* break indicator */
#define BELKIN_SA_LSR_THE		0x20	/* transmit holding register empty */
#define BELKIN_SA_LSR_TE		0x40	/* transmit register empty */
#define BELKIN_SA_LSR_ERR		0x80	/* OE | PE | FE | BI */

#define BELKIN_SA_MSR_INDEX		3		/* Modem Status Register */
#define BELKIN_SA_MSR_DCTS		0x01	/* Delta CTS */
#define BELKIN_SA_MSR_DDSR		0x02	/* Delta DSR */
#define BELKIN_SA_MSR_DRI		0x04	/* Delta RI */
#define BELKIN_SA_MSR_DCD		0x08	/* Delta CD */
#define BELKIN_SA_MSR_CTS		0x10	/* Current CTS */
#define BELKIN_SA_MSR_DSR		0x20	/* Current DSR */
#define BELKIN_SA_MSR_RI		0x40	/* Current RI */
#define BELKIN_SA_MSR_CD		0x80	/* Current CD */

#endif /* __LINUX_USB_SERIAL_BSA_H */


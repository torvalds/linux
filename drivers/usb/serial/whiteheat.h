/*
 * USB ConnectTech WhiteHEAT driver
 *
 *      Copyright (C) 2002
 *          Connect Tech Inc.	
 *
 *      Copyright (C) 1999, 2000
 *          Greg Kroah-Hartman (greg@kroah.com)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this driver
 *
 */

#ifndef __LINUX_USB_SERIAL_WHITEHEAT_H
#define __LINUX_USB_SERIAL_WHITEHEAT_H


/* WhiteHEAT commands */
#define WHITEHEAT_OPEN			1	/* open the port */
#define WHITEHEAT_CLOSE			2	/* close the port */
#define WHITEHEAT_SETUP_PORT		3	/* change port settings */
#define WHITEHEAT_SET_RTS		4	/* turn RTS on or off */
#define WHITEHEAT_SET_DTR		5	/* turn DTR on or off */
#define WHITEHEAT_SET_BREAK		6	/* turn BREAK on or off */
#define WHITEHEAT_DUMP			7	/* dump memory */
#define WHITEHEAT_STATUS		8	/* get status */
#define WHITEHEAT_PURGE			9	/* clear the UART fifos */
#define WHITEHEAT_GET_DTR_RTS		10	/* get the state of DTR and RTS for a port */
#define WHITEHEAT_GET_HW_INFO		11	/* get EEPROM info and hardware ID */
#define WHITEHEAT_REPORT_TX_DONE	12	/* get the next TX done */
#define WHITEHEAT_EVENT			13	/* unsolicited status events */
#define WHITEHEAT_ECHO			14	/* send data to the indicated IN endpoint */
#define WHITEHEAT_DO_TEST		15	/* perform the specified test */
#define WHITEHEAT_CMD_COMPLETE		16	/* reply for certain commands */
#define WHITEHEAT_CMD_FAILURE		17	/* reply for failed commands */


/*
 * Commands to the firmware
 */


/*
 * WHITEHEAT_OPEN
 * WHITEHEAT_CLOSE
 * WHITEHEAT_STATUS
 * WHITEHEAT_GET_DTR_RTS
 * WHITEHEAT_REPORT_TX_DONE
*/
struct whiteheat_simple {
	__u8	port;	/* port number (1 to N) */
};


/*
 * WHITEHEAT_SETUP_PORT
 */
#define WHITEHEAT_PAR_NONE	'n'	/* no parity */
#define WHITEHEAT_PAR_EVEN	'e'	/* even parity */
#define WHITEHEAT_PAR_ODD	'o'	/* odd parity */
#define WHITEHEAT_PAR_SPACE	'0'	/* space (force 0) parity */
#define WHITEHEAT_PAR_MARK	'1'	/* mark (force 1) parity */

#define WHITEHEAT_SFLOW_NONE	'n'	/* no software flow control */
#define WHITEHEAT_SFLOW_RX	'r'	/* XOFF/ON is sent when RX fills/empties */
#define WHITEHEAT_SFLOW_TX	't'	/* when received XOFF/ON will stop/start TX */
#define WHITEHEAT_SFLOW_RXTX	'b'	/* both SFLOW_RX and SFLOW_TX */

#define WHITEHEAT_HFLOW_NONE		0x00	/* no hardware flow control */
#define WHITEHEAT_HFLOW_RTS_TOGGLE	0x01	/* RTS is on during transmit, off otherwise */
#define WHITEHEAT_HFLOW_DTR		0x02	/* DTR is off/on when RX fills/empties */
#define WHITEHEAT_HFLOW_CTS		0x08	/* when received CTS off/on will stop/start TX */
#define WHITEHEAT_HFLOW_DSR		0x10	/* when received DSR off/on will stop/start TX */
#define WHITEHEAT_HFLOW_RTS		0x80	/* RTS is off/on when RX fills/empties */

struct whiteheat_port_settings {
	__u8	port;		/* port number (1 to N) */
	__u32	baud;		/* any value 7 - 460800, firmware calculates best fit; arrives little endian */
	__u8	bits;		/* 5, 6, 7, or 8 */
	__u8	stop;		/* 1 or 2, default 1 (2 = 1.5 if bits = 5) */
	__u8	parity;		/* see WHITEHEAT_PAR_* above */
	__u8	sflow;		/* see WHITEHEAT_SFLOW_* above */
	__u8	xoff;		/* XOFF byte value */
	__u8	xon;		/* XON byte value */
	__u8	hflow;		/* see WHITEHEAT_HFLOW_* above */
	__u8	lloop;		/* 0/1 turns local loopback mode off/on */
} __attribute__ ((packed));


/*
 * WHITEHEAT_SET_RTS
 * WHITEHEAT_SET_DTR
 * WHITEHEAT_SET_BREAK
 */
#define WHITEHEAT_RTS_OFF	0x00
#define WHITEHEAT_RTS_ON	0x01
#define WHITEHEAT_DTR_OFF	0x00
#define WHITEHEAT_DTR_ON	0x01
#define WHITEHEAT_BREAK_OFF	0x00
#define WHITEHEAT_BREAK_ON	0x01

struct whiteheat_set_rdb {
	__u8	port;		/* port number (1 to N) */
	__u8	state;		/* 0/1 turns signal off/on */
};


/*
 * WHITEHEAT_DUMP
 */
#define WHITEHEAT_DUMP_MEM_DATA		'd'  /* data */
#define WHITEHEAT_DUMP_MEM_IDATA	'i'  /* idata */
#define WHITEHEAT_DUMP_MEM_BDATA	'b'  /* bdata */
#define WHITEHEAT_DUMP_MEM_XDATA	'x'  /* xdata */

/*
 * Allowable address ranges (firmware checks address):
 * Type DATA:  0x00 - 0xff
 * Type IDATA: 0x80 - 0xff
 * Type BDATA: 0x20 - 0x2f
 * Type XDATA: 0x0000 - 0xffff
 *
 * B/I/DATA all read the local memory space
 * XDATA reads the external memory space
 * BDATA returns bits as bytes
 *
 * NOTE: 0x80 - 0xff (local space) are the Special Function Registers
 *       of the 8051, and some have on-read side-effects.
 */

struct whiteheat_dump {
	__u8	mem_type;	/* see WHITEHEAT_DUMP_* above */
	__u16	addr;		/* address, see restrictions above */
	__u16	length;		/* number of bytes to dump, max 63 bytes */
};


/*
 * WHITEHEAT_PURGE
 */
#define WHITEHEAT_PURGE_RX	0x01	/* purge rx fifos */
#define WHITEHEAT_PURGE_TX	0x02	/* purge tx fifos */

struct whiteheat_purge {
	__u8	port;		/* port number (1 to N) */
	__u8	what;		/* bit pattern of what to purge */
};


/*
 * WHITEHEAT_ECHO
 */
struct whiteheat_echo {
	__u8	port;		/* port number (1 to N) */
	__u8	length;		/* length of message to echo, max 61 bytes */
	__u8	echo_data[61];	/* data to echo */
};


/*
 * WHITEHEAT_DO_TEST
 */
#define WHITEHEAT_TEST_UART_RW		0x01  /* read/write uart registers */
#define WHITEHEAT_TEST_UART_INTR	0x02  /* uart interrupt */
#define WHITEHEAT_TEST_SETUP_CONT	0x03  /* setup for PORT_CONT/PORT_DISCONT */
#define WHITEHEAT_TEST_PORT_CONT	0x04  /* port connect */
#define WHITEHEAT_TEST_PORT_DISCONT	0x05  /* port disconnect */
#define WHITEHEAT_TEST_UART_CLK_START	0x06  /* uart clock test start */
#define WHITEHEAT_TEST_UART_CLK_STOP	0x07  /* uart clock test stop */
#define WHITEHEAT_TEST_MODEM_FT		0x08  /* modem signals, requires a loopback cable/connector */
#define WHITEHEAT_TEST_ERASE_EEPROM	0x09  /* erase eeprom */
#define WHITEHEAT_TEST_READ_EEPROM	0x0a  /* read eeprom */
#define WHITEHEAT_TEST_PROGRAM_EEPROM	0x0b  /* program eeprom */

struct whiteheat_test {
	__u8	port;		/* port number (1 to n) */
	__u8	test;		/* see WHITEHEAT_TEST_* above*/
	__u8	info[32];	/* additional info */
};


/*
 * Replies from the firmware
 */


/*
 * WHITEHEAT_STATUS
 */
#define WHITEHEAT_EVENT_MODEM		0x01	/* modem field is valid */
#define WHITEHEAT_EVENT_ERROR		0x02	/* error field is valid */
#define WHITEHEAT_EVENT_FLOW		0x04	/* flow field is valid */
#define WHITEHEAT_EVENT_CONNECT		0x08	/* connect field is valid */

#define WHITEHEAT_FLOW_NONE		0x00	/* no flow control active */
#define WHITEHEAT_FLOW_HARD_OUT		0x01	/* TX is stopped by CTS (waiting for CTS to go on) */
#define WHITEHEAT_FLOW_HARD_IN		0x02	/* remote TX is stopped by RTS */
#define WHITEHEAT_FLOW_SOFT_OUT		0x04	/* TX is stopped by XOFF received (waiting for XON) */
#define WHITEHEAT_FLOW_SOFT_IN		0x08	/* remote TX is stopped by XOFF transmitted */
#define WHITEHEAT_FLOW_TX_DONE		0x80	/* TX has completed */

struct whiteheat_status_info {
	__u8	port;		/* port number (1 to N) */
	__u8	event;		/* indicates what the current event is, see WHITEHEAT_EVENT_* above */
	__u8	modem;		/* modem signal status (copy of uart's MSR register) */
	__u8	error;		/* line status (copy of uart's LSR register) */
	__u8	flow;		/* flow control state, see WHITEHEAT_FLOW_* above */
	__u8	connect;	/* 0 means not connected, non-zero means connected */
};


/*
 * WHITEHEAT_GET_DTR_RTS
 */
struct whiteheat_dr_info {
	__u8	mcr;		/* copy of uart's MCR register */
};


/*
 * WHITEHEAT_GET_HW_INFO
 */
struct whiteheat_hw_info {
	__u8	hw_id;		/* hardware id number, WhiteHEAT = 0 */
	__u8	sw_major_rev;	/* major version number */
	__u8	sw_minor_rev;	/* minor version number */
	struct whiteheat_hw_eeprom_info {
		__u8	b0;			/* B0 */
		__u8	vendor_id_low;		/* vendor id (low byte) */
		__u8	vendor_id_high;		/* vendor id (high byte) */
		__u8	product_id_low;		/* product id (low byte) */
		__u8	product_id_high;	/* product id (high byte) */
		__u8	device_id_low;		/* device id (low byte) */
		__u8	device_id_high;		/* device id (high byte) */
		__u8	not_used_1;
		__u8	serial_number_0;	/* serial number (low byte) */
		__u8	serial_number_1;	/* serial number */
		__u8	serial_number_2;	/* serial number */
		__u8	serial_number_3;	/* serial number (high byte) */
		__u8	not_used_2;
		__u8	not_used_3;
		__u8	checksum_low;		/* checksum (low byte) */
		__u8	checksum_high;		/* checksum (high byte */
	} hw_eeprom_info;	/* EEPROM contents */
};


/*
 * WHITEHEAT_EVENT
 */
struct whiteheat_event_info {
	__u8	port;		/* port number (1 to N) */
	__u8	event;		/* see whiteheat_status_info.event */
	__u8	info;		/* see whiteheat_status_info.modem, .error, .flow, .connect */
};


/*
 * WHITEHEAT_DO_TEST
 */
#define WHITEHEAT_TEST_FAIL	0x00  /* test failed */
#define WHITEHEAT_TEST_UNKNOWN	0x01  /* unknown test requested */
#define WHITEHEAT_TEST_PASS	0xff  /* test passed */

struct whiteheat_test_info {
	__u8	port;		/* port number (1 to N) */
	__u8	test;		/* indicates which test this is a response for, see WHITEHEAT_DO_TEST above */
	__u8	status;		/* see WHITEHEAT_TEST_* above */
	__u8	results[32];	/* test-dependent results */
};


#endif

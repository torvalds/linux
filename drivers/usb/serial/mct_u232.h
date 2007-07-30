/*
 * Definitions for MCT (Magic Control Technology) USB-RS232 Converter Driver
 *
 *   Copyright (C) 2000 Wolfgang Grandegger (wolfgang@ces.ch)
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 * This driver is for the device MCT USB-RS232 Converter (25 pin, Model No.
 * U232-P25) from Magic Control Technology Corp. (there is also a 9 pin
 * Model No. U232-P9). See http://www.mct.com.tw/p_u232.html for further
 * information. The properties of this device are listed at the end of this
 * file. This device is available from various distributors. I know Hana,
 * http://www.hana.de and D-Link, http://www.dlink.com/products/usb/dsbs25.
 *
 * All of the information about the device was acquired by using SniffUSB
 * on Windows98. The technical details of the reverse engineering are
 * summarized at the end of this file.
 */

#ifndef __LINUX_USB_SERIAL_MCT_U232_H
#define __LINUX_USB_SERIAL_MCT_U232_H

#define MCT_U232_VID	                0x0711	/* Vendor Id */
#define MCT_U232_PID	                0x0210	/* Original MCT Product Id */

/* U232-P25, Sitecom */
#define MCT_U232_SITECOM_PID		0x0230	/* Sitecom Product Id */

/* DU-H3SP USB BAY hub */
#define MCT_U232_DU_H3SP_PID		0x0200	/* D-Link DU-H3SP USB BAY */

/* Belkin badge the MCT U232-P9 as the F5U109 */
#define MCT_U232_BELKIN_F5U109_VID	0x050d	/* Vendor Id */
#define MCT_U232_BELKIN_F5U109_PID	0x0109	/* Product Id */

/*
 * Vendor Request Interface
 */
#define MCT_U232_SET_REQUEST_TYPE	0x40
#define MCT_U232_GET_REQUEST_TYPE	0xc0

#define MCT_U232_GET_MODEM_STAT_REQUEST 2  /* Get Modem Status Register (MSR) */
#define MCT_U232_GET_MODEM_STAT_SIZE    1

#define MCT_U232_GET_LINE_CTRL_REQUEST  6  /* Get Line Control Register (LCR) */
#define MCT_U232_GET_LINE_CTRL_SIZE     1  /* ... not used by this driver */

#define MCT_U232_SET_BAUD_RATE_REQUEST	5  /* Set Baud Rate Divisor */
#define MCT_U232_SET_BAUD_RATE_SIZE     4

#define MCT_U232_SET_LINE_CTRL_REQUEST	7  /* Set Line Control Register (LCR) */
#define MCT_U232_SET_LINE_CTRL_SIZE     1

#define MCT_U232_SET_MODEM_CTRL_REQUEST	10 /* Set Modem Control Register (MCR) */
#define MCT_U232_SET_MODEM_CTRL_SIZE    1

/* This USB device request code is not well understood.  It is transmitted by
   the MCT-supplied Windows driver whenever the baud rate changes. 
*/
#define MCT_U232_SET_UNKNOWN1_REQUEST   11  /* Unknown functionality */
#define MCT_U232_SET_UNKNOWN1_SIZE       1

/* This USB device request code appears to control whether CTS is required
   during transmission.
   
   Sending a zero byte allows data transmission to a device which is not
   asserting CTS.  Sending a '1' byte will cause transmission to be deferred
   until the device asserts CTS.
*/
#define MCT_U232_SET_CTS_REQUEST   12
#define MCT_U232_SET_CTS_SIZE       1

/*
 * Baud rate (divisor)
 * Actually, there are two of them, MCT website calls them "Philips solution"
 * and "Intel solution". They are the regular MCT and "Sitecom" for us.
 * This is pointless to document in the header, see the code for the bits.
 */
static int mct_u232_calculate_baud_rate(struct usb_serial *serial, speed_t value);

/*
 * Line Control Register (LCR)
 */
#define MCT_U232_SET_BREAK              0x40

#define MCT_U232_PARITY_SPACE		0x38
#define MCT_U232_PARITY_MARK		0x28
#define MCT_U232_PARITY_EVEN		0x18
#define MCT_U232_PARITY_ODD		0x08
#define MCT_U232_PARITY_NONE		0x00

#define MCT_U232_DATA_BITS_5            0x00
#define MCT_U232_DATA_BITS_6            0x01
#define MCT_U232_DATA_BITS_7            0x02
#define MCT_U232_DATA_BITS_8            0x03

#define MCT_U232_STOP_BITS_2            0x04
#define MCT_U232_STOP_BITS_1            0x00

/*
 * Modem Control Register (MCR)
 */
#define MCT_U232_MCR_NONE               0x8     /* Deactivate DTR and RTS */
#define MCT_U232_MCR_RTS                0xa     /* Activate RTS */
#define MCT_U232_MCR_DTR                0x9     /* Activate DTR */

/*
 * Modem Status Register (MSR)
 */
#define MCT_U232_MSR_INDEX              0x0     /* data[index] */
#define MCT_U232_MSR_CD                 0x80    /* Current CD */
#define MCT_U232_MSR_RI                 0x40    /* Current RI */
#define MCT_U232_MSR_DSR                0x20    /* Current DSR */
#define MCT_U232_MSR_CTS                0x10    /* Current CTS */
#define MCT_U232_MSR_DCD                0x08    /* Delta CD */
#define MCT_U232_MSR_DRI                0x04    /* Delta RI */
#define MCT_U232_MSR_DDSR               0x02    /* Delta DSR */
#define MCT_U232_MSR_DCTS               0x01    /* Delta CTS */

/*
 * Line Status Register (LSR)
 */
#define MCT_U232_LSR_INDEX              1       /* data[index] */
#define MCT_U232_LSR_ERR                0x80    /* OE | PE | FE | BI */
#define MCT_U232_LSR_TEMT               0x40    /* transmit register empty */
#define MCT_U232_LSR_THRE               0x20    /* transmit holding register empty */
#define MCT_U232_LSR_BI                 0x10    /* break indicator */
#define MCT_U232_LSR_FE                 0x08    /* framing error */
#define MCT_U232_LSR_OE                 0x02    /* overrun error */
#define MCT_U232_LSR_PE                 0x04    /* parity error */
#define MCT_U232_LSR_OE                 0x02    /* overrun error */
#define MCT_U232_LSR_DR                 0x01    /* receive data ready */


/* -----------------------------------------------------------------------------
 * Technical Specification reverse engineered with SniffUSB on Windows98
 * =====================================================================
 *
 *  The technical details of the device have been acquired be using "SniffUSB"
 *  and the vendor-supplied device driver (version 2.3A) under Windows98. To
 *  identify the USB vendor-specific requests and to assign them to terminal 
 *  settings (flow control, baud rate, etc.) the program "SerialSettings" from
 *  William G. Greathouse has been proven to be very useful. I also used the
 *  Win98 "HyperTerminal" and "usb-robot" on Linux for testing. The results and 
 *  observations are summarized below:
 *
 *  The USB requests seem to be directly mapped to the registers of a 8250,
 *  16450 or 16550 UART. The FreeBSD handbook (appendix F.4 "Input/Output
 *  devices") contains a comprehensive description of UARTs and its registers.
 *  The bit descriptions are actually taken from there.
 *
 *
 * Baud rate (divisor)
 * -------------------
 *
 *   BmRequestType:  0x40 (0100 0000B)
 *   bRequest:       0x05
 *   wValue:         0x0000
 *   wIndex:         0x0000
 *   wLength:        0x0004
 *   Data:           divisor = 115200 / baud_rate
 *
 *   SniffUSB observations (Nov 2003): Contrary to the 'wLength' value of 4
 *   shown above, observations with a Belkin F5U109 adapter, using the
 *   MCT-supplied Windows98 driver (U2SPORT.VXD, "File version: 1.21P.0104 for
 *   Win98/Me"), show this request has a length of 1 byte, presumably because
 *   of the fact that the Belkin adapter and the 'Sitecom U232-P25' adapter
 *   use a baud-rate code instead of a conventional RS-232 baud rate divisor.
 *   The current source code for this driver does not reflect this fact, but
 *   the driver works fine with this adapter/driver combination nonetheless.
 *
 *
 * Line Control Register (LCR)
 * ---------------------------
 *
 *  BmRequestType:  0x40 (0100 0000B)    0xc0 (1100 0000B)
 *  bRequest:       0x07                 0x06
 *  wValue:         0x0000
 *  wIndex:         0x0000
 *  wLength:        0x0001
 *  Data:           LCR (see below)
 *
 *  Bit 7: Divisor Latch Access Bit (DLAB). When set, access to the data
 *  	   transmit/receive register (THR/RBR) and the Interrupt Enable Register
 *  	   (IER) is disabled. Any access to these ports is now redirected to the
 *  	   Divisor Latch Registers. Setting this bit, loading the Divisor
 *  	   Registers, and clearing DLAB should be done with interrupts disabled.
 *  Bit 6: Set Break. When set to "1", the transmitter begins to transmit
 *  	   continuous Spacing until this bit is set to "0". This overrides any
 *  	   bits of characters that are being transmitted.
 *  Bit 5: Stick Parity. When parity is enabled, setting this bit causes parity
 *  	   to always be "1" or "0", based on the value of Bit 4.
 *  Bit 4: Even Parity Select (EPS). When parity is enabled and Bit 5 is "0",
 *  	   setting this bit causes even parity to be transmitted and expected.
 *  	   Otherwise, odd parity is used.
 *  Bit 3: Parity Enable (PEN). When set to "1", a parity bit is inserted
 *  	   between the last bit of the data and the Stop Bit. The UART will also
 *  	   expect parity to be present in the received data.
 *  Bit 2: Number of Stop Bits (STB). If set to "1" and using 5-bit data words,
 *  	   1.5 Stop Bits are transmitted and expected in each data word. For
 *  	   6, 7 and 8-bit data words, 2 Stop Bits are transmitted and expected.
 *  	   When this bit is set to "0", one Stop Bit is used on each data word.
 *  Bit 1: Word Length Select Bit #1 (WLSB1)
 *  Bit 0: Word Length Select Bit #0 (WLSB0)
 *  	   Together these bits specify the number of bits in each data word.
 *  	     1 0  Word Length
 *  	     0 0  5 Data Bits
 *  	     0 1  6 Data Bits
 *  	     1 0  7 Data Bits
 *  	     1 1  8 Data Bits
 *
 *  SniffUSB observations: Bit 7 seems not to be used. There seem to be two bugs
 *  in the Win98 driver: the break does not work (bit 6 is not asserted) and the
 *  stick parity bit is not cleared when set once. The LCR can also be read
 *  back with USB request 6 but this has never been observed with SniffUSB.
 *
 *
 * Modem Control Register (MCR)
 * ----------------------------
 *
 *  BmRequestType:  0x40  (0100 0000B)
 *  bRequest:       0x0a
 *  wValue:         0x0000
 *  wIndex:         0x0000
 *  wLength:        0x0001
 *  Data:           MCR (Bit 4..7, see below)
 *
 *  Bit 7: Reserved, always 0.
 *  Bit 6: Reserved, always 0.
 *  Bit 5: Reserved, always 0.
 *  Bit 4: Loop-Back Enable. When set to "1", the UART transmitter and receiver
 *  	   are internally connected together to allow diagnostic operations. In
 *  	   addition, the UART modem control outputs are connected to the UART
 *  	   modem control inputs. CTS is connected to RTS, DTR is connected to
 *  	   DSR, OUT1 is connected to RI, and OUT 2 is connected to DCD.
 *  Bit 3: OUT 2. An auxiliary output that the host processor may set high or
 *  	   low. In the IBM PC serial adapter (and most clones), OUT 2 is used
 *  	   to tri-state (disable) the interrupt signal from the
 *  	   8250/16450/16550 UART.
 *  Bit 2: OUT 1. An auxiliary output that the host processor may set high or
 *  	   low. This output is not used on the IBM PC serial adapter.
 *  Bit 1: Request to Send (RTS). When set to "1", the output of the UART -RTS
 *  	   line is Low (Active).
 *  Bit 0: Data Terminal Ready (DTR). When set to "1", the output of the UART
 *  	   -DTR line is Low (Active).
 *
 *  SniffUSB observations: Bit 2 and 4 seem not to be used but bit 3 has been
 *  seen _always_ set.
 *
 *
 * Modem Status Register (MSR)
 * ---------------------------
 *
 *  BmRequestType:  0xc0  (1100 0000B)
 *  bRequest:       0x02
 *  wValue:         0x0000
 *  wIndex:         0x0000
 *  wLength:        0x0001
 *  Data:           MSR (see below)
 *
 *  Bit 7: Data Carrier Detect (CD). Reflects the state of the DCD line on the
 *  	   UART.
 *  Bit 6: Ring Indicator (RI). Reflects the state of the RI line on the UART.
 *  Bit 5: Data Set Ready (DSR). Reflects the state of the DSR line on the UART.
 *  Bit 4: Clear To Send (CTS). Reflects the state of the CTS line on the UART.
 *  Bit 3: Delta Data Carrier Detect (DDCD). Set to "1" if the -DCD line has
 *  	   changed state one more more times since the last time the MSR was
 *  	   read by the host.
 *  Bit 2: Trailing Edge Ring Indicator (TERI). Set to "1" if the -RI line has
 *  	   had a low to high transition since the last time the MSR was read by
 *  	   the host.
 *  Bit 1: Delta Data Set Ready (DDSR). Set to "1" if the -DSR line has changed
 *  	   state one more more times since the last time the MSR was read by the
 *  	   host.
 *  Bit 0: Delta Clear To Send (DCTS). Set to "1" if the -CTS line has changed
 *  	   state one more times since the last time the MSR was read by the
 *  	   host.
 *
 *  SniffUSB observations: the MSR is also returned as first byte on the
 *  interrupt-in endpoint 0x83 to signal changes of modem status lines. The USB
 *  request to read MSR cannot be applied during normal device operation.
 *
 *
 * Line Status Register (LSR)
 * --------------------------
 *
 *  Bit 7   Error in Receiver FIFO. On the 8250/16450 UART, this bit is zero.
 *  	    This bit is set to "1" when any of the bytes in the FIFO have one or
 *  	    more of the following error conditions: PE, FE, or BI.
 *  Bit 6   Transmitter Empty (TEMT). When set to "1", there are no words
 *  	    remaining in the transmit FIFO or the transmit shift register. The
 *  	    transmitter is completely idle.
 *  Bit 5   Transmitter Holding Register Empty (THRE). When set to "1", the FIFO
 *  	    (or holding register) now has room for at least one additional word
 *  	    to transmit. The transmitter may still be transmitting when this bit
 *  	    is set to "1".
 *  Bit 4   Break Interrupt (BI). The receiver has detected a Break signal.
 *  Bit 3   Framing Error (FE). A Start Bit was detected but the Stop Bit did not
 *  	    appear at the expected time. The received word is probably garbled.
 *  Bit 2   Parity Error (PE). The parity bit was incorrect for the word received.
 *  Bit 1   Overrun Error (OE). A new word was received and there was no room in
 *  	    the receive buffer. The newly-arrived word in the shift register is
 *  	    discarded. On 8250/16450 UARTs, the word in the holding register is
 *  	    discarded and the newly- arrived word is put in the holding register.
 *  Bit 0   Data Ready (DR). One or more words are in the receive FIFO that the
 *  	    host may read. A word must be completely received and moved from the
 *  	    shift register into the FIFO (or holding register for 8250/16450
 *  	    designs) before this bit is set.
 *
 *  SniffUSB observations: the LSR is returned as second byte on the interrupt-in
 *  endpoint 0x83 to signal error conditions. Such errors have been seen with
 *  minicom/zmodem transfers (CRC errors).
 *
 *
 * Unknown #1
 * -------------------
 *
 *   BmRequestType:  0x40 (0100 0000B)
 *   bRequest:       0x0b
 *   wValue:         0x0000
 *   wIndex:         0x0000
 *   wLength:        0x0001
 *   Data:           0x00
 *
 *   SniffUSB observations (Nov 2003): With the MCT-supplied Windows98 driver
 *   (U2SPORT.VXD, "File version: 1.21P.0104 for Win98/Me"), this request
 *   occurs immediately after a "Baud rate (divisor)" message.  It was not
 *   observed at any other time.  It is unclear what purpose this message
 *   serves.
 *
 *
 * Unknown #2
 * -------------------
 *
 *   BmRequestType:  0x40 (0100 0000B)
 *   bRequest:       0x0c
 *   wValue:         0x0000
 *   wIndex:         0x0000
 *   wLength:        0x0001
 *   Data:           0x00
 *
 *   SniffUSB observations (Nov 2003): With the MCT-supplied Windows98 driver
 *   (U2SPORT.VXD, "File version: 1.21P.0104 for Win98/Me"), this request
 *   occurs immediately after the 'Unknown #1' message (see above).  It was
 *   not observed at any other time.  It is unclear what other purpose (if
 *   any) this message might serve, but without it, the USB/RS-232 adapter
 *   will not write to RS-232 devices which do not assert the 'CTS' signal.
 *
 *
 * Flow control
 * ------------
 *
 *  SniffUSB observations: no flow control specific requests have been realized
 *  apart from DTR/RTS settings. Both signals are dropped for no flow control
 *  but asserted for hardware or software flow control.
 *
 *
 * Endpoint usage
 * --------------
 *
 *  SniffUSB observations: the bulk-out endpoint 0x1 and interrupt-in endpoint
 *  0x81 is used to transmit and receive characters. The second interrupt-in 
 *  endpoint 0x83 signals exceptional conditions like modem line changes and 
 *  errors. The first byte returned is the MSR and the second byte the LSR.
 *
 *
 * Other observations
 * ------------------
 *
 *  Queued bulk transfers like used in visor.c did not work. 
 *  
 *
 * Properties of the USB device used (as found in /var/log/messages)
 * -----------------------------------------------------------------
 *
 *  Manufacturer: MCT Corporation.
 *  Product: USB-232 Interfact Controller
 *  SerialNumber: U2S22050
 *
 *    Length              = 18
 *    DescriptorType      = 01
 *    USB version         = 1.00
 *    Vendor:Product      = 0711:0210
 *    MaxPacketSize0      = 8
 *    NumConfigurations   = 1
 *    Device version      = 1.02
 *    Device Class:SubClass:Protocol = 00:00:00
 *      Per-interface classes
 *  Configuration:
 *    bLength             =    9
 *    bDescriptorType     =   02
 *    wTotalLength        = 0027
 *    bNumInterfaces      =   01
 *    bConfigurationValue =   01
 *    iConfiguration      =   00
 *    bmAttributes        =   c0
 *    MaxPower            =  100mA
 *
 *    Interface: 0
 *    Alternate Setting:  0
 *      bLength             =    9
 *      bDescriptorType     =   04
 *      bInterfaceNumber    =   00
 *      bAlternateSetting   =   00
 *      bNumEndpoints       =   03
 *      bInterface Class:SubClass:Protocol =   00:00:00
 *      iInterface          =   00
 *      Endpoint:
 * 	  bLength             =    7
 * 	  bDescriptorType     =   05
 * 	  bEndpointAddress    =   81 (in)
 * 	  bmAttributes        =   03 (Interrupt)
 * 	  wMaxPacketSize      = 0040
 * 	  bInterval           =   02
 *      Endpoint:
 * 	  bLength             =    7
 * 	  bDescriptorType     =   05
 * 	  bEndpointAddress    =   01 (out)
 * 	  bmAttributes        =   02 (Bulk)
 * 	  wMaxPacketSize      = 0040
 * 	  bInterval           =   00
 *      Endpoint:
 * 	  bLength             =    7
 * 	  bDescriptorType     =   05
 * 	  bEndpointAddress    =   83 (in)
 * 	  bmAttributes        =   03 (Interrupt)
 * 	  wMaxPacketSize      = 0002
 * 	  bInterval           =   02
 *
 *
 * Hardware details (added by Martin Hamilton, 2001/12/06)
 * -----------------------------------------------------------------
 *
 * This info was gleaned from opening a Belkin F5U109 DB9 USB serial
 * adaptor, which turns out to simply be a re-badged U232-P9.  We
 * know this because there is a sticky label on the circuit board
 * which says "U232-P9" ;-)
 * 
 * The circuit board inside the adaptor contains a Philips PDIUSBD12
 * USB endpoint chip and a Philips P87C52UBAA microcontroller with
 * embedded UART.  Exhaustive documentation for these is available at:
 *
 *   http://www.semiconductors.philips.com/pip/p87c52ubaa
 *   http://www.semiconductors.philips.com/pip/pdiusbd12
 *
 * Thanks to Julian Highfield for the pointer to the Philips database.
 * 
 */

#endif /* __LINUX_USB_SERIAL_MCT_U232_H */


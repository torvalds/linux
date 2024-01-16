/* SPDX-License-Identifier: BSD-3-Clause */
/*
	usa90msg.h

	Copyright (c) 1998-2003 InnoSys Incorporated.  All Rights Reserved
	This file is available under a BSD-style copyright

	Keyspan USB Async Message Formats for the USA19HS

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are
	met:

	1. Redistributions of source code must retain this licence text
   	without modification, this list of conditions, and the following
   	disclaimer.  The following copyright notice must appear immediately at
   	the beginning of all source files:

        	Copyright (c) 1998-2003 InnoSys Incorporated.  All Rights Reserved

        	This file is available under a BSD-style copyright

	2. The name of InnoSys Incorporated may not be used to endorse or promote
   	products derived from this software without specific prior written
   	permission.

	THIS SOFTWARE IS PROVIDED BY INNOSYS CORP. ``AS IS'' AND ANY EXPRESS OR
	IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
	OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
	NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
	INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
	(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
	SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
	CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
	LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
	OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
	SUCH DAMAGE.    

	Revisions:

	2003feb14		add setTxMode/txMode  and cancelRxXoff to portControl
	2003mar21		change name of PARITY_0/1 to add MARK/SPACE
*/

#ifndef	__USA90MSG__
#define	__USA90MSG__

struct keyspan_usa90_portControlMessage
{
	/*
		there are three types of "commands" sent in the control message:

		1.	configuration changes which must be requested by setting
			the corresponding "set" flag (and should only be requested
			when necessary, to reduce overhead on the device):
	*/

	u8	setClocking,	// host requests baud rate be set
		baudLo,			// host does baud divisor calculation
		baudHi,			// host does baud divisor calculation 
		
		setLcr,			// host requests lcr be set
		lcr,			// use PARITY, STOPBITS, DATABITS below
		
		setRxMode,		// set receive mode
		rxMode,			// RXMODE_DMA or RXMODE_BYHAND

		setTxMode,		// set transmit mode
		txMode,			// TXMODE_DMA or TXMODE_BYHAND

		setTxFlowControl,	// host requests tx flow control be set
		txFlowControl	,	// use TX_FLOW... bits below
		setRxFlowControl,	// host requests rx flow control be set
		rxFlowControl,	// use RX_FLOW... bits below
		sendXoff,		// host requests XOFF transmitted immediately
		sendXon,		// host requests XON char transmitted
		xonChar,		// specified in current character format
		xoffChar,		// specified in current character format

		sendChar,		// host requests char transmitted immediately
		txChar,			// character to send

		setRts,			// host requests RTS output be set
		rts,			// 1=on, 0=off
		setDtr, 		// host requests DTR output be set
		dtr;			// 1=on, 0=off

	
	/*
		2.	configuration data which is simply used as is 
			and must be specified correctly in every host message.
	*/

	u8	rxForwardingLength,  // forward when this number of chars available
		rxForwardingTimeout, // (1-31 in ms)
		txAckSetting;	   // 0=don't ack, 1=normal, 2-255 TBD...
	/*
		3.	Firmware states which cause actions if they change					
		and must be specified correctly in every host message.
	*/

	u8	portEnabled,	// 0=disabled, 1=enabled
		txFlush,		// 0=normal, 1=toss outbound data
		txBreak,		// 0=break off, 1=break on
		loopbackMode;	// 0=no loopback, 1=loopback enabled

	/*
		4.	commands which are flags only; these are processed in order
			(so that, e.g., if rxFlush and rxForward flags are set, the
			port will have no data to forward); any non-zero value 
			is respected
	*/

	u8	rxFlush,		// toss inbound data
		rxForward,		// forward all inbound data, NOW (as if fwdLen==1)
		cancelRxXoff,	// cancel any receive XOFF state (_txXoff)
		returnStatus;	// return current status NOW
};

// defines for bits in lcr
#define		USA_DATABITS_5		0x00
#define		USA_DATABITS_6		0x01
#define		USA_DATABITS_7		0x02
#define		USA_DATABITS_8		0x03
#define		STOPBITS_5678_1		0x00	// 1 stop bit for all byte sizes
#define		STOPBITS_5_1p5		0x04	// 1.5 stop bits for 5-bit byte
#define		STOPBITS_678_2		0x04	// 2 stop bits for 6-8 bit byte
#define		USA_PARITY_NONE		0x00
#define		USA_PARITY_ODD		0x08
#define		USA_PARITY_EVEN		0x18
#define		PARITY_MARK_1  		0x28   	// force parity MARK
#define		PARITY_SPACE_0 		0x38	// force parity SPACE

#define		TXFLOW_CTS			0x04	
#define		TXFLOW_DSR			0x08
#define		TXFLOW_XOFF			0x01	
#define		TXFLOW_XOFF_ANY		0x02	
#define		TXFLOW_XOFF_BITS	(TXFLOW_XOFF | TXFLOW_XOFF_ANY)

#define		RXFLOW_XOFF			0x10	
#define		RXFLOW_RTS			0x20	
#define		RXFLOW_DTR			0x40
#define		RXFLOW_DSR_SENSITIVITY	0x80

#define		RXMODE_BYHAND		0x00	
#define		RXMODE_DMA			0x02	

#define		TXMODE_BYHAND		0x00	
#define		TXMODE_DMA			0x02	


// all things called "StatusMessage" are sent on the status endpoint

struct keyspan_usa90_portStatusMessage	
{
	u8	msr,			// reports the actual MSR register
		cts,			// reports CTS pin
		dcd,			// reports DCD pin
		dsr,			// reports DSR pin
		ri,				// reports RI pin
		_txXoff,		// port is in XOFF state (we received XOFF)
		rxBreak,		// reports break state
		rxOverrun,		// count of overrun errors (since last reported)
		rxParity,		// count of parity errors (since last reported)
		rxFrame,		// count of frame errors (since last reported)
		portState,		// PORTSTATE_xxx bits (useful for debugging)
		messageAck,		// message acknowledgement
		charAck,		// character acknowledgement
		controlResponse;	// (value = returnStatus) a control message has been processed 
};

// bits in RX data message when STAT byte is included

#define	RXERROR_OVERRUN		0x02
#define	RXERROR_PARITY		0x04
#define	RXERROR_FRAMING		0x08
#define	RXERROR_BREAK		0x10

#define	PORTSTATE_ENABLED	0x80
#define	PORTSTATE_TXFLUSH	0x01
#define	PORTSTATE_TXBREAK	0x02
#define	PORTSTATE_LOOPBACK 	0x04

// MSR bits

#define USA_MSR_dCTS	  		0x01		// CTS has changed since last report	
#define USA_MSR_dDSR	  		0x02
#define USA_MSR_dRI			0x04
#define USA_MSR_dDCD	  		0x08

#define USA_MSR_CTS			0x10	  	// current state of CTS
#define USA_MSR_DSR			0x20
#define USA_USA_MSR_RI			0x40
#define MSR_DCD				0x80

// ie: the maximum length of an endpoint buffer
#define		MAX_DATA_LEN			64

#endif

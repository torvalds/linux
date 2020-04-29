/* SPDX-License-Identifier: BSD-3-Clause */
/*
	usa26msg.h

	Copyright (C) 1998-2000 InnoSys Incorporated.  All Rights Reserved
	This file is available under a BSD-style copyright

	Keyspan USB Async Message Formats for the USA28X

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are
	met:

	1. Redistributions of source code must retain this licence text
   	without modification, this list of conditions, and the following
   	disclaimer.  The following copyright notice must appear immediately at
   	the beginning of all source files:

        	Copyright (C) 1998-2000 InnoSys Incorporated.  All Rights Reserved

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

	Third revision: USA28X version (aka USA26)

	Buffer formats for RX/TX data messages are not defined by
	a structure, but are described here:

	USB OUT (host -> USAxx, transmit) messages contain a 
	REQUEST_ACK indicator (set to 0xff to request an ACK at the 
	completion of transmit; 0x00 otherwise), followed by data:

		RQSTACK DAT DAT DAT ...

	with a total data length of 63.

	USB IN (USAxx -> host, receive) messages begin with a status
	byte in which the 0x80 bit is either:

		(a)	0x80 bit clear
			indicates that the bytes following it are all data
			bytes:

				STAT DATA DATA DATA DATA DATA ...

			for a total of up to 63 DATA bytes,

	or:

		(b)	0x80 bit set
			indicates that the bytes following alternate data and
			status bytes:

				STAT DATA STAT DATA STAT DATA STAT DATA ...

			for a total of up to 32 DATA bytes.

	The valid bits in the STAT bytes are:

		OVERRUN	0x02
		PARITY	0x04
		FRAMING	0x08
		BREAK	0x10

	Notes:

	(1) The OVERRUN bit can appear in either (a) or (b) format
		messages, but the but the PARITY/FRAMING/BREAK bits
		only appear in (b) format messages.
	(2) For the host to determine the exact point at which the
		overrun occurred (to identify the point in the data
		stream at which the data was lost), it needs to count
		128 characters, starting at the first character of the
		message in which OVERRUN was reported; the lost character(s)
		would have been received between the 128th and 129th
		characters.
	(3)	An RX data message in which the first byte has 0x80 clear
		serves as a "break off" indicator.

	revision history:

	1999feb10	add reportHskiaChanges to allow us to ignore them
	1999feb10	add txAckThreshold for fast+loose throughput enhancement
	1999mar30	beef up support for RX error reporting
	1999apr14	add resetDataToggle to control message
	2000jan04	merge with usa17msg.h
	2000jun01	add extended BSD-style copyright text
	2001jul05	change message format to improve OVERRUN case

	Note on shared names:

	In the case of fields which have been merged between the USA17
	and USA26 definitions, the USA26 definition is the first part
	of the name and the USA17 definition is the second part of the
	name; both meanings are described below.
*/

#ifndef	__USA26MSG__
#define	__USA26MSG__


struct keyspan_usa26_portControlMessage
{
	/*
		there are three types of "commands" sent in the control message:

		1.	configuration changes which must be requested by setting
			the corresponding "set" flag (and should only be requested
			when necessary, to reduce overhead on the USA26):
	*/
	u8	setClocking,	// BOTH: host requests baud rate be set
		baudLo,		// BOTH: host does baud divisor calculation
		baudHi,		// BOTH: baudHi is only used for first port (gives lower rates)
		externalClock_txClocking,
					// USA26: 0=internal, other=external
					// USA17: 0=internal, other=external/RI
		rxClocking,		// USA17: 0=internal, 1=external/RI, other=external/DSR


		setLcr,			// BOTH: host requests lcr be set
		lcr,			// BOTH: use PARITY, STOPBITS, DATABITS below

		setFlowControl,		// BOTH: host requests flow control be set
		ctsFlowControl,		// BOTH: 1=use CTS flow control, 0=don't
		xonFlowControl,		// BOTH: 1=use XON/XOFF flow control, 0=don't
		xonChar,		// BOTH: specified in current character format
		xoffChar,		// BOTH: specified in current character format

		setTxTriState_setRts,
					// USA26: host requests TX tri-state be set
					// USA17: host requests RTS output be set
		txTriState_rts,		// BOTH: 1=active (normal), 0=tristate (off)

		setHskoa_setDtr,
					// USA26: host requests HSKOA output be set
					// USA17: host requests DTR output be set
		hskoa_dtr,		// BOTH: 1=on, 0=off

		setPrescaler,		// USA26: host requests prescalar be set (default: 13)
		prescaler;		// BOTH: specified as N/8; values 8-ff are valid
					// must be set any time internal baud rate is set;
					// must not be set when external clocking is used
					// note: in USA17, prescaler is applied whenever
					// setClocking is requested

	/*
		3.	configuration data which is simply used as is (no overhead,
			but must be specified correctly in every host message).
	*/
	u8	forwardingLength,  // BOTH: forward when this number of chars available
		reportHskiaChanges_dsrFlowControl,
						// USA26: 1=normal; 0=ignore external clock
						// USA17: 1=use DSR flow control, 0=don't
		txAckThreshold,	// BOTH: 0=not allowed, 1=normal, 2-255 deliver ACK faster
		loopbackMode;	// BOTH: 0=no loopback, 1=loopback enabled

	/*
		4.	commands which are flags only; these are processed in order
			(so that, e.g., if both _txOn and _txOff flags are set, the
			port ends in a TX_OFF state); any non-zero value is respected
	*/
	u8	_txOn,			// BOTH: enable transmitting (and continue if there's data)
		_txOff,			// BOTH: stop transmitting
		txFlush,		// BOTH: toss outbound data
		txBreak,		// BOTH: turn on break (cleared by _txOn)
		rxOn,			// BOTH: turn on receiver
		rxOff,			// BOTH: turn off receiver
		rxFlush,		// BOTH: toss inbound data
		rxForward,		// BOTH: forward all inbound data, NOW (as if fwdLen==1)
		returnStatus,	// BOTH: return current status (even if it hasn't changed)
		resetDataToggle;// BOTH: reset data toggle state to DATA0
	
};

// defines for bits in lcr
#define	USA_DATABITS_5		0x00
#define	USA_DATABITS_6		0x01
#define	USA_DATABITS_7		0x02
#define	USA_DATABITS_8		0x03
#define	STOPBITS_5678_1	0x00	// 1 stop bit for all byte sizes
#define	STOPBITS_5_1p5	0x04	// 1.5 stop bits for 5-bit byte
#define	STOPBITS_678_2	0x04	// 2 stop bits for 6/7/8-bit byte
#define	USA_PARITY_NONE		0x00
#define	USA_PARITY_ODD		0x08
#define	USA_PARITY_EVEN		0x18
#define	PARITY_1		0x28
#define	PARITY_0		0x38

// all things called "StatusMessage" are sent on the status endpoint

struct keyspan_usa26_portStatusMessage	// one for each port
{
	u8	port,			// BOTH: 0=first, 1=second, other=see below
		hskia_cts,		// USA26: reports HSKIA pin
						// USA17: reports CTS pin
		gpia_dcd,		// USA26: reports GPIA pin
						// USA17: reports DCD pin
		dsr,			// USA17: reports DSR pin
		ri,				// USA17: reports RI pin
		_txOff,			// port has been disabled (by host)
		_txXoff,		// port is in XOFF state (either host or RX XOFF)
		rxEnabled,		// as configured by rxOn/rxOff 1=on, 0=off
		controlResponse;// 1=a control message has been processed
};

// bits in RX data message when STAT byte is included
#define	RXERROR_OVERRUN	0x02
#define	RXERROR_PARITY	0x04
#define	RXERROR_FRAMING	0x08
#define	RXERROR_BREAK	0x10

struct keyspan_usa26_globalControlMessage
{
	u8	sendGlobalStatus,	// 2=request for two status responses
		resetStatusToggle,	// 1=reset global status toggle
		resetStatusCount;	// a cycling value
};

struct keyspan_usa26_globalStatusMessage
{
	u8	port,				// 3
		sendGlobalStatus,	// from request, decremented
		resetStatusCount;	// as in request
};

struct keyspan_usa26_globalDebugMessage
{
	u8	port,				// 2
		a,
		b,
		c,
		d;
};

// ie: the maximum length of an EZUSB endpoint buffer
#define	MAX_DATA_LEN			64

// update status approx. 60 times a second (16.6666 ms)
#define	STATUS_UPDATE_INTERVAL	16

// status rationing tuning value (each port gets checked each n ms)
#define	STATUS_RATION	10

#endif



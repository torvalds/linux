/*
	usa67msg.h

	Copyright (c) 1998-2007 InnoSys Incorporated.  All Rights Reserved
	This file is available under a BSD-style copyright

	Keyspan USB Async Firmware to run on Anchor FX1

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are
	met:

	1. Redistributions of source code must retain this licence text
   	without modification, this list of conditions, and the following
   	disclaimer.  The following copyright notice must appear immediately at
   	the beginning of all source files:

        	Copyright (c) 1998-2007 InnoSys Incorporated.  All Rights Reserved

        	This file is available under a BSD-style copyright

	2. Redistributions in binary form must reproduce the above copyright
   	notice, this list of conditions and the following disclaimer in the
   	documentation and/or other materials provided with the distribution.

	3. The name of InnoSys Incorprated may not be used to endorse or promote
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

	Fourth revision: This message format supports the USA28XG

	Buffer formats for RX/TX data messages are not defined by
	a structure, but are described here:

	USB OUT (host -> USAxx, transmit) messages contain a
	REQUEST_ACK indicator (set to 0xff to request an ACK at the
	completion of transmit; 0x00 otherwise), followed by data:

		RQSTACK DAT DAT DAT ...

	with a total data length of up to 63.

	USB IN (USAxx -> host, receive) messages begin with a status
	byte in which the 0x80 bit is either:

		(a)	0x80 bit clear
			indicates that the bytes following it are all data
			bytes:

				STAT DATA DATA DATA DATA DATA ...

			for a total of up to 63 DATA bytes,

	or:

		(b)	0x80 bit set
			indiates that the bytes following alternate data and
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
	2002jun05	update copyright date, improve comments
	2006feb06	modify for FX1 chip

*/

#ifndef	__USA67MSG__
#define	__USA67MSG__


// all things called "ControlMessage" are sent on the 'control' endpoint

typedef struct keyspan_usa67_portControlMessage
{
	u8	port;		// 0 or 1 (selects port)
	/*
		there are three types of "commands" sent in the control message:

		1.	configuration changes which must be requested by setting
			the corresponding "set" flag (and should only be requested
			when necessary, to reduce overhead on the device):
	*/
	u8	setClocking,	// host requests baud rate be set
		baudLo,			// host does baud divisor calculation
		baudHi,			// baudHi is only used for first port (gives lower rates)
		externalClock_txClocking,
						// 0=internal, other=external

		setLcr,			// host requests lcr be set
		lcr,			// use PARITY, STOPBITS, DATABITS below

		setFlowControl,	// host requests flow control be set
		ctsFlowControl,	// 1=use CTS flow control, 0=don't
		xonFlowControl,	// 1=use XON/XOFF flow control, 0=don't
		xonChar,		// specified in current character format
		xoffChar,		// specified in current character format

		setTxTriState_setRts,
						// host requests TX tri-state be set
		txTriState_rts,	// 1=active (normal), 0=tristate (off)

		setHskoa_setDtr,
						// host requests HSKOA output be set
		hskoa_dtr,		// 1=on, 0=off

		setPrescaler,	// host requests prescalar be set (default: 13)
		prescaler;		// specified as N/8; values 8-ff are valid
						// must be set any time internal baud rate is set;
						// must not be set when external clocking is used

	/*
		3.	configuration data which is simply used as is (no overhead,
			but must be specified correctly in every host message).
	*/
	u8	forwardingLength,  // forward when this number of chars available
		reportHskiaChanges_dsrFlowControl,
						// 1=normal; 0=ignore external clock
						// 1=use DSR flow control, 0=don't
		txAckThreshold,	// 0=not allowed, 1=normal, 2-255 deliver ACK faster
		loopbackMode;	// 0=no loopback, 1=loopback enabled

	/*
		4.	commands which are flags only; these are processed in order
			(so that, e.g., if both _txOn and _txOff flags are set, the
			port ends in a TX_OFF state); any non-zero value is respected
	*/
	u8	_txOn,			// enable transmitting (and continue if there's data)
		_txOff,			// stop transmitting
		txFlush,		// toss outbound data
		txBreak,		// turn on break (cleared by _txOn)
		rxOn,			// turn on receiver
		rxOff,			// turn off receiver
		rxFlush,		// toss inbound data
		rxForward,		// forward all inbound data, NOW (as if fwdLen==1)
		returnStatus,	// return current status (even if it hasn't changed)
		resetDataToggle;// reset data toggle state to DATA0

} keyspan_usa67_portControlMessage;

// defines for bits in lcr
#define	USA_DATABITS_5		0x00
#define	USA_DATABITS_6		0x01
#define	USA_DATABITS_7		0x02
#define	USA_DATABITS_8		0x03
#define	STOPBITS_5678_1		0x00	// 1 stop bit for all byte sizes
#define	STOPBITS_5_1p5		0x04	// 1.5 stop bits for 5-bit byte
#define	STOPBITS_678_2		0x04	// 2 stop bits for 6/7/8-bit byte
#define	USA_PARITY_NONE		0x00
#define	USA_PARITY_ODD		0x08
#define	USA_PARITY_EVEN		0x18
#define	PARITY_1			0x28
#define	PARITY_0			0x38

// all things called "StatusMessage" are sent on the status endpoint

typedef struct keyspan_usa67_portStatusMessage	// one for each port
{
	u8	port,			// 0=first, 1=second, other=see below
		hskia_cts,		// reports HSKIA pin
		gpia_dcd,		// reports GPIA pin
		_txOff,			// port has been disabled (by host)
		_txXoff,		// port is in XOFF state (either host or RX XOFF)
		txAck,			// indicates a TX message acknowledgement
		rxEnabled,		// as configured by rxOn/rxOff 1=on, 0=off
		controlResponse;// 1=a control message has been processed
} keyspan_usa67_portStatusMessage;

// bits in RX data message when STAT byte is included
#define	RXERROR_OVERRUN	0x02
#define	RXERROR_PARITY	0x04
#define	RXERROR_FRAMING	0x08
#define	RXERROR_BREAK	0x10

typedef struct keyspan_usa67_globalControlMessage
{
	u8	port,	 			// 3
		sendGlobalStatus,	// 2=request for two status responses
		resetStatusToggle,	// 1=reset global status toggle
		resetStatusCount;	// a cycling value
} keyspan_usa67_globalControlMessage;

typedef struct keyspan_usa67_globalStatusMessage
{
	u8	port,				// 3
		sendGlobalStatus,	// from request, decremented
		resetStatusCount;	// as in request
} keyspan_usa67_globalStatusMessage;

typedef struct keyspan_usa67_globalDebugMessage
{
	u8	port,				// 2
		a,
		b,
		c,
		d;
} keyspan_usa67_globalDebugMessage;

// ie: the maximum length of an FX1 endpoint buffer
#define	MAX_DATA_LEN			64

// update status approx. 60 times a second (16.6666 ms)
#define	STATUS_UPDATE_INTERVAL	16

// status rationing tuning value (each port gets checked each n ms)
#define	STATUS_RATION	10

#endif



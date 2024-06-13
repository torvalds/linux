/* SPDX-License-Identifier: BSD-3-Clause */
/*
	usa49msg.h

	Copyright (C) 1998-2000 InnoSys Incorporated.  All Rights Reserved
	This file is available under a BSD-style copyright

	Keyspan USB Async Message Formats for the USA49W

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

	4th revision: USA49W version

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
	(4)	a control message specifying disablePort will be answered
		with a status message, but no further status will be sent
		until a control messages with enablePort is sent

	revision history:

	1999feb10	add reportHskiaChanges to allow us to ignore them
	1999feb10	add txAckThreshold for fast+loose throughput enhancement
	1999mar30	beef up support for RX error reporting
	1999apr14	add resetDataToggle to control message
	2000jan04	merge with usa17msg.h
	2000mar08	clone from usa26msg.h -> usa49msg.h
	2000mar09	change to support 4 ports
	2000may03	change external clocking to match USA-49W hardware
	2000jun01	add extended BSD-style copyright text
	2001jul05	change message format to improve OVERRUN case
*/

#ifndef	__USA49MSG__
#define	__USA49MSG__


/*
	Host->device messages sent on the global control endpoint:

	portNumber	message
	----------	--------------------
	0,1,2,3		portControlMessage
	0x80		globalControlMessage
*/

struct keyspan_usa49_portControlMessage
{
	/*
		0.	0/1/2/3 	port control message follows
			0x80 set	non-port control message follows
	*/
	u8	portNumber,

	/*
		there are three types of "commands" sent in the control message:

		1.	configuration changes which must be requested by setting
			the corresponding "set" flag (and should only be requested
			when necessary, to reduce overhead on the USA26):
	*/
		setClocking,	// host requests baud rate be set
		baudLo,			// host does baud divisor calculation
		baudHi,			// baudHi is only used for first port (gives lower rates)
		prescaler,		// specified as N/8; values 8-ff are valid
						// must be set any time internal baud rate is set;
		txClocking,		// 0=internal, 1=external/DSR
		rxClocking,		// 0=internal, 1=external/DSR

		setLcr,			// host requests lcr be set
		lcr,			// use PARITY, STOPBITS, DATABITS below

		setFlowControl,	// host requests flow control be set
		ctsFlowControl,	// 1=use CTS flow control, 0=don't
		xonFlowControl,	// 1=use XON/XOFF flow control, 0=don't
		xonChar,		// specified in current character format
		xoffChar,		// specified in current character format

		setRts,			// host requests RTS output be set
		rts,			// 1=active, 0=inactive

		setDtr,			// host requests DTR output be set
		dtr;			// 1=on, 0=off


	/*
		3.	configuration data which is simply used as is (no overhead,
			but must be specified correctly in every host message).
	*/
	u8	forwardingLength,  // forward when this number of chars available
		dsrFlowControl,	// 1=use DSR flow control, 0=don't
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
		resetDataToggle,// reset data toggle state to DATA0
		enablePort,		// start servicing port (move data, check status)
		disablePort;	// stop servicing port (does implicit tx/rx flush/off)
	
};

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

/*
	during normal operation, status messages are returned 
	to the host whenever the board detects changes.  In some
	circumstances (e.g. Windows), status messages from the
	device cause problems; to shut them off, the host issues
	a control message with the disableStatusMessages flags
	set (to any non-zero value).  The device will respond to
	this message, and then suppress further status messages;
	it will resume sending status messages any time the host
	sends any control message (either global or port-specific).
*/

struct keyspan_usa49_globalControlMessage
{
	u8	portNumber,			// 0x80
		sendGlobalStatus,	// 1/2=number of status responses requested
		resetStatusToggle,	// 1=reset global status toggle
		resetStatusCount,	// a cycling value
		remoteWakeupEnable,		// 0x10=P1, 0x20=P2, 0x40=P3, 0x80=P4
		disableStatusMessages;	// 1=send no status until host talks
};

/*
	Device->host messages send on the global status endpoint

	portNumber			message
	----------			--------------------
	0x00,0x01,0x02,0x03	portStatusMessage
	0x80				globalStatusMessage
	0x81				globalDebugMessage
*/

struct keyspan_usa49_portStatusMessage	// one for each port
{
	u8	portNumber,		// 0,1,2,3
		cts,			// reports CTS pin
		dcd,			// reports DCD pin
		dsr,			// reports DSR pin
		ri,				// reports RI pin
		_txOff,			// transmit has been disabled (by host)
		_txXoff,		// transmit is in XOFF state (either host or RX XOFF)
		rxEnabled,		// as configured by rxOn/rxOff 1=on, 0=off
		controlResponse,// 1=a control message has been processed
		txAck,			// ACK (data TX complete)
		rs232valid;		// RS-232 signal valid
};

// bits in RX data message when STAT byte is included
#define	RXERROR_OVERRUN	0x02
#define	RXERROR_PARITY	0x04
#define	RXERROR_FRAMING	0x08
#define	RXERROR_BREAK	0x10

struct keyspan_usa49_globalStatusMessage
{
	u8	portNumber,			// 0x80=globalStatusMessage
		sendGlobalStatus,	// from request, decremented
		resetStatusCount;	// as in request
};

struct keyspan_usa49_globalDebugMessage
{
	u8	portNumber,			// 0x81=globalDebugMessage
		n,					// typically a count/status byte
		b;					// typically a data byte
};

// ie: the maximum length of an EZUSB endpoint buffer
#define	MAX_DATA_LEN			64

// update status approx. 60 times a second (16.6666 ms)
#define	STATUS_UPDATE_INTERVAL	16

// status rationing tuning value (each port gets checked each n ms)
#define	STATUS_RATION	10

#endif

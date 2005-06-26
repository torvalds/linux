/*******************************************************************************
*
*   (c) 1999 by Computone Corporation
*
********************************************************************************
*
*
*   PACKAGE:     Linux tty Device Driver for IntelliPort II family of multiport
*                serial I/O controllers.
*
*   DESCRIPTION: Definitions and support for In-line and Bypass commands.
*                Applicable only when the standard loadware is active.
*
*******************************************************************************/
//------------------------------------------------------------------------------
// Revision History:
//
// 10 October 1991   MAG First Draft
//  7 November 1991  MAG Reflects some new commands
// 20 February 1992  MAG CMD_HOTACK corrected: no argument.
// 24 February 1992  MAG Support added for new commands for 1.4.x loadware.
// 11 March 1992     MAG Additional commands.
// 16 March 1992     MAG Additional commands.
// 30 March 1992     MAG Additional command: CMD_DSS_NOW
// 18 May   1992     MAG Changed CMD_OPOST
//
//------------------------------------------------------------------------------
#ifndef I2CMD_H      // To prevent multiple includes
#define I2CMD_H   1

#include "ip2types.h"

// This module is designed to provide a uniform method of sending commands to
// the board through command packets. The difficulty is, some commands take
// parameters, others do not. Furthermore, it is often useful to send several
// commands to the same channel as part of the same packet. (See also i2pack.h.)
//
// This module is designed so that the caller should not be responsible for
// remembering the exact syntax of each command, or at least so that the
// compiler could check things somewhat. I'll explain as we go...
//
// First, a structure which can embody the syntax of each type of command.
//
typedef struct _cmdSyntax
{
	UCHAR length;   // Number of bytes in the command
	UCHAR flags;    // Information about the command (see below)

	// The command and its parameters, which may be of arbitrary length. Don't
	// worry yet how the parameters will be initialized; macros later take care
	// of it. Also, don't worry about the arbitrary length issue; this structure
	// is never used to allocate space (see i2cmd.c).
	UCHAR cmd[2];
} cmdSyntax, *cmdSyntaxPtr;

// Bit assignments for flags

#define INL 1           // Set if suitable for inline commands
#define BYP 2           // Set if suitable for bypass commands
#define BTH (INL|BYP)   // suitable for either!
#define END 4           // Set if this must be the last command in a block
#define VIP 8           // Set if this command is special in some way and really
						// should only be sent from the library-level and not
						// directly from user-level
#define VAR 0x10        // This command is of variable length!

// Declarations for the global arrays used to bear the commands and their
// arguments.
//
// Note: Since these are globals and the arguments might change, it is important
// that the library routine COPY these into buffers from whence they would be
// sent, rather than merely storing the pointers. In multi-threaded
// environments, important that the copy should obtain before any context switch
// is allowed. Also, for parameterized commands, DO NOT ISSUE THE SAME COMMAND
// MORE THAN ONCE WITH THE SAME PARAMETERS in the same call.
//
static UCHAR ct02[];
static UCHAR ct03[];
static UCHAR ct04[];
static UCHAR ct05[];
static UCHAR ct06[];
static UCHAR ct07[];
static UCHAR ct08[];
static UCHAR ct09[];
static UCHAR ct10[];
static UCHAR ct11[];
static UCHAR ct12[];
static UCHAR ct13[];
static UCHAR ct14[];
static UCHAR ct15[];
static UCHAR ct16[];
static UCHAR ct17[];
static UCHAR ct18[];
static UCHAR ct19[];
static UCHAR ct20[];
static UCHAR ct21[];
static UCHAR ct22[];
static UCHAR ct23[];
static UCHAR ct24[];
static UCHAR ct25[];
static UCHAR ct26[];
static UCHAR ct27[];
static UCHAR ct28[];
static UCHAR ct29[];
static UCHAR ct30[];
static UCHAR ct31[];
static UCHAR ct32[];
static UCHAR ct33[];
static UCHAR ct34[];
static UCHAR ct35[];
static UCHAR ct36[];
static UCHAR ct36a[];
static UCHAR ct41[];
static UCHAR ct42[];
static UCHAR ct43[];
static UCHAR ct44[];
static UCHAR ct45[];
static UCHAR ct46[];
static UCHAR ct48[];
static UCHAR ct49[];
static UCHAR ct50[];
static UCHAR ct51[];
static UCHAR ct52[];
static UCHAR ct56[];
static UCHAR ct57[];
static UCHAR ct58[];
static UCHAR ct59[];
static UCHAR ct60[];
static UCHAR ct61[];
static UCHAR ct62[];
static UCHAR ct63[];
static UCHAR ct64[];
static UCHAR ct65[];
static UCHAR ct66[];
static UCHAR ct67[];
static UCHAR ct68[];
static UCHAR ct69[];
static UCHAR ct70[];
static UCHAR ct71[];
static UCHAR ct72[];
static UCHAR ct73[];
static UCHAR ct74[];
static UCHAR ct75[];
static UCHAR ct76[];
static UCHAR ct77[];
static UCHAR ct78[];
static UCHAR ct79[];
static UCHAR ct80[];
static UCHAR ct81[];
static UCHAR ct82[];
static UCHAR ct83[];
static UCHAR ct84[];
static UCHAR ct85[];
static UCHAR ct86[];
static UCHAR ct87[];
static UCHAR ct88[];
static UCHAR ct89[];
static UCHAR ct90[];
static UCHAR ct91[];
static UCHAR cc01[];
static UCHAR cc02[];

// Now, refer to i2cmd.c, and see the character arrays defined there. They are
// cast here to cmdSyntaxPtr.
//
// There are library functions for issuing bypass or inline commands. These
// functions take one or more arguments of the type cmdSyntaxPtr. The routine
// then can figure out how long each command is supposed to be and easily add it
// to the list.
//
// For ease of use, we define manifests which return pointers to appropriate
// cmdSyntaxPtr things. But some commands also take arguments. If a single
// argument is used, we define a macro which performs the single assignment and
// (through the expedient of a comma expression) references the appropriate
// pointer. For commands requiring several arguments, we actually define a
// function to perform the assignments.

#define CMD_DTRUP	(cmdSyntaxPtr)(ct02)	// Raise DTR
#define CMD_DTRDN	(cmdSyntaxPtr)(ct03)	// Lower DTR
#define CMD_RTSUP	(cmdSyntaxPtr)(ct04)	// Raise RTS
#define CMD_RTSDN	(cmdSyntaxPtr)(ct05)	// Lower RTS
#define CMD_STARTFL	(cmdSyntaxPtr)(ct06)	// Start Flushing Data

#define CMD_DTRRTS_UP (cmdSyntaxPtr)(cc01)	// Raise DTR and RTS
#define CMD_DTRRTS_DN (cmdSyntaxPtr)(cc02)	// Lower DTR and RTS

// Set Baud Rate for transmit and receive
#define CMD_SETBAUD(arg) \
	(((cmdSyntaxPtr)(ct07))->cmd[1] = (arg),(cmdSyntaxPtr)(ct07))

#define CBR_50       1
#define CBR_75       2
#define CBR_110      3
#define CBR_134      4
#define CBR_150      5
#define CBR_200      6
#define CBR_300      7
#define CBR_600      8
#define CBR_1200     9
#define CBR_1800     10
#define CBR_2400     11
#define CBR_4800     12
#define CBR_9600     13
#define CBR_19200    14
#define CBR_38400    15
#define CBR_2000     16
#define CBR_3600     17
#define CBR_7200     18
#define CBR_56000    19
#define CBR_57600    20
#define CBR_64000    21
#define CBR_76800    22
#define CBR_115200   23
#define CBR_C1       24    // Custom baud rate 1
#define CBR_C2       25    // Custom baud rate 2
#define CBR_153600   26
#define CBR_230400   27
#define CBR_307200   28
#define CBR_460800   29
#define CBR_921600   30

// Set Character size
//
#define CMD_SETBITS(arg) \
	(((cmdSyntaxPtr)(ct08))->cmd[1] = (arg),(cmdSyntaxPtr)(ct08))

#define CSZ_5  0
#define CSZ_6  1
#define CSZ_7  2
#define CSZ_8  3

// Set number of stop bits
//
#define CMD_SETSTOP(arg) \
	(((cmdSyntaxPtr)(ct09))->cmd[1] = (arg),(cmdSyntaxPtr)(ct09))

#define CST_1  0
#define CST_15 1  // 1.5 stop bits
#define CST_2  2

// Set parity option
//
#define CMD_SETPAR(arg) \
	(((cmdSyntaxPtr)(ct10))->cmd[1] = (arg),(cmdSyntaxPtr)(ct10))

#define CSP_NP 0  // no parity
#define CSP_OD 1  // odd parity
#define CSP_EV 2  // Even parity
#define CSP_SP 3  // Space parity
#define CSP_MK 4  // Mark parity

// Define xon char for transmitter flow control
//
#define CMD_DEF_IXON(arg) \
	(((cmdSyntaxPtr)(ct11))->cmd[1] = (arg),(cmdSyntaxPtr)(ct11))

// Define xoff char for transmitter flow control
//
#define CMD_DEF_IXOFF(arg) \
	(((cmdSyntaxPtr)(ct12))->cmd[1] = (arg),(cmdSyntaxPtr)(ct12))

#define CMD_STOPFL   (cmdSyntaxPtr)(ct13) // Stop Flushing data

// Acknowledge receipt of hotkey signal
//
#define CMD_HOTACK   (cmdSyntaxPtr)(ct14)

// Define irq level to use. Should actually be sent by library-level code, not
// directly from user...
//
#define CMDVALUE_IRQ 15 // For library use at initialization. Until this command
						// is sent, board processing doesn't really start.
#define CMD_SET_IRQ(arg) \
	(((cmdSyntaxPtr)(ct15))->cmd[1] = (arg),(cmdSyntaxPtr)(ct15))

#define CIR_POLL  0  // No IRQ - Poll
#define CIR_3     3  // IRQ 3
#define CIR_4     4  // IRQ 4
#define CIR_5     5  // IRQ 5
#define CIR_7     7  // IRQ 7
#define CIR_10    10 // IRQ 10
#define CIR_11    11 // IRQ 11
#define CIR_12    12 // IRQ 12
#define CIR_15    15 // IRQ 15

// Select transmit flow xon/xoff options
//
#define CMD_IXON_OPT(arg) \
	(((cmdSyntaxPtr)(ct16))->cmd[1] = (arg),(cmdSyntaxPtr)(ct16))

#define CIX_NONE  0  // Incoming Xon/Xoff characters not special
#define CIX_XON   1  // Xoff disable, Xon enable
#define CIX_XANY  2  // Xoff disable, any key enable

// Select receive flow xon/xoff options
//
#define CMD_OXON_OPT(arg) \
	(((cmdSyntaxPtr)(ct17))->cmd[1] = (arg),(cmdSyntaxPtr)(ct17))

#define COX_NONE  0  // Don't send Xon/Xoff
#define COX_XON   1  // Send xon/xoff to start/stop incoming data


#define CMD_CTS_REP  (cmdSyntaxPtr)(ct18) // Enable  CTS reporting
#define CMD_CTS_NREP (cmdSyntaxPtr)(ct19) // Disable CTS reporting

#define CMD_DCD_REP  (cmdSyntaxPtr)(ct20) // Enable  DCD reporting
#define CMD_DCD_NREP (cmdSyntaxPtr)(ct21) // Disable DCD reporting

#define CMD_DSR_REP  (cmdSyntaxPtr)(ct22) // Enable  DSR reporting
#define CMD_DSR_NREP (cmdSyntaxPtr)(ct23) // Disable DSR reporting

#define CMD_RI_REP   (cmdSyntaxPtr)(ct24) // Enable  RI  reporting
#define CMD_RI_NREP  (cmdSyntaxPtr)(ct25) // Disable RI  reporting

// Enable break reporting and select style
//
#define CMD_BRK_REP(arg) \
	(((cmdSyntaxPtr)(ct26))->cmd[1] = (arg),(cmdSyntaxPtr)(ct26))

#define CBK_STAT     0x00  // Report breaks as a status (exception,irq)
#define CBK_NULL     0x01  // Report breaks as a good null
#define CBK_STAT_SEQ 0x02  // Report breaks as a status AND as in-band character
                           //  sequence FFh, 01h, 10h
#define CBK_SEQ      0x03  // Report breaks as the in-band 
						   //sequence FFh, 01h, 10h ONLY.
#define CBK_FLSH     0x04  // if this bit set also flush input data
#define CBK_POSIX    0x08  // if this bit set report as FF,0,0 sequence
#define CBK_SINGLE   0x10  // if this bit set with CBK_SEQ or CBK_STAT_SEQ
						   //then reports single null instead of triple

#define CMD_BRK_NREP (cmdSyntaxPtr)(ct27) // Disable break reporting

// Specify maximum block size for received data
//
#define CMD_MAX_BLOCK(arg) \
	(((cmdSyntaxPtr)(ct28))->cmd[1] = (arg),(cmdSyntaxPtr)(ct28))

// -- COMMAND 29 is reserved --

#define CMD_CTSFL_ENAB  (cmdSyntaxPtr)(ct30) // Enable  CTS flow control
#define CMD_CTSFL_DSAB  (cmdSyntaxPtr)(ct31) // Disable CTS flow control
#define CMD_RTSFL_ENAB  (cmdSyntaxPtr)(ct32) // Enable  RTS flow control
#define CMD_RTSFL_DSAB  (cmdSyntaxPtr)(ct33) // Disable RTS flow control

// Specify istrip option
//
#define CMD_ISTRIP_OPT(arg) \
	(((cmdSyntaxPtr)(ct34))->cmd[1] = (arg),(cmdSyntaxPtr)(ct34))

#define CIS_NOSTRIP  0  // Strip characters to character size
#define CIS_STRIP    1  // Strip any 8-bit characters to 7 bits

// Send a break of arg milliseconds
//
#define CMD_SEND_BRK(arg) \
	(((cmdSyntaxPtr)(ct35))->cmd[1] = (arg),(cmdSyntaxPtr)(ct35))

// Set error reporting mode
//
#define CMD_SET_ERROR(arg) \
	(((cmdSyntaxPtr)(ct36))->cmd[1] = (arg),(cmdSyntaxPtr)(ct36))

#define CSE_ESTAT 0  // Report error in a status packet
#define CSE_NOREP 1  // Treat character as though it were good
#define CSE_DROP  2  // Discard the character
#define CSE_NULL  3  // Replace with a null
#define CSE_MARK  4  // Replace with a 3-character sequence (as Unix)

#define  CMD_SET_REPLACEMENT(arg,ch)   \
			(((cmdSyntaxPtr)(ct36a))->cmd[1] = (arg), \
			(((cmdSyntaxPtr)(ct36a))->cmd[2] = (ch),  \
			(cmdSyntaxPtr)(ct36a))

#define CSE_REPLACE  0x8	// Replace the errored character with the
							// replacement character defined here

#define CSE_STAT_REPLACE   0x18	// Replace the errored character with the
								// replacement character defined here AND
								// report the error as a status packet (as in
								// CSE_ESTAT).


// COMMAND 37, to send flow control packets, is handled only by low-level
// library code in response to data movement and shouldn't ever be sent by the
// user code. See i2pack.h and the body of i2lib.c for details.

// Enable on-board post-processing, using options given in oflag argument.
// Formerly, this command was automatically preceded by a CMD_OPOST_OFF command
// because the loadware does not permit sending back-to-back CMD_OPOST_ON
// commands without an intervening CMD_OPOST_OFF. BUT, WE LEARN 18 MAY 92, that
// CMD_OPOST_ON and CMD_OPOST_OFF must each be at the end of a packet (or in a
// solo packet). This means the caller must specify separately CMD_OPOST_OFF,
// CMD_OPOST_ON(parm) when he calls i2QueueCommands(). That function will ensure
// each gets a separate packet. Extra CMD_OPOST_OFF's are always ok.
//
#define CMD_OPOST_ON(oflag)   \
	(*(USHORT *)(((cmdSyntaxPtr)(ct39))->cmd[1]) = (oflag), \
		(cmdSyntaxPtr)(ct39))

#define CMD_OPOST_OFF   (cmdSyntaxPtr)(ct40) // Disable on-board post-proc

#define CMD_RESUME   (cmdSyntaxPtr)(ct41)	// Resume: behave as though an XON
											// were received;

// Set Transmit baud rate (see command 7 for arguments)
//
#define CMD_SETBAUD_TX(arg) \
	(((cmdSyntaxPtr)(ct42))->cmd[1] = (arg),(cmdSyntaxPtr)(ct42))

// Set Receive baud rate (see command 7 for arguments)
//
#define CMD_SETBAUD_RX(arg) \
	(((cmdSyntaxPtr)(ct43))->cmd[1] = (arg),(cmdSyntaxPtr)(ct43))

// Request interrupt from board each arg milliseconds. Interrupt will specify
// "received data", even though there may be no data present. If arg == 0,
// disables any such interrupts.
//
#define CMD_PING_REQ(arg) \
	(((cmdSyntaxPtr)(ct44))->cmd[1] = (arg),(cmdSyntaxPtr)(ct44))

#define CMD_HOT_ENAB (cmdSyntaxPtr)(ct45) // Enable Hot-key checking
#define CMD_HOT_DSAB (cmdSyntaxPtr)(ct46) // Disable Hot-key checking

#if 0
// COMMAND 47: Send Protocol info via Unix flags:
// iflag = Unix tty t_iflag
// cflag = Unix tty t_cflag
// lflag = Unix tty t_lflag
// See System V Unix/Xenix documentation for the meanings of the bit fields
// within these flags
//
#define CMD_UNIX_FLAGS(iflag,cflag,lflag) i2cmdUnixFlags(iflag,cflag,lflag)
#endif  /*  0  */

#define CMD_DSRFL_ENAB  (cmdSyntaxPtr)(ct48) // Enable  DSR receiver ctrl
#define CMD_DSRFL_DSAB  (cmdSyntaxPtr)(ct49) // Disable DSR receiver ctrl
#define CMD_DTRFL_ENAB  (cmdSyntaxPtr)(ct50) // Enable  DTR flow control
#define CMD_DTRFL_DSAB  (cmdSyntaxPtr)(ct51) // Disable DTR flow control
#define CMD_BAUD_RESET  (cmdSyntaxPtr)(ct52) // Reset baudrate table

// COMMAND 54: Define custom rate #1
// rate = (short) 1/10 of the desired baud rate
//
#define CMD_BAUD_DEF1(rate) i2cmdBaudDef(1,rate)

// COMMAND 55: Define custom rate #2
// rate = (short) 1/10 of the desired baud rate
//
#define CMD_BAUD_DEF2(rate) i2cmdBaudDef(2,rate)

// Pause arg hundredths of seconds. (Note, this is NOT milliseconds.)
//
#define CMD_PAUSE(arg) \
	(((cmdSyntaxPtr)(ct56))->cmd[1] = (arg),(cmdSyntaxPtr)(ct56))

#define CMD_SUSPEND     (cmdSyntaxPtr)(ct57) // Suspend output
#define CMD_UNSUSPEND   (cmdSyntaxPtr)(ct58) // Un-Suspend output

// Set parity-checking options
//
#define CMD_PARCHK(arg) \
	(((cmdSyntaxPtr)(ct59))->cmd[1] = (arg),(cmdSyntaxPtr)(ct59))

#define CPK_ENAB  0     // Enable parity checking on input
#define CPK_DSAB  1     // Disable parity checking on input

#define CMD_BMARK_REQ   (cmdSyntaxPtr)(ct60) // Bookmark request


// Enable/Disable internal loopback mode
//
#define CMD_INLOOP(arg) \
	(((cmdSyntaxPtr)(ct61))->cmd[1] = (arg),(cmdSyntaxPtr)(ct61))

#define CIN_DISABLE  0  // Normal operation (default)
#define CIN_ENABLE   1  // Internal (local) loopback
#define CIN_REMOTE   2  // Remote loopback

// Specify timeout for hotkeys: Delay will be (arg x 10) milliseconds, arg == 0
// --> no timeout: wait forever.
//
#define CMD_HOT_TIME(arg) \
	(((cmdSyntaxPtr)(ct62))->cmd[1] = (arg),(cmdSyntaxPtr)(ct62))


// Define (outgoing) xon for receive flow control
//
#define CMD_DEF_OXON(arg) \
	(((cmdSyntaxPtr)(ct63))->cmd[1] = (arg),(cmdSyntaxPtr)(ct63))

// Define (outgoing) xoff for receiver flow control
//
#define CMD_DEF_OXOFF(arg) \
	(((cmdSyntaxPtr)(ct64))->cmd[1] = (arg),(cmdSyntaxPtr)(ct64))

// Enable/Disable RTS on transmit (1/2 duplex-style)
//
#define CMD_RTS_XMIT(arg) \
	(((cmdSyntaxPtr)(ct65))->cmd[1] = (arg),(cmdSyntaxPtr)(ct65))

#define CHD_DISABLE  0
#define CHD_ENABLE   1

// Set high-water-mark level (debugging use only)
//
#define CMD_SETHIGHWAT(arg) \
	(((cmdSyntaxPtr)(ct66))->cmd[1] = (arg),(cmdSyntaxPtr)(ct66))

// Start flushing tagged data (tag = 0-14)
//
#define CMD_START_SELFL(tag) \
	(((cmdSyntaxPtr)(ct67))->cmd[1] = (tag),(cmdSyntaxPtr)(ct67))

// End flushing tagged data (tag = 0-14)
//
#define CMD_END_SELFL(tag) \
	(((cmdSyntaxPtr)(ct68))->cmd[1] = (tag),(cmdSyntaxPtr)(ct68))

#define CMD_HWFLOW_OFF  (cmdSyntaxPtr)(ct69) // Disable HW TX flow control
#define CMD_ODSRFL_ENAB (cmdSyntaxPtr)(ct70) // Enable DSR output f/c
#define CMD_ODSRFL_DSAB (cmdSyntaxPtr)(ct71) // Disable DSR output f/c
#define CMD_ODCDFL_ENAB (cmdSyntaxPtr)(ct72) // Enable DCD output f/c
#define CMD_ODCDFL_DSAB (cmdSyntaxPtr)(ct73) // Disable DCD output f/c

// Set transmit interrupt load level. Count should be an even value 2-12
//
#define CMD_LOADLEVEL(count) \
	(((cmdSyntaxPtr)(ct74))->cmd[1] = (count),(cmdSyntaxPtr)(ct74))

// If reporting DSS changes, map to character sequence FFh, 2, MSR
//
#define CMD_STATDATA(arg) \
	(((cmdSyntaxPtr)(ct75))->cmd[1] = (arg),(cmdSyntaxPtr)(ct75))

#define CSTD_DISABLE// Report DSS changes as status packets only (default)
#define CSTD_ENABLE	// Report DSS changes as in-band data sequence as well as
					// by status packet.

#define CMD_BREAK_ON    (cmdSyntaxPtr)(ct76)// Set break and stop xmit
#define CMD_BREAK_OFF   (cmdSyntaxPtr)(ct77)// End break and restart xmit
#define CMD_GETFC       (cmdSyntaxPtr)(ct78)// Request for flow control packet
											// from board.

// Transmit this character immediately
//
#define CMD_XMIT_NOW(ch) \
	(((cmdSyntaxPtr)(ct79))->cmd[1] = (ch),(cmdSyntaxPtr)(ct79))

// Set baud rate via "divisor latch"
//
#define CMD_DIVISOR_LATCH(which,value) \
			(((cmdSyntaxPtr)(ct80))->cmd[1] = (which), \
			*(USHORT *)(((cmdSyntaxPtr)(ct80))->cmd[2]) = (value), \
			(cmdSyntaxPtr)(ct80))

#define CDL_RX 1	// Set receiver rate
#define CDL_TX 2	// Set transmit rate
					// (CDL_TX | CDL_RX) Set both rates

// Request for special diagnostic status pkt from the board.
//
#define CMD_GET_STATUS (cmdSyntaxPtr)(ct81)

// Request time-stamped transmit character count packet.
//
#define CMD_GET_TXCNT  (cmdSyntaxPtr)(ct82)

// Request time-stamped receive character count packet.
//
#define CMD_GET_RXCNT  (cmdSyntaxPtr)(ct83)

// Request for box/board I.D. packet.
#define CMD_GET_BOXIDS (cmdSyntaxPtr)(ct84)

// Enable or disable multiple channels according to bit-mapped ushorts box 1-4
//
#define CMD_ENAB_MULT(enable, box1, box2, box3, box4)    \
			(((cmdSytaxPtr)(ct85))->cmd[1] = (enable),            \
			*(USHORT *)(((cmdSyntaxPtr)(ct85))->cmd[2]) = (box1), \
			*(USHORT *)(((cmdSyntaxPtr)(ct85))->cmd[4]) = (box2), \
			*(USHORT *)(((cmdSyntaxPtr)(ct85))->cmd[6]) = (box3), \
			*(USHORT *)(((cmdSyntaxPtr)(ct85))->cmd[8]) = (box4), \
			(cmdSyntaxPtr)(ct85))

#define CEM_DISABLE  0
#define CEM_ENABLE   1

// Enable or disable receiver or receiver interrupts (default both enabled)
//
#define CMD_RCV_ENABLE(ch) \
	(((cmdSyntaxPtr)(ct86))->cmd[1] = (ch),(cmdSyntaxPtr)(ct86))

#define CRE_OFF      0  // Disable the receiver
#define CRE_ON       1  // Enable the receiver
#define CRE_INTOFF   2  // Disable receiver interrupts (to loadware)
#define CRE_INTON    3  // Enable receiver interrupts (to loadware)

// Starts up a hardware test process, which runs transparently, and sends a
// STAT_HWFAIL packet in case a hardware failure is detected.
//
#define CMD_HW_TEST  (cmdSyntaxPtr)(ct87)

// Change receiver threshold and timeout value:
// Defaults: timeout = 20mS
// threshold count = 8 when DTRflow not in use,
// threshold count = 5 when DTRflow in use.
//
#define CMD_RCV_THRESHOLD(count,ms) \
			(((cmdSyntaxPtr)(ct88))->cmd[1] = (count), \
			((cmdSyntaxPtr)(ct88))->cmd[2] = (ms), \
			(cmdSyntaxPtr)(ct88))

// Makes the loadware report DSS signals for this channel immediately.
//
#define CMD_DSS_NOW (cmdSyntaxPtr)(ct89)
	
// Set the receive silo parameters 
// 	timeout is ms idle wait until delivery       (~VTIME)
// 	threshold is max characters cause interrupt  (~VMIN)
//
#define CMD_SET_SILO(timeout,threshold) \
			(((cmdSyntaxPtr)(ct90))->cmd[1] = (timeout), \
			((cmdSyntaxPtr)(ct90))->cmd[2]  = (threshold), \
			(cmdSyntaxPtr)(ct90))

// Set timed break in decisecond (1/10s)
//
#define CMD_LBREAK(ds) \
	(((cmdSyntaxPtr)(ct91))->cmd[1] = (ds),(cmdSyntaxPtr)(ct66))



#endif // I2CMD_H

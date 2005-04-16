/*******************************************************************************
*
*   (c) 1998 by Computone Corporation
*
********************************************************************************
*
*
*   PACKAGE:     Linux tty Device Driver for IntelliPort II family of multiport
*                serial I/O controllers.
*
*   DESCRIPTION: Definitions of the packets used to transfer data and commands
*                Host <--> Board. Information provided here is only applicable
*                when the standard loadware is active.
*
*******************************************************************************/
#ifndef I2PACK_H
#define I2PACK_H  1

//-----------------------------------------------
// Revision History:
//
// 10 October 1991   MAG First draft
// 24 February 1992  MAG Additions for 1.4.x loadware
// 11 March 1992     MAG New status packets
//
//-----------------------------------------------

//------------------------------------------------------------------------------
// Packet Formats:
//
// Information passes between the host and board through the FIFO in packets.
// These have headers which indicate the type of packet. Because the fifo data
// path may be 16-bits wide, the protocol is constrained such that each packet
// is always padded to an even byte count. (The lower-level interface routines
// -- i2ellis.c -- are designed to do this).
//
// The sender (be it host or board) must place some number of complete packets
// in the fifo, then place a message in the mailbox that packets are available.
// Placing such a message interrupts the "receiver" (be it board or host), who
// reads the mailbox message and determines that there are incoming packets
// ready. Since there are no partial packets, and the length of a packet is
// given in the header, the remainder of the packet can be read without checking
// for FIFO empty condition. The process is repeated, packet by packet, until
// the incoming FIFO is empty. Then the receiver uses the outbound mailbox to
// signal the board that it has read the data. Only then can the sender place
// additional data in the fifo.
//------------------------------------------------------------------------------
//
//------------------------------------------------
// Definition of Packet Header Area
//------------------------------------------------
//
// Caution: these only define header areas. In actual use the data runs off
// beyond the end of these structures.
//
// Since these structures are based on sequences of bytes which go to the board,
// there cannot be ANY padding between the elements.
#pragma pack(1)

//----------------------------
// DATA PACKETS
//----------------------------

typedef struct _i2DataHeader
{
	unsigned char i2sChannel;  /* The channel number: 0-255 */

	// -- Bitfields are allocated LSB first --

	// For incoming data, indicates whether this is an ordinary packet or a
	// special one (e.g., hot key hit).
	unsigned i2sId : 2 __attribute__ ((__packed__));

	// For tagging data packets. There are flush commands which flush only data
	// packets bearing a particular tag. (used in implementing IntelliView and
	// IntelliPrint). THE TAG VALUE 0xf is RESERVED and must not be used (it has
	// meaning internally to the loadware).
	unsigned i2sTag : 4;

	// These two bits determine the type of packet sent/received.
	unsigned i2sType : 2;

	// The count of data to follow: does not include the possible additional
	// padding byte. MAXIMUM COUNT: 4094. The top four bits must be 0.
	unsigned short i2sCount;

} i2DataHeader, *i2DataHeaderPtr;

// Structure is immediately followed by the data, proper.

//----------------------------
// NON-DATA PACKETS
//----------------------------

typedef struct _i2CmdHeader
{
	unsigned char i2sChannel;	// The channel number: 0-255 (Except where noted
								// - see below

	// Number of bytes of commands, status or whatever to follow
	unsigned i2sCount : 6;

	// These two bits determine the type of packet sent/received.
	unsigned i2sType : 2;

} i2CmdHeader, *i2CmdHeaderPtr;

// Structure is immediately followed by the applicable data.

//---------------------------------------
// Flow Control Packets (Outbound)
//---------------------------------------

// One type of outbound command packet is so important that the entire structure
// is explicitly defined here. That is the flow-control packet. This is never
// sent by user-level code (as would be the commands to raise/lower DTR, for
// example). These are only sent by the library routines in response to reading
// incoming data into the buffers.
//
// The parameters inside the command block are maintained in place, then the
// block is sent at the appropriate time.

typedef struct _flowIn
{
	i2CmdHeader    hd;      // Channel #, count, type (see above)
	unsigned char  fcmd;    // The flow control command (37)
	unsigned short asof;    // As of byte number "asof" (LSB first!) I have room
							// for "room" bytes
	unsigned short room;
} flowIn, *flowInPtr;

//----------------------------------------
// (Incoming) Status Packets
//----------------------------------------

// Incoming packets which are non-data packets are status packets. In this case,
// the channel number in the header is unimportant. What follows are one or more
// sub-packets, the first word of which consists of the channel (first or low
// byte) and the status indicator (second or high byte), followed by possibly
// more data.

#define STAT_CTS_UP     0  /* CTS raised  (no other bytes) */
#define STAT_CTS_DN     1  /* CTS dropped (no other bytes) */
#define STAT_DCD_UP     2  /* DCD raised  (no other bytes) */
#define STAT_DCD_DN     3  /* DCD dropped (no other bytes) */
#define STAT_DSR_UP     4  /* DSR raised  (no other bytes) */
#define STAT_DSR_DN     5  /* DSR dropped (no other bytes) */
#define STAT_RI_UP      6  /* RI  raised  (no other bytes) */
#define STAT_RI_DN      7  /* RI  dropped (no other bytes) */
#define STAT_BRK_DET    8  /* BRK detect  (no other bytes) */
#define STAT_FLOW       9  /* Flow control(-- more: see below */
#define STAT_BMARK      10 /* Bookmark    (no other bytes)
							* Bookmark is sent as a response to
							* a command 60: request for bookmark
							*/
#define STAT_STATUS     11 /* Special packet: see below */
#define STAT_TXCNT      12 /* Special packet: see below */
#define STAT_RXCNT      13 /* Special packet: see below */
#define STAT_BOXIDS     14 /* Special packet: see below */
#define STAT_HWFAIL     15 /* Special packet: see below */

#define STAT_MOD_ERROR  0xc0
#define STAT_MODEM      0xc0/* If status & STAT_MOD_ERROR:
							 * == STAT_MODEM, then this is a modem
							 * status packet, given in response to a
							 * CMD_DSS_NOW command.
							 * The low nibble has each data signal:
							 */
#define STAT_MOD_DCD    0x8
#define STAT_MOD_RI     0x4
#define STAT_MOD_DSR    0x2
#define STAT_MOD_CTS    0x1

#define STAT_ERROR      0x80/* If status & STAT_MOD_ERROR
							 * == STAT_ERROR, then
							 * sort of error on the channel.
							 * The remaining seven bits indicate
							 * what sort of error it is.
							 */
/* The low three bits indicate parity, framing, or overrun errors */

#define STAT_E_PARITY   4     /* Parity error */
#define STAT_E_FRAMING  2     /* Framing error */
#define STAT_E_OVERRUN  1     /* (uxart) overrun error */

//---------------------------------------
// STAT_FLOW packets
//---------------------------------------

typedef struct _flowStat
{
	unsigned short asof;
	unsigned short room;
}flowStat, *flowStatPtr;

// flowStat packets are received from the board to regulate the flow of outgoing
// data. A local copy of this structure is also kept to track the amount of
// credits used and credits remaining. "room" is the amount of space in the
// board's buffers, "as of" having received a certain byte number. When sending
// data to the fifo, you must calculate how much buffer space your packet will
// use.  Add this to the current "asof" and subtract it from the current "room".
//
// The calculation for the board's buffer is given by CREDIT_USAGE, where size
// is the un-rounded count of either data characters or command characters.
// (Which is to say, the count rounded up, plus two).

#define CREDIT_USAGE(size) (((size) + 3) & ~1)

//---------------------------------------
// STAT_STATUS packets
//---------------------------------------

typedef  struct   _debugStat
{
	unsigned char d_ccsr;
	unsigned char d_txinh;
	unsigned char d_stat1;
	unsigned char d_stat2;
} debugStat, *debugStatPtr;

// debugStat packets are sent to the host in response to a CMD_GET_STATUS
// command.  Each byte is bit-mapped as described below:

#define D_CCSR_XON      2     /* Has received XON, ready to transmit */
#define D_CCSR_XOFF     4     /* Has received XOFF, not transmitting */
#define D_CCSR_TXENAB   8     /* Transmitter is enabled */
#define D_CCSR_RXENAB   0x80  /* Receiver is enabled */

#define D_TXINH_BREAK   1     /* We are sending a break */
#define D_TXINH_EMPTY   2     /* No data to send */
#define D_TXINH_SUSP    4     /* Output suspended via command 57 */
#define D_TXINH_CMD     8     /* We are processing an in-line command */
#define D_TXINH_LCD     0x10  /* LCD diagnostics are running */
#define D_TXINH_PAUSE   0x20  /* We are processing a PAUSE command */
#define D_TXINH_DCD     0x40  /* DCD is low, preventing transmission */
#define D_TXINH_DSR     0x80  /* DSR is low, preventing transmission */

#define D_STAT1_TXEN    1     /* Transmit INTERRUPTS enabled */
#define D_STAT1_RXEN    2     /* Receiver INTERRUPTS enabled */
#define D_STAT1_MDEN    4     /* Modem (data set sigs) interrupts enabled */
#define D_STAT1_RLM     8     /* Remote loopback mode selected */
#define D_STAT1_LLM     0x10  /* Local internal loopback mode selected */
#define D_STAT1_CTS     0x20  /* CTS is low, preventing transmission */
#define D_STAT1_DTR     0x40  /* DTR is low, to stop remote transmission */
#define D_STAT1_RTS     0x80  /* RTS is low, to stop remote transmission */

#define D_STAT2_TXMT    1     /* Transmit buffers are all empty */
#define D_STAT2_RXMT    2     /* Receive buffers are all empty */
#define D_STAT2_RXINH   4     /* Loadware has tried to inhibit remote
							   * transmission:  dropped DTR, sent XOFF,
							   * whatever...
							   */
#define D_STAT2_RXFLO   8     /* Loadware can send no more data to host
							   * until it receives a flow-control packet
							   */
//-----------------------------------------
// STAT_TXCNT and STAT_RXCNT packets
//----------------------------------------

typedef  struct   _cntStat
{
	unsigned short cs_time;    // (Assumes host is little-endian!)
	unsigned short cs_count;
} cntStat, *cntStatPtr;

// These packets are sent in response to a CMD_GET_RXCNT or a CMD_GET_TXCNT
// bypass command. cs_time is a running 1 Millisecond counter which acts as a
// time stamp. cs_count is a running counter of data sent or received from the
// uxarts. (Not including data added by the chip itself, as with CRLF
// processing).
//------------------------------------------
// STAT_HWFAIL packets
//------------------------------------------

typedef struct _failStat
{
	unsigned char fs_written;
	unsigned char fs_read;
	unsigned short fs_address;
} failStat, *failStatPtr;

// This packet is sent whenever the on-board diagnostic process detects an
// error. At startup, this process is dormant. The host can wake it up by
// issuing the bypass command CMD_HW_TEST. The process runs at low priority and
// performs continuous hardware verification; writing data to certain on-board
// registers, reading it back, and comparing. If it detects an error, this
// packet is sent to the host, and the process goes dormant again until the host
// sends another CMD_HW_TEST. It then continues with the next register to be
// tested.

//------------------------------------------------------------------------------
// Macros to deal with the headers more easily! Note that these are defined so
// they may be used as "left" as well as "right" expressions.
//------------------------------------------------------------------------------

// Given a pointer to the packet, reference the channel number
//
#define CHANNEL_OF(pP)  ((i2DataHeaderPtr)(pP))->i2sChannel

// Given a pointer to the packet, reference the Packet type
//
#define PTYPE_OF(pP) ((i2DataHeaderPtr)(pP))->i2sType

// The possible types of packets
//
#define PTYPE_DATA   0  /* Host <--> Board */
#define PTYPE_BYPASS 1  /* Host ---> Board */
#define PTYPE_INLINE 2  /* Host ---> Board */
#define PTYPE_STATUS 2  /* Host <--- Board */

// Given a pointer to a Data packet, reference the Tag
//
#define TAG_OF(pP) ((i2DataHeaderPtr)(pP))->i2sTag

// Given a pointer to a Data packet, reference the data i.d.
//
#define ID_OF(pP)  ((i2DataHeaderPtr)(pP))->i2sId

// The possible types of ID's
//
#define ID_ORDINARY_DATA   0
#define ID_HOT_KEY         1

// Given a pointer to a Data packet, reference the count
//
#define DATA_COUNT_OF(pP) ((i2DataHeaderPtr)(pP))->i2sCount

// Given a pointer to a Data packet, reference the beginning of data
//
#define DATA_OF(pP) &((unsigned char *)(pP))[4] // 4 = size of header

// Given a pointer to a Non-Data packet, reference the count
//
#define CMD_COUNT_OF(pP) ((i2CmdHeaderPtr)(pP))->i2sCount

#define MAX_CMD_PACK_SIZE  62 // Maximum size of such a count

// Given a pointer to a Non-Data packet, reference the beginning of data
//
#define CMD_OF(pP) &((unsigned char *)(pP))[2]  // 2 = size of header

//--------------------------------
// MailBox Bits:
//--------------------------------

//--------------------------
// Outgoing (host to board)
//--------------------------
//
#define MB_OUT_STUFFED     0x80  // Host has placed output in fifo 
#define MB_IN_STRIPPED     0x40  // Host has read in all input from fifo 

//--------------------------
// Incoming (board to host)
//--------------------------
//
#define MB_IN_STUFFED      0x80  // Board has placed input in fifo 
#define MB_OUT_STRIPPED    0x40  // Board has read all output from fifo 
#define MB_FATAL_ERROR     0x20  // Board has encountered a fatal error

#pragma pack(4)                  // Reset padding to command-line default

#endif      // I2PACK_H


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
*   DESCRIPTION: Header file for high level library functions
*
*******************************************************************************/
#ifndef I2LIB_H
#define I2LIB_H   1
//------------------------------------------------------------------------------
// I2LIB.H
//
// IntelliPort-II and IntelliPort-IIEX
//
// Defines, structure definitions, and external declarations for i2lib.c
//------------------------------------------------------------------------------
//--------------------------------------
// Mandatory Includes:
//--------------------------------------
#include "ip2types.h"
#include "i2ellis.h"
#include "i2pack.h"
#include "i2cmd.h"
#include <linux/workqueue.h>

//------------------------------------------------------------------------------
// i2ChanStr -- Channel Structure:
// Used to track per-channel information for the library routines using standard
// loadware. Note also, a pointer to an array of these structures is patched
// into the i2eBordStr (see i2ellis.h)
//------------------------------------------------------------------------------
//
// If we make some limits on the maximum block sizes, we can avoid dealing with
// buffer wrap. The wrapping of the buffer is based on where the start of the
// packet is. Then there is always room for the packet contiguously.
//
// Maximum total length of an outgoing data or in-line command block. The limit
// of 36 on data is quite arbitrary and based more on DOS memory limitations
// than the board interface. However, for commands, the maximum packet length is
// MAX_CMD_PACK_SIZE, because the field size for the count is only a few bits
// (see I2PACK.H) in such packets. For data packets, the count field size is not
// the limiting factor. As of this writing, MAX_OBUF_BLOCK < MAX_CMD_PACK_SIZE,
// but be careful if wanting to modify either.
//
#define MAX_OBUF_BLOCK  36

// Another note on maximum block sizes: we are buffering packets here. Data is
// put into the buffer (if there is room) regardless of the credits from the
// board. The board sends new credits whenever it has removed from his buffers a
// number of characters equal to 80% of total buffer size. (Of course, the total
// buffer size is what is reported when the very first set of flow control
// status packets are received from the board. Therefore, to be robust, you must
// always fill the board to at least 80% of the current credit limit, else you
// might not give it enough to trigger a new report. These conditions are
// obtained here so long as the maximum output block size is less than 20% the
// size of the board's output buffers. This is true at present by "coincidence"
// or "infernal knowledge": the board's output buffers are at least 700 bytes
// long (20% = 140 bytes, at least). The 80% figure is "official", so the safest
// strategy might be to trap the first flow control report and guarantee that
// the effective maxObufBlock is the minimum of MAX_OBUF_BLOCK and 20% of first
// reported buffer credit.
//
#define MAX_CBUF_BLOCK  6	// Maximum total length of a bypass command block

#define IBUF_SIZE       512	// character capacity of input buffer per channel
#define OBUF_SIZE       1024// character capacity of output buffer per channel
#define CBUF_SIZE       10	// character capacity of output bypass buffer

typedef struct _i2ChanStr
{
	// First, back-pointers so that given a pointer to this structure, you can
	// determine the correct board and channel number to reference, (say, when
	// issuing commands, etc. (Note, channel number is in infl.hd.i2sChannel.)

	int      port_index;    // Index of port in channel structure array attached
							// to board structure.
	PTTY     pTTY;          // Pointer to tty structure for port (OS specific)
	USHORT   validity;      // Indicates whether the given channel has been
							// initialized, really exists (or is a missing
							// channel, e.g. channel 9 on an 8-port box.)

	i2eBordStrPtr  pMyBord; // Back-pointer to this channel's board structure 

	int      wopen;			// waiting fer carrier

	int      throttled;		// Set if upper layer can take no data

	int      flags;         // Defined in tty.h

	PWAITQ   open_wait;     // Pointer for OS sleep function.
	PWAITQ   close_wait;    // Pointer for OS sleep function.
	PWAITQ   delta_msr_wait;// Pointer for OS sleep function.
	PWAITQ   dss_now_wait;	// Pointer for OS sleep function.

	struct timer_list  BookmarkTimer;   // Used by i2DrainOutput
	wait_queue_head_t pBookmarkWait;   // Used by i2DrainOutput

	int      BaudBase;
	int      BaudDivisor;

	USHORT   ClosingDelay;
	USHORT   ClosingWaitTime;

	volatile
	flowIn   infl;	// This structure is initialized as a completely
					// formed flow-control command packet, and as such
					// has the channel number, also the capacity and
					// "as-of" data needed continuously.

	USHORT   sinceLastFlow; // Counts the number of characters read from input
							// buffers, since the last time flow control info
							// was sent.

	USHORT   whenSendFlow;  // Determines when new flow control is to be sent to
							// the board. Note unlike earlier manifestations of
							// the driver, these packets can be sent from
							// in-place.

	USHORT   channelNeeds;  // Bit map of important things which must be done
							// for this channel. (See bits below )

	volatile
	flowStat outfl;         // Same type of structure is used to hold current
							// flow control information used to control our
							// output. "asof" is kept updated as data is sent,
							// and "room" never goes to zero.

	// The incoming ring buffer
	// Unlike the outgoing buffers, this holds raw data, not packets. The two
	// extra bytes are used to hold the byte-padding when there is room for an
	// odd number of bytes before we must wrap.
	//
	UCHAR    Ibuf[IBUF_SIZE + 2];
	volatile
	USHORT   Ibuf_stuff;     // Stuffing index
	volatile
	USHORT   Ibuf_strip;     // Stripping index

	// The outgoing ring-buffer: Holds Data and command packets. N.B., even
	// though these are in the channel structure, the channel is also written
	// here, the easier to send it to the fifo when ready. HOWEVER, individual
	// packets here are NOT padded to even length: the routines for writing
	// blocks to the fifo will pad to even byte counts.
	//
	UCHAR	Obuf[OBUF_SIZE+MAX_OBUF_BLOCK+4];
	volatile
	USHORT	Obuf_stuff;     // Stuffing index
	volatile
	USHORT	Obuf_strip;     // Stripping index
	int	Obuf_char_count;

	// The outgoing bypass-command buffer. Unlike earlier manifestations, the
	// flow control packets are sent directly from the structures. As above, the
	// channel number is included in the packet, but they are NOT padded to even
	// size.
	//
	UCHAR    Cbuf[CBUF_SIZE+MAX_CBUF_BLOCK+2];
	volatile
	USHORT   Cbuf_stuff;     // Stuffing index
	volatile
	USHORT   Cbuf_strip;     // Stripping index

	// The temporary buffer for the Linux tty driver PutChar entry.
	//
	UCHAR    Pbuf[MAX_OBUF_BLOCK - sizeof (i2DataHeader)];
	volatile
	USHORT   Pbuf_stuff;     // Stuffing index

	// The state of incoming data-set signals
	//
	USHORT   dataSetIn;     // Bit-mapped according to below. Also indicates
							// whether a break has been detected since last
							// inquiry.

	// The state of outcoming data-set signals (as far as we can tell!)
	//
	USHORT   dataSetOut;     // Bit-mapped according to below. 

	// Most recent hot-key identifier detected
	//
	USHORT   hotKeyIn;      // Hot key as sent by the board, HOT_CLEAR indicates
				// no hot key detected since last examined.

	// Counter of outstanding requests for bookmarks
	//
	short   bookMarks;	// Number of outstanding bookmark requests, (+ive
						// whenever a bookmark request if queued up, -ive
						// whenever a bookmark is received).

	// Misc options
	//
	USHORT   channelOptions;   // See below

	// To store various incoming special packets
	//
	debugStat   channelStatus;
	cntStat     channelRcount;
	cntStat     channelTcount;
	failStat    channelFail;

	// To store the last values for line characteristics we sent to the board.
	//
	int	speed;

	int flush_flags;

	void (*trace)(unsigned short,unsigned char,unsigned char,unsigned long,...);

	/*
	 * Kernel counters for the 4 input interrupts 
	 */
	struct async_icount icount;

	/*
	 *	Task queues for processing input packets from the board.
	 */
	struct work_struct	tqueue_input;
	struct work_struct	tqueue_status;
	struct work_struct	tqueue_hangup;

	rwlock_t Ibuf_spinlock;
	rwlock_t Obuf_spinlock;
	rwlock_t Cbuf_spinlock;
	rwlock_t Pbuf_spinlock;

} i2ChanStr, *i2ChanStrPtr;

//---------------------------------------------------
// Manifests and bit-maps for elements in i2ChanStr
//---------------------------------------------------
//
// flush flags
//
#define STARTFL_FLAG 1
#define STOPFL_FLAG  2

// validity
//
#define CHANNEL_MAGIC_BITS 0xff00
#define CHANNEL_MAGIC      0x5300   // (validity & CHANNEL_MAGIC_BITS) ==
									// CHANNEL_MAGIC --> structure good

#define CHANNEL_SUPPORT    0x0001   // Indicates channel is supported, exists,
									// and passed P.O.S.T.

// channelNeeds
//
#define NEED_FLOW    1  // Indicates flow control has been queued
#define NEED_INLINE  2  // Indicates inline commands or data queued
#define NEED_BYPASS  4  // Indicates bypass commands queued
#define NEED_CREDIT  8  // Indicates would be sending except has not sufficient
						// credit. The data is still in the channel structure,
						// but the channel is not enqueued in the board
						// structure again until there is a credit received from
						// the board.

// dataSetIn (Also the bits for i2GetStatus return value)
//
#define I2_DCD 1
#define I2_CTS 2
#define I2_DSR 4
#define I2_RI  8

// dataSetOut (Also the bits for i2GetStatus return value)
//
#define I2_DTR 1
#define I2_RTS 2

// i2GetStatus() can optionally clear these bits
//
#define I2_BRK    0x10  // A break was detected
#define I2_PAR    0x20  // A parity error was received 
#define I2_FRA    0x40  // A framing error was received
#define I2_OVR    0x80  // An overrun error was received 

// i2GetStatus() automatically clears these bits */
//
#define I2_DDCD   0x100 // DCD changed from its  former value
#define I2_DCTS   0x200 // CTS changed from its former value 
#define I2_DDSR   0x400 // DSR changed from its former value 
#define I2_DRI    0x800 // RI changed from its former value 

// hotKeyIn
//
#define HOT_CLEAR 0x1322   // Indicates that no hot-key has been detected

// channelOptions
//
#define CO_NBLOCK_WRITE 1  	// Writes don't block waiting for buffer. (Default
							// is, they do wait.)

// fcmodes
//
#define I2_OUTFLOW_CTS  0x0001
#define I2_INFLOW_RTS   0x0002
#define I2_INFLOW_DSR   0x0004
#define I2_INFLOW_DTR   0x0008
#define I2_OUTFLOW_DSR  0x0010
#define I2_OUTFLOW_DTR  0x0020
#define I2_OUTFLOW_XON  0x0040
#define I2_OUTFLOW_XANY 0x0080
#define I2_INFLOW_XON   0x0100

#define I2_CRTSCTS      (I2_OUTFLOW_CTS|I2_INFLOW_RTS)
#define I2_IXANY_MODE   (I2_OUTFLOW_XON|I2_OUTFLOW_XANY)

//-------------------------------------------
// Macros used from user level like functions
//-------------------------------------------

// Macros to set and clear channel options
//
#define i2SetOption(pCh, option) pCh->channelOptions |= option
#define i2ClrOption(pCh, option) pCh->channelOptions &= ~option

// Macro to set fatal-error trap
//
#define i2SetFatalTrap(pB, routine) pB->i2eFatalTrap = routine

//--------------------------------------------
// Declarations and prototypes for i2lib.c
//--------------------------------------------
//
static int  i2InitChannels(i2eBordStrPtr, int, i2ChanStrPtr);
static int  i2QueueCommands(int, i2ChanStrPtr, int, int, cmdSyntaxPtr,...);
static int  i2GetStatus(i2ChanStrPtr, int);
static int  i2Input(i2ChanStrPtr);
static int  i2InputFlush(i2ChanStrPtr);
static int  i2Output(i2ChanStrPtr, const char *, int, int);
static int  i2OutputFree(i2ChanStrPtr);
static int  i2ServiceBoard(i2eBordStrPtr);
static void i2DrainOutput(i2ChanStrPtr, int);

#ifdef IP2DEBUG_TRACE
void ip2trace(unsigned short,unsigned char,unsigned char,unsigned long,...);
#else
#define ip2trace(a,b,c,d...) do {} while (0)
#endif

// Argument to i2QueueCommands
//
#define C_IN_LINE 1
#define C_BYPASS  0

#endif   // I2LIB_H

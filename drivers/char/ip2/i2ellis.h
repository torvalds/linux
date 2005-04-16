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
*   DESCRIPTION: Mainline code for the device driver
*
*******************************************************************************/
//------------------------------------------------------------------------------
// i2ellis.h
//
// IntelliPort-II and IntelliPort-IIEX
//
// Extremely
// Low
// Level
// Interface
// Services
//
// Structure Definitions and declarations for "ELLIS" service routines found in
// i2ellis.c
//
// These routines are based on properties of the IntelliPort-II and -IIEX
// hardware and bootstrap firmware, and are not sensitive to particular
// conventions of any particular loadware.
//
// Unlike i2hw.h, which provides IRONCLAD hardware definitions, the material
// here and in i2ellis.c is intended to provice a useful, but not required,
// layer of insulation from the hardware specifics.
//------------------------------------------------------------------------------
#ifndef  I2ELLIS_H   /* To prevent multiple includes */
#define  I2ELLIS_H   1
//------------------------------------------------
// Revision History:
//
// 30 September 1991 MAG First Draft Started
// 12 October   1991 ...continued...
//
// 20 December  1996 AKM Linux version
//-------------------------------------------------

//----------------------
// Mandatory Includes:
//----------------------
#include <linux/config.h>
#include "ip2types.h"
#include "i2hw.h"       // The hardware definitions

//------------------------------------------
// STAT_BOXIDS packets
//------------------------------------------
#define MAX_BOX		4

typedef struct _bidStat
{
	unsigned char bid_value[MAX_BOX];
} bidStat, *bidStatPtr;

// This packet is sent in response to a CMD_GET_BOXIDS bypass command. For -IIEX
// boards, reports the hardware-specific "asynchronous resource register" on
// each expansion box. Boxes not present report 0xff. For -II boards, the first
// element contains 0x80 for 8-port, 0x40 for 4-port boards.

// Box IDs aka ARR or Async Resource Register (more than you want to know)
//   7   6   5   4   3   2   1   0
//   F   F   N   N   L   S   S   S
//   =============================
//   F   F   -  Product Family Designator
//   =====+++++++++++++++++++++++++++++++
//   0   0   -  Intelliport II EX / ISA-8
//   1   0   -  IntelliServer
//   0   1   -  SAC - Port Device (Intelliport III ??? )
//           =====+++++++++++++++++++++++++++++++++++++++
//           N   N   -  Number of Ports
//           0   0   -  8  (eight)
//           0   1   -  4  (four)
//           1   0   -  12 (twelve)
//           1   1   -  16 (sixteen)
//                   =++++++++++++++++++++++++++++++++++
//                   L  -   LCD Display Module Present
//                   0  -   No
//                   1  -   LCD module present
//                   =========+++++++++++++++++++++++++++++++++++++
//                      S   S   S - Async Signals Supported Designator
//                      0   0   0 - 8dss, Mod DCE DB25 Female
//                      0   0   1 - 6dss, RJ-45
//                      0   1   0 - RS-232/422 dss, DB25 Female
//                      0   1   1 - RS-232/422 dss, separate 232/422 DB25 Female
//                      1   0   0 - 6dss, 921.6 I/F with ST654's
//                      1   0   1 - RS-423/232 8dss, RJ-45 10Pin
//                      1   1   0 - 6dss, Mod DCE DB25 Female
//                      1   1   1 - NO BOX PRESENT

#define FF(c)	((c & 0xC0) >> 6)
#define NN(c)	((c & 0x30) >> 4)
#define L(c)	((c & 0x08) >> 3)
#define SSS(c)	 (c & 0x07)

#define BID_HAS_654(x)	(SSS(x) == 0x04)
#define BID_NO_BOX	0xff /* no box */
#define BID_8PORT  	0x80 /* IP2-8 port */
#define BID_4PORT   	0x81 /* IP2-4 port */
#define BID_EXP_MASK   	0x30 /* IP2-EX  */
#define BID_EXP_8PORT	0x00 /*     8, */
#define BID_EXP_4PORT	0x10 /*     4, */
#define BID_EXP_UNDEF	0x20 /*     UNDEF, */
#define BID_EXP_16PORT	0x30 /*    16, */
#define BID_LCD_CTRL   	0x08 /* LCD Controller */
#define BID_LCD_NONE	0x00 /* - no controller present */
#define BID_LCD_PRES   	0x08 /* - controller present */
#define BID_CON_MASK	0x07 /* - connector pinouts */
#define BID_CON_DB25	0x00 /* - DB-25 F */
#define BID_CON_RJ45	0x01 /* - rj45 */

//------------------------------------------------------------------------------
// i2eBordStr
//
// This structure contains all the information the ELLIS routines require in
// dealing with a particular board.
//------------------------------------------------------------------------------
// There are some queues here which are guaranteed to never contain the entry
// for a single channel twice. So they must be slightly larger to allow
// unambiguous full/empty management
//
#define CH_QUEUE_SIZE ABS_MOST_PORTS+2

typedef struct _i2eBordStr
{
	porStr         i2ePom;	// Structure containing the power-on message.

	unsigned short i2ePomSize;
						// The number of bytes actually read if
						// different from sizeof i2ePom, indicates
						// there is an error!

	unsigned short i2eStartMail;
						// Contains whatever inbound mailbox data
						// present at startup. NO_MAIL_HERE indicates
						// nothing was present. No special
						// significance as of this writing, but may be
						// useful for diagnostic reasons.

	unsigned short i2eValid;
						// Indicates validity of the structure; if
						// i2eValid == I2E_MAGIC, then we can trust
						// the other fields. Some (especially
						// initialization) functions are good about
						// checking for validity.  Many functions do
						// not, it being assumed that the larger
						// context assures we are using a valid
						// i2eBordStrPtr.

	unsigned short i2eError;
						// Used for returning an error condition from
						// several functions which use i2eBordStrPtr
						// as an argument.

	// Accelerators to characterize separate features of a board, derived from a
	// number of sources.

	unsigned short i2eFifoSize;
						// Always, the size of the FIFO. For
						// IntelliPort-II, always the same, for -IIEX
						// taken from the Power-On reset message.

	volatile 
	unsigned short i2eFifoRemains;
						// Used during normal operation to indicate a
						// lower bound on the amount of data which
						// might be in the outbound fifo.

	unsigned char  i2eFifoStyle;
						// Accelerator which tells which style (-II or
						// -IIEX) FIFO we are using.

	unsigned char  i2eDataWidth16;
						// Accelerator which tells whether we should
						// do 8 or 16-bit data transfers.

	unsigned char  i2eMaxIrq;
						// The highest allowable IRQ, based on the
						// slot size.

	unsigned char  i2eChangeIrq;
						// Whether tis valid to change IRQ's
						// ISA = ok, EISA, MicroChannel, no

	// Accelerators for various addresses on the board
	int            i2eBase;        // I/O Address of the Board
	int            i2eData;        // From here data transfers happen
	int            i2eStatus;      // From here status reads happen
	int            i2ePointer;     // (IntelliPort-II: pointer/commands)
	int            i2eXMail;       // (IntelliPOrt-IIEX: mailboxes
	int            i2eXMask;       // (IntelliPort-IIEX: mask write

	//-------------------------------------------------------
	// Information presented in a common format across boards
	// For each box, bit map of the channels present.  Box closest to 
	// the host is box 0. LSB is channel 0. IntelliPort-II (non-expandable)
	// is taken to be box 0. These are derived from product i.d. registers.

	unsigned short i2eChannelMap[ABS_MAX_BOXES];

	// Same as above, except each is derived from firmware attempting to detect
	// the uart presence (by reading a valid GFRCR register). If bits are set in
	// i2eChannelMap and not in i2eGoodMap, there is a potential problem.

	unsigned short i2eGoodMap[ABS_MAX_BOXES];

	// ---------------------------
	// For indirect function calls

	// Routine to cause an N-millisecond delay: Patched by the ii2Initialize
	// function.

	void  (*i2eDelay)(unsigned int);

	// Routine to write N bytes to the board through the FIFO. Returns true if
	// all copacetic, otherwise returns false and error is in i2eError field.
	// IF COUNT IS ODD, ROUNDS UP TO THE NEXT EVEN NUMBER.

	int   (*i2eWriteBuf)(struct _i2eBordStr *, unsigned char *, int);

	// Routine to read N bytes from the board through the FIFO. Returns true if
	// copacetic, otherwise returns false and error in i2eError.
	// IF COUNT IS ODD, ROUNDS UP TO THE NEXT EVEN NUMBER.

	int   (*i2eReadBuf)(struct _i2eBordStr *, unsigned char *, int);

	// Returns a word from FIFO. Will use 2 byte operations if needed.

	unsigned short (*i2eReadWord)(struct _i2eBordStr *);

	// Writes a word to FIFO. Will use 2 byte operations if needed.

	void  (*i2eWriteWord)(struct _i2eBordStr *, unsigned short);

	// Waits specified time for the Transmit FIFO to go empty. Returns true if
	//  ok, otherwise returns false and error in i2eError.

	int   (*i2eWaitForTxEmpty)(struct _i2eBordStr *, int);

	// Returns true or false according to whether the outgoing mailbox is empty.

	int   (*i2eTxMailEmpty)(struct _i2eBordStr *);

	// Checks whether outgoing mailbox is empty.  If so, sends mail and returns
	// true.  Otherwise returns false.

	int   (*i2eTrySendMail)(struct _i2eBordStr *, unsigned char);

	// If no mail available, returns NO_MAIL_HERE, else returns the value in the
	// mailbox (guaranteed can't be NO_MAIL_HERE).

	unsigned short (*i2eGetMail)(struct _i2eBordStr *);

	// Enables the board to interrupt the host when it writes to the mailbox.
	// Irqs will not occur, however, until the loadware separately enables
	// interrupt generation to the host.  The standard loadware does this in
	// response to a command packet sent by the host. (Also, disables
	// any other potential interrupt sources from the board -- other than the
	// inbound mailbox).

	void  (*i2eEnableMailIrq)(struct _i2eBordStr *);

	// Writes an arbitrary value to the mask register.

	void  (*i2eWriteMask)(struct _i2eBordStr *, unsigned char);


	// State information

	// During downloading, indicates the number of blocks remaining to download
	// to the board.

	short i2eToLoad;

	// State of board (see manifests below) (e.g., whether in reset condition,
	// whether standard loadware is installed, etc.

	unsigned char  i2eState;

	// These three fields are only valid when there is loadware running on the
	// board. (i2eState == II_STATE_LOADED or i2eState == II_STATE_STDLOADED )

	unsigned char  i2eLVersion;  // Loadware version
	unsigned char  i2eLRevision; // Loadware revision
	unsigned char  i2eLSub;      // Loadware subrevision

	// Flags which only have meaning in the context of the standard loadware.
	// Somewhat violates the layering concept, but there is so little additional
	// needed at the board level (while much additional at the channel level),
	// that this beats maintaining two different per-board structures.

	// Indicates which IRQ the board has been initialized (from software) to use
	// For MicroChannel boards, any value different from IRQ_UNDEFINED means
	// that the software command has been sent to enable interrupts (or specify
	// they are disabled). Special value: IRQ_UNDEFINED indicates that the
	// software command to select the interrupt has not yet been sent, therefore
	// (since the standard loadware insists that it be sent before any other
	// packets are sent) no other packets should be sent yet.

	unsigned short i2eUsingIrq;

	// This is set when we hit the MB_OUT_STUFFED mailbox, which prevents us
	// putting more in the mailbox until an appropriate mailbox message is
	// received.

	unsigned char  i2eWaitingForEmptyFifo;

	// Any mailbox bits waiting to be sent to the board are OR'ed in here.

	unsigned char  i2eOutMailWaiting;

	// The head of any incoming packet is read into here, is then examined and 
	// we dispatch accordingly.

	unsigned short i2eLeadoffWord[1];

	// Running counter of interrupts where the mailbox indicated incoming data.

	unsigned short i2eFifoInInts;

	// Running counter of interrupts where the mailbox indicated outgoing data
	// had been stripped.

	unsigned short i2eFifoOutInts;

	// If not void, gives the address of a routine to call if fatal board error
	// is found (only applies to standard l/w).

	void  (*i2eFatalTrap)(struct _i2eBordStr *);

	// Will point to an array of some sort of channel structures (whose format
	// is unknown at this level, being a function of what loadware is
	// installed and the code configuration (max sizes of buffers, etc.)).

	void  *i2eChannelPtr;

	// Set indicates that the board has gone fatal.

	unsigned short i2eFatal;

	// The number of elements pointed to by i2eChannelPtr.

	unsigned short i2eChannelCnt;

	// Ring-buffers of channel structures whose channels have particular needs.

	rwlock_t	Fbuf_spinlock;
	volatile
	unsigned short i2Fbuf_strip;	// Strip index
	volatile 
	unsigned short i2Fbuf_stuff;	// Stuff index
	void  *i2Fbuf[CH_QUEUE_SIZE];	// An array of channel pointers
									// of channels who need to send
									// flow control packets.
	rwlock_t	Dbuf_spinlock;
	volatile
	unsigned short i2Dbuf_strip;	// Strip index
	volatile
	unsigned short i2Dbuf_stuff;	// Stuff index
	void  *i2Dbuf[CH_QUEUE_SIZE];	// An array of channel pointers
									// of channels who need to send
									// data or in-line command packets.
	rwlock_t	Bbuf_spinlock;
	volatile
	unsigned short i2Bbuf_strip;	// Strip index
	volatile
	unsigned short i2Bbuf_stuff;	// Stuff index
	void  *i2Bbuf[CH_QUEUE_SIZE];	// An array of channel pointers
									// of channels who need to send
									// bypass command packets.

	/*
	 * A set of flags to indicate that certain events have occurred on at least
	 * one of the ports on this board. We use this to decide whether to spin
	 * through the channels looking for breaks, etc.
	 */
	int		got_input;
	int		status_change;
	bidStat	channelBtypes;

	/*
	 * Debugging counters, etc.
	 */
	unsigned long debugFlowQueued;
	unsigned long debugInlineQueued;
	unsigned long debugDataQueued;
	unsigned long debugBypassQueued;
	unsigned long debugFlowCount;
	unsigned long debugInlineCount;
	unsigned long debugBypassCount;
	
	rwlock_t	read_fifo_spinlock;
	rwlock_t	write_fifo_spinlock;

//	For queuing interrupt bottom half handlers.	/\/\|=mhw=|\/\/
	struct work_struct	tqueue_interrupt;

	struct timer_list  SendPendingTimer;   // Used by iiSendPending
	unsigned int	SendPendingRetry;
} i2eBordStr, *i2eBordStrPtr;

//-------------------------------------------------------------------
// Macro Definitions for the indirect calls defined in the i2eBordStr
//-------------------------------------------------------------------
//
#define iiDelay(a,b)          (*(a)->i2eDelay)(b)
#define iiWriteBuf(a,b,c)     (*(a)->i2eWriteBuf)(a,b,c)
#define iiReadBuf(a,b,c)      (*(a)->i2eReadBuf)(a,b,c)

#define iiWriteWord(a,b)      (*(a)->i2eWriteWord)(a,b)
#define iiReadWord(a)         (*(a)->i2eReadWord)(a)

#define iiWaitForTxEmpty(a,b) (*(a)->i2eWaitForTxEmpty)(a,b)

#define iiTxMailEmpty(a)      (*(a)->i2eTxMailEmpty)(a)
#define iiTrySendMail(a,b)    (*(a)->i2eTrySendMail)(a,b)

#define iiGetMail(a)          (*(a)->i2eGetMail)(a)
#define iiEnableMailIrq(a)    (*(a)->i2eEnableMailIrq)(a)
#define iiDisableMailIrq(a)   (*(a)->i2eWriteMask)(a,0)
#define iiWriteMask(a,b)      (*(a)->i2eWriteMask)(a,b)

//-------------------------------------------
// Manifests for i2eBordStr:
//-------------------------------------------

#define YES 1
#define NO  0

#define NULLFUNC (void (*)(void))0
#define NULLPTR (void *)0

typedef void (*delayFunc_t)(unsigned int);

// i2eValid
//
#define I2E_MAGIC       0x4251   // Structure is valid.
#define I2E_INCOMPLETE  0x1122   // Structure failed during init.


// i2eError
//
#define I2EE_GOOD       0	// Operation successful
#define I2EE_BADADDR    1	// Address out of range
#define I2EE_BADSTATE   2	// Attempt to perform a function when the board
							// structure was in the incorrect state
#define I2EE_BADMAGIC   3	// Bad magic number from Power On test (i2ePomSize
							// reflects what was read
#define I2EE_PORM_SHORT 4	// Power On message too short
#define I2EE_PORM_LONG  5	// Power On message too long
#define I2EE_BAD_FAMILY 6	// Un-supported board family type
#define I2EE_INCONSIST  7	// Firmware reports something impossible,
							// e.g. unexpected number of ports... Almost no
							// excuse other than bad FIFO...
#define I2EE_POSTERR    8	// Power-On self test reported a bad error
#define I2EE_BADBUS     9	// Unknown Bus type declared in message
#define I2EE_TXE_TIME   10	// Timed out waiting for TX Fifo to empty
#define I2EE_INVALID    11	// i2eValid field does not indicate a valid and
							// complete board structure (for functions which
							// require this be so.)
#define I2EE_BAD_PORT   12	// Discrepancy between channels actually found and
							// what the product is supposed to have. Check
							// i2eGoodMap vs i2eChannelMap for details.
#define I2EE_BAD_IRQ    13	// Someone specified an unsupported IRQ
#define I2EE_NOCHANNELS 14	// No channel structures have been defined (for
							// functions requiring this).

// i2eFifoStyle
//
#define FIFO_II   0  /* IntelliPort-II style: see also i2hw.h */
#define FIFO_IIEX 1  /* IntelliPort-IIEX style */

// i2eGetMail
//
#define NO_MAIL_HERE    0x1111	// Since mail is unsigned char, cannot possibly
								// promote to 0x1111.
// i2eState
//
#define II_STATE_COLD      0  // Addresses have been defined, but board not even
							  // reset yet.
#define II_STATE_RESET     1  // Board,if it exists, has just been reset
#define II_STATE_READY     2  // Board ready for its first block
#define II_STATE_LOADING   3  // Board continuing load
#define II_STATE_LOADED    4  // Board has finished load: status ok
#define II_STATE_BADLOAD   5  // Board has finished load: failed!
#define II_STATE_STDLOADED 6  // Board has finished load: standard firmware

// i2eUsingIrq
//
#define IRQ_UNDEFINED   0x1352  // No valid irq (or polling = 0) can ever
								// promote to this!
//------------------------------------------
// Handy Macros for i2ellis.c and others
// Note these are common to -II and -IIEX
//------------------------------------------

// Given a pointer to the board structure, does the input FIFO have any data or
// not?
//
#define HAS_INPUT(pB)      !(INB(pB->i2eStatus) & ST_IN_EMPTY)
#define HAS_NO_INPUT(pB)   (INB(pB->i2eStatus) & ST_IN_EMPTY)

// Given a pointer to board structure, read a byte or word from the fifo
//
#define BYTE_FROM(pB)      (unsigned char)INB(pB->i2eData)
#define WORD_FROM(pB)      (unsigned short)INW(pB->i2eData)

// Given a pointer to board structure, is there room for any data to be written
// to the data fifo?
//
#define HAS_OUTROOM(pB)    !(INB(pB->i2eStatus) & ST_OUT_FULL)
#define HAS_NO_OUTROOM(pB) (INB(pB->i2eStatus) & ST_OUT_FULL)

// Given a pointer to board structure, write a single byte to the fifo
// structure. Note that for 16-bit interfaces, the high order byte is undefined
// and unknown.
//
#define BYTE_TO(pB, c)     OUTB(pB->i2eData,(c))

// Write a word to the fifo structure. For 8-bit interfaces, this may have
// unknown results.
//
#define WORD_TO(pB, c)     OUTW(pB->i2eData,(c))

// Given a pointer to the board structure, is there anything in the incoming
// mailbox?
//
#define HAS_MAIL(pB)       (INB(pB->i2eStatus) & ST_IN_MAIL)

#define UPDATE_FIFO_ROOM(pB)  (pB)->i2eFifoRemains=(pB)->i2eFifoSize

// Handy macro to round up a number (like the buffer write and read routines do)
// 
#define ROUNDUP(number)    (((number)+1) & (~1))

//------------------------------------------
// Function Declarations for i2ellis.c
//------------------------------------------
//
// Functions called directly
//
// Initialization of a board & structure is in four (five!) parts:
//
// 0) iiEllisInit()  - Initialize iiEllis subsystem.
// 1) iiSetAddress() - Define the board address & delay function for a board.
// 2) iiReset()      - Reset the board   (provided it exists)
//       -- Note you may do this to several boards --
// 3) iiResetDelay() - Delay for 2 seconds (once for all boards)
// 4) iiInitialize() - Attempt to read Power-up message; further initialize
//                     accelerators
//
// Then you may use iiDownloadAll() or iiDownloadFile() (in i2file.c) to write
// loadware.  To change loadware, you must begin again with step 2, resetting
// the board again (step 1 not needed).

static void iiEllisInit(void);
static int iiSetAddress(i2eBordStrPtr, int, delayFunc_t );
static int iiReset(i2eBordStrPtr);
static int iiResetDelay(i2eBordStrPtr);
static int iiInitialize(i2eBordStrPtr);

// Routine to validate that all channels expected are there.
//
extern int iiValidateChannels(i2eBordStrPtr);

// Routine used to download a block of loadware.
//
static int iiDownloadBlock(i2eBordStrPtr, loadHdrStrPtr, int);

// Return values given by iiDownloadBlock, iiDownloadAll, iiDownloadFile:
//
#define II_DOWN_BADVALID   0	// board structure is invalid
#define II_DOWN_CONTINUING 1	// So far, so good, firmware expects more
#define II_DOWN_GOOD       2	// Download complete, CRC good
#define II_DOWN_BAD        3	// Download complete, but CRC bad
#define II_DOWN_BADFILE    4	// Bad magic number in loadware file
#define II_DOWN_BADSTATE   5	// Board is in an inappropriate state for
								// downloading loadware. (see i2eState)
#define II_DOWN_TIMEOUT    6	// Timeout waiting for firmware
#define II_DOWN_OVER       7	// Too much data
#define II_DOWN_UNDER      8	// Not enough data
#define II_DOWN_NOFILE     9	// Loadware file not found

// Routine to download an entire loadware module: Return values are a subset of
// iiDownloadBlock's, excluding, of course, II_DOWN_CONTINUING
//
static int iiDownloadAll(i2eBordStrPtr, loadHdrStrPtr, int, int);

// Called indirectly always.  Needed externally so the routine might be
// SPECIFIED as an argument to iiReset()
//
//static void ii2DelayIO(unsigned int);		// N-millisecond delay using
											//hardware spin
//static void ii2DelayTimer(unsigned int);	// N-millisecond delay using Linux
											//timer

// Many functions defined here return True if good, False otherwise, with an
// error code in i2eError field. Here is a handy macro for setting the error
// code and returning.
//
#define COMPLETE(pB,code) \
	if(1){ \
		 pB->i2eError = code; \
		 return (code == I2EE_GOOD);\
	}

#endif   // I2ELLIS_H

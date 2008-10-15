/*******************************************************************************
*
*   (c) 1999 by Computone Corporation
*
********************************************************************************
*
*
*   PACKAGE:     Linux tty Device Driver for IntelliPort family of multiport
*                serial I/O controllers.
*
*   DESCRIPTION: High-level interface code for the device driver. Uses the
*                Extremely Low Level Interface Support (i2ellis.c). Provides an
*                interface to the standard loadware, to support drivers or
*                application code. (This is included source code, not a separate
*                compilation module.)
*
*******************************************************************************/
//------------------------------------------------------------------------------
// Note on Strategy:
// Once the board has been initialized, it will interrupt us when:
// 1) It has something in the fifo for us to read (incoming data, flow control
// packets, or whatever).
// 2) It has stripped whatever we have sent last time in the FIFO (and
// consequently is ready for more).
//
// Note also that the buffer sizes declared in i2lib.h are VERY SMALL. This
// worsens performance considerably, but is done so that a great many channels
// might use only a little memory.
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// Revision History:
//
// 0.00 -  4/16/91 --- First Draft
// 0.01 -  4/29/91 --- 1st beta release
// 0.02 -  6/14/91 --- Changes to allow small model compilation
// 0.03 -  6/17/91 MAG Break reporting protected from interrupts routines with
//                     in-line asm added for moving data to/from ring buffers,
//                     replacing a variety of methods used previously.
// 0.04 -  6/21/91 MAG Initial flow-control packets not queued until
//                     i2_enable_interrupts time. Former versions would enqueue
//                     them at i2_init_channel time, before we knew how many
//                     channels were supposed to exist!
// 0.05 - 10/12/91 MAG Major changes: works through the ellis.c routines now;
//                     supports new 16-bit protocol and expandable boards.
//      - 10/24/91 MAG Most changes in place and stable.
// 0.06 -  2/20/92 MAG Format of CMD_HOTACK corrected: the command takes no
//                     argument.
// 0.07 -- 3/11/92 MAG Support added to store special packet types at interrupt
//                     level (mostly responses to specific commands.)
// 0.08 -- 3/30/92 MAG Support added for STAT_MODEM packet
// 0.09 -- 6/24/93 MAG i2Link... needed to update number of boards BEFORE
//                     turning on the interrupt.
// 0.10 -- 6/25/93 MAG To avoid gruesome death from a bad board, we sanity check
//                     some incoming.
//
// 1.1  - 12/25/96 AKM Linux version.
//      - 10/09/98 DMC Revised Linux version.
//------------------------------------------------------------------------------

//************
//* Includes *
//************

#include <linux/sched.h>
#include "i2lib.h"


//***********************
//* Function Prototypes *
//***********************
static void i2QueueNeeds(i2eBordStrPtr, i2ChanStrPtr, int);
static i2ChanStrPtr i2DeQueueNeeds(i2eBordStrPtr, int );
static void i2StripFifo(i2eBordStrPtr);
static void i2StuffFifoBypass(i2eBordStrPtr);
static void i2StuffFifoFlow(i2eBordStrPtr);
static void i2StuffFifoInline(i2eBordStrPtr);
static int i2RetryFlushOutput(i2ChanStrPtr);

// Not a documented part of the library routines (careful...) but the Diagnostic
// i2diag.c finds them useful to help the throughput in certain limited
// single-threaded operations.
static void iiSendPendingMail(i2eBordStrPtr);
static void serviceOutgoingFifo(i2eBordStrPtr);

// Functions defined in ip2.c as part of interrupt handling
static void do_input(struct work_struct *);
static void do_status(struct work_struct *);

//***************
//* Debug  Data *
//***************
#ifdef DEBUG_FIFO

unsigned char DBGBuf[0x4000];
unsigned short I = 0;

static void
WriteDBGBuf(char *s, unsigned char *src, unsigned short n ) 
{
	char *p = src;

	// XXX: We need a spin lock here if we ever use this again

	while (*s) {	// copy label
		DBGBuf[I] = *s++;
		I = I++ & 0x3fff;
	}
	while (n--) {	// copy data
		DBGBuf[I] = *p++;
		I = I++ & 0x3fff;
	}
}

static void
fatality(i2eBordStrPtr pB )
{
	int i;

	for (i=0;i<sizeof(DBGBuf);i++) {
		if ((i%16) == 0)
			printk("\n%4x:",i);
		printk("%02x ",DBGBuf[i]);
	}
	printk("\n");
	for (i=0;i<sizeof(DBGBuf);i++) {
		if ((i%16) == 0)
			printk("\n%4x:",i);
		if (DBGBuf[i] >= ' ' && DBGBuf[i] <= '~') {
			printk(" %c ",DBGBuf[i]);
		} else {
			printk(" . ");
		}
	}
	printk("\n");
	printk("Last index %x\n",I);
}
#endif /* DEBUG_FIFO */

//********
//* Code *
//********

static inline int
i2Validate ( i2ChanStrPtr pCh )
{
	//ip2trace(pCh->port_index, ITRC_VERIFY,ITRC_ENTER,2,pCh->validity,
	//	(CHANNEL_MAGIC | CHANNEL_SUPPORT));
	return ((pCh->validity & (CHANNEL_MAGIC_BITS | CHANNEL_SUPPORT)) 
			  == (CHANNEL_MAGIC | CHANNEL_SUPPORT));
}

static void iiSendPendingMail_t(unsigned long data)
{
	i2eBordStrPtr pB = (i2eBordStrPtr)data;

	iiSendPendingMail(pB);
}

//******************************************************************************
// Function:   iiSendPendingMail(pB)
// Parameters: Pointer to a board structure
// Returns:    Nothing
//
// Description:
// If any outgoing mail bits are set and there is outgoing mailbox is empty,
// send the mail and clear the bits.
//******************************************************************************
static void
iiSendPendingMail(i2eBordStrPtr pB)
{
	if (pB->i2eOutMailWaiting && (!pB->i2eWaitingForEmptyFifo) )
	{
		if (iiTrySendMail(pB, pB->i2eOutMailWaiting))
		{
			/* If we were already waiting for fifo to empty,
			 * or just sent MB_OUT_STUFFED, then we are
			 * still waiting for it to empty, until we should
			 * receive an MB_IN_STRIPPED from the board.
			 */
			pB->i2eWaitingForEmptyFifo |=
				(pB->i2eOutMailWaiting & MB_OUT_STUFFED);
			pB->i2eOutMailWaiting = 0;
			pB->SendPendingRetry = 0;
		} else {
/*		The only time we hit this area is when "iiTrySendMail" has
		failed.  That only occurs when the outbound mailbox is
		still busy with the last message.  We take a short breather
		to let the board catch up with itself and then try again.
		16 Retries is the limit - then we got a borked board.
			/\/\|=mhw=|\/\/				*/

			if( ++pB->SendPendingRetry < 16 ) {
				setup_timer(&pB->SendPendingTimer,
					iiSendPendingMail_t, (unsigned long)pB);
				mod_timer(&pB->SendPendingTimer, jiffies + 1);
			} else {
				printk( KERN_ERR "IP2: iiSendPendingMail unable to queue outbound mail\n" );
			}
		}
	}
}

//******************************************************************************
// Function:   i2InitChannels(pB, nChannels, pCh)
// Parameters: Pointer to Ellis Board structure
//             Number of channels to initialize
//             Pointer to first element in an array of channel structures
// Returns:    Success or failure
//
// Description:
//
// This function patches pointers, back-pointers, and initializes all the
// elements in the channel structure array.
//
// This should be run after the board structure is initialized, through having
// loaded the standard loadware (otherwise it complains).
//
// In any case, it must be done before any serious work begins initializing the
// irq's or sending commands...
//
//******************************************************************************
static int
i2InitChannels ( i2eBordStrPtr pB, int nChannels, i2ChanStrPtr pCh)
{
	int index, stuffIndex;
	i2ChanStrPtr *ppCh;
	
	if (pB->i2eValid != I2E_MAGIC) {
		I2_COMPLETE(pB, I2EE_BADMAGIC);
	}
	if (pB->i2eState != II_STATE_STDLOADED) {
		I2_COMPLETE(pB, I2EE_BADSTATE);
	}

	rwlock_init(&pB->read_fifo_spinlock);
	rwlock_init(&pB->write_fifo_spinlock);
	rwlock_init(&pB->Dbuf_spinlock);
	rwlock_init(&pB->Bbuf_spinlock);
	rwlock_init(&pB->Fbuf_spinlock);
	
	// NO LOCK needed yet - this is init

	pB->i2eChannelPtr = pCh;
	pB->i2eChannelCnt = nChannels;

	pB->i2Fbuf_strip = pB->i2Fbuf_stuff = 0;
	pB->i2Dbuf_strip = pB->i2Dbuf_stuff = 0;
	pB->i2Bbuf_strip = pB->i2Bbuf_stuff = 0;

	pB->SendPendingRetry = 0;

	memset ( pCh, 0, sizeof (i2ChanStr) * nChannels );

	for (index = stuffIndex = 0, ppCh = (i2ChanStrPtr *)(pB->i2Fbuf);
		  nChannels && index < ABS_MOST_PORTS;
		  index++)
	{
		if ( !(pB->i2eChannelMap[index >> 4] & (1 << (index & 0xf)) ) ) {
			continue;
		}
		rwlock_init(&pCh->Ibuf_spinlock);
		rwlock_init(&pCh->Obuf_spinlock);
		rwlock_init(&pCh->Cbuf_spinlock);
		rwlock_init(&pCh->Pbuf_spinlock);
		// NO LOCK needed yet - this is init
		// Set up validity flag according to support level
		if (pB->i2eGoodMap[index >> 4] & (1 << (index & 0xf)) ) {
			pCh->validity = CHANNEL_MAGIC | CHANNEL_SUPPORT;
		} else {
			pCh->validity = CHANNEL_MAGIC;
		}
		pCh->pMyBord = pB;      /* Back-pointer */

		// Prepare an outgoing flow-control packet to send as soon as the chance
		// occurs.
		if ( pCh->validity & CHANNEL_SUPPORT ) {
			pCh->infl.hd.i2sChannel = index;
			pCh->infl.hd.i2sCount = 5;
			pCh->infl.hd.i2sType = PTYPE_BYPASS;
			pCh->infl.fcmd = 37;
			pCh->infl.asof = 0;
			pCh->infl.room = IBUF_SIZE - 1;

			pCh->whenSendFlow = (IBUF_SIZE/5)*4; // when 80% full

		// The following is similar to calling i2QueueNeeds, except that this
		// is done in longhand, since we are setting up initial conditions on
		// many channels at once.
			pCh->channelNeeds = NEED_FLOW;  // Since starting from scratch
			pCh->sinceLastFlow = 0;         // No bytes received since last flow
											// control packet was queued
			stuffIndex++;
			*ppCh++ = pCh;      // List this channel as needing
								// initial flow control packet sent
		}

		// Don't allow anything to be sent until the status packets come in from
		// the board.

		pCh->outfl.asof = 0;
		pCh->outfl.room = 0;

		// Initialize all the ring buffers

		pCh->Ibuf_stuff = pCh->Ibuf_strip = 0;
		pCh->Obuf_stuff = pCh->Obuf_strip = 0;
		pCh->Cbuf_stuff = pCh->Cbuf_strip = 0;

		memset( &pCh->icount, 0, sizeof (struct async_icount) );
		pCh->hotKeyIn       = HOT_CLEAR;
		pCh->channelOptions = 0;
		pCh->bookMarks      = 0;
		init_waitqueue_head(&pCh->pBookmarkWait);

		init_waitqueue_head(&pCh->open_wait);
		init_waitqueue_head(&pCh->close_wait);
		init_waitqueue_head(&pCh->delta_msr_wait);

		// Set base and divisor so default custom rate is 9600
		pCh->BaudBase    = 921600;	// MAX for ST654, changed after we get
		pCh->BaudDivisor = 96;		// the boxids (UART types) later

		pCh->dataSetIn   = 0;
		pCh->dataSetOut  = 0;

		pCh->wopen       = 0;
		pCh->throttled   = 0;

		pCh->speed       = CBR_9600;

		pCh->flags    = 0;

		pCh->ClosingDelay     = 5*HZ/10;
		pCh->ClosingWaitTime  = 30*HZ;

		// Initialize task queue objects
		INIT_WORK(&pCh->tqueue_input, do_input);
		INIT_WORK(&pCh->tqueue_status, do_status);

#ifdef IP2DEBUG_TRACE
		pCh->trace = ip2trace;
#endif

		++pCh;
     	--nChannels;
	}
	// No need to check for wrap here; this is initialization.
	pB->i2Fbuf_stuff = stuffIndex;
	I2_COMPLETE(pB, I2EE_GOOD);

}

//******************************************************************************
// Function:   i2DeQueueNeeds(pB, type)
// Parameters: Pointer to a board structure
//             type bit map: may include NEED_INLINE, NEED_BYPASS, or NEED_FLOW
// Returns:   
//             Pointer to a channel structure
//
// Description: Returns pointer struct of next channel that needs service of
//  the type specified. Otherwise returns a NULL reference.
//
//******************************************************************************
static i2ChanStrPtr 
i2DeQueueNeeds(i2eBordStrPtr pB, int type)
{
	unsigned short queueIndex;
	unsigned long flags;

	i2ChanStrPtr pCh = NULL;

	switch(type) {

	case  NEED_INLINE:

		write_lock_irqsave(&pB->Dbuf_spinlock, flags);
		if ( pB->i2Dbuf_stuff != pB->i2Dbuf_strip)
		{
			queueIndex = pB->i2Dbuf_strip;
			pCh = pB->i2Dbuf[queueIndex];
			queueIndex++;
			if (queueIndex >= CH_QUEUE_SIZE) {
				queueIndex = 0;
			}
			pB->i2Dbuf_strip = queueIndex;
			pCh->channelNeeds &= ~NEED_INLINE;
		}
		write_unlock_irqrestore(&pB->Dbuf_spinlock, flags);
		break;

	case NEED_BYPASS:

		write_lock_irqsave(&pB->Bbuf_spinlock, flags);
		if (pB->i2Bbuf_stuff != pB->i2Bbuf_strip)
		{
			queueIndex = pB->i2Bbuf_strip;
			pCh = pB->i2Bbuf[queueIndex];
			queueIndex++;
			if (queueIndex >= CH_QUEUE_SIZE) {
				queueIndex = 0;
			}
			pB->i2Bbuf_strip = queueIndex;
			pCh->channelNeeds &= ~NEED_BYPASS;
		}
		write_unlock_irqrestore(&pB->Bbuf_spinlock, flags);
		break;
	
	case NEED_FLOW:

		write_lock_irqsave(&pB->Fbuf_spinlock, flags);
		if (pB->i2Fbuf_stuff != pB->i2Fbuf_strip)
		{
			queueIndex = pB->i2Fbuf_strip;
			pCh = pB->i2Fbuf[queueIndex];
			queueIndex++;
			if (queueIndex >= CH_QUEUE_SIZE) {
				queueIndex = 0;
			}
			pB->i2Fbuf_strip = queueIndex;
			pCh->channelNeeds &= ~NEED_FLOW;
		}
		write_unlock_irqrestore(&pB->Fbuf_spinlock, flags);
		break;
	default:
		printk(KERN_ERR "i2DeQueueNeeds called with bad type:%x\n",type);
		break;
	}
	return pCh;
}

//******************************************************************************
// Function:   i2QueueNeeds(pB, pCh, type)
// Parameters: Pointer to a board structure
//             Pointer to a channel structure
//             type bit map: may include NEED_INLINE, NEED_BYPASS, or NEED_FLOW
// Returns:    Nothing
//
// Description:
// For each type of need selected, if the given channel is not already in the
// queue, adds it, and sets the flag indicating it is in the queue.
//******************************************************************************
static void
i2QueueNeeds(i2eBordStrPtr pB, i2ChanStrPtr pCh, int type)
{
	unsigned short queueIndex;
	unsigned long flags;

	// We turn off all the interrupts during this brief process, since the
	// interrupt-level code might want to put things on the queue as well.

	switch (type) {

	case NEED_INLINE:

		write_lock_irqsave(&pB->Dbuf_spinlock, flags);
		if ( !(pCh->channelNeeds & NEED_INLINE) )
		{
			pCh->channelNeeds |= NEED_INLINE;
			queueIndex = pB->i2Dbuf_stuff;
			pB->i2Dbuf[queueIndex++] = pCh;
			if (queueIndex >= CH_QUEUE_SIZE)
				queueIndex = 0;
			pB->i2Dbuf_stuff = queueIndex;
		}
		write_unlock_irqrestore(&pB->Dbuf_spinlock, flags);
		break;

	case NEED_BYPASS:

		write_lock_irqsave(&pB->Bbuf_spinlock, flags);
		if ((type & NEED_BYPASS) && !(pCh->channelNeeds & NEED_BYPASS))
		{
			pCh->channelNeeds |= NEED_BYPASS;
			queueIndex = pB->i2Bbuf_stuff;
			pB->i2Bbuf[queueIndex++] = pCh;
			if (queueIndex >= CH_QUEUE_SIZE)
				queueIndex = 0;
			pB->i2Bbuf_stuff = queueIndex;
		} 
		write_unlock_irqrestore(&pB->Bbuf_spinlock, flags);
		break;

	case NEED_FLOW:

		write_lock_irqsave(&pB->Fbuf_spinlock, flags);
		if ((type & NEED_FLOW) && !(pCh->channelNeeds & NEED_FLOW))
		{
			pCh->channelNeeds |= NEED_FLOW;
			queueIndex = pB->i2Fbuf_stuff;
			pB->i2Fbuf[queueIndex++] = pCh;
			if (queueIndex >= CH_QUEUE_SIZE)
				queueIndex = 0;
			pB->i2Fbuf_stuff = queueIndex;
		}
		write_unlock_irqrestore(&pB->Fbuf_spinlock, flags);
		break;

	case NEED_CREDIT:
		pCh->channelNeeds |= NEED_CREDIT;
		break;
	default:
		printk(KERN_ERR "i2QueueNeeds called with bad type:%x\n",type);
		break;
	}
	return;
}

//******************************************************************************
// Function:   i2QueueCommands(type, pCh, timeout, nCommands, pCs,...)
// Parameters: type - PTYPE_BYPASS or PTYPE_INLINE
//             pointer to the channel structure
//             maximum period to wait
//             number of commands (n)
//             n commands
// Returns:    Number of commands sent, or -1 for error
//
// get board lock before calling
//
// Description:
// Queues up some commands to be sent to a channel. To send possibly several
// bypass or inline commands to the given channel. The timeout parameter
// indicates how many HUNDREDTHS OF SECONDS to wait until there is room:
// 0 = return immediately if no room, -ive  = wait forever, +ive = number of
// 1/100 seconds to wait. Return values:
// -1 Some kind of nasty error: bad channel structure or invalid arguments.
//  0 No room to send all the commands
// (+)   Number of commands sent
//******************************************************************************
static int
i2QueueCommands(int type, i2ChanStrPtr pCh, int timeout, int nCommands,
					 cmdSyntaxPtr pCs0,...)
{
	int totalsize = 0;
	int blocksize;
	int lastended;
	cmdSyntaxPtr *ppCs;
	cmdSyntaxPtr pCs;
	int count;
	int flag;
	i2eBordStrPtr pB;

	unsigned short maxBlock;
	unsigned short maxBuff;
	short bufroom;
	unsigned short stuffIndex;
	unsigned char *pBuf;
	unsigned char *pInsert;
	unsigned char *pDest, *pSource;
	unsigned short channel;
	int cnt;
	unsigned long flags = 0;
	rwlock_t *lock_var_p = NULL;

	// Make sure the channel exists, otherwise do nothing
	if ( !i2Validate ( pCh ) ) {
		return -1;
	}

	ip2trace (CHANN, ITRC_QUEUE, ITRC_ENTER, 0 );

	pB = pCh->pMyBord;

	// Board must also exist, and THE INTERRUPT COMMAND ALREADY SENT
	if (pB->i2eValid != I2E_MAGIC || pB->i2eUsingIrq == I2_IRQ_UNDEFINED)
		return -2;
	// If the board has gone fatal, return bad, and also hit the trap routine if
	// it exists.
	if (pB->i2eFatal) {
		if ( pB->i2eFatalTrap ) {
			(*(pB)->i2eFatalTrap)(pB);
		}
		return -3;
	}
	// Set up some variables, Which buffers are we using?  How big are they?
	switch(type)
	{
	case PTYPE_INLINE:
		flag = INL;
		maxBlock = MAX_OBUF_BLOCK;
		maxBuff = OBUF_SIZE;
		pBuf = pCh->Obuf;
		break;
	case PTYPE_BYPASS:
		flag = BYP;
		maxBlock = MAX_CBUF_BLOCK;
		maxBuff = CBUF_SIZE;
		pBuf = pCh->Cbuf;
		break;
	default:
		return -4;
	}
	// Determine the total size required for all the commands
	totalsize = blocksize = sizeof(i2CmdHeader);
	lastended = 0;
	ppCs = &pCs0;
	for ( count = nCommands; count; count--, ppCs++)
	{
		pCs = *ppCs;
		cnt = pCs->length;
		// Will a new block be needed for this one? 
		// Two possible reasons: too
		// big or previous command has to be at the end of a packet.
		if ((blocksize + cnt > maxBlock) || lastended) {
			blocksize = sizeof(i2CmdHeader);
			totalsize += sizeof(i2CmdHeader);
		}
		totalsize += cnt;
		blocksize += cnt;

		// If this command had to end a block, then we will make sure to
		// account for it should there be any more blocks.
		lastended = pCs->flags & END;
	}
	for (;;) {
		// Make sure any pending flush commands go out before we add more data.
		if ( !( pCh->flush_flags && i2RetryFlushOutput( pCh ) ) ) {
			// How much room (this time through) ?
			switch(type) {
			case PTYPE_INLINE:
				lock_var_p = &pCh->Obuf_spinlock;
				write_lock_irqsave(lock_var_p, flags);
				stuffIndex = pCh->Obuf_stuff;
				bufroom = pCh->Obuf_strip - stuffIndex;
				break;
			case PTYPE_BYPASS:
				lock_var_p = &pCh->Cbuf_spinlock;
				write_lock_irqsave(lock_var_p, flags);
				stuffIndex = pCh->Cbuf_stuff;
				bufroom = pCh->Cbuf_strip - stuffIndex;
				break;
			default:
				return -5;
			}
			if (--bufroom < 0) {
				bufroom += maxBuff;
			}

			ip2trace (CHANN, ITRC_QUEUE, 2, 1, bufroom );

			// Check for overflow
			if (totalsize <= bufroom) {
				// Normal Expected path - We still hold LOCK
				break; /* from for()- Enough room: goto proceed */
			}
			ip2trace(CHANN, ITRC_QUEUE, 3, 1, totalsize);
			write_unlock_irqrestore(lock_var_p, flags);
		} else
			ip2trace(CHANN, ITRC_QUEUE, 3, 1, totalsize);

		/* Prepare to wait for buffers to empty */
		serviceOutgoingFifo(pB);	// Dump what we got

		if (timeout == 0) {
			return 0;   // Tired of waiting
		}
		if (timeout > 0)
			timeout--;   // So negative values == forever
		
		if (!in_interrupt()) {
			schedule_timeout_interruptible(1);	// short nap
		} else {
			// we cannot sched/sleep in interrupt silly
			return 0;   
		}
		if (signal_pending(current)) {
			return 0;   // Wake up! Time to die!!!
		}

		ip2trace (CHANN, ITRC_QUEUE, 4, 0 );

	}	// end of for(;;)

	// At this point we have room and the lock - stick them in.
	channel = pCh->infl.hd.i2sChannel;
	pInsert = &pBuf[stuffIndex];     // Pointer to start of packet
	pDest = CMD_OF(pInsert);         // Pointer to start of command

	// When we start counting, the block is the size of the header
	for (blocksize = sizeof(i2CmdHeader), count = nCommands,
			lastended = 0, ppCs = &pCs0;
		count;
		count--, ppCs++)
	{
		pCs = *ppCs;         // Points to command protocol structure

		// If this is a bookmark request command, post the fact that a bookmark
		// request is pending. NOTE THIS TRICK ONLY WORKS BECAUSE CMD_BMARK_REQ
		// has no parameters!  The more general solution would be to reference
		// pCs->cmd[0].
		if (pCs == CMD_BMARK_REQ) {
			pCh->bookMarks++;

			ip2trace (CHANN, ITRC_DRAIN, 30, 1, pCh->bookMarks );

		}
		cnt = pCs->length;

		// If this command would put us over the maximum block size or 
		// if the last command had to be at the end of a block, we end
		// the existing block here and start a new one.
		if ((blocksize + cnt > maxBlock) || lastended) {

			ip2trace (CHANN, ITRC_QUEUE, 5, 0 );

			PTYPE_OF(pInsert) = type;
			CHANNEL_OF(pInsert) = channel;
			// count here does not include the header
			CMD_COUNT_OF(pInsert) = blocksize - sizeof(i2CmdHeader);
			stuffIndex += blocksize;
			if(stuffIndex >= maxBuff) {
				stuffIndex = 0;
				pInsert = pBuf;
			}
			pInsert = &pBuf[stuffIndex];  // Pointer to start of next pkt
			pDest = CMD_OF(pInsert);
			blocksize = sizeof(i2CmdHeader);
		}
		// Now we know there is room for this one in the current block

		blocksize += cnt;       // Total bytes in this command
		pSource = pCs->cmd;     // Copy the command into the buffer
		while (cnt--) {
			*pDest++ = *pSource++;
		}
		// If this command had to end a block, then we will make sure to account
		// for it should there be any more blocks.
		lastended = pCs->flags & END;
	}	// end for
	// Clean up the final block by writing header, etc

	PTYPE_OF(pInsert) = type;
	CHANNEL_OF(pInsert) = channel;
	// count here does not include the header
	CMD_COUNT_OF(pInsert) = blocksize - sizeof(i2CmdHeader);
	stuffIndex += blocksize;
	if(stuffIndex >= maxBuff) {
		stuffIndex = 0;
		pInsert = pBuf;
	}
	// Updates the index, and post the need for service. When adding these to
	// the queue of channels, we turn off the interrupt while doing so,
	// because at interrupt level we might want to push a channel back to the
	// end of the queue.
	switch(type)
	{
	case PTYPE_INLINE:
		pCh->Obuf_stuff = stuffIndex;  // Store buffer pointer
		write_unlock_irqrestore(&pCh->Obuf_spinlock, flags);

		pB->debugInlineQueued++;
		// Add the channel pointer to list of channels needing service (first
		// come...), if it's not already there.
		i2QueueNeeds(pB, pCh, NEED_INLINE);
		break;

	case PTYPE_BYPASS:
		pCh->Cbuf_stuff = stuffIndex;  // Store buffer pointer
		write_unlock_irqrestore(&pCh->Cbuf_spinlock, flags);

		pB->debugBypassQueued++;
		// Add the channel pointer to list of channels needing service (first
		// come...), if it's not already there.
		i2QueueNeeds(pB, pCh, NEED_BYPASS);
		break;
	}

	ip2trace (CHANN, ITRC_QUEUE, ITRC_RETURN, 1, nCommands );

	return nCommands; // Good status: number of commands sent
}

//******************************************************************************
// Function:   i2GetStatus(pCh,resetBits)
// Parameters: Pointer to a channel structure
//             Bit map of status bits to clear
// Returns:    Bit map of current status bits
//
// Description:
// Returns the state of data set signals, and whether a break has been received,
// (see i2lib.h for bit-mapped result). resetBits is a bit-map of any status
// bits to be cleared: I2_BRK, I2_PAR, I2_FRA, I2_OVR,... These are cleared
// AFTER the condition is passed. If pCh does not point to a valid channel,
// returns -1 (which would be impossible otherwise.
//******************************************************************************
static int
i2GetStatus(i2ChanStrPtr pCh, int resetBits)
{
	unsigned short status;
	i2eBordStrPtr pB;

	ip2trace (CHANN, ITRC_STATUS, ITRC_ENTER, 2, pCh->dataSetIn, resetBits );

	// Make sure the channel exists, otherwise do nothing */
	if ( !i2Validate ( pCh ) )
		return -1;

	pB = pCh->pMyBord;

	status = pCh->dataSetIn;

	// Clear any specified error bits: but note that only actual error bits can
	// be cleared, regardless of the value passed.
	if (resetBits)
	{
		pCh->dataSetIn &= ~(resetBits & (I2_BRK | I2_PAR | I2_FRA | I2_OVR));
		pCh->dataSetIn &= ~(I2_DDCD | I2_DCTS | I2_DDSR | I2_DRI);
	}

	ip2trace (CHANN, ITRC_STATUS, ITRC_RETURN, 1, pCh->dataSetIn );

	return status;
}

//******************************************************************************
// Function:   i2Input(pChpDest,count)
// Parameters: Pointer to a channel structure
//             Pointer to data buffer
//             Number of bytes to read
// Returns:    Number of bytes read, or -1 for error
//
// Description:
// Strips data from the input buffer and writes it to pDest. If there is a
// collosal blunder, (invalid structure pointers or the like), returns -1.
// Otherwise, returns the number of bytes read.
//******************************************************************************
static int
i2Input(i2ChanStrPtr pCh)
{
	int amountToMove;
	unsigned short stripIndex;
	int count;
	unsigned long flags = 0;

	ip2trace (CHANN, ITRC_INPUT, ITRC_ENTER, 0);

	// Ensure channel structure seems real
	if ( !i2Validate( pCh ) ) {
		count = -1;
		goto i2Input_exit;
	}
	write_lock_irqsave(&pCh->Ibuf_spinlock, flags);

	// initialize some accelerators and private copies
	stripIndex = pCh->Ibuf_strip;

	count = pCh->Ibuf_stuff - stripIndex;

	// If buffer is empty or requested data count was 0, (trivial case) return
	// without any further thought.
	if ( count == 0 ) {
		write_unlock_irqrestore(&pCh->Ibuf_spinlock, flags);
		goto i2Input_exit;
	}
	// Adjust for buffer wrap
	if ( count < 0 ) {
		count += IBUF_SIZE;
	}
	// Don't give more than can be taken by the line discipline
	amountToMove = pCh->pTTY->receive_room;
	if (count > amountToMove) {
		count = amountToMove;
	}
	// How much could we copy without a wrap?
	amountToMove = IBUF_SIZE - stripIndex;

	if (amountToMove > count) {
		amountToMove = count;
	}
	// Move the first block
	pCh->pTTY->ldisc.ops->receive_buf( pCh->pTTY,
		 &(pCh->Ibuf[stripIndex]), NULL, amountToMove );
	// If we needed to wrap, do the second data move
	if (count > amountToMove) {
		pCh->pTTY->ldisc.ops->receive_buf( pCh->pTTY,
		 pCh->Ibuf, NULL, count - amountToMove );
	}
	// Bump and wrap the stripIndex all at once by the amount of data read. This
	// method is good regardless of whether the data was in one or two pieces.
	stripIndex += count;
	if (stripIndex >= IBUF_SIZE) {
		stripIndex -= IBUF_SIZE;
	}
	pCh->Ibuf_strip = stripIndex;

	// Update our flow control information and possibly queue ourselves to send
	// it, depending on how much data has been stripped since the last time a
	// packet was sent.
	pCh->infl.asof += count;

	if ((pCh->sinceLastFlow += count) >= pCh->whenSendFlow) {
		pCh->sinceLastFlow -= pCh->whenSendFlow;
		write_unlock_irqrestore(&pCh->Ibuf_spinlock, flags);
		i2QueueNeeds(pCh->pMyBord, pCh, NEED_FLOW);
	} else {
		write_unlock_irqrestore(&pCh->Ibuf_spinlock, flags);
	}

i2Input_exit:

	ip2trace (CHANN, ITRC_INPUT, ITRC_RETURN, 1, count);

	return count;
}

//******************************************************************************
// Function:   i2InputFlush(pCh)
// Parameters: Pointer to a channel structure
// Returns:    Number of bytes stripped, or -1 for error
//
// Description:
// Strips any data from the input buffer. If there is a collosal blunder,
// (invalid structure pointers or the like), returns -1. Otherwise, returns the
// number of bytes stripped.
//******************************************************************************
static int
i2InputFlush(i2ChanStrPtr pCh)
{
	int count;
	unsigned long flags;

	// Ensure channel structure seems real
	if ( !i2Validate ( pCh ) )
		return -1;

	ip2trace (CHANN, ITRC_INPUT, 10, 0);

	write_lock_irqsave(&pCh->Ibuf_spinlock, flags);
	count = pCh->Ibuf_stuff - pCh->Ibuf_strip;

	// Adjust for buffer wrap
	if (count < 0) {
		count += IBUF_SIZE;
	}

	// Expedient way to zero out the buffer
	pCh->Ibuf_strip = pCh->Ibuf_stuff;


	// Update our flow control information and possibly queue ourselves to send
	// it, depending on how much data has been stripped since the last time a
	// packet was sent.

	pCh->infl.asof += count;

	if ( (pCh->sinceLastFlow += count) >= pCh->whenSendFlow )
	{
		pCh->sinceLastFlow -= pCh->whenSendFlow;
		write_unlock_irqrestore(&pCh->Ibuf_spinlock, flags);
		i2QueueNeeds(pCh->pMyBord, pCh, NEED_FLOW);
	} else {
		write_unlock_irqrestore(&pCh->Ibuf_spinlock, flags);
	}

	ip2trace (CHANN, ITRC_INPUT, 19, 1, count);

	return count;
}

//******************************************************************************
// Function:   i2InputAvailable(pCh)
// Parameters: Pointer to a channel structure
// Returns:    Number of bytes available, or -1 for error
//
// Description:
// If there is a collosal blunder, (invalid structure pointers or the like),
// returns -1. Otherwise, returns the number of bytes stripped. Otherwise,
// returns the number of bytes available in the buffer.
//******************************************************************************
#if 0
static int
i2InputAvailable(i2ChanStrPtr pCh)
{
	int count;

	// Ensure channel structure seems real
	if ( !i2Validate ( pCh ) ) return -1;


	// initialize some accelerators and private copies
	read_lock_irqsave(&pCh->Ibuf_spinlock, flags);
	count = pCh->Ibuf_stuff - pCh->Ibuf_strip;
	read_unlock_irqrestore(&pCh->Ibuf_spinlock, flags);

	// Adjust for buffer wrap
	if (count < 0)
	{
		count += IBUF_SIZE;
	}

	return count;
}
#endif 

//******************************************************************************
// Function:   i2Output(pCh, pSource, count)
// Parameters: Pointer to channel structure
//             Pointer to source data
//             Number of bytes to send
// Returns:    Number of bytes sent, or -1 for error
//
// Description:
// Queues the data at pSource to be sent as data packets to the board. If there
// is a collosal blunder, (invalid structure pointers or the like), returns -1.
// Otherwise, returns the number of bytes written. What if there is not enough
// room for all the data? If pCh->channelOptions & CO_NBLOCK_WRITE is set, then
// we transfer as many characters as we can now, then return. If this bit is
// clear (default), routine will spin along until all the data is buffered.
// Should this occur, the 1-ms delay routine is called while waiting to avoid
// applications that one cannot break out of.
//******************************************************************************
static int
i2Output(i2ChanStrPtr pCh, const char *pSource, int count)
{
	i2eBordStrPtr pB;
	unsigned char *pInsert;
	int amountToMove;
	int countOriginal = count;
	unsigned short channel;
	unsigned short stuffIndex;
	unsigned long flags;

	int bailout = 10;

	ip2trace (CHANN, ITRC_OUTPUT, ITRC_ENTER, 2, count, 0 );

	// Ensure channel structure seems real
	if ( !i2Validate ( pCh ) ) 
		return -1;

	// initialize some accelerators and private copies
	pB = pCh->pMyBord;
	channel = pCh->infl.hd.i2sChannel;

	// If the board has gone fatal, return bad, and also hit the trap routine if
	// it exists.
	if (pB->i2eFatal) {
		if (pB->i2eFatalTrap) {
			(*(pB)->i2eFatalTrap)(pB);
		}
		return -1;
	}
	// Proceed as though we would do everything
	while ( count > 0 ) {

		// How much room in output buffer is there?
		read_lock_irqsave(&pCh->Obuf_spinlock, flags);
		amountToMove = pCh->Obuf_strip - pCh->Obuf_stuff - 1;
		read_unlock_irqrestore(&pCh->Obuf_spinlock, flags);
		if (amountToMove < 0) {
			amountToMove += OBUF_SIZE;
		}
		// Subtract off the headers size and see how much room there is for real
		// data. If this is negative, we will discover later.
		amountToMove -= sizeof (i2DataHeader);

		// Don't move more (now) than can go in a single packet
		if ( amountToMove > (int)(MAX_OBUF_BLOCK - sizeof(i2DataHeader)) ) {
			amountToMove = MAX_OBUF_BLOCK - sizeof(i2DataHeader);
		}
		// Don't move more than the count we were given
		if (amountToMove > count) {
			amountToMove = count;
		}
		// Now we know how much we must move: NB because the ring buffers have
		// an overflow area at the end, we needn't worry about wrapping in the
		// middle of a packet.

// Small WINDOW here with no LOCK but I can't call Flush with LOCK
// We would be flushing (or ending flush) anyway

		ip2trace (CHANN, ITRC_OUTPUT, 10, 1, amountToMove );

		if ( !(pCh->flush_flags && i2RetryFlushOutput(pCh) ) 
				&& amountToMove > 0 )
		{
			write_lock_irqsave(&pCh->Obuf_spinlock, flags);
			stuffIndex = pCh->Obuf_stuff;
      
			// Had room to move some data: don't know whether the block size,
			// buffer space, or what was the limiting factor...
			pInsert = &(pCh->Obuf[stuffIndex]);

			// Set up the header
			CHANNEL_OF(pInsert)     = channel;
			PTYPE_OF(pInsert)       = PTYPE_DATA;
			TAG_OF(pInsert)         = 0;
			ID_OF(pInsert)          = ID_ORDINARY_DATA;
			DATA_COUNT_OF(pInsert)  = amountToMove;

			// Move the data
			memcpy( (char*)(DATA_OF(pInsert)), pSource, amountToMove );
			// Adjust pointers and indices
			pSource					+= amountToMove;
			pCh->Obuf_char_count	+= amountToMove;
			stuffIndex 				+= amountToMove + sizeof(i2DataHeader);
			count 					-= amountToMove;

			if (stuffIndex >= OBUF_SIZE) {
				stuffIndex = 0;
			}
			pCh->Obuf_stuff = stuffIndex;

			write_unlock_irqrestore(&pCh->Obuf_spinlock, flags);

			ip2trace (CHANN, ITRC_OUTPUT, 13, 1, stuffIndex );

		} else {

			// Cannot move data
			// becuz we need to stuff a flush 
			// or amount to move is <= 0

			ip2trace(CHANN, ITRC_OUTPUT, 14, 3,
				amountToMove,  pB->i2eFifoRemains,
				pB->i2eWaitingForEmptyFifo );

			// Put this channel back on queue
			// this ultimatly gets more data or wakes write output
			i2QueueNeeds(pB, pCh, NEED_INLINE);

			if ( pB->i2eWaitingForEmptyFifo ) {

				ip2trace (CHANN, ITRC_OUTPUT, 16, 0 );

				// or schedule
				if (!in_interrupt()) {

					ip2trace (CHANN, ITRC_OUTPUT, 61, 0 );

					schedule_timeout_interruptible(2);
					if (signal_pending(current)) {
						break;
					}
					continue;
				} else {

					ip2trace (CHANN, ITRC_OUTPUT, 62, 0 );

					// let interrupt in = WAS restore_flags()
					// We hold no lock nor is irq off anymore???
					
					break;
				}
				break;   // from while(count)
			}
			else if ( pB->i2eFifoRemains < 32 && !pB->i2eTxMailEmpty ( pB ) )
			{
				ip2trace (CHANN, ITRC_OUTPUT, 19, 2,
					pB->i2eFifoRemains,
					pB->i2eTxMailEmpty );

				break;   // from while(count)
			} else if ( pCh->channelNeeds & NEED_CREDIT ) {

				ip2trace (CHANN, ITRC_OUTPUT, 22, 0 );

				break;   // from while(count)
			} else if ( --bailout) {

				// Try to throw more things (maybe not us) in the fifo if we're
				// not already waiting for it.
	
				ip2trace (CHANN, ITRC_OUTPUT, 20, 0 );

				serviceOutgoingFifo(pB);
				//break;  CONTINUE;
			} else {
				ip2trace (CHANN, ITRC_OUTPUT, 21, 3,
					pB->i2eFifoRemains,
					pB->i2eOutMailWaiting,
					pB->i2eWaitingForEmptyFifo );

				break;   // from while(count)
			}
		}
	} // End of while(count)

	i2QueueNeeds(pB, pCh, NEED_INLINE);

	// We drop through either when the count expires, or when there is some
	// count left, but there was a non-blocking write.
	if (countOriginal > count) {

		ip2trace (CHANN, ITRC_OUTPUT, 17, 2, countOriginal, count );

		serviceOutgoingFifo( pB );
	}

	ip2trace (CHANN, ITRC_OUTPUT, ITRC_RETURN, 2, countOriginal, count );

	return countOriginal - count;
}

//******************************************************************************
// Function:   i2FlushOutput(pCh)
// Parameters: Pointer to a channel structure
// Returns:    Nothing
//
// Description:
// Sends bypass command to start flushing (waiting possibly forever until there
// is room), then sends inline command to stop flushing output, (again waiting
// possibly forever).
//******************************************************************************
static inline void
i2FlushOutput(i2ChanStrPtr pCh)
{

	ip2trace (CHANN, ITRC_FLUSH, 1, 1, pCh->flush_flags );

	if (pCh->flush_flags)
		return;

	if ( 1 != i2QueueCommands(PTYPE_BYPASS, pCh, 0, 1, CMD_STARTFL) ) {
		pCh->flush_flags = STARTFL_FLAG;		// Failed - flag for later

		ip2trace (CHANN, ITRC_FLUSH, 2, 0 );

	} else if ( 1 != i2QueueCommands(PTYPE_INLINE, pCh, 0, 1, CMD_STOPFL) ) {
		pCh->flush_flags = STOPFL_FLAG;		// Failed - flag for later

		ip2trace (CHANN, ITRC_FLUSH, 3, 0 );
	}
}

static int 
i2RetryFlushOutput(i2ChanStrPtr pCh)
{
	int old_flags = pCh->flush_flags;

	ip2trace (CHANN, ITRC_FLUSH, 14, 1, old_flags );

	pCh->flush_flags = 0;	// Clear flag so we can avoid recursion
									// and queue the commands

	if ( old_flags & STARTFL_FLAG ) {
		if ( 1 == i2QueueCommands(PTYPE_BYPASS, pCh, 0, 1, CMD_STARTFL) ) {
			old_flags = STOPFL_FLAG;	//Success - send stop flush
		} else {
			old_flags = STARTFL_FLAG;	//Failure - Flag for retry later
		}

		ip2trace (CHANN, ITRC_FLUSH, 15, 1, old_flags );

	}
	if ( old_flags & STOPFL_FLAG ) {
		if (1 == i2QueueCommands(PTYPE_INLINE, pCh, 0, 1, CMD_STOPFL)) {
			old_flags = 0;	// Success - clear flags
		}

		ip2trace (CHANN, ITRC_FLUSH, 16, 1, old_flags );
	}
	pCh->flush_flags = old_flags;

	ip2trace (CHANN, ITRC_FLUSH, 17, 1, old_flags );

	return old_flags;
}

//******************************************************************************
// Function:   i2DrainOutput(pCh,timeout)
// Parameters: Pointer to a channel structure
//             Maximum period to wait
// Returns:    ?
//
// Description:
// Uses the bookmark request command to ask the board to send a bookmark back as
// soon as all the data is completely sent.
//******************************************************************************
static void
i2DrainWakeup(unsigned long d)
{
	i2ChanStrPtr pCh = (i2ChanStrPtr)d;

	ip2trace (CHANN, ITRC_DRAIN, 10, 1, pCh->BookmarkTimer.expires );

	pCh->BookmarkTimer.expires = 0;
	wake_up_interruptible( &pCh->pBookmarkWait );
}

static void
i2DrainOutput(i2ChanStrPtr pCh, int timeout)
{
	wait_queue_t wait;
	i2eBordStrPtr pB;

	ip2trace (CHANN, ITRC_DRAIN, ITRC_ENTER, 1, pCh->BookmarkTimer.expires);

	pB = pCh->pMyBord;
	// If the board has gone fatal, return bad, 
	// and also hit the trap routine if it exists.
	if (pB->i2eFatal) {
		if (pB->i2eFatalTrap) {
			(*(pB)->i2eFatalTrap)(pB);
		}
		return;
	}
	if ((timeout > 0) && (pCh->BookmarkTimer.expires == 0 )) {
		// One per customer (channel)
		setup_timer(&pCh->BookmarkTimer, i2DrainWakeup,
				(unsigned long)pCh);

		ip2trace (CHANN, ITRC_DRAIN, 1, 1, pCh->BookmarkTimer.expires );

		mod_timer(&pCh->BookmarkTimer, jiffies + timeout);
	}
	
	i2QueueCommands( PTYPE_INLINE, pCh, -1, 1, CMD_BMARK_REQ );

	init_waitqueue_entry(&wait, current);
	add_wait_queue(&(pCh->pBookmarkWait), &wait);
	set_current_state( TASK_INTERRUPTIBLE );

	serviceOutgoingFifo( pB );
	
	schedule();	// Now we take our interruptible sleep on

	// Clean up the queue
	set_current_state( TASK_RUNNING );
	remove_wait_queue(&(pCh->pBookmarkWait), &wait);

	// if expires == 0 then timer poped, then do not need to del_timer
	if ((timeout > 0) && pCh->BookmarkTimer.expires && 
	                     time_before(jiffies, pCh->BookmarkTimer.expires)) {
		del_timer( &(pCh->BookmarkTimer) );
		pCh->BookmarkTimer.expires = 0;

		ip2trace (CHANN, ITRC_DRAIN, 3, 1, pCh->BookmarkTimer.expires );

	}
	ip2trace (CHANN, ITRC_DRAIN, ITRC_RETURN, 1, pCh->BookmarkTimer.expires );
	return;
}

//******************************************************************************
// Function:   i2OutputFree(pCh)
// Parameters: Pointer to a channel structure
// Returns:    Space in output buffer
//
// Description:
// Returns -1 if very gross error. Otherwise returns the amount of bytes still
// free in the output buffer.
//******************************************************************************
static int
i2OutputFree(i2ChanStrPtr pCh)
{
	int amountToMove;
	unsigned long flags;

	// Ensure channel structure seems real
	if ( !i2Validate ( pCh ) ) {
		return -1;
	}
	read_lock_irqsave(&pCh->Obuf_spinlock, flags);
	amountToMove = pCh->Obuf_strip - pCh->Obuf_stuff - 1;
	read_unlock_irqrestore(&pCh->Obuf_spinlock, flags);

	if (amountToMove < 0) {
		amountToMove += OBUF_SIZE;
	}
	// If this is negative, we will discover later
	amountToMove -= sizeof(i2DataHeader);

	return (amountToMove < 0) ? 0 : amountToMove;
}
static void

ip2_owake( PTTY tp)
{
	i2ChanStrPtr  pCh;

	if (tp == NULL) return;

	pCh = tp->driver_data;

	ip2trace (CHANN, ITRC_SICMD, 10, 2, tp->flags,
			(1 << TTY_DO_WRITE_WAKEUP) );

	tty_wakeup(tp);
}

static inline void
set_baud_params(i2eBordStrPtr pB) 
{
	int i,j;
	i2ChanStrPtr  *pCh;

	pCh = (i2ChanStrPtr *) pB->i2eChannelPtr;

	for (i = 0; i < ABS_MAX_BOXES; i++) {
		if (pB->channelBtypes.bid_value[i]) {
			if (BID_HAS_654(pB->channelBtypes.bid_value[i])) {
				for (j = 0; j < ABS_BIGGEST_BOX; j++) {
					if (pCh[i*16+j] == NULL)
						break;
					(pCh[i*16+j])->BaudBase    = 921600;	// MAX for ST654
					(pCh[i*16+j])->BaudDivisor = 96;
				}
			} else {	// has cirrus cd1400
				for (j = 0; j < ABS_BIGGEST_BOX; j++) {
					if (pCh[i*16+j] == NULL)
						break;
					(pCh[i*16+j])->BaudBase    = 115200;	// MAX for CD1400
					(pCh[i*16+j])->BaudDivisor = 12;
				}
			}
		}
	}
}

//******************************************************************************
// Function:   i2StripFifo(pB)
// Parameters: Pointer to a board structure
// Returns:    ?
//
// Description:
// Strips all the available data from the incoming FIFO, identifies the type of
// packet, and either buffers the data or does what needs to be done.
//
// Note there is no overflow checking here: if the board sends more data than it
// ought to, we will not detect it here, but blindly overflow...
//******************************************************************************

// A buffer for reading in blocks for unknown channels
static unsigned char junkBuffer[IBUF_SIZE];

// A buffer to read in a status packet. Because of the size of the count field
// for these things, the maximum packet size must be less than MAX_CMD_PACK_SIZE
static unsigned char cmdBuffer[MAX_CMD_PACK_SIZE + 4];

// This table changes the bit order from MSR order given by STAT_MODEM packet to
// status bits used in our library.
static char xlatDss[16] = {
0      | 0     | 0      | 0      ,
0      | 0     | 0      | I2_CTS ,
0      | 0     | I2_DSR | 0      ,
0      | 0     | I2_DSR | I2_CTS ,
0      | I2_RI | 0      | 0      ,
0      | I2_RI | 0      | I2_CTS ,
0      | I2_RI | I2_DSR | 0      ,
0      | I2_RI | I2_DSR | I2_CTS ,
I2_DCD | 0     | 0      | 0      ,
I2_DCD | 0     | 0      | I2_CTS ,
I2_DCD | 0     | I2_DSR | 0      ,
I2_DCD | 0     | I2_DSR | I2_CTS ,
I2_DCD | I2_RI | 0      | 0      ,
I2_DCD | I2_RI | 0      | I2_CTS ,
I2_DCD | I2_RI | I2_DSR | 0      ,
I2_DCD | I2_RI | I2_DSR | I2_CTS };

static inline void
i2StripFifo(i2eBordStrPtr pB)
{
	i2ChanStrPtr pCh;
	int channel;
	int count;
	unsigned short stuffIndex;
	int amountToRead;
	unsigned char *pc, *pcLimit;
	unsigned char uc;
	unsigned char dss_change;
	unsigned long bflags,cflags;

//	ip2trace (ITRC_NO_PORT, ITRC_SFIFO, ITRC_ENTER, 0 );

	while (I2_HAS_INPUT(pB)) {
//		ip2trace (ITRC_NO_PORT, ITRC_SFIFO, 2, 0 );

		// Process packet from fifo a one atomic unit
		write_lock_irqsave(&pB->read_fifo_spinlock, bflags);
   
		// The first word (or two bytes) will have channel number and type of
		// packet, possibly other information
		pB->i2eLeadoffWord[0] = iiReadWord(pB);

		switch(PTYPE_OF(pB->i2eLeadoffWord))
		{
		case PTYPE_DATA:
			pB->got_input = 1;

//			ip2trace (ITRC_NO_PORT, ITRC_SFIFO, 3, 0 );

			channel = CHANNEL_OF(pB->i2eLeadoffWord); /* Store channel */
			count = iiReadWord(pB);          /* Count is in the next word */

// NEW: Check the count for sanity! Should the hardware fail, our death
// is more pleasant. While an oversize channel is acceptable (just more
// than the driver supports), an over-length count clearly means we are
// sick!
			if ( ((unsigned int)count) > IBUF_SIZE ) {
				pB->i2eFatal = 2;
				write_unlock_irqrestore(&pB->read_fifo_spinlock,
						bflags);
				return;     /* Bail out ASAP */
			}
			// Channel is illegally big ?
			if ((channel >= pB->i2eChannelCnt) ||
				(NULL==(pCh = ((i2ChanStrPtr*)pB->i2eChannelPtr)[channel])))
			{
				iiReadBuf(pB, junkBuffer, count);
				write_unlock_irqrestore(&pB->read_fifo_spinlock,
						bflags);
				break;         /* From switch: ready for next packet */
			}

			// Channel should be valid, then

			// If this is a hot-key, merely post its receipt for now. These are
			// always supposed to be 1-byte packets, so we won't even check the
			// count. Also we will post an acknowledgement to the board so that
			// more data can be forthcoming. Note that we are not trying to use
			// these sequences in this driver, merely to robustly ignore them.
			if(ID_OF(pB->i2eLeadoffWord) == ID_HOT_KEY)
			{
				pCh->hotKeyIn = iiReadWord(pB) & 0xff;
				write_unlock_irqrestore(&pB->read_fifo_spinlock,
						bflags);
				i2QueueCommands(PTYPE_BYPASS, pCh, 0, 1, CMD_HOTACK);
				break;   /* From the switch: ready for next packet */
			}

			// Normal data! We crudely assume there is room for the data in our
			// buffer because the board wouldn't have exceeded his credit limit.
			write_lock_irqsave(&pCh->Ibuf_spinlock, cflags);
													// We have 2 locks now
			stuffIndex = pCh->Ibuf_stuff;
			amountToRead = IBUF_SIZE - stuffIndex;
			if (amountToRead > count)
				amountToRead = count;

			// stuffIndex would have been already adjusted so there would 
			// always be room for at least one, and count is always at least
			// one.

			iiReadBuf(pB, &(pCh->Ibuf[stuffIndex]), amountToRead);
			pCh->icount.rx += amountToRead;

			// Update the stuffIndex by the amount of data moved. Note we could
			// never ask for more data than would just fit. However, we might
			// have read in one more byte than we wanted because the read
			// rounds up to even bytes. If this byte is on the end of the
			// packet, and is padding, we ignore it. If the byte is part of
			// the actual data, we need to move it.

			stuffIndex += amountToRead;

			if (stuffIndex >= IBUF_SIZE) {
				if ((amountToRead & 1) && (count > amountToRead)) {
					pCh->Ibuf[0] = pCh->Ibuf[IBUF_SIZE];
					amountToRead++;
					stuffIndex = 1;
				} else {
					stuffIndex = 0;
				}
			}

			// If there is anything left over, read it as well
			if (count > amountToRead) {
				amountToRead = count - amountToRead;
				iiReadBuf(pB, &(pCh->Ibuf[stuffIndex]), amountToRead);
				pCh->icount.rx += amountToRead;
				stuffIndex += amountToRead;
			}

			// Update stuff index
			pCh->Ibuf_stuff = stuffIndex;
			write_unlock_irqrestore(&pCh->Ibuf_spinlock, cflags);
			write_unlock_irqrestore(&pB->read_fifo_spinlock,
					bflags);

#ifdef USE_IQ
			schedule_work(&pCh->tqueue_input);
#else
			do_input(&pCh->tqueue_input);
#endif

			// Note we do not need to maintain any flow-control credits at this
			// time:  if we were to increment .asof and decrement .room, there
			// would be no net effect. Instead, when we strip data, we will
			// increment .asof and leave .room unchanged.

			break;   // From switch: ready for next packet

		case PTYPE_STATUS:
			ip2trace (ITRC_NO_PORT, ITRC_SFIFO, 4, 0 );
      
			count = CMD_COUNT_OF(pB->i2eLeadoffWord);

			iiReadBuf(pB, cmdBuffer, count);
			// We can release early with buffer grab
			write_unlock_irqrestore(&pB->read_fifo_spinlock,
					bflags);

			pc = cmdBuffer;
			pcLimit = &(cmdBuffer[count]);

			while (pc < pcLimit) {
				channel = *pc++;

				ip2trace (channel, ITRC_SFIFO, 7, 2, channel, *pc );

				/* check for valid channel */
				if (channel < pB->i2eChannelCnt
					 && 
					 (pCh = (((i2ChanStrPtr*)pB->i2eChannelPtr)[channel])) != NULL
					)
				{
					dss_change = 0;

					switch (uc = *pc++)
					{
					/* Breaks and modem signals are easy: just update status */
					case STAT_CTS_UP:
						if ( !(pCh->dataSetIn & I2_CTS) )
						{
							pCh->dataSetIn |= I2_DCTS;
							pCh->icount.cts++;
							dss_change = 1;
						}
						pCh->dataSetIn |= I2_CTS;
						break;

					case STAT_CTS_DN:
						if ( pCh->dataSetIn & I2_CTS )
						{
							pCh->dataSetIn |= I2_DCTS;
							pCh->icount.cts++;
							dss_change = 1;
						}
						pCh->dataSetIn &= ~I2_CTS;
						break;

					case STAT_DCD_UP:
						ip2trace (channel, ITRC_MODEM, 1, 1, pCh->dataSetIn );

						if ( !(pCh->dataSetIn & I2_DCD) )
						{
							ip2trace (CHANN, ITRC_MODEM, 2, 0 );
							pCh->dataSetIn |= I2_DDCD;
							pCh->icount.dcd++;
							dss_change = 1;
						}
						pCh->dataSetIn |= I2_DCD;

						ip2trace (channel, ITRC_MODEM, 3, 1, pCh->dataSetIn );
						break;

					case STAT_DCD_DN:
						ip2trace (channel, ITRC_MODEM, 4, 1, pCh->dataSetIn );
						if ( pCh->dataSetIn & I2_DCD )
						{
							ip2trace (channel, ITRC_MODEM, 5, 0 );
							pCh->dataSetIn |= I2_DDCD;
							pCh->icount.dcd++;
							dss_change = 1;
						}
						pCh->dataSetIn &= ~I2_DCD;

						ip2trace (channel, ITRC_MODEM, 6, 1, pCh->dataSetIn );
						break;

					case STAT_DSR_UP:
						if ( !(pCh->dataSetIn & I2_DSR) )
						{
							pCh->dataSetIn |= I2_DDSR;
							pCh->icount.dsr++;
							dss_change = 1;
						}
						pCh->dataSetIn |= I2_DSR;
						break;

					case STAT_DSR_DN:
						if ( pCh->dataSetIn & I2_DSR )
						{
							pCh->dataSetIn |= I2_DDSR;
							pCh->icount.dsr++;
							dss_change = 1;
						}
						pCh->dataSetIn &= ~I2_DSR;
						break;

					case STAT_RI_UP:
						if ( !(pCh->dataSetIn & I2_RI) )
						{
							pCh->dataSetIn |= I2_DRI;
							pCh->icount.rng++;
							dss_change = 1;
						}
						pCh->dataSetIn |= I2_RI ;
						break;

					case STAT_RI_DN:
						// to be compat with serial.c
						//if ( pCh->dataSetIn & I2_RI )
						//{
						//	pCh->dataSetIn |= I2_DRI;
						//	pCh->icount.rng++; 
						//	dss_change = 1;
						//}
						pCh->dataSetIn &= ~I2_RI ;
						break;

					case STAT_BRK_DET:
						pCh->dataSetIn |= I2_BRK;
						pCh->icount.brk++;
						dss_change = 1;
						break;

					// Bookmarks? one less request we're waiting for
					case STAT_BMARK:
						pCh->bookMarks--;
						if (pCh->bookMarks <= 0 ) {
							pCh->bookMarks = 0;
							wake_up_interruptible( &pCh->pBookmarkWait );

						ip2trace (channel, ITRC_DRAIN, 20, 1, pCh->BookmarkTimer.expires );
						}
						break;

					// Flow control packets? Update the new credits, and if
					// someone was waiting for output, queue him up again.
					case STAT_FLOW:
						pCh->outfl.room =
							((flowStatPtr)pc)->room -
							(pCh->outfl.asof - ((flowStatPtr)pc)->asof);

						ip2trace (channel, ITRC_STFLW, 1, 1, pCh->outfl.room );

						if (pCh->channelNeeds & NEED_CREDIT)
						{
							ip2trace (channel, ITRC_STFLW, 2, 1, pCh->channelNeeds);

							pCh->channelNeeds &= ~NEED_CREDIT;
							i2QueueNeeds(pB, pCh, NEED_INLINE);
							if ( pCh->pTTY )
								ip2_owake(pCh->pTTY);
						}

						ip2trace (channel, ITRC_STFLW, 3, 1, pCh->channelNeeds);

						pc += sizeof(flowStat);
						break;

					/* Special packets: */
					/* Just copy the information into the channel structure */

					case STAT_STATUS:

						pCh->channelStatus = *((debugStatPtr)pc);
						pc += sizeof(debugStat);
						break;

					case STAT_TXCNT:

						pCh->channelTcount = *((cntStatPtr)pc);
						pc += sizeof(cntStat);
						break;

					case STAT_RXCNT:

						pCh->channelRcount = *((cntStatPtr)pc);
						pc += sizeof(cntStat);
						break;

					case STAT_BOXIDS:
						pB->channelBtypes = *((bidStatPtr)pc);
						pc += sizeof(bidStat);
						set_baud_params(pB);
						break;

					case STAT_HWFAIL:
						i2QueueCommands (PTYPE_INLINE, pCh, 0, 1, CMD_HW_TEST);
						pCh->channelFail = *((failStatPtr)pc);
						pc += sizeof(failStat);
						break;

					/* No explicit match? then
					 * Might be an error packet...
					 */
					default:
						switch (uc & STAT_MOD_ERROR)
						{
						case STAT_ERROR:
							if (uc & STAT_E_PARITY) {
								pCh->dataSetIn |= I2_PAR;
								pCh->icount.parity++;
							}
							if (uc & STAT_E_FRAMING){
								pCh->dataSetIn |= I2_FRA;
								pCh->icount.frame++;
							}
							if (uc & STAT_E_OVERRUN){
								pCh->dataSetIn |= I2_OVR;
								pCh->icount.overrun++;
							}
							break;

						case STAT_MODEM:
							// the answer to DSS_NOW request (not change)
							pCh->dataSetIn = (pCh->dataSetIn
								& ~(I2_RI | I2_CTS | I2_DCD | I2_DSR) )
								| xlatDss[uc & 0xf];
							wake_up_interruptible ( &pCh->dss_now_wait );
						default:
							break;
						}
					}  /* End of switch on status type */
					if (dss_change) {
#ifdef USE_IQ
						schedule_work(&pCh->tqueue_status);
#else
						do_status(&pCh->tqueue_status);
#endif
					}
				}
				else  /* Or else, channel is invalid */
				{
					// Even though the channel is invalid, we must test the
					// status to see how much additional data it has (to be
					// skipped)
					switch (*pc++)
					{
					case STAT_FLOW:
						pc += 4;    /* Skip the data */
						break;

					default:
						break;
					}
				}
			}  // End of while (there is still some status packet left)
			break;

		default: // Neither packet? should be impossible
			ip2trace (ITRC_NO_PORT, ITRC_SFIFO, 5, 1,
				PTYPE_OF(pB->i2eLeadoffWord) );
			write_unlock_irqrestore(&pB->read_fifo_spinlock,
					bflags);

			break;
		}  // End of switch on type of packets
	}	/*while(board I2_HAS_INPUT)*/

	ip2trace (ITRC_NO_PORT, ITRC_SFIFO, ITRC_RETURN, 0 );

	// Send acknowledgement to the board even if there was no data!
	pB->i2eOutMailWaiting |= MB_IN_STRIPPED;
	return;
}

//******************************************************************************
// Function:   i2Write2Fifo(pB,address,count)
// Parameters: Pointer to a board structure, source address, byte count
// Returns:    bytes written
//
// Description:
//  Writes count bytes to board io address(implied) from source
//  Adjusts count, leaves reserve for next time around bypass cmds
//******************************************************************************
static int
i2Write2Fifo(i2eBordStrPtr pB, unsigned char *source, int count,int reserve)
{
	int rc = 0;
	unsigned long flags;
	write_lock_irqsave(&pB->write_fifo_spinlock, flags);
	if (!pB->i2eWaitingForEmptyFifo) {
		if (pB->i2eFifoRemains > (count+reserve)) {
			pB->i2eFifoRemains -= count;
			iiWriteBuf(pB, source, count);
			pB->i2eOutMailWaiting |= MB_OUT_STUFFED;
			rc =  count;
		}
	}
	write_unlock_irqrestore(&pB->write_fifo_spinlock, flags);
	return rc;
}
//******************************************************************************
// Function:   i2StuffFifoBypass(pB)
// Parameters: Pointer to a board structure
// Returns:    Nothing
//
// Description:
// Stuffs as many bypass commands into the fifo as possible. This is simpler
// than stuffing data or inline commands to fifo, since we do not have
// flow-control to deal with.
//******************************************************************************
static inline void
i2StuffFifoBypass(i2eBordStrPtr pB)
{
	i2ChanStrPtr pCh;
	unsigned char *pRemove;
	unsigned short stripIndex;
	unsigned short packetSize;
	unsigned short paddedSize;
	unsigned short notClogged = 1;
	unsigned long flags;

	int bailout = 1000;

	// Continue processing so long as there are entries, or there is room in the
	// fifo. Each entry represents a channel with something to do.
	while ( --bailout && notClogged && 
			(NULL != (pCh = i2DeQueueNeeds(pB,NEED_BYPASS))))
	{
		write_lock_irqsave(&pCh->Cbuf_spinlock, flags);
		stripIndex = pCh->Cbuf_strip;

		// as long as there are packets for this channel...

		while (stripIndex != pCh->Cbuf_stuff) {
			pRemove = &(pCh->Cbuf[stripIndex]);
			packetSize = CMD_COUNT_OF(pRemove) + sizeof(i2CmdHeader);
			paddedSize = roundup(packetSize, 2);

			if (paddedSize > 0) {
				if ( 0 == i2Write2Fifo(pB, pRemove, paddedSize,0)) {
					notClogged = 0;	/* fifo full */
					i2QueueNeeds(pB, pCh, NEED_BYPASS);	// Put back on queue
					break;   // Break from the channel
				} 
			}
#ifdef DEBUG_FIFO
WriteDBGBuf("BYPS", pRemove, paddedSize);
#endif	/* DEBUG_FIFO */
			pB->debugBypassCount++;

			pRemove += packetSize;
			stripIndex += packetSize;
			if (stripIndex >= CBUF_SIZE) {
				stripIndex = 0;
				pRemove = pCh->Cbuf;
			}
		}
		// Done with this channel. Move to next, removing this one from 
		// the queue of channels if we cleaned it out (i.e., didn't get clogged.
		pCh->Cbuf_strip = stripIndex;
		write_unlock_irqrestore(&pCh->Cbuf_spinlock, flags);
	}  // Either clogged or finished all the work

#ifdef IP2DEBUG_TRACE
	if ( !bailout ) {
		ip2trace (ITRC_NO_PORT, ITRC_ERROR, 1, 0 );
	}
#endif
}

//******************************************************************************
// Function:   i2StuffFifoFlow(pB)
// Parameters: Pointer to a board structure
// Returns:    Nothing
//
// Description:
// Stuffs as many flow control packets into the fifo as possible. This is easier
// even than doing normal bypass commands, because there is always at most one
// packet, already assembled, for each channel.
//******************************************************************************
static inline void
i2StuffFifoFlow(i2eBordStrPtr pB)
{
	i2ChanStrPtr pCh;
	unsigned short paddedSize = roundup(sizeof(flowIn), 2);

	ip2trace (ITRC_NO_PORT, ITRC_SFLOW, ITRC_ENTER, 2,
		pB->i2eFifoRemains, paddedSize );

	// Continue processing so long as there are entries, or there is room in the
	// fifo. Each entry represents a channel with something to do.
	while ( (NULL != (pCh = i2DeQueueNeeds(pB,NEED_FLOW)))) {
		pB->debugFlowCount++;

		// NO Chan LOCK needed ???
		if ( 0 == i2Write2Fifo(pB,(unsigned char *)&(pCh->infl),paddedSize,0)) {
			break;
		}
#ifdef DEBUG_FIFO
		WriteDBGBuf("FLOW",(unsigned char *) &(pCh->infl), paddedSize);
#endif /* DEBUG_FIFO */

	}  // Either clogged or finished all the work

	ip2trace (ITRC_NO_PORT, ITRC_SFLOW, ITRC_RETURN, 0 );
}

//******************************************************************************
// Function:   i2StuffFifoInline(pB)
// Parameters: Pointer to a board structure
// Returns:    Nothing
//
// Description:
// Stuffs as much data and inline commands into the fifo as possible. This is
// the most complex fifo-stuffing operation, since there if now the channel
// flow-control issue to deal with.
//******************************************************************************
static inline void
i2StuffFifoInline(i2eBordStrPtr pB)
{
	i2ChanStrPtr pCh;
	unsigned char *pRemove;
	unsigned short stripIndex;
	unsigned short packetSize;
	unsigned short paddedSize;
	unsigned short notClogged = 1;
	unsigned short flowsize;
	unsigned long flags;

	int bailout  = 1000;
	int bailout2;

	ip2trace (ITRC_NO_PORT, ITRC_SICMD, ITRC_ENTER, 3, pB->i2eFifoRemains, 
			pB->i2Dbuf_strip, pB->i2Dbuf_stuff );

	// Continue processing so long as there are entries, or there is room in the
	// fifo. Each entry represents a channel with something to do.
	while ( --bailout && notClogged && 
			(NULL != (pCh = i2DeQueueNeeds(pB,NEED_INLINE))) )
	{
		write_lock_irqsave(&pCh->Obuf_spinlock, flags);
		stripIndex = pCh->Obuf_strip;

		ip2trace (CHANN, ITRC_SICMD, 3, 2, stripIndex, pCh->Obuf_stuff );

		// as long as there are packets for this channel...
		bailout2 = 1000;
		while ( --bailout2 && stripIndex != pCh->Obuf_stuff) {
			pRemove = &(pCh->Obuf[stripIndex]);

			// Must determine whether this be a data or command packet to
			// calculate correctly the header size and the amount of
			// flow-control credit this type of packet will use.
			if (PTYPE_OF(pRemove) == PTYPE_DATA) {
				flowsize = DATA_COUNT_OF(pRemove);
				packetSize = flowsize + sizeof(i2DataHeader);
			} else {
				flowsize = CMD_COUNT_OF(pRemove);
				packetSize = flowsize + sizeof(i2CmdHeader);
			}
			flowsize = CREDIT_USAGE(flowsize);
			paddedSize = roundup(packetSize, 2);

			ip2trace (CHANN, ITRC_SICMD, 4, 2, pB->i2eFifoRemains, paddedSize );

			// If we don't have enough credits from the board to send the data,
			// flag the channel that we are waiting for flow control credit, and
			// break out. This will clean up this channel and remove us from the
			// queue of hot things to do.

				ip2trace (CHANN, ITRC_SICMD, 5, 2, pCh->outfl.room, flowsize );

			if (pCh->outfl.room <= flowsize)	{
				// Do Not have the credits to send this packet.
				i2QueueNeeds(pB, pCh, NEED_CREDIT);
				notClogged = 0;
				break;   // So to do next channel
			}
			if ( (paddedSize > 0) 
				&& ( 0 == i2Write2Fifo(pB, pRemove, paddedSize, 128))) {
				// Do Not have room in fifo to send this packet.
				notClogged = 0;
				i2QueueNeeds(pB, pCh, NEED_INLINE);	
				break;   // Break from the channel
			}
#ifdef DEBUG_FIFO
WriteDBGBuf("DATA", pRemove, paddedSize);
#endif /* DEBUG_FIFO */
			pB->debugInlineCount++;

			pCh->icount.tx += flowsize;
			// Update current credits
			pCh->outfl.room -= flowsize;
			pCh->outfl.asof += flowsize;
			if (PTYPE_OF(pRemove) == PTYPE_DATA) {
				pCh->Obuf_char_count -= DATA_COUNT_OF(pRemove);
			}
			pRemove += packetSize;
			stripIndex += packetSize;

			ip2trace (CHANN, ITRC_SICMD, 6, 2, stripIndex, pCh->Obuf_strip);

			if (stripIndex >= OBUF_SIZE) {
				stripIndex = 0;
				pRemove = pCh->Obuf;

				ip2trace (CHANN, ITRC_SICMD, 7, 1, stripIndex );

			}
		}	/* while */
		if ( !bailout2 ) {
			ip2trace (CHANN, ITRC_ERROR, 3, 0 );
		}
		// Done with this channel. Move to next, removing this one from the
		// queue of channels if we cleaned it out (i.e., didn't get clogged.
		pCh->Obuf_strip = stripIndex;
		write_unlock_irqrestore(&pCh->Obuf_spinlock, flags);
		if ( notClogged )
		{

			ip2trace (CHANN, ITRC_SICMD, 8, 0 );

			if ( pCh->pTTY ) {
				ip2_owake(pCh->pTTY);
			}
		}
	}  // Either clogged or finished all the work

	if ( !bailout ) {
		ip2trace (ITRC_NO_PORT, ITRC_ERROR, 4, 0 );
	}

	ip2trace (ITRC_NO_PORT, ITRC_SICMD, ITRC_RETURN, 1,pB->i2Dbuf_strip);
}

//******************************************************************************
// Function:   serviceOutgoingFifo(pB)
// Parameters: Pointer to a board structure
// Returns:    Nothing
//
// Description:
// Helper routine to put data in the outgoing fifo, if we aren't already waiting
// for something to be there. If the fifo has only room for a very little data,
// go head and hit the board with a mailbox hit immediately. Otherwise, it will
// have to happen later in the interrupt processing. Since this routine may be
// called both at interrupt and foreground time, we must turn off interrupts
// during the entire process.
//******************************************************************************
static void
serviceOutgoingFifo(i2eBordStrPtr pB)
{
	// If we aren't currently waiting for the board to empty our fifo, service
	// everything that is pending, in priority order (especially, Bypass before
	// Inline).
	if ( ! pB->i2eWaitingForEmptyFifo )
	{
		i2StuffFifoFlow(pB);
		i2StuffFifoBypass(pB);
		i2StuffFifoInline(pB);

		iiSendPendingMail(pB);
	} 
}

//******************************************************************************
// Function:   i2ServiceBoard(pB)
// Parameters: Pointer to a board structure
// Returns:    Nothing
//
// Description:
// Normally this is called from interrupt level, but there is deliberately
// nothing in here specific to being called from interrupt level. All the
// hardware-specific, interrupt-specific things happen at the outer levels.
//
// For example, a timer interrupt could drive this routine for some sort of
// polled operation. The only requirement is that the programmer deal with any
// atomiticity/concurrency issues that result.
//
// This routine responds to the board's having sent mailbox information to the
// host (which would normally cause an interrupt). This routine reads the
// incoming mailbox. If there is no data in it, this board did not create the
// interrupt and/or has nothing to be done to it. (Except, if we have been
// waiting to write mailbox data to it, we may do so.
//
// Based on the value in the mailbox, we may take various actions.
//
// No checking here of pB validity: after all, it shouldn't have been called by
// the handler unless pB were on the list.
//******************************************************************************
static inline int
i2ServiceBoard ( i2eBordStrPtr pB )
{
	unsigned inmail;
	unsigned long flags;


	/* This should be atomic because of the way we are called... */
	if (NO_MAIL_HERE == ( inmail = pB->i2eStartMail ) ) {
		inmail = iiGetMail(pB);
	}
	pB->i2eStartMail = NO_MAIL_HERE;

	ip2trace (ITRC_NO_PORT, ITRC_INTR, 2, 1, inmail );

	if (inmail != NO_MAIL_HERE) {
		// If the board has gone fatal, nothing to do but hit a bit that will
		// alert foreground tasks to protest!
		if ( inmail & MB_FATAL_ERROR ) {
			pB->i2eFatal = 1;
			goto exit_i2ServiceBoard;
		}

		/* Assuming no fatal condition, we proceed to do work */
		if ( inmail & MB_IN_STUFFED ) {
			pB->i2eFifoInInts++;
			i2StripFifo(pB);     /* There might be incoming packets */
		}

		if (inmail & MB_OUT_STRIPPED) {
			pB->i2eFifoOutInts++;
			write_lock_irqsave(&pB->write_fifo_spinlock, flags);
			pB->i2eFifoRemains = pB->i2eFifoSize;
			pB->i2eWaitingForEmptyFifo = 0;
			write_unlock_irqrestore(&pB->write_fifo_spinlock,
					flags);

			ip2trace (ITRC_NO_PORT, ITRC_INTR, 30, 1, pB->i2eFifoRemains );

		}
		serviceOutgoingFifo(pB);
	}

	ip2trace (ITRC_NO_PORT, ITRC_INTR, 8, 0 );

exit_i2ServiceBoard:

	return 0;
}

/*
 *  ipmi_bt_sm.c
 *
 *  The state machine for an Open IPMI BT sub-driver under ipmi_si.c, part
 *  of the driver architecture at http://sourceforge.net/project/openipmi
 *
 *  Author:	Rocky Craig <first.last@hp.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.  */

#include <linux/kernel.h> /* For printk. */
#include <linux/string.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/ipmi_msgdefs.h>		/* for completion codes */
#include "ipmi_si_sm.h"

static int bt_debug = 0x00;	/* Production value 0, see following flags */

#define	BT_DEBUG_ENABLE	1
#define BT_DEBUG_MSG	2
#define BT_DEBUG_STATES	4
module_param(bt_debug, int, 0644);
MODULE_PARM_DESC(bt_debug, "debug bitmask, 1=enable, 2=messages, 4=states");

/* Typical "Get BT Capabilities" values are 2-3 retries, 5-10 seconds,
   and 64 byte buffers.  However, one HP implementation wants 255 bytes of
   buffer (with a documented message of 160 bytes) so go for the max.
   Since the Open IPMI architecture is single-message oriented at this
   stage, the queue depth of BT is of no concern. */

#define BT_NORMAL_TIMEOUT	5000000	/* seconds in microseconds */
#define BT_RETRY_LIMIT		2
#define BT_RESET_DELAY		6000000	/* 6 seconds after warm reset */

enum bt_states {
	BT_STATE_IDLE,
	BT_STATE_XACTION_START,
	BT_STATE_WRITE_BYTES,
	BT_STATE_WRITE_END,
	BT_STATE_WRITE_CONSUME,
	BT_STATE_B2H_WAIT,
	BT_STATE_READ_END,
	BT_STATE_RESET1,		/* These must come last */
	BT_STATE_RESET2,
	BT_STATE_RESET3,
	BT_STATE_RESTART,
	BT_STATE_HOSED
};

struct si_sm_data {
	enum bt_states	state;
	enum bt_states	last_state;	/* assist printing and resets */
	unsigned char	seq;		/* BT sequence number */
	struct si_sm_io	*io;
        unsigned char	write_data[IPMI_MAX_MSG_LENGTH];
        int		write_count;
        unsigned char	read_data[IPMI_MAX_MSG_LENGTH];
        int		read_count;
        int		truncated;
        long		timeout;
        unsigned int	error_retries;	/* end of "common" fields */
	int		nonzero_status;	/* hung BMCs stay all 0 */
};

#define BT_CLR_WR_PTR	0x01	/* See IPMI 1.5 table 11.6.4 */
#define BT_CLR_RD_PTR	0x02
#define BT_H2B_ATN	0x04
#define BT_B2H_ATN	0x08
#define BT_SMS_ATN	0x10
#define BT_OEM0		0x20
#define BT_H_BUSY	0x40
#define BT_B_BUSY	0x80

/* Some bits are toggled on each write: write once to set it, once
   more to clear it; writing a zero does nothing.  To absolutely
   clear it, check its state and write if set.  This avoids the "get
   current then use as mask" scheme to modify one bit.  Note that the
   variable "bt" is hardcoded into these macros. */

#define BT_STATUS	bt->io->inputb(bt->io, 0)
#define BT_CONTROL(x)	bt->io->outputb(bt->io, 0, x)

#define BMC2HOST	bt->io->inputb(bt->io, 1)
#define HOST2BMC(x)	bt->io->outputb(bt->io, 1, x)

#define BT_INTMASK_R	bt->io->inputb(bt->io, 2)
#define BT_INTMASK_W(x)	bt->io->outputb(bt->io, 2, x)

/* Convenience routines for debugging.  These are not multi-open safe!
   Note the macros have hardcoded variables in them. */

static char *state2txt(unsigned char state)
{
	switch (state) {
		case BT_STATE_IDLE:		return("IDLE");
		case BT_STATE_XACTION_START:	return("XACTION");
		case BT_STATE_WRITE_BYTES:	return("WR_BYTES");
		case BT_STATE_WRITE_END:	return("WR_END");
		case BT_STATE_WRITE_CONSUME:	return("WR_CONSUME");
		case BT_STATE_B2H_WAIT:		return("B2H_WAIT");
		case BT_STATE_READ_END:		return("RD_END");
		case BT_STATE_RESET1:		return("RESET1");
		case BT_STATE_RESET2:		return("RESET2");
		case BT_STATE_RESET3:		return("RESET3");
		case BT_STATE_RESTART:		return("RESTART");
		case BT_STATE_HOSED:		return("HOSED");
	}
	return("BAD STATE");
}
#define STATE2TXT state2txt(bt->state)

static char *status2txt(unsigned char status, char *buf)
{
	strcpy(buf, "[ ");
	if (status & BT_B_BUSY) strcat(buf, "B_BUSY ");
	if (status & BT_H_BUSY) strcat(buf, "H_BUSY ");
	if (status & BT_OEM0) strcat(buf, "OEM0 ");
	if (status & BT_SMS_ATN) strcat(buf, "SMS ");
	if (status & BT_B2H_ATN) strcat(buf, "B2H ");
	if (status & BT_H2B_ATN) strcat(buf, "H2B ");
	strcat(buf, "]");
	return buf;
}
#define STATUS2TXT(buf) status2txt(status, buf)

/* This will be called from within this module on a hosed condition */
#define FIRST_SEQ	0
static unsigned int bt_init_data(struct si_sm_data *bt, struct si_sm_io *io)
{
	bt->state = BT_STATE_IDLE;
	bt->last_state = BT_STATE_IDLE;
	bt->seq = FIRST_SEQ;
	bt->io = io;
	bt->write_count = 0;
	bt->read_count = 0;
	bt->error_retries = 0;
	bt->nonzero_status = 0;
	bt->truncated = 0;
	bt->timeout = BT_NORMAL_TIMEOUT;
	return 3; /* We claim 3 bytes of space; ought to check SPMI table */
}

static int bt_start_transaction(struct si_sm_data *bt,
				unsigned char *data,
				unsigned int size)
{
	unsigned int i;

	if ((size < 2) || (size > (IPMI_MAX_MSG_LENGTH - 2)))
	       return -1;

	if ((bt->state != BT_STATE_IDLE) && (bt->state != BT_STATE_HOSED))
		return -2;

	if (bt_debug & BT_DEBUG_MSG) {
    		printk(KERN_WARNING "+++++++++++++++++++++++++++++++++++++\n");
		printk(KERN_WARNING "BT: write seq=0x%02X:", bt->seq);
		for (i = 0; i < size; i ++)
		       printk (" %02x", data[i]);
		printk("\n");
	}
	bt->write_data[0] = size + 1;	/* all data plus seq byte */
	bt->write_data[1] = *data;	/* NetFn/LUN */
	bt->write_data[2] = bt->seq;
	memcpy(bt->write_data + 3, data + 1, size - 1);
	bt->write_count = size + 2;

	bt->error_retries = 0;
	bt->nonzero_status = 0;
	bt->read_count = 0;
	bt->truncated = 0;
	bt->state = BT_STATE_XACTION_START;
	bt->last_state = BT_STATE_IDLE;
	bt->timeout = BT_NORMAL_TIMEOUT;
	return 0;
}

/* After the upper state machine has been told SI_SM_TRANSACTION_COMPLETE
   it calls this.  Strip out the length and seq bytes. */

static int bt_get_result(struct si_sm_data *bt,
			   unsigned char *data,
			   unsigned int length)
{
	int i, msg_len;

	msg_len = bt->read_count - 2;		/* account for length & seq */
	/* Always NetFn, Cmd, cCode */
	if (msg_len < 3 || msg_len > IPMI_MAX_MSG_LENGTH) {
		printk(KERN_DEBUG "BT results: bad msg_len = %d\n", msg_len);
		data[0] = bt->write_data[1] | 0x4;	/* Kludge a response */
		data[1] = bt->write_data[3];
		data[2] = IPMI_ERR_UNSPECIFIED;
		msg_len = 3;
	} else {
		data[0] = bt->read_data[1];
		data[1] = bt->read_data[3];
		if (length < msg_len)
		       bt->truncated = 1;
		if (bt->truncated) {	/* can be set in read_all_bytes() */
			data[2] = IPMI_ERR_MSG_TRUNCATED;
			msg_len = 3;
		} else
		       memcpy(data + 2, bt->read_data + 4, msg_len - 2);

		if (bt_debug & BT_DEBUG_MSG) {
			printk (KERN_WARNING "BT: res (raw)");
			for (i = 0; i < msg_len; i++)
			       printk(" %02x", data[i]);
			printk ("\n");
		}
	}
	bt->read_count = 0;	/* paranoia */
	return msg_len;
}

/* This bit's functionality is optional */
#define BT_BMC_HWRST	0x80

static void reset_flags(struct si_sm_data *bt)
{
	if (BT_STATUS & BT_H_BUSY)
	       BT_CONTROL(BT_H_BUSY);
	if (BT_STATUS & BT_B_BUSY)
	       BT_CONTROL(BT_B_BUSY);
	BT_CONTROL(BT_CLR_WR_PTR);
	BT_CONTROL(BT_SMS_ATN);

	if (BT_STATUS & BT_B2H_ATN) {
		int i;
		BT_CONTROL(BT_H_BUSY);
		BT_CONTROL(BT_B2H_ATN);
		BT_CONTROL(BT_CLR_RD_PTR);
		for (i = 0; i < IPMI_MAX_MSG_LENGTH + 2; i++)
		       BMC2HOST;
		BT_CONTROL(BT_H_BUSY);
	}
}

static inline void write_all_bytes(struct si_sm_data *bt)
{
	int i;

	if (bt_debug & BT_DEBUG_MSG) {
    		printk(KERN_WARNING "BT: write %d bytes seq=0x%02X",
			bt->write_count, bt->seq);
		for (i = 0; i < bt->write_count; i++)
			printk (" %02x", bt->write_data[i]);
		printk ("\n");
	}
	for (i = 0; i < bt->write_count; i++)
	       HOST2BMC(bt->write_data[i]);
}

static inline int read_all_bytes(struct si_sm_data *bt)
{
	unsigned char i;

	bt->read_data[0] = BMC2HOST;
	bt->read_count = bt->read_data[0];
	if (bt_debug & BT_DEBUG_MSG)
    		printk(KERN_WARNING "BT: read %d bytes:", bt->read_count);

	/* minimum: length, NetFn, Seq, Cmd, cCode == 5 total, or 4 more
	   following the length byte. */
	if (bt->read_count < 4 || bt->read_count >= IPMI_MAX_MSG_LENGTH) {
		if (bt_debug & BT_DEBUG_MSG)
			printk("bad length %d\n", bt->read_count);
		bt->truncated = 1;
		return 1;	/* let next XACTION START clean it up */
	}
	for (i = 1; i <= bt->read_count; i++)
	       bt->read_data[i] = BMC2HOST;
	bt->read_count++;	/* account for the length byte */

	if (bt_debug & BT_DEBUG_MSG) {
	    	for (i = 0; i < bt->read_count; i++)
			printk (" %02x", bt->read_data[i]);
	    	printk ("\n");
	}
	if (bt->seq != bt->write_data[2])	/* idiot check */
		printk(KERN_DEBUG "BT: internal error: sequence mismatch\n");

	/* per the spec, the (NetFn, Seq, Cmd) tuples should match */
	if ((bt->read_data[3] == bt->write_data[3]) &&		/* Cmd */
        	(bt->read_data[2] == bt->write_data[2]) &&	/* Sequence */
        	((bt->read_data[1] & 0xF8) == (bt->write_data[1] & 0xF8)))
			return 1;

	if (bt_debug & BT_DEBUG_MSG)
	       printk(KERN_WARNING "BT: bad packet: "
		"want 0x(%02X, %02X, %02X) got (%02X, %02X, %02X)\n",
		bt->write_data[1], bt->write_data[2], bt->write_data[3],
		bt->read_data[1],  bt->read_data[2],  bt->read_data[3]);
	return 0;
}

/* Modifies bt->state appropriately, need to get into the bt_event() switch */

static void error_recovery(struct si_sm_data *bt, char *reason)
{
	unsigned char status;
	char buf[40]; /* For getting status */

	bt->timeout = BT_NORMAL_TIMEOUT; /* various places want to retry */

	status = BT_STATUS;
	printk(KERN_DEBUG "BT: %s in %s %s\n", reason, STATE2TXT,
	       STATUS2TXT(buf));

	(bt->error_retries)++;
	if (bt->error_retries > BT_RETRY_LIMIT) {
		printk(KERN_DEBUG "retry limit (%d) exceeded\n", BT_RETRY_LIMIT);
		bt->state = BT_STATE_HOSED;
		if (!bt->nonzero_status)
			printk(KERN_ERR "IPMI: BT stuck, try power cycle\n");
		else if (bt->error_retries <= BT_RETRY_LIMIT + 1) {
			printk(KERN_DEBUG "IPMI: BT reset (takes 5 secs)\n");
        		bt->state = BT_STATE_RESET1;
		}
	return;
	}

	/* Sometimes the BMC queues get in an "off-by-one" state...*/
	if ((bt->state == BT_STATE_B2H_WAIT) && (status & BT_B2H_ATN)) {
    		printk(KERN_DEBUG "retry B2H_WAIT\n");
		return;
	}

	printk(KERN_DEBUG "restart command\n");
	bt->state = BT_STATE_RESTART;
}

/* Check the status and (possibly) advance the BT state machine.  The
   default return is SI_SM_CALL_WITH_DELAY. */

static enum si_sm_result bt_event(struct si_sm_data *bt, long time)
{
	unsigned char status;
	char buf[40]; /* For getting status */
	int i;

	status = BT_STATUS;
	bt->nonzero_status |= status;

	if ((bt_debug & BT_DEBUG_STATES) && (bt->state != bt->last_state))
		printk(KERN_WARNING "BT: %s %s TO=%ld - %ld \n",
			STATE2TXT,
			STATUS2TXT(buf),
			bt->timeout,
			time);
	bt->last_state = bt->state;

	if (bt->state == BT_STATE_HOSED)
	       return SI_SM_HOSED;

	if (bt->state != BT_STATE_IDLE) {	/* do timeout test */
		bt->timeout -= time;
		if ((bt->timeout < 0) && (bt->state < BT_STATE_RESET1)) {
			error_recovery(bt, "timed out");
			return SI_SM_CALL_WITHOUT_DELAY;
		}
	}

	switch (bt->state) {

    	case BT_STATE_IDLE:	/* check for asynchronous messages */
		if (status & BT_SMS_ATN) {
			BT_CONTROL(BT_SMS_ATN);	/* clear it */
			return SI_SM_ATTN;
		}
		return SI_SM_IDLE;

	case BT_STATE_XACTION_START:
		if (status & BT_H_BUSY) {
			BT_CONTROL(BT_H_BUSY);
			break;
		}
    		if (status & BT_B2H_ATN)
		       break;
		bt->state = BT_STATE_WRITE_BYTES;
		return SI_SM_CALL_WITHOUT_DELAY;	/* for logging */

	case BT_STATE_WRITE_BYTES:
		if (status & (BT_B_BUSY | BT_H2B_ATN))
		       break;
		BT_CONTROL(BT_CLR_WR_PTR);
		write_all_bytes(bt);
		BT_CONTROL(BT_H2B_ATN);	/* clears too fast to catch? */
		bt->state = BT_STATE_WRITE_CONSUME;
		return SI_SM_CALL_WITHOUT_DELAY; /* it MIGHT sail through */

	case BT_STATE_WRITE_CONSUME: /* BMCs usually blow right thru here */
        	if (status & (BT_H2B_ATN | BT_B_BUSY))
		       break;
		bt->state = BT_STATE_B2H_WAIT;
		/* fall through with status */

	/* Stay in BT_STATE_B2H_WAIT until a packet matches.  However, spinning
	   hard here, constantly reading status, seems to hold off the
	   generation of B2H_ATN so ALWAYS return CALL_WITH_DELAY. */

	case BT_STATE_B2H_WAIT:
    		if (!(status & BT_B2H_ATN))
		       break;

		/* Assume ordered, uncached writes: no need to wait */
		if (!(status & BT_H_BUSY))
		       BT_CONTROL(BT_H_BUSY); /* set */
		BT_CONTROL(BT_B2H_ATN);		/* clear it, ACK to the BMC */
		BT_CONTROL(BT_CLR_RD_PTR);	/* reset the queue */
		i = read_all_bytes(bt);
		BT_CONTROL(BT_H_BUSY);		/* clear */
		if (!i)				/* Try this state again */
		       break;
		bt->state = BT_STATE_READ_END;
		return SI_SM_CALL_WITHOUT_DELAY;	/* for logging */

    	case BT_STATE_READ_END:

		/* I could wait on BT_H_BUSY to go clear for a truly clean
		   exit.  However, this is already done in XACTION_START
		   and the (possible) extra loop/status/possible wait affects
		   performance.  So, as long as it works, just ignore H_BUSY */

#ifdef MAKE_THIS_TRUE_IF_NECESSARY

		if (status & BT_H_BUSY)
		       break;
#endif
		bt->seq++;
		bt->state = BT_STATE_IDLE;
		return SI_SM_TRANSACTION_COMPLETE;

	case BT_STATE_RESET1:
    		reset_flags(bt);
    		bt->timeout = BT_RESET_DELAY;
		bt->state = BT_STATE_RESET2;
		break;

	case BT_STATE_RESET2:		/* Send a soft reset */
		BT_CONTROL(BT_CLR_WR_PTR);
		HOST2BMC(3);		/* number of bytes following */
		HOST2BMC(0x18);		/* NetFn/LUN == Application, LUN 0 */
		HOST2BMC(42);		/* Sequence number */
		HOST2BMC(3);		/* Cmd == Soft reset */
		BT_CONTROL(BT_H2B_ATN);
		bt->state = BT_STATE_RESET3;
		break;

	case BT_STATE_RESET3:
		if (bt->timeout > 0)
		       return SI_SM_CALL_WITH_DELAY;
		bt->state = BT_STATE_RESTART;	/* printk in debug modes */
		break;

	case BT_STATE_RESTART:		/* don't reset retries! */
		reset_flags(bt);
		bt->write_data[2] = ++bt->seq;
		bt->read_count = 0;
		bt->nonzero_status = 0;
		bt->timeout = BT_NORMAL_TIMEOUT;
		bt->state = BT_STATE_XACTION_START;
		break;

	default:	/* HOSED is supposed to be caught much earlier */
		error_recovery(bt, "internal logic error");
		break;
  	}
  	return SI_SM_CALL_WITH_DELAY;
}

static int bt_detect(struct si_sm_data *bt)
{
	/* It's impossible for the BT status and interrupt registers to be
	   all 1's, (assuming a properly functioning, self-initialized BMC)
	   but that's what you get from reading a bogus address, so we
	   test that first.  The calling routine uses negative logic. */

	if ((BT_STATUS == 0xFF) && (BT_INTMASK_R == 0xFF))
	       return 1;
	reset_flags(bt);
	return 0;
}

static void bt_cleanup(struct si_sm_data *bt)
{
}

static int bt_size(void)
{
	return sizeof(struct si_sm_data);
}

struct si_sm_handlers bt_smi_handlers =
{
	.init_data         = bt_init_data,
	.start_transaction = bt_start_transaction,
	.get_result        = bt_get_result,
	.event             = bt_event,
	.detect            = bt_detect,
	.cleanup           = bt_cleanup,
	.size              = bt_size,
};

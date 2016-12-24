/* generic HDLC line discipline for Linux
 *
 * Written by Paul Fulghum paulkf@microgate.com
 * for Microgate Corporation
 *
 * Microgate and SyncLink are registered trademarks of Microgate Corporation
 *
 * Adapted from ppp.c, written by Michael Callahan <callahan@maths.ox.ac.uk>,
 *	Al Longyear <longyear@netcom.com>,
 *	Paul Mackerras <Paul.Mackerras@cs.anu.edu.au>
 *
 * Original release 01/11/99
 *
 * This code is released under the GNU General Public License (GPL)
 *
 * This module implements the tty line discipline N_HDLC for use with
 * tty device drivers that support bit-synchronous HDLC communications.
 *
 * All HDLC data is frame oriented which means:
 *
 * 1. tty write calls represent one complete transmit frame of data
 *    The device driver should accept the complete frame or none of 
 *    the frame (busy) in the write method. Each write call should have
 *    a byte count in the range of 2-65535 bytes (2 is min HDLC frame
 *    with 1 addr byte and 1 ctrl byte). The max byte count of 65535
 *    should include any crc bytes required. For example, when using
 *    CCITT CRC32, 4 crc bytes are required, so the maximum size frame
 *    the application may transmit is limited to 65531 bytes. For CCITT
 *    CRC16, the maximum application frame size would be 65533.
 *
 *
 * 2. receive callbacks from the device driver represents
 *    one received frame. The device driver should bypass
 *    the tty flip buffer and call the line discipline receive
 *    callback directly to avoid fragmenting or concatenating
 *    multiple frames into a single receive callback.
 *
 *    The HDLC line discipline queues the receive frames in separate
 *    buffers so complete receive frames can be returned by the
 *    tty read calls.
 *
 * 3. tty read calls returns an entire frame of data or nothing.
 *    
 * 4. all send and receive data is considered raw. No processing
 *    or translation is performed by the line discipline, regardless
 *    of the tty flags
 *
 * 5. When line discipline is queried for the amount of receive
 *    data available (FIOC), 0 is returned if no data available,
 *    otherwise the count of the next available frame is returned.
 *    (instead of the sum of all received frame counts).
 *
 * These conventions allow the standard tty programming interface
 * to be used for synchronous HDLC applications when used with
 * this line discipline (or another line discipline that is frame
 * oriented such as N_PPP).
 *
 * The SyncLink driver (synclink.c) implements both asynchronous
 * (using standard line discipline N_TTY) and synchronous HDLC
 * (using N_HDLC) communications, with the latter using the above
 * conventions.
 *
 * This implementation is very basic and does not maintain
 * any statistics. The main point is to enforce the raw data
 * and frame orientation of HDLC communications.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define HDLC_MAGIC 0x239e

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>

#undef VERSION
#define VERSION(major,minor,patch) (((((major)<<8)+(minor))<<8)+(patch))

#include <linux/poll.h>
#include <linux/in.h>
#include <linux/ioctl.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/string.h>	/* used in new tty drivers */
#include <linux/signal.h>	/* used in new tty drivers */
#include <linux/if.h>
#include <linux/bitops.h>

#include <asm/termios.h>
#include <linux/uaccess.h>

/*
 * Buffers for individual HDLC frames
 */
#define MAX_HDLC_FRAME_SIZE 65535 
#define DEFAULT_RX_BUF_COUNT 10
#define MAX_RX_BUF_COUNT 60
#define DEFAULT_TX_BUF_COUNT 3

struct n_hdlc_buf {
	struct n_hdlc_buf *link;
	int		  count;
	char		  buf[1];
};

#define	N_HDLC_BUF_SIZE	(sizeof(struct n_hdlc_buf) + maxframe)

struct n_hdlc_buf_list {
	struct n_hdlc_buf *head;
	struct n_hdlc_buf *tail;
	int		  count;
	spinlock_t	  spinlock;
};

/**
 * struct n_hdlc - per device instance data structure
 * @magic - magic value for structure
 * @flags - miscellaneous control flags
 * @tty - ptr to TTY structure
 * @backup_tty - TTY to use if tty gets closed
 * @tbusy - reentrancy flag for tx wakeup code
 * @woke_up - FIXME: describe this field
 * @tbuf - currently transmitting tx buffer
 * @tx_buf_list - list of pending transmit frame buffers
 * @rx_buf_list - list of received frame buffers
 * @tx_free_buf_list - list unused transmit frame buffers
 * @rx_free_buf_list - list unused received frame buffers
 */
struct n_hdlc {
	int			magic;
	__u32			flags;
	struct tty_struct	*tty;
	struct tty_struct	*backup_tty;
	int			tbusy;
	int			woke_up;
	struct n_hdlc_buf	*tbuf;
	struct n_hdlc_buf_list	tx_buf_list;
	struct n_hdlc_buf_list	rx_buf_list;
	struct n_hdlc_buf_list	tx_free_buf_list;
	struct n_hdlc_buf_list	rx_free_buf_list;
};

/*
 * HDLC buffer list manipulation functions
 */
static void n_hdlc_buf_put(struct n_hdlc_buf_list *list,
			   struct n_hdlc_buf *buf);
static struct n_hdlc_buf *n_hdlc_buf_get(struct n_hdlc_buf_list *list);

/* Local functions */

static struct n_hdlc *n_hdlc_alloc (void);

/* debug level can be set by insmod for debugging purposes */
#define DEBUG_LEVEL_INFO	1
static int debuglevel;

/* max frame size for memory allocations */
static int maxframe = 4096;

/* TTY callbacks */

static ssize_t n_hdlc_tty_read(struct tty_struct *tty, struct file *file,
			   __u8 __user *buf, size_t nr);
static ssize_t n_hdlc_tty_write(struct tty_struct *tty, struct file *file,
			    const unsigned char *buf, size_t nr);
static int n_hdlc_tty_ioctl(struct tty_struct *tty, struct file *file,
			    unsigned int cmd, unsigned long arg);
static unsigned int n_hdlc_tty_poll(struct tty_struct *tty, struct file *filp,
				    poll_table *wait);
static int n_hdlc_tty_open(struct tty_struct *tty);
static void n_hdlc_tty_close(struct tty_struct *tty);
static void n_hdlc_tty_receive(struct tty_struct *tty, const __u8 *cp,
			       char *fp, int count);
static void n_hdlc_tty_wakeup(struct tty_struct *tty);

#define bset(p,b)	((p)[(b) >> 5] |= (1 << ((b) & 0x1f)))

#define tty2n_hdlc(tty)	((struct n_hdlc *) ((tty)->disc_data))
#define n_hdlc2tty(n_hdlc)	((n_hdlc)->tty)

static void flush_rx_queue(struct tty_struct *tty)
{
	struct n_hdlc *n_hdlc = tty2n_hdlc(tty);
	struct n_hdlc_buf *buf;

	while ((buf = n_hdlc_buf_get(&n_hdlc->rx_buf_list)))
		n_hdlc_buf_put(&n_hdlc->rx_free_buf_list, buf);
}

static void flush_tx_queue(struct tty_struct *tty)
{
	struct n_hdlc *n_hdlc = tty2n_hdlc(tty);
	struct n_hdlc_buf *buf;
	unsigned long flags;

	while ((buf = n_hdlc_buf_get(&n_hdlc->tx_buf_list)))
		n_hdlc_buf_put(&n_hdlc->tx_free_buf_list, buf);
 	spin_lock_irqsave(&n_hdlc->tx_buf_list.spinlock, flags);
	if (n_hdlc->tbuf) {
		n_hdlc_buf_put(&n_hdlc->tx_free_buf_list, n_hdlc->tbuf);
		n_hdlc->tbuf = NULL;
	}
	spin_unlock_irqrestore(&n_hdlc->tx_buf_list.spinlock, flags);
}

static struct tty_ldisc_ops n_hdlc_ldisc = {
	.owner		= THIS_MODULE,
	.magic		= TTY_LDISC_MAGIC,
	.name		= "hdlc",
	.open		= n_hdlc_tty_open,
	.close		= n_hdlc_tty_close,
	.read		= n_hdlc_tty_read,
	.write		= n_hdlc_tty_write,
	.ioctl		= n_hdlc_tty_ioctl,
	.poll		= n_hdlc_tty_poll,
	.receive_buf	= n_hdlc_tty_receive,
	.write_wakeup	= n_hdlc_tty_wakeup,
	.flush_buffer   = flush_rx_queue,
};

/**
 * n_hdlc_release - release an n_hdlc per device line discipline info structure
 * @n_hdlc - per device line discipline info structure
 */
static void n_hdlc_release(struct n_hdlc *n_hdlc)
{
	struct tty_struct *tty = n_hdlc2tty (n_hdlc);
	struct n_hdlc_buf *buf;
	
	if (debuglevel >= DEBUG_LEVEL_INFO)	
		printk("%s(%d)n_hdlc_release() called\n",__FILE__,__LINE__);
		
	/* Ensure that the n_hdlcd process is not hanging on select()/poll() */
	wake_up_interruptible (&tty->read_wait);
	wake_up_interruptible (&tty->write_wait);

	if (tty->disc_data == n_hdlc)
		tty->disc_data = NULL;	/* Break the tty->n_hdlc link */

	/* Release transmit and receive buffers */
	for(;;) {
		buf = n_hdlc_buf_get(&n_hdlc->rx_free_buf_list);
		if (buf) {
			kfree(buf);
		} else
			break;
	}
	for(;;) {
		buf = n_hdlc_buf_get(&n_hdlc->tx_free_buf_list);
		if (buf) {
			kfree(buf);
		} else
			break;
	}
	for(;;) {
		buf = n_hdlc_buf_get(&n_hdlc->rx_buf_list);
		if (buf) {
			kfree(buf);
		} else
			break;
	}
	for(;;) {
		buf = n_hdlc_buf_get(&n_hdlc->tx_buf_list);
		if (buf) {
			kfree(buf);
		} else
			break;
	}
	kfree(n_hdlc->tbuf);
	kfree(n_hdlc);
	
}	/* end of n_hdlc_release() */

/**
 * n_hdlc_tty_close - line discipline close
 * @tty - pointer to tty info structure
 *
 * Called when the line discipline is changed to something
 * else, the tty is closed, or the tty detects a hangup.
 */
static void n_hdlc_tty_close(struct tty_struct *tty)
{
	struct n_hdlc *n_hdlc = tty2n_hdlc (tty);

	if (debuglevel >= DEBUG_LEVEL_INFO)	
		printk("%s(%d)n_hdlc_tty_close() called\n",__FILE__,__LINE__);
		
	if (n_hdlc != NULL) {
		if (n_hdlc->magic != HDLC_MAGIC) {
			printk (KERN_WARNING"n_hdlc: trying to close unopened tty!\n");
			return;
		}
#if defined(TTY_NO_WRITE_SPLIT)
		clear_bit(TTY_NO_WRITE_SPLIT,&tty->flags);
#endif
		tty->disc_data = NULL;
		if (tty == n_hdlc->backup_tty)
			n_hdlc->backup_tty = NULL;
		if (tty != n_hdlc->tty)
			return;
		if (n_hdlc->backup_tty) {
			n_hdlc->tty = n_hdlc->backup_tty;
		} else {
			n_hdlc_release (n_hdlc);
		}
	}
	
	if (debuglevel >= DEBUG_LEVEL_INFO)	
		printk("%s(%d)n_hdlc_tty_close() success\n",__FILE__,__LINE__);
		
}	/* end of n_hdlc_tty_close() */

/**
 * n_hdlc_tty_open - called when line discipline changed to n_hdlc
 * @tty - pointer to tty info structure
 *
 * Returns 0 if success, otherwise error code
 */
static int n_hdlc_tty_open (struct tty_struct *tty)
{
	struct n_hdlc *n_hdlc = tty2n_hdlc (tty);

	if (debuglevel >= DEBUG_LEVEL_INFO)	
		printk("%s(%d)n_hdlc_tty_open() called (device=%s)\n",
		__FILE__,__LINE__,
		tty->name);
		
	/* There should not be an existing table for this slot. */
	if (n_hdlc) {
		printk (KERN_ERR"n_hdlc_tty_open:tty already associated!\n" );
		return -EEXIST;
	}
	
	n_hdlc = n_hdlc_alloc();
	if (!n_hdlc) {
		printk (KERN_ERR "n_hdlc_alloc failed\n");
		return -ENFILE;
	}
		
	tty->disc_data = n_hdlc;
	n_hdlc->tty    = tty;
	tty->receive_room = 65536;
	
#if defined(TTY_NO_WRITE_SPLIT)
	/* change tty_io write() to not split large writes into 8K chunks */
	set_bit(TTY_NO_WRITE_SPLIT,&tty->flags);
#endif
	
	/* flush receive data from driver */
	tty_driver_flush_buffer(tty);
		
	if (debuglevel >= DEBUG_LEVEL_INFO)	
		printk("%s(%d)n_hdlc_tty_open() success\n",__FILE__,__LINE__);
		
	return 0;
	
}	/* end of n_tty_hdlc_open() */

/**
 * n_hdlc_send_frames - send frames on pending send buffer list
 * @n_hdlc - pointer to ldisc instance data
 * @tty - pointer to tty instance data
 *
 * Send frames on pending send buffer list until the driver does not accept a
 * frame (busy) this function is called after adding a frame to the send buffer
 * list and by the tty wakeup callback.
 */
static void n_hdlc_send_frames(struct n_hdlc *n_hdlc, struct tty_struct *tty)
{
	register int actual;
	unsigned long flags;
	struct n_hdlc_buf *tbuf;

	if (debuglevel >= DEBUG_LEVEL_INFO)	
		printk("%s(%d)n_hdlc_send_frames() called\n",__FILE__,__LINE__);
 check_again:
		
 	spin_lock_irqsave(&n_hdlc->tx_buf_list.spinlock, flags);
	if (n_hdlc->tbusy) {
		n_hdlc->woke_up = 1;
 		spin_unlock_irqrestore(&n_hdlc->tx_buf_list.spinlock, flags);
		return;
	}
	n_hdlc->tbusy = 1;
	n_hdlc->woke_up = 0;
	spin_unlock_irqrestore(&n_hdlc->tx_buf_list.spinlock, flags);

	/* get current transmit buffer or get new transmit */
	/* buffer from list of pending transmit buffers */
		
	tbuf = n_hdlc->tbuf;
	if (!tbuf)
		tbuf = n_hdlc_buf_get(&n_hdlc->tx_buf_list);
		
	while (tbuf) {
		if (debuglevel >= DEBUG_LEVEL_INFO)	
			printk("%s(%d)sending frame %p, count=%d\n",
				__FILE__,__LINE__,tbuf,tbuf->count);
			
		/* Send the next block of data to device */
		set_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
		actual = tty->ops->write(tty, tbuf->buf, tbuf->count);

		/* rollback was possible and has been done */
		if (actual == -ERESTARTSYS) {
			n_hdlc->tbuf = tbuf;
			break;
		}
		/* if transmit error, throw frame away by */
		/* pretending it was accepted by driver */
		if (actual < 0)
			actual = tbuf->count;
		
		if (actual == tbuf->count) {
			if (debuglevel >= DEBUG_LEVEL_INFO)	
				printk("%s(%d)frame %p completed\n",
					__FILE__,__LINE__,tbuf);
					
			/* free current transmit buffer */
			n_hdlc_buf_put(&n_hdlc->tx_free_buf_list, tbuf);
			
			/* this tx buffer is done */
			n_hdlc->tbuf = NULL;
			
			/* wait up sleeping writers */
			wake_up_interruptible(&tty->write_wait);
	
			/* get next pending transmit buffer */
			tbuf = n_hdlc_buf_get(&n_hdlc->tx_buf_list);
		} else {
			if (debuglevel >= DEBUG_LEVEL_INFO)	
				printk("%s(%d)frame %p pending\n",
					__FILE__,__LINE__,tbuf);
					
			/* buffer not accepted by driver */
			/* set this buffer as pending buffer */
			n_hdlc->tbuf = tbuf;
			break;
		}
	}
	
	if (!tbuf)
		clear_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
	
	/* Clear the re-entry flag */
	spin_lock_irqsave(&n_hdlc->tx_buf_list.spinlock, flags);
	n_hdlc->tbusy = 0;
	spin_unlock_irqrestore(&n_hdlc->tx_buf_list.spinlock, flags); 
	
        if (n_hdlc->woke_up)
	  goto check_again;

	if (debuglevel >= DEBUG_LEVEL_INFO)	
		printk("%s(%d)n_hdlc_send_frames() exit\n",__FILE__,__LINE__);
		
}	/* end of n_hdlc_send_frames() */

/**
 * n_hdlc_tty_wakeup - Callback for transmit wakeup
 * @tty	- pointer to associated tty instance data
 *
 * Called when low level device driver can accept more send data.
 */
static void n_hdlc_tty_wakeup(struct tty_struct *tty)
{
	struct n_hdlc *n_hdlc = tty2n_hdlc(tty);

	if (debuglevel >= DEBUG_LEVEL_INFO)	
		printk("%s(%d)n_hdlc_tty_wakeup() called\n",__FILE__,__LINE__);
		
	if (!n_hdlc)
		return;

	if (tty != n_hdlc->tty) {
		clear_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
		return;
	}

	n_hdlc_send_frames (n_hdlc, tty);
		
}	/* end of n_hdlc_tty_wakeup() */

/**
 * n_hdlc_tty_receive - Called by tty driver when receive data is available
 * @tty	- pointer to tty instance data
 * @data - pointer to received data
 * @flags - pointer to flags for data
 * @count - count of received data in bytes
 *
 * Called by tty low level driver when receive data is available. Data is
 * interpreted as one HDLC frame.
 */
static void n_hdlc_tty_receive(struct tty_struct *tty, const __u8 *data,
			       char *flags, int count)
{
	register struct n_hdlc *n_hdlc = tty2n_hdlc (tty);
	register struct n_hdlc_buf *buf;

	if (debuglevel >= DEBUG_LEVEL_INFO)	
		printk("%s(%d)n_hdlc_tty_receive() called count=%d\n",
			__FILE__,__LINE__, count);
		
	/* This can happen if stuff comes in on the backup tty */
	if (!n_hdlc || tty != n_hdlc->tty)
		return;
		
	/* verify line is using HDLC discipline */
	if (n_hdlc->magic != HDLC_MAGIC) {
		printk("%s(%d) line not using HDLC discipline\n",
			__FILE__,__LINE__);
		return;
	}
	
	if ( count>maxframe ) {
		if (debuglevel >= DEBUG_LEVEL_INFO)	
			printk("%s(%d) rx count>maxframesize, data discarded\n",
			       __FILE__,__LINE__);
		return;
	}

	/* get a free HDLC buffer */	
	buf = n_hdlc_buf_get(&n_hdlc->rx_free_buf_list);
	if (!buf) {
		/* no buffers in free list, attempt to allocate another rx buffer */
		/* unless the maximum count has been reached */
		if (n_hdlc->rx_buf_list.count < MAX_RX_BUF_COUNT)
			buf = kmalloc(N_HDLC_BUF_SIZE, GFP_ATOMIC);
	}
	
	if (!buf) {
		if (debuglevel >= DEBUG_LEVEL_INFO)	
			printk("%s(%d) no more rx buffers, data discarded\n",
			       __FILE__,__LINE__);
		return;
	}
		
	/* copy received data to HDLC buffer */
	memcpy(buf->buf,data,count);
	buf->count=count;

	/* add HDLC buffer to list of received frames */
	n_hdlc_buf_put(&n_hdlc->rx_buf_list, buf);
	
	/* wake up any blocked reads and perform async signalling */
	wake_up_interruptible (&tty->read_wait);
	if (n_hdlc->tty->fasync != NULL)
		kill_fasync (&n_hdlc->tty->fasync, SIGIO, POLL_IN);

}	/* end of n_hdlc_tty_receive() */

/**
 * n_hdlc_tty_read - Called to retrieve one frame of data (if available)
 * @tty - pointer to tty instance data
 * @file - pointer to open file object
 * @buf - pointer to returned data buffer
 * @nr - size of returned data buffer
 * 	
 * Returns the number of bytes returned or error code.
 */
static ssize_t n_hdlc_tty_read(struct tty_struct *tty, struct file *file,
			   __u8 __user *buf, size_t nr)
{
	struct n_hdlc *n_hdlc = tty2n_hdlc(tty);
	int ret = 0;
	struct n_hdlc_buf *rbuf;
	DECLARE_WAITQUEUE(wait, current);

	if (debuglevel >= DEBUG_LEVEL_INFO)	
		printk("%s(%d)n_hdlc_tty_read() called\n",__FILE__,__LINE__);
		
	/* Validate the pointers */
	if (!n_hdlc)
		return -EIO;

	/* verify user access to buffer */
	if (!access_ok(VERIFY_WRITE, buf, nr)) {
		printk(KERN_WARNING "%s(%d) n_hdlc_tty_read() can't verify user "
		"buffer\n", __FILE__, __LINE__);
		return -EFAULT;
	}

	add_wait_queue(&tty->read_wait, &wait);

	for (;;) {
		if (test_bit(TTY_OTHER_CLOSED, &tty->flags)) {
			ret = -EIO;
			break;
		}
		if (tty_hung_up_p(file))
			break;

		set_current_state(TASK_INTERRUPTIBLE);

		rbuf = n_hdlc_buf_get(&n_hdlc->rx_buf_list);
		if (rbuf) {
			if (rbuf->count > nr) {
				/* too large for caller's buffer */
				ret = -EOVERFLOW;
			} else {
				if (copy_to_user(buf, rbuf->buf, rbuf->count))
					ret = -EFAULT;
				else
					ret = rbuf->count;
			}

			if (n_hdlc->rx_free_buf_list.count >
			    DEFAULT_RX_BUF_COUNT)
				kfree(rbuf);
			else
				n_hdlc_buf_put(&n_hdlc->rx_free_buf_list, rbuf);
			break;
		}
			
		/* no data */
		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			break;
		}

		schedule();

		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}
	}

	remove_wait_queue(&tty->read_wait, &wait);
	__set_current_state(TASK_RUNNING);

	return ret;
	
}	/* end of n_hdlc_tty_read() */

/**
 * n_hdlc_tty_write - write a single frame of data to device
 * @tty	- pointer to associated tty device instance data
 * @file - pointer to file object data
 * @data - pointer to transmit data (one frame)
 * @count - size of transmit frame in bytes
 * 		
 * Returns the number of bytes written (or error code).
 */
static ssize_t n_hdlc_tty_write(struct tty_struct *tty, struct file *file,
			    const unsigned char *data, size_t count)
{
	struct n_hdlc *n_hdlc = tty2n_hdlc (tty);
	int error = 0;
	DECLARE_WAITQUEUE(wait, current);
	struct n_hdlc_buf *tbuf;

	if (debuglevel >= DEBUG_LEVEL_INFO)	
		printk("%s(%d)n_hdlc_tty_write() called count=%Zd\n",
			__FILE__,__LINE__,count);
		
	/* Verify pointers */
	if (!n_hdlc)
		return -EIO;

	if (n_hdlc->magic != HDLC_MAGIC)
		return -EIO;

	/* verify frame size */
	if (count > maxframe ) {
		if (debuglevel & DEBUG_LEVEL_INFO)
			printk (KERN_WARNING
				"n_hdlc_tty_write: truncating user packet "
				"from %lu to %d\n", (unsigned long) count,
				maxframe );
		count = maxframe;
	}
	
	add_wait_queue(&tty->write_wait, &wait);

	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
	
		tbuf = n_hdlc_buf_get(&n_hdlc->tx_free_buf_list);
		if (tbuf)
			break;

		if (file->f_flags & O_NONBLOCK) {
			error = -EAGAIN;
			break;
		}
		schedule();
			
		n_hdlc = tty2n_hdlc (tty);
		if (!n_hdlc || n_hdlc->magic != HDLC_MAGIC || 
		    tty != n_hdlc->tty) {
			printk("n_hdlc_tty_write: %p invalid after wait!\n", n_hdlc);
			error = -EIO;
			break;
		}
			
		if (signal_pending(current)) {
			error = -EINTR;
			break;
		}
	}

	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&tty->write_wait, &wait);

	if (!error) {		
		/* Retrieve the user's buffer */
		memcpy(tbuf->buf, data, count);

		/* Send the data */
		tbuf->count = error = count;
		n_hdlc_buf_put(&n_hdlc->tx_buf_list,tbuf);
		n_hdlc_send_frames(n_hdlc,tty);
	}

	return error;
	
}	/* end of n_hdlc_tty_write() */

/**
 * n_hdlc_tty_ioctl - process IOCTL system call for the tty device.
 * @tty - pointer to tty instance data
 * @file - pointer to open file object for device
 * @cmd - IOCTL command code
 * @arg - argument for IOCTL call (cmd dependent)
 *
 * Returns command dependent result.
 */
static int n_hdlc_tty_ioctl(struct tty_struct *tty, struct file *file,
			    unsigned int cmd, unsigned long arg)
{
	struct n_hdlc *n_hdlc = tty2n_hdlc (tty);
	int error = 0;
	int count;
	unsigned long flags;
	
	if (debuglevel >= DEBUG_LEVEL_INFO)	
		printk("%s(%d)n_hdlc_tty_ioctl() called %d\n",
			__FILE__,__LINE__,cmd);
		
	/* Verify the status of the device */
	if (!n_hdlc || n_hdlc->magic != HDLC_MAGIC)
		return -EBADF;

	switch (cmd) {
	case FIONREAD:
		/* report count of read data available */
		/* in next available frame (if any) */
		spin_lock_irqsave(&n_hdlc->rx_buf_list.spinlock,flags);
		if (n_hdlc->rx_buf_list.head)
			count = n_hdlc->rx_buf_list.head->count;
		else
			count = 0;
		spin_unlock_irqrestore(&n_hdlc->rx_buf_list.spinlock,flags);
		error = put_user(count, (int __user *)arg);
		break;

	case TIOCOUTQ:
		/* get the pending tx byte count in the driver */
		count = tty_chars_in_buffer(tty);
		/* add size of next output frame in queue */
		spin_lock_irqsave(&n_hdlc->tx_buf_list.spinlock,flags);
		if (n_hdlc->tx_buf_list.head)
			count += n_hdlc->tx_buf_list.head->count;
		spin_unlock_irqrestore(&n_hdlc->tx_buf_list.spinlock,flags);
		error = put_user(count, (int __user *)arg);
		break;

	case TCFLSH:
		switch (arg) {
		case TCIOFLUSH:
		case TCOFLUSH:
			flush_tx_queue(tty);
		}
		/* fall through to default */

	default:
		error = n_tty_ioctl_helper(tty, file, cmd, arg);
		break;
	}
	return error;
	
}	/* end of n_hdlc_tty_ioctl() */

/**
 * n_hdlc_tty_poll - TTY callback for poll system call
 * @tty - pointer to tty instance data
 * @filp - pointer to open file object for device
 * @poll_table - wait queue for operations
 * 
 * Determine which operations (read/write) will not block and return info
 * to caller.
 * Returns a bit mask containing info on which ops will not block.
 */
static unsigned int n_hdlc_tty_poll(struct tty_struct *tty, struct file *filp,
				    poll_table *wait)
{
	struct n_hdlc *n_hdlc = tty2n_hdlc (tty);
	unsigned int mask = 0;

	if (debuglevel >= DEBUG_LEVEL_INFO)	
		printk("%s(%d)n_hdlc_tty_poll() called\n",__FILE__,__LINE__);
		
	if (n_hdlc && n_hdlc->magic == HDLC_MAGIC && tty == n_hdlc->tty) {
		/* queue current process into any wait queue that */
		/* may awaken in the future (read and write) */

		poll_wait(filp, &tty->read_wait, wait);
		poll_wait(filp, &tty->write_wait, wait);

		/* set bits for operations that won't block */
		if (n_hdlc->rx_buf_list.head)
			mask |= POLLIN | POLLRDNORM;	/* readable */
		if (test_bit(TTY_OTHER_CLOSED, &tty->flags))
			mask |= POLLHUP;
		if (tty_hung_up_p(filp))
			mask |= POLLHUP;
		if (!tty_is_writelocked(tty) &&
				n_hdlc->tx_free_buf_list.head)
			mask |= POLLOUT | POLLWRNORM;	/* writable */
	}
	return mask;
}	/* end of n_hdlc_tty_poll() */

/**
 * n_hdlc_alloc - allocate an n_hdlc instance data structure
 *
 * Returns a pointer to newly created structure if success, otherwise %NULL
 */
static struct n_hdlc *n_hdlc_alloc(void)
{
	struct n_hdlc_buf *buf;
	int i;
	struct n_hdlc *n_hdlc = kzalloc(sizeof(*n_hdlc), GFP_KERNEL);

	if (!n_hdlc)
		return NULL;

	spin_lock_init(&n_hdlc->rx_free_buf_list.spinlock);
	spin_lock_init(&n_hdlc->tx_free_buf_list.spinlock);
	spin_lock_init(&n_hdlc->rx_buf_list.spinlock);
	spin_lock_init(&n_hdlc->tx_buf_list.spinlock);
	
	/* allocate free rx buffer list */
	for(i=0;i<DEFAULT_RX_BUF_COUNT;i++) {
		buf = kmalloc(N_HDLC_BUF_SIZE, GFP_KERNEL);
		if (buf)
			n_hdlc_buf_put(&n_hdlc->rx_free_buf_list,buf);
		else if (debuglevel >= DEBUG_LEVEL_INFO)	
			printk("%s(%d)n_hdlc_alloc(), kalloc() failed for rx buffer %d\n",__FILE__,__LINE__, i);
	}
	
	/* allocate free tx buffer list */
	for(i=0;i<DEFAULT_TX_BUF_COUNT;i++) {
		buf = kmalloc(N_HDLC_BUF_SIZE, GFP_KERNEL);
		if (buf)
			n_hdlc_buf_put(&n_hdlc->tx_free_buf_list,buf);
		else if (debuglevel >= DEBUG_LEVEL_INFO)	
			printk("%s(%d)n_hdlc_alloc(), kalloc() failed for tx buffer %d\n",__FILE__,__LINE__, i);
	}
	
	/* Initialize the control block */
	n_hdlc->magic  = HDLC_MAGIC;
	n_hdlc->flags  = 0;
	
	return n_hdlc;
	
}	/* end of n_hdlc_alloc() */

/**
 * n_hdlc_buf_put - add specified HDLC buffer to tail of specified list
 * @list - pointer to buffer list
 * @buf	- pointer to buffer
 */
static void n_hdlc_buf_put(struct n_hdlc_buf_list *list,
			   struct n_hdlc_buf *buf)
{
	unsigned long flags;
	spin_lock_irqsave(&list->spinlock,flags);
	
	buf->link=NULL;
	if (list->tail)
		list->tail->link = buf;
	else
		list->head = buf;
	list->tail = buf;
	(list->count)++;
	
	spin_unlock_irqrestore(&list->spinlock,flags);
	
}	/* end of n_hdlc_buf_put() */

/**
 * n_hdlc_buf_get - remove and return an HDLC buffer from list
 * @list - pointer to HDLC buffer list
 * 
 * Remove and return an HDLC buffer from the head of the specified HDLC buffer
 * list.
 * Returns a pointer to HDLC buffer if available, otherwise %NULL.
 */
static struct n_hdlc_buf* n_hdlc_buf_get(struct n_hdlc_buf_list *list)
{
	unsigned long flags;
	struct n_hdlc_buf *buf;
	spin_lock_irqsave(&list->spinlock,flags);
	
	buf = list->head;
	if (buf) {
		list->head = buf->link;
		(list->count)--;
	}
	if (!list->head)
		list->tail = NULL;
	
	spin_unlock_irqrestore(&list->spinlock,flags);
	return buf;
	
}	/* end of n_hdlc_buf_get() */

static char hdlc_banner[] __initdata =
	KERN_INFO "HDLC line discipline maxframe=%u\n";
static char hdlc_register_ok[] __initdata =
	KERN_INFO "N_HDLC line discipline registered.\n";
static char hdlc_register_fail[] __initdata =
	KERN_ERR "error registering line discipline: %d\n";

static int __init n_hdlc_init(void)
{
	int status;

	/* range check maxframe arg */
	if (maxframe < 4096)
		maxframe = 4096;
	else if (maxframe > 65535)
		maxframe = 65535;

	printk(hdlc_banner, maxframe);

	status = tty_register_ldisc(N_HDLC, &n_hdlc_ldisc);
	if (!status)
		printk(hdlc_register_ok);
	else
		printk(hdlc_register_fail, status);

	return status;
	
}	/* end of init_module() */

static char hdlc_unregister_ok[] __exitdata =
	KERN_INFO "N_HDLC: line discipline unregistered\n";
static char hdlc_unregister_fail[] __exitdata =
	KERN_ERR "N_HDLC: can't unregister line discipline (err = %d)\n";

static void __exit n_hdlc_exit(void)
{
	/* Release tty registration of line discipline */
	int status = tty_unregister_ldisc(N_HDLC);

	if (status)
		printk(hdlc_unregister_fail, status);
	else
		printk(hdlc_unregister_ok);
}

module_init(n_hdlc_init);
module_exit(n_hdlc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Paul Fulghum paulkf@microgate.com");
module_param(debuglevel, int, 0);
module_param(maxframe, int, 0);
MODULE_ALIAS_LDISC(N_HDLC);

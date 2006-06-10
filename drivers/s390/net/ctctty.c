/*
 * CTC / ESCON network driver, tty interface.
 *
 * Copyright (C) 2001 IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Author(s): Fritz Elfert (elfert@de.ibm.com, felfert@millenux.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_reg.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <linux/devfs_fs_kernel.h>
#include "ctctty.h"
#include "ctcdbug.h"

#define CTC_TTY_MAJOR       43
#define CTC_TTY_MAX_DEVICES 64

#define CTC_ASYNC_MAGIC          0x49344C01 /* for paranoia-checking        */
#define CTC_ASYNC_INITIALIZED    0x80000000 /* port was initialized         */
#define CTC_ASYNC_NORMAL_ACTIVE  0x20000000 /* Normal device active         */
#define CTC_ASYNC_CLOSING        0x08000000 /* Serial port is closing       */
#define CTC_ASYNC_CTS_FLOW       0x04000000 /* Do CTS flow control          */
#define CTC_ASYNC_CHECK_CD       0x02000000 /* i.e., CLOCAL                 */
#define CTC_ASYNC_HUP_NOTIFY         0x0001 /* Notify tty on hangups/closes */
#define CTC_ASYNC_NETDEV_OPEN        0x0002 /* Underlying netdev is open    */
#define CTC_ASYNC_TX_LINESTAT        0x0004 /* Must send line status        */
#define CTC_ASYNC_SPLIT_TERMIOS      0x0008 /* Sep. termios for dialin/out  */
#define CTC_TTY_XMIT_SIZE              1024 /* Default bufsize for write    */
#define CTC_SERIAL_XMIT_MAX            4000 /* Maximum bufsize for write    */

/* Private data (similar to async_struct in <linux/serial.h>) */
typedef struct {
  int			magic;
  int			flags;		 /* defined in tty.h               */
  int			mcr;		 /* Modem control register         */
  int                   msr;             /* Modem status register          */
  int                   lsr;             /* Line status register           */
  int			line;
  int			count;		 /* # of fd on device              */
  int			blocked_open;	 /* # of blocked opens             */
  struct net_device     *netdev;
  struct sk_buff_head   tx_queue;        /* transmit queue                 */
  struct sk_buff_head   rx_queue;        /* receive queue                  */
  struct tty_struct 	*tty;            /* Pointer to corresponding tty   */
  wait_queue_head_t	open_wait;
  wait_queue_head_t	close_wait;
  struct semaphore      write_sem;
  struct tasklet_struct tasklet;
  struct timer_list     stoptimer;
} ctc_tty_info;

/* Description of one CTC-tty */
typedef struct {
  struct tty_driver  *ctc_tty_device;		   /* tty-device             */
  ctc_tty_info       info[CTC_TTY_MAX_DEVICES];	   /* Private data           */
} ctc_tty_driver;

static ctc_tty_driver *driver;

/* Leave this unchanged unless you know what you do! */
#define MODEM_PARANOIA_CHECK
#define MODEM_DO_RESTART

#define CTC_TTY_NAME "ctctty"

static __u32 ctc_tty_magic = CTC_ASYNC_MAGIC;
static int ctc_tty_shuttingdown = 0;

static spinlock_t ctc_tty_lock;

/* ctc_tty_try_read() is called from within ctc_tty_rcv_skb()
 * to stuff incoming data directly into a tty's flip-buffer. If the
 * flip buffer is full, the packet gets queued up.
 *
 * Return:
 *  1 = Success
 *  0 = Failure, data has to be buffered and later processed by
 *      ctc_tty_readmodem().
 */
static int
ctc_tty_try_read(ctc_tty_info * info, struct sk_buff *skb)
{
	int len;
	struct tty_struct *tty;

	DBF_TEXT(trace, 5, __FUNCTION__);
	if ((tty = info->tty)) {
		if (info->mcr & UART_MCR_RTS) {
			len = skb->len;
			tty_insert_flip_string(tty, skb->data, len);
			tty_flip_buffer_push(tty);
			kfree_skb(skb);
			return 1;
		}
	}
	return 0;
}

/* ctc_tty_readmodem() is called periodically from within timer-interrupt.
 * It tries getting received data from the receive queue an stuff it into
 * the tty's flip-buffer.
 */
static int
ctc_tty_readmodem(ctc_tty_info *info)
{
	int ret = 1;
	struct tty_struct *tty;

	DBF_TEXT(trace, 5, __FUNCTION__);
	if ((tty = info->tty)) {
		if (info->mcr & UART_MCR_RTS) {
			struct sk_buff *skb;

			if ((skb = skb_dequeue(&info->rx_queue))) {
				int len = skb->len;
				tty_insert_flip_string(tty, skb->data, len);
				skb_pull(skb, len);
				tty_flip_buffer_push(tty);
				if (skb->len > 0)
					skb_queue_head(&info->rx_queue, skb);
				else {
					kfree_skb(skb);
					ret = !skb_queue_empty(&info->rx_queue);
				}
			}
		}
	}
	return ret;
}

void
ctc_tty_setcarrier(struct net_device *netdev, int on)
{
	int i;

	DBF_TEXT(trace, 4, __FUNCTION__);
	if ((!driver) || ctc_tty_shuttingdown)
		return;
	for (i = 0; i < CTC_TTY_MAX_DEVICES; i++)
		if (driver->info[i].netdev == netdev) {
			ctc_tty_info *info = &driver->info[i];
			if (on)
				info->msr |= UART_MSR_DCD;
			else
				info->msr &= ~UART_MSR_DCD;
			if ((info->flags & CTC_ASYNC_CHECK_CD) && (!on))
				tty_hangup(info->tty);
		}
}

void
ctc_tty_netif_rx(struct sk_buff *skb)
{
	int i;
	ctc_tty_info *info = NULL;

	DBF_TEXT(trace, 5, __FUNCTION__);
	if (!skb)
		return;
	if ((!skb->dev) || (!driver) || ctc_tty_shuttingdown) {
		dev_kfree_skb(skb);
		return;
	}
	for (i = 0; i < CTC_TTY_MAX_DEVICES; i++)
		if (driver->info[i].netdev == skb->dev) {
			info = &driver->info[i];
			break;
		}
	if (!info) {
		dev_kfree_skb(skb);
		return;
	}
	if (skb->len < 6) {
		dev_kfree_skb(skb);
		return;
	}
	if (memcmp(skb->data, &ctc_tty_magic, sizeof(__u32))) {
		dev_kfree_skb(skb);
		return;
	}
	skb_pull(skb, sizeof(__u32));

	i = *((int *)skb->data);
	skb_pull(skb, sizeof(info->mcr));
	if (i & UART_MCR_RTS) {
		info->msr |= UART_MSR_CTS;
		if (info->flags & CTC_ASYNC_CTS_FLOW)
			info->tty->hw_stopped = 0;
	} else {
		info->msr &= ~UART_MSR_CTS;
		if (info->flags & CTC_ASYNC_CTS_FLOW)
			info->tty->hw_stopped = 1;
	}
	if (i & UART_MCR_DTR)
		info->msr |= UART_MSR_DSR;
	else
		info->msr &= ~UART_MSR_DSR;
	if (skb->len <= 0) {
		kfree_skb(skb);
		return;
	}
	/* Try to deliver directly via tty-flip-buf if queue is empty */
	if (skb_queue_empty(&info->rx_queue))
		if (ctc_tty_try_read(info, skb))
			return;
	/* Direct deliver failed or queue wasn't empty.
	 * Queue up for later dequeueing via timer-irq.
	 */
	skb_queue_tail(&info->rx_queue, skb);
	/* Schedule dequeuing */
	tasklet_schedule(&info->tasklet);
}

static int
ctc_tty_tint(ctc_tty_info * info)
{
	struct sk_buff *skb = skb_dequeue(&info->tx_queue);
	int stopped = (info->tty->hw_stopped || info->tty->stopped);
	int wake = 1;
	int rc;

	DBF_TEXT(trace, 4, __FUNCTION__);
	if (!info->netdev) {
		if (skb)
			kfree_skb(skb);
		return 0;
	}
	if (info->flags & CTC_ASYNC_TX_LINESTAT) {
		int skb_res = info->netdev->hard_header_len +
			sizeof(info->mcr) + sizeof(__u32);
		/* If we must update line status,
		 * create an empty dummy skb and insert it.
		 */
		if (skb)
			skb_queue_head(&info->tx_queue, skb);

		skb = dev_alloc_skb(skb_res);
		if (!skb) {
			printk(KERN_WARNING
			       "ctc_tty: Out of memory in %s%d tint\n",
			       CTC_TTY_NAME, info->line);
			return 1;
		}
		skb_reserve(skb, skb_res);
		stopped = 0;
		wake = 0;
	}
	if (!skb)
		return 0;
	if (stopped) {
		skb_queue_head(&info->tx_queue, skb);
		return 1;
	}
#if 0
	if (skb->len > 0)
		printk(KERN_DEBUG "tint: %d %02x\n", skb->len, *(skb->data));
	else
		printk(KERN_DEBUG "tint: %d STAT\n", skb->len);
#endif
	memcpy(skb_push(skb, sizeof(info->mcr)), &info->mcr, sizeof(info->mcr));
	memcpy(skb_push(skb, sizeof(__u32)), &ctc_tty_magic, sizeof(__u32));
	rc = info->netdev->hard_start_xmit(skb, info->netdev);
	if (rc) {
		skb_pull(skb, sizeof(info->mcr) + sizeof(__u32));
		if (skb->len > 0)
			skb_queue_head(&info->tx_queue, skb);
		else
			kfree_skb(skb);
	} else {
		struct tty_struct *tty = info->tty;

		info->flags &= ~CTC_ASYNC_TX_LINESTAT;
		if (tty) {
			tty_wakeup(tty);
		}
	}
	return (skb_queue_empty(&info->tx_queue) ? 0 : 1);
}

/************************************************************
 *
 * Modem-functions
 *
 * mostly "stolen" from original Linux-serial.c and friends.
 *
 ************************************************************/

static inline int
ctc_tty_paranoia_check(ctc_tty_info * info, char *name, const char *routine)
{
#ifdef MODEM_PARANOIA_CHECK
	if (!info) {
		printk(KERN_WARNING "ctc_tty: null info_struct for %s in %s\n",
		       name, routine);
		return 1;
	}
	if (info->magic != CTC_ASYNC_MAGIC) {
		printk(KERN_WARNING "ctc_tty: bad magic for info struct %s in %s\n",
		       name, routine);
		return 1;
	}
#endif
	return 0;
}

static void
ctc_tty_inject(ctc_tty_info *info, char c)
{
	int skb_res;
	struct sk_buff *skb;

	DBF_TEXT(trace, 4, __FUNCTION__);
	if (ctc_tty_shuttingdown)
		return;
	skb_res = info->netdev->hard_header_len + sizeof(info->mcr) +
		sizeof(__u32) + 1;
	skb = dev_alloc_skb(skb_res);
	if (!skb) {
		printk(KERN_WARNING
		       "ctc_tty: Out of memory in %s%d tx_inject\n",
		       CTC_TTY_NAME, info->line);
		return;
	}
	skb_reserve(skb, skb_res);
	*(skb_put(skb, 1)) = c;
	skb_queue_head(&info->tx_queue, skb);
	tasklet_schedule(&info->tasklet);
}

static void
ctc_tty_transmit_status(ctc_tty_info *info)
{
	DBF_TEXT(trace, 5, __FUNCTION__);
	if (ctc_tty_shuttingdown)
		return;
	info->flags |= CTC_ASYNC_TX_LINESTAT;
	tasklet_schedule(&info->tasklet);
}

static void
ctc_tty_change_speed(ctc_tty_info * info)
{
	unsigned int cflag;
	unsigned int quot;
	int i;

	DBF_TEXT(trace, 3, __FUNCTION__);
	if (!info->tty || !info->tty->termios)
		return;
	cflag = info->tty->termios->c_cflag;

	quot = i = cflag & CBAUD;
	if (i & CBAUDEX) {
		i &= ~CBAUDEX;
		if (i < 1 || i > 2)
			info->tty->termios->c_cflag &= ~CBAUDEX;
		else
			i += 15;
	}
	if (quot) {
		info->mcr |= UART_MCR_DTR;
		info->mcr |= UART_MCR_RTS;
		ctc_tty_transmit_status(info);
	} else {
		info->mcr &= ~UART_MCR_DTR;
		info->mcr &= ~UART_MCR_RTS;
		ctc_tty_transmit_status(info);
		return;
	}

	/* CTS flow control flag and modem status interrupts */
	if (cflag & CRTSCTS) {
		info->flags |= CTC_ASYNC_CTS_FLOW;
	} else
		info->flags &= ~CTC_ASYNC_CTS_FLOW;
	if (cflag & CLOCAL)
		info->flags &= ~CTC_ASYNC_CHECK_CD;
	else {
		info->flags |= CTC_ASYNC_CHECK_CD;
	}
}

static int
ctc_tty_startup(ctc_tty_info * info)
{
	DBF_TEXT(trace, 3, __FUNCTION__);
	if (info->flags & CTC_ASYNC_INITIALIZED)
		return 0;
#ifdef CTC_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "starting up %s%d ...\n", CTC_TTY_NAME, info->line);
#endif
	/*
	 * Now, initialize the UART
	 */
	info->mcr = UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2;
	if (info->tty)
		clear_bit(TTY_IO_ERROR, &info->tty->flags);
	/*
	 * and set the speed of the serial port
	 */
	ctc_tty_change_speed(info);

	info->flags |= CTC_ASYNC_INITIALIZED;
	if (!(info->flags & CTC_ASYNC_NETDEV_OPEN))
		info->netdev->open(info->netdev);
	info->flags |= CTC_ASYNC_NETDEV_OPEN;
	return 0;
}

static void
ctc_tty_stopdev(unsigned long data)
{
	ctc_tty_info *info = (ctc_tty_info *)data;

	if ((!info) || (!info->netdev) ||
	    (info->flags & CTC_ASYNC_INITIALIZED))
		return;
	info->netdev->stop(info->netdev);
	info->flags &= ~CTC_ASYNC_NETDEV_OPEN;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void
ctc_tty_shutdown(ctc_tty_info * info)
{
	DBF_TEXT(trace, 3, __FUNCTION__);
	if (!(info->flags & CTC_ASYNC_INITIALIZED))
		return;
#ifdef CTC_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "Shutting down %s%d ....\n", CTC_TTY_NAME, info->line);
#endif
	info->msr &= ~UART_MSR_RI;
	if (!info->tty || (info->tty->termios->c_cflag & HUPCL))
		info->mcr &= ~(UART_MCR_DTR | UART_MCR_RTS);
	if (info->tty)
		set_bit(TTY_IO_ERROR, &info->tty->flags);
	mod_timer(&info->stoptimer, jiffies + (10 * HZ));
	skb_queue_purge(&info->tx_queue);
	skb_queue_purge(&info->rx_queue);
	info->flags &= ~CTC_ASYNC_INITIALIZED;
}

/* ctc_tty_write() is the main send-routine. It is called from the upper
 * levels within the kernel to perform sending data. Depending on the
 * online-flag it either directs output to the at-command-interpreter or
 * to the lower level. Additional tasks done here:
 *  - If online, check for escape-sequence (+++)
 *  - If sending audio-data, call ctc_tty_DLEdown() to parse DLE-codes.
 *  - If receiving audio-data, call ctc_tty_end_vrx() to abort if needed.
 *  - If dialing, abort dial.
 */
static int
ctc_tty_write(struct tty_struct *tty, const u_char * buf, int count)
{
	int c;
	int total = 0;
	ctc_tty_info *info = (ctc_tty_info *) tty->driver_data;

	DBF_TEXT(trace, 5, __FUNCTION__);
	if (ctc_tty_shuttingdown)
		goto ex;
	if (ctc_tty_paranoia_check(info, tty->name, "ctc_tty_write"))
		goto ex;
	if (!tty)
		goto ex;
	if (!info->netdev) {
		total = -ENODEV;
		goto ex;
	}
	while (1) {
		struct sk_buff *skb;
		int skb_res;

		c = (count < CTC_TTY_XMIT_SIZE) ? count : CTC_TTY_XMIT_SIZE;
		if (c <= 0)
			break;

		skb_res = info->netdev->hard_header_len + sizeof(info->mcr) +
			+ sizeof(__u32);
		skb = dev_alloc_skb(skb_res + c);
		if (!skb) {
			printk(KERN_WARNING
			       "ctc_tty: Out of memory in %s%d write\n",
			       CTC_TTY_NAME, info->line);
			break;
		}
		skb_reserve(skb, skb_res);
		memcpy(skb_put(skb, c), buf, c);
		skb_queue_tail(&info->tx_queue, skb);
		buf += c;
		total += c;
		count -= c;
	}
	if (!skb_queue_empty(&info->tx_queue)) {
		info->lsr &= ~UART_LSR_TEMT;
		tasklet_schedule(&info->tasklet);
	}
ex:
	DBF_TEXT(trace, 6, __FUNCTION__);
	return total;
}

static int
ctc_tty_write_room(struct tty_struct *tty)
{
	ctc_tty_info *info = (ctc_tty_info *) tty->driver_data;

	if (ctc_tty_paranoia_check(info, tty->name, "ctc_tty_write_room"))
		return 0;
	return CTC_TTY_XMIT_SIZE;
}

static int
ctc_tty_chars_in_buffer(struct tty_struct *tty)
{
	ctc_tty_info *info = (ctc_tty_info *) tty->driver_data;

	if (ctc_tty_paranoia_check(info, tty->name, "ctc_tty_chars_in_buffer"))
		return 0;
	return 0;
}

static void
ctc_tty_flush_buffer(struct tty_struct *tty)
{
	ctc_tty_info *info;
	unsigned long flags;

	DBF_TEXT(trace, 4, __FUNCTION__);
	if (!tty)
		goto ex;
	spin_lock_irqsave(&ctc_tty_lock, flags);
	info = (ctc_tty_info *) tty->driver_data;
	if (ctc_tty_paranoia_check(info, tty->name, "ctc_tty_flush_buffer")) {
		spin_unlock_irqrestore(&ctc_tty_lock, flags);
		goto ex;
	}
	skb_queue_purge(&info->tx_queue);
	info->lsr |= UART_LSR_TEMT;
	spin_unlock_irqrestore(&ctc_tty_lock, flags);
	wake_up_interruptible(&tty->write_wait);
	tty_wakeup(tty);
ex:
	DBF_TEXT_(trace, 2, "ex: %s ", __FUNCTION__);
	return;
}

static void
ctc_tty_flush_chars(struct tty_struct *tty)
{
	ctc_tty_info *info = (ctc_tty_info *) tty->driver_data;

	DBF_TEXT(trace, 4, __FUNCTION__);
	if (ctc_tty_shuttingdown)
		return;
	if (ctc_tty_paranoia_check(info, tty->name, "ctc_tty_flush_chars"))
		return;
	if (tty->stopped || tty->hw_stopped || skb_queue_empty(&info->tx_queue))
		return;
	tasklet_schedule(&info->tasklet);
}

/*
 * ------------------------------------------------------------
 * ctc_tty_throttle()
 *
 * This routine is called by the upper-layer tty layer to signal that
 * incoming characters should be throttled.
 * ------------------------------------------------------------
 */
static void
ctc_tty_throttle(struct tty_struct *tty)
{
	ctc_tty_info *info = (ctc_tty_info *) tty->driver_data;

	DBF_TEXT(trace, 4, __FUNCTION__);
	if (ctc_tty_paranoia_check(info, tty->name, "ctc_tty_throttle"))
		return;
	info->mcr &= ~UART_MCR_RTS;
	if (I_IXOFF(tty))
		ctc_tty_inject(info, STOP_CHAR(tty));
	ctc_tty_transmit_status(info);
}

static void
ctc_tty_unthrottle(struct tty_struct *tty)
{
	ctc_tty_info *info = (ctc_tty_info *) tty->driver_data;

	DBF_TEXT(trace, 4, __FUNCTION__);
	if (ctc_tty_paranoia_check(info, tty->name, "ctc_tty_unthrottle"))
		return;
	info->mcr |= UART_MCR_RTS;
	if (I_IXOFF(tty))
		ctc_tty_inject(info, START_CHAR(tty));
	ctc_tty_transmit_status(info);
}

/*
 * ------------------------------------------------------------
 * ctc_tty_ioctl() and friends
 * ------------------------------------------------------------
 */

/*
 * ctc_tty_get_lsr_info - get line status register info
 *
 * Purpose: Let user call ioctl() to get info when the UART physically
 *          is emptied.  On bus types like RS485, the transmitter must
 *          release the bus after transmitting. This must be done when
 *          the transmit shift register is empty, not be done when the
 *          transmit holding register is empty.  This functionality
 *          allows RS485 driver to be written in user space.
 */
static int
ctc_tty_get_lsr_info(ctc_tty_info * info, uint __user *value)
{
	u_char status;
	uint result;
	ulong flags;

	DBF_TEXT(trace, 4, __FUNCTION__);
	spin_lock_irqsave(&ctc_tty_lock, flags);
	status = info->lsr;
	spin_unlock_irqrestore(&ctc_tty_lock, flags);
	result = ((status & UART_LSR_TEMT) ? TIOCSER_TEMT : 0);
	put_user(result, value);
	return 0;
}


static int ctc_tty_tiocmget(struct tty_struct *tty, struct file *file)
{
	ctc_tty_info *info = (ctc_tty_info *) tty->driver_data;
	u_char control,
	 status;
	uint result;
	ulong flags;

	DBF_TEXT(trace, 4, __FUNCTION__);
	if (ctc_tty_paranoia_check(info, tty->name, "ctc_tty_ioctl"))
		return -ENODEV;
	if (tty->flags & (1 << TTY_IO_ERROR))
		return -EIO;

	control = info->mcr;
	spin_lock_irqsave(&ctc_tty_lock, flags);
	status = info->msr;
	spin_unlock_irqrestore(&ctc_tty_lock, flags);
	result = ((control & UART_MCR_RTS) ? TIOCM_RTS : 0)
	    | ((control & UART_MCR_DTR) ? TIOCM_DTR : 0)
	    | ((status & UART_MSR_DCD) ? TIOCM_CAR : 0)
	    | ((status & UART_MSR_RI) ? TIOCM_RNG : 0)
	    | ((status & UART_MSR_DSR) ? TIOCM_DSR : 0)
	    | ((status & UART_MSR_CTS) ? TIOCM_CTS : 0);
	return result;
}

static int
ctc_tty_tiocmset(struct tty_struct *tty, struct file *file,
		 unsigned int set, unsigned int clear)
{
	ctc_tty_info *info = (ctc_tty_info *) tty->driver_data;

	DBF_TEXT(trace, 4, __FUNCTION__);
	if (ctc_tty_paranoia_check(info, tty->name, "ctc_tty_ioctl"))
		return -ENODEV;
	if (tty->flags & (1 << TTY_IO_ERROR))
		return -EIO;

	if (set & TIOCM_RTS)
		info->mcr |= UART_MCR_RTS;
	if (set & TIOCM_DTR)
		info->mcr |= UART_MCR_DTR;

	if (clear & TIOCM_RTS)
		info->mcr &= ~UART_MCR_RTS;
	if (clear & TIOCM_DTR)
		info->mcr &= ~UART_MCR_DTR;

	if ((set | clear) & (TIOCM_RTS|TIOCM_DTR))
		ctc_tty_transmit_status(info);
	return 0;
}

static int
ctc_tty_ioctl(struct tty_struct *tty, struct file *file,
	       uint cmd, ulong arg)
{
	ctc_tty_info *info = (ctc_tty_info *) tty->driver_data;
	int error;
	int retval;

	DBF_TEXT(trace, 4, __FUNCTION__);
	if (ctc_tty_paranoia_check(info, tty->name, "ctc_tty_ioctl"))
		return -ENODEV;
	if (tty->flags & (1 << TTY_IO_ERROR))
		return -EIO;
	switch (cmd) {
		case TCSBRK:   /* SVID version: non-zero arg --> no break */
#ifdef CTC_DEBUG_MODEM_IOCTL
			printk(KERN_DEBUG "%s%d ioctl TCSBRK\n", CTC_TTY_NAME, info->line);
#endif
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			tty_wait_until_sent(tty, 0);
			return 0;
		case TCSBRKP:  /* support for POSIX tcsendbreak() */
#ifdef CTC_DEBUG_MODEM_IOCTL
			printk(KERN_DEBUG "%s%d ioctl TCSBRKP\n", CTC_TTY_NAME, info->line);
#endif
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			tty_wait_until_sent(tty, 0);
			return 0;
		case TIOCGSOFTCAR:
#ifdef CTC_DEBUG_MODEM_IOCTL
			printk(KERN_DEBUG "%s%d ioctl TIOCGSOFTCAR\n", CTC_TTY_NAME,
			       info->line);
#endif
			error = put_user(C_CLOCAL(tty) ? 1 : 0, (ulong __user *) arg);
			return error;
		case TIOCSSOFTCAR:
#ifdef CTC_DEBUG_MODEM_IOCTL
			printk(KERN_DEBUG "%s%d ioctl TIOCSSOFTCAR\n", CTC_TTY_NAME,
			       info->line);
#endif
			error = get_user(arg, (ulong __user *) arg);
			if (error)
				return error;
			tty->termios->c_cflag =
			    ((tty->termios->c_cflag & ~CLOCAL) |
			     (arg ? CLOCAL : 0));
			return 0;
		case TIOCSERGETLSR:	/* Get line status register */
#ifdef CTC_DEBUG_MODEM_IOCTL
			printk(KERN_DEBUG "%s%d ioctl TIOCSERGETLSR\n", CTC_TTY_NAME,
			       info->line);
#endif
			if (access_ok(VERIFY_WRITE, (void __user *) arg, sizeof(uint)))
				return ctc_tty_get_lsr_info(info, (uint __user *) arg);
			else
				return -EFAULT;
		default:
#ifdef CTC_DEBUG_MODEM_IOCTL
			printk(KERN_DEBUG "UNKNOWN ioctl 0x%08x on %s%d\n", cmd,
			       CTC_TTY_NAME, info->line);
#endif
			return -ENOIOCTLCMD;
	}
	return 0;
}

static void
ctc_tty_set_termios(struct tty_struct *tty, struct termios *old_termios)
{
	ctc_tty_info *info = (ctc_tty_info *) tty->driver_data;
	unsigned int cflag = tty->termios->c_cflag;

	DBF_TEXT(trace, 4, __FUNCTION__);
	ctc_tty_change_speed(info);

	/* Handle transition to B0 */
	if ((old_termios->c_cflag & CBAUD) && !(cflag & CBAUD)) {
		info->mcr &= ~(UART_MCR_DTR|UART_MCR_RTS);
		ctc_tty_transmit_status(info);
	}

	/* Handle transition from B0 to other */
	if (!(old_termios->c_cflag & CBAUD) && (cflag & CBAUD)) {
		info->mcr |= UART_MCR_DTR;
		if (!(tty->termios->c_cflag & CRTSCTS) ||
                    !test_bit(TTY_THROTTLED, &tty->flags)) {
                        info->mcr |= UART_MCR_RTS;
                }
		ctc_tty_transmit_status(info);
	}

	/* Handle turning off CRTSCTS */
	if ((old_termios->c_cflag & CRTSCTS) &&
            !(tty->termios->c_cflag & CRTSCTS))
                tty->hw_stopped = 0;
}

/*
 * ------------------------------------------------------------
 * ctc_tty_open() and friends
 * ------------------------------------------------------------
 */
static int
ctc_tty_block_til_ready(struct tty_struct *tty, struct file *filp, ctc_tty_info *info)
{
	DECLARE_WAITQUEUE(wait, NULL);
	int do_clocal = 0;
	unsigned long flags;
	int retval;

	DBF_TEXT(trace, 4, __FUNCTION__);
	/*
	 * If the device is in the middle of being closed, then block
	 * until it's done, and then try again.
	 */
	if (tty_hung_up_p(filp) ||
	    (info->flags & CTC_ASYNC_CLOSING)) {
		if (info->flags & CTC_ASYNC_CLOSING)
			wait_event(info->close_wait,
				   !(info->flags & CTC_ASYNC_CLOSING));
#ifdef MODEM_DO_RESTART
		if (info->flags & CTC_ASYNC_HUP_NOTIFY)
			return -EAGAIN;
		else
			return -ERESTARTSYS;
#else
		return -EAGAIN;
#endif
	}
	/*
	 * If non-blocking mode is set, then make the check up front
	 * and then exit.
	 */
	if ((filp->f_flags & O_NONBLOCK) ||
	    (tty->flags & (1 << TTY_IO_ERROR))) {
		info->flags |= CTC_ASYNC_NORMAL_ACTIVE;
		return 0;
	}
	if (tty->termios->c_cflag & CLOCAL)
		do_clocal = 1;
	/*
	 * Block waiting for the carrier detect and the line to become
	 * free (i.e., not in use by the callout).  While we are in
	 * this loop, info->count is dropped by one, so that
	 * ctc_tty_close() knows when to free things.  We restore it upon
	 * exit, either normal or abnormal.
	 */
	retval = 0;
	add_wait_queue(&info->open_wait, &wait);
#ifdef CTC_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "ctc_tty_block_til_ready before block: %s%d, count = %d\n",
	       CTC_TTY_NAME, info->line, info->count);
#endif
	spin_lock_irqsave(&ctc_tty_lock, flags);
	if (!(tty_hung_up_p(filp)))
		info->count--;
	spin_unlock_irqrestore(&ctc_tty_lock, flags);
	info->blocked_open++;
	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (tty_hung_up_p(filp) ||
		    !(info->flags & CTC_ASYNC_INITIALIZED)) {
#ifdef MODEM_DO_RESTART
			if (info->flags & CTC_ASYNC_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;
#else
			retval = -EAGAIN;
#endif
			break;
		}
		if (!(info->flags & CTC_ASYNC_CLOSING) &&
		    (do_clocal || (info->msr & UART_MSR_DCD))) {
			break;
		}
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
#ifdef CTC_DEBUG_MODEM_OPEN
		printk(KERN_DEBUG "ctc_tty_block_til_ready blocking: %s%d, count = %d\n",
		       CTC_TTY_NAME, info->line, info->count);
#endif
		schedule();
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&info->open_wait, &wait);
	if (!tty_hung_up_p(filp))
		info->count++;
	info->blocked_open--;
#ifdef CTC_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "ctc_tty_block_til_ready after blocking: %s%d, count = %d\n",
	       CTC_TTY_NAME, info->line, info->count);
#endif
	if (retval)
		return retval;
	info->flags |= CTC_ASYNC_NORMAL_ACTIVE;
	return 0;
}

/*
 * This routine is called whenever a serial port is opened.  It
 * enables interrupts for a serial port, linking in its async structure into
 * the IRQ chain.   It also performs the serial-specific
 * initialization for the tty structure.
 */
static int
ctc_tty_open(struct tty_struct *tty, struct file *filp)
{
	ctc_tty_info *info;
	unsigned long saveflags;
	int retval,
	 line;

	DBF_TEXT(trace, 3, __FUNCTION__);
	line = tty->index;
	if (line < 0 || line > CTC_TTY_MAX_DEVICES)
		return -ENODEV;
	info = &driver->info[line];
	if (ctc_tty_paranoia_check(info, tty->name, "ctc_tty_open"))
		return -ENODEV;
	if (!info->netdev)
		return -ENODEV;
#ifdef CTC_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "ctc_tty_open %s, count = %d\n", tty->name,
	       info->count);
#endif
	spin_lock_irqsave(&ctc_tty_lock, saveflags);
	info->count++;
	tty->driver_data = info;
	info->tty = tty;
	spin_unlock_irqrestore(&ctc_tty_lock, saveflags);
	/*
	 * Start up serial port
	 */
	retval = ctc_tty_startup(info);
	if (retval) {
#ifdef CTC_DEBUG_MODEM_OPEN
		printk(KERN_DEBUG "ctc_tty_open return after startup\n");
#endif
		return retval;
	}
	retval = ctc_tty_block_til_ready(tty, filp, info);
	if (retval) {
#ifdef CTC_DEBUG_MODEM_OPEN
		printk(KERN_DEBUG "ctc_tty_open return after ctc_tty_block_til_ready \n");
#endif
		return retval;
	}
#ifdef CTC_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "ctc_tty_open %s successful...\n", tty->name);
#endif
	return 0;
}

static void
ctc_tty_close(struct tty_struct *tty, struct file *filp)
{
	ctc_tty_info *info = (ctc_tty_info *) tty->driver_data;
	ulong flags;
	ulong timeout;
	DBF_TEXT(trace, 3, __FUNCTION__);
	if (!info || ctc_tty_paranoia_check(info, tty->name, "ctc_tty_close"))
		return;
	spin_lock_irqsave(&ctc_tty_lock, flags);
	if (tty_hung_up_p(filp)) {
		spin_unlock_irqrestore(&ctc_tty_lock, flags);
#ifdef CTC_DEBUG_MODEM_OPEN
		printk(KERN_DEBUG "ctc_tty_close return after tty_hung_up_p\n");
#endif
		return;
	}
	if ((tty->count == 1) && (info->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  Info->count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * serial port won't be shutdown.
		 */
		printk(KERN_ERR "ctc_tty_close: bad port count; tty->count is 1, "
		       "info->count is %d\n", info->count);
		info->count = 1;
	}
	if (--info->count < 0) {
		printk(KERN_ERR "ctc_tty_close: bad port count for %s%d: %d\n",
		       CTC_TTY_NAME, info->line, info->count);
		info->count = 0;
	}
	if (info->count) {
		local_irq_restore(flags);
#ifdef CTC_DEBUG_MODEM_OPEN
		printk(KERN_DEBUG "ctc_tty_close after info->count != 0\n");
#endif
		return;
	}
	info->flags |= CTC_ASYNC_CLOSING;
	tty->closing = 1;
	/*
	 * At this point we stop accepting input.  To do this, we
	 * disable the receive line status interrupts, and tell the
	 * interrupt driver to stop checking the data ready bit in the
	 * line status register.
	 */
	if (info->flags & CTC_ASYNC_INITIALIZED) {
		tty_wait_until_sent(tty, 30*HZ); /* 30 seconds timeout */
		/*
		 * Before we drop DTR, make sure the UART transmitter
		 * has completely drained; this is especially
		 * important if there is a transmit FIFO!
		 */
		timeout = jiffies + HZ;
		while (!(info->lsr & UART_LSR_TEMT)) {
			spin_unlock_irqrestore(&ctc_tty_lock, flags);
			msleep(500);
			spin_lock_irqsave(&ctc_tty_lock, flags);
			if (time_after(jiffies,timeout))
				break;
		}
	}
	ctc_tty_shutdown(info);
	if (tty->driver->flush_buffer) {
		skb_queue_purge(&info->tx_queue);
		info->lsr |= UART_LSR_TEMT;
	}
	tty_ldisc_flush(tty);
	info->tty = 0;
	tty->closing = 0;
	if (info->blocked_open) {
		msleep_interruptible(500);
		wake_up_interruptible(&info->open_wait);
	}
	info->flags &= ~(CTC_ASYNC_NORMAL_ACTIVE | CTC_ASYNC_CLOSING);
	wake_up_interruptible(&info->close_wait);
	spin_unlock_irqrestore(&ctc_tty_lock, flags);
#ifdef CTC_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "ctc_tty_close normal exit\n");
#endif
}

/*
 * ctc_tty_hangup() --- called by tty_hangup() when a hangup is signaled.
 */
static void
ctc_tty_hangup(struct tty_struct *tty)
{
	ctc_tty_info *info = (ctc_tty_info *)tty->driver_data;
	unsigned long saveflags;
	DBF_TEXT(trace, 3, __FUNCTION__);
	if (ctc_tty_paranoia_check(info, tty->name, "ctc_tty_hangup"))
		return;
	ctc_tty_shutdown(info);
	info->count = 0;
	info->flags &= ~CTC_ASYNC_NORMAL_ACTIVE;
	spin_lock_irqsave(&ctc_tty_lock, saveflags);
	info->tty = 0;
	spin_unlock_irqrestore(&ctc_tty_lock, saveflags);
	wake_up_interruptible(&info->open_wait);
}


/*
 * For all online tty's, try sending data to
 * the lower levels.
 */
static void
ctc_tty_task(unsigned long arg)
{
	ctc_tty_info *info = (void *)arg;
	unsigned long saveflags;
	int again;

	DBF_TEXT(trace, 3, __FUNCTION__);
	spin_lock_irqsave(&ctc_tty_lock, saveflags);
	if ((!ctc_tty_shuttingdown) && info) {
		again = ctc_tty_tint(info);
		if (!again)
			info->lsr |= UART_LSR_TEMT;
		again |= ctc_tty_readmodem(info);
		if (again) {
			tasklet_schedule(&info->tasklet);
		}
	}
	spin_unlock_irqrestore(&ctc_tty_lock, saveflags);
}

static struct tty_operations ctc_ops = {
	.open = ctc_tty_open,
	.close = ctc_tty_close,
	.write = ctc_tty_write,
	.flush_chars = ctc_tty_flush_chars,
	.write_room = ctc_tty_write_room,
	.chars_in_buffer = ctc_tty_chars_in_buffer,
	.flush_buffer = ctc_tty_flush_buffer,
	.ioctl = ctc_tty_ioctl,
	.throttle = ctc_tty_throttle,
	.unthrottle = ctc_tty_unthrottle,
	.set_termios = ctc_tty_set_termios,
	.hangup = ctc_tty_hangup,
	.tiocmget = ctc_tty_tiocmget,
	.tiocmset = ctc_tty_tiocmset,
};

int
ctc_tty_init(void)
{
	int i;
	ctc_tty_info *info;
	struct tty_driver *device;

	DBF_TEXT(trace, 2, __FUNCTION__);
	driver = kmalloc(sizeof(ctc_tty_driver), GFP_KERNEL);
	if (driver == NULL) {
		printk(KERN_WARNING "Out of memory in ctc_tty_modem_init\n");
		return -ENOMEM;
	}
	memset(driver, 0, sizeof(ctc_tty_driver));
	device = alloc_tty_driver(CTC_TTY_MAX_DEVICES);
	if (!device) {
		kfree(driver);
		printk(KERN_WARNING "Out of memory in ctc_tty_modem_init\n");
		return -ENOMEM;
	}

	device->devfs_name = "ctc/" CTC_TTY_NAME;
	device->name = CTC_TTY_NAME;
	device->major = CTC_TTY_MAJOR;
	device->minor_start = 0;
	device->type = TTY_DRIVER_TYPE_SERIAL;
	device->subtype = SERIAL_TYPE_NORMAL;
	device->init_termios = tty_std_termios;
	device->init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	device->flags = TTY_DRIVER_REAL_RAW;
	device->driver_name = "ctc_tty",
	tty_set_operations(device, &ctc_ops);
	if (tty_register_driver(device)) {
		printk(KERN_WARNING "ctc_tty: Couldn't register serial-device\n");
		put_tty_driver(device);
		kfree(driver);
		return -1;
	}
	driver->ctc_tty_device = device;
	for (i = 0; i < CTC_TTY_MAX_DEVICES; i++) {
		info = &driver->info[i];
		init_MUTEX(&info->write_sem);
		tasklet_init(&info->tasklet, ctc_tty_task,
				(unsigned long) info);
		info->magic = CTC_ASYNC_MAGIC;
		info->line = i;
		info->tty = 0;
		info->count = 0;
		info->blocked_open = 0;
		init_waitqueue_head(&info->open_wait);
		init_waitqueue_head(&info->close_wait);
		skb_queue_head_init(&info->tx_queue);
		skb_queue_head_init(&info->rx_queue);
		init_timer(&info->stoptimer);
		info->stoptimer.function = ctc_tty_stopdev;
		info->stoptimer.data = (unsigned long)info;
		info->mcr = UART_MCR_RTS;
	}
	return 0;
}

int
ctc_tty_register_netdev(struct net_device *dev) {
	int ttynum;
	char *err;
	char *p;

	DBF_TEXT(trace, 2, __FUNCTION__);
	if ((!dev) || (!dev->name)) {
		printk(KERN_WARNING
		       "ctc_tty_register_netdev called "
		       "with NULL dev or NULL dev-name\n");
		return -1;
	}

	/*
	 *	If the name is a format string the caller wants us to
	 *	do a name allocation : format string must end with %d
	 */
	if (strchr(dev->name, '%'))
	{
		int err = dev_alloc_name(dev, dev->name);	// dev->name is changed by this
		if (err < 0) {
			printk(KERN_DEBUG "dev_alloc returned error %d\n", err);
			return err;
		}

	}

	for (p = dev->name; p && ((*p < '0') || (*p > '9')); p++);
	ttynum = simple_strtoul(p, &err, 0);
	if ((ttynum < 0) || (ttynum >= CTC_TTY_MAX_DEVICES) ||
	    (err && *err)) {
		printk(KERN_WARNING
		       "ctc_tty_register_netdev called "
		       "with number in name '%s'\n", dev->name);
		return -1;
	}
	if (driver->info[ttynum].netdev) {
		printk(KERN_WARNING
		       "ctc_tty_register_netdev called "
		       "for already registered device '%s'\n",
		       dev->name);
		return -1;
	}
	driver->info[ttynum].netdev = dev;
	return 0;
}

void
ctc_tty_unregister_netdev(struct net_device *dev) {
	int i;
	unsigned long saveflags;
	ctc_tty_info *info = NULL;

	DBF_TEXT(trace, 2, __FUNCTION__);
	spin_lock_irqsave(&ctc_tty_lock, saveflags);
	for (i = 0; i < CTC_TTY_MAX_DEVICES; i++)
		if (driver->info[i].netdev == dev) {
			info = &driver->info[i];
			break;
		}
	if (info) {
		info->netdev = NULL;
		skb_queue_purge(&info->tx_queue);
		skb_queue_purge(&info->rx_queue);
	}
	spin_unlock_irqrestore(&ctc_tty_lock, saveflags);
}

void
ctc_tty_cleanup(void) {
	unsigned long saveflags;

	DBF_TEXT(trace, 2, __FUNCTION__);
	spin_lock_irqsave(&ctc_tty_lock, saveflags);
	ctc_tty_shuttingdown = 1;
	spin_unlock_irqrestore(&ctc_tty_lock, saveflags);
	tty_unregister_driver(driver->ctc_tty_device);
	put_tty_driver(driver->ctc_tty_device);
	kfree(driver);
	driver = NULL;
}

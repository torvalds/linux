/*
 * slcan.c - serial line CAN interface driver (using tty line discipline)
 *
 * This file is derived from linux/drivers/net/slip/slip.c and got
 * inspiration from linux/drivers/net/can/can327.c for the rework made
 * on the line discipline code.
 *
 * slip.c Authors  : Laurence Culhane <loz@holmes.demon.co.uk>
 *                   Fred N. van Kempen <waltje@uwalt.nl.mugnet.org>
 * slcan.c Author  : Oliver Hartkopp <socketcan@hartkopp.net>
 * can327.c Author : Max Staudt <max-linux@enpas.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see http://www.gnu.org/licenses/gpl.html
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>

#include <linux/uaccess.h>
#include <linux/bitops.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/workqueue.h>
#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/skb.h>

#include "slcan.h"

MODULE_ALIAS_LDISC(N_SLCAN);
MODULE_DESCRIPTION("serial line CAN interface");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Oliver Hartkopp <socketcan@hartkopp.net>");
MODULE_AUTHOR("Dario Binacchi <dario.binacchi@amarulasolutions.com>");

/* maximum rx buffer len: extended CAN frame with timestamp */
#define SLCAN_MTU (sizeof("T1111222281122334455667788EA5F\r") + 1)

#define SLCAN_CMD_LEN 1
#define SLCAN_SFF_ID_LEN 3
#define SLCAN_EFF_ID_LEN 8
#define SLCAN_DATA_LENGTH_LEN 1
#define SLCAN_ERROR_LEN 1
#define SLCAN_STATE_LEN 1
#define SLCAN_STATE_BE_RXCNT_LEN 3
#define SLCAN_STATE_BE_TXCNT_LEN 3
#define SLCAN_STATE_MSG_LEN     (SLCAN_CMD_LEN +		\
                                 SLCAN_STATE_LEN +		\
                                 SLCAN_STATE_BE_RXCNT_LEN +	\
                                 SLCAN_STATE_BE_TXCNT_LEN)
#define SLCAN_ERROR_MSG_LEN_MIN (SLCAN_CMD_LEN +	\
                                 SLCAN_ERROR_LEN +	\
                                 SLCAN_DATA_LENGTH_LEN)
#define SLCAN_FRAME_MSG_LEN_MIN (SLCAN_CMD_LEN +	\
                                 SLCAN_SFF_ID_LEN +	\
                                 SLCAN_DATA_LENGTH_LEN)
struct slcan {
	struct can_priv         can;

	/* Various fields. */
	struct tty_struct	*tty;		/* ptr to TTY structure	     */
	struct net_device	*dev;		/* easy for intr handling    */
	spinlock_t		lock;
	struct work_struct	tx_work;	/* Flushes transmit buffer   */

	/* These are pointers to the malloc()ed frame buffers. */
	unsigned char		rbuff[SLCAN_MTU];	/* receiver buffer   */
	int			rcount;         /* received chars counter    */
	unsigned char		xbuff[SLCAN_MTU];	/* transmitter buffer*/
	unsigned char		*xhead;         /* pointer to next XMIT byte */
	int			xleft;          /* bytes left in XMIT queue  */

	unsigned long		flags;		/* Flag values/ mode etc     */
#define SLF_ERROR		0               /* Parity, etc. error        */
#define SLF_XCMD		1               /* Command transmission      */
	unsigned long           cmd_flags;      /* Command flags             */
#define CF_ERR_RST		0               /* Reset errors on open      */
	wait_queue_head_t       xcmd_wait;      /* Wait queue for commands   */
						/* transmission              */
};

static const u32 slcan_bitrate_const[] = {
	10000, 20000, 50000, 100000, 125000,
	250000, 500000, 800000, 1000000
};

bool slcan_err_rst_on_open(struct net_device *ndev)
{
	struct slcan *sl = netdev_priv(ndev);

	return !!test_bit(CF_ERR_RST, &sl->cmd_flags);
}

int slcan_enable_err_rst_on_open(struct net_device *ndev, bool on)
{
	struct slcan *sl = netdev_priv(ndev);

	if (netif_running(ndev))
		return -EBUSY;

	if (on)
		set_bit(CF_ERR_RST, &sl->cmd_flags);
	else
		clear_bit(CF_ERR_RST, &sl->cmd_flags);

	return 0;
}

/*************************************************************************
 *			SLCAN ENCAPSULATION FORMAT			 *
 *************************************************************************/

/* A CAN frame has a can_id (11 bit standard frame format OR 29 bit extended
 * frame format) a data length code (len) which can be from 0 to 8
 * and up to <len> data bytes as payload.
 * Additionally a CAN frame may become a remote transmission frame if the
 * RTR-bit is set. This causes another ECU to send a CAN frame with the
 * given can_id.
 *
 * The SLCAN ASCII representation of these different frame types is:
 * <type> <id> <dlc> <data>*
 *
 * Extended frames (29 bit) are defined by capital characters in the type.
 * RTR frames are defined as 'r' types - normal frames have 't' type:
 * t => 11 bit data frame
 * r => 11 bit RTR frame
 * T => 29 bit data frame
 * R => 29 bit RTR frame
 *
 * The <id> is 3 (standard) or 8 (extended) bytes in ASCII Hex (base64).
 * The <dlc> is a one byte ASCII number ('0' - '8')
 * The <data> section has at much ASCII Hex bytes as defined by the <dlc>
 *
 * Examples:
 *
 * t1230 : can_id 0x123, len 0, no data
 * t4563112233 : can_id 0x456, len 3, data 0x11 0x22 0x33
 * T12ABCDEF2AA55 : extended can_id 0x12ABCDEF, len 2, data 0xAA 0x55
 * r1230 : can_id 0x123, len 0, no data, remote transmission request
 *
 */

/*************************************************************************
 *			STANDARD SLCAN DECAPSULATION			 *
 *************************************************************************/

/* Send one completely decapsulated can_frame to the network layer */
static void slcan_bump_frame(struct slcan *sl)
{
	struct sk_buff *skb;
	struct can_frame *cf;
	int i, tmp;
	u32 tmpid;
	char *cmd = sl->rbuff;

	if (sl->rcount < SLCAN_FRAME_MSG_LEN_MIN)
		return;

	skb = alloc_can_skb(sl->dev, &cf);
	if (unlikely(!skb)) {
		sl->dev->stats.rx_dropped++;
		return;
	}

	switch (*cmd) {
	case 'r':
		cf->can_id = CAN_RTR_FLAG;
		fallthrough;
	case 't':
		/* store dlc ASCII value and terminate SFF CAN ID string */
		cf->len = sl->rbuff[SLCAN_CMD_LEN + SLCAN_SFF_ID_LEN];
		sl->rbuff[SLCAN_CMD_LEN + SLCAN_SFF_ID_LEN] = 0;
		/* point to payload data behind the dlc */
		cmd += SLCAN_CMD_LEN + SLCAN_SFF_ID_LEN + 1;
		break;
	case 'R':
		cf->can_id = CAN_RTR_FLAG;
		fallthrough;
	case 'T':
		cf->can_id |= CAN_EFF_FLAG;
		/* store dlc ASCII value and terminate EFF CAN ID string */
		cf->len = sl->rbuff[SLCAN_CMD_LEN + SLCAN_EFF_ID_LEN];
		sl->rbuff[SLCAN_CMD_LEN + SLCAN_EFF_ID_LEN] = 0;
		/* point to payload data behind the dlc */
		cmd += SLCAN_CMD_LEN + SLCAN_EFF_ID_LEN + 1;
		break;
	default:
		goto decode_failed;
	}

	if (kstrtou32(sl->rbuff + SLCAN_CMD_LEN, 16, &tmpid))
		goto decode_failed;

	cf->can_id |= tmpid;

	/* get len from sanitized ASCII value */
	if (cf->len >= '0' && cf->len < '9')
		cf->len -= '0';
	else
		goto decode_failed;

	/* RTR frames may have a dlc > 0 but they never have any data bytes */
	if (!(cf->can_id & CAN_RTR_FLAG)) {
		for (i = 0; i < cf->len; i++) {
			tmp = hex_to_bin(*cmd++);
			if (tmp < 0)
				goto decode_failed;

			cf->data[i] = (tmp << 4);
			tmp = hex_to_bin(*cmd++);
			if (tmp < 0)
				goto decode_failed;

			cf->data[i] |= tmp;
		}
	}

	sl->dev->stats.rx_packets++;
	if (!(cf->can_id & CAN_RTR_FLAG))
		sl->dev->stats.rx_bytes += cf->len;

	netif_rx(skb);
	return;

decode_failed:
	sl->dev->stats.rx_errors++;
	dev_kfree_skb(skb);
}

/* A change state frame must contain state info and receive and transmit
 * error counters.
 *
 * Examples:
 *
 * sb256256 : state bus-off: rx counter 256, tx counter 256
 * sa057033 : state active, rx counter 57, tx counter 33
 */
static void slcan_bump_state(struct slcan *sl)
{
	struct net_device *dev = sl->dev;
	struct sk_buff *skb;
	struct can_frame *cf;
	char *cmd = sl->rbuff;
	u32 rxerr, txerr;
	enum can_state state, rx_state, tx_state;

	switch (cmd[1]) {
	case 'a':
		state = CAN_STATE_ERROR_ACTIVE;
		break;
	case 'w':
		state = CAN_STATE_ERROR_WARNING;
		break;
	case 'p':
		state = CAN_STATE_ERROR_PASSIVE;
		break;
	case 'b':
		state = CAN_STATE_BUS_OFF;
		break;
	default:
		return;
	}

	if (state == sl->can.state || sl->rcount != SLCAN_STATE_MSG_LEN)
		return;

	cmd += SLCAN_STATE_BE_RXCNT_LEN + SLCAN_CMD_LEN + 1;
	cmd[SLCAN_STATE_BE_TXCNT_LEN] = 0;
	if (kstrtou32(cmd, 10, &txerr))
		return;

	*cmd = 0;
	cmd -= SLCAN_STATE_BE_RXCNT_LEN;
	if (kstrtou32(cmd, 10, &rxerr))
		return;

	skb = alloc_can_err_skb(dev, &cf);

	tx_state = txerr >= rxerr ? state : 0;
	rx_state = txerr <= rxerr ? state : 0;
	can_change_state(dev, cf, tx_state, rx_state);

	if (state == CAN_STATE_BUS_OFF) {
		can_bus_off(dev);
	} else if (skb) {
		cf->can_id |= CAN_ERR_CNT;
		cf->data[6] = txerr;
		cf->data[7] = rxerr;
	}

	if (skb)
		netif_rx(skb);
}

/* An error frame can contain more than one type of error.
 *
 * Examples:
 *
 * e1a : len 1, errors: ACK error
 * e3bcO: len 3, errors: Bit0 error, CRC error, Tx overrun error
 */
static void slcan_bump_err(struct slcan *sl)
{
	struct net_device *dev = sl->dev;
	struct sk_buff *skb;
	struct can_frame *cf;
	char *cmd = sl->rbuff;
	bool rx_errors = false, tx_errors = false, rx_over_errors = false;
	int i, len;

	if (sl->rcount < SLCAN_ERROR_MSG_LEN_MIN)
		return;

	/* get len from sanitized ASCII value */
	len = cmd[1];
	if (len >= '0' && len < '9')
		len -= '0';
	else
		return;

	if ((len + SLCAN_CMD_LEN + 1) > sl->rcount)
		return;

	skb = alloc_can_err_skb(dev, &cf);

	if (skb)
		cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;

	cmd += SLCAN_CMD_LEN + 1;
	for (i = 0; i < len; i++, cmd++) {
		switch (*cmd) {
		case 'a':
			netdev_dbg(dev, "ACK error\n");
			tx_errors = true;
			if (skb) {
				cf->can_id |= CAN_ERR_ACK;
				cf->data[3] = CAN_ERR_PROT_LOC_ACK;
			}

			break;
		case 'b':
			netdev_dbg(dev, "Bit0 error\n");
			tx_errors = true;
			if (skb)
				cf->data[2] |= CAN_ERR_PROT_BIT0;

			break;
		case 'B':
			netdev_dbg(dev, "Bit1 error\n");
			tx_errors = true;
			if (skb)
				cf->data[2] |= CAN_ERR_PROT_BIT1;

			break;
		case 'c':
			netdev_dbg(dev, "CRC error\n");
			rx_errors = true;
			if (skb) {
				cf->data[2] |= CAN_ERR_PROT_BIT;
				cf->data[3] = CAN_ERR_PROT_LOC_CRC_SEQ;
			}

			break;
		case 'f':
			netdev_dbg(dev, "Form Error\n");
			rx_errors = true;
			if (skb)
				cf->data[2] |= CAN_ERR_PROT_FORM;

			break;
		case 'o':
			netdev_dbg(dev, "Rx overrun error\n");
			rx_over_errors = true;
			rx_errors = true;
			if (skb) {
				cf->can_id |= CAN_ERR_CRTL;
				cf->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;
			}

			break;
		case 'O':
			netdev_dbg(dev, "Tx overrun error\n");
			tx_errors = true;
			if (skb) {
				cf->can_id |= CAN_ERR_CRTL;
				cf->data[1] = CAN_ERR_CRTL_TX_OVERFLOW;
			}

			break;
		case 's':
			netdev_dbg(dev, "Stuff error\n");
			rx_errors = true;
			if (skb)
				cf->data[2] |= CAN_ERR_PROT_STUFF;

			break;
		default:
			if (skb)
				dev_kfree_skb(skb);

			return;
		}
	}

	if (rx_errors)
		dev->stats.rx_errors++;

	if (rx_over_errors)
		dev->stats.rx_over_errors++;

	if (tx_errors)
		dev->stats.tx_errors++;

	if (skb)
		netif_rx(skb);
}

static void slcan_bump(struct slcan *sl)
{
	switch (sl->rbuff[0]) {
	case 'r':
		fallthrough;
	case 't':
		fallthrough;
	case 'R':
		fallthrough;
	case 'T':
		return slcan_bump_frame(sl);
	case 'e':
		return slcan_bump_err(sl);
	case 's':
		return slcan_bump_state(sl);
	default:
		return;
	}
}

/* parse tty input stream */
static void slcan_unesc(struct slcan *sl, unsigned char s)
{
	if ((s == '\r') || (s == '\a')) { /* CR or BEL ends the pdu */
		if (!test_and_clear_bit(SLF_ERROR, &sl->flags))
			slcan_bump(sl);

		sl->rcount = 0;
	} else {
		if (!test_bit(SLF_ERROR, &sl->flags))  {
			if (sl->rcount < SLCAN_MTU)  {
				sl->rbuff[sl->rcount++] = s;
				return;
			}

			sl->dev->stats.rx_over_errors++;
			set_bit(SLF_ERROR, &sl->flags);
		}
	}
}

/*************************************************************************
 *			STANDARD SLCAN ENCAPSULATION			 *
 *************************************************************************/

/* Encapsulate one can_frame and stuff into a TTY queue. */
static void slcan_encaps(struct slcan *sl, struct can_frame *cf)
{
	int actual, i;
	unsigned char *pos;
	unsigned char *endpos;
	canid_t id = cf->can_id;

	pos = sl->xbuff;

	if (cf->can_id & CAN_RTR_FLAG)
		*pos = 'R'; /* becomes 'r' in standard frame format (SFF) */
	else
		*pos = 'T'; /* becomes 't' in standard frame format (SSF) */

	/* determine number of chars for the CAN-identifier */
	if (cf->can_id & CAN_EFF_FLAG) {
		id &= CAN_EFF_MASK;
		endpos = pos + SLCAN_EFF_ID_LEN;
	} else {
		*pos |= 0x20; /* convert R/T to lower case for SFF */
		id &= CAN_SFF_MASK;
		endpos = pos + SLCAN_SFF_ID_LEN;
	}

	/* build 3 (SFF) or 8 (EFF) digit CAN identifier */
	pos++;
	while (endpos >= pos) {
		*endpos-- = hex_asc_upper[id & 0xf];
		id >>= 4;
	}

	pos += (cf->can_id & CAN_EFF_FLAG) ?
		SLCAN_EFF_ID_LEN : SLCAN_SFF_ID_LEN;

	*pos++ = cf->len + '0';

	/* RTR frames may have a dlc > 0 but they never have any data bytes */
	if (!(cf->can_id & CAN_RTR_FLAG)) {
		for (i = 0; i < cf->len; i++)
			pos = hex_byte_pack_upper(pos, cf->data[i]);

		sl->dev->stats.tx_bytes += cf->len;
	}

	*pos++ = '\r';

	/* Order of next two lines is *very* important.
	 * When we are sending a little amount of data,
	 * the transfer may be completed inside the ops->write()
	 * routine, because it's running with interrupts enabled.
	 * In this case we *never* got WRITE_WAKEUP event,
	 * if we did not request it before write operation.
	 *       14 Oct 1994  Dmitry Gorodchanin.
	 */
	set_bit(TTY_DO_WRITE_WAKEUP, &sl->tty->flags);
	actual = sl->tty->ops->write(sl->tty, sl->xbuff, pos - sl->xbuff);
	sl->xleft = (pos - sl->xbuff) - actual;
	sl->xhead = sl->xbuff + actual;
}

/* Write out any remaining transmit buffer. Scheduled when tty is writable */
static void slcan_transmit(struct work_struct *work)
{
	struct slcan *sl = container_of(work, struct slcan, tx_work);
	int actual;

	spin_lock_bh(&sl->lock);
	/* First make sure we're connected. */
	if (unlikely(!netif_running(sl->dev)) &&
	    likely(!test_bit(SLF_XCMD, &sl->flags))) {
		spin_unlock_bh(&sl->lock);
		return;
	}

	if (sl->xleft <= 0)  {
		if (unlikely(test_bit(SLF_XCMD, &sl->flags))) {
			clear_bit(SLF_XCMD, &sl->flags);
			clear_bit(TTY_DO_WRITE_WAKEUP, &sl->tty->flags);
			spin_unlock_bh(&sl->lock);
			wake_up(&sl->xcmd_wait);
			return;
		}

		/* Now serial buffer is almost free & we can start
		 * transmission of another packet
		 */
		sl->dev->stats.tx_packets++;
		clear_bit(TTY_DO_WRITE_WAKEUP, &sl->tty->flags);
		spin_unlock_bh(&sl->lock);
		netif_wake_queue(sl->dev);
		return;
	}

	actual = sl->tty->ops->write(sl->tty, sl->xhead, sl->xleft);
	sl->xleft -= actual;
	sl->xhead += actual;
	spin_unlock_bh(&sl->lock);
}

/* Called by the driver when there's room for more data.
 * Schedule the transmit.
 */
static void slcan_write_wakeup(struct tty_struct *tty)
{
	struct slcan *sl = tty->disc_data;

	schedule_work(&sl->tx_work);
}

/* Send a can_frame to a TTY queue. */
static netdev_tx_t slcan_netdev_xmit(struct sk_buff *skb,
				     struct net_device *dev)
{
	struct slcan *sl = netdev_priv(dev);

	if (can_dev_dropped_skb(dev, skb))
		return NETDEV_TX_OK;

	spin_lock(&sl->lock);
	if (!netif_running(dev))  {
		spin_unlock(&sl->lock);
		netdev_warn(dev, "xmit: iface is down\n");
		goto out;
	}
	if (!sl->tty) {
		spin_unlock(&sl->lock);
		goto out;
	}

	netif_stop_queue(sl->dev);
	slcan_encaps(sl, (struct can_frame *)skb->data); /* encaps & send */
	spin_unlock(&sl->lock);

	skb_tx_timestamp(skb);

out:
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

/******************************************
 *   Routines looking at netdevice side.
 ******************************************/

static int slcan_transmit_cmd(struct slcan *sl, const unsigned char *cmd)
{
	int ret, actual, n;

	spin_lock(&sl->lock);
	if (!sl->tty) {
		spin_unlock(&sl->lock);
		return -ENODEV;
	}

	n = scnprintf(sl->xbuff, sizeof(sl->xbuff), "%s", cmd);
	set_bit(TTY_DO_WRITE_WAKEUP, &sl->tty->flags);
	actual = sl->tty->ops->write(sl->tty, sl->xbuff, n);
	sl->xleft = n - actual;
	sl->xhead = sl->xbuff + actual;
	set_bit(SLF_XCMD, &sl->flags);
	spin_unlock(&sl->lock);
	ret = wait_event_interruptible_timeout(sl->xcmd_wait,
					       !test_bit(SLF_XCMD, &sl->flags),
					       HZ);
	clear_bit(SLF_XCMD, &sl->flags);
	if (ret == -ERESTARTSYS)
		return ret;

	if (ret == 0)
		return -ETIMEDOUT;

	return 0;
}

/* Netdevice UP -> DOWN routine */
static int slcan_netdev_close(struct net_device *dev)
{
	struct slcan *sl = netdev_priv(dev);
	int err;

	if (sl->can.bittiming.bitrate &&
	    sl->can.bittiming.bitrate != CAN_BITRATE_UNKNOWN) {
		err = slcan_transmit_cmd(sl, "C\r");
		if (err)
			netdev_warn(dev,
				    "failed to send close command 'C\\r'\n");
	}

	/* TTY discipline is running. */
	clear_bit(TTY_DO_WRITE_WAKEUP, &sl->tty->flags);
	flush_work(&sl->tx_work);

	netif_stop_queue(dev);
	sl->rcount   = 0;
	sl->xleft    = 0;
	close_candev(dev);
	sl->can.state = CAN_STATE_STOPPED;
	if (sl->can.bittiming.bitrate == CAN_BITRATE_UNKNOWN)
		sl->can.bittiming.bitrate = CAN_BITRATE_UNSET;

	return 0;
}

/* Netdevice DOWN -> UP routine */
static int slcan_netdev_open(struct net_device *dev)
{
	struct slcan *sl = netdev_priv(dev);
	unsigned char cmd[SLCAN_MTU];
	int err, s;

	/* The baud rate is not set with the command
	 * `ip link set <iface> type can bitrate <baud>' and therefore
	 * can.bittiming.bitrate is CAN_BITRATE_UNSET (0), causing
	 * open_candev() to fail. So let's set to a fake value.
	 */
	if (sl->can.bittiming.bitrate == CAN_BITRATE_UNSET)
		sl->can.bittiming.bitrate = CAN_BITRATE_UNKNOWN;

	err = open_candev(dev);
	if (err) {
		netdev_err(dev, "failed to open can device\n");
		return err;
	}

	if (sl->can.bittiming.bitrate != CAN_BITRATE_UNKNOWN) {
		for (s = 0; s < ARRAY_SIZE(slcan_bitrate_const); s++) {
			if (sl->can.bittiming.bitrate == slcan_bitrate_const[s])
				break;
		}

		/* The CAN framework has already validate the bitrate value,
		 * so we can avoid to check if `s' has been properly set.
		 */
		snprintf(cmd, sizeof(cmd), "C\rS%d\r", s);
		err = slcan_transmit_cmd(sl, cmd);
		if (err) {
			netdev_err(dev,
				   "failed to send bitrate command 'C\\rS%d\\r'\n",
				   s);
			goto cmd_transmit_failed;
		}

		if (test_bit(CF_ERR_RST, &sl->cmd_flags)) {
			err = slcan_transmit_cmd(sl, "F\r");
			if (err) {
				netdev_err(dev,
					   "failed to send error command 'F\\r'\n");
				goto cmd_transmit_failed;
			}
		}

		if (sl->can.ctrlmode & CAN_CTRLMODE_LISTENONLY) {
			err = slcan_transmit_cmd(sl, "L\r");
			if (err) {
				netdev_err(dev,
					   "failed to send listen-only command 'L\\r'\n");
				goto cmd_transmit_failed;
			}
		} else {
			err = slcan_transmit_cmd(sl, "O\r");
			if (err) {
				netdev_err(dev,
					   "failed to send open command 'O\\r'\n");
				goto cmd_transmit_failed;
			}
		}
	}

	sl->can.state = CAN_STATE_ERROR_ACTIVE;
	netif_start_queue(dev);
	return 0;

cmd_transmit_failed:
	close_candev(dev);
	return err;
}

static const struct net_device_ops slcan_netdev_ops = {
	.ndo_open               = slcan_netdev_open,
	.ndo_stop               = slcan_netdev_close,
	.ndo_start_xmit         = slcan_netdev_xmit,
	.ndo_change_mtu         = can_change_mtu,
};

/******************************************
 *  Routines looking at TTY side.
 ******************************************/

/* Handle the 'receiver data ready' interrupt.
 * This function is called by the 'tty_io' module in the kernel when
 * a block of SLCAN data has been received, which can now be decapsulated
 * and sent on to some IP layer for further processing. This will not
 * be re-entered while running but other ldisc functions may be called
 * in parallel
 */
static void slcan_receive_buf(struct tty_struct *tty, const u8 *cp,
			      const u8 *fp, size_t count)
{
	struct slcan *sl = tty->disc_data;

	if (!netif_running(sl->dev))
		return;

	/* Read the characters out of the buffer */
	while (count--) {
		if (fp && *fp++) {
			if (!test_and_set_bit(SLF_ERROR, &sl->flags))
				sl->dev->stats.rx_errors++;
			cp++;
			continue;
		}
		slcan_unesc(sl, *cp++);
	}
}

/* Open the high-level part of the SLCAN channel.
 * This function is called by the TTY module when the
 * SLCAN line discipline is called for.
 *
 * Called in process context serialized from other ldisc calls.
 */
static int slcan_open(struct tty_struct *tty)
{
	struct net_device *dev;
	struct slcan *sl;
	int err;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (!tty->ops->write)
		return -EOPNOTSUPP;

	dev = alloc_candev(sizeof(*sl), 1);
	if (!dev)
		return -ENFILE;

	sl = netdev_priv(dev);

	/* Configure TTY interface */
	tty->receive_room = 65536; /* We don't flow control */
	sl->rcount = 0;
	sl->xleft = 0;
	spin_lock_init(&sl->lock);
	INIT_WORK(&sl->tx_work, slcan_transmit);
	init_waitqueue_head(&sl->xcmd_wait);

	/* Configure CAN metadata */
	sl->can.bitrate_const = slcan_bitrate_const;
	sl->can.bitrate_const_cnt = ARRAY_SIZE(slcan_bitrate_const);
	sl->can.ctrlmode_supported = CAN_CTRLMODE_LISTENONLY;

	/* Configure netdev interface */
	sl->dev	= dev;
	dev->netdev_ops = &slcan_netdev_ops;
	dev->ethtool_ops = &slcan_ethtool_ops;

	/* Mark ldisc channel as alive */
	sl->tty = tty;
	tty->disc_data = sl;

	err = register_candev(dev);
	if (err) {
		free_candev(dev);
		pr_err("can't register candev\n");
		return err;
	}

	netdev_info(dev, "slcan on %s.\n", tty->name);
	/* TTY layer expects 0 on success */
	return 0;
}

/* Close down a SLCAN channel.
 * This means flushing out any pending queues, and then returning. This
 * call is serialized against other ldisc functions.
 * Once this is called, no other ldisc function of ours is entered.
 *
 * We also use this method for a hangup event.
 */
static void slcan_close(struct tty_struct *tty)
{
	struct slcan *sl = tty->disc_data;

	unregister_candev(sl->dev);

	/*
	 * The netdev needn't be UP (so .ndo_stop() is not called). Hence make
	 * sure this is not running before freeing it up.
	 */
	flush_work(&sl->tx_work);

	/* Mark channel as dead */
	spin_lock_bh(&sl->lock);
	tty->disc_data = NULL;
	sl->tty = NULL;
	spin_unlock_bh(&sl->lock);

	netdev_info(sl->dev, "slcan off %s.\n", tty->name);
	free_candev(sl->dev);
}

/* Perform I/O control on an active SLCAN channel. */
static int slcan_ioctl(struct tty_struct *tty, unsigned int cmd,
		       unsigned long arg)
{
	struct slcan *sl = tty->disc_data;
	unsigned int tmp;

	switch (cmd) {
	case SIOCGIFNAME:
		tmp = strlen(sl->dev->name) + 1;
		if (copy_to_user((void __user *)arg, sl->dev->name, tmp))
			return -EFAULT;
		return 0;

	case SIOCSIFHWADDR:
		return -EINVAL;

	default:
		return tty_mode_ioctl(tty, cmd, arg);
	}
}

static struct tty_ldisc_ops slcan_ldisc = {
	.owner		= THIS_MODULE,
	.num		= N_SLCAN,
	.name		= KBUILD_MODNAME,
	.open		= slcan_open,
	.close		= slcan_close,
	.ioctl		= slcan_ioctl,
	.receive_buf	= slcan_receive_buf,
	.write_wakeup	= slcan_write_wakeup,
};

static int __init slcan_init(void)
{
	int status;

	pr_info("serial line CAN interface driver\n");

	/* Fill in our line protocol discipline, and register it */
	status = tty_register_ldisc(&slcan_ldisc);
	if (status)
		pr_err("can't register line discipline\n");

	return status;
}

static void __exit slcan_exit(void)
{
	/* This will only be called when all channels have been closed by
	 * userspace - tty_ldisc.c takes care of the module's refcount.
	 */
	tty_unregister_ldisc(&slcan_ldisc);
}

module_init(slcan_init);
module_exit(slcan_exit);

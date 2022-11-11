// SPDX-License-Identifier: GPL-2.0
/* ELM327 based CAN interface driver (tty line discipline)
 *
 * This driver started as a derivative of linux/drivers/net/can/slcan.c
 * and my thanks go to the original authors for their inspiration.
 *
 * can327.c Author : Max Staudt <max-linux@enpas.org>
 * slcan.c Author  : Oliver Hartkopp <socketcan@hartkopp.net>
 * slip.c Authors  : Laurence Culhane <loz@holmes.demon.co.uk>
 *                   Fred N. van Kempen <waltje@uwalt.nl.mugnet.org>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>

#include <linux/bitops.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/tty_ldisc.h>
#include <linux/workqueue.h>

#include <uapi/linux/tty.h>

#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/can/rx-offload.h>

#define CAN327_NAPI_WEIGHT 4

#define CAN327_SIZE_TXBUF 32
#define CAN327_SIZE_RXBUF 1024

#define CAN327_CAN_CONFIG_SEND_SFF 0x8000
#define CAN327_CAN_CONFIG_VARIABLE_DLC 0x4000
#define CAN327_CAN_CONFIG_RECV_BOTH_SFF_EFF 0x2000
#define CAN327_CAN_CONFIG_BAUDRATE_MULT_8_7 0x1000

#define CAN327_DUMMY_CHAR 'y'
#define CAN327_DUMMY_STRING "y"
#define CAN327_READY_CHAR '>'

/* Bits in elm->cmds_todo */
enum can327_tx_do {
	CAN327_TX_DO_CAN_DATA = 0,
	CAN327_TX_DO_CANID_11BIT,
	CAN327_TX_DO_CANID_29BIT_LOW,
	CAN327_TX_DO_CANID_29BIT_HIGH,
	CAN327_TX_DO_CAN_CONFIG_PART2,
	CAN327_TX_DO_CAN_CONFIG,
	CAN327_TX_DO_RESPONSES,
	CAN327_TX_DO_SILENT_MONITOR,
	CAN327_TX_DO_INIT,
};

struct can327 {
	/* This must be the first member when using alloc_candev() */
	struct can_priv can;

	struct can_rx_offload offload;

	/* TTY buffers */
	u8 txbuf[CAN327_SIZE_TXBUF];
	u8 rxbuf[CAN327_SIZE_RXBUF];

	/* Per-channel lock */
	spinlock_t lock;

	/* TTY and netdev devices that we're bridging */
	struct tty_struct *tty;
	struct net_device *dev;

	/* TTY buffer accounting */
	struct work_struct tx_work;	/* Flushes TTY TX buffer */
	u8 *txhead;			/* Next TX byte */
	size_t txleft;			/* Bytes left to TX */
	int rxfill;			/* Bytes already RX'd in buffer */

	/* State machine */
	enum {
		CAN327_STATE_NOTINIT = 0,
		CAN327_STATE_GETDUMMYCHAR,
		CAN327_STATE_GETPROMPT,
		CAN327_STATE_RECEIVING,
	} state;

	/* Things we have yet to send */
	char **next_init_cmd;
	unsigned long cmds_todo;

	/* The CAN frame and config the ELM327 is sending/using,
	 * or will send/use after finishing all cmds_todo
	 */
	struct can_frame can_frame_to_send;
	u16 can_config;
	u8 can_bitrate_divisor;

	/* Parser state */
	bool drop_next_line;

	/* Stop the channel on UART side hardware failure, e.g. stray
	 * characters or neverending lines. This may be caused by bad
	 * UART wiring, a bad ELM327, a bad UART bridge...
	 * Once this is true, nothing will be sent to the TTY.
	 */
	bool uart_side_failure;
};

static inline void can327_uart_side_failure(struct can327 *elm);

static void can327_send(struct can327 *elm, const void *buf, size_t len)
{
	int written;

	lockdep_assert_held(&elm->lock);

	if (elm->uart_side_failure)
		return;

	memcpy(elm->txbuf, buf, len);

	/* Order of next two lines is *very* important.
	 * When we are sending a little amount of data,
	 * the transfer may be completed inside the ops->write()
	 * routine, because it's running with interrupts enabled.
	 * In this case we *never* got WRITE_WAKEUP event,
	 * if we did not request it before write operation.
	 *       14 Oct 1994  Dmitry Gorodchanin.
	 */
	set_bit(TTY_DO_WRITE_WAKEUP, &elm->tty->flags);
	written = elm->tty->ops->write(elm->tty, elm->txbuf, len);
	if (written < 0) {
		netdev_err(elm->dev, "Failed to write to tty %s.\n",
			   elm->tty->name);
		can327_uart_side_failure(elm);
		return;
	}

	elm->txleft = len - written;
	elm->txhead = elm->txbuf + written;
}

/* Take the ELM327 out of almost any state and back into command mode.
 * We send CAN327_DUMMY_CHAR which will either abort any running
 * operation, or be echoed back to us in case we're already in command
 * mode.
 */
static void can327_kick_into_cmd_mode(struct can327 *elm)
{
	lockdep_assert_held(&elm->lock);

	if (elm->state != CAN327_STATE_GETDUMMYCHAR &&
	    elm->state != CAN327_STATE_GETPROMPT) {
		can327_send(elm, CAN327_DUMMY_STRING, 1);

		elm->state = CAN327_STATE_GETDUMMYCHAR;
	}
}

/* Schedule a CAN frame and necessary config changes to be sent to the TTY. */
static void can327_send_frame(struct can327 *elm, struct can_frame *frame)
{
	lockdep_assert_held(&elm->lock);

	/* Schedule any necessary changes in ELM327's CAN configuration */
	if (elm->can_frame_to_send.can_id != frame->can_id) {
		/* Set the new CAN ID for transmission. */
		if ((frame->can_id ^ elm->can_frame_to_send.can_id)
		    & CAN_EFF_FLAG) {
			elm->can_config =
				(frame->can_id & CAN_EFF_FLAG ? 0 : CAN327_CAN_CONFIG_SEND_SFF) |
				CAN327_CAN_CONFIG_VARIABLE_DLC |
				CAN327_CAN_CONFIG_RECV_BOTH_SFF_EFF |
				elm->can_bitrate_divisor;

			set_bit(CAN327_TX_DO_CAN_CONFIG, &elm->cmds_todo);
		}

		if (frame->can_id & CAN_EFF_FLAG) {
			clear_bit(CAN327_TX_DO_CANID_11BIT, &elm->cmds_todo);
			set_bit(CAN327_TX_DO_CANID_29BIT_LOW, &elm->cmds_todo);
			set_bit(CAN327_TX_DO_CANID_29BIT_HIGH, &elm->cmds_todo);
		} else {
			set_bit(CAN327_TX_DO_CANID_11BIT, &elm->cmds_todo);
			clear_bit(CAN327_TX_DO_CANID_29BIT_LOW,
				  &elm->cmds_todo);
			clear_bit(CAN327_TX_DO_CANID_29BIT_HIGH,
				  &elm->cmds_todo);
		}
	}

	/* Schedule the CAN frame itself. */
	elm->can_frame_to_send = *frame;
	set_bit(CAN327_TX_DO_CAN_DATA, &elm->cmds_todo);

	can327_kick_into_cmd_mode(elm);
}

/* ELM327 initialisation sequence.
 * The line length is limited by the buffer in can327_handle_prompt().
 */
static char *can327_init_script[] = {
	"AT WS\r",        /* v1.0: Warm Start */
	"AT PP FF OFF\r", /* v1.0: All Programmable Parameters Off */
	"AT M0\r",        /* v1.0: Memory Off */
	"AT AL\r",        /* v1.0: Allow Long messages */
	"AT BI\r",        /* v1.0: Bypass Initialisation */
	"AT CAF0\r",      /* v1.0: CAN Auto Formatting Off */
	"AT CFC0\r",      /* v1.0: CAN Flow Control Off */
	"AT CF 000\r",    /* v1.0: Reset CAN ID Filter */
	"AT CM 000\r",    /* v1.0: Reset CAN ID Mask */
	"AT E1\r",        /* v1.0: Echo On */
	"AT H1\r",        /* v1.0: Headers On */
	"AT L0\r",        /* v1.0: Linefeeds Off */
	"AT SH 7DF\r",    /* v1.0: Set CAN sending ID to 0x7df */
	"AT ST FF\r",     /* v1.0: Set maximum Timeout for response after TX */
	"AT AT0\r",       /* v1.2: Adaptive Timing Off */
	"AT D1\r",        /* v1.3: Print DLC On */
	"AT S1\r",        /* v1.3: Spaces On */
	"AT TP B\r",      /* v1.0: Try Protocol B */
	NULL
};

static void can327_init_device(struct can327 *elm)
{
	lockdep_assert_held(&elm->lock);

	elm->state = CAN327_STATE_NOTINIT;
	elm->can_frame_to_send.can_id = 0x7df; /* ELM327 HW default */
	elm->rxfill = 0;
	elm->drop_next_line = 0;

	/* We can only set the bitrate as a fraction of 500000.
	 * The bitrates listed in can327_bitrate_const will
	 * limit the user to the right values.
	 */
	elm->can_bitrate_divisor = 500000 / elm->can.bittiming.bitrate;
	elm->can_config =
		CAN327_CAN_CONFIG_SEND_SFF | CAN327_CAN_CONFIG_VARIABLE_DLC |
		CAN327_CAN_CONFIG_RECV_BOTH_SFF_EFF | elm->can_bitrate_divisor;

	/* Configure ELM327 and then start monitoring */
	elm->next_init_cmd = &can327_init_script[0];
	set_bit(CAN327_TX_DO_INIT, &elm->cmds_todo);
	set_bit(CAN327_TX_DO_SILENT_MONITOR, &elm->cmds_todo);
	set_bit(CAN327_TX_DO_RESPONSES, &elm->cmds_todo);
	set_bit(CAN327_TX_DO_CAN_CONFIG, &elm->cmds_todo);

	can327_kick_into_cmd_mode(elm);
}

static void can327_feed_frame_to_netdev(struct can327 *elm, struct sk_buff *skb)
{
	lockdep_assert_held(&elm->lock);

	if (!netif_running(elm->dev))
		return;

	/* Queue for NAPI pickup.
	 * rx-offload will update stats and LEDs for us.
	 */
	if (can_rx_offload_queue_tail(&elm->offload, skb))
		elm->dev->stats.rx_fifo_errors++;

	/* Wake NAPI */
	can_rx_offload_irq_finish(&elm->offload);
}

/* Called when we're out of ideas and just want it all to end. */
static inline void can327_uart_side_failure(struct can327 *elm)
{
	struct can_frame *frame;
	struct sk_buff *skb;

	lockdep_assert_held(&elm->lock);

	elm->uart_side_failure = true;

	clear_bit(TTY_DO_WRITE_WAKEUP, &elm->tty->flags);

	elm->can.can_stats.bus_off++;
	netif_stop_queue(elm->dev);
	elm->can.state = CAN_STATE_BUS_OFF;
	can_bus_off(elm->dev);

	netdev_err(elm->dev,
		   "ELM327 misbehaved. Blocking further communication.\n");

	skb = alloc_can_err_skb(elm->dev, &frame);
	if (!skb)
		return;

	frame->can_id |= CAN_ERR_BUSOFF;
	can327_feed_frame_to_netdev(elm, skb);
}

/* Compares a byte buffer (non-NUL terminated) to the payload part of
 * a string, and returns true iff the buffer (content *and* length) is
 * exactly that string, without the terminating NUL byte.
 *
 * Example: If reference is "BUS ERROR", then this returns true iff nbytes == 9
 *          and !memcmp(buf, "BUS ERROR", 9).
 *
 * The reason to use strings is so we can easily include them in the C
 * code, and to avoid hardcoding lengths.
 */
static inline bool can327_rxbuf_cmp(const u8 *buf, size_t nbytes,
				    const char *reference)
{
	size_t ref_len = strlen(reference);

	return (nbytes == ref_len) && !memcmp(buf, reference, ref_len);
}

static void can327_parse_error(struct can327 *elm, size_t len)
{
	struct can_frame *frame;
	struct sk_buff *skb;

	lockdep_assert_held(&elm->lock);

	skb = alloc_can_err_skb(elm->dev, &frame);
	if (!skb)
		/* It's okay to return here:
		 * The outer parsing loop will drop this UART buffer.
		 */
		return;

	/* Filter possible error messages based on length of RX'd line */
	if (can327_rxbuf_cmp(elm->rxbuf, len, "UNABLE TO CONNECT")) {
		netdev_err(elm->dev,
			   "ELM327 reported UNABLE TO CONNECT. Please check your setup.\n");
	} else if (can327_rxbuf_cmp(elm->rxbuf, len, "BUFFER FULL")) {
		/* This will only happen if the last data line was complete.
		 * Otherwise, can327_parse_frame() will heuristically
		 * emit this kind of error frame instead.
		 */
		frame->can_id |= CAN_ERR_CRTL;
		frame->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;
	} else if (can327_rxbuf_cmp(elm->rxbuf, len, "BUS ERROR")) {
		frame->can_id |= CAN_ERR_BUSERROR;
	} else if (can327_rxbuf_cmp(elm->rxbuf, len, "CAN ERROR")) {
		frame->can_id |= CAN_ERR_PROT;
	} else if (can327_rxbuf_cmp(elm->rxbuf, len, "<RX ERROR")) {
		frame->can_id |= CAN_ERR_PROT;
	} else if (can327_rxbuf_cmp(elm->rxbuf, len, "BUS BUSY")) {
		frame->can_id |= CAN_ERR_PROT;
		frame->data[2] = CAN_ERR_PROT_OVERLOAD;
	} else if (can327_rxbuf_cmp(elm->rxbuf, len, "FB ERROR")) {
		frame->can_id |= CAN_ERR_PROT;
		frame->data[2] = CAN_ERR_PROT_TX;
	} else if (len == 5 && !memcmp(elm->rxbuf, "ERR", 3)) {
		/* ERR is followed by two digits, hence line length 5 */
		netdev_err(elm->dev, "ELM327 reported an ERR%c%c. Please power it off and on again.\n",
			   elm->rxbuf[3], elm->rxbuf[4]);
		frame->can_id |= CAN_ERR_CRTL;
	} else {
		/* Something else has happened.
		 * Maybe garbage on the UART line.
		 * Emit a generic error frame.
		 */
	}

	can327_feed_frame_to_netdev(elm, skb);
}

/* Parse CAN frames coming as ASCII from ELM327.
 * They can be of various formats:
 *
 * 29-bit ID (EFF):  12 34 56 78 D PL PL PL PL PL PL PL PL
 * 11-bit ID (!EFF): 123 D PL PL PL PL PL PL PL PL
 *
 * where D = DLC, PL = payload byte
 *
 * Instead of a payload, RTR indicates a remote request.
 *
 * We will use the spaces and line length to guess the format.
 */
static int can327_parse_frame(struct can327 *elm, size_t len)
{
	struct can_frame *frame;
	struct sk_buff *skb;
	int hexlen;
	int datastart;
	int i;

	lockdep_assert_held(&elm->lock);

	skb = alloc_can_skb(elm->dev, &frame);
	if (!skb)
		return -ENOMEM;

	/* Find first non-hex and non-space character:
	 *  - In the simplest case, there is none.
	 *  - For RTR frames, 'R' is the first non-hex character.
	 *  - An error message may replace the end of the data line.
	 */
	for (hexlen = 0; hexlen <= len; hexlen++) {
		if (hex_to_bin(elm->rxbuf[hexlen]) < 0 &&
		    elm->rxbuf[hexlen] != ' ') {
			break;
		}
	}

	/* Sanity check whether the line is really a clean hexdump,
	 * or terminated by an error message, or contains garbage.
	 */
	if (hexlen < len && !isdigit(elm->rxbuf[hexlen]) &&
	    !isupper(elm->rxbuf[hexlen]) && '<' != elm->rxbuf[hexlen] &&
	    ' ' != elm->rxbuf[hexlen]) {
		/* The line is likely garbled anyway, so bail.
		 * The main code will restart listening.
		 */
		kfree_skb(skb);
		return -ENODATA;
	}

	/* Use spaces in CAN ID to distinguish 29 or 11 bit address length.
	 * No out-of-bounds access:
	 * We use the fact that we can always read from elm->rxbuf.
	 */
	if (elm->rxbuf[2] == ' ' && elm->rxbuf[5] == ' ' &&
	    elm->rxbuf[8] == ' ' && elm->rxbuf[11] == ' ' &&
	    elm->rxbuf[13] == ' ') {
		frame->can_id = CAN_EFF_FLAG;
		datastart = 14;
	} else if (elm->rxbuf[3] == ' ' && elm->rxbuf[5] == ' ') {
		datastart = 6;
	} else {
		/* This is not a well-formatted data line.
		 * Assume it's an error message.
		 */
		kfree_skb(skb);
		return -ENODATA;
	}

	if (hexlen < datastart) {
		/* The line is too short to be a valid frame hex dump.
		 * Something interrupted the hex dump or it is invalid.
		 */
		kfree_skb(skb);
		return -ENODATA;
	}

	/* From here on all chars up to buf[hexlen] are hex or spaces,
	 * at well-defined offsets.
	 */

	/* Read CAN data length */
	frame->len = (hex_to_bin(elm->rxbuf[datastart - 2]) << 0);

	/* Read CAN ID */
	if (frame->can_id & CAN_EFF_FLAG) {
		frame->can_id |= (hex_to_bin(elm->rxbuf[0]) << 28) |
				 (hex_to_bin(elm->rxbuf[1]) << 24) |
				 (hex_to_bin(elm->rxbuf[3]) << 20) |
				 (hex_to_bin(elm->rxbuf[4]) << 16) |
				 (hex_to_bin(elm->rxbuf[6]) << 12) |
				 (hex_to_bin(elm->rxbuf[7]) << 8) |
				 (hex_to_bin(elm->rxbuf[9]) << 4) |
				 (hex_to_bin(elm->rxbuf[10]) << 0);
	} else {
		frame->can_id |= (hex_to_bin(elm->rxbuf[0]) << 8) |
				 (hex_to_bin(elm->rxbuf[1]) << 4) |
				 (hex_to_bin(elm->rxbuf[2]) << 0);
	}

	/* Check for RTR frame */
	if (elm->rxfill >= hexlen + 3 &&
	    !memcmp(&elm->rxbuf[hexlen], "RTR", 3)) {
		frame->can_id |= CAN_RTR_FLAG;
	}

	/* Is the line long enough to hold the advertised payload?
	 * Note: RTR frames have a DLC, but no actual payload.
	 */
	if (!(frame->can_id & CAN_RTR_FLAG) &&
	    (hexlen < frame->len * 3 + datastart)) {
		/* Incomplete frame.
		 * Probably the ELM327's RS232 TX buffer was full.
		 * Emit an error frame and exit.
		 */
		frame->can_id = CAN_ERR_FLAG | CAN_ERR_CRTL;
		frame->len = CAN_ERR_DLC;
		frame->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;
		can327_feed_frame_to_netdev(elm, skb);

		/* Signal failure to parse.
		 * The line will be re-parsed as an error line, which will fail.
		 * However, this will correctly drop the state machine back into
		 * command mode.
		 */
		return -ENODATA;
	}

	/* Parse the data nibbles. */
	for (i = 0; i < frame->len; i++) {
		frame->data[i] =
			(hex_to_bin(elm->rxbuf[datastart + 3 * i]) << 4) |
			(hex_to_bin(elm->rxbuf[datastart + 3 * i + 1]));
	}

	/* Feed the frame to the network layer. */
	can327_feed_frame_to_netdev(elm, skb);

	return 0;
}

static void can327_parse_line(struct can327 *elm, size_t len)
{
	lockdep_assert_held(&elm->lock);

	/* Skip empty lines */
	if (!len)
		return;

	/* Skip echo lines */
	if (elm->drop_next_line) {
		elm->drop_next_line = 0;
		return;
	} else if (!memcmp(elm->rxbuf, "AT", 2)) {
		return;
	}

	/* Regular parsing */
	if (elm->state == CAN327_STATE_RECEIVING &&
	    can327_parse_frame(elm, len)) {
		/* Parse an error line. */
		can327_parse_error(elm, len);

		/* Start afresh. */
		can327_kick_into_cmd_mode(elm);
	}
}

static void can327_handle_prompt(struct can327 *elm)
{
	struct can_frame *frame = &elm->can_frame_to_send;
	/* Size this buffer for the largest ELM327 line we may generate,
	 * which is currently an 8 byte CAN frame's payload hexdump.
	 * Items in can327_init_script must fit here, too!
	 */
	char local_txbuf[sizeof("0102030405060708\r")];

	lockdep_assert_held(&elm->lock);

	if (!elm->cmds_todo) {
		/* Enter CAN monitor mode */
		can327_send(elm, "ATMA\r", 5);
		elm->state = CAN327_STATE_RECEIVING;

		/* We will be in the default state once this command is
		 * sent, so enable the TX packet queue.
		 */
		netif_wake_queue(elm->dev);

		return;
	}

	/* Reconfigure ELM327 step by step as indicated by elm->cmds_todo */
	if (test_bit(CAN327_TX_DO_INIT, &elm->cmds_todo)) {
		snprintf(local_txbuf, sizeof(local_txbuf), "%s",
			 *elm->next_init_cmd);

		elm->next_init_cmd++;
		if (!(*elm->next_init_cmd)) {
			clear_bit(CAN327_TX_DO_INIT, &elm->cmds_todo);
			/* Init finished. */
		}

	} else if (test_and_clear_bit(CAN327_TX_DO_SILENT_MONITOR, &elm->cmds_todo)) {
		snprintf(local_txbuf, sizeof(local_txbuf),
			 "ATCSM%i\r",
			 !!(elm->can.ctrlmode & CAN_CTRLMODE_LISTENONLY));

	} else if (test_and_clear_bit(CAN327_TX_DO_RESPONSES, &elm->cmds_todo)) {
		snprintf(local_txbuf, sizeof(local_txbuf),
			 "ATR%i\r",
			 !(elm->can.ctrlmode & CAN_CTRLMODE_LISTENONLY));

	} else if (test_and_clear_bit(CAN327_TX_DO_CAN_CONFIG, &elm->cmds_todo)) {
		snprintf(local_txbuf, sizeof(local_txbuf),
			 "ATPC\r");
		set_bit(CAN327_TX_DO_CAN_CONFIG_PART2, &elm->cmds_todo);

	} else if (test_and_clear_bit(CAN327_TX_DO_CAN_CONFIG_PART2, &elm->cmds_todo)) {
		snprintf(local_txbuf, sizeof(local_txbuf),
			 "ATPB%04X\r",
			 elm->can_config);

	} else if (test_and_clear_bit(CAN327_TX_DO_CANID_29BIT_HIGH, &elm->cmds_todo)) {
		snprintf(local_txbuf, sizeof(local_txbuf),
			 "ATCP%02X\r",
			 (frame->can_id & CAN_EFF_MASK) >> 24);

	} else if (test_and_clear_bit(CAN327_TX_DO_CANID_29BIT_LOW, &elm->cmds_todo)) {
		snprintf(local_txbuf, sizeof(local_txbuf),
			 "ATSH%06X\r",
			 frame->can_id & CAN_EFF_MASK & ((1 << 24) - 1));

	} else if (test_and_clear_bit(CAN327_TX_DO_CANID_11BIT, &elm->cmds_todo)) {
		snprintf(local_txbuf, sizeof(local_txbuf),
			 "ATSH%03X\r",
			 frame->can_id & CAN_SFF_MASK);

	} else if (test_and_clear_bit(CAN327_TX_DO_CAN_DATA, &elm->cmds_todo)) {
		if (frame->can_id & CAN_RTR_FLAG) {
			/* Send an RTR frame. Their DLC is fixed.
			 * Some chips don't send them at all.
			 */
			snprintf(local_txbuf, sizeof(local_txbuf), "ATRTR\r");
		} else {
			/* Send a regular CAN data frame */
			int i;

			for (i = 0; i < frame->len; i++) {
				snprintf(&local_txbuf[2 * i],
					 sizeof(local_txbuf), "%02X",
					 frame->data[i]);
			}

			snprintf(&local_txbuf[2 * i], sizeof(local_txbuf),
				 "\r");
		}

		elm->drop_next_line = 1;
		elm->state = CAN327_STATE_RECEIVING;

		/* We will be in the default state once this command is
		 * sent, so enable the TX packet queue.
		 */
		netif_wake_queue(elm->dev);
	}

	can327_send(elm, local_txbuf, strlen(local_txbuf));
}

static bool can327_is_ready_char(char c)
{
	/* Bits 0xc0 are sometimes set (randomly), hence the mask.
	 * Probably bad hardware.
	 */
	return (c & 0x3f) == CAN327_READY_CHAR;
}

static void can327_drop_bytes(struct can327 *elm, size_t i)
{
	lockdep_assert_held(&elm->lock);

	memmove(&elm->rxbuf[0], &elm->rxbuf[i], CAN327_SIZE_RXBUF - i);
	elm->rxfill -= i;
}

static void can327_parse_rxbuf(struct can327 *elm, size_t first_new_char_idx)
{
	size_t len, pos;

	lockdep_assert_held(&elm->lock);

	switch (elm->state) {
	case CAN327_STATE_NOTINIT:
		elm->rxfill = 0;
		break;

	case CAN327_STATE_GETDUMMYCHAR:
		/* Wait for 'y' or '>' */
		for (pos = 0; pos < elm->rxfill; pos++) {
			if (elm->rxbuf[pos] == CAN327_DUMMY_CHAR) {
				can327_send(elm, "\r", 1);
				elm->state = CAN327_STATE_GETPROMPT;
				pos++;
				break;
			} else if (can327_is_ready_char(elm->rxbuf[pos])) {
				can327_send(elm, CAN327_DUMMY_STRING, 1);
				pos++;
				break;
			}
		}

		can327_drop_bytes(elm, pos);
		break;

	case CAN327_STATE_GETPROMPT:
		/* Wait for '>' */
		if (can327_is_ready_char(elm->rxbuf[elm->rxfill - 1]))
			can327_handle_prompt(elm);

		elm->rxfill = 0;
		break;

	case CAN327_STATE_RECEIVING:
		/* Find <CR> delimiting feedback lines. */
		len = first_new_char_idx;
		while (len < elm->rxfill && elm->rxbuf[len] != '\r')
			len++;

		if (len == CAN327_SIZE_RXBUF) {
			/* Assume the buffer ran full with garbage.
			 * Did we even connect at the right baud rate?
			 */
			netdev_err(elm->dev,
				   "RX buffer overflow. Faulty ELM327 or UART?\n");
			can327_uart_side_failure(elm);
		} else if (len == elm->rxfill) {
			if (can327_is_ready_char(elm->rxbuf[elm->rxfill - 1])) {
				/* The ELM327's AT ST response timeout ran out,
				 * so we got a prompt.
				 * Clear RX buffer and restart listening.
				 */
				elm->rxfill = 0;

				can327_handle_prompt(elm);
			}

			/* No <CR> found - we haven't received a full line yet.
			 * Wait for more data.
			 */
		} else {
			/* We have a full line to parse. */
			can327_parse_line(elm, len);

			/* Remove parsed data from RX buffer. */
			can327_drop_bytes(elm, len + 1);

			/* More data to parse? */
			if (elm->rxfill)
				can327_parse_rxbuf(elm, 0);
		}
	}
}

static int can327_netdev_open(struct net_device *dev)
{
	struct can327 *elm = netdev_priv(dev);
	int err;

	spin_lock_bh(&elm->lock);

	if (!elm->tty) {
		spin_unlock_bh(&elm->lock);
		return -ENODEV;
	}

	if (elm->uart_side_failure)
		netdev_warn(elm->dev,
			    "Reopening netdev after a UART side fault has been detected.\n");

	/* Clear TTY buffers */
	elm->rxfill = 0;
	elm->txleft = 0;

	/* open_candev() checks for elm->can.bittiming.bitrate != 0 */
	err = open_candev(dev);
	if (err) {
		spin_unlock_bh(&elm->lock);
		return err;
	}

	can327_init_device(elm);
	spin_unlock_bh(&elm->lock);

	err = can_rx_offload_add_manual(dev, &elm->offload, CAN327_NAPI_WEIGHT);
	if (err) {
		close_candev(dev);
		return err;
	}

	can_rx_offload_enable(&elm->offload);

	elm->can.state = CAN_STATE_ERROR_ACTIVE;
	netif_start_queue(dev);

	return 0;
}

static int can327_netdev_close(struct net_device *dev)
{
	struct can327 *elm = netdev_priv(dev);

	/* Interrupt whatever the ELM327 is doing right now */
	spin_lock_bh(&elm->lock);
	can327_send(elm, CAN327_DUMMY_STRING, 1);
	spin_unlock_bh(&elm->lock);

	netif_stop_queue(dev);

	/* Give UART one final chance to flush. */
	clear_bit(TTY_DO_WRITE_WAKEUP, &elm->tty->flags);
	flush_work(&elm->tx_work);

	can_rx_offload_disable(&elm->offload);
	elm->can.state = CAN_STATE_STOPPED;
	can_rx_offload_del(&elm->offload);
	close_candev(dev);

	return 0;
}

/* Send a can_frame to a TTY. */
static netdev_tx_t can327_netdev_start_xmit(struct sk_buff *skb,
					    struct net_device *dev)
{
	struct can327 *elm = netdev_priv(dev);
	struct can_frame *frame = (struct can_frame *)skb->data;

	if (can_dev_dropped_skb(dev, skb))
		return NETDEV_TX_OK;

	/* We shouldn't get here after a hardware fault:
	 * can_bus_off() calls netif_carrier_off()
	 */
	if (elm->uart_side_failure) {
		WARN_ON_ONCE(elm->uart_side_failure);
		goto out;
	}

	netif_stop_queue(dev);

	/* BHs are already disabled, so no spin_lock_bh().
	 * See Documentation/networking/netdevices.rst
	 */
	spin_lock(&elm->lock);
	can327_send_frame(elm, frame);
	spin_unlock(&elm->lock);

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += frame->can_id & CAN_RTR_FLAG ? 0 : frame->len;

	skb_tx_timestamp(skb);

out:
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops can327_netdev_ops = {
	.ndo_open = can327_netdev_open,
	.ndo_stop = can327_netdev_close,
	.ndo_start_xmit = can327_netdev_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static const struct ethtool_ops can327_ethtool_ops = {
	.get_ts_info = ethtool_op_get_ts_info,
};

static bool can327_is_valid_rx_char(u8 c)
{
	static const bool lut_char_is_valid['z'] = {
		['\r'] = true,
		[' '] = true,
		['.'] = true,
		['0'] = true, true, true, true, true,
		['5'] = true, true, true, true, true,
		['<'] = true,
		[CAN327_READY_CHAR] = true,
		['?'] = true,
		['A'] = true, true, true, true, true, true, true,
		['H'] = true, true, true, true, true, true, true,
		['O'] = true, true, true, true, true, true, true,
		['V'] = true, true, true, true, true,
		['a'] = true,
		['b'] = true,
		['v'] = true,
		[CAN327_DUMMY_CHAR] = true,
	};
	BUILD_BUG_ON(CAN327_DUMMY_CHAR >= 'z');

	return (c < ARRAY_SIZE(lut_char_is_valid) && lut_char_is_valid[c]);
}

/* Handle incoming ELM327 ASCII data.
 * This will not be re-entered while running, but other ldisc
 * functions may be called in parallel.
 */
static void can327_ldisc_rx(struct tty_struct *tty, const unsigned char *cp,
			    const char *fp, int count)
{
	struct can327 *elm = (struct can327 *)tty->disc_data;
	size_t first_new_char_idx;

	if (elm->uart_side_failure)
		return;

	spin_lock_bh(&elm->lock);

	/* Store old rxfill, so can327_parse_rxbuf() will have
	 * the option of skipping already checked characters.
	 */
	first_new_char_idx = elm->rxfill;

	while (count-- && elm->rxfill < CAN327_SIZE_RXBUF) {
		if (fp && *fp++) {
			netdev_err(elm->dev,
				   "Error in received character stream. Check your wiring.");

			can327_uart_side_failure(elm);

			spin_unlock_bh(&elm->lock);
			return;
		}

		/* Ignore NUL characters, which the PIC microcontroller may
		 * inadvertently insert due to a known hardware bug.
		 * See ELM327 documentation, which refers to a Microchip PIC
		 * bug description.
		 */
		if (*cp) {
			/* Check for stray characters on the UART line.
			 * Likely caused by bad hardware.
			 */
			if (!can327_is_valid_rx_char(*cp)) {
				netdev_err(elm->dev,
					   "Received illegal character %02x.\n",
					   *cp);
				can327_uart_side_failure(elm);

				spin_unlock_bh(&elm->lock);
				return;
			}

			elm->rxbuf[elm->rxfill++] = *cp;
		}

		cp++;
	}

	if (count >= 0) {
		netdev_err(elm->dev,
			   "Receive buffer overflowed. Bad chip or wiring? count = %i",
			   count);

		can327_uart_side_failure(elm);

		spin_unlock_bh(&elm->lock);
		return;
	}

	can327_parse_rxbuf(elm, first_new_char_idx);
	spin_unlock_bh(&elm->lock);
}

/* Write out remaining transmit buffer.
 * Scheduled when TTY is writable.
 */
static void can327_ldisc_tx_worker(struct work_struct *work)
{
	struct can327 *elm = container_of(work, struct can327, tx_work);
	ssize_t written;

	if (elm->uart_side_failure)
		return;

	spin_lock_bh(&elm->lock);

	if (elm->txleft) {
		written = elm->tty->ops->write(elm->tty, elm->txhead,
					       elm->txleft);
		if (written < 0) {
			netdev_err(elm->dev, "Failed to write to tty %s.\n",
				   elm->tty->name);
			can327_uart_side_failure(elm);

			spin_unlock_bh(&elm->lock);
			return;
		}

		elm->txleft -= written;
		elm->txhead += written;
	}

	if (!elm->txleft)
		clear_bit(TTY_DO_WRITE_WAKEUP, &elm->tty->flags);

	spin_unlock_bh(&elm->lock);
}

/* Called by the driver when there's room for more data. */
static void can327_ldisc_tx_wakeup(struct tty_struct *tty)
{
	struct can327 *elm = (struct can327 *)tty->disc_data;

	schedule_work(&elm->tx_work);
}

/* ELM327 can only handle bitrates that are integer divisors of 500 kHz,
 * or 7/8 of that. Divisors are 1 to 64.
 * Currently we don't implement support for 7/8 rates.
 */
static const u32 can327_bitrate_const[] = {
	7812,  7936,  8064,  8196,   8333,   8474,   8620,   8771,
	8928,  9090,  9259,  9433,   9615,   9803,   10000,  10204,
	10416, 10638, 10869, 11111,  11363,  11627,  11904,  12195,
	12500, 12820, 13157, 13513,  13888,  14285,  14705,  15151,
	15625, 16129, 16666, 17241,  17857,  18518,  19230,  20000,
	20833, 21739, 22727, 23809,  25000,  26315,  27777,  29411,
	31250, 33333, 35714, 38461,  41666,  45454,  50000,  55555,
	62500, 71428, 83333, 100000, 125000, 166666, 250000, 500000
};

static int can327_ldisc_open(struct tty_struct *tty)
{
	struct net_device *dev;
	struct can327 *elm;
	int err;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (!tty->ops->write)
		return -EOPNOTSUPP;

	dev = alloc_candev(sizeof(struct can327), 0);
	if (!dev)
		return -ENFILE;
	elm = netdev_priv(dev);

	/* Configure TTY interface */
	tty->receive_room = 65536; /* We don't flow control */
	spin_lock_init(&elm->lock);
	INIT_WORK(&elm->tx_work, can327_ldisc_tx_worker);

	/* Configure CAN metadata */
	elm->can.bitrate_const = can327_bitrate_const;
	elm->can.bitrate_const_cnt = ARRAY_SIZE(can327_bitrate_const);
	elm->can.ctrlmode_supported = CAN_CTRLMODE_LISTENONLY;

	/* Configure netdev interface */
	elm->dev = dev;
	dev->netdev_ops = &can327_netdev_ops;
	dev->ethtool_ops = &can327_ethtool_ops;

	/* Mark ldisc channel as alive */
	elm->tty = tty;
	tty->disc_data = elm;

	/* Let 'er rip */
	err = register_candev(elm->dev);
	if (err) {
		free_candev(elm->dev);
		return err;
	}

	netdev_info(elm->dev, "can327 on %s.\n", tty->name);

	return 0;
}

/* Close down a can327 channel.
 * This means flushing out any pending queues, and then returning.
 * This call is serialized against other ldisc functions:
 * Once this is called, no other ldisc function of ours is entered.
 *
 * We also use this function for a hangup event.
 */
static void can327_ldisc_close(struct tty_struct *tty)
{
	struct can327 *elm = (struct can327 *)tty->disc_data;

	/* unregister_netdev() calls .ndo_stop() so we don't have to.
	 * Our .ndo_stop() also flushes the TTY write wakeup handler,
	 * so we can safely set elm->tty = NULL after this.
	 */
	unregister_candev(elm->dev);

	/* Mark channel as dead */
	spin_lock_bh(&elm->lock);
	tty->disc_data = NULL;
	elm->tty = NULL;
	spin_unlock_bh(&elm->lock);

	netdev_info(elm->dev, "can327 off %s.\n", tty->name);

	free_candev(elm->dev);
}

static int can327_ldisc_ioctl(struct tty_struct *tty, unsigned int cmd,
			      unsigned long arg)
{
	struct can327 *elm = (struct can327 *)tty->disc_data;
	unsigned int tmp;

	switch (cmd) {
	case SIOCGIFNAME:
		tmp = strnlen(elm->dev->name, IFNAMSIZ - 1) + 1;
		if (copy_to_user((void __user *)arg, elm->dev->name, tmp))
			return -EFAULT;
		return 0;

	case SIOCSIFHWADDR:
		return -EINVAL;

	default:
		return tty_mode_ioctl(tty, cmd, arg);
	}
}

static struct tty_ldisc_ops can327_ldisc = {
	.owner = THIS_MODULE,
	.name = KBUILD_MODNAME,
	.num = N_CAN327,
	.receive_buf = can327_ldisc_rx,
	.write_wakeup = can327_ldisc_tx_wakeup,
	.open = can327_ldisc_open,
	.close = can327_ldisc_close,
	.ioctl = can327_ldisc_ioctl,
};

static int __init can327_init(void)
{
	int status;

	status = tty_register_ldisc(&can327_ldisc);
	if (status)
		pr_err("Can't register line discipline\n");

	return status;
}

static void __exit can327_exit(void)
{
	/* This will only be called when all channels have been closed by
	 * userspace - tty_ldisc.c takes care of the module's refcount.
	 */
	tty_unregister_ldisc(&can327_ldisc);
}

module_init(can327_init);
module_exit(can327_exit);

MODULE_ALIAS_LDISC(N_CAN327);
MODULE_DESCRIPTION("ELM327 based CAN interface");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Max Staudt <max@enpas.org>");

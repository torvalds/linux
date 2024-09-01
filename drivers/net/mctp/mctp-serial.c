// SPDX-License-Identifier: GPL-2.0
/*
 * Management Component Transport Protocol (MCTP) - serial transport
 * binding. This driver is an implementation of the DMTF specificiation
 * "DSP0253 - Management Component Transport Protocol (MCTP) Serial Transport
 * Binding", available at:
 *
 *  https://www.dmtf.org/sites/default/files/standards/documents/DSP0253_1.0.0.pdf
 *
 * This driver provides DSP0253-type MCTP-over-serial transport using a Linux
 * tty device, by setting the N_MCTP line discipline on the tty.
 *
 * Copyright (c) 2021 Code Construct
 */

#include <linux/idr.h>
#include <linux/if_arp.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/tty.h>
#include <linux/workqueue.h>
#include <linux/crc-ccitt.h>

#include <linux/mctp.h>
#include <net/mctp.h>
#include <net/pkt_sched.h>

#define MCTP_SERIAL_MTU		68 /* base mtu (64) + mctp header */
#define MCTP_SERIAL_FRAME_MTU	(MCTP_SERIAL_MTU + 6) /* + serial framing */

#define MCTP_SERIAL_VERSION	0x1 /* DSP0253 defines a single version: 1 */

#define BUFSIZE			MCTP_SERIAL_FRAME_MTU

#define BYTE_FRAME		0x7e
#define BYTE_ESC		0x7d

#define FCS_INIT		0xffff

static DEFINE_IDA(mctp_serial_ida);

enum mctp_serial_state {
	STATE_IDLE,
	STATE_START,
	STATE_HEADER,
	STATE_DATA,
	STATE_ESCAPE,
	STATE_TRAILER,
	STATE_DONE,
	STATE_ERR,
};

struct mctp_serial {
	struct net_device	*netdev;
	struct tty_struct	*tty;

	int			idx;

	/* protects our rx & tx state machines; held during both paths */
	spinlock_t		lock;

	struct work_struct	tx_work;
	enum mctp_serial_state	txstate, rxstate;
	u16			txfcs, rxfcs, rxfcs_rcvd;
	unsigned int		txlen, rxlen;
	unsigned int		txpos, rxpos;
	unsigned char		txbuf[BUFSIZE],
				rxbuf[BUFSIZE];
};

static bool needs_escape(unsigned char c)
{
	return c == BYTE_ESC || c == BYTE_FRAME;
}

static int next_chunk_len(struct mctp_serial *dev)
{
	int i;

	/* either we have no bytes to send ... */
	if (dev->txpos == dev->txlen)
		return 0;

	/* ... or the next byte to send is an escaped byte; requiring a
	 * single-byte chunk...
	 */
	if (needs_escape(dev->txbuf[dev->txpos]))
		return 1;

	/* ... or we have one or more bytes up to the next escape - this chunk
	 * will be those non-escaped bytes, and does not include the escaped
	 * byte.
	 */
	for (i = 1; i + dev->txpos < dev->txlen; i++) {
		if (needs_escape(dev->txbuf[dev->txpos + i]))
			break;
	}

	return i;
}

static int write_chunk(struct mctp_serial *dev, unsigned char *buf, int len)
{
	return dev->tty->ops->write(dev->tty, buf, len);
}

static void mctp_serial_tx_work(struct work_struct *work)
{
	struct mctp_serial *dev = container_of(work, struct mctp_serial,
					       tx_work);
	unsigned char c, buf[3];
	unsigned long flags;
	int len, txlen;

	spin_lock_irqsave(&dev->lock, flags);

	/* txstate represents the next thing to send */
	switch (dev->txstate) {
	case STATE_START:
		dev->txpos = 0;
		fallthrough;
	case STATE_HEADER:
		buf[0] = BYTE_FRAME;
		buf[1] = MCTP_SERIAL_VERSION;
		buf[2] = dev->txlen;

		if (!dev->txpos)
			dev->txfcs = crc_ccitt(FCS_INIT, buf + 1, 2);

		txlen = write_chunk(dev, buf + dev->txpos, 3 - dev->txpos);
		if (txlen <= 0) {
			dev->txstate = STATE_ERR;
		} else {
			dev->txpos += txlen;
			if (dev->txpos == 3) {
				dev->txstate = STATE_DATA;
				dev->txpos = 0;
			}
		}
		break;

	case STATE_ESCAPE:
		buf[0] = dev->txbuf[dev->txpos] & ~0x20;
		txlen = write_chunk(dev, buf, 1);
		if (txlen <= 0) {
			dev->txstate = STATE_ERR;
		} else {
			dev->txpos += txlen;
			if (dev->txpos == dev->txlen) {
				dev->txstate = STATE_TRAILER;
				dev->txpos = 0;
			}
		}

		break;

	case STATE_DATA:
		len = next_chunk_len(dev);
		if (len) {
			c = dev->txbuf[dev->txpos];
			if (len == 1 && needs_escape(c)) {
				buf[0] = BYTE_ESC;
				buf[1] = c & ~0x20;
				dev->txfcs = crc_ccitt_byte(dev->txfcs, c);
				txlen = write_chunk(dev, buf, 2);
				if (txlen == 2)
					dev->txpos++;
				else if (txlen == 1)
					dev->txstate = STATE_ESCAPE;
				else
					dev->txstate = STATE_ERR;
			} else {
				txlen = write_chunk(dev,
						    dev->txbuf + dev->txpos,
						    len);
				if (txlen <= 0) {
					dev->txstate = STATE_ERR;
				} else {
					dev->txfcs = crc_ccitt(dev->txfcs,
							       dev->txbuf +
							       dev->txpos,
							       txlen);
					dev->txpos += txlen;
				}
			}
			if (dev->txstate == STATE_DATA &&
			    dev->txpos == dev->txlen) {
				dev->txstate = STATE_TRAILER;
				dev->txpos = 0;
			}
			break;
		}
		dev->txstate = STATE_TRAILER;
		dev->txpos = 0;
		fallthrough;

	case STATE_TRAILER:
		buf[0] = dev->txfcs >> 8;
		buf[1] = dev->txfcs & 0xff;
		buf[2] = BYTE_FRAME;
		txlen = write_chunk(dev, buf + dev->txpos, 3 - dev->txpos);
		if (txlen <= 0) {
			dev->txstate = STATE_ERR;
		} else {
			dev->txpos += txlen;
			if (dev->txpos == 3) {
				dev->txstate = STATE_DONE;
				dev->txpos = 0;
			}
		}
		break;
	default:
		netdev_err_once(dev->netdev, "invalid tx state %d\n",
				dev->txstate);
	}

	if (dev->txstate == STATE_DONE) {
		dev->netdev->stats.tx_packets++;
		dev->netdev->stats.tx_bytes += dev->txlen;
		dev->txlen = 0;
		dev->txpos = 0;
		clear_bit(TTY_DO_WRITE_WAKEUP, &dev->tty->flags);
		dev->txstate = STATE_IDLE;
		spin_unlock_irqrestore(&dev->lock, flags);

		netif_wake_queue(dev->netdev);
	} else {
		spin_unlock_irqrestore(&dev->lock, flags);
	}
}

static netdev_tx_t mctp_serial_tx(struct sk_buff *skb, struct net_device *ndev)
{
	struct mctp_serial *dev = netdev_priv(ndev);
	unsigned long flags;

	WARN_ON(dev->txstate != STATE_IDLE);

	if (skb->len > MCTP_SERIAL_MTU) {
		dev->netdev->stats.tx_dropped++;
		goto out;
	}

	spin_lock_irqsave(&dev->lock, flags);
	netif_stop_queue(dev->netdev);
	skb_copy_bits(skb, 0, dev->txbuf, skb->len);
	dev->txpos = 0;
	dev->txlen = skb->len;
	dev->txstate = STATE_START;
	spin_unlock_irqrestore(&dev->lock, flags);

	set_bit(TTY_DO_WRITE_WAKEUP, &dev->tty->flags);
	schedule_work(&dev->tx_work);

out:
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static void mctp_serial_tty_write_wakeup(struct tty_struct *tty)
{
	struct mctp_serial *dev = tty->disc_data;

	schedule_work(&dev->tx_work);
}

static void mctp_serial_rx(struct mctp_serial *dev)
{
	struct mctp_skb_cb *cb;
	struct sk_buff *skb;

	if (dev->rxfcs != dev->rxfcs_rcvd) {
		dev->netdev->stats.rx_dropped++;
		dev->netdev->stats.rx_crc_errors++;
		return;
	}

	skb = netdev_alloc_skb(dev->netdev, dev->rxlen);
	if (!skb) {
		dev->netdev->stats.rx_dropped++;
		return;
	}

	skb->protocol = htons(ETH_P_MCTP);
	skb_put_data(skb, dev->rxbuf, dev->rxlen);
	skb_reset_network_header(skb);

	cb = __mctp_cb(skb);
	cb->halen = 0;

	netif_rx(skb);
	dev->netdev->stats.rx_packets++;
	dev->netdev->stats.rx_bytes += dev->rxlen;
}

static void mctp_serial_push_header(struct mctp_serial *dev, unsigned char c)
{
	switch (dev->rxpos) {
	case 0:
		if (c == BYTE_FRAME)
			dev->rxpos++;
		else
			dev->rxstate = STATE_ERR;
		break;
	case 1:
		if (c == MCTP_SERIAL_VERSION) {
			dev->rxpos++;
			dev->rxfcs = crc_ccitt_byte(FCS_INIT, c);
		} else {
			dev->rxstate = STATE_ERR;
		}
		break;
	case 2:
		if (c > MCTP_SERIAL_FRAME_MTU) {
			dev->rxstate = STATE_ERR;
		} else {
			dev->rxlen = c;
			dev->rxpos = 0;
			dev->rxstate = STATE_DATA;
			dev->rxfcs = crc_ccitt_byte(dev->rxfcs, c);
		}
		break;
	}
}

static void mctp_serial_push_trailer(struct mctp_serial *dev, unsigned char c)
{
	switch (dev->rxpos) {
	case 0:
		dev->rxfcs_rcvd = c << 8;
		dev->rxpos++;
		break;
	case 1:
		dev->rxfcs_rcvd |= c;
		dev->rxpos++;
		break;
	case 2:
		if (c != BYTE_FRAME) {
			dev->rxstate = STATE_ERR;
		} else {
			mctp_serial_rx(dev);
			dev->rxlen = 0;
			dev->rxpos = 0;
			dev->rxstate = STATE_IDLE;
		}
		break;
	}
}

static void mctp_serial_push(struct mctp_serial *dev, unsigned char c)
{
	switch (dev->rxstate) {
	case STATE_IDLE:
		dev->rxstate = STATE_HEADER;
		fallthrough;
	case STATE_HEADER:
		mctp_serial_push_header(dev, c);
		break;

	case STATE_ESCAPE:
		c |= 0x20;
		fallthrough;
	case STATE_DATA:
		if (dev->rxstate != STATE_ESCAPE && c == BYTE_ESC) {
			dev->rxstate = STATE_ESCAPE;
		} else {
			dev->rxfcs = crc_ccitt_byte(dev->rxfcs, c);
			dev->rxbuf[dev->rxpos] = c;
			dev->rxpos++;
			dev->rxstate = STATE_DATA;
			if (dev->rxpos == dev->rxlen) {
				dev->rxpos = 0;
				dev->rxstate = STATE_TRAILER;
			}
		}
		break;

	case STATE_TRAILER:
		mctp_serial_push_trailer(dev, c);
		break;

	case STATE_ERR:
		if (c == BYTE_FRAME)
			dev->rxstate = STATE_IDLE;
		break;

	default:
		netdev_err_once(dev->netdev, "invalid rx state %d\n",
				dev->rxstate);
	}
}

static void mctp_serial_tty_receive_buf(struct tty_struct *tty, const u8 *c,
					const u8 *f, size_t len)
{
	struct mctp_serial *dev = tty->disc_data;
	int i;

	if (!netif_running(dev->netdev))
		return;

	/* we don't (currently) use the flag bytes, just data. */
	for (i = 0; i < len; i++)
		mctp_serial_push(dev, c[i]);
}

static void mctp_serial_uninit(struct net_device *ndev)
{
	struct mctp_serial *dev = netdev_priv(ndev);

	cancel_work_sync(&dev->tx_work);
}

static const struct net_device_ops mctp_serial_netdev_ops = {
	.ndo_start_xmit = mctp_serial_tx,
	.ndo_uninit = mctp_serial_uninit,
};

static void mctp_serial_setup(struct net_device *ndev)
{
	ndev->type = ARPHRD_MCTP;

	/* we limit at the fixed MTU, which is also the MCTP-standard
	 * baseline MTU, so is also our minimum
	 */
	ndev->mtu = MCTP_SERIAL_MTU;
	ndev->max_mtu = MCTP_SERIAL_MTU;
	ndev->min_mtu = MCTP_SERIAL_MTU;

	ndev->hard_header_len = 0;
	ndev->addr_len = 0;
	ndev->tx_queue_len = DEFAULT_TX_QUEUE_LEN;
	ndev->flags = IFF_NOARP;
	ndev->netdev_ops = &mctp_serial_netdev_ops;
	ndev->needs_free_netdev = true;
}

static int mctp_serial_open(struct tty_struct *tty)
{
	struct mctp_serial *dev;
	struct net_device *ndev;
	char name[32];
	int idx, rc;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (!tty->ops->write)
		return -EOPNOTSUPP;

	idx = ida_alloc(&mctp_serial_ida, GFP_KERNEL);
	if (idx < 0)
		return idx;

	snprintf(name, sizeof(name), "mctpserial%d", idx);
	ndev = alloc_netdev(sizeof(*dev), name, NET_NAME_ENUM,
			    mctp_serial_setup);
	if (!ndev) {
		rc = -ENOMEM;
		goto free_ida;
	}

	dev = netdev_priv(ndev);
	dev->idx = idx;
	dev->tty = tty;
	dev->netdev = ndev;
	dev->txstate = STATE_IDLE;
	dev->rxstate = STATE_IDLE;
	spin_lock_init(&dev->lock);
	INIT_WORK(&dev->tx_work, mctp_serial_tx_work);

	rc = register_netdev(ndev);
	if (rc)
		goto free_netdev;

	tty->receive_room = 64 * 1024;
	tty->disc_data = dev;

	return 0;

free_netdev:
	free_netdev(ndev);

free_ida:
	ida_free(&mctp_serial_ida, idx);
	return rc;
}

static void mctp_serial_close(struct tty_struct *tty)
{
	struct mctp_serial *dev = tty->disc_data;
	int idx = dev->idx;

	unregister_netdev(dev->netdev);
	ida_free(&mctp_serial_ida, idx);
}

static struct tty_ldisc_ops mctp_ldisc = {
	.owner		= THIS_MODULE,
	.num		= N_MCTP,
	.name		= "mctp",
	.open		= mctp_serial_open,
	.close		= mctp_serial_close,
	.receive_buf	= mctp_serial_tty_receive_buf,
	.write_wakeup	= mctp_serial_tty_write_wakeup,
};

static int __init mctp_serial_init(void)
{
	return tty_register_ldisc(&mctp_ldisc);
}

static void __exit mctp_serial_exit(void)
{
	tty_unregister_ldisc(&mctp_ldisc);
}

module_init(mctp_serial_init);
module_exit(mctp_serial_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jeremy Kerr <jk@codeconstruct.com.au>");
MODULE_DESCRIPTION("MCTP Serial transport");

#if IS_ENABLED(CONFIG_MCTP_SERIAL_TEST)
#include <kunit/test.h>

#define MAX_CHUNKS 6
struct test_chunk_tx {
	u8 input_len;
	u8 input[MCTP_SERIAL_MTU];
	u8 chunks[MAX_CHUNKS];
};

static void test_next_chunk_len(struct kunit *test)
{
	struct mctp_serial devx;
	struct mctp_serial *dev = &devx;
	int next;

	const struct test_chunk_tx *params = test->param_value;

	memset(dev, 0x0, sizeof(*dev));
	memcpy(dev->txbuf, params->input, params->input_len);
	dev->txlen = params->input_len;

	for (size_t i = 0; i < MAX_CHUNKS; i++) {
		next = next_chunk_len(dev);
		dev->txpos += next;
		KUNIT_EXPECT_EQ(test, next, params->chunks[i]);

		if (next == 0) {
			KUNIT_EXPECT_EQ(test, dev->txpos, dev->txlen);
			return;
		}
	}

	KUNIT_FAIL_AND_ABORT(test, "Ran out of chunks");
}

static struct test_chunk_tx chunk_tx_tests[] = {
	{
		.input_len = 5,
		.input = { 0x00, 0x11, 0x22, 0x7e, 0x80 },
		.chunks = { 3, 1, 1, 0},
	},
	{
		.input_len = 5,
		.input = { 0x00, 0x11, 0x22, 0x7e, 0x7d },
		.chunks = { 3, 1, 1, 0},
	},
	{
		.input_len = 3,
		.input = { 0x7e, 0x11, 0x22, },
		.chunks = { 1, 2, 0},
	},
	{
		.input_len = 3,
		.input = { 0x7e, 0x7e, 0x7d, },
		.chunks = { 1, 1, 1, 0},
	},
	{
		.input_len = 4,
		.input = { 0x7e, 0x7e, 0x00, 0x7d, },
		.chunks = { 1, 1, 1, 1, 0},
	},
	{
		.input_len = 6,
		.input = { 0x7e, 0x7e, 0x00, 0x7d, 0x10, 0x10},
		.chunks = { 1, 1, 1, 1, 2, 0},
	},
	{
		.input_len = 1,
		.input = { 0x7e },
		.chunks = { 1, 0 },
	},
	{
		.input_len = 1,
		.input = { 0x80 },
		.chunks = { 1, 0 },
	},
	{
		.input_len = 3,
		.input = { 0x80, 0x80, 0x00 },
		.chunks = { 3, 0 },
	},
	{
		.input_len = 7,
		.input = { 0x01, 0x00, 0x08, 0xc8, 0x00, 0x80, 0x02 },
		.chunks = { 7, 0 },
	},
	{
		.input_len = 7,
		.input = { 0x01, 0x00, 0x08, 0xc8, 0x7e, 0x80, 0x02 },
		.chunks = { 4, 1, 2, 0 },
	},
};

KUNIT_ARRAY_PARAM(chunk_tx, chunk_tx_tests, NULL);

static struct kunit_case mctp_serial_test_cases[] = {
	KUNIT_CASE_PARAM(test_next_chunk_len, chunk_tx_gen_params),
};

static struct kunit_suite mctp_serial_test_suite = {
	.name = "mctp_serial",
	.test_cases = mctp_serial_test_cases,
};

kunit_test_suite(mctp_serial_test_suite);

#endif /* CONFIG_MCTP_SERIAL_TEST */

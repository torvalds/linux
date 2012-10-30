/*
 * Copyright (c) 2006 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2006 Sam Leffler, Errno Consulting
 * Copyright (c) 2007 Christoph Hellwig <hch@lst.de>
 * Copyright (c) 2008-2009 Weongyo Jeong <weongyo@freebsd.org>
 * Copyright (c) 2012 Pontus Fuchs <pontus.fuchs@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define AR5523_FLAG_PRE_FIRMWARE	(1 << 0)
#define AR5523_FLAG_ABG			(1 << 1)

#define AR5523_FIRMWARE_FILE	"ar5523.bin"

#define AR5523_CMD_TX_PIPE	0x01
#define	AR5523_DATA_TX_PIPE	0x02
#define	AR5523_CMD_RX_PIPE	0x81
#define	AR5523_DATA_RX_PIPE	0x82

#define ar5523_cmd_tx_pipe(dev) \
	usb_sndbulkpipe((dev), AR5523_CMD_TX_PIPE)
#define ar5523_data_tx_pipe(dev) \
	usb_sndbulkpipe((dev), AR5523_DATA_TX_PIPE)
#define ar5523_cmd_rx_pipe(dev) \
	usb_rcvbulkpipe((dev), AR5523_CMD_RX_PIPE)
#define ar5523_data_rx_pipe(dev) \
	usb_rcvbulkpipe((dev), AR5523_DATA_RX_PIPE)

#define	AR5523_DATA_TIMEOUT	10000
#define	AR5523_CMD_TIMEOUT	1000

#define AR5523_TX_DATA_COUNT		8
#define AR5523_TX_DATA_RESTART_COUNT	2
#define AR5523_RX_DATA_COUNT		16
#define AR5523_RX_DATA_REFILL_COUNT	8

#define AR5523_CMD_ID	1
#define AR5523_DATA_ID	2

#define AR5523_TX_WD_TIMEOUT	(HZ * 2)
#define AR5523_FLUSH_TIMEOUT	(HZ * 3)

enum AR5523_flags {
	AR5523_HW_UP,
	AR5523_USB_DISCONNECTED,
	AR5523_CONNECTED
};

struct ar5523_tx_cmd {
	struct ar5523		*ar;
	struct urb		*urb_tx;
	void			*buf_tx;
	void			*odata;
	int			olen;
	int			flags;
	int			res;
	struct completion	done;
};

/* This struct is placed in tx_info->driver_data. It must not be larger
 *  than IEEE80211_TX_INFO_DRIVER_DATA_SIZE.
 */
struct ar5523_tx_data {
	struct list_head	list;
	struct ar5523		*ar;
	struct sk_buff		*skb;
	struct urb		*urb;
};

struct ar5523_rx_data {
	struct	list_head	list;
	struct ar5523		*ar;
	struct urb		*urb;
	struct sk_buff		*skb;
};

struct ar5523 {
	struct usb_device	*dev;
	struct ieee80211_hw	*hw;

	unsigned long		flags;
	struct mutex		mutex;
	struct workqueue_struct *wq;

	struct ar5523_tx_cmd	tx_cmd;

	struct delayed_work	stat_work;

	struct timer_list	tx_wd_timer;
	struct work_struct	tx_wd_work;
	struct work_struct	tx_work;
	struct list_head	tx_queue_pending;
	struct list_head	tx_queue_submitted;
	spinlock_t		tx_data_list_lock;
	wait_queue_head_t	tx_flush_waitq;

	/* Queued + Submitted TX frames */
	atomic_t		tx_nr_total;

	/* Submitted TX frames */
	atomic_t		tx_nr_pending;

	void			*rx_cmd_buf;
	struct urb		*rx_cmd_urb;

	struct ar5523_rx_data	rx_data[AR5523_RX_DATA_COUNT];
	spinlock_t		rx_data_list_lock;
	struct list_head	rx_data_free;
	struct list_head	rx_data_used;
	atomic_t		rx_data_free_cnt;

	struct work_struct	rx_refill_work;

	unsigned int		rxbufsz;
	u8			serial[16];

	struct ieee80211_channel channels[14];
	struct ieee80211_rate	rates[12];
	struct ieee80211_supported_band band;
	struct ieee80211_vif	*vif;
};

/* flags for sending firmware commands */
#define AR5523_CMD_FLAG_READ	(1 << 1)
#define AR5523_CMD_FLAG_MAGIC	(1 << 2)

#define ar5523_dbg(ar, format, arg...) \
	dev_dbg(&(ar)->dev->dev, format, ## arg)

/* On USB hot-unplug there can be a lot of URBs in flight and they'll all
 * fail. Instead of dealing with them in every possible place just surpress
 * any messages on USB disconnect.
 */
#define ar5523_err(ar, format, arg...) \
do { \
	if (!test_bit(AR5523_USB_DISCONNECTED, &ar->flags)) { \
		dev_err(&(ar)->dev->dev, format, ## arg); \
	} \
} while (0)
#define ar5523_info(ar, format, arg...)	\
	dev_info(&(ar)->dev->dev, format, ## arg)

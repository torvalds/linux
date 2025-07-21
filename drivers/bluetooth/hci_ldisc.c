// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *  Bluetooth HCI UART driver
 *
 *  Copyright (C) 2000-2001  Qualcomm Incorporated
 *  Copyright (C) 2002-2003  Maxim Krasnyansky <maxk@qualcomm.com>
 *  Copyright (C) 2004-2005  Marcel Holtmann <marcel@holtmann.org>
 */

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/poll.h>

#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/signal.h>
#include <linux/ioctl.h>
#include <linux/skbuff.h>
#include <linux/firmware.h>
#include <linux/serdev.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btintel.h"
#include "btbcm.h"
#include "hci_uart.h"

#define VERSION "2.3"

static const struct hci_uart_proto *hup[HCI_UART_MAX_PROTO];

int hci_uart_register_proto(const struct hci_uart_proto *p)
{
	if (p->id >= HCI_UART_MAX_PROTO)
		return -EINVAL;

	if (hup[p->id])
		return -EEXIST;

	hup[p->id] = p;

	BT_INFO("HCI UART protocol %s registered", p->name);

	return 0;
}

int hci_uart_unregister_proto(const struct hci_uart_proto *p)
{
	if (p->id >= HCI_UART_MAX_PROTO)
		return -EINVAL;

	if (!hup[p->id])
		return -EINVAL;

	hup[p->id] = NULL;

	return 0;
}

static const struct hci_uart_proto *hci_uart_get_proto(unsigned int id)
{
	if (id >= HCI_UART_MAX_PROTO)
		return NULL;

	return hup[id];
}

static inline void hci_uart_tx_complete(struct hci_uart *hu, int pkt_type)
{
	struct hci_dev *hdev = hu->hdev;

	/* Update HCI stat counters */
	switch (pkt_type) {
	case HCI_COMMAND_PKT:
		hdev->stat.cmd_tx++;
		break;

	case HCI_ACLDATA_PKT:
		hdev->stat.acl_tx++;
		break;

	case HCI_SCODATA_PKT:
		hdev->stat.sco_tx++;
		break;
	}
}

static inline struct sk_buff *hci_uart_dequeue(struct hci_uart *hu)
{
	struct sk_buff *skb = hu->tx_skb;

	if (!skb) {
		percpu_down_read(&hu->proto_lock);

		if (test_bit(HCI_UART_PROTO_READY, &hu->flags) ||
		    test_bit(HCI_UART_PROTO_INIT, &hu->flags))
			skb = hu->proto->dequeue(hu);

		percpu_up_read(&hu->proto_lock);
	} else {
		hu->tx_skb = NULL;
	}

	return skb;
}

int hci_uart_tx_wakeup(struct hci_uart *hu)
{
	/* This may be called in an IRQ context, so we can't sleep. Therefore
	 * we try to acquire the lock only, and if that fails we assume the
	 * tty is being closed because that is the only time the write lock is
	 * acquired. If, however, at some point in the future the write lock
	 * is also acquired in other situations, then this must be revisited.
	 */
	if (!percpu_down_read_trylock(&hu->proto_lock))
		return 0;

	if (!test_bit(HCI_UART_PROTO_READY, &hu->flags) &&
	    !test_bit(HCI_UART_PROTO_INIT, &hu->flags))
		goto no_schedule;

	set_bit(HCI_UART_TX_WAKEUP, &hu->tx_state);
	if (test_and_set_bit(HCI_UART_SENDING, &hu->tx_state))
		goto no_schedule;

	BT_DBG("");

	schedule_work(&hu->write_work);

no_schedule:
	percpu_up_read(&hu->proto_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(hci_uart_tx_wakeup);

static void hci_uart_write_work(struct work_struct *work)
{
	struct hci_uart *hu = container_of(work, struct hci_uart, write_work);
	struct tty_struct *tty = hu->tty;
	struct hci_dev *hdev = hu->hdev;
	struct sk_buff *skb;

	/* REVISIT: should we cope with bad skbs or ->write() returning
	 * and error value ?
	 */

restart:
	clear_bit(HCI_UART_TX_WAKEUP, &hu->tx_state);

	while ((skb = hci_uart_dequeue(hu))) {
		int len;

		set_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
		len = tty->ops->write(tty, skb->data, skb->len);
		hdev->stat.byte_tx += len;

		skb_pull(skb, len);
		if (skb->len) {
			hu->tx_skb = skb;
			break;
		}

		hci_uart_tx_complete(hu, hci_skb_pkt_type(skb));
		kfree_skb(skb);
	}

	clear_bit(HCI_UART_SENDING, &hu->tx_state);
	if (test_bit(HCI_UART_TX_WAKEUP, &hu->tx_state))
		goto restart;

	wake_up_bit(&hu->tx_state, HCI_UART_SENDING);
}

void hci_uart_init_work(struct work_struct *work)
{
	struct hci_uart *hu = container_of(work, struct hci_uart, init_ready);
	int err;
	struct hci_dev *hdev;

	if (!test_and_clear_bit(HCI_UART_INIT_PENDING, &hu->hdev_flags))
		return;

	err = hci_register_dev(hu->hdev);
	if (err < 0) {
		BT_ERR("Can't register HCI device");
		clear_bit(HCI_UART_PROTO_READY, &hu->flags);
		hu->proto->close(hu);
		hdev = hu->hdev;
		hu->hdev = NULL;
		hci_free_dev(hdev);
		return;
	}

	set_bit(HCI_UART_REGISTERED, &hu->flags);
}

int hci_uart_init_ready(struct hci_uart *hu)
{
	if (!test_bit(HCI_UART_INIT_PENDING, &hu->hdev_flags))
		return -EALREADY;

	schedule_work(&hu->init_ready);

	return 0;
}

int hci_uart_wait_until_sent(struct hci_uart *hu)
{
	return wait_on_bit_timeout(&hu->tx_state, HCI_UART_SENDING,
				   TASK_INTERRUPTIBLE,
				   msecs_to_jiffies(2000));
}

/* ------- Interface to HCI layer ------ */
/* Reset device */
static int hci_uart_flush(struct hci_dev *hdev)
{
	struct hci_uart *hu  = hci_get_drvdata(hdev);
	struct tty_struct *tty = hu->tty;

	BT_DBG("hdev %p tty %p", hdev, tty);

	if (hu->tx_skb) {
		kfree_skb(hu->tx_skb); hu->tx_skb = NULL;
	}

	/* Flush any pending characters in the driver and discipline. */
	tty_ldisc_flush(tty);
	tty_driver_flush_buffer(tty);

	percpu_down_read(&hu->proto_lock);

	if (test_bit(HCI_UART_PROTO_READY, &hu->flags))
		hu->proto->flush(hu);

	percpu_up_read(&hu->proto_lock);

	return 0;
}

/* Initialize device */
static int hci_uart_open(struct hci_dev *hdev)
{
	BT_DBG("%s %p", hdev->name, hdev);

	/* Undo clearing this from hci_uart_close() */
	hdev->flush = hci_uart_flush;

	return 0;
}

/* Close device */
static int hci_uart_close(struct hci_dev *hdev)
{
	BT_DBG("hdev %p", hdev);

	hci_uart_flush(hdev);
	hdev->flush = NULL;
	return 0;
}

/* Send frames from HCI layer */
static int hci_uart_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);

	BT_DBG("%s: type %d len %d", hdev->name, hci_skb_pkt_type(skb),
	       skb->len);

	percpu_down_read(&hu->proto_lock);

	if (!test_bit(HCI_UART_PROTO_READY, &hu->flags) &&
	    !test_bit(HCI_UART_PROTO_INIT, &hu->flags)) {
		percpu_up_read(&hu->proto_lock);
		return -EUNATCH;
	}

	hu->proto->enqueue(hu, skb);
	percpu_up_read(&hu->proto_lock);

	hci_uart_tx_wakeup(hu);

	return 0;
}

/* Check the underlying device or tty has flow control support */
bool hci_uart_has_flow_control(struct hci_uart *hu)
{
	/* serdev nodes check if the needed operations are present */
	if (hu->serdev)
		return true;

	if (hu->tty->driver->ops->tiocmget && hu->tty->driver->ops->tiocmset)
		return true;

	return false;
}

/* Flow control or un-flow control the device */
void hci_uart_set_flow_control(struct hci_uart *hu, bool enable)
{
	struct tty_struct *tty = hu->tty;
	struct ktermios ktermios;
	int status;
	unsigned int set = 0;
	unsigned int clear = 0;

	if (hu->serdev) {
		serdev_device_set_flow_control(hu->serdev, !enable);
		serdev_device_set_rts(hu->serdev, !enable);
		return;
	}

	if (enable) {
		/* Disable hardware flow control */
		ktermios = tty->termios;
		ktermios.c_cflag &= ~CRTSCTS;
		tty_set_termios(tty, &ktermios);
		BT_DBG("Disabling hardware flow control: %s",
		       (tty->termios.c_cflag & CRTSCTS) ? "failed" : "success");

		/* Clear RTS to prevent the device from sending */
		/* Most UARTs need OUT2 to enable interrupts */
		status = tty->driver->ops->tiocmget(tty);
		BT_DBG("Current tiocm 0x%x", status);

		set &= ~(TIOCM_OUT2 | TIOCM_RTS);
		clear = ~set;
		set &= TIOCM_DTR | TIOCM_RTS | TIOCM_OUT1 |
		       TIOCM_OUT2 | TIOCM_LOOP;
		clear &= TIOCM_DTR | TIOCM_RTS | TIOCM_OUT1 |
			 TIOCM_OUT2 | TIOCM_LOOP;
		status = tty->driver->ops->tiocmset(tty, set, clear);
		BT_DBG("Clearing RTS: %s", status ? "failed" : "success");
	} else {
		/* Set RTS to allow the device to send again */
		status = tty->driver->ops->tiocmget(tty);
		BT_DBG("Current tiocm 0x%x", status);

		set |= (TIOCM_OUT2 | TIOCM_RTS);
		clear = ~set;
		set &= TIOCM_DTR | TIOCM_RTS | TIOCM_OUT1 |
		       TIOCM_OUT2 | TIOCM_LOOP;
		clear &= TIOCM_DTR | TIOCM_RTS | TIOCM_OUT1 |
			 TIOCM_OUT2 | TIOCM_LOOP;
		status = tty->driver->ops->tiocmset(tty, set, clear);
		BT_DBG("Setting RTS: %s", status ? "failed" : "success");

		/* Re-enable hardware flow control */
		ktermios = tty->termios;
		ktermios.c_cflag |= CRTSCTS;
		tty_set_termios(tty, &ktermios);
		BT_DBG("Enabling hardware flow control: %s",
		       !(tty->termios.c_cflag & CRTSCTS) ? "failed" : "success");
	}
}

void hci_uart_set_speeds(struct hci_uart *hu, unsigned int init_speed,
			 unsigned int oper_speed)
{
	hu->init_speed = init_speed;
	hu->oper_speed = oper_speed;
}

void hci_uart_set_baudrate(struct hci_uart *hu, unsigned int speed)
{
	struct tty_struct *tty = hu->tty;
	struct ktermios ktermios;

	ktermios = tty->termios;
	ktermios.c_cflag &= ~CBAUD;
	tty_termios_encode_baud_rate(&ktermios, speed, speed);

	/* tty_set_termios() return not checked as it is always 0 */
	tty_set_termios(tty, &ktermios);

	BT_DBG("%s: New tty speeds: %d/%d", hu->hdev->name,
	       tty->termios.c_ispeed, tty->termios.c_ospeed);
}

static int hci_uart_setup(struct hci_dev *hdev)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);
	struct hci_rp_read_local_version *ver;
	struct sk_buff *skb;
	unsigned int speed;
	int err;

	/* Init speed if any */
	if (hu->init_speed)
		speed = hu->init_speed;
	else if (hu->proto->init_speed)
		speed = hu->proto->init_speed;
	else
		speed = 0;

	if (speed)
		hci_uart_set_baudrate(hu, speed);

	/* Operational speed if any */
	if (hu->oper_speed)
		speed = hu->oper_speed;
	else if (hu->proto->oper_speed)
		speed = hu->proto->oper_speed;
	else
		speed = 0;

	if (hu->proto->set_baudrate && speed) {
		err = hu->proto->set_baudrate(hu, speed);
		if (!err)
			hci_uart_set_baudrate(hu, speed);
	}

	if (hu->proto->setup)
		return hu->proto->setup(hu);

	if (!test_bit(HCI_UART_VND_DETECT, &hu->hdev_flags))
		return 0;

	skb = __hci_cmd_sync(hdev, HCI_OP_READ_LOCAL_VERSION, 0, NULL,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		BT_ERR("%s: Reading local version information failed (%ld)",
		       hdev->name, PTR_ERR(skb));
		return 0;
	}

	if (skb->len != sizeof(*ver)) {
		BT_ERR("%s: Event length mismatch for version information",
		       hdev->name);
		goto done;
	}

	ver = (struct hci_rp_read_local_version *)skb->data;

	switch (le16_to_cpu(ver->manufacturer)) {
#ifdef CONFIG_BT_HCIUART_INTEL
	case 2:
		hdev->set_bdaddr = btintel_set_bdaddr;
		btintel_check_bdaddr(hdev);
		break;
#endif
#ifdef CONFIG_BT_HCIUART_BCM
	case 15:
		hdev->set_bdaddr = btbcm_set_bdaddr;
		btbcm_check_bdaddr(hdev);
		break;
#endif
	default:
		break;
	}

done:
	kfree_skb(skb);
	return 0;
}

/* ------ LDISC part ------ */
/* hci_uart_tty_open
 *
 *     Called when line discipline changed to HCI_UART.
 *
 * Arguments:
 *     tty    pointer to tty info structure
 * Return Value:
 *     0 if success, otherwise error code
 */
static int hci_uart_tty_open(struct tty_struct *tty)
{
	struct hci_uart *hu;

	BT_DBG("tty %p", tty);

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	/* Error if the tty has no write op instead of leaving an exploitable
	 * hole
	 */
	if (tty->ops->write == NULL)
		return -EOPNOTSUPP;

	hu = kzalloc(sizeof(*hu), GFP_KERNEL);
	if (!hu) {
		BT_ERR("Can't allocate control structure");
		return -ENFILE;
	}
	if (percpu_init_rwsem(&hu->proto_lock)) {
		BT_ERR("Can't allocate semaphore structure");
		kfree(hu);
		return -ENOMEM;
	}

	tty->disc_data = hu;
	hu->tty = tty;
	tty->receive_room = 65536;

	/* disable alignment support by default */
	hu->alignment = 1;
	hu->padding = 0;

	/* Use serial port speed as oper_speed */
	hu->oper_speed = tty->termios.c_ospeed;

	INIT_WORK(&hu->init_ready, hci_uart_init_work);
	INIT_WORK(&hu->write_work, hci_uart_write_work);

	/* Flush any pending characters in the driver */
	tty_driver_flush_buffer(tty);

	return 0;
}

/* hci_uart_tty_close()
 *
 *    Called when the line discipline is changed to something
 *    else, the tty is closed, or the tty detects a hangup.
 */
static void hci_uart_tty_close(struct tty_struct *tty)
{
	struct hci_uart *hu = tty->disc_data;
	struct hci_dev *hdev;

	BT_DBG("tty %p", tty);

	/* Detach from the tty */
	tty->disc_data = NULL;

	if (!hu)
		return;

	hdev = hu->hdev;
	if (hdev)
		hci_uart_close(hdev);

	if (test_bit(HCI_UART_PROTO_READY, &hu->flags)) {
		percpu_down_write(&hu->proto_lock);
		clear_bit(HCI_UART_PROTO_READY, &hu->flags);
		percpu_up_write(&hu->proto_lock);

		cancel_work_sync(&hu->init_ready);
		cancel_work_sync(&hu->write_work);

		if (hdev) {
			if (test_bit(HCI_UART_REGISTERED, &hu->flags))
				hci_unregister_dev(hdev);
			hci_free_dev(hdev);
		}
		hu->proto->close(hu);
	}
	clear_bit(HCI_UART_PROTO_SET, &hu->flags);

	percpu_free_rwsem(&hu->proto_lock);

	kfree(hu);
}

/* hci_uart_tty_wakeup()
 *
 *    Callback for transmit wakeup. Called when low level
 *    device driver can accept more send data.
 *
 * Arguments:        tty    pointer to associated tty instance data
 * Return Value:    None
 */
static void hci_uart_tty_wakeup(struct tty_struct *tty)
{
	struct hci_uart *hu = tty->disc_data;

	BT_DBG("");

	if (!hu)
		return;

	clear_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);

	if (tty != hu->tty)
		return;

	if (test_bit(HCI_UART_PROTO_READY, &hu->flags) ||
	    test_bit(HCI_UART_PROTO_INIT, &hu->flags))
		hci_uart_tx_wakeup(hu);
}

/* hci_uart_tty_receive()
 *
 *     Called by tty low level driver when receive data is
 *     available.
 *
 * Arguments:  tty          pointer to tty instance data
 *             data         pointer to received data
 *             flags        pointer to flags for data
 *             count        count of received data in bytes
 *
 * Return Value:    None
 */
static void hci_uart_tty_receive(struct tty_struct *tty, const u8 *data,
				 const u8 *flags, size_t count)
{
	struct hci_uart *hu = tty->disc_data;

	if (!hu || tty != hu->tty)
		return;

	percpu_down_read(&hu->proto_lock);

	if (!test_bit(HCI_UART_PROTO_READY, &hu->flags) &&
	    !test_bit(HCI_UART_PROTO_INIT, &hu->flags)) {
		percpu_up_read(&hu->proto_lock);
		return;
	}

	/* It does not need a lock here as it is already protected by a mutex in
	 * tty caller
	 */
	hu->proto->recv(hu, data, count);
	percpu_up_read(&hu->proto_lock);

	if (hu->hdev)
		hu->hdev->stat.byte_rx += count;

	tty_unthrottle(tty);
}

static int hci_uart_register_dev(struct hci_uart *hu)
{
	struct hci_dev *hdev;
	int err;

	BT_DBG("");

	/* Initialize and register HCI device */
	hdev = hci_alloc_dev();
	if (!hdev) {
		BT_ERR("Can't allocate HCI device");
		return -ENOMEM;
	}

	hu->hdev = hdev;

	hdev->bus = HCI_UART;
	hci_set_drvdata(hdev, hu);

	/* Only when vendor specific setup callback is provided, consider
	 * the manufacturer information valid. This avoids filling in the
	 * value for Ericsson when nothing is specified.
	 */
	if (hu->proto->setup)
		hdev->manufacturer = hu->proto->manufacturer;

	hdev->open  = hci_uart_open;
	hdev->close = hci_uart_close;
	hdev->flush = hci_uart_flush;
	hdev->send  = hci_uart_send_frame;
	hdev->setup = hci_uart_setup;
	SET_HCIDEV_DEV(hdev, hu->tty->dev);

	if (test_bit(HCI_UART_RAW_DEVICE, &hu->hdev_flags))
		hci_set_quirk(hdev, HCI_QUIRK_RAW_DEVICE);

	if (test_bit(HCI_UART_EXT_CONFIG, &hu->hdev_flags))
		hci_set_quirk(hdev, HCI_QUIRK_EXTERNAL_CONFIG);

	if (!test_bit(HCI_UART_RESET_ON_INIT, &hu->hdev_flags))
		hci_set_quirk(hdev, HCI_QUIRK_RESET_ON_CLOSE);

	/* Only call open() for the protocol after hdev is fully initialized as
	 * open() (or a timer/workqueue it starts) may attempt to reference it.
	 */
	err = hu->proto->open(hu);
	if (err) {
		hu->hdev = NULL;
		hci_free_dev(hdev);
		return err;
	}

	if (test_bit(HCI_UART_INIT_PENDING, &hu->hdev_flags))
		return 0;

	if (hci_register_dev(hdev) < 0) {
		BT_ERR("Can't register HCI device");
		hu->proto->close(hu);
		hu->hdev = NULL;
		hci_free_dev(hdev);
		return -ENODEV;
	}

	set_bit(HCI_UART_REGISTERED, &hu->flags);

	return 0;
}

static int hci_uart_set_proto(struct hci_uart *hu, int id)
{
	const struct hci_uart_proto *p;
	int err;

	p = hci_uart_get_proto(id);
	if (!p)
		return -EPROTONOSUPPORT;

	hu->proto = p;

	set_bit(HCI_UART_PROTO_INIT, &hu->flags);

	err = hci_uart_register_dev(hu);
	if (err) {
		return err;
	}

	set_bit(HCI_UART_PROTO_READY, &hu->flags);
	clear_bit(HCI_UART_PROTO_INIT, &hu->flags);

	return 0;
}

static int hci_uart_set_flags(struct hci_uart *hu, unsigned long flags)
{
	unsigned long valid_flags = BIT(HCI_UART_RAW_DEVICE) |
				    BIT(HCI_UART_RESET_ON_INIT) |
				    BIT(HCI_UART_INIT_PENDING) |
				    BIT(HCI_UART_EXT_CONFIG) |
				    BIT(HCI_UART_VND_DETECT);

	if (flags & ~valid_flags)
		return -EINVAL;

	hu->hdev_flags = flags;

	return 0;
}

/* hci_uart_tty_ioctl()
 *
 *    Process IOCTL system call for the tty device.
 *
 * Arguments:
 *
 *    tty        pointer to tty instance data
 *    cmd        IOCTL command code
 *    arg        argument for IOCTL call (cmd dependent)
 *
 * Return Value:    Command dependent
 */
static int hci_uart_tty_ioctl(struct tty_struct *tty, unsigned int cmd,
			      unsigned long arg)
{
	struct hci_uart *hu = tty->disc_data;
	int err = 0;

	BT_DBG("");

	/* Verify the status of the device */
	if (!hu)
		return -EBADF;

	switch (cmd) {
	case HCIUARTSETPROTO:
		if (!test_and_set_bit(HCI_UART_PROTO_SET, &hu->flags)) {
			err = hci_uart_set_proto(hu, arg);
			if (err)
				clear_bit(HCI_UART_PROTO_SET, &hu->flags);
		} else
			err = -EBUSY;
		break;

	case HCIUARTGETPROTO:
		if (test_bit(HCI_UART_PROTO_SET, &hu->flags) &&
		    test_bit(HCI_UART_PROTO_READY, &hu->flags))
			err = hu->proto->id;
		else
			err = -EUNATCH;
		break;

	case HCIUARTGETDEVICE:
		if (test_bit(HCI_UART_REGISTERED, &hu->flags))
			err = hu->hdev->id;
		else
			err = -EUNATCH;
		break;

	case HCIUARTSETFLAGS:
		if (test_bit(HCI_UART_PROTO_SET, &hu->flags))
			err = -EBUSY;
		else
			err = hci_uart_set_flags(hu, arg);
		break;

	case HCIUARTGETFLAGS:
		err = hu->hdev_flags;
		break;

	default:
		err = n_tty_ioctl_helper(tty, cmd, arg);
		break;
	}

	return err;
}

/*
 * We don't provide read/write/poll interface for user space.
 */
static ssize_t hci_uart_tty_read(struct tty_struct *tty, struct file *file,
				 u8 *buf, size_t nr, void **cookie,
				 unsigned long offset)
{
	return 0;
}

static ssize_t hci_uart_tty_write(struct tty_struct *tty, struct file *file,
				  const u8 *data, size_t count)
{
	return 0;
}

static struct tty_ldisc_ops hci_uart_ldisc = {
	.owner		= THIS_MODULE,
	.num		= N_HCI,
	.name		= "n_hci",
	.open		= hci_uart_tty_open,
	.close		= hci_uart_tty_close,
	.read		= hci_uart_tty_read,
	.write		= hci_uart_tty_write,
	.ioctl		= hci_uart_tty_ioctl,
	.compat_ioctl	= hci_uart_tty_ioctl,
	.receive_buf	= hci_uart_tty_receive,
	.write_wakeup	= hci_uart_tty_wakeup,
};

static int __init hci_uart_init(void)
{
	int err;

	BT_INFO("HCI UART driver ver %s", VERSION);

	/* Register the tty discipline */
	err = tty_register_ldisc(&hci_uart_ldisc);
	if (err) {
		BT_ERR("HCI line discipline registration failed. (%d)", err);
		return err;
	}

#ifdef CONFIG_BT_HCIUART_H4
	h4_init();
#endif
#ifdef CONFIG_BT_HCIUART_BCSP
	bcsp_init();
#endif
#ifdef CONFIG_BT_HCIUART_LL
	ll_init();
#endif
#ifdef CONFIG_BT_HCIUART_ATH3K
	ath_init();
#endif
#ifdef CONFIG_BT_HCIUART_3WIRE
	h5_init();
#endif
#ifdef CONFIG_BT_HCIUART_INTEL
	intel_init();
#endif
#ifdef CONFIG_BT_HCIUART_BCM
	bcm_init();
#endif
#ifdef CONFIG_BT_HCIUART_QCA
	qca_init();
#endif
#ifdef CONFIG_BT_HCIUART_AG6XX
	ag6xx_init();
#endif
#ifdef CONFIG_BT_HCIUART_MRVL
	mrvl_init();
#endif
#ifdef CONFIG_BT_HCIUART_AML
	aml_init();
#endif
	return 0;
}

static void __exit hci_uart_exit(void)
{
#ifdef CONFIG_BT_HCIUART_H4
	h4_deinit();
#endif
#ifdef CONFIG_BT_HCIUART_BCSP
	bcsp_deinit();
#endif
#ifdef CONFIG_BT_HCIUART_LL
	ll_deinit();
#endif
#ifdef CONFIG_BT_HCIUART_ATH3K
	ath_deinit();
#endif
#ifdef CONFIG_BT_HCIUART_3WIRE
	h5_deinit();
#endif
#ifdef CONFIG_BT_HCIUART_INTEL
	intel_deinit();
#endif
#ifdef CONFIG_BT_HCIUART_BCM
	bcm_deinit();
#endif
#ifdef CONFIG_BT_HCIUART_QCA
	qca_deinit();
#endif
#ifdef CONFIG_BT_HCIUART_AG6XX
	ag6xx_deinit();
#endif
#ifdef CONFIG_BT_HCIUART_MRVL
	mrvl_deinit();
#endif
#ifdef CONFIG_BT_HCIUART_AML
	aml_deinit();
#endif
	tty_unregister_ldisc(&hci_uart_ldisc);
}

module_init(hci_uart_init);
module_exit(hci_uart_exit);

MODULE_AUTHOR("Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Bluetooth HCI UART driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_ALIAS_LDISC(N_HCI);

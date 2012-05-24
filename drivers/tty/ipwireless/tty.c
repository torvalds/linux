/*
 * IPWireless 3G PCMCIA Network Driver
 *
 * Original code
 *   by Stephen Blackheath <stephen@blacksapphire.com>,
 *      Ben Martel <benm@symmetric.co.nz>
 *
 * Copyrighted as follows:
 *   Copyright (C) 2004 by Symmetric Systems Ltd (NZ)
 *
 * Various driver changes and rewrites, port to new kernels
 *   Copyright (C) 2006-2007 Jiri Kosina
 *
 * Misc code cleanups and updates
 *   Copyright (C) 2007 David Sterba
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/ppp_defs.h>
#include <linux/if.h>
#include <linux/ppp-ioctl.h>
#include <linux/sched.h>
#include <linux/serial.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/uaccess.h>

#include "tty.h"
#include "network.h"
#include "hardware.h"
#include "main.h"

#define IPWIRELESS_PCMCIA_START 	(0)
#define IPWIRELESS_PCMCIA_MINORS	(24)
#define IPWIRELESS_PCMCIA_MINOR_RANGE	(8)

#define TTYTYPE_MODEM    (0)
#define TTYTYPE_MONITOR  (1)
#define TTYTYPE_RAS_RAW  (2)

struct ipw_tty {
	int index;
	struct ipw_hardware *hardware;
	unsigned int channel_idx;
	unsigned int secondary_channel_idx;
	int tty_type;
	struct ipw_network *network;
	struct tty_struct *linux_tty;
	int open_count;
	unsigned int control_lines;
	struct mutex ipw_tty_mutex;
	int tx_bytes_queued;
	int closing;
};

static struct ipw_tty *ttys[IPWIRELESS_PCMCIA_MINORS];

static struct tty_driver *ipw_tty_driver;

static char *tty_type_name(int tty_type)
{
	static char *channel_names[] = {
		"modem",
		"monitor",
		"RAS-raw"
	};

	return channel_names[tty_type];
}

static void report_registering(struct ipw_tty *tty)
{
	char *iftype = tty_type_name(tty->tty_type);

	printk(KERN_INFO IPWIRELESS_PCCARD_NAME
	       ": registering %s device ttyIPWp%d\n", iftype, tty->index);
}

static void report_deregistering(struct ipw_tty *tty)
{
	char *iftype = tty_type_name(tty->tty_type);

	printk(KERN_INFO IPWIRELESS_PCCARD_NAME
	       ": deregistering %s device ttyIPWp%d\n", iftype,
	       tty->index);
}

static struct ipw_tty *get_tty(int index)
{
	/*
	 * The 'ras_raw' channel is only available when 'loopback' mode
	 * is enabled.
	 * Number of minor starts with 16 (_RANGE * _RAS_RAW).
	 */
	if (!ipwireless_loopback && index >=
			 IPWIRELESS_PCMCIA_MINOR_RANGE * TTYTYPE_RAS_RAW)
		return NULL;

	return ttys[index];
}

static int ipw_open(struct tty_struct *linux_tty, struct file *filp)
{
	struct ipw_tty *tty = get_tty(linux_tty->index);

	if (!tty)
		return -ENODEV;

	mutex_lock(&tty->ipw_tty_mutex);

	if (tty->closing) {
		mutex_unlock(&tty->ipw_tty_mutex);
		return -ENODEV;
	}
	if (tty->open_count == 0)
		tty->tx_bytes_queued = 0;

	tty->open_count++;

	tty->linux_tty = linux_tty;
	linux_tty->driver_data = tty;
	linux_tty->low_latency = 1;

	if (tty->tty_type == TTYTYPE_MODEM)
		ipwireless_ppp_open(tty->network);

	mutex_unlock(&tty->ipw_tty_mutex);

	return 0;
}

static void do_ipw_close(struct ipw_tty *tty)
{
	tty->open_count--;

	if (tty->open_count == 0) {
		struct tty_struct *linux_tty = tty->linux_tty;

		if (linux_tty != NULL) {
			tty->linux_tty = NULL;
			linux_tty->driver_data = NULL;

			if (tty->tty_type == TTYTYPE_MODEM)
				ipwireless_ppp_close(tty->network);
		}
	}
}

static void ipw_hangup(struct tty_struct *linux_tty)
{
	struct ipw_tty *tty = linux_tty->driver_data;

	if (!tty)
		return;

	mutex_lock(&tty->ipw_tty_mutex);
	if (tty->open_count == 0) {
		mutex_unlock(&tty->ipw_tty_mutex);
		return;
	}

	do_ipw_close(tty);

	mutex_unlock(&tty->ipw_tty_mutex);
}

static void ipw_close(struct tty_struct *linux_tty, struct file *filp)
{
	ipw_hangup(linux_tty);
}

/* Take data received from hardware, and send it out the tty */
void ipwireless_tty_received(struct ipw_tty *tty, unsigned char *data,
			unsigned int length)
{
	struct tty_struct *linux_tty;
	int work = 0;

	mutex_lock(&tty->ipw_tty_mutex);
	linux_tty = tty->linux_tty;
	if (linux_tty == NULL) {
		mutex_unlock(&tty->ipw_tty_mutex);
		return;
	}

	if (!tty->open_count) {
		mutex_unlock(&tty->ipw_tty_mutex);
		return;
	}
	mutex_unlock(&tty->ipw_tty_mutex);

	work = tty_insert_flip_string(linux_tty, data, length);

	if (work != length)
		printk(KERN_DEBUG IPWIRELESS_PCCARD_NAME
				": %d chars not inserted to flip buffer!\n",
				length - work);

	/*
	 * This may sleep if ->low_latency is set
	 */
	if (work)
		tty_flip_buffer_push(linux_tty);
}

static void ipw_write_packet_sent_callback(void *callback_data,
					   unsigned int packet_length)
{
	struct ipw_tty *tty = callback_data;

	/*
	 * Packet has been sent, so we subtract the number of bytes from our
	 * tally of outstanding TX bytes.
	 */
	tty->tx_bytes_queued -= packet_length;
}

static int ipw_write(struct tty_struct *linux_tty,
		     const unsigned char *buf, int count)
{
	struct ipw_tty *tty = linux_tty->driver_data;
	int room, ret;

	if (!tty)
		return -ENODEV;

	mutex_lock(&tty->ipw_tty_mutex);
	if (!tty->open_count) {
		mutex_unlock(&tty->ipw_tty_mutex);
		return -EINVAL;
	}

	room = IPWIRELESS_TX_QUEUE_SIZE - tty->tx_bytes_queued;
	if (room < 0)
		room = 0;
	/* Don't allow caller to write any more than we have room for */
	if (count > room)
		count = room;

	if (count == 0) {
		mutex_unlock(&tty->ipw_tty_mutex);
		return 0;
	}

	ret = ipwireless_send_packet(tty->hardware, IPW_CHANNEL_RAS,
			       buf, count,
			       ipw_write_packet_sent_callback, tty);
	if (ret == -1) {
		mutex_unlock(&tty->ipw_tty_mutex);
		return 0;
	}

	tty->tx_bytes_queued += count;
	mutex_unlock(&tty->ipw_tty_mutex);

	return count;
}

static int ipw_write_room(struct tty_struct *linux_tty)
{
	struct ipw_tty *tty = linux_tty->driver_data;
	int room;

	/* FIXME: Exactly how is the tty object locked here .. */
	if (!tty)
		return -ENODEV;

	if (!tty->open_count)
		return -EINVAL;

	room = IPWIRELESS_TX_QUEUE_SIZE - tty->tx_bytes_queued;
	if (room < 0)
		room = 0;

	return room;
}

static int ipwireless_get_serial_info(struct ipw_tty *tty,
				      struct serial_struct __user *retinfo)
{
	struct serial_struct tmp;

	if (!retinfo)
		return (-EFAULT);

	memset(&tmp, 0, sizeof(tmp));
	tmp.type = PORT_UNKNOWN;
	tmp.line = tty->index;
	tmp.port = 0;
	tmp.irq = 0;
	tmp.flags = 0;
	tmp.baud_base = 115200;
	tmp.close_delay = 0;
	tmp.closing_wait = 0;
	tmp.custom_divisor = 0;
	tmp.hub6 = 0;
	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;

	return 0;
}

static int ipw_chars_in_buffer(struct tty_struct *linux_tty)
{
	struct ipw_tty *tty = linux_tty->driver_data;

	if (!tty)
		return 0;

	if (!tty->open_count)
		return 0;

	return tty->tx_bytes_queued;
}

static int get_control_lines(struct ipw_tty *tty)
{
	unsigned int my = tty->control_lines;
	unsigned int out = 0;

	if (my & IPW_CONTROL_LINE_RTS)
		out |= TIOCM_RTS;
	if (my & IPW_CONTROL_LINE_DTR)
		out |= TIOCM_DTR;
	if (my & IPW_CONTROL_LINE_CTS)
		out |= TIOCM_CTS;
	if (my & IPW_CONTROL_LINE_DSR)
		out |= TIOCM_DSR;
	if (my & IPW_CONTROL_LINE_DCD)
		out |= TIOCM_CD;

	return out;
}

static int set_control_lines(struct ipw_tty *tty, unsigned int set,
			     unsigned int clear)
{
	int ret;

	if (set & TIOCM_RTS) {
		ret = ipwireless_set_RTS(tty->hardware, tty->channel_idx, 1);
		if (ret)
			return ret;
		if (tty->secondary_channel_idx != -1) {
			ret = ipwireless_set_RTS(tty->hardware,
					  tty->secondary_channel_idx, 1);
			if (ret)
				return ret;
		}
	}
	if (set & TIOCM_DTR) {
		ret = ipwireless_set_DTR(tty->hardware, tty->channel_idx, 1);
		if (ret)
			return ret;
		if (tty->secondary_channel_idx != -1) {
			ret = ipwireless_set_DTR(tty->hardware,
					  tty->secondary_channel_idx, 1);
			if (ret)
				return ret;
		}
	}
	if (clear & TIOCM_RTS) {
		ret = ipwireless_set_RTS(tty->hardware, tty->channel_idx, 0);
		if (tty->secondary_channel_idx != -1) {
			ret = ipwireless_set_RTS(tty->hardware,
					  tty->secondary_channel_idx, 0);
			if (ret)
				return ret;
		}
	}
	if (clear & TIOCM_DTR) {
		ret = ipwireless_set_DTR(tty->hardware, tty->channel_idx, 0);
		if (tty->secondary_channel_idx != -1) {
			ret = ipwireless_set_DTR(tty->hardware,
					  tty->secondary_channel_idx, 0);
			if (ret)
				return ret;
		}
	}
	return 0;
}

static int ipw_tiocmget(struct tty_struct *linux_tty)
{
	struct ipw_tty *tty = linux_tty->driver_data;
	/* FIXME: Exactly how is the tty object locked here .. */

	if (!tty)
		return -ENODEV;

	if (!tty->open_count)
		return -EINVAL;

	return get_control_lines(tty);
}

static int
ipw_tiocmset(struct tty_struct *linux_tty,
	     unsigned int set, unsigned int clear)
{
	struct ipw_tty *tty = linux_tty->driver_data;
	/* FIXME: Exactly how is the tty object locked here .. */

	if (!tty)
		return -ENODEV;

	if (!tty->open_count)
		return -EINVAL;

	return set_control_lines(tty, set, clear);
}

static int ipw_ioctl(struct tty_struct *linux_tty,
		     unsigned int cmd, unsigned long arg)
{
	struct ipw_tty *tty = linux_tty->driver_data;

	if (!tty)
		return -ENODEV;

	if (!tty->open_count)
		return -EINVAL;

	/* FIXME: Exactly how is the tty object locked here .. */

	switch (cmd) {
	case TIOCGSERIAL:
		return ipwireless_get_serial_info(tty, (void __user *) arg);

	case TIOCSSERIAL:
		return 0;	/* Keeps the PCMCIA scripts happy. */
	}

	if (tty->tty_type == TTYTYPE_MODEM) {
		switch (cmd) {
		case PPPIOCGCHAN:
			{
				int chan = ipwireless_ppp_channel_index(
							tty->network);

				if (chan < 0)
					return -ENODEV;
				if (put_user(chan, (int __user *) arg))
					return -EFAULT;
			}
			return 0;

		case PPPIOCGUNIT:
			{
				int unit = ipwireless_ppp_unit_number(
						tty->network);

				if (unit < 0)
					return -ENODEV;
				if (put_user(unit, (int __user *) arg))
					return -EFAULT;
			}
			return 0;

		case FIONREAD:
			{
				int val = 0;

				if (put_user(val, (int __user *) arg))
					return -EFAULT;
			}
			return 0;
		case TCFLSH:
			return tty_perform_flush(linux_tty, arg);
		}
	}
	return -ENOIOCTLCMD;
}

static int add_tty(int j,
		    struct ipw_hardware *hardware,
		    struct ipw_network *network, int channel_idx,
		    int secondary_channel_idx, int tty_type)
{
	ttys[j] = kzalloc(sizeof(struct ipw_tty), GFP_KERNEL);
	if (!ttys[j])
		return -ENOMEM;
	ttys[j]->index = j;
	ttys[j]->hardware = hardware;
	ttys[j]->channel_idx = channel_idx;
	ttys[j]->secondary_channel_idx = secondary_channel_idx;
	ttys[j]->network = network;
	ttys[j]->tty_type = tty_type;
	mutex_init(&ttys[j]->ipw_tty_mutex);

	tty_register_device(ipw_tty_driver, j, NULL);
	ipwireless_associate_network_tty(network, channel_idx, ttys[j]);

	if (secondary_channel_idx != -1)
		ipwireless_associate_network_tty(network,
						 secondary_channel_idx,
						 ttys[j]);
	if (get_tty(j) == ttys[j])
		report_registering(ttys[j]);
	return 0;
}

struct ipw_tty *ipwireless_tty_create(struct ipw_hardware *hardware,
				      struct ipw_network *network)
{
	int i, j;

	for (i = 0; i < IPWIRELESS_PCMCIA_MINOR_RANGE; i++) {
		int allfree = 1;

		for (j = i; j < IPWIRELESS_PCMCIA_MINORS;
				j += IPWIRELESS_PCMCIA_MINOR_RANGE)
			if (ttys[j] != NULL) {
				allfree = 0;
				break;
			}

		if (allfree) {
			j = i;

			if (add_tty(j, hardware, network,
					IPW_CHANNEL_DIALLER, IPW_CHANNEL_RAS,
					TTYTYPE_MODEM))
				return NULL;

			j += IPWIRELESS_PCMCIA_MINOR_RANGE;
			if (add_tty(j, hardware, network,
					IPW_CHANNEL_DIALLER, -1,
					TTYTYPE_MONITOR))
				return NULL;

			j += IPWIRELESS_PCMCIA_MINOR_RANGE;
			if (add_tty(j, hardware, network,
					IPW_CHANNEL_RAS, -1,
					TTYTYPE_RAS_RAW))
				return NULL;

			return ttys[i];
		}
	}
	return NULL;
}

/*
 * Must be called before ipwireless_network_free().
 */
void ipwireless_tty_free(struct ipw_tty *tty)
{
	int j;
	struct ipw_network *network = ttys[tty->index]->network;

	for (j = tty->index; j < IPWIRELESS_PCMCIA_MINORS;
			j += IPWIRELESS_PCMCIA_MINOR_RANGE) {
		struct ipw_tty *ttyj = ttys[j];

		if (ttyj) {
			mutex_lock(&ttyj->ipw_tty_mutex);
			if (get_tty(j) == ttyj)
				report_deregistering(ttyj);
			ttyj->closing = 1;
			if (ttyj->linux_tty != NULL) {
				mutex_unlock(&ttyj->ipw_tty_mutex);
				tty_hangup(ttyj->linux_tty);
				/* Wait till the tty_hangup has completed */
				flush_work_sync(&ttyj->linux_tty->hangup_work);
				/* FIXME: Exactly how is the tty object locked here
				   against a parallel ioctl etc */
				mutex_lock(&ttyj->ipw_tty_mutex);
			}
			while (ttyj->open_count)
				do_ipw_close(ttyj);
			ipwireless_disassociate_network_ttys(network,
							     ttyj->channel_idx);
			tty_unregister_device(ipw_tty_driver, j);
			ttys[j] = NULL;
			mutex_unlock(&ttyj->ipw_tty_mutex);
			kfree(ttyj);
		}
	}
}

static const struct tty_operations tty_ops = {
	.open = ipw_open,
	.close = ipw_close,
	.hangup = ipw_hangup,
	.write = ipw_write,
	.write_room = ipw_write_room,
	.ioctl = ipw_ioctl,
	.chars_in_buffer = ipw_chars_in_buffer,
	.tiocmget = ipw_tiocmget,
	.tiocmset = ipw_tiocmset,
};

int ipwireless_tty_init(void)
{
	int result;

	ipw_tty_driver = alloc_tty_driver(IPWIRELESS_PCMCIA_MINORS);
	if (!ipw_tty_driver)
		return -ENOMEM;

	ipw_tty_driver->driver_name = IPWIRELESS_PCCARD_NAME;
	ipw_tty_driver->name = "ttyIPWp";
	ipw_tty_driver->major = 0;
	ipw_tty_driver->minor_start = IPWIRELESS_PCMCIA_START;
	ipw_tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
	ipw_tty_driver->subtype = SERIAL_TYPE_NORMAL;
	ipw_tty_driver->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV;
	ipw_tty_driver->init_termios = tty_std_termios;
	ipw_tty_driver->init_termios.c_cflag =
	    B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	ipw_tty_driver->init_termios.c_ispeed = 9600;
	ipw_tty_driver->init_termios.c_ospeed = 9600;
	tty_set_operations(ipw_tty_driver, &tty_ops);
	result = tty_register_driver(ipw_tty_driver);
	if (result) {
		printk(KERN_ERR IPWIRELESS_PCCARD_NAME
		       ": failed to register tty driver\n");
		put_tty_driver(ipw_tty_driver);
		return result;
	}

	return 0;
}

void ipwireless_tty_release(void)
{
	int ret;

	ret = tty_unregister_driver(ipw_tty_driver);
	put_tty_driver(ipw_tty_driver);
	if (ret != 0)
		printk(KERN_ERR IPWIRELESS_PCCARD_NAME
			": tty_unregister_driver failed with code %d\n", ret);
}

int ipwireless_tty_is_modem(struct ipw_tty *tty)
{
	return tty->tty_type == TTYTYPE_MODEM;
}

void
ipwireless_tty_notify_control_line_change(struct ipw_tty *tty,
					  unsigned int channel_idx,
					  unsigned int control_lines,
					  unsigned int changed_mask)
{
	unsigned int old_control_lines = tty->control_lines;

	tty->control_lines = (tty->control_lines & ~changed_mask)
		| (control_lines & changed_mask);

	/*
	 * If DCD is de-asserted, we close the tty so pppd can tell that we
	 * have gone offline.
	 */
	if ((old_control_lines & IPW_CONTROL_LINE_DCD)
			&& !(tty->control_lines & IPW_CONTROL_LINE_DCD)
			&& tty->linux_tty) {
		tty_hangup(tty->linux_tty);
	}
}


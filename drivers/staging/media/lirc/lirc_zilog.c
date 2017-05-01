/*
 * i2c IR lirc driver for devices with zilog IR processors
 *
 * Copyright (c) 2000 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 * modified for PixelView (BT878P+W/FM) by
 *      Michal Kochanowicz <mkochano@pld.org.pl>
 *      Christoph Bartelmus <lirc@bartelmus.de>
 * modified for KNC ONE TV Station/Anubis Typhoon TView Tuner by
 *      Ulrich Mueller <ulrich.mueller42@web.de>
 * modified for Asus TV-Box and Creative/VisionTek BreakOut-Box by
 *      Stefan Jahn <stefan@lkcc.org>
 * modified for inclusion into kernel sources by
 *      Jerome Brock <jbrock@users.sourceforge.net>
 * modified for Leadtek Winfast PVR2000 by
 *      Thomas Reitmayr (treitmayr@yahoo.com)
 * modified for Hauppauge PVR-150 IR TX device by
 *      Mark Weaver <mark@npsl.co.uk>
 * changed name from lirc_pvr150 to lirc_zilog, works on more than pvr-150
 *	Jarod Wilson <jarod@redhat.com>
 *
 * parts are cut&pasted from the lirc_i2c.c driver
 *
 * Numerous changes updating lirc_zilog.c in kernel 2.6.38 and later are
 * Copyright (C) 2011 Andy Walls <awalls@md.metrocast.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/kernel.h>
#include <linux/sched/signal.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/firmware.h>
#include <linux/vmalloc.h>

#include <linux/mutex.h>
#include <linux/kthread.h>

#include <media/lirc_dev.h>
#include <media/lirc.h>

/* Max transfer size done by I2C transfer functions */
#define MAX_XFER_SIZE  64

struct IR;

struct IR_rx {
	struct kref ref;
	struct IR *ir;

	/* RX device */
	struct mutex client_lock;
	struct i2c_client *c;

	/* RX polling thread data */
	struct task_struct *task;

	/* RX read data */
	unsigned char b[3];
	bool hdpvr_data_fmt;
};

struct IR_tx {
	struct kref ref;
	struct IR *ir;

	/* TX device */
	struct mutex client_lock;
	struct i2c_client *c;

	/* TX additional actions needed */
	int need_boot;
	bool post_tx_ready_poll;
};

struct IR {
	struct kref ref;
	struct list_head list;

	/* FIXME spinlock access to l.features */
	struct lirc_driver l;
	struct lirc_buffer rbuf;

	struct mutex ir_lock;
	atomic_t open_count;

	struct i2c_adapter *adapter;

	spinlock_t rx_ref_lock; /* struct IR_rx kref get()/put() */
	struct IR_rx *rx;

	spinlock_t tx_ref_lock; /* struct IR_tx kref get()/put() */
	struct IR_tx *tx;
};

/* IR transceiver instance object list */
/*
 * This lock is used for the following:
 * a. ir_devices_list access, insertions, deletions
 * b. struct IR kref get()s and put()s
 * c. serialization of ir_probe() for the two i2c_clients for a Z8
 */
static DEFINE_MUTEX(ir_devices_lock);
static LIST_HEAD(ir_devices_list);

/* Block size for IR transmitter */
#define TX_BLOCK_SIZE	99

/* Hauppauge IR transmitter data */
struct tx_data_struct {
	/* Boot block */
	unsigned char *boot_data;

	/* Start of binary data block */
	unsigned char *datap;

	/* End of binary data block */
	unsigned char *endp;

	/* Number of installed codesets */
	unsigned int num_code_sets;

	/* Pointers to codesets */
	unsigned char **code_sets;

	/* Global fixed data template */
	int fixed[TX_BLOCK_SIZE];
};

static struct tx_data_struct *tx_data;
static struct mutex tx_data_lock;


/* module parameters */
static bool debug;	/* debug output */
static bool tx_only;	/* only handle the IR Tx function */


/* struct IR reference counting */
static struct IR *get_ir_device(struct IR *ir, bool ir_devices_lock_held)
{
	if (ir_devices_lock_held) {
		kref_get(&ir->ref);
	} else {
		mutex_lock(&ir_devices_lock);
		kref_get(&ir->ref);
		mutex_unlock(&ir_devices_lock);
	}
	return ir;
}

static void release_ir_device(struct kref *ref)
{
	struct IR *ir = container_of(ref, struct IR, ref);

	/*
	 * Things should be in this state by now:
	 * ir->rx set to NULL and deallocated - happens before ir->rx->ir put()
	 * ir->rx->task kthread stopped - happens before ir->rx->ir put()
	 * ir->tx set to NULL and deallocated - happens before ir->tx->ir put()
	 * ir->open_count ==  0 - happens on final close()
	 * ir_lock, tx_ref_lock, rx_ref_lock, all released
	 */
	if (ir->l.minor >= 0) {
		lirc_unregister_driver(ir->l.minor);
		ir->l.minor = -1;
	}

	if (kfifo_initialized(&ir->rbuf.fifo))
		lirc_buffer_free(&ir->rbuf);
	list_del(&ir->list);
	kfree(ir);
}

static int put_ir_device(struct IR *ir, bool ir_devices_lock_held)
{
	int released;

	if (ir_devices_lock_held)
		return kref_put(&ir->ref, release_ir_device);

	mutex_lock(&ir_devices_lock);
	released = kref_put(&ir->ref, release_ir_device);
	mutex_unlock(&ir_devices_lock);

	return released;
}

/* struct IR_rx reference counting */
static struct IR_rx *get_ir_rx(struct IR *ir)
{
	struct IR_rx *rx;

	spin_lock(&ir->rx_ref_lock);
	rx = ir->rx;
	if (rx != NULL)
		kref_get(&rx->ref);
	spin_unlock(&ir->rx_ref_lock);
	return rx;
}

static void destroy_rx_kthread(struct IR_rx *rx, bool ir_devices_lock_held)
{
	/* end up polling thread */
	if (!IS_ERR_OR_NULL(rx->task)) {
		kthread_stop(rx->task);
		rx->task = NULL;
		/* Put the ir ptr that ir_probe() gave to the rx poll thread */
		put_ir_device(rx->ir, ir_devices_lock_held);
	}
}

static void release_ir_rx(struct kref *ref)
{
	struct IR_rx *rx = container_of(ref, struct IR_rx, ref);
	struct IR *ir = rx->ir;

	/*
	 * This release function can't do all the work, as we want
	 * to keep the rx_ref_lock a spinlock, and killing the poll thread
	 * and releasing the ir reference can cause a sleep.  That work is
	 * performed by put_ir_rx()
	 */
	ir->l.features &= ~LIRC_CAN_REC_LIRCCODE;
	/* Don't put_ir_device(rx->ir) here; lock can't be freed yet */
	ir->rx = NULL;
	/* Don't do the kfree(rx) here; we still need to kill the poll thread */
}

static int put_ir_rx(struct IR_rx *rx, bool ir_devices_lock_held)
{
	int released;
	struct IR *ir = rx->ir;

	spin_lock(&ir->rx_ref_lock);
	released = kref_put(&rx->ref, release_ir_rx);
	spin_unlock(&ir->rx_ref_lock);
	/* Destroy the rx kthread while not holding the spinlock */
	if (released) {
		destroy_rx_kthread(rx, ir_devices_lock_held);
		kfree(rx);
		/* Make sure we're not still in a poll_table somewhere */
		wake_up_interruptible(&ir->rbuf.wait_poll);
	}
	/* Do a reference put() for the rx->ir reference, if we released rx */
	if (released)
		put_ir_device(ir, ir_devices_lock_held);
	return released;
}

/* struct IR_tx reference counting */
static struct IR_tx *get_ir_tx(struct IR *ir)
{
	struct IR_tx *tx;

	spin_lock(&ir->tx_ref_lock);
	tx = ir->tx;
	if (tx != NULL)
		kref_get(&tx->ref);
	spin_unlock(&ir->tx_ref_lock);
	return tx;
}

static void release_ir_tx(struct kref *ref)
{
	struct IR_tx *tx = container_of(ref, struct IR_tx, ref);
	struct IR *ir = tx->ir;

	ir->l.features &= ~LIRC_CAN_SEND_PULSE;
	/* Don't put_ir_device(tx->ir) here, so our lock doesn't get freed */
	ir->tx = NULL;
	kfree(tx);
}

static int put_ir_tx(struct IR_tx *tx, bool ir_devices_lock_held)
{
	int released;
	struct IR *ir = tx->ir;

	spin_lock(&ir->tx_ref_lock);
	released = kref_put(&tx->ref, release_ir_tx);
	spin_unlock(&ir->tx_ref_lock);
	/* Do a reference put() for the tx->ir reference, if we released tx */
	if (released)
		put_ir_device(ir, ir_devices_lock_held);
	return released;
}

static int add_to_buf(struct IR *ir)
{
	__u16 code;
	unsigned char codes[2];
	unsigned char keybuf[6];
	int got_data = 0;
	int ret;
	int failures = 0;
	unsigned char sendbuf[1] = { 0 };
	struct lirc_buffer *rbuf = ir->l.rbuf;
	struct IR_rx *rx;
	struct IR_tx *tx;

	if (lirc_buffer_full(rbuf)) {
		dev_dbg(ir->l.dev, "buffer overflow\n");
		return -EOVERFLOW;
	}

	rx = get_ir_rx(ir);
	if (rx == NULL)
		return -ENXIO;

	/* Ensure our rx->c i2c_client remains valid for the duration */
	mutex_lock(&rx->client_lock);
	if (rx->c == NULL) {
		mutex_unlock(&rx->client_lock);
		put_ir_rx(rx, false);
		return -ENXIO;
	}

	tx = get_ir_tx(ir);

	/*
	 * service the device as long as it is returning
	 * data and we have space
	 */
	do {
		if (kthread_should_stop()) {
			ret = -ENODATA;
			break;
		}

		/*
		 * Lock i2c bus for the duration.  RX/TX chips interfere so
		 * this is worth it
		 */
		mutex_lock(&ir->ir_lock);

		if (kthread_should_stop()) {
			mutex_unlock(&ir->ir_lock);
			ret = -ENODATA;
			break;
		}

		/*
		 * Send random "poll command" (?)  Windows driver does this
		 * and it is a good point to detect chip failure.
		 */
		ret = i2c_master_send(rx->c, sendbuf, 1);
		if (ret != 1) {
			dev_err(ir->l.dev, "i2c_master_send failed with %d\n",
				ret);
			if (failures >= 3) {
				mutex_unlock(&ir->ir_lock);
				dev_err(ir->l.dev,
					"unable to read from the IR chip after 3 resets, giving up\n");
				break;
			}

			/* Looks like the chip crashed, reset it */
			dev_err(ir->l.dev,
				"polling the IR receiver chip failed, trying reset\n");

			set_current_state(TASK_UNINTERRUPTIBLE);
			if (kthread_should_stop()) {
				mutex_unlock(&ir->ir_lock);
				ret = -ENODATA;
				break;
			}
			schedule_timeout((100 * HZ + 999) / 1000);
			if (tx != NULL)
				tx->need_boot = 1;

			++failures;
			mutex_unlock(&ir->ir_lock);
			ret = 0;
			continue;
		}

		if (kthread_should_stop()) {
			mutex_unlock(&ir->ir_lock);
			ret = -ENODATA;
			break;
		}
		ret = i2c_master_recv(rx->c, keybuf, sizeof(keybuf));
		mutex_unlock(&ir->ir_lock);
		if (ret != sizeof(keybuf)) {
			dev_err(ir->l.dev,
				"i2c_master_recv failed with %d -- keeping last read buffer\n",
				ret);
		} else {
			rx->b[0] = keybuf[3];
			rx->b[1] = keybuf[4];
			rx->b[2] = keybuf[5];
			dev_dbg(ir->l.dev,
				"key (0x%02x/0x%02x)\n",
				rx->b[0], rx->b[1]);
		}

		/* key pressed ? */
		if (rx->hdpvr_data_fmt) {
			if (got_data && (keybuf[0] == 0x80)) {
				ret = 0;
				break;
			} else if (got_data && (keybuf[0] == 0x00)) {
				ret = -ENODATA;
				break;
			}
		} else if ((rx->b[0] & 0x80) == 0) {
			ret = got_data ? 0 : -ENODATA;
			break;
		}

		/* look what we have */
		code = (((__u16)rx->b[0] & 0x7f) << 6) | (rx->b[1] >> 2);

		codes[0] = (code >> 8) & 0xff;
		codes[1] = code & 0xff;

		/* return it */
		lirc_buffer_write(rbuf, codes);
		++got_data;
		ret = 0;
	} while (!lirc_buffer_full(rbuf));

	mutex_unlock(&rx->client_lock);
	if (tx != NULL)
		put_ir_tx(tx, false);
	put_ir_rx(rx, false);
	return ret;
}

/*
 * Main function of the polling thread -- from lirc_dev.
 * We don't fit the LIRC model at all anymore.  This is horrible, but
 * basically we have a single RX/TX device with a nasty failure mode
 * that needs to be accounted for across the pair.  lirc lets us provide
 * fops, but prevents us from using the internal polling, etc. if we do
 * so.  Hence the replication.  Might be neater to extend the LIRC model
 * to account for this but I'd think it's a very special case of seriously
 * messed up hardware.
 */
static int lirc_thread(void *arg)
{
	struct IR *ir = arg;
	struct lirc_buffer *rbuf = ir->l.rbuf;

	dev_dbg(ir->l.dev, "poll thread started\n");

	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);

		/* if device not opened, we can sleep half a second */
		if (atomic_read(&ir->open_count) == 0) {
			schedule_timeout(HZ/2);
			continue;
		}

		/*
		 * This is ~113*2 + 24 + jitter (2*repeat gap + code length).
		 * We use this interval as the chip resets every time you poll
		 * it (bad!).  This is therefore just sufficient to catch all
		 * of the button presses.  It makes the remote much more
		 * responsive.  You can see the difference by running irw and
		 * holding down a button.  With 100ms, the old polling
		 * interval, you'll notice breaks in the repeat sequence
		 * corresponding to lost keypresses.
		 */
		schedule_timeout((260 * HZ) / 1000);
		if (kthread_should_stop())
			break;
		if (!add_to_buf(ir))
			wake_up_interruptible(&rbuf->wait_poll);
	}

	dev_dbg(ir->l.dev, "poll thread ended\n");
	return 0;
}

/* safe read of a uint32 (always network byte order) */
static int read_uint32(unsigned char **data,
				     unsigned char *endp, unsigned int *val)
{
	if (*data + 4 > endp)
		return 0;
	*val = ((*data)[0] << 24) | ((*data)[1] << 16) |
	       ((*data)[2] << 8) | (*data)[3];
	*data += 4;
	return 1;
}

/* safe read of a uint8 */
static int read_uint8(unsigned char **data,
				    unsigned char *endp, unsigned char *val)
{
	if (*data + 1 > endp)
		return 0;
	*val = *((*data)++);
	return 1;
}

/* safe skipping of N bytes */
static int skip(unsigned char **data,
			      unsigned char *endp, unsigned int distance)
{
	if (*data + distance > endp)
		return 0;
	*data += distance;
	return 1;
}

/* decompress key data into the given buffer */
static int get_key_data(unsigned char *buf,
			     unsigned int codeset, unsigned int key)
{
	unsigned char *data, *endp, *diffs, *key_block;
	unsigned char keys, ndiffs, id;
	unsigned int base, lim, pos, i;

	/* Binary search for the codeset */
	for (base = 0, lim = tx_data->num_code_sets; lim; lim >>= 1) {
		pos = base + (lim >> 1);
		data = tx_data->code_sets[pos];

		if (!read_uint32(&data, tx_data->endp, &i))
			goto corrupt;

		if (i == codeset)
			break;
		else if (codeset > i) {
			base = pos + 1;
			--lim;
		}
	}
	/* Not found? */
	if (!lim)
		return -EPROTO;

	/* Set end of data block */
	endp = pos < tx_data->num_code_sets - 1 ?
		tx_data->code_sets[pos + 1] : tx_data->endp;

	/* Read the block header */
	if (!read_uint8(&data, endp, &keys) ||
	    !read_uint8(&data, endp, &ndiffs) ||
	    ndiffs > TX_BLOCK_SIZE || keys == 0)
		goto corrupt;

	/* Save diffs & skip */
	diffs = data;
	if (!skip(&data, endp, ndiffs))
		goto corrupt;

	/* Read the id of the first key */
	if (!read_uint8(&data, endp, &id))
		goto corrupt;

	/* Unpack the first key's data */
	for (i = 0; i < TX_BLOCK_SIZE; ++i) {
		if (tx_data->fixed[i] == -1) {
			if (!read_uint8(&data, endp, &buf[i]))
				goto corrupt;
		} else {
			buf[i] = (unsigned char)tx_data->fixed[i];
		}
	}

	/* Early out key found/not found */
	if (key == id)
		return 0;
	if (keys == 1)
		return -EPROTO;

	/* Sanity check */
	key_block = data;
	if (!skip(&data, endp, (keys - 1) * (ndiffs + 1)))
		goto corrupt;

	/* Binary search for the key */
	for (base = 0, lim = keys - 1; lim; lim >>= 1) {
		/* Seek to block */
		unsigned char *key_data;

		pos = base + (lim >> 1);
		key_data = key_block + (ndiffs + 1) * pos;

		if (*key_data == key) {
			/* skip key id */
			++key_data;

			/* found, so unpack the diffs */
			for (i = 0; i < ndiffs; ++i) {
				unsigned char val;

				if (!read_uint8(&key_data, endp, &val) ||
				    diffs[i] >= TX_BLOCK_SIZE)
					goto corrupt;
				buf[diffs[i]] = val;
			}

			return 0;
		} else if (key > *key_data) {
			base = pos + 1;
			--lim;
		}
	}
	/* Key not found */
	return -EPROTO;

corrupt:
	pr_err("firmware is corrupt\n");
	return -EFAULT;
}

/* send a block of data to the IR TX device */
static int send_data_block(struct IR_tx *tx, unsigned char *data_block)
{
	int i, j, ret;
	unsigned char buf[5];

	for (i = 0; i < TX_BLOCK_SIZE;) {
		int tosend = TX_BLOCK_SIZE - i;

		if (tosend > 4)
			tosend = 4;
		buf[0] = (unsigned char)(i + 1);
		for (j = 0; j < tosend; ++j)
			buf[1 + j] = data_block[i + j];
		dev_dbg(tx->ir->l.dev, "%*ph", 5, buf);
		ret = i2c_master_send(tx->c, buf, tosend + 1);
		if (ret != tosend + 1) {
			dev_err(tx->ir->l.dev,
				"i2c_master_send failed with %d\n", ret);
			return ret < 0 ? ret : -EFAULT;
		}
		i += tosend;
	}
	return 0;
}

/* send boot data to the IR TX device */
static int send_boot_data(struct IR_tx *tx)
{
	int ret, i;
	unsigned char buf[4];

	/* send the boot block */
	ret = send_data_block(tx, tx_data->boot_data);
	if (ret != 0)
		return ret;

	/* Hit the go button to activate the new boot data */
	buf[0] = 0x00;
	buf[1] = 0x20;
	ret = i2c_master_send(tx->c, buf, 2);
	if (ret != 2) {
		dev_err(tx->ir->l.dev, "i2c_master_send failed with %d\n", ret);
		return ret < 0 ? ret : -EFAULT;
	}

	/*
	 * Wait for zilog to settle after hitting go post boot block upload.
	 * Without this delay, the HD-PVR and HVR-1950 both return an -EIO
	 * upon attempting to get firmware revision, and tx probe thus fails.
	 */
	for (i = 0; i < 10; i++) {
		ret = i2c_master_send(tx->c, buf, 1);
		if (ret == 1)
			break;
		udelay(100);
	}

	if (ret != 1) {
		dev_err(tx->ir->l.dev, "i2c_master_send failed with %d\n", ret);
		return ret < 0 ? ret : -EFAULT;
	}

	/* Here comes the firmware version... (hopefully) */
	ret = i2c_master_recv(tx->c, buf, 4);
	if (ret != 4) {
		dev_err(tx->ir->l.dev, "i2c_master_recv failed with %d\n", ret);
		return 0;
	}
	if ((buf[0] != 0x80) && (buf[0] != 0xa0)) {
		dev_err(tx->ir->l.dev, "unexpected IR TX init response: %02x\n",
			buf[0]);
		return 0;
	}
	dev_notice(tx->ir->l.dev,
		   "Zilog/Hauppauge IR blaster firmware version %d.%d.%d loaded\n",
		   buf[1], buf[2], buf[3]);

	return 0;
}

/* unload "firmware", lock held */
static void fw_unload_locked(void)
{
	if (tx_data) {
		vfree(tx_data->code_sets);

		vfree(tx_data->datap);

		vfree(tx_data);
		tx_data = NULL;
		pr_debug("successfully unloaded IR blaster firmware\n");
	}
}

/* unload "firmware" for the IR TX device */
static void fw_unload(void)
{
	mutex_lock(&tx_data_lock);
	fw_unload_locked();
	mutex_unlock(&tx_data_lock);
}

/* load "firmware" for the IR TX device */
static int fw_load(struct IR_tx *tx)
{
	int ret;
	unsigned int i;
	unsigned char *data, version, num_global_fixed;
	const struct firmware *fw_entry;

	/* Already loaded? */
	mutex_lock(&tx_data_lock);
	if (tx_data) {
		ret = 0;
		goto out;
	}

	/* Request codeset data file */
	ret = request_firmware(&fw_entry, "haup-ir-blaster.bin", tx->ir->l.dev);
	if (ret != 0) {
		dev_err(tx->ir->l.dev,
			"firmware haup-ir-blaster.bin not available (%d)\n",
			ret);
		ret = ret < 0 ? ret : -EFAULT;
		goto out;
	}
	dev_dbg(tx->ir->l.dev, "firmware of size %zu loaded\n", fw_entry->size);

	/* Parse the file */
	tx_data = vmalloc(sizeof(*tx_data));
	if (tx_data == NULL) {
		release_firmware(fw_entry);
		ret = -ENOMEM;
		goto out;
	}
	tx_data->code_sets = NULL;

	/* Copy the data so hotplug doesn't get confused and timeout */
	tx_data->datap = vmalloc(fw_entry->size);
	if (tx_data->datap == NULL) {
		release_firmware(fw_entry);
		vfree(tx_data);
		ret = -ENOMEM;
		goto out;
	}
	memcpy(tx_data->datap, fw_entry->data, fw_entry->size);
	tx_data->endp = tx_data->datap + fw_entry->size;
	release_firmware(fw_entry); fw_entry = NULL;

	/* Check version */
	data = tx_data->datap;
	if (!read_uint8(&data, tx_data->endp, &version))
		goto corrupt;
	if (version != 1) {
		dev_err(tx->ir->l.dev,
			"unsupported code set file version (%u, expected 1) -- please upgrade to a newer driver\n",
			version);
		fw_unload_locked();
		ret = -EFAULT;
		goto out;
	}

	/* Save boot block for later */
	tx_data->boot_data = data;
	if (!skip(&data, tx_data->endp, TX_BLOCK_SIZE))
		goto corrupt;

	if (!read_uint32(&data, tx_data->endp,
			      &tx_data->num_code_sets))
		goto corrupt;

	dev_dbg(tx->ir->l.dev, "%u IR blaster codesets loaded\n",
		tx_data->num_code_sets);

	tx_data->code_sets = vmalloc(
		tx_data->num_code_sets * sizeof(char *));
	if (tx_data->code_sets == NULL) {
		fw_unload_locked();
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < TX_BLOCK_SIZE; ++i)
		tx_data->fixed[i] = -1;

	/* Read global fixed data template */
	if (!read_uint8(&data, tx_data->endp, &num_global_fixed) ||
	    num_global_fixed > TX_BLOCK_SIZE)
		goto corrupt;
	for (i = 0; i < num_global_fixed; ++i) {
		unsigned char pos, val;

		if (!read_uint8(&data, tx_data->endp, &pos) ||
		    !read_uint8(&data, tx_data->endp, &val) ||
		    pos >= TX_BLOCK_SIZE)
			goto corrupt;
		tx_data->fixed[pos] = (int)val;
	}

	/* Filch out the position of each code set */
	for (i = 0; i < tx_data->num_code_sets; ++i) {
		unsigned int id;
		unsigned char keys;
		unsigned char ndiffs;

		/* Save the codeset position */
		tx_data->code_sets[i] = data;

		/* Read header */
		if (!read_uint32(&data, tx_data->endp, &id) ||
		    !read_uint8(&data, tx_data->endp, &keys) ||
		    !read_uint8(&data, tx_data->endp, &ndiffs) ||
		    ndiffs > TX_BLOCK_SIZE || keys == 0)
			goto corrupt;

		/* skip diff positions */
		if (!skip(&data, tx_data->endp, ndiffs))
			goto corrupt;

		/*
		 * After the diffs we have the first key id + data -
		 * global fixed
		 */
		if (!skip(&data, tx_data->endp,
			       1 + TX_BLOCK_SIZE - num_global_fixed))
			goto corrupt;

		/* Then we have keys-1 blocks of key id+diffs */
		if (!skip(&data, tx_data->endp,
			       (ndiffs + 1) * (keys - 1)))
			goto corrupt;
	}
	ret = 0;
	goto out;

corrupt:
	dev_err(tx->ir->l.dev, "firmware is corrupt\n");
	fw_unload_locked();
	ret = -EFAULT;

out:
	mutex_unlock(&tx_data_lock);
	return ret;
}

/* copied from lirc_dev */
static ssize_t read(struct file *filep, char __user *outbuf, size_t n,
		    loff_t *ppos)
{
	struct IR *ir = filep->private_data;
	struct IR_rx *rx;
	struct lirc_buffer *rbuf = ir->l.rbuf;
	int ret = 0, written = 0, retries = 0;
	unsigned int m;
	DECLARE_WAITQUEUE(wait, current);

	dev_dbg(ir->l.dev, "read called\n");
	if (n % rbuf->chunk_size) {
		dev_dbg(ir->l.dev, "read result = -EINVAL\n");
		return -EINVAL;
	}

	rx = get_ir_rx(ir);
	if (rx == NULL)
		return -ENXIO;

	/*
	 * we add ourselves to the task queue before buffer check
	 * to avoid losing scan code (in case when queue is awaken somewhere
	 * between while condition checking and scheduling)
	 */
	add_wait_queue(&rbuf->wait_poll, &wait);
	set_current_state(TASK_INTERRUPTIBLE);

	/*
	 * while we didn't provide 'length' bytes, device is opened in blocking
	 * mode and 'copy_to_user' is happy, wait for data.
	 */
	while (written < n && ret == 0) {
		if (lirc_buffer_empty(rbuf)) {
			/*
			 * According to the read(2) man page, 'written' can be
			 * returned as less than 'n', instead of blocking
			 * again, returning -EWOULDBLOCK, or returning
			 * -ERESTARTSYS
			 */
			if (written)
				break;
			if (filep->f_flags & O_NONBLOCK) {
				ret = -EWOULDBLOCK;
				break;
			}
			if (signal_pending(current)) {
				ret = -ERESTARTSYS;
				break;
			}
			schedule();
			set_current_state(TASK_INTERRUPTIBLE);
		} else {
			unsigned char buf[MAX_XFER_SIZE];

			if (rbuf->chunk_size > sizeof(buf)) {
				dev_err(ir->l.dev,
					"chunk_size is too big (%d)!\n",
					rbuf->chunk_size);
				ret = -EINVAL;
				break;
			}
			m = lirc_buffer_read(rbuf, buf);
			if (m == rbuf->chunk_size) {
				ret = copy_to_user(outbuf + written, buf,
						   rbuf->chunk_size);
				written += rbuf->chunk_size;
			} else {
				retries++;
			}
			if (retries >= 5) {
				dev_err(ir->l.dev, "Buffer read failed!\n");
				ret = -EIO;
			}
		}
	}

	remove_wait_queue(&rbuf->wait_poll, &wait);
	put_ir_rx(rx, false);
	set_current_state(TASK_RUNNING);

	dev_dbg(ir->l.dev, "read result = %d (%s)\n", ret,
		ret ? "Error" : "OK");

	return ret ? ret : written;
}

/* send a keypress to the IR TX device */
static int send_code(struct IR_tx *tx, unsigned int code, unsigned int key)
{
	unsigned char data_block[TX_BLOCK_SIZE];
	unsigned char buf[2];
	int i, ret;

	/* Get data for the codeset/key */
	ret = get_key_data(data_block, code, key);

	if (ret == -EPROTO) {
		dev_err(tx->ir->l.dev,
			"failed to get data for code %u, key %u -- check lircd.conf entries\n",
			code, key);
		return ret;
	} else if (ret != 0)
		return ret;

	/* Send the data block */
	ret = send_data_block(tx, data_block);
	if (ret != 0)
		return ret;

	/* Send data block length? */
	buf[0] = 0x00;
	buf[1] = 0x40;
	ret = i2c_master_send(tx->c, buf, 2);
	if (ret != 2) {
		dev_err(tx->ir->l.dev, "i2c_master_send failed with %d\n", ret);
		return ret < 0 ? ret : -EFAULT;
	}

	/* Give the z8 a moment to process data block */
	for (i = 0; i < 10; i++) {
		ret = i2c_master_send(tx->c, buf, 1);
		if (ret == 1)
			break;
		udelay(100);
	}

	if (ret != 1) {
		dev_err(tx->ir->l.dev, "i2c_master_send failed with %d\n", ret);
		return ret < 0 ? ret : -EFAULT;
	}

	/* Send finished download? */
	ret = i2c_master_recv(tx->c, buf, 1);
	if (ret != 1) {
		dev_err(tx->ir->l.dev, "i2c_master_recv failed with %d\n", ret);
		return ret < 0 ? ret : -EFAULT;
	}
	if (buf[0] != 0xA0) {
		dev_err(tx->ir->l.dev, "unexpected IR TX response #1: %02x\n",
			buf[0]);
		return -EFAULT;
	}

	/* Send prepare command? */
	buf[0] = 0x00;
	buf[1] = 0x80;
	ret = i2c_master_send(tx->c, buf, 2);
	if (ret != 2) {
		dev_err(tx->ir->l.dev, "i2c_master_send failed with %d\n", ret);
		return ret < 0 ? ret : -EFAULT;
	}

	/*
	 * The sleep bits aren't necessary on the HD PVR, and in fact, the
	 * last i2c_master_recv always fails with a -5, so for now, we're
	 * going to skip this whole mess and say we're done on the HD PVR
	 */
	if (!tx->post_tx_ready_poll) {
		dev_dbg(tx->ir->l.dev, "sent code %u, key %u\n", code, key);
		return 0;
	}

	/*
	 * This bit NAKs until the device is ready, so we retry it
	 * sleeping a bit each time.  This seems to be what the windows
	 * driver does, approximately.
	 * Try for up to 1s.
	 */
	for (i = 0; i < 20; ++i) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout((50 * HZ + 999) / 1000);
		ret = i2c_master_send(tx->c, buf, 1);
		if (ret == 1)
			break;
		dev_dbg(tx->ir->l.dev,
			"NAK expected: i2c_master_send failed with %d (try %d)\n",
			ret, i+1);
	}
	if (ret != 1) {
		dev_err(tx->ir->l.dev,
			"IR TX chip never got ready: last i2c_master_send failed with %d\n",
			ret);
		return ret < 0 ? ret : -EFAULT;
	}

	/* Seems to be an 'ok' response */
	i = i2c_master_recv(tx->c, buf, 1);
	if (i != 1) {
		dev_err(tx->ir->l.dev, "i2c_master_recv failed with %d\n", ret);
		return -EFAULT;
	}
	if (buf[0] != 0x80) {
		dev_err(tx->ir->l.dev, "unexpected IR TX response #2: %02x\n",
			buf[0]);
		return -EFAULT;
	}

	/* Oh good, it worked */
	dev_dbg(tx->ir->l.dev, "sent code %u, key %u\n", code, key);
	return 0;
}

/*
 * Write a code to the device.  We take in a 32-bit number (an int) and then
 * decode this to a codeset/key index.  The key data is then decompressed and
 * sent to the device.  We have a spin lock as per i2c documentation to prevent
 * multiple concurrent sends which would probably cause the device to explode.
 */
static ssize_t write(struct file *filep, const char __user *buf, size_t n,
		     loff_t *ppos)
{
	struct IR *ir = filep->private_data;
	struct IR_tx *tx;
	size_t i;
	int failures = 0;

	/* Validate user parameters */
	if (n % sizeof(int))
		return -EINVAL;

	/* Get a struct IR_tx reference */
	tx = get_ir_tx(ir);
	if (tx == NULL)
		return -ENXIO;

	/* Ensure our tx->c i2c_client remains valid for the duration */
	mutex_lock(&tx->client_lock);
	if (tx->c == NULL) {
		mutex_unlock(&tx->client_lock);
		put_ir_tx(tx, false);
		return -ENXIO;
	}

	/* Lock i2c bus for the duration */
	mutex_lock(&ir->ir_lock);

	/* Send each keypress */
	for (i = 0; i < n;) {
		int ret = 0;
		int command;

		if (copy_from_user(&command, buf + i, sizeof(command))) {
			mutex_unlock(&ir->ir_lock);
			mutex_unlock(&tx->client_lock);
			put_ir_tx(tx, false);
			return -EFAULT;
		}

		/* Send boot data first if required */
		if (tx->need_boot == 1) {
			/* Make sure we have the 'firmware' loaded, first */
			ret = fw_load(tx);
			if (ret != 0) {
				mutex_unlock(&ir->ir_lock);
				mutex_unlock(&tx->client_lock);
				put_ir_tx(tx, false);
				if (ret != -ENOMEM)
					ret = -EIO;
				return ret;
			}
			/* Prep the chip for transmitting codes */
			ret = send_boot_data(tx);
			if (ret == 0)
				tx->need_boot = 0;
		}

		/* Send the code */
		if (ret == 0) {
			ret = send_code(tx, (unsigned int)command >> 16,
					    (unsigned int)command & 0xFFFF);
			if (ret == -EPROTO) {
				mutex_unlock(&ir->ir_lock);
				mutex_unlock(&tx->client_lock);
				put_ir_tx(tx, false);
				return ret;
			}
		}

		/*
		 * Hmm, a failure.  If we've had a few then give up, otherwise
		 * try a reset
		 */
		if (ret != 0) {
			/* Looks like the chip crashed, reset it */
			dev_err(tx->ir->l.dev,
				"sending to the IR transmitter chip failed, trying reset\n");

			if (failures >= 3) {
				dev_err(tx->ir->l.dev,
					"unable to send to the IR chip after 3 resets, giving up\n");
				mutex_unlock(&ir->ir_lock);
				mutex_unlock(&tx->client_lock);
				put_ir_tx(tx, false);
				return ret;
			}
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout((100 * HZ + 999) / 1000);
			tx->need_boot = 1;
			++failures;
		} else
			i += sizeof(int);
	}

	/* Release i2c bus */
	mutex_unlock(&ir->ir_lock);

	mutex_unlock(&tx->client_lock);

	/* Give back our struct IR_tx reference */
	put_ir_tx(tx, false);

	/* All looks good */
	return n;
}

/* copied from lirc_dev */
static unsigned int poll(struct file *filep, poll_table *wait)
{
	struct IR *ir = filep->private_data;
	struct IR_rx *rx;
	struct lirc_buffer *rbuf = ir->l.rbuf;
	unsigned int ret;

	dev_dbg(ir->l.dev, "poll called\n");

	rx = get_ir_rx(ir);
	if (rx == NULL) {
		/*
		 * Revisit this, if our poll function ever reports writeable
		 * status for Tx
		 */
		dev_dbg(ir->l.dev, "poll result = POLLERR\n");
		return POLLERR;
	}

	/*
	 * Add our lirc_buffer's wait_queue to the poll_table. A wake up on
	 * that buffer's wait queue indicates we may have a new poll status.
	 */
	poll_wait(filep, &rbuf->wait_poll, wait);

	/* Indicate what ops could happen immediately without blocking */
	ret = lirc_buffer_empty(rbuf) ? 0 : (POLLIN|POLLRDNORM);

	dev_dbg(ir->l.dev, "poll result = %s\n",
		ret ? "POLLIN|POLLRDNORM" : "none");
	return ret;
}

static long ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	struct IR *ir = filep->private_data;
	unsigned long __user *uptr = (unsigned long __user *)arg;
	int result;
	unsigned long mode, features;

	features = ir->l.features;

	switch (cmd) {
	case LIRC_GET_LENGTH:
		result = put_user(13UL, uptr);
		break;
	case LIRC_GET_FEATURES:
		result = put_user(features, uptr);
		break;
	case LIRC_GET_REC_MODE:
		if (!(features&LIRC_CAN_REC_MASK))
			return -ENOSYS;

		result = put_user(LIRC_REC2MODE
				  (features&LIRC_CAN_REC_MASK),
				  uptr);
		break;
	case LIRC_SET_REC_MODE:
		if (!(features&LIRC_CAN_REC_MASK))
			return -ENOSYS;

		result = get_user(mode, uptr);
		if (!result && !(LIRC_MODE2REC(mode) & features))
			result = -EINVAL;
		break;
	case LIRC_GET_SEND_MODE:
		if (!(features&LIRC_CAN_SEND_MASK))
			return -ENOSYS;

		result = put_user(LIRC_MODE_PULSE, uptr);
		break;
	case LIRC_SET_SEND_MODE:
		if (!(features&LIRC_CAN_SEND_MASK))
			return -ENOSYS;

		result = get_user(mode, uptr);
		if (!result && mode != LIRC_MODE_PULSE)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}
	return result;
}

static struct IR *get_ir_device_by_minor(unsigned int minor)
{
	struct IR *ir;
	struct IR *ret = NULL;

	mutex_lock(&ir_devices_lock);

	if (!list_empty(&ir_devices_list)) {
		list_for_each_entry(ir, &ir_devices_list, list) {
			if (ir->l.minor == minor) {
				ret = get_ir_device(ir, true);
				break;
			}
		}
	}

	mutex_unlock(&ir_devices_lock);
	return ret;
}

/*
 * Open the IR device.  Get hold of our IR structure and
 * stash it in private_data for the file
 */
static int open(struct inode *node, struct file *filep)
{
	struct IR *ir;
	unsigned int minor = MINOR(node->i_rdev);

	/* find our IR struct */
	ir = get_ir_device_by_minor(minor);

	if (ir == NULL)
		return -ENODEV;

	atomic_inc(&ir->open_count);

	/* stash our IR struct */
	filep->private_data = ir;

	nonseekable_open(node, filep);
	return 0;
}

/* Close the IR device */
static int close(struct inode *node, struct file *filep)
{
	/* find our IR struct */
	struct IR *ir = filep->private_data;

	if (ir == NULL) {
		pr_err("ir: close: no private_data attached to the file!\n");
		return -ENODEV;
	}

	atomic_dec(&ir->open_count);

	put_ir_device(ir, false);
	return 0;
}

static int ir_remove(struct i2c_client *client);
static int ir_probe(struct i2c_client *client, const struct i2c_device_id *id);

#define ID_FLAG_TX	0x01
#define ID_FLAG_HDPVR	0x02

static const struct i2c_device_id ir_transceiver_id[] = {
	{ "ir_tx_z8f0811_haup",  ID_FLAG_TX                 },
	{ "ir_rx_z8f0811_haup",  0                          },
	{ "ir_tx_z8f0811_hdpvr", ID_FLAG_HDPVR | ID_FLAG_TX },
	{ "ir_rx_z8f0811_hdpvr", ID_FLAG_HDPVR              },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ir_transceiver_id);

static struct i2c_driver driver = {
	.driver = {
		.name	= "Zilog/Hauppauge i2c IR",
	},
	.probe		= ir_probe,
	.remove		= ir_remove,
	.id_table	= ir_transceiver_id,
};

static const struct file_operations lirc_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= read,
	.write		= write,
	.poll		= poll,
	.unlocked_ioctl	= ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= ioctl,
#endif
	.open		= open,
	.release	= close
};

static struct lirc_driver lirc_template = {
	.name		= "lirc_zilog",
	.minor		= -1,
	.code_length	= 13,
	.buffer_size	= BUFLEN / 2,
	.chunk_size	= 2,
	.fops		= &lirc_fops,
	.owner		= THIS_MODULE,
};

static int ir_remove(struct i2c_client *client)
{
	if (strncmp("ir_tx_z8", client->name, 8) == 0) {
		struct IR_tx *tx = i2c_get_clientdata(client);

		if (tx != NULL) {
			mutex_lock(&tx->client_lock);
			tx->c = NULL;
			mutex_unlock(&tx->client_lock);
			put_ir_tx(tx, false);
		}
	} else if (strncmp("ir_rx_z8", client->name, 8) == 0) {
		struct IR_rx *rx = i2c_get_clientdata(client);

		if (rx != NULL) {
			mutex_lock(&rx->client_lock);
			rx->c = NULL;
			mutex_unlock(&rx->client_lock);
			put_ir_rx(rx, false);
		}
	}
	return 0;
}


/* ir_devices_lock must be held */
static struct IR *get_ir_device_by_adapter(struct i2c_adapter *adapter)
{
	struct IR *ir;

	if (list_empty(&ir_devices_list))
		return NULL;

	list_for_each_entry(ir, &ir_devices_list, list)
		if (ir->adapter == adapter) {
			get_ir_device(ir, true);
			return ir;
		}

	return NULL;
}

static int ir_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct IR *ir;
	struct IR_tx *tx;
	struct IR_rx *rx;
	struct i2c_adapter *adap = client->adapter;
	int ret;
	bool tx_probe = false;

	dev_dbg(&client->dev, "%s: %s on i2c-%d (%s), client addr=0x%02x\n",
		__func__, id->name, adap->nr, adap->name, client->addr);

	/*
	 * The IR receiver    is at i2c address 0x71.
	 * The IR transmitter is at i2c address 0x70.
	 */

	if (id->driver_data & ID_FLAG_TX)
		tx_probe = true;
	else if (tx_only) /* module option */
		return -ENXIO;

	pr_info("probing IR %s on %s (i2c-%d)\n",
		   tx_probe ? "Tx" : "Rx", adap->name, adap->nr);

	mutex_lock(&ir_devices_lock);

	/* Use a single struct IR instance for both the Rx and Tx functions */
	ir = get_ir_device_by_adapter(adap);
	if (ir == NULL) {
		ir = kzalloc(sizeof(struct IR), GFP_KERNEL);
		if (!ir) {
			ret = -ENOMEM;
			goto out_no_ir;
		}
		kref_init(&ir->ref);

		/* store for use in ir_probe() again, and open() later on */
		INIT_LIST_HEAD(&ir->list);
		list_add_tail(&ir->list, &ir_devices_list);

		ir->adapter = adap;
		mutex_init(&ir->ir_lock);
		atomic_set(&ir->open_count, 0);
		spin_lock_init(&ir->tx_ref_lock);
		spin_lock_init(&ir->rx_ref_lock);

		/* set lirc_dev stuff */
		memcpy(&ir->l, &lirc_template, sizeof(struct lirc_driver));
		/*
		 * FIXME this is a pointer reference to us, but no refcount.
		 *
		 * This OK for now, since lirc_dev currently won't touch this
		 * buffer as we provide our own lirc_fops.
		 *
		 * Currently our own lirc_fops rely on this ir->l.rbuf pointer
		 */
		ir->l.rbuf = &ir->rbuf;
		ir->l.dev  = &adap->dev;
		ret = lirc_buffer_init(ir->l.rbuf,
				       ir->l.chunk_size, ir->l.buffer_size);
		if (ret)
			goto out_put_ir;
	}

	if (tx_probe) {
		/* Get the IR_rx instance for later, if already allocated */
		rx = get_ir_rx(ir);

		/* Set up a struct IR_tx instance */
		tx = kzalloc(sizeof(struct IR_tx), GFP_KERNEL);
		if (!tx) {
			ret = -ENOMEM;
			goto out_put_xx;
		}
		kref_init(&tx->ref);
		ir->tx = tx;

		ir->l.features |= LIRC_CAN_SEND_PULSE;
		mutex_init(&tx->client_lock);
		tx->c = client;
		tx->need_boot = 1;
		tx->post_tx_ready_poll =
			       (id->driver_data & ID_FLAG_HDPVR) ? false : true;

		/* An ir ref goes to the struct IR_tx instance */
		tx->ir = get_ir_device(ir, true);

		/* A tx ref goes to the i2c_client */
		i2c_set_clientdata(client, get_ir_tx(ir));

		/*
		 * Load the 'firmware'.  We do this before registering with
		 * lirc_dev, so the first firmware load attempt does not happen
		 * after a open() or write() call on the device.
		 *
		 * Failure here is not deemed catastrophic, so the receiver will
		 * still be usable.  Firmware load will be retried in write(),
		 * if it is needed.
		 */
		fw_load(tx);

		/* Proceed only if the Rx client is also ready or not needed */
		if (rx == NULL && !tx_only) {
			dev_info(tx->ir->l.dev,
				 "probe of IR Tx on %s (i2c-%d) done. Waiting on IR Rx.\n",
				 adap->name, adap->nr);
			goto out_ok;
		}
	} else {
		/* Get the IR_tx instance for later, if already allocated */
		tx = get_ir_tx(ir);

		/* Set up a struct IR_rx instance */
		rx = kzalloc(sizeof(struct IR_rx), GFP_KERNEL);
		if (!rx) {
			ret = -ENOMEM;
			goto out_put_xx;
		}
		kref_init(&rx->ref);
		ir->rx = rx;

		ir->l.features |= LIRC_CAN_REC_LIRCCODE;
		mutex_init(&rx->client_lock);
		rx->c = client;
		rx->hdpvr_data_fmt =
			       (id->driver_data & ID_FLAG_HDPVR) ? true : false;

		/* An ir ref goes to the struct IR_rx instance */
		rx->ir = get_ir_device(ir, true);

		/* An rx ref goes to the i2c_client */
		i2c_set_clientdata(client, get_ir_rx(ir));

		/*
		 * Start the polling thread.
		 * It will only perform an empty loop around schedule_timeout()
		 * until we register with lirc_dev and the first user open()
		 */
		/* An ir ref goes to the new rx polling kthread */
		rx->task = kthread_run(lirc_thread, get_ir_device(ir, true),
				       "zilog-rx-i2c-%d", adap->nr);
		if (IS_ERR(rx->task)) {
			ret = PTR_ERR(rx->task);
			dev_err(tx->ir->l.dev,
				"%s: could not start IR Rx polling thread\n",
				__func__);
			/* Failed kthread, so put back the ir ref */
			put_ir_device(ir, true);
			/* Failure exit, so put back rx ref from i2c_client */
			i2c_set_clientdata(client, NULL);
			put_ir_rx(rx, true);
			ir->l.features &= ~LIRC_CAN_REC_LIRCCODE;
			goto out_put_tx;
		}

		/* Proceed only if the Tx client is also ready */
		if (tx == NULL) {
			pr_info("probe of IR Rx on %s (i2c-%d) done. Waiting on IR Tx.\n",
				   adap->name, adap->nr);
			goto out_ok;
		}
	}

	/* register with lirc */
	ir->l.minor = lirc_register_driver(&ir->l);
	if (ir->l.minor < 0) {
		dev_err(tx->ir->l.dev,
			"%s: lirc_register_driver() failed: %i\n",
			__func__, ir->l.minor);
		ret = -EBADRQC;
		goto out_put_xx;
	}
	dev_info(ir->l.dev,
		 "IR unit on %s (i2c-%d) registered as lirc%d and ready\n",
		 adap->name, adap->nr, ir->l.minor);

out_ok:
	if (rx != NULL)
		put_ir_rx(rx, true);
	if (tx != NULL)
		put_ir_tx(tx, true);
	put_ir_device(ir, true);
	dev_info(ir->l.dev,
		 "probe of IR %s on %s (i2c-%d) done\n",
		 tx_probe ? "Tx" : "Rx", adap->name, adap->nr);
	mutex_unlock(&ir_devices_lock);
	return 0;

out_put_xx:
	if (rx != NULL)
		put_ir_rx(rx, true);
out_put_tx:
	if (tx != NULL)
		put_ir_tx(tx, true);
out_put_ir:
	put_ir_device(ir, true);
out_no_ir:
	dev_err(&client->dev,
		"%s: probing IR %s on %s (i2c-%d) failed with %d\n",
		__func__, tx_probe ? "Tx" : "Rx", adap->name, adap->nr, ret);
	mutex_unlock(&ir_devices_lock);
	return ret;
}

static int __init zilog_init(void)
{
	int ret;

	pr_notice("Zilog/Hauppauge IR driver initializing\n");

	mutex_init(&tx_data_lock);

	request_module("firmware_class");

	ret = i2c_add_driver(&driver);
	if (ret)
		pr_err("initialization failed\n");
	else
		pr_notice("initialization complete\n");

	return ret;
}

static void __exit zilog_exit(void)
{
	i2c_del_driver(&driver);
	/* if loaded */
	fw_unload();
	pr_notice("Zilog/Hauppauge IR driver unloaded\n");
}

module_init(zilog_init);
module_exit(zilog_exit);

MODULE_DESCRIPTION("Zilog/Hauppauge infrared transmitter driver (i2c stack)");
MODULE_AUTHOR("Gerd Knorr, Michal Kochanowicz, Christoph Bartelmus, Ulrich Mueller, Stefan Jahn, Jerome Brock, Mark Weaver, Andy Walls");
MODULE_LICENSE("GPL");
/* for compat with old name, which isn't all that accurate anymore */
MODULE_ALIAS("lirc_pvr150");

module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Enable debugging messages");

module_param(tx_only, bool, 0644);
MODULE_PARM_DESC(tx_only, "Only handle the IR transmit function");

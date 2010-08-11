#include <linux/module.h>
#include <linux/types.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/poll.h>

#include "ts27010_mux.h"
#include "ts27010_ringbuf.h"

struct ts27010_ldisc_data {
	struct ts27010_ringbuf		*rbuf;
	struct work_struct		recv_work;
	spinlock_t			recv_lock;

	struct mutex			send_lock;
};

static void ts27010_ldisc_recv_worker(struct work_struct *work)
{
	struct ts27010_ldisc_data *ts =
		container_of(work, struct ts27010_ldisc_data, recv_work);

	/* TODO: should have a *mux to pass around */
	ts27010_mux_recv(ts->rbuf);
}


int ts27010_ldisc_send(struct tty_struct *tty, u8 *data, int len)
{
	struct ts27010_ldisc_data *ts = 0;

	if (tty->disc_data == NULL) {
		pr_err("\n %s try to send mux command while	\
			ttyS is closed.\n", __func__);
		return len;
	} else
		ts = tty->disc_data;

	mutex_lock(&ts->send_lock);
	if (tty->driver->ops->write_room(tty) < len)
		pr_err("\n******** write overflow ********\n\n");
	len = tty->driver->ops->write(tty, data, len);
	mutex_unlock(&ts->send_lock);
	return len;
}

/*
 * Called when a tty is put into tx27010mux line discipline. Called in process
 * context.
 */
static int ts27010_ldisc_open(struct tty_struct *tty)
{
	struct ts27010_ldisc_data *ts;
	int err;

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL) {
		err = -ENOMEM;
		goto err0;
	}

	ts->rbuf = ts27010_ringbuf_alloc(LDISC_BUFFER_SIZE);
	if (ts->rbuf == NULL) {
		err = ENOMEM;
		goto err1;
	}
	INIT_WORK(&ts->recv_work, ts27010_ldisc_recv_worker);

	mutex_init(&ts->send_lock);
	ts->recv_lock = __SPIN_LOCK_UNLOCKED(ts->recv_lock);

	tty->disc_data = ts;

	/* TODO: goes away with clean tty interface */
	ts27010mux_tty = tty;

	return 0;

err1:
	kfree(ts);
err0:
	return err;
}

/*
 * Called when the tty is put into another line discipline
 * or it hangs up.  We have to wait for any cpu currently
 * executing in any of the other ts27010_tty_* routines to
 * finish before we can call tsmux27010_unregister_channel and free
 * the tsmux27010 struct.  This routine must be called from
 * process context, not interrupt or softirq context.
 */
static void ts27010_ldisc_close(struct tty_struct *tty)
{
	struct ts27010_ldisc_data *ts = tty->disc_data;

	if (!ts)
		return;

	tty->disc_data = NULL;
	/* TODO: goes away with clean tty interface */
	ts27010mux_tty = NULL;
	/* TODO: find some way of dealing with ts_data freeing safely */
	ts27010_ringbuf_free(ts->rbuf);
	kfree(ts);
}

/*
 * Called on tty hangup in process context.
 *
 * Wait for I/O to driver to complete and unregister ts27010mux channel.
 * This is already done by the close routine, so just call that.
 */
static int ts27010_ldisc_hangup(struct tty_struct *tty)
{
	ts27010_ldisc_close(tty);
	return 0;
}
/*
 * Read does nothing - no data is ever available this way.
 */
static ssize_t ts27010_ldisc_read(struct tty_struct *tty, struct file *file,
				   unsigned char __user *buf, size_t count)
{
	return -EAGAIN;
}

/*
 * Write on the tty does nothing.
 */
static ssize_t ts27010_ldisc_write(struct tty_struct *tty, struct file *file,
				   const unsigned char *buf, size_t count)
{
	return -EAGAIN;
}

/*
 * Called in process context only. May be re-entered by multiple
 * ioctl calling threads.
 */
static int ts27010_ldisc_ioctl(struct tty_struct *tty, struct file *file,
				unsigned int cmd, unsigned long arg)
{
	int err;

	switch (cmd) {
	default:
		/* Try the various mode ioctls */
		err = tty_mode_ioctl(tty, file, cmd, arg);
	}

	return err;
}

/* No kernel lock - fine */
static unsigned int ts27010_ldisc_poll(struct tty_struct *tty,
				       struct file *file,
				       poll_table *wait)
{
	return 0;
}

/*
 * This can now be called from hard interrupt level as well
 * as soft interrupt level or mainline.  Because of this,
 * we copy the data and schedule work so that we can assure
 * the mux receive code is called in processes context.
 */
static void ts27010_ldisc_receive(struct tty_struct *tty,
				  const unsigned char *data,
				  char *cflags, int count)
{
	struct ts27010_ldisc_data *ts = tty->disc_data;
	int n;
	unsigned long flags;

	WARN_ON(count == 0);

	spin_lock_irqsave(&ts->recv_lock, flags);
	n = ts27010_ringbuf_write(ts->rbuf, data, count);
	spin_unlock_irqrestore(&ts->recv_lock, flags);

	if (n < count)
		pr_err("ts27010_ldisc: buffer overrun.  dropping data.\n");

	schedule_work(&ts->recv_work);
}

static void ts27010_ldisc_wakeup(struct tty_struct *tty)
{
	pr_info(" Enter into ts27010mux_tty_wakeup\n");
}



static struct tty_ldisc_ops ts27010_ldisc = {
	.owner  = THIS_MODULE,
	.magic	= TTY_LDISC_MAGIC,
	.name	= "n_ts27010",
	.open	= ts27010_ldisc_open,
	.close	= ts27010_ldisc_close,
	.hangup	= ts27010_ldisc_hangup,
	.read	= ts27010_ldisc_read,
	.write	= ts27010_ldisc_write,
	.ioctl	= ts27010_ldisc_ioctl,
	.poll	= ts27010_ldisc_poll,
	.receive_buf = ts27010_ldisc_receive,
	.write_wakeup = ts27010_ldisc_wakeup,
};

int ts27010_ldisc_init(void)
{
	int err;

	err = tty_register_ldisc(N_GSM0710, &ts27010_ldisc);
	if (err < 0)
		pr_err("ts27010: unable to register line discipline\n");

	return err;
}

void ts27010_ldisc_remove(void)
{
	tty_unregister_ldisc(N_GSM0710);
}

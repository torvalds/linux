#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include "ts27010_mux.h"
#include "ts27010_ringbuf.h"

struct ts27010_tty_channel_data {
	atomic_t ref_count;
	struct tty_struct *tty;
};

struct ts27010_tty_data {
	struct ts27010_tty_channel_data		chan[NR_MUXS];
};

/* TODO: find a good place to put this */
struct tty_driver *driver;

/* TODO: should request a major */
#define TS0710MUX_MAJOR 245
#define TS0710MUX_MINOR_START 0

int ts27010_tty_send(int line, u8 *data, int len)
{
	struct ts27010_tty_data *td = driver->driver_state;
	struct tty_struct *tty = td->chan[line].tty;

	if (!tty) {
		pr_info("ts27010: mux%d no open.  discarding %d bytes\n",
			line, len);
		return 0;
	}

	BUG_ON(tty_insert_flip_string(tty, data, len) != len);
	tty_flip_buffer_push(tty);
	return len;
}

int ts27010_tty_send_rbuf(int line, struct ts27010_ringbuf *rbuf,
			  int data_idx, int len)
{
	struct ts27010_tty_data *td = driver->driver_state;
	struct tty_struct *tty = td->chan[line].tty;

	if (!tty) {
		pr_info("ts27010: mux%d no open.  discarding %d bytes\n",
			line, len);
		return 0;
	}

	while (len--) {
		char c = ts27010_ringbuf_peek(rbuf, data_idx++);
		tty_insert_flip_char(tty, c, TTY_NORMAL);
	}
	tty_flip_buffer_push(tty);
	return len;
}

static int ts27010_tty_open(struct tty_struct *tty, struct file *filp)
{
	struct ts27010_tty_data *td = tty->driver->driver_state;
	int err;
	int line;

	if (!ts27010_mux_active()) {
		pr_err("ts27010: tty open when line discipline not active.\n");
		err = -ENODEV;
		goto err;
	}

	line = tty->index;
	if ((line < 0) || (line >= NR_MUXS)) {
		pr_err("ts27010: tty index out of range.\n");
		err = -ENODEV;
		goto err;
	}

	atomic_inc(&td->chan[line].ref_count);

	td->chan[line].tty = tty;

	err = ts27010_mux_line_open(line);
	if (err < 0)
		goto err;

	return 0;

err:
	return err;
}


static void ts27010_tty_close(struct tty_struct *tty, struct file *filp)
{
	struct ts27010_tty_data *td = tty->driver->driver_state;

	if (atomic_dec_and_test(&td->chan[tty->index].ref_count)) {
		ts27010_mux_line_close(tty->index);

		td->chan[tty->index].tty = NULL;

		/*
		 * the old code did:
		 *   wake_up_interruptible(&tty->read_wait);
		 *   wake_up_interruptible(&tty->write_wait);
		 *
		 * I belive this is unecessary
		 */
	}
}

static int ts27010_tty_write(struct tty_struct *tty,
			     const unsigned char *buf, int count)
{
	return ts27010_mux_line_write(tty->index, buf, count);
}


static int ts27010_tty_write_room(struct tty_struct *tty)
{
	return ts27010_mux_line_write_room(tty->index);
}

static void ts27010_tty_flush_buffer(struct tty_struct *tty)
{
	pr_warning("ts27010: flush_buffer not implemented on line %d\n",
		tty->index);
}

static int ts27010_tty_chars_in_buffer(struct tty_struct *tty)
{
	return ts27010_mux_line_chars_in_buffer(tty->index);
}

static void ts27010_tty_throttle(struct tty_struct *tty)
{
	pr_warning("ts27010: throttle not implemented on line %d\n",
		tty->index);
}

static void ts27010_tty_unthrottle(struct tty_struct *tty)
{
	pr_warning("ts27010: unthrottle not implemented on line %d\n",
		tty->index);
}

static int ts27010_tty_ioctl(struct tty_struct *tty, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	int line;

	line = tty->index;
	if ((line < 0) || (line >= NR_MUXS))
		return -ENODEV;

	switch (cmd) {
	case TS0710MUX_IO_MSC_HANGUP:
		pr_warning("ts27010: ioctl msc_hangup not implemented\n");
		return 0;

	case TS0710MUX_IO_TEST_CMD:
		pr_warning("ts27010: ioctl msc_hangup not implemented\n");
		return 0;

	default:
		break;
	}

	return -ENOIOCTLCMD;
}

static const struct tty_operations ts27010_tty_ops = {
	.open = ts27010_tty_open,
	.close = ts27010_tty_close,
	.write = ts27010_tty_write,
	.write_room = ts27010_tty_write_room,
	.flush_buffer = ts27010_tty_flush_buffer,
	.chars_in_buffer = ts27010_tty_chars_in_buffer,
	.throttle = ts27010_tty_throttle,
	.unthrottle = ts27010_tty_unthrottle,
	.ioctl = ts27010_tty_ioctl,
};

int ts27010_tty_init(void)
{
	struct ts27010_tty_data *td;
	int err;
	int i;

	driver = alloc_tty_driver(NR_MUXS);
	if (driver == NULL) {
		err = -ENOMEM;
		goto err0;
	}

	td = kzalloc(sizeof(*td), GFP_KERNEL);
	if (td == NULL) {
		err = -ENOMEM;
		goto err1;
	}

	for (i = 0; i < NR_MUXS; i++)
		atomic_set(&td->chan[i].ref_count, 0);

	driver->driver_state = td;

	driver->driver_name = "ts0710mux";
	driver->name = "ts0710mux";
	driver->major = TS0710MUX_MAJOR;
	driver->major = 234;
	driver->minor_start = TS0710MUX_MINOR_START;
	driver->type = TTY_DRIVER_TYPE_SERIAL;
	driver->subtype = SERIAL_TYPE_NORMAL;
	driver->init_termios = tty_std_termios;
	driver->init_termios.c_iflag = 0;
	driver->init_termios.c_oflag = 0;
	driver->init_termios.c_cflag = B38400 | CS8 | CREAD;
	driver->init_termios.c_lflag = 0;
	driver->flags = TTY_DRIVER_RESET_TERMIOS | TTY_DRIVER_REAL_RAW;

	driver->other = NULL;
	driver->owner = THIS_MODULE;

	tty_set_operations(driver, &ts27010_tty_ops);

	if (tty_register_driver(driver)) {
		pr_err("ts27010: can't register tty driver\n");
		err = -EINVAL;
		goto err2;
	}

	return 0;

err2:
	kfree(td);
err1:
	put_tty_driver(driver);
err0:
	return err;
}

void ts27010_tty_remove(void)
{
	struct ts27010_tty_data *td = driver->driver_state;
	tty_unregister_driver(driver);
	kfree(td);
	put_tty_driver(driver);
}


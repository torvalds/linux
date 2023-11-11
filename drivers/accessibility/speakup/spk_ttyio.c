// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/slab.h>

#include "speakup.h"
#include "spk_types.h"
#include "spk_priv.h"

struct spk_ldisc_data {
	char buf;
	struct completion completion;
	bool buf_free;
	struct spk_synth *synth;
};

/*
 * This allows to catch within spk_ttyio_ldisc_open whether it is getting set
 * on for a speakup-driven device.
 */
static struct tty_struct *speakup_tty;
/* This mutex serializes the use of such global speakup_tty variable */
static DEFINE_MUTEX(speakup_tty_mutex);

static int ser_to_dev(int ser, dev_t *dev_no)
{
	if (ser < 0 || ser > (255 - 64)) {
		pr_err("speakup: Invalid ser param. Must be between 0 and 191 inclusive.\n");
		return -EINVAL;
	}

	*dev_no = MKDEV(4, (64 + ser));
	return 0;
}

static int get_dev_to_use(struct spk_synth *synth, dev_t *dev_no)
{
	/* use ser only when dev is not specified */
	if (strcmp(synth->dev_name, SYNTH_DEFAULT_DEV) ||
	    synth->ser == SYNTH_DEFAULT_SER)
		return tty_dev_name_to_number(synth->dev_name, dev_no);

	return ser_to_dev(synth->ser, dev_no);
}

static int spk_ttyio_ldisc_open(struct tty_struct *tty)
{
	struct spk_ldisc_data *ldisc_data;

	if (tty != speakup_tty)
		/* Somebody tried to use this line discipline outside speakup */
		return -ENODEV;

	if (!tty->ops->write)
		return -EOPNOTSUPP;

	ldisc_data = kmalloc(sizeof(*ldisc_data), GFP_KERNEL);
	if (!ldisc_data)
		return -ENOMEM;

	init_completion(&ldisc_data->completion);
	ldisc_data->buf_free = true;
	tty->disc_data = ldisc_data;

	return 0;
}

static void spk_ttyio_ldisc_close(struct tty_struct *tty)
{
	kfree(tty->disc_data);
}

static int spk_ttyio_receive_buf2(struct tty_struct *tty,
				  const unsigned char *cp,
				  const char *fp, int count)
{
	struct spk_ldisc_data *ldisc_data = tty->disc_data;
	struct spk_synth *synth = ldisc_data->synth;

	if (synth->read_buff_add) {
		int i;

		for (i = 0; i < count; i++)
			synth->read_buff_add(cp[i]);

		return count;
	}

	if (!ldisc_data->buf_free)
		/* ttyio_in will tty_flip_buffer_push */
		return 0;

	/* Make sure the consumer has read buf before we have seen
	 * buf_free == true and overwrite buf
	 */
	mb();

	ldisc_data->buf = cp[0];
	ldisc_data->buf_free = false;
	complete(&ldisc_data->completion);

	return 1;
}

static struct tty_ldisc_ops spk_ttyio_ldisc_ops = {
	.owner          = THIS_MODULE,
	.num		= N_SPEAKUP,
	.name           = "speakup_ldisc",
	.open           = spk_ttyio_ldisc_open,
	.close          = spk_ttyio_ldisc_close,
	.receive_buf2	= spk_ttyio_receive_buf2,
};

static int spk_ttyio_out(struct spk_synth *in_synth, const char ch);
static int spk_ttyio_out_unicode(struct spk_synth *in_synth, u16 ch);
static void spk_ttyio_send_xchar(struct spk_synth *in_synth, char ch);
static void spk_ttyio_tiocmset(struct spk_synth *in_synth, unsigned int set, unsigned int clear);
static unsigned char spk_ttyio_in(struct spk_synth *in_synth);
static unsigned char spk_ttyio_in_nowait(struct spk_synth *in_synth);
static void spk_ttyio_flush_buffer(struct spk_synth *in_synth);
static int spk_ttyio_wait_for_xmitr(struct spk_synth *in_synth);

struct spk_io_ops spk_ttyio_ops = {
	.synth_out = spk_ttyio_out,
	.synth_out_unicode = spk_ttyio_out_unicode,
	.send_xchar = spk_ttyio_send_xchar,
	.tiocmset = spk_ttyio_tiocmset,
	.synth_in = spk_ttyio_in,
	.synth_in_nowait = spk_ttyio_in_nowait,
	.flush_buffer = spk_ttyio_flush_buffer,
	.wait_for_xmitr = spk_ttyio_wait_for_xmitr,
};
EXPORT_SYMBOL_GPL(spk_ttyio_ops);

static inline void get_termios(struct tty_struct *tty,
			       struct ktermios *out_termios)
{
	down_read(&tty->termios_rwsem);
	*out_termios = tty->termios;
	up_read(&tty->termios_rwsem);
}

static int spk_ttyio_initialise_ldisc(struct spk_synth *synth)
{
	int ret = 0;
	struct tty_struct *tty;
	struct ktermios tmp_termios;
	dev_t dev;

	ret = get_dev_to_use(synth, &dev);
	if (ret)
		return ret;

	tty = tty_kopen_exclusive(dev);
	if (IS_ERR(tty))
		return PTR_ERR(tty);

	if (tty->ops->open)
		ret = tty->ops->open(tty, NULL);
	else
		ret = -ENODEV;

	if (ret) {
		tty_unlock(tty);
		return ret;
	}

	clear_bit(TTY_HUPPED, &tty->flags);
	/* ensure hardware flow control is enabled */
	get_termios(tty, &tmp_termios);
	if (!(tmp_termios.c_cflag & CRTSCTS)) {
		tmp_termios.c_cflag |= CRTSCTS;
		tty_set_termios(tty, &tmp_termios);
		/*
		 * check c_cflag to see if it's updated as tty_set_termios
		 * may not return error even when no tty bits are
		 * changed by the request.
		 */
		get_termios(tty, &tmp_termios);
		if (!(tmp_termios.c_cflag & CRTSCTS))
			pr_warn("speakup: Failed to set hardware flow control\n");
	}

	tty_unlock(tty);

	mutex_lock(&speakup_tty_mutex);
	speakup_tty = tty;
	ret = tty_set_ldisc(tty, N_SPEAKUP);
	speakup_tty = NULL;
	mutex_unlock(&speakup_tty_mutex);

	if (!ret) {
		/* Success */
		struct spk_ldisc_data *ldisc_data = tty->disc_data;

		ldisc_data->synth = synth;
		synth->dev = tty;
		return 0;
	}

	pr_err("speakup: Failed to set N_SPEAKUP on tty\n");

	tty_lock(tty);
	if (tty->ops->close)
		tty->ops->close(tty, NULL);
	tty_unlock(tty);

	tty_kclose(tty);

	return ret;
}

void spk_ttyio_register_ldisc(void)
{
	if (tty_register_ldisc(&spk_ttyio_ldisc_ops))
		pr_warn("speakup: Error registering line discipline. Most synths won't work.\n");
}

void spk_ttyio_unregister_ldisc(void)
{
	tty_unregister_ldisc(&spk_ttyio_ldisc_ops);
}

static int spk_ttyio_out(struct spk_synth *in_synth, const char ch)
{
	struct tty_struct *tty = in_synth->dev;
	int ret;

	if (!in_synth->alive || !tty->ops->write)
		return 0;

	ret = tty->ops->write(tty, &ch, 1);

	if (ret == 0)
		/* No room */
		return 0;

	if (ret > 0)
		/* Success */
		return 1;

	pr_warn("%s: I/O error, deactivating speakup\n",
		in_synth->long_name);
	/* No synth any more, so nobody will restart TTYs,
	 * and we thus need to do it ourselves.  Now that there
	 * is no synth we can let application flood anyway
	 */
	in_synth->alive = 0;
	speakup_start_ttys();
	return 0;
}

static int spk_ttyio_out_unicode(struct spk_synth *in_synth, u16 ch)
{
	int ret;

	if (ch < 0x80) {
		ret = spk_ttyio_out(in_synth, ch);
	} else if (ch < 0x800) {
		ret  = spk_ttyio_out(in_synth, 0xc0 | (ch >> 6));
		ret &= spk_ttyio_out(in_synth, 0x80 | (ch & 0x3f));
	} else {
		ret  = spk_ttyio_out(in_synth, 0xe0 | (ch >> 12));
		ret &= spk_ttyio_out(in_synth, 0x80 | ((ch >> 6) & 0x3f));
		ret &= spk_ttyio_out(in_synth, 0x80 | (ch & 0x3f));
	}
	return ret;
}

static void spk_ttyio_send_xchar(struct spk_synth *in_synth, char ch)
{
	struct tty_struct *tty = in_synth->dev;

	if (tty->ops->send_xchar)
		tty->ops->send_xchar(tty, ch);
}

static void spk_ttyio_tiocmset(struct spk_synth *in_synth, unsigned int set, unsigned int clear)
{
	struct tty_struct *tty = in_synth->dev;

	if (tty->ops->tiocmset)
		tty->ops->tiocmset(tty, set, clear);
}

static int spk_ttyio_wait_for_xmitr(struct spk_synth *in_synth)
{
	return 1;
}

static unsigned char ttyio_in(struct spk_synth *in_synth, int timeout)
{
	struct tty_struct *tty = in_synth->dev;
	struct spk_ldisc_data *ldisc_data = tty->disc_data;
	char rv;

	if (!timeout) {
		if (!try_wait_for_completion(&ldisc_data->completion))
			return 0xff;
	} else if (wait_for_completion_timeout(&ldisc_data->completion,
					usecs_to_jiffies(timeout)) == 0) {
		pr_warn("spk_ttyio: timeout (%d)  while waiting for input\n",
			timeout);
		return 0xff;
	}

	rv = ldisc_data->buf;
	/* Make sure we have read buf before we set buf_free to let
	 * the producer overwrite it
	 */
	mb();
	ldisc_data->buf_free = true;
	/* Let TTY push more characters */
	tty_flip_buffer_push(tty->port);

	return rv;
}

static unsigned char spk_ttyio_in(struct spk_synth *in_synth)
{
	return ttyio_in(in_synth, SPK_SYNTH_TIMEOUT);
}

static unsigned char spk_ttyio_in_nowait(struct spk_synth *in_synth)
{
	u8 rv = ttyio_in(in_synth, 0);

	return (rv == 0xff) ? 0 : rv;
}

static void spk_ttyio_flush_buffer(struct spk_synth *in_synth)
{
	struct tty_struct *tty = in_synth->dev;

	if (tty->ops->flush_buffer)
		tty->ops->flush_buffer(tty);
}

int spk_ttyio_synth_probe(struct spk_synth *synth)
{
	int rv = spk_ttyio_initialise_ldisc(synth);

	if (rv)
		return rv;

	synth->alive = 1;

	return 0;
}
EXPORT_SYMBOL_GPL(spk_ttyio_synth_probe);

void spk_ttyio_release(struct spk_synth *in_synth)
{
	struct tty_struct *tty = in_synth->dev;

	if (tty == NULL)
		return;

	tty_lock(tty);

	if (tty->ops->close)
		tty->ops->close(tty, NULL);

	tty_ldisc_flush(tty);
	tty_unlock(tty);
	tty_kclose(tty);

	in_synth->dev = NULL;
}
EXPORT_SYMBOL_GPL(spk_ttyio_release);

const char *spk_ttyio_synth_immediate(struct spk_synth *in_synth, const char *buff)
{
	struct tty_struct *tty = in_synth->dev;
	u_char ch;

	while ((ch = *buff)) {
		if (ch == '\n')
			ch = in_synth->procspeech;
		if (tty_write_room(tty) < 1 ||
		    !in_synth->io_ops->synth_out(in_synth, ch))
			return buff;
		buff++;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(spk_ttyio_synth_immediate);

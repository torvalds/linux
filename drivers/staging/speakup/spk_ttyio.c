#include <linux/types.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/slab.h>

#include "speakup.h"
#include "spk_types.h"
#include "spk_priv.h"

struct spk_ldisc_data {
	char buf;
	struct semaphore sem;
	bool buf_free;
};

static struct spk_synth *spk_ttyio_synth;
static struct tty_struct *speakup_tty;

static int spk_ttyio_ldisc_open(struct tty_struct *tty)
{
	struct spk_ldisc_data *ldisc_data;

	if (tty->ops->write == NULL)
		return -EOPNOTSUPP;
	speakup_tty = tty;

	ldisc_data = kmalloc(sizeof(struct spk_ldisc_data), GFP_KERNEL);
	if (!ldisc_data) {
		pr_err("speakup: Failed to allocate ldisc_data.\n");
		return -ENOMEM;
	}

	sema_init(&ldisc_data->sem, 0);
	ldisc_data->buf_free = true;
	speakup_tty->disc_data = ldisc_data;

	return 0;
}

static void spk_ttyio_ldisc_close(struct tty_struct *tty)
{
	kfree(speakup_tty->disc_data);
	speakup_tty = NULL;
}

static int spk_ttyio_receive_buf2(struct tty_struct *tty,
		const unsigned char *cp, char *fp, int count)
{
	struct spk_ldisc_data *ldisc_data = tty->disc_data;

	if (spk_ttyio_synth->read_buff_add) {
		int i;
		for (i = 0; i < count; i++)
			spk_ttyio_synth->read_buff_add(cp[i]);

		return count;
	}

	if (!ldisc_data->buf_free)
		/* ttyio_in will tty_schedule_flip */
		return 0;

	/* Make sure the consumer has read buf before we have seen
	 * buf_free == true and overwrite buf */
	mb();

	ldisc_data->buf = cp[0];
	ldisc_data->buf_free = false;
	up(&ldisc_data->sem);

	return 1;
}

static struct tty_ldisc_ops spk_ttyio_ldisc_ops = {
	.owner          = THIS_MODULE,
	.magic          = TTY_LDISC_MAGIC,
	.name           = "speakup_ldisc",
	.open           = spk_ttyio_ldisc_open,
	.close          = spk_ttyio_ldisc_close,
	.receive_buf2	= spk_ttyio_receive_buf2,
};

static int spk_ttyio_out(struct spk_synth *in_synth, const char ch);
static void spk_ttyio_send_xchar(char ch);
static void spk_ttyio_tiocmset(unsigned int set, unsigned int clear);
static unsigned char spk_ttyio_in(void);
static unsigned char spk_ttyio_in_nowait(void);
static void spk_ttyio_flush_buffer(void);

struct spk_io_ops spk_ttyio_ops = {
	.synth_out = spk_ttyio_out,
	.send_xchar = spk_ttyio_send_xchar,
	.tiocmset = spk_ttyio_tiocmset,
	.synth_in = spk_ttyio_in,
	.synth_in_nowait = spk_ttyio_in_nowait,
	.flush_buffer = spk_ttyio_flush_buffer,
};
EXPORT_SYMBOL_GPL(spk_ttyio_ops);

static inline void get_termios(struct tty_struct *tty, struct ktermios *out_termios)
{
	down_read(&tty->termios_rwsem);
	*out_termios = tty->termios;
	up_read(&tty->termios_rwsem);
}

static int spk_ttyio_initialise_ldisc(int ser)
{
	int ret = 0;
	struct tty_struct *tty;
	struct ktermios tmp_termios;

	ret = tty_register_ldisc(N_SPEAKUP, &spk_ttyio_ldisc_ops);
	if (ret) {
		pr_err("Error registering line discipline.\n");
		return ret;
	}

	if (ser < 0 || ser > (255 - 64)) {
		pr_err("speakup: Invalid ser param. Must be between 0 and 191 inclusive.\n");
		return -EINVAL;
	}

	/* TODO: support more than ttyS* */
	tty = tty_open_by_driver(MKDEV(4, (ser +  64)), NULL, NULL);
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
		 * check c_cflag to see if it's updated as tty_set_termios may not return
		 * error even when no tty bits are changed by the request.
		 */
		get_termios(tty, &tmp_termios);
		if (!(tmp_termios.c_cflag & CRTSCTS))
			pr_warn("speakup: Failed to set hardware flow control\n");
	}

	tty_unlock(tty);

	ret = tty_set_ldisc(tty, N_SPEAKUP);

	return ret;
}

static int spk_ttyio_out(struct spk_synth *in_synth, const char ch)
{
	if (in_synth->alive && speakup_tty && speakup_tty->ops->write) {
		int ret = speakup_tty->ops->write(speakup_tty, &ch, 1);
		if (ret == 0)
			/* No room */
			return 0;
		if (ret < 0) {
			pr_warn("%s: I/O error, deactivating speakup\n", in_synth->long_name);
			/* No synth any more, so nobody will restart TTYs, and we thus
			 * need to do it ourselves.  Now that there is no synth we can
			 * let application flood anyway
			 */
			in_synth->alive = 0;
			speakup_start_ttys();
			return 0;
		}
		return 1;
	}
	return 0;
}

static void spk_ttyio_send_xchar(char ch)
{
	speakup_tty->ops->send_xchar(speakup_tty, ch);
}

static void spk_ttyio_tiocmset(unsigned int set, unsigned int clear)
{
	speakup_tty->ops->tiocmset(speakup_tty, set, clear);
}

static unsigned char ttyio_in(int timeout)
{
	struct spk_ldisc_data *ldisc_data = speakup_tty->disc_data;
	char rv;

	if (down_timeout(&ldisc_data->sem, usecs_to_jiffies(timeout)) == -ETIME) {
		if (timeout)
			pr_warn("spk_ttyio: timeout (%d)  while waiting for input\n",
				timeout);
		return 0xff;
	}

	rv = ldisc_data->buf;
	/* Make sure we have read buf before we set buf_free to let
	 * the producer overwrite it */
	mb();
	ldisc_data->buf_free = true;
	/* Let TTY push more characters */
	tty_schedule_flip(speakup_tty->port);

	return rv;
}

static unsigned char spk_ttyio_in(void)
{
	return ttyio_in(SPK_SYNTH_TIMEOUT);
}

static unsigned char spk_ttyio_in_nowait(void)
{
	u8 rv = ttyio_in(0);

	return (rv == 0xff) ? 0 : rv;
}

static void spk_ttyio_flush_buffer(void)
{
	if (speakup_tty->ops->flush_buffer)
		speakup_tty->ops->flush_buffer(speakup_tty);
}

int spk_ttyio_synth_probe(struct spk_synth *synth)
{
	int rv = spk_ttyio_initialise_ldisc(synth->ser);

	if (rv)
		return rv;

	synth->alive = 1;
	spk_ttyio_synth = synth;

	return 0;
}
EXPORT_SYMBOL_GPL(spk_ttyio_synth_probe);

void spk_ttyio_release(void)
{
	int idx;

	if (!speakup_tty)
		return;

	tty_lock(speakup_tty);
	idx = speakup_tty->index;

	if (speakup_tty->ops->close)
		speakup_tty->ops->close(speakup_tty, NULL);

	tty_ldisc_flush(speakup_tty);
	tty_unlock(speakup_tty);
	tty_ldisc_release(speakup_tty);
}
EXPORT_SYMBOL_GPL(spk_ttyio_release);

const char *spk_ttyio_synth_immediate(struct spk_synth *synth, const char *buff)
{
	u_char ch;

	while ((ch = *buff)) {
		if (ch == '\n')
			ch = synth->procspeech;
		if (tty_write_room(speakup_tty) < 1 || !synth->io_ops->synth_out(synth, ch))
			return buff;
		buff++;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(spk_ttyio_synth_immediate);

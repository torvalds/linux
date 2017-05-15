#include <linux/types.h>
#include <linux/tty.h>

#include "speakup.h"
#include "spk_types.h"

static struct tty_struct *speakup_tty;

static int spk_ttyio_ldisc_open(struct tty_struct *tty)
{
	if (tty->ops->write == NULL)
		return -EOPNOTSUPP;
	speakup_tty = tty;

	return 0;
}

static void spk_ttyio_ldisc_close(struct tty_struct *tty)
{
	speakup_tty = NULL;
}

static struct tty_ldisc_ops spk_ttyio_ldisc_ops = {
	.owner          = THIS_MODULE,
	.magic          = TTY_LDISC_MAGIC,
	.name           = "speakup_ldisc",
	.open           = spk_ttyio_ldisc_open,
	.close          = spk_ttyio_ldisc_close,
};

static int spk_ttyio_out(struct spk_synth *in_synth, const char ch);
struct spk_io_ops spk_ttyio_ops = {
	.synth_out = spk_ttyio_out,
};
EXPORT_SYMBOL_GPL(spk_ttyio_ops);

static int spk_ttyio_initialise_ldisc(int ser)
{
	int ret = 0;
	struct tty_struct *tty;

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

int spk_ttyio_synth_probe(struct spk_synth *synth)
{
	int rv = spk_ttyio_initialise_ldisc(synth->ser);

	if (rv)
		return rv;

	synth->alive = 1;

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

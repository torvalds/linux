/*
 *    ebcdic keycode functions for s390 console drivers
 *
 *    Copyright IBM Corp. 2003
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 */

#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/keyboard.h>

#define NR_FN_HANDLER	20

struct kbd_data;

typedef void (fn_handler_fn)(struct kbd_data *);

/*
 * FIXME: explain key_maps tricks.
 */

struct kbd_data {
	struct tty_port *port;
	unsigned short **key_maps;
	char **func_table;
	fn_handler_fn **fn_handler;
	struct kbdiacruc *accent_table;
	unsigned int accent_table_size;
	unsigned int diacr;
	unsigned short sysrq;
};

struct kbd_data *kbd_alloc(void);
void kbd_free(struct kbd_data *);
void kbd_ascebc(struct kbd_data *, unsigned char *);

void kbd_keycode(struct kbd_data *, unsigned int);
int kbd_ioctl(struct kbd_data *, unsigned int, unsigned long);

/*
 * Helper Functions.
 */
static inline void
kbd_put_queue(struct tty_port *port, int ch)
{
	struct tty_struct *tty = tty_port_tty_get(port);
	if (!tty)
		return;
	tty_insert_flip_char(tty, ch, 0);
	tty_schedule_flip(tty);
	tty_kref_put(tty);
}

static inline void
kbd_puts_queue(struct tty_port *port, char *cp)
{
	struct tty_struct *tty = tty_port_tty_get(port);
	if (!tty)
		return;
	while (*cp)
		tty_insert_flip_char(tty, *cp++, 0);
	tty_schedule_flip(tty);
	tty_kref_put(tty);
}

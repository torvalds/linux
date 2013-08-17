/* 
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __LINE_H__
#define __LINE_H__

#include "linux/list.h"
#include "linux/workqueue.h"
#include "linux/tty.h"
#include "linux/interrupt.h"
#include "linux/spinlock.h"
#include "linux/mutex.h"
#include "chan_user.h"
#include "mconsole_kern.h"

/* There's only two modifiable fields in this - .mc.list and .driver */
struct line_driver {
	const char *name;
	const char *device_name;
	const short major;
	const short minor_start;
	const short type;
	const short subtype;
	const int read_irq;
	const char *read_irq_name;
	const int write_irq;
	const char *write_irq_name;
	struct mc_device mc;
	struct tty_driver *driver;
};

struct line {
	struct tty_struct *tty;
	struct mutex count_lock;
	unsigned long count;
	int valid;

	char *init_str;
	struct list_head chan_list;
	struct chan *chan_in, *chan_out;

	/*This lock is actually, mostly, local to*/
	spinlock_t lock;
	int throttled;
	/* Yes, this is a real circular buffer.
	 * XXX: And this should become a struct kfifo!
	 *
	 * buffer points to a buffer allocated on demand, of length
	 * LINE_BUFSIZE, head to the start of the ring, tail to the end.*/
	char *buffer;
	char *head;
	char *tail;

	int sigio;
	struct delayed_work task;
	const struct line_driver *driver;
};

extern void line_close(struct tty_struct *tty, struct file * filp);
extern int line_open(struct line *lines, struct tty_struct *tty);
extern int line_setup(char **conf, unsigned nlines, char **def,
		      char *init, char *name);
extern int line_write(struct tty_struct *tty, const unsigned char *buf,
		      int len);
extern int line_put_char(struct tty_struct *tty, unsigned char ch);
extern void line_set_termios(struct tty_struct *tty, struct ktermios * old);
extern int line_chars_in_buffer(struct tty_struct *tty);
extern void line_flush_buffer(struct tty_struct *tty);
extern void line_flush_chars(struct tty_struct *tty);
extern int line_write_room(struct tty_struct *tty);
extern int line_ioctl(struct tty_struct *tty, unsigned int cmd,
				unsigned long arg);
extern void line_throttle(struct tty_struct *tty);
extern void line_unthrottle(struct tty_struct *tty);

extern char *add_xterm_umid(char *base);
extern int line_setup_irq(int fd, int input, int output, struct line *line,
			  void *data);
extern void line_close_chan(struct line *line);
extern int register_lines(struct line_driver *line_driver,
			  const struct tty_operations *driver,
			  struct line *lines, int nlines);
extern int setup_one_line(struct line *lines, int n, char *init,
			  const struct chan_opts *opts, char **error_out);
extern void close_lines(struct line *lines, int nlines);

extern int line_config(struct line *lines, unsigned int sizeof_lines,
		       char *str, const struct chan_opts *opts,
		       char **error_out);
extern int line_id(char **str, int *start_out, int *end_out);
extern int line_remove(struct line *lines, unsigned int sizeof_lines, int n,
		       char **error_out);
extern int line_get_config(char *dev, struct line *lines,
			   unsigned int sizeof_lines, char *str,
			   int size, char **error_out);

#endif

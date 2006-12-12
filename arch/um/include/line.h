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
#include "chan_user.h"
#include "mconsole_kern.h"

struct line_driver {
	char *name;
	char *device_name;
	short major;
	short minor_start;
	short type;
	short subtype;
	int read_irq;
	char *read_irq_name;
	int write_irq;
	char *write_irq_name;
	char *symlink_from;
	char *symlink_to;
	struct mc_device mc;
};

struct line {
	struct tty_struct *tty;
	char *init_str;
	int init_pri;
	struct list_head chan_list;
	int valid;
	int count;
	int throttled;
	/*This lock is actually, mostly, local to*/
	spinlock_t lock;

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
	int have_irq;
};

#define LINE_INIT(str, d) \
	{ .init_str =	str, \
	  .init_pri =	INIT_STATIC, \
	  .valid =	1, \
	  .lock =	SPIN_LOCK_UNLOCKED, \
	  .driver =	d }

struct lines {
	int num;
};

#define LINES_INIT(n) {  .num =	n }

extern void line_close(struct tty_struct *tty, struct file * filp);
extern int line_open(struct line *lines, struct tty_struct *tty);
extern int line_setup(struct line *lines, unsigned int sizeof_lines,
		      char *init);
extern int line_write(struct tty_struct *tty, const unsigned char *buf,
		      int len);
extern void line_put_char(struct tty_struct *tty, unsigned char ch);
extern void line_set_termios(struct tty_struct *tty, struct ktermios * old);
extern int line_chars_in_buffer(struct tty_struct *tty);
extern void line_flush_buffer(struct tty_struct *tty);
extern void line_flush_chars(struct tty_struct *tty);
extern int line_write_room(struct tty_struct *tty);
extern int line_ioctl(struct tty_struct *tty, struct file * file,
		      unsigned int cmd, unsigned long arg);
extern void line_throttle(struct tty_struct *tty);
extern void line_unthrottle(struct tty_struct *tty);

extern char *add_xterm_umid(char *base);
extern int line_setup_irq(int fd, int input, int output, struct line *line,
			  void *data);
extern void line_close_chan(struct line *line);
extern struct tty_driver * line_register_devfs(struct lines *set,
					       struct line_driver *line_driver,
					       const struct tty_operations *driver,
					       struct line *lines, int nlines);
extern void lines_init(struct line *lines, int nlines, struct chan_opts *opts);
extern void close_lines(struct line *lines, int nlines);

extern int line_config(struct line *lines, unsigned int sizeof_lines,
		       char *str, const struct chan_opts *opts);
extern int line_id(char **str, int *start_out, int *end_out);
extern int line_remove(struct line *lines, unsigned int sizeof_lines, int n);
extern int line_get_config(char *dev, struct line *lines,
			   unsigned int sizeof_lines, char *str,
			   int size, char **error_out);

#endif

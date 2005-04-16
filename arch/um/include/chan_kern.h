/* 
 * Copyright (C) 2000, 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __CHAN_KERN_H__
#define __CHAN_KERN_H__

#include "linux/tty.h"
#include "linux/list.h"
#include "linux/console.h"
#include "chan_user.h"
#include "line.h"

struct chan {
	struct list_head list;
	char *dev;
	unsigned int primary:1;
	unsigned int input:1;
	unsigned int output:1;
	unsigned int opened:1;
	int fd;
	enum chan_init_pri pri;
	struct chan_ops *ops;
	void *data;
};

extern void chan_interrupt(struct list_head *chans, struct work_struct *task,
			   struct tty_struct *tty, int irq);
extern int parse_chan_pair(char *str, struct list_head *chans, int pri, 
			   int device, struct chan_opts *opts);
extern int open_chan(struct list_head *chans);
extern int write_chan(struct list_head *chans, const char *buf, int len,
			     int write_irq);
extern int console_write_chan(struct list_head *chans, const char *buf, 
			      int len);
extern int console_open_chan(struct line *line, struct console *co,
			     struct chan_opts *opts);
extern void close_chan(struct list_head *chans);
extern void chan_enable_winch(struct list_head *chans, struct tty_struct *tty);
extern void enable_chan(struct list_head *chans, struct tty_struct *tty);
extern int chan_window_size(struct list_head *chans, 
			     unsigned short *rows_out, 
			     unsigned short *cols_out);
extern int chan_out_fd(struct list_head *chans);
extern int chan_config_string(struct list_head *chans, char *str, int size,
			      char **error_out);

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */

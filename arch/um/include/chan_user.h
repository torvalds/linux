/* 
 * Copyright (C) 2000, 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __CHAN_USER_H__
#define __CHAN_USER_H__

#include "init.h"

struct chan_opts {
	void (*announce)(char *dev_name, int dev);
	char *xterm_title;
	int raw;
	unsigned long tramp_stack;
	int in_kernel;
};

enum chan_init_pri { INIT_STATIC, INIT_ALL, INIT_ONE };

struct chan_ops {
	char *type;
	void *(*init)(char *, int, const struct chan_opts *);
	int (*open)(int, int, int, void *, char **);
	void (*close)(int, void *);
	int (*read)(int, char *, void *);
	int (*write)(int, const char *, int, void *);
	int (*console_write)(int, const char *, int);
	int (*window_size)(int, void *, unsigned short *, unsigned short *);
	void (*free)(void *);
	int winch;
};

extern const struct chan_ops fd_ops, null_ops, port_ops, pts_ops, pty_ops,
	tty_ops, xterm_ops;

extern void generic_close(int fd, void *unused);
extern int generic_read(int fd, char *c_out, void *unused);
extern int generic_write(int fd, const char *buf, int n, void *unused);
extern int generic_console_write(int fd, const char *buf, int n);
extern int generic_window_size(int fd, void *unused, unsigned short *rows_out,
			       unsigned short *cols_out);
extern void generic_free(void *data);

struct tty_struct;
extern void register_winch(int fd,  struct tty_struct *tty);
extern void register_winch_irq(int fd, int tty_fd, int pid, struct tty_struct *tty);

#define __channel_help(fn, prefix) \
__uml_help(fn, prefix "[0-9]*=<channel description>\n" \
"    Attach a console or serial line to a host channel.  See\n" \
"    http://user-mode-linux.sourceforge.net/input.html for a complete\n" \
"    description of this switch.\n\n" \
);

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

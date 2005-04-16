/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdlib.h>
#include <errno.h>
#include "chan_user.h"
#include "os.h"

static int null_chan;

static void *null_init(char *str, int device, struct chan_opts *opts)
{
	return(&null_chan);
}

static int null_open(int input, int output, int primary, void *d,
		     char **dev_out)
{
	*dev_out = NULL;
	return(os_open_file(DEV_NULL, of_rdwr(OPENFLAGS()), 0));
}

static int null_read(int fd, char *c_out, void *unused)
{
	return(-ENODEV);
}

static void null_free(void *data)
{
}

struct chan_ops null_ops = {
	.type		= "null",
	.init		= null_init,
	.open		= null_open,
	.close		= generic_close,
	.read		= null_read,
	.write		= generic_write,
	.console_write	= generic_console_write,
	.window_size	= generic_window_size,
	.free		= null_free,
	.winch		= 0,
};

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

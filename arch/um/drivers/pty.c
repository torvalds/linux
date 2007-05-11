/* 
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include "chan_user.h"
#include "user.h"
#include "kern_util.h"
#include "os.h"
#include "um_malloc.h"

struct pty_chan {
	void (*announce)(char *dev_name, int dev);
	int dev;
	int raw;
	struct termios tt;
	char dev_name[sizeof("/dev/pts/0123456\0")];
};

static void *pty_chan_init(char *str, int device, const struct chan_opts *opts)
{
	struct pty_chan *data;

	data = um_kmalloc(sizeof(*data));
	if(data == NULL) return(NULL);
	*data = ((struct pty_chan) { .announce  	= opts->announce, 
				     .dev  		= device,
				     .raw  		= opts->raw });
	return(data);
}

static int pts_open(int input, int output, int primary, void *d,
		    char **dev_out)
{
	struct pty_chan *data = d;
	char *dev;
	int fd, err;

	fd = get_pty();
	if(fd < 0){
		err = -errno;
		printk("open_pts : Failed to open pts\n");
		return err;
	}
	if(data->raw){
		CATCH_EINTR(err = tcgetattr(fd, &data->tt));
		if(err)
			return(err);

		err = raw(fd);
		if(err)
			return(err);
	}

	dev = ptsname(fd);
	sprintf(data->dev_name, "%s", dev);
	*dev_out = data->dev_name;
	if (data->announce)
		(*data->announce)(dev, data->dev);
	return(fd);
}

static int getmaster(char *line)
{
	char *pty, *bank, *cp;
	int master, err;

	pty = &line[strlen("/dev/ptyp")];
	for (bank = "pqrs"; *bank; bank++) {
		line[strlen("/dev/pty")] = *bank;
		*pty = '0';
		if (os_stat_file(line, NULL) < 0)
			break;
		for (cp = "0123456789abcdef"; *cp; cp++) {
			*pty = *cp;
			master = os_open_file(line, of_rdwr(OPENFLAGS()), 0);
			if (master >= 0) {
				char *tp = &line[strlen("/dev/")];

				/* verify slave side is usable */
				*tp = 't';
				err = os_access(line, OS_ACC_RW_OK);
				*tp = 'p';
				if(err == 0) return(master);
				(void) os_close_file(master);
			}
		}
	}
	return(-1);
}

static int pty_open(int input, int output, int primary, void *d,
		    char **dev_out)
{
	struct pty_chan *data = d;
	int fd, err;
	char dev[sizeof("/dev/ptyxx\0")] = "/dev/ptyxx";

	fd = getmaster(dev);
	if(fd < 0)
		return(-errno);

	if(data->raw){
		err = raw(fd);
		if(err)
			return(err);
	}
	
	if(data->announce) (*data->announce)(dev, data->dev);

	sprintf(data->dev_name, "%s", dev);
	*dev_out = data->dev_name;
	return(fd);
}

const struct chan_ops pty_ops = {
	.type		= "pty",
	.init		= pty_chan_init,
	.open		= pty_open,
	.close		= generic_close,
	.read		= generic_read,
	.write		= generic_write,
	.console_write	= generic_console_write,
	.window_size	= generic_window_size,
	.free		= generic_free,
	.winch		= 0,
};

const struct chan_ops pts_ops = {
	.type		= "pts",
	.init		= pty_chan_init,
	.open		= pts_open,
	.close		= generic_close,
	.read		= generic_read,
	.write		= generic_write,
	.console_write	= generic_console_write,
	.window_size	= generic_window_size,
	.free		= generic_free,
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

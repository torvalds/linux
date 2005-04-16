/*
 * Copyright (C) 2000, 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include "user_util.h"
#include "kern_util.h"
#include "user.h"
#include "initrd.h"
#include "os.h"

int load_initrd(char *filename, void *buf, int size)
{
	int fd, n;

	fd = os_open_file(filename, of_read(OPENFLAGS()), 0);
	if(fd < 0){
		printk("Opening '%s' failed - err = %d\n", filename, -fd);
		return(-1);
	}
	n = os_read_file(fd, buf, size);
	if(n != size){
		printk("Read of %d bytes from '%s' failed, err = %d\n", size,
		       filename, -n);
		return(-1);
	}

	os_close_file(fd);
	return(0);
}

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

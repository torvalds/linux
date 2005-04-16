/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/config.h"
#include "linux/mm.h"
#include "mem_user.h"

unsigned long set_task_sizes_skas(int arg, unsigned long *host_size_out, 
				  unsigned long *task_size_out)
{
	/* Round up to the nearest 4M */
	unsigned long top = ROUND_4M((unsigned long) &arg);

#ifdef CONFIG_HOST_TASK_SIZE
	*host_size_out = CONFIG_HOST_TASK_SIZE;
	*task_size_out = CONFIG_HOST_TASK_SIZE;
#else
	*host_size_out = top;
	*task_size_out = top;
#endif
	return(((unsigned long) set_task_sizes_skas) & ~0xffffff);
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

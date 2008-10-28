/*
 * Copyright (C) 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#ifndef __MEM_KERN_H__
#define __MEM_KERN_H__

#include "linux/list.h"
#include "linux/types.h"

struct remapper {
	struct list_head list;
	int (*proc)(int, unsigned long, int, __u64);
};

extern void register_remapper(struct remapper *info);

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

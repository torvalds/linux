/*
 * Copyright (C) 2011 Mathieu Desnoyers (mathieu.desnoyers@efficios.com)
 *
 * wrapper around vmalloc_sync_all. Using KALLSYMS to get its address when
 * available, else we need to have a kernel that exports this function to GPL
 * modules.
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

#ifdef CONFIG_KALLSYMS

#include <linux/kallsyms.h>
#include <linux/fs.h>
#include <linux/splice.h>
#include "kallsyms.h"

static
ssize_t (*splice_to_pipe_sym)(struct pipe_inode_info *pipe,
			      struct splice_pipe_desc *spd);

ssize_t wrapper_splice_to_pipe(struct pipe_inode_info *pipe,
			       struct splice_pipe_desc *spd)
{
	if (!splice_to_pipe_sym)
		splice_to_pipe_sym = (void *) kallsyms_lookup_funcptr("splice_to_pipe"); 
	if (splice_to_pipe_sym) {
		return splice_to_pipe_sym(pipe, spd);
	} else {
		printk(KERN_WARNING "LTTng: splice_to_pipe symbol lookup failed.\n");
		return -ENOSYS;
	}
}

#else

#include <linux/fs.h>
#include <linux/splice.h>

ssize_t wrapper_splice_to_pipe(struct pipe_inode_info *pipe,
			       struct splice_pipe_desc *spd)
{
	return splice_to_pipe(pipe, spd);
}

#endif

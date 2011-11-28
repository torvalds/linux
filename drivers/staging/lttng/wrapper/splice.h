#ifndef _LTT_WRAPPER_SPLICE_H
#define _LTT_WRAPPER_SPLICE_H

/*
 * Copyright (C) 2011 Mathieu Desnoyers (mathieu.desnoyers@efficios.com)
 *
 * wrapper around vmalloc_sync_all. Using KALLSYMS to get its address when
 * available, else we need to have a kernel that exports this function to GPL
 * modules.
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

#include <linux/splice.h>

ssize_t wrapper_splice_to_pipe(struct pipe_inode_info *pipe,
			       struct splice_pipe_desc *spd);

#ifndef PIPE_DEF_BUFFERS
#define PIPE_DEF_BUFFERS 16
#endif

#endif /* _LTT_WRAPPER_SPLICE_H */

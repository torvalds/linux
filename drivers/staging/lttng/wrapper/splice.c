/*
 * wrapper/splice.c
 *
 * wrapper around splice_to_pipe. Using KALLSYMS to get its address when
 * available, else we need to have a kernel that exports this function to GPL
 * modules.
 *
 * Copyright (C) 2011-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; only
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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

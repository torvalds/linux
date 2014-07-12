/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * API and structure definitions for params_tree.
 *
 * Author: LiuYing <emoly.liu@oracle.com>
 */
#ifndef __PARAMS_TREE_H__
#define __PARAMS_TREE_H__

#include "libcfs.h"

#undef LPROCFS
#if  defined(CONFIG_PROC_FS)
# define LPROCFS
#endif

#ifdef LPROCFS
typedef struct file			     cfs_param_file_t;
typedef struct inode			    cfs_inode_t;
typedef struct proc_inode		       cfs_proc_inode_t;
typedef struct seq_file			 cfs_seq_file_t;
typedef struct seq_operations		   cfs_seq_ops_t;
typedef struct file_operations		  cfs_param_file_ops_t;
typedef struct proc_dir_entry		   cfs_param_dentry_t;
typedef struct poll_table_struct		cfs_poll_table_t;
#define CFS_PARAM_MODULE			THIS_MODULE
#define cfs_file_private(file)		  (file->private_data)
#define cfs_dentry_data(dentry)		 (dentry->data)
#define cfs_proc_inode_pde(proc_inode)	  (proc_inode->pde)
#define cfs_proc_inode(proc_inode)	      (proc_inode->vfs_inode)
#define cfs_seq_read_common		     seq_read
#define cfs_seq_lseek_common		    seq_lseek
#define cfs_seq_private(seq)		    (seq->private)
#define cfs_seq_printf(seq, format, ...)	seq_printf(seq, format,  \
							   ## __VA_ARGS__)
#define cfs_seq_release(inode, file)	    seq_release(inode, file)
#define cfs_seq_puts(seq, s)		    seq_puts(seq, s)
#define cfs_seq_putc(seq, s)		    seq_putc(seq, s)
#define cfs_seq_read(file, buf, count, ppos, rc) (rc = seq_read(file, buf, \
							    count, ppos))
#define cfs_seq_open(file, ops, rc)	     (rc = seq_open(file, ops))

#else /* !LPROCFS */

typedef struct cfs_params_file {
	void	   *param_private;
	loff_t	  param_pos;
	unsigned int    param_flags;
} cfs_param_file_t;

typedef struct cfs_param_inode {
	void    *param_private;
} cfs_inode_t;

typedef struct cfs_param_dentry {
	void *param_data;
} cfs_param_dentry_t;

typedef struct cfs_proc_inode {
	cfs_param_dentry_t *param_pde;
	cfs_inode_t	 param_inode;
} cfs_proc_inode_t;

struct cfs_seq_operations;
typedef struct cfs_seq_file {
	char		      *buf;
	size_t		     size;
	size_t		     from;
	size_t		     count;
	loff_t		     index;
	loff_t		     version;
	struct mutex			lock;
	struct cfs_seq_operations *op;
	void		      *private;
} cfs_seq_file_t;

typedef struct cfs_seq_operations {
	void *(*start)(cfs_seq_file_t *m, loff_t *pos);
	void  (*stop)(cfs_seq_file_t *m, void *v);
	void *(*next)(cfs_seq_file_t *m, void *v, loff_t *pos);
	int   (*show)(cfs_seq_file_t *m, void *v);
} cfs_seq_ops_t;

typedef void *cfs_poll_table_t;

typedef struct cfs_param_file_ops {
	struct module *owner;
	int (*open)(cfs_inode_t *, struct file *);
	loff_t (*llseek)(struct file *, loff_t, int);
	int (*release)(cfs_inode_t *, cfs_param_file_t *);
	unsigned int (*poll)(struct file *, cfs_poll_table_t *);
	ssize_t (*write)(struct file *, const char *, size_t, loff_t *);
	ssize_t (*read)(struct file *, char *, size_t, loff_t *);
} cfs_param_file_ops_t;
typedef cfs_param_file_ops_t *cfs_lproc_filep_t;

static inline cfs_proc_inode_t *FAKE_PROC_I(const cfs_inode_t *inode)
{
	return container_of(inode, cfs_proc_inode_t, param_inode);
}

#define CFS_PARAM_MODULE			NULL
#define cfs_file_private(file)		  (file->param_private)
#define cfs_dentry_data(dentry)		 (dentry->param_data)
#define cfs_proc_inode(proc_inode)	      (proc_inode->param_inode)
#define cfs_proc_inode_pde(proc_inode)	  (proc_inode->param_pde)
#define cfs_seq_read_common		     NULL
#define cfs_seq_lseek_common		    NULL
#define cfs_seq_private(seq)		    (seq->private)
#define cfs_seq_read(file, buf, count, ppos, rc) do {} while (0)
#define cfs_seq_open(file, ops, rc)		     \
do {						    \
	 cfs_seq_file_t *p = cfs_file_private(file);    \
	 if (!p) {				      \
		LIBCFS_ALLOC(p, sizeof(*p));	    \
		if (!p) {			       \
			rc = -ENOMEM;		   \
			break;			  \
		}				       \
		cfs_file_private(file) = p;	     \
	}					       \
	memset(p, 0, sizeof(*p));		       \
	p->op = ops;				    \
	rc = 0;					 \
} while (0)

#endif /* LPROCFS */

/* XXX: params_tree APIs */

#endif  /* __PARAMS_TREE_H__ */

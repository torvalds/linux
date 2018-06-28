#ifndef _HFI1_DEBUGFS_H
#define _HFI1_DEBUGFS_H
/*
 * Copyright(c) 2015, 2016, 2018 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

struct hfi1_ibdev;

#define DEBUGFS_FILE_CREATE(name, parent, data, ops, mode)	\
do { \
	struct dentry *ent; \
	const char *__name = name; \
	ent = debugfs_create_file(__name, mode, parent, \
		data, ops); \
	if (!ent) \
		pr_warn("create of %s failed\n", __name); \
} while (0)

#define DEBUGFS_SEQ_FILE_OPS(name) \
static const struct seq_operations _##name##_seq_ops = { \
	.start = _##name##_seq_start, \
	.next  = _##name##_seq_next, \
	.stop  = _##name##_seq_stop, \
	.show  = _##name##_seq_show \
}

#define DEBUGFS_SEQ_FILE_OPEN(name) \
static int _##name##_open(struct inode *inode, struct file *s) \
{ \
	struct seq_file *seq; \
	int ret; \
	ret =  seq_open(s, &_##name##_seq_ops); \
	if (ret) \
		return ret; \
	seq = s->private_data; \
	seq->private = inode->i_private; \
	return 0; \
}

#define DEBUGFS_FILE_OPS(name) \
static const struct file_operations _##name##_file_ops = { \
	.owner   = THIS_MODULE, \
	.open    = _##name##_open, \
	.read    = hfi1_seq_read, \
	.llseek  = hfi1_seq_lseek, \
	.release = seq_release \
}

#define DEBUGFS_SEQ_FILE_CREATE(name, parent, data) \
	DEBUGFS_FILE_CREATE(#name, parent, data, &_##name##_file_ops, 0444)

ssize_t hfi1_seq_read(struct file *file, char __user *buf, size_t size,
		      loff_t *ppos);
loff_t hfi1_seq_lseek(struct file *file, loff_t offset, int whence);

#ifdef CONFIG_DEBUG_FS
void hfi1_dbg_ibdev_init(struct hfi1_ibdev *ibd);
void hfi1_dbg_ibdev_exit(struct hfi1_ibdev *ibd);
void hfi1_dbg_init(void);
void hfi1_dbg_exit(void);

#else
static inline void hfi1_dbg_ibdev_init(struct hfi1_ibdev *ibd)
{
}

static inline void hfi1_dbg_ibdev_exit(struct hfi1_ibdev *ibd)
{
}

static inline void hfi1_dbg_init(void)
{
}

static inline void hfi1_dbg_exit(void)
{
}
#endif

#endif                          /* _HFI1_DEBUGFS_H */

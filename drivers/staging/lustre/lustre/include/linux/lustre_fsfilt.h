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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/include/linux/lustre_fsfilt.h
 *
 * Filesystem interface helper.
 */

#ifndef _LINUX_LUSTRE_FSFILT_H
#define _LINUX_LUSTRE_FSFILT_H

#ifndef _LUSTRE_FSFILT_H
#error Do not #include this file directly. #include <lustre_fsfilt.h> instead
#endif


#include <obd.h>
#include <obd_class.h>

typedef void (*fsfilt_cb_t)(struct obd_device *obd, __u64 last_rcvd,
			    void *data, int error);

struct fsfilt_operations {
	struct list_head fs_list;
	module_t *fs_owner;
	char   *fs_type;
	char   *(* fs_getlabel)(struct super_block *sb);
	void   *(* fs_start)(struct inode *inode, int op, void *desc_private,
			     int logs);
	int     (* fs_commit)(struct inode *inode, void *handle,int force_sync);
	int     (* fs_map_inode_pages)(struct inode *inode, struct page **page,
				       int pages, unsigned long *blocks,
				       int create, struct mutex *sem);
	int     (* fs_write_record)(struct file *, void *, int size, loff_t *,
				    int force_sync);
	int     (* fs_read_record)(struct file *, void *, int size, loff_t *);
	int     (* fs_setup)(struct super_block *sb);
};

extern int fsfilt_register_ops(struct fsfilt_operations *fs_ops);
extern void fsfilt_unregister_ops(struct fsfilt_operations *fs_ops);
extern struct fsfilt_operations *fsfilt_get_ops(const char *type);
extern void fsfilt_put_ops(struct fsfilt_operations *fs_ops);

static inline char *fsfilt_get_label(struct obd_device *obd,
				     struct super_block *sb)
{
	if (obd->obd_fsops->fs_getlabel == NULL)
		return NULL;
	if (obd->obd_fsops->fs_getlabel(sb)[0] == '\0')
		return NULL;

	return obd->obd_fsops->fs_getlabel(sb);
}

#define FSFILT_OP_UNLINK		1
#define FSFILT_OP_CANCEL_UNLINK	 10

#define __fsfilt_check_slow(obd, start, msg)			      \
do {								      \
	if (cfs_time_before(jiffies, start + 15 * HZ))		\
		break;						    \
	else if (cfs_time_before(jiffies, start + 30 * HZ))	   \
		CDEBUG(D_VFSTRACE, "%s: slow %s %lus\n", obd->obd_name,   \
		       msg, (jiffies-start) / HZ);		    \
	else if (cfs_time_before(jiffies, start + DISK_TIMEOUT * HZ)) \
		CWARN("%s: slow %s %lus\n", obd->obd_name, msg,	   \
		      (jiffies - start) / HZ);			\
	else							      \
		CERROR("%s: slow %s %lus\n", obd->obd_name, msg,	  \
		       (jiffies - start) / HZ);		       \
} while (0)

#define fsfilt_check_slow(obd, start, msg)	      \
do {						    \
	__fsfilt_check_slow(obd, start, msg);	   \
	start = jiffies;				\
} while (0)

static inline void *fsfilt_start_log(struct obd_device *obd,
				     struct inode *inode, int op,
				     struct obd_trans_info *oti, int logs)
{
	unsigned long now = jiffies;
	void *parent_handle = oti ? oti->oti_handle : NULL;
	void *handle;

	handle = obd->obd_fsops->fs_start(inode, op, parent_handle, logs);
	CDEBUG(D_INFO, "started handle %p (%p)\n", handle, parent_handle);

	if (oti != NULL) {
		if (parent_handle == NULL) {
			oti->oti_handle = handle;
		} else if (handle != parent_handle) {
			CERROR("mismatch: parent %p, handle %p, oti %p\n",
			       parent_handle, handle, oti);
			LBUG();
		}
	}
	fsfilt_check_slow(obd, now, "journal start");
	return handle;
}

static inline int fsfilt_commit(struct obd_device *obd, struct inode *inode,
				void *handle, int force_sync)
{
	unsigned long now = jiffies;
	int rc = obd->obd_fsops->fs_commit(inode, handle, force_sync);
	CDEBUG(D_INFO, "committing handle %p\n", handle);

	fsfilt_check_slow(obd, now, "journal start");

	return rc;
}

static inline int fsfilt_map_inode_pages(struct obd_device *obd,
					 struct inode *inode,
					 struct page **page, int pages,
					 unsigned long *blocks,
					 int create, struct mutex *mutex)
{
	return obd->obd_fsops->fs_map_inode_pages(inode, page, pages, blocks,
						  create, mutex);
}

static inline int fsfilt_read_record(struct obd_device *obd, struct file *file,
				     void *buf, loff_t size, loff_t *offs)
{
	return obd->obd_fsops->fs_read_record(file, buf, size, offs);
}

static inline int fsfilt_write_record(struct obd_device *obd, struct file *file,
				      void *buf, loff_t size, loff_t *offs,
				      int force_sync)
{
	return obd->obd_fsops->fs_write_record(file, buf, size,offs,force_sync);
}

static inline int fsfilt_setup(struct obd_device *obd, struct super_block *fs)
{
	if (obd->obd_fsops->fs_setup)
		return obd->obd_fsops->fs_setup(fs);
	return 0;
}




#endif

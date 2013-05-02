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
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/obdclass/lprocfs_status.c
 *
 * Author: Hariharan Thantry <thantry@users.sourceforge.net>
 */

#define DEBUG_SUBSYSTEM S_CLASS


#include <obd_class.h>
#include <lprocfs_status.h>
#include <lustre/lustre_idl.h>

#if defined(LPROCFS)

static int lprocfs_no_percpu_stats = 0;
CFS_MODULE_PARM(lprocfs_no_percpu_stats, "i", int, 0644,
		"Do not alloc percpu data for lprocfs stats");

#define MAX_STRING_SIZE 128

/* for bug 10866, global variable */
DECLARE_RWSEM(_lprocfs_lock);
EXPORT_SYMBOL(_lprocfs_lock);

int lprocfs_single_release(struct inode *inode, struct file *file)
{
	LPROCFS_EXIT();
	return single_release(inode, file);
}
EXPORT_SYMBOL(lprocfs_single_release);

int lprocfs_seq_release(struct inode *inode, struct file *file)
{
	LPROCFS_EXIT();
	return seq_release(inode, file);
}
EXPORT_SYMBOL(lprocfs_seq_release);

static struct proc_dir_entry *__lprocfs_srch(struct proc_dir_entry *head,
					     const char *name)
{
	struct proc_dir_entry *temp;

	if (head == NULL)
		return NULL;

	temp = head->subdir;
	while (temp != NULL) {
		if (strcmp(temp->name, name) == 0) {
			return temp;
		}

		temp = temp->next;
	}
	return NULL;
}

struct proc_dir_entry *lprocfs_srch(struct proc_dir_entry *head,
				    const char *name)
{
	struct proc_dir_entry *temp;

	LPROCFS_SRCH_ENTRY();
	temp = __lprocfs_srch(head, name);
	LPROCFS_SRCH_EXIT();
	return temp;
}
EXPORT_SYMBOL(lprocfs_srch);

/* lprocfs API calls */

/* Function that emulates snprintf but also has the side effect of advancing
   the page pointer for the next write into the buffer, incrementing the total
   length written to the buffer, and decrementing the size left in the
   buffer. */
static int lprocfs_obd_snprintf(char **page, int end, int *len,
				const char *format, ...)
{
	va_list list;
	int n;

	if (*len >= end)
		return 0;

	va_start(list, format);
	n = vsnprintf(*page, end - *len, format, list);
	va_end(list);

	*page += n; *len += n;
	return n;
}

proc_dir_entry_t *lprocfs_add_simple(struct proc_dir_entry *root,
					 char *name,
					 read_proc_t *read_proc,
					 write_proc_t *write_proc,
					 void *data,
					 struct file_operations *fops)
{
	proc_dir_entry_t *proc;
	mode_t mode = 0;

	if (root == NULL || name == NULL)
		return ERR_PTR(-EINVAL);
	if (read_proc)
		mode = 0444;
	if (write_proc)
		mode |= 0200;
	if (fops)
		mode = 0644;
	LPROCFS_WRITE_ENTRY();
	proc = create_proc_entry(name, mode, root);
	if (!proc) {
		CERROR("LprocFS: No memory to create /proc entry %s", name);
		LPROCFS_WRITE_EXIT();
		return ERR_PTR(-ENOMEM);
	}
	proc->read_proc = read_proc;
	proc->write_proc = write_proc;
	proc->data = data;
	if (fops)
		proc->proc_fops = fops;
	LPROCFS_WRITE_EXIT();
	return proc;
}
EXPORT_SYMBOL(lprocfs_add_simple);

struct proc_dir_entry *lprocfs_add_symlink(const char *name,
			struct proc_dir_entry *parent, const char *format, ...)
{
	struct proc_dir_entry *entry;
	char *dest;
	va_list ap;

	if (parent == NULL || format == NULL)
		return NULL;

	OBD_ALLOC_WAIT(dest, MAX_STRING_SIZE + 1);
	if (dest == NULL)
		return NULL;

	va_start(ap, format);
	vsnprintf(dest, MAX_STRING_SIZE, format, ap);
	va_end(ap);

	entry = proc_symlink(name, parent, dest);
	if (entry == NULL)
		CERROR("LprocFS: Could not create symbolic link from %s to %s",
			name, dest);

	OBD_FREE(dest, MAX_STRING_SIZE + 1);
	return entry;
}
EXPORT_SYMBOL(lprocfs_add_symlink);

static ssize_t lprocfs_fops_read(struct file *f, char __user *buf,
				 size_t size, loff_t *ppos)
{
	struct proc_dir_entry *dp = PDE(f->f_dentry->d_inode);
	char *page, *start = NULL;
	int rc = 0, eof = 1, count;

	if (*ppos >= PAGE_CACHE_SIZE)
		return 0;

	page = (char *)__get_free_page(GFP_KERNEL);
	if (page == NULL)
		return -ENOMEM;

	if (LPROCFS_ENTRY_AND_CHECK(dp)) {
		rc = -ENOENT;
		goto out;
	}

	OBD_FAIL_TIMEOUT(OBD_FAIL_LPROC_REMOVE, 10);
	if (dp->read_proc)
		rc = dp->read_proc(page, &start, *ppos, PAGE_CACHE_SIZE,
				   &eof, dp->data);
	LPROCFS_EXIT();
	if (rc <= 0)
		goto out;

	/* for lustre proc read, the read count must be less than PAGE_SIZE */
	LASSERT(eof == 1);

	if (start == NULL) {
		rc -= *ppos;
		if (rc < 0)
			rc = 0;
		if (rc == 0)
			goto out;
		start = page + *ppos;
	} else if (start < page) {
		start = page;
	}

	count = (rc < size) ? rc : size;
	if (copy_to_user(buf, start, count)) {
		rc = -EFAULT;
		goto out;
	}
	*ppos += count;

out:
	free_page((unsigned long)page);
	return rc;
}

static ssize_t lprocfs_fops_write(struct file *f, const char __user *buf,
				  size_t size, loff_t *ppos)
{
	struct proc_dir_entry *dp = PDE(f->f_dentry->d_inode);
	int rc = -EIO;

	if (LPROCFS_ENTRY_AND_CHECK(dp))
		return -ENOENT;
	if (dp->write_proc)
		rc = dp->write_proc(f, buf, size, dp->data);
	LPROCFS_EXIT();
	return rc;
}

static struct file_operations lprocfs_generic_fops = {
	.owner = THIS_MODULE,
	.read = lprocfs_fops_read,
	.write = lprocfs_fops_write,
};

int lprocfs_evict_client_open(struct inode *inode, struct file *f)
{
	struct proc_dir_entry *dp = PDE(f->f_dentry->d_inode);
	struct obd_device *obd = dp->data;

	atomic_inc(&obd->obd_evict_inprogress);

	return 0;
}

int lprocfs_evict_client_release(struct inode *inode, struct file *f)
{
	struct proc_dir_entry *dp = PDE(f->f_dentry->d_inode);
	struct obd_device *obd = dp->data;

	atomic_dec(&obd->obd_evict_inprogress);
	wake_up(&obd->obd_evict_inprogress_waitq);

	return 0;
}

struct file_operations lprocfs_evict_client_fops = {
	.owner = THIS_MODULE,
	.read = lprocfs_fops_read,
	.write = lprocfs_fops_write,
	.open = lprocfs_evict_client_open,
	.release = lprocfs_evict_client_release,
};
EXPORT_SYMBOL(lprocfs_evict_client_fops);

/**
 * Add /proc entries.
 *
 * \param root [in]  The parent proc entry on which new entry will be added.
 * \param list [in]  Array of proc entries to be added.
 * \param data [in]  The argument to be passed when entries read/write routines
 *		   are called through /proc file.
 *
 * \retval 0   on success
 *	 < 0 on error
 */
int lprocfs_add_vars(struct proc_dir_entry *root, struct lprocfs_vars *list,
		     void *data)
{
	int rc = 0;

	if (root == NULL || list == NULL)
		return -EINVAL;

	LPROCFS_WRITE_ENTRY();
	while (list->name != NULL) {
		struct proc_dir_entry *cur_root, *proc;
		char *pathcopy, *cur, *next, pathbuf[64];
		int pathsize = strlen(list->name) + 1;

		proc = NULL;
		cur_root = root;

		/* need copy of path for strsep */
		if (strlen(list->name) > sizeof(pathbuf) - 1) {
			OBD_ALLOC(pathcopy, pathsize);
			if (pathcopy == NULL)
				GOTO(out, rc = -ENOMEM);
		} else {
			pathcopy = pathbuf;
		}

		next = pathcopy;
		strcpy(pathcopy, list->name);

		while (cur_root != NULL && (cur = strsep(&next, "/"))) {
			if (*cur =='\0') /* skip double/trailing "/" */
				continue;

			proc = __lprocfs_srch(cur_root, cur);
			CDEBUG(D_OTHER, "cur_root=%s, cur=%s, next=%s, (%s)\n",
			       cur_root->name, cur, next,
			       (proc ? "exists" : "new"));
			if (next != NULL) {
				cur_root = (proc ? proc :
					    proc_mkdir(cur, cur_root));
			} else if (proc == NULL) {
				mode_t mode = 0;
				if (list->proc_mode != 0000) {
					mode = list->proc_mode;
				} else {
					if (list->read_fptr)
						mode = 0444;
					if (list->write_fptr)
						mode |= 0200;
				}
				proc = create_proc_entry(cur, mode, cur_root);
			}
		}

		if (pathcopy != pathbuf)
			OBD_FREE(pathcopy, pathsize);

		if (cur_root == NULL || proc == NULL) {
			CERROR("LprocFS: No memory to create /proc entry %s",
			       list->name);
			GOTO(out, rc = -ENOMEM);
		}

		if (list->fops)
			proc->proc_fops = list->fops;
		else
			proc->proc_fops = &lprocfs_generic_fops;
		proc->read_proc = list->read_fptr;
		proc->write_proc = list->write_fptr;
		proc->data = (list->data ? list->data : data);
		list++;
	}
out:
	LPROCFS_WRITE_EXIT();
	return rc;
}
EXPORT_SYMBOL(lprocfs_add_vars);

void lprocfs_remove_nolock(struct proc_dir_entry **rooth)
{
	struct proc_dir_entry *root = *rooth;
	struct proc_dir_entry *temp = root;
	struct proc_dir_entry *rm_entry;
	struct proc_dir_entry *parent;

	if (!root)
		return;
	*rooth = NULL;

	parent = root->parent;
	LASSERT(parent != NULL);

	while (1) {
		while (temp->subdir != NULL)
			temp = temp->subdir;

		rm_entry = temp;
		temp = temp->parent;

		/* Memory corruption once caused this to fail, and
		   without this LASSERT we would loop here forever. */
		LASSERTF(strlen(rm_entry->name) == rm_entry->namelen,
			 "0x%p  %s/%s len %d\n", rm_entry, temp->name,
			 rm_entry->name, (int)strlen(rm_entry->name));

		remove_proc_entry(rm_entry->name, temp);
		if (temp == parent)
			break;
	}
}

void lprocfs_remove(struct proc_dir_entry **rooth)
{
	LPROCFS_WRITE_ENTRY(); /* search vs remove race */
	lprocfs_remove_nolock(rooth);
	LPROCFS_WRITE_EXIT();
}
EXPORT_SYMBOL(lprocfs_remove);

void lprocfs_remove_proc_entry(const char *name, struct proc_dir_entry *parent)
{
	LASSERT(parent != NULL);
	remove_proc_entry(name, parent);
}
EXPORT_SYMBOL(lprocfs_remove_proc_entry);

void lprocfs_try_remove_proc_entry(const char *name,
				   struct proc_dir_entry *parent)
{
	struct proc_dir_entry	 *t = NULL;
	struct proc_dir_entry	**p;
	int			  len, busy = 0;

	LASSERT(parent != NULL);
	len = strlen(name);

	LPROCFS_WRITE_ENTRY();

	/* lookup target name */
	for (p = &parent->subdir; *p; p = &(*p)->next) {
		if ((*p)->namelen != len)
			continue;
		if (memcmp(name, (*p)->name, len))
			continue;
		t = *p;
		break;
	}

	if (t) {
		/* verify it's empty: do not count "num_refs" */
		for (p = &t->subdir; *p; p = &(*p)->next) {
			if ((*p)->namelen != strlen("num_refs")) {
				busy = 1;
				break;
			}
			if (memcmp("num_refs", (*p)->name,
				   strlen("num_refs"))) {
				busy = 1;
				break;
			}
		}
	}

	if (busy == 0)
		lprocfs_remove_nolock(&t);

	LPROCFS_WRITE_EXIT();

	return;
}
EXPORT_SYMBOL(lprocfs_try_remove_proc_entry);

struct proc_dir_entry *lprocfs_register(const char *name,
					struct proc_dir_entry *parent,
					struct lprocfs_vars *list, void *data)
{
	struct proc_dir_entry *newchild;

	newchild = lprocfs_srch(parent, name);
	if (newchild != NULL) {
		CERROR(" Lproc: Attempting to register %s more than once \n",
		       name);
		return ERR_PTR(-EALREADY);
	}

	newchild = proc_mkdir(name, parent);
	if (newchild != NULL && list != NULL) {
		int rc = lprocfs_add_vars(newchild, list, data);
		if (rc) {
			lprocfs_remove(&newchild);
			return ERR_PTR(rc);
		}
	}
	return newchild;
}
EXPORT_SYMBOL(lprocfs_register);

/* Generic callbacks */
int lprocfs_rd_uint(char *page, char **start, off_t off,
		    int count, int *eof, void *data)
{
	unsigned int *temp = data;
	return snprintf(page, count, "%u\n", *temp);
}
EXPORT_SYMBOL(lprocfs_rd_uint);

int lprocfs_wr_uint(struct file *file, const char *buffer,
		    unsigned long count, void *data)
{
	unsigned *p = data;
	char dummy[MAX_STRING_SIZE + 1], *end;
	unsigned long tmp;

	dummy[MAX_STRING_SIZE] = '\0';
	if (copy_from_user(dummy, buffer, MAX_STRING_SIZE))
		return -EFAULT;

	tmp = simple_strtoul(dummy, &end, 0);
	if (dummy == end)
		return -EINVAL;

	*p = (unsigned int)tmp;
	return count;
}
EXPORT_SYMBOL(lprocfs_wr_uint);

int lprocfs_rd_u64(char *page, char **start, off_t off,
		   int count, int *eof, void *data)
{
	LASSERT(data != NULL);
	*eof = 1;
	return snprintf(page, count, LPU64"\n", *(__u64 *)data);
}
EXPORT_SYMBOL(lprocfs_rd_u64);

int lprocfs_rd_atomic(char *page, char **start, off_t off,
		   int count, int *eof, void *data)
{
	atomic_t *atom = data;
	LASSERT(atom != NULL);
	*eof = 1;
	return snprintf(page, count, "%d\n", atomic_read(atom));
}
EXPORT_SYMBOL(lprocfs_rd_atomic);

int lprocfs_wr_atomic(struct file *file, const char *buffer,
		      unsigned long count, void *data)
{
	atomic_t *atm = data;
	int val = 0;
	int rc;

	rc = lprocfs_write_helper(buffer, count, &val);
	if (rc < 0)
		return rc;

	if (val <= 0)
		return -ERANGE;

	atomic_set(atm, val);
	return count;
}
EXPORT_SYMBOL(lprocfs_wr_atomic);

int lprocfs_rd_uuid(char *page, char **start, off_t off, int count,
		    int *eof, void *data)
{
	struct obd_device *obd = data;

	LASSERT(obd != NULL);
	*eof = 1;
	return snprintf(page, count, "%s\n", obd->obd_uuid.uuid);
}
EXPORT_SYMBOL(lprocfs_rd_uuid);

int lprocfs_rd_name(char *page, char **start, off_t off, int count,
		    int *eof, void *data)
{
	struct obd_device *dev = data;

	LASSERT(dev != NULL);
	*eof = 1;
	return snprintf(page, count, "%s\n", dev->obd_name);
}
EXPORT_SYMBOL(lprocfs_rd_name);

int lprocfs_rd_blksize(char *page, char **start, off_t off, int count,
		       int *eof, void *data)
{
	struct obd_device *obd = data;
	struct obd_statfs  osfs;
	int rc = obd_statfs(NULL, obd->obd_self_export, &osfs,
			    cfs_time_shift_64(-OBD_STATFS_CACHE_SECONDS),
			    OBD_STATFS_NODELAY);
	if (!rc) {
		*eof = 1;
		rc = snprintf(page, count, "%u\n", osfs.os_bsize);
	}
	return rc;
}
EXPORT_SYMBOL(lprocfs_rd_blksize);

int lprocfs_rd_kbytestotal(char *page, char **start, off_t off, int count,
			   int *eof, void *data)
{
	struct obd_device *obd = data;
	struct obd_statfs  osfs;
	int rc = obd_statfs(NULL, obd->obd_self_export, &osfs,
			    cfs_time_shift_64(-OBD_STATFS_CACHE_SECONDS),
			    OBD_STATFS_NODELAY);
	if (!rc) {
		__u32 blk_size = osfs.os_bsize >> 10;
		__u64 result = osfs.os_blocks;

		while (blk_size >>= 1)
			result <<= 1;

		*eof = 1;
		rc = snprintf(page, count, LPU64"\n", result);
	}
	return rc;
}
EXPORT_SYMBOL(lprocfs_rd_kbytestotal);

int lprocfs_rd_kbytesfree(char *page, char **start, off_t off, int count,
			  int *eof, void *data)
{
	struct obd_device *obd = data;
	struct obd_statfs  osfs;
	int rc = obd_statfs(NULL, obd->obd_self_export, &osfs,
			    cfs_time_shift_64(-OBD_STATFS_CACHE_SECONDS),
			    OBD_STATFS_NODELAY);
	if (!rc) {
		__u32 blk_size = osfs.os_bsize >> 10;
		__u64 result = osfs.os_bfree;

		while (blk_size >>= 1)
			result <<= 1;

		*eof = 1;
		rc = snprintf(page, count, LPU64"\n", result);
	}
	return rc;
}
EXPORT_SYMBOL(lprocfs_rd_kbytesfree);

int lprocfs_rd_kbytesavail(char *page, char **start, off_t off, int count,
			   int *eof, void *data)
{
	struct obd_device *obd = data;
	struct obd_statfs  osfs;
	int rc = obd_statfs(NULL, obd->obd_self_export, &osfs,
			    cfs_time_shift_64(-OBD_STATFS_CACHE_SECONDS),
			    OBD_STATFS_NODELAY);
	if (!rc) {
		__u32 blk_size = osfs.os_bsize >> 10;
		__u64 result = osfs.os_bavail;

		while (blk_size >>= 1)
			result <<= 1;

		*eof = 1;
		rc = snprintf(page, count, LPU64"\n", result);
	}
	return rc;
}
EXPORT_SYMBOL(lprocfs_rd_kbytesavail);

int lprocfs_rd_filestotal(char *page, char **start, off_t off, int count,
			  int *eof, void *data)
{
	struct obd_device *obd = data;
	struct obd_statfs  osfs;
	int rc = obd_statfs(NULL, obd->obd_self_export, &osfs,
			    cfs_time_shift_64(-OBD_STATFS_CACHE_SECONDS),
			    OBD_STATFS_NODELAY);
	if (!rc) {
		*eof = 1;
		rc = snprintf(page, count, LPU64"\n", osfs.os_files);
	}

	return rc;
}
EXPORT_SYMBOL(lprocfs_rd_filestotal);

int lprocfs_rd_filesfree(char *page, char **start, off_t off, int count,
			 int *eof, void *data)
{
	struct obd_device *obd = data;
	struct obd_statfs  osfs;
	int rc = obd_statfs(NULL, obd->obd_self_export, &osfs,
			    cfs_time_shift_64(-OBD_STATFS_CACHE_SECONDS),
			    OBD_STATFS_NODELAY);
	if (!rc) {
		*eof = 1;
		rc = snprintf(page, count, LPU64"\n", osfs.os_ffree);
	}
	return rc;
}
EXPORT_SYMBOL(lprocfs_rd_filesfree);

int lprocfs_rd_server_uuid(char *page, char **start, off_t off, int count,
			   int *eof, void *data)
{
	struct obd_device *obd = data;
	struct obd_import *imp;
	char *imp_state_name = NULL;
	int rc = 0;

	LASSERT(obd != NULL);
	LPROCFS_CLIMP_CHECK(obd);
	imp = obd->u.cli.cl_import;
	imp_state_name = ptlrpc_import_state_name(imp->imp_state);
	*eof = 1;
	rc = snprintf(page, count, "%s\t%s%s\n",
		      obd2cli_tgt(obd), imp_state_name,
		      imp->imp_deactive ? "\tDEACTIVATED" : "");

	LPROCFS_CLIMP_EXIT(obd);
	return rc;
}
EXPORT_SYMBOL(lprocfs_rd_server_uuid);

int lprocfs_rd_conn_uuid(char *page, char **start, off_t off, int count,
			 int *eof,  void *data)
{
	struct obd_device *obd = data;
	struct ptlrpc_connection *conn;
	int rc = 0;

	LASSERT(obd != NULL);

	LPROCFS_CLIMP_CHECK(obd);
	conn = obd->u.cli.cl_import->imp_connection;
	*eof = 1;
	if (conn && obd->u.cli.cl_import) {
		rc = snprintf(page, count, "%s\n",
			      conn->c_remote_uuid.uuid);
	} else {
		rc = snprintf(page, count, "%s\n", "<none>");
	}

	LPROCFS_CLIMP_EXIT(obd);
	return rc;
}
EXPORT_SYMBOL(lprocfs_rd_conn_uuid);

/** add up per-cpu counters */
void lprocfs_stats_collect(struct lprocfs_stats *stats, int idx,
			   struct lprocfs_counter *cnt)
{
	unsigned int			num_entry;
	struct lprocfs_counter		*percpu_cntr;
	struct lprocfs_counter_header	*cntr_header;
	int				i;
	unsigned long			flags = 0;

	memset(cnt, 0, sizeof(*cnt));

	if (stats == NULL) {
		/* set count to 1 to avoid divide-by-zero errs in callers */
		cnt->lc_count = 1;
		return;
	}

	cnt->lc_min = LC_MIN_INIT;

	num_entry = lprocfs_stats_lock(stats, LPROCFS_GET_NUM_CPU, &flags);

	for (i = 0; i < num_entry; i++) {
		if (stats->ls_percpu[i] == NULL)
			continue;
		cntr_header = &stats->ls_cnt_header[idx];
		percpu_cntr = lprocfs_stats_counter_get(stats, i, idx);

		cnt->lc_count += percpu_cntr->lc_count;
		cnt->lc_sum += percpu_cntr->lc_sum;
		if (percpu_cntr->lc_min < cnt->lc_min)
			cnt->lc_min = percpu_cntr->lc_min;
		if (percpu_cntr->lc_max > cnt->lc_max)
			cnt->lc_max = percpu_cntr->lc_max;
		cnt->lc_sumsquare += percpu_cntr->lc_sumsquare;
	}

	lprocfs_stats_unlock(stats, LPROCFS_GET_NUM_CPU, &flags);
}
EXPORT_SYMBOL(lprocfs_stats_collect);

/**
 * Append a space separated list of current set flags to str.
 */
#define flag2str(flag) \
	if (imp->imp_##flag && max - len > 0) \
	     len += snprintf(str + len, max - len, "%s" #flag, len ? ", " : "");
static int obd_import_flags2str(struct obd_import *imp, char *str, int max)
{
	int len = 0;

	if (imp->imp_obd->obd_no_recov)
		len += snprintf(str, max - len, "no_recov");

	flag2str(invalid);
	flag2str(deactive);
	flag2str(replayable);
	flag2str(pingable);
	return len;
}
#undef flags2str

static const char *obd_connect_names[] = {
	"read_only",
	"lov_index",
	"unused",
	"write_grant",
	"server_lock",
	"version",
	"request_portal",
	"acl",
	"xattr",
	"create_on_write",
	"truncate_lock",
	"initial_transno",
	"inode_bit_locks",
	"join_file(obsolete)",
	"getattr_by_fid",
	"no_oh_for_devices",
	"remote_client",
	"remote_client_by_force",
	"max_byte_per_rpc",
	"64bit_qdata",
	"mds_capability",
	"oss_capability",
	"early_lock_cancel",
	"som",
	"adaptive_timeouts",
	"lru_resize",
	"mds_mds_connection",
	"real_conn",
	"change_qunit_size",
	"alt_checksum_algorithm",
	"fid_is_enabled",
	"version_recovery",
	"pools",
	"grant_shrink",
	"skip_orphan",
	"large_ea",
	"full20",
	"layout_lock",
	"64bithash",
	"object_max_bytes",
	"imp_recov",
	"jobstats",
	"umask",
	"einprogress",
	"grant_param",
	"flock_owner",
	"lvb_type",
	"nanoseconds_times",
	"lightweight_conn",
	"short_io",
	"pingless",
	"unknown",
	NULL
};

int obd_connect_flags2str(char *page, int count, __u64 flags, char *sep)
{
	__u64 mask = 1;
	int i, ret = 0;

	for (i = 0; obd_connect_names[i] != NULL; i++, mask <<= 1) {
		if (flags & mask)
			ret += snprintf(page + ret, count - ret, "%s%s",
					ret ? sep : "", obd_connect_names[i]);
	}
	if (flags & ~(mask - 1))
		ret += snprintf(page + ret, count - ret,
				"%sunknown flags "LPX64,
				ret ? sep : "", flags & ~(mask - 1));
	return ret;
}
EXPORT_SYMBOL(obd_connect_flags2str);

int lprocfs_rd_import(char *page, char **start, off_t off, int count,
		      int *eof, void *data)
{
	struct lprocfs_counter		ret;
	struct lprocfs_counter_header	*header;
	struct obd_device		*obd	= (struct obd_device *)data;
	struct obd_import		*imp;
	struct obd_import_conn		*conn;
	int				i;
	int				j;
	int				k;
	int				rw	= 0;

	LASSERT(obd != NULL);
	LPROCFS_CLIMP_CHECK(obd);
	imp = obd->u.cli.cl_import;
	*eof = 1;

	i = snprintf(page, count,
		     "import:\n"
		     "    name: %s\n"
		     "    target: %s\n"
		     "    state: %s\n"
		     "    instance: %u\n"
		     "    connect_flags: [",
		     obd->obd_name,
		     obd2cli_tgt(obd),
		     ptlrpc_import_state_name(imp->imp_state),
		     imp->imp_connect_data.ocd_instance);
	i += obd_connect_flags2str(page + i, count - i,
				   imp->imp_connect_data.ocd_connect_flags,
				   ", ");
	i += snprintf(page + i, count - i,
		      "]\n"
		      "    import_flags: [");
	i += obd_import_flags2str(imp, page + i, count - i);

	i += snprintf(page + i, count - i,
		      "]\n"
		      "    connection:\n"
		      "       failover_nids: [");
	spin_lock(&imp->imp_lock);
	j = 0;
	list_for_each_entry(conn, &imp->imp_conn_list, oic_item) {
		i += snprintf(page + i, count - i, "%s%s", j ? ", " : "",
			      libcfs_nid2str(conn->oic_conn->c_peer.nid));
		j++;
	}
	i += snprintf(page + i, count - i,
		      "]\n"
		      "       current_connection: %s\n"
		      "       connection_attempts: %u\n"
		      "       generation: %u\n"
		      "       in-progress_invalidations: %u\n",
		      imp->imp_connection == NULL ? "<none>" :
			      libcfs_nid2str(imp->imp_connection->c_peer.nid),
		      imp->imp_conn_cnt,
		      imp->imp_generation,
		      atomic_read(&imp->imp_inval_count));
	spin_unlock(&imp->imp_lock);

	if (obd->obd_svc_stats == NULL)
		goto out_climp;

	header = &obd->obd_svc_stats->ls_cnt_header[PTLRPC_REQWAIT_CNTR];
	lprocfs_stats_collect(obd->obd_svc_stats, PTLRPC_REQWAIT_CNTR, &ret);
	if (ret.lc_count != 0) {
		/* first argument to do_div MUST be __u64 */
		__u64 sum = ret.lc_sum;
		do_div(sum, ret.lc_count);
		ret.lc_sum = sum;
	} else
		ret.lc_sum = 0;
	i += snprintf(page + i, count - i,
		      "    rpcs:\n"
		      "       inflight: %u\n"
		      "       unregistering: %u\n"
		      "       timeouts: %u\n"
		      "       avg_waittime: "LPU64" %s\n",
		      atomic_read(&imp->imp_inflight),
		      atomic_read(&imp->imp_unregistering),
		      atomic_read(&imp->imp_timeouts),
		      ret.lc_sum, header->lc_units);

	k = 0;
	for(j = 0; j < IMP_AT_MAX_PORTALS; j++) {
		if (imp->imp_at.iat_portal[j] == 0)
			break;
		k = max_t(unsigned int, k,
			  at_get(&imp->imp_at.iat_service_estimate[j]));
	}
	i += snprintf(page + i, count - i,
		      "    service_estimates:\n"
		      "       services: %u sec\n"
		      "       network: %u sec\n",
		      k,
		      at_get(&imp->imp_at.iat_net_latency));

	i += snprintf(page + i, count - i,
		      "    transactions:\n"
		      "       last_replay: "LPU64"\n"
		      "       peer_committed: "LPU64"\n"
		      "       last_checked: "LPU64"\n",
		      imp->imp_last_replay_transno,
		      imp->imp_peer_committed_transno,
		      imp->imp_last_transno_checked);

	/* avg data rates */
	for (rw = 0; rw <= 1; rw++) {
		lprocfs_stats_collect(obd->obd_svc_stats,
				      PTLRPC_LAST_CNTR + BRW_READ_BYTES + rw,
				      &ret);
		if (ret.lc_sum > 0 && ret.lc_count > 0) {
			/* first argument to do_div MUST be __u64 */
			__u64 sum = ret.lc_sum;
			do_div(sum, ret.lc_count);
			ret.lc_sum = sum;
			i += snprintf(page + i, count - i,
				      "    %s_data_averages:\n"
				      "       bytes_per_rpc: "LPU64"\n",
				      rw ? "write" : "read",
				      ret.lc_sum);
		}
		k = (int)ret.lc_sum;
		j = opcode_offset(OST_READ + rw) + EXTRA_MAX_OPCODES;
		header = &obd->obd_svc_stats->ls_cnt_header[j];
		lprocfs_stats_collect(obd->obd_svc_stats, j, &ret);
		if (ret.lc_sum > 0 && ret.lc_count != 0) {
			/* first argument to do_div MUST be __u64 */
			__u64 sum = ret.lc_sum;
			do_div(sum, ret.lc_count);
			ret.lc_sum = sum;
			i += snprintf(page + i, count - i,
				      "       %s_per_rpc: "LPU64"\n",
				      header->lc_units, ret.lc_sum);
			j = (int)ret.lc_sum;
			if (j > 0)
				i += snprintf(page + i, count - i,
					      "       MB_per_sec: %u.%.02u\n",
					      k / j, (100 * k / j) % 100);
		}
	}

out_climp:
	LPROCFS_CLIMP_EXIT(obd);
	return i;
}
EXPORT_SYMBOL(lprocfs_rd_import);

int lprocfs_rd_state(char *page, char **start, off_t off, int count,
		      int *eof, void *data)
{
	struct obd_device *obd = (struct obd_device *)data;
	struct obd_import *imp;
	int i, j, k;

	LASSERT(obd != NULL);
	LPROCFS_CLIMP_CHECK(obd);
	imp = obd->u.cli.cl_import;
	*eof = 1;

	i = snprintf(page, count, "current_state: %s\n",
		     ptlrpc_import_state_name(imp->imp_state));
	i += snprintf(page + i, count - i,
		      "state_history:\n");
	k = imp->imp_state_hist_idx;
	for (j = 0; j < IMP_STATE_HIST_LEN; j++) {
		struct import_state_hist *ish =
			&imp->imp_state_hist[(k + j) % IMP_STATE_HIST_LEN];
		if (ish->ish_state == 0)
			continue;
		i += snprintf(page + i, count - i, " - ["CFS_TIME_T", %s]\n",
			      ish->ish_time,
			      ptlrpc_import_state_name(ish->ish_state));
	}

	LPROCFS_CLIMP_EXIT(obd);
	return i;
}
EXPORT_SYMBOL(lprocfs_rd_state);

int lprocfs_at_hist_helper(char *page, int count, int rc,
			   struct adaptive_timeout *at)
{
	int i;
	for (i = 0; i < AT_BINS; i++)
		rc += snprintf(page + rc, count - rc, "%3u ", at->at_hist[i]);
	rc += snprintf(page + rc, count - rc, "\n");
	return rc;
}
EXPORT_SYMBOL(lprocfs_at_hist_helper);

/* See also ptlrpc_lprocfs_rd_timeouts */
int lprocfs_rd_timeouts(char *page, char **start, off_t off, int count,
			int *eof, void *data)
{
	struct obd_device *obd = (struct obd_device *)data;
	struct obd_import *imp;
	unsigned int cur, worst;
	time_t now, worstt;
	struct dhms ts;
	int i, rc = 0;

	LASSERT(obd != NULL);
	LPROCFS_CLIMP_CHECK(obd);
	imp = obd->u.cli.cl_import;
	*eof = 1;

	now = cfs_time_current_sec();

	/* Some network health info for kicks */
	s2dhms(&ts, now - imp->imp_last_reply_time);
	rc += snprintf(page + rc, count - rc,
		       "%-10s : %ld, "DHMS_FMT" ago\n",
		       "last reply", imp->imp_last_reply_time, DHMS_VARS(&ts));

	cur = at_get(&imp->imp_at.iat_net_latency);
	worst = imp->imp_at.iat_net_latency.at_worst_ever;
	worstt = imp->imp_at.iat_net_latency.at_worst_time;
	s2dhms(&ts, now - worstt);
	rc += snprintf(page + rc, count - rc,
		       "%-10s : cur %3u  worst %3u (at %ld, "DHMS_FMT" ago) ",
		       "network", cur, worst, worstt, DHMS_VARS(&ts));
	rc = lprocfs_at_hist_helper(page, count, rc,
				    &imp->imp_at.iat_net_latency);

	for(i = 0; i < IMP_AT_MAX_PORTALS; i++) {
		if (imp->imp_at.iat_portal[i] == 0)
			break;
		cur = at_get(&imp->imp_at.iat_service_estimate[i]);
		worst = imp->imp_at.iat_service_estimate[i].at_worst_ever;
		worstt = imp->imp_at.iat_service_estimate[i].at_worst_time;
		s2dhms(&ts, now - worstt);
		rc += snprintf(page + rc, count - rc,
			       "portal %-2d  : cur %3u  worst %3u (at %ld, "
			       DHMS_FMT" ago) ", imp->imp_at.iat_portal[i],
			       cur, worst, worstt, DHMS_VARS(&ts));
		rc = lprocfs_at_hist_helper(page, count, rc,
					  &imp->imp_at.iat_service_estimate[i]);
	}

	LPROCFS_CLIMP_EXIT(obd);
	return rc;
}
EXPORT_SYMBOL(lprocfs_rd_timeouts);

int lprocfs_rd_connect_flags(char *page, char **start, off_t off,
			     int count, int *eof, void *data)
{
	struct obd_device *obd = data;
	__u64 flags;
	int ret = 0;

	LPROCFS_CLIMP_CHECK(obd);
	flags = obd->u.cli.cl_import->imp_connect_data.ocd_connect_flags;
	ret = snprintf(page, count, "flags="LPX64"\n", flags);
	ret += obd_connect_flags2str(page + ret, count - ret, flags, "\n");
	ret += snprintf(page + ret, count - ret, "\n");
	LPROCFS_CLIMP_EXIT(obd);
	return ret;
}
EXPORT_SYMBOL(lprocfs_rd_connect_flags);

int lprocfs_rd_num_exports(char *page, char **start, off_t off, int count,
			   int *eof,  void *data)
{
	struct obd_device *obd = data;

	LASSERT(obd != NULL);
	*eof = 1;
	return snprintf(page, count, "%u\n", obd->obd_num_exports);
}
EXPORT_SYMBOL(lprocfs_rd_num_exports);

int lprocfs_rd_numrefs(char *page, char **start, off_t off, int count,
		       int *eof, void *data)
{
	struct obd_type *class = (struct obd_type*) data;

	LASSERT(class != NULL);
	*eof = 1;
	return snprintf(page, count, "%d\n", class->typ_refcnt);
}
EXPORT_SYMBOL(lprocfs_rd_numrefs);

int lprocfs_obd_setup(struct obd_device *obd, struct lprocfs_vars *list)
{
	int rc = 0;

	LASSERT(obd != NULL);
	LASSERT(obd->obd_magic == OBD_DEVICE_MAGIC);
	LASSERT(obd->obd_type->typ_procroot != NULL);

	obd->obd_proc_entry = lprocfs_register(obd->obd_name,
					       obd->obd_type->typ_procroot,
					       list, obd);
	if (IS_ERR(obd->obd_proc_entry)) {
		rc = PTR_ERR(obd->obd_proc_entry);
		CERROR("error %d setting up lprocfs for %s\n",rc,obd->obd_name);
		obd->obd_proc_entry = NULL;
	}
	return rc;
}
EXPORT_SYMBOL(lprocfs_obd_setup);

int lprocfs_obd_cleanup(struct obd_device *obd)
{
	if (!obd)
		return -EINVAL;
	if (obd->obd_proc_exports_entry) {
		/* Should be no exports left */
		LASSERT(obd->obd_proc_exports_entry->subdir == NULL);
		lprocfs_remove(&obd->obd_proc_exports_entry);
		obd->obd_proc_exports_entry = NULL;
	}
	if (obd->obd_proc_entry) {
		lprocfs_remove(&obd->obd_proc_entry);
		obd->obd_proc_entry = NULL;
	}
	return 0;
}
EXPORT_SYMBOL(lprocfs_obd_cleanup);

static void lprocfs_free_client_stats(struct nid_stat *client_stat)
{
	CDEBUG(D_CONFIG, "stat %p - data %p/%p\n", client_stat,
	       client_stat->nid_proc, client_stat->nid_stats);

	LASSERTF(atomic_read(&client_stat->nid_exp_ref_count) == 0,
		 "nid %s:count %d\n", libcfs_nid2str(client_stat->nid),
		 atomic_read(&client_stat->nid_exp_ref_count));

	if (client_stat->nid_proc)
		lprocfs_remove(&client_stat->nid_proc);

	if (client_stat->nid_stats)
		lprocfs_free_stats(&client_stat->nid_stats);

	if (client_stat->nid_ldlm_stats)
		lprocfs_free_stats(&client_stat->nid_ldlm_stats);

	OBD_FREE_PTR(client_stat);
	return;

}

void lprocfs_free_per_client_stats(struct obd_device *obd)
{
	cfs_hash_t *hash = obd->obd_nid_stats_hash;
	struct nid_stat *stat;
	ENTRY;

	/* we need extra list - because hash_exit called to early */
	/* not need locking because all clients is died */
	while (!list_empty(&obd->obd_nid_stats)) {
		stat = list_entry(obd->obd_nid_stats.next,
				      struct nid_stat, nid_list);
		list_del_init(&stat->nid_list);
		cfs_hash_del(hash, &stat->nid, &stat->nid_hash);
		lprocfs_free_client_stats(stat);
	}
	EXIT;
}
EXPORT_SYMBOL(lprocfs_free_per_client_stats);

struct lprocfs_stats *lprocfs_alloc_stats(unsigned int num,
					  enum lprocfs_stats_flags flags)
{
	struct lprocfs_stats	*stats;
	unsigned int		num_entry;
	unsigned int		percpusize = 0;
	int			i;

	if (num == 0)
		return NULL;

	if (lprocfs_no_percpu_stats != 0)
		flags |= LPROCFS_STATS_FLAG_NOPERCPU;

	if (flags & LPROCFS_STATS_FLAG_NOPERCPU)
		num_entry = 1;
	else
		num_entry = num_possible_cpus();

	/* alloc percpu pointers for all possible cpu slots */
	LIBCFS_ALLOC(stats, offsetof(typeof(*stats), ls_percpu[num_entry]));
	if (stats == NULL)
		return NULL;

	stats->ls_num = num;
	stats->ls_flags = flags;
	spin_lock_init(&stats->ls_lock);

	/* alloc num of counter headers */
	LIBCFS_ALLOC(stats->ls_cnt_header,
		     stats->ls_num * sizeof(struct lprocfs_counter_header));
	if (stats->ls_cnt_header == NULL)
		goto fail;

	if ((flags & LPROCFS_STATS_FLAG_NOPERCPU) != 0) {
		/* contains only one set counters */
		percpusize = lprocfs_stats_counter_size(stats);
		LIBCFS_ALLOC_ATOMIC(stats->ls_percpu[0], percpusize);
		if (stats->ls_percpu[0] == NULL)
			goto fail;
		stats->ls_biggest_alloc_num = 1;
	} else if ((flags & LPROCFS_STATS_FLAG_IRQ_SAFE) != 0) {
		/* alloc all percpu data, currently only obd_memory use this */
		for (i = 0; i < num_entry; ++i)
			if (lprocfs_stats_alloc_one(stats, i) < 0)
				goto fail;
	}

	return stats;

fail:
	lprocfs_free_stats(&stats);
	return NULL;
}
EXPORT_SYMBOL(lprocfs_alloc_stats);

void lprocfs_free_stats(struct lprocfs_stats **statsh)
{
	struct lprocfs_stats *stats = *statsh;
	unsigned int num_entry;
	unsigned int percpusize;
	unsigned int i;

	if (stats == NULL || stats->ls_num == 0)
		return;
	*statsh = NULL;

	if (stats->ls_flags & LPROCFS_STATS_FLAG_NOPERCPU)
		num_entry = 1;
	else
		num_entry = num_possible_cpus();

	percpusize = lprocfs_stats_counter_size(stats);
	for (i = 0; i < num_entry; i++)
		if (stats->ls_percpu[i] != NULL)
			LIBCFS_FREE(stats->ls_percpu[i], percpusize);
	if (stats->ls_cnt_header != NULL)
		LIBCFS_FREE(stats->ls_cnt_header, stats->ls_num *
					sizeof(struct lprocfs_counter_header));
	LIBCFS_FREE(stats, offsetof(typeof(*stats), ls_percpu[num_entry]));
}
EXPORT_SYMBOL(lprocfs_free_stats);

void lprocfs_clear_stats(struct lprocfs_stats *stats)
{
	struct lprocfs_counter		*percpu_cntr;
	struct lprocfs_counter_header	*header;
	int				i;
	int				j;
	unsigned int			num_entry;
	unsigned long			flags = 0;

	num_entry = lprocfs_stats_lock(stats, LPROCFS_GET_NUM_CPU, &flags);

	for (i = 0; i < num_entry; i++) {
		if (stats->ls_percpu[i] == NULL)
			continue;
		for (j = 0; j < stats->ls_num; j++) {
			header = &stats->ls_cnt_header[j];
			percpu_cntr = lprocfs_stats_counter_get(stats, i, j);
			percpu_cntr->lc_count		= 0;
			percpu_cntr->lc_min		= LC_MIN_INIT;
			percpu_cntr->lc_max		= 0;
			percpu_cntr->lc_sumsquare	= 0;
			percpu_cntr->lc_sum		= 0;
			if (stats->ls_flags & LPROCFS_STATS_FLAG_IRQ_SAFE)
				percpu_cntr->lc_sum_irq	= 0;
		}
	}

	lprocfs_stats_unlock(stats, LPROCFS_GET_NUM_CPU, &flags);
}
EXPORT_SYMBOL(lprocfs_clear_stats);

static ssize_t lprocfs_stats_seq_write(struct file *file, const char *buf,
				       size_t len, loff_t *off)
{
	struct seq_file *seq = file->private_data;
	struct lprocfs_stats *stats = seq->private;

	lprocfs_clear_stats(stats);

	return len;
}

static void *lprocfs_stats_seq_start(struct seq_file *p, loff_t *pos)
{
	struct lprocfs_stats *stats = p->private;
	/* return 1st cpu location */
	return (*pos >= stats->ls_num) ? NULL :
		lprocfs_stats_counter_get(stats, 0, *pos);
}

static void lprocfs_stats_seq_stop(struct seq_file *p, void *v)
{
}

static void *lprocfs_stats_seq_next(struct seq_file *p, void *v, loff_t *pos)
{
	struct lprocfs_stats *stats = p->private;
	++*pos;
	return (*pos >= stats->ls_num) ? NULL :
		lprocfs_stats_counter_get(stats, 0, *pos);
}

/* seq file export of one lprocfs counter */
static int lprocfs_stats_seq_show(struct seq_file *p, void *v)
{
	struct lprocfs_stats		*stats	= p->private;
	struct lprocfs_counter		*cntr	= v;
	struct lprocfs_counter		ret;
	struct lprocfs_counter_header	*header;
	int				entry_size;
	int				idx;
	int				rc	= 0;

	if (cntr == &(stats->ls_percpu[0])->lp_cntr[0]) {
		struct timeval now;
		do_gettimeofday(&now);
		rc = seq_printf(p, "%-25s %lu.%lu secs.usecs\n",
				"snapshot_time", now.tv_sec, now.tv_usec);
		if (rc < 0)
			return rc;
	}
	entry_size = sizeof(*cntr);
	if (stats->ls_flags & LPROCFS_STATS_FLAG_IRQ_SAFE)
		entry_size += sizeof(__s64);
	idx = ((void *)cntr - (void *)&(stats->ls_percpu[0])->lp_cntr[0]) /
		entry_size;

	header = &stats->ls_cnt_header[idx];
	lprocfs_stats_collect(stats, idx, &ret);

	if (ret.lc_count == 0)
		goto out;

	rc = seq_printf(p, "%-25s "LPD64" samples [%s]", header->lc_name,
			ret.lc_count, header->lc_units);

	if (rc < 0)
		goto out;

	if ((header->lc_config & LPROCFS_CNTR_AVGMINMAX) &&
	    (ret.lc_count > 0)) {
		rc = seq_printf(p, " "LPD64" "LPD64" "LPD64,
				ret.lc_min, ret.lc_max, ret.lc_sum);
		if (rc < 0)
			goto out;
		if (header->lc_config & LPROCFS_CNTR_STDDEV)
			rc = seq_printf(p, " "LPD64, ret.lc_sumsquare);
		if (rc < 0)
			goto out;
	}
	rc = seq_printf(p, "\n");
 out:
	return (rc < 0) ? rc : 0;
}

struct seq_operations lprocfs_stats_seq_sops = {
	start: lprocfs_stats_seq_start,
	stop:  lprocfs_stats_seq_stop,
	next:  lprocfs_stats_seq_next,
	show:  lprocfs_stats_seq_show,
};

static int lprocfs_stats_seq_open(struct inode *inode, struct file *file)
{
	struct proc_dir_entry *dp = PDE(inode);
	struct seq_file *seq;
	int rc;

	if (LPROCFS_ENTRY_AND_CHECK(dp))
		return -ENOENT;

	rc = seq_open(file, &lprocfs_stats_seq_sops);
	if (rc) {
		LPROCFS_EXIT();
		return rc;
	}
	seq = file->private_data;
	seq->private = dp->data;
	return 0;
}

struct file_operations lprocfs_stats_seq_fops = {
	.owner   = THIS_MODULE,
	.open    = lprocfs_stats_seq_open,
	.read    = seq_read,
	.write   = lprocfs_stats_seq_write,
	.llseek  = seq_lseek,
	.release = lprocfs_seq_release,
};

int lprocfs_register_stats(struct proc_dir_entry *root, const char *name,
			   struct lprocfs_stats *stats)
{
	struct proc_dir_entry *entry;
	LASSERT(root != NULL);

	LPROCFS_WRITE_ENTRY();
	entry = create_proc_entry(name, 0644, root);
	if (entry) {
		entry->proc_fops = &lprocfs_stats_seq_fops;
		entry->data = stats;
	}

	LPROCFS_WRITE_EXIT();

	if (entry == NULL)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL(lprocfs_register_stats);

void lprocfs_counter_init(struct lprocfs_stats *stats, int index,
			  unsigned conf, const char *name, const char *units)
{
	struct lprocfs_counter_header	*header;
	struct lprocfs_counter		*percpu_cntr;
	unsigned long			flags = 0;
	unsigned int			i;
	unsigned int			num_cpu;

	LASSERT(stats != NULL);

	header = &stats->ls_cnt_header[index];
	LASSERTF(header != NULL, "Failed to allocate stats header:[%d]%s/%s\n",
		 index, name, units);

	header->lc_config = conf;
	header->lc_name   = name;
	header->lc_units  = units;

	num_cpu = lprocfs_stats_lock(stats, LPROCFS_GET_NUM_CPU, &flags);
	for (i = 0; i < num_cpu; ++i) {
		if (stats->ls_percpu[i] == NULL)
			continue;
		percpu_cntr = lprocfs_stats_counter_get(stats, i, index);
		percpu_cntr->lc_count		= 0;
		percpu_cntr->lc_min		= LC_MIN_INIT;
		percpu_cntr->lc_max		= 0;
		percpu_cntr->lc_sumsquare	= 0;
		percpu_cntr->lc_sum		= 0;
		if ((stats->ls_flags & LPROCFS_STATS_FLAG_IRQ_SAFE) != 0)
			percpu_cntr->lc_sum_irq	= 0;
	}
	lprocfs_stats_unlock(stats, LPROCFS_GET_NUM_CPU, &flags);
}
EXPORT_SYMBOL(lprocfs_counter_init);

#define LPROCFS_OBD_OP_INIT(base, stats, op)			       \
do {								       \
	unsigned int coffset = base + OBD_COUNTER_OFFSET(op);	      \
	LASSERT(coffset < stats->ls_num);				  \
	lprocfs_counter_init(stats, coffset, 0, #op, "reqs");	      \
} while (0)

void lprocfs_init_ops_stats(int num_private_stats, struct lprocfs_stats *stats)
{
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, iocontrol);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, get_info);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, set_info_async);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, attach);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, detach);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, setup);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, precleanup);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, cleanup);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, process_config);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, postrecov);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, add_conn);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, del_conn);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, connect);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, reconnect);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, disconnect);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, fid_init);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, fid_fini);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, fid_alloc);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, statfs);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, statfs_async);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, packmd);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, unpackmd);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, preallocate);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, precreate);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, create);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, create_async);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, destroy);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, setattr);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, setattr_async);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, getattr);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, getattr_async);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, brw);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, merge_lvb);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, adjust_kms);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, punch);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, sync);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, migrate);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, copy);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, iterate);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, preprw);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, commitrw);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, enqueue);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, change_cbdata);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, find_cbdata);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, cancel);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, cancel_unused);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, init_export);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, destroy_export);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, extent_calc);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, llog_init);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, llog_connect);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, llog_finish);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, pin);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, unpin);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, import_event);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, notify);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, health_check);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, get_uuid);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, quotacheck);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, quotactl);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, ping);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, pool_new);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, pool_rem);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, pool_add);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, pool_del);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, getref);
	LPROCFS_OBD_OP_INIT(num_private_stats, stats, putref);
}
EXPORT_SYMBOL(lprocfs_init_ops_stats);

int lprocfs_alloc_obd_stats(struct obd_device *obd, unsigned num_private_stats)
{
	struct lprocfs_stats *stats;
	unsigned int num_stats;
	int rc, i;

	LASSERT(obd->obd_stats == NULL);
	LASSERT(obd->obd_proc_entry != NULL);
	LASSERT(obd->obd_cntr_base == 0);

	num_stats = ((int)sizeof(*obd->obd_type->typ_dt_ops) / sizeof(void *)) +
		num_private_stats - 1 /* o_owner */;
	stats = lprocfs_alloc_stats(num_stats, 0);
	if (stats == NULL)
		return -ENOMEM;

	lprocfs_init_ops_stats(num_private_stats, stats);

	for (i = num_private_stats; i < num_stats; i++) {
		/* If this LBUGs, it is likely that an obd
		 * operation was added to struct obd_ops in
		 * <obd.h>, and that the corresponding line item
		 * LPROCFS_OBD_OP_INIT(.., .., opname)
		 * is missing from the list above. */
		LASSERTF(stats->ls_cnt_header[i].lc_name != NULL,
			 "Missing obd_stat initializer obd_op "
			 "operation at offset %d.\n", i - num_private_stats);
	}
	rc = lprocfs_register_stats(obd->obd_proc_entry, "stats", stats);
	if (rc < 0) {
		lprocfs_free_stats(&stats);
	} else {
		obd->obd_stats  = stats;
		obd->obd_cntr_base = num_private_stats;
	}
	return rc;
}
EXPORT_SYMBOL(lprocfs_alloc_obd_stats);

void lprocfs_free_obd_stats(struct obd_device *obd)
{
	if (obd->obd_stats)
		lprocfs_free_stats(&obd->obd_stats);
}
EXPORT_SYMBOL(lprocfs_free_obd_stats);

#define LPROCFS_MD_OP_INIT(base, stats, op)			     \
do {								    \
	unsigned int coffset = base + MD_COUNTER_OFFSET(op);	    \
	LASSERT(coffset < stats->ls_num);			       \
	lprocfs_counter_init(stats, coffset, 0, #op, "reqs");	   \
} while (0)

void lprocfs_init_mps_stats(int num_private_stats, struct lprocfs_stats *stats)
{
	LPROCFS_MD_OP_INIT(num_private_stats, stats, getstatus);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, null_inode);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, find_cbdata);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, close);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, create);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, done_writing);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, enqueue);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, getattr);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, getattr_name);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, intent_lock);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, link);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, rename);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, is_subdir);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, setattr);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, sync);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, readpage);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, unlink);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, setxattr);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, getxattr);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, init_ea_size);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, get_lustre_md);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, free_lustre_md);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, set_open_replay_data);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, clear_open_replay_data);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, set_lock_data);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, lock_match);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, cancel_unused);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, renew_capa);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, unpack_capa);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, get_remote_perm);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, intent_getattr_async);
	LPROCFS_MD_OP_INIT(num_private_stats, stats, revalidate_lock);
}
EXPORT_SYMBOL(lprocfs_init_mps_stats);

int lprocfs_alloc_md_stats(struct obd_device *obd,
			   unsigned num_private_stats)
{
	struct lprocfs_stats *stats;
	unsigned int num_stats;
	int rc, i;

	LASSERT(obd->md_stats == NULL);
	LASSERT(obd->obd_proc_entry != NULL);
	LASSERT(obd->md_cntr_base == 0);

	num_stats = 1 + MD_COUNTER_OFFSET(revalidate_lock) +
		    num_private_stats;
	stats = lprocfs_alloc_stats(num_stats, 0);
	if (stats == NULL)
		return -ENOMEM;

	lprocfs_init_mps_stats(num_private_stats, stats);

	for (i = num_private_stats; i < num_stats; i++) {
		if (stats->ls_cnt_header[i].lc_name == NULL) {
			CERROR("Missing md_stat initializer md_op "
			       "operation at offset %d. Aborting.\n",
			       i - num_private_stats);
			LBUG();
		}
	}
	rc = lprocfs_register_stats(obd->obd_proc_entry, "md_stats", stats);
	if (rc < 0) {
		lprocfs_free_stats(&stats);
	} else {
		obd->md_stats  = stats;
		obd->md_cntr_base = num_private_stats;
	}
	return rc;
}
EXPORT_SYMBOL(lprocfs_alloc_md_stats);

void lprocfs_free_md_stats(struct obd_device *obd)
{
	struct lprocfs_stats *stats = obd->md_stats;

	if (stats != NULL) {
		obd->md_stats = NULL;
		obd->md_cntr_base = 0;
		lprocfs_free_stats(&stats);
	}
}
EXPORT_SYMBOL(lprocfs_free_md_stats);

void lprocfs_init_ldlm_stats(struct lprocfs_stats *ldlm_stats)
{
	lprocfs_counter_init(ldlm_stats,
			     LDLM_ENQUEUE - LDLM_FIRST_OPC,
			     0, "ldlm_enqueue", "reqs");
	lprocfs_counter_init(ldlm_stats,
			     LDLM_CONVERT - LDLM_FIRST_OPC,
			     0, "ldlm_convert", "reqs");
	lprocfs_counter_init(ldlm_stats,
			     LDLM_CANCEL - LDLM_FIRST_OPC,
			     0, "ldlm_cancel", "reqs");
	lprocfs_counter_init(ldlm_stats,
			     LDLM_BL_CALLBACK - LDLM_FIRST_OPC,
			     0, "ldlm_bl_callback", "reqs");
	lprocfs_counter_init(ldlm_stats,
			     LDLM_CP_CALLBACK - LDLM_FIRST_OPC,
			     0, "ldlm_cp_callback", "reqs");
	lprocfs_counter_init(ldlm_stats,
			     LDLM_GL_CALLBACK - LDLM_FIRST_OPC,
			     0, "ldlm_gl_callback", "reqs");
}
EXPORT_SYMBOL(lprocfs_init_ldlm_stats);

int lprocfs_exp_rd_nid(char *page, char **start, off_t off, int count,
			 int *eof,  void *data)
{
	struct obd_export *exp = data;
	LASSERT(exp != NULL);
	*eof = 1;
	return snprintf(page, count, "%s\n", obd_export_nid2str(exp));
}

struct exp_uuid_cb_data {
	char		   *page;
	int		     count;
	int		    *eof;
	int		    *len;
};

static void
lprocfs_exp_rd_cb_data_init(struct exp_uuid_cb_data *cb_data, char *page,
			    int count, int *eof, int *len)
{
	cb_data->page = page;
	cb_data->count = count;
	cb_data->eof = eof;
	cb_data->len = len;
}

int lprocfs_exp_print_uuid(cfs_hash_t *hs, cfs_hash_bd_t *bd,
			   struct hlist_node *hnode, void *cb_data)

{
	struct obd_export *exp = cfs_hash_object(hs, hnode);
	struct exp_uuid_cb_data *data = (struct exp_uuid_cb_data *)cb_data;

	if (exp->exp_nid_stats)
		*data->len += snprintf((data->page + *data->len),
				       data->count, "%s\n",
				       obd_uuid2str(&exp->exp_client_uuid));
	return 0;
}

int lprocfs_exp_rd_uuid(char *page, char **start, off_t off, int count,
			int *eof,  void *data)
{
	struct nid_stat *stats = (struct nid_stat *)data;
	struct exp_uuid_cb_data cb_data;
	struct obd_device *obd = stats->nid_obd;
	int len = 0;

	*eof = 1;
	page[0] = '\0';
	lprocfs_exp_rd_cb_data_init(&cb_data, page, count, eof, &len);
	cfs_hash_for_each_key(obd->obd_nid_hash, &stats->nid,
			      lprocfs_exp_print_uuid, &cb_data);
	return (*cb_data.len);
}

int lprocfs_exp_print_hash(cfs_hash_t *hs, cfs_hash_bd_t *bd,
			   struct hlist_node *hnode, void *cb_data)

{
	struct exp_uuid_cb_data *data = cb_data;
	struct obd_export       *exp = cfs_hash_object(hs, hnode);

	if (exp->exp_lock_hash != NULL) {
		if (!*data->len) {
			*data->len += cfs_hash_debug_header(data->page,
							    data->count);
		}
		*data->len += cfs_hash_debug_str(hs, data->page + *data->len,
						 data->count);
	}

	return 0;
}

int lprocfs_exp_rd_hash(char *page, char **start, off_t off, int count,
			int *eof,  void *data)
{
	struct nid_stat *stats = (struct nid_stat *)data;
	struct exp_uuid_cb_data cb_data;
	struct obd_device *obd = stats->nid_obd;
	int len = 0;

	*eof = 1;
	page[0] = '\0';
	lprocfs_exp_rd_cb_data_init(&cb_data, page, count, eof, &len);

	cfs_hash_for_each_key(obd->obd_nid_hash, &stats->nid,
			      lprocfs_exp_print_hash, &cb_data);
	return (*cb_data.len);
}

int lprocfs_nid_stats_clear_read(char *page, char **start, off_t off,
					int count, int *eof,  void *data)
{
	*eof = 1;
	return snprintf(page, count, "%s\n",
			"Write into this file to clear all nid stats and "
			"stale nid entries");
}
EXPORT_SYMBOL(lprocfs_nid_stats_clear_read);

static int lprocfs_nid_stats_clear_write_cb(void *obj, void *data)
{
	struct nid_stat *stat = obj;
	ENTRY;

	CDEBUG(D_INFO,"refcnt %d\n", atomic_read(&stat->nid_exp_ref_count));
	if (atomic_read(&stat->nid_exp_ref_count) == 1) {
		/* object has only hash references. */
		spin_lock(&stat->nid_obd->obd_nid_lock);
		list_move(&stat->nid_list, data);
		spin_unlock(&stat->nid_obd->obd_nid_lock);
		RETURN(1);
	}
	/* we has reference to object - only clear data*/
	if (stat->nid_stats)
		lprocfs_clear_stats(stat->nid_stats);

	RETURN(0);
}

int lprocfs_nid_stats_clear_write(struct file *file, const char *buffer,
				  unsigned long count, void *data)
{
	struct obd_device *obd = (struct obd_device *)data;
	struct nid_stat *client_stat;
	LIST_HEAD(free_list);

	cfs_hash_cond_del(obd->obd_nid_stats_hash,
			  lprocfs_nid_stats_clear_write_cb, &free_list);

	while (!list_empty(&free_list)) {
		client_stat = list_entry(free_list.next, struct nid_stat,
					     nid_list);
		list_del_init(&client_stat->nid_list);
		lprocfs_free_client_stats(client_stat);
	}

	return count;
}
EXPORT_SYMBOL(lprocfs_nid_stats_clear_write);

int lprocfs_exp_setup(struct obd_export *exp, lnet_nid_t *nid, int *newnid)
{
	struct nid_stat *new_stat, *old_stat;
	struct obd_device *obd = NULL;
	proc_dir_entry_t *entry;
	char *buffer = NULL;
	int rc = 0;
	ENTRY;

	*newnid = 0;

	if (!exp || !exp->exp_obd || !exp->exp_obd->obd_proc_exports_entry ||
	    !exp->exp_obd->obd_nid_stats_hash)
		RETURN(-EINVAL);

	/* not test against zero because eric say:
	 * You may only test nid against another nid, or LNET_NID_ANY.
	 * Anything else is nonsense.*/
	if (!nid || *nid == LNET_NID_ANY)
		RETURN(0);

	obd = exp->exp_obd;

	CDEBUG(D_CONFIG, "using hash %p\n", obd->obd_nid_stats_hash);

	OBD_ALLOC_PTR(new_stat);
	if (new_stat == NULL)
		RETURN(-ENOMEM);

	new_stat->nid	       = *nid;
	new_stat->nid_obd	   = exp->exp_obd;
	/* we need set default refcount to 1 to balance obd_disconnect */
	atomic_set(&new_stat->nid_exp_ref_count, 1);

	old_stat = cfs_hash_findadd_unique(obd->obd_nid_stats_hash,
					   nid, &new_stat->nid_hash);
	CDEBUG(D_INFO, "Found stats %p for nid %s - ref %d\n",
	       old_stat, libcfs_nid2str(*nid),
	       atomic_read(&new_stat->nid_exp_ref_count));

	/* We need to release old stats because lprocfs_exp_cleanup() hasn't
	 * been and will never be called. */
	if (exp->exp_nid_stats) {
		nidstat_putref(exp->exp_nid_stats);
		exp->exp_nid_stats = NULL;
	}

	/* Return -EALREADY here so that we know that the /proc
	 * entry already has been created */
	if (old_stat != new_stat) {
		exp->exp_nid_stats = old_stat;
		GOTO(destroy_new, rc = -EALREADY);
	}
	/* not found - create */
	OBD_ALLOC(buffer, LNET_NIDSTR_SIZE);
	if (buffer == NULL)
		GOTO(destroy_new, rc = -ENOMEM);

	memcpy(buffer, libcfs_nid2str(*nid), LNET_NIDSTR_SIZE);
	new_stat->nid_proc = lprocfs_register(buffer,
					      obd->obd_proc_exports_entry,
					      NULL, NULL);
	OBD_FREE(buffer, LNET_NIDSTR_SIZE);

	if (new_stat->nid_proc == NULL) {
		CERROR("Error making export directory for nid %s\n",
		       libcfs_nid2str(*nid));
		GOTO(destroy_new_ns, rc = -ENOMEM);
	}

	entry = lprocfs_add_simple(new_stat->nid_proc, "uuid",
				   lprocfs_exp_rd_uuid, NULL, new_stat, NULL);
	if (IS_ERR(entry)) {
		CWARN("Error adding the NID stats file\n");
		rc = PTR_ERR(entry);
		GOTO(destroy_new_ns, rc);
	}

	entry = lprocfs_add_simple(new_stat->nid_proc, "hash",
				   lprocfs_exp_rd_hash, NULL, new_stat, NULL);
	if (IS_ERR(entry)) {
		CWARN("Error adding the hash file\n");
		rc = PTR_ERR(entry);
		GOTO(destroy_new_ns, rc);
	}

	exp->exp_nid_stats = new_stat;
	*newnid = 1;
	/* protect competitive add to list, not need locking on destroy */
	spin_lock(&obd->obd_nid_lock);
	list_add(&new_stat->nid_list, &obd->obd_nid_stats);
	spin_unlock(&obd->obd_nid_lock);

	RETURN(rc);

destroy_new_ns:
	if (new_stat->nid_proc != NULL)
		lprocfs_remove(&new_stat->nid_proc);
	cfs_hash_del(obd->obd_nid_stats_hash, nid, &new_stat->nid_hash);

destroy_new:
	nidstat_putref(new_stat);
	OBD_FREE_PTR(new_stat);
	RETURN(rc);
}
EXPORT_SYMBOL(lprocfs_exp_setup);

int lprocfs_exp_cleanup(struct obd_export *exp)
{
	struct nid_stat *stat = exp->exp_nid_stats;

	if(!stat || !exp->exp_obd)
		RETURN(0);

	nidstat_putref(exp->exp_nid_stats);
	exp->exp_nid_stats = NULL;

	return 0;
}
EXPORT_SYMBOL(lprocfs_exp_cleanup);

int lprocfs_write_helper(const char *buffer, unsigned long count,
			 int *val)
{
	return lprocfs_write_frac_helper(buffer, count, val, 1);
}
EXPORT_SYMBOL(lprocfs_write_helper);

int lprocfs_write_frac_helper(const char *buffer, unsigned long count,
			      int *val, int mult)
{
	char kernbuf[20], *end, *pbuf;

	if (count > (sizeof(kernbuf) - 1))
		return -EINVAL;

	if (copy_from_user(kernbuf, buffer, count))
		return -EFAULT;

	kernbuf[count] = '\0';
	pbuf = kernbuf;
	if (*pbuf == '-') {
		mult = -mult;
		pbuf++;
	}

	*val = (int)simple_strtoul(pbuf, &end, 10) * mult;
	if (pbuf == end)
		return -EINVAL;

	if (end != NULL && *end == '.') {
		int temp_val, pow = 1;
		int i;

		pbuf = end + 1;
		if (strlen(pbuf) > 5)
			pbuf[5] = '\0'; /*only allow 5bits fractional*/

		temp_val = (int)simple_strtoul(pbuf, &end, 10) * mult;

		if (pbuf < end) {
			for (i = 0; i < (end - pbuf); i++)
				pow *= 10;

			*val += temp_val / pow;
		}
	}
	return 0;
}
EXPORT_SYMBOL(lprocfs_write_frac_helper);

int lprocfs_read_frac_helper(char *buffer, unsigned long count, long val,
			     int mult)
{
	long decimal_val, frac_val;
	int prtn;

	if (count < 10)
		return -EINVAL;

	decimal_val = val / mult;
	prtn = snprintf(buffer, count, "%ld", decimal_val);
	frac_val = val % mult;

	if (prtn < (count - 4) && frac_val > 0) {
		long temp_frac;
		int i, temp_mult = 1, frac_bits = 0;

		temp_frac = frac_val * 10;
		buffer[prtn++] = '.';
		while (frac_bits < 2 && (temp_frac / mult) < 1 ) {
			/* only reserved 2 bits fraction */
			buffer[prtn++] ='0';
			temp_frac *= 10;
			frac_bits++;
		}
		/*
		 * Need to think these cases :
		 *      1. #echo x.00 > /proc/xxx       output result : x
		 *      2. #echo x.0x > /proc/xxx       output result : x.0x
		 *      3. #echo x.x0 > /proc/xxx       output result : x.x
		 *      4. #echo x.xx > /proc/xxx       output result : x.xx
		 *      Only reserved 2 bits fraction.
		 */
		for (i = 0; i < (5 - prtn); i++)
			temp_mult *= 10;

		frac_bits = min((int)count - prtn, 3 - frac_bits);
		prtn += snprintf(buffer + prtn, frac_bits, "%ld",
				 frac_val * temp_mult / mult);

		prtn--;
		while(buffer[prtn] < '1' || buffer[prtn] > '9') {
			prtn--;
			if (buffer[prtn] == '.') {
				prtn--;
				break;
			}
		}
		prtn++;
	}
	buffer[prtn++] ='\n';
	return prtn;
}
EXPORT_SYMBOL(lprocfs_read_frac_helper);

int lprocfs_write_u64_helper(const char *buffer, unsigned long count,__u64 *val)
{
	return lprocfs_write_frac_u64_helper(buffer, count, val, 1);
}
EXPORT_SYMBOL(lprocfs_write_u64_helper);

int lprocfs_write_frac_u64_helper(const char *buffer, unsigned long count,
			      __u64 *val, int mult)
{
	char kernbuf[22], *end, *pbuf;
	__u64 whole, frac = 0, units;
	unsigned frac_d = 1;

	if (count > (sizeof(kernbuf) - 1))
		return -EINVAL;

	if (copy_from_user(kernbuf, buffer, count))
		return -EFAULT;

	kernbuf[count] = '\0';
	pbuf = kernbuf;
	if (*pbuf == '-') {
		mult = -mult;
		pbuf++;
	}

	whole = simple_strtoull(pbuf, &end, 10);
	if (pbuf == end)
		return -EINVAL;

	if (end != NULL && *end == '.') {
		int i;
		pbuf = end + 1;

		/* need to limit frac_d to a __u32 */
		if (strlen(pbuf) > 10)
			pbuf[10] = '\0';

		frac = simple_strtoull(pbuf, &end, 10);
		/* count decimal places */
		for (i = 0; i < (end - pbuf); i++)
			frac_d *= 10;
	}

	units = 1;
	switch(*end) {
	case 'p': case 'P':
		units <<= 10;
	case 't': case 'T':
		units <<= 10;
	case 'g': case 'G':
		units <<= 10;
	case 'm': case 'M':
		units <<= 10;
	case 'k': case 'K':
		units <<= 10;
	}
	/* Specified units override the multiplier */
	if (units)
		mult = mult < 0 ? -units : units;

	frac *= mult;
	do_div(frac, frac_d);
	*val = whole * mult + frac;
	return 0;
}
EXPORT_SYMBOL(lprocfs_write_frac_u64_helper);

static char *lprocfs_strnstr(const char *s1, const char *s2, size_t len)
{
	size_t l2;

	l2 = strlen(s2);
	if (!l2)
		return (char *)s1;
	while (len >= l2) {
		len--;
		if (!memcmp(s1, s2, l2))
			return (char *)s1;
		s1++;
	}
	return NULL;
}

/**
 * Find the string \a name in the input \a buffer, and return a pointer to the
 * value immediately following \a name, reducing \a count appropriately.
 * If \a name is not found the original \a buffer is returned.
 */
char *lprocfs_find_named_value(const char *buffer, const char *name,
				unsigned long *count)
{
	char *val;
	size_t buflen = *count;

	/* there is no strnstr() in rhel5 and ubuntu kernels */
	val = lprocfs_strnstr(buffer, name, buflen);
	if (val == NULL)
		return (char *)buffer;

	val += strlen(name);			     /* skip prefix */
	while (val < buffer + buflen && isspace(*val)) /* skip separator */
		val++;

	*count = 0;
	while (val < buffer + buflen && isalnum(*val)) {
		++*count;
		++val;
	}

	return val - *count;
}
EXPORT_SYMBOL(lprocfs_find_named_value);

int lprocfs_seq_create(proc_dir_entry_t *parent,
		       const char *name,
		       mode_t mode,
		       const struct file_operations *seq_fops,
		       void *data)
{
	struct proc_dir_entry *entry;
	ENTRY;

	/* Disallow secretly (un)writable entries. */
	LASSERT((seq_fops->write == NULL) == ((mode & 0222) == 0));

	LPROCFS_WRITE_ENTRY();
	entry = create_proc_entry(name, mode, parent);
	if (entry) {
		entry->proc_fops = seq_fops;
		entry->data = data;
	}
	LPROCFS_WRITE_EXIT();

	if (entry == NULL)
		RETURN(-ENOMEM);

	RETURN(0);
}
EXPORT_SYMBOL(lprocfs_seq_create);

int lprocfs_obd_seq_create(struct obd_device *dev,
			   const char *name,
			   mode_t mode,
			   const struct file_operations *seq_fops,
			   void *data)
{
	return (lprocfs_seq_create(dev->obd_proc_entry, name,
				   mode, seq_fops, data));
}
EXPORT_SYMBOL(lprocfs_obd_seq_create);

void lprocfs_oh_tally(struct obd_histogram *oh, unsigned int value)
{
	if (value >= OBD_HIST_MAX)
		value = OBD_HIST_MAX - 1;

	spin_lock(&oh->oh_lock);
	oh->oh_buckets[value]++;
	spin_unlock(&oh->oh_lock);
}
EXPORT_SYMBOL(lprocfs_oh_tally);

void lprocfs_oh_tally_log2(struct obd_histogram *oh, unsigned int value)
{
	unsigned int val;

	for (val = 0; ((1 << val) < value) && (val <= OBD_HIST_MAX); val++)
		;

	lprocfs_oh_tally(oh, val);
}
EXPORT_SYMBOL(lprocfs_oh_tally_log2);

unsigned long lprocfs_oh_sum(struct obd_histogram *oh)
{
	unsigned long ret = 0;
	int i;

	for (i = 0; i < OBD_HIST_MAX; i++)
		ret +=  oh->oh_buckets[i];
	return ret;
}
EXPORT_SYMBOL(lprocfs_oh_sum);

void lprocfs_oh_clear(struct obd_histogram *oh)
{
	spin_lock(&oh->oh_lock);
	memset(oh->oh_buckets, 0, sizeof(oh->oh_buckets));
	spin_unlock(&oh->oh_lock);
}
EXPORT_SYMBOL(lprocfs_oh_clear);

int lprocfs_obd_rd_hash(char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	struct obd_device *obd = data;
	int c = 0;

	if (obd == NULL)
		return 0;

	c += cfs_hash_debug_header(page, count);
	c += cfs_hash_debug_str(obd->obd_uuid_hash, page + c, count - c);
	c += cfs_hash_debug_str(obd->obd_nid_hash, page + c, count - c);
	c += cfs_hash_debug_str(obd->obd_nid_stats_hash, page+c, count-c);

	return c;
}
EXPORT_SYMBOL(lprocfs_obd_rd_hash);

int lprocfs_obd_rd_recovery_status(char *page, char **start, off_t off,
				   int count, int *eof, void *data)
{
	struct obd_device *obd = data;
	int len = 0, size;

	LASSERT(obd != NULL);
	LASSERT(count >= 0);

	/* Set start of user data returned to
	   page + off since the user may have
	   requested to read much smaller than
	   what we need to read */
	*start = page + off;

	/* We know we are allocated a page here.
	   Also we know that this function will
	   not need to write more than a page
	   so we can truncate at PAGE_CACHE_SIZE.  */
	size = min(count + (int)off + 1, (int)PAGE_CACHE_SIZE);

	/* Initialize the page */
	memset(page, 0, size);

	if (lprocfs_obd_snprintf(&page, size, &len, "status: ") <= 0)
		goto out;
	if (obd->obd_max_recoverable_clients == 0) {
		if (lprocfs_obd_snprintf(&page, size, &len, "INACTIVE\n") <= 0)
			goto out;

		goto fclose;
	}

	/* sampled unlocked, but really... */
	if (obd->obd_recovering == 0) {
		if (lprocfs_obd_snprintf(&page, size, &len, "COMPLETE\n") <= 0)
			goto out;
		if (lprocfs_obd_snprintf(&page, size, &len,
					 "recovery_start: %lu\n",
					 obd->obd_recovery_start) <= 0)
			goto out;
		if (lprocfs_obd_snprintf(&page, size, &len,
					 "recovery_duration: %lu\n",
					 obd->obd_recovery_end -
					 obd->obd_recovery_start) <= 0)
			goto out;
		/* Number of clients that have completed recovery */
		if (lprocfs_obd_snprintf(&page, size, &len,
					 "completed_clients: %d/%d\n",
					 obd->obd_max_recoverable_clients -
					 obd->obd_stale_clients,
					 obd->obd_max_recoverable_clients) <= 0)
			goto out;
		if (lprocfs_obd_snprintf(&page, size, &len,
					 "replayed_requests: %d\n",
					 obd->obd_replayed_requests) <= 0)
			goto out;
		if (lprocfs_obd_snprintf(&page, size, &len,
					 "last_transno: "LPD64"\n",
					 obd->obd_next_recovery_transno - 1)<=0)
			goto out;
		if (lprocfs_obd_snprintf(&page, size, &len, "VBR: %s\n",
					 obd->obd_version_recov ?
					 "ENABLED" : "DISABLED") <=0)
			goto out;
		if (lprocfs_obd_snprintf(&page, size, &len, "IR: %s\n",
					 obd->obd_no_ir ?
					 "DISABLED" : "ENABLED") <= 0)
			goto out;
		goto fclose;
	}

	if (lprocfs_obd_snprintf(&page, size, &len, "RECOVERING\n") <= 0)
		goto out;
	if (lprocfs_obd_snprintf(&page, size, &len, "recovery_start: %lu\n",
				 obd->obd_recovery_start) <= 0)
		goto out;
	if (lprocfs_obd_snprintf(&page, size, &len, "time_remaining: %lu\n",
				 cfs_time_current_sec() >=
				 obd->obd_recovery_start +
				 obd->obd_recovery_timeout ? 0 :
				 obd->obd_recovery_start +
				 obd->obd_recovery_timeout -
				 cfs_time_current_sec()) <= 0)
		goto out;
	if (lprocfs_obd_snprintf(&page, size, &len,"connected_clients: %d/%d\n",
				 atomic_read(&obd->obd_connected_clients),
				 obd->obd_max_recoverable_clients) <= 0)
		goto out;
	/* Number of clients that have completed recovery */
	if (lprocfs_obd_snprintf(&page, size, &len,"req_replay_clients: %d\n",
				 atomic_read(&obd->obd_req_replay_clients))
		<= 0)
		goto out;
	if (lprocfs_obd_snprintf(&page, size, &len,"lock_repay_clients: %d\n",
				 atomic_read(&obd->obd_lock_replay_clients))
		<=0)
		goto out;
	if (lprocfs_obd_snprintf(&page, size, &len,"completed_clients: %d\n",
				 atomic_read(&obd->obd_connected_clients) -
				 atomic_read(&obd->obd_lock_replay_clients))
		<=0)
		goto out;
	if (lprocfs_obd_snprintf(&page, size, &len,"evicted_clients: %d\n",
				 obd->obd_stale_clients) <= 0)
		goto out;
	if (lprocfs_obd_snprintf(&page, size, &len,"replayed_requests: %d\n",
				 obd->obd_replayed_requests) <= 0)
		goto out;
	if (lprocfs_obd_snprintf(&page, size, &len, "queued_requests: %d\n",
				 obd->obd_requests_queued_for_recovery) <= 0)
		goto out;

	if (lprocfs_obd_snprintf(&page, size, &len, "next_transno: "LPD64"\n",
				 obd->obd_next_recovery_transno) <= 0)
		goto out;

fclose:
	*eof = 1;
out:
	return min(count, len - (int)off);
}
EXPORT_SYMBOL(lprocfs_obd_rd_recovery_status);

int lprocfs_obd_rd_ir_factor(char *page, char **start, off_t off,
			     int count, int *eof, void *data)
{
	struct obd_device *obd = (struct obd_device *)data;
	LASSERT(obd != NULL);

	return snprintf(page, count, "%d\n",
			obd->obd_recovery_ir_factor);
}
EXPORT_SYMBOL(lprocfs_obd_rd_ir_factor);

int lprocfs_obd_wr_ir_factor(struct file *file, const char *buffer,
			     unsigned long count, void *data)
{
	struct obd_device *obd = (struct obd_device *)data;
	int val, rc;
	LASSERT(obd != NULL);

	rc = lprocfs_write_helper(buffer, count, &val);
	if (rc)
		return rc;

	if (val < OBD_IR_FACTOR_MIN || val > OBD_IR_FACTOR_MAX)
		return -EINVAL;

	obd->obd_recovery_ir_factor = val;
	return count;
}
EXPORT_SYMBOL(lprocfs_obd_wr_ir_factor);

int lprocfs_obd_rd_recovery_time_soft(char *page, char **start, off_t off,
				      int count, int *eof, void *data)
{
	struct obd_device *obd = (struct obd_device *)data;
	LASSERT(obd != NULL);

	return snprintf(page, count, "%d\n",
			obd->obd_recovery_timeout);
}
EXPORT_SYMBOL(lprocfs_obd_rd_recovery_time_soft);

int lprocfs_obd_wr_recovery_time_soft(struct file *file, const char *buffer,
				      unsigned long count, void *data)
{
	struct obd_device *obd = (struct obd_device *)data;
	int val, rc;
	LASSERT(obd != NULL);

	rc = lprocfs_write_helper(buffer, count, &val);
	if (rc)
		return rc;

	obd->obd_recovery_timeout = val;
	return count;
}
EXPORT_SYMBOL(lprocfs_obd_wr_recovery_time_soft);

int lprocfs_obd_rd_recovery_time_hard(char *page, char **start, off_t off,
				      int count, int *eof, void *data)
{
	struct obd_device *obd = data;
	LASSERT(obd != NULL);

	return snprintf(page, count, "%u\n", obd->obd_recovery_time_hard);
}
EXPORT_SYMBOL(lprocfs_obd_rd_recovery_time_hard);

int lprocfs_obd_wr_recovery_time_hard(struct file *file, const char *buffer,
				      unsigned long count, void *data)
{
	struct obd_device *obd = data;
	int val, rc;
	LASSERT(obd != NULL);

	rc = lprocfs_write_helper(buffer, count, &val);
	if (rc)
		return rc;

	obd->obd_recovery_time_hard = val;
	return count;
}
EXPORT_SYMBOL(lprocfs_obd_wr_recovery_time_hard);

int lprocfs_obd_rd_max_pages_per_rpc(char *page, char **start, off_t off,
				     int count, int *eof, void *data)
{
	struct obd_device *dev = data;
	struct client_obd *cli = &dev->u.cli;
	int rc;

	client_obd_list_lock(&cli->cl_loi_list_lock);
	rc = snprintf(page, count, "%d\n", cli->cl_max_pages_per_rpc);
	client_obd_list_unlock(&cli->cl_loi_list_lock);
	return rc;
}
EXPORT_SYMBOL(lprocfs_obd_rd_max_pages_per_rpc);

int lprocfs_target_rd_instance(char *page, char **start, off_t off,
			       int count, int *eof, void *data)
{
	struct obd_device *obd = (struct obd_device *)data;
	struct obd_device_target *target = &obd->u.obt;

	LASSERT(obd != NULL);
	LASSERT(target->obt_magic == OBT_MAGIC);
	*eof = 1;
	return snprintf(page, count, "%u\n", obd->u.obt.obt_instance);
}
EXPORT_SYMBOL(lprocfs_target_rd_instance);
#endif /* LPROCFS*/

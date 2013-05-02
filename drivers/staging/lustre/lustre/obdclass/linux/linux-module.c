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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/obdclass/linux/linux-module.c
 *
 * Object Devices Class Driver
 * These are the only exported functions, they provide some generic
 * infrastructure for managing object devices
 */

#define DEBUG_SUBSYSTEM S_CLASS

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/lp.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/delay.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/highmem.h>
#include <asm/io.h>
#include <asm/ioctls.h>
#include <asm/poll.h>
#include <asm/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/seq_file.h>

#include <linux/libcfs/libcfs.h>
#include <obd_support.h>
#include <obd_class.h>
#include <linux/lnet/lnetctl.h>
#include <lprocfs_status.h>
#include <lustre_ver.h>
#include <lustre/lustre_build_version.h>

int proc_version;

/* buffer MUST be at least the size of obd_ioctl_hdr */
int obd_ioctl_getdata(char **buf, int *len, void *arg)
{
	struct obd_ioctl_hdr hdr;
	struct obd_ioctl_data *data;
	int err;
	int offset = 0;
	ENTRY;

	err = copy_from_user(&hdr, (void *)arg, sizeof(hdr));
	if ( err )
		RETURN(err);

	if (hdr.ioc_version != OBD_IOCTL_VERSION) {
		CERROR("Version mismatch kernel (%x) vs application (%x)\n",
		       OBD_IOCTL_VERSION, hdr.ioc_version);
		RETURN(-EINVAL);
	}

	if (hdr.ioc_len > OBD_MAX_IOCTL_BUFFER) {
		CERROR("User buffer len %d exceeds %d max buffer\n",
		       hdr.ioc_len, OBD_MAX_IOCTL_BUFFER);
		RETURN(-EINVAL);
	}

	if (hdr.ioc_len < sizeof(struct obd_ioctl_data)) {
		CERROR("User buffer too small for ioctl (%d)\n", hdr.ioc_len);
		RETURN(-EINVAL);
	}

	/* When there are lots of processes calling vmalloc on multi-core
	 * system, the high lock contention will hurt performance badly,
	 * obdfilter-survey is an example, which relies on ioctl. So we'd
	 * better avoid vmalloc on ioctl path. LU-66 */
	OBD_ALLOC_LARGE(*buf, hdr.ioc_len);
	if (*buf == NULL) {
		CERROR("Cannot allocate control buffer of len %d\n",
		       hdr.ioc_len);
		RETURN(-EINVAL);
	}
	*len = hdr.ioc_len;
	data = (struct obd_ioctl_data *)*buf;

	err = copy_from_user(*buf, (void *)arg, hdr.ioc_len);
	if ( err ) {
		OBD_FREE_LARGE(*buf, hdr.ioc_len);
		RETURN(err);
	}

	if (obd_ioctl_is_invalid(data)) {
		CERROR("ioctl not correctly formatted\n");
		OBD_FREE_LARGE(*buf, hdr.ioc_len);
		RETURN(-EINVAL);
	}

	if (data->ioc_inllen1) {
		data->ioc_inlbuf1 = &data->ioc_bulk[0];
		offset += cfs_size_round(data->ioc_inllen1);
	}

	if (data->ioc_inllen2) {
		data->ioc_inlbuf2 = &data->ioc_bulk[0] + offset;
		offset += cfs_size_round(data->ioc_inllen2);
	}

	if (data->ioc_inllen3) {
		data->ioc_inlbuf3 = &data->ioc_bulk[0] + offset;
		offset += cfs_size_round(data->ioc_inllen3);
	}

	if (data->ioc_inllen4) {
		data->ioc_inlbuf4 = &data->ioc_bulk[0] + offset;
	}

	EXIT;
	return 0;
}
EXPORT_SYMBOL(obd_ioctl_getdata);

int obd_ioctl_popdata(void *arg, void *data, int len)
{
	int err;

	err = copy_to_user(arg, data, len);
	if (err)
		err = -EFAULT;
	return err;
}
EXPORT_SYMBOL(obd_ioctl_popdata);

/*  opening /dev/obd */
static int obd_class_open(struct inode * inode, struct file * file)
{
	ENTRY;

	try_module_get(THIS_MODULE);
	RETURN(0);
}

/*  closing /dev/obd */
static int obd_class_release(struct inode * inode, struct file * file)
{
	ENTRY;

	module_put(THIS_MODULE);
	RETURN(0);
}

/* to control /dev/obd */
static long obd_class_ioctl(struct file *filp, unsigned int cmd,
			    unsigned long arg)
{
	int err = 0;
	ENTRY;

	/* Allow non-root access for OBD_IOC_PING_TARGET - used by lfs check */
	if (!cfs_capable(CFS_CAP_SYS_ADMIN) && (cmd != OBD_IOC_PING_TARGET))
		RETURN(err = -EACCES);
	if ((cmd & 0xffffff00) == ((int)'T') << 8) /* ignore all tty ioctls */
		RETURN(err = -ENOTTY);

	err = class_handle_ioctl(cmd, (unsigned long)arg);

	RETURN(err);
}

/* declare character device */
static struct file_operations obd_psdev_fops = {
	.owner	  = THIS_MODULE,
	.unlocked_ioctl = obd_class_ioctl, /* unlocked_ioctl */
	.open	   = obd_class_open,      /* open */
	.release	= obd_class_release,   /* release */
};

/* modules setup */
psdev_t obd_psdev = {
	.minor = OBD_DEV_MINOR,
	.name  = OBD_DEV_NAME,
	.fops  = &obd_psdev_fops,
};


#ifdef LPROCFS
int obd_proc_read_version(char *page, char **start, off_t off, int count,
			  int *eof, void *data)
{
	*eof = 1;
	return snprintf(page, count, "lustre: %s\nkernel: %s\nbuild:  %s\n",
			LUSTRE_VERSION_STRING, "patchless_client",
			BUILD_VERSION);
}

int obd_proc_read_pinger(char *page, char **start, off_t off, int count,
			 int *eof, void *data)
{
	*eof = 1;
	return snprintf(page, count, "%s\n",
			"on"
		       );
}

/**
 * Check all obd devices health
 *
 * \param page
 * \param start
 * \param off
 * \param count
 * \param eof
 * \param data
 *		  proc read function parameters, please refer to kernel
 *		  code fs/proc/generic.c proc_file_read()
 * \param data [in] unused
 *
 * \retval number of characters printed
 */
static int obd_proc_read_health(char *page, char **start, off_t off,
				int count, int *eof, void *data)
{
	int rc = 0, i;
	*eof = 1;

	if (libcfs_catastrophe)
		rc += snprintf(page + rc, count - rc, "LBUG\n");

	read_lock(&obd_dev_lock);
	for (i = 0; i < class_devno_max(); i++) {
		struct obd_device *obd;

		obd = class_num2obd(i);
		if (obd == NULL || !obd->obd_attached || !obd->obd_set_up)
			continue;

		LASSERT(obd->obd_magic == OBD_DEVICE_MAGIC);
		if (obd->obd_stopping)
			continue;

		class_incref(obd, __FUNCTION__, current);
		read_unlock(&obd_dev_lock);

		if (obd_health_check(NULL, obd)) {
			rc += snprintf(page + rc, count - rc,
				       "device %s reported unhealthy\n",
				       obd->obd_name);
		}
		class_decref(obd, __FUNCTION__, current);
		read_lock(&obd_dev_lock);
	}
	read_unlock(&obd_dev_lock);

	if (rc == 0)
		return snprintf(page, count, "healthy\n");

	rc += snprintf(page + rc, count - rc, "NOT HEALTHY\n");
	return rc;
}

static int obd_proc_rd_jobid_var(char *page, char **start, off_t off,
				int count, int *eof, void *data)
{
	return snprintf(page, count, "%s\n", obd_jobid_var);
}

static int obd_proc_wr_jobid_var(struct file *file, const char *buffer,
				unsigned long count, void *data)
{
	if (!count || count > JOBSTATS_JOBID_VAR_MAX_LEN)
		return -EINVAL;

	memset(obd_jobid_var, 0, JOBSTATS_JOBID_VAR_MAX_LEN + 1);
	/* Trim the trailing '\n' if any */
	memcpy(obd_jobid_var, buffer, count - (buffer[count - 1] == '\n'));
	return count;
}

/* Root for /proc/fs/lustre */
struct proc_dir_entry *proc_lustre_root = NULL;
EXPORT_SYMBOL(proc_lustre_root);

struct lprocfs_vars lprocfs_base[] = {
	{ "version", obd_proc_read_version, NULL, NULL },
	{ "pinger", obd_proc_read_pinger, NULL, NULL },
	{ "health_check", obd_proc_read_health, NULL, NULL },
	{ "jobid_var", obd_proc_rd_jobid_var,
		       obd_proc_wr_jobid_var, NULL },
	{ 0 }
};
#else
#define lprocfs_base NULL
#endif /* LPROCFS */

static void *obd_device_list_seq_start(struct seq_file *p, loff_t *pos)
{
	if (*pos >= class_devno_max())
		return NULL;

	return pos;
}

static void obd_device_list_seq_stop(struct seq_file *p, void *v)
{
}

static void *obd_device_list_seq_next(struct seq_file *p, void *v, loff_t *pos)
{
	++*pos;
	if (*pos >= class_devno_max())
		return NULL;

	return pos;
}

static int obd_device_list_seq_show(struct seq_file *p, void *v)
{
	loff_t index = *(loff_t *)v;
	struct obd_device *obd = class_num2obd((int)index);
	char *status;

	if (obd == NULL)
		return 0;

	LASSERT(obd->obd_magic == OBD_DEVICE_MAGIC);
	if (obd->obd_stopping)
		status = "ST";
	else if (obd->obd_inactive)
		status = "IN";
	else if (obd->obd_set_up)
		status = "UP";
	else if (obd->obd_attached)
		status = "AT";
	else
		status = "--";

	return seq_printf(p, "%3d %s %s %s %s %d\n",
			  (int)index, status, obd->obd_type->typ_name,
			  obd->obd_name, obd->obd_uuid.uuid,
			  atomic_read(&obd->obd_refcount));
}

struct seq_operations obd_device_list_sops = {
	.start = obd_device_list_seq_start,
	.stop = obd_device_list_seq_stop,
	.next = obd_device_list_seq_next,
	.show = obd_device_list_seq_show,
};

static int obd_device_list_open(struct inode *inode, struct file *file)
{
	struct proc_dir_entry *dp = PDE(inode);
	struct seq_file *seq;
	int rc = seq_open(file, &obd_device_list_sops);

	if (rc)
		return rc;

	seq = file->private_data;
	seq->private = dp->data;

	return 0;
}

struct file_operations obd_device_list_fops = {
	.owner   = THIS_MODULE,
	.open    = obd_device_list_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

int class_procfs_init(void)
{
	int rc;
	ENTRY;

	obd_sysctl_init();
	proc_lustre_root = lprocfs_register("fs/lustre", NULL,
					    lprocfs_base, NULL);
	rc = lprocfs_seq_create(proc_lustre_root, "devices", 0444,
				&obd_device_list_fops, NULL);
	if (rc)
		CERROR("error adding /proc/fs/lustre/devices file\n");
	RETURN(0);
}

int class_procfs_clean(void)
{
	ENTRY;
	if (proc_lustre_root) {
		lprocfs_remove(&proc_lustre_root);
	}
	RETURN(0);
}

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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_LNET

#include <linux/libcfs/libcfs.h>
#include <linux/libcfs/libcfs_crypto.h>
#include <linux/lnet/lib-lnet.h>
#include <linux/lnet/lnet.h>
#include "tracefile.h"

void
kportal_memhog_free (struct libcfs_device_userstate *ldu)
{
	struct page **level0p = &ldu->ldu_memhog_root_page;
	struct page **level1p;
	struct page **level2p;
	int	   count1;
	int	   count2;

	if (*level0p != NULL) {

		level1p = (struct page **)page_address(*level0p);
		count1 = 0;

		while (count1 < PAGE_CACHE_SIZE/sizeof(struct page *) &&
		       *level1p != NULL) {

			level2p = (struct page **)page_address(*level1p);
			count2 = 0;

			while (count2 < PAGE_CACHE_SIZE/sizeof(struct page *) &&
			       *level2p != NULL) {

				__free_page(*level2p);
				ldu->ldu_memhog_pages--;
				level2p++;
				count2++;
			}

			__free_page(*level1p);
			ldu->ldu_memhog_pages--;
			level1p++;
			count1++;
		}

		__free_page(*level0p);
		ldu->ldu_memhog_pages--;

		*level0p = NULL;
	}

	LASSERT (ldu->ldu_memhog_pages == 0);
}

int
kportal_memhog_alloc(struct libcfs_device_userstate *ldu, int npages,
		     gfp_t flags)
{
	struct page **level0p;
	struct page **level1p;
	struct page **level2p;
	int	   count1;
	int	   count2;

	LASSERT (ldu->ldu_memhog_pages == 0);
	LASSERT (ldu->ldu_memhog_root_page == NULL);

	if (npages < 0)
		return -EINVAL;

	if (npages == 0)
		return 0;

	level0p = &ldu->ldu_memhog_root_page;
	*level0p = alloc_page(flags);
	if (*level0p == NULL)
		return -ENOMEM;
	ldu->ldu_memhog_pages++;

	level1p = (struct page **)page_address(*level0p);
	count1 = 0;
	memset(level1p, 0, PAGE_CACHE_SIZE);

	while (ldu->ldu_memhog_pages < npages &&
	       count1 < PAGE_CACHE_SIZE/sizeof(struct page *)) {

		if (cfs_signal_pending())
			return (-EINTR);

		*level1p = alloc_page(flags);
		if (*level1p == NULL)
			return -ENOMEM;
		ldu->ldu_memhog_pages++;

		level2p = (struct page **)page_address(*level1p);
		count2 = 0;
		memset(level2p, 0, PAGE_CACHE_SIZE);

		while (ldu->ldu_memhog_pages < npages &&
		       count2 < PAGE_CACHE_SIZE/sizeof(struct page *)) {

			if (cfs_signal_pending())
				return (-EINTR);

			*level2p = alloc_page(flags);
			if (*level2p == NULL)
				return (-ENOMEM);
			ldu->ldu_memhog_pages++;

			level2p++;
			count2++;
		}

		level1p++;
		count1++;
	}

	return 0;
}

/* called when opening /dev/device */
static int libcfs_psdev_open(unsigned long flags, void *args)
{
	struct libcfs_device_userstate *ldu;

	try_module_get(THIS_MODULE);

	LIBCFS_ALLOC(ldu, sizeof(*ldu));
	if (ldu != NULL) {
		ldu->ldu_memhog_pages = 0;
		ldu->ldu_memhog_root_page = NULL;
	}
	*(struct libcfs_device_userstate **)args = ldu;

	return 0;
}

/* called when closing /dev/device */
static int libcfs_psdev_release(unsigned long flags, void *args)
{
	struct libcfs_device_userstate *ldu;

	ldu = (struct libcfs_device_userstate *)args;
	if (ldu != NULL) {
		kportal_memhog_free(ldu);
		LIBCFS_FREE(ldu, sizeof(*ldu));
	}

	module_put(THIS_MODULE);
	return 0;
}

static struct rw_semaphore ioctl_list_sem;
static struct list_head ioctl_list;

int libcfs_register_ioctl(struct libcfs_ioctl_handler *hand)
{
	int rc = 0;

	down_write(&ioctl_list_sem);
	if (!list_empty(&hand->item))
		rc = -EBUSY;
	else
		list_add_tail(&hand->item, &ioctl_list);
	up_write(&ioctl_list_sem);

	return rc;
}
EXPORT_SYMBOL(libcfs_register_ioctl);

int libcfs_deregister_ioctl(struct libcfs_ioctl_handler *hand)
{
	int rc = 0;

	down_write(&ioctl_list_sem);
	if (list_empty(&hand->item))
		rc = -ENOENT;
	else
		list_del_init(&hand->item);
	up_write(&ioctl_list_sem);

	return rc;
}
EXPORT_SYMBOL(libcfs_deregister_ioctl);

static int libcfs_ioctl_int(struct cfs_psdev_file *pfile,unsigned long cmd,
			    void *arg, struct libcfs_ioctl_data *data)
{
	int err = -EINVAL;

	switch (cmd) {
	case IOC_LIBCFS_CLEAR_DEBUG:
		libcfs_debug_clear_buffer();
		return 0;
	/*
	 * case IOC_LIBCFS_PANIC:
	 * Handled in arch/cfs_module.c
	 */
	case IOC_LIBCFS_MARK_DEBUG:
		if (data->ioc_inlbuf1 == NULL ||
		    data->ioc_inlbuf1[data->ioc_inllen1 - 1] != '\0')
			return -EINVAL;
		libcfs_debug_mark_buffer(data->ioc_inlbuf1);
		return 0;
	case IOC_LIBCFS_MEMHOG:
		if (pfile->private_data == NULL) {
			err = -EINVAL;
		} else {
			kportal_memhog_free(pfile->private_data);
			/* XXX The ioc_flags is not GFP flags now, need to be fixed */
			err = kportal_memhog_alloc(pfile->private_data,
						   data->ioc_count,
						   data->ioc_flags);
			if (err != 0)
				kportal_memhog_free(pfile->private_data);
		}
		break;

	case IOC_LIBCFS_PING_TEST: {
		extern void (kping_client)(struct libcfs_ioctl_data *);
		void (*ping)(struct libcfs_ioctl_data *);

		CDEBUG(D_IOCTL, "doing %d pings to nid %s (%s)\n",
		       data->ioc_count, libcfs_nid2str(data->ioc_nid),
		       libcfs_nid2str(data->ioc_nid));
		ping = symbol_get(kping_client);
		if (!ping)
			CERROR("symbol_get failed\n");
		else {
			ping(data);
			symbol_put(kping_client);
		}
		return 0;
	}

	default: {
		struct libcfs_ioctl_handler *hand;
		err = -EINVAL;
		down_read(&ioctl_list_sem);
		list_for_each_entry(hand, &ioctl_list, item) {
			err = hand->handle_ioctl(cmd, data);
			if (err != -EINVAL) {
				if (err == 0)
					err = libcfs_ioctl_popdata(arg,
							data, sizeof (*data));
				break;
			}
		}
		up_read(&ioctl_list_sem);
		break;
	}
	}

	return err;
}

static int libcfs_ioctl(struct cfs_psdev_file *pfile, unsigned long cmd, void *arg)
{
	char    *buf;
	struct libcfs_ioctl_data *data;
	int err = 0;

	LIBCFS_ALLOC_GFP(buf, 1024, GFP_IOFS);
	if (buf == NULL)
		return -ENOMEM;

	/* 'cmd' and permissions get checked in our arch-specific caller */
	if (libcfs_ioctl_getdata(buf, buf + 800, (void *)arg)) {
		CERROR("PORTALS ioctl: data error\n");
		GOTO(out, err = -EINVAL);
	}
	data = (struct libcfs_ioctl_data *)buf;

	err = libcfs_ioctl_int(pfile, cmd, arg, data);

out:
	LIBCFS_FREE(buf, 1024);
	return err;
}


struct cfs_psdev_ops libcfs_psdev_ops = {
	libcfs_psdev_open,
	libcfs_psdev_release,
	NULL,
	NULL,
	libcfs_ioctl
};

extern int insert_proc(void);
extern void remove_proc(void);
MODULE_AUTHOR("Peter J. Braam <braam@clusterfs.com>");
MODULE_DESCRIPTION("Portals v3.1");
MODULE_LICENSE("GPL");

extern struct miscdevice libcfs_dev;
extern struct rw_semaphore cfs_tracefile_sem;
extern struct mutex cfs_trace_thread_mutex;
extern struct cfs_wi_sched *cfs_sched_rehash;

extern void libcfs_init_nidstrings(void);
extern int libcfs_arch_init(void);
extern void libcfs_arch_cleanup(void);

static int init_libcfs_module(void)
{
	int rc;

	libcfs_arch_init();
	libcfs_init_nidstrings();
	init_rwsem(&cfs_tracefile_sem);
	mutex_init(&cfs_trace_thread_mutex);
	init_rwsem(&ioctl_list_sem);
	INIT_LIST_HEAD(&ioctl_list);
	init_waitqueue_head(&cfs_race_waitq);

	rc = libcfs_debug_init(5 * 1024 * 1024);
	if (rc < 0) {
		printk(KERN_ERR "LustreError: libcfs_debug_init: %d\n", rc);
		return (rc);
	}

	rc = cfs_cpu_init();
	if (rc != 0)
		goto cleanup_debug;

	rc = misc_register(&libcfs_dev);
	if (rc) {
		CERROR("misc_register: error %d\n", rc);
		goto cleanup_cpu;
	}

	rc = cfs_wi_startup();
	if (rc) {
		CERROR("initialize workitem: error %d\n", rc);
		goto cleanup_deregister;
	}

	/* max to 4 threads, should be enough for rehash */
	rc = min(cfs_cpt_weight(cfs_cpt_table, CFS_CPT_ANY), 4);
	rc = cfs_wi_sched_create("cfs_rh", cfs_cpt_table, CFS_CPT_ANY,
				 rc, &cfs_sched_rehash);
	if (rc != 0) {
		CERROR("Startup workitem scheduler: error: %d\n", rc);
		goto cleanup_deregister;
	}

	rc = cfs_crypto_register();
	if (rc) {
		CERROR("cfs_crypto_register: error %d\n", rc);
		goto cleanup_wi;
	}


	rc = insert_proc();
	if (rc) {
		CERROR("insert_proc: error %d\n", rc);
		goto cleanup_crypto;
	}

	CDEBUG (D_OTHER, "portals setup OK\n");
	return 0;
 cleanup_crypto:
	cfs_crypto_unregister();
 cleanup_wi:
	cfs_wi_shutdown();
 cleanup_deregister:
	misc_deregister(&libcfs_dev);
cleanup_cpu:
	cfs_cpu_fini();
 cleanup_debug:
	libcfs_debug_cleanup();
	return rc;
}

static void exit_libcfs_module(void)
{
	int rc;

	remove_proc();

	CDEBUG(D_MALLOC, "before Portals cleanup: kmem %d\n",
	       atomic_read(&libcfs_kmemory));

	if (cfs_sched_rehash != NULL) {
		cfs_wi_sched_destroy(cfs_sched_rehash);
		cfs_sched_rehash = NULL;
	}

	cfs_crypto_unregister();
	cfs_wi_shutdown();

	rc = misc_deregister(&libcfs_dev);
	if (rc)
		CERROR("misc_deregister error %d\n", rc);

	cfs_cpu_fini();

	if (atomic_read(&libcfs_kmemory) != 0)
		CERROR("Portals memory leaked: %d bytes\n",
		       atomic_read(&libcfs_kmemory));

	rc = libcfs_debug_cleanup();
	if (rc)
		printk(KERN_ERR "LustreError: libcfs_debug_cleanup: %d\n",
		       rc);

	fini_rwsem(&ioctl_list_sem);
	fini_rwsem(&cfs_tracefile_sem);

	libcfs_arch_cleanup();
}

MODULE_VERSION("1.0.0");
module_init(init_libcfs_module);
module_exit(exit_libcfs_module);

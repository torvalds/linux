/*
 * u_fs.h
 *
 * Utility definitions for the FunctionFS
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzej.p@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef U_FFS_H
#define U_FFS_H

#include <linux/usb/composite.h>
#include <linux/list.h>
#include <linux/mutex.h>

#ifdef VERBOSE_DEBUG
#ifndef pr_vdebug
#  define pr_vdebug pr_debug
#endif /* pr_vdebug */
#  define ffs_dump_mem(prefix, ptr, len) \
	print_hex_dump_bytes(pr_fmt(prefix ": "), DUMP_PREFIX_NONE, ptr, len)
#else
#ifndef pr_vdebug
#  define pr_vdebug(...)                 do { } while (0)
#endif /* pr_vdebug */
#  define ffs_dump_mem(prefix, ptr, len) do { } while (0)
#endif /* VERBOSE_DEBUG */

#define ENTER()    pr_vdebug("%s()\n", __func__)

struct f_fs_opts;

struct ffs_dev {
	const char *name;
	bool name_allocated;
	bool mounted;
	bool desc_ready;
	bool single;
	struct ffs_data *ffs_data;
	struct f_fs_opts *opts;
	struct list_head entry;

	int (*ffs_ready_callback)(struct ffs_data *ffs);
	void (*ffs_closed_callback)(struct ffs_data *ffs);
	void *(*ffs_acquire_dev_callback)(struct ffs_dev *dev);
	void (*ffs_release_dev_callback)(struct ffs_dev *dev);
};

extern struct mutex ffs_lock;

static inline void ffs_dev_lock(void)
{
	mutex_lock(&ffs_lock);
}

static inline void ffs_dev_unlock(void)
{
	mutex_unlock(&ffs_lock);
}

struct ffs_dev *ffs_alloc_dev(void);
int ffs_name_dev(struct ffs_dev *dev, const char *name);
int ffs_single_dev(struct ffs_dev *dev);
void ffs_free_dev(struct ffs_dev *dev);

struct ffs_epfile;
struct ffs_function;

enum ffs_state {
	/*
	 * Waiting for descriptors and strings.
	 *
	 * In this state no open(2), read(2) or write(2) on epfiles
	 * may succeed (which should not be the problem as there
	 * should be no such files opened in the first place).
	 */
	FFS_READ_DESCRIPTORS,
	FFS_READ_STRINGS,

	/*
	 * We've got descriptors and strings.  We are or have called
	 * functionfs_ready_callback().  functionfs_bind() may have
	 * been called but we don't know.
	 *
	 * This is the only state in which operations on epfiles may
	 * succeed.
	 */
	FFS_ACTIVE,

	/*
	 * All endpoints have been closed.  This state is also set if
	 * we encounter an unrecoverable error.  The only
	 * unrecoverable error is situation when after reading strings
	 * from user space we fail to initialise epfiles or
	 * functionfs_ready_callback() returns with error (<0).
	 *
	 * In this state no open(2), read(2) or write(2) (both on ep0
	 * as well as epfile) may succeed (at this point epfiles are
	 * unlinked and all closed so this is not a problem; ep0 is
	 * also closed but ep0 file exists and so open(2) on ep0 must
	 * fail).
	 */
	FFS_CLOSING
};

enum ffs_setup_state {
	/* There is no setup request pending. */
	FFS_NO_SETUP,
	/*
	 * User has read events and there was a setup request event
	 * there.  The next read/write on ep0 will handle the
	 * request.
	 */
	FFS_SETUP_PENDING,
	/*
	 * There was event pending but before user space handled it
	 * some other event was introduced which canceled existing
	 * setup.  If this state is set read/write on ep0 return
	 * -EIDRM.  This state is only set when adding event.
	 */
	FFS_SETUP_CANCELED
};

struct ffs_data {
	struct usb_gadget		*gadget;

	/*
	 * Protect access read/write operations, only one read/write
	 * at a time.  As a consequence protects ep0req and company.
	 * While setup request is being processed (queued) this is
	 * held.
	 */
	struct mutex			mutex;

	/*
	 * Protect access to endpoint related structures (basically
	 * usb_ep_queue(), usb_ep_dequeue(), etc. calls) except for
	 * endpoint zero.
	 */
	spinlock_t			eps_lock;

	/*
	 * XXX REVISIT do we need our own request? Since we are not
	 * handling setup requests immediately user space may be so
	 * slow that another setup will be sent to the gadget but this
	 * time not to us but another function and then there could be
	 * a race.  Is that the case? Or maybe we can use cdev->req
	 * after all, maybe we just need some spinlock for that?
	 */
	struct usb_request		*ep0req;		/* P: mutex */
	struct completion		ep0req_completion;	/* P: mutex */
	int				ep0req_status;		/* P: mutex */

	/* reference counter */
	atomic_t			ref;
	/* how many files are opened (EP0 and others) */
	atomic_t			opened;

	/* EP0 state */
	enum ffs_state			state;

	/*
	 * Possible transitions:
	 * + FFS_NO_SETUP       -> FFS_SETUP_PENDING  -- P: ev.waitq.lock
	 *               happens only in ep0 read which is P: mutex
	 * + FFS_SETUP_PENDING  -> FFS_NO_SETUP       -- P: ev.waitq.lock
	 *               happens only in ep0 i/o  which is P: mutex
	 * + FFS_SETUP_PENDING  -> FFS_SETUP_CANCELED -- P: ev.waitq.lock
	 * + FFS_SETUP_CANCELED -> FFS_NO_SETUP       -- cmpxchg
	 */
	enum ffs_setup_state		setup_state;

#define FFS_SETUP_STATE(ffs)					\
	((enum ffs_setup_state)cmpxchg(&(ffs)->setup_state,	\
				       FFS_SETUP_CANCELED, FFS_NO_SETUP))

	/* Events & such. */
	struct {
		u8				types[4];
		unsigned short			count;
		/* XXX REVISIT need to update it in some places, or do we? */
		unsigned short			can_stall;
		struct usb_ctrlrequest		setup;

		wait_queue_head_t		waitq;
	} ev; /* the whole structure, P: ev.waitq.lock */

	/* Flags */
	unsigned long			flags;
#define FFS_FL_CALL_CLOSED_CALLBACK 0
#define FFS_FL_BOUND                1

	/* Active function */
	struct ffs_function		*func;

	/*
	 * Device name, write once when file system is mounted.
	 * Intended for user to read if she wants.
	 */
	const char			*dev_name;
	/* Private data for our user (ie. gadget).  Managed by user. */
	void				*private_data;

	/* filled by __ffs_data_got_descs() */
	/*
	 * Real descriptors are 16 bytes after raw_descs (so you need
	 * to skip 16 bytes (ie. ffs->raw_descs + 16) to get to the
	 * first full speed descriptor).  raw_descs_length and
	 * raw_fs_descs_length do not have those 16 bytes added.
	 */
	const void			*raw_descs;
	unsigned			raw_descs_length;
	unsigned			raw_fs_descs_length;
	unsigned			fs_descs_count;
	unsigned			hs_descs_count;

	unsigned short			strings_count;
	unsigned short			interfaces_count;
	unsigned short			eps_count;
	unsigned short			_pad1;

	/* filled by __ffs_data_got_strings() */
	/* ids in stringtabs are set in functionfs_bind() */
	const void			*raw_strings;
	struct usb_gadget_strings	**stringtabs;

	/*
	 * File system's super block, write once when file system is
	 * mounted.
	 */
	struct super_block		*sb;

	/* File permissions, written once when fs is mounted */
	struct ffs_file_perms {
		umode_t				mode;
		kuid_t				uid;
		kgid_t				gid;
	}				file_perms;

	/*
	 * The endpoint files, filled by ffs_epfiles_create(),
	 * destroyed by ffs_epfiles_destroy().
	 */
	struct ffs_epfile		*epfiles;
};


struct f_fs_opts {
	struct usb_function_instance	func_inst;
	struct ffs_dev			*dev;
	unsigned			refcnt;
	bool				no_configfs;
};

static inline struct f_fs_opts *to_f_fs_opts(struct usb_function_instance *fi)
{
	return container_of(fi, struct f_fs_opts, func_inst);
}

#endif /* U_FFS_H */

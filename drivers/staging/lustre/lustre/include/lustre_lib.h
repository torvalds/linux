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
 * lustre/include/lustre_lib.h
 *
 * Basic Lustre library routines.
 */

#ifndef _LUSTRE_LIB_H
#define _LUSTRE_LIB_H

/** \defgroup lib lib
 *
 * @{
 */

#include <linux/libcfs/libcfs.h>
#include <lustre/lustre_idl.h>
#include <lustre_ver.h>
#include <lustre_cfg.h>
#include <linux/lustre_lib.h>

/* target.c */
struct ptlrpc_request;
struct obd_export;
struct lu_target;
struct l_wait_info;
#include <lustre_ha.h>
#include <lustre_net.h>
#include <lvfs.h>


int target_pack_pool_reply(struct ptlrpc_request *req);
int do_set_info_async(struct obd_import *imp,
		      int opcode, int version,
		      obd_count keylen, void *key,
		      obd_count vallen, void *val,
		      struct ptlrpc_request_set *set);

#define OBD_RECOVERY_MAX_TIME (obd_timeout * 18) /* b13079 */
#define OBD_MAX_IOCTL_BUFFER CONFIG_LUSTRE_OBD_MAX_IOCTL_BUFFER

void target_send_reply(struct ptlrpc_request *req, int rc, int fail_id);

/* client.c */

int client_sanobd_setup(struct obd_device *obddev, struct lustre_cfg* lcfg);
struct client_obd *client_conn2cli(struct lustre_handle *conn);

struct md_open_data;
struct obd_client_handle {
	struct lustre_handle	 och_fh;
	struct lu_fid		 och_fid;
	struct md_open_data	*och_mod;
	struct lustre_handle	 och_lease_handle; /* open lock for lease */
	__u32			 och_magic;
	fmode_t			 och_flags;
};
#define OBD_CLIENT_HANDLE_MAGIC 0xd15ea5ed

/* statfs_pack.c */
void statfs_pack(struct obd_statfs *osfs, struct kstatfs *sfs);
void statfs_unpack(struct kstatfs *sfs, struct obd_statfs *osfs);

/* l_lock.c */
struct lustre_lock {
	int			l_depth;
	struct task_struct	*l_owner;
	struct semaphore	l_sem;
	spinlock_t		l_spin;
};

void l_lock_init(struct lustre_lock *);
void l_lock(struct lustre_lock *);
void l_unlock(struct lustre_lock *);
int l_has_lock(struct lustre_lock *);

/*
 * For md echo client
 */
enum md_echo_cmd {
	ECHO_MD_CREATE       = 1, /* Open/Create file on MDT */
	ECHO_MD_MKDIR	= 2, /* Mkdir on MDT */
	ECHO_MD_DESTROY      = 3, /* Unlink file on MDT */
	ECHO_MD_RMDIR	= 4, /* Rmdir on MDT */
	ECHO_MD_LOOKUP       = 5, /* Lookup on MDT */
	ECHO_MD_GETATTR      = 6, /* Getattr on MDT */
	ECHO_MD_SETATTR      = 7, /* Setattr on MDT */
	ECHO_MD_ALLOC_FID    = 8, /* Get FIDs from MDT */
};

/*
 *   OBD IOCTLS
 */
#define OBD_IOCTL_VERSION 0x00010004

struct obd_ioctl_data {
	__u32 ioc_len;
	__u32 ioc_version;

	union {
		__u64 ioc_cookie;
		__u64 ioc_u64_1;
	};
	union {
		__u32 ioc_conn1;
		__u32 ioc_u32_1;
	};
	union {
		__u32 ioc_conn2;
		__u32 ioc_u32_2;
	};

	struct obdo ioc_obdo1;
	struct obdo ioc_obdo2;

	obd_size ioc_count;
	obd_off  ioc_offset;
	__u32    ioc_dev;
	__u32    ioc_command;

	__u64 ioc_nid;
	__u32 ioc_nal;
	__u32 ioc_type;

	/* buffers the kernel will treat as user pointers */
	__u32  ioc_plen1;
	char  *ioc_pbuf1;
	__u32  ioc_plen2;
	char  *ioc_pbuf2;

	/* inline buffers for various arguments */
	__u32  ioc_inllen1;
	char  *ioc_inlbuf1;
	__u32  ioc_inllen2;
	char  *ioc_inlbuf2;
	__u32  ioc_inllen3;
	char  *ioc_inlbuf3;
	__u32  ioc_inllen4;
	char  *ioc_inlbuf4;

	char    ioc_bulk[0];
};

struct obd_ioctl_hdr {
	__u32 ioc_len;
	__u32 ioc_version;
};

static inline int obd_ioctl_packlen(struct obd_ioctl_data *data)
{
	int len = cfs_size_round(sizeof(struct obd_ioctl_data));
	len += cfs_size_round(data->ioc_inllen1);
	len += cfs_size_round(data->ioc_inllen2);
	len += cfs_size_round(data->ioc_inllen3);
	len += cfs_size_round(data->ioc_inllen4);
	return len;
}


static inline int obd_ioctl_is_invalid(struct obd_ioctl_data *data)
{
	if (data->ioc_len > (1<<30)) {
		CERROR("OBD ioctl: ioc_len larger than 1<<30\n");
		return 1;
	}
	if (data->ioc_inllen1 > (1<<30)) {
		CERROR("OBD ioctl: ioc_inllen1 larger than 1<<30\n");
		return 1;
	}
	if (data->ioc_inllen2 > (1<<30)) {
		CERROR("OBD ioctl: ioc_inllen2 larger than 1<<30\n");
		return 1;
	}
	if (data->ioc_inllen3 > (1<<30)) {
		CERROR("OBD ioctl: ioc_inllen3 larger than 1<<30\n");
		return 1;
	}
	if (data->ioc_inllen4 > (1<<30)) {
		CERROR("OBD ioctl: ioc_inllen4 larger than 1<<30\n");
		return 1;
	}
	if (data->ioc_inlbuf1 && !data->ioc_inllen1) {
		CERROR("OBD ioctl: inlbuf1 pointer but 0 length\n");
		return 1;
	}
	if (data->ioc_inlbuf2 && !data->ioc_inllen2) {
		CERROR("OBD ioctl: inlbuf2 pointer but 0 length\n");
		return 1;
	}
	if (data->ioc_inlbuf3 && !data->ioc_inllen3) {
		CERROR("OBD ioctl: inlbuf3 pointer but 0 length\n");
		return 1;
	}
	if (data->ioc_inlbuf4 && !data->ioc_inllen4) {
		CERROR("OBD ioctl: inlbuf4 pointer but 0 length\n");
		return 1;
	}
	if (data->ioc_pbuf1 && !data->ioc_plen1) {
		CERROR("OBD ioctl: pbuf1 pointer but 0 length\n");
		return 1;
	}
	if (data->ioc_pbuf2 && !data->ioc_plen2) {
		CERROR("OBD ioctl: pbuf2 pointer but 0 length\n");
		return 1;
	}
	if (data->ioc_plen1 && !data->ioc_pbuf1) {
		CERROR("OBD ioctl: plen1 set but NULL pointer\n");
		return 1;
	}
	if (data->ioc_plen2 && !data->ioc_pbuf2) {
		CERROR("OBD ioctl: plen2 set but NULL pointer\n");
		return 1;
	}
	if (obd_ioctl_packlen(data) > data->ioc_len) {
		CERROR("OBD ioctl: packlen exceeds ioc_len (%d > %d)\n",
		       obd_ioctl_packlen(data), data->ioc_len);
		return 1;
	}
	return 0;
}


#include <obd_support.h>

/* function defined in lustre/obdclass/<platform>/<platform>-module.c */
int obd_ioctl_getdata(char **buf, int *len, void *arg);
int obd_ioctl_popdata(void *arg, void *data, int len);

static inline void obd_ioctl_freedata(char *buf, int len)
{
	OBD_FREE_LARGE(buf, len);
	return;
}

/*
 * BSD ioctl description:
 * #define IOC_V1       _IOR(g, n1, long)
 * #define IOC_V2       _IOW(g, n2, long)
 *
 * ioctl(f, IOC_V1, arg);
 * arg will be treated as a long value,
 *
 * ioctl(f, IOC_V2, arg)
 * arg will be treated as a pointer, bsd will call
 * copyin(buf, arg, sizeof(long))
 *
 * To make BSD ioctl handles argument correctly and simplely,
 * we change _IOR to _IOWR so BSD will copyin obd_ioctl_data
 * for us. Does this change affect Linux?  (XXX Liang)
 */
#define OBD_IOC_CREATE		 _IOWR('f', 101, OBD_IOC_DATA_TYPE)
#define OBD_IOC_DESTROY		_IOW ('f', 104, OBD_IOC_DATA_TYPE)
#define OBD_IOC_PREALLOCATE	    _IOWR('f', 105, OBD_IOC_DATA_TYPE)

#define OBD_IOC_SETATTR		_IOW ('f', 107, OBD_IOC_DATA_TYPE)
#define OBD_IOC_GETATTR		_IOWR ('f', 108, OBD_IOC_DATA_TYPE)
#define OBD_IOC_READ		   _IOWR('f', 109, OBD_IOC_DATA_TYPE)
#define OBD_IOC_WRITE		  _IOWR('f', 110, OBD_IOC_DATA_TYPE)


#define OBD_IOC_STATFS		 _IOWR('f', 113, OBD_IOC_DATA_TYPE)
#define OBD_IOC_SYNC		   _IOW ('f', 114, OBD_IOC_DATA_TYPE)
#define OBD_IOC_READ2		  _IOWR('f', 115, OBD_IOC_DATA_TYPE)
#define OBD_IOC_FORMAT		 _IOWR('f', 116, OBD_IOC_DATA_TYPE)
#define OBD_IOC_PARTITION	      _IOWR('f', 117, OBD_IOC_DATA_TYPE)
#define OBD_IOC_COPY		   _IOWR('f', 120, OBD_IOC_DATA_TYPE)
#define OBD_IOC_MIGR		   _IOWR('f', 121, OBD_IOC_DATA_TYPE)
#define OBD_IOC_PUNCH		  _IOWR('f', 122, OBD_IOC_DATA_TYPE)

#define OBD_IOC_MODULE_DEBUG	   _IOWR('f', 124, OBD_IOC_DATA_TYPE)
#define OBD_IOC_BRW_READ	       _IOWR('f', 125, OBD_IOC_DATA_TYPE)
#define OBD_IOC_BRW_WRITE	      _IOWR('f', 126, OBD_IOC_DATA_TYPE)
#define OBD_IOC_NAME2DEV	       _IOWR('f', 127, OBD_IOC_DATA_TYPE)
#define OBD_IOC_UUID2DEV	       _IOWR('f', 130, OBD_IOC_DATA_TYPE)

#define OBD_IOC_GETNAME		_IOWR('f', 131, OBD_IOC_DATA_TYPE)
#define OBD_IOC_GETMDNAME	      _IOR('f', 131, char[MAX_OBD_NAME])
#define OBD_IOC_GETDTNAME	       OBD_IOC_GETNAME

#define OBD_IOC_LOV_GET_CONFIG	 _IOWR('f', 132, OBD_IOC_DATA_TYPE)
#define OBD_IOC_CLIENT_RECOVER	 _IOW ('f', 133, OBD_IOC_DATA_TYPE)
#define OBD_IOC_PING_TARGET	    _IOW ('f', 136, OBD_IOC_DATA_TYPE)

#define OBD_IOC_DEC_FS_USE_COUNT       _IO  ('f', 139      )
#define OBD_IOC_NO_TRANSNO	     _IOW ('f', 140, OBD_IOC_DATA_TYPE)
#define OBD_IOC_SET_READONLY	   _IOW ('f', 141, OBD_IOC_DATA_TYPE)
#define OBD_IOC_ABORT_RECOVERY	 _IOR ('f', 142, OBD_IOC_DATA_TYPE)

#define OBD_IOC_ROOT_SQUASH	    _IOWR('f', 143, OBD_IOC_DATA_TYPE)

#define OBD_GET_VERSION		_IOWR ('f', 144, OBD_IOC_DATA_TYPE)

#define OBD_IOC_GSS_SUPPORT	    _IOWR('f', 145, OBD_IOC_DATA_TYPE)

#define OBD_IOC_CLOSE_UUID	     _IOWR ('f', 147, OBD_IOC_DATA_TYPE)

#define OBD_IOC_CHANGELOG_SEND	 _IOW ('f', 148, OBD_IOC_DATA_TYPE)
#define OBD_IOC_GETDEVICE	      _IOWR ('f', 149, OBD_IOC_DATA_TYPE)
#define OBD_IOC_FID2PATH	       _IOWR ('f', 150, OBD_IOC_DATA_TYPE)
/* see also <lustre/lustre_user.h> for ioctls 151-153 */
/* OBD_IOC_LOV_SETSTRIPE: See also LL_IOC_LOV_SETSTRIPE */
#define OBD_IOC_LOV_SETSTRIPE	  _IOW ('f', 154, OBD_IOC_DATA_TYPE)
/* OBD_IOC_LOV_GETSTRIPE: See also LL_IOC_LOV_GETSTRIPE */
#define OBD_IOC_LOV_GETSTRIPE	  _IOW ('f', 155, OBD_IOC_DATA_TYPE)
/* OBD_IOC_LOV_SETEA: See also LL_IOC_LOV_SETEA */
#define OBD_IOC_LOV_SETEA	      _IOW ('f', 156, OBD_IOC_DATA_TYPE)
/* see <lustre/lustre_user.h> for ioctls 157-159 */
/* OBD_IOC_QUOTACHECK: See also LL_IOC_QUOTACHECK */
#define OBD_IOC_QUOTACHECK	     _IOW ('f', 160, int)
/* OBD_IOC_POLL_QUOTACHECK: See also LL_IOC_POLL_QUOTACHECK */
#define OBD_IOC_POLL_QUOTACHECK	_IOR ('f', 161, struct if_quotacheck *)
/* OBD_IOC_QUOTACTL: See also LL_IOC_QUOTACTL */
#define OBD_IOC_QUOTACTL	       _IOWR('f', 162, struct if_quotactl)
/* see  also <lustre/lustre_user.h> for ioctls 163-176 */
#define OBD_IOC_CHANGELOG_REG	  _IOW ('f', 177, struct obd_ioctl_data)
#define OBD_IOC_CHANGELOG_DEREG	_IOW ('f', 178, struct obd_ioctl_data)
#define OBD_IOC_CHANGELOG_CLEAR	_IOW ('f', 179, struct obd_ioctl_data)
#define OBD_IOC_RECORD		 _IOWR('f', 180, OBD_IOC_DATA_TYPE)
#define OBD_IOC_ENDRECORD	      _IOWR('f', 181, OBD_IOC_DATA_TYPE)
#define OBD_IOC_PARSE		  _IOWR('f', 182, OBD_IOC_DATA_TYPE)
#define OBD_IOC_DORECORD	       _IOWR('f', 183, OBD_IOC_DATA_TYPE)
#define OBD_IOC_PROCESS_CFG	    _IOWR('f', 184, OBD_IOC_DATA_TYPE)
#define OBD_IOC_DUMP_LOG	       _IOWR('f', 185, OBD_IOC_DATA_TYPE)
#define OBD_IOC_CLEAR_LOG	      _IOWR('f', 186, OBD_IOC_DATA_TYPE)
#define OBD_IOC_PARAM		  _IOW ('f', 187, OBD_IOC_DATA_TYPE)
#define OBD_IOC_POOL		   _IOWR('f', 188, OBD_IOC_DATA_TYPE)
#define OBD_IOC_REPLACE_NIDS	   _IOWR('f', 189, OBD_IOC_DATA_TYPE)

#define OBD_IOC_CATLOGLIST	     _IOWR('f', 190, OBD_IOC_DATA_TYPE)
#define OBD_IOC_LLOG_INFO	      _IOWR('f', 191, OBD_IOC_DATA_TYPE)
#define OBD_IOC_LLOG_PRINT	     _IOWR('f', 192, OBD_IOC_DATA_TYPE)
#define OBD_IOC_LLOG_CANCEL	    _IOWR('f', 193, OBD_IOC_DATA_TYPE)
#define OBD_IOC_LLOG_REMOVE	    _IOWR('f', 194, OBD_IOC_DATA_TYPE)
#define OBD_IOC_LLOG_CHECK	     _IOWR('f', 195, OBD_IOC_DATA_TYPE)
/* OBD_IOC_LLOG_CATINFO is deprecated */
#define OBD_IOC_LLOG_CATINFO	   _IOWR('f', 196, OBD_IOC_DATA_TYPE)

#define ECHO_IOC_GET_STRIPE	    _IOWR('f', 200, OBD_IOC_DATA_TYPE)
#define ECHO_IOC_SET_STRIPE	    _IOWR('f', 201, OBD_IOC_DATA_TYPE)
#define ECHO_IOC_ENQUEUE	       _IOWR('f', 202, OBD_IOC_DATA_TYPE)
#define ECHO_IOC_CANCEL		_IOWR('f', 203, OBD_IOC_DATA_TYPE)

#define OBD_IOC_GET_OBJ_VERSION	_IOR('f', 210, OBD_IOC_DATA_TYPE)

/* <lustre/lustre_user.h> defines ioctl number 218-219 */
#define OBD_IOC_GET_MNTOPT	     _IOW('f', 220, mntopt_t)

#define OBD_IOC_ECHO_MD		_IOR('f', 221, struct obd_ioctl_data)
#define OBD_IOC_ECHO_ALLOC_SEQ	 _IOWR('f', 222, struct obd_ioctl_data)

#define OBD_IOC_START_LFSCK	       _IOWR('f', 230, OBD_IOC_DATA_TYPE)
#define OBD_IOC_STOP_LFSCK	       _IOW('f', 231, OBD_IOC_DATA_TYPE)
#define OBD_IOC_PAUSE_LFSCK	       _IOW('f', 232, OBD_IOC_DATA_TYPE)

/* XXX _IOWR('f', 250, long) has been defined in
 * libcfs/include/libcfs/libcfs_private.h for debug, don't use it
 */

/* Until such time as we get_info the per-stripe maximum from the OST,
 * we define this to be 2T - 4k, which is the ext3 maxbytes. */
#define LUSTRE_STRIPE_MAXBYTES 0x1fffffff000ULL

/* Special values for remove LOV EA from disk */
#define LOVEA_DELETE_VALUES(size, count, offset) (size == 0 && count == 0 && \
						 offset == (typeof(offset))(-1))

/* #define POISON_BULK 0 */

/*
 * l_wait_event is a flexible sleeping function, permitting simple caller
 * configuration of interrupt and timeout sensitivity along with actions to
 * be performed in the event of either exception.
 *
 * The first form of usage looks like this:
 *
 * struct l_wait_info lwi = LWI_TIMEOUT_INTR(timeout, timeout_handler,
 *					   intr_handler, callback_data);
 * rc = l_wait_event(waitq, condition, &lwi);
 *
 * l_wait_event() makes the current process wait on 'waitq' until 'condition'
 * is TRUE or a "killable" signal (SIGTERM, SIKGILL, SIGINT) is pending.  It
 * returns 0 to signify 'condition' is TRUE, but if a signal wakes it before
 * 'condition' becomes true, it optionally calls the specified 'intr_handler'
 * if not NULL, and returns -EINTR.
 *
 * If a non-zero timeout is specified, signals are ignored until the timeout
 * has expired.  At this time, if 'timeout_handler' is not NULL it is called.
 * If it returns FALSE l_wait_event() continues to wait as described above with
 * signals enabled.  Otherwise it returns -ETIMEDOUT.
 *
 * LWI_INTR(intr_handler, callback_data) is shorthand for
 * LWI_TIMEOUT_INTR(0, NULL, intr_handler, callback_data)
 *
 * The second form of usage looks like this:
 *
 * struct l_wait_info lwi = LWI_TIMEOUT(timeout, timeout_handler);
 * rc = l_wait_event(waitq, condition, &lwi);
 *
 * This form is the same as the first except that it COMPLETELY IGNORES
 * SIGNALS.  The caller must therefore beware that if 'timeout' is zero, or if
 * 'timeout_handler' is not NULL and returns FALSE, then the ONLY thing that
 * can unblock the current process is 'condition' becoming TRUE.
 *
 * Another form of usage is:
 * struct l_wait_info lwi = LWI_TIMEOUT_INTERVAL(timeout, interval,
 *					       timeout_handler);
 * rc = l_wait_event(waitq, condition, &lwi);
 * This is the same as previous case, but condition is checked once every
 * 'interval' jiffies (if non-zero).
 *
 * Subtle synchronization point: this macro does *not* necessary takes
 * wait-queue spin-lock before returning, and, hence, following idiom is safe
 * ONLY when caller provides some external locking:
 *
 *	     Thread1			    Thread2
 *
 *   l_wait_event(&obj->wq, ....);				       (1)
 *
 *				    wake_up(&obj->wq):		 (2)
 *					 spin_lock(&q->lock);	  (2.1)
 *					 __wake_up_common(q, ...);     (2.2)
 *					 spin_unlock(&q->lock, flags); (2.3)
 *
 *   OBD_FREE_PTR(obj);						  (3)
 *
 * As l_wait_event() may "short-cut" execution and return without taking
 * wait-queue spin-lock, some additional synchronization is necessary to
 * guarantee that step (3) can begin only after (2.3) finishes.
 *
 * XXX nikita: some ptlrpc daemon threads have races of that sort.
 *
 */
static inline int back_to_sleep(void *arg)
{
	return 0;
}

#define LWI_ON_SIGNAL_NOOP ((void (*)(void *))(-1))

struct l_wait_info {
	cfs_duration_t lwi_timeout;
	cfs_duration_t lwi_interval;
	int	    lwi_allow_intr;
	int  (*lwi_on_timeout)(void *);
	void (*lwi_on_signal)(void *);
	void  *lwi_cb_data;
};

/* NB: LWI_TIMEOUT ignores signals completely */
#define LWI_TIMEOUT(time, cb, data)	     \
((struct l_wait_info) {			 \
	.lwi_timeout    = time,		 \
	.lwi_on_timeout = cb,		   \
	.lwi_cb_data    = data,		 \
	.lwi_interval   = 0,		    \
	.lwi_allow_intr = 0		     \
})

#define LWI_TIMEOUT_INTERVAL(time, interval, cb, data)  \
((struct l_wait_info) {				 \
	.lwi_timeout    = time,			 \
	.lwi_on_timeout = cb,			   \
	.lwi_cb_data    = data,			 \
	.lwi_interval   = interval,		     \
	.lwi_allow_intr = 0			     \
})

#define LWI_TIMEOUT_INTR(time, time_cb, sig_cb, data)   \
((struct l_wait_info) {				 \
	.lwi_timeout    = time,			 \
	.lwi_on_timeout = time_cb,		      \
	.lwi_on_signal  = sig_cb,		       \
	.lwi_cb_data    = data,			 \
	.lwi_interval   = 0,			    \
	.lwi_allow_intr = 0			     \
})

#define LWI_TIMEOUT_INTR_ALL(time, time_cb, sig_cb, data)       \
((struct l_wait_info) {					 \
	.lwi_timeout    = time,				 \
	.lwi_on_timeout = time_cb,			      \
	.lwi_on_signal  = sig_cb,			       \
	.lwi_cb_data    = data,				 \
	.lwi_interval   = 0,				    \
	.lwi_allow_intr = 1				     \
})

#define LWI_INTR(cb, data)  LWI_TIMEOUT_INTR(0, NULL, cb, data)


/*
 * wait for @condition to become true, but no longer than timeout, specified
 * by @info.
 */
#define __l_wait_event(wq, condition, info, ret, l_add_wait)		   \
do {									   \
	wait_queue_t __wait;						 \
	cfs_duration_t __timeout = info->lwi_timeout;			  \
	sigset_t   __blocked;					      \
	int   __allow_intr = info->lwi_allow_intr;			     \
									       \
	ret = 0;							       \
	if (condition)							 \
		break;							 \
									       \
	init_waitqueue_entry(&__wait, current);					    \
	l_add_wait(&wq, &__wait);					      \
									       \
	/* Block all signals (just the non-fatal ones if no timeout). */       \
	if (info->lwi_on_signal != NULL && (__timeout == 0 || __allow_intr))   \
		__blocked = cfs_block_sigsinv(LUSTRE_FATAL_SIGS);	      \
	else								   \
		__blocked = cfs_block_sigsinv(0);			      \
									       \
	for (;;) {							     \
		unsigned       __wstate;				       \
									       \
		__wstate = info->lwi_on_signal != NULL &&		      \
			   (__timeout == 0 || __allow_intr) ?		  \
			TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE;	       \
									       \
		set_current_state(TASK_INTERRUPTIBLE);		 \
									       \
		if (condition)						 \
			break;						 \
									       \
		if (__timeout == 0) {					  \
			schedule();						\
		} else {						       \
			cfs_duration_t interval = info->lwi_interval?	  \
					     min_t(cfs_duration_t,	     \
						 info->lwi_interval,__timeout):\
					     __timeout;			\
			cfs_duration_t remaining = schedule_timeout(interval);\
			__timeout = cfs_time_sub(__timeout,		    \
					    cfs_time_sub(interval, remaining));\
			if (__timeout == 0) {				  \
				if (info->lwi_on_timeout == NULL ||	    \
				    info->lwi_on_timeout(info->lwi_cb_data)) { \
					ret = -ETIMEDOUT;		      \
					break;				 \
				}					      \
				/* Take signals after the timeout expires. */  \
				if (info->lwi_on_signal != NULL)	       \
				    (void)cfs_block_sigsinv(LUSTRE_FATAL_SIGS);\
			}						      \
		}							      \
									       \
		if (condition)						 \
			break;						 \
		if (cfs_signal_pending()) {				    \
			if (info->lwi_on_signal != NULL &&		     \
			    (__timeout == 0 || __allow_intr)) {		\
				if (info->lwi_on_signal != LWI_ON_SIGNAL_NOOP) \
					info->lwi_on_signal(info->lwi_cb_data);\
				ret = -EINTR;				  \
				break;					 \
			}						      \
			/* We have to do this here because some signals */     \
			/* are not blockable - ie from strace(1).       */     \
			/* In these cases we want to schedule_timeout() */     \
			/* again, because we don't want that to return  */     \
			/* -EINTR when the RPC actually succeeded.      */     \
			/* the recalc_sigpending() below will deliver the */     \
			/* signal properly.			     */     \
			cfs_clear_sigpending();				\
		}							      \
	}								      \
									       \
	cfs_restore_sigs(__blocked);					   \
									       \
	set_current_state(TASK_RUNNING);			       \
	remove_wait_queue(&wq, &__wait);					   \
} while (0)



#define l_wait_event(wq, condition, info)		       \
({							      \
	int		 __ret;			      \
	struct l_wait_info *__info = (info);		    \
								\
	__l_wait_event(wq, condition, __info,		   \
		       __ret, add_wait_queue);		   \
	__ret;						  \
})

#define l_wait_event_exclusive(wq, condition, info)	     \
({							      \
	int		 __ret;			      \
	struct l_wait_info *__info = (info);		    \
								\
	__l_wait_event(wq, condition, __info,		   \
		       __ret, add_wait_queue_exclusive);	 \
	__ret;						  \
})

#define l_wait_event_exclusive_head(wq, condition, info)	\
({							      \
	int		 __ret;			      \
	struct l_wait_info *__info = (info);		    \
								\
	__l_wait_event(wq, condition, __info,		   \
		       __ret, add_wait_queue_exclusive_head);    \
	__ret;						  \
})

#define l_wait_condition(wq, condition)			 \
({							      \
	struct l_wait_info lwi = { 0 };			 \
	l_wait_event(wq, condition, &lwi);		      \
})

#define l_wait_condition_exclusive(wq, condition)	       \
({							      \
	struct l_wait_info lwi = { 0 };			 \
	l_wait_event_exclusive(wq, condition, &lwi);	    \
})

#define l_wait_condition_exclusive_head(wq, condition)	  \
({							      \
	struct l_wait_info lwi = { 0 };			 \
	l_wait_event_exclusive_head(wq, condition, &lwi);       \
})

#define LIBLUSTRE_CLIENT (0)

/** @} lib */

#endif /* _LUSTRE_LIB_H */

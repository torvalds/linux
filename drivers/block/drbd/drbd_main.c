/*
   drbd.c

   This file is part of DRBD by Philipp Reisner and Lars Ellenberg.

   Copyright (C) 2001-2008, LINBIT Information Technologies GmbH.
   Copyright (C) 1999-2008, Philipp Reisner <philipp.reisner@linbit.com>.
   Copyright (C) 2002-2008, Lars Ellenberg <lars.ellenberg@linbit.com>.

   Thanks to Carter Burden, Bart Grantham and Gennadiy Nerubayev
   from Logicworks, Inc. for making SDP replication support possible.

   drbd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   drbd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with drbd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 */

#include <linux/module.h>
#include <linux/drbd.h>
#include <asm/uaccess.h>
#include <asm/types.h>
#include <net/sock.h>
#include <linux/ctype.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/memcontrol.h>
#include <linux/mm_inline.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/reboot.h>
#include <linux/notifier.h>
#include <linux/kthread.h>

#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>
#include <linux/vmalloc.h>

#include <linux/drbd_limits.h>
#include "drbd_int.h"
#include "drbd_req.h" /* only for _req_mod in tl_release and tl_clear */

#include "drbd_vli.h"

struct after_state_chg_work {
	struct drbd_work w;
	union drbd_state os;
	union drbd_state ns;
	enum chg_state_flags flags;
	struct completion *done;
};

static DEFINE_MUTEX(drbd_main_mutex);
int drbdd_init(struct drbd_thread *);
int drbd_worker(struct drbd_thread *);
int drbd_asender(struct drbd_thread *);

int drbd_init(void);
static int drbd_open(struct block_device *bdev, fmode_t mode);
static int drbd_release(struct gendisk *gd, fmode_t mode);
static int w_after_state_ch(struct drbd_conf *mdev, struct drbd_work *w, int unused);
static void after_state_ch(struct drbd_conf *mdev, union drbd_state os,
			   union drbd_state ns, enum chg_state_flags flags);
static int w_md_sync(struct drbd_conf *mdev, struct drbd_work *w, int unused);
static void md_sync_timer_fn(unsigned long data);
static int w_bitmap_io(struct drbd_conf *mdev, struct drbd_work *w, int unused);
static int w_go_diskless(struct drbd_conf *mdev, struct drbd_work *w, int unused);
static void _tl_clear(struct drbd_conf *mdev);

MODULE_AUTHOR("Philipp Reisner <phil@linbit.com>, "
	      "Lars Ellenberg <lars@linbit.com>");
MODULE_DESCRIPTION("drbd - Distributed Replicated Block Device v" REL_VERSION);
MODULE_VERSION(REL_VERSION);
MODULE_LICENSE("GPL");
MODULE_PARM_DESC(minor_count, "Maximum number of drbd devices ("
		 __stringify(DRBD_MINOR_COUNT_MIN) "-" __stringify(DRBD_MINOR_COUNT_MAX) ")");
MODULE_ALIAS_BLOCKDEV_MAJOR(DRBD_MAJOR);

#include <linux/moduleparam.h>
/* allow_open_on_secondary */
MODULE_PARM_DESC(allow_oos, "DONT USE!");
/* thanks to these macros, if compiled into the kernel (not-module),
 * this becomes the boot parameter drbd.minor_count */
module_param(minor_count, uint, 0444);
module_param(disable_sendpage, bool, 0644);
module_param(allow_oos, bool, 0);
module_param(cn_idx, uint, 0444);
module_param(proc_details, int, 0644);

#ifdef CONFIG_DRBD_FAULT_INJECTION
int enable_faults;
int fault_rate;
static int fault_count;
int fault_devs;
/* bitmap of enabled faults */
module_param(enable_faults, int, 0664);
/* fault rate % value - applies to all enabled faults */
module_param(fault_rate, int, 0664);
/* count of faults inserted */
module_param(fault_count, int, 0664);
/* bitmap of devices to insert faults on */
module_param(fault_devs, int, 0644);
#endif

/* module parameter, defined */
unsigned int minor_count = DRBD_MINOR_COUNT_DEF;
bool disable_sendpage;
bool allow_oos;
unsigned int cn_idx = CN_IDX_DRBD;
int proc_details;       /* Detail level in proc drbd*/

/* Module parameter for setting the user mode helper program
 * to run. Default is /sbin/drbdadm */
char usermode_helper[80] = "/sbin/drbdadm";

module_param_string(usermode_helper, usermode_helper, sizeof(usermode_helper), 0644);

/* in 2.6.x, our device mapping and config info contains our virtual gendisks
 * as member "struct gendisk *vdisk;"
 */
struct drbd_conf **minor_table;

struct kmem_cache *drbd_request_cache;
struct kmem_cache *drbd_ee_cache;	/* epoch entries */
struct kmem_cache *drbd_bm_ext_cache;	/* bitmap extents */
struct kmem_cache *drbd_al_ext_cache;	/* activity log extents */
mempool_t *drbd_request_mempool;
mempool_t *drbd_ee_mempool;
mempool_t *drbd_md_io_page_pool;
struct bio_set *drbd_md_io_bio_set;

/* I do not use a standard mempool, because:
   1) I want to hand out the pre-allocated objects first.
   2) I want to be able to interrupt sleeping allocation with a signal.
   Note: This is a single linked list, the next pointer is the private
	 member of struct page.
 */
struct page *drbd_pp_pool;
spinlock_t   drbd_pp_lock;
int          drbd_pp_vacant;
wait_queue_head_t drbd_pp_wait;

DEFINE_RATELIMIT_STATE(drbd_ratelimit_state, 5 * HZ, 5);

static const struct block_device_operations drbd_ops = {
	.owner =   THIS_MODULE,
	.open =    drbd_open,
	.release = drbd_release,
};

struct bio *bio_alloc_drbd(gfp_t gfp_mask)
{
	if (!drbd_md_io_bio_set)
		return bio_alloc(gfp_mask, 1);

	return bio_alloc_bioset(gfp_mask, 1, drbd_md_io_bio_set);
}

#ifdef __CHECKER__
/* When checking with sparse, and this is an inline function, sparse will
   give tons of false positives. When this is a real functions sparse works.
 */
int _get_ldev_if_state(struct drbd_conf *mdev, enum drbd_disk_state mins)
{
	int io_allowed;

	atomic_inc(&mdev->local_cnt);
	io_allowed = (mdev->state.disk >= mins);
	if (!io_allowed) {
		if (atomic_dec_and_test(&mdev->local_cnt))
			wake_up(&mdev->misc_wait);
	}
	return io_allowed;
}

#endif

/**
 * DOC: The transfer log
 *
 * The transfer log is a single linked list of &struct drbd_tl_epoch objects.
 * mdev->newest_tle points to the head, mdev->oldest_tle points to the tail
 * of the list. There is always at least one &struct drbd_tl_epoch object.
 *
 * Each &struct drbd_tl_epoch has a circular double linked list of requests
 * attached.
 */
static int tl_init(struct drbd_conf *mdev)
{
	struct drbd_tl_epoch *b;

	/* during device minor initialization, we may well use GFP_KERNEL */
	b = kmalloc(sizeof(struct drbd_tl_epoch), GFP_KERNEL);
	if (!b)
		return 0;
	INIT_LIST_HEAD(&b->requests);
	INIT_LIST_HEAD(&b->w.list);
	b->next = NULL;
	b->br_number = 4711;
	b->n_writes = 0;
	b->w.cb = NULL; /* if this is != NULL, we need to dec_ap_pending in tl_clear */

	mdev->oldest_tle = b;
	mdev->newest_tle = b;
	INIT_LIST_HEAD(&mdev->out_of_sequence_requests);
	INIT_LIST_HEAD(&mdev->barrier_acked_requests);

	mdev->tl_hash = NULL;
	mdev->tl_hash_s = 0;

	return 1;
}

static void tl_cleanup(struct drbd_conf *mdev)
{
	D_ASSERT(mdev->oldest_tle == mdev->newest_tle);
	D_ASSERT(list_empty(&mdev->out_of_sequence_requests));
	kfree(mdev->oldest_tle);
	mdev->oldest_tle = NULL;
	kfree(mdev->unused_spare_tle);
	mdev->unused_spare_tle = NULL;
	kfree(mdev->tl_hash);
	mdev->tl_hash = NULL;
	mdev->tl_hash_s = 0;
}

/**
 * _tl_add_barrier() - Adds a barrier to the transfer log
 * @mdev:	DRBD device.
 * @new:	Barrier to be added before the current head of the TL.
 *
 * The caller must hold the req_lock.
 */
void _tl_add_barrier(struct drbd_conf *mdev, struct drbd_tl_epoch *new)
{
	struct drbd_tl_epoch *newest_before;

	INIT_LIST_HEAD(&new->requests);
	INIT_LIST_HEAD(&new->w.list);
	new->w.cb = NULL; /* if this is != NULL, we need to dec_ap_pending in tl_clear */
	new->next = NULL;
	new->n_writes = 0;

	newest_before = mdev->newest_tle;
	new->br_number = newest_before->br_number+1;
	if (mdev->newest_tle != new) {
		mdev->newest_tle->next = new;
		mdev->newest_tle = new;
	}
}

/**
 * tl_release() - Free or recycle the oldest &struct drbd_tl_epoch object of the TL
 * @mdev:	DRBD device.
 * @barrier_nr:	Expected identifier of the DRBD write barrier packet.
 * @set_size:	Expected number of requests before that barrier.
 *
 * In case the passed barrier_nr or set_size does not match the oldest
 * &struct drbd_tl_epoch objects this function will cause a termination
 * of the connection.
 */
void tl_release(struct drbd_conf *mdev, unsigned int barrier_nr,
		       unsigned int set_size)
{
	struct drbd_tl_epoch *b, *nob; /* next old barrier */
	struct list_head *le, *tle;
	struct drbd_request *r;

	spin_lock_irq(&mdev->req_lock);

	b = mdev->oldest_tle;

	/* first some paranoia code */
	if (b == NULL) {
		dev_err(DEV, "BAD! BarrierAck #%u received, but no epoch in tl!?\n",
			barrier_nr);
		goto bail;
	}
	if (b->br_number != barrier_nr) {
		dev_err(DEV, "BAD! BarrierAck #%u received, expected #%u!\n",
			barrier_nr, b->br_number);
		goto bail;
	}
	if (b->n_writes != set_size) {
		dev_err(DEV, "BAD! BarrierAck #%u received with n_writes=%u, expected n_writes=%u!\n",
			barrier_nr, set_size, b->n_writes);
		goto bail;
	}

	/* Clean up list of requests processed during current epoch */
	list_for_each_safe(le, tle, &b->requests) {
		r = list_entry(le, struct drbd_request, tl_requests);
		_req_mod(r, barrier_acked);
	}
	/* There could be requests on the list waiting for completion
	   of the write to the local disk. To avoid corruptions of
	   slab's data structures we have to remove the lists head.

	   Also there could have been a barrier ack out of sequence, overtaking
	   the write acks - which would be a bug and violating write ordering.
	   To not deadlock in case we lose connection while such requests are
	   still pending, we need some way to find them for the
	   _req_mode(connection_lost_while_pending).

	   These have been list_move'd to the out_of_sequence_requests list in
	   _req_mod(, barrier_acked) above.
	   */
	list_splice_init(&b->requests, &mdev->barrier_acked_requests);

	nob = b->next;
	if (test_and_clear_bit(CREATE_BARRIER, &mdev->flags)) {
		_tl_add_barrier(mdev, b);
		if (nob)
			mdev->oldest_tle = nob;
		/* if nob == NULL b was the only barrier, and becomes the new
		   barrier. Therefore mdev->oldest_tle points already to b */
	} else {
		D_ASSERT(nob != NULL);
		mdev->oldest_tle = nob;
		kfree(b);
	}

	spin_unlock_irq(&mdev->req_lock);
	dec_ap_pending(mdev);

	return;

bail:
	spin_unlock_irq(&mdev->req_lock);
	drbd_force_state(mdev, NS(conn, C_PROTOCOL_ERROR));
}


/**
 * _tl_restart() - Walks the transfer log, and applies an action to all requests
 * @mdev:	DRBD device.
 * @what:       The action/event to perform with all request objects
 *
 * @what might be one of connection_lost_while_pending, resend, fail_frozen_disk_io,
 * restart_frozen_disk_io.
 */
static void _tl_restart(struct drbd_conf *mdev, enum drbd_req_event what)
{
	struct drbd_tl_epoch *b, *tmp, **pn;
	struct list_head *le, *tle, carry_reads;
	struct drbd_request *req;
	int rv, n_writes, n_reads;

	b = mdev->oldest_tle;
	pn = &mdev->oldest_tle;
	while (b) {
		n_writes = 0;
		n_reads = 0;
		INIT_LIST_HEAD(&carry_reads);
		list_for_each_safe(le, tle, &b->requests) {
			req = list_entry(le, struct drbd_request, tl_requests);
			rv = _req_mod(req, what);

			n_writes += (rv & MR_WRITE) >> MR_WRITE_SHIFT;
			n_reads  += (rv & MR_READ) >> MR_READ_SHIFT;
		}
		tmp = b->next;

		if (n_writes) {
			if (what == resend) {
				b->n_writes = n_writes;
				if (b->w.cb == NULL) {
					b->w.cb = w_send_barrier;
					inc_ap_pending(mdev);
					set_bit(CREATE_BARRIER, &mdev->flags);
				}

				drbd_queue_work(&mdev->data.work, &b->w);
			}
			pn = &b->next;
		} else {
			if (n_reads)
				list_add(&carry_reads, &b->requests);
			/* there could still be requests on that ring list,
			 * in case local io is still pending */
			list_del(&b->requests);

			/* dec_ap_pending corresponding to queue_barrier.
			 * the newest barrier may not have been queued yet,
			 * in which case w.cb is still NULL. */
			if (b->w.cb != NULL)
				dec_ap_pending(mdev);

			if (b == mdev->newest_tle) {
				/* recycle, but reinit! */
				D_ASSERT(tmp == NULL);
				INIT_LIST_HEAD(&b->requests);
				list_splice(&carry_reads, &b->requests);
				INIT_LIST_HEAD(&b->w.list);
				b->w.cb = NULL;
				b->br_number = net_random();
				b->n_writes = 0;

				*pn = b;
				break;
			}
			*pn = tmp;
			kfree(b);
		}
		b = tmp;
		list_splice(&carry_reads, &b->requests);
	}

	/* Actions operating on the disk state, also want to work on
	   requests that got barrier acked. */

	list_for_each_safe(le, tle, &mdev->barrier_acked_requests) {
		req = list_entry(le, struct drbd_request, tl_requests);
		_req_mod(req, what);
	}
}


/**
 * tl_clear() - Clears all requests and &struct drbd_tl_epoch objects out of the TL
 * @mdev:	DRBD device.
 *
 * This is called after the connection to the peer was lost. The storage covered
 * by the requests on the transfer gets marked as our of sync. Called from the
 * receiver thread and the worker thread.
 */
void tl_clear(struct drbd_conf *mdev)
{
	spin_lock_irq(&mdev->req_lock);
	_tl_clear(mdev);
	spin_unlock_irq(&mdev->req_lock);
}

static void _tl_clear(struct drbd_conf *mdev)
{
	struct list_head *le, *tle;
	struct drbd_request *r;

	_tl_restart(mdev, connection_lost_while_pending);

	/* we expect this list to be empty. */
	D_ASSERT(list_empty(&mdev->out_of_sequence_requests));

	/* but just in case, clean it up anyways! */
	list_for_each_safe(le, tle, &mdev->out_of_sequence_requests) {
		r = list_entry(le, struct drbd_request, tl_requests);
		/* It would be nice to complete outside of spinlock.
		 * But this is easier for now. */
		_req_mod(r, connection_lost_while_pending);
	}

	/* ensure bit indicating barrier is required is clear */
	clear_bit(CREATE_BARRIER, &mdev->flags);

	memset(mdev->app_reads_hash, 0, APP_R_HSIZE*sizeof(void *));

}

void tl_restart(struct drbd_conf *mdev, enum drbd_req_event what)
{
	spin_lock_irq(&mdev->req_lock);
	_tl_restart(mdev, what);
	spin_unlock_irq(&mdev->req_lock);
}

/**
 * tl_abort_disk_io() - Abort disk I/O for all requests for a certain mdev in the TL
 * @mdev:	DRBD device.
 */
void tl_abort_disk_io(struct drbd_conf *mdev)
{
	struct drbd_tl_epoch *b;
	struct list_head *le, *tle;
	struct drbd_request *req;

	spin_lock_irq(&mdev->req_lock);
	b = mdev->oldest_tle;
	while (b) {
		list_for_each_safe(le, tle, &b->requests) {
			req = list_entry(le, struct drbd_request, tl_requests);
			if (!(req->rq_state & RQ_LOCAL_PENDING))
				continue;
			_req_mod(req, abort_disk_io);
		}
		b = b->next;
	}

	list_for_each_safe(le, tle, &mdev->barrier_acked_requests) {
		req = list_entry(le, struct drbd_request, tl_requests);
		if (!(req->rq_state & RQ_LOCAL_PENDING))
			continue;
		_req_mod(req, abort_disk_io);
	}

	spin_unlock_irq(&mdev->req_lock);
}

/**
 * cl_wide_st_chg() - true if the state change is a cluster wide one
 * @mdev:	DRBD device.
 * @os:		old (current) state.
 * @ns:		new (wanted) state.
 */
static int cl_wide_st_chg(struct drbd_conf *mdev,
			  union drbd_state os, union drbd_state ns)
{
	return (os.conn >= C_CONNECTED && ns.conn >= C_CONNECTED &&
		 ((os.role != R_PRIMARY && ns.role == R_PRIMARY) ||
		  (os.conn != C_STARTING_SYNC_T && ns.conn == C_STARTING_SYNC_T) ||
		  (os.conn != C_STARTING_SYNC_S && ns.conn == C_STARTING_SYNC_S) ||
		  (os.disk != D_FAILED && ns.disk == D_FAILED))) ||
		(os.conn >= C_CONNECTED && ns.conn == C_DISCONNECTING) ||
		(os.conn == C_CONNECTED && ns.conn == C_VERIFY_S);
}

enum drbd_state_rv
drbd_change_state(struct drbd_conf *mdev, enum chg_state_flags f,
		  union drbd_state mask, union drbd_state val)
{
	unsigned long flags;
	union drbd_state os, ns;
	enum drbd_state_rv rv;

	spin_lock_irqsave(&mdev->req_lock, flags);
	os = mdev->state;
	ns.i = (os.i & ~mask.i) | val.i;
	rv = _drbd_set_state(mdev, ns, f, NULL);
	ns = mdev->state;
	spin_unlock_irqrestore(&mdev->req_lock, flags);

	return rv;
}

/**
 * drbd_force_state() - Impose a change which happens outside our control on our state
 * @mdev:	DRBD device.
 * @mask:	mask of state bits to change.
 * @val:	value of new state bits.
 */
void drbd_force_state(struct drbd_conf *mdev,
	union drbd_state mask, union drbd_state val)
{
	drbd_change_state(mdev, CS_HARD, mask, val);
}

static enum drbd_state_rv is_valid_state(struct drbd_conf *, union drbd_state);
static enum drbd_state_rv is_valid_state_transition(struct drbd_conf *,
						    union drbd_state,
						    union drbd_state);
enum sanitize_state_warnings {
	NO_WARNING,
	ABORTED_ONLINE_VERIFY,
	ABORTED_RESYNC,
	CONNECTION_LOST_NEGOTIATING,
	IMPLICITLY_UPGRADED_DISK,
	IMPLICITLY_UPGRADED_PDSK,
};
static union drbd_state sanitize_state(struct drbd_conf *mdev, union drbd_state os,
				       union drbd_state ns, enum sanitize_state_warnings *warn);
int drbd_send_state_req(struct drbd_conf *,
			union drbd_state, union drbd_state);

static enum drbd_state_rv
_req_st_cond(struct drbd_conf *mdev, union drbd_state mask,
	     union drbd_state val)
{
	union drbd_state os, ns;
	unsigned long flags;
	enum drbd_state_rv rv;

	if (test_and_clear_bit(CL_ST_CHG_SUCCESS, &mdev->flags))
		return SS_CW_SUCCESS;

	if (test_and_clear_bit(CL_ST_CHG_FAIL, &mdev->flags))
		return SS_CW_FAILED_BY_PEER;

	rv = 0;
	spin_lock_irqsave(&mdev->req_lock, flags);
	os = mdev->state;
	ns.i = (os.i & ~mask.i) | val.i;
	ns = sanitize_state(mdev, os, ns, NULL);

	if (!cl_wide_st_chg(mdev, os, ns))
		rv = SS_CW_NO_NEED;
	if (!rv) {
		rv = is_valid_state(mdev, ns);
		if (rv == SS_SUCCESS) {
			rv = is_valid_state_transition(mdev, ns, os);
			if (rv == SS_SUCCESS)
				rv = SS_UNKNOWN_ERROR; /* cont waiting, otherwise fail. */
		}
	}
	spin_unlock_irqrestore(&mdev->req_lock, flags);

	return rv;
}

/**
 * drbd_req_state() - Perform an eventually cluster wide state change
 * @mdev:	DRBD device.
 * @mask:	mask of state bits to change.
 * @val:	value of new state bits.
 * @f:		flags
 *
 * Should not be called directly, use drbd_request_state() or
 * _drbd_request_state().
 */
static enum drbd_state_rv
drbd_req_state(struct drbd_conf *mdev, union drbd_state mask,
	       union drbd_state val, enum chg_state_flags f)
{
	struct completion done;
	unsigned long flags;
	union drbd_state os, ns;
	enum drbd_state_rv rv;

	init_completion(&done);

	if (f & CS_SERIALIZE)
		mutex_lock(&mdev->state_mutex);

	spin_lock_irqsave(&mdev->req_lock, flags);
	os = mdev->state;
	ns.i = (os.i & ~mask.i) | val.i;
	ns = sanitize_state(mdev, os, ns, NULL);

	if (cl_wide_st_chg(mdev, os, ns)) {
		rv = is_valid_state(mdev, ns);
		if (rv == SS_SUCCESS)
			rv = is_valid_state_transition(mdev, ns, os);
		spin_unlock_irqrestore(&mdev->req_lock, flags);

		if (rv < SS_SUCCESS) {
			if (f & CS_VERBOSE)
				print_st_err(mdev, os, ns, rv);
			goto abort;
		}

		drbd_state_lock(mdev);
		if (!drbd_send_state_req(mdev, mask, val)) {
			drbd_state_unlock(mdev);
			rv = SS_CW_FAILED_BY_PEER;
			if (f & CS_VERBOSE)
				print_st_err(mdev, os, ns, rv);
			goto abort;
		}

		wait_event(mdev->state_wait,
			(rv = _req_st_cond(mdev, mask, val)));

		if (rv < SS_SUCCESS) {
			drbd_state_unlock(mdev);
			if (f & CS_VERBOSE)
				print_st_err(mdev, os, ns, rv);
			goto abort;
		}
		spin_lock_irqsave(&mdev->req_lock, flags);
		os = mdev->state;
		ns.i = (os.i & ~mask.i) | val.i;
		rv = _drbd_set_state(mdev, ns, f, &done);
		drbd_state_unlock(mdev);
	} else {
		rv = _drbd_set_state(mdev, ns, f, &done);
	}

	spin_unlock_irqrestore(&mdev->req_lock, flags);

	if (f & CS_WAIT_COMPLETE && rv == SS_SUCCESS) {
		D_ASSERT(current != mdev->worker.task);
		wait_for_completion(&done);
	}

abort:
	if (f & CS_SERIALIZE)
		mutex_unlock(&mdev->state_mutex);

	return rv;
}

/**
 * _drbd_request_state() - Request a state change (with flags)
 * @mdev:	DRBD device.
 * @mask:	mask of state bits to change.
 * @val:	value of new state bits.
 * @f:		flags
 *
 * Cousin of drbd_request_state(), useful with the CS_WAIT_COMPLETE
 * flag, or when logging of failed state change requests is not desired.
 */
enum drbd_state_rv
_drbd_request_state(struct drbd_conf *mdev, union drbd_state mask,
		    union drbd_state val, enum chg_state_flags f)
{
	enum drbd_state_rv rv;

	wait_event(mdev->state_wait,
		   (rv = drbd_req_state(mdev, mask, val, f)) != SS_IN_TRANSIENT_STATE);

	return rv;
}

static void print_st(struct drbd_conf *mdev, char *name, union drbd_state ns)
{
	dev_err(DEV, " %s = { cs:%s ro:%s/%s ds:%s/%s %c%c%c%c }\n",
	    name,
	    drbd_conn_str(ns.conn),
	    drbd_role_str(ns.role),
	    drbd_role_str(ns.peer),
	    drbd_disk_str(ns.disk),
	    drbd_disk_str(ns.pdsk),
	    is_susp(ns) ? 's' : 'r',
	    ns.aftr_isp ? 'a' : '-',
	    ns.peer_isp ? 'p' : '-',
	    ns.user_isp ? 'u' : '-'
	    );
}

void print_st_err(struct drbd_conf *mdev, union drbd_state os,
	          union drbd_state ns, enum drbd_state_rv err)
{
	if (err == SS_IN_TRANSIENT_STATE)
		return;
	dev_err(DEV, "State change failed: %s\n", drbd_set_st_err_str(err));
	print_st(mdev, " state", os);
	print_st(mdev, "wanted", ns);
}


/**
 * is_valid_state() - Returns an SS_ error code if ns is not valid
 * @mdev:	DRBD device.
 * @ns:		State to consider.
 */
static enum drbd_state_rv
is_valid_state(struct drbd_conf *mdev, union drbd_state ns)
{
	/* See drbd_state_sw_errors in drbd_strings.c */

	enum drbd_fencing_p fp;
	enum drbd_state_rv rv = SS_SUCCESS;

	fp = FP_DONT_CARE;
	if (get_ldev(mdev)) {
		fp = mdev->ldev->dc.fencing;
		put_ldev(mdev);
	}

	if (get_net_conf(mdev)) {
		if (!mdev->net_conf->two_primaries &&
		    ns.role == R_PRIMARY && ns.peer == R_PRIMARY)
			rv = SS_TWO_PRIMARIES;
		put_net_conf(mdev);
	}

	if (rv <= 0)
		/* already found a reason to abort */;
	else if (ns.role == R_SECONDARY && mdev->open_cnt)
		rv = SS_DEVICE_IN_USE;

	else if (ns.role == R_PRIMARY && ns.conn < C_CONNECTED && ns.disk < D_UP_TO_DATE)
		rv = SS_NO_UP_TO_DATE_DISK;

	else if (fp >= FP_RESOURCE &&
		 ns.role == R_PRIMARY && ns.conn < C_CONNECTED && ns.pdsk >= D_UNKNOWN)
		rv = SS_PRIMARY_NOP;

	else if (ns.role == R_PRIMARY && ns.disk <= D_INCONSISTENT && ns.pdsk <= D_INCONSISTENT)
		rv = SS_NO_UP_TO_DATE_DISK;

	else if (ns.conn > C_CONNECTED && ns.disk < D_INCONSISTENT)
		rv = SS_NO_LOCAL_DISK;

	else if (ns.conn > C_CONNECTED && ns.pdsk < D_INCONSISTENT)
		rv = SS_NO_REMOTE_DISK;

	else if (ns.conn > C_CONNECTED && ns.disk < D_UP_TO_DATE && ns.pdsk < D_UP_TO_DATE)
		rv = SS_NO_UP_TO_DATE_DISK;

	else if ((ns.conn == C_CONNECTED ||
		  ns.conn == C_WF_BITMAP_S ||
		  ns.conn == C_SYNC_SOURCE ||
		  ns.conn == C_PAUSED_SYNC_S) &&
		  ns.disk == D_OUTDATED)
		rv = SS_CONNECTED_OUTDATES;

	else if ((ns.conn == C_VERIFY_S || ns.conn == C_VERIFY_T) &&
		 (mdev->sync_conf.verify_alg[0] == 0))
		rv = SS_NO_VERIFY_ALG;

	else if ((ns.conn == C_VERIFY_S || ns.conn == C_VERIFY_T) &&
		  mdev->agreed_pro_version < 88)
		rv = SS_NOT_SUPPORTED;

	else if (ns.conn >= C_CONNECTED && ns.pdsk == D_UNKNOWN)
		rv = SS_CONNECTED_OUTDATES;

	return rv;
}

/**
 * is_valid_state_transition() - Returns an SS_ error code if the state transition is not possible
 * @mdev:	DRBD device.
 * @ns:		new state.
 * @os:		old state.
 */
static enum drbd_state_rv
is_valid_state_transition(struct drbd_conf *mdev, union drbd_state ns,
			  union drbd_state os)
{
	enum drbd_state_rv rv = SS_SUCCESS;

	if ((ns.conn == C_STARTING_SYNC_T || ns.conn == C_STARTING_SYNC_S) &&
	    os.conn > C_CONNECTED)
		rv = SS_RESYNC_RUNNING;

	if (ns.conn == C_DISCONNECTING && os.conn == C_STANDALONE)
		rv = SS_ALREADY_STANDALONE;

	if (ns.disk > D_ATTACHING && os.disk == D_DISKLESS)
		rv = SS_IS_DISKLESS;

	if (ns.conn == C_WF_CONNECTION && os.conn < C_UNCONNECTED)
		rv = SS_NO_NET_CONFIG;

	if (ns.disk == D_OUTDATED && os.disk < D_OUTDATED && os.disk != D_ATTACHING)
		rv = SS_LOWER_THAN_OUTDATED;

	if (ns.conn == C_DISCONNECTING && os.conn == C_UNCONNECTED)
		rv = SS_IN_TRANSIENT_STATE;

	if (ns.conn == os.conn && ns.conn == C_WF_REPORT_PARAMS)
		rv = SS_IN_TRANSIENT_STATE;

	/* While establishing a connection only allow cstate to change.
	   Delay/refuse role changes, detach attach etc... */
	if (test_bit(STATE_SENT, &mdev->flags) &&
	    !(os.conn == C_WF_REPORT_PARAMS ||
	      (ns.conn == C_WF_REPORT_PARAMS && os.conn == C_WF_CONNECTION)))
		rv = SS_IN_TRANSIENT_STATE;

	if ((ns.conn == C_VERIFY_S || ns.conn == C_VERIFY_T) && os.conn < C_CONNECTED)
		rv = SS_NEED_CONNECTION;

	if ((ns.conn == C_VERIFY_S || ns.conn == C_VERIFY_T) &&
	    ns.conn != os.conn && os.conn > C_CONNECTED)
		rv = SS_RESYNC_RUNNING;

	if ((ns.conn == C_STARTING_SYNC_S || ns.conn == C_STARTING_SYNC_T) &&
	    os.conn < C_CONNECTED)
		rv = SS_NEED_CONNECTION;

	if ((ns.conn == C_SYNC_TARGET || ns.conn == C_SYNC_SOURCE)
	    && os.conn < C_WF_REPORT_PARAMS)
		rv = SS_NEED_CONNECTION; /* No NetworkFailure -> SyncTarget etc... */

	return rv;
}

static void print_sanitize_warnings(struct drbd_conf *mdev, enum sanitize_state_warnings warn)
{
	static const char *msg_table[] = {
		[NO_WARNING] = "",
		[ABORTED_ONLINE_VERIFY] = "Online-verify aborted.",
		[ABORTED_RESYNC] = "Resync aborted.",
		[CONNECTION_LOST_NEGOTIATING] = "Connection lost while negotiating, no data!",
		[IMPLICITLY_UPGRADED_DISK] = "Implicitly upgraded disk",
		[IMPLICITLY_UPGRADED_PDSK] = "Implicitly upgraded pdsk",
	};

	if (warn != NO_WARNING)
		dev_warn(DEV, "%s\n", msg_table[warn]);
}

/**
 * sanitize_state() - Resolves implicitly necessary additional changes to a state transition
 * @mdev:	DRBD device.
 * @os:		old state.
 * @ns:		new state.
 * @warn_sync_abort:
 *
 * When we loose connection, we have to set the state of the peers disk (pdsk)
 * to D_UNKNOWN. This rule and many more along those lines are in this function.
 */
static union drbd_state sanitize_state(struct drbd_conf *mdev, union drbd_state os,
				       union drbd_state ns, enum sanitize_state_warnings *warn)
{
	enum drbd_fencing_p fp;
	enum drbd_disk_state disk_min, disk_max, pdsk_min, pdsk_max;

	if (warn)
		*warn = NO_WARNING;

	fp = FP_DONT_CARE;
	if (get_ldev(mdev)) {
		fp = mdev->ldev->dc.fencing;
		put_ldev(mdev);
	}

	/* Disallow Network errors to configure a device's network part */
	if ((ns.conn >= C_TIMEOUT && ns.conn <= C_TEAR_DOWN) &&
	    os.conn <= C_DISCONNECTING)
		ns.conn = os.conn;

	/* After a network error (+C_TEAR_DOWN) only C_UNCONNECTED or C_DISCONNECTING can follow.
	 * If you try to go into some Sync* state, that shall fail (elsewhere). */
	if (os.conn >= C_TIMEOUT && os.conn <= C_TEAR_DOWN &&
	    ns.conn != C_UNCONNECTED && ns.conn != C_DISCONNECTING && ns.conn <= C_CONNECTED)
		ns.conn = os.conn;

	/* we cannot fail (again) if we already detached */
	if (ns.disk == D_FAILED && os.disk == D_DISKLESS)
		ns.disk = D_DISKLESS;

	/* After C_DISCONNECTING only C_STANDALONE may follow */
	if (os.conn == C_DISCONNECTING && ns.conn != C_STANDALONE)
		ns.conn = os.conn;

	if (ns.conn < C_CONNECTED) {
		ns.peer_isp = 0;
		ns.peer = R_UNKNOWN;
		if (ns.pdsk > D_UNKNOWN || ns.pdsk < D_INCONSISTENT)
			ns.pdsk = D_UNKNOWN;
	}

	/* Clear the aftr_isp when becoming unconfigured */
	if (ns.conn == C_STANDALONE && ns.disk == D_DISKLESS && ns.role == R_SECONDARY)
		ns.aftr_isp = 0;

	/* Abort resync if a disk fails/detaches */
	if (os.conn > C_CONNECTED && ns.conn > C_CONNECTED &&
	    (ns.disk <= D_FAILED || ns.pdsk <= D_FAILED)) {
		if (warn)
			*warn =	os.conn == C_VERIFY_S || os.conn == C_VERIFY_T ?
				ABORTED_ONLINE_VERIFY : ABORTED_RESYNC;
		ns.conn = C_CONNECTED;
	}

	/* Connection breaks down before we finished "Negotiating" */
	if (ns.conn < C_CONNECTED && ns.disk == D_NEGOTIATING &&
	    get_ldev_if_state(mdev, D_NEGOTIATING)) {
		if (mdev->ed_uuid == mdev->ldev->md.uuid[UI_CURRENT]) {
			ns.disk = mdev->new_state_tmp.disk;
			ns.pdsk = mdev->new_state_tmp.pdsk;
		} else {
			if (warn)
				*warn = CONNECTION_LOST_NEGOTIATING;
			ns.disk = D_DISKLESS;
			ns.pdsk = D_UNKNOWN;
		}
		put_ldev(mdev);
	}

	/* D_CONSISTENT and D_OUTDATED vanish when we get connected */
	if (ns.conn >= C_CONNECTED && ns.conn < C_AHEAD) {
		if (ns.disk == D_CONSISTENT || ns.disk == D_OUTDATED)
			ns.disk = D_UP_TO_DATE;
		if (ns.pdsk == D_CONSISTENT || ns.pdsk == D_OUTDATED)
			ns.pdsk = D_UP_TO_DATE;
	}

	/* Implications of the connection stat on the disk states */
	disk_min = D_DISKLESS;
	disk_max = D_UP_TO_DATE;
	pdsk_min = D_INCONSISTENT;
	pdsk_max = D_UNKNOWN;
	switch ((enum drbd_conns)ns.conn) {
	case C_WF_BITMAP_T:
	case C_PAUSED_SYNC_T:
	case C_STARTING_SYNC_T:
	case C_WF_SYNC_UUID:
	case C_BEHIND:
		disk_min = D_INCONSISTENT;
		disk_max = D_OUTDATED;
		pdsk_min = D_UP_TO_DATE;
		pdsk_max = D_UP_TO_DATE;
		break;
	case C_VERIFY_S:
	case C_VERIFY_T:
		disk_min = D_UP_TO_DATE;
		disk_max = D_UP_TO_DATE;
		pdsk_min = D_UP_TO_DATE;
		pdsk_max = D_UP_TO_DATE;
		break;
	case C_CONNECTED:
		disk_min = D_DISKLESS;
		disk_max = D_UP_TO_DATE;
		pdsk_min = D_DISKLESS;
		pdsk_max = D_UP_TO_DATE;
		break;
	case C_WF_BITMAP_S:
	case C_PAUSED_SYNC_S:
	case C_STARTING_SYNC_S:
	case C_AHEAD:
		disk_min = D_UP_TO_DATE;
		disk_max = D_UP_TO_DATE;
		pdsk_min = D_INCONSISTENT;
		pdsk_max = D_CONSISTENT; /* D_OUTDATED would be nice. But explicit outdate necessary*/
		break;
	case C_SYNC_TARGET:
		disk_min = D_INCONSISTENT;
		disk_max = D_INCONSISTENT;
		pdsk_min = D_UP_TO_DATE;
		pdsk_max = D_UP_TO_DATE;
		break;
	case C_SYNC_SOURCE:
		disk_min = D_UP_TO_DATE;
		disk_max = D_UP_TO_DATE;
		pdsk_min = D_INCONSISTENT;
		pdsk_max = D_INCONSISTENT;
		break;
	case C_STANDALONE:
	case C_DISCONNECTING:
	case C_UNCONNECTED:
	case C_TIMEOUT:
	case C_BROKEN_PIPE:
	case C_NETWORK_FAILURE:
	case C_PROTOCOL_ERROR:
	case C_TEAR_DOWN:
	case C_WF_CONNECTION:
	case C_WF_REPORT_PARAMS:
	case C_MASK:
		break;
	}
	if (ns.disk > disk_max)
		ns.disk = disk_max;

	if (ns.disk < disk_min) {
		if (warn)
			*warn = IMPLICITLY_UPGRADED_DISK;
		ns.disk = disk_min;
	}
	if (ns.pdsk > pdsk_max)
		ns.pdsk = pdsk_max;

	if (ns.pdsk < pdsk_min) {
		if (warn)
			*warn = IMPLICITLY_UPGRADED_PDSK;
		ns.pdsk = pdsk_min;
	}

	if (fp == FP_STONITH &&
	    (ns.role == R_PRIMARY && ns.conn < C_CONNECTED && ns.pdsk > D_OUTDATED) &&
	    !(os.role == R_PRIMARY && os.conn < C_CONNECTED && os.pdsk > D_OUTDATED))
		ns.susp_fen = 1; /* Suspend IO while fence-peer handler runs (peer lost) */

	if (mdev->sync_conf.on_no_data == OND_SUSPEND_IO &&
	    (ns.role == R_PRIMARY && ns.disk < D_UP_TO_DATE && ns.pdsk < D_UP_TO_DATE) &&
	    !(os.role == R_PRIMARY && os.disk < D_UP_TO_DATE && os.pdsk < D_UP_TO_DATE))
		ns.susp_nod = 1; /* Suspend IO while no data available (no accessible data available) */

	if (ns.aftr_isp || ns.peer_isp || ns.user_isp) {
		if (ns.conn == C_SYNC_SOURCE)
			ns.conn = C_PAUSED_SYNC_S;
		if (ns.conn == C_SYNC_TARGET)
			ns.conn = C_PAUSED_SYNC_T;
	} else {
		if (ns.conn == C_PAUSED_SYNC_S)
			ns.conn = C_SYNC_SOURCE;
		if (ns.conn == C_PAUSED_SYNC_T)
			ns.conn = C_SYNC_TARGET;
	}

	return ns;
}

/* helper for __drbd_set_state */
static void set_ov_position(struct drbd_conf *mdev, enum drbd_conns cs)
{
	if (mdev->agreed_pro_version < 90)
		mdev->ov_start_sector = 0;
	mdev->rs_total = drbd_bm_bits(mdev);
	mdev->ov_position = 0;
	if (cs == C_VERIFY_T) {
		/* starting online verify from an arbitrary position
		 * does not fit well into the existing protocol.
		 * on C_VERIFY_T, we initialize ov_left and friends
		 * implicitly in receive_DataRequest once the
		 * first P_OV_REQUEST is received */
		mdev->ov_start_sector = ~(sector_t)0;
	} else {
		unsigned long bit = BM_SECT_TO_BIT(mdev->ov_start_sector);
		if (bit >= mdev->rs_total) {
			mdev->ov_start_sector =
				BM_BIT_TO_SECT(mdev->rs_total - 1);
			mdev->rs_total = 1;
		} else
			mdev->rs_total -= bit;
		mdev->ov_position = mdev->ov_start_sector;
	}
	mdev->ov_left = mdev->rs_total;
}

static void drbd_resume_al(struct drbd_conf *mdev)
{
	if (test_and_clear_bit(AL_SUSPENDED, &mdev->flags))
		dev_info(DEV, "Resumed AL updates\n");
}

/**
 * __drbd_set_state() - Set a new DRBD state
 * @mdev:	DRBD device.
 * @ns:		new state.
 * @flags:	Flags
 * @done:	Optional completion, that will get completed after the after_state_ch() finished
 *
 * Caller needs to hold req_lock, and global_state_lock. Do not call directly.
 */
enum drbd_state_rv
__drbd_set_state(struct drbd_conf *mdev, union drbd_state ns,
	         enum chg_state_flags flags, struct completion *done)
{
	union drbd_state os;
	enum drbd_state_rv rv = SS_SUCCESS;
	enum sanitize_state_warnings ssw;
	struct after_state_chg_work *ascw;

	os = mdev->state;

	ns = sanitize_state(mdev, os, ns, &ssw);

	if (ns.i == os.i)
		return SS_NOTHING_TO_DO;

	if (!(flags & CS_HARD)) {
		/*  pre-state-change checks ; only look at ns  */
		/* See drbd_state_sw_errors in drbd_strings.c */

		rv = is_valid_state(mdev, ns);
		if (rv < SS_SUCCESS) {
			/* If the old state was illegal as well, then let
			   this happen...*/

			if (is_valid_state(mdev, os) == rv)
				rv = is_valid_state_transition(mdev, ns, os);
		} else
			rv = is_valid_state_transition(mdev, ns, os);
	}

	if (rv < SS_SUCCESS) {
		if (flags & CS_VERBOSE)
			print_st_err(mdev, os, ns, rv);
		return rv;
	}

	print_sanitize_warnings(mdev, ssw);

	{
	char *pbp, pb[300];
	pbp = pb;
	*pbp = 0;
	if (ns.role != os.role)
		pbp += sprintf(pbp, "role( %s -> %s ) ",
			       drbd_role_str(os.role),
			       drbd_role_str(ns.role));
	if (ns.peer != os.peer)
		pbp += sprintf(pbp, "peer( %s -> %s ) ",
			       drbd_role_str(os.peer),
			       drbd_role_str(ns.peer));
	if (ns.conn != os.conn)
		pbp += sprintf(pbp, "conn( %s -> %s ) ",
			       drbd_conn_str(os.conn),
			       drbd_conn_str(ns.conn));
	if (ns.disk != os.disk)
		pbp += sprintf(pbp, "disk( %s -> %s ) ",
			       drbd_disk_str(os.disk),
			       drbd_disk_str(ns.disk));
	if (ns.pdsk != os.pdsk)
		pbp += sprintf(pbp, "pdsk( %s -> %s ) ",
			       drbd_disk_str(os.pdsk),
			       drbd_disk_str(ns.pdsk));
	if (is_susp(ns) != is_susp(os))
		pbp += sprintf(pbp, "susp( %d -> %d ) ",
			       is_susp(os),
			       is_susp(ns));
	if (ns.aftr_isp != os.aftr_isp)
		pbp += sprintf(pbp, "aftr_isp( %d -> %d ) ",
			       os.aftr_isp,
			       ns.aftr_isp);
	if (ns.peer_isp != os.peer_isp)
		pbp += sprintf(pbp, "peer_isp( %d -> %d ) ",
			       os.peer_isp,
			       ns.peer_isp);
	if (ns.user_isp != os.user_isp)
		pbp += sprintf(pbp, "user_isp( %d -> %d ) ",
			       os.user_isp,
			       ns.user_isp);
	dev_info(DEV, "%s\n", pb);
	}

	/* solve the race between becoming unconfigured,
	 * worker doing the cleanup, and
	 * admin reconfiguring us:
	 * on (re)configure, first set CONFIG_PENDING,
	 * then wait for a potentially exiting worker,
	 * start the worker, and schedule one no_op.
	 * then proceed with configuration.
	 */
	if (ns.disk == D_DISKLESS &&
	    ns.conn == C_STANDALONE &&
	    ns.role == R_SECONDARY &&
	    !test_and_set_bit(CONFIG_PENDING, &mdev->flags))
		set_bit(DEVICE_DYING, &mdev->flags);

	/* if we are going -> D_FAILED or D_DISKLESS, grab one extra reference
	 * on the ldev here, to be sure the transition -> D_DISKLESS resp.
	 * drbd_ldev_destroy() won't happen before our corresponding
	 * after_state_ch works run, where we put_ldev again. */
	if ((os.disk != D_FAILED && ns.disk == D_FAILED) ||
	    (os.disk != D_DISKLESS && ns.disk == D_DISKLESS))
		atomic_inc(&mdev->local_cnt);

	mdev->state = ns;

	if (os.disk == D_ATTACHING && ns.disk >= D_NEGOTIATING)
		drbd_print_uuids(mdev, "attached to UUIDs");

	wake_up(&mdev->misc_wait);
	wake_up(&mdev->state_wait);

	/* aborted verify run. log the last position */
	if ((os.conn == C_VERIFY_S || os.conn == C_VERIFY_T) &&
	    ns.conn < C_CONNECTED) {
		mdev->ov_start_sector =
			BM_BIT_TO_SECT(drbd_bm_bits(mdev) - mdev->ov_left);
		dev_info(DEV, "Online Verify reached sector %llu\n",
			(unsigned long long)mdev->ov_start_sector);
	}

	if ((os.conn == C_PAUSED_SYNC_T || os.conn == C_PAUSED_SYNC_S) &&
	    (ns.conn == C_SYNC_TARGET  || ns.conn == C_SYNC_SOURCE)) {
		dev_info(DEV, "Syncer continues.\n");
		mdev->rs_paused += (long)jiffies
				  -(long)mdev->rs_mark_time[mdev->rs_last_mark];
		if (ns.conn == C_SYNC_TARGET)
			mod_timer(&mdev->resync_timer, jiffies);
	}

	if ((os.conn == C_SYNC_TARGET  || os.conn == C_SYNC_SOURCE) &&
	    (ns.conn == C_PAUSED_SYNC_T || ns.conn == C_PAUSED_SYNC_S)) {
		dev_info(DEV, "Resync suspended\n");
		mdev->rs_mark_time[mdev->rs_last_mark] = jiffies;
	}

	if (os.conn == C_CONNECTED &&
	    (ns.conn == C_VERIFY_S || ns.conn == C_VERIFY_T)) {
		unsigned long now = jiffies;
		int i;

		set_ov_position(mdev, ns.conn);
		mdev->rs_start = now;
		mdev->rs_last_events = 0;
		mdev->rs_last_sect_ev = 0;
		mdev->ov_last_oos_size = 0;
		mdev->ov_last_oos_start = 0;

		for (i = 0; i < DRBD_SYNC_MARKS; i++) {
			mdev->rs_mark_left[i] = mdev->ov_left;
			mdev->rs_mark_time[i] = now;
		}

		drbd_rs_controller_reset(mdev);

		if (ns.conn == C_VERIFY_S) {
			dev_info(DEV, "Starting Online Verify from sector %llu\n",
					(unsigned long long)mdev->ov_position);
			mod_timer(&mdev->resync_timer, jiffies);
		}
	}

	if (get_ldev(mdev)) {
		u32 mdf = mdev->ldev->md.flags & ~(MDF_CONSISTENT|MDF_PRIMARY_IND|
						 MDF_CONNECTED_IND|MDF_WAS_UP_TO_DATE|
						 MDF_PEER_OUT_DATED|MDF_CRASHED_PRIMARY);

		if (test_bit(CRASHED_PRIMARY, &mdev->flags))
			mdf |= MDF_CRASHED_PRIMARY;
		if (mdev->state.role == R_PRIMARY ||
		    (mdev->state.pdsk < D_INCONSISTENT && mdev->state.peer == R_PRIMARY))
			mdf |= MDF_PRIMARY_IND;
		if (mdev->state.conn > C_WF_REPORT_PARAMS)
			mdf |= MDF_CONNECTED_IND;
		if (mdev->state.disk > D_INCONSISTENT)
			mdf |= MDF_CONSISTENT;
		if (mdev->state.disk > D_OUTDATED)
			mdf |= MDF_WAS_UP_TO_DATE;
		if (mdev->state.pdsk <= D_OUTDATED && mdev->state.pdsk >= D_INCONSISTENT)
			mdf |= MDF_PEER_OUT_DATED;
		if (mdf != mdev->ldev->md.flags) {
			mdev->ldev->md.flags = mdf;
			drbd_md_mark_dirty(mdev);
		}
		if (os.disk < D_CONSISTENT && ns.disk >= D_CONSISTENT)
			drbd_set_ed_uuid(mdev, mdev->ldev->md.uuid[UI_CURRENT]);
		put_ldev(mdev);
	}

	/* Peer was forced D_UP_TO_DATE & R_PRIMARY, consider to resync */
	if (os.disk == D_INCONSISTENT && os.pdsk == D_INCONSISTENT &&
	    os.peer == R_SECONDARY && ns.peer == R_PRIMARY)
		set_bit(CONSIDER_RESYNC, &mdev->flags);

	/* Receiver should clean up itself */
	if (os.conn != C_DISCONNECTING && ns.conn == C_DISCONNECTING)
		drbd_thread_stop_nowait(&mdev->receiver);

	/* Now the receiver finished cleaning up itself, it should die */
	if (os.conn != C_STANDALONE && ns.conn == C_STANDALONE)
		drbd_thread_stop_nowait(&mdev->receiver);

	/* Upon network failure, we need to restart the receiver. */
	if (os.conn > C_WF_CONNECTION &&
	    ns.conn <= C_TEAR_DOWN && ns.conn >= C_TIMEOUT)
		drbd_thread_restart_nowait(&mdev->receiver);

	/* Resume AL writing if we get a connection */
	if (os.conn < C_CONNECTED && ns.conn >= C_CONNECTED)
		drbd_resume_al(mdev);

	/* remember last connect and attach times so request_timer_fn() won't
	 * kill newly established sessions while we are still trying to thaw
	 * previously frozen IO */
	if (os.conn != C_WF_REPORT_PARAMS && ns.conn == C_WF_REPORT_PARAMS)
		mdev->last_reconnect_jif = jiffies;
	if ((os.disk == D_ATTACHING || os.disk == D_NEGOTIATING) &&
	    ns.disk > D_NEGOTIATING)
		mdev->last_reattach_jif = jiffies;

	ascw = kmalloc(sizeof(*ascw), GFP_ATOMIC);
	if (ascw) {
		ascw->os = os;
		ascw->ns = ns;
		ascw->flags = flags;
		ascw->w.cb = w_after_state_ch;
		ascw->done = done;
		drbd_queue_work(&mdev->data.work, &ascw->w);
	} else {
		dev_warn(DEV, "Could not kmalloc an ascw\n");
	}

	return rv;
}

static int w_after_state_ch(struct drbd_conf *mdev, struct drbd_work *w, int unused)
{
	struct after_state_chg_work *ascw =
		container_of(w, struct after_state_chg_work, w);
	after_state_ch(mdev, ascw->os, ascw->ns, ascw->flags);
	if (ascw->flags & CS_WAIT_COMPLETE) {
		D_ASSERT(ascw->done != NULL);
		complete(ascw->done);
	}
	kfree(ascw);

	return 1;
}

static void abw_start_sync(struct drbd_conf *mdev, int rv)
{
	if (rv) {
		dev_err(DEV, "Writing the bitmap failed not starting resync.\n");
		_drbd_request_state(mdev, NS(conn, C_CONNECTED), CS_VERBOSE);
		return;
	}

	switch (mdev->state.conn) {
	case C_STARTING_SYNC_T:
		_drbd_request_state(mdev, NS(conn, C_WF_SYNC_UUID), CS_VERBOSE);
		break;
	case C_STARTING_SYNC_S:
		drbd_start_resync(mdev, C_SYNC_SOURCE);
		break;
	}
}

int drbd_bitmap_io_from_worker(struct drbd_conf *mdev,
		int (*io_fn)(struct drbd_conf *),
		char *why, enum bm_flag flags)
{
	int rv;

	D_ASSERT(current == mdev->worker.task);

	/* open coded non-blocking drbd_suspend_io(mdev); */
	set_bit(SUSPEND_IO, &mdev->flags);

	drbd_bm_lock(mdev, why, flags);
	rv = io_fn(mdev);
	drbd_bm_unlock(mdev);

	drbd_resume_io(mdev);

	return rv;
}

/**
 * after_state_ch() - Perform after state change actions that may sleep
 * @mdev:	DRBD device.
 * @os:		old state.
 * @ns:		new state.
 * @flags:	Flags
 */
static void after_state_ch(struct drbd_conf *mdev, union drbd_state os,
			   union drbd_state ns, enum chg_state_flags flags)
{
	enum drbd_fencing_p fp;
	enum drbd_req_event what = nothing;
	union drbd_state nsm = (union drbd_state){ .i = -1 };

	if (os.conn != C_CONNECTED && ns.conn == C_CONNECTED) {
		clear_bit(CRASHED_PRIMARY, &mdev->flags);
		if (mdev->p_uuid)
			mdev->p_uuid[UI_FLAGS] &= ~((u64)2);
	}

	fp = FP_DONT_CARE;
	if (get_ldev(mdev)) {
		fp = mdev->ldev->dc.fencing;
		put_ldev(mdev);
	}

	/* Inform userspace about the change... */
	drbd_bcast_state(mdev, ns);

	if (!(os.role == R_PRIMARY && os.disk < D_UP_TO_DATE && os.pdsk < D_UP_TO_DATE) &&
	    (ns.role == R_PRIMARY && ns.disk < D_UP_TO_DATE && ns.pdsk < D_UP_TO_DATE))
		drbd_khelper(mdev, "pri-on-incon-degr");

	/* Here we have the actions that are performed after a
	   state change. This function might sleep */

	if (os.disk <= D_NEGOTIATING && ns.disk > D_NEGOTIATING)
		mod_timer(&mdev->request_timer, jiffies + HZ);

	nsm.i = -1;
	if (ns.susp_nod) {
		if (os.conn < C_CONNECTED && ns.conn >= C_CONNECTED)
			what = resend;

		if ((os.disk == D_ATTACHING || os.disk == D_NEGOTIATING) &&
		    ns.disk > D_NEGOTIATING)
			what = restart_frozen_disk_io;

		if (what != nothing)
			nsm.susp_nod = 0;
	}

	if (ns.susp_fen) {
		/* case1: The outdate peer handler is successful: */
		if (os.pdsk > D_OUTDATED  && ns.pdsk <= D_OUTDATED) {
			if (test_bit(NEW_CUR_UUID, &mdev->flags)) {
				drbd_uuid_new_current(mdev);
				clear_bit(NEW_CUR_UUID, &mdev->flags);
			}
			spin_lock_irq(&mdev->req_lock);
			_tl_clear(mdev);
			_drbd_set_state(_NS(mdev, susp_fen, 0), CS_VERBOSE, NULL);
			spin_unlock_irq(&mdev->req_lock);
		}
		/* case2: The connection was established again: */
		if (os.conn < C_CONNECTED && ns.conn >= C_CONNECTED) {
			clear_bit(NEW_CUR_UUID, &mdev->flags);
			what = resend;
			nsm.susp_fen = 0;
		}
	}

	if (what != nothing) {
		spin_lock_irq(&mdev->req_lock);
		_tl_restart(mdev, what);
		nsm.i &= mdev->state.i;
		_drbd_set_state(mdev, nsm, CS_VERBOSE, NULL);
		spin_unlock_irq(&mdev->req_lock);
	}

	/* Became sync source.  With protocol >= 96, we still need to send out
	 * the sync uuid now. Need to do that before any drbd_send_state, or
	 * the other side may go "paused sync" before receiving the sync uuids,
	 * which is unexpected. */
	if ((os.conn != C_SYNC_SOURCE && os.conn != C_PAUSED_SYNC_S) &&
	    (ns.conn == C_SYNC_SOURCE || ns.conn == C_PAUSED_SYNC_S) &&
	    mdev->agreed_pro_version >= 96 && get_ldev(mdev)) {
		drbd_gen_and_send_sync_uuid(mdev);
		put_ldev(mdev);
	}

	/* Do not change the order of the if above and the two below... */
	if (os.pdsk == D_DISKLESS && ns.pdsk > D_DISKLESS) {      /* attach on the peer */
		/* we probably will start a resync soon.
		 * make sure those things are properly reset. */
		mdev->rs_total = 0;
		mdev->rs_failed = 0;
		atomic_set(&mdev->rs_pending_cnt, 0);
		drbd_rs_cancel_all(mdev);

		drbd_send_uuids(mdev);
		drbd_send_state(mdev, ns);
	}
	/* No point in queuing send_bitmap if we don't have a connection
	 * anymore, so check also the _current_ state, not only the new state
	 * at the time this work was queued. */
	if (os.conn != C_WF_BITMAP_S && ns.conn == C_WF_BITMAP_S &&
	    mdev->state.conn == C_WF_BITMAP_S)
		drbd_queue_bitmap_io(mdev, &drbd_send_bitmap, NULL,
				"send_bitmap (WFBitMapS)",
				BM_LOCKED_TEST_ALLOWED);

	/* Lost contact to peer's copy of the data */
	if ((os.pdsk >= D_INCONSISTENT &&
	     os.pdsk != D_UNKNOWN &&
	     os.pdsk != D_OUTDATED)
	&&  (ns.pdsk < D_INCONSISTENT ||
	     ns.pdsk == D_UNKNOWN ||
	     ns.pdsk == D_OUTDATED)) {
		if (get_ldev(mdev)) {
			if ((ns.role == R_PRIMARY || ns.peer == R_PRIMARY) &&
			    mdev->ldev->md.uuid[UI_BITMAP] == 0 && ns.disk >= D_UP_TO_DATE) {
				if (is_susp(mdev->state)) {
					set_bit(NEW_CUR_UUID, &mdev->flags);
				} else {
					drbd_uuid_new_current(mdev);
					drbd_send_uuids(mdev);
				}
			}
			put_ldev(mdev);
		}
	}

	if (ns.pdsk < D_INCONSISTENT && get_ldev(mdev)) {
		if (os.peer == R_SECONDARY && ns.peer == R_PRIMARY &&
		    mdev->ldev->md.uuid[UI_BITMAP] == 0 && ns.disk >= D_UP_TO_DATE) {
			drbd_uuid_new_current(mdev);
			drbd_send_uuids(mdev);
		}
		/* D_DISKLESS Peer becomes secondary */
		if (os.peer == R_PRIMARY && ns.peer == R_SECONDARY)
			/* We may still be Primary ourselves.
			 * No harm done if the bitmap still changes,
			 * redirtied pages will follow later. */
			drbd_bitmap_io_from_worker(mdev, &drbd_bm_write,
				"demote diskless peer", BM_LOCKED_SET_ALLOWED);
		put_ldev(mdev);
	}

	/* Write out all changed bits on demote.
	 * Though, no need to da that just yet
	 * if there is a resync going on still */
	if (os.role == R_PRIMARY && ns.role == R_SECONDARY &&
		mdev->state.conn <= C_CONNECTED && get_ldev(mdev)) {
		/* No changes to the bitmap expected this time, so assert that,
		 * even though no harm was done if it did change. */
		drbd_bitmap_io_from_worker(mdev, &drbd_bm_write,
				"demote", BM_LOCKED_TEST_ALLOWED);
		put_ldev(mdev);
	}

	/* Last part of the attaching process ... */
	if (ns.conn >= C_CONNECTED &&
	    os.disk == D_ATTACHING && ns.disk == D_NEGOTIATING) {
		drbd_send_sizes(mdev, 0, 0);  /* to start sync... */
		drbd_send_uuids(mdev);
		drbd_send_state(mdev, ns);
	}

	/* We want to pause/continue resync, tell peer. */
	if (ns.conn >= C_CONNECTED &&
	     ((os.aftr_isp != ns.aftr_isp) ||
	      (os.user_isp != ns.user_isp)))
		drbd_send_state(mdev, ns);

	/* In case one of the isp bits got set, suspend other devices. */
	if ((!os.aftr_isp && !os.peer_isp && !os.user_isp) &&
	    (ns.aftr_isp || ns.peer_isp || ns.user_isp))
		suspend_other_sg(mdev);

	/* Make sure the peer gets informed about eventual state
	   changes (ISP bits) while we were in WFReportParams. */
	if (os.conn == C_WF_REPORT_PARAMS && ns.conn >= C_CONNECTED)
		drbd_send_state(mdev, ns);

	if (os.conn != C_AHEAD && ns.conn == C_AHEAD)
		drbd_send_state(mdev, ns);

	/* We are in the progress to start a full sync... */
	if ((os.conn != C_STARTING_SYNC_T && ns.conn == C_STARTING_SYNC_T) ||
	    (os.conn != C_STARTING_SYNC_S && ns.conn == C_STARTING_SYNC_S))
		/* no other bitmap changes expected during this phase */
		drbd_queue_bitmap_io(mdev,
			&drbd_bmio_set_n_write, &abw_start_sync,
			"set_n_write from StartingSync", BM_LOCKED_TEST_ALLOWED);

	/* We are invalidating our self... */
	if (os.conn < C_CONNECTED && ns.conn < C_CONNECTED &&
	    os.disk > D_INCONSISTENT && ns.disk == D_INCONSISTENT)
		/* other bitmap operation expected during this phase */
		drbd_queue_bitmap_io(mdev, &drbd_bmio_set_n_write, NULL,
			"set_n_write from invalidate", BM_LOCKED_MASK);

	/* first half of local IO error, failure to attach,
	 * or administrative detach */
	if (os.disk != D_FAILED && ns.disk == D_FAILED) {
		enum drbd_io_error_p eh = EP_PASS_ON;
		int was_io_error = 0;
		/* corresponding get_ldev was in __drbd_set_state, to serialize
		 * our cleanup here with the transition to D_DISKLESS.
		 * But is is still not save to dreference ldev here, since
		 * we might come from an failed Attach before ldev was set. */
		if (mdev->ldev) {
			eh = mdev->ldev->dc.on_io_error;
			was_io_error = test_and_clear_bit(WAS_IO_ERROR, &mdev->flags);

			if (was_io_error && eh == EP_CALL_HELPER)
				drbd_khelper(mdev, "local-io-error");

			/* Immediately allow completion of all application IO,
			 * that waits for completion from the local disk,
			 * if this was a force-detach due to disk_timeout
			 * or administrator request (drbdsetup detach --force).
			 * Do NOT abort otherwise.
			 * Aborting local requests may cause serious problems,
			 * if requests are completed to upper layers already,
			 * and then later the already submitted local bio completes.
			 * This can cause DMA into former bio pages that meanwhile
			 * have been re-used for other things.
			 * So aborting local requests may cause crashes,
			 * or even worse, silent data corruption.
			 */
			if (test_and_clear_bit(FORCE_DETACH, &mdev->flags))
				tl_abort_disk_io(mdev);

			/* current state still has to be D_FAILED,
			 * there is only one way out: to D_DISKLESS,
			 * and that may only happen after our put_ldev below. */
			if (mdev->state.disk != D_FAILED)
				dev_err(DEV,
					"ASSERT FAILED: disk is %s during detach\n",
					drbd_disk_str(mdev->state.disk));

			if (ns.conn >= C_CONNECTED)
				drbd_send_state(mdev, ns);

			drbd_rs_cancel_all(mdev);

			/* In case we want to get something to stable storage still,
			 * this may be the last chance.
			 * Following put_ldev may transition to D_DISKLESS. */
			drbd_md_sync(mdev);
		}
		put_ldev(mdev);
	}

        /* second half of local IO error, failure to attach,
         * or administrative detach,
         * after local_cnt references have reached zero again */
        if (os.disk != D_DISKLESS && ns.disk == D_DISKLESS) {
                /* We must still be diskless,
                 * re-attach has to be serialized with this! */
                if (mdev->state.disk != D_DISKLESS)
                        dev_err(DEV,
                                "ASSERT FAILED: disk is %s while going diskless\n",
                                drbd_disk_str(mdev->state.disk));

		if (ns.conn >= C_CONNECTED)
			drbd_send_state(mdev, ns);

		/* corresponding get_ldev in __drbd_set_state
		 * this may finally trigger drbd_ldev_destroy. */
		put_ldev(mdev);
	}

	/* Notify peer that I had a local IO error, and did not detached.. */
	if (os.disk == D_UP_TO_DATE && ns.disk == D_INCONSISTENT && ns.conn >= C_CONNECTED)
		drbd_send_state(mdev, ns);

	/* Disks got bigger while they were detached */
	if (ns.disk > D_NEGOTIATING && ns.pdsk > D_NEGOTIATING &&
	    test_and_clear_bit(RESYNC_AFTER_NEG, &mdev->flags)) {
		if (ns.conn == C_CONNECTED)
			resync_after_online_grow(mdev);
	}

	/* A resync finished or aborted, wake paused devices... */
	if ((os.conn > C_CONNECTED && ns.conn <= C_CONNECTED) ||
	    (os.peer_isp && !ns.peer_isp) ||
	    (os.user_isp && !ns.user_isp))
		resume_next_sg(mdev);

	/* sync target done with resync.  Explicitly notify peer, even though
	 * it should (at least for non-empty resyncs) already know itself. */
	if (os.disk < D_UP_TO_DATE && os.conn >= C_SYNC_SOURCE && ns.conn == C_CONNECTED)
		drbd_send_state(mdev, ns);

	/* Wake up role changes, that were delayed because of connection establishing */
	if (os.conn == C_WF_REPORT_PARAMS && ns.conn != C_WF_REPORT_PARAMS) {
		clear_bit(STATE_SENT, &mdev->flags);
		wake_up(&mdev->state_wait);
	}

	/* This triggers bitmap writeout of potentially still unwritten pages
	 * if the resync finished cleanly, or aborted because of peer disk
	 * failure, or because of connection loss.
	 * For resync aborted because of local disk failure, we cannot do
	 * any bitmap writeout anymore.
	 * No harm done if some bits change during this phase.
	 */
	if (os.conn > C_CONNECTED && ns.conn <= C_CONNECTED && get_ldev(mdev)) {
		drbd_queue_bitmap_io(mdev, &drbd_bm_write_copy_pages, NULL,
			"write from resync_finished", BM_LOCKED_CHANGE_ALLOWED);
		put_ldev(mdev);
	}

	/* free tl_hash if we Got thawed and are C_STANDALONE */
	if (ns.conn == C_STANDALONE && !is_susp(ns) && mdev->tl_hash)
		drbd_free_tl_hash(mdev);

	/* Upon network connection, we need to start the receiver */
	if (os.conn == C_STANDALONE && ns.conn == C_UNCONNECTED)
		drbd_thread_start(&mdev->receiver);

	/* Terminate worker thread if we are unconfigured - it will be
	   restarted as needed... */
	if (ns.disk == D_DISKLESS &&
	    ns.conn == C_STANDALONE &&
	    ns.role == R_SECONDARY) {
		if (os.aftr_isp != ns.aftr_isp)
			resume_next_sg(mdev);
		/* set in __drbd_set_state, unless CONFIG_PENDING was set */
		if (test_bit(DEVICE_DYING, &mdev->flags))
			drbd_thread_stop_nowait(&mdev->worker);
	}

	drbd_md_sync(mdev);
}


static int drbd_thread_setup(void *arg)
{
	struct drbd_thread *thi = (struct drbd_thread *) arg;
	struct drbd_conf *mdev = thi->mdev;
	unsigned long flags;
	int retval;

restart:
	retval = thi->function(thi);

	spin_lock_irqsave(&thi->t_lock, flags);

	/* if the receiver has been "Exiting", the last thing it did
	 * was set the conn state to "StandAlone",
	 * if now a re-connect request comes in, conn state goes C_UNCONNECTED,
	 * and receiver thread will be "started".
	 * drbd_thread_start needs to set "Restarting" in that case.
	 * t_state check and assignment needs to be within the same spinlock,
	 * so either thread_start sees Exiting, and can remap to Restarting,
	 * or thread_start see None, and can proceed as normal.
	 */

	if (thi->t_state == Restarting) {
		dev_info(DEV, "Restarting %s\n", current->comm);
		thi->t_state = Running;
		spin_unlock_irqrestore(&thi->t_lock, flags);
		goto restart;
	}

	thi->task = NULL;
	thi->t_state = None;
	smp_mb();
	complete(&thi->stop);
	spin_unlock_irqrestore(&thi->t_lock, flags);

	dev_info(DEV, "Terminating %s\n", current->comm);

	/* Release mod reference taken when thread was started */
	module_put(THIS_MODULE);
	return retval;
}

static void drbd_thread_init(struct drbd_conf *mdev, struct drbd_thread *thi,
		      int (*func) (struct drbd_thread *))
{
	spin_lock_init(&thi->t_lock);
	thi->task    = NULL;
	thi->t_state = None;
	thi->function = func;
	thi->mdev = mdev;
}

int drbd_thread_start(struct drbd_thread *thi)
{
	struct drbd_conf *mdev = thi->mdev;
	struct task_struct *nt;
	unsigned long flags;

	const char *me =
		thi == &mdev->receiver ? "receiver" :
		thi == &mdev->asender  ? "asender"  :
		thi == &mdev->worker   ? "worker"   : "NONSENSE";

	/* is used from state engine doing drbd_thread_stop_nowait,
	 * while holding the req lock irqsave */
	spin_lock_irqsave(&thi->t_lock, flags);

	switch (thi->t_state) {
	case None:
		dev_info(DEV, "Starting %s thread (from %s [%d])\n",
				me, current->comm, current->pid);

		/* Get ref on module for thread - this is released when thread exits */
		if (!try_module_get(THIS_MODULE)) {
			dev_err(DEV, "Failed to get module reference in drbd_thread_start\n");
			spin_unlock_irqrestore(&thi->t_lock, flags);
			return false;
		}

		init_completion(&thi->stop);
		D_ASSERT(thi->task == NULL);
		thi->reset_cpu_mask = 1;
		thi->t_state = Running;
		spin_unlock_irqrestore(&thi->t_lock, flags);
		flush_signals(current); /* otherw. may get -ERESTARTNOINTR */

		nt = kthread_create(drbd_thread_setup, (void *) thi,
				    "drbd%d_%s", mdev_to_minor(mdev), me);

		if (IS_ERR(nt)) {
			dev_err(DEV, "Couldn't start thread\n");

			module_put(THIS_MODULE);
			return false;
		}
		spin_lock_irqsave(&thi->t_lock, flags);
		thi->task = nt;
		thi->t_state = Running;
		spin_unlock_irqrestore(&thi->t_lock, flags);
		wake_up_process(nt);
		break;
	case Exiting:
		thi->t_state = Restarting;
		dev_info(DEV, "Restarting %s thread (from %s [%d])\n",
				me, current->comm, current->pid);
		/* fall through */
	case Running:
	case Restarting:
	default:
		spin_unlock_irqrestore(&thi->t_lock, flags);
		break;
	}

	return true;
}


void _drbd_thread_stop(struct drbd_thread *thi, int restart, int wait)
{
	unsigned long flags;

	enum drbd_thread_state ns = restart ? Restarting : Exiting;

	/* may be called from state engine, holding the req lock irqsave */
	spin_lock_irqsave(&thi->t_lock, flags);

	if (thi->t_state == None) {
		spin_unlock_irqrestore(&thi->t_lock, flags);
		if (restart)
			drbd_thread_start(thi);
		return;
	}

	if (thi->t_state != ns) {
		if (thi->task == NULL) {
			spin_unlock_irqrestore(&thi->t_lock, flags);
			return;
		}

		thi->t_state = ns;
		smp_mb();
		init_completion(&thi->stop);
		if (thi->task != current)
			force_sig(DRBD_SIGKILL, thi->task);

	}

	spin_unlock_irqrestore(&thi->t_lock, flags);

	if (wait)
		wait_for_completion(&thi->stop);
}

#ifdef CONFIG_SMP
/**
 * drbd_calc_cpu_mask() - Generate CPU masks, spread over all CPUs
 * @mdev:	DRBD device.
 *
 * Forces all threads of a device onto the same CPU. This is beneficial for
 * DRBD's performance. May be overwritten by user's configuration.
 */
void drbd_calc_cpu_mask(struct drbd_conf *mdev)
{
	int ord, cpu;

	/* user override. */
	if (cpumask_weight(mdev->cpu_mask))
		return;

	ord = mdev_to_minor(mdev) % cpumask_weight(cpu_online_mask);
	for_each_online_cpu(cpu) {
		if (ord-- == 0) {
			cpumask_set_cpu(cpu, mdev->cpu_mask);
			return;
		}
	}
	/* should not be reached */
	cpumask_setall(mdev->cpu_mask);
}

/**
 * drbd_thread_current_set_cpu() - modifies the cpu mask of the _current_ thread
 * @mdev:	DRBD device.
 *
 * call in the "main loop" of _all_ threads, no need for any mutex, current won't die
 * prematurely.
 */
void drbd_thread_current_set_cpu(struct drbd_conf *mdev)
{
	struct task_struct *p = current;
	struct drbd_thread *thi =
		p == mdev->asender.task  ? &mdev->asender  :
		p == mdev->receiver.task ? &mdev->receiver :
		p == mdev->worker.task   ? &mdev->worker   :
		NULL;
	ERR_IF(thi == NULL)
		return;
	if (!thi->reset_cpu_mask)
		return;
	thi->reset_cpu_mask = 0;
	set_cpus_allowed_ptr(p, mdev->cpu_mask);
}
#endif

/* the appropriate socket mutex must be held already */
int _drbd_send_cmd(struct drbd_conf *mdev, struct socket *sock,
			  enum drbd_packets cmd, struct p_header80 *h,
			  size_t size, unsigned msg_flags)
{
	int sent, ok;

	ERR_IF(!h) return false;
	ERR_IF(!size) return false;

	h->magic   = BE_DRBD_MAGIC;
	h->command = cpu_to_be16(cmd);
	h->length  = cpu_to_be16(size-sizeof(struct p_header80));

	sent = drbd_send(mdev, sock, h, size, msg_flags);

	ok = (sent == size);
	if (!ok && !signal_pending(current))
		dev_warn(DEV, "short sent %s size=%d sent=%d\n",
		    cmdname(cmd), (int)size, sent);
	return ok;
}

/* don't pass the socket. we may only look at it
 * when we hold the appropriate socket mutex.
 */
int drbd_send_cmd(struct drbd_conf *mdev, int use_data_socket,
		  enum drbd_packets cmd, struct p_header80 *h, size_t size)
{
	int ok = 0;
	struct socket *sock;

	if (use_data_socket) {
		mutex_lock(&mdev->data.mutex);
		sock = mdev->data.socket;
	} else {
		mutex_lock(&mdev->meta.mutex);
		sock = mdev->meta.socket;
	}

	/* drbd_disconnect() could have called drbd_free_sock()
	 * while we were waiting in down()... */
	if (likely(sock != NULL))
		ok = _drbd_send_cmd(mdev, sock, cmd, h, size, 0);

	if (use_data_socket)
		mutex_unlock(&mdev->data.mutex);
	else
		mutex_unlock(&mdev->meta.mutex);
	return ok;
}

int drbd_send_cmd2(struct drbd_conf *mdev, enum drbd_packets cmd, char *data,
		   size_t size)
{
	struct p_header80 h;
	int ok;

	h.magic   = BE_DRBD_MAGIC;
	h.command = cpu_to_be16(cmd);
	h.length  = cpu_to_be16(size);

	if (!drbd_get_data_sock(mdev))
		return 0;

	ok = (sizeof(h) ==
		drbd_send(mdev, mdev->data.socket, &h, sizeof(h), 0));
	ok = ok && (size ==
		drbd_send(mdev, mdev->data.socket, data, size, 0));

	drbd_put_data_sock(mdev);

	return ok;
}

int drbd_send_sync_param(struct drbd_conf *mdev, struct syncer_conf *sc)
{
	struct p_rs_param_95 *p;
	struct socket *sock;
	int size, rv;
	const int apv = mdev->agreed_pro_version;

	size = apv <= 87 ? sizeof(struct p_rs_param)
		: apv == 88 ? sizeof(struct p_rs_param)
			+ strlen(mdev->sync_conf.verify_alg) + 1
		: apv <= 94 ? sizeof(struct p_rs_param_89)
		: /* apv >= 95 */ sizeof(struct p_rs_param_95);

	/* used from admin command context and receiver/worker context.
	 * to avoid kmalloc, grab the socket right here,
	 * then use the pre-allocated sbuf there */
	mutex_lock(&mdev->data.mutex);
	sock = mdev->data.socket;

	if (likely(sock != NULL)) {
		enum drbd_packets cmd = apv >= 89 ? P_SYNC_PARAM89 : P_SYNC_PARAM;

		p = &mdev->data.sbuf.rs_param_95;

		/* initialize verify_alg and csums_alg */
		memset(p->verify_alg, 0, 2 * SHARED_SECRET_MAX);

		p->rate = cpu_to_be32(sc->rate);
		p->c_plan_ahead = cpu_to_be32(sc->c_plan_ahead);
		p->c_delay_target = cpu_to_be32(sc->c_delay_target);
		p->c_fill_target = cpu_to_be32(sc->c_fill_target);
		p->c_max_rate = cpu_to_be32(sc->c_max_rate);

		if (apv >= 88)
			strcpy(p->verify_alg, mdev->sync_conf.verify_alg);
		if (apv >= 89)
			strcpy(p->csums_alg, mdev->sync_conf.csums_alg);

		rv = _drbd_send_cmd(mdev, sock, cmd, &p->head, size, 0);
	} else
		rv = 0; /* not ok */

	mutex_unlock(&mdev->data.mutex);

	return rv;
}

int drbd_send_protocol(struct drbd_conf *mdev)
{
	struct p_protocol *p;
	int size, cf, rv;

	size = sizeof(struct p_protocol);

	if (mdev->agreed_pro_version >= 87)
		size += strlen(mdev->net_conf->integrity_alg) + 1;

	/* we must not recurse into our own queue,
	 * as that is blocked during handshake */
	p = kmalloc(size, GFP_NOIO);
	if (p == NULL)
		return 0;

	p->protocol      = cpu_to_be32(mdev->net_conf->wire_protocol);
	p->after_sb_0p   = cpu_to_be32(mdev->net_conf->after_sb_0p);
	p->after_sb_1p   = cpu_to_be32(mdev->net_conf->after_sb_1p);
	p->after_sb_2p   = cpu_to_be32(mdev->net_conf->after_sb_2p);
	p->two_primaries = cpu_to_be32(mdev->net_conf->two_primaries);

	cf = 0;
	if (mdev->net_conf->want_lose)
		cf |= CF_WANT_LOSE;
	if (mdev->net_conf->dry_run) {
		if (mdev->agreed_pro_version >= 92)
			cf |= CF_DRY_RUN;
		else {
			dev_err(DEV, "--dry-run is not supported by peer");
			kfree(p);
			return -1;
		}
	}
	p->conn_flags    = cpu_to_be32(cf);

	if (mdev->agreed_pro_version >= 87)
		strcpy(p->integrity_alg, mdev->net_conf->integrity_alg);

	rv = drbd_send_cmd(mdev, USE_DATA_SOCKET, P_PROTOCOL,
			   (struct p_header80 *)p, size);
	kfree(p);
	return rv;
}

int _drbd_send_uuids(struct drbd_conf *mdev, u64 uuid_flags)
{
	struct p_uuids p;
	int i;

	if (!get_ldev_if_state(mdev, D_NEGOTIATING))
		return 1;

	for (i = UI_CURRENT; i < UI_SIZE; i++)
		p.uuid[i] = mdev->ldev ? cpu_to_be64(mdev->ldev->md.uuid[i]) : 0;

	mdev->comm_bm_set = drbd_bm_total_weight(mdev);
	p.uuid[UI_SIZE] = cpu_to_be64(mdev->comm_bm_set);
	uuid_flags |= mdev->net_conf->want_lose ? 1 : 0;
	uuid_flags |= test_bit(CRASHED_PRIMARY, &mdev->flags) ? 2 : 0;
	uuid_flags |= mdev->new_state_tmp.disk == D_INCONSISTENT ? 4 : 0;
	p.uuid[UI_FLAGS] = cpu_to_be64(uuid_flags);

	put_ldev(mdev);

	return drbd_send_cmd(mdev, USE_DATA_SOCKET, P_UUIDS,
			     (struct p_header80 *)&p, sizeof(p));
}

int drbd_send_uuids(struct drbd_conf *mdev)
{
	return _drbd_send_uuids(mdev, 0);
}

int drbd_send_uuids_skip_initial_sync(struct drbd_conf *mdev)
{
	return _drbd_send_uuids(mdev, 8);
}

void drbd_print_uuids(struct drbd_conf *mdev, const char *text)
{
	if (get_ldev_if_state(mdev, D_NEGOTIATING)) {
		u64 *uuid = mdev->ldev->md.uuid;
		dev_info(DEV, "%s %016llX:%016llX:%016llX:%016llX\n",
		     text,
		     (unsigned long long)uuid[UI_CURRENT],
		     (unsigned long long)uuid[UI_BITMAP],
		     (unsigned long long)uuid[UI_HISTORY_START],
		     (unsigned long long)uuid[UI_HISTORY_END]);
		put_ldev(mdev);
	} else {
		dev_info(DEV, "%s effective data uuid: %016llX\n",
				text,
				(unsigned long long)mdev->ed_uuid);
	}
}

int drbd_gen_and_send_sync_uuid(struct drbd_conf *mdev)
{
	struct p_rs_uuid p;
	u64 uuid;

	D_ASSERT(mdev->state.disk == D_UP_TO_DATE);

	uuid = mdev->ldev->md.uuid[UI_BITMAP];
	if (uuid && uuid != UUID_JUST_CREATED)
		uuid = uuid + UUID_NEW_BM_OFFSET;
	else
		get_random_bytes(&uuid, sizeof(u64));
	drbd_uuid_set(mdev, UI_BITMAP, uuid);
	drbd_print_uuids(mdev, "updated sync UUID");
	drbd_md_sync(mdev);
	p.uuid = cpu_to_be64(uuid);

	return drbd_send_cmd(mdev, USE_DATA_SOCKET, P_SYNC_UUID,
			     (struct p_header80 *)&p, sizeof(p));
}

int drbd_send_sizes(struct drbd_conf *mdev, int trigger_reply, enum dds_flags flags)
{
	struct p_sizes p;
	sector_t d_size, u_size;
	int q_order_type;
	unsigned int max_bio_size;
	int ok;

	if (get_ldev_if_state(mdev, D_NEGOTIATING)) {
		D_ASSERT(mdev->ldev->backing_bdev);
		d_size = drbd_get_max_capacity(mdev->ldev);
		u_size = mdev->ldev->dc.disk_size;
		q_order_type = drbd_queue_order_type(mdev);
		max_bio_size = queue_max_hw_sectors(mdev->ldev->backing_bdev->bd_disk->queue) << 9;
		max_bio_size = min(max_bio_size, DRBD_MAX_BIO_SIZE);
		put_ldev(mdev);
	} else {
		d_size = 0;
		u_size = 0;
		q_order_type = QUEUE_ORDERED_NONE;
		max_bio_size = DRBD_MAX_BIO_SIZE; /* ... multiple BIOs per peer_request */
	}

	/* Never allow old drbd (up to 8.3.7) to see more than 32KiB */
	if (mdev->agreed_pro_version <= 94)
		max_bio_size = min(max_bio_size, DRBD_MAX_SIZE_H80_PACKET);

	p.d_size = cpu_to_be64(d_size);
	p.u_size = cpu_to_be64(u_size);
	p.c_size = cpu_to_be64(trigger_reply ? 0 : drbd_get_capacity(mdev->this_bdev));
	p.max_bio_size = cpu_to_be32(max_bio_size);
	p.queue_order_type = cpu_to_be16(q_order_type);
	p.dds_flags = cpu_to_be16(flags);

	ok = drbd_send_cmd(mdev, USE_DATA_SOCKET, P_SIZES,
			   (struct p_header80 *)&p, sizeof(p));
	return ok;
}

/**
 * drbd_send_current_state() - Sends the drbd state to the peer
 * @mdev:	DRBD device.
 */
int drbd_send_current_state(struct drbd_conf *mdev)
{
	struct socket *sock;
	struct p_state p;
	int ok = 0;

	/* Grab state lock so we wont send state if we're in the middle
	 * of a cluster wide state change on another thread */
	drbd_state_lock(mdev);

	mutex_lock(&mdev->data.mutex);

	p.state = cpu_to_be32(mdev->state.i); /* Within the send mutex */
	sock = mdev->data.socket;

	if (likely(sock != NULL)) {
		ok = _drbd_send_cmd(mdev, sock, P_STATE,
				    (struct p_header80 *)&p, sizeof(p), 0);
	}

	mutex_unlock(&mdev->data.mutex);

	drbd_state_unlock(mdev);
	return ok;
}

/**
 * drbd_send_state() - After a state change, sends the new state to the peer
 * @mdev:	DRBD device.
 * @state:	the state to send, not necessarily the current state.
 *
 * Each state change queues an "after_state_ch" work, which will eventually
 * send the resulting new state to the peer. If more state changes happen
 * between queuing and processing of the after_state_ch work, we still
 * want to send each intermediary state in the order it occurred.
 */
int drbd_send_state(struct drbd_conf *mdev, union drbd_state state)
{
	struct socket *sock;
	struct p_state p;
	int ok = 0;

	mutex_lock(&mdev->data.mutex);

	p.state = cpu_to_be32(state.i);
	sock = mdev->data.socket;

	if (likely(sock != NULL)) {
		ok = _drbd_send_cmd(mdev, sock, P_STATE,
				    (struct p_header80 *)&p, sizeof(p), 0);
	}

	mutex_unlock(&mdev->data.mutex);

	return ok;
}

int drbd_send_state_req(struct drbd_conf *mdev,
	union drbd_state mask, union drbd_state val)
{
	struct p_req_state p;

	p.mask    = cpu_to_be32(mask.i);
	p.val     = cpu_to_be32(val.i);

	return drbd_send_cmd(mdev, USE_DATA_SOCKET, P_STATE_CHG_REQ,
			     (struct p_header80 *)&p, sizeof(p));
}

int drbd_send_sr_reply(struct drbd_conf *mdev, enum drbd_state_rv retcode)
{
	struct p_req_state_reply p;

	p.retcode    = cpu_to_be32(retcode);

	return drbd_send_cmd(mdev, USE_META_SOCKET, P_STATE_CHG_REPLY,
			     (struct p_header80 *)&p, sizeof(p));
}

int fill_bitmap_rle_bits(struct drbd_conf *mdev,
	struct p_compressed_bm *p,
	struct bm_xfer_ctx *c)
{
	struct bitstream bs;
	unsigned long plain_bits;
	unsigned long tmp;
	unsigned long rl;
	unsigned len;
	unsigned toggle;
	int bits;

	/* may we use this feature? */
	if ((mdev->sync_conf.use_rle == 0) ||
		(mdev->agreed_pro_version < 90))
			return 0;

	if (c->bit_offset >= c->bm_bits)
		return 0; /* nothing to do. */

	/* use at most thus many bytes */
	bitstream_init(&bs, p->code, BM_PACKET_VLI_BYTES_MAX, 0);
	memset(p->code, 0, BM_PACKET_VLI_BYTES_MAX);
	/* plain bits covered in this code string */
	plain_bits = 0;

	/* p->encoding & 0x80 stores whether the first run length is set.
	 * bit offset is implicit.
	 * start with toggle == 2 to be able to tell the first iteration */
	toggle = 2;

	/* see how much plain bits we can stuff into one packet
	 * using RLE and VLI. */
	do {
		tmp = (toggle == 0) ? _drbd_bm_find_next_zero(mdev, c->bit_offset)
				    : _drbd_bm_find_next(mdev, c->bit_offset);
		if (tmp == -1UL)
			tmp = c->bm_bits;
		rl = tmp - c->bit_offset;

		if (toggle == 2) { /* first iteration */
			if (rl == 0) {
				/* the first checked bit was set,
				 * store start value, */
				DCBP_set_start(p, 1);
				/* but skip encoding of zero run length */
				toggle = !toggle;
				continue;
			}
			DCBP_set_start(p, 0);
		}

		/* paranoia: catch zero runlength.
		 * can only happen if bitmap is modified while we scan it. */
		if (rl == 0) {
			dev_err(DEV, "unexpected zero runlength while encoding bitmap "
			    "t:%u bo:%lu\n", toggle, c->bit_offset);
			return -1;
		}

		bits = vli_encode_bits(&bs, rl);
		if (bits == -ENOBUFS) /* buffer full */
			break;
		if (bits <= 0) {
			dev_err(DEV, "error while encoding bitmap: %d\n", bits);
			return 0;
		}

		toggle = !toggle;
		plain_bits += rl;
		c->bit_offset = tmp;
	} while (c->bit_offset < c->bm_bits);

	len = bs.cur.b - p->code + !!bs.cur.bit;

	if (plain_bits < (len << 3)) {
		/* incompressible with this method.
		 * we need to rewind both word and bit position. */
		c->bit_offset -= plain_bits;
		bm_xfer_ctx_bit_to_word_offset(c);
		c->bit_offset = c->word_offset * BITS_PER_LONG;
		return 0;
	}

	/* RLE + VLI was able to compress it just fine.
	 * update c->word_offset. */
	bm_xfer_ctx_bit_to_word_offset(c);

	/* store pad_bits */
	DCBP_set_pad_bits(p, (8 - bs.cur.bit) & 0x7);

	return len;
}

/**
 * send_bitmap_rle_or_plain
 *
 * Return 0 when done, 1 when another iteration is needed, and a negative error
 * code upon failure.
 */
static int
send_bitmap_rle_or_plain(struct drbd_conf *mdev,
			 struct p_header80 *h, struct bm_xfer_ctx *c)
{
	struct p_compressed_bm *p = (void*)h;
	unsigned long num_words;
	int len;
	int ok;

	len = fill_bitmap_rle_bits(mdev, p, c);

	if (len < 0)
		return -EIO;

	if (len) {
		DCBP_set_code(p, RLE_VLI_Bits);
		ok = _drbd_send_cmd(mdev, mdev->data.socket, P_COMPRESSED_BITMAP, h,
			sizeof(*p) + len, 0);

		c->packets[0]++;
		c->bytes[0] += sizeof(*p) + len;

		if (c->bit_offset >= c->bm_bits)
			len = 0; /* DONE */
	} else {
		/* was not compressible.
		 * send a buffer full of plain text bits instead. */
		num_words = min_t(size_t, BM_PACKET_WORDS, c->bm_words - c->word_offset);
		len = num_words * sizeof(long);
		if (len)
			drbd_bm_get_lel(mdev, c->word_offset, num_words, (unsigned long*)h->payload);
		ok = _drbd_send_cmd(mdev, mdev->data.socket, P_BITMAP,
				   h, sizeof(struct p_header80) + len, 0);
		c->word_offset += num_words;
		c->bit_offset = c->word_offset * BITS_PER_LONG;

		c->packets[1]++;
		c->bytes[1] += sizeof(struct p_header80) + len;

		if (c->bit_offset > c->bm_bits)
			c->bit_offset = c->bm_bits;
	}
	if (ok) {
		if (len == 0) {
			INFO_bm_xfer_stats(mdev, "send", c);
			return 0;
		} else
			return 1;
	}
	return -EIO;
}

/* See the comment at receive_bitmap() */
int _drbd_send_bitmap(struct drbd_conf *mdev)
{
	struct bm_xfer_ctx c;
	struct p_header80 *p;
	int err;

	ERR_IF(!mdev->bitmap) return false;

	/* maybe we should use some per thread scratch page,
	 * and allocate that during initial device creation? */
	p = (struct p_header80 *) __get_free_page(GFP_NOIO);
	if (!p) {
		dev_err(DEV, "failed to allocate one page buffer in %s\n", __func__);
		return false;
	}

	if (get_ldev(mdev)) {
		if (drbd_md_test_flag(mdev->ldev, MDF_FULL_SYNC)) {
			dev_info(DEV, "Writing the whole bitmap, MDF_FullSync was set.\n");
			drbd_bm_set_all(mdev);
			if (drbd_bm_write(mdev)) {
				/* write_bm did fail! Leave full sync flag set in Meta P_DATA
				 * but otherwise process as per normal - need to tell other
				 * side that a full resync is required! */
				dev_err(DEV, "Failed to write bitmap to disk!\n");
			} else {
				drbd_md_clear_flag(mdev, MDF_FULL_SYNC);
				drbd_md_sync(mdev);
			}
		}
		put_ldev(mdev);
	}

	c = (struct bm_xfer_ctx) {
		.bm_bits = drbd_bm_bits(mdev),
		.bm_words = drbd_bm_words(mdev),
	};

	do {
		err = send_bitmap_rle_or_plain(mdev, p, &c);
	} while (err > 0);

	free_page((unsigned long) p);
	return err == 0;
}

int drbd_send_bitmap(struct drbd_conf *mdev)
{
	int err;

	if (!drbd_get_data_sock(mdev))
		return -1;
	err = !_drbd_send_bitmap(mdev);
	drbd_put_data_sock(mdev);
	return err;
}

int drbd_send_b_ack(struct drbd_conf *mdev, u32 barrier_nr, u32 set_size)
{
	int ok;
	struct p_barrier_ack p;

	p.barrier  = barrier_nr;
	p.set_size = cpu_to_be32(set_size);

	if (mdev->state.conn < C_CONNECTED)
		return false;
	ok = drbd_send_cmd(mdev, USE_META_SOCKET, P_BARRIER_ACK,
			(struct p_header80 *)&p, sizeof(p));
	return ok;
}

/**
 * _drbd_send_ack() - Sends an ack packet
 * @mdev:	DRBD device.
 * @cmd:	Packet command code.
 * @sector:	sector, needs to be in big endian byte order
 * @blksize:	size in byte, needs to be in big endian byte order
 * @block_id:	Id, big endian byte order
 */
static int _drbd_send_ack(struct drbd_conf *mdev, enum drbd_packets cmd,
			  u64 sector,
			  u32 blksize,
			  u64 block_id)
{
	int ok;
	struct p_block_ack p;

	p.sector   = sector;
	p.block_id = block_id;
	p.blksize  = blksize;
	p.seq_num  = cpu_to_be32(atomic_add_return(1, &mdev->packet_seq));

	if (!mdev->meta.socket || mdev->state.conn < C_CONNECTED)
		return false;
	ok = drbd_send_cmd(mdev, USE_META_SOCKET, cmd,
				(struct p_header80 *)&p, sizeof(p));
	return ok;
}

/* dp->sector and dp->block_id already/still in network byte order,
 * data_size is payload size according to dp->head,
 * and may need to be corrected for digest size. */
int drbd_send_ack_dp(struct drbd_conf *mdev, enum drbd_packets cmd,
		     struct p_data *dp, int data_size)
{
	data_size -= (mdev->agreed_pro_version >= 87 && mdev->integrity_r_tfm) ?
		crypto_hash_digestsize(mdev->integrity_r_tfm) : 0;
	return _drbd_send_ack(mdev, cmd, dp->sector, cpu_to_be32(data_size),
			      dp->block_id);
}

int drbd_send_ack_rp(struct drbd_conf *mdev, enum drbd_packets cmd,
		     struct p_block_req *rp)
{
	return _drbd_send_ack(mdev, cmd, rp->sector, rp->blksize, rp->block_id);
}

/**
 * drbd_send_ack() - Sends an ack packet
 * @mdev:	DRBD device.
 * @cmd:	Packet command code.
 * @e:		Epoch entry.
 */
int drbd_send_ack(struct drbd_conf *mdev,
	enum drbd_packets cmd, struct drbd_epoch_entry *e)
{
	return _drbd_send_ack(mdev, cmd,
			      cpu_to_be64(e->sector),
			      cpu_to_be32(e->size),
			      e->block_id);
}

/* This function misuses the block_id field to signal if the blocks
 * are is sync or not. */
int drbd_send_ack_ex(struct drbd_conf *mdev, enum drbd_packets cmd,
		     sector_t sector, int blksize, u64 block_id)
{
	return _drbd_send_ack(mdev, cmd,
			      cpu_to_be64(sector),
			      cpu_to_be32(blksize),
			      cpu_to_be64(block_id));
}

int drbd_send_drequest(struct drbd_conf *mdev, int cmd,
		       sector_t sector, int size, u64 block_id)
{
	int ok;
	struct p_block_req p;

	p.sector   = cpu_to_be64(sector);
	p.block_id = block_id;
	p.blksize  = cpu_to_be32(size);

	ok = drbd_send_cmd(mdev, USE_DATA_SOCKET, cmd,
				(struct p_header80 *)&p, sizeof(p));
	return ok;
}

int drbd_send_drequest_csum(struct drbd_conf *mdev,
			    sector_t sector, int size,
			    void *digest, int digest_size,
			    enum drbd_packets cmd)
{
	int ok;
	struct p_block_req p;

	p.sector   = cpu_to_be64(sector);
	p.block_id = BE_DRBD_MAGIC + 0xbeef;
	p.blksize  = cpu_to_be32(size);

	p.head.magic   = BE_DRBD_MAGIC;
	p.head.command = cpu_to_be16(cmd);
	p.head.length  = cpu_to_be16(sizeof(p) - sizeof(struct p_header80) + digest_size);

	mutex_lock(&mdev->data.mutex);

	ok = (sizeof(p) == drbd_send(mdev, mdev->data.socket, &p, sizeof(p), 0));
	ok = ok && (digest_size == drbd_send(mdev, mdev->data.socket, digest, digest_size, 0));

	mutex_unlock(&mdev->data.mutex);

	return ok;
}

int drbd_send_ov_request(struct drbd_conf *mdev, sector_t sector, int size)
{
	int ok;
	struct p_block_req p;

	p.sector   = cpu_to_be64(sector);
	p.block_id = BE_DRBD_MAGIC + 0xbabe;
	p.blksize  = cpu_to_be32(size);

	ok = drbd_send_cmd(mdev, USE_DATA_SOCKET, P_OV_REQUEST,
			   (struct p_header80 *)&p, sizeof(p));
	return ok;
}

/* called on sndtimeo
 * returns false if we should retry,
 * true if we think connection is dead
 */
static int we_should_drop_the_connection(struct drbd_conf *mdev, struct socket *sock)
{
	int drop_it;
	/* long elapsed = (long)(jiffies - mdev->last_received); */

	drop_it =   mdev->meta.socket == sock
		|| !mdev->asender.task
		|| get_t_state(&mdev->asender) != Running
		|| mdev->state.conn < C_CONNECTED;

	if (drop_it)
		return true;

	drop_it = !--mdev->ko_count;
	if (!drop_it) {
		dev_err(DEV, "[%s/%d] sock_sendmsg time expired, ko = %u\n",
		       current->comm, current->pid, mdev->ko_count);
		request_ping(mdev);
	}

	return drop_it; /* && (mdev->state == R_PRIMARY) */;
}

/* The idea of sendpage seems to be to put some kind of reference
 * to the page into the skb, and to hand it over to the NIC. In
 * this process get_page() gets called.
 *
 * As soon as the page was really sent over the network put_page()
 * gets called by some part of the network layer. [ NIC driver? ]
 *
 * [ get_page() / put_page() increment/decrement the count. If count
 *   reaches 0 the page will be freed. ]
 *
 * This works nicely with pages from FSs.
 * But this means that in protocol A we might signal IO completion too early!
 *
 * In order not to corrupt data during a resync we must make sure
 * that we do not reuse our own buffer pages (EEs) to early, therefore
 * we have the net_ee list.
 *
 * XFS seems to have problems, still, it submits pages with page_count == 0!
 * As a workaround, we disable sendpage on pages
 * with page_count == 0 or PageSlab.
 */
static int _drbd_no_send_page(struct drbd_conf *mdev, struct page *page,
		   int offset, size_t size, unsigned msg_flags)
{
	int sent = drbd_send(mdev, mdev->data.socket, kmap(page) + offset, size, msg_flags);
	kunmap(page);
	if (sent == size)
		mdev->send_cnt += size>>9;
	return sent == size;
}

static int _drbd_send_page(struct drbd_conf *mdev, struct page *page,
		    int offset, size_t size, unsigned msg_flags)
{
	mm_segment_t oldfs = get_fs();
	int sent, ok;
	int len = size;

	/* e.g. XFS meta- & log-data is in slab pages, which have a
	 * page_count of 0 and/or have PageSlab() set.
	 * we cannot use send_page for those, as that does get_page();
	 * put_page(); and would cause either a VM_BUG directly, or
	 * __page_cache_release a page that would actually still be referenced
	 * by someone, leading to some obscure delayed Oops somewhere else. */
	if (disable_sendpage || (page_count(page) < 1) || PageSlab(page))
		return _drbd_no_send_page(mdev, page, offset, size, msg_flags);

	msg_flags |= MSG_NOSIGNAL;
	drbd_update_congested(mdev);
	set_fs(KERNEL_DS);
	do {
		sent = mdev->data.socket->ops->sendpage(mdev->data.socket, page,
							offset, len,
							msg_flags);
		if (sent == -EAGAIN) {
			if (we_should_drop_the_connection(mdev,
							  mdev->data.socket))
				break;
			else
				continue;
		}
		if (sent <= 0) {
			dev_warn(DEV, "%s: size=%d len=%d sent=%d\n",
			     __func__, (int)size, len, sent);
			break;
		}
		len    -= sent;
		offset += sent;
	} while (len > 0 /* THINK && mdev->cstate >= C_CONNECTED*/);
	set_fs(oldfs);
	clear_bit(NET_CONGESTED, &mdev->flags);

	ok = (len == 0);
	if (likely(ok))
		mdev->send_cnt += size>>9;
	return ok;
}

static int _drbd_send_bio(struct drbd_conf *mdev, struct bio *bio)
{
	struct bio_vec *bvec;
	int i;
	/* hint all but last page with MSG_MORE */
	bio_for_each_segment(bvec, bio, i) {
		if (!_drbd_no_send_page(mdev, bvec->bv_page,
				     bvec->bv_offset, bvec->bv_len,
				     i == bio->bi_vcnt -1 ? 0 : MSG_MORE))
			return 0;
	}
	return 1;
}

static int _drbd_send_zc_bio(struct drbd_conf *mdev, struct bio *bio)
{
	struct bio_vec *bvec;
	int i;
	/* hint all but last page with MSG_MORE */
	bio_for_each_segment(bvec, bio, i) {
		if (!_drbd_send_page(mdev, bvec->bv_page,
				     bvec->bv_offset, bvec->bv_len,
				     i == bio->bi_vcnt -1 ? 0 : MSG_MORE))
			return 0;
	}
	return 1;
}

static int _drbd_send_zc_ee(struct drbd_conf *mdev, struct drbd_epoch_entry *e)
{
	struct page *page = e->pages;
	unsigned len = e->size;
	/* hint all but last page with MSG_MORE */
	page_chain_for_each(page) {
		unsigned l = min_t(unsigned, len, PAGE_SIZE);
		if (!_drbd_send_page(mdev, page, 0, l,
				page_chain_next(page) ? MSG_MORE : 0))
			return 0;
		len -= l;
	}
	return 1;
}

static u32 bio_flags_to_wire(struct drbd_conf *mdev, unsigned long bi_rw)
{
	if (mdev->agreed_pro_version >= 95)
		return  (bi_rw & REQ_SYNC ? DP_RW_SYNC : 0) |
			(bi_rw & REQ_FUA ? DP_FUA : 0) |
			(bi_rw & REQ_FLUSH ? DP_FLUSH : 0) |
			(bi_rw & REQ_DISCARD ? DP_DISCARD : 0);
	else
		return bi_rw & REQ_SYNC ? DP_RW_SYNC : 0;
}

/* Used to send write requests
 * R_PRIMARY -> Peer	(P_DATA)
 */
int drbd_send_dblock(struct drbd_conf *mdev, struct drbd_request *req)
{
	int ok = 1;
	struct p_data p;
	unsigned int dp_flags = 0;
	void *dgb;
	int dgs;

	if (!drbd_get_data_sock(mdev))
		return 0;

	dgs = (mdev->agreed_pro_version >= 87 && mdev->integrity_w_tfm) ?
		crypto_hash_digestsize(mdev->integrity_w_tfm) : 0;

	if (req->size <= DRBD_MAX_SIZE_H80_PACKET) {
		p.head.h80.magic   = BE_DRBD_MAGIC;
		p.head.h80.command = cpu_to_be16(P_DATA);
		p.head.h80.length  =
			cpu_to_be16(sizeof(p) - sizeof(union p_header) + dgs + req->size);
	} else {
		p.head.h95.magic   = BE_DRBD_MAGIC_BIG;
		p.head.h95.command = cpu_to_be16(P_DATA);
		p.head.h95.length  =
			cpu_to_be32(sizeof(p) - sizeof(union p_header) + dgs + req->size);
	}

	p.sector   = cpu_to_be64(req->sector);
	p.block_id = (unsigned long)req;
	p.seq_num  = cpu_to_be32(atomic_add_return(1, &mdev->packet_seq));

	dp_flags = bio_flags_to_wire(mdev, req->master_bio->bi_rw);

	if (mdev->state.conn >= C_SYNC_SOURCE &&
	    mdev->state.conn <= C_PAUSED_SYNC_T)
		dp_flags |= DP_MAY_SET_IN_SYNC;

	p.dp_flags = cpu_to_be32(dp_flags);
	set_bit(UNPLUG_REMOTE, &mdev->flags);
	ok = (sizeof(p) ==
		drbd_send(mdev, mdev->data.socket, &p, sizeof(p), dgs ? MSG_MORE : 0));
	if (ok && dgs) {
		dgb = mdev->int_dig_out;
		drbd_csum_bio(mdev, mdev->integrity_w_tfm, req->master_bio, dgb);
		ok = dgs == drbd_send(mdev, mdev->data.socket, dgb, dgs, 0);
	}
	if (ok) {
		/* For protocol A, we have to memcpy the payload into
		 * socket buffers, as we may complete right away
		 * as soon as we handed it over to tcp, at which point the data
		 * pages may become invalid.
		 *
		 * For data-integrity enabled, we copy it as well, so we can be
		 * sure that even if the bio pages may still be modified, it
		 * won't change the data on the wire, thus if the digest checks
		 * out ok after sending on this side, but does not fit on the
		 * receiving side, we sure have detected corruption elsewhere.
		 */
		if (mdev->net_conf->wire_protocol == DRBD_PROT_A || dgs)
			ok = _drbd_send_bio(mdev, req->master_bio);
		else
			ok = _drbd_send_zc_bio(mdev, req->master_bio);

		/* double check digest, sometimes buffers have been modified in flight. */
		if (dgs > 0 && dgs <= 64) {
			/* 64 byte, 512 bit, is the largest digest size
			 * currently supported in kernel crypto. */
			unsigned char digest[64];
			drbd_csum_bio(mdev, mdev->integrity_w_tfm, req->master_bio, digest);
			if (memcmp(mdev->int_dig_out, digest, dgs)) {
				dev_warn(DEV,
					"Digest mismatch, buffer modified by upper layers during write: %llus +%u\n",
					(unsigned long long)req->sector, req->size);
			}
		} /* else if (dgs > 64) {
		     ... Be noisy about digest too large ...
		} */
	}

	drbd_put_data_sock(mdev);

	return ok;
}

/* answer packet, used to send data back for read requests:
 *  Peer       -> (diskless) R_PRIMARY   (P_DATA_REPLY)
 *  C_SYNC_SOURCE -> C_SYNC_TARGET         (P_RS_DATA_REPLY)
 */
int drbd_send_block(struct drbd_conf *mdev, enum drbd_packets cmd,
		    struct drbd_epoch_entry *e)
{
	int ok;
	struct p_data p;
	void *dgb;
	int dgs;

	dgs = (mdev->agreed_pro_version >= 87 && mdev->integrity_w_tfm) ?
		crypto_hash_digestsize(mdev->integrity_w_tfm) : 0;

	if (e->size <= DRBD_MAX_SIZE_H80_PACKET) {
		p.head.h80.magic   = BE_DRBD_MAGIC;
		p.head.h80.command = cpu_to_be16(cmd);
		p.head.h80.length  =
			cpu_to_be16(sizeof(p) - sizeof(struct p_header80) + dgs + e->size);
	} else {
		p.head.h95.magic   = BE_DRBD_MAGIC_BIG;
		p.head.h95.command = cpu_to_be16(cmd);
		p.head.h95.length  =
			cpu_to_be32(sizeof(p) - sizeof(struct p_header80) + dgs + e->size);
	}

	p.sector   = cpu_to_be64(e->sector);
	p.block_id = e->block_id;
	/* p.seq_num  = 0;    No sequence numbers here.. */

	/* Only called by our kernel thread.
	 * This one may be interrupted by DRBD_SIG and/or DRBD_SIGKILL
	 * in response to admin command or module unload.
	 */
	if (!drbd_get_data_sock(mdev))
		return 0;

	ok = sizeof(p) == drbd_send(mdev, mdev->data.socket, &p, sizeof(p), dgs ? MSG_MORE : 0);
	if (ok && dgs) {
		dgb = mdev->int_dig_out;
		drbd_csum_ee(mdev, mdev->integrity_w_tfm, e, dgb);
		ok = dgs == drbd_send(mdev, mdev->data.socket, dgb, dgs, 0);
	}
	if (ok)
		ok = _drbd_send_zc_ee(mdev, e);

	drbd_put_data_sock(mdev);

	return ok;
}

int drbd_send_oos(struct drbd_conf *mdev, struct drbd_request *req)
{
	struct p_block_desc p;

	p.sector  = cpu_to_be64(req->sector);
	p.blksize = cpu_to_be32(req->size);

	return drbd_send_cmd(mdev, USE_DATA_SOCKET, P_OUT_OF_SYNC, &p.head, sizeof(p));
}

/*
  drbd_send distinguishes two cases:

  Packets sent via the data socket "sock"
  and packets sent via the meta data socket "msock"

		    sock                      msock
  -----------------+-------------------------+------------------------------
  timeout           conf.timeout / 2          conf.timeout / 2
  timeout action    send a ping via msock     Abort communication
					      and close all sockets
*/

/*
 * you must have down()ed the appropriate [m]sock_mutex elsewhere!
 */
int drbd_send(struct drbd_conf *mdev, struct socket *sock,
	      void *buf, size_t size, unsigned msg_flags)
{
	struct kvec iov;
	struct msghdr msg;
	int rv, sent = 0;

	if (!sock)
		return -1000;

	/* THINK  if (signal_pending) return ... ? */

	iov.iov_base = buf;
	iov.iov_len  = size;

	msg.msg_name       = NULL;
	msg.msg_namelen    = 0;
	msg.msg_control    = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags      = msg_flags | MSG_NOSIGNAL;

	if (sock == mdev->data.socket) {
		mdev->ko_count = mdev->net_conf->ko_count;
		drbd_update_congested(mdev);
	}
	do {
		/* STRANGE
		 * tcp_sendmsg does _not_ use its size parameter at all ?
		 *
		 * -EAGAIN on timeout, -EINTR on signal.
		 */
/* THINK
 * do we need to block DRBD_SIG if sock == &meta.socket ??
 * otherwise wake_asender() might interrupt some send_*Ack !
 */
		rv = kernel_sendmsg(sock, &msg, &iov, 1, size);
		if (rv == -EAGAIN) {
			if (we_should_drop_the_connection(mdev, sock))
				break;
			else
				continue;
		}
		D_ASSERT(rv != 0);
		if (rv == -EINTR) {
			flush_signals(current);
			rv = 0;
		}
		if (rv < 0)
			break;
		sent += rv;
		iov.iov_base += rv;
		iov.iov_len  -= rv;
	} while (sent < size);

	if (sock == mdev->data.socket)
		clear_bit(NET_CONGESTED, &mdev->flags);

	if (rv <= 0) {
		if (rv != -EAGAIN) {
			dev_err(DEV, "%s_sendmsg returned %d\n",
			    sock == mdev->meta.socket ? "msock" : "sock",
			    rv);
			drbd_force_state(mdev, NS(conn, C_BROKEN_PIPE));
		} else
			drbd_force_state(mdev, NS(conn, C_TIMEOUT));
	}

	return sent;
}

static int drbd_open(struct block_device *bdev, fmode_t mode)
{
	struct drbd_conf *mdev = bdev->bd_disk->private_data;
	unsigned long flags;
	int rv = 0;

	mutex_lock(&drbd_main_mutex);
	spin_lock_irqsave(&mdev->req_lock, flags);
	/* to have a stable mdev->state.role
	 * and no race with updating open_cnt */

	if (mdev->state.role != R_PRIMARY) {
		if (mode & FMODE_WRITE)
			rv = -EROFS;
		else if (!allow_oos)
			rv = -EMEDIUMTYPE;
	}

	if (!rv)
		mdev->open_cnt++;
	spin_unlock_irqrestore(&mdev->req_lock, flags);
	mutex_unlock(&drbd_main_mutex);

	return rv;
}

static int drbd_release(struct gendisk *gd, fmode_t mode)
{
	struct drbd_conf *mdev = gd->private_data;
	mutex_lock(&drbd_main_mutex);
	mdev->open_cnt--;
	mutex_unlock(&drbd_main_mutex);
	return 0;
}

static void drbd_set_defaults(struct drbd_conf *mdev)
{
	/* This way we get a compile error when sync_conf grows,
	   and we forgot to initialize it here */
	mdev->sync_conf = (struct syncer_conf) {
		/* .rate = */		DRBD_RATE_DEF,
		/* .after = */		DRBD_AFTER_DEF,
		/* .al_extents = */	DRBD_AL_EXTENTS_DEF,
		/* .verify_alg = */	{}, 0,
		/* .cpu_mask = */	{}, 0,
		/* .csums_alg = */	{}, 0,
		/* .use_rle = */	0,
		/* .on_no_data = */	DRBD_ON_NO_DATA_DEF,
		/* .c_plan_ahead = */	DRBD_C_PLAN_AHEAD_DEF,
		/* .c_delay_target = */	DRBD_C_DELAY_TARGET_DEF,
		/* .c_fill_target = */	DRBD_C_FILL_TARGET_DEF,
		/* .c_max_rate = */	DRBD_C_MAX_RATE_DEF,
		/* .c_min_rate = */	DRBD_C_MIN_RATE_DEF
	};

	/* Have to use that way, because the layout differs between
	   big endian and little endian */
	mdev->state = (union drbd_state) {
		{ .role = R_SECONDARY,
		  .peer = R_UNKNOWN,
		  .conn = C_STANDALONE,
		  .disk = D_DISKLESS,
		  .pdsk = D_UNKNOWN,
		  .susp = 0,
		  .susp_nod = 0,
		  .susp_fen = 0
		} };
}

void drbd_init_set_defaults(struct drbd_conf *mdev)
{
	/* the memset(,0,) did most of this.
	 * note: only assignments, no allocation in here */

	drbd_set_defaults(mdev);

	atomic_set(&mdev->ap_bio_cnt, 0);
	atomic_set(&mdev->ap_pending_cnt, 0);
	atomic_set(&mdev->rs_pending_cnt, 0);
	atomic_set(&mdev->unacked_cnt, 0);
	atomic_set(&mdev->local_cnt, 0);
	atomic_set(&mdev->net_cnt, 0);
	atomic_set(&mdev->packet_seq, 0);
	atomic_set(&mdev->pp_in_use, 0);
	atomic_set(&mdev->pp_in_use_by_net, 0);
	atomic_set(&mdev->rs_sect_in, 0);
	atomic_set(&mdev->rs_sect_ev, 0);
	atomic_set(&mdev->ap_in_flight, 0);
	atomic_set(&mdev->md_io_in_use, 0);

	mutex_init(&mdev->data.mutex);
	mutex_init(&mdev->meta.mutex);
	sema_init(&mdev->data.work.s, 0);
	sema_init(&mdev->meta.work.s, 0);
	mutex_init(&mdev->state_mutex);

	spin_lock_init(&mdev->data.work.q_lock);
	spin_lock_init(&mdev->meta.work.q_lock);

	spin_lock_init(&mdev->al_lock);
	spin_lock_init(&mdev->req_lock);
	spin_lock_init(&mdev->peer_seq_lock);
	spin_lock_init(&mdev->epoch_lock);

	INIT_LIST_HEAD(&mdev->active_ee);
	INIT_LIST_HEAD(&mdev->sync_ee);
	INIT_LIST_HEAD(&mdev->done_ee);
	INIT_LIST_HEAD(&mdev->read_ee);
	INIT_LIST_HEAD(&mdev->net_ee);
	INIT_LIST_HEAD(&mdev->resync_reads);
	INIT_LIST_HEAD(&mdev->data.work.q);
	INIT_LIST_HEAD(&mdev->meta.work.q);
	INIT_LIST_HEAD(&mdev->resync_work.list);
	INIT_LIST_HEAD(&mdev->unplug_work.list);
	INIT_LIST_HEAD(&mdev->go_diskless.list);
	INIT_LIST_HEAD(&mdev->md_sync_work.list);
	INIT_LIST_HEAD(&mdev->start_resync_work.list);
	INIT_LIST_HEAD(&mdev->bm_io_work.w.list);

	mdev->resync_work.cb  = w_resync_timer;
	mdev->unplug_work.cb  = w_send_write_hint;
	mdev->go_diskless.cb  = w_go_diskless;
	mdev->md_sync_work.cb = w_md_sync;
	mdev->bm_io_work.w.cb = w_bitmap_io;
	mdev->start_resync_work.cb = w_start_resync;
	init_timer(&mdev->resync_timer);
	init_timer(&mdev->md_sync_timer);
	init_timer(&mdev->start_resync_timer);
	init_timer(&mdev->request_timer);
	mdev->resync_timer.function = resync_timer_fn;
	mdev->resync_timer.data = (unsigned long) mdev;
	mdev->md_sync_timer.function = md_sync_timer_fn;
	mdev->md_sync_timer.data = (unsigned long) mdev;
	mdev->start_resync_timer.function = start_resync_timer_fn;
	mdev->start_resync_timer.data = (unsigned long) mdev;
	mdev->request_timer.function = request_timer_fn;
	mdev->request_timer.data = (unsigned long) mdev;

	init_waitqueue_head(&mdev->misc_wait);
	init_waitqueue_head(&mdev->state_wait);
	init_waitqueue_head(&mdev->net_cnt_wait);
	init_waitqueue_head(&mdev->ee_wait);
	init_waitqueue_head(&mdev->al_wait);
	init_waitqueue_head(&mdev->seq_wait);

	drbd_thread_init(mdev, &mdev->receiver, drbdd_init);
	drbd_thread_init(mdev, &mdev->worker, drbd_worker);
	drbd_thread_init(mdev, &mdev->asender, drbd_asender);

	mdev->agreed_pro_version = PRO_VERSION_MAX;
	mdev->write_ordering = WO_bdev_flush;
	mdev->resync_wenr = LC_FREE;
	mdev->peer_max_bio_size = DRBD_MAX_BIO_SIZE_SAFE;
	mdev->local_max_bio_size = DRBD_MAX_BIO_SIZE_SAFE;
}

void drbd_mdev_cleanup(struct drbd_conf *mdev)
{
	int i;
	if (mdev->receiver.t_state != None)
		dev_err(DEV, "ASSERT FAILED: receiver t_state == %d expected 0.\n",
				mdev->receiver.t_state);

	/* no need to lock it, I'm the only thread alive */
	if (atomic_read(&mdev->current_epoch->epoch_size) !=  0)
		dev_err(DEV, "epoch_size:%d\n", atomic_read(&mdev->current_epoch->epoch_size));
	mdev->al_writ_cnt  =
	mdev->bm_writ_cnt  =
	mdev->read_cnt     =
	mdev->recv_cnt     =
	mdev->send_cnt     =
	mdev->writ_cnt     =
	mdev->p_size       =
	mdev->rs_start     =
	mdev->rs_total     =
	mdev->rs_failed    = 0;
	mdev->rs_last_events = 0;
	mdev->rs_last_sect_ev = 0;
	for (i = 0; i < DRBD_SYNC_MARKS; i++) {
		mdev->rs_mark_left[i] = 0;
		mdev->rs_mark_time[i] = 0;
	}
	D_ASSERT(mdev->net_conf == NULL);

	drbd_set_my_capacity(mdev, 0);
	if (mdev->bitmap) {
		/* maybe never allocated. */
		drbd_bm_resize(mdev, 0, 1);
		drbd_bm_cleanup(mdev);
	}

	drbd_free_resources(mdev);
	clear_bit(AL_SUSPENDED, &mdev->flags);

	/*
	 * currently we drbd_init_ee only on module load, so
	 * we may do drbd_release_ee only on module unload!
	 */
	D_ASSERT(list_empty(&mdev->active_ee));
	D_ASSERT(list_empty(&mdev->sync_ee));
	D_ASSERT(list_empty(&mdev->done_ee));
	D_ASSERT(list_empty(&mdev->read_ee));
	D_ASSERT(list_empty(&mdev->net_ee));
	D_ASSERT(list_empty(&mdev->resync_reads));
	D_ASSERT(list_empty(&mdev->data.work.q));
	D_ASSERT(list_empty(&mdev->meta.work.q));
	D_ASSERT(list_empty(&mdev->resync_work.list));
	D_ASSERT(list_empty(&mdev->unplug_work.list));
	D_ASSERT(list_empty(&mdev->go_diskless.list));

	drbd_set_defaults(mdev);
}


static void drbd_destroy_mempools(void)
{
	struct page *page;

	while (drbd_pp_pool) {
		page = drbd_pp_pool;
		drbd_pp_pool = (struct page *)page_private(page);
		__free_page(page);
		drbd_pp_vacant--;
	}

	/* D_ASSERT(atomic_read(&drbd_pp_vacant)==0); */

	if (drbd_md_io_bio_set)
		bioset_free(drbd_md_io_bio_set);
	if (drbd_md_io_page_pool)
		mempool_destroy(drbd_md_io_page_pool);
	if (drbd_ee_mempool)
		mempool_destroy(drbd_ee_mempool);
	if (drbd_request_mempool)
		mempool_destroy(drbd_request_mempool);
	if (drbd_ee_cache)
		kmem_cache_destroy(drbd_ee_cache);
	if (drbd_request_cache)
		kmem_cache_destroy(drbd_request_cache);
	if (drbd_bm_ext_cache)
		kmem_cache_destroy(drbd_bm_ext_cache);
	if (drbd_al_ext_cache)
		kmem_cache_destroy(drbd_al_ext_cache);

	drbd_md_io_bio_set   = NULL;
	drbd_md_io_page_pool = NULL;
	drbd_ee_mempool      = NULL;
	drbd_request_mempool = NULL;
	drbd_ee_cache        = NULL;
	drbd_request_cache   = NULL;
	drbd_bm_ext_cache    = NULL;
	drbd_al_ext_cache    = NULL;

	return;
}

static int drbd_create_mempools(void)
{
	struct page *page;
	const int number = (DRBD_MAX_BIO_SIZE/PAGE_SIZE) * minor_count;
	int i;

	/* prepare our caches and mempools */
	drbd_request_mempool = NULL;
	drbd_ee_cache        = NULL;
	drbd_request_cache   = NULL;
	drbd_bm_ext_cache    = NULL;
	drbd_al_ext_cache    = NULL;
	drbd_pp_pool         = NULL;
	drbd_md_io_page_pool = NULL;
	drbd_md_io_bio_set   = NULL;

	/* caches */
	drbd_request_cache = kmem_cache_create(
		"drbd_req", sizeof(struct drbd_request), 0, 0, NULL);
	if (drbd_request_cache == NULL)
		goto Enomem;

	drbd_ee_cache = kmem_cache_create(
		"drbd_ee", sizeof(struct drbd_epoch_entry), 0, 0, NULL);
	if (drbd_ee_cache == NULL)
		goto Enomem;

	drbd_bm_ext_cache = kmem_cache_create(
		"drbd_bm", sizeof(struct bm_extent), 0, 0, NULL);
	if (drbd_bm_ext_cache == NULL)
		goto Enomem;

	drbd_al_ext_cache = kmem_cache_create(
		"drbd_al", sizeof(struct lc_element), 0, 0, NULL);
	if (drbd_al_ext_cache == NULL)
		goto Enomem;

	/* mempools */
#ifdef COMPAT_HAVE_BIOSET_CREATE
	drbd_md_io_bio_set = bioset_create(DRBD_MIN_POOL_PAGES, 0);
	if (drbd_md_io_bio_set == NULL)
		goto Enomem;
#endif

	drbd_md_io_page_pool = mempool_create_page_pool(DRBD_MIN_POOL_PAGES, 0);
	if (drbd_md_io_page_pool == NULL)
		goto Enomem;

	drbd_request_mempool = mempool_create(number,
		mempool_alloc_slab, mempool_free_slab, drbd_request_cache);
	if (drbd_request_mempool == NULL)
		goto Enomem;

	drbd_ee_mempool = mempool_create(number,
		mempool_alloc_slab, mempool_free_slab, drbd_ee_cache);
	if (drbd_ee_mempool == NULL)
		goto Enomem;

	/* drbd's page pool */
	spin_lock_init(&drbd_pp_lock);

	for (i = 0; i < number; i++) {
		page = alloc_page(GFP_HIGHUSER);
		if (!page)
			goto Enomem;
		set_page_private(page, (unsigned long)drbd_pp_pool);
		drbd_pp_pool = page;
	}
	drbd_pp_vacant = number;

	return 0;

Enomem:
	drbd_destroy_mempools(); /* in case we allocated some */
	return -ENOMEM;
}

static int drbd_notify_sys(struct notifier_block *this, unsigned long code,
	void *unused)
{
	/* just so we have it.  you never know what interesting things we
	 * might want to do here some day...
	 */

	return NOTIFY_DONE;
}

static struct notifier_block drbd_notifier = {
	.notifier_call = drbd_notify_sys,
};

static void drbd_release_ee_lists(struct drbd_conf *mdev)
{
	int rr;

	rr = drbd_release_ee(mdev, &mdev->active_ee);
	if (rr)
		dev_err(DEV, "%d EEs in active list found!\n", rr);

	rr = drbd_release_ee(mdev, &mdev->sync_ee);
	if (rr)
		dev_err(DEV, "%d EEs in sync list found!\n", rr);

	rr = drbd_release_ee(mdev, &mdev->read_ee);
	if (rr)
		dev_err(DEV, "%d EEs in read list found!\n", rr);

	rr = drbd_release_ee(mdev, &mdev->done_ee);
	if (rr)
		dev_err(DEV, "%d EEs in done list found!\n", rr);

	rr = drbd_release_ee(mdev, &mdev->net_ee);
	if (rr)
		dev_err(DEV, "%d EEs in net list found!\n", rr);
}

/* caution. no locking.
 * currently only used from module cleanup code. */
static void drbd_delete_device(unsigned int minor)
{
	struct drbd_conf *mdev = minor_to_mdev(minor);

	if (!mdev)
		return;

	del_timer_sync(&mdev->request_timer);

	/* paranoia asserts */
	if (mdev->open_cnt != 0)
		dev_err(DEV, "open_cnt = %d in %s:%u", mdev->open_cnt,
				__FILE__ , __LINE__);

	ERR_IF (!list_empty(&mdev->data.work.q)) {
		struct list_head *lp;
		list_for_each(lp, &mdev->data.work.q) {
			dev_err(DEV, "lp = %p\n", lp);
		}
	};
	/* end paranoia asserts */

	del_gendisk(mdev->vdisk);

	/* cleanup stuff that may have been allocated during
	 * device (re-)configuration or state changes */

	if (mdev->this_bdev)
		bdput(mdev->this_bdev);

	drbd_free_resources(mdev);

	drbd_release_ee_lists(mdev);

	/* should be freed on disconnect? */
	kfree(mdev->ee_hash);
	/*
	mdev->ee_hash_s = 0;
	mdev->ee_hash = NULL;
	*/

	lc_destroy(mdev->act_log);
	lc_destroy(mdev->resync);

	kfree(mdev->p_uuid);
	/* mdev->p_uuid = NULL; */

	kfree(mdev->int_dig_out);
	kfree(mdev->int_dig_in);
	kfree(mdev->int_dig_vv);

	/* cleanup the rest that has been
	 * allocated from drbd_new_device
	 * and actually free the mdev itself */
	drbd_free_mdev(mdev);
}

static void drbd_cleanup(void)
{
	unsigned int i;

	unregister_reboot_notifier(&drbd_notifier);

	/* first remove proc,
	 * drbdsetup uses it's presence to detect
	 * whether DRBD is loaded.
	 * If we would get stuck in proc removal,
	 * but have netlink already deregistered,
	 * some drbdsetup commands may wait forever
	 * for an answer.
	 */
	if (drbd_proc)
		remove_proc_entry("drbd", NULL);

	drbd_nl_cleanup();

	if (minor_table) {
		i = minor_count;
		while (i--)
			drbd_delete_device(i);
		drbd_destroy_mempools();
	}

	kfree(minor_table);

	unregister_blkdev(DRBD_MAJOR, "drbd");

	printk(KERN_INFO "drbd: module cleanup done.\n");
}

/**
 * drbd_congested() - Callback for the flusher thread
 * @congested_data:	User data
 * @bdi_bits:		Bits the BDI flusher thread is currently interested in
 *
 * Returns 1<<BDI_async_congested and/or 1<<BDI_sync_congested if we are congested.
 */
static int drbd_congested(void *congested_data, int bdi_bits)
{
	struct drbd_conf *mdev = congested_data;
	struct request_queue *q;
	char reason = '-';
	int r = 0;

	if (!may_inc_ap_bio(mdev)) {
		/* DRBD has frozen IO */
		r = bdi_bits;
		reason = 'd';
		goto out;
	}

	if (test_bit(CALLBACK_PENDING, &mdev->flags)) {
		r |= (1 << BDI_async_congested);
		/* Without good local data, we would need to read from remote,
		 * and that would need the worker thread as well, which is
		 * currently blocked waiting for that usermode helper to
		 * finish.
		 */
		if (!get_ldev_if_state(mdev, D_UP_TO_DATE))
			r |= (1 << BDI_sync_congested);
		else
			put_ldev(mdev);
		r &= bdi_bits;
		reason = 'c';
		goto out;
	}

	if (get_ldev(mdev)) {
		q = bdev_get_queue(mdev->ldev->backing_bdev);
		r = bdi_congested(&q->backing_dev_info, bdi_bits);
		put_ldev(mdev);
		if (r)
			reason = 'b';
	}

	if (bdi_bits & (1 << BDI_async_congested) && test_bit(NET_CONGESTED, &mdev->flags)) {
		r |= (1 << BDI_async_congested);
		reason = reason == 'b' ? 'a' : 'n';
	}

out:
	mdev->congestion_reason = reason;
	return r;
}

struct drbd_conf *drbd_new_device(unsigned int minor)
{
	struct drbd_conf *mdev;
	struct gendisk *disk;
	struct request_queue *q;

	/* GFP_KERNEL, we are outside of all write-out paths */
	mdev = kzalloc(sizeof(struct drbd_conf), GFP_KERNEL);
	if (!mdev)
		return NULL;
	if (!zalloc_cpumask_var(&mdev->cpu_mask, GFP_KERNEL))
		goto out_no_cpumask;

	mdev->minor = minor;

	drbd_init_set_defaults(mdev);

	q = blk_alloc_queue(GFP_KERNEL);
	if (!q)
		goto out_no_q;
	mdev->rq_queue = q;
	q->queuedata   = mdev;

	disk = alloc_disk(1);
	if (!disk)
		goto out_no_disk;
	mdev->vdisk = disk;

	set_disk_ro(disk, true);

	disk->queue = q;
	disk->major = DRBD_MAJOR;
	disk->first_minor = minor;
	disk->fops = &drbd_ops;
	sprintf(disk->disk_name, "drbd%d", minor);
	disk->private_data = mdev;

	mdev->this_bdev = bdget(MKDEV(DRBD_MAJOR, minor));
	/* we have no partitions. we contain only ourselves. */
	mdev->this_bdev->bd_contains = mdev->this_bdev;

	q->backing_dev_info.congested_fn = drbd_congested;
	q->backing_dev_info.congested_data = mdev;

	blk_queue_make_request(q, drbd_make_request);
	blk_queue_flush(q, REQ_FLUSH | REQ_FUA);
	/* Setting the max_hw_sectors to an odd value of 8kibyte here
	   This triggers a max_bio_size message upon first attach or connect */
	blk_queue_max_hw_sectors(q, DRBD_MAX_BIO_SIZE_SAFE >> 8);
	blk_queue_bounce_limit(q, BLK_BOUNCE_ANY);
	blk_queue_merge_bvec(q, drbd_merge_bvec);
	q->queue_lock = &mdev->req_lock;

	mdev->md_io_page = alloc_page(GFP_KERNEL);
	if (!mdev->md_io_page)
		goto out_no_io_page;

	if (drbd_bm_init(mdev))
		goto out_no_bitmap;
	/* no need to lock access, we are still initializing this minor device. */
	if (!tl_init(mdev))
		goto out_no_tl;

	mdev->app_reads_hash = kzalloc(APP_R_HSIZE*sizeof(void *), GFP_KERNEL);
	if (!mdev->app_reads_hash)
		goto out_no_app_reads;

	mdev->current_epoch = kzalloc(sizeof(struct drbd_epoch), GFP_KERNEL);
	if (!mdev->current_epoch)
		goto out_no_epoch;

	INIT_LIST_HEAD(&mdev->current_epoch->list);
	mdev->epochs = 1;

	return mdev;

/* out_whatever_else:
	kfree(mdev->current_epoch); */
out_no_epoch:
	kfree(mdev->app_reads_hash);
out_no_app_reads:
	tl_cleanup(mdev);
out_no_tl:
	drbd_bm_cleanup(mdev);
out_no_bitmap:
	__free_page(mdev->md_io_page);
out_no_io_page:
	put_disk(disk);
out_no_disk:
	blk_cleanup_queue(q);
out_no_q:
	free_cpumask_var(mdev->cpu_mask);
out_no_cpumask:
	kfree(mdev);
	return NULL;
}

/* counterpart of drbd_new_device.
 * last part of drbd_delete_device. */
void drbd_free_mdev(struct drbd_conf *mdev)
{
	kfree(mdev->current_epoch);
	kfree(mdev->app_reads_hash);
	tl_cleanup(mdev);
	if (mdev->bitmap) /* should no longer be there. */
		drbd_bm_cleanup(mdev);
	__free_page(mdev->md_io_page);
	put_disk(mdev->vdisk);
	blk_cleanup_queue(mdev->rq_queue);
	free_cpumask_var(mdev->cpu_mask);
	drbd_free_tl_hash(mdev);
	kfree(mdev);
}


int __init drbd_init(void)
{
	int err;

	if (sizeof(struct p_handshake) != 80) {
		printk(KERN_ERR
		       "drbd: never change the size or layout "
		       "of the HandShake packet.\n");
		return -EINVAL;
	}

	if (minor_count < DRBD_MINOR_COUNT_MIN || minor_count > DRBD_MINOR_COUNT_MAX) {
		printk(KERN_ERR
			"drbd: invalid minor_count (%d)\n", minor_count);
#ifdef MODULE
		return -EINVAL;
#else
		minor_count = 8;
#endif
	}

	err = drbd_nl_init();
	if (err)
		return err;

	err = register_blkdev(DRBD_MAJOR, "drbd");
	if (err) {
		printk(KERN_ERR
		       "drbd: unable to register block device major %d\n",
		       DRBD_MAJOR);
		return err;
	}

	register_reboot_notifier(&drbd_notifier);

	/*
	 * allocate all necessary structs
	 */
	err = -ENOMEM;

	init_waitqueue_head(&drbd_pp_wait);

	drbd_proc = NULL; /* play safe for drbd_cleanup */
	minor_table = kzalloc(sizeof(struct drbd_conf *)*minor_count,
				GFP_KERNEL);
	if (!minor_table)
		goto Enomem;

	err = drbd_create_mempools();
	if (err)
		goto Enomem;

	drbd_proc = proc_create_data("drbd", S_IFREG | S_IRUGO , NULL, &drbd_proc_fops, NULL);
	if (!drbd_proc)	{
		printk(KERN_ERR "drbd: unable to register proc file\n");
		goto Enomem;
	}

	rwlock_init(&global_state_lock);

	printk(KERN_INFO "drbd: initialized. "
	       "Version: " REL_VERSION " (api:%d/proto:%d-%d)\n",
	       API_VERSION, PRO_VERSION_MIN, PRO_VERSION_MAX);
	printk(KERN_INFO "drbd: %s\n", drbd_buildtag());
	printk(KERN_INFO "drbd: registered as block device major %d\n",
		DRBD_MAJOR);
	printk(KERN_INFO "drbd: minor_table @ 0x%p\n", minor_table);

	return 0; /* Success! */

Enomem:
	drbd_cleanup();
	if (err == -ENOMEM)
		/* currently always the case */
		printk(KERN_ERR "drbd: ran out of memory\n");
	else
		printk(KERN_ERR "drbd: initialization failure\n");
	return err;
}

void drbd_free_bc(struct drbd_backing_dev *ldev)
{
	if (ldev == NULL)
		return;

	blkdev_put(ldev->backing_bdev, FMODE_READ | FMODE_WRITE | FMODE_EXCL);
	blkdev_put(ldev->md_bdev, FMODE_READ | FMODE_WRITE | FMODE_EXCL);

	kfree(ldev);
}

void drbd_free_sock(struct drbd_conf *mdev)
{
	if (mdev->data.socket) {
		mutex_lock(&mdev->data.mutex);
		kernel_sock_shutdown(mdev->data.socket, SHUT_RDWR);
		sock_release(mdev->data.socket);
		mdev->data.socket = NULL;
		mutex_unlock(&mdev->data.mutex);
	}
	if (mdev->meta.socket) {
		mutex_lock(&mdev->meta.mutex);
		kernel_sock_shutdown(mdev->meta.socket, SHUT_RDWR);
		sock_release(mdev->meta.socket);
		mdev->meta.socket = NULL;
		mutex_unlock(&mdev->meta.mutex);
	}
}


void drbd_free_resources(struct drbd_conf *mdev)
{
	crypto_free_hash(mdev->csums_tfm);
	mdev->csums_tfm = NULL;
	crypto_free_hash(mdev->verify_tfm);
	mdev->verify_tfm = NULL;
	crypto_free_hash(mdev->cram_hmac_tfm);
	mdev->cram_hmac_tfm = NULL;
	crypto_free_hash(mdev->integrity_w_tfm);
	mdev->integrity_w_tfm = NULL;
	crypto_free_hash(mdev->integrity_r_tfm);
	mdev->integrity_r_tfm = NULL;

	drbd_free_sock(mdev);

	__no_warn(local,
		  drbd_free_bc(mdev->ldev);
		  mdev->ldev = NULL;);
}

/* meta data management */

struct meta_data_on_disk {
	u64 la_size;           /* last agreed size. */
	u64 uuid[UI_SIZE];   /* UUIDs. */
	u64 device_uuid;
	u64 reserved_u64_1;
	u32 flags;             /* MDF */
	u32 magic;
	u32 md_size_sect;
	u32 al_offset;         /* offset to this block */
	u32 al_nr_extents;     /* important for restoring the AL */
	      /* `-- act_log->nr_elements <-- sync_conf.al_extents */
	u32 bm_offset;         /* offset to the bitmap, from here */
	u32 bm_bytes_per_bit;  /* BM_BLOCK_SIZE */
	u32 la_peer_max_bio_size;   /* last peer max_bio_size */
	u32 reserved_u32[3];

} __packed;

/**
 * drbd_md_sync() - Writes the meta data super block if the MD_DIRTY flag bit is set
 * @mdev:	DRBD device.
 */
void drbd_md_sync(struct drbd_conf *mdev)
{
	struct meta_data_on_disk *buffer;
	sector_t sector;
	int i;

	del_timer(&mdev->md_sync_timer);
	/* timer may be rearmed by drbd_md_mark_dirty() now. */
	if (!test_and_clear_bit(MD_DIRTY, &mdev->flags))
		return;

	/* We use here D_FAILED and not D_ATTACHING because we try to write
	 * metadata even if we detach due to a disk failure! */
	if (!get_ldev_if_state(mdev, D_FAILED))
		return;

	buffer = drbd_md_get_buffer(mdev);
	if (!buffer)
		goto out;

	memset(buffer, 0, 512);

	buffer->la_size = cpu_to_be64(drbd_get_capacity(mdev->this_bdev));
	for (i = UI_CURRENT; i < UI_SIZE; i++)
		buffer->uuid[i] = cpu_to_be64(mdev->ldev->md.uuid[i]);
	buffer->flags = cpu_to_be32(mdev->ldev->md.flags);
	buffer->magic = cpu_to_be32(DRBD_MD_MAGIC);

	buffer->md_size_sect  = cpu_to_be32(mdev->ldev->md.md_size_sect);
	buffer->al_offset     = cpu_to_be32(mdev->ldev->md.al_offset);
	buffer->al_nr_extents = cpu_to_be32(mdev->act_log->nr_elements);
	buffer->bm_bytes_per_bit = cpu_to_be32(BM_BLOCK_SIZE);
	buffer->device_uuid = cpu_to_be64(mdev->ldev->md.device_uuid);

	buffer->bm_offset = cpu_to_be32(mdev->ldev->md.bm_offset);
	buffer->la_peer_max_bio_size = cpu_to_be32(mdev->peer_max_bio_size);

	D_ASSERT(drbd_md_ss__(mdev, mdev->ldev) == mdev->ldev->md.md_offset);
	sector = mdev->ldev->md.md_offset;

	if (!drbd_md_sync_page_io(mdev, mdev->ldev, sector, WRITE)) {
		/* this was a try anyways ... */
		dev_err(DEV, "meta data update failed!\n");
		drbd_chk_io_error(mdev, 1, DRBD_META_IO_ERROR);
	}

	/* Update mdev->ldev->md.la_size_sect,
	 * since we updated it on metadata. */
	mdev->ldev->md.la_size_sect = drbd_get_capacity(mdev->this_bdev);

	drbd_md_put_buffer(mdev);
out:
	put_ldev(mdev);
}

/**
 * drbd_md_read() - Reads in the meta data super block
 * @mdev:	DRBD device.
 * @bdev:	Device from which the meta data should be read in.
 *
 * Return 0 (NO_ERROR) on success, and an enum drbd_ret_code in case
 * something goes wrong.  Currently only: ERR_IO_MD_DISK, ERR_MD_INVALID.
 */
int drbd_md_read(struct drbd_conf *mdev, struct drbd_backing_dev *bdev)
{
	struct meta_data_on_disk *buffer;
	int i, rv = NO_ERROR;

	if (!get_ldev_if_state(mdev, D_ATTACHING))
		return ERR_IO_MD_DISK;

	buffer = drbd_md_get_buffer(mdev);
	if (!buffer)
		goto out;

	if (!drbd_md_sync_page_io(mdev, bdev, bdev->md.md_offset, READ)) {
		/* NOTE: can't do normal error processing here as this is
		   called BEFORE disk is attached */
		dev_err(DEV, "Error while reading metadata.\n");
		rv = ERR_IO_MD_DISK;
		goto err;
	}

	if (be32_to_cpu(buffer->magic) != DRBD_MD_MAGIC) {
		dev_err(DEV, "Error while reading metadata, magic not found.\n");
		rv = ERR_MD_INVALID;
		goto err;
	}
	if (be32_to_cpu(buffer->al_offset) != bdev->md.al_offset) {
		dev_err(DEV, "unexpected al_offset: %d (expected %d)\n",
		    be32_to_cpu(buffer->al_offset), bdev->md.al_offset);
		rv = ERR_MD_INVALID;
		goto err;
	}
	if (be32_to_cpu(buffer->bm_offset) != bdev->md.bm_offset) {
		dev_err(DEV, "unexpected bm_offset: %d (expected %d)\n",
		    be32_to_cpu(buffer->bm_offset), bdev->md.bm_offset);
		rv = ERR_MD_INVALID;
		goto err;
	}
	if (be32_to_cpu(buffer->md_size_sect) != bdev->md.md_size_sect) {
		dev_err(DEV, "unexpected md_size: %u (expected %u)\n",
		    be32_to_cpu(buffer->md_size_sect), bdev->md.md_size_sect);
		rv = ERR_MD_INVALID;
		goto err;
	}

	if (be32_to_cpu(buffer->bm_bytes_per_bit) != BM_BLOCK_SIZE) {
		dev_err(DEV, "unexpected bm_bytes_per_bit: %u (expected %u)\n",
		    be32_to_cpu(buffer->bm_bytes_per_bit), BM_BLOCK_SIZE);
		rv = ERR_MD_INVALID;
		goto err;
	}

	bdev->md.la_size_sect = be64_to_cpu(buffer->la_size);
	for (i = UI_CURRENT; i < UI_SIZE; i++)
		bdev->md.uuid[i] = be64_to_cpu(buffer->uuid[i]);
	bdev->md.flags = be32_to_cpu(buffer->flags);
	mdev->sync_conf.al_extents = be32_to_cpu(buffer->al_nr_extents);
	bdev->md.device_uuid = be64_to_cpu(buffer->device_uuid);

	spin_lock_irq(&mdev->req_lock);
	if (mdev->state.conn < C_CONNECTED) {
		unsigned int peer;
		peer = be32_to_cpu(buffer->la_peer_max_bio_size);
		peer = max(peer, DRBD_MAX_BIO_SIZE_SAFE);
		mdev->peer_max_bio_size = peer;
	}
	spin_unlock_irq(&mdev->req_lock);

	if (mdev->sync_conf.al_extents < 7)
		mdev->sync_conf.al_extents = 127;

 err:
	drbd_md_put_buffer(mdev);
 out:
	put_ldev(mdev);

	return rv;
}

/**
 * drbd_md_mark_dirty() - Mark meta data super block as dirty
 * @mdev:	DRBD device.
 *
 * Call this function if you change anything that should be written to
 * the meta-data super block. This function sets MD_DIRTY, and starts a
 * timer that ensures that within five seconds you have to call drbd_md_sync().
 */
#ifdef DEBUG
void drbd_md_mark_dirty_(struct drbd_conf *mdev, unsigned int line, const char *func)
{
	if (!test_and_set_bit(MD_DIRTY, &mdev->flags)) {
		mod_timer(&mdev->md_sync_timer, jiffies + HZ);
		mdev->last_md_mark_dirty.line = line;
		mdev->last_md_mark_dirty.func = func;
	}
}
#else
void drbd_md_mark_dirty(struct drbd_conf *mdev)
{
	if (!test_and_set_bit(MD_DIRTY, &mdev->flags))
		mod_timer(&mdev->md_sync_timer, jiffies + 5*HZ);
}
#endif

static void drbd_uuid_move_history(struct drbd_conf *mdev) __must_hold(local)
{
	int i;

	for (i = UI_HISTORY_START; i < UI_HISTORY_END; i++)
		mdev->ldev->md.uuid[i+1] = mdev->ldev->md.uuid[i];
}

void _drbd_uuid_set(struct drbd_conf *mdev, int idx, u64 val) __must_hold(local)
{
	if (idx == UI_CURRENT) {
		if (mdev->state.role == R_PRIMARY)
			val |= 1;
		else
			val &= ~((u64)1);

		drbd_set_ed_uuid(mdev, val);
	}

	mdev->ldev->md.uuid[idx] = val;
	drbd_md_mark_dirty(mdev);
}


void drbd_uuid_set(struct drbd_conf *mdev, int idx, u64 val) __must_hold(local)
{
	if (mdev->ldev->md.uuid[idx]) {
		drbd_uuid_move_history(mdev);
		mdev->ldev->md.uuid[UI_HISTORY_START] = mdev->ldev->md.uuid[idx];
	}
	_drbd_uuid_set(mdev, idx, val);
}

/**
 * drbd_uuid_new_current() - Creates a new current UUID
 * @mdev:	DRBD device.
 *
 * Creates a new current UUID, and rotates the old current UUID into
 * the bitmap slot. Causes an incremental resync upon next connect.
 */
void drbd_uuid_new_current(struct drbd_conf *mdev) __must_hold(local)
{
	u64 val;
	unsigned long long bm_uuid = mdev->ldev->md.uuid[UI_BITMAP];

	if (bm_uuid)
		dev_warn(DEV, "bm UUID was already set: %llX\n", bm_uuid);

	mdev->ldev->md.uuid[UI_BITMAP] = mdev->ldev->md.uuid[UI_CURRENT];

	get_random_bytes(&val, sizeof(u64));
	_drbd_uuid_set(mdev, UI_CURRENT, val);
	drbd_print_uuids(mdev, "new current UUID");
	/* get it to stable storage _now_ */
	drbd_md_sync(mdev);
}

void drbd_uuid_set_bm(struct drbd_conf *mdev, u64 val) __must_hold(local)
{
	if (mdev->ldev->md.uuid[UI_BITMAP] == 0 && val == 0)
		return;

	if (val == 0) {
		drbd_uuid_move_history(mdev);
		mdev->ldev->md.uuid[UI_HISTORY_START] = mdev->ldev->md.uuid[UI_BITMAP];
		mdev->ldev->md.uuid[UI_BITMAP] = 0;
	} else {
		unsigned long long bm_uuid = mdev->ldev->md.uuid[UI_BITMAP];
		if (bm_uuid)
			dev_warn(DEV, "bm UUID was already set: %llX\n", bm_uuid);

		mdev->ldev->md.uuid[UI_BITMAP] = val & ~((u64)1);
	}
	drbd_md_mark_dirty(mdev);
}

/**
 * drbd_bmio_set_n_write() - io_fn for drbd_queue_bitmap_io() or drbd_bitmap_io()
 * @mdev:	DRBD device.
 *
 * Sets all bits in the bitmap and writes the whole bitmap to stable storage.
 */
int drbd_bmio_set_n_write(struct drbd_conf *mdev)
{
	int rv = -EIO;

	if (get_ldev_if_state(mdev, D_ATTACHING)) {
		drbd_md_set_flag(mdev, MDF_FULL_SYNC);
		drbd_md_sync(mdev);
		drbd_bm_set_all(mdev);

		rv = drbd_bm_write(mdev);

		if (!rv) {
			drbd_md_clear_flag(mdev, MDF_FULL_SYNC);
			drbd_md_sync(mdev);
		}

		put_ldev(mdev);
	}

	return rv;
}

/**
 * drbd_bmio_clear_n_write() - io_fn for drbd_queue_bitmap_io() or drbd_bitmap_io()
 * @mdev:	DRBD device.
 *
 * Clears all bits in the bitmap and writes the whole bitmap to stable storage.
 */
int drbd_bmio_clear_n_write(struct drbd_conf *mdev)
{
	int rv = -EIO;

	drbd_resume_al(mdev);
	if (get_ldev_if_state(mdev, D_ATTACHING)) {
		drbd_bm_clear_all(mdev);
		rv = drbd_bm_write(mdev);
		put_ldev(mdev);
	}

	return rv;
}

static int w_bitmap_io(struct drbd_conf *mdev, struct drbd_work *w, int unused)
{
	struct bm_io_work *work = container_of(w, struct bm_io_work, w);
	int rv = -EIO;

	D_ASSERT(atomic_read(&mdev->ap_bio_cnt) == 0);

	if (get_ldev(mdev)) {
		drbd_bm_lock(mdev, work->why, work->flags);
		rv = work->io_fn(mdev);
		drbd_bm_unlock(mdev);
		put_ldev(mdev);
	}

	clear_bit(BITMAP_IO, &mdev->flags);
	smp_mb__after_clear_bit();
	wake_up(&mdev->misc_wait);

	if (work->done)
		work->done(mdev, rv);

	clear_bit(BITMAP_IO_QUEUED, &mdev->flags);
	work->why = NULL;
	work->flags = 0;

	return 1;
}

void drbd_ldev_destroy(struct drbd_conf *mdev)
{
	lc_destroy(mdev->resync);
	mdev->resync = NULL;
	lc_destroy(mdev->act_log);
	mdev->act_log = NULL;
	__no_warn(local,
		drbd_free_bc(mdev->ldev);
		mdev->ldev = NULL;);

	if (mdev->md_io_tmpp) {
		__free_page(mdev->md_io_tmpp);
		mdev->md_io_tmpp = NULL;
	}
	clear_bit(GO_DISKLESS, &mdev->flags);
}

static int w_go_diskless(struct drbd_conf *mdev, struct drbd_work *w, int unused)
{
	D_ASSERT(mdev->state.disk == D_FAILED);
	/* we cannot assert local_cnt == 0 here, as get_ldev_if_state will
	 * inc/dec it frequently. Once we are D_DISKLESS, no one will touch
	 * the protected members anymore, though, so once put_ldev reaches zero
	 * again, it will be safe to free them. */
	drbd_force_state(mdev, NS(disk, D_DISKLESS));
	return 1;
}

void drbd_go_diskless(struct drbd_conf *mdev)
{
	D_ASSERT(mdev->state.disk == D_FAILED);
	if (!test_and_set_bit(GO_DISKLESS, &mdev->flags))
		drbd_queue_work(&mdev->data.work, &mdev->go_diskless);
}

/**
 * drbd_queue_bitmap_io() - Queues an IO operation on the whole bitmap
 * @mdev:	DRBD device.
 * @io_fn:	IO callback to be called when bitmap IO is possible
 * @done:	callback to be called after the bitmap IO was performed
 * @why:	Descriptive text of the reason for doing the IO
 *
 * While IO on the bitmap happens we freeze application IO thus we ensure
 * that drbd_set_out_of_sync() can not be called. This function MAY ONLY be
 * called from worker context. It MUST NOT be used while a previous such
 * work is still pending!
 */
void drbd_queue_bitmap_io(struct drbd_conf *mdev,
			  int (*io_fn)(struct drbd_conf *),
			  void (*done)(struct drbd_conf *, int),
			  char *why, enum bm_flag flags)
{
	D_ASSERT(current == mdev->worker.task);

	D_ASSERT(!test_bit(BITMAP_IO_QUEUED, &mdev->flags));
	D_ASSERT(!test_bit(BITMAP_IO, &mdev->flags));
	D_ASSERT(list_empty(&mdev->bm_io_work.w.list));
	if (mdev->bm_io_work.why)
		dev_err(DEV, "FIXME going to queue '%s' but '%s' still pending?\n",
			why, mdev->bm_io_work.why);

	mdev->bm_io_work.io_fn = io_fn;
	mdev->bm_io_work.done = done;
	mdev->bm_io_work.why = why;
	mdev->bm_io_work.flags = flags;

	spin_lock_irq(&mdev->req_lock);
	set_bit(BITMAP_IO, &mdev->flags);
	if (atomic_read(&mdev->ap_bio_cnt) == 0) {
		if (!test_and_set_bit(BITMAP_IO_QUEUED, &mdev->flags))
			drbd_queue_work(&mdev->data.work, &mdev->bm_io_work.w);
	}
	spin_unlock_irq(&mdev->req_lock);
}

/**
 * drbd_bitmap_io() -  Does an IO operation on the whole bitmap
 * @mdev:	DRBD device.
 * @io_fn:	IO callback to be called when bitmap IO is possible
 * @why:	Descriptive text of the reason for doing the IO
 *
 * freezes application IO while that the actual IO operations runs. This
 * functions MAY NOT be called from worker context.
 */
int drbd_bitmap_io(struct drbd_conf *mdev, int (*io_fn)(struct drbd_conf *),
		char *why, enum bm_flag flags)
{
	int rv;

	D_ASSERT(current != mdev->worker.task);

	if ((flags & BM_LOCKED_SET_ALLOWED) == 0)
		drbd_suspend_io(mdev);

	drbd_bm_lock(mdev, why, flags);
	rv = io_fn(mdev);
	drbd_bm_unlock(mdev);

	if ((flags & BM_LOCKED_SET_ALLOWED) == 0)
		drbd_resume_io(mdev);

	return rv;
}

void drbd_md_set_flag(struct drbd_conf *mdev, int flag) __must_hold(local)
{
	if ((mdev->ldev->md.flags & flag) != flag) {
		drbd_md_mark_dirty(mdev);
		mdev->ldev->md.flags |= flag;
	}
}

void drbd_md_clear_flag(struct drbd_conf *mdev, int flag) __must_hold(local)
{
	if ((mdev->ldev->md.flags & flag) != 0) {
		drbd_md_mark_dirty(mdev);
		mdev->ldev->md.flags &= ~flag;
	}
}
int drbd_md_test_flag(struct drbd_backing_dev *bdev, int flag)
{
	return (bdev->md.flags & flag) != 0;
}

static void md_sync_timer_fn(unsigned long data)
{
	struct drbd_conf *mdev = (struct drbd_conf *) data;

	drbd_queue_work_front(&mdev->data.work, &mdev->md_sync_work);
}

static int w_md_sync(struct drbd_conf *mdev, struct drbd_work *w, int unused)
{
	dev_warn(DEV, "md_sync_timer expired! Worker calls drbd_md_sync().\n");
#ifdef DEBUG
	dev_warn(DEV, "last md_mark_dirty: %s:%u\n",
		mdev->last_md_mark_dirty.func, mdev->last_md_mark_dirty.line);
#endif
	drbd_md_sync(mdev);
	return 1;
}

#ifdef CONFIG_DRBD_FAULT_INJECTION
/* Fault insertion support including random number generator shamelessly
 * stolen from kernel/rcutorture.c */
struct fault_random_state {
	unsigned long state;
	unsigned long count;
};

#define FAULT_RANDOM_MULT 39916801  /* prime */
#define FAULT_RANDOM_ADD	479001701 /* prime */
#define FAULT_RANDOM_REFRESH 10000

/*
 * Crude but fast random-number generator.  Uses a linear congruential
 * generator, with occasional help from get_random_bytes().
 */
static unsigned long
_drbd_fault_random(struct fault_random_state *rsp)
{
	long refresh;

	if (!rsp->count--) {
		get_random_bytes(&refresh, sizeof(refresh));
		rsp->state += refresh;
		rsp->count = FAULT_RANDOM_REFRESH;
	}
	rsp->state = rsp->state * FAULT_RANDOM_MULT + FAULT_RANDOM_ADD;
	return swahw32(rsp->state);
}

static char *
_drbd_fault_str(unsigned int type) {
	static char *_faults[] = {
		[DRBD_FAULT_MD_WR] = "Meta-data write",
		[DRBD_FAULT_MD_RD] = "Meta-data read",
		[DRBD_FAULT_RS_WR] = "Resync write",
		[DRBD_FAULT_RS_RD] = "Resync read",
		[DRBD_FAULT_DT_WR] = "Data write",
		[DRBD_FAULT_DT_RD] = "Data read",
		[DRBD_FAULT_DT_RA] = "Data read ahead",
		[DRBD_FAULT_BM_ALLOC] = "BM allocation",
		[DRBD_FAULT_AL_EE] = "EE allocation",
		[DRBD_FAULT_RECEIVE] = "receive data corruption",
	};

	return (type < DRBD_FAULT_MAX) ? _faults[type] : "**Unknown**";
}

unsigned int
_drbd_insert_fault(struct drbd_conf *mdev, unsigned int type)
{
	static struct fault_random_state rrs = {0, 0};

	unsigned int ret = (
		(fault_devs == 0 ||
			((1 << mdev_to_minor(mdev)) & fault_devs) != 0) &&
		(((_drbd_fault_random(&rrs) % 100) + 1) <= fault_rate));

	if (ret) {
		fault_count++;

		if (__ratelimit(&drbd_ratelimit_state))
			dev_warn(DEV, "***Simulating %s failure\n",
				_drbd_fault_str(type));
	}

	return ret;
}
#endif

const char *drbd_buildtag(void)
{
	/* DRBD built from external sources has here a reference to the
	   git hash of the source code. */

	static char buildtag[38] = "\0uilt-in";

	if (buildtag[0] == 0) {
#ifdef MODULE
		sprintf(buildtag, "srcversion: %-24s", THIS_MODULE->srcversion);
#else
		buildtag[0] = 'b';
#endif
	}

	return buildtag;
}

module_init(drbd_init)
module_exit(drbd_cleanup)

EXPORT_SYMBOL(drbd_conn_str);
EXPORT_SYMBOL(drbd_role_str);
EXPORT_SYMBOL(drbd_disk_str);
EXPORT_SYMBOL(drbd_set_st_err_str);

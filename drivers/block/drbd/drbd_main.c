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

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/jiffies.h>
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
#include <linux/workqueue.h>
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>
#include <linux/vmalloc.h>

#include <linux/drbd_limits.h>
#include "drbd_int.h"
#include "drbd_protocol.h"
#include "drbd_req.h" /* only for _req_mod in tl_release and tl_clear */
#include "drbd_vli.h"
#include "drbd_debugfs.h"

static DEFINE_MUTEX(drbd_main_mutex);
static int drbd_open(struct block_device *bdev, fmode_t mode);
static void drbd_release(struct gendisk *gd, fmode_t mode);
static void md_sync_timer_fn(unsigned long data);
static int w_bitmap_io(struct drbd_work *w, int unused);

MODULE_AUTHOR("Philipp Reisner <phil@linbit.com>, "
	      "Lars Ellenberg <lars@linbit.com>");
MODULE_DESCRIPTION("drbd - Distributed Replicated Block Device v" REL_VERSION);
MODULE_VERSION(REL_VERSION);
MODULE_LICENSE("GPL");
MODULE_PARM_DESC(minor_count, "Approximate number of drbd devices ("
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
int proc_details;       /* Detail level in proc drbd*/

/* Module parameter for setting the user mode helper program
 * to run. Default is /sbin/drbdadm */
char usermode_helper[80] = "/sbin/drbdadm";

module_param_string(usermode_helper, usermode_helper, sizeof(usermode_helper), 0644);

/* in 2.6.x, our device mapping and config info contains our virtual gendisks
 * as member "struct gendisk *vdisk;"
 */
struct idr drbd_devices;
struct list_head drbd_resources;

struct kmem_cache *drbd_request_cache;
struct kmem_cache *drbd_ee_cache;	/* peer requests */
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
	struct bio *bio;

	if (!drbd_md_io_bio_set)
		return bio_alloc(gfp_mask, 1);

	bio = bio_alloc_bioset(gfp_mask, 1, drbd_md_io_bio_set);
	if (!bio)
		return NULL;
	return bio;
}

#ifdef __CHECKER__
/* When checking with sparse, and this is an inline function, sparse will
   give tons of false positives. When this is a real functions sparse works.
 */
int _get_ldev_if_state(struct drbd_device *device, enum drbd_disk_state mins)
{
	int io_allowed;

	atomic_inc(&device->local_cnt);
	io_allowed = (device->state.disk >= mins);
	if (!io_allowed) {
		if (atomic_dec_and_test(&device->local_cnt))
			wake_up(&device->misc_wait);
	}
	return io_allowed;
}

#endif

/**
 * tl_release() - mark as BARRIER_ACKED all requests in the corresponding transfer log epoch
 * @connection:	DRBD connection.
 * @barrier_nr:	Expected identifier of the DRBD write barrier packet.
 * @set_size:	Expected number of requests before that barrier.
 *
 * In case the passed barrier_nr or set_size does not match the oldest
 * epoch of not yet barrier-acked requests, this function will cause a
 * termination of the connection.
 */
void tl_release(struct drbd_connection *connection, unsigned int barrier_nr,
		unsigned int set_size)
{
	struct drbd_request *r;
	struct drbd_request *req = NULL;
	int expect_epoch = 0;
	int expect_size = 0;

	spin_lock_irq(&connection->resource->req_lock);

	/* find oldest not yet barrier-acked write request,
	 * count writes in its epoch. */
	list_for_each_entry(r, &connection->transfer_log, tl_requests) {
		const unsigned s = r->rq_state;
		if (!req) {
			if (!(s & RQ_WRITE))
				continue;
			if (!(s & RQ_NET_MASK))
				continue;
			if (s & RQ_NET_DONE)
				continue;
			req = r;
			expect_epoch = req->epoch;
			expect_size ++;
		} else {
			if (r->epoch != expect_epoch)
				break;
			if (!(s & RQ_WRITE))
				continue;
			/* if (s & RQ_DONE): not expected */
			/* if (!(s & RQ_NET_MASK)): not expected */
			expect_size++;
		}
	}

	/* first some paranoia code */
	if (req == NULL) {
		drbd_err(connection, "BAD! BarrierAck #%u received, but no epoch in tl!?\n",
			 barrier_nr);
		goto bail;
	}
	if (expect_epoch != barrier_nr) {
		drbd_err(connection, "BAD! BarrierAck #%u received, expected #%u!\n",
			 barrier_nr, expect_epoch);
		goto bail;
	}

	if (expect_size != set_size) {
		drbd_err(connection, "BAD! BarrierAck #%u received with n_writes=%u, expected n_writes=%u!\n",
			 barrier_nr, set_size, expect_size);
		goto bail;
	}

	/* Clean up list of requests processed during current epoch. */
	/* this extra list walk restart is paranoia,
	 * to catch requests being barrier-acked "unexpectedly".
	 * It usually should find the same req again, or some READ preceding it. */
	list_for_each_entry(req, &connection->transfer_log, tl_requests)
		if (req->epoch == expect_epoch)
			break;
	list_for_each_entry_safe_from(req, r, &connection->transfer_log, tl_requests) {
		if (req->epoch != expect_epoch)
			break;
		_req_mod(req, BARRIER_ACKED);
	}
	spin_unlock_irq(&connection->resource->req_lock);

	return;

bail:
	spin_unlock_irq(&connection->resource->req_lock);
	conn_request_state(connection, NS(conn, C_PROTOCOL_ERROR), CS_HARD);
}


/**
 * _tl_restart() - Walks the transfer log, and applies an action to all requests
 * @connection:	DRBD connection to operate on.
 * @what:       The action/event to perform with all request objects
 *
 * @what might be one of CONNECTION_LOST_WHILE_PENDING, RESEND, FAIL_FROZEN_DISK_IO,
 * RESTART_FROZEN_DISK_IO.
 */
/* must hold resource->req_lock */
void _tl_restart(struct drbd_connection *connection, enum drbd_req_event what)
{
	struct drbd_request *req, *r;

	list_for_each_entry_safe(req, r, &connection->transfer_log, tl_requests)
		_req_mod(req, what);
}

void tl_restart(struct drbd_connection *connection, enum drbd_req_event what)
{
	spin_lock_irq(&connection->resource->req_lock);
	_tl_restart(connection, what);
	spin_unlock_irq(&connection->resource->req_lock);
}

/**
 * tl_clear() - Clears all requests and &struct drbd_tl_epoch objects out of the TL
 * @device:	DRBD device.
 *
 * This is called after the connection to the peer was lost. The storage covered
 * by the requests on the transfer gets marked as our of sync. Called from the
 * receiver thread and the worker thread.
 */
void tl_clear(struct drbd_connection *connection)
{
	tl_restart(connection, CONNECTION_LOST_WHILE_PENDING);
}

/**
 * tl_abort_disk_io() - Abort disk I/O for all requests for a certain device in the TL
 * @device:	DRBD device.
 */
void tl_abort_disk_io(struct drbd_device *device)
{
	struct drbd_connection *connection = first_peer_device(device)->connection;
	struct drbd_request *req, *r;

	spin_lock_irq(&connection->resource->req_lock);
	list_for_each_entry_safe(req, r, &connection->transfer_log, tl_requests) {
		if (!(req->rq_state & RQ_LOCAL_PENDING))
			continue;
		if (req->device != device)
			continue;
		_req_mod(req, ABORT_DISK_IO);
	}
	spin_unlock_irq(&connection->resource->req_lock);
}

static int drbd_thread_setup(void *arg)
{
	struct drbd_thread *thi = (struct drbd_thread *) arg;
	struct drbd_resource *resource = thi->resource;
	unsigned long flags;
	int retval;

	snprintf(current->comm, sizeof(current->comm), "drbd_%c_%s",
		 thi->name[0],
		 resource->name);

restart:
	retval = thi->function(thi);

	spin_lock_irqsave(&thi->t_lock, flags);

	/* if the receiver has been "EXITING", the last thing it did
	 * was set the conn state to "StandAlone",
	 * if now a re-connect request comes in, conn state goes C_UNCONNECTED,
	 * and receiver thread will be "started".
	 * drbd_thread_start needs to set "RESTARTING" in that case.
	 * t_state check and assignment needs to be within the same spinlock,
	 * so either thread_start sees EXITING, and can remap to RESTARTING,
	 * or thread_start see NONE, and can proceed as normal.
	 */

	if (thi->t_state == RESTARTING) {
		drbd_info(resource, "Restarting %s thread\n", thi->name);
		thi->t_state = RUNNING;
		spin_unlock_irqrestore(&thi->t_lock, flags);
		goto restart;
	}

	thi->task = NULL;
	thi->t_state = NONE;
	smp_mb();
	complete_all(&thi->stop);
	spin_unlock_irqrestore(&thi->t_lock, flags);

	drbd_info(resource, "Terminating %s\n", current->comm);

	/* Release mod reference taken when thread was started */

	if (thi->connection)
		kref_put(&thi->connection->kref, drbd_destroy_connection);
	kref_put(&resource->kref, drbd_destroy_resource);
	module_put(THIS_MODULE);
	return retval;
}

static void drbd_thread_init(struct drbd_resource *resource, struct drbd_thread *thi,
			     int (*func) (struct drbd_thread *), const char *name)
{
	spin_lock_init(&thi->t_lock);
	thi->task    = NULL;
	thi->t_state = NONE;
	thi->function = func;
	thi->resource = resource;
	thi->connection = NULL;
	thi->name = name;
}

int drbd_thread_start(struct drbd_thread *thi)
{
	struct drbd_resource *resource = thi->resource;
	struct task_struct *nt;
	unsigned long flags;

	/* is used from state engine doing drbd_thread_stop_nowait,
	 * while holding the req lock irqsave */
	spin_lock_irqsave(&thi->t_lock, flags);

	switch (thi->t_state) {
	case NONE:
		drbd_info(resource, "Starting %s thread (from %s [%d])\n",
			 thi->name, current->comm, current->pid);

		/* Get ref on module for thread - this is released when thread exits */
		if (!try_module_get(THIS_MODULE)) {
			drbd_err(resource, "Failed to get module reference in drbd_thread_start\n");
			spin_unlock_irqrestore(&thi->t_lock, flags);
			return false;
		}

		kref_get(&resource->kref);
		if (thi->connection)
			kref_get(&thi->connection->kref);

		init_completion(&thi->stop);
		thi->reset_cpu_mask = 1;
		thi->t_state = RUNNING;
		spin_unlock_irqrestore(&thi->t_lock, flags);
		flush_signals(current); /* otherw. may get -ERESTARTNOINTR */

		nt = kthread_create(drbd_thread_setup, (void *) thi,
				    "drbd_%c_%s", thi->name[0], thi->resource->name);

		if (IS_ERR(nt)) {
			drbd_err(resource, "Couldn't start thread\n");

			if (thi->connection)
				kref_put(&thi->connection->kref, drbd_destroy_connection);
			kref_put(&resource->kref, drbd_destroy_resource);
			module_put(THIS_MODULE);
			return false;
		}
		spin_lock_irqsave(&thi->t_lock, flags);
		thi->task = nt;
		thi->t_state = RUNNING;
		spin_unlock_irqrestore(&thi->t_lock, flags);
		wake_up_process(nt);
		break;
	case EXITING:
		thi->t_state = RESTARTING;
		drbd_info(resource, "Restarting %s thread (from %s [%d])\n",
				thi->name, current->comm, current->pid);
		/* fall through */
	case RUNNING:
	case RESTARTING:
	default:
		spin_unlock_irqrestore(&thi->t_lock, flags);
		break;
	}

	return true;
}


void _drbd_thread_stop(struct drbd_thread *thi, int restart, int wait)
{
	unsigned long flags;

	enum drbd_thread_state ns = restart ? RESTARTING : EXITING;

	/* may be called from state engine, holding the req lock irqsave */
	spin_lock_irqsave(&thi->t_lock, flags);

	if (thi->t_state == NONE) {
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

int conn_lowest_minor(struct drbd_connection *connection)
{
	struct drbd_peer_device *peer_device;
	int vnr = 0, minor = -1;

	rcu_read_lock();
	peer_device = idr_get_next(&connection->peer_devices, &vnr);
	if (peer_device)
		minor = device_to_minor(peer_device->device);
	rcu_read_unlock();

	return minor;
}

#ifdef CONFIG_SMP
/**
 * drbd_calc_cpu_mask() - Generate CPU masks, spread over all CPUs
 *
 * Forces all threads of a resource onto the same CPU. This is beneficial for
 * DRBD's performance. May be overwritten by user's configuration.
 */
static void drbd_calc_cpu_mask(cpumask_var_t *cpu_mask)
{
	unsigned int *resources_per_cpu, min_index = ~0;

	resources_per_cpu = kzalloc(nr_cpu_ids * sizeof(*resources_per_cpu), GFP_KERNEL);
	if (resources_per_cpu) {
		struct drbd_resource *resource;
		unsigned int cpu, min = ~0;

		rcu_read_lock();
		for_each_resource_rcu(resource, &drbd_resources) {
			for_each_cpu(cpu, resource->cpu_mask)
				resources_per_cpu[cpu]++;
		}
		rcu_read_unlock();
		for_each_online_cpu(cpu) {
			if (resources_per_cpu[cpu] < min) {
				min = resources_per_cpu[cpu];
				min_index = cpu;
			}
		}
		kfree(resources_per_cpu);
	}
	if (min_index == ~0) {
		cpumask_setall(*cpu_mask);
		return;
	}
	cpumask_set_cpu(min_index, *cpu_mask);
}

/**
 * drbd_thread_current_set_cpu() - modifies the cpu mask of the _current_ thread
 * @device:	DRBD device.
 * @thi:	drbd_thread object
 *
 * call in the "main loop" of _all_ threads, no need for any mutex, current won't die
 * prematurely.
 */
void drbd_thread_current_set_cpu(struct drbd_thread *thi)
{
	struct drbd_resource *resource = thi->resource;
	struct task_struct *p = current;

	if (!thi->reset_cpu_mask)
		return;
	thi->reset_cpu_mask = 0;
	set_cpus_allowed_ptr(p, resource->cpu_mask);
}
#else
#define drbd_calc_cpu_mask(A) ({})
#endif

/**
 * drbd_header_size  -  size of a packet header
 *
 * The header size is a multiple of 8, so any payload following the header is
 * word aligned on 64-bit architectures.  (The bitmap send and receive code
 * relies on this.)
 */
unsigned int drbd_header_size(struct drbd_connection *connection)
{
	if (connection->agreed_pro_version >= 100) {
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(struct p_header100), 8));
		return sizeof(struct p_header100);
	} else {
		BUILD_BUG_ON(sizeof(struct p_header80) !=
			     sizeof(struct p_header95));
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(struct p_header80), 8));
		return sizeof(struct p_header80);
	}
}

static unsigned int prepare_header80(struct p_header80 *h, enum drbd_packet cmd, int size)
{
	h->magic   = cpu_to_be32(DRBD_MAGIC);
	h->command = cpu_to_be16(cmd);
	h->length  = cpu_to_be16(size);
	return sizeof(struct p_header80);
}

static unsigned int prepare_header95(struct p_header95 *h, enum drbd_packet cmd, int size)
{
	h->magic   = cpu_to_be16(DRBD_MAGIC_BIG);
	h->command = cpu_to_be16(cmd);
	h->length = cpu_to_be32(size);
	return sizeof(struct p_header95);
}

static unsigned int prepare_header100(struct p_header100 *h, enum drbd_packet cmd,
				      int size, int vnr)
{
	h->magic = cpu_to_be32(DRBD_MAGIC_100);
	h->volume = cpu_to_be16(vnr);
	h->command = cpu_to_be16(cmd);
	h->length = cpu_to_be32(size);
	h->pad = 0;
	return sizeof(struct p_header100);
}

static unsigned int prepare_header(struct drbd_connection *connection, int vnr,
				   void *buffer, enum drbd_packet cmd, int size)
{
	if (connection->agreed_pro_version >= 100)
		return prepare_header100(buffer, cmd, size, vnr);
	else if (connection->agreed_pro_version >= 95 &&
		 size > DRBD_MAX_SIZE_H80_PACKET)
		return prepare_header95(buffer, cmd, size);
	else
		return prepare_header80(buffer, cmd, size);
}

static void *__conn_prepare_command(struct drbd_connection *connection,
				    struct drbd_socket *sock)
{
	if (!sock->socket)
		return NULL;
	return sock->sbuf + drbd_header_size(connection);
}

void *conn_prepare_command(struct drbd_connection *connection, struct drbd_socket *sock)
{
	void *p;

	mutex_lock(&sock->mutex);
	p = __conn_prepare_command(connection, sock);
	if (!p)
		mutex_unlock(&sock->mutex);

	return p;
}

void *drbd_prepare_command(struct drbd_peer_device *peer_device, struct drbd_socket *sock)
{
	return conn_prepare_command(peer_device->connection, sock);
}

static int __send_command(struct drbd_connection *connection, int vnr,
			  struct drbd_socket *sock, enum drbd_packet cmd,
			  unsigned int header_size, void *data,
			  unsigned int size)
{
	int msg_flags;
	int err;

	/*
	 * Called with @data == NULL and the size of the data blocks in @size
	 * for commands that send data blocks.  For those commands, omit the
	 * MSG_MORE flag: this will increase the likelihood that data blocks
	 * which are page aligned on the sender will end up page aligned on the
	 * receiver.
	 */
	msg_flags = data ? MSG_MORE : 0;

	header_size += prepare_header(connection, vnr, sock->sbuf, cmd,
				      header_size + size);
	err = drbd_send_all(connection, sock->socket, sock->sbuf, header_size,
			    msg_flags);
	if (data && !err)
		err = drbd_send_all(connection, sock->socket, data, size, 0);
	/* DRBD protocol "pings" are latency critical.
	 * This is supposed to trigger tcp_push_pending_frames() */
	if (!err && (cmd == P_PING || cmd == P_PING_ACK))
		drbd_tcp_nodelay(sock->socket);

	return err;
}

static int __conn_send_command(struct drbd_connection *connection, struct drbd_socket *sock,
			       enum drbd_packet cmd, unsigned int header_size,
			       void *data, unsigned int size)
{
	return __send_command(connection, 0, sock, cmd, header_size, data, size);
}

int conn_send_command(struct drbd_connection *connection, struct drbd_socket *sock,
		      enum drbd_packet cmd, unsigned int header_size,
		      void *data, unsigned int size)
{
	int err;

	err = __conn_send_command(connection, sock, cmd, header_size, data, size);
	mutex_unlock(&sock->mutex);
	return err;
}

int drbd_send_command(struct drbd_peer_device *peer_device, struct drbd_socket *sock,
		      enum drbd_packet cmd, unsigned int header_size,
		      void *data, unsigned int size)
{
	int err;

	err = __send_command(peer_device->connection, peer_device->device->vnr,
			     sock, cmd, header_size, data, size);
	mutex_unlock(&sock->mutex);
	return err;
}

int drbd_send_ping(struct drbd_connection *connection)
{
	struct drbd_socket *sock;

	sock = &connection->meta;
	if (!conn_prepare_command(connection, sock))
		return -EIO;
	return conn_send_command(connection, sock, P_PING, 0, NULL, 0);
}

int drbd_send_ping_ack(struct drbd_connection *connection)
{
	struct drbd_socket *sock;

	sock = &connection->meta;
	if (!conn_prepare_command(connection, sock))
		return -EIO;
	return conn_send_command(connection, sock, P_PING_ACK, 0, NULL, 0);
}

int drbd_send_sync_param(struct drbd_peer_device *peer_device)
{
	struct drbd_socket *sock;
	struct p_rs_param_95 *p;
	int size;
	const int apv = peer_device->connection->agreed_pro_version;
	enum drbd_packet cmd;
	struct net_conf *nc;
	struct disk_conf *dc;

	sock = &peer_device->connection->data;
	p = drbd_prepare_command(peer_device, sock);
	if (!p)
		return -EIO;

	rcu_read_lock();
	nc = rcu_dereference(peer_device->connection->net_conf);

	size = apv <= 87 ? sizeof(struct p_rs_param)
		: apv == 88 ? sizeof(struct p_rs_param)
			+ strlen(nc->verify_alg) + 1
		: apv <= 94 ? sizeof(struct p_rs_param_89)
		: /* apv >= 95 */ sizeof(struct p_rs_param_95);

	cmd = apv >= 89 ? P_SYNC_PARAM89 : P_SYNC_PARAM;

	/* initialize verify_alg and csums_alg */
	memset(p->verify_alg, 0, 2 * SHARED_SECRET_MAX);

	if (get_ldev(peer_device->device)) {
		dc = rcu_dereference(peer_device->device->ldev->disk_conf);
		p->resync_rate = cpu_to_be32(dc->resync_rate);
		p->c_plan_ahead = cpu_to_be32(dc->c_plan_ahead);
		p->c_delay_target = cpu_to_be32(dc->c_delay_target);
		p->c_fill_target = cpu_to_be32(dc->c_fill_target);
		p->c_max_rate = cpu_to_be32(dc->c_max_rate);
		put_ldev(peer_device->device);
	} else {
		p->resync_rate = cpu_to_be32(DRBD_RESYNC_RATE_DEF);
		p->c_plan_ahead = cpu_to_be32(DRBD_C_PLAN_AHEAD_DEF);
		p->c_delay_target = cpu_to_be32(DRBD_C_DELAY_TARGET_DEF);
		p->c_fill_target = cpu_to_be32(DRBD_C_FILL_TARGET_DEF);
		p->c_max_rate = cpu_to_be32(DRBD_C_MAX_RATE_DEF);
	}

	if (apv >= 88)
		strcpy(p->verify_alg, nc->verify_alg);
	if (apv >= 89)
		strcpy(p->csums_alg, nc->csums_alg);
	rcu_read_unlock();

	return drbd_send_command(peer_device, sock, cmd, size, NULL, 0);
}

int __drbd_send_protocol(struct drbd_connection *connection, enum drbd_packet cmd)
{
	struct drbd_socket *sock;
	struct p_protocol *p;
	struct net_conf *nc;
	int size, cf;

	sock = &connection->data;
	p = __conn_prepare_command(connection, sock);
	if (!p)
		return -EIO;

	rcu_read_lock();
	nc = rcu_dereference(connection->net_conf);

	if (nc->tentative && connection->agreed_pro_version < 92) {
		rcu_read_unlock();
		mutex_unlock(&sock->mutex);
		drbd_err(connection, "--dry-run is not supported by peer");
		return -EOPNOTSUPP;
	}

	size = sizeof(*p);
	if (connection->agreed_pro_version >= 87)
		size += strlen(nc->integrity_alg) + 1;

	p->protocol      = cpu_to_be32(nc->wire_protocol);
	p->after_sb_0p   = cpu_to_be32(nc->after_sb_0p);
	p->after_sb_1p   = cpu_to_be32(nc->after_sb_1p);
	p->after_sb_2p   = cpu_to_be32(nc->after_sb_2p);
	p->two_primaries = cpu_to_be32(nc->two_primaries);
	cf = 0;
	if (nc->discard_my_data)
		cf |= CF_DISCARD_MY_DATA;
	if (nc->tentative)
		cf |= CF_DRY_RUN;
	p->conn_flags    = cpu_to_be32(cf);

	if (connection->agreed_pro_version >= 87)
		strcpy(p->integrity_alg, nc->integrity_alg);
	rcu_read_unlock();

	return __conn_send_command(connection, sock, cmd, size, NULL, 0);
}

int drbd_send_protocol(struct drbd_connection *connection)
{
	int err;

	mutex_lock(&connection->data.mutex);
	err = __drbd_send_protocol(connection, P_PROTOCOL);
	mutex_unlock(&connection->data.mutex);

	return err;
}

static int _drbd_send_uuids(struct drbd_peer_device *peer_device, u64 uuid_flags)
{
	struct drbd_device *device = peer_device->device;
	struct drbd_socket *sock;
	struct p_uuids *p;
	int i;

	if (!get_ldev_if_state(device, D_NEGOTIATING))
		return 0;

	sock = &peer_device->connection->data;
	p = drbd_prepare_command(peer_device, sock);
	if (!p) {
		put_ldev(device);
		return -EIO;
	}
	spin_lock_irq(&device->ldev->md.uuid_lock);
	for (i = UI_CURRENT; i < UI_SIZE; i++)
		p->uuid[i] = cpu_to_be64(device->ldev->md.uuid[i]);
	spin_unlock_irq(&device->ldev->md.uuid_lock);

	device->comm_bm_set = drbd_bm_total_weight(device);
	p->uuid[UI_SIZE] = cpu_to_be64(device->comm_bm_set);
	rcu_read_lock();
	uuid_flags |= rcu_dereference(peer_device->connection->net_conf)->discard_my_data ? 1 : 0;
	rcu_read_unlock();
	uuid_flags |= test_bit(CRASHED_PRIMARY, &device->flags) ? 2 : 0;
	uuid_flags |= device->new_state_tmp.disk == D_INCONSISTENT ? 4 : 0;
	p->uuid[UI_FLAGS] = cpu_to_be64(uuid_flags);

	put_ldev(device);
	return drbd_send_command(peer_device, sock, P_UUIDS, sizeof(*p), NULL, 0);
}

int drbd_send_uuids(struct drbd_peer_device *peer_device)
{
	return _drbd_send_uuids(peer_device, 0);
}

int drbd_send_uuids_skip_initial_sync(struct drbd_peer_device *peer_device)
{
	return _drbd_send_uuids(peer_device, 8);
}

void drbd_print_uuids(struct drbd_device *device, const char *text)
{
	if (get_ldev_if_state(device, D_NEGOTIATING)) {
		u64 *uuid = device->ldev->md.uuid;
		drbd_info(device, "%s %016llX:%016llX:%016llX:%016llX\n",
		     text,
		     (unsigned long long)uuid[UI_CURRENT],
		     (unsigned long long)uuid[UI_BITMAP],
		     (unsigned long long)uuid[UI_HISTORY_START],
		     (unsigned long long)uuid[UI_HISTORY_END]);
		put_ldev(device);
	} else {
		drbd_info(device, "%s effective data uuid: %016llX\n",
				text,
				(unsigned long long)device->ed_uuid);
	}
}

void drbd_gen_and_send_sync_uuid(struct drbd_peer_device *peer_device)
{
	struct drbd_device *device = peer_device->device;
	struct drbd_socket *sock;
	struct p_rs_uuid *p;
	u64 uuid;

	D_ASSERT(device, device->state.disk == D_UP_TO_DATE);

	uuid = device->ldev->md.uuid[UI_BITMAP];
	if (uuid && uuid != UUID_JUST_CREATED)
		uuid = uuid + UUID_NEW_BM_OFFSET;
	else
		get_random_bytes(&uuid, sizeof(u64));
	drbd_uuid_set(device, UI_BITMAP, uuid);
	drbd_print_uuids(device, "updated sync UUID");
	drbd_md_sync(device);

	sock = &peer_device->connection->data;
	p = drbd_prepare_command(peer_device, sock);
	if (p) {
		p->uuid = cpu_to_be64(uuid);
		drbd_send_command(peer_device, sock, P_SYNC_UUID, sizeof(*p), NULL, 0);
	}
}

int drbd_send_sizes(struct drbd_peer_device *peer_device, int trigger_reply, enum dds_flags flags)
{
	struct drbd_device *device = peer_device->device;
	struct drbd_socket *sock;
	struct p_sizes *p;
	sector_t d_size, u_size;
	int q_order_type;
	unsigned int max_bio_size;

	if (get_ldev_if_state(device, D_NEGOTIATING)) {
		D_ASSERT(device, device->ldev->backing_bdev);
		d_size = drbd_get_max_capacity(device->ldev);
		rcu_read_lock();
		u_size = rcu_dereference(device->ldev->disk_conf)->disk_size;
		rcu_read_unlock();
		q_order_type = drbd_queue_order_type(device);
		max_bio_size = queue_max_hw_sectors(device->ldev->backing_bdev->bd_disk->queue) << 9;
		max_bio_size = min(max_bio_size, DRBD_MAX_BIO_SIZE);
		put_ldev(device);
	} else {
		d_size = 0;
		u_size = 0;
		q_order_type = QUEUE_ORDERED_NONE;
		max_bio_size = DRBD_MAX_BIO_SIZE; /* ... multiple BIOs per peer_request */
	}

	sock = &peer_device->connection->data;
	p = drbd_prepare_command(peer_device, sock);
	if (!p)
		return -EIO;

	if (peer_device->connection->agreed_pro_version <= 94)
		max_bio_size = min(max_bio_size, DRBD_MAX_SIZE_H80_PACKET);
	else if (peer_device->connection->agreed_pro_version < 100)
		max_bio_size = min(max_bio_size, DRBD_MAX_BIO_SIZE_P95);

	p->d_size = cpu_to_be64(d_size);
	p->u_size = cpu_to_be64(u_size);
	p->c_size = cpu_to_be64(trigger_reply ? 0 : drbd_get_capacity(device->this_bdev));
	p->max_bio_size = cpu_to_be32(max_bio_size);
	p->queue_order_type = cpu_to_be16(q_order_type);
	p->dds_flags = cpu_to_be16(flags);
	return drbd_send_command(peer_device, sock, P_SIZES, sizeof(*p), NULL, 0);
}

/**
 * drbd_send_current_state() - Sends the drbd state to the peer
 * @peer_device:	DRBD peer device.
 */
int drbd_send_current_state(struct drbd_peer_device *peer_device)
{
	struct drbd_socket *sock;
	struct p_state *p;

	sock = &peer_device->connection->data;
	p = drbd_prepare_command(peer_device, sock);
	if (!p)
		return -EIO;
	p->state = cpu_to_be32(peer_device->device->state.i); /* Within the send mutex */
	return drbd_send_command(peer_device, sock, P_STATE, sizeof(*p), NULL, 0);
}

/**
 * drbd_send_state() - After a state change, sends the new state to the peer
 * @peer_device:      DRBD peer device.
 * @state:     the state to send, not necessarily the current state.
 *
 * Each state change queues an "after_state_ch" work, which will eventually
 * send the resulting new state to the peer. If more state changes happen
 * between queuing and processing of the after_state_ch work, we still
 * want to send each intermediary state in the order it occurred.
 */
int drbd_send_state(struct drbd_peer_device *peer_device, union drbd_state state)
{
	struct drbd_socket *sock;
	struct p_state *p;

	sock = &peer_device->connection->data;
	p = drbd_prepare_command(peer_device, sock);
	if (!p)
		return -EIO;
	p->state = cpu_to_be32(state.i); /* Within the send mutex */
	return drbd_send_command(peer_device, sock, P_STATE, sizeof(*p), NULL, 0);
}

int drbd_send_state_req(struct drbd_peer_device *peer_device, union drbd_state mask, union drbd_state val)
{
	struct drbd_socket *sock;
	struct p_req_state *p;

	sock = &peer_device->connection->data;
	p = drbd_prepare_command(peer_device, sock);
	if (!p)
		return -EIO;
	p->mask = cpu_to_be32(mask.i);
	p->val = cpu_to_be32(val.i);
	return drbd_send_command(peer_device, sock, P_STATE_CHG_REQ, sizeof(*p), NULL, 0);
}

int conn_send_state_req(struct drbd_connection *connection, union drbd_state mask, union drbd_state val)
{
	enum drbd_packet cmd;
	struct drbd_socket *sock;
	struct p_req_state *p;

	cmd = connection->agreed_pro_version < 100 ? P_STATE_CHG_REQ : P_CONN_ST_CHG_REQ;
	sock = &connection->data;
	p = conn_prepare_command(connection, sock);
	if (!p)
		return -EIO;
	p->mask = cpu_to_be32(mask.i);
	p->val = cpu_to_be32(val.i);
	return conn_send_command(connection, sock, cmd, sizeof(*p), NULL, 0);
}

void drbd_send_sr_reply(struct drbd_peer_device *peer_device, enum drbd_state_rv retcode)
{
	struct drbd_socket *sock;
	struct p_req_state_reply *p;

	sock = &peer_device->connection->meta;
	p = drbd_prepare_command(peer_device, sock);
	if (p) {
		p->retcode = cpu_to_be32(retcode);
		drbd_send_command(peer_device, sock, P_STATE_CHG_REPLY, sizeof(*p), NULL, 0);
	}
}

void conn_send_sr_reply(struct drbd_connection *connection, enum drbd_state_rv retcode)
{
	struct drbd_socket *sock;
	struct p_req_state_reply *p;
	enum drbd_packet cmd = connection->agreed_pro_version < 100 ? P_STATE_CHG_REPLY : P_CONN_ST_CHG_REPLY;

	sock = &connection->meta;
	p = conn_prepare_command(connection, sock);
	if (p) {
		p->retcode = cpu_to_be32(retcode);
		conn_send_command(connection, sock, cmd, sizeof(*p), NULL, 0);
	}
}

static void dcbp_set_code(struct p_compressed_bm *p, enum drbd_bitmap_code code)
{
	BUG_ON(code & ~0xf);
	p->encoding = (p->encoding & ~0xf) | code;
}

static void dcbp_set_start(struct p_compressed_bm *p, int set)
{
	p->encoding = (p->encoding & ~0x80) | (set ? 0x80 : 0);
}

static void dcbp_set_pad_bits(struct p_compressed_bm *p, int n)
{
	BUG_ON(n & ~0x7);
	p->encoding = (p->encoding & (~0x7 << 4)) | (n << 4);
}

static int fill_bitmap_rle_bits(struct drbd_device *device,
			 struct p_compressed_bm *p,
			 unsigned int size,
			 struct bm_xfer_ctx *c)
{
	struct bitstream bs;
	unsigned long plain_bits;
	unsigned long tmp;
	unsigned long rl;
	unsigned len;
	unsigned toggle;
	int bits, use_rle;

	/* may we use this feature? */
	rcu_read_lock();
	use_rle = rcu_dereference(first_peer_device(device)->connection->net_conf)->use_rle;
	rcu_read_unlock();
	if (!use_rle || first_peer_device(device)->connection->agreed_pro_version < 90)
		return 0;

	if (c->bit_offset >= c->bm_bits)
		return 0; /* nothing to do. */

	/* use at most thus many bytes */
	bitstream_init(&bs, p->code, size, 0);
	memset(p->code, 0, size);
	/* plain bits covered in this code string */
	plain_bits = 0;

	/* p->encoding & 0x80 stores whether the first run length is set.
	 * bit offset is implicit.
	 * start with toggle == 2 to be able to tell the first iteration */
	toggle = 2;

	/* see how much plain bits we can stuff into one packet
	 * using RLE and VLI. */
	do {
		tmp = (toggle == 0) ? _drbd_bm_find_next_zero(device, c->bit_offset)
				    : _drbd_bm_find_next(device, c->bit_offset);
		if (tmp == -1UL)
			tmp = c->bm_bits;
		rl = tmp - c->bit_offset;

		if (toggle == 2) { /* first iteration */
			if (rl == 0) {
				/* the first checked bit was set,
				 * store start value, */
				dcbp_set_start(p, 1);
				/* but skip encoding of zero run length */
				toggle = !toggle;
				continue;
			}
			dcbp_set_start(p, 0);
		}

		/* paranoia: catch zero runlength.
		 * can only happen if bitmap is modified while we scan it. */
		if (rl == 0) {
			drbd_err(device, "unexpected zero runlength while encoding bitmap "
			    "t:%u bo:%lu\n", toggle, c->bit_offset);
			return -1;
		}

		bits = vli_encode_bits(&bs, rl);
		if (bits == -ENOBUFS) /* buffer full */
			break;
		if (bits <= 0) {
			drbd_err(device, "error while encoding bitmap: %d\n", bits);
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
	dcbp_set_pad_bits(p, (8 - bs.cur.bit) & 0x7);

	return len;
}

/**
 * send_bitmap_rle_or_plain
 *
 * Return 0 when done, 1 when another iteration is needed, and a negative error
 * code upon failure.
 */
static int
send_bitmap_rle_or_plain(struct drbd_device *device, struct bm_xfer_ctx *c)
{
	struct drbd_socket *sock = &first_peer_device(device)->connection->data;
	unsigned int header_size = drbd_header_size(first_peer_device(device)->connection);
	struct p_compressed_bm *p = sock->sbuf + header_size;
	int len, err;

	len = fill_bitmap_rle_bits(device, p,
			DRBD_SOCKET_BUFFER_SIZE - header_size - sizeof(*p), c);
	if (len < 0)
		return -EIO;

	if (len) {
		dcbp_set_code(p, RLE_VLI_Bits);
		err = __send_command(first_peer_device(device)->connection, device->vnr, sock,
				     P_COMPRESSED_BITMAP, sizeof(*p) + len,
				     NULL, 0);
		c->packets[0]++;
		c->bytes[0] += header_size + sizeof(*p) + len;

		if (c->bit_offset >= c->bm_bits)
			len = 0; /* DONE */
	} else {
		/* was not compressible.
		 * send a buffer full of plain text bits instead. */
		unsigned int data_size;
		unsigned long num_words;
		unsigned long *p = sock->sbuf + header_size;

		data_size = DRBD_SOCKET_BUFFER_SIZE - header_size;
		num_words = min_t(size_t, data_size / sizeof(*p),
				  c->bm_words - c->word_offset);
		len = num_words * sizeof(*p);
		if (len)
			drbd_bm_get_lel(device, c->word_offset, num_words, p);
		err = __send_command(first_peer_device(device)->connection, device->vnr, sock, P_BITMAP, len, NULL, 0);
		c->word_offset += num_words;
		c->bit_offset = c->word_offset * BITS_PER_LONG;

		c->packets[1]++;
		c->bytes[1] += header_size + len;

		if (c->bit_offset > c->bm_bits)
			c->bit_offset = c->bm_bits;
	}
	if (!err) {
		if (len == 0) {
			INFO_bm_xfer_stats(device, "send", c);
			return 0;
		} else
			return 1;
	}
	return -EIO;
}

/* See the comment at receive_bitmap() */
static int _drbd_send_bitmap(struct drbd_device *device)
{
	struct bm_xfer_ctx c;
	int err;

	if (!expect(device->bitmap))
		return false;

	if (get_ldev(device)) {
		if (drbd_md_test_flag(device->ldev, MDF_FULL_SYNC)) {
			drbd_info(device, "Writing the whole bitmap, MDF_FullSync was set.\n");
			drbd_bm_set_all(device);
			if (drbd_bm_write(device)) {
				/* write_bm did fail! Leave full sync flag set in Meta P_DATA
				 * but otherwise process as per normal - need to tell other
				 * side that a full resync is required! */
				drbd_err(device, "Failed to write bitmap to disk!\n");
			} else {
				drbd_md_clear_flag(device, MDF_FULL_SYNC);
				drbd_md_sync(device);
			}
		}
		put_ldev(device);
	}

	c = (struct bm_xfer_ctx) {
		.bm_bits = drbd_bm_bits(device),
		.bm_words = drbd_bm_words(device),
	};

	do {
		err = send_bitmap_rle_or_plain(device, &c);
	} while (err > 0);

	return err == 0;
}

int drbd_send_bitmap(struct drbd_device *device)
{
	struct drbd_socket *sock = &first_peer_device(device)->connection->data;
	int err = -1;

	mutex_lock(&sock->mutex);
	if (sock->socket)
		err = !_drbd_send_bitmap(device);
	mutex_unlock(&sock->mutex);
	return err;
}

void drbd_send_b_ack(struct drbd_connection *connection, u32 barrier_nr, u32 set_size)
{
	struct drbd_socket *sock;
	struct p_barrier_ack *p;

	if (connection->cstate < C_WF_REPORT_PARAMS)
		return;

	sock = &connection->meta;
	p = conn_prepare_command(connection, sock);
	if (!p)
		return;
	p->barrier = barrier_nr;
	p->set_size = cpu_to_be32(set_size);
	conn_send_command(connection, sock, P_BARRIER_ACK, sizeof(*p), NULL, 0);
}

/**
 * _drbd_send_ack() - Sends an ack packet
 * @device:	DRBD device.
 * @cmd:	Packet command code.
 * @sector:	sector, needs to be in big endian byte order
 * @blksize:	size in byte, needs to be in big endian byte order
 * @block_id:	Id, big endian byte order
 */
static int _drbd_send_ack(struct drbd_peer_device *peer_device, enum drbd_packet cmd,
			  u64 sector, u32 blksize, u64 block_id)
{
	struct drbd_socket *sock;
	struct p_block_ack *p;

	if (peer_device->device->state.conn < C_CONNECTED)
		return -EIO;

	sock = &peer_device->connection->meta;
	p = drbd_prepare_command(peer_device, sock);
	if (!p)
		return -EIO;
	p->sector = sector;
	p->block_id = block_id;
	p->blksize = blksize;
	p->seq_num = cpu_to_be32(atomic_inc_return(&peer_device->device->packet_seq));
	return drbd_send_command(peer_device, sock, cmd, sizeof(*p), NULL, 0);
}

/* dp->sector and dp->block_id already/still in network byte order,
 * data_size is payload size according to dp->head,
 * and may need to be corrected for digest size. */
void drbd_send_ack_dp(struct drbd_peer_device *peer_device, enum drbd_packet cmd,
		      struct p_data *dp, int data_size)
{
	if (peer_device->connection->peer_integrity_tfm)
		data_size -= crypto_hash_digestsize(peer_device->connection->peer_integrity_tfm);
	_drbd_send_ack(peer_device, cmd, dp->sector, cpu_to_be32(data_size),
		       dp->block_id);
}

void drbd_send_ack_rp(struct drbd_peer_device *peer_device, enum drbd_packet cmd,
		      struct p_block_req *rp)
{
	_drbd_send_ack(peer_device, cmd, rp->sector, rp->blksize, rp->block_id);
}

/**
 * drbd_send_ack() - Sends an ack packet
 * @device:	DRBD device
 * @cmd:	packet command code
 * @peer_req:	peer request
 */
int drbd_send_ack(struct drbd_peer_device *peer_device, enum drbd_packet cmd,
		  struct drbd_peer_request *peer_req)
{
	return _drbd_send_ack(peer_device, cmd,
			      cpu_to_be64(peer_req->i.sector),
			      cpu_to_be32(peer_req->i.size),
			      peer_req->block_id);
}

/* This function misuses the block_id field to signal if the blocks
 * are is sync or not. */
int drbd_send_ack_ex(struct drbd_peer_device *peer_device, enum drbd_packet cmd,
		     sector_t sector, int blksize, u64 block_id)
{
	return _drbd_send_ack(peer_device, cmd,
			      cpu_to_be64(sector),
			      cpu_to_be32(blksize),
			      cpu_to_be64(block_id));
}

int drbd_send_drequest(struct drbd_peer_device *peer_device, int cmd,
		       sector_t sector, int size, u64 block_id)
{
	struct drbd_socket *sock;
	struct p_block_req *p;

	sock = &peer_device->connection->data;
	p = drbd_prepare_command(peer_device, sock);
	if (!p)
		return -EIO;
	p->sector = cpu_to_be64(sector);
	p->block_id = block_id;
	p->blksize = cpu_to_be32(size);
	return drbd_send_command(peer_device, sock, cmd, sizeof(*p), NULL, 0);
}

int drbd_send_drequest_csum(struct drbd_peer_device *peer_device, sector_t sector, int size,
			    void *digest, int digest_size, enum drbd_packet cmd)
{
	struct drbd_socket *sock;
	struct p_block_req *p;

	/* FIXME: Put the digest into the preallocated socket buffer.  */

	sock = &peer_device->connection->data;
	p = drbd_prepare_command(peer_device, sock);
	if (!p)
		return -EIO;
	p->sector = cpu_to_be64(sector);
	p->block_id = ID_SYNCER /* unused */;
	p->blksize = cpu_to_be32(size);
	return drbd_send_command(peer_device, sock, cmd, sizeof(*p), digest, digest_size);
}

int drbd_send_ov_request(struct drbd_peer_device *peer_device, sector_t sector, int size)
{
	struct drbd_socket *sock;
	struct p_block_req *p;

	sock = &peer_device->connection->data;
	p = drbd_prepare_command(peer_device, sock);
	if (!p)
		return -EIO;
	p->sector = cpu_to_be64(sector);
	p->block_id = ID_SYNCER /* unused */;
	p->blksize = cpu_to_be32(size);
	return drbd_send_command(peer_device, sock, P_OV_REQUEST, sizeof(*p), NULL, 0);
}

/* called on sndtimeo
 * returns false if we should retry,
 * true if we think connection is dead
 */
static int we_should_drop_the_connection(struct drbd_connection *connection, struct socket *sock)
{
	int drop_it;
	/* long elapsed = (long)(jiffies - device->last_received); */

	drop_it =   connection->meta.socket == sock
		|| !connection->asender.task
		|| get_t_state(&connection->asender) != RUNNING
		|| connection->cstate < C_WF_REPORT_PARAMS;

	if (drop_it)
		return true;

	drop_it = !--connection->ko_count;
	if (!drop_it) {
		drbd_err(connection, "[%s/%d] sock_sendmsg time expired, ko = %u\n",
			 current->comm, current->pid, connection->ko_count);
		request_ping(connection);
	}

	return drop_it; /* && (device->state == R_PRIMARY) */;
}

static void drbd_update_congested(struct drbd_connection *connection)
{
	struct sock *sk = connection->data.socket->sk;
	if (sk->sk_wmem_queued > sk->sk_sndbuf * 4 / 5)
		set_bit(NET_CONGESTED, &connection->flags);
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
static int _drbd_no_send_page(struct drbd_peer_device *peer_device, struct page *page,
			      int offset, size_t size, unsigned msg_flags)
{
	struct socket *socket;
	void *addr;
	int err;

	socket = peer_device->connection->data.socket;
	addr = kmap(page) + offset;
	err = drbd_send_all(peer_device->connection, socket, addr, size, msg_flags);
	kunmap(page);
	if (!err)
		peer_device->device->send_cnt += size >> 9;
	return err;
}

static int _drbd_send_page(struct drbd_peer_device *peer_device, struct page *page,
		    int offset, size_t size, unsigned msg_flags)
{
	struct socket *socket = peer_device->connection->data.socket;
	mm_segment_t oldfs = get_fs();
	int len = size;
	int err = -EIO;

	/* e.g. XFS meta- & log-data is in slab pages, which have a
	 * page_count of 0 and/or have PageSlab() set.
	 * we cannot use send_page for those, as that does get_page();
	 * put_page(); and would cause either a VM_BUG directly, or
	 * __page_cache_release a page that would actually still be referenced
	 * by someone, leading to some obscure delayed Oops somewhere else. */
	if (disable_sendpage || (page_count(page) < 1) || PageSlab(page))
		return _drbd_no_send_page(peer_device, page, offset, size, msg_flags);

	msg_flags |= MSG_NOSIGNAL;
	drbd_update_congested(peer_device->connection);
	set_fs(KERNEL_DS);
	do {
		int sent;

		sent = socket->ops->sendpage(socket, page, offset, len, msg_flags);
		if (sent <= 0) {
			if (sent == -EAGAIN) {
				if (we_should_drop_the_connection(peer_device->connection, socket))
					break;
				continue;
			}
			drbd_warn(peer_device->device, "%s: size=%d len=%d sent=%d\n",
			     __func__, (int)size, len, sent);
			if (sent < 0)
				err = sent;
			break;
		}
		len    -= sent;
		offset += sent;
	} while (len > 0 /* THINK && device->cstate >= C_CONNECTED*/);
	set_fs(oldfs);
	clear_bit(NET_CONGESTED, &peer_device->connection->flags);

	if (len == 0) {
		err = 0;
		peer_device->device->send_cnt += size >> 9;
	}
	return err;
}

static int _drbd_send_bio(struct drbd_peer_device *peer_device, struct bio *bio)
{
	struct bio_vec bvec;
	struct bvec_iter iter;

	/* hint all but last page with MSG_MORE */
	bio_for_each_segment(bvec, bio, iter) {
		int err;

		err = _drbd_no_send_page(peer_device, bvec.bv_page,
					 bvec.bv_offset, bvec.bv_len,
					 bio_iter_last(bvec, iter)
					 ? 0 : MSG_MORE);
		if (err)
			return err;
	}
	return 0;
}

static int _drbd_send_zc_bio(struct drbd_peer_device *peer_device, struct bio *bio)
{
	struct bio_vec bvec;
	struct bvec_iter iter;

	/* hint all but last page with MSG_MORE */
	bio_for_each_segment(bvec, bio, iter) {
		int err;

		err = _drbd_send_page(peer_device, bvec.bv_page,
				      bvec.bv_offset, bvec.bv_len,
				      bio_iter_last(bvec, iter) ? 0 : MSG_MORE);
		if (err)
			return err;
	}
	return 0;
}

static int _drbd_send_zc_ee(struct drbd_peer_device *peer_device,
			    struct drbd_peer_request *peer_req)
{
	struct page *page = peer_req->pages;
	unsigned len = peer_req->i.size;
	int err;

	/* hint all but last page with MSG_MORE */
	page_chain_for_each(page) {
		unsigned l = min_t(unsigned, len, PAGE_SIZE);

		err = _drbd_send_page(peer_device, page, 0, l,
				      page_chain_next(page) ? MSG_MORE : 0);
		if (err)
			return err;
		len -= l;
	}
	return 0;
}

static u32 bio_flags_to_wire(struct drbd_connection *connection, unsigned long bi_rw)
{
	if (connection->agreed_pro_version >= 95)
		return  (bi_rw & REQ_SYNC ? DP_RW_SYNC : 0) |
			(bi_rw & REQ_FUA ? DP_FUA : 0) |
			(bi_rw & REQ_FLUSH ? DP_FLUSH : 0) |
			(bi_rw & REQ_DISCARD ? DP_DISCARD : 0);
	else
		return bi_rw & REQ_SYNC ? DP_RW_SYNC : 0;
}

/* Used to send write or TRIM aka REQ_DISCARD requests
 * R_PRIMARY -> Peer	(P_DATA, P_TRIM)
 */
int drbd_send_dblock(struct drbd_peer_device *peer_device, struct drbd_request *req)
{
	struct drbd_device *device = peer_device->device;
	struct drbd_socket *sock;
	struct p_data *p;
	unsigned int dp_flags = 0;
	int digest_size;
	int err;

	sock = &peer_device->connection->data;
	p = drbd_prepare_command(peer_device, sock);
	digest_size = peer_device->connection->integrity_tfm ?
		      crypto_hash_digestsize(peer_device->connection->integrity_tfm) : 0;

	if (!p)
		return -EIO;
	p->sector = cpu_to_be64(req->i.sector);
	p->block_id = (unsigned long)req;
	p->seq_num = cpu_to_be32(atomic_inc_return(&device->packet_seq));
	dp_flags = bio_flags_to_wire(peer_device->connection, req->master_bio->bi_rw);
	if (device->state.conn >= C_SYNC_SOURCE &&
	    device->state.conn <= C_PAUSED_SYNC_T)
		dp_flags |= DP_MAY_SET_IN_SYNC;
	if (peer_device->connection->agreed_pro_version >= 100) {
		if (req->rq_state & RQ_EXP_RECEIVE_ACK)
			dp_flags |= DP_SEND_RECEIVE_ACK;
		/* During resync, request an explicit write ack,
		 * even in protocol != C */
		if (req->rq_state & RQ_EXP_WRITE_ACK
		|| (dp_flags & DP_MAY_SET_IN_SYNC))
			dp_flags |= DP_SEND_WRITE_ACK;
	}
	p->dp_flags = cpu_to_be32(dp_flags);

	if (dp_flags & DP_DISCARD) {
		struct p_trim *t = (struct p_trim*)p;
		t->size = cpu_to_be32(req->i.size);
		err = __send_command(peer_device->connection, device->vnr, sock, P_TRIM, sizeof(*t), NULL, 0);
		goto out;
	}

	/* our digest is still only over the payload.
	 * TRIM does not carry any payload. */
	if (digest_size)
		drbd_csum_bio(peer_device->connection->integrity_tfm, req->master_bio, p + 1);
	err = __send_command(peer_device->connection, device->vnr, sock, P_DATA, sizeof(*p) + digest_size, NULL, req->i.size);
	if (!err) {
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
		if (!(req->rq_state & (RQ_EXP_RECEIVE_ACK | RQ_EXP_WRITE_ACK)) || digest_size)
			err = _drbd_send_bio(peer_device, req->master_bio);
		else
			err = _drbd_send_zc_bio(peer_device, req->master_bio);

		/* double check digest, sometimes buffers have been modified in flight. */
		if (digest_size > 0 && digest_size <= 64) {
			/* 64 byte, 512 bit, is the largest digest size
			 * currently supported in kernel crypto. */
			unsigned char digest[64];
			drbd_csum_bio(peer_device->connection->integrity_tfm, req->master_bio, digest);
			if (memcmp(p + 1, digest, digest_size)) {
				drbd_warn(device,
					"Digest mismatch, buffer modified by upper layers during write: %llus +%u\n",
					(unsigned long long)req->i.sector, req->i.size);
			}
		} /* else if (digest_size > 64) {
		     ... Be noisy about digest too large ...
		} */
	}
out:
	mutex_unlock(&sock->mutex);  /* locked by drbd_prepare_command() */

	return err;
}

/* answer packet, used to send data back for read requests:
 *  Peer       -> (diskless) R_PRIMARY   (P_DATA_REPLY)
 *  C_SYNC_SOURCE -> C_SYNC_TARGET         (P_RS_DATA_REPLY)
 */
int drbd_send_block(struct drbd_peer_device *peer_device, enum drbd_packet cmd,
		    struct drbd_peer_request *peer_req)
{
	struct drbd_device *device = peer_device->device;
	struct drbd_socket *sock;
	struct p_data *p;
	int err;
	int digest_size;

	sock = &peer_device->connection->data;
	p = drbd_prepare_command(peer_device, sock);

	digest_size = peer_device->connection->integrity_tfm ?
		      crypto_hash_digestsize(peer_device->connection->integrity_tfm) : 0;

	if (!p)
		return -EIO;
	p->sector = cpu_to_be64(peer_req->i.sector);
	p->block_id = peer_req->block_id;
	p->seq_num = 0;  /* unused */
	p->dp_flags = 0;
	if (digest_size)
		drbd_csum_ee(peer_device->connection->integrity_tfm, peer_req, p + 1);
	err = __send_command(peer_device->connection, device->vnr, sock, cmd, sizeof(*p) + digest_size, NULL, peer_req->i.size);
	if (!err)
		err = _drbd_send_zc_ee(peer_device, peer_req);
	mutex_unlock(&sock->mutex);  /* locked by drbd_prepare_command() */

	return err;
}

int drbd_send_out_of_sync(struct drbd_peer_device *peer_device, struct drbd_request *req)
{
	struct drbd_socket *sock;
	struct p_block_desc *p;

	sock = &peer_device->connection->data;
	p = drbd_prepare_command(peer_device, sock);
	if (!p)
		return -EIO;
	p->sector = cpu_to_be64(req->i.sector);
	p->blksize = cpu_to_be32(req->i.size);
	return drbd_send_command(peer_device, sock, P_OUT_OF_SYNC, sizeof(*p), NULL, 0);
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
int drbd_send(struct drbd_connection *connection, struct socket *sock,
	      void *buf, size_t size, unsigned msg_flags)
{
	struct kvec iov;
	struct msghdr msg;
	int rv, sent = 0;

	if (!sock)
		return -EBADR;

	/* THINK  if (signal_pending) return ... ? */

	iov.iov_base = buf;
	iov.iov_len  = size;

	msg.msg_name       = NULL;
	msg.msg_namelen    = 0;
	msg.msg_control    = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags      = msg_flags | MSG_NOSIGNAL;

	if (sock == connection->data.socket) {
		rcu_read_lock();
		connection->ko_count = rcu_dereference(connection->net_conf)->ko_count;
		rcu_read_unlock();
		drbd_update_congested(connection);
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
			if (we_should_drop_the_connection(connection, sock))
				break;
			else
				continue;
		}
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

	if (sock == connection->data.socket)
		clear_bit(NET_CONGESTED, &connection->flags);

	if (rv <= 0) {
		if (rv != -EAGAIN) {
			drbd_err(connection, "%s_sendmsg returned %d\n",
				 sock == connection->meta.socket ? "msock" : "sock",
				 rv);
			conn_request_state(connection, NS(conn, C_BROKEN_PIPE), CS_HARD);
		} else
			conn_request_state(connection, NS(conn, C_TIMEOUT), CS_HARD);
	}

	return sent;
}

/**
 * drbd_send_all  -  Send an entire buffer
 *
 * Returns 0 upon success and a negative error value otherwise.
 */
int drbd_send_all(struct drbd_connection *connection, struct socket *sock, void *buffer,
		  size_t size, unsigned msg_flags)
{
	int err;

	err = drbd_send(connection, sock, buffer, size, msg_flags);
	if (err < 0)
		return err;
	if (err != size)
		return -EIO;
	return 0;
}

static int drbd_open(struct block_device *bdev, fmode_t mode)
{
	struct drbd_device *device = bdev->bd_disk->private_data;
	unsigned long flags;
	int rv = 0;

	mutex_lock(&drbd_main_mutex);
	spin_lock_irqsave(&device->resource->req_lock, flags);
	/* to have a stable device->state.role
	 * and no race with updating open_cnt */

	if (device->state.role != R_PRIMARY) {
		if (mode & FMODE_WRITE)
			rv = -EROFS;
		else if (!allow_oos)
			rv = -EMEDIUMTYPE;
	}

	if (!rv)
		device->open_cnt++;
	spin_unlock_irqrestore(&device->resource->req_lock, flags);
	mutex_unlock(&drbd_main_mutex);

	return rv;
}

static void drbd_release(struct gendisk *gd, fmode_t mode)
{
	struct drbd_device *device = gd->private_data;
	mutex_lock(&drbd_main_mutex);
	device->open_cnt--;
	mutex_unlock(&drbd_main_mutex);
}

static void drbd_set_defaults(struct drbd_device *device)
{
	/* Beware! The actual layout differs
	 * between big endian and little endian */
	device->state = (union drbd_dev_state) {
		{ .role = R_SECONDARY,
		  .peer = R_UNKNOWN,
		  .conn = C_STANDALONE,
		  .disk = D_DISKLESS,
		  .pdsk = D_UNKNOWN,
		} };
}

void drbd_init_set_defaults(struct drbd_device *device)
{
	/* the memset(,0,) did most of this.
	 * note: only assignments, no allocation in here */

	drbd_set_defaults(device);

	atomic_set(&device->ap_bio_cnt, 0);
	atomic_set(&device->ap_actlog_cnt, 0);
	atomic_set(&device->ap_pending_cnt, 0);
	atomic_set(&device->rs_pending_cnt, 0);
	atomic_set(&device->unacked_cnt, 0);
	atomic_set(&device->local_cnt, 0);
	atomic_set(&device->pp_in_use_by_net, 0);
	atomic_set(&device->rs_sect_in, 0);
	atomic_set(&device->rs_sect_ev, 0);
	atomic_set(&device->ap_in_flight, 0);
	atomic_set(&device->md_io.in_use, 0);

	mutex_init(&device->own_state_mutex);
	device->state_mutex = &device->own_state_mutex;

	spin_lock_init(&device->al_lock);
	spin_lock_init(&device->peer_seq_lock);

	INIT_LIST_HEAD(&device->active_ee);
	INIT_LIST_HEAD(&device->sync_ee);
	INIT_LIST_HEAD(&device->done_ee);
	INIT_LIST_HEAD(&device->read_ee);
	INIT_LIST_HEAD(&device->net_ee);
	INIT_LIST_HEAD(&device->resync_reads);
	INIT_LIST_HEAD(&device->resync_work.list);
	INIT_LIST_HEAD(&device->unplug_work.list);
	INIT_LIST_HEAD(&device->bm_io_work.w.list);
	INIT_LIST_HEAD(&device->pending_master_completion[0]);
	INIT_LIST_HEAD(&device->pending_master_completion[1]);
	INIT_LIST_HEAD(&device->pending_completion[0]);
	INIT_LIST_HEAD(&device->pending_completion[1]);

	device->resync_work.cb  = w_resync_timer;
	device->unplug_work.cb  = w_send_write_hint;
	device->bm_io_work.w.cb = w_bitmap_io;

	init_timer(&device->resync_timer);
	init_timer(&device->md_sync_timer);
	init_timer(&device->start_resync_timer);
	init_timer(&device->request_timer);
	device->resync_timer.function = resync_timer_fn;
	device->resync_timer.data = (unsigned long) device;
	device->md_sync_timer.function = md_sync_timer_fn;
	device->md_sync_timer.data = (unsigned long) device;
	device->start_resync_timer.function = start_resync_timer_fn;
	device->start_resync_timer.data = (unsigned long) device;
	device->request_timer.function = request_timer_fn;
	device->request_timer.data = (unsigned long) device;

	init_waitqueue_head(&device->misc_wait);
	init_waitqueue_head(&device->state_wait);
	init_waitqueue_head(&device->ee_wait);
	init_waitqueue_head(&device->al_wait);
	init_waitqueue_head(&device->seq_wait);

	device->resync_wenr = LC_FREE;
	device->peer_max_bio_size = DRBD_MAX_BIO_SIZE_SAFE;
	device->local_max_bio_size = DRBD_MAX_BIO_SIZE_SAFE;
}

void drbd_device_cleanup(struct drbd_device *device)
{
	int i;
	if (first_peer_device(device)->connection->receiver.t_state != NONE)
		drbd_err(device, "ASSERT FAILED: receiver t_state == %d expected 0.\n",
				first_peer_device(device)->connection->receiver.t_state);

	device->al_writ_cnt  =
	device->bm_writ_cnt  =
	device->read_cnt     =
	device->recv_cnt     =
	device->send_cnt     =
	device->writ_cnt     =
	device->p_size       =
	device->rs_start     =
	device->rs_total     =
	device->rs_failed    = 0;
	device->rs_last_events = 0;
	device->rs_last_sect_ev = 0;
	for (i = 0; i < DRBD_SYNC_MARKS; i++) {
		device->rs_mark_left[i] = 0;
		device->rs_mark_time[i] = 0;
	}
	D_ASSERT(device, first_peer_device(device)->connection->net_conf == NULL);

	drbd_set_my_capacity(device, 0);
	if (device->bitmap) {
		/* maybe never allocated. */
		drbd_bm_resize(device, 0, 1);
		drbd_bm_cleanup(device);
	}

	drbd_free_ldev(device->ldev);
	device->ldev = NULL;

	clear_bit(AL_SUSPENDED, &device->flags);

	D_ASSERT(device, list_empty(&device->active_ee));
	D_ASSERT(device, list_empty(&device->sync_ee));
	D_ASSERT(device, list_empty(&device->done_ee));
	D_ASSERT(device, list_empty(&device->read_ee));
	D_ASSERT(device, list_empty(&device->net_ee));
	D_ASSERT(device, list_empty(&device->resync_reads));
	D_ASSERT(device, list_empty(&first_peer_device(device)->connection->sender_work.q));
	D_ASSERT(device, list_empty(&device->resync_work.list));
	D_ASSERT(device, list_empty(&device->unplug_work.list));

	drbd_set_defaults(device);
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

	/* D_ASSERT(device, atomic_read(&drbd_pp_vacant)==0); */

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
		"drbd_ee", sizeof(struct drbd_peer_request), 0, 0, NULL);
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
	drbd_md_io_bio_set = bioset_create(DRBD_MIN_POOL_PAGES, 0);
	if (drbd_md_io_bio_set == NULL)
		goto Enomem;

	drbd_md_io_page_pool = mempool_create_page_pool(DRBD_MIN_POOL_PAGES, 0);
	if (drbd_md_io_page_pool == NULL)
		goto Enomem;

	drbd_request_mempool = mempool_create_slab_pool(number,
		drbd_request_cache);
	if (drbd_request_mempool == NULL)
		goto Enomem;

	drbd_ee_mempool = mempool_create_slab_pool(number, drbd_ee_cache);
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

static void drbd_release_all_peer_reqs(struct drbd_device *device)
{
	int rr;

	rr = drbd_free_peer_reqs(device, &device->active_ee);
	if (rr)
		drbd_err(device, "%d EEs in active list found!\n", rr);

	rr = drbd_free_peer_reqs(device, &device->sync_ee);
	if (rr)
		drbd_err(device, "%d EEs in sync list found!\n", rr);

	rr = drbd_free_peer_reqs(device, &device->read_ee);
	if (rr)
		drbd_err(device, "%d EEs in read list found!\n", rr);

	rr = drbd_free_peer_reqs(device, &device->done_ee);
	if (rr)
		drbd_err(device, "%d EEs in done list found!\n", rr);

	rr = drbd_free_peer_reqs(device, &device->net_ee);
	if (rr)
		drbd_err(device, "%d EEs in net list found!\n", rr);
}

/* caution. no locking. */
void drbd_destroy_device(struct kref *kref)
{
	struct drbd_device *device = container_of(kref, struct drbd_device, kref);
	struct drbd_resource *resource = device->resource;
	struct drbd_peer_device *peer_device, *tmp_peer_device;

	del_timer_sync(&device->request_timer);

	/* paranoia asserts */
	D_ASSERT(device, device->open_cnt == 0);
	/* end paranoia asserts */

	/* cleanup stuff that may have been allocated during
	 * device (re-)configuration or state changes */

	if (device->this_bdev)
		bdput(device->this_bdev);

	drbd_free_ldev(device->ldev);
	device->ldev = NULL;

	drbd_release_all_peer_reqs(device);

	lc_destroy(device->act_log);
	lc_destroy(device->resync);

	kfree(device->p_uuid);
	/* device->p_uuid = NULL; */

	if (device->bitmap) /* should no longer be there. */
		drbd_bm_cleanup(device);
	__free_page(device->md_io.page);
	put_disk(device->vdisk);
	blk_cleanup_queue(device->rq_queue);
	kfree(device->rs_plan_s);

	/* not for_each_connection(connection, resource):
	 * those may have been cleaned up and disassociated already.
	 */
	for_each_peer_device_safe(peer_device, tmp_peer_device, device) {
		kref_put(&peer_device->connection->kref, drbd_destroy_connection);
		kfree(peer_device);
	}
	memset(device, 0xfd, sizeof(*device));
	kfree(device);
	kref_put(&resource->kref, drbd_destroy_resource);
}

/* One global retry thread, if we need to push back some bio and have it
 * reinserted through our make request function.
 */
static struct retry_worker {
	struct workqueue_struct *wq;
	struct work_struct worker;

	spinlock_t lock;
	struct list_head writes;
} retry;

static void do_retry(struct work_struct *ws)
{
	struct retry_worker *retry = container_of(ws, struct retry_worker, worker);
	LIST_HEAD(writes);
	struct drbd_request *req, *tmp;

	spin_lock_irq(&retry->lock);
	list_splice_init(&retry->writes, &writes);
	spin_unlock_irq(&retry->lock);

	list_for_each_entry_safe(req, tmp, &writes, tl_requests) {
		struct drbd_device *device = req->device;
		struct bio *bio = req->master_bio;
		unsigned long start_jif = req->start_jif;
		bool expected;

		expected =
			expect(atomic_read(&req->completion_ref) == 0) &&
			expect(req->rq_state & RQ_POSTPONED) &&
			expect((req->rq_state & RQ_LOCAL_PENDING) == 0 ||
				(req->rq_state & RQ_LOCAL_ABORTED) != 0);

		if (!expected)
			drbd_err(device, "req=%p completion_ref=%d rq_state=%x\n",
				req, atomic_read(&req->completion_ref),
				req->rq_state);

		/* We still need to put one kref associated with the
		 * "completion_ref" going zero in the code path that queued it
		 * here.  The request object may still be referenced by a
		 * frozen local req->private_bio, in case we force-detached.
		 */
		kref_put(&req->kref, drbd_req_destroy);

		/* A single suspended or otherwise blocking device may stall
		 * all others as well.  Fortunately, this code path is to
		 * recover from a situation that "should not happen":
		 * concurrent writes in multi-primary setup.
		 * In a "normal" lifecycle, this workqueue is supposed to be
		 * destroyed without ever doing anything.
		 * If it turns out to be an issue anyways, we can do per
		 * resource (replication group) or per device (minor) retry
		 * workqueues instead.
		 */

		/* We are not just doing generic_make_request(),
		 * as we want to keep the start_time information. */
		inc_ap_bio(device);
		__drbd_make_request(device, bio, start_jif);
	}
}

/* called via drbd_req_put_completion_ref(),
 * holds resource->req_lock */
void drbd_restart_request(struct drbd_request *req)
{
	unsigned long flags;
	spin_lock_irqsave(&retry.lock, flags);
	list_move_tail(&req->tl_requests, &retry.writes);
	spin_unlock_irqrestore(&retry.lock, flags);

	/* Drop the extra reference that would otherwise
	 * have been dropped by complete_master_bio.
	 * do_retry() needs to grab a new one. */
	dec_ap_bio(req->device);

	queue_work(retry.wq, &retry.worker);
}

void drbd_destroy_resource(struct kref *kref)
{
	struct drbd_resource *resource =
		container_of(kref, struct drbd_resource, kref);

	idr_destroy(&resource->devices);
	free_cpumask_var(resource->cpu_mask);
	kfree(resource->name);
	memset(resource, 0xf2, sizeof(*resource));
	kfree(resource);
}

void drbd_free_resource(struct drbd_resource *resource)
{
	struct drbd_connection *connection, *tmp;

	for_each_connection_safe(connection, tmp, resource) {
		list_del(&connection->connections);
		drbd_debugfs_connection_cleanup(connection);
		kref_put(&connection->kref, drbd_destroy_connection);
	}
	drbd_debugfs_resource_cleanup(resource);
	kref_put(&resource->kref, drbd_destroy_resource);
}

static void drbd_cleanup(void)
{
	unsigned int i;
	struct drbd_device *device;
	struct drbd_resource *resource, *tmp;

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

	if (retry.wq)
		destroy_workqueue(retry.wq);

	drbd_genl_unregister();
	drbd_debugfs_cleanup();

	idr_for_each_entry(&drbd_devices, device, i)
		drbd_delete_device(device);

	/* not _rcu since, no other updater anymore. Genl already unregistered */
	for_each_resource_safe(resource, tmp, &drbd_resources) {
		list_del(&resource->resources);
		drbd_free_resource(resource);
	}

	drbd_destroy_mempools();
	unregister_blkdev(DRBD_MAJOR, "drbd");

	idr_destroy(&drbd_devices);

	pr_info("module cleanup done.\n");
}

/**
 * drbd_congested() - Callback for the flusher thread
 * @congested_data:	User data
 * @bdi_bits:		Bits the BDI flusher thread is currently interested in
 *
 * Returns 1<<WB_async_congested and/or 1<<WB_sync_congested if we are congested.
 */
static int drbd_congested(void *congested_data, int bdi_bits)
{
	struct drbd_device *device = congested_data;
	struct request_queue *q;
	char reason = '-';
	int r = 0;

	if (!may_inc_ap_bio(device)) {
		/* DRBD has frozen IO */
		r = bdi_bits;
		reason = 'd';
		goto out;
	}

	if (test_bit(CALLBACK_PENDING, &first_peer_device(device)->connection->flags)) {
		r |= (1 << WB_async_congested);
		/* Without good local data, we would need to read from remote,
		 * and that would need the worker thread as well, which is
		 * currently blocked waiting for that usermode helper to
		 * finish.
		 */
		if (!get_ldev_if_state(device, D_UP_TO_DATE))
			r |= (1 << WB_sync_congested);
		else
			put_ldev(device);
		r &= bdi_bits;
		reason = 'c';
		goto out;
	}

	if (get_ldev(device)) {
		q = bdev_get_queue(device->ldev->backing_bdev);
		r = bdi_congested(&q->backing_dev_info, bdi_bits);
		put_ldev(device);
		if (r)
			reason = 'b';
	}

	if (bdi_bits & (1 << WB_async_congested) &&
	    test_bit(NET_CONGESTED, &first_peer_device(device)->connection->flags)) {
		r |= (1 << WB_async_congested);
		reason = reason == 'b' ? 'a' : 'n';
	}

out:
	device->congestion_reason = reason;
	return r;
}

static void drbd_init_workqueue(struct drbd_work_queue* wq)
{
	spin_lock_init(&wq->q_lock);
	INIT_LIST_HEAD(&wq->q);
	init_waitqueue_head(&wq->q_wait);
}

struct completion_work {
	struct drbd_work w;
	struct completion done;
};

static int w_complete(struct drbd_work *w, int cancel)
{
	struct completion_work *completion_work =
		container_of(w, struct completion_work, w);

	complete(&completion_work->done);
	return 0;
}

void drbd_flush_workqueue(struct drbd_work_queue *work_queue)
{
	struct completion_work completion_work;

	completion_work.w.cb = w_complete;
	init_completion(&completion_work.done);
	drbd_queue_work(work_queue, &completion_work.w);
	wait_for_completion(&completion_work.done);
}

struct drbd_resource *drbd_find_resource(const char *name)
{
	struct drbd_resource *resource;

	if (!name || !name[0])
		return NULL;

	rcu_read_lock();
	for_each_resource_rcu(resource, &drbd_resources) {
		if (!strcmp(resource->name, name)) {
			kref_get(&resource->kref);
			goto found;
		}
	}
	resource = NULL;
found:
	rcu_read_unlock();
	return resource;
}

struct drbd_connection *conn_get_by_addrs(void *my_addr, int my_addr_len,
				     void *peer_addr, int peer_addr_len)
{
	struct drbd_resource *resource;
	struct drbd_connection *connection;

	rcu_read_lock();
	for_each_resource_rcu(resource, &drbd_resources) {
		for_each_connection_rcu(connection, resource) {
			if (connection->my_addr_len == my_addr_len &&
			    connection->peer_addr_len == peer_addr_len &&
			    !memcmp(&connection->my_addr, my_addr, my_addr_len) &&
			    !memcmp(&connection->peer_addr, peer_addr, peer_addr_len)) {
				kref_get(&connection->kref);
				goto found;
			}
		}
	}
	connection = NULL;
found:
	rcu_read_unlock();
	return connection;
}

static int drbd_alloc_socket(struct drbd_socket *socket)
{
	socket->rbuf = (void *) __get_free_page(GFP_KERNEL);
	if (!socket->rbuf)
		return -ENOMEM;
	socket->sbuf = (void *) __get_free_page(GFP_KERNEL);
	if (!socket->sbuf)
		return -ENOMEM;
	return 0;
}

static void drbd_free_socket(struct drbd_socket *socket)
{
	free_page((unsigned long) socket->sbuf);
	free_page((unsigned long) socket->rbuf);
}

void conn_free_crypto(struct drbd_connection *connection)
{
	drbd_free_sock(connection);

	crypto_free_hash(connection->csums_tfm);
	crypto_free_hash(connection->verify_tfm);
	crypto_free_hash(connection->cram_hmac_tfm);
	crypto_free_hash(connection->integrity_tfm);
	crypto_free_hash(connection->peer_integrity_tfm);
	kfree(connection->int_dig_in);
	kfree(connection->int_dig_vv);

	connection->csums_tfm = NULL;
	connection->verify_tfm = NULL;
	connection->cram_hmac_tfm = NULL;
	connection->integrity_tfm = NULL;
	connection->peer_integrity_tfm = NULL;
	connection->int_dig_in = NULL;
	connection->int_dig_vv = NULL;
}

int set_resource_options(struct drbd_resource *resource, struct res_opts *res_opts)
{
	struct drbd_connection *connection;
	cpumask_var_t new_cpu_mask;
	int err;

	if (!zalloc_cpumask_var(&new_cpu_mask, GFP_KERNEL))
		return -ENOMEM;

	/* silently ignore cpu mask on UP kernel */
	if (nr_cpu_ids > 1 && res_opts->cpu_mask[0] != 0) {
		err = bitmap_parse(res_opts->cpu_mask, DRBD_CPU_MASK_SIZE,
				   cpumask_bits(new_cpu_mask), nr_cpu_ids);
		if (err == -EOVERFLOW) {
			/* So what. mask it out. */
			cpumask_var_t tmp_cpu_mask;
			if (zalloc_cpumask_var(&tmp_cpu_mask, GFP_KERNEL)) {
				cpumask_setall(tmp_cpu_mask);
				cpumask_and(new_cpu_mask, new_cpu_mask, tmp_cpu_mask);
				drbd_warn(resource, "Overflow in bitmap_parse(%.12s%s), truncating to %u bits\n",
					res_opts->cpu_mask,
					strlen(res_opts->cpu_mask) > 12 ? "..." : "",
					nr_cpu_ids);
				free_cpumask_var(tmp_cpu_mask);
				err = 0;
			}
		}
		if (err) {
			drbd_warn(resource, "bitmap_parse() failed with %d\n", err);
			/* retcode = ERR_CPU_MASK_PARSE; */
			goto fail;
		}
	}
	resource->res_opts = *res_opts;
	if (cpumask_empty(new_cpu_mask))
		drbd_calc_cpu_mask(&new_cpu_mask);
	if (!cpumask_equal(resource->cpu_mask, new_cpu_mask)) {
		cpumask_copy(resource->cpu_mask, new_cpu_mask);
		for_each_connection_rcu(connection, resource) {
			connection->receiver.reset_cpu_mask = 1;
			connection->asender.reset_cpu_mask = 1;
			connection->worker.reset_cpu_mask = 1;
		}
	}
	err = 0;

fail:
	free_cpumask_var(new_cpu_mask);
	return err;

}

struct drbd_resource *drbd_create_resource(const char *name)
{
	struct drbd_resource *resource;

	resource = kzalloc(sizeof(struct drbd_resource), GFP_KERNEL);
	if (!resource)
		goto fail;
	resource->name = kstrdup(name, GFP_KERNEL);
	if (!resource->name)
		goto fail_free_resource;
	if (!zalloc_cpumask_var(&resource->cpu_mask, GFP_KERNEL))
		goto fail_free_name;
	kref_init(&resource->kref);
	idr_init(&resource->devices);
	INIT_LIST_HEAD(&resource->connections);
	resource->write_ordering = WO_bdev_flush;
	list_add_tail_rcu(&resource->resources, &drbd_resources);
	mutex_init(&resource->conf_update);
	mutex_init(&resource->adm_mutex);
	spin_lock_init(&resource->req_lock);
	drbd_debugfs_resource_add(resource);
	return resource;

fail_free_name:
	kfree(resource->name);
fail_free_resource:
	kfree(resource);
fail:
	return NULL;
}

/* caller must be under adm_mutex */
struct drbd_connection *conn_create(const char *name, struct res_opts *res_opts)
{
	struct drbd_resource *resource;
	struct drbd_connection *connection;

	connection = kzalloc(sizeof(struct drbd_connection), GFP_KERNEL);
	if (!connection)
		return NULL;

	if (drbd_alloc_socket(&connection->data))
		goto fail;
	if (drbd_alloc_socket(&connection->meta))
		goto fail;

	connection->current_epoch = kzalloc(sizeof(struct drbd_epoch), GFP_KERNEL);
	if (!connection->current_epoch)
		goto fail;

	INIT_LIST_HEAD(&connection->transfer_log);

	INIT_LIST_HEAD(&connection->current_epoch->list);
	connection->epochs = 1;
	spin_lock_init(&connection->epoch_lock);

	connection->send.seen_any_write_yet = false;
	connection->send.current_epoch_nr = 0;
	connection->send.current_epoch_writes = 0;

	resource = drbd_create_resource(name);
	if (!resource)
		goto fail;

	connection->cstate = C_STANDALONE;
	mutex_init(&connection->cstate_mutex);
	init_waitqueue_head(&connection->ping_wait);
	idr_init(&connection->peer_devices);

	drbd_init_workqueue(&connection->sender_work);
	mutex_init(&connection->data.mutex);
	mutex_init(&connection->meta.mutex);

	drbd_thread_init(resource, &connection->receiver, drbd_receiver, "receiver");
	connection->receiver.connection = connection;
	drbd_thread_init(resource, &connection->worker, drbd_worker, "worker");
	connection->worker.connection = connection;
	drbd_thread_init(resource, &connection->asender, drbd_asender, "asender");
	connection->asender.connection = connection;

	kref_init(&connection->kref);

	connection->resource = resource;

	if (set_resource_options(resource, res_opts))
		goto fail_resource;

	kref_get(&resource->kref);
	list_add_tail_rcu(&connection->connections, &resource->connections);
	drbd_debugfs_connection_add(connection);
	return connection;

fail_resource:
	list_del(&resource->resources);
	drbd_free_resource(resource);
fail:
	kfree(connection->current_epoch);
	drbd_free_socket(&connection->meta);
	drbd_free_socket(&connection->data);
	kfree(connection);
	return NULL;
}

void drbd_destroy_connection(struct kref *kref)
{
	struct drbd_connection *connection = container_of(kref, struct drbd_connection, kref);
	struct drbd_resource *resource = connection->resource;

	if (atomic_read(&connection->current_epoch->epoch_size) !=  0)
		drbd_err(connection, "epoch_size:%d\n", atomic_read(&connection->current_epoch->epoch_size));
	kfree(connection->current_epoch);

	idr_destroy(&connection->peer_devices);

	drbd_free_socket(&connection->meta);
	drbd_free_socket(&connection->data);
	kfree(connection->int_dig_in);
	kfree(connection->int_dig_vv);
	memset(connection, 0xfc, sizeof(*connection));
	kfree(connection);
	kref_put(&resource->kref, drbd_destroy_resource);
}

static int init_submitter(struct drbd_device *device)
{
	/* opencoded create_singlethread_workqueue(),
	 * to be able to say "drbd%d", ..., minor */
	device->submit.wq = alloc_workqueue("drbd%u_submit",
			WQ_UNBOUND | WQ_MEM_RECLAIM, 1, device->minor);
	if (!device->submit.wq)
		return -ENOMEM;

	INIT_WORK(&device->submit.worker, do_submit);
	INIT_LIST_HEAD(&device->submit.writes);
	return 0;
}

enum drbd_ret_code drbd_create_device(struct drbd_config_context *adm_ctx, unsigned int minor)
{
	struct drbd_resource *resource = adm_ctx->resource;
	struct drbd_connection *connection;
	struct drbd_device *device;
	struct drbd_peer_device *peer_device, *tmp_peer_device;
	struct gendisk *disk;
	struct request_queue *q;
	int id;
	int vnr = adm_ctx->volume;
	enum drbd_ret_code err = ERR_NOMEM;

	device = minor_to_device(minor);
	if (device)
		return ERR_MINOR_OR_VOLUME_EXISTS;

	/* GFP_KERNEL, we are outside of all write-out paths */
	device = kzalloc(sizeof(struct drbd_device), GFP_KERNEL);
	if (!device)
		return ERR_NOMEM;
	kref_init(&device->kref);

	kref_get(&resource->kref);
	device->resource = resource;
	device->minor = minor;
	device->vnr = vnr;

	drbd_init_set_defaults(device);

	q = blk_alloc_queue(GFP_KERNEL);
	if (!q)
		goto out_no_q;
	device->rq_queue = q;
	q->queuedata   = device;

	disk = alloc_disk(1);
	if (!disk)
		goto out_no_disk;
	device->vdisk = disk;

	set_disk_ro(disk, true);

	disk->queue = q;
	disk->major = DRBD_MAJOR;
	disk->first_minor = minor;
	disk->fops = &drbd_ops;
	sprintf(disk->disk_name, "drbd%d", minor);
	disk->private_data = device;

	device->this_bdev = bdget(MKDEV(DRBD_MAJOR, minor));
	/* we have no partitions. we contain only ourselves. */
	device->this_bdev->bd_contains = device->this_bdev;

	q->backing_dev_info.congested_fn = drbd_congested;
	q->backing_dev_info.congested_data = device;

	blk_queue_make_request(q, drbd_make_request);
	blk_queue_flush(q, REQ_FLUSH | REQ_FUA);
	/* Setting the max_hw_sectors to an odd value of 8kibyte here
	   This triggers a max_bio_size message upon first attach or connect */
	blk_queue_max_hw_sectors(q, DRBD_MAX_BIO_SIZE_SAFE >> 8);
	blk_queue_bounce_limit(q, BLK_BOUNCE_ANY);
	blk_queue_merge_bvec(q, drbd_merge_bvec);
	q->queue_lock = &resource->req_lock;

	device->md_io.page = alloc_page(GFP_KERNEL);
	if (!device->md_io.page)
		goto out_no_io_page;

	if (drbd_bm_init(device))
		goto out_no_bitmap;
	device->read_requests = RB_ROOT;
	device->write_requests = RB_ROOT;

	id = idr_alloc(&drbd_devices, device, minor, minor + 1, GFP_KERNEL);
	if (id < 0) {
		if (id == -ENOSPC)
			err = ERR_MINOR_OR_VOLUME_EXISTS;
		goto out_no_minor_idr;
	}
	kref_get(&device->kref);

	id = idr_alloc(&resource->devices, device, vnr, vnr + 1, GFP_KERNEL);
	if (id < 0) {
		if (id == -ENOSPC)
			err = ERR_MINOR_OR_VOLUME_EXISTS;
		goto out_idr_remove_minor;
	}
	kref_get(&device->kref);

	INIT_LIST_HEAD(&device->peer_devices);
	INIT_LIST_HEAD(&device->pending_bitmap_io);
	for_each_connection(connection, resource) {
		peer_device = kzalloc(sizeof(struct drbd_peer_device), GFP_KERNEL);
		if (!peer_device)
			goto out_idr_remove_from_resource;
		peer_device->connection = connection;
		peer_device->device = device;

		list_add(&peer_device->peer_devices, &device->peer_devices);
		kref_get(&device->kref);

		id = idr_alloc(&connection->peer_devices, peer_device, vnr, vnr + 1, GFP_KERNEL);
		if (id < 0) {
			if (id == -ENOSPC)
				err = ERR_INVALID_REQUEST;
			goto out_idr_remove_from_resource;
		}
		kref_get(&connection->kref);
	}

	if (init_submitter(device)) {
		err = ERR_NOMEM;
		goto out_idr_remove_vol;
	}

	add_disk(disk);

	/* inherit the connection state */
	device->state.conn = first_connection(resource)->cstate;
	if (device->state.conn == C_WF_REPORT_PARAMS) {
		for_each_peer_device(peer_device, device)
			drbd_connected(peer_device);
	}
	/* move to create_peer_device() */
	for_each_peer_device(peer_device, device)
		drbd_debugfs_peer_device_add(peer_device);
	drbd_debugfs_device_add(device);
	return NO_ERROR;

out_idr_remove_vol:
	idr_remove(&connection->peer_devices, vnr);
out_idr_remove_from_resource:
	for_each_connection(connection, resource) {
		peer_device = idr_find(&connection->peer_devices, vnr);
		if (peer_device) {
			idr_remove(&connection->peer_devices, vnr);
			kref_put(&connection->kref, drbd_destroy_connection);
		}
	}
	for_each_peer_device_safe(peer_device, tmp_peer_device, device) {
		list_del(&peer_device->peer_devices);
		kfree(peer_device);
	}
	idr_remove(&resource->devices, vnr);
out_idr_remove_minor:
	idr_remove(&drbd_devices, minor);
	synchronize_rcu();
out_no_minor_idr:
	drbd_bm_cleanup(device);
out_no_bitmap:
	__free_page(device->md_io.page);
out_no_io_page:
	put_disk(disk);
out_no_disk:
	blk_cleanup_queue(q);
out_no_q:
	kref_put(&resource->kref, drbd_destroy_resource);
	kfree(device);
	return err;
}

void drbd_delete_device(struct drbd_device *device)
{
	struct drbd_resource *resource = device->resource;
	struct drbd_connection *connection;
	struct drbd_peer_device *peer_device;
	int refs = 3;

	/* move to free_peer_device() */
	for_each_peer_device(peer_device, device)
		drbd_debugfs_peer_device_cleanup(peer_device);
	drbd_debugfs_device_cleanup(device);
	for_each_connection(connection, resource) {
		idr_remove(&connection->peer_devices, device->vnr);
		refs++;
	}
	idr_remove(&resource->devices, device->vnr);
	idr_remove(&drbd_devices, device_to_minor(device));
	del_gendisk(device->vdisk);
	synchronize_rcu();
	kref_sub(&device->kref, refs, drbd_destroy_device);
}

static int __init drbd_init(void)
{
	int err;

	if (minor_count < DRBD_MINOR_COUNT_MIN || minor_count > DRBD_MINOR_COUNT_MAX) {
		pr_err("invalid minor_count (%d)\n", minor_count);
#ifdef MODULE
		return -EINVAL;
#else
		minor_count = DRBD_MINOR_COUNT_DEF;
#endif
	}

	err = register_blkdev(DRBD_MAJOR, "drbd");
	if (err) {
		pr_err("unable to register block device major %d\n",
		       DRBD_MAJOR);
		return err;
	}

	/*
	 * allocate all necessary structs
	 */
	init_waitqueue_head(&drbd_pp_wait);

	drbd_proc = NULL; /* play safe for drbd_cleanup */
	idr_init(&drbd_devices);

	rwlock_init(&global_state_lock);
	INIT_LIST_HEAD(&drbd_resources);

	err = drbd_genl_register();
	if (err) {
		pr_err("unable to register generic netlink family\n");
		goto fail;
	}

	err = drbd_create_mempools();
	if (err)
		goto fail;

	err = -ENOMEM;
	drbd_proc = proc_create_data("drbd", S_IFREG | S_IRUGO , NULL, &drbd_proc_fops, NULL);
	if (!drbd_proc)	{
		pr_err("unable to register proc file\n");
		goto fail;
	}

	retry.wq = create_singlethread_workqueue("drbd-reissue");
	if (!retry.wq) {
		pr_err("unable to create retry workqueue\n");
		goto fail;
	}
	INIT_WORK(&retry.worker, do_retry);
	spin_lock_init(&retry.lock);
	INIT_LIST_HEAD(&retry.writes);

	if (drbd_debugfs_init())
		pr_notice("failed to initialize debugfs -- will not be available\n");

	pr_info("initialized. "
	       "Version: " REL_VERSION " (api:%d/proto:%d-%d)\n",
	       API_VERSION, PRO_VERSION_MIN, PRO_VERSION_MAX);
	pr_info("%s\n", drbd_buildtag());
	pr_info("registered as block device major %d\n", DRBD_MAJOR);
	return 0; /* Success! */

fail:
	drbd_cleanup();
	if (err == -ENOMEM)
		pr_err("ran out of memory\n");
	else
		pr_err("initialization failure\n");
	return err;
}

void drbd_free_ldev(struct drbd_backing_dev *ldev)
{
	if (ldev == NULL)
		return;

	blkdev_put(ldev->backing_bdev, FMODE_READ | FMODE_WRITE | FMODE_EXCL);
	blkdev_put(ldev->md_bdev, FMODE_READ | FMODE_WRITE | FMODE_EXCL);

	kfree(ldev->disk_conf);
	kfree(ldev);
}

static void drbd_free_one_sock(struct drbd_socket *ds)
{
	struct socket *s;
	mutex_lock(&ds->mutex);
	s = ds->socket;
	ds->socket = NULL;
	mutex_unlock(&ds->mutex);
	if (s) {
		/* so debugfs does not need to mutex_lock() */
		synchronize_rcu();
		kernel_sock_shutdown(s, SHUT_RDWR);
		sock_release(s);
	}
}

void drbd_free_sock(struct drbd_connection *connection)
{
	if (connection->data.socket)
		drbd_free_one_sock(&connection->data);
	if (connection->meta.socket)
		drbd_free_one_sock(&connection->meta);
}

/* meta data management */

void conn_md_sync(struct drbd_connection *connection)
{
	struct drbd_peer_device *peer_device;
	int vnr;

	rcu_read_lock();
	idr_for_each_entry(&connection->peer_devices, peer_device, vnr) {
		struct drbd_device *device = peer_device->device;

		kref_get(&device->kref);
		rcu_read_unlock();
		drbd_md_sync(device);
		kref_put(&device->kref, drbd_destroy_device);
		rcu_read_lock();
	}
	rcu_read_unlock();
}

/* aligned 4kByte */
struct meta_data_on_disk {
	u64 la_size_sect;      /* last agreed size. */
	u64 uuid[UI_SIZE];   /* UUIDs. */
	u64 device_uuid;
	u64 reserved_u64_1;
	u32 flags;             /* MDF */
	u32 magic;
	u32 md_size_sect;
	u32 al_offset;         /* offset to this block */
	u32 al_nr_extents;     /* important for restoring the AL (userspace) */
	      /* `-- act_log->nr_elements <-- ldev->dc.al_extents */
	u32 bm_offset;         /* offset to the bitmap, from here */
	u32 bm_bytes_per_bit;  /* BM_BLOCK_SIZE */
	u32 la_peer_max_bio_size;   /* last peer max_bio_size */

	/* see al_tr_number_to_on_disk_sector() */
	u32 al_stripes;
	u32 al_stripe_size_4k;

	u8 reserved_u8[4096 - (7*8 + 10*4)];
} __packed;



void drbd_md_write(struct drbd_device *device, void *b)
{
	struct meta_data_on_disk *buffer = b;
	sector_t sector;
	int i;

	memset(buffer, 0, sizeof(*buffer));

	buffer->la_size_sect = cpu_to_be64(drbd_get_capacity(device->this_bdev));
	for (i = UI_CURRENT; i < UI_SIZE; i++)
		buffer->uuid[i] = cpu_to_be64(device->ldev->md.uuid[i]);
	buffer->flags = cpu_to_be32(device->ldev->md.flags);
	buffer->magic = cpu_to_be32(DRBD_MD_MAGIC_84_UNCLEAN);

	buffer->md_size_sect  = cpu_to_be32(device->ldev->md.md_size_sect);
	buffer->al_offset     = cpu_to_be32(device->ldev->md.al_offset);
	buffer->al_nr_extents = cpu_to_be32(device->act_log->nr_elements);
	buffer->bm_bytes_per_bit = cpu_to_be32(BM_BLOCK_SIZE);
	buffer->device_uuid = cpu_to_be64(device->ldev->md.device_uuid);

	buffer->bm_offset = cpu_to_be32(device->ldev->md.bm_offset);
	buffer->la_peer_max_bio_size = cpu_to_be32(device->peer_max_bio_size);

	buffer->al_stripes = cpu_to_be32(device->ldev->md.al_stripes);
	buffer->al_stripe_size_4k = cpu_to_be32(device->ldev->md.al_stripe_size_4k);

	D_ASSERT(device, drbd_md_ss(device->ldev) == device->ldev->md.md_offset);
	sector = device->ldev->md.md_offset;

	if (drbd_md_sync_page_io(device, device->ldev, sector, WRITE)) {
		/* this was a try anyways ... */
		drbd_err(device, "meta data update failed!\n");
		drbd_chk_io_error(device, 1, DRBD_META_IO_ERROR);
	}
}

/**
 * drbd_md_sync() - Writes the meta data super block if the MD_DIRTY flag bit is set
 * @device:	DRBD device.
 */
void drbd_md_sync(struct drbd_device *device)
{
	struct meta_data_on_disk *buffer;

	/* Don't accidentally change the DRBD meta data layout. */
	BUILD_BUG_ON(UI_SIZE != 4);
	BUILD_BUG_ON(sizeof(struct meta_data_on_disk) != 4096);

	del_timer(&device->md_sync_timer);
	/* timer may be rearmed by drbd_md_mark_dirty() now. */
	if (!test_and_clear_bit(MD_DIRTY, &device->flags))
		return;

	/* We use here D_FAILED and not D_ATTACHING because we try to write
	 * metadata even if we detach due to a disk failure! */
	if (!get_ldev_if_state(device, D_FAILED))
		return;

	buffer = drbd_md_get_buffer(device, __func__);
	if (!buffer)
		goto out;

	drbd_md_write(device, buffer);

	/* Update device->ldev->md.la_size_sect,
	 * since we updated it on metadata. */
	device->ldev->md.la_size_sect = drbd_get_capacity(device->this_bdev);

	drbd_md_put_buffer(device);
out:
	put_ldev(device);
}

static int check_activity_log_stripe_size(struct drbd_device *device,
		struct meta_data_on_disk *on_disk,
		struct drbd_md *in_core)
{
	u32 al_stripes = be32_to_cpu(on_disk->al_stripes);
	u32 al_stripe_size_4k = be32_to_cpu(on_disk->al_stripe_size_4k);
	u64 al_size_4k;

	/* both not set: default to old fixed size activity log */
	if (al_stripes == 0 && al_stripe_size_4k == 0) {
		al_stripes = 1;
		al_stripe_size_4k = MD_32kB_SECT/8;
	}

	/* some paranoia plausibility checks */

	/* we need both values to be set */
	if (al_stripes == 0 || al_stripe_size_4k == 0)
		goto err;

	al_size_4k = (u64)al_stripes * al_stripe_size_4k;

	/* Upper limit of activity log area, to avoid potential overflow
	 * problems in al_tr_number_to_on_disk_sector(). As right now, more
	 * than 72 * 4k blocks total only increases the amount of history,
	 * limiting this arbitrarily to 16 GB is not a real limitation ;-)  */
	if (al_size_4k > (16 * 1024 * 1024/4))
		goto err;

	/* Lower limit: we need at least 8 transaction slots (32kB)
	 * to not break existing setups */
	if (al_size_4k < MD_32kB_SECT/8)
		goto err;

	in_core->al_stripe_size_4k = al_stripe_size_4k;
	in_core->al_stripes = al_stripes;
	in_core->al_size_4k = al_size_4k;

	return 0;
err:
	drbd_err(device, "invalid activity log striping: al_stripes=%u, al_stripe_size_4k=%u\n",
			al_stripes, al_stripe_size_4k);
	return -EINVAL;
}

static int check_offsets_and_sizes(struct drbd_device *device, struct drbd_backing_dev *bdev)
{
	sector_t capacity = drbd_get_capacity(bdev->md_bdev);
	struct drbd_md *in_core = &bdev->md;
	s32 on_disk_al_sect;
	s32 on_disk_bm_sect;

	/* The on-disk size of the activity log, calculated from offsets, and
	 * the size of the activity log calculated from the stripe settings,
	 * should match.
	 * Though we could relax this a bit: it is ok, if the striped activity log
	 * fits in the available on-disk activity log size.
	 * Right now, that would break how resize is implemented.
	 * TODO: make drbd_determine_dev_size() (and the drbdmeta tool) aware
	 * of possible unused padding space in the on disk layout. */
	if (in_core->al_offset < 0) {
		if (in_core->bm_offset > in_core->al_offset)
			goto err;
		on_disk_al_sect = -in_core->al_offset;
		on_disk_bm_sect = in_core->al_offset - in_core->bm_offset;
	} else {
		if (in_core->al_offset != MD_4kB_SECT)
			goto err;
		if (in_core->bm_offset < in_core->al_offset + in_core->al_size_4k * MD_4kB_SECT)
			goto err;

		on_disk_al_sect = in_core->bm_offset - MD_4kB_SECT;
		on_disk_bm_sect = in_core->md_size_sect - in_core->bm_offset;
	}

	/* old fixed size meta data is exactly that: fixed. */
	if (in_core->meta_dev_idx >= 0) {
		if (in_core->md_size_sect != MD_128MB_SECT
		||  in_core->al_offset != MD_4kB_SECT
		||  in_core->bm_offset != MD_4kB_SECT + MD_32kB_SECT
		||  in_core->al_stripes != 1
		||  in_core->al_stripe_size_4k != MD_32kB_SECT/8)
			goto err;
	}

	if (capacity < in_core->md_size_sect)
		goto err;
	if (capacity - in_core->md_size_sect < drbd_md_first_sector(bdev))
		goto err;

	/* should be aligned, and at least 32k */
	if ((on_disk_al_sect & 7) || (on_disk_al_sect < MD_32kB_SECT))
		goto err;

	/* should fit (for now: exactly) into the available on-disk space;
	 * overflow prevention is in check_activity_log_stripe_size() above. */
	if (on_disk_al_sect != in_core->al_size_4k * MD_4kB_SECT)
		goto err;

	/* again, should be aligned */
	if (in_core->bm_offset & 7)
		goto err;

	/* FIXME check for device grow with flex external meta data? */

	/* can the available bitmap space cover the last agreed device size? */
	if (on_disk_bm_sect < (in_core->la_size_sect+7)/MD_4kB_SECT/8/512)
		goto err;

	return 0;

err:
	drbd_err(device, "meta data offsets don't make sense: idx=%d "
			"al_s=%u, al_sz4k=%u, al_offset=%d, bm_offset=%d, "
			"md_size_sect=%u, la_size=%llu, md_capacity=%llu\n",
			in_core->meta_dev_idx,
			in_core->al_stripes, in_core->al_stripe_size_4k,
			in_core->al_offset, in_core->bm_offset, in_core->md_size_sect,
			(unsigned long long)in_core->la_size_sect,
			(unsigned long long)capacity);

	return -EINVAL;
}


/**
 * drbd_md_read() - Reads in the meta data super block
 * @device:	DRBD device.
 * @bdev:	Device from which the meta data should be read in.
 *
 * Return NO_ERROR on success, and an enum drbd_ret_code in case
 * something goes wrong.
 *
 * Called exactly once during drbd_adm_attach(), while still being D_DISKLESS,
 * even before @bdev is assigned to @device->ldev.
 */
int drbd_md_read(struct drbd_device *device, struct drbd_backing_dev *bdev)
{
	struct meta_data_on_disk *buffer;
	u32 magic, flags;
	int i, rv = NO_ERROR;

	if (device->state.disk != D_DISKLESS)
		return ERR_DISK_CONFIGURED;

	buffer = drbd_md_get_buffer(device, __func__);
	if (!buffer)
		return ERR_NOMEM;

	/* First, figure out where our meta data superblock is located,
	 * and read it. */
	bdev->md.meta_dev_idx = bdev->disk_conf->meta_dev_idx;
	bdev->md.md_offset = drbd_md_ss(bdev);

	if (drbd_md_sync_page_io(device, bdev, bdev->md.md_offset, READ)) {
		/* NOTE: can't do normal error processing here as this is
		   called BEFORE disk is attached */
		drbd_err(device, "Error while reading metadata.\n");
		rv = ERR_IO_MD_DISK;
		goto err;
	}

	magic = be32_to_cpu(buffer->magic);
	flags = be32_to_cpu(buffer->flags);
	if (magic == DRBD_MD_MAGIC_84_UNCLEAN ||
	    (magic == DRBD_MD_MAGIC_08 && !(flags & MDF_AL_CLEAN))) {
			/* btw: that's Activity Log clean, not "all" clean. */
		drbd_err(device, "Found unclean meta data. Did you \"drbdadm apply-al\"?\n");
		rv = ERR_MD_UNCLEAN;
		goto err;
	}

	rv = ERR_MD_INVALID;
	if (magic != DRBD_MD_MAGIC_08) {
		if (magic == DRBD_MD_MAGIC_07)
			drbd_err(device, "Found old (0.7) meta data magic. Did you \"drbdadm create-md\"?\n");
		else
			drbd_err(device, "Meta data magic not found. Did you \"drbdadm create-md\"?\n");
		goto err;
	}

	if (be32_to_cpu(buffer->bm_bytes_per_bit) != BM_BLOCK_SIZE) {
		drbd_err(device, "unexpected bm_bytes_per_bit: %u (expected %u)\n",
		    be32_to_cpu(buffer->bm_bytes_per_bit), BM_BLOCK_SIZE);
		goto err;
	}


	/* convert to in_core endian */
	bdev->md.la_size_sect = be64_to_cpu(buffer->la_size_sect);
	for (i = UI_CURRENT; i < UI_SIZE; i++)
		bdev->md.uuid[i] = be64_to_cpu(buffer->uuid[i]);
	bdev->md.flags = be32_to_cpu(buffer->flags);
	bdev->md.device_uuid = be64_to_cpu(buffer->device_uuid);

	bdev->md.md_size_sect = be32_to_cpu(buffer->md_size_sect);
	bdev->md.al_offset = be32_to_cpu(buffer->al_offset);
	bdev->md.bm_offset = be32_to_cpu(buffer->bm_offset);

	if (check_activity_log_stripe_size(device, buffer, &bdev->md))
		goto err;
	if (check_offsets_and_sizes(device, bdev))
		goto err;

	if (be32_to_cpu(buffer->bm_offset) != bdev->md.bm_offset) {
		drbd_err(device, "unexpected bm_offset: %d (expected %d)\n",
		    be32_to_cpu(buffer->bm_offset), bdev->md.bm_offset);
		goto err;
	}
	if (be32_to_cpu(buffer->md_size_sect) != bdev->md.md_size_sect) {
		drbd_err(device, "unexpected md_size: %u (expected %u)\n",
		    be32_to_cpu(buffer->md_size_sect), bdev->md.md_size_sect);
		goto err;
	}

	rv = NO_ERROR;

	spin_lock_irq(&device->resource->req_lock);
	if (device->state.conn < C_CONNECTED) {
		unsigned int peer;
		peer = be32_to_cpu(buffer->la_peer_max_bio_size);
		peer = max(peer, DRBD_MAX_BIO_SIZE_SAFE);
		device->peer_max_bio_size = peer;
	}
	spin_unlock_irq(&device->resource->req_lock);

 err:
	drbd_md_put_buffer(device);

	return rv;
}

/**
 * drbd_md_mark_dirty() - Mark meta data super block as dirty
 * @device:	DRBD device.
 *
 * Call this function if you change anything that should be written to
 * the meta-data super block. This function sets MD_DIRTY, and starts a
 * timer that ensures that within five seconds you have to call drbd_md_sync().
 */
#ifdef DEBUG
void drbd_md_mark_dirty_(struct drbd_device *device, unsigned int line, const char *func)
{
	if (!test_and_set_bit(MD_DIRTY, &device->flags)) {
		mod_timer(&device->md_sync_timer, jiffies + HZ);
		device->last_md_mark_dirty.line = line;
		device->last_md_mark_dirty.func = func;
	}
}
#else
void drbd_md_mark_dirty(struct drbd_device *device)
{
	if (!test_and_set_bit(MD_DIRTY, &device->flags))
		mod_timer(&device->md_sync_timer, jiffies + 5*HZ);
}
#endif

void drbd_uuid_move_history(struct drbd_device *device) __must_hold(local)
{
	int i;

	for (i = UI_HISTORY_START; i < UI_HISTORY_END; i++)
		device->ldev->md.uuid[i+1] = device->ldev->md.uuid[i];
}

void __drbd_uuid_set(struct drbd_device *device, int idx, u64 val) __must_hold(local)
{
	if (idx == UI_CURRENT) {
		if (device->state.role == R_PRIMARY)
			val |= 1;
		else
			val &= ~((u64)1);

		drbd_set_ed_uuid(device, val);
	}

	device->ldev->md.uuid[idx] = val;
	drbd_md_mark_dirty(device);
}

void _drbd_uuid_set(struct drbd_device *device, int idx, u64 val) __must_hold(local)
{
	unsigned long flags;
	spin_lock_irqsave(&device->ldev->md.uuid_lock, flags);
	__drbd_uuid_set(device, idx, val);
	spin_unlock_irqrestore(&device->ldev->md.uuid_lock, flags);
}

void drbd_uuid_set(struct drbd_device *device, int idx, u64 val) __must_hold(local)
{
	unsigned long flags;
	spin_lock_irqsave(&device->ldev->md.uuid_lock, flags);
	if (device->ldev->md.uuid[idx]) {
		drbd_uuid_move_history(device);
		device->ldev->md.uuid[UI_HISTORY_START] = device->ldev->md.uuid[idx];
	}
	__drbd_uuid_set(device, idx, val);
	spin_unlock_irqrestore(&device->ldev->md.uuid_lock, flags);
}

/**
 * drbd_uuid_new_current() - Creates a new current UUID
 * @device:	DRBD device.
 *
 * Creates a new current UUID, and rotates the old current UUID into
 * the bitmap slot. Causes an incremental resync upon next connect.
 */
void drbd_uuid_new_current(struct drbd_device *device) __must_hold(local)
{
	u64 val;
	unsigned long long bm_uuid;

	get_random_bytes(&val, sizeof(u64));

	spin_lock_irq(&device->ldev->md.uuid_lock);
	bm_uuid = device->ldev->md.uuid[UI_BITMAP];

	if (bm_uuid)
		drbd_warn(device, "bm UUID was already set: %llX\n", bm_uuid);

	device->ldev->md.uuid[UI_BITMAP] = device->ldev->md.uuid[UI_CURRENT];
	__drbd_uuid_set(device, UI_CURRENT, val);
	spin_unlock_irq(&device->ldev->md.uuid_lock);

	drbd_print_uuids(device, "new current UUID");
	/* get it to stable storage _now_ */
	drbd_md_sync(device);
}

void drbd_uuid_set_bm(struct drbd_device *device, u64 val) __must_hold(local)
{
	unsigned long flags;
	if (device->ldev->md.uuid[UI_BITMAP] == 0 && val == 0)
		return;

	spin_lock_irqsave(&device->ldev->md.uuid_lock, flags);
	if (val == 0) {
		drbd_uuid_move_history(device);
		device->ldev->md.uuid[UI_HISTORY_START] = device->ldev->md.uuid[UI_BITMAP];
		device->ldev->md.uuid[UI_BITMAP] = 0;
	} else {
		unsigned long long bm_uuid = device->ldev->md.uuid[UI_BITMAP];
		if (bm_uuid)
			drbd_warn(device, "bm UUID was already set: %llX\n", bm_uuid);

		device->ldev->md.uuid[UI_BITMAP] = val & ~((u64)1);
	}
	spin_unlock_irqrestore(&device->ldev->md.uuid_lock, flags);

	drbd_md_mark_dirty(device);
}

/**
 * drbd_bmio_set_n_write() - io_fn for drbd_queue_bitmap_io() or drbd_bitmap_io()
 * @device:	DRBD device.
 *
 * Sets all bits in the bitmap and writes the whole bitmap to stable storage.
 */
int drbd_bmio_set_n_write(struct drbd_device *device) __must_hold(local)
{
	int rv = -EIO;

	drbd_md_set_flag(device, MDF_FULL_SYNC);
	drbd_md_sync(device);
	drbd_bm_set_all(device);

	rv = drbd_bm_write(device);

	if (!rv) {
		drbd_md_clear_flag(device, MDF_FULL_SYNC);
		drbd_md_sync(device);
	}

	return rv;
}

/**
 * drbd_bmio_clear_n_write() - io_fn for drbd_queue_bitmap_io() or drbd_bitmap_io()
 * @device:	DRBD device.
 *
 * Clears all bits in the bitmap and writes the whole bitmap to stable storage.
 */
int drbd_bmio_clear_n_write(struct drbd_device *device) __must_hold(local)
{
	drbd_resume_al(device);
	drbd_bm_clear_all(device);
	return drbd_bm_write(device);
}

static int w_bitmap_io(struct drbd_work *w, int unused)
{
	struct drbd_device *device =
		container_of(w, struct drbd_device, bm_io_work.w);
	struct bm_io_work *work = &device->bm_io_work;
	int rv = -EIO;

	D_ASSERT(device, atomic_read(&device->ap_bio_cnt) == 0);

	if (get_ldev(device)) {
		drbd_bm_lock(device, work->why, work->flags);
		rv = work->io_fn(device);
		drbd_bm_unlock(device);
		put_ldev(device);
	}

	clear_bit_unlock(BITMAP_IO, &device->flags);
	wake_up(&device->misc_wait);

	if (work->done)
		work->done(device, rv);

	clear_bit(BITMAP_IO_QUEUED, &device->flags);
	work->why = NULL;
	work->flags = 0;

	return 0;
}

/**
 * drbd_queue_bitmap_io() - Queues an IO operation on the whole bitmap
 * @device:	DRBD device.
 * @io_fn:	IO callback to be called when bitmap IO is possible
 * @done:	callback to be called after the bitmap IO was performed
 * @why:	Descriptive text of the reason for doing the IO
 *
 * While IO on the bitmap happens we freeze application IO thus we ensure
 * that drbd_set_out_of_sync() can not be called. This function MAY ONLY be
 * called from worker context. It MUST NOT be used while a previous such
 * work is still pending!
 *
 * Its worker function encloses the call of io_fn() by get_ldev() and
 * put_ldev().
 */
void drbd_queue_bitmap_io(struct drbd_device *device,
			  int (*io_fn)(struct drbd_device *),
			  void (*done)(struct drbd_device *, int),
			  char *why, enum bm_flag flags)
{
	D_ASSERT(device, current == first_peer_device(device)->connection->worker.task);

	D_ASSERT(device, !test_bit(BITMAP_IO_QUEUED, &device->flags));
	D_ASSERT(device, !test_bit(BITMAP_IO, &device->flags));
	D_ASSERT(device, list_empty(&device->bm_io_work.w.list));
	if (device->bm_io_work.why)
		drbd_err(device, "FIXME going to queue '%s' but '%s' still pending?\n",
			why, device->bm_io_work.why);

	device->bm_io_work.io_fn = io_fn;
	device->bm_io_work.done = done;
	device->bm_io_work.why = why;
	device->bm_io_work.flags = flags;

	spin_lock_irq(&device->resource->req_lock);
	set_bit(BITMAP_IO, &device->flags);
	if (atomic_read(&device->ap_bio_cnt) == 0) {
		if (!test_and_set_bit(BITMAP_IO_QUEUED, &device->flags))
			drbd_queue_work(&first_peer_device(device)->connection->sender_work,
					&device->bm_io_work.w);
	}
	spin_unlock_irq(&device->resource->req_lock);
}

/**
 * drbd_bitmap_io() -  Does an IO operation on the whole bitmap
 * @device:	DRBD device.
 * @io_fn:	IO callback to be called when bitmap IO is possible
 * @why:	Descriptive text of the reason for doing the IO
 *
 * freezes application IO while that the actual IO operations runs. This
 * functions MAY NOT be called from worker context.
 */
int drbd_bitmap_io(struct drbd_device *device, int (*io_fn)(struct drbd_device *),
		char *why, enum bm_flag flags)
{
	int rv;

	D_ASSERT(device, current != first_peer_device(device)->connection->worker.task);

	if ((flags & BM_LOCKED_SET_ALLOWED) == 0)
		drbd_suspend_io(device);

	drbd_bm_lock(device, why, flags);
	rv = io_fn(device);
	drbd_bm_unlock(device);

	if ((flags & BM_LOCKED_SET_ALLOWED) == 0)
		drbd_resume_io(device);

	return rv;
}

void drbd_md_set_flag(struct drbd_device *device, int flag) __must_hold(local)
{
	if ((device->ldev->md.flags & flag) != flag) {
		drbd_md_mark_dirty(device);
		device->ldev->md.flags |= flag;
	}
}

void drbd_md_clear_flag(struct drbd_device *device, int flag) __must_hold(local)
{
	if ((device->ldev->md.flags & flag) != 0) {
		drbd_md_mark_dirty(device);
		device->ldev->md.flags &= ~flag;
	}
}
int drbd_md_test_flag(struct drbd_backing_dev *bdev, int flag)
{
	return (bdev->md.flags & flag) != 0;
}

static void md_sync_timer_fn(unsigned long data)
{
	struct drbd_device *device = (struct drbd_device *) data;
	drbd_device_post_work(device, MD_SYNC);
}

const char *cmdname(enum drbd_packet cmd)
{
	/* THINK may need to become several global tables
	 * when we want to support more than
	 * one PRO_VERSION */
	static const char *cmdnames[] = {
		[P_DATA]	        = "Data",
		[P_DATA_REPLY]	        = "DataReply",
		[P_RS_DATA_REPLY]	= "RSDataReply",
		[P_BARRIER]	        = "Barrier",
		[P_BITMAP]	        = "ReportBitMap",
		[P_BECOME_SYNC_TARGET]  = "BecomeSyncTarget",
		[P_BECOME_SYNC_SOURCE]  = "BecomeSyncSource",
		[P_UNPLUG_REMOTE]	= "UnplugRemote",
		[P_DATA_REQUEST]	= "DataRequest",
		[P_RS_DATA_REQUEST]     = "RSDataRequest",
		[P_SYNC_PARAM]	        = "SyncParam",
		[P_SYNC_PARAM89]	= "SyncParam89",
		[P_PROTOCOL]            = "ReportProtocol",
		[P_UUIDS]	        = "ReportUUIDs",
		[P_SIZES]	        = "ReportSizes",
		[P_STATE]	        = "ReportState",
		[P_SYNC_UUID]           = "ReportSyncUUID",
		[P_AUTH_CHALLENGE]      = "AuthChallenge",
		[P_AUTH_RESPONSE]	= "AuthResponse",
		[P_PING]		= "Ping",
		[P_PING_ACK]	        = "PingAck",
		[P_RECV_ACK]	        = "RecvAck",
		[P_WRITE_ACK]	        = "WriteAck",
		[P_RS_WRITE_ACK]	= "RSWriteAck",
		[P_SUPERSEDED]          = "Superseded",
		[P_NEG_ACK]	        = "NegAck",
		[P_NEG_DREPLY]	        = "NegDReply",
		[P_NEG_RS_DREPLY]	= "NegRSDReply",
		[P_BARRIER_ACK]	        = "BarrierAck",
		[P_STATE_CHG_REQ]       = "StateChgRequest",
		[P_STATE_CHG_REPLY]     = "StateChgReply",
		[P_OV_REQUEST]          = "OVRequest",
		[P_OV_REPLY]            = "OVReply",
		[P_OV_RESULT]           = "OVResult",
		[P_CSUM_RS_REQUEST]     = "CsumRSRequest",
		[P_RS_IS_IN_SYNC]	= "CsumRSIsInSync",
		[P_COMPRESSED_BITMAP]   = "CBitmap",
		[P_DELAY_PROBE]         = "DelayProbe",
		[P_OUT_OF_SYNC]		= "OutOfSync",
		[P_RETRY_WRITE]		= "RetryWrite",
		[P_RS_CANCEL]		= "RSCancel",
		[P_CONN_ST_CHG_REQ]	= "conn_st_chg_req",
		[P_CONN_ST_CHG_REPLY]	= "conn_st_chg_reply",
		[P_RETRY_WRITE]		= "retry_write",
		[P_PROTOCOL_UPDATE]	= "protocol_update",

		/* enum drbd_packet, but not commands - obsoleted flags:
		 *	P_MAY_IGNORE
		 *	P_MAX_OPT_CMD
		 */
	};

	/* too big for the array: 0xfffX */
	if (cmd == P_INITIAL_META)
		return "InitialMeta";
	if (cmd == P_INITIAL_DATA)
		return "InitialData";
	if (cmd == P_CONNECTION_FEATURES)
		return "ConnectionFeatures";
	if (cmd >= ARRAY_SIZE(cmdnames))
		return "Unknown";
	return cmdnames[cmd];
}

/**
 * drbd_wait_misc  -  wait for a request to make progress
 * @device:	device associated with the request
 * @i:		the struct drbd_interval embedded in struct drbd_request or
 *		struct drbd_peer_request
 */
int drbd_wait_misc(struct drbd_device *device, struct drbd_interval *i)
{
	struct net_conf *nc;
	DEFINE_WAIT(wait);
	long timeout;

	rcu_read_lock();
	nc = rcu_dereference(first_peer_device(device)->connection->net_conf);
	if (!nc) {
		rcu_read_unlock();
		return -ETIMEDOUT;
	}
	timeout = nc->ko_count ? nc->timeout * HZ / 10 * nc->ko_count : MAX_SCHEDULE_TIMEOUT;
	rcu_read_unlock();

	/* Indicate to wake up device->misc_wait on progress.  */
	i->waiting = true;
	prepare_to_wait(&device->misc_wait, &wait, TASK_INTERRUPTIBLE);
	spin_unlock_irq(&device->resource->req_lock);
	timeout = schedule_timeout(timeout);
	finish_wait(&device->misc_wait, &wait);
	spin_lock_irq(&device->resource->req_lock);
	if (!timeout || device->state.conn < C_CONNECTED)
		return -ETIMEDOUT;
	if (signal_pending(current))
		return -ERESTARTSYS;
	return 0;
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
_drbd_insert_fault(struct drbd_device *device, unsigned int type)
{
	static struct fault_random_state rrs = {0, 0};

	unsigned int ret = (
		(fault_devs == 0 ||
			((1 << device_to_minor(device)) & fault_devs) != 0) &&
		(((_drbd_fault_random(&rrs) % 100) + 1) <= fault_rate));

	if (ret) {
		fault_count++;

		if (__ratelimit(&drbd_ratelimit_state))
			drbd_warn(device, "***Simulating %s failure\n",
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

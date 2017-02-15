/*
 * Copyright (c) 2002, 2007 Red Hat, Inc. All rights reserved.
 *
 * This software may be freely redistributed under the terms of the
 * GNU General Public License.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 *          David Howells <dhowells@redhat.com>
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/circ_buf.h>
#include <linux/sched.h>
#include "internal.h"

#if 0
unsigned afs_vnode_update_timeout = 10;
#endif  /*  0  */

#define afs_breakring_space(server) \
	CIRC_SPACE((server)->cb_break_head, (server)->cb_break_tail,	\
		   ARRAY_SIZE((server)->cb_break))

//static void afs_callback_updater(struct work_struct *);

static struct workqueue_struct *afs_callback_update_worker;

/*
 * allow the fileserver to request callback state (re-)initialisation
 */
void afs_init_callback_state(struct afs_server *server)
{
	struct afs_vnode *vnode;

	_enter("{%p}", server);

	spin_lock(&server->cb_lock);

	/* kill all the promises on record from this server */
	while (!RB_EMPTY_ROOT(&server->cb_promises)) {
		vnode = rb_entry(server->cb_promises.rb_node,
				 struct afs_vnode, cb_promise);
		_debug("UNPROMISE { vid=%x:%u uq=%u}",
		       vnode->fid.vid, vnode->fid.vnode, vnode->fid.unique);
		rb_erase(&vnode->cb_promise, &server->cb_promises);
		vnode->cb_promised = false;
	}

	spin_unlock(&server->cb_lock);
	_leave("");
}

/*
 * handle the data invalidation side of a callback being broken
 */
void afs_broken_callback_work(struct work_struct *work)
{
	struct afs_vnode *vnode =
		container_of(work, struct afs_vnode, cb_broken_work);

	_enter("");

	if (test_bit(AFS_VNODE_DELETED, &vnode->flags))
		return;

	/* we're only interested in dealing with a broken callback on *this*
	 * vnode and only if no-one else has dealt with it yet */
	if (!mutex_trylock(&vnode->validate_lock))
		return; /* someone else is dealing with it */

	if (test_bit(AFS_VNODE_CB_BROKEN, &vnode->flags)) {
		if (S_ISDIR(vnode->vfs_inode.i_mode))
			afs_clear_permits(vnode);

		if (afs_vnode_fetch_status(vnode, NULL, NULL) < 0)
			goto out;

		if (test_bit(AFS_VNODE_DELETED, &vnode->flags))
			goto out;

		/* if the vnode's data version number changed then its contents
		 * are different */
		if (test_and_clear_bit(AFS_VNODE_ZAP_DATA, &vnode->flags))
			afs_zap_data(vnode);
	}

out:
	mutex_unlock(&vnode->validate_lock);

	/* avoid the potential race whereby the mutex_trylock() in this
	 * function happens again between the clear_bit() and the
	 * mutex_unlock() */
	if (test_bit(AFS_VNODE_CB_BROKEN, &vnode->flags)) {
		_debug("requeue");
		queue_work(afs_callback_update_worker, &vnode->cb_broken_work);
	}
	_leave("");
}

/*
 * actually break a callback
 */
static void afs_break_callback(struct afs_server *server,
			       struct afs_vnode *vnode)
{
	_enter("");

	set_bit(AFS_VNODE_CB_BROKEN, &vnode->flags);

	if (vnode->cb_promised) {
		spin_lock(&vnode->lock);

		_debug("break callback");

		spin_lock(&server->cb_lock);
		if (vnode->cb_promised) {
			rb_erase(&vnode->cb_promise, &server->cb_promises);
			vnode->cb_promised = false;
		}
		spin_unlock(&server->cb_lock);

		queue_work(afs_callback_update_worker, &vnode->cb_broken_work);
		if (list_empty(&vnode->granted_locks) &&
		    !list_empty(&vnode->pending_locks))
			afs_lock_may_be_available(vnode);
		spin_unlock(&vnode->lock);
	}
}

/*
 * allow the fileserver to explicitly break one callback
 * - happens when
 *   - the backing file is changed
 *   - a lock is released
 */
static void afs_break_one_callback(struct afs_server *server,
				   struct afs_fid *fid)
{
	struct afs_vnode *vnode;
	struct rb_node *p;

	_debug("find");
	spin_lock(&server->fs_lock);
	p = server->fs_vnodes.rb_node;
	while (p) {
		vnode = rb_entry(p, struct afs_vnode, server_rb);
		if (fid->vid < vnode->fid.vid)
			p = p->rb_left;
		else if (fid->vid > vnode->fid.vid)
			p = p->rb_right;
		else if (fid->vnode < vnode->fid.vnode)
			p = p->rb_left;
		else if (fid->vnode > vnode->fid.vnode)
			p = p->rb_right;
		else if (fid->unique < vnode->fid.unique)
			p = p->rb_left;
		else if (fid->unique > vnode->fid.unique)
			p = p->rb_right;
		else
			goto found;
	}

	/* not found so we just ignore it (it may have moved to another
	 * server) */
not_available:
	_debug("not avail");
	spin_unlock(&server->fs_lock);
	_leave("");
	return;

found:
	_debug("found");
	ASSERTCMP(server, ==, vnode->server);

	if (!igrab(AFS_VNODE_TO_I(vnode)))
		goto not_available;
	spin_unlock(&server->fs_lock);

	afs_break_callback(server, vnode);
	iput(&vnode->vfs_inode);
	_leave("");
}

/*
 * allow the fileserver to break callback promises
 */
void afs_break_callbacks(struct afs_server *server, size_t count,
			 struct afs_callback callbacks[])
{
	_enter("%p,%zu,", server, count);

	ASSERT(server != NULL);
	ASSERTCMP(count, <=, AFSCBMAX);

	for (; count > 0; callbacks++, count--) {
		_debug("- Fid { vl=%08x n=%u u=%u }  CB { v=%u x=%u t=%u }",
		       callbacks->fid.vid,
		       callbacks->fid.vnode,
		       callbacks->fid.unique,
		       callbacks->version,
		       callbacks->expiry,
		       callbacks->type
		       );
		afs_break_one_callback(server, &callbacks->fid);
	}

	_leave("");
	return;
}

/*
 * record the callback for breaking
 * - the caller must hold server->cb_lock
 */
static void afs_do_give_up_callback(struct afs_server *server,
				    struct afs_vnode *vnode)
{
	struct afs_callback *cb;

	_enter("%p,%p", server, vnode);

	cb = &server->cb_break[server->cb_break_head];
	cb->fid		= vnode->fid;
	cb->version	= vnode->cb_version;
	cb->expiry	= vnode->cb_expiry;
	cb->type	= vnode->cb_type;
	smp_wmb();
	server->cb_break_head =
		(server->cb_break_head + 1) &
		(ARRAY_SIZE(server->cb_break) - 1);

	/* defer the breaking of callbacks to try and collect as many as
	 * possible to ship in one operation */
	switch (atomic_inc_return(&server->cb_break_n)) {
	case 1 ... AFSCBMAX - 1:
		queue_delayed_work(afs_callback_update_worker,
				   &server->cb_break_work, HZ * 2);
		break;
	case AFSCBMAX:
		afs_flush_callback_breaks(server);
		break;
	default:
		break;
	}

	ASSERT(server->cb_promises.rb_node != NULL);
	rb_erase(&vnode->cb_promise, &server->cb_promises);
	vnode->cb_promised = false;
	_leave("");
}

/*
 * discard the callback on a deleted item
 */
void afs_discard_callback_on_delete(struct afs_vnode *vnode)
{
	struct afs_server *server = vnode->server;

	_enter("%d", vnode->cb_promised);

	if (!vnode->cb_promised) {
		_leave(" [not promised]");
		return;
	}

	ASSERT(server != NULL);

	spin_lock(&server->cb_lock);
	if (vnode->cb_promised) {
		ASSERT(server->cb_promises.rb_node != NULL);
		rb_erase(&vnode->cb_promise, &server->cb_promises);
		vnode->cb_promised = false;
	}
	spin_unlock(&server->cb_lock);
	_leave("");
}

/*
 * give up the callback registered for a vnode on the file server when the
 * inode is being cleared
 */
void afs_give_up_callback(struct afs_vnode *vnode)
{
	struct afs_server *server = vnode->server;

	DECLARE_WAITQUEUE(myself, current);

	_enter("%d", vnode->cb_promised);

	_debug("GIVE UP INODE %p", &vnode->vfs_inode);

	if (!vnode->cb_promised) {
		_leave(" [not promised]");
		return;
	}

	ASSERT(server != NULL);

	spin_lock(&server->cb_lock);
	if (vnode->cb_promised && afs_breakring_space(server) == 0) {
		add_wait_queue(&server->cb_break_waitq, &myself);
		for (;;) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			if (!vnode->cb_promised ||
			    afs_breakring_space(server) != 0)
				break;
			spin_unlock(&server->cb_lock);
			schedule();
			spin_lock(&server->cb_lock);
		}
		remove_wait_queue(&server->cb_break_waitq, &myself);
		__set_current_state(TASK_RUNNING);
	}

	/* of course, it's always possible for the server to break this vnode's
	 * callback first... */
	if (vnode->cb_promised)
		afs_do_give_up_callback(server, vnode);

	spin_unlock(&server->cb_lock);
	_leave("");
}

/*
 * dispatch a deferred give up callbacks operation
 */
void afs_dispatch_give_up_callbacks(struct work_struct *work)
{
	struct afs_server *server =
		container_of(work, struct afs_server, cb_break_work.work);

	_enter("");

	/* tell the fileserver to discard the callback promises it has
	 * - in the event of ENOMEM or some other error, we just forget that we
	 *   had callbacks entirely, and the server will call us later to break
	 *   them
	 */
	afs_fs_give_up_callbacks(server, true);
}

/*
 * flush the outstanding callback breaks on a server
 */
void afs_flush_callback_breaks(struct afs_server *server)
{
	mod_delayed_work(afs_callback_update_worker, &server->cb_break_work, 0);
}

#if 0
/*
 * update a bunch of callbacks
 */
static void afs_callback_updater(struct work_struct *work)
{
	struct afs_server *server;
	struct afs_vnode *vnode, *xvnode;
	time_t now;
	long timeout;
	int ret;

	server = container_of(work, struct afs_server, updater);

	_enter("");

	now = get_seconds();

	/* find the first vnode to update */
	spin_lock(&server->cb_lock);
	for (;;) {
		if (RB_EMPTY_ROOT(&server->cb_promises)) {
			spin_unlock(&server->cb_lock);
			_leave(" [nothing]");
			return;
		}

		vnode = rb_entry(rb_first(&server->cb_promises),
				 struct afs_vnode, cb_promise);
		if (atomic_read(&vnode->usage) > 0)
			break;
		rb_erase(&vnode->cb_promise, &server->cb_promises);
		vnode->cb_promised = false;
	}

	timeout = vnode->update_at - now;
	if (timeout > 0) {
		queue_delayed_work(afs_vnode_update_worker,
				   &afs_vnode_update, timeout * HZ);
		spin_unlock(&server->cb_lock);
		_leave(" [nothing]");
		return;
	}

	list_del_init(&vnode->update);
	atomic_inc(&vnode->usage);
	spin_unlock(&server->cb_lock);

	/* we can now perform the update */
	_debug("update %s", vnode->vldb.name);
	vnode->state = AFS_VL_UPDATING;
	vnode->upd_rej_cnt = 0;
	vnode->upd_busy_cnt = 0;

	ret = afs_vnode_update_record(vl, &vldb);
	switch (ret) {
	case 0:
		afs_vnode_apply_update(vl, &vldb);
		vnode->state = AFS_VL_UPDATING;
		break;
	case -ENOMEDIUM:
		vnode->state = AFS_VL_VOLUME_DELETED;
		break;
	default:
		vnode->state = AFS_VL_UNCERTAIN;
		break;
	}

	/* and then reschedule */
	_debug("reschedule");
	vnode->update_at = get_seconds() + afs_vnode_update_timeout;

	spin_lock(&server->cb_lock);

	if (!list_empty(&server->cb_promises)) {
		/* next update in 10 minutes, but wait at least 1 second more
		 * than the newest record already queued so that we don't spam
		 * the VL server suddenly with lots of requests
		 */
		xvnode = list_entry(server->cb_promises.prev,
				    struct afs_vnode, update);
		if (vnode->update_at <= xvnode->update_at)
			vnode->update_at = xvnode->update_at + 1;
		xvnode = list_entry(server->cb_promises.next,
				    struct afs_vnode, update);
		timeout = xvnode->update_at - now;
		if (timeout < 0)
			timeout = 0;
	} else {
		timeout = afs_vnode_update_timeout;
	}

	list_add_tail(&vnode->update, &server->cb_promises);

	_debug("timeout %ld", timeout);
	queue_delayed_work(afs_vnode_update_worker,
			   &afs_vnode_update, timeout * HZ);
	spin_unlock(&server->cb_lock);
	afs_put_vnode(vl);
}
#endif

/*
 * initialise the callback update process
 */
int __init afs_callback_update_init(void)
{
	afs_callback_update_worker = alloc_ordered_workqueue("kafs_callbackd",
							     WQ_MEM_RECLAIM);
	return afs_callback_update_worker ? 0 : -ENOMEM;
}

/*
 * shut down the callback update process
 */
void afs_callback_update_kill(void)
{
	destroy_workqueue(afs_callback_update_worker);
}

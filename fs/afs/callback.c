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

/*
 * Handle invalidation of an mmap'd file.  We invalidate all the PTEs referring
 * to the pages in this file's pagecache, forcing the kernel to go through
 * ->fault() or ->page_mkwrite() - at which point we can handle invalidation
 * more fully.
 */
void afs_invalidate_mmap_work(struct work_struct *work)
{
	struct afs_vnode *vnode = container_of(work, struct afs_vnode, cb_work);

	unmap_mapping_pages(vnode->vfs_inode.i_mapping, 0, 0, false);
}

void afs_server_init_callback_work(struct work_struct *work)
{
	struct afs_server *server = container_of(work, struct afs_server, initcb_work);
	struct afs_vnode *vnode;
	struct afs_cell *cell = server->cell;

	down_read(&cell->fs_open_mmaps_lock);

	list_for_each_entry(vnode, &cell->fs_open_mmaps, cb_mmap_link) {
		if (vnode->cb_server == server) {
			clear_bit(AFS_VNODE_CB_PROMISED, &vnode->flags);
			queue_work(system_unbound_wq, &vnode->cb_work);
		}
	}

	up_read(&cell->fs_open_mmaps_lock);
}

/*
 * Allow the fileserver to request callback state (re-)initialisation.
 * Unfortunately, UUIDs are not guaranteed unique.
 */
void afs_init_callback_state(struct afs_server *server)
{
	rcu_read_lock();
	do {
		server->cb_s_break++;
		atomic_inc(&server->cell->fs_s_break);
		if (!list_empty(&server->cell->fs_open_mmaps))
			queue_work(system_unbound_wq, &server->initcb_work);

	} while ((server = rcu_dereference(server->uuid_next)));
	rcu_read_unlock();
}

/*
 * actually break a callback
 */
void __afs_break_callback(struct afs_vnode *vnode, enum afs_cb_break_reason reason)
{
	_enter("");

	clear_bit(AFS_VNODE_NEW_CONTENT, &vnode->flags);
	if (test_and_clear_bit(AFS_VNODE_CB_PROMISED, &vnode->flags)) {
		vnode->cb_break++;
		vnode->cb_v_break = vnode->volume->cb_v_break;
		afs_clear_permits(vnode);

		if (vnode->lock_state == AFS_VNODE_LOCK_WAITING_FOR_CB)
			afs_lock_may_be_available(vnode);

		if (reason != afs_cb_break_for_deleted &&
		    vnode->status.type == AFS_FTYPE_FILE &&
		    atomic_read(&vnode->cb_nr_mmap))
			queue_work(system_unbound_wq, &vnode->cb_work);

		trace_afs_cb_break(&vnode->fid, vnode->cb_break, reason, true);
	} else {
		trace_afs_cb_break(&vnode->fid, vnode->cb_break, reason, false);
	}
}

void afs_break_callback(struct afs_vnode *vnode, enum afs_cb_break_reason reason)
{
	write_seqlock(&vnode->cb_lock);
	__afs_break_callback(vnode, reason);
	write_sequnlock(&vnode->cb_lock);
}

/*
 * Look up a volume by volume ID under RCU conditions.
 */
static struct afs_volume *afs_lookup_volume_rcu(struct afs_cell *cell,
						afs_volid_t vid)
{
	struct afs_volume *volume = NULL;
	struct rb_node *p;
	int seq = 0;

	do {
		/* Unfortunately, rbtree walking doesn't give reliable results
		 * under just the RCU read lock, so we have to check for
		 * changes.
		 */
		read_seqbegin_or_lock(&cell->volume_lock, &seq);

		p = rcu_dereference_raw(cell->volumes.rb_node);
		while (p) {
			volume = rb_entry(p, struct afs_volume, cell_node);

			if (volume->vid < vid)
				p = rcu_dereference_raw(p->rb_left);
			else if (volume->vid > vid)
				p = rcu_dereference_raw(p->rb_right);
			else
				break;
			volume = NULL;
		}

	} while (need_seqretry(&cell->volume_lock, seq));

	done_seqretry(&cell->volume_lock, seq);
	return volume;
}

/*
 * allow the fileserver to explicitly break one callback
 * - happens when
 *   - the backing file is changed
 *   - a lock is released
 */
static void afs_break_one_callback(struct afs_volume *volume,
				   struct afs_fid *fid)
{
	struct super_block *sb;
	struct afs_vnode *vnode;
	struct inode *inode;

	if (fid->vnode == 0 && fid->unique == 0) {
		/* The callback break applies to an entire volume. */
		write_lock(&volume->cb_v_break_lock);
		volume->cb_v_break++;
		trace_afs_cb_break(fid, volume->cb_v_break,
				   afs_cb_break_for_volume_callback, false);
		write_unlock(&volume->cb_v_break_lock);
		return;
	}

	/* See if we can find a matching inode - even an I_NEW inode needs to
	 * be marked as it can have its callback broken before we finish
	 * setting up the local inode.
	 */
	sb = rcu_dereference(volume->sb);
	if (!sb)
		return;

	inode = find_inode_rcu(sb, fid->vnode, afs_ilookup5_test_by_fid, fid);
	if (inode) {
		vnode = AFS_FS_I(inode);
		afs_break_callback(vnode, afs_cb_break_for_callback);
	} else {
		trace_afs_cb_miss(fid, afs_cb_break_for_callback);
	}
}

static void afs_break_some_callbacks(struct afs_server *server,
				     struct afs_callback_break *cbb,
				     size_t *_count)
{
	struct afs_callback_break *residue = cbb;
	struct afs_volume *volume;
	afs_volid_t vid = cbb->fid.vid;
	size_t i;

	volume = afs_lookup_volume_rcu(server->cell, vid);

	/* TODO: Find all matching volumes if we couldn't match the server and
	 * break them anyway.
	 */

	for (i = *_count; i > 0; cbb++, i--) {
		if (cbb->fid.vid == vid) {
			_debug("- Fid { vl=%08llx n=%llu u=%u }",
			       cbb->fid.vid,
			       cbb->fid.vnode,
			       cbb->fid.unique);
			--*_count;
			if (volume)
				afs_break_one_callback(volume, &cbb->fid);
		} else {
			*residue++ = *cbb;
		}
	}
}

/*
 * allow the fileserver to break callback promises
 */
void afs_break_callbacks(struct afs_server *server, size_t count,
			 struct afs_callback_break *callbacks)
{
	_enter("%p,%zu,", server, count);

	ASSERT(server != NULL);

	rcu_read_lock();

	while (count > 0)
		afs_break_some_callbacks(server, callbacks, &count);

	rcu_read_unlock();
	return;
}

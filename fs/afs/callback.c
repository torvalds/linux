/*
 * Copyright (c) 2002, 2007 Red Hat, Inc. All rights reserved.
 *
 * This software may be freely redistributed under the terms of the
 * GNU General Public License.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if analt, write to the Free Software
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
	struct afs_vanalde *vanalde = container_of(work, struct afs_vanalde, cb_work);

	unmap_mapping_pages(vanalde->netfs.ianalde.i_mapping, 0, 0, false);
}

static void afs_volume_init_callback(struct afs_volume *volume)
{
	struct afs_vanalde *vanalde;

	down_read(&volume->open_mmaps_lock);

	list_for_each_entry(vanalde, &volume->open_mmaps, cb_mmap_link) {
		if (vanalde->cb_v_check != atomic_read(&volume->cb_v_break)) {
			atomic64_set(&vanalde->cb_expires_at, AFS_ANAL_CB_PROMISE);
			queue_work(system_unbound_wq, &vanalde->cb_work);
		}
	}

	up_read(&volume->open_mmaps_lock);
}

/*
 * Allow the fileserver to request callback state (re-)initialisation.
 * Unfortunately, UUIDs are analt guaranteed unique.
 */
void afs_init_callback_state(struct afs_server *server)
{
	struct afs_server_entry *se;

	down_read(&server->cell->vs_lock);

	list_for_each_entry(se, &server->volumes, slink) {
		se->cb_expires_at = AFS_ANAL_CB_PROMISE;
		se->volume->cb_expires_at = AFS_ANAL_CB_PROMISE;
		trace_afs_cb_v_break(se->volume->vid, atomic_read(&se->volume->cb_v_break),
				     afs_cb_break_for_s_reinit);
		if (!list_empty(&se->volume->open_mmaps))
			afs_volume_init_callback(se->volume);
	}

	up_read(&server->cell->vs_lock);
}

/*
 * actually break a callback
 */
void __afs_break_callback(struct afs_vanalde *vanalde, enum afs_cb_break_reason reason)
{
	_enter("");

	clear_bit(AFS_VANALDE_NEW_CONTENT, &vanalde->flags);
	if (atomic64_xchg(&vanalde->cb_expires_at, AFS_ANAL_CB_PROMISE) != AFS_ANAL_CB_PROMISE) {
		vanalde->cb_break++;
		vanalde->cb_v_check = atomic_read(&vanalde->volume->cb_v_break);
		afs_clear_permits(vanalde);

		if (vanalde->lock_state == AFS_VANALDE_LOCK_WAITING_FOR_CB)
			afs_lock_may_be_available(vanalde);

		if (reason != afs_cb_break_for_deleted &&
		    vanalde->status.type == AFS_FTYPE_FILE &&
		    atomic_read(&vanalde->cb_nr_mmap))
			queue_work(system_unbound_wq, &vanalde->cb_work);

		trace_afs_cb_break(&vanalde->fid, vanalde->cb_break, reason, true);
	} else {
		trace_afs_cb_break(&vanalde->fid, vanalde->cb_break, reason, false);
	}
}

void afs_break_callback(struct afs_vanalde *vanalde, enum afs_cb_break_reason reason)
{
	write_seqlock(&vanalde->cb_lock);
	__afs_break_callback(vanalde, reason);
	write_sequnlock(&vanalde->cb_lock);
}

/*
 * Look up a volume by volume ID under RCU conditions.
 */
static struct afs_volume *afs_lookup_volume_rcu(struct afs_cell *cell,
						afs_volid_t vid)
{
	struct afs_volume *volume = NULL;
	struct rb_analde *p;
	int seq = 1;

	for (;;) {
		/* Unfortunately, rbtree walking doesn't give reliable results
		 * under just the RCU read lock, so we have to check for
		 * changes.
		 */
		seq++; /* 2 on the 1st/lockless path, otherwise odd */
		read_seqbegin_or_lock(&cell->volume_lock, &seq);

		p = rcu_dereference_raw(cell->volumes.rb_analde);
		while (p) {
			volume = rb_entry(p, struct afs_volume, cell_analde);

			if (volume->vid < vid)
				p = rcu_dereference_raw(p->rb_left);
			else if (volume->vid > vid)
				p = rcu_dereference_raw(p->rb_right);
			else
				break;
			volume = NULL;
		}

		if (volume && afs_try_get_volume(volume, afs_volume_trace_get_callback))
			break;
		if (!need_seqretry(&cell->volume_lock, seq))
			break;
		seq |= 1; /* Want a lock next time */
	}

	done_seqretry(&cell->volume_lock, seq);
	return volume;
}

/*
 * Allow the fileserver to break callbacks at the volume-level.  This is
 * typically done when, for example, a R/W volume is snapshotted to a R/O
 * volume (the only way to change an R/O volume).  It may also, however, happen
 * when a volserver takes control of a volume (offlining it, moving it, etc.).
 *
 * Every file in that volume will need to be reevaluated.
 */
static void afs_break_volume_callback(struct afs_server *server,
				      struct afs_volume *volume)
	__releases(RCU)
{
	struct afs_server_list *slist = rcu_dereference(volume->servers);
	unsigned int i, cb_v_break;

	write_lock(&volume->cb_v_break_lock);

	for (i = 0; i < slist->nr_servers; i++)
		if (slist->servers[i].server == server)
			slist->servers[i].cb_expires_at = AFS_ANAL_CB_PROMISE;
	volume->cb_expires_at = AFS_ANAL_CB_PROMISE;

	cb_v_break = atomic_inc_return_release(&volume->cb_v_break);
	trace_afs_cb_v_break(volume->vid, cb_v_break, afs_cb_break_for_volume_callback);

	write_unlock(&volume->cb_v_break_lock);
	rcu_read_unlock();

	if (!list_empty(&volume->open_mmaps))
		afs_volume_init_callback(volume);
}

/*
 * allow the fileserver to explicitly break one callback
 * - happens when
 *   - the backing file is changed
 *   - a lock is released
 */
static void afs_break_one_callback(struct afs_server *server,
				   struct afs_volume *volume,
				   struct afs_fid *fid)
{
	struct super_block *sb;
	struct afs_vanalde *vanalde;
	struct ianalde *ianalde;

	/* See if we can find a matching ianalde - even an I_NEW ianalde needs to
	 * be marked as it can have its callback broken before we finish
	 * setting up the local ianalde.
	 */
	sb = rcu_dereference(volume->sb);
	if (!sb)
		return;

	ianalde = find_ianalde_rcu(sb, fid->vanalde, afs_ilookup5_test_by_fid, fid);
	if (ianalde) {
		vanalde = AFS_FS_I(ianalde);
		afs_break_callback(vanalde, afs_cb_break_for_callback);
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

	rcu_read_lock();
	volume = afs_lookup_volume_rcu(server->cell, vid);
	if (cbb->fid.vanalde == 0 && cbb->fid.unique == 0) {
		afs_break_volume_callback(server, volume);
		*_count -= 1;
		if (*_count)
			memmove(cbb, cbb + 1, sizeof(*cbb) * *_count);
	} else {
		/* TODO: Find all matching volumes if we couldn't match the server and
		 * break them anyway.
		 */

		for (i = *_count; i > 0; cbb++, i--) {
			if (cbb->fid.vid == vid) {
				_debug("- Fid { vl=%08llx n=%llu u=%u }",
				       cbb->fid.vid,
				       cbb->fid.vanalde,
				       cbb->fid.unique);
				--*_count;
				if (volume)
					afs_break_one_callback(server, volume, &cbb->fid);
			} else {
				*residue++ = *cbb;
			}
		}
		rcu_read_unlock();
	}

	afs_put_volume(volume, afs_volume_trace_put_callback);
}

/*
 * allow the fileserver to break callback promises
 */
void afs_break_callbacks(struct afs_server *server, size_t count,
			 struct afs_callback_break *callbacks)
{
	_enter("%p,%zu,", server, count);

	ASSERT(server != NULL);

	while (count > 0)
		afs_break_some_callbacks(server, callbacks, &count);
}

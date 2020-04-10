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
 * Create volume and callback interests on a server.
 */
static struct afs_cb_interest *afs_create_interest(struct afs_server *server,
						   struct afs_vnode *vnode)
{
	struct afs_vol_interest *new_vi, *vi;
	struct afs_cb_interest *new;
	struct rb_node *parent, **pp;

	new_vi = kzalloc(sizeof(struct afs_vol_interest), GFP_KERNEL);
	if (!new_vi)
		return NULL;

	new = kzalloc(sizeof(struct afs_cb_interest), GFP_KERNEL);
	if (!new) {
		kfree(new_vi);
		return NULL;
	}

	new_vi->usage = 1;
	new_vi->vid = vnode->volume->vid;
	INIT_HLIST_HEAD(&new_vi->cb_interests);

	refcount_set(&new->usage, 1);
	new->sb = vnode->vfs_inode.i_sb;
	new->server = afs_get_server(server, afs_server_trace_get_new_cbi);
	INIT_HLIST_NODE(&new->cb_vlink);

	write_seqlock(&server->cb_break_lock);

	pp = &server->cb_volumes.rb_node;
	while ((parent = *pp)) {
		vi = rb_entry(parent, struct afs_vol_interest, srv_node);
		if (vi->vid < new_vi->vid) {
			pp = &(*pp)->rb_left;
		} else if (vi->vid > new_vi->vid) {
			pp = &(*pp)->rb_right;
		} else {
			vi->usage++;
			goto found_vi;
		}
	}

	vi = new_vi;
	new_vi = NULL;
	rb_link_node_rcu(&vi->srv_node, parent, pp);
	rb_insert_color(&vi->srv_node, &server->cb_volumes);

found_vi:
	new->vol_interest = vi;
	hlist_add_head(&new->cb_vlink, &vi->cb_interests);

	write_sequnlock(&server->cb_break_lock);
	kfree(new_vi);
	return new;
}

/*
 * Set up an interest-in-callbacks record for a volume on a server and
 * register it with the server.
 * - Called with vnode->io_lock held.
 */
int afs_register_server_cb_interest(struct afs_vnode *vnode,
				    struct afs_server_list *slist,
				    unsigned int index)
{
	struct afs_server_entry *entry = &slist->servers[index];
	struct afs_cb_interest *cbi, *vcbi, *new, *old;
	struct afs_server *server = entry->server;

again:
	vcbi = rcu_dereference_protected(vnode->cb_interest,
					 lockdep_is_held(&vnode->io_lock));
	if (vcbi && likely(vcbi == entry->cb_interest))
		return 0;

	read_lock(&slist->lock);
	cbi = afs_get_cb_interest(entry->cb_interest);
	read_unlock(&slist->lock);

	if (vcbi) {
		if (vcbi == cbi) {
			afs_put_cb_interest(afs_v2net(vnode), cbi);
			return 0;
		}

		/* Use a new interest in the server list for the same server
		 * rather than an old one that's still attached to a vnode.
		 */
		if (cbi && vcbi->server == cbi->server) {
			write_seqlock(&vnode->cb_lock);
			old = rcu_dereference_protected(vnode->cb_interest,
							lockdep_is_held(&vnode->cb_lock.lock));
			rcu_assign_pointer(vnode->cb_interest, cbi);
			write_sequnlock(&vnode->cb_lock);
			afs_put_cb_interest(afs_v2net(vnode), old);
			return 0;
		}

		/* Re-use the one attached to the vnode. */
		if (!cbi && vcbi->server == server) {
			write_lock(&slist->lock);
			if (entry->cb_interest) {
				write_unlock(&slist->lock);
				afs_put_cb_interest(afs_v2net(vnode), cbi);
				goto again;
			}

			entry->cb_interest = cbi;
			write_unlock(&slist->lock);
			return 0;
		}
	}

	if (!cbi) {
		new = afs_create_interest(server, vnode);
		if (!new)
			return -ENOMEM;

		write_lock(&slist->lock);
		if (!entry->cb_interest) {
			entry->cb_interest = afs_get_cb_interest(new);
			cbi = new;
			new = NULL;
		} else {
			cbi = afs_get_cb_interest(entry->cb_interest);
		}
		write_unlock(&slist->lock);
		afs_put_cb_interest(afs_v2net(vnode), new);
	}

	ASSERT(cbi);

	/* Change the server the vnode is using.  This entails scrubbing any
	 * interest the vnode had in the previous server it was using.
	 */
	write_seqlock(&vnode->cb_lock);

	old = rcu_dereference_protected(vnode->cb_interest,
					lockdep_is_held(&vnode->cb_lock.lock));
	rcu_assign_pointer(vnode->cb_interest, cbi);
	vnode->cb_s_break = cbi->server->cb_s_break;
	vnode->cb_v_break = vnode->volume->cb_v_break;
	clear_bit(AFS_VNODE_CB_PROMISED, &vnode->flags);

	write_sequnlock(&vnode->cb_lock);
	afs_put_cb_interest(afs_v2net(vnode), old);
	return 0;
}

/*
 * Remove an interest on a server.
 */
void afs_put_cb_interest(struct afs_net *net, struct afs_cb_interest *cbi)
{
	struct afs_vol_interest *vi;

	if (cbi && refcount_dec_and_test(&cbi->usage)) {
		if (!hlist_unhashed(&cbi->cb_vlink)) {
			write_seqlock(&cbi->server->cb_break_lock);

			hlist_del_init(&cbi->cb_vlink);
			vi = cbi->vol_interest;
			cbi->vol_interest = NULL;
			if (--vi->usage == 0)
				rb_erase(&vi->srv_node, &cbi->server->cb_volumes);
			else
				vi = NULL;

			write_sequnlock(&cbi->server->cb_break_lock);
			if (vi)
				kfree_rcu(vi, rcu);
			afs_put_server(net, cbi->server, afs_server_trace_put_cbi);
		}
		kfree_rcu(cbi, rcu);
	}
}

/*
 * allow the fileserver to request callback state (re-)initialisation
 */
void afs_init_callback_state(struct afs_server *server)
{
	server->cb_s_break++;
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
		afs_clear_permits(vnode);

		if (vnode->lock_state == AFS_VNODE_LOCK_WAITING_FOR_CB)
			afs_lock_may_be_available(vnode);

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
 * Look up a volume interest by volume ID under RCU conditions.
 */
static struct afs_vol_interest *afs_lookup_vol_interest_rcu(struct afs_server *server,
							    afs_volid_t vid)
{
	struct afs_vol_interest *vi = NULL;
	struct rb_node *p;
	int seq = 0;

	do {
		/* Unfortunately, rbtree walking doesn't give reliable results
		 * under just the RCU read lock, so we have to check for
		 * changes.
		 */
		read_seqbegin_or_lock(&server->cb_break_lock, &seq);

		p = rcu_dereference_raw(server->cb_volumes.rb_node);
		while (p) {
			vi = rb_entry(p, struct afs_vol_interest, srv_node);

			if (vi->vid < vid)
				p = rcu_dereference_raw(p->rb_left);
			else if (vi->vid > vid)
				p = rcu_dereference_raw(p->rb_right);
			else
				break;
			/* We want to repeat the search, this time with the
			 * lock properly locked.
			 */
			vi = NULL;
		}

	} while (need_seqretry(&server->cb_break_lock, seq));

	done_seqretry(&server->cb_break_lock, seq);
	return vi;
}

/*
 * allow the fileserver to explicitly break one callback
 * - happens when
 *   - the backing file is changed
 *   - a lock is released
 */
static void afs_break_one_callback(struct afs_server *server,
				   struct afs_fid *fid,
				   struct afs_vol_interest *vi)
{
	struct afs_cb_interest *cbi;
	struct afs_vnode *vnode;
	struct inode *inode;

	/* Step through all interested superblocks.  There may be more than one
	 * because of cell aliasing.
	 */
	hlist_for_each_entry_rcu(cbi, &vi->cb_interests, cb_vlink) {
		if (fid->vnode == 0 && fid->unique == 0) {
			/* The callback break applies to an entire volume. */
			struct afs_super_info *as = AFS_FS_S(cbi->sb);
			struct afs_volume *volume = as->volume;

			write_lock(&volume->cb_v_break_lock);
			volume->cb_v_break++;
			trace_afs_cb_break(fid, volume->cb_v_break,
					   afs_cb_break_for_volume_callback, false);
			write_unlock(&volume->cb_v_break_lock);
		} else {
			/* See if we can find a matching inode - even an I_NEW
			 * inode needs to be marked as it can have its callback
			 * broken before we finish setting up the local inode.
			 */
			inode = find_inode_rcu(cbi->sb, fid->vnode,
					       afs_ilookup5_test_by_fid, fid);
			if (inode) {
				vnode = AFS_FS_I(inode);
				afs_break_callback(vnode, afs_cb_break_for_callback);
			} else {
				trace_afs_cb_miss(fid, afs_cb_break_for_callback);
			}
		}
	}
}

static void afs_break_some_callbacks(struct afs_server *server,
				     struct afs_callback_break *cbb,
				     size_t *_count)
{
	struct afs_callback_break *residue = cbb;
	struct afs_vol_interest *vi;
	afs_volid_t vid = cbb->fid.vid;
	size_t i;

	vi = afs_lookup_vol_interest_rcu(server, vid);

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
			if (vi)
				afs_break_one_callback(server, &cbb->fid, vi);
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

/*
 * Clear the callback interests in a server list.
 */
void afs_clear_callback_interests(struct afs_net *net, struct afs_server_list *slist)
{
	int i;

	for (i = 0; i < slist->nr_servers; i++) {
		afs_put_cb_interest(net, slist->servers[i].cb_interest);
		slist->servers[i].cb_interest = NULL;
	}
}

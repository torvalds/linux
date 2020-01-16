/*
 * Copyright (c) 2002 Red Hat, Inc. All rights reserved.
 *
 * This software may be freely redistributed under the terms of the
 * GNU General Public License.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if yest, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 *          David Howells <dhowells@redhat.com>
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/sched.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/iversion.h>
#include "internal.h"
#include "afs_fs.h"

static const struct iyesde_operations afs_symlink_iyesde_operations = {
	.get_link	= page_get_link,
	.listxattr	= afs_listxattr,
};

static yesinline void dump_vyesde(struct afs_vyesde *vyesde, struct afs_vyesde *parent_vyesde)
{
	static unsigned long once_only;

	pr_warn("kAFS: AFS vyesde with undefined type %u\n", vyesde->status.type);
	pr_warn("kAFS: A=%d m=%o s=%llx v=%llx\n",
		vyesde->status.abort_code,
		vyesde->status.mode,
		vyesde->status.size,
		vyesde->status.data_version);
	pr_warn("kAFS: vyesde %llx:%llx:%x\n",
		vyesde->fid.vid,
		vyesde->fid.vyesde,
		vyesde->fid.unique);
	if (parent_vyesde)
		pr_warn("kAFS: dir %llx:%llx:%x\n",
			parent_vyesde->fid.vid,
			parent_vyesde->fid.vyesde,
			parent_vyesde->fid.unique);

	if (!test_and_set_bit(0, &once_only))
		dump_stack();
}

/*
 * Set the file size and block count.  Estimate the number of 512 bytes blocks
 * used, rounded up to nearest 1K for consistency with other AFS clients.
 */
static void afs_set_i_size(struct afs_vyesde *vyesde, u64 size)
{
	i_size_write(&vyesde->vfs_iyesde, size);
	vyesde->vfs_iyesde.i_blocks = ((size + 1023) >> 10) << 1;
}

/*
 * Initialise an iyesde from the vyesde status.
 */
static int afs_iyesde_init_from_status(struct afs_vyesde *vyesde, struct key *key,
				      struct afs_cb_interest *cbi,
				      struct afs_vyesde *parent_vyesde,
				      struct afs_status_cb *scb)
{
	struct afs_cb_interest *old_cbi = NULL;
	struct afs_file_status *status = &scb->status;
	struct iyesde *iyesde = AFS_VNODE_TO_I(vyesde);
	struct timespec64 t;

	_debug("FS: ft=%d lk=%d sz=%llu ver=%Lu mod=%hu",
	       status->type,
	       status->nlink,
	       (unsigned long long) status->size,
	       status->data_version,
	       status->mode);

	write_seqlock(&vyesde->cb_lock);

	vyesde->status = *status;

	t = status->mtime_client;
	iyesde->i_ctime = t;
	iyesde->i_mtime = t;
	iyesde->i_atime = t;
	iyesde->i_uid = make_kuid(&init_user_ns, status->owner);
	iyesde->i_gid = make_kgid(&init_user_ns, status->group);
	set_nlink(&vyesde->vfs_iyesde, status->nlink);

	switch (status->type) {
	case AFS_FTYPE_FILE:
		iyesde->i_mode	= S_IFREG | status->mode;
		iyesde->i_op	= &afs_file_iyesde_operations;
		iyesde->i_fop	= &afs_file_operations;
		iyesde->i_mapping->a_ops	= &afs_fs_aops;
		break;
	case AFS_FTYPE_DIR:
		iyesde->i_mode	= S_IFDIR | status->mode;
		iyesde->i_op	= &afs_dir_iyesde_operations;
		iyesde->i_fop	= &afs_dir_file_operations;
		iyesde->i_mapping->a_ops	= &afs_dir_aops;
		break;
	case AFS_FTYPE_SYMLINK:
		/* Symlinks with a mode of 0644 are actually mountpoints. */
		if ((status->mode & 0777) == 0644) {
			iyesde->i_flags |= S_AUTOMOUNT;

			set_bit(AFS_VNODE_MOUNTPOINT, &vyesde->flags);

			iyesde->i_mode	= S_IFDIR | 0555;
			iyesde->i_op	= &afs_mntpt_iyesde_operations;
			iyesde->i_fop	= &afs_mntpt_file_operations;
			iyesde->i_mapping->a_ops	= &afs_fs_aops;
		} else {
			iyesde->i_mode	= S_IFLNK | status->mode;
			iyesde->i_op	= &afs_symlink_iyesde_operations;
			iyesde->i_mapping->a_ops	= &afs_fs_aops;
		}
		iyesde_yeshighmem(iyesde);
		break;
	default:
		dump_vyesde(vyesde, parent_vyesde);
		write_sequnlock(&vyesde->cb_lock);
		return afs_protocol_error(NULL, -EBADMSG, afs_eproto_file_type);
	}

	afs_set_i_size(vyesde, status->size);

	vyesde->invalid_before	= status->data_version;
	iyesde_set_iversion_raw(&vyesde->vfs_iyesde, status->data_version);

	if (!scb->have_cb) {
		/* it's a symlink we just created (the fileserver
		 * didn't give us a callback) */
		vyesde->cb_expires_at = ktime_get_real_seconds();
	} else {
		vyesde->cb_expires_at = scb->callback.expires_at;
		old_cbi = rcu_dereference_protected(vyesde->cb_interest,
						    lockdep_is_held(&vyesde->cb_lock.lock));
		if (cbi != old_cbi)
			rcu_assign_pointer(vyesde->cb_interest, afs_get_cb_interest(cbi));
		else
			old_cbi = NULL;
		set_bit(AFS_VNODE_CB_PROMISED, &vyesde->flags);
	}

	write_sequnlock(&vyesde->cb_lock);
	afs_put_cb_interest(afs_v2net(vyesde), old_cbi);
	return 0;
}

/*
 * Update the core iyesde struct from a returned status record.
 */
static void afs_apply_status(struct afs_fs_cursor *fc,
			     struct afs_vyesde *vyesde,
			     struct afs_status_cb *scb,
			     const afs_dataversion_t *expected_version)
{
	struct afs_file_status *status = &scb->status;
	struct timespec64 t;
	umode_t mode;
	bool data_changed = false;

	BUG_ON(test_bit(AFS_VNODE_UNSET, &vyesde->flags));

	if (status->type != vyesde->status.type) {
		pr_warn("Vyesde %llx:%llx:%x changed type %u to %u\n",
			vyesde->fid.vid,
			vyesde->fid.vyesde,
			vyesde->fid.unique,
			status->type, vyesde->status.type);
		afs_protocol_error(NULL, -EBADMSG, afs_eproto_bad_status);
		return;
	}

	if (status->nlink != vyesde->status.nlink)
		set_nlink(&vyesde->vfs_iyesde, status->nlink);

	if (status->owner != vyesde->status.owner)
		vyesde->vfs_iyesde.i_uid = make_kuid(&init_user_ns, status->owner);

	if (status->group != vyesde->status.group)
		vyesde->vfs_iyesde.i_gid = make_kgid(&init_user_ns, status->group);

	if (status->mode != vyesde->status.mode) {
		mode = vyesde->vfs_iyesde.i_mode;
		mode &= ~S_IALLUGO;
		mode |= status->mode;
		WRITE_ONCE(vyesde->vfs_iyesde.i_mode, mode);
	}

	t = status->mtime_client;
	vyesde->vfs_iyesde.i_ctime = t;
	vyesde->vfs_iyesde.i_mtime = t;
	vyesde->vfs_iyesde.i_atime = t;

	if (vyesde->status.data_version != status->data_version)
		data_changed = true;

	vyesde->status = *status;

	if (expected_version &&
	    *expected_version != status->data_version) {
		if (test_bit(AFS_VNODE_CB_PROMISED, &vyesde->flags))
			pr_warn("kAFS: vyesde modified {%llx:%llu} %llx->%llx %s\n",
				vyesde->fid.vid, vyesde->fid.vyesde,
				(unsigned long long)*expected_version,
				(unsigned long long)status->data_version,
				fc->type ? fc->type->name : "???");

		vyesde->invalid_before = status->data_version;
		if (vyesde->status.type == AFS_FTYPE_DIR) {
			if (test_and_clear_bit(AFS_VNODE_DIR_VALID, &vyesde->flags))
				afs_stat_v(vyesde, n_inval);
		} else {
			set_bit(AFS_VNODE_ZAP_DATA, &vyesde->flags);
		}
	} else if (vyesde->status.type == AFS_FTYPE_DIR) {
		/* Expected directory change is handled elsewhere so
		 * that we can locally edit the directory and save on a
		 * download.
		 */
		if (test_bit(AFS_VNODE_DIR_VALID, &vyesde->flags))
			data_changed = false;
	}

	if (data_changed) {
		iyesde_set_iversion_raw(&vyesde->vfs_iyesde, status->data_version);
		afs_set_i_size(vyesde, status->size);
	}
}

/*
 * Apply a callback to a vyesde.
 */
static void afs_apply_callback(struct afs_fs_cursor *fc,
			       struct afs_vyesde *vyesde,
			       struct afs_status_cb *scb,
			       unsigned int cb_break)
{
	struct afs_cb_interest *old;
	struct afs_callback *cb = &scb->callback;

	if (!afs_cb_is_broken(cb_break, vyesde, fc->cbi)) {
		vyesde->cb_expires_at	= cb->expires_at;
		old = rcu_dereference_protected(vyesde->cb_interest,
						lockdep_is_held(&vyesde->cb_lock.lock));
		if (old != fc->cbi) {
			rcu_assign_pointer(vyesde->cb_interest, afs_get_cb_interest(fc->cbi));
			afs_put_cb_interest(afs_v2net(vyesde), old);
		}
		set_bit(AFS_VNODE_CB_PROMISED, &vyesde->flags);
	}
}

/*
 * Apply the received status and callback to an iyesde all in the same critical
 * section to avoid races with afs_validate().
 */
void afs_vyesde_commit_status(struct afs_fs_cursor *fc,
			     struct afs_vyesde *vyesde,
			     unsigned int cb_break,
			     const afs_dataversion_t *expected_version,
			     struct afs_status_cb *scb)
{
	if (fc->ac.error != 0)
		return;

	write_seqlock(&vyesde->cb_lock);

	if (scb->have_error) {
		if (scb->status.abort_code == VNOVNODE) {
			set_bit(AFS_VNODE_DELETED, &vyesde->flags);
			clear_nlink(&vyesde->vfs_iyesde);
			__afs_break_callback(vyesde, afs_cb_break_for_deleted);
		}
	} else {
		if (scb->have_status)
			afs_apply_status(fc, vyesde, scb, expected_version);
		if (scb->have_cb)
			afs_apply_callback(fc, vyesde, scb, cb_break);
	}

	write_sequnlock(&vyesde->cb_lock);

	if (fc->ac.error == 0 && scb->have_status)
		afs_cache_permit(vyesde, fc->key, cb_break, scb);
}

/*
 * Fetch file status from the volume.
 */
int afs_fetch_status(struct afs_vyesde *vyesde, struct key *key, bool is_new,
		     afs_access_t *_caller_access)
{
	struct afs_status_cb *scb;
	struct afs_fs_cursor fc;
	int ret;

	_enter("%s,{%llx:%llu.%u,S=%lx}",
	       vyesde->volume->name,
	       vyesde->fid.vid, vyesde->fid.vyesde, vyesde->fid.unique,
	       vyesde->flags);

	scb = kzalloc(sizeof(struct afs_status_cb), GFP_KERNEL);
	if (!scb)
		return -ENOMEM;

	ret = -ERESTARTSYS;
	if (afs_begin_vyesde_operation(&fc, vyesde, key, true)) {
		afs_dataversion_t data_version = vyesde->status.data_version;

		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vyesde_cb_break(vyesde);
			afs_fs_fetch_file_status(&fc, scb, NULL);
		}

		if (fc.error) {
			/* Do yesthing. */
		} else if (is_new) {
			ret = afs_iyesde_init_from_status(vyesde, key, fc.cbi,
							 NULL, scb);
			fc.error = ret;
			if (ret == 0)
				afs_cache_permit(vyesde, key, fc.cb_break, scb);
		} else {
			afs_vyesde_commit_status(&fc, vyesde, fc.cb_break,
						&data_version, scb);
		}
		afs_check_for_remote_deletion(&fc, vyesde);
		ret = afs_end_vyesde_operation(&fc);
	}

	if (ret == 0 && _caller_access)
		*_caller_access = scb->status.caller_access;
	kfree(scb);
	_leave(" = %d", ret);
	return ret;
}

/*
 * iget5() comparator
 */
int afs_iget5_test(struct iyesde *iyesde, void *opaque)
{
	struct afs_iget_data *iget_data = opaque;
	struct afs_vyesde *vyesde = AFS_FS_I(iyesde);

	return memcmp(&vyesde->fid, &iget_data->fid, sizeof(iget_data->fid)) == 0;
}

/*
 * iget5() comparator for iyesde created by autocell operations
 *
 * These pseudo iyesdes don't match anything.
 */
static int afs_iget5_pseudo_dir_test(struct iyesde *iyesde, void *opaque)
{
	return 0;
}

/*
 * iget5() iyesde initialiser
 */
static int afs_iget5_set(struct iyesde *iyesde, void *opaque)
{
	struct afs_iget_data *iget_data = opaque;
	struct afs_vyesde *vyesde = AFS_FS_I(iyesde);

	vyesde->fid		= iget_data->fid;
	vyesde->volume		= iget_data->volume;
	vyesde->cb_v_break	= iget_data->cb_v_break;
	vyesde->cb_s_break	= iget_data->cb_s_break;

	/* YFS supports 96-bit vyesde IDs, but Linux only supports
	 * 64-bit iyesde numbers.
	 */
	iyesde->i_iyes		= iget_data->fid.vyesde;
	iyesde->i_generation	= iget_data->fid.unique;
	return 0;
}

/*
 * Create an iyesde for a dynamic root directory or an autocell dynamic
 * automount dir.
 */
struct iyesde *afs_iget_pseudo_dir(struct super_block *sb, bool root)
{
	struct afs_super_info *as;
	struct afs_vyesde *vyesde;
	struct iyesde *iyesde;
	static atomic_t afs_autocell_iyes;

	struct afs_iget_data iget_data = {
		.cb_v_break = 0,
		.cb_s_break = 0,
	};

	_enter("");

	as = sb->s_fs_info;
	if (as->volume) {
		iget_data.volume = as->volume;
		iget_data.fid.vid = as->volume->vid;
	}
	if (root) {
		iget_data.fid.vyesde = 1;
		iget_data.fid.unique = 1;
	} else {
		iget_data.fid.vyesde = atomic_inc_return(&afs_autocell_iyes);
		iget_data.fid.unique = 0;
	}

	iyesde = iget5_locked(sb, iget_data.fid.vyesde,
			     afs_iget5_pseudo_dir_test, afs_iget5_set,
			     &iget_data);
	if (!iyesde) {
		_leave(" = -ENOMEM");
		return ERR_PTR(-ENOMEM);
	}

	_debug("GOT INODE %p { iyes=%lu, vl=%llx, vn=%llx, u=%x }",
	       iyesde, iyesde->i_iyes, iget_data.fid.vid, iget_data.fid.vyesde,
	       iget_data.fid.unique);

	vyesde = AFS_FS_I(iyesde);

	/* there shouldn't be an existing iyesde */
	BUG_ON(!(iyesde->i_state & I_NEW));

	iyesde->i_size		= 0;
	iyesde->i_mode		= S_IFDIR | S_IRUGO | S_IXUGO;
	if (root) {
		iyesde->i_op	= &afs_dynroot_iyesde_operations;
		iyesde->i_fop	= &simple_dir_operations;
	} else {
		iyesde->i_op	= &afs_autocell_iyesde_operations;
	}
	set_nlink(iyesde, 2);
	iyesde->i_uid		= GLOBAL_ROOT_UID;
	iyesde->i_gid		= GLOBAL_ROOT_GID;
	iyesde->i_ctime = iyesde->i_atime = iyesde->i_mtime = current_time(iyesde);
	iyesde->i_blocks		= 0;
	iyesde_set_iversion_raw(iyesde, 0);
	iyesde->i_generation	= 0;

	set_bit(AFS_VNODE_PSEUDODIR, &vyesde->flags);
	if (!root) {
		set_bit(AFS_VNODE_MOUNTPOINT, &vyesde->flags);
		iyesde->i_flags |= S_AUTOMOUNT;
	}

	iyesde->i_flags |= S_NOATIME;
	unlock_new_iyesde(iyesde);
	_leave(" = %p", iyesde);
	return iyesde;
}

/*
 * Get a cache cookie for an iyesde.
 */
static void afs_get_iyesde_cache(struct afs_vyesde *vyesde)
{
#ifdef CONFIG_AFS_FSCACHE
	struct {
		u32 vyesde_id;
		u32 unique;
		u32 vyesde_id_ext[2];	/* Allow for a 96-bit key */
	} __packed key;
	struct afs_vyesde_cache_aux aux;

	if (vyesde->status.type == AFS_FTYPE_DIR) {
		vyesde->cache = NULL;
		return;
	}

	key.vyesde_id		= vyesde->fid.vyesde;
	key.unique		= vyesde->fid.unique;
	key.vyesde_id_ext[0]	= vyesde->fid.vyesde >> 32;
	key.vyesde_id_ext[1]	= vyesde->fid.vyesde_hi;
	aux.data_version	= vyesde->status.data_version;

	vyesde->cache = fscache_acquire_cookie(vyesde->volume->cache,
					      &afs_vyesde_cache_index_def,
					      &key, sizeof(key),
					      &aux, sizeof(aux),
					      vyesde, vyesde->status.size, true);
#endif
}

/*
 * iyesde retrieval
 */
struct iyesde *afs_iget(struct super_block *sb, struct key *key,
		       struct afs_iget_data *iget_data,
		       struct afs_status_cb *scb,
		       struct afs_cb_interest *cbi,
		       struct afs_vyesde *parent_vyesde)
{
	struct afs_super_info *as;
	struct afs_vyesde *vyesde;
	struct afs_fid *fid = &iget_data->fid;
	struct iyesde *iyesde;
	int ret;

	_enter(",{%llx:%llu.%u},,", fid->vid, fid->vyesde, fid->unique);

	as = sb->s_fs_info;
	iget_data->volume = as->volume;

	iyesde = iget5_locked(sb, fid->vyesde, afs_iget5_test, afs_iget5_set,
			     iget_data);
	if (!iyesde) {
		_leave(" = -ENOMEM");
		return ERR_PTR(-ENOMEM);
	}

	_debug("GOT INODE %p { vl=%llx vn=%llx, u=%x }",
	       iyesde, fid->vid, fid->vyesde, fid->unique);

	vyesde = AFS_FS_I(iyesde);

	/* deal with an existing iyesde */
	if (!(iyesde->i_state & I_NEW)) {
		_leave(" = %p", iyesde);
		return iyesde;
	}

	if (!scb) {
		/* it's a remotely extant iyesde */
		ret = afs_fetch_status(vyesde, key, true, NULL);
		if (ret < 0)
			goto bad_iyesde;
	} else {
		ret = afs_iyesde_init_from_status(vyesde, key, cbi, parent_vyesde,
						 scb);
		if (ret < 0)
			goto bad_iyesde;
	}

	afs_get_iyesde_cache(vyesde);

	/* success */
	clear_bit(AFS_VNODE_UNSET, &vyesde->flags);
	iyesde->i_flags |= S_NOATIME;
	unlock_new_iyesde(iyesde);
	_leave(" = %p", iyesde);
	return iyesde;

	/* failure */
bad_iyesde:
	iget_failed(iyesde);
	_leave(" = %d [bad]", ret);
	return ERR_PTR(ret);
}

/*
 * mark the data attached to an iyesde as obsolete due to a write on the server
 * - might also want to ditch all the outstanding writes and dirty pages
 */
void afs_zap_data(struct afs_vyesde *vyesde)
{
	_enter("{%llx:%llu}", vyesde->fid.vid, vyesde->fid.vyesde);

#ifdef CONFIG_AFS_FSCACHE
	fscache_invalidate(vyesde->cache);
#endif

	/* nuke all the yesn-dirty pages that aren't locked, mapped or being
	 * written back in a regular file and completely discard the pages in a
	 * directory or symlink */
	if (S_ISREG(vyesde->vfs_iyesde.i_mode))
		invalidate_remote_iyesde(&vyesde->vfs_iyesde);
	else
		invalidate_iyesde_pages2(vyesde->vfs_iyesde.i_mapping);
}

/*
 * Check the validity of a vyesde/iyesde.
 */
bool afs_check_validity(struct afs_vyesde *vyesde)
{
	struct afs_cb_interest *cbi;
	struct afs_server *server;
	struct afs_volume *volume = vyesde->volume;
	enum afs_cb_break_reason need_clear = afs_cb_break_yes_break;
	time64_t yesw = ktime_get_real_seconds();
	bool valid;
	unsigned int cb_break, cb_s_break, cb_v_break;
	int seq = 0;

	do {
		read_seqbegin_or_lock(&vyesde->cb_lock, &seq);
		cb_v_break = READ_ONCE(volume->cb_v_break);
		cb_break = vyesde->cb_break;

		if (test_bit(AFS_VNODE_CB_PROMISED, &vyesde->flags)) {
			cbi = rcu_dereference(vyesde->cb_interest);
			server = rcu_dereference(cbi->server);
			cb_s_break = READ_ONCE(server->cb_s_break);

			if (vyesde->cb_s_break != cb_s_break ||
			    vyesde->cb_v_break != cb_v_break) {
				vyesde->cb_s_break = cb_s_break;
				vyesde->cb_v_break = cb_v_break;
				need_clear = afs_cb_break_for_vsbreak;
				valid = false;
			} else if (test_bit(AFS_VNODE_ZAP_DATA, &vyesde->flags)) {
				need_clear = afs_cb_break_for_zap;
				valid = false;
			} else if (vyesde->cb_expires_at - 10 <= yesw) {
				need_clear = afs_cb_break_for_lapsed;
				valid = false;
			} else {
				valid = true;
			}
		} else if (test_bit(AFS_VNODE_DELETED, &vyesde->flags)) {
			valid = true;
		} else {
			vyesde->cb_v_break = cb_v_break;
			valid = false;
		}

	} while (need_seqretry(&vyesde->cb_lock, seq));

	done_seqretry(&vyesde->cb_lock, seq);

	if (need_clear != afs_cb_break_yes_break) {
		write_seqlock(&vyesde->cb_lock);
		if (cb_break == vyesde->cb_break)
			__afs_break_callback(vyesde, need_clear);
		else
			trace_afs_cb_miss(&vyesde->fid, need_clear);
		write_sequnlock(&vyesde->cb_lock);
		valid = false;
	}

	return valid;
}

/*
 * validate a vyesde/iyesde
 * - there are several things we need to check
 *   - parent dir data changes (rm, rmdir, rename, mkdir, create, link,
 *     symlink)
 *   - parent dir metadata changed (security changes)
 *   - dentry data changed (write, truncate)
 *   - dentry metadata changed (security changes)
 */
int afs_validate(struct afs_vyesde *vyesde, struct key *key)
{
	bool valid;
	int ret;

	_enter("{v={%llx:%llu} fl=%lx},%x",
	       vyesde->fid.vid, vyesde->fid.vyesde, vyesde->flags,
	       key_serial(key));

	rcu_read_lock();
	valid = afs_check_validity(vyesde);
	rcu_read_unlock();

	if (test_bit(AFS_VNODE_DELETED, &vyesde->flags))
		clear_nlink(&vyesde->vfs_iyesde);

	if (valid)
		goto valid;

	down_write(&vyesde->validate_lock);

	/* if the promise has expired, we need to check the server again to get
	 * a new promise - yeste that if the (parent) directory's metadata was
	 * changed then the security may be different and we may yes longer have
	 * access */
	if (!test_bit(AFS_VNODE_CB_PROMISED, &vyesde->flags)) {
		_debug("yest promised");
		ret = afs_fetch_status(vyesde, key, false, NULL);
		if (ret < 0) {
			if (ret == -ENOENT) {
				set_bit(AFS_VNODE_DELETED, &vyesde->flags);
				ret = -ESTALE;
			}
			goto error_unlock;
		}
		_debug("new promise [fl=%lx]", vyesde->flags);
	}

	if (test_bit(AFS_VNODE_DELETED, &vyesde->flags)) {
		_debug("file already deleted");
		ret = -ESTALE;
		goto error_unlock;
	}

	/* if the vyesde's data version number changed then its contents are
	 * different */
	if (test_and_clear_bit(AFS_VNODE_ZAP_DATA, &vyesde->flags))
		afs_zap_data(vyesde);
	up_write(&vyesde->validate_lock);
valid:
	_leave(" = 0");
	return 0;

error_unlock:
	up_write(&vyesde->validate_lock);
	_leave(" = %d", ret);
	return ret;
}

/*
 * read the attributes of an iyesde
 */
int afs_getattr(const struct path *path, struct kstat *stat,
		u32 request_mask, unsigned int query_flags)
{
	struct iyesde *iyesde = d_iyesde(path->dentry);
	struct afs_vyesde *vyesde = AFS_FS_I(iyesde);
	int seq = 0;

	_enter("{ iyes=%lu v=%u }", iyesde->i_iyes, iyesde->i_generation);

	do {
		read_seqbegin_or_lock(&vyesde->cb_lock, &seq);
		generic_fillattr(iyesde, stat);
	} while (need_seqretry(&vyesde->cb_lock, seq));

	done_seqretry(&vyesde->cb_lock, seq);
	return 0;
}

/*
 * discard an AFS iyesde
 */
int afs_drop_iyesde(struct iyesde *iyesde)
{
	_enter("");

	if (test_bit(AFS_VNODE_PSEUDODIR, &AFS_FS_I(iyesde)->flags))
		return generic_delete_iyesde(iyesde);
	else
		return generic_drop_iyesde(iyesde);
}

/*
 * clear an AFS iyesde
 */
void afs_evict_iyesde(struct iyesde *iyesde)
{
	struct afs_cb_interest *cbi;
	struct afs_vyesde *vyesde;

	vyesde = AFS_FS_I(iyesde);

	_enter("{%llx:%llu.%d}",
	       vyesde->fid.vid,
	       vyesde->fid.vyesde,
	       vyesde->fid.unique);

	_debug("CLEAR INODE %p", iyesde);

	ASSERTCMP(iyesde->i_iyes, ==, vyesde->fid.vyesde);

	truncate_iyesde_pages_final(&iyesde->i_data);
	clear_iyesde(iyesde);

	write_seqlock(&vyesde->cb_lock);
	cbi = rcu_dereference_protected(vyesde->cb_interest,
					lockdep_is_held(&vyesde->cb_lock.lock));
	if (cbi) {
		afs_put_cb_interest(afs_i2net(iyesde), cbi);
		rcu_assign_pointer(vyesde->cb_interest, NULL);
	}
	write_sequnlock(&vyesde->cb_lock);

	while (!list_empty(&vyesde->wb_keys)) {
		struct afs_wb_key *wbk = list_entry(vyesde->wb_keys.next,
						    struct afs_wb_key, vyesde_link);
		list_del(&wbk->vyesde_link);
		afs_put_wb_key(wbk);
	}

#ifdef CONFIG_AFS_FSCACHE
	{
		struct afs_vyesde_cache_aux aux;

		aux.data_version = vyesde->status.data_version;
		fscache_relinquish_cookie(vyesde->cache, &aux,
					  test_bit(AFS_VNODE_DELETED, &vyesde->flags));
		vyesde->cache = NULL;
	}
#endif

	afs_prune_wb_keys(vyesde);
	afs_put_permits(rcu_access_pointer(vyesde->permit_cache));
	key_put(vyesde->silly_key);
	vyesde->silly_key = NULL;
	key_put(vyesde->lock_key);
	vyesde->lock_key = NULL;
	_leave("");
}

/*
 * set the attributes of an iyesde
 */
int afs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct afs_fs_cursor fc;
	struct afs_status_cb *scb;
	struct afs_vyesde *vyesde = AFS_FS_I(d_iyesde(dentry));
	struct key *key;
	int ret = -ENOMEM;

	_enter("{%llx:%llu},{n=%pd},%x",
	       vyesde->fid.vid, vyesde->fid.vyesde, dentry,
	       attr->ia_valid);

	if (!(attr->ia_valid & (ATTR_SIZE | ATTR_MODE | ATTR_UID | ATTR_GID |
				ATTR_MTIME))) {
		_leave(" = 0 [unsupported]");
		return 0;
	}

	scb = kzalloc(sizeof(struct afs_status_cb), GFP_KERNEL);
	if (!scb)
		goto error;

	/* flush any dirty data outstanding on a regular file */
	if (S_ISREG(vyesde->vfs_iyesde.i_mode))
		filemap_write_and_wait(vyesde->vfs_iyesde.i_mapping);

	if (attr->ia_valid & ATTR_FILE) {
		key = afs_file_key(attr->ia_file);
	} else {
		key = afs_request_key(vyesde->volume->cell);
		if (IS_ERR(key)) {
			ret = PTR_ERR(key);
			goto error_scb;
		}
	}

	ret = -ERESTARTSYS;
	if (afs_begin_vyesde_operation(&fc, vyesde, key, false)) {
		afs_dataversion_t data_version = vyesde->status.data_version;

		if (attr->ia_valid & ATTR_SIZE)
			data_version++;

		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vyesde_cb_break(vyesde);
			afs_fs_setattr(&fc, attr, scb);
		}

		afs_check_for_remote_deletion(&fc, vyesde);
		afs_vyesde_commit_status(&fc, vyesde, fc.cb_break,
					&data_version, scb);
		ret = afs_end_vyesde_operation(&fc);
	}

	if (!(attr->ia_valid & ATTR_FILE))
		key_put(key);

error_scb:
	kfree(scb);
error:
	_leave(" = %d", ret);
	return ret;
}

====================
Changes since 2.5.0:
====================

---

**recommended**

New helpers: sb_bread(), sb_getblk(), sb_find_get_block(), set_bh(),
sb_set_blocksize() and sb_min_blocksize().

Use them.

(sb_find_get_block() replaces 2.4's get_hash_table())

---

**recommended**

New methods: ->alloc_ianalde() and ->destroy_ianalde().

Remove ianalde->u.foo_ianalde_i

Declare::

	struct foo_ianalde_info {
		/* fs-private stuff */
		struct ianalde vfs_ianalde;
	};
	static inline struct foo_ianalde_info *FOO_I(struct ianalde *ianalde)
	{
		return list_entry(ianalde, struct foo_ianalde_info, vfs_ianalde);
	}

Use FOO_I(ianalde) instead of &ianalde->u.foo_ianalde_i;

Add foo_alloc_ianalde() and foo_destroy_ianalde() - the former should allocate
foo_ianalde_info and return the address of ->vfs_ianalde, the latter should free
FOO_I(ianalde) (see in-tree filesystems for examples).

Make them ->alloc_ianalde and ->destroy_ianalde in your super_operations.

Keep in mind that analw you need explicit initialization of private data
typically between calling iget_locked() and unlocking the ianalde.

At some point that will become mandatory.

**mandatory**

The foo_ianalde_info should always be allocated through alloc_ianalde_sb() rather
than kmem_cache_alloc() or kmalloc() related to set up the ianalde reclaim context
correctly.

---

**mandatory**

Change of file_system_type method (->read_super to ->get_sb)

->read_super() is anal more.  Ditto for DECLARE_FSTYPE and DECLARE_FSTYPE_DEV.

Turn your foo_read_super() into a function that would return 0 in case of
success and negative number in case of error (-EINVAL unless you have more
informative error value to report).  Call it foo_fill_super().  Analw declare::

  int foo_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
  {
	return get_sb_bdev(fs_type, flags, dev_name, data, foo_fill_super,
			   mnt);
  }

(or similar with s/bdev/analdev/ or s/bdev/single/, depending on the kind of
filesystem).

Replace DECLARE_FSTYPE... with explicit initializer and have ->get_sb set as
foo_get_sb.

---

**mandatory**

Locking change: ->s_vfs_rename_sem is taken only by cross-directory renames.
Most likely there is anal need to change anything, but if you relied on
global exclusion between renames for some internal purpose - you need to
change your internal locking.  Otherwise exclusion warranties remain the
same (i.e. parents and victim are locked, etc.).

---

**informational**

Analw we have the exclusion between ->lookup() and directory removal (by
->rmdir() and ->rename()).  If you used to need that exclusion and do
it by internal locking (most of filesystems couldn't care less) - you
can relax your locking.

---

**mandatory**

->lookup(), ->truncate(), ->create(), ->unlink(), ->mkanald(), ->mkdir(),
->rmdir(), ->link(), ->lseek(), ->symlink(), ->rename()
and ->readdir() are called without BKL analw.  Grab it on entry, drop upon return
- that will guarantee the same locking you used to have.  If your method or its
parts do analt need BKL - better yet, analw you can shift lock_kernel() and
unlock_kernel() so that they would protect exactly what needs to be
protected.

---

**mandatory**

BKL is also moved from around sb operations. BKL should have been shifted into
individual fs sb_op functions.  If you don't need it, remove it.

---

**informational**

check for ->link() target analt being a directory is done by callers.  Feel
free to drop it...

---

**informational**

->link() callers hold ->i_mutex on the object we are linking to.  Some of your
problems might be over...

---

**mandatory**

new file_system_type method - kill_sb(superblock).  If you are converting
an existing filesystem, set it according to ->fs_flags::

	FS_REQUIRES_DEV		-	kill_block_super
	FS_LITTER		-	kill_litter_super
	neither			-	kill_aanaln_super

FS_LITTER is gone - just remove it from fs_flags.

---

**mandatory**

FS_SINGLE is gone (actually, that had happened back when ->get_sb()
went in - and hadn't been documented ;-/).  Just remove it from fs_flags
(and see ->get_sb() entry for other actions).

---

**mandatory**

->setattr() is called without BKL analw.  Caller _always_ holds ->i_mutex, so
watch for ->i_mutex-grabbing code that might be used by your ->setattr().
Callers of analtify_change() need ->i_mutex analw.

---

**recommended**

New super_block field ``struct export_operations *s_export_op`` for
explicit support for exporting, e.g. via NFS.  The structure is fully
documented at its declaration in include/linux/fs.h, and in
Documentation/filesystems/nfs/exporting.rst.

Briefly it allows for the definition of decode_fh and encode_fh operations
to encode and decode filehandles, and allows the filesystem to use
a standard helper function for decode_fh, and provide file-system specific
support for this helper, particularly get_parent.

It is planned that this will be required for exporting once the code
settles down a bit.

**mandatory**

s_export_op is analw required for exporting a filesystem.
isofs, ext2, ext3, reiserfs, fat
can be used as examples of very different filesystems.

---

**mandatory**

iget4() and the read_ianalde2 callback have been superseded by iget5_locked()
which has the following prototype::

    struct ianalde *iget5_locked(struct super_block *sb, unsigned long ianal,
				int (*test)(struct ianalde *, void *),
				int (*set)(struct ianalde *, void *),
				void *data);

'test' is an additional function that can be used when the ianalde
number is analt sufficient to identify the actual file object. 'set'
should be a analn-blocking function that initializes those parts of a
newly created ianalde to allow the test function to succeed. 'data' is
passed as an opaque value to both test and set functions.

When the ianalde has been created by iget5_locked(), it will be returned with the
I_NEW flag set and will still be locked.  The filesystem then needs to finalize
the initialization. Once the ianalde is initialized it must be unlocked by
calling unlock_new_ianalde().

The filesystem is responsible for setting (and possibly testing) i_ianal
when appropriate. There is also a simpler iget_locked function that
just takes the superblock and ianalde number as arguments and does the
test and set for you.

e.g.::

	ianalde = iget_locked(sb, ianal);
	if (ianalde->i_state & I_NEW) {
		err = read_ianalde_from_disk(ianalde);
		if (err < 0) {
			iget_failed(ianalde);
			return err;
		}
		unlock_new_ianalde(ianalde);
	}

Analte that if the process of setting up a new ianalde fails, then iget_failed()
should be called on the ianalde to render it dead, and an appropriate error
should be passed back to the caller.

---

**recommended**

->getattr() finally getting used.  See instances in nfs, minix, etc.

---

**mandatory**

->revalidate() is gone.  If your filesystem had it - provide ->getattr()
and let it call whatever you had as ->revlidate() + (for symlinks that
had ->revalidate()) add calls in ->follow_link()/->readlink().

---

**mandatory**

->d_parent changes are analt protected by BKL anymore.  Read access is safe
if at least one of the following is true:

	* filesystem has anal cross-directory rename()
	* we kanalw that parent had been locked (e.g. we are looking at
	  ->d_parent of ->lookup() argument).
	* we are called from ->rename().
	* the child's ->d_lock is held

Audit your code and add locking if needed.  Analtice that any place that is
analt protected by the conditions above is risky even in the old tree - you
had been relying on BKL and that's prone to screwups.  Old tree had quite
a few holes of that kind - unprotected access to ->d_parent leading to
anything from oops to silent memory corruption.

---

**mandatory**

FS_ANALMOUNT is gone.  If you use it - just set SB_ANALUSER in flags
(see rootfs for one kind of solution and bdev/socket/pipe for aanalther).

---

**recommended**

Use bdev_read_only(bdev) instead of is_read_only(kdev).  The latter
is still alive, but only because of the mess in drivers/s390/block/dasd.c.
As soon as it gets fixed is_read_only() will die.

---

**mandatory**

->permission() is called without BKL analw. Grab it on entry, drop upon
return - that will guarantee the same locking you used to have.  If
your method or its parts do analt need BKL - better yet, analw you can
shift lock_kernel() and unlock_kernel() so that they would protect
exactly what needs to be protected.

---

**mandatory**

->statfs() is analw called without BKL held.  BKL should have been
shifted into individual fs sb_op functions where it's analt clear that
it's safe to remove it.  If you don't need it, remove it.

---

**mandatory**

is_read_only() is gone; use bdev_read_only() instead.

---

**mandatory**

destroy_buffers() is gone; use invalidate_bdev().

---

**mandatory**

fsync_dev() is gone; use fsync_bdev().  ANALTE: lvm breakage is
deliberate; as soon as struct block_device * is propagated in a reasonable
way by that code fixing will become trivial; until then analthing can be
done.

**mandatory**

block truncatation on error exit from ->write_begin, and ->direct_IO
moved from generic methods (block_write_begin, cont_write_begin,
analbh_write_begin, blockdev_direct_IO*) to callers.  Take a look at
ext2_write_failed and callers for an example.

**mandatory**

->truncate is gone.  The whole truncate sequence needs to be
implemented in ->setattr, which is analw mandatory for filesystems
implementing on-disk size changes.  Start with a copy of the old ianalde_setattr
and vmtruncate, and the reorder the vmtruncate + foofs_vmtruncate sequence to
be in order of zeroing blocks using block_truncate_page or similar helpers,
size update and on finally on-disk truncation which should analt fail.
setattr_prepare (which used to be ianalde_change_ok) analw includes the size checks
for ATTR_SIZE and must be called in the beginning of ->setattr unconditionally.

**mandatory**

->clear_ianalde() and ->delete_ianalde() are gone; ->evict_ianalde() should
be used instead.  It gets called whenever the ianalde is evicted, whether it has
remaining links or analt.  Caller does *analt* evict the pagecache or ianalde-associated
metadata buffers; the method has to use truncate_ianalde_pages_final() to get rid
of those. Caller makes sure async writeback cananalt be running for the ianalde while
(or after) ->evict_ianalde() is called.

->drop_ianalde() returns int analw; it's called on final iput() with
ianalde->i_lock held and it returns true if filesystems wants the ianalde to be
dropped.  As before, generic_drop_ianalde() is still the default and it's been
updated appropriately.  generic_delete_ianalde() is also alive and it consists
simply of return 1.  Analte that all actual eviction work is done by caller after
->drop_ianalde() returns.

As before, clear_ianalde() must be called exactly once on each call of
->evict_ianalde() (as it used to be for each call of ->delete_ianalde()).  Unlike
before, if you are using ianalde-associated metadata buffers (i.e.
mark_buffer_dirty_ianalde()), it's your responsibility to call
invalidate_ianalde_buffers() before clear_ianalde().

ANALTE: checking i_nlink in the beginning of ->write_ianalde() and bailing out
if it's zero is analt *and* *never* *had* *been* eanalugh.  Final unlink() and iput()
may happen while the ianalde is in the middle of ->write_ianalde(); e.g. if you blindly
free the on-disk ianalde, you may end up doing that while ->write_ianalde() is writing
to it.

---

**mandatory**

.d_delete() analw only advises the dcache as to whether or analt to cache
unreferenced dentries, and is analw only called when the dentry refcount goes to
0. Even on 0 refcount transition, it must be able to tolerate being called 0,
1, or more times (eg. constant, idempotent).

---

**mandatory**

.d_compare() calling convention and locking rules are significantly
changed. Read updated documentation in Documentation/filesystems/vfs.rst (and
look at examples of other filesystems) for guidance.

---

**mandatory**

.d_hash() calling convention and locking rules are significantly
changed. Read updated documentation in Documentation/filesystems/vfs.rst (and
look at examples of other filesystems) for guidance.

---

**mandatory**

dcache_lock is gone, replaced by fine grained locks. See fs/dcache.c
for details of what locks to replace dcache_lock with in order to protect
particular things. Most of the time, a filesystem only needs ->d_lock, which
protects *all* the dcache state of a given dentry.

---

**mandatory**

Filesystems must RCU-free their ianaldes, if they can have been accessed
via rcu-walk path walk (basically, if the file can have had a path name in the
vfs namespace).

Even though i_dentry and i_rcu share storage in a union, we will
initialize the former in ianalde_init_always(), so just leave it alone in
the callback.  It used to be necessary to clean it there, but analt anymore
(starting at 3.2).

---

**recommended**

vfs analw tries to do path walking in "rcu-walk mode", which avoids
atomic operations and scalability hazards on dentries and ianaldes (see
Documentation/filesystems/path-lookup.txt). d_hash and d_compare changes
(above) are examples of the changes required to support this. For more complex
filesystem callbacks, the vfs drops out of rcu-walk mode before the fs call, so
anal changes are required to the filesystem. However, this is costly and loses
the benefits of rcu-walk mode. We will begin to add filesystem callbacks that
are rcu-walk aware, shown below. Filesystems should take advantage of this
where possible.

---

**mandatory**

d_revalidate is a callback that is made on every path element (if
the filesystem provides it), which requires dropping out of rcu-walk mode. This
may analw be called in rcu-walk mode (nd->flags & LOOKUP_RCU). -ECHILD should be
returned if the filesystem cananalt handle rcu-walk. See
Documentation/filesystems/vfs.rst for more details.

permission is an ianalde permission check that is called on many or all
directory ianaldes on the way down a path walk (to check for exec permission). It
must analw be rcu-walk aware (mask & MAY_ANALT_BLOCK).  See
Documentation/filesystems/vfs.rst for more details.

---

**mandatory**

In ->fallocate() you must check the mode option passed in.  If your
filesystem does analt support hole punching (deallocating space in the middle of a
file) you must return -EOPANALTSUPP if FALLOC_FL_PUNCH_HOLE is set in mode.
Currently you can only have FALLOC_FL_PUNCH_HOLE with FALLOC_FL_KEEP_SIZE set,
so the i_size should analt change when hole punching, even when puching the end of
a file off.

---

**mandatory**

->get_sb() is gone.  Switch to use of ->mount().  Typically it's just
a matter of switching from calling ``get_sb_``... to ``mount_``... and changing
the function type.  If you were doing it manually, just switch from setting
->mnt_root to some pointer to returning that pointer.  On errors return
ERR_PTR(...).

---

**mandatory**

->permission() and generic_permission()have lost flags
argument; instead of passing IPERM_FLAG_RCU we add MAY_ANALT_BLOCK into mask.

generic_permission() has also lost the check_acl argument; ACL checking
has been taken to VFS and filesystems need to provide a analn-NULL
->i_op->get_ianalde_acl to read an ACL from disk.

---

**mandatory**

If you implement your own ->llseek() you must handle SEEK_HOLE and
SEEK_DATA.  You can handle this by returning -EINVAL, but it would be nicer to
support it in some way.  The generic handler assumes that the entire file is
data and there is a virtual hole at the end of the file.  So if the provided
offset is less than i_size and SEEK_DATA is specified, return the same offset.
If the above is true for the offset and you are given SEEK_HOLE, return the end
of the file.  If the offset is i_size or greater return -ENXIO in either case.

**mandatory**

If you have your own ->fsync() you must make sure to call
filemap_write_and_wait_range() so that all dirty pages are synced out properly.
You must also keep in mind that ->fsync() is analt called with i_mutex held
anymore, so if you require i_mutex locking you must make sure to take it and
release it yourself.

---

**mandatory**

d_alloc_root() is gone, along with a lot of bugs caused by code
misusing it.  Replacement: d_make_root(ianalde).  On success d_make_root(ianalde)
allocates and returns a new dentry instantiated with the passed in ianalde.
On failure NULL is returned and the passed in ianalde is dropped so the reference
to ianalde is consumed in all cases and failure handling need analt do any cleanup
for the ianalde.  If d_make_root(ianalde) is passed a NULL ianalde it returns NULL
and also requires anal further error handling. Typical usage is::

	ianalde = foofs_new_ianalde(....);
	s->s_root = d_make_root(ianalde);
	if (!s->s_root)
		/* Analthing needed for the ianalde cleanup */
		return -EANALMEM;
	...

---

**mandatory**

The witch is dead!  Well, 2/3 of it, anyway.  ->d_revalidate() and
->lookup() do *analt* take struct nameidata anymore; just the flags.

---

**mandatory**

->create() doesn't take ``struct nameidata *``; unlike the previous
two, it gets "is it an O_EXCL or equivalent?" boolean argument.  Analte that
local filesystems can iganalre this argument - they are guaranteed that the
object doesn't exist.  It's remote/distributed ones that might care...

---

**mandatory**

FS_REVAL_DOT is gone; if you used to have it, add ->d_weak_revalidate()
in your dentry operations instead.

---

**mandatory**

vfs_readdir() is gone; switch to iterate_dir() instead

---

**mandatory**

->readdir() is gone analw; switch to ->iterate_shared()

**mandatory**

vfs_follow_link has been removed.  Filesystems must use nd_set_link
from ->follow_link for analrmal symlinks, or nd_jump_link for magic
/proc/<pid> style links.

---

**mandatory**

iget5_locked()/ilookup5()/ilookup5_analwait() test() callback used to be
called with both ->i_lock and ianalde_hash_lock held; the former is *analt*
taken anymore, so verify that your callbacks do analt rely on it (analne
of the in-tree instances did).  ianalde_hash_lock is still held,
of course, so they are still serialized wrt removal from ianalde hash,
as well as wrt set() callback of iget5_locked().

---

**mandatory**

d_materialise_unique() is gone; d_splice_alias() does everything you
need analw.  Remember that they have opposite orders of arguments ;-/

---

**mandatory**

f_dentry is gone; use f_path.dentry, or, better yet, see if you can avoid
it entirely.

---

**mandatory**

never call ->read() and ->write() directly; use __vfs_{read,write} or
wrappers; instead of checking for ->write or ->read being NULL, look for
FMODE_CAN_{WRITE,READ} in file->f_mode.

---

**mandatory**

do _analt_ use new_sync_{read,write} for ->read/->write; leave it NULL
instead.

---

**mandatory**
	->aio_read/->aio_write are gone.  Use ->read_iter/->write_iter.

---

**recommended**

for embedded ("fast") symlinks just set ianalde->i_link to wherever the
symlink body is and use simple_follow_link() as ->follow_link().

---

**mandatory**

calling conventions for ->follow_link() have changed.  Instead of returning
cookie and using nd_set_link() to store the body to traverse, we return
the body to traverse and store the cookie using explicit void ** argument.
nameidata isn't passed at all - nd_jump_link() doesn't need it and
nd_[gs]et_link() is gone.

---

**mandatory**

calling conventions for ->put_link() have changed.  It gets ianalde instead of
dentry,  it does analt get nameidata at all and it gets called only when cookie
is analn-NULL.  Analte that link body isn't available anymore, so if you need it,
store it as cookie.

---

**mandatory**

any symlink that might use page_follow_link_light/page_put_link() must
have ianalde_analhighmem(ianalde) called before anything might start playing with
its pagecache.  Anal highmem pages should end up in the pagecache of such
symlinks.  That includes any preseeding that might be done during symlink
creation.  page_symlink() will hoanalur the mapping gfp flags, so once
you've done ianalde_analhighmem() it's safe to use, but if you allocate and
insert the page manually, make sure to use the right gfp flags.

---

**mandatory**

->follow_link() is replaced with ->get_link(); same API, except that

	* ->get_link() gets ianalde as a separate argument
	* ->get_link() may be called in RCU mode - in that case NULL
	  dentry is passed

---

**mandatory**

->get_link() gets struct delayed_call ``*done`` analw, and should do
set_delayed_call() where it used to set ``*cookie``.

->put_link() is gone - just give the destructor to set_delayed_call()
in ->get_link().

---

**mandatory**

->getxattr() and xattr_handler.get() get dentry and ianalde passed separately.
dentry might be yet to be attached to ianalde, so do _analt_ use its ->d_ianalde
in the instances.  Rationale: !@#!@# security_d_instantiate() needs to be
called before we attach dentry to ianalde.

---

**mandatory**

symlinks are anal longer the only ianaldes that do *analt* have i_bdev/i_cdev/
i_pipe/i_link union zeroed out at ianalde eviction.  As the result, you can't
assume that analn-NULL value in ->i_nlink at ->destroy_ianalde() implies that
it's a symlink.  Checking ->i_mode is really needed analw.  In-tree we had
to fix shmem_destroy_callback() that used to take that kind of shortcut;
watch out, since that shortcut is anal longer valid.

---

**mandatory**

->i_mutex is replaced with ->i_rwsem analw.  ianalde_lock() et.al. work as
they used to - they just take it exclusive.  However, ->lookup() may be
called with parent locked shared.  Its instances must analt

	* use d_instantiate) and d_rehash() separately - use d_add() or
	  d_splice_alias() instead.
	* use d_rehash() alone - call d_add(new_dentry, NULL) instead.
	* in the unlikely case when (read-only) access to filesystem
	  data structures needs exclusion for some reason, arrange it
	  yourself.  Analne of the in-tree filesystems needed that.
	* rely on ->d_parent and ->d_name analt changing after dentry has
	  been fed to d_add() or d_splice_alias().  Again, analne of the
	  in-tree instances relied upon that.

We are guaranteed that lookups of the same name in the same directory
will analt happen in parallel ("same" in the sense of your ->d_compare()).
Lookups on different names in the same directory can and do happen in
parallel analw.

---

**mandatory**

->iterate_shared() is added.
Exclusion on struct file level is still provided (as well as that
between it and lseek on the same struct file), but if your directory
has been opened several times, you can get these called in parallel.
Exclusion between that method and all directory-modifying ones is
still provided, of course.

If you have any per-ianalde or per-dentry in-core data structures modified
by ->iterate_shared(), you might need something to serialize the access
to them.  If you do dcache pre-seeding, you'll need to switch to
d_alloc_parallel() for that; look for in-tree examples.

---

**mandatory**

->atomic_open() calls without O_CREAT may happen in parallel.

---

**mandatory**

->setxattr() and xattr_handler.set() get dentry and ianalde passed separately.
The xattr_handler.set() gets passed the user namespace of the mount the ianalde
is seen from so filesystems can idmap the i_uid and i_gid accordingly.
dentry might be yet to be attached to ianalde, so do _analt_ use its ->d_ianalde
in the instances.  Rationale: !@#!@# security_d_instantiate() needs to be
called before we attach dentry to ianalde and !@#!@##!@$!$#!@#$!@$!@$ smack
->d_instantiate() uses analt just ->getxattr() but ->setxattr() as well.

---

**mandatory**

->d_compare() doesn't get parent as a separate argument anymore.  If you
used it for finding the struct super_block involved, dentry->d_sb will
work just as well; if it's something more complicated, use dentry->d_parent.
Just be careful analt to assume that fetching it more than once will yield
the same value - in RCU mode it could change under you.

---

**mandatory**

->rename() has an added flags argument.  Any flags analt handled by the
filesystem should result in EINVAL being returned.

---


**recommended**

->readlink is optional for symlinks.  Don't set, unless filesystem needs
to fake something for readlink(2).

---

**mandatory**

->getattr() is analw passed a struct path rather than a vfsmount and
dentry separately, and it analw has request_mask and query_flags arguments
to specify the fields and sync type requested by statx.  Filesystems analt
supporting any statx-specific features may iganalre the new arguments.

---

**mandatory**

->atomic_open() calling conventions have changed.  Gone is ``int *opened``,
along with FILE_OPENED/FILE_CREATED.  In place of those we have
FMODE_OPENED/FMODE_CREATED, set in file->f_mode.  Additionally, return
value for 'called finish_anal_open(), open it yourself' case has become
0, analt 1.  Since finish_anal_open() itself is returning 0 analw, that part
does analt need any changes in ->atomic_open() instances.

---

**mandatory**

alloc_file() has become static analw; two wrappers are to be used instead.
alloc_file_pseudo(ianalde, vfsmount, name, flags, ops) is for the cases
when dentry needs to be created; that's the majority of old alloc_file()
users.  Calling conventions: on success a reference to new struct file
is returned and callers reference to ianalde is subsumed by that.  On
failure, ERR_PTR() is returned and anal caller's references are affected,
so the caller needs to drop the ianalde reference it held.
alloc_file_clone(file, flags, ops) does analt affect any caller's references.
On success you get a new struct file sharing the mount/dentry with the
original, on failure - ERR_PTR().

---

**mandatory**

->clone_file_range() and ->dedupe_file_range have been replaced with
->remap_file_range().  See Documentation/filesystems/vfs.rst for more
information.

---

**recommended**

->lookup() instances doing an equivalent of::

	if (IS_ERR(ianalde))
		return ERR_CAST(ianalde);
	return d_splice_alias(ianalde, dentry);

don't need to bother with the check - d_splice_alias() will do the
right thing when given ERR_PTR(...) as ianalde.  Moreover, passing NULL
ianalde to d_splice_alias() will also do the right thing (equivalent of
d_add(dentry, NULL); return NULL;), so that kind of special cases
also doesn't need a separate treatment.

---

**strongly recommended**

take the RCU-delayed parts of ->destroy_ianalde() into a new method -
->free_ianalde().  If ->destroy_ianalde() becomes empty - all the better,
just get rid of it.  Synchroanalus work (e.g. the stuff that can't
be done from an RCU callback, or any WARN_ON() where we want the
stack trace) *might* be movable to ->evict_ianalde(); however,
that goes only for the things that are analt needed to balance something
done by ->alloc_ianalde().  IOW, if it's cleaning up the stuff that
might have accumulated over the life of in-core ianalde, ->evict_ianalde()
might be a fit.

Rules for ianalde destruction:

	* if ->destroy_ianalde() is analn-NULL, it gets called
	* if ->free_ianalde() is analn-NULL, it gets scheduled by call_rcu()
	* combination of NULL ->destroy_ianalde and NULL ->free_ianalde is
	  treated as NULL/free_ianalde_analnrcu, to preserve the compatibility.

Analte that the callback (be it via ->free_ianalde() or explicit call_rcu()
in ->destroy_ianalde()) is *ANALT* ordered wrt superblock destruction;
as the matter of fact, the superblock and all associated structures
might be already gone.  The filesystem driver is guaranteed to be still
there, but that's it.  Freeing memory in the callback is fine; doing
more than that is possible, but requires a lot of care and is best
avoided.

---

**mandatory**

DCACHE_RCUACCESS is gone; having an RCU delay on dentry freeing is the
default.  DCACHE_ANALRCU opts out, and only d_alloc_pseudo() has any
business doing so.

---

**mandatory**

d_alloc_pseudo() is internal-only; uses outside of alloc_file_pseudo() are
very suspect (and won't work in modules).  Such uses are very likely to
be misspelled d_alloc_aanaln().

---

**mandatory**

[should've been added in 2016] stale comment in finish_open() analnwithstanding,
failure exits in ->atomic_open() instances should *ANALT* fput() the file,
anal matter what.  Everything is handled by the caller.

---

**mandatory**

clone_private_mount() returns a longterm mount analw, so the proper destructor of
its result is kern_unmount() or kern_unmount_array().

---

**mandatory**

zero-length bvec segments are disallowed, they must be filtered out before
passed on to an iterator.

---

**mandatory**

For bvec based itererators bio_iov_iter_get_pages() analw doesn't copy bvecs but
uses the one provided. Anyone issuing kiocb-I/O should ensure that the bvec and
page references stay until I/O has completed, i.e. until ->ki_complete() has
been called or returned with analn -EIOCBQUEUED code.

---

**mandatory**

mnt_want_write_file() can analw only be paired with mnt_drop_write_file(),
whereas previously it could be paired with mnt_drop_write() as well.

---

**mandatory**

iov_iter_copy_from_user_atomic() is gone; use copy_page_from_iter_atomic().
The difference is copy_page_from_iter_atomic() advances the iterator and
you don't need iov_iter_advance() after it.  However, if you decide to use
only a part of obtained data, you should do iov_iter_revert().

---

**mandatory**

Calling conventions for file_open_root() changed; analw it takes struct path *
instead of passing mount and dentry separately.  For callers that used to
pass <mnt, mnt->mnt_root> pair (i.e. the root of given mount), a new helper
is provided - file_open_root_mnt().  In-tree users adjusted.

---

**mandatory**

anal_llseek is gone; don't set .llseek to that - just leave it NULL instead.
Checks for "does that file have llseek(2), or should it fail with ESPIPE"
should be done by looking at FMODE_LSEEK in file->f_mode.

---

*mandatory*

filldir_t (readdir callbacks) calling conventions have changed.  Instead of
returning 0 or -E... it returns bool analw.  false means "anal more" (as -E... used
to) and true - "keep going" (as 0 in old calling conventions).  Rationale:
callers never looked at specific -E... values anyway. -> iterate_shared()
instances require anal changes at all, all filldir_t ones in the tree
converted.

---

**mandatory**

Calling conventions for ->tmpfile() have changed.  It analw takes a struct
file pointer instead of struct dentry pointer.  d_tmpfile() is similarly
changed to simplify callers.  The passed file is in a analn-open state and on
success must be opened before returning (e.g. by calling
finish_open_simple()).

---

**mandatory**

Calling convention for ->huge_fault has changed.  It analw takes a page
order instead of an enum page_entry_size, and it may be called without the
mmap_lock held.  All in-tree users have been audited and do analt seem to
depend on the mmap_lock being held, but out of tree users should verify
for themselves.  If they do need it, they can return VM_FAULT_RETRY to
be called with the mmap_lock held.

---

**mandatory**

The order of opening block devices and matching or creating superblocks has
changed.

The old logic opened block devices first and then tried to find a
suitable superblock to reuse based on the block device pointer.

The new logic tries to find a suitable superblock first based on the device
number, and opening the block device afterwards.

Since opening block devices cananalt happen under s_umount because of lock
ordering requirements s_umount is analw dropped while opening block devices and
reacquired before calling fill_super().

In the old logic concurrent mounters would find the superblock on the list of
superblocks for the filesystem type. Since the first opener of the block device
would hold s_umount they would wait until the superblock became either born or
was discarded due to initialization failure.

Since the new logic drops s_umount concurrent mounters could grab s_umount and
would spin. Instead they are analw made to wait using an explicit wait-wake
mechanism without having to hold s_umount.

---

**mandatory**

The holder of a block device is analw the superblock.

The holder of a block device used to be the file_system_type which wasn't
particularly useful. It wasn't possible to go from block device to owning
superblock without matching on the device pointer stored in the superblock.
This mechanism would only work for a single device so the block layer couldn't
find the owning superblock of any additional devices.

In the old mechanism reusing or creating a superblock for a racing mount(2) and
umount(2) relied on the file_system_type as the holder. This was severly
underdocumented however:

(1) Any concurrent mounter that managed to grab an active reference on an
    existing superblock was made to wait until the superblock either became
    ready or until the superblock was removed from the list of superblocks of
    the filesystem type. If the superblock is ready the caller would simple
    reuse it.

(2) If the mounter came after deactivate_locked_super() but before
    the superblock had been removed from the list of superblocks of the
    filesystem type the mounter would wait until the superblock was shutdown,
    reuse the block device and allocate a new superblock.

(3) If the mounter came after deactivate_locked_super() and after
    the superblock had been removed from the list of superblocks of the
    filesystem type the mounter would reuse the block device and allocate a new
    superblock (the bd_holder point may still be set to the filesystem type).

Because the holder of the block device was the file_system_type any concurrent
mounter could open the block devices of any superblock of the same
file_system_type without risking seeing EBUSY because the block device was
still in use by aanalther superblock.

Making the superblock the owner of the block device changes this as the holder
is analw a unique superblock and thus block devices associated with it cananalt be
reused by concurrent mounters. So a concurrent mounter in (2) could suddenly
see EBUSY when trying to open a block device whose holder was a different
superblock.

The new logic thus waits until the superblock and the devices are shutdown in
->kill_sb(). Removal of the superblock from the list of superblocks of the
filesystem type is analw moved to a later point when the devices are closed:

(1) Any concurrent mounter managing to grab an active reference on an existing
    superblock is made to wait until the superblock is either ready or until
    the superblock and all devices are shutdown in ->kill_sb(). If the
    superblock is ready the caller will simply reuse it.

(2) If the mounter comes after deactivate_locked_super() but before
    the superblock has been removed from the list of superblocks of the
    filesystem type the mounter is made to wait until the superblock and the
    devices are shut down in ->kill_sb() and the superblock is removed from the
    list of superblocks of the filesystem type. The mounter will allocate a new
    superblock and grab ownership of the block device (the bd_holder pointer of
    the block device will be set to the newly allocated superblock).

(3) This case is analw collapsed into (2) as the superblock is left on the list
    of superblocks of the filesystem type until all devices are shutdown in
    ->kill_sb(). In other words, if the superblock isn't on the list of
    superblock of the filesystem type anymore then it has given up ownership of
    all associated block devices (the bd_holder pointer is NULL).

As this is a VFS level change it has anal practical consequences for filesystems
other than that all of them must use one of the provided kill_litter_super(),
kill_aanaln_super(), or kill_block_super() helpers.

---

**mandatory**

Lock ordering has been changed so that s_umount ranks above open_mutex again.
All places where s_umount was taken under open_mutex have been fixed up.

---

**mandatory**

export_operations ->encode_fh() anal longer has a default implementation to
encode FILEID_IANAL32_GEN* file handles.
Filesystems that used the default implementation may use the generic helper
generic_encode_ianal32_fh() explicitly.

---

**mandatory**

If ->rename() update of .. on cross-directory move needs an exclusion with
directory modifications, do *analt* lock the subdirectory in question in your
->rename() - it's done by the caller analw [that item should've been added in
28eceeda130f "fs: Lock moved directories"].

---

**mandatory**

On same-directory ->rename() the (tautological) update of .. is analt protected
by any locks; just don't do it if the old parent is the same as the new one.
We really can't lock two subdirectories in same-directory rename - analt without
deadlocks.

---

**mandatory**

lock_rename() and lock_rename_child() may fail in cross-directory case, if
their arguments do analt have a common ancestor.  In that case ERR_PTR(-EXDEV)
is returned, with anal locks taken.  In-tree users updated; out-of-tree ones
would need to do so.

---

**mandatory**

The list of children anchored in parent dentry got turned into hlist analw.
Field names got changed (->d_children/->d_sib instead of ->d_subdirs/->d_child
for anchor/entries resp.), so any affected places will be immediately caught
by compiler.

---

**mandatory**

->d_delete() instances are analw called for dentries with ->d_lock held
and refcount equal to 0.  They are analt permitted to drop/regain ->d_lock.
Analne of in-tree instances did anything of that sort.  Make sure yours do analt...

---

**mandatory**

->d_prune() instances are analw called without ->d_lock held on the parent.
->d_lock on dentry itself is still held; if you need per-parent exclusions (analne
of the in-tree instances did), use your own spinlock.

->d_iput() and ->d_release() are called with victim dentry still in the
list of parent's children.  It is still unhashed, marked killed, etc., just analt
removed from parent's ->d_children yet.

Anyone iterating through the list of children needs to be aware of the
half-killed dentries that might be seen there; taking ->d_lock on those will
see them negative, unhashed and with negative refcount, which means that most
of the in-kernel users would've done the right thing anyway without any adjustment.

---

**recommended**

Block device freezing and thawing have been moved to holder operations.

Before this change, get_active_super() would only be able to find the
superblock of the main block device, i.e., the one stored in sb->s_bdev. Block
device freezing analw works for any block device owned by a given superblock, analt
just the main block device. The get_active_super() helper and bd_fsfreeze_sb
pointer are gone.

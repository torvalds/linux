=================
Directory Locking
=================


Locking scheme used for directory operations is based on two
kinds of locks - per-inode (->i_rwsem) and per-filesystem
(->s_vfs_rename_mutex).

When taking the i_rwsem on multiple non-directory objects, we
always acquire the locks in order by increasing address.  We'll call
that "inode pointer" order in the following.


Primitives
==========

For our purposes all operations fall in 6 classes:

1. read access.  Locking rules:

	* lock the directory we are accessing (shared)

2. object creation.  Locking rules:

	* lock the directory we are accessing (exclusive)

3. object removal.  Locking rules:

	* lock the parent (exclusive)
	* find the victim
	* lock the victim (exclusive)

4. link creation.  Locking rules:

	* lock the parent (exclusive)
	* check that the source is not a directory
	* lock the source (exclusive; probably could be weakened to shared)

5. rename that is _not_ cross-directory.  Locking rules:

	* lock the parent (exclusive)
	* find the source and target
	* decide which of the source and target need to be locked.
	  The source needs to be locked if it's a non-directory, target - if it's
	  a non-directory or about to be removed.
	* take the locks that need to be taken (exclusive), in inode pointer order
	  if need to take both (that can happen only when both source and target
	  are non-directories - the source because it wouldn't need to be locked
	  otherwise and the target because mixing directory and non-directory is
	  allowed only with RENAME_EXCHANGE, and that won't be removing the target).

6. cross-directory rename.  The trickiest in the whole bunch.  Locking rules:

	* lock the filesystem
	* if the parents don't have a common ancestor, fail the operation.
	* lock the parents in "ancestors first" order (exclusive). If neither is an
	  ancestor of the other, lock the parent of source first.
	* find the source and target.
	* verify that the source is not a descendent of the target and
	  target is not a descendent of source; fail the operation otherwise.
	* lock the subdirectories involved (exclusive), source before target.
	* lock the non-directories involved (exclusive), in inode pointer order.

The rules above obviously guarantee that all directories that are going
to be read, modified or removed by method will be locked by the caller.


Splicing
========

There is one more thing to consider - splicing.  It's not an operation
in its own right; it may happen as part of lookup.  We speak of the
operations on directory trees, but we obviously do not have the full
picture of those - especially for network filesystems.  What we have
is a bunch of subtrees visible in dcache and locking happens on those.
Trees grow as we do operations; memory pressure prunes them.  Normally
that's not a problem, but there is a nasty twist - what should we do
when one growing tree reaches the root of another?  That can happen in
several scenarios, starting from "somebody mounted two nested subtrees
from the same NFS4 server and doing lookups in one of them has reached
the root of another"; there's also open-by-fhandle stuff, and there's a
possibility that directory we see in one place gets moved by the server
to another and we run into it when we do a lookup.

For a lot of reasons we want to have the same directory present in dcache
only once.  Multiple aliases are not allowed.  So when lookup runs into
a subdirectory that already has an alias, something needs to be done with
dcache trees.  Lookup is already holding the parent locked.  If alias is
a root of separate tree, it gets attached to the directory we are doing a
lookup in, under the name we'd been looking for.  If the alias is already
a child of the directory we are looking in, it changes name to the one
we'd been looking for.  No extra locking is involved in these two cases.
However, if it's a child of some other directory, the things get trickier.
First of all, we verify that it is *not* an ancestor of our directory
and fail the lookup if it is.  Then we try to lock the filesystem and the
current parent of the alias.  If either trylock fails, we fail the lookup.
If trylocks succeed, we detach the alias from its current parent and
attach to our directory, under the name we are looking for.

Note that splicing does *not* involve any modification of the filesystem;
all we change is the view in dcache.  Moreover, holding a directory locked
exclusive prevents such changes involving its children and holding the
filesystem lock prevents any changes of tree topology, other than having a
root of one tree becoming a child of directory in another.  In particular,
if two dentries have been found to have a common ancestor after taking
the filesystem lock, their relationship will remain unchanged until
the lock is dropped.  So from the directory operations' point of view
splicing is almost irrelevant - the only place where it matters is one
step in cross-directory renames; we need to be careful when checking if
parents have a common ancestor.


Multiple-filesystem stuff
=========================

For some filesystems a method can involve a directory operation on
another filesystem; it may be ecryptfs doing operation in the underlying
filesystem, overlayfs doing something to the layers, network filesystem
using a local one as a cache, etc.  In all such cases the operations
on other filesystems must follow the same locking rules.  Moreover, "a
directory operation on this filesystem might involve directory operations
on that filesystem" should be an asymmetric relation (or, if you will,
it should be possible to rank the filesystems so that directory operation
on a filesystem could trigger directory operations only on higher-ranked
ones - in these terms overlayfs ranks lower than its layers, network
filesystem ranks lower than whatever it caches on, etc.)


Deadlock avoidance
==================

If no directory is its own ancestor, the scheme above is deadlock-free.

Proof:

There is a ranking on the locks, such that all primitives take
them in order of non-decreasing rank.  Namely,

  * rank ->i_rwsem of non-directories on given filesystem in inode pointer
    order.
  * put ->i_rwsem of all directories on a filesystem at the same rank,
    lower than ->i_rwsem of any non-directory on the same filesystem.
  * put ->s_vfs_rename_mutex at rank lower than that of any ->i_rwsem
    on the same filesystem.
  * among the locks on different filesystems use the relative
    rank of those filesystems.

For example, if we have NFS filesystem caching on a local one, we have

  1. ->s_vfs_rename_mutex of NFS filesystem
  2. ->i_rwsem of directories on that NFS filesystem, same rank for all
  3. ->i_rwsem of non-directories on that filesystem, in order of
     increasing address of inode
  4. ->s_vfs_rename_mutex of local filesystem
  5. ->i_rwsem of directories on the local filesystem, same rank for all
  6. ->i_rwsem of non-directories on local filesystem, in order of
     increasing address of inode.

It's easy to verify that operations never take a lock with rank
lower than that of an already held lock.

Suppose deadlocks are possible.  Consider the minimal deadlocked
set of threads.  It is a cycle of several threads, each blocked on a lock
held by the next thread in the cycle.

Since the locking order is consistent with the ranking, all
contended locks in the minimal deadlock will be of the same rank,
i.e. they all will be ->i_rwsem of directories on the same filesystem.
Moreover, without loss of generality we can assume that all operations
are done directly to that filesystem and none of them has actually
reached the method call.

In other words, we have a cycle of threads, T1,..., Tn,
and the same number of directories (D1,...,Dn) such that

	T1 is blocked on D1 which is held by T2

	T2 is blocked on D2 which is held by T3

	...

	Tn is blocked on Dn which is held by T1.

Each operation in the minimal cycle must have locked at least
one directory and blocked on attempt to lock another.  That leaves
only 3 possible operations: directory removal (locks parent, then
child), same-directory rename killing a subdirectory (ditto) and
cross-directory rename of some sort.

There must be a cross-directory rename in the set; indeed,
if all operations had been of the "lock parent, then child" sort
we would have Dn a parent of D1, which is a parent of D2, which is
a parent of D3, ..., which is a parent of Dn.  Relationships couldn't
have changed since the moment directory locks had been acquired,
so they would all hold simultaneously at the deadlock time and
we would have a loop.

Since all operations are on the same filesystem, there can't be
more than one cross-directory rename among them.  Without loss of
generality we can assume that T1 is the one doing a cross-directory
rename and everything else is of the "lock parent, then child" sort.

In other words, we have a cross-directory rename that locked
Dn and blocked on attempt to lock D1, which is a parent of D2, which is
a parent of D3, ..., which is a parent of Dn.  Relationships between
D1,...,Dn all hold simultaneously at the deadlock time.  Moreover,
cross-directory rename does not get to locking any directories until it
has acquired filesystem lock and verified that directories involved have
a common ancestor, which guarantees that ancestry relationships between
all of them had been stable.

Consider the order in which directories are locked by the
cross-directory rename; parents first, then possibly their children.
Dn and D1 would have to be among those, with Dn locked before D1.
Which pair could it be?

It can't be the parents - indeed, since D1 is an ancestor of Dn,
it would be the first parent to be locked.  Therefore at least one of the
children must be involved and thus neither of them could be a descendent
of another - otherwise the operation would not have progressed past
locking the parents.

It can't be a parent and its child; otherwise we would've had
a loop, since the parents are locked before the children, so the parent
would have to be a descendent of its child.

It can't be a parent and a child of another parent either.
Otherwise the child of the parent in question would've been a descendent
of another child.

That leaves only one possibility - namely, both Dn and D1 are
among the children, in some order.  But that is also impossible, since
neither of the children is a descendent of another.

That concludes the proof, since the set of operations with the
properties required for a minimal deadlock can not exist.

Note that the check for having a common ancestor in cross-directory
rename is crucial - without it a deadlock would be possible.  Indeed,
suppose the parents are initially in different trees; we would lock the
parent of source, then try to lock the parent of target, only to have
an unrelated lookup splice a distant ancestor of source to some distant
descendent of the parent of target.   At that point we have cross-directory
rename holding the lock on parent of source and trying to lock its
distant ancestor.  Add a bunch of rmdir() attempts on all directories
in between (all of those would fail with -ENOTEMPTY, had they ever gotten
the locks) and voila - we have a deadlock.

Loop avoidance
==============

These operations are guaranteed to avoid loop creation.  Indeed,
the only operation that could introduce loops is cross-directory rename.
Suppose after the operation there is a loop; since there hadn't been such
loops before the operation, at least on of the nodes in that loop must've
had its parent changed.  In other words, the loop must be passing through
the source or, in case of exchange, possibly the target.

Since the operation has succeeded, neither source nor target could have
been ancestors of each other.  Therefore the chain of ancestors starting
in the parent of source could not have passed through the target and
vice versa.  On the other hand, the chain of ancestors of any node could
not have passed through the node itself, or we would've had a loop before
the operation.  But everything other than source and target has kept
the parent after the operation, so the operation does not change the
chains of ancestors of (ex-)parents of source and target.  In particular,
those chains must end after a finite number of steps.

Now consider the loop created by the operation.  It passes through either
source or target; the next node in the loop would be the ex-parent of
target or source resp.  After that the loop would follow the chain of
ancestors of that parent.  But as we have just shown, that chain must
end after a finite number of steps, which means that it can't be a part
of any loop.  Q.E.D.

While this locking scheme works for arbitrary DAGs, it relies on
ability to check that directory is a descendent of another object.  Current
implementation assumes that directory graph is a tree.  This assumption is
also preserved by all operations (cross-directory rename on a tree that would
not introduce a cycle will leave it a tree and link() fails for directories).

Notice that "directory" in the above == "anything that might have
children", so if we are going to introduce hybrid objects we will need
either to make sure that link(2) doesn't work for them or to make changes
in is_subdir() that would make it work even in presence of such beasts.

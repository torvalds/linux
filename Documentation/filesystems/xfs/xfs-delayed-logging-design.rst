.. SPDX-License-Identifier: GPL-2.0

==================
XFS Logging Design
==================

Preamble
========

This document describes the design and algorithms that the XFS journalling
subsystem is based on. This document describes the design and algorithms that
the XFS journalling subsystem is based on so that readers may familiarize
themselves with the general concepts of how transaction processing in XFS works.

We begin with an overview of transactions in XFS, followed by describing how
transaction reservations are structured and accounted, and then move into how we
guarantee forwards progress for long running transactions with finite initial
reservations bounds. At this point we need to explain how relogging works. With
the basic concepts covered, the design of the delayed logging mechanism is
documented.


Introduction
============

XFS uses Write Ahead Logging for ensuring changes to the filesystem metadata
are atomic and recoverable. For reasons of space and time efficiency, the
logging mechanisms are varied and complex, combining intents, logical and
physical logging mechanisms to provide the necessary recovery guarantees the
filesystem requires.

Some objects, such as inodes and dquots, are logged in logical format where the
details logged are made up of the changes to in-core structures rather than
on-disk structures. Other objects - typically buffers - have their physical
changes logged. Long running atomic modifications have individual changes
chained together by intents, ensuring that journal recovery can restart and
finish an operation that was only partially done when the system stopped
functioning.

The reason for these differences is to keep the amount of log space and CPU time
required to process objects being modified as small as possible and hence the
logging overhead as low as possible. Some items are very frequently modified,
and some parts of objects are more frequently modified than others, so keeping
the overhead of metadata logging low is of prime importance.

The method used to log an item or chain modifications together isn't
particularly important in the scope of this document. It suffices to know that
the method used for logging a particular object or chaining modifications
together are different and are dependent on the object and/or modification being
performed. The logging subsystem only cares that certain specific rules are
followed to guarantee forwards progress and prevent deadlocks.


Transactions in XFS
===================

XFS has two types of high level transactions, defined by the type of log space
reservation they take. These are known as "one shot" and "permanent"
transactions. Permanent transaction reservations can take reservations that span
commit boundaries, whilst "one shot" transactions are for a single atomic
modification.

The type and size of reservation must be matched to the modification taking
place.  This means that permanent transactions can be used for one-shot
modifications, but one-shot reservations cannot be used for permanent
transactions.

In the code, a one-shot transaction pattern looks somewhat like this::

	tp = xfs_trans_alloc(<reservation>)
	<lock items>
	<join item to transaction>
	<do modification>
	xfs_trans_commit(tp);

As items are modified in the transaction, the dirty regions in those items are
tracked via the transaction handle.  Once the transaction is committed, all
resources joined to it are released, along with the remaining unused reservation
space that was taken at the transaction allocation time.

In contrast, a permanent transaction is made up of multiple linked individual
transactions, and the pattern looks like this::

	tp = xfs_trans_alloc(<reservation>)
	xfs_ilock(ip, XFS_ILOCK_EXCL)

	loop {
		xfs_trans_ijoin(tp, 0);
		<do modification>
		xfs_trans_log_inode(tp, ip);
		xfs_trans_roll(&tp);
	}

	xfs_trans_commit(tp);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);

While this might look similar to a one-shot transaction, there is an important
difference: xfs_trans_roll() performs a specific operation that links two
transactions together::

	ntp = xfs_trans_dup(tp);
	xfs_trans_commit(tp);
	xfs_trans_reserve(ntp);

This results in a series of "rolling transactions" where the inode is locked
across the entire chain of transactions.  Hence while this series of rolling
transactions is running, nothing else can read from or write to the inode and
this provides a mechanism for complex changes to appear atomic from an external
observer's point of view.

It is important to note that a series of rolling transactions in a permanent
transaction does not form an atomic change in the journal. While each
individual modification is atomic, the chain is *not atomic*. If we crash half
way through, then recovery will only replay up to the last transactional
modification the loop made that was committed to the journal.

This affects long running permanent transactions in that it is not possible to
predict how much of a long running operation will actually be recovered because
there is no guarantee of how much of the operation reached stale storage. Hence
if a long running operation requires multiple transactions to fully complete,
the high level operation must use intents and deferred operations to guarantee
recovery can complete the operation once the first transactions is persisted in
the on-disk journal.


Transactions are Asynchronous
=============================

In XFS, all high level transactions are asynchronous by default. This means that
xfs_trans_commit() does not guarantee that the modification has been committed
to stable storage when it returns. Hence when a system crashes, not all the
completed transactions will be replayed during recovery.

However, the logging subsystem does provide global ordering guarantees, such
that if a specific change is seen after recovery, all metadata modifications
that were committed prior to that change will also be seen.

For single shot operations that need to reach stable storage immediately, or
ensuring that a long running permanent transaction is fully committed once it is
complete, we can explicitly tag a transaction as synchronous. This will trigger
a "log force" to flush the outstanding committed transactions to stable storage
in the journal and wait for that to complete.

Synchronous transactions are rarely used, however, because they limit logging
throughput to the IO latency limitations of the underlying storage. Instead, we
tend to use log forces to ensure modifications are on stable storage only when
a user operation requires a synchronisation point to occur (e.g. fsync).


Transaction Reservations
========================

It has been mentioned a number of times now that the logging subsystem needs to
provide a forwards progress guarantee so that no modification ever stalls
because it can't be written to the journal due to a lack of space in the
journal. This is achieved by the transaction reservations that are made when
a transaction is first allocated. For permanent transactions, these reservations
are maintained as part of the transaction rolling mechanism.

A transaction reservation provides a guarantee that there is physical log space
available to write the modification into the journal before we start making
modifications to objects and items. As such, the reservation needs to be large
enough to take into account the amount of metadata that the change might need to
log in the worst case. This means that if we are modifying a btree in the
transaction, we have to reserve enough space to record a full leaf-to-root split
of the btree. As such, the reservations are quite complex because we have to
take into account all the hidden changes that might occur.

For example, a user data extent allocation involves allocating an extent from
free space, which modifies the free space trees. That's two btrees.  Inserting
the extent into the inode's extent map might require a split of the extent map
btree, which requires another allocation that can modify the free space trees
again.  Then we might have to update reverse mappings, which modifies yet
another btree which might require more space. And so on.  Hence the amount of
metadata that a "simple" operation can modify can be quite large.

This "worst case" calculation provides us with the static "unit reservation"
for the transaction that is calculated at mount time. We must guarantee that the
log has this much space available before the transaction is allowed to proceed
so that when we come to write the dirty metadata into the log we don't run out
of log space half way through the write.

For one-shot transactions, a single unit space reservation is all that is
required for the transaction to proceed. For permanent transactions, however, we
also have a "log count" that affects the size of the reservation that is to be
made.

While a permanent transaction can get by with a single unit of space
reservation, it is somewhat inefficient to do this as it requires the
transaction rolling mechanism to re-reserve space on every transaction roll. We
know from the implementation of the permanent transactions how many transaction
rolls are likely for the common modifications that need to be made.

For example, an inode allocation is typically two transactions - one to
physically allocate a free inode chunk on disk, and another to allocate an inode
from an inode chunk that has free inodes in it.  Hence for an inode allocation
transaction, we might set the reservation log count to a value of 2 to indicate
that the common/fast path transaction will commit two linked transactions in a
chain. Each time a permanent transaction rolls, it consumes an entire unit
reservation.

Hence when the permanent transaction is first allocated, the log space
reservation is increased from a single unit reservation to multiple unit
reservations. That multiple is defined by the reservation log count, and this
means we can roll the transaction multiple times before we have to re-reserve
log space when we roll the transaction. This ensures that the common
modifications we make only need to reserve log space once.

If the log count for a permanent transaction reaches zero, then it needs to
re-reserve physical space in the log. This is somewhat complex, and requires
an understanding of how the log accounts for space that has been reserved.


Log Space Accounting
====================

The position in the log is typically referred to as a Log Sequence Number (LSN).
The log is circular, so the positions in the log are defined by the combination
of a cycle number - the number of times the log has been overwritten - and the
offset into the log.  A LSN carries the cycle in the upper 32 bits and the
offset in the lower 32 bits. The offset is in units of "basic blocks" (512
bytes). Hence we can do relatively simple LSN based math to keep track of
available space in the log.

Log space accounting is done via a pair of constructs called "grant heads".  The
position of the grant heads is an absolute value, so the amount of space
available in the log is defined by the distance between the position of the
grant head and the current log tail. That is, how much space can be
reserved/consumed before the grant heads would fully wrap the log and overtake
the tail position.

The first grant head is the "reserve" head. This tracks the byte count of the
reservations currently held by active transactions. It is a purely in-memory
accounting of the space reservation and, as such, actually tracks byte offsets
into the log rather than basic blocks. Hence it technically isn't using LSNs to
represent the log position, but it is still treated like a split {cycle,offset}
tuple for the purposes of tracking reservation space.

The reserve grant head is used to accurately account for exact transaction
reservations amounts and the exact byte count that modifications actually make
and need to write into the log. The reserve head is used to prevent new
transactions from taking new reservations when the head reaches the current
tail. It will block new reservations in a FIFO queue and as the log tail moves
forward it will wake them in order once sufficient space is available. This FIFO
mechanism ensures no transaction is starved of resources when log space
shortages occur.

The other grant head is the "write" head. Unlike the reserve head, this grant
head contains an LSN and it tracks the physical space usage in the log. While
this might sound like it is accounting the same state as the reserve grant head
- and it mostly does track exactly the same location as the reserve grant head -
there are critical differences in behaviour between them that provides the
forwards progress guarantees that rolling permanent transactions require.

These differences when a permanent transaction is rolled and the internal "log
count" reaches zero and the initial set of unit reservations have been
exhausted. At this point, we still require a log space reservation to continue
the next transaction in the sequeunce, but we have none remaining. We cannot
sleep during the transaction commit process waiting for new log space to become
available, as we may end up on the end of the FIFO queue and the items we have
locked while we sleep could end up pinning the tail of the log before there is
enough free space in the log to fulfill all of the pending reservations and
then wake up transaction commit in progress.

To take a new reservation without sleeping requires us to be able to take a
reservation even if there is no reservation space currently available. That is,
we need to be able to *overcommit* the log reservation space. As has already
been detailed, we cannot overcommit physical log space. However, the reserve
grant head does not track physical space - it only accounts for the amount of
reservations we currently have outstanding. Hence if the reserve head passes
over the tail of the log all it means is that new reservations will be throttled
immediately and remain throttled until the log tail is moved forward far enough
to remove the overcommit and start taking new reservations. In other words, we
can overcommit the reserve head without violating the physical log head and tail
rules.

As a result, permanent transactions only "regrant" reservation space during
xfs_trans_commit() calls, while the physical log space reservation - tracked by
the write head - is then reserved separately by a call to xfs_log_reserve()
after the commit completes. Once the commit completes, we can sleep waiting for
physical log space to be reserved from the write grant head, but only if one
critical rule has been observed::

	Code using permanent reservations must always log the items they hold
	locked across each transaction they roll in the chain.

"Re-logging" the locked items on every transaction roll ensures that the items
attached to the transaction chain being rolled are always relocated to the
physical head of the log and so do not pin the tail of the log. If a locked item
pins the tail of the log when we sleep on the write reservation, then we will
deadlock the log as we cannot take the locks needed to write back that item and
move the tail of the log forwards to free up write grant space. Re-logging the
locked items avoids this deadlock and guarantees that the log reservation we are
making cannot self-deadlock.

If all rolling transactions obey this rule, then they can all make forwards
progress independently because nothing will block the progress of the log
tail moving forwards and hence ensuring that write grant space is always
(eventually) made available to permanent transactions no matter how many times
they roll.


Re-logging Explained
====================

XFS allows multiple separate modifications to a single object to be carried in
the log at any given time.  This allows the log to avoid needing to flush each
change to disk before recording a new change to the object. XFS does this via a
method called "re-logging". Conceptually, this is quite simple - all it requires
is that any new change to the object is recorded with a *new copy* of all the
existing changes in the new transaction that is written to the log.

That is, if we have a sequence of changes A through to F, and the object was
written to disk after change D, we would see in the log the following series
of transactions, their contents and the log sequence number (LSN) of the
transaction::

	Transaction		Contents	LSN
	   A			   A		   X
	   B			  A+B		  X+n
	   C			 A+B+C		 X+n+m
	   D			A+B+C+D		X+n+m+o
	    <object written to disk>
	   E			   E		   Y (> X+n+m+o)
	   F			  E+F		  Y+p

In other words, each time an object is relogged, the new transaction contains
the aggregation of all the previous changes currently held only in the log.

This relogging technique allows objects to be moved forward in the log so that
an object being relogged does not prevent the tail of the log from ever moving
forward.  This can be seen in the table above by the changing (increasing) LSN
of each subsequent transaction, and it's the technique that allows us to
implement long-running, multiple-commit permanent transactions. 

A typical example of a rolling transaction is the removal of extents from an
inode which can only be done at a rate of two extents per transaction because
of reservation size limitations. Hence a rolling extent removal transaction
keeps relogging the inode and btree buffers as they get modified in each
removal operation. This keeps them moving forward in the log as the operation
progresses, ensuring that current operation never gets blocked by itself if the
log wraps around.

Hence it can be seen that the relogging operation is fundamental to the correct
working of the XFS journalling subsystem. From the above description, most
people should be able to see why the XFS metadata operations writes so much to
the log - repeated operations to the same objects write the same changes to
the log over and over again. Worse is the fact that objects tend to get
dirtier as they get relogged, so each subsequent transaction is writing more
metadata into the log.

It should now also be obvious how relogging and asynchronous transactions go
hand in hand. That is, transactions don't get written to the physical journal
until either a log buffer is filled (a log buffer can hold multiple
transactions) or a synchronous operation forces the log buffers holding the
transactions to disk. This means that XFS is doing aggregation of transactions
in memory - batching them, if you like - to minimise the impact of the log IO on
transaction throughput.

The limitation on asynchronous transaction throughput is the number and size of
log buffers made available by the log manager. By default there are 8 log
buffers available and the size of each is 32kB - the size can be increased up
to 256kB by use of a mount option.

Effectively, this gives us the maximum bound of outstanding metadata changes
that can be made to the filesystem at any point in time - if all the log
buffers are full and under IO, then no more transactions can be committed until
the current batch completes. It is now common for a single current CPU core to
be to able to issue enough transactions to keep the log buffers full and under
IO permanently. Hence the XFS journalling subsystem can be considered to be IO
bound.

Delayed Logging: Concepts
=========================

The key thing to note about the asynchronous logging combined with the
relogging technique XFS uses is that we can be relogging changed objects
multiple times before they are committed to disk in the log buffers. If we
return to the previous relogging example, it is entirely possible that
transactions A through D are committed to disk in the same log buffer.

That is, a single log buffer may contain multiple copies of the same object,
but only one of those copies needs to be there - the last one "D", as it
contains all the changes from the previous changes. In other words, we have one
necessary copy in the log buffer, and three stale copies that are simply
wasting space. When we are doing repeated operations on the same set of
objects, these "stale objects" can be over 90% of the space used in the log
buffers. It is clear that reducing the number of stale objects written to the
log would greatly reduce the amount of metadata we write to the log, and this
is the fundamental goal of delayed logging.

From a conceptual point of view, XFS is already doing relogging in memory (where
memory == log buffer), only it is doing it extremely inefficiently. It is using
logical to physical formatting to do the relogging because there is no
infrastructure to keep track of logical changes in memory prior to physically
formatting the changes in a transaction to the log buffer. Hence we cannot avoid
accumulating stale objects in the log buffers.

Delayed logging is the name we've given to keeping and tracking transactional
changes to objects in memory outside the log buffer infrastructure. Because of
the relogging concept fundamental to the XFS journalling subsystem, this is
actually relatively easy to do - all the changes to logged items are already
tracked in the current infrastructure. The big problem is how to accumulate
them and get them to the log in a consistent, recoverable manner.
Describing the problems and how they have been solved is the focus of this
document.

One of the key changes that delayed logging makes to the operation of the
journalling subsystem is that it disassociates the amount of outstanding
metadata changes from the size and number of log buffers available. In other
words, instead of there only being a maximum of 2MB of transaction changes not
written to the log at any point in time, there may be a much greater amount
being accumulated in memory. Hence the potential for loss of metadata on a
crash is much greater than for the existing logging mechanism.

It should be noted that this does not change the guarantee that log recovery
will result in a consistent filesystem. What it does mean is that as far as the
recovered filesystem is concerned, there may be many thousands of transactions
that simply did not occur as a result of the crash. This makes it even more
important that applications that care about their data use fsync() where they
need to ensure application level data integrity is maintained.

It should be noted that delayed logging is not an innovative new concept that
warrants rigorous proofs to determine whether it is correct or not. The method
of accumulating changes in memory for some period before writing them to the
log is used effectively in many filesystems including ext3 and ext4. Hence
no time is spent in this document trying to convince the reader that the
concept is sound. Instead it is simply considered a "solved problem" and as
such implementing it in XFS is purely an exercise in software engineering.

The fundamental requirements for delayed logging in XFS are simple:

	1. Reduce the amount of metadata written to the log by at least
	   an order of magnitude.
	2. Supply sufficient statistics to validate Requirement #1.
	3. Supply sufficient new tracing infrastructure to be able to debug
	   problems with the new code.
	4. No on-disk format change (metadata or log format).
	5. Enable and disable with a mount option.
	6. No performance regressions for synchronous transaction workloads.

Delayed Logging: Design
=======================

Storing Changes
---------------

The problem with accumulating changes at a logical level (i.e. just using the
existing log item dirty region tracking) is that when it comes to writing the
changes to the log buffers, we need to ensure that the object we are formatting
is not changing while we do this. This requires locking the object to prevent
concurrent modification. Hence flushing the logical changes to the log would
require us to lock every object, format them, and then unlock them again.

This introduces lots of scope for deadlocks with transactions that are already
running. For example, a transaction has object A locked and modified, but needs
the delayed logging tracking lock to commit the transaction. However, the
flushing thread has the delayed logging tracking lock already held, and is
trying to get the lock on object A to flush it to the log buffer. This appears
to be an unsolvable deadlock condition, and it was solving this problem that
was the barrier to implementing delayed logging for so long.

The solution is relatively simple - it just took a long time to recognise it.
Put simply, the current logging code formats the changes to each item into an
vector array that points to the changed regions in the item. The log write code
simply copies the memory these vectors point to into the log buffer during
transaction commit while the item is locked in the transaction. Instead of
using the log buffer as the destination of the formatting code, we can use an
allocated memory buffer big enough to fit the formatted vector.

If we then copy the vector into the memory buffer and rewrite the vector to
point to the memory buffer rather than the object itself, we now have a copy of
the changes in a format that is compatible with the log buffer writing code.
that does not require us to lock the item to access. This formatting and
rewriting can all be done while the object is locked during transaction commit,
resulting in a vector that is transactionally consistent and can be accessed
without needing to lock the owning item.

Hence we avoid the need to lock items when we need to flush outstanding
asynchronous transactions to the log. The differences between the existing
formatting method and the delayed logging formatting can be seen in the
diagram below.

Current format log vector::

    Object    +---------------------------------------------+
    Vector 1      +----+
    Vector 2                    +----+
    Vector 3                                   +----------+

After formatting::

    Log Buffer    +-V1-+-V2-+----V3----+

Delayed logging vector::

    Object    +---------------------------------------------+
    Vector 1      +----+
    Vector 2                    +----+
    Vector 3                                   +----------+

After formatting::

    Memory Buffer +-V1-+-V2-+----V3----+
    Vector 1      +----+
    Vector 2           +----+
    Vector 3                +----------+

The memory buffer and associated vector need to be passed as a single object,
but still need to be associated with the parent object so if the object is
relogged we can replace the current memory buffer with a new memory buffer that
contains the latest changes.

The reason for keeping the vector around after we've formatted the memory
buffer is to support splitting vectors across log buffer boundaries correctly.
If we don't keep the vector around, we do not know where the region boundaries
are in the item, so we'd need a new encapsulation method for regions in the log
buffer writing (i.e. double encapsulation). This would be an on-disk format
change and as such is not desirable.  It also means we'd have to write the log
region headers in the formatting stage, which is problematic as there is per
region state that needs to be placed into the headers during the log write.

Hence we need to keep the vector, but by attaching the memory buffer to it and
rewriting the vector addresses to point at the memory buffer we end up with a
self-describing object that can be passed to the log buffer write code to be
handled in exactly the same manner as the existing log vectors are handled.
Hence we avoid needing a new on-disk format to handle items that have been
relogged in memory.


Tracking Changes
----------------

Now that we can record transactional changes in memory in a form that allows
them to be used without limitations, we need to be able to track and accumulate
them so that they can be written to the log at some later point in time.  The
log item is the natural place to store this vector and buffer, and also makes sense
to be the object that is used to track committed objects as it will always
exist once the object has been included in a transaction.

The log item is already used to track the log items that have been written to
the log but not yet written to disk. Such log items are considered "active"
and as such are stored in the Active Item List (AIL) which is a LSN-ordered
double linked list. Items are inserted into this list during log buffer IO
completion, after which they are unpinned and can be written to disk. An object
that is in the AIL can be relogged, which causes the object to be pinned again
and then moved forward in the AIL when the log buffer IO completes for that
transaction.

Essentially, this shows that an item that is in the AIL can still be modified
and relogged, so any tracking must be separate to the AIL infrastructure. As
such, we cannot reuse the AIL list pointers for tracking committed items, nor
can we store state in any field that is protected by the AIL lock. Hence the
committed item tracking needs its own locks, lists and state fields in the log
item.

Similar to the AIL, tracking of committed items is done through a new list
called the Committed Item List (CIL).  The list tracks log items that have been
committed and have formatted memory buffers attached to them. It tracks objects
in transaction commit order, so when an object is relogged it is removed from
its place in the list and re-inserted at the tail. This is entirely arbitrary
and done to make it easy for debugging - the last items in the list are the
ones that are most recently modified. Ordering of the CIL is not necessary for
transactional integrity (as discussed in the next section) so the ordering is
done for convenience/sanity of the developers.


Delayed Logging: Checkpoints
----------------------------

When we have a log synchronisation event, commonly known as a "log force",
all the items in the CIL must be written into the log via the log buffers.
We need to write these items in the order that they exist in the CIL, and they
need to be written as an atomic transaction. The need for all the objects to be
written as an atomic transaction comes from the requirements of relogging and
log replay - all the changes in all the objects in a given transaction must
either be completely replayed during log recovery, or not replayed at all. If
a transaction is not replayed because it is not complete in the log, then
no later transactions should be replayed, either.

To fulfill this requirement, we need to write the entire CIL in a single log
transaction. Fortunately, the XFS log code has no fixed limit on the size of a
transaction, nor does the log replay code. The only fundamental limit is that
the transaction cannot be larger than just under half the size of the log.  The
reason for this limit is that to find the head and tail of the log, there must
be at least one complete transaction in the log at any given time. If a
transaction is larger than half the log, then there is the possibility that a
crash during the write of a such a transaction could partially overwrite the
only complete previous transaction in the log. This will result in a recovery
failure and an inconsistent filesystem and hence we must enforce the maximum
size of a checkpoint to be slightly less than a half the log.

Apart from this size requirement, a checkpoint transaction looks no different
to any other transaction - it contains a transaction header, a series of
formatted log items and a commit record at the tail. From a recovery
perspective, the checkpoint transaction is also no different - just a lot
bigger with a lot more items in it. The worst case effect of this is that we
might need to tune the recovery transaction object hash size.

Because the checkpoint is just another transaction and all the changes to log
items are stored as log vectors, we can use the existing log buffer writing
code to write the changes into the log. To do this efficiently, we need to
minimise the time we hold the CIL locked while writing the checkpoint
transaction. The current log write code enables us to do this easily with the
way it separates the writing of the transaction contents (the log vectors) from
the transaction commit record, but tracking this requires us to have a
per-checkpoint context that travels through the log write process through to
checkpoint completion.

Hence a checkpoint has a context that tracks the state of the current
checkpoint from initiation to checkpoint completion. A new context is initiated
at the same time a checkpoint transaction is started. That is, when we remove
all the current items from the CIL during a checkpoint operation, we move all
those changes into the current checkpoint context. We then initialise a new
context and attach that to the CIL for aggregation of new transactions.

This allows us to unlock the CIL immediately after transfer of all the
committed items and effectively allows new transactions to be issued while we
are formatting the checkpoint into the log. It also allows concurrent
checkpoints to be written into the log buffers in the case of log force heavy
workloads, just like the existing transaction commit code does. This, however,
requires that we strictly order the commit records in the log so that
checkpoint sequence order is maintained during log replay.

To ensure that we can be writing an item into a checkpoint transaction at
the same time another transaction modifies the item and inserts the log item
into the new CIL, then checkpoint transaction commit code cannot use log items
to store the list of log vectors that need to be written into the transaction.
Hence log vectors need to be able to be chained together to allow them to be
detached from the log items. That is, when the CIL is flushed the memory
buffer and log vector attached to each log item needs to be attached to the
checkpoint context so that the log item can be released. In diagrammatic form,
the CIL would look like this before the flush::

	CIL Head
	   |
	   V
	Log Item <-> log vector 1	-> memory buffer
	   |				-> vector array
	   V
	Log Item <-> log vector 2	-> memory buffer
	   |				-> vector array
	   V
	......
	   |
	   V
	Log Item <-> log vector N-1	-> memory buffer
	   |				-> vector array
	   V
	Log Item <-> log vector N	-> memory buffer
					-> vector array

And after the flush the CIL head is empty, and the checkpoint context log
vector list would look like::

	Checkpoint Context
	   |
	   V
	log vector 1	-> memory buffer
	   |		-> vector array
	   |		-> Log Item
	   V
	log vector 2	-> memory buffer
	   |		-> vector array
	   |		-> Log Item
	   V
	......
	   |
	   V
	log vector N-1	-> memory buffer
	   |		-> vector array
	   |		-> Log Item
	   V
	log vector N	-> memory buffer
			-> vector array
			-> Log Item

Once this transfer is done, the CIL can be unlocked and new transactions can
start, while the checkpoint flush code works over the log vector chain to
commit the checkpoint.

Once the checkpoint is written into the log buffers, the checkpoint context is
attached to the log buffer that the commit record was written to along with a
completion callback. Log IO completion will call that callback, which can then
run transaction committed processing for the log items (i.e. insert into AIL
and unpin) in the log vector chain and then free the log vector chain and
checkpoint context.

Discussion Point: I am uncertain as to whether the log item is the most
efficient way to track vectors, even though it seems like the natural way to do
it. The fact that we walk the log items (in the CIL) just to chain the log
vectors and break the link between the log item and the log vector means that
we take a cache line hit for the log item list modification, then another for
the log vector chaining. If we track by the log vectors, then we only need to
break the link between the log item and the log vector, which means we should
dirty only the log item cachelines. Normally I wouldn't be concerned about one
vs two dirty cachelines except for the fact I've seen upwards of 80,000 log
vectors in one checkpoint transaction. I'd guess this is a "measure and
compare" situation that can be done after a working and reviewed implementation
is in the dev tree....

Delayed Logging: Checkpoint Sequencing
--------------------------------------

One of the key aspects of the XFS transaction subsystem is that it tags
committed transactions with the log sequence number of the transaction commit.
This allows transactions to be issued asynchronously even though there may be
future operations that cannot be completed until that transaction is fully
committed to the log. In the rare case that a dependent operation occurs (e.g.
re-using a freed metadata extent for a data extent), a special, optimised log
force can be issued to force the dependent transaction to disk immediately.

To do this, transactions need to record the LSN of the commit record of the
transaction. This LSN comes directly from the log buffer the transaction is
written into. While this works just fine for the existing transaction
mechanism, it does not work for delayed logging because transactions are not
written directly into the log buffers. Hence some other method of sequencing
transactions is required.

As discussed in the checkpoint section, delayed logging uses per-checkpoint
contexts, and as such it is simple to assign a sequence number to each
checkpoint. Because the switching of checkpoint contexts must be done
atomically, it is simple to ensure that each new context has a monotonically
increasing sequence number assigned to it without the need for an external
atomic counter - we can just take the current context sequence number and add
one to it for the new context.

Then, instead of assigning a log buffer LSN to the transaction commit LSN
during the commit, we can assign the current checkpoint sequence. This allows
operations that track transactions that have not yet completed know what
checkpoint sequence needs to be committed before they can continue. As a
result, the code that forces the log to a specific LSN now needs to ensure that
the log forces to a specific checkpoint.

To ensure that we can do this, we need to track all the checkpoint contexts
that are currently committing to the log. When we flush a checkpoint, the
context gets added to a "committing" list which can be searched. When a
checkpoint commit completes, it is removed from the committing list. Because
the checkpoint context records the LSN of the commit record for the checkpoint,
we can also wait on the log buffer that contains the commit record, thereby
using the existing log force mechanisms to execute synchronous forces.

It should be noted that the synchronous forces may need to be extended with
mitigation algorithms similar to the current log buffer code to allow
aggregation of multiple synchronous transactions if there are already
synchronous transactions being flushed. Investigation of the performance of the
current design is needed before making any decisions here.

The main concern with log forces is to ensure that all the previous checkpoints
are also committed to disk before the one we need to wait for. Therefore we
need to check that all the prior contexts in the committing list are also
complete before waiting on the one we need to complete. We do this
synchronisation in the log force code so that we don't need to wait anywhere
else for such serialisation - it only matters when we do a log force.

The only remaining complexity is that a log force now also has to handle the
case where the forcing sequence number is the same as the current context. That
is, we need to flush the CIL and potentially wait for it to complete. This is a
simple addition to the existing log forcing code to check the sequence numbers
and push if required. Indeed, placing the current sequence checkpoint flush in
the log force code enables the current mechanism for issuing synchronous
transactions to remain untouched (i.e. commit an asynchronous transaction, then
force the log at the LSN of that transaction) and so the higher level code
behaves the same regardless of whether delayed logging is being used or not.

Delayed Logging: Checkpoint Log Space Accounting
------------------------------------------------

The big issue for a checkpoint transaction is the log space reservation for the
transaction. We don't know how big a checkpoint transaction is going to be
ahead of time, nor how many log buffers it will take to write out, nor the
number of split log vector regions are going to be used. We can track the
amount of log space required as we add items to the commit item list, but we
still need to reserve the space in the log for the checkpoint.

A typical transaction reserves enough space in the log for the worst case space
usage of the transaction. The reservation accounts for log record headers,
transaction and region headers, headers for split regions, buffer tail padding,
etc. as well as the actual space for all the changed metadata in the
transaction. While some of this is fixed overhead, much of it is dependent on
the size of the transaction and the number of regions being logged (the number
of log vectors in the transaction).

An example of the differences would be logging directory changes versus logging
inode changes. If you modify lots of inode cores (e.g. ``chmod -R g+w *``), then
there are lots of transactions that only contain an inode core and an inode log
format structure. That is, two vectors totaling roughly 150 bytes. If we modify
10,000 inodes, we have about 1.5MB of metadata to write in 20,000 vectors. Each
vector is 12 bytes, so the total to be logged is approximately 1.75MB. In
comparison, if we are logging full directory buffers, they are typically 4KB
each, so we in 1.5MB of directory buffers we'd have roughly 400 buffers and a
buffer format structure for each buffer - roughly 800 vectors or 1.51MB total
space.  From this, it should be obvious that a static log space reservation is
not particularly flexible and is difficult to select the "optimal value" for
all workloads.

Further, if we are going to use a static reservation, which bit of the entire
reservation does it cover? We account for space used by the transaction
reservation by tracking the space currently used by the object in the CIL and
then calculating the increase or decrease in space used as the object is
relogged. This allows for a checkpoint reservation to only have to account for
log buffer metadata used such as log header records.

However, even using a static reservation for just the log metadata is
problematic. Typically log record headers use at least 16KB of log space per
1MB of log space consumed (512 bytes per 32k) and the reservation needs to be
large enough to handle arbitrary sized checkpoint transactions. This
reservation needs to be made before the checkpoint is started, and we need to
be able to reserve the space without sleeping.  For a 8MB checkpoint, we need a
reservation of around 150KB, which is a non-trivial amount of space.

A static reservation needs to manipulate the log grant counters - we can take a
permanent reservation on the space, but we still need to make sure we refresh
the write reservation (the actual space available to the transaction) after
every checkpoint transaction completion. Unfortunately, if this space is not
available when required, then the regrant code will sleep waiting for it.

The problem with this is that it can lead to deadlocks as we may need to commit
checkpoints to be able to free up log space (refer back to the description of
rolling transactions for an example of this).  Hence we *must* always have
space available in the log if we are to use static reservations, and that is
very difficult and complex to arrange. It is possible to do, but there is a
simpler way.

The simpler way of doing this is tracking the entire log space used by the
items in the CIL and using this to dynamically calculate the amount of log
space required by the log metadata. If this log metadata space changes as a
result of a transaction commit inserting a new memory buffer into the CIL, then
the difference in space required is removed from the transaction that causes
the change. Transactions at this level will *always* have enough space
available in their reservation for this as they have already reserved the
maximal amount of log metadata space they require, and such a delta reservation
will always be less than or equal to the maximal amount in the reservation.

Hence we can grow the checkpoint transaction reservation dynamically as items
are added to the CIL and avoid the need for reserving and regranting log space
up front. This avoids deadlocks and removes a blocking point from the
checkpoint flush code.

As mentioned early, transactions can't grow to more than half the size of the
log. Hence as part of the reservation growing, we need to also check the size
of the reservation against the maximum allowed transaction size. If we reach
the maximum threshold, we need to push the CIL to the log. This is effectively
a "background flush" and is done on demand. This is identical to
a CIL push triggered by a log force, only that there is no waiting for the
checkpoint commit to complete. This background push is checked and executed by
transaction commit code.

If the transaction subsystem goes idle while we still have items in the CIL,
they will be flushed by the periodic log force issued by the xfssyncd. This log
force will push the CIL to disk, and if the transaction subsystem stays idle,
allow the idle log to be covered (effectively marked clean) in exactly the same
manner that is done for the existing logging method. A discussion point is
whether this log force needs to be done more frequently than the current rate
which is once every 30s.


Delayed Logging: Log Item Pinning
---------------------------------

Currently log items are pinned during transaction commit while the items are
still locked. This happens just after the items are formatted, though it could
be done any time before the items are unlocked. The result of this mechanism is
that items get pinned once for every transaction that is committed to the log
buffers. Hence items that are relogged in the log buffers will have a pin count
for every outstanding transaction they were dirtied in. When each of these
transactions is completed, they will unpin the item once. As a result, the item
only becomes unpinned when all the transactions complete and there are no
pending transactions. Thus the pinning and unpinning of a log item is symmetric
as there is a 1:1 relationship with transaction commit and log item completion.

For delayed logging, however, we have an asymmetric transaction commit to
completion relationship. Every time an object is relogged in the CIL it goes
through the commit process without a corresponding completion being registered.
That is, we now have a many-to-one relationship between transaction commit and
log item completion. The result of this is that pinning and unpinning of the
log items becomes unbalanced if we retain the "pin on transaction commit, unpin
on transaction completion" model.

To keep pin/unpin symmetry, the algorithm needs to change to a "pin on
insertion into the CIL, unpin on checkpoint completion". In other words, the
pinning and unpinning becomes symmetric around a checkpoint context. We have to
pin the object the first time it is inserted into the CIL - if it is already in
the CIL during a transaction commit, then we do not pin it again. Because there
can be multiple outstanding checkpoint contexts, we can still see elevated pin
counts, but as each checkpoint completes the pin count will retain the correct
value according to its context.

Just to make matters slightly more complex, this checkpoint level context
for the pin count means that the pinning of an item must take place under the
CIL commit/flush lock. If we pin the object outside this lock, we cannot
guarantee which context the pin count is associated with. This is because of
the fact pinning the item is dependent on whether the item is present in the
current CIL or not. If we don't pin the CIL first before we check and pin the
object, we have a race with CIL being flushed between the check and the pin
(or not pinning, as the case may be). Hence we must hold the CIL flush/commit
lock to guarantee that we pin the items correctly.

Delayed Logging: Concurrent Scalability
---------------------------------------

A fundamental requirement for the CIL is that accesses through transaction
commits must scale to many concurrent commits. The current transaction commit
code does not break down even when there are transactions coming from 2048
processors at once. The current transaction code does not go any faster than if
there was only one CPU using it, but it does not slow down either.

As a result, the delayed logging transaction commit code needs to be designed
for concurrency from the ground up. It is obvious that there are serialisation
points in the design - the three important ones are:

	1. Locking out new transaction commits while flushing the CIL
	2. Adding items to the CIL and updating item space accounting
	3. Checkpoint commit ordering

Looking at the transaction commit and CIL flushing interactions, it is clear
that we have a many-to-one interaction here. That is, the only restriction on
the number of concurrent transactions that can be trying to commit at once is
the amount of space available in the log for their reservations. The practical
limit here is in the order of several hundred concurrent transactions for a
128MB log, which means that it is generally one per CPU in a machine.

The amount of time a transaction commit needs to hold out a flush is a
relatively long period of time - the pinning of log items needs to be done
while we are holding out a CIL flush, so at the moment that means it is held
across the formatting of the objects into memory buffers (i.e. while memcpy()s
are in progress). Ultimately a two pass algorithm where the formatting is done
separately to the pinning of objects could be used to reduce the hold time of
the transaction commit side.

Because of the number of potential transaction commit side holders, the lock
really needs to be a sleeping lock - if the CIL flush takes the lock, we do not
want every other CPU in the machine spinning on the CIL lock. Given that
flushing the CIL could involve walking a list of tens of thousands of log
items, it will get held for a significant time and so spin contention is a
significant concern. Preventing lots of CPUs spinning doing nothing is the
main reason for choosing a sleeping lock even though nothing in either the
transaction commit or CIL flush side sleeps with the lock held.

It should also be noted that CIL flushing is also a relatively rare operation
compared to transaction commit for asynchronous transaction workloads - only
time will tell if using a read-write semaphore for exclusion will limit
transaction commit concurrency due to cache line bouncing of the lock on the
read side.

The second serialisation point is on the transaction commit side where items
are inserted into the CIL. Because transactions can enter this code
concurrently, the CIL needs to be protected separately from the above
commit/flush exclusion. It also needs to be an exclusive lock but it is only
held for a very short time and so a spin lock is appropriate here. It is
possible that this lock will become a contention point, but given the short
hold time once per transaction I think that contention is unlikely.

The final serialisation point is the checkpoint commit record ordering code
that is run as part of the checkpoint commit and log force sequencing. The code
path that triggers a CIL flush (i.e. whatever triggers the log force) will enter
an ordering loop after writing all the log vectors into the log buffers but
before writing the commit record. This loop walks the list of committing
checkpoints and needs to block waiting for checkpoints to complete their commit
record write. As a result it needs a lock and a wait variable. Log force
sequencing also requires the same lock, list walk, and blocking mechanism to
ensure completion of checkpoints.

These two sequencing operations can use the mechanism even though the
events they are waiting for are different. The checkpoint commit record
sequencing needs to wait until checkpoint contexts contain a commit LSN
(obtained through completion of a commit record write) while log force
sequencing needs to wait until previous checkpoint contexts are removed from
the committing list (i.e. they've completed). A simple wait variable and
broadcast wakeups (thundering herds) has been used to implement these two
serialisation queues. They use the same lock as the CIL, too. If we see too
much contention on the CIL lock, or too many context switches as a result of
the broadcast wakeups these operations can be put under a new spinlock and
given separate wait lists to reduce lock contention and the number of processes
woken by the wrong event.


Lifecycle Changes
-----------------

The existing log item life cycle is as follows::

	1. Transaction allocate
	2. Transaction reserve
	3. Lock item
	4. Join item to transaction
		If not already attached,
			Allocate log item
			Attach log item to owner item
		Attach log item to transaction
	5. Modify item
		Record modifications in log item
	6. Transaction commit
		Pin item in memory
		Format item into log buffer
		Write commit LSN into transaction
		Unlock item
		Attach transaction to log buffer

	<log buffer IO dispatched>
	<log buffer IO completes>

	7. Transaction completion
		Mark log item committed
		Insert log item into AIL
			Write commit LSN into log item
		Unpin log item
	8. AIL traversal
		Lock item
		Mark log item clean
		Flush item to disk

	<item IO completion>

	9. Log item removed from AIL
		Moves log tail
		Item unlocked

Essentially, steps 1-6 operate independently from step 7, which is also
independent of steps 8-9. An item can be locked in steps 1-6 or steps 8-9
at the same time step 7 is occurring, but only steps 1-6 or 8-9 can occur
at the same time. If the log item is in the AIL or between steps 6 and 7
and steps 1-6 are re-entered, then the item is relogged. Only when steps 8-9
are entered and completed is the object considered clean.

With delayed logging, there are new steps inserted into the life cycle::

	1. Transaction allocate
	2. Transaction reserve
	3. Lock item
	4. Join item to transaction
		If not already attached,
			Allocate log item
			Attach log item to owner item
		Attach log item to transaction
	5. Modify item
		Record modifications in log item
	6. Transaction commit
		Pin item in memory if not pinned in CIL
		Format item into log vector + buffer
		Attach log vector and buffer to log item
		Insert log item into CIL
		Write CIL context sequence into transaction
		Unlock item

	<next log force>

	7. CIL push
		lock CIL flush
		Chain log vectors and buffers together
		Remove items from CIL
		unlock CIL flush
		write log vectors into log
		sequence commit records
		attach checkpoint context to log buffer

	<log buffer IO dispatched>
	<log buffer IO completes>

	8. Checkpoint completion
		Mark log item committed
		Insert item into AIL
			Write commit LSN into log item
		Unpin log item
	9. AIL traversal
		Lock item
		Mark log item clean
		Flush item to disk
	<item IO completion>
	10. Log item removed from AIL
		Moves log tail
		Item unlocked

From this, it can be seen that the only life cycle differences between the two
logging methods are in the middle of the life cycle - they still have the same
beginning and end and execution constraints. The only differences are in the
committing of the log items to the log itself and the completion processing.
Hence delayed logging should not introduce any constraints on log item
behaviour, allocation or freeing that don't already exist.

As a result of this zero-impact "insertion" of delayed logging infrastructure
and the design of the internal structures to avoid on disk format changes, we
can basically switch between delayed logging and the existing mechanism with a
mount option. Fundamentally, there is no reason why the log manager would not
be able to swap methods automatically and transparently depending on load
characteristics, but this should not be necessary if delayed logging works as
designed.

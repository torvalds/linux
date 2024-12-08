.. SPDX-License-Identifier: GPL-2.0
.. _iomap_operations:

..
        Dumb style notes to maintain the author's sanity:
        Please try to start sentences on separate lines so that
        sentence changes don't bleed colors in diff.
        Heading decorations are documented in sphinx.rst.

=========================
Supported File Operations
=========================

.. contents:: Table of Contents
   :local:

Below are a discussion of the high level file operations that iomap
implements.

Buffered I/O
============

Buffered I/O is the default file I/O path in Linux.
File contents are cached in memory ("pagecache") to satisfy reads and
writes.
Dirty cache will be written back to disk at some point that can be
forced via ``fsync`` and variants.

iomap implements nearly all the folio and pagecache management that
filesystems have to implement themselves under the legacy I/O model.
This means that the filesystem need not know the details of allocating,
mapping, managing uptodate and dirty state, or writeback of pagecache
folios.
Under the legacy I/O model, this was managed very inefficiently with
linked lists of buffer heads instead of the per-folio bitmaps that iomap
uses.
Unless the filesystem explicitly opts in to buffer heads, they will not
be used, which makes buffered I/O much more efficient, and the pagecache
maintainer much happier.

``struct address_space_operations``
-----------------------------------

The following iomap functions can be referenced directly from the
address space operations structure:

 * ``iomap_dirty_folio``
 * ``iomap_release_folio``
 * ``iomap_invalidate_folio``
 * ``iomap_is_partially_uptodate``

The following address space operations can be wrapped easily:

 * ``read_folio``
 * ``readahead``
 * ``writepages``
 * ``bmap``
 * ``swap_activate``

``struct iomap_folio_ops``
--------------------------

The ``->iomap_begin`` function for pagecache operations may set the
``struct iomap::folio_ops`` field to an ops structure to override
default behaviors of iomap:

.. code-block:: c

 struct iomap_folio_ops {
     struct folio *(*get_folio)(struct iomap_iter *iter, loff_t pos,
                                unsigned len);
     void (*put_folio)(struct inode *inode, loff_t pos, unsigned copied,
                       struct folio *folio);
     bool (*iomap_valid)(struct inode *inode, const struct iomap *iomap);
 };

iomap calls these functions:

  - ``get_folio``: Called to allocate and return an active reference to
    a locked folio prior to starting a write.
    If this function is not provided, iomap will call
    ``iomap_get_folio``.
    This could be used to `set up per-folio filesystem state
    <https://lore.kernel.org/all/20190429220934.10415-5-agruenba@redhat.com/>`_
    for a write.

  - ``put_folio``: Called to unlock and put a folio after a pagecache
    operation completes.
    If this function is not provided, iomap will ``folio_unlock`` and
    ``folio_put`` on its own.
    This could be used to `commit per-folio filesystem state
    <https://lore.kernel.org/all/20180619164137.13720-6-hch@lst.de/>`_
    that was set up by ``->get_folio``.

  - ``iomap_valid``: The filesystem may not hold locks between
    ``->iomap_begin`` and ``->iomap_end`` because pagecache operations
    can take folio locks, fault on userspace pages, initiate writeback
    for memory reclamation, or engage in other time-consuming actions.
    If a file's space mapping data are mutable, it is possible that the
    mapping for a particular pagecache folio can `change in the time it
    takes
    <https://lore.kernel.org/all/20221123055812.747923-8-david@fromorbit.com/>`_
    to allocate, install, and lock that folio.

    For the pagecache, races can happen if writeback doesn't take
    ``i_rwsem`` or ``invalidate_lock`` and updates mapping information.
    Races can also happen if the filesystem allows concurrent writes.
    For such files, the mapping *must* be revalidated after the folio
    lock has been taken so that iomap can manage the folio correctly.

    fsdax does not need this revalidation because there's no writeback
    and no support for unwritten extents.

    Filesystems subject to this kind of race must provide a
    ``->iomap_valid`` function to decide if the mapping is still valid.
    If the mapping is not valid, the mapping will be sampled again.

    To support making the validity decision, the filesystem's
    ``->iomap_begin`` function may set ``struct iomap::validity_cookie``
    at the same time that it populates the other iomap fields.
    A simple validation cookie implementation is a sequence counter.
    If the filesystem bumps the sequence counter every time it modifies
    the inode's extent map, it can be placed in the ``struct
    iomap::validity_cookie`` during ``->iomap_begin``.
    If the value in the cookie is found to be different to the value
    the filesystem holds when the mapping is passed back to
    ``->iomap_valid``, then the iomap should considered stale and the
    validation failed.

These ``struct kiocb`` flags are significant for buffered I/O with iomap:

 * ``IOCB_NOWAIT``: Turns on ``IOMAP_NOWAIT``.

Internal per-Folio State
------------------------

If the fsblock size matches the size of a pagecache folio, it is assumed
that all disk I/O operations will operate on the entire folio.
The uptodate (memory contents are at least as new as what's on disk) and
dirty (memory contents are newer than what's on disk) status of the
folio are all that's needed for this case.

If the fsblock size is less than the size of a pagecache folio, iomap
tracks the per-fsblock uptodate and dirty state itself.
This enables iomap to handle both "bs < ps" `filesystems
<https://lore.kernel.org/all/20230725122932.144426-1-ritesh.list@gmail.com/>`_
and large folios in the pagecache.

iomap internally tracks two state bits per fsblock:

 * ``uptodate``: iomap will try to keep folios fully up to date.
   If there are read(ahead) errors, those fsblocks will not be marked
   uptodate.
   The folio itself will be marked uptodate when all fsblocks within the
   folio are uptodate.

 * ``dirty``: iomap will set the per-block dirty state when programs
   write to the file.
   The folio itself will be marked dirty when any fsblock within the
   folio is dirty.

iomap also tracks the amount of read and write disk IOs that are in
flight.
This structure is much lighter weight than ``struct buffer_head``
because there is only one per folio, and the per-fsblock overhead is two
bits vs. 104 bytes.

Filesystems wishing to turn on large folios in the pagecache should call
``mapping_set_large_folios`` when initializing the incore inode.

Buffered Readahead and Reads
----------------------------

The ``iomap_readahead`` function initiates readahead to the pagecache.
The ``iomap_read_folio`` function reads one folio's worth of data into
the pagecache.
The ``flags`` argument to ``->iomap_begin`` will be set to zero.
The pagecache takes whatever locks it needs before calling the
filesystem.

Buffered Writes
---------------

The ``iomap_file_buffered_write`` function writes an ``iocb`` to the
pagecache.
``IOMAP_WRITE`` or ``IOMAP_WRITE`` | ``IOMAP_NOWAIT`` will be passed as
the ``flags`` argument to ``->iomap_begin``.
Callers commonly take ``i_rwsem`` in either shared or exclusive mode
before calling this function.

mmap Write Faults
~~~~~~~~~~~~~~~~~

The ``iomap_page_mkwrite`` function handles a write fault to a folio in
the pagecache.
``IOMAP_WRITE | IOMAP_FAULT`` will be passed as the ``flags`` argument
to ``->iomap_begin``.
Callers commonly take the mmap ``invalidate_lock`` in shared or
exclusive mode before calling this function.

Buffered Write Failures
~~~~~~~~~~~~~~~~~~~~~~~

After a short write to the pagecache, the areas not written will not
become marked dirty.
The filesystem must arrange to `cancel
<https://lore.kernel.org/all/20221123055812.747923-6-david@fromorbit.com/>`_
such `reservations
<https://lore.kernel.org/linux-xfs/20220817093627.GZ3600936@dread.disaster.area/>`_
because writeback will not consume the reservation.
The ``iomap_write_delalloc_release`` can be called from a
``->iomap_end`` function to find all the clean areas of the folios
caching a fresh (``IOMAP_F_NEW``) delalloc mapping.
It takes the ``invalidate_lock``.

The filesystem must supply a function ``punch`` to be called for
each file range in this state.
This function must *only* remove delayed allocation reservations, in
case another thread racing with the current thread writes successfully
to the same region and triggers writeback to flush the dirty data out to
disk.

Zeroing for File Operations
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Filesystems can call ``iomap_zero_range`` to perform zeroing of the
pagecache for non-truncation file operations that are not aligned to
the fsblock size.
``IOMAP_ZERO`` will be passed as the ``flags`` argument to
``->iomap_begin``.
Callers typically hold ``i_rwsem`` and ``invalidate_lock`` in exclusive
mode before calling this function.

Unsharing Reflinked File Data
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Filesystems can call ``iomap_file_unshare`` to force a file sharing
storage with another file to preemptively copy the shared data to newly
allocate storage.
``IOMAP_WRITE | IOMAP_UNSHARE`` will be passed as the ``flags`` argument
to ``->iomap_begin``.
Callers typically hold ``i_rwsem`` and ``invalidate_lock`` in exclusive
mode before calling this function.

Truncation
----------

Filesystems can call ``iomap_truncate_page`` to zero the bytes in the
pagecache from EOF to the end of the fsblock during a file truncation
operation.
``truncate_setsize`` or ``truncate_pagecache`` will take care of
everything after the EOF block.
``IOMAP_ZERO`` will be passed as the ``flags`` argument to
``->iomap_begin``.
Callers typically hold ``i_rwsem`` and ``invalidate_lock`` in exclusive
mode before calling this function.

Pagecache Writeback
-------------------

Filesystems can call ``iomap_writepages`` to respond to a request to
write dirty pagecache folios to disk.
The ``mapping`` and ``wbc`` parameters should be passed unchanged.
The ``wpc`` pointer should be allocated by the filesystem and must
be initialized to zero.

The pagecache will lock each folio before trying to schedule it for
writeback.
It does not lock ``i_rwsem`` or ``invalidate_lock``.

The dirty bit will be cleared for all folios run through the
``->map_blocks`` machinery described below even if the writeback fails.
This is to prevent dirty folio clots when storage devices fail; an
``-EIO`` is recorded for userspace to collect via ``fsync``.

The ``ops`` structure must be specified and is as follows:

``struct iomap_writeback_ops``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

 struct iomap_writeback_ops {
     int (*map_blocks)(struct iomap_writepage_ctx *wpc, struct inode *inode,
                       loff_t offset, unsigned len);
     int (*prepare_ioend)(struct iomap_ioend *ioend, int status);
     void (*discard_folio)(struct folio *folio, loff_t pos);
 };

The fields are as follows:

  - ``map_blocks``: Sets ``wpc->iomap`` to the space mapping of the file
    range (in bytes) given by ``offset`` and ``len``.
    iomap calls this function for each dirty fs block in each dirty folio,
    though it will `reuse mappings
    <https://lore.kernel.org/all/20231207072710.176093-15-hch@lst.de/>`_
    for runs of contiguous dirty fsblocks within a folio.
    Do not return ``IOMAP_INLINE`` mappings here; the ``->iomap_end``
    function must deal with persisting written data.
    Do not return ``IOMAP_DELALLOC`` mappings here; iomap currently
    requires mapping to allocated space.
    Filesystems can skip a potentially expensive mapping lookup if the
    mappings have not changed.
    This revalidation must be open-coded by the filesystem; it is
    unclear if ``iomap::validity_cookie`` can be reused for this
    purpose.
    This function must be supplied by the filesystem.

  - ``prepare_ioend``: Enables filesystems to transform the writeback
    ioend or perform any other preparatory work before the writeback I/O
    is submitted.
    This might include pre-write space accounting updates, or installing
    a custom ``->bi_end_io`` function for internal purposes, such as
    deferring the ioend completion to a workqueue to run metadata update
    transactions from process context.
    This function is optional.

  - ``discard_folio``: iomap calls this function after ``->map_blocks``
    fails to schedule I/O for any part of a dirty folio.
    The function should throw away any reservations that may have been
    made for the write.
    The folio will be marked clean and an ``-EIO`` recorded in the
    pagecache.
    Filesystems can use this callback to `remove
    <https://lore.kernel.org/all/20201029163313.1766967-1-bfoster@redhat.com/>`_
    delalloc reservations to avoid having delalloc reservations for
    clean pagecache.
    This function is optional.

Pagecache Writeback Completion
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To handle the bookkeeping that must happen after disk I/O for writeback
completes, iomap creates chains of ``struct iomap_ioend`` objects that
wrap the ``bio`` that is used to write pagecache data to disk.
By default, iomap finishes writeback ioends by clearing the writeback
bit on the folios attached to the ``ioend``.
If the write failed, it will also set the error bits on the folios and
the address space.
This can happen in interrupt or process context, depending on the
storage device.

Filesystems that need to update internal bookkeeping (e.g. unwritten
extent conversions) should provide a ``->prepare_ioend`` function to
set ``struct iomap_end::bio::bi_end_io`` to its own function.
This function should call ``iomap_finish_ioends`` after finishing its
own work (e.g. unwritten extent conversion).

Some filesystems may wish to `amortize the cost of running metadata
transactions
<https://lore.kernel.org/all/20220120034733.221737-1-david@fromorbit.com/>`_
for post-writeback updates by batching them.
They may also require transactions to run from process context, which
implies punting batches to a workqueue.
iomap ioends contain a ``list_head`` to enable batching.

Given a batch of ioends, iomap has a few helpers to assist with
amortization:

 * ``iomap_sort_ioends``: Sort all the ioends in the list by file
   offset.

 * ``iomap_ioend_try_merge``: Given an ioend that is not in any list and
   a separate list of sorted ioends, merge as many of the ioends from
   the head of the list into the given ioend.
   ioends can only be merged if the file range and storage addresses are
   contiguous; the unwritten and shared status are the same; and the
   write I/O outcome is the same.
   The merged ioends become their own list.

 * ``iomap_finish_ioends``: Finish an ioend that possibly has other
   ioends linked to it.

Direct I/O
==========

In Linux, direct I/O is defined as file I/O that is issued directly to
storage, bypassing the pagecache.
The ``iomap_dio_rw`` function implements O_DIRECT (direct I/O) reads and
writes for files.

.. code-block:: c

 ssize_t iomap_dio_rw(struct kiocb *iocb, struct iov_iter *iter,
                      const struct iomap_ops *ops,
                      const struct iomap_dio_ops *dops,
                      unsigned int dio_flags, void *private,
                      size_t done_before);

The filesystem can provide the ``dops`` parameter if it needs to perform
extra work before or after the I/O is issued to storage.
The ``done_before`` parameter tells the how much of the request has
already been transferred.
It is used to continue a request asynchronously when `part of the
request
<https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=c03098d4b9ad76bca2966a8769dcfe59f7f85103>`_
has already been completed synchronously.

The ``done_before`` parameter should be set if writes for the ``iocb``
have been initiated prior to the call.
The direction of the I/O is determined from the ``iocb`` passed in.

The ``dio_flags`` argument can be set to any combination of the
following values:

 * ``IOMAP_DIO_FORCE_WAIT``: Wait for the I/O to complete even if the
   kiocb is not synchronous.

 * ``IOMAP_DIO_OVERWRITE_ONLY``: Perform a pure overwrite for this range
   or fail with ``-EAGAIN``.
   This can be used by filesystems with complex unaligned I/O
   write paths to provide an optimised fast path for unaligned writes.
   If a pure overwrite can be performed, then serialisation against
   other I/Os to the same filesystem block(s) is unnecessary as there is
   no risk of stale data exposure or data loss.
   If a pure overwrite cannot be performed, then the filesystem can
   perform the serialisation steps needed to provide exclusive access
   to the unaligned I/O range so that it can perform allocation and
   sub-block zeroing safely.
   Filesystems can use this flag to try to reduce locking contention,
   but a lot of `detailed checking
   <https://lore.kernel.org/linux-ext4/20230314130759.642710-1-bfoster@redhat.com/>`_
   is required to do it `correctly
   <https://lore.kernel.org/linux-ext4/20230810165559.946222-1-bfoster@redhat.com/>`_.

 * ``IOMAP_DIO_PARTIAL``: If a page fault occurs, return whatever
   progress has already been made.
   The caller may deal with the page fault and retry the operation.
   If the caller decides to retry the operation, it should pass the
   accumulated return values of all previous calls as the
   ``done_before`` parameter to the next call.

These ``struct kiocb`` flags are significant for direct I/O with iomap:

 * ``IOCB_NOWAIT``: Turns on ``IOMAP_NOWAIT``.

 * ``IOCB_SYNC``: Ensure that the device has persisted data to disk
   before completing the call.
   In the case of pure overwrites, the I/O may be issued with FUA
   enabled.

 * ``IOCB_HIPRI``: Poll for I/O completion instead of waiting for an
   interrupt.
   Only meaningful for asynchronous I/O, and only if the entire I/O can
   be issued as a single ``struct bio``.

 * ``IOCB_DIO_CALLER_COMP``: Try to run I/O completion from the caller's
   process context.
   See ``linux/fs.h`` for more details.

Filesystems should call ``iomap_dio_rw`` from ``->read_iter`` and
``->write_iter``, and set ``FMODE_CAN_ODIRECT`` in the ``->open``
function for the file.
They should not set ``->direct_IO``, which is deprecated.

If a filesystem wishes to perform its own work before direct I/O
completion, it should call ``__iomap_dio_rw``.
If its return value is not an error pointer or a NULL pointer, the
filesystem should pass the return value to ``iomap_dio_complete`` after
finishing its internal work.

Return Values
-------------

``iomap_dio_rw`` can return one of the following:

 * A non-negative number of bytes transferred.

 * ``-ENOTBLK``: Fall back to buffered I/O.
   iomap itself will return this value if it cannot invalidate the page
   cache before issuing the I/O to storage.
   The ``->iomap_begin`` or ``->iomap_end`` functions may also return
   this value.

 * ``-EIOCBQUEUED``: The asynchronous direct I/O request has been
   queued and will be completed separately.

 * Any of the other negative error codes.

Direct Reads
------------

A direct I/O read initiates a read I/O from the storage device to the
caller's buffer.
Dirty parts of the pagecache are flushed to storage before initiating
the read io.
The ``flags`` value for ``->iomap_begin`` will be ``IOMAP_DIRECT`` with
any combination of the following enhancements:

 * ``IOMAP_NOWAIT``, as defined previously.

Callers commonly hold ``i_rwsem`` in shared mode before calling this
function.

Direct Writes
-------------

A direct I/O write initiates a write I/O to the storage device from the
caller's buffer.
Dirty parts of the pagecache are flushed to storage before initiating
the write io.
The pagecache is invalidated both before and after the write io.
The ``flags`` value for ``->iomap_begin`` will be ``IOMAP_DIRECT |
IOMAP_WRITE`` with any combination of the following enhancements:

 * ``IOMAP_NOWAIT``, as defined previously.

 * ``IOMAP_OVERWRITE_ONLY``: Allocating blocks and zeroing partial
   blocks is not allowed.
   The entire file range must map to a single written or unwritten
   extent.
   The file I/O range must be aligned to the filesystem block size
   if the mapping is unwritten and the filesystem cannot handle zeroing
   the unaligned regions without exposing stale contents.

 * ``IOMAP_ATOMIC``: This write is being issued with torn-write
   protection.
   Only a single bio can be created for the write, and the write must
   not be split into multiple I/O requests, i.e. flag REQ_ATOMIC must be
   set.
   The file range to write must be aligned to satisfy the requirements
   of both the filesystem and the underlying block device's atomic
   commit capabilities.
   If filesystem metadata updates are required (e.g. unwritten extent
   conversion or copy on write), all updates for the entire file range
   must be committed atomically as well.
   Only one space mapping is allowed per untorn write.
   Untorn writes must be aligned to, and must not be longer than, a
   single file block.

Callers commonly hold ``i_rwsem`` in shared or exclusive mode before
calling this function.

``struct iomap_dio_ops:``
-------------------------
.. code-block:: c

 struct iomap_dio_ops {
     void (*submit_io)(const struct iomap_iter *iter, struct bio *bio,
                       loff_t file_offset);
     int (*end_io)(struct kiocb *iocb, ssize_t size, int error,
                   unsigned flags);
     struct bio_set *bio_set;
 };

The fields of this structure are as follows:

  - ``submit_io``: iomap calls this function when it has constructed a
    ``struct bio`` object for the I/O requested, and wishes to submit it
    to the block device.
    If no function is provided, ``submit_bio`` will be called directly.
    Filesystems that would like to perform additional work before (e.g.
    data replication for btrfs) should implement this function.

  - ``end_io``: This is called after the ``struct bio`` completes.
    This function should perform post-write conversions of unwritten
    extent mappings, handle write failures, etc.
    The ``flags`` argument may be set to a combination of the following:

    * ``IOMAP_DIO_UNWRITTEN``: The mapping was unwritten, so the ioend
      should mark the extent as written.

    * ``IOMAP_DIO_COW``: Writing to the space in the mapping required a
      copy on write operation, so the ioend should switch mappings.

  - ``bio_set``: This allows the filesystem to provide a custom bio_set
    for allocating direct I/O bios.
    This enables filesystems to `stash additional per-bio information
    <https://lore.kernel.org/all/20220505201115.937837-3-hch@lst.de/>`_
    for private use.
    If this field is NULL, generic ``struct bio`` objects will be used.

Filesystems that want to perform extra work after an I/O completion
should set a custom ``->bi_end_io`` function via ``->submit_io``.
Afterwards, the custom endio function must call
``iomap_dio_bio_end_io`` to finish the direct I/O.

DAX I/O
=======

Some storage devices can be directly mapped as memory.
These devices support a new access mode known as "fsdax" that allows
loads and stores through the CPU and memory controller.

fsdax Reads
-----------

A fsdax read performs a memcpy from storage device to the caller's
buffer.
The ``flags`` value for ``->iomap_begin`` will be ``IOMAP_DAX`` with any
combination of the following enhancements:

 * ``IOMAP_NOWAIT``, as defined previously.

Callers commonly hold ``i_rwsem`` in shared mode before calling this
function.

fsdax Writes
------------

A fsdax write initiates a memcpy to the storage device from the caller's
buffer.
The ``flags`` value for ``->iomap_begin`` will be ``IOMAP_DAX |
IOMAP_WRITE`` with any combination of the following enhancements:

 * ``IOMAP_NOWAIT``, as defined previously.

 * ``IOMAP_OVERWRITE_ONLY``: The caller requires a pure overwrite to be
   performed from this mapping.
   This requires the filesystem extent mapping to already exist as an
   ``IOMAP_MAPPED`` type and span the entire range of the write I/O
   request.
   If the filesystem cannot map this request in a way that allows the
   iomap infrastructure to perform a pure overwrite, it must fail the
   mapping operation with ``-EAGAIN``.

Callers commonly hold ``i_rwsem`` in exclusive mode before calling this
function.

fsdax mmap Faults
~~~~~~~~~~~~~~~~~

The ``dax_iomap_fault`` function handles read and write faults to fsdax
storage.
For a read fault, ``IOMAP_DAX | IOMAP_FAULT`` will be passed as the
``flags`` argument to ``->iomap_begin``.
For a write fault, ``IOMAP_DAX | IOMAP_FAULT | IOMAP_WRITE`` will be
passed as the ``flags`` argument to ``->iomap_begin``.

Callers commonly hold the same locks as they do to call their iomap
pagecache counterparts.

fsdax Truncation, fallocate, and Unsharing
------------------------------------------

For fsdax files, the following functions are provided to replace their
iomap pagecache I/O counterparts.
The ``flags`` argument to ``->iomap_begin`` are the same as the
pagecache counterparts, with ``IOMAP_DAX`` added.

 * ``dax_file_unshare``
 * ``dax_zero_range``
 * ``dax_truncate_page``

Callers commonly hold the same locks as they do to call their iomap
pagecache counterparts.

fsdax Deduplication
-------------------

Filesystems implementing the ``FIDEDUPERANGE`` ioctl must call the
``dax_remap_file_range_prep`` function with their own iomap read ops.

Seeking Files
=============

iomap implements the two iterating whence modes of the ``llseek`` system
call.

SEEK_DATA
---------

The ``iomap_seek_data`` function implements the SEEK_DATA "whence" value
for llseek.
``IOMAP_REPORT`` will be passed as the ``flags`` argument to
``->iomap_begin``.

For unwritten mappings, the pagecache will be searched.
Regions of the pagecache with a folio mapped and uptodate fsblocks
within those folios will be reported as data areas.

Callers commonly hold ``i_rwsem`` in shared mode before calling this
function.

SEEK_HOLE
---------

The ``iomap_seek_hole`` function implements the SEEK_HOLE "whence" value
for llseek.
``IOMAP_REPORT`` will be passed as the ``flags`` argument to
``->iomap_begin``.

For unwritten mappings, the pagecache will be searched.
Regions of the pagecache with no folio mapped, or a !uptodate fsblock
within a folio will be reported as sparse hole areas.

Callers commonly hold ``i_rwsem`` in shared mode before calling this
function.

Swap File Activation
====================

The ``iomap_swapfile_activate`` function finds all the base-page aligned
regions in a file and sets them up as swap space.
The file will be ``fsync()``'d before activation.
``IOMAP_REPORT`` will be passed as the ``flags`` argument to
``->iomap_begin``.
All mappings must be mapped or unwritten; cannot be dirty or shared, and
cannot span multiple block devices.
Callers must hold ``i_rwsem`` in exclusive mode; this is already
provided by ``swapon``.

File Space Mapping Reporting
============================

iomap implements two of the file space mapping system calls.

FS_IOC_FIEMAP
-------------

The ``iomap_fiemap`` function exports file extent mappings to userspace
in the format specified by the ``FS_IOC_FIEMAP`` ioctl.
``IOMAP_REPORT`` will be passed as the ``flags`` argument to
``->iomap_begin``.
Callers commonly hold ``i_rwsem`` in shared mode before calling this
function.

FIBMAP (deprecated)
-------------------

``iomap_bmap`` implements FIBMAP.
The calling conventions are the same as for FIEMAP.
This function is only provided to maintain compatibility for filesystems
that implemented FIBMAP prior to conversion.
This ioctl is deprecated; do **not** add a FIBMAP implementation to
filesystems that do not have it.
Callers should probably hold ``i_rwsem`` in shared mode before calling
this function, but this is unclear.

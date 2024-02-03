.. SPDX-License-Identifier: GPL-2.0-only

================
Design of dm-vdo
================

The dm-vdo (virtual data optimizer) target provides inline deduplication,
compression, zero-block elimination, and thin provisioning. A dm-vdo target
can be backed by up to 256TB of storage, and can present a logical size of
up to 4PB. This target was originally developed at Permabit Technology
Corp. starting in 2009. It was first released in 2013 and has been used in
production environments ever since. It was made open-source in 2017 after
Permabit was acquired by Red Hat. This document describes the design of
dm-vdo. For usage, see vdo.rst in the same directory as this file.

Because deduplication rates fall drastically as the block size increases, a
vdo target has a maximum block size of 4K. However, it can achieve
deduplication rates of 254:1, i.e. up to 254 copies of a given 4K block can
reference a single 4K of actual storage. It can achieve compression rates
of 14:1. All zero blocks consume no storage at all.

Theory of Operation
===================

The design of dm-vdo is based on the idea that deduplication is a two-part
problem. The first is to recognize duplicate data. The second is to avoid
storing multiple copies of those duplicates. Therefore, dm-vdo has two main
parts: a deduplication index (called UDS) that is used to discover
duplicate data, and a data store with a reference counted block map that
maps from logical block addresses to the actual storage location of the
data.

Zones and Threading
-------------------

Due to the complexity of data optimization, the number of metadata
structures involved in a single write operation to a vdo target is larger
than most other targets. Furthermore, because vdo must operate on small
block sizes in order to achieve good deduplication rates, acceptable
performance can only be achieved through parallelism. Therefore, vdo's
design attempts to be lock-free.

Most of a vdo's main data structures are designed to be easily divided into
"zones" such that any given bio must only access a single zone of any zoned
structure. Safety with minimal locking is achieved by ensuring that during
normal operation, each zone is assigned to a specific thread, and only that
thread will access the portion of the data structure in that zone.
Associated with each thread is a work queue. Each bio is associated with a
request object (the "data_vio") which will be added to a work queue when
the next phase of its operation requires access to the structures in the
zone associated with that queue.

Another way of thinking about this arrangement is that the work queue for
each zone has an implicit lock on the structures it manages for all its
operations, because vdo guarantees that no other thread will alter those
structures.

Although each structure is divided into zones, this division is not
reflected in the on-disk representation of each data structure. Therefore,
the number of zones for each structure, and hence the number of threads,
can be reconfigured each time a vdo target is started.

The Deduplication Index
-----------------------

In order to identify duplicate data efficiently, vdo was designed to
leverage some common characteristics of duplicate data. From empirical
observations, we gathered two key insights. The first is that in most data
sets with significant amounts of duplicate data, the duplicates tend to
have temporal locality. When a duplicate appears, it is more likely that
other duplicates will be detected, and that those duplicates will have been
written at about the same time. This is why the index keeps records in
temporal order. The second insight is that new data is more likely to
duplicate recent data than it is to duplicate older data and in general,
there are diminishing returns to looking further back in time. Therefore,
when the index is full, it should cull its oldest records to make space for
new ones. Another important idea behind the design of the index is that the
ultimate goal of deduplication is to reduce storage costs. Since there is a
trade-off between the storage saved and the resources expended to achieve
those savings, vdo does not attempt to find every last duplicate block. It
is sufficient to find and eliminate most of the redundancy.

Each block of data is hashed to produce a 16-byte block name. An index
record consists of this block name paired with the presumed location of
that data on the underlying storage. However, it is not possible to
guarantee that the index is accurate. In the most common case, this occurs
because it is too costly to update the index when a block is over-written
or discarded. Doing so would require either storing the block name along
with the blocks, which is difficult to do efficiently in block-based
storage, or reading and rehashing each block before overwriting it.
Inaccuracy can also result from a hash collision where two different blocks
have the same name. In practice, this is extremely unlikely, but because
vdo does not use a cryptographic hash, a malicious workload could be
constructed. Because of these inaccuracies, vdo treats the locations in the
index as hints, and reads each indicated block to verify that it is indeed
a duplicate before sharing the existing block with a new one.

Records are collected into groups called chapters. New records are added to
the newest chapter, called the open chapter. This chapter is stored in a
format optimized for adding and modifying records, and the content of the
open chapter is not finalized until it runs out of space for new records.
When the open chapter fills up, it is closed and a new open chapter is
created to collect new records.

Closing a chapter converts it to a different format which is optimized for
reading. The records are written to a series of record pages based on the
order in which they were received. This means that records with temporal
locality should be on a small number of pages, reducing the I/O required to
retrieve them. The chapter also compiles an index that indicates which
record page contains any given name. This index means that a request for a
name can determine exactly which record page may contain that record,
without having to load the entire chapter from storage. This index uses
only a subset of the block name as its key, so it cannot guarantee that an
index entry refers to the desired block name. It can only guarantee that if
there is a record for this name, it will be on the indicated page. Closed
chapters are read-only structures and their contents are never altered in
any way.

Once enough records have been written to fill up all the available index
space, the oldest chapter is removed to make space for new chapters. Any
time a request finds a matching record in the index, that record is copied
into the open chapter. This ensures that useful block names remain available
in the index, while unreferenced block names are forgotten over time.

In order to find records in older chapters, the index also maintains a
higher level structure called the volume index, which contains entries
mapping each block name to the chapter containing its newest record. This
mapping is updated as records for the block name are copied or updated,
ensuring that only the newest record for a given block name can be found.
An older record for a block name will no longer be found even though it has
not been deleted from its chapter. Like the chapter index, the volume index
uses only a subset of the block name as its key and can not definitively
say that a record exists for a name. It can only say which chapter would
contain the record if a record exists. The volume index is stored entirely
in memory and is saved to storage only when the vdo target is shut down.

From the viewpoint of a request for a particular block name, it will first
look up the name in the volume index. This search will either indicate that
the name is new, or which chapter to search. If it returns a chapter, the
request looks up its name in the chapter index. This will indicate either
that the name is new, or which record page to search. Finally, if it is not
new, the request will look for its name in the indicated record page.
This process may require up to two page reads per request (one for the
chapter index page and one for the request page). However, recently
accessed pages are cached so that these page reads can be amortized across
many block name requests.

The volume index and the chapter indexes are implemented using a
memory-efficient structure called a delta index. Instead of storing the
entire block name (the key) for each entry, the entries are sorted by name
and only the difference between adjacent keys (the delta) is stored.
Because we expect the hashes to be randomly distributed, the size of the
deltas follows an exponential distribution. Because of this distribution,
the deltas are expressed using a Huffman code to take up even less space.
The entire sorted list of keys is called a delta list. This structure
allows the index to use many fewer bytes per entry than a traditional hash
table, but it is slightly more expensive to look up entries, because a
request must read every entry in a delta list to add up the deltas in order
to find the record it needs. The delta index reduces this lookup cost by
splitting its key space into many sub-lists, each starting at a fixed key
value, so that each individual list is short.

The default index size can hold 64 million records, corresponding to about
256GB of data. This means that the index can identify duplicate data if the
original data was written within the last 256GB of writes. This range is
called the deduplication window. If new writes duplicate data that is older
than that, the index will not be able to find it because the records of the
older data have been removed. This means that if an application writes a
200 GB file to a vdo target and then immediately writes it again, the two
copies will deduplicate perfectly. Doing the same with a 500 GB file will
result in no deduplication, because the beginning of the file will no
longer be in the index by the time the second write begins (assuming there
is no duplication within the file itself).

If an application anticipates a data workload that will see useful
deduplication beyond the 256GB threshold, vdo can be configured to use a
larger index with a correspondingly larger deduplication window. (This
configuration can only be set when the target is created, not altered
later. It is important to consider the expected workload for a vdo target
before configuring it.)  There are two ways to do this.

One way is to increase the memory size of the index, which also increases
the amount of backing storage required. Doubling the size of the index will
double the length of the deduplication window at the expense of doubling
the storage size and the memory requirements.

The other option is to enable sparse indexing. Sparse indexing increases
the deduplication window by a factor of 10, at the expense of also
increasing the storage size by a factor of 10. However with sparse
indexing, the memory requirements do not increase. The trade-off is
slightly more computation per request and a slight decrease in the amount
of deduplication detected. For most workloads with significant amounts of
duplicate data, sparse indexing will detect 97-99% of the deduplication
that a standard index will detect.

The vio and data_vio Structures
-------------------------------

A vio (short for Vdo I/O) is conceptually similar to a bio, with additional
fields and data to track vdo-specific information. A struct vio maintains a
pointer to a bio but also tracks other fields specific to the operation of
vdo. The vio is kept separate from its related bio because there are many
circumstances where vdo completes the bio but must continue to do work
related to deduplication or compression.

Metadata reads and writes, and other writes that originate within vdo, use
a struct vio directly. Application reads and writes use a larger structure
called a data_vio to track information about their progress. A struct
data_vio contain a struct vio and also includes several other fields
related to deduplication and other vdo features. The data_vio is the
primary unit of application work in vdo. Each data_vio proceeds through a
set of steps to handle the application data, after which it is reset and
returned to a pool of data_vios for reuse.

There is a fixed pool of 2048 data_vios. This number was chosen to bound
the amount of work that is required to recover from a crash. In addition,
benchmarks have indicated that increasing the size of the pool does not
significantly improve performance.

The Data Store
--------------

The data store is implemented by three main data structures, all of which
work in concert to reduce or amortize metadata updates across as many data
writes as possible.

*The Slab Depot*

Most of the vdo volume belongs to the slab depot. The depot contains a
collection of slabs. The slabs can be up to 32GB, and are divided into
three sections. Most of a slab consists of a linear sequence of 4K blocks.
These blocks are used either to store data, or to hold portions of the
block map (see below). In addition to the data blocks, each slab has a set
of reference counters, using 1 byte for each data block. Finally each slab
has a journal.

Reference updates are written to the slab journal. Slab journal blocks are
written out either when they are full, or when the recovery journal
requests they do so in order to allow the main recovery journal (see below)
to free up space. The slab journal is used both to ensure that the main
recovery journal can regularly free up space, and also to amortize the cost
of updating individual reference blocks. The reference counters are kept in
memory and are written out, a block at a time in oldest-dirtied-order, only
when there is a need to reclaim slab journal space. The write operations
are performed in the background as needed so they do not add latency to
particular I/O operations.

Each slab is independent of every other. They are assigned to "physical
zones" in round-robin fashion. If there are P physical zones, then slab n
is assigned to zone n mod P.

The slab depot maintains an additional small data structure, the "slab
summary," which is used to reduce the amount of work needed to come back
online after a crash. The slab summary maintains an entry for each slab
indicating whether or not the slab has ever been used, whether all of its
reference count updates have been persisted to storage, and approximately
how full it is. During recovery, each physical zone will attempt to recover
at least one slab, stopping whenever it has recovered a slab which has some
free blocks. Once each zone has some space, or has determined that none is
available, the target can resume normal operation in a degraded mode. Read
and write requests can be serviced, perhaps with degraded performance,
while the remainder of the dirty slabs are recovered.

*The Block Map*

The block map contains the logical to physical mapping. It can be thought
of as an array with one entry per logical address. Each entry is 5 bytes,
36 bits of which contain the physical block number which holds the data for
the given logical address. The other 4 bits are used to indicate the nature
of the mapping. Of the 16 possible states, one represents a logical address
which is unmapped (i.e. it has never been written, or has been discarded),
one represents an uncompressed block, and the other 14 states are used to
indicate that the mapped data is compressed, and which of the compression
slots in the compressed block contains the data for this logical address.

In practice, the array of mapping entries is divided into "block map
pages," each of which fits in a single 4K block. Each block map page
consists of a header and 812 mapping entries. Each mapping page is actually
a leaf of a radix tree which consists of block map pages at each level.
There are 60 radix trees which are assigned to "logical zones" in round
robin fashion. (If there are L logical zones, tree n will belong to zone n
mod L.) At each level, the trees are interleaved, so logical addresses
0-811 belong to tree 0, logical addresses 812-1623 belong to tree 1, and so
on. The interleaving is maintained all the way up to the 60 root nodes.
Choosing 60 trees results in an evenly distributed number of trees per zone
for a large number of possible logical zone counts. The storage for the 60
tree roots is allocated at format time. All other block map pages are
allocated out of the slabs as needed. This flexible allocation avoids the
need to pre-allocate space for the entire set of logical mappings and also
makes growing the logical size of a vdo relatively easy.

In operation, the block map maintains two caches. It is prohibitive to keep
the entire leaf level of the trees in memory, so each logical zone
maintains its own cache of leaf pages. The size of this cache is
configurable at target start time. The second cache is allocated at start
time, and is large enough to hold all the non-leaf pages of the entire
block map. This cache is populated as pages are needed.

*The Recovery Journal*

The recovery journal is used to amortize updates across the block map and
slab depot. Each write request causes an entry to be made in the journal.
Entries are either "data remappings" or "block map remappings." For a data
remapping, the journal records the logical address affected and its old and
new physical mappings. For a block map remapping, the journal records the
block map page number and the physical block allocated for it. Block map
pages are never reclaimed or repurposed, so the old mapping is always 0.

Each journal entry is an intent record summarizing the metadata updates
that are required for a data_vio. The recovery journal issues a flush
before each journal block write to ensure that the physical data for the
new block mappings in that block are stable on storage, and journal block
writes are all issued with the FUA bit set to ensure the recovery journal
entries themselves are stable. The journal entry and the data write it
represents must be stable on disk before the other metadata structures may
be updated to reflect the operation. These entries allow the vdo device to
reconstruct the logical to physical mappings after an unexpected
interruption such as a loss of power.

*Write Path*

All write I/O to vdo is asynchronous. Each bio will be acknowledged as soon
as vdo has done enough work to guarantee that it can complete the write
eventually. Generally, the data for acknowledged but unflushed write I/O
can be treated as though it is cached in memory. If an application
requires data to be stable on storage, it must issue a flush or write the
data with the FUA bit set like any other asynchronous I/O. Shutting down
the vdo target will also flush any remaining I/O.

Application write bios follow the steps outlined below.

1.  A data_vio is obtained from the data_vio pool and associated with the
    application bio. If there are no data_vios available, the incoming bio
    will block until a data_vio is available. This provides back pressure
    to the application. The data_vio pool is protected by a spin lock.

    The newly acquired data_vio is reset and the bio's data is copied into
    the data_vio if it is a write and the data is not all zeroes. The data
    must be copied because the application bio can be acknowledged before
    the data_vio processing is complete, which means later processing steps
    will no longer have access to the application bio. The application bio
    may also be smaller than 4K, in which case the data_vio will have
    already read the underlying block and the data is instead copied over
    the relevant portion of the larger block.

2.  The data_vio places a claim (the "logical lock") on the logical address
    of the bio. It is vital to prevent simultaneous modifications of the
    same logical address, because deduplication involves sharing blocks.
    This claim is implemented as an entry in a hashtable where the key is
    the logical address and the value is a pointer to the data_vio
    currently handling that address.

    If a data_vio looks in the hashtable and finds that another data_vio is
    already operating on that logical address, it waits until the previous
    operation finishes. It also sends a message to inform the current
    lock holder that it is waiting. Most notably, a new data_vio waiting
    for a logical lock will flush the previous lock holder out of the
    compression packer (step 8d) rather than allowing it to continue
    waiting to be packed.

    This stage requires the data_vio to get an implicit lock on the
    appropriate logical zone to prevent concurrent modifications of the
    hashtable. This implicit locking is handled by the zone divisions
    described above.

3.  The data_vio traverses the block map tree to ensure that all the
    necessary internal tree nodes have been allocated, by trying to find
    the leaf page for its logical address. If any interior tree page is
    missing, it is allocated at this time out of the same physical storage
    pool used to store application data.

    a. If any page-node in the tree has not yet been allocated, it must be
       allocated before the write can continue. This step requires the
       data_vio to lock the page-node that needs to be allocated. This
       lock, like the logical block lock in step 2, is a hashtable entry
       that causes other data_vios to wait for the allocation process to
       complete.

       The implicit logical zone lock is released while the allocation is
       happening, in order to allow other operations in the same logical
       zone to proceed. The details of allocation are the same as in
       step 4. Once a new node has been allocated, that node is added to
       the tree using a similar process to adding a new data block mapping.
       The data_vio journals the intent to add the new node to the block
       map tree (step 10), updates the reference count of the new block
       (step 11), and reacquires the implicit logical zone lock to add the
       new mapping to the parent tree node (step 12). Once the tree is
       updated, the data_vio proceeds down the tree. Any other data_vios
       waiting on this allocation also proceed.

    b. In the steady-state case, the block map tree nodes will already be
       allocated, so the data_vio just traverses the tree until it finds
       the required leaf node. The location of the mapping (the "block map
       slot") is recorded in the data_vio so that later steps do not need
       to traverse the tree again. The data_vio then releases the implicit
       logical zone lock.

4.  If the block is a zero block, skip to step 9. Otherwise, an attempt is
    made to allocate a free data block. This allocation ensures that the
    data_vio can write its data somewhere even if deduplication and
    compression are not possible. This stage gets an implicit lock on a
    physical zone to search for free space within that zone.

    The data_vio will search each slab in a zone until it finds a free
    block or decides there are none. If the first zone has no free space,
    it will proceed to search the next physical zone by taking the implicit
    lock for that zone and releasing the previous one until it finds a
    free block or runs out of zones to search. The data_vio will acquire a
    struct pbn_lock (the "physical block lock") on the free block. The
    struct pbn_lock also has several fields to record the various kinds of
    claims that data_vios can have on physical blocks. The pbn_lock is
    added to a hashtable like the logical block locks in step 2. This
    hashtable is also covered by the implicit physical zone lock. The
    reference count of the free block is updated to prevent any other
    data_vio from considering it free. The reference counters are a
    sub-component of the slab and are thus also covered by the implicit
    physical zone lock.

5.  If an allocation was obtained, the data_vio has all the resources it
    needs to complete the write. The application bio can safely be
    acknowledged at this point. The acknowledgment happens on a separate
    thread to prevent the application callback from blocking other data_vio
    operations.

    If an allocation could not be obtained, the data_vio continues to
    attempt to deduplicate or compress the data, but the bio is not
    acknowledged because the vdo device may be out of space.

6.  At this point vdo must determine where to store the application data.
    The data_vio's data is hashed and the hash (the "record name") is
    recorded in the data_vio.

7.  The data_vio reserves or joins a struct hash_lock, which manages all of
    the data_vios currently writing the same data. Active hash locks are
    tracked in a hashtable similar to the way logical block locks are
    tracked in step 2. This hashtable is covered by the implicit lock on
    the hash zone.

    If there is no existing hash lock for this data_vio's record_name, the
    data_vio obtains a hash lock from the pool, adds it to the hashtable,
    and sets itself as the new hash lock's "agent." The hash_lock pool is
    also covered by the implicit hash zone lock. The hash lock agent will
    do all the work to decide where the application data will be
    written. If a hash lock for the data_vio's record_name already exists,
    and the data_vio's data is the same as the agent's data, the new
    data_vio will wait for the agent to complete its work and then share
    its result.

    In the rare case that a hash lock exists for the data_vio's hash but
    the data does not match the hash lock's agent, the data_vio skips to
    step 8h and attempts to write its data directly. This can happen if two
    different data blocks produce the same hash, for example.

8.  The hash lock agent attempts to deduplicate or compress its data with
    the following steps.

    a. The agent initializes and sends its embedded deduplication request
       (struct uds_request) to the deduplication index. This does not
       require the data_vio to get any locks because the index components
       manage their own locking. The data_vio waits until it either gets a
       response from the index or times out.

    b. If the deduplication index returns advice, the data_vio attempts to
       obtain a physical block lock on the indicated physical address, in
       order to read the data and verify that it is the same as the
       data_vio's data, and that it can accept more references. If the
       physical address is already locked by another data_vio, the data at
       that address may soon be overwritten so it is not safe to use the
       address for deduplication.

    c. If the data matches and the physical block can add references, the
       agent and any other data_vios waiting on it will record this
       physical block as their new physical address and proceed to step 9
       to record their new mapping. If there are more data_vios in the hash
       lock than there are references available, one of the remaining
       data_vios becomes the new agent and continues to step 8d as if no
       valid advice was returned.

    d. If no usable duplicate block was found, the agent first checks that
       it has an allocated physical block (from step 3) that it can write
       to. If the agent does not have an allocation, some other data_vio in
       the hash lock that does have an allocation takes over as agent. If
       none of the data_vios have an allocated physical block, these writes
       are out of space, so they proceed to step 13 for cleanup.

    e. The agent attempts to compress its data. If the data does not
       compress, the data_vio will continue to step 8h to write its data
       directly.

       If the compressed size is small enough, the agent will release the
       implicit hash zone lock and go to the packer (struct packer) where
       it will be placed in a bin (struct packer_bin) along with other
       data_vios. All compression operations require the implicit lock on
       the packer zone.

       The packer can combine up to 14 compressed blocks in a single 4k
       data block. Compression is only helpful if vdo can pack at least 2
       data_vios into a single data block. This means that a data_vio may
       wait in the packer for an arbitrarily long time for other data_vios
       to fill out the compressed block. There is a mechanism for vdo to
       evict waiting data_vios when continuing to wait would cause
       problems. Circumstances causing an eviction include an application
       flush, device shutdown, or a subsequent data_vio trying to overwrite
       the same logical block address. A data_vio may also be evicted from
       the packer if it cannot be paired with any other compressed block
       before more compressible blocks need to use its bin. An evicted
       data_vio will proceed to step 8h to write its data directly.

    f. If the agent fills a packer bin, either because all 14 of its slots
       are used or because it has no remaining space, it is written out
       using the allocated physical block from one of its data_vios. Step
       8d has already ensured that an allocation is available.

    g. Each data_vio sets the compressed block as its new physical address.
       The data_vio obtains an implicit lock on the physical zone and
       acquires the struct pbn_lock for the compressed block, which is
       modified to be a shared lock. Then it releases the implicit physical
       zone lock and proceeds to step 8i.

    h. Any data_vio evicted from the packer will have an allocation from
       step 3. It will write its data to that allocated physical block.

    i. After the data is written, if the data_vio is the agent of a hash
       lock, it will reacquire the implicit hash zone lock and share its
       physical address with as many other data_vios in the hash lock as
       possible. Each data_vio will then proceed to step 9 to record its
       new mapping.

    j. If the agent actually wrote new data (whether compressed or not),
       the deduplication index is updated to reflect the location of the
       new data. The agent then releases the implicit hash zone lock.

9.  The data_vio determines the previous mapping of the logical address.
    There is a cache for block map leaf pages (the "block map cache"),
    because there are usually too many block map leaf nodes to store
    entirely in memory. If the desired leaf page is not in the cache, the
    data_vio will reserve a slot in the cache and load the desired page
    into it, possibly evicting an older cached page. The data_vio then
    finds the current physical address for this logical address (the "old
    physical mapping"), if any, and records it. This step requires a lock
    on the block map cache structures, covered by the implicit logical zone
    lock.

10. The data_vio makes an entry in the recovery journal containing the
    logical block address, the old physical mapping, and the new physical
    mapping. Making this journal entry requires holding the implicit
    recovery journal lock. The data_vio will wait in the journal until all
    recovery blocks up to the one containing its entry have been written
    and flushed to ensure the transaction is stable on storage.

11. Once the recovery journal entry is stable, the data_vio makes two slab
    journal entries: an increment entry for the new mapping, and a
    decrement entry for the old mapping. These two operations each require
    holding a lock on the affected physical slab, covered by its implicit
    physical zone lock. For correctness during recovery, the slab journal
    entries in any given slab journal must be in the same order as the
    corresponding recovery journal entries. Therefore, if the two entries
    are in different zones, they are made concurrently, and if they are in
    the same zone, the increment is always made before the decrement in
    order to avoid underflow. After each slab journal entry is made in
    memory, the associated reference count is also updated in memory.

12. Once both of the reference count updates are done, the data_vio
    acquires the implicit logical zone lock and updates the
    logical-to-physical mapping in the block map to point to the new
    physical block. At this point the write operation is complete.

13. If the data_vio has a hash lock, it acquires the implicit hash zone
    lock and releases its hash lock to the pool.

    The data_vio then acquires the implicit physical zone lock and releases
    the struct pbn_lock it holds for its allocated block. If it had an
    allocation that it did not use, it also sets the reference count for
    that block back to zero to free it for use by subsequent data_vios.

    The data_vio then acquires the implicit logical zone lock and releases
    the logical block lock acquired in step 2.

    The application bio is then acknowledged if it has not previously been
    acknowledged, and the data_vio is returned to the pool.

*Read Path*

An application read bio follows a much simpler set of steps. It does steps
1 and 2 in the write path to obtain a data_vio and lock its logical
address. If there is already a write data_vio in progress for that logical
address that is guaranteed to complete, the read data_vio will copy the
data from the write data_vio and return it. Otherwise, it will look up the
logical-to-physical mapping by traversing the block map tree as in step 3,
and then read and possibly decompress the indicated data at the indicated
physical block address. A read data_vio will not allocate block map tree
nodes if they are missing. If the interior block map nodes do not exist
yet, the logical block map address must still be unmapped and the read
data_vio will return all zeroes. A read data_vio handles cleanup and
acknowledgment as in step 13, although it only needs to release the logical
lock and return itself to the pool.

*Small Writes*

All storage within vdo is managed as 4KB blocks, but it can accept writes
as small as 512 bytes. Processing a write that is smaller than 4K requires
a read-modify-write operation that reads the relevant 4K block, copies the
new data over the approriate sectors of the block, and then launches a
write operation for the modified data block. The read and write stages of
this operation are nearly identical to the normal read and write
operations, and a single data_vio is used throughout this operation.

*Recovery*

When a vdo is restarted after a crash, it will attempt to recover from the
recovery journal. During the pre-resume phase of the next start, the
recovery journal is read. The increment portion of valid entries are played
into the block map. Next, valid entries are played, in order as required,
into the slab journals. Finally, each physical zone attempts to replay at
least one slab journal to reconstruct the reference counts of one slab.
Once each zone has some free space (or has determined that it has none),
the vdo comes back online, while the remainder of the slab journals are
used to reconstruct the rest of the reference counts in the background.

*Read-only Rebuild*

If a vdo encounters an unrecoverable error, it will enter read-only mode.
This mode indicates that some previously acknowledged data may have been
lost. The vdo may be instructed to rebuild as best it can in order to
return to a writable state. However, this is never done automatically due
to the possibility that data has been lost. During a read-only rebuild, the
block map is recovered from the recovery journal as before. However, the
reference counts are not rebuilt from the slab journals. Instead, the
reference counts are zeroed, the entire block map is traversed, and the
reference counts are updated from the block mappings. While this may lose
some data, it ensures that the block map and reference counts are
consistent with each other. This allows vdo to resume normal operation and
accept further writes.

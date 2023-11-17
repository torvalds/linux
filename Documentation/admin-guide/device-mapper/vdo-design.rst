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
design attempts to be lock-free. Most of a vdo's main data structures are
designed to be easily divided into "zones" such that any given bio must
only access a single zone of any zoned structure. Safety with minimal
locking is achieved by ensuring that during normal operation, each zone is
assigned to a specific thread, and only that thread will access the portion
of that data structure in that zone. Associated with each thread is a work
queue. Each bio is associated with a request object which can be added to a
work queue when the next phase of its operation requires access to the
structures in the zone associated with that queue. Although each structure
may be divided into zones, this division is not reflected in the on-disk
representation of each data structure. Therefore, the number of zones for
each structure, and hence the number of threads, is configured each time a
vdo target is started.

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
guarantee that the index is accurate. Most often, this occurs because it is
too costly to update the index when a block is over-written or discarded.
Doing so would require either storing the block name along with the blocks,
which is difficult to do efficiently in block-based storage, or reading and
rehashing each block before overwriting it. Inaccuracy can also result from
a hash collision where two different blocks have the same name. In
practice, this is extremely unlikely, but because vdo does not use a
cryptographic hash, a malicious workload can be constructed. Because of
these inaccuracies, vdo treats the locations in the index as hints, and
reads each indicated block to verify that it is indeed a duplicate before
sharing the existing block with a new one.

Records are collected into groups called chapters. New records are added to
the newest chapter, called the open chapter. This chapter is stored in a
format optimized for adding and modifying records, and the content of the
open chapter is not finalized until it runs out of space for new records.
When the open chapter fills up, it is closed and a new open chapter is
created to collect new records.

Closing a chapter converts it to a different format which is optimized for
writing. The records are written to a series of record pages based on the
order in which they were received. This means that records with temporal
locality should be on a small number of pages, reducing the I/O required to
retrieve them. The chapter also compiles an index that indicates which
record page contains any given name. This index means that a request for a
name can determine exactly which record page may contain that record,
without having to load the entire chapter from storage. This index uses
only a subset of the block name as its key, so it cannot guarantee that an
index entry refers to the desired block name. It can only guarantee that if
there is a record for this name, it will be on the indicated page. The
contents of a closed chapter are never altered in any way; these chapters
are read-only structures.

Once enough records have been written to fill up all the available index
space, the oldest chapter gets removed to make space for new chapters. Any
time a request finds a matching record in the index, that record is copied
to the open chapter. This ensures that useful block names remain available
in the index, while unreferenced block names are forgotten.

In order to find records in older chapters, the index also maintains a
higher level structure called the volume index, which contains entries
mapping a block name to the chapter containing its newest record. This
mapping is updated as records for the block name are copied or updated,
ensuring that only the newer record for a given block name is findable.
Older records for a block name can no longer be found even though they have
not been deleted. Like the chapter index, the volume index uses only a
subset of the block name as its key and can not definitively say that a
record exists for a name. It can only say which chapter would contain the
record if a record exists. The volume index is stored entirely in memory
and is saved to storage only when the vdo target is shut down.

From the viewpoint of a request for a particular block name, first it will
look up the name in the volume index which will indicate either that the
record is new, or which chapter to search. If the latter, the request looks
up its name in the chapter index to determine if the record is new, or
which record page to search. Finally, if not new, the request will look for
its record on the indicated record page. This process may require up to two
page reads per request (one for the chapter index page and one for the
request page). However, recently accessed pages are cached so that these
page reads can be amortized across many block name requests.

The volume index and the chapter indexes are implemented using a
memory-efficient structure called a delta index. Instead of storing the
entire key (the block name) for each entry, the entries are sorted by name
and only the difference between adjacent keys (the delta) is stored.
Because we expect the hashes to be evenly distributed, the size of the
deltas follows an exponential distribution. Because of this distribution,
the deltas are expressed in a Huffman code to take up even less space. The
entire sorted list of keys is called a delta list. This structure allows
the index to use many fewer bytes per entry than a traditional hash table,
but it is slightly more expensive to look up entries, because a request
must read every entry in a delta list to add up the deltas in order to find
the record it needs. The delta index reduces this lookup cost by splitting
its key space into many sub-lists, each starting at a fixed key value, so
that each individual list is short.

The default index size can hold 64 million records, corresponding to about
256GB. This means that the index can identify duplicate data if the
original data was written within the last 256GB of writes. This range is
called the deduplication window. If new writes duplicate data that is older
than that, the index will not be able to find it because the records of the
older data have been removed. So when writing a 200 GB file to a vdo
target, and then immediately writing it again, the two copies will
deduplicate perfectly. Doing the same with a 500 GB file will result in no
deduplication, because the beginning of the file will no longer be in the
index by the time the second write begins (assuming there is no duplication
within the file itself).

If you anticipate a data workload that will see useful deduplication beyond
the 256GB threshold, vdo can be configured to use a larger index with a
correspondingly larger deduplication window. (This configuration can only
be set when the target is created, not altered later. It is important to
consider the expected workload for a vdo target before configuring it.)
There are two ways to do this.

One way is to increase the memory size of the index, which also increases
the amount of backing storage required. Doubling the size of the index will
double the length of the deduplication window at the expense of doubling
the storage size and the memory requirements.

The other way is to enable sparse indexing. Sparse indexing increases the
deduplication window by a factor of 10, at the expense of also increasing
the storage size by a factor of 10. However with sparse indexing, the
memory requirements do not increase; the trade-off is slightly more
computation per request, and a slight decrease in the amount of
deduplication detected. (For workloads with significant amounts of
duplicate data, sparse indexing will detect 97-99% of the deduplication
that a standard, or "dense", index will detect.)

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
has a journal. Reference updates are written to the slab journal, which is
written out one block at a time as each block fills. A copy of the
reference counters are kept in memory, and are written out a block at a
time, in oldest-dirtied-order whenever there is a need to reclaim slab
journal space. The journal is used both to ensure that the main recovery
journal (see below) can regularly free up space, and also to amortize the
cost of updating individual reference blocks.

Each slab is independent of every other. They are assigned to "physical
zones" in round-robin fashion. If there are P physical zones, then slab n
is assigned to zone n mod P.

The slab depot maintains an additional small data structure, the "slab
summary," which is used to reduce the amount of work needed to come back
online after a crash. The slab summary maintains an entry for each slab
indicating whether or not the slab has ever been used, whether it is clean
(i.e. all of its reference count updates have been persisted to storage),
and approximately how full it is. During recovery, each physical zone will
attempt to recover at least one slab, stopping whenever it has recovered a
slab which has some free blocks. Once each zone has some space (or has
determined that none is available), the target can resume normal operation
in a degraded mode. Read and write requests can be serviced, perhaps with
degraded performance, while the remainder of the dirty slabs are recovered.

*The Block Map*

The block map contains the logical to physical mapping. It can be thought
of as an array with one entry per logical address. Each entry is 5 bytes,
36 bits of which contain the physical block number which holds the data for
the given logical address. The other 4 bits are used to indicate the nature
of the mapping. Of the 16 possible states, one represents a logical address
which is unmapped (i.e. it has never been written, or has been discarded),
one represents an uncompressed block, and the other 14 states are used to
indicate that the mapped data is compressed, and which of the compression
slots in the compressed block this logical address maps to (see below).

In practice, the array of mapping entries is divided into "block map
pages," each of which fits in a single 4K block. Each block map page
consists of a header, and 812 mapping entries (812 being the number that
fit). Each mapping page is actually a leaf of a radix tree which consists
of block map pages at each level. There are 60 radix trees which are
assigned to "logical zones" in round robin fashion (if there are L logical
zones, tree n will belong to zone n mod L). At each level, the trees are
interleaved, so logical addresses 0-811 belong to tree 0, logical addresses
812-1623 belong to tree 1, and so on. The interleaving is maintained all
the way up the forest. 60 was chosen as the number of trees because it is
highly composite and hence results in an evenly distributed number of trees
per zone for a large number of possible logical zone counts. The storage
for the 60 tree roots is allocated at format time. All other block map
pages are allocated out of the slabs as needed. This flexible allocation
avoids the need to pre-allocate space for the entire set of logical
mappings and also makes growing the logical size of a vdo easy to
implement.

In operation, the block map maintains two caches. It is prohibitive to keep
the entire leaf level of the trees in memory, so each logical zone
maintains its own cache of leaf pages. The size of this cache is
configurable at target start time. The second cache is allocated at start
time, and is large enough to hold all the non-leaf pages of the entire
block map. This cache is populated as needed.

*The Recovery Journal*

The recovery journal is used to amortize updates across the block map and
slab depot. Each write request causes an entry to be made in the journal.
Entries are either "data remappings" or "block map remappings." For a data
remapping, the journal records the logical address affected and its old and
new physical mappings. For a block map remapping, the journal records the
block map page number and the physical block allocated for it (block map
pages are never reclaimed, so the old mapping is always 0). Each journal
entry and the data write it represents must be stable on disk before the
other metadata structures may be updated to reflect the operation.

*Write Path*

A write bio is first assigned a "data_vio," the request object which will
operate on behalf of the bio. (A "vio," from Vdo I/O, is vdo's wrapper for
bios; metadata operations use a vio, whereas submitted bios require the
much larger data_vio.) There is a fixed pool of 2048 data_vios. This number
was chosen both to bound the amount of work that is required to recover
from a crash, and because measurements indicate that increasing it consumes
more resources, but does not improve performance. These measurements have
been, and should continue to be, revisited over time.

Once a data_vio is assigned, the following steps are performed:

1.  The bio's data is checked to see if it is all zeros, and copied if not.

2.  A lock is obtained on the logical address of the bio. Because
    deduplication involves sharing blocks, it is vital to prevent
    simultaneous modifications of the same block.

3.  The block map tree is traversed, loading any non-leaf pages which cover
    the logical address and are not already in memory. If any of these
    pages, or the leaf page which covers the logical address have not been
    allocated, and the block is not all zeros, they are allocated at this
    time.

4.  If the block is a zero block, skip to step 9. Otherwise, an attempt is
    made to allocate a free data block.

5.  If an allocation was obtained, the bio is acknowledged.

6.  The bio's data is hashed.

7.  The data_vio obtains or joins a "hash lock," which represents all of
    the bios currently writing the same data.

8.  If the hash lock does not already have a data_vio acting as its agent,
    the current one assumes that role. As the agent:

        a) The index is queried.

        b) If an entry is found, the indicated block is read and compared
           to the data being written.

        c) If the data matches, we have identified duplicate data. As many
           of the data_vios as there are references available for that
           block (including the agent) are shared. If there are more
           data_vios in the hash lock than there are references available,
           one of them becomes the new agent and continues as if there was
           no duplicate found.

        d) If no duplicate was found, and the agent in the hash lock does
           not have an allocation (fron step 3), another data_vio in the
           hash lock will become the agent and write the data. If no
           data_vio in the hash lock has an allocation, the data_vios will
           be marked out of space and go to step 13 for cleanup.

           If there is an allocation, the data being written will be
           compressed. If the compressed size is sufficiently small, the
           data_vio will go to the packer where it may be placed in a bin
           along with other data_vios.

        e) Once a bin is full, either because it is out of space, or
           because all 14 of its slots are in use, it is written out.

        f) Each data_vio from the bin just written is the agent of some
           hash lock, it will now proceed to treat the just written
           compressed block as if it were a duplicate and share it with as
           many other data_vios in its hash lock as possible.

        g) If the agent's data is not compressed, it will attempt to write
           its data to the block it has allocated.

        h) If the data was written, this new block is treated as a
           duplicate and shared as much as possible with any other
           data_vios in the hash lock.

        i) If the agent wrote new data (whether compressed or not), the
           index is updated to reflect the new entry.

9.  The block map is queried to determine the previous mapping of the
    logical address.

10. An entry is made in the recovery journal. The data_vio will block in
    the journal until a flush has completed to ensure the data it may have
    written is stable. It must also wait until its journal entry is stable
    on disk. (Journal writes are all issued with the FUA bit set.)

11. Once the recovery journal entry is stable, the data_vio makes two slab
    journal entries: an increment entry for the new mapping, and a
    decrement entry for the old mapping, if that mapping was non-zero. For
    correctness during recovery, the slab journal entries in any given slab
    journal must be in the same order as the corresponding recovery journal
    entries. Therefore, if the two entries are in different zones, they are
    made concurrently, and if they are in the same zone, the increment is
    always made before the decrement in order to avoid underflow. After
    each slab journal entry is made in memory, the associated reference
    count is also updated in memory. Each of these updates will get written
    out as needed. (Slab journal blocks are written out either when they
    are full, or when the recovery journal requests they do so in order to
    allow the recovery journal to free up space; reference count blocks are
    written out whenever the associated slab journal requests they do so in
    order to free up slab journal space.)

12. Once all the reference count updates are done, the block map is updated
    and the write is complete.

13. If the data_vio did not use its allocation, it releases the allocated
    block, the hash lock (if it has one), and its logical lock. The
    data_vio then returns to the pool.

*Read Path*

Reads are much simpler than writes. After a data_vio is assigned to the
bio, and the logical lock is obtained, the block map is queried. If the
block is mapped, the appropriate physical block is read, and if necessary,
decompressed.

*Recovery*

When a vdo is restarted after a crash, it will attempt to recover from the
recovery journal. During the pre-resume phase of the next start, the
recovery journal is read. The increment portion of valid entries are played
into the block map. Next, valid entries are played, in order as required,
into the slab journals. Finally, each physical zone attempts to replay at
least one slab journal to reconstruct the reference counts of one slab.
Once each zone has some free space (or has determined that it has none),
the vdo comes back online, while the remainder of the slab journals are
used to reconstruct the rest of the reference counts.

*Read-only Rebuild*

If a vdo encounters an unrecoverable error, it will enter read-only mode.
This mode indicates that some previously acknowledged data may have been
lost. The vdo may be instructed to rebuild as best it can in order to
return to a writable state. However, this is never done automatically due
to the likelihood that data has been lost. During a read-only rebuild, the
block map is recovered from the recovery journal as before. However, the
reference counts are not rebuilt from the slab journals. Rather, the
reference counts are zeroed, and then the entire block map is traversed,
and the reference counts are updated from it. While this may lose some
data, it ensures that the block map and reference counts are consistent.

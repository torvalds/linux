.. SPDX-License-Identifier: GPL-2.0

=============
False Sharing
=============

What is False Sharing
=====================
False sharing is related with cache mechanism of maintaining the data
coherence of one cache line stored in multiple CPU's caches; then
academic definition for it is in [1]_. Consider a struct with a
refcount and a string::

	struct foo {
		refcount_t refcount;
		...
		char name[16];
	} ____cacheline_internodealigned_in_smp;

Member 'refcount'(A) and 'name'(B) _share_ one cache line like below::

                +-----------+                     +-----------+
                |   CPU 0   |                     |   CPU 1   |
                +-----------+                     +-----------+
               /                                        |
              /                                         |
             V                                          V
         +----------------------+             +----------------------+
         | A      B             | Cache 0     | A       B            | Cache 1
         +----------------------+             +----------------------+
                             |                  |
  ---------------------------+------------------+-----------------------------
                             |                  |
                           +----------------------+
                           |                      |
                           +----------------------+
              Main Memory  | A       B            |
                           +----------------------+

'refcount' is modified frequently, but 'name' is set once at object
creation time and is never modified.  When many CPUs access 'foo' at
the same time, with 'refcount' being only bumped by one CPU frequently
and 'name' being read by other CPUs, all those reading CPUs have to
reload the whole cache line over and over due to the 'sharing', even
though 'name' is never changed.

There are many real-world cases of performance regressions caused by
false sharing.  One of these is a rw_semaphore 'mmap_lock' inside
mm_struct struct, whose cache line layout change triggered a
regression and Linus analyzed in [2]_.

There are two key factors for a harmful false sharing:

* A global datum accessed (shared) by many CPUs
* In the concurrent accesses to the data, there is at least one write
  operation: write/write or write/read cases.

The sharing could be from totally unrelated kernel components, or
different code paths of the same kernel component.


False Sharing Pitfalls
======================
Back in time when one platform had only one or a few CPUs, hot data
members could be purposely put in the same cache line to make them
cache hot and save cacheline/TLB, like a lock and the data protected
by it.  But for recent large system with hundreds of CPUs, this may
not work when the lock is heavily contended, as the lock owner CPU
could write to the data, while other CPUs are busy spinning the lock.

Looking at past cases, there are several frequently occurring patterns
for false sharing:

* lock (spinlock/mutex/semaphore) and data protected by it are
  purposely put in one cache line.
* global data being put together in one cache line. Some kernel
  subsystems have many global parameters of small size (4 bytes),
  which can easily be grouped together and put into one cache line.
* data members of a big data structure randomly sitting together
  without being noticed (cache line is usually 64 bytes or more),
  like 'mem_cgroup' struct.

Following 'mitigation' section provides real-world examples.

False sharing could easily happen unless they are intentionally
checked, and it is valuable to run specific tools for performance
critical workloads to detect false sharing affecting performance case
and optimize accordingly.


How to detect and analyze False Sharing
========================================
perf record/report/stat are widely used for performance tuning, and
once hotspots are detected, tools like 'perf-c2c' and 'pahole' can
be further used to detect and pinpoint the possible false sharing
data structures.  'addr2line' is also good at decoding instruction
pointer when there are multiple layers of inline functions.

perf-c2c can capture the cache lines with most false sharing hits,
decoded functions (line number of file) accessing that cache line,
and in-line offset of the data. Simple commands are::

  $ perf c2c record -ag sleep 3
  $ perf c2c report --call-graph none -k vmlinux

When running above during testing will-it-scale's tlb_flush1 case,
perf reports something like::

  Total records                     :    1658231
  Locked Load/Store Operations      :      89439
  Load Operations                   :     623219
  Load Local HITM                   :      92117
  Load Remote HITM                  :        139

  #----------------------------------------------------------------------
      4        0     2374        0        0        0  0xff1100088366d880
  #----------------------------------------------------------------------
    0.00%   42.29%    0.00%    0.00%    0.00%    0x8     1       1  0xffffffff81373b7b         0       231       129     5312        64  [k] __mod_lruvec_page_state    [kernel.vmlinux]  memcontrol.h:752   1
    0.00%   13.10%    0.00%    0.00%    0.00%    0x8     1       1  0xffffffff81374718         0       226        97     3551        64  [k] folio_lruvec_lock_irqsave  [kernel.vmlinux]  memcontrol.h:752   1
    0.00%   11.20%    0.00%    0.00%    0.00%    0x8     1       1  0xffffffff812c29bf         0       170       136      555        64  [k] lru_add_fn                 [kernel.vmlinux]  mm_inline.h:41     1
    0.00%    7.62%    0.00%    0.00%    0.00%    0x8     1       1  0xffffffff812c3ec5         0       175       108      632        64  [k] release_pages              [kernel.vmlinux]  mm_inline.h:41     1
    0.00%   23.29%    0.00%    0.00%    0.00%   0x10     1       1  0xffffffff81372d0a         0       234       279     1051        64  [k] __mod_memcg_lruvec_state   [kernel.vmlinux]  memcontrol.c:736   1

A nice introduction for perf-c2c is [3]_.

'pahole' decodes data structure layouts delimited in cache line
granularity.  Users can match the offset in perf-c2c output with
pahole's decoding to locate the exact data members.  For global
data, users can search the data address in System.map.


Possible Mitigations
====================
False sharing does not always need to be mitigated.  False sharing
mitigations should balance performance gains with complexity and
space consumption.  Sometimes, lower performance is OK, and it's
unnecessary to hyper-optimize every rarely used data structure or
a cold data path.

False sharing hurting performance cases are seen more frequently with
core count increasing.  Because of these detrimental effects, many
patches have been proposed across variety of subsystems (like
networking and memory management) and merged.  Some common mitigations
(with examples) are:

* Separate hot global data in its own dedicated cache line, even if it
  is just a 'short' type. The downside is more consumption of memory,
  cache line and TLB entries.

  - Commit 91b6d3256356 ("net: cache align tcp_memory_allocated, tcp_sockets_allocated")

* Reorganize the data structure, separate the interfering members to
  different cache lines.  One downside is it may introduce new false
  sharing of other members.

  - Commit 802f1d522d5f ("mm: page_counter: re-layout structure to reduce false sharing")

* Replace 'write' with 'read' when possible, especially in loops.
  Like for some global variable, use compare(read)-then-write instead
  of unconditional write. For example, use::

	if (!test_bit(XXX))
		set_bit(XXX);

  instead of directly "set_bit(XXX);", similarly for atomic_t data::

	if (atomic_read(XXX) == AAA)
		atomic_set(XXX, BBB);

  - Commit 7b1002f7cfe5 ("bcache: fixup bcache_dev_sectors_dirty_add() multithreaded CPU false sharing")
  - Commit 292648ac5cf1 ("mm: gup: allow FOLL_PIN to scale in SMP")

* Turn hot global data to 'per-cpu data + global data' when possible,
  or reasonably increase the threshold for syncing per-cpu data to
  global data, to reduce or postpone the 'write' to that global data.

  - Commit 520f897a3554 ("ext4: use percpu_counters for extent_status cache hits/misses")
  - Commit 56f3547bfa4d ("mm: adjust vm_committed_as_batch according to vm overcommit policy")

Surely, all mitigations should be carefully verified to not cause side
effects.  To avoid introducing false sharing when coding, it's better
to:

* Be aware of cache line boundaries
* Group mostly read-only fields together
* Group things that are written at the same time together
* Separate frequently read and frequently written fields on
  different cache lines.

and better add a comment stating the false sharing consideration.

One note is, sometimes even after a severe false sharing is detected
and solved, the performance may still have no obvious improvement as
the hotspot switches to a new place.


Miscellaneous
=============
One open issue is that the kernel has an optional data structure
randomization mechanism, which also randomizes the situation of cache
line sharing among data members.


.. [1] https://en.wikipedia.org/wiki/False_sharing
.. [2] https://lore.kernel.org/lkml/CAHk-=whoqV=cX5VC80mmR9rr+Z+yQ6fiQZm36Fb-izsanHg23w@mail.gmail.com/
.. [3] https://joemario.github.io/blog/2016/09/01/c2c-blog/

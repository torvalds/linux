.. SPDX-License-Identifier: GPL-2.0

===========================
MEMORY ALLOCATION PROFILING
===========================

Low overhead (suitable for production) accounting of all memory allocations,
tracked by file and line number.

Usage:
kconfig options:
- CONFIG_MEM_ALLOC_PROFILING

- CONFIG_MEM_ALLOC_PROFILING_ENABLED_BY_DEFAULT

- CONFIG_MEM_ALLOC_PROFILING_DEBUG
  adds warnings for allocations that weren't accounted because of a
  missing annotation

Boot parameter:
  sysctl.vm.mem_profiling={0|1|never}[,compressed]

  When set to "never", memory allocation profiling overhead is minimized and it
  cannot be enabled at runtime (sysctl becomes read-only).
  When CONFIG_MEM_ALLOC_PROFILING_ENABLED_BY_DEFAULT=y, default value is "1".
  When CONFIG_MEM_ALLOC_PROFILING_ENABLED_BY_DEFAULT=n, default value is "never".
  "compressed" optional parameter will try to store page tag references in a
  compact format, avoiding page extensions. This results in improved performance
  and memory consumption, however it might fail depending on system configuration.
  If compression fails, a warning is issued and memory allocation profiling gets
  disabled.

sysctl:
  /proc/sys/vm/mem_profiling

Runtime info:
  /proc/allocinfo

Example output::

  root@moria-kvm:~# sort -g /proc/allocinfo|tail|numfmt --to=iec
        2.8M    22648 fs/kernfs/dir.c:615 func:__kernfs_new_node
        3.8M      953 mm/memory.c:4214 func:alloc_anon_folio
        4.0M     1010 drivers/staging/ctagmod/ctagmod.c:20 [ctagmod] func:ctagmod_start
        4.1M        4 net/netfilter/nf_conntrack_core.c:2567 func:nf_ct_alloc_hashtable
        6.0M     1532 mm/filemap.c:1919 func:__filemap_get_folio
        8.8M     2785 kernel/fork.c:307 func:alloc_thread_stack_node
         13M      234 block/blk-mq.c:3421 func:blk_mq_alloc_rqs
         14M     3520 mm/mm_init.c:2530 func:alloc_large_system_hash
         15M     3656 mm/readahead.c:247 func:page_cache_ra_unbounded
         55M     4887 mm/slub.c:2259 func:alloc_slab_page
        122M    31168 mm/page_ext.c:270 func:alloc_page_ext

Theory of operation
===================

Memory allocation profiling builds off of code tagging, which is a library for
declaring static structs (that typically describe a file and line number in
some way, hence code tagging) and then finding and operating on them at runtime,
- i.e. iterating over them to print them in debugfs/procfs.

To add accounting for an allocation call, we replace it with a macro
invocation, alloc_hooks(), that
- declares a code tag
- stashes a pointer to it in task_struct
- calls the real allocation function
- and finally, restores the task_struct alloc tag pointer to its previous value.

This allows for alloc_hooks() calls to be nested, with the most recent one
taking effect. This is important for allocations internal to the mm/ code that
do not properly belong to the outer allocation context and should be counted
separately: for example, slab object extension vectors, or when the slab
allocates pages from the page allocator.

Thus, proper usage requires determining which function in an allocation call
stack should be tagged. There are many helper functions that essentially wrap
e.g. kmalloc() and do a little more work, then are called in multiple places;
we'll generally want the accounting to happen in the callers of these helpers,
not in the helpers themselves.

To fix up a given helper, for example foo(), do the following:
- switch its allocation call to the _noprof() version, e.g. kmalloc_noprof()

- rename it to foo_noprof()

- define a macro version of foo() like so:

  #define foo(...) alloc_hooks(foo_noprof(__VA_ARGS__))

It's also possible to stash a pointer to an alloc tag in your own data structures.

Do this when you're implementing a generic data structure that does allocations
"on behalf of" some other code - for example, the rhashtable code. This way,
instead of seeing a large line in /proc/allocinfo for rhashtable.c, we can
break it out by rhashtable type.

To do so:
- Hook your data structure's init function, like any other allocation function.

- Within your init function, use the convenience macro alloc_tag_record() to
  record alloc tag in your data structure.

- Then, use the following form for your allocations:
  alloc_hooks_tag(ht->your_saved_tag, kmalloc_noprof(...))

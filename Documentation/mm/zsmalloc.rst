========
zsmalloc
========

This allocator is designed for use with zram. Thus, the allocator is
supposed to work well under low memory conditions. In particular, it
never attempts higher order page allocation which is very likely to
fail under memory pressure. On the other hand, if we just use single
(0-order) pages, it would suffer from very high fragmentation --
any object of size PAGE_SIZE/2 or larger would occupy an entire page.
This was one of the major issues with its predecessor (xvmalloc).

To overcome these issues, zsmalloc allocates a bunch of 0-order pages
and links them together using various 'struct page' fields. These linked
pages act as a single higher-order page i.e. an object can span 0-order
page boundaries. The code refers to these linked pages as a single entity
called zspage.

For simplicity, zsmalloc can only allocate objects of size up to PAGE_SIZE
since this satisfies the requirements of all its current users (in the
worst case, page is incompressible and is thus stored "as-is" i.e. in
uncompressed form). For allocation requests larger than this size, failure
is returned (see zs_malloc).

Additionally, zs_malloc() does not return a dereferenceable pointer.
Instead, it returns an opaque handle (unsigned long) which encodes actual
location of the allocated object. The reason for this indirection is that
zsmalloc does not keep zspages permanently mapped since that would cause
issues on 32-bit systems where the VA region for kernel space mappings
is very small. So, before using the allocating memory, the object has to
be mapped using zs_map_object() to get a usable pointer and subsequently
unmapped using zs_unmap_object().

stat
====

With CONFIG_ZSMALLOC_STAT, we could see zsmalloc internal information via
``/sys/kernel/debug/zsmalloc/<user name>``. Here is a sample of stat output::

 # cat /sys/kernel/debug/zsmalloc/zram0/classes

 class  size       10%       20%       30%       40%       50%       60%       70%       80%       90%       99%      100% obj_allocated   obj_used pages_used pages_per_zspage freeable
    ...
    ...
    30   512         0        12         4         1         0         1         0         0         1         0       414          3464       3346        433                1       14
    31   528         2         7         2         2         1         0         1         0         0         2       117          4154       3793        536                4       44
    32   544         6         3         4         1         2         1         0         0         0         1       260          4170       3965        556                2       26
    ...
    ...


class
	index
size
	object size zspage stores
10%
	the number of zspages with usage ratio less than 10% (see below)
20%
	the number of zspages with usage ratio between 10% and 20%
30%
	the number of zspages with usage ratio between 20% and 30%
40%
	the number of zspages with usage ratio between 30% and 40%
50%
	the number of zspages with usage ratio between 40% and 50%
60%
	the number of zspages with usage ratio between 50% and 60%
70%
	the number of zspages with usage ratio between 60% and 70%
80%
	the number of zspages with usage ratio between 70% and 80%
90%
	the number of zspages with usage ratio between 80% and 90%
99%
	the number of zspages with usage ratio between 90% and 99%
100%
	the number of zspages with usage ratio 100%
obj_allocated
	the number of objects allocated
obj_used
	the number of objects allocated to the user
pages_used
	the number of pages allocated for the class
pages_per_zspage
	the number of 0-order pages to make a zspage
freeable
	the approximate number of pages class compaction can free

Each zspage maintains inuse counter which keeps track of the number of
objects stored in the zspage.  The inuse counter determines the zspage's
"fullness group" which is calculated as the ratio of the "inuse" objects to
the total number of objects the zspage can hold (objs_per_zspage). The
closer the inuse counter is to objs_per_zspage, the better.

Internals
=========

zsmalloc has 255 size classes, each of which can hold a number of zspages.
Each zspage can contain up to ZSMALLOC_CHAIN_SIZE physical (0-order) pages.
The optimal zspage chain size for each size class is calculated during the
creation of the zsmalloc pool (see calculate_zspage_chain_size()).

As an optimization, zsmalloc merges size classes that have similar
characteristics in terms of the number of pages per zspage and the number
of objects that each zspage can store.

For instance, consider the following size classes:::

  class  size       10%   ....    100% obj_allocated   obj_used pages_used pages_per_zspage freeable
  ...
     94  1536        0    ....       0             0          0          0                3        0
    100  1632        0    ....       0             0          0          0                2        0
  ...


Size classes #95-99 are merged with size class #100. This means that when we
need to store an object of size, say, 1568 bytes, we end up using size class
#100 instead of size class #96. Size class #100 is meant for objects of size
1632 bytes, so each object of size 1568 bytes wastes 1632-1568=64 bytes.

Size class #100 consists of zspages with 2 physical pages each, which can
hold a total of 5 objects. If we need to store 13 objects of size 1568, we
end up allocating three zspages, or 6 physical pages.

However, if we take a closer look at size class #96 (which is meant for
objects of size 1568 bytes) and trace `calculate_zspage_chain_size()`, we
find that the most optimal zspage configuration for this class is a chain
of 5 physical pages:::

    pages per zspage      wasted bytes     used%
           1                  960           76
           2                  352           95
           3                 1312           89
           4                  704           95
           5                   96           99

This means that a class #96 configuration with 5 physical pages can store 13
objects of size 1568 in a single zspage, using a total of 5 physical pages.
This is more efficient than the class #100 configuration, which would use 6
physical pages to store the same number of objects.

As the zspage chain size for class #96 increases, its key characteristics
such as pages per-zspage and objects per-zspage also change. This leads to
dewer class mergers, resulting in a more compact grouping of classes, which
reduces memory wastage.

Let's take a closer look at the bottom of `/sys/kernel/debug/zsmalloc/zramX/classes`:::

  class  size       10%   ....    100% obj_allocated   obj_used pages_used pages_per_zspage freeable

  ...
    202  3264         0   ..         0             0          0          0                4        0
    254  4096         0   ..         0             0          0          0                1        0
  ...

Size class #202 stores objects of size 3264 bytes and has a maximum of 4 pages
per zspage. Any object larger than 3264 bytes is considered huge and belongs
to size class #254, which stores each object in its own physical page (objects
in huge classes do not share pages).

Increasing the size of the chain of zspages also results in a higher watermark
for the huge size class and fewer huge classes overall. This allows for more
efficient storage of large objects.

For zspage chain size of 8, huge class watermark becomes 3632 bytes:::

  class  size       10%   ....    100% obj_allocated   obj_used pages_used pages_per_zspage freeable

  ...
    202  3264         0   ..         0             0          0          0                4        0
    211  3408         0   ..         0             0          0          0                5        0
    217  3504         0   ..         0             0          0          0                6        0
    222  3584         0   ..         0             0          0          0                7        0
    225  3632         0   ..         0             0          0          0                8        0
    254  4096         0   ..         0             0          0          0                1        0
  ...

For zspage chain size of 16, huge class watermark becomes 3840 bytes:::

  class  size       10%   ....    100% obj_allocated   obj_used pages_used pages_per_zspage freeable

  ...
    202  3264         0   ..         0             0          0          0                4        0
    206  3328         0   ..         0             0          0          0               13        0
    207  3344         0   ..         0             0          0          0                9        0
    208  3360         0   ..         0             0          0          0               14        0
    211  3408         0   ..         0             0          0          0                5        0
    212  3424         0   ..         0             0          0          0               16        0
    214  3456         0   ..         0             0          0          0               11        0
    217  3504         0   ..         0             0          0          0                6        0
    219  3536         0   ..         0             0          0          0               13        0
    222  3584         0   ..         0             0          0          0                7        0
    223  3600         0   ..         0             0          0          0               15        0
    225  3632         0   ..         0             0          0          0                8        0
    228  3680         0   ..         0             0          0          0                9        0
    230  3712         0   ..         0             0          0          0               10        0
    232  3744         0   ..         0             0          0          0               11        0
    234  3776         0   ..         0             0          0          0               12        0
    235  3792         0   ..         0             0          0          0               13        0
    236  3808         0   ..         0             0          0          0               14        0
    238  3840         0   ..         0             0          0          0               15        0
    254  4096         0   ..         0             0          0          0                1        0
  ...

Overall the combined zspage chain size effect on zsmalloc pool configuration:::

  pages per zspage   number of size classes (clusters)   huge size class watermark
         4                        69                               3264
         5                        86                               3408
         6                        93                               3504
         7                       112                               3584
         8                       123                               3632
         9                       140                               3680
        10                       143                               3712
        11                       159                               3744
        12                       164                               3776
        13                       180                               3792
        14                       183                               3808
        15                       188                               3840
        16                       191                               3840


A synthetic test
----------------

zram as a build artifacts storage (Linux kernel compilation).

* `CONFIG_ZSMALLOC_CHAIN_SIZE=4`

  zsmalloc classes stats:::

    class  size       10%   ....    100% obj_allocated   obj_used pages_used pages_per_zspage freeable

    ...
    Total              13   ..        51        413836     412973     159955                         3

  zram mm_stat:::

   1691783168 628083717 655175680        0 655175680       60        0    34048    34049


* `CONFIG_ZSMALLOC_CHAIN_SIZE=8`

  zsmalloc classes stats:::

    class  size       10%   ....    100% obj_allocated   obj_used pages_used pages_per_zspage freeable

    ...
    Total              18   ..        87        414852     412978     156666                         0

  zram mm_stat:::

    1691803648 627793930 641703936        0 641703936       60        0    33591    33591

Using larger zspage chains may result in using fewer physical pages, as seen
in the example where the number of physical pages used decreased from 159955
to 156666, at the same time maximum zsmalloc pool memory usage went down from
655175680 to 641703936 bytes.

However, this advantage may be offset by the potential for increased system
memory pressure (as some zspages have larger chain sizes) in cases where there
is heavy internal fragmentation and zspool compaction is unable to relocate
objects and release zspages. In these cases, it is recommended to decrease
the limit on the size of the zspage chains (as specified by the
CONFIG_ZSMALLOC_CHAIN_SIZE option).

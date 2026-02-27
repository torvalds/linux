
.. SPDX-License-Identifier: GPL-2.0

=========================================
A vmemmap diet for HugeTLB and Device DAX
=========================================

HugeTLB
=======

This section is to explain how HugeTLB Vmemmap Optimization (HVO) works.

The ``struct page`` structures are used to describe a physical page frame. By
default, there is a one-to-one mapping from a page frame to its corresponding
``struct page``.

HugeTLB pages consist of multiple base page size pages and is supported by many
architectures. See Documentation/admin-guide/mm/hugetlbpage.rst for more
details. On the x86-64 architecture, HugeTLB pages of size 2MB and 1GB are
currently supported. Since the base page size on x86 is 4KB, a 2MB HugeTLB page
consists of 512 base pages and a 1GB HugeTLB page consists of 262144 base pages.
For each base page, there is a corresponding ``struct page``.

Within the HugeTLB subsystem, only the first 4 ``struct page`` are used to
contain unique information about a HugeTLB page. ``__NR_USED_SUBPAGE`` provides
this upper limit. The only 'useful' information in the remaining ``struct page``
is the compound_info field, and this field is the same for all tail pages.

By removing redundant ``struct page`` for HugeTLB pages, memory can be returned
to the buddy allocator for other uses.

Different architectures support different HugeTLB pages. For example, the
following table is the HugeTLB page size supported by x86 and arm64
architectures. Because arm64 supports 4k, 16k, and 64k base pages and
supports contiguous entries, so it supports many kinds of sizes of HugeTLB
page.

+--------------+-----------+-----------------------------------------------+
| Architecture | Page Size |                HugeTLB Page Size              |
+--------------+-----------+-----------+-----------+-----------+-----------+
|    x86-64    |    4KB    |    2MB    |    1GB    |           |           |
+--------------+-----------+-----------+-----------+-----------+-----------+
|              |    4KB    |   64KB    |    2MB    |    32MB   |    1GB    |
|              +-----------+-----------+-----------+-----------+-----------+
|    arm64     |   16KB    |    2MB    |   32MB    |     1GB   |           |
|              +-----------+-----------+-----------+-----------+-----------+
|              |   64KB    |    2MB    |  512MB    |    16GB   |           |
+--------------+-----------+-----------+-----------+-----------+-----------+

When the system boot up, every HugeTLB page has more than one ``struct page``
structs which size is (unit: pages)::

   struct_size = HugeTLB_Size / PAGE_SIZE * sizeof(struct page) / PAGE_SIZE

Where HugeTLB_Size is the size of the HugeTLB page. We know that the size
of the HugeTLB page is always n times PAGE_SIZE. So we can get the following
relationship::

   HugeTLB_Size = n * PAGE_SIZE

Then::

   struct_size = n * PAGE_SIZE / PAGE_SIZE * sizeof(struct page) / PAGE_SIZE
               = n * sizeof(struct page) / PAGE_SIZE

We can use huge mapping at the pud/pmd level for the HugeTLB page.

For the HugeTLB page of the pmd level mapping, then::

   struct_size = n * sizeof(struct page) / PAGE_SIZE
               = PAGE_SIZE / sizeof(pte_t) * sizeof(struct page) / PAGE_SIZE
               = sizeof(struct page) / sizeof(pte_t)
               = 64 / 8
               = 8 (pages)

Where n is how many pte entries which one page can contains. So the value of
n is (PAGE_SIZE / sizeof(pte_t)).

This optimization only supports 64-bit system, so the value of sizeof(pte_t)
is 8. And this optimization also applicable only when the size of ``struct page``
is a power of two. In most cases, the size of ``struct page`` is 64 bytes (e.g.
x86-64 and arm64). So if we use pmd level mapping for a HugeTLB page, the
size of ``struct page`` structs of it is 8 page frames which size depends on the
size of the base page.

For the HugeTLB page of the pud level mapping, then::

   struct_size = PAGE_SIZE / sizeof(pmd_t) * struct_size(pmd)
               = PAGE_SIZE / 8 * 8 (pages)
               = PAGE_SIZE (pages)

Where the struct_size(pmd) is the size of the ``struct page`` structs of a
HugeTLB page of the pmd level mapping.

E.g.: A 2MB HugeTLB page on x86_64 consists in 8 page frames while 1GB
HugeTLB page consists in 4096.

Next, we take the pmd level mapping of the HugeTLB page as an example to
show the internal implementation of this optimization. There are 8 pages
``struct page`` structs associated with a HugeTLB page which is pmd mapped.

Here is how things look before optimization::

    HugeTLB                  struct pages(8 pages)         page frame(8 pages)
 +-----------+ ---virt_to_page---> +-----------+   mapping to   +-----------+
 |           |                     |     0     | -------------> |     0     |
 |           |                     +-----------+                +-----------+
 |           |                     |     1     | -------------> |     1     |
 |           |                     +-----------+                +-----------+
 |           |                     |     2     | -------------> |     2     |
 |           |                     +-----------+                +-----------+
 |           |                     |     3     | -------------> |     3     |
 |           |                     +-----------+                +-----------+
 |           |                     |     4     | -------------> |     4     |
 |    PMD    |                     +-----------+                +-----------+
 |   level   |                     |     5     | -------------> |     5     |
 |  mapping  |                     +-----------+                +-----------+
 |           |                     |     6     | -------------> |     6     |
 |           |                     +-----------+                +-----------+
 |           |                     |     7     | -------------> |     7     |
 |           |                     +-----------+                +-----------+
 |           |
 |           |
 |           |
 +-----------+

The first page of ``struct page`` (page 0) associated with the HugeTLB page
contains the 4 ``struct page`` necessary to describe the HugeTLB. The remaining
pages of ``struct page`` (page 1 to page 7) are tail pages.

The optimization is only applied when the size of the struct page is a power
of 2. In this case, all tail pages of the same order are identical. See
compound_head(). This allows us to remap the tail pages of the vmemmap to a
shared, read-only page. The head page is also remapped to a new page. This
allows the original vmemmap pages to be freed.

Here is how things look after remapping::

    HugeTLB                  struct pages(8 pages)                 page frame (new)
 +-----------+ ---virt_to_page---> +-----------+   mapping to   +----------------+
 |           |                     |     0     | -------------> |       0        |
 |           |                     +-----------+                +----------------+
 |           |                     |     1     | ------┐
 |           |                     +-----------+       |
 |           |                     |     2     | ------┼        +----------------------------+
 |           |                     +-----------+       |        | A single, per-zone page    |
 |           |                     |     3     | ------┼------> | frame shared among all     |
 |           |                     +-----------+       |        | hugepages of the same size |
 |           |                     |     4     | ------┼        +----------------------------+
 |           |                     +-----------+       |
 |           |                     |     5     | ------┼
 |    PMD    |                     +-----------+       |
 |   level   |                     |     6     | ------┼
 |  mapping  |                     +-----------+       |
 |           |                     |     7     | ------┘
 |           |                     +-----------+
 |           |
 |           |
 |           |
 +-----------+

When a HugeTLB is freed to the buddy system, we should allocate 7 pages for
vmemmap pages and restore the previous mapping relationship.

For the HugeTLB page of the pud level mapping. It is similar to the former.
We also can use this approach to free (PAGE_SIZE - 1) vmemmap pages.

Apart from the HugeTLB page of the pmd/pud level mapping, some architectures
(e.g. aarch64) provides a contiguous bit in the translation table entries
that hints to the MMU to indicate that it is one of a contiguous set of
entries that can be cached in a single TLB entry.

The contiguous bit is used to increase the mapping size at the pmd and pte
(last) level. So this type of HugeTLB page can be optimized only when its
size of the ``struct page`` structs is greater than **1** page.

Device DAX
==========

The device-dax interface uses the same tail deduplication technique explained
in the previous chapter, except when used with the vmemmap in
the device (altmap).

The following page sizes are supported in DAX: PAGE_SIZE (4K on x86_64),
PMD_SIZE (2M on x86_64) and PUD_SIZE (1G on x86_64).
For powerpc equivalent details see Documentation/arch/powerpc/vmemmap_dedup.rst

The differences with HugeTLB are relatively minor.

It only use 3 ``struct page`` for storing all information as opposed
to 4 on HugeTLB pages.

There's no remapping of vmemmap given that device-dax memory is not part of
System RAM ranges initialized at boot. Thus the tail page deduplication
happens at a later stage when we populate the sections. HugeTLB reuses the
the head vmemmap page representing, whereas device-dax reuses the tail
vmemmap page. This results in only half of the savings compared to HugeTLB.

Deduplicated tail pages are not mapped read-only.

Here's how things look like on device-dax after the sections are populated::

 +-----------+ ---virt_to_page---> +-----------+   mapping to   +-----------+
 |           |                     |     0     | -------------> |     0     |
 |           |                     +-----------+                +-----------+
 |           |                     |     1     | -------------> |     1     |
 |           |                     +-----------+                +-----------+
 |           |                     |     2     | ----------------^ ^ ^ ^ ^ ^
 |           |                     +-----------+                   | | | | |
 |           |                     |     3     | ------------------+ | | | |
 |           |                     +-----------+                     | | | |
 |           |                     |     4     | --------------------+ | | |
 |    PMD    |                     +-----------+                       | | |
 |   level   |                     |     5     | ----------------------+ | |
 |  mapping  |                     +-----------+                         | |
 |           |                     |     6     | ------------------------+ |
 |           |                     +-----------+                           |
 |           |                     |     7     | --------------------------+
 |           |                     +-----------+
 |           |
 |           |
 |           |
 +-----------+

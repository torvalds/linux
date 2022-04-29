.. SPDX-License-Identifier: GPL-2.0

==================================
Free some vmemmap pages of HugeTLB
==================================

The struct page structures (page structs) are used to describe a physical
page frame. By default, there is a one-to-one mapping from a page frame to
it's corresponding page struct.

HugeTLB pages consist of multiple base page size pages and is supported by many
architectures. See Documentation/admin-guide/mm/hugetlbpage.rst for more
details. On the x86-64 architecture, HugeTLB pages of size 2MB and 1GB are
currently supported. Since the base page size on x86 is 4KB, a 2MB HugeTLB page
consists of 512 base pages and a 1GB HugeTLB page consists of 4096 base pages.
For each base page, there is a corresponding page struct.

Within the HugeTLB subsystem, only the first 4 page structs are used to
contain unique information about a HugeTLB page. __NR_USED_SUBPAGE provides
this upper limit. The only 'useful' information in the remaining page structs
is the compound_head field, and this field is the same for all tail pages.

By removing redundant page structs for HugeTLB pages, memory can be returned
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

When the system boot up, every HugeTLB page has more than one struct page
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
is 8. And this optimization also applicable only when the size of struct page
is a power of two. In most cases, the size of struct page is 64 bytes (e.g.
x86-64 and arm64). So if we use pmd level mapping for a HugeTLB page, the
size of struct page structs of it is 8 page frames which size depends on the
size of the base page.

For the HugeTLB page of the pud level mapping, then::

   struct_size = PAGE_SIZE / sizeof(pmd_t) * struct_size(pmd)
               = PAGE_SIZE / 8 * 8 (pages)
               = PAGE_SIZE (pages)

Where the struct_size(pmd) is the size of the struct page structs of a
HugeTLB page of the pmd level mapping.

E.g.: A 2MB HugeTLB page on x86_64 consists in 8 page frames while 1GB
HugeTLB page consists in 4096.

Next, we take the pmd level mapping of the HugeTLB page as an example to
show the internal implementation of this optimization. There are 8 pages
struct page structs associated with a HugeTLB page which is pmd mapped.

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

The value of page->compound_head is the same for all tail pages. The first
page of page structs (page 0) associated with the HugeTLB page contains the 4
page structs necessary to describe the HugeTLB. The only use of the remaining
pages of page structs (page 1 to page 7) is to point to page->compound_head.
Therefore, we can remap pages 1 to 7 to page 0. Only 1 page of page structs
will be used for each HugeTLB page. This will allow us to free the remaining
7 pages to the buddy allocator.

Here is how things look after remapping::

    HugeTLB                  struct pages(8 pages)         page frame(8 pages)
 +-----------+ ---virt_to_page---> +-----------+   mapping to   +-----------+
 |           |                     |     0     | -------------> |     0     |
 |           |                     +-----------+                +-----------+
 |           |                     |     1     | ---------------^ ^ ^ ^ ^ ^ ^
 |           |                     +-----------+                  | | | | | |
 |           |                     |     2     | -----------------+ | | | | |
 |           |                     +-----------+                    | | | | |
 |           |                     |     3     | -------------------+ | | | |
 |           |                     +-----------+                      | | | |
 |           |                     |     4     | ---------------------+ | | |
 |    PMD    |                     +-----------+                        | | |
 |   level   |                     |     5     | -----------------------+ | |
 |  mapping  |                     +-----------+                          | |
 |           |                     |     6     | -------------------------+ |
 |           |                     +-----------+                            |
 |           |                     |     7     | ---------------------------+
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
size of the struct page structs is greater than 1 page.

Notice: The head vmemmap page is not freed to the buddy allocator and all
tail vmemmap pages are mapped to the head vmemmap page frame. So we can see
more than one struct page struct with PG_head (e.g. 8 per 2 MB HugeTLB page)
associated with each HugeTLB page. The compound_head() can handle this
correctly (more details refer to the comment above compound_head()).

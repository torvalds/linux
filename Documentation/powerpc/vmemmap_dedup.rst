.. SPDX-License-Identifier: GPL-2.0

==========
Device DAX
==========

The device-dax interface uses the tail deduplication technique explained in
Documentation/mm/vmemmap_dedup.rst

On powerpc, vmemmap deduplication is only used with radix MMU translation. Also
with a 64K page size, only the devdax namespace with 1G alignment uses vmemmap
deduplication.

With 2M PMD level mapping, we require 32 struct pages and a single 64K vmemmap
page can contain 1024 struct pages (64K/sizeof(struct page)). Hence there is no
vmemmap deduplication possible.

With 1G PUD level mapping, we require 16384 struct pages and a single 64K
vmemmap page can contain 1024 struct pages (64K/sizeof(struct page)). Hence we
require 16 64K pages in vmemmap to map the struct page for 1G PUD level mapping.

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
 |    PUD    |                     +-----------+                       | | |
 |   level   |                     |     .     | ----------------------+ | |
 |  mapping  |                     +-----------+                         | |
 |           |                     |     .     | ------------------------+ |
 |           |                     +-----------+                           |
 |           |                     |     15    | --------------------------+
 |           |                     +-----------+
 |           |
 |           |
 |           |
 +-----------+


With 4K page size, 2M PMD level mapping requires 512 struct pages and a single
4K vmemmap page contains 64 struct pages(4K/sizeof(struct page)). Hence we
require 8 4K pages in vmemmap to map the struct page for 2M pmd level mapping.

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

With 1G PUD level mapping, we require 262144 struct pages and a single 4K
vmemmap page can contain 64 struct pages (4K/sizeof(struct page)). Hence we
require 4096 4K pages in vmemmap to map the struct pages for 1G PUD level
mapping.

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
 |    PUD    |                     +-----------+                       | | |
 |   level   |                     |     .     | ----------------------+ | |
 |  mapping  |                     +-----------+                         | |
 |           |                     |     .     | ------------------------+ |
 |           |                     +-----------+                           |
 |           |                     |   4095    | --------------------------+
 |           |                     +-----------+
 |           |
 |           |
 |           |
 +-----------+

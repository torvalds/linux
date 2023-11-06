======================
Kernel page table dump
======================

ptdump is a debugfs interface that provides a detailed dump of the
kernel page tables. It offers a comprehensive overview of the kernel
virtual memory layout as well as the attributes associated with the
various regions in a human-readable format. It is useful to dump the
kernel page tables to verify permissions and memory types. Examining the
page table entries and permissions helps identify potential security
vulnerabilities such as mappings with overly permissive access rights or
improper memory protections.

Memory hotplug allows dynamic expansion or contraction of available
memory without requiring a system reboot. To maintain the consistency
and integrity of the memory management data structures, arm64 makes use
of the ``mem_hotplug_lock`` semaphore in write mode. Additionally, in
read mode, ``mem_hotplug_lock`` supports an efficient implementation of
``get_online_mems()`` and ``put_online_mems()``. These protect the
offlining of memory being accessed by the ptdump code.

In order to dump the kernel page tables, enable the following
configurations and mount debugfs::

 CONFIG_GENERIC_PTDUMP=y
 CONFIG_PTDUMP_CORE=y
 CONFIG_PTDUMP_DEBUGFS=y

 mount -t debugfs nodev /sys/kernel/debug
 cat /sys/kernel/debug/kernel_page_tables

On analysing the output of ``cat /sys/kernel/debug/kernel_page_tables``
one can derive information about the virtual address range of the entry,
followed by size of the memory region covered by this entry, the
hierarchical structure of the page tables and finally the attributes
associated with each page. The page attributes provide information about
access permissions, execution capability, type of mapping such as leaf
level PTE or block level PGD, PMD and PUD, and access status of a page
within the kernel memory. Assessing these attributes can assist in
understanding the memory layout, access patterns and security
characteristics of the kernel pages.

Kernel virtual memory layout example::

 start address        end address         size             attributes
 +---------------------------------------------------------------------------------------+
 | ---[ Linear Mapping start ]---------------------------------------------------------- |
 | ..................                                                                    |
 | 0xfff0000000000000-0xfff0000000210000  2112K PTE RW NX SHD AF  UXN  MEM/NORMAL-TAGGED |
 | 0xfff0000000210000-0xfff0000001c00000 26560K PTE ro NX SHD AF  UXN  MEM/NORMAL        |
 | ..................                                                                    |
 | ---[ Linear Mapping end ]------------------------------------------------------------ |
 +---------------------------------------------------------------------------------------+
 | ---[ Modules start ]----------------------------------------------------------------- |
 | ..................                                                                    |
 | 0xffff800000000000-0xffff800008000000   128M PTE                                      |
 | ..................                                                                    |
 | ---[ Modules end ]------------------------------------------------------------------- |
 +---------------------------------------------------------------------------------------+
 | ---[ vmalloc() area ]---------------------------------------------------------------- |
 | ..................                                                                    |
 | 0xffff800008010000-0xffff800008200000  1984K PTE ro x  SHD AF       UXN  MEM/NORMAL   |
 | 0xffff800008200000-0xffff800008e00000    12M PTE ro x  SHD AF  CON  UXN  MEM/NORMAL   |
 | ..................                                                                    |
 | ---[ vmalloc() end ]----------------------------------------------------------------- |
 +---------------------------------------------------------------------------------------+
 | ---[ Fixmap start ]------------------------------------------------------------------ |
 | ..................                                                                    |
 | 0xfffffbfffdb80000-0xfffffbfffdb90000    64K PTE ro x  SHD AF  UXN  MEM/NORMAL        |
 | 0xfffffbfffdb90000-0xfffffbfffdba0000    64K PTE ro NX SHD AF  UXN  MEM/NORMAL        |
 | ..................                                                                    |
 | ---[ Fixmap end ]-------------------------------------------------------------------- |
 +---------------------------------------------------------------------------------------+
 | ---[ PCI I/O start ]----------------------------------------------------------------- |
 | ..................                                                                    |
 | 0xfffffbfffe800000-0xfffffbffff800000    16M PTE                                      |
 | ..................                                                                    |
 | ---[ PCI I/O end ]------------------------------------------------------------------- |
 +---------------------------------------------------------------------------------------+
 | ---[ vmemmap start ]----------------------------------------------------------------- |
 | ..................                                                                    |
 | 0xfffffc0002000000-0xfffffc0002200000     2M PTE RW NX SHD AF  UXN  MEM/NORMAL        |
 | 0xfffffc0002200000-0xfffffc0020000000   478M PTE                                      |
 | ..................                                                                    |
 | ---[ vmemmap end ]------------------------------------------------------------------- |
 +---------------------------------------------------------------------------------------+

``cat /sys/kernel/debug/kernel_page_tables`` output::

 0xfff0000001c00000-0xfff0000080000000     2020M PTE  RW NX SHD AF   UXN    MEM/NORMAL-TAGGED
 0xfff0000080000000-0xfff0000800000000       30G PMD
 0xfff0000800000000-0xfff0000800700000        7M PTE  RW NX SHD AF   UXN    MEM/NORMAL-TAGGED
 0xfff0000800700000-0xfff0000800710000       64K PTE  ro NX SHD AF   UXN    MEM/NORMAL-TAGGED
 0xfff0000800710000-0xfff0000880000000  2089920K PTE  RW NX SHD AF   UXN    MEM/NORMAL-TAGGED
 0xfff0000880000000-0xfff0040000000000     4062G PMD
 0xfff0040000000000-0xffff800000000000     3964T PGD

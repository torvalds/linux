.. SPDX-License-Identifier: GPL-2.0

===========
Page Tables
===========

Paged virtual memory was invented along with virtual memory as a concept in
1962 on the Ferranti Atlas Computer which was the first computer with paged
virtual memory. The feature migrated to newer computers and became a de facto
feature of all Unix-like systems as time went by. In 1985 the feature was
included in the Intel 80386, which was the CPU Linux 1.0 was developed on.

Page tables map virtual addresses as seen by the CPU into physical addresses
as seen on the external memory bus.

Linux defines page tables as a hierarchy which is currently five levels in
height. The architecture code for each supported architecture will then
map this to the restrictions of the hardware.

The physical address corresponding to the virtual address is often referenced
by the underlying physical page frame. The **page frame number** or **pfn**
is the physical address of the page (as seen on the external memory bus)
divided by `PAGE_SIZE`.

Physical memory address 0 will be *pfn 0* and the highest pfn will be
the last page of physical memory the external address bus of the CPU can
address.

With a page granularity of 4KB and a address range of 32 bits, pfn 0 is at
address 0x00000000, pfn 1 is at address 0x00001000, pfn 2 is at 0x00002000
and so on until we reach pfn 0xfffff at 0xfffff000. With 16KB pages pfs are
at 0x00004000, 0x00008000 ... 0xffffc000 and pfn goes from 0 to 0x3fffff.

As you can see, with 4KB pages the page base address uses bits 12-31 of the
address, and this is why `PAGE_SHIFT` in this case is defined as 12 and
`PAGE_SIZE` is usually defined in terms of the page shift as `(1 << PAGE_SHIFT)`

Over time a deeper hierarchy has been developed in response to increasing memory
sizes. When Linux was created, 4KB pages and a single page table called
`swapper_pg_dir` with 1024 entries was used, covering 4MB which coincided with
the fact that Torvald's first computer had 4MB of physical memory. Entries in
this single table were referred to as *PTE*:s - page table entries.

The software page table hierarchy reflects the fact that page table hardware has
become hierarchical and that in turn is done to save page table memory and
speed up mapping.

One could of course imagine a single, linear page table with enormous amounts
of entries, breaking down the whole memory into single pages. Such a page table
would be very sparse, because large portions of the virtual memory usually
remains unused. By using hierarchical page tables large holes in the virtual
address space does not waste valuable page table memory, because it will suffice
to mark large areas as unmapped at a higher level in the page table hierarchy.

Additionally, on modern CPUs, a higher level page table entry can point directly
to a physical memory range, which allows mapping a contiguous range of several
megabytes or even gigabytes in a single high-level page table entry, taking
shortcuts in mapping virtual memory to physical memory: there is no need to
traverse deeper in the hierarchy when you find a large mapped range like this.

The page table hierarchy has now developed into this::

  +-----+
  | PGD |
  +-----+
     |
     |   +-----+
     +-->| P4D |
         +-----+
            |
            |   +-----+
            +-->| PUD |
                +-----+
                   |
                   |   +-----+
                   +-->| PMD |
                       +-----+
                          |
                          |   +-----+
                          +-->| PTE |
                              +-----+


Symbols on the different levels of the page table hierarchy have the following
meaning beginning from the bottom:

- **pte**, `pte_t`, `pteval_t` = **Page Table Entry** - mentioned earlier.
  The *pte* is an array of `PTRS_PER_PTE` elements of the `pteval_t` type, each
  mapping a single page of virtual memory to a single page of physical memory.
  The architecture defines the size and contents of `pteval_t`.

  A typical example is that the `pteval_t` is a 32- or 64-bit value with the
  upper bits being a **pfn** (page frame number), and the lower bits being some
  architecture-specific bits such as memory protection.

  The **entry** part of the name is a bit confusing because while in Linux 1.0
  this did refer to a single page table entry in the single top level page
  table, it was retrofitted to be an array of mapping elements when two-level
  page tables were first introduced, so the *pte* is the lowermost page
  *table*, not a page table *entry*.

- **pmd**, `pmd_t`, `pmdval_t` = **Page Middle Directory**, the hierarchy right
  above the *pte*, with `PTRS_PER_PMD` references to the *pte*:s.

- **pud**, `pud_t`, `pudval_t` = **Page Upper Directory** was introduced after
  the other levels to handle 4-level page tables. It is potentially unused,
  or *folded* as we will discuss later.

- **p4d**, `p4d_t`, `p4dval_t` = **Page Level 4 Directory** was introduced to
  handle 5-level page tables after the *pud* was introduced. Now it was clear
  that we needed to replace *pgd*, *pmd*, *pud* etc with a figure indicating the
  directory level and that we cannot go on with ad hoc names any more. This
  is only used on systems which actually have 5 levels of page tables, otherwise
  it is folded.

- **pgd**, `pgd_t`, `pgdval_t` = **Page Global Directory** - the Linux kernel
  main page table handling the PGD for the kernel memory is still found in
  `swapper_pg_dir`, but each userspace process in the system also has its own
  memory context and thus its own *pgd*, found in `struct mm_struct` which
  in turn is referenced to in each `struct task_struct`. So tasks have memory
  context in the form of a `struct mm_struct` and this in turn has a
  `struct pgt_t *pgd` pointer to the corresponding page global directory.

To repeat: each level in the page table hierarchy is a *array of pointers*, so
the **pgd** contains `PTRS_PER_PGD` pointers to the next level below, **p4d**
contains `PTRS_PER_P4D` pointers to **pud** items and so on. The number of
pointers on each level is architecture-defined.::

        PMD
  --> +-----+           PTE
      | ptr |-------> +-----+
      | ptr |-        | ptr |-------> PAGE
      | ptr | \       | ptr |
      | ptr |  \        ...
      | ... |   \
      | ptr |    \         PTE
      +-----+     +----> +-----+
                         | ptr |-------> PAGE
                         | ptr |
                           ...


Page Table Folding
==================

If the architecture does not use all the page table levels, they can be *folded*
which means skipped, and all operations performed on page tables will be
compile-time augmented to just skip a level when accessing the next lower
level.

Page table handling code that wishes to be architecture-neutral, such as the
virtual memory manager, will need to be written so that it traverses all of the
currently five levels. This style should also be preferred for
architecture-specific code, so as to be robust to future changes.


MMU, TLB, and Page Faults
=========================

The `Memory Management Unit (MMU)` is a hardware component that handles virtual
to physical address translations. It may use relatively small caches in hardware
called `Translation Lookaside Buffers (TLBs)` and `Page Walk Caches` to speed up
these translations.

When CPU accesses a memory location, it provides a virtual address to the MMU,
which checks if there is the existing translation in the TLB or in the Page
Walk Caches (on architectures that support them). If no translation is found,
MMU uses the page walks to determine the physical address and create the map.

The dirty bit for a page is set (i.e., turned on) when the page is written to.
Each page of memory has associated permission and dirty bits. The latter
indicate that the page has been modified since it was loaded into memory.

If nothing prevents it, eventually the physical memory can be accessed and the
requested operation on the physical frame is performed.

There are several reasons why the MMU can't find certain translations. It could
happen because the CPU is trying to access memory that the current task is not
permitted to, or because the data is not present into physical memory.

When these conditions happen, the MMU triggers page faults, which are types of
exceptions that signal the CPU to pause the current execution and run a special
function to handle the mentioned exceptions.

There are common and expected causes of page faults. These are triggered by
process management optimization techniques called "Lazy Allocation" and
"Copy-on-Write". Page faults may also happen when frames have been swapped out
to persistent storage (swap partition or file) and evicted from their physical
locations.

These techniques improve memory efficiency, reduce latency, and minimize space
occupation. This document won't go deeper into the details of "Lazy Allocation"
and "Copy-on-Write" because these subjects are out of scope as they belong to
Process Address Management.

Swapping differentiates itself from the other mentioned techniques because it's
undesirable since it's performed as a means to reduce memory under heavy
pressure.

Swapping can't work for memory mapped by kernel logical addresses. These are a
subset of the kernel virtual space that directly maps a contiguous range of
physical memory. Given any logical address, its physical address is determined
with simple arithmetic on an offset. Accesses to logical addresses are fast
because they avoid the need for complex page table lookups at the expenses of
frames not being evictable and pageable out.

If the kernel fails to make room for the data that must be present in the
physical frames, the kernel invokes the out-of-memory (OOM) killer to make room
by terminating lower priority processes until pressure reduces under a safe
threshold.

Additionally, page faults may be also caused by code bugs or by maliciously
crafted addresses that the CPU is instructed to access. A thread of a process
could use instructions to address (non-shared) memory which does not belong to
its own address space, or could try to execute an instruction that want to write
to a read-only location.

If the above-mentioned conditions happen in user-space, the kernel sends a
`Segmentation Fault` (SIGSEGV) signal to the current thread. That signal usually
causes the termination of the thread and of the process it belongs to.

This document is going to simplify and show an high altitude view of how the
Linux kernel handles these page faults, creates tables and tables' entries,
check if memory is present and, if not, requests to load data from persistent
storage or from other devices, and updates the MMU and its caches.

The first steps are architecture dependent. Most architectures jump to
`do_page_fault()`, whereas the x86 interrupt handler is defined by the
`DEFINE_IDTENTRY_RAW_ERRORCODE()` macro which calls `handle_page_fault()`.

Whatever the routes, all architectures end up to the invocation of
`handle_mm_fault()` which, in turn, (likely) ends up calling
`__handle_mm_fault()` to carry out the actual work of allocating the page
tables.

The unfortunate case of not being able to call `__handle_mm_fault()` means
that the virtual address is pointing to areas of physical memory which are not
permitted to be accessed (at least from the current context). This
condition resolves to the kernel sending the above-mentioned SIGSEGV signal
to the process and leads to the consequences already explained.

`__handle_mm_fault()` carries out its work by calling several functions to
find the entry's offsets of the upper layers of the page tables and allocate
the tables that it may need.

The functions that look for the offset have names like `*_offset()`, where the
"*" is for pgd, p4d, pud, pmd, pte; instead the functions to allocate the
corresponding tables, layer by layer, are called `*_alloc`, using the
above-mentioned convention to name them after the corresponding types of tables
in the hierarchy.

The page table walk may end at one of the middle or upper layers (PMD, PUD).

Linux supports larger page sizes than the usual 4KB (i.e., the so called
`huge pages`). When using these kinds of larger pages, higher level pages can
directly map them, with no need to use lower level page entries (PTE). Huge
pages contain large contiguous physical regions that usually span from 2MB to
1GB. They are respectively mapped by the PMD and PUD page entries.

The huge pages bring with them several benefits like reduced TLB pressure,
reduced page table overhead, memory allocation efficiency, and performance
improvement for certain workloads. However, these benefits come with
trade-offs, like wasted memory and allocation challenges.

At the very end of the walk with allocations, if it didn't return errors,
`__handle_mm_fault()` finally calls `handle_pte_fault()`, which via `do_fault()`
performs one of `do_read_fault()`, `do_cow_fault()`, `do_shared_fault()`.
"read", "cow", "shared" give hints about the reasons and the kind of fault it's
handling.

The actual implementation of the workflow is very complex. Its design allows
Linux to handle page faults in a way that is tailored to the specific
characteristics of each architecture, while still sharing a common overall
structure.

To conclude this high altitude view of how Linux handles page faults, let's
add that the page faults handler can be disabled and enabled respectively with
`pagefault_disable()` and `pagefault_enable()`.

Several code path make use of the latter two functions because they need to
disable traps into the page faults handler, mostly to prevent deadlocks.

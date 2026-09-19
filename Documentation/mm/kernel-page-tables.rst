.. SPDX-License-Identifier: GPL-2.0

==================
Kernel Page Tables
==================

Introduction
============

The kernel page tables are created early during boot and, unlike the page
tables of user processes, most of them remain static throughout the system
lifetime.

Every architecture has a direct map (also called linear map) that maps the
physical memory at a fixed offset, so that a physical address can be
translated to a kernel virtual address with simple arithmetic. On most
architectures the direct map is a part of the kernel page tables, with a few
exceptions described in the `Direct map`_ section below.

On 32-bit systems with high memory the direct map covers only a part of the
physical memory, see Documentation/mm/highmem.rst.

The vmalloc area, present on every architecture with an MMU, is used for
allocations of virtually contiguous memory whose backing pages are not
necessarily physically contiguous, and for mapping of the device memory. Its
page tables are created and torn down at runtime, see
Documentation/mm/vmalloc.rst.

Architectures that use the `SPARSEMEM_VMEMMAP` memory model reserve a range of
kernel address space for the memory map, so that `struct page` objects appear
as a virtually contiguous array indexed by the page frame number, see
Documentation/mm/memory-model.rst.

Besides these, the kernel image may be mapped in a dedicated part of the
kernel address space rather than accessed through the direct map. In that
case its mapping is an alias of the direct map of the physical memory the
image occupies.

The rest of the kernel address space is architecture specific. For instance,
x86 has a region for the EFI runtime services and s390 has a region for the
code that has to run in the 31-bit addressing mode.

Direct map
==========

On most architectures the direct map is an ordinary part of the kernel page
tables. It is created early during boot with the largest pages the hardware
and the kernel configuration allow.

Several architectures are different.

MIPS
----

MIPS does not map the physical memory with page tables at all. Instead, a
part of the kernel virtual address space is a window into the physical
address space: the hardware translates the addresses that fall into that
window by a fixed transformation of the address bits, without walking the
page tables and without using the TLB. The memory attributes, such as
cacheability and the privilege level required to access the memory, are a
property of the window rather than of an individual page.

There are no page table entries describing the direct map, so its properties
cannot be changed for an individual page. On 32-bit systems the window covers
only 512 MiB of the physical memory, so everything above that is high memory.

LoongArch
---------

Like MIPS, LoongArch maps the physical memory with a hardware window. The
window covers 256 TiB of the physical address space on 64-bit and 512 MiB on
32-bit systems, and everything above that is high memory.

The window occupies the lower part of the kernel address space. The upper
part, which includes the vmalloc area, is mapped with kernel page tables.

PowerPC with the hash MMU
-------------------------

On 64-bit PowerPC systems with the hash MMU the direct map does not exist in
the Linux page tables. It is installed into the hardware hash page table early
during boot.

Modifying such mappings requires updating the hash page table directly, and
the hash MMU code implements this only for the kernel image permissions,
`debug_pagealloc` and KFENCE.

With the radix MMU the direct map is a part of the ordinary kernel page
tables.

Modifying the kernel page tables
================================

Except for the vmalloc area, the kernel page tables are mostly static. Still,
there are cases when the permissions of existing kernel mappings have to be
updated, for instance when a module is loaded and its text becomes read-only
and executable, or when a page is temporarily removed from the direct map to
reduce its exposure.

There are two families of functions for this, both declared in
`include/linux/set_memory.h`:

* `set_memory_*()` change permissions of an arbitrary kernel mapping. They
  take a kernel virtual address and the number of pages.

* `set_direct_map_*()` change permissions of the direct mapping for the range
  of page frames starting at the page represented by a `struct page`. They take
  a `struct page` pointer and the number of pages.

Architectures that implement `set_memory()` select `CONFIG_ARCH_HAS_SET_MEMORY`

Architectures that implement `set_direct_map()` select
`CONFIG_ARCH_HAS_SET_DIRECT_MAP`.

Common semantics
----------------

Ranges
~~~~~~

The `set_memory()` functions expect a range described by a start address and a
number of pages. The address must be page aligned and the entire range must be
covered by page table entries the architecture knows how to update.

The entries may be marked as not present, set_memory_p() and set_memory_valid()
exist exactly to bring such a mapping back.

When a range does not qualify, an architecture will usually say so by returning
an error and sometimes by a WARN()ing as well, unless it prefers to keep it to
itself and return success, see `Architecture specific differences`_.

The `set_direct_map()` functions expect a range described by the first
`struct page` and a number of pages, and they update the direct map starting
at that page.

The pages that follow the first one are updated regardless of what they are, so
the caller has to make sure that the range does not extend beyond the memory it
owns.

Some architectures cannot split a large mapping, and they reject a range that
is a part of one, see `Architecture specific differences`_.

Calling `set_memory()` with the number of pages set to zero is a no-op that
returns success, except on arm64, where doing nothing to the wrong address is
still an error.

Aliases
~~~~~~~

A physical page may be mapped several times, for instance in the direct map
and in the vmalloc area, and the permissions of these mappings may differ.

Whether the direct map alias is updated by a `set_memory()` call, and
which permission bits make it there, is entirely up to the architecture, and
there is not much agreement between them, see
`Architecture specific differences`_.

Relying on that is a gamble; code that needs the direct map alias to change
should say so with the `set_direct_map()` APIs.

Failures and partial updates
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Both families of functions return 0 on success and a negative error code on
failure. The most common failures are `-EINVAL` for a range that cannot be
handled and `-ENOMEM` when splitting a large mapping fails to allocate a page
table.

Some of the range checks happen upfront, so their failure leaves the page
tables unchanged.

**The update is not atomic and there is no rollback.**

The architecture implementations walk the range and update the page tables as
they go, and they stop at the first entry that cannot be updated. When an error
is returned, an arbitrary prefix of the range may have been updated already,
and the same is true for the direct map alias when the architecture updates it.

For example, when a range spans two large mappings and splitting the second
one fails because there is no memory for a page table, the first one is
already split and updated by the time the error is returned.

None of the APIs inform the caller where in the range they failed, so reverting
such a partial update is possible in principle but unreliable in practice.

The caller may try to restore the original permissions over the entire range,
but that revert goes through the very code that has just failed, which does
not inspire much confidence.

The callers should therefore be prepared to give up on the memory in question:
leak it or panic, but never return it to the allocator before the permissions
are restored and never assume that the requested permissions are in effect.
Neither option is appealing, but both beat handing out a page whose
permissions nobody knows.

TLB flushing
~~~~~~~~~~~~

The `set_memory()` functions flush the TLB for the affected range before
they return, so that the new permissions are in effect for every CPU.

The `set_direct_map()` functions have `_noflush` in their names because when
they were first introduced on x86, the intention was that the TLB flushing
could be optimized by letting the caller handle it.

For example, vfree() batches the TLB flushes for the areas allocated with
`VM_FLUSH_RESET_PERMS`, folding the flush of the direct map into the flush it
has to do for the vmalloc mapping anyway.

Some architectures flush the TLB in the `_noflush` functions anyway, so the
name is best read as a suggestion. It does not make the flush by the caller
unnecessary, it only makes it more expensive.

A caller that changes the permissions to more restrictive ones must flush the
TLB itself.

Context
~~~~~~~

Architectures use different locking mechanisms to synchronize kernel page table
updates, and both families of functions may sleep, for instance when they
allocate memory to split a large mapping.

The caller cannot presume it is safe to call these APIs from an atomic context.

The `set_direct_map()` functions must not be called for high memory pages,
which have no direct map alias to update.

Unimplemented APIs silently succeed
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When an architecture does not implement these APIs, the generic stubs in
`include/linux/set_memory.h` return 0, that is, they report success for work
they have no intention of doing.

The same happens inside several architecture implementations. The arm64
`set_direct_map()` functions return 0 when can_set_direct_map() is false, and
the LoongArch `set_memory()` functions return 0 for the addresses in its
windowed direct mapping, which is not backed by page tables at all.

Returning success without actually updating the page tables is a deliberate
trade-off that keeps the callers free of `#ifdef`\ s, but they have to realize:

**a return value of 0 does not imply that the permissions were actually
changed.**

For best-effort hardening that is good enough. When correctness or security
depends on the permissions, the caller has to make sure the architecture
really implements what it needs. For instance, secretmem depends on
`CONFIG_ARCH_HAS_SET_DIRECT_MAP` and calls can_set_direct_map() at runtime.

Architecture specific differences
=================================

The APIs are implemented by seven architectures and, beyond the common
semantics described above, their behaviour differs in several respects.

Which of the APIs are implemented:

=========  =====================  =========================
Arch       `ARCH_HAS_SET_MEMORY`  `ARCH_HAS_SET_DIRECT_MAP`
=========  =====================  =========================
arm        yes                    no
arm64      yes                    yes
loongarch  yes                    yes
powerpc    yes                    no
riscv      yes (MMU only)         yes (MMU only)
s390       yes                    yes
x86        yes                    yes
=========  =====================  =========================

Only set_memory_ro(), set_memory_rw(), set_memory_x() and set_memory_nx() are
available everywhere, and even these are not universal: some architectures
restrict the ranges that can be modified, for instance arm64 rejects direct map
addresses and accepts only addresses in vmalloc space. The architectures that
may run on hardware without an execute permission bit, like x86 and s390,
silently skip the update of the executable bit.

set_memory_rox() has a generic implementation that calls set_memory_ro() and
set_memory_x() in turn; PowerPC, s390 and x86 override it with a single-pass
version.

Several architectures define additional APIs. Some of those may have identical
semantics but different names. For example, making a mapping present or not
present is spelled differently: set_memory_p() and set_memory_np() on x86 and
PowerPC, set_memory_valid() on arm and arm64.

The direct map and the kernel image are normally mapped with the largest
possible pages, and changing the permissions of a single page inside such a
mapping requires splitting it, which not every architecture can do.

arm
---

* Does not implement `set_direct_map()`.
* Provides set_memory_valid().
* set_memory_ro(), set_memory_rw(), set_memory_x() and set_memory_nx() accept
  only vmalloc and module addresses.
* set_memory_valid() accepts any address.
* Does not update mapping aliases.

arm64
-----

* Provides set_memory_valid().
* Provides the memory encryption helpers, which are effective only when the
  kernel runs as a confidential guest.
* set_memory_ro(), set_memory_rw(), set_memory_x() and set_memory_nx() accept
  only vmalloc and module addresses:

  - the range must fit in the VM area that contains its start
  - the VM area must have `VM_ALLOC` set and `VM_ALLOW_HUGE_VMAP` clear

* set_memory_valid() accepts any address.
* The encryption helpers accept only direct map addresses.
* The `set_memory()` functions propagate the read-only and the read-write
  changes to the direct map alias when `rodata=full` is in effect.
* Splits leaf mappings before the update on the hardware that supports it.
  The split itself may be partial when it fails midway, but the permissions are
  left untouched in that case.
  Without support for splitting large mappings, a range that covers a leaf
  entry only partially fails with a WARN()ing and `-EINVAL`.
  If a range spans one or more full leaf entries and a partial leaf entry, the
  permissions of the full leaf entries are updated before the failure.
* The `set_direct_map()` functions return 0 without doing anything when the
  direct map cannot be modified, see can_set_direct_map().
* Skips the TLB flush in `set_memory()` when the update only turns an invalid
  mapping into a valid one.
* Does not flush TLB in `set_direct_map()`.

LoongArch
---------

* Accepts only the addresses above the hardware window and silently returns
  success for the rest, see `Direct map`_.
* Does not update mapping aliases.
* Does not split anything: a leaf entry is updated as a whole, which changes
  the permissions of the entire large mapping.
* Flushes the TLB in `set_direct_map()`.

PowerPC
-------

* Does not implement `set_direct_map()`.
* Provides set_memory_np() and set_memory_p().
* Rejects huge vmalloc mappings.
* With the hash MMU on 64-bit systems accepts nothing but the vmalloc and the
  I/O regions.
* With the radix MMU accepts direct map addresses, but still cannot split a
  large mapping.
* Does not update mapping aliases.

riscv
-----

* Implements both APIs only when the MMU is enabled.
* Provides set_memory_rw_nx().
* The `set_memory()` functions accept any mapped kernel address, including the
  direct map, but a vmalloc range must have the `pages` array of its VM area
  populated, which rules out vmap() and ioremap() mappings.
* On 64-bit systems the `set_memory()` functions update the direct map alias of
  a vmalloc range, including the executable bit.
* Does not split vmalloc ranges: a leaf entry is updated as a whole, which
  changes the permissions of the entire large mapping.
* Splits the direct map on 64-bit systems.
* Flushes the TLB in `set_direct_map()`.

s390
----

* Provides set_memory_4k(), set_memory_rwnx() and the
  `__set_memory_*(start, end)` variants that take a range rather than a page
  count.
* The `set_memory()` functions accept any mapped kernel address, including the
  direct map.
* Skips the update of the executable bit when the hardware has no support for
  it.
* The `set_memory()` functions propagate only the read-only and read-write
  changes to the direct map alias of a `VM_ALLOC` area, and deliberately not
  the executable bit.
* Splits leaf PUD and PMD entries when the range is not aligned to them or when
  set_memory_4k() is requested.
* Updates the page table entries with instructions that invalidate the
  corresponding TLB entries, so no separate flush is needed anywhere.

x86
---

* Provides the largest set of operations on top of the common ones:

  - the cache attribute helpers: set_memory_uc(), set_memory_wc(),
    set_memory_wb()
  - presence control: set_memory_np() and set_memory_p()
  - set_memory_4k()
  - set_memory_global() and set_memory_nonglobal()
  - the array variants that operate on `struct page` arrays or arrays of
    virtual addresses
  - memory encryption: set_memory_encrypted() and set_memory_decrypted()

* The `set_memory()` functions accept any mapped kernel address, including the
  direct map, and silently succeed for the unmapped holes inside it.
* Does nothing in set_memory_x() and set_memory_nx() when the CPU has no
  execute permission bit.
* The `set_memory()` functions apply the change to the direct map alias and,
  for the kernel image, to the high kernel mapping. The NX bit is never
  propagated, so that the direct map stays non-executable.
* Splits large mappings on demand and can collapse them back when the
  permissions become uniform again.
* Does not flush the TLB in `set_direct_map()`, but splitting a large
  mapping flushes it anyway.

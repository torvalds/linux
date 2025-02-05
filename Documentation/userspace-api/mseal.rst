.. SPDX-License-Identifier: GPL-2.0

=====================
Introduction of mseal
=====================

:Author: Jeff Xu <jeffxu@chromium.org>

Modern CPUs support memory permissions such as RW and NX bits. The memory
permission feature improves security stance on memory corruption bugs, i.e.
the attacker can’t just write to arbitrary memory and point the code to it,
the memory has to be marked with X bit, or else an exception will happen.

Memory sealing additionally protects the mapping itself against
modifications. This is useful to mitigate memory corruption issues where a
corrupted pointer is passed to a memory management system. For example,
such an attacker primitive can break control-flow integrity guarantees
since read-only memory that is supposed to be trusted can become writable
or .text pages can get remapped. Memory sealing can automatically be
applied by the runtime loader to seal .text and .rodata pages and
applications can additionally seal security critical data at runtime.

A similar feature already exists in the XNU kernel with the
VM_FLAGS_PERMANENT flag [1] and on OpenBSD with the mimmutable syscall [2].

SYSCALL
=======
mseal syscall signature
-----------------------
   ``int mseal(void \* addr, size_t len, unsigned long flags)``

   **addr**/**len**: virtual memory address range.
      The address range set by **addr**/**len** must meet:
         - The start address must be in an allocated VMA.
         - The start address must be page aligned.
         - The end address (**addr** + **len**) must be in an allocated VMA.
         - no gap (unallocated memory) between start and end address.

      The ``len`` will be paged aligned implicitly by the kernel.

   **flags**: reserved for future use.

   **Return values**:
      - **0**: Success.
      - **-EINVAL**:
         * Invalid input ``flags``.
         * The start address (``addr``) is not page aligned.
         * Address range (``addr`` + ``len``) overflow.
      - **-ENOMEM**:
         * The start address (``addr``) is not allocated.
         * The end address (``addr`` + ``len``) is not allocated.
         * A gap (unallocated memory) between start and end address.
      - **-EPERM**:
         * sealing is supported only on 64-bit CPUs, 32-bit is not supported.

   **Note about error return**:
      - For above error cases, users can expect the given memory range is
        unmodified, i.e. no partial update.
      - There might be other internal errors/cases not listed here, e.g.
        error during merging/splitting VMAs, or the process reaching the maximum
        number of supported VMAs. In those cases, partial updates to the given
        memory range could happen. However, those cases should be rare.

   **Architecture support**:
      mseal only works on 64-bit CPUs, not 32-bit CPUs.

   **Idempotent**:
      users can call mseal multiple times. mseal on an already sealed memory
      is a no-action (not error).

   **no munseal**
      Once mapping is sealed, it can't be unsealed. The kernel should never
      have munseal, this is consistent with other sealing feature, e.g.
      F_SEAL_SEAL for file.

Blocked mm syscall for sealed mapping
-------------------------------------
   It might be important to note: **once the mapping is sealed, it will
   stay in the process's memory until the process terminates**.

   Example::

         *ptr = mmap(0, 4096, PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
         rc = mseal(ptr, 4096, 0);
         /* munmap will fail */
         rc = munmap(ptr, 4096);
         assert(rc < 0);

   Blocked mm syscall:
      - munmap
      - mmap
      - mremap
      - mprotect and pkey_mprotect
      - some destructive madvise behaviors: MADV_DONTNEED, MADV_FREE,
        MADV_DONTNEED_LOCKED, MADV_FREE, MADV_DONTFORK, MADV_WIPEONFORK

   The first set of syscalls to block is munmap, mremap, mmap. They can
   either leave an empty space in the address space, therefore allowing
   replacement with a new mapping with new set of attributes, or can
   overwrite the existing mapping with another mapping.

   mprotect and pkey_mprotect are blocked because they changes the
   protection bits (RWX) of the mapping.

   Certain destructive madvise behaviors, specifically MADV_DONTNEED,
   MADV_FREE, MADV_DONTNEED_LOCKED, and MADV_WIPEONFORK, can introduce
   risks when applied to anonymous memory by threads lacking write
   permissions. Consequently, these operations are prohibited under such
   conditions. The aforementioned behaviors have the potential to modify
   region contents by discarding pages, effectively performing a memset(0)
   operation on the anonymous memory.

   Kernel will return -EPERM for blocked syscalls.

   When blocked syscall return -EPERM due to sealing, the memory regions may
   or may not be changed, depends on the syscall being blocked:

      - munmap: munmap is atomic. If one of VMAs in the given range is
        sealed, none of VMAs are updated.
      - mprotect, pkey_mprotect, madvise: partial update might happen, e.g.
        when mprotect over multiple VMAs, mprotect might update the beginning
        VMAs before reaching the sealed VMA and return -EPERM.
      - mmap and mremap: undefined behavior.

Use cases
=========
- glibc:
  The dynamic linker, during loading ELF executables, can apply sealing to
  mapping segments.

- Chrome browser: protect some security sensitive data structures.

When not to use mseal
=====================
Applications can apply sealing to any virtual memory region from userspace,
but it is *crucial to thoroughly analyze the mapping's lifetime* prior to
apply the sealing. This is because the sealed mapping *won’t be unmapped*
until the process terminates or the exec system call is invoked.

For example:
   - aio/shm
     aio/shm can call mmap and  munmap on behalf of userspace, e.g.
     ksys_shmdt() in shm.c. The lifetimes of those mapping are not tied to
     the lifetime of the process. If those memories are sealed from userspace,
     then munmap will fail, causing leaks in VMA address space during the
     lifetime of the process.

   - ptr allocated by malloc (heap)
     Don't use mseal on the memory ptr return from malloc().
     malloc() is implemented by allocator, e.g. by glibc. Heap manager might
     allocate a ptr from brk or mapping created by mmap.
     If an app calls mseal on a ptr returned from malloc(), this can affect
     the heap manager's ability to manage the mappings; the outcome is
     non-deterministic.

     Example::

        ptr = malloc(size);
        /* don't call mseal on ptr return from malloc. */
        mseal(ptr, size);
        /* free will success, allocator can't shrink heap lower than ptr */
        free(ptr);

mseal doesn't block
===================
In a nutshell, mseal blocks certain mm syscall from modifying some of VMA's
attributes, such as protection bits (RWX). Sealed mappings doesn't mean the
memory is immutable.

As Jann Horn pointed out in [3], there are still a few ways to write
to RO memory, which is, in a way, by design. And those could be blocked
by different security measures.

Those cases are:

   - Write to read-only memory through /proc/self/mem interface (FOLL_FORCE).
   - Write to read-only memory through ptrace (such as PTRACE_POKETEXT).
   - userfaultfd.

The idea that inspired this patch comes from Stephen Röttger’s work in V8
CFI [4]. Chrome browser in ChromeOS will be the first user of this API.

Reference
=========
- [1] https://github.com/apple-oss-distributions/xnu/blob/1031c584a5e37aff177559b9f69dbd3c8c3fd30a/osfmk/mach/vm_statistics.h#L274
- [2] https://man.openbsd.org/mimmutable.2
- [3] https://lore.kernel.org/lkml/CAG48ez3ShUYey+ZAFsU2i1RpQn0a5eOs2hzQ426FkcgnfUGLvA@mail.gmail.com
- [4] https://docs.google.com/document/d/1O2jwK4dxI3nRcOJuPYkonhTkNQfbmwdvxQMyXgeaRHo/edit#heading=h.bvaojj9fu6hc

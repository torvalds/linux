====================
High Memory Handling
====================

By: Peter Zijlstra <a.p.zijlstra@chello.nl>

.. contents:: :local:

What Is High Memory?
====================

High memory (highmem) is used when the size of physical memory approaches or
exceeds the maximum size of virtual memory.  At that point it becomes
impossible for the kernel to keep all of the available physical memory mapped
at all times.  This means the kernel needs to start using temporary mappings of
the pieces of physical memory that it wants to access.

The part of (physical) memory not covered by a permanent mapping is what we
refer to as 'highmem'.  There are various architecture dependent constraints on
where exactly that border lies.

In the i386 arch, for example, we choose to map the kernel into every process's
VM space so that we don't have to pay the full TLB invalidation costs for
kernel entry/exit.  This means the available virtual memory space (4GiB on
i386) has to be divided between user and kernel space.

The traditional split for architectures using this approach is 3:1, 3GiB for
userspace and the top 1GiB for kernel space::

		+--------+ 0xffffffff
		| Kernel |
		+--------+ 0xc0000000
		|        |
		| User   |
		|        |
		+--------+ 0x00000000

This means that the kernel can at most map 1GiB of physical memory at any one
time, but because we need virtual address space for other things - including
temporary maps to access the rest of the physical memory - the actual direct
map will typically be less (usually around ~896MiB).

Other architectures that have mm context tagged TLBs can have separate kernel
and user maps.  Some hardware (like some ARMs), however, have limited virtual
space when they use mm context tags.


Temporary Virtual Mappings
==========================

The kernel contains several ways of creating temporary mappings. The following
list shows them in order of preference of use.

* kmap_local_page(), kmap_local_folio() - These functions are used to create
  short term mappings. They can be invoked from any context (including
  interrupts) but the mappings can only be used in the context which acquired
  them. The only differences between them consist in the first taking a pointer
  to a struct page and the second taking a pointer to struct folio and the byte
  offset within the folio which identifies the page.

  These functions should always be used, whereas kmap_atomic() and kmap() have
  been deprecated.

  These mappings are thread-local and CPU-local, meaning that the mapping
  can only be accessed from within this thread and the thread is bound to the
  CPU while the mapping is active. Although preemption is never disabled by
  this function, the CPU can not be unplugged from the system via
  CPU-hotplug until the mapping is disposed.

  It's valid to take pagefaults in a local kmap region, unless the context
  in which the local mapping is acquired does not allow it for other reasons.

  As said, pagefaults and preemption are never disabled. There is no need to
  disable preemption because, when context switches to a different task, the
  maps of the outgoing task are saved and those of the incoming one are
  restored.

  kmap_local_page(), as well as kmap_local_folio() always returns valid virtual
  kernel addresses and it is assumed that kunmap_local() will never fail.

  On CONFIG_HIGHMEM=n kernels and for low memory pages they return the
  virtual address of the direct mapping. Only real highmem pages are
  temporarily mapped. Therefore, users may call a plain page_address()
  for pages which are known to not come from ZONE_HIGHMEM. However, it is
  always safe to use kmap_local_{page,folio}() / kunmap_local().

  While they are significantly faster than kmap(), for the highmem case they
  come with restrictions about the pointers validity. Contrary to kmap()
  mappings, the local mappings are only valid in the context of the caller
  and cannot be handed to other contexts. This implies that users must
  be absolutely sure to keep the use of the return address local to the
  thread which mapped it.

  Most code can be designed to use thread local mappings. User should
  therefore try to design their code to avoid the use of kmap() by mapping
  pages in the same thread the address will be used and prefer
  kmap_local_page() or kmap_local_folio().

  Nesting kmap_local_page() and kmap_atomic() mappings is allowed to a certain
  extent (up to KMAP_TYPE_NR) but their invocations have to be strictly ordered
  because the map implementation is stack based. See kmap_local_page() kdocs
  (included in the "Functions" section) for details on how to manage nested
  mappings.

* kmap_atomic(). This function has been deprecated; use kmap_local_page().

  NOTE: Conversions to kmap_local_page() must take care to follow the mapping
  restrictions imposed on kmap_local_page(). Furthermore, the code between
  calls to kmap_atomic() and kunmap_atomic() may implicitly depend on the side
  effects of atomic mappings, i.e. disabling page faults or preemption, or both.
  In that case, explicit calls to pagefault_disable() or preempt_disable() or
  both must be made in conjunction with the use of kmap_local_page().

  [Legacy documentation]

  This permits a very short duration mapping of a single page.  Since the
  mapping is restricted to the CPU that issued it, it performs well, but
  the issuing task is therefore required to stay on that CPU until it has
  finished, lest some other task displace its mappings.

  kmap_atomic() may also be used by interrupt contexts, since it does not
  sleep and the callers too may not sleep until after kunmap_atomic() is
  called.

  Each call of kmap_atomic() in the kernel creates a non-preemptible section
  and disable pagefaults. This could be a source of unwanted latency. Therefore
  users should prefer kmap_local_page() instead of kmap_atomic().

  It is assumed that k[un]map_atomic() won't fail.

* kmap(). This function has been deprecated; use kmap_local_page().

  NOTE: Conversions to kmap_local_page() must take care to follow the mapping
  restrictions imposed on kmap_local_page(). In particular, it is necessary to
  make sure that the kernel virtual memory pointer is only valid in the thread
  that obtained it.

  [Legacy documentation]

  This should be used to make short duration mapping of a single page with no
  restrictions on preemption or migration. It comes with an overhead as mapping
  space is restricted and protected by a global lock for synchronization. When
  mapping is no longer needed, the address that the page was mapped to must be
  released with kunmap().

  Mapping changes must be propagated across all the CPUs. kmap() also
  requires global TLB invalidation when the kmap's pool wraps and it might
  block when the mapping space is fully utilized until a slot becomes
  available. Therefore, kmap() is only callable from preemptible context.

  All the above work is necessary if a mapping must last for a relatively
  long time but the bulk of high-memory mappings in the kernel are
  short-lived and only used in one place. This means that the cost of
  kmap() is mostly wasted in such cases. kmap() was not intended for long
  term mappings but it has morphed in that direction and its use is
  strongly discouraged in newer code and the set of the preceding functions
  should be preferred.

  On 64-bit systems, calls to kmap_local_page(), kmap_atomic() and kmap() have
  no real work to do because a 64-bit address space is more than sufficient to
  address all the physical memory whose pages are permanently mapped.

* vmap().  This can be used to make a long duration mapping of multiple
  physical pages into a contiguous virtual space.  It needs global
  synchronization to unmap.


Cost of Temporary Mappings
==========================

The cost of creating temporary mappings can be quite high.  The arch has to
manipulate the kernel's page tables, the data TLB and/or the MMU's registers.

If CONFIG_HIGHMEM is not set, then the kernel will try and create a mapping
simply with a bit of arithmetic that will convert the page struct address into
a pointer to the page contents rather than juggling mappings about.  In such a
case, the unmap operation may be a null operation.

If CONFIG_MMU is not set, then there can be no temporary mappings and no
highmem.  In such a case, the arithmetic approach will also be used.


i386 PAE
========

The i386 arch, under some circumstances, will permit you to stick up to 64GiB
of RAM into your 32-bit machine.  This has a number of consequences:

* Linux needs a page-frame structure for each page in the system and the
  pageframes need to live in the permanent mapping, which means:

* you can have 896M/sizeof(struct page) page-frames at most; with struct
  page being 32-bytes that would end up being something in the order of 112G
  worth of pages; the kernel, however, needs to store more than just
  page-frames in that memory...

* PAE makes your page tables larger - which slows the system down as more
  data has to be accessed to traverse in TLB fills and the like.  One
  advantage is that PAE has more PTE bits and can provide advanced features
  like NX and PAT.

The general recommendation is that you don't use more than 8GiB on a 32-bit
machine - although more might work for you and your workload, you're pretty
much on your own - don't expect kernel developers to really care much if things
come apart.


Functions
=========

.. kernel-doc:: include/linux/highmem.h
.. kernel-doc:: mm/highmem.c
.. kernel-doc:: include/linux/highmem-internal.h

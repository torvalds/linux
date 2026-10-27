.. SPDX-License-Identifier: GPL-2.0

===========================
mmap_prepare callback HOWTO
===========================

Introduction
============

The ``struct file->f_op->mmap()`` callback has been deprecated as it is both a
stability and security risk, and doesn't always permit the merging of adjacent
mappings resulting in unnecessary memory fragmentation.

It has been replaced with the ``file->f_op->mmap_prepare()`` callback which
solves these problems.

This hook is called right at the beginning of setting up the mapping, and
importantly it is invoked *before* any merging of adjacent mappings has taken
place.

If an error arises upon mapping, it might arise after this callback has been
invoked, therefore it should be treated as effectively stateless.

That is - no resources should be allocated nor state updated to reflect that a
mapping has been established, as the mapping may either be merged, or fail to be
mapped after the callback is complete.

Mapped callback
---------------

If resources need to be allocated per-mapping, or state such as a reference
count needs to be manipulated, this should be done using the ``vm_ops->mapped``
hook, which itself should be set by the >mmap_prepare hook.

This callback is only invoked if a new mapping has been established and was not
merged with any other, and is invoked at a point where no error may occur before
the mapping is established.

You may return an error to the callback itself, which will cause the mapping to
become unmapped and an error returned to the mmap() caller. This is useful if
resources need to be allocated, and that allocation might fail.

How To Use
==========

In your driver's struct file_operations struct, specify an ``mmap_prepare``
callback rather than an ``mmap`` one, e.g. for ext4:

.. code-block:: C

    const struct file_operations ext4_file_operations = {
        ...
        .mmap_prepare    = ext4_file_mmap_prepare,
    };

This has a signature of ``int (*mmap_prepare)(struct vm_area_desc *)``.

Examining the struct vm_area_desc type:

.. code-block:: C

    struct vm_area_desc {
        /* Immutable state. */
        const struct mm_struct *const mm;
        struct file *const file; /* May vary from vm_file in stacked callers. */
        unsigned long start;
        unsigned long end;

        /* Mutable fields. Populated with initial state. */
        pgoff_t pgoff;
        struct file *vm_file;
        vma_flags_t vma_flags;
        pgprot_t page_prot;

        /* Write-only fields. */
        const struct vm_operations_struct *vm_ops;
        void *private_data;

        /* Take further action? */
        struct mmap_action action;
    };

This is straightforward - you have all the fields you need to set up the
mapping, and you can update the mutable and writable fields, for instance:

.. code-block:: C

    static int ext4_file_mmap_prepare(struct vm_area_desc *desc)
    {
        int ret;
        struct file *file = desc->file;
        struct inode *inode = file->f_mapping->host;

        ...

        file_accessed(file);
        if (IS_DAX(file_inode(file))) {
            desc->vm_ops = &ext4_dax_vm_ops;
            vma_desc_set_flags(desc, VMA_HUGEPAGE_BIT);
        } else {
            desc->vm_ops = &ext4_file_vm_ops;
        }
        return 0;
    }

Importantly, you no longer have to dance around with reference counts or locks
when updating these fields - **you can simply go ahead and change them**.

Everything is taken care of by the mapping code.

VMA Flags
---------

Along with ``mmap_prepare``, VMA flags have undergone an overhaul. Where before
you would invoke one of vm_flags_init(), vm_flags_reset(), vm_flags_set(),
vm_flags_clear(), and vm_flags_mod() to modify flags (and to have the
locking done correctly for you, this is no longer necessary.

Also, the legacy approach of specifying VMA flags via ``VM_READ``, ``VM_WRITE``,
etc. - i.e. using a ``-VM_xxx``- macro has changed too.

When implementing mmap_prepare(), reference flags by their bit number, defined
as a ``VMA_xxx_BIT`` macro, e.g. ``VMA_READ_BIT``, ``VMA_WRITE_BIT`` etc.,
and use one of (where ``desc`` is a pointer to struct vm_area_desc):

* ``vma_desc_test_any(desc, ...)`` - Specify a comma-separated list of flags
  you wish to test for (whether _any_ are set), e.g. - ``vma_desc_test_any(
  desc, VMA_WRITE_BIT, VMA_MAYWRITE_BIT)`` - returns ``true`` if either are set,
  otherwise ``false``.
* ``vma_desc_set_flags(desc, ...)`` - Update the VMA descriptor flags to set
  additional flags specified by a comma-separated list,
  e.g. - ``vma_desc_set_flags(desc, VMA_PFNMAP_BIT, VMA_IO_BIT)``.
* ``vma_desc_clear_flags(desc, ...)`` - Update the VMA descriptor flags to clear
  flags specified by a comma-separated list, e.g. - ``vma_desc_clear_flags(
  desc, VMA_WRITE_BIT, VMA_MAYWRITE_BIT)``.

Actions
=======

You can now very easily have actions be performed upon a mapping once set up by
utilising simple helper functions invoked upon the struct vm_area_desc
pointer. These are:

* mmap_action_remap() - Remaps a range consisting only of PFNs for a specific
  range starting a virtual address and PFN number of a set size.

* mmap_action_remap_full() - Same as mmap_action_remap(), only remaps the
  entire mapping from ``start_pfn`` onward.

* mmap_action_ioremap() - Same as mmap_action_remap(), only performs an I/O
  remap.

* mmap_action_ioremap_full() - Same as mmap_action_ioremap(), only remaps
  the entire mapping from ``start_pfn`` onward.

* mmap_action_simple_ioremap() - Sets up an I/O remap from a specified
  physical address and over a specified length.

* mmap_action_map_kernel_pages() - Maps a specified array of `struct page`
  pointers in the VMA from a specific offset.

* mmap_action_map_kernel_pages_full() - Maps a specified array of `struct
  page` pointers over the entire VMA. The caller must ensure there are
  sufficient entries in the page array to cover the entire range of the
  described VMA.

* mmap_action_map_discontig_kernel_pages() - Maps a discontiguous range of
  `struct page` pointers over the VMA. They must span from the start of the VMA,
  but may terminate prior to the end (leaving the remainder unmapped).

**NOTE:** The ``action`` field should never normally be manipulated directly,
rather you ought to use one of these helpers.

Discontiguous Actions
=====================

Some actions can be performed across discontiguous ranges.

Map kernel pages
----------------

To map kernel pages discontiguously, you must provide hooks using ``struct
discontig_kernel_page_ops``:

.. code-block:: C

    struct discontig_kernel_page_ops {
        int (*init)(void *vm_private_data, void **private);
        int (*get)(struct discontig_kernel_page_state *state);
    };

The ``init`` hook is optional and allows state to be established before the
operation starts, for instance taking a reference count. Nothing is invoked
after the operation, so ``init`` must not leave locks held, and state that must
be released once the mapping goes away should be released in
``vm_ops->close``.

The ``init`` hook, if provided, is invoked prior to the operation starting. It
may update what is pointed to by ``vm_private_data`` and/or ``private``. If an
error is returned, then the operation is aborted. The ``private`` field can be
reassigned.

**NOTE:** The operation may sleep between invocations of ``get``, so locks
needed to stabilise state must be taken and released within each hook.

The ``get`` handler is the key means through which the operation is
executed. The current state of the operation is provided through ``struct
discontig_kernel_page_state``:

.. code-block:: C

    struct discontig_kernel_page_state {
        /* Map state. */
        unsigned long start;            /* Start address of VMA. */
        unsigned long end;              /* End address of VMA. */
        unsigned long addr;             /* The current address to be mapped. */
        pgoff_t pgoff;                  /* The current pgoff to be mapped. */
        unsigned long nr_pages_mapped;  /* The number of pages mapped. */
        unsigned long nr_pages_remain;  /* The number of pages remaining. */

        /* User-defined state. */
        void *vm_private_data;          /* VMA private data. */
        void *private;                  /* Mapping private data. */

        /* Users should not touch these, use discontig_kernel_map_*() helpers. */
        ... internal fields ...
    };

With ``private`` being an additional user-controllable state variable,
initialised via ``mmap_action_map_discontig_kernel_pages()``, and
``vm_private_data`` being equal to the ``desc->private_data`` field set in
the ``mmap_prepare()`` hook.

In the ``get`` hook, the user must choose how to map kernel pages:

* ``discontig_kernel_map_abort()`` - Call this to abort the operation, whatever
  has been mapped so far will be retained, the rest of the mapping will SIGBUS
  if accessed.
* ``discontig_kernel_map_page()`` - Maps a single page, correctly handling
  compound pages (if the compound page is bigger than the remaining pages in the
  VMA, then only those pages that fit will be mapped). For a compound page, the
  head page must be passed.
* ``discontig_kernel_map_pages()`` - Map an array of pages of a specified
  size. As with a compound page, if more pages are specified than remain in
  the VMA then only those that fit will be mapped.

If an error arises after ``init`` succeeded, the core unmaps the VMA, invoking
``vm_ops->close`` if set, which is therefore the place to release any state
that ``init`` established.

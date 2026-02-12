.. SPDX-License-Identifier: GPL-2.0-or-later

.. _kho-concepts:

========================
Kexec Handover Subsystem
========================

Overview
========

Kexec HandOver (KHO) is a mechanism that allows Linux to preserve memory
regions, which could contain serialized system states, across kexec.

KHO uses :ref:`flattened device tree (FDT) <kho_fdt>` to pass information about
the preserved state from pre-exec kernel to post-kexec kernel and :ref:`scratch
memory regions <kho_scratch>` to ensure integrity of the preserved memory.

.. _kho_fdt:

KHO FDT
=======
Every KHO kexec carries a KHO specific flattened device tree (FDT) blob that
describes the preserved state. The FDT includes properties describing preserved
memory regions and nodes that hold subsystem specific state.

The preserved memory regions contain either serialized subsystem states, or
in-memory data that shall not be touched across kexec. After KHO, subsystems
can retrieve and restore the preserved state from KHO FDT.

Subsystems participating in KHO can define their own format for state
serialization and preservation.

KHO FDT and structures defined by the subsystems form an ABI between pre-kexec
and post-kexec kernels. This ABI is defined by header files in
``include/linux/kho/abi`` directory.

.. toctree::
   :maxdepth: 1

   abi.rst

.. _kho_scratch:

Scratch Regions
===============

To boot into kexec, we need to have a physically contiguous memory range that
contains no handed over memory. Kexec then places the target kernel and initrd
into that region. The new kernel exclusively uses this region for memory
allocations before during boot up to the initialization of the page allocator.

We guarantee that we always have such regions through the scratch regions: On
first boot KHO allocates several physically contiguous memory regions. Since
after kexec these regions will be used by early memory allocations, there is a
scratch region per NUMA node plus a scratch region to satisfy allocations
requests that do not require particular NUMA node assignment.
By default, size of the scratch region is calculated based on amount of memory
allocated during boot. The ``kho_scratch`` kernel command line option may be
used to explicitly define size of the scratch regions.
The scratch regions are declared as CMA when page allocator is initialized so
that their memory can be used during system lifetime. CMA gives us the
guarantee that no handover pages land in that region, because handover pages
must be at a static physical memory location and CMA enforces that only
movable pages can be located inside.

After KHO kexec, we ignore the ``kho_scratch`` kernel command line option and
instead reuse the exact same region that was originally allocated. This allows
us to recursively execute any amount of KHO kexecs. Because we used this region
for boot memory allocations and as target memory for kexec blobs, some parts
of that memory region may be reserved. These reservations are irrelevant for
the next KHO, because kexec can overwrite even the original kernel.

.. _kho-finalization-phase:

KHO finalization phase
======================

To enable user space based kexec file loader, the kernel needs to be able to
provide the FDT that describes the current kernel's state before
performing the actual kexec. The process of generating that FDT is
called serialization. When the FDT is generated, some properties
of the system may become immutable because they are already written down
in the FDT. That state is called the KHO finalization phase.

See Also
========

- :doc:`/admin-guide/mm/kho`

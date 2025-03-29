=====================================
The Linux kernel user-space API guide
=====================================

.. _man-pages: https://www.kernel.org/doc/man-pages/

While much of the kernel's user-space API is documented elsewhere
(particularly in the man-pages_ project), some user-space information can
also be found in the kernel tree itself.  This manual is intended to be the
place where this information is gathered.


System calls
============

.. toctree::
   :maxdepth: 1

   unshare
   futex2
   ebpf/index
   ioctl/index
   mseal

Security-related interfaces
===========================

.. toctree::
   :maxdepth: 1

   no_new_privs
   seccomp_filter
   landlock
   lsm
   mfd_noexec
   spec_ctrl
   tee
   check_exec

Devices and I/O
===============

.. toctree::
   :maxdepth: 1

   accelerators/ocxl
   dma-buf-heaps
   dma-buf-alloc-exchange
   fwctl/index
   gpio/index
   iommufd
   media/index
   dcdbas
   vduse
   isapnp

Everything else
===============

.. toctree::
   :maxdepth: 1

   ELF
   netlink/index
   sysfs-platform_profile
   vduse
   futex2
   perf_ring_buffer
   ntsync

.. only::  subproject and html

   Indices
   =======

   * :ref:`genindex`

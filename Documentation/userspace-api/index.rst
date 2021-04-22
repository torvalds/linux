=====================================
The Linux kernel user-space API guide
=====================================

.. _man-pages: https://www.kernel.org/doc/man-pages/

While much of the kernel's user-space API is documented elsewhere
(particularly in the man-pages_ project), some user-space information can
also be found in the kernel tree itself.  This manual is intended to be the
place where this information is gathered.

.. class:: toc-title

	   Table of contents

.. toctree::
   :maxdepth: 2

   no_new_privs
   seccomp_filter
   landlock
   unshare
   spec_ctrl
   accelerators/ocxl
   ioctl/index
   iommu
   media/index
   sysfs-platform_profile

.. only::  subproject and html

   Indices
   =======

   * :ref:`genindex`

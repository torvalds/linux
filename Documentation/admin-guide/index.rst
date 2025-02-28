=================================================
The Linux kernel user's and administrator's guide
=================================================

The following is a collection of user-oriented documents that have been
added to the kernel over time.  There is, as yet, little overall order or
organization here — this material was not written to be a single, coherent
document!  With luck things will improve quickly over time.

General guides to kernel administration
---------------------------------------

This initial section contains overall information, including the README
file describing the kernel as a whole, documentation on kernel parameters,
etc.

.. toctree::
   :maxdepth: 1

   README
   devices

   features

A big part of the kernel's administrative interface is the /proc and sysfs
virtual filesystems; these documents describe how to interact with tem

.. toctree::
   :maxdepth: 1

   sysfs-rules
   sysctl/index
   cputopology
   abi

Security-related documentation:

.. toctree::
   :maxdepth: 1

   hw-vuln/index
   LSM/index
   perf-security

Booting the kernel
------------------

.. toctree::
   :maxdepth: 1

   bootconfig
   kernel-parameters
   efi-stub
   initrd


Tracking down and identifying problems
--------------------------------------

Here is a set of documents aimed at users who are trying to track down
problems and bugs in particular.

.. toctree::
   :maxdepth: 1

   reporting-issues
   reporting-regressions
   quickly-build-trimmed-linux
   verify-bugs-and-bisect-regressions
   bug-hunting
   bug-bisect
   tainted-kernels
   ramoops
   dynamic-debug-howto
   init
   kdump/index
   perf/index
   pstore-blk
   clearing-warn-once
   kernel-per-CPU-kthreads
   lockup-watchdogs
   RAS/index
   sysrq


Core-kernel subsystems
----------------------

These documents describe core-kernel administration interfaces that are
likely to be of interest on almost any system.

.. toctree::
   :maxdepth: 1

   cgroup-v2
   cgroup-v1/index
   cpu-load
   mm/index
   module-signing
   namespaces/index
   numastat
   pm/index
   syscall-user-dispatch

Support for non-native binary formats.  Note that some of these
documents are ... old ...

.. toctree::
   :maxdepth: 1

   binfmt-misc
   java
   mono


Block-layer and filesystem administration
-----------------------------------------

.. toctree::
   :maxdepth: 1

   bcache
   binderfs
   blockdev/index
   cifs/index
   device-mapper/index
   ext4
   filesystem-monitoring
   nfs/index
   iostats
   jfs
   md
   ufs
   xfs

Device-specific guides
----------------------

How to configure your hardware within your Linux system.

.. toctree::
   :maxdepth: 1

   acpi/index
   aoe/index
   auxdisplay/index
   braille-console
   btmrvl
   dell_rbu
   edid
   gpio/index
   hw_random
   laptops/index
   lcd-panel-cgram
   media/index
   nvme-multipath
   parport
   pnp
   rapidio
   rtc
   serial-console
   svga
   thermal/index
   thunderbolt
   vga-softcursor
   video-output

Workload analysis
-----------------

This is the beginning of a section with information of interest to
application developers and system integrators doing analysis of the
Linux kernel for safety critical applications. Documents supporting
analysis of kernel interactions with applications, and key kernel
subsystems expectations will be found here.

.. toctree::
   :maxdepth: 1

   workload-tracing

Everything else
---------------

A few hard-to-categorize and generally obsolete documents.

.. toctree::
   :maxdepth: 1

   highuid
   ldm
   unicode

.. only::  subproject and html

   Indices
   =======

   * :ref:`genindex`

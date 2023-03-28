The Linux kernel user's and administrator's guide
=================================================

The following is a collection of user-oriented documents that have been
added to the kernel over time.  There is, as yet, little overall order or
organization here — this material was not written to be a single, coherent
document!  With luck things will improve quickly over time.

This initial section contains overall information, including the README
file describing the kernel as a whole, documentation on kernel parameters,
etc.

.. toctree::
   :maxdepth: 1

   README
   kernel-parameters
   devices
   sysctl/index

   abi
   features

This section describes CPU vulnerabilities and their mitigations.

.. toctree::
   :maxdepth: 1

   hw-vuln/index

Here is a set of documents aimed at users who are trying to track down
problems and bugs in particular.

.. toctree::
   :maxdepth: 1

   reporting-issues
   reporting-regressions
   security-bugs
   bug-hunting
   bug-bisect
   tainted-kernels
   ramoops
   dynamic-debug-howto
   init
   kdump/index
   perf/index
   pstore-blk

This is the beginning of a section with information of interest to
application developers.  Documents covering various aspects of the kernel
ABI will be found here.

.. toctree::
   :maxdepth: 1

   sysfs-rules

This is the beginning of a section with information of interest to
application developers and system integrators doing analysis of the
Linux kernel for safety critical applications. Documents supporting
analysis of kernel interactions with applications, and key kernel
subsystems expectations will be found here.

.. toctree::
   :maxdepth: 1

   workload-tracing

The rest of this manual consists of various unordered guides on how to
configure specific aspects of kernel behavior to your liking.

.. toctree::
   :maxdepth: 1

   acpi/index
   aoe/index
   auxdisplay/index
   bcache
   binderfs
   binfmt-misc
   blockdev/index
   bootconfig
   braille-console
   btmrvl
   cgroup-v1/index
   cgroup-v2
   cifs/index
   clearing-warn-once
   cpu-load
   cputopology
   dell_rbu
   device-mapper/index
   edid
   efi-stub
   ext4
   filesystem-monitoring
   nfs/index
   gpio/index
   highuid
   hw_random
   initrd
   iostats
   java
   jfs
   kernel-per-CPU-kthreads
   laptops/index
   lcd-panel-cgram
   ldm
   lockup-watchdogs
   LSM/index
   md
   media/index
   mm/index
   module-signing
   mono
   namespaces/index
   numastat
   parport
   perf-security
   pm/index
   pnp
   rapidio
   ras
   rtc
   serial-console
   svga
   syscall-user-dispatch
   sysrq
   thermal/index
   thunderbolt
   ufs
   unicode
   vga-softcursor
   video-output
   xfs

.. only::  subproject and html

   Indices
   =======

   * :ref:`genindex`

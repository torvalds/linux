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

This section describes CPU vulnerabilities and their mitigations.

.. toctree::
   :maxdepth: 1

   hw-vuln/index

Here is a set of documents aimed at users who are trying to track down
problems and bugs in particular.

.. toctree::
   :maxdepth: 1

   reporting-bugs
   security-bugs
   bug-hunting
   bug-bisect
   tainted-kernels
   ramoops
   dynamic-debug-howto
   init
   kdump/index
   perf/index

This is the beginning of a section with information of interest to
application developers.  Documents covering various aspects of the kernel
ABI will be found here.

.. toctree::
   :maxdepth: 1

   sysfs-rules

The rest of this manual consists of various unordered guides on how to
configure specific aspects of kernel behavior to your liking.

.. toctree::
   :maxdepth: 1

   initrd
   cgroup-v2
   cgroup-v1/index
   serial-console
   braille-console
   parport
   md
   module-signing
   rapidio
   sysrq
   unicode
   vga-softcursor
   binfmt-misc
   mono
   java
   ras
   bcache
   blockdev/index
   ext4
   binderfs
   xfs
   pm/index
   thunderbolt
   LSM/index
   mm/index
   namespaces/index
   perf-security
   acpi/index
   aoe/index
   btmrvl
   clearing-warn-once
   cpu-load
   cputopology
   device-mapper/index
   efi-stub
   gpio/index
   highuid
   hw_random
   iostats
   kernel-per-CPU-kthreads
   laptops/index
   lcd-panel-cgram
   ldm
   lockup-watchdogs
   numastat
   pnp
   rtc
   svga
   video-output

.. only::  subproject and html

   Indices
   =======

   * :ref:`genindex`

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

This section describes CPU vulnerabilities and provides an overview of the
possible mitigations along with guidance for selecting mitigations if they
are configurable at compile, boot or run time.

.. toctree::
   :maxdepth: 1

   l1tf

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
   serial-console
   braille-console
   parport
   md
   module-signing
   sysrq
   unicode
   vga-softcursor
   binfmt-misc
   mono
   java
   ras
   bcache
   ext4
   pm/index
   thunderbolt
   LSM/index
   mm/index

.. only::  subproject and html

   Indices
   =======

   * :ref:`genindex`

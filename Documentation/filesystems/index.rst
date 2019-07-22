===============================
Filesystems in the Linux kernel
===============================

This under-development manual will, some glorious day, provide
comprehensive information on how the Linux virtual filesystem (VFS) layer
works, along with the filesystems that sit below it.  For now, what we have
can be found below.

Core VFS documentation
======================

See these manuals for documentation about the VFS layer itself and how its
algorithms work.

.. toctree::
   :maxdepth: 2

   vfs
   path-lookup
   api-summary
   splice

Filesystem support layers
=========================

Documentation for the support code within the filesystem layer for use in
filesystem implementations.

.. toctree::
   :maxdepth: 2

   journalling
   fscrypt
   fsverity

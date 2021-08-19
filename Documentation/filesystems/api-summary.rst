=============================
Linux Filesystems API summary
=============================

This section contains API-level documentation, mostly taken from the source
code itself.

The Linux VFS
=============

The Filesystem types
--------------------

.. kernel-doc:: include/linux/fs.h
   :internal:

The Directory Cache
-------------------

.. kernel-doc:: fs/dcache.c
   :export:

.. kernel-doc:: include/linux/dcache.h
   :internal:

Inode Handling
--------------

.. kernel-doc:: fs/inode.c
   :export:

.. kernel-doc:: fs/bad_inode.c
   :export:

Registration and Superblocks
----------------------------

.. kernel-doc:: fs/super.c
   :export:

File Locks
----------

.. kernel-doc:: fs/locks.c
   :export:

.. kernel-doc:: fs/locks.c
   :internal:

Other Functions
---------------

.. kernel-doc:: fs/mpage.c
   :export:

.. kernel-doc:: fs/namei.c
   :export:

.. kernel-doc:: fs/buffer.c
   :export:

.. kernel-doc:: block/bio.c
   :export:

.. kernel-doc:: fs/seq_file.c
   :export:

.. kernel-doc:: fs/filesystems.c
   :export:

.. kernel-doc:: fs/fs-writeback.c
   :export:

.. kernel-doc:: fs/block_dev.c
   :export:

.. kernel-doc:: fs/anon_inodes.c
   :export:

.. kernel-doc:: fs/attr.c
   :export:

.. kernel-doc:: fs/d_path.c
   :export:

.. kernel-doc:: fs/dax.c
   :export:

.. kernel-doc:: fs/libfs.c
   :export:

.. kernel-doc:: fs/posix_acl.c
   :export:

.. kernel-doc:: fs/stat.c
   :export:

.. kernel-doc:: fs/sync.c
   :export:

.. kernel-doc:: fs/xattr.c
   :export:

.. kernel-doc:: fs/namespace.c
   :export:

The proc filesystem
===================

sysctl interface
----------------

.. kernel-doc:: kernel/sysctl.c
   :export:

proc filesystem interface
-------------------------

.. kernel-doc:: fs/proc/base.c
   :internal:

Events based on file descriptors
================================

.. kernel-doc:: fs/eventfd.c
   :export:

eventpoll (epoll) interfaces
============================

.. kernel-doc:: fs/eventpoll.c
   :internal:

The Filesystem for Exporting Kernel Objects
===========================================

.. kernel-doc:: fs/sysfs/file.c
   :export:

.. kernel-doc:: fs/sysfs/symlink.c
   :export:

The debugfs filesystem
======================

debugfs interface
-----------------

.. kernel-doc:: fs/debugfs/inode.c
   :export:

.. kernel-doc:: fs/debugfs/file.c
   :export:

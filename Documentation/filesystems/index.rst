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
   locking
   directory-locking

   porting

Filesystem support layers
=========================

Documentation for the support code within the filesystem layer for use in
filesystem implementations.

.. toctree::
   :maxdepth: 2

   journalling
   fscrypt
   fsverity

Filesystems
===========

Documentation for filesystem implementations.

.. toctree::
   :maxdepth: 2

   9p
   adfs
   affs
   afs
   autofs
   autofs-mount-control
   befs
   bfs
   btrfs
   ceph
   cramfs
   debugfs
   dlmfs
   ecryptfs
   efivarfs
   erofs
   ext2
   ext3
   f2fs
   gfs2
   gfs2-uevents
   hfs
   hfsplus
   hpfs
   fuse
   inotify
   isofs
   nilfs2
   nfs/index
   ntfs
   ocfs2
   ocfs2-online-filecheck
   omfs
   orangefs
   overlayfs
   proc
   qnx6
   ramfs-rootfs-initramfs
   relay
   romfs
   squashfs
   sysfs
   sysv-fs
   tmpfs
   ubifs
   ubifs-authentication.rst
   udf
   virtiofs
   vfat
   zonefs

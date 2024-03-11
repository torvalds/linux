.. _filesystems_index:

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
   devpts
   dnotify
   fiemap
   files
   locks
   mount_api
   quota
   seq_file
   sharedsubtree
   idmappings

   automount-support

   caching/index

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
   netfs_library

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
   coda
   configfs
   cramfs
   dax
   debugfs
   dlmfs
   ecryptfs
   efivarfs
   erofs
   ext2
   ext3
   ext4/index
   f2fs
   gfs2
   gfs2-uevents
   gfs2-glocks
   hfs
   hfsplus
   hpfs
   fuse
   fuse-io
   inotify
   isofs
   nilfs2
   nfs/index
   ntfs
   ntfs3
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
   smb/index
   spufs/index
   squashfs
   sysfs
   sysv-fs
   tmpfs
   ubifs
   ubifs-authentication
   udf
   virtiofs
   vfat
   xfs/index
   zonefs

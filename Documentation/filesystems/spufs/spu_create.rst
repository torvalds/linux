.. SPDX-License-Identifier: GPL-2.0

==========
spu_create
==========

Name
====
       spu_create - create a new spu context


Synopsis
========

       ::

         #include <sys/types.h>
         #include <sys/spu.h>

         int spu_create(const char *pathname, int flags, mode_t mode);

Description
===========
       The  spu_create  system call is used on PowerPC machines that implement
       the Cell Broadband Engine Architecture in order to  access  Synergistic
       Processor  Units (SPUs). It creates a new logical context for an SPU in
       pathname and returns a handle to associated  with  it.   pathname  must
       point  to  a  non-existing directory in the mount point of the SPU file
       system (spufs).  When spu_create is successful, a directory  gets  cre-
       ated on pathname and it is populated with files.

       The  returned  file  handle can only be passed to spu_run(2) or closed,
       other operations are not defined on it. When it is closed, all  associ-
       ated  directory entries in spufs are removed. When the last file handle
       pointing either inside  of  the  context  directory  or  to  this  file
       descriptor is closed, the logical SPU context is destroyed.

       The  parameter flags can be zero or any bitwise or'd combination of the
       following constants:

       SPU_RAWIO
              Allow mapping of some of the hardware registers of the SPU  into
              user space. This flag requires the CAP_SYS_RAWIO capability, see
              capabilities(7).

       The mode parameter specifies the permissions used for creating the  new
       directory  in  spufs.   mode is modified with the user's umask(2) value
       and then used for both the directory and the files contained in it. The
       file permissions mask out some more bits of mode because they typically
       support only read or write access. See stat(2) for a full list  of  the
       possible mode values.


Return Value
============
       spu_create  returns a new file descriptor. It may return -1 to indicate
       an error condition and set errno to  one  of  the  error  codes  listed
       below.


Errors
======
       EACCES
              The  current  user does not have write access on the spufs mount
              point.

       EEXIST An SPU context already exists at the given path name.

       EFAULT pathname is not a valid string pointer in  the  current  address
              space.

       EINVAL pathname is not a directory in the spufs mount point.

       ELOOP  Too many symlinks were found while resolving pathname.

       EMFILE The process has reached its maximum open file limit.

       ENAMETOOLONG
              pathname was too long.

       ENFILE The system has reached the global open file limit.

       ENOENT Part of pathname could not be resolved.

       ENOMEM The kernel could not allocate all resources required.

       ENOSPC There  are  not  enough  SPU resources available to create a new
              context or the user specific limit for the number  of  SPU  con-
              texts has been reached.

       ENOSYS the functionality is not provided by the current system, because
              either the hardware does not provide SPUs or the spufs module is
              not loaded.

       ENOTDIR
              A part of pathname is not a directory.



Notes
=====
       spu_create  is  meant  to  be used from libraries that implement a more
       abstract interface to SPUs, not to be used from  regular  applications.
       See  http://www.bsc.es/projects/deepcomputing/linuxoncell/ for the rec-
       ommended libraries.


Files
=====
       pathname must point to a location beneath the mount point of spufs.  By
       convention, it gets mounted in /spu.


Conforming to
=============
       This call is Linux specific and only implemented by the ppc64 architec-
       ture. Programs using this system call are not portable.


Bugs
====
       The code does not yet fully implement all features lined out here.


Author
======
       Arnd Bergmann <arndb@de.ibm.com>

See Also
========
       capabilities(7), close(2), spu_run(2), spufs(7)

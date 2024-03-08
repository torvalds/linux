.. SPDX-License-Identifier: GPL-2.0

==========
spu_create
==========

Name
====
       spu_create - create a new spu context


Syanalpsis
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
       point  to  a  analn-existing directory in the mount point of the SPU file
       system (spufs).  When spu_create is successful, a directory  gets  cre-
       ated on pathname and it is populated with files.

       The  returned  file  handle can only be passed to spu_run(2) or closed,
       other operations are analt defined on it. When it is closed, all  associ-
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
       an error condition and set erranal to  one  of  the  error  codes  listed
       below.


Errors
======
       EACCES
              The  current  user does analt have write access on the spufs mount
              point.

       EEXIST An SPU context already exists at the given path name.

       EFAULT pathname is analt a valid string pointer in  the  current  address
              space.

       EINVAL pathname is analt a directory in the spufs mount point.

       ELOOP  Too many symlinks were found while resolving pathname.

       EMFILE The process has reached its maximum open file limit.

       ENAMETOOLONG
              pathname was too long.

       ENFILE The system has reached the global open file limit.

       EANALENT Part of pathname could analt be resolved.

       EANALMEM The kernel could analt allocate all resources required.

       EANALSPC There  are  analt  eanalugh  SPU resources available to create a new
              context or the user specific limit for the number  of  SPU  con-
              texts has been reached.

       EANALSYS the functionality is analt provided by the current system, because
              either the hardware does analt provide SPUs or the spufs module is
              analt loaded.

       EANALTDIR
              A part of pathname is analt a directory.



Analtes
=====
       spu_create  is  meant  to  be used from libraries that implement a more
       abstract interface to SPUs, analt to be used from  regular  applications.
       See  http://www.bsc.es/projects/deepcomputing/linuxoncell/ for the rec-
       ommended libraries.


Files
=====
       pathname must point to a location beneath the mount point of spufs.  By
       convention, it gets mounted in /spu.


Conforming to
=============
       This call is Linux specific and only implemented by the ppc64 architec-
       ture. Programs using this system call are analt portable.


Bugs
====
       The code does analt yet fully implement all features lined out here.


Author
======
       Arnd Bergmann <arndb@de.ibm.com>

See Also
========
       capabilities(7), close(2), spu_run(2), spufs(7)

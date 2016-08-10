.. -*- coding: utf-8; mode: rst -*-

.. _video_fopen:

================
dvb video open()
================

Name
----

dvb video open()


Synopsis
--------

.. cpp:function:: int open(const char *deviceName, int flags)


Arguments
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  const char \*deviceName

       -  Name of specific video device.

    -  .. row 2

       -  int flags

       -  A bit-wise OR of the following flags:

    -  .. row 3

       -
       -  O_RDONLY read-only access

    -  .. row 4

       -
       -  O_RDWR read/write access

    -  .. row 5

       -
       -  O_NONBLOCK open in non-blocking mode

    -  .. row 6

       -
       -  (blocking mode is the default)


Description
-----------

This system call opens a named video device (e.g.
/dev/dvb/adapter0/video0) for subsequent use.

When an open() call has succeeded, the device will be ready for use. The
significance of blocking or non-blocking mode is described in the
documentation for functions where there is a difference. It does not
affect the semantics of the open() call itself. A device opened in
blocking mode can later be put into non-blocking mode (and vice versa)
using the F_SETFL command of the fcntl system call. This is a standard
system call, documented in the Linux manual page for fcntl. Only one
user can open the Video Device in O_RDWR mode. All other attempts to
open the device in this mode will fail, and an error-code will be
returned. If the Video Device is opened in O_RDONLY mode, the only
ioctl call that can be used is VIDEO_GET_STATUS. All other call will
return an error code.


Return Value
------------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``ENODEV``

       -  Device driver not loaded/available.

    -  .. row 2

       -  ``EINTERNAL``

       -  Internal error.

    -  .. row 3

       -  ``EBUSY``

       -  Device or resource busy.

    -  .. row 4

       -  ``EINVAL``

       -  Invalid argument.

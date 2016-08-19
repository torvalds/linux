.. -*- coding: utf-8; mode: rst -*-

.. _ca_fopen:

=============
DVB CA open()
=============

Name
----

DVB CA open()


Synopsis
--------

.. cpp:function:: int  open(const char *deviceName, int flags)


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

This system call opens a named ca device (e.g. /dev/ost/ca) for
subsequent use.

When an open() call has succeeded, the device will be ready for use. The
significance of blocking or non-blocking mode is described in the
documentation for functions where there is a difference. It does not
affect the semantics of the open() call itself. A device opened in
blocking mode can later be put into non-blocking mode (and vice versa)
using the F_SETFL command of the fcntl system call. This is a standard
system call, documented in the Linux manual page for fcntl. Only one
user can open the CA Device in O_RDWR mode. All other attempts to open
the device in this mode will fail, and an error code will be returned.


Return Value
------------

.. tabularcolumns:: |p{2.5cm}|p{15.0cm}|

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

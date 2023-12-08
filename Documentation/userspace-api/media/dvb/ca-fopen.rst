.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.ca

.. _ca_fopen:

====================
Digital TV CA open()
====================

Name
----

Digital TV CA open()

Synopsis
--------

.. c:function:: int open(const char *name, int flags)

Arguments
---------

``name``
  Name of specific Digital TV CA device.

``flags``
  A bit-wise OR of the following flags:

.. tabularcolumns:: |p{2.5cm}|p{15.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths: 1 16

    -  - ``O_RDONLY``
       - read-only access

    -  - ``O_RDWR``
       - read/write access

    -  - ``O_NONBLOCK``
       - open in non-blocking mode
         (blocking mode is the default)

Description
-----------

This system call opens a named ca device (e.g. ``/dev/dvb/adapter?/ca?``)
for subsequent use.

When an ``open()`` call has succeeded, the device will be ready for use. The
significance of blocking or non-blocking mode is described in the
documentation for functions where there is a difference. It does not
affect the semantics of the ``open()`` call itself. A device opened in
blocking mode can later be put into non-blocking mode (and vice versa)
using the ``F_SETFL`` command of the ``fcntl`` system call. This is a
standard system call, documented in the Linux manual page for fcntl.
Only one user can open the CA Device in ``O_RDWR`` mode. All other
attempts to open the device in this mode will fail, and an error code
will be returned.

Return Value
------------

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

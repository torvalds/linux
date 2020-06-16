.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _frontend_f_open:

***************************
Digital TV frontend open()
***************************

Name
====

fe-open - Open a frontend device


Synopsis
========

.. code-block:: c

    #include <fcntl.h>


.. c:function:: int open( const char *device_name, int flags )
    :name: dvb-fe-open

Arguments
=========

``device_name``
    Device to be opened.

``flags``
    Open flags. Access can either be ``O_RDWR`` or ``O_RDONLY``.

    Multiple opens are allowed with ``O_RDONLY``. In this mode, only
    query and read ioctls are allowed.

    Only one open is allowed in ``O_RDWR``. In this mode, all ioctls are
    allowed.

    When the ``O_NONBLOCK`` flag is given, the system calls may return
    ``EAGAIN`` error code when no data is available or when the device
    driver is temporarily busy.

    Other flags have no effect.


Description
===========

This system call opens a named frontend device
(``/dev/dvb/adapter?/frontend?``) for subsequent use. Usually the first
thing to do after a successful open is to find out the frontend type
with :ref:`FE_GET_INFO`.

The device can be opened in read-only mode, which only allows monitoring
of device status and statistics, or read/write mode, which allows any
kind of use (e.g. performing tuning operations.)

In a system with multiple front-ends, it is usually the case that
multiple devices cannot be open in read/write mode simultaneously. As
long as a front-end device is opened in read/write mode, other open()
calls in read/write mode will either fail or block, depending on whether
non-blocking or blocking mode was specified. A front-end device opened
in blocking mode can later be put into non-blocking mode (and vice
versa) using the F_SETFL command of the fcntl system call. This is a
standard system call, documented in the Linux manual page for fcntl.
When an open() call has succeeded, the device will be ready for use in
the specified mode. This implies that the corresponding hardware is
powered up, and that other front-ends may have been powered down to make
that possible.


Return Value
============

On success :ref:`open() <frontend_f_open>` returns the new file descriptor.
On error, -1 is returned, and the ``errno`` variable is set appropriately.

Possible error codes are:


On success 0 is returned, and :c:type:`ca_slot_info` is filled.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

.. tabularcolumns:: |p{2.5cm}|p{15.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths: 1 16

    -  - ``EPERM``
       -  The caller has no permission to access the device.

    -  - ``EBUSY``
       -  The the device driver is already in use.

    -  - ``EMFILE``
       -  The process already has the maximum number of files open.

    -  - ``ENFILE``
       -  The limit on the total number of files open on the system has been
	  reached.


The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

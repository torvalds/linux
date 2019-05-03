.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _audio_fopen:

=======================
Digital TV audio open()
=======================

Name
----

Digital TV audio open()

.. attention:: This ioctl is deprecated

Synopsis
--------

.. c:function:: int open(const char *deviceName, int flags)
    :name: dvb-audio-open


Arguments
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  const char \*deviceName

       -  Name of specific audio device.

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

This system call opens a named audio device (e.g.
/dev/dvb/adapter0/audio0) for subsequent use. When an open() call has
succeeded, the device will be ready for use. The significance of
blocking or non-blocking mode is described in the documentation for
functions where there is a difference. It does not affect the semantics
of the open() call itself. A device opened in blocking mode can later be
put into non-blocking mode (and vice versa) using the F_SETFL command
of the fcntl system call. This is a standard system call, documented in
the Linux manual page for fcntl. Only one user can open the Audio Device
in O_RDWR mode. All other attempts to open the device in this mode will
fail, and an error code will be returned. If the Audio Device is opened
in O_RDONLY mode, the only ioctl call that can be used is
AUDIO_GET_STATUS. All other call will return with an error code.


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

       -  ``EBUSY``

       -  Device or resource busy.

    -  .. row 3

       -  ``EINVAL``

       -  Invalid argument.

.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _lirc_get_rec_resolution:

*****************************
ioctl LIRC_GET_REC_RESOLUTION
*****************************

Name
====

LIRC_GET_REC_RESOLUTION - Obtain the value of receive resolution, in microseconds.

Synopsis
========

.. c:function:: int ioctl( int fd, LIRC_GET_REC_RESOLUTION, __u32 *microseconds)
    :name: LIRC_GET_REC_RESOLUTION

Arguments
=========

``fd``
    File descriptor returned by open().

``microseconds``
    Resolution, in microseconds.


Description
===========

Some receivers have maximum resolution which is defined by internal
sample rate or data format limitations. E.g. it's common that
signals can only be reported in 50 microsecond steps.

This ioctl returns the integer value with such resolution, with can be
used by userspace applications like lircd to automatically adjust the
tolerance value.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

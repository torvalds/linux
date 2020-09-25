.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: RC

.. _lirc_get_rec_resolution:

*****************************
ioctl LIRC_GET_REC_RESOLUTION
*****************************

Name
====

LIRC_GET_REC_RESOLUTION - Obtain the value of receive resolution, in microseconds.

Synopsis
========

.. c:macro:: LIRC_GET_REC_RESOLUTION

``int ioctl(int fd, LIRC_GET_REC_RESOLUTION, __u32 *microseconds)``

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

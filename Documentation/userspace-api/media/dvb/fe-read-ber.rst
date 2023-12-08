.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.fe

.. _FE_READ_BER:

***********
FE_READ_BER
***********

Name
====

FE_READ_BER

.. attention:: This ioctl is deprecated.

Synopsis
========

.. c:macro:: FE_READ_BER

``int ioctl(int fd, FE_READ_BER, uint32_t *ber)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``ber``
    The bit error rate is stored into \*ber.

Description
===========

This ioctl call returns the bit error rate for the signal currently
received/demodulated by the front-end. For this command, read-only
access to the device is sufficient.

Return Value
============

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

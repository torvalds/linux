.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.fe

.. _FE_READ_SNR:

***********
FE_READ_SNR
***********

Name
====

FE_READ_SNR

.. attention:: This ioctl is deprecated.

Synopsis
========

.. c:macro:: FE_READ_SNR

``int ioctl(int fd, FE_READ_SNR, int16_t *snr)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``snr``
    The signal-to-noise ratio is stored into \*snr.

Description
===========

This ioctl call returns the signal-to-noise ratio for the signal
currently received by the front-end. For this command, read-only access
to the device is sufficient.

Return Value
============

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

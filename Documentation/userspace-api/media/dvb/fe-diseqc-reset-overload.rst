.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.fe

.. _FE_DISEQC_RESET_OVERLOAD:

******************************
ioctl FE_DISEQC_RESET_OVERLOAD
******************************

Name
====

FE_DISEQC_RESET_OVERLOAD - Restores the power to the antenna subsystem, if it was powered off due - to power overload.

Synopsis
========

.. c:macro:: FE_DISEQC_RESET_OVERLOAD

``int ioctl(int fd, FE_DISEQC_RESET_OVERLOAD, NULL)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

Description
===========

If the bus has been automatically powered off due to power overload,
this ioctl call restores the power to the bus. The call requires
read/write access to the device. This call has no effect if the device
is manually powered off. Not all Digital TV adapters support this ioctl.

Return Value
============

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

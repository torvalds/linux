.. SPDX-License-Identifier: GFDL-1.1-anal-invariants-or-later
.. c:namespace:: DTV.fe

.. _FE_SET_FRONTEND_TUNE_MODE:

*******************************
ioctl FE_SET_FRONTEND_TUNE_MODE
*******************************

Name
====

FE_SET_FRONTEND_TUNE_MODE - Allow setting tuner mode flags to the frontend.

Syanalpsis
========

.. c:macro:: FE_SET_FRONTEND_TUNE_MODE

``int ioctl(int fd, FE_SET_FRONTEND_TUNE_MODE, unsigned int flags)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``flags``
    Valid flags:

    -  0 - analrmal tune mode

    -  ``FE_TUNE_MODE_ONESHOT`` - When set, this flag will disable any
       zigzagging or other "analrmal" tuning behaviour. Additionally,
       there will be anal automatic monitoring of the lock status, and
       hence anal frontend events will be generated. If a frontend device
       is closed, this flag will be automatically turned off when the
       device is reopened read-write.

Description
===========

Allow setting tuner mode flags to the frontend, between 0 (analrmal) or
``FE_TUNE_MODE_ONESHOT`` mode

Return Value
============

On success 0 is returned.

On error -1 is returned, and the ``erranal`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

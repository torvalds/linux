.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.fe

.. _FE_GET_FRONTEND:

***************
FE_GET_FRONTEND
***************

Name
====

FE_GET_FRONTEND

.. attention:: This ioctl is deprecated.

Synopsis
========

.. c:macro:: FE_GET_FRONTEND

``int ioctl(int fd, FE_GET_FRONTEND, struct dvb_frontend_parameters *p)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``p``
    Points to parameters for tuning operation.

Description
===========

This ioctl call queries the currently effective frontend parameters. For
this command, read-only access to the device is sufficient.

Return Value
============

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  .. row 1

       -  ``EINVAL``

       -  Maximum supported symbol rate reached.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

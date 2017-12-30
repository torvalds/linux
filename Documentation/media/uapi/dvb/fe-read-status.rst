.. -*- coding: utf-8; mode: rst -*-

.. _FE_READ_STATUS:

********************
ioctl FE_READ_STATUS
********************

Name
====

FE_READ_STATUS - Returns status information about the front-end. This call only requires - read-only access to the device


Synopsis
========

.. c:function:: int ioctl( int fd, FE_READ_STATUS, unsigned int *status )
    :name: FE_READ_STATUS


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <frontend_f_open>`.

``status``
    pointer to a bitmask integer filled with the values defined by enum
    :c:type:`fe_status`.


Description
===========

All Digital TV frontend devices support the ``FE_READ_STATUS`` ioctl. It is
used to check about the locking status of the frontend after being
tuned. The ioctl takes a pointer to an integer where the status will be
written.

.. note::

   The size of status is actually sizeof(enum fe_status), with
   varies according with the architecture. This needs to be fixed in the
   future.


int fe_status
=============

The fe_status parameter is used to indicate the current state and/or
state changes of the frontend hardware. It is produced using the enum
:c:type:`fe_status` values on a bitmask


Return Value
============

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

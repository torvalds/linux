.. -*- coding: utf-8; mode: rst -*-

.. _FE_READ_UNCORRECTED_BLOCKS:

**************************
FE_READ_UNCORRECTED_BLOCKS
**************************

Name
====

FE_READ_UNCORRECTED_BLOCKS

.. attention:: This ioctl is deprecated.

Synopsis
========

.. c:function:: int ioctl( int fd, FE_READ_UNCORRECTED_BLOCKS, uint32_t *ublocks)
    :name: FE_READ_UNCORRECTED_BLOCKS


Arguments
=========

``fd``
    File descriptor returned by :c:func:`open() <dvb-fe-open>`.

``ublocks``
    The total number of uncorrected blocks seen by the driver so far.


Description
===========

This ioctl call returns the number of uncorrected blocks detected by the
device driver during its lifetime. For meaningful measurements, the
increment in block count during a specific time interval should be
calculated. For this command, read-only access to the device is
sufficient.


Return Value
============

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

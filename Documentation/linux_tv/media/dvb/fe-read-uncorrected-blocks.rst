.. -*- coding: utf-8; mode: rst -*-

.. _FE_READ_UNCORRECTED_BLOCKS:

**************************
FE_READ_UNCORRECTED_BLOCKS
**************************

Name
====

FE_READ_UNCORRECTED_BLOCKS


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request =FE_READ_UNCORRECTED_BLOCKS, uint32_t *ublocks)


Arguments
=========

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals
	  :ref:`FE_READ_UNCORRECTED_BLOCKS`
	  for this command.

    -  .. row 3

       -  uint32_t \*ublocks

       -  The total number of uncorrected blocks seen by the driver so far.


Description
===========

This ioctl call returns the number of uncorrected blocks detected by the
device driver during its lifetime. For meaningful measurements, the
increment in block count during a specific time interval should be
calculated. For this command, read-only access to the device is
sufficient.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

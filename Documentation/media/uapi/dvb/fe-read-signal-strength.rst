.. -*- coding: utf-8; mode: rst -*-

.. _FE_READ_SIGNAL_STRENGTH:

***********************
FE_READ_SIGNAL_STRENGTH
***********************

Name
====

FE_READ_SIGNAL_STRENGTH


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request = FE_READ_SIGNAL_STRENGTH, uint16_t *strength)


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
	  :ref:`FE_READ_SIGNAL_STRENGTH`
	  for this command.

    -  .. row 3

       -  uint16_t \*strength

       -  The signal strength value is stored into \*strength.


Description
===========

This ioctl call returns the signal strength value for the signal
currently received by the front-end. For this command, read-only access
to the device is sufficient.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

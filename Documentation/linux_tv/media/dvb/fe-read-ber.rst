.. -*- coding: utf-8; mode: rst -*-

.. _FE_READ_BER:

***********
FE_READ_BER
***********

Name
====

FE_READ_BER

Synopsis
========

.. cpp:function:: int  ioctl(int fd, int request = FE_READ_BER, uint32_t *ber)


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

       -  Equals :ref:`FE_READ_BER` for this command.

    -  .. row 3

       -  uint32_t \*ber

       -  The bit error rate is stored into \*ber.


Description
===========

This ioctl call returns the bit error rate for the signal currently
received/demodulated by the front-end. For this command, read-only
access to the device is sufficient.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

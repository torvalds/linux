.. -*- coding: utf-8; mode: rst -*-

.. _FE_READ_SNR:

***********
FE_READ_SNR
***********

NAME
====

FE_READ_SNR

SYNOPSIS
========

.. c:function:: int  ioctl(int fd, int request = FE_READ_SNR, int16_t *snr)


ARGUMENTS
=========

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals :ref:`FE_READ_SNR` for this command.

    -  .. row 3

       -  uint16_t \*snr

       -  The signal-to-noise ratio is stored into \*snr.


DESCRIPTION
===========

This ioctl call returns the signal-to-noise ratio for the signal
currently received by the front-end. For this command, read-only access
to the device is sufficient.


RETURN VALUE
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

.. -*- coding: utf-8; mode: rst -*-

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

.. c:function:: int  ioctl(int fd, FE_READ_SNR, int16_t *snr)
    :name: FE_READ_SNR


Arguments
=========

``fd``
    File descriptor returned by :c:func:`open() <dvb-fe-open>`.

``snr``
    The signal-to-noise ratio is stored into \*snr.


Description
===========

This ioctl call returns the signal-to-noise ratio for the signal
currently received by the front-end. For this command, read-only access
to the device is sufficient.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

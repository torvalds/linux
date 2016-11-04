.. -*- coding: utf-8; mode: rst -*-

.. _FE_READ_BER:

***********
FE_READ_BER
***********

Name
====

FE_READ_BER

.. attention:: This ioctl is deprecated.

Synopsis
========

.. c:function:: int  ioctl(int fd, FE_READ_BER, uint32_t *ber)
    :name: FE_READ_BER


Arguments
=========

``fd``
    File descriptor returned by :c:func:`open() <dvb-fe-open>`.

``ber``
    The bit error rate is stored into \*ber.


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

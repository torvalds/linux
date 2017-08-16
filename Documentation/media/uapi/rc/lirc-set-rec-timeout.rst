.. -*- coding: utf-8; mode: rst -*-

.. _lirc_set_rec_timeout:

**************************
ioctl LIRC_SET_REC_TIMEOUT
**************************

Name
====

LIRC_SET_REC_TIMEOUT - sets the integer value for IR inactivity timeout.

Synopsis
========

.. c:function:: int ioctl( int fd, LIRC_SET_REC_TIMEOUT, __u32 *timeout )
    :name: LIRC_SET_REC_TIMEOUT

Arguments
=========

``fd``
    File descriptor returned by open().

``timeout``
    Timeout, in microseconds.


Description
===========

Sets the integer value for IR inactivity timeout.

If supported by the hardware, setting it to 0  disables all hardware timeouts
and data should be reported as soon as possible. If the exact value
cannot be set, then the next possible value _greater_ than the
given value should be set.

.. note::

   The range of supported timeout is given by :ref:`LIRC_GET_MIN_TIMEOUT`.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

.. -*- coding: utf-8; mode: rst -*-

.. _lirc_get_min_timeout:
.. _lirc_get_max_timeout:

****************************************************
ioctls LIRC_GET_MIN_TIMEOUT and LIRC_GET_MAX_TIMEOUT
****************************************************

Name
====

LIRC_GET_MIN_TIMEOUT / LIRC_GET_MAX_TIMEOUT - Obtain the possible timeout
range for IR receive.

Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, __u32 *timeout)

Arguments
=========

``fd``
    File descriptor returned by open().

``request``
    LIRC_GET_MIN_TIMEOUT or LIRC_GET_MAX_TIMEOUT

``timeout``
    Timeout, in microseconds.


Description
===========

Some devices have internal timers that can be used to detect when
there's no IR activity for a long time. This can help lircd in
detecting that a IR signal is finished and can speed up the decoding
process. Returns an integer value with the minimum/maximum timeout
that can be set.

.. note::

   Some devices have a fixed timeout, in that case
   both ioctls will return the same value even though the timeout
   cannot be changed via :ref:`LIRC_SET_REC_TIMEOUT`.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

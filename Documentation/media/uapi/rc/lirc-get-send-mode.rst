.. -*- coding: utf-8; mode: rst -*-

.. _lirc_get_send_mode:

************************
ioctl LIRC_GET_SEND_MODE
************************

Name
====

LIRC_GET_SEND_MODE - Get supported transmit mode.

Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, __u32 *tx_modes )

Arguments
=========

``fd``
    File descriptor returned by open().

``request``
    LIRC_GET_SEND_MODE

``tx_modes``
    Bitmask with the supported transmit modes.


Description
===========

Get supported transmit mode.

.. _lirc-mode-pulse:

Currently, only ``LIRC_MODE_PULSE`` is supported by lircd on TX. On
puse mode, a sequence of pulse/space integer values are written to the
lirc device using ``write()``.

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

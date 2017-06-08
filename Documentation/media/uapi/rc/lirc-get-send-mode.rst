.. -*- coding: utf-8; mode: rst -*-

.. _lirc_get_send_mode:
.. _lirc_set_send_mode:

************************************************
ioctls LIRC_GET_SEND_MODE and LIRC_SET_SEND_MODE
************************************************

Name
====

LIRC_GET_SEND_MODE/LIRC_SET_SEND_MODE - Get/set supported transmit mode.

Synopsis
========

.. c:function:: int ioctl( int fd, LIRC_GET_SEND_MODE, __u32 *tx_modes )
    :name: LIRC_GET_SEND_MODE

.. c:function:: int ioctl( int fd, LIRC_SET_SEND_MODE, __u32 *tx_modes )
    :name: LIRC_SET_SEND_MODE

Arguments
=========

``fd``
    File descriptor returned by open().

``tx_modes``
    Bitmask with the supported transmit modes.


Description
===========

Get/set current transmit mode.

Only :ref:`LIRC_MODE_PULSE <lirc-mode-pulse>` is supported by for IR send,
depending on the driver. Use :ref:`lirc_get_features` to find out which
modes the driver supports.

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

.. -*- coding: utf-8; mode: rst -*-

.. _lirc_get_rec_mode:
.. _lirc_set_rec_mode:

**********************************************
ioctls LIRC_GET_REC_MODE and LIRC_SET_REC_MODE
**********************************************

Name
====

LIRC_GET_REC_MODE/LIRC_SET_REC_MODE - Get/set supported receive modes.

Synopsis
========

.. c:function:: int ioctl( int fd, LIRC_GET_REC_MODE, __u32 rx_modes)
	:name: LIRC_GET_REC_MODE

.. c:function:: int ioctl( int fd, LIRC_SET_REC_MODE, __u32 rx_modes)
	:name: LIRC_SET_REC_MODE

Arguments
=========

``fd``
    File descriptor returned by open().

``rx_modes``
    Bitmask with the supported transmit modes.

Description
===========

Get/set supported receive modes. Only :ref:`LIRC_MODE_MODE2 <lirc-mode-mode2>`
is supported for IR receive.

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

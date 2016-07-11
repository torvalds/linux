.. -*- coding: utf-8; mode: rst -*-

.. _lirc_get_rec_mode:

***********************
ioctl LIRC_GET_REC_MODE
***********************

Name
====

LIRC_GET_REC_MODE - Get supported receive modes.

Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, __u32 rx_modes)

Arguments
=========

``fd``
    File descriptor returned by open().

``request``
    LIRC_GET_REC_MODE

``rx_modes``
    Bitmask with the supported transmit modes.

Description
===========

Get supported receive modes.

Supported receive modes
=======================

.. _lirc-mode-mode2:

``LIRC_MODE_MODE2``

    The driver returns a sequence of pulse and space codes to userspace.

.. _lirc-mode-lirccode:

``LIRC_MODE_LIRCCODE``

    The IR signal is decoded internally by the receiver. The LIRC interface
    returns the scancode as an integer value. This is the usual mode used
    by several TV media cards.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

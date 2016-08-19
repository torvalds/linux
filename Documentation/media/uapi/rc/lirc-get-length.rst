.. -*- coding: utf-8; mode: rst -*-

.. _lirc_get_length:

*********************
ioctl LIRC_GET_LENGTH
*********************

Name
====

LIRC_GET_LENGTH - Retrieves the code length in bits.

Synopsis
========

.. c:function:: int ioctl( int fd, LIRC_GET_LENGTH, __u32 *length )
    :name: LIRC_GET_LENGTH

Arguments
=========

``fd``
    File descriptor returned by open().

``length``
    length, in bits


Description
===========

Retrieves the code length in bits (only for ``LIRC-MODE-LIRCCODE``).
Reads on the device must be done in blocks matching the bit count.
The bit could should be rounded up so that it matches full bytes.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

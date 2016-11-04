.. -*- coding: utf-8; mode: rst -*-

.. _lirc_set_send_carrier:

***************************
ioctl LIRC_SET_SEND_CARRIER
***************************

Name
====

LIRC_SET_SEND_CARRIER - Set send carrier used to modulate IR TX.


Synopsis
========

.. c:function:: int ioctl( int fd, LIRC_SET_SEND_CARRIER, __u32 *frequency )
    :name: LIRC_SET_SEND_CARRIER

Arguments
=========

``fd``
    File descriptor returned by open().

``frequency``
    Frequency of the carrier to be modulated, in Hz.

Description
===========

Set send carrier used to modulate IR PWM pulses and spaces.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

.. -*- coding: utf-8; mode: rst -*-

.. _lirc_set_rec_carrier:

**************************
ioctl LIRC_SET_REC_CARRIER
**************************

Name
====

LIRC_SET_REC_CARRIER - Set carrier used to modulate IR receive.


Synopsis
========

.. c:function:: int ioctl( int fd, LIRC_SET_REC_CARRIER, __u32 *frequency )
    :name: LIRC_SET_REC_CARRIER

Arguments
=========

``fd``
    File descriptor returned by open().

``frequency``
    Frequency of the carrier that modulates PWM data, in Hz.

Description
===========

Set receive carrier used to modulate IR PWM pulses and spaces.

.. note::

   If called together with :ref:`LIRC_SET_REC_CARRIER_RANGE`, this ioctl
   sets the upper bound frequency that will be recognized by the device.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

.. -*- coding: utf-8; mode: rst -*-

.. _lirc_set_rec_carrier_range:

********************************
ioctl LIRC_SET_REC_CARRIER_RANGE
********************************

Name
====

LIRC_SET_REC_CARRIER_RANGE - Set lower bound of the carrier used to modulate
IR receive.

Synopsis
========

.. c:function:: int ioctl( int fd, LIRC_SET_REC_CARRIER_RANGE, __u32 *frequency )
    :name: LIRC_SET_REC_CARRIER_RANGE

Arguments
=========

``fd``
    File descriptor returned by open().

``frequency``
    Frequency of the carrier that modulates PWM data, in Hz.

Description
===========

This ioctl sets the upper range of carrier frequency that will be recognized
by the IR receiver.

.. note::

   To set a range use :ref:`LIRC_SET_REC_CARRIER_RANGE
   <LIRC_SET_REC_CARRIER_RANGE>` with the lower bound first and later call
   :ref:`LIRC_SET_REC_CARRIER <LIRC_SET_REC_CARRIER>` with the upper bound.

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

.. -*- coding: utf-8; mode: rst -*-

.. _lirc_set_send_duty_cycle:

******************************
ioctl LIRC_SET_SEND_DUTY_CYCLE
******************************

Name
====

LIRC_SET_SEND_DUTY_CYCLE - Set the duty cycle of the carrier signal for
IR transmit.

Synopsis
========

.. c:function:: int ioctl( int fd, LIRC_SET_SEND_DUTY_CYCLE, __u32 *duty_cycle)
    :name: LIRC_SET_SEND_DUTY_CYCLE

Arguments
=========

``fd``
    File descriptor returned by open().

``duty_cycle``
    Duty cicle, describing the pulse width in percent (from 1 to 99) of
    the total cycle. Values 0 and 100 are reserved.


Description
===========

Get/set the duty cycle of the carrier signal for IR transmit.

Currently, no special meaning is defined for 0 or 100, but this
could be used to switch off carrier generation in the future, so
these values should be reserved.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

.. -*- coding: utf-8; mode: rst -*-

.. _lirc_set_measure_carrier_mode:

***********************************
ioctl LIRC_SET_MEASURE_CARRIER_MODE
***********************************

Name
====

LIRC_SET_MEASURE_CARRIER_MODE - enable or disable measure mode

Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, __u32 *enable )

Arguments
=========

``fd``
    File descriptor returned by open().

``request``
    LIRC_SET_MEASURE_CARRIER_MODE

``enable``
    enable = 1 means enable measure mode, enable = 0 means disable measure
    mode.


Description
===========

.. _lirc-mode2-frequency:

Enable or disable measure mode. If enabled, from the next key
press on, the driver will send ``LIRC_MODE2_FREQUENCY`` packets. By
default this should be turned off.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

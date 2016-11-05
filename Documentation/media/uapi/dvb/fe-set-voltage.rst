.. -*- coding: utf-8; mode: rst -*-

.. _FE_SET_VOLTAGE:

********************
ioctl FE_SET_VOLTAGE
********************

Name
====

FE_SET_VOLTAGE - Allow setting the DC level sent to the antenna subsystem.


Synopsis
========

.. c:function:: int ioctl( int fd, FE_SET_VOLTAGE, enum fe_sec_voltage *voltage )
    :name: FE_SET_VOLTAGE


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <frontend_f_open>`.

``voltage``
    pointer to enum :c:type:`fe_sec_voltage`

    Valid values are described at enum
    :c:type:`fe_sec_voltage`.


Description
===========

This ioctl allows to set the DC voltage level sent through the antenna
cable to 13V, 18V or off.

Usually, a satellite antenna subsystems require that the digital TV
device to send a DC voltage to feed power to the LNBf. Depending on the
LNBf type, the polarization or the intermediate frequency (IF) of the
LNBf can controlled by the voltage level. Other devices (for example,
the ones that implement DISEqC and multipoint LNBf's don't need to
control the voltage level, provided that either 13V or 18V is sent to
power up the LNBf.

.. attention:: if more than one device is connected to the same antenna,
   setting a voltage level may interfere on other devices, as they may lose
   the capability of setting polarization or IF. So, on those cases, setting
   the voltage to SEC_VOLTAGE_OFF while the device is not is used is
   recommended.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

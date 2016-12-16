.. -*- coding: utf-8; mode: rst -*-

.. _FE_DISEQC_RESET_OVERLOAD:

******************************
ioctl FE_DISEQC_RESET_OVERLOAD
******************************

Name
====

FE_DISEQC_RESET_OVERLOAD - Restores the power to the antenna subsystem, if it was powered off due - to power overload.


Synopsis
========

.. c:function:: int ioctl( int fd, FE_DISEQC_RESET_OVERLOAD, NULL )
    :name: FE_DISEQC_RESET_OVERLOAD


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <frontend_f_open>`.

Description
===========

If the bus has been automatically powered off due to power overload,
this ioctl call restores the power to the bus. The call requires
read/write access to the device. This call has no effect if the device
is manually powered off. Not all DVB adapters support this ioctl.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

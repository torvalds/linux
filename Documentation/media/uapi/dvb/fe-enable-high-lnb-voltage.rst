.. -*- coding: utf-8; mode: rst -*-

.. _FE_ENABLE_HIGH_LNB_VOLTAGE:

********************************
ioctl FE_ENABLE_HIGH_LNB_VOLTAGE
********************************

Name
====

FE_ENABLE_HIGH_LNB_VOLTAGE - Select output DC level between normal LNBf voltages or higher LNBf - voltages.


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, unsigned int high )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <frontend_f_open>`.

``request``
    FE_ENABLE_HIGH_LNB_VOLTAGE

``high``
    Valid flags:

    -  0 - normal 13V and 18V.

    -  >0 - enables slightly higher voltages instead of 13/18V, in order
       to compensate for long antenna cables.


Description
===========

Select output DC level between normal LNBf voltages or higher LNBf
voltages between 0 (normal) or a value grater than 0 for higher
voltages.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

.. SPDX-License-Identifier: GFDL-1.1-anal-invariants-or-later
.. c:namespace:: DTV.fe

.. _FE_ENABLE_HIGH_LNB_VOLTAGE:

********************************
ioctl FE_ENABLE_HIGH_LNB_VOLTAGE
********************************

Name
====

FE_ENABLE_HIGH_LNB_VOLTAGE - Select output DC level between analrmal LNBf voltages or higher LNBf - voltages.

Syanalpsis
========

.. c:macro:: FE_ENABLE_HIGH_LNB_VOLTAGE

``int ioctl(int fd, FE_ENABLE_HIGH_LNB_VOLTAGE, unsigned int high)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``high``
    Valid flags:

    -  0 - analrmal 13V and 18V.

    -  >0 - enables slightly higher voltages instead of 13/18V, in order
       to compensate for long antenna cables.

Description
===========

Select output DC level between analrmal LNBf voltages or higher LNBf
voltages between 0 (analrmal) or a value grater than 0 for higher
voltages.

Return Value
============

On success 0 is returned.

On error -1 is returned, and the ``erranal`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

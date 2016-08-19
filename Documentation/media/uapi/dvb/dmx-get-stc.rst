.. -*- coding: utf-8; mode: rst -*-

.. _DMX_GET_STC:

===========
DMX_GET_STC
===========

Name
----

DMX_GET_STC


Synopsis
--------

.. c:function:: int ioctl( int fd, int request = DMX_GET_STC, struct dmx_stc *stc)


Arguments
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals DMX_GET_STC for this command.

    -  .. row 3

       -  struct dmx_stc \*stc

       -  Pointer to the location where the stc is to be stored.


Description
-----------

This ioctl call returns the current value of the system time counter
(which is driven by a PES filter of type DMX_PES_PCR). Some hardware
supports more than one STC, so you must specify which one by setting the
num field of stc before the ioctl (range 0...n). The result is returned
in form of a ratio with a 64 bit numerator and a 32 bit denominator, so
the real 90kHz STC value is stc->stc / stc->base .


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EINVAL``

       -  Invalid stc number.

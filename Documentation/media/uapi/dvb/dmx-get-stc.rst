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

.. c:function:: int ioctl( int fd, DMX_GET_STC, struct dmx_stc *stc)
    :name: DMX_GET_STC

Arguments
---------

``fd``
    File descriptor returned by :c:func:`open() <dvb-dmx-open>`.

``stc``
    Pointer to :c:type:`dmx_stc` where the stc data is to be stored.


Description
-----------

This ioctl call returns the current value of the system time counter
(which is driven by a PES filter of type :c:type:`DMX_PES_PCR <dmx_ts_pes>`).
Some hardware supports more than one STC, so you must specify which one by
setting the :c:type:`num <dmx_stc>` field of stc before the ioctl (range 0...n).
The result is returned in form of a ratio with a 64 bit numerator
and a 32 bit denominator, so the real 90kHz STC value is
``stc->stc / stc->base``.


Return Value
------------

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

.. tabularcolumns:: |p{2.5cm}|p{15.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths: 1 16

    -  .. row 1

       -  ``EINVAL``

       -  Invalid stc number.


The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

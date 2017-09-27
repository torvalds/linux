.. -*- coding: utf-8; mode: rst -*-

.. _DMX_SET_FILTER:

==============
DMX_SET_FILTER
==============

Name
----

DMX_SET_FILTER


Synopsis
--------

.. c:function:: int ioctl( int fd, DMX_SET_FILTER, struct dmx_sct_filter_params *params)
    :name: DMX_SET_FILTER

Arguments
---------

``fd``
    File descriptor returned by :c:func:`open() <dvb-dmx-open>`.

``params``

    Pointer to structure containing filter parameters.


Description
-----------

This ioctl call sets up a filter according to the filter and mask
parameters provided. A timeout may be defined stating number of seconds
to wait for a section to be loaded. A value of 0 means that no timeout
should be applied. Finally there is a flag field where it is possible to
state whether a section should be CRC-checked, whether the filter should
be a ”one-shot” filter, i.e. if the filtering operation should be
stopped after the first section is received, and whether the filtering
operation should be started immediately (without waiting for a
:ref:`DMX_START` ioctl call). If a filter was previously set-up, this
filter will be canceled, and the receive buffer will be flushed.


Return Value
------------


On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

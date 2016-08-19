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

.. c:function:: int ioctl( int fd, int request = DMX_SET_FILTER, struct dmx_sct_filter_params *params)


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

       -  Equals DMX_SET_FILTER for this command.

    -  .. row 3

       -  struct dmx_sct_filter_params \*params

       -  Pointer to structure containing filter parameters.


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
DMX_START ioctl call). If a filter was previously set-up, this filter
will be canceled, and the receive buffer will be flushed.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

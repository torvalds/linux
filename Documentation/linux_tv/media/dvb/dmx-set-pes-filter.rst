.. -*- coding: utf-8; mode: rst -*-

.. _DMX_SET_PES_FILTER:

==================
DMX_SET_PES_FILTER
==================

Name
----

DMX_SET_PES_FILTER


Synopsis
--------

.. cpp:function:: int ioctl( int fd, int request = DMX_SET_PES_FILTER, struct dmx_pes_filter_params *params)


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

       -  Equals DMX_SET_PES_FILTER for this command.

    -  .. row 3

       -  struct dmx_pes_filter_params \*params

       -  Pointer to structure containing filter parameters.


Description
-----------

This ioctl call sets up a PES filter according to the parameters
provided. By a PES filter is meant a filter that is based just on the
packet identifier (PID), i.e. no PES header or payload filtering
capability is supported.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EBUSY``

       -  This error code indicates that there are conflicting requests.
	  There are active filters filtering data from another input source.
	  Make sure that these filters are stopped before starting this
	  filter.

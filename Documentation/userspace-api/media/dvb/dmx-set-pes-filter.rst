.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.dmx

.. _DMX_SET_PES_FILTER:

==================
DMX_SET_PES_FILTER
==================

Name
----

DMX_SET_PES_FILTER

Synopsis
--------

.. c:macro:: DMX_SET_PES_FILTER

``int ioctl(int fd, DMX_SET_PES_FILTER, struct dmx_pes_filter_params *params)``

Arguments
---------

``fd``
    File descriptor returned by :c:func:`open()`.

``params``
    Pointer to structure containing filter parameters.

Description
-----------

This ioctl call sets up a PES filter according to the parameters
provided. By a PES filter is meant a filter that is based just on the
packet identifier (PID), i.e. no PES header or payload filtering
capability is supported.

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

       -  ``EBUSY``

       -  This error code indicates that there are conflicting requests.
	  There are active filters filtering data from another input source.
	  Make sure that these filters are stopped before starting this
	  filter.

The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

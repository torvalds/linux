.. -*- coding: utf-8; mode: rst -*-

.. _DMX_START:

=========
DMX_START
=========

Name
----

DMX_START


Synopsis
--------

.. c:function:: int ioctl( int fd, DMX_START)
    :name: DMX_START


Arguments
---------

``fd``
    File descriptor returned by :c:func:`open() <dvb-dmx-open>`.

Description
-----------

This ioctl call is used to start the actual filtering operation defined
via the ioctl calls DMX_SET_FILTER or DMX_SET_PES_FILTER.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

.. tabularcolumns:: |p{2.5cm}|p{15.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EINVAL``

       -  Invalid argument, i.e. no filtering parameters provided via the
	  DMX_SET_FILTER or DMX_SET_PES_FILTER functions.

    -  .. row 2

       -  ``EBUSY``

       -  This error code indicates that there are conflicting requests.
	  There are active filters filtering data from another input source.
	  Make sure that these filters are stopped before starting this
	  filter.

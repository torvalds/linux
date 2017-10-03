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
via the ioctl calls :ref:`DMX_SET_FILTER` or :ref:`DMX_SET_PES_FILTER`.


Return Value
------------

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

.. tabularcolumns:: |p{2.5cm}|p{15.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EINVAL``

       -  Invalid argument, i.e. no filtering parameters provided via the
	  :ref:`DMX_SET_FILTER` or :ref:`DMX_SET_PES_FILTER` ioctls.

    -  .. row 2

       -  ``EBUSY``

       -  This error code indicates that there are conflicting requests.
	  There are active filters filtering data from another input source.
	  Make sure that these filters are stopped before starting this
	  filter.


The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

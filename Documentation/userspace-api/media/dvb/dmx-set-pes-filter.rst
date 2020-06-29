.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _DMX_SET_PES_FILTER:

==================
DMX_SET_PES_FILTER
==================

Name
----

DMX_SET_PES_FILTER


Synopsis
--------

.. c:function:: int ioctl( int fd, DMX_SET_PES_FILTER, struct dmx_pes_filter_params *params)
    :name: DMX_SET_PES_FILTER


Arguments
---------


``fd``
    File descriptor returned by :c:func:`open() <dvb-dmx-open>`.

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

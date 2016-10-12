.. -*- coding: utf-8; mode: rst -*-

.. _dmx_fread:

================
DVB demux read()
================

Name
----

DVB demux read()


Synopsis
--------

.. c:function:: size_t read(int fd, void *buf, size_t count)
    :name: dvb-dmx-read

Arguments
---------

``fd``
  File descriptor returned by a previous call to :c:func:`open() <dvb-ca-open>`.

 ``buf``
   Buffer to be filled

``count``
   Max number of bytes to read

Description
-----------

This system call returns filtered data, which might be section or PES
data. The filtered data is transferred from the driver’s internal
circular buffer to buf. The maximum amount of data to be transferred is
implied by count.

Return Value
------------

.. tabularcolumns:: |p{2.5cm}|p{15.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EWOULDBLOCK``

       -  No data to return and O_NONBLOCK was specified.

    -  .. row 2

       -  ``EBADF``

       -  fd is not a valid open file descriptor.

    -  .. row 3

       -  ``ECRC``

       -  Last section had a CRC error - no data returned. The buffer is
	  flushed.

    -  .. row 4

       -  ``EOVERFLOW``

       -

    -  .. row 5

       -
       -  The filtered data was not read from the buffer in due time,
	  resulting in non-read data being lost. The buffer is flushed.

    -  .. row 6

       -  ``ETIMEDOUT``

       -  The section was not loaded within the stated timeout period. See
	  ioctl DMX_SET_FILTER for how to set a timeout.

    -  .. row 7

       -  ``EFAULT``

       -  The driver failed to write to the callers buffer due to an invalid
	  \*buf pointer.

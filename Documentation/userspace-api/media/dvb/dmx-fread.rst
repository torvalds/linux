.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.dmx

.. _dmx_fread:

=======================
Digital TV demux read()
=======================

Name
----

Digital TV demux read()

Synopsis
--------

.. c:function:: size_t read(int fd, void *buf, size_t count)

Arguments
---------

``fd``
  File descriptor returned by a previous call to :c:func:`open()`.

 ``buf``
   Buffer to be filled

``count``
   Max number of bytes to read

Description
-----------

This system call returns filtered data, which might be section or Packetized
Elementary Stream (PES) data. The filtered data is transferred from
the driver’s internal circular buffer to ``buf``. The maximum amount of data
to be transferred is implied by count.

.. note::

   if a section filter created with
   :c:type:`DMX_CHECK_CRC <dmx_sct_filter_params>` flag set,
   data that fails on CRC check will be silently ignored.

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

    -  -  ``EWOULDBLOCK``
       -  No data to return and ``O_NONBLOCK`` was specified.

    -  -  ``EOVERFLOW``
       -  The filtered data was not read from the buffer in due time,
	  resulting in non-read data being lost. The buffer is flushed.

    -  -  ``ETIMEDOUT``
       -  The section was not loaded within the stated timeout period.
          See ioctl :ref:`DMX_SET_FILTER` for how to set a timeout.

    -  -  ``EFAULT``
       -  The driver failed to write to the callers buffer due to an
          invalid \*buf pointer.

The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

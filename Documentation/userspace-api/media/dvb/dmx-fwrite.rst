.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.dmx

.. _dmx_fwrite:

========================
Digital TV demux write()
========================

Name
----

Digital TV demux write()

Synopsis
--------

.. c:function:: ssize_t write(int fd, const void *buf, size_t count)

Arguments
---------

``fd``
  File descriptor returned by a previous call to :c:func:`open()`.

``buf``
     Buffer with data to be written

``count``
    Number of bytes at the buffer

Description
-----------

This system call is only provided by the logical device
``/dev/dvb/adapter?/dvr?``, associated with the physical demux device that
provides the actual DVR functionality. It is used for replay of a
digitally recorded Transport Stream. Matching filters have to be defined
in the corresponding physical demux device, ``/dev/dvb/adapter?/demux?``.
The amount of data to be transferred is implied by count.

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
       -  No data was written. This might happen if ``O_NONBLOCK`` was
	  specified and there is no more buffer space available (if
	  ``O_NONBLOCK`` is not specified the function will block until buffer
	  space is available).

    -  -  ``EBUSY``
       -  This error code indicates that there are conflicting requests. The
	  corresponding demux device is setup to receive data from the
	  front- end. Make sure that these filters are stopped and that the
	  filters with input set to ``DMX_IN_DVR`` are started.

The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

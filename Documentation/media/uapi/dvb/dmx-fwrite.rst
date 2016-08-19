.. -*- coding: utf-8; mode: rst -*-

.. _dmx_fwrite:

=================
DVB demux write()
=================

Name
----

DVB demux write()


Synopsis
--------

.. cpp:function:: ssize_t write(int fd, const void *buf, size_t count)


Arguments
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  void \*buf

       -  Pointer to the buffer containing the Transport Stream.

    -  .. row 3

       -  size_t count

       -  Size of buf.


Description
-----------

This system call is only provided by the logical device
/dev/dvb/adapter0/dvr0, associated with the physical demux device that
provides the actual DVR functionality. It is used for replay of a
digitally recorded Transport Stream. Matching filters have to be defined
in the corresponding physical demux device, /dev/dvb/adapter0/demux0.
The amount of data to be transferred is implied by count.


Return Value
------------

.. tabularcolumns:: |p{2.5cm}|p{15.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EWOULDBLOCK``

       -  No data was written. This might happen if O_NONBLOCK was
	  specified and there is no more buffer space available (if
	  O_NONBLOCK is not specified the function will block until buffer
	  space is available).

    -  .. row 2

       -  ``EBUSY``

       -  This error code indicates that there are conflicting requests. The
	  corresponding demux device is setup to receive data from the
	  front- end. Make sure that these filters are stopped and that the
	  filters with input set to DMX_IN_DVR are started.

    -  .. row 3

       -  ``EBADF``

       -  fd is not a valid open file descriptor.

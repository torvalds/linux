.. -*- coding: utf-8; mode: rst -*-

.. _dmx_fcalls:

********************
Demux Function Calls
********************


.. _dmx_fopen:

DVB demux open()
================

Description
-----------

This system call, used with a device name of /dev/dvb/adapter0/demux0,
allocates a new filter and returns a handle which can be used for
subsequent control of that filter. This call has to be made for each
filter to be used, i.e. every returned file descriptor is a reference to
a single filter. /dev/dvb/adapter0/dvr0 is a logical device to be used
for retrieving Transport Streams for digital video recording. When
reading from this device a transport stream containing the packets from
all PES filters set in the corresponding demux device
(/dev/dvb/adapter0/demux0) having the output set to DMX_OUT_TS_TAP. A
recorded Transport Stream is replayed by writing to this device.

The significance of blocking or non-blocking mode is described in the
documentation for functions where there is a difference. It does not
affect the semantics of the open() call itself. A device opened in
blocking mode can later be put into non-blocking mode (and vice versa)
using the F_SETFL command of the fcntl system call.

Synopsis
--------

.. c:function:: int open(const char *deviceName, int flags)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  const char \*deviceName

       -  Name of demux device.

    -  .. row 2

       -  int flags

       -  A bit-wise OR of the following flags:

    -  .. row 3

       -
       -  O_RDWR read/write access

    -  .. row 4

       -
       -  O_NONBLOCK open in non-blocking mode

    -  .. row 5

       -
       -  (blocking mode is the default)


Return Value
------------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``ENODEV``

       -  Device driver not loaded/available.

    -  .. row 2

       -  ``EINVAL``

       -  Invalid argument.

    -  .. row 3

       -  ``EMFILE``

       -  “Too many open files”, i.e. no more filters available.

    -  .. row 4

       -  ``ENOMEM``

       -  The driver failed to allocate enough memory.



.. _dmx_fclose:

DVB demux close()
=================

Description
-----------

This system call deactivates and deallocates a filter that was
previously allocated via the open() call.

Synopsis
--------

.. c:function:: int close(int fd)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().


Return Value
------------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EBADF``

       -  fd is not a valid open file descriptor.



.. _dmx_fread:

DVB demux read()
================

Description
-----------

This system call returns filtered data, which might be section or PES
data. The filtered data is transferred from the driver’s internal
circular buffer to buf. The maximum amount of data to be transferred is
implied by count.

Synopsis
--------

.. c:function:: size_t read(int fd, void *buf, size_t count)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  void \*buf

       -  Pointer to the buffer to be used for returned filtered data.

    -  .. row 3

       -  size_t count

       -  Size of buf.


Return Value
------------



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



.. _dmx_fwrite:

DVB demux write()
=================

Description
-----------

This system call is only provided by the logical device
/dev/dvb/adapter0/dvr0, associated with the physical demux device that
provides the actual DVR functionality. It is used for replay of a
digitally recorded Transport Stream. Matching filters have to be defined
in the corresponding physical demux device, /dev/dvb/adapter0/demux0.
The amount of data to be transferred is implied by count.

Synopsis
--------

.. c:function:: ssize_t write(int fd, const void *buf, size_t count)

Arguments
----------



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


Return Value
------------



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



.. _DMX_START:

DMX_START
=========

Description
-----------

This ioctl call is used to start the actual filtering operation defined
via the ioctl calls DMX_SET_FILTER or DMX_SET_PES_FILTER.

Synopsis
--------

.. c:function:: int ioctl( int fd, int request = DMX_START)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals DMX_START for this command.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



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



.. _DMX_STOP:

DMX_STOP
========

Description
-----------

This ioctl call is used to stop the actual filtering operation defined
via the ioctl calls DMX_SET_FILTER or DMX_SET_PES_FILTER and
started via the DMX_START command.

Synopsis
--------

.. c:function:: int ioctl( int fd, int request = DMX_STOP)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals DMX_STOP for this command.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _DMX_SET_FILTER:

DMX_SET_FILTER
==============

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

Synopsis
--------

.. c:function:: int ioctl( int fd, int request = DMX_SET_FILTER, struct dmx_sct_filter_params *params)

Arguments
----------



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


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _DMX_SET_PES_FILTER:

DMX_SET_PES_FILTER
==================

Description
-----------

This ioctl call sets up a PES filter according to the parameters
provided. By a PES filter is meant a filter that is based just on the
packet identifier (PID), i.e. no PES header or payload filtering
capability is supported.

Synopsis
--------

.. c:function:: int ioctl( int fd, int request = DMX_SET_PES_FILTER, struct dmx_pes_filter_params *params)

Arguments
----------



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



.. _DMX_SET_BUFFER_SIZE:

DMX_SET_BUFFER_SIZE
===================

Description
-----------

This ioctl call is used to set the size of the circular buffer used for
filtered data. The default size is two maximum sized sections, i.e. if
this function is not called a buffer size of 2 \* 4096 bytes will be
used.

Synopsis
--------

.. c:function:: int ioctl( int fd, int request = DMX_SET_BUFFER_SIZE, unsigned long size)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals DMX_SET_BUFFER_SIZE for this command.

    -  .. row 3

       -  unsigned long size

       -  Size of circular buffer.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _DMX_GET_EVENT:

DMX_GET_EVENT
=============

Description
-----------

This ioctl call returns an event if available. If an event is not
available, the behavior depends on whether the device is in blocking or
non-blocking mode. In the latter case, the call fails immediately with
errno set to ``EWOULDBLOCK``. In the former case, the call blocks until an
event becomes available.

Synopsis
--------

.. c:function:: int ioctl( int fd, int request = DMX_GET_EVENT, struct dmx_event *ev)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals DMX_GET_EVENT for this command.

    -  .. row 3

       -  struct dmx_event \*ev

       -  Pointer to the location where the event is to be stored.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EWOULDBLOCK``

       -  There is no event pending, and the device is in non-blocking mode.



.. _DMX_GET_STC:

DMX_GET_STC
===========

Description
-----------

This ioctl call returns the current value of the system time counter
(which is driven by a PES filter of type DMX_PES_PCR). Some hardware
supports more than one STC, so you must specify which one by setting the
num field of stc before the ioctl (range 0...n). The result is returned
in form of a ratio with a 64 bit numerator and a 32 bit denominator, so
the real 90kHz STC value is stc->stc / stc->base .

Synopsis
--------

.. c:function:: int ioctl( int fd, int request = DMX_GET_STC, struct dmx_stc *stc)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals DMX_GET_STC for this command.

    -  .. row 3

       -  struct dmx_stc \*stc

       -  Pointer to the location where the stc is to be stored.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EINVAL``

       -  Invalid stc number.



.. _DMX_GET_PES_PIDS:

DMX_GET_PES_PIDS
================

Description
-----------

This ioctl is undocumented. Documentation is welcome.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = DMX_GET_PES_PIDS, __u16[5])

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals DMX_GET_PES_PIDS for this command.

    -  .. row 3

       -  __u16[5]

       -  Undocumented.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _DMX_GET_CAPS:

DMX_GET_CAPS
============

Description
-----------

This ioctl is undocumented. Documentation is welcome.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = DMX_GET_CAPS, dmx_caps_t *)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals DMX_GET_CAPS for this command.

    -  .. row 3

       -  dmx_caps_t *

       -  Undocumented.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _DMX_SET_SOURCE:

DMX_SET_SOURCE
==============

Description
-----------

This ioctl is undocumented. Documentation is welcome.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = DMX_SET_SOURCE, dmx_source_t *)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals DMX_SET_SOURCE for this command.

    -  .. row 3

       -  dmx_source_t *

       -  Undocumented.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _DMX_ADD_PID:

DMX_ADD_PID
===========

Description
-----------

This ioctl call allows to add multiple PIDs to a transport stream filter
previously set up with DMX_SET_PES_FILTER and output equal to
DMX_OUT_TSDEMUX_TAP.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = DMX_ADD_PID, __u16 *)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals DMX_ADD_PID for this command.

    -  .. row 3

       -  __u16 *

       -  PID number to be filtered.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _DMX_REMOVE_PID:

DMX_REMOVE_PID
==============

Description
-----------

This ioctl call allows to remove a PID when multiple PIDs are set on a
transport stream filter, e. g. a filter previously set up with output
equal to DMX_OUT_TSDEMUX_TAP, created via either
DMX_SET_PES_FILTER or DMX_ADD_PID.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = DMX_REMOVE_PID, __u16 *)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals DMX_REMOVE_PID for this command.

    -  .. row 3

       -  __u16 *

       -  PID of the PES filter to be removed.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

.. -*- coding: utf-8; mode: rst -*-

.. _dmx_fopen:

================
DVB demux open()
================

Name
----

DVB demux open()


Synopsis
--------

.. cpp:function:: int open(const char *deviceName, int flags)


Arguments
---------

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

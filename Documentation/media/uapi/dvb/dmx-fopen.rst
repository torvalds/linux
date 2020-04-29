.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _dmx_fopen:

=======================
Digital TV demux open()
=======================

Name
----

Digital TV demux open()


Synopsis
--------

.. c:function:: int open(const char *deviceName, int flags)
    :name: dvb-dmx-open

Arguments
---------

``name``
  Name of specific Digital TV demux device.

``flags``
  A bit-wise OR of the following flags:

.. tabularcolumns:: |p{2.5cm}|p{15.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths: 1 16

    -
       - ``O_RDONLY``
       - read-only access

    -
       - ``O_RDWR``
       - read/write access

    -
       - ``O_NONBLOCK``
       - open in non-blocking mode
         (blocking mode is the default)


Description
-----------

This system call, used with a device name of ``/dev/dvb/adapter?/demux?``,
allocates a new filter and returns a handle which can be used for
subsequent control of that filter. This call has to be made for each
filter to be used, i.e. every returned file descriptor is a reference to
a single filter. ``/dev/dvb/adapter?/dvr?`` is a logical device to be used
for retrieving Transport Streams for digital video recording. When
reading from this device a transport stream containing the packets from
all PES filters set in the corresponding demux device
(``/dev/dvb/adapter?/demux?``) having the output set to ``DMX_OUT_TS_TAP``.
A recorded Transport Stream is replayed by writing to this device.

The significance of blocking or non-blocking mode is described in the
documentation for functions where there is a difference. It does not
affect the semantics of the ``open()`` call itself. A device opened
in blocking mode can later be put into non-blocking mode (and vice versa)
using the ``F_SETFL`` command of the fcntl system call.


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

    -  -  ``EMFILE``
       -  “Too many open files”, i.e. no more filters available.

The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

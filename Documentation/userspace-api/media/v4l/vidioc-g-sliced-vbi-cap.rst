.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: V4L

.. _VIDIOC_G_SLICED_VBI_CAP:

*****************************
ioctl VIDIOC_G_SLICED_VBI_CAP
*****************************

Name
====

VIDIOC_G_SLICED_VBI_CAP - Query sliced VBI capabilities

Synopsis
========

.. c:macro:: VIDIOC_G_SLICED_VBI_CAP

``int ioctl(int fd, VIDIOC_G_SLICED_VBI_CAP, struct v4l2_sliced_vbi_cap *argp)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``argp``
    Pointer to struct :c:type:`v4l2_sliced_vbi_cap`.

Description
===========

To find out which data services are supported by a sliced VBI capture or
output device, applications initialize the ``type`` field of a struct
:c:type:`v4l2_sliced_vbi_cap`, clear the
``reserved`` array and call the :ref:`VIDIOC_G_SLICED_VBI_CAP <VIDIOC_G_SLICED_VBI_CAP>` ioctl. The
driver fills in the remaining fields or returns an ``EINVAL`` error code if
the sliced VBI API is unsupported or ``type`` is invalid.

.. note::

   The ``type`` field was added, and the ioctl changed from read-only
   to write-read, in Linux 2.6.19.

.. c:type:: v4l2_sliced_vbi_cap

.. tabularcolumns:: |p{1.2cm}|p{4.2cm}|p{4.1cm}|p{4.0cm}|p{4.0cm}|

.. flat-table:: struct v4l2_sliced_vbi_cap
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 3 2 2 2

    * - __u16
      - ``service_set``
      - :cspan:`2` A set of all data services supported by the driver.

	Equal to the union of all elements of the ``service_lines`` array.
    * - __u16
      - ``service_lines``\ [2][24]
      - :cspan:`2` Each element of this array contains a set of data
	services the hardware can look for or insert into a particular
	scan line. Data services are defined in :ref:`vbi-services`.
	Array indices map to ITU-R line numbers\ [#f1]_ as follows:
    * -
      -
      - Element
      - 525 line systems
      - 625 line systems
    * -
      -
      - ``service_lines``\ [0][1]
      - 1
      - 1
    * -
      -
      - ``service_lines``\ [0][23]
      - 23
      - 23
    * -
      -
      - ``service_lines``\ [1][1]
      - 264
      - 314
    * -
      -
      - ``service_lines``\ [1][23]
      - 286
      - 336
    * -
    * -
      -
      - :cspan:`2` The number of VBI lines the hardware can capture or
	output per frame, or the number of services it can identify on a
	given line may be limited. For example on PAL line 16 the hardware
	may be able to look for a VPS or Teletext signal, but not both at
	the same time. Applications can learn about these limits using the
	:ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl as described in
	:ref:`sliced`.
    * -
    * -
      -
      - :cspan:`2` Drivers must set ``service_lines`` [0][0] and
	``service_lines``\ [1][0] to zero.
    * - __u32
      - ``type``
      - Type of the data stream, see :c:type:`v4l2_buf_type`. Should be
	``V4L2_BUF_TYPE_SLICED_VBI_CAPTURE`` or
	``V4L2_BUF_TYPE_SLICED_VBI_OUTPUT``.
    * - __u32
      - ``reserved``\ [3]
      - :cspan:`2` This array is reserved for future extensions.

	Applications and drivers must set it to zero.

.. [#f1]

   See also :ref:`vbi-525` and :ref:`vbi-625`.

.. raw:: latex

    \scriptsize

.. tabularcolumns:: |p{3.5cm}|p{1.0cm}|p{2.0cm}|p{2.0cm}|p{8.0cm}|

.. _vbi-services:

.. flat-table:: Sliced VBI services
    :header-rows:  1
    :stub-columns: 0
    :widths:       2 1 1 2 2

    * - Symbol
      - Value
      - Reference
      - Lines, usually
      - Payload
    * - ``V4L2_SLICED_TELETEXT_B`` (Teletext System B)
      - 0x0001
      - :ref:`ets300706`,

	:ref:`itu653`
      - PAL/SECAM line 7-22, 320-335 (second field 7-22)
      - Last 42 of the 45 byte Teletext packet, that is without clock
	run-in and framing code, lsb first transmitted.
    * - ``V4L2_SLICED_VPS``
      - 0x0400
      - :ref:`ets300231`
      - PAL line 16
      - Byte number 3 to 15 according to Figure 9 of ETS 300 231, lsb
	first transmitted.
    * - ``V4L2_SLICED_CAPTION_525``
      - 0x1000
      - :ref:`cea608`
      - NTSC line 21, 284 (second field 21)
      - Two bytes in transmission order, including parity bit, lsb first
	transmitted.
    * - ``V4L2_SLICED_WSS_625``
      - 0x4000
      - :ref:`en300294`,

	:ref:`itu1119`
      - PAL/SECAM line 23
      -

	::

	    Byte        0                 1
		 msb         lsb  msb           lsb
	    Bit  7 6 5 4 3 2 1 0  x x 13 12 11 10 9
    * - ``V4L2_SLICED_VBI_525``
      - 0x1000
      - :cspan:`2` Set of services applicable to 525 line systems.
    * - ``V4L2_SLICED_VBI_625``
      - 0x4401
      - :cspan:`2` Set of services applicable to 625 line systems.

.. raw:: latex

    \normalsize

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The value in the ``type`` field is wrong.

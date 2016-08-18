.. -*- coding: utf-8; mode: rst -*-

.. _sliced:

*************************
Sliced VBI Data Interface
*************************

VBI stands for Vertical Blanking Interval, a gap in the sequence of
lines of an analog video signal. During VBI no picture information is
transmitted, allowing some time while the electron beam of a cathode ray
tube TV returns to the top of the screen.

Sliced VBI devices use hardware to demodulate data transmitted in the
VBI. V4L2 drivers shall *not* do this by software, see also the
:ref:`raw VBI interface <raw-vbi>`. The data is passed as short
packets of fixed size, covering one scan line each. The number of
packets per video frame is variable.

Sliced VBI capture and output devices are accessed through the same
character special files as raw VBI devices. When a driver supports both
interfaces, the default function of a ``/dev/vbi`` device is *raw* VBI
capturing or output, and the sliced VBI function is only available after
calling the :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl as defined
below. Likewise a ``/dev/video`` device may support the sliced VBI API,
however the default function here is video capturing or output.
Different file descriptors must be used to pass raw and sliced VBI data
simultaneously, if this is supported by the driver.


Querying Capabilities
=====================

Devices supporting the sliced VBI capturing or output API set the
``V4L2_CAP_SLICED_VBI_CAPTURE`` or ``V4L2_CAP_SLICED_VBI_OUTPUT`` flag
respectively, in the ``capabilities`` field of struct
:ref:`v4l2_capability <v4l2-capability>` returned by the
:ref:`VIDIOC_QUERYCAP` ioctl. At least one of the
read/write, streaming or asynchronous :ref:`I/O methods <io>` must be
supported. Sliced VBI devices may have a tuner or modulator.


Supplemental Functions
======================

Sliced VBI devices shall support :ref:`video input or output <video>`
and :ref:`tuner or modulator <tuner>` ioctls if they have these
capabilities, and they may support :ref:`control` ioctls.
The :ref:`video standard <standard>` ioctls provide information vital
to program a sliced VBI device, therefore must be supported.


.. _sliced-vbi-format-negotitation:

Sliced VBI Format Negotiation
=============================

To find out which data services are supported by the hardware
applications can call the
:ref:`VIDIOC_G_SLICED_VBI_CAP <VIDIOC_G_SLICED_VBI_CAP>` ioctl.
All drivers implementing the sliced VBI interface must support this
ioctl. The results may differ from those of the
:ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl when the number of VBI
lines the hardware can capture or output per frame, or the number of
services it can identify on a given line are limited. For example on PAL
line 16 the hardware may be able to look for a VPS or Teletext signal,
but not both at the same time.

To determine the currently selected services applications set the
``type`` field of struct :ref:`v4l2_format <v4l2-format>` to
``V4L2_BUF_TYPE_SLICED_VBI_CAPTURE`` or
``V4L2_BUF_TYPE_SLICED_VBI_OUTPUT``, and the
:ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>` ioctl fills the ``fmt.sliced``
member, a struct
:ref:`v4l2_sliced_vbi_format <v4l2-sliced-vbi-format>`.

Applications can request different parameters by initializing or
modifying the ``fmt.sliced`` member and calling the
:ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl with a pointer to the
:ref:`struct v4l2_format <v4l2-format>` structure.

The sliced VBI API is more complicated than the raw VBI API because the
hardware must be told which VBI service to expect on each scan line. Not
all services may be supported by the hardware on all lines (this is
especially true for VBI output where Teletext is often unsupported and
other services can only be inserted in one specific line). In many
cases, however, it is sufficient to just set the ``service_set`` field
to the required services and let the driver fill the ``service_lines``
array according to hardware capabilities. Only if more precise control
is needed should the programmer set the ``service_lines`` array
explicitly.

The :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl modifies the parameters
according to hardware capabilities. When the driver allocates resources
at this point, it may return an ``EBUSY`` error code if the required
resources are temporarily unavailable. Other resource allocation points
which may return ``EBUSY`` can be the
:ref:`VIDIOC_STREAMON` ioctl and the first
:ref:`read() <func-read>`, :ref:`write() <func-write>` and
:ref:`select() <func-select>` call.


.. _v4l2-sliced-vbi-format:

struct v4l2_sliced_vbi_format
-----------------------------

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{2.9cm}|p{2.9cm}|p{2.9cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 3 2 2 2


    -  .. row 1

       -  __u32

       -  ``service_set``

       -  :cspan:`2`

	  If ``service_set`` is non-zero when passed with
	  :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` or
	  :ref:`VIDIOC_TRY_FMT <VIDIOC_G_FMT>`, the ``service_lines``
	  array will be filled by the driver according to the services
	  specified in this field. For example, if ``service_set`` is
	  initialized with ``V4L2_SLICED_TELETEXT_B | V4L2_SLICED_WSS_625``,
	  a driver for the cx25840 video decoder sets lines 7-22 of both
	  fields [#f1]_ to ``V4L2_SLICED_TELETEXT_B`` and line 23 of the first
	  field to ``V4L2_SLICED_WSS_625``. If ``service_set`` is set to
	  zero, then the values of ``service_lines`` will be used instead.

	  On return the driver sets this field to the union of all elements
	  of the returned ``service_lines`` array. It may contain less
	  services than requested, perhaps just one, if the hardware cannot
	  handle more services simultaneously. It may be empty (zero) if
	  none of the requested services are supported by the hardware.

    -  .. row 2

       -  __u16

       -  ``service_lines``\ [2][24]

       -  :cspan:`2`

	  Applications initialize this array with sets of data services the
	  driver shall look for or insert on the respective scan line.
	  Subject to hardware capabilities drivers return the requested set,
	  a subset, which may be just a single service, or an empty set.
	  When the hardware cannot handle multiple services on the same line
	  the driver shall choose one. No assumptions can be made on which
	  service the driver chooses.

	  Data services are defined in :ref:`vbi-services2`. Array indices
	  map to ITU-R line numbers\ [#f2]_ as follows:

    -  .. row 3

       -
       -
       -  Element

       -  525 line systems

       -  625 line systems

    -  .. row 4

       -
       -
       -  ``service_lines``\ [0][1]

       -  1

       -  1

    -  .. row 5

       -
       -
       -  ``service_lines``\ [0][23]

       -  23

       -  23

    -  .. row 6

       -
       -
       -  ``service_lines``\ [1][1]

       -  264

       -  314

    -  .. row 7

       -
       -
       -  ``service_lines``\ [1][23]

       -  286

       -  336

    -  .. row 8

       -
       -
       -  :cspan:`2` Drivers must set ``service_lines`` [0][0] and
	  ``service_lines``\ [1][0] to zero. The
	  ``V4L2_VBI_ITU_525_F1_START``, ``V4L2_VBI_ITU_525_F2_START``,
	  ``V4L2_VBI_ITU_625_F1_START`` and ``V4L2_VBI_ITU_625_F2_START``
	  defines give the start line numbers for each field for each 525 or
	  625 line format as a convenience. Don't forget that ITU line
	  numbering starts at 1, not 0.

    -  .. row 9

       -  __u32

       -  ``io_size``

       -  :cspan:`2` Maximum number of bytes passed by one
	  :ref:`read() <func-read>` or :ref:`write() <func-write>` call,
	  and the buffer size in bytes for the
	  :ref:`VIDIOC_QBUF` and
	  :ref:`VIDIOC_DQBUF <VIDIOC_QBUF>` ioctl. Drivers set this field
	  to the size of struct
	  :ref:`v4l2_sliced_vbi_data <v4l2-sliced-vbi-data>` times the
	  number of non-zero elements in the returned ``service_lines``
	  array (that is the number of lines potentially carrying data).

    -  .. row 10

       -  __u32

       -  ``reserved``\ [2]

       -  :cspan:`2` This array is reserved for future extensions.
	  Applications and drivers must set it to zero.



.. _vbi-services2:

Sliced VBI services
-------------------

.. tabularcolumns:: |p{4.4cm}|p{2.2cm}|p{2.2cm}|p{4.4cm}|p{4.3cm}|

.. flat-table::
    :header-rows:  1
    :stub-columns: 0
    :widths:       2 1 1 2 2


    -  .. row 1

       -  Symbol

       -  Value

       -  Reference

       -  Lines, usually

       -  Payload

    -  .. row 2

       -  ``V4L2_SLICED_TELETEXT_B`` (Teletext System B)

       -  0x0001

       -  :ref:`ets300706`, :ref:`itu653`

       -  PAL/SECAM line 7-22, 320-335 (second field 7-22)

       -  Last 42 of the 45 byte Teletext packet, that is without clock
	  run-in and framing code, lsb first transmitted.

    -  .. row 3

       -  ``V4L2_SLICED_VPS``

       -  0x0400

       -  :ref:`ets300231`

       -  PAL line 16

       -  Byte number 3 to 15 according to Figure 9 of ETS 300 231, lsb
	  first transmitted.

    -  .. row 4

       -  ``V4L2_SLICED_CAPTION_525``

       -  0x1000

       -  :ref:`cea608`

       -  NTSC line 21, 284 (second field 21)

       -  Two bytes in transmission order, including parity bit, lsb first
	  transmitted.

    -  .. row 5

       -  ``V4L2_SLICED_WSS_625``

       -  0x4000

       -  :ref:`itu1119`, :ref:`en300294`

       -  PAL/SECAM line 23

       -

	  ::

	      Byte         0                 1
		    msb         lsb  msb           lsb
	       Bit  7 6 5 4 3 2 1 0  x x 13 12 11 10 9

    -  .. row 6

       -  ``V4L2_SLICED_VBI_525``

       -  0x1000

       -  :cspan:`2` Set of services applicable to 525 line systems.

    -  .. row 7

       -  ``V4L2_SLICED_VBI_625``

       -  0x4401

       -  :cspan:`2` Set of services applicable to 625 line systems.


Drivers may return an ``EINVAL`` error code when applications attempt to
read or write data without prior format negotiation, after switching the
video standard (which may invalidate the negotiated VBI parameters) and
after switching the video input (which may change the video standard as
a side effect). The :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl may
return an ``EBUSY`` error code when applications attempt to change the
format while i/o is in progress (between a
:ref:`VIDIOC_STREAMON` and
:ref:`VIDIOC_STREAMOFF <VIDIOC_STREAMON>` call, and after the first
:ref:`read() <func-read>` or :ref:`write() <func-write>` call).


Reading and writing sliced VBI data
===================================

A single :ref:`read() <func-read>` or :ref:`write() <func-write>`
call must pass all data belonging to one video frame. That is an array
of :ref:`struct v4l2_sliced_vbi_data <v4l2-sliced-vbi-data>` structures with one or
more elements and a total size not exceeding ``io_size`` bytes. Likewise
in streaming I/O mode one buffer of ``io_size`` bytes must contain data
of one video frame. The ``id`` of unused
:ref:`struct v4l2_sliced_vbi_data <v4l2-sliced-vbi-data>` elements must be zero.


.. _v4l2-sliced-vbi-data:

struct v4l2_sliced_vbi_data
---------------------------

.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  __u32

       -  ``id``

       -  A flag from :ref:`vbi-services` identifying the type of data in
	  this packet. Only a single bit must be set. When the ``id`` of a
	  captured packet is zero, the packet is empty and the contents of
	  other fields are undefined. Applications shall ignore empty
	  packets. When the ``id`` of a packet for output is zero the
	  contents of the ``data`` field are undefined and the driver must
	  no longer insert data on the requested ``field`` and ``line``.

    -  .. row 2

       -  __u32

       -  ``field``

       -  The video field number this data has been captured from, or shall
	  be inserted at. ``0`` for the first field, ``1`` for the second
	  field.

    -  .. row 3

       -  __u32

       -  ``line``

       -  The field (as opposed to frame) line number this data has been
	  captured from, or shall be inserted at. See :ref:`vbi-525` and
	  :ref:`vbi-625` for valid values. Sliced VBI capture devices can
	  set the line number of all packets to ``0`` if the hardware cannot
	  reliably identify scan lines. The field number must always be
	  valid.

    -  .. row 4

       -  __u32

       -  ``reserved``

       -  This field is reserved for future extensions. Applications and
	  drivers must set it to zero.

    -  .. row 5

       -  __u8

       -  ``data``\ [48]

       -  The packet payload. See :ref:`vbi-services` for the contents and
	  number of bytes passed for each data type. The contents of padding
	  bytes at the end of this array are undefined, drivers and
	  applications shall ignore them.


Packets are always passed in ascending line number order, without
duplicate line numbers. The :ref:`write() <func-write>` function and
the :ref:`VIDIOC_QBUF` ioctl must return an ``EINVAL``
error code when applications violate this rule. They must also return an
EINVAL error code when applications pass an incorrect field or line
number, or a combination of ``field``, ``line`` and ``id`` which has not
been negotiated with the :ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>` or
:ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl. When the line numbers are
unknown the driver must pass the packets in transmitted order. The
driver can insert empty packets with ``id`` set to zero anywhere in the
packet array.

To assure synchronization and to distinguish from frame dropping, when a
captured frame does not carry any of the requested data services drivers
must pass one or more empty packets. When an application fails to pass
VBI data in time for output, the driver must output the last VPS and WSS
packet again, and disable the output of Closed Caption and Teletext
data, or output data which is ignored by Closed Caption and Teletext
decoders.

A sliced VBI device may support :ref:`read/write <rw>` and/or
streaming (:ref:`memory mapping <mmap>` and/or
:ref:`user pointer <userp>`) I/O. The latter bears the possibility of
synchronizing video and VBI data by using buffer timestamps.


Sliced VBI Data in MPEG Streams
===============================

If a device can produce an MPEG output stream, it may be capable of
providing
:ref:`negotiated sliced VBI services <sliced-vbi-format-negotitation>`
as data embedded in the MPEG stream. Users or applications control this
sliced VBI data insertion with the
:ref:`V4L2_CID_MPEG_STREAM_VBI_FMT <v4l2-mpeg-stream-vbi-fmt>`
control.

If the driver does not provide the
:ref:`V4L2_CID_MPEG_STREAM_VBI_FMT <v4l2-mpeg-stream-vbi-fmt>`
control, or only allows that control to be set to
:ref:`V4L2_MPEG_STREAM_VBI_FMT_NONE <v4l2-mpeg-stream-vbi-fmt>`,
then the device cannot embed sliced VBI data in the MPEG stream.

The
:ref:`V4L2_CID_MPEG_STREAM_VBI_FMT <v4l2-mpeg-stream-vbi-fmt>`
control does not implicitly set the device driver to capture nor cease
capturing sliced VBI data. The control only indicates to embed sliced
VBI data in the MPEG stream, if an application has negotiated sliced VBI
service be captured.

It may also be the case that a device can embed sliced VBI data in only
certain types of MPEG streams: for example in an MPEG-2 PS but not an
MPEG-2 TS. In this situation, if sliced VBI data insertion is requested,
the sliced VBI data will be embedded in MPEG stream types when
supported, and silently omitted from MPEG stream types where sliced VBI
data insertion is not supported by the device.

The following subsections specify the format of the embedded sliced VBI
data.


MPEG Stream Embedded, Sliced VBI Data Format: NONE
--------------------------------------------------

The
:ref:`V4L2_MPEG_STREAM_VBI_FMT_NONE <v4l2-mpeg-stream-vbi-fmt>`
embedded sliced VBI format shall be interpreted by drivers as a control
to cease embedding sliced VBI data in MPEG streams. Neither the device
nor driver shall insert "empty" embedded sliced VBI data packets in the
MPEG stream when this format is set. No MPEG stream data structures are
specified for this format.


MPEG Stream Embedded, Sliced VBI Data Format: IVTV
--------------------------------------------------

The
:ref:`V4L2_MPEG_STREAM_VBI_FMT_IVTV <v4l2-mpeg-stream-vbi-fmt>`
embedded sliced VBI format, when supported, indicates to the driver to
embed up to 36 lines of sliced VBI data per frame in an MPEG-2 *Private
Stream 1 PES* packet encapsulated in an MPEG-2 *Program Pack* in the
MPEG stream.

*Historical context*: This format specification originates from a
custom, embedded, sliced VBI data format used by the ``ivtv`` driver.
This format has already been informally specified in the kernel sources
in the file ``Documentation/video4linux/cx2341x/README.vbi`` . The
maximum size of the payload and other aspects of this format are driven
by the CX23415 MPEG decoder's capabilities and limitations with respect
to extracting, decoding, and displaying sliced VBI data embedded within
an MPEG stream.

This format's use is *not* exclusive to the ``ivtv`` driver *nor*
exclusive to CX2341x devices, as the sliced VBI data packet insertion
into the MPEG stream is implemented in driver software. At least the
``cx18`` driver provides sliced VBI data insertion into an MPEG-2 PS in
this format as well.

The following definitions specify the payload of the MPEG-2 *Private
Stream 1 PES* packets that contain sliced VBI data when
:ref:`V4L2_MPEG_STREAM_VBI_FMT_IVTV <v4l2-mpeg-stream-vbi-fmt>`
is set. (The MPEG-2 *Private Stream 1 PES* packet header and
encapsulating MPEG-2 *Program Pack* header are not detailed here. Please
refer to the MPEG-2 specifications for details on those packet headers.)

The payload of the MPEG-2 *Private Stream 1 PES* packets that contain
sliced VBI data is specified by struct
:ref:`v4l2_mpeg_vbi_fmt_ivtv <v4l2-mpeg-vbi-fmt-ivtv>`. The
payload is variable length, depending on the actual number of lines of
sliced VBI data present in a video frame. The payload may be padded at
the end with unspecified fill bytes to align the end of the payload to a
4-byte boundary. The payload shall never exceed 1552 bytes (2 fields
with 18 lines/field with 43 bytes of data/line and a 4 byte magic
number).


.. _v4l2-mpeg-vbi-fmt-ivtv:

struct v4l2_mpeg_vbi_fmt_ivtv
-----------------------------

.. tabularcolumns:: |p{3.5cm}|p{3.5cm}|p{3.5cm}|p{7.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 1 2


    -  .. row 1

       -  __u8

       -  ``magic``\ [4]

       -
       -  A "magic" constant from :ref:`v4l2-mpeg-vbi-fmt-ivtv-magic` that
	  indicates this is a valid sliced VBI data payload and also
	  indicates which member of the anonymous union, ``itv0`` or
	  ``ITV0``, to use for the payload data.

    -  .. row 2

       -  union

       -  (anonymous)

    -  .. row 3

       -
       -  struct :ref:`v4l2_mpeg_vbi_itv0 <v4l2-mpeg-vbi-itv0>`

       -  ``itv0``

       -  The primary form of the sliced VBI data payload that contains
	  anywhere from 1 to 35 lines of sliced VBI data. Line masks are
	  provided in this form of the payload indicating which VBI lines
	  are provided.

    -  .. row 4

       -
       -  struct :ref:`v4l2_mpeg_vbi_ITV0 <v4l2-mpeg-vbi-itv0-1>`

       -  ``ITV0``

       -  An alternate form of the sliced VBI data payload used when 36
	  lines of sliced VBI data are present. No line masks are provided
	  in this form of the payload; all valid line mask bits are
	  implcitly set.



.. _v4l2-mpeg-vbi-fmt-ivtv-magic:

Magic Constants for struct v4l2_mpeg_vbi_fmt_ivtv magic field
-------------------------------------------------------------

.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. flat-table::
    :header-rows:  1
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  Defined Symbol

       -  Value

       -  Description

    -  .. row 2

       -  ``V4L2_MPEG_VBI_IVTV_MAGIC0``

       -  "itv0"

       -  Indicates the ``itv0`` member of the union in struct
	  :ref:`v4l2_mpeg_vbi_fmt_ivtv <v4l2-mpeg-vbi-fmt-ivtv>` is
	  valid.

    -  .. row 3

       -  ``V4L2_MPEG_VBI_IVTV_MAGIC1``

       -  "ITV0"

       -  Indicates the ``ITV0`` member of the union in struct
	  :ref:`v4l2_mpeg_vbi_fmt_ivtv <v4l2-mpeg-vbi-fmt-ivtv>` is
	  valid and that 36 lines of sliced VBI data are present.



.. _v4l2-mpeg-vbi-itv0:

struct v4l2_mpeg_vbi_itv0
-------------------------

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __le32

       -  ``linemask``\ [2]

       -  Bitmasks indicating the VBI service lines present. These
	  ``linemask`` values are stored in little endian byte order in the
	  MPEG stream. Some reference ``linemask`` bit positions with their
	  corresponding VBI line number and video field are given below.
	  b\ :sub:`0` indicates the least significant bit of a ``linemask``
	  value:



	  ::

	      linemask[0] b0:     line  6     first field
	      linemask[0] b17:        line 23     first field
	      linemask[0] b18:        line  6     second field
	      linemask[0] b31:        line 19     second field
	      linemask[1] b0:     line 20     second field
	      linemask[1] b3:     line 23     second field
	      linemask[1] b4-b31: unused and set to 0

    -  .. row 2

       -  struct
	  :ref:`v4l2_mpeg_vbi_itv0_line <v4l2-mpeg-vbi-itv0-line>`

       -  ``line``\ [35]

       -  This is a variable length array that holds from 1 to 35 lines of
	  sliced VBI data. The sliced VBI data lines present correspond to
	  the bits set in the ``linemask`` array, starting from b\ :sub:`0`
	  of ``linemask``\ [0] up through b\ :sub:`31` of ``linemask``\ [0],
	  and from b\ :sub:`0` of ``linemask``\ [1] up through b\ :sub:`3` of
	  ``linemask``\ [1]. ``line``\ [0] corresponds to the first bit
	  found set in the ``linemask`` array, ``line``\ [1] corresponds to
	  the second bit found set in the ``linemask`` array, etc. If no
	  ``linemask`` array bits are set, then ``line``\ [0] may contain
	  one line of unspecified data that should be ignored by
	  applications.



.. _v4l2-mpeg-vbi-itv0-1:

struct v4l2_mpeg_vbi_ITV0
-------------------------

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  struct
	  :ref:`v4l2_mpeg_vbi_itv0_line <v4l2-mpeg-vbi-itv0-line>`

       -  ``line``\ [36]

       -  A fixed length array of 36 lines of sliced VBI data. ``line``\ [0]
	  through ``line``\ [17] correspond to lines 6 through 23 of the
	  first field. ``line``\ [18] through ``line``\ [35] corresponds to
	  lines 6 through 23 of the second field.



.. _v4l2-mpeg-vbi-itv0-line:

struct v4l2_mpeg_vbi_itv0_line
------------------------------

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u8

       -  ``id``

       -  A line identifier value from
	  :ref:`ITV0-Line-Identifier-Constants` that indicates the type of
	  sliced VBI data stored on this line.

    -  .. row 2

       -  __u8

       -  ``data``\ [42]

       -  The sliced VBI data for the line.



.. _ITV0-Line-Identifier-Constants:

Line Identifiers for struct v4l2_mpeg_vbi_itv0_line id field
------------------------------------------------------------

.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. flat-table::
    :header-rows:  1
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  Defined Symbol

       -  Value

       -  Description

    -  .. row 2

       -  ``V4L2_MPEG_VBI_IVTV_TELETEXT_B``

       -  1

       -  Refer to :ref:`Sliced VBI services <vbi-services2>` for a
	  description of the line payload.

    -  .. row 3

       -  ``V4L2_MPEG_VBI_IVTV_CAPTION_525``

       -  4

       -  Refer to :ref:`Sliced VBI services <vbi-services2>` for a
	  description of the line payload.

    -  .. row 4

       -  ``V4L2_MPEG_VBI_IVTV_WSS_625``

       -  5

       -  Refer to :ref:`Sliced VBI services <vbi-services2>` for a
	  description of the line payload.

    -  .. row 5

       -  ``V4L2_MPEG_VBI_IVTV_VPS``

       -  7

       -  Refer to :ref:`Sliced VBI services <vbi-services2>` for a
	  description of the line payload.



.. [#f1]
   According to :ref:`ETS 300 706 <ets300706>` lines 6-22 of the first
   field and lines 5-22 of the second field may carry Teletext data.

.. [#f2]
   See also :ref:`vbi-525` and :ref:`vbi-625`.

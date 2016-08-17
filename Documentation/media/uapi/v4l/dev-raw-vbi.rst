.. -*- coding: utf-8; mode: rst -*-

.. _raw-vbi:

**********************
Raw VBI Data Interface
**********************

VBI is an abbreviation of Vertical Blanking Interval, a gap in the
sequence of lines of an analog video signal. During VBI no picture
information is transmitted, allowing some time while the electron beam
of a cathode ray tube TV returns to the top of the screen. Using an
oscilloscope you will find here the vertical synchronization pulses and
short data packages ASK modulated [#f1]_ onto the video signal. These are
transmissions of services such as Teletext or Closed Caption.

Subject of this interface type is raw VBI data, as sampled off a video
signal, or to be added to a signal for output. The data format is
similar to uncompressed video images, a number of lines times a number
of samples per line, we call this a VBI image.

Conventionally V4L2 VBI devices are accessed through character device
special files named ``/dev/vbi`` and ``/dev/vbi0`` to ``/dev/vbi31``
with major number 81 and minor numbers 224 to 255. ``/dev/vbi`` is
typically a symbolic link to the preferred VBI device. This convention
applies to both input and output devices.

To address the problems of finding related video and VBI devices VBI
capturing and output is also available as device function under
``/dev/video``. To capture or output raw VBI data with these devices
applications must call the :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl.
Accessed as ``/dev/vbi``, raw VBI capturing or output is the default
device function.


Querying Capabilities
=====================

Devices supporting the raw VBI capturing or output API set the
``V4L2_CAP_VBI_CAPTURE`` or ``V4L2_CAP_VBI_OUTPUT`` flags, respectively,
in the ``capabilities`` field of struct
:ref:`v4l2_capability <v4l2-capability>` returned by the
:ref:`VIDIOC_QUERYCAP` ioctl. At least one of the
read/write, streaming or asynchronous I/O methods must be supported. VBI
devices may or may not have a tuner or modulator.


Supplemental Functions
======================

VBI devices shall support :ref:`video input or output <video>`,
:ref:`tuner or modulator <tuner>`, and :ref:`controls <control>`
ioctls as needed. The :ref:`video standard <standard>` ioctls provide
information vital to program a VBI device, therefore must be supported.


Raw VBI Format Negotiation
==========================

Raw VBI sampling abilities can vary, in particular the sampling
frequency. To properly interpret the data V4L2 specifies an ioctl to
query the sampling parameters. Moreover, to allow for some flexibility
applications can also suggest different parameters.

As usual these parameters are *not* reset at :ref:`open() <func-open>`
time to permit Unix tool chains, programming a device and then reading
from it as if it was a plain file. Well written V4L2 applications should
always ensure they really get what they want, requesting reasonable
parameters and then checking if the actual parameters are suitable.

To query the current raw VBI capture parameters applications set the
``type`` field of a struct :ref:`v4l2_format <v4l2-format>` to
``V4L2_BUF_TYPE_VBI_CAPTURE`` or ``V4L2_BUF_TYPE_VBI_OUTPUT``, and call
the :ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>` ioctl with a pointer to this
structure. Drivers fill the struct
:ref:`v4l2_vbi_format <v4l2-vbi-format>` ``vbi`` member of the
``fmt`` union.

To request different parameters applications set the ``type`` field of a
struct :ref:`v4l2_format <v4l2-format>` as above and initialize all
fields of the struct :ref:`v4l2_vbi_format <v4l2-vbi-format>`
``vbi`` member of the ``fmt`` union, or better just modify the results
of :ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>`, and call the :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>`
ioctl with a pointer to this structure. Drivers return an ``EINVAL`` error
code only when the given parameters are ambiguous, otherwise they modify
the parameters according to the hardware capabilities and return the
actual parameters. When the driver allocates resources at this point, it
may return an ``EBUSY`` error code to indicate the returned parameters are
valid but the required resources are currently not available. That may
happen for instance when the video and VBI areas to capture would
overlap, or when the driver supports multiple opens and another process
already requested VBI capturing or output. Anyway, applications must
expect other resource allocation points which may return ``EBUSY``, at the
:ref:`VIDIOC_STREAMON` ioctl and the first :ref:`read() <func-read>`
, :ref:`write() <func-write>` and :ref:`select() <func-select>` calls.

VBI devices must implement both the :ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>` and
:ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl, even if :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ignores all requests
and always returns default parameters as :ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>` does.
:ref:`VIDIOC_TRY_FMT <VIDIOC_G_FMT>` is optional.


.. _v4l2-vbi-format:

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct v4l2_vbi_format
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``sampling_rate``

       -  Samples per second, i. e. unit 1 Hz.

    -  .. row 2

       -  __u32

       -  ``offset``

       -  Horizontal offset of the VBI image, relative to the leading edge
	  of the line synchronization pulse and counted in samples: The
	  first sample in the VBI image will be located ``offset`` /
	  ``sampling_rate`` seconds following the leading edge. See also
	  :ref:`vbi-hsync`.

    -  .. row 3

       -  __u32

       -  ``samples_per_line``

       -

    -  .. row 4

       -  __u32

       -  ``sample_format``

       -  Defines the sample format as in :ref:`pixfmt`, a
	  four-character-code. [#f2]_ Usually this is ``V4L2_PIX_FMT_GREY``,
	  i. e. each sample consists of 8 bits with lower values oriented
	  towards the black level. Do not assume any other correlation of
	  values with the signal level. For example, the MSB does not
	  necessarily indicate if the signal is 'high' or 'low' because 128
	  may not be the mean value of the signal. Drivers shall not convert
	  the sample format by software.

    -  .. row 5

       -  __u32

       -  ``start``\ [#f2]_

       -  This is the scanning system line number associated with the first
	  line of the VBI image, of the first and the second field
	  respectively. See :ref:`vbi-525` and :ref:`vbi-625` for valid
	  values. The ``V4L2_VBI_ITU_525_F1_START``,
	  ``V4L2_VBI_ITU_525_F2_START``, ``V4L2_VBI_ITU_625_F1_START`` and
	  ``V4L2_VBI_ITU_625_F2_START`` defines give the start line numbers
	  for each field for each 525 or 625 line format as a convenience.
	  Don't forget that ITU line numbering starts at 1, not 0. VBI input
	  drivers can return start values 0 if the hardware cannot reliable
	  identify scanning lines, VBI acquisition may not require this
	  information.

    -  .. row 6

       -  __u32

       -  ``count``\ [#f2]_

       -  The number of lines in the first and second field image,
	  respectively.

    -  .. row 7

       -  :cspan:`2`

	  Drivers should be as flexibility as possible. For example, it may
	  be possible to extend or move the VBI capture window down to the
	  picture area, implementing a 'full field mode' to capture data
	  service transmissions embedded in the picture.

	  An application can set the first or second ``count`` value to zero
	  if no data is required from the respective field; ``count``\ [1]
	  if the scanning system is progressive, i. e. not interlaced. The
	  corresponding start value shall be ignored by the application and
	  driver. Anyway, drivers may not support single field capturing and
	  return both count values non-zero.

	  Both ``count`` values set to zero, or line numbers outside the
	  bounds depicted in :ref:`vbi-525` and :ref:`vbi-625`, or a
	  field image covering lines of two fields, are invalid and shall
	  not be returned by the driver.

	  To initialize the ``start`` and ``count`` fields, applications
	  must first determine the current video standard selection. The
	  :ref:`v4l2_std_id <v4l2-std-id>` or the ``framelines`` field
	  of struct :ref:`v4l2_standard <v4l2-standard>` can be evaluated
	  for this purpose.

    -  .. row 8

       -  __u32

       -  ``flags``

       -  See :ref:`vbifmt-flags` below. Currently only drivers set flags,
	  applications must set this field to zero.

    -  .. row 9

       -  __u32

       -  ``reserved``\ [#f2]_

       -  This array is reserved for future extensions. Drivers and
	  applications must set it to zero.



.. _vbifmt-flags:

.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. flat-table:: Raw VBI Format Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  ``V4L2_VBI_UNSYNC``

       -  0x0001

       -  This flag indicates hardware which does not properly distinguish
	  between fields. Normally the VBI image stores the first field
	  (lower scanning line numbers) first in memory. This may be a top
	  or bottom field depending on the video standard. When this flag is
	  set the first or second field may be stored first, however the
	  fields are still in correct temporal order with the older field
	  first in memory. [#f3]_

    -  .. row 2

       -  ``V4L2_VBI_INTERLACED``

       -  0x0002

       -  By default the two field images will be passed sequentially; all
	  lines of the first field followed by all lines of the second field
	  (compare :ref:`field-order` ``V4L2_FIELD_SEQ_TB`` and
	  ``V4L2_FIELD_SEQ_BT``, whether the top or bottom field is first in
	  memory depends on the video standard). When this flag is set, the
	  two fields are interlaced (cf. ``V4L2_FIELD_INTERLACED``). The
	  first line of the first field followed by the first line of the
	  second field, then the two second lines, and so on. Such a layout
	  may be necessary when the hardware has been programmed to capture
	  or output interlaced video images and is unable to separate the
	  fields for VBI capturing at the same time. For simplicity setting
	  this flag implies that both ``count`` values are equal and
	  non-zero.



.. _vbi-hsync:

.. figure::  dev-raw-vbi_files/vbi_hsync.*
    :alt:    vbi_hsync.pdf / vbi_hsync.gif
    :align:  center

    **Figure 4.1. Line synchronization**


.. _vbi-525:

.. figure::  dev-raw-vbi_files/vbi_525.*
    :alt:    vbi_525.pdf / vbi_525.gif
    :align:  center

    **Figure 4.2. ITU-R 525 line numbering (M/NTSC and M/PAL)**



.. _vbi-625:

.. figure::  dev-raw-vbi_files/vbi_625.*
    :alt:    vbi_625.pdf / vbi_625.gif
    :align:  center

    **Figure 4.3. ITU-R 625 line numbering**



Remember the VBI image format depends on the selected video standard,
therefore the application must choose a new standard or query the
current standard first. Attempts to read or write data ahead of format
negotiation, or after switching the video standard which may invalidate
the negotiated VBI parameters, should be refused by the driver. A format
change during active I/O is not permitted.


Reading and writing VBI images
==============================

To assure synchronization with the field number and easier
implementation, the smallest unit of data passed at a time is one frame,
consisting of two fields of VBI images immediately following in memory.

The total size of a frame computes as follows:


.. code-block:: c

    (count[0] + count[1]) * samples_per_line * sample size in bytes

The sample size is most likely always one byte, applications must check
the ``sample_format`` field though, to function properly with other
drivers.

A VBI device may support :ref:`read/write <rw>` and/or streaming
(:ref:`memory mapping <mmap>` or :ref:`user pointer <userp>`) I/O.
The latter bears the possibility of synchronizing video and VBI data by
using buffer timestamps.

Remember the :ref:`VIDIOC_STREAMON <VIDIOC_STREAMON>` ioctl and the
first :ref:`read() <func-read>`, :ref:`write() <func-write>` and
:ref:`select() <func-select>` call can be resource allocation
points returning an ``EBUSY`` error code if the required hardware resources
are temporarily unavailable, for example the device is already in use by
another process.

.. [#f1]
   ASK: Amplitude-Shift Keying. A high signal level represents a '1'
   bit, a low level a '0' bit.

.. [#f2]
   A few devices may be unable to sample VBI data at all but can extend
   the video capture window to the VBI region.

.. [#f3]
   Most VBI services transmit on both fields, but some have different
   semantics depending on the field number. These cannot be reliable
   decoded or encoded when ``V4L2_VBI_UNSYNC`` is set.

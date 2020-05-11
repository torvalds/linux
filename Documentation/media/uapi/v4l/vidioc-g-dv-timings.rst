.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _VIDIOC_G_DV_TIMINGS:

**********************************************
ioctl VIDIOC_G_DV_TIMINGS, VIDIOC_S_DV_TIMINGS
**********************************************

Name
====

VIDIOC_G_DV_TIMINGS - VIDIOC_S_DV_TIMINGS - VIDIOC_SUBDEV_G_DV_TIMINGS - VIDIOC_SUBDEV_S_DV_TIMINGS - Get or set DV timings for input or output


Synopsis
========

.. c:function:: int ioctl( int fd, VIDIOC_G_DV_TIMINGS, struct v4l2_dv_timings *argp )
    :name: VIDIOC_G_DV_TIMINGS

.. c:function:: int ioctl( int fd, VIDIOC_S_DV_TIMINGS, struct v4l2_dv_timings *argp )
    :name: VIDIOC_S_DV_TIMINGS

.. c:function:: int ioctl( int fd, VIDIOC_SUBDEV_G_DV_TIMINGS, struct v4l2_dv_timings *argp )
    :name: VIDIOC_SUBDEV_G_DV_TIMINGS

.. c:function:: int ioctl( int fd, VIDIOC_SUBDEV_S_DV_TIMINGS, struct v4l2_dv_timings *argp )
    :name: VIDIOC_SUBDEV_S_DV_TIMINGS


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``
    Pointer to struct :c:type:`v4l2_dv_timings`.


Description
===========

To set DV timings for the input or output, applications use the
:ref:`VIDIOC_S_DV_TIMINGS <VIDIOC_G_DV_TIMINGS>` ioctl and to get the current timings,
applications use the :ref:`VIDIOC_G_DV_TIMINGS <VIDIOC_G_DV_TIMINGS>` ioctl. The detailed timing
information is filled in using the structure struct
:c:type:`v4l2_dv_timings`. These ioctls take a
pointer to the struct :c:type:`v4l2_dv_timings`
structure as argument. If the ioctl is not supported or the timing
values are not correct, the driver returns ``EINVAL`` error code.

The ``linux/v4l2-dv-timings.h`` header can be used to get the timings of
the formats in the :ref:`cea861` and :ref:`vesadmt` standards. If
the current input or output does not support DV timings (e.g. if
:ref:`VIDIOC_ENUMINPUT` does not set the
``V4L2_IN_CAP_DV_TIMINGS`` flag), then ``ENODATA`` error code is returned.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    This ioctl is not supported, or the :ref:`VIDIOC_S_DV_TIMINGS <VIDIOC_G_DV_TIMINGS>`
    parameter was unsuitable.

ENODATA
    Digital video timings are not supported for this input or output.

EBUSY
    The device is busy and therefore can not change the timings.


.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. c:type:: v4l2_bt_timings

.. flat-table:: struct v4l2_bt_timings
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``width``
      - Width of the active video in pixels.
    * - __u32
      - ``height``
      - Height of the active video frame in lines. So for interlaced
	formats the height of the active video in each field is
	``height``/2.
    * - __u32
      - ``interlaced``
      - Progressive (``V4L2_DV_PROGRESSIVE``) or interlaced (``V4L2_DV_INTERLACED``).
    * - __u32
      - ``polarities``
      - This is a bit mask that defines polarities of sync signals. bit 0
	(``V4L2_DV_VSYNC_POS_POL``) is for vertical sync polarity and bit
	1 (``V4L2_DV_HSYNC_POS_POL``) is for horizontal sync polarity. If
	the bit is set (1) it is positive polarity and if is cleared (0),
	it is negative polarity.
    * - __u64
      - ``pixelclock``
      - Pixel clock in Hz. Ex. 74.25MHz->74250000
    * - __u32
      - ``hfrontporch``
      - Horizontal front porch in pixels
    * - __u32
      - ``hsync``
      - Horizontal sync length in pixels
    * - __u32
      - ``hbackporch``
      - Horizontal back porch in pixels
    * - __u32
      - ``vfrontporch``
      - Vertical front porch in lines. For interlaced formats this refers
	to the odd field (aka field 1).
    * - __u32
      - ``vsync``
      - Vertical sync length in lines. For interlaced formats this refers
	to the odd field (aka field 1).
    * - __u32
      - ``vbackporch``
      - Vertical back porch in lines. For interlaced formats this refers
	to the odd field (aka field 1).
    * - __u32
      - ``il_vfrontporch``
      - Vertical front porch in lines for the even field (aka field 2) of
	interlaced field formats. Must be 0 for progressive formats.
    * - __u32
      - ``il_vsync``
      - Vertical sync length in lines for the even field (aka field 2) of
	interlaced field formats. Must be 0 for progressive formats.
    * - __u32
      - ``il_vbackporch``
      - Vertical back porch in lines for the even field (aka field 2) of
	interlaced field formats. Must be 0 for progressive formats.
    * - __u32
      - ``standards``
      - The video standard(s) this format belongs to. This will be filled
	in by the driver. Applications must set this to 0. See
	:ref:`dv-bt-standards` for a list of standards.
    * - __u32
      - ``flags``
      - Several flags giving more information about the format. See
	:ref:`dv-bt-flags` for a description of the flags.
    * - struct :c:type:`v4l2_fract`
      - ``picture_aspect``
      - The picture aspect if the pixels are not square. Only valid if the
        ``V4L2_DV_FL_HAS_PICTURE_ASPECT`` flag is set.
    * - __u8
      - ``cea861_vic``
      - The Video Identification Code according to the CEA-861 standard.
        Only valid if the ``V4L2_DV_FL_HAS_CEA861_VIC`` flag is set.
    * - __u8
      - ``hdmi_vic``
      - The Video Identification Code according to the HDMI standard.
        Only valid if the ``V4L2_DV_FL_HAS_HDMI_VIC`` flag is set.
    * - __u8
      - ``reserved[46]``
      - Reserved for future extensions. Drivers and applications must set
	the array to zero.


.. tabularcolumns:: |p{3.5cm}|p{3.5cm}|p{7.0cm}|p{3.5cm}|

.. c:type:: v4l2_dv_timings

.. flat-table:: struct v4l2_dv_timings
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``type``
      - Type of DV timings as listed in :ref:`dv-timing-types`.
    * - union {
      - (anonymous)
    * - struct :c:type:`v4l2_bt_timings`
      - ``bt``
      - Timings defined by BT.656/1120 specifications
    * - __u32
      - ``reserved``\ [32]
      -
    * - }
      -

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. _dv-timing-types:

.. flat-table:: DV Timing types
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - Timing type
      - value
      - Description
    * -
      -
      -
    * - ``V4L2_DV_BT_656_1120``
      - 0
      - BT.656/1120 timings

.. tabularcolumns:: |p{4.5cm}|p{12.8cm}|

.. _dv-bt-standards:

.. flat-table:: DV BT Timing standards
    :header-rows:  0
    :stub-columns: 0

    * - Timing standard
      - Description
    * - ``V4L2_DV_BT_STD_CEA861``
      - The timings follow the CEA-861 Digital TV Profile standard
    * - ``V4L2_DV_BT_STD_DMT``
      - The timings follow the VESA Discrete Monitor Timings standard
    * - ``V4L2_DV_BT_STD_CVT``
      - The timings follow the VESA Coordinated Video Timings standard
    * - ``V4L2_DV_BT_STD_GTF``
      - The timings follow the VESA Generalized Timings Formula standard
    * - ``V4L2_DV_BT_STD_SDI``
      - The timings follow the SDI Timings standard.
	There are no horizontal syncs/porches at all in this format.
	Total blanking timings must be set in hsync or vsync fields only.

.. tabularcolumns:: |p{7.0cm}|p{10.5cm}|

.. _dv-bt-flags:

.. flat-table:: DV BT Timing flags
    :header-rows:  0
    :stub-columns: 0

    * - Flag
      - Description
    * - ``V4L2_DV_FL_REDUCED_BLANKING``
      - CVT/GTF specific: the timings use reduced blanking (CVT) or the
	'Secondary GTF' curve (GTF). In both cases the horizontal and/or
	vertical blanking intervals are reduced, allowing a higher
	resolution over the same bandwidth. This is a read-only flag,
	applications must not set this.
    * - ``V4L2_DV_FL_CAN_REDUCE_FPS``
      - CEA-861 specific: set for CEA-861 formats with a framerate that is
	a multiple of six. These formats can be optionally played at 1 /
	1.001 speed to be compatible with 60 Hz based standards such as
	NTSC and PAL-M that use a framerate of 29.97 frames per second. If
	the transmitter can't generate such frequencies, then the flag
	will also be cleared. This is a read-only flag, applications must
	not set this.
    * - ``V4L2_DV_FL_REDUCED_FPS``
      - CEA-861 specific: only valid for video transmitters or video
        receivers that have the ``V4L2_DV_FL_CAN_DETECT_REDUCED_FPS``
	set. This flag is cleared otherwise. It is also only valid for
	formats with the ``V4L2_DV_FL_CAN_REDUCE_FPS`` flag set, for other
	formats the flag will be cleared by the driver.

	If the application sets this flag for a transmitter, then the
	pixelclock used to set up the transmitter is divided by 1.001 to
	make it compatible with NTSC framerates. If the transmitter can't
	generate such frequencies, then the flag will be cleared.

	If a video receiver detects that the format uses a reduced framerate,
	then it will set this flag to signal this to the application.
    * - ``V4L2_DV_FL_HALF_LINE``
      - Specific to interlaced formats: if set, then the vertical
	frontporch of field 1 (aka the odd field) is really one half-line
	longer and the vertical backporch of field 2 (aka the even field)
	is really one half-line shorter, so each field has exactly the
	same number of half-lines. Whether half-lines can be detected or
	used depends on the hardware.
    * - ``V4L2_DV_FL_IS_CE_VIDEO``
      - If set, then this is a Consumer Electronics (CE) video format.
	Such formats differ from other formats (commonly called IT
	formats) in that if R'G'B' encoding is used then by default the
	R'G'B' values use limited range (i.e. 16-235) as opposed to full
	range (i.e. 0-255). All formats defined in CEA-861 except for the
	640x480p59.94 format are CE formats.
    * - ``V4L2_DV_FL_FIRST_FIELD_EXTRA_LINE``
      - Some formats like SMPTE-125M have an interlaced signal with a odd
	total height. For these formats, if this flag is set, the first
	field has the extra line. Else, it is the second field.
    * - ``V4L2_DV_FL_HAS_PICTURE_ASPECT``
      - If set, then the picture_aspect field is valid. Otherwise assume that
        the pixels are square, so the picture aspect ratio is the same as the
	width to height ratio.
    * - ``V4L2_DV_FL_HAS_CEA861_VIC``
      - If set, then the cea861_vic field is valid and contains the Video
        Identification Code as per the CEA-861 standard.
    * - ``V4L2_DV_FL_HAS_HDMI_VIC``
      - If set, then the hdmi_vic field is valid and contains the Video
        Identification Code as per the HDMI standard (HDMI Vendor Specific
	InfoFrame).
    * - ``V4L2_DV_FL_CAN_DETECT_REDUCED_FPS``
      - CEA-861 specific: only valid for video receivers, the flag is
        cleared by transmitters.
        If set, then the hardware can detect the difference between
	regular framerates and framerates reduced by 1000/1001. E.g.:
	60 vs 59.94 Hz, 30 vs 29.97 Hz or 24 vs 23.976 Hz.

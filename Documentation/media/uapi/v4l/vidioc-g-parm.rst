.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _VIDIOC_G_PARM:

**********************************
ioctl VIDIOC_G_PARM, VIDIOC_S_PARM
**********************************

Name
====

VIDIOC_G_PARM - VIDIOC_S_PARM - Get or set streaming parameters


Synopsis
========

.. c:function:: int ioctl( int fd, VIDIOC_G_PARM, v4l2_streamparm *argp )
    :name: VIDIOC_G_PARM

.. c:function:: int ioctl( int fd, VIDIOC_S_PARM, v4l2_streamparm *argp )
    :name: VIDIOC_S_PARM


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``
    Pointer to struct :c:type:`v4l2_streamparm`.


Description
===========

The current video standard determines a nominal number of frames per
second. If less than this number of frames is to be captured or output,
applications can request frame skipping or duplicating on the driver
side. This is especially useful when using the :ref:`read() <func-read>` or
:ref:`write() <func-write>`, which are not augmented by timestamps or sequence
counters, and to avoid unnecessary data copying.

Changing the frame interval shall never change the format. Changing the
format, on the other hand, may change the frame interval.

Further these ioctls can be used to determine the number of buffers used
internally by a driver in read/write mode. For implications see the
section discussing the :ref:`read() <func-read>` function.

To get and set the streaming parameters applications call the
:ref:`VIDIOC_G_PARM <VIDIOC_G_PARM>` and :ref:`VIDIOC_S_PARM <VIDIOC_G_PARM>` ioctl, respectively. They take a
pointer to a struct :c:type:`v4l2_streamparm` which contains a
union holding separate parameters for input and output devices.


.. tabularcolumns:: |p{3.5cm}|p{3.5cm}|p{3.5cm}|p{7.0cm}|

.. c:type:: v4l2_streamparm

.. flat-table:: struct v4l2_streamparm
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 1 2

    * - __u32
      - ``type``
      -
      - The buffer (stream) type, same as struct
	:c:type:`v4l2_format` ``type``, set by the
	application. See :c:type:`v4l2_buf_type`.
    * - union
      - ``parm``
      -
      -
    * -
      - struct :c:type:`v4l2_captureparm`
      - ``capture``
      - Parameters for capture devices, used when ``type`` is
	``V4L2_BUF_TYPE_VIDEO_CAPTURE`` or
	``V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE``.
    * -
      - struct :c:type:`v4l2_outputparm`
      - ``output``
      - Parameters for output devices, used when ``type`` is
	``V4L2_BUF_TYPE_VIDEO_OUTPUT`` or ``V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE``.
    * -
      - __u8
      - ``raw_data``\ [200]
      - A place holder for future extensions.



.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. c:type:: v4l2_captureparm

.. flat-table:: struct v4l2_captureparm
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``capability``
      - See :ref:`parm-caps`.
    * - __u32
      - ``capturemode``
      - Set by drivers and applications, see :ref:`parm-flags`.
    * - struct :c:type:`v4l2_fract`
      - ``timeperframe``
      - This is the desired period between successive frames captured by
	the driver, in seconds. The field is intended to skip frames on
	the driver side, saving I/O bandwidth.

	Applications store here the desired frame period, drivers return
	the actual frame period, which must be greater or equal to the
	nominal frame period determined by the current video standard
	(struct :c:type:`v4l2_standard` ``frameperiod``
	field). Changing the video standard (also implicitly by switching
	the video input) may reset this parameter to the nominal frame
	period. To reset manually applications can just set this field to
	zero.

	Drivers support this function only when they set the
	``V4L2_CAP_TIMEPERFRAME`` flag in the ``capability`` field.
    * - __u32
      - ``extendedmode``
      - Custom (driver specific) streaming parameters. When unused,
	applications and drivers must set this field to zero. Applications
	using this field should check the driver name and version, see
	:ref:`querycap`.
    * - __u32
      - ``readbuffers``
      - Applications set this field to the desired number of buffers used
	internally by the driver in :ref:`read() <func-read>` mode.
	Drivers return the actual number of buffers. When an application
	requests zero buffers, drivers should just return the current
	setting rather than the minimum or an error code. For details see
	:ref:`rw`.
    * - __u32
      - ``reserved``\ [4]
      - Reserved for future extensions. Drivers and applications must set
	the array to zero.



.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. c:type:: v4l2_outputparm

.. flat-table:: struct v4l2_outputparm
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``capability``
      - See :ref:`parm-caps`.
    * - __u32
      - ``outputmode``
      - Set by drivers and applications, see :ref:`parm-flags`.
    * - struct :c:type:`v4l2_fract`
      - ``timeperframe``
      - This is the desired period between successive frames output by the
	driver, in seconds.
    * - :cspan:`2`

	The field is intended to repeat frames on the driver side in
	:ref:`write() <func-write>` mode (in streaming mode timestamps
	can be used to throttle the output), saving I/O bandwidth.

	Applications store here the desired frame period, drivers return
	the actual frame period, which must be greater or equal to the
	nominal frame period determined by the current video standard
	(struct :c:type:`v4l2_standard` ``frameperiod``
	field). Changing the video standard (also implicitly by switching
	the video output) may reset this parameter to the nominal frame
	period. To reset manually applications can just set this field to
	zero.

	Drivers support this function only when they set the
	``V4L2_CAP_TIMEPERFRAME`` flag in the ``capability`` field.
    * - __u32
      - ``extendedmode``
      - Custom (driver specific) streaming parameters. When unused,
	applications and drivers must set this field to zero. Applications
	using this field should check the driver name and version, see
	:ref:`querycap`.
    * - __u32
      - ``writebuffers``
      - Applications set this field to the desired number of buffers used
	internally by the driver in :ref:`write() <func-write>` mode. Drivers
	return the actual number of buffers. When an application requests
	zero buffers, drivers should just return the current setting
	rather than the minimum or an error code. For details see
	:ref:`rw`.
    * - __u32
      - ``reserved``\ [4]
      - Reserved for future extensions. Drivers and applications must set
	the array to zero.



.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. _parm-caps:

.. flat-table:: Streaming Parameters Capabilities
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_CAP_TIMEPERFRAME``
      - 0x1000
      - The frame skipping/repeating controlled by the ``timeperframe``
	field is supported.



.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. _parm-flags:

.. flat-table:: Capture Parameters Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_MODE_HIGHQUALITY``
      - 0x0001
      - High quality imaging mode. High quality mode is intended for still
	imaging applications. The idea is to get the best possible image
	quality that the hardware can deliver. It is not defined how the
	driver writer may achieve that; it will depend on the hardware and
	the ingenuity of the driver writer. High quality mode is a
	different mode from the regular motion video capture modes. In
	high quality mode:

	-  The driver may be able to capture higher resolutions than for
	   motion capture.

	-  The driver may support fewer pixel formats than motion capture
	   (eg; true color).

	-  The driver may capture and arithmetically combine multiple
	   successive fields or frames to remove color edge artifacts and
	   reduce the noise in the video data.

	-  The driver may capture images in slices like a scanner in order
	   to handle larger format images than would otherwise be
	   possible.

	-  An image capture operation may be significantly slower than
	   motion capture.

	-  Moving objects in the image might have excessive motion blur.

	-  Capture might only work through the :ref:`read() <func-read>` call.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

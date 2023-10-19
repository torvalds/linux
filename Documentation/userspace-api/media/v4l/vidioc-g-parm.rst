.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: V4L

.. _VIDIOC_G_PARM:

**********************************
ioctl VIDIOC_G_PARM, VIDIOC_S_PARM
**********************************

Name
====

VIDIOC_G_PARM - VIDIOC_S_PARM - Get or set streaming parameters

Synopsis
========

.. c:macro:: VIDIOC_G_PARM

``int ioctl(int fd, VIDIOC_G_PARM, v4l2_streamparm *argp)``

.. c:macro:: VIDIOC_S_PARM

``int ioctl(int fd, VIDIOC_S_PARM, v4l2_streamparm *argp)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``argp``
    Pointer to struct :c:type:`v4l2_streamparm`.

Description
===========

Applications can request a different frame interval. The capture or
output device will be reconfigured to support the requested frame
interval if possible. Optionally drivers may choose to skip or
repeat frames to achieve the requested frame interval.

For stateful encoders (see :ref:`encoder`) this represents the
frame interval that is typically embedded in the encoded video stream.

Changing the frame interval shall never change the format. Changing the
format, on the other hand, may change the frame interval.

Further these ioctls can be used to determine the number of buffers used
internally by a driver in read/write mode. For implications see the
section discussing the :c:func:`read()` function.

To get and set the streaming parameters applications call the
:ref:`VIDIOC_G_PARM <VIDIOC_G_PARM>` and
:ref:`VIDIOC_S_PARM <VIDIOC_G_PARM>` ioctl, respectively. They take a
pointer to a struct :c:type:`v4l2_streamparm` which contains a
union holding separate parameters for input and output devices.

.. tabularcolumns:: |p{3.7cm}|p{3.5cm}|p{10.1cm}|

.. c:type:: v4l2_streamparm

.. flat-table:: struct v4l2_streamparm
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``type``
      - The buffer (stream) type, same as struct
	:c:type:`v4l2_format` ``type``, set by the
	application. See :c:type:`v4l2_buf_type`.
    * - union {
      - ``parm``
    * - struct :c:type:`v4l2_captureparm`
      - ``capture``
      - Parameters for capture devices, used when ``type`` is
	``V4L2_BUF_TYPE_VIDEO_CAPTURE`` or
	``V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE``.
    * - struct :c:type:`v4l2_outputparm`
      - ``output``
      - Parameters for output devices, used when ``type`` is
	``V4L2_BUF_TYPE_VIDEO_OUTPUT`` or ``V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE``.
    * - __u8
      - ``raw_data``\ [200]
      - A place holder for future extensions.
    * - }


.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.5cm}|

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
	the driver, in seconds.
    * - :cspan:`2`

	This will configure the speed at which the video source (e.g. a sensor)
	generates video frames. If the speed is fixed, then the driver may
	choose to skip or repeat frames in order to achieve the requested
	frame rate.

	For stateful encoders (see :ref:`encoder`) this represents the
	frame interval that is typically embedded in the encoded video stream.

	Applications store here the desired frame period, drivers return
	the actual frame period.

	Changing the video standard (also implicitly by switching
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
	internally by the driver in :c:func:`read()` mode.
	Drivers return the actual number of buffers. When an application
	requests zero buffers, drivers should just return the current
	setting rather than the minimum or an error code. For details see
	:ref:`rw`.
    * - __u32
      - ``reserved``\ [4]
      - Reserved for future extensions. Drivers and applications must set
	the array to zero.


.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.5cm}|

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
	:c:func:`write()` mode (in streaming mode timestamps
	can be used to throttle the output), saving I/O bandwidth.

	For stateful encoders (see :ref:`encoder`) this represents the
	frame interval that is typically embedded in the encoded video stream
	and it provides a hint to the encoder of the speed at which raw
	frames are queued up to the encoder.

	Applications store here the desired frame period, drivers return
	the actual frame period.

	Changing the video standard (also implicitly by switching
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
	internally by the driver in :c:func:`write()` mode. Drivers
	return the actual number of buffers. When an application requests
	zero buffers, drivers should just return the current setting
	rather than the minimum or an error code. For details see
	:ref:`rw`.
    * - __u32
      - ``reserved``\ [4]
      - Reserved for future extensions. Drivers and applications must set
	the array to zero.


.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.5cm}|

.. _parm-caps:

.. flat-table:: Streaming Parameters Capabilities
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_CAP_TIMEPERFRAME``
      - 0x1000
      - The frame period can be modified by setting the ``timeperframe``
	field.


.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.5cm}|

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

	-  Capture might only work through the :c:func:`read()` call.

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

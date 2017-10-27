.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_CROPCAP:

********************
ioctl VIDIOC_CROPCAP
********************

Name
====

VIDIOC_CROPCAP - Information about the video cropping and scaling abilities


Synopsis
========

.. c:function:: int ioctl( int fd, VIDIOC_CROPCAP, struct v4l2_cropcap *argp )
    :name: VIDIOC_CROPCAP


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``
    Pointer to struct :c:type:`v4l2_cropcap`.


Description
===========

Applications use this function to query the cropping limits, the pixel
aspect of images and to calculate scale factors. They set the ``type``
field of a v4l2_cropcap structure to the respective buffer (stream)
type and call the :ref:`VIDIOC_CROPCAP` ioctl with a pointer to this
structure. Drivers fill the rest of the structure. The results are
constant except when switching the video standard. Remember this switch
can occur implicit when switching the video input or output.

This ioctl must be implemented for video capture or output devices that
support cropping and/or scaling and/or have non-square pixels, and for
overlay devices.

.. c:type:: v4l2_cropcap

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct v4l2_cropcap
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``type``
      - Type of the data stream, set by the application. Only these types
	are valid here: ``V4L2_BUF_TYPE_VIDEO_CAPTURE``, ``V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE``,
	``V4L2_BUF_TYPE_VIDEO_OUTPUT``, ``V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE`` and
	``V4L2_BUF_TYPE_VIDEO_OVERLAY``. See :c:type:`v4l2_buf_type` and the note above.
    * - struct :ref:`v4l2_rect <v4l2-rect-crop>`
      - ``bounds``
      - Defines the window within capturing or output is possible, this
	may exclude for example the horizontal and vertical blanking
	areas. The cropping rectangle cannot exceed these limits. Width
	and height are defined in pixels, the driver writer is free to
	choose origin and units of the coordinate system in the analog
	domain.
    * - struct :ref:`v4l2_rect <v4l2-rect-crop>`
      - ``defrect``
      - Default cropping rectangle, it shall cover the "whole picture".
	Assuming pixel aspect 1/1 this could be for example a 640 × 480
	rectangle for NTSC, a 768 × 576 rectangle for PAL and SECAM
	centered over the active picture area. The same co-ordinate system
	as for ``bounds`` is used.
    * - struct :c:type:`v4l2_fract`
      - ``pixelaspect``
      - This is the pixel aspect (y / x) when no scaling is applied, the
	ratio of the actual sampling frequency and the frequency required
	to get square pixels.

	When cropping coordinates refer to square pixels, the driver sets
	``pixelaspect`` to 1/1. Other common values are 54/59 for PAL and
	SECAM, 11/10 for NTSC sampled according to [:ref:`itu601`].

.. note::
   Unfortunately in the case of multiplanar buffer types
   (``V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE`` and ``V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE``)
   this API was messed up with regards to how the :c:type:`v4l2_cropcap` ``type`` field
   should be filled in. Some drivers only accepted the ``_MPLANE`` buffer type while
   other drivers only accepted a non-multiplanar buffer type (i.e. without the
   ``_MPLANE`` at the end).

   Starting with kernel 4.13 both variations are allowed.



.. _v4l2-rect-crop:

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct v4l2_rect
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __s32
      - ``left``
      - Horizontal offset of the top, left corner of the rectangle, in
	pixels.
    * - __s32
      - ``top``
      - Vertical offset of the top, left corner of the rectangle, in
	pixels.
    * - __u32
      - ``width``
      - Width of the rectangle, in pixels.
    * - __u32
      - ``height``
      - Height of the rectangle, in pixels.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The struct :c:type:`v4l2_cropcap` ``type`` is
    invalid.

ENODATA
    Cropping is not supported for this input or output.

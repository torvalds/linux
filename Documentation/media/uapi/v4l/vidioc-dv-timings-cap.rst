.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_DV_TIMINGS_CAP:

*********************************************************
ioctl VIDIOC_DV_TIMINGS_CAP, VIDIOC_SUBDEV_DV_TIMINGS_CAP
*********************************************************

Name
====

VIDIOC_DV_TIMINGS_CAP - VIDIOC_SUBDEV_DV_TIMINGS_CAP - The capabilities of the Digital Video receiver/transmitter


Synopsis
========

.. c:function:: int ioctl( int fd, VIDIOC_DV_TIMINGS_CAP, struct v4l2_dv_timings_cap *argp )
    :name: VIDIOC_DV_TIMINGS_CAP

.. c:function:: int ioctl( int fd, VIDIOC_SUBDEV_DV_TIMINGS_CAP, struct v4l2_dv_timings_cap *argp )
    :name: VIDIOC_SUBDEV_DV_TIMINGS_CAP


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``


Description
===========

To query the capabilities of the DV receiver/transmitter applications
initialize the ``pad`` field to 0, zero the reserved array of struct
:ref:`v4l2_dv_timings_cap <v4l2-dv-timings-cap>` and call the
``VIDIOC_DV_TIMINGS_CAP`` ioctl on a video node and the driver will fill
in the structure.

.. note::

   Drivers may return different values after
   switching the video input or output.

When implemented by the driver DV capabilities of subdevices can be
queried by calling the ``VIDIOC_SUBDEV_DV_TIMINGS_CAP`` ioctl directly
on a subdevice node. The capabilities are specific to inputs (for DV
receivers) or outputs (for DV transmitters), applications must specify
the desired pad number in the struct
:ref:`v4l2_dv_timings_cap <v4l2-dv-timings-cap>` ``pad`` field and
zero the ``reserved`` array. Attempts to query capabilities on a pad
that doesn't support them will return an ``EINVAL`` error code.


.. tabularcolumns:: |p{1.2cm}|p{3.0cm}|p{13.3cm}|

.. _v4l2-bt-timings-cap:

.. flat-table:: struct v4l2_bt_timings_cap
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``min_width``

       -  Minimum width of the active video in pixels.

    -  .. row 2

       -  __u32

       -  ``max_width``

       -  Maximum width of the active video in pixels.

    -  .. row 3

       -  __u32

       -  ``min_height``

       -  Minimum height of the active video in lines.

    -  .. row 4

       -  __u32

       -  ``max_height``

       -  Maximum height of the active video in lines.

    -  .. row 5

       -  __u64

       -  ``min_pixelclock``

       -  Minimum pixelclock frequency in Hz.

    -  .. row 6

       -  __u64

       -  ``max_pixelclock``

       -  Maximum pixelclock frequency in Hz.

    -  .. row 7

       -  __u32

       -  ``standards``

       -  The video standard(s) supported by the hardware. See
	  :ref:`dv-bt-standards` for a list of standards.

    -  .. row 8

       -  __u32

       -  ``capabilities``

       -  Several flags giving more information about the capabilities. See
	  :ref:`dv-bt-cap-capabilities` for a description of the flags.

    -  .. row 9

       -  __u32

       -  ``reserved``\ [16]

       -  Reserved for future extensions.
	  Drivers must set the array to zero.



.. tabularcolumns:: |p{1.0cm}|p{3.5cm}|p{3.5cm}|p{9.5cm}|

.. _v4l2-dv-timings-cap:

.. flat-table:: struct v4l2_dv_timings_cap
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2 1


    -  .. row 1

       -  __u32

       -  ``type``

       -  Type of DV timings as listed in :ref:`dv-timing-types`.

    -  .. row 2

       -  __u32

       -  ``pad``

       -  Pad number as reported by the media controller API. This field is
	  only used when operating on a subdevice node. When operating on a
	  video node applications must set this field to zero.

    -  .. row 3

       -  __u32

       -  ``reserved``\ [2]

       -  Reserved for future extensions.

	  Drivers and applications must set the array to zero.

    -  .. row 4

       -  union

       -
       -

    -  .. row 5

       -
       -  struct :ref:`v4l2_bt_timings_cap <v4l2-bt-timings-cap>`

       -  ``bt``

       -  BT.656/1120 timings capabilities of the hardware.

    -  .. row 6

       -
       -  __u32

       -  ``raw_data``\ [32]

       -

.. tabularcolumns:: |p{7.0cm}|p{10.5cm}|

.. _dv-bt-cap-capabilities:

.. flat-table:: DV BT Timing capabilities
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  Flag

       -  Description

    -  .. row 2

       -
       -

    -  .. row 3

       -  ``V4L2_DV_BT_CAP_INTERLACED``

       -  Interlaced formats are supported.

    -  .. row 4

       -  ``V4L2_DV_BT_CAP_PROGRESSIVE``

       -  Progressive formats are supported.

    -  .. row 5

       -  ``V4L2_DV_BT_CAP_REDUCED_BLANKING``

       -  CVT/GTF specific: the timings can make use of reduced blanking
	  (CVT) or the 'Secondary GTF' curve (GTF).

    -  .. row 6

       -  ``V4L2_DV_BT_CAP_CUSTOM``

       -  Can support non-standard timings, i.e. timings not belonging to
	  the standards set in the ``standards`` field.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

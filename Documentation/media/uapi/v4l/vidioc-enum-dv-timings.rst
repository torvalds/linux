.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_ENUM_DV_TIMINGS:

***********************************************************
ioctl VIDIOC_ENUM_DV_TIMINGS, VIDIOC_SUBDEV_ENUM_DV_TIMINGS
***********************************************************

Name
====

VIDIOC_ENUM_DV_TIMINGS - VIDIOC_SUBDEV_ENUM_DV_TIMINGS - Enumerate supported Digital Video timings


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, struct v4l2_enum_dv_timings *argp )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``request``
    VIDIOC_ENUM_DV_TIMINGS, VIDIOC_SUBDEV_ENUM_DV_TIMINGS

``argp``


Description
===========

While some DV receivers or transmitters support a wide range of timings,
others support only a limited number of timings. With this ioctl
applications can enumerate a list of known supported timings. Call
:ref:`VIDIOC_DV_TIMINGS_CAP` to check if it
also supports other standards or even custom timings that are not in
this list.

To query the available timings, applications initialize the ``index``
field, set the ``pad`` field to 0, zero the reserved array of struct
:ref:`v4l2_enum_dv_timings <v4l2-enum-dv-timings>` and call the
``VIDIOC_ENUM_DV_TIMINGS`` ioctl on a video node with a pointer to this
structure. Drivers fill the rest of the structure or return an ``EINVAL``
error code when the index is out of bounds. To enumerate all supported
DV timings, applications shall begin at index zero, incrementing by one
until the driver returns ``EINVAL``.

.. note::

   Drivers may enumerate a different set of DV timings after
   switching the video input or output.

When implemented by the driver DV timings of subdevices can be queried
by calling the ``VIDIOC_SUBDEV_ENUM_DV_TIMINGS`` ioctl directly on a
subdevice node. The DV timings are specific to inputs (for DV receivers)
or outputs (for DV transmitters), applications must specify the desired
pad number in the struct
:ref:`v4l2_enum_dv_timings <v4l2-enum-dv-timings>` ``pad`` field.
Attempts to enumerate timings on a pad that doesn't support them will
return an ``EINVAL`` error code.


.. _v4l2-enum-dv-timings:

.. flat-table:: struct v4l2_enum_dv_timings
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``index``

       -  Number of the DV timings, set by the application.

    -  .. row 2

       -  __u32

       -  ``pad``

       -  Pad number as reported by the media controller API. This field is
	  only used when operating on a subdevice node. When operating on a
	  video node applications must set this field to zero.

    -  .. row 3

       -  __u32

       -  ``reserved``\ [2]

       -  Reserved for future extensions. Drivers and applications must set
	  the array to zero.

    -  .. row 4

       -  struct :ref:`v4l2_dv_timings <v4l2-dv-timings>`

       -  ``timings``

       -  The timings.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The struct :ref:`v4l2_enum_dv_timings <v4l2-enum-dv-timings>`
    ``index`` is out of bounds or the ``pad`` number is invalid.

ENODATA
    Digital video presets are not supported for this input or output.

.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_SUBDEV_G_SELECTION:

**********************************************************
ioctl VIDIOC_SUBDEV_G_SELECTION, VIDIOC_SUBDEV_S_SELECTION
**********************************************************

Name
====

VIDIOC_SUBDEV_G_SELECTION - VIDIOC_SUBDEV_S_SELECTION - Get or set selection rectangles on a subdev pad


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, struct v4l2_subdev_selection *argp )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``request``
    VIDIOC_SUBDEV_G_SELECTION, VIDIOC_SUBDEV_S_SELECTION

``argp``


Description
===========

The selections are used to configure various image processing
functionality performed by the subdevs which affect the image size. This
currently includes cropping, scaling and composition.

The selection API replaces
:ref:`the old subdev crop API <VIDIOC_SUBDEV_G_CROP>`. All the
function of the crop API, and more, are supported by the selections API.

See :ref:`subdev` for more information on how each selection target
affects the image processing pipeline inside the subdevice.


Types of selection targets
--------------------------

There are two types of selection targets: actual and bounds. The actual
targets are the targets which configure the hardware. The BOUNDS target
will return a rectangle that contain all possible actual rectangles.


Discovering supported features
------------------------------

To discover which targets are supported, the user can perform
``VIDIOC_SUBDEV_G_SELECTION`` on them. Any unsupported target will
return ``EINVAL``.

Selection targets and flags are documented in
:ref:`v4l2-selections-common`.


.. _v4l2-subdev-selection:

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct v4l2_subdev_selection
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``which``

       -  Active or try selection, from enum
	  :ref:`v4l2_subdev_format_whence <v4l2-subdev-format-whence>`.

    -  .. row 2

       -  __u32

       -  ``pad``

       -  Pad number as reported by the media framework.

    -  .. row 3

       -  __u32

       -  ``target``

       -  Target selection rectangle. See :ref:`v4l2-selections-common`.

    -  .. row 4

       -  __u32

       -  ``flags``

       -  Flags. See :ref:`v4l2-selection-flags`.

    -  .. row 5

       -  struct :ref:`v4l2_rect <v4l2-rect>`

       -  ``r``

       -  Selection rectangle, in pixels.

    -  .. row 6

       -  __u32

       -  ``reserved``\ [8]

       -  Reserved for future extensions. Applications and drivers must set
	  the array to zero.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EBUSY
    The selection rectangle can't be changed because the pad is
    currently busy. This can be caused, for instance, by an active video
    stream on the pad. The ioctl must not be retried without performing
    another action to fix the problem first. Only returned by
    ``VIDIOC_SUBDEV_S_SELECTION``

EINVAL
    The struct :ref:`v4l2_subdev_selection <v4l2-subdev-selection>`
    ``pad`` references a non-existing pad, the ``which`` field
    references a non-existing format, or the selection target is not
    supported on the given subdev pad.

.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _VIDIOC_ENUM_FRAMEINTERVALS:

********************************
ioctl VIDIOC_ENUM_FRAMEINTERVALS
********************************

Name
====

VIDIOC_ENUM_FRAMEINTERVALS - Enumerate frame intervals


Synopsis
========

.. c:function:: int ioctl( int fd, VIDIOC_ENUM_FRAMEINTERVALS, struct v4l2_frmivalenum *argp )
    :name: VIDIOC_ENUM_FRAMEINTERVALS


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``
    Pointer to struct :c:type:`v4l2_frmivalenum`
    that contains a pixel format and size and receives a frame interval.


Description
===========

This ioctl allows applications to enumerate all frame intervals that the
device supports for the given pixel format and frame size.

The supported pixel formats and frame sizes can be obtained by using the
:ref:`VIDIOC_ENUM_FMT` and
:ref:`VIDIOC_ENUM_FRAMESIZES` functions.

The return value and the content of the ``v4l2_frmivalenum.type`` field
depend on the type of frame intervals the device supports. Here are the
semantics of the function for the different cases:

-  **Discrete:** The function returns success if the given index value
   (zero-based) is valid. The application should increase the index by
   one for each call until ``EINVAL`` is returned. The
   `v4l2_frmivalenum.type` field is set to
   `V4L2_FRMIVAL_TYPE_DISCRETE` by the driver. Of the union only
   the `discrete` member is valid.

-  **Step-wise:** The function returns success if the given index value
   is zero and ``EINVAL`` for any other index value. The
   ``v4l2_frmivalenum.type`` field is set to
   ``V4L2_FRMIVAL_TYPE_STEPWISE`` by the driver. Of the union only the
   ``stepwise`` member is valid.

-  **Continuous:** This is a special case of the step-wise type above.
   The function returns success if the given index value is zero and
   ``EINVAL`` for any other index value. The ``v4l2_frmivalenum.type``
   field is set to ``V4L2_FRMIVAL_TYPE_CONTINUOUS`` by the driver. Of
   the union only the ``stepwise`` member is valid and the ``step``
   value is set to 1.

When the application calls the function with index zero, it must check
the ``type`` field to determine the type of frame interval enumeration
the device supports. Only for the ``V4L2_FRMIVAL_TYPE_DISCRETE`` type
does it make sense to increase the index value to receive more frame
intervals.

.. note::

   The order in which the frame intervals are returned has no
   special meaning. In particular does it not say anything about potential
   default frame intervals.

Applications can assume that the enumeration data does not change
without any interaction from the application itself. This means that the
enumeration data is consistent if the application does not perform any
other ioctl calls while it runs the frame interval enumeration.

.. note::

   **Frame intervals and frame rates:** The V4L2 API uses frame
   intervals instead of frame rates. Given the frame interval the frame
   rate can be computed as follows:

   ::

       frame_rate = 1 / frame_interval


Structs
=======

In the structs below, *IN* denotes a value that has to be filled in by
the application, *OUT* denotes values that the driver fills in. The
application should zero out all members except for the *IN* fields.


.. c:type:: v4l2_frmival_stepwise

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct v4l2_frmival_stepwise
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - struct :c:type:`v4l2_fract`
      - ``min``
      - Minimum frame interval [s].
    * - struct :c:type:`v4l2_fract`
      - ``max``
      - Maximum frame interval [s].
    * - struct :c:type:`v4l2_fract`
      - ``step``
      - Frame interval step size [s].



.. c:type:: v4l2_frmivalenum

.. tabularcolumns:: |p{1.8cm}|p{4.4cm}|p{2.4cm}|p{8.9cm}|

.. flat-table:: struct v4l2_frmivalenum
    :header-rows:  0
    :stub-columns: 0

    * - __u32
      - ``index``
      -
      - IN: Index of the given frame interval in the enumeration.
    * - __u32
      - ``pixel_format``
      -
      - IN: Pixel format for which the frame intervals are enumerated.
    * - __u32
      - ``width``
      -
      - IN: Frame width for which the frame intervals are enumerated.
    * - __u32
      - ``height``
      -
      - IN: Frame height for which the frame intervals are enumerated.
    * - __u32
      - ``type``
      -
      - OUT: Frame interval type the device supports.
    * - union
      -
      -
      - OUT: Frame interval with the given index.
    * -
      - struct :c:type:`v4l2_fract`
      - ``discrete``
      - Frame interval [s].
    * -
      - struct :c:type:`v4l2_frmival_stepwise`
      - ``stepwise``
      -
    * - __u32
      - ``reserved[2]``
      -
      - Reserved space for future use. Must be zeroed by drivers and
	applications.



Enums
=====


.. c:type:: v4l2_frmivaltypes

.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. flat-table:: enum v4l2_frmivaltypes
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_FRMIVAL_TYPE_DISCRETE``
      - 1
      - Discrete frame interval.
    * - ``V4L2_FRMIVAL_TYPE_CONTINUOUS``
      - 2
      - Continuous frame interval.
    * - ``V4L2_FRMIVAL_TYPE_STEPWISE``
      - 3
      - Step-wise defined frame interval.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

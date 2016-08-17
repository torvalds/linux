.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_ENUM_FRAMESIZES:

****************************
ioctl VIDIOC_ENUM_FRAMESIZES
****************************

Name
====

VIDIOC_ENUM_FRAMESIZES - Enumerate frame sizes


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, struct v4l2_frmsizeenum *argp )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``request``
    VIDIOC_ENUM_FRAMESIZES

``argp``
    Pointer to a struct :ref:`v4l2_frmsizeenum <v4l2-frmsizeenum>`
    that contains an index and pixel format and receives a frame width
    and height.


Description
===========

This ioctl allows applications to enumerate all frame sizes (i. e. width
and height in pixels) that the device supports for the given pixel
format.

The supported pixel formats can be obtained by using the
:ref:`VIDIOC_ENUM_FMT` function.

The return value and the content of the ``v4l2_frmsizeenum.type`` field
depend on the type of frame sizes the device supports. Here are the
semantics of the function for the different cases:

-  **Discrete:** The function returns success if the given index value
   (zero-based) is valid. The application should increase the index by
   one for each call until ``EINVAL`` is returned. The
   ``v4l2_frmsizeenum.type`` field is set to
   ``V4L2_FRMSIZE_TYPE_DISCRETE`` by the driver. Of the union only the
   ``discrete`` member is valid.

-  **Step-wise:** The function returns success if the given index value
   is zero and ``EINVAL`` for any other index value. The
   ``v4l2_frmsizeenum.type`` field is set to
   ``V4L2_FRMSIZE_TYPE_STEPWISE`` by the driver. Of the union only the
   ``stepwise`` member is valid.

-  **Continuous:** This is a special case of the step-wise type above.
   The function returns success if the given index value is zero and
   ``EINVAL`` for any other index value. The ``v4l2_frmsizeenum.type``
   field is set to ``V4L2_FRMSIZE_TYPE_CONTINUOUS`` by the driver. Of
   the union only the ``stepwise`` member is valid and the
   ``step_width`` and ``step_height`` values are set to 1.

When the application calls the function with index zero, it must check
the ``type`` field to determine the type of frame size enumeration the
device supports. Only for the ``V4L2_FRMSIZE_TYPE_DISCRETE`` type does
it make sense to increase the index value to receive more frame sizes.

.. note::

   The order in which the frame sizes are returned has no special
   meaning. In particular does it not say anything about potential default
   format sizes.

Applications can assume that the enumeration data does not change
without any interaction from the application itself. This means that the
enumeration data is consistent if the application does not perform any
other ioctl calls while it runs the frame size enumeration.


Structs
=======

In the structs below, *IN* denotes a value that has to be filled in by
the application, *OUT* denotes values that the driver fills in. The
application should zero out all members except for the *IN* fields.


.. _v4l2-frmsize-discrete:

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct v4l2_frmsize_discrete
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``width``

       -  Width of the frame [pixel].

    -  .. row 2

       -  __u32

       -  ``height``

       -  Height of the frame [pixel].



.. _v4l2-frmsize-stepwise:

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct v4l2_frmsize_stepwise
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``min_width``

       -  Minimum frame width [pixel].

    -  .. row 2

       -  __u32

       -  ``max_width``

       -  Maximum frame width [pixel].

    -  .. row 3

       -  __u32

       -  ``step_width``

       -  Frame width step size [pixel].

    -  .. row 4

       -  __u32

       -  ``min_height``

       -  Minimum frame height [pixel].

    -  .. row 5

       -  __u32

       -  ``max_height``

       -  Maximum frame height [pixel].

    -  .. row 6

       -  __u32

       -  ``step_height``

       -  Frame height step size [pixel].



.. _v4l2-frmsizeenum:

.. flat-table:: struct v4l2_frmsizeenum
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  __u32

       -  ``index``

       -
       -  IN: Index of the given frame size in the enumeration.

    -  .. row 2

       -  __u32

       -  ``pixel_format``

       -
       -  IN: Pixel format for which the frame sizes are enumerated.

    -  .. row 3

       -  __u32

       -  ``type``

       -
       -  OUT: Frame size type the device supports.

    -  .. row 4

       -  union

       -
       -
       -  OUT: Frame size with the given index.

    -  .. row 5

       -
       -  struct :ref:`v4l2_frmsize_discrete <v4l2-frmsize-discrete>`

       -  ``discrete``

       -

    -  .. row 6

       -
       -  struct :ref:`v4l2_frmsize_stepwise <v4l2-frmsize-stepwise>`

       -  ``stepwise``

       -

    -  .. row 7

       -  __u32

       -  ``reserved[2]``

       -
       -  Reserved space for future use. Must be zeroed by drivers and
	  applications.



Enums
=====


.. _v4l2-frmsizetypes:

.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. flat-table:: enum v4l2_frmsizetypes
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  ``V4L2_FRMSIZE_TYPE_DISCRETE``

       -  1

       -  Discrete frame size.

    -  .. row 2

       -  ``V4L2_FRMSIZE_TYPE_CONTINUOUS``

       -  2

       -  Continuous frame size.

    -  .. row 3

       -  ``V4L2_FRMSIZE_TYPE_STEPWISE``

       -  3

       -  Step-wise defined frame size.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

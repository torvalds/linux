.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

*************
Configuration
*************

Applications can use the :ref:`selection API <VIDIOC_G_SELECTION>` to
select an area in a video signal or a buffer, and to query for default
settings and hardware limits.

Video hardware can have various cropping, composing and scaling
limitations. It may only scale up or down, support only discrete scaling
factors, or have different scaling abilities in the horizontal and
vertical directions. Also it may not support scaling at all. At the same
time the cropping/composing rectangles may have to be aligned, and both
the source and the sink may have arbitrary upper and lower size limits.
Therefore, as usual, drivers are expected to adjust the requested
parameters and return the actual values selected. An application can
control the rounding behaviour using
:ref:`constraint flags <v4l2-selection-flags>`.


Configuration of video capture
==============================

See figure :ref:`sel-targets-capture` for examples of the selection
targets available for a video capture device. It is recommended to
configure the cropping targets before to the composing targets.

The range of coordinates of the top left corner, width and height of
areas that can be sampled is given by the ``V4L2_SEL_TGT_CROP_BOUNDS``
target. It is recommended for the driver developers to put the top/left
corner at position ``(0,0)``. The rectangle's coordinates are expressed
in pixels.

The top left corner, width and height of the source rectangle, that is
the area actually sampled, is given by the ``V4L2_SEL_TGT_CROP`` target.
It uses the same coordinate system as ``V4L2_SEL_TGT_CROP_BOUNDS``. The
active cropping area must lie completely inside the capture boundaries.
The driver may further adjust the requested size and/or position
according to hardware limitations.

Each capture device has a default source rectangle, given by the
``V4L2_SEL_TGT_CROP_DEFAULT`` target. This rectangle shall cover what the
driver writer considers the complete picture. Drivers shall set the
active crop rectangle to the default when the driver is first loaded,
but not later.

The composing targets refer to a memory buffer. The limits of composing
coordinates are obtained using ``V4L2_SEL_TGT_COMPOSE_BOUNDS``. All
coordinates are expressed in pixels. The rectangle's top/left corner
must be located at position ``(0,0)``. The width and height are equal to
the image size set by :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>`.

The part of a buffer into which the image is inserted by the hardware is
controlled by the ``V4L2_SEL_TGT_COMPOSE`` target. The rectangle's
coordinates are also expressed in the same coordinate system as the
bounds rectangle. The composing rectangle must lie completely inside
bounds rectangle. The driver must adjust the composing rectangle to fit
to the bounding limits. Moreover, the driver can perform other
adjustments according to hardware limitations. The application can
control rounding behaviour using
:ref:`constraint flags <v4l2-selection-flags>`.

For capture devices the default composing rectangle is queried using
``V4L2_SEL_TGT_COMPOSE_DEFAULT``. It is usually equal to the bounding
rectangle.

The part of a buffer that is modified by the hardware is given by
``V4L2_SEL_TGT_COMPOSE_PADDED``. It contains all pixels defined using
``V4L2_SEL_TGT_COMPOSE`` plus all padding data modified by hardware
during insertion process. All pixels outside this rectangle *must not*
be changed by the hardware. The content of pixels that lie inside the
padded area but outside active area is undefined. The application can
use the padded and active rectangles to detect where the rubbish pixels
are located and remove them if needed.


Configuration of video output
=============================

For output devices targets and ioctls are used similarly to the video
capture case. The *composing* rectangle refers to the insertion of an
image into a video signal. The cropping rectangles refer to a memory
buffer. It is recommended to configure the composing targets before to
the cropping targets.

The cropping targets refer to the memory buffer that contains an image
to be inserted into a video signal or graphical screen. The limits of
cropping coordinates are obtained using ``V4L2_SEL_TGT_CROP_BOUNDS``.
All coordinates are expressed in pixels. The top/left corner is always
point ``(0,0)``. The width and height is equal to the image size
specified using :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl.

The top left corner, width and height of the source rectangle, that is
the area from which image date are processed by the hardware, is given
by the ``V4L2_SEL_TGT_CROP``. Its coordinates are expressed in the
same coordinate system as the bounds rectangle. The active cropping area
must lie completely inside the crop boundaries and the driver may
further adjust the requested size and/or position according to hardware
limitations.

For output devices the default cropping rectangle is queried using
``V4L2_SEL_TGT_CROP_DEFAULT``. It is usually equal to the bounding
rectangle.

The part of a video signal or graphics display where the image is
inserted by the hardware is controlled by ``V4L2_SEL_TGT_COMPOSE``
target. The rectangle's coordinates are expressed in pixels. The
composing rectangle must lie completely inside the bounds rectangle. The
driver must adjust the area to fit to the bounding limits. Moreover, the
driver can perform other adjustments according to hardware limitations.

The device has a default composing rectangle, given by the
``V4L2_SEL_TGT_COMPOSE_DEFAULT`` target. This rectangle shall cover what
the driver writer considers the complete picture. It is recommended for
the driver developers to put the top/left corner at position ``(0,0)``.
Drivers shall set the active composing rectangle to the default one when
the driver is first loaded.

The devices may introduce additional content to video signal other than
an image from memory buffers. It includes borders around an image.
However, such a padded area is driver-dependent feature not covered by
this document. Driver developers are encouraged to keep padded rectangle
equal to active one. The padded target is accessed by the
``V4L2_SEL_TGT_COMPOSE_PADDED`` identifier. It must contain all pixels
from the ``V4L2_SEL_TGT_COMPOSE`` target.


Scaling control
===============

An application can detect if scaling is performed by comparing the width
and the height of rectangles obtained using ``V4L2_SEL_TGT_CROP`` and
``V4L2_SEL_TGT_COMPOSE`` targets. If these are not equal then the
scaling is applied. The application can compute the scaling ratios using
these values.

.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _image-source-controls:

******************************
Image Source Control Reference
******************************

The Image Source control class is intended for low-level control of
image source devices such as image sensors. The devices feature an
analogue to digital converter and a bus transmitter to transmit the
image data out of the device.


.. _image-source-control-id:

Image Source Control IDs
========================

``V4L2_CID_IMAGE_SOURCE_CLASS (class)``
    The IMAGE_SOURCE class descriptor.

``V4L2_CID_VBLANK (integer)``
    Vertical blanking. The idle period after every frame during which no
    image data is produced. The unit of vertical blanking is a line.
    Every line has length of the image width plus horizontal blanking at
    the pixel rate defined by ``V4L2_CID_PIXEL_RATE`` control in the
    same sub-device.

``V4L2_CID_HBLANK (integer)``
    Horizontal blanking. The idle period after every line of image data
    during which no image data is produced. The unit of horizontal
    blanking is pixels.

``V4L2_CID_ANALOGUE_GAIN (integer)``
    Analogue gain is gain affecting all colour components in the pixel
    matrix. The gain operation is performed in the analogue domain
    before A/D conversion.

``V4L2_CID_TEST_PATTERN_RED (integer)``
    Test pattern red colour component.

``V4L2_CID_TEST_PATTERN_GREENR (integer)``
    Test pattern green (next to red) colour component.

``V4L2_CID_TEST_PATTERN_BLUE (integer)``
    Test pattern blue colour component.

``V4L2_CID_TEST_PATTERN_GREENB (integer)``
    Test pattern green (next to blue) colour component.

``V4L2_CID_UNIT_CELL_SIZE (struct)``
    This control returns the unit cell size in nanometers. The struct
    :c:type:`v4l2_area` provides the width and the height in separate
    fields to take into consideration asymmetric pixels.
    This control does not take into consideration any possible hardware
    binning.
    The unit cell consists of the whole area of the pixel, sensitive and
    non-sensitive.
    This control is required for automatic calibration of sensors/cameras.

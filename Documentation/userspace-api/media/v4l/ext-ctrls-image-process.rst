.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _image-process-controls:

*******************************
Image Process Control Reference
*******************************

The Image Process control class is intended for low-level control of
image processing functions. Unlike ``V4L2_CID_IMAGE_SOURCE_CLASS``, the
controls in this class affect processing the image, and do not control
capturing of it.


.. _image-process-control-id:

Image Process Control IDs
=========================

``V4L2_CID_IMAGE_PROC_CLASS (class)``
    The IMAGE_PROC class descriptor.

``V4L2_CID_LINK_FREQ (integer menu)``
    Data bus frequency. Together with the media bus pixel code, bus type
    (clock cycles per sample), the data bus frequency defines the pixel
    rate (``V4L2_CID_PIXEL_RATE``) in the pixel array (or possibly
    elsewhere, if the device is not an image sensor). The frame rate can
    be calculated from the pixel clock, image width and height and
    horizontal and vertical blanking. While the pixel rate control may
    be defined elsewhere than in the subdev containing the pixel array,
    the frame rate cannot be obtained from that information. This is
    because only on the pixel array it can be assumed that the vertical
    and horizontal blanking information is exact: no other blanking is
    allowed in the pixel array. The selection of frame rate is performed
    by selecting the desired horizontal and vertical blanking. The unit
    of this control is Hz.

``V4L2_CID_PIXEL_RATE (64-bit integer)``
    Pixel rate in the source pads of the subdev. This control is
    read-only and its unit is pixels / second.

``V4L2_CID_TEST_PATTERN (menu)``
    Some capture/display/sensor devices have the capability to generate
    test pattern images. These hardware specific test patterns can be
    used to test if a device is working properly.

``V4L2_CID_DEINTERLACING_MODE (menu)``
    The video deinterlacing mode (such as Bob, Weave, ...). The menu items are
    driver specific and are documented in :ref:`uapi-v4l-drivers`.

``V4L2_CID_DIGITAL_GAIN (integer)``
    Digital gain is the value by which all colour components
    are multiplied by. Typically the digital gain applied is the
    control value divided by e.g. 0x100, meaning that to get no
    digital gain the control value needs to be 0x100. The no-gain
    configuration is also typically the default.

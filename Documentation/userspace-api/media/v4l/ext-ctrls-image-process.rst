.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

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

.. _v4l2-cid-link-freq:

``V4L2_CID_LINK_FREQ (integer menu)``
    The frequency of the data bus (e.g. parallel or CSI-2).

.. _v4l2-cid-pixel-rate:

``V4L2_CID_PIXEL_RATE (64-bit integer)``
    Pixel sampling rate in the device's pixel array. This control is
    read-only and its unit is pixels / second.

    Some devices use horizontal and vertical balanking to configure the frame
    rate. The frame rate can be calculated from the pixel rate, analogue crop
    rectangle as well as horizontal and vertical blanking. The pixel rate
    control may be present in a different sub-device than the blanking controls
    and the analogue crop rectangle configuration.

    The configuration of the frame rate is performed by selecting the desired
    horizontal and vertical blanking. The unit of this control is Hz.

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

.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _colorimetry-controls:

*****************************
Colorimetry Control Reference
*****************************

The Colorimetry class includes controls for High Dynamic Range
imaging for representing colors in digital images and video. The
controls should be used for video and image encoding and decoding
as well as in HDMI receivers and transmitters.

Colorimetry Control IDs
-----------------------

.. _colorimetry-control-id:

``V4L2_CID_COLORIMETRY_CLASS (class)``
    The Colorimetry class descriptor. Calling
    :ref:`VIDIOC_QUERYCTRL` for this control will
    return a description of this control class.
